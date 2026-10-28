#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qvapplication.h"
#include "qvplatformfunctions.h"
#include "qvrenamedialog.h"
#include "windowgeometry.h"

#include <QFileDialog>
#include <QString>
#include <QGraphicsPixmapItem>
#include <QPixmap>
#include <QClipboard>
#include <QCoreApplication>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include <QMovie>
#include <QImageWriter>
#include <QSettings>
#include <QStyle>
#include <QIcon>
#include <QMimeDatabase>
#include <QScreen>
#include <QCursor>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QMenu>
#include <QWindow>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    // Create the window with a translucent surface so the background color's alpha shows the
    // desktop through behind the image. An opaque background color, which is the default, paints
    // over the whole surface and looks exactly like a regular opaque window.
    // The attribute is only read when the native window is created, so it has to be set here
    // instead of in settingsUpdated().
    setAttribute(Qt::WA_TranslucentBackground);

#ifdef COCOA_LOADED
    // Allow the titlebar to overlap widgets with full size content view
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    centralWidget()->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
#endif

    // Initialize variables
    justLaunchedWithImage = false;
    storedWindowState = Qt::WindowNoState;
    openWithNeedsPopulate = true;

    // Initialize graphicsviewkDefaultBufferAlignment
    graphicsView = new QVGraphicsView(this);
    centralWidget()->layout()->addWidget(graphicsView);

    // Make the fullscreen label a floating overlay above the image instead of a layout item
    // that takes up height
    ui->fullscreenLabel->hide();
    ui->fullscreenLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Connect graphicsview signals
    connect(graphicsView, &QVGraphicsView::fileChanged, this, &MainWindow::fileChanged);
    connect(graphicsView, &QVGraphicsView::updatedLoadedPixmapItem, this,
            &MainWindow::setWindowSize);
    connect(graphicsView, &QVGraphicsView::cancelSlideshow, this, &MainWindow::cancelSlideshow);

    // Initialize escape shortcut
    escShortcut = new QShortcut(Qt::Key_Escape, this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (windowState().testFlag(Qt::WindowFullScreen))
            toggleFullScreen();
    });

    // Enable drag&dropping
    setAcceptDrops(true);

    // Make info dialog object
    info = new QVInfoDialog(this);

    // Timer for slideshow
    connect(slideshowController.getTimer(), &QTimer::timeout, this, &MainWindow::slideshowAction);

    // Context menu
    auto &actionManager = qvApp->getActionManager();

    contextMenu = new QMenu(this);

    actionManager.addCloneOfAction(contextMenu, "open");
    actionManager.addCloneOfAction(contextMenu, "openurl");
    contextMenu->addMenu(actionManager.buildRecentsMenu(true, contextMenu));
    contextMenu->addMenu(actionManager.buildOpenWithMenu(contextMenu));
    actionManager.addCloneOfAction(contextMenu, "opencontainingfolder");
    contextMenu->addSeparator();
    actionManager.addCloneOfAction(contextMenu, "rename");
    actionManager.addCloneOfAction(contextMenu, "delete");
    contextMenu->addSeparator();
    actionManager.addCloneOfAction(contextMenu, "nextfile");
    actionManager.addCloneOfAction(contextMenu, "previousfile");
    contextMenu->addSeparator();
    contextMenu->addMenu(actionManager.buildViewMenu(true, contextMenu, true));
    contextMenu->addMenu(actionManager.buildToolsMenu(true, contextMenu, false));
    contextMenu->addMenu(actionManager.buildHelpMenu(true, contextMenu));
    contextMenu->addSeparator();
    actionManager.addCloneOfAction(contextMenu, "options");

    connect(contextMenu, &QMenu::triggered, this, [this](QAction *triggeredAction) {
        ActionManager::actionTriggered(triggeredAction, this);
    });

    // Initialize menubar
    setMenuBar(actionManager.buildMenuBar(this));
    // Stop actions conflicting with the window's actions
    const auto menubarActions = ActionManager::getAllNestedActions(menuBar()->actions());
    for (auto action : menubarActions) {
        action->setShortcutContext(Qt::WidgetShortcut);
    }
    connect(menuBar(), &QMenuBar::triggered, this, [this](QAction *triggeredAction) {
        ActionManager::actionTriggered(triggeredAction, this);
    });

    // Add all actions to this window so keyboard shortcuts are always triggered
    // using virtual menu to hold them so i can connect to the triggered signal
    virtualMenu = new QMenu(this);
    const auto &actionKeys = actionManager.getActionLibrary().keys();
    for (const QString &key : actionKeys) {
        actionManager.addCloneOfAction(virtualMenu, key);
    }
    addActions(virtualMenu->actions());
    connect(virtualMenu, &QMenu::triggered, this, [this](QAction *triggeredAction) {
        ActionManager::actionTriggered(triggeredAction, this);
    });

    // Enable actions related to having a window
    disableActions();

    // Only look up the Open With entries when a menu is opened, so the shell lookup doesn't
    // compete with loading the image
    const auto &openWithMenus = actionManager.getAllClonesOfMenu("openwith", this);
    for (const auto &menu : openWithMenus) {
        connect(menu, &QMenu::aboutToShow, this, &MainWindow::openWithMenuAboutToShow);
    }

    // Connect functions to application components
    connect(&qvApp->getShortcutManager(), &ShortcutManager::shortcutsUpdated, this,
            &MainWindow::shortcutsUpdated);
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this,
            &MainWindow::settingsUpdated);
    settingsUpdated();
    shortcutsUpdated();

    // Timer for delayed-load Open With menu, used as a fallback for programmatic population
    populateOpenWithTimer = new QTimer(this);
    populateOpenWithTimer->setSingleShot(true);
    populateOpenWithTimer->setInterval(1000);
    connect(populateOpenWithTimer, &QTimer::timeout, this,
            &MainWindow::requestPopulateOpenWithMenu);

    // Connection for open with menu population futurewatcher
    connect(&openWithFutureWatcher, &QFutureWatcher<QList<OpenWith::OpenWithItem>>::finished, this,
            [this]() { populateOpenWithMenu(openWithFutureWatcher.result()); });

    // Load window geometry
    QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());

    // Show welcome dialog on first launch
    if (!settings.value("firstlaunch", false).toBool()) {
        settings.setValue("firstlaunch", true);
        settings.setValue("configversion", VERSION);
        qvApp->openWelcomeDialog(this);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowActivate) {
        qvApp->addToLastActiveWindows(this);
    }
    return QMainWindow::event(event);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMainWindow::contextMenuEvent(event);

    // Show native menu on macOS with cocoa framework loaded
