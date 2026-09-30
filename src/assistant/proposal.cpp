// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "proposal.h"

#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace {
bool fields(const QJsonObject &object, const QStringList &names)
{
    const auto keys = object.keys();
    return QSet<QString>(keys.begin(), keys.end()) == QSet<QString>(names.begin(), names.end());
}

bool integer(const QJsonValue &value, int minimum = 0)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number && number >= minimum && number <= std::numeric_limits<int>::max();
}

bool identifier(const QJsonValue &value)
{
    if (!value.isString()) return false;
    const QString text = value.toString();
    if (text.trimmed().isEmpty() || text.size() > 128) return false;
    for (const auto c : text) {
        if (c.unicode() < 32) return false;
    }
    return true;
}

bool editorId(const QJsonValue &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9]{1,10}$"));
    bool valid = false;
    const auto id = value.toString().toInt(&valid);
    return value.isString() && pattern.match(value.toString()).hasMatch() && valid && id >= 0;
}

bool rate(const QJsonValue &value)
{
    const auto object = value.toObject();
    return value.isObject() && fields(object, {QStringLiteral("numerator"), QStringLiteral("denominator")}) &&
           integer(object.value(QStringLiteral("numerator")), 1) && integer(object.value(QStringLiteral("denominator")), 1);
}

bool range(const QJsonObject &object, const QString &startKey)
{
    const auto start = object.value(startKey);
    const auto duration = object.value(QStringLiteral("duration_frames"));
    return integer(start) && integer(duration, 1) && start.toInteger() + duration.toInteger() <= std::numeric_limits<int>::max();
}

QString invalid()
{
    return QStringLiteral("invalid_contract");
}
} // namespace

QString Montaz::validateSnapshot(const QJsonObject &snapshot)
{
    if (!fields(snapshot, {QStringLiteral("project_id"), QStringLiteral("sequence_id"), QStringLiteral("revision"), QStringLiteral("frame_rate"),
                           QStringLiteral("assets"), QStringLiteral("tracks"), QStringLiteral("occupied")}) ||
        !identifier(snapshot.value(QStringLiteral("project_id"))) || !identifier(snapshot.value(QStringLiteral("sequence_id"))) ||
        !integer(snapshot.value(QStringLiteral("revision"))) || !rate(snapshot.value(QStringLiteral("frame_rate"))))
        return invalid();
    QSet<QString> assetIds, trackIds, instanceIds;
    for (const auto &key : {QStringLiteral("assets"), QStringLiteral("tracks"), QStringLiteral("occupied")}) {
        if (!snapshot.value(key).isArray() || snapshot.value(key).toArray().size() > 10000) return invalid();
    }
    for (const auto value : snapshot.value(QStringLiteral("assets")).toArray()) {
        const auto item = value.toObject();
        const auto id = item.value(QStringLiteral("bin_id"));
        if (!fields(item, {QStringLiteral("bin_id"), QStringLiteral("duration_frames"), QStringLiteral("has_video"), QStringLiteral("has_audio")}) ||
            !editorId(id) || assetIds.contains(id.toString()) || !integer(item.value(QStringLiteral("duration_frames")), 1) ||
            !item.value(QStringLiteral("has_video")).isBool() || !item.value(QStringLiteral("has_audio")).isBool())
            return invalid();
        assetIds.insert(id.toString());
    }
    for (const auto value : snapshot.value(QStringLiteral("tracks")).toArray()) {
        const auto item = value.toObject();
        const auto id = item.value(QStringLiteral("track_id"));
        const auto kind = item.value(QStringLiteral("kind")).toString();
        if (!fields(item, {QStringLiteral("track_id"), QStringLiteral("kind"), QStringLiteral("locked")}) || !editorId(id) ||
            trackIds.contains(id.toString()) || (kind != QStringLiteral("video") && kind != QStringLiteral("audio")) ||
            !item.value(QStringLiteral("locked")).isBool())
            return invalid();
        trackIds.insert(id.toString());
    }
    for (const auto value : snapshot.value(QStringLiteral("occupied")).toArray()) {
        const auto item = value.toObject();
        const auto id = item.value(QStringLiteral("instance_id"));
        if (!fields(item, {QStringLiteral("instance_id"), QStringLiteral("track_id"), QStringLiteral("start_frame"), QStringLiteral("duration_frames")}) ||
            !identifier(id) || instanceIds.contains(id.toString()) || !trackIds.contains(item.value(QStringLiteral("track_id")).toString()) ||
            !range(item, QStringLiteral("start_frame")))
            return invalid();
        instanceIds.insert(id.toString());
    }
    return {};
}

