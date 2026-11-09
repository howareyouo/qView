#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "qvinfodialog.h"
#include "qvimagecore.h"
#include "qvgraphicsview.h"
#include "openwith.h"

#include "fileoperations.h"
#include "urlopener.h"

#include <QMainWindow>
#include <QMenuBar>
#include <QShortcut>
#include <QTimer>

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

    void populateOpenWithMenu(QList<OpenWith::OpenWithItem> openWithItems);

    void openWithMenuAboutToShow();

    void refreshProperties();

    void updateWindowTitle();

    void updateWindowFilePath();

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

    void saveFrameAs();

    void pause();

    void nextFrame();

    void decreaseSpeed();

    void resetSpeed();

    void increaseSpeed();

    void toggleFullScreen();

    int getTitlebarOverlap() const;

    bool isMenuBarVisible() const
    {
        // Deliberately avoids QMainWindow::menuBar(), which would create an empty one on demand
        return menuBarBuilt && menuBar()->isVisible();
    }

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

    // Shows the fullscreen label only when the window is fullscreen and the settings ask for
    // details, and gives it its floating position
    void refreshFullscreenLabel();

    void updateFrameless();

protected slots:
    void settingsUpdated();
    void shortcutsUpdated();

private:
    // Builds the menu bar the first time it is needed. It is skipped entirely when the menu bar
    // is disabled, since building it is one of the more expensive parts of window setup and its
    // actions also exist as clones in the virtual menu that holds the window shortcuts.
    void ensureMenuBar();

    Ui::MainWindow *ui;
    QVGraphicsView *graphicsView;

    QMenu *contextMenu;
    QMenu *virtualMenu;

    bool menuBarBuilt = false;

    QShortcut *escShortcut;

    QVInfoDialog *info;

    QColor customBackgroundColor;

    Qt::WindowStates storedWindowState;

    FileOperations fileOperations;

    UrlOpener urlOpener;

    QTimer slideshowTimer;

    QFutureWatcher<QList<OpenWith::OpenWithItem>> openWithFutureWatcher;

    bool openWithNeedsPopulate;
};

#endif // MAINWINDOW_H
