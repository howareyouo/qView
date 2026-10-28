#include "urlopener.h"
#include "mainwindow.h"
#include "qvapplication.h"

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

void UrlOpener::openUrl(MainWindow &window, const QUrl &url)
{
    if (!url.isValid()) {
        QMessageBox::critical(&window, window.tr("Error"), window.tr("Error: URL is invalid"));
        return;
    }

    auto request = QNetworkRequest(url);
    auto *reply = networkAccessManager.get(request);
    auto *progressDialog =
            new QProgressDialog(window.tr("Downloading image..."), window.tr("Cancel"), 0, 100);
    progressDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->setWindowTitle(window.tr("Open URL..."));
    progressDialog->open();

    connect(progressDialog, &QProgressDialog::canceled, reply, [reply] { reply->abort(); });

    connect(reply, &QNetworkReply::downloadProgress, progressDialog,
            [progressDialog](qreal bytesReceived, qreal bytesTotal) {
                auto percent = (bytesReceived / bytesTotal) * 100;
                progressDialog->setValue(qRound(percent));
            });

    connect(reply, &QNetworkReply::finished, &window,
            [&window, progressDialog, reply] {
        if (reply->error()) {
            progressDialog->close();
            QMessageBox::critical(&window, window.tr("Error"),
                                  window.tr("Error ") + QString::number(reply->error()) + ": "
                                          + reply->errorString());

            progressDialog->deleteLater();
            return;
        }

        progressDialog->setMaximum(0);

        auto *tempFile = new QTemporaryFile(&window);
        tempFile->setFileTemplate(QDir::tempPath() + "/" + qvApp->applicationName()
                                  + ".XXXXXX.png");

        auto *saveFutureWatcher = new QFutureWatcher<bool>();
        connect(saveFutureWatcher, &QFutureWatcher<bool>::finished, &window,
                [&window, progressDialog, tempFile, saveFutureWatcher]() {
                    progressDialog->close();
                    if (saveFutureWatcher->result()) {
                        if (tempFile->open()) {
                            window.openFile(tempFile->fileName());
                        }
                    } else {
                        QMessageBox::critical(&window, window.tr("Error"),
                                              window.tr("Error: Invalid image"));
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
    connect(inputDialog, &QInputDialog::finished, &window,
            [&window, inputDialog](int result) {
        if (result) {
            const auto url = QUrl(inputDialog->textValue());
            window.openUrl(url);
        }
        inputDialog->deleteLater();
    });
    inputDialog->open();
}
