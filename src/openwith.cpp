#include "mainwindow.h"
#include "openwith.h"
#include "qvplatformfunctions.h"
#include "qvwin32functions.h"
#include "ui_qvopenwithdialog.h"

#include <QCollator>
#include <QDir>
#include <QFileDialog>
#include <QProcess>
#include <QStandardPaths>
#include <QMimeDatabase>
#include <QSettings>

#include <QDebug>

const QList<OpenWith::OpenWithItem> OpenWith::getOpenWithItems(const QString &filePath)
{
    if (!filePath.isNull() && !QFileInfo::exists(filePath))
        return {};

    QList<OpenWithItem> listOfOpenWithItems = QVPlatformFunctions::getOpenWithItems(filePath);

    // Natural/alphabetic sort
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(
            listOfOpenWithItems.begin(), listOfOpenWithItems.end(),
            [&collator](const OpenWith::OpenWithItem &item0, const OpenWith::OpenWithItem &item1) {
                return collator.compare(item0.name, item1.name) < 0;
            });

    // Move default item to beginning
    for (int i = 0; i < listOfOpenWithItems.length(); i++) {
        const auto &item = listOfOpenWithItems.at(i);
        if (item.isDefault)
            listOfOpenWithItems.move(i, 0);
    }

    return listOfOpenWithItems;
}

QList<OpenWith::OpenWithItem> OpenWith::getOpenWithItemsFromDesktopFiles(const QString &filePath)
{
    QList<OpenWithItem> listOfOpenWithItems;

    QString mimeName;
    if (!filePath.isEmpty()) {
        QMimeDatabase mimedb;
        mimeName = mimedb.mimeTypeForFile(filePath, QMimeDatabase::MatchContent).name();
    }

    // This should probably be async dude
    QProcess process;
    process.start("xdg-mime", { "query", "default", mimeName });
    process.waitForFinished();
    QString defaultApplication = process.readAllStandardOutput().trimmed();

    const QStringList &applicationLocations =
            QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const auto &location : applicationLocations) {
        auto dir = QDir(location);
        const auto &entryInfoList = dir.entryInfoList();
        for (const auto &fileInfo : entryInfoList) {
            // Don't add qView to the open with menu!
            if (fileInfo.fileName() == "qView.desktop"
                || fileInfo.fileName() == "com.interversehq.qView.desktop")
                continue;

            if (!fileInfo.fileName().endsWith(".desktop"))
                continue;

            OpenWithItem openWithItem;
            // additional info to hold
            QString mimeTypes;
            bool noDisplay = false;

            QFile file(fileInfo.absoluteFilePath());
            file.open(QIODevice::ReadOnly);
            QTextStream in(&file);
            QString line;
            while (in.readLineInto(&line)) {
                const qsizetype equalsIndex = line.indexOf('=');
                if (equalsIndex == -1)
                    continue;

                const QString key = line.left(equalsIndex);
                const QString value = line.mid(equalsIndex + 1);
                const auto keyIs = [&key](const QString &name) {
                    return key.compare(name, Qt::CaseInsensitive) == 0;
                };

                if (keyIs("Name") && openWithItem.name.isEmpty()) {
                    openWithItem.name = value;
                } else if (keyIs("Icon") && openWithItem.icon.isNull()) {
                    openWithItem.iconName = value;
                } else if (keyIs("Categories") && openWithItem.categories.isEmpty()) {
                    openWithItem.categories = value.split(";");
                } else if (keyIs("Exec") && openWithItem.exec.isEmpty()) {
                    // Strip the field codes (e.g. %f) from the command line
                    openWithItem.exec = QString(value).remove(QRegularExpression("%.*"));
                } else if (keyIs("MimeType") && mimeTypes.isEmpty()) {
                    mimeTypes = value;
                } else if (keyIs("NoDisplay") || keyIs("Hidden")) {
                    if (value.compare("true", Qt::CaseInsensitive) == 0)
                        noDisplay = true;
                }
            }
            if ((mimeTypes.contains(mimeName, Qt::CaseInsensitive) || mimeName.isEmpty())
                && !noDisplay) {
                // If the program is the default program, save it to add to the beginning after
                // sorting
                openWithItem.isDefault = fileInfo.fileName() == defaultApplication;

                listOfOpenWithItems.append(openWithItem);
            }
        }
    }

    return listOfOpenWithItems;
}

