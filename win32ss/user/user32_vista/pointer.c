/*
 * PROJECT:     ReactOS User32 - Vista+ APIs
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Pointer device, auto-rotation and window arrangement queries
 * COPYRIGHT:   Adapted from corresponding Wine user32 implementations
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>

#if (WINVER < 0x0602)
typedef struct tagPOINTER_DEVICE_INFO POINTER_DEVICE_INFO;
typedef struct tagPOINTER_PEN_INFO POINTER_PEN_INFO;
#endif

#define NDEBUG
#include <debug.h>

BOOL APIENTRY
NtUserGetPointerDeviceRects(
    _In_ HANDLE device,
    _Out_ RECT *deviceRect,
    _Out_ RECT *displayRect);

BOOL
WINAPI
GetAutoRotationState(
    _Out_ PAR_STATE pState)
{
    if (pState == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *pState = AR_NOT_SUPPORTED;
    return TRUE;
}

BOOL
WINAPI
GetPointerDevice(
    _In_ HANDLE device,
    _Out_ POINTER_DEVICE_INFO *pointerDevice)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pointerDevice);

    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

BOOL
WINAPI
GetPointerDeviceRects(
    _In_ HANDLE device,
    _Out_ RECT *deviceRect,
    _Out_ RECT *displayRect)
{
    if (device == NULL || deviceRect == NULL || displayRect == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return NtUserGetPointerDeviceRects(device, deviceRect, displayRect);
}

BOOL
WINAPI
GetPointerFrameTouchInfoHistory(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *entriesCount,
    _Inout_ UINT32 *pointerCount,
    _Out_writes_(*entriesCount * *pointerCount) POINTER_TOUCH_INFO *touchInfo)
{
    UNREFERENCED_PARAMETER(pointerId);
    UNREFERENCED_PARAMETER(entriesCount);
    UNREFERENCED_PARAMETER(pointerCount);
    UNREFERENCED_PARAMETER(touchInfo);

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL
WINAPI
GetPointerPenInfo(
    _In_ UINT32 pointerId,
    _Out_ POINTER_PEN_INFO *penInfo)
{
    UNREFERENCED_PARAMETER(pointerId);
    UNREFERENCED_PARAMETER(penInfo);

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL
WINAPI
GetPointerPenInfoHistory(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *entriesCount,
    _Out_writes_(*entriesCount) POINTER_PEN_INFO *penInfo)
{
    UNREFERENCED_PARAMETER(pointerId);
    UNREFERENCED_PARAMETER(entriesCount);
    UNREFERENCED_PARAMETER(penInfo);

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL
WINAPI
SkipPointerFrameMessages(
    _In_ UINT32 pointerId)
{
    UNREFERENCED_PARAMETER(pointerId);

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL
WINAPI
IsWindowArranged(
    _In_ HWND hwnd)
{
    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return FALSE;
}
