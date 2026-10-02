// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "assistant/editbatch.h"
#include "assistant/editoradapter.h"
#include "assistant/proposal.h"
#include "bin/model/subtitlemodel.hpp"
#include "bin/projectclip.h"
#include "doc/kdenlivedoc.h"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "test_utils.hpp"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
#include <mlt++/MltConsumer.h>

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
    const bool rhythmic = GENERATE(false, true);
    QTemporaryDir temporary;
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto stack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(stack);
    document.setUrl(QUrl::fromLocalFile(temporary.filePath(QStringLiteral("timing.kdenlive"))));
    pCore->projectManager()->testSetDocument(&document);
    auto timeline = TimelineItemModel::construct(document.uuid(), stack);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int video = -1, audio = -1, spareAudio = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, video));
    REQUIRE(timeline->requestTrackInsertion(-1, audio, QString(), true));
    REQUIRE(timeline->requestTrackInsertion(-1, spareAudio, QString(), true));
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
        if (rhythmic) {
            proposal[QStringLiteral("protocol_version")] = 4;
            proposal.remove(QStringLiteral("accents"));
            proposal[QStringLiteral("anchors")] = QJsonArray{shot / 2, shot + 3, shot * 3 / 2, 2 * shot - 3, shot * 5 / 2};
            proposal[QStringLiteral("analysis")] = QJsonObject{{QStringLiteral("version"), QStringLiteral("periodic-onsets-1")},
                                                               {QStringLiteral("mode"), QStringLiteral("beats")},
                                                               {QStringLiteral("bpm"), 120},
                                                               {QStringLiteral("periodicity"), 1},
                                                               {QStringLiteral("support"), 1},
                                                               {QStringLiteral("reason"), QStringLiteral("none")},
                                                               {QStringLiteral("cached"), false}};
        }
        SECTION("Cue capture is read-only and rejects missing source evidence before generation")
        {
            const auto spec = adapter.cueRequest();
            REQUIRE(spec.value(QStringLiteral("clips")) == timing.value(QStringLiteral("clips")));
            auto cue = spec.value(QStringLiteral("clips")).toArray()[0].toObject();
            cue.remove(QStringLiteral("bin_id"));
            cue.remove(QStringLiteral("track_id"));
            cue.remove(QStringLiteral("in_frame"));
            cue[QStringLiteral("category")] = QStringLiteral("waves");
            cue[QStringLiteral("duration_frames")] = int(fps.first / fps.second);
            cue[QStringLiteral("instruction")] = QStringLiteral("Gentle water");
            cue[QStringLiteral("reason")] = QStringLiteral("Visible water");
            cue[QStringLiteral("source_sha256")] = QString(64, 'a');
            QJsonObject review{{QStringLiteral("request_id"), QStringLiteral("native-cues")},
                               {QStringLiteral("project_id"), context.value(QStringLiteral("project_id"))},
                               {QStringLiteral("sequence_id"), context.value(QStringLiteral("sequence_id"))},
                               {QStringLiteral("base_revision"), context.value(QStringLiteral("revision"))},
                               {QStringLiteral("frame_rate"), context.value(QStringLiteral("frame_rate"))},
                               {QStringLiteral("cached_assets"), 1},
                               {QStringLiteral("unavailable_assets"), 0},
                               {QStringLiteral("cues"), QJsonArray{cue}}};
            REQUIRE(Montaz::validateSoundCues(context, spec, review).isEmpty());
            // Synthetic MLT producer has no source file matching cached visual evidence.
            REQUIRE(adapter.effectFromCue(context, spec, review, 0).isEmpty());
            REQUIRE(adapter.effectFromCue(context, spec, review, -1).isEmpty());
            REQUIRE(adapter.snapshot() == context);
            REQUIRE(stack->count() == 0);
            REQUIRE(timeline->getClipsCount() == 4);
        }
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
        SECTION("Temporary selection permits timing Apply while permanent groups are rejected")
        {
            REQUIRE(timeline->requestSetSelection({ids[0], ids[1]}));
            REQUIRE(adapter.timingRequest() == timing);
            const auto selected = adapter.snapshot();
            proposal[QStringLiteral("base_revision")] = selected.value(QStringLiteral("revision"));
            REQUIRE(adapter.applyTiming(selected, timing, proposal).isEmpty());
            REQUIRE(timeline->getClipPosition(ids[1]) == 20 + shot + 3);
            REQUIRE(timeline->getClipPosition(ids[2]) == 20 + 2 * shot - 3);
            REQUIRE(stack->count() == 1);
            stack->undo();
            REQUIRE(timeline->getClipPosition(ids[1]) == 20 + shot);
            REQUIRE(timeline->checkConsistency());
            timeline->requestClearSelection();
            REQUIRE(timeline->requestClipsGroup({ids[0], ids[1]}) >= 0);
            REQUIRE(adapter.timingRequest().isEmpty());
        }
        SECTION("Muted music and hidden video invalidate timing eligibility")
        {
            timeline->setTrackProperty(audio, QStringLiteral("hide"), QStringLiteral("3"));
            REQUIRE(adapter.timingRequest().isEmpty());
            REQUIRE_FALSE(adapter.applyTiming(context, timing, proposal).isEmpty());
            timeline->setTrackProperty(audio, QStringLiteral("hide"), QStringLiteral("1"));
            REQUIRE_FALSE(adapter.timingRequest().isEmpty());
            timeline->setTrackProperty(video, QStringLiteral("hide"), QStringLiteral("1"));
            REQUIRE(adapter.timingRequest().isEmpty());
            REQUIRE(stack->count() == 0);
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

TEST_CASE("Assistant captions and style switches preserve native subtitle identity", "[Montaz]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto stack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(stack);
    document.setUrl(QUrl::fromLocalFile(temporary.filePath(QStringLiteral("captions.kdenlive"))));
    document.setDocumentProperty(QStringLiteral("documentid"), QStringLiteral("montaz-caption-test-") + document.uuid().toString(QUuid::WithoutBraces));
    pCore->projectManager()->testSetDocument(&document);
    auto timeline = TimelineItemModel::construct(document.uuid(), stack);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int audio = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, audio, QString(), true));
    {
        const auto path = temporary.filePath(QStringLiteral("speech.wav"));
        writeMusicWave(path, 6);
        auto producer = std::make_shared<Mlt::Producer>(pCore->getProjectProfile(), "avformat", path.toUtf8().constData());
        REQUIRE(producer->is_valid());
        QString binId;
        Fun undo = []() { return true; }, redo = []() { return true; };
        REQUIRE(bin->requestAddBinClip(binId, producer, QStringLiteral("-1"), undo, redo));
        int id = -1;
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("A%1/10/99").arg(binId), audio, 100, id));
        stack->clear();
        Montaz::EditorAdapter adapter;
        const auto spec = adapter.captionRequest();
        REQUIRE_FALSE(spec.isEmpty());
        REQUIRE(spec.value(QStringLiteral("clips")).toArray().size() == 1);
        REQUIRE(spec.value(QStringLiteral("clips")).toArray()[0].toObject().value(QStringLiteral("audio_stream")) == 0);
        const auto context = adapter.snapshot();
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        const auto hash = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
        file.close();
        QJsonArray captions;
        for (int i = 0; i < 2; ++i)
            captions.append(QJsonObject{{QStringLiteral("instance_id"), QString::number(id)},
                                        {QStringLiteral("start_frame"), 100 + 30 * i},
                                        {QStringLiteral("duration_frames"), 25},
                                        {QStringLiteral("text"), QStringLiteral("Spoken words %1").arg(i)}});
        const QJsonObject review{{QStringLiteral("request_id"), QStringLiteral("native-captions")},
                                 {QStringLiteral("project_id"), context.value(QStringLiteral("project_id"))},
                                 {QStringLiteral("sequence_id"), context.value(QStringLiteral("sequence_id"))},
                                 {QStringLiteral("base_revision"), context.value(QStringLiteral("revision"))},
                                 {QStringLiteral("frame_rate"), context.value(QStringLiteral("frame_rate"))},
                                 {QStringLiteral("checked_clips"), 1},
                                 {QStringLiteral("cached_clips"), 0},
                                 {QStringLiteral("skipped_clips"), 0},
                                 {QStringLiteral("source_sha256"), QJsonObject{{binId, hash}}},
                                 {QStringLiteral("captions"), captions}};
        SECTION("Apply one undo, replay rejection, repeated styles retain IDs and timing")
        {
            REQUIRE(adapter.applyCaptions(context, spec, review).isEmpty());
            REQUIRE(stack->count() == 1);
            auto model = timeline->getSubtitleModel();
            REQUIRE(model);
            REQUIRE(model->rowCount() == 2);
            const auto originals = model->getAllSubtitles();
            const auto firstId = model->getSubtitleIdFromIndex(0);
            REQUIRE_FALSE(adapter.applyCaptions(context, spec, review).isEmpty());
            stack->undo();
            REQUIRE(model->rowCount() == 0);
            REQUIRE(timeline->checkConsistency());
            stack->redo();
            REQUIRE(model->rowCount() == 2);
            for (const auto &preset : {QStringLiteral("clean"), QStringLiteral("bold"), QStringLiteral("clean"), QStringLiteral("original")}) {
                const auto before = model->getForceStyle();
                const int count = stack->count();
                REQUIRE(adapter.captionStyle(preset).isEmpty());
                const auto after = model->getForceStyle();
                REQUIRE(stack->count() == count + 1);
                REQUIRE(model->getSubtitleIdFromIndex(0) == firstId);
                const auto current = model->getAllSubtitles();
                REQUIRE(current.size() == originals.size());
                for (int i = 0; i < current.size(); ++i) {
                    REQUIRE(current[i].first == originals[i].first);
                    REQUIRE(current[i].second.endTime() == originals[i].second.endTime());
                    REQUIRE(current[i].second.text() == originals[i].second.text());
                }
                REQUIRE(adapter.captionStyle(preset).isEmpty());
                REQUIRE(stack->count() == count + 1);
                stack->undo();
                REQUIRE(model->getForceStyle() == before);
                stack->redo();
                REQUIRE(model->getForceStyle() == after);
            }
            REQUIRE(model->getForceStyle().isEmpty());
            REQUIRE(timeline->getClipPosition(id) == 100);
            REQUIRE(timeline->getClipIn(id) == 10);
            REQUIRE(timeline->getClipPlaytime(id) == 90);
            // Upstream checkConsistency counts clip/composition/guide snaps only, not subtitles.
        }
        SECTION("Source changes reject before creating native subtitles")
        {
            REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Append));
            file.write("changed");
            file.close();
            REQUIRE(adapter.applyCaptions(context, spec, review) == QStringLiteral("media_changed"));
            REQUIRE_FALSE(timeline->getSubtitleModel());
            REQUIRE(stack->count() == 0);
        }
        SECTION("Muted audio is skipped and mute changes invalidate review")
        {
            timeline->setTrackProperty(audio, QStringLiteral("hide"), QStringLiteral("3"));
            REQUIRE(adapter.captionRequest().value(QStringLiteral("clips")).toArray().isEmpty());
            REQUIRE_FALSE(adapter.applyCaptions(context, spec, review).isEmpty());
        }
        SECTION("Native insertion failure does not accumulate a false undo command")
        {
            const auto model = timeline->createSubtitleModel();
            Fun localUndo = []() { return true; }, localRedo = []() { return true; };
            const double fps = pCore->getCurrentFps();
            const SubtitleEvent event(true, GenTime(25, fps), model->getLayerDefaultStyle(0), QString(), 0, 0, 0, QString(), QStringLiteral("Words"));
            REQUIRE(model->addSubtitle({0, GenTime(0, fps)}, event, localUndo, localRedo));
            REQUIRE_FALSE(model->addSubtitle({0, GenTime(0, fps)}, event, localUndo, localRedo));
            REQUIRE(model->rowCount() == 1);
            REQUIRE(localUndo());
            REQUIRE(model->rowCount() == 0);
            REQUIRE(localRedo());
            REQUIRE(model->rowCount() == 1);
        }
        stack->clear();
    }
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
}

