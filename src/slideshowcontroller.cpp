#include "slideshowcontroller.h"
#include "mainwindow.h"
#include "qvapplication.h"

#include <QIcon>

void SlideshowController::toggle(MainWindow &window)
{
    if (timer.isActive())
        timer.stop();
    else
        timer.start();

    const bool running = timer.isActive();
    const QString text = window.tr(running ? "Stop S&lideshow" : "Start S&lideshow");
    const QIcon icon = QIcon::fromTheme(running ? "media-playback-stop" : "media-playback-start");

    qvApp->getActionManager().forEachCloneOfAction(
            "slideshow", [&text, &icon](QAction *slideshowAction) {
                slideshowAction->setText(text);
                slideshowAction->setIcon(icon);
            }, &window);
}

void SlideshowController::cancel(MainWindow &window)
{
    if (timer.isActive())
        toggle(window);
}
