#include "fileoperations.h"
#include "mainwindow.h"
#include "qvcocoafunctions.h"
#include "qvapplication.h"

#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>

void FileOperations::askDeleteFile(MainWindow &window, bool permanent)
{
    if (!permanent
        && !qvApp->getSettingsManager().getBool(SettingsManager::Setting::AskDelete)) {
        deleteFile(window, permanent);
        return;
    }

    const QFileInfo &fileInfo = window.getCurrentFileDetails().fileInfo;
    const QString fileName = window.getCurrentFileDetails().fileInfo.fileName();

    if (!fileInfo.isWritable()) {
        QMessageBox::critical(
                &window, window.tr("Error"),
                window.tr("Can't delete %1:\nNo write permission or file is read-only.")
                        .arg(fileName));
        return;
    }

    QString messageText;
    if (permanent) {
        messageText = window.tr("Are you sure you want to delete %1 permanently? This can't be "
                                "undone.")
                              .arg(fileName);
    } else {
#ifdef Q_OS_WIN
        messageText = window.tr("Are you sure you want to move %1 to the Recycle Bin?").arg(fileName);
#else
        messageText = window.tr("Are you sure you want to move %1 to the Trash?").arg(fileName);
#endif
    }

    auto *msgBox = new QMessageBox(QMessageBox::Question, window.tr("Delete"), messageText,
                                   QMessageBox::Yes | QMessageBox::No, &window);
    if (!permanent)
        msgBox->setCheckBox(new QCheckBox(window.tr("Do not ask again")));

    connect(msgBox, &QMessageBox::finished, &window, [&window, msgBox, permanent](int result) {
        if (result != QMessageBox::Yes)
            return;

        if (!permanent) {
            QSettings settings;
            settings.beginGroup("options");
            settings.setValue("askdelete", !msgBox->checkBox()->isChecked());
            qvApp->getSettingsManager().loadSettings();
        }
        window.deleteFile(permanent);
    });

    msgBox->open();
}

void FileOperations::deleteFile(MainWindow &window, bool permanent)
{
    const QFileInfo &fileInfo = window.getCurrentFileDetails().fileInfo;
    const QString filePath = fileInfo.absoluteFilePath();
    const QString fileName = fileInfo.fileName();

    window.view().closeImage();

    bool success;
    QString trashFilePath;
    if (permanent) {
        success = QFile::remove(filePath);
    } else {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
        QFile file(filePath);
        success = file.moveToTrash();
        if (success)
            trashFilePath = file.fileName();
#elif defined Q_OS_MACOS && COCOA_LOADED
        QString trashedFile = QVCocoaFunctions::deleteFile(filePath);
        success = !trashedFile.isEmpty();
        if (success)
            trashFilePath = QUrl(trashedFile).toLocalFile(); // remove file:// protocol
#elif defined Q_OS_UNIX && !defined Q_OS_MACOS
        trashFilePath = deleteFileLinuxFallback(filePath, false);
        success = !trashFilePath.isEmpty();
#else
        QMessageBox::critical(&window, window.tr("Not Supported"),
                              window.tr("This program was compiled with an old version of Qt and "
                                        "this "
                                        "feature is not available.\n"
                                        "If you see this message, please report a bug!"));

        return;
#endif
    }

    if (!success || QFile::exists(filePath)) {
        window.openFile(filePath);
        QMessageBox::critical(&window, window.tr("Error"),
                              window.tr("Can't delete %1.").arg(fileName));
        return;
    }

    const auto afterDelete =
            qvApp->getSettingsManager().getInt(SettingsManager::Setting::AfterDelete);
    if (afterDelete > 1)
        window.view().goToFile(QVGraphicsView::GoToFileMode::next);
    else if (afterDelete < 1)
        window.view().goToFile(QVGraphicsView::GoToFileMode::previous);

    if (!trashFilePath.isEmpty())
        lastDeletedFiles.push({ trashFilePath, filePath });

    window.disableActions();
}

QString FileOperations::deleteFileLinuxFallback(const QString &path, bool putBack)
{
    QStringList gioArgs = { "trash", path };
    if (putBack)
        gioArgs.insert(1, "--restore");

    QProcess process;
    process.start("gio", gioArgs);
    process.waitForFinished();

    if (process.error() != QProcess::FailedToStart && !putBack) {
        process.start("gio", { "trash", "--list" });
        process.waitForFinished();

        const auto &output = QString(process.readAllStandardOutput()).split("\n");
        for (const auto &line : output) {
            if (line.contains(path))
                return line.split("\t").at(0);
        }
    }

    qWarning("Failed to use linux fallback delete");
    return "";
}

void FileOperations::undoDelete(MainWindow &window)
{
    if (lastDeletedFiles.isEmpty())
        return;

    const DeletedPaths lastDeletedFile = lastDeletedFiles.pop();
    if (lastDeletedFile.pathInTrash.isEmpty() || lastDeletedFile.previousPath.isEmpty())
        return;

#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)) || (defined Q_OS_MACOS && COCOA_LOADED)
    const QFileInfo fileInfo(lastDeletedFile.pathInTrash);
    if (!fileInfo.isWritable()) {
        QMessageBox::critical(&window,
                              window.tr("Error"),
                              window.tr("Can't undo deletion of %1:\n"
                                        "No write permission or file is read-only.")
                                      .arg(fileInfo.fileName()));
        return;
    }

    bool success = QFile::rename(lastDeletedFile.pathInTrash, lastDeletedFile.previousPath);
    if (!success) {
        QMessageBox::critical(&window, window.tr("Error"),
                              window.tr("Failed undoing deletion of %1.").arg(fileInfo.fileName()));
    }
#elif defined Q_OS_UNIX && !defined Q_OS_MACOS
    deleteFileLinuxFallback(lastDeletedFile.pathInTrash, true);
#else
    QMessageBox::critical(&window, window.tr("Not Supported"),
                          window.tr("This program was compiled with an old version of Qt and this "
                                    "feature "
                                    "is not available.\n"
                                    "If you see this message, please report a bug!"));

    return;
#endif

    window.openFile(lastDeletedFile.previousPath);
    window.disableActions();
}
