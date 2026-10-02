// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "assistant/assistantwidget.h"
#include "assistant/chatview.h"
#include "assistant/editbatch.h"
#include "assistant/editoradapter.h"
#include "assistant/montage.h"
#include "assistant/proposal.h"
#include "assistant/workerclient.h"
#include "bin/model/subtitlemodel.hpp"
#include "bin/projectclip.h"
#include "doc/kdenlivedoc.h"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "test_utils.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
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

TEST_CASE("Assistant keeps optional tools together and sound requests out of the editing brief", "[Montaz][AssistantUI]")
{
    Montaz::AssistantWidget widget;
    const auto transitions = widget.findChild<QPushButton *>(QStringLiteral("assistantTransitions"));
    REQUIRE(transitions);
    REQUIRE(transitions->menu());
    const auto menu = transitions->menu()->actions();
    REQUIRE(menu.size() == 6);
    REQUIRE(menu[0]->text() == QStringLiteral("Beat-synced cuts"));
    REQUIRE(menu[1]->text() == QStringLiteral("Match cut"));
    REQUIRE(menu[0]->isEnabled());
    REQUIRE(menu[1]->isEnabled());
    for (int i = 3; i < 6; ++i)
        REQUIRE_FALSE(menu[i]->isEnabled());
    widget.resize(500, 700);
    widget.show();
    QApplication::processEvents();
    bool menuOpened = false;
    QTimer::singleShot(0, transitions, [&]() {
        menuOpened = transitions->menu()->isVisible();
        transitions->menu()->close();
    });
    transitions->click();
    QApplication::processEvents();
    REQUIRE(menuOpened);
    REQUIRE(widget.findChild<QPushButton *>(QStringLiteral("assistantAddMusic")));
    for (const auto button : widget.findChildren<QPushButton *>())
        REQUIRE_FALSE(QStringList{"Add sound", "Align cuts", "Match cut"}.contains(button->text()));
    for (const auto label : widget.findChildren<QLabel *>())
        REQUIRE_FALSE(label->text().contains(QStringLiteral("Story → Music")));
    const auto chat = widget.findChild<Montaz::ChatView *>();
    const auto worker = widget.findChild<Montaz::WorkerClient *>();
    const auto composer = widget.findChild<QPlainTextEdit *>(QStringLiteral("assistantComposer"));
    REQUIRE(chat);
    REQUIRE(worker);
    REQUIRE(composer);
    // No document/media: these requests must settle before configuring a
    // provider, not fall through to a visual edit or leave the composer busy.
    for (const auto &request :
         {QStringLiteral("Suggest sounds"), QStringLiteral("Add a 2 second sound of waves"), QStringLiteral("Add a 11 second sound of waves"),
          QStringLiteral("Apply a whip pan"), QStringLiteral("Add a sound effect"), QStringLiteral("Add sound:"), QStringLiteral("Add captions please")}) {
        composer->setPlainText(request);
        chat->submit();
        REQUIRE_FALSE(worker->busy());
        REQUIRE(composer->isEnabled());
        REQUIRE(chat->draft().isEmpty());
    }
    const auto history = widget.findChild<QTextBrowser *>()->toPlainText();
    REQUIRE_FALSE(history.contains(QStringLiteral("Analyzing footage")));
    REQUIRE(history.contains(QStringLiteral("not available yet")));
}

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

