/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/mm/ARM3/init.c
 * PURPOSE:         Memory manager initialization stubs for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include <mm/ARM3/miarm.h>

#define IS_PAGE_ALIGNED(addr) ((((ULONG_PTR)(addr)) & (PAGE_SIZE - 1)) == 0)

#if defined(_M_ARM64) || defined(__aarch64__)
BOOLEAN MiArm64PfnFinalizePending = FALSE;
BOOLEAN ExpArm64PoolBootstrapMode = FALSE;
VOID MiArm64FinalizePfnDatabase(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);
VOID MiArm64DumpPoolDescriptors(_In_ PVOID VirtualAddress,
                                _In_z_ PCSTR ContextTag);
static VOID MiBuildNonPagedPool(VOID);
static VOID MiBuildSystemPteSpace(VOID);
PVOID MiSystemPteSpaceStart;
PVOID MiSystemPteSpaceEnd;
/* Optional one-shot trace budget for verbose mapping logs (unused in release). */
#if DBG
static volatile LONG MiArm64MapTraceBudget = 16;
#endif
static volatile LONG MiArm64MapProgressBudget = 8;
static volatile LONG MiArm64AliasLogBudget = 4;
static LONG MiArm64SelfMapProbe = -1;
/* Control whether MiMapPTEs zeroes newly allocated leaf pages (data pages). */
static volatile BOOLEAN MiArm64ZeroLeafPages = TRUE;
/* Optional boot-time cap for initial nonpaged pool mapping (in MiB). 0 = no cap. */
static ULONG MiArm64NonPagedPoolCapMb = 0;

static VOID MiMapPPEs(PVOID StartAddress, PVOID EndAddress);
static VOID MiMapPDEs(PVOID StartAddress, PVOID EndAddress);

#define ARM64_PTE_TYPE_TABLE        0x3ULL
#define ARM64_PTE_AF                (1ULL << 10)  /* Access Flag - required for L3 page entries */
#define ARM64_PTE_SH_INNER          (3ULL << 8)   /* Inner Shareable */
#define ARM64_PTE_AP_RW_EL1         (0ULL << 6)   /* EL1 R/W, EL0 no access */
/*
 * For recursive self-map entry: must work as BOTH table descriptor (L0-L2)
 * AND page descriptor (L3). Table descriptors ignore AF/SH/AP, but L3 needs
 * AF=1 or we get Access Flag faults. Include shareability for proper caching.
 */
#define ARM64_SELFMAP_ENTRY_BITS    (ARM64_PTE_TYPE_TABLE | ARM64_PTE_AF | ARM64_PTE_SH_INNER)
#define MI_ARM64_MAKE_TABLE_DESC(Pfn) (((UINT64)(Pfn) << PAGE_SHIFT) | ARM64_PTE_TYPE_TABLE)

/* Minimal PL011 UART helpers for early, direct serial breadcrumbs. */
#if defined(_M_ARM64) || defined(__aarch64__)
#define ARM64_PL011_BASE 0x09000000ULL
#define ARM64_PL011_DR   0x00
#define ARM64_PL011_FR   0x18
#define ARM64_PL011_FR_TXFF (1u << 5)
static __inline VOID MiArm64UartPutc(char Ch)
{
    volatile ULONG *Uart = (volatile ULONG *)(ULONG_PTR)ARM64_PL011_BASE;
    if (!Uart) return;
    while (Uart[ARM64_PL011_FR / sizeof(ULONG)] & ARM64_PL011_FR_TXFF)
    {
        __asm__ __volatile__("wfi");
    }
    Uart[ARM64_PL011_DR / sizeof(ULONG)] = (ULONG)(unsigned char)Ch;
}
static __inline VOID MiArm64UartPuts(const char *S)
{
    if (!S) return;
    while (*S)
    {
        if (*S == '\n') MiArm64UartPutc('\r');
        MiArm64UartPutc(*S++);
    }
}
static __inline VOID MiArm64UartPutHex64(ULONGLONG V)
{
    static const char H[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; --i)
    {
        ULONG shift = (ULONG)i * 4;
        MiArm64UartPutc(H[(V >> shift) & 0xFULL]);
    }
}
#endif /* defined(_M_ARM64) || defined(__aarch64__) */

VOID
MiArm64DumpPoolDescriptors(
    _In_ PVOID VirtualAddress,
    _In_z_ PCSTR ContextTag)
{
#if defined(_M_ARM64) || defined(__aarch64__)
    UINT64 Ttbr1;
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    UINT64 RootPa = Ttbr1 & ~((UINT64)PAGE_SIZE - 1ULL);
    volatile UINT64 *L0 = (volatile UINT64 *)(ULONG_PTR)(KSEG0_BASE | RootPa);

    ULONG L0Index = MiAddressToPxi(VirtualAddress);
    ULONG L1Index = (ULONG)(((ULONG_PTR)VirtualAddress >> PPI_SHIFT) & 0x1FF);
    ULONG L2Index = MiAddressToPdeOffset(VirtualAddress);
    ULONG L3Index = MiAddressToPteOffset(VirtualAddress);

    UINT64 E0 = L0[L0Index];
    volatile UINT64 *L1 = (E0 & 1ULL) ? (volatile UINT64 *)(ULONG_PTR)(KSEG0_BASE | (E0 & ~0xFFFULL)) : NULL;
    UINT64 E1 = L1 ? L1[L1Index] : 0;
    volatile UINT64 *L2 = (E1 & 1ULL) ? (volatile UINT64 *)(ULONG_PTR)(KSEG0_BASE | (E1 & ~0xFFFULL)) : NULL;
    UINT64 E2 = L2 ? L2[L2Index] : 0;
    volatile UINT64 *L3 = (E2 & 1ULL) ? (volatile UINT64 *)(ULONG_PTR)(KSEG0_BASE | (E2 & ~0xFFFULL)) : NULL;
    UINT64 E3 = L3 ? L3[L3Index] : 0;

    CHAR Log[200];
    if (NT_SUCCESS(RtlStringCbPrintfA(Log, sizeof(Log),
                                      "[arm64] NPPOOL %s: VA %p L0[%03lx]=0x%016llx L1[%03lx]=0x%016llx L2[%03lx]=0x%016llx L3[%03lx]=0x%016llx",
                                      ContextTag,
                                      VirtualAddress,
                                      (ULONG)L0Index,
                                      (unsigned long long)E0,
                                      (ULONG)L1Index,
                                      (unsigned long long)E1,
                                      (ULONG)L2Index,
                                      (unsigned long long)E2,
                                      (ULONG)L3Index,
                                      (unsigned long long)E3)))
    {
        KiArm64BootStageLog(Log);
    }

    PMMPTE PointerPte = MiAddressToPte(VirtualAddress);
    if (!L3)
    {
        if (NT_SUCCESS(RtlStringCbPrintfA(Log, sizeof(Log),
                                          "[arm64] NPPOOL %s: PTE %p unmapped",
                                          ContextTag,
                                          PointerPte)))
        {
            KiArm64BootStageLog(Log);
        }
    }
    else if (NT_SUCCESS(RtlStringCbPrintfA(Log, sizeof(Log),
                                           "[arm64] NPPOOL %s: PTE %p = 0x%016llx",
                                           ContextTag,
                                           PointerPte,
                                           (unsigned long long)E3)))
    {
        KiArm64BootStageLog(Log);
    }
#else
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(ContextTag);
#endif
}

static __inline PVOID
MiArm64PhysToKseg0(UINT64 Phys)
{
    /* Use the identity-mapped view of physical memory for page table
     * manipulation during early ARM64 bring-up. The identity mapping
     * established by KiArm64EnsureIdentityMapping covers all physical
     * RAM used for TTBR1 tables, so we do not need to rely on a KSEG0
     * alias for these internal MM operations. */
    return (PVOID)(ULONG_PTR)(KSEG0_BASE | Phys);
}

static __inline PVOID
MiArm64PfnToKseg0(PFN_NUMBER Pfn)
{
    return MiArm64PhysToKseg0(((UINT64)Pfn) << PAGE_SHIFT);
}

