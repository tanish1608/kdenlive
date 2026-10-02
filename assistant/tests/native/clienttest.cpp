// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "chatcommands.h"
#include "editbatch.h"
#include "montage.h"
#include "proposal.h"
#include "workerclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_UNIX
#include <signal.h>
#endif

namespace {
QJsonObject snapshot()
{
    QJsonArray assets;
    for (int i = 1; i <= 6; ++i)
        assets.append(QJsonObject{{"bin_id", QString::number(i)}, {"duration_frames", 300}, {"has_audio", true}, {"has_video", true}});
    return {{"project_id", "project"},
            {"sequence_id", "sequence"},
            {"revision", 2},
            {"frame_rate", QJsonObject{{"numerator", 30}, {"denominator", 1}}},
            {"assets", assets},
            {"tracks", QJsonArray{QJsonObject{{"track_id", "1"}, {"kind", "video"}, {"locked", false}},
                                  QJsonObject{{"track_id", "2"}, {"kind", "audio"}, {"locked", false}}}},
            {"occupied", QJsonArray{}}};
}

QJsonObject operation()
{
    return {{"instance_id", "request:0"}, {"bin_id", "1"}, {"track_id", "1"}, {"start_frame", 0}, {"in_frame", 0}, {"duration_frames", 60}};
}

QJsonObject proposal()
{
    return {{"request_id", "request"},
            {"project_id", "project"},
            {"sequence_id", "sequence"},
            {"base_revision", 2},
            {"frame_rate", QJsonObject{{"numerator", 30}, {"denominator", 1}}},
            {"operations", QJsonArray{operation()}},
            {"protocol_version", 1}};
}

QJsonObject musicSnapshot()
{
    auto value = snapshot();
    value["occupied"] = QJsonArray{QJsonObject{{"instance_id", "shot"}, {"track_id", "1"}, {"start_frame", 20}, {"duration_frames", 120}}};
    return value;
}

QJsonObject musicSpec()
{
    return {{"path", "/private/project.assets/music/track.wav"}, {"track_id", "2"}, {"start_frame", 20}, {"duration_frames", 120}, {"recipe", "postcard"},
            {"instruction", "private editorial brief"}};
}

QJsonObject soundtrack()
{
    auto value = musicSpec();
    value.remove("recipe");
    value.remove("instruction");
    value["request_id"] = "request";
    value["project_id"] = "project";
    value["sequence_id"] = "sequence";
    value["base_revision"] = 2;
    value["frame_rate"] = musicSnapshot()["frame_rate"];
    value["sha256"] = QString(64, 'a');
    value["accents"] = QJsonArray{15, 60, 90};
    value["cached"] = false;
    return value;
}

QJsonObject timingSpec()
{
    QJsonArray clips;
    for (int i = 0; i < 3; ++i)
        clips.append(QJsonObject{{"instance_id", QStringLiteral("shot%1").arg(i)},
                                 {"bin_id", QString::number(i + 1)},
                                 {"track_id", "1"},
                                 {"in_frame", 0},
                                 {"start_frame", i * 60},
                                 {"duration_frames", 60}});
    return {{"clips", clips},
            {"max_shift_frames", 7},
            {"music", QJsonObject{{"instance_id", "music"}, {"path", "/private/music.wav"}, {"sha256", QString(64, 'a')}}}};
}

QJsonObject timingSnapshot()
{
    auto value = snapshot();
    QJsonArray occupied;
    for (const auto item : timingSpec()["clips"].toArray()) {
        auto clip = item.toObject();
        clip.remove("bin_id");
        clip.remove("in_frame");
        occupied.append(clip);
    }
    occupied.append(QJsonObject{{"instance_id", "music"}, {"track_id", "2"}, {"start_frame", 0}, {"duration_frames", 180}});
    value["occupied"] = occupied;
    return value;
}

QJsonObject timingProposal()
{
    auto value = proposal();
    value["protocol_version"] = 2;
    value["music_sha256"] = QString(64, 'a');
    value["accents"] = QJsonArray{64, 116};
    auto operations = timingSpec()["clips"].toArray();
    for (int i = 0; i < 3; ++i) {
        auto op = operations[i].toObject();
        op["start_frame"] = i == 0 ? 0 : i == 1 ? 64 : 116;
        op["duration_frames"] = i == 1 ? 52 : 64;
        operations[i] = op;
    }
    value["operations"] = operations;
    return value;
}

QJsonObject cueSpec()
{
    return {{"clips", timingSpec()["clips"]}};
}
QJsonObject cueSnapshot()
{
    auto value = timingSnapshot();
    auto occupied = value["occupied"].toArray();
    occupied.removeLast();
    value["occupied"] = occupied;
    return value;
}
QJsonObject cueReview()
{
    return {{"request_id", "request"},
            {"project_id", "project"},
            {"sequence_id", "sequence"},
            {"base_revision", 2},
            {"frame_rate", snapshot()["frame_rate"]},
            {"cached_assets", 3},
            {"unavailable_assets", 0},
            {"cues", QJsonArray{QJsonObject{{"instance_id", "shot0"},
                                            {"category", "waves"},
                                            {"start_frame", 0},
                                            {"duration_frames", 60},
                                            {"instruction", "Gentle coastal waves"},
                                            {"reason", "Visible coastal water"},
                                            {"source_sha256", QString(64, 'a')}}}}};
}

QJsonObject captionSpec()
{
    return {{"clips", QJsonArray{QJsonObject{{"instance_id", "spoken"},
                                             {"bin_id", "1"},
                                             {"track_id", "2"},
                                             {"start_frame", 100},
                                             {"in_frame", 30},
                                             {"duration_frames", 90},
                                             {"audio_stream", 0}}}}};
}
QJsonObject captionSnapshot()
{
    auto value = snapshot();
    value["occupied"] = QJsonArray{QJsonObject{{"instance_id", "spoken"}, {"track_id", "2"}, {"start_frame", 100}, {"duration_frames", 90}}};
    return value;
}
QJsonObject captionReview()
{
    return {{"request_id", "request"},
            {"project_id", "project"},
            {"sequence_id", "sequence"},
            {"base_revision", 2},
            {"frame_rate", snapshot()["frame_rate"]},
            {"checked_clips", 1},
            {"cached_clips", 0},
            {"skipped_clips", 0},
            {"source_sha256", QJsonObject{{"1", QString(64, 'a')}}},
            {"captions", QJsonArray{QJsonObject{{"instance_id", "spoken"}, {"start_frame", 102}, {"duration_frames", 46}, {"text", "Private spoken words"}}}}};
}

void fakeWorker(const QString &path, const QByteArray &script)
{
    QVERIFY(QDir().mkpath(path + QStringLiteral("/montaz_assistant")));
    QFile file(path + QStringLiteral("/montaz_assistant/worker.py"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(script), script.size());
}
} // namespace

namespace {
QJsonObject matchSpec()
{
    QJsonArray clips;
    for (int i = 0; i < 2; ++i)
        clips.append(QJsonObject{{"instance_id", QString::number(11 + i)},
                                 {"bin_id", QString::number(1 + i)},
                                 {"track_id", "1"},
                                 {"start_frame", i * 60},
                                 {"in_frame", 5},
                                 {"duration_frames", 60}});
    return {{"width", 1920}, {"height", 1080}, {"clips", clips}};
}
QJsonObject matchSnapshot()
{
    auto value = snapshot();
    QJsonArray occupied;
    for (const auto v : matchSpec()["clips"].toArray()) {
        auto c = v.toObject();
        c.remove("bin_id");
        c.remove("in_frame");
        occupied.append(c);
    }
    value["occupied"] = occupied;
    return value;
}
QJsonObject matchReview()
{
    QJsonArray operations;
    for (int i = 0; i < 2; ++i) {
        QJsonArray keyframes;
        for (int j = 0; j < 30; ++j)
            keyframes.append(QJsonObject{{"frame", i == 0 ? 30 + j : j}, {"x", -0.025}, {"y", -0.025}, {"scale", 1.05}});
        operations.append(QJsonObject{{"instance_id", QString::number(11 + i)}, {"keyframes", keyframes}});
    }
    return {{"request_id", "request"},
            {"project_id", "project"},
            {"sequence_id", "sequence"},
            {"base_revision", 2},
            {"frame_rate", snapshot()["frame_rate"]},
            {"protocol_version", 3},
            {"reason", "private object explanation"},
            {"status", "matched"},
            {"cached", false},
            {"operations", operations},
            {"source_hashes", QJsonObject{{"1", QString(64, 'a')}, {"2", QString(64, 'b')}}},
            {"target", QJsonObject{{"x", 0.5}, {"y", 0.5}, {"height", 0.2}}},
            {"preview", QJsonObject{{"outgoing", "/9j/2Q=="}, {"incoming", "/9j/2Q=="}}}};
}
QJsonObject montageSpec()
{
    auto value = matchSpec();
    auto clips = value["clips"].toArray();
    auto third = clips[1].toObject();
    third["instance_id"] = "13";
    third["bin_id"] = "3";
    third["start_frame"] = 120;
    clips.append(third);
    for (int i = 0; i < clips.size(); ++i) {
        auto c = clips[i].toObject();
        c["zoom_limit"] = 1.6;
        clips[i] = c;
    }
    value["clips"] = clips;
    value["shot_frames"] = 10;
    value["track_id"] = "1";
    value["start_frame"] = 180;
    return value;
}
QJsonObject montageSnapshot()
{
    auto value = snapshot();
    QJsonArray occupied;
    for (const auto v : montageSpec()["clips"].toArray()) {
        auto c = v.toObject();
        c.remove("bin_id");
        c.remove("in_frame");
        c.remove("zoom_limit");
        occupied.append(c);
    }
    value["occupied"] = occupied;
    return value;
}
QJsonObject montageReview()
{
    QJsonArray ops;
    for (int i : {2, 0, 1}) {
        QJsonArray keys;
        for (int j = 0; j < 10; ++j)
            keys.append(QJsonObject{{"frame", j}, {"x", -0.05}, {"y", -0.05}, {"scale", 1.1}, {"anchor_x", 0.5}, {"anchor_y", 0.5}, {"anchor_height", 0.2}});
        ops.append(QJsonObject{{"source_instance_id", QString::number(11 + i)}, {"in_frame", 7 + i}, {"keyframes", keys}});
    }
    return {{"request_id", "montage"},
            {"project_id", "project"},
            {"sequence_id", "sequence"},
            {"base_revision", 2},
            {"frame_rate", montageSnapshot()["frame_rate"]},
            {"protocol_version", 5},
            {"status", "matched"},
            {"reason", "One framing for all three shots"},
            {"source_hashes", QJsonObject{{"1", QString(64, 'a')}, {"2", QString(64, 'b')}, {"3", QString(64, 'c')}}},
            {"operations", ops},
            {"target", QJsonObject{{"x", 0.5}, {"y", 0.5}, {"height", 0.22}}},
            {"excluded", QJsonArray{}}};
}
} // namespace

class ClientTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void onlyExplicitStandaloneChatRequestsUseTools()
    {
        using Montaz::ChatAction;
        QCOMPARE(Montaz::chatCommand(QStringLiteral("Please align cuts to the beats")).action, ChatAction::BeatCuts);
        QCOMPARE(Montaz::chatCommand(QStringLiteral("Can you apply a match cut between these two clips?")).action, ChatAction::MatchCut);
        QCOMPARE(Montaz::chatCommand(QStringLiteral("Add background music")).action, ChatAction::Music);
        QCOMPARE(Montaz::chatCommand(QStringLiteral("suggest sound effects")).action, ChatAction::SoundSuggestions);
        for (const auto &message : {QStringLiteral("Don't add match cuts"), QStringLiteral("Explain match cuts"),
                                    QStringLiteral("Align cuts and make a longer edit"), QStringLiteral("Create a travel edit with no sound effects"),
                                    QStringLiteral("Suggest sounds\nand change all my clips"), QStringLiteral("Add music and apply match cuts")}) {
            QCOMPARE(Montaz::chatCommand(message).action, ChatAction::Edit);
        }
    }
    void chatSoundRequestsKeepTheirDescriptionAndDuration()
    {
        using Montaz::ChatAction;
        const auto request = Montaz::chatCommand(QStringLiteral("add a 3 second sound of coastal waves"));
        QCOMPARE(request.action, ChatAction::SoundEffect);
        QCOMPARE(request.seconds, 3.0);
        QCOMPARE(request.description, QStringLiteral("coastal waves"));
        const auto simple = Montaz::chatCommand(QStringLiteral("Add sound: quiet footsteps"));
        QCOMPARE(simple.action, ChatAction::SoundEffect);
        QCOMPARE(simple.description, QStringLiteral("quiet footsteps"));
        QCOMPARE(simple.seconds, 2.0);
        QCOMPARE(Montaz::chatCommand(QStringLiteral("add a .5 second sound of a click")).seconds, 0.5);
        for (const auto &message : {QStringLiteral("add a 11 second sound of a click"), QStringLiteral("add a 0 second sound of a click"),
                                    QStringLiteral("add a -1 second sound of a click"), QStringLiteral("add sound: ") + QString(1001, 'a')})
            QCOMPARE(Montaz::chatCommand(message).action, ChatAction::InvalidSound);
    }
    void unavailableTransitionsDoNotStartAnEditingBrief()
    {
        for (const auto &message : {QStringLiteral("Apply a whip pan"), QStringLiteral("Use object wipes"), QStringLiteral("Add a speed ramp")})
            QCOMPARE(Montaz::chatCommand(message).action, Montaz::ChatAction::UnavailableTransition);
    }
    void initTestCase()
    {
        QCoreApplication::setApplicationName(QStringLiteral("montaz-client-test-%1").arg(QCoreApplication::applicationPid()));
        QStandardPaths::setTestModeEnabled(true);
    }
    void cleanupTestCase() { QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively(); }
    void montageRequestBoundsSelectionAndAppendSpan()
    {
        QVERIFY(Montaz::validateMontageRequest(montageSnapshot(), montageSpec()).isEmpty());
        for (const auto change : {QJsonObject{{"shot_frames", 7}}, QJsonObject{{"shot_frames", 13}}, QJsonObject{{"shot_frames", true}},
                                  QJsonObject{{"start_frame", 179}}, QJsonObject{{"track_id", "2"}}, QJsonObject{{"unknown", 0}}}) {
            auto spec = montageSpec();
            for (auto it = change.begin(); it != change.end(); ++it)
                spec[it.key()] = it.value();
            QVERIFY(!Montaz::validateMontageRequest(montageSnapshot(), spec).isEmpty());
        }
        for (const auto change : {QJsonObject{{"in_frame", 299}}, QJsonObject{{"zoom_limit", 3}}, QJsonObject{{"bin_id", "999"}},
                                  QJsonObject{{"start_frame", 1}}, QJsonObject{{"duration_frames", 4}}, QJsonObject{{"instance_id", "15"}}}) {
            auto spec = montageSpec();
            auto clips = spec["clips"].toArray();
            auto c = clips[0].toObject();
            for (auto it = change.begin(); it != change.end(); ++it)
                c[it.key()] = it.value();
            clips[0] = c;
            spec["clips"] = clips;
            QVERIFY(!Montaz::validateMontageRequest(montageSnapshot(), spec).isEmpty());
        }
        auto context = montageSnapshot();
        auto tracks = context["tracks"].toArray();
        auto track = tracks[0].toObject();
        track["locked"] = true;
        tracks[0] = track;
        context["tracks"] = tracks;
        QVERIFY(!Montaz::validateMontageRequest(context, montageSpec()).isEmpty());
    }
    void montageRequiresCommonFramingAndWholeShotKeys()
    {
        QVERIFY(Montaz::validateMontageReview(montageSnapshot(), montageSpec(), montageReview()).isEmpty());
        for (const auto change :
             {QJsonObject{{"x", 0.01}}, QJsonObject{{"scale", 1.7}}, QJsonObject{{"frame", 1}}, QJsonObject{{"anchor_x", 0.6}},
              QJsonObject{{"anchor_x", 0.5015}}, QJsonObject{{"anchor_height", 0.3}}, QJsonObject{{"scale", true}}, QJsonObject{{"expression", "untrusted"}}}) {
            auto review = montageReview();
            auto ops = review["operations"].toArray();
            auto op = ops[1].toObject();
            auto keys = op["keyframes"].toArray();
            auto key = keys[0].toObject();
            for (auto it = change.begin(); it != change.end(); ++it)
                key[it.key()] = it.value();
            keys[0] = key;
            op["keyframes"] = keys;
            ops[1] = op;
            review["operations"] = ops;
            QVERIFY(!Montaz::validateMontageReview(montageSnapshot(), montageSpec(), review).isEmpty());
        }
        for (const auto change : {QJsonObject{{"in_frame", 4}}, QJsonObject{{"in_frame", 60}}, QJsonObject{{"source_instance_id", "999"}},
                                  QJsonObject{{"keyframes", QJsonArray{}}}}) {
            auto review = montageReview();
            auto ops = review["operations"].toArray();
            auto op = ops[0].toObject();
            for (auto it = change.begin(); it != change.end(); ++it)
                op[it.key()] = it.value();
            ops[0] = op;
            review["operations"] = ops;
            QVERIFY(!Montaz::validateMontageReview(montageSnapshot(), montageSpec(), review).isEmpty());
        }
    }
    void montageAccountsForExcludedShotsAndRejectsReplayContext()
    {
        auto review = montageReview();
        auto ops = review["operations"].toArray();
        ops.removeLast();
        review["operations"] = ops;
        QVERIFY(!Montaz::validateMontageReview(montageSnapshot(), montageSpec(), review).isEmpty());
        review["excluded"] = QJsonArray{QJsonObject{{"source_instance_id", "12"}, {"reason", "No compatible framing"}}};
        QVERIFY(Montaz::validateMontageReview(montageSnapshot(), montageSpec(), review).isEmpty());
        review["excluded"] = QJsonArray{QJsonObject{{"source_instance_id", "13"}, {"reason", "Duplicate chosen shot"}}};
        QVERIFY(!Montaz::validateMontageReview(montageSnapshot(), montageSpec(), review).isEmpty());
        for (const auto change : {QJsonObject{{"base_revision", 3}}, QJsonObject{{"protocol_version", 3}}, QJsonObject{{"status", "approved"}},
                                  QJsonObject{{"request_id", "bad\nrequest"}},
                                  QJsonObject{{"source_hashes", QJsonObject{{"1", QString(64, 'a') + '\n'}, {"2", QString(64, 'b')}, {"3", QString(64, 'c')}}}},
                                  QJsonObject{{"source_hashes", QJsonObject{}}}}) {
            auto value = montageReview();
            for (auto it = change.begin(); it != change.end(); ++it)
                value[it.key()] = it.value();
            QVERIFY(!Montaz::validateMontageReview(montageSnapshot(), montageSpec(), value).isEmpty());
        }
        auto value = montageReview();
        auto duplicate = value["operations"].toArray();
        duplicate[1] = duplicate[0];
        value["operations"] = duplicate;
        QVERIFY(!Montaz::validateMontageReview(montageSnapshot(), montageSpec(), value).isEmpty());
    }
    void montageEmptyResultIsTerminalAndHasNoGeometry()
    {
        auto value = montageReview();
        value["status"] = "no_match";
        value["operations"] = QJsonArray{};
        value["target"] = QJsonObject{};
        QJsonArray excluded;
        for (int id = 11; id <= 13; ++id)
            excluded.append(QJsonObject{{"source_instance_id", QString::number(id)}, {"reason", "No fit"}});
        value["excluded"] = excluded;
        QVERIFY(Montaz::validateMontageReview(montageSnapshot(), montageSpec(), value).isEmpty());
        value["target"] = montageReview()["target"];
        QVERIFY(!Montaz::validateMontageReview(montageSnapshot(), montageSpec(), value).isEmpty());
    }
    void matchContractsRejectUnsafeTransformsAndStaleIdentity()
    {
        QVERIFY(Montaz::validateMatchRequest(matchSnapshot(), matchSpec()).isEmpty());
        QVERIFY(Montaz::validateMatchReview(matchSnapshot(), matchSpec(), matchReview()).isEmpty());
        for (const auto change : {QJsonObject{{"base_revision", 3}}, QJsonObject{{"protocol_version", 1}}, QJsonObject{{"status", "approved"}},
                                  QJsonObject{{"preview", QJsonObject{{"outgoing", "invalid"}, {"incoming", "/9j/2Q=="}}}}}) {
            auto review = matchReview();
            for (auto it = change.begin(); it != change.end(); ++it)
                review[it.key()] = it.value();
            QVERIFY(!Montaz::validateMatchReview(matchSnapshot(), matchSpec(), review).isEmpty());
        }
        for (const auto change : {QJsonObject{{"x", 0.1}}, QJsonObject{{"scale", 3}}, QJsonObject{{"scale", true}}, QJsonObject{{"frame", 0}},
                                  QJsonObject{{"expression", "arbitrary"}}}) {
            auto review = matchReview();
            auto ops = review["operations"].toArray();
            auto op = ops[0].toObject();
            auto keys = op["keyframes"].toArray();
            auto k = keys[0].toObject();
            for (auto it = change.begin(); it != change.end(); ++it)
                k[it.key()] = it.value();
            keys[0] = k;
            op["keyframes"] = keys;
            ops[0] = op;
            review["operations"] = ops;
            QVERIFY(!Montaz::validateMatchReview(matchSnapshot(), matchSpec(), review).isEmpty());
        }
    }
    void matchTransportRequiresOrderedProgressTerminalEventsAndCleanExit()
    {
        for (int fault = 0; fault < 6; ++fault) {
            auto review = matchReview();
            if (fault == 1) {
                review["status"] = "no_match";
                review["operations"] = QJsonArray{};
            }
            QByteArray worker = R"PY(import json,sys
r=json.loads(sys.stdin.readline())
def send(value): print(json.dumps({'version':1,**value}),flush=True)
send({'type':'ready'})
for n in (1,2):send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==1 else 'match','status':'running','reason':''})
PROGRESS
for n in (3,4):send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==4 else 'match','status':'completed','reason':''})
review=json.loads(r'''REVIEW''')
review['request_id']=r['request_id']
FAULT
send({'type':'match','review':review})
EXIT
)PY";
            worker.replace("REVIEW", QJsonDocument(review).toJson(QJsonDocument::Compact));
            worker.replace(
                "PROGRESS",
                fault == 2
                    ? QByteArray("pass")
                    : QByteArray("for phase in ('finding','tracking'):send({'type':'match_progress','phase':phase,'cached':False,'model':'gemini-test'})"));
            worker.replace("FAULT", fault == 3 ? "review['base_revision']=3" : fault == 4 ? "review['operations'][0]['keyframes'][0]['scale']=4" : "pass");
            worker.replace("EXIT", fault == 5 ? "sys.exit(7)" : "pass");
            QTemporaryDir temp;
            fakeWorker(temp.path(), worker);
            Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
            QSignalSpy ready(&client, &Montaz::WorkerClient::matchReady), failed(&client, &Montaz::WorkerClient::failed);
            QVERIFY(client.start(matchSnapshot(),
                                 {{"match", matchSpec()}, {"sources", QJsonObject{{"1", "/private/source1.mp4"}, {"2", "/private/source2.mp4"}}}}));
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 3000);
            QCOMPARE(ready.count(), fault < 2 ? 1 : 0);
            QCOMPARE(failed.count(), fault < 2 ? 0 : 1);
        }
        const auto logs = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/montaz/runs/");
        for (const auto name : QDir(logs).entryList({"*.jsonl"}, QDir::Files)) {
            QFile file(logs + name);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto data = file.readAll();
            QVERIFY(!data.contains("private object explanation"));
            QVERIFY(!data.contains("/private/source"));
            QVERIFY(!data.contains("/9j/2Q=="));
        }
    }
    void stoppingMatchTrackingDiscardsLateResults()
    {
        QTemporaryDir temp;
        fakeWorker(temp.path(), R"PY(import json,sys,time,signal
r=json.loads(sys.stdin.readline())
signal.signal(signal.SIGTERM,lambda *_:None)
def send(value):print(json.dumps({'version':1,**value}),flush=True)
send({'type':'ready'})
for n in (1,2):send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==1 else 'match','status':'running','reason':''})
for phase in ('finding','tracking'):send({'type':'match_progress','phase':phase,'cached':False,'model':'gemini-test'})
time.sleep(10)
)PY");
        Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
        QSignalSpy progress(&client, &Montaz::WorkerClient::eventReceived), ready(&client, &Montaz::WorkerClient::matchReady),
            stopped(&client, &Montaz::WorkerClient::stopped);
        QVERIFY(
            client.start(matchSnapshot(), {{"match", matchSpec()}, {"sources", QJsonObject{{"1", "/private/source1.mp4"}, {"2", "/private/source2.mp4"}}}}));
        QTRY_COMPARE_WITH_TIMEOUT(progress.count(), 4, 3000);
        client.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
        QCOMPARE(ready.count(), 0);
    }
    void stopAndDestructionReachWorkerMediaChildren()
    {
#ifdef Q_OS_UNIX
        // 0: worker exits on SIGTERM; 1: worker ignores SIGTERM until killed; 2: client destroyed while busy.
        for (int mode = 0; mode < 3; ++mode) {
            QTemporaryDir temp;
            auto script = QByteArray(R"PY(import json,signal,subprocess,sys,time
r=json.loads(sys.stdin.readline())
def send(x): print(json.dumps({'version':1,**x}),flush=True)
send({'type':'ready'})
for n in (1,2): send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==1 else 'planner','status':'running','reason':''})
child=subprocess.Popen([sys.executable,'-c','import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(60)'])
open('PIDFILE','w').write(str(child.pid))
MODE
time.sleep(60)
)PY");
            script.replace("PIDFILE", (temp.path() + QStringLiteral("/child.pid")).toUtf8());
            script.replace("MODE", mode == 1 ? "signal.signal(signal.SIGTERM,signal.SIG_IGN)" : "pass");
            fakeWorker(temp.path(), script);
            QFile pidFile(temp.path() + QStringLiteral("/child.pid"));
            {
                Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
                QSignalSpy stopped(&client, &Montaz::WorkerClient::stopped);
                QVERIFY(client.start(snapshot()));
                QTRY_VERIFY_WITH_TIMEOUT(pidFile.exists() && pidFile.size() > 0, 5000);
                if (mode < 2) {
                    client.stop();
                    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
                    QVERIFY(!client.busy());
                }
            }
            QVERIFY(pidFile.open(QIODevice::ReadOnly));
            const int pid = pidFile.readAll().toInt();
            QVERIFY(pid > 0);
            QTRY_VERIFY2_WITH_TIMEOUT(::kill(pid, 0) != 0, qPrintable(QStringLiteral("mode %1 child %2").arg(mode).arg(pid)), 3000);
        }
