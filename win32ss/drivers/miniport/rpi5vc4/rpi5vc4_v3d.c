/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Read-only BCM2712 V3D 7.1 discovery
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5vc4_v3d.h"

/* V3D identity register offsets from the upstream Broadcom V3D DRM driver. */
#define V3D_HUB_IDENT0             0x0008
#define V3D_HUB_IDENT1             0x000C
#define V3D_HUB_IDENT2             0x0010
#define V3D_HUB_IDENT3             0x0014
#define V3D_HUB_IDENT1_NCORES_MASK 0x00000F00
#define V3D_HUB_IDENT1_NCORES_SHIFT 8
#define V3D_HUB_IDENT1_REV_MASK    0x000000F0
#define V3D_HUB_IDENT1_REV_SHIFT   4
#define V3D_HUB_IDENT1_TVER_MASK   0x0000000F
#define V3D_HUB_IDENT1_TVER_SHIFT  0
#define V3D_MMU_DEBUG_INFO         0x1238

#define V3D_CTL_IDENT0             0x0000
#define V3D_CTL_IDENT1             0x0004
#define V3D_CTL_IDENT2             0x0008

#define V3D_SMS_REE_CS             0x0000
#define V3D_SMS_TEE_CS             0x0400
#define V3D_SMS_STATE_MASK         0x0000000F
#define V3D_SMS_STATE_IDLE         0x00000000

static ULONG
Rpi5V3dRead(
    _In_ PVOID Base,
    _In_ ULONG Offset)
{
    return VideoPortReadRegisterUlong((PULONG)((PUCHAR)Base + Offset));
}

static PVOID
Rpi5V3dMapRange(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PVIDEO_ACCESS_RANGE Range,
    _In_ ULONG RequiredLength)
{
    if (Range->RangeStart.QuadPart == 0 ||
        Range->RangeLength < RequiredLength ||
        Range->RangeInIoSpace)
    {
        return NULL;
    }

    return VideoPortGetDeviceBase(DeviceExtension,
                                  Range->RangeStart,
                                  Range->RangeLength,
                                  FALSE);
}

