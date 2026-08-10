/*
 * PROJECT:     ReactOS New Device Installer Unit Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Batch driver-cache key and concurrency tests
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>

#include "driver_cache.h"

#define TEST_DEVICE_COUNT 12
#define TEST_WAITER_COUNT (TEST_DEVICE_COUNT - 1)

static const GUID ClassGuidA = {0x50127dc3, 0x0f36, 0x415e, {0xa6, 0xcc, 0x4c, 0xb3, 0xbe, 0x91, 0x0b, 0x65}};
static const GUID ClassGuidB = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR ProcessorIds[] = L"ACPI\\ACPI0007\0ACPI\\Processor\0";
static const WCHAR ProcessorIdsLower[] = L"acpi\\acpi0007\0acpi\\processor\0";
static const WCHAR ProcessorIdsReordered[] = L"ACPI\\Processor\0ACPI\\ACPI0007\0";
static const WCHAR ProcessorCompatibleIds[] = L"ACPI\\Processor\0*Processor\0";
static const WCHAR ProcessorCompatibleIdsLower[] = L"acpi\\processor\0*processor\0";
static const WCHAR ProcessorCompatibleIdsReordered[] = L"*Processor\0ACPI\\Processor\0";
static const WCHAR PowerIds[] = L"ACPI\\ACPI_PWR\0";
static const WCHAR ProcessorInf[] = L"C:\\ReactOS\\inf\\pnpcpu.inf";

typedef struct _SAME_KEY_CONTEXT
{
    HANDLE StartEvent;
    HANDLE AllBusyEvent;
    BOOL RecoverUnknown;
    volatile LONG BusyObserved;
    volatile LONG Failures;
    volatile LONG Discoveries;
    volatile LONG Completed;
} SAME_KEY_CONTEXT, *PSAME_KEY_CONTEXT;

typedef struct _PARALLEL_KEY_CONTEXT
{
    HANDLE StartEvent;
    HANDLE BothEnteredEvent;
    HANDLE ReleaseEvent;
    const GUID *ClassGuid;
    LPCWSTR HardwareIds;
    volatile LONG *Entered;
    volatile LONG *Failures;
} PARALLEL_KEY_CONTEXT, *PPARALLEL_KEY_CONTEXT;

static PNEWDEV_DRIVER_CACHE_ENTRY
AcquireEntryForIds(
    IN const GUID *ClassGuid,
    IN LPCWSTR HardwareIds,
    IN LPCWSTR CompatibleIds)
{
    PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry;
    NEWDEV_DRIVER_CACHE_ACQUIRE AcquireResult;

    for (;;)
    {
        AcquireResult = NewDevDriverCacheTryAcquire(ClassGuid, HardwareIds, CompatibleIds, &CacheEntry);
        if (AcquireResult == NewDevDriverCacheAcquired)
            return CacheEntry;
        if (AcquireResult != NewDevDriverCacheBusy || !NewDevDriverCacheWait(CacheEntry))
            return NULL;
    }
}

static PNEWDEV_DRIVER_CACHE_ENTRY
AcquireEntry(
    IN const GUID *ClassGuid,
    IN LPCWSTR HardwareIds)
{
    return AcquireEntryForIds(ClassGuid, HardwareIds, NULL);
}

static VOID
JoinThreads(
    IN HANDLE *Threads,
    IN DWORD ThreadCount,
    IN LPCSTR Description)
{
    DWORD WaitResult;

    if (ThreadCount != 0)
    {
        WaitResult = WaitForMultipleObjects(ThreadCount, Threads, TRUE, 10000);
        ok(WaitResult == WAIT_OBJECT_0, "%s workers timed out: 0x%lx\n", Description, WaitResult);
        if (WaitResult != WAIT_OBJECT_0)
            WaitForMultipleObjects(ThreadCount, Threads, TRUE, INFINITE);
    }

    while (ThreadCount != 0)
        CloseHandle(Threads[--ThreadCount]);
}

static DWORD WINAPI
SameKeyWaiterThread(
    IN PVOID Parameter)
{
    PSAME_KEY_CONTEXT Context = Parameter;
    PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry = NULL;
    NEWDEV_DRIVER_CACHE_ACQUIRE AcquireResult;
    NEWDEV_DRIVER_CACHE_RESULT CacheResult;
    WCHAR InfFileName[MAX_PATH] = L"";
    BOOL NullDriver = FALSE;
    BOOL CountedBusy = FALSE;

    if (WaitForSingleObject(Context->StartEvent, 5000) != WAIT_OBJECT_0)
    {
        InterlockedIncrement(&Context->Failures);
        return 0;
    }

    for (;;)
    {
        AcquireResult = NewDevDriverCacheTryAcquire(&ClassGuidA, ProcessorIds, NULL, &CacheEntry);
        if (AcquireResult == NewDevDriverCacheAcquired)
            break;
        if (AcquireResult != NewDevDriverCacheBusy)
        {
            InterlockedIncrement(&Context->Failures);
            return 0;
        }

        if (!CountedBusy)
        {
            CountedBusy = TRUE;
            if (InterlockedIncrement(&Context->BusyObserved) == TEST_WAITER_COUNT)
                SetEvent(Context->AllBusyEvent);
        }

        if (!NewDevDriverCacheWait(CacheEntry))
        {
            InterlockedIncrement(&Context->Failures);
            return 0;
        }
    }

    CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    if (Context->RecoverUnknown && CacheResult == NewDevDriverCacheUnknown)
    {
        InterlockedIncrement(&Context->Discoveries);
        NewDevDriverCacheRememberFound(CacheEntry, ProcessorInf, FALSE);
    }
    else if (CacheResult != NewDevDriverCacheFound || lstrcmpiW(InfFileName, ProcessorInf) || NullDriver)
    {
        InterlockedIncrement(&Context->Failures);
    }

    NewDevDriverCacheRelease(CacheEntry);
    InterlockedIncrement(&Context->Completed);
    return 0;
}

static DWORD WINAPI
DifferentKeyThread(
    IN PVOID Parameter)
{
    PPARALLEL_KEY_CONTEXT Context = Parameter;
    PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry;
    WCHAR InfFileName[MAX_PATH] = L"";
    BOOL NullDriver = FALSE;

    if (WaitForSingleObject(Context->StartEvent, 5000) != WAIT_OBJECT_0)
    {
        InterlockedIncrement(Context->Failures);
        return 0;
    }

    CacheEntry = AcquireEntry(Context->ClassGuid, Context->HardwareIds);
    if (!CacheEntry)
    {
        InterlockedIncrement(Context->Failures);
        return 0;
    }

    if (NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver) != NewDevDriverCacheUnknown)
        InterlockedIncrement(Context->Failures);

    if (InterlockedIncrement(Context->Entered) == 2)
        SetEvent(Context->BothEnteredEvent);

    if (WaitForSingleObject(Context->ReleaseEvent, 5000) != WAIT_OBJECT_0)
        InterlockedIncrement(Context->Failures);

    NewDevDriverCacheRememberFound(CacheEntry, ProcessorInf, FALSE);
    NewDevDriverCacheRelease(CacheEntry);
    return 0;
}

static VOID
RunSameKeyWaiterTest(
    IN BOOL RecoverUnknown)
{
    SAME_KEY_CONTEXT Context = {0};
    PNEWDEV_DRIVER_CACHE_ENTRY OwnerEntry;
    NEWDEV_DRIVER_CACHE_RESULT CacheResult;
    HANDLE Threads[TEST_WAITER_COUNT];
    WCHAR InfFileName[MAX_PATH] = L"";
    BOOL NullDriver = FALSE;
    DWORD WaitResult;
    DWORD ThreadCount = 0;
    DWORD i;

    Context.StartEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Context.AllBusyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Context.RecoverUnknown = RecoverUnknown;
    ok(Context.StartEvent && Context.AllBusyEvent, "event creation failed: %lu\n", GetLastError());
    if (!Context.StartEvent || !Context.AllBusyEvent)
        goto cleanup_handles;

    NewDevDriverCacheBeginBatch();
    OwnerEntry = AcquireEntry(&ClassGuidA, ProcessorIds);
    ok(OwnerEntry != NULL, "owner acquire failed: %lu\n", GetLastError());
    if (!OwnerEntry)
        goto cleanup_batch;

    CacheResult = NewDevDriverCacheGetResult(OwnerEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    ok_eq_int(CacheResult, NewDevDriverCacheUnknown);

    for (i = 0; i < TEST_WAITER_COUNT; i++)
    {
        Threads[ThreadCount] = CreateThread(NULL, 0, SameKeyWaiterThread, &Context, 0, NULL);
        ok(Threads[ThreadCount] != NULL, "CreateThread[%lu] failed: %lu\n", i, GetLastError());
        if (Threads[ThreadCount])
            ThreadCount++;
    }

    SetEvent(Context.StartEvent);
    WaitResult = WaitForSingleObject(Context.AllBusyEvent, 5000);
    ok(WaitResult == WAIT_OBJECT_0, "not every same-key waiter observed the busy owner: 0x%lx\n", WaitResult);

    if (!RecoverUnknown)
        NewDevDriverCacheRememberFound(OwnerEntry, ProcessorInf, FALSE);
    NewDevDriverCacheRelease(OwnerEntry);

    JoinThreads(Threads, ThreadCount, RecoverUnknown ? "recovery" : "same-key");
    ok_eq_long(Context.Failures, 0);
    ok_eq_long(Context.BusyObserved, ThreadCount);
    ok_eq_long(Context.Discoveries, RecoverUnknown ? 1 : 0);
    ok_eq_long(Context.Completed, ThreadCount);

cleanup_batch:
    NewDevDriverCacheEndBatch();
cleanup_handles:
    if (Context.AllBusyEvent)
        CloseHandle(Context.AllBusyEvent);
    if (Context.StartEvent)
        CloseHandle(Context.StartEvent);
}

static VOID
TestNotFoundConfirmation(VOID)
{
    PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry;
    NEWDEV_DRIVER_CACHE_RESULT CacheResult;
    WCHAR InfFileName[MAX_PATH] = L"";
    BOOL NullDriver = FALSE;

    NewDevDriverCacheBeginBatch();

    CacheEntry = AcquireEntry(&ClassGuidA, ProcessorIds);
    ok(CacheEntry != NULL, "initial acquire failed: %lu\n", GetLastError());
    if (!CacheEntry)
        goto cleanup;
    NewDevDriverCacheRememberNotFoundPending(CacheEntry);
    NewDevDriverCacheRelease(CacheEntry);

    CacheEntry = AcquireEntry(&ClassGuidA, ProcessorIdsLower);
    ok(CacheEntry != NULL, "pending acquire failed: %lu\n", GetLastError());
    if (!CacheEntry)
        goto cleanup;
    CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    ok_eq_int(CacheResult, NewDevDriverCacheNotFoundPending);
    NewDevDriverCacheInvalidate(CacheEntry);
    NewDevDriverCacheRememberNotFound(CacheEntry);
    NewDevDriverCacheRelease(CacheEntry);

    CacheEntry = AcquireEntry(&ClassGuidA, ProcessorIds);
    ok(CacheEntry != NULL, "confirmed acquire failed: %lu\n", GetLastError());
    if (!CacheEntry)
        goto cleanup;
    CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    ok_eq_int(CacheResult, NewDevDriverCacheNotFound);
    NewDevDriverCacheRelease(CacheEntry);

cleanup:
    NewDevDriverCacheEndBatch();
}

static VOID
TestKeySemantics(VOID)
{
    PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry;
    NEWDEV_DRIVER_CACHE_RESULT CacheResult;
    WCHAR InfFileName[MAX_PATH] = L"";
    BOOL NullDriver = FALSE;

    NewDevDriverCacheBeginBatch();

    CacheEntry = AcquireEntry(&ClassGuidA, ProcessorIds);
    ok(CacheEntry != NULL, "initial acquire failed\n");
    if (!CacheEntry)
        goto cleanup;
    NewDevDriverCacheRememberFound(CacheEntry, ProcessorInf, FALSE);
    NewDevDriverCacheRelease(CacheEntry);

    CacheEntry = AcquireEntry(&ClassGuidA, ProcessorIdsLower);
    ok(CacheEntry != NULL, "case-insensitive acquire failed\n");
    if (!CacheEntry)
        goto cleanup;
    CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    ok_eq_int(CacheResult, NewDevDriverCacheFound);
    NewDevDriverCacheRelease(CacheEntry);

    CacheEntry = AcquireEntry(&ClassGuidA, ProcessorIdsReordered);
    ok(CacheEntry != NULL, "reordered acquire failed\n");
    if (!CacheEntry)
        goto cleanup;
    CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    ok_eq_int(CacheResult, NewDevDriverCacheUnknown);
    NewDevDriverCacheRelease(CacheEntry);

    CacheEntry = AcquireEntry(&ClassGuidB, ProcessorIds);
    ok(CacheEntry != NULL, "different-class acquire failed\n");
    if (!CacheEntry)
        goto cleanup;
    CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    ok_eq_int(CacheResult, NewDevDriverCacheUnknown);
    NewDevDriverCacheRelease(CacheEntry);

    CacheEntry = AcquireEntryForIds(&ClassGuidA, ProcessorIds, ProcessorCompatibleIds);
    ok(CacheEntry != NULL, "compatible-ID acquire failed\n");
    if (!CacheEntry)
        goto cleanup;
    CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    ok_eq_int(CacheResult, NewDevDriverCacheUnknown);
    NewDevDriverCacheRememberFound(CacheEntry, ProcessorInf, FALSE);
    NewDevDriverCacheRelease(CacheEntry);

    CacheEntry = AcquireEntryForIds(&ClassGuidA, ProcessorIdsLower, ProcessorCompatibleIdsLower);
    ok(CacheEntry != NULL, "case-insensitive compatible-ID acquire failed\n");
    if (!CacheEntry)
        goto cleanup;
    CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    ok_eq_int(CacheResult, NewDevDriverCacheFound);
    NewDevDriverCacheRelease(CacheEntry);

    CacheEntry = AcquireEntryForIds(&ClassGuidA, ProcessorIds, ProcessorCompatibleIdsReordered);
    ok(CacheEntry != NULL, "reordered compatible-ID acquire failed\n");
    if (!CacheEntry)
        goto cleanup;
    CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    ok_eq_int(CacheResult, NewDevDriverCacheUnknown);
    NewDevDriverCacheRelease(CacheEntry);

cleanup:
    NewDevDriverCacheEndBatch();
}

static VOID
TestNestedBatchLifetime(VOID)
{
    PNEWDEV_DRIVER_CACHE_ENTRY CacheEntry;
    NEWDEV_DRIVER_CACHE_RESULT CacheResult;
    WCHAR InfFileName[MAX_PATH] = L"";
    BOOL NullDriver = FALSE;

    NewDevDriverCacheBeginBatch();
    CacheEntry = AcquireEntry(&ClassGuidA, ProcessorIds);
    ok(CacheEntry != NULL, "outer acquire failed\n");
    if (!CacheEntry)
        goto cleanup_outer;
    NewDevDriverCacheRememberFound(CacheEntry, ProcessorInf, FALSE);
    NewDevDriverCacheRelease(CacheEntry);

    NewDevDriverCacheBeginBatch();
    NewDevDriverCacheEndBatch();
    CacheEntry = AcquireEntry(&ClassGuidA, ProcessorIds);
    ok(CacheEntry != NULL, "nested lifetime acquire failed\n");
    if (!CacheEntry)
        goto cleanup_outer;
    CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
    ok_eq_int(CacheResult, NewDevDriverCacheFound);
    NewDevDriverCacheRelease(CacheEntry);

cleanup_outer:
    NewDevDriverCacheEndBatch();

    NewDevDriverCacheBeginBatch();
    CacheEntry = AcquireEntry(&ClassGuidA, ProcessorIds);
    ok(CacheEntry != NULL, "new batch acquire failed\n");
    if (CacheEntry)
    {
        CacheResult = NewDevDriverCacheGetResult(CacheEntry, InfFileName, ARRAYSIZE(InfFileName), &NullDriver);
        ok_eq_int(CacheResult, NewDevDriverCacheUnknown);
        NewDevDriverCacheRelease(CacheEntry);
    }
    NewDevDriverCacheEndBatch();
}

static VOID
TestDifferentKeysRunConcurrently(VOID)
{
    PARALLEL_KEY_CONTEXT Context[2] = {0};
    HANDLE Threads[2] = {NULL, NULL};
    HANDLE StartEvent;
    HANDLE BothEnteredEvent;
    HANDLE ReleaseEvent;
    volatile LONG Entered = 0;
    volatile LONG Failures = 0;
    DWORD WaitResult;
    DWORD ThreadCount = 0;
    DWORD i;

    StartEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BothEnteredEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ReleaseEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(StartEvent && BothEnteredEvent && ReleaseEvent, "event creation failed: %lu\n", GetLastError());
    if (!StartEvent || !BothEnteredEvent || !ReleaseEvent)
        goto cleanup_handles;

    NewDevDriverCacheBeginBatch();
    for (i = 0; i < 2; i++)
    {
        Context[i].StartEvent = StartEvent;
        Context[i].BothEnteredEvent = BothEnteredEvent;
        Context[i].ReleaseEvent = ReleaseEvent;
        Context[i].ClassGuid = &ClassGuidA;
        Context[i].HardwareIds = i ? PowerIds : ProcessorIds;
        Context[i].Entered = &Entered;
        Context[i].Failures = &Failures;
        Threads[ThreadCount] = CreateThread(NULL, 0, DifferentKeyThread, &Context[i], 0, NULL);
        ok(Threads[ThreadCount] != NULL, "CreateThread[%lu] failed: %lu\n", i, GetLastError());
        if (Threads[ThreadCount])
            ThreadCount++;
    }

    SetEvent(StartEvent);
    WaitResult = WaitForSingleObject(BothEnteredEvent, 5000);
    ok_eq_hex(WaitResult, WAIT_OBJECT_0);
    SetEvent(ReleaseEvent);
    JoinThreads(Threads, ThreadCount, "different-key");
    ok_eq_long(Failures, 0);
    ok_eq_long(Entered, ThreadCount);
    NewDevDriverCacheEndBatch();

cleanup_handles:
    if (ReleaseEvent)
        CloseHandle(ReleaseEvent);
    if (BothEnteredEvent)
        CloseHandle(BothEnteredEvent);
    if (StartEvent)
        CloseHandle(StartEvent);
}

START_TEST(BatchDriverCache)
{
    NewDevDriverCacheInitialize();
    RunSameKeyWaiterTest(FALSE);
    RunSameKeyWaiterTest(TRUE);
    TestNotFoundConfirmation();
    TestKeySemantics();
    TestNestedBatchLifetime();
    TestDifferentKeysRunConcurrently();
    NewDevDriverCacheUninitialize();
}
