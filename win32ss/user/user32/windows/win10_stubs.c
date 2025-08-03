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

/* FUNCTIONS ******************************************************************/

/*
 * Windows 10 DPI Awareness APIs
 */

BOOL
WINAPI
SetProcessDPIAware(VOID)
{
    WARN("SetProcessDPIAware stub\n");
    return TRUE;
}

BOOL
WINAPI
IsProcessDPIAware(VOID)
{
    WARN("IsProcessDPIAware stub\n");
    return FALSE;
}

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

BOOL
WINAPI
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT value)
{
    WARN("SetProcessDpiAwarenessContext stub\n");
    return TRUE;
}

UINT
WINAPI
GetDpiForSystem(VOID)
{
    WARN("GetDpiForSystem stub\n");
    return 96; // Standard DPI
}

UINT
WINAPI
GetDpiForWindow(HWND hwnd)
{
    WARN("GetDpiForWindow stub\n");
    return 96; // Standard DPI
}

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

BOOL
WINAPI
LogicalToPhysicalPointForPerMonitorDPI(HWND hwnd, LPPOINT lpPoint)
{
    WARN("LogicalToPhysicalPointForPerMonitorDPI stub\n");
    return LogicalToPhysicalPoint(hwnd, lpPoint);
}

BOOL
WINAPI
PhysicalToLogicalPointForPerMonitorDPI(HWND hwnd, LPPOINT lpPoint)
{
    WARN("PhysicalToLogicalPointForPerMonitorDPI stub\n");
    return PhysicalToLogicalPoint(hwnd, lpPoint);
}

/*
 * Windows 10 Touch and Pen Input APIs
 */

