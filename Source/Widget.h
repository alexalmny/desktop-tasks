#pragma once
#include "Store.h"
#include "Auth.h"
#include "Api.h"
#include <QWidget>
#include <QListWidget>
#include <QSettings>
#include <QTimer>
class QComboBox;
class QLabel;
class QLineEdit;

class Column : public QListWidget {
    Q_OBJECT
public:
    explicit Column(bool completed, QWidget *parent = nullptr);
signals:
    void dropped(const QString &list, const QString &task, bool completed);
protected:
    void startDrag(Qt::DropActions actions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    bool completed;
};
class Widget : public QWidget {
    Q_OBJECT
public:
    Widget(const QString &directory, bool normalWindow, bool demo = false);
    Store *store() { return &cache; }
    void showOnDesktop();
private:
    QString directory;
    Store cache;
    Auth auth;
    Api api;
    QSettings settings;
    Column *todo, *done;
    QComboBox *filter, *destination;
    QLineEdit *input;
    QLabel *status;
    QTimer timer;
    bool desktop, waitingAuth = false;
    int failures = 0;
    QDateTime retryNotBefore;
    void render();
    void message(const QString &text);
    void sync();
    void schedule(bool success, int retryAfter = 0);
    void configure();
};
