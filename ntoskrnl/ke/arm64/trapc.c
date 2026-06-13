/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/trapc.c
 * PURPOSE:         Trap handling stubs for ARM64
 */

#include <ntoskrnl.h>
#include <arm64trap.h>
#include <fpstate.h>
#define NDEBUG
#include <debug.h>
#include <mm/ARM3/miarm.h>
#ifdef KDBG
#include <kdbg/kdb.h>
#endif

#define ARM64_STUB() UNIMPLEMENTED_DBGBREAK()

#ifndef ARM64_PTE_ADDR_MASK
#define ARM64_PTE_ADDR_MASK 0x0000FFFFFFFFF000ULL
#endif

#define ESR_EC_BRK 0x3C
// Cookie lives in Reserved bits 16-31, mode in bits 8-15; byte 0 carries PreviousIrql
#define ARM64_PREVIOUS_MODE_COOKIE 0x4D500000UL
#define ARM64_PREVIOUS_MODE_MASK   0xFFFF0000UL
#define ARM64_PREVIOUS_MODE_VALUE  0x0000FF00UL
#define ARM64_PREVIOUS_MODE_SHIFT  8

/* Forward declaration for SError handler */
BOOLEAN NTAPI KiSErrorHandler(_In_ PKTRAP_FRAME TrapFrame);
extern PVOID KiArm64PanicStack;
DECLSPEC_NORETURN
VOID
KiArm64BugCheckOnPanicStack(
    _In_ PVOID PanicStack,
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR BugCheckParameter1,
    _In_ ULONG_PTR BugCheckParameter2,
    _In_ ULONG_PTR BugCheckParameter3,
    _In_ ULONG_PTR BugCheckParameter4);

#define KI_ARM64_POOL_BLOCK(x, i) \
    ((PPOOL_HEADER)((ULONG_PTR)(x) + ((i) * POOL_BLOCK_SIZE)))

static
VOID
KiArm64DumpKernelWalk(
    _In_z_ PCSTR Tag,
    _In_ ULONG64 Va);

static
BOOLEAN
KiArm64IsKernelAddressMappedNoFault(
    _In_ ULONG_PTR Va)
{
    ULONG64 Ttbr1;
    ULONG64 RootPa;
    volatile ULONG64 *Table;
    ULONG64 Entry;

    if (Va < (ULONG_PTR)MmSystemRangeStart)
        return FALSE;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    RootPa = Ttbr1 & ARM64_PTE_ADDR_MASK;
    if (RootPa == 0)
        return FALSE;

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
    Entry = Table[(Va >> PXI_SHIFT) & PXI_MASK];
    if ((Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
        return FALSE;

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Entry & ARM64_PTE_ADDR_MASK);
    Entry = Table[(Va >> PPI_SHIFT) & PPI_MASK];
    if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
        return TRUE;
    if ((Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
        return FALSE;

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Entry & ARM64_PTE_ADDR_MASK);
    Entry = Table[(Va >> PDI_SHIFT) & PDI_MASK_ARM64];
    if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
        return TRUE;
    if ((Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
        return FALSE;

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Entry & ARM64_PTE_ADDR_MASK);
    Entry = Table[(Va >> PTI_SHIFT) & PTI_MASK_ARM64];
    return ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_PAGE);
}

static
VOID
KiArm64SavePreviousModeForTrap(
    _Inout_ PKTHREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    TrapFrame->Reserved = (TrapFrame->Reserved & 0xFFUL) | ARM64_PREVIOUS_MODE_COOKIE | (((ULONG)Thread->PreviousMode << ARM64_PREVIOUS_MODE_SHIFT) & ARM64_PREVIOUS_MODE_VALUE);
    Thread->PreviousMode = KiGetPreviousMode(TrapFrame);
}

static
VOID
KiArm64RestorePreviousModeFromTrap(
    _Inout_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG PreviousMode = TrapFrame->Reserved;

    if ((PreviousMode & ARM64_PREVIOUS_MODE_MASK) == ARM64_PREVIOUS_MODE_COOKIE)
    {
        Thread->PreviousMode = (CHAR)((PreviousMode & ARM64_PREVIOUS_MODE_VALUE) >> ARM64_PREVIOUS_MODE_SHIFT);
    }
}

static
BOOLEAN
KiArm64ValidatePoolHeader(
    _In_ PVOID BaseVa,
    _In_ PPOOL_HEADER Entry,
    _In_ POOL_TYPE BasePoolType)
{
    if (Entry->BlockSize == 0)
        return FALSE;

    if ((Entry->BlockSize * POOL_BLOCK_SIZE) +
        ((ULONG_PTR)Entry - (ULONG_PTR)BaseVa) > PAGE_SIZE)
    {
        return FALSE;
    }

    if ((Entry->PreviousSize == 0) && ((PVOID)Entry != BaseVa))
        return FALSE;

    if ((Entry->PreviousSize * POOL_BLOCK_SIZE) >
        ((ULONG_PTR)Entry - (ULONG_PTR)BaseVa))
    {
        return FALSE;
    }

    if (((Entry->PoolType - 1) & BASE_POOL_TYPE_MASK) != BasePoolType)
        return FALSE;

    if ((Entry->PoolTag & 0x00808080) != 0)
        return FALSE;

    return TRUE;
}

static
VOID
KiArm64DumpPagedPoolPageByPfnAlias(
    _In_ ULONG64 Far,
    _In_ ULONG64 PteValue)
{
    ULONG_PTR PageVa;
    ULONG_PTR FaultOffset;
    ULONG_PTR AliasPage;
    PFN_NUMBER DataPfn;
    ULONG_PTR QwordOffset;
    ULONG64 PrevQword, ThisQword, NextQword;
    PPOOL_HEADER Match = NULL;
    ULONG_PTR MatchOffset = 0;
    ULONG_PTR MatchEnd = 0;
    ULONG ValidHeaders = 0;

    if ((PteValue & 1ULL) == 0)
        return;

    DataPfn = (PFN_NUMBER)((PteValue & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
    AliasPage = MI_ARM64_PFN_TO_VA(DataPfn);
    PageVa = (ULONG_PTR)PAGE_ALIGN(Far);
    FaultOffset = BYTE_OFFSET(Far);
    QwordOffset = BYTE_OFFSET(ALIGN_DOWN_BY(Far, sizeof(ULONG64)));

    if (!KiArm64IsKernelAddressMappedNoFault(AliasPage))
    {
        DPRINT1("[arm64][SErrorPoolQ] page=%p pfn=%Ix alias=%p not mapped in KSEG0\n",
                (PVOID)PageVa,
                (ULONG_PTR)DataPfn,
                (PVOID)AliasPage);
        return;
    }

    PrevQword = *(volatile ULONG64 *)(AliasPage + ((QwordOffset >= sizeof(ULONG64)) ?
                                                   (QwordOffset - sizeof(ULONG64)) :
                                                   QwordOffset));
    ThisQword = *(volatile ULONG64 *)(AliasPage + QwordOffset);
    NextQword = *(volatile ULONG64 *)(AliasPage +
                                      (((QwordOffset + sizeof(ULONG64)) < PAGE_SIZE) ?
                                       (QwordOffset + sizeof(ULONG64)) :
                                       QwordOffset));

    DPRINT1("[arm64][SErrorPoolQ] page=%p pfn=%Ix off=0x%03Ix q[-1]=0x%016llx "
            "q[0]=0x%016llx q[+1]=0x%016llx\n",
            (PVOID)PageVa,
            (ULONG_PTR)DataPfn,
            FaultOffset,
            (unsigned long long)PrevQword,
            (unsigned long long)ThisQword,
            (unsigned long long)NextQword);

    KiArm64DumpKernelWalk("pool-far", PageVa);
    KiArm64DumpKernelWalk("pool-alias", AliasPage);

    for (ULONG_PTR Offset = 0;
         Offset + sizeof(POOL_HEADER) < PAGE_SIZE;
         Offset += sizeof(ULONG64))
    {
        PPOOL_HEADER Entry = (PPOOL_HEADER)(AliasPage + Offset);
        ULONG_PTR EntryStart = PageVa + Offset;
        ULONG_PTR EntryEnd;

        if (!KiArm64ValidatePoolHeader((PVOID)AliasPage, Entry, PagedPool))
            continue;

        ValidHeaders++;
        EntryEnd = EntryStart + (Entry->BlockSize * POOL_BLOCK_SIZE);
        if (((ULONG_PTR)Far >= EntryStart) && ((ULONG_PTR)Far < EntryEnd))
        {
            Match = Entry;
            MatchOffset = Offset;
            MatchEnd = EntryEnd;
            break;
        }
    }

    if (Match != NULL)
    {
        DPRINT1("[arm64][SErrorPool] match page=%p hdr=%p range=%p..%p "
                "off=0x%03Ix bsz=%u prev=%u type=0x%x tag=%.4s billed=%p "
                "u1=0x%08lx\n",
                (PVOID)PageVa,
                (PVOID)(PageVa + MatchOffset),
                (PVOID)(PageVa + MatchOffset),
                (PVOID)MatchEnd,
                MatchOffset,
                Match->BlockSize,
                Match->PreviousSize,
                Match->PoolType,
                (PCHAR)&Match->PoolTag,
                Match->ProcessBilled,
                Match->Ulong1);
    }
    else
    {
        DPRINT1("[arm64][SErrorPool] no-small-pool-match page=%p pfn=%Ix "
                "off=0x%03Ix validHeaders=%lu\n",
                (PVOID)PageVa,
                (ULONG_PTR)DataPfn,
                FaultOffset,
                ValidHeaders);
    }
}

FORCEINLINE
ULONG
KiArm64GetPteAttrIndex(
    _In_ MMPTE Pte)
{
    return (ULONG)(Pte.u.Hard.CacheType |
                   (Pte.u.Hard.OsAvailable2 << 2));
}

static
VOID
KiArm64DumpKernelWalk(
    _In_z_ PCSTR Tag,
    _In_ ULONG64 Va)
{
    ULONG64 Ttbr1 = 0, RootPa = 0;
    ULONG64 L0Entry = 0, L1Entry = 0, L2Entry = 0, L3Entry = 0;
    ULONG64 FinalEntry = 0;
    volatile ULONG64 *Table;
    PCSTR Level = "none";
    MMPTE HwPte;

    HwPte.u.Long = 0;

    if ((ULONG_PTR)Va < (ULONG_PTR)MmSystemRangeStart)
        return;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    RootPa = Ttbr1 & ARM64_PTE_ADDR_MASK;
    if (RootPa == 0)
        goto Dump;

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
    L0Entry = Table[MiAddressToPxi((PVOID)(ULONG_PTR)Va)];
    if ((L0Entry & 1ULL) == 0)
        goto Dump;

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L0Entry & ARM64_PTE_ADDR_MASK);
    L1Entry = Table[((ULONG_PTR)Va >> PPI_SHIFT) & PPI_MASK];
    if ((L1Entry & 1ULL) == 0)
        goto Dump;

    if ((L1Entry & 3ULL) == 1ULL)
    {
        FinalEntry = L1Entry;
        Level = "l1blk";
        goto Dump;
    }

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L1Entry & ARM64_PTE_ADDR_MASK);
    L2Entry = Table[MiAddressToPdi((PVOID)(ULONG_PTR)Va)];
    if ((L2Entry & 1ULL) == 0)
        goto Dump;

    if ((L2Entry & 3ULL) == 1ULL)
    {
        FinalEntry = L2Entry;
        Level = "l2blk";
        goto Dump;
    }

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L2Entry & ARM64_PTE_ADDR_MASK);
    L3Entry = Table[MiAddressToPti((PVOID)(ULONG_PTR)Va)];
    if ((L3Entry & 1ULL) != 0)
    {
        FinalEntry = L3Entry;
        Level = "l3pg";
    }

Dump:
    HwPte.u.Long = FinalEntry;

    DPRINT1("[arm64][KWALK] %s va=%p ttbr1=0x%016llx root=0x%016llx "
            "l0=0x%016llx l1=0x%016llx l2=0x%016llx l3=0x%016llx "
            "lvl=%s eff=0x%016llx pfn=%Ix sh=%u attr=%u af=%u ng=%u pxn=%u "
            "uxn=%u ro=%u us=%u\n",
            Tag,
            (PVOID)(ULONG_PTR)Va,
            (unsigned long long)Ttbr1,
            (unsigned long long)RootPa,
            (unsigned long long)L0Entry,
            (unsigned long long)L1Entry,
            (unsigned long long)L2Entry,
            (unsigned long long)L3Entry,
            Level,
            (unsigned long long)FinalEntry,
            (ULONG_PTR)((FinalEntry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT),
            HwPte.u.Hard.Shareability,
            KiArm64GetPteAttrIndex(HwPte),
            HwPte.u.Hard.Accessed,
            HwPte.u.Hard.NonGlobal,
            HwPte.u.Hard.PrivilegedNoExecute,
            HwPte.u.Hard.UserNoExecute,
            HwPte.u.Hard.NotDirty,
            HwPte.u.Hard.Owner);
}

static
BOOLEAN
KiArm64ShouldTraceSystemDllAddress(
    _In_opt_ PEPROCESS Process,
    _In_ ULONG64 Va)
{
    ULONG_PTR Base;
    ULONG_PTR Address;

    if ((Process == NULL) || (PspSystemDllBase == NULL))
        return FALSE;

    Address = (ULONG_PTR)Va;
    if (Address >= (ULONG_PTR)MmSystemRangeStart)
        return FALSE;

    Base = (ULONG_PTR)PspSystemDllBase;
    return ((Address >= Base) && (Address < (Base + 0x200000)));
}

static
VOID
KiArm64DumpUserWalk(
    _In_z_ PCSTR Tag,
    _In_ ULONG64 Va)
{
    PFN_NUMBER PteFrame = 0;
    PMMPTE PointerPte;
    MMPTE HwPte;
    ULONG64 Ttbr0 = 0, RootPa = 0;
    ULONG64 L0Entry = 0, L1Entry = 0, L2Entry = 0, L3Entry = 0;
    ULONG Depth = 0;

    PointerPte = MiArm64UserPteKseg0ForPfn((PVOID)(ULONG_PTR)PAGE_ALIGN(Va), &PteFrame);
    if (PointerPte != NULL)
    {
        HwPte.u.Long = PointerPte->u.Long;
    }
    else
    {
        HwPte.u.Long = 0;
    }

    if ((ULONG_PTR)Va < (ULONG_PTR)MmSystemRangeStart)
    {
        volatile ULONG64 *Table;
        ULONG Index;

        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
        RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;
        if (RootPa != 0)
        {
            Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
            Index = ((ULONG_PTR)Va >> 39) & 0x1FF;
            L0Entry = Table[Index];
            if ((L0Entry & 0x3ULL) == 0x3ULL)
            {
                Depth = 1;
                Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L0Entry & ARM64_PTE_ADDR_MASK);
                Index = ((ULONG_PTR)Va >> 30) & 0x1FF;
                L1Entry = Table[Index];
                if ((L1Entry & 0x1ULL) != 0)
                {
                    Depth = 2;
                    if ((L1Entry & 0x3ULL) == 0x3ULL)
                    {
                        Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L1Entry & ARM64_PTE_ADDR_MASK);
                        Index = ((ULONG_PTR)Va >> 21) & 0x1FF;
                        L2Entry = Table[Index];
                        if ((L2Entry & 0x1ULL) != 0)
                        {
                            Depth = 3;
                            if ((L2Entry & 0x3ULL) == 0x3ULL)
                            {
                                Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L2Entry & ARM64_PTE_ADDR_MASK);
                                Index = ((ULONG_PTR)Va >> 12) & 0x1FF;
                                L3Entry = Table[Index];
                                if ((L3Entry & 0x3ULL) == 0x3ULL)
                                    Depth = 4;
                            }
                        }
                    }
                }
            }
        }
    }

    DPRINT1("[arm64][UserWalk] %s va=%p depth=%lu root=0x%016llx "
            "l0=0x%016llx l1=0x%016llx l2=0x%016llx l3=0x%016llx "
            "pte=%p val=0x%016llx pteframe=%Ix sh=%u attr=%u ng=%u pxn=%u "
            "uxn=%u ro=%u us=%u\n",
            Tag,
            (PVOID)(ULONG_PTR)Va,
            Depth,
            (unsigned long long)RootPa,
            (unsigned long long)L0Entry,
            (unsigned long long)L1Entry,
            (unsigned long long)L2Entry,
            (unsigned long long)L3Entry,
            PointerPte,
            (unsigned long long)HwPte.u.Long,
            (ULONG_PTR)PteFrame,
            HwPte.u.Hard.Shareability,
            KiArm64GetPteAttrIndex(HwPte),
            HwPte.u.Hard.NonGlobal,
            HwPte.u.Hard.PrivilegedNoExecute,
            HwPte.u.Hard.UserNoExecute,
            HwPte.u.Hard.NotDirty,
            HwPte.u.Hard.Owner);
}

