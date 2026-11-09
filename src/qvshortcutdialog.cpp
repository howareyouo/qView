#include "qvshortcutdialog.h"
#include "ui_qvshortcutdialog.h"
#include "qvapplication.h"

#include <QMessageBox>

QVShortcutDialog::QVShortcutDialog(int index,
                                   GetTransientShortcutCallback getTransientShortcutCallback,
                                   QWidget *parent)
    : QDialog(parent), ui(new Ui::QVShortcutDialog)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint | Qt::CustomizeWindowHint));

    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, &QVShortcutDialog::buttonBoxClicked);

    shortcutObject = qvApp->getShortcutManager().getShortcutsList().value(index);
    this->index = index;
    this->getTransientShortcutCallback = getTransientShortcutCallback;
    ui->keySequenceEdit->setKeySequence(getTransientShortcutCallback(index).join(", "));
    ui->keySequenceEdit->setClearButtonEnabled(true);
}

QVShortcutDialog::~QVShortcutDialog()
{
    delete ui;
}

void QVShortcutDialog::done(int r)
{
    // The dialog is closed by acceptValidated() once the entered shortcuts have been validated, so
    // ignore the accepted result coming from the button box.
    if (r != QDialog::Accepted)
        QDialog::done(r);
}

void QVShortcutDialog::buttonBoxClicked(QAbstractButton *button)
{
    const auto role = ui->buttonBox->buttonRole(button);

    if (role == QDialogButtonBox::ResetRole) {
        ui->keySequenceEdit->setKeySequence(shortcutObject.defaultShortcuts.join(", "));
        return;
    }

    if (role != QDialogButtonBox::AcceptRole)
        return;

    const QStringList shortcutsStringList =
            ui->keySequenceEdit->keySequence().toString().split(", ");
    const auto sequenceList = ShortcutManager::stringListToKeySequenceList(shortcutsStringList);

    for (const auto &sequence : sequenceList) {
        const auto conflictingShortcut = shortcutAlreadyBound(sequence, shortcutObject.name);
        if (!conflictingShortcut.isEmpty()) {
            const QString nativeShortcutString = sequence.toString(QKeySequence::NativeText);
            QMessageBox::warning(this, tr("Shortcut Already Used"),
                                 tr("\"%1\" is already bound to \"%2\"")
                                         .arg(nativeShortcutString, conflictingShortcut));
            return;
        }
    }

    acceptValidated();

    emit shortcutsListChanged(index, shortcutsStringList);
}

QString QVShortcutDialog::shortcutAlreadyBound(const QKeySequence &chosenSequence,
                                               const QString &exemptShortcut)
{
    if (chosenSequence.isEmpty())
        return "";

    const auto &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    for (int i = 0; i < shortcutsList.length(); i++) {
        const auto &shortcut = shortcutsList.value(i);
        const auto sequenceList =
                ShortcutManager::stringListToKeySequenceList(getTransientShortcutCallback(i));

        if (sequenceList.contains(chosenSequence) && shortcut.name != exemptShortcut)
            return shortcut.readableName;
    }
    return "";
}

void QVShortcutDialog::acceptValidated()
{
    QDialog::done(1);
}
