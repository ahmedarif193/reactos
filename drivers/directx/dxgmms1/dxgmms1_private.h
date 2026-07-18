/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Private internal header for dxgmms1.sys
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * dxgmms1.sys is a kernel-mode module loaded explicitly by dxgkrnl.sys.
 * It is NOT a PnP driver and does NOT create device objects.  Instead it
 * exports DxgkMmsRegister(), which dxgkrnl calls to obtain a dispatch
 * table (DXGMMS_INTERFACE).  dxgkrnl then invokes those callbacks on
 * behalf of GPU adapters and their scheduling nodes.
 *
 * Internal object hierarchy:
 *
 *   DXGMMS_ADAPTER_CONTEXT   — one per GPU adapter registered via AddAdapter
 *     DXGMMS_NODE[NumNodes]  — one per GPU engine (3D, Video Decode, etc.)
 *     DXGMMS_SEGMENT[NumSegments] — one per GPU memory segment
 *
 * Memory pool tag: 'smmG'  (displayed as 'Gmms' in !poolfind / poolmon)
 */

#ifndef _DXGMMS1_PRIVATE_H_
#define _DXGMMS1_PRIVATE_H_

#include <ntifs.h>
#include <ntddk.h>
#include <wdm.h>

/*
 * windef.h provides UINT, BOOL, and other Win32 basic types that are not
 * unconditionally defined by ntddk.h in kernel mode.
 */
#include <windef.h>

/*
 * Pull in the public WDDM DDI headers that define types shared with
 * dxgkrnl (PHYSICAL_ADDRESS, etc.).  d3dkmddi.h transitively includes
 * d3dkmdt.h -> d3dukmdt.h, so a single include suffices.
 */
#define _D3DKMDDI_H_
#include <d3dkmddi.h>
#undef _D3DKMDDI_H_

/*
 * ReactOS debug macros (DPRINT, DPRINT1, ASSERT, etc.).
 * Use the explicit reactos/ path so the include is not shadowed by the
 * local debug.h in drivers/directx/dxgmms1/ when -I points at this dir.
 * Must be included before the local debug.h which uses DPRINT/DPRINT1.
 */
#include <reactos/debug.h>

/* Include our own debug helpers (wraps DPRINT/DPRINT1 with module prefix). */
#include "debug.h"

/* -------------------------------------------------------------------------
 * Pool tag
 *
 * Stored in memory in little-endian order; debugger tools reverse the
 * bytes, so the tag reads 'Gmms' in pool listings.
 * ------------------------------------------------------------------------- */
#define DXGMMS1_POOL_TAG    'smmG'

/* -------------------------------------------------------------------------
 * Interface version
 * ------------------------------------------------------------------------- */
#define DXGMMS_INTERFACE_VERSION    1

/* -------------------------------------------------------------------------
 * Paging buffer operation codes
 *
 * Passed as the Operation parameter to BuildPagingBuffer.  These mirror
 * the DXGK_OPERATION values defined in the Windows WDK dispmprt.h but
 * are reproduced here to avoid that dependency.
 * ------------------------------------------------------------------------- */
#define DXGMMS_PAGING_OP_TRANSFER       0   /* Move allocation between segments */
#define DXGMMS_PAGING_OP_FILL          1   /* Fill allocation with a pattern    */
#define DXGMMS_PAGING_OP_DISCARD       2   /* Discard allocation contents       */
#define DXGMMS_PAGING_OP_READ_PHYS     3   /* Read from physical address        */
#define DXGMMS_PAGING_OP_WRITE_PHYS    4   /* Write to physical address         */

/* -------------------------------------------------------------------------
 * Segment flags (InitializeSegment Flags parameter bitmask)
 * ------------------------------------------------------------------------- */
#define DXGMMS_SEGMENT_FLAG_APERTURE    0x00000001  /* CPU-visible aperture      */
#define DXGMMS_SEGMENT_FLAG_AGPTYPE     0x00000002  /* AGP-style aperture        */
#define DXGMMS_SEGMENT_FLAG_CPUVISIBLE  0x00000004  /* Mappable by CPU           */
#define DXGMMS_SEGMENT_FLAG_CACHECOHER  0x00000008  /* Cache-coherent with CPU   */