TEST_CASE("Headless master-effect loading restores subtitles without a main window", "[Montaz]")
{
    REQUIRE(pCore->window() == nullptr);
    QTemporaryDir temporary;
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto stack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(stack);
    document.setUrl(QUrl::fromLocalFile(temporary.filePath(QStringLiteral("headless.kdenlive"))));
    document.setDocumentProperty(QStringLiteral("documentid"), document.uuid().toString(QUuid::WithoutBraces));
    pCore->projectManager()->testSetDocument(&document);
    auto timeline = TimelineItemModel::construct(document.uuid(), stack);
    document.addTimeline(timeline->uuid(), timeline);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    {
        auto service = std::make_shared<Mlt::Tractor>(pCore->getProjectProfile());
        Mlt::Filter filter(pCore->getProjectProfile(), "avfilter.subtitles");
        REQUIRE(filter.is_valid());
        filter.set("internal_added", 237);
        filter.set("av.force_style", "FontSize=32,Bold=-1");
        filter.set("kdenlive:locked", 1);
        filter.set("disable", 1);
        service->attach(filter);
        timeline->importMasterEffects(std::static_pointer_cast<Mlt::Service>(service));
        auto model = timeline->getSubtitleModel();
        REQUIRE(model);
        REQUIRE(model->getForceStyle() == QStringLiteral("FontSize=32,Bold=-1"));
        REQUIRE(model->isLocked());
        REQUIRE(model->isDisabled());
        REQUIRE(stack->count() == 0);
    }
    stack->clear();
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
}

