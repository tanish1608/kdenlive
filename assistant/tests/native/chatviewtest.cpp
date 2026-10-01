// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#include "chatview.h"

#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTest>
#include <QTextBrowser>

class ChatViewTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void messagesCannotBecomeHtmlOrLinks()
    {
        Montaz::ChatView chat;
        chat.addMessage(QStringLiteral("<b>You</b>"), QStringLiteral("<img src='file:///private/photo'>\n<a href='https://example.com'>text</a>"));
        auto history = chat.findChild<QTextBrowser *>();
        QVERIFY(history->toPlainText().contains(QStringLiteral("<img src='file:///private/photo'>")));
        QVERIFY(history->toPlainText().contains(QStringLiteral("<b>You</b>")));
        QVERIFY(!history->openLinks());
        QVERIFY(!history->openExternalLinks());
    }
    void enterSendsOnceAndBusyRejectsDuplicates()
    {
        Montaz::ChatView chat;
        auto composer = chat.findChild<QPlainTextEdit *>();
        QSignalSpy submitted(&chat, &Montaz::ChatView::submitted);
        composer->setPlainText(QStringLiteral("  Make a calm travel edit  "));
        QTest::keyClick(composer, Qt::Key_Return);
        QCOMPARE(submitted.count(), 1);
        QCOMPARE(submitted.first().first().toString(), QStringLiteral("Make a calm travel edit"));
        QVERIFY(chat.draft().isEmpty());
        composer->setPlainText(QStringLiteral("Second request"));
        chat.submit();
        QCOMPARE(submitted.count(), 1);
        chat.setBusy(false);
        chat.submit();
        QCOMPARE(submitted.count(), 2);
    }
    void shiftEnterKeepsMultilineDraft()
    {
        Montaz::ChatView chat;
        auto composer = chat.findChild<QPlainTextEdit *>();
        QSignalSpy submitted(&chat, &Montaz::ChatView::submitted);
        QTest::keyClicks(composer, "Beach opening");
        QTest::keyClick(composer, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClicks(composer, "Quiet ending");
        QCOMPARE(submitted.count(), 0);
        QCOMPARE(chat.draft(), QStringLiteral("Beach opening\nQuiet ending"));
    }
    void resetRemovesPreviousProjectContext()
    {
        Montaz::ChatView chat;
        chat.addMessage(QStringLiteral("You"), QStringLiteral("Private old project"));
        chat.findChild<QPlainTextEdit *>()->setPlainText(QStringLiteral("Private draft"));
        chat.reset();
        QVERIFY(chat.findChild<QTextBrowser *>()->toPlainText().isEmpty());
        QVERIFY(chat.draft().isEmpty());
    }
    void oversizedBriefIsRetainedAndExplained()
    {
        Montaz::ChatView chat;
        QSignalSpy submitted(&chat, &Montaz::ChatView::submitted);
        QSignalSpy rejected(&chat, &Montaz::ChatView::inputTooLong);
        chat.findChild<QPlainTextEdit *>()->setPlainText(QString(2001, QLatin1Char('a')));
        chat.submit();
        QCOMPARE(submitted.count(), 0);
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(chat.draft().size(), 2001);
        QVERIFY(chat.isEnabled());
    }
};

QTEST_MAIN(ChatViewTest)
#include "chatviewtest.moc"
