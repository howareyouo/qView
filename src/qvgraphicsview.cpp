#include "qvgraphicsview.h"
#include "qvapplication.h"
#include "qvinfodialog.h"
#include "qvcocoafunctions.h"
#include "qvplatformfunctions.h"
#include "settingsmanager.h"
#include <QRandomGenerator>
#include <QWheelEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QSettings>
#include <QMessageBox>
#include <QMovie>
#include <QtMath>
#include <QGestureEvent>
#include <QScreen>
#include <QScrollBar>

QVGraphicsView::QVGraphicsView(QWidget *parent) : QGraphicsView(parent)
{
    // GraphicsView setup
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setFrameShape(QFrame::NoFrame);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    viewport()->setAutoFillBackground(false);

    // part of a pathetic attempt at gesture support
    grabGesture(Qt::PinchGesture);

    // Scene setup
    auto *scene = new QGraphicsScene(-1000000.0, -1000000.0, 2000000.0, 2000000.0, this);
    setScene(scene);

    // Initialize other variables
    currentScale = 1.0;
    scaledSize = QSize();
    isOriginalSize = false;
    lastZoomEventPos = QPoint(-1, -1);
    lastZoomRoundingError = QPointF();
    lastScrollRoundingError = QPointF();
    mousePressButton = Qt::MouseButton::NoButton;
    mousePressModifiers = Qt::KeyboardModifier::NoModifier;
    mousePressPosition = QPoint();
    windowMoveOffset = QPoint();
    windowMoveStarted = false;

    zoomBasisScaleFactor = 1.0;
    fitBasisPixmapSize = QSize();

    connect(&imageCore, &QVImageCore::animatedFrameChanged, this,
            &QVGraphicsView::animatedFrameChanged);
    connect(&imageCore, &QVImageCore::fileChanged, this, &QVGraphicsView::postLoad);
    connect(&imageCore, &QVImageCore::updateLoadedPixmapItem, this,
            &QVGraphicsView::updateLoadedPixmapItem);
    connect(&imageCore, &QVImageCore::fullPixmapLoaded, this, &QVGraphicsView::fullPixmapLoaded);

    // Should replace the other timer eventually
    expensiveScaleTimerNew = new QTimer(this);
    expensiveScaleTimerNew->setSingleShot(true);
    expensiveScaleTimerNew->setInterval(50);
    connect(expensiveScaleTimerNew, &QTimer::timeout, this, [this] { scaleExpensively(); });

    loadedPixmapItem = new QGraphicsPixmapItem();
    scene->addItem(loadedPixmapItem);

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this,
            &QVGraphicsView::settingsUpdated);
    settingsUpdated();
}

// Events

void QVGraphicsView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (!isOriginalSize)
        resetScale();
    else
        centerOn(loadedPixmapItem);
}

void QVGraphicsView::dropEvent(QDropEvent *event)
{
    QGraphicsView::dropEvent(event);
    loadMimeData(event->mimeData());
}

