// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QJsonObject>
#include <QWidget>

class QMediaPlayer;
class QLabel;
class QListWidget;
class QPushButton;
class QComboBox;
class QGroupBox;
class QTreeWidget;
class QSpinBox;

namespace Montaz {
class EditorAdapter;
class WorkerClient;
class ChatView;

class AssistantWidget : public QWidget
{
public:
    explicit AssistantWidget(QWidget *parent = nullptr);

private:
    void clearProposal();
    void composeMusic();
    void refineTiming();
    void planEdit(const QString &message);
    void reply(const QString &message);
    void resetConversation();
    void setTask(int row, const QString &status);
    void finishTasks(const QString &status);
    EditorAdapter *m_adapter;
    WorkerClient *m_worker;
    QLabel *m_status;
    QLabel *m_shotReason;
    QListWidget *m_preview;
    QPushButton *m_music;
    QPushButton *m_align;
    bool m_timingMode{false};
    QJsonObject m_timingSpec;
    QJsonObject m_timingProposal;
    QPushButton *m_listen;
    QMediaPlayer *m_audioPreview;
    bool m_musicMode{false};
    QJsonObject m_musicSpec;
    QJsonObject m_soundtrack;
    QPushButton *m_plan;
    QPushButton *m_stop;
    QPushButton *m_apply;
    QPushButton *m_newBrief;
    QComboBox *m_recipe;
    ChatView *m_chat;
    QGroupBox *m_previewCard;
    QTreeWidget *m_tasks;
    QSpinBox *m_duration;
    QString m_contextKey;
    QString m_brief;
    int m_activeTask{-1};
    bool m_applying{false};
    int m_requestedSeconds{0};
    QJsonObject m_snapshot;
    QJsonObject m_proposal;
};
} // namespace Montaz
