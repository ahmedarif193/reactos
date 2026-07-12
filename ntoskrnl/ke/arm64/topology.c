/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/topology.c
 * PURPOSE:         ARM64 CPU, NUMA and SMT topology discovery
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/arm64/acpi.h>

#define NDEBUG
#include <debug.h>

#define KI_ARM64_ACPI_ENABLED         0x1
#define KI_ARM64_SRAT_TYPE_MEMORY     1
#define KI_ARM64_SRAT_TYPE_GICC       3
#define KI_ARM64_SRAT_ENABLED         0x1
#define KI_ARM64_INVALID_NODE         0xFF
#define KI_ARM64_SRAT_HEADER_SIZE     (sizeof(DESCRIPTION_HEADER) + sizeof(ULONG) + sizeof(ULONGLONG))
#define KI_ARM64_SYSTEM_RANGE_BASE    0xFFFF800000000000ULL
#define KI_ARM64_MPIDR_MT             (1ULL << 24)
#define KI_ARM64_MPIDR_AFF_ALL        0xFF00FFFFFFULL
#define KI_ARM64_MPIDR_CORE_MT        0xFF00FFFF00ULL
#define KI_ARM64_MPIDR_PACKAGE        0xFF00FF0000ULL
#define KI_ARM64_NUMA_MM_MAX_NODES    1

#include <pshpack1.h>
typedef struct _KI_ARM64_SRAT_SUBTABLE
{
    UCHAR Type;
    UCHAR Length;
} KI_ARM64_SRAT_SUBTABLE, *PKI_ARM64_SRAT_SUBTABLE;

typedef struct _KI_ARM64_SRAT_GICC_AFFINITY
{
    KI_ARM64_SRAT_SUBTABLE Header;
    ULONG ProximityDomain;
    ULONG AcpiProcessorUid;
    ULONG Flags;
    ULONG ClockDomain;
} KI_ARM64_SRAT_GICC_AFFINITY, *PKI_ARM64_SRAT_GICC_AFFINITY;

typedef struct _KI_ARM64_SRAT_MEMORY_AFFINITY
{
    KI_ARM64_SRAT_SUBTABLE Header;
    ULONG ProximityDomain;
    USHORT Reserved1;
    ULONGLONG BaseAddress;
    ULONGLONG Length;
    ULONG Reserved2;
    ULONG Flags;
    ULONGLONG Reserved3;
} KI_ARM64_SRAT_MEMORY_AFFINITY, *PKI_ARM64_SRAT_MEMORY_AFFINITY;
#include <poppack.h>

typedef struct _KI_ARM64_CPU_NUMA_TOPOLOGY
{
    BOOLEAN Present;
    BOOLEAN HasSratNode;
    BOOLEAN HasMpidr;
    ULONG AcpiProcessorUid;
    ULONG ProximityDomain;
    UCHAR NodeNumber;
    ULONGLONG Mpidr;
} KI_ARM64_CPU_NUMA_TOPOLOGY, *PKI_ARM64_CPU_NUMA_TOPOLOGY;

typedef struct _KI_ARM64_NODE_DOMAIN
{
    BOOLEAN Present;
    ULONG ProximityDomain;
} KI_ARM64_NODE_DOMAIN, *PKI_ARM64_NODE_DOMAIN;

extern KNODE KiArm64NodeBlock[MAXIMUM_PROCESSORS - 1];

KI_ARM64_NUMA_NODE_RANGE KiArm64NumaNodeRanges[KI_MAX_NUMA_NODES];
UCHAR KiArm64NumaNodeCount = 1;
BOOLEAN KiArm64SmtFinalized = FALSE;

static BOOLEAN KiArm64NumaDiscovered;
static BOOLEAN KiArm64NumaDefaultInitialized;
static BOOLEAN KiArm64AcpiIdentityMapped;
static BOOLEAN KiArm64NodeStorageInitialized;
static KI_ARM64_CPU_NUMA_TOPOLOGY KiArm64CpuNumaTopology[MAXIMUM_PROCESSORS];
static KI_ARM64_NODE_DOMAIN KiArm64NodeDomains[KI_MAX_NUMA_NODES];

static
PVOID
KiArm64AcpiPointerFromPhysical(_In_ ULONGLONG PhysicalAddress)
{
    if (PhysicalAddress == 0)
    {
        return NULL;
    }

    if (PhysicalAddress >= KI_ARM64_SYSTEM_RANGE_BASE)
    {
        return (PVOID)(ULONG_PTR)PhysicalAddress;
    }

    if (KiArm64AcpiIdentityMapped)
    {
        return (PVOID)(ULONG_PTR)PhysicalAddress;
    }

    return NULL;
}

static
ULONGLONG
KiArm64ReadUnaligned64(_In_ const VOID *Source)
{
    ULONGLONG Value;

    RtlCopyMemory(&Value, Source, sizeof(Value));
    return Value;
}

static
VOID
KiArm64EnsureNodeStorage(VOID)
{
    ULONG Node;

    KeNodeBlock[0] = &KiNode0;

    for (Node = 1; Node < KI_MAX_NUMA_NODES; Node++)
    {
        KeNodeBlock[Node] = &KiArm64NodeBlock[Node - 1];
    }

    if (!KiArm64NodeStorageInitialized)
    {
        RtlZeroMemory(&KiNode0, sizeof(KiNode0));
        RtlZeroMemory(KiArm64NodeBlock, sizeof(KiArm64NodeBlock));
        KiArm64NodeStorageInitialized = TRUE;
    }
}