#ifdef COCOA_LOADED
    // On regular context menu, recents submenu updates right before it is shown.
    // The native cocoa menu does not update elements until the entire menu is reopened, so we
    // update first
    qvApp->getActionManager().loadRecentsList();
#endif
    QVPlatformFunctions::showContextMenu(contextMenu, event, windowHandle());
}

void MainWindow::showEvent(QShowEvent *event)
{
    // Enable full size content view. With some Qt versions, this can break its restoreGeometry
    // functionality even if we enable this after. Presumably restoreGeometry saves data for
    // further processing after the window is shown, hence the timer here to queue this on the
    // event loop and run after that processing happens.
    QTimer::singleShot(
            0, this, [this]() { QVPlatformFunctions::setFullSizeContentView(windowHandle(), true); });

    QMainWindow::showEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Full size content view can confuse saveGeometry, making it think the window is taller than
    // it really is, so the restored window ends up taller. Turn it off before saving geometry.
    QVPlatformFunctions::setFullSizeContentView(windowHandle(), false);

    QSettings settings;
    settings.setValue("geometry", saveGeometry());

    qvApp->deleteFromLastActiveWindows(this);
    qvApp->getActionManager().untrackClonedActions(contextMenu);
    qvApp->getActionManager().untrackClonedActions(menuBar());
    qvApp->getActionManager().untrackClonedActions(virtualMenu);

    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        const auto *changeEvent = static_cast<QWindowStateChangeEvent *>(event);
        if (windowState().testFlag(Qt::WindowFullScreen)
            != changeEvent->oldState().testFlag(Qt::WindowFullScreen))
            fullscreenChanged();
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    updateFullscreenLabelGeometry();
    QMainWindow::resizeEvent(event);
}

