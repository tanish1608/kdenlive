// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace Montaz {
class EditorAdapter;
class WorkerClient;

class AssistantWidget : public QWidget
{
public:
    explicit AssistantWidget(QWidget *parent = nullptr);

private:
    void clearProposal();
    EditorAdapter *m_adapter;
    WorkerClient *m_worker;
    QLabel *m_status;
    QListWidget *m_preview;
    QPushButton *m_plan;
    QPushButton *m_stop;
    QPushButton *m_apply;
    QJsonObject m_snapshot;
    QJsonObject m_proposal;
};
} // namespace Montaz
