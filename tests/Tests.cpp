#include "Store.h"
#include "Api.h"
#include "Auth.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

static QJsonObject task(QString id = "t", QString status = "needsAction") {
    return {{"id", id}, {"list", "l"}, {"title", "Test"}, {"status", status}};
}
static QJsonArray lists() { return {QJsonObject{{"id", "l"}, {"title", "List"}}}; }

class Server : public QTcpServer {
public:
    struct Response { int code; QByteArray body; QByteArray headers; bool disconnect = false; };
    QList<Response> responses;
    QList<QByteArray> requests;
    std::function<void(const QByteArray &)> onRequest;
    Server() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto *socket = nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                auto bytes = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", bytes);
                const int end = bytes.indexOf("\r\n\r\n"); if (end < 0) return;
                int length = 0;
                for (const auto &line : bytes.left(end).split('\n')) if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (bytes.size() < end + 4 + length || socket->property("handled").toBool()) return;
                socket->setProperty("handled", true); requests.append(bytes);
                if (onRequest) onRequest(bytes);
                if (responses.isEmpty()) { socket->abort(); return; }
                const auto reply = responses.takeFirst();
                if (reply.disconnect) { socket->abort(); return; }
                socket->write("HTTP/1.1 " + QByteArray::number(reply.code) + " Test\r\nContent-Type: application/json\r\nConnection: close\r\n" + reply.headers
                    + "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\n\r\n" + reply.body);
                socket->disconnectFromHost();
            });
        });
    }
    QUrl url() const { return QUrl(QString("http://127.0.0.1:%1/").arg(serverPort())); }
    void json(const QJsonObject &body, int code = 200, QByteArray headers = {}) {
        responses.append({code, QJsonDocument(body).toJson(QJsonDocument::Compact), headers, false});
    }
    void snapshot() { json({{"items", lists()}}); json({{"items", QJsonArray{task()}}}); }
};