#else
        QSKIP("Process groups are POSIX-only");
#endif
    }
    void captionsBindAudioSourceNativeIntervalsAndSafeText()
    {
        QVERIFY(Montaz::validateCaptionRequest(captionSnapshot(), captionSpec()).isEmpty());
        QVERIFY(Montaz::validateCaptions(captionSnapshot(), captionSpec(), captionReview()).isEmpty());
        for (const auto &change :
             {QJsonObject{{"instance_id", "missing"}}, QJsonObject{{"start_frame", 99}}, QJsonObject{{"duration_frames", 89}},
              QJsonObject{{"duration_frames", true}}, QJsonObject{{"text", "{override}"}}, QJsonObject{{"text", "back\\slash"}}, QJsonObject{{"text", ""}}}) {
            auto review = captionReview();
            auto caption = review["captions"].toArray()[0].toObject();
            for (auto it = change.begin(); it != change.end(); ++it)
                caption[it.key()] = it.value();
            review["captions"] = QJsonArray{caption};
            QVERIFY(!Montaz::validateCaptions(captionSnapshot(), captionSpec(), review).isEmpty());
        }
        for (const auto &change : {QJsonObject{{"base_revision", 3}}, QJsonObject{{"checked_clips", 0}}, QJsonObject{{"skipped_clips", 1}},
                                   QJsonObject{{"source_sha256", QJsonObject{}}}}) {
            auto review = captionReview();
            for (auto it = change.begin(); it != change.end(); ++it)
                review[it.key()] = it.value();
            QVERIFY(!Montaz::validateCaptions(captionSnapshot(), captionSpec(), review).isEmpty());
        }
        // Outward rounding can add one frame to a six-second fractional-offset segment.
        auto longSnapshot = captionSnapshot();
        auto occupancy = longSnapshot["occupied"].toArray()[0].toObject();
        occupancy["duration_frames"] = 200;
        longSnapshot["occupied"] = QJsonArray{occupancy};
        auto longSpec = captionSpec();
        auto longClip = longSpec["clips"].toArray()[0].toObject();
        longClip["duration_frames"] = 200;
        longSpec["clips"] = QJsonArray{longClip};
        auto longReview = captionReview();
        auto longCaption = longReview["captions"].toArray()[0].toObject();
        longCaption["duration_frames"] = 181;
        longReview["captions"] = QJsonArray{longCaption};
        QVERIFY(Montaz::validateCaptions(longSnapshot, longSpec, longReview).isEmpty());
        longCaption["duration_frames"] = 182;
        longReview["captions"] = QJsonArray{longCaption};
        QVERIFY(!Montaz::validateCaptions(longSnapshot, longSpec, longReview).isEmpty());
        auto spec = captionSpec();
        auto clip = spec["clips"].toArray()[0].toObject();
        clip["audio_stream"] = 64;
        spec["clips"] = QJsonArray{clip};
        QVERIFY(!Montaz::validateCaptionRequest(captionSnapshot(), spec).isEmpty());
        auto review = captionReview();
        review["captions"] = QJsonArray{};
        review["skipped_clips"] = 1;
        QVERIFY(Montaz::validateCaptions(captionSnapshot(), captionSpec(), review).isEmpty());
    }
    void captionsRequireCompleteProgressCleanExitAndDiscardResultsOnStop()
    {
        const QByteArray script = R"PY(import json,sys,signal,time
r=json.loads(sys.stdin.readline())
def send(x): print(json.dumps({'version':1,**x}),flush=True)
send({'type':'ready'})
for n in (1,2): send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==1 else 'captions','status':'running','reason':''})
FAULT
send({'type':'caption_progress','completed':1,'cached':0,'skipped':0,'total':1})
for n in (3,4): send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==4 else 'captions','status':'completed','reason':''})
s=r['snapshot']
p=dict(request_id=r['request_id'],project_id=s['project_id'],sequence_id=s['sequence_id'],base_revision=s['revision'],frame_rate=s['frame_rate'],checked_clips=1,cached_clips=0,skipped_clips=0,source_sha256={'1':'a'*64},captions=[dict(instance_id='spoken',start_frame=102,duration_frames=46,text='Private spoken words')])
CHANGE
send({'type':'captions','review':p})
EXIT
)PY";
        for (int fault = 0; fault < 7; ++fault) {
            auto worker = script;
            worker.replace("FAULT", fault == 6 ? "signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(60)" : "pass");
            worker.replace("CHANGE", fault == 1   ? "p['captions']=[];p['skipped_clips']=1"
                                     : fault == 2 ? "p['base_revision']=3"
                                     : fault == 4 ? "p['cached_clips']=1"
                                                  : "pass");
            if (fault == 1) worker.replace("'skipped':0", "'skipped':1");
            if (fault == 3) worker.replace("'completed':1", "'completed':0");
            worker.replace("EXIT", fault == 5 ? "sys.exit(7)" : "pass");
            QTemporaryDir temp;
            fakeWorker(temp.path(), worker);
            Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
            QSignalSpy ready(&client, &Montaz::WorkerClient::captionsReady), failures(&client, &Montaz::WorkerClient::failed),
                stopped(&client, &Montaz::WorkerClient::stopped);
            QVERIFY(client.start(captionSnapshot(), {{"captions", captionSpec()}, {"sources", QJsonObject{{"1", "/private/speech.wav"}}}}));
            if (fault == 6) {
                QTest::qWait(100);
                client.stop();
            }
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 3000);
            QCOMPARE(ready.count(), fault < 2 ? 1 : 0);
            QCOMPARE(failures.count(), fault >= 2 && fault < 6 ? 1 : 0);
            QCOMPARE(stopped.count(), fault == 6 ? 1 : 0);
        }
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/montaz/runs/");
        for (const auto &name : QDir(directory).entryList({QStringLiteral("*.jsonl")}, QDir::Files)) {
            QFile log(directory + name);
            QVERIFY(log.open(QIODevice::ReadOnly));
            const auto bytes = log.readAll();
            QVERIFY(!bytes.contains("Private spoken words"));
            QVERIFY(!bytes.contains("speech.wav"));
            QVERIFY(!bytes.contains("source_sha256"));
        }
    }
    void noPlacedAudioCompletesRealWorkerWithoutGeminiSettings()
    {
        QTemporaryDir temp;
        const auto oldKey = qgetenv("GEMINI_API_KEY"), oldConfig = qgetenv("MONTAZ_ASSISTANT_CONFIG");
        const auto restore = qScopeGuard([oldKey, oldConfig]() {
            qputenv("GEMINI_API_KEY", oldKey);
            qputenv("MONTAZ_ASSISTANT_CONFIG", oldConfig);
        });
        qputenv("GEMINI_API_KEY", "");
        qputenv("MONTAZ_ASSISTANT_CONFIG", (temp.path() + QStringLiteral("/missing.json")).toUtf8());
        Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), qEnvironmentVariable("MONTAZ_TEST_SOURCE"));
        QSignalSpy ready(&client, &Montaz::WorkerClient::captionsReady), failures(&client, &Montaz::WorkerClient::failed);
        QVERIFY(client.start(snapshot(), {{"captions", QJsonObject{{"clips", QJsonArray{}}}}, {"sources", QJsonObject{}}}));
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 5000);
        QCOMPARE(failures.count(), 0);
        QCOMPARE(ready.count(), 1);
        QVERIFY(ready[0][0].toJsonObject()["captions"].toArray().isEmpty());
    }
    void validAndReplay()
    {
        QVERIFY(Montaz::validateProposal(snapshot(), proposal()).isEmpty());
        QCOMPARE(Montaz::validateProposal(snapshot(), proposal(), {QStringLiteral("request")}), QStringLiteral("already_applied"));
    }
    void timingPreservesSourceIdentityTotalLengthAndFrameBounds()
    {
        QVERIFY(Montaz::validateTimingRequest(timingSnapshot(), timingSpec()).isEmpty());
        QVERIFY(Montaz::validateTimingProposal(timingSnapshot(), timingSpec(), timingProposal()).isEmpty());
        for (const auto &change : {QJsonObject{{"in_frame", 1}}, QJsonObject{{"start_frame", 8}}, QJsonObject{{"duration_frames", 59}},
                                   QJsonObject{{"duration_frames", true}}, QJsonObject{{"start_frame", 2147483648.0}}, QJsonObject{{"bin_id", "2"}}}) {
            auto value = timingProposal();
            auto operations = value["operations"].toArray();
            auto op = operations[0].toObject();
            for (auto it = change.begin(); it != change.end(); ++it)
                op[it.key()] = it.value();
            operations[0] = op;
            value["operations"] = operations;
            QVERIFY(!Montaz::validateTimingProposal(timingSnapshot(), timingSpec(), value).isEmpty());
        }
        for (const auto &change : {QJsonObject{{"music_sha256", QString(64, 'b')}}, QJsonObject{{"base_revision", 3}},
                                   QJsonObject{{"accents", QJsonArray{63, 116}}}, QJsonObject{{"protocol_version", 1}}}) {
            auto value = timingProposal();
            for (auto it = change.begin(); it != change.end(); ++it)
                value[it.key()] = it.value();
            QVERIFY(!Montaz::validateTimingProposal(timingSnapshot(), timingSpec(), value).isEmpty());
        }
    }
    void timingRequiresValidatedResultsAndCleanExitAndSupportsStop()
    {
        const QByteArray script = R"PY(import json,sys,signal,time
r=json.loads(sys.stdin.readline())
def send(x): print(json.dumps({'version':1,**x}),flush=True)
send({'type':'ready'})
FAULT
for n,status in enumerate(('running','running','completed','completed'),1):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n in (1,4) else 'timing','status':status,'reason':''})
s=r['snapshot']; t=r['timing']; ops=t['clips']
for i,o in enumerate(ops):
    o['start_frame']=[0,64,116][i];o['duration_frames']=[64,52,64][i]
