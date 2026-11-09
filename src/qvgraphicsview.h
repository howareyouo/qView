#ifndef QVGRAPHICSVIEW_H
#define QVGRAPHICSVIEW_H

#include "qvimagecore.h"
#include <QGraphicsView>
#include <QImageReader>
#include <QMimeData>
#include <QDir>
#include <QTimer>
#include <QFileInfo>

class QVGraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    QVGraphicsView(QWidget *parent = nullptr);

    // The navigation cursor belongs to the folder listing it moves through
    using GoToFileMode = QVImageCore::GoToFileMode;

    QMimeData *getMimeData() const;
    void loadMimeData(const QMimeData *mimeData);
    void loadFile(const QString &fileName);

    void reloadFile();

    void zoomIn(const QPoint &pos = QPoint(-1, -1));

    void zoomOut(const QPoint &pos = QPoint(-1, -1));

    void zoom(qreal requestedStep, QPoint pos = QPoint(-1, -1));

    void scaleExpensively();
    void makeUnscaled();

    void resetScale();
    void originalSize();

    // Going back to the fit means shrinking the window back to the size it grew from
    void returnToFit();

    // Shows the image that was just loaded: refits it, gives the window the image's size when the
    // settings call for that, and puts scaleToKeep back on top. A zoom the last session ended at
    // goes on the first image presented this way. Leaving scaleToKeep at zero shows the fit.
    void presentImage(qreal scaleToKeep = 0);

    // Sizes the window to the image when the settings call for it, and records the size the image
    // was given as the one zooming back out returns to
    void fitWindowToImage();

    // Marks the next image presented as the one the window was launched with, which is the only
    // image the "resize on launch" mode resizes for
    void setLaunchingImage(bool value) { justLaunchedWithImage = value; }

    // Writes the zoom this view ends at, so the next session starts where this one left off
    void saveSessionZoom();

    void goToFile(GoToFileMode mode, int index = 0);

    void settingsUpdated();

    void closeImage();
    void jumpToNextFrame();
    void setPaused(const bool &desiredState);
    void setSpeed(const int &desiredSpeed);
    void rotateImage(int rotation);

    const QVImageCore::FileDetails &getCurrentFileDetails() const
    {
        return imageCore.getCurrentFileDetails();
    }
    const QPixmap &getLoadedPixmap() const { return imageCore.getLoadedPixmap(); }
    const QMovie &getLoadedMovie() const { return imageCore.getLoadedMovie(); }

    bool isResetScale() const { return !isOriginalSize && qFuzzyCompare(currentScale, 1.0); }

signals:
    void cancelSlideshow();

    void fileChanged();

protected:
    void wheelEvent(QWheelEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    void dropEvent(QDropEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;

    void dragMoveEvent(QDragMoveEvent *event) override;

    void dragLeaveEvent(QDragLeaveEvent *event) override;

    void enterEvent(QEnterEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void paintEvent(QPaintEvent *event) override;

    bool event(QEvent *event) override;

    void fitInViewMarginless(const QRectF &rect);
    void fitInViewMarginless(const QGraphicsItem *item);

    void centerOn(const QPointF &pos);

    void centerOn(qreal x, qreal y);

    void centerOn(const QGraphicsItem *item);

private slots:
    void animatedFrameChanged(QRect);

    void postLoad();

    void updateLoadedPixmapItem();

    void fullPixmapLoaded();

private:
    // What a new session needs to put the zoom back: the shape of the image it belongs to and how
    // far past the fit scale it was zoomed, written as "16:9,1.2x"
    QString getZoomRecord() const;

    // Puts the zoom a previous session ended at back on an image that has just been loaded. Has to
    // run after the window has been given the new image's size, since that refits the image.
    void restoreZoom(const QString &zoomRecord, bool originalSizeMode);

    // Reads the zoom the last session ended at, if the settings ask for it to come back
    void restoreSessionZoom();

    qreal maximumScaleOnScreen() const;

    void restoreWindowSize();

    void rememberFittedWindowSize();

    void updateFilteringMode();

    void applyScale(qreal scaleFactor);

    // Puts a zoom step on the image itself, on top of the transform the view already has
    void applyRelativeZoom(qreal relativeFactor);

    // Puts a zoom relative to the fit back on the image the way zooming would: the window grows for
    // the part it can hold and only the rest is left on the image
    void applyZoomRelativeToFit(qreal scaleFactor);

    void setOrientedTransform(bool mirrored, bool flipped, const QTransform &base);

    // Shows a pixmap at a new resolution, giving the view back the mirroring and flipping that the
    // base transform it is shown with does not carry
    void setPixmapKeepingOrientation(const QPixmap &pixmap, const QTransform &base);

    bool isAtFit() const;

    // Whether a zoom step may move the window at all: the settings have to allow it, and what part
    // of a step the window can take is worked out per step
    bool canResizeWindowForZoom() const;

    // Both work out the part of a zoom step the window can take and write the size it would move
    // to, without resizing anything: a step that ends past the zoom limits is rejected before the
    // window is touched. They return the part of the step the window takes, or 1.0 when the step
    // is left to the image
    qreal planGrowWindowForZoom(qreal scaleFactor, QSize &targetSize);
    qreal planShrinkWindowForZoom(qreal scaleFactor, QSize &targetSize);

    // Resizes the window for a zoom step and answers how much the fit really grew by: a window the
    // screen has no room left for keeps the size it had, so it grows by one and leaves the whole
    // step to the image
    qreal applyZoomWindowResize(const QSize &targetSize);

    bool isWindowMovable() const;

    // The edge of a frameless window under a point of the viewport, standing in for the native
    // resize border such a window does not have
    Qt::Edges framelessResizeEdgesAt(const QPoint &viewportPos) const;

    bool hasPannableArea() const;

    QGraphicsPixmapItem *loadedPixmapItem;

    constexpr static int MARGIN = -2;
    constexpr static qreal MAX_EXPENSIVE_SCALING_SIZE = 3;

    // A scale within this much of the fit scale counts as being at it, which is the only scale
    // the window is resized at
    constexpr static qreal FIT_EPSILON = 0.0001;

    // How far the image may zoom, relative to the fit scale
    constexpr static qreal MAX_ZOOM_SCALE = 500;
    constexpr static qreal MIN_ZOOM_SCALE = 0.01;

    // Set to too high a value to activate for now...
    constexpr static qreal MAX_FILTERING_SIZE = 5000;

    qreal currentScale;
    QSize scaledSize;
    bool isOriginalSize;
    QPoint lastZoomEventPos;
    QPointF lastZoomRoundingError;
    QPointF lastScrollRoundingError;
    QSize preZoomWindowSize;
    QSize zoomWindowFittedSize;

    // Set when the image about to be presented is the one the window was launched with
    bool justLaunchedWithImage = false;

    // Zoom the last session ended at, put back on the first image this view presents
    bool sessionZoomPending = false;
    QString sessionZoomRecord;
    bool sessionZoomOriginalSize = false;

    QTransform absoluteTransform;
    QTransform zoomBasis;
    qreal zoomBasisScaleFactor;

    // Size the fit was computed against, so a preview upgrade can rescale the
    // absolute transform to the full-resolution pixmap
    QSize fitBasisPixmapSize;

    QVImageCore imageCore{ this };

    QTimer *expensiveScaleTimerNew;
    Qt::MouseButton mousePressButton;
    Qt::KeyboardModifiers mousePressModifiers;
    QPoint mousePressPosition;
    QPoint windowMoveOffset;
    bool windowMoveStarted;
};
#endif // QVGRAPHICSVIEW_H
