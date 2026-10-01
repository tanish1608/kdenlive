// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>

namespace Montaz {
QString validateMatchRequest(const QJsonObject &snapshot, const QJsonObject &spec);
QString validateMatchReview(const QJsonObject &snapshot, const QJsonObject &spec, const QJsonObject &review);
QString validateCaptionRequest(const QJsonObject &snapshot, const QJsonObject &spec);
QString validateCaptions(const QJsonObject &snapshot, const QJsonObject &spec, const QJsonObject &review);
QString validateCueRequest(const QJsonObject &snapshot, const QJsonObject &spec);
QString validateSoundCues(const QJsonObject &snapshot, const QJsonObject &spec, const QJsonObject &review);
QString validateTimingRequest(const QJsonObject &snapshot, const QJsonObject &timing);
QString validateTimingProposal(const QJsonObject &snapshot, const QJsonObject &timing, const QJsonObject &proposal);
QString validateMusicRequest(const QJsonObject &snapshot, const QJsonObject &music);
QString validateEffectRequest(const QJsonObject &snapshot, const QJsonObject &effect);
QString validateSoundtrack(const QJsonObject &snapshot, const QJsonObject &music, const QJsonObject &soundtrack, bool effect = false);
QString validateEditorialReview(const QJsonObject &snapshot, const QJsonObject &proposal, const QJsonObject &review);
constexpr int MaxMessageBytes = 1024 * 1024;

// Empty result means valid. Validation never mutates the editor.
QString validateSnapshot(const QJsonObject &snapshot);
QString validateProposal(const QJsonObject &snapshot, const QJsonObject &proposal, const QSet<QString> &appliedRequests = {});
} // namespace Montaz
