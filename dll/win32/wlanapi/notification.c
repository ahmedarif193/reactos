/*
 * Wireless LAN API (wlanapi.dll) -- notification delivery
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * One callback per client handle: the first registration parks a worker
 * thread in _RpcAsyncGetNotification which dispatches each delivered
 * WLAN_NOTIFICATION_DATA to the callback.  Setting the source to NONE or
 * closing the handle stops the worker.
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <wlansvc_c.h>
#include "wlanapi_local.h"

#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(wlanapi);

/* One registration per opened client handle. */
typedef struct _WLAN_NOTIF_REG
{
    struct _WLAN_NOTIF_REG     *Next;
    HANDLE                      hClientHandle;
    DWORD                       dwSource;
    WLAN_NOTIFICATION_CALLBACK  Callback;
    PVOID                       Context;
    HANDLE                      hThread;
    volatile LONG               Running;
} WLAN_NOTIF_REG, *PWLAN_NOTIF_REG;

static PWLAN_NOTIF_REG   NotifListHead = NULL;
static CRITICAL_SECTION  NotifLock;
static LONG              NotifInit = 0;

static VOID
WlanNotifEnsureInit(VOID)
{
    if (InterlockedCompareExchange(&NotifInit, 1, 0) == 0)
    {
        InitializeCriticalSection(&NotifLock);
        NotifListHead = NULL;
    }
}

/* Lock held by caller. */
static PWLAN_NOTIF_REG
WlanNotifFind(HANDLE hClientHandle)
{
    PWLAN_NOTIF_REG reg;

    for (reg = NotifListHead; reg != NULL; reg = reg->Next)
    {
        if (reg->hClientHandle == hClientHandle)
            return reg;
    }
    return NULL;
}

/* Lock held by caller. */
static VOID
WlanNotifUnlink(PWLAN_NOTIF_REG target)
{
    PWLAN_NOTIF_REG *pp = &NotifListHead;

    while (*pp != NULL)
    {
        if (*pp == target)
        {
            *pp = target->Next;
            target->Next = NULL;
            return;
        }
        pp = &(*pp)->Next;
    }
}

/*
 * Worker: drains notifications from the service and dispatches the callback.
 * The teardown path joins this thread before freeing reg; the parked async
 * call returns non-SUCCESS once the source is NONE or the handle is closed.
 */
static DWORD WINAPI
WlanNotifThread(LPVOID lpParameter)
{
    PWLAN_NOTIF_REG reg = (PWLAN_NOTIF_REG)lpParameter;

    for (;;)
    {
        PWLAN_NOTIFICATION_DATA pData = NULL;
        DWORD dwResult = ERROR_SUCCESS;

        RpcTryExcept
        {
            dwResult = _RpcAsyncGetNotification(reg->hClientHandle, &pData);
        }
        RpcExcept(EXCEPTION_EXECUTE_HANDLER)
        {
            dwResult = RpcExceptionCode();
        }
        RpcEndExcept;

        if (dwResult == ERROR_TIMEOUT)
        {
            /* Bounded poll expired with nothing queued; keep polling unless
             * a teardown started meanwhile. */
            if (pData != NULL)
                WlanFreeMemory(pData);
            if (!InterlockedCompareExchange(&reg->Running, 1, 1))
                break;
            continue;
        }

        if (dwResult != ERROR_SUCCESS || pData == NULL)
        {
            /* Source set to NONE, handle closed, or RPC failure -> stop. */
            if (pData != NULL)
                WlanFreeMemory(pData);
            break;
        }

        /* Skip dispatch if a teardown has started meanwhile. */
        if (InterlockedCompareExchange(&reg->Running, 1, 1) && reg->Callback != NULL)
            reg->Callback(pData, reg->Context);

        if (pData->pData != NULL)
            WlanFreeMemory(pData->pData);
        WlanFreeMemory(pData);
    }

    return 0;
}

/*
 * Release the parked getter, join the worker, then free reg.
 * Caller must NOT hold NotifLock; reg must already be unlinked.
 */
