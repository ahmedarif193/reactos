/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halarm64/acpi/arm64.c
 * PURPOSE:         ARM64 ACPI table discovery helpers
 */

#include <hal.h>
#include <halacpi.h>
#include <halacpi_arm64.h>
#include <ntifs.h>
#define NDEBUG
#include <debug.h>

#ifndef GTDT_SIGNATURE
#define GTDT_SIGNATURE 0x54445447 /* "GTDT" */
#endif
#ifndef SPCR_SIGNATURE
#define SPCR_SIGNATURE 0x52435053 /* "SPCR" */
#endif
#ifndef IORT_SIGNATURE
#define IORT_SIGNATURE 0x54524F49 /* "IORT" */
#endif
#ifndef PPTT_SIGNATURE
#define PPTT_SIGNATURE 0x54545050 /* "PPTT" */
#endif

/*
 * MADT subtable types for ARM64 GIC structures
 * Types 0x0B-0x0F are ARM64-specific GIC entries
 */
#define ACPI_MADT_TYPE_INTERRUPT_OVERRIDE     0x02  /* Interrupt source override */
#define ACPI_MADT_TYPE_NMI_SOURCE             0x03  /* NMI source */
#define ACPI_MADT_TYPE_GENERIC_INTERRUPT      0x0B  /* GIC CPU Interface (GICC) */
#define ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR    0x0C  /* GIC Distributor (GICD) */
#define ACPI_MADT_TYPE_GENERIC_MSI_FRAME      0x0D  /* GIC MSI Frame */
#define ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR  0x0E  /* GIC Redistributor (GICR) */
#define ACPI_MADT_TYPE_GENERIC_TRANSLATOR     0x0F  /* GIC Interrupt Translation Service (ITS) */

#ifndef ACPI_FADT_PSCI_COMPLIANT
#define ACPI_FADT_PSCI_COMPLIANT 0x00000001
#endif
#ifndef ACPI_FADT_PSCI_USE_HVC
#define ACPI_FADT_PSCI_USE_HVC    0x00000002
#endif

#include <pshpack1.h>
typedef struct _ACPI_SUBTABLE_HEADER
{
    UCHAR Type;
    UCHAR Length;
} ACPI_SUBTABLE_HEADER, *PACPI_SUBTABLE_HEADER;

typedef struct _ACPI_MADT
{
    DESCRIPTION_HEADER Header;
    ULONG LocalApicAddress;
    ULONG Flags;
} ACPI_MADT, *PACPI_MADT;

typedef struct _ACPI_MADT_GENERIC_DISTRIBUTOR
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG GicId;
    ULONGLONG BaseAddress;
    ULONG SystemVectorBase;
    UCHAR GicVersion;
    UCHAR Reserved2[3];
} ACPI_MADT_GENERIC_DISTRIBUTOR, *PACPI_MADT_GENERIC_DISTRIBUTOR;

typedef struct _ACPI_MADT_GENERIC_REDISTRIBUTOR
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONGLONG BaseAddress;
    ULONG Length;
    ULONG Reserved2;
} ACPI_MADT_GENERIC_REDISTRIBUTOR, *PACPI_MADT_GENERIC_REDISTRIBUTOR;

typedef struct _ACPI_MADT_GENERIC_MSI_FRAME
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG MsiFrameId;
    ULONGLONG BaseAddress;
    ULONG Flags;
    USHORT SpiCount;
    USHORT SpiBase;
} ACPI_MADT_GENERIC_MSI_FRAME, *PACPI_MADT_GENERIC_MSI_FRAME;

typedef struct _ACPI_MADT_GENERIC_TRANSLATOR
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG TranslationId;
    ULONGLONG BaseAddress;
    ULONG Reserved2;
} ACPI_MADT_GENERIC_TRANSLATOR, *PACPI_MADT_GENERIC_TRANSLATOR;

typedef struct _ACPI_MADT_GENERIC_INTERRUPT
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG CpuInterfaceNumber;
    ULONG AcpiProcessorUid;
    ULONG Flags;
    ULONG ParkingProtocolVersion;
    ULONG PerformanceInterrupt;
    ULONGLONG ParkedAddress;
    ULONGLONG BaseAddress;
    ULONGLONG GicvBaseAddress;
    ULONGLONG GichBaseAddress;
    ULONG VgicMaintenanceInterrupt;
    ULONGLONG GicrBaseAddress;
    ULONGLONG Mpidr;
} ACPI_MADT_GENERIC_INTERRUPT, *PACPI_MADT_GENERIC_INTERRUPT;

/*
 * Interrupt Source Override structure (Type 0x02)
 * Used on ARM64 to redirect platform interrupts to different GSIs
 * with specific polarity and trigger mode settings.
 */
typedef struct _ACPI_MADT_INTERRUPT_OVERRIDE
{
    ACPI_SUBTABLE_HEADER Header;
    UCHAR Bus;                      /* Bus type (0 = system/ISA) */
    UCHAR SourceIrq;                /* Original IRQ number */
    ULONG GlobalSystemInterrupt;    /* Target GSI */
    USHORT IntiFlags;               /* Polarity (bits 0-1) and Trigger (bits 2-3) */
} ACPI_MADT_INTERRUPT_OVERRIDE, *PACPI_MADT_INTERRUPT_OVERRIDE;

/*
 * NMI Source structure (Type 0x03)
 * Describes platform Non-Maskable Interrupt sources.
 */
