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
    const QFileInfo applicationFileInfo(applicationArg0);
    const QFileInfo portableMarker(applicationFileInfo.absolutePath()
                                   + QStringLiteral("/portable"));
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

    // Records the phase that just finished and starts the clock for the next one
    const auto recordStartupPhase = [&startupTimer](const char *phase) {
        QVProfile::record(phase, startupTimer.nsecsElapsed());
        startupTimer.restart();
    };

    QCoreApplication::setOrganizationName("qView");
    QCoreApplication::setApplicationName("qView");
    QCoreApplication::setApplicationVersion(QString::number(VERSION));
    detectPortableMode(argv[0]);
    recordStartupPhase("startup: pre application init");

    QVApplication app(argc, argv);
    // Covers everything in the QVApplication constructor, including the QApplication base
    // constructor, the platform plugin and the member managers
    recordStartupPhase("startup: application construction");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QObject::tr("file"), QObject::tr("The file to open."));
    parser.process(app);
    recordStartupPhase("startup: command line parse");

    auto *window = QVApplication::newWindow();
    // Also covers show() and raise(), so the first native window creation is included
    recordStartupPhase("startup: new window");

    const auto positionalArgs = parser.positionalArguments();
    if (!positionalArgs.isEmpty())
        QVApplication::openFile(window, positionalArgs.constFirst(), true);
    QVProfile::record("startup: open file request", startupTimer.nsecsElapsed());

    QVProfile::record("startup: until event loop", QVProfile::sinceStartup());

    // Marks the point where the event loop starts, so "first event loop turn" can be separated
    // from the work done before it
    QTimer::singleShot(0, &app, []() { QVProfile::milestone("startup: first event loop turn"); });

    const int exitCode = QApplication::exec();
    QVProfile::dump();
    return exitCode;
}
