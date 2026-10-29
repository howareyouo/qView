#ifndef ACTIONMANAGER_H
#define ACTIONMANAGER_H

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

    static QList<QAction *> getAllActionsOfType(const QList<QAction *> &givenActionList,
                                                const QString &type)
    {
        QList<QAction *> allActionsOfType;

        const auto allNestedActions = getAllNestedActions(givenActionList);
        for (const auto &action : allNestedActions) {
            if (action->data() == type)
                allActionsOfType << action;
        }

        return allActionsOfType;
    }

    // Recent files are stored as a plain-text JSON array so that the settings file
    // stays human-readable in portable mode
    static QVariant recentsListToVariant(const QList<SRecent> &recentsList)
    {
        QJsonArray jsonArray;
        for (const auto &recent : recentsList) {
            jsonArray.append(
                    QJsonObject{ { "fileName", recent.fileName }, { "filePath", recent.filePath } });
        }
        return QString::fromUtf8(QJsonDocument(jsonArray).toJson(QJsonDocument::Compact));
    }

    static QList<SRecent> variantToRecentsList(const QVariant &variant)
    {
        QList<SRecent> recentsList;
        const QJsonDocument document = QJsonDocument::fromJson(variant.toString().toUtf8());
        for (const auto &value : document.array()) {
            const QJsonObject object = value.toObject();
            recentsList.append(
                    { object.value("fileName").toString(), object.value("filePath").toString() });
        }
        return recentsList;
    }

    // Converts the legacy binary QVariantList format written before the JSON format existed
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

    QList<QMenu *> getAllClonesOfMenu(const QString &key) const;

    QList<QMenu *> getAllClonesOfMenu(const QString &key, QWidget *parent) const;

    void untrackClonedActions(const QList<QAction *> &actions);

    void untrackClonedActions(const QMenu *menu);

    void untrackClonedActions(const QMenuBar *menuBar);

    void hideAllInstancesOfAction(const QString &key);

    QMenuBar *buildMenuBar(QWidget *parent = nullptr);

    QMenu *buildViewMenu(bool addIcon = true, QWidget *parent = nullptr,
                         bool includeFileInfo = false);

    QMenu *buildToolsMenu(bool addIcon = true, QWidget *parent = nullptr,
                          bool includeOptions = true);

    QMenu *buildHelpMenu(bool addIcon = true, QWidget *parent = nullptr);

    QMenu *buildRecentsMenu(bool includeClearAction = true, QWidget *parent = nullptr);

    void loadRecentsList();

    void saveRecentsList();

    void addFileToRecentsList(const QFileInfo &file);

    void auditRecentsList();

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

    const QHash<QString, EnableRule> &getEnableRules() const { return actionEnableRules; }

    int getRecentsListMaxLength() const { return recentsListMaxLength; };
    int getOpenWithMaxLength() const { return openWithMaxLength; };

signals:
    void recentsMenuUpdated();

protected:
    void initializeActionLibrary();

    static bool hasAncestor(QObject *object, QObject *ancestor);

private:
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
