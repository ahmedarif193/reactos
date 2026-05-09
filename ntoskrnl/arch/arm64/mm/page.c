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

BOOLEAN
MiArm64ReadUserPtePhysically(
    _In_ PVOID Address,
    _Out_opt_ PULONG64 OutPte,
    _Out_opt_ PULONG OutDepth);

typedef struct _MI_ARM64_USER_PTE_WALK
{
    volatile ULONG64 *PointerPte;
    ULONG64 PteValue;
    ULONG Depth;
} MI_ARM64_USER_PTE_WALK, *PMI_ARM64_USER_PTE_WALK;

static
BOOLEAN
MiArm64EnsureTablePageMapped(
    _In_ ULONG64 TablePa)
{
    PFN_NUMBER Pfn;

    if ((TablePa & (PAGE_SIZE - 1)) != 0)
    {
        return FALSE;
    }

    Pfn = (PFN_NUMBER)(TablePa >> PAGE_SHIFT);
    if ((Pfn == 0) || (Pfn > MmHighestPhysicalPage))
    {
        return FALSE;
    }

    MiArm64MapKseg0Page(Pfn);
    return TRUE;
}

static
BOOLEAN
MiArm64GetUserPteAddressForProcess(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ PMI_ARM64_USER_PTE_WALK Walk);

static
BOOLEAN
MiArm64GetUserPteAddress(
    _In_ PVOID Address,
    _Out_ PMI_ARM64_USER_PTE_WALK Walk);

static
MMPTE
MiArm64ClearKernelPte(
    _In_ PVOID Address,
    _Inout_ PMMPTE PointerPte);

static
MMPTE
MiArm64ClearUserPte(
    _In_ PVOID Address,
    _In_ PMI_ARM64_USER_PTE_WALK Walk);

static
BOOLEAN
MiArm64ConsumeDirtyState(
    _In_ MMPTE OldPte,
    _In_ BOOLEAN IsPhysical);

static
VOID
MiArm64PreserveDirtyStateForProtect(
    _In_ MMPTE OldPte,
    _In_ MMPTE NewPte);

static
VOID
MiArm64ReleaseMappedPageReference(
    _In_ PFN_NUMBER PageFrameNumber);

static
VOID
MiArm64ReleaseUserPageTableReference(
    _In_ PEPROCESS Process,
    _In_ PVOID Address);

static LONG MiArm64UserMapTraceCount;
static LONG MiArm64UserPublishTraceCount;
static LONG MiArm64MapCacheSyncTraceCount;

typedef enum _MI_ARM64_DCACHE_OPERATION
{
    MiArm64DcacheInvalidate,
    MiArm64DcacheCleanInvalidate
} MI_ARM64_DCACHE_OPERATION;

FORCEINLINE
VOID
MiArm64CacheLineSizes(
    _Out_ PULONG DcacheLineSize,
    _Out_ PULONG IcacheLineSize)
{
    ULONG64 Ctr;

    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    *DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);
    *IcacheLineSize = 4u << (Ctr & 0xF);
}

