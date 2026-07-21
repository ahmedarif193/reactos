/*
 * PROJECT:     ReactOS HAL — ARM64 BCM2837 (Raspberry Pi 3) platform support
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Header for the BCM2836/BCM2837 interrupt controller backend.
 *
 *              The BCM2837 (Raspberry Pi 3 family) has no ARM GIC at all.
 *              Interrupts are delivered through two cascaded blocks:
 *
 *                1. The per-core "ARM-local" block (BCM2836 QA7) which owns
 *                   the generic-timer lines, four software mailboxes per
 *                   core, and the routing of the VideoCore interrupt output.
 *
 *                2. The legacy BCM2835 "ARMCTRL" interrupt controller which
 *                   collects the 64 VideoCore/peripheral IRQs plus 8 ARM
 *                   "basic" IRQs and forwards them to one selected core.
 *
 *              Register layouts are a clean-room implementation derived from
 *              the public Broadcom datasheets ("BCM2835 ARM Peripherals",
 *              section 7, and "BCM2836 ARM-local peripherals / QA7") and
 *              from observation of the ACPI tables shipped by the Raspberry
 *              Pi UEFI firmware FADT (OEM "BC2836", table "RPI3").
 *
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/* ------------------------------------------------------------------ */
/*  Physical bases (BCM2837: SoC peripherals at 0x3F000000)           */
/* ------------------------------------------------------------------ */
#define BCM2837_LOCAL_BASE          0x40000000ULL   /* ARM-local (QA7)    */
#define BCM2837_LOCAL_LENGTH        0x100
#define BCM2837_ARMCTRL_BASE        0x3F00B200ULL   /* ARMCTRL IC         */
#define BCM2837_ARMCTRL_LENGTH      0x28

/* ------------------------------------------------------------------ */
/*  ARM-local (QA7) register offsets                                  */
/* ------------------------------------------------------------------ */
#define BCM2837_LOCAL_GPU_INT_ROUTING       0x0C  /* [1:0] IRQ core      */
#define BCM2837_LOCAL_PMU_INT_ROUTING_SET   0x10
#define BCM2837_LOCAL_PMU_INT_ROUTING_CLR   0x14
#define BCM2837_LOCAL_INT_ROUTING           0x24  /* local timer routing */
#define BCM2837_LOCAL_AXI_IRQ_CONTROL       0x30
#define BCM2837_LOCAL_TIMER_CTRL_STATUS     0x34
#define BCM2837_LOCAL_TIMER_WRITE_FLAGS     0x38  /* bit31 = IRQ clear   */
#define BCM2837_LOCAL_TIMER_INT_CONTROL(n)  (0x40 + 4 * (n))
#define BCM2837_LOCAL_MAILBOX_INT_CONTROL(n) (0x50 + 4 * (n))
#define BCM2837_LOCAL_IRQ_SOURCE(n)         (0x60 + 4 * (n))
#define BCM2837_LOCAL_MAILBOX_SET(n, m)     (0x80 + 0x10 * (n) + 4 * (m))
#define BCM2837_LOCAL_MAILBOX_RDCLR(n, m)   (0xC0 + 0x10 * (n) + 4 * (m))

/* IRQ_SOURCE / TIMER_INT_CONTROL bit positions (per core) */
#define BCM2837_SRC_CNTPS                   0   /* Secure phys timer    */
#define BCM2837_SRC_CNTPNS                  1   /* Non-secure phys timer*/
#define BCM2837_SRC_CNTHP                   2   /* Hyp timer            */
#define BCM2837_SRC_CNTV                    3   /* Virtual timer        */
#define BCM2837_SRC_MAILBOX0                4
#define BCM2837_SRC_GPU                     8   /* ARMCTRL output       */
#define BCM2837_SRC_PMU                     9
#define BCM2837_SRC_AXI                     10  /* core 0 only          */
#define BCM2837_SRC_LOCAL_TIMER             11

