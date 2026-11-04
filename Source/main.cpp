#include "Widget.h"
#include "Desktop.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>

int main(int argc, char **argv) {
    bool normal = false;
    for (int i = 1; i < argc; ++i) if (QByteArray(argv[i]) == "--window" || QByteArray(argv[i]) == "--demo") normal = true;
    // Read the saved mode before creating QApplication: Wayland chooses its shell integration at startup.
    QString preliminaryProfile;
    for (int i = 1; i + 1 < argc; ++i) if (QByteArray(argv[i]) == "--profile") preliminaryProfile = QString::fromLocal8Bit(argv[i + 1]);
    if (preliminaryProfile.isEmpty()) {
#ifdef Q_OS_WIN
        preliminaryProfile = qEnvironmentVariable("LOCALAPPDATA") + "/desktop-tasks";
#else
        preliminaryProfile = qEnvironmentVariable("XDG_DATA_HOME", QDir::homePath() + "/.local/share") + "/desktop-tasks";
#endif
    }
    QSettings preliminary(QDir(preliminaryProfile).absoluteFilePath("settings.ini"), QSettings::IniFormat);
    initializeDesktopShell(!normal && preliminary.value("desktop", true).toBool());
    QApplication app(argc, argv);
    app.setApplicationName("desktop-tasks"); app.setApplicationVersion("0.1.0");
    QCommandLineParser parser; parser.setApplicationDescription("Native Google Tasks desktop widget"); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"window", "Use a normal window instead of desktop integration."});
    parser.addOption({"profile", "Use a separate local data directory.", "directory"});
    parser.addOption({"demo", "Use 100 synthetic tasks in a temporary profile, without network or OAuth."});
    parser.addOption({"quit-after", "Exit after N seconds (smoke tests/measurement).", "seconds"});
    parser.addOption({"screenshot", "Save a screenshot after first render (use with --demo).", "png"});
    parser.process(app);
    QTemporaryDir demo;
    QString profile = parser.isSet("demo") ? demo.path() : QFileInfo(preliminaryProfile).absoluteFilePath();
    if (!QDir().mkpath(profile)) return 1;
    QFile::setPermissions(profile, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QLockFile lock(profile + "/instance.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { QMessageBox::warning(nullptr, "Desktop Tasks", QObject::tr("Ce profil est déjà ouvert, ou son verrou est inaccessible.")); return 1; }
    Widget widget(profile, normal, parser.isSet("demo"));
    if (parser.isSet("demo")) {
        QJsonArray tasks;
        for (int i = 0; i < 100; ++i) tasks.append(QJsonObject{{"id", QString::number(i)}, {"list", "demo"},
            {"title", QObject::tr("Tâche de démonstration %1").arg(i + 1)}, {"status", i % 3 == 0 ? "completed" : "needsAction"}});
        if (!widget.store()->snapshot(QJsonArray{QJsonObject{{"id", "demo"}, {"title", "Démonstration"}}}, tasks)) return 1;
    }
    widget.showOnDesktop();
    if (parser.isSet("quit-after")) {
        bool ok = false; const int seconds = parser.value("quit-after").toInt(&ok);
        if (!ok || seconds < 1 || seconds > 3600) return 2;
        QTimer::singleShot(seconds * 1000, &app, &QApplication::quit);
    }
    if (parser.isSet("screenshot")) QTimer::singleShot(250, &widget, [&] { widget.grab().save(parser.value("screenshot")); });
    return app.exec();
}