static VOID
MiArm64NormalizePoolPfnFlagsRange(_In_ PVOID Base,
                                  _In_ SIZE_T SizeBytes)
{
#if defined(_M_ARM64) || defined(__aarch64__)
    PMMPTE first = MiAddressToPte(Base);
    PMMPTE last = MiAddressToPte((PVOID)((ULONG_PTR)Base + SizeBytes - 1));
    PMMPTE p;
    for (p = first; p <= last; ++p)
    {
        if (p->u.Hard.Valid)
        {
            PFN_NUMBER pfn = PFN_FROM_PTE(p);
            PMMPFN pf = MiGetPfnEntry(pfn);
            if (pf && (MiGetPfnEntryIndex(pf) <= MmHighestPhysicalPage))
            {
                pf->u3.e1.StartOfAllocation = 0;
                pf->u3.e1.EndOfAllocation = 0;
                pf->u4.VerifierAllocation = 0;
            }
        }
    }
#else
    UNREFERENCED_PARAMETER(Base);
    UNREFERENCED_PARAMETER(SizeBytes);
#endif
}

static __inline volatile UINT64*
MiArm64LookupTableEntry(UINT64 Ttbr1, PVOID Va, ULONG Level)
{
    UINT64 root_pa = Ttbr1 & ~((UINT64)PAGE_SIZE - 1ULL);
    volatile UINT64 *l0 = (volatile UINT64 *)MiArm64PhysToKseg0(root_pa);
    ULONG l0_idx = MiAddressToPxi(Va);

    if (Level == 0)
        return &l0[l0_idx];

    UINT64 e0 = l0[l0_idx];
    if ((e0 & 1ULL) == 0)
        return NULL;

    volatile UINT64 *l1 = (volatile UINT64 *)MiArm64PhysToKseg0(e0 & ~0xFFFULL);
    ULONG l1_idx = (((ULONG_PTR)Va) >> PPI_SHIFT) & 0x1FF;
    if (Level == 1)
        return &l1[l1_idx];

    UINT64 e1 = l1[l1_idx];
    if ((e1 & 1ULL) == 0)
        return NULL;

    volatile UINT64 *l2 = (volatile UINT64 *)MiArm64PhysToKseg0(e1 & ~0xFFFULL);
    ULONG l2_idx = (((ULONG_PTR)Va) >> PDI_SHIFT) & 0x1FF;
    if (Level == 2)
        return &l2[l2_idx];

    UINT64 e2 = l2[l2_idx];
    if ((e2 & 1ULL) == 0)
        return NULL;

    volatile UINT64 *l3 = (volatile UINT64 *)MiArm64PhysToKseg0(e2 & ~0xFFFULL);
    ULONG l3_idx = MiAddressToPteOffset(Va);
    return &l3[l3_idx];
}

VOID
MiArm64MapPageTablePage(UINT64 Ttbr1, PVOID TableVa, PFN_NUMBER Pfn)
{
    volatile UINT64 *Leaf = MiArm64LookupTableEntry(Ttbr1, TableVa, 3);
    if (!Leaf)
        return;

    UINT64 Desc = ((UINT64)Pfn << PAGE_SHIFT) |
                  0x3ULL |                /* valid page */
                  ((UINT64)4ULL << 2) |   /* AttrIndx=4 (Normal WB) */
                  (3ULL << 8) |           /* Inner-shareable */
                  (1ULL << 10) |          /* AF */
                  (1ULL << 53) |          /* PXN */
                  (1ULL << 54);           /* UXN */
    *Leaf = Desc;
    __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
}

/*
 * Ensure that the alias page containing the page table entries for AliasVa
 * is mapped in the kernel address space. This creates any missing parent
 * tables (L0/L1/L2) and installs an L3 mapping for the alias page itself.
 */
VOID
MiArm64EnsureAliasMappingForPointer(
    _In_ PVOID AliasVa)
{
    UINT64 Ttbr1;
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    /* Ensure L0 self-map entry points to the root L0 */
    {
        UINT64 RootPa = Ttbr1 & ~((UINT64)PAGE_SIZE - 1ULL);
        volatile UINT64 *RootL0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);
        ULONG SelfIndex = MiAddressToPxi((PVOID)PXE_SELFMAP);
        if ((RootL0[SelfIndex] & ~0xFULL) != (RootPa & ~0xFULL))
        {
            RootL0[SelfIndex] = (RootPa | ARM64_SELFMAP_ENTRY_BITS);
            __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1\n\tdsb ish\n\tisb" ::: "memory");
        }
    }

    UNREFERENCED_PARAMETER(AliasVa);
}

/*
 * Map the PXE alias page that contains the self-map entry so that
 * dereferencing addresses in the PXE alias region is safe.
 *
 * CRITICAL: The ARM64 self-map at L0[493] creates a recursive structure where:
 * - PXE_BASE (indices [493,493,493,493]) walks purely through the recursive entry
 * - PPE_BASE (indices [493,493,493,*]) works because the first 3 levels are recursive
 * - PDE_BASE (indices [493,493,*,*]) and PTE_BASE (indices [493,*,*,*]) require
 *   intermediate L0 entries to exist because the walk leaves the recursive path
 *
 * For example, PDE_BASE+0 has indices [493,493,0,0]:
 *   L0[493] -> L0 (recursive)
 *   L0[493] -> L0 (recursive)
 *   L0[0] -> This needs a valid table descriptor!
 *   L1[0] -> The actual entry we're accessing
 *
 * This function sets up not only the recursive L0[493] entry but also ensures
 * that intermediate table entries exist for the self-map to work correctly.
 */
