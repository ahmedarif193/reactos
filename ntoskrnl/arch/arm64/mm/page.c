/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/mm/page.c
 * PURPOSE:         ARM64 virtual memory helper routines
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>
#include "../include/ke.h"

/* ARM64 PTE address mask - extracts physical address from page table entry (bits 47:12) */
#ifndef ARM64_PTE_ADDR_MASK
#define ARM64_PTE_ADDR_MASK 0x0000FFFFFFFFF000ULL
#endif

static
NTSTATUS
MmCreateVirtualMappingUnsafeEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical);

static
BOOLEAN
MiIsPageTablePresent(
    _In_ PVOID Address);

#if defined(_M_ARM64) || defined(__aarch64__)
BOOLEAN
MiArm64ReadUserPtePhysically(
    _In_ PVOID Address,
    _Out_opt_ PULONG64 OutPte,
    _Out_opt_ PULONG OutDepth);

static LONG MiArm64UserMapTraceCount;
static LONG MiArm64UserPublishTraceCount;
static LONG MiArm64MapCacheSyncTraceCount;

FORCEINLINE
VOID
MiArm64InvalidatePageByPfnAlias(
    _In_ PFN_NUMBER PageFrameNumber)
{
    ULONG64 Ctr;
    ULONG DcacheLineSize;
    ULONG_PTR AliasVa;

    AliasVa = (ULONG_PTR)KSEG0_BASE + ((ULONG_PTR)PageFrameNumber << PAGE_SHIFT);

    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);

    __asm__ __volatile__("dsb sy" ::: "memory");
    for (ULONG_PTR Offset = 0; Offset < PAGE_SIZE; Offset += DcacheLineSize)
    {
        __asm__ __volatile__("dc ivac, %0" :: "r"(AliasVa + Offset) : "memory");
    }
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
MiArm64PublishPageByPfnAlias(
    _In_ PFN_NUMBER PageFrameNumber,
    _In_ BOOLEAN SweepIcache)
{
    ULONG64 Ctr;
    ULONG DcacheLineSize;
    ULONG IcacheLineSize;
    ULONG_PTR AliasVa;

    AliasVa = (ULONG_PTR)KSEG0_BASE + ((ULONG_PTR)PageFrameNumber << PAGE_SHIFT);

    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);
    IcacheLineSize = 4u << (Ctr & 0xF);

    __asm__ __volatile__("dsb sy" ::: "memory");
    for (ULONG_PTR Offset = 0; Offset < PAGE_SIZE; Offset += DcacheLineSize)
    {
        __asm__ __volatile__("dc civac, %0" :: "r"(AliasVa + Offset) : "memory");
    }
    __asm__ __volatile__("dsb sy" ::: "memory");

    if (SweepIcache)
    {
        for (ULONG_PTR Offset = 0; Offset < PAGE_SIZE; Offset += IcacheLineSize)
        {
            __asm__ __volatile__("ic ivau, %0" :: "r"(AliasVa + Offset) : "memory");
        }
        __asm__ __volatile__("dsb sy" ::: "memory");
    }

    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
MiArm64SyncMappedPfnCacheAttribute(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ PFN_NUMBER PageFrameNumber,
    _In_ MMPTE FinalPte)
{
    PMMPFN Pfn1;
    MI_PFN_CACHE_ATTRIBUTE OldCache;
    MI_PFN_CACHE_ATTRIBUTE NewCache;
    LONG TraceIndex;

    Pfn1 = MiGetPfnEntry(PageFrameNumber);
    if (Pfn1 == NULL)
    {
        return;
    }

    OldCache = Pfn1->u3.e1.CacheAttribute;
    NewCache = MiGetPteCacheAttribute(&FinalPte);
    if (OldCache == NewCache)
    {
        return;
    }

    Pfn1->u3.e1.CacheAttribute = NewCache;

    TraceIndex = InterlockedIncrement(&MiArm64MapCacheSyncTraceCount);
    if (TraceIndex <= 96)
    {
        DPRINT1("[arm64][MAPCA] sync[%ld] proc=%.16s va=%p page=%Ix old=%u new=%u "
                "pte=0x%016llx ref=%u share=%u loc=%u\n",
                TraceIndex,
                Process ? Process->ImageFileName : "<kernel>",
                Address,
                PageFrameNumber,
                OldCache,
                NewCache,
                (unsigned long long)FinalPte.u.Long,
                (ULONG)Pfn1->u3.e2.ReferenceCount,
                (ULONG)Pfn1->u2.ShareCount,
                (ULONG)Pfn1->u3.e1.PageLocation);
    }
}

FORCEINLINE
BOOLEAN
MiArm64ShouldTraceUserMap(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    ULONG_PTR Base;
    ULONG_PTR Va;

    if ((Process == NULL) || (PspSystemDllBase == NULL))
        return FALSE;

    Va = (ULONG_PTR)Address;
    if (Va >= (ULONG_PTR)MmSystemRangeStart)
        return FALSE;

    Base = (ULONG_PTR)PspSystemDllBase;
    return ((Va >= Base) && (Va < (Base + 0x200000)));
}

FORCEINLINE
VOID
MiArm64TraceUserMapState(
    _In_z_ PCSTR Site,
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ PFN_NUMBER Page,
    _In_ ULONG Protection,
    _In_ ULONG64 OldPte)
{
    ULONG64 PteValue = 0;
    ULONG Depth = 0;
    LONG TraceIndex;

    if (!MiArm64ShouldTraceUserMap(Process, Address))
        return;

    TraceIndex = InterlockedIncrement(&MiArm64UserMapTraceCount);
    if (TraceIndex > 40)
        return;

    MiArm64ReadUserPtePhysically(Address, &PteValue, &Depth);
    DPRINT1("[arm64][UVMAP] %s[%ld] proc=%.16s va=%p page=%Ix "
            "prot=0x%lx old=0x%016llx depth=%lu pte=0x%016llx\n",
            Site,
            TraceIndex,
            Process ? Process->ImageFileName : "<none>",
            Address,
            Page,
            Protection,
            (unsigned long long)OldPte,
            Depth,
            (unsigned long long)PteValue);
}

FORCEINLINE
VOID
MiArm64TraceUserTableStore(
    _In_z_ PCSTR Site,
    _In_ PVOID FaultAddress,
    _In_ ULONG Level,
    _In_ ULONG Index,
    _In_ ULONG64 OldValue,
    _In_ ULONG64 NewValue)
{
    PETHREAD Thread;
    PEPROCESS CurrentProcess;
    PEPROCESS ApcProcess;
    PEPROCESS SavedApcProcess;
    ULONG64 Ttbr0;
    ULONG64 Ttbr1;

    Thread = PsGetCurrentThread();
    CurrentProcess = PsGetCurrentProcess();
    ApcProcess = Thread ? (PEPROCESS)Thread->Tcb.ApcState.Process : NULL;
    SavedApcProcess = Thread ? (PEPROCESS)Thread->Tcb.SavedApcState.Process : NULL;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    DPRINT("[arm64][PTW] %s Va=%p L%lu[%lu] Old=0x%llx New=0x%llx "
           "Cur=%p(%.16s) ApcIdx=%lu Attached=%u Apc=%p(%.16s) Saved=%p(%.16s) "
           "TTBR0=0x%llx TTBR1=0x%llx CPU=%lu\n",
            Site,
            FaultAddress,
            Level,
            Index,
            (unsigned long long)OldValue,
            (unsigned long long)NewValue,
            CurrentProcess,
            CurrentProcess ? CurrentProcess->ImageFileName : "<none>",
            Thread ? (ULONG)Thread->Tcb.ApcStateIndex : 0,
            KeIsAttachedProcess() ? 1u : 0u,
            ApcProcess,
            ApcProcess ? ApcProcess->ImageFileName : "<none>",
            SavedApcProcess,
            SavedApcProcess ? SavedApcProcess->ImageFileName : "<none>",
            (unsigned long long)Ttbr0,
            (unsigned long long)Ttbr1,
            KeGetCurrentProcessorNumber());
}

#define MI_ARM64_STORE_TABLE_ENTRY(_Site, _Address, _Level, _Table, _Index, _PteValue) \
    do { \
        ULONG64 __OldEntry = (_Table)[(_Index)].u.Long; \
        MiArm64TraceUserTableStore((_Site), (_Address), (_Level), (_Index), __OldEntry, (_PteValue).u.Long); \
        (_Table)[(_Index)] = (_PteValue); \
    } while (0)

#define MI_ARM64_STORE_TABLE_ENTRY64(_Site, _Address, _Level, _Table, _Index, _Value64) \
    do { \
        ULONG64 __OldEntry = (_Table)[(_Index)].u.Long; \
        MiArm64TraceUserTableStore((_Site), (_Address), (_Level), (_Index), __OldEntry, (ULONG64)(_Value64)); \
        (_Table)[(_Index)].u.Long = (ULONG64)(_Value64); \
    } while (0)
#endif

/*
 * MiArm64ReadUserPtePhysically - Walk TTBR0's page tables via KSEG0 and read PTE.
 *
 * This function walks the user page table hierarchy PHYSICALLY using KSEG0
 * direct mapping. It NEVER accesses TTBR0 alias addresses, avoiding all
 * nested fault issues that plague the TTBR0 alias approach.
 *
 * Parameters:
 *   Address   - User-space virtual address to look up
 *   OutPte    - If non-NULL, receives the L3 PTE value (or 0 if not mapped)
 *   OutDepth  - If non-NULL, receives the depth reached:
 *               0 = Invalid TTBR0 or L0 invalid
 *               1 = L0 valid, L1 invalid
 *               2 = L1 valid (or block), L2 invalid
 *               3 = L2 valid (or block), L3 reached
 *               4 = L3 is valid page descriptor
 *
 * Returns:
 *   TRUE if the address is mapped (page exists), FALSE otherwise.
 */
BOOLEAN
MiArm64ReadUserPtePhysically(
    _In_ PVOID Address,
    _Out_opt_ PULONG64 OutPte,
    _Out_opt_ PULONG OutDepth)
{
    ULONG64 Ttbr0, RootPa;
    volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
    ULONG64 L0Entry, L1Entry, L2Entry, L3Entry;

    /* Initialize outputs */
    if (OutPte) *OutPte = 0;
    if (OutDepth) *OutDepth = 0;

    /* Must be a user address */
    if (Address >= MmSystemRangeStart)
        return FALSE;

    /* Read TTBR0 to get the user page table root */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;  /* Extract PA, mask ASID bits */
    if (RootPa == 0)
        return FALSE;

    /* Calculate indices for each level */
    L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
    L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
    L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
    L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

    /* Access L0 table via KSEG0 direct mapping */
    L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
    L0Entry = L0Table[L0Idx];

    /* Check L0 entry validity - must be a table descriptor (bits[1:0]=0b11) */
    if ((L0Entry & 0x3ULL) != 0x3ULL)
        return FALSE;

    if (OutDepth) *OutDepth = 1;

    /* Access L1 table */
    L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & ARM64_PTE_ADDR_MASK));
    L1Entry = L1Table[L1Idx];

    /*
     * L1 could be a 1GB block (bits[1:0]=0b01) or table (0b11) or invalid.
     *
     * ARM64 Critical: Block descriptors (1GB/2MB) in user space are from FreeLoader's
     * identity mapping, NOT from the Memory Manager. We must NOT treat them as
     * "present" pages because:
     * 1. They point to wrong physical memory (old identity-mapped addresses)
     * 2. ReactOS MM creates only 4KB page mappings for user addresses
     * 3. Section views need proper L3 PTEs to work correctly
     *
     * Return FALSE for blocks - callers like MmIsPagePresent need to know that
     * no proper MM-created mapping exists at this address.
     */
    if ((L1Entry & 0x1ULL) == 0)
        return FALSE;  /* L1 invalid */
    if ((L1Entry & 0x3ULL) == 0x1ULL)
    {
        /* L1 is 1GB block - FreeLoader identity mapping, treat as not present */
        if (OutPte) *OutPte = L1Entry;
        if (OutDepth) *OutDepth = 1;  /* Stopped at L1 block, not a proper page */
        return FALSE;
    }

    if (OutDepth) *OutDepth = 2;

    /* L1 is table, access L2 */
    L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & ARM64_PTE_ADDR_MASK));
    L2Entry = L2Table[L2Idx];

    /* L2 could be a 2MB block or table or invalid */
    if ((L2Entry & 0x1ULL) == 0)
        return FALSE;  /* L2 invalid */
    if ((L2Entry & 0x3ULL) == 0x1ULL)
    {
        /* L2 is 2MB block - FreeLoader identity mapping, treat as not present */
        if (OutPte) *OutPte = L2Entry;
        if (OutDepth) *OutDepth = 2;  /* Stopped at L2 block, not a proper page */
        return FALSE;
    }

    if (OutDepth) *OutDepth = 3;

    /* L2 is table, access L3 */
    L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & ARM64_PTE_ADDR_MASK));
    L3Entry = L3Table[L3Idx];

    if (OutPte) *OutPte = L3Entry;

    /* L3 entry: page descriptor (bits[1:0]=0b11) means page is present (proper 4KB mapping) */
    if ((L3Entry & 0x3ULL) == 0x3ULL)
    {
        if (OutDepth) *OutDepth = 4;
        return TRUE;
    }

    return FALSE;
}

/*
 * ARM64 Cache Invalidation Functions
 *
 * Cache coherency on ARM64 requires careful handling depending on data direction:
 *
 * INCOMING data (DMA/ramdisk populated page, read fault):
 *   The data source (device, ramdisk) has already written to RAM.
 *   CPU cache may have stale/garbage data for the VA.
 *   Use DC IVAC (Invalidate only) - discard cache lines without writeback.
 *   CRITICAL: DC CIVAC is WRONG - it writes back garbage to RAM first!
 *
 * OUTGOING data (CPU wrote, needs visibility to DMA/other observers):
 *   CPU has written data that may still be in cache.
 *   Use DC CIVAC (Clean & Invalidate) - write back dirty lines first.
 *
 * CTR_EL0 layout:
 *   Bits [3:0]   = IminLine (I-cache line size as log2(words))
 *   Bits [19:16] = DminLine (D-cache line size as log2(words))
 *   Line size = 4 << field_value (in bytes)
 */

/*
 * MiInvalidateDCachePageIncoming - Invalidate D-cache for INCOMING data.
 *
 * Use this when the page has been populated by an external source (DMA, ramdisk,
 * PIO) and the CPU needs to see fresh data. This DISCARDS any stale cache
 * contents without writing them back.
 *
 * WARNING: Using this on pages with valid CPU-written data will LOSE that data!
 */
