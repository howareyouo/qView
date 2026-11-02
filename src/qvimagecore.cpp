#include "qvimagecore.h"
#include "qvapplication.h"
#include "qvplatformfunctions.h"
#include "qvprofiler.h"
#include <QMessageBox>
#include <QImageIOHandler>
#include <QUrl>
#include <QSettings>
#include <QtConcurrent/QtConcurrentRun>
#include <QIcon>
#include <QGuiApplication>
#include <QScreen>

QCache<QString, QVImageCore::ReadData> QVImageCore::imageCache;

// Preloads decode at reduced size, and only a couple of them run at a time so they cannot crowd
// out the decode of the file the user is actually opening
static constexpr int preloadDecodeConcurrency = 2;

QVImageCore::QVImageCore(QObject *parent) : QObject(parent)
{
    // Set allocation limit to 8 GiB on Qt6
    QImageReader::setAllocationLimit(8192);

    currentRotation = 0;

    connect(&loadedMovie, &QMovie::updated, this, &QVImageCore::animatedFrameChanged);

    connect(&loadFutureWatcher, &QFutureWatcher<ReadData>::finished, this, [this]() {
        const ReadData result = loadFutureWatcher.result();
        loadPixmap(result);
        // The first paint was a preview decode; get the full-resolution decode going
        if (result.isPreview)
            startFullDecode(result.absoluteFilePath, result.targetColorSpace);
    });

    connect(&folderInfoFutureWatcher, &QFutureWatcher<FolderScanInfo>::finished, this, [this]() {
        const FolderScanInfo scanInfo = folderInfoFutureWatcher.result();
        // The user may have navigated somewhere else while the scan was running, so only
        // apply the results if the loaded file still lives in the scanned directory
        if (currentFileDetails.fileInfo.path() != scanInfo.dirInfo.dirPath)
            return;
        currentFileDetails.folderFileInfoList = scanInfo.fileList;
        lastDirInfo = scanInfo.dirInfo;
        randomSortSeed = scanInfo.randomSortSeed;
        currentFileDetails.updateLoadedIndexInFolder();
    });

    largestDimension = 0;
    QSize largestScreenSize;
    const auto screenList = QGuiApplication::screens();
    for (auto const &screen : screenList) {
        int largerDimension;
        if (screen->size().height() > screen->size().width()) {
            largerDimension = screen->size().height();
        } else {
            largerDimension = screen->size().width();
        }

        if (largerDimension > largestDimension) {
            largestDimension = largerDimension;
            largestScreenSize = screen->size() * screen->devicePixelRatio();
        }
    }
    // Initial preview-decode target until the view reports its fitted viewport size
    fitTargetSize = largestScreenSize;

    waitingOnLoad = false;

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this,
            &QVImageCore::settingsUpdated);
    settingsUpdated();
}

