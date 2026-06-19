#pragma once

#include <ntifs.h>
#include <arc/arc.h>

typedef struct _HALP_ARM64_GICC_ENTRY
{
    ULONGLONG Mpidr;
    ULONGLONG GicrBase;
    ULONGLONG GiccBase;
    ULONG Flags;
    ULONG ParkingVersion;       /* MADT GICC Parking Protocol Version (0 = PSCI) */
    ULONGLONG ParkedAddress;    /* MADT GICC Parked Address (mailbox PA)          */
} HALP_ARM64_GICC_ENTRY, *PHALP_ARM64_GICC_ENTRY;

/*
 * Maximum number of GICR (redistributor) regions from MADT.
 * Multi-socket ARM64 systems can have separate GICR regions per socket.
 * This matches HALP_GIC_MAX_REDIST_REGIONS in gic_internal.h.
 */
#define HALP_ARM64_MAX_GICR_REGIONS 8

#define HALP_ARM64_MAX_ITS_ENTRIES 4
#define HALP_ARM64_MAX_MSI_FRAMES 4

/*
 * HALP_ARM64_GICR_REGION - Describes a GICR region from MADT
 *
 * Populated from ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR (0x0E) entries.
 * Each entry describes a contiguous memory region containing one or
 * more redistributor frames.
 */
typedef struct _HALP_ARM64_GICR_REGION
{
    ULONGLONG BaseAddress;      /* Physical base address */
    ULONG Length;               /* Region length in bytes */
} HALP_ARM64_GICR_REGION, *PHALP_ARM64_GICR_REGION;

typedef struct _HALP_ARM64_GIC_ITS_ENTRY
{
    ULONG ItsId;
    ULONGLONG BaseAddress;
} HALP_ARM64_GIC_ITS_ENTRY, *PHALP_ARM64_GIC_ITS_ENTRY;

typedef struct _HALP_ARM64_GIC_MSI_FRAME_ENTRY
{
    ULONG MsiFrameId;
    ULONGLONG BaseAddress;
    USHORT SpiBase;
    USHORT SpiCount;
    ULONG Flags;
} HALP_ARM64_GIC_MSI_FRAME_ENTRY, *PHALP_ARM64_GIC_MSI_FRAME_ENTRY;

/*
 * ARM64 Interrupt Source Override Entry
 *
 * On ARM64 with GIC, interrupt source overrides are less common than x86
 * but can still occur. This structure maps a source interrupt (e.g., from
 * a legacy device or platform-specific source) to a GSI with polarity and
 * trigger mode settings.
 *
 * Polarity values:
 *   0 = Conforms to bus specifications
 *   1 = Active high
 *   2 = Reserved
 *   3 = Active low
 *
 * Trigger mode values:
 *   0 = Conforms to bus specifications
 *   1 = Edge-triggered
 *   2 = Reserved
 *   3 = Level-triggered
 */
#define HALP_ARM64_MAX_INT_OVERRIDES 32

typedef struct _HALP_ARM64_INT_OVERRIDE_ENTRY
{
    UCHAR Bus;              /* Source bus (0 = system) */
    UCHAR SourceIrq;        /* Source IRQ number */
    UCHAR Polarity;         /* 0=bus, 1=active-high, 3=active-low */
    UCHAR TriggerMode;      /* 0=bus, 1=edge, 3=level */
    ULONG GlobalSystemInterrupt;  /* GSI (maps to GIC INTID for SPIs) */
} HALP_ARM64_INT_OVERRIDE_ENTRY, *PHALP_ARM64_INT_OVERRIDE_ENTRY;

/*
 * ARM64 NMI Source Entry
 *
 * Non-Maskable Interrupt sources on ARM64.
 * These are typically PPIs (Private Peripheral Interrupts) that can
 * be configured as NMIs via GIC priority settings.
 */
#define HALP_ARM64_MAX_NMI_SOURCES 8