static
VOID
KiArm64ResetNodeProcessorMasks(_In_ UCHAR NodeCount)
{
    ULONG Node;

    KiArm64EnsureNodeStorage();

    if (NodeCount == 0)
    {
        NodeCount = 1;
    }

    for (Node = 0; Node < NodeCount; Node++)
    {
        KeNodeBlock[Node]->NodeNumber = (UCHAR)Node;
        KeNodeBlock[Node]->ProcessorMask = 0;
    }
}

static
PACPI_BIOS_MULTI_NODE
KiArm64FindAcpiNode(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PCONFIGURATION_COMPONENT_DATA ComponentEntry;
    PCONFIGURATION_COMPONENT_DATA Next = NULL;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;

    if ((LoaderBlock == NULL) || (LoaderBlock->ConfigurationRoot == NULL))
    {
        return NULL;
    }

    ComponentEntry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot,
                                                  AdapterClass,
                                                  MultiFunctionAdapter,
                                                  NULL,
                                                  &Next);
    while (ComponentEntry != NULL)
    {
        if ((ComponentEntry->ComponentEntry.Identifier != NULL) &&
            (_stricmp(ComponentEntry->ComponentEntry.Identifier, "ACPI BIOS") == 0))
        {
            break;
        }

        Next = ComponentEntry;
        ComponentEntry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot,
                                                      AdapterClass,
                                                      MultiFunctionAdapter,
                                                      NULL,
                                                      &Next);
    }

    if ((ComponentEntry == NULL) || (ComponentEntry->ConfigurationData == NULL))
    {
        return NULL;
    }

    ResourceList = ComponentEntry->ConfigurationData;
    return (PACPI_BIOS_MULTI_NODE)(ResourceList + 1);
}

static
PRSDP
KiArm64FindRsdp(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_BIOS_MULTI_NODE AcpiNode;
    PRSDP Rsdp;

    AcpiNode = KiArm64FindAcpiNode(LoaderBlock);
    if ((AcpiNode == NULL) || (AcpiNode->RsdpAddress.QuadPart == 0))
    {
        return NULL;
    }

    Rsdp = (PRSDP)KiArm64AcpiPointerFromPhysical(AcpiNode->RsdpAddress.QuadPart);
    if ((Rsdp == NULL) || (Rsdp->Signature != RSDP_SIGNATURE))
    {
        return NULL;
    }

    return Rsdp;
}

static
PDESCRIPTION_HEADER
KiArm64FindAcpiTable(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
                     _In_ ULONG Signature)
{
    PRSDP Rsdp;
    PDESCRIPTION_HEADER Root;
    ULONGLONG RootPa = 0;
    ULONG Count;
    ULONG Index;

    Rsdp = KiArm64FindRsdp(LoaderBlock);
    if (Rsdp != NULL)
    {
        if ((Rsdp->Revision >= 2) && (Rsdp->XsdtAddress.QuadPart != 0))
        {
            RootPa = Rsdp->XsdtAddress.QuadPart;
        }
        else
        {
            RootPa = Rsdp->RsdtAddress;
        }
    }

    if (RootPa == 0)
    {
        PACPI_BIOS_MULTI_NODE AcpiNode = KiArm64FindAcpiNode(LoaderBlock);
        if (AcpiNode != NULL)
        {
            RootPa = AcpiNode->RsdtAddress.QuadPart;
        }
    }

    Root = (PDESCRIPTION_HEADER)KiArm64AcpiPointerFromPhysical(RootPa);
    if ((Root == NULL) || (Root->Length < sizeof(DESCRIPTION_HEADER)))
    {
        return NULL;
    }

    if (Root->Signature == XSDT_SIGNATURE)
    {
        PXSDT Xsdt = (PXSDT)Root;

        Count = (Root->Length - sizeof(DESCRIPTION_HEADER)) / sizeof(PHYSICAL_ADDRESS);
        for (Index = 0; Index < Count; Index++)
        {
            ULONGLONG TablePa = KiArm64ReadUnaligned64(&Xsdt->Tables[Index]);
            PDESCRIPTION_HEADER Header =
                (PDESCRIPTION_HEADER)KiArm64AcpiPointerFromPhysical(TablePa);

            if ((Header != NULL) &&
                (Header->Length >= sizeof(DESCRIPTION_HEADER)) &&
                (Header->Signature == Signature))
            {
                return Header;
            }
        }
    }
    else if (Root->Signature == RSDT_SIGNATURE)
    {
        PRSDT Rsdt = (PRSDT)Root;

        Count = (Root->Length - sizeof(DESCRIPTION_HEADER)) / sizeof(ULONG);
        for (Index = 0; Index < Count; Index++)
        {
            PDESCRIPTION_HEADER Header =
                (PDESCRIPTION_HEADER)KiArm64AcpiPointerFromPhysical(Rsdt->Tables[Index]);

            if ((Header != NULL) &&
                (Header->Length >= sizeof(DESCRIPTION_HEADER)) &&
                (Header->Signature == Signature))
            {
                return Header;
            }
        }
    }

    return NULL;
}

static
VOID
KiArm64InitializeDefaultNumaTopology(VOID)
{
    ULONG Cpu;

    RtlZeroMemory(KiArm64CpuNumaTopology, sizeof(KiArm64CpuNumaTopology));
    RtlZeroMemory(KiArm64NodeDomains, sizeof(KiArm64NodeDomains));
    RtlZeroMemory(KiArm64NumaNodeRanges, sizeof(KiArm64NumaNodeRanges));

    KiArm64NumaNodeCount = 1;
    KeNumberNodes = 1;
    KiArm64NodeDomains[0].Present = TRUE;
    KiArm64NodeDomains[0].ProximityDomain = 0;
    KiArm64ResetNodeProcessorMasks(1);

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
    {
        KiArm64CpuNumaTopology[Cpu].Present = TRUE;
        KiArm64CpuNumaTopology[Cpu].AcpiProcessorUid = Cpu;
        KiArm64CpuNumaTopology[Cpu].NodeNumber = 0;
        KiArm64CpuNumaTopology[Cpu].ProximityDomain = 0;
    }

    KiArm64NumaDefaultInitialized = TRUE;
}

