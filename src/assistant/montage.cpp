// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "montage.h"
#include "proposal.h"

#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
bool fields(const QJsonObject &object, const QStringList &names)
{
    const auto keys = object.keys();
    return QSet<QString>(keys.begin(), keys.end()) == QSet<QString>(names.begin(), names.end());
}
bool number(const QJsonValue &value, double low, double high)
{
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= low && value.toDouble() <= high;
}
bool integer(const QJsonValue &value, int low = 0)
{
    return number(value, low, std::numeric_limits<int>::max()) && std::floor(value.toDouble()) == value.toDouble();
}
bool text(const QJsonValue &value, int limit)
{
    return value.isString() && !value.toString().trimmed().isEmpty() && value.toString().size() <= limit;
}
bool identifier(const QJsonValue &value)
{
    if (!text(value, 128)) return false;
    for (const auto c : value.toString())
        if (c.unicode() < 32 || c.unicode() == 127) return false;
    return true;
}
QString invalid()
{
    return QStringLiteral("invalid_contract");
}
} // namespace

QString Montaz::validateMontageRequest(const QJsonObject &snapshot, const QJsonObject &spec)
{
    if (!validateSnapshot(snapshot).isEmpty() || !fields(spec, {"clips", "width", "height", "shot_frames", "track_id", "start_frame"}) ||
        !integer(spec.value("width"), 32) || !integer(spec.value("height"), 32) || spec.value("width").toInt() > 8192 || spec.value("height").toInt() > 8192 ||
        !integer(spec.value("shot_frames"), 8) || spec.value("shot_frames").toInt() > 12 || !integer(spec.value("start_frame")) ||
        !spec.value("clips").isArray())
        return invalid();
    const auto fps = snapshot.value("frame_rate").toObject();
    const double rate = fps.value("numerator").toDouble() / fps.value("denominator").toDouble();
    const double aspect = spec.value("width").toDouble() / spec.value("height").toDouble();
    const auto clips = spec.value("clips").toArray();
    if (rate < 1 || rate > 60 || aspect < 0.25 || aspect > 4 || clips.size() < 2 || clips.size() > 6 ||
        spec.value("start_frame").toInteger() + clips.size() * spec.value("shot_frames").toInteger() > std::numeric_limits<int>::max())
        return invalid();
    QMap<QString, QJsonObject> assets, occupied;
    for (const auto v : snapshot.value("assets").toArray())
        assets.insert(v.toObject().value("bin_id").toString(), v.toObject());
    for (const auto v : snapshot.value("occupied").toArray()) {
        const auto o = v.toObject();
        occupied.insert(o.value("instance_id").toString(), o);
        // Append beyond the entire sequence, preserving existing video/audio.
        if (o.value("start_frame").toInteger() + o.value("duration_frames").toInteger() > spec.value("start_frame").toInteger()) return invalid();
    }
    bool track = false;
    for (const auto v : snapshot.value("tracks").toArray()) {
        const auto t = v.toObject();
        if (t.value("track_id") == spec.value("track_id")) track = t.value("kind") == "video" && !t.value("locked").toBool();
    }
    if (!track) return invalid();
    QSet<QString> ids, bins;
    qint64 previous = -1;
    for (const auto v : clips) {
        const auto c = v.toObject();
        if (!fields(c, {"instance_id", "bin_id", "track_id", "start_frame", "in_frame", "duration_frames", "zoom_limit"}) ||
            !c.value("instance_id").isString() || !c.value("bin_id").isString() || !integer(c.value("start_frame")) || !integer(c.value("in_frame")) ||
            !integer(c.value("duration_frames"), spec.value("shot_frames").toInt()) || !number(c.value("zoom_limit"), 1, 2) ||
            c.value("track_id") != spec.value("track_id"))
            return invalid();
        const auto id = c.value("instance_id").toString(), bin = c.value("bin_id").toString();
        const auto a = assets.value(bin);
        auto o = c;
        o.remove("bin_id");
        o.remove("in_frame");
        o.remove("zoom_limit");
        if (!occupied.contains(id) || occupied.value(id) != o || ids.contains(id) || bins.contains(bin) || !a.value("has_video").toBool() ||
            c.value("in_frame").toInteger() + c.value("duration_frames").toInteger() > a.value("duration_frames").toInteger() ||
            c.value("start_frame").toInteger() < previous)
            return invalid();
        ids.insert(id);
        bins.insert(bin);
        previous = c.value("start_frame").toInteger() + c.value("duration_frames").toInteger();
    }
    return {};
}

