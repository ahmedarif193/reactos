/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for IP interface change notification registrations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

struct callback_state
{
    LONG count;
    DWORD caller_thread;
    BOOL same_thread;
    BOOL initial_type;
    BOOL null_row;
};

static VOID record_change_callback(struct callback_state *state, const void *row, MIB_NOTIFICATION_TYPE type)
{
    if (GetCurrentThreadId() != state->caller_thread) state->same_thread = FALSE;
    if (type != MibInitialNotification) state->initial_type = FALSE;
    if (row) state->null_row = FALSE;
    InterlockedIncrement(&state->count);
}

static VOID WINAPI interface_change_callback(PVOID context, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE type)
{
    record_change_callback(context, row, type);
}

static VOID WINAPI unicast_change_callback(PVOID context, PMIB_UNICASTIPADDRESS_ROW row, MIB_NOTIFICATION_TYPE type)
{
    record_change_callback(context, row, type);
}

static VOID WINAPI route_change_callback(PVOID context, PMIB_IPFORWARD_ROW2 row, MIB_NOTIFICATION_TYPE type)
{
    record_change_callback(context, row, type);
}

static VOID check_registration(HANDLE handle, struct callback_state *state, LONG expected_count)
{
    DWORD flags = 0xcccccccc;
    BOOL result;

    ok(handle != NULL, "Expected an opaque notification handle\n");
    ok_long(state->count, expected_count);
    ok(state->same_thread, "Initial callback ran on another thread\n");
    ok(state->initial_type, "Initial callback used a non-initial notification type\n");
    ok(state->null_row, "Initial callback supplied a row\n");

    SetLastError(0xdeadbeef);
    result = GetHandleInformation(handle, &flags);
    ok(!result, "Opaque notification handle was accepted as a kernel handle\n");
    ok_long(GetLastError(), ERROR_INVALID_HANDLE);
    ok_hex(flags, 0xcccccccc);

    SetLastError(0xdeadbeef);
    result = CloseHandle(handle);
    ok(!result, "CloseHandle accepted an opaque notification handle\n");
    ok_long(GetLastError(), ERROR_INVALID_HANDLE);
    ok_long(CancelMibChangeNotify2(handle), ERROR_SUCCESS);
}

static VOID initialize_callback_state(struct callback_state *state)
{
    ZeroMemory(state, sizeof(*state));
    state->caller_thread = GetCurrentThreadId();
    state->same_thread = TRUE;
    state->initial_type = TRUE;
    state->null_row = TRUE;
}

static VOID test_interface_registration(ADDRESS_FAMILY family, BOOLEAN initial, LONG expected_count)
{
    struct callback_state state;
    HANDLE handle = NULL;
    DWORD error;

    initialize_callback_state(&state);
    error = NotifyIpInterfaceChange(family, interface_change_callback, &state, initial, &handle);
    ok_long(error, ERROR_SUCCESS);
    check_registration(handle, &state, expected_count);
}

static VOID test_unicast_registration(ADDRESS_FAMILY family, BOOLEAN initial, LONG expected_count)
{
    struct callback_state state;
    HANDLE handle = NULL;
    DWORD error;

    initialize_callback_state(&state);
    error = NotifyUnicastIpAddressChange(family, unicast_change_callback, &state, initial, &handle);
    ok_long(error, ERROR_SUCCESS);
    check_registration(handle, &state, expected_count);
}

static VOID test_route_registration(ADDRESS_FAMILY family, BOOLEAN initial, LONG expected_count)
{
    struct callback_state state;
    HANDLE handle = NULL;
    DWORD error;

    initialize_callback_state(&state);
    error = NotifyRouteChange2(family, route_change_callback, &state, initial, &handle);
    ok_long(error, ERROR_SUCCESS);
    check_registration(handle, &state, expected_count);
}

START_TEST(NotifyIpInterfaceChange)
{
    struct callback_state state = {0};
    HANDLE handle;
    DWORD error, index;

    handle = (HANDLE)(ULONG_PTR)0xcccccccc;
    error = NotifyIpInterfaceChange(AF_INET, NULL, &state, FALSE, &handle);
    ok_long(error, ERROR_SUCCESS);
    ok(handle != NULL && handle != (HANDLE)(ULONG_PTR)0xcccccccc, "Expected an opaque notification handle, got %p\n", handle);
    ok_long(CancelMibChangeNotify2(handle), ERROR_SUCCESS);

    handle = (HANDLE)(ULONG_PTR)0xcccccccc;
    error = NotifyIpInterfaceChange(AF_UNIX, interface_change_callback, &state, FALSE, &handle);
    ok_long(error, ERROR_INVALID_PARAMETER);
    ok(handle == NULL, "Invalid family left handle %p\n", handle);
    ok_long(state.count, 0);
    ok_long(CancelMibChangeNotify2(NULL), ERROR_INVALID_HANDLE);

    handle = (HANDLE)(ULONG_PTR)0xcccccccc;
    error = NotifyUnicastIpAddressChange(AF_INET, NULL, &state, FALSE, &handle);
    ok_long(error, ERROR_SUCCESS);
    ok(handle != NULL && handle != (HANDLE)(ULONG_PTR)0xcccccccc, "Expected a unicast notification handle, got %p\n", handle);
    ok_long(CancelMibChangeNotify2(handle), ERROR_SUCCESS);

    handle = (HANDLE)(ULONG_PTR)0xcccccccc;
    error = NotifyUnicastIpAddressChange(AF_UNIX, unicast_change_callback, &state, FALSE, &handle);
    ok_long(error, ERROR_INVALID_PARAMETER);
    ok(handle == NULL, "Invalid unicast family left handle %p\n", handle);

    handle = (HANDLE)(ULONG_PTR)0xcccccccc;
    error = NotifyRouteChange2(AF_INET, NULL, &state, FALSE, &handle);
    ok_long(error, ERROR_SUCCESS);
    ok(handle != NULL && handle != (HANDLE)(ULONG_PTR)0xcccccccc, "Expected a route notification handle, got %p\n", handle);
    ok_long(CancelMibChangeNotify2(handle), ERROR_SUCCESS);

    handle = (HANDLE)(ULONG_PTR)0xcccccccc;
    error = NotifyRouteChange2(AF_UNIX, route_change_callback, &state, FALSE, &handle);
    ok_long(error, ERROR_INVALID_PARAMETER);
    ok(handle == NULL, "Invalid route family left handle %p\n", handle);

    test_interface_registration(AF_INET, FALSE, 0);
    test_interface_registration(AF_INET, TRUE, 1);
    test_interface_registration(AF_UNSPEC, TRUE, 2);
    test_interface_registration(AF_INET6, TRUE, 1);
    test_unicast_registration(AF_INET, FALSE, 0);
    test_unicast_registration(AF_INET, TRUE, 1);
    test_unicast_registration(AF_UNSPEC, TRUE, 2);
    test_unicast_registration(AF_INET6, TRUE, 1);
    test_route_registration(AF_INET, FALSE, 0);
    test_route_registration(AF_INET, TRUE, 1);
    test_route_registration(AF_UNSPEC, TRUE, 2);
    test_route_registration(AF_INET6, TRUE, 1);

    for (index = 0; index < 32; ++index)
    {
        handle = NULL;
        error = NotifyIpInterfaceChange(AF_INET, interface_change_callback, &state, FALSE, &handle);
        ok(error == ERROR_SUCCESS && handle != NULL, "Registration %lu returned %lu and %p\n", index, error, handle);
        if (!error && handle) ok_long(CancelMibChangeNotify2(handle), ERROR_SUCCESS);
    }
}