TEST_CASE("Object match cuts add native transforms atomically", "[Montaz]")
{
    const auto oldProfile = pCore->getCurrentProfilePath();
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto stack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(stack);
    pCore->projectManager()->testSetDocument(&document);
    REQUIRE(pCore->setCurrentProfile(QStringLiteral("atsc_1080p_25")));
    auto timeline = TimelineItemModel::construct(document.uuid(), stack);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int video = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, video));
    int ids[2] = {-1, -1};
    for (int i = 0; i < 2; ++i) {
        const auto bid = KdenliveTests::createAVProducer(pCore->getProjectProfile(), bin);
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/2/11").arg(bid), video, i * 10, ids[i], true, false, false));
    }
    REQUIRE(timeline->requestSetSelection({ids[0], ids[1]}));
    stack->clear();
    {
        Montaz::EditorAdapter adapter;
        const auto context = adapter.snapshot(), spec = adapter.matchRequest();
        REQUIRE_FALSE(spec.isEmpty());
        QJsonArray operations;
        QJsonObject hashes;
        const auto fps = context.value("frame_rate").toObject();
        const int length = qMin(10, qMax(2, int(std::floor(fps.value("numerator").toDouble() / fps.value("denominator").toDouble() + 0.5))));
        const auto clips = spec.value("clips").toArray();
        for (int i = 0; i < 2; ++i) {
            const auto c = clips[i].toObject();
            QFile file(bin->getClipByBinID(c.value("bin_id").toString())->clipUrl());
            REQUIRE(file.open(QIODevice::ReadOnly));
            hashes[c.value("bin_id").toString()] = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
            QJsonArray frames;
            for (int j = 0; j < length; ++j)
                frames.append(QJsonObject{{"frame", i == 0 ? 10 - length + j : j}, {"x", -0.025}, {"y", -0.025}, {"scale", 1.05}});
            operations.append(QJsonObject{{"instance_id", QString::number(ids[i])}, {"keyframes", frames}});
        }
        QJsonObject review{{"request_id", "match-test"},
                           {"project_id", context.value("project_id")},
                           {"sequence_id", context.value("sequence_id")},
                           {"base_revision", context.value("revision")},
                           {"frame_rate", context.value("frame_rate")},
                           {"protocol_version", 3},
                           {"status", "matched"},
                           {"reason", "Controlled alignment"},
                           {"cached", false},
                           {"source_hashes", hashes},
                           {"operations", operations},
                           {"target", QJsonObject{{"x", 0.5}, {"y", 0.5}, {"height", 0.2}}},
                           {"preview", QJsonObject{{"outgoing", "/9j/2Q=="}, {"incoming", "/9j/2Q=="}}}};
        REQUIRE(Montaz::validateMatchReview(context, spec, review).isEmpty());
        SECTION("Apply keeps clip timing and source trims; one Undo and Redo preserve keyframes")
        {
            REQUIRE(adapter.applyMatch(context, spec, review).isEmpty());
            REQUIRE(stack->count() == 1);
            QString stored;
            for (int i = 0; i < 2; ++i) {
                auto effects = timeline->getClipEffectStackModel(ids[i]);
                REQUIRE(effects->rowCount() == 1);
                const auto model = effects->getAssetModelById(QStringLiteral("qtblend"));
                REQUIRE(model);
                const auto data = model->getAllParameters();
                auto found = std::find_if(data.begin(), data.end(), [](const auto &v) { return v.first == QStringLiteral("rect"); });
                REQUIRE(found != data.end());
                const auto positions = [](const QString &animation) {
                    QStringList result;
                    for (const auto &key : animation.split(QLatin1Char(';')))
                        result.append(key.section(QLatin1Char('='), 0, 0));
                    return result;
                };
                for (const auto &parameter : data) {
                    if (parameter.first == QStringLiteral("rotation") || parameter.first == QStringLiteral("rotate_anchor")) {
                        REQUIRE(positions(parameter.second.toString()) == positions(found->second.toString()));
                    }
                }
                // Loading the editable keyframe model must not repair missing sibling keys.
                model->prepareKeyframes();
                REQUIRE(model->getAllParameters() == data);
                if (i == 0) {
                    stored = found->second.toString();
                    REQUIRE(stored.startsWith(QString::number(2 + 10 - length) + QLatin1Char('=')));
                }
                REQUIRE(timeline->getClipPosition(ids[i]) == i * 10);
                REQUIRE(timeline->getClipIn(ids[i]) == 2);
                REQUIRE(timeline->getClipPlaytime(ids[i]) == 10);
            }
            REQUIRE_FALSE(adapter.applyMatch(context, spec, review).isEmpty());
            stack->undo();
            for (int id : ids)
                REQUIRE(timeline->getClipEffectStackModel(id)->rowCount() == 0);
            stack->redo();
            for (int id : ids)
                REQUIRE(timeline->getClipEffectStackModel(id)->rowCount() == 1);
            const auto params = timeline->getClipEffectStackModel(ids[0])->getAssetModelById(QStringLiteral("qtblend"))->getAllParameters();
            const auto found = std::find_if(params.begin(), params.end(), [](const auto &v) { return v.first == QStringLiteral("rect"); });
            REQUIRE(found->second.toString() == stored);
            REQUIRE(timeline->checkConsistency());
        }
        SECTION("A stale revision or selection never changes effects")
        {
            REQUIRE(timeline->requestSetSelection({ids[0]}));
            REQUIRE_FALSE(adapter.applyMatch(context, spec, review).isEmpty());
            REQUIRE(timeline->requestSetSelection({ids[0], ids[1]}));
            auto stale = review;
            stale["base_revision"] = context.value("revision").toInt() + 1;
            REQUIRE_FALSE(adapter.applyMatch(context, spec, stale).isEmpty());
            REQUIRE(stack->count() == 0);
        }
        SECTION("Invalid geometry and source identity reject the whole batch")
        {
            auto bad = review;
            auto ops = operations;
            auto op = ops[1].toObject();
            auto frames = op["keyframes"].toArray();
            auto k = frames[0].toObject();
            k["scale"] = 3;
            frames[0] = k;
            op["keyframes"] = frames;
            ops[1] = op;
            bad["operations"] = ops;
            REQUIRE_FALSE(adapter.applyMatch(context, spec, bad).isEmpty());
            bad = review;
            auto changed = hashes;
            changed[changed.begin().key()] = QString(64, 'a');
            bad["source_hashes"] = changed;
            REQUIRE_FALSE(adapter.applyMatch(context, spec, bad).isEmpty());
            for (int id : ids)
                REQUIRE(timeline->getClipEffectStackModel(id)->rowCount() == 0);
            REQUIRE(stack->count() == 0);
        }
        SECTION("Real clip groups stay ineligible while temporary selection is supported")
        {
            timeline->requestClearSelection();
            REQUIRE(timeline->requestClipsGroup({ids[0], ids[1]}) >= 0);
            REQUIRE(timeline->requestSetSelection({ids[0], ids[1]}));
            REQUIRE(adapter.matchRequest().isEmpty());
            REQUIRE_FALSE(adapter.applyMatch(context, spec, review).isEmpty());
            for (int id : ids)
                REQUIRE(timeline->getClipEffectStackModel(id)->rowCount() == 0);
        }
        SECTION("A failed second native effect rolls the first effect back")
        {
            int step = 0;
            const auto result = Montaz::applyInsertBatch(
                operations,
                [&](const QJsonObject &op, Montaz::UndoFunction &undo, Montaz::UndoFunction &redo) {
                    if (++step == 2) return false;
                    return timeline->getClipEffectStackModel(op.value("instance_id").toString().toInt())
                        ->appendEffectWithUndo(QStringLiteral("qtblend"), undo, redo)
                        .first;
                },
                [&](const auto &, const auto &) { FAIL("Failed batch cannot commit"); });
            REQUIRE_FALSE(result.isEmpty());
            for (int id : ids)
                REQUIRE(timeline->getClipEffectStackModel(id)->rowCount() == 0);
            REQUIRE(stack->count() == 0);
        }
    }
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
    pCore->setCurrentProfile(oldProfile);
}

