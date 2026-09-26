/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halarm64/include/halacpip.h
 * PURPOSE:         ARM64 ACPI HAL private definitions
 */

#pragma once

#include <halacpi.h>

#pragma pack(push, 1)

typedef struct _HALP_ACPI_MCFG
{
    DESCRIPTION_HEADER Header;
    ULONGLONG Reserved;
} HALP_ACPI_MCFG, *PHALP_ACPI_MCFG;

typedef struct _HALP_ACPI_MCFG_ALLOCATION
{
    ULONGLONG BaseAddress;
    USHORT PciSegment;
    UCHAR StartBusNumber;
    UCHAR EndBusNumber;
    ULONG Reserved;
} HALP_ACPI_MCFG_ALLOCATION, *PHALP_ACPI_MCFG_ALLOCATION;
#pragma pack(pop)

#define HALP_ACPI_SEGMENT_ANY 0xFFFF

#define ACPI_GAS_SYSTEM_MEMORY 0
#define ACPI_GAS_SYSTEM_IO     1

extern PHALP_ACPI_MCFG_ALLOCATION HalpAcpiMcfgAllocations;
extern ULONG HalpAcpiMcfgAllocationCount;
extern volatile LONG HalpAcpiEcamCoverageFlags;
extern BOOLEAN HalpPmTimerInitialized;
extern GEN_ADDR HalpPm1EventBlocks[2];
extern GEN_ADDR HalpPm1ControlBlocks[2];
extern GEN_ADDR HalpPm2ControlBlock;
extern GEN_ADDR HalpGeneralPurposeBlocks[2];
extern BOOLEAN HalpPm1EventBlockValid[2];
extern BOOLEAN HalpPm1ControlBlockValid[2];
extern BOOLEAN HalpPm2ControlBlockValid;
extern BOOLEAN HalpGeneralPurposeBlockValid[2];

extern ULONG HalpPmTimerMask;

#define HALP_ACPI_ECAM_COVERAGE_USED              0x00000001L
#define HALP_ACPI_ECAM_COVERAGE_NO_TABLE          0x00000002L
#define HALP_ACPI_ECAM_COVERAGE_SEGMENT_ANY       0x00000004L
#define HALP_ACPI_ECAM_COVERAGE_NO_ALLOCATION     0x00000008L
#define HALP_ACPI_ECAM_COVERAGE_BUS_TOO_HIGH      0x00000010L
#define HALP_ACPI_ECAM_COVERAGE_OFFSET_TOO_HIGH   0x00000020L
#define HALP_ACPI_ECAM_COVERAGE_RANGE_OVERRUN     0x00000040L
#define HALP_ACPI_ECAM_COVERAGE_ZERO_LENGTH       0x00000080L

BOOLEAN
NTAPI
HalpAcpiAccessConfigEcam(
    _In_ BOOLEAN Write,
    _In_ USHORT Segment,
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER Slot,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length
    );

BOOLEAN
NTAPI
HalpArm64AccessPciConfigSpace(
    _In_ BOOLEAN Write,
    _In_ USHORT Segment,
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER Slot,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length
    );

BOOLEAN
NTAPI
HalpArm64HasPciConfigSpaceBackend(
    VOID
    );

BOOLEAN
NTAPI
HalpArm64QueryPciRootBusRange(
    _Out_opt_ PULONG MinBus,
    _Out_opt_ PULONG MaxBus
    );

BOOLEAN
HalpAcpiReadRegister(
    _In_ const GEN_ADDR *Gas,
    _Out_ ULONG *Value);

BOOLEAN
HalpAcpiWriteRegister(
    _In_ const GEN_ADDR *Gas,
    _In_ ULONG Value);

/* EOF */
