#include "Api.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>

// Qt can transparently resend buffered POSTs after a dropped connection. Refuse replay.
class OneShotBody : public QIODevice {
public:
    explicit OneShotBody(QByteArray data, QObject *parent) : QIODevice(parent), data(std::move(data)) { open(ReadOnly); }
    bool isSequential() const override { return true; }
    bool reset() override { return false; }
    qint64 bytesAvailable() const override { return data.size() - offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *output, qint64 maximum) override {
        const qint64 count = qMin(maximum, qint64(data.size()) - offset);
        memcpy(output, data.constData() + offset, size_t(count)); offset += count; return count;
    }
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    QByteArray data;
    qint64 offset = 0;
};

Api::Api(Store *s, QUrl endpoint, QObject *parent) : QObject(parent), store(s), base(std::move(endpoint)) {}
QString Api::escaped(const QString &id) { return QString::fromLatin1(QUrl::toPercentEncoding(id)); }
void Api::request(const QByteArray &method, const QString &path, const QJsonObject &body, Callback callback) {
    QNetworkRequest req(QUrl(base.toString() + path));
    req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    req.setRawHeader("Accept", "application/json");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(20000);
    QNetworkReply *reply = nullptr;
    if (method == "GET") reply = network.get(req);
    else if (method == "POST") {
        const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        auto *upload = new OneShotBody(bytes, this);
        req.setAttribute(QNetworkRequest::DoNotBufferUploadDataAttribute, true);
        req.setHeader(QNetworkRequest::ContentLengthHeader, bytes.size());
        reply = network.sendCustomRequest(req, method, upload);
        connect(reply, &QObject::destroyed, upload, &QObject::deleteLater);
    } else reply = network.sendCustomRequest(req, method, QJsonDocument(body).toJson(QJsonDocument::Compact));
    // Bound individual responses; a malicious/broken endpoint must not exhaust RAM.
    connect(reply, &QNetworkReply::readyRead, this, [reply] { if (reply->bytesAvailable() > 16 * 1024 * 1024) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401) emit unauthorized();
        const auto header = reply->rawHeader("Retry-After");
        bool numeric = false; int delay = header.toInt(&numeric);
        if (!numeric && !header.isEmpty()) delay = int(QDateTime::currentDateTimeUtc().secsTo(QDateTime::fromString(QString::fromLatin1(header), Qt::RFC2822Date)));
        retryAfter = qMax(retryAfter, qBound(0, delay, 86400));
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(reply->readAll(), &parse);
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
            && parse.error == QJsonParseError::NoError && doc.isObject();
        reply->deleteLater(); callback(ok, doc.object(), status);
    });
}
void Api::sync(const QString &accessToken) {
    if (active || accessToken.isEmpty()) return;
    if (!store->ready() || !store->flush()) { emit finished(false, store->error(), 0); return; }
    active = true; token = accessToken; mutationError.clear(); retryAfter = 0;
    remoteLists = {}; remoteTasks = {}; listIndex = 0; pages.clear();
    pushNext();
}
void Api::pushNext() {
    QJsonObject op;
    for (const auto &value : store->outbox()) {
        if (!value.toObject()["uncertain"].toBool()) { op = value.toObject(); break; }
    }
    if (op.isEmpty()) { fetchLists(); return; }
    const bool create = op["kind"] == "create";
    // Persist before POST: a crash or lost reply must never automatically duplicate a task.
    if (create && !store->uncertain(op["id"].toString())) { done(false, store->error()); return; }
    QJsonObject body{{"status", op["status"]}};
    if (create) body["title"] = op["title"];
    const QString path = "lists/" + escaped(op["list"].toString()) + "/tasks"
        + (create ? "" : "/" + escaped(op["task"].toString()));
    request(create ? "POST" : "PATCH", path, body, [this, op, create](bool ok, const QJsonObject &result, int status) {
        if (!ok || result["id"].toString().isEmpty()) {
            mutationError = create ? tr("Ajout incertain (HTTP %1) : vérifiez Google Tasks avant toute nouvelle tentative.").arg(status)
                                   : tr("Modification en attente (HTTP %1). Reconnectez Google si 401/403 ; liste/tâche inaccessible si 404.").arg(status);
            fetchLists(); return;
        }
        if (!store->acknowledge(op, result)) { done(false, store->error()); return; }
        pushNext();
    });
}
void Api::fetchLists(const QString &page) {
    QString path = "users/@me/lists?maxResults=100";
    if (!page.isEmpty()) path += "&pageToken=" + escaped(page);
    request("GET", path, {}, [this](bool ok, const QJsonObject &result, int status) {
        if (!ok || (result.contains("items") && !result["items"].isArray())) {
            done(false, tr("Hors ligne / lecture refusée (HTTP %1). Cache conservé. %2").arg(status).arg(mutationError)); return;
        }
        for (const auto &item : result["items"].toArray()) {
            if (item.toObject()["id"].toString().isEmpty()) { done(false, tr("Réponse Google invalide.")); return; }
            remoteLists.append(item);
        }
        const QString next = result["nextPageToken"].toString();
        if (!next.isEmpty()) {
            if (pages.contains(next) || pages.size() > 10000) { done(false, tr("Pagination Google invalide.")); return; }
            pages.insert(next); fetchLists(next); return;
        }
        pages.clear(); fetchTasks();
    });
}
void Api::fetchTasks(const QString &page) {
    if (listIndex >= remoteLists.size()) {
        if (!store->snapshot(remoteLists, remoteTasks)) { done(false, store->error()); return; }
        done(mutationError.isEmpty(), mutationError); return;
    }
    const QString list = remoteLists[listIndex].toObject()["id"].toString();
    QString path = "lists/" + escaped(list) + "/tasks?maxResults=100&showCompleted=true&showHidden=true&showDeleted=false";
    if (!page.isEmpty()) path += "&pageToken=" + escaped(page);
    request("GET", path, {}, [this, list](bool ok, const QJsonObject &result, int status) {
        if (!ok || (result.contains("items") && !result["items"].isArray())) {
            done(false, tr("Lecture des tâches impossible (HTTP %1). Cache conservé. %2").arg(status).arg(mutationError)); return;
        }
        for (const auto &item : result["items"].toArray()) {
            auto t = item.toObject();
            if (t["id"].toString().isEmpty() || (t["status"] != "completed" && t["status"] != "needsAction")) {
                done(false, tr("Tâche Google invalide ; cache conservé.")); return;
            }
            if (t["deleted"].toBool()) continue;
            // Only cache fields used by the widget, not task notes or assignment metadata.
            remoteTasks.append(QJsonObject{{"id", t["id"]}, {"list", list}, {"title", t["title"]},
                                          {"status", t["status"]}, {"parent", t["parent"]}, {"position", t["position"]}});
            if (remoteTasks.size() > 200000) { done(false, tr("Limite de sécurité : plus de 200 000 tâches.")); return; }
        }
        const QString next = result["nextPageToken"].toString();
        if (!next.isEmpty()) {
            if (pages.contains(next) || pages.size() > 10000) { done(false, tr("Pagination Google invalide.")); return; }
            pages.insert(next); fetchTasks(next); return;
        }
        ++listIndex; pages.clear(); fetchTasks();
    });
}
void Api::done(bool success, const QString &message) {
    active = false; token.clear();
    emit finished(success, message, retryAfter);
}