p=dict(request_id=r['request_id'],project_id=s['project_id'],sequence_id=s['sequence_id'],base_revision=s['revision'],frame_rate=s['frame_rate'],protocol_version=2,music_sha256=t['music']['sha256'],accents=[64,116],operations=ops)
CHANGE
send({'type':'timing','proposal':p})
EXIT
)PY";
        for (int fault = 0; fault < 5; ++fault) {
            auto worker = script;
            worker.replace("FAULT", fault == 3 ? "signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(60)" : "pass");
            worker.replace("CHANGE", fault == 1   ? "p['operations'][0]['in_frame']=1"
                                     : fault == 4 ? "p['protocol_version']=4;p.pop('accents');p['anchors']=[15,30,45,64,90,116,150];p['analysis']=dict(version="
                                                    "'periodic-onsets-1',mode='beats',bpm=120,periodicity=.9,support=.9,reason='none',cached=True)"
                                                  : "pass");
            worker.replace("EXIT", fault == 2 ? "sys.exit(7)" : "pass");
            QTemporaryDir temp;
            fakeWorker(temp.path(), worker);
            Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
            QSignalSpy ready(&client, &Montaz::WorkerClient::timingReady), failures(&client, &Montaz::WorkerClient::failed),
                stopped(&client, &Montaz::WorkerClient::stopped);
            QVERIFY(client.start(timingSnapshot(), {{"timing", timingSpec()}}));
            if (fault == 3) {
                QTest::qWait(100);
                client.stop();
            }
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 3000);
            QCOMPARE(ready.count(), fault == 0 || fault == 4 ? 1 : 0);
            QCOMPARE(failures.count(), fault == 1 || fault == 2 ? 1 : 0);
            QCOMPARE(stopped.count(), fault == 3 ? 1 : 0);
        }
    }
    void rhythmicTimingRequiresStrictEvidenceAndObservedAnchors()
    {
        auto value = timingProposal();
        value["protocol_version"] = 4;
        value.remove("accents");
        value["anchors"] = QJsonArray{15, 30, 45, 64, 90, 116, 150};
        const QJsonObject analysis{
            {"version", "periodic-onsets-1"}, {"mode", "beats"}, {"bpm", 120}, {"periodicity", 0.9}, {"support", 0.9}, {"reason", "none"}, {"cached", false}};
        value["analysis"] = analysis;
        QVERIFY(Montaz::validateTimingProposal(timingSnapshot(), timingSpec(), value).isEmpty());
        for (const auto &change : {QJsonObject{{"version", "old"}}, QJsonObject{{"mode", "downbeats"}}, QJsonObject{{"bpm", true}}, QJsonObject{{"bpm", 181}},
                                   QJsonObject{{"periodicity", 0.54}}, QJsonObject{{"support", 0.69}}, QJsonObject{{"reason", "weak_periodicity"}},
                                   QJsonObject{{"cached", 0}}, QJsonObject{{"extra", 1}}}) {
            auto malformed = value;
            auto evidence = analysis;
            for (auto it = change.begin(); it != change.end(); ++it)
                evidence[it.key()] = it.value();
            malformed["analysis"] = evidence;
            QVERIFY(!Montaz::validateTimingProposal(timingSnapshot(), timingSpec(), malformed).isEmpty());
        }
        for (const auto &anchors :
             {QJsonArray{15, 30, 45, 63, 90, 116, 150}, QJsonArray{64, 116}, QJsonArray{15, 30, 45, 64, 64, 116}, QJsonArray{15, 30, 45, 64, 116, 180}}) {
            auto malformed = value;
            malformed["anchors"] = anchors;
            QVERIFY(!Montaz::validateTimingProposal(timingSnapshot(), timingSpec(), malformed).isEmpty());
        }
        auto fallback = analysis;
        fallback["mode"] = "accents";
        fallback["bpm"] = fallback["periodicity"] = fallback["support"] = 0;
        fallback["reason"] = "unstable_tempo";
        value["analysis"] = fallback;
        value["anchors"] = QJsonArray{64, 116};
        QVERIFY(Montaz::validateTimingProposal(timingSnapshot(), timingSpec(), value).isEmpty());
        fallback["support"] = 0.9;
        value["analysis"] = fallback;
        QVERIFY(!Montaz::validateTimingProposal(timingSnapshot(), timingSpec(), value).isEmpty());
    }
    void cuesRequireNativeShotBoundsEvidenceIdentityAndSparseAvailableAudio()
    {
        QVERIFY(Montaz::validateCueRequest(cueSnapshot(), cueSpec()).isEmpty());
        QVERIFY(Montaz::validateSoundCues(cueSnapshot(), cueSpec(), cueReview()).isEmpty());
        for (const auto &change : {QJsonObject{{"instance_id", "missing"}}, QJsonObject{{"category", "explosion"}}, QJsonObject{{"start_frame", 1}},
                                   QJsonObject{{"duration_frames", 14}}, QJsonObject{{"duration_frames", true}}, QJsonObject{{"source_sha256", "invalid"}},
                                   QJsonObject{{"reason", ""}}}) {
            auto review = cueReview();
            auto cue = review["cues"].toArray()[0].toObject();
            for (auto it = change.begin(); it != change.end(); ++it)
                cue[it.key()] = it.value();
            review["cues"] = QJsonArray{cue};
            QVERIFY(!Montaz::validateSoundCues(cueSnapshot(), cueSpec(), review).isEmpty());
        }
        auto review = cueReview();
        review["base_revision"] = 3;
        QVERIFY(!Montaz::validateSoundCues(cueSnapshot(), cueSpec(), review).isEmpty());
        review = cueReview();
        review["cached_assets"] = 2;
        QVERIFY(!Montaz::validateSoundCues(cueSnapshot(), cueSpec(), review).isEmpty());
        auto cues = cueReview()["cues"].toArray();
        auto second = cues[0].toObject();
        second["instance_id"] = "shot1";
        second["category"] = "kart_engine";
        second["start_frame"] = 60;
        cues.append(second);
        review = cueReview();
        review["cues"] = cues;
        QVERIFY(!Montaz::validateSoundCues(cueSnapshot(), cueSpec(), review).isEmpty());
        second["instance_id"] = "shot2";
        second["start_frame"] = 120;
        cues[1] = second;
        review["cues"] = cues;
        QVERIFY(Montaz::validateSoundCues(cueSnapshot(), cueSpec(), review).isEmpty());
        QVERIFY(!Montaz::validateSoundCues(timingSnapshot(), cueSpec(), review).isEmpty());
        review["cues"] = QJsonArray{};
        QVERIFY(Montaz::validateSoundCues(timingSnapshot(), cueSpec(), review).isEmpty());
        auto spec = cueSpec();
        auto clips = spec["clips"].toArray();
        clips.removeLast();
        spec["clips"] = clips;
        QVERIFY(!Montaz::validateCueRequest(cueSnapshot(), spec).isEmpty());
    }
    void cuesRequireCompleteCacheProgressCleanExitAndSupportStop()
    {
        const QByteArray script = R"PY(import json,sys,signal,time
r=json.loads(sys.stdin.readline())
def send(x): print(json.dumps({'version':1,**x}),flush=True)
send({'type':'ready'})
for n,status in enumerate(('running','running'),1):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==1 else 'sound_cues','status':status,'reason':''})
FAULT
send({'type':'cue_progress','completed':3,'cached':3,'total':3,'model':'gemini-test'})
for n in (3,4):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':'sound_cues' if n==3 else None,'status':'completed','reason':''})
s=r['snapshot']
p=dict(request_id=r['request_id'],project_id=s['project_id'],sequence_id=s['sequence_id'],base_revision=s['revision'],frame_rate=s['frame_rate'],cached_assets=3,unavailable_assets=0,cues=[dict(instance_id='shot0',category='waves',start_frame=0,duration_frames=60,instruction='Gentle waves',reason='Visible water',source_sha256='a'*64)])
CHANGE
send({'type':'sound_cues','review':p})
EXIT
)PY";
        for (int fault = 0; fault < 7; ++fault) {
            auto worker = script;
            worker.replace("FAULT", fault == 6 ? "signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(60)" : "pass");
            worker.replace("CHANGE", fault == 1   ? "p['cues']=[]"
                                     : fault == 2 ? "p['base_revision']=3"
                                     : fault == 4 ? "p['cached_assets']=2;p['unavailable_assets']=1"
                                                  : "pass");
            if (fault == 3) worker.replace("'completed':3", "'completed':2").replace("'cached':3", "'cached':2");
            worker.replace("EXIT", fault == 5 ? "sys.exit(7)" : "pass");
            QTemporaryDir temp;
            fakeWorker(temp.path(), worker);
            Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
            QSignalSpy ready(&client, &Montaz::WorkerClient::soundCuesReady), failures(&client, &Montaz::WorkerClient::failed),
                stopped(&client, &Montaz::WorkerClient::stopped);
            QVERIFY(client.start(cueSnapshot(), {{"cues", cueSpec()}, {"sources", QJsonObject{{"1", "/one.mov"}, {"2", "/two.mov"}, {"3", "/three.mov"}}}}));
            if (fault == 6) {
                QTest::qWait(100);
                client.stop();
            }
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 3000);
            QCOMPARE(ready.count(), fault < 2 ? 1 : 0);
            QCOMPARE(failures.count(), fault >= 2 && fault < 6 ? 1 : 0);
            QCOMPARE(stopped.count(), fault == 6 ? 1 : 0);
        }
    }
    void musicIsBoundToTheIssuedPathSpanTrackAndRevision()
    {
        QVERIFY(Montaz::validateMusicRequest(musicSnapshot(), musicSpec()).isEmpty());
        QVERIFY(Montaz::validateSoundtrack(musicSnapshot(), musicSpec(), soundtrack()).isEmpty());
        for (const auto &change :
             {QJsonObject{{"path", "/different.wav"}}, QJsonObject{{"track_id", "1"}}, QJsonObject{{"start_frame", 0}}, QJsonObject{{"duration_frames", 119}},
              QJsonObject{{"base_revision", 3}}, QJsonObject{{"cached", 1}}, QJsonObject{{"accents", QJsonArray{60, 15}}},
              QJsonObject{{"accents", QJsonArray{120}}}, QJsonObject{{"sha256", "not-a-hash"}}}) {
            auto value = soundtrack();
            for (auto it = change.begin(); it != change.end(); ++it)
                value[it.key()] = it.value();
            QVERIFY(!Montaz::validateSoundtrack(musicSnapshot(), musicSpec(), value).isEmpty());
        }
        auto editor = musicSnapshot();
        auto tracks = editor["tracks"].toArray();
        auto audio = tracks[1].toObject();
        audio["locked"] = true;
        tracks[1] = audio;
        editor["tracks"] = tracks;
        QVERIFY(!Montaz::validateMusicRequest(editor, musicSpec()).isEmpty());
        editor = musicSnapshot();
        auto occupied = editor["occupied"].toArray();
        occupied.append(QJsonObject{{"instance_id", "sound"}, {"track_id", "2"}, {"start_frame", 139}, {"duration_frames", 1}});
        editor["occupied"] = occupied;
        QVERIFY(!Montaz::validateMusicRequest(editor, musicSpec()).isEmpty());
    }
    void effectIntervalsAreBoundedAndNeverCoverGapsOrOccupiedAudio()
    {
        auto spec = musicSpec();
        spec.remove("recipe");
        spec["start_frame"] = 30;
        spec["duration_frames"] = 60;
        QVERIFY(Montaz::validateEffectRequest(musicSnapshot(), spec).isEmpty());
        auto result = soundtrack();
        result["start_frame"] = 30;
        result["duration_frames"] = 60;
        result["accents"] = QJsonArray{};
        QVERIFY(Montaz::validateSoundtrack(musicSnapshot(), spec, result, true).isEmpty());
        for (const auto &change : {QJsonObject{{"start_frame", 19}}, QJsonObject{{"duration_frames", 14}}, QJsonObject{{"duration_frames", 301}},
                                   QJsonObject{{"instruction", " "}}, QJsonObject{{"track_id", "1"}}, QJsonObject{{"recipe", "postcard"}}}) {
            auto invalid = spec;
            for (auto it = change.begin(); it != change.end(); ++it)
                invalid[it.key()] = it.value();
            QVERIFY(!Montaz::validateEffectRequest(musicSnapshot(), invalid).isEmpty());
        }
        auto occupied = musicSnapshot();
        auto ranges = occupied["occupied"].toArray();
        ranges.append(QJsonObject{{"instance_id", "sound"}, {"track_id", "2"}, {"start_frame", 89}, {"duration_frames", 1}});
        occupied["occupied"] = ranges;
        QVERIFY(!Montaz::validateEffectRequest(occupied, spec).isEmpty());
        result["accents"] = QJsonArray{15};
        QVERIFY(!Montaz::validateSoundtrack(musicSnapshot(), spec, result, true).isEmpty());
        result["accents"] = QJsonArray{};
        result["start_frame"] = 31;
        QVERIFY(!Montaz::validateSoundtrack(musicSnapshot(), spec, result, true).isEmpty());
    }
    void musicRequiresOrderedProgressMatchingResultAndCleanExit()
    {
        const QByteArray script = R"PY(import json,sys,signal,time
r=json.loads(sys.stdin.readline())
def send(value): print(json.dumps({'version':1,**value}),flush=True)
send({'type':'ready'})
for n in (1,2):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==1 else 'music','status':'running','reason':''})
send({'type':'music_progress','phase':'generation','model':'music_v2_5','cached':False})
FAULT
send({'type':'music_progress','phase':'measurement','model':'music_v2_5','cached':False})
for n in (3,4):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==4 else 'music','status':'completed','reason':''})
s=r['snapshot']; spec=r['music']
p={k:spec[k] for k in ('path','track_id','start_frame','duration_frames')}
p.update(request_id=r['request_id'],project_id=s['project_id'],sequence_id=s['sequence_id'],base_revision=s['revision'],frame_rate=s['frame_rate'],sha256='a'*64,accents=[15,60],cached=False)
CHANGE
send({'type':'soundtrack','soundtrack':p})
EXIT
)PY";
        for (const bool effect : {false, true})
            for (int fault = 0; fault < 5; ++fault) {
                QTemporaryDir temp;
                auto worker = script;
                if (effect) {
                    worker.replace("'music'", "'effect'");
                    worker.replace("music_v2_5", "eleven_text_to_sound_v2");
                    worker.replace("accents=[15,60]", "accents=[]");
                }
                worker.replace("FAULT", fault == 3   ? "send({'type':'music_progress','phase':'generation','model':'music_v2_5','cached':False})"
                                        : fault == 4 ? "signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(60)"
                                                     : "pass");
                worker.replace("CHANGE", fault == 1 ? "p['path']='/unissued.wav'" : "pass");
                worker.replace("EXIT", fault == 2 ? "sys.exit(7)" : "pass");
                fakeWorker(temp.path(), worker);
                Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
                QSignalSpy ready(&client, &Montaz::WorkerClient::soundtrackReady), failures(&client, &Montaz::WorkerClient::failed),
                    stopped(&client, &Montaz::WorkerClient::stopped), events(&client, &Montaz::WorkerClient::eventReceived);
                auto spec = musicSpec();
                if (effect) spec.remove("recipe");
                QVERIFY(client.start(musicSnapshot(), {{effect ? "effect" : "music", spec}}));
                if (fault == 4) {
                    QTRY_COMPARE_WITH_TIMEOUT(events.count(), 3, 3000);
                    client.stop();
                }
                QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 3000);
                QCOMPARE(ready.count(), fault == 0 ? 1 : 0);
                QCOMPARE(failures.count(), fault > 0 && fault != 4 ? 1 : 0);
                QCOMPARE(stopped.count(), fault == 4 ? 1 : 0);
                const auto logs = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/montaz/runs/");
                for (const auto &name : QDir(logs).entryList({QStringLiteral("*.jsonl")}, QDir::Files)) {
                    QFile file(logs + name);
                    QVERIFY(file.open(QIODevice::ReadOnly));
                    const auto bytes = file.readAll();
                    QVERIFY(!bytes.contains("project.assets"));
                    QVERIFY(!bytes.contains("private editorial brief"));
                }
            }
    }
    void editorialReviewCannotInventShotsOrMisstateDuration()
    {
        const QJsonObject shot{{"instance_id", "request:0"}, {"role", "detail"}, {"quality", 80}, {"reason", "Visible detail introduces the place"}};
        const QJsonObject review{{"requested_frames", 1800},
                                 {"available_frames", 120},
                                 {"skipped_assets", 2},
                                 {"shots", QJsonArray{shot}},
                                 {"assessment", QJsonObject{{"status", "kept"}, {"reason", "Coherent observed detail"}, {"code", "none"}}}};
        QVERIFY(Montaz::validateEditorialReview(snapshot(), proposal(), review).isEmpty());
        for (const auto &key : {QStringLiteral("requested_frames"), QStringLiteral("available_frames")}) {
            auto invalid = review;
            invalid[key] = 59;
            QVERIFY(!Montaz::validateEditorialReview(snapshot(), proposal(), invalid).isEmpty());
        }
        for (const auto &change : {QJsonObject{{"instance_id", "invented"}}, QJsonObject{{"role", "speech"}}, QJsonObject{{"quality", true}},
                                   QJsonObject{{"quality", 49}}, QJsonObject{{"reason", " "}}, QJsonObject{{"reason", QString(301, 'x')}}}) {
            auto invalid = review;
            auto invalidShot = shot;
            for (auto it = change.begin(); it != change.end(); ++it)
                invalidShot[it.key()] = it.value();
            invalid["shots"] = QJsonArray{invalidShot};
            QVERIFY(!Montaz::validateEditorialReview(snapshot(), proposal(), invalid).isEmpty());
        }
        auto invalid = review;
        invalid["skipped_assets"] = 6;
        QVERIFY(!Montaz::validateEditorialReview(snapshot(), proposal(), invalid).isEmpty());
        for (const auto &assessment :
             {QJsonObject{{"status", "approved"}, {"reason", "Good"}, {"code", "none"}}, QJsonObject{{"status", "kept"}, {"reason", " "}, {"code", "none"}},
              QJsonObject{{"status", "revised"}, {"reason", QString(601, 'x')}, {"code", "none"}},
              QJsonObject{{"status", "unavailable"}, {"reason", "Approved anyway"}, {"code", "provider_timeout"}},
              QJsonObject{{"status", "unavailable"}, {"reason", ""}, {"code", "private arbitrary text"}},
              QJsonObject{{"status", "kept"}, {"reason", "Good"}, {"code", "provider_timeout"}}}) {
            invalid = review;
            invalid["assessment"] = assessment;
            QVERIFY(!Montaz::validateEditorialReview(snapshot(), proposal(), invalid).isEmpty());
        }
        invalid = review;
        invalid["assessment"] = QJsonObject{{"status", "unavailable"}, {"reason", ""}, {"code", "provider_timeout"}};
        QVERIFY(Montaz::validateEditorialReview(snapshot(), proposal(), invalid).isEmpty());
    }
    void batchFailureRollsBackAndNeverCommits()
    {
        int inserted = 0, committed = 0;
        const auto result = Montaz::applyInsertBatch(
            QJsonArray{operation(), operation(), operation()},
            [&inserted](const QJsonObject &, Montaz::UndoFunction &undo, Montaz::UndoFunction &) {
                if (inserted == 2) return false;
                ++inserted;
                const auto previous = undo;
                undo = [&inserted, previous]() {
                    --inserted;
                    return previous();
                };
                return true;
            },
            [&committed](const Montaz::UndoFunction &, const Montaz::UndoFunction &) { ++committed; });
        QCOMPARE(result, QStringLiteral("apply_failed_rolled_back"));
        QCOMPARE(inserted, 0);
        QCOMPARE(committed, 0);
    }
    void successfulBatchCommitsOnceAndRetainsUndoRedo()
    {
        int inserted = 0, committed = 0;
        Montaz::UndoFunction savedUndo, savedRedo;
        const auto result = Montaz::applyInsertBatch(
            QJsonArray{operation(), operation(), operation()},
            [&inserted](const QJsonObject &, Montaz::UndoFunction &undo, Montaz::UndoFunction &redo) {
                ++inserted;
                const auto previousUndo = undo, previousRedo = redo;
                undo = [&inserted, previousUndo]() {
                    --inserted;
                    return previousUndo();
                };
                redo = [&inserted, previousRedo]() {
                    if (!previousRedo()) return false;
                    ++inserted;
                    return true;
                };
                return true;
            },
            [&](const Montaz::UndoFunction &undo, const Montaz::UndoFunction &redo) {
                ++committed;
                savedUndo = undo;
                savedRedo = redo;
            });
        QVERIFY(result.isEmpty());
        QCOMPARE(committed, 1);
        QCOMPARE(inserted, 3);
        QVERIFY(savedUndo());
        QCOMPARE(inserted, 0);
        QVERIFY(savedRedo());
        QCOMPARE(inserted, 3);
    }
    void rollbackFailureIsExplicit()
    {
        int calls = 0;
        const auto result = Montaz::applyInsertBatch(
            QJsonArray{operation(), operation()},
            [&calls](const QJsonObject &, Montaz::UndoFunction &undo, Montaz::UndoFunction &) {
                undo = []() { return false; };
                return ++calls == 1;
            },
            [](const Montaz::UndoFunction &, const Montaz::UndoFunction &) { QFAIL("Failed batch cannot commit"); });
        QCOMPARE(result, QStringLiteral("rollback_failed"));
    }
    void staleAndProfileChanges()
    {
        for (const auto &key : {QStringLiteral("project_id"), QStringLiteral("sequence_id"), QStringLiteral("base_revision"), QStringLiteral("frame_rate")}) {
            auto value = proposal();
            value[key] = key == QStringLiteral("base_revision") ? QJsonValue(3)
                         : key == QStringLiteral("frame_rate")  ? QJsonValue(QJsonObject{{"numerator", 25}, {"denominator", 1}})
                                                                : QJsonValue("other");
            QCOMPARE(Montaz::validateProposal(snapshot(), value), QStringLiteral("stale_proposal"));
        }
    }
    void strictNumericFrames()
    {
        for (const auto &value : {QJsonValue(true), QJsonValue(0.5), QJsonValue(-1), QJsonValue(2147483648.0), QJsonValue("30")}) {
            auto op = operation();
            op["start_frame"] = value;
            auto plan = proposal();
            plan["operations"] = QJsonArray{op};
            QVERIFY(!Montaz::validateProposal(snapshot(), plan).isEmpty());
        }
    }
    void duplicatesAndOverlap()
    {
        auto value = proposal();
        value["operations"] = QJsonArray{operation(), operation()};
        QCOMPARE(Montaz::validateProposal(snapshot(), value), QStringLiteral("duplicate_instance"));
        auto op = operation();
        op["instance_id"] = "second";
        op["start_frame"] = 59;
        value["operations"] = QJsonArray{operation(), op};
        QCOMPARE(Montaz::validateProposal(snapshot(), value), QStringLiteral("overlap"));
        op["start_frame"] = 60;
        value["operations"] = QJsonArray{operation(), op};
        QVERIFY(Montaz::validateProposal(snapshot(), value).isEmpty());
    }
    void sourceBoundsAndExistingOccupancy()
    {
        auto value = proposal();
        auto op = operation();
        op["in_frame"] = 240;
        value["operations"] = QJsonArray{op};
        QVERIFY(Montaz::validateProposal(snapshot(), value).isEmpty());
        op["in_frame"] = 241;
        value["operations"] = QJsonArray{op};
        QCOMPARE(Montaz::validateProposal(snapshot(), value), QStringLiteral("source_bounds"));
        auto editor = snapshot();
        editor["occupied"] = QJsonArray{QJsonObject{{"instance_id", "existing"}, {"track_id", "1"}, {"start_frame", 20}, {"duration_frames", 10}}};
        QCOMPARE(Montaz::validateProposal(editor, proposal()), QStringLiteral("overlap"));
    }
    void lockedSilentAndUnknownSources()
    {
        auto editor = snapshot();
        auto tracks = editor["tracks"].toArray();
        auto track = tracks[0].toObject();
        track["locked"] = true;
        tracks[0] = track;
        editor["tracks"] = tracks;
        QCOMPARE(Montaz::validateProposal(editor, proposal()), QStringLiteral("locked_track"));
        editor = snapshot();
        auto assets = editor["assets"].toArray();
        auto asset = assets[0].toObject();
        asset["has_audio"] = false;
        assets[0] = asset;
        editor["assets"] = assets;
        auto op = operation();
        op["track_id"] = "2";
        auto value = proposal();
        value["operations"] = QJsonArray{op};
        QCOMPARE(Montaz::validateProposal(editor, value), QStringLiteral("missing_stream"));
        op["bin_id"] = "/untrusted/path";
        value["operations"] = QJsonArray{op};
        QCOMPARE(Montaz::validateProposal(editor, value), QStringLiteral("unknown_asset_or_track"));
    }
    void unknownOperationRejected()
    {
        auto value = proposal();
        auto op = operation();
        op["shell"] = "anything";
        value["operations"] = QJsonArray{op};
        QVERIFY(!Montaz::validateProposal(snapshot(), value).isEmpty());
        auto editor = snapshot();
        editor["revision"] = true;
        QVERIFY(!Montaz::validateSnapshot(editor).isEmpty());
    }
    void realPythonWorker()
    {
        Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), qEnvironmentVariable("MONTAZ_TEST_SOURCE"));
        QSignalSpy ready(&client, &Montaz::WorkerClient::proposalReady);
        QSignalSpy failures(&client, &Montaz::WorkerClient::failed);
        QSignalSpy events(&client, &Montaz::WorkerClient::eventReceived);
        QVERIFY(client.start(snapshot()));
        QVERIFY(!client.start(snapshot()));
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 5000);
        QCOMPARE(failures.count(), 0);
        QCOMPARE(ready.count(), 1);
        QCOMPARE(events.count(), 4);
        const auto plan = ready[0][0].toJsonObject();
        QCOMPARE(plan["operations"].toArray().size(), 6);
        QVERIFY(Montaz::validateProposal(snapshot(), plan).isEmpty());
        client.recordApply(plan["request_id"].toString(), {});
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/montaz/runs/");
        QFile log(directory + plan["request_id"].toString() + QStringLiteral(".jsonl"));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const auto bytes = log.readAll();
        QVERIFY(bytes.contains("proposal_ready"));
        QVERIFY(bytes.contains("applied"));
        QVERIFY(!bytes.contains("bin_id"));
        log.close();
        QVERIFY(log.remove());
        QVERIFY(client.start(snapshot()));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 5000);
        QVERIFY(ready[0][0].toJsonObject()["request_id"] != ready[1][0].toJsonObject()["request_id"]);
    }
    void draftRevisionTransportRejectsAppendEscapesShorteningAndLateExit()
    {
        auto context = snapshot();
        context["occupied"] = QJsonArray{QJsonObject{{"instance_id", "10"}, {"track_id", "1"}, {"start_frame", 0}, {"duration_frames", 60}},
                                         QJsonObject{{"instance_id", "20"}, {"track_id", "2"}, {"start_frame", 0}, {"duration_frames", 180}}};
        auto clip = operation();
        clip["instance_id"] = "10";
        const QJsonObject spec{{"clips", QJsonArray{clip}}};
        QJsonObject sources;
        for (int i = 1; i <= 6; ++i)
            sources[QString::number(i)] = QStringLiteral("/private/source%1.mp4").arg(i);
        const QJsonObject brief{{"duration_seconds", 2}, {"recipe", "postcard"}, {"instruction", "private revision instruction"}};
        const QByteArray script = R"PY(import json,sys
r=json.loads(sys.stdin.readline())
assert r['type']=='revise_media' and r['draft']['clips'][0]['instance_id']=='10'
def send(value): print(json.dumps({'version':1,**value}),flush=True)
send({'type':'ready'})
for n in (1,2):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==1 else 'planner','status':'running','reason':''})
for phase in ('analysis','planning','reviewing'):
    send({'type':'progress','phase':phase,'completed':6,'cached':6,'total':6,'model':'gemini-test'})
