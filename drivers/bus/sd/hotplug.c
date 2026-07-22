/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Card detect ISR, DPC, and worker (hotplug handling)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#define NDEBUG
#include <debug.h>

/**
 * @brief SDHCI Interrupt Service Routine (ISR).
 *
 * Reads the SDHCI interrupt status register and handles card insertion,
 * card removal, and SDIO card interrupt events. Acknowledged events are
 * masked at the signal-enable level to prevent interrupt storms, and
 * the pending status is accumulated atomically for the DPC to consume.
 * Command/data completion interrupts are acknowledged, accumulated in
 * CommandInterruptStatus, and a DPC is queued to signal CommandEvent.
 *
 * @param[in] Interrupt      Pointer to the interrupt object (unused).
 * @param[in] ServiceContext  Pointer to the FDO_EXTENSION (ISR context).
 *
 * @return TRUE if the interrupt was claimed and handled, FALSE otherwise.
 */
BOOLEAN
NTAPI
SdBusInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PFDO_EXTENSION FdoExtension = (PFDO_EXTENSION)ServiceContext;
    ULONG IntStatus;
    ULONG CmdDataBits;
    ULONG DmaBit;
    ULONG CardBits;
    ULONG CardSignalEnable;

    UNREFERENCED_PARAMETER(Interrupt);

    if (FdoExtension->RegisterBase == NULL)
    {
        return FALSE;
    }

    if (FdoExtension->Common.DeviceState != SdBusDeviceStateStarted)
    {
        return FALSE;
    }

    IntStatus = SdBusReadReg32(FdoExtension, SDHCI_INT_STATUS);

    if (IntStatus == 0 || IntStatus == 0xFFFFFFFF)
    {
        return FALSE;
    }

    CmdDataBits = IntStatus & (SDHCI_INT_CMD_COMPLETE |
                               SDHCI_INT_XFER_COMPLETE |
                               SDHCI_INT_BUFFER_READ_READY |
                               SDHCI_INT_BUFFER_WRITE_READY |
                               SDHCI_INT_ERROR |
                               SDHCI_INT_CMD_ERROR_MASK |
                               SDHCI_INT_DATA_ERROR_MASK);

    DmaBit = IntStatus & SDHCI_INT_DMA;

    CardSignalEnable = SdBusReadReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE);
    CardBits = IntStatus & CardSignalEnable & (SDHCI_INT_CARD_INSERTION |
                                               SDHCI_INT_CARD_REMOVAL |
                                               SDHCI_INT_CARD_INTERRUPT);

    if ((CmdDataBits | DmaBit | CardBits) == 0)
    {
        return FALSE;
    }

    if (CmdDataBits)
    {
        SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS, CmdDataBits);
        InterlockedOr(&FdoExtension->CommandInterruptStatus, (LONG)CmdDataBits);
        KeInsertQueueDpc(&FdoExtension->CommandDpc, NULL, NULL);
    }

    if (DmaBit)
    {
        ULONG NextAddr = SdBusReadReg32(FdoExtension, SDHCI_SDMA_ADDRESS);
        SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS, SDHCI_INT_DMA);
        SdBusWriteReg32(FdoExtension, SDHCI_SDMA_ADDRESS, NextAddr);
    }

    if (CardBits)
    {
        SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS, CardBits);

        if (CardBits & (SDHCI_INT_CARD_INSERTION | SDHCI_INT_CARD_REMOVAL))
        {
            ULONG SignalEnable;
            SignalEnable = SdBusReadReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE);
            SignalEnable &= ~(SDHCI_INT_CARD_INSERTION | SDHCI_INT_CARD_REMOVAL);
            SdBusWriteReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE, SignalEnable);
        }

        if (CardBits & SDHCI_INT_CARD_INTERRUPT)
        {
            ULONG SignalEnable;
            ULONG StatusEnable;
            StatusEnable = SdBusReadReg32(FdoExtension, SDHCI_INT_STATUS_ENABLE);
            StatusEnable &= ~SDHCI_INT_CARD_INTERRUPT;
            SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS_ENABLE, StatusEnable);
            SignalEnable = SdBusReadReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE);
            SignalEnable &= ~SDHCI_INT_CARD_INTERRUPT;
            SdBusWriteReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE, SignalEnable);
        }

        InterlockedOr((volatile LONG *)&FdoExtension->PendingInterruptStatus,
                      (LONG)CardBits);
        KeInsertQueueDpc(&FdoExtension->CardDetectDpc, NULL, NULL);
    }

    return TRUE;
}

