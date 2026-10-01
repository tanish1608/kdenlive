// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "assistant/editbatch.h"
#include "assistant/editoradapter.h"
#include "assistant/proposal.h"
#include "bin/projectclip.h"
#include "doc/kdenlivedoc.h"
#include "test_utils.hpp"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <cmath>

namespace {
void writeMusicWave(const QString &path, int seconds)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    QDataStream writer(&file);
    writer.setByteOrder(QDataStream::LittleEndian);
    const quint32 samples = 48000 * seconds, bytes = samples * 4;
    writer.writeRawData("RIFF", 4);
    writer << quint32(36 + bytes);
    writer.writeRawData("WAVEfmt ", 8);
    writer << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000) << quint16(4) << quint16(16);
    writer.writeRawData("data", 4);
    writer << bytes;
    for (quint32 i = 0; i < samples; ++i) {
        const auto value = qint16(4000 * std::sin(i * 2.0 * 3.141592653589793 * 440 / 48000));
        writer << value << value;
    }
}
} // namespace

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

TEST_CASE("Assistant refines existing cuts with native identities and atomic undo", "[Montaz]")
{
    QTemporaryDir temporary;
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto stack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(stack);
    document.setUrl(QUrl::fromLocalFile(temporary.filePath(QStringLiteral("timing.kdenlive"))));
    pCore->projectManager()->testSetDocument(&document);
    auto timeline = TimelineItemModel::construct(document.uuid(), stack);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int video = -1, audio = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, video));
    REQUIRE(timeline->requestTrackInsertion(-1, audio, QString(), true));
    {
        const auto fps = pCore->getProjectFpsInfo();
        const int shot = int(2.0 * fps.first / fps.second);
        const auto source = KdenliveTests::createProducerWithSound(pCore->getProjectProfile(), bin, shot * 4);
        std::vector<int> ids;
        for (int i = 0; i < 3; ++i) {
            int id = -1;
            REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/5/%2").arg(source).arg(shot + 4), video, 20 + i * shot, id));
            ids.push_back(id);
        }
        const QString musicPath = temporary.filePath(QStringLiteral("music.wav"));
        writeMusicWave(musicPath, 8);
        auto producer = std::make_shared<Mlt::Producer>(pCore->getProjectProfile(), "avformat", musicPath.toUtf8().constData());
        REQUIRE(producer->is_valid());
        producer->set("kdenlive:montaz_role", "music");
        Fun undo = []() { return true; }, redo = []() { return true; };
        QString musicId;
        REQUIRE(bin->requestAddBinClip(musicId, producer, QStringLiteral("-1"), undo, redo));
        int musicClip = -1;
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("A%1/0/%2").arg(musicId).arg(shot * 3 - 1), audio, 20, musicClip));
        stack->clear();
        Montaz::EditorAdapter adapter;
        const auto context = adapter.snapshot(), timing = adapter.timingRequest();
        REQUIRE_FALSE(timing.isEmpty());
        auto operations = timing.value(QStringLiteral("clips")).toArray();
        for (int i = 0; i < 3; ++i) {
            auto op = operations[i].toObject();
            op[QStringLiteral("start_frame")] = 20 + i * shot + (i == 1 ? 3 : i == 2 ? -3 : 0);
            op[QStringLiteral("duration_frames")] = shot + (i == 1 ? -6 : 3);
            operations[i] = op;
        }
        QJsonObject proposal{{QStringLiteral("request_id"), QStringLiteral("native-timing")},
                             {QStringLiteral("project_id"), context.value(QStringLiteral("project_id"))},
                             {QStringLiteral("sequence_id"), context.value(QStringLiteral("sequence_id"))},
                             {QStringLiteral("base_revision"), context.value(QStringLiteral("revision"))},
                             {QStringLiteral("frame_rate"), context.value(QStringLiteral("frame_rate"))},
                             {QStringLiteral("protocol_version"), 2},
                             {QStringLiteral("music_sha256"), timing.value(QStringLiteral("music")).toObject().value(QStringLiteral("sha256"))},
                             {QStringLiteral("accents"), QJsonArray{shot + 3, 2 * shot - 3}},
                             {QStringLiteral("operations"), operations}};
        SECTION("Apply and one Undo/Redo preserve IDs, source starts, bin, audio and duration")
        {
            const int assets = bin->getAllClipIds().size();
            REQUIRE(adapter.applyTiming(context, timing, proposal).isEmpty());
            REQUIRE(stack->count() == 1);
            REQUIRE(bin->getAllClipIds().size() == assets);
            REQUIRE(timeline->getTrackClipsCount(video) == 3);
            REQUIRE(timeline->getClipPosition(musicClip) == 20);
            REQUIRE(timeline->getClipPlaytime(musicClip) == 3 * shot);
            REQUIRE(timeline->checkConsistency());
            for (int i = 0; i < 3; ++i) {
                REQUIRE(timeline->getClipPosition(ids[i]) == operations[i].toObject().value(QStringLiteral("start_frame")).toInt());
                REQUIRE(timeline->getClipPlaytime(ids[i]) == operations[i].toObject().value(QStringLiteral("duration_frames")).toInt());
                REQUIRE(timeline->getClipIn(ids[i]) == 5);
            }
            REQUIRE_FALSE(adapter.applyTiming(context, timing, proposal).isEmpty());
            stack->undo();
            for (int i = 0; i < 3; ++i) {
                REQUIRE(timeline->getClipPosition(ids[i]) == 20 + i * shot);
                REQUIRE(timeline->getClipPlaytime(ids[i]) == shot);
                REQUIRE(timeline->getClipIn(ids[i]) == 5);
            }
            REQUIRE(timeline->checkConsistency());
            stack->redo();
            REQUIRE(timeline->getClipPosition(ids[1]) == 20 + shot + 3);
            REQUIRE(timeline->getClipPlaytime(ids[1]) == shot - 6);
            REQUIRE(timeline->checkConsistency());
        }
        SECTION("A changed source start and changed music fail before mutation")
        {
            auto bad = proposal;
            auto badOperations = operations;
            auto first = badOperations[0].toObject();
            first[QStringLiteral("in_frame")] = 6;
            badOperations[0] = first;
            bad[QStringLiteral("operations")] = badOperations;
            REQUIRE_FALSE(adapter.applyTiming(context, timing, bad).isEmpty());
            QFile wave(musicPath);
            REQUIRE(wave.open(QIODevice::Append));
            wave.write("changed");
            wave.close();
            REQUIRE_FALSE(adapter.applyTiming(context, timing, proposal).isEmpty());
            REQUIRE(stack->count() == 0);
            REQUIRE(timeline->getClipPlaytime(ids[0]) == shot);
        }
        SECTION("Native partial resize is restored when the following batch step fails")
        {
            int calls = 0;
            const auto result = Montaz::applyInsertBatch(
                QJsonArray{QJsonObject{}, QJsonObject{}},
                [&](const QJsonObject &, Fun &batchUndo, Fun &batchRedo) {
                    if (++calls == 2) return false;
                    int duration = 1;
                    return timeline->requestItemResize(ids[0], duration, true, true, batchUndo, batchRedo);
                },
                [](const Fun &, const Fun &) { FAIL("A failed timing batch must not commit"); });
            REQUIRE(result == QStringLiteral("apply_failed_rolled_back"));
            REQUIRE(timeline->getClipPlaytime(ids[0]) == shot);
            REQUIRE(timeline->getClipIn(ids[0]) == 5);
            REQUIRE(stack->count() == 0);
            REQUIRE(timeline->checkConsistency());
        }
        SECTION("Lock changes and manual gaps invalidate the review")
        {
            timeline->setTrackLockedState(video, true);
            timeline->setTrackLockedState(video, false);
            REQUIRE_FALSE(adapter.applyTiming(context, timing, proposal).isEmpty());
            REQUIRE(timeline->requestItemResize(ids[0], shot - 1, true) == shot - 1);
            REQUIRE(adapter.timingRequest().isEmpty());
        }
        stack->clear();
    }
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
}

