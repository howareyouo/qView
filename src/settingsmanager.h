#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QVariant>
#include <QVector>
#include <QDir>

class SettingsManager : public QObject
{
    Q_OBJECT
public:
    enum class Setting {
        // Window settings
        BgColorEnabled = 0,
        BgColor,
        BgColorAlpha,
        TitleBarMode,
        WindowResizeMode,
        MinWindowResizedPercentage,
        MaxWindowResizedPercentage,
        HideTitlebar,
        Frameless,
        ForceDarkMode,
        QuitOnLastWindow,
        MenuBarEnabled,
        FullScreenDetails,
        CtrlDragWindow,
        // Image settings
        FilteringEnabled,
        ScalingEnabled,
        ScalingTwoEnabled,
        ScaleFactor,
        ScrollZoom,
        FractionalZoom,
        CursorZoom,
        CropMode,
        PastActualSizeEnabled,
        ColorSpaceConversion,
        // Miscellaneous settings
        Language,
        SortMode,
        SortDescending,
        PreloadingMode,
        LoopFoldersEnabled,
        SlideshowReversed,
        SlideshowTimer,
        AfterDelete,
        AskDelete,
        AllowMimeContentDetection,
        SaveRecents,
        UpdateNotifications,
        SkipHidden
    };
    Q_ENUM(Setting)

    struct SettingData
    {
        QVariant defaultValue;
        QVariant value;
    };

    explicit SettingsManager(QObject *parent = nullptr);

    // Returns the "translations" folder next to the executable that
    // language files (qview_*.qm) are loaded from.
    static QDir translationsDir();

    QString getSystemLanguage() const;

    bool loadTranslation() const;

    void loadSettings();

    void setSetting(const QString &key, const QVariant &value);

    bool getBool(Setting setting, bool defaults = false) const;
    int getInt(Setting setting, bool defaults = false) const;
    double getDouble(Setting setting, bool defaults = false) const;
    const QString getString(Setting setting, bool defaults = false) const;
    const QVariant getSetting(Setting setting, bool defaults = false) const;
    bool isDefault(Setting setting) const;

    // Direct enum-based access to setting data
    const SettingData &getSettingData(Setting setting) const;

    const QString &getSettingKey(Setting setting);

signals:
    void settingsUpdated();

private:
    void initializeSettingDataCache();
    QVector<SettingData> settingDataCache;
};

#endif // SETTINGSMANAGER_H
