#ifndef QVOPTIONSDIALOG_H
#define QVOPTIONSDIALOG_H

#include "qvshortcutdialog.h"
#include "settingsmanager.h"

#include <QDialog>
#include <QCheckBox>
#include <QRadioButton>
#include <QComboBox>
#include <QSpinBox>

namespace Ui {
class QVOptionsDialog;
}

class QVOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QVOptionsDialog(QWidget *parent = nullptr);
    ~QVOptionsDialog() override;

protected:
    void changeEvent(QEvent *event) override;
    void done(int r) override;

    void modifySetting(SettingsManager::Setting setting, QVariant value);
    void saveSettings();
    void syncSettings(bool defaults = false, bool makeConnections = false);
    void syncCheckbox(QCheckBox *checkbox, SettingsManager::Setting setting, bool defaults = false,
                      bool makeConnection = false);
    void syncRadioButtons(QList<QRadioButton *> buttons, SettingsManager::Setting setting,
                          bool defaults = false, bool makeConnection = false);
    void syncComboBox(QComboBox *comboBox, SettingsManager::Setting setting, bool defaults = false,
                      bool makeConnection = false);
    void syncComboBoxData(QComboBox *comboBox, SettingsManager::Setting setting,
                          bool defaults = false, bool makeConnection = false);
    void syncSpinBox(QSpinBox *spinBox, SettingsManager::Setting setting, bool defaults = false,
                     bool makeConnection = false);
    void syncDoubleSpinBox(QDoubleSpinBox *doubleSpinBox, SettingsManager::Setting setting,
                           bool defaults = false, bool makeConnection = false);
    void syncShortcuts(bool defaults = false);
    void updateShortcutsTable();
    void updateButtonBox();
    void bgColorButtonClicked();
    void updateBgColorButton();
    void setBgColorControlsEnabled(bool enabled);
    void populateCategories(int selectedRow);
    void populateLanguages();

private slots:
    void shortcutCellDoubleClicked(int row, int column);

    void buttonBoxClicked(QAbstractButton *button);

    void bgColorCheckboxStateChanged(int arg1);

    void scalingCheckboxStateChanged(int arg1);

    void windowResizeComboBoxCurrentIndexChanged(int index);

    void languageComboBoxCurrentIndexChanged(int index);

    void scrollZoomsComboBoxCurrentIndexChanged(int index);

private:
    Ui::QVOptionsDialog *ui;

    QHash<int, QVariant> transientSettings;

    QList<QStringList> transientShortcuts;

    bool languageRestartMessageShown;
};

#endif // QVOPTIONSDIALOG_H
