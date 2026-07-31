/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Power management for FDO and PDO
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"
#include "hardware.h"

#define NDEBUG
#include <debug.h>

static VOID
SdBusCompleteFdoPowerIrp(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp,
    _In_ CCHAR PriorityBoost)
{
    IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
    IoCompleteRequest(Irp, PriorityBoost);
}

static NTSTATUS
SdBusForwardFdoPowerIrp(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    PDEVICE_OBJECT LowerDevice = FdoExtension->LowerDevice;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
    return PoCallDriver(LowerDevice, Irp);
}

static NTSTATUS
NTAPI
SdBusFdoPowerCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event = (PKEVENT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
SdBusForwardPowerIrpAndWait(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SdBusFdoPowerCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = PoCallDriver(FdoExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    return Irp->IoStatus.Status;
}

static NTSTATUS
SdBusQuiesceRequests(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG TimeoutMs)
{
    LARGE_INTEGER PollDelay;
    ULONG Elapsed = 0;
    const ULONG PollStepMs = 10;

    PollDelay.QuadPart = -((LONGLONG)PollStepMs * 10000);

    while (Elapsed < TimeoutMs)
    {
        if (InterlockedCompareExchange(&FdoExtension->OutstandingRequestCount,
                                       0, 0) == 0)
        {
            return STATUS_SUCCESS;
        }

        KeDelayExecutionThread(KernelMode, FALSE, &PollDelay);
        Elapsed += PollStepMs;
    }

    DPRINT1("SdBusQuiesceRequests: timed out waiting for %ld outstanding requests\n",
            FdoExtension->OutstandingRequestCount);
    return STATUS_IO_TIMEOUT;
}

static VOID
SdBusUnblockRequests(
    _In_ PFDO_EXTENSION FdoExtension)
{
    InterlockedExchange(&FdoExtension->RequestsBlocked, 0);
    if (FdoExtension->WorkerStarted &&
        InterlockedCompareExchange(&FdoExtension->WorkerShutdown, 0, 0) == 0)
    {
        /* A deferred SDIO enable may have consumed the earlier wake while the
         * transition gate was closed. Give it another chance now. */
        KeSetEvent(&FdoExtension->RequestArrived, IO_NO_INCREMENT, FALSE);
    }
}

static VOID
SdBusEnableRuntimeInterrupts(
    _In_ PFDO_EXTENSION FdoExtension)
{
    ULONG SignalMask;

    SignalMask = SDHCI_INT_CMD_COMPLETE |
                 SDHCI_INT_XFER_COMPLETE |
                 SDHCI_INT_DMA |
                 SDHCI_INT_BUFFER_WRITE_READY |
                 SDHCI_INT_BUFFER_READ_READY |
                 SDHCI_INT_ERROR |
                 SDHCI_INT_CMD_ERROR_MASK |
                 SDHCI_INT_DATA_ERROR_MASK;
    if (!FdoExtension->NonRemovable)
    {
        SignalMask |= SDHCI_INT_CARD_INSERTION | SDHCI_INT_CARD_REMOVAL;
    }

    SdBusUpdateInterruptSignalEnable(FdoExtension, SignalMask, 0);
    SdBusRefreshCardInterrupt(FdoExtension);
}

static VOID
SdBusRestoreManagedSdioFunctions(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG InsertionGeneration)
{
    PDEVICE_OBJECT FunctionPdos[7];
    ULONG FunctionCount = 0;
    ULONG FunctionIndex;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    for (Entry = FdoExtension->ChildPdoList.Flink;
         Entry != &FdoExtension->ChildPdoList &&
         FunctionCount < RTL_NUMBER_OF(FunctionPdos);
         Entry = Entry->Flink)
    {
        PPDO_EXTENSION Child;

        Child = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
        if (Child->Present && Child->Started &&
            Child->InsertionGeneration == InsertionGeneration &&
            Child->FunctionNumber != 0 &&
            InterlockedCompareExchange(&Child->ManageIoEnable, 0, 0) != 0)
        {
            FunctionPdos[FunctionCount] = Child->Common.Self;
            ObReferenceObject(FunctionPdos[FunctionCount]);
            FunctionCount++;
        }
    }
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    for (FunctionIndex = 0; FunctionIndex < FunctionCount; FunctionIndex++)
    {
        PPDO_EXTENSION Child;
        NTSTATUS Status;

        Child = (PPDO_EXTENSION)FunctionPdos[FunctionIndex]->DeviceExtension;
        Status = SdBusSetSdioFunctionEnabled(Child, TRUE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusRestoreManagedSdioFunctions: function %lu failed "
                    "(0x%08lx)\n", Child->FunctionNumber, Status);
        }
        ObDereferenceObject(FunctionPdos[FunctionIndex]);
    }
}

static
NTSTATUS
SdBusRestoreControllerState(
    _In_ PFDO_EXTENSION FdoExtension)
{
    LARGE_INTEGER Delay;
    ULONG Timeout;
    USHORT ClockControl;
    UCHAR PowerControl;
    ULONG SignalEnable;

    if (!FdoExtension->RegistersMapped)
    {
        return STATUS_SUCCESS;
    }

    InterlockedExchange(&FdoExtension->CurrentClockKhz, 0);
    Delay.QuadPart = -10000LL;

    SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL);
    Timeout = SD_RESET_TIMEOUT_MS;
    while (Timeout > 0)
    {
        if (!(SdBusReadReg8(FdoExtension, SDHCI_SOFTWARE_RESET) & SDHCI_RESET_ALL))
        {
            break;
        }

        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        Timeout--;
    }

    if (Timeout == 0)
    {
        return STATUS_IO_TIMEOUT;
    }

    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
    InterlockedExchange(&FdoExtension->PendingInterruptStatus, 0);
    KeClearEvent(&FdoExtension->CommandEvent);
    SdBusHardwareInitializeController(FdoExtension);

    if (FdoExtension->HostCapabilities & SDHCI_CAP_VOLTAGE_330)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_330 | SDHCI_PC_BUS_POWER_ON;
    }
    else if (FdoExtension->HostCapabilities & SDHCI_CAP_VOLTAGE_300)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_300 | SDHCI_PC_BUS_POWER_ON;
    }
    else if (FdoExtension->HostCapabilities & SDHCI_CAP_VOLTAGE_180)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_180 | SDHCI_PC_BUS_POWER_ON;
    }
    else
    {
        return STATUS_SD_BUS_POWER_ERROR;
    }
    SdBusWriteReg8(FdoExtension, SDHCI_POWER_CONTROL, PowerControl);

    if (FdoExtension->MaxClockFrequency > 0)
    {
        USHORT Divisor;
        USHORT DivisorHigh;

        Divisor = (USHORT)SDHCI_CALC_CLK_DIVIDER(FdoExtension->MaxClockFrequency,
                                            SD_INIT_CLOCK_KHZ);
        if (Divisor > 0x3FF)
        {
            Divisor = 0x3FF;
        }

        DivisorHigh = (Divisor & 0x300) >> 2;
        ClockControl = (USHORT)((Divisor & 0xFF) << SDHCI_CLK_FREQ_SEL_SHIFT);
        ClockControl |= (USHORT)DivisorHigh;
        ClockControl |= SDHCI_CLK_INT_CLK_ENABLE;
        SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockControl);

        Timeout = 200;
        while (Timeout > 0)
        {
            ClockControl = SdBusReadReg16(FdoExtension, SDHCI_CLOCK_CONTROL);
            if (ClockControl & SDHCI_CLK_INT_CLK_STABLE)
            {
                break;
            }

            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
            Timeout--;
        }

        if (Timeout == 0)
        {
            return STATUS_IO_TIMEOUT;
        }

        ClockControl |= SDHCI_CLK_SD_CLK_ENABLE;
        SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockControl);
        InterlockedExchange(&FdoExtension->CurrentClockKhz,
                            (LONG)(Divisor == 0 ?
                                   FdoExtension->MaxClockFrequency :
                                   FdoExtension->MaxClockFrequency /
                                       ((ULONG)Divisor << 1)));
    }

    Delay.QuadPart = -(LONGLONG)SD_POWER_UP_DELAY_MS * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    /* SDHCI full reset restores 1-bit transfers until the card is reconfigured. */
    FdoExtension->CurrentBusWidth = 1;

    SdBusWriteReg8(FdoExtension, SDHCI_TIMEOUT_CONTROL, 0x0E);
    SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);
    SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS_ENABLE,
                    SDHCI_INT_CMD_COMPLETE |
                    SDHCI_INT_XFER_COMPLETE |
                    SDHCI_INT_DMA |
                    SDHCI_INT_BUFFER_WRITE_READY |
                    SDHCI_INT_BUFFER_READ_READY |
                    SDHCI_INT_CARD_INSERTION |
                    SDHCI_INT_CARD_REMOVAL |
                    SDHCI_INT_CARD_INTERRUPT |
                    SDHCI_INT_ERROR |
                    SDHCI_INT_CMD_ERROR_MASK |
                    SDHCI_INT_DATA_ERROR_MASK);
    SignalEnable = SDHCI_INT_CMD_COMPLETE |
                   SDHCI_INT_XFER_COMPLETE |
                   SDHCI_INT_DMA |
                   SDHCI_INT_BUFFER_WRITE_READY |
                   SDHCI_INT_BUFFER_READ_READY |
                   SDHCI_INT_ERROR |
                   SDHCI_INT_CMD_ERROR_MASK |
                   SDHCI_INT_DATA_ERROR_MASK;
    SdBusWriteReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE, SignalEnable);

    return STATUS_SUCCESS;
}

