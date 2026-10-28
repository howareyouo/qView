#include "mainwindow.h"
#include "qvapplication.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

// Portable mode: when a "portable" file exists next to the executable,
// store settings in a "config" folder there instead of the system registry
static void detectPortableMode(const QString &applicationArg0)
{
    const QFileInfo portableMarker(QFileInfo(applicationArg0).absolutePath() + QStringLiteral("/portable"));
    if (!portableMarker.isFile())
        return;

    const QString configDir = portableMarker.absolutePath() + QStringLiteral("/config");
    QDir().mkpath(configDir);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir);
}

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("qView");
    QCoreApplication::setApplicationName("qView");
    QCoreApplication::setApplicationVersion(QString::number(VERSION));
    detectPortableMode(argv[0]);
    QVApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QObject::tr("file"), QObject::tr("The file to open."));
    parser.process(app);

    auto *window = QVApplication::newWindow();
    if (!parser.positionalArguments().isEmpty())
        QVApplication::openFile(window, parser.positionalArguments().constFirst(), true);

    return QApplication::exec();
}