/**
 * @brief Create a child PDO for a newly detected SD card.
 *
 * Must be called at PASSIVE_LEVEL. Creates the device object, initializes
 * the PDO extension with default state, sets up the remove lock, and
 * configures the device for direct I/O.
 *
 * @param[in]  FdoExtension     Pointer to the parent FDO device extension.
 * @param[out] OutPdoExtension  Receives a pointer to the new PDO extension on success,
 *                               or NULL on failure.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
static NTSTATUS
SdBusCreateChildPdo(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_ PPDO_EXTENSION *OutPdoExtension)
{
    PDEVICE_OBJECT Pdo;
    PPDO_EXTENSION PdoExtension;
    NTSTATUS Status;

    Status = IoCreateDevice(FdoExtension->Common.Self->DriverObject,
                            sizeof(PDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_MASS_STORAGE,
                            FILE_AUTOGENERATED_DEVICE_NAME,
                            FALSE,
                            &Pdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusCreateChildPdo: IoCreateDevice failed (0x%08lx)\n", Status);
        *OutPdoExtension = NULL;
        return Status;
    }

    PdoExtension = (PPDO_EXTENSION)Pdo->DeviceExtension;
    RtlZeroMemory(PdoExtension, sizeof(PDO_EXTENSION));

    PdoExtension->Common.Self = Pdo;
    PdoExtension->Common.IsFdo = FALSE;
    PdoExtension->Common.DeviceState = SdBusDeviceStateStopped;
    PdoExtension->Common.DevicePowerState = PowerDeviceD0;

    PdoExtension->FdoExtension = FdoExtension;
    PdoExtension->Present = TRUE;
    PdoExtension->ReportedMissing = FALSE;
    PdoExtension->Started = FALSE;
    PdoExtension->InterfaceReferenceCount = 0;

    InitializeListHead(&PdoExtension->ListEntry);
    IoInitializeRemoveLock(&PdoExtension->RemoveLock, TAG_SDBUS, 0, 0);

    /* Inherit I/O method from the FDO */
    Pdo->Flags |= DO_DIRECT_IO;
    Pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    *OutPdoExtension = PdoExtension;
    return STATUS_SUCCESS;
}