VOID
SdBusReIdentifyChildren(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PLIST_ENTRY Entry;
    PPDO_EXTENSION PdoExtension = NULL;
    SD_CID CachedCid;
    SD_CARD_TYPE CachedCardType;
    ULONG InsertionGeneration;
    UCHAR CachedSdioFunctions;
    USHORT CachedSdioVendorId;
    USHORT CachedSdioDeviceId;
    KIRQL OldIrql;
    NTSTATUS Status;
    BOOLEAN CardSwapped;
    PDEVICE_OBJECT PdoDeviceObject;

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    for (Entry = FdoExtension->ChildPdoList.Flink;
         Entry != &FdoExtension->ChildPdoList;
         Entry = Entry->Flink)
    {
        PPDO_EXTENSION Candidate;

        Candidate = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
        if (Candidate->Present && Candidate->FunctionNumber == 0 &&
            !Candidate->IsEmmcPartition)
        {
            PdoExtension = Candidate;
            ObReferenceObject(PdoExtension->Common.Self);
            break;
        }
    }
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    if (PdoExtension == NULL)
    {
        ULONG PresentState;

        PresentState = SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE);
        if (FdoExtension->NonRemovable ||
            (PresentState & SDHCI_PS_CARD_INSERTED))
        {
            (VOID)SdBusEnumerateInsertedCard(FdoExtension, TRUE, FALSE);
        }
        return;
    }
    PdoDeviceObject = PdoExtension->Common.Self;

    CachedCid = PdoExtension->Cid;
    CachedCardType = PdoExtension->CardType;
    CachedSdioFunctions = PdoExtension->SdioNumFunctions;
    CachedSdioVendorId = PdoExtension->SdioVendorId;
    CachedSdioDeviceId = PdoExtension->SdioDeviceId;
    InsertionGeneration = PdoExtension->InsertionGeneration;

    Status = SdBusEnumerateCard(FdoExtension, PdoExtension, FALSE);
    if (Status == STATUS_SD_RETRY_NO_UHS)
    {
        DPRINT1("SdBusReIdentifyChildren: voltage switch aborted; "
                "retrying at 3.3V without UHS\n");
        Status = SdBusEnumerateCard(FdoExtension, PdoExtension, TRUE);
    }

    CardSwapped = !NT_SUCCESS(Status) ||
                  PdoExtension->CardType != CachedCardType;
    if (!CardSwapped &&
        (CachedCardType == SdCardTypeSdio ||
         CachedCardType == SdCardTypeCombo))
    {
        CardSwapped =
            PdoExtension->SdioNumFunctions != CachedSdioFunctions ||
            PdoExtension->SdioVendorId != CachedSdioVendorId ||
            PdoExtension->SdioDeviceId != CachedSdioDeviceId;
    }
    if (!CardSwapped && CachedCardType != SdCardTypeSdio)
    {
        CardSwapped =
            RtlCompareMemory(&PdoExtension->Cid, &CachedCid,
                             sizeof(SD_CID)) != sizeof(SD_CID);
    }

    if (CardSwapped)
    {
        DPRINT1("SdBusReIdentifyChildren: card identity changed or re-enum "
                "failed (0x%08lx); invalidating generation %lu\n",
                Status, InsertionGeneration);

        KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
        for (Entry = FdoExtension->ChildPdoList.Flink;
             Entry != &FdoExtension->ChildPdoList;
             Entry = Entry->Flink)
        {
            PPDO_EXTENSION Child;

            Child = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
            if (Child->InsertionGeneration == InsertionGeneration)
            {
                Child->Present = FALSE;
                Child->ReportedMissing = TRUE;
            }
        }
        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

        SdBusDeleteMissingInternalSdioHosts(FdoExtension);
        IoInvalidateDeviceRelations(FdoExtension->PhysicalDevice, BusRelations);
        ObDereferenceObject(PdoDeviceObject);
        return;
    }

    (VOID)SdBusSetTransferClock(FdoExtension, PdoExtension);
    SdBusRestoreManagedSdioFunctions(FdoExtension, InsertionGeneration);
    ObDereferenceObject(PdoDeviceObject);
}