/* -------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */
typedef struct _DXGMMS_ADAPTER_CONTEXT   DXGMMS_ADAPTER_CONTEXT;
typedef struct _DXGMMS_ADAPTER_CONTEXT  *PDXGMMS_ADAPTER_CONTEXT;

typedef struct _DXGMMS_NODE              DXGMMS_NODE;
typedef struct _DXGMMS_NODE             *PDXGMMS_NODE;

typedef struct _DXGMMS_SEGMENT          DXGMMS_SEGMENT;
typedef struct _DXGMMS_SEGMENT         *PDXGMMS_SEGMENT;

typedef struct _DXGMMS_INTERFACE        DXGMMS_INTERFACE;
typedef struct _DXGMMS_INTERFACE       *PDXGMMS_INTERFACE;

typedef struct _DXGMMS_ALLOCATION_ENTRY DXGMMS_ALLOCATION_ENTRY;
typedef struct _DXGMMS_ALLOCATION_ENTRY *PDXGMMS_ALLOCATION_ENTRY;

/*
 * Include preemption.h here so that DXGMMS_PREEMPTION_STATE and
 * DXGMMS_TDR_STATE are fully defined before they are embedded by value
 * in DXGMMS_NODE and DXGMMS_ADAPTER_CONTEXT below.
 *
 * preemption.h repeats the forward declarations above; that is harmless
 * because C99 allows identical typedef redeclarations.
 */
#include "preemption.h"

/* -------------------------------------------------------------------------
 * DXGMMS_INTERFACE
 *
 * The dispatch table that DxgkMmsRegister() fills in and hands to dxgkrnl.
 * Every function pointer in this table is a mandatory entry point; dxgkrnl
 * will call them through the registered interface rather than through the
 * normal export mechanism so that version negotiation is possible.
 *
 * Calling convention: NTAPI (== __stdcall on x86, default on amd64/arm64).
 * ------------------------------------------------------------------------- */
struct _DXGMMS_INTERFACE
{
    /*
     * Size of this structure as filled in by dxgmms1.  dxgkrnl checks this
     * before accessing any field beyond the version.
     */
    ULONG   Size;

    /*
     * Interface version — currently DXGMMS_INTERFACE_VERSION (1).
     * dxgkrnl refuses to use an interface whose version it does not
     * recognise.
     */
    ULONG   Version;

    /* ------------------------------------------------------------------ */
    /* Scheduling                                                           */
    /* ------------------------------------------------------------------ */

    NTSTATUS
    (NTAPI *ScheduleCommand)(
        _In_ PVOID AdapterContext,
        _In_ PVOID CommandBuffer);

    NTSTATUS
    (NTAPI *PreemptCommand)(
        _In_ PVOID   AdapterContext,
        _In_ ULONG   NodeOrdinal,
        _In_ ULONG64 PreemptionFenceId);

    NTSTATUS
    (NTAPI *WaitForIdleGpu)(
        _In_ PVOID AdapterContext,
        _In_ ULONG NodeOrdinal);

    /* ------------------------------------------------------------------ */
    /* Paging                                                               */
    /* ------------------------------------------------------------------ */

    NTSTATUS
    (NTAPI *BuildPagingBuffer)(
        _In_  PVOID    AdapterContext,
        _Out_ PVOID    PagingBuffer,
        _In_  ULONG    Operation,
        _In_  PVOID    Allocation,
        _In_  ULONGLONG SrcOffset,
        _In_  ULONGLONG DstOffset,
        _In_  SIZE_T   TransferSize);

    NTSTATUS
    (NTAPI *SubmitPagingBuffer)(
        _In_ PVOID AdapterContext,
        _In_ PVOID PagingBuffer);

    /* ------------------------------------------------------------------ */
    /* Memory segments                                                      */
    /* ------------------------------------------------------------------ */

    NTSTATUS
    (NTAPI *InitializeSegment)(
        _In_ PVOID            AdapterContext,
        _In_ ULONG            SegmentId,
        _In_ ULONGLONG        Size,
        _In_ PHYSICAL_ADDRESS BaseAddress,
        _In_ ULONG            Flags);

