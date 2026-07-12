/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Processor cache and package topology providers used by
 *              NtQuerySystemInformation(Ex) logical processor classes
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define TAG_KTOPO 'opTK'

#if defined(_M_AMD64) || defined(_M_IX86)

/* ------------------------------------------------------------------ */
/*  x86 / amd64: CPUID-based discovery                                 */
/* ------------------------------------------------------------------ */

static
VOID
KipCpuId(
    _In_ ULONG Leaf,
    _In_ ULONG SubLeaf,
    _Out_writes_all_(4) ULONG Registers[4])
{
    __cpuidex((int *)Registers, (int)Leaf, (int)SubLeaf);
}

static
ULONG
KipCeilLog2(
    _In_ ULONG Value)
{
    if (Value <= 1)
        return 0;

    return (ULONG)RtlFindMostSignificantBit(Value - 1) + 1;
}

#define KIP_MAX_CACHES_PER_CPU 8

typedef struct _KIP_CPU_CACHE_SAMPLE
{
    CACHE_DESCRIPTOR Descriptor;
    ULONG ShareShift;          /* APIC id bits below the sharing domain */
} KIP_CPU_CACHE_SAMPLE, *PKIP_CPU_CACHE_SAMPLE;

typedef struct _KIP_CPU_SAMPLE
{
    BOOLEAN Valid;
    ULONG ApicId;
    ULONG PackageShift;        /* APIC id bits below the package */
    ULONG CacheCount;
    KIP_CPU_CACHE_SAMPLE Cache[KIP_MAX_CACHES_PER_CPU];
} KIP_CPU_SAMPLE, *PKIP_CPU_SAMPLE;

/* Per-record sharing-domain key used to merge cache samples */
typedef struct _KIP_CACHE_KEY
{
    ULONG ShareShift;
    ULONG GroupId;
} KIP_CACHE_KEY, *PKIP_CACHE_KEY;

/* AMD legacy L2/L3 associativity encoding (CPUID 0x80000006) */
static
UCHAR
KipAmdLegacyAssociativity(
    _In_ ULONG Code,
    _In_ ULONG Ways)
{
    switch (Code)
    {
        case 0x1: return 1;
        case 0x2: return 2;
        case 0x4: return 4;
        case 0x6: return 8;
        case 0x8: return 16;
        case 0xA: return 32;
        case 0xB: return 48;
        case 0xC: return 64;
        case 0xD: return 96;
        case 0xE: return 128;
        case 0xF: return 0xFF;   /* fully associative */
        default:  return (UCHAR)min(Ways, 0xFEUL);
    }
}

static
VOID
KipAddCacheSample(
    _Inout_ PKIP_CPU_SAMPLE Sample,
    _In_ UCHAR Level,
    _In_ PROCESSOR_CACHE_TYPE Type,
    _In_ ULONG Size,
    _In_ ULONG LineSize,
    _In_ UCHAR Associativity,
    _In_ ULONG ShareShift)
{
    PKIP_CPU_CACHE_SAMPLE Cache;

    if ((Sample->CacheCount >= KIP_MAX_CACHES_PER_CPU) || (Size == 0))
        return;

    Cache = &Sample->Cache[Sample->CacheCount++];
    Cache->Descriptor.Level = Level;
    Cache->Descriptor.Type = Type;
    Cache->Descriptor.Size = Size;
    Cache->Descriptor.LineSize = (USHORT)min(LineSize, 0xFFFFUL);
    Cache->Descriptor.Associativity = Associativity;
    Cache->ShareShift = ShareShift;
}

