#include "qvoptionsdialog.h"
#include "ui_qvoptionsdialog.h"
#include "qvapplication.h"
#include <QColorDialog>
#include <QPalette>
#include <QScreen>
#include <QMessageBox>
#include <QSettings>
#include <QKeySequence>

#include <QDebug>

QVOptionsDialog::QVOptionsDialog(QWidget *parent) : QDialog(parent), ui(new Ui::QVOptionsDialog)
{
    ui->setupUi(this);

    // Set platform-specific modifier text for Ctrl drag checkbox
    QString ctrlString = QKeySequence(Qt::ControlModifier).toString().remove('+');
    ui->ctrlDragCheckbox->setText(ui->ctrlDragCheckbox->text().arg(ctrlString));
    ui->ctrlDragCheckbox->setToolTip(ui->ctrlDragCheckbox->toolTip().arg(ctrlString));
    ui->framelessCheckbox->setToolTip(ui->framelessCheckbox->toolTip().arg(ctrlString));

    languageRestartMessageShown = false;

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint | Qt::CustomizeWindowHint));

    resize(640, 530);

    qvApp->ensureFontLoaded(":/fonts/MaterialIconsOutlined-Regular.otf");

    connect(ui->categoryList, &QListWidget::currentRowChanged, this,
            [this](int currentRow) { ui->stackedWidget->setCurrentIndex(currentRow); });
    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, &QVOptionsDialog::buttonBoxClicked);
    connect(ui->shortcutsTable, &QTableWidget::cellDoubleClicked, this,
            &QVOptionsDialog::shortcutCellDoubleClicked);
    connect(ui->bgColorCheckbox, &QCheckBox::stateChanged, this,
            &QVOptionsDialog::bgColorCheckboxStateChanged);
    connect(ui->scalingCheckbox, &QCheckBox::stateChanged, this,
            &QVOptionsDialog::scalingCheckboxStateChanged);

    QSettings settings;

    populateCategories(settings.value("optionstab", 1).toInt());
    populateLanguages();

    // On macOS, the dialog should not be dependent on any window
#ifndef Q_OS_MACOS
    setWindowModality(Qt::WindowModal);
#else
    // Load window geometry
    restoreGeometry(settings.value("optionsgeometry").toByteArray());
#endif

    if (QOperatingSystemVersion::current()
        < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13)) {
        setWindowTitle("Preferences");
    }

#ifdef QV_DISABLE_ONLINE_VERSION_CHECK
    ui->updateCheckbox->hide();
#endif // QV_DISABLE_ONLINE_VERSION_CHECK

// Platform specific settings
#ifdef Q_OS_MACOS
    ui->menubarCheckbox->hide();
    ui->framelessCheckbox->hide();
#else
    ui->forceDarkModeCheckbox->hide();
    ui->hideTitlebarCheckbox->hide();
    ui->quitOnLastWindowCheckbox->hide();
#endif

    syncSettings(false, true);
    connect(ui->windowResizeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &QVOptionsDialog::windowResizeComboBoxCurrentIndexChanged);
    connect(ui->langComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &QVOptionsDialog::languageComboBoxCurrentIndexChanged);
    connect(ui->scrollZoomsComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &QVOptionsDialog::scrollZoomsComboBoxCurrentIndexChanged);
    syncShortcuts();
    updateButtonBox();
}

QVOptionsDialog::~QVOptionsDialog()
{
    delete ui;
}

void QVOptionsDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange) {
        populateCategories(ui->categoryList->currentRow());
    }
    QDialog::changeEvent(event);
}

void QVOptionsDialog::done(int r)
{
    // Save window geometry
    QSettings settings;
    settings.setValue("optionsgeometry", saveGeometry());
    settings.setValue("optionstab", ui->categoryList->currentRow());

    QDialog::done(r);
}

void QVOptionsDialog::modifySetting(SettingsManager::Setting setting, QVariant value)
{
    transientSettings.insert(static_cast<int>(setting), value);
    updateButtonBox();
}

void QVOptionsDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup("options");

    auto &settingsManager = qvApp->getSettingsManager();
    for (auto it = transientSettings.constBegin(); it != transientSettings.constEnd(); ++it) {
        const auto setting = static_cast<SettingsManager::Setting>(it.key());
        settings.setValue(settingsManager.getSettingKey(setting), it.value());
    }

    settings.endGroup();
    settings.beginGroup("shortcuts");

    const auto &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    for (int i = 0; i < transientShortcuts.length(); i++) {
        settings.setValue(shortcutsList.value(i).name, transientShortcuts.value(i));
    }

    qvApp->getShortcutManager().updateShortcuts();
    qvApp->getSettingsManager().loadSettings();
}

void QVOptionsDialog::syncSettings(bool defaults, bool makeConnections)
{
    auto &settingsManager = qvApp->getSettingsManager();
    settingsManager.loadSettings();

    // bgcolorenabled
    syncCheckbox(ui->bgColorCheckbox, SettingsManager::Setting::BgColorEnabled, defaults,
                 makeConnections);
    // bgcolor
    const auto bgColor = SettingsManager::Setting::BgColor;
    ui->bgColorButton->setText(settingsManager.getString(bgColor, defaults));
    transientSettings.insert(static_cast<int>(bgColor), ui->bgColorButton->text());
    updateBgColorButton();
    connect(ui->bgColorButton, &QPushButton::clicked, this, &QVOptionsDialog::bgColorButtonClicked);
    // bgcoloralpha
    syncSpinBox(ui->bgColorAlphaSpinBox, SettingsManager::Setting::BgColorAlpha, defaults,
                makeConnections);
    setBgColorControlsEnabled(ui->bgColorCheckbox->isChecked());
    // titlebarmode
    syncRadioButtons({ ui->titlebarRadioButton0, ui->titlebarRadioButton1, ui->titlebarRadioButton2,
                       ui->titlebarRadioButton3 },
                     SettingsManager::Setting::TitleBarMode, defaults, makeConnections);
    // windowresizemode
    syncComboBox(ui->windowResizeComboBox, SettingsManager::Setting::WindowResizeMode, defaults,
                 makeConnections);
    windowResizeComboBoxCurrentIndexChanged(ui->windowResizeComboBox->currentIndex());
    // minwindowresizedpercentage
    syncSpinBox(ui->minWindowResizeSpinBox, SettingsManager::Setting::MinWindowResizedPercentage,
                defaults, makeConnections);
    // maxwindowresizedperecentage
    syncSpinBox(ui->maxWindowResizeSpinBox, SettingsManager::Setting::MaxWindowResizedPercentage,
                defaults, makeConnections);
    // hidetitlebar
    syncCheckbox(ui->hideTitlebarCheckbox, SettingsManager::Setting::HideTitlebar, defaults,
                 makeConnections);
    // forcedarkmode
    syncCheckbox(ui->forceDarkModeCheckbox, SettingsManager::Setting::ForceDarkMode, defaults,
                 makeConnections);
    // quitonlastwindow
    syncCheckbox(ui->quitOnLastWindowCheckbox, SettingsManager::Setting::QuitOnLastWindow, defaults,
                 makeConnections);
    // ctrldragwindow
    syncCheckbox(ui->ctrlDragCheckbox, SettingsManager::Setting::CtrlDragWindow, defaults,
                 makeConnections);
    // frameless
    syncCheckbox(ui->framelessCheckbox, SettingsManager::Setting::Frameless, defaults,
                 makeConnections);
    // menubarenabled
    syncCheckbox(ui->menubarCheckbox, SettingsManager::Setting::MenuBarEnabled, defaults,
                 makeConnections);
    // fullscreendetails
    syncCheckbox(ui->detailsInFullscreen, SettingsManager::Setting::FullScreenDetails, defaults,
                 makeConnections);
    // filteringenabled
    syncCheckbox(ui->filteringCheckbox, SettingsManager::Setting::FilteringEnabled, defaults,
                 makeConnections);
    // scalingenabled
    syncCheckbox(ui->scalingCheckbox, SettingsManager::Setting::ScalingEnabled, defaults,
                 makeConnections);
    ui->scalingTwoCheckbox->setEnabled(ui->scalingCheckbox->isChecked());
    // scalingtwoenabled
    syncCheckbox(ui->scalingTwoCheckbox, SettingsManager::Setting::ScalingTwoEnabled, defaults,
                 makeConnections);
    // scalefactor
    syncSpinBox(ui->scaleFactorSpinBox, SettingsManager::Setting::ScaleFactor, defaults,
                makeConnections);
    // scrollzoom
    syncComboBox(ui->scrollZoomsComboBox, SettingsManager::Setting::ScrollZoom, defaults,
                 makeConnections);
    // fractionalzoom
    syncCheckbox(ui->fractionalZoomCheckbox, SettingsManager::Setting::FractionalZoom, defaults,
                 makeConnections);
    // cursorzoom
    syncCheckbox(ui->cursorZoomCheckbox, SettingsManager::Setting::CursorZoom, defaults,
                 makeConnections);
    // cropmode
    syncComboBox(ui->cropModeComboBox, SettingsManager::Setting::CropMode, defaults,
                 makeConnections);
    // pastactualsizeenabled
    syncCheckbox(ui->pastActualSizeCheckbox, SettingsManager::Setting::PastActualSizeEnabled,
                 defaults, makeConnections);
    // colorspaceconversion
    syncComboBox(ui->colorSpaceConversionComboBox, SettingsManager::Setting::ColorSpaceConversion,
                 defaults, makeConnections);
    // language
    syncComboBoxData(ui->langComboBox, SettingsManager::Setting::Language, defaults,
                     makeConnections);
    // sortmode
    syncComboBox(ui->sortComboBox, SettingsManager::Setting::SortMode, defaults, makeConnections);
    // sortdescending
    syncRadioButtons({ ui->descendingRadioButton0, ui->descendingRadioButton1 },
                     SettingsManager::Setting::SortDescending, defaults, makeConnections);
    // preloadingmode
    syncComboBox(ui->preloadingComboBox, SettingsManager::Setting::PreloadingMode, defaults,
                 makeConnections);
    // loopfolders
    syncCheckbox(ui->loopFoldersCheckbox, SettingsManager::Setting::LoopFoldersEnabled, defaults,
                 makeConnections);
    // slideshowreversed
    syncComboBox(ui->slideshowDirectionComboBox, SettingsManager::Setting::SlideshowReversed,
                 defaults, makeConnections);
    // slideshowtimer
    syncDoubleSpinBox(ui->slideshowTimerSpinBox, SettingsManager::Setting::SlideshowTimer, defaults,
                      makeConnections);
    // afterdelete
    syncComboBox(ui->afterDeletionComboBox, SettingsManager::Setting::AfterDelete, defaults,
                 makeConnections);
    // askdelete
    syncCheckbox(ui->askDeleteCheckbox, SettingsManager::Setting::AskDelete, defaults,
                 makeConnections);
    // allowmimecontentdetection
    syncCheckbox(ui->mimeContentDetectionCheckbox,
                 SettingsManager::Setting::AllowMimeContentDetection, defaults, makeConnections);
    // saverecents
    syncCheckbox(ui->saveRecentsCheckbox, SettingsManager::Setting::SaveRecents, defaults,
                 makeConnections);
    // updatenotifications
    syncCheckbox(ui->updateCheckbox, SettingsManager::Setting::UpdateNotifications, defaults,
                 makeConnections);
    // skiphidden
    syncCheckbox(ui->skipHiddenCheckbox, SettingsManager::Setting::SkipHidden, defaults,
                 makeConnections);
}

