// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "assistantwidget.h"
#include "chatview.h"
#include "editoradapter.h"
#include "proposal.h"
#include "workerclient.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"

#include <KLocalizedString>
#include <QComboBox>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

Montaz::AssistantWidget::AssistantWidget(QWidget *parent)
    : QWidget(parent)
    , m_adapter(new EditorAdapter(this))
{
    setMinimumWidth(320);
    const auto python = qEnvironmentVariable("KDENLIVE_ASSISTANT_PYTHON", QStandardPaths::findExecutable(QStringLiteral("python3")));
    m_worker = new WorkerClient(python, QStringLiteral(MONTAZ_ASSISTANT_SOURCE_DIR), this);
    auto layout = new QVBoxLayout(this);
    auto header = new QHBoxLayout;
    auto title = new QLabel(i18n("Your editing assistant"), this);
    auto font = title->font();
    font.setBold(true);
    title->setFont(font);
    header->addWidget(title, 1);
    m_newBrief = new QPushButton(QIcon::fromTheme(QStringLiteral("document-new")), i18n("New brief"), this);
    header->addWidget(m_newBrief);
    layout->addLayout(header);
    auto options = new QHBoxLayout;
    m_recipe = new QComboBox(this);
    m_recipe->setAccessibleName(i18n("Editing style"));
    m_recipe->addItem(i18n("Travel postcard"), QStringLiteral("postcard"));
    m_recipe->addItem(i18n("Adventure montage"), QStringLiteral("adventure"));
    m_recipe->addItem(i18n("Travel diary"), QStringLiteral("diary"));
    m_duration = new QSpinBox(this);
    m_duration->setRange(1, 600);
    m_duration->setValue(60);
    m_duration->setSuffix(i18n(" seconds"));
    m_duration->setAccessibleName(i18n("Requested duration"));
    options->addWidget(m_recipe, 1);
    options->addWidget(m_duration);
    layout->addLayout(options);
    m_tasks = new QTreeWidget(this);
    m_tasks->setObjectName(QStringLiteral("assistantTasks"));
    m_tasks->setAccessibleName(i18n("Edit plan"));
    m_tasks->setHeaderLabels({i18n("Edit plan"), i18n("Status")});
    m_tasks->setRootIsDecorated(false);
    m_tasks->setSelectionMode(QAbstractItemView::NoSelection);
    m_tasks->setFocusPolicy(Qt::NoFocus);
    m_tasks->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tasks->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (const auto &task : {i18n("Understand footage"), i18n("Arrange the story"), i18n("Review proposed shots"), i18n("Apply to timeline")})
        new QTreeWidgetItem(m_tasks, {task, i18n("Waiting")});
    m_tasks->setFixedHeight(m_tasks->sizeHintForRow(0) * 4 + m_tasks->header()->sizeHint().height() + 8);
    m_tasks->hide();
    layout->addWidget(m_tasks);
    m_previewCard = new QGroupBox(i18n("Proposed shots"), this);
    auto previewLayout = new QVBoxLayout(m_previewCard);
    m_preview = new QListWidget(m_previewCard);
    m_preview->setAccessibleName(i18n("Proposed shots"));
    m_preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_preview->setTextElideMode(Qt::ElideMiddle);
    m_preview->setMaximumHeight(135);
    previewLayout->addWidget(m_preview);
    m_apply = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), i18n("Apply edit"), m_previewCard);
    m_apply->setEnabled(false);
    previewLayout->addWidget(m_apply);
    m_previewCard->hide();
    layout->addWidget(m_previewCard);
    m_chat = new ChatView(this);
    auto composer = m_chat->findChild<QPlainTextEdit *>(QStringLiteral("assistantComposer"));
    composer->setPlaceholderText(i18n("Describe an edit or refine your previous brief…"));
    composer->setAccessibleName(i18n("Message the AI assistant"));
    layout->addWidget(m_chat, 1);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    auto buttons = new QHBoxLayout;
    auto logs = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), i18n("Run logs"), this);
    logs->setFlat(true);
    buttons->addWidget(logs);
    buttons->addStretch();
    m_stop = new QPushButton(QIcon::fromTheme(QStringLiteral("process-stop")), i18n("Stop"), this);
    m_stop->setEnabled(false);
    m_plan = new QPushButton(QIcon::fromTheme(QStringLiteral("mail-send")), i18n("Send"), this);
    buttons->addWidget(m_stop);
    buttons->addWidget(m_plan);
    layout->addLayout(buttons);
    connect(logs, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/montaz/runs")));
    });
    connect(m_worker, &WorkerClient::busyChanged, this, [this](bool busy) {
        m_plan->setEnabled(!busy);
        m_stop->setEnabled(busy);
        m_newBrief->setEnabled(!busy);
        m_chat->setBusy(busy);
        if (busy) m_apply->setEnabled(false);
        m_recipe->setEnabled(!busy);
        m_duration->setEnabled(!busy);
    });
    connect(m_plan, &QPushButton::clicked, m_chat, &ChatView::submit);
    connect(m_chat, &ChatView::submitted, this, &AssistantWidget::planEdit);
    connect(m_chat, &ChatView::inputTooLong, this, [this]() { reply(i18n("Keep each message under 2,000 characters.")); });
    connect(m_newBrief, &QPushButton::clicked, this, [this]() {
        clearProposal();
        resetConversation();
    });
    connect(m_stop, &QPushButton::clicked, m_worker, &WorkerClient::stop);
    connect(m_worker, &WorkerClient::stopped, this, [this]() {
        if (m_activeTask < 0) return;
        clearProposal();
        finishTasks(i18n("Stopped"));
        reply(i18n("Stopped. Completed footage analysis is saved for your next attempt."));
    });
    connect(m_worker, &WorkerClient::failed, this, [this](const QString &code) {
        if (m_activeTask < 0) return;
        clearProposal();
        finishTasks(i18n("Failed"));
        if (code == QStringLiteral("missing_api_key"))
            reply(i18n("Gemini is not configured. Set GEMINI_API_KEY or the private Montaz assistant settings file."));
        else if (code == QStringLiteral("provider_auth") || code == QStringLiteral("invalid_settings"))
            reply(i18n("Gemini rejected its configuration. Check the API key and model in your assistant settings."));
        else if (code == QStringLiteral("provider_timeout") || code == QStringLiteral("worker_timeout") || code == QStringLiteral("media_timeout"))
            reply(i18n("Planning timed out. Completed analysis was retained; try again."));
        else if (code == QStringLiteral("provider_unavailable"))
            reply(i18n("Gemini is unavailable or rate limited. Completed analysis was retained; try again later."));
        else if (code == QStringLiteral("invalid_analysis") || code == QStringLiteral("invalid_plan") || code == QStringLiteral("invalid_model_response"))
            reply(i18n("Gemini returned unusable evidence or cuts. No edit was applied. Try a different brief."));
        else
            reply(i18n("Planning could not finish. Check your source files and unlocked video tracks. No edit was applied."));
    });
    connect(m_worker, &WorkerClient::eventReceived, this, [this](const QJsonObject &event) {
        if (event.value(QStringLiteral("type")) != QStringLiteral("progress")) return;
        if (event.value(QStringLiteral("phase")) == QStringLiteral("planning")) {
            if (m_activeTask == 0) {
                setTask(0, i18n("Done"));
                setTask(1, i18n("Working"));
                m_chat->addMessage(i18n("Assistant"), i18n("Footage understood. I’m arranging the shots into a story."));
            }
            m_activeTask = 1;
            m_status->setText(i18n("Planning cuts…"));
        } else {
            m_status->setText(i18n("Analyzed %1 of %2 videos · %3 cached", event.value(QStringLiteral("completed")).toInt(),
                                   event.value(QStringLiteral("total")).toInt(), event.value(QStringLiteral("cached")).toInt()));
        }
    });
    connect(m_worker, &WorkerClient::proposalReady, this, [this](const QJsonObject &proposal) {
        const auto current = m_adapter->snapshot();
        if (current != m_snapshot || !validateProposal(current, proposal).isEmpty()) {
            clearProposal();
            finishTasks(i18n("Outdated"));
            reply(i18n("The sequence changed. Send a message to request a new plan."));
            return;
        }
        m_proposal = proposal;
        const auto rate = current.value(QStringLiteral("frame_rate")).toObject();
        const double secondsPerFrame = double(rate.value(QStringLiteral("denominator")).toInt()) / rate.value(QStringLiteral("numerator")).toInt();
        int frames = 0;
        int index = 0;
        for (const auto value : proposal.value(QStringLiteral("operations")).toArray()) {
            const auto op = value.toObject();
            const int duration = op.value(QStringLiteral("duration_frames")).toInt();
            frames += duration;
            const auto clip = pCore->projectItemModel()->getClipByBinID(op.value(QStringLiteral("bin_id")).toString());
            const auto name = clip ? clip->clipName() : i18n("Unavailable clip");
            m_preview->addItem(i18n("%1. %2 · %3 s", ++index, name, QString::number(duration * secondsPerFrame, 'f', 1)));
            m_preview->item(index - 1)->setToolTip(i18n("%1\nSource: %2–%3 s · Timeline: %4 s", name,
                                                        QString::number(op.value(QStringLiteral("in_frame")).toInt() * secondsPerFrame, 'f', 2),
                                                        QString::number((op.value(QStringLiteral("in_frame")).toInt() + duration) * secondsPerFrame, 'f', 2),
                                                        QString::number(op.value(QStringLiteral("start_frame")).toInt() * secondsPerFrame, 'f', 2)));
        }
        setTask(0, i18n("Done"));
        setTask(1, i18n("Done"));
        setTask(2, i18n("Ready"));
        m_activeTask = 2;
        m_apply->setEnabled(true);
        m_previewCard->show();
        const double seconds = frames * secondsPerFrame;
        reply(seconds + 0.1 < m_requestedSeconds ? i18n("I’ve proposed %1 shots totaling %2 seconds of your requested %3. I kept the edit shorter rather than "
                                                        "repeating footage. Review the shots above, then apply or send a refinement.",
                                                        index, QString::number(seconds, 'f', 1), m_requestedSeconds)
                                                 : i18n("Your %1-shot plan is ready (%2 seconds). Review the shots above, then apply or send a refinement.",
                                                        index, QString::number(seconds, 'f', 1)));
        m_status->setText(i18n("Ready for review · video only"));
    });
    connect(m_apply, &QPushButton::clicked, this, [this]() {
        const auto proposal = m_proposal;
        if (proposal.isEmpty()) return;
        const auto current = m_adapter->snapshot();
        m_applying = true;
        const auto result = current == m_snapshot ? m_adapter->apply(proposal) : QStringLiteral("stale_proposal");
        m_applying = false;
        m_worker->recordApply(proposal.value(QStringLiteral("request_id")).toString(), result);
        clearProposal();
        setTask(2, i18n("Done"));
        setTask(3, result.isEmpty() ? i18n("Done") : i18n("Failed"));
        m_activeTask = -1;
        reply(result.isEmpty()                              ? i18n("Edit appended to the timeline. Use Undo to revert the whole edit.")
              : result == QStringLiteral("rollback_failed") ? i18n("The edit could not be fully reverted. Reopen the project before trying again.")
                                                            : i18n("Edit could not be applied. Send a message to request a new plan."));
    });
    connect(m_adapter, &EditorAdapter::invalidated, this, [this]() {
        if (m_applying) return;
        if (!m_proposal.isEmpty() || m_worker->busy()) {
            const bool busy = m_worker->busy();
            clearProposal();
            if (busy) m_worker->stop();
            finishTasks(i18n("Outdated"));
            reply(i18n("The sequence changed. Send a message to request a new plan."));
        }
    });
    auto contextTimer = new QTimer(this);
    contextTimer->setInterval(250);
    connect(contextTimer, &QTimer::timeout, this, [this]() {
        const auto key = m_adapter->contextKey();
        if (key != m_contextKey) {
            m_contextKey = key;
            clearProposal();
            resetConversation();
        }
    });
    contextTimer->start();
    const auto briefChanged = [this]() {
        if (!m_proposal.isEmpty()) {
            clearProposal();
            finishTasks(i18n("Outdated"));
            reply(i18n("Editing style or duration changed. Send a message to request a new plan."));
        }
    };
    connect(m_recipe, &QComboBox::currentIndexChanged, this, briefChanged);
    connect(m_duration, &QSpinBox::valueChanged, this, briefChanged);
    resetConversation();
}

