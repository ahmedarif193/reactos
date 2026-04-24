/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SDBUS_REQUEST_PACKET processing and SDHCI command execution
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#define NDEBUG
#include <debug.h>

#define SD_MAX_RETRIES 3

static BOOLEAN
SdBusIsRetryableError(
    _In_ NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_SD_CMD_CRC_ERROR:
        case STATUS_SD_DATA_CRC_ERROR:
        case STATUS_SD_CMD_TIMEOUT:
        case STATUS_SD_DATA_TIMEOUT:
        case STATUS_IO_TIMEOUT:
            return TRUE;
        default:
            return FALSE;
    }
}

/**
 * @brief Wait for specific SDHCI interrupt bits via the ISR-driven event.
 *
 * Replaces all KeStallExecutionProcessor polling loops. The ISR acknowledges
 * interrupt status bits, accumulates them in CommandInterruptStatus, and
 * signals CommandEvent. This function waits for the desired bits (or errors)
 * and returns the matched bits. Only consumed bits are cleared from the
 * accumulator.
 *
 * @param[in]  FdoExtension  Host controller FDO extension.
 * @param[in]  WantedBits    Interrupt status bits to wait for (e.g. CMD_COMPLETE).
 * @param[in]  ErrorBits     Error bits that should also terminate the wait.
 * @param[in]  TimeoutMs     Maximum wait time in milliseconds.
 * @param[out] ResultBits    Receives the matched bits on success or error.
 *
 * @return STATUS_SUCCESS if WantedBits matched, STATUS_IO_DEVICE_ERROR if
 *         ErrorBits matched, STATUS_IO_TIMEOUT on timeout.
 */
NTSTATUS
SdBusWaitForInterrupt(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG WantedBits,
    _In_ ULONG ErrorBits,
    _In_ ULONG TimeoutMs,
    _Out_ PULONG ResultBits)
{
    LARGE_INTEGER Timeout;
    Timeout.QuadPart = -(LONGLONG)TimeoutMs * 10000; /* ms -> 100ns, relative */

    for (;;)
    {
        ULONG Bits = (ULONG)FdoExtension->CommandInterruptStatus; /* volatile read */
        ULONG Match = Bits & (WantedBits | ErrorBits);

        if (Match)
        {
            InterlockedAnd(&FdoExtension->CommandInterruptStatus, ~(LONG)Match);
            *ResultBits = Match;
            if (Match & ErrorBits)
            {
                return STATUS_IO_DEVICE_ERROR;
            }
            return STATUS_SUCCESS;
        }

        {
            NTSTATUS Status = KeWaitForSingleObject(
                &FdoExtension->CommandEvent,
                Executive, KernelMode, FALSE, &Timeout);
            if (Status == STATUS_TIMEOUT)
            {
                *ResultBits = 0;
                return STATUS_IO_TIMEOUT;
            }
        }
    }
}

/**
 * @brief Map an SD_RESPONSE_TYPE enum to SDHCI command register flags.
 *
 * Translates the abstract response type (R1, R1b, R2, R3, etc.) into the
 * corresponding SDHCI response length, CRC check, and index check bits.
 *
 * @param[in] ResponseType  The SD response type to translate.
 *
 * @return SDHCI command register flags for the given response type.
 */
static USHORT
SdBusResponseTypeToFlags(
    _In_ SD_RESPONSE_TYPE ResponseType)
{
    switch (ResponseType)
    {
        case SDRT_NONE:
            return SDHCI_CMD_RESP_NONE;

        case SDRT_1:
        case SDRT_5:
        case SDRT_6:
            return SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK;

        case SDRT_1B:
        case SDRT_5B:
            return SDHCI_CMD_RESP_48_BUSY | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK;

        case SDRT_2:
            return SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC_CHECK;

        case SDRT_3:
        case SDRT_4:
            return SDHCI_CMD_RESP_48;

        default:
            return SDHCI_CMD_RESP_NONE;
    }
}

static NTSTATUS
SdBusWaitForBusyRelease(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex)
{
    ULONG Timeout;

    Timeout = SD_DATA_TIMEOUT_MS * 100;
    while (Timeout > 0)
    {
        if (!(SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE) &
              SDHCI_PS_DATA_INHIBIT))
        {
            return STATUS_SUCCESS;
        }

        KeStallExecutionProcessor(10);
        Timeout--;
    }

    DPRINT1("SdBusWaitForBusyRelease: CMD%u timed out waiting for busy release\n",
            CommandIndex);
    SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
    return STATUS_IO_TIMEOUT;
}

static
VOID
SdBusDelayOneMillisecond(
    VOID)
{
    LARGE_INTEGER Delay;

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        KeStallExecutionProcessor(1000);
        return;
    }

    Delay.QuadPart = -10000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

static
NTSTATUS
SdBusHandleMmcSoftReset(
    _In_ PFDO_EXTENSION FdoExtension)
{
    ULONG Timeout;

    SdBusWriteReg8(FdoExtension,
                   SDHCI_SOFTWARE_RESET,
                   SDHCI_RESET_CMD | SDHCI_RESET_DATA);

    Timeout = SD_RESET_TIMEOUT_MS;
    while (Timeout > 0)
    {
        UCHAR ResetVal;

        ResetVal = SdBusReadReg8(FdoExtension, SDHCI_SOFTWARE_RESET);
        if (!(ResetVal & (SDHCI_RESET_CMD | SDHCI_RESET_DATA)))
        {
            return STATUS_SUCCESS;
        }

        SdBusDelayOneMillisecond();
        Timeout--;
    }

    DPRINT1("SdBusHandleMmcSoftReset: controller reset timed out\n");
    return STATUS_IO_TIMEOUT;
}

/**
 * @brief Build the SDHCI transfer mode register value from a command descriptor.
 *
 * Sets data direction, multi-block, block-count enable, and auto-CMD12 bits
 * based on the transfer type and direction in the command descriptor.
 *
 * @param[in] CmdDesc     Pointer to the command descriptor specifying transfer type and direction.
 * @param[in] DataLength  The data transfer length in bytes (0 for command-only).
 *
 * @return The 16-bit SDHCI transfer mode register value, or 0 for no data.
 */
static USHORT
SdBusBuildTransferMode(
    _In_ PSDCMD_DESCRIPTOR CmdDesc,
    _In_ ULONG DataLength,
    _In_ BOOLEAN UseDma)
{
    USHORT TransferMode = 0;

    if (DataLength == 0 || CmdDesc->TransferType == SDTT_CMD_ONLY)
    {
        return 0;
    }

    /* Enable DMA for this transfer when using ADMA2 or SDMA */
    if (UseDma)
    {
        TransferMode |= SDHCI_TRNS_DMA_ENABLE;
    }

    /* Data direction */
    if (CmdDesc->TransferDirection == SDTD_READ)
    {
        TransferMode |= SDHCI_TRNS_DATA_DIR_READ;
    }

    /* Multi-block */
    if (CmdDesc->TransferType == SDTT_MULTI_BLOCK)
    {
        TransferMode |= SDHCI_TRNS_MULTI_BLOCK | SDHCI_TRNS_BLK_CNT_ENABLE |
                         SDHCI_TRNS_AUTO_CMD12;
    }
    else if (CmdDesc->TransferType == SDTT_MULTI_BLOCK_NO_CMD12)
    {
        TransferMode |= SDHCI_TRNS_MULTI_BLOCK | SDHCI_TRNS_BLK_CNT_ENABLE;
    }
    else
    {
        /* Single block */
        TransferMode |= SDHCI_TRNS_BLK_CNT_ENABLE;
    }

    return TransferMode;
}

/**
 * @brief Detailed result for ADMA2 descriptor table construction.
 */
typedef enum _SD_ADMA2_BUILD_STATUS
{
    SdAdma2BuildSuccess = 0,
    SdAdma2BuildAddressTooHigh,
    SdAdma2BuildDescriptorOverflow,
    SdAdma2BuildMapFailed
} SD_ADMA2_BUILD_STATUS;

/**
 * @brief Synchronization context for asynchronous SG map callback delivery.
 */
typedef struct _SD_ADMA2_SG_CONTEXT
{
    KEVENT Event;
    PSCATTER_GATHER_LIST ScatterGatherList;
    NTSTATUS Status;
} SD_ADMA2_SG_CONTEXT, *PSD_ADMA2_SG_CONTEXT;

typedef enum _SD_TRANSFER_PATH
{
    SdTransferPathNone = 0,
    SdTransferPathPio,
    SdTransferPathSdma,
    SdTransferPathAdma2,
    SdTransferPathAdma2Bounce
} SD_TRANSFER_PATH;

static BOOLEAN
SdBusTransferPathUsesDma(
    _In_ SD_TRANSFER_PATH TransferPath)
{
    return TransferPath == SdTransferPathSdma ||
           TransferPath == SdTransferPathAdma2 ||
           TransferPath == SdTransferPathAdma2Bounce;
}

static BOOLEAN
SdBusTransferPathUsesAdma2(
    _In_ SD_TRANSFER_PATH TransferPath)
{
    return TransferPath == SdTransferPathAdma2 ||
           TransferPath == SdTransferPathAdma2Bounce;
}

static BOOLEAN
SdBusTransferPathUsesBounce(
    _In_ SD_TRANSFER_PATH TransferPath)
{
    return TransferPath == SdTransferPathSdma ||
           TransferPath == SdTransferPathAdma2Bounce;
}

static const char *
SdBusTransferPathName(
    _In_ SD_TRANSFER_PATH TransferPath)
{
    switch (TransferPath)
    {
        case SdTransferPathAdma2: return "ADMA2";
        case SdTransferPathAdma2Bounce: return "ADMA2_BOUNCE";
        case SdTransferPathSdma: return "SDMA";
        case SdTransferPathPio: return "PIO";
        default: return "NODATA";
    }
}

