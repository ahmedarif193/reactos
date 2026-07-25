/*
 * PROJECT:     ReactOS WLAN Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        base/services/wlansvc/nwifi_ioctl.c
 * PURPOSE:     IOCTL bridge to the nwifi.sys control device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Issues the <reactos/nwifi_ioctl.h> IOCTLs on \\.\Nwifi; a worker thread
 * pends GET_NOTIFICATION and turns kernel events into WLAN ACM notifications.
 * If nwifi is not loaded, entry points fail and no interfaces are presented.
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

/*
 * Shared control-device handle: opened lazily, kept open for the service's
 * lifetime, serialised by WlanSvcLock at the call sites.  The notify worker
 * uses a private handle so its pended read never blocks control IOCTLs.
 */
static HANDLE NwifiControlHandle = INVALID_HANDLE_VALUE;

/* The notify worker. */
static HANDLE NwifiNotifyThread = NULL;
static HANDLE NwifiNotifyHandle = INVALID_HANDLE_VALUE;
static volatile LONG NwifiNotifyRun = 0;

/* Returns INVALID_HANDLE_VALUE when nwifi is not loaded ("no adapters"). */
static HANDLE
NwifiOpenDevice(VOID)
{
    return CreateFileW(NWIFI_CONTROL_WIN32_NAME,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED,
                       NULL);
}

/* Lazily obtain the shared control handle.  Lock held by caller. */
static HANDLE
NwifiGetControlHandle(VOID)
{
    if (NwifiControlHandle == INVALID_HANDLE_VALUE)
        NwifiControlHandle = NwifiOpenDevice();
    return NwifiControlHandle;
}

/*
 * Issue one synchronous (buffered) IOCTL on an overlapped handle, waiting for
 * completion.  Maps "device not present" to ERROR_NOT_READY so callers can
 * report a sensible WLAN error.  *pBytesReturned receives the output length.
 */
static DWORD
NwifiIoctl(HANDLE hDev,
           DWORD ControlCode,
           LPVOID pIn, DWORD InSize,
           LPVOID pOut, DWORD OutSize,
           LPDWORD pBytesReturned)
{
    OVERLAPPED ov;
    DWORD bytes = 0;
    DWORD err;

    if (pBytesReturned != NULL)
        *pBytesReturned = 0;

    if (hDev == INVALID_HANDLE_VALUE)
        return ERROR_NOT_READY;

    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    if (DeviceIoControl(hDev, ControlCode, pIn, InSize, pOut, OutSize, &bytes, &ov))
    {
        err = ERROR_SUCCESS;
    }
    else
    {
        err = GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            if (GetOverlappedResult(hDev, &ov, &bytes, TRUE))
                err = ERROR_SUCCESS;
            else
                err = GetLastError();
        }
    }

    CloseHandle(ov.hEvent);

    if (err == ERROR_SUCCESS && pBytesReturned != NULL)
        *pBytesReturned = bytes;

    return err;
}