static
VOID
KiArm64DumpUserAliasQwords(
    _In_z_ PCSTR Tag,
    _In_ ULONG64 Va,
    _In_ ULONG Offset0,
    _In_ ULONG Offset1,
    _In_ ULONG Offset2)
{
    PMMPTE PointerPte;
    PMMPFN PfnEntry;
    PFN_NUMBER PteFrame = 0;
    PFN_NUMBER DataPfn;
    ULONG_PTR AliasVa;
    ULONG64 Qword0, Qword1, Qword2;

    PointerPte = MiArm64UserPteKseg0ForPfn((PVOID)(ULONG_PTR)PAGE_ALIGN(Va), &PteFrame);
    if ((PointerPte == NULL) || (PointerPte->u.Hard.Valid == 0))
    {
        DPRINT1("[arm64][UALIAS] %s va=%p missing pte=%p pteframe=%Ix\n",
                Tag,
                (PVOID)(ULONG_PTR)Va,
                PointerPte,
                (ULONG_PTR)PteFrame);
        return;
    }

    DataPfn = PFN_FROM_PTE(PointerPte);
    AliasVa = MI_ARM64_PFN_TO_VA(DataPfn);
    Offset0 = ALIGN_DOWN_BY(Offset0 & (PAGE_SIZE - 1), sizeof(ULONG64));
    Offset1 = ALIGN_DOWN_BY(Offset1 & (PAGE_SIZE - 1), sizeof(ULONG64));
    Offset2 = ALIGN_DOWN_BY(Offset2 & (PAGE_SIZE - 1), sizeof(ULONG64));

    if (!KiArm64IsKernelAddressMappedNoFault(AliasVa))
    {
        DPRINT1("[arm64][UALIAS] %s va=%p pfn=%Ix pteframe=%Ix alias=%p not mapped in KSEG0\n",
                Tag,
                (PVOID)(ULONG_PTR)Va,
                (ULONG_PTR)DataPfn,
                (ULONG_PTR)PteFrame,
                (PVOID)AliasVa);
        return;
    }

    Qword0 = *(volatile ULONG64 *)(AliasVa + Offset0);
    Qword1 = *(volatile ULONG64 *)(AliasVa + Offset1);
    Qword2 = *(volatile ULONG64 *)(AliasVa + Offset2);
    PfnEntry = MI_PFN_ELEMENT(DataPfn);

    DPRINT1("[arm64][UALIAS] %s va=%p pfn=%Ix pteframe=%Ix pteaddr=%p "
            "loc=%u cache=%u ref=%u share=%u q[%03x]=0x%016llx q[%03x]=0x%016llx "
            "q[%03x]=0x%016llx\n",
            Tag,
            (PVOID)(ULONG_PTR)Va,
            (ULONG_PTR)DataPfn,
            (ULONG_PTR)PteFrame,
            PfnEntry ? PfnEntry->PteAddress : NULL,
            PfnEntry ? (ULONG)PfnEntry->u3.e1.PageLocation : 0U,
            PfnEntry ? (ULONG)PfnEntry->u3.e1.CacheAttribute : 0U,
            PfnEntry ? (ULONG)PfnEntry->u3.e2.ReferenceCount : 0U,
            PfnEntry ? (ULONG)PfnEntry->u2.ShareCount : 0U,
            Offset0,
            (unsigned long long)Qword0,
            Offset1,
            (unsigned long long)Qword1,
            Offset2,
            (unsigned long long)Qword2);
}

extern BOOLEAN KdDebuggerEnabled;
extern BOOLEAN KdDebuggerNotPresent;
extern BOOLEAN KdPitchDebugger;

static
BOOLEAN
KiArm64FixupUserAccessFlagFault(
    _In_ PVOID FaultAddress,
    _In_ ULONG FaultStatus)
{
    ULONG64 Ttbr0, RootPa;
    volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
    ULONG64 L0Entry, L1Entry, L2Entry, L3Entry;

    /* Access Flag fault codes: 0x9/0xA/0xB for levels 1/2/3 */
    if ((FaultStatus != 0x9) && (FaultStatus != 0xA) && (FaultStatus != 0xB))
        return FALSE;

    if ((ULONG_PTR)FaultAddress >= (ULONG_PTR)MmSystemRangeStart)
        return FALSE;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;
    if (RootPa == 0)
        return FALSE;

    L0Idx = ((ULONG64)(ULONG_PTR)FaultAddress >> 39) & 0x1FF;
    L1Idx = ((ULONG64)(ULONG_PTR)FaultAddress >> 30) & 0x1FF;
    L2Idx = ((ULONG64)(ULONG_PTR)FaultAddress >> 21) & 0x1FF;
    L3Idx = ((ULONG64)(ULONG_PTR)FaultAddress >> 12) & 0x1FF;

    L0Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
    L0Entry = L0Table[L0Idx];
    if ((L0Entry & 0x3ULL) != 0x3ULL)
        return FALSE;

    L1Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L0Entry & ARM64_PTE_ADDR_MASK);
    L1Entry = L1Table[L1Idx];
    if ((L1Entry & 0x1ULL) == 0)
        return FALSE;
    if ((FaultStatus == 0x9) && ((L1Entry & 0x3ULL) == 0x1ULL))
    {
        if ((L1Entry & (1ULL << 10)) == 0)
            L1Table[L1Idx] = (L1Entry | (1ULL << 10));
        goto TlbFlush;
    }
    if ((L1Entry & 0x3ULL) != 0x3ULL)
        return FALSE;

    L2Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L1Entry & ARM64_PTE_ADDR_MASK);
    L2Entry = L2Table[L2Idx];
    if ((L2Entry & 0x1ULL) == 0)
        return FALSE;
    if ((FaultStatus == 0xA) && ((L2Entry & 0x3ULL) == 0x1ULL))
    {
        if ((L2Entry & (1ULL << 10)) == 0)
            L2Table[L2Idx] = (L2Entry | (1ULL << 10));
        goto TlbFlush;
    }
    if ((L2Entry & 0x3ULL) != 0x3ULL)
        return FALSE;

    L3Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L2Entry & ARM64_PTE_ADDR_MASK);
    L3Entry = L3Table[L3Idx];
    if ((FaultStatus == 0xB) && ((L3Entry & 0x3ULL) == 0x3ULL))
    {
        if ((L3Entry & (1ULL << 10)) == 0)
            L3Table[L3Idx] = (L3Entry | (1ULL << 10));
        goto TlbFlush;
    }

    return FALSE;

TlbFlush:
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vaae1is, %0" :: "r"((ULONG_PTR)FaultAddress >> PAGE_SHIFT) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
    return TRUE;
}

static __inline VOID
KiArm64StageLogf(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[192];
    va_list Args;

    va_start(Args, Format);
    if (NT_SUCCESS(RtlStringCbVPrintfA(Buffer, sizeof(Buffer), Format, Args)))
    {
        DPRINT1("%s\n", Buffer);
    }
    va_end(Args);
}

#ifndef KI_ARM64_STAGE_LOGF
#define KI_ARM64_STAGE_LOGF(...) KiArm64StageLogf(__VA_ARGS__)
#endif

NTSTATUS
NTAPI
MmArmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation);

/*
 * MmAccessFault: The dispatch function that routes page faults to the
 * appropriate handler (MmArmAccessFault for ARM3 allocations, or
 * MmNotPresentFaultSectionView for ROS section views like VACB buffers).
 *
 * ARM64 MUST call this instead of MmArmAccessFault to properly handle
 * kernel section views created by the ReactOS memory manager.
 */
NTSTATUS
NTAPI
MmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation);

static KIRQL KiArm64LastDataAbortIrql;
static LONG KiArm64HighIrqlFaultLogBudget = 16;

VOID
KiSystemService(
    _Inout_ PKTHREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Instruction);

VOID
NTAPI
KiSwapProcess(_Inout_ PKPROCESS NewProcess,
              _Inout_ PKPROCESS OldProcess)
{
    ASSERT(NewProcess != NULL);

#ifdef CONFIG_SMP
    {
        PKIPCR Pcr = KeGetPcr();
        if (Pcr != NULL)
        {
            KAFFINITY Member = Pcr->Prcb.SetMember;

            InterlockedOr64((PLONG64)&NewProcess->ActiveProcessors, (LONG64)Member);
            if ((OldProcess != NULL) && (OldProcess != NewProcess))
            {
                InterlockedAnd64((PLONG64)&OldProcess->ActiveProcessors, (LONG64)~Member);
            }
        }
    }
#endif

    if (OldProcess == NewProcess)
    {
        /* Same process, nothing to do */
        return;
    }

    if ((OldProcess != NULL) &&
        (NewProcess->DirectoryTableBase[0] == OldProcess->DirectoryTableBase[0]) &&
        (NewProcess->DirectoryTableBase[1] == OldProcess->DirectoryTableBase[1]))
    {
        /* Same TTBR roots, nothing to do */
        return;
    }

    ASSERT(NewProcess->DirectoryTableBase[0] != 0);
    ASSERT(NewProcess->DirectoryTableBase[1] != 0);

    KiArm64WriteUserTtbr(NewProcess->DirectoryTableBase[0],
                         NewProcess->DirectoryTableBase[1]);
}

typedef struct _ARM64_EARLY_SYNC_CONTEXT
{
    ARM64_EARLY_TRAP_STATE State;
    PKTRAP_FRAME TrapFramePointer;
    PKEXCEPTION_FRAME ExceptionFramePointer;
    KTRAP_FRAME TrapFrame;
    KEXCEPTION_FRAME ExceptionFrame;
} ARM64_EARLY_SYNC_CONTEXT, *PARM64_EARLY_SYNC_CONTEXT;

C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.VectorId) == 0x0);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.ExceptionSyndrome) == 0x8);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.FaultAddress) == 0x10);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Elr) == 0x18);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Spsr) == 0x20);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.X[0]) == 0x28);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.Sp) == 0x120);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.Pc) == 0x128);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.Pstate) == 0x130);
C_ASSERT(sizeof(ARM64_EARLY_TRAP_STATE) == 0x138);  /* State should end at 0x138 */
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, TrapFramePointer) == 0x138);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, ExceptionFramePointer) == 0x140);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, TrapFrame) == 0x148);
#define ARM64_EARLY_SYNC_CONTEXT_ALLOC_SIZE 0x590
C_ASSERT(sizeof(ARM64_EARLY_SYNC_CONTEXT) <= ARM64_EARLY_SYNC_CONTEXT_ALLOC_SIZE);