FORCEINLINE
VOID
MiArm64MaintainPageCache(
    _In_ PVOID Address,
    _In_ MI_ARM64_DCACHE_OPERATION DcacheOperation,
    _In_ BOOLEAN SweepIcache)
{
    ULONG DcacheLineSize;
    ULONG IcacheLineSize;
    ULONG_PTR Va;

    MiArm64CacheLineSizes(&DcacheLineSize, &IcacheLineSize);
    Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

    __asm__ __volatile__("dsb sy" ::: "memory");
    for (ULONG_PTR Offset = 0; Offset < PAGE_SIZE; Offset += DcacheLineSize)
    {
        if (DcacheOperation == MiArm64DcacheCleanInvalidate)
        {
            __asm__ __volatile__("dc civac, %0" :: "r"(Va + Offset) : "memory");
        }
        else
        {
            __asm__ __volatile__("dc ivac, %0" :: "r"(Va + Offset) : "memory");
        }
    }
    __asm__ __volatile__("dsb sy" ::: "memory");

    if (SweepIcache)
    {
        for (ULONG_PTR Offset = 0; Offset < PAGE_SIZE; Offset += IcacheLineSize)
        {
            __asm__ __volatile__("ic ivau, %0" :: "r"(Va + Offset) : "memory");
        }
        __asm__ __volatile__("dsb sy" ::: "memory");
    }

    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
MiArm64CleanInvalidateCacheLine(
    _In_ PVOID Address)
{
    __asm__ __volatile__("dmb ish" ::: "memory");
    __asm__ __volatile__("dc civac, %0" :: "r"(Address) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
}

FORCEINLINE
VOID
MiArm64InvalidatePageByPfnAlias(
    _In_ PFN_NUMBER PageFrameNumber)
{
    MiArm64MaintainPageCache((PVOID)(KSEG0_BASE + ((ULONG_PTR)PageFrameNumber << PAGE_SHIFT)),
                             MiArm64DcacheInvalidate,
                             FALSE);
}

FORCEINLINE
VOID
MiArm64PublishPageByPfnAlias(
    _In_ PFN_NUMBER PageFrameNumber,
    _In_ BOOLEAN SweepIcache)
{
    MiArm64MaintainPageCache((PVOID)(KSEG0_BASE + ((ULONG_PTR)PageFrameNumber << PAGE_SHIFT)),
                             MiArm64DcacheCleanInvalidate,
                             SweepIcache);
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

static
BOOLEAN
MiArm64GetUserPteAddressForProcess(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ PMI_ARM64_USER_PTE_WALK Walk)
{
    ULONG64 RootPa, L1Pa, L2Pa, L3Pa;
    volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
    ULONG64 L0Entry, L1Entry, L2Entry;

    Walk->PointerPte = NULL;
    Walk->PteValue = 0;
    Walk->Depth = 0;

    if (Address >= MmSystemRangeStart)
    {
        return FALSE;
    }

    ASSERT(Process != NULL);
    if (Process == PsGetCurrentProcess())
    {
        ULONG64 Ttbr0;

        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
        RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;
    }
    else
    {
        RootPa = Process->Pcb.DirectoryTableBase[0] & ARM64_PTE_ADDR_MASK;
    }

    if (RootPa == 0)
    {
        return FALSE;
    }

    L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
    L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
    L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
    L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

    if (!MiArm64EnsureTablePageMapped(RootPa))
    {
        return FALSE;
    }

    L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
    L0Entry = L0Table[L0Idx];
    if ((L0Entry & 0x3ULL) != 0x3ULL)
    {
        return FALSE;
    }
    Walk->Depth = 1;

    L1Pa = L0Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64EnsureTablePageMapped(L1Pa))
    {
        return FALSE;
    }

    L1Table = (volatile ULONG64 *)(KSEG0_BASE | L1Pa);
    L1Entry = L1Table[L1Idx];
    if ((L1Entry & 0x1ULL) == 0)
    {
        return FALSE;
    }
    if ((L1Entry & 0x3ULL) == 0x1ULL)
    {
        Walk->PteValue = L1Entry;
        return FALSE;
    }
    if ((L1Entry & 0x3ULL) != 0x3ULL)
    {
        return FALSE;
    }
    Walk->Depth = 2;

    L2Pa = L1Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64EnsureTablePageMapped(L2Pa))
    {
        return FALSE;
    }

    L2Table = (volatile ULONG64 *)(KSEG0_BASE | L2Pa);
    L2Entry = L2Table[L2Idx];
    if ((L2Entry & 0x1ULL) == 0)
    {
        return FALSE;
    }
    if ((L2Entry & 0x3ULL) == 0x1ULL)
    {
        Walk->PteValue = L2Entry;
        return FALSE;
    }
    if ((L2Entry & 0x3ULL) != 0x3ULL)
    {
        return FALSE;
    }
    Walk->Depth = 3;

    L3Pa = L2Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64EnsureTablePageMapped(L3Pa))
    {
        return FALSE;
    }

    L3Table = (volatile ULONG64 *)(KSEG0_BASE | L3Pa);
    Walk->PointerPte = &L3Table[L3Idx];
    Walk->PteValue = L3Table[L3Idx];
    if ((Walk->PteValue & 0x3ULL) == 0x3ULL)
    {
        Walk->Depth = 4;
    }

    return TRUE;
}

static
BOOLEAN
MiArm64GetUserPteAddress(
    _In_ PVOID Address,
    _Out_ PMI_ARM64_USER_PTE_WALK Walk)
{
    return MiArm64GetUserPteAddressForProcess(PsGetCurrentProcess(), Address, Walk);
}

/* Walk the active TTBR0 hierarchy via KSEG0 and return the L3 PTE state. */
BOOLEAN
MiArm64ReadUserPtePhysically(
    _In_ PVOID Address,
    _Out_opt_ PULONG64 OutPte,
    _Out_opt_ PULONG OutDepth)
{
    MI_ARM64_USER_PTE_WALK Walk;

    if (!MiArm64GetUserPteAddress(Address, &Walk))
    {
        if (OutPte) *OutPte = Walk.PteValue;
        if (OutDepth) *OutDepth = Walk.Depth;
        return FALSE;
    }

    if (OutPte) *OutPte = Walk.PteValue;
    if (OutDepth) *OutDepth = Walk.Depth;
    return ((Walk.PteValue & 0x3ULL) == 0x3ULL);
}

FORCEINLINE
VOID
MiArm64InvalidateUserAddress(
    _In_ PVOID Address)
{
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> PAGE_SHIFT) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