VOID
MiInvalidateDCachePageIncoming(
    _In_ PVOID Address)
{
    ULONG64 Ctr;
    ULONG DcacheLineSize, IcacheLineSize;
    ULONG_PTR Va;

    /* Read CTR_EL0 to get cache line sizes */
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);  /* Bits [19:16] = DminLine */
    IcacheLineSize = 4u << (Ctr & 0xF);          /* Bits [3:0] = IminLine */

    /* Align address to page boundary */
    Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

    /* Ensure all prior memory operations complete before cache invalidation */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * DC IVAC (Invalidate by VA to PoC) - invalidate only, no writeback.
     * This is correct for incoming data: discard stale cache, read fresh RAM.
     *
     * Note: DC IVAC is permitted at EL1 regardless of SCTLR_EL1.UCI setting.
     * The UCI bit only affects EL0 access to cache maintenance instructions.
     */
    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
    {
        __asm__ __volatile__("dc ivac, %0" :: "r"(Va + offset) : "memory");
    }

    /* Ensure D-cache invalidation completes before I-cache ops */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * IC IVAU (Invalidate I-cache by VA to PoU).
     * For executable pages, also invalidate I-cache using correct line size.
     */
    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += IcacheLineSize)
    {
        __asm__ __volatile__("ic ivau, %0" :: "r"(Va + offset) : "memory");
    }

    /* Ensure I-cache operations complete and synchronize instruction stream */
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

/*
 * MiInvalidateDCachePageOutgoing - Clean & Invalidate D-cache for OUTGOING data.
 *
 * Use this when the CPU has written data that needs to be visible to external
 * observers (DMA, other CPUs). This writes back dirty cache lines to RAM first,
 * then invalidates them.
 */
VOID
MiInvalidateDCachePageOutgoing(
    _In_ PVOID Address)
{
    ULONG64 Ctr;
    ULONG DcacheLineSize, IcacheLineSize;
    ULONG_PTR Va;

    /* Read CTR_EL0 to get cache line sizes */
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);  /* Bits [19:16] = DminLine */
    IcacheLineSize = 4u << (Ctr & 0xF);          /* Bits [3:0] = IminLine */

    /* Align address to page boundary */
    Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

    /* Ensure all prior stores complete before cache operations */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * DC CIVAC (Clean and Invalidate by VA to PoC).
     * Writes back any dirty data to RAM, then invalidates the cache line.
     * This ensures CPU writes reach main memory before DMA/others read.
     */
    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
    {
        __asm__ __volatile__("dc civac, %0" :: "r"(Va + offset) : "memory");
    }

    /* Ensure D-cache operations complete before I-cache ops */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * IC IVAU (Invalidate I-cache by VA to PoU).
     * For self-modifying code scenarios, invalidate I-cache using correct line size.
     */
    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += IcacheLineSize)
    {
        __asm__ __volatile__("ic ivau, %0" :: "r"(Va + offset) : "memory");
    }

    /* Ensure I-cache operations complete and synchronize instruction stream */
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

/*
 * MiInvalidateDCachePage - Legacy wrapper, uses CIVAC (outgoing) semantics.
 *
 * This maintains backward compatibility with existing callers.
 * New code should use MiInvalidateDCachePageIncoming or MiInvalidateDCachePageOutgoing
 * explicitly based on the data flow direction.
 */
VOID
MiInvalidateDCachePage(
    _In_ PVOID Address)
{
    /* Default to outgoing semantics for backward compatibility */
    MiInvalidateDCachePageOutgoing(Address);
}

/*
 * ARM64-specific helper to get PFN for user addresses by walking the
 * TTBR0 page table hierarchy directly using system PTEs.
 *
 * On ARM64 with TTBR0/TTBR1 split, the kernel's self-mapping (in TTBR1)
 * cannot access user page tables (in TTBR0). We must use the CURRENTLY
 * ACTIVE TTBR0 register to walk the hierarchy, not DirectoryTableBase
 * (which may point to kernel tables for the System process).
 *
 * Returns 0 if the page is not mapped.
 */
static
PFN_NUMBER
MiArm64GetUserPfn(
    _In_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PFN_NUMBER RootPfn, PpePfn, PdePfn, PtePfn, PagePfn = 0;
    PMMPTE MappingPte;
    PMMPTE MappedPage;
    MMPTE MapPte;
    ULONG PxeIndex, PpeIndex, PdeIndex, PteIndex;

    ASSERT(Address < MmSystemRangeStart);
    ASSERT(Process != NULL);

    /*
     * Get the root table PFN.
     *
     * BUG #52 FIX: For a DIFFERENT process (e.g., during KeStackAttachProcess or
     * timer DPC), we must use DirectoryTableBase[0], not TTBR0.
     *
     * BUG #52b FIX (SMP): For the CURRENT process, DirectoryTableBase[0] may be
     * stale or point to different page tables than what TTBR0_EL1 actually uses.
     * MmCreateVirtualMappingUnsafeEx writes via TTBR0, so readback must also
     * use TTBR0 to see the same page tables. Use TTBR0 when Process is current.
     */
    if (Process == PsGetCurrentProcess())
    {
        ULONG64 Ttbr0Val;
        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0Val));
        RootPfn = (Ttbr0Val & ~(ULONG64)0xFFF) >> PAGE_SHIFT;
    }
    else
    {
        RootPfn = (Process->Pcb.DirectoryTableBase[0] >> PAGE_SHIFT);
    }
    if (RootPfn == 0)
        return 0;

    /* Calculate indices */
    PxeIndex = MiAddressToPxi(Address);
    PpeIndex = (((ULONG64)Address >> PPI_SHIFT) & 0x1FF);
    PdeIndex = (((ULONG64)Address >> PDI_SHIFT) & 0x1FF);
    PteIndex = MiAddressToPti(Address);

    /* Reserve a system PTE for mapping */
    MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
    if (!MappingPte)
        return 0;

    /* Map the root table (L0) */
    MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte, MappingPte, MM_READWRITE, RootPfn);
    MI_MAKE_DIRTY_PAGE(&MapPte);
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Check L0 (PXE) */
    if (!MappedPage[PxeIndex].u.Hard.Valid)
        goto Cleanup;
    PpePfn = MappedPage[PxeIndex].u.Hard.PageFrameNumber;

    /* Remap to L1 (PPE table) */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = PpePfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Check L1 (PPE) */
    if (!MappedPage[PpeIndex].u.Hard.Valid)
        goto Cleanup;
    PdePfn = MappedPage[PpeIndex].u.Hard.PageFrameNumber;

    /* Remap to L2 (PDE table) */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = PdePfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Check L2 (PDE) */
    if (!MappedPage[PdeIndex].u.Hard.Valid)
        goto Cleanup;
    PtePfn = MappedPage[PdeIndex].u.Hard.PageFrameNumber;

    /* Remap to L3 (PTE table) */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = PtePfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Check L3 (PTE) and get the page PFN */
    if (MappedPage[PteIndex].u.Hard.Valid)
        PagePfn = MappedPage[PteIndex].u.Hard.PageFrameNumber;

Cleanup:
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

    return PagePfn;
}

/*
 * ARM64-specific helper to create page table entries in TTBR0's hierarchy
 * and write a demand-zero PTE for a user address.
 *
 * This is necessary because on ARM64 with TTBR0/TTBR1 split:
 * - The self-mapping (via MiAddressToPte) is in TTBR1's hierarchy
 * - User addresses are translated via TTBR0's hierarchy
 * - Writing a PTE via self-mapping only updates TTBR1, not TTBR0
 *
 * This function walks TTBR0 directly and creates page tables as needed,
 * then writes the demand-zero PTE. It also writes to TTBR1's self-mapping
 * to keep it in sync for subsequent kernel operations.
 *
 * Returns TRUE if successful, FALSE on failure.
 */
BOOLEAN
MiArm64WriteDemandZeroPteToTtbr0(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ MMPTE DemandZeroPte)
{
    PFN_NUMBER RootPfn, L1Pfn, L2Pfn, L3Pfn;
    PFN_NUMBER NewPagePfn;
    PMMPTE MappingPte;
    PMMPTE MappedPage;
    MMPTE MapPte, TempPte, NewTableEntry;
    ULONG L0Index, L1Index, L2Index, L3Index;
    ULONG64 Ttbr0;
    KIRQL OldIrql;
    ULONG Color;
    BOOLEAN Success = FALSE;

    UNREFERENCED_PARAMETER(Process);
    ASSERT(Address < MmSystemRangeStart);

    /* Get the root table PFN from current TTBR0 */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPfn = (Ttbr0 & ~(ULONG64)0xFFF) >> PAGE_SHIFT;
    if (RootPfn == 0)
        return FALSE;

    /* Calculate indices for each level */
    L0Index = MiAddressToPxi(Address);
    L1Index = (((ULONG64)Address >> PPI_SHIFT) & 0x1FF);
    L2Index = (((ULONG64)Address >> PDI_SHIFT) & 0x1FF);
    L3Index = MiAddressToPti(Address);

    /* Reserve a system PTE for mapping */
    MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
    if (!MappingPte)
        return FALSE;

    /* Build the PTE template for mapping physical pages into kernel space */
    MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte, MappingPte, MM_READWRITE, RootPfn);
    MI_MAKE_DIRTY_PAGE(&MapPte);

    /* Build the table entry template for new page table descriptors */
    NewTableEntry.u.Long = 0;
    NewTableEntry.u.Hard.Valid = 1;
    NewTableEntry.u.Hard.NotLargePage = 1;  /* Table descriptor */
    NewTableEntry.u.Hard.Accessed = 1;
    NewTableEntry.u.Hard.Shareability = 3;  /* Inner Shareable */
    MI_SET_PTE_ATTR_INDEX(&NewTableEntry, MI_ARM64_MAIR_NORMAL_WB_IDX);

    /*
     * Walk TTBR0 page tables and create missing levels.
     * We work from L0 (root) down to L3 (page table).
     */

    /* === L0 -> L1 === */
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L0Index].u.Long & 1))
    {
        /* L1 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            if (!NewPagePfn)
                goto Cleanup;
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        /* Write L0 entry pointing to new L1 table */
        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MI_ARM64_STORE_TABLE_ENTRY("MiArm64WriteDemandZeroPteToTtbr0.L0",
                                   Address,
                                   0,
                                   MappedPage,
                                   L0Index,
                                   TempPte);
        ASSERT((MappedPage[L0Index].u.Long & 0x3ULL) == 0x3ULL);
        __asm__ __volatile__("dsb sy" ::: "memory");
    }
    L1Pfn = MappedPage[L0Index].u.Hard.PageFrameNumber;

    /* === L1 -> L2 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L1Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L1Index].u.Long & 1))
    {
        /* L2 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            if (!NewPagePfn)
                goto Cleanup;
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        /* Write L1 entry pointing to new L2 table */
        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MI_ARM64_STORE_TABLE_ENTRY("MiArm64WriteDemandZeroPteToTtbr0.L1",
                                   Address,
                                   1,
                                   MappedPage,
                                   L1Index,
                                   TempPte);
        ASSERT((MappedPage[L1Index].u.Long & 0x3ULL) == 0x3ULL);
        __asm__ __volatile__("dsb sy" ::: "memory");

    }
    L2Pfn = MappedPage[L1Index].u.Hard.PageFrameNumber;

    /* === L2 -> L3 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L2Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L2Index].u.Long & 1))
    {
        /* L3 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            if (!NewPagePfn)
                goto Cleanup;
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        /* Write L2 entry pointing to new L3 table */
        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MI_ARM64_STORE_TABLE_ENTRY("MiArm64WriteDemandZeroPteToTtbr0.L2",
                                   Address,
                                   2,
                                   MappedPage,
                                   L2Index,
                                   TempPte);
        ASSERT((MappedPage[L2Index].u.Long & 0x3ULL) == 0x3ULL);
        __asm__ __volatile__("dsb sy" ::: "memory");

    }
    L3Pfn = MappedPage[L2Index].u.Hard.PageFrameNumber;

    /* === Write final PTE to L3 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L3Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Write the demand-zero PTE */
    if (MappedPage[L3Index].u.Long == 0)
    {
        MI_ARM64_STORE_TABLE_ENTRY("MiArm64WriteDemandZeroPteToTtbr0.L3",
                                   Address,
                                   3,
                                   MappedPage,
                                   L3Index,
                                   DemandZeroPte);
        __asm__ __volatile__("dsb sy" ::: "memory");
        Success = TRUE;

    }

Cleanup:
    /* Cleanup */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

    /* TLB flush for the user address */
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");

    return Success;
}

/*
 * ARM64-specific helper to write a PTE value to a user address's page table
 * entry directly in TTBR0's hierarchy.
 *
 * This is needed because on ARM64:
 * - The self-mapping (MiAddressToPte, etc.) is in TTBR1's hierarchy
 * - User addresses are translated via TTBR0's hierarchy
 * - Writing to self-mapping only updates TTBR1, not TTBR0
 *
 * This function walks TTBR0, finds the L3 page table entry for the given
 * user address, and writes the PTE value to it.
 *
 * Returns TRUE on success, FALSE on failure (e.g., page tables don't exist).
 */