    NTSTATUS
    (NTAPI *AllocateFromSegment)(
        _In_  PVOID      AdapterContext,
        _In_  ULONG      SegmentId,
        _In_  SIZE_T     Size,
        _In_  SIZE_T     Alignment,
        _Out_ PULONGLONG OutOffset);

    VOID
    (NTAPI *FreeFromSegment)(
        _In_ PVOID     AdapterContext,
        _In_ ULONG     SegmentId,
        _In_ ULONGLONG Offset,
        _In_ SIZE_T    Size);

    /* ------------------------------------------------------------------ */
    /* Adapter lifecycle                                                    */
    /* ------------------------------------------------------------------ */

    NTSTATUS
    (NTAPI *AddAdapter)(
        _In_  PVOID  AdapterContext,
        _In_  ULONG  NumNodes,
        _In_  ULONG  NumSegments,
        _Out_ PVOID *OutMmsContext);

    VOID
    (NTAPI *RemoveAdapter)(
        _In_ PVOID MmsContext);

    /* ------------------------------------------------------------------ */
    /* Query                                                                */
    /* ------------------------------------------------------------------ */

    ULONG64
    (NTAPI *QueryLastCompletedFence)(
        _In_ PVOID MmsContext,
        _In_ ULONG NodeOrdinal);
};

/*
 * Number of GPU priority bands used by the scheduler.
 *   0 = background, 1 = normal, 2 = high, 3 = realtime (video/audio).
 */
#define DXGMMS_PRIORITY_LEVELS          4
#define DXGMMS_PRIORITY_BACKGROUND      0
#define DXGMMS_PRIORITY_NORMAL          1
#define DXGMMS_PRIORITY_HIGH            2
#define DXGMMS_PRIORITY_REALTIME        3
#define DXGMMS_SUBMIT_FLAG_PAGING       0x00000001u

/* Default GPU quantum in milliseconds and as a negative 100-ns KTIMER interval. */
#define DXGMMS_QUANTUM_MS               16
#define DXGMMS_QUANTUM_100NS            (-1LL * DXGMMS_QUANTUM_MS * 10000LL)

/* Maximum nodes (GPU engines) per adapter. */
#define DXGMMS_MAX_NODES                8

/* Pool tags */
#define TAG_DXGMMS_ADAPTER              'AdmG'   /* DXGMMS_ADAPTER_CONTEXT allocation */
#define TAG_DXGMMS_CMD_BUF              'BmdG'   /* DXGMMS_COMMAND_BUFFER allocation  */

/* -------------------------------------------------------------------------
 * DXGMMS_COMMAND_BUFFER
 *
 * Represents one DMA command buffer in flight through the scheduler.
 * ------------------------------------------------------------------------- */
typedef struct _DXGMMS_COMMAND_BUFFER
{
    LIST_ENTRY          QueueEntry;
    PVOID               DxgkrnlBuffer;
    PVOID               DmaBuffer;
    ULONG               DmaBufferSegmentId;
    PHYSICAL_ADDRESS    DmaBufferPhysicalAddress;
    ULONG               DmaBufferSize;
    ULONG               DmaBufferSubmissionStartOffset;
    ULONG               DmaBufferSubmissionEndOffset;
    PVOID               DmaBufferPrivateData;
    ULONG               DmaBufferPrivateDataSize;
    ULONG               DmaBufferPrivateDataSubmissionStartOffset;
    ULONG               DmaBufferPrivateDataSubmissionEndOffset;
    HANDLE              ContextHandle;
    ULONG               Flags;
    ULONG               NodeOrdinal;
    ULONG               Priority;
    ULONG64             SubmitFenceId;
    BOOLEAN             IsPreempted;
    BOOLEAN             IsCompleted;
} DXGMMS_COMMAND_BUFFER, *PDXGMMS_COMMAND_BUFFER;

/* -------------------------------------------------------------------------
 * DXGMMS_NODE
 *
 * Per-GPU-engine state.
 * ------------------------------------------------------------------------- */
struct _DXGMMS_NODE
{
    ULONG               NodeOrdinal;
    PDXGMMS_ADAPTER_CONTEXT AdapterContext;

    KSPIN_LOCK          Lock;
#define QueueLock Lock