static
VOID
KiArm64EnsureDefaultNumaTopology(VOID)
{
    if (!KiArm64NumaDefaultInitialized)
    {
        KiArm64InitializeDefaultNumaTopology();
    }
}

static
BOOLEAN
KiArm64FindCpuByUid(_In_ ULONG AcpiProcessorUid,
                    _Out_ PULONG CpuNumber)
{
    ULONG Cpu;

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
    {
        if (KiArm64CpuNumaTopology[Cpu].Present &&
            (KiArm64CpuNumaTopology[Cpu].AcpiProcessorUid == AcpiProcessorUid))
        {
            *CpuNumber = Cpu;
            return TRUE;
        }
    }

    return FALSE;
}

static
UCHAR
KiArm64CreateNodeForDomain(_In_ ULONG ProximityDomain)
{
    UCHAR Node;

    for (Node = 0; Node < KiArm64NumaNodeCount; Node++)
    {
        if (KiArm64NodeDomains[Node].Present &&
            (KiArm64NodeDomains[Node].ProximityDomain == ProximityDomain))
        {
            return Node;
        }
    }

    if (KiArm64NumaNodeCount >= KI_MAX_NUMA_NODES)
    {
        return 0;
    }

    Node = KiArm64NumaNodeCount++;
    KiArm64NodeDomains[Node].Present = TRUE;
    KiArm64NodeDomains[Node].ProximityDomain = ProximityDomain;
    KeNodeBlock[Node]->NodeNumber = Node;
    KeNodeBlock[Node]->ProcessorMask = 0;
    return Node;
}

static
BOOLEAN
KiArm64FindNodeForDomain(_In_ ULONG ProximityDomain,
                         _Out_ PUCHAR NodeNumber)
{
    UCHAR Node;

    for (Node = 0; Node < KiArm64NumaNodeCount; Node++)
    {
        if (KiArm64NodeDomains[Node].Present &&
            (KiArm64NodeDomains[Node].ProximityDomain == ProximityDomain))
        {
            *NodeNumber = Node;
            return TRUE;
        }
    }

    return FALSE;
}

static
VOID
KiArm64UpdateNodeRange(_In_ UCHAR Node,
                       _In_ ULONG ProximityDomain,
                       _In_ ULONGLONG BaseAddress,
                       _In_ ULONGLONG Length)
{
    PKI_ARM64_NUMA_NODE_RANGE Range;
    ULONGLONG LimitAddress;

    if ((Node >= KI_MAX_NUMA_NODES) || (Length == 0))
    {
        return;
    }

    if (BaseAddress > MAXULONGLONG - Length)
    {
        LimitAddress = MAXULONGLONG;
    }
    else
    {
        LimitAddress = BaseAddress + Length;
    }

    if (LimitAddress <= BaseAddress)
    {
        return;
    }

    Range = &KiArm64NumaNodeRanges[Node];
    if (!Range->Enabled)
    {
        Range->BaseAddress = BaseAddress;
        Range->LimitAddress = LimitAddress;
        Range->ProximityDomain = ProximityDomain;
        Range->Enabled = TRUE;
    }
    else
    {
        if (BaseAddress < Range->BaseAddress)
        {
            Range->BaseAddress = BaseAddress;
        }

        if (LimitAddress > Range->LimitAddress)
        {
            Range->LimitAddress = LimitAddress;
        }
    }

    Range->Length = Range->LimitAddress - Range->BaseAddress;
}

static
VOID
KiArm64ParseMadtCpuUids(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PARM64_ACPI_MADT Madt;
    ULONG_PTR EntryAddress;
    ULONG_PTR TableEnd;
    ULONG LogicalCpu = 0;

    Madt = (PARM64_ACPI_MADT)KiArm64FindAcpiTable(LoaderBlock, APIC_SIGNATURE);
    if ((Madt == NULL) || (Madt->Header.Length < sizeof(*Madt)))
    {
        return;
    }

    RtlZeroMemory(KiArm64CpuNumaTopology, sizeof(KiArm64CpuNumaTopology));

    EntryAddress = (ULONG_PTR)Madt + sizeof(*Madt);
    TableEnd = (ULONG_PTR)Madt + Madt->Header.Length;

    while (EntryAddress + sizeof(ARM64_ACPI_MADT_SUBTABLE) <= TableEnd)
    {
        PARM64_ACPI_MADT_SUBTABLE Entry = (PARM64_ACPI_MADT_SUBTABLE)EntryAddress;

        if ((Entry->Length < sizeof(*Entry)) || (EntryAddress + Entry->Length > TableEnd))
        {
            break;
        }

        if ((Entry->Type == ARM64_ACPI_MADT_TYPE_GENERIC_INTERRUPT) &&
            (Entry->Length >= FIELD_OFFSET(ARM64_ACPI_MADT_GENERIC_INTERRUPT, AcpiProcessorUid) +
                              sizeof(ULONG)) &&
            (LogicalCpu < MAXIMUM_PROCESSORS))
        {
            PARM64_ACPI_MADT_GENERIC_INTERRUPT Gicc =
                (PARM64_ACPI_MADT_GENERIC_INTERRUPT)Entry;

            if (Gicc->Flags & KI_ARM64_ACPI_ENABLED)
            {
                KiArm64CpuNumaTopology[LogicalCpu].Present = TRUE;
                KiArm64CpuNumaTopology[LogicalCpu].AcpiProcessorUid = Gicc->AcpiProcessorUid;
                KiArm64CpuNumaTopology[LogicalCpu].NodeNumber = 0;
                KiArm64CpuNumaTopology[LogicalCpu].ProximityDomain = 0;

                if (Entry->Length >= FIELD_OFFSET(ARM64_ACPI_MADT_GENERIC_INTERRUPT, Mpidr) +
                                     sizeof(ULONGLONG))
                {
                    KiArm64CpuNumaTopology[LogicalCpu].Mpidr = KiArm64ReadUnaligned64(&Gicc->Mpidr);
                    KiArm64CpuNumaTopology[LogicalCpu].HasMpidr = TRUE;
                }

                LogicalCpu++;
            }
        }

        EntryAddress += Entry->Length;
    }
}