typedef struct _ACPI_MADT_NMI_SOURCE
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT IntiFlags;               /* Polarity (bits 0-1) and Trigger (bits 2-3) */
    ULONG GlobalSystemInterrupt;    /* GSI for this NMI */
} ACPI_MADT_NMI_SOURCE, *PACPI_MADT_NMI_SOURCE;

typedef struct _ACPI_TABLE_GTDT
{
    DESCRIPTION_HEADER Header;
    ULONGLONG CounterBlockAddress;
    ULONG Reserved;
    ULONG SecureEl1Interrupt;
    ULONG SecureEl1Flags;
    ULONG NonSecureEl1Interrupt;
    ULONG NonSecureEl1Flags;
    ULONG VirtualTimerInterrupt;
    ULONG VirtualTimerFlags;
    ULONG NonSecureEl2Interrupt;
    ULONG NonSecureEl2Flags;
    ULONGLONG CounterReadBlockAddress;
    ULONG PlatformTimerCount;
    ULONG PlatformTimerOffset;
} ACPI_TABLE_GTDT, *PACPI_TABLE_GTDT;

typedef struct _ACPI_TABLE_SPCR
{
    DESCRIPTION_HEADER Header;
    UCHAR InterfaceType;
    UCHAR Reserved[3];
    GEN_ADDR SerialPort;
    UCHAR InterruptType;
    UCHAR PcInterrupt;
    ULONG Interrupt;
    UCHAR BaudRate;
    UCHAR Parity;
    UCHAR StopBits;
    UCHAR FlowControl;
    UCHAR TerminalType;
    UCHAR Reserved1;
    USHORT PciDeviceId;
    USHORT PciVendorId;
    UCHAR PciBus;
    UCHAR PciDevice;
    UCHAR PciFunction;
    ULONG PciFlags;
    UCHAR PciSegment;
    ULONG Reserved2;
} ACPI_TABLE_SPCR, *PACPI_TABLE_SPCR;

/*
 * IORT (I/O Remapping Table) structures
 */
typedef struct _ACPI_TABLE_IORT
{
    DESCRIPTION_HEADER Header;
    ULONG NodeCount;
    ULONG NodeOffset;
    ULONG Reserved;
} ACPI_TABLE_IORT, *PACPI_TABLE_IORT;

typedef struct _ACPI_IORT_NODE
{
    UCHAR Type;
    USHORT Length;
    UCHAR Revision;
    ULONG Identifier;
    ULONG MappingCount;
    ULONG MappingOffset;
} ACPI_IORT_NODE, *PACPI_IORT_NODE;

/* IORT node types */
#define ACPI_IORT_NODE_ITS_GROUP      0
#define ACPI_IORT_NODE_NAMED_COMPONENT 1
#define ACPI_IORT_NODE_ROOT_COMPLEX   2
#define ACPI_IORT_NODE_SMMU           3
#define ACPI_IORT_NODE_SMMU_V3        4
#define ACPI_IORT_NODE_PMCG           5

typedef struct _ACPI_IORT_SMMU
{
    ULONGLONG BaseAddress;
    ULONGLONG Span;
    ULONG Model;
    ULONG Flags;
    ULONG GlobalInterruptOffset;
    ULONG ContextInterruptCount;
    ULONG ContextInterruptOffset;
    ULONG PmuInterruptCount;
    ULONG PmuInterruptOffset;
    ULONG Nsgirpt;
    ULONGLONG NsgcfgirptGsiv;
} ACPI_IORT_SMMU, *PACPI_IORT_SMMU;

typedef struct _ACPI_IORT_ROOT_COMPLEX
{
    ULONG CacheCoherent;
    UCHAR AllocationHints;
    USHORT Reserved;
    UCHAR MemoryAccessFlags;
    ULONG AtsAttribute;
    ULONG PciSegmentNumber;
    UCHAR MemoryAddressLimit;
    UCHAR Reserved2[3];
} ACPI_IORT_ROOT_COMPLEX, *PACPI_IORT_ROOT_COMPLEX;

/*
 * PPTT (Processor Properties Topology Table) structures
 */
typedef struct _ACPI_TABLE_PPTT
{
    DESCRIPTION_HEADER Header;
} ACPI_TABLE_PPTT, *PACPI_TABLE_PPTT;

typedef struct _ACPI_PPTT_SUBTABLE
{
    UCHAR Type;
    UCHAR Length;
    USHORT Reserved;
} ACPI_PPTT_SUBTABLE, *PACPI_PPTT_SUBTABLE;

/* PPTT subtable types */
#define ACPI_PPTT_TYPE_PROCESSOR  0
#define ACPI_PPTT_TYPE_CACHE      1
#define ACPI_PPTT_TYPE_ID         2

typedef struct _ACPI_PPTT_PROCESSOR
{
    ACPI_PPTT_SUBTABLE Header;
    ULONG Flags;
    ULONG Parent;
    ULONG AcpiProcessorId;
    ULONG NumberOfPrivResources;
    /* Private resources follow */
} ACPI_PPTT_PROCESSOR, *PACPI_PPTT_PROCESSOR;

typedef struct _ACPI_PPTT_CACHE
{
    ACPI_PPTT_SUBTABLE Header;
    ULONG Flags;
    ULONG NextLevelCache;
    ULONG Size;
    ULONG NumberOfSets;
    UCHAR Associativity;
    UCHAR Attributes;
    USHORT LineSize;
} ACPI_PPTT_CACHE, *PACPI_PPTT_CACHE;

#include <poppack.h>