BOOLEAN
MiArm64WritePteToTtbr0(
    _In_ PVOID UserVirtualAddress,
    _In_ MMPTE PteValue)
{
    PFN_NUMBER RootPfn, L1Pfn, L2Pfn, L3Pfn;
    PFN_NUMBER NewPagePfn;
    PMMPTE MappingPte;
    PMMPTE MappedPage;
    MMPTE MapPte, TempPte, NewTableEntry;
    ULONG L0Index, L1Index, L2Index, L3Index;
    ULONG64 Ttbr0;
    KIRQL OldIrql;
    ULONG Color;
    BOOLEAN Success = FALSE;

    ASSERT(UserVirtualAddress < MmSystemRangeStart);

    /* Get the root table PFN from current TTBR0 */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPfn = (Ttbr0 & ~(ULONG64)0xFFF) >> PAGE_SHIFT;
    if (RootPfn == 0)
        return FALSE;

    /* Calculate indices for each level */
    L0Index = MiAddressToPxi(UserVirtualAddress);
    L1Index = (((ULONG64)UserVirtualAddress >> PPI_SHIFT) & 0x1FF);
    L2Index = (((ULONG64)UserVirtualAddress >> PDI_SHIFT) & 0x1FF);
    L3Index = MiAddressToPti(UserVirtualAddress);

    /* Reserve a system PTE for mapping */
    MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
    if (!MappingPte)
        return FALSE;

    /* Build the PTE template for mapping physical pages into kernel space */
    MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte, MappingPte, MM_READWRITE, RootPfn);
    MI_MAKE_DIRTY_PAGE(&MapPte);

    /* Build the table entry template for new page table descriptors */
    NewTableEntry.u.Long = 0;
    NewTableEntry.u.Hard.Valid = 1;
    NewTableEntry.u.Hard.NotLargePage = 1;  /* Table descriptor */
    NewTableEntry.u.Hard.Accessed = 1;
    NewTableEntry.u.Hard.Shareability = 3;  /* Inner Shareable */
    MI_SET_PTE_ATTR_INDEX(&NewTableEntry, MI_ARM64_MAIR_NORMAL_WB_IDX);

    /*
     * Walk TTBR0 page tables and create missing levels.
     * We work from L0 (root) down to L3 (page table).
     */

    /* === L0 -> L1 === */
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L0Index].u.Long & 1))
    {
        /* L1 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            if (!NewPagePfn)
                goto Cleanup;
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MI_ARM64_STORE_TABLE_ENTRY("MiArm64WritePteToTtbr0.L0",
                                   UserVirtualAddress,
                                   0,
                                   MappedPage,
                                   L0Index,
                                   TempPte);
        ASSERT((MappedPage[L0Index].u.Long & 0x3ULL) == 0x3ULL);
        __asm__ __volatile__("dsb sy" ::: "memory");
    }
    L1Pfn = MappedPage[L0Index].u.Hard.PageFrameNumber;

    /* === L1 -> L2 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L1Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L1Index].u.Long & 1))
    {
        /* L2 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            if (!NewPagePfn)
                goto Cleanup;
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MI_ARM64_STORE_TABLE_ENTRY("MiArm64WritePteToTtbr0.L1",
                                   UserVirtualAddress,
                                   1,
                                   MappedPage,
                                   L1Index,
                                   TempPte);
        ASSERT((MappedPage[L1Index].u.Long & 0x3ULL) == 0x3ULL);
        __asm__ __volatile__("dsb sy" ::: "memory");
    }
    L2Pfn = MappedPage[L1Index].u.Hard.PageFrameNumber;

    /* === L2 -> L3 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L2Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L2Index].u.Long & 1))
    {
        /* L3 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            if (!NewPagePfn)
                goto Cleanup;
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MI_ARM64_STORE_TABLE_ENTRY("MiArm64WritePteToTtbr0.L2",
                                   UserVirtualAddress,
                                   2,
                                   MappedPage,
                                   L2Index,
                                   TempPte);
        ASSERT((MappedPage[L2Index].u.Long & 0x3ULL) == 0x3ULL);
        __asm__ __volatile__("dsb sy" ::: "memory");
    }
    L3Pfn = MappedPage[L2Index].u.Hard.PageFrameNumber;

    /* === Write PTE to L3 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L3Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Write the PTE value */
    MI_ARM64_STORE_TABLE_ENTRY("MiArm64WritePteToTtbr0.L3",
                               UserVirtualAddress,
                               3,
                               MappedPage,
                               L3Index,
                               PteValue);
    __asm__ __volatile__("dsb sy" ::: "memory");
    Success = TRUE;

Cleanup:
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

    /* TLB flush for the user address */
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");

    return Success;
}

static
ULONG
MiProtectionFromPte(
    _In_ MMPTE Pte);

static
BOOLEAN
MmDeleteVirtualMappingEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical);

NTSTATUS
NTAPI
MmCreateVirtualMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page)
{
    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    if (Page == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!MmIsPageInUse(Page))
    {
        DPRINT1("Page %Ix is not in use (Addr=%p Proc=%p)\n", Page, Address, Process);
#if defined(CONFIG_SMP)
        /*
         * SMP: Page may have been freed by another CPU during a race
         * in the section fault handler. Log and fail gracefully.
         */
        return STATUS_UNSUCCESSFUL;
#else
        KeBugCheck(MEMORY_MANAGEMENT);
#endif
    }

    return MmCreateVirtualMappingUnsafeEx(Process, Address, Protection, Page, FALSE);
}

NTSTATUS
NTAPI
MmCreateVirtualMappingUnsafe(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page)
{
    return MmCreateVirtualMappingUnsafeEx(Process, Address, Protection, Page, FALSE);
}

NTSTATUS
NTAPI
MmCreatePhysicalMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page)
{
    return MmCreateVirtualMappingUnsafeEx(Process, Address, Protection, Page, TRUE);
}

