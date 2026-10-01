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
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

Montaz::WorkerClient::WorkerClient(QString python, QString sourceDirectory, QObject *parent, int deadlineMs)
    : QObject(parent)
    , m_python(std::move(python))
    , m_sourceDirectory(std::move(sourceDirectory))
    , m_defaultDeadline(deadlineMs)
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

bool Montaz::WorkerClient::start(const QJsonObject &snapshot, const QJsonObject &context)
{
    if (m_busy || !validateSnapshot(snapshot).isEmpty()) return false;
    m_snapshot = snapshot;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const auto logDirectory = cache + QStringLiteral("/montaz/runs");
    m_logPath = !cache.isEmpty() && QDir().mkpath(logDirectory) ? logDirectory + QLatin1Char('/') + m_requestId + QStringLiteral(".jsonl") : QString();
    m_proposal = {};
    m_review = {};
    m_soundtrack = {};
    m_buffer.clear();
    m_sequence = 0;
    m_outputBytes = 0;
    m_failure.clear();
    m_ready = false;
    m_completed = false;
    m_cancelled = false;
    m_musicRequest = context.contains(QStringLiteral("music"));
    m_mediaRequest = !context.isEmpty() && !m_musicRequest;
    m_musicPhase = 0;
    m_musicSpec = context.value(QStringLiteral("music")).toObject();
    m_planningProgressReceived = false;
    m_progressCompleted = m_progressCached = 0;
    QJsonObject request{{QStringLiteral("version"), 1},
                        {QStringLiteral("type"), m_musicRequest   ? QStringLiteral("compose_music")
                                                 : m_mediaRequest ? QStringLiteral("plan_media")
                                                                  : QStringLiteral("plan_demo")},
                        {QStringLiteral("request_id"), m_requestId},
                        {QStringLiteral("snapshot"), snapshot}};
    if (m_musicRequest) {
        if (context.size() != 1 || !validateMusicRequest(snapshot, m_musicSpec).isEmpty()) return false;
        request.insert(QStringLiteral("music"), m_musicSpec);
    } else if (m_mediaRequest) {
        if (context.size() != 2 || !context.value(QStringLiteral("sources")).isObject() || !context.value(QStringLiteral("brief")).isObject()) return false;
        request.insert(QStringLiteral("sources"), context.value(QStringLiteral("sources")));
        request.insert(QStringLiteral("brief"), context.value(QStringLiteral("brief")));
    }
    const auto line = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (line.size() > MaxMessageBytes) return false;
    auto environment = QProcessEnvironment::systemEnvironment();
    // Use only this checkout's assistant package, not modules from another project.
    environment.insert(QStringLiteral("PYTHONPATH"), m_sourceDirectory);
    environment.insert(QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("MONTAZ_ANALYSIS_CACHE"), cache + QStringLiteral("/montaz/analysis"));
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(m_sourceDirectory);
    m_process.setProgram(m_python);
    m_process.setArguments({QStringLiteral("-u"), QStringLiteral("-m"), QStringLiteral("montaz_assistant.worker")});
    m_busy = true;
    record(QStringLiteral("started"), {{QStringLiteral("assets"), snapshot.value(QStringLiteral("assets")).toArray().size()},
                                       {QStringLiteral("revision"), snapshot.value(QStringLiteral("revision"))}});
    m_deadline.start(m_mediaRequest || m_musicRequest ? 180000 : m_defaultDeadline);
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
    m_review = {};
    m_soundtrack = {};
    m_deadline.stop();
    m_process.terminate();
    m_killTimer.start();
}