static
VOID
KiArm64ParseSratCpuAffinity(_In_ PDESCRIPTION_HEADER Srat)
{
    ULONG_PTR EntryAddress;
    ULONG_PTR TableEnd;

    KiArm64NumaNodeCount = 0;
    RtlZeroMemory(KiArm64NodeDomains, sizeof(KiArm64NodeDomains));
    RtlZeroMemory(KiArm64NumaNodeRanges, sizeof(KiArm64NumaNodeRanges));

    EntryAddress = (ULONG_PTR)Srat + KI_ARM64_SRAT_HEADER_SIZE;
    TableEnd = (ULONG_PTR)Srat + Srat->Length;

    while (EntryAddress + sizeof(KI_ARM64_SRAT_SUBTABLE) <= TableEnd)
    {
        PKI_ARM64_SRAT_SUBTABLE Entry = (PKI_ARM64_SRAT_SUBTABLE)EntryAddress;

        if ((Entry->Length < sizeof(*Entry)) || (EntryAddress + Entry->Length > TableEnd))
        {
            break;
        }

        if ((Entry->Type == KI_ARM64_SRAT_TYPE_GICC) &&
            (Entry->Length >= sizeof(KI_ARM64_SRAT_GICC_AFFINITY)))
        {
            PKI_ARM64_SRAT_GICC_AFFINITY Gicc =
                (PKI_ARM64_SRAT_GICC_AFFINITY)Entry;
            ULONG CpuNumber;

            if ((Gicc->Flags & KI_ARM64_SRAT_ENABLED) &&
                KiArm64FindCpuByUid(Gicc->AcpiProcessorUid, &CpuNumber))
            {
                UCHAR Node = KiArm64CreateNodeForDomain(Gicc->ProximityDomain);

                KiArm64CpuNumaTopology[CpuNumber].NodeNumber = Node;
                KiArm64CpuNumaTopology[CpuNumber].ProximityDomain = Gicc->ProximityDomain;
                KiArm64CpuNumaTopology[CpuNumber].HasSratNode = TRUE;
            }
        }

        EntryAddress += Entry->Length;
    }

    if (KiArm64NumaNodeCount == 0)
    {
        KiArm64NumaNodeCount = 1;
        KiArm64NodeDomains[0].Present = TRUE;
        KiArm64NodeDomains[0].ProximityDomain = 0;
    }
}

static
VOID
KiArm64ParseSratMemoryAffinity(_In_ PDESCRIPTION_HEADER Srat)
{
    ULONG_PTR EntryAddress;
    ULONG_PTR TableEnd;

    EntryAddress = (ULONG_PTR)Srat + KI_ARM64_SRAT_HEADER_SIZE;
    TableEnd = (ULONG_PTR)Srat + Srat->Length;

    while (EntryAddress + sizeof(KI_ARM64_SRAT_SUBTABLE) <= TableEnd)
    {
        PKI_ARM64_SRAT_SUBTABLE Entry = (PKI_ARM64_SRAT_SUBTABLE)EntryAddress;

        if ((Entry->Length < sizeof(*Entry)) || (EntryAddress + Entry->Length > TableEnd))
        {
            break;
        }

        if ((Entry->Type == KI_ARM64_SRAT_TYPE_MEMORY) &&
            (Entry->Length >= sizeof(KI_ARM64_SRAT_MEMORY_AFFINITY)))
        {
            PKI_ARM64_SRAT_MEMORY_AFFINITY Memory =
                (PKI_ARM64_SRAT_MEMORY_AFFINITY)Entry;
            UCHAR Node;

            if ((Memory->Flags & KI_ARM64_SRAT_ENABLED) &&
                KiArm64FindNodeForDomain(Memory->ProximityDomain, &Node))
            {
                KiArm64UpdateNodeRange(Node,
                                       Memory->ProximityDomain,
                                       Memory->BaseAddress,
                                       Memory->Length);
            }
        }

        EntryAddress += Entry->Length;
    }
}

static
VOID
KiArm64ParseSrat(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PDESCRIPTION_HEADER Srat;

    Srat = KiArm64FindAcpiTable(LoaderBlock, SRAT_SIGNATURE);
    if ((Srat == NULL) ||
        (Srat->Length < KI_ARM64_SRAT_HEADER_SIZE))
    {
        KiArm64NumaNodeCount = 1;
        KeNumberNodes = 1;
        KiArm64NodeDomains[0].Present = TRUE;
        KiArm64NodeDomains[0].ProximityDomain = 0;
        return;
    }

    KiArm64ParseSratCpuAffinity(Srat);
    KiArm64ParseSratMemoryAffinity(Srat);

    if (KiArm64NumaNodeCount == 0)
    {
        KiArm64NumaNodeCount = 1;
    }

    KeNumberNodes = (KiArm64NumaNodeCount < KI_ARM64_NUMA_MM_MAX_NODES) ?
                    KiArm64NumaNodeCount : KI_ARM64_NUMA_MM_MAX_NODES;
    KiArm64ResetNodeProcessorMasks(KeNumberNodes);
}

