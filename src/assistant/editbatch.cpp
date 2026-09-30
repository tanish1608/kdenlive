// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "editbatch.h"
#include <QJsonObject>

QString Montaz::applyInsertBatch(const QJsonArray &operations, const InsertOperation &insert, const CommitOperation &commit)
{
    if (operations.isEmpty()) return QStringLiteral("invalid_contract");
    UndoFunction undo = []() { return true; };
    UndoFunction redo = []() { return true; };
    for (const auto value : operations) {
        if (!insert(value.toObject(), undo, redo)) {
            return undo() ? QStringLiteral("apply_failed_rolled_back") : QStringLiteral("rollback_failed");
        }
    }
    commit(undo, redo);
    return {};
}
