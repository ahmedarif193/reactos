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

#define NDEBUG
#include <debug.h>

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
IsWindowArranged(
    _In_ HWND hwnd)
{
    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }

    return FALSE;
}
