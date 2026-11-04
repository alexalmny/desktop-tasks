#include "Store.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

static QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString Store::key(const QJsonObject &t) { return t["list"].toString() + "/" + t["id"].toString(); }

Store::Store(QString path, QObject *parent) : QObject(parent), file(std::move(path)) {
    data = {{"version", 1}, {"lists", QJsonArray{}}, {"tasks", QJsonArray{}}, {"outbox", QJsonArray{}}};
    QFile input(file);
    if (!input.exists()) return;
    QJsonParseError parse;
    if (!input.open(QIODevice::ReadOnly) || input.size() > 64 * 1024 * 1024) {
        healthy = false;
    } else {
        const auto doc = QJsonDocument::fromJson(input.readAll(), &parse);
        const auto root = doc.object();
        healthy = parse.error == QJsonParseError::NoError && root["version"].toInt() == 1
            && root["lists"].isArray() && root["tasks"].isArray() && root["outbox"].isArray();
        if (healthy) {
            for (const auto &value : root["outbox"].toArray()) {
                const auto op = value.toObject();
                if (op["id"].toString().isEmpty() || op["list"].toString().isEmpty()
                    || op["task"].toString().isEmpty()
                    || (op["kind"] != "create" && op["kind"] != "status")) healthy = false;
            }
        }
        if (healthy) data = root;
    }
    if (!healthy) lastError = tr("Cache illisible : sauvegardez puis déplacez %1. Aucune écriture autorisée.").arg(file);
}

bool Store::flush() {
    if (!healthy) { emit problem(lastError); return false; }
    QDir dir = QFileInfo(file).absoluteDir();
    if (!dir.mkpath(".")) { lastError = tr("Impossible de créer le dossier du cache."); emit problem(lastError); return false; }
    QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly)) { lastError = output.errorString(); emit problem(lastError); return false; }
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        lastError = output.errorString(); emit problem(lastError); return false;
    }
    return true;
}
bool Store::commit(const QJsonObject &previous) {
    if (!flush()) { data = previous; return false; }
    emit changed();
    return true;
}

bool Store::add(const QString &list, const QString &title) {
    if (list.isEmpty() || title.trimmed().isEmpty() || title.size() > 1024) return false;
    const auto previous = data;
    auto ts = tasks(); auto ops = outbox();
    const QString local = "local:" + uuid();
    ts.append(QJsonObject{{"id", local}, {"list", list}, {"title", title.trimmed()}, {"status", "needsAction"}});
    ops.append(QJsonObject{{"id", uuid()}, {"kind", "create"}, {"task", local}, {"list", list},
                          {"title", title.trimmed()}, {"status", "needsAction"}});
    data["tasks"] = ts; data["outbox"] = ops;
    return commit(previous);
}

bool Store::setCompleted(const QString &list, const QString &task, bool completed) {
    const auto previous = data;
    const QString status = completed ? "completed" : "needsAction";
    auto ts = tasks(); auto ops = outbox();
    int found = -1;
    for (int i = 0; i < ts.size(); ++i) {
        auto t = ts[i].toObject();
        if (t["list"] == list && t["id"] == task) { t["status"] = status; ts[i] = t; found = i; break; }
    }
    if (found < 0) return false;
    bool replaced = false;
    for (int i = 0; i < ops.size(); ++i) {
        auto op = ops[i].toObject();
        if (op["list"] == list && op["task"] == task) {
            op["status"] = status;
            // Keep an uncertain POST paused even when its displayed status is edited.
            op["id"] = uuid(); ops[i] = op; replaced = true; break;
        }
    }
    if (!replaced) ops.append(QJsonObject{{"id", uuid()}, {"kind", "status"}, {"task", task}, {"list", list}, {"status", status}});
    data["tasks"] = ts; data["outbox"] = ops;
    return commit(previous);
}

bool Store::snapshot(const QJsonArray &ls, const QJsonArray &remote) {
    const auto previous = data;
    auto merged = remote;
    for (const auto &pending : outbox()) {
        const auto op = pending.toObject();
        const QString pendingKey = op["list"].toString() + "/" + op["task"].toString();
        for (const auto &local : tasks()) {
            if (key(local.toObject()) != pendingKey) continue;
            bool replaced = false;
            for (int i = 0; i < merged.size(); ++i) {
                if (key(merged[i].toObject()) == pendingKey) {
                    auto t = merged[i].toObject(); t["status"] = op["status"]; merged[i] = t; replaced = true; break;
                }
            }
            if (!replaced) merged.append(local);
            break;
        }
    }
    data["lists"] = ls; data["tasks"] = merged;
    return commit(previous);
}

bool Store::acknowledge(const QJsonObject &sent, QJsonObject result) {
    const auto previous = data;
    auto ops = outbox(); auto ts = tasks();
    result = {{"id", result["id"]}, {"list", sent["list"]}, {"title", result["title"]},
              {"status", result["status"]}, {"parent", result["parent"]}, {"position", result["position"]}};
    if (result["id"].toString().isEmpty()) return false;
    for (int i = ops.size() - 1; i >= 0; --i) {
        auto op = ops[i].toObject();
        if (op["id"] == sent["id"]) { ops.removeAt(i); }
        else if (sent["kind"] == "create" && op["task"] == sent["task"] && op["list"] == sent["list"]) {
            op["task"] = result["id"]; op["kind"] = "status"; op.remove("uncertain");
            ops[i] = op; result["status"] = op["status"];
        } else if (op["task"] == sent["task"] && op["list"] == sent["list"]) result["status"] = op["status"];
    }
    for (int i = 0; i < ts.size(); ++i) {
        auto t = ts[i].toObject();
        if (t["list"] == sent["list"] && t["id"] == sent["task"]) { ts[i] = result; break; }
    }
    data["tasks"] = ts; data["outbox"] = ops;
    return commit(previous);
}

bool Store::uncertain(const QString &operationId) {
    const auto previous = data;
    auto ops = outbox();
    for (int i = 0; i < ops.size(); ++i) {
        auto op = ops[i].toObject();
        if (op["id"] == operationId) { op["uncertain"] = true; ops[i] = op; }
    }
    data["outbox"] = ops;
    return commit(previous);
}
bool Store::discardPending() {
    const auto previous = data;
    auto ts = tasks();
    for (int i = ts.size() - 1; i >= 0; --i)
        if (ts[i].toObject()["id"].toString().startsWith("local:")) ts.removeAt(i);
    data["outbox"] = QJsonArray{}; data["tasks"] = ts;
    return commit(previous);
}
bool Store::retryUncertain() {
    const auto previous = data;
    auto ops = outbox();
    for (int i = 0; i < ops.size(); ++i) { auto op = ops[i].toObject(); op.remove("uncertain"); ops[i] = op; }
    data["outbox"] = ops;
    return commit(previous);
}
