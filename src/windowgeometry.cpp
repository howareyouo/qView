#include "windowgeometry.h"
#include "mainwindow.h"
#include "qvapplication.h"

#include <QGuiApplication>
#include <QMenuBar>
#include <QScreen>

namespace {

// Adapted from Qt source code (QGuiApplication::screenAt). That version also keeps a list of the
// screens it has already visited; here a screen visited twice costs nothing, because the
// comparison below is strict and a repeat can never beat the largest area found so far.
QScreen *screenContaining(const QRect &rect)
{
    QScreen *bestScreen = nullptr;
    int bestScreenArea = 0;
    const auto screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        // The virtual siblings include the screen itself, so iterate directly
        const auto siblings = screen->virtualSiblings();
        for (QScreen *sibling : siblings) {
            const QRect intersect = sibling->geometry().intersected(rect);
            const int area = intersect.width() * intersect.height();
            if (area > bestScreenArea) {
                bestScreen = sibling;
                bestScreenArea = area;
            }
        }
    }
    return bestScreen;
}

// The largest size the window can take on screen: the room the screen leaves once the part the
// window frame adds around the content is taken off
QSize maximumSizeOn(const QWidget &window, QScreen *screen)
{
    const QSize frameExtraSize = window.frameGeometry().size() - window.size();
    return screen->availableSize() - frameExtraSize;
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
    return maximumSizeOn(window, screenFor(window));
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

QSize fittedWindowSize(MainWindow &window, const QSize &imageSize, const QSize &minimumImageSize)
{
    const auto &settingsManager = qvApp->getSettingsManager();
    const qreal minWindowResizedPercentage =
            settingsManager.getInt(SettingsManager::Setting::MinWindowResizedPercentage) / 100.0;
    const qreal maxWindowResizedPercentage =
            settingsManager.getInt(SettingsManager::Setting::MaxWindowResizedPercentage) / 100.0;

    QSize extraWidgetsSize{ 0, 0 };

    if (window.isMenuBarVisible())
        extraWidgetsSize.rheight() += window.menuBar()->height();
    // Zero everywhere but on macOS, where the titlebar covers part of the content
    extraWidgetsSize.rheight() += window.getTitlebarOverlap();

    QScreen *screen = screenFor(window);
    const QSize hardLimitSize = maximumSizeOn(window, screen) - extraWidgetsSize;
    const QSize screenSize = screen->size();
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
    if (!minimumImageSize.isEmpty()
        && fittedImageSize.boundedTo(minimumImageSize) == fittedImageSize)
        fittedImageSize = minimumImageSize;

    return fittedImageSize + extraWidgetsSize;
}

} // namespace WindowGeometry