/**
 * @brief Completion callback for GetScatterGatherList mapping.
 *
 * Captures the mapped scatter/gather list and signals a waiting thread.
 *
 * @param[in] DeviceObject  DMA target device object (unused).
 * @param[in] Irp           IRP pointer (unused).
 * @param[in] ScatterGather Scatter/gather list returned by DMA subsystem.
 * @param[in] Context       Pointer to SD_ADMA2_SG_CONTEXT.
 */
static VOID
NTAPI
SdBusAdma2ScatterGatherCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PSCATTER_GATHER_LIST ScatterGather,
    _In_ PVOID Context)
{
    PSD_ADMA2_SG_CONTEXT SgContext = (PSD_ADMA2_SG_CONTEXT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    SgContext->ScatterGatherList = ScatterGather;
    SgContext->Status = (ScatterGather != NULL) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    KeSetEvent(&SgContext->Event, IO_NO_INCREMENT, FALSE);
}

/**
 * @brief Call GetScatterGatherList at DISPATCH_LEVEL as required by HAL.
 *
 * The current HAL implementation for scatter/gather mapping asserts
 * DISPATCH_LEVEL in the adapter-channel path. This wrapper raises IRQL
 * only when needed, invokes GetScatterGatherList, then restores IRQL.
 *
 * @param[in] FdoExtension   Pointer to the host controller FDO extension.
 * @param[in] Mdl            MDL describing the data buffer to map.
 * @param[in] DataLength     Number of bytes to map.
 * @param[in] WriteToDevice  TRUE for host->card transfer, FALSE for card->host.
 * @param[in] Callback       Driver list-control callback for SG completion.
 * @param[in] Context        Opaque callback context pointer.
 *
 * @return NTSTATUS from GetScatterGatherList.
 */
static NTSTATUS
SdBusGetScatterGatherListAtDispatch(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PMDL Mdl,
    _In_ ULONG DataLength,
    _In_ BOOLEAN WriteToDevice,
    _In_ PDRIVER_LIST_CONTROL Callback,
    _In_ PVOID Context)
{
    KIRQL OldIrql = PASSIVE_LEVEL;
    KIRQL CurrentIrql;
    NTSTATUS Status;
    BOOLEAN Raised = FALSE;

    CurrentIrql = KeGetCurrentIrql();
    ASSERT(CurrentIrql <= DISPATCH_LEVEL);

    if (CurrentIrql < DISPATCH_LEVEL)
    {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        Raised = TRUE;
    }

    Status = FdoExtension->DmaAdapter->DmaOperations->GetScatterGatherList(
        FdoExtension->DmaAdapter,
        FdoExtension->PhysicalDevice,
        Mdl,
        MmGetMdlVirtualAddress(Mdl),
        DataLength,
        Callback,
        Context,
        WriteToDevice);

    if (Raised)
    {
        KeLowerIrql(OldIrql);
    }

    return Status;
}

/**
 * @brief Call PutScatterGatherList at DISPATCH_LEVEL as required by HAL.
 *
 * The HAL release path also asserts DISPATCH_LEVEL in this tree, so SG
 * list release must run at that IRQL to avoid debug assertions.
 *
 * @param[in] FdoExtension       Pointer to the host controller FDO extension.
 * @param[in] ScatterGatherList  SG list previously returned by HAL mapping.
 * @param[in] WriteToDevice      Original DMA direction used for mapping.
 */
static VOID
SdBusPutScatterGatherListAtDispatch(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PSCATTER_GATHER_LIST ScatterGatherList,
    _In_ BOOLEAN WriteToDevice)
{
    KIRQL OldIrql = PASSIVE_LEVEL;
    KIRQL CurrentIrql;
    BOOLEAN Raised = FALSE;

    CurrentIrql = KeGetCurrentIrql();
    ASSERT(CurrentIrql <= DISPATCH_LEVEL);

    if (CurrentIrql < DISPATCH_LEVEL)
    {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        Raised = TRUE;
    }

    FdoExtension->DmaAdapter->DmaOperations->PutScatterGatherList(
        FdoExtension->DmaAdapter,
        ScatterGatherList,
        WriteToDevice);

    if (Raised)
    {
        KeLowerIrql(OldIrql);
    }
}

static USHORT
SdBusAdma2EncodeLength(
    _In_ ULONG Length)
{
    ASSERT(Length > 0);
    ASSERT(Length <= SDHCI_ADMA2_MAX_LENGTH);

    if (Length == SDHCI_ADMA2_MAX_LENGTH)
    {
        return 0;
    }

    return (USHORT)Length;
}

static ULONG
SdBusAdma2BoundaryCapacity(
    _In_ ULONG PhysicalAddress)
{
    ULONG Offset = PhysicalAddress & (SDHCI_ADMA2_MAX_LENGTH - 1);

    if (Offset == 0)
    {
        return SDHCI_ADMA2_MAX_LENGTH;
    }

    return SDHCI_ADMA2_MAX_LENGTH - Offset;
}

static ULONG
SdBusBuildAdma2TableForPhysicalBuffer(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG DataLength)
{
    PSDHCI_ADMA2_DESCRIPTOR_32 Desc = FdoExtension->Adma2Table;
    ULONGLONG PhysAddr64;
    ULONG PhysAddr;
    ULONG Remaining;
    ULONG DescIndex;

    PhysAddr64 = PhysicalAddress.QuadPart;
    if (PhysAddr64 > (ULONGLONG)0xFFFFFFFF)
    {
        return 0;
    }

    PhysAddr = (ULONG)PhysAddr64;
    Remaining = DataLength;
    DescIndex = 0;

    while (Remaining > 0)
    {
        ULONG SegLen;
        ULONG BoundaryCap;

        if (DescIndex >= FdoExtension->Adma2MaxDescriptors)
        {
            return 0;
        }

        BoundaryCap = SdBusAdma2BoundaryCapacity(PhysAddr);
        SegLen = Remaining;
        if (SegLen > BoundaryCap)
        {
            SegLen = BoundaryCap;
        }
        if (SegLen > SDHCI_ADMA2_MAX_LENGTH)
        {
            SegLen = SDHCI_ADMA2_MAX_LENGTH;
        }

        Desc[DescIndex].Attributes = SDHCI_ADMA2_ACT_TRAN | SDHCI_ADMA2_VALID;
        Desc[DescIndex].Length = SdBusAdma2EncodeLength(SegLen);
        Desc[DescIndex].Address = PhysAddr;

        PhysAddr += SegLen;
        Remaining -= SegLen;
        DescIndex++;
    }

    if (DescIndex > 0)
    {
        Desc[DescIndex - 1].Attributes |= SDHCI_ADMA2_END;
    }

    return DescIndex;
}

/**
 * @brief Build ADMA2 descriptor table from MDL physical pages.
 *
 * Walks the MDL's page frame number (PFN) array, coalescing contiguous
 * physical pages into single ADMA2 transfer descriptors.  Sets the END
 * bit on the last descriptor.  Falls back (returns 0) if any physical
 * page is above 4 GB (32-bit ADMA2 descriptors can't reach it).
 *
 * @param[in]  FdoExtension  Host controller FDO extension with ADMA2 table.
 * @param[in]  Mdl           MDL describing the caller's data buffer.
 * @param[in]  DataLength    Transfer length in bytes.
 * @param[out] BuildStatus   Optional detailed failure reason when return is 0.
 *
 * @return Number of descriptors built, or 0 on failure.
 */
static ULONG
SdBusBuildAdma2Table(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PMDL Mdl,
    _In_ ULONG DataLength,
    _Out_opt_ SD_ADMA2_BUILD_STATUS *BuildStatus)
{
    PSDHCI_ADMA2_DESCRIPTOR_32 Desc = FdoExtension->Adma2Table;
    PPFN_NUMBER PfnArray;
    ULONG ByteOffset;
    ULONG Remaining;
    ULONG DescIndex;
    ULONG PageIndex;
    ULONG PageCount;
    ULONG MaxDesc;

    PfnArray = (PPFN_NUMBER)(Mdl + 1); /* MmGetMdlPfnArray */
    ByteOffset = MmGetMdlByteOffset(Mdl);
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(
        MmGetMdlVirtualAddress(Mdl), DataLength);
    MaxDesc = FdoExtension->Adma2MaxDescriptors;
    Remaining = DataLength;
    DescIndex = 0;
    PageIndex = 0;
    if (BuildStatus != NULL)
    {
        *BuildStatus = SdAdma2BuildSuccess;
    }

    while (Remaining > 0 && DescIndex < MaxDesc && PageIndex < PageCount)
    {
        ULONGLONG PhysAddr64;
        ULONG PhysAddr;
        ULONG Offset;
        ULONG ChunkLen;

        /* Physical address of current page + byte offset within page */
        Offset = (PageIndex == 0) ? ByteOffset : 0;
        PhysAddr64 = ((ULONGLONG)PfnArray[PageIndex] << PAGE_SHIFT) + Offset;

        /* 32-bit ADMA2 can't address above 4 GB */
        if (PhysAddr64 > (ULONGLONG)0xFFFFFFFF)
        {
            if (BuildStatus != NULL)
            {
                *BuildStatus = SdAdma2BuildAddressTooHigh;
            }
            return 0;
        }

        PhysAddr = (ULONG)PhysAddr64;
        ChunkLen = PAGE_SIZE - Offset;
        PageIndex++;

        while (PageIndex < PageCount &&
               ChunkLen < Remaining &&
               ChunkLen < SDHCI_ADMA2_MAX_LENGTH &&
               ChunkLen < SdBusAdma2BoundaryCapacity(PhysAddr))
        {
            if (PfnArray[PageIndex] != PfnArray[PageIndex - 1] + 1)
            {
                break;
            }
            ChunkLen += PAGE_SIZE;
            PageIndex++;
        }

        if (ChunkLen > Remaining)
        {
            ChunkLen = Remaining;
        }

        while (ChunkLen > 0 && DescIndex < MaxDesc)
        {
            ULONG SegLen;
            ULONG BoundaryCap;

            BoundaryCap = SdBusAdma2BoundaryCapacity(PhysAddr);
            SegLen = ChunkLen;
            if (SegLen > BoundaryCap)
            {
                SegLen = BoundaryCap;
            }
            if (SegLen > SDHCI_ADMA2_MAX_LENGTH)
            {
                SegLen = SDHCI_ADMA2_MAX_LENGTH;
            }

            Desc[DescIndex].Attributes = SDHCI_ADMA2_ACT_TRAN | SDHCI_ADMA2_VALID;
            Desc[DescIndex].Length = SdBusAdma2EncodeLength(SegLen);
            Desc[DescIndex].Address = PhysAddr;

            PhysAddr += SegLen;
            ChunkLen -= SegLen;
            Remaining -= SegLen;
            DescIndex++;
        }
    }

    /*
     * Table exhausted before describing the full transfer.
     * Returning success here can corrupt I/O because BlockCount still
     * reflects the full request.
     */
    if (Remaining != 0 || PageIndex != PageCount)
    {
        if (BuildStatus != NULL)
        {
            *BuildStatus = SdAdma2BuildDescriptorOverflow;
        }
        return 0;
    }

    /* Set END bit on last descriptor */
    if (DescIndex > 0)
    {
        Desc[DescIndex - 1].Attributes |= SDHCI_ADMA2_END;
    }

    return DescIndex;
}

/**
 * @brief Build ADMA2 descriptor table using HAL DMA map-register translation.
 *
 * Uses GetScatterGatherList/PutScatterGatherList so a 32-bit ADMA2 controller
 * can still DMA buffers backed by physical pages above 4 GB.
 *
 * @param[in]  FdoExtension       Host controller FDO extension.
 * @param[in]  Mdl                MDL describing transfer buffer.
 * @param[in]  DataLength         Transfer size in bytes.
 * @param[in]  WriteToDevice      TRUE for host->card, FALSE for card->host.
 * @param[out] DescCount          Number of ADMA2 descriptors populated.
 * @param[out] ScatterGatherList  Active SG mapping to release after transfer.
 * @param[out] BuildStatus        Optional detailed failure reason.
 *
 * @return STATUS_SUCCESS on success, error status on failure.
 */
static NTSTATUS
SdBusBuildAdma2TableFromDmaMapping(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PMDL Mdl,
    _In_ ULONG DataLength,
    _In_ BOOLEAN WriteToDevice,
    _Out_ PULONG DescCount,
    _Out_ PSCATTER_GATHER_LIST *ScatterGatherList,
    _Out_opt_ SD_ADMA2_BUILD_STATUS *BuildStatus)
{
    PSDHCI_ADMA2_DESCRIPTOR_32 Desc = FdoExtension->Adma2Table;
    SD_ADMA2_SG_CONTEXT SgContext;
    NTSTATUS Status;
    ULONG ElementIndex;
    ULONG Remaining;
    ULONG MaxDesc;
    ULONG DescIndex;

    if (BuildStatus != NULL)
    {
        *BuildStatus = SdAdma2BuildSuccess;
    }
    *DescCount = 0;
    *ScatterGatherList = NULL;

    if (FdoExtension->DmaAdapter == NULL ||
        FdoExtension->DmaAdapter->DmaOperations == NULL ||
        FdoExtension->DmaAdapter->DmaOperations->GetScatterGatherList == NULL ||
        FdoExtension->DmaAdapter->DmaOperations->PutScatterGatherList == NULL)
    {
        if (BuildStatus != NULL)
        {
            *BuildStatus = SdAdma2BuildMapFailed;
        }
        return STATUS_NOT_SUPPORTED;
    }

    KeInitializeEvent(&SgContext.Event, NotificationEvent, FALSE);
    SgContext.ScatterGatherList = NULL;
    SgContext.Status = STATUS_UNSUCCESSFUL;

    Status = SdBusGetScatterGatherListAtDispatch(FdoExtension,
                                                 Mdl,
                                                 DataLength,
                                                 WriteToDevice,
                                                 SdBusAdma2ScatterGatherCallback,
                                                 &SgContext);
    if (!NT_SUCCESS(Status))
    {
        if (BuildStatus != NULL)
        {
            *BuildStatus = SdAdma2BuildMapFailed;
        }
        return Status;
    }

    KeWaitForSingleObject(&SgContext.Event, Executive, KernelMode, FALSE, NULL);
    if (!NT_SUCCESS(SgContext.Status) || SgContext.ScatterGatherList == NULL)
    {
        if (BuildStatus != NULL)
        {
            *BuildStatus = SdAdma2BuildMapFailed;
        }
        return STATUS_IO_DEVICE_ERROR;
    }

    MaxDesc = FdoExtension->Adma2MaxDescriptors;
    Remaining = DataLength;
    DescIndex = 0;

    for (ElementIndex = 0;
         ElementIndex < SgContext.ScatterGatherList->NumberOfElements &&
         Remaining > 0;
         ElementIndex++)
    {
        ULONGLONG PhysAddr64 = SgContext.ScatterGatherList->Elements[ElementIndex].Address.QuadPart;
        ULONG ElementLength = SgContext.ScatterGatherList->Elements[ElementIndex].Length;

        while (ElementLength > 0 && Remaining > 0)
        {
            ULONG SegLen;

            if (DescIndex >= MaxDesc)
            {
                if (BuildStatus != NULL)
                {
                    *BuildStatus = SdAdma2BuildDescriptorOverflow;
                }
                goto FailReleaseSg;
            }

            if (PhysAddr64 > (ULONGLONG)0xFFFFFFFF)
            {
                if (BuildStatus != NULL)
                {
                    *BuildStatus = SdAdma2BuildAddressTooHigh;
                }
                goto FailReleaseSg;
            }

            SegLen = ElementLength;
            if (SegLen > Remaining)
            {
                SegLen = Remaining;
            }
            if (SegLen > SdBusAdma2BoundaryCapacity((ULONG)PhysAddr64))
            {
                SegLen = SdBusAdma2BoundaryCapacity((ULONG)PhysAddr64);
            }
            if (SegLen > SDHCI_ADMA2_MAX_LENGTH)
            {
                SegLen = SDHCI_ADMA2_MAX_LENGTH;
            }

            Desc[DescIndex].Attributes = SDHCI_ADMA2_ACT_TRAN | SDHCI_ADMA2_VALID;
            Desc[DescIndex].Length = SdBusAdma2EncodeLength(SegLen);
            Desc[DescIndex].Address = (ULONG)PhysAddr64;

            PhysAddr64 += SegLen;
            ElementLength -= SegLen;
            Remaining -= SegLen;
            DescIndex++;
        }
    }

    if (Remaining != 0 || DescIndex == 0)
    {
        if (BuildStatus != NULL)
        {
            *BuildStatus = SdAdma2BuildDescriptorOverflow;
        }
        goto FailReleaseSg;
    }

    Desc[DescIndex - 1].Attributes |= SDHCI_ADMA2_END;
    *DescCount = DescIndex;
    *ScatterGatherList = SgContext.ScatterGatherList;
    return STATUS_SUCCESS;

FailReleaseSg:
    SdBusPutScatterGatherListAtDispatch(FdoExtension,
                                        SgContext.ScatterGatherList,
                                        WriteToDevice);
    return STATUS_BUFFER_TOO_SMALL;
}

/**
 * @brief Execute an SDHCI command with optional ADMA2, SDMA, or PIO data transfer.
 *
 * Waits for inhibit bits to clear, programs block size/count, writes the
 * argument and command registers, waits for command completion, then
 * performs data transfer via ADMA2 scatter-gather (preferred), SDMA bounce
 * buffer (fallback), or block-by-block PIO.
 *
 * @param[in]      FdoExtension  Pointer to the host controller FDO extension.
 * @param[in]      CmdDesc       Pointer to the command descriptor (command index,
 *                                response type, transfer type/direction).
 * @param[in]      Argument      The 32-bit command argument.
 * @param[in,out]  Mdl           Optional MDL describing the data buffer for transfer.
 * @param[in]      DataLength    Length of the data transfer in bytes (0 for command-only).
 * @param[out]     Response      Optional pointer to receive response data (1 or 4 ULONGs).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdBusSendSdhciCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PSDCMD_DESCRIPTOR CmdDesc,
    _In_ ULONG Argument,
    _Inout_opt_ PMDL Mdl,
    _In_ ULONG DataLength,
    _Out_opt_ PULONG Response)
{
    NTSTATUS Status = STATUS_SUCCESS;
    USHORT CommandFlags;
    USHORT TransferMode;
    USHORT CmdReg;
    ULONG BlockCount = 0;
    ULONG BlockSize = 0;
    PVOID DataBuffer = NULL;
    BOOLEAN HasData;
    BOOLEAN IsRead;
    BOOLEAN WriteToDevice;
    SD_TRANSFER_PATH TransferPath;
    SD_ADMA2_BUILD_STATUS Adma2BuildStatus = SdAdma2BuildSuccess;
    ULONG Adma2DescCount = 0;
    PSCATTER_GATHER_LIST ActiveScatterGatherList = NULL;

    HasData = (DataLength > 0 && Mdl != NULL &&
               CmdDesc->TransferType != SDTT_CMD_ONLY);
    IsRead = (CmdDesc->TransferDirection == SDTD_READ);
    WriteToDevice = !IsRead;

    TransferPath = SdTransferPathNone;
    if (HasData)
    {
        if (FdoExtension->UseAdma2)
        {
            TransferPath = SdTransferPathAdma2;
        }
        else if (FdoExtension->UseSdma &&
                 DataLength <= FdoExtension->DmaBufferSize)
        {
            TransferPath = SdTransferPathSdma;
        }
        else
        {
            TransferPath = SdTransferPathPio;
        }
    }

    DPRINT("SdBusSendSdhciCommand: CMD%u Arg=0x%08lx %s Len=%lu %s\n",
           CmdDesc->Cmd, Argument,
           HasData ? (IsRead ? "READ" : "WRITE") : "NODATA",
           DataLength,
           SdBusTransferPathName(TransferPath));

    if (HasData)
    {
        if ((DataLength % SD_DEFAULT_BLOCK_SIZE) != 0)
        {
            DPRINT1("SdBusSendSdhciCommand: Unaligned data length %lu\n", DataLength);
            return STATUS_INVALID_PARAMETER;
        }

        BlockSize = SD_DEFAULT_BLOCK_SIZE;
        BlockCount = DataLength / BlockSize;
        if (BlockCount == 0 || BlockCount > 0xFFFF)
        {
            DPRINT1("SdBusSendSdhciCommand: Invalid block count %lu\n", BlockCount);
            return STATUS_INVALID_PARAMETER;
        }
    }

    /* Step 1: Wait for CMD_INHIBIT (and DATA_INHIBIT if data transfer) to clear.
     * This is a hardware-readiness poll, not a completion wait. The previous
     * command's inhibit clears within a few clock cycles. */
    {
        ULONG InhibitTimeout = SD_CMD_TIMEOUT_MS * 100; /* ~10µs per iteration */
        while (InhibitTimeout > 0)
        {
            ULONG PresentState = SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE);
            ULONG InhibitMask = SDHCI_PS_CMD_INHIBIT;

            if (HasData)
            {
                InhibitMask |= SDHCI_PS_DATA_INHIBIT;
            }

            if (!(PresentState & InhibitMask))
            {
                break;
            }

            KeStallExecutionProcessor(10);
            InhibitTimeout--;
        }

        if (InhibitTimeout == 0)
        {
            DPRINT1("SdBusSendSdhciCommand: CMD%u inhibit timed out\n", CmdDesc->Cmd);
            return STATUS_IO_TIMEOUT;
        }
    }

    /* Step 2: Clear pending command/data interrupt state for this new command */
    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
    KeClearEvent(&FdoExtension->CommandEvent);
    SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);

    /* Build ADMA2 descriptor table for this transfer */
    if (TransferPath == SdTransferPathAdma2)
    {
        Adma2DescCount = SdBusBuildAdma2Table(FdoExtension,
                                              Mdl,
                                              DataLength,
                                              &Adma2BuildStatus);

        if (Adma2DescCount == 0 &&
            Adma2BuildStatus == SdAdma2BuildAddressTooHigh)
        {
            if (FdoExtension->DmaBuffer != NULL &&
                DataLength <= FdoExtension->DmaBufferSize)
            {
                Adma2DescCount = SdBusBuildAdma2TableForPhysicalBuffer(
                    FdoExtension,
                    FdoExtension->DmaPhysical,
                    DataLength);
                if (Adma2DescCount != 0)
                {
                    TransferPath = SdTransferPathAdma2Bounce;
                }
                else
                {
                    TransferPath = FdoExtension->UseSdma ?
                                   SdTransferPathSdma :
                                   SdTransferPathPio;
                }
            }
            else if (FdoExtension->UseDmaMappingForAdma2)
            {
                Status = SdBusBuildAdma2TableFromDmaMapping(FdoExtension,
                                                            Mdl,
                                                            DataLength,
                                                            WriteToDevice,
                                                            &Adma2DescCount,
                                                            &ActiveScatterGatherList,
                                                            &Adma2BuildStatus);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("SdBusSendSdhciCommand: ADMA2 DMA mapping build failed (0x%08lx)\n",
                            Status);
                }
            }
        }

        if (SdBusTransferPathUsesAdma2(TransferPath) &&
            Adma2DescCount == 0)
        {
            if (Adma2BuildStatus == SdAdma2BuildAddressTooHigh)
            {
                DPRINT1("SdBusSendSdhciCommand: ADMA2 can't address MDL pages "
                        "above 4GB, disabling ADMA2 for this controller\n");
                FdoExtension->UseAdma2 = FALSE;
            }
            else if (Adma2BuildStatus == SdAdma2BuildDescriptorOverflow)
            {
                DPRINT1("SdBusSendSdhciCommand: ADMA2 descriptor table too small "
                        "for %lu bytes, falling back\n", DataLength);
            }
            else if (Adma2BuildStatus == SdAdma2BuildMapFailed)
            {
                DPRINT1("SdBusSendSdhciCommand: ADMA2 DMA mapping unavailable, "
                        "disabling ADMA2 for this controller\n");
                FdoExtension->UseAdma2 = FALSE;
            }
            else
            {
                DPRINT1("SdBusSendSdhciCommand: ADMA2 table build failed, falling back\n");
            }

            TransferPath = (HasData &&
                            FdoExtension->UseSdma &&
                            DataLength <= FdoExtension->DmaBufferSize) ?
                           SdTransferPathSdma :
                           SdTransferPathPio;
        }
        else
        {
            DPRINT("SdBusSendSdhciCommand: %s CMD%u %s %lu bytes (%lu descs)\n",
                   SdBusTransferPathName(TransferPath),
                   CmdDesc->Cmd,
                   IsRead ? "READ" : "WRITE",
                   DataLength,
                   Adma2DescCount);
        }
    }

    if (SdBusTransferPathUsesBounce(TransferPath))
    {
        DataBuffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (DataBuffer == NULL)
        {
            DPRINT1("SdBusSendSdhciCommand: Failed to map MDL, falling back\n");
            TransferPath = SdTransferPathPio;
        }
        else if (!IsRead)
        {
            RtlCopyMemory(FdoExtension->DmaBuffer, DataBuffer, DataLength);
        }
    }

    /* Step 3: Set up DMA address, block size and count for data transfers */
    if (HasData)
    {
        USHORT BlockSizeReg = (USHORT)(BlockSize & SDHCI_BLOCK_SIZE_MASK);

        if (SdBusTransferPathUsesAdma2(TransferPath))
        {
            UCHAR HostCtrl;

            /* ADMA2: no SDMA boundary bits needed in block size register */

            /* Select ADMA2 mode in host control register */
            HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
            HostCtrl &= ~SDHCI_HC_DMA_SELECT_MASK;
            HostCtrl |= SDHCI_HC_DMA_SELECT_ADMA2;
            SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);

            /* Write ADMA2 descriptor table physical address */
            SdBusWriteReg32(FdoExtension, SDHCI_ADMA_ADDRESS_LOW,
                            FdoExtension->Adma2TablePhysical.LowPart);
            SdBusWriteReg32(FdoExtension, SDHCI_ADMA_ADDRESS_HIGH, 0);
        }
        else if (TransferPath == SdTransferPathSdma)
        {
            UCHAR HostCtrl;

            /* Set 512 KB SDMA boundary to avoid boundary interrupts for <= 64 KB */
            BlockSizeReg |= (USHORT)(7 << 12);

            /* Select SDMA mode in host control register */
            HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
            HostCtrl &= ~SDHCI_HC_DMA_SELECT_MASK;
            HostCtrl |= SDHCI_HC_DMA_SELECT_SDMA;
            SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);

            /* Program SDMA system address (must be written before command) */
            SdBusWriteReg32(FdoExtension, SDHCI_SDMA_ADDRESS,
                            FdoExtension->DmaPhysical.LowPart);
        }

        SdBusWriteReg16(FdoExtension, SDHCI_BLOCK_SIZE, BlockSizeReg);
        SdBusWriteReg16(FdoExtension, SDHCI_BLOCK_COUNT, (USHORT)BlockCount);
    }

    /* Step 4: Write argument register */
    SdBusWriteReg32(FdoExtension, SDHCI_ARGUMENT, Argument);

    /* Step 5: Write transfer mode register (before command) */
    if (HasData)
    {
        TransferMode = SdBusBuildTransferMode(CmdDesc, DataLength,
                                               SdBusTransferPathUsesDma(TransferPath));
        SdBusWriteReg16(FdoExtension, SDHCI_TRANSFER_MODE, TransferMode);
    }

    /* Step 6: Write command register -- this triggers the command */
    CommandFlags = SdBusResponseTypeToFlags(CmdDesc->ResponseType);
    if (HasData)
    {
        CommandFlags |= SDHCI_CMD_DATA_PRESENT;
    }

    /* For application commands, CMD55 has already been sent by the caller */
    CmdReg = SDHCI_MAKE_CMD(CmdDesc->Cmd, CommandFlags);
    SdBusWriteReg16(FdoExtension, SDHCI_COMMAND, CmdReg);

    /* Step 7: Wait for command completion via interrupt */
    {
        ULONG CmdBits;
        NTSTATUS WaitStatus = SdBusWaitForInterrupt(
            FdoExtension,
            SDHCI_INT_CMD_COMPLETE,
            SDHCI_INT_CMD_ERROR_MASK,
            SD_CMD_TIMEOUT_MS,
            &CmdBits);

        if (WaitStatus == STATUS_IO_DEVICE_ERROR)
        {
            SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_CMD);
            if (HasData)
            {
                SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
            }
            InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
            if (CmdBits & SDHCI_INT_CMD_TIMEOUT)
            {
                Status = STATUS_SD_CMD_TIMEOUT;
                goto Cleanup;
            }
            Status = STATUS_SD_CMD_CRC_ERROR;
            goto Cleanup;
        }

        if (WaitStatus == STATUS_IO_TIMEOUT)
        {
            DPRINT1("SdBusSendSdhciCommand: CMD%u command completion timed out\n",
                    CmdDesc->Cmd);
            SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_CMD);
            if (HasData)
            {
                SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
            }
            InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
            Status = STATUS_IO_TIMEOUT;
            goto Cleanup;
        }

        /* Read response registers */
        if (Response != NULL)
        {
            if (CommandFlags & SDHCI_CMD_RESP_136)
            {
                Response[0] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE0);
                Response[1] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE1);
                Response[2] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE2);
                Response[3] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE3);
            }
            else
            {
                Response[0] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE0);
            }
        }

        if (!HasData && (CommandFlags & SDHCI_CMD_RESP_48_BUSY))
        {
            Status = SdBusWaitForBusyRelease(FdoExtension, CmdDesc->Cmd);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }
        }
    }

    /* Step 8: Handle data transfer */
    if (HasData && SdBusTransferPathUsesAdma2(TransferPath))
    {
        /*
         * ADMA2 path: controller scatter-gathers directly to/from caller pages.
         * No bounce buffer copy needed. Wait for XFER_COMPLETE or a data error.
         */
        ULONG DmaBits;
        NTSTATUS WaitStatus = SdBusWaitForInterrupt(
            FdoExtension,
            SDHCI_INT_XFER_COMPLETE,
            SDHCI_INT_DATA_ERROR_MASK,
            SD_DATA_TIMEOUT_MS,
            &DmaBits);

        if (WaitStatus == STATUS_IO_DEVICE_ERROR)
        {
            if (DmaBits & SDHCI_INT_ADMA)
            {
                UCHAR AdmaErr = SdBusReadReg8(FdoExtension,
                                               SDHCI_ADMA_ERROR_STATUS);
                DPRINT1("SdBusSendSdhciCommand: ADMA2 error status 0x%02x\n",
                        AdmaErr);
            }
            SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
            InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
            if (DmaBits & SDHCI_INT_DATA_TIMEOUT)
            {
                Status = STATUS_SD_DATA_TIMEOUT;
                goto Cleanup;
            }
            Status = STATUS_SD_DATA_CRC_ERROR;
            goto Cleanup;
        }

        if (WaitStatus == STATUS_IO_TIMEOUT)
        {
            UCHAR AdmaErr = SdBusReadReg8(FdoExtension,
                                           SDHCI_ADMA_ERROR_STATUS);
            DPRINT1("SdBusSendSdhciCommand: ADMA2 transfer timed out, "
                    "error status 0x%02x\n", AdmaErr);
            SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
            InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
            Status = STATUS_IO_TIMEOUT;
            goto Cleanup;
        }

        if (TransferPath == SdTransferPathAdma2Bounce && IsRead)
        {
            RtlCopyMemory(DataBuffer, FdoExtension->DmaBuffer, DataLength);
        }
    }
    else if (HasData && TransferPath == SdTransferPathSdma)
    {
        /*
         * SDMA path: controller transfers data between card and bounce buffer.
         * DMA boundary interrupts are handled entirely in the ISR.
         * Wait here for XFER_COMPLETE or a data error.
         */
        ULONG DmaBits;
        NTSTATUS WaitStatus = SdBusWaitForInterrupt(
            FdoExtension,
            SDHCI_INT_XFER_COMPLETE,
            SDHCI_INT_DATA_ERROR_MASK,
            SD_DATA_TIMEOUT_MS,
            &DmaBits);

        if (WaitStatus == STATUS_IO_DEVICE_ERROR)
        {
            SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
            InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
            if (DmaBits & SDHCI_INT_DATA_TIMEOUT)
            {
                Status = STATUS_SD_DATA_TIMEOUT;
                goto Cleanup;
            }
            Status = STATUS_SD_DATA_CRC_ERROR;
            goto Cleanup;
        }

        if (WaitStatus == STATUS_IO_TIMEOUT)
        {
            DPRINT1("SdBusSendSdhciCommand: SDMA transfer complete timed out\n");
            SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
            InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
            Status = STATUS_IO_TIMEOUT;
            goto Cleanup;
        }

        /* For reads, copy from bounce buffer to caller's MDL buffer */
        if (IsRead)
        {
            RtlCopyMemory(DataBuffer, FdoExtension->DmaBuffer, DataLength);
        }
    }
    else if (HasData)
    {
        /* PIO path: block-by-block transfer via data buffer port */
        ULONG PioBits;
        NTSTATUS WaitStatus;
        ULONG BufferReadyBit = IsRead ? SDHCI_INT_BUFFER_READ_READY
                                      : SDHCI_INT_BUFFER_WRITE_READY;

        DataBuffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (DataBuffer == NULL)
        {
            DPRINT1("SdBusSendSdhciCommand: Failed to map MDL\n");
            SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        {
            PULONG DataWords = (PULONG)DataBuffer;
            ULONG WordsPerBlock = BlockSize / sizeof(ULONG);
            ULONG Block;
            ULONG Word;

            for (Block = 0; Block < BlockCount; Block++)
            {
                /* Wait for buffer ready via interrupt */
                WaitStatus = SdBusWaitForInterrupt(
                    FdoExtension,
                    BufferReadyBit,
                    SDHCI_INT_DATA_ERROR_MASK,
                    SD_DATA_TIMEOUT_MS,
                    &PioBits);

                if (WaitStatus == STATUS_IO_DEVICE_ERROR)
                {
                    SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET,
                                   SDHCI_RESET_DATA);
                    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
                    if (PioBits & SDHCI_INT_DATA_TIMEOUT)
                    {
                        Status = STATUS_SD_DATA_TIMEOUT;
                        goto Cleanup;
                    }
                    Status = STATUS_SD_DATA_CRC_ERROR;
                    goto Cleanup;
                }

                if (WaitStatus == STATUS_IO_TIMEOUT)
                {
                    DPRINT1("SdBusSendSdhciCommand: Data buffer timeout (block %lu)\n",
                            Block);
                    SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET,
                                   SDHCI_RESET_DATA);
                    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
                    Status = STATUS_IO_TIMEOUT;
                    goto Cleanup;
                }

                /* Transfer one block via the data buffer port */
                for (Word = 0; Word < WordsPerBlock; Word++)
                {
                    ULONG Index = Block * WordsPerBlock + Word;
                    if (IsRead)
                    {
                        DataWords[Index] = SdBusReadReg32(FdoExtension,
                                                          SDHCI_BUFFER_DATA_PORT);
                    }
                    else
                    {
                        SdBusWriteReg32(FdoExtension,
                                        SDHCI_BUFFER_DATA_PORT,
                                        DataWords[Index]);
                    }
                }
            }
        }

        /* Wait for transfer complete via interrupt */
        WaitStatus = SdBusWaitForInterrupt(
            FdoExtension,
            SDHCI_INT_XFER_COMPLETE,
            SDHCI_INT_DATA_ERROR_MASK,
            SD_DATA_TIMEOUT_MS,
            &PioBits);

        if (WaitStatus == STATUS_IO_DEVICE_ERROR)
        {
            SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
            InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
            if (PioBits & SDHCI_INT_DATA_TIMEOUT)
            {
                Status = STATUS_SD_DATA_TIMEOUT;
                goto Cleanup;
            }
            Status = STATUS_SD_DATA_CRC_ERROR;
            goto Cleanup;
        }

        if (WaitStatus == STATUS_IO_TIMEOUT)
        {
            DPRINT1("SdBusSendSdhciCommand: Transfer complete timed out\n");
            SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_DATA);
            InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
            Status = STATUS_IO_TIMEOUT;
            goto Cleanup;
        }
    }

    Status = STATUS_SUCCESS;

