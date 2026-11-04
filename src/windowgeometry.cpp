#include "windowgeometry.h"
#include "mainwindow.h"
#include "qvapplication.h"

#include <QGuiApplication>
#include <QMenuBar>
#include <QScreen>
#include <QVarLengthArray>

namespace {

// Initially copied from Qt source code (QGuiApplication::screenAt) and then customized
QScreen *screenContaining(const QRect &rect)
{
    QScreen *bestScreen = nullptr;
    int bestScreenArea = 0;
    QVarLengthArray<const QScreen *, 8> visitedScreens;
    const auto screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        if (visitedScreens.contains(screen))
            continue;
        // The virtual siblings include the screen itself, so iterate directly
        const auto siblings = screen->virtualSiblings();
        for (QScreen *sibling : siblings) {
            const QRect intersect = sibling->geometry().intersected(rect);
            const int area = intersect.width() * intersect.height();
            if (area > bestScreenArea) {
                bestScreen = sibling;
                bestScreenArea = area;
            }
            visitedScreens.append(sibling);
        }
    }
    return bestScreen;
}

} // namespace

namespace WindowGeometry {

QScreen *screenFor(const QWidget &window)
{
    const QRect frameGeometry = window.frameGeometry();

    // A window can straddle screens, and the one it covers most is the one it belongs to
    if (QScreen *screen = screenContaining(frameGeometry))
        return screen;

    if (QScreen *screen = QGuiApplication::screenAt(frameGeometry.center()))
        return screen;

    return QGuiApplication::primaryScreen();
}

QSize maximumWindowSize(const QWidget &window)
{
    const QSize frameExtraSize = window.frameGeometry().size() - window.size();
    return screenFor(window)->availableSize() - frameExtraSize;
}

void resizeKeepingCenter(QWidget &window, const QSize &targetSize)
{
    const QRect availableGeometry = screenFor(window)->availableGeometry();
    const QPoint frameOffset = window.geometry().topLeft() - window.frameGeometry().topLeft();
    const QSize frameSize = window.frameGeometry().size() - window.size() + targetSize;

    QRect targetFrameRect(QPoint(), frameSize);
    targetFrameRect.moveCenter(window.frameGeometry().center());
    if (targetFrameRect.left() < availableGeometry.left())
        targetFrameRect.moveLeft(availableGeometry.left());
    if (targetFrameRect.top() < availableGeometry.top())
        targetFrameRect.moveTop(availableGeometry.top());
    if (targetFrameRect.right() > availableGeometry.right())
        targetFrameRect.moveRight(availableGeometry.right());
    if (targetFrameRect.bottom() > availableGeometry.bottom())
        targetFrameRect.moveBottom(availableGeometry.bottom());

    window.setGeometry(QRect(targetFrameRect.topLeft() + frameOffset, targetSize));
}

void resizeToImage(MainWindow &window, const QSize &imageSize, const QSize &minimumImageSize)
{
    const auto &settingsManager = qvApp->getSettingsManager();
    const qreal minWindowResizedPercentage =
            settingsManager.getInt(SettingsManager::Setting::MinWindowResizedPercentage) / 100.0;
    const qreal maxWindowResizedPercentage =
            settingsManager.getInt(SettingsManager::Setting::MaxWindowResizedPercentage) / 100.0;

    QSize extraWidgetsSize{ 0, 0 };

    if (window.isMenuBarVisible())
        extraWidgetsSize.rheight() += window.menuBar()->height();

    const int titlebarOverlap = window.getTitlebarOverlap();
    if (titlebarOverlap != 0)
        extraWidgetsSize.rheight() += titlebarOverlap;

    const QSize hardLimitSize = maximumWindowSize(window) - extraWidgetsSize;
    const QSize screenSize = screenFor(window)->size();
    const QSize minWindowSize = (screenSize * minWindowResizedPercentage).boundedTo(hardLimitSize);
    const QSize maxWindowSize = (screenSize * maxWindowResizedPercentage).boundedTo(hardLimitSize);

    QSize fittedImageSize = imageSize;
    if (fittedImageSize.width() < minWindowSize.width()
        && fittedImageSize.height() < minWindowSize.height()) {
        fittedImageSize.scale(minWindowSize, Qt::KeepAspectRatio);
    } else if (fittedImageSize.width() > maxWindowSize.width()
               || fittedImageSize.height() > maxWindowSize.height()) {
        fittedImageSize.scale(maxWindowSize, Qt::KeepAspectRatio);
    }

    // Windows reports the wrong minimum width, so we constrain the image size relative to the dpi
    // to stop weirdness with tiny images
    if (!minimumImageSize.isEmpty()) {
        if (fittedImageSize.boundedTo(minimumImageSize) == fittedImageSize)
            fittedImageSize = minimumImageSize;
    }

    resizeKeepingCenter(window, fittedImageSize + extraWidgetsSize);
}

}
