#include "qvwin32functions.h"

#include "ShlObj_core.h"
#include "winuser.h"
#include "wingdi.h"
#include "Objbase.h"
#include "appmodel.h"
#include "Shlwapi.h"

#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QCollator>
#include <QVersionNumber>
#include <QXmlStreamReader>
#include <QWindow>

#include <QDebug>

// Owns the shell association handler behind an open-with entry: the shell hands back a
// reference, and this is where it is released again, so an entry only has to carry the handler
// around as a value.
class AssocHandler
{
public:
    explicit AssocHandler(IAssocHandler *handler) : handler(handler) {}
    ~AssocHandler() { handler->Release(); }

    // What the shell calls this handler and the executable behind it. Empty when the shell
    // declines to say. Each string is allocated by the shell and freed here.
    QString uiName() const;
    QString name() const;
    QString iconLocation() const;

    // Opens the file through this handler, given the data object built from the shell item
    void invoke(IDataObject *dataObject) const { handler->Invoke(dataObject); }

private:
    IAssocHandler *handler;
};

QString AssocHandler::uiName() const
{
    LPWSTR value = nullptr;
    if (FAILED(handler->GetUIName(&value)))
        return QString();

    const QString result = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return result;
}

QString AssocHandler::name() const
{
    LPWSTR value = nullptr;
    if (FAILED(handler->GetName(&value)))
        return QString();

    const QString result = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return result;
}

QString AssocHandler::iconLocation() const
{
    LPWSTR value = nullptr;
    int index = 0;
    if (FAILED(handler->GetIconLocation(&value, &index)))
        return QString();

    const QString result = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return result;
}

QList<OpenWith::OpenWithItem> QVWin32Functions::getOpenWithItems(const QString &filePath)
{
    QList<OpenWith::OpenWithItem> listOfOpenWithItems;

    QFileInfo info(filePath);
    QString extension = "." + info.suffix();

    // Get default program first
    WCHAR assocString[MAX_PATH];
    DWORD assocStringSize = MAX_PATH;
    AssocQueryStringW(0, ASSOCSTR_FRIENDLYAPPNAME, qUtf16Printable(extension), L"open", assocString,
                      &assocStringSize);

    QString defaultProgramName = QString::fromWCharArray(assocString);

    // Get the big list of recommended assochandlers for this file type
    IEnumAssocHandlers *assocHandlers = 0;
    if (!SUCCEEDED(SHAssocEnumHandlers(qUtf16Printable(extension), ASSOC_FILTER_RECOMMENDED,
                                       &assocHandlers))) {
        qDebug() << "Failed to retrieve enum handlers";
        return listOfOpenWithItems;
    }

    ULONG retrieved = 0;
    IAssocHandler *retrievedHandler = 0;
    while (assocHandlers->Next(1, &retrievedHandler, &retrieved) == S_OK) {
        // The reference the shell handed back belongs to this entry from here on
        const QSharedPointer<AssocHandler> assocHandler(new AssocHandler(retrievedHandler));

        OpenWith::OpenWithItem openWithItem;
        openWithItem.name = assocHandler->uiName();
        openWithItem.exec = assocHandler->name();
        const QString iconLocation = assocHandler->iconLocation();

        // An entry the shell can neither name nor point at can be shown or invoked; skip it
        if (openWithItem.name.isEmpty() || openWithItem.exec.isEmpty())
            continue;

        openWithItem.isDefault = openWithItem.name == defaultProgramName;
        openWithItem.winAssocHandler = assocHandler;

        bool isAppx = iconLocation.contains("ms-resource");

        // Don't include qView in open with menu
        if (openWithItem.name == "qView")
            continue;

        // Validity check
        if (!QFile(openWithItem.exec).exists() && !isAppx) {
            qDebug() << openWithItem.name << "is an invalid openwith entry";
            continue;
        }

        // Replace ampersands with escaped ampersands for menu items
        openWithItem.name.replace("&", "&&");

        // Set an icon
        if (isAppx) {
            WCHAR realIconPath[MAX_PATH];
            SHLoadIndirectString(reinterpret_cast<LPCWSTR>(iconLocation.utf16()), realIconPath,
                                 MAX_PATH, NULL);
            openWithItem.icon = QIcon(QString::fromWCharArray(realIconPath));
        } else {
            QFileIconProvider iconProvider;
            openWithItem.icon = iconProvider.icon(QFileInfo(iconLocation));
        }

        listOfOpenWithItems.append(openWithItem);
    }

    assocHandlers->Release();

    return listOfOpenWithItems;
}

void QVWin32Functions::openWithInvokeAssocHandler(const QString &filePath,
                                                  AssocHandler *winAssocHandler)
{
    const QString &nativeFilePath = QDir::toNativeSeparators(filePath);

    // Create IShellItem
    IShellItem *shellItem = 0;
    HRESULT result = SHCreateItemFromParsingName(qUtf16Printable(nativeFilePath), NULL,
                                                 IID_IShellItem, (void **)&shellItem);
    if (!SUCCEEDED(result)) {
        qDebug() << "Failed to create IShellItem for " << filePath;
        return;
    }

    // Get IDataObject from that
    IDataObject *dataObject = 0;
    if (!SUCCEEDED(shellItem->BindToHandler(NULL, BHID_DataObject, IID_IDataObject,
                                            (void **)&dataObject))) {
        qDebug() << "Failed to get IDataObject from IShellItem";
        shellItem->Release();
        return;
    }

    winAssocHandler->invoke(dataObject);

    dataObject->Release();
    shellItem->Release();
}

void QVWin32Functions::showOpenWithDialog(const QString &filePath, const QWindow *parent)
{
    const QString &nativeFilePath = QDir::toNativeSeparators(filePath);
    const OPENASINFO info = { qUtf16Printable(nativeFilePath), 0, OAIF_EXEC };

    HWND winId = reinterpret_cast<HWND>(parent->winId());
    if (!SUCCEEDED(SHOpenWithDialog(winId, &info)))
        qDebug() << "Failed launching open with dialog";
}

QByteArray QVWin32Functions::getIccProfileForWindow(const QWindow *window)
{
    QByteArray result;
    const HWND hWnd = reinterpret_cast<HWND>(window->winId());
    const HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor) {
        MONITORINFOEXW monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFOEXW);
        if (GetMonitorInfoW(hMonitor, &monitorInfo)) {
            const HDC hDC = CreateICW(monitorInfo.szDevice, monitorInfo.szDevice, NULL, NULL);
            if (hDC) {
                WCHAR profilePathBuff[MAX_PATH];
                DWORD profilePathSize = MAX_PATH;
                if (GetICMProfileW(hDC, &profilePathSize, profilePathBuff)) {
                    QString profilePath = QString::fromWCharArray(profilePathBuff);
                    QFile file(profilePath);
                    if (file.open(QIODevice::ReadOnly)) {
                        result = file.readAll();
                        file.close();
                    }
                }
                DeleteDC(hDC);
            }
        }
    }
    return result;
}
