/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Raspberry Pi 5 BCM2712 SDHCI hooks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _SDBUS_BCM2712_H_
#define _SDBUS_BCM2712_H_

#include "hardware.h"

NTSTATUS
SdBusBcm2712MapResources(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PHYSICAL_ADDRESS HostPhysicalAddress,
    _In_ PCM_PARTIAL_RESOURCE_LIST PartialList);

#endif /* _SDBUS_BCM2712_H_ */
