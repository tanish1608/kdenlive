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
#include <QAudioOutput>
#include <QComboBox>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

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
    m_music = new QPushButton(QIcon::fromTheme(QStringLiteral("audio-x-generic")), i18n("Add music"), this);
    m_music->setToolTip(i18n("Compose an ElevenLabs instrumental for the current timeline. Save the project first."));
    auto soundActions = new QHBoxLayout;
    soundActions->addWidget(m_music);
    m_effect = new QPushButton(i18n("Add sound"), this);
    m_effect->setToolTip(i18n("Describe a sound at the playhead or suggest sparse sounds from cached footage."));
    soundActions->addWidget(m_effect);
    auto sounds = new QMenu(m_effect);
    sounds->addAction(i18n("Describe a sound"), this, &AssistantWidget::composeEffect);
    sounds->addAction(i18n("Suggest sounds"), this, &AssistantWidget::suggestSounds);
    m_effect->setMenu(sounds);
    m_align = new QPushButton(i18n("Align cuts"), this);
    m_align->setToolTip(i18n("Review small cut shifts to accents in your existing assistant music."));
    soundActions->addWidget(m_align);
    layout->addLayout(soundActions);
    m_captions = new QPushButton(i18n("Captions"), this);
    m_captions->setToolTip(i18n("Recognize placed original audio or change the appearance of existing captions."));
    auto captionsMenu = new QMenu(m_captions);
    captionsMenu->addAction(i18n("Generate captions"), this, &AssistantWidget::generateCaptions);
    captionsMenu->addSeparator();
    captionsMenu->addAction(i18n("Clean caption style"), this, [this]() { changeCaptionStyle(QStringLiteral("clean")); });
    captionsMenu->addAction(i18n("Bold caption style"), this, [this]() { changeCaptionStyle(QStringLiteral("bold")); });
    captionsMenu->addAction(i18n("Original caption styles"), this, [this]() { changeCaptionStyle(QStringLiteral("original")); });
    m_captions->setMenu(captionsMenu);
    layout->addWidget(m_captions);
    connect(m_align, &QPushButton::clicked, this, &AssistantWidget::refineTiming);
    connect(m_music, &QPushButton::clicked, this, &AssistantWidget::composeMusic);
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
    m_shotReason = new QLabel(m_previewCard);
    m_shotReason->setTextFormat(Qt::PlainText);
    m_shotReason->setWordWrap(true);
    previewLayout->addWidget(m_shotReason);
    connect(m_preview, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item) { m_shotReason->setText(item ? item->data(Qt::UserRole).toString() : QString()); });
    m_apply = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), i18n("Apply edit"), m_previewCard);
    m_apply->setEnabled(false);
    m_audioPreview = new QMediaPlayer(this);
    auto audioOutput = new QAudioOutput(this);
    audioOutput->setVolume(0.6);
    m_audioPreview->setAudioOutput(audioOutput);
    m_listen = new QPushButton(i18n("Listen"), m_previewCard);
    m_listen->setAccessibleName(i18n("Preview proposed audio"));
    m_listen->setCheckable(true);
    m_listen->hide();
    previewLayout->addWidget(m_listen);
    connect(m_listen, &QPushButton::toggled, this, [this](bool playing) {
        if (playing)
            m_audioPreview->play();
        else
            m_audioPreview->stop();
        m_listen->setText(playing ? i18n("Stop preview") : i18n("Listen"));
    });
    connect(m_audioPreview, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia) m_listen->setChecked(false);
    });
    connect(m_audioPreview, &QMediaPlayer::errorOccurred, this, [this]() {
        m_listen->setChecked(false);
        m_status->setText(i18n("Audio preview is unavailable. You can inspect the generated track after applying it."));
    });
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
        m_music->setEnabled(!busy);
        m_effect->setEnabled(!busy);
        m_align->setEnabled(!busy);
        m_captions->setEnabled(!busy);
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
        reply(m_captionMode  ? i18n("Stopped. No captions were applied. Completed speech results are kept for reuse.")
              : m_cueMode    ? i18n("Stopped. No sound was generated or applied; your timeline stays unchanged.")
              : m_timingMode ? i18n("Stopped. Your cuts and music stay unchanged.")
              : m_effectMode ? i18n("Stopped. No sound was applied. Completed audio is kept for reuse; ElevenLabs may still finish a submitted request.")
              : m_musicMode  ? i18n("Stopped. No audio was applied. Completed music is kept for reuse; ElevenLabs may still finish a submitted request.")
                             : i18n("Stopped. Completed footage analysis is saved for your next attempt."));
    });
    connect(m_worker, &WorkerClient::failed, this, [this](const QString &code) {
        if (m_activeTask < 0) return;
        clearProposal();
        finishTasks(code == QStringLiteral("no_alignment_needed") ? i18n("No change") : i18n("Failed"));
        if (m_captionMode) {
            if (code == QStringLiteral("missing_api_key"))
                reply(i18n("Gemini is not configured. Set GEMINI_API_KEY or gemini_api_key in the private assistant settings."));
            else if (code == QStringLiteral("provider_auth") || code == QStringLiteral("invalid_settings"))
                reply(i18n("Gemini rejected its configuration. Check your API key and model. No captions were applied."));
            else if (code == QStringLiteral("provider_timeout") || code == QStringLiteral("worker_timeout") || code == QStringLiteral("media_timeout"))
                reply(i18n("Caption recognition timed out. No captions were applied; completed speech results are kept for reuse."));
            else
                reply(i18n("Captions could not be prepared. Check source audio and Run logs. No captions were applied."));
            return;
        }
        if (m_cueMode) {
            if (code == QStringLiteral("analysis_unavailable"))
                reply(i18n("No current cached visual evidence is available. Analyze the footage with the visual planner first. No sound was generated."));
            else if (code == QStringLiteral("missing_api_key"))
                reply(i18n("Gemini is not configured. Set GEMINI_API_KEY or gemini_api_key in the private assistant settings."));
            else if (code == QStringLiteral("provider_auth") || code == QStringLiteral("invalid_settings"))
                reply(i18n("Gemini rejected its configuration. Check the API key and model in your assistant settings. No sound was generated."));
            else if (code == QStringLiteral("provider_timeout") || code == QStringLiteral("worker_timeout") || code == QStringLiteral("media_timeout"))
                reply(i18n("Sound suggestions timed out. No sound was generated or applied. Cached visual evidence remains available."));
            else
                reply(i18n("Sound suggestions could not finish. No sound was generated or applied. Check Run logs before trying again."));
            return;
        }
        if (m_timingMode) {
            reply(code == QStringLiteral("no_alignment_needed")
                      ? i18n("No useful cut shifts fit the current footage and music. Your edit stays unchanged.")
                      : i18n("Timing refinement could not be prepared. Your edit stays unchanged; check the current sequence and music."));
            return;
        }
        if (m_effectMode) {
            if (code == QStringLiteral("missing_effect_key"))
                reply(i18n("ElevenLabs is not configured. Set ELEVENLABS_API_KEY or elevenlabs_api_key in the private assistant settings."));
            else if (code == QStringLiteral("effect_auth"))
                reply(i18n("ElevenLabs rejected access to sound effects. Check the key and its permissions. No sound was applied."));
            else if (code == QStringLiteral("effect_timeout") || code == QStringLiteral("worker_timeout"))
                reply(i18n("Sound generation timed out. No sound was applied; completed audio remains available for reuse."));
            else
                reply(i18n("The sound effect could not be prepared. Your timeline stays unchanged. Check Run logs before retrying."));
            return;
        }
        if (m_musicMode) {
            if (code == QStringLiteral("missing_music_key"))
                reply(i18n("ElevenLabs is not configured. Set ELEVENLABS_API_KEY or elevenlabs_api_key in the private assistant settings."));
            else if (code == QStringLiteral("music_auth"))
                reply(i18n("ElevenLabs rejected access to music. Check the key and its music permissions. No audio was applied."));
            else if (code == QStringLiteral("music_timeout") || code == QStringLiteral("worker_timeout"))
                reply(i18n("Music generation timed out. No audio was applied; completed music remains available for reuse."));
            else if (code == QStringLiteral("invalid_music_audio"))
                reply(i18n("The generated audio failed duration or sound checks. No audio was applied. Completed provider output is kept for a local retry."));
            else
                reply(i18n("Music could not be prepared. Check ElevenLabs availability and credits, then try again. No audio was applied."));
            return;
        }
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
        else if (code == QStringLiteral("insufficient_footage"))
            reply(i18n("No usable video ranges are long enough for this travel edit. Add clearer clips of at least one second. No edit was applied."));
        else
            reply(i18n("Planning could not finish. Check your source files and unlocked video tracks. No edit was applied."));
    });
    connect(m_worker, &WorkerClient::eventReceived, this, [this](const QJsonObject &event) {
        if (event.value(QStringLiteral("type")) == QStringLiteral("caption_progress")) {
            m_status->setText(i18n("Checking speech · %1 of %2 audio clips · %3 cached · %4 without speech", event.value(QStringLiteral("completed")).toInt(),
                                   event.value(QStringLiteral("total")).toInt(), event.value(QStringLiteral("cached")).toInt(),
                                   event.value(QStringLiteral("skipped")).toInt()));
            return;
        }
        if (event.value(QStringLiteral("type")) == QStringLiteral("cue_progress")) {
            const int completed = event.value(QStringLiteral("completed")).toInt(), total = event.value(QStringLiteral("total")).toInt();
            m_status->setText(i18n("Reusing visual evidence · %1 of %2 sources checked", completed, total));
            if (completed == total) {
                setTask(0, i18n("Done"));
                setTask(1, i18n("Working"));
                m_activeTask = 1;
            }
            return;
        }
        if (event.value(QStringLiteral("type")) == QStringLiteral("music_progress")) {
            const bool measurement = event.value(QStringLiteral("phase")) == QStringLiteral("measurement");
            if (measurement) {
                setTask(0, i18n("Done"));
                setTask(1, i18n("Working"));
                m_activeTask = 1;
            }
            m_status->setText(m_effectMode  ? (measurement ? i18n("Checking sound timing…") : i18n("Preparing sound effect…"))
                              : measurement ? i18n("Checking music timing…")
                              : event.value(QStringLiteral("cached")).toBool() ? i18n("Reusing prepared music…")
                                                                               : i18n("Composing instrumental music…"));
            return;
        }
        if (event.value(QStringLiteral("type")) != QStringLiteral("progress")) return;
        if (event.value(QStringLiteral("phase")) == QStringLiteral("planning")) {
            if (m_activeTask == 0) {
                setTask(0, i18n("Done"));
                setTask(1, i18n("Working"));
                m_chat->addMessage(i18n("Assistant"), i18n("Footage understood. I’m arranging the shots into a story."));
            }
            m_activeTask = 1;
            m_status->setText(i18n("Planning cuts…"));
        } else if (event.value(QStringLiteral("phase")) == QStringLiteral("reviewing")) {
            setTask(0, i18n("Done"));
            setTask(1, i18n("Done"));
            setTask(2, i18n("Working"));
            m_activeTask = 2;
            m_status->setText(i18n("Reviewing story and pacing…"));
            m_chat->addMessage(i18n("Assistant"), i18n("I’m checking this draft against the cached observations once. Any revision keeps its total length."));
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
        const auto review = m_worker->editorialReview();
        const auto shotReviews = review.value(QStringLiteral("shots")).toArray();
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
            if (!shotReviews.isEmpty()) {
                const auto shotReview = shotReviews[index - 1].toObject();
                const auto role = shotReview.value(QStringLiteral("role")).toString();
                const auto label = role == QStringLiteral("establishing") ? i18n("Place")
                                   : role == QStringLiteral("detail")     ? i18n("Detail")
                                   : role == QStringLiteral("action")     ? i18n("Activity")
                                   : role == QStringLiteral("closing")    ? i18n("Ending")
                                                                          : i18n("Shot");
                const auto reason = i18n("%1 · %2", label, shotReview.value(QStringLiteral("reason")).toString());
                m_preview->item(index - 1)->setData(Qt::UserRole, reason);
                m_preview->item(index - 1)->setToolTip(m_preview->item(index - 1)->toolTip() + QLatin1Char('\n') + reason);
            }
        }
        m_preview->setCurrentRow(0);
        setTask(0, i18n("Done"));
        setTask(1, i18n("Done"));
        setTask(2, i18n("Ready"));
        m_activeTask = 2;
        m_apply->setEnabled(true);
        m_previewCard->show();
        const double seconds = frames * secondsPerFrame;
        if (!review.isEmpty()) {
            const auto assessment = review.value(QStringLiteral("assessment")).toObject();
            const auto status = assessment.value(QStringLiteral("status")).toString();
            const auto reason = assessment.value(QStringLiteral("reason")).toString();
            if (status == QStringLiteral("unavailable")) {
                setTask(2, i18n("Unavailable"));
                const bool limited = assessment.value(QStringLiteral("code")) == QStringLiteral("review_limit");
                m_chat->addMessage(i18n("Assistant"),
                                   limited
                                       ? i18n("Quality review was skipped because this draft exceeds the review limit. The original draft passed source "
                                              "and timing checks; review its story and pacing yourself before Apply.")
                                       : i18n("Quality review was unavailable. The original draft passed source and timing checks, but its story and pacing "
                                              "were not reviewed. You can inspect it before Apply or try again."));
            } else {
                m_chat->addMessage(i18n("Assistant"), status == QStringLiteral("revised")
                                                          ? i18n("One revision was proposed with the same total length: %1", reason)
                                                          : i18n("The model review kept the draft: %1", reason));
                m_chat->addMessage(i18n("Assistant"), i18n("This review uses sampled observations. Preview the actual cuts to judge the result."));
            }
            m_chat->addMessage(i18n("Assistant"),
                               i18n("Your footage offers up to %1 seconds of distinct usable shots for this style. %2 sources were excluded because they "
                                    "were too short, unsuitable, or duplicates. Select a proposed shot to see why it belongs in the story.",
                                    QString::number(review.value(QStringLiteral("available_frames")).toInt() * secondsPerFrame, 'f', 1),
                                    review.value(QStringLiteral("skipped_assets")).toInt()));
        }
        reply(seconds + 0.1 < m_requestedSeconds ? i18n("I’ve proposed %1 shots totaling %2 seconds of your requested %3. I kept the edit shorter rather than "
                                                        "repeating footage. Review the shots above, then apply or send a refinement.",
                                                        index, QString::number(seconds, 'f', 1), m_requestedSeconds)
                                                 : i18n("Your %1-shot plan is ready (%2 seconds). Review the shots above, then apply or send a refinement.",
                                                        index, QString::number(seconds, 'f', 1)));
        m_status->setText(review.value(QStringLiteral("assessment")).toObject().value(QStringLiteral("status")) == QStringLiteral("unavailable")
                              ? i18n("Draft ready · quality review unavailable")
                              : i18n("Ready for review · video only"));
    });
    connect(m_worker, &WorkerClient::soundtrackReady, this, [this](const QJsonObject &soundtrack) {
        if (m_adapter->snapshot() != m_snapshot || !validateSoundtrack(m_snapshot, m_musicSpec, soundtrack, m_effectMode).isEmpty()) {
            clearProposal();
            finishTasks(i18n("Outdated"));
            reply(i18n("The sequence changed. Request fresh audio for the current edit."));
            return;
        }
        m_soundtrack = soundtrack;
        const auto rate = m_snapshot.value(QStringLiteral("frame_rate")).toObject();
        const double seconds = double(soundtrack.value(QStringLiteral("duration_frames")).toInt()) * rate.value(QStringLiteral("denominator")).toInt() /
                               rate.value(QStringLiteral("numerator")).toInt();
        m_previewCard->setTitle(m_effectMode ? i18n("Proposed sound effect") : i18n("Proposed music"));
        const double start = double(soundtrack.value(QStringLiteral("start_frame")).toInt()) * rate.value(QStringLiteral("denominator")).toInt() /
                             rate.value(QStringLiteral("numerator")).toInt();
        m_preview->addItem(m_effectMode ? i18n("Sound effect · %1 seconds at %2 s", QString::number(seconds, 'f', 2), QString::number(start, 'f', 2))
                                        : i18n("Instrumental · %1 seconds · ElevenLabs", QString::number(seconds, 'f', 2)));
        m_preview->item(0)->setData(
            Qt::UserRole, m_effectMode
                              ? i18n("Proposed sound at the reviewed position, with quiet level and soft edges. Listen before adding it; existing audio and "
                                     "cuts stay unchanged.")
                              : i18n("%1 energy accents detected. Cuts stay unchanged. The track has a gentle opening and ending. Listen before adding it.",
                                     soundtrack.value(QStringLiteral("accents")).toArray().size()));
        m_preview->setCurrentRow(0);
        m_audioPreview->setSource(QUrl::fromLocalFile(soundtrack.value(QStringLiteral("path")).toString()));
        m_listen->show();
        m_apply->setText(m_effectMode ? i18n("Apply sound") : i18n("Apply music"));
        m_apply->setEnabled(true);
        m_previewCard->show();
        setTask(0, i18n("Done"));
        setTask(1, i18n("Done"));
        setTask(2, i18n("Ready"));
        m_activeTask = 2;
        reply(m_effectMode ? i18n("Your sound effect is ready. Listen and review it, then apply at the reviewed position as one undoable edit.")
                           : i18n("Your instrumental is ready. Apply adds it to a free audio track as one undoable edit. Existing audio stays in place."));
    });
    connect(m_worker, &WorkerClient::captionsReady, this, [this](const QJsonObject &review) {
        if (m_adapter->snapshot() != m_snapshot || m_adapter->captionRequest() != m_captionSpec ||
            !validateCaptions(m_snapshot, m_captionSpec, review).isEmpty()) {
            clearProposal();
            finishTasks(i18n("Outdated"));
            reply(i18n("The audio or sequence changed. Request fresh captions."));
            return;
        }
        const auto captions = review.value(QStringLiteral("captions")).toArray();
        if (captions.isEmpty()) {
            clearProposal();
            finishTasks(i18n("No speech"));
            reply(i18n("No speech to caption. Only placed original audio is checked; video-only footage and assistant music or effects do not need captions."));
            return;
        }
        m_captionReview = review;
        const auto rate = m_snapshot.value(QStringLiteral("frame_rate")).toObject();
        const double unit = double(rate.value(QStringLiteral("denominator")).toInt()) / rate.value(QStringLiteral("numerator")).toInt();
        for (const auto v : captions) {
            const auto c = v.toObject();
            m_preview->addItem(
                i18n("%1–%2 s · %3", QString::number(c.value(QStringLiteral("start_frame")).toInt() * unit, 'f', 2),
                     QString::number((c.value(QStringLiteral("start_frame")).toInt() + c.value(QStringLiteral("duration_frames")).toInt()) * unit, 'f', 2),
                     c.value(QStringLiteral("text")).toString()));
            m_preview->item(m_preview->count() - 1)
                ->setData(Qt::UserRole, i18n("%1\nSpeech timing is approximate. Review wording and timing, then refine with Kdenlive’s subtitle editor.",
                                             c.value(QStringLiteral("text")).toString()));
        }
        m_preview->setCurrentRow(0);
        m_previewCard->setTitle(i18n("Proposed captions"));
        m_apply->setText(i18n("Apply captions"));
        m_apply->setEnabled(true);
        m_previewCard->show();
        setTask(0, i18n("Done"));
        setTask(1, i18n("Done"));
        setTask(2, i18n("Ready"));
        m_activeTask = 2;
        reply(i18np("One caption is ready; %2 audio results were reused. Review the exact words and approximate timing. Apply adds editable native subtitles "
                    "as one undoable edit.",
                    "%1 captions are ready; %2 audio results were reused. Review the exact words and approximate timing. Apply adds editable native subtitles "
                    "as one undoable edit.",
                    captions.size(), review.value(QStringLiteral("cached_clips")).toInt()));
    });
    connect(m_worker, &WorkerClient::soundCuesReady, this, [this](const QJsonObject &review) {
        if (m_adapter->snapshot() != m_snapshot || !validateSoundCues(m_snapshot, m_cueSpec, review).isEmpty()) {
            clearProposal();
            finishTasks(i18n("Outdated"));
            reply(i18n("The sequence changed. Request fresh sound suggestions."));
            return;
        }
        const auto cues = review.value(QStringLiteral("cues")).toArray();
        setTask(0, i18n("Done"));
        setTask(1, i18n("Done"));
        if (cues.isEmpty()) {
            clearProposal();
            finishTasks(i18n("No suggestions"));
            reply(i18n("No supported sound opportunities fit the cached footage and free audio space. No sound was generated or applied."));
            return;
        }
        m_cueReview = review;
        const auto rate = m_snapshot.value(QStringLiteral("frame_rate")).toObject();
        const double unit = double(rate.value(QStringLiteral("denominator")).toInt()) / rate.value(QStringLiteral("numerator")).toInt();
        for (const auto v : cues) {
            const auto c = v.toObject();
            m_preview->addItem(i18n("%1 · %2 s at %3 s", c.value(QStringLiteral("category")).toString().replace(QLatin1Char('_'), QLatin1Char(' ')),
                                    QString::number(c.value(QStringLiteral("duration_frames")).toInt() * unit, 'f', 2),
                                    QString::number(c.value(QStringLiteral("start_frame")).toInt() * unit, 'f', 2)));
            m_preview->item(m_preview->count() - 1)
                ->setData(Qt::UserRole, i18n("Proposed foley: %1. Cached visual observation: %2. This is not verified location audio.",
                                             c.value(QStringLiteral("instruction")).toString(), c.value(QStringLiteral("reason")).toString()));
        }
        m_preview->setCurrentRow(0);
        m_previewCard->setTitle(i18n("Suggested sounds"));
        m_apply->setText(i18n("Generate selected sound"));
        m_apply->setEnabled(true);
        m_previewCard->show();
        setTask(2, i18n("Ready"));
        m_activeTask = 2;
        reply(i18n(
            "Reused %1 source analyses; %2 were unavailable. Select a suggestion to generate, then listen before applying. No sound has been generated yet.",
            review.value(QStringLiteral("cached_assets")).toInt(), review.value(QStringLiteral("unavailable_assets")).toInt()));
    });
    connect(m_worker, &WorkerClient::timingReady, this, [this](const QJsonObject &proposal) {
        if (m_adapter->snapshot() != m_snapshot || !validateTimingProposal(m_snapshot, m_timingSpec, proposal).isEmpty()) {
            clearProposal();
            finishTasks(i18n("Outdated"));
            reply(i18n("The sequence changed. Choose Align cuts for a fresh timing review."));
            return;
        }
        m_timingProposal = proposal;
        const auto rate = m_snapshot.value(QStringLiteral("frame_rate")).toObject();
        const double unit = double(rate.value(QStringLiteral("denominator")).toInt()) / rate.value(QStringLiteral("numerator")).toInt();
        const auto originals = m_timingSpec.value(QStringLiteral("clips")).toArray(), operations = proposal.value(QStringLiteral("operations")).toArray();
        for (int i = 1; i < operations.size(); ++i) {
            const int before = originals[i].toObject().value(QStringLiteral("start_frame")).toInt(),
                      after = operations[i].toObject().value(QStringLiteral("start_frame")).toInt();
            if (before == after) continue;
            m_preview->addItem(
                i18n("Cut %1 · %2 → %3 s (%4 frames)", i, QString::number(before * unit, 'f', 2), QString::number(after * unit, 'f', 2), after - before));
            m_preview->item(m_preview->count() - 1)
                ->setData(Qt::UserRole, i18n("Moves to a measured energy accent. Adjacent shot lengths change; footage is not stretched. This is not a "
                                             "verified beat or phrase boundary."));
        }
        m_preview->setCurrentRow(0);
        m_previewCard->setTitle(i18n("Proposed cut timing"));
        m_apply->setText(i18n("Apply timing"));
        m_apply->setEnabled(true);
        m_previewCard->show();
        setTask(0, i18n("Done"));
        setTask(1, i18n("Done"));
        setTask(2, i18n("Ready"));
        m_activeTask = 2;
        reply(i18np("One cut shift is ready for review. Apply changes the existing clips in one undoable edit and keeps the music and total length.",
                    "%1 cut shifts are ready for review. Apply changes the existing clips in one undoable edit and keeps the music and total length.",
                    m_preview->count()));
    });
    connect(m_apply, &QPushButton::clicked, this, [this]() {
        if (!m_captionReview.isEmpty()) {
            const auto review = m_captionReview;
            m_applying = true;
            const auto result = m_adapter->applyCaptions(m_snapshot, m_captionSpec, review);
            m_applying = false;
            m_worker->recordApply(review.value(QStringLiteral("request_id")).toString(), result);
            clearProposal();
            setTask(2, i18n("Done"));
            setTask(3, result.isEmpty() ? i18n("Done") : i18n("Failed"));
            m_activeTask = -1;
            reply(result.isEmpty()
                      ? i18n("Captions added. One Undo removes all added captions. Use Caption style to change their appearance without changing timing.")
                  : result == QStringLiteral("rollback_failed") ? i18n("The caption edit could not be fully reverted. Reopen the project before trying again.")
                                                                : i18n("Captions could not be applied. Request a fresh review."));
            return;
        }
        if (!m_cueReview.isEmpty()) {
            const auto spec = m_adapter->effectFromCue(m_snapshot, m_cueSpec, m_cueReview, m_preview->currentRow());
            if (spec.isEmpty()) {
                clearProposal();
                finishTasks(i18n("Outdated"));
                reply(i18n("The footage or sequence changed, or the source is unavailable. Request fresh sound suggestions."));
                return;
            }
            beginEffect(spec);
            return;
        }
        if (!m_timingProposal.isEmpty()) {
            const auto proposal = m_timingProposal;
            m_applying = true;
            const auto result = m_adapter->applyTiming(m_snapshot, m_timingSpec, proposal);
            m_applying = false;
            m_worker->recordApply(proposal.value(QStringLiteral("request_id")).toString(), result);
            clearProposal();
            setTask(2, i18n("Done"));
            setTask(3, result.isEmpty() ? i18n("Done") : i18n("Failed"));
            m_activeTask = -1;
            reply(result.isEmpty() ? i18n("Cut timing refined. One Undo restores the original cuts. Music and total length stay unchanged.")
                  : result == QStringLiteral("rollback_failed") ? i18n("The refinement could not be fully reverted. Reopen the project before trying again.")
                                                                : i18n("Timing refinement could not be applied. Request a fresh review."));
            return;
        }
        if (!m_soundtrack.isEmpty()) {
            const auto soundtrack = m_soundtrack;
            m_applying = true;
            const auto result = m_adapter->applySoundtrack(m_snapshot, m_musicSpec, soundtrack, m_effectMode);
            m_applying = false;
            m_worker->recordApply(soundtrack.value(QStringLiteral("request_id")).toString(), result);
            clearProposal();
            setTask(2, i18n("Done"));
            setTask(3, result.isEmpty() ? i18n("Done") : i18n("Failed"));
            m_activeTask = -1;
            reply(result.isEmpty() ? i18n("Audio added. One Undo removes the sound and its imported asset.")
                                   : i18n("Audio could not be applied. Reopen the project if rollback failed; otherwise request fresh audio."));
            return;
        }
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
        if (!m_proposal.isEmpty() || !m_soundtrack.isEmpty() || !m_timingProposal.isEmpty() || !m_cueReview.isEmpty() || !m_captionReview.isEmpty() ||
            m_worker->busy()) {
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
        if (!m_proposal.isEmpty() || !m_soundtrack.isEmpty() || !m_timingProposal.isEmpty() || !m_cueReview.isEmpty() || !m_captionReview.isEmpty()) {
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
    m_musicMode = false;
    m_effectMode = false;
    m_cueMode = false;
    m_timingMode = false;
    m_captionMode = false;
    const QStringList taskNames{i18n("Understand footage"), i18n("Arrange the story"), i18n("Check story and pacing"), i18n("Apply to timeline")};
    for (int row = 0; row < 4; ++row)
        m_tasks->topLevelItem(row)->setText(0, taskNames[row]);
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
    reply(i18n("I’ll analyze your footage, arrange a visual story, and show the proposed shots. Apply appends them as one undoable edit. Use Add music after "
               "applying, and Captions when original speech audio is on the timeline."));
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
    m_soundtrack = {};
    m_musicSpec = {};
    m_timingSpec = {};
    m_timingProposal = {};
    m_cueSpec = {};
    m_cueReview = {};
    m_captionSpec = {};
    m_captionReview = {};
    m_listen->setChecked(false);
    m_audioPreview->stop();
    m_audioPreview->setSource({});
    m_listen->hide();
    m_previewCard->setTitle(i18n("Proposed shots"));
    m_apply->setText(i18n("Apply edit"));
    m_apply->setEnabled(false);
    m_previewCard->hide();
    m_preview->clear();
    m_shotReason->clear();
}

void Montaz::AssistantWidget::composeMusic()
{
    clearProposal();
    finishTasks(i18n("Superseded"));
    m_musicMode = true;
    m_effectMode = false;
    m_cueMode = false;
    m_timingMode = false;
    m_captionMode = false;
    m_musicSpec = m_adapter->musicRequest(m_recipe->currentData().toString(), m_brief);
    m_snapshot = m_adapter->snapshot();
    m_contextKey = m_adapter->contextKey();
    if (m_musicSpec.isEmpty()) {
        reply(i18n("Save the project, add 3–600 seconds of video to the timeline, and leave an unlocked audio track free before adding music."));
        return;
    }
    if (!m_worker->start(m_snapshot, {{QStringLiteral("music"), m_musicSpec}})) {
        reply(i18n("Music could not start. Check the active sequence and audio tracks."));
        return;
    }
    const QStringList tasks{i18n("Compose instrumental"), i18n("Check music timing"), i18n("Listen and review"), i18n("Add to timeline")};
    for (int row = 0; row < 4; ++row) {
        m_tasks->topLevelItem(row)->setText(0, tasks[row]);
        setTask(row, i18n("Waiting"));
    }
    m_activeTask = 0;
    setTask(0, i18n("Working"));
    m_tasks->show();
    reply(i18n("I’ll compose an instrumental for the current video using this travel style. Stop cancels local processing; Apply is available after review."));
}

void Montaz::AssistantWidget::refineTiming()
{
    clearProposal();
    finishTasks(i18n("Superseded"));
    m_timingMode = true;
    m_captionMode = false;
    m_musicMode = false;
    m_effectMode = false;
    m_cueMode = false;
    m_timingSpec = m_adapter->timingRequest();
    m_snapshot = m_adapter->snapshot();
    m_contextKey = m_adapter->contextKey();
    if (m_timingSpec.isEmpty()) {
        reply(i18n("Use one continuous video track with at least two shots and assistant music covering the edit. This refinement requires normal-speed, "
                   "ungrouped clips without effects, transitions, subtitles or other audio."));
        return;
    }
    if (!m_worker->start(m_snapshot, {{QStringLiteral("timing"), m_timingSpec}})) {
        reply(i18n("Timing refinement could not start. Check the active sequence."));
        return;
    }
    const QStringList tasks{i18n("Measure music accents"), i18n("Validate cut shifts"), i18n("Review timing changes"), i18n("Apply refinement")};
    for (int row = 0; row < 4; ++row) {
        m_tasks->topLevelItem(row)->setText(0, tasks[row]);
        setTask(row, i18n("Waiting"));
    }
    m_activeTask = 0;
    setTask(0, i18n("Working"));
    m_tasks->show();
    reply(i18n("I’ll measure your existing music and propose small cut shifts. Shot order, source starts, music and total length stay unchanged. No new "
               "generation is needed."));
}

void Montaz::AssistantWidget::composeEffect()
{
    bool accepted = false;
    const QString description =
        QInputDialog::getMultiLineText(this, i18n("Add sound effect"),
                                       i18n("Describe one short sound at the playhead, for example gentle coastal waves. No music or speech."), QString(),
                                       &accepted)
            .trimmed();
    if (!accepted || description.isEmpty()) return;
    if (description.size() > 1000) {
        reply(i18n("Keep the sound description within 1,000 characters."));
        return;
    }
    const double seconds = QInputDialog::getDouble(this, i18n("Sound duration"), i18n("Seconds (the sound must fit within the shot at the playhead):"), 2.0,
                                                   0.5, 10.0, 2, &accepted);
    if (!accepted) return;
    const auto current = m_adapter->snapshot();
    const auto rate = current.value(QStringLiteral("frame_rate")).toObject();
    const double fps = double(rate.value(QStringLiteral("numerator")).toInt()) / rate.value(QStringLiteral("denominator")).toInt(1);
    const int length = qBound(int(std::ceil(fps / 2)), qRound(seconds * fps), int(std::floor(10 * fps)));
    beginEffect(m_adapter->effectRequest(description, length));
}

void Montaz::AssistantWidget::beginEffect(const QJsonObject &spec)
{
    clearProposal();
    finishTasks(i18n("Superseded"));
    m_cueMode = false;
    m_effectMode = true;
    m_musicMode = true;
    m_timingMode = false;
    m_captionMode = false;
    m_musicSpec = spec;
    m_snapshot = m_adapter->snapshot();
    m_contextKey = m_adapter->contextKey();
    if (m_musicSpec.isEmpty()) {
        reply(i18n("Save the project and place the playhead inside a shot with enough remaining time and a free unlocked audio track."));
        return;
    }
    if (!m_worker->start(m_snapshot, {{QStringLiteral("effect"), m_musicSpec}})) {
        reply(i18n("Sound generation could not start. Check the active sequence."));
        return;
    }
    const QStringList tasks{i18n("Generate described sound"), i18n("Check sound timing"), i18n("Listen and review"), i18n("Add to timeline")};
    for (int row = 0; row < 4; ++row) {
        m_tasks->topLevelItem(row)->setText(0, tasks[row]);
        setTask(row, i18n("Waiting"));
    }
    m_activeTask = 0;
    setTask(0, i18n("Working"));
    m_tasks->show();
    reply(i18n("I’ll generate the described sound with ElevenLabs for this position. Listen before applying; existing audio and cuts stay unchanged."));
}

void Montaz::AssistantWidget::suggestSounds()
{
    clearProposal();
    finishTasks(i18n("Superseded"));
    m_cueMode = true;
    m_musicMode = false;
    m_effectMode = false;
    m_timingMode = false;
    m_captionMode = false;
    m_cueSpec = m_adapter->cueRequest();
    m_snapshot = m_adapter->snapshot();
    m_contextKey = m_adapter->contextKey();
    if (m_cueSpec.isEmpty()) {
        reply(
            i18n("Use up to 64 normal-speed video-only shots on one track, without groups, effects or transitions. Audio must be assistant music or effects."));
        return;
    }
    QSet<QString> selected;
    for (const auto v : m_cueSpec.value(QStringLiteral("clips")).toArray())
        selected.insert(v.toObject().value(QStringLiteral("bin_id")).toString());
    const auto allSources = m_adapter->mediaSources(m_snapshot, selected);
    QJsonObject sources;
    for (const auto v : m_cueSpec.value(QStringLiteral("clips")).toArray()) {
        const auto id = v.toObject().value(QStringLiteral("bin_id")).toString();
        sources[id] = allSources.value(id);
    }
    if (!m_worker->start(m_snapshot, {{QStringLiteral("cues"), m_cueSpec}, {QStringLiteral("sources"), sources}})) {
        reply(i18n("Sound suggestions could not start. Check the active sequence and source files."));
        return;
    }
    const QStringList tasks{i18n("Reuse visual evidence"), i18n("Suggest sparse sounds"), i18n("Review sound cues"), i18n("Generate chosen sound")};
    for (int row = 0; row < 4; ++row) {
        m_tasks->topLevelItem(row)->setText(0, tasks[row]);
        setTask(row, i18n("Waiting"));
    }
    m_activeTask = 0;
    setTask(0, i18n("Working"));
    m_tasks->show();
    reply(i18n("I’ll reuse cached observations for the selected footage and ask Gemini for a few sound opportunities. Missing analyses are skipped; no footage "
               "is reanalyzed and no sound is generated yet."));
}

void Montaz::AssistantWidget::generateCaptions()
{
    clearProposal();
    finishTasks(i18n("Superseded"));
    m_captionMode = true;
    m_cueMode = m_musicMode = m_effectMode = m_timingMode = false;
    m_captionSpec = m_adapter->captionRequest();
    m_snapshot = m_adapter->snapshot();
    m_contextKey = m_adapter->contextKey();
    if (m_captionSpec.isEmpty()) {
        reply(i18n("Start with an empty, unlocked, enabled subtitle track and up to eight non-overlapping original audio clips, each at most 30 seconds and "
                   "120 seconds in total. Audio must have normal speed and no clip or track effects or transitions."));
        return;
    }
    QSet<QString> selected;
    for (const auto v : m_captionSpec.value(QStringLiteral("clips")).toArray())
        selected.insert(v.toObject().value(QStringLiteral("bin_id")).toString());
    const auto sources = m_adapter->audioSources(m_snapshot, selected);
    if (sources.size() != selected.size() ||
        !m_worker->start(m_snapshot, {{QStringLiteral("captions"), m_captionSpec}, {QStringLiteral("sources"), sources}})) {
        reply(i18n("Captions could not start. Check the placed audio and source files."));
        return;
    }
    const QStringList tasks{i18n("Check placed audio"), i18n("Recognize speech"), i18n("Review captions"), i18n("Apply captions")};
    for (int row = 0; row < 4; ++row) {
        m_tasks->topLevelItem(row)->setText(0, tasks[row]);
        setTask(row, i18n("Waiting"));
    }
    m_activeTask = 0;
    setTask(0, i18n("Working"));
    m_tasks->show();
    reply(i18n("I’ll check placed original audio for speech. Silent clips finish locally; other eligible audio is sent to Gemini. Captions appear for review "
               "before anything is added."));
}

void Montaz::AssistantWidget::changeCaptionStyle(const QString &preset)
{
    if (m_worker->busy()) return;
    clearProposal();
    finishTasks(i18n("Superseded"));
    m_applying = true;
    const auto result = m_adapter->captionStyle(preset);
    m_applying = false;
    reply(result.isEmpty() ? i18n("Caption appearance updated. Text and timing are preserved; Undo restores the previous appearance. Original restores each "
                                  "caption’s saved style.")
                           : i18n("Add captions and enable and unlock their track before changing the style."));
}
