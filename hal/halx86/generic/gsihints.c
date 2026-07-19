/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     MADT interrupt source override hints for IOAPIC RTE programming
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <hal.h>
#include <reactos/hal/acpi_pci.h>
#define NDEBUG
#include <debug.h>

/* MADT interrupt-source-override IntiFlags per GSI, filled by the ACPI MADT
   parser; HAL variants without that parser leave it empty. */
#define HALP_GSI_HINT_SLOTS HAL_ACPI_MAX_GSI_PINS
#define HALP_GSI_HINT_VALID 0x80

static UCHAR HalpGsiOverrideFlags[HALP_GSI_HINT_SLOTS];

VOID
NTAPI
HalpRegisterInterruptOverride(
    _In_ ULONG SourceIrq,
    _In_ ULONG GlobalIrq,
    _In_ USHORT IntiFlags)
{
    UNREFERENCED_PARAMETER(SourceIrq);

    if (GlobalIrq >= HALP_GSI_HINT_SLOTS)
    {
        DPRINT1("HalpRegisterInterruptOverride: GSI %lu out of range\n", GlobalIrq);
        return;
    }

    HalpGsiOverrideFlags[GlobalIrq] = HALP_GSI_HINT_VALID | (UCHAR)(IntiFlags & 0x0F);
}

BOOLEAN
NTAPI
HalpQueryMadtRteHints(
    _In_ ULONG Gsi,
    _Out_ PBOOLEAN ActiveLow,
    _Out_ PBOOLEAN LevelTriggered)
{
    UCHAR Flags;

    if (Gsi >= HALP_GSI_HINT_SLOTS)
        return FALSE;

    Flags = HalpGsiOverrideFlags[Gsi];
    if (!(Flags & HALP_GSI_HINT_VALID))
        return FALSE;

    /* IntiFlags bits 0-1 polarity, 2-3 trigger; overrides are ISA-only, so
       "conforms" (00) resolves to high/edge */
    *ActiveLow = ((Flags & 0x03) == 0x03);
    *LevelTriggered = (((Flags >> 2) & 0x03) == 0x03);
    return TRUE;
}