void QVOptionsDialog::syncCheckbox(QCheckBox *checkbox, SettingsManager::Setting setting,
                                   bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getBool(setting, defaults);
    checkbox->setChecked(val);
    transientSettings.insert(static_cast<int>(setting), val);

    if (makeConnection) {
        connect(checkbox, &QCheckBox::stateChanged, this,
                [this, setting](int arg1) { modifySetting(setting, static_cast<bool>(arg1)); });
    }
}

void QVOptionsDialog::syncRadioButtons(QList<QRadioButton *> buttons,
                                       SettingsManager::Setting setting, bool defaults,
                                       bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInt(setting, defaults);
    buttons.value(val)->setChecked(true);
    transientSettings.insert(static_cast<int>(setting), val);

    if (makeConnection) {
        for (int i = 0; i < buttons.length(); i++) {
            connect(buttons.value(i), &QRadioButton::clicked, this,
                    [this, setting, i] { modifySetting(setting, i); });
        }
    }
}

void QVOptionsDialog::syncComboBox(QComboBox *comboBox, SettingsManager::Setting setting,
                                   bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInt(setting, defaults);
    comboBox->setCurrentIndex(val);
    transientSettings.insert(static_cast<int>(setting), val);

    if (makeConnection) {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, setting](int index) { modifySetting(setting, index); });
    }
}

