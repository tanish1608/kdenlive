// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "assistantwidget.h"
#include "editoradapter.h"
#include "proposal.h"
#include "workerclient.h"

#include <KLocalizedString>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

Montaz::AssistantWidget::AssistantWidget(QWidget *parent)
    : QWidget(parent)
    , m_adapter(new EditorAdapter(this))
{
    const auto python = qEnvironmentVariable("KDENLIVE_ASSISTANT_PYTHON", QStandardPaths::findExecutable(QStringLiteral("python3")));
    m_worker = new WorkerClient(python, QStringLiteral(MONTAZ_ASSISTANT_SOURCE_DIR), this);
    auto layout = new QVBoxLayout(this);
    auto description =
        new QLabel(i18n("Preview up to six shots from imported videos. This integration preview adds video only; each shot lasts up to two seconds."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    m_status = new QLabel(i18n("Import videos and open a sequence to begin."), this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    m_preview = new QListWidget(this);
    layout->addWidget(m_preview);
    auto buttons = new QHBoxLayout;
    m_plan = new QPushButton(i18n("Preview shots"), this);
    m_stop = new QPushButton(i18n("Stop"), this);
    m_apply = new QPushButton(i18n("Apply edit"), this);
    m_stop->setEnabled(false);
    m_apply->setEnabled(false);
    buttons->addWidget(m_plan);
    buttons->addWidget(m_stop);
    buttons->addWidget(m_apply);
    layout->addLayout(buttons);
    connect(m_worker, &WorkerClient::busyChanged, this, [this](bool busy) {
        m_plan->setEnabled(!busy);
        m_stop->setEnabled(busy);
        if (busy) m_apply->setEnabled(false);
    });
    connect(m_plan, &QPushButton::clicked, this, [this]() {
        clearProposal();
        m_snapshot = m_adapter->snapshot();
        if (m_snapshot.isEmpty() || !m_worker->start(m_snapshot)) {
            m_status->setText(i18n("The active sequence could not be read. Import videos and try again."));
            return;
        }
        m_status->setText(i18n("Preparing a preview…"));
    });
    connect(m_stop, &QPushButton::clicked, m_worker, &WorkerClient::stop);
    connect(m_worker, &WorkerClient::stopped, this, [this]() {
        clearProposal();
        m_status->setText(i18n("Stopped."));
    });
    connect(m_worker, &WorkerClient::failed, this, [this](const QString &code) {
        clearProposal();
        m_status->setText(code == QStringLiteral("worker_timeout")
                              ? i18n("The preview timed out. Try again.")
                              : i18n("Preview could not finish. Check that videos are imported and a video track is unlocked."));
    });
    connect(m_worker, &WorkerClient::proposalReady, this, [this](const QJsonObject &proposal) {
        const auto current = m_adapter->snapshot();
        if (current != m_snapshot || !validateProposal(current, proposal).isEmpty()) {
            clearProposal();
            m_status->setText(i18n("The sequence changed. Request a new preview."));
            return;
        }
        m_proposal = proposal;
        for (const auto value : proposal.value(QStringLiteral("operations")).toArray()) {
            const auto op = value.toObject();
            m_preview->addItem(i18n("Clip %1 → track %2, frame %3, %4 frames", op.value(QStringLiteral("bin_id")).toString(),
                                    op.value(QStringLiteral("track_id")).toString(), op.value(QStringLiteral("start_frame")).toInt(),
                                    op.value(QStringLiteral("duration_frames")).toInt()));
        }
        m_apply->setEnabled(true);
        m_status->setText(i18n("Preview ready. Apply to append these shots as one undoable edit."));
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
}

void Montaz::AssistantWidget::clearProposal()
{
    m_proposal = {};
    m_apply->setEnabled(false);
    m_preview->clear();
}
