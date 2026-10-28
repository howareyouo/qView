#include "actionmanager.h"
#include "mainwindow.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"
#include "qvplatformfunctions.h"
#include "openwith.h"
#include "shortcutmanager.h"

#include <QSettings>
#include <QMimeDatabase>
#include <QFileIconProvider>

#include <functional>

ActionManager::ActionManager(QObject *parent) : QObject(parent)
{
    isSaveRecentsEnabled = true;
    recentsListMaxLength = 10;
    openWithMaxLength = 10;

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this,
            &ActionManager::settingsUpdated);
    settingsUpdated();

    initializeActionLibrary();

    recentsSaveTimer = new QTimer(this);
    recentsSaveTimer->setSingleShot(true);
    recentsSaveTimer->setInterval(500);
    connect(recentsSaveTimer, &QTimer::timeout, this, &ActionManager::saveRecentsList);

    loadRecentsList();

#ifdef COCOA_LOADED
    windowMenu = new QMenu(tr("Window"));
    QVCocoaFunctions::setWindowMenu(windowMenu);
#endif
}

ActionManager::~ActionManager()
{
    qDeleteAll(actionLibrary);
}

void ActionManager::settingsUpdated()
{
    isSaveRecentsEnabled = qvApp->getSettingsManager().getBool(SettingsManager::Setting::SaveRecents);

    auto const recentsMenus = menuCloneLibrary.values("recents");
    for (const auto &recentsMenu : recentsMenus) {
        recentsMenu->menuAction()->setVisible(isSaveRecentsEnabled);
    }
    if (!isSaveRecentsEnabled)
        clearRecentsList();
}

QAction *ActionManager::addCloneOfAction(QWidget *parent, const QString &key)
{
    if (auto action = getAction(key)) {
        auto newAction = new QAction(parent);
        newAction->setIcon(action->icon());
        newAction->setData(action->data());
        newAction->setText(action->text());
        newAction->setMenuRole(action->menuRole());
        newAction->setEnabled(action->isEnabled());
        newAction->setCheckable(action->isCheckable());
        newAction->setChecked(action->isChecked());
        newAction->setShortcuts(action->shortcuts());
        newAction->setVisible(action->isVisible());
        actionCloneLibrary.insert(key, newAction);
        parent->addAction(newAction);
        return newAction;
    }

    return nullptr;
}

QAction *ActionManager::getAction(const QString &key) const
{
    if (auto action = actionLibrary.value(key))
        return action;

    return nullptr;
}

QList<QAction *> ActionManager::getAllInstancesOfAction(const QString &key) const
{
    QList<QAction *> listOfActions = getAllClonesOfAction(key);

    if (auto mainAction = getAction(key))
        listOfActions.append(mainAction);

    return listOfActions;
}

QList<QAction *> ActionManager::getAllClonesOfAction(const QString &key) const
{
    return actionCloneLibrary.values(key);
}

QList<QAction *> ActionManager::getAllClonesOfAction(const QString &key, QWidget *parent) const
{
    QList<QAction *> listOfChildActions;
    const auto &actions = getAllClonesOfAction(key);
    for (const auto &action : actions) {
        if (hasAncestor(action, parent)) {
            listOfChildActions.append(action);
        }
    };
    return listOfChildActions;
}

void ActionManager::forEachCloneOfAction(const QString &key,
                                         const std::function<void(QAction *)> &func,
                                         QWidget *parent) const
{
    const auto cloneList = getAllClonesOfAction(key, parent);
    for (auto *action : cloneList)
        func(action);
}

void ActionManager::forEachInstanceOfAction(const QString &key,
                                            const std::function<void(QAction *)> &func) const
{
    if (auto *action = getAction(key))
        func(action);

    const auto cloneList = getAllClonesOfAction(key);
    for (auto *action : cloneList)
        func(action);
}

QList<QMenu *> ActionManager::getAllClonesOfMenu(const QString &key) const
{
    return menuCloneLibrary.values(key);
}

QList<QMenu *> ActionManager::getAllClonesOfMenu(const QString &key, QWidget *parent) const
{
    QList<QMenu *> listOfChildMenus;
    const auto &menus = getAllClonesOfMenu(key);
    for (const auto &menu : menus) {
        if (hasAncestor(menu->menuAction(), parent)) {
            listOfChildMenus.append(menu);
        }
    };
    return listOfChildMenus;
}

void ActionManager::untrackClonedActions(const QList<QAction *> &actions)
{
    for (const auto &action : actions) {
        if (auto menu = action->menu()) {
            QString key = action->data().toString();
            if (key.length() != 0)
                menuCloneLibrary.remove(key, menu);
        } else {
            QString key = action->data().toStringList().first();
            actionCloneLibrary.remove(key, action);
        }
    }
}

void ActionManager::untrackClonedActions(const QMenu *menu)
{
    untrackClonedActions(getAllNestedActions(menu->actions()));
}

void ActionManager::untrackClonedActions(const QMenuBar *menuBar)
{
    untrackClonedActions(getAllNestedActions(menuBar->actions()));
}

void ActionManager::hideAllInstancesOfAction(const QString &key)
{
    forEachInstanceOfAction(key, [](QAction *action) { action->setVisible(false); });
}

