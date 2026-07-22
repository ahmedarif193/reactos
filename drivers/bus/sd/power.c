/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Power management for FDO and PDO
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#define NDEBUG
#include <debug.h>

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
        Status = Irp->IoStatus.Status;
    }

    return Status;
}

static VOID
SdBusQuiesceRequests(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG TimeoutMs)
{
    LARGE_INTEGER PollDelay;
    ULONG Elapsed = 0;
    const ULONG PollStepMs = 10;

    if (!FdoExtension->WorkerStarted)
    {
        return;
    }

    PollDelay.QuadPart = -((LONGLONG)PollStepMs * 10000);

    while (Elapsed < TimeoutMs)
    {
        if (InterlockedCompareExchange(&FdoExtension->OutstandingRequestCount,
                                       0, 0) == 0)
        {
            return;
        }

        KeDelayExecutionThread(KernelMode, FALSE, &PollDelay);
        Elapsed += PollStepMs;
    }

    DPRINT1("SdBusQuiesceRequests: timed out waiting for %ld outstanding requests\n",
            FdoExtension->OutstandingRequestCount);
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

    if (!FdoExtension->RegistersMapped)
    {
        return STATUS_SUCCESS;
    }

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

    if (FdoExtension->HostCapabilities & SDHCI_CAP_VOLTAGE_330)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_330 | SDHCI_PC_BUS_POWER_ON;
    }
    else if (FdoExtension->HostCapabilities & SDHCI_CAP_VOLTAGE_300)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_300 | SDHCI_PC_BUS_POWER_ON;
    }
    else
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_180 | SDHCI_PC_BUS_POWER_ON;
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
                    SDHCI_INT_BUFFER_WRITE_READY |
                    SDHCI_INT_BUFFER_READ_READY |
                    SDHCI_INT_CARD_INSERTION |
                    SDHCI_INT_CARD_REMOVAL |
                    SDHCI_INT_CARD_INTERRUPT |
                    SDHCI_INT_ERROR |
                    SDHCI_INT_CMD_ERROR_MASK |
                    SDHCI_INT_DATA_ERROR_MASK);
    SdBusWriteReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE,
                    SDHCI_INT_CMD_COMPLETE |
                    SDHCI_INT_XFER_COMPLETE |
                    SDHCI_INT_BUFFER_WRITE_READY |
                    SDHCI_INT_BUFFER_READ_READY |
                    SDHCI_INT_CARD_INSERTION |
                    SDHCI_INT_CARD_REMOVAL |
                    SDHCI_INT_CARD_INTERRUPT |
                    SDHCI_INT_ERROR |
                    SDHCI_INT_CMD_ERROR_MASK |
                    SDHCI_INT_DATA_ERROR_MASK);

    return STATUS_SUCCESS;
}

static VOID
SdBusReIdentifyChildren(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PLIST_ENTRY Entry;
    PPDO_EXTENSION PdoExtension;
    SD_CID CachedCid;
    KIRQL OldIrql;
    NTSTATUS Status;
    BOOLEAN CardSwapped = FALSE;

    if (FdoExtension->ChildPdoCount == 0)
    {
        return;
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    Entry = FdoExtension->ChildPdoList.Flink;
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    while (Entry != &FdoExtension->ChildPdoList)
    {
        PLIST_ENTRY Next = Entry->Flink;
        PdoExtension = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);

        if (!PdoExtension->Present)
        {
            Entry = Next;
            continue;
        }

        CachedCid = PdoExtension->Cid;

        Status = SdBusEnumerateCard(FdoExtension, PdoExtension, FALSE);
        if (Status == STATUS_SD_RETRY_NO_UHS)
        {
            DPRINT1("SdBusReIdentifyChildren: voltage switch aborted; "
                    "retrying at 3.3V without UHS\n");
            Status = SdBusEnumerateCard(FdoExtension, PdoExtension, TRUE);
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusReIdentifyChildren: Re-enum failed (0x%08lx); "
                    "marking PDO absent\n", Status);
            PdoExtension->Present = FALSE;
            PdoExtension->ReportedMissing = TRUE;
            CardSwapped = TRUE;
            Entry = Next;
            continue;
        }

        if (RtlCompareMemory(&PdoExtension->Cid, &CachedCid,
                             sizeof(SD_CID)) != sizeof(SD_CID))
        {
            DPRINT1("SdBusReIdentifyChildren: CID changed during sleep "
                    "(old PSN=%08lx new PSN=%08lx); invalidating PDO\n",
                    CachedCid.ProductSerialNumber,
                    PdoExtension->Cid.ProductSerialNumber);
            PdoExtension->Present = FALSE;
            PdoExtension->ReportedMissing = TRUE;
            CardSwapped = TRUE;
            Entry = Next;
            continue;
        }

        (VOID)SdBusSetTransferClock(FdoExtension, PdoExtension);
        Entry = Next;
    }

    if (CardSwapped)
    {
        IoInvalidateDeviceRelations(FdoExtension->PhysicalDevice, BusRelations);
    }
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

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_SET_POWER:
            DPRINT1("SdBusFdoPower: IRP_MN_SET_POWER Type=%d State=%d\n",
                   IoStack->Parameters.Power.Type,
                   IoStack->Parameters.Power.State.DeviceState);

            if (IoStack->Parameters.Power.Type == SystemPowerState)
            {
                /* System power IRP -- just pass down */
                PoStartNextPowerIrp(Irp);
                IoSkipCurrentIrpStackLocation(Irp);
                return PoCallDriver(FdoExtension->LowerDevice, Irp);
            }

            if (IoStack->Parameters.Power.Type == DevicePowerState)
            {
                DEVICE_POWER_STATE NewState;
                NewState = IoStack->Parameters.Power.State.DeviceState;

                if (NewState > PowerDeviceD0 && FdoExtension->RegistersMapped)
                {
                    SdBusQuiesceRequests(FdoExtension, 5000);

                    /* Going to low power -- disable interrupts */
                    SdBusWriteReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE, 0);
                }

                Status = SdBusForwardPowerIrpAndWait(FdoExtension, Irp);
                if (NT_SUCCESS(Status))
                {
                    FdoExtension->Common.DevicePowerState = NewState;

                    if (NewState == PowerDeviceD0)
                    {
                        Status = SdBusRestoreControllerState(FdoExtension);
                        if (NT_SUCCESS(Status))
                        {
                            SdBusReIdentifyChildren(FdoExtension);
                        }
                    }
                }

                PoStartNextPowerIrp(Irp);
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }

            /* Unrecognized power type, pass down */
            PoStartNextPowerIrp(Irp);
            IoSkipCurrentIrpStackLocation(Irp);
            return PoCallDriver(FdoExtension->LowerDevice, Irp);

        case IRP_MN_QUERY_POWER:
            /* Always succeed power queries */
            PoStartNextPowerIrp(Irp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            return PoCallDriver(FdoExtension->LowerDevice, Irp);

        case IRP_MN_WAIT_WAKE:
        default:
            /* Pass down unhandled power IRPs */
            PoStartNextPowerIrp(Irp);
            IoSkipCurrentIrpStackLocation(Irp);
            Status = PoCallDriver(FdoExtension->LowerDevice, Irp);
            return Status;
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
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