/* Runs on the processor being described (caller sets affinity) */
static
VOID
KipSampleCurrentProcessor(
    _Out_ PKIP_CPU_SAMPLE Sample)
{
    ULONG Regs[4];
    ULONG MaxLeaf, MaxExtLeaf;
    ULONG Leaf1Ebx, Leaf1Edx;
    ULONG DetLeaf = 0;
    ULONG SubLeaf;
    BOOLEAN IsIntel, IsAmd;
    ULONG SmtShift = 0;

    RtlZeroMemory(Sample, sizeof(*Sample));

    KipCpuId(0, 0, Regs);
    MaxLeaf = Regs[0];
    IsIntel = (Regs[1] == 0x756E6547);  /* "Genu" */
    IsAmd = (Regs[1] == 0x68747541);    /* "Auth" */

    KipCpuId(0x80000000, 0, Regs);
    MaxExtLeaf = (Regs[0] >= 0x80000000) ? Regs[0] : 0;

    if (MaxLeaf < 1)
        return;

    /* initial (8-bit) APIC id; refined below by the topology leaves */
    KipCpuId(1, 0, Regs);
    Leaf1Ebx = Regs[1];
    Leaf1Edx = Regs[3];
    Sample->ApicId = (Leaf1Ebx >> 24) & 0xFF;

    /*
     * Package shift from the extended topology leaves (0x1F preferred,
     * 0x0B classic): the shift of the last valid level is the number of
     * APIC id bits that address logical processors inside the package.
     */
    if (MaxLeaf >= 0x0B)
    {
        ULONG Leaf = (MaxLeaf >= 0x1F) ? 0x1F : 0x0B;
        BOOLEAN Found = FALSE;

        for (SubLeaf = 0; SubLeaf < 8; SubLeaf++)
        {
            ULONG LevelType;

            KipCpuId(Leaf, SubLeaf, Regs);
            LevelType = (Regs[2] >> 8) & 0xFF;
            if (LevelType == 0)
                break;

            if (LevelType == 1)
                SmtShift = Regs[0] & 0x1F;

            Sample->PackageShift = Regs[0] & 0x1F;
            Sample->ApicId = Regs[3];
            Found = TRUE;
        }

        if (!Found)
            Sample->PackageShift = 0;
    }

    if (Sample->PackageShift == 0)
    {
        /* legacy fallback: logical processor count from leaf 1 */
        ULONG Logical = 1;

        if (Leaf1Edx & (1UL << 28))   /* HTT */
            Logical = max((Leaf1Ebx >> 16) & 0xFF, 1UL);

        Sample->PackageShift = KipCeilLog2(Logical);
    }

    /* deterministic cache parameter leaf */
    if (IsIntel && (MaxLeaf >= 4))
    {
        DetLeaf = 4;
    }
    else if (IsAmd && (MaxExtLeaf >= 0x8000001D))
    {
        KipCpuId(0x80000001, 0, Regs);
        if (Regs[2] & (1UL << 22))   /* TopologyExtensions */
            DetLeaf = 0x8000001D;
    }

    if (DetLeaf != 0)
    {
        for (SubLeaf = 0; SubLeaf < KIP_MAX_CACHES_PER_CPU; SubLeaf++)
        {
            ULONG Type, Level, ShareCount, Ways, Partitions, LineSize, Sets;
            PROCESSOR_CACHE_TYPE CacheType;
            UCHAR Associativity;

            KipCpuId(DetLeaf, SubLeaf, Regs);
            Type = Regs[0] & 0x1F;
            if ((Type == 0) || (Type > 3))
                break;

            Level = (Regs[0] >> 5) & 0x7;
            ShareCount = ((Regs[0] >> 14) & 0xFFF) + 1;
            Ways = ((Regs[1] >> 22) & 0x3FF) + 1;
            Partitions = ((Regs[1] >> 12) & 0x3FF) + 1;
            LineSize = (Regs[1] & 0xFFF) + 1;
            Sets = Regs[2] + 1;

            CacheType = (Type == 1) ? CacheData :
                        (Type == 2) ? CacheInstruction : CacheUnified;
            Associativity = (Regs[0] & (1UL << 9))
                            ? 0xFF : (UCHAR)min(Ways, 0xFEUL);

            KipAddCacheSample(Sample,
                              (UCHAR)Level,
                              CacheType,
                              Ways * Partitions * LineSize * Sets,
                              LineSize,
                              Associativity,
                              KipCeilLog2(ShareCount));
        }
    }
    else if (MaxExtLeaf >= 0x80000006)
    {
        /* legacy AMD fixed-function leaves (pre-topology-extensions parts
           have no SMT, so L1/L2 sharing is the logical processor itself) */
        ULONG L1Regs[4], L2Regs[4];

        KipCpuId(0x80000005, 0, L1Regs);
        KipCpuId(0x80000006, 0, L2Regs);

        /* L1 data: ECX = size[31:24]KB, assoc[23:16], line[7:0] */
        KipAddCacheSample(Sample, 1, CacheData,
                          ((L1Regs[2] >> 24) & 0xFF) * 1024,
                          L1Regs[2] & 0xFF,
                          (UCHAR)min((L1Regs[2] >> 16) & 0xFF, 0xFEUL),
                          SmtShift);

        /* L1 instruction: EDX, same layout */
        KipAddCacheSample(Sample, 1, CacheInstruction,
                          ((L1Regs[3] >> 24) & 0xFF) * 1024,
                          L1Regs[3] & 0xFF,
                          (UCHAR)min((L1Regs[3] >> 16) & 0xFF, 0xFEUL),
                          SmtShift);

        /* L2: ECX = size[31:16]KB, assoc-code[15:12], line[7:0] */
        KipAddCacheSample(Sample, 2, CacheUnified,
                          ((L2Regs[2] >> 16) & 0xFFFF) * 1024,
                          L2Regs[2] & 0xFF,
                          KipAmdLegacyAssociativity((L2Regs[2] >> 12) & 0xF,
                                                    16),
                          SmtShift);

        /* L3: EDX = size[31:18] * 512KB, assoc-code[15:12], line[7:0] */
        if (((L2Regs[3] >> 18) & 0x3FFF) != 0)
        {
            KipAddCacheSample(Sample, 3, CacheUnified,
                              ((L2Regs[3] >> 18) & 0x3FFF) * 512 * 1024,
                              L2Regs[3] & 0xFF,
                              KipAmdLegacyAssociativity((L2Regs[3] >> 12) & 0xF,
                                                        16),
                              Sample->PackageShift);
        }
    }

    Sample->Valid = TRUE;
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
    PKIP_CPU_SAMPLE Samples;
    PKIP_CACHE_KEY Keys;
    ULONG PackageIds[MAXIMUM_PROCESSORS];
    ULONG ProcessorCount = (ULONG)KeNumberProcessors;
    ULONG Records_ = 0, Sets_ = 0;
    ULONG i, c, r, p;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    *RecordCount = 0;
    *SetCount = 0;

    /* one allocation: per-CPU samples followed by the cache dedup keys */
    Samples = ExAllocatePoolWithTag(PagedPool,
                                    (ProcessorCount * sizeof(*Samples)) +
                                    (MaxRecords * sizeof(*Keys)),
                                    TAG_KTOPO);
    if (Samples == NULL)
        return;
    Keys = (PKIP_CACHE_KEY)(Samples + ProcessorCount);

    RtlZeroMemory(Samples, ProcessorCount * sizeof(*Samples));

    /* Visit every active processor once; the sweep feeds both the cache
       and the package views */
    for (i = 0; i < ProcessorCount; i++)
    {
        if (!(KeActiveProcessors & AFFINITY_MASK(i)))
            continue;

        KeSetSystemAffinityThread(AFFINITY_MASK(i));
        KipSampleCurrentProcessor(&Samples[i]);
    }
    KeRevertToUserAffinityThread();

    for (i = 0; i < ProcessorCount; i++)
    {
        ULONG PackageId;
        BOOLEAN Merged;

        if (!Samples[i].Valid || (KiProcessorBlock[i] == NULL))
            continue;

        /* merge this processor's caches into their sharing domains */
        for (c = 0; (c < Samples[i].CacheCount) && (MaxRecords != 0); c++)
        {
            PKIP_CPU_CACHE_SAMPLE Cache = &Samples[i].Cache[c];
            ULONG GroupId = Samples[i].ApicId >> Cache->ShareShift;

            Merged = FALSE;
            for (r = 0; r < Records_; r++)
            {
                if ((Records[r].Descriptor.Level == Cache->Descriptor.Level) &&
                    (Records[r].Descriptor.Type == Cache->Descriptor.Type) &&
                    (Keys[r].ShareShift == Cache->ShareShift) &&
                    (Keys[r].GroupId == GroupId))
                {
                    Records[r].ProcessorSet |= KiProcessorBlock[i]->SetMember;
                    Merged = TRUE;
                    break;
                }
            }

            if (!Merged && (Records_ < MaxRecords))
            {
                Records[Records_].Descriptor = Cache->Descriptor;
                Records[Records_].ProcessorSet = KiProcessorBlock[i]->SetMember;
                Keys[Records_].ShareShift = Cache->ShareShift;
                Keys[Records_].GroupId = GroupId;
                Records_++;
            }
        }

        /* merge it into its physical package */
        if (MaxSets != 0)
        {
            PackageId = Samples[i].ApicId >> Samples[i].PackageShift;

            Merged = FALSE;
            for (p = 0; p < Sets_; p++)
            {
                if (PackageIds[p] == PackageId)
                {
                    Sets[p] |= KiProcessorBlock[i]->SetMember;
                    Merged = TRUE;
                    break;
                }
            }

            if (!Merged && (Sets_ < MaxSets) && (Sets_ < MAXIMUM_PROCESSORS))
            {
                PackageIds[Sets_] = PackageId;
                Sets[Sets_] = KiProcessorBlock[i]->SetMember;
                Sets_++;
            }
        }
    }

    ExFreePoolWithTag(Samples, TAG_KTOPO);

    *RecordCount = Records_;
    *SetCount = Sets_;
}

#elif defined(_M_ARM64)

/* ------------------------------------------------------------------ */
/*  ARM64: KiQueryProcessorTopology is implemented by                  */
/*  ke/arm64/topology.c from CLIDR/CCSIDR + MPIDR data                 */
/* ------------------------------------------------------------------ */

#else

/* ------------------------------------------------------------------ */
/*  Other architectures: no discovery support yet                      */
/* ------------------------------------------------------------------ */

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
    UNREFERENCED_PARAMETER(Records);
    UNREFERENCED_PARAMETER(MaxRecords);
    UNREFERENCED_PARAMETER(Sets);
    UNREFERENCED_PARAMETER(MaxSets);

    *RecordCount = 0;
    *SetCount = 0;
}

#endif
