/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Static VidMmInterface and VidSchInterface dispatch tables
 * COPYRIGHT:   Copyright 2024-2026 ReactOS Team
 *
 * dxgmms1.sys exports two versioned dispatch tables as data symbols:
 *
 *   VidMmInterface  (ordinal 1) — 73 function pointers for VidMm
 *   VidSchInterface (ordinal 2) — 61 function pointers for VidSch
 *
 * Each table has the layout: { ULONG Size; ULONG Version; PVOID fn[N]; }
 * dxgkrnl.sys loads these at driver init time and calls through them.
 *
 * This file defines both tables with their full Win7 slot counts.
 * Core functions are wired to real implementations; the rest are stubbed
 * with VidMmStubNotImplemented / VidSchStubNotImplemented.
 */

#include "dxgmms1_private.h"

/* =========================================================================
 * Generic stub functions
 *
 * VidMm functions are overwhelmingly nonpaged (71/73 in Win7).
 * VidSch split is ~40/60 nonpaged/paged.
 * We keep stubs nonpaged for safety.
 * ========================================================================= */

static NTSTATUS
NTAPI
VidMmStubNotImplemented(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS
NTAPI
VidSchStubNotImplemented(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

/* =========================================================================
 * VidMm core function wrappers
 *
 * The interface table expects flat function pointers with PVOID first args.
 * Our existing implementations already match that convention.
 * ========================================================================= */

/* Slot 0: VidMm adapter initialization */
static NTSTATUS
NTAPI
VidMmInitializeAdapter(
    _In_ PVOID AdapterContext,
    _In_ ULONG NumSegments)
{
    UNREFERENCED_PARAMETER(AdapterContext);
    UNREFERENCED_PARAMETER(NumSegments);

    return STATUS_NOT_SUPPORTED;
}

/* Slot 1: VidMm adapter teardown */
static VOID
NTAPI
VidMmTeardownAdapter(
    _In_ PVOID MmsContext)
{
    DxgkMmsRemoveAdapter(MmsContext);
}

/* Slot 2: Initialize a memory segment */
static NTSTATUS
NTAPI
VidMmInitializeSegment(
    _In_ PVOID            AdapterContext,
    _In_ ULONG            SegmentId,
    _In_ ULONGLONG        Size,
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ ULONG            Flags)
{
    return DxgkMmsInitializeSegment(AdapterContext, SegmentId, Size,
                                    BaseAddress, Flags);
}

/* Slot 3: Create allocation (allocate from segment) */
static NTSTATUS
NTAPI
VidMmCreateAllocation(
    _In_  PVOID      AdapterContext,
    _In_  ULONG      SegmentId,
    _In_  SIZE_T     Size,
    _In_  SIZE_T     Alignment,
    _Out_ PULONGLONG OutOffset)
{
    return DxgkMmsAllocateFromSegment(AdapterContext, SegmentId, Size,
                                      Alignment, OutOffset);
}

/* Slot 4: Destroy allocation (free from segment) */
static VOID
NTAPI
VidMmDestroyAllocation(
    _In_ PVOID     AdapterContext,
    _In_ ULONG     SegmentId,
    _In_ ULONGLONG Offset,
    _In_ SIZE_T    Size)
{
    DxgkMmsFreeFromSegment(AdapterContext, SegmentId, Offset, Size);
}

/* Slot 5: Lock allocation (build paging buffer for CPU access) */
static NTSTATUS
NTAPI
VidMmLockAllocation(
    _In_  PVOID    AdapterContext,
    _Out_ PVOID    PagingBuffer,
    _In_  ULONG    Operation,
    _In_  PVOID    Allocation,
    _In_  ULONGLONG SrcOffset,
    _In_  ULONGLONG DstOffset,
    _In_  SIZE_T   TransferSize)
{
    return DxgkMmsBuildPagingBuffer(AdapterContext, PagingBuffer, Operation,
                                    Allocation, SrcOffset, DstOffset,
                                    TransferSize);
}

/* Slot 6: Unlock allocation (submit paging buffer) */
static NTSTATUS
NTAPI
VidMmUnlockAllocation(
    _In_ PVOID AdapterContext,
    _In_ PVOID PagingBuffer)
{
    return DxgkMmsSubmitPagingBuffer(AdapterContext, PagingBuffer);
}

/* Slot 7: Query segment info stub — returns basic info */
static NTSTATUS
NTAPI
VidMmQuerySegmentInfo(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

/* Slot 8: Evict allocation */
static NTSTATUS
NTAPI
VidMmEvictAllocation(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

/* Slot 9: Make allocation resident */
static NTSTATUS
NTAPI
VidMmMakeResident(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

/* =========================================================================
 * VidSch core function wrappers
 * ========================================================================= */

/* Slot 0: VidSch adapter initialization */
static NTSTATUS
NTAPI
VidSchInitializeAdapter(
    _In_  PVOID  AdapterContext,
    _In_  ULONG  NumNodes,
    _In_  ULONG  NumSegments,
    _Out_ PVOID *OutMmsContext)
{
    return DxgkMmsAddAdapter(AdapterContext, NumNodes, NumSegments,
                              OutMmsContext);
}

/* Slot 1: VidSch adapter teardown */
static VOID
NTAPI
VidSchTeardownAdapter(
    _In_ PVOID MmsContext)
{
    DxgkMmsRemoveAdapter(MmsContext);
}

/* Slot 2: Submit DMA packet */
static NTSTATUS
NTAPI
VidSchSubmitCommand(
    _In_ PVOID AdapterContext,
    _In_ PVOID CommandBuffer)
{
    return DxgkMmsScheduleCommand(AdapterContext, CommandBuffer);
}

/* Slot 3: Preempt running command */
static NTSTATUS
NTAPI
VidSchPreemptCommand(
    _In_ PVOID   AdapterContext,
    _In_ ULONG   NodeOrdinal,
    _In_ ULONG64 PreemptionFenceId)
{
    return DxgkMmsPreemptCommand(AdapterContext, NodeOrdinal,
                                  PreemptionFenceId);
}

/* Slot 4: Query fence value */
static ULONG64
NTAPI
VidSchQueryFence(
    _In_ PVOID MmsContext,
    _In_ ULONG NodeOrdinal)
{
    return DxgkMmsQueryLastCompletedFence(MmsContext, NodeOrdinal);
}

/* Slot 5: Wait for GPU idle */
static NTSTATUS
NTAPI
VidSchWaitForIdle(
    _In_ PVOID AdapterContext,
    _In_ ULONG NodeOrdinal)
{
    return DxgkMmsWaitForIdleGpu(AdapterContext, NodeOrdinal);
}

/* Slot 6: Signal fence (notification) */
static NTSTATUS
NTAPI
VidSchSignalFence(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

/* Slot 7: Wait on fence */
static NTSTATUS
NTAPI
VidSchWaitOnFence(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

/* =========================================================================
 * VidMmInterface — 73 function pointer slots
 *
 * Layout: { ULONG Size; ULONG Version; PVOID fn[73]; }
 * Total: 8 + (73 * 8) = 592 bytes on amd64
 *
 * Slot mapping:
 *   [0]  InitializeAdapter
 *   [1]  TeardownAdapter
 *   [2]  InitializeSegment
 *   [3]  CreateAllocation (AllocateFromSegment)
 *   [4]  DestroyAllocation (FreeFromSegment)
 *   [5]  LockAllocation (BuildPagingBuffer)
 *   [6]  UnlockAllocation (SubmitPagingBuffer)
 *   [7]  QuerySegmentInfo
 *   [8]  EvictAllocation
 *   [9]  MakeResident
 *   [10..72] Stubbed (STATUS_NOT_IMPLEMENTED)
 * ========================================================================= */

#define VIDMM_INTERFACE_FN_COUNT    73
#define VIDMM_INTERFACE_SIZE        (sizeof(ULONG) * 2 + sizeof(PVOID) * VIDMM_INTERFACE_FN_COUNT)
#define VIDMM_INTERFACE_VERSION     1

typedef struct _VIDMM_INTERFACE_TABLE
{
    ULONG Size;
    ULONG Version;
    PVOID Functions[VIDMM_INTERFACE_FN_COUNT];
} VIDMM_INTERFACE_TABLE;

const VIDMM_INTERFACE_TABLE VidMmInterface =
{
    VIDMM_INTERFACE_SIZE,
    VIDMM_INTERFACE_VERSION,
    {
        /* [0]  */ (PVOID)VidMmInitializeAdapter,
        /* [1]  */ (PVOID)VidMmTeardownAdapter,
        /* [2]  */ (PVOID)VidMmInitializeSegment,
        /* [3]  */ (PVOID)VidMmCreateAllocation,
        /* [4]  */ (PVOID)VidMmDestroyAllocation,
        /* [5]  */ (PVOID)VidMmLockAllocation,
        /* [6]  */ (PVOID)VidMmUnlockAllocation,
        /* [7]  */ (PVOID)VidMmQuerySegmentInfo,
        /* [8]  */ (PVOID)VidMmEvictAllocation,
        /* [9]  */ (PVOID)VidMmMakeResident,
        /* [10] */ (PVOID)VidMmStubNotImplemented,
        /* [11] */ (PVOID)VidMmStubNotImplemented,
        /* [12] */ (PVOID)VidMmStubNotImplemented,
        /* [13] */ (PVOID)VidMmStubNotImplemented,
        /* [14] */ (PVOID)VidMmStubNotImplemented,
        /* [15] */ (PVOID)VidMmStubNotImplemented,
        /* [16] */ (PVOID)VidMmStubNotImplemented,
        /* [17] */ (PVOID)VidMmStubNotImplemented,
        /* [18] */ (PVOID)VidMmStubNotImplemented,
        /* [19] */ (PVOID)VidMmStubNotImplemented,
        /* [20] */ (PVOID)VidMmStubNotImplemented,
        /* [21] */ (PVOID)VidMmStubNotImplemented,
        /* [22] */ (PVOID)VidMmStubNotImplemented,
        /* [23] */ (PVOID)VidMmStubNotImplemented,
        /* [24] */ (PVOID)VidMmStubNotImplemented,
        /* [25] */ (PVOID)VidMmStubNotImplemented,
        /* [26] */ (PVOID)VidMmStubNotImplemented,
        /* [27] */ (PVOID)VidMmStubNotImplemented,
        /* [28] */ (PVOID)VidMmStubNotImplemented,
        /* [29] */ (PVOID)VidMmStubNotImplemented,
        /* [30] */ (PVOID)VidMmStubNotImplemented,
        /* [31] */ (PVOID)VidMmStubNotImplemented,
        /* [32] */ (PVOID)VidMmStubNotImplemented,
        /* [33] */ (PVOID)VidMmStubNotImplemented,
        /* [34] */ (PVOID)VidMmStubNotImplemented,
        /* [35] */ (PVOID)VidMmStubNotImplemented,
        /* [36] */ (PVOID)VidMmStubNotImplemented,
        /* [37] */ (PVOID)VidMmStubNotImplemented,
        /* [38] */ (PVOID)VidMmStubNotImplemented,
        /* [39] */ (PVOID)VidMmStubNotImplemented,
        /* [40] */ (PVOID)VidMmStubNotImplemented,
        /* [41] */ (PVOID)VidMmStubNotImplemented,
        /* [42] */ (PVOID)VidMmStubNotImplemented,
        /* [43] */ (PVOID)VidMmStubNotImplemented,
        /* [44] */ (PVOID)VidMmStubNotImplemented,
        /* [45] */ (PVOID)VidMmStubNotImplemented,
        /* [46] */ (PVOID)VidMmStubNotImplemented,
        /* [47] */ (PVOID)VidMmStubNotImplemented,
        /* [48] */ (PVOID)VidMmStubNotImplemented,
        /* [49] */ (PVOID)VidMmStubNotImplemented,
        /* [50] */ (PVOID)VidMmStubNotImplemented,
        /* [51] */ (PVOID)VidMmStubNotImplemented,
        /* [52] */ (PVOID)VidMmStubNotImplemented,
        /* [53] */ (PVOID)VidMmStubNotImplemented,
        /* [54] */ (PVOID)VidMmStubNotImplemented,
        /* [55] */ (PVOID)VidMmStubNotImplemented,
        /* [56] */ (PVOID)VidMmStubNotImplemented,
        /* [57] */ (PVOID)VidMmStubNotImplemented,
        /* [58] */ (PVOID)VidMmStubNotImplemented,
        /* [59] */ (PVOID)VidMmStubNotImplemented,
        /* [60] */ (PVOID)VidMmStubNotImplemented,
        /* [61] */ (PVOID)VidMmStubNotImplemented,
        /* [62] */ (PVOID)VidMmStubNotImplemented,
        /* [63] */ (PVOID)VidMmStubNotImplemented,
        /* [64] */ (PVOID)VidMmStubNotImplemented,
        /* [65] */ (PVOID)VidMmStubNotImplemented,
        /* [66] */ (PVOID)VidMmStubNotImplemented,
        /* [67] */ (PVOID)VidMmStubNotImplemented,
        /* [68] */ (PVOID)VidMmStubNotImplemented,
        /* [69] */ (PVOID)VidMmStubNotImplemented,
        /* [70] */ (PVOID)VidMmStubNotImplemented,
        /* [71] */ (PVOID)VidMmStubNotImplemented,
        /* [72] */ (PVOID)VidMmStubNotImplemented,
    }
};

/* =========================================================================
 * VidSchInterface — 61 function pointer slots
 *
 * Layout: { ULONG Size; ULONG Version; PVOID fn[61]; }
 * Total: 8 + (61 * 8) = 496 bytes on amd64
 *
 * Slot mapping:
 *   [0]  InitializeAdapter
 *   [1]  TeardownAdapter
 *   [2]  SubmitCommand
 *   [3]  PreemptCommand
 *   [4]  QueryFence
 *   [5]  WaitForIdle
 *   [6]  SignalFence
 *   [7]  WaitOnFence
 *   [8..60] Stubbed (STATUS_NOT_IMPLEMENTED)
 * ========================================================================= */

#define VIDSCH_INTERFACE_FN_COUNT   61
#define VIDSCH_INTERFACE_SIZE       (sizeof(ULONG) * 2 + sizeof(PVOID) * VIDSCH_INTERFACE_FN_COUNT)
#define VIDSCH_INTERFACE_VERSION    1

typedef struct _VIDSCH_INTERFACE_TABLE
{
    ULONG Size;
    ULONG Version;
    PVOID Functions[VIDSCH_INTERFACE_FN_COUNT];
} VIDSCH_INTERFACE_TABLE;

const VIDSCH_INTERFACE_TABLE VidSchInterface =
{
    VIDSCH_INTERFACE_SIZE,
    VIDSCH_INTERFACE_VERSION,
    {
        /* [0]  */ (PVOID)VidSchInitializeAdapter,
        /* [1]  */ (PVOID)VidSchTeardownAdapter,
        /* [2]  */ (PVOID)VidSchSubmitCommand,
        /* [3]  */ (PVOID)VidSchPreemptCommand,
        /* [4]  */ (PVOID)VidSchQueryFence,
        /* [5]  */ (PVOID)VidSchWaitForIdle,
        /* [6]  */ (PVOID)VidSchSignalFence,
        /* [7]  */ (PVOID)VidSchWaitOnFence,
        /* [8]  */ (PVOID)VidSchStubNotImplemented,
        /* [9]  */ (PVOID)VidSchStubNotImplemented,
        /* [10] */ (PVOID)VidSchStubNotImplemented,
        /* [11] */ (PVOID)VidSchStubNotImplemented,
        /* [12] */ (PVOID)VidSchStubNotImplemented,
        /* [13] */ (PVOID)VidSchStubNotImplemented,
        /* [14] */ (PVOID)VidSchStubNotImplemented,
        /* [15] */ (PVOID)VidSchStubNotImplemented,
        /* [16] */ (PVOID)VidSchStubNotImplemented,
        /* [17] */ (PVOID)VidSchStubNotImplemented,
        /* [18] */ (PVOID)VidSchStubNotImplemented,
        /* [19] */ (PVOID)VidSchStubNotImplemented,
        /* [20] */ (PVOID)VidSchStubNotImplemented,
        /* [21] */ (PVOID)VidSchStubNotImplemented,
        /* [22] */ (PVOID)VidSchStubNotImplemented,
        /* [23] */ (PVOID)VidSchStubNotImplemented,
        /* [24] */ (PVOID)VidSchStubNotImplemented,
        /* [25] */ (PVOID)VidSchStubNotImplemented,
        /* [26] */ (PVOID)VidSchStubNotImplemented,
        /* [27] */ (PVOID)VidSchStubNotImplemented,
        /* [28] */ (PVOID)VidSchStubNotImplemented,
        /* [29] */ (PVOID)VidSchStubNotImplemented,
        /* [30] */ (PVOID)VidSchStubNotImplemented,
        /* [31] */ (PVOID)VidSchStubNotImplemented,
        /* [32] */ (PVOID)VidSchStubNotImplemented,
        /* [33] */ (PVOID)VidSchStubNotImplemented,
        /* [34] */ (PVOID)VidSchStubNotImplemented,
        /* [35] */ (PVOID)VidSchStubNotImplemented,
        /* [36] */ (PVOID)VidSchStubNotImplemented,
        /* [37] */ (PVOID)VidSchStubNotImplemented,
        /* [38] */ (PVOID)VidSchStubNotImplemented,
        /* [39] */ (PVOID)VidSchStubNotImplemented,
        /* [40] */ (PVOID)VidSchStubNotImplemented,
        /* [41] */ (PVOID)VidSchStubNotImplemented,
        /* [42] */ (PVOID)VidSchStubNotImplemented,
        /* [43] */ (PVOID)VidSchStubNotImplemented,
        /* [44] */ (PVOID)VidSchStubNotImplemented,
        /* [45] */ (PVOID)VidSchStubNotImplemented,
        /* [46] */ (PVOID)VidSchStubNotImplemented,
        /* [47] */ (PVOID)VidSchStubNotImplemented,
        /* [48] */ (PVOID)VidSchStubNotImplemented,
        /* [49] */ (PVOID)VidSchStubNotImplemented,
        /* [50] */ (PVOID)VidSchStubNotImplemented,
        /* [51] */ (PVOID)VidSchStubNotImplemented,
        /* [52] */ (PVOID)VidSchStubNotImplemented,
        /* [53] */ (PVOID)VidSchStubNotImplemented,
        /* [54] */ (PVOID)VidSchStubNotImplemented,
        /* [55] */ (PVOID)VidSchStubNotImplemented,
        /* [56] */ (PVOID)VidSchStubNotImplemented,
        /* [57] */ (PVOID)VidSchStubNotImplemented,
        /* [58] */ (PVOID)VidSchStubNotImplemented,
        /* [59] */ (PVOID)VidSchStubNotImplemented,
        /* [60] */ (PVOID)VidSchStubNotImplemented,
    }
};
