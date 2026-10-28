#include "folderscanner.h"
#include "qvapplication.h"

#include <QCollator>
#include <QDir>
#include <QMimeDatabase>
#include <algorithm>
#include <chrono>
#include <random>

QList<FolderScanner::CompatibleFile>
FolderScanner::getCompatibleFilesForDir(const QString &dirPath, int sortMode, bool skipHidden,
                                        bool allowMimeContentDetection)
{
    QList<CompatibleFile> fileList;

    QMimeDatabase mimeDb;
    const auto &extensions = qvApp->getFileExtensionList();
    const auto &mimeTypes = qvApp->getMimeTypeNameList();

    QMimeDatabase::MatchMode mimeMatchMode =
            allowMimeContentDetection ? QMimeDatabase::MatchDefault : QMimeDatabase::MatchExtension;

    // skip hidden files if user wants to
    QDir::Filters filters = QDir::Files;
    if (!skipHidden)
        filters |= QDir::Hidden;

    const QFileInfoList currentFolder = QDir(dirPath).entryInfoList(filters, QDir::Unsorted);
    for (const QFileInfo &fileInfo : currentFolder) {
        bool matched = false;
        const QString absoluteFilePath = fileInfo.absoluteFilePath();
        const QString fileName = fileInfo.fileName();
        for (const QString &extension : extensions) {
            if (fileName.endsWith(extension, Qt::CaseInsensitive)) {
                matched = true;
                break;
            }
        }
        QString mimeType;
        if (!matched || sortMode == SettingsManager::SortByType) {
            mimeType = mimeDb.mimeTypeForFile(absoluteFilePath, mimeMatchMode).name();
            matched |= mimeTypes.contains(mimeType);
        }

        // ignore macOS ._ metadata files
        if (fileName.startsWith("._")) {
            matched = false;
        }

        if (matched) {
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
    const QString targetPath = fileInfo.absoluteFilePath().normalized(QString::NormalizationForm_D);
    for (int i = 0; i < fileList.length(); i++) {
        // Compare absoluteFilePath first because it's way faster, but double-check with
        // QFileInfo::operator== because it respects file system case sensitivity rules
        QString candidatePath =
                fileList[i].absoluteFilePath.normalized(QString::NormalizationForm_D);
        if (candidatePath.compare(targetPath, Qt::CaseInsensitive) == 0
            && QFileInfo(fileList[i].absoluteFilePath) == fileInfo) {
            return i;
        }
    }
    return -1;
}