void MainWindow::updateFullscreenLabelGeometry()
{
    if (!ui->fullscreenLabel->isVisible())
        return;

    // Float the label over the image at the top of the window, below the titlebar area on macOS
    ui->fullscreenLabel->setGeometry(0, getTitlebarOverlap(), width(),
                                     ui->fullscreenLabel->sizeHint().height());
    ui->fullscreenLabel->raise();
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MouseButton::BackButton)
        view().goToFile(QVGraphicsView::GoToFileMode::previous);
    else if (event->button() == Qt::MouseButton::ForwardButton)
        view().goToFile(QVGraphicsView::GoToFileMode::next);
    else if (event->button() == Qt::MouseButton::MiddleButton) {
        if (getCurrentFileDetails().isPixmapLoaded && graphicsView->isResetScale())
            toggleFullScreen();
        else
            resetZoom();
    }

    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MouseButton::LeftButton)
        toggleFullScreen();
    QMainWindow::mouseDoubleClickEvent(event);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    const QColor &backgroundColor =
            customBackgroundColor.isValid() ? customBackgroundColor : painter.background().color();

    // Find the top of the viewport to account for the menu bar if it's inside the window
    // and/or the label that displays titlebar text in full screen mode.
    const int viewportY = graphicsView->mapTo(this, QPoint()).y();
    // On macOS, part of the viewport may be additionally covered with the window's translucent
    // titlebar due to full size content view.
    const int unobscuredViewportY = qMax(getTitlebarOverlap(), viewportY);

    // A translucent background color has to replace the pixels of the window's translucent surface
    // instead of blending with whatever is already there, otherwise the desktop would not show
    // through by the amount the color's alpha allows.
    const bool translucentBackgroundColor = backgroundColor.alpha() < 255;
    if (translucentBackgroundColor)
        painter.setCompositionMode(QPainter::CompositionMode_Source);

    // Erase the area above the viewport, i.e. fill it with the painter's default color. With a
    // translucent background color, eraseRect() would paint an opaque color there instead.
    const QRect headerRect = QRect(0, 0, width(), viewportY);
    if (headerRect.isValid()) {
        if (translucentBackgroundColor)
            painter.fillRect(headerRect, backgroundColor);
        else
            painter.eraseRect(headerRect);
    }

    // Fill the viewport with the background color.
    const QRect viewportRect = rect().adjusted(0, viewportY, 0, 0);
    if (viewportRect.isValid()) {
        painter.fillRect(viewportRect, backgroundColor);
    }

    if (translucentBackgroundColor)
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // If there's an error message, draw it centered inside the unobscured area of the viewport.
    const QRect unobscuredViewportRect = rect().adjusted(0, unobscuredViewportY, 0, 0);
    if (getCurrentFileDetails().errorData.hasError && unobscuredViewportRect.isValid()) {
        const QVImageCore::ErrorData &errorData = getCurrentFileDetails().errorData;
        const QString errorMessage =
                tr("Error occurred opening\n%3\n%2 (Error %1)")
                        .arg(QString::number(errorData.errorNum), errorData.errorString,
                             getCurrentFileDetails().fileInfo.fileName());
        painter.setFont(font());
        painter.setPen(QVApplication::getPerceivedBrightness(backgroundColor) > 0.5 ? Qt::black
                                                                                    : Qt::white);
        painter.drawText(unobscuredViewportRect, errorMessage, QTextOption(Qt::AlignCenter));
    }
    QMainWindow::paintEvent(event);
}

void MainWindow::fullscreenChanged()
{
    const bool isFullscreen = windowState().testFlag(Qt::WindowFullScreen);
    const auto fullscreenActions =
            qvApp->getActionManager().getAllClonesOfAction("fullscreen", this);
    for (const auto &fullscreenAction : fullscreenActions) {
        fullscreenAction->setText(isFullscreen ? tr("Exit F&ull Screen")
                                               : tr("Enter F&ull Screen"));
        fullscreenAction->setIcon(isFullscreen ? QIcon::fromTheme("view-restore")
                                               : QIcon::fromTheme("view-fullscreen"));
    }
    ui->fullscreenLabel->setVisible(
            isFullscreen
            && qvApp->getSettingsManager().getBool(SettingsManager::Setting::FullScreenDetails));
    updateFullscreenLabelGeometry();
}

void MainWindow::openFile(const QString &fileName)
{
    graphicsView->loadFile(fileName);
    cancelSlideshow();
}