void Montaz::AssistantWidget::planEdit(const QString &message)
{
    clearProposal();
    finishTasks(i18n("Superseded"));
    m_tasks->hide();
    m_chat->addMessage(i18n("You"), message);
    const auto brief = m_brief.isEmpty() ? message : m_brief + QStringLiteral("\nRefinement: ") + message;
    if (brief.size() > 2000) {
        m_chat->setBusy(false);
        reply(i18n("This brief has reached its length limit. Choose New brief and describe the complete edit in up to 2,000 characters."));
        return;
    }
    m_snapshot = m_adapter->snapshot();
    m_contextKey = m_adapter->contextKey();
    m_requestedSeconds = m_duration->value();
    QJsonObject context;
    if (!qEnvironmentVariableIsSet("MONTAZ_ASSISTANT_DEMO")) {
        const auto sources = m_adapter->mediaSources(m_snapshot);
        if (sources.isEmpty()) {
            m_chat->setBusy(false);
            reply(i18n("Import readable video files and unlock a video track before planning."));
            return;
        }
        context = {{QStringLiteral("sources"), sources},
                   {QStringLiteral("brief"), QJsonObject{{QStringLiteral("duration_seconds"), m_requestedSeconds},
                                                         {QStringLiteral("recipe"), m_recipe->currentData().toString()},
                                                         {QStringLiteral("instruction"), brief}}}};
    }
    if (m_snapshot.isEmpty() || !m_worker->start(m_snapshot, context)) {
        m_chat->setBusy(false);
        reply(i18n("The active sequence could not be read. Import videos and try again."));
        return;
    }
    m_brief = brief;
    m_tasks->show();
    for (int row = 0; row < 4; ++row)
        setTask(row, i18n("Waiting"));
    m_activeTask = 0;
    setTask(0, i18n("Working"));
    reply(i18n("I’ll analyze your footage, arrange a visual story, and show the proposed shots. Apply appends them as one undoable edit. Audio and captions "
               "are not added yet."));
    m_status->setText(i18n("Analyzing footage…"));
}

void Montaz::AssistantWidget::reply(const QString &message)
{
    m_chat->addMessage(i18n("Assistant"), message);
    m_status->setText(message);
}

void Montaz::AssistantWidget::resetConversation()
{
    m_brief.clear();
    m_activeTask = -1;
    m_tasks->hide();
    m_chat->reset();
    m_chat->setBusy(m_worker->busy());
    m_chat->addMessage(i18n("Assistant"), i18n("What would you like to make? Describe a travel highlight, choose a style and duration, then send. You can "
                                               "refine the brief here before applying."));
    m_status->setText(i18n("Enter to send · Shift+Enter for a new line"));
}

void Montaz::AssistantWidget::setTask(int row, const QString &status)
{
    m_tasks->topLevelItem(row)->setText(1, status);
}

void Montaz::AssistantWidget::finishTasks(const QString &status)
{
    if (m_activeTask < 0) return;
    setTask(m_activeTask, status);
    for (int row = m_activeTask + 1; row < 4; ++row)
        setTask(row, i18n("Not run"));
    m_activeTask = -1;
}

void Montaz::AssistantWidget::clearProposal()
{
    m_proposal = {};
    m_apply->setEnabled(false);
    m_previewCard->hide();
    m_preview->clear();
}
