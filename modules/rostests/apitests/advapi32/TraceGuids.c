/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Classic ETW provider registration lifecycle
 */

#include "precomp.h"
#include <wmistr.h>
#include <evntrace.h>

static const GUID ProviderGuid = {0x36905a26,0x7a7d,0x4d76,{0x99,0xae,0x56,0x17,0x19,0xe5,0x76,0x11}};
static ULONG WINAPI Callback(WMIDPREQUESTCODE Code, PVOID Context, ULONG *Size, PVOID Buffer)
{
    return ERROR_SUCCESS;
}

START_TEST(TraceGuids)
{
    TRACEHANDLE First = ~(TRACEHANDLE)0, Second = ~(TRACEHANDLE)0;
    ULONG Status;

    Status = RegisterTraceGuidsW(Callback, NULL, &ProviderGuid, 0, NULL, NULL, NULL, &First);
    ok_long(Status, ERROR_SUCCESS);
    if (Status != ERROR_SUCCESS) return;
    ok(First != ~(TRACEHANDLE)0 && First != 0, "Uninitialized provider handle %I64x\n", First);

    Status = RegisterTraceGuidsA(Callback, NULL, &ProviderGuid, 0, NULL, NULL, NULL, &Second);
    ok_long(Status, ERROR_SUCCESS);
    if (Status == ERROR_SUCCESS)
    {
        ok(Second != First, "Live registrations share a handle\n");
        Status = UnregisterTraceGuids(Second);
        ok_long(Status, ERROR_SUCCESS);
    }
    Status = UnregisterTraceGuids(First);
    ok_long(Status, ERROR_SUCCESS);
    Status = UnregisterTraceGuids(First);
    ok(Status != ERROR_SUCCESS, "Double unregistration succeeded\n");

    Status = RegisterTraceGuidsW(NULL, NULL, &ProviderGuid, 0, NULL, NULL, NULL, &First);
    ok_long(Status, ERROR_INVALID_PARAMETER);
    Status = RegisterTraceGuidsW(Callback, NULL, NULL, 0, NULL, NULL, NULL, &First);
    ok_long(Status, ERROR_INVALID_PARAMETER);
    Status = RegisterTraceGuidsW(Callback, NULL, &ProviderGuid, 0, NULL, NULL, NULL, NULL);
    ok_long(Status, ERROR_INVALID_PARAMETER);
}