    LIST_ENTRY          ReadyQueue[DXGMMS_PRIORITY_LEVELS];
    PDXGMMS_COMMAND_BUFFER RunningBuffer;

    ULONG64             CurrentFenceId;
    ULONG64             LastPreemptedFence;
    ULONG64             PreemptionFenceId;

    volatile LONG64     LastSubmittedFenceId;
    volatile LONG64     LastCompletedFenceId;

    KDPC                QuantumDpc;
    KTIMER              QuantumTimer;

    BOOLEAN             PreemptionPending;

    KEVENT              IdleEvent;

    /* ------------------------------------------------------------------ */
    /* Preemption subsystem (preemption.h / preemption.c)                   */
    /* ------------------------------------------------------------------ */

    /*
     * Per-node preemption tracking.  Initialised by DxgkMmsInitPreemption.
     */
    DXGMMS_PREEMPTION_STATE PreemptState;
};

/* -------------------------------------------------------------------------
 * DXGMMS_SEGMENT
 *
 * Per-segment video memory descriptor.  The MMS maintains a simple
 * free-list allocator for each segment.
 *
 * AllocationList tracks all DXGMMS_ALLOCATION_ENTRY objects currently
 * resident in this segment.  Used by DxgkMmsEvictLeastRecentlyUsed
 * to select an eviction victim when the segment is under pressure.
 * Protected by Lock.
 *
 * For aperture segments, AperturePages is a NonPagedPool array of
 * PFN_NUMBER with NumAperturePages entries (one per PAGE_SIZE page).
 * NULL for non-aperture segments.
 * ------------------------------------------------------------------------- */
struct _DXGMMS_SEGMENT
{
    ULONG               SegmentId;
    PHYSICAL_ADDRESS    BaseAddress;
    ULONGLONG           TotalSize;
    ULONGLONG           AllocatedSize;
    ULONG               Flags;

    /*
     * Spinlock protecting the segment free list and AllocationList.
     * Acquired at DISPATCH_LEVEL.
     */
    KSPIN_LOCK          Lock;

    /*
     * Head of the free block list, sorted by offset.  Each entry is a
     * DXGMMS_FREE_BLOCK allocated from pool with tag DXGMMS1_POOL_TAG.
     */
    LIST_ENTRY          FreeListHead;

    /*
     * List of DXGMMS_ALLOCATION_ENTRY objects for all allocations currently
     * resident in this segment.  Used by the eviction policy.
     * Protected by Lock.
     */
    LIST_ENTRY          AllocationList;

    /* ------------------------------------------------------------------ */
    /* Aperture-segment fields (valid when Flags & DXGMMS_SEGMENT_FLAG_APERTURE) */
    /* ------------------------------------------------------------------ */

    /*
     * AperturePages: NonPagedPool array of PFN_NUMBER, one entry per
     * aperture page slot.  Allocated by DxgkMmsInitializeSegment for
     * aperture segments; NULL for VRAM segments.
     */
    PPFN_NUMBER         AperturePages;

    /*
     * Number of valid entries in AperturePages.
     * Equals (TotalSize / PAGE_SIZE) for aperture segments.
     */
    ULONG               NumAperturePages;
};

/* -------------------------------------------------------------------------
 * DXGMMS_FREE_BLOCK
 *
 * A single entry on a segment's free list.
 * ------------------------------------------------------------------------- */
typedef struct _DXGMMS_FREE_BLOCK
{
    /* Links into DXGMMS_SEGMENT.FreeListHead (sorted by Offset). */
    LIST_ENTRY  ListEntry;

    /* Byte offset from the segment base of this free block. */
    ULONGLONG   Offset;

    /* Size of this free block in bytes. */
    ULONGLONG   Size;
} DXGMMS_FREE_BLOCK, *PDXGMMS_FREE_BLOCK;

/* -------------------------------------------------------------------------
 * DXGMMS_ADAPTER_CONTEXT
 *
 * One instance per registered GPU adapter.  Allocated by AddAdapter and
 * freed by RemoveAdapter.
 * ------------------------------------------------------------------------- */
struct _DXGMMS_ADAPTER_CONTEXT
{
    ULONG               Signature;

    PVOID               DxgkrnlContext;
    PVOID               MiniportContext;

