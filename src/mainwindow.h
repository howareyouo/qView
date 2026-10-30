#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "qvinfodialog.h"
#include "qvimagecore.h"
#include "qvgraphicsview.h"
#include "openwith.h"

#include "fileoperations.h"
#include "slideshowcontroller.h"
#include "urlopener.h"

#include <QMainWindow>
#include <QShortcut>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    QVGraphicsView &view() { return *graphicsView; }

    void requestPopulateOpenWithMenu();

    void populateOpenWithMenu(const QList<OpenWith::OpenWithItem> openWithItems);

    void openWithMenuAboutToShow();

    void refreshProperties();

    void updateWindowTitle();

    void updateWindowFilePath();

    void setWindowSize();

    bool getIsPixmapLoaded() const;

    void setJustLaunchedWithImage(bool value);

    void openRecent(int i);

    void openUrl(const QUrl &url);

    void pickUrl();

    void openContainingFolder();

    void openWith(const OpenWith::OpenWithItem &exec);

    void showFileInfo();

    void askDeleteFile(bool permanent);

    void deleteFile(bool permanent);

    void undoDelete();

    void copy();

    void paste();

    void rename();

    void resetZoom();

    void saveFrameAs();

    void pause();

    void nextFrame();

    void decreaseSpeed();

    void resetSpeed();

    void increaseSpeed();

    void toggleFullScreen();

    int getTitlebarOverlap() const;

    const QVImageCore::FileDetails &getCurrentFileDetails() const
    {
        return graphicsView->getCurrentFileDetails();
    }

public slots:
    void openFile(const QString &fileName);

    void toggleSlideshow();

    void slideshowAction();

    void cancelSlideshow();

    void fileChanged();

    void disableActions();

protected:
    bool event(QEvent *event) override;

    void contextMenuEvent(QContextMenuEvent *event) override;

    void showEvent(QShowEvent *event) override;

    void closeEvent(QCloseEvent *event) override;

    void changeEvent(QEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseDoubleClickEvent(QMouseEvent *event) override;

    void paintEvent(QPaintEvent *event) override;

    void fullscreenChanged();

    void updateFullscreenLabelGeometry();

    void updateFrameless();

protected slots:
    void settingsUpdated();
    void shortcutsUpdated();

private:
    Ui::MainWindow *ui;
    QVGraphicsView *graphicsView;

    void saveZoom();

    QMenu *contextMenu;
    QMenu *virtualMenu;

    QShortcut *escShortcut;

    QVInfoDialog *info;

    QColor customBackgroundColor;

    bool justLaunchedWithImage;

    Qt::WindowStates storedWindowState;

    FileOperations fileOperations;

    UrlOpener urlOpener;

    SlideshowController slideshowController;

    QTimer *populateOpenWithTimer;
    QFutureWatcher<QList<OpenWith::OpenWithItem>> openWithFutureWatcher;

    bool openWithNeedsPopulate;

    // Zoom the last session ended at, put back on the first image this window loads
    bool zoomRestorePending;
    qreal zoomRestoreScale;
    bool zoomRestoreOriginalSize;
};

#endif // MAINWINDOW_H
