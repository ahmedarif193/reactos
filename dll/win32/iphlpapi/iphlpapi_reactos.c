/*
 * PROJECT:     ReactOS Networking
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        dll/win32/iphlpapi/iphlpapi_reactos.c
 * PURPOSE:     ReactOS compatibility helpers
 * PROGRAMMERS: Pierre Schweitzer <pierre@reactos.org>
 */

#define IPHLPAPI_DLL_LINKAGE
#include "iphlpapi_private.h"
#include <netioapi.h>
#include <netiodef.h>
#include <wine/nsi.h>

WINE_DEFAULT_DEBUG_CHANNEL(iphlpapi);

#ifdef __REACTOS__
static DWORD get_interface_alias(const GUID *guid, WCHAR *name, ULONG *size)
{
    WCHAR alias[IF_MAX_STRING_SIZE + 1];
    NET_LUID luid;
    DWORD required, status;

    if (!guid || !size) return ERROR_INVALID_PARAMETER;
    if ((status = ConvertInterfaceGuidToLuid(guid, &luid))) return status;
    if ((status = ConvertInterfaceLuidToAlias(&luid, alias, ARRAYSIZE(alias)))) return status;
    required = (wcslen(alias) + 1) * sizeof(WCHAR);
    if (!name || *size < required)
    {
        *size = required;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    CopyMemory(name, alias, required);
    *size = required;
    return ERROR_SUCCESS;
}

DWORD WINAPI NhGetInterfaceNameFromDeviceGuid(const GUID *guid, WCHAR *name, ULONG *size, DWORD unknown4, DWORD unknown5)
{
    UNREFERENCED_PARAMETER(unknown4);
    UNREFERENCED_PARAMETER(unknown5);
    return get_interface_alias(guid, name, size);
}

DWORD WINAPI NhGetInterfaceNameFromGuid(const GUID *guid, WCHAR *name, ULONG *size, DWORD unknown4, DWORD unknown5)
{
    DWORD status;

    UNREFERENCED_PARAMETER(unknown4);
    UNREFERENCED_PARAMETER(unknown5);
    status = get_interface_alias(guid, name, size);
    if (status == ERROR_NOT_FOUND) SetLastError(ERROR_PATH_NOT_FOUND);
    return status;
}

DWORD WINAPI NhGetGuidFromInterfaceName(WCHAR *name, GUID *guid, DWORD unknown3, DWORD unknown4)
{
    NET_LUID luid;
    DWORD status;

    UNREFERENCED_PARAMETER(unknown3);
    UNREFERENCED_PARAMETER(unknown4);
    if (!name || !guid) return ERROR_INVALID_PARAMETER;
    if ((status = ConvertInterfaceAliasToLuid(name, &luid))) return status;
    return ConvertInterfaceLuidToGuid(&luid, guid);
}

enum network_change_kind
{
    NETWORK_CHANGE_INTERFACE,
    NETWORK_CHANGE_UNICAST,
    NETWORK_CHANGE_ROUTE
};

union network_change_callback
{
    PIPINTERFACE_CHANGE_CALLBACK interface_callback;
    PUNICAST_IPADDRESS_CHANGE_CALLBACK unicast_callback;
    PIPFORWARD_CHANGE_CALLBACK route_callback;
};

struct network_change_notification
{
    struct network_change_notification *next;
    union network_change_callback callback;
    PVOID context;
    ADDRESS_FAMILY family;
    enum network_change_kind kind;
    CRITICAL_SECTION lock;
    CRITICAL_SECTION callback_lock;
    OVERLAPPED ipv4_overlapped;
    OVERLAPPED ipv6_overlapped;
    HANDLE ipv4_nsi_handle;
    HANDLE ipv6_nsi_handle;
    HANDLE stop_event;
    HANDLE thread;
    DWORD thread_id;
    void *ipv4_snapshot;
    void *ipv6_snapshot;
    LONG references;
    LONG cancelled;
    BOOL ipv4_pending;
    BOOL ipv6_pending;
};

static SRWLOCK network_change_notification_list_lock = SRWLOCK_INIT;
static struct network_change_notification *network_change_notifications;

static BOOL network_change_notification_cancelled(struct network_change_notification *notification)
{
    return InterlockedCompareExchange(&notification->cancelled, FALSE, FALSE) != FALSE;
}

static void network_change_notification_release(struct network_change_notification *notification)
{
    if (InterlockedDecrement(&notification->references)) return;
    if (notification->ipv4_snapshot) FreeMibTable(notification->ipv4_snapshot);
    if (notification->ipv6_snapshot) FreeMibTable(notification->ipv6_snapshot);
    if (notification->thread) CloseHandle(notification->thread);
    if (notification->ipv4_overlapped.hEvent) CloseHandle(notification->ipv4_overlapped.hEvent);
    if (notification->ipv6_overlapped.hEvent) CloseHandle(notification->ipv6_overlapped.hEvent);
    if (notification->stop_event) CloseHandle(notification->stop_event);
    DeleteCriticalSection(&notification->callback_lock);
    DeleteCriticalSection(&notification->lock);
    HeapFree(GetProcessHeap(), 0, notification);
}

static BOOL interface_row_key_equal(const MIB_IPINTERFACE_ROW *left, const MIB_IPINTERFACE_ROW *right)
{
    return left->Family == right->Family && left->InterfaceLuid.Value == right->InterfaceLuid.Value;
}

static const MIB_IPINTERFACE_ROW *find_interface_row(const MIB_IPINTERFACE_TABLE *table, const MIB_IPINTERFACE_ROW *row)
{
    ULONG index;

    if (!table) return NULL;
    for (index = 0; index < table->NumEntries; ++index)
    {
        if (interface_row_key_equal(&table->Table[index], row)) return &table->Table[index];
    }
    return NULL;
}

static void call_network_change_notification(struct network_change_notification *notification, void *row, MIB_NOTIFICATION_TYPE type)
{
    EnterCriticalSection(&notification->callback_lock);
    if (!network_change_notification_cancelled(notification))
    {
        if (notification->kind == NETWORK_CHANGE_INTERFACE && notification->callback.interface_callback) notification->callback.interface_callback(notification->context, row, type);
        else if (notification->kind == NETWORK_CHANGE_UNICAST && notification->callback.unicast_callback) notification->callback.unicast_callback(notification->context, row, type);
        else if (notification->kind == NETWORK_CHANGE_ROUTE && notification->callback.route_callback) notification->callback.route_callback(notification->context, row, type);
    }
    LeaveCriticalSection(&notification->callback_lock);
}

static void dispatch_interface_changes(struct network_change_notification *notification, ADDRESS_FAMILY family)
{
    MIB_IPINTERFACE_TABLE *snapshot = family == AF_INET ? notification->ipv4_snapshot : notification->ipv6_snapshot;
    MIB_IPINTERFACE_TABLE *current = NULL;
    const MIB_IPINTERFACE_ROW *old_row;
    ULONG index;
    DWORD error;
    BOOL notified = FALSE;

    error = GetIpInterfaceTable(family, &current);
    if (error)
    {
        if (current) FreeMibTable(current);
        call_network_change_notification(notification, NULL, MibParameterNotification);
        return;
    }

    for (index = 0; index < current->NumEntries && !network_change_notification_cancelled(notification); ++index)
    {
        old_row = find_interface_row(snapshot, &current->Table[index]);
        if (!old_row)
        {
            call_network_change_notification(notification, &current->Table[index], MibAddInstance);
            notified = TRUE;
        }
        else if (memcmp(old_row, &current->Table[index], sizeof(*old_row)))
        {
            call_network_change_notification(notification, &current->Table[index], MibParameterNotification);
            notified = TRUE;
        }
    }

    if (snapshot)
    {
        for (index = 0; index < snapshot->NumEntries && !network_change_notification_cancelled(notification); ++index)
        {
            if (find_interface_row(current, &snapshot->Table[index])) continue;
            call_network_change_notification(notification, &snapshot->Table[index], MibDeleteInstance);
            notified = TRUE;
        }
    }

    if (!notified && !network_change_notification_cancelled(notification)) call_network_change_notification(notification, NULL, MibParameterNotification);
    if (snapshot) FreeMibTable(snapshot);
    if (family == AF_INET) notification->ipv4_snapshot = current;
    else notification->ipv6_snapshot = current;
}

static BOOL unicast_row_key_equal(const MIB_UNICASTIPADDRESS_ROW *left, const MIB_UNICASTIPADDRESS_ROW *right)
{
    return left->InterfaceLuid.Value == right->InterfaceLuid.Value && !memcmp(&left->Address, &right->Address, sizeof(left->Address));
}

static const MIB_UNICASTIPADDRESS_ROW *find_unicast_row(const MIB_UNICASTIPADDRESS_TABLE *table, const MIB_UNICASTIPADDRESS_ROW *row)
{
    ULONG index;

    if (!table) return NULL;
    for (index = 0; index < table->NumEntries; ++index)
    {
        if (unicast_row_key_equal(&table->Table[index], row)) return &table->Table[index];
    }
    return NULL;
}

static void dispatch_unicast_changes(struct network_change_notification *notification, ADDRESS_FAMILY family)
{
    MIB_UNICASTIPADDRESS_TABLE *snapshot = family == AF_INET ? notification->ipv4_snapshot : notification->ipv6_snapshot;
    MIB_UNICASTIPADDRESS_TABLE *current = NULL;
    const MIB_UNICASTIPADDRESS_ROW *old_row;
    ULONG index;
    DWORD error;
    BOOL notified = FALSE;

    error = GetUnicastIpAddressTable(family, &current);
    if (error)
    {
        if (current) FreeMibTable(current);
        call_network_change_notification(notification, NULL, MibParameterNotification);
        return;
    }

    for (index = 0; index < current->NumEntries && !network_change_notification_cancelled(notification); ++index)
    {
        old_row = find_unicast_row(snapshot, &current->Table[index]);
        if (!old_row)
        {
            call_network_change_notification(notification, &current->Table[index], MibAddInstance);
            notified = TRUE;
        }
        else if (memcmp(old_row, &current->Table[index], sizeof(*old_row)))
        {
            call_network_change_notification(notification, &current->Table[index], MibParameterNotification);
            notified = TRUE;
        }
    }

    if (snapshot)
    {
        for (index = 0; index < snapshot->NumEntries && !network_change_notification_cancelled(notification); ++index)
        {
            if (find_unicast_row(current, &snapshot->Table[index])) continue;
            call_network_change_notification(notification, &snapshot->Table[index], MibDeleteInstance);
            notified = TRUE;
        }
    }

    if (!notified && !network_change_notification_cancelled(notification)) call_network_change_notification(notification, NULL, MibParameterNotification);
    if (snapshot) FreeMibTable(snapshot);
    if (family == AF_INET) notification->ipv4_snapshot = current;
    else notification->ipv6_snapshot = current;
}

static BOOL route_row_key_equal(const MIB_IPFORWARD_ROW2 *left, const MIB_IPFORWARD_ROW2 *right)
{
    if (left->InterfaceLuid.Value != right->InterfaceLuid.Value) return FALSE;
    if (left->DestinationPrefix.PrefixLength != right->DestinationPrefix.PrefixLength) return FALSE;
    if (memcmp(&left->DestinationPrefix.Prefix, &right->DestinationPrefix.Prefix, sizeof(left->DestinationPrefix.Prefix))) return FALSE;
    return !memcmp(&left->NextHop, &right->NextHop, sizeof(left->NextHop));
}

static const MIB_IPFORWARD_ROW2 *find_route_row(const MIB_IPFORWARD_TABLE2 *table, const MIB_IPFORWARD_ROW2 *row)
{
    ULONG index;

    if (!table) return NULL;
    for (index = 0; index < table->NumEntries; ++index)
    {
        if (route_row_key_equal(&table->Table[index], row)) return &table->Table[index];
    }
    return NULL;
}

static void dispatch_route_changes(struct network_change_notification *notification, ADDRESS_FAMILY family)
{
    MIB_IPFORWARD_TABLE2 *snapshot = family == AF_INET ? notification->ipv4_snapshot : notification->ipv6_snapshot;
    MIB_IPFORWARD_TABLE2 *current = NULL;
    const MIB_IPFORWARD_ROW2 *old_row;
    ULONG index;
    DWORD error;
    BOOL notified = FALSE;

    error = GetIpForwardTable2(family, &current);
    if (error)
    {
        if (current) FreeMibTable(current);
        call_network_change_notification(notification, NULL, MibParameterNotification);
        return;
    }

    for (index = 0; index < current->NumEntries && !network_change_notification_cancelled(notification); ++index)
    {
        old_row = find_route_row(snapshot, &current->Table[index]);
        if (!old_row)
        {
            call_network_change_notification(notification, &current->Table[index], MibAddInstance);
            notified = TRUE;
        }
        else if (memcmp(old_row, &current->Table[index], sizeof(*old_row)))
        {
            call_network_change_notification(notification, &current->Table[index], MibParameterNotification);
            notified = TRUE;
        }
    }

    if (snapshot)
    {
        for (index = 0; index < snapshot->NumEntries && !network_change_notification_cancelled(notification); ++index)
        {
            if (find_route_row(current, &snapshot->Table[index])) continue;
            call_network_change_notification(notification, &snapshot->Table[index], MibDeleteInstance);
            notified = TRUE;
        }
    }

    if (!notified && !network_change_notification_cancelled(notification)) call_network_change_notification(notification, NULL, MibParameterNotification);
    if (snapshot) FreeMibTable(snapshot);
    if (family == AF_INET) notification->ipv4_snapshot = current;
    else notification->ipv6_snapshot = current;
}

static void dispatch_network_changes(struct network_change_notification *notification, ADDRESS_FAMILY family)
{
    if (notification->kind == NETWORK_CHANGE_INTERFACE) dispatch_interface_changes(notification, family);
    else if (notification->kind == NETWORK_CHANGE_UNICAST) dispatch_unicast_changes(notification, family);
    else dispatch_route_changes(notification, family);
}

static DWORD request_network_change_locked(struct network_change_notification *notification, ADDRESS_FAMILY family)
{
    const NPI_MODULEID *module = family == AF_INET ? &NPI_MS_IPV4_MODULEID : &NPI_MS_IPV6_MODULEID;
    OVERLAPPED *overlapped = family == AF_INET ? &notification->ipv4_overlapped : &notification->ipv6_overlapped;
    HANDLE *nsi_handle = family == AF_INET ? &notification->ipv4_nsi_handle : &notification->ipv6_nsi_handle;
    BOOL *pending = family == AF_INET ? &notification->ipv4_pending : &notification->ipv6_pending;
    HANDLE event = overlapped->hEvent;
    DWORD table;
    DWORD error;

    if (notification->kind == NETWORK_CHANGE_INTERFACE) table = NSI_IP_INTERFACE_TABLE;
    else if (notification->kind == NETWORK_CHANGE_UNICAST) table = NSI_IP_UNICAST_TABLE;
    else table = NSI_IP_FORWARD_TABLE;
    memset(overlapped, 0, sizeof(*overlapped));
    overlapped->hEvent = event;
    ResetEvent(event);
    error = NsiRequestChangeNotification(0, module, table, overlapped, nsi_handle);
    if (error != ERROR_IO_PENDING) return error;
    *pending = TRUE;
    return ERROR_SUCCESS;
}

static void cancel_network_changes_locked(struct network_change_notification *notification)
{
    if (notification->ipv4_pending)
    {
        NsiCancelChangeNotification(&notification->ipv4_overlapped);
        notification->ipv4_pending = FALSE;
    }
    if (notification->ipv6_pending)
    {
        NsiCancelChangeNotification(&notification->ipv6_overlapped);
        notification->ipv6_pending = FALSE;
    }
}

static DWORD WINAPI network_change_notification_worker(PVOID parameter)
{
    struct network_change_notification *notification = parameter;
    ADDRESS_FAMILY families[3];
    HANDLE waits[3];
    DWORD bytes, error, index, count = 1;
    BOOL completed;

    waits[0] = notification->stop_event;
    if (notification->family != AF_INET6)
    {
        waits[count] = notification->ipv4_overlapped.hEvent;
        families[count++] = AF_INET;
    }
    if (notification->family != AF_INET)
    {
        waits[count] = notification->ipv6_overlapped.hEvent;
        families[count++] = AF_INET6;
    }

    for (;;)
    {
        index = WaitForMultipleObjects(count, waits, FALSE, INFINITE);
        if (index == WAIT_OBJECT_0 || index >= WAIT_OBJECT_0 + count) break;
        index -= WAIT_OBJECT_0;

        EnterCriticalSection(&notification->lock);
        if (families[index] == AF_INET) notification->ipv4_pending = FALSE;
        else notification->ipv6_pending = FALSE;
        LeaveCriticalSection(&notification->lock);

        if (families[index] == AF_INET) completed = GetOverlappedResult(notification->ipv4_nsi_handle, &notification->ipv4_overlapped, &bytes, FALSE);
        else completed = GetOverlappedResult(notification->ipv6_nsi_handle, &notification->ipv6_overlapped, &bytes, FALSE);
        error = completed ? ERROR_SUCCESS : GetLastError();
        if (!network_change_notification_cancelled(notification) && (completed || error != ERROR_OPERATION_ABORTED)) dispatch_network_changes(notification, families[index]);

        EnterCriticalSection(&notification->lock);
        if (network_change_notification_cancelled(notification)) error = ERROR_OPERATION_ABORTED;
        else error = request_network_change_locked(notification, families[index]);
        LeaveCriticalSection(&notification->lock);
        if (error) break;
    }

    network_change_notification_release(notification);
    return ERROR_SUCCESS;
}

DWORD IphlpapiCancelMibChangeNotify2(HANDLE handle)
{
    struct network_change_notification **cursor;
    struct network_change_notification *notification = NULL;

    AcquireSRWLockExclusive(&network_change_notification_list_lock);
    for (cursor = &network_change_notifications; *cursor; cursor = &(*cursor)->next)
    {
        if ((HANDLE)*cursor != handle) continue;
        notification = *cursor;
        *cursor = notification->next;
        break;
    }
    ReleaseSRWLockExclusive(&network_change_notification_list_lock);
    if (!notification) return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&notification->lock);
    InterlockedExchange(&notification->cancelled, TRUE);
    SetEvent(notification->stop_event);
    cancel_network_changes_locked(notification);
    LeaveCriticalSection(&notification->lock);
    if (notification->thread_id != GetCurrentThreadId()) WaitForSingleObject(notification->thread, INFINITE);
    network_change_notification_release(notification);
    return ERROR_SUCCESS;
}

