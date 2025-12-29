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

    if (!MmIsPageInUse(Page))
    {
        DPRINT1("Page %Ix is not in use\n", Page);
        KeBugCheck(MEMORY_MANAGEMENT);
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

    ProtectionMask = MiMakeProtectionMask(Protection);
    ASSERT(ProtectionMask != MM_INVALID_PROTECTION);
    ASSERT(ProtectionMask != MM_NOACCESS);
    ASSERT(ProtectionMask != MM_ZERO_ACCESS);

    if (Process == NULL)
    {
        ASSERT(Address >= MmSystemRangeStart);
        ASSERT(ProtectionMask != MM_WRITECOPY);
        ASSERT(ProtectionMask != MM_EXECUTE_WRITECOPY);

        MiMakeSystemAddressValid(Address, PsGetCurrentProcess());
    }
    else
    {
        ASSERT(Address < MmSystemRangeStart);
        ASSERT(Process == PsGetCurrentProcess());

        MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
        MiMakePdeExistAndMakeValid(MiAddressToPde(Address), Process, MM_NOIRQL);
    }

    PointerPte = MiAddressToPte(Address);
    MI_MAKE_HARDWARE_PTE(&TempPte, PointerPte, ProtectionMask, Page);

    if (!IsPhysical)
    {
        KIRQL OldIrql = MiAcquirePfnLock();
        PMMPFN Pfn1 = MiGetPfnEntry(Page);

        Pfn1->u2.ShareCount++;
        Pfn1->u3.e1.PageLocation = ActiveAndValid;
        MiReleasePfnLock(OldIrql);
    }

    if (InterlockedExchangePte(PointerPte, TempPte.u.Long) != 0)
    {
        DPRINT1("Mapping collision at %p\n", Address);
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    if (Address < MmSystemRangeStart)
    {
        MiIncrementPageTableReferences(Address);
        MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    }

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
    PMMPTE PointerPte;
    MMPTE OldPte;
    BOOLEAN ValidPde = FALSE;
    BOOLEAN Locked = FALSE;

    OldPte.u.Long = 0;

    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    if (Process == NULL)
    {
        ASSERT(Address >= MmSystemRangeStart);
        MiMakeSystemAddressValid(Address, PsGetCurrentProcess());
        ValidPde = TRUE;
    }
    else
    {
        ASSERT(Address < MmSystemRangeStart);
        ASSERT(Process == PsGetCurrentProcess());

        MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
        Locked = TRUE;

        ValidPde = MiIsPageTablePresent(Address);
        if (ValidPde)
            MiMakePdeExistAndMakeValid(MiAddressToPde(Address), Process, MM_NOIRQL);
    }

    if (ValidPde)
    {
        PointerPte = MiAddressToPte(Address);
        OldPte.u.Long = InterlockedExchangePte(PointerPte, 0);
        KeInvalidateTlbEntry(Address);
    }

    if (OldPte.u.Long != 0)
    {
        if (WasDirty)
            *WasDirty = (OldPte.u.Hard.Valid && (OldPte.u.Hard.NotDirty == 0));
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

    if (Process != NULL)
    {
        if (OldPte.u.Long != 0)
        {
            if (MiDecrementPageTableReferences(Address) == 0)
            {
                KIRQL OldIrql = MiAcquirePfnLock();
                MiDeletePde(MiAddressToPde(Address), Process);
                MiReleasePfnLock(OldIrql);
            }
        }

        if (!IsPhysical && OldPte.u.Hard.Valid)
        {
            KIRQL OldIrql = MiAcquirePfnLock();
            PMMPFN Pfn1 = MiGetPfnEntry(OldPte.u.Hard.PageFrameNumber);

            ASSERT(Pfn1->u2.ShareCount > 0);
            if (--Pfn1->u2.ShareCount == 0)
                Pfn1->u3.e1.PageLocation = TransitionPage;

            MiReleasePfnLock(OldIrql);
        }

        if (Locked)
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
    PMMPTE PointerPte;

    ASSERT(Process != NULL);
    ASSERT(Address < MmSystemRangeStart);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    MiMakePdeExistAndMakeValid(MiAddressToPde(Address), Process, MM_NOIRQL);

    PointerPte = MiAddressToPte(Address);

    if (PointerPte->u.Hard.Valid)
    {
        MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
        return STATUS_CONFLICTING_ADDRESSES;
    }

    PointerPte->u.Long = 0;
    PointerPte->u.Soft.PageFileLow = SwapEntry & 0xF;
    PointerPte->u.Soft.PageFileHigh = SwapEntry >> 4;
    PointerPte->u.Soft.Prototype = 0;
    PointerPte->u.Soft.Protection = MM_READWRITE;

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
    PMMPTE PointerPte;
    MMPTE OldPte;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    PointerPte = MiAddressToPte(Address);

    OldPte.u.Long = InterlockedExchangePte(PointerPte, 0);
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
    PMMPTE PointerPte;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());
    PointerPte = MiAddressToPte(Address);

    if (!FlagOn(PointerPte->u.Long, 0x800) || PointerPte->u.Hard.Valid)
        *SwapEntry = 0;
    else
        *SwapEntry = (SWAPENTRY)(((ULONG64)PointerPte->u.Soft.PageFileHigh << 4) |
                                  PointerPte->u.Soft.PageFileLow);

    MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
}

BOOLEAN
NTAPI
MmIsPagePresent(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    BOOLEAN Present;

    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        MiMakeSystemAddressValid(Address, PsGetCurrentProcess());
        return MiAddressToPte(Address)->u.Hard.Valid != 0;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());

    if (!MiIsPageTablePresent(Address))
    {
        MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
        return FALSE;
    }

    MiMakePdeExistAndMakeValid(MiAddressToPde(Address), Process, MM_NOIRQL);
    Present = MiAddressToPte(Address)->u.Hard.Valid != 0;

    MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
    return Present;
}

BOOLEAN
NTAPI
MmIsDisabledPage(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE PointerPte;

    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        return FALSE;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());

    if (!MiIsPageTablePresent(Address))
    {
        MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
        return FALSE;
    }

    MiMakePdeExistAndMakeValid(MiAddressToPde(Address), Process, MM_NOIRQL);
    PointerPte = MiAddressToPte(Address);

    if (!PointerPte->u.Hard.Valid)
    {
        MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
        return FALSE;
    }

    MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
    return (PointerPte->u.Hard.Writable == 0) && (PointerPte->u.Hard.CopyOnWrite == 0);
}

