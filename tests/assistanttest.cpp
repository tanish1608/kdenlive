// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "assistant/editoradapter.h"
#include "doc/kdenlivedoc.h"
#include "test_utils.hpp"

#include <QJsonArray>

TEST_CASE("Assistant applies six video inserts in one undoable batch", "[Montaz]")
{
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto stack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(stack);
    pCore->projectManager()->testSetDocument(&document);
    auto timeline = TimelineItemModel::construct(document.uuid(), stack);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int video = -1, audio = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, video));
    REQUIRE(timeline->requestTrackInsertion(-1, audio, QString(), true));
    for (int i = 0; i < 6; ++i)
        KdenliveTests::createAVProducer(pCore->getProjectProfile(), bin);
    stack->clear();
    {
        Montaz::EditorAdapter adapter;
        const auto snapshot = adapter.snapshot();
        REQUIRE_FALSE(snapshot.isEmpty());
        REQUIRE(snapshot.value(QStringLiteral("assets")).toArray().size() == 6);
        QJsonArray operations;
        int position = 0;
        for (const auto value : snapshot.value(QStringLiteral("assets")).toArray()) {
            const auto asset = value.toObject();
            const int duration = std::min(10, asset.value(QStringLiteral("duration_frames")).toInt());
            operations.append(QJsonObject{{QStringLiteral("instance_id"), QStringLiteral("demo:%1").arg(position)},
                                          {QStringLiteral("bin_id"), asset.value(QStringLiteral("bin_id"))},
                                          {QStringLiteral("track_id"), QString::number(video)},
                                          {QStringLiteral("start_frame"), position},
                                          {QStringLiteral("in_frame"), 0},
                                          {QStringLiteral("duration_frames"), duration}});
            position += duration;
        }
        const QJsonObject proposal{{QStringLiteral("request_id"), QStringLiteral("six-shots")},
                                   {QStringLiteral("project_id"), snapshot.value(QStringLiteral("project_id"))},
                                   {QStringLiteral("sequence_id"), snapshot.value(QStringLiteral("sequence_id"))},
                                   {QStringLiteral("base_revision"), snapshot.value(QStringLiteral("revision"))},
                                   {QStringLiteral("frame_rate"), snapshot.value(QStringLiteral("frame_rate"))},
                                   {QStringLiteral("operations"), operations},
                                   {QStringLiteral("protocol_version"), 1}};
        SECTION("Apply, duplicate delivery, undo, redo, exact cut frames")
        {
            REQUIRE(adapter.apply(proposal).isEmpty());
            REQUIRE(stack->count() == 1);
            REQUIRE(timeline->getTrackClipsCount(video) == 6);
            REQUIRE(timeline->getTrackClipsCount(audio) == 0);
            REQUIRE(timeline->checkConsistency());
            REQUIRE_FALSE(adapter.apply(proposal).isEmpty());
            REQUIRE(timeline->getTrackClipsCount(video) == 6);
            const auto clips = adapter.snapshot().value(QStringLiteral("occupied")).toArray();
            for (const auto value : clips) {
                const auto clip = value.toObject();
                const int start = clip.value(QStringLiteral("start_frame")).toInt();
                const int duration = clip.value(QStringLiteral("duration_frames")).toInt();
                const int id = clip.value(QStringLiteral("instance_id")).toString().toInt();
                REQUIRE(timeline->getClipByPosition(video, start) == id);
                REQUIRE(timeline->getClipByPosition(video, start + duration - 1) == id);
            }
            stack->undo();
            REQUIRE(timeline->getTrackClipsCount(video) == 0);
            REQUIRE(timeline->checkConsistency());
            REQUIRE_FALSE(adapter.apply(proposal).isEmpty());
            stack->redo();
            REQUIRE(timeline->getTrackClipsCount(video) == 6);
            REQUIRE(timeline->checkConsistency());
        }
        SECTION("Lock and unlock invalidates outstanding proposals")
        {
            timeline->setTrackLockedState(video, true);
            timeline->setTrackLockedState(video, false);
            REQUIRE_FALSE(adapter.apply(proposal).isEmpty());
            REQUIRE(timeline->getTrackClipsCount(video) == 0);
        }
        SECTION("Source bounds reject the entire proposal before insertion")
        {
            auto invalid = proposal;
            auto items = operations;
            auto last = items.last().toObject();
            last[QStringLiteral("in_frame")] = 2147483640;
            last[QStringLiteral("duration_frames")] = 100;
            items[items.size() - 1] = last;
            invalid[QStringLiteral("operations")] = items;
            REQUIRE_FALSE(adapter.apply(invalid).isEmpty());
            REQUIRE(timeline->getTrackClipsCount(video) == 0);
            REQUIRE(stack->count() == 0);
        }
    }
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
}
