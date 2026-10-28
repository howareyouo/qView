#include "qvplatformfunctions.h"

#include "qvcocoafunctions.h"
#include "qvlinuxx11functions.h"
#include "qvwin32functions.h"

#include <QContextMenuEvent>
#include <QFile>
#include <QMenu>
#include <QUrl>
#include <QWidget>

void QVPlatformFunctions::showContextMenu(QMenu *menu, QContextMenuEvent *event, QWindow *window)
{
#ifdef COCOA_LOADED
    QVCocoaFunctions::showMenu(menu, event->pos(), window);
#else
    Q_UNUSED(window)
    menu->popup(event->globalPos());
#endif
}

void QVPlatformFunctions::setUserDefaults()
{
#ifdef COCOA_LOADED
    QVCocoaFunctions::setUserDefaults();
#endif
}

void QVPlatformFunctions::setFullSizeContentView(QWindow *window, bool enable)
{
#ifdef COCOA_LOADED
    QVCocoaFunctions::setFullSizeContentView(window, enable);
#else
    Q_UNUSED(window)
    Q_UNUSED(enable)
#endif
}

void QVPlatformFunctions::setVibrancy(bool isHidden, bool alwaysDark, QWindow *window)
{
#ifdef COCOA_LOADED
    QVCocoaFunctions::setVibrancy(isHidden, alwaysDark, window);
#else
    Q_UNUSED(isHidden)
    Q_UNUSED(alwaysDark)
    Q_UNUSED(window)
#endif
}

int QVPlatformFunctions::getObscuredHeight(QWindow *window)
{
#ifdef COCOA_LOADED
    return QVCocoaFunctions::getObscuredHeight(window);
#else
    Q_UNUSED(window)
    return 0;
#endif
}

bool QVPlatformFunctions::startSystemMove(QWidget *widget)
{
#ifdef COCOA_LOADED
    return QVCocoaFunctions::startSystemMove(widget);
#else
    return widget->windowHandle()->startSystemMove();
#endif
}

void QVPlatformFunctions::closeWindow(QWindow *window)
{
#ifdef COCOA_LOADED
    QVCocoaFunctions::closeWindow(window);
#else
    Q_UNUSED(window)
#endif
}

void QVPlatformFunctions::setAlternate(QMenu *menu, int index)
{
#ifdef COCOA_LOADED
    QVCocoaFunctions::setAlternate(menu, index);
#else
    Q_UNUSED(menu)
    Q_UNUSED(index)
#endif
}

void QVPlatformFunctions::setDockRecents(const QStringList &recentPathsList)
{
#ifdef COCOA_LOADED
    QVCocoaFunctions::setDockRecents(recentPathsList);
#else
    Q_UNUSED(recentPathsList)
#endif
}

void QVPlatformFunctions::openWithInvokeAssocHandler(const QString &filePath,
                                                     void *winAssocHandler)
{
#if defined(Q_OS_WIN) && defined(WIN32_LOADED)
    QVWin32Functions::openWithInvokeAssocHandler(filePath, winAssocHandler);
#else
    Q_UNUSED(filePath)
    Q_UNUSED(winAssocHandler)
#endif
}

QVPlatformFunctions::TrashOutcome QVPlatformFunctions::moveToTrash(const QString &filePath,
                                                                   QString *trashedPath)
{
    QFile file(filePath);
    if (!file.moveToTrash())
        return TrashOutcome::Failure;
    if (trashedPath)
        *trashedPath = file.fileName();
    return TrashOutcome::Success;
}

QByteArray QVPlatformFunctions::getIccProfileForWindow(const QWindow *window)
{
#if defined(WIN32_LOADED)
    return QVWin32Functions::getIccProfileForWindow(window);
#elif defined(COCOA_LOADED)
    return QVCocoaFunctions::getIccProfileForWindow(window);
#elif defined(X11_LOADED)
    return QVLinuxX11Functions::getIccProfileForWindow(window);
#else
    Q_UNUSED(window)
    return QByteArray();
#endif
}