typedef struct _HALP_ARM64_NMI_SOURCE_ENTRY
{
    UCHAR Polarity;         /* Interrupt polarity */
    UCHAR TriggerMode;      /* Trigger mode */
    USHORT Reserved;
    ULONG GlobalSystemInterrupt;  /* GSI */
} HALP_ARM64_NMI_SOURCE_ENTRY, *PHALP_ARM64_NMI_SOURCE_ENTRY;

typedef struct _HALP_ARM64_GIC_INFO
{
    BOOLEAN Present;
    UCHAR GicVersion;
    UCHAR Reserved[2];
    ULONGLONG GicdBase;
    ULONGLONG GiccBase;
    ULONGLONG GicrBase;           /* Legacy: first GICR region base (for compatibility) */
    ULONGLONG GicrLength;         /* Legacy: first GICR region length */
    ULONG SystemVectorBase;       /* GICD system vector base (from MADT) */
    ULONG ItsCount;
    ULONG MsiFrameCount;
    ULONG GiccEntryCount;
    ULONG GicrRegionCount;        /* Number of GICR regions from MADT */
    ULONG IntOverrideCount;       /* Number of interrupt source overrides */
    ULONG NmiSourceCount;         /* Number of NMI sources */
    HALP_ARM64_GICC_ENTRY GiccEntries[MAXIMUM_PROCESSORS];
    HALP_ARM64_GIC_ITS_ENTRY ItsEntries[HALP_ARM64_MAX_ITS_ENTRIES];
    HALP_ARM64_GIC_MSI_FRAME_ENTRY MsiFrames[HALP_ARM64_MAX_MSI_FRAMES];
    HALP_ARM64_GICR_REGION GicrRegions[HALP_ARM64_MAX_GICR_REGIONS];  /* All GICR regions */
    HALP_ARM64_INT_OVERRIDE_ENTRY IntOverrides[HALP_ARM64_MAX_INT_OVERRIDES];
    HALP_ARM64_NMI_SOURCE_ENTRY NmiSources[HALP_ARM64_MAX_NMI_SOURCES];
} HALP_ARM64_GIC_INFO, *PHALP_ARM64_GIC_INFO;

typedef struct _HALP_ARM64_GTDT_INFO
{
    BOOLEAN Present;
    UCHAR Reserved[3];
    ULONGLONG CounterBlockAddress;
    ULONGLONG CounterReadBlockAddress;
    ULONG SecureEl1Interrupt;
    ULONG SecureEl1Flags;
    ULONG NonSecureEl1Interrupt;
    ULONG NonSecureEl1Flags;
    ULONG VirtualTimerInterrupt;
    ULONG VirtualTimerFlags;
    ULONG NonSecureEl2Interrupt;
    ULONG NonSecureEl2Flags;
    ULONG PlatformTimerCount;
    ULONG PlatformTimerOffset;
} HALP_ARM64_GTDT_INFO, *PHALP_ARM64_GTDT_INFO;

typedef struct _HALP_ARM64_SPCR_INFO
{
    BOOLEAN Present;
    UCHAR InterfaceType;
    UCHAR BaudRate;
    UCHAR Parity;
    UCHAR StopBits;
    UCHAR FlowControl;
    UCHAR TerminalType;
    UCHAR AddressSpaceId;
    UCHAR AccessSize;
    UCHAR BitWidth;
    UCHAR BitOffset;
    UCHAR Reserved;
    ULONG Interrupt;
    UCHAR InterruptType;
    UCHAR PcInterrupt;
    USHORT Reserved2;
    ULONGLONG BaseAddress;
} HALP_ARM64_SPCR_INFO, *PHALP_ARM64_SPCR_INFO;

typedef struct _HALP_ARM64_PSCI_INFO
{
    BOOLEAN Present;
    BOOLEAN UseHvc;
    USHORT ArmBootArch;
} HALP_ARM64_PSCI_INFO, *PHALP_ARM64_PSCI_INFO;

