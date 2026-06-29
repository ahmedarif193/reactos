/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware-specific SDHCI hook dispatcher
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "hardware.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
SdBusHardwareMapResources(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PHYSICAL_ADDRESS HostPhysicalAddress,
    _In_ PCM_PARTIAL_RESOURCE_LIST PartialList)
{
    UNREFERENCED_PARAMETER(FdoExtension);
    UNREFERENCED_PARAMETER(HostPhysicalAddress);
    UNREFERENCED_PARAMETER(PartialList);

    return STATUS_SUCCESS;
}

VOID
SdBusHardwareRelease(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL)
    {
        return;
    }

    if (HardwareExtension->Ops != NULL &&
        HardwareExtension->Ops->Release != NULL)
    {
        HardwareExtension->Ops->Release(FdoExtension);
        return;
    }

    FdoExtension->HardwareExtension = NULL;
}

VOID
SdBusHardwareInitializeController(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL ||
        HardwareExtension->Ops == NULL ||
        HardwareExtension->Ops->InitializeController == NULL)
    {
        return;
    }

    HardwareExtension->Ops->InitializeController(FdoExtension);
}

VOID
SdBusHardwareSelectPins(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN MmcTiming)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL ||
        HardwareExtension->Ops == NULL ||
        HardwareExtension->Ops->SelectPins == NULL)
    {
        return;
    }

    HardwareExtension->Ops->SelectPins(FdoExtension, MmcTiming);
}

BOOLEAN
SdBusHardwareCanVoltageSwitch(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PSDBUS_HARDWARE_EXTENSION HardwareExtension;

    HardwareExtension = FdoExtension->HardwareExtension;
    if (HardwareExtension == NULL ||
        HardwareExtension->Ops == NULL ||
        HardwareExtension->Ops->CanVoltageSwitch == NULL)
    {
        return FALSE;
    }

    return HardwareExtension->Ops->CanVoltageSwitch(FdoExtension);
}