void QVImageCore::loadFile(const QString &fileName, bool isReloading)
{
    QV_PROFILE_SCOPE("open: load file (gui)");

    if (waitingOnLoad) {
        return;
    }

    QString sanitaryFileName = fileName;

    // sanitize file name if necessary
    QUrl sanitaryUrl = QUrl(fileName);
    if (sanitaryUrl.isLocalFile())
        sanitaryFileName = sanitaryUrl.toLocalFile();

    QFileInfo fileInfo(sanitaryFileName);
    sanitaryFileName = fileInfo.absoluteFilePath();

    if (fileInfo.isDir()) {
        updateFolderInfo(sanitaryFileName);
        if (currentFileDetails.folderFileInfoList.isEmpty())
            closeImage();
        else
            loadFile(currentFileDetails.folderFileInfoList.at(0).absoluteFilePath);
        return;
    }

    // Pause playing movie because it feels better that way
    setPaused(true);

    currentFileDetails.isLoadRequested = true;
    waitingOnLoad = true;

    // Scan the folder in the same worker task as the decoding, unless the listing we already have
    // contains this file
    FolderScanParams folderScanParams;
    if (FolderScanner::findLoadedIndexInFolder(fileInfo, currentFileDetails.folderFileInfoList)
        == -1)
        folderScanParams = makeFolderScanParams(fileInfo.path());

    QColorSpace targetColorSpace = getTargetColorSpace();
    QString cacheKey = getPixmapCacheKey(sanitaryFileName, fileInfo.size(), targetColorSpace);

    // A preview decode is only worthwhile for the initial display of a file, not reloads. The
    // viewport size is the largest the preview can be shown at, so decoding any more than that
    // would only make the first paint wait for pixels that cannot be displayed yet.
    QSize previewTargetSize;
    if (!isReloading && fitTargetSize.isValid())
        previewTargetSize = fitTargetSize;

    // check if cached already before loading the long way
    auto *cachedData = isReloading ? nullptr : QVImageCore::imageCache.take(cacheKey);
    if (cachedData != nullptr) {
        ReadData readData = *cachedData;
        delete cachedData;
        loadPixmap(readData);
        // The cached entry was a preview decode; get the full decode going
        if (readData.isPreview)
            startFullDecode(sanitaryFileName, targetColorSpace);
    }
    // or see if the preloader is already working on it
    else if (preloadFilesInProgress.contains(sanitaryFileName)) {
        waitingOnPreloadFile = sanitaryFileName;
    } else {
        // A preload that was only queued is superseded by the decode requested now
        pendingPreloadFiles.removeOne(sanitaryFileName);
        loadFutureWatcher.setFuture(QtConcurrent::run(&QVImageCore::readFile, this,
                                                      sanitaryFileName, targetColorSpace,
                                                      folderScanParams, previewTargetSize,
                                                      fileInfo.size()));
    }
}

QVImageCore::ReadData QVImageCore::readFile(const QString &fileName,
                                            const QColorSpace &targetColorSpace,
                                            const FolderScanParams &folderScanParams,
                                            QSize previewTargetSize, qint64 fileSize)
{
    QV_PROFILE_SCOPE("decode: read file");

    QImageReader imageReader;
    imageReader.setAutoTransform(true);

    imageReader.setFileName(fileName);

    QImage readImage;
    QElapsedTimer readTimer;
    readTimer.start();
    const bool isSvg = imageReader.format() == "svg" || imageReader.format() == "svgz";
    QSize originalSize = imageReader.size();
    bool isPreview = false;
    if (isSvg) {
        // Render vectors into a high resolution
        QIcon icon;
        icon.addFile(fileName);
        readImage = icon.pixmap(largestDimension).toImage();
        // If this fails, try reading the normal way so that a proper error message is given
        if (readImage.isNull())
            readImage = imageReader.read();
    } else {
        // Fast first paint: when the format supports decoding at a reduced size, do a
        // preview decode now and let the full-resolution decode follow in the background
        if (previewTargetSize.isValid() && imageReader.supportsOption(QImageIOHandler::ScaledSize)
            && originalSize.isValid()) {
            const QSize previewSize = originalSize.scaled(previewTargetSize, Qt::KeepAspectRatio);
            // Only bother when this actually decodes noticeably less than the full image
            if (previewSize.width() < originalSize.width()
                && previewSize.height() < originalSize.height()) {
                imageReader.setScaledSize(previewSize);
                isPreview = true;
            }
        }

        readImage = imageReader.read();
    }
    QVProfile::record("decode: file read", readTimer.nsecsElapsed());

    // Whether the format can animate at all. Static formats then skip the QMovie probe in
    // loadPixmap, which opens the file a second time; PNG is always probed because animated PNGs
    // are not advertised by the reader the way GIF and WebP are.
    const bool mayAnimate = imageReader.supportsAnimation() || imageReader.format() == "png";

    QElapsedTimer convertTimer;
    convertTimer.start();
    readImage.convertTo(QImage::Format::Format_ARGB32_Premultiplied);

    // Assume image is sRGB if it doesn't specify
    if (!readImage.colorSpace().isValid())
        readImage.setColorSpace(QColorSpace::SRgb);

    // Convert image color space if we have a target that's different
    if (targetColorSpace.isValid() && readImage.colorSpace() != targetColorSpace)
        readImage.convertToColorSpace(targetColorSpace);
    QVProfile::record("decode: format+colorspace", convertTimer.nsecsElapsed());

    QFileInfo fileInfo(fileName);

    // The size is already known when the file was stat'ed by the caller, so it does not need
    // to be looked up again here
    const qint64 actualFileSize = fileSize >= 0 ? fileSize : fileInfo.size();

    ReadData readData = { readImage,        fileInfo.absoluteFilePath(),
                          actualFileSize,   originalSize,
                          targetColorSpace, {} };
    readData.cacheKey = getPixmapCacheKey(readData.absoluteFilePath, readData.fileSize,
                                          targetColorSpace);

    if (readImage.isNull()) {
        readData.errorData = { true, imageReader.error(), imageReader.errorString() };
    }
    readData.isPreview = isPreview;
    readData.mayAnimate = mayAnimate;

    // Scan the folder while the file is loaded instead of in a separate blocking step
    if (folderScanParams.scanFolder) {
        QV_PROFILE_SCOPE("decode: folder scan");
        readData.folderInfoScanned = true;
        readData.folderScanInfo = FolderScanner::scanFolder(folderScanParams);
        readData.loadedIndexInFolder =
                FolderScanner::findLoadedIndexInFolder(fileInfo, readData.folderScanInfo.fileList);
    }

    return readData;
}