/*
 * IORT (I/O Remapping Table) - ARM64 specific
 * Describes SMMU and device ID mappings for DMA/MSI-X
 */
#define HALP_ARM64_MAX_IORT_SMMU 4
#define HALP_ARM64_MAX_IORT_RC   8
#define HALP_ARM64_MAX_IORT_NAMED 16
#define HALP_ARM64_MAX_IORT_ITS_GROUPS 4

typedef struct _HALP_ARM64_IORT_SMMU_ENTRY
{
    ULONGLONG BaseAddress;
    ULONGLONG Span;
    ULONG Model;
    ULONG Flags;
    ULONG GlobalInterruptArrayOffset;
    ULONG ContextInterruptCount;
    ULONG ContextInterruptArrayOffset;
    ULONG PmuInterruptCount;
    ULONG PmuInterruptArrayOffset;
    ULONG Nsgirpt;
    ULONGLONG NsgcfgirptGsiv;
} HALP_ARM64_IORT_SMMU_ENTRY, *PHALP_ARM64_IORT_SMMU_ENTRY;

typedef struct _HALP_ARM64_IORT_ROOT_COMPLEX_ENTRY
{
    ULONG CacheCoherent;
    UCHAR AllocationHints;
    UCHAR Reserved;
    UCHAR MemoryAccessFlags;
    UCHAR AtsAttribute;
    ULONG PciSegmentNumber;
    UCHAR MemoryAddressLimit;
    UCHAR Reserved2[3];
} HALP_ARM64_IORT_ROOT_COMPLEX_ENTRY, *PHALP_ARM64_IORT_ROOT_COMPLEX_ENTRY;

typedef struct _HALP_ARM64_IORT_INFO
{
    BOOLEAN Present;
    UCHAR Reserved[3];
    ULONG SmmuCount;
    ULONG RootComplexCount;
    HALP_ARM64_IORT_SMMU_ENTRY SmmuEntries[HALP_ARM64_MAX_IORT_SMMU];
    HALP_ARM64_IORT_ROOT_COMPLEX_ENTRY RootComplexEntries[HALP_ARM64_MAX_IORT_RC];
} HALP_ARM64_IORT_INFO, *PHALP_ARM64_IORT_INFO;

/*
 * PPTT (Processor Properties Topology Table) - ARM64 specific
 * Describes CPU topology including clusters and cache hierarchy
 */
#define HALP_ARM64_MAX_PPTT_PROCESSORS 64
#define HALP_ARM64_MAX_PPTT_CACHES     128

typedef struct _HALP_ARM64_PPTT_PROCESSOR_ENTRY
{
    ULONG Flags;
    ULONG Parent;
    ULONG AcpiProcessorId;
    ULONG NumberOfCaches;
    ULONG CacheRefs[4];   /* First 4 cache references */
} HALP_ARM64_PPTT_PROCESSOR_ENTRY, *PHALP_ARM64_PPTT_PROCESSOR_ENTRY;

typedef struct _HALP_ARM64_PPTT_CACHE_ENTRY
{
    ULONG Flags;
    ULONG NextLevelCache;
    ULONG Size;
    ULONG NumberOfSets;
    UCHAR Associativity;
    UCHAR Attributes;
    USHORT LineSize;
} HALP_ARM64_PPTT_CACHE_ENTRY, *PHALP_ARM64_PPTT_CACHE_ENTRY;

typedef struct _HALP_ARM64_PPTT_INFO
{
    BOOLEAN Present;
    UCHAR Reserved[3];
    ULONG ProcessorCount;
    ULONG CacheCount;
    HALP_ARM64_PPTT_PROCESSOR_ENTRY Processors[HALP_ARM64_MAX_PPTT_PROCESSORS];
    HALP_ARM64_PPTT_CACHE_ENTRY Caches[HALP_ARM64_MAX_PPTT_CACHES];
} HALP_ARM64_PPTT_INFO, *PHALP_ARM64_PPTT_INFO;

