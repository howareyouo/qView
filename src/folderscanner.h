#ifndef FOLDERSCANNER_H
#define FOLDERSCANNER_H

#include <QFileInfo>
#include <QList>
#include <QString>

// Folder scanning and sorting logic, extracted from QVImageCore.
namespace FolderScanner {

struct CompatibleFile
{
    QString absoluteFilePath;
    QString fileName;

    // Only populated if needed for sorting
    qint64 lastModified;
    qint64 lastCreated;
    qint64 size;
    QString mimeType;
};

struct DirInfo
{
    QString dirPath;
    qsizetype fileCount;
    int sortMode;
    bool sortDescending;

    bool operator!=(const DirInfo &other) const
    {
        return dirPath != other.dirPath || fileCount != other.fileCount
                || sortMode != other.sortMode || sortDescending != other.sortDescending;
    }
};

struct FolderScanParams
{
    // The folder only needs to be scanned if the file isn't in the listing we already have
    bool scanFolder = false;
    QString dirPath;
    int sortMode = 0;
    bool sortDescending = false;
    bool skipHidden = false;
    bool allowMimeContentDetection = false;
    DirInfo lastDirInfo;
    unsigned randomSortSeed = 0;
};

struct FolderScanInfo
{
    QList<CompatibleFile> fileList;
    DirInfo dirInfo;
    unsigned randomSortSeed = 0;
};

QList<CompatibleFile> getCompatibleFilesForDir(const QString &dirPath, int sortMode, bool skipHidden,
                                               bool allowMimeContentDetection);

FolderScanInfo scanFolder(const FolderScanParams &params);

int findLoadedIndexInFolder(const QFileInfo &fileInfo, const QList<CompatibleFile> &fileList);
}

#endif // FOLDERSCANNER_H