QString Montaz::validateProposal(const QJsonObject &snapshot, const QJsonObject &proposal, const QSet<QString> &appliedRequests)
{
    if (!validateSnapshot(snapshot).isEmpty()) return invalid();
    if (!fields(proposal, {QStringLiteral("request_id"), QStringLiteral("project_id"), QStringLiteral("sequence_id"), QStringLiteral("base_revision"),
                           QStringLiteral("frame_rate"), QStringLiteral("operations"), QStringLiteral("protocol_version")}) ||
        !identifier(proposal.value(QStringLiteral("request_id"))) || !integer(proposal.value(QStringLiteral("protocol_version")), 1) ||
        proposal.value(QStringLiteral("protocol_version")).toInt() != 1 || !integer(proposal.value(QStringLiteral("base_revision"))) ||
        !rate(proposal.value(QStringLiteral("frame_rate"))) || !proposal.value(QStringLiteral("operations")).isArray())
        return invalid();
    if (proposal.value(QStringLiteral("project_id")) != snapshot.value(QStringLiteral("project_id")) ||
        proposal.value(QStringLiteral("sequence_id")) != snapshot.value(QStringLiteral("sequence_id")) ||
        proposal.value(QStringLiteral("base_revision")) != snapshot.value(QStringLiteral("revision")) ||
        proposal.value(QStringLiteral("frame_rate")) != snapshot.value(QStringLiteral("frame_rate")))
        return QStringLiteral("stale_proposal");
    if (appliedRequests.contains(proposal.value(QStringLiteral("request_id")).toString())) return QStringLiteral("already_applied");
    const auto operations = proposal.value(QStringLiteral("operations")).toArray();
    if (operations.isEmpty() || operations.size() > 256) return invalid();
    QMap<QString, QJsonObject> assets, tracks;
    QMap<QString, QList<QPair<qint64, qint64>>> occupied;
    QSet<QString> instances;
    for (const auto value : snapshot.value(QStringLiteral("assets")).toArray()) {
        const auto item = value.toObject();
        assets.insert(item.value(QStringLiteral("bin_id")).toString(), item);
    }
    for (const auto value : snapshot.value(QStringLiteral("tracks")).toArray()) {
        const auto item = value.toObject();
        tracks.insert(item.value(QStringLiteral("track_id")).toString(), item);
    }
    for (const auto value : snapshot.value(QStringLiteral("occupied")).toArray()) {
        const auto item = value.toObject();
        instances.insert(item.value(QStringLiteral("instance_id")).toString());
        const auto start = item.value(QStringLiteral("start_frame")).toInteger();
        occupied[item.value(QStringLiteral("track_id")).toString()].append({start, start + item.value(QStringLiteral("duration_frames")).toInteger()});
    }
    for (const auto value : operations) {
        const auto op = value.toObject();
        if (!fields(op, {QStringLiteral("instance_id"), QStringLiteral("bin_id"), QStringLiteral("track_id"), QStringLiteral("start_frame"),
                         QStringLiteral("in_frame"), QStringLiteral("duration_frames")}) ||
            !identifier(op.value(QStringLiteral("instance_id"))) || !range(op, QStringLiteral("start_frame")) || !range(op, QStringLiteral("in_frame")))
            return invalid();
        const auto instance = op.value(QStringLiteral("instance_id")).toString();
        if (instances.contains(instance)) return QStringLiteral("duplicate_instance");
        instances.insert(instance);
        const auto bin = op.value(QStringLiteral("bin_id")).toString();
        const auto trackId = op.value(QStringLiteral("track_id")).toString();
        if (!assets.contains(bin) || !tracks.contains(trackId)) return QStringLiteral("unknown_asset_or_track");
        const auto track = tracks.value(trackId);
        const auto asset = assets.value(bin);
        if (track.value(QStringLiteral("locked")).toBool()) return QStringLiteral("locked_track");
        const QString stream = track.value(QStringLiteral("kind")) == QStringLiteral("video") ? QStringLiteral("has_video") : QStringLiteral("has_audio");
        if (!asset.value(stream).toBool()) return QStringLiteral("missing_stream");
        const auto duration = op.value(QStringLiteral("duration_frames")).toInteger();
        if (op.value(QStringLiteral("in_frame")).toInteger() + duration > asset.value(QStringLiteral("duration_frames")).toInteger())
            return QStringLiteral("source_bounds");
        const auto start = op.value(QStringLiteral("start_frame")).toInteger();
        const auto end = start + duration;
        for (const auto &existing : occupied[trackId]) {
            if (start < existing.second && existing.first < end) return QStringLiteral("overlap");
        }
        occupied[trackId].append({start, end});
    }
    return {};
}
