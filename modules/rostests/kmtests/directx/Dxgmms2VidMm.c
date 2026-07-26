/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgmms2 video memory segment ownership contract tests
 *
 * dxgmms2 owns segment space: the commit ledger, the virgin-space cursor, and
 * the placement decision.  dxgkrnl asks for an offset and reports when a
 * placement is gone; it keeps no second copy of occupancy.  These tests pin
 * the allocator's rules without a graphics stack behind them.
 */

#include <kmt_test.h>
#include <reactos/drivers/directx/dxgmms2.h>
#include "vidmm_core.h"

#define TAG_DXGMMS2_VIDMM_TEST 'mV2D'
#define DXGMMS2_VIDMM_TEST_SEGMENTS 2
#define DXGMMS2_VIDMM_TEST_POOL 8
#define DXGMMS2_VIDMM_TEST_SEGMENT_SIZE 0x100000ULL
#define DXGMMS2_VIDMM_TEST_PAGE 0x1000ULL
#define DXGMMS2_VIDMM_TEST_BATCH 32

typedef struct _DXGMMS2_VIDMM_TEST_STATE
{
    DXGMMS2_VIDMM_CORE Core;
    DXGMMS2_VIDMM_RANGE Pool[DXGMMS2_VIDMM_TEST_POOL];
} DXGMMS2_VIDMM_TEST_STATE, *PDXGMMS2_VIDMM_TEST_STATE;

static VOID
InitSegmentDesc(
    _Out_ DXGMMS2_VIDMM_SEGMENT_DESC_V1 *Desc,
    _In_ ULONGLONG Size,
    _In_ ULONGLONG CommitLimit,
    _In_ ULONG Flags)
{
    RtlZeroMemory(Desc, sizeof(*Desc));
    Desc->Size = Size;
    Desc->CommitLimit = CommitLimit;
    Desc->Flags = Flags;
}

static VOID
InitReserveInfo(
    _Out_ DXGMMS2_VIDMM_RESERVE_INFO_V1 *Info,
    _In_ ULONGLONG Size,
    _In_ ULONGLONG Alignment,
    _In_ ULONGLONG OwnerCookie)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Size = Size;
    Info->Alignment = Alignment;
    Info->OwnerCookie = OwnerCookie;
}

static VOID
InitSegmentStatus(
    _Out_ DXGMMS2_VIDMM_SEGMENT_STATUS_V1 *Status)
{
    RtlZeroMemory(Status, sizeof(*Status));
}

static NTSTATUS
ReserveSimple(
    _Inout_ PDXGMMS2_VIDMM_TEST_STATE State,
    _In_ ULONG SegmentIndex,
    _In_ ULONGLONG Size,
    _In_ ULONGLONG OwnerCookie,
    _Out_ PULONGLONG OutOffset)
{
    DXGMMS2_VIDMM_RESERVE_INFO_V1 Info;

    InitReserveInfo(&Info, Size, DXGMMS2_VIDMM_TEST_PAGE, OwnerCookie);
    return Dxgmms2VidMmCoreReserve(&State->Core, SegmentIndex, &Info, OutOffset);
}

static ULONGLONG
SegmentUsedSize(
    _Inout_ PDXGMMS2_VIDMM_TEST_STATE State,
    _In_ ULONG SegmentIndex)
{
    DXGMMS2_VIDMM_SEGMENT_STATUS_V1 Status;

    InitSegmentStatus(&Status);
    if (!NT_SUCCESS(Dxgmms2VidMmCoreQuerySegment(&State->Core, SegmentIndex, &Status)))
        return MAXULONGLONG;
    return Status.UsedSize;
}

