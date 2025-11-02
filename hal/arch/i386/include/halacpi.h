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

extern PHALP_ACPI_MCFG HalpAcpiMcfgTable;
extern PHALP_ACPI_MCFG_ALLOCATION HalpAcpiMcfgAllocations;
extern ULONG HalpAcpiMcfgAllocationCount;
extern PUCHAR HalpAcpiMcfgSegDisabled; /* Per-allocation disable map: 1 = ECAM disabled for that segment */
extern ULONG HalpAcpiMcfgSegDisabledCount;
extern volatile LONG HalpAcpiEcamCoverageFlags;
extern BOOLEAN HalpAcpiEcamDisabled;
extern BOOLEAN HalpPmTimerInitialized;

/* Phase 1 ACPI initialization (pool/registry available) */
VOID
HalpAcpiPhase1Init(VOID);
extern ULONG HalpPmTimerMask;
extern ULONG HalpAcpiPmTimerFrequency;

#define HALP_ACPI_ECAM_COVERAGE_USED              0x00000001L
#define HALP_ACPI_ECAM_COVERAGE_NO_TABLE          0x00000002L
#define HALP_ACPI_ECAM_COVERAGE_SEGMENT_ANY       0x00000004L
#define HALP_ACPI_ECAM_COVERAGE_NO_ALLOCATION     0x00000008L
#define HALP_ACPI_ECAM_COVERAGE_BUS_TOO_HIGH      0x00000010L
#define HALP_ACPI_ECAM_COVERAGE_OFFSET_TOO_HIGH   0x00000020L
#define HALP_ACPI_ECAM_COVERAGE_RANGE_OVERRUN     0x00000040L
#define HALP_ACPI_ECAM_COVERAGE_ZERO_LENGTH       0x00000080L
#define HALP_ACPI_ECAM_COVERAGE_MAP_FAILURE       0x00000100L
#define HALP_ACPI_ECAM_COVERAGE_VENDOR_ALL_ONES   0x00000200L
#define HALP_ACPI_ECAM_COVERAGE_DISABLED_GLOBAL   0x00000400L
#define HALP_ACPI_ECAM_COVERAGE_FORCED_LEGACY     0x00000800L

PHALP_ACPI_MCFG_ALLOCATION
NTAPI
HalpAcpiGetMcfgAllocation(
    _In_ USHORT Segment,
    _In_ UCHAR BusNumber
    );

BOOLEAN
NTAPI
HalpAcpiGetEcamAddress(
    _In_ USHORT Segment,
    _In_ UCHAR BusNumber,
    _In_ UCHAR DeviceNumber,
    _In_ UCHAR FunctionNumber,
    _In_ ULONG RegisterOffset,
    _Out_ PPHYSICAL_ADDRESS Address
    );

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

NTSTATUS
NTAPI
HalpAcpiTableCacheInit(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
    );

PVOID
NTAPI
HalpAcpiGetTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Signature
    );

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpSetupAcpiPhase0(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
    );

PVOID
NTAPI
HalAcpiGetTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Signature
    );

ULONG
NTAPI
HalpAcpiTimerRead(VOID);

/* EOF */
