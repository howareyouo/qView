#ifndef QVPLATFORMFUNCTIONS_H
#define QVPLATFORMFUNCTIONS_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QWindow>

class QContextMenuEvent;
class QMenu;
class QWidget;

// Facade over the platform-specific function classes. Always compiled; dispatches to the
// platform implementation at compile time and provides no-op defaults elsewhere.
class QVPlatformFunctions
{
public:
    enum class TrashOutcome
    {
        Success,
        Failure,
        Unsupported
    };

    static void showContextMenu(QMenu *menu, QContextMenuEvent *event, QWindow *window);

    static void setUserDefaults();

    static void setFullSizeContentView(QWindow *window, bool enable);

    static void setVibrancy(bool isHidden, bool alwaysDark, QWindow *window);

    static int getObscuredHeight(QWindow *window);

    static bool startSystemMove(QWidget *widget);

    static void closeWindow(QWindow *window);

    static void setAlternate(QMenu *menu, int index);

    static void setDockRecents(const QStringList &recentPathsList);

    static void openWithInvokeAssocHandler(const QString &filePath, void *winAssocHandler);

    // Moves a file into the platform trash. Returns the outcome; on success trashedPath is
    // set to the file's new location inside the trash.
    static TrashOutcome moveToTrash(const QString &filePath, QString *trashedPath);

    // Moves a file into (putBack = false) or out of (putBack = true) the trash with gio.
    // Returns the file's path inside the trash, or an empty string on failure.
    static QString gioTrash(const QString &path, bool putBack);

    static QByteArray getIccProfileForWindow(const QWindow *window);
};

#endif // QVPLATFORMFUNCTIONS_H
