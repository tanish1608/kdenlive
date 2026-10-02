// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace Montaz {
class WorkerClient : public QObject
{
    Q_OBJECT
public:
    explicit WorkerClient(QString python, QString sourceDirectory, QObject *parent = nullptr, int deadlineMs = 30000);
    ~WorkerClient() override;
    bool start(const QJsonObject &snapshot, const QJsonObject &context = {});
    void stop();
    bool busy() const { return m_busy; }
    QJsonObject editorialReview() const { return m_review; }
    void recordApply(const QString &requestId, const QString &resultCode);
    static constexpr int MaxRunLogs = 200;

Q_SIGNALS:
    void busyChanged(bool busy);
    void matchReady(const QJsonObject &review);
    void captionsReady(const QJsonObject &review);
    void soundCuesReady(const QJsonObject &review);
    void soundtrackReady(const QJsonObject &soundtrack);
    void timingReady(const QJsonObject &proposal);
    void proposalReady(const QJsonObject &proposal);
    void eventReceived(const QJsonObject &event);
    void failed(const QString &code);
    void stopped();

private:
    void readOutput();
    void abort(const QString &code);
    void signalWorker(bool kill);
    void finish(int exitCode, QProcess::ExitStatus exitStatus);
    void record(const QString &type, const QJsonObject &details = {});
    QProcess m_process;
    QTimer m_deadline;
    QTimer m_killTimer;
    QString m_python;
    QString m_sourceDirectory;
    QByteArray m_buffer;
    QJsonObject m_snapshot;
    QJsonObject m_proposal;
    QJsonObject m_review;
    QJsonObject m_musicSpec;
    QJsonObject m_timingSpec;
    int m_matchPhase{0};
    bool m_matchRequest{false};
    QJsonObject m_matchSpec;
    QJsonObject m_matchReview;
    bool m_timingRequest{false};
    QJsonObject m_soundtrack;
    bool m_captionRequest{false};
    QJsonObject m_captionSpec;
    QJsonObject m_captionReview;
    int m_progressSkipped{0};
    bool m_captionProgressReceived{false};
    bool m_cueRequest{false};
    QJsonObject m_cueSpec;
    QJsonObject m_cueReview;
    bool m_musicRequest{false};
    bool m_effectRequest{false};
    int m_musicPhase{0};
    QString m_requestId;
    QString m_failure;
    QString m_logPath;
    int m_sequence{0};
    int m_outputBytes{0};
    int m_defaultDeadline;
    qint64 m_processGroup{0};
    int m_progressCompleted{0};
    int m_progressCached{0};
    bool m_mediaRequest{false};
    QJsonObject m_draftSpec;
    bool m_planningProgressReceived{false};
    bool m_reviewingProgressReceived{false};
    bool m_ready{false};
    bool m_completed{false};
    bool m_busy{false};
    bool m_cancelled{false};
};
} // namespace Montaz