/* ------------------------------------------------------------------ */
/*  ARMCTRL (BCM2835 §7) register offsets, relative to 0x...B200      */
/* ------------------------------------------------------------------ */
#define BCM2837_ARMCTRL_PENDING_BASIC       0x00
#define BCM2837_ARMCTRL_PENDING_1           0x04  /* GPU IRQs  0-31     */
#define BCM2837_ARMCTRL_PENDING_2           0x08  /* GPU IRQs 32-63     */
#define BCM2837_ARMCTRL_FIQ_CONTROL         0x0C
#define BCM2837_ARMCTRL_ENABLE_1            0x10
#define BCM2837_ARMCTRL_ENABLE_2            0x14
#define BCM2837_ARMCTRL_ENABLE_BASIC        0x18
#define BCM2837_ARMCTRL_DISABLE_1           0x1C
#define BCM2837_ARMCTRL_DISABLE_2           0x20
#define BCM2837_ARMCTRL_DISABLE_BASIC       0x24

/*
 * PENDING_BASIC layout: bits 0-7 are the ARM "basic" IRQs; bits 10-20 are
 * direct flags for a fixed set of GPU IRQs (7, 9, 10, 18, 19, 53-57, 62).
 * Bits 8/9 summarize PENDING_1/PENDING_2 but deliberately EXCLUDE the
 * shortcut-flagged IRQs, so the bank pending registers must always be
 * read in full — the SDHCI (62) and USB (9) lines never set bits 8/9.
 */
#define BCM2837_BASIC_IRQ_MASK              0xFFu

/* ------------------------------------------------------------------ */
/*  INTID convention used by this backend                             */
/*                                                                    */
/*  The kernel and the common HAL paths address interrupts with GIC   */
/*  INTID semantics, and the firmware DSDT/GTDT use the same numbers  */
/*  as GSIVs, so the backend keeps a single combined namespace:       */
/*                                                                    */
/*    0..3     SGIs (IPI/APC/DPC)    -> per-core mailbox 0..3         */
/*    26/27/29/30 generic-timer PPIs -> local timer rows 2/3/0/1      */
/*    32..95   firmware GSIVs 0x20.. -> ARMCTRL GPU IRQ (INTID - 32)  */
/*    96..103  firmware GSIVs 0x60.. -> ARMCTRL basic IRQ (INTID-96)  */
/* ------------------------------------------------------------------ */
#define BCM2837_INTID_GPU_BASE              32
#define BCM2837_INTID_BASIC_BASE            96
#define BCM2837_INTID_BASIC_LIMIT           104
#define BCM2837_GPU_IRQ_COUNT               64
#define BCM2837_BASIC_IRQ_COUNT             8
#define BCM2837_CORE_COUNT                  4
#define BCM2837_SGI_COUNT                   4

/* ACPI OEM identification used by the Raspberry Pi 3 UEFI firmware */
#define BCM2837_ACPI_OEM_ID                 "BC2836"
#define BCM2837_ACPI_OEM_TABLE_ID           "RPI3"

/* Set when the FADT identifies a Raspberry Pi 3 (BCM2837) platform. */
extern BOOLEAN Bcm2837Detected;

/**
 * Bcm2837InterruptProbe - Detect the RPi3 and install the IRQ backend.
 *
 * Checks the ACPI FADT OEM identification.  On a match, registers the
 * BCM2836 local-intc/ARMCTRL backend as the HAL's interrupt controller
 * through the platform-extension contract (halext.h) so that no GIC
 * register is ever touched.
 *
 * Idempotent; safe to call from both HalInitializeProcessor(0) and
 * HalInitSystem Phase 0.
 *
 * @param LoaderBlock   Boot loader parameter block (Phase 0), or NULL.
 * @return  TRUE if running on BCM2837.
 */
BOOLEAN
Bcm2837InterruptProbe(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);
