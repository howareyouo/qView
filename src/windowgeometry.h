#ifndef WINDOWGEOMETRY_H
#define WINDOWGEOMETRY_H

#include <QSize>

class QScreen;
class QWidget;
class MainWindow;

namespace WindowGeometry {
// The screen a window belongs to: the one it covers the most of, then the one its center is on,
// then the primary screen
QScreen *screenFor(const QWidget &window);

// The largest size the window can take on its screen, with the part its frame adds taken off
QSize maximumWindowSize(const QWidget &window);

// Resizes the window around its own center, keeping it inside its screen
void resizeKeepingCenter(QWidget &window, const QSize &targetSize);

// Resizes the window to fit the image within its screen's bounds, honoring the min/max resize
// percentages from the settings and the platform-provided minimum image size (used on Windows)
void resizeToImage(MainWindow &window, const QSize &imageSize, const QSize &minimumImageSize);
} // namespace WindowGeometry

#endif // WINDOWGEOMETRY_H
