/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SDBUS_INTERFACE_STANDARD implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#define NDEBUG
#include <debug.h>

static BOOLEAN
SdBusHasInterruptCallbackLocked(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PLIST_ENTRY Entry;
    PPDO_EXTENSION PdoExtension;

    for (Entry = FdoExtension->ChildPdoList.Flink;
         Entry != &FdoExtension->ChildPdoList;
         Entry = Entry->Flink)
    {
        PdoExtension = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
        if (PdoExtension->Present &&
            PdoExtension->CardInterruptForwardEnabled &&
            PdoExtension->CallbackRoutine != NULL)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static VOID
SdBusUpdateCardInterruptLocked(
    _In_ PFDO_EXTENSION FdoExtension)
{
    if (SdBusHasInterruptCallbackLocked(FdoExtension) &&
        InterlockedCompareExchange(&FdoExtension->PendingSdioAcks, 0, 0) == 0)
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
}

VOID
SdBusRefreshCardInterrupt(
    _In_ PFDO_EXTENSION FdoExtension)
{
    KIRQL OldIrql;

    if (FdoExtension == NULL)
    {
        return;
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    SdBusUpdateCardInterruptLocked(FdoExtension);
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);
}

/**
 * @brief Increment the SD bus interface reference count.
 *
 * @param[in] Context  Pointer to the SDBUS_INTERFACE_CONTEXT for this interface instance.
 */
VOID
NTAPI
SdBusInterfaceReference(
    _In_ PVOID Context)
{
    PSDBUS_INTERFACE_CONTEXT InterfaceContext;

    InterfaceContext = (PSDBUS_INTERFACE_CONTEXT)Context;
    (VOID)SdBusReferenceInterfaceContext(InterfaceContext);
}

BOOLEAN
SdBusReferenceInterfaceContext(
    _In_ PSDBUS_INTERFACE_CONTEXT InterfaceContext)
{
    LONG ReferenceCount;

    if (InterfaceContext == NULL || InterfaceContext->PdoExtension == NULL)
    {
        return FALSE;
    }

    ReferenceCount = InterlockedCompareExchange(&InterfaceContext->ReferenceCount,
                                                0,
                                                0);
    while (ReferenceCount > 0)
    {
        LONG PreviousCount;

        if (ReferenceCount == MAXLONG)
        {
            return FALSE;
        }

        PreviousCount = InterlockedCompareExchange(
            &InterfaceContext->ReferenceCount,
            ReferenceCount + 1,
            ReferenceCount);
        if (PreviousCount == ReferenceCount)
        {
            return TRUE;
        }
        ReferenceCount = PreviousCount;
    }

    return FALSE;
}

/**
 * @brief Decrement the SD bus interface reference count.
 *
 * Frees the interface context allocation when the last reference is released.
 *
 * @param[in] Context  Pointer to the SDBUS_INTERFACE_CONTEXT for this interface instance.
 */
VOID
NTAPI
SdBusInterfaceDereference(
    _In_ PVOID Context)
{
    PSDBUS_INTERFACE_CONTEXT InterfaceContext;
    PPDO_EXTENSION PdoExtension;
    PFDO_EXTENSION FdoExtension;
    PDEVICE_OBJECT TargetObject;
    PDEVICE_OBJECT PdoDeviceObject;
    PDEVICE_OBJECT FdoDeviceObject;
    LONG NewCount;
    KIRQL OldIrql;

    InterfaceContext = (PSDBUS_INTERFACE_CONTEXT)Context;
    if (InterfaceContext == NULL || InterfaceContext->PdoExtension == NULL)
    {
        return;
    }

    PdoExtension = InterfaceContext->PdoExtension;
    PdoDeviceObject = InterfaceContext->PdoDeviceObject;
    FdoDeviceObject = InterfaceContext->FdoDeviceObject;
    NewCount = InterlockedDecrement(&InterfaceContext->ReferenceCount);

    if (NewCount == 0)
    {
        KeAcquireSpinLock(&InterfaceContext->TargetLock, &OldIrql);
        TargetObject = InterfaceContext->TargetObject;
        InterfaceContext->TargetObject = NULL;
        KeReleaseSpinLock(&InterfaceContext->TargetLock, OldIrql);

        FdoExtension = PdoExtension->FdoExtension;
        if (FdoExtension != NULL)
        {
            KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
            if (PdoExtension->InitializedInterfaceContext == InterfaceContext)
            {
                PdoExtension->InitializedInterfaceContext = NULL;
                InterlockedExchange(&PdoExtension->ManageIoEnable, 0);
                InterlockedExchange(&PdoExtension->SdioEnablePending, 0);
            }
            if (PdoExtension->CallbackInterfaceContext == InterfaceContext)
            {
                PdoExtension->CallbackRoutine = NULL;
                PdoExtension->CallbackContext = NULL;
                PdoExtension->CallbackInterfaceContext = NULL;
                PdoExtension->CallbackAtDpcLevel = FALSE;
                PdoExtension->CardInterruptForwardEnabled = FALSE;
            }
            InterfaceContext->Initialized = FALSE;
            if (InterlockedExchange(&InterfaceContext->InterruptOutstanding, 0) != 0)
            {
                InterlockedDecrement(&FdoExtension->PendingSdioAcks);
            }
            SdBusUpdateCardInterruptLocked(FdoExtension);
            KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);
        }

        ExFreePoolWithTag(InterfaceContext, TAG_SDBUS);
        if (TargetObject != NULL)
        {
            ObDereferenceObject(TargetObject);
        }
        ObDereferenceObject(PdoDeviceObject);
        ObDereferenceObject(FdoDeviceObject);
    }
}

/**
 * @brief Initialize the SD bus interface with function driver callback parameters.
 *
 * Stores the callback routine, context, target block size, and interrupt
 * generation flag from the function driver. Also stores the callback in the
 * PDO extension for ISR dispatch. Enables SDIO card interrupt signal
 * delivery if the device generates interrupts.
 *
 * @param[in] Context              Pointer to the SDBUS_INTERFACE_CONTEXT for this instance.
 * @param[in] InterfaceParameters  Pointer to SDBUS_INTERFACE_PARAMETERS from the function driver.
 *
 * @return STATUS_SUCCESS on success, or STATUS_INVALID_PARAMETER.
 */
NTSTATUS
NTAPI
SdBusInitializeInterfaceImpl(
    _In_ PVOID Context,
    _In_ PSDBUS_INTERFACE_PARAMETERS InterfaceParameters)
{
    PSDBUS_INTERFACE_CONTEXT InterfaceContext;
    PSDBUS_INTERFACE_PARAMETERS Params;
    PPDO_EXTENSION PdoExtension;
    PFDO_EXTENSION FdoExtension;
    PSDBUS_INTERFACE_CONTEXT PreviousOwner;
    KIRQL OldIrql;
    NTSTATUS Status;
    BOOLEAN ManageIoEnable;
    BOOLEAN ClaimedInitialization = FALSE;
    LONG PreviousManageIoEnable;
    PDEVICE_OBJECT PreviousTarget;

    InterfaceContext = (PSDBUS_INTERFACE_CONTEXT)Context;
    if (InterfaceContext == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Params = InterfaceParameters;
    if (Params == NULL || Params->Size < sizeof(SDBUS_INTERFACE_PARAMETERS) ||
        Params->TargetObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PdoExtension = InterfaceContext->PdoExtension;
    if (PdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((Params->SdioFlags & ~(SDIO_FLAG_DO_NOT_MANAGE_IO_ENABLE |
                               SDIO_FLAG_SDIO_ENABLE_POLLING)) != 0 ||
        (Params->DeviceGeneratesInterrupts && Params->CallbackRoutine == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Params->DeviceGeneratesInterrupts &&
        (PdoExtension->FunctionNumber == 0 ||
         (PdoExtension->CardType != SdCardTypeSdio &&
          PdoExtension->CardType != SdCardTypeCombo)))
    {
        return STATUS_NOT_SUPPORTED;
    }

    FdoExtension = PdoExtension->FdoExtension;
    if (FdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    PreviousOwner = (PSDBUS_INTERFACE_CONTEXT)
        PdoExtension->InitializedInterfaceContext;
    if (PreviousOwner != NULL && PreviousOwner != InterfaceContext)
    {
        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }
    if (PreviousOwner == NULL)
    {
        PdoExtension->InitializedInterfaceContext = InterfaceContext;
        ClaimedInitialization = TRUE;
    }
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    ManageIoEnable = PdoExtension->FunctionNumber != 0 &&
        !(Params->SdioFlags & SDIO_FLAG_DO_NOT_MANAGE_IO_ENABLE);
    PreviousManageIoEnable = InterlockedExchange(
        &PdoExtension->ManageIoEnable, ManageIoEnable ? 1 : 0);
    if (ManageIoEnable && PdoExtension->Started)
    {
        if (KeGetCurrentIrql() <= APC_LEVEL)
        {
            Status = SdBusSetSdioFunctionEnabledAdmitted(PdoExtension, TRUE);
            if (!NT_SUCCESS(Status))
            {
                KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
                if (ClaimedInitialization &&
                    PdoExtension->InitializedInterfaceContext == InterfaceContext)
                {
                    PdoExtension->InitializedInterfaceContext = NULL;
                }
                KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);
                InterlockedExchange(&PdoExtension->ManageIoEnable,
                                    PreviousManageIoEnable);
                InterlockedExchange(&PdoExtension->SdioEnablePending, 0);
                return Status;
            }
        }
        else
        {
            InterlockedExchange(&PdoExtension->SdioEnableStatus,
                                STATUS_PENDING);
            InterlockedExchange(&PdoExtension->SdioEnablePending, 1);
            if (FdoExtension->WorkerStarted)
            {
                KeSetEvent(&FdoExtension->RequestArrived,
                           IO_NO_INCREMENT,
                           FALSE);
            }
        }
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    InterfaceContext->CallbackRoutine = Params->CallbackRoutine;
    InterfaceContext->CallbackContext = Params->CallbackRoutineContext;
    InterfaceContext->DeviceGeneratesInterrupts = Params->DeviceGeneratesInterrupts;
    InterfaceContext->CallbackAtDpcLevel = Params->CallbackAtDpcLevel;
    InterfaceContext->SdioFlags = Params->SdioFlags;
    InterfaceContext->Initialized = TRUE;

    if (Params->DeviceGeneratesInterrupts)
    {
        PdoExtension->CallbackRoutine = Params->CallbackRoutine;
        PdoExtension->CallbackContext = Params->CallbackRoutineContext;
        PdoExtension->CallbackInterfaceContext = InterfaceContext;
        PdoExtension->CallbackAtDpcLevel = Params->CallbackAtDpcLevel;
        PdoExtension->CardInterruptForwardEnabled = TRUE;
    }
    else
    {
        PdoExtension->CallbackRoutine = NULL;
        PdoExtension->CallbackContext = NULL;
        PdoExtension->CallbackInterfaceContext = NULL;
        PdoExtension->CallbackAtDpcLevel = FALSE;
        PdoExtension->CardInterruptForwardEnabled = FALSE;
        if (InterlockedExchange(&InterfaceContext->InterruptOutstanding, 0) != 0)
        {
            InterlockedDecrement(&FdoExtension->PendingSdioAcks);
        }
    }
    SdBusUpdateCardInterruptLocked(FdoExtension);
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    ObReferenceObject(Params->TargetObject);
    KeAcquireSpinLock(&InterfaceContext->TargetLock, &OldIrql);
    PreviousTarget = InterfaceContext->TargetObject;
    InterfaceContext->TargetObject = Params->TargetObject;
    KeReleaseSpinLock(&InterfaceContext->TargetLock, OldIrql);
    if (PreviousTarget != NULL)
    {
        ObDereferenceObject(PreviousTarget);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Re-enable the SDIO card interrupt after the function driver finishes processing.
 *
 * Called by the function driver to acknowledge an SDIO card interrupt. Sets
 * the card interrupt bit in the SDHCI interrupt signal enable register so
 * the ISR can deliver subsequent SDIO interrupts.
 *
 * @param[in] Context  Pointer to the SDBUS_INTERFACE_CONTEXT for this instance.
 */
NTSTATUS
NTAPI
SdBusAcknowledgeInterruptImpl(
    _In_ PVOID Context)
{
    PSDBUS_INTERFACE_CONTEXT InterfaceContext;
    PPDO_EXTENSION PdoExtension;
    PFDO_EXTENSION FdoExtension;
    KIRQL OldIrql;

    InterfaceContext = (PSDBUS_INTERFACE_CONTEXT)Context;
    if (InterfaceContext == NULL || InterfaceContext->PdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PdoExtension = InterfaceContext->PdoExtension;
    FdoExtension = PdoExtension->FdoExtension;

    if (FdoExtension == NULL || FdoExtension->RegisterBase == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    if (PdoExtension->CallbackInterfaceContext != InterfaceContext ||
        !InterfaceContext->DeviceGeneratesInterrupts)
    {
        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (InterlockedExchange(&InterfaceContext->InterruptOutstanding, 0) != 0)
    {
        InterlockedDecrement(&FdoExtension->PendingSdioAcks);
    }
    SdBusUpdateCardInterruptLocked(FdoExtension);
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    return STATUS_SUCCESS;
}

/**
 * @brief Open the SD bus interface and populate the SDBUS_INTERFACE_STANDARD structure.
 *
 * Allocates a per-open interface context, fills in the interface function
 * pointers (reference, dereference, initialize, acknowledge interrupt),
 * and takes an initial reference on the PDO.
 *
 * @param[in]  PdoExtension  Pointer to the child PDO extension.
 * @param[out] Interface     Pointer to the SDBUS_INTERFACE_STANDARD structure to fill in.
 * @param[in]  Size          Size of the caller's interface buffer in bytes.
 * @param[in]  Version       Interface version requested by the caller.
 *
 * @return STATUS_SUCCESS, STATUS_BUFFER_TOO_SMALL, STATUS_NOT_SUPPORTED,
 *         or STATUS_INSUFFICIENT_RESOURCES.
 */
NTSTATUS
SdBusOpenInterfaceImpl(
    _In_ PPDO_EXTENSION PdoExtension,
    _Out_ PSDBUS_INTERFACE_STANDARD Interface,
    _In_ USHORT Size,
    _In_ USHORT Version)
{
    PSDBUS_INTERFACE_CONTEXT InterfaceContext;
    PFDO_EXTENSION FdoExtension;

    if (PdoExtension == NULL || Interface == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Size < sizeof(SDBUS_INTERFACE_STANDARD))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Version > SDBUS_INTERFACE_VERSION)
    {
        return STATUS_NOT_SUPPORTED;
    }

    FdoExtension = PdoExtension->FdoExtension;
    if (FdoExtension == NULL || PdoExtension->Common.Self == NULL ||
        FdoExtension->Common.Self == NULL || !PdoExtension->Present ||
        PdoExtension->Common.DeviceState == SdBusDeviceStateRemoved ||
        FdoExtension->Common.DeviceState == SdBusDeviceStateRemoved)
    {
        return STATUS_DEVICE_REMOVED;
    }

    /* Allocate a per-open interface context */
    InterfaceContext = (PSDBUS_INTERFACE_CONTEXT)ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(SDBUS_INTERFACE_CONTEXT),
        TAG_SDBUS);

    if (InterfaceContext == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(InterfaceContext, sizeof(SDBUS_INTERFACE_CONTEXT));
    KeInitializeSpinLock(&InterfaceContext->TargetLock);
    InterfaceContext->TargetObject = PdoExtension->Common.Self;
    InterfaceContext->PdoExtension = PdoExtension;
    InterfaceContext->PdoDeviceObject = PdoExtension->Common.Self;
    InterfaceContext->FdoDeviceObject = FdoExtension->Common.Self;
    InterfaceContext->ReferenceCount = 1;

    ObReferenceObject(InterfaceContext->TargetObject);
    ObReferenceObject(InterfaceContext->PdoDeviceObject);
    ObReferenceObject(InterfaceContext->FdoDeviceObject);

    /* Fill in the interface structure */
    Interface->Size = sizeof(SDBUS_INTERFACE_STANDARD);
    Interface->Version = SDBUS_INTERFACE_VERSION;
    Interface->Context = InterfaceContext;
    Interface->InterfaceReference = SdBusInterfaceReference;
    Interface->InterfaceDereference = SdBusInterfaceDereference;
    Interface->InitializeInterface = SdBusInitializeInterfaceImpl;
    Interface->AcknowledgeInterrupt = SdBusAcknowledgeInterruptImpl;

    return STATUS_SUCCESS;
}
