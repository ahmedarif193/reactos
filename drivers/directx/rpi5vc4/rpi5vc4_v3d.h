/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2712 V3D 7.1 (VideoCore VII) 3D engine plumbing —
 *              SMS power sequencing, hub/core identification, GPU MMU
 *              page table, and control-list (CLE) job submission.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Register layout derived from the Linux kernel v3d DRM driver
 * (drivers/gpu/drm/v3d/v3d_regs.h, GPL-2.0+, Copyright (C) 2017-2018
 * Broadcom) at rpi-6.6.y, and the BCM2712 device tree shipped in
 * media/boot/rpi/bcm2712-rpi-5-b.dtb:
 *
 *   v3d@2000000 { compatible = "brcm,2712-v3d";
 *                 reg = <0x10 0x2000000 0x0 0x4000>,   hub
 *                       <0x10 0x2008000 0x0 0x6000>,   core0
 *                       <0x10 0x2030800 0x0 0x700>; }  sms
 */

#ifndef _RPI5VC4_V3D_H_
#define _RPI5VC4_V3D_H_

#include "rpi5vc4.h"

/* Register blocks (BCM2712, from the v3d device-tree node). */
#define RPI5_V3D_HUB_PHYS               0x1002000000ULL
#define RPI5_V3D_HUB_LENGTH             0x4000
#define RPI5_V3D_CORE0_PHYS             0x1002008000ULL
#define RPI5_V3D_CORE0_LENGTH           0x6000
#define RPI5_V3D_SMS_PHYS               0x1002030800ULL
#define RPI5_V3D_SMS_LENGTH             0x700

/* ---- Hub registers ---------------------------------------------------- */
#define V3D_HUB_AXICFG                  0x0000
#define V3D_HUB_UIFCFG                  0x0004
#define V3D_HUB_IDENT0                  0x0008
#define V3D_HUB_IDENT1                  0x000c
#define V3D_HUB_IDENT1_NCORES_SHIFT     8
#define V3D_HUB_IDENT1_NCORES_MASK      (0xFu << 8)
#define V3D_HUB_IDENT1_REV_SHIFT        4
#define V3D_HUB_IDENT1_REV_MASK         (0xFu << 4)
#define V3D_HUB_IDENT1_TVER_SHIFT       0
#define V3D_HUB_IDENT1_TVER_MASK        (0xFu << 0)
#define V3D_HUB_IDENT2                  0x0010
#define V3D_HUB_IDENT2_WITH_MMU         (1u << 8)
#define V3D_HUB_IDENT3                  0x0014
#define V3D_HUB_INT_STS                 0x0050
#define V3D_HUB_INT_SET                 0x0054
#define V3D_HUB_INT_CLR                 0x0058
#define V3D_HUB_INT_MSK_STS             0x005c
#define V3D_HUB_INT_MSK_SET             0x0060
#define V3D_HUB_INT_MSK_CLR             0x0064

/* ---- Hub MMU cache + MMU ----------------------------------------------- */
#define V3D_MMUC_CONTROL                0x1000
#define V3D_MMUC_CONTROL_CLEAR          (1u << 3)
#define V3D_MMUC_CONTROL_FLUSHING       (1u << 2)
#define V3D_MMUC_CONTROL_FLUSH          (1u << 1)
#define V3D_MMUC_CONTROL_ENABLE         (1u << 0)

#define V3D_MMU_CTL                     0x1200
#define V3D_MMU_CTL_CAP_EXCEEDED_ABORT  (1u << 26)
#define V3D_MMU_CTL_CAP_EXCEEDED_INT    (1u << 25)
#define V3D_MMU_CTL_PT_INVALID_INT      (1u << 18)
#define V3D_MMU_CTL_WRITE_VIOLATION_INT (1u << 10)
#define V3D_MMU_CTL_PT_INVALID          (1u << 20)
#define V3D_MMU_CTL_PT_INVALID_ABORT    (1u << 19)
#define V3D_MMU_CTL_PT_INVALID_ENABLE   (1u << 16)
#define V3D_MMU_CTL_WRITE_VIOLATION_ABORT (1u << 11)
#define V3D_MMU_CTL_TLB_CLEARING        (1u << 7)
#define V3D_MMU_CTL_TLB_CLEAR           (1u << 2)
#define V3D_MMU_CTL_ENABLE              (1u << 0)

#define V3D_MMU_PT_PA_BASE              0x1204
#define V3D_MMU_VIO_ADDR                0x1234
#define V3D_MMU_CTL_TLB_STATS_ENABLE    (1u << 1)
#define V3D_MMU_ILLEGAL_ADDR            0x1230
#define V3D_MMU_ILLEGAL_ADDR_ENABLE     (1u << 31)