/*
 * KiArm64PreviousModeFromContext - Determine the true previous mode
 *
 * On ARM64, when kernel code dereferences a NULL pointer (e.g., accessing
 * offset 4 from NULL), the FAR contains a low user address (0x4), but the
 * ELR contains the kernel address where the fault occurred.
 *
 * SPSR.M[3:0] should indicate EL1 for kernel faults, but in some scenarios
 * (possibly related to exception nesting or SPSR caching), it may be 0 (EL0).
 *
 * To correctly identify kernel faults, we check BOTH:
 * 1. SPSR.M[3:0] - the processor mode bits
 * 2. ELR - the exception link register (faulting instruction address)
 *
 * If ELR is in kernel space (>= MmSystemRangeStart), the fault came from
 * kernel code, regardless of what SPSR says. This ensures NULL pointer
 * dereferences in kernel code are properly treated as kernel faults.
 */
static
KPROCESSOR_MODE
KiArm64PreviousModeFromContext(
    _In_ ULONG64 SpsrValue,
    _In_ ULONG64 ElrValue)
{
    ULONG Mode = (ULONG)(SpsrValue & 0xFULL);

    /*
     * If SPSR.M indicates EL1 (non-zero), it's definitely a kernel fault.
     */
    if (Mode != 0)
    {
        return KernelMode;
    }

    /*
     * SPSR.M is 0 (EL0), but check ELR to catch kernel NULL pointer dereferences.
     * If ELR is in kernel space, the faulting instruction was in the kernel,
     * so this is a kernel fault even though FAR may be a user address.
     *
     * Use KSEG0_BASE (compile-time constant) instead of MmSystemRangeStart
     * because MmSystemRangeStart may not be initialized during early boot.
     */
    if (ElrValue >= KSEG0_BASE)
    {
        return KernelMode;
    }

    /*
     * Both SPSR.M is 0 (EL0) and ELR is in user space - this is a true user fault.
     */
    return UserMode;
}

static
VOID
KiArm64InitializeTrapFrame(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context,
    _Out_ PKTRAP_FRAME TrapFrame)
{
    ULONG64 Fpcr = 0;
    ULONG64 Fpsr = 0;
    PKEXCEPTION_FRAME ExceptionFrame = &Context->ExceptionFrame;
    KIRQL CurrentIrql;

    RtlZeroMemory(TrapFrame, sizeof(*TrapFrame));

    /*
     * Capture IRQL BEFORE any potential IRQL changes during exception handling.
     * For synchronous exceptions (data/instruction abort), we're at the IRQL
     * that was active when the fault occurred. This is critical for Windows
     * ARM64 compliance - the trap frame must preserve the interrupted IRQL.
     */
    CurrentIrql = KeGetCurrentIrql();

    TrapFrame->PreviousMode = (CHAR)KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);
    TrapFrame->PreviousIrql = (UCHAR)CurrentIrql;
    TrapFrame->TrapFrame = (ULONG64)(ULONG_PTR)TrapFrame;
    TrapFrame->FaultAddress = Context->State.FaultAddress;
    TrapFrame->Spsr = (ULONG)Context->State.Spsr;
    TrapFrame->Esr = (ULONG)Context->State.ExceptionSyndrome;
    TrapFrame->Sp = Context->State.Registers.Sp;
    TrapFrame->Pc = Context->State.Elr;
    TrapFrame->Lr = Context->State.Registers.X[30];
    TrapFrame->Fp = Context->State.Registers.X[29];

    RtlCopyMemory(TrapFrame->X,
                  Context->State.Registers.X,
                  sizeof(TrapFrame->X));

    RtlZeroMemory(ExceptionFrame, sizeof(*ExceptionFrame));
    ExceptionFrame->TrapFrame = (ULONG64)(ULONG_PTR)TrapFrame;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(Fpcr));
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(Fpsr));
    ExceptionFrame->Fpcr = Fpcr;
    ExceptionFrame->Fpsr = Fpsr;
    ExceptionFrame->X19 = Context->State.Registers.X[19];
    ExceptionFrame->X20 = Context->State.Registers.X[20];
    ExceptionFrame->X21 = Context->State.Registers.X[21];
    ExceptionFrame->X22 = Context->State.Registers.X[22];
    ExceptionFrame->X23 = Context->State.Registers.X[23];
    ExceptionFrame->X24 = Context->State.Registers.X[24];
    ExceptionFrame->X25 = Context->State.Registers.X[25];
    ExceptionFrame->X26 = Context->State.Registers.X[26];
    ExceptionFrame->X27 = Context->State.Registers.X[27];
    ExceptionFrame->X28 = Context->State.Registers.X[28];
    ExceptionFrame->Fp = Context->State.Registers.X[29];
    ExceptionFrame->Lr = Context->State.Registers.X[30];
    Context->ExceptionFramePointer = ExceptionFrame;
}

static LONG KiArm64SyncExceptionLogBudget = 128;
static LONG KiArm64SessionFaultLogBudget = 16;
extern volatile ULONG_PTR MiArm64SessionWsStage;
extern volatile ULONG_PTR MiArm64SessionWsFp;
extern volatile ULONG_PTR MiArm64SessionWsLr;
extern volatile ULONG_PTR MiArm64SessionWsSavedFp;
extern volatile ULONG_PTR MiArm64SessionWsSavedLr;
extern volatile ULONG_PTR MiArm64SessionWsSp;
/*
 * One-shot trap guard per CPU to prevent recursive exception storms before
 * the debugger is fully operational. Set on first entry; any re-entry while
 * set triggers an immediate bugcheck with the captured context.
 */
static volatile LONG KiArm64TrapActive[MAXIMUM_PROCESSORS] = {0};

static
VOID
KiArm64DeliverPendingUserApc(
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    KIRQL OldIrql;
    BOOLEAN Raised = FALSE;

    if (TrapFrame->PreviousMode != UserMode)
    {
        return;
    }

    Thread = KeGetCurrentThread();
    if (!Thread->ApcState.UserApcPending)
    {
        return;
    }

    OldIrql = KeGetCurrentIrql();
    if (OldIrql > APC_LEVEL)
    {
        return;
    }

    Thread->Alerted[KernelMode] = FALSE;

    if (OldIrql < APC_LEVEL)
    {
        KeRaiseIrql(APC_LEVEL, &OldIrql);
        Raised = TRUE;
    }

    _enable();
    KiDeliverApc(UserMode, ExceptionFrame, TrapFrame);
    _disable();

    if (Raised)
    {
        KeLowerIrql(OldIrql);
    }
}

static
VOID
KiArm64ReleaseWorkingSetsForBugCheck(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS Process = PsGetCurrentProcess();
    BOOLEAN Raised = FALSE;
    KIRQL PreviousIrql = KeGetCurrentIrql();

    if (PreviousIrql < APC_LEVEL)
    {
        KeRaiseIrql(APC_LEVEL, &PreviousIrql);
        Raised = TRUE;
    }

    if (Thread->OwnsSystemWorkingSetExclusive || Thread->OwnsSystemWorkingSetShared)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   "[arm64] KiArm64ReleaseWorkingSets: releasing system WS=%p thread=%p mutex=%p count=0x%llx\n",
                   &MmSystemCacheWs,
                   Thread,
                   &MmSystemCacheWs.WorkingSetMutex,
                   (unsigned long long)MmSystemCacheWs.WorkingSetMutex.Value);
        MiUnlockWorkingSet(Thread, &MmSystemCacheWs);
    }

    if ((Thread->OwnsSessionWorkingSetExclusive || Thread->OwnsSessionWorkingSetShared) &&
        (MmSessionSpace != NULL))
    {
        MiUnlockWorkingSet(Thread, &MmSessionSpace->GlobalVirtualAddress->Vm);
    }

    if (Thread->OwnsProcessWorkingSetExclusive || Thread->OwnsProcessWorkingSetShared)
    {
        if (Process != NULL)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_TRACE_LEVEL,
                       "[arm64] KiArm64ReleaseWorkingSets: releasing process WS thread=%p process=%p vm=%p mutex=%p count=0x%llx\n",
                       Thread,
                       Process,
                       &Process->Vm,
                       &Process->Vm.WorkingSetMutex,
                       (unsigned long long)Process->Vm.WorkingSetMutex.Value);
            MiUnlockProcessWorkingSetUnsafe(Process, Thread);
        }
    }

    if (Raised)
    {
        KeLowerIrql(PreviousIrql);
    }
}

static __inline VOID
KiArm64ClearTrapActive(VOID)
{
    ULONG ProcessorIndex = KeGetCurrentProcessorNumber();

    if (ProcessorIndex < MAXIMUM_PROCESSORS)
    {
        InterlockedExchange(&KiArm64TrapActive[ProcessorIndex], 0);
    }
}

#define KI_ARM64_ACCESS_READ    0
#define KI_ARM64_ACCESS_WRITE   1
#define KI_ARM64_ACCESS_EXECUTE 8

static __inline ULONG_PTR
KiArm64AccessTypeToExceptionInfo(
    _In_ BOOLEAN WriteAccess,
    _In_ BOOLEAN InstructionFetch)
{
    if (InstructionFetch)
    {
        return KI_ARM64_ACCESS_EXECUTE;
    }

    return WriteAccess ? KI_ARM64_ACCESS_WRITE : KI_ARM64_ACCESS_READ;
}

static
ULONG
KiArm64BuildFaultCode(
    _In_ ULONG FaultStatus,
    _In_ BOOLEAN WriteAccess,
    _In_ BOOLEAN InstructionFetch,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Code = 0;

    /* Prevent unused warnings for bring-up-only debug guards on GCC/MinGW. */
    if (0)
    {
        (void)KiArm64SyncExceptionLogBudget;
        KiArm64ReleaseWorkingSetsForBugCheck();
    }

    switch (FaultStatus & 0x3FULL)
    {
        case 0x00: /* Address size fault level 0 */
        case 0x01: /* Address size fault level 1 */
        case 0x02: /* Address size fault level 2 */
        case 0x03: /* Address size fault level 3 */
        case 0x04: /* Translation fault level 0 */
        case 0x05: /* Translation fault level 1 */
        case 0x06: /* Translation fault level 2 */
        case 0x07: /* Translation fault level 3 */
            /* Treat as not-present fault (bit 0 cleared) */
            break;

        default:
            Code |= 0x1; /* Present */
            break;
    }

    if (WriteAccess)
    {
        Code |= 0x2;
    }

    if (InstructionFetch)
    {
        Code |= 0x20;
    }

    if (PreviousMode == UserMode)
    {
        Code |= 0x4;
    }

    return Code;
}

static
VOID
KiArm64ReportUnhandledSyncException(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context,
    _In_ ULONG Esr)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Esr);
}

static volatile LONG KiArm64FirstCrashPrinted;

DECLSPEC_NORETURN
VOID
KiArm64BugCheckSynchronousException(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context)
{
    /*
     * Even if ELR is in low VA (e.g. NULL), proceed to a controlled
     * bugcheck with a reconstructed trap frame. Spinning here hides the
     * original fault and trips the watchdog.
     */
    KTRAP_FRAME TrapFrame;
    ULONG Esr = (ULONG)(Context->State.ExceptionSyndrome & 0xFFFFFFFFULL);
    ULONG EsrClass = (Esr >> 26) & 0x3FULL;
    BOOLEAN WriteAccess = (Esr & (1u << 6)) != 0;

    /* Reconstruct a trap frame so the bugcheck dump has architectural state. */
    KiArm64InitializeTrapFrame(Context, &TrapFrame);

    /*
     * Enable KD so "*** Fatal System Error" prints via DbgPrint in KeBugCheckWithTf.
     * Set KdPitchDebugger to prevent KD re-initialization attempts that could
     * cause faults during the bugcheck path. This ensures we get crash output
     * without risking infinite fault loops from KdEnableDebuggerWithLock.
     */
    KdDebuggerEnabled = TRUE;
    KdDebuggerNotPresent = TRUE;  /* No interactive debugger attached */
    KdPitchDebugger = TRUE;       /* Prevent KD re-init which can fault */

    /* Print first crash info */
    if (InterlockedCompareExchange(&KiArm64FirstCrashPrinted, 1, 0) == 0)
    {
        KI_ARM64_STAGE_LOGF("[arm64] FirstCrash: vec=%lu esr=0x%lx elr=%p far=%p",
                           (ULONG)Context->State.VectorId,
                           (ULONG)Context->State.ExceptionSyndrome,
                           (PVOID)(ULONG_PTR)Context->State.Elr,
                           (PVOID)(ULONG_PTR)Context->State.FaultAddress);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "[arm64] CRASH regs: elr=%p far=%p x0=%p x1=%p lr=%p fp=%p cpu=%lu\n",
                   (PVOID)(ULONG_PTR)Context->State.Elr,
                   (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                   (PVOID)(ULONG_PTR)TrapFrame.X[0],
                   (PVOID)(ULONG_PTR)TrapFrame.X[1],
                   (PVOID)(ULONG_PTR)TrapFrame.Lr,
                   (PVOID)(ULONG_PTR)TrapFrame.Fp,
                   (ULONG)KeGetCurrentProcessorNumber());
        {
            PULONG_PTR FpWalk = (PULONG_PTR)(ULONG_PTR)TrapFrame.Fp;
            ULONG Depth;

            for (Depth = 0; Depth < 8; Depth++)
            {
                if (((ULONG_PTR)FpWalk < (ULONG_PTR)MmSystemRangeStart) ||
                    ((ULONG_PTR)FpWalk & 0x7) ||
                    !MmIsAddressValid(FpWalk) ||
                    !MmIsAddressValid(FpWalk + 1))
                {
                    break;
                }
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                           "[arm64] CRASH frame[%lu]: ret=%p\n",
                           Depth, (PVOID)FpWalk[1]);
                FpWalk = (PULONG_PTR)FpWalk[0];
            }
        }
    }

