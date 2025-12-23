/* Minimal ACPI ECAM stubs for legacy (non-ACPI) HALs */
#include "../include/hal.h"
#include "../include/halacpi.h"
#include <reactos/hal/acpi_cstate.h>

volatile LONG HalpAcpiEcamCoverageFlags = 0;
BOOLEAN HalpAcpiEcamDisabled = TRUE;

PHALP_ACPI_MCFG_ALLOCATION NTAPI
HalpAcpiGetMcfgAllocation(USHORT Segment, UCHAR BusNumber)
{
    UNREFERENCED_PARAMETER(Segment);
    UNREFERENCED_PARAMETER(BusNumber);
    InterlockedOr(&HalpAcpiEcamCoverageFlags, HALP_ACPI_ECAM_COVERAGE_NO_TABLE);
    return NULL;
}

BOOLEAN NTAPI
HalpAcpiAccessConfigEcam(BOOLEAN Write, USHORT Segment, ULONG BusNumber,
                         PCI_SLOT_NUMBER Slot, PVOID Buffer, ULONG Offset, ULONG Length)
{
    UNREFERENCED_PARAMETER(Write);
    UNREFERENCED_PARAMETER(Segment);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(Slot);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Length);
    InterlockedOr(&HalpAcpiEcamCoverageFlags, HALP_ACPI_ECAM_COVERAGE_DISABLED_GLOBAL);
    return FALSE;
}

BOOLEAN NTAPI
HalpAcpiQueryPowerButton(VOID)
{
    return FALSE;
}

NTSTATUS NTAPI
HalGetAcpiCStateInformation(
    _Out_ PHAL_ACPI_C_STATE_INFO Info)
{
    UNREFERENCED_PARAMETER(Info);
    return STATUS_NOT_SUPPORTED;
}

BOOLEAN NTAPI
HalIsAcpiBusMasterActive(VOID)
{
    return FALSE;
}
