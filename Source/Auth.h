#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTimer>
#include <QDateTime>
#include <QUrlQuery>

class Auth : public QObject {
    Q_OBJECT
public:
    explicit Auth(QString directory, QObject *parent = nullptr);
    bool importClient(const QString &file);
    bool configured() const { return !clientId.isEmpty(); }
    bool hasToken() const { return !refreshToken.isEmpty(); }
    void load();
    void login();
    void authorize();
    QString accessToken() const { return access; }
    void invalidateAccess() { access.clear(); }
    static QByteArray challenge(const QByteArray &verifier);
    static bool validCallback(const QUrl &url, const QByteArray &state, const QString &path);
signals:
    void authorized();
    void problem(const QString &message);
    void notice(const QString &message);
    void openBrowser(const QUrl &url);
private:
    QString directory, clientId, clientSecret, refreshToken, access, redirect;
    QByteArray state, verifier;
    QDateTime expires;
    QNetworkAccessManager network;
    QTcpServer listener;
    QTimer timeout;
    bool busy = false;
    void exchange(const QUrlQuery &parameters, bool initial);
    void acceptConnection();
};