BOOL
WINAPI
RegisterTouchWindow(HWND hwnd, ULONG ulFlags)
{
    WARN("RegisterTouchWindow stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
UnregisterTouchWindow(HWND hwnd)
{
    WARN("UnregisterTouchWindow stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
IsTouchWindow(HWND hwnd, PULONG pulFlags)
{
    WARN("IsTouchWindow stub\n");
    if (pulFlags)
        *pulFlags = 0;
    return FALSE;
}

BOOL
WINAPI
GetTouchInputInfo(HTOUCHINPUT hTouchInput,
                  UINT cInputs,
                  PTOUCHINPUT pInputs,
                  int cbSize)
{
    WARN("GetTouchInputInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
CloseTouchInputHandle(HTOUCHINPUT hTouchInput)
{
    WARN("CloseTouchInputHandle stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetGestureInfo(HGESTUREINFO hGestureInfo,
               PGESTUREINFO pGestureInfo)
{
    WARN("GetGestureInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
CloseGestureInfoHandle(HGESTUREINFO hGestureInfo)
{
    WARN("CloseGestureInfoHandle stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SetGestureConfig(HWND hwnd,
                 DWORD dwReserved,
                 UINT cIDs,
                 PGESTURECONFIG pGestureConfig,
                 UINT cbSize)
{
    WARN("SetGestureConfig stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetGestureConfig(HWND hwnd,
                 DWORD dwReserved,
                 DWORD dwFlags,
                 PUINT pcIDs,
                 PGESTURECONFIG pGestureConfig,
                 UINT cbSize)
{
    WARN("GetGestureConfig stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * Windows 10 Pointer APIs
 */

BOOL
WINAPI
GetPointerInfo(UINT32 pointerId,
               POINTER_INFO *pointerInfo)
{
    WARN("GetPointerInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetPointerType(UINT32 pointerId,
               POINTER_INPUT_TYPE *pointerType)
{
    WARN("GetPointerType stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetPointerTouchInfo(UINT32 pointerId,
                    POINTER_TOUCH_INFO *touchInfo)
{
    WARN("GetPointerTouchInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetPointerDevices(UINT32 *deviceCount,
                  POINTER_DEVICE_INFO *pointerDevices)
{
    WARN("GetPointerDevices stub\n");
    if (deviceCount)
        *deviceCount = 0;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
RegisterPointerDeviceNotifications(HWND window,
                                   BOOL notifyRange)
{
    WARN("RegisterPointerDeviceNotifications stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

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

BOOL
WINAPI
IsWindowRedirectedForPrint(HWND hWnd)
{
    WARN("IsWindowRedirectedForPrint stub\n");
    return FALSE;
}

BOOL
WINAPI
ChangeWindowMessageFilter(UINT message, DWORD dwFlag)
{
    WARN("ChangeWindowMessageFilter stub\n");
    return TRUE;
}

BOOL
WINAPI
ChangeWindowMessageFilterEx(HWND hwnd,
                            UINT message,
                            DWORD action,
                            PCHANGEFILTERSTRUCT pChangeFilterStruct)
{
    WARN("ChangeWindowMessageFilterEx stub\n");
    return TRUE;
}

/*
 * Windows 10 Shutdown APIs
 */

BOOL
WINAPI
ShutdownBlockReasonCreate(HWND hWnd,
                          LPCWSTR pwszReason)
{
    WARN("ShutdownBlockReasonCreate stub\n");
    return TRUE;
}

BOOL
WINAPI
ShutdownBlockReasonQuery(HWND hWnd,
                         LPWSTR pwszBuff,
                         DWORD *pcchBuff)
{
    WARN("ShutdownBlockReasonQuery stub\n");
    if (pcchBuff)
        *pcchBuff = 0;
    return FALSE;
}

BOOL
WINAPI
ShutdownBlockReasonDestroy(HWND hWnd)
{
    WARN("ShutdownBlockReasonDestroy stub\n");
    return TRUE;
}

/*
 * Windows 10 Display and Monitor APIs
 */

LONG
WINAPI
GetDisplayConfigBufferSizes(UINT32 flags,
                            UINT32 *numPathArrayElements,
                            UINT32 *numModeInfoArrayElements)
{
    WARN("GetDisplayConfigBufferSizes stub\n");
    if (numPathArrayElements)
        *numPathArrayElements = 0;
    if (numModeInfoArrayElements)
        *numModeInfoArrayElements = 0;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG
WINAPI
QueryDisplayConfig(UINT32 flags,
                   UINT32 *numPathArrayElements,
                   DISPLAYCONFIG_PATH_INFO *pathArray,
                   UINT32 *numModeInfoArrayElements,
                   DISPLAYCONFIG_MODE_INFO *modeInfoArray,
                   DISPLAYCONFIG_TOPOLOGY_ID *currentTopologyId)
{
    WARN("QueryDisplayConfig stub\n");
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG
WINAPI
SetDisplayConfig(UINT32 numPathArrayElements,
                 DISPLAYCONFIG_PATH_INFO *pathArray,
                 UINT32 numModeInfoArrayElements,
                 DISPLAYCONFIG_MODE_INFO *modeInfoArray,
                 UINT32 flags)
{
    WARN("SetDisplayConfig stub\n");
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG
WINAPI
DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_HEADER *requestPacket)
{
    WARN("DisplayConfigGetDeviceInfo stub\n");
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG
WINAPI
DisplayConfigSetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_HEADER *setPacket)
{
    WARN("DisplayConfigSetDeviceInfo stub\n");
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * Windows 10 Raw Input APIs
 */

UINT
WINAPI
GetRawInputData(HRAWINPUT hRawInput,
                UINT uiCommand,
                LPVOID pData,
                PUINT pcbSize,
                UINT cbSizeHeader)
{
    WARN("GetRawInputData stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return (UINT)-1;
}

UINT
WINAPI
GetRawInputDeviceList(PRAWINPUTDEVICELIST pRawInputDeviceList,
                      PUINT puiNumDevices,
                      UINT cbSize)
{
    WARN("GetRawInputDeviceList stub\n");
    if (puiNumDevices)
        *puiNumDevices = 0;
    return 0;
}

UINT
WINAPI
GetRawInputDeviceInfoA(HANDLE hDevice,
                       UINT uiCommand,
                       LPVOID pData,
                       PUINT pcbSize)
{
    WARN("GetRawInputDeviceInfoA stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return (UINT)-1;
}

UINT
WINAPI
GetRawInputDeviceInfoW(HANDLE hDevice,
                       UINT uiCommand,
                       LPVOID pData,
                       PUINT pcbSize)
{
    WARN("GetRawInputDeviceInfoW stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return (UINT)-1;
}

BOOL
WINAPI
RegisterRawInputDevices(PCRAWINPUTDEVICE pRawInputDevices,
                        UINT uiNumDevices,
                        UINT cbSize)
{
    WARN("RegisterRawInputDevices stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

LRESULT
WINAPI
DefRawInputProc(PRAWINPUT *paRawInput,
                INT nInput,
                UINT cbSizeHeader)
{
    WARN("DefRawInputProc stub\n");
    return 0;
}