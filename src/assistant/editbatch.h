// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QJsonArray>
#include <QString>
#include <functional>

namespace Montaz {
using UndoFunction = std::function<bool()>;
using InsertOperation = std::function<bool(const QJsonObject &, UndoFunction &, UndoFunction &)>;
using CommitOperation = std::function<void(const UndoFunction &, const UndoFunction &)>;

// The caller must validate against current editor state before entering the batch.
// Failed insertions must undo their own partial work, as Kdenlive's API guarantees.
QString applyInsertBatch(const QJsonArray &operations, const InsertOperation &insert, const CommitOperation &commit);
} // namespace Montaz
