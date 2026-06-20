/*
 * PROJECT:         ReactOS HAL (ARM64)
 * FILE:            hal/halarm64/halarm64_acpi_int.c
 * PURPOSE:         ACPI interrupt routing for ARM64 GIC
 *
 * This file implements ACPI-based interrupt routing for ARM64 systems
 * with GIC (Generic Interrupt Controller). It provides:
 *
 * - GSI (Global System Interrupt) to GIC INTID translation
 * - MADT interrupt source override handling
 * - NMI source identification
 * - Proper polarity and trigger mode configuration
 *
 * Key concepts:
 * - GSI: ACPI's abstract interrupt numbering, used in MADT and _PRT
 * - INTID: GIC's interrupt ID (0-15 SGI, 16-31 PPI, 32-1019 SPI, 8192+ LPI)
 * - SystemVectorBase: Offset from GICD entry in MADT (usually 0)
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <halacpi_arm64.h>

#define NDEBUG
#include <debug.h>

/*
 * Note: The main implementation is in hal/halarm64/acpi/arm64.c
 * This file can be used for additional HAL-specific interrupt routing
 * logic that doesn't belong in the common ACPI code.
 *
 * The functions HalpArm64TranslateGsiToIntId, HalpArm64GetIntOverrideForIrq,
 * and HalpArm64IsNmiSource are implemented in arm64.c and declared in
 * halacpi_arm64.h.
 */

/*
 * ARM64 GIC Interrupt Type Classification
 *
 * This helper determines the interrupt type from the GIC INTID,
 * which affects routing and IRQL assignment.
 */
typedef enum _HAL_ARM64_INT_TYPE
{
    HalArm64IntTypeSgi,    /* Software Generated Interrupt (0-15) */
    HalArm64IntTypePpi,    /* Private Peripheral Interrupt (16-31) */
    HalArm64IntTypeSpi,    /* Shared Peripheral Interrupt (32-1019) */
    HalArm64IntTypeReserved, /* Reserved (1020-1023) */
    HalArm64IntTypeLpi,    /* Locality-specific Peripheral Interrupt (8192+) */
    HalArm64IntTypeInvalid /* Invalid range */
} HAL_ARM64_INT_TYPE;

/*
 * HalpArm64GetInterruptType - Classify an INTID
 */
HAL_ARM64_INT_TYPE
HalpArm64GetInterruptType(
    _In_ ULONG IntId)
{
    if (IntId < 16)
        return HalArm64IntTypeSgi;
    if (IntId < 32)
        return HalArm64IntTypePpi;
    if (IntId < 1020)
        return HalArm64IntTypeSpi;
    if (IntId < 1024)
        return HalArm64IntTypeReserved;
    if (IntId < 8192)
        return HalArm64IntTypeInvalid;
    return HalArm64IntTypeLpi;
}

/*
 * HalpArm64GetInterruptTypeName - Get string name for interrupt type
 */
const char *
HalpArm64GetInterruptTypeName(
    _In_ HAL_ARM64_INT_TYPE Type)
{
    switch (Type)
    {
        case HalArm64IntTypeSgi: return "SGI";
        case HalArm64IntTypePpi: return "PPI";
        case HalArm64IntTypeSpi: return "SPI";
        case HalArm64IntTypeReserved: return "Reserved";
        case HalArm64IntTypeLpi: return "LPI";
        default: return "Invalid";
    }
}

/*
 * HalpArm64TranslatePolarityToGic - Convert ACPI polarity to GIC configuration
 *
 * ACPI polarity values:
 *   0 = Conforms to bus specifications
 *   1 = Active high
 *   2 = Reserved
 *   3 = Active low
 *
 * Returns: TRUE for active-low, FALSE for active-high
 */
BOOLEAN
HalpArm64TranslatePolarityToGic(
    _In_ UCHAR AcpiPolarity,
    _In_ HAL_ARM64_INT_TYPE IntType)
{
    switch (AcpiPolarity)
    {
        case 0:
            /* Bus default: SPIs are typically active-high on ARM64 */
            return FALSE;
        case 1:
            /* Active high */
            return FALSE;
        case 3:
            /* Active low */
            return TRUE;
        default:
            /* Reserved/unknown, use default */
            DPRINT1("[arm64] Unknown polarity %u, using default\n", AcpiPolarity);
            return FALSE;
    }
}

