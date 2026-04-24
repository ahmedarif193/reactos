/*
 * PROJECT:     ReactOS SD Port Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DMA and PIO transfer setup for SD/eMMC data operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *******************************************************************/

#include "sdport.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

static
VOID
SdPortFreeTransferMdl(
    _Inout_ PSDPORT_REQUEST Request)
{
    if (Request->DataMdl != NULL)
    {
        MmUnlockPages(Request->DataMdl);
        IoFreeMdl(Request->DataMdl);
        Request->DataMdl = NULL;
    }
}

static
NTSTATUS
SdPortLockTransferBuffer(
    _Inout_ PSDPORT_REQUEST Request,
    _In_ PVOID Buffer,
    _In_ SIZE_T Length)
{
    PMDL Mdl;

    Request->DataBuffer = Buffer;

    if (Length > MAXULONG)
    {
        DPRINT1("SdPortLockTransferBuffer: Length 0x%Ix exceeds MAXULONG\n",
                Length);
        return STATUS_INVALID_PARAMETER;
    }

    Mdl = IoAllocateMdl(Buffer, (ULONG)Length, FALSE, FALSE, NULL);
    if (Mdl == NULL)
    {
        DPRINT1("SdPortLockTransferBuffer: IoAllocateMdl failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    __try
    {
        MmProbeAndLockPages(Mdl,
                            KernelMode,
                            (Request->Command.TransferDirection == SDTD_WRITE) ?
                            IoReadAccess : IoWriteAccess);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        IoFreeMdl(Mdl);
        DPRINT1("SdPortLockTransferBuffer: MmProbeAndLockPages failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    __endtry;

    Request->DataMdl = Mdl;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SdPortBuildAdma2Descriptors(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ SIZE_T Length)
{
    PSDHCI_ADMA2_DESCRIPTOR_32 DescTable;
    PPFN_NUMBER PfnArray;
    ULONG ByteOffset;
    SIZE_T Remaining;
    ULONG DescIndex;
    ULONG PageIndex;
    ULONG PageCount;
    ULONG MaxDesc;

    DescTable = (PSDHCI_ADMA2_DESCRIPTOR_32)SlotExtension->AdmaDescriptorBuffer;
    MaxDesc = SlotExtension->AdmaDescriptorSize / sizeof(SDHCI_ADMA2_DESCRIPTOR_32);

    RtlZeroMemory(DescTable, SlotExtension->AdmaDescriptorSize);

    PfnArray = MmGetMdlPfnArray(Request->DataMdl);
    ByteOffset = MmGetMdlByteOffset(Request->DataMdl);
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Request->DataMdl),
                                               Length);
    Remaining = Length;
    DescIndex = 0;
    PageIndex = 0;

    while (Remaining > 0 && DescIndex < MaxDesc && PageIndex < PageCount)
    {
        ULONGLONG PhysAddr64;
        ULONG PhysAddr;
        ULONG Offset;
        SIZE_T SegmentLength;
        ULONG PerDescCap;
        ULONG BoundaryCap;

        Offset = (PageIndex == 0) ? ByteOffset : 0;
        PhysAddr64 = ((ULONGLONG)PfnArray[PageIndex] << PAGE_SHIFT) + Offset;
        if (PhysAddr64 > 0xFFFFFFFFULL)
        {
            DPRINT1("SdPortBuildAdma2Descriptors: address above 4GB not supported\n");
            return STATUS_NOT_SUPPORTED;
        }

        PhysAddr = (ULONG)PhysAddr64;

        BoundaryCap = SDHCI_ADMA2_MAX_LENGTH - (PhysAddr & 0xFFFF);
        PerDescCap = (SDHCI_ADMA2_MAX_LENGTH < BoundaryCap) ?
                     SDHCI_ADMA2_MAX_LENGTH : BoundaryCap;

        SegmentLength = (SIZE_T)(PAGE_SIZE - Offset);
        PageIndex++;

        while (PageIndex < PageCount &&
               (SegmentLength + PAGE_SIZE) <= PerDescCap &&
               SegmentLength < Remaining)
        {
            if (PfnArray[PageIndex] != (PfnArray[PageIndex - 1] + 1))
            {
                break;
            }

            SegmentLength += PAGE_SIZE;
            PageIndex++;
        }

        if (SegmentLength > Remaining)
        {
            SegmentLength = Remaining;
        }

        if (SegmentLength > PerDescCap)
        {
            SegmentLength = PerDescCap;
        }

        DescTable[DescIndex].Attributes = SDHCI_ADMA2_ACT_TRAN | SDHCI_ADMA2_VALID;
        DescTable[DescIndex].Length = (USHORT)SegmentLength;
        DescTable[DescIndex].Address = PhysAddr;

        Remaining -= SegmentLength;
        DescIndex++;
    }

    if (Remaining != 0 || PageIndex != PageCount)
    {
        DPRINT1("SdPortBuildAdma2Descriptors: descriptor table exhausted (0x%Ix bytes remain)\n",
                Remaining);
        return STATUS_BUFFER_OVERFLOW;
    }

    if (DescIndex == 0)
    {
        DPRINT1("SdPortBuildAdma2Descriptors: no descriptors built\n");
        return STATUS_UNSUCCESSFUL;
    }

    DescTable[DescIndex - 1].Attributes |= (SDHCI_ADMA2_END | SDHCI_ADMA2_INT);
    return STATUS_SUCCESS;
}

/**
 * @brief Configure a request for PIO data transfer.
 *
 * In PIO mode, the port driver reads/writes data through the host
 * controller's buffer data port register one block at a time,
 * triggered by buffer-ready interrupts. This function allocates an MDL
 * for the data buffer and locks the pages in memory.
 *
 * @param[in]     SlotExtension  Pointer to the slot extension (unused, reserved).
 * @param[in,out] Request        The request to configure with PIO transfer state.
 * @param[in]     Buffer         Pointer to the data buffer.
 * @param[in]     Length         Total transfer length in bytes.
 *
 * @return STATUS_SUCCESS on success, STATUS_INVALID_PARAMETER if the buffer
 */
NTSTATUS
SdPortSetupPioTransfer(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ PVOID Buffer,
    _In_ SIZE_T Length)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(SlotExtension);

    if (Buffer == NULL || Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Request->DataBuffer = Buffer;
    Request->DataMdl = NULL;
    Request->UseAdma = FALSE;
    Request->AdmaDescriptorTable = NULL;
    Request->BytesTransferred = 0;

    Request->PioBytesDone = 0;
    Request->PioBytesPerBlock = Request->BlockSize;

    Status = SdPortLockTransferBuffer(Request, Buffer, Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    DPRINT1("SdPortSetupPioTransfer: Buffer=%p Length=0x%Ix\n", Buffer, Length);
    return STATUS_SUCCESS;
}

/**
 * @brief Configure a request for ADMA2 DMA data transfer.
 *
 * Builds an ADMA2 descriptor table from the data buffer's physical page
 * layout. Adjacent physically contiguous pages are coalesced into single
 *
 * @param[in]     SlotExtension  Pointer to the slot extension with ADMA buffers.
 * @param[in,out] Request        The request to configure with DMA transfer state.
 * @param[in]     Buffer         Pointer to the data buffer.
 * @param[in]     Length         Total transfer length in bytes.
 *
 * @return STATUS_SUCCESS on success, STATUS_INVALID_PARAMETER if the buffer
 */
NTSTATUS
SdPortSetupDmaTransfer(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ PVOID Buffer,
    _In_ SIZE_T Length)
{
    NTSTATUS Status;

    if (Buffer == NULL || Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (SlotExtension->AdmaDescriptorBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Request->DataMdl = NULL;
    Request->BytesTransferred = 0;

    Status = SdPortLockTransferBuffer(Request, Buffer, Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SdPortBuildAdma2Descriptors(SlotExtension, Request, Length);
    if (!NT_SUCCESS(Status))
    {
        SdPortFreeTransferMdl(Request);
        return Status;
    }

    Request->UseAdma = TRUE;
    Request->AdmaDescriptorTable = SlotExtension->AdmaDescriptorBuffer;
    Request->AdmaDescriptorPhysical = SlotExtension->AdmaDescriptorPhysical;
    Request->DataBuffer = Buffer;

    DPRINT1("SdPortSetupDmaTransfer: Built ADMA descriptors for 0x%Ix bytes\n",
            Length);

    return STATUS_SUCCESS;
}

/**
 * @brief Allocate a physically contiguous buffer for ADMA2 descriptors.
 *
 * The buffer must be physically contiguous because the SDHCI controller
 * reads the descriptor table via DMA using a single physical base address.
 * Allocates enough space for SDPORT_MAX_ADMA2_DESCRIPTORS entries (8 bytes
 * each in the 32-bit ADMA2 descriptor format).
 *
 * @param[in,out] SlotExtension  Pointer to the slot extension; on success,
 *                               AdmaDescriptorBuffer, AdmaDescriptorPhysical,
 *                               and AdmaDescriptorSize are populated.
 *
 * @return STATUS_SUCCESS on success, or STATUS_INSUFFICIENT_RESOURCES if the
 *         contiguous memory allocation fails.
 */
NTSTATUS
SdPortAllocateAdmaDescriptors(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension)
{
    ULONG Size;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS BoundaryAddress;

    Size = SDPORT_MAX_ADMA2_DESCRIPTORS * sizeof(SDHCI_ADMA2_DESCRIPTOR_32);

    /*
     * ADMA2 descriptor table must be aligned to a 4-byte boundary.
     * MmAllocateContiguousMemorySpecifyCache returns page-aligned
     * memory, which satisfies this requirement.
     */
    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = 0xFFFFFFFF;   /* 32-bit addressable */
    BoundaryAddress.QuadPart = 0;        /* No boundary crossing restriction */

    SlotExtension->AdmaDescriptorBuffer =
        MmAllocateContiguousMemorySpecifyCache(Size,
                                                LowAddress,
                                                HighAddress,
                                                BoundaryAddress,
                                                MmNonCached);

    if (SlotExtension->AdmaDescriptorBuffer == NULL)
    {
        DPRINT1("SdPortAllocateAdmaDescriptors: Allocation failed (%lu bytes)\n",
                Size);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(SlotExtension->AdmaDescriptorBuffer, Size);

    SlotExtension->AdmaDescriptorPhysical =
        MmGetPhysicalAddress(SlotExtension->AdmaDescriptorBuffer);
    SlotExtension->AdmaDescriptorSize = Size;

    DPRINT1("SdPortAllocateAdmaDescriptors: Allocated %lu bytes at %p (phys 0x%08lx)\n",
           Size,
           SlotExtension->AdmaDescriptorBuffer,
           SlotExtension->AdmaDescriptorPhysical.LowPart);

    return STATUS_SUCCESS;
}

/**
 * @brief Free the ADMA2 descriptor buffer for a slot.
 *
 * Frees the physically contiguous ADMA2 descriptor buffer previously
 * allocated by SdPortAllocateAdmaDescriptors and resets the related
 * fields in the slot extension. Safe to call if no buffer was allocated.
 *
 * @param[in,out] SlotExtension  Pointer to the slot extension whose ADMA
 *                               descriptor buffer is freed.
 */
VOID
SdPortFreeAdmaDescriptors(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension)
{
    if (SlotExtension->AdmaDescriptorBuffer != NULL)
    {
        MmFreeContiguousMemory(SlotExtension->AdmaDescriptorBuffer);
        SlotExtension->AdmaDescriptorBuffer = NULL;
        SlotExtension->AdmaDescriptorPhysical.QuadPart = 0;
        SlotExtension->AdmaDescriptorSize = 0;
    }
}

VOID
SdPortEnableManagedPio(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension)
{
    if (SlotExtension != NULL)
    {
        SlotExtension->UsePortPio = TRUE;
    }
}
