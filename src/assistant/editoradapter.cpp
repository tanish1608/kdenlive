// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "editoradapter.h"
#include "editbatch.h"
#include "proposal.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "project/projectmanager.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>
#include <QFileInfo>
#include <QJsonArray>
#include <QThread>
#include <algorithm>
#include <limits>
#include <numeric>

Montaz::EditorAdapter::EditorAdapter(QObject *parent)
    : QObject(parent)
{
    const auto bin = pCore->projectItemModel();
    connect(bin.get(), &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        if (roles.isEmpty() || std::any_of(roles.begin(), roles.end(),
                                           [](int role) { return role != AbstractProjectItem::DataThumbnail && role != AbstractProjectItem::JobProgress; }))
            invalidate();
    });
    connect(bin.get(), &QAbstractItemModel::rowsInserted, this, [this]() { invalidate(); });
    connect(bin.get(), &QAbstractItemModel::rowsRemoved, this, [this]() { invalidate(); });
    connect(bin.get(), &QAbstractItemModel::modelReset, this, [this]() { invalidate(); });
    connect(pCore.get(), &Core::updateProjectTimecode, this, [this]() { invalidate(); });
}

void Montaz::EditorAdapter::invalidate()
{
    if (m_revision < std::numeric_limits<int>::max()) ++m_revision;
    Q_EMIT invalidated();
}

void Montaz::EditorAdapter::bind()
{
    auto document = pCore->currentDoc();
    auto timeline = document ? pCore->projectManager()->getTimeline() : nullptr;
    if (document == m_document && timeline == m_timeline.lock()) return;
    if (document != m_document) m_applyHealthy = true;
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    m_document = document;
    m_timeline = timeline;
    invalidate();
    if (!document || !timeline) return;
    m_connections << connect(timeline.get(), &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        if (roles.isEmpty() || std::any_of(roles.begin(), roles.end(), [](int role) { return role != TimelineModel::AudioLevelsRole; })) invalidate();
    });
    m_connections << connect(timeline.get(), &QAbstractItemModel::rowsInserted, this, [this]() { invalidate(); });
    m_connections << connect(timeline.get(), &QAbstractItemModel::rowsRemoved, this, [this]() { invalidate(); });
    m_connections << connect(timeline.get(), &QAbstractItemModel::rowsMoved, this, [this]() { invalidate(); });
    m_connections << connect(timeline.get(), &QAbstractItemModel::modelReset, this, [this]() { invalidate(); });
    const auto stack = pCore->projectManager()->undoStack();
    if (stack) m_connections << connect(stack.get(), &QUndoStack::indexChanged, this, [this]() { invalidate(); });
}

void Montaz::EditorAdapter::refreshContext()
{
    bind();
}