static VOID
TestStartAndSegmentDescription(
    _Inout_ PDXGMMS2_VIDMM_TEST_STATE State)
{
    DXGMMS2_VIDMM_SEGMENT_DESC_V1 Desc;
    DXGMMS2_VIDMM_SEGMENT_STATUS_V1 Status;
    ULONGLONG Offset = 0;
    NTSTATUS NtStatus;

    Dxgmms2VidMmCoreInitialize(&State->Core, State->Pool, RTL_NUMBER_OF(State->Pool));

    /* Placement before start is refused rather than silently accepted. */
    NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 1, &Offset);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);

    NtStatus = Dxgmms2VidMmCoreStart(&State->Core, 0);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);
    NtStatus = Dxgmms2VidMmCoreStart(&State->Core, DXGMMS2_VIDMM_MAX_SEGMENTS + 1);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);

    NtStatus = Dxgmms2VidMmCoreStart(&State->Core, DXGMMS2_VIDMM_TEST_SEGMENTS);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    NtStatus = Dxgmms2VidMmCoreStart(&State->Core, DXGMMS2_VIDMM_TEST_SEGMENTS);
    ok_eq_hex(NtStatus, STATUS_INVALID_DEVICE_STATE);

    /* A described-but-not-yet-valid segment cannot take a placement. */
    NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 1, &Offset);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);

    InitSegmentDesc(&Desc, 0, 0, 0);
    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, 0, &Desc);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);

    /* A commit limit above the segment is a miniport description error. */
    InitSegmentDesc(&Desc, DXGMMS2_VIDMM_TEST_SEGMENT_SIZE, DXGMMS2_VIDMM_TEST_SEGMENT_SIZE * 2, 0);
    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, 0, &Desc);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);

    InitSegmentDesc(&Desc, DXGMMS2_VIDMM_TEST_SEGMENT_SIZE, 0, 0);
    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, 0, &Desc);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);

    InitSegmentDesc(&Desc, DXGMMS2_VIDMM_TEST_SEGMENT_SIZE, 0, DXGMMS2_VIDMM_SEGMENT_APERTURE);
    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, 1, &Desc);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);

    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, DXGMMS2_VIDMM_TEST_SEGMENTS, &Desc);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);

    InitSegmentStatus(&Status);
    NtStatus = Dxgmms2VidMmCoreQuerySegment(&State->Core, 1, &Status);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(Status.Size, DXGMMS2_VIDMM_TEST_SEGMENT_SIZE);
    /* A zero commit limit means the whole segment. */
    ok_eq_ulonglong(Status.CommitLimit, DXGMMS2_VIDMM_TEST_SEGMENT_SIZE);
    ok_eq_ulonglong(Status.UsedSize, 0ULL);
    ok_eq_ulong(Status.Flags, (ULONG)DXGMMS2_VIDMM_SEGMENT_APERTURE);
}

/*
 * Virgin space first: the high-water cursor never hands back a retired offset,
 * which some GPUs require.  First-fit over the gaps is the fallback once the
 * tail is exhausted.
 */
static VOID
TestVirginSpaceThenFirstFit(
    _Inout_ PDXGMMS2_VIDMM_TEST_STATE State)
{
    DXGMMS2_VIDMM_SEGMENT_STATUS_V1 Status;
    ULONGLONG First = MAXULONGLONG;
    ULONGLONG Second = MAXULONGLONG;
    ULONGLONG Third = MAXULONGLONG;
    ULONGLONG Recycled = MAXULONGLONG;
    NTSTATUS NtStatus;

    NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x1001, &First);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(First, 0ULL);

    NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x1002, &Second);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(Second, DXGMMS2_VIDMM_TEST_PAGE);

    NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x1003, &Third);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(Third, DXGMMS2_VIDMM_TEST_PAGE * 2);

    ok_eq_ulonglong(SegmentUsedSize(State, 0), DXGMMS2_VIDMM_TEST_PAGE * 3);

    /* Freeing the middle leaves a hole, but virgin space is still preferred. */
    NtStatus = Dxgmms2VidMmCoreRelease(&State->Core, 0, 0x1002);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(SegmentUsedSize(State, 0), DXGMMS2_VIDMM_TEST_PAGE * 2);

    NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x1004, &Recycled);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(Recycled, DXGMMS2_VIDMM_TEST_PAGE * 3);

    InitSegmentStatus(&Status);
    (VOID)Dxgmms2VidMmCoreQuerySegment(&State->Core, 0, &Status);
    ok_eq_ulonglong(Status.BumpOffset, DXGMMS2_VIDMM_TEST_PAGE * 4);
    ok_eq_ulong(Status.PlacementCount, 3UL);

    /* Releasing something that was never placed is reported, not ignored. */
    NtStatus = Dxgmms2VidMmCoreRelease(&State->Core, 0, 0xDEAD);
    ok_eq_hex(NtStatus, STATUS_NOT_FOUND);
    NtStatus = Dxgmms2VidMmCoreRelease(&State->Core, 0, 0);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);
}