BOOLEAN
NTAPI
MmIsPageSwapEntry(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE PointerPte;
    BOOLEAN Result = FALSE;

    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        return FALSE;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());

    if (MiIsPageTablePresent(Address))
    {
        MiMakePdeExistAndMakeValid(MiAddressToPde(Address), Process, MM_NOIRQL);
        PointerPte = MiAddressToPte(Address);
        Result = (!PointerPte->u.Hard.Valid && FlagOn(PointerPte->u.Long, 0x800));
    }

    MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
    return Result;
}

ULONG
NTAPI
MmGetPageProtect(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE PointerPte;
    ULONG Protect;

    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        MiMakeSystemAddressValid(Address, PsGetCurrentProcess());
        PointerPte = MiAddressToPte(Address);
        return PointerPte->u.Hard.Valid ? MiProtectionFromPte(*PointerPte) : PAGE_NOACCESS;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());

    if (!MiIsPageTablePresent(Address))
    {
        MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
        return PAGE_NOACCESS;
    }

    MiMakePdeExistAndMakeValid(MiAddressToPde(Address), Process, MM_NOIRQL);
    PointerPte = MiAddressToPte(Address);
    Protect = PointerPte->u.Hard.Valid ? MiProtectionFromPte(*PointerPte) : PAGE_NOACCESS;

    MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
    return Protect;
}

VOID
NTAPI
MmSetPageProtect(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection)
{
    ULONG ProtectionMask;
    PMMPTE PointerPte;
    MMPTE TempPte, OldPte;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());
    ASSERT(Address < MmSystemRangeStart);

    ProtectionMask = MiMakeProtectionMask(Protection);
    ASSERT(ProtectionMask != MM_INVALID_PROTECTION);

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    MiMakePdeExistAndMakeValid(MiAddressToPde(Address), Process, MM_NOIRQL);

    PointerPte = MiAddressToPte(Address);

    TempPte.u.Long = MiDetermineUserGlobalPteMask(PointerPte);
    TempPte.u.Long |= MmProtectToPteMask[ProtectionMask];
    TempPte.u.Hard.PageFrameNumber = PointerPte->u.Hard.PageFrameNumber;

    if ((ProtectionMask != MM_NOACCESS) && !FlagOn(ProtectionMask, MM_GUARDPAGE))
        TempPte.u.Hard.Valid = 1;

    if (PointerPte->u.Hard.Accessed)
        TempPte.u.Hard.Accessed = 1;
    if (PointerPte->u.Hard.NotDirty == 0)
        MI_MAKE_DIRTY_PAGE(&TempPte);

    OldPte.u.Long = InterlockedExchangePte(PointerPte, TempPte.u.Long);

    if (!OldPte.u.Hard.Valid && FlagOn(OldPte.u.Long, 0x800))
    {
        DPRINT1("Unexpected non-present PTE during protection change\n");
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    if (OldPte.u.Long != TempPte.u.Long)
        KeInvalidateTlbEntry(Address);

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
}

