// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "editoradapter.h"
#include "editbatch.h"
#include "montage.h"
#include "proposal.h"

#include "bin/model/subtitlemodel.hpp"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "effects/effectsrepository.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "mainwindow.h"
#include "project/projectmanager.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
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
    m_draft = {};
    m_draftSources = {};
    invalidate();
    if (!document || !timeline) return;
    m_connections << connect(timeline.get(), &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        if (roles.isEmpty() ||
            std::any_of(roles.begin(), roles.end(), [](int role) { return role != TimelineModel::AudioLevelsRole && role != TimelineModel::SelectedRole; }))
            invalidate();
    });
    m_connections << connect(timeline.get(), &QAbstractItemModel::rowsInserted, this, [this]() { invalidate(); });
    m_connections << connect(timeline.get(), &QAbstractItemModel::rowsRemoved, this, [this]() { invalidate(); });
    m_connections << connect(timeline.get(), &QAbstractItemModel::rowsMoved, this, [this]() { invalidate(); });
    m_connections << connect(timeline.get(), &QAbstractItemModel::modelReset, this, [this]() { invalidate(); });
    watchSubtitles(timeline);
    m_connections << connect(timeline.get(), &TimelineItemModel::subtitleModelInitialized, this, [this]() {
        watchSubtitles(m_timeline.lock());
        invalidate();
    });
    const auto stack = pCore->projectManager()->undoStack();
    if (stack) m_connections << connect(stack.get(), &QUndoStack::indexChanged, this, [this]() { invalidate(); });
}

void Montaz::EditorAdapter::refreshContext()
{
    bind();
}

QString Montaz::EditorAdapter::contextKey()
{
    bind();
    const auto timeline = m_timeline.lock();
    if (!m_document || !timeline) return {};
    return m_document->uuid().toString(QUuid::WithoutBraces) + QLatin1Char('/') + timeline->uuid().toString(QUuid::WithoutBraces);
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

QJsonObject Montaz::EditorAdapter::mediaSources(const QJsonObject &context, const QSet<QString> &selected)
{
    if (snapshot() != context) return {};
    QJsonObject sources;
    for (const auto &value : context.value(QStringLiteral("assets")).toArray()) {
        const auto asset = value.toObject();
        if (!asset.value(QStringLiteral("has_video")).toBool()) continue;
        const auto id = asset.value(QStringLiteral("bin_id")).toString();
        if (!selected.isEmpty() && !selected.contains(id)) continue;
        const auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (!clip || !QFileInfo(clip->clipUrl()).isFile()) return {};
        sources.insert(id, QFileInfo(clip->clipUrl()).absoluteFilePath());
    }
    return sources;
}

namespace {
QJsonObject draftSourceIdentity(const QJsonArray &clips)
{
    QJsonObject sources;
    for (const auto v : clips) {
        const auto binId = v.toObject().value("bin_id").toString();
        const auto source = pCore->projectItemModel()->getClipByBinID(binId);
        if (!source) return {};
        const QFileInfo file(source->clipUrl());
        sources[binId] = QJsonArray{source->clipUrl(), file.size(), file.lastModified().toMSecsSinceEpoch(), source->getFramePlaytime()};
    }
    return sources;
}
} // namespace

bool Montaz::EditorAdapter::hasDraft()
{
    bind();
    return !m_draft.isEmpty();
}

QJsonObject Montaz::EditorAdapter::draftRequest()
{
    const auto current = snapshot();
    const auto timeline = m_timeline.lock();
    if (!timeline || m_draft.isEmpty() || !validateDraftRequest(current, m_draft).isEmpty() || timeline->getCompositionsCount() ||
        draftSourceIdentity(m_draft.value("clips").toArray()) != m_draftSources)
        return {};
    for (const auto v : m_draft.value("clips").toArray()) {
        const auto c = v.toObject();
        const int id = c.value("instance_id").toString().toInt();
        if (!timeline->isClip(id) || timeline->getClipBinId(id) != c.value("bin_id").toString() || timeline->getClipIn(id) != c.value("in_frame").toInt() ||
            timeline->hasPersistentGroup(id) || timeline->hasClipEndMix(id) || timeline->getMixDuration(id) > 0 || timeline->getClipSpeed(id) != 1.0 ||
            timeline->getClipEffectStackModel(id)->rowCount() || timeline->getClipState(id).first != PlaylistState::VideoOnly)
            return {};
        const auto asset = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(id));
        const int track = timeline->getClipTrackId(id);
        if (!asset || asset->getEffectStack()->rowCount() || timeline->getTrackEffectStackModel(track)->rowCount() ||
            (timeline->getTrackProperty(track, QStringLiteral("hide")).toInt() & 1))
            return {};
    }
    return m_draft;
}

void Montaz::EditorAdapter::commitDraft(const QJsonArray &clips, const UndoFunction &undo, const UndoFunction &redo, const QString &label)
{
    const auto key = contextKey();
    const auto previous = m_draft, previousSources = m_draftSources;
    const QJsonObject next{{"clips", clips}};
    const auto nextSources = draftSourceIdentity(clips);
    const QPointer<EditorAdapter> guard(this);
    auto restore = [guard, key](const QJsonObject &draft, const QJsonObject &sources) {
        if (guard && guard->contextKey() == key) {
            guard->m_draft = draft;
            guard->m_draftSources = sources;
        }
    };
    restore(next, nextSources);
    pCore->pushUndo(
        [undo, restore, previous, previousSources]() {
            if (!undo()) return false;
            restore(previous, previousSources);
            return true;
        },
        [redo, restore, next, nextSources]() {
            if (!redo()) return false;
            restore(next, nextSources);
            return true;
        },
        label);
}

