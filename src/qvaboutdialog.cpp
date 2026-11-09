#include "qvaboutdialog.h"
#include "ui_qvaboutdialog.h"

#include "qvapplication.h"

#include <QSysInfo>

QVAboutDialog::QVAboutDialog(double givenLatestVersionNum, QWidget *parent)
    : QDialog(parent), ui(new Ui::QVAboutDialog), latestVersionNum(givenLatestVersionNum)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint | Qt::CustomizeWindowHint));

    // Application modal on mac, window modal everywhere else
#ifdef Q_OS_MACOS
    setWindowModality(Qt::ApplicationModal);
#else
    setWindowModality(Qt::WindowModal);
#endif

    // add fonts
    qvApp->ensureFontLoaded(":/fonts/Lato-Light.ttf");
    qvApp->ensureFontLoaded(":/fonts/Lato-Regular.ttf");

#ifdef Q_OS_MACOS
    const int modifier = 4;
    const int logoFontSize = 96;
#else
    const int modifier = 0;
    const int logoFontSize = 72;
#endif

    // set main title font
    ui->logoLabel->setFont(QFont("Lato", logoFontSize, QFont::Light));

    // set subtitle font & text
    QFont subtitleFont("Lato", 18 + modifier);
    subtitleFont.setStyleName("Regular");
    // If this is a nightly build, display the build number
#ifdef NIGHTLY
    const QString subtitleText = tr("Nightly %1").arg(QT_STRINGIFY(NIGHTLY));
#else
    const QString subtitleText = tr("version %1").arg(QString::number(VERSION, 'f', 1));
#endif
    ui->subtitleLabel->setFont(subtitleFont);
    ui->subtitleLabel->setText(subtitleText);

    // set update font & text
    QFont updateFont("Lato", 10 + modifier);
    updateFont.setStyleName("Regular");
    ui->updateLabel->setFont(updateFont);
    ui->updateLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->updateLabel->setOpenExternalLinks(true);

    // set infolabel2 font, text, and properties
    QFont infoFont("Lato", 8 + modifier);
    infoFont.setStyleName("Regular");
    const QString infoText =
            tr("Built with Qt %1 (%2)<br>"
               R"(Source code available under GPLv3 on <a style="color: #03A9F4; text-decoration:none;" href="https://github.com/jurplel/qView">GitHub</a><br>)"
               "Icon glyph created by Guilhem from the Noun Project<br>"
               "Copyright © %3 jurplel and qView contributors")
                    .arg(QT_VERSION_STR, QSysInfo::buildCpuArchitecture(), "2018-2025");

    ui->infoLabel2->setFont(infoFont);
    ui->infoLabel2->setText(infoText);

    ui->infoLabel2->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->infoLabel2->setOpenExternalLinks(true);

#ifndef QV_DISABLE_ONLINE_VERSION_CHECK
    if (latestVersionNum < 0.0) {
        qvApp->checkUpdates(false);
        latestVersionNum = 0.0;
    }
#endif // QV_DISABLE_ONLINE_VERSION_CHECK

    updateText();
}

QVAboutDialog::~QVAboutDialog()
{
    delete ui;
}

void QVAboutDialog::updateText()
{
    QString updateLabelText;
#ifndef QV_DISABLE_ONLINE_VERSION_CHECK
    updateLabelText = tr("Checking for updates...");
    if (latestVersionNum > VERSION) {
        //: %1 is a version number e.g. "4.0 update available"
        updateLabelText = tr("%1 update available").arg(QString::number(latestVersionNum, 'f', 1));
    } else if (latestVersionNum > 0.0) {
        updateLabelText = tr("No updates available");
    } else if (latestVersionNum < 0.0) {
        updateLabelText = tr("Error checking for updates");
    }
    updateLabelText += "<br>";
#endif // QV_DISABLE_ONLINE_VERSION_CHECK
    ui->updateLabel->setText(
            updateLabelText
            + R"(<a style="color: #03A9F4; text-decoration:none;" href="https://interversehq.com/qview/">interversehq.com/qview</a>)");
}

void QVAboutDialog::setLatestVersionNum(double value)
{
    latestVersionNum = value;
    updateText();
}