void QVGraphicsView::dragEnterEvent(QDragEnterEvent *event)
{
    QGraphicsView::dragEnterEvent(event);
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void QVGraphicsView::dragMoveEvent(QDragMoveEvent *event)
{
    QGraphicsView::dragMoveEvent(event);
    event->acceptProposedAction();
}

void QVGraphicsView::dragLeaveEvent(QDragLeaveEvent *event)
{
    QGraphicsView::dragLeaveEvent(event);
    event->accept();
}

void QVGraphicsView::enterEvent(QEnterEvent *event)
{
    QGraphicsView::enterEvent(event);
    viewport()->setCursor(Qt::ArrowCursor);
}

void QVGraphicsView::mousePressEvent(QMouseEvent *event)
{
    const auto startWindowMove = [this]() {
        return QVPlatformFunctions::startSystemMove(window());
    };

    const auto startFallbackWindowMove = [this, event]() {
        mousePressButton = event->button();
        mousePressModifiers = event->modifiers();
        mousePressPosition = event->globalPosition().toPoint();
        windowMoveOffset = mousePressPosition - window()->pos();
        windowMoveStarted = false;
    };

    // Check for Ctrl/Cmd drag
    if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ControlModifier)
        && (window()->windowFlags().testFlag(Qt::FramelessWindowHint)
            || qvApp->getSettingsManager().getBool(SettingsManager::Setting::CtrlDragWindow))) {
        if (isWindowMovable()) {
            if (!startWindowMove()) {
                startFallbackWindowMove();
            }
            return;
        }
    }

    // A frameless window has no native resize border, so resize it from the edges of the view
    if (event->button() == Qt::LeftButton
        && window()->windowFlags().testFlag(Qt::FramelessWindowHint)) {
        if (isWindowMovable()) {
            const QPoint windowPos = viewport()->mapTo(window(), event->pos());
            const QSize windowSize = window()->size();
            const int resizeBorder = 6;

            Qt::Edges edges;
            if (windowPos.x() <= resizeBorder)
                edges |= Qt::LeftEdge;
            else if (windowPos.x() >= windowSize.width() - resizeBorder - 1)
                edges |= Qt::RightEdge;
            if (windowPos.y() <= resizeBorder)
                edges |= Qt::TopEdge;
            else if (windowPos.y() >= windowSize.height() - resizeBorder - 1)
                edges |= Qt::BottomEdge;

            if (edges != Qt::Edges()) {
                window()->windowHandle()->startSystemResize(edges);
                return;
            }
        }
    }

    // Check for titlebar region drag
    if (event->button() == Qt::LeftButton && isWindowMovable()) {
#ifdef COCOA_LOADED
        // Check if click is in titlebar region
        int titlebarHeight = QVCocoaFunctions::getTitlebarHeight(window()->windowHandle());
        if (event->pos().y() <= titlebarHeight) {
            if (!startWindowMove()) {
                startFallbackWindowMove();
            }
            return;
        }
#endif
    }

    // A fitted image leaves nothing to scroll, so a left drag moves the window instead of
    // silently doing nothing
    if (event->button() == Qt::LeftButton && !hasPannableArea() && isWindowMovable()) {
        startFallbackWindowMove();
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

bool QVGraphicsView::isWindowMovable() const
{
    const auto windowState = window()->windowState();
    return !windowState.testFlag(Qt::WindowFullScreen)
            && !windowState.testFlag(Qt::WindowMaximized);
}

bool QVGraphicsView::hasPannableArea() const
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return false;

    const QRectF pixmapRect = transform().mapRect(loadedPixmapItem->sceneBoundingRect());
    const QRect viewportRect = viewport()->rect();

    // The fitted image is allowed to overflow by the MARGIN, which is not worth panning
    constexpr int panSlack = 8;
    return pixmapRect.width() > viewportRect.width() + panSlack
            || pixmapRect.height() > viewportRect.height() + panSlack;
}

void QVGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    if (mousePressButton == Qt::LeftButton) {
        if (mousePressModifiers.testFlag(Qt::ControlModifier)
            && !event->modifiers().testFlag(Qt::ControlModifier)) {
            mousePressButton = Qt::NoButton;
            mousePressModifiers = Qt::NoModifier;
            windowMoveStarted = false;
            QGraphicsView::mouseMoveEvent(event);
            return;
        }

        const QPoint globalPosition = event->globalPosition().toPoint();
        // Don't start moving the window on the press itself, or double clicking would be eaten
        if (!windowMoveStarted) {
            if ((globalPosition - mousePressPosition).manhattanLength()
                <= QApplication::startDragDistance())
                return;
            windowMoveStarted = true;
        }

        window()->move(globalPosition - windowMoveOffset);
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void QVGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    mousePressButton = Qt::NoButton;
    mousePressModifiers = Qt::NoModifier;
    windowMoveStarted = false;
    QGraphicsView::mouseReleaseEvent(event);
    viewport()->setCursor(Qt::ArrowCursor);
}

bool QVGraphicsView::event(QEvent *event)
{
    // this is for touchpad pinch gestures
    if (event->type() == QEvent::Gesture) {
        auto *gestureEvent = static_cast<QGestureEvent *>(event);
        if (QGesture *pinch = gestureEvent->gesture(Qt::PinchGesture)) {
            auto *pinchGesture = static_cast<QPinchGesture *>(pinch);
            QPinchGesture::ChangeFlags changeFlags = pinchGesture->changeFlags();

            if (changeFlags & QPinchGesture::ScaleFactorChanged) {
                const QPoint hotPoint = mapFromGlobal(pinchGesture->hotSpot().toPoint());
                zoom(pinchGesture->scaleFactor(), hotPoint);
            }

            // Fun rotation stuff maybe later
            //            if (changeFlags & QPinchGesture::RotationAngleChanged) {
            //                qreal rotationDelta = pinchGesture->rotationAngle() -
            //                pinchGesture->lastRotationAngle(); rotate(rotationDelta);
            //                centerOn(loadedPixmapItem);
            //            }
            return true;
        }
    } else if (event->type() == QEvent::NativeGesture) {
        auto *nativeEvent = static_cast<QNativeGestureEvent *>(event);
        if (nativeEvent->gestureType() == Qt::ZoomNativeGesture) {
            const QPoint eventPos = nativeEvent->position().toPoint();
            zoom(nativeEvent->value() + 1, eventPos);
            return true;
        }
    }
    return QGraphicsView::event(event);
}