VOID
MiArm64MapPxeAlias(VOID)
{
#if defined(_M_ARM64) || defined(__aarch64__)
    UINT64 Ttbr1;
    ULONG i;

    KiArm64BootStageLog("[arm64] MiArm64MapPxeAlias: entry");

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    /* The alias VA we want to back is the page containing PXE_SELFMAP (= PXE_BASE page). */
    PVOID VaBase = (PVOID)((ULONG_PTR)PXE_SELFMAP & ~(PAGE_SIZE - 1ULL));
    UINT64 RootPa = Ttbr1 & ~((UINT64)PAGE_SIZE - 1ULL);
    ULONG SelfIndex = MiAddressToPxi((PVOID)PXE_SELFMAP);

    UNREFERENCED_PARAMETER(VaBase);

    {
        CHAR Stage[200];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiArm64MapPxeAlias: Ttbr1=0x%llx VaBase=%p RootPa=0x%llx SelfIdx=%lu",
                                          (unsigned long long)Ttbr1,
                                          VaBase,
                                          (unsigned long long)RootPa,
                                          (unsigned long)SelfIndex)))
        {
            KiArm64BootStageLog(Stage);
        }
    }

    volatile UINT64 *RootL0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);
    UINT64 Current = RootL0[SelfIndex];

    /* Ensure the L0 recursive entry points to the L0 root with proper table descriptor bits. */
    UINT64 DesiredEntry = RootPa | ARM64_SELFMAP_ENTRY_BITS;

    if ((Current & ~0xFULL) != RootPa)
    {
        CHAR Stage[160];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiArm64MapPxeAlias: fixing L0[%lu] from 0x%llx to 0x%llx",
                                          (unsigned long)SelfIndex,
                                          (unsigned long long)Current,
                                          (unsigned long long)DesiredEntry)))
        {
            KiArm64BootStageLog(Stage);
        }

        RootL0[SelfIndex] = DesiredEntry;
        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
        Current = RootL0[SelfIndex];
    }
    else
    {
        KiArm64BootStageLog("[arm64] MiArm64MapPxeAlias: L0[493] recursive entry already correct");
    }

    /*
     * NOTE: The self-map recursive structure has specific requirements for L0 entries.
     * When accessing PDE_BASE or PTE_BASE through the self-map, the translation walk
     * may need to traverse L0 entries that correspond to user-mode address ranges.
     *
     * DANGEROUS APPROACH (REMOVED): Previously, we filled ALL empty L0 entries with
     * a shared placeholder page. This creates serious problems:
     * - Those entries are no longer "empty" - code may treat them as real mappings
     * - When user processes need real page tables, we'd have conflicts
     * - Hard-to-debug memory corruption when the shared page is accessed
     *
     * SAFER APPROACH: Only ensure the kernel-space L0 entries are properly mapped.
     * The self-map should primarily be used for kernel address space management.
     * If user-space self-map access is needed, allocate real tables on-demand
     * through proper fault handlers, not dummy placeholders.
     *
     * For now, we rely on FreeLDR having set up the necessary kernel L0 entries
     * (typically indices 256-511 for kernel space on ARM64). The self-map recursive
     * entry at L0[493] is sufficient for most kernel MM operations.
     */
    {
        /* Log the self-map configuration for debugging */
        CHAR Stage[160];
        ULONG ValidEntries = 0;

        /* Count how many L0 entries are actually mapped */
        for (i = 0; i < 512; i++)
        {
            if (RootL0[i] & 1ULL)
                ValidEntries++;
        }

        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiArm64MapPxeAlias: L0 has %lu valid entries (no placeholders)",
                                          (unsigned long)ValidEntries)))
        {
            KiArm64BootStageLog(Stage);
        }
    }

    /* Verify the self-map setup by checking we can now access PXE_BASE.
     * With L0[493]->L0, the recursive walk should work:
     * PXE_BASE[493] accesses L0[493]->L0[493]->L0[493]->L0[493] = L0[493] itself.
     */
    {
        CHAR Stage[200];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiArm64MapPxeAlias: L0[%lu]=0x%llx (PA via KSEG0)",
                                          (unsigned long)SelfIndex,
                                          (unsigned long long)Current)))
        {
            KiArm64BootStageLog(Stage);
        }
    }

    /* Invalidate TLB entries for all self-map windows to ensure fresh translations. */
    __asm__ __volatile__(
        "dsb ishst\n\t"
        "tlbi vaae1is, %0\n\t"
        "tlbi vaae1is, %1\n\t"
        "tlbi vaae1is, %2\n\t"
        "tlbi vaae1is, %3\n\t"
        "dsb ish\n\t"
        "isb"
        :
        : "r"(PXE_BASE >> 12),
          "r"(PPE_BASE >> 12),
          "r"(PDE_BASE >> 12),
          "r"(PTE_BASE >> 12)
        : "memory");

    KiArm64BootStageLog("[arm64] MiArm64MapPxeAlias: self-map recursion configured");
#endif
}

VOID
MiArm64MapAliasForPointer(
    _In_ PVOID AliasVa)
{
#if defined(_M_ARM64) || defined(__aarch64__)
    UINT64 Ttbr1;
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    PVOID AliasBase = (PVOID)((ULONG_PTR)AliasVa & ~(PAGE_SIZE - 1ULL));

    /* Only call MiArm64MapPxeAlias for addresses actually IN the PXE_BASE range.
     * MiIsUserPxe checks if it's a "user" PXE, but we must first ensure it's
     * even in the PXE range at all. PTE/PDE/PPE addresses are all less than
     * PXE_BASE, so the old check would incorrectly match them.
     */
    if ((AliasVa >= (PVOID)PXE_BASE) && MiIsUserPxe(AliasVa))
    {
        MiArm64MapPxeAlias();
        return;
    }

    /* Handle all PPEs - both user and kernel/system.
     * The self-map needs alias pages for kernel PPEs too.
     */
    if ((AliasVa >= (PVOID)PPE_BASE) && (AliasVa < (PVOID)PXE_BASE))
    {
        ULONG64 ippe = (((ULONG64)(ULONG_PTR)AliasVa) - (ULONG64)PPE_BASE) >> 3;
        ULONG64 pxi = (ippe >> 9) & 0x1FFULL;
        PVOID VaSynth = (PVOID)(pxi << PXI_SHIFT);

        volatile UINT64 *E0 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 0);

        /* L1 PFN backs the PPE alias page */
        if (E0 && ((*E0 & 1ULL) != 0))
        {
            PFN_NUMBER PfnL1 = (PFN_NUMBER)((*E0 & ~0xFFFULL) >> PAGE_SHIFT);
            MiArm64MapPageTablePage(Ttbr1, AliasBase, PfnL1);
        }
        return;
    }

    /* Handle all PDEs - both user and kernel/system.
     * The self-map needs alias pages for kernel PDEs too.
     */
    if ((AliasVa >= (PVOID)PDE_BASE) && (AliasVa < (PVOID)PPE_BASE))
    {
        ULONG64 ipde = (((ULONG64)(ULONG_PTR)AliasVa) - (ULONG64)PDE_BASE) >> 3;
        ULONG64 ppi = (ipde >> 9) & 0x1FFULL;
        ULONG64 pxi = (ipde >> 18) & 0x1FFULL;
        PVOID VaSynth = (PVOID)((pxi << PXI_SHIFT) | (ppi << PPI_SHIFT));

        volatile UINT64 *E0 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 0);
        if (E0 && ((*E0 & 1ULL) == 0))
        {
            PFN_NUMBER Pfn = MxGetNextPage(1);
            if (Pfn != 0)
            {
                /* Zero new L1 table page then publish descriptor */
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                *E0 = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                __asm__ __volatile__("dsb ishst" ::: "memory");
            }
        }

        volatile UINT64 *E1 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 1);
        if (E1 && ((*E1 & 1ULL) != 0))
        {
            PFN_NUMBER PfnL2 = (PFN_NUMBER)((*E1 & ~0xFFFULL) >> PAGE_SHIFT);
            MiArm64MapPageTablePage(Ttbr1, AliasBase, PfnL2);
        }
        return;
    }

    /* Handle all PTEs - both user and kernel/system.
     * The self-map needs alias pages for kernel PTEs too!
     *
     * Previously, only MiIsUserPte() addresses were handled, which caused
     * translation faults when accessing PTEs for kernel addresses
     * (e.g., FFFFF6C000214000 is the PTE for kernel VA 0xFFFF800042800000).
     */
    if ((AliasVa >= (PVOID)PTE_BASE) && (AliasVa < (PVOID)PDE_BASE))
    {
        ULONG64 ipte = (((ULONG64)(ULONG_PTR)AliasVa) - (ULONG64)PTE_BASE) >> 3;
        ULONG64 pdi = (ipte >> 9) & 0x1FFULL;
        ULONG64 ppi = (ipte >> 18) & 0x1FFULL;
        ULONG64 pxi = (ipte >> 27) & 0x1FFULL;
        PVOID VaSynth = (PVOID)((pxi << PXI_SHIFT) | (ppi << PPI_SHIFT) | (pdi << PDI_SHIFT));

        volatile UINT64 *E0 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 0);
        if (E0 && ((*E0 & 1ULL) == 0))
        {
            PFN_NUMBER Pfn = MxGetNextPage(1);
            if (Pfn != 0)
            {
                /* Zero new L1 table page then publish descriptor */
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                *E0 = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                __asm__ __volatile__("dsb ishst" ::: "memory");

                /* Log when creating L0 entries for kernel PTE alias regions */
                if (MiArm64AliasLogBudget > 0)
                {
                    LONG Snapshot = InterlockedDecrement(&MiArm64AliasLogBudget);
                    if (Snapshot >= 0)
                    {
                        CHAR Log[160];
                        if (NT_SUCCESS(RtlStringCbPrintfA(Log, sizeof(Log),
                            "[arm64] MiArm64MapAliasForPointer: created L0[%lu] for PTE alias %p (VaSynth=%p)",
                            (ULONG)pxi, AliasVa, VaSynth)))
                        {
                            KiArm64BootStageLog(Log);
                        }
                    }
                }
            }
        }

        volatile UINT64 *E1 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 1);
        if (E1 && ((*E1 & 1ULL) == 0))
        {
            PFN_NUMBER Pfn = MxGetNextPage(1);
            if (Pfn != 0)
            {
                *E1 = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
            }
        }

        volatile UINT64 *E2 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 2);
        if (E2 && ((*E2 & 1ULL) != 0))
        {
            PFN_NUMBER PfnL3 = (PFN_NUMBER)((*E2 & ~0xFFFULL) >> PAGE_SHIFT);
            MiArm64MapPageTablePage(Ttbr1, AliasBase, PfnL3);
        }
        return;
    }
