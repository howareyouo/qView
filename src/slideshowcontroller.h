#ifndef SLIDESHOWCONTROLLER_H
#define SLIDESHOWCONTROLLER_H

#include <QTimer>

class MainWindow;

// Owns the slideshow timer and the start/stop action text updates.
class SlideshowController
{
public:
    void toggle(MainWindow &window);

    void cancel(MainWindow &window);

    void setInterval(int interval) { timer.setInterval(interval); }

    bool isActive() const { return timer.isActive(); }

    QTimer *timer() { return &timer; }

private:
    QTimer timer;
};

#endif // SLIDESHOWCONTROLLER_H
