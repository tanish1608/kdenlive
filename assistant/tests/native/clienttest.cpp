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