/**
 * @brief Handle IRP_MJ_POWER for the FDO.
 *
 * For IRP_MN_SET_POWER: passes system power IRPs down unchanged, handles
 * device power IRPs with a completion routine to restore SDHCI controller
 * state on return to D0, and disables interrupt signals when entering
 * low-power states. For IRP_MN_QUERY_POWER: always succeeds.
 * Unhandled power minor functions are passed to the lower driver.
 *
 * @param[in]     DeviceObject  Pointer to the FDO device object.
 * @param[in,out] Irp           Pointer to the power IRP.
 *
 * @return NTSTATUS from PoCallDriver.
 */
NTSTATUS
SdBusFdoPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFDO_EXTENSION FdoExtension;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    FdoExtension = (PFDO_EXTENSION)DeviceObject->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    Status = IoAcquireRemoveLock(&FdoExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_SET_POWER:
            DPRINT1("SdBusFdoPower: IRP_MN_SET_POWER Type=%d State=%d\n",
                   IoStack->Parameters.Power.Type,
                   IoStack->Parameters.Power.State.DeviceState);

            if (IoStack->Parameters.Power.Type == SystemPowerState)
            {
                /* System power IRP -- just pass down */
                return SdBusForwardFdoPowerIrp(FdoExtension, Irp);
            }

            if (IoStack->Parameters.Power.Type == DevicePowerState)
            {
                DEVICE_POWER_STATE NewState;
                DEVICE_POWER_STATE OldState;
                BOOLEAN BlockedHere;
                LONG PreviousBlock;

                NewState = IoStack->Parameters.Power.State.DeviceState;
                OldState = FdoExtension->Common.DevicePowerState;
                BlockedHere = FALSE;

                if (NewState == PowerDeviceD0 ||
                    (NewState > PowerDeviceD0 && FdoExtension->RegistersMapped))
                {
                    PreviousBlock = InterlockedExchange(
                        &FdoExtension->RequestsBlocked, 1);
                    BlockedHere = (PreviousBlock == 0);
                }

                if (NewState > PowerDeviceD0 && FdoExtension->RegistersMapped)
                {
                    Status = SdBusQuiesceRequests(FdoExtension, 5000);
                    if (!NT_SUCCESS(Status))
                    {
                        if (BlockedHere)
                        {
                            SdBusUnblockRequests(FdoExtension);
                        }
                        PoStartNextPowerIrp(Irp);
                        Irp->IoStatus.Status = Status;
                        SdBusCompleteFdoPowerIrp(FdoExtension, Irp, IO_NO_INCREMENT);
                        return Status;
                    }

                    /* Going to low power -- disable interrupts */
                    SdBusWriteReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE, 0);
                }

                Status = SdBusForwardPowerIrpAndWait(FdoExtension, Irp);
                if (NT_SUCCESS(Status))
                {
                    if (NewState == PowerDeviceD0)
                    {
                        Status = SdBusRestoreControllerState(FdoExtension);
                        if (NT_SUCCESS(Status) && FdoExtension->RegistersMapped)
                        {
                            SdBusReIdentifyChildren(FdoExtension);
                            SdBusUnblockRequests(FdoExtension);
                            SdBusEnableRuntimeInterrupts(FdoExtension);
                        }
                        else if (NT_SUCCESS(Status))
                        {
                            SdBusUnblockRequests(FdoExtension);
                        }
                    }

                    if (NT_SUCCESS(Status))
                    {
                        FdoExtension->Common.DevicePowerState = NewState;
                    }
                }
                else if (BlockedHere)
                {
                    SdBusUnblockRequests(FdoExtension);
                    if (OldState == PowerDeviceD0 && FdoExtension->RegistersMapped)
                    {
                        SdBusEnableRuntimeInterrupts(FdoExtension);
                    }
                }

                PoStartNextPowerIrp(Irp);
                Irp->IoStatus.Status = Status;
                SdBusCompleteFdoPowerIrp(FdoExtension, Irp, IO_NO_INCREMENT);
                return Status;
            }

            /* Unrecognized power type, pass down */
            return SdBusForwardFdoPowerIrp(FdoExtension, Irp);

        case IRP_MN_QUERY_POWER:
            /* Always succeed power queries */
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return SdBusForwardFdoPowerIrp(FdoExtension, Irp);

        case IRP_MN_WAIT_WAKE:
        default:
            /* Pass down unhandled power IRPs */
            return SdBusForwardFdoPowerIrp(FdoExtension, Irp);
    }
}