#ifdef KDBG
    /*
     * Call KDBG to display crash diagnostics before bugcheck.
     * This is the last safe point to show crash info.
     */
    {
        EXCEPTION_RECORD64 ExceptionRecord64;
        CONTEXT KdbContext;

        RtlZeroMemory(&ExceptionRecord64, sizeof(ExceptionRecord64));
        /* Determine exception code based on ESR class */
        if (EsrClass == 0x24 || EsrClass == 0x25)
        {
            ExceptionRecord64.ExceptionCode = STATUS_ACCESS_VIOLATION;
            ExceptionRecord64.NumberParameters = 2;
            ExceptionRecord64.ExceptionInformation[0] = WriteAccess ? 1 : 0;
            ExceptionRecord64.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;
        }
        else
        {
            ExceptionRecord64.ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
            ExceptionRecord64.NumberParameters = 0;
        }
        ExceptionRecord64.ExceptionFlags = 0;
        ExceptionRecord64.ExceptionAddress = Context->State.Elr;

        RtlZeroMemory(&KdbContext, sizeof(KdbContext));
        KdbContext.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_ARM64;
        KeTrapFrameToContext(&TrapFrame, NULL, &KdbContext);

        KdbEnterDebuggerException(&ExceptionRecord64, KernelMode, &KdbContext, FALSE);
    }
#endif /* KDBG */

    KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN,
                     (ULONG_PTR)Context->State.VectorId,
                     (ULONG_PTR)Context->State.ExceptionSyndrome,
                     (ULONG_PTR)Context->State.FaultAddress,
                     (ULONG_PTR)Context->State.Elr,
                     &TrapFrame);

    /* ARM64: __builtin_unreachable() generates trap instruction, avoid it */
    while (1) { }
}


BOOLEAN
KiArm64HandleSynchronousException(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context)
{
    ULONG Esr = (ULONG)(Context->State.ExceptionSyndrome & 0xFFFFFFFFULL);
    ULONG EsrClass = (Esr >> 26) & 0x3FULL;
    ULONG Iss = Esr & 0x01FFFFFFUL;
    ULONG FaultStatus = Iss & 0x3FULL;
    PKTRAP_FRAME TrapFrame;
    KPROCESSOR_MODE PreviousMode;
    NTSTATUS Status;
    BOOLEAN WriteAccess;

    __asm__ __volatile__("" ::: "memory");

    Context->TrapFramePointer = NULL;
    Context->ExceptionFramePointer = NULL;

    /*
     * One-shot guard to avoid recursive exception storms - KERNEL MODE ONLY.
     *
     * User-mode faults (ELR in user range, SPSR.M=EL0) must NOT trigger
     * this guard because each user-mode exception entry is independent.
     * A second user-mode fault at the same VA after ERET (e.g., stale
     * I-cache causing EC=0x00 after a resolved EC=0x20) is a new fault,
     * not a recursive kernel exception storm.
     *
     * Previously this guard fired for ALL faults, causing spurious
     * "Invalid Opcode" bugchecks when lsass.exe/services.exe re-faulted
     * at KiUserApcDispatcher after instruction abort resolution.
     */
    {
        ULONG CpuIndex = KeGetCurrentProcessorNumber();
        KPROCESSOR_MODE FaultMode = KiArm64PreviousModeFromContext(
            Context->State.Spsr, Context->State.Elr);

        if ((EsrClass != ESR_EC_BRK) &&
            (CpuIndex < MAXIMUM_PROCESSORS) &&
            (FaultMode == KernelMode))
        {
            if (InterlockedCompareExchange(&KiArm64TrapActive[CpuIndex], 1, 0) != 0)
            {
                KiArm64BugCheckSynchronousException(Context);
                return TRUE; /* not reached */
            }
        }
    }

    switch (EsrClass)
    {
                case ESR_EC_FP_TRAP:  /* FP/ASIMD access when trapped */
                case ESR_EC_SVE_TRAP: /* SVE access when trapped */
                case ESR_EC_SME_TRAP: /* SME access when trapped */
                {
                    TrapFrame = &Context->TrapFrame;
                    KiArm64InitializeTrapFrame(Context, TrapFrame);

                    if (KiArm64HandleFpTrap(TrapFrame, Esr))
                    {
                        Context->TrapFramePointer = TrapFrame;
                        Context->ExceptionFramePointer = &Context->ExceptionFrame;
                        KiArm64ClearTrapActive();
                        return TRUE;
                    }

                    break;
                }

	        case 0x11: /* SVC from lower EL */
	        case 0x15: /* SVC from same EL */
	        {
	            ULONG Instruction;
	            PKTHREAD Thread;

	            TrapFrame = &Context->TrapFrame;
	            KiArm64InitializeTrapFrame(Context, TrapFrame);

            /*
             * ARM64 FIX: Set up TrapFrame linked list for system calls.
             *
             * On x86-64, the assembly entry code (KiSystemCall64 in trap.S)
             * saves the old Thread->TrapFrame into TrapFrame->TrapFrame and
             * sets Thread->TrapFrame to the new SVC trap frame. On ARM64 we
             * must do this in C since our entry is handled in trapc.c.
             *
             * This linked list is critical because:
             * 1. NtContinue reads Thread->TrapFrame to find the current frame
             * 2. KiGetLinkedTrapFrame reads TrapFrame->TrapFrame to get the
             *    previous frame for unwinding
             * 3. After the syscall, we restore Thread->TrapFrame to the
             *    previous value (matching x86-64 assembly exit behavior)
             *
             * KiArm64InitializeTrapFrame sets TrapFrame->TrapFrame to a
             * self-link; we override it here with the proper previous frame.
             */
            Thread = KeGetCurrentThread();
            KiArm64SavePreviousModeForTrap(Thread, TrapFrame);
            TrapFrame->TrapFrame = (ULONG64)(ULONG_PTR)Thread->TrapFrame;
            Thread->TrapFrame = TrapFrame;

            /*
             * ARM64 System Call Number Encoding
             *
             * X8 carries the full service number including the table selector:
             *   - Bits [11:0]  = service function index (SERVICE_NUMBER_MASK)
             *   - Bit  [12]    = service table selector (0=NT, 1=Win32K)
             *
             * The user-mode stubs (STUB_U in syscalls.inc) set X8 to the full
             * service ID (e.g., 0x0025 for NtOpenFile, 0x10FA for
             * NtUserProcessConnect). SVC #0 is used for all calls.
             *
             * KiSystemService extracts the table index via:
             *   TableIndex = (Instruction >> SERVICE_TABLE_SHIFT) & SERVICE_TABLE_MASK
             * which maps bit 12 to the correct byte offset into the service
             * descriptor table array.
             */
            Instruction = (ULONG)(TrapFrame->X[8] & 0x1FFF);

            /*
             * ARM64 FIX: Clear trap-active BEFORE calling KiSystemService.
             *
             * System calls can legitimately call nested system calls (e.g.,
             * NtCreateSymbolicLinkObject calls ObInsertObject which calls
             * other Nt* functions via Zw* wrappers). Each Zw* wrapper uses
             * SVC #0 to enter the kernel, even when already in kernel mode.
             *
             * If we don't clear the trap flag before KiSystemService, the
             * nested SVC will see KiArm64TrapActive as set and incorrectly
             * treat it as a recursive exception, causing a spurious crash.
             *
             * This is safe because:
             * 1. The trap frame is already initialized
             * 2. KiSystemService handles its own exception safety
             * 3. Any real fault during the syscall will set its own flag
             */
            KiArm64ClearTrapActive();

            KiSystemService(Thread, TrapFrame, Instruction);

            /*
             * ARM64 FIX: Re-read TrapFrame BEFORE accessing it after
             * KiSystemService returns. If PsConvertToGuiThread was called
             * inside KiSystemService (first Win32k syscall), KeSwitchKernelStack
             * copies the kernel stack to a new location and frees the old one.
             * Our local TrapFrame pointer is stale and points to freed memory.
             * Re-read from Thread->TrapFrame immediately.
             */
            TrapFrame = Thread->TrapFrame;

            /*
             * ARM64 FIX: Re-read TrapFrame and restore linked list after
             * KiSystemService returns.
             *
             * KiSystemService does NOT restore Thread->TrapFrame at exit
             * (unlike x86-64 where assembly does this). We do it here,
             * mirroring the x86-64 assembly exit path.
             *
             * If PsConvertToGuiThread was called (first win32k system call),
             * KeSwitchKernelStack copies the kernel stack to a new location
             * and adjusts Thread->TrapFrame. Our local TrapFrame pointer is
             * stale (points to freed old stack). Re-reading from
             * Thread->TrapFrame gets the correct (possibly relocated) address.
             *
             * Then we restore Thread->TrapFrame to the previous value via
             * the linked list (TrapFrame->TrapFrame).
             */
            TrapFrame = Thread->TrapFrame;
            KiArm64DeliverPendingUserApc(&Context->ExceptionFrame,
                                         TrapFrame);
            KiArm64RestorePreviousModeFromTrap(Thread, TrapFrame);
            Thread->TrapFrame = KiGetLinkedTrapFrame(TrapFrame);

            /*
             * ARM64 FIX: Recompute Context from TrapFrame after possible
             * stack switch.
             *
             * If PsConvertToGuiThread called KeSwitchKernelStack, the entire
             * kernel stack was copied to a new location. The local 'Context'
             * pointer still holds the OLD stack address (it was set from x0/sp
             * by the assembly caller before the stack switch). Writing to the
             * old Context would go to freed memory, while the assembly code
             * (trapvec.S) reads TrapFramePointer from the NEW sp-relative
             * address.
             *
             * Since TrapFrame is embedded in Context at a fixed offset
             * (Context->TrapFrame), we can recover the correct Context
             * address from the (possibly relocated) TrapFrame pointer.
             *
             * For non-stack-switch syscalls, this computes the same Context
             * as before, so it's a safe no-op.
             */
            Context = CONTAINING_RECORD(TrapFrame,
                                        ARM64_EARLY_SYNC_CONTEXT,
                                        TrapFrame);
            if (TrapFrame->Pc < 0x10000)
            {
                DPRINT1("[arm64][SVC-RET] instr=0x%lx tf=%p ef=%p pc=%p lr=%p fp=%p sp=%p "
                        "spsr=0x%lx x0=%p x8=%p linked=%p threadtf=%p\n",
                        Instruction,
                        TrapFrame,
                        &Context->ExceptionFrame,
                        (PVOID)(ULONG_PTR)TrapFrame->Pc,
                        (PVOID)(ULONG_PTR)TrapFrame->Lr,
                        (PVOID)(ULONG_PTR)TrapFrame->Fp,
                        (PVOID)(ULONG_PTR)TrapFrame->Sp,
                        TrapFrame->Spsr,
                        (PVOID)(ULONG_PTR)TrapFrame->X0,
                        (PVOID)(ULONG_PTR)TrapFrame->X8,
                        (PVOID)(ULONG_PTR)TrapFrame->TrapFrame,
                        Thread->TrapFrame);
            }
            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            return TRUE;
        }

	        case 0x20: /* Instruction abort, lower EL */
	        case 0x21: /* Instruction abort, same EL  */
	        {
	            EXCEPTION_RECORD ExceptionRecord;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

	            PreviousMode = KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);
	            WriteAccess = FALSE;

            /*
             * ARM64: Clear trap-active and data abort owner flags BEFORE MmAccessFault.
             *
             * MmAccessFault can trigger nested page faults (e.g., SYSCACHE faults
             * during file I/O when reading a code page from disk). If we don't clear
             * KiArm64TrapActive, the nested fault will see the flag set and incorrectly
             * bugcheck as a recursive exception.
             *
             * This matches the data abort handler which also clears these before
             * calling MmAccessFault.
             */
            KiArm64ClearTrapActive();

            /*
             * ARM64: Handle access flag faults for user instruction fetches.
             *
             * Access flag faults (IFSC 0x09-0x0B) mean the page IS present and
             * has correct permissions, but the AF (Access Flag) bit is not set.
             * KiArm64BuildFaultCode classifies these as "present" faults (bit 0=1),
             * which routes them through MmpAccessFault. MmpAccessFault returns
             * STATUS_ACCESS_VIOLATION for instruction fetches, incorrectly killing
             * the process.
             *
             * Fix: Check for access flag faults and fix them up directly via
             * KiArm64FixupUserAccessFlagFault before calling MmAccessFault.
             */
            if ((FaultStatus == 0x9 || FaultStatus == 0xA || FaultStatus == 0xB) &&
                (ULONG_PTR)Context->State.FaultAddress < (ULONG_PTR)MmSystemRangeStart)
            {
                if (KiArm64FixupUserAccessFlagFault(
                        (PVOID)(ULONG_PTR)Context->State.FaultAddress, FaultStatus))
                {
                    Context->TrapFramePointer = TrapFrame;
                    Context->ExceptionFramePointer = &Context->ExceptionFrame;
                    return TRUE;
                }
            }

            Status = MmAccessFault(KiArm64BuildFaultCode(FaultStatus, WriteAccess, TRUE, PreviousMode), (PVOID)(ULONG_PTR)Context->State.FaultAddress, PreviousMode, TrapFrame);

            /* MM refuses not-present faults above APC_LEVEL; that is a fatal IRQL contract violation */
            if (Status == (NTSTATUS)(STATUS_IN_PAGE_ERROR | 0x10000000)) KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL, (ULONG_PTR)Context->State.FaultAddress, KeGetCurrentIrql(), 8, (ULONG_PTR)Context->State.Elr);

            if (NT_SUCCESS(Status))
            {
                KeInvalidateTlbEntry((PVOID)(ULONG_PTR)Context->State.FaultAddress);
                /*
                 * ARM64: cache maintenance after instruction fault resolution.
                 * Keep this path targeted to the faulting page to avoid global
                 * I-cache invalidation on every resolved user fault.
                 */
                {
                    ULONG_PTR Va;
                    ULONG DcacheLineSize, IcacheLineSize;

                    Va = (ULONG_PTR)Context->State.FaultAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);
                    KiArm64GetCacheLineSizes(&DcacheLineSize, &IcacheLineSize);

                    /*
                     * For kernel addresses, invalidate D-cache lines for the page.
                     * For user addresses we only do I-cache maintenance here to avoid
                     * PAN/permission traps on DC operations.
                     */
                    if (Va >= (ULONG_PTR)MmSystemRangeStart)
                    {
                        for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
                        {
                            __asm__ __volatile__("dc ivac, %0" :: "r"(Va + offset) : "memory");
                        }
                        __asm__ __volatile__("dsb ish" ::: "memory");
                    }

                    /* I-cache invalidation is always required for instruction fetches. */
                    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += IcacheLineSize)
                    {
                        __asm__ __volatile__("ic ivau, %0" :: "r"(Va + offset) : "memory");
                    }

                    __asm__ __volatile__("dsb ish" ::: "memory");
                    __asm__ __volatile__("isb" ::: "memory");
                }

                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_ACCESS_VIOLATION;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 2;
            ExceptionRecord.ExceptionInformation[0] = KiArm64AccessTypeToExceptionInfo(WriteAccess, TRUE);
            ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                KiArm64ReportUnhandledSyncException(Context, Esr);

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                PreviousMode,
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        case 0x24: /* Data abort, lower EL */
        case 0x25: /* Data abort, same EL  */
        {
            PETHREAD CurrentThread;
            ULONG64 CurrentSp;
            PVOID StackLimit;

            /*
             * ARM64: Handle alignment faults (DFSC = 0x21) as exceptions.
             *
             * DFSC values 0x21, 0x22, 0x23 are alignment faults, not page faults.
             * These cannot be resolved by the page fault handler and should be
             * dispatched as STATUS_DATATYPE_MISALIGNMENT exceptions.
             *
             * This is critical for Clang ARM64 which generates 128-bit SIMD stores
             * (str q0) that require 16-byte alignment. When the target address is
             * not 16-byte aligned, the CPU generates an alignment fault.
             *
             * Example: RtlZeroMemory on TEB offset 0x7D8 (8-byte aligned, not 16)
             * causes str q0 instruction to fault with DFSC=0x21.
             */
            if ((FaultStatus & 0x3C) == 0x20)  /* DFSC 0x20-0x23 are alignment related */
            {
                EXCEPTION_RECORD ExceptionRecord;
                KPROCESSOR_MODE Mode;
                BOOLEAN IsWrite = (Iss & (1u << 6)) != 0;

                TrapFrame = &Context->TrapFrame;
                KiArm64InitializeTrapFrame(Context, TrapFrame);

                Mode = KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);

                RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
                ExceptionRecord.ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
                ExceptionRecord.ExceptionFlags = 0;
                ExceptionRecord.ExceptionRecord = NULL;
                ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
                ExceptionRecord.NumberParameters = 2;
                ExceptionRecord.ExceptionInformation[0] = (ULONG_PTR)IsWrite;
                ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

                if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                {
                    KiArm64ReportUnhandledSyncException(Context, Esr);
                }

                KiDispatchException(&ExceptionRecord,
                                    Context->ExceptionFramePointer,
                                    TrapFrame,
                                    Mode,
                                    TRUE);

                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }


            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);
            PreviousMode = KiArm64PreviousModeFromContext(Context->State.Spsr,
                                                          Context->State.Elr);

            /*
             * Check the interrupted kernel SP, not this C handler's SP. The
             * vector/common handler and this function allocate a large frame
             * before reaching this point, so using the current SP can falsely
             * turn an ordinary kernel data abort into KERNEL_STACK_INPAGE_ERROR.
             */
            CurrentSp = Context->State.Registers.Sp;
            CurrentThread = PsGetCurrentThread();
            if (CurrentThread == NULL)
            {
                if (KiArm64PanicStack != NULL)
                {
                    KiArm64BugCheckOnPanicStack(KiArm64PanicStack,
                                                KERNEL_SECURITY_CHECK_FAILURE,
                                                CurrentSp,
                                                0,
                                                Context->State.FaultAddress,
                                                0xBAD7A11);
                }

                for (;;)
                {
                    __asm__ __volatile__("wfe");
                }
            }

            StackLimit = (PVOID)CurrentThread->Tcb.StackLimit;

            if ((PreviousMode == KernelMode) &&
                (CurrentSp < (ULONG64)StackLimit + 0x800))
            {
                if (KiArm64PanicStack != NULL)
                {
                    KiArm64BugCheckOnPanicStack(KiArm64PanicStack,
                                                KERNEL_STACK_INPAGE_ERROR,
                                                CurrentSp,
                                                (ULONG_PTR)StackLimit,
                                                Context->State.FaultAddress,
                                                0xDEAD5743);
                }

                for (;;)
                {
                    __asm__ __volatile__("wfe");
                }
            }


            WriteAccess = (Iss & (1u << 6)) != 0;

            /* ARM64: skip accessed-bit fast path to avoid dereferencing
             * an unmapped PTE alias during bring-up. Fallback to the
             * general fault handler below. */

            {
#if DBG && defined(ARM64_TRAP_TRACE)
                ULONG ProcessorIndex = KeGetCurrentProcessorNumber();
                LONG GuardSnapshot = -1;
#endif

                /* Keep logging minimal in trap path to avoid reentry */
#if DBG && defined(ARM64_TRAP_TRACE)
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                           "[arm64] DA: esr=0x%lx far=%p elr=%p cpu=%lu guard=%ld\n",
                           Esr,
                           (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                           (PVOID)(ULONG_PTR)Context->State.Elr,
                           ProcessorIndex,
                           GuardSnapshot);
#endif

                {
                    ULONG FaultCodeArg = KiArm64BuildFaultCode(FaultStatus, WriteAccess, FALSE, PreviousMode);
                    PVOID AddressArg = (PVOID)(ULONG_PTR)Context->State.FaultAddress;

                    /*
                     * ARM64: Access Flag faults (DFSC 0x9/0xA/0xB) must be fixed up by
                     * setting AF in the faulting descriptor. Some early boot mappings
                     * can have AF clear, causing an infinite abort loop even though the
                     * translation is otherwise valid.
                     */
                    if ((ULONG_PTR)AddressArg < (ULONG_PTR)MmSystemRangeStart &&
                        (FaultStatus == 0x9 || FaultStatus == 0xA || FaultStatus == 0xB))
                    {
                        if (KiArm64FixupUserAccessFlagFault(AddressArg, FaultStatus))
                        {
                            Context->TrapFramePointer = TrapFrame;
                            Context->ExceptionFramePointer = &Context->ExceptionFrame;
                            KiArm64ClearTrapActive();
                            return TRUE;
                        }
                    }

                    /*
                     * ARM64: Clear trap-active flag BEFORE calling MmAccessFault.
                     *
                     * MmAccessFault can legitimately cause nested page faults. For example:
                     * - User page fault on DLL section
                     * - MmNotPresentFaultSectionView calls MmMakeSegmentResident
                     * - MmMakeSegmentResident triggers file I/O
                     * - File I/O accesses System Cache (VACB)
                     * - System Cache page fault on a different address
                     *
                     * If we don't clear KiArm64TrapActive, the second fault will see
                     * the flag set and incorrectly treat it as a recursive exception,
                     * causing a spurious bugcheck.
                     *
                     * This mirrors the SVC handling (line ~734) which clears the flag
                     * before calling KiSystemService.
                     */
                    KiArm64ClearTrapActive();

                    if ((MmSessionSpace != NULL) &&
                        ((ULONG_PTR)AddressArg >= (ULONG_PTR)MmSessionSpace) &&
                        ((ULONG_PTR)AddressArg < ((ULONG_PTR)MmSessionSpace + PAGE_SIZE)) &&
                        (InterlockedDecrement(&KiArm64SessionFaultLogBudget) >= 0))
                    {
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64][SESSION-DA] pre va=%p elr=%p esr=0x%lx dfsc=0x%lx write=%d "
                                   "tf=%p ef=%p sp=%p fp=%p lr=%p x8=%p x10=%p x11=%p prev=%d stage=%p\n",
                                   AddressArg,
                                   (PVOID)(ULONG_PTR)Context->State.Elr,
                                   Esr,
                                   (ULONG)FaultStatus,
                                   (int)WriteAccess,
                                   TrapFrame,
                                   &Context->ExceptionFrame,
                                   (PVOID)TrapFrame->Sp,
                                   (PVOID)TrapFrame->Fp,
                                   (PVOID)TrapFrame->Lr,
                                   (PVOID)TrapFrame->X8,
                                   (PVOID)TrapFrame->X10,
                                   (PVOID)TrapFrame->X11,
                                   (int)PreviousMode,
                                   (PVOID)MiArm64SessionWsStage);
                    }

                    KiArm64LastDataAbortIrql = KeGetCurrentIrql();
                    if ((KiArm64LastDataAbortIrql >= DISPATCH_LEVEL) && (InterlockedDecrement(&KiArm64HighIrqlFaultLogBudget) >= 0))
                    {
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "[arm64][HIGH-IRQL-FAULT] irql=%u va=%p pc=%p lr=%p sp=%p code=0x%lx mode=%d\n", (ULONG)KiArm64LastDataAbortIrql, AddressArg, (PVOID)(ULONG_PTR)Context->State.Elr, (PVOID)TrapFrame->Lr, (PVOID)TrapFrame->Sp, FaultCodeArg, (int)PreviousMode);
                    }

                    Status = MmAccessFault(FaultCodeArg, AddressArg, PreviousMode, TrapFrame);

                    /* MM refuses not-present faults above APC_LEVEL; that is a fatal IRQL contract violation */
                    if (Status == (NTSTATUS)(STATUS_IN_PAGE_ERROR | 0x10000000)) KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL, (ULONG_PTR)AddressArg, KeGetCurrentIrql(), WriteAccess ? 1 : 0, (ULONG_PTR)Context->State.Elr);

                    if ((MmSessionSpace != NULL) &&
                        ((ULONG_PTR)AddressArg >= (ULONG_PTR)MmSessionSpace) &&
                        ((ULONG_PTR)AddressArg < ((ULONG_PTR)MmSessionSpace + PAGE_SIZE)) &&
                        (KiArm64SessionFaultLogBudget >= 0))
                    {
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64][SESSION-DA] post status=0x%lx tfpc=%p tffp=%p tflr=%p tfsp=%p "
                                   "x19=%p x20=%p x21=%p x29=%p x30=%p stage=%p savedfp=%p savedlr=%p\n",
                                   Status,
                                   (PVOID)TrapFrame->Pc,
                                   (PVOID)TrapFrame->Fp,
                                   (PVOID)TrapFrame->Lr,
                                   (PVOID)TrapFrame->Sp,
                                   (PVOID)Context->ExceptionFrame.X19,
                                   (PVOID)Context->ExceptionFrame.X20,
                                   (PVOID)Context->ExceptionFrame.X21,
                                   (PVOID)TrapFrame->Fp,
                                   (PVOID)TrapFrame->Lr,
                                   (PVOID)MiArm64SessionWsStage,
                                   (PVOID)MiArm64SessionWsSavedFp,
                                   (PVOID)MiArm64SessionWsSavedLr);
                    }
                }

