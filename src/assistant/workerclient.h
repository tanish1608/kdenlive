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
    bool start(const QJsonObject &snapshot);
    void stop();
    bool busy() const { return m_busy; }
    void recordApply(const QString &requestId, const QString &resultCode);

Q_SIGNALS:
    void busyChanged(bool busy);
    void proposalReady(const QJsonObject &proposal);
    void eventReceived(const QJsonObject &event);
    void failed(const QString &code);
    void stopped();

private:
    void readOutput();
    void abort(const QString &code);
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
    QString m_requestId;
    QString m_failure;
    QString m_logPath;
    int m_sequence{0};
    int m_outputBytes{0};
    bool m_ready{false};
    bool m_completed{false};
    bool m_busy{false};
    bool m_cancelled{false};
};
} // namespace Montaz
