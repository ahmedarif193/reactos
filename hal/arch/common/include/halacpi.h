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

/*
 * HPET Description Table (from ACPI spec)
 * Hardware ID contains:
 *   [31:16] = PCI Vendor ID
 *   [15] = Legacy IRQ Routing Capable
 *   [14] = Reserved
 *   [12:8] = Number of Comparators
 *   [7:0] = Hardware Revision
 */
typedef struct _HPET_DESCRIPTION_TABLE
{
    DESCRIPTION_HEADER Header;
    ULONG HardwareId;
    GEN_ADDR Address;
    UCHAR HpetNumber;
    USHORT MinimumPeriodicTickCount;
    UCHAR PageProtection;
} HPET_DESCRIPTION_TABLE, *PHPET_DESCRIPTION_TABLE;

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

/*
 * HPET Register offsets (from Intel HPET spec)
 */
#define HPET_REG_CAPABILITIES       0x000   /* General Capabilities and ID */
#define HPET_REG_CONFIGURATION      0x010   /* General Configuration */
#define HPET_REG_INT_STATUS         0x020   /* General Interrupt Status */
#define HPET_REG_MAIN_COUNTER       0x0F0   /* Main Counter Value */
#define HPET_REG_TIMER0_CONFIG      0x100   /* Timer 0 Configuration */
#define HPET_REG_TIMER0_COMPARATOR  0x108   /* Timer 0 Comparator Value */

/* HPET Capabilities Register bits */
#define HPET_CAP_COUNT_SIZE_64      (1ULL << 13)  /* 64-bit counter */
#define HPET_CAP_LEGACY_ROUTE       (1ULL << 15)  /* Legacy replacement route */
#define HPET_CAP_NUM_TIMERS_MASK    0x1F00        /* Number of timers - 1 */
#define HPET_CAP_NUM_TIMERS_SHIFT   8
#define HPET_CAP_VENDOR_ID_MASK     0xFFFF0000    /* Vendor ID */
#define HPET_CAP_VENDOR_ID_SHIFT    16
#define HPET_CAP_PERIOD_MASK        0xFFFFFFFF00000000ULL  /* Period in femtoseconds */
#define HPET_CAP_PERIOD_SHIFT       32

/* HPET Configuration Register bits */
#define HPET_CFG_ENABLE             (1ULL << 0)   /* Overall enable */
#define HPET_CFG_LEGACY_ROUTE       (1ULL << 1)   /* Legacy replacement route enable */

/* HPET global variables */
extern BOOLEAN HalpHpetInitialized;
extern PVOID HalpHpetBase;
extern ULONGLONG HalpHpetPeriod;        /* Period in femtoseconds */
extern ULONGLONG HalpHpetFrequency;     /* Frequency in Hz */

extern PHALP_ACPI_MCFG HalpAcpiMcfgTable;
extern PHALP_ACPI_MCFG_ALLOCATION HalpAcpiMcfgAllocations;
extern ULONG HalpAcpiMcfgAllocationCount;
extern PUCHAR HalpAcpiMcfgSegDisabled; /* Per-allocation ECAM state: 0=unknown, 1=working, 2=disabled */
extern ULONG HalpAcpiMcfgSegDisabledCount;
extern volatile LONG HalpAcpiEcamCoverageFlags;
extern BOOLEAN HalpAcpiEcamDisabled;
extern BOOLEAN HalpPmTimerInitialized;
extern FADT HalpFixedAcpiDescTable;
extern GEN_ADDR HalpPm1EventBlocks[2];
extern GEN_ADDR HalpPm1ControlBlocks[2];
extern GEN_ADDR HalpPm2ControlBlock;
extern GEN_ADDR HalpGeneralPurposeBlocks[2];
extern BOOLEAN HalpPm1EventBlockValid[2];
extern BOOLEAN HalpPm1ControlBlockValid[2];
extern BOOLEAN HalpPm2ControlBlockValid;
extern BOOLEAN HalpGeneralPurposeBlockValid[2];

extern PHYSICAL_ADDRESS HalpAcpiRootTablePhysicalAddress;

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiDiscoverHpetTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpHpetInitialize(VOID);

ULONGLONG
NTAPI
HalpHpetQueryCounter(VOID);

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiDiscoverWaetTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiLogRootTablesOrdered(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PFADT Fadt);

PDESCRIPTION_HEADER
NTAPI
HalpAcpiGetCachedTable(
    _In_ ULONG Signature);

PVOID
NTAPI
HalpAcpiCopyBiosTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDESCRIPTION_HEADER TableHeader);

VOID
NTAPI
HalpAcpiCacheTable(
    _In_ PDESCRIPTION_HEADER TableHeader);

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiEnumerateRootTables(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PRSDT Root);

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

#if defined(_M_ARM64) || defined(__aarch64__)
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
#endif

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

PVOID
NTAPI
HaliGetCachedAcpiTable(
    _In_ ULONG Signature,
    _In_opt_ PCSTR OemId,
    _In_opt_ PCSTR OemTableId
    );

ULONG
NTAPI
HalpAcpiTimerRead(VOID);

BOOLEAN
HalpAcpiReadRegister(
    _In_ const GEN_ADDR *Gas,
    _Out_ ULONG *Value);

BOOLEAN
HalpAcpiWriteRegister(
    _In_ const GEN_ADDR *Gas,
    _In_ ULONG Value);

/* EOF */
