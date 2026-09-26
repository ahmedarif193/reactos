#pragma once

//
// Internal HAL structure
//
typedef struct _ACPI_CACHED_TABLE
{
    LIST_ENTRY Links;
    DESCRIPTION_HEADER Header;
    /* table follows */
} ACPI_CACHED_TABLE, *PACPI_CACHED_TABLE;

/* acpi/tables.c */
extern LIST_ENTRY HalpAcpiTableCacheList;
extern PHYSICAL_ADDRESS HalpAcpiRsdpAddress;
extern PHYSICAL_ADDRESS HalpAcpiRootTablePhysicalAddress;
extern ULONG HalpInvalidAcpiTable;

/* acpi/halacpi.c */
extern BOOLEAN HalpProcessedACPIPhase0;
extern BOOLEAN HalpPhysicalMemoryMayAppearAbove4GB;
extern FADT HalpFixedAcpiDescTable;
extern PDEBUG_PORT_TABLE HalpDebugPortTable;
extern PACPI_SRAT HalpAcpiSrat;
extern PHYSICAL_ADDRESS HalpMaxHotPlugMemoryAddress;
extern ULONG HalpPicVectorRedirect[16];

NTSTATUS
NTAPI
HalpAcpiTableCacheInit(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
    );

PVOID
NTAPI
HalpAcpiGetTable(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Signature
    );

PVOID
NTAPI
HalAcpiGetTable(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Signature
    );

PDESCRIPTION_HEADER
NTAPI
HalpAcpiGetCachedTable(
    _In_ ULONG Signature
    );

PVOID
NTAPI
HalpAcpiCopyBiosTable(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDESCRIPTION_HEADER TableHeader
    );

VOID
NTAPI
HalpAcpiCacheTable(
    _In_ PDESCRIPTION_HEADER TableHeader
    );

PVOID
NTAPI
HaliGetCachedAcpiTable(
    _In_ ULONG Signature,
    _In_opt_ PCSTR OemId,
    _In_opt_ PCSTR OemTableId
    );

PFN_COUNT
NTAPI
HalpAcpiPagesForRange(
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ ULONG Length
    );

PHYSICAL_ADDRESS
NTAPI
HalpAcpiSelectFadtPointer(
    _In_ ULONG LegacyPointer,
    _In_ PHYSICAL_ADDRESS ExtendedPointer
    );

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiLogTables(
    _In_ PFADT Fadt
    );

/*
 * Architecture hook: map phase 0 HAL memory that holds a cached table.
 * The mapping must stay valid for the lifetime of the system.
 */
PVOID
NTAPI
HalpAcpiMapCachedTable(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ PFN_COUNT PageCount
    );

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpSetupAcpiPhase0(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
    );

VOID
NTAPI
HalpNumaInitializeStaticConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
    );

VOID
NTAPI
HalpDynamicSystemResourceConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
    );

VOID
NTAPI
HalpAcpiDetectMachineSpecificActions(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PFADT DescriptionTable
    );

VOID
NTAPI
HalpInitBootTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
    );

VOID
NTAPI
HalpInitNonBusHandler(
    VOID
    );

/* EOF */