#endif
}

static
BOOLEAN
MiArm64CanTouchSystemPageTables(VOID)
{
    if (MiArm64SelfMapProbe != -1)
    {
        return MiArm64SelfMapProbe != 0;
    }

    UINT64 Ttbr1;
    _SEH2_TRY
    {
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        KiArm64BootStageLog("[arm64] MiArm64CanTouchSystemPageTables: ttbr1_el1 read fault");
        MiArm64SelfMapProbe = 0;
        _SEH2_YIELD(EXCEPTION_EXECUTE_HANDLER);
    }
    _SEH2_END;

    UINT64 RootPa = Ttbr1 & ~((UINT64)PAGE_SIZE - 1ULL);

    /* CRITICAL FIX: Must access physical page tables via KSEG0 mapping, not raw physical address!
     * KSEG0_BASE | PhysAddr gives the kernel's direct-map window for physical memory.
     */
    volatile UINT64 *RootL0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);

    BOOLEAN Faulted = FALSE;
    UINT64 Current = 0;

    _SEH2_TRY
    {
        Current = RootL0[MiAddressToPxi((PVOID)PXE_SELFMAP)];
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Faulted = TRUE;
    }
    _SEH2_END;

    if (Faulted)
    {
        /* This should not happen if FreeLDR set up KSEG0 correctly */
        KiArm64BootStageLog("[arm64] MiArm64CanTouchSystemPageTables: L0 access fault via KSEG0");
        MiArm64SelfMapProbe = 0;
        return FALSE;
    }

    UINT64 Desired = RootPa | ARM64_SELFMAP_ENTRY_BITS;
    ULONG SelfIndex = MiAddressToPxi((PVOID)PXE_SELFMAP);
    if ((Current & ~((UINT64)PAGE_SIZE - 1ULL)) != RootPa)
    {
        RootL0[SelfIndex] = Desired;
        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1\n\tdsb ish\n\tisb" ::: "memory");
        KiArm64BootStageLog("[arm64] MiArm64CanTouchSystemPageTables: recursive entry patched");
    }

    MiArm64SelfMapProbe = 1;
    return TRUE;
}
#endif

PVOID MiSessionViewEnd;

CODE_SEG("INIT")
NTSTATUS
NTAPI
MiInitMachineDependent(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    /* TODO: Flesh this out with proper ARM64 system VA construction. */

    /* Ensure kernel leaf PTEs use Normal WB (MAIR index 4) to match loader. */
    extern MMPTE ValidKernelPte;
    ValidKernelPte.u.Long |= ((ULONGLONG)4ULL << ARM64_PTE_CACHE_SHIFT);

    if (MmSecondaryColors == 0)
    {
        MmSecondaryColors = MI_SECONDARY_COLORS;
        MmSecondaryColorMask = MmSecondaryColors - 1;
    }

    if (MmSystemCacheWs.MinimumWorkingSetSize == 0)
    {
        MmSystemCacheWs.MinimumWorkingSetSize = 0;
        MmSystemCacheWs.WorkingSetSize = 0;
    }

    if (MmNonPagedSystemStart == NULL)
    {
        MmNonPagedSystemStart = (PVOID)MI_SYSTEM_SPACE_START;
    }

    if (MmHyperSpaceEnd == NULL)
    {
        MmHyperSpaceEnd = (PVOID)HYPER_SPACE_END;
    }

    if (MmSystemCacheStart == NULL)
    {
        MmSystemCacheStart = (PVOID)MI_SYSTEM_CACHE_START;
        MmSystemCacheEnd = (PVOID)MI_SYSTEM_CACHE_END;
    }

    if (MmNonPagedPoolEnd == NULL)
    {
        MmNonPagedPoolEnd = (PVOID)MI_NONPAGED_POOL_END;
    }

    if (MmNonPagedPoolStart == NULL)
    {
        MmNonPagedPoolStart = (PVOID)(MI_SYSTEM_SPACE_START + (16 * _1MB));
    }

    if (MmNonPagedPoolExpansionStart == NULL)
    {
        MmNonPagedPoolExpansionStart = MmNonPagedPoolEnd;
    }

    if (MmPagedPoolStart == NULL)
    {
        MmPagedPoolStart = (PVOID)MI_PAGED_POOL_START;
    }

    if (MmPagedPoolEnd == NULL)
    {
        MmPagedPoolEnd = MmPagedPoolStart;
    }

    if (MmSizeOfPagedPoolInBytes == 0)
    {
        MmSizeOfPagedPoolInBytes = MI_MIN_INIT_PAGED_POOLSIZE;
    }

    if (MmWorkingSetList == NULL)
    {
        MmWorkingSetList = (PMMWSL)MI_WORKING_SET_LIST;
    }

    if (MmPfnDatabase == NULL)
    {
        MmPfnDatabase = (PMMPFN)MI_PFN_DATABASE;
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        UINT64 Ttbr1;
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
        CHAR Stage[160];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiInitMachineDependent: ttbr1_el1=0x%llx",
                                          (unsigned long long)Ttbr1)))
        {
            KiArm64BootStageLog(Stage);
        }

        /* Also log the L0 self-map entry via KSEG0 to confirm
         * the recursive slot points at the root table. */
        {
            UINT64 RootPa = Ttbr1 & ~((UINT64)PAGE_SIZE - 1ULL);
            volatile UINT64 *RootL0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);
            ULONG SelfIndex = MiAddressToPxi((PVOID)PXE_SELFMAP);
            UINT64 Entry = 0;
            BOOLEAN Faulted = FALSE;
            _SEH2_TRY
            {
                Entry = RootL0[SelfIndex];
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Faulted = TRUE;
            }
            _SEH2_END;

            CHAR Check[200];
    if (NT_SUCCESS(RtlStringCbPrintfA(Check,
                                      sizeof(Check),
                                      "[arm64] MiInitMachineDependent: root_pa=0x%llx self_idx=%lu L0=0x%llx faulted=%d",
                                      (unsigned long long)RootPa,
                                      (unsigned long)SelfIndex,
                                      (unsigned long long)Entry,
                                      Faulted)))
    {
        KiArm64BootStageLog(Check);
    }
        }
    }
    if (MiArm64CanTouchSystemPageTables())
    {
        /* Seed the system process CR3-equivalent (TTBR1) into DirectoryTableBase[0]. */
        {
            UINT64 Ttbr1;
            __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
            PsGetCurrentProcess()->Pcb.DirectoryTableBase[0] = (ULONG_PTR)(Ttbr1 & ~((UINT64)PAGE_SIZE - 1ULL));
        }
        /* Seed alias windows early to avoid fault-time alias recursion. */
        KiArm64BootStageLog("[arm64] MiInitMachineDependent: seeding alias windows");
        MiArm64MapPxeAlias();
        MiArm64MapAliasForPointer((PVOID)PPE_BASE);
        MiArm64MapAliasForPointer((PVOID)PDE_BASE);
        MiArm64MapAliasForPointer((PVOID)PTE_BASE);

        /* Pre-map PFN DB page table levels (parity with amd64):
         * Ensure PPEs and PDEs exist for the PFN DB span so the PTE_BASE
         * leaf can be safely touched by MiMapPfnDatabase. */
        {
            PVOID PfnDbStart = (PVOID)MmPfnDatabase;
            PVOID PfnDbEnd = (PVOID)((PUCHAR)MmPfnDatabase + (MxPfnAllocation * PAGE_SIZE) - 1);
            MiMapPPEs(PfnDbStart, PfnDbEnd);
            MiMapPDEs(PfnDbStart, PfnDbEnd);
        }

        /* Map the PFN database before touching the color tables so the
         * MmFreePagesByColor backing range has valid leaf entries. */
        KiArm64BootStageLog("[arm64] MiInitMachineDependent: mapping PFN database");
        MiMapPfnDatabase(LoaderBlock);

        KiArm64BootStageLog("[arm64] MiInitMachineDependent: initializing color tables");
        MiInitializeColorTables();
        KiArm64BootStageLog("[arm64] MiInitMachineDependent: color tables ready");
        KiArm64BootStageLog("[arm64] MiInitMachineDependent: building nonpaged pool");
        MiBuildNonPagedPool();
        KiArm64BootStageLog("[arm64] MiInitMachineDependent: nonpaged pool ready");

        /* Initialize the nonpaged pool descriptor so ExAllocatePoolWithTag works */
        KiArm64BootStageLog("[arm64] MiInitMachineDependent: initializing pool descriptor");
        InitializePool(NonPagedPool, 0);
        KiArm64BootStageLog("[arm64] MiInitMachineDependent: pool descriptor ready");

        KiArm64BootStageLog("[arm64] MiInitMachineDependent: building system PTE space");
        MiBuildSystemPteSpace();
        KiArm64BootStageLog("[arm64] MiInitMachineDependent: system PTE space ready");

        KiArm64BootStageLog("[arm64] MiInitMachineDependent: initializing PFN database");
        MiInitializePfnDatabase(LoaderBlock);
        KiArm64BootStageLog("[arm64] MiInitMachineDependent: PFN database ready");

        if (MiArm64PfnFinalizePending)
        {
            KiArm64BootStageLog("[arm64] MiInitMachineDependent: finalizing PFN database now");
            MiArm64FinalizePfnDatabase(LoaderBlock);
            KiArm64BootStageLog("[arm64] MiInitMachineDependent: PFN database finalized");
            /* After PFN DB finalize, normalize PFN boundary flags for initial NPP. */
            {
                MiArm64NormalizePoolPfnFlagsRange(MmNonPagedPoolStart,
                                                  MmSizeOfNonPagedPoolInBytes);
                KiArm64BootStageLog("[arm64] MiInitMachineDependent: normalized NPP PFN flags after finalize");
            }
        }
    }
    else
    {
        KiArm64BootStageLog("[arm64] MiInitMachineDependent: deferring pool/PTE bring-up (self-map unavailable)");
    }
    KiArm64BootStageLog("[arm64] MiInitMachineDependent: guard check done");