void QVGraphicsView::wheelEvent(QWheelEvent *event)
{
    const QPoint eventPos = event->position().toPoint();

    const bool modifierPressed = event->modifiers().testFlag(Qt::ControlModifier);
    bool dontZoom = qvGetSettingInt(SettingsManager::Setting::ScrollZoom)
            == SettingsManager::ScrollZoomsScroll;
    if (modifierPressed) {
        dontZoom = !dontZoom;
    }

    bool touchDeviceDetected = false;
    // Auto-detect touchpad
    touchDeviceDetected = event->device()->type() == QInputDevice::DeviceType::TouchPad
            || event->device()->type() == QInputDevice::DeviceType::TouchScreen;
    // Real touchpads are likely to exhibit these characteristics in empirical testing
    touchDeviceDetected = touchDeviceDetected && event->phase() != Qt::NoScrollPhase;
    if (touchDeviceDetected
        && qvGetSettingInt(SettingsManager::Setting::ScrollZoom)
                == SettingsManager::ScrollZoomsAutoDetect) {
        // If this is a touch device, override setting
        dontZoom = !modifierPressed;
    }

    if (dontZoom) {
        const qreal scrollDivisor = 2.0; // To make scrolling less sensitive
        qreal scrollX = event->angleDelta().x() * (isRightToLeft() ? 1 : -1) / scrollDivisor;
        qreal scrollY = event->angleDelta().y() * -1 / scrollDivisor;

        if (event->modifiers() & Qt::ShiftModifier)
            std::swap(scrollX, scrollY);

        QPointF targetScrollDelta = QPointF(scrollX, scrollY) - lastScrollRoundingError;
        QPoint roundedScrollDelta = targetScrollDelta.toPoint();

        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + roundedScrollDelta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + roundedScrollDelta.y());

        lastScrollRoundingError = roundedScrollDelta - targetScrollDelta;

        return;
    }

    const int yDelta = event->angleDelta().y();
    const qreal yScale = 120.0;

    if (yDelta == 0)
        return;

    const qreal zoomAmountPerWheelClick = qvGetSettingInt(SettingsManager::Setting::ScaleFactor) / 100.0;
    qreal zoomFactor = zoomAmountPerWheelClick;
    if (qvGetSettingBool(SettingsManager::Setting::FractionalZoom) || touchDeviceDetected) {
        const qreal fractionalWheelClicks = qFabs(yDelta) / yScale;
        zoomFactor *= fractionalWheelClicks;
    }
    zoomFactor += 1.0;

    if (yDelta < 0)
        zoomFactor = qPow(zoomFactor, -1);

    zoom(zoomFactor, eventPos);
}

// Functions

QMimeData *QVGraphicsView::getMimeData() const
{
    auto *mimeData = new QMimeData();
    if (!getCurrentFileDetails().isPixmapLoaded)
        return mimeData;

    mimeData->setUrls(
            { QUrl::fromLocalFile(imageCore.getCurrentFileDetails().fileInfo.absoluteFilePath()) });
    mimeData->setImageData(imageCore.getLoadedPixmap().toImage());
    return mimeData;
}

void QVGraphicsView::loadMimeData(const QMimeData *mimeData)
{
    if (mimeData == nullptr)
        return;

    if (!mimeData->hasUrls())
        return;

    const QList<QUrl> urlList = mimeData->urls();

    bool first = true;
    for (const auto &url : urlList) {
        if (first) {
            loadFile(url.toString());
            emit cancelSlideshow();
            first = false;
            continue;
        }
        QVApplication::openFile(url.toString());
    }
}

void QVGraphicsView::loadFile(const QString &fileName)
{
    imageCore.loadFile(fileName);
}

void QVGraphicsView::reloadFile()
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return;

    imageCore.loadFile(getCurrentFileDetails().fileInfo.absoluteFilePath(), true);
}

void QVGraphicsView::postLoad()
{
    preZoomWindowSize = QSize();
    updateLoadedPixmapItem();
    qvApp->getActionManager().addFileToRecentsList(getCurrentFileDetails().fileInfo);

    emit fileChanged();
}

void QVGraphicsView::zoomIn(const QPoint &pos)
{
    zoom(qvGetSettingInt(SettingsManager::Setting::ScaleFactor) / 100.0 + 1, pos);
}

void QVGraphicsView::zoomOut(const QPoint &pos)
{
    zoom(qPow(qvGetSettingInt(SettingsManager::Setting::ScaleFactor) / 100.0 + 1, -1), pos);
}

