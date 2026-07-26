/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DMA buffer ring, allocation lists and patch-location validation
 *
 * This is the last place a bad number can be caught before the GPU acts on it.
 * DXGK_ALLOCATIONLIST carries no size, so dxgkrnl must bounds-check every
 * patch offset itself: an unchecked patch writes a GPU address into a command
 * buffer at an arbitrary offset.  No dxgkrnl or miniport types.
 */

#ifndef _DXGK_DMA_CORE_H_
#define _DXGK_DMA_CORE_H_

#include <ntddk.h>

#define DXGK_DMA_CORE_MAX_ALLOCATIONS   256
#define DXGK_DMA_CORE_MAX_PATCHES       1024

/* One entry of the allocation list a render submission references. */
typedef struct _DXGK_DMA_ALLOCATION_REF
{
    ULONGLONG AllocationCookie;   /* opaque identity, 0 = empty slot */
    ULONGLONG SizeInBytes;
    ULONG     SegmentId;          /* 0 = not resident */
    BOOLEAN   WriteOperation;
} DXGK_DMA_ALLOCATION_REF, *PDXGK_DMA_ALLOCATION_REF;

typedef struct _DXGK_DMA_ALLOCATION_LIST
{
    DXGK_DMA_ALLOCATION_REF Entries[DXGK_DMA_CORE_MAX_ALLOCATIONS];
    ULONG Count;
} DXGK_DMA_ALLOCATION_LIST, *PDXGK_DMA_ALLOCATION_LIST;

/* One patch the miniport asked dxgkrnl to apply to the command buffer. */
typedef struct _DXGK_DMA_PATCH
{
    ULONG     AllocationIndex;    /* index into the allocation list */
    ULONG     PatchOffset;        /* byte offset within the DMA buffer */
    ULONGLONG AllocationOffset;   /* byte offset within the allocation */
    ULONG     SlotId;
} DXGK_DMA_PATCH, *PDXGK_DMA_PATCH;

/* A per-context command ring mapped to user mode. */
typedef struct _DXGK_DMA_RING
{
    ULONGLONG CapacityInBytes;
    ULONGLONG SubmissionStartOffset;
    ULONGLONG SubmissionEndOffset;
    ULONG     PrivateDataSize;
    ULONG     MaxPrivateDataSize;
    BOOLEAN   Initialized;
} DXGK_DMA_RING, *PDXGK_DMA_RING;

NTSTATUS DxgkDmaCoreRingInitialize(_Out_ PDXGK_DMA_RING Ring, _In_ ULONGLONG CapacityInBytes, _In_ ULONG MaxPrivateDataSize);
NTSTATUS DxgkDmaCoreRingSetSubmission(_Inout_ PDXGK_DMA_RING Ring, _In_ ULONGLONG StartOffset, _In_ ULONGLONG EndOffset, _In_ ULONG PrivateDataSize);
BOOLEAN DxgkDmaCoreRingSubmissionLength(_In_ const DXGK_DMA_RING *Ring, _Out_ PULONGLONG Length);

VOID DxgkDmaCoreAllocationListInitialize(_Out_ PDXGK_DMA_ALLOCATION_LIST List);
NTSTATUS DxgkDmaCoreAllocationListAdd(_Inout_ PDXGK_DMA_ALLOCATION_LIST List, _In_ ULONGLONG AllocationCookie, _In_ ULONGLONG SizeInBytes, _In_ ULONG SegmentId, _In_ BOOLEAN WriteOperation, _Out_ PULONG OutIndex);
/* A cookie may legally appear once; a duplicate would let one allocation be
 * patched under two different residency decisions. */
BOOLEAN DxgkDmaCoreAllocationListFind(_In_ const DXGK_DMA_ALLOCATION_LIST *List, _In_ ULONGLONG AllocationCookie, _Out_ PULONG OutIndex);

/* Validates one patch against the buffer it writes into and the allocation it
 * references.  PointerSize is the width of the GPU address being written. */
NTSTATUS DxgkDmaCoreValidatePatch(_In_ const DXGK_DMA_ALLOCATION_LIST *List, _In_ const DXGK_DMA_RING *Ring, _In_ const DXGK_DMA_PATCH *Patch, _In_ ULONG PointerSize);
/* Validates a whole patch list, reporting the first offender. */
NTSTATUS DxgkDmaCoreValidatePatchList(_In_ const DXGK_DMA_ALLOCATION_LIST *List, _In_ const DXGK_DMA_RING *Ring, _In_reads_(PatchCount) const DXGK_DMA_PATCH *Patches, _In_ ULONG PatchCount, _In_ ULONG PointerSize, _Out_ PULONG OutFailedIndex);

#endif /* _DXGK_DMA_CORE_H_ */