void QVOptionsDialog::syncComboBoxData(QComboBox *comboBox, SettingsManager::Setting setting,
                                       bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getString(setting, defaults);
    comboBox->setCurrentIndex(comboBox->findData(val));
    transientSettings.insert(static_cast<int>(setting), val);

    if (makeConnection) {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, setting, comboBox](int index) {
                    Q_UNUSED(index)
                    modifySetting(setting, comboBox->currentData());
                });
    }
}

void QVOptionsDialog::syncSpinBox(QSpinBox *spinBox, SettingsManager::Setting setting,
                                  bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInt(setting, defaults);
    spinBox->setValue(val);
    transientSettings.insert(static_cast<int>(setting), val);

    if (makeConnection) {
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, setting](int arg1) { modifySetting(setting, arg1); });
    }
}

void QVOptionsDialog::syncDoubleSpinBox(QDoubleSpinBox *doubleSpinBox,
                                        SettingsManager::Setting setting, bool defaults,
                                        bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getDouble(setting, defaults);
    doubleSpinBox->setValue(val);
    transientSettings.insert(static_cast<int>(setting), val);

    if (makeConnection) {
        connect(doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, setting](double arg1) { modifySetting(setting, arg1); });
    }
}

void QVOptionsDialog::syncShortcuts(bool defaults)
{
    qvApp->getShortcutManager().updateShortcuts();

    transientShortcuts.clear();
    const auto &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    ui->shortcutsTable->setRowCount(shortcutsList.length());

    for (int i = 0; i < shortcutsList.length(); i++) {
        const ShortcutManager::SShortcut &shortcut = shortcutsList.value(i);

        // Add shortcut to transient shortcut list
        if (defaults)
            transientShortcuts.append(shortcut.defaultShortcuts);
        else
            transientShortcuts.append(shortcut.shortcuts);

        // Add shortcut to table widget
        auto *nameItem = new QTableWidgetItem();
        nameItem->setText(shortcut.readableName);
        ui->shortcutsTable->setItem(i, 0, nameItem);

        auto *shortcutsItem = new QTableWidgetItem();
        shortcutsItem->setText(
                ShortcutManager::stringListToReadableString(transientShortcuts.value(i)));
        ui->shortcutsTable->setItem(i, 1, shortcutsItem);
    }
    updateShortcutsTable();
}

void QVOptionsDialog::updateShortcutsTable()
{
    for (int i = 0; i < transientShortcuts.length(); i++) {
        const QStringList &shortcuts = transientShortcuts.value(i);
        ui->shortcutsTable->item(i, 1)->setText(
                ShortcutManager::stringListToReadableString(shortcuts));
    }
    updateButtonBox();
}