void MainWindow::settingsUpdated()
{
    auto &settingsManager = qvApp->getSettingsManager();

    updateWindowTitle();

    // Cache custom background color since we read it in paintEvent
    customBackgroundColor = settingsManager.getBool(SettingsManager::Setting::BgColorEnabled)
            ? QColor(settingsManager.getString(SettingsManager::Setting::BgColor))
            : QColor();

    // Store the background opacity (0-100%) in the color's alpha channel. The window is
    // already created translucent, so repainting is all it takes for the change to take effect.
    if (customBackgroundColor.isValid()) {
        const int bgColorAlpha =
                qBound(0, settingsManager.getInt(SettingsManager::Setting::BgColorAlpha), 100);
        customBackgroundColor.setAlpha(qRound(bgColorAlpha * 255.0 / 100.0));
    }

    // menubarenabled
    bool menuBarEnabled = settingsManager.getBool(SettingsManager::Setting::MenuBarEnabled);
#ifdef Q_OS_MACOS
    // Menu bar is effectively always enabled on macOS
    menuBarEnabled = true;
#endif
    menuBar()->setVisible(menuBarEnabled);

    // frameless
    updateFrameless();
    const bool frameless = settingsManager.getBool(SettingsManager::Setting::Frameless);
    for (const auto &action : qvApp->getActionManager().getAllInstancesOfAction("frameless")) {
        action->setChecked(frameless);
    }

    // titlebaralwaysdark/hidetitlebar
    QVPlatformFunctions::setVibrancy(settingsManager.getBool(SettingsManager::Setting::HideTitlebar),
                                     settingsManager.getBool(SettingsManager::Setting::ForceDarkMode),
                                     windowHandle());
#ifdef COCOA_LOADED
    // quitonlastwindow
    qvApp->setQuitOnLastWindowClosed(
            settingsManager.getBool(SettingsManager::Setting::QuitOnLastWindow));
#endif

    // slideshow timer
    slideshowController.setInterval(static_cast<int>(
            settingsManager.getDouble(SettingsManager::Setting::SlideshowTimer) * 1000));

    ui->fullscreenLabel->setVisible(
            settingsManager.getBool(SettingsManager::Setting::FullScreenDetails)
            && windowState().testFlag(Qt::WindowFullScreen));
    updateFullscreenLabelGeometry();

    setWindowSize();

    // repaint in case background color changed
    update();
}

void MainWindow::updateFrameless()
{
#ifndef Q_OS_MACOS
    const bool frameless = qvApp->getSettingsManager().getBool(SettingsManager::Setting::Frameless);
    if (frameless == windowFlags().testFlag(Qt::FramelessWindowHint))
        return;

    // setWindowFlag() hides the window, so restore the state it had afterwards
    const Qt::WindowStates state = windowState();
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::FramelessWindowHint, frameless);

    if (!wasVisible)
        return;

    if (state.testFlag(Qt::WindowFullScreen))
        showFullScreen();
    else if (state.testFlag(Qt::WindowMaximized))
        showMaximized();
    else
        show();
#endif
}

void MainWindow::shortcutsUpdated()
{
    // If esc is not used in a shortcut, let it exit fullscreen
    escShortcut->setKey(Qt::Key_Escape);

    const auto &actionLibrary = qvApp->getActionManager().getActionLibrary();
    for (const auto &action : actionLibrary) {
        if (action->shortcuts().contains(QKeySequence(Qt::Key_Escape))) {
            escShortcut->setKey({});
            break;
        }
    }
}

void MainWindow::openRecent(int i)
{
    auto recentsList = qvApp->getActionManager().getRecentsList();
    graphicsView->loadFile(recentsList.value(i).filePath);
    cancelSlideshow();
}

void MainWindow::fileChanged()
{
    openWithNeedsPopulate = true;
    populateOpenWithTimer->start();
    disableActions();

    if (info->isVisible())
        refreshProperties();
    updateWindowTitle();
    updateWindowFilePath();

    // repaint to handle error message
    update();
}

void MainWindow::disableActions()
{
    using EnableRule = ActionManager::EnableRule;

    const auto &actionManager = qvApp->getActionManager();
    const auto &enableRules = actionManager.getEnableRules();
    for (auto it = enableRules.constBegin(); it != enableRules.constEnd(); ++it) {
        bool enabled;
        switch (it.value()) {
        case EnableRule::PixmapLoaded:
            enabled = getCurrentFileDetails().isPixmapLoaded;
            break;
        case EnableRule::MovieLoaded:
            enabled = getCurrentFileDetails().isMovieLoaded;
            break;
        case EnableRule::FolderLoaded:
            enabled = !getCurrentFileDetails().folderFileInfoList.isEmpty();
            break;
        case EnableRule::UndoAvailable:
            enabled = fileOperations.canUndo();
            break;
        case EnableRule::Always:
            enabled = true;
            break;
        default:
            enabled = false;
            break;
        }

        for (const auto &clone : actionManager.getAllClonesOfAction(it.key(), this))
            clone->setEnabled(enabled);
    }

    const auto &openWithMenus = actionManager.getAllClonesOfMenu("openwith", this);
    for (const auto &menu : openWithMenus) {
        menu->setEnabled(getCurrentFileDetails().isPixmapLoaded);
    }
}