QString Montaz::EditorAdapter::applyDraft(const QJsonObject &context, const QJsonObject &draft, const QJsonObject &proposal)
{
    if (QThread::currentThread() != thread()) return QStringLiteral("wrong_thread");
    if (snapshot() != context || draftRequest() != draft || !validateDraftProposal(context, draft, proposal, m_appliedRequests).isEmpty())
        return QStringLiteral("invalid_draft_revision");
    const auto timeline = m_timeline.lock();
    // Native group deletion includes every selected item. Clear temporary
    // multi-selection before removing only the explicitly owned clip IDs.
    timeline->requestClearSelection();
    QJsonArray steps, inserted;
    for (const auto v : draft.value("clips").toArray())
        steps.append(QJsonObject{{"delete", v.toObject().value("instance_id")}});
    for (const auto v : proposal.value("operations").toArray())
        steps.append(v);
    const auto result = applyInsertBatch(
        steps,
        [timeline, &inserted](const QJsonObject &op, UndoFunction &undo, UndoFunction &redo) {
            if (op.contains("delete")) return timeline->requestItemDeletion(op.value("delete").toString().toInt(), undo, redo, false);
            const int in = op.value("in_frame").toInt(), out = in + op.value("duration_frames").toInt() - 1;
            int id = -1;
            if (!timeline->requestClipInsertion(QStringLiteral("V%1/%2/%3").arg(op.value("bin_id").toString()).arg(in).arg(out),
                                                op.value("track_id").toString().toInt(), op.value("start_frame").toInt(), id, false, true, false, undo, redo,
                                                {}, 1) ||
                id < 0)
                return false;
            auto owned = op;
            owned["instance_id"] = QString::number(id);
            inserted.append(owned);
            return true;
        },
        [this, &inserted](const UndoFunction &undo, const UndoFunction &redo) { commitDraft(inserted, undo, redo, i18n("Revise assistant draft")); });
    if (result.isEmpty()) m_appliedRequests.insert(proposal.value("request_id").toString());
    if (result == QStringLiteral("rollback_failed")) m_applyHealthy = false;
    invalidate();
    return result;
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
    QJsonArray inserted;
    const auto result = applyInsertBatch(
        proposal.value(QStringLiteral("operations")).toArray(),
        [timeline, &inserted](const QJsonObject &op, UndoFunction &undo, UndoFunction &redo) {
            const int track = op.value(QStringLiteral("track_id")).toString().toInt();
            const int in = op.value(QStringLiteral("in_frame")).toInt();
            const int out = in + op.value(QStringLiteral("duration_frames")).toInt() - 1;
            const auto prefix = timeline->isAudioTrack(track) ? QStringLiteral("A") : QStringLiteral("V");
            const auto source =
                prefix + op.value(QStringLiteral("bin_id")).toString() + QLatin1Char('/') + QString::number(in) + QLatin1Char('/') + QString::number(out);
            int id = -1;
            if (!timeline->requestClipInsertion(source, track, op.value(QStringLiteral("start_frame")).toInt(), id, false, true, false, undo, redo, {}, 1) ||
                id < 0)
                return false;
            auto owned = op;
            owned["instance_id"] = QString::number(id);
            inserted.append(owned);
            return true;
        },
        [this, &inserted](const UndoFunction &undo, const UndoFunction &redo) { commitDraft(inserted, undo, redo, i18n("Apply assistant edit")); });
    if (result.isEmpty()) m_appliedRequests.insert(proposal.value(QStringLiteral("request_id")).toString());
    if (result == QStringLiteral("rollback_failed")) m_applyHealthy = false;
    invalidate();
    return result;
}

QJsonObject Montaz::EditorAdapter::musicRequest(const QString &recipe, const QString &instruction)
{
    const auto current = snapshot();
    if (current.isEmpty() || !m_document->url().isLocalFile()) return {};
    QSet<QString> videos;
    for (const auto value : current.value(QStringLiteral("tracks")).toArray()) {
        const auto track = value.toObject();
        if (track.value(QStringLiteral("kind")) == QStringLiteral("video")) videos.insert(track.value(QStringLiteral("track_id")).toString());
    }
    int start = std::numeric_limits<int>::max(), end = 0;
    for (const auto value : current.value(QStringLiteral("occupied")).toArray()) {
        const auto item = value.toObject();
        if (!videos.contains(item.value(QStringLiteral("track_id")).toString())) continue;
        start = std::min(start, item.value(QStringLiteral("start_frame")).toInt());
        end = std::max(end, item.value(QStringLiteral("start_frame")).toInt() + item.value(QStringLiteral("duration_frames")).toInt());
    }
    if (end <= start) return {};
    // Generated media is durable project content, not an evictable analysis cache.
    const QFileInfo project(m_document->url().toLocalFile());
    const QString directory = project.absolutePath() + QLatin1Char('/') + project.completeBaseName() + QStringLiteral(".assets/music");
    if (!QDir().mkpath(directory)) return {};
    const QString path = directory + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".wav");
    QJsonObject music{{QStringLiteral("path"), path},
                      {QStringLiteral("start_frame"), start},
                      {QStringLiteral("duration_frames"), end - start},
                      {QStringLiteral("recipe"), recipe},
                      {QStringLiteral("instruction"), instruction}};
    for (const auto value : current.value(QStringLiteral("tracks")).toArray()) {
        music[QStringLiteral("track_id")] = value.toObject().value(QStringLiteral("track_id"));
        if (validateMusicRequest(current, music).isEmpty()) return music;
    }
    return {};
}