static VOID
WlanNotifTeardown(PWLAN_NOTIF_REG reg, HANDLE hClientHandle, BOOL handleStillValid)
{
    InterlockedExchange(&reg->Running, 0);
    reg->Callback = NULL;

    /* If the handle is still open, setting the source to NONE makes the
     * service release the parked getter; otherwise the close path does. */
    if (handleStillValid)
    {
        DWORD dwPrev = 0;
        RpcTryExcept
        {
            (void)_RpcRegisterNotification(hClientHandle,
                                           WLAN_NOTIFICATION_SOURCE_NONE,
                                           &dwPrev);
        }
        RpcExcept(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        RpcEndExcept;
    }

    if (reg->hThread != NULL)
    {
        WaitForSingleObject(reg->hThread, INFINITE);
        CloseHandle(reg->hThread);
    }

    HeapFree(GetProcessHeap(), 0, reg);
}

DWORD
WlanRegisterNotificationImpl(HANDLE hClientHandle,
                             DWORD dwNotifSource,
                             BOOL bIgnoreDuplicate,
                             WLAN_NOTIFICATION_CALLBACK funcCallback,
                             PVOID pCallbackContext,
                             PDWORD pdwPrevNotifSource)
{
    PWLAN_NOTIF_REG reg;
    DWORD dwPrev = WLAN_NOTIFICATION_SOURCE_NONE;
    DWORD dwResult = ERROR_SUCCESS;
    BOOL startThread = FALSE;

    UNREFERENCED_PARAMETER(bIgnoreDuplicate);

    WlanNotifEnsureInit();

    /* Register the requested sources with the service (gates the async getter). */
    RpcTryExcept
    {
        dwResult = _RpcRegisterNotification(hClientHandle, dwNotifSource, &dwPrev);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        dwResult = RpcExceptionCode();
    }
    RpcEndExcept;

    if (dwResult != ERROR_SUCCESS)
        return dwResult;

    EnterCriticalSection(&NotifLock);
    reg = WlanNotifFind(hClientHandle);

    if (dwNotifSource == WLAN_NOTIFICATION_SOURCE_NONE || funcCallback == NULL)
    {
        /* Deregister: unlink, then drain + free the worker outside the lock. */
        if (reg != NULL)
        {
            WlanNotifUnlink(reg);
            LeaveCriticalSection(&NotifLock);
            /* Service source already set to NONE by the RPC call above. */
            WlanNotifTeardown(reg, hClientHandle, FALSE);
        }
        else
        {
            LeaveCriticalSection(&NotifLock);
        }

        if (pdwPrevNotifSource != NULL)
            *pdwPrevNotifSource = dwPrev;
        return ERROR_SUCCESS;
    }

    if (reg == NULL)
    {
        reg = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*reg));
        if (reg == NULL)
        {
            LeaveCriticalSection(&NotifLock);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        reg->hClientHandle = hClientHandle;
        reg->Next = NotifListHead;
        NotifListHead = reg;
        startThread = TRUE;
    }

    reg->dwSource = dwNotifSource;
    reg->Callback = funcCallback;
    reg->Context = pCallbackContext;

    if (startThread)
    {
        InterlockedExchange(&reg->Running, 1);
        reg->hThread = CreateThread(NULL, 0, WlanNotifThread, reg, 0, NULL);
        if (reg->hThread == NULL)
        {
            WlanNotifUnlink(reg);
            LeaveCriticalSection(&NotifLock);
            HeapFree(GetProcessHeap(), 0, reg);
            return GetLastError();
        }
    }

    LeaveCriticalSection(&NotifLock);

    if (pdwPrevNotifSource != NULL)
        *pdwPrevNotifSource = dwPrev;
    return ERROR_SUCCESS;
}

/*
 * Called from WlanCloseHandle before _RpcCloseHandle: the handle is still
 * valid, so teardown can set the source to NONE to release the worker.
 */
VOID
WlanStopNotificationThread(HANDLE hClientHandle)
{
    PWLAN_NOTIF_REG reg;

    if (InterlockedCompareExchange(&NotifInit, 1, 1) == 0)
        return;

    EnterCriticalSection(&NotifLock);
    reg = WlanNotifFind(hClientHandle);
    if (reg != NULL)
        WlanNotifUnlink(reg);
    LeaveCriticalSection(&NotifLock);

    if (reg != NULL)
        WlanNotifTeardown(reg, hClientHandle, TRUE);
}
