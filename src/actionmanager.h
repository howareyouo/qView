#ifndef ACTIONMANAGER_H
#define ACTIONMANAGER_H

#include <QFileInfo>
#include <QMenuBar>
#include <QMultiHash>

#include <functional>

class MainWindow;

class ActionManager : public QObject
{
    Q_OBJECT
public:
    struct SRecent
    {
        QString fileName;
        QString filePath;

        bool operator==(const SRecent &other) const
        {
            return (fileName == other.fileName && filePath == other.filePath);
        }

        operator QString() const { return "SRecent(" + fileName + ", " + filePath + ")"; }
    };

    // What an action carries in QAction::data(): its key, plus whatever its family keeps beside
    // it. Library actions store the tag that says when they may run, generated entries store the
    // item they stand for. Every read and write of that variant goes through here, so no caller
    // has to know which shape an action happens to use.
    struct Payload
    {
        QString key;
        QVariant item;
    };

    static Payload payloadOf(const QAction *action);

    static void setPayload(QAction *action, const QString &key, const QVariant &item = QVariant());

    static QList<QAction *> getAllNestedActions(const QList<QAction *> &givenActionList)
    {
        QList<QAction *> totalActionList;
        for (const auto &action : givenActionList) {
            if (action->isSeparator())
                continue;

            if (action->menu())
                totalActionList.append(getAllNestedActions(action->menu()->actions()));

            totalActionList << action;
        }
        return totalActionList;
    }

    static QVariantList recentsListToVariantList(const QList<SRecent> &recentsList)
    {
        QVariantList variantList;
        for (const auto &recent : recentsList) {
            QStringList stringList = { recent.fileName, recent.filePath };
            variantList.append(QVariant(stringList));
        }
        return variantList;
    }

    static QList<SRecent> variantListToRecentsList(const QVariantList &variantList)
    {
        QList<SRecent> recentsList;
        for (const auto &variant : variantList) {
            auto stringList = variant.toStringList();
            recentsList.append({ stringList.value(0), stringList.value(1) });
        }
        return recentsList;
    }

    enum class EnableRule
    {
        PixmapLoaded,  // enabled when a pixmap is loaded
        MovieLoaded,   // enabled when a movie is loaded
        FolderLoaded,  // enabled when folder information is available
        UndoAvailable, // enabled when a deletion can be undone
        Always,        // enabled regardless of load state
    };

    // What the current window can do right now, in terms the enable rules are written against.
    // Gathering this is the window's job; deciding which actions may run is this class's.
    struct EnableState
    {
        bool pixmapLoaded = false;
        bool movieLoaded = false;
        bool folderLoaded = false;
        bool undoAvailable = false;
    };

    explicit ActionManager(QObject *parent = nullptr);
    ~ActionManager() override;

    // Single source of truth for every named action: creation, its handler, whether it
    // may run without a relevant window, and its shortcut list entry
    struct ActionDefinition
    {
        const char *key;
        std::function<QAction *()> create;
        std::function<void(MainWindow *, QAction *)> run;
        bool requiresWindow = true;
        std::function<QString()> readableName;
        std::function<QStringList()> defaultShortcuts;
    };

    static const QList<ActionDefinition> &definitions();

    void settingsUpdated();

    QAction *addCloneOfAction(QWidget *parent, const QString &key);

    QAction *getAction(const QString &key) const;

    QList<QAction *> getAllInstancesOfAction(const QString &key) const;

    QList<QAction *> getAllClonesOfAction(const QString &key) const;

    QList<QAction *> getAllClonesOfAction(const QString &key, QWidget *parent) const;

    void forEachCloneOfAction(const QString &key, const std::function<void(QAction *)> &func,
                              QWidget *parent = nullptr) const;

    void forEachInstanceOfAction(const QString &key,
                                 const std::function<void(QAction *)> &func) const;

    // Sets the text and icon every clone of an action shows, for the actions whose wording
    // follows their state (play/stop, pause/resume, enter/exit full screen)
    void setClonePresentation(const QString &key, const QString &text, const QIcon &icon,
                              QWidget *parent = nullptr) const;

    // Turns an enable state into the enabled flags of every action a rule governs
    void applyEnableState(const EnableState &state, QWidget *parent = nullptr) const;

    QList<QMenu *> getAllClonesOfMenu(const QString &key) const;

    QList<QMenu *> getAllClonesOfMenu(const QString &key, QWidget *parent) const;

    void untrackClonedActions(const QList<QAction *> &actions);

    void untrackClonedActions(const QMenu *menu);

    void untrackClonedActions(const QMenuBar *menuBar);

    QMenuBar *buildMenuBar(QWidget *parent = nullptr);

    QMenu *buildViewMenu(bool addIcon = true, QWidget *parent = nullptr,
                         bool includeFileInfo = false);

    QMenu *buildToolsMenu(bool addIcon = true, QWidget *parent = nullptr,
                          bool includeOptions = true);

    QMenu *buildHelpMenu(bool addIcon = true, QWidget *parent = nullptr);

    QMenu *buildRecentsMenu(bool includeClearAction = true, QWidget *parent = nullptr);

    // Every menu built here holds clones, so whatever it triggers has to be routed back to the
    // definition that answers for it. Attaching the menu does that in one place.
    void attachMenu(QMenu *menu, MainWindow *window);

    void attachMenuBar(QMenuBar *menuBar, MainWindow *window);

    void loadRecentsList();

    void saveRecentsList();

    void addFileToRecentsList(const QFileInfo &file);

    void auditRecentsList(bool checkFileExistence = true);

    void clearRecentsList();

    void updateRecentsMenu();

    // Updates every clone of the actions named "prefix0".."prefix{max-1}": clones at or
    // beyond itemCount are hidden, the rest are shown and handed to populate
    void updateIndexedCloneActions(
            const QString &prefix, int max, int itemCount,
            const std::function<void(int i, QAction *action)> &populate);

    QMenu *buildOpenWithMenu(QWidget *parent = nullptr);

    static void actionTriggered(QAction *triggeredAction);

    static void actionTriggered(QAction *triggeredAction, MainWindow *relevantWindow);

    static QIcon getCacheableMenuIcon(const QString &cacheKey, const QIcon &icon);

    const QList<SRecent> &getRecentsList() const { return recentsList; }

    const QHash<QString, QAction *> &getActionLibrary() const { return actionLibrary; }

    int getOpenWithMaxLength() const { return openWithMaxLength; };

signals:
    void recentsMenuUpdated();

protected:
    void initializeActionLibrary();

    static bool hasAncestor(QObject *object, QObject *ancestor);

private:
    // Registers a menu built here in the clone library, keyed by its menu action's data
    void trackClonedMenu(QMenu *menu);

    // Adds the max hidden entries named "prefix0".."prefix{max-1}" that
    // updateIndexedCloneActions fills in later
    void createIndexedCloneActions(QMenu *menu, const QString &prefix, int max,
                                   const QVariant &defaultItem);

    QHash<QString, QAction *> actionLibrary;

    QHash<QString, EnableRule> actionEnableRules;

    QMultiHash<QString, QAction *> actionCloneLibrary;

    QMultiHash<QString, QMenu *> menuCloneLibrary;

    QMenu *windowMenu;

    QList<SRecent> recentsList;

    QTimer *recentsSaveTimer;

    // Settings
    bool isSaveRecentsEnabled;
    int recentsListMaxLength;
    int openWithMaxLength;
};

#endif // ACTIONMANAGER_H