void QVOptionsDialog::shortcutCellDoubleClicked(int row, int column)
{
    Q_UNUSED(column)
    auto getTransientShortcutCallback = [this](int index) {
        return transientShortcuts.value(index);
    };
    auto *shortcutDialog = new QVShortcutDialog(row, getTransientShortcutCallback, this);
    connect(shortcutDialog, &QVShortcutDialog::shortcutsListChanged, this,
            [this](int index, const QStringList &stringListShortcuts) {
                transientShortcuts.replace(index, stringListShortcuts);
                updateShortcutsTable();
            });
    shortcutDialog->open();
}

void QVOptionsDialog::buttonBoxClicked(QAbstractButton *button)
{
    auto role = ui->buttonBox->buttonRole(button);
    if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::ApplyRole) {
        saveSettings();
        if (role == QDialogButtonBox::ApplyRole)
            button->setEnabled(false);
    } else if (role == QDialogButtonBox::ResetRole) {
        syncSettings(true);
        syncShortcuts(true);
    }
}

void QVOptionsDialog::updateButtonBox()
{
    QPushButton *defaultsButton = ui->buttonBox->button(QDialogButtonBox::RestoreDefaults);
    QPushButton *applyButton = ui->buttonBox->button(QDialogButtonBox::Apply);
    defaultsButton->setEnabled(false);
    applyButton->setEnabled(false);

    // settings
    const auto keys = transientSettings.keys();
    for (const int key : keys) {
        const auto setting = static_cast<SettingsManager::Setting>(key);
        const auto &transientValue = transientSettings.value(key);
        const auto savedValue = qvApp->getSettingsManager().getSetting(setting);
        const auto defaultValue = qvApp->getSettingsManager().getSetting(setting, true);

        if (transientValue != savedValue)
            applyButton->setEnabled(true);
        if (transientValue != defaultValue)
            defaultsButton->setEnabled(true);
    }

    // shortcuts
    const QList<ShortcutManager::SShortcut> &shortcutsList =
            qvApp->getShortcutManager().getShortcutsList();
    for (int i = 0; i < transientShortcuts.length(); i++) {
        const auto &transientValue = transientShortcuts.value(i);
        QStringList savedValue = shortcutsList.value(i).shortcuts;
        QStringList defaultValue = shortcutsList.value(i).defaultShortcuts;

        if (transientValue != savedValue)
            applyButton->setEnabled(true);
        if (transientValue != defaultValue)
            defaultsButton->setEnabled(true);
    }
}

void QVOptionsDialog::bgColorButtonClicked()
{

    auto *colorDialog = new QColorDialog(ui->bgColorButton->text(), this);
    colorDialog->setWindowModality(Qt::WindowModal);
    connect(colorDialog, &QDialog::accepted, colorDialog, [this, colorDialog] {
        auto selectedColor = colorDialog->currentColor();

        if (!selectedColor.isValid())
            return;

        modifySetting(SettingsManager::Setting::BgColor, selectedColor.name());
        ui->bgColorButton->setText(selectedColor.name());
        updateBgColorButton();
        colorDialog->deleteLater();
    });
    colorDialog->open();
}

void QVOptionsDialog::updateBgColorButton()
{
    QPixmap newPixmap = QPixmap(32, 32);
    newPixmap.fill(ui->bgColorButton->text());
    ui->bgColorButton->setIcon(QIcon(newPixmap));
}

void QVOptionsDialog::bgColorCheckboxStateChanged(int arg1)
{
    setBgColorControlsEnabled(arg1 > 0);

    updateBgColorButton();
}

void QVOptionsDialog::setBgColorControlsEnabled(bool enabled)
{
    ui->bgColorButton->setEnabled(enabled);
    ui->bgColorAlphaLabel->setEnabled(enabled);
    ui->bgColorAlphaSpinBox->setEnabled(enabled);
}