static
MMPTE
MiArm64ClearKernelPte(
    _In_ PVOID Address,
    _Inout_ PMMPTE PointerPte)
{
    MMPTE OldPte;

    OldPte.u.Long = PointerPte->u.Long;
    if (OldPte.u.Hard.Valid)
    {
        MiArm64MaintainPageCache(Address, MiArm64DcacheCleanInvalidate, TRUE);
    }

    PointerPte->u.Long = 0;
    MiArm64SyncKernelLeafPteWrite(PointerPte);
    __asm__ __volatile__("dsb ishst" ::: "memory");
    KeInvalidateTlbEntry(Address);

    return OldPte;
}

static
MMPTE
MiArm64ClearUserPte(
    _In_ PVOID Address,
    _In_ PMI_ARM64_USER_PTE_WALK Walk)
{
    MMPTE OldPte;

    OldPte.u.Long = Walk->PteValue;
    if (OldPte.u.Hard.Valid)
    {
        MiArm64PublishPageByPfnAlias(OldPte.u.Hard.PageFrameNumber,
                                     OldPte.u.Hard.UserNoExecute == 0);
        __atomic_store_n(Walk->PointerPte, 0, __ATOMIC_SEQ_CST);
        MiArm64InvalidateUserAddress(Address);
    }

    return OldPte;
}

static
BOOLEAN
MiArm64ConsumeDirtyState(
    _In_ MMPTE OldPte,
    _In_ BOOLEAN IsPhysical)
{
    PFN_NUMBER PageFrameNumber;
    PMMPFN PfnEntry;
    KIRQL OldIrql;
    BOOLEAN Dirty;

    if (!OldPte.u.Hard.Valid)
    {
        return FALSE;
    }

    if (MI_IS_PAGE_DIRTY(&OldPte))
    {
        return TRUE;
    }

    if (IsPhysical)
    {
        return FALSE;
    }

    PageFrameNumber = OldPte.u.Hard.PageFrameNumber;
    if ((PageFrameNumber == 0) || (PageFrameNumber > MmHighestPhysicalPage))
    {
        return FALSE;
    }

    Dirty = FALSE;
    OldIrql = MiAcquirePfnLock();
    PfnEntry = MiGetPfnEntry(PageFrameNumber);
    if ((PfnEntry != NULL) && PfnEntry->u3.e1.Modified)
    {
        PfnEntry->u3.e1.Modified = 0;
        Dirty = TRUE;
    }
    MiReleasePfnLock(OldIrql);

    return Dirty;
}

static
VOID
MiArm64PreserveDirtyStateForProtect(
    _In_ MMPTE OldPte,
    _In_ MMPTE NewPte)
{
    PFN_NUMBER PageFrameNumber;
    PMMPFN PfnEntry;
    KIRQL OldIrql;

    if (!OldPte.u.Hard.Valid ||
        !MI_IS_PAGE_DIRTY(&OldPte) ||
        MI_IS_PAGE_DIRTY(&NewPte))
    {
        return;
    }

    PageFrameNumber = OldPte.u.Hard.PageFrameNumber;
    if ((PageFrameNumber == 0) || (PageFrameNumber > MmHighestPhysicalPage))
    {
        return;
    }

    OldIrql = MiAcquirePfnLock();
    PfnEntry = MiGetPfnEntry(PageFrameNumber);
    if (PfnEntry != NULL)
    {
        PfnEntry->u3.e1.Modified = 1;
    }
    MiReleasePfnLock(OldIrql);
}

static
VOID
MiArm64ReleaseMappedPageReference(
    _In_ PFN_NUMBER PageFrameNumber)
{
    KIRQL OldIrql;
    PMMPFN PfnEntry;

    if ((PageFrameNumber == 0) || (PageFrameNumber > MmHighestPhysicalPage))
    {
        return;
    }

    OldIrql = MiAcquirePfnLock();
    PfnEntry = MiGetPfnEntry(PageFrameNumber);
    if ((PfnEntry != NULL) && (PfnEntry->u2.ShareCount > 0))
    {
        if (--PfnEntry->u2.ShareCount == 0)
        {
            PfnEntry->u3.e1.PageLocation = TransitionPage;
        }
    }
    MiReleasePfnLock(OldIrql);
}