    /* Miniport DDI function pointers */
    PVOID               DxgkDdiSubmitCommand;
    PVOID               DxgkDdiPreemptCommand;
    PVOID               DxgkDdiQueryCurrentFence;
    PVOID               DxgkDdiBuildPagingBuffer;   /* IRQL: PASSIVE_LEVEL */
    PVOID               DxgkDdiResetFromTimeout;    /* IRQL: PASSIVE_LEVEL */
    PVOID               DxgkDdiRestartFromTimeout;  /* IRQL: PASSIVE_LEVEL */

    /* Completion notification callback (provided by dxgkrnl) */
    PVOID               CompletionCallback;
    PVOID               CompletionCallbackContext;

    /* Node array */
    ULONG               NumNodes;
    PDXGMMS_NODE        Nodes;

    /* Segment array */
    ULONG               NumSegments;
    PDXGMMS_SEGMENT     Segments;

    /* Adapter-wide synchronisation */
    KSPIN_LOCK          AdapterLock;

    BOOLEAN             Removed;

    /* Link into the global DxgMmsAdapterListHead */
    LIST_ENTRY          GlobalListEntry;

    /* ------------------------------------------------------------------ */
    /* Paging engine                                                        */
    /* ------------------------------------------------------------------ */

    /*
     * PagingBuffer: the adapter-wide paging DMA buffer (PDXGMMS_PAGING_BUFFER
     * cast to PVOID).  Allocated once by DxgkMmsInitializePagingBuffer at
     * adapter start time; freed by DxgkMmsFreePagingBuffer at remove time.
     * Stored as PVOID here to avoid a forward-reference dependency between
     * dxgmms1_private.h and paging.h.
     */
    PVOID               PagingBuffer;

    /*
     * PagingFenceId: monotonically increasing counter used to generate
     * unique fence IDs for paging submissions.  Incremented under
     * Node->Lock before each SubmitPagingBuffer call; read atomically
     * on amd64 (naturally 64-bit aligned ULONG64 read is always atomic).
     */
    volatile ULONG64    PagingFenceId;

    /* ------------------------------------------------------------------ */
    /* Eviction support                                                     */
    /* ------------------------------------------------------------------ */

    /*
     * EvictCallback: called by DxgkMmsEvictLeastRecentlyUsed to request
     * that dxgkrnl build and submit a paging buffer that moves an
     * allocation out of its current segment.  NULL if not provided.
     *
     * Prototype: NTSTATUS NTAPI fn(PVOID DxgkrnlContext, PVOID AllocationHandle)
     */
    PVOID               EvictCallback;

    /*
     * GlobalAllocationList: all DXGMMS_ALLOCATION_ENTRY objects across all
     * segments for this adapter.  Protected by AdapterLock.
     */
    LIST_ENTRY          GlobalAllocationList;

    /* ------------------------------------------------------------------ */
    /* TDR / preemption watchdog (preemption.h / preemption.c)             */
    /* ------------------------------------------------------------------ */

    /*
     * Per-adapter TDR watchdog state.  Initialised by DxgkMmsTdrInit at
     * adapter start time.
     */
    DXGMMS_TDR_STATE    TdrState;
};

#define DXGMMS_ADAPTER_SIGNATURE    'AMMA'  /* 'AMMA' — Adapter MMS */

/* -------------------------------------------------------------------------
 * Global state (defined in dxgmms1.c)
 * ------------------------------------------------------------------------- */

/* Spinlock protecting DxgMmsAdapterListHead. */
extern KSPIN_LOCK   DxgMmsAdapterListLock;

/* List of all registered DXGMMS_ADAPTER_CONTEXT instances. */
extern LIST_ENTRY   DxgMmsAdapterListHead;

/* -------------------------------------------------------------------------
 * Function declarations — interface.c
 * ------------------------------------------------------------------------- */
NTSTATUS
NTAPI
DxgkMmsRegister(
    _In_  PVOID            DxgkrnlContext,
    _Out_ PDXGMMS_INTERFACE Interface);

VOID
NTAPI
DxgkMmsUnregister(
    _In_ PVOID DxgkrnlContext);

/* -------------------------------------------------------------------------
 * Function declarations — scheduler.c
 * ------------------------------------------------------------------------- */
