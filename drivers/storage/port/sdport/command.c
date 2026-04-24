/*
 * PROJECT:     ReactOS SD Port Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SD command building and execution
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *******************************************************************/

#include "sdport.h"

#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/**
 * @brief Wait for the miniport to signal command completion.
 *
 * Blocks on the slot extension's request event with a configurable timeout.
 * The event is signaled by the interrupt DPC when the command completes.
 *
 * @param[in] SlotExtension  Pointer to the slot extension containing the event.
 * @param[in] TimeoutMs      Maximum time to wait in milliseconds.
 *
 * @return The request status stored by the DPC, or STATUS_SD_CMD_TIMEOUT
 *         if the wait timed out.
 */
static
NTSTATUS
SdPortWaitForRequestCompletion(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ ULONG TimeoutMs)
{
    LARGE_INTEGER Timeout;
    NTSTATUS WaitStatus;

    Timeout.QuadPart = -(LONGLONG)TimeoutMs * 10000LL;

    WaitStatus = KeWaitForSingleObject(&SlotExtension->RequestEvent,
                                        Executive,
                                        KernelMode,
                                        FALSE,
                                        &Timeout);

    if (WaitStatus == STATUS_TIMEOUT)
    {
        DPRINT1("SdPortWaitForRequestCompletion: Timed out after %lu ms\n",
                TimeoutMs);
        return STATUS_SD_CMD_TIMEOUT;
    }

    return SlotExtension->RequestStatus;
}