#if DBG && defined(ARM64_TRAP_TRACE)
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                           "[arm64] DA exit: status=0x%lx cpu=%lu\n",
                           Status,
                           ProcessorIndex);
#endif
            }

            if (NT_SUCCESS(Status))
            {
                /*
                 * Do not do generic D-cache invalidation here. A resolved data
                 * abort can be a write/access-flag fault on a normal cached page
                 * that already contains dirty CPU data. DC IVAC would discard those
                 * dirty lines and corrupt unrelated data in the same page. Cache
                 * maintenance for DMA/PIO buffers belongs at the device mapping
                 * boundary, not in the generic page-fault return path.
                 */
                {
                    ULONG_PTR Va = (ULONG_PTR)Context->State.FaultAddress;

                    if (Va < (ULONG_PTR)MmSystemRangeStart)
                    {
                        static const ULONG Shifts[4] = {39, 30, 21, 12};
                        ULONG64 Root;
                        ULONG64 Entry;
                        volatile ULONG64 *Table;
                        volatile ULONG64 *Slot;
                        ULONG Level;

                        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Root));
                        Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Root & ARM64_PTE_ADDR_MASK);
                        for (Level = 0; Level < 4; Level++)
                        {
                            Slot = &Table[(Va >> Shifts[Level]) & 0x1FF];
                            Entry = *Slot;
                            MiArm64CleanEntryToPoC(Slot);
                            if ((Level == 3) || ((Entry & 3) != 3))
                            {
                                break;
                            }
                            Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Entry & ARM64_PTE_ADDR_MASK);
                        }
                    }
                    else
                    {
                        volatile ULONG64 *Entry;

                        Entry = (volatile ULONG64 *)MiArm64KernelPpeKseg0((PVOID)Va);
                        if (Entry != NULL)
                        {
                            MiArm64CleanEntryToPoC(Entry);
                        }

                        Entry = (volatile ULONG64 *)MiArm64KernelPdeKseg0((PVOID)Va);
                        if (Entry != NULL)
                        {
                            MiArm64CleanEntryToPoC(Entry);
                        }

                        Entry = (volatile ULONG64 *)MiArm64KernelPteKseg0((PVOID)Va);
                        if (Entry != NULL)
                        {
                            MiArm64CleanEntryToPoC(Entry);
                        }
                    }
                }
                KeInvalidateTlbEntry((PVOID)(ULONG_PTR)Context->State.FaultAddress);
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            /* Not resolved by Mm - unhandled data abort. */
            if (PreviousMode == KernelMode)
            {
                ULONG InstWord = 0, PrevInstWord = 0;
                NTSTATUS InstReadStatus = STATUS_SUCCESS;

                _SEH2_TRY
                {
                    InstWord = *(volatile ULONG *)(ULONG_PTR)Context->State.Elr;
                    if (Context->State.Elr >= sizeof(ULONG))
                    {
                        PrevInstWord = *(volatile ULONG *)(ULONG_PTR)(Context->State.Elr - sizeof(ULONG));
                    }
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    InstReadStatus = _SEH2_GetExceptionCode();
                }
                _SEH2_END;

                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-KERNEL] FAIL VA=%p ELR=%p Status=0x%lx DFSC=0x%lx Write=%d Vec=%p Irql=%u Proc=%s\n",
                    (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                    (PVOID)(ULONG_PTR)Context->State.Elr,
                    (ULONG)Status,
                    (ULONG)FaultStatus,
                    (int)WriteAccess,
                    (PVOID)(ULONG_PTR)Context->State.VectorId,
                    (ULONG)KiArm64LastDataAbortIrql,
                    (PCSTR)((PEPROCESS)KeGetCurrentThread()->ApcState.Process)->ImageFileName);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-KERNEL] insn prev=0x%08lx cur=0x%08lx read=0x%lx sp=%p fp=%p lr=%p spsr=0x%lx\n",
                    PrevInstWord,
                    InstWord,
                    InstReadStatus,
                    (PVOID)TrapFrame->Sp,
                    (PVOID)TrapFrame->Fp,
                    (PVOID)TrapFrame->Lr,
                    TrapFrame->Spsr);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-KERNEL] X0=%p X1=%p X8=%p X10=%p X11=%p X16=%p X17=%p X18=%p\n",
                    (PVOID)TrapFrame->X0,
                    (PVOID)TrapFrame->X1,
                    (PVOID)TrapFrame->X8,
                    (PVOID)TrapFrame->X10,
                    (PVOID)TrapFrame->X11,
                    (PVOID)TrapFrame->X16,
                    (PVOID)TrapFrame->X17,
                    (PVOID)TrapFrame->X18);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-KERNEL] X19=%p X20=%p X21=%p X22=%p X23=%p X24=%p X25=%p X26=%p X27=%p X28=%p\n",
                    (PVOID)Context->ExceptionFrame.X19,
                    (PVOID)Context->ExceptionFrame.X20,
                    (PVOID)Context->ExceptionFrame.X21,
                    (PVOID)Context->ExceptionFrame.X22,
                    (PVOID)Context->ExceptionFrame.X23,
                    (PVOID)Context->ExceptionFrame.X24,
                    (PVOID)Context->ExceptionFrame.X25,
                    (PVOID)Context->ExceptionFrame.X26,
                    (PVOID)Context->ExceptionFrame.X27,
                    (PVOID)Context->ExceptionFrame.X28);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-KERNEL] SessWs stage=%p fp=%p lr=%p sp=%p savedfp=%p savedlr=%p\n",
                    (PVOID)MiArm64SessionWsStage,
                    (PVOID)MiArm64SessionWsFp,
                    (PVOID)MiArm64SessionWsLr,
                    (PVOID)MiArm64SessionWsSp,
                    (PVOID)MiArm64SessionWsSavedFp,
                    (PVOID)MiArm64SessionWsSavedLr);
            }

            if (PreviousMode == UserMode)
            {
                PKTHREAD CurrentThread = KeGetCurrentThread();
                PETHREAD EThread = (PETHREAD)CurrentThread;
                PEPROCESS CurrentProcess = (PEPROCESS)CurrentThread->ApcState.Process;
                ULONG64 TpidrEl0 = 0;

                __asm__ __volatile__("mrs %0, tpidr_el0" : "=r"(TpidrEl0));

                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-USER] FAIL VA=%p ELR=%p Status=0x%lx DFSC=0x%lx Write=%d Proc=%s TID=%p\n",
                    (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                    (PVOID)(ULONG_PTR)Context->State.Elr,
                    (ULONG)Status, (ULONG)FaultStatus,
                    (int)WriteAccess,
                    (PCSTR)CurrentProcess->ImageFileName,
                    (PVOID)EThread->Cid.UniqueThread);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-USER] LR=%p SP=%p FP=%p X0=%p X1=%p\n",
                    (PVOID)TrapFrame->Lr, (PVOID)TrapFrame->Sp,
                    (PVOID)TrapFrame->Fp, (PVOID)TrapFrame->X0,
                    (PVOID)TrapFrame->X1);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-USER] SPSR=0x%I64x TPIDR_EL0=0x%I64x X18=%p\n",
                    (unsigned long long)TrapFrame->Spsr,
                    (unsigned long long)TpidrEl0,
                    (PVOID)TrapFrame->X[18]);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-USER] X2=%p X3=%p X4=%p X5=%p X6=%p X7=%p\n",
                    (PVOID)TrapFrame->X[2], (PVOID)TrapFrame->X[3],
                    (PVOID)TrapFrame->X[4], (PVOID)TrapFrame->X[5],
                    (PVOID)TrapFrame->X[6], (PVOID)TrapFrame->X[7]);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-USER] X8=%p X9=%p X10=%p X11=%p X12=%p X13=%p\n",
                    (PVOID)TrapFrame->X[8], (PVOID)TrapFrame->X[9],
                    (PVOID)TrapFrame->X[10], (PVOID)TrapFrame->X[11],
                    (PVOID)TrapFrame->X[12], (PVOID)TrapFrame->X[13]);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-USER] X14=%p X15=%p X16=%p X17=%p X19=%p X20=%p\n",
                    (PVOID)TrapFrame->X[14], (PVOID)TrapFrame->X[15],
                    (PVOID)TrapFrame->X[16], (PVOID)TrapFrame->X[17],
                    (PVOID)Context->ExceptionFrame.X19,
                    (PVOID)Context->ExceptionFrame.X20);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-USER] X21=%p X22=%p X23=%p X24=%p X25=%p X26=%p X27=%p X28=%p\n",
                    (PVOID)Context->ExceptionFrame.X21,
                    (PVOID)Context->ExceptionFrame.X22,
                    (PVOID)Context->ExceptionFrame.X23,
                    (PVOID)Context->ExceptionFrame.X24,
                    (PVOID)Context->ExceptionFrame.X25,
                    (PVOID)Context->ExceptionFrame.X26,
                    (PVOID)Context->ExceptionFrame.X27,
                    (PVOID)Context->ExceptionFrame.X28);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[DABORT-USER] StartAddr=%p Win32StartAddr=%p\n",
                    (PVOID)EThread->StartAddress,
                    (PVOID)EThread->Win32StartAddress);

                /*
                 * One-shot diagnostic: walk TTBR0 page table to dump PTEs for
                 * the faulting PC page and the faulting data page. This tells us
                 * which physical pages are backing the code and data at crash time.
                 * Also dump 8 bytes of page content via KSEG0 to verify data correctness.
                 */
                {
                    static volatile LONG DabortDiagCount = 0;
                    if (InterlockedIncrement(&DabortDiagCount) <= 3)
                    {
                        ULONG64 Ttbr0;
                        ULONG64 Ttbr1;
                        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
                        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                            "[DABORT-DIAG] TTBR0=0x%I64x TTBR1=0x%I64x\n",
                            Ttbr0, Ttbr1);

                        /*
                         * Walk user page table for three VAs:
                         *  - faulting PC
                         *  - faulting data address (FAR)
                         *  - user stack pointer at fault time (SP)
                         */
                        ULONG64 DiagVAs[3] = {
                            Context->State.Elr,
                            Context->State.FaultAddress,
                            TrapFrame->Sp
                        };
                        const char *DiagNames[3] = { "PC", "FAR", "SP" };
                        for (int dv = 0; dv < 3; dv++)
                        {
                            ULONG64 Va = DiagVAs[dv];
                            ULONG64 Root = Ttbr0 & 0x0000FFFFFFFFF000ULL;
                            volatile ULONG64 *L0 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Root);
                            ULONG L0i = (Va >> 39) & 0x1FF;
                            ULONG64 L0e = (L0i < 512) ? L0[L0i] : 0;
                            ULONG64 L1e = 0, L2e = 0, L3e = 0;
                            PFN_NUMBER DataPfn = 0;
                            if ((L0e & 0x3) == 0x3) {
                                volatile ULONG64 *L1 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L0e & 0x0000FFFFFFFFF000ULL);
                                ULONG L1i = (Va >> 30) & 0x1FF;
                                L1e = L1[L1i];
                                if ((L1e & 0x3) == 0x3) {
                                    volatile ULONG64 *L2 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L1e & 0x0000FFFFFFFFF000ULL);
                                    ULONG L2i = (Va >> 21) & 0x1FF;
                                    L2e = L2[L2i];
                                    if ((L2e & 0x3) == 0x3) {
                                        volatile ULONG64 *L3 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L2e & 0x0000FFFFFFFFF000ULL);
                                        ULONG L3i = (Va >> 12) & 0x1FF;
                                        L3e = L3[L3i];
                                        if ((L3e & 0x3) == 0x3) {
                                            DataPfn = (PFN_NUMBER)((L3e >> 12) & 0xFFFFFFFFFULL);
                                        }
                                    }
                                }
                            }
                            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                "[DABORT-DIAG] %s VA=%p L0[%u]=0x%I64x L1e=0x%I64x L2e=0x%I64x L3e=0x%I64x PFN=0x%lx\n",
                                DiagNames[dv], (PVOID)Va, L0i, L0e, L1e, L2e, L3e, (ULONG)DataPfn);
                            /* Dump 16 bytes from the page via KSEG0 at the page-offset of the VA */
                            if (DataPfn != 0)
                            {
                                ULONG PageOff = (ULONG)(Va & 0xFFF);
                                volatile ULONG64 *Kseg = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(((ULONG64)DataPfn << PAGE_SHIFT) | (PageOff & ~7ULL));
                                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                    "[DABORT-DIAG] %s KSEG0[%03x]=0x%016I64x %016I64x\n",
                                    DiagNames[dv], PageOff & ~7U, Kseg[0], Kseg[1]);
                            }
                        }

                        if ((Context->State.FaultAddress == 0) &&
                            (KiArm64ShouldTraceSystemDllAddress(CurrentProcess, Context->State.Elr)))
                        {
                            ULONG64 X1 = TrapFrame->X[1];
                            ULONG64 Fp = TrapFrame->Fp;
                            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                "[DABORT-DIAG] ntdll-null x1=%p fp=%p sp=%p lr=%p\n",
                                (PVOID)X1,
                                (PVOID)Fp,
                                (PVOID)TrapFrame->Sp,
                                (PVOID)TrapFrame->Lr);
                            KiArm64DumpUserAliasQwords("ntdll-null-x1",
                                                       X1,
                                                       (ULONG)(X1 & (PAGE_SIZE - 1)),
                                                       (ULONG)((X1 + 8) & (PAGE_SIZE - 1)),
                                                       (ULONG)((X1 + 16) & (PAGE_SIZE - 1)));
                            KiArm64DumpUserAliasQwords("ntdll-null-fp",
                                                       Fp,
                                                       (ULONG)(Fp & (PAGE_SIZE - 1)),
                                                       (ULONG)((Fp + 8) & (PAGE_SIZE - 1)),
                                                       (ULONG)((Fp + 16) & (PAGE_SIZE - 1)));
                            KiArm64DumpUserAliasQwords("ntdll-null-sp-copy",
                                                       TrapFrame->Sp,
                                                       (ULONG)((TrapFrame->Sp + 64) & (PAGE_SIZE - 1)),
                                                       (ULONG)((TrapFrame->Sp + 72) & (PAGE_SIZE - 1)),
                                                       (ULONG)((TrapFrame->Sp + 80) & (PAGE_SIZE - 1)));
                        }

                        /*
                         * kernel32 .rsrc corruption diagnostic: when FAR=0xFF2B4002,
                         * probe several pages within kernel32's .rsrc section to find
                         * which page has wrong content. Also dump the page at X0 - 0x1000
                         * (the page the string walk was on just before the fault).
                         * Expected values from on-disk PE file for comparison.
                         */
                        if (Context->State.FaultAddress == 0xFF2B4002ULL)
                        {
                            /* Probe specific VAs within kernel32 .rsrc to find corruption */
                            static const ULONG64 ProbeVAs[] = {
                                0xFF09E000ULL,  /* .rsrc start (RVA 0x9E000) */
                                0xFF262000ULL,  /* locale strings area (RVA 0x262000) */
                                0xFF2A9000ULL,  /* last full .rsrc page (RVA 0x2A9000) */
                            };
                            for (int pv = 0; pv < 3; pv++)
                            {
                                ULONG64 PVa = ProbeVAs[pv];
                                ULONG64 PRoot = Ttbr0 & 0x0000FFFFFFFFF000ULL;
                                volatile ULONG64 *PL0 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(PRoot);
                                ULONG PL0i = (PVa >> 39) & 0x1FF;
                                ULONG64 PL0e = PL0[PL0i];
                                PFN_NUMBER PPfn = 0;
                                if ((PL0e & 3) == 3) {
                                    volatile ULONG64 *PL1 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(PL0e & 0x0000FFFFFFFFF000ULL);
                                    ULONG64 PL1e = PL1[(PVa >> 30) & 0x1FF];
                                    if ((PL1e & 3) == 3) {
                                        volatile ULONG64 *PL2 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(PL1e & 0x0000FFFFFFFFF000ULL);
                                        ULONG64 PL2e = PL2[(PVa >> 21) & 0x1FF];
                                        if ((PL2e & 3) == 3) {
                                            volatile ULONG64 *PL3 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(PL2e & 0x0000FFFFFFFFF000ULL);
                                            ULONG64 PL3e = PL3[(PVa >> 12) & 0x1FF];
                                            if ((PL3e & 3) == 3)
                                                PPfn = (PFN_NUMBER)((PL3e >> 12) & 0xFFFFFFFFFULL);
                                        }
                                    }
                                }
                                if (PPfn != 0) {
                                    volatile ULONG64 *PK = (volatile ULONG64 *)MI_ARM64_PFN_TO_VA(PPfn);
                                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                        "[RSRC-DIAG] VA=%p PFN=0x%lx data[0..3]=0x%016I64x 0x%016I64x 0x%016I64x 0x%016I64x\n",
                                        (PVOID)PVa, (ULONG)PPfn, PK[0], PK[1], PK[2], PK[3]);
                                } else {
                                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                        "[RSRC-DIAG] VA=%p UNMAPPED\n", (PVOID)PVa);
                                }
                            }

                            /* Also probe 16 pages leading up to the fault to find which has bad data */
                            for (ULONG64 ScanVa = 0xFF2A0000ULL; ScanVa < 0xFF2AC000ULL; ScanVa += 0x1000ULL)
                            {
                                ULONG64 SRoot = Ttbr0 & 0x0000FFFFFFFFF000ULL;
                                volatile ULONG64 *SL0 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(SRoot);
                                ULONG64 SL0e = SL0[(ScanVa >> 39) & 0x1FF];
                                PFN_NUMBER SPfn = 0;
                                if ((SL0e & 3) == 3) {
                                    volatile ULONG64 *SL1 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(SL0e & 0x0000FFFFFFFFF000ULL);
                                    ULONG64 SL1e = SL1[(ScanVa >> 30) & 0x1FF];
                                    if ((SL1e & 3) == 3) {
                                        volatile ULONG64 *SL2 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(SL1e & 0x0000FFFFFFFFF000ULL);
                                        ULONG64 SL2e = SL2[(ScanVa >> 21) & 0x1FF];
                                        if ((SL2e & 3) == 3) {
                                            volatile ULONG64 *SL3 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(SL2e & 0x0000FFFFFFFFF000ULL);
                                            ULONG64 SL3e = SL3[(ScanVa >> 12) & 0x1FF];
                                            if ((SL3e & 3) == 3)
                                                SPfn = (PFN_NUMBER)((SL3e >> 12) & 0xFFFFFFFFFULL);
                                        }
                                    }
                                }
                                if (SPfn != 0) {
                                    volatile ULONG64 *SK = (volatile ULONG64 *)MI_ARM64_PFN_TO_VA(SPfn);
                                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                        "[RSRC-SCAN] VA=%p PFN=0x%lx d[0]=0x%016I64x d[1]=0x%016I64x\n",
                                        (PVOID)ScanVa, (ULONG)SPfn, SK[0], SK[1]);
                                } else {
                                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                        "[RSRC-SCAN] VA=%p UNMAPPED\n", (PVOID)ScanVa);
                                }
                            }
                        }
                    }
                }
            }