#endif

    if (MmSystemPtesStart[SystemPteSpace] == NULL)
    {
        MmSystemPtesStart[SystemPteSpace] = MiAddressToPte(KSEG0_BASE);
        MmSystemPtesEnd[SystemPteSpace] = MmSystemPtesStart[SystemPteSpace];
    }

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
NTAPI
MiInitializeSessionSpaceLayout(VOID)
{
    MmSessionSize = MI_SESSION_SIZE;
    MiSessionSpaceEnd = (PVOID)MI_SESSION_SPACE_END;

    MmSessionImageSize = MI_SESSION_IMAGE_SIZE;
    MiSessionImageEnd = MiSessionSpaceEnd;
    MiSessionImageStart = (PUCHAR)MiSessionImageEnd - MmSessionImageSize;
    ASSERT(IS_PAGE_ALIGNED(MiSessionImageStart));

    MiSessionSpaceWs = (PUCHAR)MiSessionImageStart - MI_SESSION_WORKING_SET_SIZE;

    MmSessionViewSize = MI_SESSION_VIEW_SIZE;
    MiSessionViewEnd = MiSessionSpaceWs;
    MiSessionViewStart = (PUCHAR)MiSessionViewEnd - MmSessionViewSize;
    ASSERT(IS_PAGE_ALIGNED(MiSessionViewStart));

    MmSessionPoolSize = MI_SESSION_POOL_SIZE;
    MiSessionPoolEnd = MiSessionViewStart;
    MiSessionPoolStart = (PUCHAR)MiSessionPoolEnd - MmSessionPoolSize;
    ASSERT(IS_PAGE_ALIGNED(MiSessionPoolStart));

    MmSessionBase = MiSessionPoolStart;

    MmSystemViewSize = MI_SYSTEM_VIEW_SIZE;
    MiSystemViewStart = (PUCHAR)MmSessionBase - MmSystemViewSize;
    ASSERT(IS_PAGE_ALIGNED(MiSystemViewStart));

    ASSERT(Add2Ptr(MmSessionBase, MmSessionSize) == MiSessionSpaceEnd);
    ASSERT(MiSessionViewEnd <= MiSessionImageStart);
    ASSERT(MmSessionBase <= MiSessionPoolStart);

    MiSessionImagePteStart = MiAddressToPte(MiSessionImageStart);
    MiSessionImagePteEnd = MiAddressToPte(MiSessionImageEnd);
    MiSessionBasePte = MiAddressToPte(MmSessionBase);
    MiSessionLastPte = MiAddressToPte(MiSessionSpaceEnd);

    MmSessionSpace = (PMM_SESSION_SPACE)Add2Ptr(MiSessionImageStart, 0x10000);
}
#if defined(_M_ARM64) || defined(__aarch64__)
static VOID
MiMapPPEs(
    PVOID StartAddress,
    PVOID EndAddress)
{
    PMMPDE PointerPpe;
    MMPDE TmplPde = ValidKernelPde;
    PMMPDE BasePpe;
    PMMPDE EndPpe;

    BasePpe = MiAddressToPpe(StartAddress);
    EndPpe = MiAddressToPpe(EndAddress);

    {
        CHAR Stage[160];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiMapPPEs: range %p-%p base=%p end=%p",
                                          StartAddress,
                                          EndAddress,
                                          BasePpe,
                                          EndPpe)))
        {
            KiArm64BootStageLog(Stage);
        }
#if defined(_M_ARM64) || defined(__aarch64__)
        MiArm64UartPuts("[uart] PPE: base=");
        MiArm64UartPutHex64((ULONGLONG)(ULONG_PTR)BasePpe);
        MiArm64UartPuts(" end=");
        MiArm64UartPutHex64((ULONGLONG)(ULONG_PTR)EndPpe);
        MiArm64UartPuts("\n");
#endif
    }

    for (PointerPpe = BasePpe;
         PointerPpe <= EndPpe;
         PointerPpe++)
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        (void)TmplPde;
        UINT64 Ttbr1;
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
        PVOID TargetVa = MiPpeToAddress(PointerPpe);
        volatile UINT64 *EntryPhys = MiArm64LookupTableEntry(Ttbr1, TargetVa, 1);

        if (!EntryPhys)
        {
            volatile UINT64 *L0Entry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 0);
            if (L0Entry && ((*L0Entry & 1ULL) == 0))
            {
                PFN_NUMBER Pfn = MxGetNextPage(1);
                if (Pfn != 0)
                {
                    /* Initialize the new L1 table page before publishing. */
                    RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                    UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                    *L0Entry = Desc;
                    __asm__ __volatile__("dsb ishst" ::: "memory");
                    PVOID SelfVa = (PVOID)((ULONG_PTR)PointerPpe & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
                    MiArm64MapPageTablePage(Ttbr1, SelfVa, Pfn);
                    CHAR Stage[160];
                    if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                                      sizeof(Stage),
                                                      "[arm64] MiMapPPEs: allocated L1 for L0[%03Ix] (VA %p)",
                                                      (SIZE_T)MiAddressToPxi(TargetVa),
                                                      TargetVa)))
                    {
                        KiArm64BootStageLog(Stage);
                    }
                }
                else
                {
                    KiArm64BootStageLog("[arm64] MiMapPPEs: failed to allocate TTBR1 L1 table");
                }
                EntryPhys = MiArm64LookupTableEntry(Ttbr1, TargetVa, 1);
            }

            if (!EntryPhys)
            {
                CHAR Warn[160];
                if (NT_SUCCESS(RtlStringCbPrintfA(Warn,
                                                  sizeof(Warn),
                                                  "[arm64] MiMapPPEs: missing L0 slot for %p",
                                                  TargetVa)))
                {
                    KiArm64BootStageLog(Warn);
                }
                continue;
            }
        }

        UINT64 Entry = *EntryPhys;
        if ((Entry & 1ULL) == 0)
        {
            PFN_NUMBER Pfn = MxGetNextPage(1);
            /* Initialize the L2 table page before publishing. */
            RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
            UINT64 table_desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
            *EntryPhys = table_desc;
            __asm__ __volatile__("dsb ishst" ::: "memory");
            PMMPDE PdePointer = MiAddressToPde(TargetVa);
            PVOID SelfVa = (PVOID)((ULONG_PTR)PdePointer & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
            MiArm64MapPageTablePage(Ttbr1, SelfVa, Pfn);
            CHAR Stage[160];
            if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                              sizeof(Stage),
                                              "[arm64] MiMapPPEs: table page mapped %p -> PFN %I64x",
                                              SelfVa,
                                              (ULONGLONG)Pfn)))
            {
                KiArm64BootStageLog(Stage);
            }
        }