void QVGraphicsView::zoom(qreal scaleFactor, QPoint pos)
{
    const bool cursorZoom = pos != QPoint(-1, -1);
    const QPoint globalPos = cursorZoom ? viewport()->mapToGlobal(pos) : QPoint(-1, -1);

    // The window only ever takes whole steps at the fit scale, since a resize refits the image
    // and would drop the zoom it had. So a step that would end off fit is split instead: the
    // image goes back to fit exactly and the window takes the rest of the step. Leaving the
    // image even slightly off fit would keep the window from shrinking back again.
    const bool atFit = qAbs(currentScale - 1.0) <= 0.0001;
    qreal imageScaleFactor = scaleFactor;

    if (!atFit) {
        const qreal fitScaleFactor = 1.0 / currentScale;
        const bool reachesFit =
                currentScale > 1.0 ? scaleFactor <= fitScaleFactor : scaleFactor >= fitScaleFactor;
        // Without reaching the fit scale the whole step belongs to the image
        scaleFactor = reachesFit ? scaleFactor / fitScaleFactor : 1.0;
    }

    if (qAbs(scaleFactor - 1.0) > 0.0001) {
        const qreal windowScaleFactor = scaleFactor > 1.0 ? growWindowForZoom(scaleFactor)
                                                          : shrinkWindowForZoom(scaleFactor);

        if (qAbs(windowScaleFactor - 1.0) > 0.0001) {
            // The resize refitted the image, so only what the window could not take is left over
            imageScaleFactor = scaleFactor / windowScaleFactor;
        } else if (!atFit) {
            // The window stays as it is, the image just goes back to the fit scale
            imageScaleFactor = 1.0 / currentScale;
        }
    }

    // The window took care of the whole step
    if (qAbs(imageScaleFactor - 1.0) < 0.0001)
        return;

    // The window is resized around its center, so the point under the cursor sits elsewhere
    // in the viewport than it did before
    if (cursorZoom)
        pos = viewport()->mapFromGlobal(globalPos);

    scaleFactor = imageScaleFactor;

    // don't zoom too far out, dude
    currentScale *= scaleFactor;
    if (currentScale >= 500 || currentScale <= 0.01) {
        currentScale *= qPow(scaleFactor, -1);
        return;
    }

    updateFilteringMode();

    if (pos != lastZoomEventPos) {
        lastZoomEventPos = pos;
        lastZoomRoundingError = QPointF();
    }
    const QPointF scenePos = mapToScene(pos) - lastZoomRoundingError;

    zoomBasisScaleFactor *= scaleFactor;
    setTransform(QTransform(zoomBasis).scale(zoomBasisScaleFactor, zoomBasisScaleFactor));
    absoluteTransform.scale(scaleFactor, scaleFactor);

    // If we have a point to zoom towards, the mouse is on top of the viewport,
    // and cursor zooming is enabled, anchor the zoom to the point under the cursor
    if (pos != QPoint(-1, -1) && underMouse() && qvGetSettingBool(SettingsManager::Setting::CursorZoom)) {
        const QPointF p1mouse = mapFromScene(scenePos);
        const QPointF move = p1mouse - pos;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                        + (move.x() * (isRightToLeft() ? -1 : 1)));
        verticalScrollBar()->setValue(verticalScrollBar()->value() + move.y());
        lastZoomRoundingError = mapToScene(pos) - scenePos;
    } else {
        centerOn(loadedPixmapItem);
    }

    if (qvGetSettingBool(SettingsManager::Setting::ScalingEnabled) && !isOriginalSize) {
        expensiveScaleTimerNew->start();
    }
}

static QScreen *screenForWindow(const QWidget *viewWindow)
{
    QScreen *foundScreen =
            viewWindow ? QGuiApplication::screenAt(viewWindow->frameGeometry().center()) : nullptr;
    return foundScreen ? foundScreen : QGuiApplication::primaryScreen();
}

static void resizeWindowKeepingCenter(QWidget *viewWindow, const QSize &targetSize)
{
    const QRect availableGeometry = screenForWindow(viewWindow)->availableGeometry();
    const QPoint frameOffset =
            viewWindow->geometry().topLeft() - viewWindow->frameGeometry().topLeft();
    const QSize frameSize = viewWindow->frameGeometry().size() - viewWindow->size() + targetSize;

    QRect targetFrameRect(QPoint(), frameSize);
    targetFrameRect.moveCenter(viewWindow->frameGeometry().center());
    if (targetFrameRect.left() < availableGeometry.left())
        targetFrameRect.moveLeft(availableGeometry.left());
    if (targetFrameRect.top() < availableGeometry.top())
        targetFrameRect.moveTop(availableGeometry.top());
    if (targetFrameRect.right() > availableGeometry.right())
        targetFrameRect.moveRight(availableGeometry.right());
    if (targetFrameRect.bottom() > availableGeometry.bottom())
        targetFrameRect.moveBottom(availableGeometry.bottom());

    viewWindow->setGeometry(QRect(targetFrameRect.topLeft() + frameOffset, targetSize));
}