/* Once the tail is gone, the allocator must reuse the holes it left behind. */
static VOID
TestFirstFitReusesHoles(
    _Inout_ PDXGMMS2_VIDMM_TEST_STATE State)
{
    DXGMMS2_VIDMM_SEGMENT_DESC_V1 Desc;
    ULONGLONG Offset = MAXULONGLONG;
    ULONGLONG Reused = MAXULONGLONG;
    NTSTATUS NtStatus;

    /* A small dedicated segment so the tail runs out quickly. */
    InitSegmentDesc(&Desc, DXGMMS2_VIDMM_TEST_PAGE * 4, 0, 0);
    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, 1, &Desc);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);

    NtStatus = ReserveSimple(State, 1, DXGMMS2_VIDMM_TEST_PAGE, 0x2001, &Offset);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(Offset, 0ULL);
    NtStatus = ReserveSimple(State, 1, DXGMMS2_VIDMM_TEST_PAGE, 0x2002, &Offset);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    NtStatus = ReserveSimple(State, 1, DXGMMS2_VIDMM_TEST_PAGE, 0x2003, &Offset);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    NtStatus = ReserveSimple(State, 1, DXGMMS2_VIDMM_TEST_PAGE, 0x2004, &Offset);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(Offset, DXGMMS2_VIDMM_TEST_PAGE * 3);

    /* Full: the commit ledger refuses before any search runs. */
    NtStatus = ReserveSimple(State, 1, DXGMMS2_VIDMM_TEST_PAGE, 0x2005, &Offset);
    ok_eq_hex(NtStatus, STATUS_NO_MEMORY);
    /* And a request larger than the segment can never be satisfied. */
    NtStatus = ReserveSimple(State, 1, DXGMMS2_VIDMM_TEST_PAGE * 16, 0x2006, &Offset);
    ok_eq_hex(NtStatus, STATUS_NO_MEMORY);

    NtStatus = Dxgmms2VidMmCoreRelease(&State->Core, 1, 0x2002);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);

    /* Virgin space is exhausted, so the hole is what is left. */
    NtStatus = ReserveSimple(State, 1, DXGMMS2_VIDMM_TEST_PAGE, 0x2007, &Reused);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(Reused, DXGMMS2_VIDMM_TEST_PAGE);

    {
        ULONGLONG Cookies[DXGMMS2_VIDMM_TEST_BATCH];
        ULONG Count = Dxgmms2VidMmCoreReleaseAll(&State->Core, Cookies, RTL_NUMBER_OF(Cookies));

        ok(Count >= 4, "release-all must return every live placement, got %lu\n", Count);
        ok_eq_ulonglong(SegmentUsedSize(State, 0), 0ULL);
        ok_eq_ulonglong(SegmentUsedSize(State, 1), 0ULL);
    }
}