#else
        if (!PointerPpe->u.Hard.Valid)
        {
            TmplPde.u.Hard.PageFrameNumber = MxGetNextPage(1);
            MI_WRITE_VALID_PTE(PointerPpe, TmplPde);
            RtlZeroMemory(MiPteToAddress(PointerPpe), PAGE_SIZE);
            if (MiArm64MapTraceBudget > 0)
            {
                LONG Snapshot = InterlockedDecrement(&MiArm64MapTraceBudget);
                if (Snapshot >= 0)
                {
                    CHAR Stage[128];
                    if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                                      sizeof(Stage),
                                                      "[arm64] MiMapPPEs: mapped PPE %p",
                                                      PointerPpe)))
                    {
                        KiArm64BootStageLog(Stage);
                    }
                }
            }
        }
#endif
    }
}

static VOID
MiMapPDEs(
    PVOID StartAddress,
    PVOID EndAddress)
{
    PMMPDE PointerPde;
    MMPDE TmplPde = ValidKernelPde;
    PMMPDE BasePde;
    PMMPDE EndPde;

    BasePde = MiAddressToPde(StartAddress);
    EndPde = MiAddressToPde(EndAddress);

    {
        CHAR Stage[160];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiMapPDEs: range %p-%p base=%p end=%p",
                                          StartAddress,
                                          EndAddress,
                                          BasePde,
                                          EndPde)))
        {
            KiArm64BootStageLog(Stage);
        }
    }

    BOOLEAN PerformedPdeMappings = FALSE;

    for (PointerPde = BasePde;
         PointerPde <= EndPde;
         PointerPde++)
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        (void)TmplPde;
        UINT64 Ttbr1;
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
        PVOID TargetVa = MiPdeToAddress(PointerPde);
        volatile UINT64 *EntryPhys = MiArm64LookupTableEntry(Ttbr1, TargetVa, 2);

        if (!EntryPhys)
        {
            volatile UINT64 *PpeEntry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 1);
            if (!PpeEntry || ((*PpeEntry & 1ULL) == 0))
            {
                volatile UINT64 *L0Entry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 0);
                if (L0Entry && ((*L0Entry & 1ULL) == 0))
                {
                    PFN_NUMBER Pfn = MxGetNextPage(1);
                    if (Pfn != 0)
                    {
                        /* Initialize the new L1 table page before publishing. */
                        RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                        UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                        *L0Entry = Desc;
                        __asm__ __volatile__("dsb ishst" ::: "memory");
                        PMMPDE PpePointer = MiAddressToPpe(TargetVa);
                        PVOID SelfVa = (PVOID)((ULONG_PTR)PpePointer & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
                        MiArm64MapPageTablePage(Ttbr1, SelfVa, Pfn);
                        PerformedPdeMappings = TRUE;
                    }
                }

                if (PpeEntry && ((*PpeEntry & 1ULL) == 0))
                {
                    PFN_NUMBER Pfn = MxGetNextPage(1);
                    if (Pfn != 0)
                    {
                        /* Initialize the new L2 table page before publishing. */
                        RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                        UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                        *PpeEntry = Desc;
                        __asm__ __volatile__("dsb ishst" ::: "memory");
                        PVOID SelfVa = (PVOID)((ULONG_PTR)PointerPde & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
                        MiArm64MapPageTablePage(Ttbr1, SelfVa, Pfn);
                        PerformedPdeMappings = TRUE;
                    }
                }
            }

            EntryPhys = MiArm64LookupTableEntry(Ttbr1, TargetVa, 2);
            if (!EntryPhys)
            {
                CHAR Warn[160];
                if (NT_SUCCESS(RtlStringCbPrintfA(Warn,
                                                  sizeof(Warn),
                                                  "[arm64] MiMapPDEs: missing L1 slot for %p",
                                                  TargetVa)))
                {
                    KiArm64BootStageLog(Warn);
                }
                continue;
            }
        }

        {
            UINT64 Entry = *EntryPhys;
            if ((Entry & 1ULL) == 0)
            {
                PFN_NUMBER Pfn = MxGetNextPage(1);
                /* Initialize the new L3 table page before publishing. */
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                UINT64 table_desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                *EntryPhys = table_desc;
                __asm__ __volatile__("dsb ishst" ::: "memory");
                PMMPTE PtePointer = MiAddressToPte(TargetVa);
                PVOID SelfVa = (PVOID)((ULONG_PTR)PtePointer & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
                MiArm64MapPageTablePage(Ttbr1, SelfVa, Pfn);
                PerformedPdeMappings = TRUE;
                //CHAR Stage[160];
                // if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                //                                   sizeof(Stage),
                //                                   "[arm64] MiMapPDEs: table page mapped %p -> PFN %I64x",
                //                                   SelfVa,
                //                                   (ULONGLONG)Pfn)))
                // {
                //     KiArm64BootStageLog(Stage);
                // }
            }
        }
#else
        if (!PointerPde->u.Hard.Valid)
        {
            TmplPde.u.Hard.PageFrameNumber = MxGetNextPage(1);
            MI_WRITE_VALID_PTE(PointerPde, TmplPde);
            RtlZeroMemory(MiPteToAddress(PointerPde), PAGE_SIZE);
            if (MiArm64MapTraceBudget > 0)
            {
                LONG Snapshot = InterlockedDecrement(&MiArm64MapTraceBudget);
                if (Snapshot >= 0)
                {
                    CHAR Stage[128];
                    if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                                      sizeof(Stage),
                                                      "[arm64] MiMapPDEs: mapped PDE %p",
                                                      PointerPde)))
                    {
                        KiArm64BootStageLog(Stage);
                    }
                }
            }
        }
#endif
    }

    if (PerformedPdeMappings)
    {
        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
    }
}