TEST_CASE("Assistant replaces only its last draft with atomic native undo", "[Montaz]")
{
    QTemporaryDir temporary;
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto stack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(stack);
    document.setUrl(QUrl::fromLocalFile(temporary.filePath(QStringLiteral("draft.kdenlive"))));
    document.setDocumentProperty(QStringLiteral("documentid"), document.uuid().toString(QUuid::WithoutBraces));
    pCore->projectManager()->testSetDocument(&document);
    const auto previousProfile = pCore->getCurrentProfilePath();
    REQUIRE(pCore->setCurrentProfile(QStringLiteral("atsc_1080p_2997")));
    auto timeline = TimelineItemModel::construct(document.uuid(), stack);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int video = -1, audio = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, video));
    REQUIRE(timeline->requestTrackInsertion(-1, audio, QString(), true));
    {
        const auto source = KdenliveTests::createProducerWithSound(pCore->getProjectProfile(), bin, 300);
        int before = -1, after = -1, music = -1;
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/0/9").arg(source), video, 0, before));
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/0/29").arg(source), video, 150, after));
        const auto musicPath = temporary.filePath(QStringLiteral("preserved.wav"));
        writeMusicWave(musicPath, 10);
        auto audioProducer = std::make_shared<Mlt::Producer>(pCore->getProjectProfile(), "avformat", musicPath.toUtf8().constData());
        REQUIRE(audioProducer->is_valid());
        QString audioSource;
        Fun audioUndo = []() { return true; }, audioRedo = []() { return true; };
        REQUIRE(bin->requestAddBinClip(audioSource, audioProducer, QStringLiteral("-1"), audioUndo, audioRedo));
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("A%1/0/239").arg(audioSource), audio, 0, music));
        stack->clear();
        Montaz::EditorAdapter adapter;
        auto proposalFor = [&](const QString &request, const QJsonArray &operations) {
            const auto context = adapter.snapshot();
            return QJsonObject{{"request_id", request},
                               {"project_id", context.value("project_id")},
                               {"sequence_id", context.value("sequence_id")},
                               {"base_revision", context.value("revision")},
                               {"frame_rate", context.value("frame_rate")},
                               {"protocol_version", 1},
                               {"operations", operations}};
        };
        QJsonArray initial;
        for (int i = 0; i < 2; ++i)
            initial.append(QJsonObject{{"instance_id", QStringLiteral("first:%1").arg(i)},
                                       {"bin_id", source},
                                       {"track_id", QString::number(video)},
                                       {"start_frame", 30 + i * 60},
                                       {"in_frame", i * 10},
                                       {"duration_frames", 60}});
        REQUIRE(adapter.apply(proposalFor(QStringLiteral("first"), initial)).isEmpty());
        REQUIRE(stack->count() == 1);
        const auto originalDraft = adapter.draftRequest();
        REQUIRE_FALSE(originalDraft.isEmpty());
        REQUIRE(timeline->checkConsistency());
        const auto model = timeline->createSubtitleModel();
        Fun subtitleUndo = []() { return true; }, subtitleRedo = []() { return true; };
        const double fps = pCore->getCurrentFps();
        const SubtitleEvent event(true, GenTime(90, fps), model->getLayerDefaultStyle(0), QString(), 0, 0, 0, QString(), QStringLiteral("Preserved words"));
        REQUIRE(model->addSubtitle({0, GenTime(45, fps)}, event, subtitleUndo, subtitleRedo));
        const auto subtitleId = model->getSubtitleIdFromIndex(0);
        const auto original = adapter.snapshot();
        QJsonArray revised;
        for (int i = 0; i < 3; ++i)
            revised.append(QJsonObject{{"instance_id", QStringLiteral("revised:%1").arg(i)},
                                       {"bin_id", source},
                                       {"track_id", QString::number(video)},
                                       {"start_frame", 30 + i * 40},
                                       {"in_frame", 90 - i * 20},
                                       {"duration_frames", 40}});
        const auto proposal = proposalFor(QStringLiteral("revised"), revised);
        SECTION("Replace, selection safety, ownership, undo redo and serialization")
        {
            const auto firstId = originalDraft.value("clips").toArray().first().toObject().value("instance_id").toString().toInt();
            REQUIRE(timeline->requestSetSelection({firstId, after}));
            const auto context = adapter.snapshot();
            auto currentProposal = proposal;
            currentProposal["base_revision"] = context.value("revision");
            REQUIRE(adapter.applyDraft(context, originalDraft, currentProposal).isEmpty());
            REQUIRE(stack->count() == 2);
            REQUIRE(timeline->getTrackClipsCount(video) == 5);
            REQUIRE(timeline->getClipByPosition(video, 0) == before);
            REQUIRE(timeline->getClipByPosition(video, 150) == after);
            REQUIRE(timeline->getClipByPosition(audio, 0) == music);
            REQUIRE(timeline->getClipPlaytime(music) == 240);
            REQUIRE(model->getSubtitleIdFromIndex(0) == subtitleId);
            REQUIRE(model->getAllSubtitles().first().second.text() == QStringLiteral("Preserved words"));
            const auto applied = adapter.snapshot().value("occupied");
            const auto revisedDraft = adapter.draftRequest();
            REQUIRE(revisedDraft.value("clips").toArray().size() == 3);
            REQUIRE_FALSE(adapter.applyDraft(context, originalDraft, currentProposal).isEmpty());
            stack->undo();
            REQUIRE(adapter.snapshot().value("occupied") == original.value("occupied"));
            REQUIRE(adapter.draftRequest() == originalDraft);
            stack->redo();
            REQUIRE(adapter.snapshot().value("occupied") == applied);
            REQUIRE(adapter.draftRequest() == revisedDraft);
            const auto scene = temporary.filePath(QStringLiteral("revised.mlt"));
            {
                Mlt::Consumer serializer(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
                REQUIRE(serializer.is_valid());
                serializer.connect(*timeline->tractor());
                serializer.run();
            }
            Mlt::Producer reopened(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
            REQUIRE(reopened.is_valid());
            REQUIRE(reopened.get_length() >= 240);
        }
        SECTION("Manual trim, locks and stale reviews refuse replacement")
        {
            const int id = originalDraft.value("clips").toArray().first().toObject().value("instance_id").toString().toInt();
            REQUIRE(timeline->requestItemResize(id, 59, true) == 59);
            REQUIRE(adapter.draftRequest().isEmpty());
            const auto edited = adapter.snapshot().value("occupied");
            REQUIRE_FALSE(adapter.applyDraft(original, originalDraft, proposal).isEmpty());
            REQUIRE(adapter.snapshot().value("occupied") == edited);
            stack->undo();
            REQUIRE_FALSE(adapter.draftRequest().isEmpty());
            timeline->setTrackLockedState(video, true);
            REQUIRE(adapter.draftRequest().isEmpty());
            timeline->setTrackLockedState(video, false);
            REQUIRE_FALSE(adapter.applyDraft(original, originalDraft, proposal).isEmpty());
        }
        SECTION("Shortened and escaping revisions cannot delete any original clip")
        {
            for (const auto &change :
                 {QJsonObject{{"duration_frames", 39}}, QJsonObject{{"track_id", QString::number(audio)}}, QJsonObject{{"in_frame", 300}}}) {
                auto invalid = proposal;
                auto operations = revised;
                auto last = operations.last().toObject();
                for (auto it = change.begin(); it != change.end(); ++it)
                    last[it.key()] = it.value();
                operations[operations.size() - 1] = last;
                invalid["operations"] = operations;
                REQUIRE_FALSE(adapter.applyDraft(original, originalDraft, invalid).isEmpty());
                REQUIRE(adapter.snapshot().value("occupied") == original.value("occupied"));
                REQUIRE(stack->count() == 1);
            }
        }
        stack->clear();
    }
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
    if (!previousProfile.isEmpty()) pCore->setCurrentProfile(previousProfile);
}