Cleanup:
    if (ActiveScatterGatherList != NULL &&
        FdoExtension->DmaAdapter != NULL &&
        FdoExtension->DmaAdapter->DmaOperations != NULL &&
        FdoExtension->DmaAdapter->DmaOperations->PutScatterGatherList != NULL)
    {
        SdBusPutScatterGatherListAtDispatch(FdoExtension,
                                            ActiveScatterGatherList,
                                            WriteToDevice);
    }

    return Status;
}

/**
 * @brief Handle SDRF_GET_PROPERTY requests.
 *
 * Reads card and host properties (media state, write-protect, bus width,
 * function type, total sectors, etc.) from cached PDO/FDO state and copies
 * them into the request packet buffer.
 *
 * @param[in]     PdoExtension  Pointer to the child PDO extension for the target card.
 * @param[in,out] Packet        Pointer to the SD bus request packet containing the property query.
 *
 * @return STATUS_SUCCESS, STATUS_BUFFER_TOO_SMALL, or STATUS_NOT_SUPPORTED.
 */
static NTSTATUS
SdBusHandleGetProperty(
    _In_ PPDO_EXTENSION PdoExtension,
    _In_ PSDBUS_REQUEST_PACKET Packet)
{
    SDBUS_PROPERTY Property = Packet->Parameters.GetSetProperty.Property;
    PVOID Buffer = Packet->Parameters.GetSetProperty.Buffer;
    ULONG Length = Packet->Parameters.GetSetProperty.Length;

    switch (Property)
    {
        case SDP_MEDIA_CHANGECOUNT:
            if (Length < sizeof(ULONG))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            /* We don't track change count yet; return 0 */
            *(PULONG)Buffer = 0;
            Packet->Information = sizeof(ULONG);
            return STATUS_SUCCESS;

        case SDP_MEDIA_STATE:
            if (Length < sizeof(SDPROP_MEDIA_STATE))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            *(SDPROP_MEDIA_STATE*)Buffer = PdoExtension->Present ?
                SDPMS_MEDIA_INSERTED : SDPMS_NO_MEDIA;
            Packet->Information = sizeof(SDPROP_MEDIA_STATE);
            return STATUS_SUCCESS;

        case SDP_WRITE_PROTECTED:
            if (Length < sizeof(BOOLEAN))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            *(PBOOLEAN)Buffer = PdoExtension->WriteProtected;
            Packet->Information = sizeof(BOOLEAN);
            return STATUS_SUCCESS;

        case SDP_FUNCTION_NUMBER:
            if (Length < sizeof(UCHAR))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            *(PUCHAR)Buffer = (UCHAR)PdoExtension->FunctionNumber;
            Packet->Information = sizeof(UCHAR);
            return STATUS_SUCCESS;

        case SDP_FUNCTION_TYPE:
        {
            SDBUS_FUNCTION_TYPE FunctionType;
            if (Length < sizeof(SDBUS_FUNCTION_TYPE))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            switch (PdoExtension->CardType)
            {
                case SdCardTypeSdV1:
                case SdCardTypeSdV2:
                case SdCardTypeSdhc:
                case SdCardTypeSdxc:
                    FunctionType = SDBUS_FUNCTION_TYPE_SD_MEMORY;
                    break;
                case SdCardTypeSdio:
                    FunctionType = SDBUS_FUNCTION_TYPE_SDIO;
                    break;
                case SdCardTypeCombo:
                    FunctionType = PdoExtension->FunctionNumber != 0 ?
                                   SDBUS_FUNCTION_TYPE_SDIO :
                                   SDBUS_FUNCTION_TYPE_SD_MEMORY;
                    break;
                case SdCardTypeMmc:
                case SdCardTypeEmmc:
                    FunctionType = SDBUS_FUNCTION_TYPE_MMC_MEMORY;
                    break;
                default:
                    FunctionType = SDBUS_FUNCTION_TYPE_UNKNOWN;
                    break;
            }
            *(SDBUS_FUNCTION_TYPE*)Buffer = FunctionType;
            Packet->Information = sizeof(SDBUS_FUNCTION_TYPE);
            return STATUS_SUCCESS;
        }

        case SDP_BUS_DRIVER_VERSION:
            if (Length < sizeof(USHORT))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            *(PUSHORT)Buffer = SDBUS_DRIVER_VERSION_4;
            Packet->Information = sizeof(USHORT);
            return STATUS_SUCCESS;

        case SDP_BUS_WIDTH:
            if (Length < sizeof(UCHAR))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            *(PUCHAR)Buffer = PdoExtension->FdoExtension->CurrentBusWidth != 0 ?
                              PdoExtension->FdoExtension->CurrentBusWidth : 1;
            Packet->Information = sizeof(UCHAR);
            return STATUS_SUCCESS;

        case SDP_HOST_BLOCK_LENGTH:
        {
            ULONG MaxBlock;
            if (Length < sizeof(USHORT))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            MaxBlock = SDHCI_MAX_BLOCK_LENGTH(
                PdoExtension->FdoExtension->HostCapabilities);
            *(PUSHORT)Buffer = (USHORT)MaxBlock;
            Packet->Information = sizeof(USHORT);
            return STATUS_SUCCESS;
        }

        case SDP_HIGH_CAPACITY_SUPPORTED:
        {
            if (Length < sizeof(BOOLEAN))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            *(PBOOLEAN)Buffer = PdoExtension->HighCapacity;
            Packet->Information = sizeof(BOOLEAN);
            return STATUS_SUCCESS;
        }

        case SDP_TOTAL_SECTORS:
        {
            if (Length < sizeof(ULONGLONG))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            *(PULONGLONG)Buffer = PdoExtension->TotalSectors;
            Packet->Information = sizeof(ULONGLONG);
            return STATUS_SUCCESS;
        }

        case SDP_ROS_CARD_TYPE:
        {
            if (Length < sizeof(ULONG))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            *(PULONG)Buffer = (ULONG)PdoExtension->CardType;
            Packet->Information = sizeof(ULONG);
            return STATUS_SUCCESS;
        }

        default:
            DPRINT1("SdBusHandleGetProperty: Unsupported property %d\n", Property);
            return STATUS_NOT_SUPPORTED;
    }
}