void QVOptionsDialog::scalingCheckboxStateChanged(int arg1)
{
    if (arg1 > 0)
        ui->scalingTwoCheckbox->setEnabled(true);
    else
        ui->scalingTwoCheckbox->setEnabled(false);
}

void QVOptionsDialog::windowResizeComboBoxCurrentIndexChanged(int index)
{
    bool enableRelatedControls = index != 0;
    ui->minWindowResizeLabel->setEnabled(enableRelatedControls);
    ui->minWindowResizeSpinBox->setEnabled(enableRelatedControls);
    ui->maxWindowResizeLabel->setEnabled(enableRelatedControls);
    ui->maxWindowResizeSpinBox->setEnabled(enableRelatedControls);
}

void QVOptionsDialog::populateCategories(int selectedRow)
{
    const int iconSize = 24;
    const int listRightPadding = 3;
    auto addItem = [&](const QChar &iconChar, const QString &text) {
        ui->categoryList->addItem(
                new QListWidgetItem(qvApp->iconFromFont("Material Icons Outlined", iconChar,
                                                        iconSize, devicePixelRatioF()),
                                    text));
    };
    ui->categoryList->setIconSize(QSize(iconSize, iconSize));
    ui->categoryList->setFont(QApplication::font());
    const QString currentStyle = qApp->style()->objectName();
    if (currentStyle.compare("fusion", Qt::CaseInsensitive) == 0
        || currentStyle.compare("macos", Qt::CaseInsensitive) == 0) {
        const QColor textColor = QApplication::palette().color(QPalette::WindowText);
        QPalette palette = ui->categoryList->palette();
        palette.setColor(QPalette::HighlightedText, textColor);
        palette.setColor(QPalette::Highlight,
                         qvApp->getPerceivedBrightness(textColor) > 0.5 ? QColor(0, 65, 127)
                                                                        : QColor(75, 166, 255));
        ui->categoryList->setPalette(palette);
    }
    ui->categoryList->clear();
    addItem(u'\ue069', tr("Window"));
    addItem(u'\ue3f4', tr("Image"));
    addItem(u'\ue429', tr("Miscellaneous"));
    addItem(u'\ue312', tr("Shortcuts"));
    ui->categoryList->setCurrentRow(selectedRow);
    ui->categoryList->setFixedWidth(ui->categoryList->sizeHintForColumn(0)
                                    + ui->categoryList->frameWidth() + listRightPadding);
    int listContentHeight = 2 * ui->categoryList->frameWidth();
    for (int i = 0; i < ui->categoryList->count(); i++)
        listContentHeight += ui->categoryList->sizeHintForRow(i);
    ui->categoryList->setFixedHeight(listContentHeight);
}

void QVOptionsDialog::populateLanguages()
{
    ui->langComboBox->clear();

    ui->langComboBox->addItem(tr("System Language"), "system");

    // Put english at the top seperately because it has no file
    QLocale eng("en");
    ui->langComboBox->addItem("English (en)", "en");

    const auto entries = SettingsManager::translationsDir().entryList(
            { QStringLiteral("qview_*.qm") }, QDir::Files);
    for (auto entry : entries) {
        entry.remove(0, 6);
        entry.remove(entry.length() - 3, 3);
        QLocale locale(entry);

        const QString langString = locale.nativeLanguageName() + " (" + entry + ")";

        ui->langComboBox->addItem(langString, entry);
    }
}

void QVOptionsDialog::languageComboBoxCurrentIndexChanged(int index)
{
    Q_UNUSED(index)
    if (!languageRestartMessageShown) {
        QMessageBox::information(this, tr("Restart Required"),
                                 tr("You must restart qView to change the language."));
        languageRestartMessageShown = true;
    }
}

void QVOptionsDialog::scrollZoomsComboBoxCurrentIndexChanged(int index)
{
    const bool zoomScrollEnabled = index != 2;
    ui->fractionalZoomCheckbox->setEnabled(zoomScrollEnabled);
}