static
VOID
KiArm64SeedActiveProcessorNodeMasks(VOID)
{
    KAFFINITY ActiveMask = KeActiveProcessors;
    ULONG Cpu;

    if (ActiveMask == 0)
    {
        ActiveMask = (KAFFINITY)1;
    }

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
    {
        UCHAR Node;

        if ((ActiveMask & AFFINITY_MASK(Cpu)) == 0)
        {
            continue;
        }

        Node = KiArm64CpuNumaTopology[Cpu].NodeNumber;
        if ((Node >= KeNumberNodes) || (KeNodeBlock[Node] == NULL))
        {
            Node = 0;
        }

        KeNodeBlock[Node]->ProcessorMask |= AFFINITY_MASK(Cpu);
    }
}

VOID
NTAPI
KiArm64DiscoverNumaTopology(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (KiArm64NumaDiscovered)
    {
        return;
    }

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        KiArm64EnsureDefaultNumaTopology();
        KiArm64SeedActiveProcessorNodeMasks();
        return;
    }

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    KiArm64AcpiIdentityMapped = TRUE;
    KiArm64InitializeDefaultNumaTopology();
    KiArm64ParseMadtCpuUids(LoaderBlock);
    KiArm64ParseSrat(LoaderBlock);
    KiArm64AcpiIdentityMapped = FALSE;
    KiArm64SeedActiveProcessorNodeMasks();
    KiArm64NumaDiscovered = TRUE;
}

static
VOID
KiArm64MergeNodeRange(_Inout_ PKI_ARM64_NUMA_NODE_RANGE Destination,
                      _In_ const KI_ARM64_NUMA_NODE_RANGE *Source)
{
    if (!Source->Enabled)
    {
        return;
    }

    if (!Destination->Enabled)
    {
        *Destination = *Source;
        return;
    }

    if (Source->BaseAddress < Destination->BaseAddress)
    {
        Destination->BaseAddress = Source->BaseAddress;
    }

    if (Source->LimitAddress > Destination->LimitAddress)
    {
        Destination->LimitAddress = Source->LimitAddress;
    }

    Destination->Length = Destination->LimitAddress - Destination->BaseAddress;
}

static
VOID
KiArm64CompactActiveNumaNodes(_In_ KAFFINITY ActiveMask)
{
    UCHAR OldToNew[KI_MAX_NUMA_NODES];
    KI_ARM64_NUMA_NODE_RANGE OldRanges[KI_MAX_NUMA_NODES];
    KI_ARM64_NUMA_NODE_RANGE NewRanges[KI_MAX_NUMA_NODES];
    KI_ARM64_NODE_DOMAIN OldDomains[KI_MAX_NUMA_NODES];
    KI_ARM64_NODE_DOMAIN NewDomains[KI_MAX_NUMA_NODES];
    ULONG Cpu;
    ULONG Node;
    UCHAR NewNodeCount = 0;

    RtlFillMemory(OldToNew, sizeof(OldToNew), KI_ARM64_INVALID_NODE);
    RtlCopyMemory(OldRanges, KiArm64NumaNodeRanges, sizeof(OldRanges));
    RtlCopyMemory(OldDomains, KiArm64NodeDomains, sizeof(OldDomains));
    RtlZeroMemory(NewRanges, sizeof(NewRanges));
    RtlZeroMemory(NewDomains, sizeof(NewDomains));

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
    {
        UCHAR OldNode;

        if (!KiArm64CpuNumaTopology[Cpu].Present ||
            ((ActiveMask & AFFINITY_MASK(Cpu)) == 0))
        {
            continue;
        }

        OldNode = KiArm64CpuNumaTopology[Cpu].NodeNumber;
        if (OldNode >= KI_MAX_NUMA_NODES)
        {
            OldNode = 0;
        }

        if (OldToNew[OldNode] == KI_ARM64_INVALID_NODE)
        {
            OldToNew[OldNode] = NewNodeCount++;
        }

        KiArm64CpuNumaTopology[Cpu].NodeNumber = OldToNew[OldNode];
    }

    if (NewNodeCount == 0)
    {
        NewNodeCount = 1;
        OldToNew[0] = 0;
    }

    for (Node = 0; Node < KI_MAX_NUMA_NODES; Node++)
    {
        if (OldToNew[Node] != KI_ARM64_INVALID_NODE)
        {
            KiArm64MergeNodeRange(&NewRanges[OldToNew[Node]], &OldRanges[Node]);
            NewDomains[OldToNew[Node]] = OldDomains[Node];
        }
    }

    RtlCopyMemory(KiArm64NumaNodeRanges, NewRanges, sizeof(KiArm64NumaNodeRanges));
    RtlCopyMemory(KiArm64NodeDomains, NewDomains, sizeof(KiArm64NodeDomains));

    KiArm64NumaNodeCount = NewNodeCount;
    KeNumberNodes = (NewNodeCount < KI_ARM64_NUMA_MM_MAX_NODES) ?
                    NewNodeCount : KI_ARM64_NUMA_MM_MAX_NODES;

    KiArm64EnsureNodeStorage();
    for (Node = 0; Node < KeNumberNodes; Node++)
    {
        KeNodeBlock[Node]->NodeNumber = (UCHAR)Node;
    }
}

