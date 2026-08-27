/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Process-manager state used by modern graphics drivers
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

VOID
NTAPI
PsSetProcessFaultInformation(
    _Inout_ PEPROCESS Process,
    _In_ PVOID FaultInformation);

NTSTATUS
NTAPI
PsSetProcessesWindowState(
    _In_ ULONG WindowState,
    _In_opt_ PVOID Context);

static
VOID
TestProcessFaultState(VOID)
{
    BOOLEAN IsReactOS;
    UCHAR BeforeCounts;
    UCHAR ExpectedCounts;
    ULONG BeforeFlags;
    ULONG FaultInformation;
    ULONG FaultQuery;
    ULONG ReturnLength;
    NTSTATUS Status;

    IsReactOS = *(volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == 0x8eac705;
    if (IsReactOS)
    {
        BeforeCounts = PsGetCurrentProcess()->ProcessFaultCounts;
        BeforeFlags = PsGetCurrentProcess()->ProcessFaultFlags;
    }

    FaultQuery = 0xA5A5A5A5;
    ReturnLength = 0xA5A5A5A5;
    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessFaultInformation, &FaultQuery, sizeof(FaultQuery), &ReturnLength);
    trace("ProcessFaultInformation before returned 0x%08lx, length %lu, value 0x%08lx\n", Status, ReturnLength, FaultQuery);
    ok_eq_hex(Status, STATUS_INVALID_INFO_CLASS);

    FaultInformation = 0xF;
    PsSetProcessFaultInformation(PsGetCurrentProcess(), &FaultInformation);
    FaultQuery = 0xA5A5A5A5;
    ReturnLength = 0xA5A5A5A5;
    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessFaultInformation, &FaultQuery, sizeof(FaultQuery), &ReturnLength);
    trace("ProcessFaultInformation after returned 0x%08lx, length %lu, value 0x%08lx\n", Status, ReturnLength, FaultQuery);
    ok_eq_hex(Status, STATUS_INVALID_INFO_CLASS);

    if (skip(IsReactOS, "native EPROCESS fields are intentionally opaque\n"))
        return;

    ExpectedCounts = BeforeCounts | 0x40;
    if ((BeforeCounts & 0x7) != 0x7)
        ExpectedCounts = (ExpectedCounts & ~0x7) | ((BeforeCounts & 0x7) + 1);
    if (((BeforeCounts >> 3) & 0x7) != 0x7)
        ExpectedCounts = (ExpectedCounts & ~0x38) | ((((BeforeCounts >> 3) & 0x7) + 1) << 3);
    ok_eq_uint(PsGetCurrentProcess()->ProcessFaultCounts, ExpectedCounts);
    ok_eq_hex(PsGetCurrentProcess()->ProcessFaultFlags, BeforeFlags | 0x4);

    FaultInformation = 0x6;
    for (ReturnLength = 0; ReturnLength != 8; ReturnLength++)
        PsSetProcessFaultInformation(PsGetCurrentProcess(), &FaultInformation);
    ok_eq_uint(PsGetCurrentProcess()->ProcessFaultCounts & 0x7F, 0x7F);
}

static
VOID
TestProcessWindowState(VOID)
{
    PVOID Context;
    NTSTATUS Status;

    if (skip(*(volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == 0x8eac705,
             "native window-state provider can block an isolated caller\n"))
    {
        return;
    }

    Status = PsSetProcessesWindowState(0, NULL);
    trace("PsSetProcessesWindowState(0) returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(PsGetCurrentProcess()->ProcessWindowState, 0);
    ok(PsGetCurrentProcess()->ProcessWindowStateContext == NULL,
       "expected a NULL window-state context\n");

    Context = (PVOID)(ULONG_PTR)0x12345678;
    Status = PsSetProcessesWindowState(0xA5, Context);
    trace("PsSetProcessesWindowState(0xA5) returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(PsGetCurrentProcess()->ProcessWindowState, 0xA5);
    ok(PsGetCurrentProcess()->ProcessWindowStateContext == Context,
       "expected the supplied window-state context\n");
}

START_TEST(PsWddmFault)
{
    TestProcessFaultState();
}

START_TEST(PsWddmWindow)
{
    TestProcessWindowState();
}