for n in (3,4):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==4 else 'planner','status':'completed','reason':''})
s=r['snapshot']
p={'request_id':r['request_id'],'project_id':s['project_id'],'sequence_id':s['sequence_id'],
   'base_revision':s['revision'],'frame_rate':s['frame_rate'],'protocol_version':1,
   'operations':[{'instance_id':'new:0','bin_id':'2','track_id':'1','start_frame':0,'in_frame':1,'duration_frames':60}]}
FAULT
review={'requested_frames':60,'available_frames':300,'skipped_assets':5,
        'assessment':{'status':'kept','reason':'Observed alternate opening','code':'none'},
        'shots':[{'instance_id':'new:0','role':'detail','quality':80,'reason':'Observed alternate opening'}]}
send({'type':'proposal','proposal':p,'review':review})
EXIT
)PY";
        for (int fault = 0; fault < 5; ++fault) {
            auto worker = script;
            worker.replace("FAULT", fault == 1   ? "p['operations'][0]['start_frame']=180"
                                    : fault == 2 ? "p['operations'][0]['duration_frames']=59"
                                    : fault == 3 ? "p['operations'][0]['track_id']='2'"
                                                 : "pass");
            worker.replace("EXIT", fault == 4 ? "sys.exit(7)" : "pass");
            QTemporaryDir temp;
            fakeWorker(temp.path(), worker);
            Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
            QSignalSpy ready(&client, &Montaz::WorkerClient::proposalReady), failures(&client, &Montaz::WorkerClient::failed);
            QVERIFY(client.start(context, {{"sources", sources}, {"brief", brief}, {"draft", spec}}));
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 5000);
            QCOMPARE(ready.count(), fault == 0 ? 1 : 0);
            QCOMPARE(failures.count(), fault == 0 ? 0 : 1);
        }
        Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), QStringLiteral("/unavailable"));
        auto stale = spec;
        clip["start_frame"] = 1;
        stale["clips"] = QJsonArray{clip};
        QVERIFY(!client.start(context, {{"sources", sources}, {"brief", brief}, {"draft", stale}}));
    }
    void editorialMetadataRequiresMatchingShotsAndCleanExit()
    {
        const QByteArray script = R"PY(import json,sys
r=json.loads(sys.stdin.readline())
def send(value): print(json.dumps({'version':1,**value}),flush=True)
send({'type':'ready'})
for n,status in enumerate(('running','running','completed','completed'),1):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n in (1,4) else 'planner','status':status,'reason':''})
s=r['snapshot']
p={'request_id':r['request_id'],'project_id':s['project_id'],'sequence_id':s['sequence_id'],
   'base_revision':s['revision'],'frame_rate':s['frame_rate'],'protocol_version':1,
   'operations':[{'instance_id':'one','bin_id':'1','track_id':'1','start_frame':0,'in_frame':0,'duration_frames':60}]}
