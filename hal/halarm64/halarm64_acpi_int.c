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
