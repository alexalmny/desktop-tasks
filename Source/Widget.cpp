#include "Widget.h"
#include "Desktop.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

static const char *mime = "application/x-desktop-tasks-task";
Column::Column(bool state, QWidget *parent) : QListWidget(parent), completed(state) {
    setDragEnabled(true); setAcceptDrops(true); setDropIndicatorShown(true);
    setDefaultDropAction(Qt::CopyAction); setSelectionMode(QAbstractItemView::SingleSelection);
    setWordWrap(true); setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAccessibleName(state ? tr("Terminées") : tr("À faire"));
}
void Column::startDrag(Qt::DropActions) {
    if (!currentItem()) return;
    const QJsonObject task = QJsonObject::fromVariantMap(currentItem()->data(Qt::UserRole).toMap());
    auto *data = new QMimeData; data->setData(mime, QJsonDocument(task).toJson(QJsonDocument::Compact));
    QDrag drag(this); drag.setMimeData(data); drag.exec(Qt::CopyAction);
}
void Column::dragEnterEvent(QDragEnterEvent *event) {
    if (qobject_cast<Column *>(event->source()) && event->mimeData()->hasFormat(mime)) event->acceptProposedAction();
}
void Column::dragMoveEvent(QDragMoveEvent *event) {
    if (qobject_cast<Column *>(event->source()) && event->mimeData()->hasFormat(mime)) event->acceptProposedAction();
}
void Column::dropEvent(QDropEvent *event) {
    if (!qobject_cast<Column *>(event->source()) || !event->mimeData()->hasFormat(mime)) return;
    const auto task = QJsonDocument::fromJson(event->mimeData()->data(mime)).object();
    emit dropped(task["list"].toString(), task["id"].toString(), completed); event->acceptProposedAction();
}

