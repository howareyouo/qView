#include "mainwindow.h"
#include "qvapplication.h"
#include "qvprofiler.h"

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

// Portable mode: when a "portable" file exists next to the executable, store settings in
// qView.ini in that same directory instead of the system registry.
static void detectPortableMode(const QString &applicationArg0)
{
    const QFileInfo portableMarker(
            QFileInfo(applicationArg0).absolutePath() + QStringLiteral("/portable"));
    if (!portableMarker.isFile())
        return;

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, portableMarker.absolutePath());
    // QSettings builds the user ini path as <path>/<org>/<app>.ini when the application name
    // is set, and clearing the name is not an option: QCoreApplication auto-fills an empty
    // name from argv[0] during construction. An organization name of "." collapses that
    // level, so the settings land directly in <appDir>/qView.ini.
    QCoreApplication::setOrganizationName(QStringLiteral("."));
}

int main(int argc, char *argv[])
{
    // Both clocks are only recorded when QV_PROFILE is set, see qvprofiler.h
    QVProfile::startStartupClock();
    QElapsedTimer startupTimer;
    startupTimer.start();

    QCoreApplication::setOrganizationName("qView");
    QCoreApplication::setApplicationName("qView");
    QCoreApplication::setApplicationVersion(QString::number(VERSION));
    detectPortableMode(argv[0]);
    QVProfile::record("startup: pre application init", startupTimer.nsecsElapsed());
    startupTimer.restart();

    QVApplication app(argc, argv);
    // Covers everything in the QVApplication constructor, including the QApplication base
    // constructor, the platform plugin and the member managers
    QVProfile::record("startup: application construction", startupTimer.nsecsElapsed());
    startupTimer.restart();

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QObject::tr("file"), QObject::tr("The file to open."));
    parser.process(app);
    QVProfile::record("startup: command line parse", startupTimer.nsecsElapsed());
    startupTimer.restart();

    auto *window = QVApplication::newWindow();
    // Also covers show() and raise(), so the first native window creation is included
    QVProfile::record("startup: new window", startupTimer.nsecsElapsed());
    startupTimer.restart();

    if (!parser.positionalArguments().isEmpty())
        QVApplication::openFile(window, parser.positionalArguments().constFirst(), true);
    QVProfile::record("startup: open file request", startupTimer.nsecsElapsed());

    QVProfile::record("startup: until event loop", QVProfile::sinceStartup());

    // Marks the point where the event loop starts, so "first event loop turn" can be separated
    // from the work done before it
    QTimer::singleShot(0, &app, []() { QVProfile::milestone("startup: first event loop turn"); });

    const int exitCode = QApplication::exec();
    QVProfile::dump();
    return exitCode;
}