#define V3D_MMU_PAGE_SHIFT              12

/* GPU MMU page-table entries: one 32-bit PTE per 4 KB page. */
#define V3D_PTE_SUPERPAGE               (1u << 31)
#define V3D_PTE_WRITEABLE               (1u << 29)
#define V3D_PTE_VALID                   (1u << 28)

/* ---- Core (CTL/CLE) registers ------------------------------------------ */
#define V3D_CTL_IDENT0                  0x0000
#define V3D_CTL_IDENT1                  0x0004
#define V3D_CTL_IDENT2                  0x0008
#define V3D_CTL_MISCCFG                 0x0018
#define V3D_CTL_SLCACTL                 0x0024
#define V3D_SLCACTL_INVALIDATE_ALL      0x0F0F0F0Fu /* TVC+TDC+UC+IC, all slices */
#define V3D_CTL_L2TCACTL                0x0030
#define V3D_L2TCACTL_TMUWCF             (1u << 8)
#define V3D_L2TCACTL_L2TFLS             (1u << 0)
#define V3D_L2TCACTL_FLM_FLUSH          (0u << 1)   /* invalidate lines        */
#define V3D_L2TCACTL_FLM_CLEAN          (2u << 1)   /* write back dirty lines  */
#define V3D_CTL_L2TFLSTA                0x0034
#define V3D_CTL_L2TFLEND                0x0038
#define V3D_CTL_INT_STS                 0x0050
#define V3D_CTL_INT_SET                 0x0054
#define V3D_CTL_INT_CLR                 0x0058
#define V3D_CTL_INT_MSK_STS             0x005c
#define V3D_CTL_INT_MSK_SET             0x0060
#define V3D_CTL_INT_MSK_CLR             0x0064
#define V3D_INT_OUTOMEM                 (1u << 2)
#define V3D_INT_FLDONE                  (1u << 1)   /* binning list done  */
#define V3D_INT_FRDONE                  (1u << 0)   /* render list done   */

#define V3D_CLE_CT0CS                   0x0100
#define V3D_CLE_CT1CS                   0x0104
#define V3D_CLE_CT0EA                   0x0108
#define V3D_CLE_CT1EA                   0x010c
#define V3D_CLE_CT0CA                   0x0110
#define V3D_CLE_CT1CA                   0x0114
#define V3D_PTB_BPCA                    0x0300
#define V3D_PTB_BPCS                    0x0304
#define V3D_PTB_BPOA                    0x0308
#define V3D_PTB_BPOS                    0x030c
#define V3D_CLE_PCS                     0x0130
#define V3D_CLE_BFC                     0x0134
#define V3D_CLE_RFC                     0x0138
#define V3D_CLE_CT00RA0                 0x0118
#define V3D_CLE_CT01RA0                 0x011c
#define V3D_ERR_STAT                    0x0F20
#define V3D_CLE_CT1CFG                  0x0144
#define V3D_CLE_CT1TILECT               0x0148
#define V3D_CLE_CT1TSKIP                0x014c
#define V3D_CLE_CT1PTCT                 0x0150
#define V3D_CLE_CT0SYNC                 0x0154
#define V3D_CLE_CT1SYNC                 0x0158
#define V3D_CLE_CT0QTS                  0x015c
#define V3D_CLE_CT0QTS_ENABLE           (1u << 1)
#define V3D_CLE_CT0QBA                  0x0160
#define V3D_CLE_CT1QBA                  0x0164
#define V3D_CLE_CT0QEA                  0x0168
#define V3D_CLE_CT1QEA                  0x016c
#define V3D_CLE_CT0QMA                  0x0170  /* binner overflow memory base */
#define V3D_CLE_CT0QMS                  0x0174  /* binner overflow memory size */

/* ---- CSD (compute shader dispatch) — core block, v7 register set ------- */
#define V3D_V7_CSD_QUEUED_CFG0          0x0930  /* CFG0 write kicks the job */
#define V3D_V7_CSD_QUEUED_CFG(n)        (V3D_V7_CSD_QUEUED_CFG0 + 4 * (n))
#define V3D_V7_INT_CSDDONE              (1u << 6)

/* ---- TFU (texture formatting unit) — hub block, v7 register set -------- */
#define V3D_V7_TFU_CS                   0x0700
#define V3D_TFU_CS_BUSY                 (1u << 0)
#define V3D_V7_TFU_SU                   0x0704
#define V3D_V7_TFU_ICFG                 0x0708
#define V3D_TFU_ICFG_IOC                (1u << 0)
#define V3D_V7_TFU_IIA                  0x070c
#define V3D_V7_TFU_ICA                  0x0710
#define V3D_V7_TFU_IIS                  0x0714
#define V3D_V7_TFU_IUA                  0x0718
#define V3D_V7_TFU_IOC_REG              0x071c
#define V3D_V7_TFU_IOA                  0x0720
#define V3D_V7_TFU_IOS                  0x0724
#define V3D_V7_TFU_COEF0                0x0728
#define V3D_V7_TFU_COEF1                0x072c
#define V3D_V7_TFU_COEF2                0x0730
#define V3D_V7_TFU_COEF3                0x0734

