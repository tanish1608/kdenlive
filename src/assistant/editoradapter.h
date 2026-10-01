// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Montaz contributors
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <memory>

class KdenliveDoc;
class TimelineItemModel;

namespace Montaz {
class EditorAdapter : public QObject
{
    Q_OBJECT
public:
    explicit EditorAdapter(QObject *parent = nullptr);
    void refreshContext();
    QString contextKey();
    QJsonObject snapshot();
    QJsonObject mediaSources(const QJsonObject &snapshot);
    QString apply(const QJsonObject &proposal);

Q_SIGNALS:
    void invalidated();

private:
    void bind();
    void invalidate();
    QPointer<KdenliveDoc> m_document;
    std::weak_ptr<TimelineItemModel> m_timeline;
    QList<QMetaObject::Connection> m_connections;
    QSet<QString> m_appliedRequests;
    int m_revision{0};
    bool m_applyHealthy{true};
};
} // namespace Montaz