static
NTSTATUS
MmCreateVirtualMappingUnsafeEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical)
{
    PMMPTE PointerPte;
    MMPTE TempPte;
    ULONG ProtectionMask;

    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    /* Reject PFN 0 - physical page 0 is reserved and should never be mapped */
    if (Page == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProtectionMask = MiMakeProtectionMask(Protection);
    ASSERT(ProtectionMask != MM_INVALID_PROTECTION);
    ASSERT(ProtectionMask != MM_NOACCESS);
    ASSERT(ProtectionMask != MM_ZERO_ACCESS);

    if (Process == NULL)
    {
        ASSERT(Address >= MmSystemRangeStart);
        ASSERT(ProtectionMask != MM_WRITECOPY);
        ASSERT(ProtectionMask != MM_EXECUTE_WRITECOPY);

        /*
         * For kernel address mappings, we need to ensure the page table entry
         * (PTE) for the target address is accessible. This means the page table
         * page containing the PTE must be valid.
         *
         * NOTE: We pass MiAddressToPte(Address) - the PTE's virtual address -
         * NOT the target Address itself. MiMakeSystemAddressValid is designed
         * to fault in PAGE TABLE addresses, not arbitrary addresses.
         *
         * If we passed Address here, it would cause infinite recursion during
         * page fault handling for section views:
         *   MmNotPresentFaultSectionView -> MmCreateVirtualMapping ->
         *   MiMakeSystemAddressValid(Address) -> MmAccessFault(Address) ->
         *   MmNotPresentFaultSectionView -> ... (infinite loop)
         */
        MiMakeSystemAddressValid(MiAddressToPte(Address), PsGetCurrentProcess());
    }
    else
    {
        /*
         * ARM64 user-space page table creation.
         *
         * On ARM64, the self-mapping mechanism requires existing page table entries
         * to walk the hierarchy. For a new process, user-space PXE entries (indices
         * 0-255) are zero-initialized. When we need to create a mapping, we must:
         *
         * 1. Check if PXE exists (can access PXE directly as it's in kernel space)
         * 2. If PXE doesn't exist, allocate a PPE page and write PXE
         * 3. Use a system PTE to map the PPE page and check/create PPE entry
         * 4. Use a system PTE to map the PDE page and check/create PDE entry
         *
         * We cannot use self-mapping addresses (MiAddressToPpe, MiAddressToPde) to
         * check validity of intermediate levels because the self-mapping walk would
         * fail on the zero entries.
         */
        PMMPTE MappingPte = NULL;
        PMMPTE MappedPage;
        MMPTE MapPte, TempPte;
        PFN_NUMBER PxePfn = 0, PpePfn = 0, PdePfn = 0, PtePfn = 0;
        ULONG PxeIndex, PpeIndex, PdeIndex;
        KIRQL OldIrql;
        ULONG Color;

        ASSERT(Address < MmSystemRangeStart);
        ASSERT(Process == PsGetCurrentProcess());

        MiLockProcessWorkingSet(Process, PsGetCurrentThread());
        MiArm64TraceUserMapState("begin", Process, Address, Page, Protection, 0);

        /* Get system PTE for temporary mapping */
        MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
        if (!MappingPte)
        {
            MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Calculate indices for each level */
        PxeIndex = MiAddressToPxi(Address);
        PpeIndex = (((ULONG64)Address >> PPI_SHIFT) & 0x1FF);
        PdeIndex = (((ULONG64)Address >> PDI_SHIFT) & 0x1FF);

        /*
         * Level 0 (PXE) - CRITICAL: We must map the CURRENTLY ACTIVE TTBR0
         * table, not DirectoryTableBase. On ARM64:
         *
         * - TTBR0 controls user address translation (addresses < 0x0000FFFFFFFFFFFF)
         * - TTBR1 controls kernel address translation (addresses >= 0xFFFF...)
         * - DirectoryTableBase[0] for System/Idle process is set from TTBR1 during
         *   boot, which means it points to KERNEL page tables, not user tables!
         *
         * The CPU uses TTBR0 to translate user addresses, so we MUST write our
         * page table entries to the TTBR0 hierarchy, not DirectoryTableBase.
         *
         * Note: MiAddressToPxe() returns a self-mapping address in TTBR1 (kernel
         * space) which cannot access user page tables in TTBR0.
         */
        {
            ULONG64 Ttbr0Actual;
            __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0Actual));
            PxePfn = (Ttbr0Actual & ~(ULONG64)0xFFF) >> PAGE_SHIFT;
        }

        /* Map the user's root table (L0) via system PTE */
        MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte, MappingPte, MM_READWRITE, PxePfn);
        MI_MAKE_DIRTY_PAGE(&MapPte);
        MI_WRITE_VALID_PTE(MappingPte, MapPte);
        MappedPage = MiPteToAddress(MappingPte);

        if (!MappedPage[PxeIndex].u.Hard.Valid)
        {
            /* Allocate page for PPE table (L1) */
            OldIrql = MiAcquirePfnLock();
            Color = MI_GET_NEXT_PROCESS_COLOR(Process);
            PpePfn = MiRemoveZeroPageSafe(Color);
            if (!PpePfn)
            {
                PpePfn = MiRemoveAnyPage(Color);
                if (!PpePfn)
                {
                    MiReleasePfnLock(OldIrql);
                    /* Cleanup: invalidate mapping, release PTE */
                    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
                    KeInvalidateTlbEntry(MappedPage);
                    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
                    MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
                    return STATUS_NO_MEMORY;
                }
                MiReleasePfnLock(OldIrql);
                MiZeroPhysicalPage(PpePfn);
            }
            else
            {
                MiReleasePfnLock(OldIrql);
            }

            /*
             * Use MiInitializePfnForOtherProcess because we can't use MiInitializePfn -
             * it would try to dereference the self-mapping address to read OriginalPte,
             * which doesn't exist yet. MiInitializePfnForOtherProcess just takes the
             * raw PTE address and parent frame.
             *
             * Pass the self-mapping address for bookkeeping (PteAddress field) but
             * the parent frame is the root table's PFN.
             */
            MiInitializePfnForOtherProcess(PpePfn, MiAddressToPxe(Address), PxePfn);
            Process->NumberOfPrivatePages++;

            /* Write PXE entry into the mapped user root table */
            TempPte = ValidKernelPte;
            TempPte.u.Hard.PageFrameNumber = PpePfn;
            TempPte.u.Hard.Owner = 0;  /* No APTable restriction */
            MI_ARM64_STORE_TABLE_ENTRY("MmCreateVirtualMappingUnsafeEx.L0",
                                       Address,
                                       0,
                                       MappedPage,
                                       PxeIndex,
                                       TempPte);
            ASSERT((MappedPage[PxeIndex].u.Long & 0x3ULL) == 0x3ULL);
            __asm__ __volatile__("dsb ishst" ::: "memory");

            /*
             * Keep the merged PXE root coherent for the current process.
             * Self-map walks (MiAddressToPxe/Pte) read through this merged page.
             */
            if (MiArm64PxeMergedPfn[0] != 0)
            {
                volatile ULONG64 *MergedL0;
                ULONG64 OldMergedEntry;
                MergedL0 = (volatile ULONG64 *)(KSEG0_BASE |
                                                ((ULONG64)MiArm64PxeMergedPfn[0] << PAGE_SHIFT));
                OldMergedEntry = MergedL0[PxeIndex];
                MiArm64TraceUserTableStore("MmCreateVirtualMappingUnsafeEx.MergedL0",
                                           Address,
                                           0,
                                           PxeIndex,
                                           OldMergedEntry,
                                           TempPte.u.Long);
                MergedL0[PxeIndex] = TempPte.u.Long;
                ASSERT((MergedL0[PxeIndex] & 0x3ULL) == 0x3ULL);
                __asm__ __volatile__("dsb ishst" ::: "memory");
            }

            /*
             * Bug #44: Track new L1 table in L0's UsedPageTableEntries.
             * MiDeletePde cascade will decrement this during process teardown.
             */
            {
                PMMPFN PfnL0 = MI_PFN_ELEMENT(PxePfn);
                PfnL0->OriginalPte.u.Soft.UsedPageTableEntries++;
                ASSERT(PfnL0->OriginalPte.u.Soft.UsedPageTableEntries <= PTE_PER_PAGE);
            }
        }
        else
        {
            /* PXE exists, get the PPE PFN from it */
            PpePfn = MappedPage[PxeIndex].u.Hard.PageFrameNumber;
        }


        /* Invalidate the mapping before reusing the system PTE for PPE level */
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(MappedPage);

        /* Level 1 (PPE) - map the PPE page via system PTE to check/write it */
        MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte, MappingPte, MM_READWRITE, PpePfn);
        MI_MAKE_DIRTY_PAGE(&MapPte);
        MI_WRITE_VALID_PTE(MappingPte, MapPte);
        MappedPage = MiPteToAddress(MappingPte);

        if (!MappedPage[PpeIndex].u.Hard.Valid)
        {
            /* Allocate page for PDE table (L2) */
            BOOLEAN ReleasePfnLock = TRUE;
            {
                extern volatile LONG MiArm64PfnLockDepth[MAXIMUM_PROCESSORS];
                ULONG CpuIndex = KeGetCurrentProcessorNumber();

                /*
                 * Avoid deadlocking on recursive PFN lock acquisition on UP during
                 * early user TTBR0 bring-up. If the PFN lock is already held on this
                 * CPU, reuse it and do not release it here.
                 */
                if (CpuIndex < MAXIMUM_PROCESSORS && MiArm64PfnLockDepth[CpuIndex] > 0)
                {
                    OldIrql = DISPATCH_LEVEL;
                    ReleasePfnLock = FALSE;
                }
                else
                {
                    OldIrql = MiAcquirePfnLock();
                }
            }
            Color = MI_GET_NEXT_PROCESS_COLOR(Process);
            PdePfn = MiRemoveZeroPageSafe(Color);
            if (!PdePfn)
            {
                PdePfn = MiRemoveAnyPage(Color);
                if (!PdePfn)
                {
                    if (ReleasePfnLock) MiReleasePfnLock(OldIrql);
                    /* Cleanup: invalidate mapping, release PTE */
                    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
                    KeInvalidateTlbEntry(MappedPage);
                    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
                    MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
                    return STATUS_NO_MEMORY;
                }
                if (ReleasePfnLock) MiReleasePfnLock(OldIrql);
                MiZeroPhysicalPage(PdePfn);
            }
            else
            {
                if (ReleasePfnLock) MiReleasePfnLock(OldIrql);
            }

            /*
             * Use MiInitializePfnForOtherProcess - the PTE address is the self-mapping
             * address (for bookkeeping), and the parent frame is the PPE page.
             */
            MiInitializePfnForOtherProcess(PdePfn, MiAddressToPpe(Address), PpePfn);
            Process->NumberOfPrivatePages++;

            /* Write PPE entry into the mapped page */
            TempPte = ValidKernelPte;
            TempPte.u.Hard.PageFrameNumber = PdePfn;
            TempPte.u.Hard.Owner = 0;  /* No APTable restriction */
            MI_ARM64_STORE_TABLE_ENTRY("MmCreateVirtualMappingUnsafeEx.L1",
                                       Address,
                                       1,
                                       MappedPage,
                                       PpeIndex,
                                       TempPte);
            ASSERT((MappedPage[PpeIndex].u.Long & 0x3ULL) == 0x3ULL);

            /*
             * Bug #44: Track new L2 table in L1's UsedPageTableEntries.
             * MiDeletePde cascade will decrement this during process teardown.
             */
            {
                PMMPFN PfnL1 = MI_PFN_ELEMENT(PpePfn);
                PfnL1->OriginalPte.u.Soft.UsedPageTableEntries++;
                ASSERT(PfnL1->OriginalPte.u.Soft.UsedPageTableEntries <= PTE_PER_PAGE);
            }
        }
        else
        {
            /* PPE exists, get PDE PFN */
            PdePfn = MappedPage[PpeIndex].u.Hard.PageFrameNumber;
        }

        /* Invalidate the mapping before reusing the system PTE */
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(MappedPage);

        /* Level 2 (PDE) - map the PDE page via system PTE */
        MapPte.u.Hard.PageFrameNumber = PdePfn;
        MI_WRITE_VALID_PTE(MappingPte, MapPte);
        MappedPage = MiPteToAddress(MappingPte);

        /*
         * ARM64: Check if the existing PDE is a 2MB block descriptor (from FreeLoader's
         * identity mapping) rather than a table descriptor. If so, we must break it up.
         *
         * On ARM64:
         * - Block descriptor (2MB at L2): Valid=1, NotLargePage=0, bits[1:0]=01
         * - Table descriptor: Valid=1, NotLargePage=1, bits[1:0]=11
         * - Invalid: Valid=0, bit[0]=0
         *
         * FreeLoader creates 2MB block descriptors for identity mapping in user space.
         * If the kernel tries to create a 4KB page mapping in the same range, we must:
         * 1. Detect the block descriptor (Valid=1 but NotLargePage=0)
         * 2. Clear it (we don't need to preserve the identity mapping)
         * 3. Allocate a fresh L3 page table
         */
        if (MappedPage[PdeIndex].u.Hard.Valid && !MappedPage[PdeIndex].u.Hard.NotLargePage)
        {
            /* This is a 2MB block descriptor - clear it */
            /* Invalidate TLB before clearing the block descriptor */
            __asm__ __volatile__("dsb ishst\n\t"
                                 "tlbi vaale1is, %0\n\t"
                                 "dsb ish\n\t"
                                 "isb"
                                 :: "r"((ULONG_PTR)Address >> PAGE_SHIFT) : "memory");

            /* Clear the 2MB block descriptor */
            MI_ARM64_STORE_TABLE_ENTRY64("MmCreateVirtualMappingUnsafeEx.L2ClearBlock",
                                         Address,
                                         2,
                                         MappedPage,
                                         PdeIndex,
                                         0ULL);
            ASSERT(MappedPage[PdeIndex].u.Long == 0);

            /* Ensure the clear is visible before proceeding */
            __asm__ __volatile__("dsb ishst" ::: "memory");

            /* Invalidate the entire 2MB range that was covered by this block */
            __asm__ __volatile__("tlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
        }

        if (!MappedPage[PdeIndex].u.Hard.Valid)
        {
            /* Allocate page for PTE table (L3) */
            OldIrql = MiAcquirePfnLock();
            Color = MI_GET_NEXT_PROCESS_COLOR(Process);
            PtePfn = MiRemoveZeroPageSafe(Color);
            if (!PtePfn)
            {
                PtePfn = MiRemoveAnyPage(Color);
                if (!PtePfn)
                {
                    MiReleasePfnLock(OldIrql);
                    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
                    KeInvalidateTlbEntry(MappedPage);
                    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
                    MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
                    return STATUS_NO_MEMORY;
                }
                MiReleasePfnLock(OldIrql);
                MiZeroPhysicalPage(PtePfn);
            }
            else
            {
                MiReleasePfnLock(OldIrql);
            }

            /*
             * Use MiInitializePfnForOtherProcess - the PTE address is the self-mapping
             * address (for bookkeeping), and the parent frame is the PDE page.
             */
            MiInitializePfnForOtherProcess(PtePfn, MiAddressToPde(Address), PdePfn);
            Process->NumberOfPrivatePages++;

            /* Write PDE entry */
            TempPte = ValidKernelPte;
            TempPte.u.Hard.PageFrameNumber = PtePfn;
            TempPte.u.Hard.Owner = 0;  /* No APTable restriction */
            MI_ARM64_STORE_TABLE_ENTRY("MmCreateVirtualMappingUnsafeEx.L2",
                                       Address,
                                       2,
                                       MappedPage,
                                       PdeIndex,
                                       TempPte);
            ASSERT((MappedPage[PdeIndex].u.Long & 0x3ULL) == 0x3ULL);

            /*
             * Bug #44: Track new L3 table in L2's UsedPageTableEntries.
             * MiDeletePde cascade will decrement this during process teardown.
             */
            {
                PMMPFN PfnL2 = MI_PFN_ELEMENT(PdePfn);
                PfnL2->OriginalPte.u.Soft.UsedPageTableEntries++;
                ASSERT(PfnL2->OriginalPte.u.Soft.UsedPageTableEntries <= PTE_PER_PAGE);
            }
        }
        else
        {
            /* PDE exists, get PTE PFN */
            PtePfn = MappedPage[PdeIndex].u.Hard.PageFrameNumber;
        }

        /* Invalidate the mapping before reusing for PTE table */
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(MappedPage);

        /*
         * Now map the PTE table page via system PTE to write the final PTE.
         * We cannot use the self-mapping (MiAddressToPte) because the self-mapping
         * chain doesn't have valid entries for user address page tables.
         */
        {
            ULONG PteIndex = MiAddressToPti(Address);
            MMPTE FinalPte;
            ULONG64 OldUserPte;

            MapPte.u.Hard.PageFrameNumber = PtePfn;
            MI_WRITE_VALID_PTE(MappingPte, MapPte);
            MappedPage = MiPteToAddress(MappingPte);

            OldUserPte = MappedPage[PteIndex].u.Long;
            MiArm64TraceUserMapState("l3-before", Process, Address, Page, Protection, OldUserPte);

            /*
             * Build the final PTE for the user page.
             *
             * CRITICAL ARM64 FIX: We cannot use MI_MAKE_HARDWARE_PTE directly because
             * it calls MiDetermineUserGlobalPteMask() which checks if the PTE address
             * is in user PTE space to set the Owner bit. However, we are writing to
             * user page tables via a SYSTEM PTE (MappedPage), so the PTE address is
             * in kernel space, and MiDetermineUserGlobalPteMask would NOT set Owner=1.
             *
             * Without Owner=1 (AP[0]=1), the ARM64 MMU treats the page as EL1-only,
             * preventing user mode (EL0) from accessing it. This causes writes to
             * appear to fail (reads return garbage/0xAA poison bytes).
             *
             * Solution: Manually build the PTE with proper user-accessible attributes:
             * - Valid=1, NotLargePage=1 for L3 page descriptor (bits [1:0]=0b11)
             * - Owner=1 for user access (AP[0]=1 allows EL0 access)
             * - NonGlobal=1 so the mapping stays process-local in the TLB
             * - NotDirty=0 for writable pages (AP[1]=0 allows writes)
             * - Accessed=1 (AF bit) required by ARM64
             * - Protection mask for cache and execute permissions
             */
            FinalPte.u.Long = 0;
            FinalPte.u.Hard.Valid = 1;
            FinalPte.u.Hard.NotLargePage = 1;  /* ARM64 L3 page descriptor */
            FinalPte.u.Hard.Owner = 1;         /* User accessible (AP[0]=1) */
            FinalPte.u.Hard.Accessed = 1;      /* Access Flag must be set */
            FinalPte.u.Hard.Shareability = 3;  /* Inner Shareable for SMP coherency */
            FinalPte.u.Hard.NonGlobal = 1;     /* EL0 mappings must not be global */
            FinalPte.u.Hard.PageFrameNumber = Page;
            FinalPte.u.Long |= MmProtectToPteMask[ProtectionMask];

            /*
             * ARM64 Dirty Bit Management:
             *
             * On ARM64, the NotDirty bit (bit 7, AP[1]) controls both write permission
             * and dirty tracking:
             * - NotDirty=0 (AP[1]=0): Page is read-write, considered "dirty"
             * - NotDirty=1 (AP[1]=1): Page is read-only, considered "not dirty"
             *
             * For WRITABLE pages: Set NotDirty=0 to allow writes.
             * For READ-ONLY pages: Set NotDirty=1 to mark as clean. This is CRITICAL
             * because MmDeleteVirtualMapping checks NotDirty to determine if the page
             * was written to. If we leave NotDirty=0 for read-only pages, they will
             * incorrectly report as dirty when unmapped, causing assertions in
             * MmUnsharePageEntrySectionSegment.
             */
            if (FinalPte.u.Hard.Writable)
            {
                MI_MAKE_DIRTY_PAGE(&FinalPte);  /* NotDirty=0 for writable */
            }
            else
            {
                /* Read-only page: set NotDirty=1 to mark as clean */
                FinalPte.u.Hard.NotDirty = 1;
            }

            if (!IsPhysical)
            {
                KIRQL PfnOldIrql = MiAcquirePfnLock();
                PMMPFN Pfn1 = MiGetPfnEntry(Page);

                Pfn1->u2.ShareCount++;
                Pfn1->u3.e1.PageLocation = ActiveAndValid;
                MiArm64SyncMappedPfnCacheAttribute(Process,
                                                   Address,
                                                   Page,
                                                   FinalPte);

                /*
                 * Increment the L3 page table's ShareCount.
                 *
                 * Each valid PTE in an L3 page table holds one ShareCount reference.
                 * MiInitializePfnForOtherProcess sets ShareCount=1 (the PDE parent
                 * reference) when a NEW L3 table is created above. Each additional
                 * PTE written into the table must add +1.
                 *
                 * This centralized increment replaces the scattered per-path
                 * increments that were in pagfault.c. All callers of
                 * MmCreateVirtualMappingUnsafe (ARM3 demand-zero, ARM3 section
                 * proto, RosMM section) now get correct L3 ShareCount tracking.
                 *
                 * The matching decrement is in MmDeleteVirtualMappingEx (for
                 * RosMM teardown) and MiDeletePte (for ARM3 teardown).
                 */
                {
                    PMMPFN PtePfn1 = MiGetPfnEntry(PtePfn);
                    if (PtePfn1 != NULL)
                    {
                        PtePfn1->u2.ShareCount++;
                    }
                }

                MiReleasePfnLock(PfnOldIrql);
            }

            /*
             * Check for mapping collision.
             *
             * ARM64 Cycle 57: FreeLoader creates identity mappings in user space (TTBR0)
             * for its own use during boot. When the kernel starts, these identity mappings
             * may still exist. If a section view is mapped at a VA that has an existing
             * identity mapping, we must clear the old PTE before installing the new one.
             *
             * This handles the case where:
             * - FreeLoader identity-mapped physical address 0xFF000000
             * - Kernel maps ntdll.dll section view at VA 0xFF000000
             * - Old PTE would cause reads to return identity-mapped (stale) data
             *
             * Solution: Invalidate TLB and clear the existing PTE before writing the new one.
             */
            if (OldUserPte != 0)
            {
                MMPTE OldPteContents;
                OldPteContents.u.Long = OldUserPte;

                /*
                 * Only clear entries that are actual valid page mappings.
                 * Non-zero software PTEs (demand-zero/decommit/proto markers)
                 * must not be treated as stale identity mappings.
                 */
                if (OldPteContents.u.Hard.Valid && OldPteContents.u.Hard.NotLargePage)
                {
                    DPRINT("[arm64] Clearing stale valid PTE at %p (old PTE=0x%llx)\n",
                            Address, OldUserPte);

                    /* Invalidate TLB for this VA first - use VAALE1IS to flush ALL ASIDs including Global entries */
                    __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> PAGE_SHIFT) : "memory");
                    __asm__ __volatile__("dsb ish" ::: "memory");
                    __asm__ __volatile__("isb" ::: "memory");

                    /*
                     * Avoid DC IVAC through the user VA itself. On ARM64/QEMU, cache
                     * maintenance through EL0 aliases has repeatedly surfaced later as
                     * deferred user-mode SErrors. Invalidate the old mapping through its
                     * stable direct-map PFN alias instead.
                     */
                    MiArm64InvalidatePageByPfnAlias(OldPteContents.u.Hard.PageFrameNumber);

                    /* Now clear the old valid PTE */
                    MI_ARM64_STORE_TABLE_ENTRY64("MmCreateVirtualMappingUnsafeEx.L3ClearOld",
                                                 Address,
                                                 3,
                                                 MappedPage,
                                                 PteIndex,
                                                 0ULL);
                    ASSERT(MappedPage[PteIndex].u.Long == 0);
                    __asm__ __volatile__("dsb ishst" ::: "memory");
                }
            }

            /*
             * ARM64 cache maintenance for user-space section mappings.
             *
             * DC CIVAC requires write permission at the current EL. For read-only
             * pages (e.g., .rdata sections containing DLL export tables), the user
             * PTE has NotDirty=1 (AP[1]=1, EL1 read-only), so DC CIVAC through
             * the user VA would fault or silently fail on QEMU, leaving stale
             * cache data that causes non-deterministic import resolution failures
             * (LdrpNameToOrdinal reads garbage from the export name table).
             *
             * Solution: write the final user PTE, release the temporary page-table
             * mapping, then publish the data page through its stable KSEG0 PFN alias.
             * That alias is EL1 read-write and does not depend on the freshly
             * created EL0 mapping, so cache maintenance stays off the user VA.
             */

            /* Step 1: Write the final PTE to the L3 table (through system PTE) */
            MI_ARM64_STORE_TABLE_ENTRY("MmCreateVirtualMappingUnsafeEx.L3Final",
                                       Address,
                                       3,
                                       MappedPage,
                                       PteIndex,
                                       FinalPte);
            ASSERT(MappedPage[PteIndex].u.Long == FinalPte.u.Long);
            __asm__ __volatile__("dsb sy" ::: "memory");

            /*
             * Bug #60 FIX: Increment UsedPageTableEntries for the L3 page table.
             *
             * MiDecrementPageTableReferences (called from MiDeleteVirtualAddresses
             * and NtFreeVirtualMemory) decrements UsedPageTableEntries for every
             * non-zero PTE it removes. Without a matching increment here, the
             * count underflows to (USHORT)-1 and MiDeletePde never fires (or
             * worse, the count wraps through zero and triggers premature page
             * table freeing, causing L2e/L3e=0x0 translation faults).
             *
             * Only increment for genuinely NEW entries (OldUserPte == 0).
             * Replacement entries (OldUserPte != 0) were either:
             * (a) FreeLoader identity mappings (never tracked, cleared above), or
             * (b) Previous MmCreateVirtualMappingUnsafeEx entries (already tracked).
             * Case (b) is already counted, so incrementing again would leak.
             * Case (a) is rare (only during early process setup); the FreeLoader
             * PTE is cleared at line ~1390 but never decremented, so incrementing
             * once for the replacement PTE correctly accounts for the eventual
             * decrement in MiDeleteVirtualAddresses.
             *
             * We increment directly on the PFN using PtePfn (already known from
             * the page table walk above) to avoid a redundant KSEG0 walk that
             * MiIncrementPageTableReferences would perform.
             */
            if (OldUserPte == 0)
            {
                PMMPFN PtePfn1 = MiGetPfnEntry(PtePfn);
                if (PtePfn1 != NULL)
                {
                    PtePfn1->OriginalPte.u.Soft.UsedPageTableEntries++;
                    ASSERT(PtePfn1->OriginalPte.u.Soft.UsedPageTableEntries <= PTE_PER_PAGE);
                }
            }

            /* Step 2: Release L3 table mapping from system PTE */
            MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
            KeInvalidateTlbEntry(MappedPage);
            MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

            /* Step 3: Publish the newly mapped page through its stable PFN alias */
            {
                BOOLEAN SweepIcache = (FinalPte.u.Hard.UserNoExecute == 0);

                MiArm64PublishPageByPfnAlias(Page, SweepIcache);

                if (MiArm64ShouldTraceUserMap(Process, Address))
                {
                    LONG TraceIndex = InterlockedIncrement(&MiArm64UserPublishTraceCount);
                    if (TraceIndex <= 40)
                    {
                        DPRINT1("[arm64][UPUB] alias[%ld] proc=%.16s va=%p page=%Ix exec=%u old=0x%016llx\n",
                                TraceIndex,
                                Process ? Process->ImageFileName : "<none>",
                                Address,
                                Page,
                                SweepIcache ? 1u : 0u,
                                (unsigned long long)OldUserPte);
                    }
                }
            }

            /*
             * ARM64 requires aggressive TLB invalidation after modifying page tables.
             * The CPU can cache "negative" translation results (translation faults),
             * so we must ensure all levels of the page table walk are re-fetched.
             *
             * Use a full EL1 TLB flush (vmalle1is) to invalidate all cached translations
             * including any cached translation fault entries from the earlier page fault.
             * The sequence is:
             * 1. DSB SY - Ensure all page table writes are complete
             * 2. TLBI VMALLE1IS - Broadcast full TLB invalidation to all cores
             * 3. DSB SY - Wait for TLB invalidation to complete
             * 4. ISB - Synchronize instruction stream
             */
            __asm__ __volatile__("dsb sy" ::: "memory");
            __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
            __asm__ __volatile__("dsb sy" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");
            MiArm64TraceUserMapState("done", Process, Address, Page, Protection, OldUserPte);

            /*
             * No TTBR0 switch needed - we now write page tables directly to the
             * TTBR0 hierarchy (read at the start of this function). This ensures
             * the mapping is immediately visible to the CPU for user address
             * translation without needing to switch TTBR0.
             */

            MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
        }

        return STATUS_SUCCESS;
    }

    /* Kernel address path - uses self-mapping */
    PointerPte = MiAddressToPte(Address);
    MI_MAKE_HARDWARE_PTE(&TempPte, PointerPte, ProtectionMask, Page);

    if (!IsPhysical)
    {
        KIRQL OldIrql = MiAcquirePfnLock();
        PMMPFN Pfn1 = MiGetPfnEntry(Page);

        Pfn1->u2.ShareCount++;
        Pfn1->u3.e1.PageLocation = ActiveAndValid;
        MiArm64SyncMappedPfnCacheAttribute(Process,
                                           Address,
                                           Page,
                                           TempPte);
        MiReleasePfnLock(OldIrql);
    }

    /*
     * ARM64 Cache Coherency for Kernel Mappings - CRITICAL FIX (Cycle 39).
     *
     * For REMAPPINGS (old PTE != 0), we must invalidate the cache before
     * writing the new PTE, because the cache may contain stale data from
     * the previous physical page that was mapped at this VA.
     *
     * For NEW mappings (old PTE == 0), we MUST NOT use DC CIVAC because:
     * 1. The VA has no valid translation - DC CIVAC will cause a Data Abort
     * 2. There's no stale cache data for an unmapped VA anyway
     *
     * Original problem this fixed:
     * When mapping ramdisk pages that were written by the bootloader, if the VA
     * was previously mapped to a DIFFERENT physical page, the cache could have
     * stale data for this VA. This caused "Invalid image DOS signature" errors.
     *
     * DC CIVAC (Clean and Invalidate by VA to PoC):
     * - Writes back any dirty data (preserves previous mappings' writes)
     * - Invalidates cache lines for this VA
     * - Forces subsequent reads to fetch from physical memory
     * - REQUIRES a valid VA translation - faults on unmapped addresses!
     *
     * Order of operations:
     * 1. Check if current PTE is valid (remapping case)
     * 2. If valid: DC CIVAC (invalidate cache for VA) - BEFORE PTE write
     * 3. DSB ISH (ensure cache ops complete)
     * 4. Write new PTE (make mapping valid)
     * 5. TLBI (invalidate TLB)
     * 6. Return to faulting instruction which will now see correct data
     */
    {
        MMPTE OldPte;
        PMMPTE CheckPte;

        /*
         * Read the current PTE to check if this is a remapping.
         * For user addresses, walk TTBR0 physically via KSEG0 (the TTBR0
         * alias can be stale after context switches - Bug #48).
         * For kernel addresses, we can use the self-map.
         */
        if (Address < MmSystemRangeStart)
        {
            ULONG64 OldPteVal;
            MiArm64ReadUserPtePhysically(Address, &OldPteVal, NULL);
            OldPte.u.Long = OldPteVal;
        }
        else
        {
            CheckPte = PointerPte;
            OldPte.u.Long = CheckPte->u.Long;
        }

        /*
         * Only perform cache invalidation if there's an existing valid mapping.
         * DC CIVAC requires a valid VA translation - it will fault on unmapped addresses!
         */
        if (OldPte.u.Long != 0 && OldPte.u.Hard.Valid)
        {
            ULONG64 Ctr;
            ULONG DcacheLineSize;
            ULONG_PTR Va;

            /* Read CTR_EL0 to get D-cache line size */
            __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
            DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);

            /* Invalidate D-cache for the target virtual address */
            Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

            /* Invalidate each cache line in the page */
            for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
            {
                __asm__ __volatile__("dc civac, %0" :: "r"(Va + offset) : "memory");
            }

            /* Ensure cache operations complete BEFORE writing PTE */
            __asm__ __volatile__("dsb ish" ::: "memory");
        }
    }

    /*
     * ARM64 TTBR0 Alias Fix: Write user-space PTEs via TTBR0 alias.
     *
     * On ARM64, user addresses use TTBR0's page tables while kernel addresses
     * use TTBR1's page tables. The self-map at PTE_BASE is in TTBR1's hierarchy.
     *
     * L0[494] in TTBR1 now points to TTBR0's L0 page (the "TTBR0 alias").
     * For user addresses, MiAddressToPteTtbr0() goes through this alias to
     * correctly access TTBR0's page table hierarchy.
     *
     * This replaces the old MiArm64WritePteToTtbr0() workaround.
     */
    if (Address < MmSystemRangeStart)
    {
        /*
         * ARM64 FIX (Bug #48): User PTE was already written correctly via
         * KSEG0 physical mapping in MiArm64MapUserPage (system PTE approach).
         *
         * Previously this code also wrote the PTE through the TTBR0 alias
         * (MiAddressToPteTtbr0), but that alias can be stale after context
         * switches, causing cross-process PTE corruption. Since the PTE is
         * already written to the correct physical L3 table page above, this
         * second write is both redundant and dangerous. Skip it on ARM64.
         */
    }
    else
    {
        ULONG_PTR OldPteValue;

        /*
         * Kernel address: Self-Map is valid for TTBR1.
         *
         * ARM64 FIX: Cannot use InterlockedExchangePte (LDXR/STXR) here.
         * The self-map reinterprets TABLE descriptors as PAGE descriptors at
         * the final level, resulting in incorrect memory attributes (AttrIndex
         * from "Ignored" TABLE bits = 0, likely Device memory). Exclusive
        * monitors (LDXR/STXR) are UNPREDICTABLE on Device memory, causing
        * infinite STXR failures under HVF hardware virtualization.
        *
         * Regular aligned 64-bit stores are naturally atomic on ARM64 and
         * work correctly on any memory type. But the self-map leaf is only an
         * alias: the hardware TTBR1 walker may still be reading the real L3
         * page table page through KSEG0. Mirror the leaf into the TTBR1 walk
         * after the self-map store so section/system-cache faults publish a
         * real hardware-visible mapping, not just a recursive alias update.
         */
        OldPteValue = PointerPte->u.Long;
        PointerPte->u.Long = TempPte.u.Long;
        MiArm64SyncKernelLeafPteWrite(PointerPte);
        __asm__ __volatile__("dsb ishst" ::: "memory");

        if (OldPteValue != 0)
        {
            DPRINT1("Mapping collision at %p\n", Address);
            KeBugCheck(MEMORY_MANAGEMENT);
        }
    }

    /*
     * ARM64: Invalidate the TLB entry for this address.
     * Even for new mappings (where old PTE was 0), the CPU might have a
     * "negative" TLB entry cached from the page fault that triggered this
     * mapping. We must invalidate it so the CPU picks up the new mapping.
     */
    KeInvalidateTlbEntry(Address);

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
MmDeleteVirtualMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page)
{
    return MmDeleteVirtualMappingEx(Process, Address, WasDirty, Page, FALSE);
}