/**
 * @brief Handle SDRF_SET_PROPERTY requests.
 *
 * Configures card and host properties (bus width, bus clock, card interrupt
 * forwarding) by writing to SDHCI registers.
 *
 * @param[in]     PdoExtension  Pointer to the child PDO extension for the target card.
 * @param[in,out] Packet        Pointer to the SD bus request packet containing the property to set.
 *
 * @return STATUS_SUCCESS, STATUS_BUFFER_TOO_SMALL, STATUS_INVALID_PARAMETER,
 *         STATUS_IO_TIMEOUT, or STATUS_NOT_SUPPORTED.
 */
static NTSTATUS
SdBusHandleSetProperty(
    _In_ PPDO_EXTENSION PdoExtension,
    _In_ PSDBUS_REQUEST_PACKET Packet)
{
    SDBUS_PROPERTY Property = Packet->Parameters.GetSetProperty.Property;
    PVOID Buffer = Packet->Parameters.GetSetProperty.Buffer;
    ULONG Length = Packet->Parameters.GetSetProperty.Length;
    PFDO_EXTENSION FdoExtension = PdoExtension->FdoExtension;

    switch (Property)
    {
        case SDP_BUS_WIDTH:
        {
            UCHAR RequestedWidth;
            UCHAR HostCtrl;

            if (Length < sizeof(UCHAR))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            RequestedWidth = *(PUCHAR)Buffer;
            HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);

            /* Clear width bits */
            HostCtrl &= ~(SDHCI_HC_DATA_WIDTH_4BIT | SDHCI_HC_DATA_WIDTH_8BIT);

            switch (RequestedWidth)
            {
                case 8:
                    HostCtrl |= SDHCI_HC_DATA_WIDTH_8BIT;
                    break;
                case 4:
                    HostCtrl |= SDHCI_HC_DATA_WIDTH_4BIT;
                    break;
                case 1:
                    /* 1-bit is the default (no bits set) */
                    break;
                default:
                    return STATUS_INVALID_PARAMETER;
            }

            SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);
            FdoExtension->CurrentBusWidth = RequestedWidth;
            return STATUS_SUCCESS;
        }

        case SDP_BUS_CLOCK:
        {
            ULONG RequestedClockKhz;
            USHORT Divisor;
            USHORT DivisorHigh;
            USHORT ClockControl;
            ULONG Timeout;

            if (Length < sizeof(ULONG))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            RequestedClockKhz = *(PULONG)Buffer;

            /* Disable SD clock first */
            ClockControl = SdBusReadReg16(FdoExtension, SDHCI_CLOCK_CONTROL);
            ClockControl &= ~SDHCI_CLK_SD_CLK_ENABLE;
            SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockControl);

            if (RequestedClockKhz == 0)
            {
                /* Just disable the clock */
                return STATUS_SUCCESS;
            }

            /* Calculate divisor */
            if (FdoExtension->MaxClockFrequency > 0)
            {
                Divisor = (USHORT)SDHCI_CLK_DIVISOR(FdoExtension->MaxClockFrequency,
                                                     RequestedClockKhz);
            }
            else
            {
                Divisor = 1;
            }

            if (Divisor > 0)
            {
                Divisor--;
            }
            if (Divisor > 0x3FF)
            {
                Divisor = 0x3FF;
            }

            DivisorHigh = (Divisor & 0x300) >> 2;
            ClockControl = (USHORT)((Divisor & 0xFF) << SDHCI_CLK_FREQ_SEL_SHIFT);
            ClockControl |= (USHORT)DivisorHigh;
            ClockControl |= SDHCI_CLK_INT_CLK_ENABLE;

            SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockControl);

            /* Wait for internal clock to stabilize */
            Timeout = 200;
            while (Timeout > 0)
            {
                ClockControl = SdBusReadReg16(FdoExtension, SDHCI_CLOCK_CONTROL);
                if (ClockControl & SDHCI_CLK_INT_CLK_STABLE)
                {
                    break;
                }
                KeStallExecutionProcessor(1000);
                Timeout--;
            }

            if (Timeout == 0)
            {
                return STATUS_IO_TIMEOUT;
            }

            /* Enable SD clock */
            ClockControl |= SDHCI_CLK_SD_CLK_ENABLE;
            SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockControl);

            return STATUS_SUCCESS;
        }

        case SDP_SET_CARD_INTERRUPT_FORWARD:
        {
            BOOLEAN Enable;

            if (Length < sizeof(BOOLEAN))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            Enable = *(PBOOLEAN)Buffer;
            if (Enable)
            {
                SdBusUpdateInterruptSignalEnable(FdoExtension,
                                                 SDHCI_INT_CARD_INTERRUPT,
                                                 0);
            }
            else
            {
                SdBusUpdateInterruptSignalEnable(FdoExtension,
                                                 0,
                                                 SDHCI_INT_CARD_INTERRUPT);
            }
            return STATUS_SUCCESS;
        }

        default:
            DPRINT1("SdBusHandleSetProperty: Unsupported property %d\n", Property);
            return STATUS_NOT_SUPPORTED;
    }
}