VOID
NTAPI
MmSetDirtyBit(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN Dirty)
{
    PMMPTE PointerPte;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());
    ASSERT(Address < MmSystemRangeStart);

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    MiMakePdeExistAndMakeValid(MiAddressToPde(Address), Process, MM_NOIRQL);

    PointerPte = MiAddressToPte(Address);

    if (!PointerPte->u.Hard.Valid && FlagOn(PointerPte->u.Long, 0x800))
    {
        DPRINT1("Invalid PTE for MmSetDirtyBit\n");
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    if (Dirty)
        MI_MAKE_DIRTY_PAGE(PointerPte);
    else
        MI_MAKE_CLEAN_PAGE(PointerPte);

    if (!Dirty)
        KeInvalidateTlbEntry(Address);

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
        MiMakeSystemAddressValid(Address, PsGetCurrentProcess());
    }
    else
    {
        ASSERT(Process != NULL);
        ASSERT(Process == PsGetCurrentProcess());

        MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());

        if (!MiIsPageTablePresent(Address))
        {
            MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
            return 0;
        }

        MiMakePdeExistAndMakeValid(MiAddressToPde(Address), Process, MM_NOIRQL);
    }

    PointerPte = MiAddressToPte(Address);
    if (PointerPte->u.Hard.Valid)
        PageFrame = PointerPte->u.Hard.PageFrameNumber;

    if (Address < MmSystemRangeStart)
        MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());

    return PageFrame;
}

static
BOOLEAN
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
    PMMPFN Pfn;

    ASSERT((PsGetCurrentThread()->OwnsProcessWorkingSetExclusive) ||
           (PsGetCurrentThread()->OwnsProcessWorkingSetShared));
    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

#if _MI_PAGING_LEVELS == 4
    PointerPxe = MiAddressToPxe(Address);
    if ((PointerPxe->u.Hard.Valid == 1) || (PointerPxe->u.Soft.Transition == 1))
    {
        Pfn = MiGetPfnEntry(PFN_FROM_PXE(PointerPxe));
        if (Pfn->OriginalPte.u.Soft.UsedPageTableEntries == 0)
            return FALSE;
    }
    else if (PointerPxe->u.Soft.UsedPageTableEntries == 0)
    {
        return FALSE;
    }

    if (PointerPxe->u.Hard.Valid == 0)
        MiMakeSystemAddressValid(MiPteToAddress(PointerPxe), PsGetCurrentProcess());
#endif

    PointerPpe = MiAddressToPpe(Address);
    if ((PointerPpe->u.Hard.Valid == 1) || (PointerPpe->u.Soft.Transition == 1))
    {
        Pfn = MiGetPfnEntry(PFN_FROM_PPE(PointerPpe));
        if (Pfn->OriginalPte.u.Soft.UsedPageTableEntries == 0)
            return FALSE;
    }
    else if (PointerPpe->u.Soft.UsedPageTableEntries == 0)
    {
        return FALSE;
    }

    if (PointerPpe->u.Hard.Valid == 0)
        MiMakeSystemAddressValid(MiPteToAddress(PointerPpe), PsGetCurrentProcess());

    PointerPde = MiAddressToPde(Address);
    if ((PointerPde->u.Hard.Valid == 0) && (PointerPde->u.Soft.Transition == 0))
        return PointerPde->u.Soft.UsedPageTableEntries != 0;

    Pfn = MiGetPfnEntry(PFN_FROM_PDE(PointerPde));
    return Pfn->OriginalPte.u.Soft.UsedPageTableEntries != 0;
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