/*
 * HalpArm64TranslateTriggerToGic - Convert ACPI trigger mode to GIC configuration
 *
 * ACPI trigger mode values:
 *   0 = Conforms to bus specifications
 *   1 = Edge-triggered
 *   2 = Reserved
 *   3 = Level-triggered
 *
 * Returns: TRUE for edge-triggered, FALSE for level-triggered
 */
BOOLEAN
HalpArm64TranslateTriggerToGic(
    _In_ UCHAR AcpiTrigger,
    _In_ HAL_ARM64_INT_TYPE IntType)
{
    switch (AcpiTrigger)
    {
        case 0:
            /* Bus default */
            if (IntType == HalArm64IntTypeSgi)
                return TRUE;  /* SGIs are edge-triggered */
            if (IntType == HalArm64IntTypeLpi)
                return TRUE;  /* LPIs are edge-triggered */
            /* SPIs and PPIs default to level-triggered */
            return FALSE;
        case 1:
            /* Edge-triggered */
            return TRUE;
        case 3:
            /* Level-triggered */
            return FALSE;
        default:
            /* Reserved/unknown, use default */
            DPRINT1("[arm64] Unknown trigger %u, using default\n", AcpiTrigger);
            return (IntType == HalArm64IntTypeSgi || IntType == HalArm64IntTypeLpi);
    }
}

/*
 * HalpArm64ConfigureGicInterrupt - Configure GIC interrupt parameters
 *
 * This function sets the polarity and trigger mode for a GIC interrupt.
 * It should be called before enabling the interrupt.
 *
 * For SPIs (INTID 32+), configuration is done via GICD_ICFGR.
 * For PPIs (INTID 16-31), configuration is done via GICR_ICFGR1.
 * SGIs (INTID 0-15) have fixed edge-triggered configuration.
 *
 * Polarity handling:
 *   GICv3 does not have a direct polarity configuration register.
 *   Active-low interrupts should be inverted at the interrupt source
 *   (e.g., GPIO controller) or handled by platform-specific logic.
 *   The ActiveLow parameter is logged but not directly programmed.
 */
VOID
HalpArm64ConfigureGicInterrupt(
    _In_ ULONG IntId,
    _In_ BOOLEAN EdgeTriggered,
    _In_ BOOLEAN ActiveLow)
{
    HAL_ARM64_INT_TYPE Type = HalpArm64GetInterruptType(IntId);

    DPRINT("[arm64] ConfigureGicInterrupt: INTID=%lu Type=%s Edge=%u Low=%u\n",
           IntId, HalpArm64GetInterruptTypeName(Type), EdgeTriggered, ActiveLow);

    /*
     * GIC interrupt configuration:
     *
     * Trigger mode (edge vs. level):
     *   Configured via GICD_ICFGR (for SPIs) or GICR_ICFGR1 (for PPIs).
     *   Each interrupt has 2 bits in ICFGR:
     *     Bit 1: 0 = level-sensitive, 1 = edge-triggered
     *     Bit 0: Reserved (should be written as 0)
     *
     * Polarity (active-high vs. active-low):
     *   GICv3 doesn't have a direct polarity configuration register.
     *   Active-low interrupts must be inverted at the interrupt source
     *   (e.g., by the GPIO controller or platform-specific logic).
     *   We log the polarity for diagnostic purposes but cannot program it
     *   directly into the GIC.
     */

    /*
     * Log active-low polarity for diagnostic purposes.
     * On most ARM64 platforms, interrupts are active-high by default.
     * Active-low configuration requires platform-specific handling.
     */
    if (ActiveLow)
    {
        DPRINT1("[arm64] INTID %lu: Active-low polarity requested but GIC does not support "
                "direct polarity programming. Platform must handle inversion.\n", IntId);
    }

    /*
     * Program the trigger mode via HalpArm64ProgramGicTrigger.
     * This function handles:
     *   - SGIs (0-15): Fixed edge-triggered, returns immediately
     *   - PPIs (16-31): Programs GICR_ICFGR1 in the redistributor
     *   - SPIs (32-1019): Programs GICD_ICFGR in the distributor
     *   - LPIs (8192+): Fixed edge-triggered, returns immediately
     */
    HalpArm64ProgramGicTrigger(IntId, EdgeTriggered);
}
