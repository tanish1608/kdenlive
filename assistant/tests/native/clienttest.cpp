// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "editbatch.h"
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

void fakeWorker(const QString &path, const QByteArray &script)
{
    QVERIFY(QDir().mkpath(path + QStringLiteral("/montaz_assistant")));
    QFile file(path + QStringLiteral("/montaz_assistant/worker.py"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(script), script.size());
}
} // namespace

class ClientTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        QCoreApplication::setApplicationName(QStringLiteral("montaz-client-test-%1").arg(QCoreApplication::applicationPid()));
        QStandardPaths::setTestModeEnabled(true);
    }
    void cleanupTestCase() { QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively(); }
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
        for (int fault = 0; fault < 4; ++fault) {
            auto worker = script;
            worker.replace("FAULT", fault == 3 ? "signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(60)" : "pass");
            worker.replace("CHANGE", fault == 1 ? "p['operations'][0]['in_frame']=1" : "pass");
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
            QCOMPARE(ready.count(), fault == 0 ? 1 : 0);
            QCOMPARE(failures.count(), fault > 0 && fault != 3 ? 1 : 0);
            QCOMPARE(stopped.count(), fault == 3 ? 1 : 0);
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
        const QJsonObject review{{"requested_frames", 1800}, {"available_frames", 120}, {"skipped_assets", 2}, {"shots", QJsonArray{shot}}};
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