NTSTATUS
NTAPI
DxgkMmsScheduleCommand(
    _In_ PVOID AdapterContext,
    _In_ PVOID CommandBuffer);

/*
 * DxgkMmsScheduleNext — dispatch the next ready command buffer on a node.
 * Called from preemption.c and TDR reset paths.
 * IRQL: <= DISPATCH_LEVEL
 */
VOID
DxgkMmsScheduleNext(
    _In_ PDXGMMS_NODE Node);

NTSTATUS
NTAPI
DxgkMmsWaitForIdleGpu(
    _In_ PVOID AdapterContext,
    _In_ ULONG NodeOrdinal);

VOID
DxgkMmsCommandCompleted(
    _In_ PVOID  MmsContext,
    _In_ ULONG  NodeOrdinal,
    _In_ ULONG64 CompletedFenceId);

/*
 * DxgkMmsPreemptCommand — implemented in preemption.c.
 * Declared here for interface.c to reference it in DxgkMmsRegister.
 * IRQL: <= DISPATCH_LEVEL
 */
NTSTATUS
NTAPI
DxgkMmsPreemptCommand(
    _In_ PVOID   AdapterContext,
    _In_ ULONG   NodeOrdinal,
    _In_ ULONG64 PreemptionFenceId);

/* -------------------------------------------------------------------------
 * Function declarations — paging.c
 * ------------------------------------------------------------------------- */
NTSTATUS
NTAPI
DxgkMmsBuildPagingBuffer(
    _In_  PVOID     AdapterContext,
    _Out_ PVOID     PagingBuffer,
    _In_  ULONG     Operation,
    _In_  PVOID     Allocation,
    _In_  ULONGLONG SrcOffset,
    _In_  ULONGLONG DstOffset,
    _In_  SIZE_T    TransferSize);

NTSTATUS
NTAPI
DxgkMmsSubmitPagingBuffer(
    _In_ PVOID AdapterContext,
    _In_ PVOID PagingBuffer);

/* -------------------------------------------------------------------------
 * Function declarations — preemption.c
 * ------------------------------------------------------------------------- */
NTSTATUS
DxgkMmsInitPreemption(
    _In_ PDXGMMS_NODE Node);

VOID
DxgkMmsHandlePreemptionComplete(
    _In_ PDXGMMS_NODE Node,
    _In_ ULONG64      CompletedFenceId);

/* -------------------------------------------------------------------------
 * Function declarations — vidmm.c
 * ------------------------------------------------------------------------- */
NTSTATUS
NTAPI
DxgkMmsInitializeSegment(
    _In_ PVOID            AdapterContext,
    _In_ ULONG            SegmentId,
    _In_ ULONGLONG        Size,
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ ULONG            Flags);

NTSTATUS
NTAPI
DxgkMmsAllocateFromSegment(
    _In_  PVOID      AdapterContext,
    _In_  ULONG      SegmentId,
    _In_  SIZE_T     Size,
    _In_  SIZE_T     Alignment,
    _Out_ PULONGLONG OutOffset);

VOID
NTAPI
DxgkMmsFreeFromSegment(
    _In_ PVOID     AdapterContext,
    _In_ ULONG     SegmentId,
    _In_ ULONGLONG Offset,
    _In_ SIZE_T    Size);

/* -------------------------------------------------------------------------
 * Function declarations — adapter lifecycle (interface.c)
 * ------------------------------------------------------------------------- */
NTSTATUS
NTAPI
DxgkMmsAddAdapter(
    _In_  PVOID  AdapterContext,
    _In_  ULONG  NumNodes,
    _In_  ULONG  NumSegments,
    _Out_ PVOID *OutMmsContext);

VOID
NTAPI
DxgkMmsRemoveAdapter(
    _In_ PVOID MmsContext);

ULONG64
NTAPI
DxgkMmsQueryLastCompletedFence(
    _In_ PVOID MmsContext,
    _In_ ULONG NodeOrdinal);

/* -------------------------------------------------------------------------
 * DriverUnload (dxgmms1.c)
 * ------------------------------------------------------------------------- */
DRIVER_UNLOAD DxgkMmsUnload;

#endif /* _DXGMMS1_PRIVATE_H_ */
