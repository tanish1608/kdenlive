// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QJsonObject>
#include <QString>

namespace Montaz {
// Native-only foundation. Worker dispatch/UI must opt in through a separate handoff.
QString validateMontageRequest(const QJsonObject &snapshot, const QJsonObject &spec);
QString validateMontageReview(const QJsonObject &snapshot, const QJsonObject &spec, const QJsonObject &review);
} // namespace Montaz