TEST_CASE("Assistant refines existing cuts with native identities and atomic undo", "[Montaz]")
{
    const bool rhythmic = GENERATE(false, true);
    const bool owned = GENERATE(false, true);
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
        Montaz::EditorAdapter adapter;
        std::vector<int> ids;
        QJsonArray initial;
        for (int i = 0; i < 3; ++i) {
            if (owned) {
                initial.append(QJsonObject{{"instance_id", QStringLiteral("owned:%1").arg(i)},
                                           {"bin_id", source},
                                           {"track_id", QString::number(video)},
                                           {"start_frame", 20 + i * shot},
                                           {"in_frame", 5},
                                           {"duration_frames", shot}});
            } else {
                int id = -1;
                REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/5/%2").arg(source).arg(shot + 4), video, 20 + i * shot, id));
                ids.push_back(id);
            }
        }
        if (owned) {
            const auto context = adapter.snapshot();
            const QJsonObject first{{"request_id", QStringLiteral("owned-timing")},
                                    {"project_id", context.value("project_id")},
                                    {"sequence_id", context.value("sequence_id")},
                                    {"base_revision", context.value("revision")},
                                    {"frame_rate", context.value("frame_rate")},
                                    {"protocol_version", 1},
                                    {"operations", initial}};
            REQUIRE(adapter.apply(first).isEmpty());
            for (int i = 0; i < 3; ++i)
                ids.push_back(timeline->getClipByPosition(video, 20 + i * shot));
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
        const auto context = adapter.snapshot(), timing = adapter.timingRequest();
        const auto draftBefore = adapter.draftRequest();
        REQUIRE(adapter.hasDraft() == owned);
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
            proposal[QStringLiteral("analysis")] = QJsonObject{{QStringLiteral("version"), QStringLiteral("periodic-onsets-2")},
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
            if (owned) REQUIRE(adapter.draftRequest().value("clips") == operations);
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
            if (owned) REQUIRE(adapter.draftRequest() == draftBefore);
            for (int i = 0; i < 3; ++i) {
                REQUIRE(timeline->getClipPosition(ids[i]) == 20 + i * shot);
                REQUIRE(timeline->getClipPlaytime(ids[i]) == shot);
                REQUIRE(timeline->getClipIn(ids[i]) == 5);
            }
            REQUIRE(timeline->checkConsistency());
            stack->redo();
            if (owned) REQUIRE(adapter.draftRequest().value("clips") == operations);
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

TEST_CASE("Quick montage appends short aligned shots with one native undo", "[Montaz][Montage]")
{
    const auto oldProfile = pCore->getCurrentProfilePath();
    const int length = GENERATE(8, 10, 12);
    const auto profile = GENERATE(QStringLiteral("atsc_1080p_25"), QStringLiteral("atsc_1080p_2997"));
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto undo = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undo);
    pCore->projectManager()->testSetDocument(&document);
    REQUIRE(pCore->setCurrentProfile(profile));
    auto timeline = TimelineItemModel::construct(document.uuid(), undo);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int track = -1, audio = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, track));
    REQUIRE(timeline->requestTrackInsertion(-1, audio, QString(), true));
    QList<int> ids;
    QStringList bins;
    for (int i = 0; i < 3; ++i) {
        const auto bid = KdenliveTests::createAVProducer(pCore->getProjectProfile(), bin);
        int id = -1;
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/2/21").arg(bid), track, i * 20, id, true, false, false));
        ids << id;
        bins << bid;
    }
    int neighbor = -1, sound = -1;
    REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/0/9").arg(bins[0]), track, 90, neighbor, true, false, false));
    REQUIRE(timeline->requestClipInsertion(QStringLiteral("A%1/0/9").arg(bins[0]), audio, 120, sound, true, false, false));
    REQUIRE(timeline->requestSetSelection({ids[0], ids[1], ids[2]}));
    undo->clear();
    {
        Montaz::EditorAdapter adapter;
        const auto context = adapter.snapshot(), spec = adapter.montageRequest(length);
        REQUIRE_FALSE(spec.isEmpty());
        REQUIRE(spec.value("start_frame").toInt() >= 130);
        QJsonArray operations;
        QJsonObject hashes;
        for (const auto &bid : bins) {
            QFile file(bin->getClipByBinID(bid)->clipUrl());
            REQUIRE(file.open(QIODevice::ReadOnly));
            hashes[bid] = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
        }
        for (int i : {2, 0, 1}) {
            QJsonArray keys;
            for (int j = 0; j < length; ++j)
                keys.append(
                    QJsonObject{{"frame", j}, {"x", -0.025}, {"y", -0.025}, {"scale", 1.05}, {"anchor_x", 0.5}, {"anchor_y", 0.5}, {"anchor_height", 0.2}});
            operations.append(QJsonObject{{"source_instance_id", QString::number(ids[i])}, {"in_frame", 3 + i}, {"keyframes", keys}});
        }
        QJsonObject review{{"request_id", "quick-montage-test"},
                           {"project_id", context.value("project_id")},
                           {"sequence_id", context.value("sequence_id")},
                           {"base_revision", context.value("revision")},
                           {"frame_rate", context.value("frame_rate")},
                           {"protocol_version", 5},
                           {"status", "matched"},
                           {"reason", "Controlled shared framing"},
                           {"source_hashes", hashes},
                           {"operations", operations},
                           {"target", QJsonObject{{"x", 0.5}, {"y", 0.5}, {"height", 0.21}}},
                           {"excluded", QJsonArray{}}};
        REQUIRE(Montaz::validateMontageReview(context, spec, review).isEmpty());
        SECTION("Reordered short copies preserve originals, neighbors and audio")
        {
            REQUIRE(adapter.applyMontage(context, spec, review).isEmpty());
            REQUIRE(undo->count() == 1);
            REQUIRE(timeline->getTrackClipsCount(track) == 7);
            REQUIRE(timeline->getTrackClipsCount(audio) == 1);
            QStringList saved;
            int index = 0;
            for (int source : {2, 0, 1}) {
                const int id = timeline->getClipByPosition(track, spec.value("start_frame").toInt() + index * length);
                REQUIRE(id >= 0);
                REQUIRE(timeline->getClipBinId(id) == bins[source]);
                REQUIRE(timeline->getClipIn(id) == 3 + source);
                REQUIRE(timeline->getClipPlaytime(id) == length);
                REQUIRE(timeline->getClipByPosition(track, spec.value("start_frame").toInt() + (index + 1) * length - 1) == id);
                const auto model = timeline->getClipEffectStackModel(id)->getAssetModelById(QStringLiteral("qtblend"));
                REQUIRE(model);
                const auto parameters = model->getAllParameters();
                model->prepareKeyframes();
                REQUIRE(parameters == model->getAllParameters());
                const auto rect = std::find_if(parameters.begin(), parameters.end(), [](const auto &v) { return v.first == QStringLiteral("rect"); });
                REQUIRE(rect != parameters.end());
                REQUIRE(rect->second.toString().split(';').size() == length);
                REQUIRE(rect->second.toString().startsWith(QString::number(3 + source) + QLatin1Char('=')));
                saved << rect->second.toString();
                ++index;
            }
            for (int i = 0; i < ids.size(); ++i) {
                REQUIRE(timeline->getClipPosition(ids[i]) == i * 20);
                REQUIRE(timeline->getClipIn(ids[i]) == 2);
                REQUIRE(timeline->getClipPlaytime(ids[i]) == 20);
                REQUIRE(timeline->getClipEffectStackModel(ids[i])->rowCount() == 0);
            }
            REQUIRE(timeline->getClipPosition(neighbor) == 90);
            REQUIRE(timeline->getClipPosition(sound) == 120);
            REQUIRE(timeline->checkConsistency());
            REQUIRE_FALSE(adapter.applyMontage(context, spec, review).isEmpty());
            undo->undo();
            REQUIRE(timeline->getTrackClipsCount(track) == 4);
            REQUIRE(timeline->getClipPosition(sound) == 120);
            REQUIRE_FALSE(adapter.applyMontage(context, spec, review).isEmpty());
            undo->redo();
            REQUIRE(timeline->getTrackClipsCount(track) == 7);
            for (int i = 0; i < saved.size(); ++i) {
                const int id = timeline->getClipByPosition(track, spec.value("start_frame").toInt() + i * length);
                const auto params = timeline->getClipEffectStackModel(id)->getAssetModelById(QStringLiteral("qtblend"))->getAllParameters();
                const auto rect = std::find_if(params.begin(), params.end(), [](const auto &v) { return v.first == QStringLiteral("rect"); });
                REQUIRE(rect != params.end());
                REQUIRE(rect->second.toString() == saved[i]);
            }
            REQUIRE(timeline->checkConsistency());
        }
        SECTION("Source checksum and selected-source changes reject before insertion")
        {
            auto bad = review;
            auto changed = hashes;
            changed[bins[0]] = QString(64, 'a');
            bad["source_hashes"] = changed;
            REQUIRE_FALSE(adapter.applyMontage(context, spec, bad).isEmpty());
            REQUIRE(timeline->requestSetSelection({ids[0], ids[1]}));
            REQUIRE_FALSE(adapter.applyMontage(context, spec, review).isEmpty());
            REQUIRE(undo->count() == 0);
            REQUIRE(timeline->getTrackClipsCount(track) == 4);
        }
        SECTION("Original source trims and common framing are enforced")
        {
            auto bad = review;
            auto ops = operations;
            auto op = ops[1].toObject();
            op["in_frame"] = 0;
            ops[1] = op;
            bad["operations"] = ops;
            REQUIRE_FALSE(adapter.applyMontage(context, spec, bad).isEmpty());
            op = operations[1].toObject();
            auto keys = op["keyframes"].toArray();
            auto key = keys[0].toObject();
            key["anchor_x"] = 0.7;
            keys[0] = key;
            op["keyframes"] = keys;
            ops[1] = op;
            bad["operations"] = ops;
            REQUIRE_FALSE(adapter.applyMontage(context, spec, bad).isEmpty());
            REQUIRE(undo->count() == 0);
            REQUIRE(timeline->getTrackClipsCount(track) == 4);
        }
        SECTION("A partial insertion and effect batch rolls back on failure")
        {
            int step = 0;
            const auto result = Montaz::applyInsertBatch(
                operations,
                [&](const QJsonObject &, Montaz::UndoFunction &u, Montaz::UndoFunction &r) {
                    if (++step == 2) return false;
                    int id = -1;
                    if (!timeline->requestClipInsertion(QStringLiteral("V%1/3/%2").arg(bins[0]).arg(3 + length - 1), track, spec.value("start_frame").toInt(),
                                                        id, false, true, false, u, r, {}, 1))
                        return false;
                    return timeline->getClipEffectStackModel(id)->appendEffectWithUndo(QStringLiteral("qtblend"), u, r).first;
                },
                [](const auto &, const auto &) { FAIL("Failed montage cannot commit"); });
            REQUIRE(result == QStringLiteral("apply_failed_rolled_back"));
            REQUIRE(undo->count() == 0);
            REQUIRE(timeline->getTrackClipsCount(track) == 4);
            REQUIRE(timeline->checkConsistency());
        }
    }
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
    pCore->setCurrentProfile(oldProfile);
}