static VOID
TestAlignmentAndCommitLimit(
    _Inout_ PDXGMMS2_VIDMM_TEST_STATE State)
{
    DXGMMS2_VIDMM_SEGMENT_DESC_V1 Desc;
    DXGMMS2_VIDMM_RESERVE_INFO_V1 Info;
    ULONGLONG Offset = MAXULONGLONG;
    NTSTATUS NtStatus;

    /* Half the segment is off limits: the ledger honours the miniport's cap. */
    InitSegmentDesc(&Desc, DXGMMS2_VIDMM_TEST_PAGE * 8, DXGMMS2_VIDMM_TEST_PAGE * 4, 0);
    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, 1, &Desc);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);

    InitReserveInfo(&Info, DXGMMS2_VIDMM_TEST_PAGE * 4, DXGMMS2_VIDMM_TEST_PAGE, 0x3001);
    NtStatus = Dxgmms2VidMmCoreReserve(&State->Core, 1, &Info, &Offset);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    InitReserveInfo(&Info, DXGMMS2_VIDMM_TEST_PAGE, DXGMMS2_VIDMM_TEST_PAGE, 0x3002);
    NtStatus = Dxgmms2VidMmCoreReserve(&State->Core, 1, &Info, &Offset);
    ok_eq_hex(NtStatus, STATUS_NO_MEMORY);

    NtStatus = Dxgmms2VidMmCoreRelease(&State->Core, 1, 0x3001);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);

    /* Size is rounded up to its own alignment, so the next offset clears it. */
    InitReserveInfo(&Info, 1, DXGMMS2_VIDMM_TEST_PAGE, 0x3003);
    NtStatus = Dxgmms2VidMmCoreReserve(&State->Core, 1, &Info, &Offset);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(SegmentUsedSize(State, 1), DXGMMS2_VIDMM_TEST_PAGE);

    /* A zero-sized or unowned request is a caller error. */
    InitReserveInfo(&Info, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x3004);
    NtStatus = Dxgmms2VidMmCoreReserve(&State->Core, 1, &Info, &Offset);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);
    InitReserveInfo(&Info, DXGMMS2_VIDMM_TEST_PAGE, DXGMMS2_VIDMM_TEST_PAGE, 0);
    NtStatus = Dxgmms2VidMmCoreReserve(&State->Core, 1, &Info, &Offset);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);
    /* Alignment must be a power of two to be meaningful. */
    InitReserveInfo(&Info, DXGMMS2_VIDMM_TEST_PAGE, 3, 0x3005);
    NtStatus = Dxgmms2VidMmCoreReserve(&State->Core, 1, &Info, &Offset);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);

    {
        ULONGLONG Cookies[DXGMMS2_VIDMM_TEST_BATCH];

        (VOID)Dxgmms2VidMmCoreReleaseAll(&State->Core, Cookies, RTL_NUMBER_OF(Cookies));
    }
}

/*
 * Redescribing a segment that already holds placements would invalidate every
 * offset dxgkrnl was handed out of it.
 */
static VOID
TestSegmentRedescriptionIsGuarded(
    _Inout_ PDXGMMS2_VIDMM_TEST_STATE State)
{
    DXGMMS2_VIDMM_SEGMENT_DESC_V1 Desc;
    ULONGLONG Offset = MAXULONGLONG;
    NTSTATUS NtStatus;

    InitSegmentDesc(&Desc, DXGMMS2_VIDMM_TEST_SEGMENT_SIZE, 0, 0);
    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, 1, &Desc);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);

    NtStatus = ReserveSimple(State, 1, DXGMMS2_VIDMM_TEST_PAGE, 0x4001, &Offset);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);

    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, 1, &Desc);
    ok_eq_hex(NtStatus, STATUS_INVALID_DEVICE_STATE);

    NtStatus = Dxgmms2VidMmCoreRelease(&State->Core, 1, 0x4001);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, 1, &Desc);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
}

/*
 * The eviction ranking is dxgmms2's: offered content first, then the lowest
 * priority among what the caller has not excluded.
 */
