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

#endif /* _DXGK_PAGING_CORE_H_ */
