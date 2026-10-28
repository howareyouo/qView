#include "windowgeometry.h"
#include "mainwindow.h"

#include <QGuiApplication>
#include <QScreen>
#include <QVarLengthArray>

namespace WindowGeometry {

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

void resizeToImage(MainWindow &window, QSize imageSize, qreal minWindowResizedPercentage,
                   qreal maxWindowResizedPercentage, const QSize &minimumImageSize)
{
    // Try to grab the current screen
    QScreen *currentScreen = screenContaining(window.frameGeometry());

    // makeshift validity check
    bool screenValid = QGuiApplication::screens().contains(currentScreen);
    // Use first screen as fallback
    if (!screenValid)
        currentScreen = QGuiApplication::screens().at(0);

    QSize extraWidgetsSize{ 0, 0 };

    if (window.menuBar()->isVisible())
        extraWidgetsSize.rheight() += window.menuBar()->height();

    const int titlebarOverlap = window.getTitlebarOverlap();
    if (titlebarOverlap != 0)
        extraWidgetsSize.rheight() += titlebarOverlap;

    const QSize windowFrameSize = window.frameGeometry().size() - window.geometry().size();
    const QSize hardLimitSize = currentScreen->availableSize() - windowFrameSize - extraWidgetsSize;
    const QSize screenSize = currentScreen->size();
    const QSize minWindowSize = (screenSize * minWindowResizedPercentage).boundedTo(hardLimitSize);
    const QSize maxWindowSize = (screenSize * maxWindowResizedPercentage).boundedTo(hardLimitSize);

    if (imageSize.width() < minWindowSize.width() && imageSize.height() < minWindowSize.height()) {
        imageSize.scale(minWindowSize, Qt::KeepAspectRatio);
    } else if (imageSize.width() > maxWindowSize.width()
               || imageSize.height() > maxWindowSize.height()) {
        imageSize.scale(maxWindowSize, Qt::KeepAspectRatio);
    }

    // Windows reports the wrong minimum width, so we constrain the image size relative to the dpi
    // to stop weirdness with tiny images
    if (!minimumImageSize.isEmpty()) {
        if (imageSize.boundedTo(minimumImageSize) == imageSize)
            imageSize = minimumImageSize;
    }

    // Match center after new geometry
    // This is smoother than a single geometry set for some reason
    QRect oldRect = window.geometry();
    window.resize(imageSize + extraWidgetsSize);
    QRect newRect = window.geometry();
    newRect.moveCenter(oldRect.center());

    // Ensure titlebar is not above or below the available screen area
    const QRect availableScreenRect = currentScreen->availableGeometry();
    const int topFrameHeight = window.geometry().top() - window.frameGeometry().top();
    const int windowMinY = availableScreenRect.top() + topFrameHeight;
    const int windowMaxY =
            availableScreenRect.top() + availableScreenRect.height() - titlebarOverlap;
    if (newRect.top() < windowMinY)
        newRect.moveTop(windowMinY);
    if (newRect.top() > windowMaxY)
        newRect.moveTop(windowMaxY);

    window.setGeometry(newRect);
}

}