void QVGraphicsView::applyZoomWindowResize(const QSize &targetSize)
{
    QWidget *viewWindow = window();
    if (!viewWindow)
        return;

    // The new window size, the refitted scene and the repainted content have to reach the screen
    // as one frame. A plain resize leaves the enlarged area unfilled for a moment, which the
    // translucent window shows as a flash of whatever is behind it.
    const bool updatesWereEnabled = viewWindow->updatesEnabled();
    viewWindow->setUpdatesEnabled(false);
    resizeWindowKeepingCenter(viewWindow, targetSize);
    if (updatesWereEnabled) {
        viewWindow->setUpdatesEnabled(true);
        viewWindow->repaint();
    }

    // The cursor zoom rounding error belongs to the old geometry
    lastZoomEventPos = QPoint(-1, -1);
    lastZoomRoundingError = QPointF();
}

qreal QVGraphicsView::growWindowForZoom(qreal scaleFactor)
{
    QWidget *viewWindow = window();
    if (!viewWindow || isOriginalSize || !getCurrentFileDetails().isPixmapLoaded)
        return 1.0;

    // Respect the user's choice of never resizing the window automatically
    if (qvGetSettingInt(SettingsManager::Setting::WindowResizeMode) == SettingsManager::WindowResizeNever)
        return 1.0;

    if (viewWindow->isMaximized() || viewWindow->isFullScreen())
        return 1.0;

    // When the fit is locked to the actual size, the image doesn't follow the window size
    if (qAbs(qAbs(absoluteTransform.m11()) - 1.0) < 0.0001)
        return 1.0;

    const QSize currentSize = viewWindow->size();
    const QSize frameExtraSize = viewWindow->frameGeometry().size() - currentSize;
    const QSize maximumSize =
            screenForWindow(viewWindow)->availableGeometry().size() - frameExtraSize;

    // Grow both dimensions by the same factor so the window keeps its shape, and stop as soon as
    // either dimension reaches the screen instead of stretching the other one towards it
    const qreal availableScaleFactor = qMin(maximumSize.width() / qreal(currentSize.width()),
                                            maximumSize.height() / qreal(currentSize.height()));
    const qreal appliedScaleFactor = qMin(scaleFactor, availableScaleFactor);

    QSize targetSize(qRound(currentSize.width() * appliedScaleFactor),
                     qRound(currentSize.height() * appliedScaleFactor));
    targetSize = targetSize.boundedTo(maximumSize);
    // A window that is already as large as the screen can't grow any further
    if (targetSize == currentSize)
        return 1.0;

    if (!preZoomWindowSize.isValid())
        preZoomWindowSize = currentSize;

    applyZoomWindowResize(targetSize);
    return appliedScaleFactor;
}

qreal QVGraphicsView::shrinkWindowForZoom(qreal scaleFactor)
{
    QWidget *viewWindow = window();
    if (!viewWindow || isOriginalSize || !preZoomWindowSize.isValid())
        return 1.0;

    if (viewWindow->isMaximized() || viewWindow->isFullScreen()) {
        preZoomWindowSize = QSize();
        return 1.0;
    }

    const QSize currentSize = viewWindow->size();
    const bool windowWasResizedElsewhere = currentSize.width() < preZoomWindowSize.width()
            || currentSize.height() < preZoomWindowSize.height();

    // Shrink by the same factor in both dimensions as well, and never below the size the window
    // had before it started to grow
    const qreal availableScaleFactor =
            qMax(preZoomWindowSize.width() / qreal(currentSize.width()),
                 preZoomWindowSize.height() / qreal(currentSize.height()));
    const qreal appliedScaleFactor = qMax(scaleFactor, availableScaleFactor);

    QSize targetSize(qRound(currentSize.width() * appliedScaleFactor),
                     qRound(currentSize.height() * appliedScaleFactor));
    targetSize.setWidth(qMax(targetSize.width(), preZoomWindowSize.width()));
    targetSize.setHeight(qMax(targetSize.height(), preZoomWindowSize.height()));

    if (windowWasResizedElsewhere || targetSize == currentSize) {
        preZoomWindowSize = QSize();
        return 1.0;
    }

    const bool backToStartSize = targetSize == preZoomWindowSize;
    if (backToStartSize)
        preZoomWindowSize = QSize();

    applyZoomWindowResize(targetSize);
    return appliedScaleFactor;
}

void QVGraphicsView::restoreWindowSize()
{
    if (!preZoomWindowSize.isValid())
        return;

    const QSize restoreSize = preZoomWindowSize;
    preZoomWindowSize = QSize();

    QWidget *viewWindow = window();
    if (!viewWindow || viewWindow->isMaximized() || viewWindow->isFullScreen())
        return;

    if (viewWindow->size() == restoreSize)
        return;

    applyZoomWindowResize(restoreSize);
}

