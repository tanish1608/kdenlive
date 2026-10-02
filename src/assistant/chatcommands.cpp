// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "chatcommands.h"

#include <QRegularExpression>

Montaz::ChatCommand Montaz::chatCommand(const QString &message)
{
    const auto text = message.trimmed();
    if (text.size() > 2000 || text.contains(QLatin1Char('\n'))) return {};
    const QString prefix = QStringLiteral("\\A(?:(?:please|can you|could you)\\s+)?");
    const QString suffix = QStringLiteral("[.!?]?\\z");
    const auto matches = [&](const QString &body) {
        return QRegularExpression(prefix + QStringLiteral("(?:") + body + QStringLiteral(")") + suffix, QRegularExpression::CaseInsensitiveOption).match(text);
    };
    if (matches(QStringLiteral("(?:add|compose|generate) (?:some |background )?music")).hasMatch()) return {ChatAction::Music};
    if (matches(QStringLiteral("(?:suggest|add|design) (?:some )?sound(?: effect)?s?")).hasMatch()) return {ChatAction::SoundSuggestions};
    if (matches(QStringLiteral("(?:align|sync) (?:the )?cuts(?: (?:to|with) (?:the )?(?:beats?|music))?|(?:add|use|apply) beat[- ]synced cuts")).hasMatch())
        return {ChatAction::BeatCuts};
    if (matches(QStringLiteral("(?:add|use|apply|create) (?:a )?match cuts?(?: (?:to|between) (?:the )?(?:selected|these) (?:two )?(?:clips|shots))?"))
            .hasMatch())
        return {ChatAction::MatchCut};
    const auto unavailable = matches(QStringLiteral("(?:add|use|apply) (?:a )?(whip pans?|object wipes?|speed ramps?)(?: transitions?)?"));
    if (unavailable.hasMatch()) return {ChatAction::UnavailableTransition, unavailable.captured(1)};
    const auto sound = matches(QStringLiteral("(?:add|generate|create) (?:(?:a|an) )?(?:(-?(?:\\d+(?:\\.\\d+)?|\\.\\d+))\\s*(?:s|seconds?)\\s+)?"
                                              "(?:sound(?: effect)?|sfx)(?:\\s*:\\s*|\\s+(?:of|for)\\s+|\\s+)(.+?)"));
    if (sound.hasMatch()) {
        const double seconds = sound.captured(1).isEmpty() ? 2.0 : sound.captured(1).toDouble();
        const auto description = sound.captured(2).trimmed();
        if (seconds < 0.5 || seconds > 10 || description.size() > 1000) return {ChatAction::InvalidSound};
        return {ChatAction::SoundEffect, description, seconds};
    }
    return {};
}