BOOLEAN
NTAPI
MmDeletePhysicalMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page)
{
    return MmDeleteVirtualMappingEx(Process, Address, WasDirty, Page, TRUE);
}

static
BOOLEAN
MmDeleteVirtualMappingEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical)
{
    MMPTE OldPte;
    BOOLEAN ValidPde = FALSE;

    OldPte.u.Long = 0;

    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    if (Process == NULL)
    {
        ASSERT(Address >= MmSystemRangeStart);

        /*
         * For kernel addresses, check if the page table hierarchy is valid.
         * We cannot call MiMakeSystemAddressValid(Address) here because:
         * 1. This function is used during page fault cleanup/unmapping
         * 2. MiMakeSystemAddressValid would try to fault in the address
         * 3. This would cause infinite recursion
         *
         * Instead, use MiIsPdeForAddressValid to check without faulting.
         * If page tables don't exist, there's nothing to delete.
         */
        ValidPde = MiIsPdeForAddressValid(Address);
        if (ValidPde)
        {
            PMMPTE PointerPte = MiAddressToPte(Address);

            OldPte.u.Long = PointerPte->u.Long;
            if (OldPte.u.Hard.Valid)
            {
                MiInvalidateDCachePage(Address);
            }
            /*
             * ARM64: Use simple store instead of InterlockedExchangePte
             * (LDXR/STXR) for self-map PTE access. See LDXR/STXR comment
             * in MmCreateVirtualMappingUnsafeEx kernel path.
             */
            OldPte.u.Long = PointerPte->u.Long;
            PointerPte->u.Long = 0;
            __asm__ __volatile__("dsb ishst" ::: "memory");
            KeInvalidateTlbEntry(Address);
        }
    }
    else
    {
        ASSERT(Address < MmSystemRangeStart);
        ASSERT(Process == PsGetCurrentProcess());

        /*
         * ARM64 TTBR0 Alias Avoidance: Delete user PTEs via physical walking.
         *
         * The TTBR0 alias addresses are broken on ARM64. We must access
         * user page tables via KSEG0 direct physical mapping.
         */
        {
            ULONG64 PteValue;
            ULONG Depth;

            /* Walk TTBR0 page tables physically and get the L3 PTE */
            if (MiArm64ReadUserPtePhysically(Address, &PteValue, &Depth) && Depth >= 3)
            {
                OldPte.u.Long = PteValue;
                ValidPde = TRUE;

                /* If PTE is valid, clear it via physical memory */
                if (OldPte.u.Hard.Valid)
                {
                    ULONG64 RootPa;
                    volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
                    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
                    ULONG64 L0Entry, L1Entry, L2Entry;

                    /* Invalidate D-cache before clearing PTE */
                    MiInvalidateDCachePage(Address);

                    /*
                     * CRITICAL FIX: Use TTBR0 register (not DirectoryTableBase[0])
                     * to find the user page table root.
                     *
                     * DirectoryTableBase[0] for the System process may point to
                     * TTBR1 (kernel page tables) instead of TTBR0 (user page tables).
                     * Using the wrong root means we'd clear a PTE in the wrong tree,
                     * leaving the REAL PTE in TTBR0's tree still valid.
                     *
                     * This was the root cause of SMP driver image corruption:
                     * section view PTEs were never actually cleared during unmap,
                     * so the next driver mapped at the same VA would read stale data
                     * from the previous driver's physical pages.
                     */
                    {
                        ULONG64 Ttbr0Val;
                        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0Val));
                        RootPa = Ttbr0Val & ARM64_PTE_ADDR_MASK;
                    }
                    if (RootPa != 0)
                    {
                        L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
                        L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
                        L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
                        L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

                        L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
                        L0Entry = L0Table[L0Idx];
                        L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & ARM64_PTE_ADDR_MASK));
                        L1Entry = L1Table[L1Idx];

                        /* Check for 1GB block (shouldn't happen for user space) */
                        if ((L1Entry & 0x3ULL) == 0x3ULL)
                        {
                            L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & ARM64_PTE_ADDR_MASK));
                            L2Entry = L2Table[L2Idx];

                            /* Check for 2MB block */
                            if ((L2Entry & 0x3ULL) == 0x3ULL)
                            {
                                L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & ARM64_PTE_ADDR_MASK));

                                /* Clear the L3 PTE atomically */
                                __atomic_exchange_n(&L3Table[L3Idx], 0, __ATOMIC_SEQ_CST);

                                /* Full barrier and TLB invalidation */
                                __asm__ __volatile__("dsb ish" ::: "memory");
                                __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> 12) : "memory");
                                __asm__ __volatile__("dsb ish" ::: "memory");
                                __asm__ __volatile__("isb" ::: "memory");
                            }
                        }
                    }
                }
            }
            else if (Depth >= 3)
            {
                /* Page table exists but PTE is not valid - nothing to delete */
                OldPte.u.Long = PteValue;
                ValidPde = TRUE;
            }
        }
    }

    if (ValidPde && OldPte.u.Hard.Valid)
    {
        /* Full cache and TLB flush for coherency */
        __asm__ __volatile__("ic ialluis" ::: "memory");
        __asm__ __volatile__("dsb ish" ::: "memory");
        __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
        __asm__ __volatile__("dsb ish" ::: "memory");
        __asm__ __volatile__("isb" ::: "memory");
    }

    if (OldPte.u.Long != 0)
    {
        if (WasDirty)
        {
            /*
             * ARM64 FIX (Bug #47): Check both hardware dirty bit AND PFN Modified flag.
             *
             * On ARM64, the hardware dirty bit (NotDirty, AP[2]) is combined with
             * the write-permission bit. When MmSetPageProtect changes a page from
             * writable to read-only, NotDirty is set to 1, losing the dirty state.
             *
             * MmSetPageProtect saves the dirty state in PFN.u3.e1.Modified when
             * this happens. We check both sources here to correctly identify pages
             * that were modified but later had their protection downgraded.
             *
             * This prevents data loss in the page eviction path: without this check,
             * private COW pages (e.g., DLL IAT pages that were resolved then
             * re-protected as read-only) would be evicted without being saved to
             * the pagefile, causing resolved imports to revert to unresolved
             * hint/name RVAs on re-fault.
             */
            BOOLEAN HwDirty = (OldPte.u.Hard.Valid && (OldPte.u.Hard.NotDirty == 0));
            BOOLEAN PfnDirty = FALSE;

            if (!HwDirty && OldPte.u.Hard.Valid && !IsPhysical)
            {
                PFN_NUMBER DirtyCheckPfn = OldPte.u.Hard.PageFrameNumber;
                if (DirtyCheckPfn != 0 && DirtyCheckPfn <= MmHighestPhysicalPage)
                {
                    KIRQL DirtyIrql = MiAcquirePfnLock();
                    PMMPFN PfnEntry = MiGetPfnEntry(DirtyCheckPfn);
                    if (PfnEntry != NULL && PfnEntry->u3.e1.Modified)
                    {
                        PfnDirty = TRUE;
                        /* Clear the Modified flag since we're reporting it now */
                        PfnEntry->u3.e1.Modified = 0;
                    }
                    MiReleasePfnLock(DirtyIrql);
                }
            }

            *WasDirty = HwDirty || PfnDirty;
        }
        if (Page)
            *Page = OldPte.u.Hard.PageFrameNumber;
    }
    else
    {
        if (WasDirty)
            *WasDirty = FALSE;
        if (Page)
            *Page = 0;
    }

    /*
     * ARM64: Decrement ARM3 PFN ShareCount for both kernel and user pages.
     * MmCreateVirtualMappingUnsafeEx increments ShareCount for all non-physical
     * pages (both kernel and user paths). MmDeleteVirtualMapping must match.
     *
     * Previously only done for Process != NULL (user pages), causing stale
     * ShareCount=1 on freed kernel section pages (Cc cache). When those PFNs
     * were reallocated for user section pages, the stale ShareCount propagated
     * indefinitely since MmAllocPage didn't reset it.
     */
    if (!IsPhysical && OldPte.u.Hard.Valid)
    {
        PFN_NUMBER DeletePfn = OldPte.u.Hard.PageFrameNumber;
        if (DeletePfn != 0 && DeletePfn <= MmHighestPhysicalPage)
        {
            KIRQL OldIrql = MiAcquirePfnLock();
            PMMPFN Pfn1 = MiGetPfnEntry(DeletePfn);

            if (Pfn1->u2.ShareCount > 0)
            {
                if (--Pfn1->u2.ShareCount == 0)
                    Pfn1->u3.e1.PageLocation = TransitionPage;
            }

            MiReleasePfnLock(OldIrql);
        }
    }

    if (Process != NULL && !IsPhysical && OldPte.u.Hard.Valid)
    {
        /*
         * Decrement the L3 page table's ShareCount for this removed PTE.
         *
         * MmCreateVirtualMappingUnsafeEx increments L3 ShareCount when
         * writing a PTE. We must match that decrement here for the RosMM
         * teardown path (MmFreeMemoryArea -> MmDeleteVirtualMapping).
         *
         * The ARM3 teardown path (MiDeleteVirtualAddresses -> MiDeletePte)
         * has its own L3 ShareCount decrement via MiDecrementShareCount,
         * so this code path is only reached by RosMM cleanup.
         *
         * Use MiArm64GetUserL3PfnSafe to walk TTBR0 via KSEG0 and find
         * the L3 page table PFN. The 'Safe' variant checks that the PFN
         * is still ActiveAndValid (not already freed by MiDeletePde).
         */
        PFN_NUMBER L3Pfn = MiArm64GetUserL3PfnSafe(Address);
        if (L3Pfn != 0)
        {
            KIRQL PtIrql = MiAcquirePfnLock();
            PMMPFN L3PfnEntry = MiGetPfnEntry(L3Pfn);
            if (L3PfnEntry->u2.ShareCount > 0)
            {
                MiDecrementShareCount(L3PfnEntry, L3Pfn);
            }

            /*
             * Bug #60 FIX: Decrement UsedPageTableEntries for the removed PTE.
             *
             * MmCreateVirtualMappingUnsafeEx now increments UsedPageTableEntries
             * when writing a new user PTE. This decrement balances that for the
             * RosMM teardown path (MmFreeMemoryArea -> MmDeleteVirtualMapping).
             *
             * The ARM3 teardown path (MiDeleteVirtualAddresses) has its own
             * decrement via MiDecrementPageTableReferences, so this only fires
             * for RosMM-cleaned pages. The two paths are mutually exclusive:
             * MiDeleteVirtualAddresses checks TempPte.u.Long and skips zero PTEs,
             * so it won't double-decrement if MmDeleteVirtualMapping ran first.
             */
            if (L3PfnEntry->OriginalPte.u.Soft.UsedPageTableEntries > 0)
            {
                L3PfnEntry->OriginalPte.u.Soft.UsedPageTableEntries--;
            }
            else
            {
                DPRINT1("[arm64][BUG60] MmDeleteVirtualMappingEx: UsedPTE already 0 for VA=%p "
                        "L3Pfn=%lx Proc=%s\n",
                        Address, (ULONG)L3Pfn,
                        Process->ImageFileName);
            }

            MiReleasePfnLock(PtIrql);
        }
    }

    return OldPte.u.Long != 0;
}

