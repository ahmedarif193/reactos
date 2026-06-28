/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SDBUS_INTERFACE_STANDARD implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#define NDEBUG
#include <debug.h>

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
    PPDO_EXTENSION PdoExtension;

    InterfaceContext = (PSDBUS_INTERFACE_CONTEXT)Context;
    if (InterfaceContext == NULL || InterfaceContext->PdoExtension == NULL)
    {
        return;
    }

    PdoExtension = InterfaceContext->PdoExtension;
    InterlockedIncrement(&PdoExtension->InterfaceReferenceCount);
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
    LONG NewCount;

    InterfaceContext = (PSDBUS_INTERFACE_CONTEXT)Context;
    if (InterfaceContext == NULL || InterfaceContext->PdoExtension == NULL)
    {
        return;
    }

    PdoExtension = InterfaceContext->PdoExtension;
    NewCount = InterlockedDecrement(&PdoExtension->InterfaceReferenceCount);

    if (NewCount == 0)
    {
        /* Last reference released; free the interface context */
        ExFreePoolWithTag(InterfaceContext, TAG_SDBUS);
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

    InterfaceContext = (PSDBUS_INTERFACE_CONTEXT)Context;
    if (InterfaceContext == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Params = InterfaceParameters;
    if (Params == NULL || Params->Size < sizeof(SDBUS_INTERFACE_PARAMETERS))
    {
        return STATUS_INVALID_PARAMETER;
    }

    PdoExtension = InterfaceContext->PdoExtension;
    if (PdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    InterfaceContext->CallbackRoutine = Params->CallbackRoutine;
    InterfaceContext->CallbackContext = Params->CallbackRoutineContext;
    InterfaceContext->DeviceGeneratesInterrupts = Params->DeviceGeneratesInterrupts;

    PdoExtension->CallbackRoutine = Params->CallbackRoutine;
    PdoExtension->CallbackContext = Params->CallbackRoutineContext;
    PdoExtension->CallbackAtDpcLevel = Params->CallbackAtDpcLevel;

    if (Params->DeviceGeneratesInterrupts && PdoExtension->FdoExtension)
    {
        SdBusUpdateInterruptSignalEnable(PdoExtension->FdoExtension,
                                         SDHCI_INT_CARD_INTERRUPT,
                                         0);
    }
    else if (PdoExtension->FdoExtension)
    {
        SdBusUpdateInterruptSignalEnable(PdoExtension->FdoExtension,
                                         0,
                                         SDHCI_INT_CARD_INTERRUPT);
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

    SdBusUpdateInterruptSignalEnable(FdoExtension,
                                     SDHCI_INT_CARD_INTERRUPT,
                                     0);

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

    if (Size < sizeof(SDBUS_INTERFACE_STANDARD))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Version > SDBUS_INTERFACE_VERSION)
    {
        return STATUS_NOT_SUPPORTED;
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
    InterfaceContext->PdoExtension = PdoExtension;

    /* Fill in the interface structure */
    Interface->Size = sizeof(SDBUS_INTERFACE_STANDARD);
    Interface->Version = SDBUS_INTERFACE_VERSION;
    Interface->Context = InterfaceContext;
    Interface->InterfaceReference = SdBusInterfaceReference;
    Interface->InterfaceDereference = SdBusInterfaceDereference;
    Interface->InitializeInterface = SdBusInitializeInterfaceImpl;
    Interface->AcknowledgeInterrupt = SdBusAcknowledgeInterruptImpl;

    /* Take an initial reference */
    InterlockedIncrement(&PdoExtension->InterfaceReferenceCount);

    return STATUS_SUCCESS;
}