static VOID
TestEvictionRanking(
    _Inout_ PDXGMMS2_VIDMM_TEST_STATE State)
{
    ULONGLONG Offset = MAXULONGLONG;
    ULONGLONG Victim = MAXULONGLONG;
    NTSTATUS NtStatus;

    /* Start from a known-empty ledger and prove it really is empty: a stale
     * count here means release-all left bookkeeping behind. */
    {
        ULONGLONG Drain[DXGMMS2_VIDMM_TEST_BATCH];
        DXGMMS2_VIDMM_SEGMENT_STATUS_V1 Seg0;
        DXGMMS2_VIDMM_SEGMENT_STATUS_V1 Seg1;

        (VOID)Dxgmms2VidMmCoreReleaseAll(&State->Core, Drain, RTL_NUMBER_OF(Drain));
        InitSegmentStatus(&Seg0);
        InitSegmentStatus(&Seg1);
        (VOID)Dxgmms2VidMmCoreQuerySegment(&State->Core, 0, &Seg0);
        (VOID)Dxgmms2VidMmCoreQuerySegment(&State->Core, 1, &Seg1);
        ok_eq_ulong(Seg0.PlacementCount, 0UL);
        ok_eq_ulong(Seg1.PlacementCount, 0UL);
        ok_eq_ulonglong(Seg0.UsedSize, 0ULL);
        ok_eq_ulonglong(Seg1.UsedSize, 0ULL);
        ok_eq_ulong(State->Core.LiveRangeCount, 0UL);
    }

    { NTSTATUS Observed = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x5001, &Offset); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x5002, &Offset); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x5003, &Offset); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* Ranking is only meaningful over the set this segment actually holds. */
    {
        DXGMMS2_VIDMM_SEGMENT_STATUS_V1 SegStatus;

        InitSegmentStatus(&SegStatus);
        (VOID)Dxgmms2VidMmCoreQuerySegment(&State->Core, 0, &SegStatus);
        ok_eq_ulong(SegStatus.PlacementCount, 3UL);
    }

    NtStatus = Dxgmms2VidMmCoreSetRangeState(&State->Core, 0, 0x5001, 10, 0);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    NtStatus = Dxgmms2VidMmCoreSetRangeState(&State->Core, 0, 0x5002, 5, 0);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    NtStatus = Dxgmms2VidMmCoreSetRangeState(&State->Core, 0, 0x5003, 20, 0);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    NtStatus = Dxgmms2VidMmCoreSetRangeState(&State->Core, 0, 0xBEEF, 1, 0);
    ok_eq_hex(NtStatus, STATUS_NOT_FOUND);

    /* Lowest priority wins when nothing is offered. */
    ok_bool_true(Dxgmms2VidMmCoreFindEvictionCandidate(&State->Core, 0, 0, &Victim), "a victim exists");
    ok_eq_ulonglong(Victim, 0x5002ULL);

    /* Offered content is the preferred victim regardless of priority. */
    NtStatus = Dxgmms2VidMmCoreSetRangeState(&State->Core, 0, 0x5003, 20, DXGMMS2_VIDMM_RANGE_OFFERED);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_bool_true(Dxgmms2VidMmCoreFindEvictionCandidate(&State->Core, 0, 0, &Victim), "a victim exists");
    ok_eq_ulonglong(Victim, 0x5003ULL);

    /* Excluded flags remove a candidate from consideration entirely. */
    ok_bool_true(Dxgmms2VidMmCoreFindEvictionCandidate(&State->Core, 0, DXGMMS2_VIDMM_RANGE_OFFERED, &Victim),
                 "a non-offered victim exists");
    ok_eq_ulonglong(Victim, 0x5002ULL);

    /* An active submission pins placement in every segment. */
    NtStatus = Dxgmms2VidMmCoreSetRangeState(&State->Core, 0, 0x5002, 5, DXGMMS2_VIDMM_RANGE_PINNED);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_bool_true(Dxgmms2VidMmCoreFindEvictionCandidate(&State->Core, 0, DXGMMS2_VIDMM_RANGE_OFFERED | DXGMMS2_VIDMM_RANGE_PINNED, &Victim),
                 "the unpinned lower priority wins");
    ok_eq_ulonglong(Victim, 0x5001ULL);

    /* When everything is excluded there is no victim, and no stale answer. */
    NtStatus = Dxgmms2VidMmCoreSetRangeState(&State->Core, 0, 0x5001, 10, DXGMMS2_VIDMM_RANGE_PINNED);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    Victim = MAXULONGLONG;
    ok_bool_false(Dxgmms2VidMmCoreFindEvictionCandidate(&State->Core, 0, DXGMMS2_VIDMM_RANGE_OFFERED | DXGMMS2_VIDMM_RANGE_PINNED, &Victim),
                  "everything is pinned or offered");
    ok_eq_ulonglong(Victim, 0ULL);

    {
        ULONGLONG Cookies[DXGMMS2_VIDMM_TEST_BATCH];

        (VOID)Dxgmms2VidMmCoreReleaseAll(&State->Core, Cookies, RTL_NUMBER_OF(Cookies));
    }
}

/*
 * A full record pool is a property of this bookkeeping, not of the segments.
 * Reporting it as a full segment would send the caller off to evict something
 * while space was still free, so the pool grows on demand instead.
 */
