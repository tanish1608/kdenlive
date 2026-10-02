// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QString>

namespace Montaz {
enum class ChatAction { Edit, Music, SoundSuggestions, SoundEffect, BeatCuts, MatchCut, UnavailableTransition, InvalidSound };
struct ChatCommand
{
    ChatAction action{ChatAction::Edit};
    QString description;
    double seconds{2.0};
};
// Only standalone, explicit requests invoke tools. A normal editing brief,
// negation or discussion of a transition remains an editing instruction.
ChatCommand chatCommand(const QString &message);
} // namespace Montaz
