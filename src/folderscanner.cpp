#include "folderscanner.h"
#include "qvapplication.h"

#include <QCollator>
#include <QDir>
#include <QMimeDatabase>
#include <QSet>
#include <algorithm>
#include <chrono>
#include <random>

QList<FolderScanner::CompatibleFile>
FolderScanner::getCompatibleFilesForDir(const QString &dirPath, int sortMode, bool skipHidden,
                                        bool allowMimeContentDetection)
{
    QList<CompatibleFile> fileList;

    const auto &extensions = qvApp->getFileExtensionList();
    const auto &mimeTypes = qvApp->getMimeTypeNameList();

    // Matching used to compare every directory entry against every known extension with a
    // case-insensitive endsWith(). Sets of lower case extensions and mime type names turn both
    // of those into a single lookup per file name.
    QSet<QString> extensionSet;
    extensionSet.reserve(extensions.size());
    for (const QString &extension : extensions)
        extensionSet.insert(extension.toLower());

    QSet<QString> mimeTypeSet;
    mimeTypeSet.reserve(mimeTypes.size());
    for (const QString &mimeType : mimeTypes)
        mimeTypeSet.insert(mimeType);

    QMimeDatabase mimeDb;
    const QMimeDatabase::MatchMode mimeMatchMode =
            allowMimeContentDetection ? QMimeDatabase::MatchDefault : QMimeDatabase::MatchExtension;

    // skip hidden files if user wants to
    QDir::Filters filters = QDir::Files;
    if (!skipHidden)
        filters |= QDir::Hidden;

    // Only the names are listed; a file is inspected further once it is known to be compatible
    const QDir dir(dirPath);
    const QStringList fileNames = dir.entryList(filters, QDir::Unsorted);
    for (const QString &fileName : fileNames) {
        // This is the suffix the endsWith() comparisons used to match against
        const qsizetype dotIndex = fileName.lastIndexOf(u'.');
        bool matched = dotIndex >= 0 && extensionSet.contains(fileName.mid(dotIndex).toLower());

        const QString absoluteFilePath = dir.filePath(fileName);
        QString mimeType;
        if (!matched || sortMode == SettingsManager::SortByType) {
            mimeType = mimeDb.mimeTypeForFile(absoluteFilePath, mimeMatchMode).name();
            matched |= mimeTypeSet.contains(mimeType);
        }

        // ignore macOS ._ metadata files
        if (fileName.startsWith("._")) {
            matched = false;
        }

        if (!matched)
            continue;

        const QFileInfo fileInfo(absoluteFilePath);
        fileList.append({ absoluteFilePath, fileName,
                          sortMode == SettingsManager::SortByLastModified
                                  ? fileInfo.lastModified().toMSecsSinceEpoch()
                                  : 0,
                          sortMode == SettingsManager::SortByDateCreated
                                  ? fileInfo.birthTime().toMSecsSinceEpoch()
                                  : 0,
                          sortMode == SettingsManager::SortBySize ? fileInfo.size() : 0,
                          sortMode == SettingsManager::SortByType ? mimeType : QString() });
    }

    return fileList;
}

FolderScanner::FolderScanInfo FolderScanner::scanFolder(const FolderScanParams &params)
{
    FolderScanInfo scanInfo;

    scanInfo.fileList = getCompatibleFilesForDir(params.dirPath, params.sortMode, params.skipHidden,
                                                 params.allowMimeContentDetection);

    scanInfo.dirInfo = { params.dirPath, scanInfo.fileList.count(), params.sortMode,
                         params.sortDescending };
    // If the current folder changed since the last image, assign a new seed for random sorting
    if (params.lastDirInfo != scanInfo.dirInfo)
        scanInfo.randomSortSeed = std::chrono::system_clock::now().time_since_epoch().count();
    else
        scanInfo.randomSortSeed = params.randomSortSeed;

    auto &fileList = scanInfo.fileList;

    // Sorting. The comparison direction of each mode mirrors the original
    // per-mode implementations exactly.
    switch (params.sortMode) {
    case SettingsManager::SortByName: {
        // Natural sorting
        QCollator collator;
        collator.setNumericMode(true);
        std::sort(fileList.begin(), fileList.end(),
                  [&](const CompatibleFile &file1, const CompatibleFile &file2) {
                      const int order = collator.compare(file1.fileName, file2.fileName);
                      return params.sortDescending ? order > 0 : order < 0;
                  });
        break;
    }
    case SettingsManager::SortByType: {
        // Type
        QCollator collator;
        std::sort(fileList.begin(), fileList.end(),
                  [&](const CompatibleFile &file1, const CompatibleFile &file2) {
                      const int order = collator.compare(file1.mimeType, file2.mimeType);
                      return params.sortDescending ? order > 0 : order < 0;
                  });
        break;
    }
    case SettingsManager::SortByLastModified:
    case SettingsManager::SortByDateCreated:
    case SettingsManager::SortBySize: {
        // Date modified / date created / size, all numeric keys
        const auto sortKey = [](int mode, const CompatibleFile &file) -> qint64 {
            switch (mode) {
            case SettingsManager::SortByLastModified:
                return file.lastModified;
            case SettingsManager::SortByDateCreated:
                return file.lastCreated;
            default:
                return file.size;
            }
        };
        std::sort(fileList.begin(), fileList.end(),
                  [&](const CompatibleFile &file1, const CompatibleFile &file2) {
                      return params.sortDescending ? sortKey(params.sortMode, file1)
                                                             < sortKey(params.sortMode, file2)
                                                   : sortKey(params.sortMode, file1)
                                                             > sortKey(params.sortMode, file2);
                  });
        break;
    }
    case SettingsManager::SortRandomly:
        // Random
        std::shuffle(fileList.begin(), fileList.end(),
                     std::default_random_engine(scanInfo.randomSortSeed));
        break;
    default:
        Q_ASSERT(false);
        break;
    }

    return scanInfo;
}

int FolderScanner::findLoadedIndexInFolder(const QFileInfo &fileInfo,
                                           const QList<CompatibleFile> &fileList)
{
    const QString targetPath = fileInfo.absoluteFilePath();

#ifdef Q_OS_MACOS
    // macOS may hand out decomposed names, so the case-insensitive comparison happens on the
    // normalized paths there
    const QString normalizedTargetPath = targetPath.normalized(QString::NormalizationForm_D);
#endif

    for (int i = 0; i < fileList.length(); i++) {
        const QString &candidatePath = fileList.at(i).absoluteFilePath;

        // An exact match names the same file on every file system, so it can be returned
        // without normalizing or stat'ing anything
        if (candidatePath == targetPath)
            return i;

        // Compare absoluteFilePath first because it's way faster, but double-check with
        // QFileInfo::operator== because it respects file system case sensitivity rules
#ifdef Q_OS_MACOS
        const QString normalizedCandidatePath =
                candidatePath.normalized(QString::NormalizationForm_D);
        if (normalizedCandidatePath.compare(normalizedTargetPath, Qt::CaseInsensitive) != 0)
            continue;
#else
        if (candidatePath.compare(targetPath, Qt::CaseInsensitive) != 0)
            continue;
#endif

        if (QFileInfo(candidatePath) == fileInfo)
            return i;
    }
    return -1;
}
