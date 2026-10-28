#ifndef WINDOWGEOMETRY_H
#define WINDOWGEOMETRY_H

#include <QRect>
#include <QSize>

class MainWindow;
class QScreen;

namespace WindowGeometry {
QScreen *screenContaining(const QRect &rect);

// Resizes the window to fit the image within the current screen's bounds, honoring the
// min/max resize percentages and the platform-provided minimum image size (used on Windows).
void resizeToImage(MainWindow &window, QSize imageSize, qreal minWindowResizedPercentage,
                   qreal maxWindowResizedPercentage, const QSize &minimumImageSize);
}

#endif // WINDOWGEOMETRY_H
