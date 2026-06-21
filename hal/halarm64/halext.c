/*
 * PROJECT:     ReactOS HAL — ARM64 platform-extension manager
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Detects the SoC platform and holds the services registered
 *              by the matching platform extension (see halext.h for the
 *              contract).  This file is the only place in the generic HAL
 *              that knows which platforms exist; everything else consumes
 *              the registered interfaces blindly.
 *
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include "halext.h"
#include "gic_internal.h"

/* Platform probes — the one allowed list of platform knowledge. */
#include "bcm2712_pci.h"
#include "bcm2711_pci.h"
#include "bcm2711_vc.h"
#include "bcm2837.h"

#define NDEBUG
#include <debug.h>

/* ================================================================== */
/*  Platform table                                                    */
/* ================================================================== */

typedef struct _HAL_ARM64_PLATFORM
{
    PCSTR Name;
    BOOLEAN (*Probe)(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);
} HAL_ARM64_PLATFORM;

static const HAL_ARM64_PLATFORM HalpArm64Platforms[] =
{
    { "BCM2712 (Raspberry Pi 5)", Bcm2712PciProbe },
    { "BCM2711 (Raspberry Pi 4)", Bcm2711PciProbe },
    { "BCM2837 (Raspberry Pi 3)", Bcm2837InterruptProbe },
};

static BOOLEAN HalpArm64PlatformMatched = FALSE;

/* ================================================================== */
/*  Registered services                                               */
/* ================================================================== */

const HAL_ARM64_INTERRUPT_CONTROLLER *HalpArm64InterruptController = NULL;
const HAL_ARM64_PCI_CONFIG_BACKEND *HalpArm64PciConfigBackend = NULL;

static const HAL_ARM64_PLATFORM_DEVICE *HalpArm64PlatformDevices[HAL_ARM64_MAX_PLATFORM_DEVICES];
static ULONG HalpArm64PlatformDeviceCount = 0;

/* ================================================================== */
/*  Registration                                                      */
/* ================================================================== */

NTSTATUS
HalpArm64RegisterInterruptController(
    _In_ const HAL_ARM64_INTERRUPT_CONTROLLER *Controller)
{
    if (!Controller)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64InterruptController)
        return STATUS_TOO_LATE;

    HalpArm64InterruptController = Controller;

    /*
     * The GIC layer must stop trusting its discovery defaults: on GIC-less
     * platforms the firmware MADT often carries compatibility decoys and
     * the GICD/GICC globals default to QEMU-virt physical addresses.
     */
    HalpGicAdoptExternalInterruptController();

    DPRINT1("[arm64][HALEXT] Interrupt controller: %s\n", Controller->Name);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpArm64RegisterPciConfigBackend(
    _In_ const HAL_ARM64_PCI_CONFIG_BACKEND *Backend)
{
    if (!Backend)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64PciConfigBackend)
        return STATUS_TOO_LATE;

    HalpArm64PciConfigBackend = Backend;

    DPRINT1("[arm64][HALEXT] PCI config-space backend: %s\n", Backend->Name);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpArm64RegisterPlatformDevice(
    _In_ const HAL_ARM64_PLATFORM_DEVICE *Device)
{
    if (!Device || !Device->DeviceId)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64PlatformDeviceCount >= HAL_ARM64_MAX_PLATFORM_DEVICES)
        return STATUS_INSUFFICIENT_RESOURCES;

    HalpArm64PlatformDevices[HalpArm64PlatformDeviceCount++] = Device;

    DPRINT1("[arm64][HALEXT] Platform device: %s (%S)\n",
            Device->Name ? Device->Name : "?",
            Device->DeviceId);
    return STATUS_SUCCESS;
}

/* ================================================================== */
/*  Generic HAL consumption                                           */
/* ================================================================== */

VOID
HalpArm64ProbePlatforms(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG Index;

    if (HalpArm64PlatformMatched)
        return;

    for (Index = 0; Index < RTL_NUMBER_OF(HalpArm64Platforms); Index++)
    {
        if (HalpArm64Platforms[Index].Probe(LoaderBlock))
        {
            HalpArm64PlatformMatched = TRUE;
            DPRINT1("[arm64][HALEXT] Platform: %s\n",
                    HalpArm64Platforms[Index].Name);
            return;
        }
    }
}

VOID
HalpArm64Phase1PlatformInit(VOID)
{
    HalpBcm2711VcInit();
}

ULONG
HalpArm64GetPlatformDeviceCount(VOID)
{
    return HalpArm64PlatformDeviceCount;
}

const HAL_ARM64_PLATFORM_DEVICE *
HalpArm64GetPlatformDevice(
    _In_ ULONG Index)
{
    if (Index >= HalpArm64PlatformDeviceCount)
        return NULL;

    return HalpArm64PlatformDevices[Index];
}