VOID
Rpi5V3dProbe(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Ident1;

    /* VideoPort calls HwInitialize for each display-device open. */
    if (DeviceExtension->V3dFlags & RPI5VC4_V3D_FLAG_IDENT_VALID)
        return;

    DeviceExtension->V3dFlags = 0;
    DeviceExtension->V3dVersion = 0;
    DeviceExtension->V3dCoreCount = 0;
    VideoPortZeroMemory(DeviceExtension->V3dHubIdent,
                        sizeof(DeviceExtension->V3dHubIdent));
    VideoPortZeroMemory(DeviceExtension->V3dCoreIdent,
                        sizeof(DeviceExtension->V3dCoreIdent));
    DeviceExtension->V3dMmuDebugInfo = 0;

    if (DeviceExtension->V3dSmsBase == NULL)
    {
        DeviceExtension->V3dSmsBase =
            Rpi5V3dMapRange(DeviceExtension,
                            &DeviceExtension->V3dSmsRange,
                            V3D_SMS_TEE_CS + sizeof(ULONG));
    }
    if (DeviceExtension->V3dSmsBase == NULL)
    {
        DbgPrint("RPI5VC4: V3D SMS mapping failed\n");
        return;
    }

    DeviceExtension->V3dFlags |= RPI5VC4_V3D_FLAG_SMS_MAPPED;
    DeviceExtension->V3dSmsReeCs =
        Rpi5V3dRead(DeviceExtension->V3dSmsBase, V3D_SMS_REE_CS);
    DeviceExtension->V3dSmsTeeCs =
        Rpi5V3dRead(DeviceExtension->V3dSmsBase, V3D_SMS_TEE_CS);

    if ((DeviceExtension->V3dSmsTeeCs & V3D_SMS_STATE_MASK) !=
        V3D_SMS_STATE_IDLE)
    {
        DbgPrint("RPI5VC4: V3D is powered down (SMS REE=0x%08lx TEE=0x%08lx)\n",
                 DeviceExtension->V3dSmsReeCs,
                 DeviceExtension->V3dSmsTeeCs);
        return;
    }

    DeviceExtension->V3dFlags |= RPI5VC4_V3D_FLAG_POWERED;
    if (DeviceExtension->V3dHubBase == NULL)
    {
        DeviceExtension->V3dHubBase =
            Rpi5V3dMapRange(DeviceExtension,
                            &DeviceExtension->V3dHubRange,
                            V3D_MMU_DEBUG_INFO + sizeof(ULONG));
    }
    if (DeviceExtension->V3dCoreBase == NULL)
    {
        DeviceExtension->V3dCoreBase =
            Rpi5V3dMapRange(DeviceExtension,
                            &DeviceExtension->V3dCoreRange,
                            V3D_CTL_IDENT2 + sizeof(ULONG));
    }
    if (DeviceExtension->V3dHubBase == NULL ||
        DeviceExtension->V3dCoreBase == NULL)
    {
        DbgPrint("RPI5VC4: V3D identity-register mapping failed\n");
        return;
    }

    DeviceExtension->V3dFlags |= RPI5VC4_V3D_FLAG_REGS_MAPPED;
    DeviceExtension->V3dHubIdent[0] =
        Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_HUB_IDENT0);
    DeviceExtension->V3dHubIdent[1] =
        Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_HUB_IDENT1);
    DeviceExtension->V3dHubIdent[2] =
        Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_HUB_IDENT2);
    DeviceExtension->V3dHubIdent[3] =
        Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_HUB_IDENT3);
    DeviceExtension->V3dCoreIdent[0] =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_IDENT0);
    DeviceExtension->V3dCoreIdent[1] =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_IDENT1);
    DeviceExtension->V3dCoreIdent[2] =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_IDENT2);
    DeviceExtension->V3dMmuDebugInfo =
        Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_MMU_DEBUG_INFO);

    Ident1 = DeviceExtension->V3dHubIdent[1];
    DeviceExtension->V3dVersion =
        ((Ident1 & V3D_HUB_IDENT1_TVER_MASK) >>
         V3D_HUB_IDENT1_TVER_SHIFT) * 10 +
        ((Ident1 & V3D_HUB_IDENT1_REV_MASK) >>
         V3D_HUB_IDENT1_REV_SHIFT);
    DeviceExtension->V3dCoreCount =
        (Ident1 & V3D_HUB_IDENT1_NCORES_MASK) >>
        V3D_HUB_IDENT1_NCORES_SHIFT;

    if (Ident1 == 0 ||
        Ident1 == MAXULONG ||
        DeviceExtension->V3dVersion != 71 ||
        DeviceExtension->V3dCoreCount == 0 ||
        DeviceExtension->V3dCoreIdent[0] == 0 ||
        DeviceExtension->V3dCoreIdent[0] == MAXULONG)
    {
        DbgPrint("RPI5VC4: invalid V3D identity hub1=0x%08lx core0=0x%08lx version=%lu cores=%lu\n",
                 Ident1,
                 DeviceExtension->V3dCoreIdent[0],
                 DeviceExtension->V3dVersion,
                 DeviceExtension->V3dCoreCount);
        return;
    }

    DeviceExtension->V3dFlags |= RPI5VC4_V3D_FLAG_IDENT_VALID;
    DbgPrint("RPI5VC4: V3D %lu.%lu identified cores=%lu hub=%08lx/%08lx/%08lx/%08lx core=%08lx/%08lx/%08lx mmu=%08lx\n",
             DeviceExtension->V3dVersion / 10,
             DeviceExtension->V3dVersion % 10,
             DeviceExtension->V3dCoreCount,
             DeviceExtension->V3dHubIdent[0],
             DeviceExtension->V3dHubIdent[1],
             DeviceExtension->V3dHubIdent[2],
             DeviceExtension->V3dHubIdent[3],
             DeviceExtension->V3dCoreIdent[0],
             DeviceExtension->V3dCoreIdent[1],
             DeviceExtension->V3dCoreIdent[2],
             DeviceExtension->V3dMmuDebugInfo);
}

VP_STATUS
Rpi5V3dQuery(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PRPI5VC4_V3D_INFO Info)
{
    VideoPortZeroMemory(Info, sizeof(*Info));
    Info->Size = sizeof(*Info);
    Info->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Info->Flags = DeviceExtension->V3dFlags;
    Info->Version = DeviceExtension->V3dVersion;
    Info->CoreCount = DeviceExtension->V3dCoreCount;
    Info->SmsReeCs = DeviceExtension->V3dSmsReeCs;
    Info->SmsTeeCs = DeviceExtension->V3dSmsTeeCs;
    VideoPortMoveMemory(Info->HubIdent,
                        DeviceExtension->V3dHubIdent,
                        sizeof(Info->HubIdent));
    VideoPortMoveMemory(Info->CoreIdent,
                        DeviceExtension->V3dCoreIdent,
                        sizeof(Info->CoreIdent));
    Info->MmuDebugInfo = DeviceExtension->V3dMmuDebugInfo;
    return NO_ERROR;
}