QJsonObject Montaz::EditorAdapter::effectRequest(const QString &instruction, int durationFrames, int startFrame)
{
    const auto current = snapshot();
    if (current.isEmpty() || !m_document->url().isLocalFile()) return {};
    const QFileInfo project(m_document->url().toLocalFile());
    const QString directory = project.absolutePath() + QLatin1Char('/') + project.completeBaseName() + QStringLiteral(".assets/effects");
    const QString path = directory + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".wav");
    QJsonObject effect{{QStringLiteral("path"), path},
                       {QStringLiteral("start_frame"), startFrame < 0 ? pCore->getMonitorPosition() : startFrame},
                       {QStringLiteral("duration_frames"), durationFrames},
                       {QStringLiteral("instruction"), instruction}};
    for (const auto value : current.value(QStringLiteral("tracks")).toArray()) {
        effect[QStringLiteral("track_id")] = value.toObject().value(QStringLiteral("track_id"));
        if (validateEffectRequest(current, effect).isEmpty()) return QDir().mkpath(directory) ? effect : QJsonObject{};
    }
    return {};
}

QString Montaz::EditorAdapter::applySoundtrack(const QJsonObject &context, const QJsonObject &music, const QJsonObject &soundtrack, bool effect)
{
    if (QThread::currentThread() != thread()) return QStringLiteral("wrong_thread");
    if (snapshot() != context || !validateSoundtrack(context, music, soundtrack, effect).isEmpty()) return QStringLiteral("invalid_soundtrack");
    const auto request = soundtrack.value(QStringLiteral("request_id")).toString();
    if (m_appliedRequests.contains(request)) return QStringLiteral("already_applied");
    QFile file(music.value(QStringLiteral("path")).toString());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024) return QStringLiteral("invalid_soundtrack");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || QString::fromLatin1(hash.result().toHex()) != soundtrack.value(QStringLiteral("sha256")).toString())
        return QStringLiteral("invalid_soundtrack");
    const auto timeline = m_timeline.lock();
    auto producer = std::make_shared<Mlt::Producer>(pCore->getProjectProfile(), "avformat", file.fileName().toUtf8().constData());
    const int length = music.value(QStringLiteral("duration_frames")).toInt();
    if (!timeline || !producer->is_valid() || producer->get_int("audio_index") < 0 || producer->get_int("video_index") >= 0 ||
        producer->get_playtime() < length)
        return QStringLiteral("invalid_soundtrack");
    producer->set("kdenlive:clipname", (effect ? i18n("Assistant sound effect") : i18n("Assistant music")).toUtf8().constData());
    producer->set("kdenlive:montaz_role", effect ? "effect" : "music");
    UndoFunction undo = []() { return true; }, redo = []() { return true; };
    QString binId;
    const auto bin = pCore->projectItemModel();
    if (!bin->requestAddBinClip(binId, producer, QStringLiteral("-1"), undo, redo)) return QStringLiteral("music_import_failed");
    int clipId = -1;
    const auto source = QStringLiteral("A%1/0/%2").arg(binId).arg(length - 1);
    if (!timeline->requestClipInsertion(source, music.value(QStringLiteral("track_id")).toString().toInt(), music.value(QStringLiteral("start_frame")).toInt(),
                                        clipId, false, true, false, undo, redo, {}, 1) ||
        clipId < 0) {
        const bool rolledBack = undo();
        if (!rolledBack) m_applyHealthy = false;
        return rolledBack ? QStringLiteral("music_insert_failed") : QStringLiteral("rollback_failed");
    }
    pCore->pushUndo(undo, redo, effect ? i18n("Add assistant sound effect") : i18n("Add assistant music"));
    m_appliedRequests.insert(request);
    invalidate();
    return {};
}

QJsonObject Montaz::EditorAdapter::cueRequest()
{
    const auto current = snapshot();
    const auto timeline = m_timeline.lock();
    if (current.isEmpty() || !timeline || timeline->getCompositionsCount()) return {};
    std::vector<QJsonObject> visuals;
    for (const auto v : current.value(QStringLiteral("occupied")).toArray()) {
        auto item = v.toObject();
        const int id = item.value(QStringLiteral("instance_id")).toString().toInt();
        if (timeline->isAudioTrack(item.value(QStringLiteral("track_id")).toString().toInt())) {
            const auto clip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(id));
            if (!clip) return {};
            const auto role = clip->getProducerProperty(QStringLiteral("kdenlive:montaz_role"));
            if (role != QStringLiteral("music") && role != QStringLiteral("effect") &&
                !QFileInfo(clip->clipUrl()).absolutePath().endsWith(QStringLiteral(".assets/music")))
                return {};
            continue;
        }
        if (timeline->isInGroup(id) || timeline->hasClipEndMix(id) || timeline->getMixDuration(id) > 0 || timeline->getClipSpeed(id) != 1.0 ||
            timeline->getClipEffectStackModel(id)->rowCount() > 0 || timeline->getClipState(id).first != PlaylistState::VideoOnly)
            return {};
        item[QStringLiteral("bin_id")] = timeline->getClipBinId(id);
        item[QStringLiteral("in_frame")] = timeline->getClipIn(id);
        visuals.push_back(item);
    }
    std::sort(visuals.begin(), visuals.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("start_frame")).toInt() < b.value(QStringLiteral("start_frame")).toInt();
    });
    QJsonArray clips;
    for (const auto &v : visuals)
        clips.append(v);
    const QJsonObject spec{{QStringLiteral("clips"), clips}};
    return validateCueRequest(current, spec).isEmpty() ? spec : QJsonObject{};
}

