#ifndef FILEOPERATIONS_H
#define FILEOPERATIONS_H

#include <QStack>
#include <QString>

class MainWindow;

// Deletion and trash/restore logic for the current file, extracted from MainWindow.
// All feedback is reported through the passed window.
class FileOperations
{
public:
    struct DeletedPaths
    {
        QString pathInTrash;
        QString previousPath;
    };

    bool canUndo() const
    {
        return !lastDeletedFiles.isEmpty() && !lastDeletedFiles.top().pathInTrash.isEmpty();
    }

    void askDeleteFile(MainWindow &window, bool permanent);

    void deleteFile(MainWindow &window, bool permanent);

    void undoDelete(MainWindow &window);

private:
    QString deleteFileLinuxFallback(const QString &path, bool putBack);

    QStack<DeletedPaths> lastDeletedFiles;
};

#endif // FILEOPERATIONS_H