VOID
MiMapPTEs(
    PVOID StartAddress,
    PVOID EndAddress)
{
    PMMPTE PointerPte;
    MMPTE TmplPte = ValidKernelPte;
    PMMPTE BasePte;
    PMMPTE EndPte;
    SIZE_T TotalPtes;
    SIZE_T HeartbeatStride;
    SIZE_T NextHeartbeat;

    BasePte = MiAddressToPte(StartAddress);
    EndPte = MiAddressToPte(EndAddress);
    TotalPtes = (SIZE_T)(EndPte - BasePte + 1);

    HeartbeatStride = (TotalPtes >= 8) ? (TotalPtes / 8) : TotalPtes;
    if (HeartbeatStride < 0x2000) HeartbeatStride = 0x2000;
    if (HeartbeatStride > TotalPtes) HeartbeatStride = TotalPtes;
    NextHeartbeat = HeartbeatStride;

    {
        CHAR Stage[160];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiMapPTEs: range %p-%p total=%zu stride=%zu base=%p end=%p",
                                          StartAddress,
                                          EndAddress,
                                          TotalPtes,
                                          HeartbeatStride,
                                          BasePte,
                                          EndPte)))
        {
            KiArm64BootStageLog(Stage);
        }
    }

    BOOLEAN PerformedMappings = FALSE;

    for (PointerPte = BasePte;
         PointerPte <= EndPte;
         PointerPte++)
    {
        {
#if defined(_M_ARM64) || defined(__aarch64__)
            (void)TmplPte;
            UINT64 Ttbr1;
            __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
            PVOID TargetVa = MiPteToAddress(PointerPte);
            volatile UINT64 *EntryPhys = MiArm64LookupTableEntry(Ttbr1, TargetVa, 3);
            if (!EntryPhys)
            {
                static BOOLEAN WarnedMissingL2 = FALSE;
                if (!WarnedMissingL2)
                {
                    CHAR Warn[160];
                    if (NT_SUCCESS(RtlStringCbPrintfA(Warn,
                                                      sizeof(Warn),
                                                      "[arm64] MiMapPTEs: missing L2 slot for %p",
                                                      TargetVa)))
                    {
                        KiArm64BootStageLog(Warn);
                    }
                    WarnedMissingL2 = TRUE;
                }
                continue;
            }

            UINT64 Entry = *EntryPhys;
            if ((Entry & 1ULL) == 0)
            {
                PFN_NUMBER Pfn = MxGetNextPage(1);
                UINT64 Desc = ((UINT64)Pfn << PAGE_SHIFT) |
                              0x3ULL |                /* valid page */
                              ((UINT64)4ULL << 2) |   /* AttrIndx=4 */
                              (3ULL << 8) |           /* Inner-shareable */
                              (1ULL << 10) |          /* AF */
                              (1ULL << 53) |          /* PXN */
                              (1ULL << 54);           /* UXN */
                *EntryPhys = Desc;
                /* Defer TLB invalidation to after the loop; ensure stores are visible now. */
                __asm__ __volatile__("dsb ishst" ::: "memory");
                if (MiArm64ZeroLeafPages)
                {
                    RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                }
                PerformedMappings = TRUE;
                // tmp log CHAR Stage[128];
                // if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                //                                   sizeof(Stage),
                //                                   "[arm64] MiMapPTEs: mapped PTE %p",
                //                                   PointerPte)))
                // {
                //     KiArm64BootStageLog(Stage);
                // }
            }
#else
            if (!PointerPte->u.Hard.Valid)
            {
                TmplPte.u.Hard.PageFrameNumber = MxGetNextPage(1);
                MI_WRITE_VALID_PTE(PointerPte, TmplPte);
                RtlZeroMemory(MiPteToAddress(PointerPte), PAGE_SIZE);
                if (MiArm64MapTraceBudget > 0)
                {
                    LONG Snapshot = InterlockedDecrement(&MiArm64MapTraceBudget);
                    if (Snapshot >= 0)
                    {
                        CHAR Stage[128];
                        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                                          sizeof(Stage),
                                                          "[arm64] MiMapPTEs: mapped PTE %p",
                                                          PointerPte)))
                        {
                            KiArm64BootStageLog(Stage);
                        }
                    }
                }
            }
#endif
        }

        if ((HeartbeatStride != 0) &&
            (TotalPtes != 0) &&
            ((SIZE_T)(PointerPte - BasePte + 1) >= NextHeartbeat) &&
            (MiArm64MapProgressBudget > 0))
        {
            LONG Snapshot = InterlockedDecrement(&MiArm64MapProgressBudget);
            if (Snapshot >= 0)
            {
                CHAR Stage[128];
                SIZE_T Processed = (SIZE_T)(PointerPte - BasePte + 1);
                if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                                  sizeof(Stage),
                                                  "[arm64] MiMapPTEs: mapped %zu/%zu PTEs",
                                                  Processed,
                                                  TotalPtes)))
                {
                    KiArm64BootStageLog(Stage);
                }
            }
            NextHeartbeat += HeartbeatStride;
        }
    }

    /* If we installed any new mappings, perform a single broadcast TLB maintenance. */
    if (PerformedMappings)
    {
        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
    }
}

static
VOID
MiBuildNonPagedPool(VOID)
{
    KiArm64BootStageLog("[arm64] MiBuildNonPagedPool: start");
    if (MxFreeDescriptor)
    {
        CHAR Stage[160];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiBuildNonPagedPool: free descriptor base=0x%lx pages=%lu",
                                          (ULONG)MxFreeDescriptor->BasePage,
                                          (ULONG)MxFreeDescriptor->PageCount)))
        {
            KiArm64BootStageLog(Stage);
        }
    }
    else
    {
        KiArm64BootStageLog("[arm64] MiBuildNonPagedPool: MxFreeDescriptor is NULL");
    }
    /* Check if this is a machine with less than 256MB of RAM, and no override */
    if ((MmNumberOfPhysicalPages <= MI_MIN_PAGES_FOR_NONPAGED_POOL_TUNING) &&
        !(MmSizeOfNonPagedPoolInBytes))
    {
        /* Force the non paged pool to be 2MB so we can reduce RAM usage */
        MmSizeOfNonPagedPoolInBytes = 2 * _1MB;
    }

    /* Check if the user gave a ridiculously large nonpaged pool RAM size */
    if ((MmSizeOfNonPagedPoolInBytes >> PAGE_SHIFT) >
        (MmNumberOfPhysicalPages * 7 / 8))
    {
        /* More than 7/8ths of RAM was dedicated to nonpaged pool, ignore! */
        MmSizeOfNonPagedPoolInBytes = 0;
    }

    /* Check if no registry setting was set, or if the setting was too low */
    if (MmSizeOfNonPagedPoolInBytes < MmMinimumNonPagedPoolSize)
    {
        SIZE_T AdditionalMb;

        /* Start with the minimum (256 KB) and add 32 KB for each MB above 4 */
        MmSizeOfNonPagedPoolInBytes = MmMinimumNonPagedPoolSize;

        if (MmNumberOfPhysicalPages > 1024)
        {
            /* 256 pages (4 KiB each) represent one MiB of physical memory */
            AdditionalMb = (SIZE_T)((MmNumberOfPhysicalPages - 1024) / 256);
            MmSizeOfNonPagedPoolInBytes += AdditionalMb *
                                           (SIZE_T)MmMinAdditionNonPagedPoolPerMb;
        }
    }

    /* Check if the registry setting or our dynamic calculation was too high */
    if (MmSizeOfNonPagedPoolInBytes > MI_MAX_INIT_NONPAGED_POOL_SIZE)
    {
        /* Set it to the maximum */
        MmSizeOfNonPagedPoolInBytes = MI_MAX_INIT_NONPAGED_POOL_SIZE;
    }

    /* Check if a percentage cap was set through the registry */
    if (MmMaximumNonPagedPoolPercent)
    {
        CHAR Stage[128];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiBuildNonPagedPool: ignoring MaximumPercent=%lu",
                                          MmMaximumNonPagedPoolPercent)))
        {
            KiArm64BootStageLog(Stage);
        }
        MmMaximumNonPagedPoolPercent = 0;
    }

    /* Page-align the nonpaged pool size */
    MmSizeOfNonPagedPoolInBytes &= ~(PAGE_SIZE - 1);

    /* Now, check if there was a registry size for the maximum size */
    if (!MmMaximumNonPagedPoolInBytes)
    {
        SIZE_T AdditionalMb;

        /* Start with the default (1MB) and add 400 KB for each MB above 4 */
        MmMaximumNonPagedPoolInBytes = MmDefaultMaximumNonPagedPool;

        if (MmNumberOfPhysicalPages > 1024)
        {
            /* 256 pages (4 KiB each) represent one MiB of physical memory */
            AdditionalMb = (SIZE_T)((MmNumberOfPhysicalPages - 1024) / 256);
            MmMaximumNonPagedPoolInBytes += AdditionalMb *
                                             (SIZE_T)MmMaxAdditionNonPagedPoolPerMb;
        }
    }

    /* Don't let the maximum go too high */
    if (MmMaximumNonPagedPoolInBytes > MI_MAX_NONPAGED_POOL_SIZE)
    {
        MmMaximumNonPagedPoolInBytes = MI_MAX_NONPAGED_POOL_SIZE;
    }

    /* Optional explicit cap for initial nonpaged pool size */
    if (MiArm64NonPagedPoolCapMb != 0)
    {
        ULONGLONG CapBytes = (ULONGLONG)MiArm64NonPagedPoolCapMb * 1024ULL * 1024ULL;
        if (MmSizeOfNonPagedPoolInBytes > CapBytes)
            MmSizeOfNonPagedPoolInBytes = (SIZE_T)CapBytes;
        if (MmMaximumNonPagedPoolInBytes > CapBytes)
            MmMaximumNonPagedPoolInBytes = (SIZE_T)CapBytes;
        {
            CHAR Stage[160];
            if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                              sizeof(Stage),
                                              "[arm64] MiBuildNonPagedPool: cap applied %lu MiB",
                                              MiArm64NonPagedPoolCapMb)))
            {
                KiArm64BootStageLog(Stage);
            }
        }
    }

    /* Convert nonpaged pool size from bytes to pages */
    MmMaximumNonPagedPoolInPages = MmMaximumNonPagedPoolInBytes >> PAGE_SHIFT;

    /* Non paged pool starts after the PFN database */
    MmNonPagedPoolStart = MmPfnDatabase + MxPfnAllocation * PAGE_SIZE;
    {
        CHAR Stage[128];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiBuildNonPagedPool: start=%p size=%I64x max=%I64x",
                                          MmNonPagedPoolStart,
                                          (ULONGLONG)MmSizeOfNonPagedPoolInBytes,
                                          (ULONGLONG)MmMaximumNonPagedPoolInBytes)))
        {
            KiArm64BootStageLog(Stage);
        }
    }

    /* Calculate the nonpaged pool expansion start region */
    MmNonPagedPoolExpansionStart = (PCHAR)MmNonPagedPoolStart +
                                          MmSizeOfNonPagedPoolInBytes;
    ASSERT(IS_PAGE_ALIGNED(MmNonPagedPoolExpansionStart));

    /* And this is where the non paged pool ends */
    MmNonPagedPoolEnd = (PCHAR)MmNonPagedPoolStart + MmMaximumNonPagedPoolInBytes;
    if (!(MmNonPagedPoolEnd < (PVOID)MM_HAL_VA_START))
    {
        CHAR Stage[128];
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] MiBuildNonPagedPool: pool end %p beyond HAL start %p",
                                          MmNonPagedPoolEnd,
                                          (PVOID)MM_HAL_VA_START)))
        {
            KiArm64BootStageLog(Stage);
        }
    }

    KiArm64BootStageLog("[arm64] MiBuildNonPagedPool: mapping address space");

    /* Map PPEs and PDEs for non paged pool (including expansion) */
    MiMapPPEs(MmNonPagedPoolStart, MmNonPagedPoolEnd);
    MiMapPDEs(MmNonPagedPoolStart, MmNonPagedPoolEnd);

    /* Map the nonpaged pool PTEs (without expansion). Avoid pre-zeroing data pages to speed boot. */
    MiArm64ZeroLeafPages = FALSE;
    MiMapPTEs(MmNonPagedPoolStart, (PCHAR)MmNonPagedPoolExpansionStart - 1);
    MiArm64ZeroLeafPages = TRUE;