QJsonObject Montaz::EditorAdapter::effectFromCue(const QJsonObject &context, const QJsonObject &spec, const QJsonObject &review, int index)
{
    if (snapshot() != context || cueRequest() != spec || !validateSoundCues(context, spec, review).isEmpty()) return {};
    const auto cues = review.value(QStringLiteral("cues")).toArray();
    if (index < 0 || index >= cues.size()) return {};
    const auto cue = cues[index].toObject();
    const auto timeline = m_timeline.lock();
    if (!timeline) return {};
    const auto clip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cue.value(QStringLiteral("instance_id")).toString().toInt()));
    if (!clip) return {};
    QFile file(clip->clipUrl());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || QString::fromLatin1(hash.result().toHex()) != cue.value(QStringLiteral("source_sha256")).toString()) return {};
    return effectRequest(cue.value(QStringLiteral("instruction")).toString(), cue.value(QStringLiteral("duration_frames")).toInt(),
                         cue.value(QStringLiteral("start_frame")).toInt());
}

QJsonObject Montaz::EditorAdapter::timingRequest()
{
    const auto current = snapshot();
    const auto timeline = m_timeline.lock();
    if (current.isEmpty() || !timeline || timeline->getCompositionsCount() || (timeline->getSubtitleModel() && timeline->getSubtitleModel()->rowCount() > 0))
        return {};
    QJsonArray clips;
    std::vector<QJsonObject> visuals;
    QJsonObject music;
    for (const auto value : current.value(QStringLiteral("occupied")).toArray()) {
        const auto item = value.toObject();
        const int id = item.value(QStringLiteral("instance_id")).toString().toInt();
        if (timeline->hasPersistentGroup(id) || timeline->hasClipEndMix(id) || timeline->getMixDuration(id) > 0 || timeline->getClipSpeed(id) != 1.0 ||
            timeline->getClipEffectStackModel(id)->rowCount() > 0)
            return {};
        const auto binId = timeline->getClipBinId(id);
        const auto asset = pCore->projectItemModel()->getClipByBinID(binId);
        const int track = timeline->getClipTrackId(id);
        if (!asset || asset->getEffectStack()->rowCount() || timeline->getTrackEffectStackModel(track)->rowCount() ||
            (timeline->getTrackProperty(track, QStringLiteral("hide")).toInt() & (timeline->isAudioTrack(track) ? 2 : 1)))
            return {};
        if (timeline->isAudioTrack(item.value(QStringLiteral("track_id")).toString().toInt())) {
            const QFileInfo path(asset->clipUrl());
            // Accept the role marker or legacy assistant music assets created before it.
            if (!music.isEmpty() || timeline->getClipState(id).first != PlaylistState::AudioOnly || timeline->getClipIn(id) != 0 ||
                path.suffix() != QStringLiteral("wav") ||
                (asset->getProducerProperty(QStringLiteral("kdenlive:montaz_role")) != QStringLiteral("music") &&
                 !path.absolutePath().endsWith(QStringLiteral(".assets/music"))))
                return {};
            QFile file(path.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024) return {};
            QCryptographicHash digest(QCryptographicHash::Sha256);
            if (!digest.addData(&file)) return {};
            music = {{QStringLiteral("path"), path.absoluteFilePath()},
                     {QStringLiteral("sha256"), QString::fromLatin1(digest.result().toHex())},
                     {QStringLiteral("instance_id"), QString::number(id)}};
        } else {
            if (timeline->getClipState(id).first != PlaylistState::VideoOnly) return {};
            auto clip = item;
            clip[QStringLiteral("bin_id")] = binId;
            clip[QStringLiteral("in_frame")] = timeline->getClipIn(id);
            visuals.push_back(clip);
        }
    }
    std::sort(visuals.begin(), visuals.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("start_frame")).toInt() < b.value(QStringLiteral("start_frame")).toInt();
    });
    for (const auto &clip : visuals)
        clips.append(clip);
    const auto fps = pCore->getProjectFpsInfo();
    const QJsonObject timing{{QStringLiteral("clips"), clips},
                             {QStringLiteral("music"), music},
                             {QStringLiteral("max_shift_frames"), qMax(1, int(qint64(fps.first) / (4 * qint64(fps.second))))}};
    return validateTimingRequest(current, timing).isEmpty() ? timing : QJsonObject{};
}