#ifdef KDBG
            /*
             * Call KDBG to display crash diagnostics (registers, stack trace,
             * modules) for kernel-mode faults. This runs regardless of KD state
             * since KDBG provides valuable crash info even when KD is "enabled"
             * for serial output but no interactive debugger is attached.
             */
            if (PreviousMode == KernelMode)
            {
                EXCEPTION_RECORD64 ExceptionRecord64;
                CONTEXT KdbContext;

                RtlZeroMemory(&ExceptionRecord64, sizeof(ExceptionRecord64));
                ExceptionRecord64.ExceptionCode = STATUS_ACCESS_VIOLATION;
                ExceptionRecord64.ExceptionFlags = 0;
                ExceptionRecord64.ExceptionAddress = Context->State.Elr;
                ExceptionRecord64.NumberParameters = 2;
                ExceptionRecord64.ExceptionInformation[0] = KiArm64AccessTypeToExceptionInfo(WriteAccess, FALSE);
                ExceptionRecord64.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

                RtlZeroMemory(&KdbContext, sizeof(KdbContext));
                KdbContext.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_ARM64;

                KeTrapFrameToContext(TrapFrame, Context->ExceptionFramePointer, &KdbContext);

                KdbEnterDebuggerException(&ExceptionRecord64,
                                          PreviousMode,
                                          &KdbContext,
                                          TRUE);
            }
