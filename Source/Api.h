#pragma once
#include "Store.h"
#include <QNetworkAccessManager>
#include <QSet>
#include <functional>

class Api : public QObject {
    Q_OBJECT
public:
    explicit Api(Store *store, QUrl base = QUrl("https://tasks.googleapis.com/tasks/v1/"), QObject *parent = nullptr);
    bool running() const { return active; }
    void sync(const QString &accessToken);
signals:
    void unauthorized();
    void finished(bool success, const QString &message, int retryAfterSeconds);
private:
    Store *store;
    QUrl base;
    QNetworkAccessManager network;
    QString token, mutationError;
    bool active = false;
    int listIndex = 0, retryAfter = 0;
    QJsonArray remoteLists, remoteTasks;
    QSet<QString> pages;
    using Callback = std::function<void(bool, const QJsonObject &, int)>;
    void request(const QByteArray &method, const QString &path, const QJsonObject &body, Callback callback);
    void pushNext();
    void fetchLists(const QString &page = {});
    void fetchTasks(const QString &page = {});
    void done(bool success, const QString &message);
    static QString escaped(const QString &id);
};
