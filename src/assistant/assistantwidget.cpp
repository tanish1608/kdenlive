// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "assistantwidget.h"
#include "editoradapter.h"
#include "proposal.h"
#include "workerclient.h"

#include <KLocalizedString>
#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

Montaz::AssistantWidget::AssistantWidget(QWidget *parent)
    : QWidget(parent)
    , m_adapter(new EditorAdapter(this))
{
    const auto python = qEnvironmentVariable("KDENLIVE_ASSISTANT_PYTHON", QStandardPaths::findExecutable(QStringLiteral("python3")));
    m_worker = new WorkerClient(python, QStringLiteral(MONTAZ_ASSISTANT_SOURCE_DIR), this);
    auto layout = new QVBoxLayout(this);
    auto description = new QLabel(
        i18n("Create visual highlights with Gemini. Sampled footage analysis is reused across runs. Review the proposed cuts before applying."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    m_instruction = new QLineEdit(this);
    m_instruction->setPlaceholderText(i18n("Describe the video you want to create…"));
    m_instruction->setMaxLength(2000);
    layout->addWidget(m_instruction);
    auto options = new QHBoxLayout;
    m_recipe = new QComboBox(this);
    m_recipe->addItem(i18n("Travel postcard"), QStringLiteral("postcard"));
    m_recipe->addItem(i18n("Adventure montage"), QStringLiteral("adventure"));
    m_recipe->addItem(i18n("Travel diary"), QStringLiteral("diary"));
    m_duration = new QSpinBox(this);
    m_duration->setRange(1, 600);
    m_duration->setValue(60);
    m_duration->setSuffix(i18n(" seconds"));
    m_duration->setAccessibleName(i18n("Requested duration"));
    options->addWidget(m_recipe);
    options->addWidget(m_duration);
    layout->addLayout(options);
    m_status = new QLabel(i18n("Import videos and open a sequence to begin."), this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    m_preview = new QListWidget(this);
    layout->addWidget(m_preview);
    auto buttons = new QHBoxLayout;
    m_plan = new QPushButton(i18n("Plan edit"), this);
    m_stop = new QPushButton(i18n("Stop"), this);
    m_apply = new QPushButton(i18n("Apply edit"), this);
    m_stop->setEnabled(false);
    m_apply->setEnabled(false);
    buttons->addWidget(m_plan);
    buttons->addWidget(m_stop);
    buttons->addWidget(m_apply);
    layout->addLayout(buttons);
    auto logs = new QPushButton(i18n("Run logs"), this);
    layout->addWidget(logs);
    connect(logs, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/montaz/runs")));
    });
    connect(m_worker, &WorkerClient::busyChanged, this, [this](bool busy) {
        m_plan->setEnabled(!busy);
        m_stop->setEnabled(busy);
        if (busy) m_apply->setEnabled(false);
        m_recipe->setEnabled(!busy);
        m_duration->setEnabled(!busy);
        m_instruction->setEnabled(!busy);
    });
    connect(m_plan, &QPushButton::clicked, this, [this]() {
        clearProposal();
        m_snapshot = m_adapter->snapshot();
        m_requestedSeconds = m_duration->value();
        QJsonObject context;
        if (!qEnvironmentVariableIsSet("MONTAZ_ASSISTANT_DEMO")) {
            const auto sources = m_adapter->mediaSources(m_snapshot);
            if (sources.isEmpty()) {
                m_status->setText(i18n("Import readable video files and unlock a video track before planning."));
                return;
            }
            context = {{QStringLiteral("sources"), sources},
                       {QStringLiteral("brief"), QJsonObject{{QStringLiteral("duration_seconds"), m_requestedSeconds},
                                                             {QStringLiteral("recipe"), m_recipe->currentData().toString()},
                                                             {QStringLiteral("instruction"), m_instruction->text()}}}};
        }
        if (m_snapshot.isEmpty() || !m_worker->start(m_snapshot, context)) {
            m_status->setText(i18n("The active sequence could not be read. Import videos and try again."));
            return;
        }
        m_status->setText(i18n("Analyzing footage…"));
    });
    connect(m_stop, &QPushButton::clicked, m_worker, &WorkerClient::stop);
    connect(m_worker, &WorkerClient::stopped, this, [this]() {
        clearProposal();
        m_status->setText(i18n("Stopped."));
    });
    connect(m_worker, &WorkerClient::failed, this, [this](const QString &code) {
        clearProposal();
        if (code == QStringLiteral("missing_api_key"))
            m_status->setText(i18n("Gemini is not configured. Set GEMINI_API_KEY or the private Montaz assistant settings file."));
        else if (code == QStringLiteral("provider_auth") || code == QStringLiteral("invalid_settings"))
            m_status->setText(i18n("Gemini rejected its configuration. Check the API key and model in your assistant settings."));
        else if (code == QStringLiteral("provider_timeout") || code == QStringLiteral("worker_timeout") || code == QStringLiteral("media_timeout"))
            m_status->setText(i18n("Planning timed out. Completed analysis was retained; try again."));
        else if (code == QStringLiteral("provider_unavailable"))
            m_status->setText(i18n("Gemini is unavailable or rate limited. Completed analysis was retained; try again later."));
        else if (code == QStringLiteral("invalid_analysis") || code == QStringLiteral("invalid_plan") || code == QStringLiteral("invalid_model_response"))
            m_status->setText(i18n("Gemini returned unusable evidence or cuts. No edit was applied. Try a different brief."));
        else
            m_status->setText(i18n("Planning could not finish. Check your source files and unlocked video tracks. No edit was applied."));
    });
    connect(m_worker, &WorkerClient::eventReceived, this, [this](const QJsonObject &event) {
        if (event.value(QStringLiteral("type")) != QStringLiteral("progress")) return;
        m_status->setText(event.value(QStringLiteral("phase")) == QStringLiteral("planning")
                              ? i18n("Planning cuts from the analyzed footage…")
                              : i18n("Analyzed %1 of %2 videos; %3 reused from cache.", event.value(QStringLiteral("completed")).toInt(),
                                     event.value(QStringLiteral("total")).toInt(), event.value(QStringLiteral("cached")).toInt()));
    });
    connect(m_worker, &WorkerClient::proposalReady, this, [this](const QJsonObject &proposal) {
        const auto current = m_adapter->snapshot();
        if (current != m_snapshot || !validateProposal(current, proposal).isEmpty()) {
            clearProposal();
            m_status->setText(i18n("The sequence changed. Request a new preview."));
            return;
        }
        m_proposal = proposal;
        int frames = 0;
        for (const auto value : proposal.value(QStringLiteral("operations")).toArray()) {
            const auto op = value.toObject();
            frames += op.value(QStringLiteral("duration_frames")).toInt();
            m_preview->addItem(i18n("Clip %1 → track %2, frame %3, %4 frames", op.value(QStringLiteral("bin_id")).toString(),
                                    op.value(QStringLiteral("track_id")).toString(), op.value(QStringLiteral("start_frame")).toInt(),
                                    op.value(QStringLiteral("duration_frames")).toInt()));
        }
        m_apply->setEnabled(true);
        const auto rate = current.value(QStringLiteral("frame_rate")).toObject();
        const double seconds = double(frames) * rate.value(QStringLiteral("denominator")).toInt() / rate.value(QStringLiteral("numerator")).toInt();
        m_status->setText(seconds + 0.1 < m_requestedSeconds
                              ? i18n("Preview: %1 seconds of the requested %2. Footage was not stretched or repeated. Apply as one undoable edit.",
                                     QString::number(seconds, 'f', 1), m_requestedSeconds)
                              : i18n("Preview ready. Apply to append these shots as one undoable edit."));
    });
    connect(m_apply, &QPushButton::clicked, this, [this]() {
        const auto proposal = m_proposal;
        if (proposal.isEmpty()) return;
        const auto current = m_adapter->snapshot();
        const auto result = current == m_snapshot ? m_adapter->apply(proposal) : QStringLiteral("stale_proposal");
        m_worker->recordApply(proposal.value(QStringLiteral("request_id")).toString(), result);
        clearProposal();
        m_status->setText(result.isEmpty()                              ? i18n("Edit applied. Use Undo to revert the whole edit.")
                          : result == QStringLiteral("rollback_failed") ? i18n("The edit could not be fully reverted. Reopen the project before trying again.")
                                                                        : i18n("Edit could not be applied. Request a new preview."));
    });
    connect(m_adapter, &EditorAdapter::invalidated, this, [this]() {
        if (!m_proposal.isEmpty() || m_worker->busy()) {
            clearProposal();
            m_worker->stop();
            m_status->setText(i18n("The sequence changed. Request a new preview."));
        }
    });
    auto contextTimer = new QTimer(this);
    contextTimer->setInterval(250);
    connect(contextTimer, &QTimer::timeout, m_adapter, &EditorAdapter::refreshContext);
    contextTimer->start();
    const auto briefChanged = [this]() {
        if (!m_proposal.isEmpty()) {
            clearProposal();
            m_status->setText(i18n("The brief changed. Request a new plan."));
        }
    };
    connect(m_instruction, &QLineEdit::textChanged, this, briefChanged);
    connect(m_recipe, &QComboBox::currentIndexChanged, this, briefChanged);
    connect(m_duration, &QSpinBox::valueChanged, this, briefChanged);
}

void Montaz::AssistantWidget::clearProposal()
{
    m_proposal = {};
    m_apply->setEnabled(false);
    m_preview->clear();
}
