/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Paging operation validation and the multipass build state machine
 *
 * DxgkDdiBuildPagingBuffer may refuse a transfer that does not fit and ask to
 * be called again from a byte offset.  Getting that loop wrong either drops
 * the tail of a copy or signals the caller's fence before the copy finished,
 * so the fence must ride only on the final pass.  No dxgkrnl types.
 */

#ifndef _DXGK_PAGING_CORE_H_
#define _DXGK_PAGING_CORE_H_

#include <ntddk.h>

#define DXGK_PAGING_CORE_MAX_PASSES 256

typedef enum _DXGK_PAGING_CORE_OP
{
    DxgkPagingCoreOpNone            = 0,
    DxgkPagingCoreOpTransfer        = 1,
    DxgkPagingCoreOpFill            = 2,
    DxgkPagingCoreOpDiscardContent  = 3,
    DxgkPagingCoreOpMapAperture     = 4,
    DxgkPagingCoreOpUnmapAperture   = 5,
    DxgkPagingCoreOpUpdatePageTable = 6,
    DxgkPagingCoreOpFlushTlb        = 7,
    DxgkPagingCoreOpNotifyResidency = 8,
    DxgkPagingCoreOpMax
} DXGK_PAGING_CORE_OP;

typedef struct _DXGK_PAGING_CORE_REQUEST
{
    DXGK_PAGING_CORE_OP Operation;
    ULONGLONG TransferSize;        /* transfer/fill */
    ULONG     SourceSegmentId;     /* transfer */
    ULONG     DestinationSegmentId;/* transfer */
    ULONGLONG GpuVirtualAddress;   /* update-page-table / flush-tlb */
    ULONGLONG SizeInBytes;         /* update-page-table / flush-tlb */
    ULONG     PageTableLevel;
    BOOLEAN   SystemMemoryPresent;
} DXGK_PAGING_CORE_REQUEST, *PDXGK_PAGING_CORE_REQUEST;

NTSTATUS DxgkPagingCoreValidate(_In_ const DXGK_PAGING_CORE_REQUEST *Request, _In_ ULONG PageTableLevelCount);

/* The multipass build loop.  A pass is only allowed to carry the caller's
 * completion fence once no further pass is required. */
typedef struct _DXGK_PAGING_CORE_MULTIPASS
{
    ULONGLONG TotalBytes;
    ULONGLONG MultipassOffset;
    ULONG     PassCount;
    BOOLEAN   Started;
    BOOLEAN   Complete;
    BOOLEAN   FenceEmitted;
} DXGK_PAGING_CORE_MULTIPASS, *PDXGK_PAGING_CORE_MULTIPASS;

NTSTATUS DxgkPagingCoreMultipassBegin(_Out_ PDXGK_PAGING_CORE_MULTIPASS Pass, _In_ ULONGLONG TotalBytes);
/* TRUE when this is the first pass, which is what DXGK_TRANSFERFLAGS.TransferStart marks. */
BOOLEAN DxgkPagingCoreMultipassIsFirst(_In_ const DXGK_PAGING_CORE_MULTIPASS Pass[1]);
/* Records that the miniport consumed BytesProcessed of the transfer.  Pass
 * STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER to request another pass. */
NTSTATUS DxgkPagingCoreMultipassAdvance(_Inout_ PDXGK_PAGING_CORE_MULTIPASS Pass, _In_ NTSTATUS MiniportStatus, _In_ ULONGLONG BytesProcessed);
/* TRUE only for the pass that finishes the operation; the caller's fence must
 * be attached to exactly that pass. */
BOOLEAN DxgkPagingCoreMultipassMayEmitFence(_In_ const DXGK_PAGING_CORE_MULTIPASS Pass[1]);
NTSTATUS DxgkPagingCoreMultipassEmitFence(_Inout_ PDXGK_PAGING_CORE_MULTIPASS Pass);

/* --- WDDM 2.2 monitored-fence paging signal ------------------------- */

