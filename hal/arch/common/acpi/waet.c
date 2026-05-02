/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/acpi/waet.c
 * PURPOSE:         WAET ACPI Table Discovery (stub for future integration)
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
HalpAcpiDiscoverWaetTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PDESCRIPTION_HEADER Header;

    Header = HalAcpiGetTable(LoaderBlock, WAET_SIGNATURE);
    if (!Header)
    {
        return;
    }

    DPRINT("HAL: ACPI WAET table discovered (len %lu)\n", Header->Length);
}
