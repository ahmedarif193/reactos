/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS user32.dll
 * FILE:            win32ss/user/user32/windows/win10_stubs.c
 * PURPOSE:         Stub implementations for Windows 10 User32 APIs
 * PROGRAMMERS:     ReactOS Development Team
 */

/* INCLUDES *******************************************************************/

#include <user32.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(user32);

/* Missing type definitions for Windows 10 APIs */
typedef enum _PROCESS_DPI_AWARENESS {
    PROCESS_DPI_UNAWARE = 0,
    PROCESS_SYSTEM_DPI_AWARE = 1,
    PROCESS_PER_MONITOR_DPI_AWARE = 2
} PROCESS_DPI_AWARENESS;

typedef HANDLE HTOUCHINPUT;
typedef HANDLE HGESTUREINFO;

typedef struct tagTOUCHINPUT {
    LONG x;
    LONG y;
    HANDLE hSource;
    DWORD dwID;
    DWORD dwFlags;
    DWORD dwMask;
    DWORD dwTime;
    ULONG_PTR dwExtraInfo;
    DWORD cxContact;
    DWORD cyContact;
} TOUCHINPUT, *PTOUCHINPUT;

typedef struct tagGESTUREINFO {
    UINT cbSize;
    DWORD dwFlags;
    DWORD dwID;
    HWND hwndTarget;
    POINTS ptsLocation;
    DWORD dwInstanceID;
    DWORD dwSequenceID;
    ULONGLONG ullArguments;
    UINT cbExtraArgs;
} GESTUREINFO, *PGESTUREINFO;

typedef struct tagGESTURECONFIG {
    DWORD dwID;
    DWORD dwWant;
    DWORD dwBlock;
} GESTURECONFIG, *PGESTURECONFIG;

typedef struct tagPOINTER_INFO {
    UINT32 pointerId;
    UINT32 frameId;
    RECT rcContact;
    RECT rcContactRaw;
    UINT32 dwKeyStates;
    UINT64 dwTime;
    UINT32 historyCount;
    INT32 InputData;
    UINT32 dwFlags;
} POINTER_INFO;

typedef struct tagPOINTER_TOUCH_INFO {
    POINTER_INFO pointerInfo;
    UINT32 touchFlags;
    UINT32 touchMask;
    RECT rcContact;
    RECT rcContactRaw;
    UINT32 orientation;
    UINT32 pressure;
} POINTER_TOUCH_INFO;

typedef struct tagPOINTER_DEVICE_INFO {
    DWORD displayOrientation;
    HANDLE device;
    UINT32 pointerDeviceType;
    HMONITOR monitor;
    ULONG startingCursorId;
    USHORT maxActiveContacts;
    WCHAR productString[520];
} POINTER_DEVICE_INFO;

