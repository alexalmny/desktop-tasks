#include "Auth.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpSocket>
#include <QUrlQuery>
#include <qt6keychain/keychain.h>

static QByteArray randomToken() {
    QByteArray bytes(32, Qt::Uninitialized);
    for (int i = 0; i < bytes.size(); ++i) bytes[i] = char(QRandomGenerator::system()->generate() & 0xff);
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QByteArray Auth::challenge(const QByteArray &value) {
    return QCryptographicHash::hash(value, QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
bool Auth::validCallback(const QUrl &url, const QByteArray &expected, const QString &path) {
    const QUrlQuery q(url);
    return url.path() == path && !expected.isEmpty() && q.allQueryItemValues("state").size() == 1
        && q.queryItemValue("state").toUtf8() == expected && q.allQueryItemValues("code").size() == 1
        && !q.queryItemValue("code").isEmpty() && !q.hasQueryItem("error");
}
Auth::Auth(QString dir, QObject *parent) : QObject(parent), directory(std::move(dir)) {
    QFile input(directory + "/oauth-client.json");
    if (input.open(QIODevice::ReadOnly)) {
        const auto o = QJsonDocument::fromJson(input.readAll()).object();
        clientId = o["client_id"].toString(); clientSecret = o["client_secret"].toString();
    }
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, this, [this] {
        listener.close(); state.clear(); busy = false; emit problem(tr("Connexion Google expirée. Réessayez."));
    });
    connect(&listener, &QTcpServer::newConnection, this, &Auth::acceptConnection);
}
bool Auth::importClient(const QString &path) {
    if (busy) return false;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 64 * 1024) { emit problem(tr("Fichier OAuth inaccessible ou trop grand.")); return false; }
    const auto o = QJsonDocument::fromJson(input.readAll()).object()["installed"].toObject();
    const auto id = o["client_id"].toString();
    if (!id.endsWith(".apps.googleusercontent.com") || o["client_secret"].toString().isEmpty()) {
        emit problem(tr("Importez le JSON d’un client Google de type Application de bureau.")); return false;
    }
    if (!clientId.isEmpty() && clientId != id) { emit problem(tr("Ce profil possède déjà un autre client OAuth. Utilisez un nouveau dossier de profil.")); return false; }
    QSaveFile output(directory + "/oauth-client.json");
    if (!output.open(QIODevice::WriteOnly)) { emit problem(output.errorString()); return false; }
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(QJsonObject{{"client_id", id}, {"client_secret", o["client_secret"]}}).toJson();
    if (output.write(bytes) != bytes.size() || !output.commit()) { emit problem(tr("Impossible de sauvegarder le client OAuth.")); return false; }
    clientId = id; clientSecret = o["client_secret"].toString();
    return true;
}
void Auth::load() {
    if (!configured()) { emit notice(tr("Importez un client OAuth puis connectez Google (menu ⋮).")); return; }
    auto *job = new QKeychain::ReadPasswordJob("desktop-tasks", this);
    job->setKey(directory); job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job] {
        if (job->error()) {
            if (job->error() == QKeychain::EntryNotFound) emit notice(tr("Connectez votre compte Google (menu ⋮)."));
            else emit problem(tr("Coffre système indisponible : %1").arg(job->errorString()));
            return;
        }
        refreshToken = job->textData(); authorize();
    });
    job->start();
}
void Auth::login() {
    if (busy) return;
    if (!configured()) { emit problem(tr("Importez d’abord un client OAuth Desktop.")); return; }
    if (!listener.listen(QHostAddress::LocalHost, 0)) { emit problem(tr("Impossible d’ouvrir le retour OAuth local.")); return; }
    busy = true; state = randomToken(); verifier = randomToken();
    redirect = QString("http://127.0.0.1:%1/oauth2callback").arg(listener.serverPort());
    QUrl url("https://accounts.google.com/o/oauth2/v2/auth");
    QUrlQuery q;
    q.addQueryItem("client_id", clientId); q.addQueryItem("redirect_uri", redirect);
    q.addQueryItem("response_type", "code"); q.addQueryItem("scope", "https://www.googleapis.com/auth/tasks");
    q.addQueryItem("state", QString::fromLatin1(state)); q.addQueryItem("code_challenge", QString::fromLatin1(challenge(verifier)));
    q.addQueryItem("code_challenge_method", "S256"); q.addQueryItem("access_type", "offline");
    q.addQueryItem("prompt", "consent select_account"); url.setQuery(q);
    timeout.start(180000); emit notice(tr("Terminez la connexion dans votre navigateur.")); emit openBrowser(url);
}
void Auth::acceptConnection() {
    while (listener.hasPendingConnections()) {
        auto *socket = listener.nextPendingConnection();
        socket->setParent(this);
        auto *timer = new QTimer(socket); timer->setSingleShot(true); timer->start(5000);
        connect(timer, &QTimer::timeout, socket, &QTcpSocket::abort);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            if (socket->property("handled").toBool()) return;
            auto bytes = socket->property("request").toByteArray() + socket->readAll();
            if (bytes.size() > 8192) { socket->abort(); return; }
            socket->setProperty("request", bytes);
            if (!bytes.contains("\r\n\r\n")) return;
            socket->setProperty("handled", true);
            const auto line = bytes.left(bytes.indexOf("\r\n")).split(' ');
            const QUrl url = line.size() == 3 ? QUrl::fromEncoded(line[1]) : QUrl();
            if (line.size() != 3 || line[0] != "GET" || !validCallback(url, state, "/oauth2callback")) {
                socket->write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"); socket->disconnectFromHost(); return;
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\nConnexion recue. Vous pouvez fermer cet onglet.");
            socket->disconnectFromHost(); listener.close(); timeout.stop(); state.clear();
            QUrlQuery q;
            q.addQueryItem("client_id", clientId); q.addQueryItem("client_secret", clientSecret);
            q.addQueryItem("code", QUrlQuery(url).queryItemValue("code")); q.addQueryItem("code_verifier", QString::fromLatin1(verifier));
            q.addQueryItem("redirect_uri", redirect); q.addQueryItem("grant_type", "authorization_code");
            verifier.clear(); exchange(q, true);
        });
    }
}
void Auth::authorize() {
    if (busy) return;
    if (!access.isEmpty() && expires > QDateTime::currentDateTimeUtc().addSecs(60)) { emit authorized(); return; }
    if (refreshToken.isEmpty()) { emit problem(tr("Connectez Google (menu ⋮).")); return; }
    busy = true;
    QUrlQuery q; q.addQueryItem("client_id", clientId); q.addQueryItem("client_secret", clientSecret);
    q.addQueryItem("refresh_token", refreshToken); q.addQueryItem("grant_type", "refresh_token");
    exchange(q, false);
}
void Auth::exchange(const QUrlQuery &parameters, bool initial) {
    QNetworkRequest request(QUrl("https://oauth2.googleapis.com/token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    auto *reply = network.post(request, parameters.query(QUrl::FullyEncoded).toUtf8().replace("+", "%2B"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, initial] {
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto result = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = reply->error() == QNetworkReply::NoError && code == 200 && !result["access_token"].toString().isEmpty();
        reply->deleteLater(); busy = false;
        if (!ok) {
            access.clear();
            emit problem(result["error"] == "invalid_grant" ? tr("Autorisation Google expirée/révoquée. Reconnectez le compte.") : tr("Échec OAuth (HTTP %1). Vérifiez la connexion et la configuration du client.").arg(code));
            return;
        }
        access = result["access_token"].toString(); expires = QDateTime::currentDateTimeUtc().addSecs(result["expires_in"].toInt(3600));
        if (!initial) { emit authorized(); return; }
        const QString token = result["refresh_token"].toString();
        if (token.isEmpty()) { access.clear(); emit problem(tr("Google n’a pas fourni d’autorisation hors ligne. Réessayez la connexion.")); return; }
        auto *job = new QKeychain::WritePasswordJob("desktop-tasks", this);
        job->setKey(directory); job->setTextData(token); job->setInsecureFallback(false);
        connect(job, &QKeychain::Job::finished, this, [this, job, token] {
            if (job->error()) { access.clear(); emit problem(tr("Impossible de protéger le jeton : %1").arg(job->errorString())); return; }
            refreshToken = token; emit authorized();
        });
        job->start();
    });
}