VOID
NTAPI
KiArm64InitializeProcessorNumaTopology(_In_ ULONG ProcessorNumber,
                                       _Inout_ PKPRCB Prcb,
                                       _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UCHAR NodeNumber;

    KiArm64EnsureDefaultNumaTopology();

    if ((Prcb == NULL) || (ProcessorNumber >= MAXIMUM_PROCESSORS))
    {
        return;
    }

    NodeNumber = KiArm64CpuNumaTopology[ProcessorNumber].NodeNumber;
    if ((NodeNumber >= KeNumberNodes) || (KeNodeBlock[NodeNumber] == NULL))
    {
        NodeNumber = 0;
    }

    Prcb->ParentNode = KeNodeBlock[NodeNumber];
    Prcb->ParentNode->ProcessorMask |= Prcb->SetMember;
}

VOID
NTAPI
KiArm64FinalizeNumaTopology(VOID)
{
    KAFFINITY ActiveMask = KeActiveProcessors;
    ULONG Cpu;

    if (!KiArm64NumaDiscovered)
    {
        KiArm64EnsureDefaultNumaTopology();
        KiArm64NumaDiscovered = TRUE;
    }

    if (ActiveMask == 0)
    {
        ActiveMask = (KAFFINITY)1;
    }

    KiArm64CompactActiveNumaNodes(ActiveMask);

    /*
     * Rebuild the node processor masks from scratch: per-processor init folds a
     * node bit in for every prepared PCR, including one for a processor that
     * ultimately fails to start, leaving a stale bit not present in
     * KeActiveProcessors. Reset and re-accumulate from the active processors.
     */
    {
        ULONG Node;
        for (Node = 0; Node < KeNumberNodes; Node++)
        {
            if (KeNodeBlock[Node] != NULL)
            {
                KeNodeBlock[Node]->ProcessorMask = 0;
            }
        }
    }

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
    {
        PKPRCB Prcb;
        UCHAR NodeNumber;

        if ((ActiveMask & AFFINITY_MASK(Cpu)) == 0)
        {
            continue;
        }

        Prcb = KiProcessorBlock[Cpu];
        if (Prcb == NULL)
        {
            continue;
        }

        NodeNumber = KiArm64CpuNumaTopology[Cpu].NodeNumber;
        if ((NodeNumber >= KeNumberNodes) || (KeNodeBlock[NodeNumber] == NULL))
        {
            NodeNumber = 0;
        }

        Prcb->ParentNode = KeNodeBlock[NodeNumber];
        Prcb->ParentNode->ProcessorMask |= Prcb->SetMember;
    }

    KeMemoryBarrier();
}

static
ULONGLONG
KiArm64SmtCoreId(_In_ ULONGLONG Mpidr)
{
    if (Mpidr & KI_ARM64_MPIDR_MT)
    {
        return Mpidr & KI_ARM64_MPIDR_CORE_MT;
    }

    return Mpidr & KI_ARM64_MPIDR_AFF_ALL;
}

static
ULONGLONG
KiArm64SmtPackageId(_In_ ULONGLONG Mpidr)
{
    return Mpidr & KI_ARM64_MPIDR_PACKAGE;
}

/* Before the APs are up KeActiveProcessors can still be empty; treat the
   boot processor as the only active one in that window */
static
KAFFINITY
KiArm64ActiveProcessorMask(VOID)
{
    KAFFINITY ActiveMask = KeActiveProcessors;

    return (ActiveMask != 0) ? ActiveMask : (KAFFINITY)1;
}

VOID
NTAPI
KiArm64FinalizeSmtTopology(VOID)
{
    KAFFINITY ActiveMask = KiArm64ActiveProcessorMask();
    ULONG Cpu;
    ULONG Other;

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
    {
        PKPRCB Prcb;
        KAFFINITY Siblings = 0;
        ULONG ThreadCount = 0;
        ULONG CoreCount = 0;
        ULONGLONG CoreId;
        ULONGLONG PackageId;
        ULONGLONG SeenCores[MAXIMUM_PROCESSORS];

        if ((ActiveMask & AFFINITY_MASK(Cpu)) == 0)
        {
            continue;
        }

        Prcb = KiProcessorBlock[Cpu];
        if (Prcb == NULL)
        {
            continue;
        }

        if (!KiArm64CpuNumaTopology[Cpu].HasMpidr)
        {
            Prcb->MultiThreadProcessorSet = Prcb->SetMember;
            Prcb->LogicalProcessorsPerCore = 1;
            Prcb->CoresPerPhysicalProcessor = 1;
            continue;
        }

        CoreId = KiArm64SmtCoreId(KiArm64CpuNumaTopology[Cpu].Mpidr);
        PackageId = KiArm64SmtPackageId(KiArm64CpuNumaTopology[Cpu].Mpidr);

        for (Other = 0; Other < MAXIMUM_PROCESSORS; Other++)
        {
            ULONGLONG OtherCore;
            ULONG Seen;

            if ((ActiveMask & AFFINITY_MASK(Other)) == 0)
            {
                continue;
            }

            if (!KiArm64CpuNumaTopology[Other].HasMpidr)
            {
                continue;
            }

            OtherCore = KiArm64SmtCoreId(KiArm64CpuNumaTopology[Other].Mpidr);

            if (OtherCore == CoreId)
            {
                Siblings |= AFFINITY_MASK(Other);
                ThreadCount++;
            }

            if (KiArm64SmtPackageId(KiArm64CpuNumaTopology[Other].Mpidr) == PackageId)
            {
                for (Seen = 0; Seen < CoreCount; Seen++)
                {
                    if (SeenCores[Seen] == OtherCore)
                    {
                        break;
                    }
                }

                if ((Seen == CoreCount) && (CoreCount < MAXIMUM_PROCESSORS))
                {
                    SeenCores[CoreCount++] = OtherCore;
                }
            }
        }

        if (Siblings == 0)
        {
            Siblings = Prcb->SetMember;
            ThreadCount = 1;
        }

        if (CoreCount == 0)
        {
            CoreCount = 1;
        }

        Prcb->MultiThreadProcessorSet = Siblings;
        Prcb->LogicalProcessorsPerCore = (UCHAR)ThreadCount;
        Prcb->CoresPerPhysicalProcessor = (UCHAR)CoreCount;
    }

    KeMemoryBarrier();
    KiArm64SmtFinalized = TRUE;
}