HALP_ARM64_GIC_INFO HalpArm64GicInfo;
HALP_ARM64_GTDT_INFO HalpArm64GtdtInfo;
HALP_ARM64_SPCR_INFO HalpArm64SpcrInfo;
HALP_ARM64_PSCI_INFO HalpArm64PsciInfo;
HALP_ARM64_IORT_INFO HalpArm64IortInfo;
HALP_ARM64_PPTT_INFO HalpArm64PpttInfo;

static BOOLEAN HalpArm64AcpiParsed;

static __inline ULONGLONG
HalpReadUnalignedU64(
    _In_ const VOID *Source)
{
    ULONGLONG Value;

    RtlCopyMemory(&Value, Source, sizeof(Value));
    return Value;
}

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiDiscoverArm64Tables(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_MADT Madt;
    PACPI_TABLE_GTDT Gtdt;
    PACPI_TABLE_SPCR Spcr;
    PFADT Fadt;

    if (HalpArm64AcpiParsed)
        return;

    RtlZeroMemory(&HalpArm64GicInfo, sizeof(HalpArm64GicInfo));
    RtlZeroMemory(&HalpArm64GtdtInfo, sizeof(HalpArm64GtdtInfo));
    RtlZeroMemory(&HalpArm64SpcrInfo, sizeof(HalpArm64SpcrInfo));
    RtlZeroMemory(&HalpArm64PsciInfo, sizeof(HalpArm64PsciInfo));
    RtlZeroMemory(&HalpArm64IortInfo, sizeof(HalpArm64IortInfo));
    RtlZeroMemory(&HalpArm64PpttInfo, sizeof(HalpArm64PpttInfo));

    Madt = HalAcpiGetTable(LoaderBlock, APIC_SIGNATURE);
    if (!Madt || Madt->Header.Length < sizeof(*Madt))
    {
        /* ACPI tables not available yet; allow retry on next call */
        DPRINT1("[arm64][ACPI] MADT not available yet, will retry\n");
        return;
    }

    /* MADT found, mark as parsed so we don't re-parse */
    HalpArm64AcpiParsed = TRUE;

    if (Madt->Header.Length >= sizeof(*Madt))
    {
        ULONG_PTR TableEnd = (ULONG_PTR)Madt + Madt->Header.Length;
        PACPI_SUBTABLE_HEADER Entry =
            (PACPI_SUBTABLE_HEADER)((ULONG_PTR)Madt + sizeof(*Madt));

        DPRINT1("[arm64][ACPI] MADT: Parsing table at %p, length=%lu\n",
                Madt, Madt->Header.Length);

        while ((ULONG_PTR)Entry + sizeof(*Entry) <= TableEnd)
        {
            if (Entry->Length < sizeof(*Entry))
                break;
            if ((ULONG_PTR)Entry + Entry->Length > TableEnd)
                break;

            DPRINT1("[arm64][ACPI] MADT: Entry Type=0x%x Length=%u\n",
                    Entry->Type, Entry->Length);

            switch (Entry->Type)
            {
                case ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:
                {
                    PACPI_MADT_GENERIC_DISTRIBUTOR Dist =
                        (PACPI_MADT_GENERIC_DISTRIBUTOR)Entry;
                    if (Entry->Length >= sizeof(*Dist))
                    {
                        ULONGLONG Base = HalpReadUnalignedU64(&Dist->BaseAddress);
                        if (Base)
                        {
                            HalpArm64GicInfo.GicdBase = Base;
                            HalpArm64GicInfo.Present = TRUE;
                        }
                        HalpArm64GicInfo.GicVersion = Dist->GicVersion;
                        HalpArm64GicInfo.SystemVectorBase = Dist->SystemVectorBase;
                    }
                    break;
                }
                case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:
                {
                    /*
                     * Interrupt Source Override - remaps source IRQ to GSI
                     * with specified polarity and trigger mode.
                     */
                    PACPI_MADT_INTERRUPT_OVERRIDE Override =
                        (PACPI_MADT_INTERRUPT_OVERRIDE)Entry;
                    if (Entry->Length >= sizeof(*Override))
                    {
                        ULONG Index = HalpArm64GicInfo.IntOverrideCount;
                        if (Index < HALP_ARM64_MAX_INT_OVERRIDES)
                        {
                            HALP_ARM64_INT_OVERRIDE_ENTRY *Out =
                                &HalpArm64GicInfo.IntOverrides[Index];
                            Out->Bus = Override->Bus;
                            Out->SourceIrq = Override->SourceIrq;
                            Out->GlobalSystemInterrupt = Override->GlobalSystemInterrupt;
                            /* Extract polarity from bits 0-1 */
                            Out->Polarity = (UCHAR)(Override->IntiFlags & 0x03);
                            /* Extract trigger mode from bits 2-3 */
                            Out->TriggerMode = (UCHAR)((Override->IntiFlags >> 2) & 0x03);
                            HalpArm64GicInfo.IntOverrideCount++;

                            DPRINT1("[arm64][ACPI] MADT: IntOverride Bus=%u Src=%u -> GSI=%lu Pol=%u Trig=%u\n",
                                    Out->Bus, Out->SourceIrq, Out->GlobalSystemInterrupt,
                                    Out->Polarity, Out->TriggerMode);
                        }
                    }
                    break;
                }
                case ACPI_MADT_TYPE_NMI_SOURCE:
                {
                    /*
                     * NMI Source - identifies GSIs that should be treated as NMIs.
                     * On ARM64 with GIC, these would be configured with priority 0
                     * (highest/non-maskable).
                     */
                    PACPI_MADT_NMI_SOURCE NmiSrc = (PACPI_MADT_NMI_SOURCE)Entry;
                    if (Entry->Length >= sizeof(*NmiSrc))
                    {
                        ULONG Index = HalpArm64GicInfo.NmiSourceCount;
                        if (Index < HALP_ARM64_MAX_NMI_SOURCES)
                        {
                            HALP_ARM64_NMI_SOURCE_ENTRY *Out =
                                &HalpArm64GicInfo.NmiSources[Index];
                            Out->GlobalSystemInterrupt = NmiSrc->GlobalSystemInterrupt;
                            Out->Polarity = (UCHAR)(NmiSrc->IntiFlags & 0x03);
                            Out->TriggerMode = (UCHAR)((NmiSrc->IntiFlags >> 2) & 0x03);
                            HalpArm64GicInfo.NmiSourceCount++;

                            DPRINT1("[arm64][ACPI] MADT: NMI Source GSI=%lu Pol=%u Trig=%u\n",
                                    Out->GlobalSystemInterrupt, Out->Polarity, Out->TriggerMode);
                        }
                    }
                    break;
                }
                case ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR:
                {
                    /*
                     * Parse ALL GICR (redistributor) entries from MADT.
                     * Multi-socket ARM64 systems may have multiple regions.
                     * This follows Linux's gic_acpi_parse_madt_redist() approach.
                     */
                    PACPI_MADT_GENERIC_REDISTRIBUTOR Redist =
                        (PACPI_MADT_GENERIC_REDISTRIBUTOR)Entry;
                    if (Entry->Length >= sizeof(*Redist))
                    {
                        ULONGLONG Base = HalpReadUnalignedU64(&Redist->BaseAddress);
                        ULONG Length = Redist->Length;
                        if (Base && Length)
                        {
                            ULONG RegIndex = HalpArm64GicInfo.GicrRegionCount;

                            /* Store in multi-region array if room available */
                            if (RegIndex < HALP_ARM64_MAX_GICR_REGIONS)
                            {
                                PHALP_ARM64_GICR_REGION Region =
                                    &HalpArm64GicInfo.GicrRegions[RegIndex];
                                Region->BaseAddress = Base;
                                Region->Length = Length;
                                HalpArm64GicInfo.GicrRegionCount++;

                                DPRINT("[arm64][ACPI] MADT: GICR region %lu @0x%llx len=0x%lx\n",
                                       RegIndex, Base, Length);
                            }
                            else
                            {
                                DPRINT1("[arm64][ACPI] Warning: too many GICR regions, max=%u\n",
                                        HALP_ARM64_MAX_GICR_REGIONS);
                            }

                            /* Maintain legacy single-region fields for compatibility */
                            if (HalpArm64GicInfo.GicrBase == 0)
                            {
                                HalpArm64GicInfo.GicrBase = Base;
                                HalpArm64GicInfo.GicrLength = Length;
                            }
                            HalpArm64GicInfo.Present = TRUE;
                        }
                    }
                    break;
                }
                case ACPI_MADT_TYPE_GENERIC_MSI_FRAME:
                {
                    PACPI_MADT_GENERIC_MSI_FRAME Frame =
                        (PACPI_MADT_GENERIC_MSI_FRAME)Entry;
                    if (Entry->Length >= sizeof(*Frame))
                    {
                        ULONG Index = HalpArm64GicInfo.MsiFrameCount;
                        if (Index < HALP_ARM64_MAX_MSI_FRAMES)
                        {
                            HALP_ARM64_GIC_MSI_FRAME_ENTRY *Out =
                                &HalpArm64GicInfo.MsiFrames[Index];
                            Out->MsiFrameId = Frame->MsiFrameId;
                            Out->BaseAddress = HalpReadUnalignedU64(&Frame->BaseAddress);
                            Out->SpiBase = Frame->SpiBase;
                            Out->SpiCount = Frame->SpiCount;
                            Out->Flags = Frame->Flags;
                            HalpArm64GicInfo.MsiFrameCount++;
                        }
                        HalpArm64GicInfo.Present = TRUE;
                    }
                    break;
                }
                case ACPI_MADT_TYPE_GENERIC_TRANSLATOR:
                {
                    PACPI_MADT_GENERIC_TRANSLATOR Its =
                        (PACPI_MADT_GENERIC_TRANSLATOR)Entry;
                    if (Entry->Length >= sizeof(*Its))
                    {
                        ULONG Index = HalpArm64GicInfo.ItsCount;
                        if (Index < HALP_ARM64_MAX_ITS_ENTRIES)
                        {
                            HALP_ARM64_GIC_ITS_ENTRY *Out =
                                &HalpArm64GicInfo.ItsEntries[Index];
                            Out->ItsId = Its->TranslationId;
                            Out->BaseAddress = HalpReadUnalignedU64(&Its->BaseAddress);
                            HalpArm64GicInfo.ItsCount++;
                        }
                        HalpArm64GicInfo.Present = TRUE;
                    }
                    break;
                }
                case ACPI_MADT_TYPE_GENERIC_INTERRUPT:
                {
                    PACPI_MADT_GENERIC_INTERRUPT Gicc =
                        (PACPI_MADT_GENERIC_INTERRUPT)Entry;
                    if (Entry->Length >= FIELD_OFFSET(ACPI_MADT_GENERIC_INTERRUPT, Mpidr) +
                        sizeof(ULONGLONG))
                    {
                        ULONG Index = HalpArm64GicInfo.GiccEntryCount;
                        if (Index < RTL_NUMBER_OF(HalpArm64GicInfo.GiccEntries))
                        {
                            HALP_ARM64_GICC_ENTRY *Out =
                                &HalpArm64GicInfo.GiccEntries[Index];
                            Out->Mpidr = HalpReadUnalignedU64(&Gicc->Mpidr);
                            Out->GicrBase = HalpReadUnalignedU64(&Gicc->GicrBaseAddress);
                            Out->GiccBase = HalpReadUnalignedU64(&Gicc->BaseAddress);
                            Out->Flags = Gicc->Flags;
                            HalpArm64GicInfo.GiccEntryCount++;
                            DPRINT1("[arm64][ACPI] MADT GICC[%lu]: MPIDR=0x%llx Flags=0x%lx GicrBase=0x%llx GiccBase=0x%llx\n",
                                    Index, Out->Mpidr, Out->Flags, Out->GicrBase, Out->GiccBase);
                        }

                        if (HalpReadUnalignedU64(&Gicc->BaseAddress))
                        {
                            HalpArm64GicInfo.GiccBase =
                                HalpReadUnalignedU64(&Gicc->BaseAddress);
                            HalpArm64GicInfo.Present = TRUE;
                        }
                    }
                    break;
                }
                default:
                    break;
            }

            Entry = (PACPI_SUBTABLE_HEADER)((ULONG_PTR)Entry + Entry->Length);
        }
    }

    if (HalpArm64GicInfo.GicdBase)
    {
        DPRINT1("[arm64][ACPI] MADT: GICD @0x%llx\n", HalpArm64GicInfo.GicdBase);
    }

    /* Log all discovered GICR regions */
    if (HalpArm64GicInfo.GicrRegionCount > 0)
    {
        ULONG RegIdx;
        DPRINT1("[arm64][ACPI] MADT: %lu GICR regions discovered\n",
                HalpArm64GicInfo.GicrRegionCount);
        for (RegIdx = 0; RegIdx < HalpArm64GicInfo.GicrRegionCount; RegIdx++)
        {
            PHALP_ARM64_GICR_REGION Region = &HalpArm64GicInfo.GicrRegions[RegIdx];
            DPRINT1("[arm64][ACPI] MADT: GICR[%lu] @0x%llx len=0x%lx\n",
                    RegIdx, Region->BaseAddress, Region->Length);
        }
    }
    else if (HalpArm64GicInfo.GicrBase)
    {
        /* Legacy fallback - single region from older parsing */
        DPRINT1("[arm64][ACPI] MADT: GICR region @0x%llx len=0x%llx (legacy)\n",
                HalpArm64GicInfo.GicrBase,
                HalpArm64GicInfo.GicrLength);
    }

    Gtdt = HalAcpiGetTable(LoaderBlock, GTDT_SIGNATURE);
    if (Gtdt && Gtdt->Header.Length >= sizeof(*Gtdt))
    {
        HalpArm64GtdtInfo.Present = TRUE;
        HalpArm64GtdtInfo.CounterBlockAddress = Gtdt->CounterBlockAddress;
        HalpArm64GtdtInfo.CounterReadBlockAddress = Gtdt->CounterReadBlockAddress;
        HalpArm64GtdtInfo.SecureEl1Interrupt = Gtdt->SecureEl1Interrupt;
        HalpArm64GtdtInfo.SecureEl1Flags = Gtdt->SecureEl1Flags;
        HalpArm64GtdtInfo.NonSecureEl1Interrupt = Gtdt->NonSecureEl1Interrupt;
        HalpArm64GtdtInfo.NonSecureEl1Flags = Gtdt->NonSecureEl1Flags;
        HalpArm64GtdtInfo.VirtualTimerInterrupt = Gtdt->VirtualTimerInterrupt;
        HalpArm64GtdtInfo.VirtualTimerFlags = Gtdt->VirtualTimerFlags;
        HalpArm64GtdtInfo.NonSecureEl2Interrupt = Gtdt->NonSecureEl2Interrupt;
        HalpArm64GtdtInfo.NonSecureEl2Flags = Gtdt->NonSecureEl2Flags;
        HalpArm64GtdtInfo.PlatformTimerCount = Gtdt->PlatformTimerCount;
        HalpArm64GtdtInfo.PlatformTimerOffset = Gtdt->PlatformTimerOffset;
    }

    Spcr = HalAcpiGetTable(LoaderBlock, SPCR_SIGNATURE);
    if (Spcr && Spcr->Header.Length >= sizeof(*Spcr))
    {
        HalpArm64SpcrInfo.Present = TRUE;
        HalpArm64SpcrInfo.InterfaceType = Spcr->InterfaceType;
        HalpArm64SpcrInfo.BaudRate = Spcr->BaudRate;
        HalpArm64SpcrInfo.Parity = Spcr->Parity;
        HalpArm64SpcrInfo.StopBits = Spcr->StopBits;
        HalpArm64SpcrInfo.FlowControl = Spcr->FlowControl;
        HalpArm64SpcrInfo.TerminalType = Spcr->TerminalType;
        HalpArm64SpcrInfo.AddressSpaceId = Spcr->SerialPort.AddressSpaceID;
        HalpArm64SpcrInfo.AccessSize = Spcr->SerialPort.AccessSize;
        HalpArm64SpcrInfo.BitWidth = Spcr->SerialPort.BitWidth;
        HalpArm64SpcrInfo.BitOffset = Spcr->SerialPort.BitOffset;
        HalpArm64SpcrInfo.Interrupt = Spcr->Interrupt;
        HalpArm64SpcrInfo.InterruptType = Spcr->InterruptType;
        HalpArm64SpcrInfo.PcInterrupt = Spcr->PcInterrupt;
        HalpArm64SpcrInfo.BaseAddress = Spcr->SerialPort.Address.QuadPart;
    }

    Fadt = HalAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
    if (Fadt)
    {
        HalpArm64PsciInfo.ArmBootArch = Fadt->arm_boot_arch;
        /* PSCI flags are in arm_boot_arch (ACPI 5.1+), NOT in the general flags field */
        HalpArm64PsciInfo.Present = (Fadt->arm_boot_arch & ACPI_FADT_PSCI_COMPLIANT) != 0;
        HalpArm64PsciInfo.UseHvc = (Fadt->arm_boot_arch & ACPI_FADT_PSCI_USE_HVC) != 0;
        DPRINT1("[arm64][ACPI] FADT: arm_boot_arch=0x%x flags=0x%lx PSCI=%s conduit=%s\n",
                Fadt->arm_boot_arch, Fadt->flags,
                HalpArm64PsciInfo.Present ? "yes" : "no",
                HalpArm64PsciInfo.UseHvc ? "HVC" : "SMC");
    }

    /*
     * Parse IORT (I/O Remapping Table) for SMMU and PCI Root Complex info.
     * This is ARM64-specific and critical for DMA/MSI configuration.
     */
    {
        PACPI_TABLE_IORT Iort;
        Iort = HalAcpiGetTable(LoaderBlock, IORT_SIGNATURE);
        if (Iort && Iort->Header.Length >= sizeof(*Iort))
        {
            ULONG_PTR TableBase = (ULONG_PTR)Iort;
            ULONG_PTR TableEnd = TableBase + Iort->Header.Length;
            ULONG_PTR NodeBase = TableBase + Iort->NodeOffset;
            ULONG NodeIndex;

            RtlZeroMemory(&HalpArm64IortInfo, sizeof(HalpArm64IortInfo));
            HalpArm64IortInfo.Present = TRUE;

            for (NodeIndex = 0; NodeIndex < Iort->NodeCount; NodeIndex++)
            {
                PACPI_IORT_NODE Node;

                if (NodeBase + sizeof(ACPI_IORT_NODE) > TableEnd)
                    break;

                Node = (PACPI_IORT_NODE)NodeBase;
                if (Node->Length < sizeof(ACPI_IORT_NODE))
                    break;
                if (NodeBase + Node->Length > TableEnd)
                    break;

                switch (Node->Type)
                {
                    case ACPI_IORT_NODE_SMMU:
                    case ACPI_IORT_NODE_SMMU_V3:
                    {
                        if (HalpArm64IortInfo.SmmuCount < HALP_ARM64_MAX_IORT_SMMU)
                        {
                            /* SMMU data follows node header */
                            PACPI_IORT_SMMU SmmuData = (PACPI_IORT_SMMU)(NodeBase + sizeof(ACPI_IORT_NODE));
                            PHALP_ARM64_IORT_SMMU_ENTRY Entry =
                                &HalpArm64IortInfo.SmmuEntries[HalpArm64IortInfo.SmmuCount];

                            if (NodeBase + sizeof(ACPI_IORT_NODE) + sizeof(ACPI_IORT_SMMU) <= TableEnd)
                            {
                                Entry->BaseAddress = HalpReadUnalignedU64(&SmmuData->BaseAddress);
                                Entry->Span = HalpReadUnalignedU64(&SmmuData->Span);
                                Entry->Model = SmmuData->Model;
                                Entry->Flags = SmmuData->Flags;
                                Entry->ContextInterruptCount = SmmuData->ContextInterruptCount;
                                Entry->PmuInterruptCount = SmmuData->PmuInterruptCount;
                                HalpArm64IortInfo.SmmuCount++;

                                DPRINT1("[arm64][ACPI] IORT: SMMU%s @0x%llx span=0x%llx\n",
                                        Node->Type == ACPI_IORT_NODE_SMMU_V3 ? "v3" : "",
                                        Entry->BaseAddress,
                                        Entry->Span);
                            }
                        }
                        break;
                    }
                    case ACPI_IORT_NODE_ROOT_COMPLEX:
                    {
                        if (HalpArm64IortInfo.RootComplexCount < HALP_ARM64_MAX_IORT_RC)
                        {
                            PACPI_IORT_ROOT_COMPLEX RcData =
                                (PACPI_IORT_ROOT_COMPLEX)(NodeBase + sizeof(ACPI_IORT_NODE));
                            PHALP_ARM64_IORT_ROOT_COMPLEX_ENTRY Entry =
                                &HalpArm64IortInfo.RootComplexEntries[HalpArm64IortInfo.RootComplexCount];

                            if (NodeBase + sizeof(ACPI_IORT_NODE) + sizeof(ACPI_IORT_ROOT_COMPLEX) <= TableEnd)
                            {
                                Entry->CacheCoherent = RcData->CacheCoherent;
                                Entry->AllocationHints = RcData->AllocationHints;
                                Entry->MemoryAccessFlags = RcData->MemoryAccessFlags;
                                Entry->AtsAttribute = (UCHAR)RcData->AtsAttribute;
                                Entry->PciSegmentNumber = RcData->PciSegmentNumber;
                                Entry->MemoryAddressLimit = RcData->MemoryAddressLimit;
                                HalpArm64IortInfo.RootComplexCount++;

                                DPRINT1("[arm64][ACPI] IORT: PCI Root Complex seg=%lu coherent=%lu\n",
                                        Entry->PciSegmentNumber,
                                        Entry->CacheCoherent);
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }

                NodeBase += Node->Length;
            }

            DPRINT1("[arm64][ACPI] IORT: %lu SMMUs, %lu Root Complexes\n",
                    HalpArm64IortInfo.SmmuCount,
                    HalpArm64IortInfo.RootComplexCount);
        }
    }

    /*
     * Parse PPTT (Processor Properties Topology Table) for CPU topology.
     * This is ARM64-specific and useful for scheduler optimizations.
     */
    {
        PACPI_TABLE_PPTT Pptt;
        Pptt = HalAcpiGetTable(LoaderBlock, PPTT_SIGNATURE);
        if (Pptt && Pptt->Header.Length >= sizeof(*Pptt))
        {
            ULONG_PTR TableBase = (ULONG_PTR)Pptt;
            ULONG_PTR TableEnd = TableBase + Pptt->Header.Length;
            ULONG_PTR SubtableBase = TableBase + sizeof(ACPI_TABLE_PPTT);

            RtlZeroMemory(&HalpArm64PpttInfo, sizeof(HalpArm64PpttInfo));
            HalpArm64PpttInfo.Present = TRUE;

            while (SubtableBase + sizeof(ACPI_PPTT_SUBTABLE) <= TableEnd)
            {
                PACPI_PPTT_SUBTABLE Subtable = (PACPI_PPTT_SUBTABLE)SubtableBase;

                if (Subtable->Length < sizeof(ACPI_PPTT_SUBTABLE))
                    break;
                if (SubtableBase + Subtable->Length > TableEnd)
                    break;

                switch (Subtable->Type)
                {
                    case ACPI_PPTT_TYPE_PROCESSOR:
                    {
                        if (HalpArm64PpttInfo.ProcessorCount < HALP_ARM64_MAX_PPTT_PROCESSORS)
                        {
                            PACPI_PPTT_PROCESSOR Proc = (PACPI_PPTT_PROCESSOR)Subtable;
                            PHALP_ARM64_PPTT_PROCESSOR_ENTRY Entry =
                                &HalpArm64PpttInfo.Processors[HalpArm64PpttInfo.ProcessorCount];

                            if (Subtable->Length >= sizeof(ACPI_PPTT_PROCESSOR))
                            {
                                Entry->Flags = Proc->Flags;
                                Entry->Parent = Proc->Parent;
                                Entry->AcpiProcessorId = Proc->AcpiProcessorId;
                                Entry->NumberOfCaches = 0;

                                /* Copy first few cache references if present */
                                if (Proc->NumberOfPrivResources > 0 &&
                                    Subtable->Length >= sizeof(ACPI_PPTT_PROCESSOR) + sizeof(ULONG))
                                {
                                    PULONG CacheRefs = (PULONG)(Proc + 1);
                                    ULONG RefCount = min(Proc->NumberOfPrivResources, 4);
                                    ULONG RefIndex;

                                    Entry->NumberOfCaches = RefCount;
                                    for (RefIndex = 0; RefIndex < RefCount; RefIndex++)
                                    {
                                        Entry->CacheRefs[RefIndex] = CacheRefs[RefIndex];
                                    }
                                }
                                HalpArm64PpttInfo.ProcessorCount++;
                            }
                        }
                        break;
                    }
                    case ACPI_PPTT_TYPE_CACHE:
                    {
                        if (HalpArm64PpttInfo.CacheCount < HALP_ARM64_MAX_PPTT_CACHES)
                        {
                            PACPI_PPTT_CACHE Cache = (PACPI_PPTT_CACHE)Subtable;
                            PHALP_ARM64_PPTT_CACHE_ENTRY Entry =
                                &HalpArm64PpttInfo.Caches[HalpArm64PpttInfo.CacheCount];

                            if (Subtable->Length >= sizeof(ACPI_PPTT_CACHE))
                            {
                                Entry->Flags = Cache->Flags;
                                Entry->NextLevelCache = Cache->NextLevelCache;
                                Entry->Size = Cache->Size;
                                Entry->NumberOfSets = Cache->NumberOfSets;
                                Entry->Associativity = Cache->Associativity;
                                Entry->Attributes = Cache->Attributes;
                                Entry->LineSize = Cache->LineSize;
                                HalpArm64PpttInfo.CacheCount++;
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }

                SubtableBase += Subtable->Length;
            }

            DPRINT1("[arm64][ACPI] PPTT: %lu processors, %lu caches\n",
                    HalpArm64PpttInfo.ProcessorCount,
                    HalpArm64PpttInfo.CacheCount);
        }
    }
}

/*
 * HalpArm64TranslateGsiToIntId
 *
 * Translates a Global System Interrupt (GSI) number from ACPI to a GIC INTID.
 * This function also applies any interrupt source overrides from the MADT.
 *
 * On ARM64 with GIC, the GSI to INTID mapping is typically direct for SPIs,
 * but interrupt source overrides can redirect source IRQs to different GSIs.
 *
 * GIC INTID ranges:
 *   0-15:     SGIs (Software Generated Interrupts)
 *   16-31:    PPIs (Private Peripheral Interrupts)
 *   32-1019:  SPIs (Shared Peripheral Interrupts)
 *   1020-1023: Special INTIDs (spurious, etc.)
 *   8192+:    LPIs (Locality-specific Peripheral Interrupts for MSI)
 */
ULONG
NTAPI
HalpArm64TranslateGsiToIntId(
    _In_ ULONG Gsi,
    _Out_opt_ PUCHAR Polarity,
    _Out_opt_ PUCHAR TriggerMode)
{
    ULONG Index;
    ULONG IntId;
    UCHAR Pol = 0;   /* 0 = conforms to bus spec (default active-high for GIC) */
    UCHAR Trig = 0;  /* 0 = conforms to bus spec (default level for SPIs) */

    /*
     * First, check if there's an interrupt source override that maps
     * a different source IRQ to this GSI. We need to find any override
     * that has this GSI as the destination and return appropriate settings.
     */
    for (Index = 0; Index < HalpArm64GicInfo.IntOverrideCount; Index++)
    {
        PHALP_ARM64_INT_OVERRIDE_ENTRY Entry = &HalpArm64GicInfo.IntOverrides[Index];

        if (Entry->GlobalSystemInterrupt == Gsi)
        {
            /* Found an override targeting this GSI */
            Pol = Entry->Polarity;
            Trig = Entry->TriggerMode;
            break;
        }
    }

    /*
     * Translate GSI to GIC INTID.
     *
     * For ARM64 systems with ACPI:
     * - The MADT GICD entry contains SystemVectorBase, which is the first
     *   SPI number (typically 0, meaning SPIs start at INTID 32).
     * - GSIs for SPIs are directly equivalent to GIC INTIDs in most systems.
     * - PPIs (16-31) and SGIs (0-15) are per-CPU and less commonly
     *   referenced in ACPI tables, but we handle them for completeness.
     */

    /* SGI range (0-15) - Software Generated Interrupts, used for IPIs */
    if (Gsi < 16)
    {
        IntId = Gsi;
        /* SGIs are edge-triggered by default */
        if (Trig == 0) Trig = 1; /* Edge-triggered */
    }
    /* PPI range (16-31) - Private Peripheral Interrupts per CPU */
    else if (Gsi < 32)
    {
        IntId = Gsi;
        /* PPIs can be edge or level, default depends on specific interrupt */
    }
    /* SPI range (32-1019) - Shared Peripheral Interrupts */
    else if (Gsi < 1020)
    {
        /*
         * SPIs: Add SystemVectorBase offset if non-zero.
         * Most ACPI systems use SystemVectorBase=0, making GSI = INTID.
         */
        IntId = Gsi + HalpArm64GicInfo.SystemVectorBase;

        /* Validate SPI is in valid range */
        if (IntId >= 1020)
        {
            DPRINT1("[arm64] HalpArm64TranslateGsiToIntId: GSI %lu maps to invalid INTID %lu\n",
                    Gsi, IntId);
            return (ULONG)-1;
        }

        /* SPIs default to level-triggered, active-high */
        if (Trig == 0) Trig = 3; /* Level-triggered */
    }
    /* Reserved range (1020-1023) */
    else if (Gsi < 1024)
    {
        /* Reserved INTIDs including spurious interrupt (1023) */
        DPRINT1("[arm64] HalpArm64TranslateGsiToIntId: GSI %lu is in reserved range\n", Gsi);
        return (ULONG)-1;
    }
    /* Gap between reserved and LPIs (1024-8191) */
    else if (Gsi < 8192)
    {
        DPRINT1("[arm64] HalpArm64TranslateGsiToIntId: GSI %lu is in invalid gap\n", Gsi);
        return (ULONG)-1;
    }
    /* LPI range (8192+) - Locality-specific Peripheral Interrupts for MSI */
    else
    {
        IntId = Gsi;
        /* LPIs are always edge-triggered */
        Trig = 1;
    }

    /* Return polarity and trigger mode if requested */
    if (Polarity)
        *Polarity = Pol;
    if (TriggerMode)
        *TriggerMode = Trig;

    return IntId;
}

/*
 * HalpArm64GetIntOverrideForIrq
 *
 * Searches the interrupt override table for an entry that remaps
 * the specified source IRQ on the given bus to a different GSI.
 */
PHALP_ARM64_INT_OVERRIDE_ENTRY
NTAPI
HalpArm64GetIntOverrideForIrq(
    _In_ UCHAR Bus,
    _In_ UCHAR SourceIrq)
{
    ULONG Index;

    for (Index = 0; Index < HalpArm64GicInfo.IntOverrideCount; Index++)
    {
        PHALP_ARM64_INT_OVERRIDE_ENTRY Entry = &HalpArm64GicInfo.IntOverrides[Index];

        if (Entry->Bus == Bus && Entry->SourceIrq == SourceIrq)
        {
            return Entry;
        }
    }

    return NULL;
}

/*
 * HalpArm64IsNmiSource
 *
 * Checks if the specified GSI is configured as an NMI source.
 * On ARM64 with GIC, NMIs would be configured with priority 0.
 */
BOOLEAN
NTAPI
HalpArm64IsNmiSource(
    _In_ ULONG Gsi)
{
    ULONG Index;

    for (Index = 0; Index < HalpArm64GicInfo.NmiSourceCount; Index++)
    {
        if (HalpArm64GicInfo.NmiSources[Index].GlobalSystemInterrupt == Gsi)
        {
            return TRUE;
        }
    }

    return FALSE;
}