extern HALP_ARM64_GIC_INFO HalpArm64GicInfo;
extern HALP_ARM64_GTDT_INFO HalpArm64GtdtInfo;
extern HALP_ARM64_SPCR_INFO HalpArm64SpcrInfo;
extern HALP_ARM64_PSCI_INFO HalpArm64PsciInfo;
extern HALP_ARM64_IORT_INFO HalpArm64IortInfo;
extern HALP_ARM64_PPTT_INFO HalpArm64PpttInfo;

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiDiscoverArm64Tables(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

/*
 * HalpArm64TranslateGsiToIntId - Translate a GSI to GIC INTID
 *
 * This function handles the translation from Global System Interrupt (GSI)
 * numbers as used in ACPI tables to GIC interrupt IDs (INTIDs).
 *
 * On ARM64 with GIC:
 * - GSIs 0-15 map to SGIs (Software Generated Interrupts) - per-CPU
 * - GSIs 16-31 map to PPIs (Private Peripheral Interrupts) - per-CPU
 * - GSIs 32-1019 map to SPIs (Shared Peripheral Interrupts) - system-wide
 * - GSIs 8192+ map to LPIs (Locality-specific Peripheral Interrupts) for MSI
 *
 * The function also applies any interrupt source overrides from MADT.
 *
 * Parameters:
 *   Gsi         - Global System Interrupt number from ACPI
 *   Polarity    - Output: Interrupt polarity (optional)
 *   TriggerMode - Output: Trigger mode (optional)
 *
 * Returns:
 *   The GIC INTID corresponding to the GSI, or (ULONG)-1 on error.
 */
ULONG
NTAPI
HalpArm64TranslateGsiToIntId(
    _In_ ULONG Gsi,
    _Out_opt_ PUCHAR Polarity,
    _Out_opt_ PUCHAR TriggerMode);

/*
 * HalpArm64GetIntOverrideForIrq - Find interrupt override for a source IRQ
 *
 * Searches the interrupt override table for an entry matching the source IRQ.
 *
 * Parameters:
 *   Bus       - Bus type (0 = system bus)
 *   SourceIrq - Source interrupt number
 *
 * Returns:
 *   Pointer to the override entry, or NULL if no override exists.
 */
PHALP_ARM64_INT_OVERRIDE_ENTRY
NTAPI
HalpArm64GetIntOverrideForIrq(
    _In_ UCHAR Bus,
    _In_ UCHAR SourceIrq);

/*
 * HalpArm64IsNmiSource - Check if a GSI is configured as NMI
 *
 * Parameters:
 *   Gsi - Global System Interrupt number
 *
 * Returns:
 *   TRUE if the GSI is an NMI source, FALSE otherwise.
 */
BOOLEAN
NTAPI
HalpArm64IsNmiSource(
    _In_ ULONG Gsi);

/*
 * HalpArm64ProgramGicTrigger - Program GIC interrupt trigger mode
 *
 * This function configures the trigger mode (edge vs level) for a GIC interrupt.
 * It programs the appropriate ICFGR register in either the distributor (for SPIs)
 * or the redistributor (for PPIs).
 *
 * Parameters:
 *   IntId         - GIC interrupt ID (INTID)
 *   EdgeTriggered - TRUE for edge-triggered, FALSE for level-triggered
 *
 * Notes:
 *   - SGIs (0-15) have fixed edge-triggered configuration and cannot be changed
 *   - PPIs (16-31) are configured via GICR_ICFGR1 in the redistributor
 *   - SPIs (32-1019) are configured via GICD_ICFGR in the distributor
 *   - LPIs (8192+) are always edge-triggered
 */
VOID
NTAPI
HalpArm64ProgramGicTrigger(
    _In_ ULONG IntId,
    _In_ BOOLEAN EdgeTriggered);