static void capture_network_snapshot(struct network_change_notification *notification, ADDRESS_FAMILY family)
{
    MIB_IPINTERFACE_TABLE *interface_table = NULL;
    MIB_UNICASTIPADDRESS_TABLE *unicast_table = NULL;
    MIB_IPFORWARD_TABLE2 *route_table = NULL;
    void *snapshot = NULL;
    DWORD error;

    if (notification->kind == NETWORK_CHANGE_INTERFACE)
    {
        error = GetIpInterfaceTable(family, &interface_table);
        snapshot = interface_table;
    }
    else if (notification->kind == NETWORK_CHANGE_UNICAST)
    {
        error = GetUnicastIpAddressTable(family, &unicast_table);
        snapshot = unicast_table;
    }
    else
    {
        error = GetIpForwardTable2(family, &route_table);
        snapshot = route_table;
    }
    if (error && snapshot)
    {
        FreeMibTable(snapshot);
        snapshot = NULL;
    }
    if (family == AF_INET) notification->ipv4_snapshot = snapshot;
    else notification->ipv6_snapshot = snapshot;
}

static DWORD register_network_change(enum network_change_kind kind, union network_change_callback callback, ADDRESS_FAMILY family, PVOID context, BOOLEAN initial, PHANDLE handle)
{
    struct network_change_notification *notification;
    DWORD error = ERROR_SUCCESS;

    if (!handle) return ERROR_INVALID_PARAMETER;
    *handle = NULL;
    if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) return ERROR_INVALID_PARAMETER;
    notification = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*notification));
    if (!notification) return ERROR_NOT_ENOUGH_MEMORY;

    notification->references = 1;
    notification->callback = callback;
    notification->context = context;
    notification->family = family;
    notification->kind = kind;
    InitializeCriticalSection(&notification->lock);
    InitializeCriticalSection(&notification->callback_lock);
    notification->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (family != AF_INET6) notification->ipv4_overlapped.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (family != AF_INET) notification->ipv6_overlapped.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!notification->stop_event || (family != AF_INET6 && !notification->ipv4_overlapped.hEvent) || (family != AF_INET && !notification->ipv6_overlapped.hEvent)) error = GetLastError();
    if (!error && family != AF_INET6) capture_network_snapshot(notification, AF_INET);
    if (!error && family != AF_INET) capture_network_snapshot(notification, AF_INET6);

    EnterCriticalSection(&notification->lock);
    if (!error && family != AF_INET6) error = request_network_change_locked(notification, AF_INET);
    if (!error && family != AF_INET) error = request_network_change_locked(notification, AF_INET6);
    if (error) cancel_network_changes_locked(notification);
    LeaveCriticalSection(&notification->lock);
    if (error)
    {
        network_change_notification_release(notification);
        return error;
    }

    InterlockedIncrement(&notification->references);
    notification->thread = CreateThread(NULL, 0, network_change_notification_worker, notification, 0, &notification->thread_id);
    if (!notification->thread)
    {
        error = GetLastError();
        network_change_notification_release(notification);
        EnterCriticalSection(&notification->lock);
        cancel_network_changes_locked(notification);
        LeaveCriticalSection(&notification->lock);
        network_change_notification_release(notification);
        return error;
    }

    InterlockedIncrement(&notification->references);
    AcquireSRWLockExclusive(&network_change_notification_list_lock);
    notification->next = network_change_notifications;
    network_change_notifications = notification;
    ReleaseSRWLockExclusive(&network_change_notification_list_lock);
    *handle = notification;

    if (initial && family != AF_INET6) call_network_change_notification(notification, NULL, MibInitialNotification);
    if (initial && family != AF_INET) call_network_change_notification(notification, NULL, MibInitialNotification);
    SetLastError(ERROR_SUCCESS);
    network_change_notification_release(notification);
    return ERROR_SUCCESS;
}

