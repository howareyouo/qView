#include "slideshowcontroller.h"
#include "mainwindow.h"
#include "qvapplication.h"

#include <QIcon>

void SlideshowController::toggle(MainWindow &window)
{
    const auto slideshowActions =
            qvApp->getActionManager().getAllClonesOfAction("slideshow", &window);

    if (timer.isActive()) {
        timer.stop();
        for (const auto &slideshowAction : slideshowActions) {
            slideshowAction->setText(window.tr("Start S&lideshow"));
            slideshowAction->setIcon(QIcon::fromTheme("media-playback-start"));
        }
    } else {
        timer.start();
        for (const auto &slideshowAction : slideshowActions) {
            slideshowAction->setText(window.tr("Stop S&lideshow"));
            slideshowAction->setIcon(QIcon::fromTheme("media-playback-stop"));
        }
    }
}

void SlideshowController::cancel(MainWindow &window)
{
    if (timer.isActive())
        toggle(window);
}