void QVImageCore::loadPixmap(const ReadData &readData)
{
    QV_PROFILE_SCOPE("open: load pixmap (gui)");

    // A full decode replacing a preview that is already on screen
    const bool isUpgrade = readData.isFullUpgrade && currentFileDetails.isPixmapLoaded;

    if (readData.errorData.hasError) {
        currentFileDetails = getEmptyFileDetails();
        currentFileDetails.errorData = readData.errorData;
    } else {
        currentFileDetails.errorData = {};
    }

    // Do this first so we can keep folder info even when loading errored files
    currentFileDetails.fileInfo = QFileInfo(readData.absoluteFilePath);
    if (readData.folderInfoScanned) {
        // The folder listing was built in the same task as the file, so adopt it as-is
        currentFileDetails.folderFileInfoList = readData.folderScanInfo.fileList;
        currentFileDetails.loadedIndexInFolder = readData.loadedIndexInFolder;
        lastDirInfo = readData.folderScanInfo.dirInfo;
        randomSortSeed = readData.folderScanInfo.randomSortSeed;
    } else {
        currentFileDetails.updateLoadedIndexInFolder();
        if (currentFileDetails.loadedIndexInFolder == -1)
            updateFolderInfo();
    }

    // Reset mechanism to avoid stalling while loading
    waitingOnLoad = false;

    if (currentFileDetails.errorData.hasError) {
        loadEmptyPixmap();
        return;
    }

    QElapsedTimer pixmapTimer;
    pixmapTimer.start();
    loadedPixmap = QPixmap::fromImage(matchCurrentRotation(readData.image));
    QVProfile::record("open: pixmap upload", pixmapTimer.nsecsElapsed());

    // Set file details
    currentFileDetails.isPixmapLoaded = true;
    currentFileDetails.baseImageSize = readData.imageSize;
    currentFileDetails.loadedPixmapSize = loadedPixmap.size();
    if (currentFileDetails.baseImageSize == QSize(-1, -1)) {
        qInfo() << "QImageReader::size gave an invalid size for "
                        + currentFileDetails.fileInfo.fileName()
                        + ", using size from loaded pixmap";
        currentFileDetails.baseImageSize = currentFileDetails.loadedPixmapSize;
    }

    // Size of the full-resolution image, used e.g. for fitting the window to the image
    // while a smaller preview decode is on screen
    currentFileDetails.fullPixmapSize = currentFileDetails.baseImageSize;
    if (currentRotation % 180 != 0)
        currentFileDetails.fullPixmapSize.transpose();

    addToCache(readData);

    // Only refresh the display; recents, window sizing, and the movie are already set up
    if (isUpgrade) {
        emit fullPixmapLoaded();
        return;
    }

    // Animation detection, skipped for formats that cannot animate: QMovie opens the file again
    // to find out, which is wasted work on every still image
    if (readData.mayAnimate) {
        QElapsedTimer movieTimer;
        movieTimer.start();
        loadedMovie.setFormat("");
        loadedMovie.stop();
        loadedMovie.setFileName(currentFileDetails.fileInfo.absoluteFilePath());

        // APNG workaround
        if (loadedMovie.format() == "png") {
            loadedMovie.setFormat("apng");
            loadedMovie.setFileName(currentFileDetails.fileInfo.absoluteFilePath());
        }

        if (loadedMovie.isValid() && loadedMovie.frameCount() != 1)
            loadedMovie.start();

        currentFileDetails.isMovieLoaded = loadedMovie.state() == QMovie::Running;

        if (!currentFileDetails.isMovieLoaded)
            if (auto device = loadedMovie.device())
                device->close();
        QVProfile::record("open: movie probe", movieTimer.nsecsElapsed());
    } else {
        loadedMovie.stop();
        loadedMovie.setFormat("");
        loadedMovie.setFileName(QString());
        currentFileDetails.isMovieLoaded = false;
    }

    currentFileDetails.timeSinceLoaded.start();

    emit fileChanged();

    requestCaching(readData.targetColorSpace);
}