class Tests : public QObject {
    Q_OBJECT
private slots:
    void pkce() {
        QCOMPARE(Auth::challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), QByteArray("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    }
    void callbackValidation() {
        QVERIFY(Auth::validCallback(QUrl("/oauth2callback?code=ok&state=abc"), "abc", "/oauth2callback"));
        for (const auto &path : {"/wrong?code=ok&state=abc", "/oauth2callback?code=ok&state=wrong", "/oauth2callback?code=ok&state=abc&state=abc", "/oauth2callback?state=abc", "/oauth2callback?code=x&code=y&state=abc", "/oauth2callback?code=x&state=abc&error=denied"})
            QVERIFY(!Auth::validCallback(QUrl(path), "abc", "/oauth2callback"));
    }
    void persistenceAndCoalescing() {
        QTemporaryDir dir; const QString path = dir.filePath("cache.json");
        { Store s(path); QVERIFY(s.snapshot(lists(), {task()})); QVERIFY(s.setCompleted("l", "t", true)); QVERIFY(s.setCompleted("l", "t", false)); QCOMPARE(s.outbox().size(), 1); }
        Store s(path); QVERIFY(s.ready()); QCOMPARE(s.outbox().size(), 1); QCOMPARE(s.tasks()[0].toObject()["status"].toString(), QString("needsAction"));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QVERIFY(!file.readAll().contains("refresh_token"));
    }
    void pendingSurvivesSnapshot() {
        QTemporaryDir dir; Store s(dir.filePath("cache.json"));
        QVERIFY(s.snapshot(lists(), {task()})); QVERIFY(s.setCompleted("l", "t", true));
        QVERIFY(s.snapshot(lists(), {task()})); QCOMPARE(s.tasks()[0].toObject()["status"].toString(), QString("completed"));
        QVERIFY(s.snapshot(lists(), {})); QCOMPARE(s.tasks().size(), 1);
    }
    void editDuringCreate() {
        QTemporaryDir dir; Store s(dir.filePath("cache.json"));
        QVERIFY(s.add("l", "Hello")); const auto op = s.outbox()[0].toObject();
        QVERIFY(s.uncertain(op["id"].toString())); QVERIFY(s.setCompleted("l", op["task"].toString(), true));
        QVERIFY(s.acknowledge(op, task("remote")));
        QCOMPARE(s.outbox().size(), 1); QCOMPARE(s.outbox()[0].toObject()["kind"].toString(), QString("status"));
        QCOMPARE(s.outbox()[0].toObject()["task"].toString(), QString("remote"));
        QCOMPARE(s.tasks()[0].toObject()["status"].toString(), QString("completed"));
    }
    void editDuringPatch() {
        QTemporaryDir dir; Store s(dir.filePath("cache.json")); s.snapshot(lists(), {task()});
        s.setCompleted("l", "t", true); const auto op = s.outbox()[0].toObject();
        s.setCompleted("l", "t", false); QVERIFY(s.acknowledge(op, task("t", "completed")));
        QCOMPARE(s.outbox().size(), 1); QCOMPARE(s.tasks()[0].toObject()["status"].toString(), QString("needsAction"));
    }
    void acknowledgementDoesNotCacheNotes() {
        QTemporaryDir dir; const auto path = dir.filePath("cache.json"); Store s(path);
        QVERIFY(s.snapshot(lists(), {task()})); QVERIFY(s.setCompleted("l", "t", true));
        auto response = task("t", "completed"); response["notes"] = "private notes"; response["selfLink"] = "unused";
        QVERIFY(s.acknowledge(s.outbox()[0].toObject(), response));
        QVERIFY(!s.tasks()[0].toObject().contains("notes")); QVERIFY(!s.tasks()[0].toObject().contains("selfLink"));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QVERIFY(!file.readAll().contains("private notes"));
    }
    void corruptCacheIsNotOverwritten() {
        QTemporaryDir dir; QFile file(dir.filePath("cache.json")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("bad JSON"); file.close();
        Store s(file.fileName()); QVERIFY(!s.ready()); QVERIFY(!s.add("l", "test"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("bad JSON"));
    }
    void writeFailureRollsBack() {
        QTemporaryDir dir; const QString path = dir.filePath("file"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        Store s(path + "/cache.json"); QVERIFY(!s.add("l", "test")); QVERIFY(s.outbox().isEmpty()); QVERIFY(s.tasks().isEmpty());
    }
    void discardAndRetry() {
        QTemporaryDir dir; Store s(dir.filePath("cache.json")); s.snapshot(lists(), {task()});
        s.add("l", "Local"); const auto op = s.outbox()[0].toObject(); s.uncertain(op["id"].toString());
        QVERIFY(s.retryUncertain()); QVERIFY(!s.outbox()[0].toObject()["uncertain"].toBool());
        s.setCompleted("l", "t", true); QVERIFY(s.discardPending());
        QVERIFY(s.outbox().isEmpty()); QCOMPARE(s.tasks().size(), 1);
    }
    void clientImportValidation() {
        QTemporaryDir dir; Auth auth(dir.path()); QSignalSpy errors(&auth, &Auth::problem);
        QFile file(dir.filePath("client.json")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{\"web\":{\"client_id\":\"bad\"}}"); file.close();
        QVERIFY(!auth.importClient(file.fileName())); QVERIFY(!auth.configured()); QCOMPARE(errors.size(), 1);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"installed\":{\"client_id\":\"test.apps.googleusercontent.com\",\"client_secret\":\"synthetic\"}}"); file.close();
        QVERIFY(auth.importClient(file.fileName())); QVERIFY(auth.configured());
    }
    void inputValidation() {
        QTemporaryDir dir; Store s(dir.filePath("cache.json"));
        QVERIFY(!s.add("", "x")); QVERIFY(!s.add("l", "   ")); QVERIFY(!s.add("l", QString(1025, 'x'))); QVERIFY(!s.setCompleted("l", "missing", true));
    }
    void paginationAndHiddenCompleted() {
        QTemporaryDir dir; Store s(dir.filePath("cache.json")); Server server;
        server.json({{"items", lists()}, {"nextPageToken", "list next"}}); server.json({{"items", QJsonArray{}}});
        server.json({{"items", QJsonArray{task()}}, {"nextPageToken", "task+next"}}); server.json({{"items", QJsonArray{task("done", "completed")}}});
        Api api(&s, server.url()); QSignalSpy finish(&api, &Api::finished); api.sync("test-token");
        QTRY_COMPARE_WITH_TIMEOUT(finish.size(), 1, 5000); QVERIFY(finish[0][0].toBool()); QCOMPARE(s.tasks().size(), 2);
        QCOMPARE(server.requests.size(), 4); QVERIFY(server.requests[1].contains("pageToken=list%20next"));
        QVERIFY(server.requests[2].contains("showCompleted=true&showHidden=true")); QVERIFY(server.requests[3].contains("task%2Bnext"));
    }
    void failureKeepsCacheAndBackoff() {
        QTemporaryDir dir; Store s(dir.filePath("cache.json")); s.snapshot(lists(), {task()}); Server server;
        server.json({{"error", "unavailable"}}, 503, "Retry-After: 300\r\n");
        Api api(&s, server.url()); QSignalSpy finish(&api, &Api::finished); api.sync("test");
        QTRY_COMPARE(finish.size(), 1); QVERIFY(!finish[0][0].toBool()); QCOMPARE(finish[0][2].toInt(), 300); QCOMPARE(s.tasks().size(), 1);
    }
    void lostPostIsNeverAutomaticallyRetried() {
        QTemporaryDir dir; Store s(dir.filePath("cache.json")); s.add("l", "New task"); Server server;
        server.responses.append({0, {}, {}, true}); server.snapshot(); server.snapshot();
        Api api(&s, server.url()); QSignalSpy finish(&api, &Api::finished); api.sync("test"); QTRY_COMPARE(finish.size(), 1);
        QVERIFY(s.outbox()[0].toObject()["uncertain"].toBool());
        api.sync("test"); QTRY_COMPARE(finish.size(), 2);
        int posts = 0; for (const auto &req : server.requests) if (req.startsWith("POST")) ++posts;
        QCOMPARE(posts, 1); QCOMPARE(s.outbox().size(), 1);
        Store reopened(dir.filePath("cache.json")); QVERIFY(reopened.outbox()[0].toObject()["uncertain"].toBool());
    }
    void patchAndCreateAcknowledged() {
        QTemporaryDir dir; Store s(dir.filePath("cache.json")); s.snapshot(lists(), {task()}); s.setCompleted("l", "t", true); s.add("l", "New");
        Server server; server.json(task("t", "completed")); server.json(task("new")); server.snapshot();
        Api api(&s, server.url()); QSignalSpy finish(&api, &Api::finished); api.sync("test"); api.sync("test");
        QTRY_COMPARE(finish.size(), 1); QVERIFY(finish[0][0].toBool()); QVERIFY(s.outbox().isEmpty());
        QVERIFY(server.requests[0].startsWith("PATCH")); QVERIFY(server.requests[1].startsWith("POST")); QCOMPARE(server.requests.size(), 4);
    }
    void repeatedPageIsRejected() {
        QTemporaryDir dir; Store s(dir.filePath("cache.json")); Server server;
        server.json({{"nextPageToken", "same"}}); server.json({{"nextPageToken", "same"}});
        Api api(&s, server.url()); QSignalSpy finish(&api, &Api::finished); api.sync("test");
        QTRY_COMPARE(finish.size(), 1); QVERIFY(!finish[0][0].toBool()); QCOMPARE(server.requests.size(), 2);
    }
};
QTEST_GUILESS_MAIN(Tests)
#include "Tests.moc"
