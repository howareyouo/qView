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
        RememberZoom,
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

    // Integer-valued settings, matching the numbers stored in QSettings
    enum SortMode
    {
        SortByName = 0,
        SortByLastModified,
        SortByDateCreated,
        SortBySize,
        SortByType,
        SortRandomly
    };
    enum CropMode
    {
        CropBoth = 0,
        CropHeight,
        CropWidth
    };
    enum TitleBarMode
    {
        TitleBarDefault = 0,
        TitleBarFileName,
        TitleBarIndexAndFileName,
        TitleBarDetails
    };
    enum WindowResizeMode
    {
        WindowResizeNever = 0,
        WindowResizeOnLaunch,
        WindowResizeAlways
    };
    enum ScrollZoomMode
    {
        ScrollZoomsImage = 0,
        ScrollZoomsAutoDetect,
        ScrollZoomsScroll
    };
    enum ColorSpaceConversionMode
    {
        ColorSpaceNone = 0,
        ColorSpaceAutoDetect,
        ColorSpaceSRgb,
        ColorSpaceDisplayP3
    };
    enum PreloadingMode
    {
        PreloadingDisabled = 0,
        PreloadingAdjacent,
        PreloadingExtended
    };
    enum AfterDeleteAction
    {
        AfterDeleteShowPrevious = 0,
        AfterDeleteDoNothing,
        AfterDeleteShowNext
    };

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
    QString getString(Setting setting, bool defaults = false) const;
    // Returns a reference into the cache, so reading a setting does not copy the variant
    const QVariant &getSetting(Setting setting, bool defaults = false) const;
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