static
VOID
MiArm64ReleaseUserPageTableReference(
    _In_ PEPROCESS Process,
    _In_ PVOID Address)
{
    ULONG64 RootPa, L1Pa, L2Pa;
    volatile ULONG64 *L0Table, *L1Table, *L2Table;
    ULONG L0Idx, L1Idx, L2Idx;
    ULONG64 L0Entry, L1Entry, L2Entry;
    PFN_NUMBER RootPfn, L1Pfn, L2Pfn, L3Pfn;
    KIRQL OldIrql;
    PMMPFN PfnRoot, PfnL1, PfnL2, PfnL3;

    ASSERT(Process != NULL);
    RootPa = Process->Pcb.DirectoryTableBase[0] & ARM64_PTE_ADDR_MASK;
    if (RootPa == 0)
    {
        return;
    }

    L0Idx = ((ULONG64)(ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK;
    L1Idx = ((ULONG64)(ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK;
    L2Idx = ((ULONG64)(ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64;

    RootPfn = RootPa >> PAGE_SHIFT;
    if (!MiArm64EnsureTablePageMapped(RootPa))
        return;

    L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
    L0Entry = L0Table[L0Idx];
    if ((L0Entry & 0x3ULL) != 0x3ULL)
        return;

    L1Pa = L0Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64EnsureTablePageMapped(L1Pa))
        return;

    L1Pfn = L1Pa >> PAGE_SHIFT;
    L1Table = (volatile ULONG64 *)(KSEG0_BASE | L1Pa);
    L1Entry = L1Table[L1Idx];
    if ((L1Entry & 0x3ULL) != 0x3ULL)
        return;

    L2Pa = L1Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64EnsureTablePageMapped(L2Pa))
        return;

    L2Pfn = L2Pa >> PAGE_SHIFT;
    L2Table = (volatile ULONG64 *)(KSEG0_BASE | L2Pa);
    L2Entry = L2Table[L2Idx];
    if ((L2Entry & 0x3ULL) != 0x3ULL)
        return;

    L3Pfn = (L2Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;

    OldIrql = MiAcquirePfnLock();
    PfnRoot = MiGetPfnEntry(RootPfn);
    PfnL1 = MiGetPfnEntry(L1Pfn);
    PfnL2 = MiGetPfnEntry(L2Pfn);
    PfnL3 = MiGetPfnEntry(L3Pfn);

    if (PfnL3 == NULL || PfnL2 == NULL || PfnL1 == NULL || PfnRoot == NULL)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL3->u2.ShareCount > 0)
    {
        MiDecrementShareCount(PfnL3, L3Pfn);
    }

    if (PfnL3->OriginalPte.u.Soft.UsedPageTableEntries > 0)
    {
        PfnL3->OriginalPte.u.Soft.UsedPageTableEntries--;
    }
    else
    {
        DPRINT1("[arm64][UPTE] MmDeleteVirtualMappingEx: UsedPTE already 0 for VA=%p "
                "L3Pfn=%lx Proc=%s\n",
                Address, (ULONG)L3Pfn, Process->ImageFileName);
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL3->OriginalPte.u.Soft.UsedPageTableEntries != 0)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL3->u2.ShareCount != 1)
    {
        DPRINT1("[arm64][UPTE] Empty L3 has unexpected share count VA=%p "
                "L3Pfn=%lx share=%lu Proc=%s\n",
                Address, (ULONG)L3Pfn, PfnL3->u2.ShareCount, Process->ImageFileName);
        MiReleasePfnLock(OldIrql);
        return;
    }

    __atomic_store_n(&L2Table[L2Idx], 0, __ATOMIC_SEQ_CST);
    MiArm64InvalidateUserAddress(Address);
    MiDecrementShareCount(PfnL2, L2Pfn);
    MI_SET_PFN_DELETED(PfnL3);
    MiDecrementShareCount(PfnL3, L3Pfn);

    if (PfnL2->OriginalPte.u.Soft.UsedPageTableEntries > 0)
    {
        PfnL2->OriginalPte.u.Soft.UsedPageTableEntries--;
    }
    else
    {
        DPRINT1("[arm64][UPTE] UsedPTE already 0 for L2 VA=%p L2Pfn=%lx Proc=%s\n",
                Address, (ULONG)L2Pfn, Process->ImageFileName);
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL2->OriginalPte.u.Soft.UsedPageTableEntries != 0)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL2->u2.ShareCount != 1)
    {
        DPRINT1("[arm64][UPTE] Empty L2 has unexpected share count VA=%p "
                "L2Pfn=%lx share=%lu Proc=%s\n",
                Address, (ULONG)L2Pfn, PfnL2->u2.ShareCount, Process->ImageFileName);
        MiReleasePfnLock(OldIrql);
        return;
    }

    __atomic_store_n(&L1Table[L1Idx], 0, __ATOMIC_SEQ_CST);
    MiArm64InvalidateUserAddress(Address);
    MiDecrementShareCount(PfnL1, L1Pfn);
    MI_SET_PFN_DELETED(PfnL2);
    MiDecrementShareCount(PfnL2, L2Pfn);

    if (PfnL1->OriginalPte.u.Soft.UsedPageTableEntries > 0)
    {
        PfnL1->OriginalPte.u.Soft.UsedPageTableEntries--;
    }
    else
    {
        DPRINT1("[arm64][UPTE] UsedPTE already 0 for L1 VA=%p L1Pfn=%lx Proc=%s\n",
                Address, (ULONG)L1Pfn, Process->ImageFileName);
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL1->OriginalPte.u.Soft.UsedPageTableEntries != 0)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL1->u2.ShareCount != 1)
    {
        DPRINT1("[arm64][UPTE] Empty L1 has unexpected share count VA=%p "
                "L1Pfn=%lx share=%lu Proc=%s\n",
                Address, (ULONG)L1Pfn, PfnL1->u2.ShareCount, Process->ImageFileName);
        MiReleasePfnLock(OldIrql);
        return;
    }

    __atomic_store_n(&L0Table[L0Idx], 0, __ATOMIC_SEQ_CST);
    MiArm64InvalidateUserAddress(Address);
    MiDecrementShareCount(PfnRoot, RootPfn);
    MI_SET_PFN_DELETED(PfnL1);
    MiDecrementShareCount(PfnL1, L1Pfn);

    if (PfnRoot->OriginalPte.u.Soft.UsedPageTableEntries > 0)
    {
        PfnRoot->OriginalPte.u.Soft.UsedPageTableEntries--;
    }
    else
    {
        DPRINT1("[arm64][UPTE] UsedPTE already 0 for L0 VA=%p RootPfn=%lx Proc=%s\n",
                Address, (ULONG)RootPfn, Process->ImageFileName);
    }

    MiReleasePfnLock(OldIrql);
}

VOID
MiInvalidateDCachePageIncoming(
    _In_ PVOID Address)
{
    MiArm64MaintainPageCache(Address, MiArm64DcacheInvalidate, TRUE);
}

VOID
MiInvalidateDCachePageOutgoing(
    _In_ PVOID Address)
{
    MiArm64MaintainPageCache(Address, MiArm64DcacheCleanInvalidate, TRUE);
}

VOID
MiInvalidateDCachePage(
    _In_ PVOID Address)
{
    MiInvalidateDCachePageOutgoing(Address);
}

static
PFN_NUMBER
MiArm64GetUserPfn(
    _In_ PEPROCESS Process,
    _In_ PVOID Address)
{
    MI_ARM64_USER_PTE_WALK Walk;
    MMPTE Pte;

    ASSERT(Address < MmSystemRangeStart);
    ASSERT(Process != NULL);

    if (!MiArm64GetUserPteAddressForProcess(Process, Address, &Walk))
    {
        return 0;
    }

    Pte.u.Long = Walk.PteValue;
    return Pte.u.Hard.Valid ? Pte.u.Hard.PageFrameNumber : 0;
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
    PETHREAD CurrentThread = PsGetCurrentThread();

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
         * MiMakeSystemAddressValid may have to fault in the page table page
         * containing this PTE. Kernel page-table VAs are protected by the
         * system working set, so satisfy that helper's lock contract here.
         */
        MiLockWorkingSet(CurrentThread, &MmSystemCacheWs);
        MiMakeSystemAddressValid(MiAddressToPte(Address), PsGetCurrentProcess());
        MiUnlockWorkingSet(CurrentThread, &MmSystemCacheWs);
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

        /* User mappings are installed in the active TTBR0 root. */
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

            MiArm64MapKseg0Page(PpePfn);

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
            TempPte = ValidKernelPde;
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

            /* Track the new L1 table in the parent L0 PFN entry. */
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

            MiArm64MapKseg0Page(PdePfn);

            /*
             * Use MiInitializePfnForOtherProcess - the PTE address is the self-mapping
             * address (for bookkeeping), and the parent frame is the PPE page.
             */
            MiInitializePfnForOtherProcess(PdePfn, MiAddressToPpe(Address), PpePfn);
            Process->NumberOfPrivatePages++;

            /* Write PPE entry into the mapped page */
            TempPte = ValidKernelPde;
            TempPte.u.Hard.PageFrameNumber = PdePfn;
            TempPte.u.Hard.Owner = 0;  /* No APTable restriction */
            MI_ARM64_STORE_TABLE_ENTRY("MmCreateVirtualMappingUnsafeEx.L1",
                                       Address,
                                       1,
                                       MappedPage,
                                       PpeIndex,
                                       TempPte);
            ASSERT((MappedPage[PpeIndex].u.Long & 0x3ULL) == 0x3ULL);

            /* Track the new L2 table in the parent L1 PFN entry. */
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

            MiArm64MapKseg0Page(PtePfn);

            /*
             * Use MiInitializePfnForOtherProcess - the PTE address is the self-mapping
             * address (for bookkeeping), and the parent frame is the PDE page.
             */
            MiInitializePfnForOtherProcess(PtePfn, MiAddressToPde(Address), PdePfn);
            Process->NumberOfPrivatePages++;

            /* Write PDE entry */
            TempPte = ValidKernelPde;
            TempPte.u.Hard.PageFrameNumber = PtePfn;
            TempPte.u.Hard.Owner = 0;  /* No APTable restriction */
            MI_ARM64_STORE_TABLE_ENTRY("MmCreateVirtualMappingUnsafeEx.L2",
                                       Address,
                                       2,
                                       MappedPage,
                                       PdeIndex,
                                       TempPte);
            ASSERT((MappedPage[PdeIndex].u.Long & 0x3ULL) == 0x3ULL);

            /* Track the new L3 table in the parent L2 PFN entry. */
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

            /* Build an EL0 L3 page descriptor in the active TTBR0 hierarchy. */
            FinalPte.u.Long = 0;
            FinalPte.u.Hard.Valid = 1;
            FinalPte.u.Hard.NotLargePage = 1;  /* ARM64 L3 page descriptor */
            FinalPte.u.Hard.Owner = 1;         /* User accessible (AP[0]=1) */
            FinalPte.u.Hard.Accessed = 1;      /* Access Flag must be set */
            FinalPte.u.Hard.Shareability = 3;  /* Inner Shareable for SMP coherency */
            FinalPte.u.Hard.NonGlobal = 1;     /* EL0 mappings must not be global */
            FinalPte.u.Hard.PageFrameNumber = Page;
            FinalPte.u.Long |= MmProtectToPteMask[ProtectionMask];

            /* NotDirty encodes write permission and dirty state on ARM64. */
            if (FinalPte.u.Hard.Writable)
            {
                MI_MAKE_DIRTY_PAGE(&FinalPte);
            }
            else
            {
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

            /* Replace any valid boot-time user mapping at this VA. */
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

            /* Write the final PTE before publishing the page through its PFN alias. */
            MI_ARM64_STORE_TABLE_ENTRY("MmCreateVirtualMappingUnsafeEx.L3Final",
                                       Address,
                                       3,
                                       MappedPage,
                                       PteIndex,
                                       FinalPte);
            ASSERT(MappedPage[PteIndex].u.Long == FinalPte.u.Long);
            __asm__ __volatile__("dsb sy" ::: "memory");

            /* Balance the teardown decrement for newly created user PTEs. */
            if (OldUserPte == 0)
            {
                PMMPFN PtePfn1 = MiGetPfnEntry(PtePfn);
                if (PtePfn1 != NULL)
                {
                    PtePfn1->OriginalPte.u.Soft.UsedPageTableEntries++;
                    ASSERT(PtePfn1->OriginalPte.u.Soft.UsedPageTableEntries <= PTE_PER_PAGE);
                }
            }

            MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
            KeInvalidateTlbEntry(MappedPage);
            MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

            /* Publish cache state through the stable PFN alias. */
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

            MiArm64InvalidateUserAddress(Address);
            MiArm64TraceUserMapState("done", Process, Address, Page, Protection, OldUserPte);

            MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
        }

        return STATUS_SUCCESS;
    }

    /* Kernel address path - uses self-mapping */
    PointerPte = MiAddressToPte(Address);
    MI_MAKE_HARDWARE_PTE_KERNEL(&TempPte, PointerPte, ProtectionMask, Page);

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

    if (PointerPte->u.Hard.Valid)
    {
        MiArm64MaintainPageCache(Address, MiArm64DcacheCleanInvalidate, FALSE);
    }

    {
        ULONG_PTR OldPteValue = PointerPte->u.Long;

        PointerPte->u.Long = TempPte.u.Long;
        MiArm64SyncKernelLeafPteWrite(PointerPte);
        __asm__ __volatile__("dsb ishst" ::: "memory");

        if (OldPteValue != 0)
        {
            DPRINT1("Mapping collision at %p\n", Address);
            KeBugCheck(MEMORY_MANAGEMENT);
        }
    }

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
    BOOLEAN ProcessWorkingSetLocked = FALSE;

    OldPte.u.Long = 0;

    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    if (Process == NULL)
    {
        ASSERT(Address >= MmSystemRangeStart);

        if (MiIsPdeForAddressValid(Address))
        {
            OldPte = MiArm64ClearKernelPte(Address, MiAddressToPte(Address));
        }
    }
    else
    {
        MI_ARM64_USER_PTE_WALK Walk;

        ASSERT(Address < MmSystemRangeStart);
        ASSERT(Process == PsGetCurrentProcess());

        MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
        ProcessWorkingSetLocked = TRUE;

        if (MiArm64GetUserPteAddress(Address, &Walk))
        {
            OldPte = MiArm64ClearUserPte(Address, &Walk);
        }
    }

    if (OldPte.u.Long != 0)
    {
        if (WasDirty)
        {
            *WasDirty = MiArm64ConsumeDirtyState(OldPte, IsPhysical);
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

    if (!IsPhysical && OldPte.u.Hard.Valid)
    {
        MiArm64ReleaseMappedPageReference(OldPte.u.Hard.PageFrameNumber);
    }

    if (Process != NULL && !IsPhysical && OldPte.u.Hard.Valid)
    {
        MiArm64ReleaseUserPageTableReference(Process, Address);
    }

    if (ProcessWorkingSetLocked)
    {
        MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
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

    {
        MI_ARM64_USER_PTE_WALK Walk;
        MMPTE NewPte;

        if (!MiArm64GetUserPteAddress(Address, &Walk))
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return STATUS_SUCCESS;
        }

        if (Walk.PteValue & 0x1ULL)
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return STATUS_CONFLICTING_ADDRESSES;
        }

        NewPte.u.Long = 0;
        NewPte.u.Soft.PageFileLow = SwapEntry & 0xF;
        NewPte.u.Soft.PageFileHigh = SwapEntry >> 4;
        NewPte.u.Soft.Prototype = 0;
        NewPte.u.Soft.Protection = MM_READWRITE;

        __atomic_store_n(Walk.PointerPte, NewPte.u.Long, __ATOMIC_SEQ_CST);
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

    {
        MI_ARM64_USER_PTE_WALK Walk;

        if (!MiArm64GetUserPteAddress(Address, &Walk))
        {
            *SwapEntry = 0;
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return;
        }

        OldPte.u.Long = __atomic_exchange_n(Walk.PointerPte, 0, __ATOMIC_SEQ_CST);
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
        MI_ARM64_USER_PTE_WALK Walk;

        MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());

        if (!MiArm64GetUserPteAddress(Address, &Walk))
        {
            *SwapEntry = 0;
        }
        else
        {
            MMPTE TempPte;
            TempPte.u.Long = Walk.PteValue;

            if (!FlagOn(Walk.PteValue, 0x800) || TempPte.u.Hard.Valid)
                *SwapEntry = 0;
            else
                *SwapEntry = (SWAPENTRY)(((ULONG64)TempPte.u.Soft.PageFileHigh << 4) |
                                          TempPte.u.Soft.PageFileLow);
        }

        MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
    }
    else
    {
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

    return MiArm64ReadUserPtePhysically(Address, NULL, NULL);
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

    {
        MI_ARM64_USER_PTE_WALK Walk;

        if (!MiArm64GetUserPteAddress(Address, &Walk))
            return FALSE;

        return ((Walk.PteValue & 0x3ULL) != 0x3ULL) &&
               ((Walk.PteValue & 0x800ULL) != 0);
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

    {
        MI_ARM64_USER_PTE_WALK Walk;

        if (!MiArm64GetUserPteAddress(Address, &Walk))
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return;
        }

        OldPte.u.Long = Walk.PteValue;

        TempPte.u.Long = 0;
        TempPte.u.Long |= MmProtectToPteMask[ProtectionMask];
        TempPte.u.Hard.PageFrameNumber = OldPte.u.Hard.PageFrameNumber;
        TempPte.u.Hard.Owner = 1;          /* User accessible (AP[0]=1) */
        TempPte.u.Hard.Shareability = 3;   /* Inner Shareable for SMP coherency */

        if ((ProtectionMask != MM_NOACCESS) && !FlagOn(ProtectionMask, MM_GUARDPAGE))
            TempPte.u.Hard.Valid = 1;

        if (OldPte.u.Hard.Accessed)
            TempPte.u.Hard.Accessed = 1;

        if (TempPte.u.Hard.Valid)
            TempPte.u.Hard.NotLargePage = 1;

        if (TempPte.u.Hard.Writable)
            MI_MAKE_DIRTY_PAGE(&TempPte);
        else
            MI_MAKE_CLEAN_PAGE(&TempPte);

        OldPte.u.Long = __atomic_exchange_n(Walk.PointerPte, TempPte.u.Long, __ATOMIC_SEQ_CST);

        MiArm64PreserveDirtyStateForProtect(OldPte, TempPte);

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

    {
        MI_ARM64_USER_PTE_WALK Walk;
        MMPTE TempPte;

        if (!MiArm64GetUserPteAddress(Address, &Walk))
            goto DirtyBitDone;

        TempPte.u.Long = Walk.PteValue;

        if (!TempPte.u.Hard.Valid)
            goto DirtyBitDone;

        if (Dirty)
            MI_MAKE_DIRTY_PAGE(&TempPte);
        else
            MI_MAKE_CLEAN_PAGE(&TempPte);

        __atomic_store_n(Walk.PointerPte, TempPte.u.Long, __ATOMIC_SEQ_CST);

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

    if (Address < MmSystemRangeStart)
    {
        MI_ARM64_USER_PTE_WALK Walk;
        return MiArm64GetUserPteAddress(Address, &Walk);
    }

#if _MI_PAGING_LEVELS == 4
    PointerPxe = MiAddressToPxe(Address);

    if (PointerPxe->u.Hard.Valid == 0 && PointerPxe->u.Soft.Transition == 0)
    {
        if (PointerPxe->u.Long == 0)
            return FALSE;
    }

    if (PointerPxe->u.Hard.Valid == 0)
        MiMakeSystemAddressValid(MiPteToAddress(PointerPxe), PsGetCurrentProcess());
#endif

    PointerPpe = MiAddressToPpe(Address);

    if (PointerPpe->u.Hard.Valid == 0 && PointerPpe->u.Soft.Transition == 0)
    {
        if (PointerPpe->u.Long == 0)
            return FALSE;
    }

    if (PointerPpe->u.Hard.Valid == 0)
        MiMakeSystemAddressValid(MiPteToAddress(PointerPpe), PsGetCurrentProcess());

    PointerPde = MiAddressToPde(Address);

    if (PointerPde->u.Hard.Valid == 1)
        return TRUE;

    if (PointerPde->u.Soft.Transition == 1)
        return TRUE;

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

FORCEINLINE
ULONG64
MiArm64NextVaRange(
    _In_ ULONG64 Va,
    _In_ ULONG64 RangeSize)
{
    return (Va + RangeSize) & ~(RangeSize - 1);
}

FORCEINLINE
VOID
MiArm64ClearUserDescriptor(
    _Inout_ volatile ULONG64 *Entry)
{
    __atomic_store_n(Entry, 0, __ATOMIC_SEQ_CST);
    MiArm64CleanInvalidateCacheLine((PVOID)Entry);
}

static
BOOLEAN
MiArm64ClearBootUserMappingAtVa(
    _In_ ULONG64 Va,
    _Out_ PULONG64 NextVa,
    _Out_ PULONG ClearedPages)
{
    ULONG64 Ttbr0;
    ULONG64 RootPa;
    ULONG64 L0Entry, L1Entry, L2Entry, L3Entry;
    volatile ULONG64 *L0Table;
    volatile ULONG64 *L1Table;
    volatile ULONG64 *L2Table;
    volatile ULONG64 *L3Table;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;

    *NextVa = Va + PAGE_SIZE;
    *ClearedPages = 0;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;
    if (RootPa == 0)
    {
        return FALSE;
    }

    L0Idx = (Va >> 39) & 0x1FF;
    L1Idx = (Va >> 30) & 0x1FF;
    L2Idx = (Va >> 21) & 0x1FF;
    L3Idx = (Va >> 12) & 0x1FF;

    L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
    L0Entry = L0Table[L0Idx];
    if ((L0Entry & 0x3ULL) != 0x3ULL)
    {
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 39);
        return FALSE;
    }

    L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & ARM64_PTE_ADDR_MASK));
    L1Entry = L1Table[L1Idx];
    if ((L1Entry & 0x1ULL) == 0)
    {
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 30);
        return FALSE;
    }

    if ((L1Entry & 0x3ULL) == 0x1ULL)
    {
        MiArm64ClearUserDescriptor(&L1Table[L1Idx]);
        MiArm64InvalidateUserAddress((PVOID)Va);
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 30);
        *ClearedPages = (1ULL << 30) >> PAGE_SHIFT;
        return TRUE;
    }

    if ((L1Entry & 0x3ULL) != 0x3ULL)
    {
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 30);
        return FALSE;
    }

    L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & ARM64_PTE_ADDR_MASK));
    L2Entry = L2Table[L2Idx];
    if ((L2Entry & 0x1ULL) == 0)
    {
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 21);
        return FALSE;
    }

    if ((L2Entry & 0x3ULL) == 0x1ULL)
    {
        MiArm64ClearUserDescriptor(&L2Table[L2Idx]);
        MiArm64InvalidateUserAddress((PVOID)Va);
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 21);
        *ClearedPages = (1ULL << 21) >> PAGE_SHIFT;
        return TRUE;
    }

    if ((L2Entry & 0x3ULL) != 0x3ULL)
    {
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 21);
        return FALSE;
    }

    L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & ARM64_PTE_ADDR_MASK));
    L3Entry = L3Table[L3Idx];
    if ((L3Entry & 0x1ULL) == 0)
    {
        return FALSE;
    }

    if ((L3Entry & 0x3ULL) == 0x3ULL)
    {
        MiArm64InvalidatePageByPfnAlias((PFN_NUMBER)((L3Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT));
    }

    MiArm64ClearUserDescriptor(&L3Table[L3Idx]);
    MiArm64InvalidateUserAddress((PVOID)Va);
    *ClearedPages = 1;
    return TRUE;
}

