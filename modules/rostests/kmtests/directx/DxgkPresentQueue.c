/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgkrnl stable circular present-queue removal tests
 */

#include <kmt_test.h>
#include "present_contract_core.h"
#include "present_dma_core.h"
#include "present_queue_core.h"

#define DXGK_PRESENT_QUEUE_TEST_CAPACITY 5

typedef struct _DXGK_PRESENT_QUEUE_TEST_ENTRY
{
    ULONG Id;
    PVOID Device;
    PVOID Context;
} DXGK_PRESENT_QUEUE_TEST_ENTRY, *PDXGK_PRESENT_QUEUE_TEST_ENTRY;

typedef struct _DXGK_PRESENT_QUEUE_TEST_QUEUE
{
    KSPIN_LOCK Lock;
    DXGK_PRESENT_QUEUE_TEST_ENTRY Entries[DXGK_PRESENT_QUEUE_TEST_CAPACITY];
    ULONG Head;
    ULONG Tail;
    ULONG Count;
    KIRQL BaseIrql;
    volatile LONG MatchOutsideLock;
    volatile LONG ReleaseInsideLock;
    ULONG ReleasedIds[DXGK_PRESENT_QUEUE_TEST_CAPACITY];
    ULONG ReleaseCount;
} DXGK_PRESENT_QUEUE_TEST_QUEUE, *PDXGK_PRESENT_QUEUE_TEST_QUEUE;

typedef struct _DXGK_PRESENT_QUEUE_TEST_MATCH
{
    PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue;
    PVOID Device;
    PVOID Context;
} DXGK_PRESENT_QUEUE_TEST_MATCH, *PDXGK_PRESENT_QUEUE_TEST_MATCH;

#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
static VOID
DxgkPresentQueueTestRefreshTargets(VOID)
{
    ok_bool_true(DxgkPresentCoreRefreshTargetReached(17, 17),
                 "equal refresh target is reached");
    ok_bool_true(DxgkPresentCoreRefreshTargetReached(18, 17),
                 "past refresh target is reached");
    ok_bool_false(DxgkPresentCoreRefreshTargetReached(16, 17),
                  "future refresh target remains pending");

    ok_bool_true(DxgkPresentCoreRefreshTargetReached(0, MAXULONG),
                 "serial comparison crosses wrap for the preceding target");
    ok_bool_false(DxgkPresentCoreRefreshTargetReached(MAXULONG, 0),
                  "serial comparison keeps the post-wrap target pending");
    ok_bool_true(DxgkPresentCoreRefreshTargetReached(1, MAXULONG),
                 "serial comparison remains reached after wrap");

    ok_bool_false(
        DxgkPresentCoreRefreshTargetShouldSignalImmediately(0, 0),
        "zero target waits for the next vblank at startup");
    ok_bool_false(
        DxgkPresentCoreRefreshTargetShouldSignalImmediately(42, 0),
        "zero target always waits for the next vblank");
    ok_bool_true(
        DxgkPresentCoreRefreshTargetShouldSignalImmediately(42, 41),
        "passed nonzero target signals immediately");
    ok_bool_true(
        DxgkPresentCoreRefreshTargetShouldSignalImmediately(42, 42),
        "equal nonzero target signals immediately");
    ok_bool_false(
        DxgkPresentCoreRefreshTargetShouldSignalImmediately(42, 43),
        "future nonzero target remains pending");
}
#endif