typedef enum _DXGK_PAGING_NO_WORK_SIGNAL_ROUTE
{
    DxgkPagingNoWorkSignalNone = 0,
    DxgkPagingNoWorkSignalHandle,
    DxgkPagingNoWorkSignalRetainedReference
} DXGK_PAGING_NO_WORK_SIGNAL_ROUTE;

BOOLEAN
DxgkPagingCoreShouldAppendMonitoredSignal(
    _In_ ULONG ConfiguredWddmLevel,
    _In_ ULONG RuntimeWddmLevel,
    _In_ BOOLEAN SignalRequested,
    _In_ BOOLEAN GpuAddressReferenceAvailable);

NTSTATUS
DxgkPagingCoreBeginMonitoredSignal(
    _In_ BOOLEAN OperationsBuilt);

NTSTATUS
DxgkPagingCoreFinishMonitoredSignal(
    _In_ NTSTATUS BuildStatus,
    _In_ ULONG BytesEmitted,
    _Out_ PBOOLEAN SignalWrittenByGpu);

DXGK_PAGING_NO_WORK_SIGNAL_ROUTE
DxgkPagingCoreNoWorkSignalRoute(
    _In_ BOOLEAN SignalRequested,
    _In_ BOOLEAN RetainedReferenceAvailable);

/*
 * WDDM paging-queue fence transaction
 *
 * A paging queue is an ordered stream.  Reserving its monitored-fence value
 * before scheduler admission without serializing the whole transaction lets a
 * later request publish value N+1 before request N has even been admitted.
 * The waiter for N then observes false completion.
 *
 * Begin holds the queue mutex through paging build/admission.  The caller
 * gives CandidateCounter to the existing residency path, which increments it
 * only when a paging packet is actually produced.  Complete commits that
 * candidate after successful admission; Abort discards it after any failure,
 * so rejected work consumes no externally visible fence value.
 */
typedef struct _DXGK_PAGING_FENCE_QUEUE_CORE
{
    KMUTEX Lock;
    ULONGLONG CommittedFenceValue;
    BOOLEAN Initialized;
    BOOLEAN ShuttingDown;
} DXGK_PAGING_FENCE_QUEUE_CORE, *PDXGK_PAGING_FENCE_QUEUE_CORE;

typedef struct _DXGK_PAGING_FENCE_TRANSACTION
{
    PDXGK_PAGING_FENCE_QUEUE_CORE Queue;
    DECLSPEC_ALIGN(8) volatile LONG64 CandidateCounter;
    ULONGLONG StartingFenceValue;
    BOOLEAN Active;
} DXGK_PAGING_FENCE_TRANSACTION, *PDXGK_PAGING_FENCE_TRANSACTION;

VOID
DxgkPagingFenceQueueCoreInitialize(
    _Out_ PDXGK_PAGING_FENCE_QUEUE_CORE Queue,
    _In_ ULONGLONG InitialFenceValue);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
DxgkPagingFenceQueueCoreBegin(
    _Inout_ PDXGK_PAGING_FENCE_QUEUE_CORE Queue,
    _Out_ PDXGK_PAGING_FENCE_TRANSACTION Transaction);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
DxgkPagingFenceQueueCoreComplete(
    _Inout_ PDXGK_PAGING_FENCE_TRANSACTION Transaction,
    _In_ ULONGLONG PublishedFenceValue);

_IRQL_requires_(PASSIVE_LEVEL)
VOID
DxgkPagingFenceQueueCoreAbort(
    _Inout_ PDXGK_PAGING_FENCE_TRANSACTION Transaction);

_IRQL_requires_(PASSIVE_LEVEL)
VOID
DxgkPagingFenceQueueCoreShutDown(
    _Inout_ PDXGK_PAGING_FENCE_QUEUE_CORE Queue);

_IRQL_requires_(PASSIVE_LEVEL)
ULONGLONG
DxgkPagingFenceQueueCoreQueryCommitted(
    _Inout_ PDXGK_PAGING_FENCE_QUEUE_CORE Queue);

#endif /* _DXGK_PAGING_CORE_H_ */