void MainWindow::requestPopulateOpenWithMenu()
{
    // A search is already in flight, and it will populate the menu once it finishes
    if (openWithFutureWatcher.isRunning())
        return;

    // Read the path here so that the worker doesn't have to touch window state
    const QString currentFilePath = getCurrentFileDetails().fileInfo.absoluteFilePath();
    openWithFutureWatcher.setFuture(QtConcurrent::run(
            [currentFilePath] { return OpenWith::getOpenWithItems(currentFilePath); }));
}

void MainWindow::openWithMenuAboutToShow()
{
    if (!openWithNeedsPopulate)
        return;

    populateOpenWithTimer->stop();
    openWithNeedsPopulate = false;
    requestPopulateOpenWithMenu();
}

void MainWindow::populateOpenWithMenu(const QList<OpenWith::OpenWithItem> openWithItems)
{
    for (int i = 0; i < qvApp->getActionManager().getOpenWithMaxLength(); i++) {
        const auto clonedActions = qvApp->getActionManager().getAllClonesOfAction(
                "openwith" + QString::number(i), this);
        for (const auto &action : clonedActions) {
            // If we are within the bounds of the open with list
            if (i < openWithItems.length()) {
                auto openWithItem = openWithItems.value(i);

                action->setVisible(true);
                action->setIconVisibleInMenu(
                        false); // Hide icon temporarily to speed up updates in certain cases
                action->setText(openWithItem.name);
                if (!openWithItem.iconName.isEmpty())
                    action->setIcon(QIcon::fromTheme(openWithItem.iconName));
                else
                    action->setIcon(openWithItem.icon);
                auto data = action->data().toList();
                data.replace(1, QVariant::fromValue(openWithItem));
                action->setData(data);
                action->setIconVisibleInMenu(true);
            } else {
                action->setVisible(false);
            }
        }
    }
}

void MainWindow::refreshProperties()
{
    int value4;
    if (getCurrentFileDetails().isMovieLoaded)
        value4 = graphicsView->getLoadedMovie().frameCount();
    else
        value4 = 0;
    info->setInfo(getCurrentFileDetails().fileInfo, getCurrentFileDetails().baseImageSize.width(),
                  getCurrentFileDetails().baseImageSize.height(), value4);
}

void MainWindow::updateWindowTitle()
{
    QString newString = "qView";
    if (getCurrentFileDetails().fileInfo.isFile()) {
        switch (qvApp->getSettingsManager().getInt(SettingsManager::Setting::TitleBarMode)) {
        case 1: {
            newString = getCurrentFileDetails().fileInfo.fileName();
            break;
        }
        case 2: {
            newString = QString::number(getCurrentFileDetails().loadedIndexInFolder + 1);
            newString += "/" + QString::number(getCurrentFileDetails().folderFileInfoList.count());
            newString += " - " + getCurrentFileDetails().fileInfo.fileName();
            break;
        }
        case 3: {
            newString = QString::number(getCurrentFileDetails().loadedIndexInFolder + 1);
            newString += "/" + QString::number(getCurrentFileDetails().folderFileInfoList.count());
            newString += " - " + getCurrentFileDetails().fileInfo.fileName();
            if (!getCurrentFileDetails().errorData.hasError) {
                newString += " - " + QString::number(getCurrentFileDetails().baseImageSize.width());
                newString += "x" + QString::number(getCurrentFileDetails().baseImageSize.height());
                newString +=
                        " - " + QVInfoDialog::formatBytes(getCurrentFileDetails().fileInfo.size());
            }
            newString += " - qView";
            break;
        }
        }
    }

    setWindowTitle(newString);

    // Update fullscreen label to titlebar text as well
    ui->fullscreenLabel->setText(newString);
}