typedef struct tagWINDOWCOMPOSITIONATTRIBDATA {
    DWORD Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef struct tagCHANGEFILTERSTRUCT {
    DWORD cbSize;
    DWORD ExtStatus;
} CHANGEFILTERSTRUCT, *PCHANGEFILTERSTRUCT;

/* Additional Windows 10 types */
/* Note: DPI types are already defined in headers */

/* Forward declarations for shell types */
typedef struct _ITEMIDLIST *PCIDLIST_ABSOLUTE;
typedef struct _ITEMIDLIST *PIDLIST_ABSOLUTE; 
typedef const struct _ITEMIDLIST *PCUITEMID_CHILD;
typedef const GUID *REFKNOWNFOLDERID;
typedef struct IBindCtx IBindCtx;
typedef struct IShellFolder IShellFolder;
typedef struct IShellItem IShellItem;

typedef struct tagPOINTER_DEVICE_CURSOR_INFO {
    UINT32 cursorId;
    UINT32 cursor;
} POINTER_DEVICE_CURSOR_INFO;

typedef struct tagPOINTER_DEVICE_PROPERTY {
    INT32 logicalMin;
    INT32 logicalMax;
    UINT32 physicalMin;
    UINT32 physicalMax;
    UINT32 unit;
    UINT32 unitExponent;
    USHORT usagePageId;
    USHORT usageId;
} POINTER_DEVICE_PROPERTY;

typedef struct tagPOINTER_PEN_INFO {
    POINTER_INFO pointerInfo;
    UINT32 penFlags;
    UINT32 penMask;
    UINT32 pressure;
    UINT32 rotation;
    INT32 tiltX;
    INT32 tiltY;
} POINTER_PEN_INFO;

/* Forward declarations and simple implementations */
// LogicalToPhysicalPoint - implemented in user32_vista
// PhysicalToLogicalPoint - implemented in user32_vista

/* FUNCTIONS ******************************************************************/

/*
 * Windows 10 DPI Awareness APIs
 */

// SetProcessDPIAware - implemented in user32_vista
// IsProcessDPIAware - implemented in user32_vista

HRESULT
WINAPI
SetProcessDpiAwareness(PROCESS_DPI_AWARENESS value)
{
    WARN("SetProcessDpiAwareness stub\n");
    return S_OK;
}

HRESULT
WINAPI
GetProcessDpiAwareness(HANDLE hprocess,
                       PROCESS_DPI_AWARENESS *value)
{
    WARN("GetProcessDpiAwareness stub\n");
    if (value)
        *value = PROCESS_DPI_UNAWARE;
    return S_OK;
}

// SetProcessDpiAwarenessContext - implemented in user32_vista

// GetDpiForSystem - implemented in user32_vista

// GetDpiForWindow - implemented in user32_vista

BOOL
WINAPI
EnableNonClientDpiScaling(HWND hwnd)
{
    WARN("EnableNonClientDpiScaling stub\n");
    return TRUE;
}

int
WINAPI
GetSystemMetricsForDpi(int nIndex, UINT dpi)
{
    WARN("GetSystemMetricsForDpi stub\n");
    return GetSystemMetrics(nIndex);
}

BOOL
WINAPI
AdjustWindowRectExForDpi(LPRECT lpRect,
                         DWORD dwStyle,
                         BOOL bMenu,
                         DWORD dwExStyle,
                         UINT dpi)
{
    WARN("AdjustWindowRectExForDpi stub\n");
    return AdjustWindowRectEx(lpRect, dwStyle, bMenu, dwExStyle);
}

// LogicalToPhysicalPointForPerMonitorDPI - implemented in user32_stubs.c

BOOL
WINAPI
PhysicalToLogicalPointForPerMonitorDPI(HWND hwnd, LPPOINT lpPoint)
{
    WARN("PhysicalToLogicalPointForPerMonitorDPI stub\n");
    /* For now, just return as-is since we don't have DPI scaling */
    return TRUE;
}

/*
 * Windows 10 Touch and Pen Input APIs
 */

// RegisterTouchWindow - implemented in user32_vista

// UnregisterTouchWindow - implemented in user32_vista

// IsTouchWindow - implemented in user32_vista

// GetTouchInputInfo - implemented in user32_vista

// CloseTouchInputHandle - implemented in user32_vista

// GetGestureInfo - implemented in user32_vista

// CloseGestureInfoHandle - implemented in user32_vista

// SetGestureConfig - implemented in user32_vista

// GetGestureConfig - implemented in user32_vista

/*
 * Windows 10 Pointer APIs
 */

// GetPointerInfo - implemented in user32_vista
// GetPointerType - implemented in user32_vista

// GetPointerTouchInfo - implemented in user32_vista
// GetPointerDevices - implemented in user32_vista  
// RegisterPointerDeviceNotifications - implemented in user32_vista

/*
 * Windows 10 Window Management APIs
 */

BOOL
WINAPI
GetWindowDisplayAffinity(HWND hWnd,
                         DWORD *pdwAffinity)
{
    WARN("GetWindowDisplayAffinity stub\n");
    if (pdwAffinity)
        *pdwAffinity = WDA_NONE;
    return TRUE;
}

BOOL
WINAPI
SetWindowDisplayAffinity(HWND hWnd,
                         DWORD dwAffinity)
{
    WARN("SetWindowDisplayAffinity stub\n");
    return TRUE;
}

BOOL
WINAPI
SetWindowCompositionAttribute(HWND hwnd,
                              WINDOWCOMPOSITIONATTRIBDATA *data)
{
    WARN("SetWindowCompositionAttribute stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetWindowCompositionAttribute(HWND hwnd,
                              WINDOWCOMPOSITIONATTRIBDATA *data)
{
    WARN("GetWindowCompositionAttribute stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

// IsWindowRedirectedForPrint - implemented in user32_vista

// ChangeWindowMessageFilter - implemented in user32_vista
// ChangeWindowMessageFilterEx - implemented in user32_vista

/*
 * Windows 10 Shutdown APIs
 */

// ShutdownBlockReasonCreate - implemented in user32_vista

BOOL
WINAPI 
ShutdownBlockReasonQuery(HWND hWnd, LPWSTR pwszBuff, DWORD *pcchBuff)
{
    WARN("ShutdownBlockReasonQuery stub\n");
    if (pcchBuff) *pcchBuff = 0;
    if (pwszBuff && *pcchBuff > 0) pwszBuff[0] = 0;
    return FALSE;
}

// ShutdownBlockReasonDestroy - implemented in user32_vista

/*
 * Windows 10 Display and Monitor APIs
 */

// GetDisplayConfigBufferSizes - implemented in user32_vista

// QueryDisplayConfig - implemented in user32_vista

// SetDisplayConfig - implemented in user32_vista

// DisplayConfigGetDeviceInfo - implemented in user32_vista

// DisplayConfigSetDeviceInfo - implemented in user32_vista

/*
 * Windows 10 Raw Input APIs
 */

// GetRawInputData - implemented in misc/stubs.c

// GetRawInputDeviceList - implemented in misc/stubs.c

// GetRawInputDeviceInfoA - implemented in misc/stubs.c

// GetRawInputDeviceInfoW - implemented in misc/stubs.c

// RegisterRawInputDevices - implemented in misc/stubs.c

// DefRawInputProc - implemented in misc/stubs.c

/* Additional Windows 10 API stubs */

BOOL
WINAPI
AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT dpiContextA, DPI_AWARENESS_CONTEXT dpiContextB)
{
    WARN("AreDpiAwarenessContextsEqual stub\n");
    return FALSE;
}

BOOL
WINAPI
EvaluateProximityToPolygon(UINT32 numVertices, POINT *controlPolygon, POINT *hitPoint)
{
    WARN("EvaluateProximityToPolygon stub\n");
    return FALSE;
}

DPI_AWARENESS
WINAPI
GetAwarenessFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT value)
{
    WARN("GetAwarenessFromDpiAwarenessContext stub\n");
    return DPI_AWARENESS_INVALID;
}

DWORD
WINAPI
GetClipboardSequenceNumber(VOID)
{
    WARN("GetClipboardSequenceNumber stub\n");
    return 0;
}

DPI_AWARENESS_CONTEXT
WINAPI
GetDpiAwarenessContextForProcess(HANDLE hProcess)
{
    WARN("GetDpiAwarenessContextForProcess stub\n");
    return DPI_AWARENESS_CONTEXT_UNAWARE;
}

HRESULT
WINAPI
GetDpiForMonitor(HMONITOR hmonitor, UINT dpiType, UINT *dpiX, UINT *dpiY)
{
    WARN("GetDpiForMonitor stub\n");
    if (dpiX) *dpiX = 96;
    if (dpiY) *dpiY = 96;
    return S_OK;
}

UINT
WINAPI
GetDpiFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT value)
{
    WARN("GetDpiFromDpiAwarenessContext stub\n");
    return 96;
}

BOOL
WINAPI
GetGUIThreadInfo(DWORD idThread, PGUITHREADINFO pgui)
{
    WARN("GetGUIThreadInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

DWORD
WINAPI
GetListBoxInfo(HWND hwnd)
{
    WARN("GetListBoxInfo stub\n");
    return 0;
}

BOOL
WINAPI
GetMenuBarInfo(HWND hwnd, LONG idObject, LONG idItem, PMENUBARINFO pmbi)
{
    WARN("GetMenuBarInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

UINT
WINAPI
GetSystemDpiForProcess(HANDLE hProcess)
{
    WARN("GetSystemDpiForProcess stub\n");
    return 96;
}

DPI_AWARENESS_CONTEXT
WINAPI
GetThreadDpiAwarenessContext(VOID)
{
    WARN("GetThreadDpiAwarenessContext stub\n");
    return DPI_AWARENESS_CONTEXT_UNAWARE;
}

BOOL
WINAPI
GetTitleBarInfo(HWND hwnd, PTITLEBARINFO pti)
{
    WARN("GetTitleBarInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

DWORD
WINAPI
GetUnpredictedMessagePos(VOID)
{
    WARN("GetUnpredictedMessagePos stub\n");
    return GetMessagePos();
}

DPI_AWARENESS_CONTEXT
WINAPI
GetWindowDpiAwarenessContext(HWND hwnd)
{
    WARN("GetWindowDpiAwarenessContext stub\n");
    return DPI_AWARENESS_CONTEXT_UNAWARE;
}

BOOL
WINAPI
IsClipboardFormatAvailable(UINT format)
{
    WARN("IsClipboardFormatAvailable stub\n");
    return FALSE;
}

DPI_AWARENESS_CONTEXT
WINAPI
SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT dpiContext)
{
    WARN("SetThreadDpiAwarenessContext stub\n");
    return DPI_AWARENESS_CONTEXT_UNAWARE;
}

BOOL
WINAPI
SkipPointerFrameMessages(UINT32 pointerId)
{
    WARN("SkipPointerFrameMessages stub\n");
    return FALSE;
}

/* Shell API stubs that were moved to user32 in Windows 10 */

HRESULT
WINAPI
SHCreateItemFromIDList(PCIDLIST_ABSOLUTE pidl, const GUID *riid, void **ppv)
{
    WARN("SHCreateItemFromIDList stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SHCreateItemFromParsingName(PCWSTR pszPath, IBindCtx *pbc, const GUID *riid, void **ppv)
{
    WARN("SHCreateItemFromParsingName stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SHCreateShellItem(PCIDLIST_ABSOLUTE pidlParent, IShellFolder *psfParent, PCUITEMID_CHILD pidl, IShellItem **ppsi)
{
    WARN("SHCreateShellItem stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SHGetFolderPathEx(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PWSTR pszPath, DWORD cchPath)
{
    WARN("SHGetFolderPathEx stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SHGetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PWSTR *ppszPath)
{
    WARN("SHGetKnownFolderPath stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SHGetSpecialFolderLocation(HWND hwndOwner, int nFolder, PIDLIST_ABSOLUTE *ppidl)
{
    WARN("SHGetSpecialFolderLocation stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
SHGetSpecialFolderPathA(HWND hwndOwner, LPSTR lpszPath, int nFolder, BOOL fCreate)
{
    WARN("SHGetSpecialFolderPathA stub\n");
    return FALSE;
}

BOOL
WINAPI
SHGetSpecialFolderPathW(HWND hwndOwner, LPWSTR lpszPath, int nFolder, BOOL fCreate)
{
    WARN("SHGetSpecialFolderPathW stub\n");
    return FALSE;
}

HRESULT
WINAPI
SHSetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PCWSTR pszPath)
{
    WARN("SHSetKnownFolderPath stub\n");
    return E_NOTIMPL;
}

/* Pointer/Touch API stubs */

BOOL
WINAPI
GetPointerCursorId(UINT32 pointerId, UINT32 *cursorId)
{
    WARN("GetPointerCursorId stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerDevice(HANDLE device, POINTER_DEVICE_INFO *pointerDevice)
{
    WARN("GetPointerDevice stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerDeviceCursors(HANDLE device, UINT32 *cursorCount, POINTER_DEVICE_CURSOR_INFO *deviceCursors)
{
    WARN("GetPointerDeviceCursors stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerDeviceProperties(HANDLE device, UINT32 *propertyCount, POINTER_DEVICE_PROPERTY *pointerProperties)
{
    WARN("GetPointerDeviceProperties stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerDeviceRects(HANDLE device, RECT *pointerDeviceRect, RECT *displayRect)
{
    WARN("GetPointerDeviceRects stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerFrameInfo(UINT32 pointerId, UINT32 *pointerCount, POINTER_INFO *pointerInfo)
{
    WARN("GetPointerFrameInfo stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerFrameInfoHistory(UINT32 pointerId, UINT32 *entriesCount, UINT32 *pointerCount, POINTER_INFO *pointerInfo)
{
    WARN("GetPointerFrameInfoHistory stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerFramePenInfo(UINT32 pointerId, UINT32 *pointerCount, POINTER_PEN_INFO *penInfo)
{
    WARN("GetPointerFramePenInfo stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerFramePenInfoHistory(UINT32 pointerId, UINT32 *entriesCount, UINT32 *pointerCount, POINTER_PEN_INFO *penInfo)
{
    WARN("GetPointerFramePenInfoHistory stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerFrameTouchInfo(UINT32 pointerId, UINT32 *pointerCount, POINTER_TOUCH_INFO *touchInfo)
{
    WARN("GetPointerFrameTouchInfo stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerFrameTouchInfoHistory(UINT32 pointerId, UINT32 *entriesCount, UINT32 *pointerCount, POINTER_TOUCH_INFO *touchInfo)
{
    WARN("GetPointerFrameTouchInfoHistory stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerPenInfo(UINT32 pointerId, POINTER_PEN_INFO *penInfo)
{
    WARN("GetPointerPenInfo stub\n");
    return FALSE;
}

BOOL
WINAPI
GetPointerPenInfoHistory(UINT32 pointerId, UINT32 *entriesCount, POINTER_PEN_INFO *penInfo)
{
    WARN("GetPointerPenInfoHistory stub\n");
    return FALSE;
}

int
WINAPI
GetPriorityClipboardFormat(UINT *paFormatPriorityList, int cFormats)
{
    WARN("GetPriorityClipboardFormat stub\n");
    return 0;
}

BOOL
WINAPI
GetRawPointerDeviceData(UINT32 pointerId, UINT32 historyCount, UINT32 propertiesCount, POINTER_DEVICE_PROPERTY *pProperties, LONG *pValues)
{
    WARN("GetRawPointerDeviceData stub\n");
    return FALSE;
}