#ifndef QVINFODIALOG_H
#define QVINFODIALOG_H

#include <QDialog>
#include <QFileInfo>
#include <QLocale>

namespace Ui {
class QVInfoDialog;
}

class QVInfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QVInfoDialog(QWidget *parent = nullptr);
    ~QVInfoDialog() override;

    void setInfo(const QFileInfo &fileInfo, int imageWidth, int imageHeight, int frames);

    void updateInfo();

    static QString formatBytes(qint64 bytes) { return QLocale().formattedDataSize(bytes); }

private:
    Ui::QVInfoDialog *ui;

    QFileInfo selectedFileInfo;
    int width = 0;
    int height = 0;
    int frameCount = 0;
};

#endif // QVINFODIALOG_H