TEST_CASE("Native object alignment survives serialization and export", "[Montaz]")
{
    const auto root = qEnvironmentVariable("MONTAZ_MATCH_QA_ROOT");
    if (root.isEmpty()) {
        SUCCEED("Set MONTAZ_MATCH_QA_ROOT for generated moving-object render checks");
        return;
    }
    const auto oldProfile = pCore->getCurrentProfilePath();
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto undo = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undo);
    pCore->projectManager()->testSetDocument(&document);
    REQUIRE(pCore->setCurrentProfile(QStringLiteral("atsc_1080p_2997")));
    auto timeline = TimelineItemModel::construct(document.uuid(), undo);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int track = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, track));
    QList<int> ids;
    QStringList bids;
    for (int i = 0; i < 2; ++i) {
        auto producer = std::make_shared<Mlt::Producer>(
            pCore->getProjectProfile(),
            QDir(root).filePath(i == 0 ? QStringLiteral("round-object-a.mp4") : QStringLiteral("round-object-b.mp4")).toUtf8().constData());
        REQUIRE(producer->is_valid());
        auto bid = QString::number(bin->getFreeClipId());
        Fun u = []() { return true; }, r = []() { return true; };
        REQUIRE(bin->requestAddBinClip(bid, producer, QStringLiteral("-1"), u, r));
        int id = -1;
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/0/59").arg(bid), track, i * 60, id, true, false, false));
        ids << id;
        bids << bid;
    }
    REQUIRE(timeline->requestSetSelection({ids[0], ids[1]}));
    undo->clear();
    {
        Montaz::EditorAdapter adapter;
        const auto context = adapter.snapshot(), spec = adapter.matchRequest();
        REQUIRE_FALSE(spec.isEmpty());
        QFile input(QDir(root).filePath(QStringLiteral("controlled.json")));
        REQUIRE(input.open(QIODevice::ReadOnly));
        auto review = QJsonDocument::fromJson(input.readAll()).object();
        REQUIRE(review.value("status") == "matched");
        review["project_id"] = context.value("project_id");
        review["sequence_id"] = context.value("sequence_id");
        review["base_revision"] = context.value("revision");
        auto operations = review.value("operations").toArray();
        auto hashes = review.value("source_hashes").toObject();
        QJsonObject rebound;
        for (int i = 0; i < 2; ++i) {
            auto op = operations[i].toObject();
            op["instance_id"] = QString::number(ids[i]);
            operations[i] = op;
            rebound[bids[i]] = hashes.value(QString::number(i + 1));
        }
        review["operations"] = operations;
        review["source_hashes"] = rebound;
        REQUIRE(adapter.applyMatch(context, spec, review).isEmpty());
        REQUIRE(undo->count() == 1);
        auto geometry = [](Mlt::Producer &producer, int position) {
            producer.seek(position);
            std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
            mlt_image_format format = mlt_image_rgb;
            int w = 640, h = 360;
            auto pixels = frame->get_image(format, w, h);
            REQUIRE(pixels);
            REQUIRE(format == mlt_image_rgb);
            int left = w, right = -1, top = h, bottom = -1, count = 0;
            double sx = 0, sy = 0;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const auto p = pixels + 3 * (y * w + x);
                    if (p[0] > 65 && p[1] > 65 && p[2] < 50) {
                        left = qMin(left, x);
                        right = qMax(right, x);
                        top = qMin(top, y);
                        bottom = qMax(bottom, y);
                        sx += x;
                        sy += y;
                        ++count;
                    }
                }
            REQUIRE(count > 100);
            return QList<double>{sx / count / w, sy / count / h, double(bottom - top + 1) / h, double(right - left + 1) / w};
        };
        const auto outgoing = geometry(*timeline->tractor(), 59), incoming = geometry(*timeline->tractor(), 60);
        REQUIRE(std::abs(outgoing[0] - incoming[0]) < 0.007);
        REQUIRE(std::abs(outgoing[1] - incoming[1]) < 0.007);
        REQUIRE(std::abs(outgoing[2] - incoming[2]) < 0.015);
        const auto scene = QDir(root).filePath(QStringLiteral("native-alignment.mlt"));
        {
            Mlt::Consumer serializer(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
            REQUIRE(serializer.is_valid());
            serializer.connect(*timeline->tractor());
            serializer.run();
        }
        Mlt::Producer reopened(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
        REQUIRE(reopened.is_valid());
        const auto restored = geometry(reopened, 60);
        for (int i = 0; i < 4; ++i)
            REQUIRE(std::abs(restored[i] - incoming[i]) < 0.003);
        undo->undo();
        const auto original = geometry(*timeline->tractor(), 59);
        REQUIRE(std::abs(original[0] - outgoing[0]) > 0.01);
        undo->redo();
        const auto redone = geometry(*timeline->tractor(), 59);
        for (int i = 0; i < 4; ++i)
            REQUIRE(std::abs(redone[i] - outgoing[i]) < 0.003);
        const auto output = QDir(root).filePath(QStringLiteral("native-alignment.mp4"));
        {
            Mlt::Consumer renderer(pCore->getProjectProfile(), "avformat", output.toUtf8().constData());
            REQUIRE(renderer.is_valid());
            renderer.set("vcodec", "libx264");
            renderer.set("crf", 18);
            renderer.set("preset", "fast");
            renderer.set("audio_off", 1);
            renderer.set("real_time", -1);
            renderer.set("in", 0);
            renderer.set("out", 119);
            renderer.set("terminate_on_pause", 1);
            std::unique_ptr<Mlt::Producer> zone(timeline->tractor()->cut(0, 119));
            renderer.connect(*zone);
            renderer.run();
        }
        REQUIRE(QFileInfo(output).size() > 1000);
        Mlt::Producer exported(pCore->getProjectProfile(), output.toUtf8().constData());
        REQUIRE(exported.is_valid());
        REQUIRE(exported.get_length() == 120);
        const auto decoded = geometry(exported, 60);
        for (int i = 0; i < 3; ++i)
            REQUIRE(std::abs(decoded[i] - incoming[i]) < 0.007);
    }
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
    pCore->setCurrentProfile(oldProfile);
}

