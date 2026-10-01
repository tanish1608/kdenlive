// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QComboBox;
class QLineEdit;
class QSpinBox;

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
    QComboBox *m_recipe;
    QLineEdit *m_instruction;
    QSpinBox *m_duration;
    int m_requestedSeconds{0};
    QJsonObject m_snapshot;
    QJsonObject m_proposal;
};
} // namespace Montaz