#endif /* KDBG */

            {
                EXCEPTION_RECORD ExceptionRecord;
                RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
                ExceptionRecord.ExceptionCode = STATUS_ACCESS_VIOLATION;
                ExceptionRecord.ExceptionFlags = 0;
                ExceptionRecord.ExceptionRecord = NULL;
                ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
                ExceptionRecord.NumberParameters = 2;
                ExceptionRecord.ExceptionInformation[0] = KiArm64AccessTypeToExceptionInfo(WriteAccess, FALSE);
                ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

                KiDispatchException(&ExceptionRecord,
                                    Context->ExceptionFramePointer,
                                    TrapFrame,
                                    PreviousMode,
                                    TRUE);

                /* Return with updated trap frame (either resumed, or KD/bugcheck handled). */
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }
        }

        case 0x22: /* PC alignment fault */
        case 0x26: /* SP alignment fault */
        {
            EXCEPTION_RECORD ExceptionRecord;
            KPROCESSOR_MODE Mode;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            Mode = KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 0;

            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                KiArm64ReportUnhandledSyncException(Context, Esr);

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                Mode,
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        case 0x2F: /* SError */
        {
            /*
             * SError (System Error) - Asynchronous external abort.
             * Call dedicated handler which implements our SError policy.
             * KiSErrorHandler will BUGCHECK for kernel faults. For user faults
             * it requests process termination and returns TRUE to continue.
             */
            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            if (KiSErrorHandler(TrapFrame))
            {
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            /* Fallback: user-mode termination failed unexpectedly */
            KeBugCheckEx(UNEXPECTED_KERNEL_MODE_TRAP, 0x2F, 0, 0, 0);
        }

        case 0x3C: /* BRK instruction */
        {
            KPROCESSOR_MODE Mode = KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);
            ULONG BrkImm = Esr & 0xFFFF;

            /*
             * BRK #0xF003: Debug service request from user-mode or kernel-mode.
             * The caller places the service type in X0 and arguments in X1-X4.
             * Handle this directly without going through exception dispatch.
             */
            if (BrkImm == 0xF003)
            {
                ULONG DebugService = (ULONG)Context->State.Registers.X[0];
                ULONG64 ServiceElr;

                TrapFrame = &Context->TrapFrame;
                KiArm64InitializeTrapFrame(Context, TrapFrame);

                switch (DebugService)
                {
                    case BREAKPOINT_PRINT:
                    {
                        BOOLEAN Handled = FALSE;
                        NTSTATUS PrintStatus;

                        PrintStatus = KdpPrint(
                            (ULONG)Context->State.Registers.X[3],   /* ComponentId */
                            (ULONG)Context->State.Registers.X[4],   /* Level */
                            (PCHAR)Context->State.Registers.X[1],   /* String */
                            (USHORT)Context->State.Registers.X[2],  /* Length */
                            Mode,
                            TrapFrame,
                            &Context->ExceptionFrame,
                            &Handled);

                        Context->State.Registers.X[0] = (UINT64)PrintStatus;
                        TrapFrame->X[0] = (UINT64)PrintStatus;
                        break;
                    }

                    case BREAKPOINT_PROMPT:
                    {
                        USHORT ReturnLength;

                        ReturnLength = KdpPrompt(
                            (PCHAR)Context->State.Registers.X[1],
                            (USHORT)Context->State.Registers.X[2],
                            (PCHAR)Context->State.Registers.X[3],
                            (USHORT)Context->State.Registers.X[4],
                            Mode,
                            TrapFrame,
                            &Context->ExceptionFrame);

                        Context->State.Registers.X[0] = (UINT64)ReturnLength;
                        TrapFrame->X[0] = (UINT64)ReturnLength;
                        break;
                    }

                    case BREAKPOINT_LOAD_SYMBOLS:
                    case BREAKPOINT_UNLOAD_SYMBOLS:
                    {
                        CONTEXT LocalContext;

                        RtlZeroMemory(&LocalContext, sizeof(LocalContext));
                        LocalContext.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_ARM64;
                        KeTrapFrameToContext(TrapFrame, &Context->ExceptionFrame, &LocalContext);
                        KdpSymbol((PSTRING)Context->State.Registers.X[1],
                                  (PKD_SYMBOLS_INFO)Context->State.Registers.X[2],
                                  (DebugService == BREAKPOINT_UNLOAD_SYMBOLS),
                                  Mode,
                                  &LocalContext,
                                  TrapFrame,
                                  &Context->ExceptionFrame);
                        Context->State.Registers.X[0] = (UINT64)STATUS_SUCCESS;
                        TrapFrame->X[0] = (UINT64)STATUS_SUCCESS;
                        break;
                    }

                    case BREAKPOINT_COMMAND_STRING:
                    {
                        CONTEXT LocalContext;

                        RtlZeroMemory(&LocalContext, sizeof(LocalContext));
                        LocalContext.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_ARM64;
                        KeTrapFrameToContext(TrapFrame, &Context->ExceptionFrame, &LocalContext);
                        KdpCommandString((PSTRING)Context->State.Registers.X[1],
                                         (PSTRING)Context->State.Registers.X[2],
                                         Mode,
                                         &LocalContext,
                                         TrapFrame,
                                         &Context->ExceptionFrame);
                        Context->State.Registers.X[0] = (UINT64)STATUS_SUCCESS;
                        TrapFrame->X[0] = (UINT64)STATUS_SUCCESS;
                        break;
                    }

                    default:
                        Context->State.Registers.X[0] = (UINT64)STATUS_NOT_IMPLEMENTED;
                        TrapFrame->X[0] = (UINT64)STATUS_NOT_IMPLEMENTED;
                        break;
                }

                /* Advance PC past the BRK instruction */
                ServiceElr = Context->State.Elr;
                TrapFrame->Pc = (ULONG64)((ULONG_PTR)ServiceElr + 4);
                Context->State.Elr = ServiceElr + 4;

                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            {
                EXCEPTION_RECORD ExceptionRecord;

                RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
                ExceptionRecord.ExceptionCode = STATUS_BREAKPOINT;
                ExceptionRecord.ExceptionFlags = 0;
                ExceptionRecord.ExceptionRecord = NULL;
                ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
                ExceptionRecord.NumberParameters = 3;
                ExceptionRecord.ExceptionInformation[0] = BREAKPOINT_BREAK;
                ExceptionRecord.ExceptionInformation[1] = 0;
                ExceptionRecord.ExceptionInformation[2] = 0;

                KiDispatchException(&ExceptionRecord,
                                    Context->ExceptionFramePointer,
                                    TrapFrame,
                                    Mode,
                                    TRUE);
            }

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        default:
        {
            EXCEPTION_RECORD ExceptionRecord;
            KPROCESSOR_MODE Mode;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            Mode = KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);

            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] UNHANDLED ESR class=0x%lx ISS=0x%lx Vec=%p ELR=%p FAR=%p SPSR=0x%lx Mode=%d Proc=%s\n",
                    (ULONG)EsrClass, (ULONG)Iss,
                    (PVOID)(ULONG_PTR)Context->State.VectorId,
                    (PVOID)(ULONG_PTR)Context->State.Elr,
                    (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                    (ULONG)Context->State.Spsr,
                    (int)Mode,
                    (PCSTR)((PEPROCESS)KeGetCurrentThread()->ApcState.Process)->ImageFileName);
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] UNHANDLED regs sp=%p fp=%p lr=%p x19=%p x20=%p x21=%p x22=%p x28=%p "
                    "sessstage=%p sessfp=%p sesslr=%p sesssp=%p savedfp=%p savedlr=%p\n",
                    (PVOID)TrapFrame->Sp,
                    (PVOID)TrapFrame->Fp,
                    (PVOID)TrapFrame->Lr,
                    (PVOID)Context->ExceptionFrame.X19,
                    (PVOID)Context->ExceptionFrame.X20,
                    (PVOID)Context->ExceptionFrame.X21,
                    (PVOID)Context->ExceptionFrame.X22,
                    (PVOID)Context->ExceptionFrame.X28,
                    (PVOID)MiArm64SessionWsStage,
                    (PVOID)MiArm64SessionWsFp,
                    (PVOID)MiArm64SessionWsLr,
                    (PVOID)MiArm64SessionWsSp,
                    (PVOID)MiArm64SessionWsSavedFp,
                    (PVOID)MiArm64SessionWsSavedLr);

            if (Context->State.Elr >= (ULONG_PTR)MmSystemRangeStart)
            {
                ULONG InstWord = 0;
                NTSTATUS ReadStatus = STATUS_SUCCESS;

                _SEH2_TRY
                {
                    InstWord = *(volatile ULONG *)(ULONG_PTR)Context->State.Elr;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    ReadStatus = _SEH2_GetExceptionCode();
                }
                _SEH2_END;

                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                        "[arm64] UNHANDLED kernel-insn ELR=%p word=0x%08lx status=0x%lx\n",
                        (PVOID)(ULONG_PTR)Context->State.Elr,
                        InstWord,
                        ReadStatus);
            }

            /* Try to read the instruction word at the faulting address */
            if (Context->State.Elr != 0 && Context->State.Elr < (ULONG_PTR)MmSystemRangeStart)
            {
                ULONG InstWord = 0;
                NTSTATUS ProbeStatus = STATUS_SUCCESS;
                _SEH2_TRY {
                    ProbeForRead((PVOID)(ULONG_PTR)Context->State.Elr, 4, 1);
                    InstWord = *(volatile ULONG *)(ULONG_PTR)Context->State.Elr;
                } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                    ProbeStatus = _SEH2_GetExceptionCode();
                } _SEH2_END;
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                        "[arm64] UNHANDLED: Instruction at ELR=%p: 0x%08lx (ProbeStatus=0x%lx)\n",
                        (PVOID)(ULONG_PTR)Context->State.Elr, InstWord, ProbeStatus);

                /* Walk TTBR0 page tables via KSEG0 to diagnose wrong-page vs bad-data */
                {
                    ULONG64 Ttbr0, Pa, L3Entry = 0;
                    volatile ULONG64 *Table;
                    ULONG64 Entry;
                    ULONG64 Elr = Context->State.Elr;

                    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
                    Pa = Ttbr0 & 0x0000FFFFFFFFF000ULL;

                    /* Walk L0 → L1 → L2 → L3 */
                    if (Pa != 0)
                    {
                        Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Pa);
                        Entry = Table[(Elr >> 39) & 0x1FF];
                        if ((Entry & 0x3ULL) == 0x3ULL) /* L0 valid */
                        {
                            Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Entry & 0x0000FFFFFFFFF000ULL);
                            Entry = Table[(Elr >> 30) & 0x1FF];
                            if ((Entry & 0x3ULL) == 0x3ULL) /* L1 valid */
                            {
                                Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Entry & 0x0000FFFFFFFFF000ULL);
                                Entry = Table[(Elr >> 21) & 0x1FF];
                                if ((Entry & 0x3ULL) == 0x3ULL) /* L2 valid → L3 table */
                                {
                                    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Entry & 0x0000FFFFFFFFF000ULL);
                                    L3Entry = Table[(Elr >> 12) & 0x1FF];
                                }
                            }
                        }
                    }

                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                            "[arm64] UNHANDLED: TTBR0=0x%016llx L3PTE=0x%016llx\n",
                            Ttbr0, L3Entry);

                    if ((L3Entry & 0x3ULL) == 0x3ULL)
                    {
                        /* L3 entry is a valid page descriptor — read through KSEG0 */
                        ULONG64 PhysPage = L3Entry & 0x0000FFFFFFFFF000ULL;
                        ULONG PageOffset = (ULONG)(Elr & 0xFFF);
                        volatile ULONG *Kseg0Ptr = (volatile ULONG *)MI_ARM64_PHYS_TO_VA(PhysPage | PageOffset);
                        ULONG Kseg0Word = *Kseg0Ptr;

                        /* Also read 8 words around the fault point through KSEG0 */
                        ULONG PageBase = PageOffset & ~0x1F; /* 32-byte aligned */
                        volatile ULONG *Base = (volatile ULONG *)MI_ARM64_PHYS_TO_VA(PhysPage | PageBase);

                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                "[arm64] UNHANDLED: PhysAddr=0x%llx Kseg0Word=0x%08lx UserWord=0x%08lx %s\n",
                                PhysPage | PageOffset, Kseg0Word, InstWord,
                                (Kseg0Word == InstWord) ? "MATCH" : "MISMATCH");
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                "[arm64] UNHANDLED: KSEG0 dump @+0x%x: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
                                PageBase,
                                Base[0], Base[1], Base[2], Base[3],
                                Base[4], Base[5], Base[6], Base[7]);
                    }
                    else
                    {
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                "[arm64] UNHANDLED: L3PTE invalid (0x%016llx) — page not mapped!\n", L3Entry);
                    }
                }
            }

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 0;

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                Mode,
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }
    }

    KiArm64ClearTrapActive();
    return FALSE;
}