/*
 * Processor cache and package providers for
 * SystemLogicalProcessorInformation(Ex).
 *
 * Cache geometry is read from CLIDR_EL1/CCSIDR_EL1 on the calling processor
 * and assumed homogeneous across the machine; refining per-cluster geometry
 * on heterogeneous (big.LITTLE) parts from the ACPI PPTT is future work.
 */

#define KI_ARM64_MAX_CACHE_LEVELS     7
#define KI_ARM64_MAX_CACHE_GEOMETRY   (2 * KI_ARM64_MAX_CACHE_LEVELS)

typedef struct _KI_ARM64_CACHE_GEOMETRY
{
    CACHE_DESCRIPTOR Descriptor;
    BOOLEAN PackageShared;
} KI_ARM64_CACHE_GEOMETRY, *PKI_ARM64_CACHE_GEOMETRY;

static
VOID
KiArm64ReadCacheLevelDescriptor(_In_ ULONG Level,
                                _In_ PROCESSOR_CACHE_TYPE Type,
                                _Out_ PCACHE_DESCRIPTOR Descriptor)
{
    ULONGLONG Size;
    ULONG LineShift;
    ULONG LineSize;
    ULONG Ways;
    ULONG Sets;

    /* CSSELR.Level is 0-based, InD picks the instruction side */
    KiArm64ReadCcsidr(Level - 1,
                      (Type == CacheInstruction),
                      &LineShift,
                      &Ways,
                      &Sets);
    LineSize = 1UL << LineShift;

    Descriptor->Level = (UCHAR)Level;
    Descriptor->LineSize = (USHORT)LineSize;    /* 3-bit field + 4: max 2048 */
    Descriptor->Type = Type;

    /* 0xFF is reserved for fully associative caches (a single set); clamp
       real way counts below it */
    if (Sets == 1)
    {
        Descriptor->Associativity = CACHE_FULLY_ASSOCIATIVE;
    }
    else if (Ways >= CACHE_FULLY_ASSOCIATIVE)
    {
        Descriptor->Associativity = CACHE_FULLY_ASSOCIATIVE - 1;
    }
    else
    {
        Descriptor->Associativity = (UCHAR)Ways;
    }

    Size = (ULONGLONG)Sets * Ways * LineSize;
    Descriptor->Size = (Size > MAXULONG) ? MAXULONG : (ULONG)Size;
}

static
ULONG
KiArm64CollectCacheGeometry(_Out_writes_(KI_ARM64_MAX_CACHE_GEOMETRY)
                                PKI_ARM64_CACHE_GEOMETRY Geometry)
{
    KIRQL OldIrql;
    ULONGLONG Clidr;
    ULONG Count = 0;
    ULONG DeepestLevel = 0;
    ULONG SharedFloor;
    ULONG Louis;
    ULONG Level;
    ULONG Index;

    /* CSSELR_EL1/CCSIDR_EL1 form a per-PE indirection pair: block thread
       migration so the whole walk reads a single processor's hierarchy */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    __asm__ __volatile__("mrs %0, clidr_el1" : "=r"(Clidr));

    for (Level = 1; Level <= KI_ARM64_MAX_CACHE_LEVELS; Level++)
    {
        ULONG CacheType = (ULONG)((Clidr >> ((Level - 1) * 3)) & 0x7);

        /* 0b000 terminates the hierarchy; treat reserved encodings the
           same way */
        if ((CacheType == 0) || (CacheType > 4))
        {
            break;
        }

        /* 0b001 instruction only, 0b011 separate instruction and data */
        if ((CacheType == 1) || (CacheType == 3))
        {
            KiArm64ReadCacheLevelDescriptor(Level,
                                            CacheInstruction,
                                            &Geometry[Count].Descriptor);
            Count++;
        }

        /* 0b010 data only, 0b011 separate instruction and data */
        if ((CacheType == 2) || (CacheType == 3))
        {
            KiArm64ReadCacheLevelDescriptor(Level,
                                            CacheData,
                                            &Geometry[Count].Descriptor);
            Count++;
        }

        /* 0b100 unified */
        if (CacheType == 4)
        {
            KiArm64ReadCacheLevelDescriptor(Level,
                                            CacheUnified,
                                            &Geometry[Count].Descriptor);
            Count++;
        }

        DeepestLevel = Level;
    }

    KeLowerIrql(OldIrql);

    /*
     * CLIDR_EL1.LoUIS names the last level that must be cleaned to reach
     * the point of unification for the Inner Shareable domain: levels
     * 1..LoUIS can hold data other cores do not observe (per-core private,
     * e.g. Cortex-A53 reports LoUIS = 1 for its private L1), while deeper
     * levels are unified (shared beyond the core). LoUIS == 0 means no
     * cleaning is required at all (FEAT_IDC-style parts); fall back to
     * treating only the deepest present level as shared.
     */
    Louis = (ULONG)((Clidr >> 21) & 0x7);
    if (Louis != 0)
    {
        SharedFloor = Louis + 1;
    }
    else
    {
        SharedFloor = (DeepestLevel != 0) ? DeepestLevel : 1;
    }

    for (Index = 0; Index < Count; Index++)
    {
        Geometry[Index].PackageShared =
            (Geometry[Index].Descriptor.Level >= SharedFloor);
    }

    return Count;
}

