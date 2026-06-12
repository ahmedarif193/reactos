/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite IPI and generic DPC call API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static volatile LONG IpiCalls;
static volatile LONG64 IpiMask;

static
ULONG_PTR
NTAPI
IpiBroadcastWorker(
    _In_ ULONG_PTR Argument)
{
    ok_eq_ulongptr(Argument, (ULONG_PTR)0x1BADD00D);
    InterlockedIncrement(&IpiCalls);
    InterlockedOr64(&IpiMask, (LONG64)((KAFFINITY)1 << KeGetCurrentProcessorNumberEx(NULL)));
    return 0x77;
}

static
VOID
TestIpiGenericCall(VOID)
{
    ULONG_PTR Result;
    ULONG Round;

    for (Round = 0; Round < 4; Round++)
    {
        IpiCalls = 0;
        IpiMask = 0;
        Result = KeIpiGenericCall(IpiBroadcastWorker, (ULONG_PTR)0x1BADD00D);
        ok_eq_ulongptr(Result, (ULONG_PTR)0x77);
        ok_eq_long(IpiCalls, (LONG)KeNumberProcessors);
        ok_eq_ulonglong((ULONGLONG)IpiMask, (ULONGLONG)KeQueryActiveProcessors());
    }
}

static volatile LONG DpcCalls;
static volatile LONG DpcSyncObserved;

static
VOID
NTAPI
GenericDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    ok_eq_pointer(DeferredContext, (PVOID)(ULONG_PTR)0xD00DFEED);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);

    InterlockedIncrement(&DpcCalls);
    if (KeSignalCallDpcSynchronize(SystemArgument2))
        InterlockedIncrement(&DpcSyncObserved);
    KeSignalCallDpcDone(SystemArgument1);
}

static
VOID
TestGenericCallDpc(VOID)
{
    DpcCalls = 0;
    DpcSyncObserved = 0;

    KeGenericCallDpc(GenericDpcRoutine, (PVOID)(ULONG_PTR)0xD00DFEED);

    ok_eq_long(DpcCalls, (LONG)KeNumberProcessors);
    ok_eq_long(DpcSyncObserved, 1L);

    KeFlushQueuedDpcs();
}

START_TEST(KeIpiKM)
{
    TestIpiGenericCall();
    TestGenericCallDpc();
}
