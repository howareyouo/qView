#ifndef QVAPPLICATION_H
#define QVAPPLICATION_H

#include "mainwindow.h"
#include "settingsmanager.h"
#include "shortcutmanager.h"
#include "actionmanager.h"
#include "updatechecker.h"
#include "qvoptionsdialog.h"
#include "qvaboutdialog.h"
#include "qvwelcomedialog.h"

#include <QApplication>
#include <QRegularExpression>

#if defined(qvApp)
#  undef qvApp
#endif

#define qvApp \
    (qobject_cast<QVApplication *>(QCoreApplication::instance())) // global qvapplication object

class QVApplication : public QApplication
{
    Q_OBJECT

public:
    explicit QVApplication(int &argc, char **argv);
    ~QVApplication() override;

    bool event(QEvent *event) override;

    static void openFile(MainWindow *window, const QString &file, bool resize = true);

    static void openFile(const QString &file, bool resize = true);

    static void pickFile(MainWindow *parent = nullptr);

    static void pickUrl(MainWindow *parnet = nullptr);

    static QString getLastFileDialogDir();

    static void setLastFileDialogDir(const QString &dir);

    static MainWindow *newWindow();

    MainWindow *getMainWindow(bool shouldBeEmpty);

    void checkUpdates(bool isStartupCheck);

    void checkedUpdates();

    void recentsMenuUpdated();

    void addToLastActiveWindows(MainWindow *window);

    void deleteFromLastActiveWindows(MainWindow *window);

    void openOptionsDialog(QWidget *parent = nullptr);

    void openWelcomeDialog(QWidget *parent = nullptr);

    void openAboutDialog(QWidget *parent = nullptr);

    struct FilterLists
    {
        QStringList nameFilterList;
        QStringList fileExtensionList;
        QStringList mimeTypeNameList;
    };

    QMenuBar *getMenuBar() const { return menuBar; }

    const QStringList &getNameFilterList() const
    {
        ensureFilterLists();
        return filterLists.nameFilterList;
    }

    const QStringList &getFileExtensionList() const
    {
        ensureFilterLists();
        return filterLists.fileExtensionList;
    }

    const QStringList &getMimeTypeNameList() const
    {
        ensureFilterLists();
        return filterLists.mimeTypeNameList;
    }

    SettingsManager &getSettingsManager() { return settingsManager; }

    ShortcutManager &getShortcutManager() { return shortcutManager; }

    ActionManager &getActionManager() { return actionManager; }

    void ensureFontLoaded(const QString &path);

    static QIcon iconFromFont(const QString &fontFamily, const QChar &codePoint,
                              const int pixelSize, const qreal pixelRatio);

    static qreal getPerceivedBrightness(const QColor &color);

private:
    QList<MainWindow *> lastActiveWindows;

    QMenu *dockMenu;

    QMenuBar *menuBar;

    static FilterLists buildFilterLists();

    void ensureFilterLists() const;

    mutable FilterLists filterLists;

    mutable bool filterListsReady = false;

    // This order is very important
    SettingsManager settingsManager;
    ActionManager actionManager;
    ShortcutManager shortcutManager;

    QPointer<QVOptionsDialog> optionsDialog;
    QPointer<QVWelcomeDialog> welcomeDialog;
    QPointer<QVAboutDialog> aboutDialog;

#ifndef QV_DISABLE_ONLINE_VERSION_CHECK
    UpdateChecker updateChecker;
#endif // QV_DISABLE_ONLINE_VERSION_CHECK

    QSet<QString> loadedFontPaths;
};

inline bool qvGetSettingBool(SettingsManager::Setting setting)
{
    return qvApp->getSettingsManager().getBool(setting);
}
inline int qvGetSettingInt(SettingsManager::Setting setting)
{
    return qvApp->getSettingsManager().getInt(setting);
}
inline double qvGetSettingDouble(SettingsManager::Setting setting)
{
    return qvApp->getSettingsManager().getDouble(setting);
}
inline const QString qvGetSettingString(SettingsManager::Setting setting)
{
    return qvApp->getSettingsManager().getString(setting);
}

#endif // QVAPPLICATION_H