/**
 * @brief Handle IRP_MJ_POWER for a child SD card PDO.
 *
 * For IRP_MN_SET_POWER with DevicePowerState: updates the tracked device
 * power state. For IRP_MN_QUERY_POWER: always succeeds.
 * IRP_MN_WAIT_WAKE and other power minors are completed with
 * STATUS_NOT_SUPPORTED. Always calls PoStartNextPowerIrp.
 *
 * @param[in]     DeviceObject  Pointer to the PDO device object.
 * @param[in,out] Irp           Pointer to the power IRP.
 *
 * @return STATUS_SUCCESS or STATUS_NOT_SUPPORTED.
 */
NTSTATUS
SdBusPdoPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPDO_EXTENSION PdoExtension;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    PdoExtension = (PPDO_EXTENSION)DeviceObject->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    Status = IoAcquireRemoveLock(&PdoExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_SET_POWER:
            DPRINT1("SdBusPdoPower: IRP_MN_SET_POWER Type=%d\n",
                   IoStack->Parameters.Power.Type);

            if (IoStack->Parameters.Power.Type == DevicePowerState)
            {
                PdoExtension->Common.DevicePowerState =
                    IoStack->Parameters.Power.State.DeviceState;
            }

            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_POWER:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_WAIT_WAKE:
            /* Not supported for now */
            Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            break;

        default:
            Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            break;
    }

    Status = Irp->IoStatus.Status;
    PoStartNextPowerIrp(Irp);
    IoReleaseRemoveLock(&PdoExtension->RemoveLock, Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