QMenuBar *ActionManager::buildMenuBar(QWidget *parent)
{
    // Global menubar
    auto *menuBar = new QMenuBar(parent);

    // Beginning of file menu
    auto *fileMenu = new QMenu(tr("&File"), menuBar);

#ifdef Q_OS_MACOS
    addCloneOfAction(fileMenu, "newwindow");
#endif
    addCloneOfAction(fileMenu, "open");
    addCloneOfAction(fileMenu, "openurl");
    fileMenu->addMenu(buildRecentsMenu(true, fileMenu));
    addCloneOfAction(fileMenu, "reloadfile");
    fileMenu->addSeparator();
#ifdef Q_OS_MACOS
    fileMenu->addSeparator();
    addCloneOfAction(fileMenu, "closewindow");
    addCloneOfAction(fileMenu, "closeallwindows");
#endif
    QVPlatformFunctions::setAlternate(fileMenu, fileMenu->actions().length() - 1);
    fileMenu->addSeparator();
    fileMenu->addMenu(buildOpenWithMenu(fileMenu));
    addCloneOfAction(fileMenu, "opencontainingfolder");
    addCloneOfAction(fileMenu, "showfileinfo");
    fileMenu->addSeparator();
    addCloneOfAction(fileMenu, "quit");

    menuBar->addMenu(fileMenu);
    // End of file menu

    // Beginning of edit menu
    auto *editMenu = new QMenu(tr("&Edit"), menuBar);

    addCloneOfAction(editMenu, "undo");
    editMenu->addSeparator();
    addCloneOfAction(editMenu, "copy");
    addCloneOfAction(editMenu, "paste");
    addCloneOfAction(editMenu, "rename");
    editMenu->addSeparator();
    addCloneOfAction(editMenu, "delete");
    addCloneOfAction(editMenu, "deletepermanent");
    QVPlatformFunctions::setAlternate(editMenu, editMenu->actions().length() - 1);

    menuBar->addMenu(editMenu);
    // End of edit menu

    // Beginning of view menu
    menuBar->addMenu(buildViewMenu(false, menuBar));
    // End of view menu

    // Beginning of go menu
    auto *goMenu = new QMenu(tr("&Go"), menuBar);

    addCloneOfAction(goMenu, "firstfile");
    addCloneOfAction(goMenu, "previousfile");
    addCloneOfAction(goMenu, "nextfile");
    addCloneOfAction(goMenu, "lastfile");
    addCloneOfAction(goMenu, "randomfile");

    menuBar->addMenu(goMenu);
    // End of go menu

    // Beginning of tools menu
    menuBar->addMenu(buildToolsMenu(false, menuBar));
    // End of tools menu

    // Beginning of window menu
#ifdef COCOA_LOADED
    menuBar->addMenu(windowMenu);
#endif
    // End of window menu

    // Beginning of help menu
    menuBar->addMenu(buildHelpMenu(false, menuBar));
    // End of help menu

    return menuBar;
}

QMenu *ActionManager::buildViewMenu(bool addIcon, QWidget *parent, bool includeFileInfo)
{
    auto *viewMenu = new QMenu(tr("&View"), parent);
    viewMenu->menuAction()->setData("view");
    if (addIcon)
        viewMenu->setIcon(QIcon::fromTheme("zoom-fit-best"));

    addCloneOfAction(viewMenu, "zoomin");
    addCloneOfAction(viewMenu, "zoomout");
    addCloneOfAction(viewMenu, "resetzoom");
    addCloneOfAction(viewMenu, "originalsize");
    viewMenu->addSeparator();
    addCloneOfAction(viewMenu, "rotateright");
    addCloneOfAction(viewMenu, "rotateleft");
    viewMenu->addSeparator();
    addCloneOfAction(viewMenu, "mirror");
    addCloneOfAction(viewMenu, "flip");
    viewMenu->addSeparator();
    addCloneOfAction(viewMenu, "fullscreen");
#ifndef Q_OS_MACOS
    addCloneOfAction(viewMenu, "frameless");
#endif
    if (includeFileInfo) {
        viewMenu->addSeparator();
        addCloneOfAction(viewMenu, "showfileinfo");
    }

    menuCloneLibrary.insert(viewMenu->menuAction()->data().toString(), viewMenu);
    return viewMenu;
}

QMenu *ActionManager::buildToolsMenu(bool addIcon, QWidget *parent, bool includeOptions)
{
    auto *toolsMenu = new QMenu(tr("&Tools"), parent);
    toolsMenu->menuAction()->setData("tools");
    if (addIcon)
        toolsMenu->setIcon(QIcon::fromTheme("configure", QIcon::fromTheme("preferences-other")));

    addCloneOfAction(toolsMenu, "saveframeas");
    addCloneOfAction(toolsMenu, "pause");
    addCloneOfAction(toolsMenu, "nextframe");
    toolsMenu->addSeparator();
    addCloneOfAction(toolsMenu, "decreasespeed");
    addCloneOfAction(toolsMenu, "resetspeed");
    addCloneOfAction(toolsMenu, "increasespeed");
    toolsMenu->addSeparator();
    addCloneOfAction(toolsMenu, "slideshow");
    if (includeOptions)
        addCloneOfAction(toolsMenu, "options");

    menuCloneLibrary.insert(toolsMenu->menuAction()->data().toString(), toolsMenu);
    return toolsMenu;
}

