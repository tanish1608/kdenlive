// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "workerclient.h"
#include "proposal.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>

Montaz::WorkerClient::WorkerClient(QString python, QString sourceDirectory, QObject *parent, int deadlineMs)
    : QObject(parent)
    , m_python(std::move(python))
    , m_sourceDirectory(std::move(sourceDirectory))
{
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(deadlineMs);
    m_killTimer.setSingleShot(true);
    m_killTimer.setInterval(750);
    connect(&m_deadline, &QTimer::timeout, this, [this]() { abort(QStringLiteral("worker_timeout")); });
    connect(&m_killTimer, &QTimer::timeout, this, [this]() {
        if (m_process.state() != QProcess::NotRunning) m_process.kill();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &WorkerClient::readOutput);
    // No provider logs yet. Do not expose arbitrary stderr to the UI or protocol.
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() { m_process.readAllStandardError(); });
    connect(&m_process, &QProcess::finished, this, &WorkerClient::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_busy) {
            m_failure = QStringLiteral("worker_start_failed");
            finish(-1, QProcess::CrashExit);
        }
    });
}

Montaz::WorkerClient::~WorkerClient()
{
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

bool Montaz::WorkerClient::start(const QJsonObject &snapshot)
{
    if (m_busy || !validateSnapshot(snapshot).isEmpty()) return false;
    m_snapshot = snapshot;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const auto logDirectory = cache + QStringLiteral("/montaz/runs");
    m_logPath = !cache.isEmpty() && QDir().mkpath(logDirectory) ? logDirectory + QLatin1Char('/') + m_requestId + QStringLiteral(".jsonl") : QString();
    m_proposal = {};
    m_buffer.clear();
    m_sequence = 0;
    m_outputBytes = 0;
    m_failure.clear();
    m_ready = false;
    m_completed = false;
    m_cancelled = false;
    const QJsonObject request{{QStringLiteral("version"), 1},
                              {QStringLiteral("type"), QStringLiteral("plan_demo")},
                              {QStringLiteral("request_id"), m_requestId},
                              {QStringLiteral("snapshot"), snapshot}};
    const auto line = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (line.size() > MaxMessageBytes) return false;
    auto environment = QProcessEnvironment::systemEnvironment();
    // Use only this checkout's assistant package, not modules from another project.
    environment.insert(QStringLiteral("PYTHONPATH"), m_sourceDirectory);
    environment.insert(QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("1"));
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(m_sourceDirectory);
    m_process.setProgram(m_python);
    m_process.setArguments({QStringLiteral("-u"), QStringLiteral("-m"), QStringLiteral("montaz_assistant.worker")});
    m_busy = true;
    record(QStringLiteral("started"), {{QStringLiteral("assets"), snapshot.value(QStringLiteral("assets")).toArray().size()},
                                       {QStringLiteral("revision"), snapshot.value(QStringLiteral("revision"))}});
    m_deadline.start();
    Q_EMIT busyChanged(true);
    m_process.start();
    m_process.write(line);
    m_process.closeWriteChannel();
    return true;
}

void Montaz::WorkerClient::stop()
{
    if (!m_busy || m_cancelled) return;
    m_cancelled = true;
    m_proposal = {};
    m_deadline.stop();
    m_process.terminate();
    m_killTimer.start();
}

void Montaz::WorkerClient::abort(const QString &code)
{
    if (!m_busy || m_cancelled || !m_failure.isEmpty()) return;
    m_failure = code;
    m_proposal = {};
    m_deadline.stop();
    m_process.terminate();
    m_killTimer.start();
}

void Montaz::WorkerClient::readOutput()
{
    const auto bytes = m_process.readAllStandardOutput();
    if (!m_busy || m_cancelled || !m_failure.isEmpty()) return;
    m_outputBytes += bytes.size();
    if (m_outputBytes > 4 * MaxMessageBytes) {
        abort(QStringLiteral("worker_output_limit"));
        return;
    }
    m_buffer += bytes;
    while (m_buffer.contains('\n')) {
        const int end = m_buffer.indexOf('\n');
        if (end > MaxMessageBytes) {
            abort(QStringLiteral("worker_output_limit"));
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(m_buffer.left(end), &error);
        m_buffer.remove(0, end + 1);
        const auto message = document.object();
        if (error.error != QJsonParseError::NoError || !document.isObject() || message.value(QStringLiteral("version")) != 1) {
            abort(QStringLiteral("invalid_worker_message"));
            return;
        }
        const auto type = message.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("ready") && !m_ready && message.size() == 2) {
            m_ready = true;
        } else if (type == QStringLiteral("event") && m_ready && !m_completed && message.value(QStringLiteral("run_id")) == m_requestId &&
                   message.value(QStringLiteral("sequence")) == m_sequence + 1) {
            ++m_sequence;
            const auto status = message.value(QStringLiteral("status")).toString();
            // This diagnostic worker has exactly one planner stage and four events.
            const QStringList states{QStringLiteral("running"), QStringLiteral("running"), QStringLiteral("completed"), QStringLiteral("completed")};
            const bool runEvent = m_sequence == 1 || m_sequence == 4;
            if (message.size() != 7 || m_sequence > 4 || status != states[m_sequence - 1] ||
                (runEvent ? !message.value(QStringLiteral("stage")).isNull() : message.value(QStringLiteral("stage")) != QStringLiteral("planner")) ||
                !message.value(QStringLiteral("reason")).isString()) {
                abort(QStringLiteral("invalid_worker_event"));
                return;
            }
            m_completed = m_sequence == 4;
            record(QStringLiteral("stage"), {{QStringLiteral("sequence"), m_sequence},
                                             {QStringLiteral("stage"), message.value(QStringLiteral("stage"))},
                                             {QStringLiteral("status"), status}});
            Q_EMIT eventReceived(message);
        } else if (type == QStringLiteral("proposal") && m_ready && m_completed && m_proposal.isEmpty() && message.size() == 3 &&
                   message.value(QStringLiteral("proposal")).isObject()) {
            const auto proposal = message.value(QStringLiteral("proposal")).toObject();
            if (proposal.value(QStringLiteral("request_id")) != m_requestId || !validateProposal(m_snapshot, proposal).isEmpty()) {
                abort(QStringLiteral("invalid_proposal"));
                return;
            }
            m_proposal = proposal;
        } else {
            abort(type == QStringLiteral("error") ? QStringLiteral("worker_rejected_request") : QStringLiteral("invalid_worker_message"));
            return;
        }
    }
    if (m_buffer.size() > MaxMessageBytes) abort(QStringLiteral("worker_output_limit"));
}

void Montaz::WorkerClient::finish(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_busy) return;
    readOutput();
    m_deadline.stop();
    m_killTimer.stop();
    const bool cancelled = m_cancelled;
    QString failure = m_failure;
    const auto proposal = m_proposal;
    if (failure.isEmpty() && (exitCode != 0 || exitStatus != QProcess::NormalExit || proposal.isEmpty() || !m_buffer.isEmpty()))
        failure = QStringLiteral("worker_incomplete");
    m_busy = false;
    m_proposal = {};
    m_buffer.clear();
    record(cancelled            ? QStringLiteral("cancelled")
           : !failure.isEmpty() ? QStringLiteral("failed")
                                : QStringLiteral("proposal_ready"),
           {{QStringLiteral("code"), failure}});
    Q_EMIT busyChanged(false);
    if (cancelled)
        Q_EMIT stopped();
    else if (!failure.isEmpty())
        Q_EMIT failed(failure);
    else
        Q_EMIT proposalReady(proposal);
}

void Montaz::WorkerClient::record(const QString &type, const QJsonObject &details)
{
    if (m_logPath.isEmpty()) return;
    QFile file(m_logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QJsonObject message{{QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                              {QStringLiteral("request_id"), m_requestId},
                              {QStringLiteral("type"), type},
                              {QStringLiteral("details"), details}};
    file.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void Montaz::WorkerClient::recordApply(const QString &requestId, const QString &resultCode)
{
    if (requestId != m_requestId || m_busy) return;
    record(resultCode.isEmpty() ? QStringLiteral("applied") : QStringLiteral("apply_rejected"), {{QStringLiteral("code"), resultCode}});
}