static
VOID
SdPortRecoverFromError(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS FailureStatus)
{
    SDPORT_BUS_OPERATION BusOp;
    BOOLEAN IsDataError;

    if (FdoExtension->MiniportInitData.ClearEvents != NULL)
    {
        FdoExtension->MiniportInitData.ClearEvents(
            FdoExtension->MiniportPrivateExtension,
            SDHCI_INT_ALL_MASK);
    }

    if (FdoExtension->MiniportInitData.IssueBusOperation != NULL)
    {
        RtlZeroMemory(&BusOp, sizeof(BusOp));
        BusOp.Type = SdResetHost;
        FdoExtension->MiniportInitData.IssueBusOperation(
            FdoExtension->MiniportPrivateExtension,
            &BusOp);
    }

    IsDataError = (FailureStatus == STATUS_SD_DATA_TIMEOUT ||
                   FailureStatus == STATUS_SD_DATA_CRC_ERROR ||
                   FailureStatus == STATUS_SD_ADMA_ERROR);

    if (IsDataError &&
        Request->Command.TransferType == SDTT_MULTI_BLOCK_NO_CMD12)
    {
        SDPORT_REQUEST StopRequest;

        RtlZeroMemory(&StopRequest, sizeof(StopRequest));
        StopRequest.Command.Cmd = SDCMD_STOP_TRANSMISSION;
        StopRequest.Command.CmdClass = SDCC_STANDARD;
        StopRequest.Command.ResponseType = SDRT_1B;
        StopRequest.Command.TransferType = SDTT_CMD_ONLY;
        StopRequest.Command.TransferDirection = SDTD_UNSPECIFIED;
        StopRequest.Argument = 0;
        StopRequest.Status = STATUS_PENDING;

        FdoExtension->MiniportInitData.IssueRequest(
            FdoExtension->MiniportPrivateExtension,
            &StopRequest);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

/**
 * @brief Fill in an SDPORT_REQUEST for a given SD command.
 *
 * Initializes the request structure with command parameters. The caller
 * is responsible for setting up data-related fields separately if the
 * command involves a data transfer.
 *
 * @param[out] Request            The request structure to initialize.
 * @param[in]  CommandIndex       The SD command index (e.g., SDCMD_GO_IDLE_STATE).
 * @param[in]  Argument           The 32-bit command argument.
 * @param[in]  ResponseType       The expected response type (R1, R2, etc.).
 * @param[in]  TransferType       The transfer type (command-only, single block, etc.).
 * @param[in]  TransferDirection  The data transfer direction (read or write).
 */
VOID
SdPortBuildCommand(
    _Out_ PSDPORT_REQUEST Request,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ SD_RESPONSE_TYPE ResponseType,
    _In_ SD_TRANSFER_TYPE TransferType,
    _In_ SD_TRANSFER_DIRECTION TransferDirection)
{
    RtlZeroMemory(Request, sizeof(SDPORT_REQUEST));

    Request->Command.Cmd = CommandIndex;
    Request->Command.CmdClass = SDCC_STANDARD;
    Request->Command.ResponseType = ResponseType;
    Request->Command.TransferType = TransferType;
    Request->Command.TransferDirection = TransferDirection;
    Request->Argument = Argument;
    Request->BlockSize = SD_DEFAULT_BLOCK_SIZE;
    Request->BlockCount = 0;
    Request->BytesTransferred = 0;
    Request->DataBuffer = NULL;
    Request->DataMdl = NULL;
    Request->UseAdma = FALSE;
    Request->Status = STATUS_PENDING;
    Request->State = SdPortRequestStateIdle;
    InitializeListHead(&Request->QueueLink);
    Request->CompletionRoutine = NULL;
    Request->CompletionContext = NULL;
    Request->PioBytesDone = 0;
    Request->PioBytesPerBlock = 0;
}

NTSTATUS
SdPortQueueRequest(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ UCHAR SlotIndex,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ PSDPORT_REQUEST_COMPLETION_ROUTINE CompletionRoutine,
    _In_opt_ PVOID CompletionContext)
{
    PSDPORT_SLOT_EXTENSION SlotExtension;
    KIRQL OldIrql;
    BOOLEAN IssueNow;
    NTSTATUS Status;

    if (SlotIndex >= FdoExtension->SlotCount)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (CompletionRoutine == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SlotExtension = &FdoExtension->Slots[SlotIndex];

    Request->CompletionRoutine = CompletionRoutine;
    Request->CompletionContext = CompletionContext;
    Request->Status = STATUS_PENDING;
    Request->State = SdPortRequestStateIdle;
    Request->PioBytesDone = 0;
    Request->PioBytesPerBlock = Request->BlockSize;

    KeAcquireSpinLock(&SlotExtension->RequestLock, &OldIrql);
    if (SlotExtension->ActiveRequest == NULL)
    {
        SlotExtension->ActiveRequest = Request;
        IssueNow = TRUE;
    }
    else
    {
        InsertTailList(&SlotExtension->PendingRequests, &Request->QueueLink);
        IssueNow = FALSE;
    }
    KeReleaseSpinLock(&SlotExtension->RequestLock, OldIrql);

    if (!IssueNow)
    {
        return STATUS_PENDING;
    }

    SlotExtension->CurrentRequest = Request;
    SlotExtension->RequestHasData =
        (Request->Command.TransferType != SDTT_CMD_ONLY &&
         Request->Command.TransferType != SDTT_UNSPECIFIED);
    SlotExtension->State = SdPortRequestStateCommand;
    Request->State = SdPortRequestStateCommand;

    Status = FdoExtension->MiniportInitData.IssueRequest(
                FdoExtension->MiniportPrivateExtension,
                Request);

    if (!NT_SUCCESS(Status))
    {
        KeAcquireSpinLock(&SlotExtension->RequestLock, &OldIrql);
        SlotExtension->ActiveRequest = NULL;
        SlotExtension->CurrentRequest = NULL;
        KeReleaseSpinLock(&SlotExtension->RequestLock, OldIrql);

        Request->Status = Status;
        CompletionRoutine(FdoExtension, Request, CompletionContext);
        return Status;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Send a single SD command and wait for completion.
 *
 * Enables the relevant completion interrupts, sends the command through the
 * miniport IssueRequest callback, and waits for the interrupt DPC to signal
 * completion. On error, clears any pending error state in the host controller.
 *
 * @param[in]     FdoExtension   Pointer to the FDO device extension.
 * @param[in]     SlotExtension  Pointer to the slot extension for the target slot.
 * @param[in,out] Request        The command request to send; updated with the
 *                               response and final status on return.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSendCommand(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request)
{
    NTSTATUS Status;
    ULONG TimeoutMs;
    BOOLEAN HasData;

    HasData = (Request->Command.TransferType != SDTT_CMD_ONLY &&
               Request->Command.TransferType != SDTT_UNSPECIFIED);

    Request->RetryCount = 0;
    Request->MaxRetries = 0;

Retry:
    /* Reset the completion event */
    KeClearEvent(&SlotExtension->RequestEvent);
    SlotExtension->RequestStatus = STATUS_PENDING;
    SlotExtension->CurrentRequest = Request;
    SlotExtension->RequestHasData = HasData;

    SlotExtension->State = SdPortRequestStateCommand;
    Request->State = SdPortRequestStateCommand;
    Request->CompletionRoutine = NULL;
    Request->CompletionContext = NULL;

    /* Enable the relevant command completion interrupts */
    if (FdoExtension->MiniportInitData.ToggleEvents != NULL)
    {
        ULONG EventMask = SDHCI_INT_CMD_COMPLETE | SDHCI_INT_CMD_ERROR_MASK;

        /* Also enable data interrupts if a data phase is expected */
        if (HasData)
        {
            EventMask |= SDHCI_INT_XFER_COMPLETE |
                         SDHCI_INT_DATA_ERROR_MASK |
                         SDHCI_INT_BUFFER_READ_READY |
                         SDHCI_INT_BUFFER_WRITE_READY |
                         SDHCI_INT_DMA;
        }

        FdoExtension->MiniportInitData.ToggleEvents(
            FdoExtension->MiniportPrivateExtension,
            EventMask,
            TRUE);
    }

    /* Issue the command to the miniport */
    Status = FdoExtension->MiniportInitData.IssueRequest(
                FdoExtension->MiniportPrivateExtension,
                Request);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortSendCommand: IssueRequest failed (0x%08lx) for CMD%u\n",
                Status, Request->Command.Cmd);
        SlotExtension->CurrentRequest = NULL;
        SlotExtension->RequestHasData = FALSE;
        SlotExtension->State = SdPortRequestStateIdle;
        Request->Status = Status;
        return Status;
    }

    /*
     * Some miniports complete requests synchronously from IssueRequest.
     * In that case there is no later interrupt-driven completion to wait for.
     */
    if (Request->Status != STATUS_PENDING)
    {
        Status = Request->Status;
        SlotExtension->CurrentRequest = NULL;
        SlotExtension->RequestHasData = FALSE;
        SlotExtension->State = SdPortRequestStateIdle;

        if (!NT_SUCCESS(Status))
        {
            goto HandleError;
        }

        return Status;
    }

    /*
     * Wait for the interrupt to signal completion.
     * Use a longer timeout for data commands.
     */
    if (HasData)
    {
        TimeoutMs = SD_DATA_TIMEOUT_MS;
    }
    else
    {
        TimeoutMs = SD_CMD_TIMEOUT_MS;
    }

    Status = SdPortWaitForRequestCompletion(SlotExtension, TimeoutMs);
    SlotExtension->CurrentRequest = NULL;
    SlotExtension->RequestHasData = FALSE;
    SlotExtension->State = SdPortRequestStateIdle;
    Request->Status = Status;

    if (NT_SUCCESS(Status))
    {
        return Status;
    }

HandleError:
    DPRINT1("SdPortSendCommand: CMD%u failed with status 0x%08lx (retry %u/%u)\n",
            Request->Command.Cmd, Status,
            Request->RetryCount, Request->MaxRetries);

    SdPortRecoverFromError(FdoExtension, SlotExtension, Request, Status);

    if (Status == STATUS_SD_CMD_CRC_ERROR ||
        Status == STATUS_SD_DATA_CRC_ERROR)
    {
        Request->MaxRetries = 1;
    }
    else if (Status == STATUS_SD_CMD_TIMEOUT ||
             Status == STATUS_SD_DATA_TIMEOUT)
    {
        Request->MaxRetries = 3;
    }
    else
    {
        Request->MaxRetries = 0;
    }

    if (Request->RetryCount < Request->MaxRetries)
    {
        Request->RetryCount++;
        DPRINT1("SdPortSendCommand: retrying CMD%u (attempt %u of %u)\n",
                Request->Command.Cmd,
                Request->RetryCount,
                Request->MaxRetries);
        goto Retry;
    }

    return Status;
}

/**
 * @brief Send an application command (ACMDxx) preceded by CMD55.
 *
 * Sends CMD55 (APP_CMD) first to tell the card that the next command is
 * an application command, then sends the actual ACMD. The RCA is used
 * to address the card (0 for broadcast before the card has an RCA).
 *
 * @param[in]     FdoExtension   Pointer to the FDO device extension.
 * @param[in]     SlotExtension  Pointer to the slot extension for the target slot.
 * @param[in,out] Request        The application command request; updated with
 *                               the response and final status on return.
 * @param[in]     Rca            The relative card address (0 before RCA is assigned).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSendAppCommand(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG Rca)
{
    SDPORT_REQUEST Cmd55;
    NTSTATUS Status;
    SDCMD_DESCRIPTOR SavedCommand;
    ULONG SavedArgument;

    /* Save the actual application command details */
    SavedCommand = Request->Command;
    SavedArgument = Request->Argument;

    /* Build and send CMD55 (APP_CMD) */
    SdPortBuildCommand(&Cmd55,
                       SDCMD_APP_CMD,
                       Rca,
                       SDRT_1,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Cmd55);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortSendAppCommand: CMD55 failed (0x%08lx)\n", Status);
        return Status;
    }

    /* Verify that the card accepted CMD55 (APP_CMD bit should be set in R1) */
    if (!(Cmd55.Response[0] & SD_STATUS_APP_CMD))
    {
        DPRINT1("SdPortSendAppCommand: Card did not accept CMD55\n");
        return STATUS_SD_IO_ERROR;
    }

    /* Restore the application command and mark it as ACMD */
    Request->Command = SavedCommand;
    Request->Command.CmdClass = SDCC_APP_CMD;
    Request->Argument = SavedArgument;

    /* Send the actual application command */
    Status = SdPortSendCommand(FdoExtension, SlotExtension, Request);
    return Status;
}

/**
 * @brief Send a command with a data transfer phase (PIO or ADMA2).
 *
 * Sets up either ADMA2 or PIO transfer depending on slot capabilities,
 * sends CMD55 first if the command is an application command, then issues
 * the data command and waits for completion. Cleans up the MDL on return.
 *
 * @param[in]     FdoExtension   Pointer to the FDO device extension.
 * @param[in]     SlotExtension  Pointer to the slot extension for the target slot.
 * @param[in,out] Request        The command request describing the data operation;
 *                               updated with the response and final status.
 * @param[in]     DataBuffer     Pointer to the data buffer for the transfer.
 * @param[in]     BlockSize      Size of each data block in bytes.
 * @param[in]     BlockCount     Number of blocks to transfer.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSendCommandWithData(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ PVOID DataBuffer,
    _In_ ULONG BlockSize,
    _In_ ULONG BlockCount)
{
    SIZE_T TotalLength;
    NTSTATUS Status;

    TotalLength = (SIZE_T)BlockSize * (SIZE_T)BlockCount;

    Request->BlockSize = BlockSize;
    Request->BlockCount = BlockCount;
    Request->DataBuffer = DataBuffer;

    /*
     * If the command is an application command (set by caller),
     * we need to send CMD55 first and then the data command.
     * However, SdPortSendAppCommand does not handle data.
     * For ACMD commands with data, the caller should send CMD55
     * separately and then call this function.
     *
     * For standard data commands, set up the transfer and issue directly.
     */

    /* Try ADMA2 first, fall back to PIO */
    if (SlotExtension->Capabilities.Adma2Supported &&
        SlotExtension->AdmaDescriptorBuffer != NULL)
    {
        Status = SdPortSetupDmaTransfer(SlotExtension, Request,
                                         DataBuffer, TotalLength);
        if (NT_SUCCESS(Status))
        {
            Request->UseAdma = TRUE;
        }
        else
        {
            /* Fall back to PIO */
            Status = SdPortSetupPioTransfer(SlotExtension, Request,
                                             DataBuffer, TotalLength);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            Request->UseAdma = FALSE;
        }
    }
    else
    {
        Status = SdPortSetupPioTransfer(SlotExtension, Request,
                                         DataBuffer, TotalLength);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Request->UseAdma = FALSE;
    }

    /* If this is an ACMD with data, send CMD55 first */
    if (Request->Command.CmdClass == SDCC_APP_CMD)
    {
        SDPORT_REQUEST Cmd55;

        SdPortBuildCommand(&Cmd55,
                           SDCMD_APP_CMD,
                           SlotExtension->Rca,
                           SDRT_1,
                           SDTT_CMD_ONLY,
                           SDTD_UNSPECIFIED);

        Status = SdPortSendCommand(FdoExtension, SlotExtension, &Cmd55);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdPortSendCommandWithData: CMD55 failed (0x%08lx)\n", Status);
            return Status;
        }
    }

    /* Now send the actual data command */
    Status = SdPortSendCommand(FdoExtension, SlotExtension, Request);

    /*
     * Clean up the MDL allocated by SdPortSetupPioTransfer.
     * If ADMA2 was used, no MDL was allocated.
     */
    if (Request->DataMdl != NULL)
    {
        MmUnlockPages(Request->DataMdl);
        IoFreeMdl(Request->DataMdl);
        Request->DataMdl = NULL;
    }

    return Status;
}