QMenu *ActionManager::buildHelpMenu(bool addIcon, QWidget *parent)
{
    auto *helpMenu = new QMenu(tr("&Help"), parent);
    helpMenu->menuAction()->setData("help");
    if (addIcon)
        helpMenu->setIcon(QIcon::fromTheme("help-about"));

    addCloneOfAction(helpMenu, "about");
    addCloneOfAction(helpMenu, "welcome");

    menuCloneLibrary.insert(helpMenu->menuAction()->data().toString(), helpMenu);
    return helpMenu;
}

QMenu *ActionManager::buildRecentsMenu(bool includeClearAction, QWidget *parent)
{
    auto *recentsMenu = new QMenu(tr("Open &Recent"), parent);
    recentsMenu->menuAction()->setData("recents");
    recentsMenu->setIcon(QIcon::fromTheme("document-open-recent"));

    connect(recentsMenu, &QMenu::aboutToShow, this, [this] { this->loadRecentsList(); });

    for (int i = 0; i < recentsListMaxLength; i++) {
        auto action = new QAction(tr("Empty"), recentsMenu);
        action->setVisible(false);
        action->setIconVisibleInMenu(true);
        action->setData("recent" + QString::number(i));

        recentsMenu->addAction(action);
        actionCloneLibrary.insert(action->data().toStringList().first(), action);
    }

    if (includeClearAction) {
        recentsMenu->addSeparator();
        addCloneOfAction(recentsMenu, "clearrecents");
    }

    menuCloneLibrary.insert(recentsMenu->menuAction()->data().toString(), recentsMenu);
    updateRecentsMenu();
    // update settings whenever recent menu is created so it can possibly be hidden
    settingsUpdated();
    return recentsMenu;
}

void ActionManager::loadRecentsList()
{
    // Prevents weird bugs when opening the recent menu while the save timer is still running
    if (recentsSaveTimer->isActive())
        return;

    QSettings settings;
    settings.beginGroup("recents");

    QVariantList variantListRecents = settings.value("recentFiles").toList();
    recentsList = variantListToRecentsList(variantListRecents);

    auditRecentsList();
}

void ActionManager::saveRecentsList()
{
    auditRecentsList();

    QSettings settings;
    settings.beginGroup("recents");

    auto variantList = recentsListToVariantList(recentsList);

    settings.setValue("recentFiles", variantList);
}

void ActionManager::addFileToRecentsList(const QFileInfo &file)
{
    recentsList.prepend({ file.fileName(), file.filePath() });
    auditRecentsList();
    recentsSaveTimer->start();
}

void ActionManager::auditRecentsList()
{
    // This function should be called whenever there is a change to recentsList,
    // and take care not to call any functions that call it.
    if (!isSaveRecentsEnabled) {
        recentsList.clear();
    }

    for (int i = 0; i < recentsList.length(); i++) {
        // Ensure each file exists
        auto recent = recentsList.value(i);
        if (!QFileInfo::exists(recent.filePath)) {
            recentsList.removeAt(i);
        }

        // Check for duplicates
        for (int i2 = 0; i2 < recentsList.length(); i2++) {
            if (i == i2)
                continue;

            if (recent == recentsList.value(i2)) {
                recentsList.removeAt(i2);
            }
        }
    }

    while (recentsList.size() > recentsListMaxLength) {
        recentsList.removeLast();
    }

    updateRecentsMenu();
}

void ActionManager::clearRecentsList()
{
    recentsList.clear();
    saveRecentsList();
}

void ActionManager::updateRecentsMenu()
{
    updateIndexedCloneActions("recent", recentsListMaxLength, recentsList.length(),
                              [this](int i, QAction *action) {
                                  const auto recent = recentsList.value(i);
                                  action->setText(recent.fileName);

#if defined Q_OS_UNIX && !defined Q_OS_MACOS
                                  // set icons for linux users
                                  QMimeDatabase mimedb;
                                  QMimeType type = mimedb.mimeTypeForFile(recent.filePath);
                                  action->setIcon(QIcon::fromTheme(
                                          type.iconName(),
                                          QIcon::fromTheme(type.genericIconName())));
#else
                                  // set icons for mac/windows users
                                  QFileInfo fileInfo(recent.filePath);
                                  QFileIconProvider provider;
                                  QIcon icon = provider.icon(fileInfo);
#  ifdef Q_OS_MACOS
                                  // Workaround for native menu slowness; also required so no full-size native icon
                                  // ever reaches a menu item (see getCacheableMenuIcon)
                                  const QString cacheKey = !fileInfo.suffix().isEmpty()
                                          ? "filetype:" + fileInfo.suffix()
                                          : "filepath:" + recent.filePath;
                                  icon = getCacheableMenuIcon(cacheKey, icon);
#  endif
                                  action->setIcon(icon);
#endif
                              });
    emit recentsMenuUpdated();
}