void QVImageCore::closeImage()
{
    currentFileDetails = getEmptyFileDetails();
    loadEmptyPixmap();
}

void QVImageCore::loadEmptyPixmap()
{
    loadedPixmap = QPixmap();
    loadedMovie.stop();
    loadedMovie.setFileName("");

    emit fileChanged();
}

QVImageCore::FileDetails QVImageCore::getEmptyFileDetails()
{
    return { QFileInfo(),
             currentFileDetails.folderFileInfoList,
             currentFileDetails.loadedIndexInFolder,
             false,
             false,
             false,
             QSize(),
             QSize(),
             QElapsedTimer(),
             {},
             QSize() };
}

void QVImageCore::updateFolderInfo(QString dirPath)
{
    QV_PROFILE_SCOPE("folderscan: sync scan (gui)");

    if (dirPath.isEmpty()) {
        dirPath = currentFileDetails.fileInfo.path();

        // No directory specified and a file is not already loaded from which we can infer one
        if (dirPath.isEmpty())
            return;
    }

    const FolderScanInfo scanInfo = FolderScanner::scanFolder(makeFolderScanParams(dirPath));

    currentFileDetails.folderFileInfoList = scanInfo.fileList;
    lastDirInfo = scanInfo.dirInfo;
    randomSortSeed = scanInfo.randomSortSeed;

    // Set current file index variable
    currentFileDetails.updateLoadedIndexInFolder();
}

void QVImageCore::updateFolderInfoAsync(QString dirPath)
{
    if (dirPath.isEmpty()) {
        dirPath = currentFileDetails.fileInfo.path();

        // No directory specified and a file is not already loaded from which we can infer one
        if (dirPath.isEmpty())
            return;
    }

    // A scan is already in flight; its result will be applied once it finishes
    if (folderInfoFutureWatcher.isRunning())
        return;

    const FolderScanParams params = makeFolderScanParams(dirPath);
    folderInfoFutureWatcher.setFuture(QtConcurrent::run([params] {
        QV_PROFILE_SCOPE("folderscan: async scan");
        return FolderScanner::scanFolder(params);
    }));
}