Widget::Widget(const QString &dir, bool normalWindow, bool demo)
    : directory(dir), cache(dir + "/cache.json", this), auth(dir, this), api(&cache, QUrl("https://tasks.googleapis.com/tasks/v1/"), this),
      settings(dir + "/settings.ini", QSettings::IniFormat), desktop(!normalWindow && settings.value("desktop", true).toBool()) {
    setWindowTitle("Desktop Tasks");
    if (desktop) setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnBottomHint);
    setMinimumSize(320, 200);
    resize(settings.value("width", 420).toInt(), settings.value("height", 340).toInt());
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(6, 5, 6, 5); layout->setSpacing(4);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel("Tasks"); header->addWidget(title);
    filter = new QComboBox; filter->setAccessibleName(tr("Filtrer les listes")); header->addWidget(filter, 1);
    auto *menuButton = new QToolButton; menuButton->setText(QString::fromUtf8("⋮")); menuButton->setAccessibleName(tr("Menu"));
    menuButton->setPopupMode(QToolButton::InstantPopup); auto *menu = new QMenu(menuButton); menuButton->setMenu(menu); header->addWidget(menuButton);
    layout->addLayout(header);
    auto *columns = new QHBoxLayout; columns->setSpacing(4);
    todo = new Column(false); done = new Column(true);
    for (auto *column : {todo, done}) {
        auto *box = new QVBoxLayout; box->setSpacing(2);
        box->addWidget(new QLabel(column == todo ? tr("À faire") : tr("Terminées"))); box->addWidget(column);
        columns->addLayout(box, 1);
        connect(column, &Column::dropped, this, [this](const QString &list, const QString &id, bool completed) {
            if (cache.setCompleted(list, id, completed)) timer.start(100);
        });
        connect(column, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
            const auto task = item->data(Qt::UserRole).toMap();
            const QString list = task["list"].toString(), id = task["id"].toString();
            const bool completed = item->checkState() == Qt::Checked;
            // Defer rebuilding the list until Qt has finished its itemChanged dispatch.
            QTimer::singleShot(0, this, [this, list, id, completed] {
                if (cache.setCompleted(list, id, completed)) timer.start(100);
            });
        });
    }
    layout->addLayout(columns, 1);
    auto *add = new QHBoxLayout;
    input = new QLineEdit; input->setMaxLength(1024); input->setPlaceholderText(tr("Nouvelle tâche…")); input->setAccessibleName(tr("Nouvelle tâche (Entrée pour ajouter)"));
    destination = new QComboBox; destination->setMaximumWidth(130); destination->setAccessibleName(tr("Liste de destination"));
    add->addWidget(input, 1); add->addWidget(destination); layout->addLayout(add);
    status = new QLabel; status->setTextFormat(Qt::PlainText); status->setMinimumWidth(0); status->setAccessibleName(tr("État de synchronisation")); layout->addWidget(status);
    connect(input, &QLineEdit::returnPressed, this, [this] {
        if (cache.add(destination->currentData().toString(), input->text())) { input->clear(); timer.start(100); }
    });
    connect(filter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        settings.setValue("filter", filter->currentData());
        if (!filter->currentData().toString().isEmpty()) destination->setCurrentIndex(destination->findData(filter->currentData()));
        render();
    });
    connect(destination, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { settings.setValue("destination", destination->currentData()); });
    menu->addAction(tr("Synchroniser"), this, &Widget::sync);
    menu->addAction(tr("Importer le client OAuth…"), this, [this] {
        const auto file = QFileDialog::getOpenFileName(nullptr, tr("Client OAuth Google Desktop"), {}, "JSON (*.json)");
        if (!file.isEmpty() && auth.importClient(file)) message(tr("Client importé. Choisissez Connecter Google."));
    });
    menu->addAction(tr("Connecter Google…"), this, [this] { auth.login(); });
    menu->addAction(tr("Taille, position et options…"), this, &Widget::configure);
    menu->addAction(tr("Réessayer les ajouts incertains…"), this, [this] {
        if (api.running()) { message(tr("Attendez la fin de la synchronisation.")); return; }
        if (QMessageBox::warning(nullptr, tr("Risque de doublon"), tr("Vérifiez d’abord Google Tasks. Une réponse perdue peut cacher un ajout déjà réussi. Réessayer tous les ajouts incertains ?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            if (!api.running() && cache.retryUncertain()) sync();
    });
    menu->addAction(tr("Abandonner les modifications en attente…"), this, [this] {
        if (api.running()) { message(tr("Attendez la fin de la synchronisation.")); return; }
        if (QMessageBox::warning(nullptr, tr("Abandon des modifications locales"), tr("Supprimer tous les ajouts et changements non synchronisés de ce profil ? Les tâches présentes sur Google ne seront pas supprimées. Les ajouts purement locaux seront perdus."), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            if (!api.running() && cache.discardPending()) sync();
    });
    menu->addAction(tr("Ouvrir le dossier local"), this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(directory)); });
    menu->addSeparator(); menu->addAction(tr("Quitter"), qApp, &QApplication::quit);
    timer.setSingleShot(true); connect(&timer, &QTimer::timeout, this, &Widget::sync);
    connect(&cache, &Store::changed, this, &Widget::render);
    connect(&cache, &Store::problem, this, &Widget::message);
    connect(&auth, &Auth::notice, this, &Widget::message);
    connect(&auth, &Auth::problem, this, [this](const QString &text) { waitingAuth = false; message(text); schedule(false); });
    connect(&auth, &Auth::openBrowser, this, [this](const QUrl &url) {
        if (!QDesktopServices::openUrl(url)) message(tr("Impossible d’ouvrir le navigateur système. Installez/configurez-en un puis réessayez."));
    });
    connect(&auth, &Auth::authorized, this, [this] { waitingAuth = false; api.sync(auth.accessToken()); });
    connect(&api, &Api::unauthorized, &auth, &Auth::invalidateAccess);
    connect(&api, &Api::finished, this, [this](bool success, const QString &text, int retryAfter) {
        int uncertain = 0;
        for (const auto &op : cache.outbox()) if (op.toObject()["uncertain"].toBool()) ++uncertain;
        if (!text.isEmpty()) message(text);
        else if (uncertain) message(tr("%1 ajout(s) incertain(s) — vérifiez Google Tasks (menu).").arg(uncertain));
        else message(tr("À jour à %1 · %2 en attente").arg(QTime::currentTime().toString("HH:mm:ss")).arg(cache.outbox().size()));
        schedule(success, retryAfter);
        if (success && cache.outbox().size() > uncertain) timer.start(100);
    });
    render();
    if (!cache.ready()) message(cache.error());
    else if (!demo) QTimer::singleShot(0, &auth, &Auth::load);
}
void Widget::render() {
    const QSignalBlocker filterBlock(filter), destinationBlock(destination), todoBlock(todo), doneBlock(done);
    const QString selected = settings.value("filter").toString();
    const QString target = settings.value("destination").toString();
    filter->clear(); filter->addItem(tr("Toutes les listes"), ""); destination->clear();
    QHash<QString, QString> names;
    for (const auto &value : cache.lists()) {
        const auto list = value.toObject(); const QString id = list["id"].toString(), title = list["title"].toString();
        filter->addItem(title, id); destination->addItem(title, id); names[id] = title;
    }
    const int filterIndex = filter->findData(selected); filter->setCurrentIndex(qMax(0, filterIndex));
    destination->setCurrentIndex(qMax(0, destination->findData(target)));
    input->setEnabled(destination->count() > 0 && cache.ready());
    todo->clear(); done->clear();
    QSet<QString> pending;
    for (const auto &value : cache.outbox()) {
        const auto op = value.toObject(); pending.insert(op["list"].toString() + "/" + op["task"].toString());
    }
    for (const auto &value : cache.tasks()) {
        const auto task = value.toObject();
        if (!filter->currentData().toString().isEmpty() && task["list"].toString() != filter->currentData().toString()) continue;
        const bool completed = task["status"] == "completed";
        auto *item = new QListWidgetItem(task["title"].toString() + (pending.contains(Store::key(task)) ? " •" : ""), completed ? done : todo);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        item->setCheckState(completed ? Qt::Checked : Qt::Unchecked); item->setData(Qt::UserRole, task.toVariantMap());
        item->setToolTip(names.value(task["list"].toString()) + "\n" + task["title"].toString() + (pending.contains(Store::key(task)) ? tr("\nModification en attente") : ""));
    }
}
void Widget::message(const QString &text) {
    status->setText(text.size() > 65 ? text.left(62) + "…" : text); status->setToolTip(text); status->setAccessibleDescription(text);
}
void Widget::sync() {
    if (!cache.ready() || api.running() || waitingAuth) return;
    const qint64 remaining = QDateTime::currentDateTimeUtc().msecsTo(retryNotBefore);
    if (remaining > 0) { timer.start(int(qMin(remaining, qint64(86400000)))); return; }
    if (!auth.hasToken()) { message(tr("Connectez Google via le menu ⋮. Le cache reste disponible.")); return; }
    waitingAuth = true; auth.authorize();
}
void Widget::schedule(bool success, int retryAfter) {
    failures = success ? 0 : qMin(failures + 1, 5);
    const int interval = qBound(30, settings.value("interval", 120).toInt(), 3600);
    const int delay = success ? interval : qMin(900, interval * (1 << failures));
    const int seconds = qMax(delay, retryAfter);
    retryNotBefore = success ? QDateTime() : QDateTime::currentDateTimeUtc().addSecs(seconds);
    timer.start(seconds * 1000);
}
void Widget::showOnDesktop() {
    const int x = settings.value("x", 30).toInt(), y = settings.value("y", 50).toInt();
    if (!attachToDesktop(this, desktop, x, y)) {
        setWindowFlags(Qt::Window); desktop = false;
        move(x, y); message(tr("Intégration bureau indisponible : mode fenêtre normal."));
    }
    show();
}
void Widget::configure() {
    QDialog dialog(nullptr); dialog.setWindowTitle(tr("Desktop Tasks — Options"));
    QFormLayout form(&dialog);
    auto spin = [&form, &dialog](const QString &label, int value, int minimum, int maximum) {
        auto *box = new QSpinBox(&dialog); box->setRange(minimum, maximum); box->setValue(value); form.addRow(label, box); return box;
    };
    auto *w = spin(tr("Largeur"), width(), 320, 4000), *h = spin(tr("Hauteur"), height(), 200, 3000);
    auto *x = spin(tr("Position X (écran principal)"), settings.value("x", 30).toInt(), 0, 16000);
    auto *y = spin(tr("Position Y"), settings.value("y", 50).toInt(), 0, 16000);
    auto *interval = spin(tr("Synchronisation (secondes)"), settings.value("interval", 120).toInt(), 30, 3600);
    auto *mode = new QCheckBox(tr("Intégration bureau (après redémarrage)")); mode->setChecked(settings.value("desktop", true).toBool()); form.addRow(mode);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons); connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    settings.setValue("width", w->value()); settings.setValue("height", h->value()); settings.setValue("x", x->value()); settings.setValue("y", y->value());
    settings.setValue("interval", interval->value()); settings.setValue("desktop", mode->isChecked()); settings.sync();
    resize(w->value(), h->value());
    if (desktop) positionDesktop(this, x->value(), y->value()); else move(x->value(), y->value());
    schedule(true);
}
