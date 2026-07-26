/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Video memory segment ownership core
 *
 * dxgmms2 owns segment space: what is committed where, how much of a segment
 * is in use, and which offset a new placement gets.  Like the scheduler core
 * this is free of dxgkrnl and miniport types — a placement's owner is an
 * opaque cookie — so the allocator's rules can be exercised on their own.
 */

#pragma once

#include <ntddk.h>
#include <reactos/drivers/directx/dxgmms2.h>

#define DXGMMS2_VIDMM_MAX_RANGES    2048

typedef struct _DXGMMS2_VIDMM_RANGE
{
    LIST_ENTRY Entry;           /* segment range list, ascending Offset */
    ULONGLONG  Offset;
    ULONGLONG  AlignedSize;
    ULONGLONG  OwnerCookie;
    ULONG      SegmentIndex;
    LONG       Priority;
    ULONG      Flags;
} DXGMMS2_VIDMM_RANGE, *PDXGMMS2_VIDMM_RANGE;

typedef struct _DXGMMS2_VIDMM_SEGMENT
{
    LIST_ENTRY RangeList;
    ULONGLONG  Size;
    ULONGLONG  CommitLimit;     /* 0 = Size */
    ULONGLONG  UsedSize;
    ULONGLONG  BumpOffset;      /* virgin-space high-water cursor */
    ULONG      RangeCount;
    ULONG      Flags;
    BOOLEAN    Valid;
} DXGMMS2_VIDMM_SEGMENT, *PDXGMMS2_VIDMM_SEGMENT;

typedef struct _DXGMMS2_VIDMM_CORE
{
    DXGMMS2_VIDMM_SEGMENT Segments[DXGMMS2_VIDMM_MAX_SEGMENTS];
    ULONG                 SegmentCount;
    LIST_ENTRY            FreeRangeList;
    PDXGMMS2_VIDMM_RANGE  RangePool;
    ULONG                 RangePoolCount;
    ULONG                 LiveRangeCount;
    BOOLEAN               Started;
} DXGMMS2_VIDMM_CORE, *PDXGMMS2_VIDMM_CORE;

/* All of these run under one caller-owned lock. */
VOID Dxgmms2VidMmCoreInitialize(_Out_ PDXGMMS2_VIDMM_CORE Core, _In_reads_(PoolCount) PDXGMMS2_VIDMM_RANGE RangePool, _In_ ULONG PoolCount);
NTSTATUS Dxgmms2VidMmCoreStart(_Inout_ PDXGMMS2_VIDMM_CORE Core, _In_ ULONG SegmentCount);
VOID Dxgmms2VidMmCoreStop(_Inout_ PDXGMMS2_VIDMM_CORE Core);
NTSTATUS Dxgmms2VidMmCoreSetSegment(_Inout_ PDXGMMS2_VIDMM_CORE Core, _In_ ULONG SegmentIndex, _In_ const DXGMMS2_VIDMM_SEGMENT_DESC_V1 *Desc);
NTSTATUS Dxgmms2VidMmCoreReserve(_Inout_ PDXGMMS2_VIDMM_CORE Core, _In_ ULONG SegmentIndex, _In_ const DXGMMS2_VIDMM_RESERVE_INFO_V1 *Info, _Out_ PULONGLONG OutOffset);
NTSTATUS Dxgmms2VidMmCoreRelease(_Inout_ PDXGMMS2_VIDMM_CORE Core, _In_ ULONG SegmentIndex, _In_ ULONGLONG OwnerCookie);
NTSTATUS Dxgmms2VidMmCoreQuerySegment(_In_ PDXGMMS2_VIDMM_CORE Core, _In_ ULONG SegmentIndex, _Inout_ DXGMMS2_VIDMM_SEGMENT_STATUS_V1 *Status);
BOOLEAN Dxgmms2VidMmCoreFindEvictionCandidate(_In_ PDXGMMS2_VIDMM_CORE Core, _In_ ULONG SegmentIndex, _In_ ULONG ExcludeFlags, _Out_ PULONGLONG OutOwnerCookie);
ULONG Dxgmms2VidMmCoreReleaseAll(_Inout_ PDXGMMS2_VIDMM_CORE Core, _Out_writes_to_(Capacity, return) PULONGLONG OwnerCookies, _In_ ULONG Capacity);
NTSTATUS Dxgmms2VidMmCoreSetRangeState(_Inout_ PDXGMMS2_VIDMM_CORE Core, _In_ ULONG SegmentIndex, _In_ ULONGLONG OwnerCookie, _In_ LONG Priority, _In_ ULONG Flags);

/* EOF */