DWORD IphlpapiNotifyIpInterfaceChange(ADDRESS_FAMILY family, PIPINTERFACE_CHANGE_CALLBACK callback, PVOID context, BOOLEAN initial, PHANDLE handle)
{
    union network_change_callback network_callback = {0};

    network_callback.interface_callback = callback;
    return register_network_change(NETWORK_CHANGE_INTERFACE, network_callback, family, context, initial, handle);
}

DWORD IphlpapiNotifyUnicastIpAddressChange(ADDRESS_FAMILY family, PUNICAST_IPADDRESS_CHANGE_CALLBACK callback, PVOID context, BOOLEAN initial, PHANDLE handle)
{
    union network_change_callback network_callback = {0};

    network_callback.unicast_callback = callback;
    return register_network_change(NETWORK_CHANGE_UNICAST, network_callback, family, context, initial, handle);
}

DWORD IphlpapiNotifyRouteChange2(ADDRESS_FAMILY family, PIPFORWARD_CHANGE_CALLBACK callback, PVOID context, BOOLEAN initial, PHANDLE handle)
{
    union network_change_callback network_callback = {0};

    network_callback.route_callback = callback;
    return register_network_change(NETWORK_CHANGE_ROUTE, network_callback, family, context, initial, handle);
}
#endif
