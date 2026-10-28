#ifndef URLOPENER_H
#define URLOPENER_H

#include <QNetworkAccessManager>

class MainWindow;
class QUrl;

// Downloads images from a URL and opens them, extracted from MainWindow.
class UrlOpener
{
public:
    void openUrl(MainWindow &window, const QUrl &url);

    void pickUrl(MainWindow &window);

private:
    QNetworkAccessManager networkAccessManager;
};

#endif // URLOPENER_H
