#include "qvwelcomedialog.h"
#include "ui_qvwelcomedialog.h"

#include "qvapplication.h"
#include "updatechecker.h"

QVWelcomeDialog::QVWelcomeDialog(QWidget *parent) : QDialog(parent), ui(new Ui::QVWelcomeDialog)
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
    const int logoFontSize = 72;
#else
    const int modifier = 0;
    const int logoFontSize = 54;
#endif

    // set main title font
    ui->logoLabel->setFont(QFont("Lato", logoFontSize, QFont::Light));

    // set subtitle font & text
    QFont subtitleFont("Lato", 14 + modifier);
    subtitleFont.setStyleName("Regular");
    const QString subtitleText =
            tr("Thank you for downloading qView.<br>Here's a few tips to get you started:");
    ui->subtitleLabel->setFont(subtitleFont);
    ui->subtitleLabel->setText(subtitleText);

    // set info font & text
    QFont infoFont("Lato", 12 + modifier);
    infoFont.setStyleName("Regular");
    const QString tipsText = tr("<ul><li>Right click to access the main menu</li><li>Drag the "
                                "image to reposition it</li><li>Scroll to zoom in and "
                                "out</li><li>Use arrow keys to switch files</li></ul>");
    ui->infoLabel->setFont(infoFont);
    ui->infoLabel->setText(tipsText);

    ui->infoLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->infoLabel->setOpenExternalLinks(true);

#ifdef QV_DISABLE_ONLINE_VERSION_CHECK
    ui->updateCheckBox->hide();
#else
    ui->updateCheckBox->setChecked(UpdateChecker::notificationsEnabled());
    connect(ui->updateCheckBox, &QCheckBox::stateChanged, this,
            [](int state) { UpdateChecker::setNotificationsEnabled(state > 0); });
#endif // QV_DISABLE_ONLINE_VERSION_CHECK
}

QVWelcomeDialog::~QVWelcomeDialog()
{
    delete ui;
}
