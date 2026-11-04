#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>

class Store : public QObject {
    Q_OBJECT
public:
    explicit Store(QString path, QObject *parent = nullptr);
    QJsonArray lists() const { return data["lists"].toArray(); }
    QJsonArray tasks() const { return data["tasks"].toArray(); }
    QJsonArray outbox() const { return data["outbox"].toArray(); }
    bool ready() const { return healthy; }
    QString error() const { return lastError; }
    bool add(const QString &list, const QString &title);
    bool setCompleted(const QString &list, const QString &task, bool completed);
    bool snapshot(const QJsonArray &lists, const QJsonArray &tasks);
    bool acknowledge(const QJsonObject &operation, QJsonObject result);
    bool uncertain(const QString &operationId);
    bool retryUncertain();
    bool discardPending();
    bool flush();
    static QString key(const QJsonObject &task);
signals:
    void changed();
    void problem(const QString &message);
private:
    QString file, lastError;
    QJsonObject data;
    bool healthy = true;
    bool commit(const QJsonObject &previous);
};