/**
 * @brief Handle SDRF_DEVICE_COMMAND requests.
 *
 * Auto-fills the RCA for addressed commands (CMD7, CMD9, CMD10, CMD13, CMD15),
 * sends CMD55 prefix for application commands, then dispatches the actual
 * command via SdBusSendSdhciCommand. Copies the response data and transfer
 * byte count back into the request packet.
 *
 * @param[in]     PdoExtension  Pointer to the child PDO extension for the target card.
 * @param[in,out] Packet        Pointer to the SD bus request packet containing the
 *                               command descriptor, argument, MDL, and data length.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
static NTSTATUS
SdBusHandleDeviceCommand(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PSDBUS_REQUEST_PACKET Packet)
{
    PFDO_EXTENSION FdoExtension = PdoExtension->FdoExtension;
    PSDCMD_DESCRIPTOR CmdDesc = &Packet->Parameters.DeviceCommand.CmdDesc;
    ULONG Argument = Packet->Parameters.DeviceCommand.Argument;
    PMDL Mdl = Packet->Parameters.DeviceCommand.Mdl;
    ULONG DataLength = Packet->Parameters.DeviceCommand.Length;
    ULONG Response[4];
    NTSTATUS Status;
    BOOLEAN HasData;
    ULONG Retry;
    ULONG MaxRetries;

    RtlZeroMemory(Response, sizeof(Response));

    if (PdoExtension->CardType == SdCardTypeEmmc ||
        PdoExtension->CardType == SdCardTypeMmc)
    {
        UCHAR PartitionId;

        PartitionId = PdoExtension->IsEmmcPartition ?
            PdoExtension->EmmcPartitionId : EMMC_PARTITION_USER;

        Status = SdBusEmmcSelectPartition(FdoExtension,
                                          PdoExtension,
                                          PartitionId);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    DPRINT("SdBusHandleDeviceCommand: CMD%u Class=%u Arg=0x%08lx Len=%lu\n",
           CmdDesc->Cmd, CmdDesc->CmdClass, Argument, DataLength);

    /*
     * Auto-fill the RCA for addressed commands.
     * The class driver operates on a specific PDO and typically doesn't
     * know the card's RCA. These commands require RCA in bits [31:16].
     */
    switch (CmdDesc->Cmd)
    {
        case SDCMD_SELECT_CARD:         /* CMD7  */
        case SDCMD_SEND_CSD:            /* CMD9  */
        case SDCMD_SEND_CID:            /* CMD10 */
        case SDCMD_SEND_STATUS:         /* CMD13 */
        case SDCMD_GO_INACTIVE_STATE:   /* CMD15 */
            Argument = PdoExtension->RelativeAddress << 16;
            break;
    }

    HasData = (DataLength > 0 && Mdl != NULL &&
               CmdDesc->TransferType != SDTT_CMD_ONLY);
    MaxRetries = HasData ? SD_MAX_RETRIES : 0;

    for (Retry = 0; ; Retry++)
    {
        if (CmdDesc->CmdClass == SDCC_APP_CMD)
        {
            Status = SdBusSendCommand(FdoExtension,
                                      SDCMD_APP_CMD,
                                      PdoExtension->RelativeAddress << 16,
                                      SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                      NULL);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("SdBusHandleDeviceCommand: CMD55 failed (0x%08lx)\n", Status);
                return Status;
            }
        }

        Status = SdBusSendSdhciCommand(FdoExtension,
                                        CmdDesc,
                                        Argument,
                                        Mdl,
                                        DataLength,
                                        Response);
        if (NT_SUCCESS(Status))
        {
            break;
        }

        if (Retry >= MaxRetries || !SdBusIsRetryableError(Status))
        {
            return Status;
        }

        DPRINT1("SdBusHandleDeviceCommand: CMD%u retry %lu/%lu after status 0x%08lx\n",
                CmdDesc->Cmd, Retry + 1, MaxRetries, Status);

        SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET,
                        SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
    }

    /* Copy response data back into the request packet */
    RtlCopyMemory(Packet->ResponseData.AsULONG, Response,
                   sizeof(Packet->ResponseData.AsULONG));
    Packet->Information = DataLength;

    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusHandleIoRwDirect(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PSDBUS_REQUEST_PACKET Packet)
{
    PFDO_EXTENSION FdoExtension = PdoExtension->FdoExtension;
    UCHAR DataOut = 0;
    NTSTATUS Status;

    Status = SdBusSdioCmd52(FdoExtension,
                            Packet->Parameters.IoDirect.Function,
                            Packet->Parameters.IoDirect.Write,
                            Packet->Parameters.IoDirect.RawMode,
                            Packet->Parameters.IoDirect.Address,
                            Packet->Parameters.IoDirect.DataIn,
                            &DataOut);

    Packet->Parameters.IoDirect.DataOut = DataOut;
    Packet->ResponseData.AsULONG[0] = DataOut;
    Packet->ResponseLength = 1;
    Packet->Information = 1;
    return Status;
}

