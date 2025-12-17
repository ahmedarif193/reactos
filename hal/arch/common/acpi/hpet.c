/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/acpi/hpet.c
 * PURPOSE:         HPET ACPI Table Discovery (stub for future timer bring-up)
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

#include <hal.h>
#include <halacpi.h>
#include <ntifs.h>
#include <reactos/drivers/acpi/acpi.h>
#define NDEBUG
#include <debug.h>

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiDiscoverHpetTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PDESCRIPTION_HEADER Header;

    Header = HalAcpiGetTable(LoaderBlock, HPET_SIGNATURE);
    if (!Header)
    {
        return;
    }

    DPRINT("HAL: ACPI HPET table discovered (len %lu)\n", Header->Length);
}