QString Montaz::EditorAdapter::applyTiming(const QJsonObject &context, const QJsonObject &timing, const QJsonObject &proposal)
{
    if (QThread::currentThread() != thread()) return QStringLiteral("wrong_thread");
    if (snapshot() != context || timingRequest() != timing || !validateTimingProposal(context, timing, proposal).isEmpty())
        return QStringLiteral("invalid_timing_proposal");
    const auto request = proposal.value(QStringLiteral("request_id")).toString();
    if (m_appliedRequests.contains(request)) return QStringLiteral("already_applied");
    const auto timeline = m_timeline.lock();
    const auto originals = timing.value(QStringLiteral("clips")).toArray();
    const bool ownsTiming = draftRequest().value(QStringLiteral("clips")) == timing.value(QStringLiteral("clips"));
    const auto operations = proposal.value(QStringLiteral("operations")).toArray();
    QJsonArray changed, steps;
    for (int i = 0; i < operations.size(); ++i)
        if (operations[i] != originals[i]) changed.append(operations[i]);
    // Temporarily shorten only affected clips to avoid native collision handling
    // altering neighbors. Retain their IDs, source in-points, and native metadata.
    for (const auto &action : {QStringLiteral("shrink"), QStringLiteral("move"), QStringLiteral("grow")})
        for (const auto value : changed) {
            auto step = value.toObject();
            step[QStringLiteral("action")] = action;
            steps.append(step);
        }
    const auto result = applyInsertBatch(
        steps,
        [timeline](const QJsonObject &step, UndoFunction &undo, UndoFunction &redo) {
            const int id = step.value(QStringLiteral("instance_id")).toString().toInt();
            const auto action = step.value(QStringLiteral("action")).toString();
            if (action == QStringLiteral("move"))
                return timeline->requestClipMove(id, step.value(QStringLiteral("track_id")).toString().toInt(),
                                                 step.value(QStringLiteral("start_frame")).toInt(), false, true, true, true, undo,
                                                 redo) == TimelineModel::MoveSuccess;
            const int requested = action == QStringLiteral("shrink") ? 1 : step.value(QStringLiteral("duration_frames")).toInt();
            int length = requested;
            return timeline->requestItemResize(id, length, true, true, undo, redo) && length == requested &&
                   timeline->getClipIn(id) == step.value(QStringLiteral("in_frame")).toInt();
        },
        [this, ownsTiming, operations](const UndoFunction &undo, const UndoFunction &redo) {
            if (ownsTiming)
                commitDraft(operations, undo, redo, i18n("Align cuts to music"));
            else
                pCore->pushUndo(undo, redo, i18n("Align cuts to music"));
        });
    if (result.isEmpty()) m_appliedRequests.insert(request);
    if (result == QStringLiteral("rollback_failed")) m_applyHealthy = false;
    invalidate();
    return result;
}

void Montaz::EditorAdapter::watchSubtitles(const std::shared_ptr<TimelineItemModel> &timeline)
{
    if (!timeline || !timeline->getSubtitleModel()) return;
    const auto model = timeline->getSubtitleModel();
    m_connections << connect(model.get(), &QAbstractItemModel::dataChanged, this, [this]() { invalidate(); });
    m_connections << connect(model.get(), &QAbstractItemModel::rowsInserted, this, [this]() { invalidate(); });
    m_connections << connect(model.get(), &QAbstractItemModel::rowsRemoved, this, [this]() { invalidate(); });
    m_connections << connect(model.get(), &QAbstractItemModel::modelReset, this, [this]() { invalidate(); });
}

QJsonObject Montaz::EditorAdapter::captionRequest()
{
    const auto current = snapshot();
    const auto timeline = m_timeline.lock();
    if (current.isEmpty() || !timeline) return {};
    const auto subs = timeline->getSubtitleModel();
    if (subs && (subs->rowCount() || subs->isLocked() || subs->isDisabled())) return {};
    std::vector<QJsonObject> selected;
    for (const auto v : current.value(QStringLiteral("occupied")).toArray()) {
        auto c = v.toObject();
        const int id = c.value(QStringLiteral("instance_id")).toString().toInt(), track = c.value(QStringLiteral("track_id")).toString().toInt();
        if (!timeline->isAudioTrack(track) || timeline->getClipState(id).first != PlaylistState::AudioOnly ||
            (timeline->getTrackProperty(track, QStringLiteral("hide")).toInt() & 2))
            continue;
        const auto binId = timeline->getClipBinId(id);
        const auto asset = pCore->projectItemModel()->getClipByBinID(binId);
        if (!asset) return {};
        const auto role = asset->getProducerProperty(QStringLiteral("kdenlive:montaz_role"));
        if (role == QStringLiteral("music") || role == QStringLiteral("effect") ||
            QFileInfo(asset->clipUrl()).absolutePath().endsWith(QStringLiteral(".assets/music")))
            continue;
        // Source decoding cannot reproduce native processing; reject rather than transcribe the wrong sound.
        if (timeline->getClipSpeed(id) != 1.0 || timeline->hasClipEndMix(id) || timeline->getMixDuration(id) > 0 ||
            timeline->getClipEffectStackModel(id)->rowCount() || timeline->getTrackEffectStackModel(track)->rowCount())
            return {};
        c[QStringLiteral("bin_id")] = binId;
        c[QStringLiteral("in_frame")] = timeline->getClipIn(id);
        c[QStringLiteral("audio_stream")] = timeline->data(timeline->makeClipIndexFromID(id), TimelineModel::AudioStreamRole).toInt();
        selected.push_back(c);
    }
    std::sort(selected.begin(), selected.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("start_frame")).toInt() < b.value(QStringLiteral("start_frame")).toInt();
    });
    QJsonArray clips;
    for (const auto &c : selected)
        clips.append(c);
    const QJsonObject spec{{QStringLiteral("clips"), clips}};
    return validateCaptionRequest(current, spec).isEmpty() ? spec : QJsonObject{};
}

QJsonObject Montaz::EditorAdapter::audioSources(const QJsonObject &context, const QSet<QString> &selected)
{
    if (snapshot() != context) return {};
    QJsonObject result;
    for (const auto &id : selected) {
        const auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (!clip || !clip->hasAudio() || !QFileInfo(clip->clipUrl()).isFile()) return {};
        result[id] = QFileInfo(clip->clipUrl()).absoluteFilePath();
    }
    return result;
}