NTSTATUS
NTAPI
MmCreatePageFileMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ SWAPENTRY SwapEntry)
{
    ASSERT(Process != NULL);
    ASSERT(Address < MmSystemRangeStart);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());

    /*
     * ARM64 FIX (Bug #48): Walk TTBR0 via KSEG0 to find and modify L3 PTE.
     * Same rationale as MmSetPageProtect - self-map aliases can be stale.
     */
    {
        ULONG64 Ttbr0, RootPa;
        volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
        ULONG L0Idx, L1Idx, L2Idx, L3Idx;
        ULONG64 L0Entry, L1Entry, L2Entry, PteVal;
        MMPTE NewPte;

        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
        RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;

        L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
        L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
        L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
        L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

        L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
        L0Entry = L0Table[L0Idx];
        if ((L0Entry & 0x3ULL) != 0x3ULL)
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return STATUS_SUCCESS; /* No page table = no conflict */
        }

        L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & ARM64_PTE_ADDR_MASK));
        L1Entry = L1Table[L1Idx];
        if ((L1Entry & 0x3ULL) != 0x3ULL)
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return STATUS_SUCCESS;
        }

        L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & ARM64_PTE_ADDR_MASK));
        L2Entry = L2Table[L2Idx];
        if ((L2Entry & 0x3ULL) != 0x3ULL)
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return STATUS_SUCCESS;
        }

        L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & ARM64_PTE_ADDR_MASK));
        PteVal = L3Table[L3Idx];

        if (PteVal & 0x1ULL) /* Valid bit set */
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return STATUS_CONFLICTING_ADDRESSES;
        }

        NewPte.u.Long = 0;
        NewPte.u.Soft.PageFileLow = SwapEntry & 0xF;
        NewPte.u.Soft.PageFileHigh = SwapEntry >> 4;
        NewPte.u.Soft.Prototype = 0;
        NewPte.u.Soft.Protection = MM_READWRITE;

        __atomic_store_n(&L3Table[L3Idx], NewPte.u.Long, __ATOMIC_SEQ_CST);
    }

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    return STATUS_SUCCESS;
}

VOID
NTAPI
MmDeletePageFileMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Inout_ SWAPENTRY *SwapEntry)
{
    MMPTE OldPte;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());

    /*
     * ARM64 FIX (Bug #48): Walk TTBR0 via KSEG0 to find and clear L3 PTE.
     * Same rationale as MmSetPageProtect - self-map aliases can be stale.
     */
    {
        ULONG64 Ttbr0, RootPa;
        volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
        ULONG L0Idx, L1Idx, L2Idx, L3Idx;
        ULONG64 L0Entry, L1Entry, L2Entry;

        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
        RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;

        L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
        L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
        L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
        L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

        L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
        L0Entry = L0Table[L0Idx];
        if ((L0Entry & 0x3ULL) != 0x3ULL)
        {
            *SwapEntry = 0;
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return;
        }

        L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & ARM64_PTE_ADDR_MASK));
        L1Entry = L1Table[L1Idx];
        if ((L1Entry & 0x3ULL) != 0x3ULL)
        {
            *SwapEntry = 0;
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return;
        }

        L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & ARM64_PTE_ADDR_MASK));
        L2Entry = L2Table[L2Idx];
        if ((L2Entry & 0x3ULL) != 0x3ULL)
        {
            *SwapEntry = 0;
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return;
        }

        L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & ARM64_PTE_ADDR_MASK));
        OldPte.u.Long = __atomic_exchange_n(&L3Table[L3Idx], 0, __ATOMIC_SEQ_CST);
    }

    if (!FlagOn(OldPte.u.Long, 0x800) || OldPte.u.Hard.Valid)
    {
        DPRINT1("Expected pagefile PTE at %p\n", Address);
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    *SwapEntry = (SWAPENTRY)(((ULONG64)OldPte.u.Soft.PageFileHigh << 4) |
                              OldPte.u.Soft.PageFileLow);

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
}

VOID
NTAPI
MmGetPageFileMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ SWAPENTRY *SwapEntry)
{
    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    if (Address < MmSystemRangeStart)
    {
        /*
         * ARM64 TTBR0 Alias Avoidance: Walk TTBR0 physically via KSEG0.
         *
         * The TTBR0 alias (L0[494]) can fault if intermediate page table
         * pages do not exist yet for the given user address range. Instead
         * of dereferencing a potentially unmapped self-map alias address,
         * walk the physical page tables directly via KSEG0.
         *
         * If the page table hierarchy is incomplete (no L1/L2/L3 for this
         * address), the PTE is effectively zero, meaning no page file
         * mapping exists.
         */
        ULONG64 PteValue;

        MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());

        if (!MiArm64ReadUserPtePhysically(Address, &PteValue, NULL))
        {
            /* Page table hierarchy incomplete - no mapping */
            *SwapEntry = 0;
        }
        else
        {
            MMPTE TempPte;
            TempPte.u.Long = PteValue;

            if (!FlagOn(PteValue, 0x800) || TempPte.u.Hard.Valid)
                *SwapEntry = 0;
            else
                *SwapEntry = (SWAPENTRY)(((ULONG64)TempPte.u.Soft.PageFileHigh << 4) |
                                          TempPte.u.Soft.PageFileLow);
        }

        MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
    }
    else
    {
        /* Kernel addresses: use the TTBR1 self-map directly */
        PMMPTE PointerPte;

        MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());
        PointerPte = MiAddressToPte(Address);

        if (!FlagOn(PointerPte->u.Long, 0x800) || PointerPte->u.Hard.Valid)
            *SwapEntry = 0;
        else
            *SwapEntry = (SWAPENTRY)(((ULONG64)PointerPte->u.Soft.PageFileHigh << 4) |
                                      PointerPte->u.Soft.PageFileLow);

        MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
    }
}

BOOLEAN
NTAPI
MmIsPagePresent(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);

        /*
         * For kernel addresses, check if the page table hierarchy is valid.
         * We must NOT call MiMakeSystemAddressValid(Address) here because:
         * 1. This function is called during page fault handling to check if
         *    a page is already present.
         * 2. MiMakeSystemAddressValid would try to fault in the page,
         *    causing infinite recursion.
         * 3. If the PDE is not valid, the page cannot be present.
         *
         * Use MiIsPdeForAddressValid to check page table validity without
         * causing recursive faults.
         */
        if (!MiIsPdeForAddressValid(Address))
        {
            /* No valid page table for this address - page cannot be present */
            return FALSE;
        }

        return MiAddressToPte(Address)->u.Hard.Valid != 0;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    /*
     * ARM64 TTBR0 Alias Avoidance Fix:
     *
     * PROBLEM: The TTBR0 alias addresses (PTE_BASE_TTBR0, etc.) are fundamentally
     * broken. Accessing them causes nested page faults that lead to WS lock
     * recursion and assertion failures.
     *
     * SOLUTION: Walk TTBR0's page tables PHYSICALLY via KSEG0 direct mapping.
     * This avoids all TTBR0 alias addresses and cannot cause nested faults.
     *
     * We don't need any locks for this because:
     * 1. We're only reading page table entries (no modification)
     * 2. Physical memory access via KSEG0 doesn't involve page table walks
     * 3. Even if the page tables change during our read, we'll get a consistent
     *    snapshot (either old or new state, both valid)
     */
    {
        ULONG64 Ttbr0, RootPa;
        volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
        ULONG L0Idx, L1Idx, L2Idx, L3Idx;
        ULONG64 L0Entry, L1Entry, L2Entry, L3Entry;

        /* Read TTBR0 to get the user page table root */
        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
        RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;  /* Extract PA, mask ASID bits */
        if (RootPa == 0)
            return FALSE;

        /* Calculate indices for each level */
        L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
        L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
        L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
        L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

        /* Access L0 table via KSEG0 direct mapping */
        L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
        L0Entry = L0Table[L0Idx];

        /* Check L0 entry validity - must be a table descriptor (bits[1:0]=0b11) */
        if ((L0Entry & 0x3ULL) != 0x3ULL)
            return FALSE;

        /* Access L1 table */
        L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & ARM64_PTE_ADDR_MASK));
        L1Entry = L1Table[L1Idx];

        /*
         * L1 could be a 1GB block (bits[1:0]=0b01) or table (0b11) or invalid.
         *
         * ARM64 Critical Fix: Block descriptors (1GB or 2MB) in user space are
         * from FreeLoader's identity mapping, NOT from the Memory Manager.
         * ReactOS MM creates only 4KB page mappings for user addresses.
         *
         * We MUST return FALSE for block descriptors so that:
         * 1. MmNotPresentFaultSectionView will create proper section mappings
         * 2. The section data gets loaded instead of reading stale identity-mapped memory
         *
         * The old code returned TRUE for blocks, which caused fs_rec.sys loading to fail
         * because MmNotPresentFaultSectionView thought the page was already present.
         */
        if ((L1Entry & 0x1ULL) == 0)
            return FALSE;  /* L1 invalid */
        if ((L1Entry & 0x3ULL) == 0x1ULL)
            return FALSE;  /* L1 is 1GB block - FreeLoader identity map, treat as not present */

        /* L1 is table, access L2 */
        L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & ARM64_PTE_ADDR_MASK));
        L2Entry = L2Table[L2Idx];

        /* L2 could be a 2MB block or table or invalid */
        if ((L2Entry & 0x1ULL) == 0)
            return FALSE;  /* L2 invalid */
        if ((L2Entry & 0x3ULL) == 0x1ULL)
            return FALSE;  /* L2 is 2MB block - FreeLoader identity map, treat as not present */

        /* L2 is table, access L3 */
        L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & ARM64_PTE_ADDR_MASK));
        L3Entry = L3Table[L3Idx];

        /* L3 entry: page descriptor (bits[1:0]=0b11) means page is present (proper 4KB mapping) */
        {
            BOOLEAN Result = (L3Entry & 0x3ULL) == 0x3ULL;

            /* Debug logging for MmIsPagePresent - stripped for performance */
            return Result;
        }
    }
}

