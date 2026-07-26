/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Paging operation validation and the multipass build state machine
 */

#include "paging_core.h"

NTSTATUS
DxgkPagingCoreValidate(
    _In_ const DXGK_PAGING_CORE_REQUEST *Request,
    _In_ ULONG PageTableLevelCount)
{
    if (Request->Operation == DxgkPagingCoreOpNone || Request->Operation >= DxgkPagingCoreOpMax)
        return STATUS_INVALID_PARAMETER;

    switch (Request->Operation)
    {
        case DxgkPagingCoreOpTransfer:
            if (Request->TransferSize == 0)
                return STATUS_INVALID_PARAMETER;
            /* A transfer between the same segment at the same place is not a
             * copy; and one side must be real memory the CPU can reach. */
            if (Request->SourceSegmentId == Request->DestinationSegmentId)
                return STATUS_INVALID_PARAMETER;
            if (Request->SourceSegmentId == 0 && Request->DestinationSegmentId == 0)
                return STATUS_INVALID_PARAMETER;
            /* Segment id 0 means system memory, which must actually exist. */
            if ((Request->SourceSegmentId == 0 || Request->DestinationSegmentId == 0) &&
                !Request->SystemMemoryPresent)
                return STATUS_INVALID_PARAMETER;
            return STATUS_SUCCESS;

        case DxgkPagingCoreOpFill:
            if (Request->TransferSize == 0)
                return STATUS_INVALID_PARAMETER;
            if (Request->DestinationSegmentId == 0)
                return STATUS_INVALID_PARAMETER;
            return STATUS_SUCCESS;

        case DxgkPagingCoreOpDiscardContent:
        case DxgkPagingCoreOpNotifyResidency:
            return STATUS_SUCCESS;

        case DxgkPagingCoreOpMapAperture:
        case DxgkPagingCoreOpUnmapAperture:
            /* Aperture segments are 1-based; segment 0 is system memory and
             * has no aperture to map into. */
            if (Request->DestinationSegmentId == 0)
                return STATUS_INVALID_PARAMETER;
            if (Request->Operation == DxgkPagingCoreOpMapAperture && !Request->SystemMemoryPresent)
                return STATUS_INVALID_PARAMETER;
            return STATUS_SUCCESS;

        case DxgkPagingCoreOpUpdatePageTable:
            if (Request->PageTableLevel >= PageTableLevelCount)
                return STATUS_INVALID_PARAMETER;
            if (Request->SizeInBytes == 0)
                return STATUS_INVALID_PARAMETER;
            if (Request->GpuVirtualAddress > MAXULONGLONG - Request->SizeInBytes)
                return STATUS_INTEGER_OVERFLOW;
            return STATUS_SUCCESS;

        case DxgkPagingCoreOpFlushTlb:
            /* A zero-length flush would leave stale translations behind while
             * reporting success. */
            if (Request->SizeInBytes == 0)
                return STATUS_INVALID_PARAMETER;
            if (Request->GpuVirtualAddress > MAXULONGLONG - Request->SizeInBytes)
                return STATUS_INTEGER_OVERFLOW;
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
DxgkPagingCoreMultipassBegin(
    _Out_ PDXGK_PAGING_CORE_MULTIPASS Pass,
    _In_ ULONGLONG TotalBytes)
{
    RtlZeroMemory(Pass, sizeof(*Pass));
    if (TotalBytes == 0)
        return STATUS_INVALID_PARAMETER;
    Pass->TotalBytes = TotalBytes;
    Pass->Started = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkPagingCoreMultipassIsFirst(
    _In_ const DXGK_PAGING_CORE_MULTIPASS Pass[1])
{
    return Pass->Started && Pass->PassCount == 0;
}

NTSTATUS
DxgkPagingCoreMultipassAdvance(
    _Inout_ PDXGK_PAGING_CORE_MULTIPASS Pass,
    _In_ NTSTATUS MiniportStatus,
    _In_ ULONGLONG BytesProcessed)
{
    if (!Pass->Started || Pass->Complete)
        return STATUS_INVALID_DEVICE_STATE;
    if (Pass->PassCount >= DXGK_PAGING_CORE_MAX_PASSES)
        return STATUS_DEVICE_BUSY;
    if (BytesProcessed > Pass->TotalBytes - Pass->MultipassOffset)
        return STATUS_INVALID_PARAMETER;

    Pass->PassCount++;
    Pass->MultipassOffset += BytesProcessed;

    if (MiniportStatus == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER)
    {
        /*
         * The miniport wants another pass.  It must have consumed something,
         * or the loop would ask for the same bytes forever.
         */
        if (BytesProcessed == 0)
            return STATUS_DEVICE_PROTOCOL_ERROR;
        if (Pass->MultipassOffset >= Pass->TotalBytes)
        {
            /* Nothing left, so there is nothing for another pass to do. */
            Pass->Complete = TRUE;
            return STATUS_SUCCESS;
        }
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    if (!NT_SUCCESS(MiniportStatus))
        return MiniportStatus;

    /* A success that left bytes behind is a protocol violation: the miniport
     * must either finish or ask for another pass. */
    if (Pass->MultipassOffset != Pass->TotalBytes)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    Pass->Complete = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkPagingCoreMultipassMayEmitFence(
    _In_ const DXGK_PAGING_CORE_MULTIPASS Pass[1])
{
    return Pass->Complete && !Pass->FenceEmitted;
}

NTSTATUS
DxgkPagingCoreMultipassEmitFence(
    _Inout_ PDXGK_PAGING_CORE_MULTIPASS Pass)
{
    /*
     * Signalling before the last pass would tell the waiter the copy finished
     * while the tail of it is still outstanding.
     */
    if (!Pass->Complete)
        return STATUS_INVALID_DEVICE_STATE;
    if (Pass->FenceEmitted)
        return STATUS_INVALID_DEVICE_STATE;
    Pass->FenceEmitted = TRUE;
    return STATUS_SUCCESS;
}

/* EOF */
