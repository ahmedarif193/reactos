#include <rtltests.h>
#include <reactos/rtl_critical.h>

#define CHECK_STATUS(Status) ok(NT_SUCCESS(Status), "status %#lx\n", Status)

static RTL_CRITICAL_SECTION RtlTestCriticalSection;
static ULONG_PTR ShutdownThreadId;

static
BOOLEAN
NTAPI
TestShutdownCallback(VOID)
{
    return ShutdownThreadId == HandleToULong(NtCurrentTeb()->ClientId.UniqueThread);
}

START_TEST(RtlCriticalSection)
{
    NTSTATUS Status;
    ULONG64 Start, Delta;

    Status = RtlInitializeCriticalSection(&RtlTestCriticalSection);
    CHECK_STATUS(Status);

    /* Simulate the critical section being owned by a foreign thread. */
    RtlTestCriticalSection.LockCount = 0;
    RtlTestCriticalSection.RecursionCount = 1;
    RtlTestCriticalSection.OwningThread = (HANDLE)(ULONG_PTR)0x12345678;

    ShutdownThreadId = HandleToULong(NtCurrentTeb()->ClientId.UniqueThread);
    RtlpSetCriticalSectionShutdownCallback(TestShutdownCallback);

    Start = GetTickCount64();
    Status = RtlEnterCriticalSection(&RtlTestCriticalSection);
    Delta = GetTickCount64() - Start;

    CHECK_STATUS(Status);
    ok(Delta < 100, "Bypass unexpectedly waited %I64u ms\n", Delta);
    ok(RtlTestCriticalSection.OwningThread == NtCurrentTeb()->ClientId.UniqueThread,
       "OwningThread not updated\n");
    ok(RtlTestCriticalSection.RecursionCount == 1,
       "RecursionCount %lu\n", RtlTestCriticalSection.RecursionCount);

    Status = RtlLeaveCriticalSection(&RtlTestCriticalSection);
    CHECK_STATUS(Status);

    RtlpSetCriticalSectionShutdownCallback(NULL);

    /* Reset structure before deletion to avoid stale owner state. */
    RtlTestCriticalSection.LockCount = -1;
    RtlTestCriticalSection.RecursionCount = 0;
    RtlTestCriticalSection.OwningThread = NULL;

    RtlDeleteCriticalSection(&RtlTestCriticalSection);
}
