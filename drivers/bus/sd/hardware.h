/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware-specific SDHCI hooks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _SDBUS_HARDWARE_H_
#define _SDBUS_HARDWARE_H_

#include "sdbus.h"

typedef struct _SDBUS_HARDWARE_OPS
{
    VOID (*Release)(_In_ PFDO_EXTENSION FdoExtension);
    VOID (*InitializeController)(_In_ PFDO_EXTENSION FdoExtension);
    VOID (*SelectPins)(_In_ PFDO_EXTENSION FdoExtension,
                       _In_ BOOLEAN MmcTiming);
    BOOLEAN (*CanVoltageSwitch)(_In_ PFDO_EXTENSION FdoExtension);
    NTSTATUS (*VoltageSwitch)(_In_ PFDO_EXTENSION FdoExtension,
                              _In_ BOOLEAN To18);
    NTSTATUS (*QueryUhsModes)(_In_ PFDO_EXTENSION FdoExtension,
                              _Out_ PULONG Modes);
} SDBUS_HARDWARE_OPS, *PSDBUS_HARDWARE_OPS;

typedef struct _SDBUS_HARDWARE_EXTENSION
{
    const SDBUS_HARDWARE_OPS *Ops;
} SDBUS_HARDWARE_EXTENSION, *PSDBUS_HARDWARE_EXTENSION;

NTSTATUS
SdBusHardwareMapResources(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PHYSICAL_ADDRESS HostPhysicalAddress,
    _In_ PCM_PARTIAL_RESOURCE_LIST PartialList);

VOID
SdBusHardwareRelease(
    _In_ PFDO_EXTENSION FdoExtension);

VOID
SdBusHardwareInitializeController(
    _In_ PFDO_EXTENSION FdoExtension);

VOID
SdBusHardwareSelectPins(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN MmcTiming);

BOOLEAN
SdBusHardwareCanVoltageSwitch(
    _In_ PFDO_EXTENSION FdoExtension);

NTSTATUS
SdBusHardwareVoltageSwitch(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN To18);

NTSTATUS
SdBusHardwareQueryUhsModes(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_ PULONG Modes);

NTSTATUS
SdBusHardwareAttach(
    _In_ PFDO_EXTENSION FdoExtension);

#endif /* _SDBUS_HARDWARE_H_ */
