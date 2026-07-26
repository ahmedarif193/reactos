/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DMA buffer ring, allocation lists and patch-location validation
 */

#include "dma_core.h"

NTSTATUS
DxgkDmaCoreRingInitialize(
    _Out_ PDXGK_DMA_RING Ring,
    _In_ ULONGLONG CapacityInBytes,
    _In_ ULONG MaxPrivateDataSize)
{
    RtlZeroMemory(Ring, sizeof(*Ring));
    if (CapacityInBytes == 0)
        return STATUS_INVALID_PARAMETER;
    Ring->CapacityInBytes = CapacityInBytes;
    Ring->MaxPrivateDataSize = MaxPrivateDataSize;
    Ring->Initialized = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkDmaCoreRingSetSubmission(
    _Inout_ PDXGK_DMA_RING Ring,
    _In_ ULONGLONG StartOffset,
    _In_ ULONGLONG EndOffset,
    _In_ ULONG PrivateDataSize)
{
    if (!Ring->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    /* The submission window must be a forward span inside the buffer.  An
     * inverted or out-of-bounds window would hand the miniport a length it
     * computes by subtraction and then reads past the mapping. */
    if (EndOffset < StartOffset)
        return STATUS_INVALID_PARAMETER;
    if (EndOffset > Ring->CapacityInBytes)
        return STATUS_INVALID_PARAMETER;
    if (PrivateDataSize > Ring->MaxPrivateDataSize)
        return STATUS_INVALID_PARAMETER;
    Ring->SubmissionStartOffset = StartOffset;
    Ring->SubmissionEndOffset = EndOffset;
    Ring->PrivateDataSize = PrivateDataSize;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkDmaCoreRingSubmissionLength(
    _In_ const DXGK_DMA_RING *Ring,
    _Out_ PULONGLONG Length)
{
    *Length = 0;
    if (!Ring->Initialized || Ring->SubmissionEndOffset < Ring->SubmissionStartOffset)
        return FALSE;
    *Length = Ring->SubmissionEndOffset - Ring->SubmissionStartOffset;
    return TRUE;
}

VOID
DxgkDmaCoreAllocationListInitialize(
    _Out_ PDXGK_DMA_ALLOCATION_LIST List)
{
    RtlZeroMemory(List, sizeof(*List));
}

NTSTATUS
DxgkDmaCoreAllocationListAdd(
    _Inout_ PDXGK_DMA_ALLOCATION_LIST List,
    _In_ ULONGLONG AllocationCookie,
    _In_ ULONGLONG SizeInBytes,
    _In_ ULONG SegmentId,
    _In_ BOOLEAN WriteOperation,
    _Out_ PULONG OutIndex)
{
    ULONG Existing;

    *OutIndex = 0;
    if (AllocationCookie == 0 || SizeInBytes == 0)
        return STATUS_INVALID_PARAMETER;
    if (List->Count >= DXGK_DMA_CORE_MAX_ALLOCATIONS)
        return STATUS_INSUFFICIENT_RESOURCES;
    /* One entry per allocation: two entries would let the same memory be
     * patched under two different residency decisions in one submission. */
    if (DxgkDmaCoreAllocationListFind(List, AllocationCookie, &Existing))
        return STATUS_OBJECT_NAME_COLLISION;
    List->Entries[List->Count].AllocationCookie = AllocationCookie;
    List->Entries[List->Count].SizeInBytes = SizeInBytes;
    List->Entries[List->Count].SegmentId = SegmentId;
    List->Entries[List->Count].WriteOperation = WriteOperation;
    *OutIndex = List->Count;
    List->Count++;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkDmaCoreAllocationListFind(
    _In_ const DXGK_DMA_ALLOCATION_LIST *List,
    _In_ ULONGLONG AllocationCookie,
    _Out_ PULONG OutIndex)
{
    ULONG Index;

    *OutIndex = 0;
    if (AllocationCookie == 0)
        return FALSE;
    for (Index = 0; Index < List->Count; ++Index)
    {
        if (List->Entries[Index].AllocationCookie == AllocationCookie)
        {
            *OutIndex = Index;
            return TRUE;
        }
    }
    return FALSE;
}

NTSTATUS
DxgkDmaCoreValidatePatch(
    _In_ const DXGK_DMA_ALLOCATION_LIST *List,
    _In_ const DXGK_DMA_RING *Ring,
    _In_ const DXGK_DMA_PATCH *Patch,
    _In_ ULONG PointerSize)
{
    const DXGK_DMA_ALLOCATION_REF *Ref;
    ULONGLONG SubmissionLength;
    ULONGLONG PatchEnd;

    if (!Ring->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if (PointerSize == 0)
        return STATUS_INVALID_PARAMETER;
    if (Patch->AllocationIndex >= List->Count)
        return STATUS_INVALID_PARAMETER;

    Ref = &List->Entries[Patch->AllocationIndex];
    /* A patch names a GPU address, so the allocation must actually be
     * somewhere the GPU can reach. */
    if (Ref->SegmentId == 0)
        return STATUS_GRAPHICS_ALLOCATION_CLOSED;

    /* The write must land wholly inside the submitted span of the buffer.
     * DXGK_ALLOCATIONLIST carries no size, so this is the only check. */
    if (!DxgkDmaCoreRingSubmissionLength(Ring, &SubmissionLength))
        return STATUS_INVALID_DEVICE_STATE;
    if (Patch->PatchOffset > SubmissionLength)
        return STATUS_INVALID_PARAMETER;
    PatchEnd = (ULONGLONG)Patch->PatchOffset + PointerSize;
    if (PatchEnd > SubmissionLength)
        return STATUS_INVALID_PARAMETER;

    /* A GPU pointer must be naturally aligned or the GPU reads a torn value. */
    if ((Patch->PatchOffset % PointerSize) != 0)
        return STATUS_DATATYPE_MISALIGNMENT;

    /* The referenced byte must be inside the allocation being pointed at. */
    if (Patch->AllocationOffset >= Ref->SizeInBytes)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

NTSTATUS
DxgkDmaCoreValidatePatchList(
    _In_ const DXGK_DMA_ALLOCATION_LIST *List,
    _In_ const DXGK_DMA_RING *Ring,
    _In_reads_(PatchCount) const DXGK_DMA_PATCH *Patches,
    _In_ ULONG PatchCount,
    _In_ ULONG PointerSize,
    _Out_ PULONG OutFailedIndex)
{
    ULONG Index;

    *OutFailedIndex = 0;
    if (PatchCount > DXGK_DMA_CORE_MAX_PATCHES)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < PatchCount; ++Index)
    {
        NTSTATUS Status = DxgkDmaCoreValidatePatch(List, Ring, &Patches[Index], PointerSize);

        if (!NT_SUCCESS(Status))
        {
            *OutFailedIndex = Index;
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

/* EOF */