void QVGraphicsView::scaleExpensively()
{
    // Determine if mirrored or flipped
    bool mirrored = transform().m11() < 0;
    bool flipped = transform().m22() < 0;

    // If we are above maximum scaling size
    if ((currentScale >= MAX_EXPENSIVE_SCALING_SIZE)
        || (!qvGetSettingBool(SettingsManager::Setting::ScalingTwoEnabled) && currentScale > 1.00001)) {
        // Return to original size
        makeUnscaled();
        return;
    }

    // Map size of the original pixmap to the scale acquired in fitting with modification from
    // zooming percentage
    const QRectF mappedRect =
            absoluteTransform.mapRect(QRectF({}, getCurrentFileDetails().loadedPixmapSize));
    const QSizeF mappedPixmapSize = mappedRect.size() * devicePixelRatioF();

    // A pixmap that already has the resolution we would scale to only shifts the image by a
    // sub-pixel amount, which the window resize just made visible as a flicker
    if (!getCurrentFileDetails().isMovieLoaded) {
        QSize targetPixmapSize = getLoadedPixmap().size();
        targetPixmapSize.scale(mappedPixmapSize.toSize(), Qt::KeepAspectRatio);
        if (targetPixmapSize == loadedPixmapItem->pixmap().size())
            return;
    }

    // Set image to scaled version, restoring mirror/flip with the new transform
    loadedPixmapItem->setPixmap(imageCore.scaleExpensively(mappedPixmapSize));

    setOrientedTransform(
            mirrored, flipped,
            QTransform::fromScale(qPow(devicePixelRatioF(), -1), qPow(devicePixelRatioF(), -1)));
}

void QVGraphicsView::makeUnscaled()
{
    // Determine if mirrored or flipped
    bool mirrored = transform().m11() < 0;
    bool flipped = transform().m22() < 0;

    // Return to original size
    if (getCurrentFileDetails().isMovieLoaded)
        loadedPixmapItem->setPixmap(getLoadedMovie().currentPixmap());
    else
        loadedPixmapItem->setPixmap(getLoadedPixmap());

    setOrientedTransform(mirrored, flipped, absoluteTransform);
}

void QVGraphicsView::setOrientedTransform(bool mirrored, bool flipped, const QTransform &base)
{
    setTransform(base);
    if (mirrored)
        scale(-1, 1);
    if (flipped)
        scale(1, -1);

    zoomBasis = transform();
    zoomBasisScaleFactor = 1.0;
}

void QVGraphicsView::updateFilteringMode()
{
    const bool exceededSmoothScaleLimit = currentScale >= MAX_FILTERING_SIZE;
    loadedPixmapItem->setTransformationMode(!exceededSmoothScaleLimit
                                                            && qvGetSettingBool(SettingsManager::Setting::FilteringEnabled)
                                                    ? Qt::SmoothTransformation
                                                    : Qt::FastTransformation);
}

void QVGraphicsView::animatedFrameChanged(QRect rect)
{
    Q_UNUSED(rect)

    if (qvGetSettingBool(SettingsManager::Setting::ScalingEnabled)) {
        scaleExpensively();
    } else {
        loadedPixmapItem->setPixmap(getLoadedMovie().currentPixmap());
    }
}

void QVGraphicsView::updateLoadedPixmapItem()
{
    // set pixmap and offset
    loadedPixmapItem->setPixmap(getLoadedPixmap());
    scaledSize = loadedPixmapItem->boundingRect().size().toSize();

    resetScale();

    emit updatedLoadedPixmapItem();
}

void QVGraphicsView::fullPixmapLoaded()
{
    // Animations swap in their own frames; the upgrade decode only refreshed the cache
    if (getCurrentFileDetails().isMovieLoaded)
        return;

    if (!getCurrentFileDetails().isPixmapLoaded || fitBasisPixmapSize.isEmpty())
        return;

    // At actual size the full pixmap is simply shown 1:1
    if (isOriginalSize) {
        loadedPixmapItem->setPixmap(getLoadedPixmap());
        return;
    }

    const QSize fullSize = getCurrentFileDetails().loadedPixmapSize;
    if (fullSize.isEmpty() || fullSize == fitBasisPixmapSize)
        return;

    // Rescale the absolute transform so the on-screen size does not jump when the
    // preview pixmap is replaced by the full-resolution one
    absoluteTransform = QTransform::fromScale(
            absoluteTransform.m11() * fitBasisPixmapSize.width() / qreal(fullSize.width()),
            absoluteTransform.m22() * fitBasisPixmapSize.height() / qreal(fullSize.height()));
    fitBasisPixmapSize = fullSize;

    if (qAbs(currentScale - 1.0) < 0.0001)
        updateLoadedPixmapItem();
    else if (qvGetSettingBool(SettingsManager::Setting::ScalingEnabled))
        scaleExpensively();
    // When scaling is disabled and the view is zoomed in, keep the current appearance;
    // the next interaction picks up the full pixmap
}