#define V3D_HUB_INT_TFUC                (1u << 1)   /* conversion complete */
#define V3D_HUB_INT_TFUF                (1u << 0)   /* fifo free           */

/* ---- SMS (BCM2712 power state machine) --------------------------------- */
#define V3D_SMS_REE_CS                  0x0000
#define V3D_SMS_TEE_CS                  0x0400
#define V3D_SMS_POWER_OFF               (1u << 30)
#define V3D_SMS_CLEAR_POWER_OFF         (1u << 29)
#define V3D_SMS_STATE_MASK              0xFu
#define V3D_SMS_STATE_IDLE              0x0
#define V3D_SMS_STATE_ISOLATING_FOR_RESET 0xa
#define V3D_SMS_STATE_RESETTING         0xb
#define V3D_SMS_STATE_POWER_OFF         0xd

/*
 * GPU virtual address window: the VRAM slab is mapped 1:1 (offset-wise)
 * starting at this GPU VA.  Control lists reference allocations at
 * RPI5VC4_V3D_SLAB_GPUVA + <segment offset of the allocation>.
 */
#define RPI5VC4_V3D_SLAB_GPUVA          0x01000000u

/* Full 4 GB GPU VA space: 1M PTEs x 4 bytes. */
#define RPI5VC4_V3D_PT_SIZE             (4 * 1024 * 1024)

/* Binner overflow pool, mapped right after the slab in GPU VA space. */
#define RPI5VC4_V3D_OVERFLOW_SIZE       (8 * 1024 * 1024)

BOOLEAN
Rpi5V3dInitialize(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

VOID
Rpi5V3dTeardown(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

/*
 * Phased job execution mirroring the Linux v3d pipeline: a job's binning
 * control list runs on CLE thread 0, its render list runs on thread 1 only
 * after binning completed, and the NEXT job's binning may overlap the
 * current job's rendering.  All callable at DISPATCH_LEVEL.
 */
BOOLEAN
Rpi5V3dResetCore(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

BOOLEAN
Rpi5V3dSubmitBin(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG BclStart,
    _In_ ULONG BclEnd,
    _In_ ULONG Qma,
    _In_ ULONG Qms,
    _In_ ULONG Qts);

BOOLEAN
Rpi5V3dSubmitRender(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG RclStart,
    _In_ ULONG RclEnd,
    _In_ BOOLEAN InvalidateCaches);

/* Consume (read + W1C) the latched bin/render completion bits. */
VOID
Rpi5V3dPollDone(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PBOOLEAN BinDone,
    _Out_ PBOOLEAN RenderDone);

/*
 * V3D core interrupt: GIC SPI 250 per the RPi5 DTB (v3d node
 * interrupts = <GIC_SPI 0xfa level-high>), INTID = 32 + 250.
 */
#define RPI5_V3D_CORE_SPI               250
#define RPI5_V3D_CORE_INTID             (32 + RPI5_V3D_CORE_SPI)
/* bcm2712.dtsi v3d lists TWO lines: <GIC_SPI 250>, <GIC_SPI 249> (core+hub
 * pair, order unproven on silicon) — connect both, INT_STS decides. */
#define RPI5_V3D_SPI2                   249
#define RPI5_V3D_INTID2                 (32 + RPI5_V3D_SPI2)

/* Kick one UMD-encoded TFU conversion job (register image). */
BOOLEAN
Rpi5V3dSubmitTfu(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST ULONG *TfuRegs);      /* [Icfg Iia Ica Iis Iua Ioc Ioa Ios C0..C3] */

/* Kick one UMD-encoded compute dispatch (CFG0..CFG7 register image). */
BOOLEAN
Rpi5V3dSubmitCsd(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST ULONG *CsdCfg);

/* Consume (read + W1C) the core CSD-complete latch. */
BOOLEAN
Rpi5V3dCsdDone(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

/* Consume (read + W1C) the hub TFU-complete latch. */
BOOLEAN
Rpi5V3dTfuDone(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

/* Best-effort SPI connect; FALSE leaves the poll timer as sole driver. */
BOOLEAN
Rpi5V3dConnectInterrupt(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

VOID
Rpi5V3dDisconnectInterrupt(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

#endif /* _RPI5VC4_V3D_H_ */