NTSTATUS
SdBusEnumerateInsertedCard(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN NotifyPnp,
    _In_ BOOLEAN Debounce)
{
    ULONG PresentState;
    PPDO_EXTENSION NewPdo = NULL;
    BOOLEAN CardAlreadyEnumerated = FALSE;
    PLIST_ENTRY Entry;
    PPDO_EXTENSION ExistingPdo;
    ULONG CurrentGeneration;
    KIRQL OldIrql;
    LARGE_INTEGER Delay;
    NTSTATUS Status;

    CurrentGeneration = (ULONG)InterlockedIncrement(
        &FdoExtension->InsertionGeneration);

    if (Debounce)
    {
        Delay.QuadPart = -(50LL * 10000LL);
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    PresentState = SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE);
    if (!(PresentState & SDHCI_PS_CARD_INSERTED))
    {
        DPRINT1("SdBusEnumerateInsertedCard: insert bounced, card gone "
                "(gen=%lu)\n", CurrentGeneration);
        return STATUS_NO_MEDIA_IN_DEVICE;
    }

    {
        ULONG StableTimeout = 100;
        while (StableTimeout > 0)
        {
            PresentState = SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE);
            if (PresentState & SDHCI_PS_CARD_STATE_STABLE)
            {
                break;
            }

            Delay.QuadPart = -10000;
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
            StableTimeout--;
        }
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    for (Entry = FdoExtension->ChildPdoList.Flink;
         Entry != &FdoExtension->ChildPdoList;
         Entry = Entry->Flink)
    {
        ExistingPdo = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
        if (ExistingPdo->Present)
        {
            CardAlreadyEnumerated = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    if (CardAlreadyEnumerated)
    {
        return STATUS_SUCCESS;
    }

    Status = SdBusCreateChildPdo(FdoExtension, &NewPdo);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    NewPdo->InsertionGeneration = CurrentGeneration;

    Status = SdBusEnumerateCard(FdoExtension, NewPdo, FALSE);
    if (Status == STATUS_SD_RETRY_NO_UHS)
    {
        DPRINT1("SdBusEnumerateInsertedCard: voltage switch aborted; "
                "retrying enumeration at 3.3V without UHS\n");
        Status = SdBusEnumerateCard(FdoExtension, NewPdo, TRUE);
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusEnumerateInsertedCard: card enumeration failed "
                "(0x%08lx)\n", Status);
        IoDeleteDevice(NewPdo->Common.Self);
        return Status;
    }

    Status = SdBusSetTransferClock(FdoExtension, NewPdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusEnumerateInsertedCard: clock raise failed "
                "(0x%08lx), continuing at init speed\n", Status);
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    InsertTailList(&FdoExtension->ChildPdoList, &NewPdo->ListEntry);
    FdoExtension->ChildPdoCount++;
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    if (NotifyPnp)
    {
        IoInvalidateDeviceRelations(FdoExtension->PhysicalDevice, BusRelations);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Work item callback for card detect events at PASSIVE_LEVEL.
 *
 * Atomically reads and clears the pending interrupt status accumulated by
 * the ISR. Processes card insertion (creates PDO, enumerates the card,
 * notifies PnP), card removal (marks PDOs absent, notifies PnP), and
 * SDIO card interrupts (invokes the function driver callback). Re-enables
 * card-detect interrupt signals and frees the work item before returning.
 *
 * @param[in] DeviceObject  Pointer to the FDO device object.
 * @param[in] Context       Pointer to the IO_WORKITEM (freed on return).
 */
static VOID
NTAPI
SdBusCardDetectWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PFDO_EXTENSION FdoExtension;
    PIO_WORKITEM WorkItem = (PIO_WORKITEM)Context;
    ULONG PendingStatus;
    KIRQL OldIrql;

    if (DeviceObject == NULL || WorkItem == NULL)
    {
        return;
    }
    FdoExtension = (PFDO_EXTENSION)DeviceObject->DeviceExtension;
    if (FdoExtension == NULL)
    {
        IoFreeWorkItem(WorkItem);
        return;
    }

    /* Atomically read and clear pending interrupt status */
    PendingStatus = (ULONG)InterlockedExchange(
        (volatile LONG *)&FdoExtension->PendingInterruptStatus, 0);

    /*
     * Handle card insertion (at PASSIVE_LEVEL — safe for IoCreateDevice,
     * card enumeration with KeDelayExecutionThread, etc.)
     */
    if (PendingStatus & SDHCI_INT_CARD_INSERTION)
    {
        (VOID)SdBusEnumerateInsertedCard(FdoExtension, TRUE, TRUE);
    }

    /*
     * Handle card removal
     */
    if (PendingStatus & SDHCI_INT_CARD_REMOVAL)
    {
        PLIST_ENTRY Entry;
        PPDO_EXTENSION PdoExtension;

        /* Mark all child PDOs as absent */
        KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);

        for (Entry = FdoExtension->ChildPdoList.Flink;
             Entry != &FdoExtension->ChildPdoList;
             Entry = Entry->Flink)
        {
            PdoExtension = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
            PdoExtension->Present = FALSE;
            PdoExtension->ReportedMissing = TRUE;
        }

        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

        /* Notify PnP manager */
        IoInvalidateDeviceRelations(FdoExtension->PhysicalDevice,
                                    BusRelations);
    }

    /*
     * Handle SDIO card interrupt — invoke callback OUTSIDE the spinlock
     * to prevent deadlock if the callback submits new SD requests.
     */
    if (PendingStatus & SDHCI_INT_CARD_INTERRUPT)
    {
        PLIST_ENTRY Entry;
        PPDO_EXTENSION PdoExtension;
        PSDBUS_CALLBACK_ROUTINE Callback;
        PVOID CallbackCtx;

        /* Snapshot callback under the lock, invoke outside */
        KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);

        for (Entry = FdoExtension->ChildPdoList.Flink;
             Entry != &FdoExtension->ChildPdoList;
             Entry = Entry->Flink)
        {
            PdoExtension = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
            if (PdoExtension->Present && PdoExtension->CallbackRoutine != NULL)
            {
                Callback = PdoExtension->CallbackRoutine;
                CallbackCtx = PdoExtension->CallbackContext;

                KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

                Callback(CallbackCtx, 0);

                KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
                /* Restart from list head — list may have changed */
                break;
            }
        }

        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);
    }

    /* Re-enable card detect interrupt signals */
    SdBusUpdateInterruptSignalEnable(FdoExtension,
                                     SDHCI_INT_CARD_INSERTION |
                                     SDHCI_INT_CARD_REMOVAL,
                                     0);

    /* Free the work item */
    IoFreeWorkItem(WorkItem);
    IoReleaseRemoveLock(&FdoExtension->RemoveLock, WorkItem);
}