void OpenWith::showOpenWithDialog(QWidget *parent)
{
    auto *mainWindow = static_cast<MainWindow *>(parent);
    QString filePath = mainWindow->getCurrentFileDetails().fileInfo.absoluteFilePath();
#ifdef Q_OS_MACOS
    auto openWithDialog = new QFileDialog(parent);
    openWithDialog->setNameFilters({ QT_TR_NOOP("All Applications (*.app)") });
    openWithDialog->setDirectory("/Applications");
    openWithDialog->open();
    connect(openWithDialog, &QFileDialog::fileSelected, [filePath](const QString &executablePath) {
        openWithExecutable("open", { "-a", executablePath }, filePath);
    });
#elif defined Q_OS_WIN
#  ifdef WIN32_LOADED
    QVWin32Functions::showOpenWithDialog(filePath, mainWindow->windowHandle());
#  else
    auto openWithDialog = new QFileDialog(parent);
    openWithDialog->setWindowTitle("Open with...");
    openWithDialog->setNameFilters({ QT_TR_NOOP("Programs (*.exe *.pif *.com *.bat *.cmd)"),
                                     QT_TR_NOOP("All Files (*)") });
    openWithDialog->setDirectory(
            QProcessEnvironment::systemEnvironment().value("PROGRAMFILES", "C:\\"));
    openWithDialog->open();
    connect(openWithDialog, &QFileDialog::fileSelected, [filePath](const QString &executablePath) {
        openWithExecutable(executablePath, filePath);
    });
#  endif
#else
    auto openWithDialog = new QVOpenWithDialog(parent);
    openWithDialog->open();
    connect(openWithDialog, &QVOpenWithDialog::selected,
            [filePath](const QString &exec, const QStringList &args) {
                openWithExecutable(exec, args, filePath);
            });
#endif
}

void OpenWith::openWithExecutable(const QString &executablePath, const QString &filePath)
{
    openWithExecutable(executablePath, QStringList(), filePath);
}

void OpenWith::openWithExecutable(const QString &executablePath, const QStringList &args,
                                  const QString &filePath)
{
    OpenWithItem item;
    item.exec = executablePath;
    item.args = args;
    openWith(filePath, item);
}

void OpenWith::openWith(const QString &filePath, const OpenWithItem &openWithItem)
{
    const QString &nativeFilePath = QDir::toNativeSeparators(filePath);
    const QString &exec = openWithItem.exec.trimmed();
    QStringList args = openWithItem.args;

    if (exec.isEmpty() || exec.isNull())
        return;

    // Windows-only native app launch method
    if (openWithItem.winAssocHandler) {
        QVPlatformFunctions::openWithInvokeAssocHandler(nativeFilePath,
                                                        openWithItem.winAssocHandler.data());
    } else {
        args.append(nativeFilePath);
        QProcess::startDetached(exec, args);
    }
}

// OpenWithDialog (for linux)
QVOpenWithDialog::QVOpenWithDialog(QWidget *parent) : QDialog(parent), ui(new Ui::QVOpenWithDialog)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint | Qt::CustomizeWindowHint));

    connect(this, &QDialog::accepted, this, &QVOpenWithDialog::triggeredOpen);
    connect(ui->treeView, &QTreeView::doubleClicked, this, &QVOpenWithDialog::triggeredOpen);

    model = new QStandardItemModel();

    ui->treeView->setModel(model);
    populateTreeView();
}

void QVOpenWithDialog::populateTreeView()
{
    auto listOfAllApps = OpenWith::getOpenWithItems(QString());

    for (const auto &category : categories) {
        auto *categoryItem = new QStandardItem(
                QIcon::fromTheme(category.iconName, QIcon::fromTheme("applications-other")),
                category.readableName);
        QMutableListIterator<OpenWith::OpenWithItem> i(listOfAllApps);
        while (i.hasNext()) {
            auto app = i.next();
            if (app.categories.contains(category.name, Qt::CaseInsensitive)
                || category.name.isEmpty()) {
                auto *item = new QStandardItem(QIcon::fromTheme(app.iconName), app.name);
                item->setData(app.exec, Qt::UserRole);
                categoryItem->setChild(categoryItem->rowCount(), item);
                i.remove();
            }
        }
        if (categoryItem->rowCount())
            model->appendRow(categoryItem);
    }
}

void QVOpenWithDialog::triggeredOpen()
{
    auto selectedIndexes = ui->treeView->selectionModel()->selectedIndexes();
    if (selectedIndexes.isEmpty())
        return;

    QString exec = selectedIndexes.first().data(Qt::UserRole).toString();
    QStringList args = exec.split(" ");
    exec = args.takeFirst();
    emit selected(exec, args);
}

QVOpenWithDialog::~QVOpenWithDialog()
{
    delete ui;
}