QString Montaz::EditorAdapter::applyCaptions(const QJsonObject &context, const QJsonObject &spec, const QJsonObject &review)
{
    if (QThread::currentThread() != thread()) return QStringLiteral("wrong_thread");
    if (snapshot() != context || captionRequest() != spec || !validateCaptions(context, spec, review).isEmpty() ||
        review.value(QStringLiteral("captions")).toArray().isEmpty())
        return QStringLiteral("invalid_captions");
    const auto request = review.value(QStringLiteral("request_id")).toString();
    if (m_appliedRequests.contains(request)) return QStringLiteral("already_applied");
    const auto hashes = review.value(QStringLiteral("source_sha256")).toObject();
    for (auto it = hashes.begin(); it != hashes.end(); ++it) {
        const auto asset = pCore->projectItemModel()->getClipByBinID(it.key());
        if (!asset) return QStringLiteral("media_changed");
        QFile file(asset->clipUrl());
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024 || !hash.addData(&file) ||
            QString::fromLatin1(hash.result().toHex()) != it.value().toString())
            return QStringLiteral("media_changed");
    }
    const auto timeline = m_timeline.lock();
    if (!timeline) return QStringLiteral("no_sequence");
    if (pCore->window())
        pCore->window()->showSubtitleTrack();
    else if (!timeline->getSubtitleModel())
        timeline->createSubtitleModel();
    const auto model = timeline->getSubtitleModel();
    if (!model || model->rowCount() || model->isLocked() || model->isDisabled()) return QStringLiteral("invalid_captions");
    const auto captionSize = pCore->getCurrentFrameDisplaySize();
    const int captionUnit = qMin(captionSize.width(), captionSize.height());
    const QString captionStyleName = QStringLiteral("Montaz Clean");
    SubtitleStyle captionAppearance;
    captionAppearance.setFontName(QStringLiteral("Arial"));
    captionAppearance.setFontSize(qMax(12, captionUnit / 24));
    captionAppearance.setBold(true);
    captionAppearance.setOutline(qMax(1, captionUnit / 360));
    captionAppearance.setMarginL(qMax(12, captionUnit / 12));
    captionAppearance.setMarginR(qMax(12, captionUnit / 12));
    captionAppearance.setMarginV(qMax(12, captionSize.height() / 15));
    model->setSubtitleStyle(captionStyleName, captionAppearance);
    const double fps = pCore->getCurrentFps();
    const auto result = applyInsertBatch(
        review.value(QStringLiteral("captions")).toArray(),
        [model, fps, captionStyleName](const QJsonObject &c, UndoFunction &undo, UndoFunction &redo) {
            const std::pair<int, GenTime> start{0, GenTime(c.value(QStringLiteral("start_frame")).toInt(), fps)};
            const SubtitleEvent event(true, GenTime(c.value(QStringLiteral("start_frame")).toInt() + c.value(QStringLiteral("duration_frames")).toInt(), fps),
                                      captionStyleName, QString(), 0, 0, 0, QString(), c.value(QStringLiteral("text")).toString());
            return model->addSubtitle(start, event, undo, redo, true);
        },
        [](const UndoFunction &undo, const UndoFunction &redo) { pCore->pushUndo(undo, redo, i18n("Add assistant captions")); });
    if (result.isEmpty()) m_appliedRequests.insert(request);
    if (result == QStringLiteral("rollback_failed")) m_applyHealthy = false;
    invalidate();
    return result;
}

QString Montaz::EditorAdapter::captionStyle(const QString &preset)
{
    if (QThread::currentThread() != thread()) return QStringLiteral("wrong_thread");
    if (snapshot().isEmpty()) return QStringLiteral("no_sequence");
    const auto timeline = m_timeline.lock();
    const auto model = timeline ? timeline->getSubtitleModel() : nullptr;
    if (!model || !model->rowCount()) return QStringLiteral("no_subtitles");
    if (model->isLocked() || model->isDisabled()) return QStringLiteral("subtitles_unavailable");
    if (!QStringList{QStringLiteral("clean"), QStringLiteral("bold"), QStringLiteral("original")}.contains(preset))
        return QStringLiteral("invalid_caption_style");
    QString style;
    if (preset != QStringLiteral("original")) {
        const auto info = model->scriptInfo();
        const auto it = info.find(QStringLiteral("PlayResY"));
        const int height = it == info.end() ? pCore->getCurrentFrameDisplaySize().height() : it->second.toInt();
        if (height <= 0) return QStringLiteral("subtitles_unavailable");
        const auto widthIt = info.find(QStringLiteral("PlayResX"));
        const int width = widthIt == info.end() ? pCore->getCurrentFrameDisplaySize().width() : widthIt->second.toInt();
        const int unit = qMin(width, height);
        if (unit <= 0) return QStringLiteral("subtitles_unavailable");
        style = QStringLiteral("FontName=Arial,FontSize=%1,Bold=%2,Italic=0,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=1,Outline=%3,Shadow="
                               "0,Alignment=2,MarginV=%4,MarginL=%5,MarginR=%5")
                    .arg(qMax(12, unit / (preset == QStringLiteral("bold") ? 20 : 24)))
                    .arg(-1)
                    .arg(qMax(1, unit / 360))
                    .arg(qMax(12, height / 15))
                    .arg(qMax(12, unit / 12));
    }
    if (model->getForceStyle() != style) model->setForceStyle(style);
    return {};
}