QJsonObject Montaz::EditorAdapter::snapshot()
{
    if (QThread::currentThread() != thread()) return {};
    bind();
    const auto timeline = m_timeline.lock();
    if (!m_document || !timeline || !m_applyHealthy || m_revision == std::numeric_limits<int>::max()) return {};
    QJsonArray assets, tracks, occupied;
    auto ids = pCore->projectItemModel()->getAllClipIds();
    std::sort(ids.begin(), ids.end());
    for (const auto &id : ids) {
        const auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (!clip || (clip->clipStatus() != FileStatus::StatusReady && clip->clipStatus() != FileStatus::StatusProxy) ||
            (clip->clipType() != ClipType::AV && clip->clipType() != ClipType::Video && clip->clipType() != ClipType::Audio) || clip->getFramePlaytime() <= 0)
            continue;
        assets.append(QJsonObject{{QStringLiteral("bin_id"), id},
                                  {QStringLiteral("duration_frames"), clip->getFramePlaytime()},
                                  {QStringLiteral("has_video"), clip->hasVideo()},
                                  {QStringLiteral("has_audio"), clip->hasAudio()}});
    }
    for (int row = 0; row < timeline->getTracksCount(); ++row) {
        const int id = timeline->getTrackIndexFromPosition(row);
        tracks.append(QJsonObject{{QStringLiteral("track_id"), QString::number(id)},
                                  {QStringLiteral("kind"), timeline->isAudioTrack(id) ? QStringLiteral("audio") : QStringLiteral("video")},
                                  {QStringLiteral("locked"), timeline->trackIsLocked(id)}});
        const auto trackIndex = timeline->makeTrackIndexFromID(id);
        std::vector<int> sorted;
        for (int child = 0; child < timeline->rowCount(trackIndex); ++child) {
            const int clip = int(timeline->index(child, 0, trackIndex).internalId());
            if (timeline->isClip(clip)) sorted.push_back(clip);
        }
        std::sort(sorted.begin(), sorted.end());
        for (int clip : sorted) {
            occupied.append(QJsonObject{{QStringLiteral("instance_id"), QString::number(clip)},
                                        {QStringLiteral("track_id"), QString::number(id)},
                                        {QStringLiteral("start_frame"), timeline->getClipPosition(clip)},
                                        {QStringLiteral("duration_frames"), timeline->getClipPlaytime(clip)}});
        }
    }
    const auto fps = pCore->getProjectFpsInfo();
    const int divisor = std::gcd(fps.first, fps.second);
    if (divisor <= 0) return {};
    const QJsonObject snapshot{
        {QStringLiteral("project_id"), m_document->uuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("sequence_id"), timeline->uuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("revision"), m_revision},
        {QStringLiteral("frame_rate"), QJsonObject{{QStringLiteral("numerator"), fps.first / divisor}, {QStringLiteral("denominator"), fps.second / divisor}}},
        {QStringLiteral("assets"), assets},
        {QStringLiteral("tracks"), tracks},
        {QStringLiteral("occupied"), occupied}};
    return validateSnapshot(snapshot).isEmpty() ? snapshot : QJsonObject{};
}

QJsonObject Montaz::EditorAdapter::mediaSources(const QJsonObject &context)
{
    if (snapshot() != context) return {};
    QJsonObject sources;
    for (const auto &value : context.value(QStringLiteral("assets")).toArray()) {
        const auto asset = value.toObject();
        if (!asset.value(QStringLiteral("has_video")).toBool()) continue;
        const auto id = asset.value(QStringLiteral("bin_id")).toString();
        const auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (!clip || !QFileInfo(clip->clipUrl()).isFile()) return {};
        sources.insert(id, QFileInfo(clip->clipUrl()).absoluteFilePath());
    }
    return sources;
}

QString Montaz::EditorAdapter::apply(const QJsonObject &proposal)
{
    if (QThread::currentThread() != thread()) return QStringLiteral("wrong_thread");
    const auto current = snapshot();
    const auto validation = validateProposal(current, proposal, m_appliedRequests);
    if (!validation.isEmpty()) return validation;
    const auto timeline = m_timeline.lock();
    if (!timeline) return QStringLiteral("no_sequence");
    // Explicit A/V prefixes suppress implicit mirror insertion and track creation.
    // Every proposal operation names exactly one imported stream and target track.
    const auto result = applyInsertBatch(
        proposal.value(QStringLiteral("operations")).toArray(),
        [timeline](const QJsonObject &op, UndoFunction &undo, UndoFunction &redo) {
            const int track = op.value(QStringLiteral("track_id")).toString().toInt();
            const int in = op.value(QStringLiteral("in_frame")).toInt();
            const int out = in + op.value(QStringLiteral("duration_frames")).toInt() - 1;
            const auto prefix = timeline->isAudioTrack(track) ? QStringLiteral("A") : QStringLiteral("V");
            const auto source =
                prefix + op.value(QStringLiteral("bin_id")).toString() + QLatin1Char('/') + QString::number(in) + QLatin1Char('/') + QString::number(out);
            int id = -1;
            return timeline->requestClipInsertion(source, track, op.value(QStringLiteral("start_frame")).toInt(), id, false, true, false, undo, redo, {}, 1) &&
                   id >= 0;
        },
        [](const UndoFunction &undo, const UndoFunction &redo) { pCore->pushUndo(undo, redo, i18n("Apply assistant edit")); });
    if (result.isEmpty()) m_appliedRequests.insert(proposal.value(QStringLiteral("request_id")).toString());
    if (result == QStringLiteral("rollback_failed")) m_applyHealthy = false;
    invalidate();
    return result;
}