static VOID
TestRecordPoolGrowsOnDemand(
    _Inout_ PDXGMMS2_VIDMM_TEST_STATE State)
{
    ULONGLONG Offset = MAXULONGLONG;
    ULONG Index;
    NTSTATUS NtStatus;

    ok_eq_ulong(State->Core.OverflowRangeCount, 0UL);

    /* Exactly enough placements to drain the preallocated pool. */
    for (Index = 0; Index < DXGMMS2_VIDMM_TEST_POOL; ++Index)
    {
        NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x6000 + Index, &Offset);
        ok_eq_hex(NtStatus, STATUS_SUCCESS);
    }
    ok_eq_ulong(State->Core.LiveRangeCount, (ULONG)DXGMMS2_VIDMM_TEST_POOL);
    ok_eq_ulong(State->Core.OverflowRangeCount, 0UL);

    /* The segment still has room, so this must succeed from overflow. */
    NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x6100, &Offset);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulong(State->Core.OverflowRangeCount, 1UL);
    NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x6101, &Offset);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulong(State->Core.OverflowRangeCount, 2UL);

    /* Releasing an overflow record frees it rather than leaking it. */
    NtStatus = Dxgmms2VidMmCoreRelease(&State->Core, 0, 0x6100);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulong(State->Core.OverflowRangeCount, 1UL);

    {
        ULONGLONG Cookies[DXGMMS2_VIDMM_TEST_BATCH];
        ULONG Count = Dxgmms2VidMmCoreReleaseAll(&State->Core, Cookies, RTL_NUMBER_OF(Cookies));

        ok_eq_ulong(Count, (ULONG)DXGMMS2_VIDMM_TEST_POOL + 1);
        ok_eq_ulong(State->Core.OverflowRangeCount, 0UL);
        ok_eq_ulong(State->Core.LiveRangeCount, 0UL);
    }
}

/*
 * By contract the owner is stopped only after dxgkrnl has released every
 * placement.  A survivor is reclaimed rather than leaked, and stopping returns
 * the core to its pre-start state so the adapter can be started again.
 */
static VOID
TestStopReclaimsAndRestarts(
    _Inout_ PDXGMMS2_VIDMM_TEST_STATE State)
{
    DXGMMS2_VIDMM_SEGMENT_DESC_V1 Desc;
    ULONGLONG Offset = MAXULONGLONG;
    NTSTATUS NtStatus;

    { NTSTATUS Observed = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x7001, &Offset); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x7002, &Offset); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok(State->Core.LiveRangeCount != 0, "placements are live before stop\n");

    Dxgmms2VidMmCoreStop(&State->Core);
    ok_eq_ulong(State->Core.LiveRangeCount, 0UL);
    ok_eq_ulong(State->Core.OverflowRangeCount, 0UL);
    ok_eq_ulong(State->Core.SegmentCount, 0UL);

    /* A stopped ledger takes nothing, and stopping again is harmless. */
    NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x7003, &Offset);
    ok_eq_hex(NtStatus, STATUS_INVALID_PARAMETER);
    Dxgmms2VidMmCoreStop(&State->Core);

    NtStatus = Dxgmms2VidMmCoreStart(&State->Core, DXGMMS2_VIDMM_TEST_SEGMENTS);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    InitSegmentDesc(&Desc, DXGMMS2_VIDMM_TEST_SEGMENT_SIZE, 0, 0);
    NtStatus = Dxgmms2VidMmCoreSetSegment(&State->Core, 0, &Desc);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);

    /* The restarted ledger hands out virgin space from the beginning again. */
    NtStatus = ReserveSimple(State, 0, DXGMMS2_VIDMM_TEST_PAGE, 0x7004, &Offset);
    ok_eq_hex(NtStatus, STATUS_SUCCESS);
    ok_eq_ulonglong(Offset, 0ULL);

    Dxgmms2VidMmCoreStop(&State->Core);
}

START_TEST(Dxgmms2VidMm)
{
    PDXGMMS2_VIDMM_TEST_STATE State;

    State = ExAllocatePoolWithTag(NonPagedPool, sizeof(*State), TAG_DXGMMS2_VIDMM_TEST);
    ok(State != NULL, "vidmm test state allocation failed\n");
    if (State == NULL)
        return;
    RtlZeroMemory(State, sizeof(*State));

    TestStartAndSegmentDescription(State);
    TestVirginSpaceThenFirstFit(State);
    TestFirstFitReusesHoles(State);
    TestAlignmentAndCommitLimit(State);
    TestSegmentRedescriptionIsGuarded(State);
    TestEvictionRanking(State);
    TestRecordPoolGrowsOnDemand(State);
    TestStopReclaimsAndRestarts(State);

    ExFreePoolWithTag(State, TAG_DXGMMS2_VIDMM_TEST);
}

/* EOF */