QVImageCore::FolderScanSettings QVImageCore::currentFolderScanSettings() const
{
    FolderScanSettings settings;
    settings.sortMode = qvGetSettingInt(SettingsManager::Setting::SortMode);
    settings.sortDescending = qvGetSettingBool(SettingsManager::Setting::SortDescending);
    settings.skipHidden = qvApp->getSettingsManager().getBool(SettingsManager::Setting::SkipHidden);
    settings.allowMimeContentDetection =
            qvGetSettingBool(SettingsManager::Setting::AllowMimeContentDetection);
    return settings;
}

QVImageCore::FolderScanParams QVImageCore::makeFolderScanParams(const QString &dirPath) const
{
    const FolderScanSettings settings = currentFolderScanSettings();

    FolderScanParams params;
    params.scanFolder = true;
    params.dirPath = dirPath;
    params.sortMode = settings.sortMode;
    params.sortDescending = settings.sortDescending;
    params.skipHidden = settings.skipHidden;
    params.allowMimeContentDetection = settings.allowMimeContentDetection;
    params.lastDirInfo = lastDirInfo;
    params.randomSortSeed = randomSortSeed;

    // Remember what the listing is being built with, so a settings change can tell whether it
    // still matches without scanning the folder again
    lastFolderScanSettings = settings;

    return params;
}

void QVImageCore::requestCaching(const QColorSpace &targetColorSpace)
{
    QV_PROFILE_SCOPE("open: preload requests");

    int preloadingMode = qvGetSettingInt(SettingsManager::Setting::PreloadingMode);
    if (preloadingMode == SettingsManager::PreloadingDisabled) {
        QVImageCore::imageCache.clear();
        pendingPreloadFiles.clear();
        return;
    }

    int preloadingDistance = 1;

    if (preloadingMode > SettingsManager::PreloadingAdjacent)
        preloadingDistance = 4;

    QStringList filesToPreload;
    for (int i = currentFileDetails.loadedIndexInFolder - preloadingDistance;
         i <= currentFileDetails.loadedIndexInFolder + preloadingDistance; i++) {
        int index = i;

        // Don't try to cache the currently loaded image
        if (index == currentFileDetails.loadedIndexInFolder)
            continue;

        // keep within index range
        if (qvGetSettingBool(SettingsManager::Setting::LoopFoldersEnabled)) {
            if (index > currentFileDetails.folderFileInfoList.length() - 1)
                index = index - (currentFileDetails.folderFileInfoList.length());
            else if (index < 0)
                index = index + (currentFileDetails.folderFileInfoList.length());
        }

        // if still out of range after looping, just cancel the cache for this index
        if (index > currentFileDetails.folderFileInfoList.length() - 1 || index < 0
            || currentFileDetails.folderFileInfoList.isEmpty())
            continue;

        QString filePath = currentFileDetails.folderFileInfoList[index].absoluteFilePath;
        filesToPreload.append(filePath);

        requestCachingFile(filePath, targetColorSpace);
    }

    // Neighbors of a file the user has already left should not keep the queue busy
    for (int i = pendingPreloadFiles.size() - 1; i >= 0; --i) {
        if (!filesToPreload.contains(pendingPreloadFiles.at(i)))
            pendingPreloadFiles.removeAt(i);
    }

    lastFilesPreloaded = filesToPreload;
}

void QVImageCore::requestCachingFile(const QString &filePath, const QColorSpace &targetColorSpace)
{
    QV_PROFILE_SCOPE("preload: request file");

    QFile imgFile(filePath);
    const qint64 fileSize = imgFile.size();
    QString cacheKey = getPixmapCacheKey(filePath, fileSize, targetColorSpace);

    // check if image is already loaded or requested
    if (QVImageCore::imageCache.contains(cacheKey) || lastFilesPreloaded.contains(filePath))
        return;

    if (fileSize / 1024 > QVImageCore::imageCache.maxCost() / 2)
        return;

    // A file that is already queued or being decoded does not need a second decode
    if (pendingPreloadFiles.contains(filePath) || preloadFilesInProgress.contains(filePath))
        return;

    pendingPreloadFiles.append(filePath);
    startPendingPreloadDecodes();
}

