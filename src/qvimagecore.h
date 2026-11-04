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
#include <QPointer>
#include <QScreen>

#include <QColorSpace>

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

        // Size of the full-resolution decode; differs from loadedPixmapSize only while
        // a preview decode is on screen
        QSize fullPixmapSize;

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

        // True when decoded at reduced size for a fast first display
        bool isPreview = false;
        // True when this is the full-resolution decode replacing a shown preview;
        // never persisted in the cache
        bool isFullUpgrade = false;
        // True when the format can animate at all; static formats skip the QMovie probe
        bool mayAnimate = false;

        // Cache key of this decode, computed once per read instead of on every cache access
        QString cacheKey;
    };

    // How the navigation cursor moves through the current folder listing
    enum class GoToFileMode { constant, first, previous, next, last, random };
    Q_ENUM(GoToFileMode)

    explicit QVImageCore(QObject *parent = nullptr);

    void loadFile(const QString &fileName, bool isReloading = false);

    // Moves the cursor and loads the file it lands on. Returns false when the listing has
    // nowhere to go in that direction and looping is off, so callers can stop what is
    // driving the navigation.
    bool goToFile(GoToFileMode mode, int index = 0);

    void loadPixmap(const ReadData &readData);
    void closeImage();
    void updateFolderInfo(QString dirPath = QString());
    void updateFolderInfoAsync(QString dirPath = QString());
    void requestCaching(const QColorSpace &targetColorSpace);
    QColorSpace getTargetColorSpace() const;
    QColorSpace detectDisplayColorSpace() const;

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

    // Sets the size in device pixels that preview decodes should target
    void setFitTargetSize(const QSize &devicePixelSize) { fitTargetSize = devicePixelSize; }

signals:
    void animatedFrameChanged(QRect rect);

    void updateLoadedPixmapItem();

    void fileChanged();

    void fullPixmapLoaded();

protected:
    void loadEmptyPixmap();
    FileDetails getEmptyFileDetails();

private:
    // The settings that shape the folder listing. A settings change only needs the folder to be
    // scanned again when one of these differs from what the current listing was built with.
    struct FolderScanSettings
    {
        int sortMode = 0;
        bool sortDescending = false;
        bool skipHidden = false;
        bool allowMimeContentDetection = false;

        bool operator==(const FolderScanSettings &other) const
        {
            return sortMode == other.sortMode && sortDescending == other.sortDescending
                    && skipHidden == other.skipHidden
                    && allowMimeContentDetection == other.allowMimeContentDetection;
        }
        bool operator!=(const FolderScanSettings &other) const { return !(*this == other); }
    };

    FolderScanSettings currentFolderScanSettings() const;

    FolderScanParams makeFolderScanParams(const QString &dirPath) const;

    ReadData readFile(const QString &fileName, const QColorSpace &targetColorSpace,
                      const FolderScanParams &folderScanParams, QSize previewTargetSize = {},
                      qint64 fileSize = -1);
    static QString getPixmapCacheKey(const QString &absoluteFilePath, const qint64 &fileSize,
                                     const QColorSpace &targetColorSpace);
    void addToCache(const ReadData &readImageAndFileInfo);
    void requestCachingFile(const QString &filePath, const QColorSpace &targetColorSpace);

    void startFullDecode(const QString &filePath, const QColorSpace &targetColorSpace);

    // Preload decoding: a small bounded queue keeps the file the user is opening from waiting
    // behind the background decodes
    void startPendingPreloadDecodes();
    void startPreloadDecode(const QString &filePath);

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
    QStringList pendingPreloadFiles;
    int preloadDecodesRunning = 0;

    // What the current folder listing was built with, to tell whether a settings change
    // actually invalidates it
    mutable FolderScanSettings lastFolderScanSettings;

    int largestDimension;

    bool waitingOnLoad;

    // Size in device pixels that preview decodes should target
    QSize fitTargetSize;

    // Files with a full-resolution decode currently running in the background
    QStringList fullDecodeFilesInProgress;

    // Display color space cache: reading and parsing the screen ICC profile costs a file read
    // and only changes when the window moves to another screen
    mutable QPointer<QScreen> displayColorSpaceScreen;
    mutable QColorSpace cachedDisplayColorSpace;
    mutable bool displayColorSpaceCached = false;
};

#endif // QVIMAGECORE_H
