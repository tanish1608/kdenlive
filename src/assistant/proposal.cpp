// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "proposal.h"

#include <QFileInfo>
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

QString Montaz::validateEditorialReview(const QJsonObject &snapshot, const QJsonObject &proposal, const QJsonObject &review)
{
    if (!fields(review, {QStringLiteral("requested_frames"), QStringLiteral("available_frames"), QStringLiteral("skipped_assets"), QStringLiteral("shots"),
                         QStringLiteral("assessment")}) ||
        !integer(review.value(QStringLiteral("requested_frames")), 1) || !integer(review.value(QStringLiteral("available_frames")), 1) ||
        !integer(review.value(QStringLiteral("skipped_assets"))) || !review.value(QStringLiteral("shots")).isArray())
        return invalid();
    const auto assessment = review.value(QStringLiteral("assessment")).toObject();
    const auto status = assessment.value(QStringLiteral("status")).toString();
    const auto reason = assessment.value(QStringLiteral("reason"));
    const auto code = assessment.value(QStringLiteral("code")).toString();
    const QStringList unavailableCodes{QStringLiteral("provider_auth"),           QStringLiteral("provider_timeout"),
                                       QStringLiteral("provider_unavailable"),    QStringLiteral("provider_request_rejected"),
                                       QStringLiteral("provider_output_limit"),   QStringLiteral("provider_blocked"),
                                       QStringLiteral("provider_empty_response"), QStringLiteral("invalid_model_response"),
                                       QStringLiteral("invalid_review"),          QStringLiteral("review_limit")};
    if (!fields(assessment, {QStringLiteral("status"), QStringLiteral("reason"), QStringLiteral("code")}) || !reason.isString() ||
        (status == QStringLiteral("unavailable") ? (!reason.toString().isEmpty() || !unavailableCodes.contains(code))
                                                 : ((status != QStringLiteral("kept") && status != QStringLiteral("revised")) ||
                                                    code != QStringLiteral("none") || reason.toString().trimmed().isEmpty() || reason.toString().size() > 600)))
        return invalid();
    const auto shots = review.value(QStringLiteral("shots")).toArray();
    const auto operations = proposal.value(QStringLiteral("operations")).toArray();
    int videos = 0;
    for (const auto value : snapshot.value(QStringLiteral("assets")).toArray())
        videos += value.toObject().value(QStringLiteral("has_video")).toBool();
    if (shots.size() != operations.size() || shots.size() + review.value(QStringLiteral("skipped_assets")).toInteger() > videos) return invalid();
    qint64 planned = 0;
    const QStringList roles{QStringLiteral("establishing"), QStringLiteral("detail"), QStringLiteral("action"), QStringLiteral("closing"),
                            QStringLiteral("other")};
    for (int i = 0; i < shots.size(); ++i) {
        const auto shot = shots[i].toObject();
        const auto op = operations[i].toObject();
        const auto reason = shot.value(QStringLiteral("reason"));
        if (!fields(shot, {QStringLiteral("instance_id"), QStringLiteral("role"), QStringLiteral("quality"), QStringLiteral("reason")}) ||
            shot.value(QStringLiteral("instance_id")) != op.value(QStringLiteral("instance_id")) ||
            !roles.contains(shot.value(QStringLiteral("role")).toString()) || !integer(shot.value(QStringLiteral("quality")), 50) ||
            shot.value(QStringLiteral("quality")).toInt() > 100 || !reason.isString() || reason.toString().trimmed().isEmpty() ||
            reason.toString().size() > 300)
            return invalid();
        planned += op.value(QStringLiteral("duration_frames")).toInteger();
    }
    if (planned > review.value(QStringLiteral("requested_frames")).toInteger() || planned > review.value(QStringLiteral("available_frames")).toInteger())
        return invalid();
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

QString Montaz::validateMusicRequest(const QJsonObject &snapshot, const QJsonObject &music)
{
    if (!validateSnapshot(snapshot).isEmpty() ||
        !fields(music, {QStringLiteral("path"), QStringLiteral("track_id"), QStringLiteral("start_frame"), QStringLiteral("duration_frames"),
                        QStringLiteral("recipe"), QStringLiteral("instruction")}) ||
        !range(music, QStringLiteral("start_frame")) || !music.value(QStringLiteral("path")).isString() ||
        music.value(QStringLiteral("path")).toString().size() > 4096 || !QFileInfo(music.value(QStringLiteral("path")).toString()).isAbsolute() ||
        QFileInfo(music.value(QStringLiteral("path")).toString()).suffix() != QStringLiteral("wav") || !music.value(QStringLiteral("instruction")).isString() ||
        music.value(QStringLiteral("instruction")).toString().size() > 2000 ||
        !QStringList{QStringLiteral("postcard"), QStringLiteral("adventure"), QStringLiteral("diary")}.contains(
            music.value(QStringLiteral("recipe")).toString()))
        return invalid();
    const auto rate = snapshot.value(QStringLiteral("frame_rate")).toObject();
    const qint64 start = music.value(QStringLiteral("start_frame")).toInteger();
    const qint64 length = music.value(QStringLiteral("duration_frames")).toInteger();
    const qint64 numerator = rate.value(QStringLiteral("numerator")).toInteger(), denominator = rate.value(QStringLiteral("denominator")).toInteger();
    if (length * denominator < 3 * numerator || length * denominator > 600 * numerator) return invalid();
    QSet<QString> videoTracks;
    bool target = false;
    for (const auto value : snapshot.value(QStringLiteral("tracks")).toArray()) {
        const auto track = value.toObject();
        if (track.value(QStringLiteral("kind")) == QStringLiteral("video")) videoTracks.insert(track.value(QStringLiteral("track_id")).toString());
        if (track.value(QStringLiteral("track_id")) == music.value(QStringLiteral("track_id")) &&
            track.value(QStringLiteral("kind")) == QStringLiteral("audio") && !track.value(QStringLiteral("locked")).toBool())
            target = true;
    }
    if (!target) return invalid();
    qint64 first = 2147483647, last = 0;
    for (const auto value : snapshot.value(QStringLiteral("occupied")).toArray()) {
        const auto item = value.toObject();
        const auto position = item.value(QStringLiteral("start_frame")).toInteger();
        const auto end = position + item.value(QStringLiteral("duration_frames")).toInteger();
        if (videoTracks.contains(item.value(QStringLiteral("track_id")).toString())) {
            first = qMin(first, position);
            last = qMax(last, end);
        }
        if (item.value(QStringLiteral("track_id")) == music.value(QStringLiteral("track_id")) && start < end && position < start + length) return invalid();
    }
    return first == start && last == start + length ? QString() : invalid();
}

QString Montaz::validateCueRequest(const QJsonObject &snapshot, const QJsonObject &spec)
{
    const auto clips = spec.value(QStringLiteral("clips")).toArray();
    if (!validateSnapshot(snapshot).isEmpty() || !fields(spec, {QStringLiteral("clips")}) || !spec.value(QStringLiteral("clips")).isArray() ||
        clips.isEmpty() || clips.size() > 64)
        return invalid();
    QMap<QString, QJsonObject> assets, tracks, occupied;
    for (const auto v : snapshot.value(QStringLiteral("assets")).toArray())
        assets[v.toObject().value(QStringLiteral("bin_id")).toString()] = v.toObject();
    for (const auto v : snapshot.value(QStringLiteral("tracks")).toArray())
        tracks[v.toObject().value(QStringLiteral("track_id")).toString()] = v.toObject();
    QSet<QString> visuals, seen;
    for (const auto v : snapshot.value(QStringLiteral("occupied")).toArray()) {
        const auto item = v.toObject();
        occupied[item.value(QStringLiteral("instance_id")).toString()] = item;
        if (tracks.value(item.value(QStringLiteral("track_id")).toString()).value(QStringLiteral("kind")) == QStringLiteral("video"))
            visuals.insert(item.value(QStringLiteral("instance_id")).toString());
    }
    qint64 previous = -1;
    QString trackId;
    for (const auto v : clips) {
        const auto c = v.toObject(), asset = assets.value(c.value(QStringLiteral("bin_id")).toString());
        const auto item = occupied.value(c.value(QStringLiteral("instance_id")).toString());
        const auto track = c.value(QStringLiteral("track_id")).toString(), id = c.value(QStringLiteral("instance_id")).toString();
        if (!fields(c, {QStringLiteral("instance_id"), QStringLiteral("bin_id"), QStringLiteral("track_id"), QStringLiteral("start_frame"),
                        QStringLiteral("in_frame"), QStringLiteral("duration_frames")}) ||
            !identifier(c.value(QStringLiteral("instance_id"))) || !identifier(c.value(QStringLiteral("bin_id"))) ||
            !identifier(c.value(QStringLiteral("track_id"))) || !range(c, QStringLiteral("start_frame")) || !range(c, QStringLiteral("in_frame")) ||
            item.isEmpty() || seen.contains(id) || !asset.value(QStringLiteral("has_video")).toBool() ||
            tracks.value(track).value(QStringLiteral("kind")) != QStringLiteral("video") ||
            c.value(QStringLiteral("in_frame")).toInteger() + c.value(QStringLiteral("duration_frames")).toInteger() >
                asset.value(QStringLiteral("duration_frames")).toInteger() ||
            c.value(QStringLiteral("start_frame")).toInteger() < previous || (!trackId.isEmpty() && trackId != track))
            return invalid();
        for (const auto &key : {QStringLiteral("track_id"), QStringLiteral("start_frame"), QStringLiteral("duration_frames")})
            if (c.value(key) != item.value(key)) return invalid();
        previous = c.value(QStringLiteral("start_frame")).toInteger() + c.value(QStringLiteral("duration_frames")).toInteger();
        trackId = track;
        seen.insert(id);
    }
    return seen == visuals ? QString() : invalid();
}

QString Montaz::validateSoundCues(const QJsonObject &snapshot, const QJsonObject &spec, const QJsonObject &review)
{
    if (!validateCueRequest(snapshot, spec).isEmpty() ||
        !fields(review, {QStringLiteral("request_id"), QStringLiteral("project_id"), QStringLiteral("sequence_id"), QStringLiteral("base_revision"),
                         QStringLiteral("frame_rate"), QStringLiteral("cached_assets"), QStringLiteral("unavailable_assets"), QStringLiteral("cues")}) ||
        !identifier(review.value(QStringLiteral("request_id"))) || review.value(QStringLiteral("project_id")) != snapshot.value(QStringLiteral("project_id")) ||
        review.value(QStringLiteral("sequence_id")) != snapshot.value(QStringLiteral("sequence_id")) ||
        review.value(QStringLiteral("base_revision")) != snapshot.value(QStringLiteral("revision")) ||
        review.value(QStringLiteral("frame_rate")) != snapshot.value(QStringLiteral("frame_rate")) || !review.value(QStringLiteral("cues")).isArray() ||
        review.value(QStringLiteral("cues")).toArray().size() > 3 || !integer(review.value(QStringLiteral("cached_assets")), 1) ||
        !integer(review.value(QStringLiteral("unavailable_assets"))))
        return invalid();
    QMap<QString, QJsonObject> clips;
    QSet<QString> bins, instances, categories;
    for (const auto v : spec.value(QStringLiteral("clips")).toArray()) {
        const auto c = v.toObject();
        clips[c.value(QStringLiteral("instance_id")).toString()] = c;
        bins.insert(c.value(QStringLiteral("bin_id")).toString());
    }
    if (review.value(QStringLiteral("cached_assets")).toInteger() + review.value(QStringLiteral("unavailable_assets")).toInteger() != bins.size())
        return invalid();
    const auto rate = snapshot.value(QStringLiteral("frame_rate")).toObject();
    const qint64 num = rate.value(QStringLiteral("numerator")).toInteger(), den = rate.value(QStringLiteral("denominator")).toInteger();
    qint64 previousStart = -1, previousEnd = -1;
    for (const auto v : review.value(QStringLiteral("cues")).toArray()) {
        const auto c = v.toObject();
        const auto id = c.value(QStringLiteral("instance_id")).toString(), category = c.value(QStringLiteral("category")).toString();
        const auto clip = clips.value(id);
        if (!fields(c, {QStringLiteral("instance_id"), QStringLiteral("category"), QStringLiteral("start_frame"), QStringLiteral("duration_frames"),
                        QStringLiteral("instruction"), QStringLiteral("reason"), QStringLiteral("source_sha256")}) ||
            !range(c, QStringLiteral("start_frame")) || clip.isEmpty() || instances.contains(id) || categories.contains(category) ||
            !QStringList{QStringLiteral("waves"), QStringLiteral("water"), QStringLiteral("kart_engine"), QStringLiteral("wind"), QStringLiteral("footsteps")}
                 .contains(category) ||
            !c.value(QStringLiteral("instruction")).isString() || c.value(QStringLiteral("instruction")).toString().trimmed().isEmpty() ||
            c.value(QStringLiteral("instruction")).toString().size() > 1000 || !c.value(QStringLiteral("reason")).isString() ||
            c.value(QStringLiteral("reason")).toString().trimmed().isEmpty() || c.value(QStringLiteral("reason")).toString().size() > 600 ||
            !QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(c.value(QStringLiteral("source_sha256")).toString()).hasMatch())
            return invalid();
        const qint64 start = c.value(QStringLiteral("start_frame")).toInteger(), length = c.value(QStringLiteral("duration_frames")).toInteger();
        if (2 * length * den < num || length * den > 2 * num || start < clip.value(QStringLiteral("start_frame")).toInteger() ||
            start + length > clip.value(QStringLiteral("start_frame")).toInteger() + clip.value(QStringLiteral("duration_frames")).toInteger() ||
            start < previousEnd || (previousStart >= 0 && (start - previousStart) * den < 3 * num))
            return invalid();
        QJsonObject effect{{QStringLiteral("path"), QStringLiteral("/editor-issued-placeholder.wav")},
                           {QStringLiteral("start_frame"), start},
                           {QStringLiteral("duration_frames"), length},
                           {QStringLiteral("instruction"), c.value(QStringLiteral("instruction"))}};
        bool available = false;
        for (const auto t : snapshot.value(QStringLiteral("tracks")).toArray()) {
            effect[QStringLiteral("track_id")] = t.toObject().value(QStringLiteral("track_id"));
            available |= validateEffectRequest(snapshot, effect).isEmpty();
        }
        if (!available) return invalid();
        instances.insert(id);
        categories.insert(category);
        previousStart = start;
        previousEnd = start + length;
    }
    return {};
}

QString Montaz::validateEffectRequest(const QJsonObject &snapshot, const QJsonObject &effect)
{
    const auto path = effect.value(QStringLiteral("path")).toString();
    if (!validateSnapshot(snapshot).isEmpty() ||
        !fields(effect, {QStringLiteral("path"), QStringLiteral("track_id"), QStringLiteral("start_frame"), QStringLiteral("duration_frames"),
                         QStringLiteral("instruction")}) ||
        !range(effect, QStringLiteral("start_frame")) || !effect.value(QStringLiteral("path")).isString() || path.size() > 4096 ||
        !QFileInfo(path).isAbsolute() || QFileInfo(path).suffix() != QStringLiteral("wav") || !effect.value(QStringLiteral("instruction")).isString() ||
        effect.value(QStringLiteral("instruction")).toString().trimmed().isEmpty() || effect.value(QStringLiteral("instruction")).toString().size() > 1000)
        return invalid();
    const auto rate = snapshot.value(QStringLiteral("frame_rate")).toObject();
    const qint64 num = rate.value(QStringLiteral("numerator")).toInteger(), den = rate.value(QStringLiteral("denominator")).toInteger();
    const qint64 start = effect.value(QStringLiteral("start_frame")).toInteger(), length = effect.value(QStringLiteral("duration_frames")).toInteger();
    if (2 * length * den < num || length * den > 10 * num) return invalid();
    QSet<QString> videoTracks;
    bool target = false, covered = false;
    for (const auto value : snapshot.value(QStringLiteral("tracks")).toArray()) {
        const auto track = value.toObject();
        if (track.value(QStringLiteral("kind")) == QStringLiteral("video")) videoTracks.insert(track.value(QStringLiteral("track_id")).toString());
        if (track.value(QStringLiteral("track_id")) == effect.value(QStringLiteral("track_id")) &&
            track.value(QStringLiteral("kind")) == QStringLiteral("audio") && !track.value(QStringLiteral("locked")).toBool())
            target = true;
    }
    for (const auto value : snapshot.value(QStringLiteral("occupied")).toArray()) {
        const auto item = value.toObject();
        const qint64 position = item.value(QStringLiteral("start_frame")).toInteger(),
                     end = position + item.value(QStringLiteral("duration_frames")).toInteger();
        if (item.value(QStringLiteral("track_id")) == effect.value(QStringLiteral("track_id")) && start < end && position < start + length) return invalid();
        if (videoTracks.contains(item.value(QStringLiteral("track_id")).toString()) && position <= start && start + length <= end) covered = true;
    }
    return target && covered ? QString() : invalid();
}

QString Montaz::validateSoundtrack(const QJsonObject &snapshot, const QJsonObject &music, const QJsonObject &soundtrack, bool effect)
{
    if (!(effect ? validateEffectRequest(snapshot, music) : validateMusicRequest(snapshot, music)).isEmpty() ||
        !fields(soundtrack, {QStringLiteral("request_id"), QStringLiteral("project_id"), QStringLiteral("sequence_id"), QStringLiteral("base_revision"),
                             QStringLiteral("frame_rate"), QStringLiteral("path"), QStringLiteral("track_id"), QStringLiteral("start_frame"),
                             QStringLiteral("duration_frames"), QStringLiteral("sha256"), QStringLiteral("accents"), QStringLiteral("cached")}) ||
        !identifier(soundtrack.value(QStringLiteral("request_id"))) ||
        soundtrack.value(QStringLiteral("project_id")) != snapshot.value(QStringLiteral("project_id")) ||
        soundtrack.value(QStringLiteral("sequence_id")) != snapshot.value(QStringLiteral("sequence_id")) ||
        soundtrack.value(QStringLiteral("base_revision")) != snapshot.value(QStringLiteral("revision")) ||
        soundtrack.value(QStringLiteral("frame_rate")) != snapshot.value(QStringLiteral("frame_rate")) ||
        !QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(soundtrack.value(QStringLiteral("sha256")).toString()).hasMatch() ||
        !soundtrack.value(QStringLiteral("cached")).isBool() || !soundtrack.value(QStringLiteral("accents")).isArray())
        return invalid();
    for (const auto &key : {QStringLiteral("path"), QStringLiteral("track_id"), QStringLiteral("start_frame"), QStringLiteral("duration_frames")})
        if (soundtrack.value(key) != music.value(key)) return invalid();
    const auto accents = soundtrack.value(QStringLiteral("accents")).toArray();
    if (accents.size() > 128 || (effect && !accents.isEmpty())) return invalid();
    qint64 previous = 0;
    for (const auto value : accents) {
        if (!integer(value, 1) || value.toInteger() <= previous || value.toInteger() >= soundtrack.value(QStringLiteral("duration_frames")).toInteger())
            return invalid();
        previous = value.toInteger();
    }
    return {};
}

QString Montaz::validateTimingRequest(const QJsonObject &snapshot, const QJsonObject &timing)
{
    const auto music = timing.value(QStringLiteral("music")).toObject();
    const auto path = music.value(QStringLiteral("path")).toString();
    const auto clips = timing.value(QStringLiteral("clips")).toArray();
    if (!validateSnapshot(snapshot).isEmpty() || !fields(timing, {QStringLiteral("clips"), QStringLiteral("music"), QStringLiteral("max_shift_frames")}) ||
        !fields(music, {QStringLiteral("path"), QStringLiteral("sha256"), QStringLiteral("instance_id")}) || path.size() > 4096 ||
        !QFileInfo(path).isAbsolute() || QFileInfo(path).suffix() != QStringLiteral("wav") || !identifier(music.value(QStringLiteral("instance_id"))) ||
        !QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(music.value(QStringLiteral("sha256")).toString()).hasMatch() ||
        !timing.value(QStringLiteral("clips")).isArray() || clips.size() < 2 || clips.size() > 256 ||
        !integer(timing.value(QStringLiteral("max_shift_frames")), 1))
        return invalid();
    const auto rate = snapshot.value(QStringLiteral("frame_rate")).toObject();
    const qint64 num = rate.value(QStringLiteral("numerator")).toInteger(), den = rate.value(QStringLiteral("denominator")).toInteger();
    if (timing.value(QStringLiteral("max_shift_frames")).toInteger() != qMax(qint64(1), num / (4 * den))) return invalid();
    QMap<QString, QJsonObject> assets, tracks, occupied;
    for (const auto value : snapshot.value(QStringLiteral("assets")).toArray())
        assets[value.toObject().value(QStringLiteral("bin_id")).toString()] = value.toObject();
    for (const auto value : snapshot.value(QStringLiteral("tracks")).toArray())
        tracks[value.toObject().value(QStringLiteral("track_id")).toString()] = value.toObject();
    for (const auto value : snapshot.value(QStringLiteral("occupied")).toArray())
        occupied[value.toObject().value(QStringLiteral("instance_id")).toString()] = value.toObject();
    QSet<QString> seen;
    QString trackId;
    qint64 previous = -1;
    for (const auto value : clips) {
        const auto c = value.toObject();
        const auto id = c.value(QStringLiteral("instance_id")).toString();
        const auto asset = assets.value(c.value(QStringLiteral("bin_id")).toString());
        const auto track = tracks.value(c.value(QStringLiteral("track_id")).toString());
        const auto item = occupied.value(id);
        const qint64 start = c.value(QStringLiteral("start_frame")).toInteger(), length = c.value(QStringLiteral("duration_frames")).toInteger();
        if (!fields(c, {QStringLiteral("instance_id"), QStringLiteral("bin_id"), QStringLiteral("track_id"), QStringLiteral("start_frame"),
                        QStringLiteral("in_frame"), QStringLiteral("duration_frames")}) ||
            !identifier(c.value(QStringLiteral("instance_id"))) || !range(c, QStringLiteral("start_frame")) || !range(c, QStringLiteral("in_frame")) ||
            !asset.value(QStringLiteral("has_video")).toBool() || track.value(QStringLiteral("kind")) != QStringLiteral("video") ||
            track.value(QStringLiteral("locked")).toBool() || length < (num + den - 1) / den ||
            c.value(QStringLiteral("in_frame")).toInteger() + length > asset.value(QStringLiteral("duration_frames")).toInteger() ||
            item.value(QStringLiteral("track_id")) != c.value(QStringLiteral("track_id")) ||
            item.value(QStringLiteral("start_frame")) != c.value(QStringLiteral("start_frame")) ||
            item.value(QStringLiteral("duration_frames")) != c.value(QStringLiteral("duration_frames")) || seen.contains(id) ||
            (previous >= 0 && previous != start) || (!trackId.isEmpty() && trackId != c.value(QStringLiteral("track_id")).toString()))
            return invalid();
        trackId = c.value(QStringLiteral("track_id")).toString();
        previous = start + length;
        seen.insert(id);
    }
    const auto audio = occupied.value(music.value(QStringLiteral("instance_id")).toString());
    const qint64 length = audio.value(QStringLiteral("duration_frames")).toInteger();
    if (audio.isEmpty() || tracks.value(audio.value(QStringLiteral("track_id")).toString()).value(QStringLiteral("kind")) != QStringLiteral("audio") ||
        audio.value(QStringLiteral("start_frame")) != clips.first().toObject().value(QStringLiteral("start_frame")) ||
        audio.value(QStringLiteral("start_frame")).toInteger() + length != previous || length * den < 3 * num || length * den > 600 * num)
        return invalid();
    seen.insert(music.value(QStringLiteral("instance_id")).toString());
    return seen.size() == occupied.size() ? QString() : invalid();
}

QString Montaz::validateTimingProposal(const QJsonObject &snapshot, const QJsonObject &timing, const QJsonObject &proposal)
{
    if (!validateTimingRequest(snapshot, timing).isEmpty() ||
        !fields(proposal, {QStringLiteral("request_id"), QStringLiteral("project_id"), QStringLiteral("sequence_id"), QStringLiteral("base_revision"),
                           QStringLiteral("frame_rate"), QStringLiteral("protocol_version"), QStringLiteral("music_sha256"), QStringLiteral("accents"),
                           QStringLiteral("operations")}) ||
        !identifier(proposal.value(QStringLiteral("request_id"))) ||
        proposal.value(QStringLiteral("project_id")) != snapshot.value(QStringLiteral("project_id")) ||
        proposal.value(QStringLiteral("sequence_id")) != snapshot.value(QStringLiteral("sequence_id")) ||
        proposal.value(QStringLiteral("base_revision")) != snapshot.value(QStringLiteral("revision")) ||
        proposal.value(QStringLiteral("frame_rate")) != snapshot.value(QStringLiteral("frame_rate")) ||
        proposal.value(QStringLiteral("protocol_version")) != 2 ||
        proposal.value(QStringLiteral("music_sha256")) != timing.value(QStringLiteral("music")).toObject().value(QStringLiteral("sha256")) ||
        !proposal.value(QStringLiteral("accents")).isArray() || !proposal.value(QStringLiteral("operations")).isArray())
        return invalid();
    const auto originals = timing.value(QStringLiteral("clips")).toArray(), operations = proposal.value(QStringLiteral("operations")).toArray();
    const auto first = originals.first().toObject().value(QStringLiteral("start_frame")).toInteger();
    const auto last = originals.last().toObject();
    const auto end = last.value(QStringLiteral("start_frame")).toInteger() + last.value(QStringLiteral("duration_frames")).toInteger();
    QSet<qint64> accents;
    qint64 previous = 0;
    for (const auto value : proposal.value(QStringLiteral("accents")).toArray()) {
        if (!integer(value, 1) || value.toInteger() <= previous || value.toInteger() >= end - first || accents.size() >= 128) return invalid();
        previous = value.toInteger();
        accents.insert(first + previous);
    }
    if (operations.size() != originals.size()) return invalid();
    auto candidate = timing;
    candidate[QStringLiteral("clips")] = operations;
    // Validate the new clips against their own occupancy while retaining native source bounds.
    auto changedSnapshot = snapshot;
    QJsonArray ranges;
    for (const auto value : snapshot.value(QStringLiteral("occupied")).toArray())
        if (value.toObject().value(QStringLiteral("instance_id")) == timing.value(QStringLiteral("music")).toObject().value(QStringLiteral("instance_id")))
            ranges.append(value);
    bool changed = false;
    for (int i = 0; i < operations.size(); ++i) {
        auto op = operations[i].toObject();
        const auto original = originals[i].toObject();
        if (!range(op, QStringLiteral("start_frame"))) return invalid();
        for (const auto &key : {QStringLiteral("instance_id"), QStringLiteral("bin_id"), QStringLiteral("track_id"), QStringLiteral("in_frame")})
            if (op.value(key) != original.value(key)) return invalid();
        const auto start = op.value(QStringLiteral("start_frame")).toInteger(), old = original.value(QStringLiteral("start_frame")).toInteger();
        if (qAbs(start - old) > timing.value(QStringLiteral("max_shift_frames")).toInteger() || (i == 0 && start != first) ||
            (start != old && !accents.contains(start)))
            return invalid();
        changed |= op != original;
        op.remove(QStringLiteral("bin_id"));
        op.remove(QStringLiteral("in_frame"));
        ranges.append(op);
    }
    changedSnapshot[QStringLiteral("occupied")] = ranges;
    return changed ? validateTimingRequest(changedSnapshot, candidate) : invalid();
}

QString Montaz::validateCaptionRequest(const QJsonObject &snapshot, const QJsonObject &spec)
{
    if (!validateSnapshot(snapshot).isEmpty() || !fields(spec, {QStringLiteral("clips")}) || !spec.value(QStringLiteral("clips")).isArray() ||
        spec.value(QStringLiteral("clips")).toArray().size() > 8)
        return invalid();
    QMap<QString, QJsonObject> assets, tracks, occupied;
    for (const auto v : snapshot.value(QStringLiteral("assets")).toArray())
        assets[v.toObject().value(QStringLiteral("bin_id")).toString()] = v.toObject();
    for (const auto v : snapshot.value(QStringLiteral("tracks")).toArray())
        tracks[v.toObject().value(QStringLiteral("track_id")).toString()] = v.toObject();
    for (const auto v : snapshot.value(QStringLiteral("occupied")).toArray())
        occupied[v.toObject().value(QStringLiteral("instance_id")).toString()] = v.toObject();
    const auto rate = snapshot.value(QStringLiteral("frame_rate")).toObject();
    const qint64 num = rate.value(QStringLiteral("numerator")).toInteger(), den = rate.value(QStringLiteral("denominator")).toInteger();
    qint64 previous = -1, total = 0;
    QSet<QString> seen;
    for (const auto v : spec.value(QStringLiteral("clips")).toArray()) {
        const auto c = v.toObject(), asset = assets.value(c.value(QStringLiteral("bin_id")).toString()),
                   item = occupied.value(c.value(QStringLiteral("instance_id")).toString());
        const auto id = c.value(QStringLiteral("instance_id")).toString();
        const qint64 start = c.value(QStringLiteral("start_frame")).toInteger(), length = c.value(QStringLiteral("duration_frames")).toInteger();
        if (!fields(c, {QStringLiteral("instance_id"), QStringLiteral("bin_id"), QStringLiteral("track_id"), QStringLiteral("start_frame"),
                        QStringLiteral("in_frame"), QStringLiteral("duration_frames"), QStringLiteral("audio_stream")}) ||
            !identifier(c.value(QStringLiteral("instance_id"))) || !range(c, QStringLiteral("start_frame")) || !range(c, QStringLiteral("in_frame")) ||
            !integer(c.value(QStringLiteral("audio_stream"))) || c.value(QStringLiteral("audio_stream")).toInteger() > 63 ||
            !asset.value(QStringLiteral("has_audio")).toBool() ||
            tracks.value(c.value(QStringLiteral("track_id")).toString()).value(QStringLiteral("kind")) != QStringLiteral("audio") ||
            item.value(QStringLiteral("track_id")) != c.value(QStringLiteral("track_id")) ||
            item.value(QStringLiteral("start_frame")) != c.value(QStringLiteral("start_frame")) ||
            item.value(QStringLiteral("duration_frames")) != c.value(QStringLiteral("duration_frames")) ||
            c.value(QStringLiteral("in_frame")).toInteger() + length > asset.value(QStringLiteral("duration_frames")).toInteger() || seen.contains(id) ||
            start < previous || length * den > 30 * num)
            return invalid();
        total += length;
        previous = start + length;
        seen.insert(id);
    }
    return total * den <= 120 * num ? QString() : invalid();
}

QString Montaz::validateCaptions(const QJsonObject &snapshot, const QJsonObject &spec, const QJsonObject &review)
{
    if (!validateCaptionRequest(snapshot, spec).isEmpty() ||
        !fields(review, {QStringLiteral("request_id"), QStringLiteral("project_id"), QStringLiteral("sequence_id"), QStringLiteral("base_revision"),
                         QStringLiteral("frame_rate"), QStringLiteral("checked_clips"), QStringLiteral("cached_clips"), QStringLiteral("skipped_clips"),
                         QStringLiteral("source_sha256"), QStringLiteral("captions")}) ||
        !identifier(review.value(QStringLiteral("request_id"))) || review.value(QStringLiteral("project_id")) != snapshot.value(QStringLiteral("project_id")) ||
        review.value(QStringLiteral("sequence_id")) != snapshot.value(QStringLiteral("sequence_id")) ||
        review.value(QStringLiteral("base_revision")) != snapshot.value(QStringLiteral("revision")) ||
        review.value(QStringLiteral("frame_rate")) != snapshot.value(QStringLiteral("frame_rate")) || !review.value(QStringLiteral("captions")).isArray() ||
        review.value(QStringLiteral("captions")).toArray().size() > 128 || !review.value(QStringLiteral("source_sha256")).isObject())
        return invalid();
    const int total = spec.value(QStringLiteral("clips")).toArray().size();
    for (const auto &key : {QStringLiteral("checked_clips"), QStringLiteral("cached_clips"), QStringLiteral("skipped_clips")})
        if (!integer(review.value(key)) || review.value(key).toInteger() > total) return invalid();
    if (review.value(QStringLiteral("checked_clips")) != total) return invalid();
    QMap<QString, QJsonObject> clips;
    QSet<QString> bins, captioned;
    for (const auto v : spec.value(QStringLiteral("clips")).toArray()) {
        const auto c = v.toObject();
        clips[c.value(QStringLiteral("instance_id")).toString()] = c;
        bins.insert(c.value(QStringLiteral("bin_id")).toString());
    }
    const auto hashes = review.value(QStringLiteral("source_sha256")).toObject();
    const auto keys = hashes.keys();
    if (QSet<QString>(keys.begin(), keys.end()) != bins) return invalid();
    for (const auto v : hashes)
        if (!QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(v.toString()).hasMatch()) return invalid();
    const auto rate = snapshot.value(QStringLiteral("frame_rate")).toObject();
    const qint64 maximum = (6 * rate.value(QStringLiteral("numerator")).toInteger() + rate.value(QStringLiteral("denominator")).toInteger() - 1) /
                               rate.value(QStringLiteral("denominator")).toInteger() +
                           1;
    qint64 previous = -1;
    for (const auto v : review.value(QStringLiteral("captions")).toArray()) {
        const auto c = v.toObject(), clip = clips.value(c.value(QStringLiteral("instance_id")).toString());
        const auto text = c.value(QStringLiteral("text")).toString();
        const qint64 start = c.value(QStringLiteral("start_frame")).toInteger(), length = c.value(QStringLiteral("duration_frames")).toInteger();
        if (!fields(c, {QStringLiteral("instance_id"), QStringLiteral("start_frame"), QStringLiteral("duration_frames"), QStringLiteral("text")}) ||
            !range(c, QStringLiteral("start_frame")) || clip.isEmpty() || length > maximum || start < previous ||
            start < clip.value(QStringLiteral("start_frame")).toInteger() ||
            start + length > clip.value(QStringLiteral("start_frame")).toInteger() + clip.value(QStringLiteral("duration_frames")).toInteger() ||
            !c.value(QStringLiteral("text")).isString() || text.trimmed().isEmpty() || text.size() > 84 || text.count(QLatin1Char('\n')) > 1 ||
            text.contains(QLatin1Char('{')) || text.contains(QLatin1Char('}')) || text.contains(QLatin1Char('\\')))
            return invalid();
        for (const auto ch : text)
            if (ch.category() == QChar::Other_Control && ch != QLatin1Char('\n')) return invalid();
        previous = start + length;
        captioned.insert(c.value(QStringLiteral("instance_id")).toString());
    }
    return review.value(QStringLiteral("skipped_clips")) == total - captioned.size() ? QString() : invalid();
}