static NTSTATUS
SdBusHandleIoRwExtended(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PSDBUS_REQUEST_PACKET Packet)
{
    PFDO_EXTENSION FdoExtension = PdoExtension->FdoExtension;
    NTSTATUS Status;

    Status = SdBusSdioCmd53(FdoExtension,
                            PdoExtension,
                            Packet->Parameters.IoExtended.Function,
                            Packet->Parameters.IoExtended.Write,
                            Packet->Parameters.IoExtended.BlockMode,
                            Packet->Parameters.IoExtended.Increment,
                            Packet->Parameters.IoExtended.Address,
                            Packet->Parameters.IoExtended.BlockCount,
                            Packet->Parameters.IoExtended.BlockSize,
                            Packet->Parameters.IoExtended.Mdl);

    if (Packet->Parameters.IoExtended.BlockMode)
    {
        Packet->Information = Packet->Parameters.IoExtended.BlockCount *
                              Packet->Parameters.IoExtended.BlockSize;
    }
    else
    {
        Packet->Information = Packet->Parameters.IoExtended.BlockCount;
    }
    return Status;
}

static NTSTATUS
SdBusHandleEmmcSwitch(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PSDBUS_REQUEST_PACKET Packet)
{
    return SdBusEmmcSwitch(PdoExtension->FdoExtension,
                           Packet->Parameters.EmmcSwitch.Access,
                           Packet->Parameters.EmmcSwitch.Index,
                           Packet->Parameters.EmmcSwitch.Value,
                           Packet->Parameters.EmmcSwitch.CmdSet);
}

