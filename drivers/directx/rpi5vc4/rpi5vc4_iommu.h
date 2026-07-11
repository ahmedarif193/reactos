/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2712 system IOMMU in front of the HVS — register layout
 *              transcribed from the Linux driver, a read-only state dump
 *              for first-boot verification, and the single helper every
 *              HVS DMA address flows through.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * DTB: hvs { iommus = <&iommu5> }, iommu@5200 "brcm,bcm2712-iommu" at
 * 0x10_00005200 (V3D has NO system IOMMU — only its private MMU).
 *
 * ARCHITECTURE (Linux drivers/iommu/bcm2712-iommu.c, rpi-6.6.y):
 * the translated IOVA aperture is 40GB..42GB; ADDRESSES BELOW THE
 * APERTURE PASS STRAIGHT THROUGH TO SDRAM.  Every address this driver
 * programs (slab + firmware FB) is < 8GB physical, i.e. identity by
 * design.  Residual risk: firmware-set MMMU_ADDR_CAP (256MB units) or
 * BYPASS_START/END windows could still fault high-DRAM accesses — the
 * pre-takeover dump verifies those on first silicon boot.
 */

#ifndef _RPI5VC4_IOMMU_H_
#define _RPI5VC4_IOMMU_H_

#include "rpi5vc4.h"

#define RPI5_HVS_IOMMU_PHYS             0x1000005200ULL
#define RPI5_HVS_IOMMU_LENGTH           0x80

/* Register offsets/bits verbatim from bcm2712-iommu.c. */
#define MMMU_CTRL_OFFSET                0x00
#define MMMU_CTRL_BYPASS                (1u << 8)
#define MMMU_CTRL_ENABLE                (1u << 0)
#define MMMU_PT_PA_BASE_OFFSET          0x04
#define MMMU_ADDR_CAP_OFFSET            0x14
#define MMMU_ADDR_CAP_ENABLE            (1u << 31)
#define MMMU_ADDR_CAP_SHIFT             28      /* 256 MB units */
#define MMMU_BYPASS_START_OFFSET        0x1C
#define MMMU_BYPASS_START_ENABLE        (1u << 31)
#define MMMU_BYPASS_START_INVERT        (1u << 30)
#define MMMU_BYPASS_END_OFFSET          0x20
#define MMMU_BYPASS_END_ENABLE          (1u << 31)
#define MMMU_MISC_OFFSET                0x24
#define MMMU_MISC_SINGLE_TABLE          (1u << 31)
#define MMMU_ILLEGAL_ADR_OFFSET         0x30
#define MMMU_DEBUG_INFO_OFFSET          0x38

/* Translated aperture (addresses below pass through untranslated). */
#define RPI5_HVS_IOMMU_APERTURE_BASE    (40ULL << 30)

/*
 * Every DMA address written into an HVS display list flows through this
 * helper.  Identity today (all our buffers sit below the translated
 * aperture); if the first-boot dump shows firmware caps/windows that
 * contradict the pass-through assumption, the fix lands HERE only.
 */
static __inline ULONGLONG
Rpi5HvsDmaAddress(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG PhysicalAddress)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    return PhysicalAddress;
}

/* Read-only pre-takeover dump; call once from StartDevice. */
static __inline VOID
Rpi5HvsIommuDumpState(VOID)
{
    PHYSICAL_ADDRESS Phys;
    PUCHAR Base;
    ULONG Ctrl, PtBase, AddrCap, BypStart, BypEnd, Misc, Illegal, Dbg;

    Phys.QuadPart = RPI5_HVS_IOMMU_PHYS;
    Base = MmMapIoSpace(Phys, RPI5_HVS_IOMMU_LENGTH, MmNonCached);
    if (Base == NULL)
    {
        DbgPrint("RPI5VC4: HVS IOMMU map failed — cannot verify state\n");
        return;
    }

    Ctrl     = READ_REGISTER_ULONG((PULONG)(Base + MMMU_CTRL_OFFSET));
    PtBase   = READ_REGISTER_ULONG((PULONG)(Base + MMMU_PT_PA_BASE_OFFSET));
    AddrCap  = READ_REGISTER_ULONG((PULONG)(Base + MMMU_ADDR_CAP_OFFSET));
    BypStart = READ_REGISTER_ULONG((PULONG)(Base + MMMU_BYPASS_START_OFFSET));
    BypEnd   = READ_REGISTER_ULONG((PULONG)(Base + MMMU_BYPASS_END_OFFSET));
    Misc     = READ_REGISTER_ULONG((PULONG)(Base + MMMU_MISC_OFFSET));
    Illegal  = READ_REGISTER_ULONG((PULONG)(Base + MMMU_ILLEGAL_ADR_OFFSET));
    Dbg      = READ_REGISTER_ULONG((PULONG)(Base + MMMU_DEBUG_INFO_OFFSET));

    DbgPrint("RPI5VC4: HVS IOMMU CTRL=%08lx (enable=%lu bypass=%lu) "
            "PT=%08lx MISC=%08lx DBG=%08lx\n",
            Ctrl, Ctrl & MMMU_CTRL_ENABLE, (Ctrl >> 8) & 1,
            PtBase, Misc, Dbg);
    DbgPrint("RPI5VC4: HVS IOMMU ADDR_CAP=%08lx (%s, cap=%lu MB) "
            "BYPASS=[%08lx..%08lx] ILLEGAL=%08lx\n",
            AddrCap,
            (AddrCap & MMMU_ADDR_CAP_ENABLE) ? "ENABLED" : "disabled",
            (AddrCap & ~MMMU_ADDR_CAP_ENABLE) << (MMMU_ADDR_CAP_SHIFT - 20),
            BypStart, BypEnd, Illegal);

    if ((AddrCap & MMMU_ADDR_CAP_ENABLE) != 0)
    {
        DbgPrint("RPI5VC4: WARNING — firmware address cap active; slab "
                "scanout above the cap will fault (see roadmap 1.12c)\n");
    }

    MmUnmapIoSpace(Base, RPI5_HVS_IOMMU_LENGTH);
}

#endif /* _RPI5VC4_IOMMU_H_ */