review={'requested_frames':1800,'available_frames':120,'skipped_assets':2,
        'assessment':{'status':'kept','reason':'private review rationale','code':'none'},
        'shots':[{'instance_id':'one','role':'detail','quality':80,'reason':'private editorial text'}]}
FAULT
send({'type':'proposal','proposal':p,'review':review})
EXIT
)PY";
        for (int fault = 0; fault < 3; ++fault) {
            auto worker = script;
            worker.replace("FAULT", fault == 1 ? "review['shots'][0]['instance_id']='invented'" : "pass");
            worker.replace("EXIT", fault == 2 ? "sys.exit(7)" : "pass");
            QTemporaryDir temp;
            fakeWorker(temp.path(), worker);
            Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
            QSignalSpy ready(&client, &Montaz::WorkerClient::proposalReady), failures(&client, &Montaz::WorkerClient::failed);
            QVERIFY(client.start(snapshot()));
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 3000);
            QCOMPARE(ready.count(), fault == 0 ? 1 : 0);
            QCOMPARE(failures.count(), fault == 0 ? 0 : 1);
            QCOMPARE(client.editorialReview().isEmpty(), fault != 0);
            const auto logs = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/montaz/runs/");
            for (const auto &name : QDir(logs).entryList({QStringLiteral("*.jsonl")}, QDir::Files)) {
                QFile file(logs + name);
                QVERIFY(file.open(QIODevice::ReadOnly));
                QVERIFY(!file.readAll().contains("private editorial text"));
            }
        }
    }
    void draftReviewRequiresOrderedProgressAndExplicitAssessment()
    {
        const QByteArray script = R"PY(import json,sys
r=json.loads(sys.stdin.readline())
def send(value): print(json.dumps({'version':1,**value}),flush=True)
send({'type':'ready'})
for n in (1,2):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==1 else 'planner','status':'running','reason':''})
for phase in PHASES:
    send({'type':'progress','phase':phase,'completed':6,'cached':6,'total':6,'model':'gemini-test'})