void ActionManager::updateIndexedCloneActions(
        const QString &prefix, int max, int itemCount,
        const std::function<void(int i, QAction *action)> &populate)
{
    for (int i = 0; i < max; i++) {
        const auto values = actionCloneLibrary.values(prefix + QString::number(i));
        for (const auto &action : values) {
            // If we are within the bounds of the item list
            if (i >= itemCount) {
                action->setVisible(false);
                continue;
            }

            action->setVisible(true);
            action->setIconVisibleInMenu(
                    false); // Hide icon temporarily to speed up updates in certain cases
            populate(i, action);
            action->setIconVisibleInMenu(true);
        }
    }
}

QMenu *ActionManager::buildOpenWithMenu(QWidget *parent)
{
    auto *openWithMenu = new QMenu(tr("Open With"), parent);
    openWithMenu->menuAction()->setData("openwith");
    openWithMenu->setIcon(QIcon::fromTheme("system-run"));
    openWithMenu->setDisabled(true);

    for (int i = 0; i < openWithMaxLength; i++) {

        auto action = new QAction(tr("Empty"), openWithMenu);
        action->setVisible(false);
        action->setIconVisibleInMenu(true);
        action->setData(QVariantList({ "openwith" + QString::number(i), "" }));

        openWithMenu->addAction(action);
        actionCloneLibrary.insert(action->data().toStringList().first(), action);
        // Some madness that will show or hide separator if an item marked as default is in the
        // first position
        if (i == 0) {
            connect(action, &QAction::changed, action, [action, openWithMenu] {
                // If this menu item is default
                if (action->data().toList().at(1).value<OpenWith::OpenWithItem>().isDefault) {
                    if (!openWithMenu->actions().at(1)->isSeparator())
                        openWithMenu->insertSeparator(openWithMenu->actions().at(1));
                } else {
                    if (openWithMenu->actions().at(1)->isSeparator())
                        openWithMenu->removeAction(openWithMenu->actions().at(1));
                }
            });
        }
    }

    openWithMenu->addSeparator();
    addCloneOfAction(openWithMenu, "openwithother");

    menuCloneLibrary.insert(openWithMenu->menuAction()->data().toString(), openWithMenu);
    return openWithMenu;
}

namespace {
const ActionManager::ActionDefinition *findDefinition(const QString &key)
{
    for (const auto &definition : ActionManager::definitions()) {
        if (key == definition.key)
            return &definition;
    }
    return nullptr;
}
} // namespace

void ActionManager::actionTriggered(QAction *triggeredAction)
{
    const auto key = triggeredAction->data().toStringList().first();

    // Actions that may run without a relevant window skip the window lookup
    if (const auto *definition = findDefinition(key); definition && !definition->requiresWindow) {
        actionTriggered(triggeredAction, nullptr);
        return;
    }

    // If some actions are triggered without an explicit window, we want
    // to give them a window without an image open
    const bool shouldBeEmpty = key.startsWith("recent") || key == "openurl";

    if (auto *window = qvApp->getMainWindow(shouldBeEmpty))
        actionTriggered(triggeredAction, window);
}

void ActionManager::actionTriggered(QAction *triggeredAction, MainWindow *relevantWindow)
{
    const auto key = triggeredAction->data().toStringList().first();

    if (const auto *definition = findDefinition(key)) {
        if (definition->run && (!definition->requiresWindow || relevantWindow))
            definition->run(relevantWindow, triggeredAction);
        return;
    }

    if (!relevantWindow)
        return;

    if (key.startsWith("recent")) {
        QChar finalChar = key.at(key.length() - 1);
        relevantWindow->openRecent(finalChar.digitValue());
    } else if (key.startsWith("openwith")) {
        const auto &openWithItem =
                triggeredAction->data().toList().at(1).value<OpenWith::OpenWithItem>();
        relevantWindow->openWith(openWithItem);
    }
}