DWORD
NwifiEnumInterfaces(PNWIFI_INTERFACE_LIST *ppList)
{
    PNWIFI_INTERFACE_LIST list = NULL;
    HANDLE hDev;
    DWORD size, ret, err;

    *ppList = NULL;

    hDev = NwifiGetControlHandle();
    if (hDev == INVALID_HANDLE_VALUE)
        return ERROR_NOT_READY;

    /* Start with room for a handful of interfaces and grow on overflow. */
    size = FIELD_OFFSET(NWIFI_INTERFACE_LIST, Item) +
           8 * sizeof(NWIFI_INTERFACE_REF);

    for (;;)
    {
        list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        if (list == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        err = NwifiIoctl(hDev, IOCTL_NWIFI_ENUM_INTERFACES,
                         NULL, 0, list, size, &ret);

        if (err == ERROR_SUCCESS)
            break;

        HeapFree(GetProcessHeap(), 0, list);
        list = NULL;

        if (err == ERROR_MORE_DATA || err == ERROR_INSUFFICIENT_BUFFER)
        {
            /* Driver wants a bigger buffer: double and retry. */
            size *= 2;
            if (size > 64 * sizeof(NWIFI_INTERFACE_REF))
                return err;
            continue;
        }
        return err;
    }

    *ppList = list;
    return ERROR_SUCCESS;
}

DWORD
NwifiScan(ULONG InterfaceIndex, PDOT11_SSID pSsid, DOT11_BSS_TYPE BssType)
{
    NWIFI_SCAN_REQUEST req;
    HANDLE hDev;

    hDev = NwifiGetControlHandle();
    if (hDev == INVALID_HANDLE_VALUE)
        return ERROR_NOT_READY;

    ZeroMemory(&req, sizeof(req));
    req.Size = sizeof(req);
    req.InterfaceIndex = InterfaceIndex;
    req.BssType = (BssType != 0) ? BssType : dot11_BSS_type_any;
    if (pSsid != NULL && pSsid->uSSIDLength != 0)
        req.Ssid = *pSsid;          /* zero-length SSID => broadcast scan */

    /*
     * The scan request only kicks OID_DOT11_SCAN_REQUEST; completion arrives
     * asynchronously on the notify channel and results are read back with
     * IOCTL_NWIFI_GET_BSS_LIST.
     */
    return NwifiIoctl(hDev, IOCTL_NWIFI_SCAN, &req, sizeof(req), NULL, 0, NULL);
}

DWORD
NwifiGetBssList(ULONG InterfaceIndex, ULONG64 UpperLuid,
                const DOT11_MAC_ADDRESS *pMac, PNWIFI_BSS_LIST *ppList)
{
    PNWIFI_BSS_LIST list = NULL;
    NWIFI_INTERFACE_REF ref;
    HANDLE hDev;
    DWORD size, ret, err;

    *ppList = NULL;

    hDev = NwifiGetControlHandle();
    if (hDev == INVALID_HANDLE_VALUE)
        return ERROR_NOT_READY;

    ZeroMemory(&ref, sizeof(ref));
    ref.Size = sizeof(ref);
    ref.InterfaceIndex = InterfaceIndex;
    ref.UpperLuid = UpperLuid;
    if (pMac != NULL)
        ref.MacAddress = *pMac;

    /* Generous initial buffer; grow on ERROR_MORE_DATA. */
    size = 8 * 1024;

    for (;;)
    {
        list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        if (list == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        err = NwifiIoctl(hDev, IOCTL_NWIFI_GET_BSS_LIST,
                         &ref, sizeof(ref),
                         list, size, &ret);

        if (err == ERROR_SUCCESS)
        {
            /* If the driver reports it needed more than we gave, grow. */
            if (list->Size > size)
            {
                size = list->Size;
                HeapFree(GetProcessHeap(), 0, list);
                list = NULL;
                if (size > 256 * 1024)
                    return ERROR_NOT_ENOUGH_MEMORY;
                continue;
            }
            break;
        }

        HeapFree(GetProcessHeap(), 0, list);
        list = NULL;

        if (err == ERROR_MORE_DATA || err == ERROR_INSUFFICIENT_BUFFER)
        {
            size *= 2;
            if (size > 256 * 1024)
                return err;
            continue;
        }
        return err;
    }

    *ppList = list;
    return ERROR_SUCCESS;
}

DWORD
NwifiConnect(PNWIFI_CONNECT_REQUEST pReq)
{
    HANDLE hDev = NwifiGetControlHandle();

    if (hDev == INVALID_HANDLE_VALUE)
        return ERROR_NOT_READY;

    pReq->Size = sizeof(*pReq);
    return NwifiIoctl(hDev, IOCTL_NWIFI_CONNECT, pReq, sizeof(*pReq),
                      NULL, 0, NULL);
}

DWORD
NwifiDisconnect(ULONG InterfaceIndex, ULONG64 UpperLuid,
                const DOT11_MAC_ADDRESS *pMac)
{
    NWIFI_INTERFACE_REF ref;
    HANDLE hDev = NwifiGetControlHandle();

    if (hDev == INVALID_HANDLE_VALUE)
        return ERROR_NOT_READY;

    ZeroMemory(&ref, sizeof(ref));
    ref.Size = sizeof(ref);
    ref.InterfaceIndex = InterfaceIndex;
    ref.UpperLuid = UpperLuid;
    if (pMac != NULL)
        ref.MacAddress = *pMac;

    return NwifiIoctl(hDev, IOCTL_NWIFI_DISCONNECT, &ref, sizeof(ref),
                      NULL, 0, NULL);
}

DWORD
NwifiSetKey(PNWIFI_SET_KEY pKey)
{
    HANDLE hDev = NwifiGetControlHandle();

    if (hDev == INVALID_HANDLE_VALUE)
        return ERROR_NOT_READY;

    pKey->Size = sizeof(*pKey);
    return NwifiIoctl(hDev, IOCTL_NWIFI_SET_KEY, pKey, sizeof(*pKey),
                      NULL, 0, NULL);
}

DWORD
NwifiQueryState(ULONG InterfaceIndex, ULONG64 UpperLuid,
                const DOT11_MAC_ADDRESS *pMac,
                PNWIFI_LINK_STATE pState)
{
    NWIFI_INTERFACE_REF ref;
    HANDLE hDev = NwifiGetControlHandle();
    DWORD ret;

    if (hDev == INVALID_HANDLE_VALUE)
        return ERROR_NOT_READY;

    ZeroMemory(&ref, sizeof(ref));
    ref.Size = sizeof(ref);
    ref.InterfaceIndex = InterfaceIndex;
    ref.UpperLuid = UpperLuid;
    if (pMac != NULL)
        ref.MacAddress = *pMac;

    ZeroMemory(pState, sizeof(*pState));
    return NwifiIoctl(hDev, IOCTL_NWIFI_QUERY_STATE, &ref, sizeof(ref),
                      pState, sizeof(*pState), &ret);
}

/*
 * Notification worker: pends IOCTL_NWIFI_GET_NOTIFICATION on a private handle
 * in a loop; each completed NWIFI_NOTIFICATION is translated into the matching
 * WLAN ACM notification (delivered to subscribed clients by notify.c).
 */

/* Map a kernel notification onto its WLAN ACM code + signalled interface. */
static VOID
NwifiDispatchNotification(const NWIFI_NOTIFICATION *pNotif)
{
    PWLANSVC_INTERFACE iface;
    PNWIFI_BSS_LIST bssList = NULL;
    GUID interfaceGuid;
    DOT11_MAC_ADDRESS macAddress;
    ULONG interfaceIndex;
    ULONG64 upperLuid;
    DWORD dwResult;

    EnterCriticalSection(&WlanSvcLock);

    iface = WlanSvcFindInterfaceByIndex(pNotif->InterfaceIndex);

    switch (pNotif->Code)
    {
        case NwifiNotifyScanComplete:
            if (iface != NULL)
            {
                /* Snapshot the driver identity; iface may be freed while the
                 * service lock is dropped for the blocking IOCTL. */
                interfaceGuid = iface->InterfaceGuid;
                interfaceIndex = iface->NwifiIndex;
                upperLuid = iface->UpperLuid;
                macAddress = iface->MacAddress;

                LeaveCriticalSection(&WlanSvcLock);
                dwResult = NwifiGetBssList(interfaceIndex, upperLuid, &macAddress, &bssList);
                EnterCriticalSection(&WlanSvcLock);

                /* Re-resolve and validate the identity before touching any
                 * in-process state: removal can free the original iface. */
                iface = WlanSvcFindInterface(&interfaceGuid);
                if (iface != NULL && iface->NwifiIndex == interfaceIndex && iface->UpperLuid == upperLuid && memcmp(&iface->MacAddress, &macAddress, sizeof(macAddress)) == 0)
                {
                    if (dwResult == ERROR_SUCCESS && bssList != NULL)
                        WlanSvcApplyBssCache(iface, bssList);
                    WlanSvcIndicateAcm(iface, wlan_notification_acm_scan_complete);
                }

                if (bssList != NULL)
                    HeapFree(GetProcessHeap(), 0, bssList);
            }
            break;

        case NwifiNotifyConnectComplete:
            if (iface != NULL)
            {
                WlanSvcUpdateLinkState(iface);
                if (pNotif->StatusCode == 0 /* NDIS_STATUS_SUCCESS */)
                    WlanSvcIndicateConnection(iface,
                        wlan_notification_acm_connection_complete,
                        (iface->ConnectedProfile[0] != L'\0')
                            ? wlan_connection_mode_profile
                            : wlan_connection_mode_discovery_unsecure,
                        iface->ConnectedProfile, WLAN_REASON_CODE_SUCCESS);
                else
                    WlanSvcIndicateConnection(iface,
                        wlan_notification_acm_connection_attempt_fail,
                        (iface->ConnectedProfile[0] != L'\0')
                            ? wlan_connection_mode_profile
                            : wlan_connection_mode_discovery_unsecure,
                        iface->ConnectedProfile, pNotif->StatusCode);
            }
            break;

        case NwifiNotifyDisconnected:
        case NwifiNotifyLinkDown:
            if (iface != NULL)
            {
                iface->Connected = FALSE;
                iface->State = wlan_interface_state_disconnected;
                iface->Rssi = -100;
                iface->LinkQuality = 0;
                WlanSvcIndicateAcm(iface, wlan_notification_acm_disconnected);
            }
            break;

        case NwifiNotifyLinkUp:
            if (iface != NULL)
            {
                WlanSvcUpdateLinkState(iface);
                WlanSvcIndicateAcm(iface,
                                   wlan_notification_acm_connection_complete);
            }
            break;

        case NwifiNotifyInterfaceArrival:
            /* Rebuild the interface list and announce the new adapter. */
            WlanSvcRefreshInterfaces();
            iface = WlanSvcFindInterfaceByIndex(pNotif->InterfaceIndex);
            if (iface != NULL)
                WlanSvcIndicateAcm(iface,
                                   wlan_notification_acm_interface_arrival);
            break;

        case NwifiNotifyInterfaceRemoval:
            if (iface != NULL)
                WlanSvcIndicateAcm(iface,
                                   wlan_notification_acm_interface_removal);
            WlanSvcRefreshInterfaces();
            break;

        case NwifiNotifyNone:
        default:
            break;
    }

    LeaveCriticalSection(&WlanSvcLock);
}

static DWORD WINAPI
NwifiNotifyWorker(LPVOID lpParameter)
{
    HANDLE hDev = (HANDLE)lpParameter;

    while (InterlockedCompareExchange(&NwifiNotifyRun, 1, 1) != 0)
    {
        NWIFI_NOTIFICATION notif;
        DWORD ret = 0;
        DWORD err;

        ZeroMemory(&notif, sizeof(notif));

        /*
         * Pend the notification IOCTL; nwifi completes it when the next
         * interface event fires.  NwifiIoctl waits on the overlapped event.
         */
        err = NwifiIoctl(hDev, IOCTL_NWIFI_GET_NOTIFICATION,
                         NULL, 0, &notif, sizeof(notif), &ret);

        if (InterlockedCompareExchange(&NwifiNotifyRun, 1, 1) == 0)
            break;      /* shutting down */

        if (err == ERROR_SUCCESS && ret >= sizeof(NWIFI_NOTIFICATION))
        {
            NwifiDispatchNotification(&notif);
            continue;
        }

        if (err == ERROR_OPERATION_ABORTED)
            break;      /* CancelIo on shutdown */

        if (err == ERROR_NOT_READY || err == ERROR_INVALID_HANDLE ||
            err == ERROR_FILE_NOT_FOUND)
            break;      /* device gone */

        /*
         * Transient/unsupported completion: back off briefly so a driver that
         * does not (yet) implement the notify IOCTL cannot spin this thread.
         */
        Sleep(500);
    }

    return 0;
}

VOID
NwifiStartNotifyWorker(VOID)
{
    HANDLE hDev;

    if (NwifiNotifyThread != NULL)
        return;     /* already running */

    /* Use a dedicated handle so the pended read never blocks control IOCTLs. */
    hDev = NwifiOpenDevice();
    if (hDev == INVALID_HANDLE_VALUE)
        return;     /* nwifi not present: no events to deliver */

    NwifiNotifyHandle = hDev;
    InterlockedExchange(&NwifiNotifyRun, 1);

    NwifiNotifyThread = CreateThread(NULL, 0, NwifiNotifyWorker, hDev, 0, NULL);
    if (NwifiNotifyThread == NULL)
    {
        InterlockedExchange(&NwifiNotifyRun, 0);
        CloseHandle(hDev);
        NwifiNotifyHandle = INVALID_HANDLE_VALUE;
    }
}

VOID
NwifiStopNotifyWorker(VOID)
{
    HANDLE hThread = NwifiNotifyThread;
    HANDLE hDev = NwifiNotifyHandle;

    if (hThread == NULL)
        return;

    /* Signal stop and unblock the pended IOCTL. */
    InterlockedExchange(&NwifiNotifyRun, 0);
    if (hDev != INVALID_HANDLE_VALUE)
        CancelIoEx(hDev, NULL);

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    NwifiNotifyThread = NULL;

    if (hDev != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDev);
        NwifiNotifyHandle = INVALID_HANDLE_VALUE;
    }
}

/* Close the shared control handle (service shutdown). */
VOID
NwifiCloseControl(VOID)
{
    if (NwifiControlHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(NwifiControlHandle);
        NwifiControlHandle = INVALID_HANDLE_VALUE;
    }
}