for n in (3,4):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==4 else 'planner','status':'completed','reason':''})
s=r['snapshot']
p={'request_id':r['request_id'],'project_id':s['project_id'],'sequence_id':s['sequence_id'],
   'base_revision':s['revision'],'frame_rate':s['frame_rate'],'protocol_version':1,
   'operations':[{'instance_id':'one','bin_id':'1','track_id':'1','start_frame':0,'in_frame':0,'duration_frames':60}]}
review={'requested_frames':1800,'available_frames':120,'skipped_assets':2,
        'assessment':{'status':'revised','reason':'private review explanation','code':'none'},
        'shots':[{'instance_id':'one','role':'detail','quality':80,'reason':'private shot rationale'}]}
FAULT
send({'type':'proposal','proposal':p,'review':review})
EXIT
)PY";
        for (int fault = 0; fault < 7; ++fault) {
            auto worker = script;
            worker.replace("PHASES", fault == 1   ? "('analysis','planning')"
                                     : fault == 2 ? "('analysis','reviewing','planning')"
                                     : fault == 3 ? "('analysis','planning','reviewing','reviewing')"
                                                  : "('analysis','planning','reviewing')");
            worker.replace("FAULT", fault == 4   ? "review.pop('assessment')"
                                    : fault == 5 ? "review['assessment']={'status':'unavailable','reason':'','code':'provider_timeout'}"
                                                 : "pass");
            worker.replace("EXIT", fault == 6 ? "sys.exit(7)" : "pass");
            QTemporaryDir temp;
            fakeWorker(temp.path(), worker);
            Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
            QSignalSpy ready(&client, &Montaz::WorkerClient::proposalReady), failures(&client, &Montaz::WorkerClient::failed);
            QJsonObject sources;
            for (int i = 1; i <= 6; ++i)
                sources[QString::number(i)] = QStringLiteral("/private/footage%1.mp4").arg(i);
            QVERIFY(client.start(snapshot(),
                                 {{"sources", sources}, {"brief", QJsonObject{{"duration_seconds", 60}, {"recipe", "postcard"}, {"instruction", "Travel"}}}}));
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 3000);
            const bool valid = fault == 0 || fault == 5;
            QCOMPARE(ready.count(), valid ? 1 : 0);
            QCOMPARE(failures.count(), valid ? 0 : 1);
            QCOMPARE(client.editorialReview().isEmpty(), !valid);
            const auto logs = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/montaz/runs/");
            for (const auto &name : QDir(logs).entryList({QStringLiteral("*.jsonl")}, QDir::Files)) {
                QFile file(logs + name);
                QVERIFY(file.open(QIODevice::ReadOnly));
                const auto bytes = file.readAll();
                QVERIFY(!bytes.contains("private review explanation"));
                QVERIFY(!bytes.contains("private shot rationale"));
                QVERIFY(!bytes.contains("/private/footage"));
            }
        }
    }
    void stopDuringDraftReviewDiscardsLateResult()
    {
        QTemporaryDir temp;
        fakeWorker(temp.path(), R"PY(import json,sys,time,signal
r=json.loads(sys.stdin.readline())
def send(value): print(json.dumps({'version':1,**value}),flush=True)
signal.signal(signal.SIGTERM,lambda *_: None)
send({'type':'ready'})
for n in (1,2):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==1 else 'planner','status':'running','reason':''})
for phase in ('analysis','planning','reviewing'):
    send({'type':'progress','phase':phase,'completed':6,'cached':6,'total':6,'model':'gemini-test'})
