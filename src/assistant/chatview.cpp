// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "chatview.h"

#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBrowser>
#include <QVBoxLayout>

Montaz::ChatView::ChatView(QWidget *parent)
    : QWidget(parent)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_history = new QTextBrowser(this);
    m_history->setObjectName(QStringLiteral("assistantConversation"));
    m_history->setOpenLinks(false);
    m_history->setOpenExternalLinks(false);
    m_history->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_history, 1);
    m_composer = new QPlainTextEdit(this);
    m_composer->setObjectName(QStringLiteral("assistantComposer"));
    m_composer->setTabChangesFocus(true);
    m_composer->setFixedHeight(86);
    m_composer->installEventFilter(this);
    layout->addWidget(m_composer);
}

void Montaz::ChatView::addMessage(const QString &speaker, const QString &text)
{
    m_messages.append({speaker.left(80), text.left(4000)});
    if (m_messages.size() > 80) m_messages.removeFirst();
    render();
}

void Montaz::ChatView::render()
{
    QString html;
    for (const auto &message : m_messages) {
        html += QStringLiteral("<p><b>%1</b><br>%2</p>")
                    .arg(message.first.toHtmlEscaped(), message.second.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    }
    m_history->setHtml(html);
    m_history->verticalScrollBar()->setValue(m_history->verticalScrollBar()->maximum());
}

void Montaz::ChatView::reset()
{
    m_messages.clear();
    m_history->clear();
    m_composer->clear();
}

void Montaz::ChatView::setBusy(bool busy)
{
    m_busy = busy;
    m_composer->setEnabled(!busy);
}

QString Montaz::ChatView::draft() const
{
    return m_composer->toPlainText().trimmed();
}

void Montaz::ChatView::submit()
{
    const auto text = draft();
    if (m_busy || text.isEmpty()) return;
    if (text.size() > 2000) {
        Q_EMIT inputTooLong();
        return;
    }
    // Disable before emitting: repeated Enter cannot start overlapping requests.
    setBusy(true);
    m_composer->clear();
    Q_EMIT submitted(text);
}

bool Montaz::ChatView::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_composer && event->type() == QEvent::KeyPress) {
        const auto key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ShiftModifier)) {
            if (!key->isAutoRepeat()) submit();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}