void QVGraphicsView::resetScale()
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return;

    fitInViewMarginless(loadedPixmapItem);

    if (qvGetSettingBool(SettingsManager::Setting::ScalingEnabled))
        expensiveScaleTimerNew->start();
}

void QVGraphicsView::originalSize()
{
    if (isOriginalSize) {
        // If we are at the actual original size
        if (transform() == QTransform()) {
            resetScale(); // back to normal mode
            return;
        }
    }
    makeUnscaled();

    resetTransform();
    centerOn(loadedPixmapItem);

    zoomBasis = transform();
    zoomBasisScaleFactor = 1.0;
    absoluteTransform = transform();

    isOriginalSize = true;
}

void QVGraphicsView::goToFile(const GoToFileMode &mode, int index)
{
    bool shouldRetryFolderInfoUpdate = false;

    // Update folder info only after a little idle time as an optimization for when
    // the user is rapidly navigating through files.
    if (!getCurrentFileDetails().timeSinceLoaded.isValid()
        || getCurrentFileDetails().timeSinceLoaded.hasExpired(3000)) {
        // Make sure the file still exists because if it disappears from the file listing we'll lose
        // track of our index within the folder. Use the static 'exists' method to avoid caching.
        // If we skip updating now, flag it for retry later once we locate a new file.
        if (QFile::exists(getCurrentFileDetails().fileInfo.absoluteFilePath()))
            imageCore.updateFolderInfoAsync();
        else
            shouldRetryFolderInfoUpdate = true;
    }

    const auto &fileList = getCurrentFileDetails().folderFileInfoList;
    if (fileList.isEmpty())
        return;

    int newIndex = getCurrentFileDetails().loadedIndexInFolder;
    int searchDirection = 0;

    switch (mode) {
    case GoToFileMode::constant: {
        newIndex = index;
        break;
    }
    case GoToFileMode::first: {
        newIndex = 0;
        searchDirection = 1;
        break;
    }
    case GoToFileMode::previous: {
        if (newIndex == 0) {
            if (qvGetSettingBool(SettingsManager::Setting::LoopFoldersEnabled))
                newIndex = fileList.size() - 1;
            else
                emit cancelSlideshow();
        } else
            newIndex--;
        searchDirection = -1;
        break;
    }
    case GoToFileMode::next: {
        if (fileList.size() - 1 == newIndex) {
            if (qvGetSettingBool(SettingsManager::Setting::LoopFoldersEnabled))
                newIndex = 0;
            else
                emit cancelSlideshow();
        } else
            newIndex++;
        searchDirection = 1;
        break;
    }
    case GoToFileMode::last: {
        newIndex = fileList.size() - 1;
        searchDirection = -1;
        break;
    }
    case GoToFileMode::random: {
        if (fileList.size() > 1) {
            int randomIndex;
            do {
                randomIndex = QRandomGenerator::global()->bounded(int(fileList.size()));
            } while (randomIndex == newIndex);
            newIndex = randomIndex;
        }
        searchDirection = 1;
        break;
    }
    }

    if (searchDirection != 0) {
        while (searchDirection == 1 && newIndex < fileList.size() - 1
               && !QFile::exists(fileList.value(newIndex).absoluteFilePath))
            newIndex++;
        while (searchDirection == -1 && newIndex > 0
               && !QFile::exists(fileList.value(newIndex).absoluteFilePath))
            newIndex--;
    }

    const QString nextImageFilePath = fileList.value(newIndex).absoluteFilePath;

    if (!QFile::exists(nextImageFilePath)
        || nextImageFilePath == getCurrentFileDetails().fileInfo.absoluteFilePath())
        return;

    if (shouldRetryFolderInfoUpdate) {
        // If the user just deleted a file through qView, closeImage will have been called which
        // empties currentFileDetails.fileInfo. In this case updateFolderInfo can't infer the
        // directory from fileInfo like it normally does, so we'll explicity pass in the folder
        // here.
        imageCore.updateFolderInfo(QFileInfo(nextImageFilePath).path());
    }

    loadFile(nextImageFilePath);
}

