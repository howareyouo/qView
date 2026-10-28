#ifndef QVIMAGECORE_H
#define QVIMAGECORE_H

#include "folderscanner.h"

#include <QObject>
#include <QImageReader>
#include <QPixmap>
#include <QMovie>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QCache>
#include <QElapsedTimer>

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#  include <QColorSpace>
#else
typedef QString QColorSpace;
#endif

class QVImageCore : public QObject
{
    Q_OBJECT

public:
    using CompatibleFile = FolderScanner::CompatibleFile;

    struct ErrorData
    {
        bool hasError = false;
        int errorNum = 0;
        QString errorString;
    };

    struct FileDetails
    {
        QFileInfo fileInfo;
        QList<CompatibleFile> folderFileInfoList;
        int loadedIndexInFolder = -1;
        bool isLoadRequested = false;
        bool isPixmapLoaded = false;
        bool isMovieLoaded = false;
        QSize baseImageSize;
        QSize loadedPixmapSize;
        QElapsedTimer timeSinceLoaded;
        ErrorData errorData;

        void updateLoadedIndexInFolder();
    };

    using DirInfo = FolderScanner::DirInfo;
    using FolderScanParams = FolderScanner::FolderScanParams;

    using FolderScanInfo = FolderScanner::FolderScanInfo;

    struct ReadData
    {
        QImage image;
        QString absoluteFilePath;
        qint64 fileSize;
        QSize imageSize;
        QColorSpace targetColorSpace;
        ErrorData errorData;

        // Only filled in when the file was read along with a scan of its folder
        bool folderInfoScanned = false;
        FolderScanInfo folderScanInfo;
        int loadedIndexInFolder = -1;
    };

    explicit QVImageCore(QObject *parent = nullptr);

    void loadFile(const QString &fileName, bool isReloading = false);
    ReadData readFile(const QString &fileName, const QColorSpace &targetColorSpace,
                      const FolderScanParams &folderScanParams);
    void loadPixmap(const ReadData &readData);
    void closeImage();
    QList<CompatibleFile> getCompatibleFiles(const QString &dirPath) const;
    void updateFolderInfo(QString dirPath = QString());
    void updateFolderInfoAsync(QString dirPath = QString());
    void requestCaching(const QColorSpace &targetColorSpace);
    void requestCachingFile(const QString &filePath, const QColorSpace &targetColorSpace);
    void addToCache(const ReadData &&readImageAndFileInfo);
    static QString getPixmapCacheKey(const QString &absoluteFilePath, const qint64 &fileSize,
                                     const QColorSpace &targetColorSpace);
    QColorSpace getTargetColorSpace() const;
    QColorSpace detectDisplayColorSpace() const;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && QT_VERSION < QT_VERSION_CHECK(6, 7, 2)
    static bool removeTinyDataTagsFromIccProfile(QByteArray &profile);
#endif

    void settingsUpdated();

    void jumpToNextFrame();
    void setPaused(bool desiredState);
    void setSpeed(int desiredSpeed);

    void rotateImage(int rotation);
    QImage matchCurrentRotation(const QImage &imageToRotate);
    QPixmap matchCurrentRotation(const QPixmap &pixmapToRotate);

    QPixmap scaleExpensively(const int desiredWidth, const int desiredHeight);
    QPixmap scaleExpensively(const QSizeF desiredSize);

    // returned const reference is read-only
    const QPixmap &getLoadedPixmap() const { return loadedPixmap; }
    const QMovie &getLoadedMovie() const { return loadedMovie; }
    const FileDetails &getCurrentFileDetails() const { return currentFileDetails; }
    int getCurrentRotation() const { return currentRotation; }

signals:
    void animatedFrameChanged(QRect rect);

    void updateLoadedPixmapItem();

    void fileChanged();

protected:
    void loadEmptyPixmap();
    FileDetails getEmptyFileDetails();

private:
    FolderScanParams makeFolderScanParams(const QString &dirPath) const;

    QPixmap loadedPixmap;
    QMovie loadedMovie;

    FileDetails currentFileDetails;
    int currentRotation;

    QFutureWatcher<ReadData> loadFutureWatcher;
    QFutureWatcher<FolderScanInfo> folderInfoFutureWatcher;

    int colorSpaceConversion;

    static QCache<QString, ReadData> imageCache;

    DirInfo lastDirInfo;
    unsigned randomSortSeed = 0;

    QStringList lastFilesPreloaded;
    QStringList preloadFilesInProgress;
    QString waitingOnPreloadFile;

    int largestDimension;

    bool waitingOnLoad;
};

#endif // QVIMAGECORE_H