static NTSTATUS
SdBusHandleEmmcSelectPartition(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PSDBUS_REQUEST_PACKET Packet)
{
    return SdBusEmmcSelectPartition(PdoExtension->FdoExtension,
                                    PdoExtension,
                                    Packet->Parameters.EmmcSelectPartition.PartitionId);
}

static NTSTATUS
SdBusHandleEmmcSanitize(
    _In_ PPDO_EXTENSION PdoExtension)
{
    return SdBusEmmcSanitize(PdoExtension->FdoExtension, PdoExtension);
}

/**
 * @brief Main SD bus request dispatcher.
 *
 * Validates the PDO and request packet, checks card presence, acquires the
 * command serialization FAST_MUTEX only for controller-touching requests, and
 * dispatches to the appropriate handler based on the request function:
 * property get/set, device command, or MMC soft reset. Cached property reads
 * stay lock-free so they do not queue behind unrelated I/O on the same host.
 *
 * @param[in]     PdoExtension   Pointer to the child PDO extension for the target card.
 * @param[in,out] RequestPacket  Pointer to the SD bus request packet to process.
 *
 * @return STATUS_SUCCESS on success, STATUS_INVALID_PARAMETER,
 *         STATUS_SD_CARD_REMOVED, or another NTSTATUS error code.
 */