/*
 * KiSErrorHandler - Handle ARM64 SError (System Error) exceptions
 *
 * SError is ARM64's mechanism for asynchronous external aborts, including:
 *   - External memory parity/ECC errors
 *   - Cache errors
 *   - Bus/interconnect errors
 *   - RAS (Reliability, Availability, Serviceability) events
 *
 * SError is asynchronous - it may be raised after the instruction that caused
 * it has retired. The FAR_EL1 value may not be valid.
 *
 * POLICY:
 *   - Normal ARM64 bring-up keeps SError masked during ordinary IRQL changes
 *   - If SError is nevertheless delivered, kernel-mode SError -> BUGCHECK
 *   - User-mode SError -> Terminate process
 */
BOOLEAN
NTAPI
KiSErrorHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG64 Esr, Far, Elr;
    ULONG ExceptionClass, Iss;
    KPROCESSOR_MODE PreviousMode;

    /* Read exception syndrome and fault address */
    __asm__ __volatile__("mrs %0, esr_el1" : "=r"(Esr));
    __asm__ __volatile__("mrs %0, far_el1" : "=r"(Far));

    /* Note: KTRAP_FRAME uses Pc (Program Counter), not Elr */
    Elr = TrapFrame->Pc;
    ExceptionClass = (ULONG)((Esr >> 26) & 0x3F);
    Iss = (ULONG)(Esr & 0x1FFFFFF);

    /* Determine if exception was from user mode or kernel mode */
    PreviousMode = KiArm64PreviousModeFromContext(TrapFrame->Spsr, TrapFrame->Pc);

    DPRINT1("\n");
    DPRINT1("========================================\n");
    DPRINT1("SError (Asynchronous External Abort)\n");
    DPRINT1("========================================\n");
    DPRINT1("ESR_EL1:    0x%016llX\n", Esr);
    DPRINT1("FAR_EL1:    0x%016llX (may be invalid for async abort)\n", Far);
    DPRINT1("ELR_EL1:    0x%016llX\n", Elr);
    DPRINT1("SPSR_EL1:   0x%016llX\n", TrapFrame->Spsr);
    DPRINT1("Mode:       %s\n", PreviousMode == KernelMode ? "Kernel" : "User");
    DPRINT1("EC:         0x%02lX (Exception Class)\n", ExceptionClass);
    DPRINT1("ISS:        0x%07lX\n", Iss);

    if (Far >= (ULONG64)(ULONG_PTR)MmSystemRangeStart)
    {
        PMMPTE SelfMapPte = MiAddressToPte((PVOID)(ULONG_PTR)Far);
        PMMPTE Kseg0Pte = MiArm64KernelPteKseg0((PVOID)(ULONG_PTR)Far);
        ULONG64 SelfMapValue = SelfMapPte ? SelfMapPte->u.Long : 0;
        ULONG64 Kseg0Value = Kseg0Pte ? Kseg0Pte->u.Long : 0;

        DPRINT1("[arm64][SErrorPTE] addr=%p selfpte=%p self=0x%016llX "
                "ksegpte=%p kseg=0x%016llX inPagedPool=%lu\n",
                (PVOID)(ULONG_PTR)Far,
                SelfMapPte,
                SelfMapValue,
                Kseg0Pte,
                Kseg0Value,
                ((Far >= (ULONG64)(ULONG_PTR)MmPagedPoolStart) &&
                 (Far < (ULONG64)(ULONG_PTR)MmPagedPoolEnd)) ? 1UL : 0UL);

        if ((Far >= (ULONG64)(ULONG_PTR)MmPagedPoolStart) &&
            (Far < (ULONG64)(ULONG_PTR)MmPagedPoolEnd))
        {
            KiArm64DumpPagedPoolPageByPfnAlias(Far,
                                              Kseg0Value ? Kseg0Value : SelfMapValue);
        }
    }

    if (PreviousMode != KernelMode)
    {
        PETHREAD Thread = PsGetCurrentThread();
        ULONG64 TpidrEl0 = 0;

        __asm__ __volatile__("mrs %0, tpidr_el0" : "=r"(TpidrEl0));

        DPRINT1("[arm64][SErrorUser] x0=%p x1=%p x2=%p x3=%p x16=%p x17=%p "
                "x18=%p tpidr_el0=%p\n",
                (PVOID)(ULONG_PTR)TrapFrame->X0,
                (PVOID)(ULONG_PTR)TrapFrame->X1,
                (PVOID)(ULONG_PTR)TrapFrame->X2,
                (PVOID)(ULONG_PTR)TrapFrame->X3,
                (PVOID)(ULONG_PTR)TrapFrame->X16,
                (PVOID)(ULONG_PTR)TrapFrame->X17,
                (PVOID)(ULONG_PTR)TrapFrame->X18,
                (PVOID)(ULONG_PTR)TpidrEl0);

        if (Far < (ULONG64)(ULONG_PTR)MmSystemRangeStart)
        {
            KiArm64DumpUserWalk("far", Far);
            KiArm64DumpUserAliasQwords("far-base",
                                       Far,
                                       BYTE_OFFSET(Far),
                                       (BYTE_OFFSET(Far) + 0x008) & (PAGE_SIZE - 1),
                                       (BYTE_OFFSET(Far) + 0x0F8) & (PAGE_SIZE - 1));
        }
        KiArm64DumpUserWalk("elr", Elr);
        KiArm64DumpUserWalk("sp", TrapFrame->Sp);
        KiArm64DumpUserWalk("x3", TrapFrame->X3);
        KiArm64DumpUserWalk("x18", TrapFrame->X18);
        KiArm64DumpUserAliasQwords("elr-code",
                                   Elr,
                                   BYTE_OFFSET(ALIGN_DOWN_BY(Elr, sizeof(ULONG64))),
                                   BYTE_OFFSET(ALIGN_DOWN_BY(Elr, sizeof(ULONG64)) + sizeof(ULONG64)),
                                   BYTE_OFFSET(ALIGN_DOWN_BY(Elr, sizeof(ULONG64)) + (2 * sizeof(ULONG64))));
        KiArm64DumpUserAliasQwords("sp-frame",
                                   TrapFrame->Sp,
                                   BYTE_OFFSET(ALIGN_DOWN_BY(TrapFrame->Sp, sizeof(ULONG64))),
                                   (BYTE_OFFSET(TrapFrame->Sp) +
                                    FIELD_OFFSET(UAPC_FRAME, MachineFrame.Sp)) & (PAGE_SIZE - 1),
                                   (BYTE_OFFSET(TrapFrame->Sp) +
                                    FIELD_OFFSET(UAPC_FRAME, MachineFrame.Pc)) & (PAGE_SIZE - 1));
        if (Thread != NULL && Thread->Tcb.Teb != NULL)
        {
            KiArm64DumpUserWalk("teb", (ULONG64)(ULONG_PTR)Thread->Tcb.Teb);
            KiArm64DumpUserAliasQwords("teb-nttib",
                                       (ULONG64)(ULONG_PTR)Thread->Tcb.Teb,
                                       0x008,
                                       0x010,
                                       0x060);
            KiArm64DumpUserAliasQwords("teb+1-dealloc",
                                       (ULONG64)(ULONG_PTR)Thread->Tcb.Teb + PAGE_SIZE,
                                       0x470,
                                       FIELD_OFFSET(TEB, DeallocationStack) & (PAGE_SIZE - 1),
                                       0x480);
        }
    }

    /* Decode ISS bits for SError (EC=0x2F) */
    if (ExceptionClass == 0x2F)
    {
        ULONG Ids = (Iss >> 24) & 1;   /* Implementation Defined Syndrome */
        ULONG Iesb = (Iss >> 13) & 1;  /* Implicit Error Synchronization Barrier */
        ULONG Aet = (Iss >> 10) & 7;   /* Asynchronous Error Type */
        ULONG Ea = (Iss >> 9) & 1;     /* External Abort type */
        ULONG Dfsc = Iss & 0x3F;       /* Data Fault Status Code */

        DPRINT1("  IDS:      %lu (Implementation Defined Syndrome)\n", Ids);
        DPRINT1("  IESB:     %lu (Implicit ESB)\n", Iesb);
        DPRINT1("  AET:      0x%lX (Async Error Type)\n", Aet);
        DPRINT1("  EA:       %lu (External Abort)\n", Ea);
        DPRINT1("  DFSC:     0x%02lX (Data Fault Status Code)\n", Dfsc);
    }
    DPRINT1("========================================\n");

    /*
     * POLICY:
     *   - Kernel-mode SError: BUGCHECK (system is in undefined state)
     *   - User-mode SError: Terminate current process (isolate error)
     */
    if (PreviousMode == KernelMode)
    {
        /* Kernel-mode SError is fatal - hardware error in kernel operation */
        KeBugCheckEx(UNEXPECTED_KERNEL_MODE_TRAP,
                     0x2F,              /* SError exception class */
                     Esr,               /* Full ESR_EL1 value */
                     Far,               /* FAR_EL1 (may be invalid) */
                     Elr);              /* ELR_EL1 (PC at exception) */
    }
    else
    {
        NTSTATUS Status;

        /*
         * User-mode SError: Likely a bad hardware access by user code.
         * Terminate the current process to prevent further damage.
         *
         * TODO: In production, could log to event log / RAS infrastructure.
         */
        DPRINT1("SError: Terminating user-mode process (PID %lu)\n",
                PsGetCurrentProcessId());

        Status = ZwTerminateProcess(NtCurrentProcess(), STATUS_UNSUCCESSFUL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SError: ZwTerminateProcess failed (status 0x%08lX)\n", Status);
            return FALSE;
        }

        /*
         * If we get here, termination was requested successfully. The thread may
         * continue briefly until process rundown completes.
         */
        return TRUE;
    }
}