namespace {
bool appendUniformTransform(const std::shared_ptr<TimelineItemModel> &timeline, int id, const QJsonArray &keys, int width, int height,
                            Montaz::UndoFunction &undo, Montaz::UndoFunction &redo)
{
    const auto stack = timeline->getClipEffectStackModel(id);
    if (!stack->appendEffectWithUndo(QStringLiteral("qtblend"), undo, redo).first) return false;
    const auto model = stack->getAssetModelById(QStringLiteral("qtblend"));
    if (!model) return false;
    QStringList values, rotations, anchors;
    for (const auto v : keys) {
        const auto k = v.toObject();
        const double s = k.value("scale").toDouble();
        const int frame = timeline->getClipIn(id) + k.value("frame").toInt();
        rotations.append(QStringLiteral("%1=0").arg(frame));
        anchors.append(QStringLiteral("%1=0.5 0.5 0 0").arg(frame));
        values.append(QStringLiteral("%1=%2 %3 %4 %5 1")
                          .arg(frame)
                          .arg(k.value("x").toDouble() * width, 0, 'f', 6)
                          .arg(k.value("y").toDouble() * height, 0, 'f', 6)
                          .arg(s * width, 0, 'f', 6)
                          .arg(s * height, 0, 'f', 6));
    }
    model->setParameter(QStringLiteral("rect"), values.join(QLatin1Char(';')));
    // Every animated parameter must share positions when the effect is loaded.
    model->setParameter(QStringLiteral("rotation"), rotations.join(QLatin1Char(';')));
    const auto parameters = model->getAllParameters();
    if (std::any_of(parameters.begin(), parameters.end(), [](const auto &p) { return p.first == QStringLiteral("rotate_anchor"); })) {
        model->setParameter(QStringLiteral("rotate_anchor"), anchors.join(QLatin1Char(';')));
    }
    model->setParameter(QStringLiteral("distort"), QStringLiteral("0"));
    return true;
}
} // namespace

QJsonObject Montaz::EditorAdapter::montageRequest(int shotFrames)
{
    const auto current = snapshot();
    const auto timeline = m_timeline.lock();
    if (current.isEmpty() || !timeline || shotFrames < 8 || shotFrames > 12 || timeline->getCompositionsCount() ||
        timeline->getMasterEffectStackModel()->rowCount() > 0 || !pCore->getProjectProfile().progressive() ||
        pCore->getProjectProfile().sample_aspect_num() != pCore->getProjectProfile().sample_aspect_den())
        return {};
    Mlt::Filter transform(pCore->getProjectProfile(), "qtblend");
    if (!EffectsRepository::get()->exists(QStringLiteral("qtblend")) || !transform.is_valid()) return {};
    const auto selected = timeline->getCurrentSelection();
    if (selected.size() < 2 || selected.size() > 6) return {};
    const auto size = pCore->getCurrentFrameSize();
    std::vector<QJsonObject> clips;
    int track = -1;
    for (int id : selected) {
        if (!timeline->isClip(id) || timeline->hasPersistentGroup(id) || timeline->hasClipEndMix(id) || timeline->getMixDuration(id) > 0 ||
            timeline->getClipSpeed(id) != 1.0 || timeline->getClipEffectStackModel(id)->rowCount() > 0 ||
            timeline->getClipState(id).first != PlaylistState::VideoOnly)
            return {};
        const int sourceTrack = timeline->getClipTrackId(id);
        if (sourceTrack < 0 || timeline->isAudioTrack(sourceTrack) || timeline->getTrackEffectStackModel(sourceTrack)->rowCount() > 0 ||
            (track >= 0 && track != sourceTrack))
            return {};
        track = sourceTrack;
        const auto asset = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(id));
        if (!asset || asset->getEffectStack()->rowCount() > 0 || !QFileInfo(asset->clipUrl()).isFile()) return {};
        // Kdenlive handles alternate MLT dimension properties, pixel aspect
        // ratio and original dimensions when a bin asset uses a proxy.
        const auto sourceSize = asset->frameSize();
        const int width = sourceSize.width(), height = sourceSize.height();
        if (width <= 0 || height <= 0) return {};
        const double density = std::max(double(width) / size.width(), double(height) / size.height());
        clips.push_back({{"instance_id", QString::number(id)},
                         {"bin_id", timeline->getClipBinId(id)},
                         {"track_id", QString::number(track)},
                         {"start_frame", timeline->getClipPosition(id)},
                         {"in_frame", timeline->getClipIn(id)},
                         {"duration_frames", timeline->getClipPlaytime(id)},
                         {"zoom_limit", std::clamp(1.6 * density, 1.1, 2.0)}});
    }
    std::sort(clips.begin(), clips.end(), [](const auto &a, const auto &b) { return a.value("start_frame").toInt() < b.value("start_frame").toInt(); });
    QJsonArray values;
    for (const auto &c : clips)
        values.append(c);
    const QJsonObject spec{{"clips", values},
                           {"width", size.width()},
                           {"height", size.height()},
                           {"shot_frames", shotFrames},
                           {"track_id", QString::number(track)},
                           {"start_frame", timeline->duration()}};
    return validateMontageRequest(current, spec).isEmpty() ? spec : QJsonObject{};
}