TEST_CASE("Assistant imports generated audio and places it with one native undo", "[Montaz]")
{
    const bool effect = GENERATE(false, true);
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto stack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(stack);
    document.setUrl(QUrl::fromLocalFile(temporary.filePath(QStringLiteral("travel.kdenlive"))));
    pCore->projectManager()->testSetDocument(&document);
    auto timeline = TimelineItemModel::construct(document.uuid(), stack);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int video = -1, audio = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, video));
    REQUIRE(timeline->requestTrackInsertion(-1, audio, QString(), true));
    {
        const auto fps = pCore->getProjectFpsInfo();
        const int frames = int(std::ceil(4.0 * fps.first / fps.second));
        const auto source = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", bin, frames + 10);
        int visual = -1;
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/0/%2").arg(source).arg(frames - 1), video, 20, visual));
        stack->clear();
        Montaz::EditorAdapter adapter;
        auto music = adapter.musicRequest(QStringLiteral("postcard"), QStringLiteral("Coastal travel"));
        REQUIRE_FALSE(music.isEmpty());
        REQUIRE(music.value(QStringLiteral("path")).toString().startsWith(temporary.filePath(QStringLiteral("travel.assets/music/"))));
        REQUIRE(music.value(QStringLiteral("duration_frames")) == frames);
        if (effect) {
            music.remove(QStringLiteral("recipe"));
            music[QStringLiteral("start_frame")] = 30;
            music[QStringLiteral("duration_frames")] = int(std::ceil(2.0 * fps.first / fps.second));
            REQUIRE(Montaz::validateEffectRequest(adapter.snapshot(), music).isEmpty());
        }
        const auto context = adapter.snapshot();
        // A local PCM WAV exercises actual MLT import, not a mock insertion callback.
        QFile wave(music.value(QStringLiteral("path")).toString());
        REQUIRE(wave.open(QIODevice::WriteOnly));
        QDataStream writer(&wave);
        writer.setByteOrder(QDataStream::LittleEndian);
        const quint32 samples = 48000 * 5, bytes = samples * 4;
        writer.writeRawData("RIFF", 4);
        writer << quint32(36 + bytes);
        writer.writeRawData("WAVEfmt ", 8);
        writer << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000) << quint16(4) << quint16(16);
        writer.writeRawData("data", 4);
        writer << bytes;
        for (quint32 i = 0; i < samples; ++i) {
            const auto value = qint16(4000 * std::sin(i * 2.0 * 3.141592653589793 * 440 / 48000));
            writer << value << value;
        }
        wave.close();
        REQUIRE(wave.open(QIODevice::ReadOnly));
        const auto hash = QCryptographicHash::hash(wave.readAll(), QCryptographicHash::Sha256).toHex();
        wave.close();
        QJsonObject soundtrack{{QStringLiteral("request_id"), QStringLiteral("music-native")},
                               {QStringLiteral("project_id"), context.value(QStringLiteral("project_id"))},
                               {QStringLiteral("sequence_id"), context.value(QStringLiteral("sequence_id"))},
                               {QStringLiteral("base_revision"), context.value(QStringLiteral("revision"))},
                               {QStringLiteral("frame_rate"), context.value(QStringLiteral("frame_rate"))},
                               {QStringLiteral("sha256"), QString::fromLatin1(hash)},
                               {QStringLiteral("accents"), effect ? QJsonArray{} : QJsonArray{30, 60}},
                               {QStringLiteral("cached"), false}};
        for (const auto &key : {QStringLiteral("path"), QStringLiteral("track_id"), QStringLiteral("start_frame"), QStringLiteral("duration_frames")})
            soundtrack[key] = music.value(key);
        const int before = bin->getAllClipIds().size();
        SECTION("Apply, undo and redo retain visual edit and generated file")
        {
            REQUIRE(adapter.applySoundtrack(context, music, soundtrack, effect).isEmpty());
            REQUIRE(stack->count() == 1);
            REQUIRE(bin->getAllClipIds().size() == before + 1);
            REQUIRE(timeline->getTrackClipsCount(audio) == 1);
            REQUIRE(timeline->getTrackClipsCount(video) == 1);
            const int position = music.value(QStringLiteral("start_frame")).toInt(), length = music.value(QStringLiteral("duration_frames")).toInt();
            const int id = timeline->getClipByPosition(audio, position);
            REQUIRE(id >= 0);
            REQUIRE(bin->getClipByBinID(timeline->getClipBinId(id))->getProducerProperty(QStringLiteral("kdenlive:montaz_role")) ==
                    (effect ? QStringLiteral("effect") : QStringLiteral("music")));
            REQUIRE(timeline->getClipPlaytime(id) == length);
            REQUIRE(timeline->getClipByPosition(audio, position + length - 1) == id);
            REQUIRE(timeline->checkConsistency());
            REQUIRE_FALSE(adapter.applySoundtrack(context, music, soundtrack, effect).isEmpty());
            stack->undo();
            REQUIRE(timeline->getTrackClipsCount(audio) == 0);
            REQUIRE(timeline->getTrackClipsCount(video) == 1);
            REQUIRE(bin->getAllClipIds().size() == before);
            REQUIRE(QFileInfo::exists(music.value(QStringLiteral("path")).toString()));
            REQUIRE_FALSE(adapter.applySoundtrack(context, music, soundtrack, effect).isEmpty());
            stack->redo();
            REQUIRE(timeline->getTrackClipsCount(audio) == 1);
            REQUIRE(bin->getAllClipIds().size() == before + 1);
            REQUIRE(timeline->checkConsistency());
        }
        SECTION("Changed file is rejected before bin or timeline mutation")
        {
            REQUIRE(wave.open(QIODevice::WriteOnly | QIODevice::Append));
            wave.write("changed");
            wave.close();
            REQUIRE_FALSE(adapter.applySoundtrack(context, music, soundtrack, effect).isEmpty());
            REQUIRE(bin->getAllClipIds().size() == before);
            REQUIRE(timeline->getTrackClipsCount(audio) == 0);
            REQUIRE(stack->count() == 0);
        }
        SECTION("Locking and unlocking audio invalidates the pending review")
        {
            timeline->setTrackLockedState(audio, true);
            timeline->setTrackLockedState(audio, false);
            REQUIRE_FALSE(adapter.applySoundtrack(context, music, soundtrack, effect).isEmpty());
            REQUIRE(bin->getAllClipIds().size() == before);
            REQUIRE(timeline->getTrackClipsCount(audio) == 0);
        }
        // Undo commands retain clip references; destroy them while the document lives.
        stack->clear();
    }
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
}