static
ULONG
KiArm64CollectPackageSets(_Out_writes_to_(MaxSets, return) PKAFFINITY Sets,
                          _In_ ULONG MaxSets)
{
    KAFFINITY ActiveMask = KiArm64ActiveProcessorMask();
    KAFFINITY Assigned = 0;
    ULONG Count = 0;
    ULONG Cpu;
    ULONG Other;

    for (Cpu = 0; (Cpu < MAXIMUM_PROCESSORS) && (Count < MaxSets); Cpu++)
    {
        KAFFINITY PackageSet;
        BOOLEAN HasId;
        ULONGLONG PackageId;

        if ((ActiveMask & AFFINITY_MASK(Cpu)) == 0)
        {
            continue;
        }

        if (Assigned & AFFINITY_MASK(Cpu))
        {
            continue;
        }

        HasId = KiArm64CpuNumaTopology[Cpu].HasMpidr;
        PackageId = HasId ?
                    KiArm64SmtPackageId(KiArm64CpuNumaTopology[Cpu].Mpidr) : 0;
        PackageSet = AFFINITY_MASK(Cpu);

        for (Other = Cpu + 1; Other < MAXIMUM_PROCESSORS; Other++)
        {
            if ((ActiveMask & AFFINITY_MASK(Other)) == 0)
            {
                continue;
            }

            if (HasId)
            {
                if (!KiArm64CpuNumaTopology[Other].HasMpidr ||
                    (KiArm64SmtPackageId(KiArm64CpuNumaTopology[Other].Mpidr) !=
                     PackageId))
                {
                    continue;
                }
            }
            else
            {
                /* Processors without MPIDR data fold into a single package */
                if (KiArm64CpuNumaTopology[Other].HasMpidr)
                {
                    continue;
                }
            }

            PackageSet |= AFFINITY_MASK(Other);
        }

        Assigned |= PackageSet;
        Sets[Count++] = PackageSet;
    }

    return Count;
}

static
ULONG
KiArm64ExpandCacheRecords(
    _In_reads_(PackageCount) const KAFFINITY *PackageSets,
    _In_ ULONG PackageCount,
    _Out_writes_to_(MaxRecords, return) PKI_CACHE_RECORD Records,
    _In_ ULONG MaxRecords)
{
    KI_ARM64_CACHE_GEOMETRY Geometry[KI_ARM64_MAX_CACHE_GEOMETRY];
    KAFFINITY ActiveMask = KiArm64ActiveProcessorMask();
    ULONG GeometryCount;
    ULONG Written = 0;
    ULONG Index;
    ULONG Package;
    ULONG Cpu;

    GeometryCount = KiArm64CollectCacheGeometry(Geometry);
    if (GeometryCount == 0)
    {
        return 0;
    }

    for (Index = 0; Index < GeometryCount; Index++)
    {
        if (Geometry[Index].PackageShared)
        {
            /* One instance of a shared level per package */
            for (Package = 0; Package < PackageCount; Package++)
            {
                if (Written >= MaxRecords)
                {
                    return Written;
                }

                Records[Written].Descriptor = Geometry[Index].Descriptor;
                Records[Written].ProcessorSet = PackageSets[Package];
                Written++;
            }
        }
        else
        {
            /* One instance of a private level per core; SMT siblings share
               their core's instance, so emit one record per sibling set */
            KAFFINITY Covered = 0;

            for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
            {
                PKPRCB Prcb;
                KAFFINITY CoreSet;

                if ((ActiveMask & AFFINITY_MASK(Cpu)) == 0)
                {
                    continue;
                }

                if (Covered & AFFINITY_MASK(Cpu))
                {
                    continue;
                }

                Prcb = KiProcessorBlock[Cpu];
                CoreSet = (Prcb != NULL) ? Prcb->MultiThreadProcessorSet : 0;
                if (CoreSet == 0)
                {
                    CoreSet = AFFINITY_MASK(Cpu);
                }

                if (Written >= MaxRecords)
                {
                    return Written;
                }

                Records[Written].Descriptor = Geometry[Index].Descriptor;
                Records[Written].ProcessorSet = CoreSet;
                Written++;

                Covered |= CoreSet | AFFINITY_MASK(Cpu);
            }
        }
    }

    return Written;
}

VOID
NTAPI
KiQueryProcessorTopology(
    _Out_writes_to_opt_(MaxRecords, *RecordCount) PKI_CACHE_RECORD Records,
    _In_ ULONG MaxRecords,
    _Out_ PULONG RecordCount,
    _Out_writes_to_opt_(MaxSets, *SetCount) PKAFFINITY Sets,
    _In_ ULONG MaxSets,
    _Out_ PULONG SetCount)
{
    KAFFINITY PackageSets[MAXIMUM_PROCESSORS];
    ULONG PackageCount;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    *RecordCount = 0;
    *SetCount = 0;

    /* Both views group processors by physical package; compute the sets
       once and share them */
    PackageCount = KiArm64CollectPackageSets(PackageSets, MAXIMUM_PROCESSORS);

    if ((Sets != NULL) && (MaxSets != 0))
    {
        *SetCount = min(PackageCount, MaxSets);
        RtlCopyMemory(Sets, PackageSets, *SetCount * sizeof(KAFFINITY));
    }

    if ((Records != NULL) && (MaxRecords != 0))
    {
        *RecordCount = KiArm64ExpandCacheRecords(PackageSets,
                                                 PackageCount,
                                                 Records,
                                                 MaxRecords);
    }
}
