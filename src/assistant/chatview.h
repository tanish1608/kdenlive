// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QList>
#include <QWidget>

class QPlainTextEdit;
class QTextBrowser;

namespace Montaz {
// Session-only conversation: never writes briefs or footage descriptions to logs.
class ChatView : public QWidget
{
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);
    void addMessage(const QString &speaker, const QString &text);
    void reset();
    void setBusy(bool busy);
    QString draft() const;
    void submit();

Q_SIGNALS:
    void submitted(const QString &text);
    void inputTooLong();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void render();
    QTextBrowser *m_history;
    QPlainTextEdit *m_composer;
    QList<QPair<QString, QString>> m_messages;
    bool m_busy{false};
};
} // namespace Montaz