void MainWindow::updateWindowFilePath()
{
    if (!windowHandle())
        return;

    const bool shouldPopulate = getCurrentFileDetails().isPixmapLoaded;
    windowHandle()->setFilePath(shouldPopulate ? getCurrentFileDetails().fileInfo.absoluteFilePath()
                                               : "");
}

void MainWindow::setWindowSize()
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return;

    // check if the program is configured to resize the window
    int windowResizeMode =
            qvApp->getSettingsManager().getInt(SettingsManager::Setting::WindowResizeMode);
    if (!(windowResizeMode == 2 || (windowResizeMode == 1 && justLaunchedWithImage)))
        return;

    justLaunchedWithImage = false;

    // check if window is maximized or fullscreened
    if (windowState().testFlag(Qt::WindowMaximized) || windowState().testFlag(Qt::WindowFullScreen))
        return;

    qreal minWindowResizedPercentage =
            qvApp->getSettingsManager().getInt(SettingsManager::Setting::MinWindowResizedPercentage)
            / 100.0;
    qreal maxWindowResizedPercentage =
            qvApp->getSettingsManager().getInt(SettingsManager::Setting::MaxWindowResizedPercentage)
            / 100.0;

    // Use the full-resolution size: while a preview decode is on screen the loaded
    // pixmap is smaller than the actual image
    QSize imageSize = getCurrentFileDetails().fullPixmapSize;
    imageSize -= QSize(4, 4);

#ifdef Q_OS_WIN
    // Windows reports the wrong minimum width, so we constrain the image size relative to the dpi
    // to stop weirdness with tiny images
    const QSize minimumImageSize(qRound(logicalDpiX() * 1.5), logicalDpiY() / 2);
#else
    const QSize minimumImageSize;
#endif

    WindowGeometry::resizeToImage(*this, imageSize, minWindowResizedPercentage,
                                  maxWindowResizedPercentage, minimumImageSize);
}

bool MainWindow::getIsPixmapLoaded() const
{
    return getCurrentFileDetails().isPixmapLoaded;
}

void MainWindow::setJustLaunchedWithImage(bool value)
{
    justLaunchedWithImage = value;
}

void MainWindow::openUrl(const QUrl &url)
{
    urlOpener.openUrl(*this, url);
}

void MainWindow::pickUrl()
{
    urlOpener.pickUrl(*this);
}

void MainWindow::openWith(const OpenWith::OpenWithItem &openWithItem)
{
    OpenWith::openWith(getCurrentFileDetails().fileInfo.absoluteFilePath(), openWithItem);
}

void MainWindow::openContainingFolder()
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return;

    const QFileInfo selectedFileInfo = getCurrentFileDetails().fileInfo;

#ifdef Q_OS_WIN
    QProcess::startDetached(
            "explorer",
            QStringList() << "/select,"
                          << QDir::toNativeSeparators(selectedFileInfo.absoluteFilePath()));
#elif defined Q_OS_MACOS
    QProcess::execute("open", QStringList() << "-R" << selectedFileInfo.absoluteFilePath());
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(selectedFileInfo.absolutePath()));
#endif
}

void MainWindow::showFileInfo()
{
    refreshProperties();
    info->show();
    info->raise();
}

void MainWindow::askDeleteFile(bool permanent)
{
    fileOperations.askDeleteFile(*this, permanent);
}

void MainWindow::deleteFile(bool permanent)
{
    fileOperations.deleteFile(*this, permanent);
}

void MainWindow::undoDelete()
{
    fileOperations.undoDelete(*this);
}

void MainWindow::copy()
{
    auto *mimeData = graphicsView->getMimeData();
    if (!mimeData->hasImage() || !mimeData->hasUrls()) {
        mimeData->deleteLater();
        return;
    }

    QApplication::clipboard()->setMimeData(mimeData);
}

void MainWindow::paste()
{
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    if (mimeData == nullptr)
        return;

    if (mimeData->hasText()) {
        auto url = QUrl(mimeData->text());

        if (url.isValid() && (url.scheme() == "http" || url.scheme() == "https")) {
            openUrl(url);
            return;
        }
    }

    graphicsView->loadMimeData(mimeData);
}

void MainWindow::rename()
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return;

    auto *renameDialog = new QVRenameDialog(this, getCurrentFileDetails().fileInfo);
    connect(renameDialog, &QVRenameDialog::newFileToOpen, this, &MainWindow::openFile);
    connect(renameDialog, &QVRenameDialog::readyToRenameFile, this, [this]() {
        if (auto device = graphicsView->getLoadedMovie().device()) {
            device->close();
        }
    });

    renameDialog->open();
}