TEST_CASE("Estimated beat timing survives native undo serialization and export", "[Montaz]")
{
    const auto root = qEnvironmentVariable("MONTAZ_BEAT_QA_ROOT");
    if (root.isEmpty()) {
        SUCCEED("Set MONTAZ_BEAT_QA_ROOT for controlled rhythm render checks");
        return;
    }
    const auto oldProfile = pCore->getCurrentProfilePath();
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto undo = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undo);
    pCore->projectManager()->testSetDocument(&document);
    REQUIRE(pCore->setCurrentProfile(QStringLiteral("atsc_720p_25")));
    auto timeline = TimelineItemModel::construct(document.uuid(), undo);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int video = -1, audio = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, video));
    REQUIRE(timeline->requestTrackInsertion(-1, audio, QString(), true));
    QList<int> ids;
    const QList<int> starts{0, 48, 102}, lengths{48, 54, 48};
    for (int i = 0; i < 3; ++i) {
        auto source = std::make_shared<Mlt::Producer>(pCore->getProjectProfile(), "avformat",
                                                      QDir(root).filePath(QStringLiteral("shot-%1.mp4").arg(i)).toUtf8().constData());
        REQUIRE(source->is_valid());
        QString bid;
        Fun addUndo = []() { return true; }, addRedo = []() { return true; };
        REQUIRE(bin->requestAddBinClip(bid, source, QStringLiteral("-1"), addUndo, addRedo));
        int id = -1;
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/0/%2").arg(bid).arg(lengths[i] - 1), video, starts[i], id, true, false, false));
        ids << id;
    }
    const auto musicPath = QDir(root).filePath(QStringLiteral("rhythm.wav"));
    auto producer = std::make_shared<Mlt::Producer>(pCore->getProjectProfile(), "avformat", musicPath.toUtf8().constData());
    REQUIRE(producer->is_valid());
    producer->set("kdenlive:montaz_role", "music");
    QString musicId;
    {
        Fun u = []() { return true; }, r = []() { return true; };
        REQUIRE(bin->requestAddBinClip(musicId, producer, QStringLiteral("-1"), u, r));
    }
    int musicClip = -1;
    REQUIRE(timeline->requestClipInsertion(QStringLiteral("A%1/0/149").arg(musicId), audio, 0, musicClip, true, false, false));
    REQUIRE(timeline->requestSetSelection({ids[0], ids[1]}));
    undo->clear();
    {
        auto render = [&root](Mlt::Producer &source, const QString &name) {
            const auto path = QDir(root).filePath(name);
            Mlt::Consumer consumer(pCore->getProjectProfile(), "avformat", path.toUtf8().constData());
            REQUIRE(consumer.is_valid());
            consumer.set("f", "matroska");
            consumer.set("vcodec", "ffv1");
            consumer.set("acodec", "pcm_s16le");
            consumer.set("ar", 8000);
            consumer.set("channels", 2);
            consumer.set("real_time", -1);
            consumer.set("terminate_on_pause", 1);
            std::unique_ptr<Mlt::Producer> zone(source.cut(0, 149));
            consumer.connect(*zone);
            consumer.run();
            REQUIRE(QFileInfo(path).size() > 1000);
        };
        render(*timeline->tractor(), QStringLiteral("before.mkv"));
        Montaz::EditorAdapter adapter;
        const auto context = adapter.snapshot(), spec = adapter.timingRequest();
        REQUIRE_FALSE(spec.isEmpty());
        QFile input(QDir(root).filePath(QStringLiteral("timing.json")));
        REQUIRE(input.open(QIODevice::ReadOnly));
        auto proposal = QJsonDocument::fromJson(input.readAll()).object();
        REQUIRE(proposal.value(QStringLiteral("frame_rate")) == context.value(QStringLiteral("frame_rate")));
        REQUIRE(proposal.value(QStringLiteral("analysis")).toObject().value(QStringLiteral("mode")) == QStringLiteral("beats"));
        proposal[QStringLiteral("project_id")] = context.value(QStringLiteral("project_id"));
        proposal[QStringLiteral("sequence_id")] = context.value(QStringLiteral("sequence_id"));
        proposal[QStringLiteral("base_revision")] = context.value(QStringLiteral("revision"));
        auto operations = proposal.value(QStringLiteral("operations")).toArray();
        REQUIRE(operations.size() == 3);
        const auto nativeClips = spec.value(QStringLiteral("clips")).toArray();
        for (int i = 0; i < 3; ++i) {
            auto op = operations[i].toObject();
            for (const auto &key : {QStringLiteral("instance_id"), QStringLiteral("bin_id"), QStringLiteral("track_id")})
                op[key] = nativeClips[i].toObject().value(key);
            operations[i] = op;
        }
        proposal[QStringLiteral("operations")] = operations;
        REQUIRE(adapter.applyTiming(context, spec, proposal).isEmpty());
        REQUIRE(undo->count() == 1);
        REQUIRE(timeline->getClipPosition(ids[1]) == 50);
        REQUIRE(timeline->getClipPosition(ids[2]) == 100);
        REQUIRE(timeline->getClipPlaytime(musicClip) == 150);
        REQUIRE(timeline->getClipPosition(musicClip) == 0);
        REQUIRE(timeline->getClipIn(musicClip) == 0);
        REQUIRE(timeline->duration() == 150);
        REQUIRE(timeline->checkConsistency());
        undo->undo();
        REQUIRE(timeline->getClipPosition(ids[1]) == 48);
        REQUIRE(timeline->getClipPosition(ids[2]) == 102);
        undo->redo();
        REQUIRE(timeline->getClipPosition(ids[1]) == 50);
        REQUIRE(timeline->getClipPosition(ids[2]) == 100);
        const auto scene = QDir(root).filePath(QStringLiteral("timing.mlt"));
        {
            Mlt::Consumer serializer(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
            REQUIRE(serializer.is_valid());
            serializer.connect(*timeline->tractor());
            serializer.run();
        }
        Mlt::Producer reopened(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
        REQUIRE(reopened.is_valid());
        // Kdenlive's black background includes seek padding beyond the content.
        // Export the exact reviewed zone, as the native render path does.
        render(reopened, QStringLiteral("aligned.mkv"));
    }
    undo->clear();
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
    pCore->setCurrentProfile(oldProfile);
}