void QVImageCore::startPendingPreloadDecodes()
{
    while (preloadDecodesRunning < preloadDecodeConcurrency && !pendingPreloadFiles.isEmpty())
        startPreloadDecode(pendingPreloadFiles.takeFirst());
}

void QVImageCore::startPreloadDecode(const QString &filePath)
{
    QV_PROFILE_SCOPE("preload: decode start");

    ++preloadDecodesRunning;
    preloadFilesInProgress.append(filePath);

    // Preloads decode at the size the viewport can show, which costs a fraction of a full decode.
    // Opening the file later paints that preview right away and upgrades it in the background.
    QSize previewTargetSize;
    if (fitTargetSize.isValid())
        previewTargetSize = fitTargetSize;

    auto *cacheFutureWatcher = new QFutureWatcher<ReadData>();
    connect(cacheFutureWatcher, &QFutureWatcher<ReadData>::finished, this,
            [cacheFutureWatcher, this]() {
                const ReadData readData = cacheFutureWatcher->result();
                preloadFilesInProgress.removeAll(readData.absoluteFilePath);
                preloadDecodesRunning--;
                cacheFutureWatcher->deleteLater();

                if (waitingOnPreloadFile == readData.absoluteFilePath) {
                    loadPixmap(readData);
                    waitingOnPreloadFile = QString();
                    // A preload is a preview decode, so the full decode has to follow
                    if (readData.isPreview)
                        startFullDecode(readData.absoluteFilePath, readData.targetColorSpace);
                } else {
                    addToCache(readData);
                }

                startPendingPreloadDecodes();
            });

    cacheFutureWatcher->setFuture(QtConcurrent::run(&QVImageCore::readFile, this, filePath,
                                                    getTargetColorSpace(), FolderScanParams(),
                                                    previewTargetSize));
}

void QVImageCore::addToCache(const ReadData &readData)
{
    QV_PROFILE_SCOPE("open: cache insert");

    // Full decodes that replace a preview on screen are already being displayed
    if (readData.isFullUpgrade)
        return;

    if (readData.image.isNull())
        return;

    // The key is computed once while decoding, so it does not have to be rebuilt from the file
    // path, size and ICC profile on every cache access
    const QString cacheKey = readData.cacheKey.isEmpty()
            ? getPixmapCacheKey(readData.absoluteFilePath, readData.fileSize,
                                readData.targetColorSpace)
            : readData.cacheKey;
    qint64 pixmapMemoryBytes = static_cast<qint64>(readData.image.width()) * readData.image.height()
            * readData.image.depth() / 8;

    qint64 pixmapMemoryKiB = qMax(pixmapMemoryBytes / 1024, 1LL);

    // Don't keep the folder listing cached, it can be very large and is only needed once
    ReadData cacheEntry;
    cacheEntry.image = readData.image;
    cacheEntry.absoluteFilePath = readData.absoluteFilePath;
    cacheEntry.fileSize = readData.fileSize;
    cacheEntry.imageSize = readData.imageSize;
    cacheEntry.targetColorSpace = readData.targetColorSpace;
    cacheEntry.errorData = readData.errorData;
    cacheEntry.isPreview = readData.isPreview;
    cacheEntry.mayAnimate = readData.mayAnimate;
    cacheEntry.cacheKey = cacheKey;
    QVImageCore::imageCache.insert(cacheKey, new ReadData(std::move(cacheEntry)), pixmapMemoryKiB);
}