VOID
NTAPI
MiArm64ClearStaleUserPtes(
    _In_ PVOID StartVa,
    _In_ SIZE_T Size,
    _In_ PEPROCESS Process)
{
    ULONG64 Va, EndVa;
    ULONG ClearedCount = 0;

    ASSERT(Process == PsGetCurrentProcess());
    UNREFERENCED_PARAMETER(Process);

    if ((ULONG_PTR)StartVa >= (ULONG_PTR)MmSystemRangeStart)
        return;

    EndVa = (ULONG64)StartVa + Size;
    if (EndVa > (ULONG64)(ULONG_PTR)MmSystemRangeStart)
    {
        EndVa = (ULONG64)(ULONG_PTR)MmSystemRangeStart;
    }
    Va = (ULONG64)StartVa;

    while (Va < EndVa)
    {
        ULONG64 NextVa;
        ULONG ClearedPages;

        MiArm64ClearBootUserMappingAtVa(Va, &NextVa, &ClearedPages);
        ClearedCount += ClearedPages;
        Va = (NextVa < EndVa) ? NextVa : EndVa;
    }

    if (ClearedCount > 0)
    {
        DPRINT("[arm64] MiArm64ClearStaleUserPtes: Cleared %lu stale PTEs in range %p-%p\n",
                ClearedCount, StartVa, (PVOID)EndVa);
    }
}