void QVGraphicsView::fitInViewMarginless(const QRectF &rect)
{
    int obscuredHeight = QVPlatformFunctions::getObscuredHeight(window()->windowHandle());

    // Set adjusted image size / bounding rect based on
    QSize adjustedImageSize = getCurrentFileDetails().loadedPixmapSize;
    QRectF adjustedBoundingRect = rect;

    switch (qvGetSettingInt(SettingsManager::Setting::CropMode)) {
    case SettingsManager::CropHeight: // only take into account height
    {
        adjustedImageSize.setWidth(1);
        adjustedBoundingRect.setWidth(1);
        break;
    }
    case SettingsManager::CropWidth: // only take into account width
    {
        adjustedImageSize.setHeight(1);
        adjustedBoundingRect.setHeight(1);
        break;
    }
    }
    adjustedBoundingRect.moveCenter(rect.center());

    if (!scene() || adjustedBoundingRect.isNull())
        return;

    // Reset the view scale to 1:1.
    QRectF unity = transform().mapRect(QRectF(0, 0, 1, 1));
    if (unity.isEmpty())
        return;
    scale(1 / unity.width(), 1 / unity.height());

    // Determine what we are resizing to
    const int adjWidth = width() - MARGIN;
    const int adjHeight = height() - MARGIN - obscuredHeight;

    // Let the core know how large a preview decode needs to be for this viewport
    imageCore.setFitTargetSize((QSizeF(adjWidth, adjHeight) * devicePixelRatioF()).toSize());

    QRectF viewRect;
    // Resize to window size unless you are meant to stop at the actual size, basically
    if (qvGetSettingBool(SettingsManager::Setting::PastActualSizeEnabled)
        || (adjustedImageSize.width() >= adjWidth || adjustedImageSize.height() >= adjHeight)) {
        viewRect = viewport()->rect().adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
        viewRect.setHeight(viewRect.height() - obscuredHeight);
    } else {
        // stop at actual size
        viewRect = QRect(QPoint(), getCurrentFileDetails().loadedPixmapSize);
        QPoint center = this->rect().center();
        center.setY(center.y() - obscuredHeight);
        viewRect.moveCenter(center);
    }

    if (viewRect.isEmpty())
        return;

    // Find the ideal x / y scaling ratio to fit \a rect in the view.
    QRectF sceneRect = transform().mapRect(adjustedBoundingRect);
    if (sceneRect.isEmpty())
        return;

    qreal xratio = viewRect.width() / sceneRect.width();
    qreal yratio = viewRect.height() / sceneRect.height();

    xratio = yratio = qMin(xratio, yratio);

    // Find and set the transform required to fit the original image
    // Compact version of above code
    QRectF sceneRect2 = transform().mapRect(QRectF({}, adjustedImageSize));
    qreal absoluteRatio =
            qMin(viewRect.width() / sceneRect2.width(), viewRect.height() / sceneRect2.height());

    absoluteTransform = QTransform::fromScale(absoluteRatio, absoluteRatio);

    // Remember which pixmap the absolute transform was computed from, so the
    // full-resolution decode can be swapped in without changing the on-screen size
    fitBasisPixmapSize = getCurrentFileDetails().loadedPixmapSize;

    // Scale and center on the center of \a rect.
    scale(xratio, yratio);
    centerOn(adjustedBoundingRect.center());

    // variables
    zoomBasis = transform();

    isOriginalSize = false;
    currentScale = 1.0;
    updateFilteringMode();
    zoomBasisScaleFactor = 1.0;
}

void QVGraphicsView::fitInViewMarginless(const QGraphicsItem *item)
{
    return fitInViewMarginless(item->sceneBoundingRect());
}

void QVGraphicsView::centerOn(const QPointF &pos)
{
    int obscuredHeight = QVPlatformFunctions::getObscuredHeight(window()->windowHandle());

    qreal width = viewport()->width();
    qreal height = viewport()->height() - obscuredHeight;
    QPointF viewPoint = transform().map(pos);

    if (isRightToLeft()) {
        qint64 horizontal = 0;
        horizontal += horizontalScrollBar()->minimum();
        horizontal += horizontalScrollBar()->maximum();
        horizontal -= int(viewPoint.x() - width / 2.0);
        horizontalScrollBar()->setValue(horizontal);
    } else {
        horizontalScrollBar()->setValue(int(viewPoint.x() - width / 2.0));
    }

    verticalScrollBar()->setValue(int(viewPoint.y() - obscuredHeight - (height / 2.0)));
}

void QVGraphicsView::centerOn(qreal x, qreal y)
{
    centerOn(QPointF(x, y));
}

void QVGraphicsView::centerOn(const QGraphicsItem *item)
{
    centerOn(item->sceneBoundingRect().center());
}

void QVGraphicsView::settingsUpdated()
{
    if (getCurrentFileDetails().isPixmapLoaded)
        resetScale();
}

void QVGraphicsView::closeImage()
{
    imageCore.closeImage();
}

void QVGraphicsView::jumpToNextFrame()
{
    imageCore.jumpToNextFrame();
}

void QVGraphicsView::setPaused(const bool &desiredState)
{
    imageCore.setPaused(desiredState);
}

void QVGraphicsView::setSpeed(const int &desiredSpeed)
{
    imageCore.setSpeed(desiredSpeed);
}

void QVGraphicsView::rotateImage(int rotation)
{
    imageCore.rotateImage(rotation);
}
