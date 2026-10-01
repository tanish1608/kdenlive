// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>

namespace Montaz {
QString validateMusicRequest(const QJsonObject &snapshot, const QJsonObject &music);
QString validateSoundtrack(const QJsonObject &snapshot, const QJsonObject &music, const QJsonObject &soundtrack);
QString validateEditorialReview(const QJsonObject &snapshot, const QJsonObject &proposal, const QJsonObject &review);
constexpr int MaxMessageBytes = 1024 * 1024;

// Empty result means valid. Validation never mutates the editor.
QString validateSnapshot(const QJsonObject &snapshot);
QString validateProposal(const QJsonObject &snapshot, const QJsonObject &proposal, const QSet<QString> &appliedRequests = {});
} // namespace Montaz