BOOLEAN
NTAPI
MmIsDisabledPage(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        return FALSE;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    /*
     * ARM64 TTBR0 Alias Avoidance: Walk TTBR0 physically via KSEG0.
     *
     * "Disabled page" means: Valid && !Writable && !CopyOnWrite
     * This is a read-only non-COW mapping - typically a private data section
     * that cannot be written.
     */
    {
        ULONG64 PteValue;
        MMPTE TempPte;

        if (!MiArm64ReadUserPtePhysically(Address, &PteValue, NULL))
            return FALSE;

        TempPte.u.Long = PteValue;
        if (!TempPte.u.Hard.Valid)
            return FALSE;

        return (TempPte.u.Hard.Writable == 0) && (TempPte.u.Hard.CopyOnWrite == 0);
    }
}

BOOLEAN
NTAPI
MmIsPageSwapEntry(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        return FALSE;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    /*
     * ARM64 TTBR0 Alias Avoidance: Walk TTBR0 physically via KSEG0.
     *
     * Swap entry check: !Valid && bit 0x800 is set (swap entry marker).
     * We need the raw PTE value to check these bits.
     */
    {
        ULONG64 PteValue;
        ULONG Depth;

        /* Get the PTE value by walking physically */
        MiArm64ReadUserPtePhysically(Address, &PteValue, &Depth);

        /* If we didn't reach L3 level (depth < 3), no PTE exists */
        if (Depth < 3)
            return FALSE;

        /* Check for swap entry: !Valid && bit 0x800 set */
        return ((PteValue & 0x3ULL) != 0x3ULL) && ((PteValue & 0x800ULL) != 0);
    }
}

ULONG
NTAPI
MmGetPageProtect(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        PMMPTE PointerPte;

        ASSERT(Process == NULL);

        /*
         * For kernel addresses, check if page tables exist without faulting.
         * We cannot call MiMakeSystemAddressValid here as it could cause
         * infinite recursion during page fault handling.
         */
        if (!MiIsPdeForAddressValid(Address))
        {
            return PAGE_NOACCESS;
        }

        PointerPte = MiAddressToPte(Address);
        return PointerPte->u.Hard.Valid ? MiProtectionFromPte(*PointerPte) : PAGE_NOACCESS;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    /*
     * ARM64 TTBR0 Alias Avoidance: Walk TTBR0 physically via KSEG0.
     */
    {
        ULONG64 PteValue;
        MMPTE TempPte;

        if (!MiArm64ReadUserPtePhysically(Address, &PteValue, NULL))
            return PAGE_NOACCESS;

        TempPte.u.Long = PteValue;
        return TempPte.u.Hard.Valid ? MiProtectionFromPte(TempPte) : PAGE_NOACCESS;
    }
}

VOID
NTAPI
MmSetPageProtect(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection)
{
    ULONG ProtectionMask;
    MMPTE TempPte, OldPte;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());
    ASSERT(Address < MmSystemRangeStart);

    ProtectionMask = MiMakeProtectionMask(Protection);
    ASSERT(ProtectionMask != MM_INVALID_PROTECTION);

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());

    /*
     * ARM64 FIX (Bug #48): Walk TTBR0 page tables via KSEG0 physical mapping
     * instead of using the TTBR1 self-map alias.
     *
     * PROBLEM: The self-map (MiAddressToPteSafe) returns a pointer through
     * TTBR1 aliases that can be STALE after context switches. When stale,
     * the pointer refers to a DIFFERENT process's L3 page table page:
     * 1. Reading PointerPte->PageFrameNumber gives the WRONG PFN
     * 2. InterlockedExchangePte writes to the WRONG process's PTE
     * 3. The current process's PTE is left unchanged
     * 4. The other process's PTE gets corrupted with a wrong PFN
     *
     * This caused non-deterministic crashes: process A's MmSetPageProtect
     * would corrupt process B's page table entries, leading to process B
     * reading wrong physical pages (corrupted DLL names like "MZx.DLL",
     * invalid pointer dereferences, translation faults on valid VAs).
     *
     * Fix: Walk TTBR0 directly via KSEG0 to find the L3 PTE, read/write
     * through the KSEG0-mapped physical address. This always accesses the
     * correct process's page table regardless of self-map state.
     */
    {
        ULONG64 Ttbr0, RootPa;
        volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
        ULONG L0Idx, L1Idx, L2Idx, L3Idx;
        ULONG64 L0Entry, L1Entry, L2Entry;

        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
        RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;

        L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
        L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
        L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
        L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

        L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
        L0Entry = L0Table[L0Idx];
        if ((L0Entry & 0x3ULL) != 0x3ULL)
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return;
        }

        L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & ARM64_PTE_ADDR_MASK));
        L1Entry = L1Table[L1Idx];
        if ((L1Entry & 0x3ULL) != 0x3ULL)
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return;
        }

        L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & ARM64_PTE_ADDR_MASK));
        L2Entry = L2Table[L2Idx];
        if ((L2Entry & 0x3ULL) != 0x3ULL)
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return;
        }

        L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & ARM64_PTE_ADDR_MASK));

        /* Read the current L3 PTE */
        OldPte.u.Long = L3Table[L3Idx];

        /* Build new PTE with requested protection */
        TempPte.u.Long = 0;
        TempPte.u.Long |= MmProtectToPteMask[ProtectionMask];
        TempPte.u.Hard.PageFrameNumber = OldPte.u.Hard.PageFrameNumber;
        TempPte.u.Hard.Owner = 1;          /* User accessible (AP[0]=1) */
        TempPte.u.Hard.Shareability = 3;   /* Inner Shareable for SMP coherency */

        if ((ProtectionMask != MM_NOACCESS) && !FlagOn(ProtectionMask, MM_GUARDPAGE))
            TempPte.u.Hard.Valid = 1;

        if (OldPte.u.Hard.Accessed)
            TempPte.u.Hard.Accessed = 1;

        /* Set NotLargePage=1 for valid L3 page descriptor (bits [1:0]=0b11) */
        if (TempPte.u.Hard.Valid)
            TempPte.u.Hard.NotLargePage = 1;

        /*
         * ARM64: Set NotDirty based on the NEW protection's writability.
         * NotDirty=0 (AP[2]=0): writable/dirty
         * NotDirty=1 (AP[2]=1): read-only/clean
         */
        if (TempPte.u.Hard.Writable)
            MI_MAKE_DIRTY_PAGE(&TempPte);
        else
            MI_MAKE_CLEAN_PAGE(&TempPte);

        /* Atomically exchange the PTE via KSEG0 */
        OldPte.u.Long = __atomic_exchange_n(&L3Table[L3Idx], TempPte.u.Long, __ATOMIC_SEQ_CST);

        /*
         * ARM64 FIX (Bug #47): Preserve dirty state in PFN when hardware dirty
         * bit is cleared due to protection downgrade (writable -> read-only).
         *
         * On ARM64, NotDirty controls both dirty tracking and write permission.
         * When changing from writable (NotDirty=0) to read-only (NotDirty=1),
         * the "page was written to" information is lost. Save it in PFN Modified
         * so MmDeleteVirtualMappingEx can report correct dirty state during eviction.
         */
        if (OldPte.u.Hard.Valid && (OldPte.u.Hard.NotDirty == 0) &&
            TempPte.u.Hard.NotDirty == 1)
        {
            PFN_NUMBER PagePfn = OldPte.u.Hard.PageFrameNumber;
            if (PagePfn != 0 && PagePfn <= MmHighestPhysicalPage)
            {
                KIRQL PfnOldIrql = MiAcquirePfnLock();
                PMMPFN Pfn1 = MiGetPfnEntry(PagePfn);
                if (Pfn1 != NULL)
                {
                    Pfn1->u3.e1.Modified = 1;
                }
                MiReleasePfnLock(PfnOldIrql);
            }
        }

        /* TLB invalidation */
        if (OldPte.u.Long != TempPte.u.Long)
        {
            __asm__ __volatile__("dsb ishst" ::: "memory");
            __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> 12) : "memory");
            __asm__ __volatile__("dsb ish" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");
        }
    }

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
}

VOID
NTAPI
MmSetDirtyBit(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN Dirty)
{
    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());
    ASSERT(Address < MmSystemRangeStart);

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());

    /*
     * ARM64 FIX (Bug #48): Walk TTBR0 page tables via KSEG0 physical mapping
     * instead of using the TTBR1 self-map alias, same rationale as MmSetPageProtect.
     * The self-map can be stale after context switches, causing modifications to
     * the wrong process's page table entries.
     */
    {
        ULONG64 Ttbr0, RootPa;
        volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
        ULONG L0Idx, L1Idx, L2Idx, L3Idx;
        ULONG64 L0Entry, L1Entry, L2Entry, PteVal;
        MMPTE TempPte;

        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
        RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;

        L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
        L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
        L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
        L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

        L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
        L0Entry = L0Table[L0Idx];
        if ((L0Entry & 0x3ULL) != 0x3ULL)
            goto DirtyBitDone;

        L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & ARM64_PTE_ADDR_MASK));
        L1Entry = L1Table[L1Idx];
        if ((L1Entry & 0x3ULL) != 0x3ULL)
            goto DirtyBitDone;

        L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & ARM64_PTE_ADDR_MASK));
        L2Entry = L2Table[L2Idx];
        if ((L2Entry & 0x3ULL) != 0x3ULL)
            goto DirtyBitDone;

        L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & ARM64_PTE_ADDR_MASK));
        PteVal = L3Table[L3Idx];
        TempPte.u.Long = PteVal;

        if (!TempPte.u.Hard.Valid)
            goto DirtyBitDone;

        if (Dirty)
            MI_MAKE_DIRTY_PAGE(&TempPte);
        else
            MI_MAKE_CLEAN_PAGE(&TempPte);

        /* Write back modified PTE via KSEG0 */
        __atomic_store_n(&L3Table[L3Idx], TempPte.u.Long, __ATOMIC_SEQ_CST);

        if (!Dirty)
        {
            __asm__ __volatile__("dsb ishst" ::: "memory");
            __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> 12) : "memory");
            __asm__ __volatile__("dsb ish" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");
        }
    }

DirtyBitDone:
    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
}

PFN_NUMBER
NTAPI
MmGetPfnForProcess(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE PointerPte;
    PFN_NUMBER PageFrame = 0;

    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);

        /*
         * For kernel addresses, check if page tables exist without faulting.
         * We cannot call MiMakeSystemAddressValid here as it could cause
         * infinite recursion during page fault handling.
         */
        if (!MiIsPdeForAddressValid(Address))
        {
            return 0;
        }

        PointerPte = MiAddressToPte(Address);
        if (PointerPte->u.Hard.Valid)
            PageFrame = PointerPte->u.Hard.PageFrameNumber;

        return PageFrame;
    }

    ASSERT(Process != NULL);
    /* Note: Process might not equal PsGetCurrentProcess() during cross-process
     * faults (e.g., timer DPC, KeStackAttachProcess). MiArm64GetUserPfn handles
     * this by using Process->Pcb.DirectoryTableBase[0] instead of TTBR0. */

    /*
     * ARM64: Use MiArm64GetUserPfn to walk user page tables directly.
     *
     * On ARM64 with TTBR0/TTBR1 split architecture, the kernel's self-mapping
     * (accessible via MiAddressToPxe/Ppe/Pde/Pte) is in TTBR1 and cannot access
     * user page tables which are in TTBR0. We must walk the user's page table
     * hierarchy using the process's DirectoryTableBase.
     */
    return MiArm64GetUserPfn(Process, Address);
}