time.sleep(10)
)PY");
        Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
        QSignalSpy events(&client, &Montaz::WorkerClient::eventReceived), ready(&client, &Montaz::WorkerClient::proposalReady),
            stopped(&client, &Montaz::WorkerClient::stopped);
        QJsonObject sources;
        for (int i = 1; i <= 6; ++i)
            sources[QString::number(i)] = QStringLiteral("/private/footage%1.mp4").arg(i);
        QVERIFY(client.start(snapshot(),
                             {{"sources", sources}, {"brief", QJsonObject{{"duration_seconds", 60}, {"recipe", "postcard"}, {"instruction", "Travel"}}}}));
        QTRY_COMPARE_WITH_TIMEOUT(events.count(), 5, 3000);
        client.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
        QVERIFY(!client.busy());
        QCOMPARE(ready.count(), 0);
        QVERIFY(client.editorialReview().isEmpty());
    }
    void missingGeminiKeyIsExplicitAndSourcePathsAreNotLogged()
    {
        QTemporaryDir temp;
        const auto oldKey = qgetenv("GEMINI_API_KEY"), oldConfig = qgetenv("MONTAZ_ASSISTANT_CONFIG");
        const auto restore = qScopeGuard([oldKey, oldConfig]() {
            qputenv("GEMINI_API_KEY", oldKey);
            qputenv("MONTAZ_ASSISTANT_CONFIG", oldConfig);
        });
        qputenv("GEMINI_API_KEY", "");
        qputenv("MONTAZ_ASSISTANT_CONFIG", (temp.path() + QStringLiteral("/empty.json")).toUtf8());
        QJsonObject sources;
        for (int i = 1; i <= 6; ++i)
            sources.insert(QString::number(i), QStringLiteral("/private/never-log-this.mp4"));
        const QJsonObject context{{"sources", sources},
                                  {"brief", QJsonObject{{"duration_seconds", 60}, {"recipe", "postcard"}, {"instruction", "private brief"}}}};
        Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), qEnvironmentVariable("MONTAZ_TEST_SOURCE"));
        QSignalSpy failures(&client, &Montaz::WorkerClient::failed), ready(&client, &Montaz::WorkerClient::proposalReady);
        QVERIFY(client.start(snapshot(), context));
        QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 5000);
        QCOMPARE(failures[0][0].toString(), QStringLiteral("missing_api_key"));
        QCOMPARE(ready.count(), 0);
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/montaz/runs/");
        for (const auto &name : QDir(directory).entryList({QStringLiteral("*.jsonl")}, QDir::Files)) {
            QFile log(directory + name);
            QVERIFY(log.open(QIODevice::ReadOnly));
            const auto bytes = log.readAll();
            QVERIFY(!bytes.contains("never-log-this"));
            QVERIFY(!bytes.contains("private brief"));
        }
    }
    void providerDiagnosesSurviveTransportAndAreLoggedWithoutResponseText()
    {
        for (const auto &code : {QByteArray("provider_request_rejected"), QByteArray("provider_output_limit"), QByteArray("provider_blocked"),
                                 QByteArray("provider_empty_response")}) {
            QTemporaryDir temp;
            fakeWorker(temp.path(), "import json,sys\nr=json.loads(sys.stdin.readline())\n"
                                    "print(json.dumps({'version':1,'type':'ready'}),flush=True)\n"
                                    "print(json.dumps({'version':1,'type':'error','code':'" +
                                        code + "'}),flush=True)\n");
            Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
            QSignalSpy failures(&client, &Montaz::WorkerClient::failed), ready(&client, &Montaz::WorkerClient::proposalReady);
            QVERIFY(client.start(snapshot()));
            QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 3000);
            QCOMPARE(failures[0][0].toString(), QString::fromUtf8(code));
            QCOMPARE(ready.count(), 0);
            QVERIFY(!client.busy());
            const auto directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/montaz/runs/");
            bool recorded = false;
            for (const auto &name : QDir(directory).entryList({QStringLiteral("*.jsonl")}, QDir::Files)) {
                QFile log(directory + name);
                QVERIFY(log.open(QIODevice::ReadOnly));
                recorded |= log.readAll().contains(code);
            }
            QVERIFY(recorded);
        }
    }
    void malformedMediaProgressCannotFinishAPlan()
    {
        const QByteArray script = R"PY(import json,sys
r=json.loads(sys.stdin.readline())
def send(value): print(json.dumps({'version':1,**value}),flush=True)
send({'type':'ready'})
for n in (1,2):
    send({'type':'event','run_id':r['request_id'],'sequence':n,'stage':None if n==1 else 'planner','status':'running','reason':''})
send({'type':'progress','phase':'analysis','completed':0.5,'cached':0,'total':6})
)PY";
        QTemporaryDir temp;
        fakeWorker(temp.path(), script);
        Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
        QSignalSpy failures(&client, &Montaz::WorkerClient::failed), ready(&client, &Montaz::WorkerClient::proposalReady);
        QVERIFY(client.start(snapshot(), {{"sources", QJsonObject{}}, {"brief", QJsonObject{}}}));
        QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 5000);
        QCOMPARE(failures[0][0].toString(), QStringLiteral("invalid_worker_progress"));
        QCOMPARE(ready.count(), 0);
    }
    void stopKillsUncooperativeWorker()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        fakeWorker(
            temp.path(),
            "import signal,time\nsignal.signal(signal.SIGTERM,signal.SIG_IGN)\nprint('{\"version\":1,\"type\":\"ready\"}',flush=True)\ntime.sleep(60)\n");
        Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
        QSignalSpy stopped(&client, &Montaz::WorkerClient::stopped);
        QSignalSpy ready(&client, &Montaz::WorkerClient::proposalReady);
        QSignalSpy failures(&client, &Montaz::WorkerClient::failed);
        QVERIFY(client.start(snapshot()));
        QTest::qWait(200);
        client.stop();
        client.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 2500);
        QCOMPARE(stopped.count(), 1);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(failures.count(), 0);
    }
    void timeoutEndsWorker()
    {
        QTemporaryDir temp;
        fakeWorker(temp.path(), "import time\ntime.sleep(60)\n");
        Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path(), nullptr, 100);
        QSignalSpy failures(&client, &Montaz::WorkerClient::failed);
        QVERIFY(client.start(snapshot()));
        QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 2500);
        QCOMPARE(failures[0][0].toString(), QStringLiteral("worker_timeout"));
        QVERIFY(!client.busy());
    }
    void malformedOversizedAndIncompleteWorkers()
    {
        const QList<QByteArray> scripts{"print('not-json',flush=True)\n", "print('x'*1100000,flush=True)\n",
                                        "print('{\"version\":1,\"type\":\"ready\"}',flush=True)\n"};
        for (const auto &script : scripts) {
            QTemporaryDir temp;
            fakeWorker(temp.path(), script);
            Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
            QSignalSpy failures(&client, &Montaz::WorkerClient::failed);
            QSignalSpy ready(&client, &Montaz::WorkerClient::proposalReady);
            QVERIFY(client.start(snapshot()));
            QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 3000);
            QCOMPARE(ready.count(), 0);
        }
    }
    void failedLaunchCanRetry()
    {
        Montaz::WorkerClient client(QStringLiteral("/nonexistent/montaz-python"), qEnvironmentVariable("MONTAZ_TEST_SOURCE"));
        QSignalSpy failures(&client, &Montaz::WorkerClient::failed);
        for (int i = 1; i <= 2; ++i) {
            QVERIFY(client.start(snapshot()));
            QTRY_COMPARE_WITH_TIMEOUT(failures.count(), i, 1000);
            QVERIFY(!client.busy());
        }
    }
    void invalidProposalOrFailedExitCannotEnableApply()
    {
        const QByteArray script = R"PY(import json,sys
request=json.loads(sys.stdin.readline())
def send(value):
    print(json.dumps({'version':1,**value}),flush=True)
send({'type':'ready'})
for sequence,status in enumerate(('running','running','completed','completed'),1):
    send({'type':'event','run_id':request['request_id'],'sequence':sequence,
          'stage':None if sequence in (1,4) else 'planner','status':status,'reason':''})
s=request['snapshot']
p={'request_id':request['request_id'],'project_id':s['project_id'],'sequence_id':s['sequence_id'],
   'base_revision':s['revision'],'frame_rate':s['frame_rate'],'protocol_version':1,
   'operations':[{'instance_id':'one','bin_id':'1','track_id':'1','start_frame':0,'in_frame':0,'duration_frames':1}]}
FAULT
send({'type':'proposal','proposal':p})
EXIT
)PY";
        for (bool invalid : {false, true}) {
            auto faulty = script;
            faulty.replace("FAULT", invalid ? "p['base_revision']+=1" : "pass");
            faulty.replace("EXIT", invalid ? "pass" : "sys.exit(7)");
            QTemporaryDir temp;
            fakeWorker(temp.path(), faulty);
            Montaz::WorkerClient client(qEnvironmentVariable("MONTAZ_TEST_PYTHON"), temp.path());
            QSignalSpy failures(&client, &Montaz::WorkerClient::failed);
            QSignalSpy ready(&client, &Montaz::WorkerClient::proposalReady);
            QVERIFY(client.start(snapshot()));
            QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 2500);
            QCOMPARE(ready.count(), 0);
        }
    }
};

QTEST_GUILESS_MAIN(ClientTest)
#include "clienttest.moc"