void QVImageCore::startFullDecode(const QString &filePath, const QColorSpace &targetColorSpace)
{
    // One full decode per file at a time
    if (fullDecodeFilesInProgress.contains(filePath))
        return;

    fullDecodeFilesInProgress.append(filePath);

    auto *fullDecodeWatcher = new QFutureWatcher<ReadData>(this);
    connect(fullDecodeWatcher, &QFutureWatcher<ReadData>::finished, this,
            [fullDecodeWatcher, filePath, this]() {
                const ReadData upgradeData = fullDecodeWatcher->result();
                fullDecodeFilesInProgress.removeAll(filePath);
                fullDecodeWatcher->deleteLater();

                if (upgradeData.errorData.hasError)
                    return;

                // Swap in the full decode only if this file is still the one displayed;
                // otherwise put it in the cache for later
                if (currentFileDetails.fileInfo.absoluteFilePath() == filePath
                    && currentFileDetails.isPixmapLoaded) {
                    ReadData readData = upgradeData;
                    readData.isFullUpgrade = true;
                    loadPixmap(readData);
                } else {
                    addToCache(upgradeData);
                }
            });

    fullDecodeWatcher->setFuture(QtConcurrent::run(&QVImageCore::readFile, this, filePath,
                                                   targetColorSpace, FolderScanParams(), QSize()));
}

QString QVImageCore::getPixmapCacheKey(const QString &absoluteFilePath, const qint64 &fileSize,
                                       const QColorSpace &targetColorSpace)
{
    QString targetColorSpaceHash =
            QCryptographicHash::hash(targetColorSpace.iccProfile(), QCryptographicHash::Md5)
                    .toHex();
    return absoluteFilePath + "\n" + QString::number(fileSize) + "\n" + targetColorSpaceHash;
}

QColorSpace QVImageCore::getTargetColorSpace() const
{
    return colorSpaceConversion == SettingsManager::ColorSpaceAutoDetect
            ? detectDisplayColorSpace()
            : colorSpaceConversion == SettingsManager::ColorSpaceSRgb ? QColorSpace::SRgb
            : colorSpaceConversion == SettingsManager::ColorSpaceDisplayP3 ? QColorSpace::DisplayP3
                                                                           : QColorSpace();
}

QColorSpace QVImageCore::detectDisplayColorSpace() const
{
    QV_PROFILE_SCOPE("open: display color space");

    QWindow *window = static_cast<QWidget *>(parent())->window()->windowHandle();
    if (!window)
        return {};

    // The profile only changes when the window moves to another screen, so it is read and
    // parsed once per screen instead of on every image load
    const QScreen *screen = window->screen();
    if (displayColorSpaceCached && displayColorSpaceScreen.data() == screen)
        return cachedDisplayColorSpace;

    const QByteArray profileData = QVPlatformFunctions::getIccProfileForWindow(window);
    cachedDisplayColorSpace = profileData.isEmpty() ? QColorSpace()
                                                    : QColorSpace::fromIccProfile(profileData);
    displayColorSpaceScreen = window->screen();
    displayColorSpaceCached = true;

    return cachedDisplayColorSpace;
}

void QVImageCore::jumpToNextFrame()
{
    if (currentFileDetails.isMovieLoaded)
        loadedMovie.jumpToNextFrame();
}

void QVImageCore::setPaused(bool desiredState)
{
    if (currentFileDetails.isMovieLoaded)
        loadedMovie.setPaused(desiredState);
}

void QVImageCore::setSpeed(int desiredSpeed)
{
    if (desiredSpeed < 0)
        desiredSpeed = 0;

    if (desiredSpeed > 1000)
        desiredSpeed = 1000;

    if (currentFileDetails.isMovieLoaded)
        loadedMovie.setSpeed(desiredSpeed);
}