static
BOOLEAN
__attribute__((unused))
MiIsPageTablePresent(
    _In_ PVOID Address)
{
#if _MI_PAGING_LEVELS == 2
    BOOLEAN Ret = MmWorkingSetList->UsedPageTableEntries[MiGetPdeOffset(Address)] != 0;
    ASSERT(Ret == (MiAddressToPde(Address)->u.Hard.Valid != 0));
    return Ret;
#else
    PMMPDE PointerPde;
    PMMPPE PointerPpe;
#if _MI_PAGING_LEVELS == 4
    PMMPXE PointerPxe;
#endif

    ASSERT((PsGetCurrentThread()->OwnsProcessWorkingSetExclusive) ||
           (PsGetCurrentThread()->OwnsProcessWorkingSetShared));
    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    /*
     * ARM64: For user-space addresses, we cannot safely use the TTBR0 alias
     * while holding the WS lock, as accessing the alias addresses may fault
     * and cause lock re-entry issues.
     *
     * Instead, we rely on the UsedPageTableEntries counters in MmWorkingSetList.
     * These counters track which page directory entries have been allocated.
     *
     * Note: This is a conservative check. If we can't determine page table
     * presence safely, we return FALSE (page not present).
     */
    if (Address < MmSystemRangeStart)
    {
        /*
         * Use UsedPageTableEntries to check if there's a PDE for this address.
         * This doesn't access TTBR0 alias addresses and is safe while holding WS lock.
         *
         * For now, use a simple check: if there are no page table entries used
         * at the PDE offset for this address, the page table doesn't exist.
         */
        ULONG PdeOffset = MiGetPdeOffset(Address);
        if (MmWorkingSetList != NULL)
        {
            if (MmWorkingSetList->UsedPageTableEntries[PdeOffset] == 0)
                return FALSE;
        }
        else
        {
            /* WorkingSetList not available - assume page table doesn't exist */
            return FALSE;
        }

        /*
         * UsedPageTableEntries says there's a page table at this PDE.
         * The full page table hierarchy should exist.
         */
        return TRUE;
    }

    /*
     * ARM64/ReactOS Fix: Check actual page table validity instead of
     * UsedPageTableEntries counters.
     *
     * The ReactOS section view code path (MmCreateVirtualMapping ->
     * MiMakePdeExistAndMakeValid) creates page tables via kernel-mode
     * fault handling, which doesn't properly increment the UsedPageTableEntries
     * counters at PXE/PPE levels. This causes MiIsPageTablePresent to
     * incorrectly return FALSE even when the page tables are valid.
     *
     * The fix is to check the actual validity of each level of the page
     * table hierarchy. If all levels (PXE, PPE, PDE) are valid, then the
     * page table is present. If any level is invalid or in transition,
     * we fault it in and continue.
     */

#if _MI_PAGING_LEVELS == 4
    PointerPxe = MiAddressToPxe(Address);

    /* If PXE is not valid and not in transition, page table cannot be present */
    if (PointerPxe->u.Hard.Valid == 0 && PointerPxe->u.Soft.Transition == 0)
    {
        /* Check if there are any pending entries (non-zero soft PTE) */
        if (PointerPxe->u.Long == 0)
            return FALSE;
        /* There's a soft PTE, page table might exist - continue checking */
    }

    /* Ensure PXE is valid before accessing PPE */
    if (PointerPxe->u.Hard.Valid == 0)
        MiMakeSystemAddressValid(MiPteToAddress(PointerPxe), PsGetCurrentProcess());
#endif

    PointerPpe = MiAddressToPpe(Address);

    /* If PPE is not valid and not in transition, page table cannot be present */
    if (PointerPpe->u.Hard.Valid == 0 && PointerPpe->u.Soft.Transition == 0)
    {
        /* Check if there are any pending entries (non-zero soft PTE) */
        if (PointerPpe->u.Long == 0)
            return FALSE;
        /* There's a soft PTE, page table might exist - continue checking */
    }

    /* Ensure PPE is valid before accessing PDE */
    if (PointerPpe->u.Hard.Valid == 0)
        MiMakeSystemAddressValid(MiPteToAddress(PointerPpe), PsGetCurrentProcess());

    PointerPde = MiAddressToPde(Address);

    /*
     * The page table for this address exists if the PDE is valid.
     * We don't rely on UsedPageTableEntries counters here because
     * they may not be properly maintained by the ROS section view path.
     */
    if (PointerPde->u.Hard.Valid == 1)
        return TRUE;

    /* PDE is not valid - check if it's in transition or has soft PTE data */
    if (PointerPde->u.Soft.Transition == 1)
        return TRUE;

    /* PDE is completely empty - no page table present */
    return PointerPde->u.Long != 0;
#endif
}

static
ULONG
MiProtectionFromPte(
    _In_ MMPTE Pte)
{
    ULONG Mask = Pte.u.Long & PTE_PROTECT_MASK;

    for (ULONG i = 0; i < ARRAYSIZE(MmProtectToPteMask); ++i)
    {
        if ((MmProtectToPteMask[i] & PTE_PROTECT_MASK) == Mask)
            return MmProtectToValue[i];
    }

    return PAGE_NOACCESS;
}

/*
 * ARM64 Cycle 57: Clear stale user-space PTEs from FreeLoader's identity mapping.
 *
 * FreeLoader creates identity mappings in user space (TTBR0) during boot.
 * These mappings cover physical addresses up to 4GB. When the kernel maps
 * section views at user addresses that overlap with this identity mapping,
 * reads would return identity-mapped data instead of section data.
 *
 * This function walks the TTBR0 page table hierarchy for the specified VA range
 * and invalidates any valid PTEs found. This ensures page faults occur when
 * the section is accessed, allowing proper demand-loading of section data.
 *
 * @param StartVa   Starting virtual address of the range to clear
 * @param Size      Size of the range in bytes
 * @param Process   Process whose user page tables should be cleared
 */
VOID
NTAPI
MiArm64ClearStaleUserPtes(
    _In_ PVOID StartVa,
    _In_ SIZE_T Size,
    _In_ PEPROCESS Process)
{
    ULONG64 Ttbr0;
    PFN_NUMBER L0Pfn, L1Pfn, L2Pfn, L3Pfn;
    PMMPTE L0Table, L1Table, L2Table, L3Table;
    PMMPTE MappingPte;
    ULONG64 Va, EndVa;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
    ULONG ClearedCount = 0;

    /* Only for user-space addresses */
    if ((ULONG_PTR)StartVa >= (ULONG_PTR)MmSystemRangeStart)
        return;

    /* Get TTBR0 (user page table root) */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    L0Pfn = (Ttbr0 >> PAGE_SHIFT) & ((1ULL << 36) - 1);  /* Get PA without ASID bits */

    EndVa = (ULONG64)StartVa + Size;
    Va = (ULONG64)StartVa;

    while (Va < EndVa)
    {
        L0Idx = (Va >> 39) & 0x1FF;
        L1Idx = (Va >> 30) & 0x1FF;
        L2Idx = (Va >> 21) & 0x1FF;
        L3Idx = (Va >> 12) & 0x1FF;

        /* Map L0 table */
        MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
        if (!MappingPte)
        {
            DPRINT1("[arm64] MiArm64ClearStaleUserPtes: Failed to get system PTE\n");
            return;
        }

        MI_WRITE_VALID_PTE(MappingPte, ValidKernelPte);
        MappingPte->u.Hard.PageFrameNumber = L0Pfn;
        KeInvalidateTlbEntry(MiPteToAddress(MappingPte));
        __asm__ __volatile__("dsb ish" ::: "memory");

        L0Table = (PMMPTE)MiPteToAddress(MappingPte);

        /* Check if L0 entry is valid */
        if (!(L0Table[L0Idx].u.Long & 1))
        {
            /* No L0 entry, skip this 512GB range */
            MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
            KeInvalidateTlbEntry(L0Table);
            MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
            Va = (Va + (1ULL << 39)) & ~((1ULL << 39) - 1);
            continue;
        }

        L1Pfn = L0Table[L0Idx].u.Hard.PageFrameNumber;
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(L0Table);

        /* Map L1 table */
        MI_WRITE_VALID_PTE(MappingPte, ValidKernelPte);
        MappingPte->u.Hard.PageFrameNumber = L1Pfn;
        KeInvalidateTlbEntry(MiPteToAddress(MappingPte));
        __asm__ __volatile__("dsb ish" ::: "memory");

        L1Table = (PMMPTE)MiPteToAddress(MappingPte);

        /* Check if L1 entry is valid */
        if (!(L1Table[L1Idx].u.Long & 1))
        {
            /* No L1 entry, skip this 1GB range */
            MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
            KeInvalidateTlbEntry(L1Table);
            MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
            Va = (Va + (1ULL << 30)) & ~((1ULL << 30) - 1);
            continue;
        }

        L2Pfn = L1Table[L1Idx].u.Hard.PageFrameNumber;
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(L1Table);

        /* Map L2 table */
        MI_WRITE_VALID_PTE(MappingPte, ValidKernelPte);
        MappingPte->u.Hard.PageFrameNumber = L2Pfn;
        KeInvalidateTlbEntry(MiPteToAddress(MappingPte));
        __asm__ __volatile__("dsb ish" ::: "memory");

        L2Table = (PMMPTE)MiPteToAddress(MappingPte);

        /* Check if L2 entry is valid */
        if (!(L2Table[L2Idx].u.Long & 1))
        {
            /* No L2 entry, skip this 2MB range */
            MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
            KeInvalidateTlbEntry(L2Table);
            MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
            Va = (Va + (1ULL << 21)) & ~((1ULL << 21) - 1);
            continue;
        }

        /* L2 entry is valid - but is it a block descriptor or table descriptor? */
        /* bits [1:0] = 0b01 for block, 0b11 for table */
        {
            ULONG64 L2Entry = L2Table[L2Idx].u.Long;
            if ((L2Entry & 0x3) == 0x1)
            {
                /*
                 * Block descriptor (2MB page) - this is a FreeLoader identity mapping!
                 * We need to CLEAR this block descriptor to ensure page faults occur.
                 */
                DPRINT("[arm64] MiArm64ClearStaleUserPtes: L2 entry at idx=%lu is BLOCK (2MB) entry=0x%llx - CLEARING IT\n",
                        (ULONG)L2Idx, L2Entry);

                /*
                 * Invalidate TLB entries for the 2MB block.
                 * We use TLBI ASIDE1 to invalidate by ASID, which is more efficient
                 * than invalidating each page individually.
                 * But for simplicity, just invalidate everything with TLBI VMALLE1.
                 */
                __asm__ __volatile__("tlbi vmalle1" ::: "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");

                /*
                 * NOTE: We intentionally do NOT invalidate D-cache for the entire 2MB block here.
                 * D-cache invalidation for user VAs at this point can cause issues:
                 * 1. The VAs are no longer mapped (we just cleared the L2 block)
                 * 2. DC IVAC on unmapped VAs may cause issues on some implementations
                 * 3. The next access will fault anyway and bring in fresh data
                 *
                 * The TLB invalidation above is sufficient - cache lines will be naturally
                 * evicted or will return stale data on next access, which will fault.
                 */

                /* Clear the L2 block descriptor */
                {
                    ULONG_PTR PteAddr = (ULONG_PTR)&L2Table[L2Idx];
                    L2Table[L2Idx].u.Long = 0;
                    __asm__ __volatile__("dmb ish" ::: "memory");
                    __asm__ __volatile__("dc civac, %0" :: "r"(PteAddr) : "memory");
                    __asm__ __volatile__("dsb ish" ::: "memory");
                }

                /* Verify the L2 entry was cleared */
                if (L2Table[L2Idx].u.Long != 0)
                {
                    DPRINT1("[arm64] MiArm64ClearStaleUserPtes: WARNING! L2 BLOCK entry NOT cleared! Still=0x%llx\n",
                            L2Table[L2Idx].u.Long);
                }
                else
                {
                    DPRINT("[arm64] MiArm64ClearStaleUserPtes: Successfully cleared L2 BLOCK entry for 2MB at VA=%p\n",
                            (PVOID)(Va & ~((1ULL << 21) - 1)));
                }

                MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
                KeInvalidateTlbEntry(L2Table);
                MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

                /* Skip the entire 2MB block */
                ClearedCount += (1ULL << 21) >> PAGE_SHIFT;  /* Count as 512 pages */
                Va = (Va + (1ULL << 21)) & ~((1ULL << 21) - 1);
                continue;
            }
            /* Table descriptor pointing to L3 table — walk it below */
        }

        L3Pfn = L2Table[L2Idx].u.Hard.PageFrameNumber;
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(L2Table);

        /* Map L3 table */
        MI_WRITE_VALID_PTE(MappingPte, ValidKernelPte);
        MappingPte->u.Hard.PageFrameNumber = L3Pfn;
        KeInvalidateTlbEntry(MiPteToAddress(MappingPte));
        __asm__ __volatile__("dsb ish" ::: "memory");

        L3Table = (PMMPTE)MiPteToAddress(MappingPte);

        /* Check if L3 entry (PTE) has bit 0 set (potentially valid) */
        if (L3Table[L3Idx].u.Long & 1)
        {
            ULONG64 OldPte = L3Table[L3Idx].u.Long;

            /* Found a PTE with Valid bit set - clear it. */
            DPRINT("[arm64] MiArm64ClearStaleUserPtes: Clearing stale PTE at VA=%p (PTE=0x%llx) L3TableVA=%p L3Idx=%lu L3Pfn=0x%llx\n",
                   (PVOID)Va, OldPte, L3Table, (ULONG)L3Idx, (ULONG64)L3Pfn);
            DPRINT("[arm64]   Page table walk: L0Pfn=0x%llx L1Pfn=0x%llx L2Pfn=0x%llx L3Pfn=0x%llx\n",
                   (ULONG64)L0Pfn, (ULONG64)L1Pfn, (ULONG64)L2Pfn, (ULONG64)L3Pfn);
            DPRINT("[arm64]   Indices: L0=%lu L1=%lu L2=%lu L3=%lu\n",
                   (ULONG)L0Idx, (ULONG)L1Idx, (ULONG)L2Idx, (ULONG)L3Idx);

            /* Invalidate TLB for this VA - use VAALE1IS to flush ALL ASIDs including Global entries.
             * FreeLoader may have created Global mappings (nG bit not set), so VAE1 (current ASID only)
             * would NOT flush them. VAALE1IS = VA, All ASIDs, Last-level, EL1, Inner Shareable */
            __asm__ __volatile__("tlbi vaale1is, %0" :: "r"(Va >> PAGE_SHIFT) : "memory");
            __asm__ __volatile__("dsb ish" ::: "memory");

            /*
             * Same rule as the stale-user remap path above: avoid invalidating
             * through the user alias we are about to retire.
             */
            MiArm64InvalidatePageByPfnAlias(L3Table[L3Idx].u.Hard.PageFrameNumber);

            /* Clear the PTE with proper cache maintenance */
            {
                ULONG_PTR PteAddr = (ULONG_PTR)&L3Table[L3Idx];

                /* Write the zero value */
                L3Table[L3Idx].u.Long = 0;

                /* Data memory barrier before cache op */
                __asm__ __volatile__("dmb ish" ::: "memory");

                /* Clean and invalidate this cache line to PoC - ensure write reaches memory */
                __asm__ __volatile__("dc civac, %0" :: "r"(PteAddr) : "memory");

                /* DSB to ensure cache op completes */
                __asm__ __volatile__("dsb ish" ::: "memory");

                /* Re-read to verify (need to invalidate cache first to get fresh read) */
                __asm__ __volatile__("dc ivac, %0" :: "r"(PteAddr) : "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
            }

            /* Verify the PTE was actually cleared */
            if (L3Table[L3Idx].u.Long != 0)
            {
                DPRINT1("[arm64] MiArm64ClearStaleUserPtes: WARNING! PTE at VA=%p was NOT cleared! Still=0x%llx\n",
                        (PVOID)Va, L3Table[L3Idx].u.Long);
            }

            ClearedCount++;
        }

        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(L3Table);
        MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

        Va += PAGE_SIZE;
    }

    if (ClearedCount > 0)
    {
        DPRINT("[arm64] MiArm64ClearStaleUserPtes: Cleared %lu stale PTEs in range %p-%p\n",
                ClearedCount, StartVa, (PVOID)EndVa);
    }
}