void MainWindow::resetZoom()
{
    graphicsView->resetScale();
    graphicsView->restoreWindowSize();
}

void MainWindow::saveFrameAs()
{
    QSettings settings;
    settings.beginGroup("recents");
    if (!getCurrentFileDetails().isMovieLoaded)
        return;

    if (graphicsView->getLoadedMovie().state() == QMovie::Running) {
        pause();
    }
    QFileDialog *saveDialog = new QFileDialog(this, tr("Save Frame As..."));
    saveDialog->setDirectory(settings.value("lastFileDialogDir", QDir::homePath()).toString());
    saveDialog->setNameFilters(qvApp->getNameFilterList());
    saveDialog->selectFile(getCurrentFileDetails().fileInfo.baseName() + "-"
                           + QString::number(graphicsView->getLoadedMovie().currentFrameNumber())
                           + ".png");
    saveDialog->setDefaultSuffix("png");
    saveDialog->setAcceptMode(QFileDialog::AcceptSave);
    saveDialog->open();
    connect(saveDialog, &QFileDialog::fileSelected, this, [=](const QString &fileName) {
        graphicsView->originalSize();
        for (int i = 0; i < graphicsView->getLoadedMovie().frameCount(); i++)
            nextFrame();

        graphicsView->getLoadedMovie().currentPixmap().save(fileName, nullptr, 100);
        graphicsView->resetScale();
    });
}

void MainWindow::pause()
{
    if (!getCurrentFileDetails().isMovieLoaded)
        return;

    const auto pauseActions = qvApp->getActionManager().getAllClonesOfAction("pause", this);

    if (graphicsView->getLoadedMovie().state() == QMovie::Running) {
        graphicsView->setPaused(true);
        for (const auto &pauseAction : pauseActions) {
            pauseAction->setText(tr("Res&ume"));
            pauseAction->setIcon(QIcon::fromTheme("media-playback-start"));
        }
    } else {
        graphicsView->setPaused(false);
        for (const auto &pauseAction : pauseActions) {
            pauseAction->setText(tr("Pause"));
            pauseAction->setIcon(QIcon::fromTheme("media-playback-pause"));
        }
    }
}

void MainWindow::nextFrame()
{
    if (!getCurrentFileDetails().isMovieLoaded)
        return;

    graphicsView->jumpToNextFrame();
}

void MainWindow::toggleSlideshow()
{
    slideshowController.toggle(*this);
}

void MainWindow::cancelSlideshow()
{
    if (slideshowController.isActive())
        toggleSlideshow();
}

void MainWindow::slideshowAction()
{
    if (qvApp->getSettingsManager().getBool(SettingsManager::Setting::SlideshowReversed))
        view().goToFile(QVGraphicsView::GoToFileMode::previous);
    else
        view().goToFile(QVGraphicsView::GoToFileMode::next);
}

void MainWindow::decreaseSpeed()
{
    if (!getCurrentFileDetails().isMovieLoaded)
        return;

    graphicsView->setSpeed(graphicsView->getLoadedMovie().speed() - 25);
}

void MainWindow::resetSpeed()
{
    if (!getCurrentFileDetails().isMovieLoaded)
        return;

    graphicsView->setSpeed(100);
}

void MainWindow::increaseSpeed()
{
    if (!getCurrentFileDetails().isMovieLoaded)
        return;

    graphicsView->setSpeed(graphicsView->getLoadedMovie().speed() + 25);
}

void MainWindow::toggleFullScreen()
{
    // Note: This is only triggered by the menu action, so the logic here should be kept to a
    // minimum. Anything that needs to run even if the window manager initiated the change should be
    // triggered by QEvent::WindowStateChange.

    // Disable updates during window state change to resolve visual glitches on macOS if the
    // titlebar is hidden
    setUpdatesEnabled(false);

    if (windowState().testFlag(Qt::WindowFullScreen)) {
        setWindowState(storedWindowState);
    } else {
        storedWindowState = windowState();

        showFullScreen();
    }

    setUpdatesEnabled(true);
}

int MainWindow::getTitlebarOverlap() const
{
    // To account for fullsizecontentview on mac (returns 0 on other platforms)
    return QVPlatformFunctions::getObscuredHeight(window()->windowHandle());
}
