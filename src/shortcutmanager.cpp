#include "shortcutmanager.h"
#include "qvapplication.h"
#include "actionmanager.h"

#include <QSettings>

ShortcutManager::ShortcutManager(QObject *parent) : QObject(parent)
{
    initializeShortcutsList();
    updateShortcuts();
}

void ShortcutManager::updateShortcuts()
{
    QSettings settings;
    settings.beginGroup("shortcuts");

    // Set all shortcuts to the user-set shortcut or the default
    for (auto &shortcut : shortcutsList) {
        shortcut.shortcuts =
                settings.value(shortcut.name, shortcut.defaultShortcuts).toStringList();
    }

    // Set all action shortcuts now that the shortcuts have changed
    const auto &actionManager = qvApp->getActionManager();
    for (const auto &shortcut : getShortcutsList()) {
        actionManager.forEachInstanceOfAction(shortcut.name, [&shortcut](QAction *action) {
            action->setShortcuts(stringListToKeySequenceList(shortcut.shortcuts));
        });
    }

    emit shortcutsUpdated();
}

void ShortcutManager::initializeShortcutsList()
{
    // The order of the definitions defines the order of the shortcuts table in the
    // options dialog; actions without a readable name have no shortcut list entry
    for (const auto &definition : ActionManager::definitions()) {
        if (!definition.readableName)
            continue;

        QStringList defaultShortcuts;
        if (definition.defaultShortcuts)
            defaultShortcuts = definition.defaultShortcuts();

        shortcutsList.append({ definition.readableName(), definition.key, defaultShortcuts, {} });
    }
}