static VOID
DxgkPresentQueueTestDmaGeometry(VOID)
{
    DXGK_PRESENT_DMA_GEOMETRY Geometry;
    UCHAR GuardedPrivateData[66];
    UCHAR GuardedAllocationList[26];
    SIZE_T AllocationListBytes;
    NTSTATUS Status;
    ULONG Index;

    Status = DxgkPresentDmaCoreSelectGeometry(
                 TRUE, 8192, 0, 64, 256, 256, 64, &Geometry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Geometry.DmaBufferSize, 8192);
    ok_eq_ulong(Geometry.DmaBufferPrivateDataSize, 64);
    ok_eq_ulong(Geometry.AllocationListSize, 256);
    ok_eq_ulong(Geometry.PatchLocationListSize, 64);
    ok_bool_true(Geometry.Declared, "context geometry remains declared");

    RtlFillMemory(GuardedPrivateData, sizeof(GuardedPrivateData), 0xa5);
    Status = DxgkPresentDmaCoreInitializePrivateData(
                 &GuardedPrivateData[1], 64);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(GuardedPrivateData[0], 0xa5);
    ok_eq_ulong(GuardedPrivateData[65], 0xa5);
    for (Index = 1; Index <= 64; ++Index)
        ok_eq_ulong(GuardedPrivateData[Index], 0);

    Status = DxgkPresentDmaCoreAllocationListBytes(
                 DXGK_PRESENT_DMA_MIN_ALLOCATIONS, 8, &AllocationListBytes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_size(AllocationListBytes, 24);
    RtlFillMemory(GuardedAllocationList, sizeof(GuardedAllocationList), 0x5a);
    Status = DxgkPresentDmaCoreInitializeAllocationList(
                 &GuardedAllocationList[1], AllocationListBytes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(GuardedAllocationList[0], 0x5a);
    ok_eq_ulong(GuardedAllocationList[25], 0x5a);
    for (Index = 1; Index <= 24; ++Index)
        ok_eq_ulong(GuardedAllocationList[Index], 0);

    Status = DxgkPresentDmaCoreSelectGeometry(
                 TRUE, 8192, 0, 0, 3, 2, 64, &Geometry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Geometry.DmaBufferPrivateDataSize, 0);
    Status = DxgkPresentDmaCoreInitializePrivateData(NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkPresentDmaCoreInitializePrivateData(
                 &GuardedPrivateData[1], 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = DxgkPresentDmaCoreSelectGeometry(
                 FALSE, 0, 0, 0, 0, 0, 64, &Geometry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Geometry.DmaBufferSize, DXGK_PRESENT_DMA_COMPAT_BYTES);
    ok_eq_ulong(Geometry.DmaBufferPrivateDataSize, 0);
    ok_bool_false(Geometry.Declared, "missing legacy pInfo uses compatibility geometry");

    Status = DxgkPresentDmaCoreSelectGeometry(
                 TRUE, 0, 0, 0, 3, 2, 64, &Geometry);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkPresentDmaCoreSelectGeometry(
                 TRUE, DXGK_PRESENT_DMA_MAX_BYTES + 1, 0, 0, 3, 2,
                 64, &Geometry);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkPresentDmaCoreSelectGeometry(
                 TRUE, 8192, 0, DXGK_PRESENT_DMA_MAX_PRIVATE_BYTES + 1,
                 3, 2, 64, &Geometry);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkPresentDmaCoreSelectGeometry(
                 TRUE, 8192, 1, 0, 3, 2, 64, &Geometry);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    Status = DxgkPresentDmaCoreSelectGeometry(
                 TRUE, 8192, 0, 0,
                 DXGK_PRESENT_DMA_MAX_ALLOCATIONS + 1, 2, 64, &Geometry);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkPresentDmaCoreAllocationListBytes(
                 DXGK_PRESENT_DMA_MIN_ALLOCATIONS,
                 (SIZE_T)MAXULONG_PTR,
                 &AllocationListBytes);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}

static VOID
DxgkPresentQueueTestContract(VOID)
{
    static const RECT SourceSubRects[] =
    {
        { 10, 20, 35, 45 },
        { 60, 70, 110, 120 }
    };
    RECT SourceRect = { 10, 20, 110, 120 };
    RECT DestinationRect = { 200, 300, 400, 500 };
    RECT DestinationSubRects[RTL_NUMBER_OF(SourceSubRects)];
    RECT FractionalSourceRect = { 0, 0, 3, 3 };
    RECT FractionalDestinationRect = { 0, 0, 10, 10 };
    RECT FractionalSubRect = { 0, 0, 1, 1 };
    RECT InvalidSubRect = { 9, 20, 35, 45 };
    DXGK_PRESENT_SCANOUT_RETIRE_POLICY RetirePolicy;
    UINT OverlayHandle;
    NTSTATUS Status;

    RtlZeroMemory(DestinationSubRects, sizeof(DestinationSubRects));
    Status = DxgkPresentCoreMapSourceSubRects(
                 &SourceRect,
                 &DestinationRect,
                 SourceSubRects,
                 RTL_NUMBER_OF(SourceSubRects),
                 DestinationSubRects,
                 RTL_NUMBER_OF(DestinationSubRects));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(DestinationSubRects[0].left, 200);
    ok_eq_long(DestinationSubRects[0].top, 300);
    ok_eq_long(DestinationSubRects[0].right, 250);
    ok_eq_long(DestinationSubRects[0].bottom, 350);
    ok_eq_long(DestinationSubRects[1].left, 300);
    ok_eq_long(DestinationSubRects[1].top, 400);
    ok_eq_long(DestinationSubRects[1].right, 400);
    ok_eq_long(DestinationSubRects[1].bottom, 500);

    Status = DxgkPresentCoreMapSourceSubRects(
                 &FractionalSourceRect,
                 &FractionalDestinationRect,
                 &FractionalSubRect,
                 1,
                 DestinationSubRects,
                 RTL_NUMBER_OF(DestinationSubRects));
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);

    Status = DxgkPresentCoreMapSourceSubRects(
                 &SourceRect,
                 &DestinationRect,
                 &InvalidSubRect,
                 1,
                 DestinationSubRects,
                 RTL_NUMBER_OF(DestinationSubRects));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = DxgkPresentCoreMapSourceSubRects(
                 &SourceRect,
                 &DestinationRect,
                 SourceSubRects,
                 RTL_NUMBER_OF(SourceSubRects),
                 DestinationSubRects,
                 1);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);

    DestinationSubRects[0] = DestinationRect;
    Status = DxgkPresentCoreCopyDestinationSubRects(
                 &DestinationRect,
                 DestinationSubRects,
                 1,
                 DestinationSubRects,
                 1);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ok_bool_true(DxgkPresentCoreIsFullDestinationRegion(
                     &DestinationRect,
                     NULL,
                     0),
                 "implicit full destination");
    ok_bool_true(DxgkPresentCoreIsFullDestinationRegion(
                     &DestinationRect,
                     DestinationSubRects,
                     1),
                 "single explicit full destination");
    DestinationSubRects[0].right--;
    ok_bool_false(DxgkPresentCoreIsFullDestinationRegion(
                      &DestinationRect,
                      DestinationSubRects,
                      1),
                  "partial destination is not widened");

    OverlayHandle = 0x12345678;
    Status = DxgkPresentCoreCompleteUnsupportedOverlayCreate(
                 STATUS_SUCCESS,
                 &OverlayHandle);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    ok_eq_ulong(OverlayHandle, 0);
    OverlayHandle = 0x12345678;
    Status = DxgkPresentCoreCompleteUnsupportedOverlayCreate(
                 STATUS_INVALID_HANDLE,
                 &OverlayHandle);
    ok_eq_hex(Status, STATUS_INVALID_HANDLE);
    ok_eq_ulong(OverlayHandle, 0);

    ok_bool_false(DxgkPresentCoreMpoV1Supported(
                      1300, TRUE, TRUE, FALSE, FALSE),
                  "internal miniport MPO is not a KMT MPO path");
    ok_bool_false(DxgkPresentCoreMpoV1Supported(
                      1200, TRUE, TRUE, TRUE, TRUE),
                  "MPO v1 query is unavailable below WDDM 1.3");
    ok_bool_true(DxgkPresentCoreMpoV1Supported(
                     1300, TRUE, TRUE, TRUE, TRUE),
                 "complete WDDM 1.3 MPO path");

    Status = DxgkPresentCoreEvaluateScanoutRetirement(
                 TRUE, FALSE, TRUE, FALSE, FALSE, &RetirePolicy);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_false(RetirePolicy.RefreshSharedPrimary,
                  "flip does not refresh a destination");
    ok_bool_true(RetirePolicy.ProgramSource,
                 "flip programs its source after retirement");
    ok_bool_true(RetirePolicy.HoldSharedSurfaceRundown,
                 "flip keeps the VidPN generation stable");

    Status = DxgkPresentCoreEvaluateScanoutRetirement(
                 FALSE, TRUE, TRUE, TRUE, TRUE, &RetirePolicy);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(RetirePolicy.RefreshSharedPrimary,
                 "blit or fill refreshes a shared-primary destination");
    ok_bool_false(RetirePolicy.ProgramSource,
                  "destination write does not flip its source");
    ok_bool_true(RetirePolicy.HoldSharedSurfaceRundown,
                 "shared-primary refresh keeps its generation stable");

    Status = DxgkPresentCoreEvaluateScanoutRetirement(
                 FALSE, TRUE, TRUE, TRUE, FALSE, &RetirePolicy);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_false(RetirePolicy.RefreshSharedPrimary,
                  "ordinary destination needs no scanout refresh");
    ok_bool_false(RetirePolicy.ProgramSource,
                  "ordinary destination needs no source flip");
    ok_bool_false(RetirePolicy.HoldSharedSurfaceRundown,
                  "ordinary destination holds no shared-surface rundown");

    Status = DxgkPresentCoreEvaluateScanoutRetirement(
                 TRUE, FALSE, FALSE, FALSE, FALSE, &RetirePolicy);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkPresentCoreEvaluateScanoutRetirement(
                 TRUE, FALSE, TRUE, TRUE, FALSE, &RetirePolicy);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}

static BOOLEAN NTAPI DxgkPresentQueueTestMatch(_In_ const VOID *OpaqueEntry, _In_opt_ PVOID OpaqueContext)
{
    const DXGK_PRESENT_QUEUE_TEST_ENTRY *Entry = OpaqueEntry;
    PDXGK_PRESENT_QUEUE_TEST_MATCH Match = OpaqueContext;

    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        InterlockedIncrement(&Match->Queue->MatchOutsideLock);
    return !((Match->Device != NULL && Entry->Device != Match->Device) || (Match->Context != NULL && Entry->Context != Match->Context));
}

static DXGK_PRESENT_QUEUE_TEST_ENTRY DxgkPresentQueueTestEntry(_In_ ULONG Id, _In_ PVOID Device, _In_ PVOID Context)
{
    DXGK_PRESENT_QUEUE_TEST_ENTRY Entry;

    RtlZeroMemory(&Entry, sizeof(Entry));
    Entry.Id = Id;
    Entry.Device = Device;
    Entry.Context = Context;
    return Entry;
}

static VOID DxgkPresentQueueTestInitialize(_Out_ PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue)
{
    RtlZeroMemory(Queue, sizeof(*Queue));
    KeInitializeSpinLock(&Queue->Lock);
    Queue->BaseIrql = KeGetCurrentIrql();
}

static VOID DxgkPresentQueueTestEnqueue(_Inout_ PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue, _In_ const DXGK_PRESENT_QUEUE_TEST_ENTRY *Entry)
{
    Queue->Entries[Queue->Tail] = *Entry;
    Queue->Tail = (Queue->Tail + 1) % DXGK_PRESENT_QUEUE_TEST_CAPACITY;
    Queue->Count++;
}

static DXGK_PRESENT_QUEUE_TEST_ENTRY DxgkPresentQueueTestPopFront(_Inout_ PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue)
{
    DXGK_PRESENT_QUEUE_TEST_ENTRY Entry = Queue->Entries[Queue->Head];

    RtlZeroMemory(&Queue->Entries[Queue->Head], sizeof(Queue->Entries[Queue->Head]));
    Queue->Head = (Queue->Head + 1) % DXGK_PRESENT_QUEUE_TEST_CAPACITY;
    Queue->Count--;
    return Entry;
}

static VOID DxgkPresentQueueTestRelease(_Inout_ PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue, _In_ const DXGK_PRESENT_QUEUE_TEST_ENTRY *Entry)
{
    if (KeGetCurrentIrql() != Queue->BaseIrql)
        InterlockedIncrement(&Queue->ReleaseInsideLock);
    Queue->ReleasedIds[Queue->ReleaseCount] = Entry->Id;
    Queue->ReleaseCount++;
}

static BOOLEAN DxgkPresentQueueTestRemove(_Inout_ PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue, _In_opt_ PVOID Device, _In_opt_ PVOID Context)
{
    DXGK_PRESENT_QUEUE_TEST_ENTRY RemovedEntry;
    DXGK_PRESENT_QUEUE_TEST_MATCH Match;
    BOOLEAN Removed;

    RtlZeroMemory(&RemovedEntry, sizeof(RemovedEntry));
    Match.Queue = Queue;
    Match.Device = Device;
    Match.Context = Context;
    Removed = DxgkPresentQueueCoreRemove(&Queue->Lock, Queue->Entries, sizeof(Queue->Entries[0]), DXGK_PRESENT_QUEUE_TEST_CAPACITY, &Queue->Head, &Queue->Tail, &Queue->Count, DxgkPresentQueueTestMatch, &Match, &RemovedEntry);
    if (Removed)
        DxgkPresentQueueTestRelease(Queue, &RemovedEntry);
    return Removed;
}

static ULONG DxgkPresentQueueTestLogicalId(_In_ const DXGK_PRESENT_QUEUE_TEST_QUEUE *Queue, _In_ ULONG Offset)
{
    return Queue->Entries[(Queue->Head + Offset) % DXGK_PRESENT_QUEUE_TEST_CAPACITY].Id;
}

static VOID DxgkPresentQueueTestLimits(VOID)
{
    DXGK_PRESENT_LIMIT_CORE State;

    DxgkPresentLimitCoreInitialize(&State, 3);
    ok_eq_ulong(DxgkPresentLimitCoreGetLimit(&State), 3);
    ok_eq_ulong(DxgkPresentLimitCoreGetReserved(&State), 0);
    ok_bool_false(DxgkPresentLimitCoreIsReached(&State), "empty default limit is not reached");
    ok_bool_true(DxgkPresentLimitCoreTryReserve(&State), "first reservation succeeds");
    ok_bool_true(DxgkPresentLimitCoreTryReserve(&State), "second reservation succeeds");
    ok_bool_true(DxgkPresentLimitCoreTryReserve(&State), "third reservation succeeds");
    ok_bool_true(DxgkPresentLimitCoreIsReached(&State), "third reservation reaches default limit");
    ok_bool_false(DxgkPresentLimitCoreTryReserve(&State), "fourth reservation is rejected");
    ok_eq_ulong(DxgkPresentLimitCoreGetReserved(&State), 3);
    { NTSTATUS Observed = DxgkPresentLimitCoreSet(&State, 2, 3, 16); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(DxgkPresentLimitCoreGetLimit(&State), 2);
    ok_bool_true(DxgkPresentLimitCoreIsReached(&State), "lowering below current reservations remains reached");
    DxgkPresentLimitCoreRelease(&State);
    ok_bool_true(DxgkPresentLimitCoreIsReached(&State), "count equal to lowered limit remains reached");
    DxgkPresentLimitCoreRelease(&State);
    ok_bool_false(DxgkPresentLimitCoreIsReached(&State), "release below limit reopens admission");
    ok_bool_true(DxgkPresentLimitCoreTryReserve(&State), "admission resumes below the lowered limit");
    { NTSTATUS Observed = DxgkPresentLimitCoreSet(&State, 0, 3, 16); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(DxgkPresentLimitCoreGetLimit(&State), 3);
    { NTSTATUS Observed = DxgkPresentLimitCoreSet(&State, 17, 3, 16); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulong(DxgkPresentLimitCoreGetLimit(&State), 3);
    DxgkPresentLimitCoreRelease(&State);
    DxgkPresentLimitCoreRelease(&State);
    ok_eq_ulong(DxgkPresentLimitCoreGetReserved(&State), 0);
}

START_TEST(DxgkPresentQueue)
{
    PVOID DeviceA = (PVOID)(ULONG_PTR)0x1000;
    PVOID DeviceB = (PVOID)(ULONG_PTR)0x2000;
    PVOID DeviceC = (PVOID)(ULONG_PTR)0x3000;
    PVOID DeviceD = (PVOID)(ULONG_PTR)0x4000;
    PVOID ContextX = (PVOID)(ULONG_PTR)0x5000;
    PVOID ContextY = (PVOID)(ULONG_PTR)0x6000;
    PVOID ContextZ = (PVOID)(ULONG_PTR)0x7000;
    DXGK_PRESENT_QUEUE_TEST_QUEUE Queue;
    DXGK_PRESENT_QUEUE_TEST_ENTRY Entry;
    ULONG Index;

#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
    DxgkPresentQueueTestRefreshTargets();
#endif
    DxgkPresentQueueTestDmaGeometry();
    DxgkPresentQueueTestContract();
    DxgkPresentQueueTestLimits();

    DxgkPresentQueueTestInitialize(&Queue);
    Entry = DxgkPresentQueueTestEntry(1, DeviceA, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(2, DeviceB, ContextY);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(3, DeviceA, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(4, DeviceB, ContextY);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestPopFront(&Queue);
    ok_eq_ulong(Entry.Id, 1);
    Entry = DxgkPresentQueueTestPopFront(&Queue);
    ok_eq_ulong(Entry.Id, 2);
    Entry = DxgkPresentQueueTestEntry(5, DeviceC, ContextZ);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(6, DeviceD, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);

    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 1);
    ok_eq_ulong(Queue.Count, 4);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 0), 3);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 1), 4);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 2), 5);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 3), 6);

    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, DeviceC, NULL), "remove wrapped middle entry by device");
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 0);
    ok_eq_ulong(Queue.Count, 3);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 0), 3);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 1), 4);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 2), 6);
    ok_eq_ulong(Queue.ReleasedIds[0], 5);

    ok_bool_false(DxgkPresentQueueTestRemove(&Queue, DeviceA, ContextY), "combined device/context mismatch must not remove");
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 0);
    ok_eq_ulong(Queue.Count, 3);

    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, ContextX), "remove first matching context while preserving order");
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 4);
    ok_eq_ulong(Queue.Count, 2);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 0), 4);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 1), 6);
    ok_eq_ulong(Queue.ReleasedIds[1], 3);

    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, DeviceB, ContextY), "remove exact device/context match");
    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "remove final remaining entry");
    ok_bool_false(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "empty queue removal must fail");
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 2);
    ok_eq_ulong(Queue.Count, 0);
    ok_eq_ulong(Queue.ReleaseCount, 4);
    ok_eq_ulong(Queue.ReleasedIds[0], 5);
    ok_eq_ulong(Queue.ReleasedIds[1], 3);
    ok_eq_ulong(Queue.ReleasedIds[2], 4);
    ok_eq_ulong(Queue.ReleasedIds[3], 6);
    ok_eq_long(InterlockedCompareExchange(&Queue.MatchOutsideLock, 0, 0), 0);
    ok_eq_long(InterlockedCompareExchange(&Queue.ReleaseInsideLock, 0, 0), 0);
    for (Index = 0; Index < DXGK_PRESENT_QUEUE_TEST_CAPACITY; ++Index)
    {
        ok_eq_ulong(Queue.Entries[Index].Id, 0);
        ok_eq_pointer(Queue.Entries[Index].Device, NULL);
        ok_eq_pointer(Queue.Entries[Index].Context, NULL);
    }

    DxgkPresentQueueTestInitialize(&Queue);
    Entry = DxgkPresentQueueTestEntry(11, DeviceA, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(12, DeviceB, ContextY);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(13, DeviceA, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(14, DeviceB, ContextY);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestPopFront(&Queue);
    ok_eq_ulong(Entry.Id, 11);
    Entry = DxgkPresentQueueTestPopFront(&Queue);
    ok_eq_ulong(Entry.Id, 12);
    Entry = DxgkPresentQueueTestEntry(15, DeviceC, ContextZ);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(16, DeviceD, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 1);
    ok_eq_ulong(Queue.Count, 4);
    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "cancel-all removes wrapped head 13");
    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "cancel-all removes wrapped head 14");
    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "cancel-all removes wrapped head 15");
    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "cancel-all removes wrapped head 16");
    ok_eq_ulong(Queue.ReleaseCount, 4);
    ok_eq_ulong(Queue.ReleasedIds[0], 13);
    ok_eq_ulong(Queue.ReleasedIds[1], 14);
    ok_eq_ulong(Queue.ReleasedIds[2], 15);
    ok_eq_ulong(Queue.ReleasedIds[3], 16);
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 2);
    ok_eq_ulong(Queue.Count, 0);
    ok_eq_long(InterlockedCompareExchange(&Queue.MatchOutsideLock, 0, 0), 0);
    ok_eq_long(InterlockedCompareExchange(&Queue.ReleaseInsideLock, 0, 0), 0);
}