/**
 * @brief Command/data completion DPC.
 *
 * Runs at DISPATCH_LEVEL after the ISR queues it. Signals CommandEvent
 * to wake the thread waiting in SdBusWaitForInterrupt.
 */
VOID
NTAPI
SdBusCommandCompletionDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PFDO_EXTENSION FdoExtension = (PFDO_EXTENSION)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeSetEvent(&FdoExtension->CommandEvent, IO_DISK_INCREMENT, FALSE);
}

/**
 * @brief Card detect DPC that queues a work item at PASSIVE_LEVEL.
 *
 * Allocates an IO_WORKITEM and queues SdBusCardDetectWorker on the
 * delayed work queue. Card enumeration requires PASSIVE_LEVEL for
 * IoCreateDevice, KeDelayExecutionThread, and IoInvalidateDeviceRelations.
 * If allocation fails, re-enables card-detect interrupt signals so future
 * events are not lost.
 *
 * @param[in] Dpc              Pointer to the DPC object (unused).
 * @param[in] DeferredContext   Pointer to the FDO_EXTENSION.
 * @param[in] SystemArgument1  System-supplied argument (unused).
 * @param[in] SystemArgument2  System-supplied argument (unused).
 */
VOID
NTAPI
SdBusCardDetectDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PFDO_EXTENSION FdoExtension = (PFDO_EXTENSION)DeferredContext;
    PIO_WORKITEM WorkItem;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (FdoExtension == NULL)
    {
        return;
    }

    if (FdoExtension->Common.DeviceState != SdBusDeviceStateStarted)
    {
        return;
    }

    if (FdoExtension->PendingInterruptStatus & SDHCI_INT_CARD_INTERRUPT)
    {
        PLIST_ENTRY Entry;
        PPDO_EXTENSION PdoExtension;
        PSDBUS_CALLBACK_ROUTINE Callback;
        PVOID CallbackCtx;
        KIRQL OldIrql;
        BOOLEAN Handled = FALSE;

        KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
        for (Entry = FdoExtension->ChildPdoList.Flink;
             Entry != &FdoExtension->ChildPdoList;
             Entry = Entry->Flink)
        {
            PdoExtension = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
            if (PdoExtension->Present && PdoExtension->CallbackRoutine != NULL &&
                PdoExtension->CallbackAtDpcLevel)
            {
                Callback = PdoExtension->CallbackRoutine;
                CallbackCtx = PdoExtension->CallbackContext;
                KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);
                Callback(CallbackCtx, 0);
                KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
                Handled = TRUE;
                break;
            }
        }
        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

        if (Handled)
        {
            InterlockedAnd((volatile LONG *)&FdoExtension->PendingInterruptStatus,
                           ~(LONG)SDHCI_INT_CARD_INTERRUPT);
        }
    }

    if (!(FdoExtension->PendingInterruptStatus &
          (SDHCI_INT_CARD_INSERTION | SDHCI_INT_CARD_REMOVAL |
           SDHCI_INT_CARD_INTERRUPT)))
    {
        return;
    }

    WorkItem = IoAllocateWorkItem(FdoExtension->Common.Self);
    if (WorkItem == NULL)
    {
        DPRINT1("SdBusCardDetectDpc: Failed to allocate work item\n");
        SdBusUpdateInterruptSignalEnable(FdoExtension,
                                         SDHCI_INT_CARD_INSERTION |
                                         SDHCI_INT_CARD_REMOVAL,
                                         0);
        return;
    }

    Status = IoAcquireRemoveLock(&FdoExtension->RemoveLock, WorkItem);
    if (!NT_SUCCESS(Status))
    {
        IoFreeWorkItem(WorkItem);
        return;
    }

    IoQueueWorkItem(WorkItem,
                    SdBusCardDetectWorker,
                    DelayedWorkQueue,
                    WorkItem);
}