NTSTATUS
SdBusHandleRequest(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PSDBUS_REQUEST_PACKET RequestPacket)
{
    NTSTATUS Status;
    PFDO_EXTENSION FdoExtension;

    if (PdoExtension == NULL || RequestPacket == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!PdoExtension->Present)
    {
        return STATUS_SD_CARD_REMOVED;
    }

    FdoExtension = PdoExtension->FdoExtension;

    if (FdoExtension->UseMiniportDispatch &&
        FdoExtension->MiniportIssueRequest != NULL)
    {
        return FdoExtension->MiniportIssueRequest(
            FdoExtension->MiniportDispatchContext, RequestPacket);
    }

    /*
     * All current SDRF_GET_PROPERTY cases are satisfied from cached state in
     * the PDO/FDO extensions. Keep them off the controller mutex so property
     * enumeration does not queue behind data or command traffic.
     */
    if (RequestPacket->RequestFunction == SDRF_GET_PROPERTY)
    {
        return SdBusHandleGetProperty(PdoExtension, RequestPacket);
    }

    /*
     * Serialize all controller-touching SDHCI register access. Multiple
     * threads (filesystem, partmgr, etc.) can submit SD requests
     * concurrently. Without this mutex, concurrent PIO transfers corrupt the
     * SDHCI controller state and cause hangs.
     */
    ExAcquireFastMutex(&FdoExtension->CommandMutex);

    switch (RequestPacket->RequestFunction)
    {
        case SDRF_SET_PROPERTY:
            Status = SdBusHandleSetProperty(PdoExtension, RequestPacket);
            break;

        case SDRF_DEVICE_COMMAND:
            Status = SdBusHandleDeviceCommand(PdoExtension, RequestPacket);
            break;

        case SDRF_MMC_SOFT_RESET:
            Status = SdBusHandleMmcSoftReset(FdoExtension);
            break;

        case SDRF_IO_RW_DIRECT:
            Status = SdBusHandleIoRwDirect(PdoExtension, RequestPacket);
            break;

        case SDRF_IO_RW_EXTENDED:
            Status = SdBusHandleIoRwExtended(PdoExtension, RequestPacket);
            break;

        case SDRF_EMMC_SWITCH:
            Status = SdBusHandleEmmcSwitch(PdoExtension, RequestPacket);
            break;

        case SDRF_EMMC_SELECT_PARTITION:
            Status = SdBusHandleEmmcSelectPartition(PdoExtension, RequestPacket);
            break;

        case SDRF_EMMC_SANITIZE:
            Status = SdBusHandleEmmcSanitize(PdoExtension);
            break;

        default:
            DPRINT1("SdBusHandleRequest: Unsupported function %d\n",
                   RequestPacket->RequestFunction);
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    ExReleaseFastMutex(&FdoExtension->CommandMutex);

    return Status;
}


static __inline PFDO_EXTENSION
SdBusCsqGetFdo(
    _In_ PIO_CSQ Csq)
{
    return CONTAINING_RECORD(Csq, FDO_EXTENSION, RequestCsq);
}

static NTSTATUS
NTAPI
SdBusCsqInsertIrpEx(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp,
    _In_ PVOID InsertContext)
{
    PFDO_EXTENSION FdoExtension = SdBusCsqGetFdo(Csq);

    UNREFERENCED_PARAMETER(InsertContext);

    if (InterlockedCompareExchange(&FdoExtension->WorkerShutdown, 0, 0) != 0)
    {
        return STATUS_DEVICE_REMOVED;
    }

    InsertTailList(&FdoExtension->RequestQueue,
                   &Irp->Tail.Overlay.ListEntry);

    KeSetEvent(&FdoExtension->RequestArrived, IO_NO_INCREMENT, FALSE);
    return STATUS_SUCCESS;
}

static VOID
NTAPI
SdBusCsqRemoveIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);

    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}

static PIRP
NTAPI
SdBusCsqPeekNextIrp(
    _In_ PIO_CSQ Csq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PFDO_EXTENSION FdoExtension = SdBusCsqGetFdo(Csq);
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(PeekContext);

    if (Irp == NULL)
    {
        Entry = FdoExtension->RequestQueue.Flink;
    }
    else
    {
        Entry = Irp->Tail.Overlay.ListEntry.Flink;
    }

    if (Entry == &FdoExtension->RequestQueue)
    {
        return NULL;
    }

    return CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
}

static VOID
NTAPI
SdBusCsqAcquireLock(
    _In_ PIO_CSQ Csq,
    _Out_ PKIRQL Irql)
{
    PFDO_EXTENSION FdoExtension = SdBusCsqGetFdo(Csq);

    KeAcquireSpinLock(&FdoExtension->RequestQueueLock, Irql);
}

static VOID
NTAPI
SdBusCsqReleaseLock(
    _In_ PIO_CSQ Csq,
    _In_ KIRQL Irql)
{
    PFDO_EXTENSION FdoExtension = SdBusCsqGetFdo(Csq);

    KeReleaseSpinLock(&FdoExtension->RequestQueueLock, Irql);
}

static VOID
NTAPI
SdBusCsqCompleteCanceledIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    PFDO_EXTENSION FdoExtension = SdBusCsqGetFdo(Csq);
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT PdoDeviceObject = IoStack->DeviceObject;
    PPDO_EXTENSION PdoExtension = NULL;

    if (PdoDeviceObject != NULL)
    {
        PdoExtension = (PPDO_EXTENSION)PdoDeviceObject->DeviceExtension;
    }

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    if (PdoExtension != NULL)
    {
        IoReleaseRemoveLock(&PdoExtension->RemoveLock, Irp);
    }

    InterlockedDecrement(&FdoExtension->OutstandingRequestCount);
}

static VOID
NTAPI
SdBusRequestWorker(
    _In_ PVOID Context)
{
    PFDO_EXTENSION FdoExtension = (PFDO_EXTENSION)Context;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    PDEVICE_OBJECT PdoDeviceObject;
    PPDO_EXTENSION PdoExtension;
    PSDBUS_REQUEST_PACKET RequestPacket;
    NTSTATUS Status;

    for (;;)
    {
        KeWaitForSingleObject(&FdoExtension->RequestArrived,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);

        for (;;)
        {
            Irp = IoCsqRemoveNextIrp(&FdoExtension->RequestCsq, NULL);
            if (Irp == NULL)
            {
                break;
            }

            IoStack = IoGetCurrentIrpStackLocation(Irp);
            PdoDeviceObject = IoStack->DeviceObject;
            PdoExtension = (PPDO_EXTENSION)PdoDeviceObject->DeviceExtension;
            RequestPacket = (PSDBUS_REQUEST_PACKET)
                IoStack->Parameters.Others.Argument1;

            if (RequestPacket == NULL)
            {
                Status = STATUS_INVALID_PARAMETER;
                Irp->IoStatus.Status = Status;
                Irp->IoStatus.Information = 0;
            }
            else
            {
                Status = SdBusHandleRequest(PdoExtension, RequestPacket);
                Irp->IoStatus.Status = Status;
                Irp->IoStatus.Information = RequestPacket->Information;
            }

            IoCompleteRequest(Irp, IO_DISK_INCREMENT);
            IoReleaseRemoveLock(&PdoExtension->RemoveLock, Irp);
            InterlockedDecrement(&FdoExtension->OutstandingRequestCount);
        }

        if (InterlockedCompareExchange(&FdoExtension->WorkerShutdown, 0, 0))
        {
            break;
        }
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS
SdBusInitializeRequestQueue(
    _In_ PFDO_EXTENSION FdoExtension)
{
    NTSTATUS Status;
    PVOID ThreadObject;

    if (FdoExtension->WorkerStarted)
    {
        return STATUS_SUCCESS;
    }

    InitializeListHead(&FdoExtension->RequestQueue);
    KeInitializeSpinLock(&FdoExtension->RequestQueueLock);
    KeInitializeEvent(&FdoExtension->RequestArrived,
                      SynchronizationEvent,
                      FALSE);
    FdoExtension->OutstandingRequestCount = 0;
    FdoExtension->WorkerShutdown = 0;

    Status = IoCsqInitializeEx(&FdoExtension->RequestCsq,
                               SdBusCsqInsertIrpEx,
                               SdBusCsqRemoveIrp,
                               SdBusCsqPeekNextIrp,
                               SdBusCsqAcquireLock,
                               SdBusCsqReleaseLock,
                               SdBusCsqCompleteCanceledIrp);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusInitializeRequestQueue: IoCsqInitialize failed (0x%08lx)\n",
                Status);
        return Status;
    }
    FdoExtension->CsqInitialized = TRUE;

    Status = PsCreateSystemThread(&FdoExtension->WorkerThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  SdBusRequestWorker,
                                  FdoExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusInitializeRequestQueue: PsCreateSystemThread failed (0x%08lx)\n",
                Status);
        FdoExtension->CsqInitialized = FALSE;
        return Status;
    }

    Status = ObReferenceObjectByHandle(FdoExtension->WorkerThreadHandle,
                                       THREAD_ALL_ACCESS,
                                       NULL,
                                       KernelMode,
                                       &ThreadObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusInitializeRequestQueue: ObReferenceObjectByHandle failed (0x%08lx)\n",
                Status);
        InterlockedExchange(&FdoExtension->WorkerShutdown, 1);
        KeSetEvent(&FdoExtension->RequestArrived, IO_NO_INCREMENT, FALSE);
        ZwClose(FdoExtension->WorkerThreadHandle);
        FdoExtension->WorkerThreadHandle = NULL;
        FdoExtension->CsqInitialized = FALSE;
        return Status;
    }

    FdoExtension->WorkerThread = ThreadObject;
    ZwClose(FdoExtension->WorkerThreadHandle);
    FdoExtension->WorkerThreadHandle = NULL;
    FdoExtension->WorkerStarted = TRUE;

    return STATUS_SUCCESS;
}

VOID
SdBusTearDownRequestQueue(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    PPDO_EXTENSION PdoExtension;

    if (FdoExtension->WorkerStarted)
    {
        InterlockedExchange(&FdoExtension->WorkerShutdown, 1);
        KeSetEvent(&FdoExtension->RequestArrived, IO_NO_INCREMENT, FALSE);

        if (FdoExtension->WorkerThread != NULL)
        {
            KeWaitForSingleObject(FdoExtension->WorkerThread,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            ObDereferenceObject(FdoExtension->WorkerThread);
            FdoExtension->WorkerThread = NULL;
        }

        FdoExtension->WorkerStarted = FALSE;
    }

    if (FdoExtension->CsqInitialized)
    {
        for (;;)
        {
            Irp = IoCsqRemoveNextIrp(&FdoExtension->RequestCsq, NULL);
            if (Irp == NULL)
            {
                break;
            }

            IoStack = IoGetCurrentIrpStackLocation(Irp);
            PdoExtension = NULL;
            if (IoStack->DeviceObject != NULL)
            {
                PdoExtension = (PPDO_EXTENSION)IoStack->DeviceObject->DeviceExtension;
            }

            Irp->IoStatus.Status = STATUS_DEVICE_REMOVED;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);

            if (PdoExtension != NULL)
            {
                IoReleaseRemoveLock(&PdoExtension->RemoveLock, Irp);
            }

            InterlockedDecrement(&FdoExtension->OutstandingRequestCount);
        }

        FdoExtension->CsqInitialized = FALSE;
    }
}