#if defined(_M_ARM64) || defined(__aarch64__)
    {
        UINT64 Ttbr1;
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
        volatile UINT64 *PoolStartPte = MiArm64LookupTableEntry(Ttbr1, MmNonPagedPoolStart, 3);

        if (PoolStartPte)
        {
            ULONGLONG PteValue = *PoolStartPte;
            CHAR Stage[160];

            if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                              sizeof(Stage),
                                              "[arm64] MiBuildNonPagedPool: NonPagedPoolStart %p PTE=%p value=0x%llx",
                                              MmNonPagedPoolStart,
                                              PoolStartPte,
                                              (unsigned long long)PteValue)))
            {
                KiArm64BootStageLog(Stage);
            }

            DbgPrintEx(DPFLTR_MM_ID,
                       DPFLTR_INFO_LEVEL,
                       "[arm64] MiBuildNonPagedPool: NonPagedPoolStart %p PTE %p value 0x%llx\n",
                       MmNonPagedPoolStart,
                       PoolStartPte,
                       (unsigned long long)PteValue);
        }
        else
        {
            KiArm64BootStageLog("[arm64] MiBuildNonPagedPool: NonPagedPoolStart PTE lookup failed");
            DbgPrintEx(DPFLTR_MM_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[arm64] MiBuildNonPagedPool: missing PTE for NonPagedPoolStart %p\n",
                       MmNonPagedPoolStart);
        }
    }
#endif
    KiArm64BootStageLog("[arm64] MiBuildNonPagedPool: address space mapped");

    /* Normalize PFN boundary flags for initial nonpaged pool pages. */
#if defined(_M_ARM64) || defined(__aarch64__)
    MiArm64NormalizePoolPfnFlagsRange(MmNonPagedPoolStart,
                                      MmSizeOfNonPagedPoolInBytes);
#endif

    /* Initialize the ARM3 nonpaged pool */
    MiInitializeNonPagedPool();
    MiInitializeNonPagedPoolThresholds();
    KiArm64BootStageLog("[arm64] MiBuildNonPagedPool: complete");
}

static
VOID
MiBuildSystemPteSpace(VOID)
{
    KiArm64BootStageLog("[arm64] MiBuildSystemPteSpace: start");
    PMMPTE PointerPte;
    SIZE_T NonPagedSystemSize;
    PVOID SystemPteRangeEnd;

    /* Use the default number of system PTEs */
    MmNumberOfSystemPtes = MI_NUMBER_SYSTEM_PTES;
    NonPagedSystemSize = (MmNumberOfSystemPtes + 1) * PAGE_SIZE;

    /* Put system PTEs at the start of the system VA space */
    MiSystemPteSpaceStart = MmNonPagedSystemStart;
    MiSystemPteSpaceEnd = (PUCHAR)MiSystemPteSpaceStart + NonPagedSystemSize;

    /* Convert exclusive end into inclusive end for the mapping helpers */
    SystemPteRangeEnd = (PVOID)((PUCHAR)MiSystemPteSpaceEnd - 1);
#if defined(_M_ARM64) || defined(__aarch64__)
    MiArm64DumpPoolDescriptors(MiSystemPteSpaceStart, "syspte-pre");
    MiArm64DumpPoolDescriptors(SystemPteRangeEnd, "syspte-end");
#endif
    MiMapPPEs(MiSystemPteSpaceStart, SystemPteRangeEnd);
    MiMapPDEs(MiSystemPteSpaceStart, SystemPteRangeEnd);
    MiMapPTEs(MiSystemPteSpaceStart, SystemPteRangeEnd);
#if defined(_M_ARM64) || defined(__aarch64__)
    MiArm64DumpPoolDescriptors(MiSystemPteSpaceStart, "syspte-post-map");
#endif
    KiArm64BootStageLog("[arm64] MiBuildSystemPteSpace: ranges mapped");

    /* Initialize the system PTE space */
    PointerPte = MiAddressToPte(MiSystemPteSpaceStart);
#if defined(_M_ARM64) || defined(__aarch64__)
    MiArm64DumpPoolDescriptors(MiSystemPteSpaceStart, "syspte-before-init");
#endif
    MiInitializeSystemPtes(PointerPte, MmNumberOfSystemPtes, SystemPteSpace);

    /* Reserve system PTEs for zeroing PTEs and clear them */
    MiFirstReservedZeroingPte = MiReserveSystemPtes(MI_ZERO_PTES + 1,
                                                    SystemPteSpace);
    RtlZeroMemory(MiFirstReservedZeroingPte, (MI_ZERO_PTES + 1) * sizeof(MMPTE));
    MiFirstReservedZeroingPte->u.Hard.PageFrameNumber = MI_ZERO_PTES;
    KiArm64BootStageLog("[arm64] MiBuildSystemPteSpace: complete");
}

VOID
MiArm64FinalizePfnDatabase(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    MiArm64PfnFinalizePending = FALSE;
}
#endif /* defined(_M_ARM64) || defined(__aarch64__) */