void QVImageCore::rotateImage(int rotation)
{
    currentRotation += rotation;

    // normalize between 360 and 0
    currentRotation = (currentRotation % 360 + 360) % 360;
    QTransform transform;

    if (currentFileDetails.isMovieLoaded) {
        transform.rotate(currentRotation);
        loadedPixmap.convertFromImage(loadedMovie.currentImage().transformed(transform));
    } else {
        transform.rotate(rotation);
        loadedPixmap = loadedPixmap.transformed(transform);
    }

    currentFileDetails.loadedPixmapSize = QSize(loadedPixmap.width(), loadedPixmap.height());
    if (rotation % 180 != 0)
        currentFileDetails.fullPixmapSize.transpose();
    emit updateLoadedPixmapItem();
}

QImage QVImageCore::matchCurrentRotation(const QImage &imageToRotate)
{
    if (!currentRotation)
        return imageToRotate;

    QTransform transform;
    transform.rotate(currentRotation);
    return imageToRotate.transformed(transform);
}

// TODO: Remove this function---extremely inefficient
QPixmap QVImageCore::matchCurrentRotation(const QPixmap &pixmapToRotate)
{
    if (!currentRotation)
        return pixmapToRotate;

    QTransform transform;
    transform.rotate(currentRotation);
    return pixmapToRotate.transformed(transform);
}

QPixmap QVImageCore::scaleExpensively(const int desiredWidth, const int desiredHeight)
{
    return scaleExpensively(QSizeF(desiredWidth, desiredHeight));
}

QPixmap QVImageCore::scaleExpensively(const QSizeF desiredSize)
{
    if (!currentFileDetails.isPixmapLoaded)
        return QPixmap();

    QSize size = QSize(loadedPixmap.width(), loadedPixmap.height());
    size.scale(desiredSize.toSize(), Qt::KeepAspectRatio);

    // Get the current frame of the animation if this is an animation
    QPixmap relevantPixmap;
    if (!currentFileDetails.isMovieLoaded) {
        relevantPixmap = loadedPixmap;
    } else {
        relevantPixmap = loadedMovie.currentPixmap();
        relevantPixmap = matchCurrentRotation(relevantPixmap);
    }

    // If we are really close to the original size, just return the original
    if (abs(desiredSize.width() - relevantPixmap.width()) < 1
        && abs(desiredSize.height() - relevantPixmap.height()) < 1) {
        return relevantPixmap;
    }

    return relevantPixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void QVImageCore::settingsUpdated()
{
    auto &settingsManager = qvApp->getSettingsManager();

    // A settings change may follow a display profile change, so the cached profile is dropped
    displayColorSpaceCached = false;

    // preloading mode
    // Cost is in KiB
    switch (qvGetSettingInt(SettingsManager::Setting::PreloadingMode)) {
    case SettingsManager::PreloadingDisabled: {
        QVImageCore::imageCache.setMaxCost(0);
        break;
    }
    case SettingsManager::PreloadingAdjacent: {
        QVImageCore::imageCache.setMaxCost(256000);
        break;
    }
    case SettingsManager::PreloadingExtended: {
        QVImageCore::imageCache.setMaxCost(2048000);
        break;
    }
    default:
        Q_ASSERT(false);
    }

    // update folder info to reflect new settings (e.g. sort order), but only when a setting that
    // shapes the listing changed; anything else leaves the current listing untouched
    if (currentFolderScanSettings() != lastFolderScanSettings)
        updateFolderInfo();

    bool changedImagePreprocessing = false;

    // colorspaceconversion
    if (colorSpaceConversion != qvGetSettingInt(SettingsManager::Setting::ColorSpaceConversion)) {
        colorSpaceConversion = qvGetSettingInt(SettingsManager::Setting::ColorSpaceConversion);
        changedImagePreprocessing = true;
    }

    if (changedImagePreprocessing && currentFileDetails.isPixmapLoaded)
        loadFile(currentFileDetails.fileInfo.absoluteFilePath());
}

void QVImageCore::FileDetails::updateLoadedIndexInFolder()
{
    loadedIndexInFolder = FolderScanner::findLoadedIndexInFolder(fileInfo, folderFileInfoList);
}