QString Montaz::EditorAdapter::applyMontage(const QJsonObject &context, const QJsonObject &spec, const QJsonObject &review)
{
    if (QThread::currentThread() != thread()) return QStringLiteral("wrong_thread");
    if (snapshot() != context || montageRequest(spec.value("shot_frames").toInt()) != spec || !validateMontageReview(context, spec, review).isEmpty() ||
        review.value("status") != "matched")
        return QStringLiteral("invalid_montage_review");
    const auto request = review.value("request_id").toString();
    if (m_appliedRequests.contains(request)) return QStringLiteral("already_applied");
    const auto hashes = review.value("source_hashes").toObject();
    const auto keys = hashes.keys();
    const auto sources = mediaSources(context, QSet<QString>(keys.begin(), keys.end()));
    for (auto it = hashes.begin(); it != hashes.end(); ++it) {
        QFile file(sources.value(it.key()).toString());
        if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024) return QStringLiteral("media_changed");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file) || QString::fromLatin1(hash.result().toHex()) != it.value().toString()) return QStringLiteral("media_changed");
    }
    QMap<QString, QString> bins;
    for (const auto v : spec.value("clips").toArray())
        bins.insert(v.toObject().value("instance_id").toString(), v.toObject().value("bin_id").toString());
    const auto timeline = m_timeline.lock();
    const int width = spec.value("width").toInt(), height = spec.value("height").toInt(), length = spec.value("shot_frames").toInt();
    const int track = spec.value("track_id").toString().toInt();
    int position = spec.value("start_frame").toInt();
    const auto result = applyInsertBatch(
        review.value("operations").toArray(),
        [timeline, &position, bins, track, length, width, height](const QJsonObject &op, UndoFunction &undo, UndoFunction &redo) {
            const int in = op.value("in_frame").toInt();
            int id = -1;
            if (!timeline->requestClipInsertion(
                    QStringLiteral("V%1/%2/%3").arg(bins.value(op.value("source_instance_id").toString())).arg(in).arg(in + length - 1), track, position, id,
                    false, true, false, undo, redo, {}, 1) ||
                id < 0)
                return false;
            if (!appendUniformTransform(timeline, id, op.value("keyframes").toArray(), width, height, undo, redo)) return false;
            position += length;
            return true;
        },
        [](const UndoFunction &undo, const UndoFunction &redo) { pCore->pushUndo(undo, redo, i18n("Create quick match montage")); });
    if (result.isEmpty()) m_appliedRequests.insert(request);
    if (result == QStringLiteral("rollback_failed")) m_applyHealthy = false;
    invalidate();
    return result;
}

QJsonObject Montaz::EditorAdapter::matchRequest()
{
    const auto current = snapshot();
    const auto timeline = m_timeline.lock();
    if (current.isEmpty() || !timeline || timeline->getCompositionsCount() || !pCore->getProjectProfile().progressive() ||
        pCore->getProjectProfile().sample_aspect_num() != pCore->getProjectProfile().sample_aspect_den())
        return {};
    Mlt::Filter transform(pCore->getProjectProfile(), "qtblend");
    if (!EffectsRepository::get()->exists(QStringLiteral("qtblend")) || !transform.is_valid()) return {};
    const auto selected = timeline->getCurrentSelection();
    if (selected.size() != 2) return {};
    std::vector<QJsonObject> clips;
    for (int id : selected) {
        if (!timeline->isClip(id) || timeline->hasPersistentGroup(id) || timeline->hasClipEndMix(id) || timeline->getMixDuration(id) > 0 ||
            timeline->getClipSpeed(id) != 1.0 || timeline->getClipEffectStackModel(id)->rowCount() > 0 ||
            timeline->getClipState(id).first != PlaylistState::VideoOnly)
            return {};
        const auto asset = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(id));
        if (!asset || asset->getEffectStack()->rowCount() > 0) return {};
        const int track = timeline->getClipTrackId(id);
        if (timeline->isAudioTrack(track)) return {};
        clips.push_back({{"instance_id", QString::number(id)},
                         {"bin_id", timeline->getClipBinId(id)},
                         {"track_id", QString::number(track)},
                         {"start_frame", timeline->getClipPosition(id)},
                         {"in_frame", timeline->getClipIn(id)},
                         {"duration_frames", timeline->getClipPlaytime(id)}});
    }
    std::sort(clips.begin(), clips.end(), [](const auto &a, const auto &b) { return a.value("start_frame").toInt() < b.value("start_frame").toInt(); });
    QJsonArray values;
    for (const auto &c : clips)
        values.append(c);
    const auto size = pCore->getCurrentFrameSize();
    QJsonObject spec{{"clips", values}, {"width", size.width()}, {"height", size.height()}};
    return validateMatchRequest(current, spec).isEmpty() ? spec : QJsonObject{};
}

QString Montaz::EditorAdapter::applyMatch(const QJsonObject &context, const QJsonObject &spec, const QJsonObject &review)
{
    if (QThread::currentThread() != thread()) return QStringLiteral("wrong_thread");
    if (snapshot() != context || matchRequest() != spec || !validateMatchReview(context, spec, review).isEmpty() || review.value("status") != "matched")
        return QStringLiteral("invalid_match_review");
    const auto request = review.value("request_id").toString();
    if (m_appliedRequests.contains(request)) return QStringLiteral("already_applied");
    const auto hashes = review.value("source_hashes").toObject();
    const auto keys = hashes.keys();
    const auto sources = mediaSources(context, QSet<QString>(keys.begin(), keys.end()));
    for (auto it = hashes.begin(); it != hashes.end(); ++it) {
        QFile file(sources.value(it.key()).toString());
        if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024) return QStringLiteral("media_changed");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file) || QString::fromLatin1(hash.result().toHex()) != it.value().toString()) return QStringLiteral("media_changed");
    }
    const auto timeline = m_timeline.lock();
    const int width = spec.value("width").toInt(), height = spec.value("height").toInt();
    const auto result = applyInsertBatch(
        review.value("operations").toArray(),
        [timeline, width, height](const QJsonObject &op, UndoFunction &undo, UndoFunction &redo) {
            const int id = op.value("instance_id").toString().toInt();
            return appendUniformTransform(timeline, id, op.value("keyframes").toArray(), width, height, undo, redo);
        },
        [](const UndoFunction &undo, const UndoFunction &redo) { pCore->pushUndo(undo, redo, i18n("Align objects across a match cut")); });
    if (result.isEmpty()) m_appliedRequests.insert(request);
    if (result == QStringLiteral("rollback_failed")) m_applyHealthy = false;
    invalidate();
    return result;
}