const QList<ActionManager::ActionDefinition> &ActionManager::definitions()
{
    static const QList<ActionDefinition> definitions = {
        // File
        { "open",
          [] { return new QAction(QIcon::fromTheme("document-open"), tr("&Open...")); },
          [](MainWindow *window, QAction *) { qvApp->pickFile(window); },
          false,
          [] { return ShortcutManager::tr("Open"); },
          [] { return ShortcutManager::keyBindingsToStringList(QKeySequence::Open); } },
        { "openurl",
          [] {
              return new QAction(
                      QIcon::fromTheme("document-open-remote", QIcon::fromTheme("folder-remote")),
                      tr("Open &URL..."));
          },
          [](MainWindow *window, QAction *) { window->pickUrl(); },
          true,
          [] { return ShortcutManager::tr("Open URL"); },
          [] { return QStringList(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O).toString()); } },
        { "reloadfile",
          [] {
              auto *action = new QAction(QIcon::fromTheme("view-refresh"), tr("Re&load File"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->view().reloadFile(); },
          true,
          [] { return ShortcutManager::tr("Reload File"); },
          [] { return ShortcutManager::keyBindingsToStringList(QKeySequence::Refresh); } },
        { "opencontainingfolder",
          [] {
              auto *action = new QAction(QIcon::fromTheme("document-open"),
                                         tr("Open Containing &Folder"));
#ifdef Q_OS_WIN
              //: Open containing folder on windows
              action->setText(tr("Show in E&xplorer"));
#elif defined Q_OS_MACOS
              //: Open containing folder on macOS
              action->setText(tr("Show in &Finder"));
#endif
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->openContainingFolder(); },
          true,
          [] {
#ifdef Q_OS_WIN
              return ShortcutManager::tr("Show in Explorer");
#elif defined Q_OS_MACOS
              return ShortcutManager::tr("Show in Finder");
#else
              return ShortcutManager::tr("Open Containing Folder");
#endif
          } },
        { "showfileinfo",
          [] {
              auto *action = new QAction(QIcon::fromTheme("document-properties"),
                                         tr("Show File &Info"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->showFileInfo(); },
          true,
          [] { return ShortcutManager::tr("Show File Info"); },
          [] { return QStringList(QKeySequence(Qt::Key_I).toString()); } },
        { "undo",
          [] {
              auto *action =
                      new QAction(QIcon::fromTheme("edit-undo"), tr("&Restore from Trash"));
#ifdef Q_OS_WIN
              //: Restore from Trash on windows
              action->setText(tr("&Undo Delete"));
#endif
              action->setData({ "undodisable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->undoDelete(); },
          true,
          [] {
#ifdef Q_OS_WIN
              return ShortcutManager::tr("Undo Delete");
#else
              return ShortcutManager::tr("Restore from Trash");
#endif
          },
          [] { return ShortcutManager::keyBindingsToStringList(QKeySequence::Undo); } },
        { "copy",
          [] {
              auto *action = new QAction(QIcon::fromTheme("edit-copy"), tr("&Copy"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->copy(); },
          true,
          [] { return ShortcutManager::tr("Copy"); },
          [] { return ShortcutManager::keyBindingsToStringList(QKeySequence::Copy); } },
        { "paste",
          [] { return new QAction(QIcon::fromTheme("edit-paste"), tr("&Paste")); },
          [](MainWindow *window, QAction *) { window->paste(); },
          true,
          [] { return ShortcutManager::tr("Paste"); },
          [] { return ShortcutManager::keyBindingsToStringList(QKeySequence::Paste); } },
        { "rename",
          [] {
              auto *action = new QAction(
                      QIcon::fromTheme("edit-rename", QIcon::fromTheme("document-properties")),
                      tr("R&ename..."));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->rename(); },
          true,
          [] { return ShortcutManager::tr("Rename"); },
          [] {
              QStringList shortcuts(QKeySequence(Qt::Key_F2).toString());
              // ctrl+r for renaming, unless it conflicts with refresh (i.e. reload file)
              if (!QKeySequence::keyBindings(QKeySequence::Refresh)
                           .contains(QKeySequence(Qt::CTRL | Qt::Key_R)))
                  shortcuts << QKeySequence(Qt::CTRL | Qt::Key_R).toString();
              // cmd+enter for renaming, mac-style
              shortcuts.prepend(QKeySequence(Qt::CTRL | Qt::Key_Return).toString());
              return shortcuts;
          } },
        { "delete",
          [] {
              auto *action = new QAction(QIcon::fromTheme("edit-delete"), tr("&Move to Trash"));
#ifdef Q_OS_WIN
              //: Move to Trash on windows
              action->setText(tr("&Delete"));
#endif
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->askDeleteFile(false); },
          true,
          [] {
#ifdef Q_OS_WIN
              return ShortcutManager::tr("Delete");
#else
              return ShortcutManager::tr("Move to Trash");
#endif
          },
          [] {
              QStringList shortcuts =
                      ShortcutManager::keyBindingsToStringList(QKeySequence::Delete);
              // cmd+backspace for deleting, mac-style
              shortcuts.prepend(QKeySequence(Qt::CTRL | Qt::Key_Backspace).toString());
              return shortcuts;
          } },
        { "deletepermanent",
          [] {
              auto *action =
                      new QAction(QIcon::fromTheme("edit-delete"), tr("Delete Permanently"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->askDeleteFile(true); },
          true,
          [] { return ShortcutManager::tr("Delete Permanently"); },
          [] {
              QStringList shortcuts(QKeySequence(Qt::SHIFT | Qt::Key_Delete).toString());
#ifdef Q_OS_MACOS
              // cmd+option+backspace
              shortcuts.prepend(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Backspace).toString());
#endif
              return shortcuts;
          } },
        // Go
        { "firstfile",
          [] {
              auto *action = new QAction(QIcon::fromTheme("go-first"), tr("&First File"));
              action->setData({ "folderdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) {
              window->view().goToFile(QVGraphicsView::GoToFileMode::first);
          },
          true,
          [] { return ShortcutManager::tr("First File"); },
          [] { return QStringList(QKeySequence(Qt::Key_Home).toString()); } },
        { "previousfile",
          [] {
              auto *action = new QAction(QIcon::fromTheme("go-previous"), tr("Previous Fi&le"));
              action->setData({ "folderdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) {
              window->view().goToFile(QVGraphicsView::GoToFileMode::previous);
          },
          true,
          [] { return ShortcutManager::tr("Previous File"); },
          [] { return QStringList(QKeySequence(Qt::Key_Left).toString()); } },
        { "nextfile",
          [] {
              auto *action = new QAction(QIcon::fromTheme("go-next"), tr("&Next File"));
              action->setData({ "folderdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) {
              window->view().goToFile(QVGraphicsView::GoToFileMode::next);
          },
          true,
          [] { return ShortcutManager::tr("Next File"); },
          [] { return QStringList(QKeySequence(Qt::Key_Right).toString()); } },
        { "lastfile",
          [] {
              auto *action = new QAction(QIcon::fromTheme("go-last"), tr("Las&t File"));
              action->setData({ "folderdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) {
              window->view().goToFile(QVGraphicsView::GoToFileMode::last);
          },
          true,
          [] { return ShortcutManager::tr("Last File"); },
          [] { return QStringList(QKeySequence(Qt::Key_End).toString()); } },
        { "randomfile",
          [] {
              auto *action = new QAction(QIcon::fromTheme("media-playlist-shuffle"),
                                         tr("Rando&m File"));
              action->setData({ "folderdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) {
              window->view().goToFile(QVGraphicsView::GoToFileMode::random);
          },
          true,
          [] { return ShortcutManager::tr("Random File"); },
          [] { return QStringList(QKeySequence(Qt::Key_R).toString()); } },
        // View
        { "zoomin",
          [] {
              auto *action = new QAction(QIcon::fromTheme("zoom-in"), tr("Zoom &In"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->view().zoomIn(); },
          true,
          [] { return ShortcutManager::tr("Zoom In"); },
          [] {
              QStringList shortcuts =
                      ShortcutManager::keyBindingsToStringList(QKeySequence::ZoomIn);
              // Allow zooming with Ctrl + plus like a regular person (without holding shift)
              if (!shortcuts.contains(QKeySequence(Qt::CTRL | Qt::Key_Equal).toString()))
                  shortcuts << QKeySequence(Qt::CTRL | Qt::Key_Equal).toString();
              return shortcuts;
          } },
        { "zoomout",
          [] {
              auto *action = new QAction(QIcon::fromTheme("zoom-out"), tr("Zoom &Out"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->view().zoomOut(); },
          true,
          [] { return ShortcutManager::tr("Zoom Out"); },
          [] { return ShortcutManager::keyBindingsToStringList(QKeySequence::ZoomOut); } },
        { "resetzoom",
          [] {
              auto *action = new QAction(QIcon::fromTheme("zoom-fit-best"), tr("Reset &Zoom"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->resetZoom(); },
          true,
          [] { return ShortcutManager::tr("Reset Zoom"); },
          [] { return QStringList(QKeySequence(Qt::CTRL | Qt::Key_0).toString()); } },
        { "originalsize",
          [] {
              auto *action = new QAction(QIcon::fromTheme("zoom-original"), tr("Ori&ginal Size"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->view().originalSize(); },
          true,
          [] { return ShortcutManager::tr("Original Size"); },
          [] { return QStringList(QKeySequence(Qt::Key_O).toString()); } },
        { "rotateright",
          [] {
              auto *action =
                      new QAction(QIcon::fromTheme("object-rotate-right"), tr("Rotate &Right"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) {
              window->view().rotateImage(90);
              window->resetZoom();
          },
          true,
          [] { return ShortcutManager::tr("Rotate Right"); },
          [] { return QStringList(QKeySequence(Qt::Key_Up).toString()); } },
        { "rotateleft",
          [] {
              auto *action =
                      new QAction(QIcon::fromTheme("object-rotate-left"), tr("Rotate &Left"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) {
              window->view().rotateImage(-90);
              window->resetZoom();
          },
          true,
          [] { return ShortcutManager::tr("Rotate Left"); },
          [] { return QStringList(QKeySequence(Qt::Key_Down).toString()); } },
        { "mirror",
          [] {
              auto *action =
                      new QAction(QIcon::fromTheme("object-flip-horizontal"), tr("&Mirror"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) {
              window->view().scale(-1, 1);
              window->resetZoom();
          },
          true,
          [] { return ShortcutManager::tr("Mirror"); },
          [] { return QStringList(QKeySequence(Qt::Key_F).toString()); } },
        { "flip",
          [] {
              auto *action = new QAction(QIcon::fromTheme("object-flip-vertical"), tr("&Flip"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) {
              window->view().scale(1, -1);
              window->resetZoom();
          },
          true,
          [] { return ShortcutManager::tr("Flip"); },
          [] { return QStringList(QKeySequence(Qt::CTRL | Qt::Key_F).toString()); } },
        { "toggletitlebar",
          [] {
              auto *action = new QAction(tr("Hide Title&bar"));
              action->setData({ "windowdisable" });
              return action;
          } },
        { "fullscreen",
          [] {
              auto *action =
                      new QAction(QIcon::fromTheme("view-fullscreen"), tr("Enter F&ull Screen"));
              action->setMenuRole(QAction::NoRole);
              action->setData({ "windowdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->toggleFullScreen(); },
          true,
          [] { return ShortcutManager::tr("Fullscreen"); },
          [] {
              QStringList shortcuts(QKeySequence(Qt::Key_Return).toString());
              // Alt+enter should also toggle fullscreen, and it has to be bound explicitly because
              // Qt's key bindings only match the numpad enter key
#ifdef Q_OS_WIN
              shortcuts << QKeySequence(Qt::ALT | Qt::Key_Return).toString();
#endif
              return shortcuts;
          } },
#ifndef Q_OS_MACOS
        { "frameless",
          [] {
              auto *action = new QAction(tr("&Frameless Window"));
              action->setCheckable(true);
              action->setChecked(
                      qvApp->getSettingsManager().getBool(SettingsManager::Setting::Frameless));
              action->setMenuRole(QAction::NoRole);
              action->setData({ "windowdisable" });
              return action;
          },
          [](MainWindow *, QAction *triggeredAction) {
              // The frameless setting is global, so there is no need for a relevant window here
              qvApp->getSettingsManager().setSetting("frameless", triggeredAction->isChecked());
          },
          false,
          [] { return ShortcutManager::tr("Frameless Window"); } },
#endif
        // Tools
        { "saveframeas",
          [] {
              auto *action =
                      new QAction(QIcon::fromTheme("document-save-as"), tr("Save Frame &As..."));
              action->setData({ "gifdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->saveFrameAs(); },
          true,
          [] { return ShortcutManager::tr("Save Frame As"); },
          [] { return ShortcutManager::keyBindingsToStringList(QKeySequence::Save); } },
        { "pause",
          [] {
              auto *action = new QAction(QIcon::fromTheme("media-playback-pause"), tr("Pa&use"));
              action->setData({ "gifdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->pause(); },
          true,
          [] { return ShortcutManager::tr("Pause"); },
          [] { return QStringList(QKeySequence(Qt::Key_P).toString()); } },
        { "nextframe",
          [] {
              auto *action = new QAction(QIcon::fromTheme("media-skip-forward"), tr("&Next Frame"));
              action->setData({ "gifdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->nextFrame(); },
          true,
          [] { return ShortcutManager::tr("Next Frame"); },
          [] { return QStringList(QKeySequence(Qt::Key_N).toString()); } },
        { "decreasespeed",
          [] {
              auto *action = new QAction(QIcon::fromTheme("media-seek-backward"),
                                         tr("&Decrease Speed"));
              action->setData({ "gifdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->decreaseSpeed(); },
          true,
          [] { return ShortcutManager::tr("Decrease Speed"); },
          [] { return QStringList(QKeySequence(Qt::Key_BracketLeft).toString()); } },
        { "resetspeed",
          [] {
              auto *action =
                      new QAction(QIcon::fromTheme("media-playback-start"), tr("&Reset Speed"));
              action->setData({ "gifdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->resetSpeed(); },
          true,
          [] { return ShortcutManager::tr("Reset Speed"); },
          [] { return QStringList(QKeySequence(Qt::Key_Backslash).toString()); } },
        { "increasespeed",
          [] {
              auto *action = new QAction(QIcon::fromTheme("media-skip-forward"),
                                         tr("&Increase Speed"));
              action->setData({ "gifdisable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->increaseSpeed(); },
          true,
          [] { return ShortcutManager::tr("Increase Speed"); },
          [] { return QStringList(QKeySequence(Qt::Key_BracketRight).toString()); } },
        { "slideshow",
          [] {
              auto *action = new QAction(QIcon::fromTheme("media-playback-start"),
                                         tr("Start S&lideshow"));
              action->setData({ "disable" });
              return action;
          },
          [](MainWindow *window, QAction *) { window->toggleSlideshow(); },
          true,
          [] { return ShortcutManager::tr("Toggle Slideshow"); } },
        // Help and application
        { "options",
          [] {
              //: This is for the options dialog on windows
              auto *action = new QAction(
                      QIcon::fromTheme("configure", QIcon::fromTheme("preferences-other")),
                      tr("&Settings"));
#ifdef Q_OS_MACOS
              if (QOperatingSystemVersion::current()
                  < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13)) {
                  //: This is for the options dialog on older mac versions
                  action->setText(tr("Preference&s..."));
              } else {
                  action->setText(tr("Setting&s..."));
              }
#endif
              return action;
          },
          [](MainWindow *window, QAction *) { qvApp->openOptionsDialog(window); },
          false,
          [] {
#ifdef Q_OS_MACOS
              if (QOperatingSystemVersion::current()
                  < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13))
                  return ShortcutManager::tr("Preferences");
              return ShortcutManager::tr("Settings");
#else
              return ShortcutManager::tr("Settings");
#endif
          },
          [] { return ShortcutManager::keyBindingsToStringList(QKeySequence::Preferences); } },
        { "about",
          [] {
              auto *action = new QAction(QIcon::fromTheme("help-about"), tr("&About"));
#ifdef Q_OS_MACOS
              //: This is for the about dialog on mac
              action->setText(tr("&About qView"));
#endif
              return action;
          },
          [](MainWindow *window, QAction *) { qvApp->openAboutDialog(window); },
          false },
        { "welcome",
          [] {
              return new QAction(QIcon::fromTheme("help-faq", QIcon::fromTheme("help-about")),
                                 tr("&Welcome"));
          },
          [](MainWindow *window, QAction *) { qvApp->openWelcomeDialog(window); },
          false },
        { "clearrecents",
          [] {
              //: This is for clearing the recents menu
              return new QAction(QIcon::fromTheme("edit-delete"), tr("Clear &Menu"));
          },
          [](MainWindow *, QAction *) { qvApp->getActionManager().clearRecentsList(); },
          false },
        { "openwithother",
          [] {
              //: Open with other program for unix non-mac
              auto *action =
                      new QAction(QIcon::fromTheme("system-run"), tr("Other Application..."));
#ifdef Q_OS_WIN
              //: Open with other program for windows
              action->setText(tr("Choose another app"));
#elif defined Q_OS_MACOS
              //: Open with other program for macos
              action->setText(tr("Other..."));
#endif
              return action;
          },
          [](MainWindow *window, QAction *) { OpenWith::showOpenWithDialog(window); } },
        // Mac exclusive
        { "newwindow",
          [] { return new QAction(QIcon::fromTheme("window-new"), tr("New Window")); },
          [](MainWindow *, QAction *) { qvApp->newWindow(); },
          false,
#ifdef Q_OS_MACOS
          [] { return ShortcutManager::tr("New Window"); },
          [] { return ShortcutManager::keyBindingsToStringList(QKeySequence::New); }
#endif
        },
        { "closewindow",
          [] { return new QAction(QIcon::fromTheme("window-close"), tr("Close Window")); },
          [](MainWindow *, QAction *) {
              auto *active = QApplication::activeWindow();
              QVPlatformFunctions::closeWindow(active->windowHandle());
              active->close();
          },
          false,
#ifdef Q_OS_MACOS
          [] { return ShortcutManager::tr("Close Window"); },
          [] { return QStringList(QKeySequence(Qt::CTRL | Qt::Key_W).toString()); }
#endif
        },
        { "closeallwindows",
          [] {
              //: Close all windows, that is
              return new QAction(QIcon::fromTheme("window-close"), tr("Close All"));
          },
          [](MainWindow *, QAction *) {
              const auto topLevelWindows = QApplication::topLevelWindows();
              for (auto *window : topLevelWindows) {
                  QVPlatformFunctions::closeWindow(window);
                  window->close();
              }
          },
          false,
#ifdef Q_OS_MACOS
          [] { return ShortcutManager::tr("Close All"); },
          [] { return QStringList(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_W).toString()); }
#endif
        },
        { "quit",
          [] {
              auto *action = new QAction(QIcon::fromTheme("application-exit"), tr("&Quit"));
#ifdef Q_OS_WIN
              //: The quit action is called "Exit" on windows
              action->setText(tr("Exit"));
#endif
              return action;
          },
          [](MainWindow *, QAction *) { QCoreApplication::quit(); },
          false,
          [] {
#ifdef Q_OS_WIN
              return ShortcutManager::tr("Exit");
#else
              return ShortcutManager::tr("Quit");
#endif
          },
          [] {
              QStringList shortcuts = ShortcutManager::keyBindingsToStringList(QKeySequence::Quit);
#ifndef Q_OS_MACOS
              shortcuts << QKeySequence(Qt::CTRL | Qt::Key_W).toString();
#endif
              return shortcuts;
          } },
    };
    return definitions;
}

namespace {
ActionManager::EnableRule enableRuleFromTag(const QString &tag)
{
    if (tag == "gifdisable")
        return ActionManager::EnableRule::MovieLoaded;
    if (tag == "folderdisable")
        return ActionManager::EnableRule::FolderLoaded;
    if (tag == "undodisable")
        return ActionManager::EnableRule::UndoAvailable;
    if (tag == "windowdisable")
        return ActionManager::EnableRule::Always;
    return ActionManager::EnableRule::PixmapLoaded;
}
} // namespace

void ActionManager::initializeActionLibrary()
{
    for (const auto &definition : definitions()) {
        auto *action = definition.create();
        auto data = action->data().toStringList();
        data.prepend(definition.key);
        action->setData(data);

        if (data.last().contains("disable")) {
            action->setEnabled(false);
            actionEnableRules.insert(definition.key, enableRuleFromTag(data.last()));
        }

        actionLibrary.insert(definition.key, action);
    }
}

QIcon ActionManager::getCacheableMenuIcon(const QString &cacheKey, const QIcon &icon)
{
    static QMutex mutex;
    static QCache<QString, QIcon> cache;
    QMutexLocker locker(&mutex);
    QIcon *cacheEntry = cache.take(cacheKey);
    if (cacheEntry == nullptr) {
        cacheEntry = new QIcon();
        // Depending on the source icon's implementation (e.g. if it's backed by a file engine), it
        // may not allow pixmap caching, so get a pixmap of each size once and copy it to a generic
        // QIcon.
        //
        // Only materialize the sizes a menu actually renders (16 pt; 32 px covers retina). Native
        // file/app icons carry reps up to 1024×1024, and QCocoaMenuItem materializes every size of
        // the QIcon into the NSMenuItem's NSImage (~8 MB per item at full size). Combined with
        // Qt's QCocoaMenuItem<->QCocoaNSMenuItem reference cycle, which keeps each closed window's
        // native menu tree alive (Qt 6.11 still), full-size icons accumulate without bound —
        // gigabytes of footprint after a week of opening and closing windows.
        for (const int iconSize : { 16, 32 })
            cacheEntry->addPixmap(icon.pixmap(iconSize, iconSize));
    }
    QIcon cacheableIcon = *cacheEntry;
    cache.insert(cacheKey, cacheEntry);
    return cacheableIcon;
}

bool ActionManager::hasAncestor(QObject *object, QObject *ancestor)
{
    while (object) {
        if (object == ancestor)
            return true;
        object = object->parent();
    }
    return false;
}