void Montaz::WorkerClient::abort(const QString &code)
{
    if (!m_busy || m_cancelled || !m_failure.isEmpty()) return;
    m_failure = code;
    m_proposal = {};
    m_review = {};
    m_soundtrack = {};
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
            // Each bounded worker request has one stage and four lifecycle events.
            const QStringList states{QStringLiteral("running"), QStringLiteral("running"), QStringLiteral("completed"), QStringLiteral("completed")};
            const bool runEvent = m_sequence == 1 || m_sequence == 4;
            if (message.size() != 7 || m_sequence > 4 || status != states[m_sequence - 1] ||
                (runEvent ? !message.value(QStringLiteral("stage")).isNull()
                          : message.value(QStringLiteral("stage")) != (m_musicRequest ? QStringLiteral("music") : QStringLiteral("planner"))) ||
                !message.value(QStringLiteral("reason")).isString()) {
                abort(QStringLiteral("invalid_worker_event"));
                return;
            }
            if (m_mediaRequest && m_sequence == 4 && !m_planningProgressReceived) {
                abort(QStringLiteral("invalid_worker_event"));
                return;
            }
            if (m_musicRequest && m_sequence == 4 && m_musicPhase != 2) {
                abort(QStringLiteral("invalid_worker_event"));
                return;
            }
            m_completed = m_sequence == 4;
            record(QStringLiteral("stage"), {{QStringLiteral("sequence"), m_sequence},
                                             {QStringLiteral("stage"), message.value(QStringLiteral("stage"))},
                                             {QStringLiteral("status"), status}});
            Q_EMIT eventReceived(message);
        } else if (type == QStringLiteral("progress") && m_mediaRequest && m_ready && m_sequence == 2 && !m_completed) {
            const auto phase = message.value(QStringLiteral("phase")).toString();
            const auto model = message.value(QStringLiteral("model")).toString();
            const int total = message.value(QStringLiteral("total")).toInt(-1);
            const int completed = message.value(QStringLiteral("completed")).toInt(-1);
            const int cached = message.value(QStringLiteral("cached")).toInt(-1);
            int videos = 0;
            for (const auto &value : m_snapshot.value(QStringLiteral("assets")).toArray())
                videos += value.toObject().value(QStringLiteral("has_video")).toBool();
            if (message.size() != 7 || !QRegularExpression(QStringLiteral("^gemini-[a-zA-Z0-9.-]{1,100}$")).match(model).hasMatch() || total != videos ||
                completed < m_progressCompleted || completed > total || cached < m_progressCached || cached > completed ||
                message.value(QStringLiteral("completed")) != completed || message.value(QStringLiteral("cached")) != cached ||
                message.value(QStringLiteral("total")) != total || (phase != QStringLiteral("analysis") && phase != QStringLiteral("planning")) ||
                (phase == QStringLiteral("planning") && completed != total) || (phase == QStringLiteral("analysis") && m_planningProgressReceived)) {
                abort(QStringLiteral("invalid_worker_progress"));
                return;
            }
            m_progressCompleted = completed;
            m_progressCached = cached;
            m_planningProgressReceived = phase == QStringLiteral("planning");
            record(QStringLiteral("progress"), {{QStringLiteral("provider"), QStringLiteral("gemini")},
                                                {QStringLiteral("model"), model},
                                                {QStringLiteral("phase"), phase},
                                                {QStringLiteral("completed"), completed},
                                                {QStringLiteral("cached"), cached},
                                                {QStringLiteral("total"), total}});
            Q_EMIT eventReceived(message);
        } else if (type == QStringLiteral("music_progress") && m_musicRequest && m_ready && m_sequence == 2 && !m_completed) {
            const auto phase = message.value(QStringLiteral("phase")).toString();
            const auto model = message.value(QStringLiteral("model")).toString();
            if (message.size() != 5 || !message.value(QStringLiteral("cached")).isBool() ||
                !QStringList{QStringLiteral("music_v1"), QStringLiteral("music_v2"), QStringLiteral("music_v2_5")}.contains(model) ||
                phase != (m_musicPhase == 0 ? QStringLiteral("generation") : QStringLiteral("measurement")) || m_musicPhase > 1) {
                abort(QStringLiteral("invalid_worker_progress"));
                return;
            }
            ++m_musicPhase;
            record(QStringLiteral("progress"), {{QStringLiteral("provider"), QStringLiteral("elevenlabs")},
                                                {QStringLiteral("model"), model},
                                                {QStringLiteral("phase"), phase},
                                                {QStringLiteral("cached"), message.value(QStringLiteral("cached"))}});
            Q_EMIT eventReceived(message);
        } else if (type == QStringLiteral("error") && m_ready && !m_completed && message.size() == 3) {
            const auto code = message.value(QStringLiteral("code")).toString();
            const QStringList allowed{QStringLiteral("missing_music_key"),
                                      QStringLiteral("music_auth"),
                                      QStringLiteral("music_unavailable"),
                                      QStringLiteral("music_timeout"),
                                      QStringLiteral("music_request_rejected"),
                                      QStringLiteral("invalid_music_audio"),
                                      QStringLiteral("invalid_music_request"),
                                      QStringLiteral("no_audio_track"),
                                      QStringLiteral("missing_api_key"),
                                      QStringLiteral("invalid_settings"),
                                      QStringLiteral("invalid_brief"),
                                      QStringLiteral("provider_auth"),
                                      QStringLiteral("provider_unavailable"),
                                      QStringLiteral("provider_timeout"),
                                      QStringLiteral("invalid_model_response"),
                                      QStringLiteral("invalid_analysis"),
                                      QStringLiteral("invalid_plan"),
                                      QStringLiteral("invalid_sources"),
                                      QStringLiteral("no_video_track"),
                                      QStringLiteral("media_changed"),
                                      QStringLiteral("insufficient_footage"),
                                      QStringLiteral("media_unreadable"),
                                      QStringLiteral("media_timeout"),
                                      QStringLiteral("invalid_request"),
                                      QStringLiteral("cancelled")};
            abort(allowed.contains(code) ? code : QStringLiteral("worker_rejected_request"));
            return;
        } else if (type == QStringLiteral("soundtrack") && m_musicRequest && m_ready && m_completed && m_soundtrack.isEmpty() && message.size() == 3) {
            const auto soundtrack = message.value(QStringLiteral("soundtrack")).toObject();
            if (soundtrack.value(QStringLiteral("request_id")) != m_requestId || !validateSoundtrack(m_snapshot, m_musicSpec, soundtrack).isEmpty()) {
                abort(QStringLiteral("invalid_soundtrack"));
                return;
            }
            m_soundtrack = soundtrack;
            record(QStringLiteral("music_review"), {{QStringLiteral("frames"), soundtrack.value(QStringLiteral("duration_frames"))},
                                                    {QStringLiteral("accents"), soundtrack.value(QStringLiteral("accents")).toArray().size()},
                                                    {QStringLiteral("cached"), soundtrack.value(QStringLiteral("cached"))}});
        } else if (type == QStringLiteral("proposal") && !m_musicRequest && m_ready && m_completed && m_proposal.isEmpty() &&
                   message.size() == (message.contains(QStringLiteral("review")) ? 4 : 3) && message.value(QStringLiteral("proposal")).isObject()) {
            const auto proposal = message.value(QStringLiteral("proposal")).toObject();
            if (proposal.value(QStringLiteral("request_id")) != m_requestId || !validateProposal(m_snapshot, proposal).isEmpty()) {
                abort(QStringLiteral("invalid_proposal"));
                return;
            }
            m_proposal = proposal;
            if (message.contains(QStringLiteral("review"))) {
                if (!message.value(QStringLiteral("review")).isObject() ||
                    !validateEditorialReview(m_snapshot, proposal, message.value(QStringLiteral("review")).toObject()).isEmpty()) {
                    abort(QStringLiteral("invalid_editorial_review"));
                    return;
                }
                m_review = message.value(QStringLiteral("review")).toObject();
                int frames = 0, quality = 0;
                for (const auto value : proposal.value(QStringLiteral("operations")).toArray())
                    frames += value.toObject().value(QStringLiteral("duration_frames")).toInt();
                for (const auto value : m_review.value(QStringLiteral("shots")).toArray())
                    quality += value.toObject().value(QStringLiteral("quality")).toInt();
                record(QStringLiteral("editorial_review"),
                       {{QStringLiteral("shots"), proposal.value(QStringLiteral("operations")).toArray().size()},
                        {QStringLiteral("planned_frames"), frames},
                        {QStringLiteral("requested_frames"), m_review.value(QStringLiteral("requested_frames"))},
                        {QStringLiteral("available_frames"), m_review.value(QStringLiteral("available_frames"))},
                        {QStringLiteral("skipped_assets"), m_review.value(QStringLiteral("skipped_assets"))},
                        {QStringLiteral("mean_model_quality"), quality / m_review.value(QStringLiteral("shots")).toArray().size()}});
            }
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
    QString failure = cancelled ? QString() : m_failure;
    const auto proposal = m_proposal;
    const auto soundtrack = m_soundtrack;
    if (!cancelled && failure.isEmpty() &&
        (exitCode != 0 || exitStatus != QProcess::NormalExit || (m_musicRequest ? soundtrack.isEmpty() : proposal.isEmpty()) || !m_buffer.isEmpty()))
        failure = QStringLiteral("worker_incomplete");
    m_busy = false;
    m_proposal = {};
    m_soundtrack = {};
    if (cancelled || !failure.isEmpty()) m_review = {};
    m_buffer.clear();
    record(cancelled            ? QStringLiteral("cancelled")
           : !failure.isEmpty() ? QStringLiteral("failed")
           : m_musicRequest     ? QStringLiteral("soundtrack_ready")
                                : QStringLiteral("proposal_ready"),
           {{QStringLiteral("code"), failure}});
    Q_EMIT busyChanged(false);
    if (cancelled)
        Q_EMIT stopped();
    else if (!failure.isEmpty())
        Q_EMIT failed(failure);
    else if (m_musicRequest)
        Q_EMIT soundtrackReady(soundtrack);
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