QString Montaz::validateMontageReview(const QJsonObject &snapshot, const QJsonObject &spec, const QJsonObject &review)
{
    if (!validateMontageRequest(snapshot, spec).isEmpty() ||
        !fields(review, {"request_id", "project_id", "sequence_id", "base_revision", "frame_rate", "protocol_version", "status", "reason", "source_hashes",
                         "operations", "target", "excluded"}) ||
        !identifier(review.value("request_id")) || review.value("project_id") != snapshot.value("project_id") ||
        review.value("sequence_id") != snapshot.value("sequence_id") || review.value("base_revision") != snapshot.value("revision") ||
        review.value("frame_rate") != snapshot.value("frame_rate") || review.value("protocol_version") != 5 || !text(review.value("reason"), 600) ||
        !review.value("source_hashes").isObject() || !review.value("operations").isArray() || !review.value("excluded").isArray() ||
        !review.value("target").isObject())
        return invalid();
    QMap<QString, QJsonObject> clips;
    QSet<QString> bins, consumed;
    for (const auto v : spec.value("clips").toArray()) {
        const auto c = v.toObject();
        clips.insert(c.value("instance_id").toString(), c);
        bins.insert(c.value("bin_id").toString());
    }
    const auto hashes = review.value("source_hashes").toObject();
    const auto keys = hashes.keys();
    if (QSet<QString>(keys.begin(), keys.end()) != bins) return invalid();
    for (const auto v : hashes)
        if (!v.isString() || !QRegularExpression(QStringLiteral("\\A[a-f0-9]{64}\\z")).match(v.toString()).hasMatch()) return invalid();
    const auto ops = review.value("operations").toArray(), excluded = review.value("excluded").toArray();
    if (ops.size() > clips.size() || excluded.size() > clips.size()) return invalid();
    for (const auto v : excluded) {
        const auto e = v.toObject();
        const auto id = e.value("source_instance_id").toString();
        if (!fields(e, {"source_instance_id", "reason"}) || !clips.contains(id) || consumed.contains(id) || !text(e.value("reason"), 200)) return invalid();
        consumed.insert(id);
    }
    const auto target = review.value("target").toObject();
    if (review.value("status") == "no_match") return ops.isEmpty() && target.isEmpty() && consumed.size() == clips.size() ? QString() : invalid();
    if (review.value("status") != "matched" || ops.size() < 2 || !fields(target, {"x", "y", "height"}) || !number(target.value("x"), 0, 1) ||
        !number(target.value("y"), 0, 1) || !number(target.value("height"), 0.001, 1))
        return invalid();
    const double toleranceX = 2.0 / spec.value("width").toInt(), toleranceY = 2.0 / spec.value("height").toInt();
    for (const auto v : ops) {
        const auto op = v.toObject(), c = clips.value(op.value("source_instance_id").toString());
        const auto id = op.value("source_instance_id").toString();
        const int length = spec.value("shot_frames").toInt();
        if (!fields(op, {"source_instance_id", "in_frame", "keyframes"}) || c.isEmpty() || consumed.contains(id) || !integer(op.value("in_frame")) ||
            op.value("in_frame").toInteger() < c.value("in_frame").toInteger() ||
            op.value("in_frame").toInteger() + length > c.value("in_frame").toInteger() + c.value("duration_frames").toInteger() ||
            !op.value("keyframes").isArray() || op.value("keyframes").toArray().size() != length)
            return invalid();
        consumed.insert(id);
        QJsonObject previous;
        for (int j = 0; j < length; ++j) {
            const auto k = op.value("keyframes").toArray()[j].toObject();
            if (!fields(k, {"frame", "x", "y", "scale", "anchor_x", "anchor_y", "anchor_height"}) || k.value("frame") != j ||
                !number(k.value("scale"), 1, c.value("zoom_limit").toDouble()) || !number(k.value("x"), -1, 0.000001) || !number(k.value("y"), -1, 0.000001) ||
                !number(k.value("anchor_x"), 0, 1) || !number(k.value("anchor_y"), 0, 1) || !number(k.value("anchor_height"), 0.001, 1))
                return invalid();
            const double s = k.value("scale").toDouble(), x = k.value("x").toDouble(), y = k.value("y").toDouble();
            if (x + s < 0.999999 || y + s < 0.999999 || std::abs(x + s * k.value("anchor_x").toDouble() - target.value("x").toDouble()) > toleranceX ||
                std::abs(y + s * k.value("anchor_y").toDouble() - target.value("y").toDouble()) > toleranceY ||
                std::abs(s * k.value("anchor_height").toDouble() - target.value("height").toDouble()) > toleranceY)
                return invalid();
            if (!previous.isEmpty()) {
                const double ds = s - previous.value("scale").toDouble(), dx = x - previous.value("x").toDouble(), dy = y - previous.value("y").toDouble();
                if (std::abs(ds) > 0.08) return invalid();
                for (int u = 0; u <= 1; ++u)
                    for (int w = 0; w <= 1; ++w)
                        if (std::hypot(dx + ds * u, dy + ds * w) > 0.05) return invalid();
            }
            previous = k;
        }
    }
    return consumed.size() == clips.size() ? QString() : invalid();
}