TEST_CASE("Quick montage shared framing survives serialization and export", "[Montaz][MontageRender]")
{
    const auto root = qEnvironmentVariable("MONTAZ_MONTAGE_QA_ROOT");
    if (root.isEmpty()) {
        SUCCEED("Set MONTAZ_MONTAGE_QA_ROOT for generated whole-shot render checks");
        return;
    }
    const int length = GENERATE(8, 10, 12);
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
    QJsonObject hashes;
    for (int i = 0; i < 3; ++i) {
        const auto path = QDir(root).filePath(QStringLiteral("subject-%1.mp4").arg(i));
        auto producer = std::make_shared<Mlt::Producer>(pCore->getProjectProfile(), path.toUtf8().constData());
        REQUIRE(producer->is_valid());
        QString bid;
        Fun u = []() { return true; }, r = []() { return true; };
        REQUIRE(bin->requestAddBinClip(bid, producer, QStringLiteral("-1"), u, r));
        int id = -1;
        REQUIRE(timeline->requestClipInsertion(QStringLiteral("V%1/0/59").arg(bid), track, i * 60, id, true, false, false));
        ids << id;
        bids << bid;
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        hashes[bid] = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    }
    REQUIRE(timeline->requestSetSelection({ids[0], ids[1], ids[2]}));
    undo->clear();
    {
        Montaz::EditorAdapter adapter;
        const auto context = adapter.snapshot(), spec = adapter.montageRequest(length);
        INFO(QJsonDocument(context).toJson(QJsonDocument::Compact).toStdString());
        QJsonArray metadata;
        for (const auto &bid : bids) {
            const auto asset = bin->getClipByBinID(bid);
            metadata.append(QJsonObject{{"bin", bid},
                                        {"width", asset->frameSize().width()},
                                        {"height", asset->frameSize().height()},
                                        {"effects", asset->getEffectStack()->rowCount()}});
        }
        INFO(QJsonDocument(metadata).toJson(QJsonDocument::Compact).toStdString());
        REQUIRE_FALSE(spec.isEmpty());
        QJsonArray operations;
        // Known synthetic rectangles move one source pixel per frame. This
        // independent ground truth checks native transforms, not AI detection.
        for (int i : {2, 0, 1}) {
            const int in = 5 + i * 4;
            QJsonArray keys;
            for (int j = 0; j < length; ++j) {
                const double center = double(900 + i * 60 + in + j) / 1920;
                keys.append(QJsonObject{
                    {"frame", j}, {"x", 0.5 - 1.25 * center}, {"y", -0.125}, {"scale", 1.25}, {"anchor_x", center}, {"anchor_y", 0.5}, {"anchor_height", 0.2}});
            }
            operations.append(QJsonObject{{"source_instance_id", QString::number(ids[i])}, {"in_frame", in}, {"keyframes", keys}});
        }
        const QJsonObject review{{"request_id", "montage-render"},
                                 {"project_id", context.value("project_id")},
                                 {"sequence_id", context.value("sequence_id")},
                                 {"base_revision", context.value("revision")},
                                 {"frame_rate", context.value("frame_rate")},
                                 {"protocol_version", 5},
                                 {"status", "matched"},
                                 {"reason", "Known moving rectangles, shared framing"},
                                 {"source_hashes", hashes},
                                 {"operations", operations},
                                 {"target", QJsonObject{{"x", 0.5}, {"y", 0.5}, {"height", 0.25}}},
                                 {"excluded", QJsonArray{}}};
        REQUIRE(adapter.applyMontage(context, spec, review).isEmpty());
        REQUIRE(undo->count() == 1);
        const int start = spec.value("start_frame").toInt();
        auto checkFrame = [](Mlt::Producer &producer, int position) {
            producer.seek(position);
            std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
            mlt_image_format format = mlt_image_rgb;
            int width = 640, height = 360;
            const auto pixels = frame->get_image(format, width, height);
            REQUIRE(pixels);
            REQUIRE(format == mlt_image_rgb);
            int top = height, bottom = -1, count = 0;
            double sx = 0, sy = 0;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    const auto p = pixels + 3 * (y * width + x);
                    if (p[0] > 100 && p[1] > 100 && p[2] < 60) {
                        top = qMin(top, y);
                        bottom = qMax(bottom, y);
                        sx += x;
                        sy += y;
                        ++count;
                    }
                }
            REQUIRE(count > 100);
            REQUIRE(std::abs(sx / count / width - 0.5) < 0.004);
            REQUIRE(std::abs(sy / count / height - 0.5) < 0.006);
            REQUIRE(std::abs(double(bottom - top + 1) / height - 0.25) < 0.007);
        };
        for (int i = 0; i < 3 * length; ++i)
            checkFrame(*timeline->tractor(), start + i);
        const auto scene = QDir(root).filePath(QStringLiteral("montage-%1.mlt").arg(length));
        {
            Mlt::Consumer serializer(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
            REQUIRE(serializer.is_valid());
            serializer.connect(*timeline->tractor());
            serializer.run();
        }
        Mlt::Producer reopened(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
        REQUIRE(reopened.is_valid());
        for (int i = 0; i < 3 * length; ++i)
            checkFrame(reopened, start + i);
        undo->undo();
        REQUIRE(timeline->getTrackClipsCount(track) == 3);
        undo->redo();
        for (int i = 0; i < 3 * length; ++i)
            checkFrame(*timeline->tractor(), start + i);
        const auto output = QDir(root).filePath(QStringLiteral("montage-%1.mp4").arg(length));
        {
            Mlt::Consumer renderer(pCore->getProjectProfile(), "avformat", output.toUtf8().constData());
            REQUIRE(renderer.is_valid());
            renderer.set("vcodec", "libx264");
            renderer.set("crf", 18);
            renderer.set("preset", "fast");
            renderer.set("audio_off", 1);
            renderer.set("real_time", -1);
            renderer.set("terminate_on_pause", 1);
            std::unique_ptr<Mlt::Producer> zone(timeline->tractor()->cut(start, start + 3 * length - 1));
            renderer.connect(*zone);
            renderer.run();
        }
        REQUIRE(QFileInfo(output).size() > 1000);
        Mlt::Producer exported(pCore->getProjectProfile(), output.toUtf8().constData());
        REQUIRE(exported.is_valid());
        REQUIRE(exported.get_length() == 3 * length);
        for (int i = 0; i < 3 * length; ++i)
            checkFrame(exported, i);
        REQUIRE(timeline->checkConsistency());
    }
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
    pCore->setCurrentProfile(oldProfile);
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

TEST_CASE("Editorial proposals apply, serialize and export at their measured duration", "[Montaz]")
{
    const auto root = qEnvironmentVariable("MONTAZ_EDITORIAL_QA_ROOT");
    if (root.isEmpty()) return;
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
    QFile sourcesFile(QDir(root).filePath(QStringLiteral("private-sources.json")));
    REQUIRE(sourcesFile.open(QIODevice::ReadOnly));
    const auto sources = QJsonDocument::fromJson(sourcesFile.readAll()).object();
    QMap<QString, QString> binIds;
    for (auto it = sources.begin(); it != sources.end(); ++it) {
        auto producer = std::make_shared<Mlt::Producer>(pCore->getProjectProfile(), it.value().toString().toUtf8().constData());
        REQUIRE(producer->is_valid());
        auto bid = QString::number(bin->getFreeClipId());
        Fun u = []() { return true; }, r = []() { return true; };
        REQUIRE(bin->requestAddBinClip(bid, producer, QStringLiteral("-1"), u, r));
        binIds[it.key()] = bid;
    }
    undo->clear();
    {
        Montaz::EditorAdapter adapter;
        const auto context = adapter.snapshot();
        QFile input(QDir(root).filePath(QStringLiteral("result.json")));
        REQUIRE(input.open(QIODevice::ReadOnly));
        const auto result = QJsonDocument::fromJson(input.readAll()).object();
        auto proposal = result.value("proposal").toObject();
        proposal["project_id"] = context.value("project_id");
        proposal["sequence_id"] = context.value("sequence_id");
        proposal["base_revision"] = context.value("revision");
        auto operations = proposal.value("operations").toArray();
        int total = 0;
        for (int i = 0; i < operations.size(); ++i) {
            auto op = operations[i].toObject();
            REQUIRE(binIds.contains(op.value("bin_id").toString()));
            op["bin_id"] = binIds.value(op.value("bin_id").toString());
            op["track_id"] = QString::number(track);
            REQUIRE(op.value("start_frame").toInt() == total);
            total += op.value("duration_frames").toInt();
            operations[i] = op;
        }
        proposal["operations"] = operations;
        REQUIRE(Montaz::validateEditorialReview(context, proposal, result.value("review").toObject()).isEmpty());
        REQUIRE(adapter.apply(proposal).isEmpty());
        REQUIRE(undo->count() == 1);
        REQUIRE(timeline->getTrackClipsCount(track) == operations.size());
        REQUIRE(timeline->duration() == total);
        REQUIRE(timeline->checkConsistency());
        const auto applied = adapter.snapshot().value("occupied").toArray();
        for (const auto item : applied) {
            const auto clip = item.toObject();
            const auto id = clip.value("instance_id").toString().toInt();
            const int start = clip.value("start_frame").toInt(), duration = clip.value("duration_frames").toInt();
            REQUIRE(timeline->getClipByPosition(track, start) == id);
            REQUIRE(timeline->getClipByPosition(track, start + duration - 1) == id);
        }
        undo->undo();
        REQUIRE(timeline->getTrackClipsCount(track) == 0);
        undo->redo();
        REQUIRE(adapter.snapshot().value("occupied").toArray() == applied);
        const auto scene = QDir(root).filePath(QStringLiteral("native-editorial.mlt"));
        {
            Mlt::Consumer serializer(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
            REQUIRE(serializer.is_valid());
            serializer.connect(*timeline->tractor());
            serializer.run();
        }
        Mlt::Producer reopened(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
        REQUIRE(reopened.is_valid());
        // The serialized black background includes seek padding. Render only
        // the reviewed content zone, as Kdenlive's native rendering path does.
        REQUIRE(reopened.get_length() >= total);
        const auto output = QDir(root).filePath(QStringLiteral("native-editorial.mp4"));
        {
            Mlt::Consumer renderer(pCore->getProjectProfile(), "avformat", output.toUtf8().constData());
            REQUIRE(renderer.is_valid());
            renderer.set("vcodec", "libx264");
            renderer.set("crf", 18);
            renderer.set("preset", "fast");
            renderer.set("audio_off", 1);
            renderer.set("real_time", -1);
            renderer.set("in", 0);
            renderer.set("out", total - 1);
            renderer.set("terminate_on_pause", 1);
            std::unique_ptr<Mlt::Producer> zone(reopened.cut(0, total - 1));
            renderer.connect(*zone);
            renderer.run();
        }
        REQUIRE(QFileInfo(output).size() > 1000);
    }
    undo->clear();
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
    pCore->setCurrentProfile(oldProfile);
}

TEST_CASE("Applied cuts agree across timeline, undo, reopened scene and export", "[Montaz][PlayheadQA]")
{
    // Generated by assistant/tools/check_playhead_agreement.py: each source encodes
    // its own frame number in luma and its identity in chroma.
    const auto root = qEnvironmentVariable("MONTAZ_PLAYHEAD_QA_ROOT");
    if (root.isEmpty()) {
        SUCCEED("Set MONTAZ_PLAYHEAD_QA_ROOT for generated mixed-rate cut-frame checks");
        return;
    }
    QFile input(QDir(root).filePath(QStringLiteral("sources.json")));
    REQUIRE(input.open(QIODevice::ReadOnly));
    const auto plan = QJsonDocument::fromJson(input.readAll()).object();
    const auto oldProfile = pCore->getCurrentProfilePath();
    auto bin = pCore->projectItemModel();
    bin->clean();
    auto undo = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undo);
    pCore->projectManager()->testSetDocument(&document);
    REQUIRE(pCore->setCurrentProfile(plan.value("profile").toString()));
    auto timeline = TimelineItemModel::construct(document.uuid(), undo);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    int track = -1;
    REQUIRE(timeline->requestTrackInsertion(-1, track));
    QStringList bids;
    for (const auto v : plan.value("clips").toArray()) {
        auto producer = std::make_shared<Mlt::Producer>(pCore->getProjectProfile(), v.toObject().value("path").toString().toUtf8().constData());
        REQUIRE(producer->is_valid());
        auto bid = QString::number(bin->getFreeClipId());
        Fun u = []() { return true; }, r = []() { return true; };
        REQUIRE(bin->requestAddBinClip(bid, producer, QStringLiteral("-1"), u, r));
        bids << bid;
    }
    undo->clear();
    {
        Montaz::EditorAdapter adapter;
        const auto context = adapter.snapshot();
        REQUIRE_FALSE(context.isEmpty());
        QJsonArray operations;
        int position = 0;
        const auto clips = plan.value("clips").toArray();
        for (int i = 0; i < clips.size(); ++i) {
            const auto c = clips[i].toObject();
            operations.append(QJsonObject{{"instance_id", QStringLiteral("qa:%1").arg(i)},
                                          {"bin_id", bids[i]},
                                          {"track_id", QString::number(track)},
                                          {"start_frame", position},
                                          {"in_frame", c.value("in_frame")},
                                          {"duration_frames", c.value("duration_frames")}});
            position += c.value("duration_frames").toInt();
        }
        const int total = position;
        const QJsonObject proposal{{"request_id", "playhead-qa"},
                                   {"project_id", context.value("project_id")},
                                   {"sequence_id", context.value("sequence_id")},
                                   {"base_revision", context.value("revision")},
                                   {"frame_rate", context.value("frame_rate")},
                                   {"protocol_version", 1},
                                   {"operations", operations}};
        const auto applied = adapter.apply(proposal);
        INFO(applied.toStdString());
        REQUIRE(applied.isEmpty());
        REQUIRE(timeline->duration() == total);
        // Mean luma of the frame-number (upper) and clip-identity (lower) blocks.
        auto sample = [](Mlt::Producer &producer, int position) {
            producer.seek(position);
            std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
            mlt_image_format format = mlt_image_rgb;
            int w = 320, h = 180;
            const auto pixels = frame->get_image(format, w, h);
            REQUIRE(pixels);
            QJsonArray result;
            for (const int centre : {h / 4, 3 * h / 4}) {
                double luma = 0;
                int count = 0;
                for (int y = centre - 8; y < centre + 8; ++y)
                    for (int x = w / 2 - 8; x < w / 2 + 8; ++x, ++count) {
                        const auto p = pixels + 3 * (y * w + x);
                        luma += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
                    }
                result.append(luma / count);
            }
            return result;
        };
        auto all = [&](Mlt::Producer &producer) {
            QJsonArray frames;
            for (int f = 0; f < total; ++f)
                frames.append(sample(producer, f));
            return frames;
        };
        QJsonObject samples{{"total", total}, {"timeline", all(*timeline->tractor())}};
        undo->undo();
        REQUIRE(timeline->duration() == 0);
        undo->redo();
        samples["redo"] = all(*timeline->tractor());
        const auto scene = QDir(root).filePath(QStringLiteral("native-playhead.mlt"));
        {
            Mlt::Consumer serializer(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
            REQUIRE(serializer.is_valid());
            serializer.connect(*timeline->tractor());
            serializer.run();
        }
        Mlt::Producer reopened(pCore->getProjectProfile(), "xml", scene.toUtf8().constData());
        REQUIRE(reopened.is_valid());
        samples["reopened"] = all(reopened);
        const auto output = QDir(root).filePath(QStringLiteral("native-playhead.mp4"));
        {
            Mlt::Consumer renderer(pCore->getProjectProfile(), "avformat", output.toUtf8().constData());
            REQUIRE(renderer.is_valid());
            renderer.set("vcodec", "libx264");
            renderer.set("crf", 0);
            renderer.set("preset", "fast");
            renderer.set("audio_off", 1);
            renderer.set("real_time", -1);
            renderer.set("in", 0);
            renderer.set("out", total - 1);
            renderer.set("terminate_on_pause", 1);
            std::unique_ptr<Mlt::Producer> zone(reopened.cut(0, total - 1));
            renderer.connect(*zone);
            renderer.run();
        }
        Mlt::Producer exported(pCore->getProjectProfile(), output.toUtf8().constData());
        REQUIRE(exported.is_valid());
        REQUIRE(exported.get_length() == total);
        samples["export"] = all(exported);
        QFile result(QDir(root).filePath(QStringLiteral("native-samples.json")));
        REQUIRE(result.open(QIODevice::WriteOnly));
        result.write(QJsonDocument(samples).toJson(QJsonDocument::Compact));
    }
    undo->clear();
    pCore->taskManager.slotCancelJobs();
    document.closing = true;
    bin->clean();
    pCore->projectManager()->testSetDocument(nullptr);
    pCore->setCurrentProfile(oldProfile);
}
