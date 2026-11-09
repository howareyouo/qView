#include "urlopener.h"
#include "mainwindow.h"

#include <QDir>
#include <QFutureWatcher>
#include <QImage>
#include <QInputDialog>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QTemporaryFile>
#include <QtConcurrent/QtConcurrentRun>

namespace {
void showError(MainWindow &window, const QString &message)
{
    QMessageBox::critical(&window, window.tr("Error"), message);
}
} // namespace

void UrlOpener::openUrl(MainWindow &window, const QUrl &url)
{
    if (!url.isValid()) {
        showError(window, window.tr("Error: URL is invalid"));
        return;
    }

    auto *reply = networkAccessManager.get(QNetworkRequest(url));
    auto *progressDialog =
            new QProgressDialog(window.tr("Downloading image..."), window.tr("Cancel"), 0, 100);
    progressDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->setWindowTitle(window.tr("Open URL..."));
    progressDialog->open();

    QObject::connect(progressDialog, &QProgressDialog::canceled, reply,
                     [reply] { reply->abort(); });

    QObject::connect(reply, &QNetworkReply::downloadProgress, progressDialog,
                     [progressDialog](qreal bytesReceived, qreal bytesTotal) {
                         progressDialog->setValue(qRound(bytesReceived / bytesTotal * 100));
                     });

    QObject::connect(reply, &QNetworkReply::finished, &window, [&window, progressDialog, reply] {
        if (reply->error()) {
            progressDialog->close();
            showError(window,
                      window.tr("Error ") + QString::number(reply->error()) + ": "
                              + reply->errorString());

            progressDialog->deleteLater();
            return;
        }

        progressDialog->setMaximum(0);

        auto *tempFile = new QTemporaryFile(&window);
        tempFile->setFileTemplate(QDir::tempPath() + "/qview.XXXXXX.png");

        auto *saveFutureWatcher = new QFutureWatcher<bool>();
        QObject::connect(saveFutureWatcher, &QFutureWatcher<bool>::finished, &window,
                         [&window, progressDialog, tempFile, saveFutureWatcher]() {
                             progressDialog->close();
                             if (saveFutureWatcher->result()) {
                                 if (tempFile->open()) {
                                     window.openFile(tempFile->fileName());
                                 }
                             } else {
                                 showError(window, window.tr("Error: Invalid image"));
                                 tempFile->deleteLater();
                             }
                             progressDialog->deleteLater();
                             saveFutureWatcher->deleteLater();
                         });

        saveFutureWatcher->setFuture(QtConcurrent::run([reply, tempFile] {
            return QImage::fromData(reply->readAll()).save(tempFile, "png");
        }));
    });
}

void UrlOpener::pickUrl(MainWindow &window)
{
    auto inputDialog = new QInputDialog(&window);
    inputDialog->setWindowTitle(window.tr("Open URL..."));
    inputDialog->setLabelText(window.tr("URL of a supported image file:"));
    inputDialog->resize(350, inputDialog->height());
    inputDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QObject::connect(inputDialog, &QInputDialog::finished, &window,
                     [&window, inputDialog](int result) {
                         if (result) {
                             const auto url = QUrl(inputDialog->textValue());
                             window.openUrl(url);
                         }
                         inputDialog->deleteLater();
                     });
    inputDialog->open();
}
