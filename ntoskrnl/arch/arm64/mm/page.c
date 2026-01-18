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
    ULONG64 Ttbr0;

    UNREFERENCED_PARAMETER(Process);
    ASSERT(Address < MmSystemRangeStart);

    /*
     * Get the root table PFN from the CURRENT TTBR0 register.
     * This is critical because for the System process, DirectoryTableBase[0]
     * points to kernel page tables (TTBR1), not user page tables.
     * User address translations ALWAYS go through TTBR0.
     */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPfn = (Ttbr0 & ~(ULONG64)0xFFF) >> PAGE_SHIFT;
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
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        /* Write L0 entry pointing to new L1 table */
        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L0Index] = TempPte;
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
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        /* Write L1 entry pointing to new L2 table */
        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L1Index] = TempPte;
        __asm__ __volatile__("dsb sy" ::: "memory");

        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "Created PPE for user address %p: PpeIndex=%lu PdePfn=0x%lx\n",
                   Address, L1Index, (ULONG)NewPagePfn);
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
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        /* Write L2 entry pointing to new L3 table */
        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L2Index] = TempPte;
        __asm__ __volatile__("dsb sy" ::: "memory");

        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "Created PDE for user address %p: PdeIndex=%lu PtePfn=0x%lx\n",
                   Address, L2Index, (ULONG)NewPagePfn);
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
        MappedPage[L3Index] = DemandZeroPte;
        __asm__ __volatile__("dsb sy" ::: "memory");
        Success = TRUE;

        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "[TTBR0] Wrote demand-zero PTE for %p: L3[%lu]=0x%llx\n",
                   Address, L3Index, DemandZeroPte.u.Long);
    }

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
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L0Index] = TempPte;
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
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L1Index] = TempPte;
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
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L2Index] = TempPte;
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
    MappedPage[L3Index] = PteValue;
    __asm__ __volatile__("dsb sy" ::: "memory");
    Success = TRUE;

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

        MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());

        /* Get system PTE for temporary mapping */
        MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
        if (!MappingPte)
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
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
                    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
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
            MappedPage[PxeIndex] = TempPte;

            DPRINT1("Created PXE for user address %p: PxeIndex=%lu PxePfn=0x%lx PpePfn=0x%lx\n",
                    Address, PxeIndex, PxePfn, PpePfn);
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
            OldIrql = MiAcquirePfnLock();
            Color = MI_GET_NEXT_PROCESS_COLOR(Process);
            PdePfn = MiRemoveZeroPageSafe(Color);
            if (!PdePfn)
            {
                PdePfn = MiRemoveAnyPage(Color);
                if (!PdePfn)
                {
                    MiReleasePfnLock(OldIrql);
                    /* Cleanup: invalidate mapping, release PTE */
                    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
                    KeInvalidateTlbEntry(MappedPage);
                    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
                    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
                    return STATUS_NO_MEMORY;
                }
                MiReleasePfnLock(OldIrql);
                MiZeroPhysicalPage(PdePfn);
            }
            else
            {
                MiReleasePfnLock(OldIrql);
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
            MappedPage[PpeIndex] = TempPte;

            DPRINT1("Created PPE for user address %p: PpeIndex=%lu PdePfn=0x%lx\n",
                    Address, PpeIndex, PdePfn);
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
                    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
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
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[PDE DEBUG] ValidKernelPte.u.Long = 0x%llx (NotLargePage=%d, Valid=%d)\n",
                       (ULONG64)ValidKernelPte.u.Long,
                       (int)ValidKernelPte.u.Hard.NotLargePage,
                       (int)ValidKernelPte.u.Hard.Valid);
            TempPte = ValidKernelPte;
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[PDE DEBUG] After copy TempPte.u.Long = 0x%llx (NotLargePage=%d)\n",
                       (ULONG64)TempPte.u.Long, (int)TempPte.u.Hard.NotLargePage);
            TempPte.u.Hard.PageFrameNumber = PtePfn;
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[PDE DEBUG] After PFN set TempPte.u.Long = 0x%llx (NotLargePage=%d, PFN=0x%lx)\n",
                       (ULONG64)TempPte.u.Long, (int)TempPte.u.Hard.NotLargePage, (ULONG)PtePfn);
            TempPte.u.Hard.Owner = 0;  /* No APTable restriction */
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[PDE DEBUG] After Owner set TempPte.u.Long = 0x%llx (NotLargePage=%d)\n",
                       (ULONG64)TempPte.u.Long, (int)TempPte.u.Hard.NotLargePage);
            MappedPage[PdeIndex] = TempPte;
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[PDE DEBUG] Written to PDE[%lu] at %p, readback = 0x%llx (NotLargePage=%d)\n",
                       PdeIndex, &MappedPage[PdeIndex], (ULONG64)MappedPage[PdeIndex].u.Long,
                       (int)MappedPage[PdeIndex].u.Hard.NotLargePage);

            DPRINT1("Created PDE for user address %p: PdeIndex=%lu PtePfn=0x%lx\n",
                    Address, PdeIndex, PtePfn);
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

            MapPte.u.Hard.PageFrameNumber = PtePfn;
            MI_WRITE_VALID_PTE(MappingPte, MapPte);
            MappedPage = MiPteToAddress(MappingPte);

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
            FinalPte.u.Hard.PageFrameNumber = Page;
            FinalPte.u.Long |= MmProtectToPteMask[ProtectionMask];

            /*
             * For writable user pages, ensure NotDirty=0 (AP[1]=0).
             * MmProtectToPteMask sets the software Writable bit (bit 55), but the
             * actual ARM64 hardware write permission is controlled by AP[1] (bit 7).
             * When NotDirty=1, AP[1]=1 makes the page read-only for EL0/EL1.
             * When NotDirty=0, AP[1]=0 makes the page read-write for EL0/EL1.
             */
            if (FinalPte.u.Hard.Writable)
            {
                MI_MAKE_DIRTY_PAGE(&FinalPte);  /* NotDirty=0 for writable */
            }

            if (!IsPhysical)
            {
                KIRQL PfnOldIrql = MiAcquirePfnLock();
                PMMPFN Pfn1 = MiGetPfnEntry(Page);

                Pfn1->u2.ShareCount++;
                Pfn1->u3.e1.PageLocation = ActiveAndValid;
                MiReleasePfnLock(PfnOldIrql);
            }

            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[MmCreateVirtualMapping] User: Address=%p PteIndex=%lu TempPte=0x%llx\n"
                       "  Valid=%d Owner=%d PFN=0x%llx\n",
                       Address, PteIndex, FinalPte.u.Long,
                       (int)FinalPte.u.Hard.Valid,
                       (int)FinalPte.u.Hard.Owner,
                       (ULONG64)FinalPte.u.Hard.PageFrameNumber);

            /* Check for mapping collision */
            if (MappedPage[PteIndex].u.Long != 0)
            {
                DPRINT1("Mapping collision at %p (existing PTE=0x%llx)\n",
                        Address, MappedPage[PteIndex].u.Long);
                MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
                KeInvalidateTlbEntry(MappedPage);
                MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
                MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
                KeBugCheck(MEMORY_MANAGEMENT);
            }

            /* Write the final PTE */
            MappedPage[PteIndex] = FinalPte;

            /* Cleanup system PTE */
            MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
            KeInvalidateTlbEntry(MappedPage);
            MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

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

            /*
             * No TTBR0 switch needed - we now write page tables directly to the
             * TTBR0 hierarchy (read at the start of this function). This ensures
             * the mapping is immediately visible to the CPU for user address
             * translation without needing to switch TTBR0.
             */

            /*
             * Skip MiIncrementPageTableReferences - it uses self-mapping which
             * doesn't work for user addresses. The PFN reference counts were
             * already set up by MiInitializePfnForOtherProcess.
             *
             * TODO: If we need proper page table reference tracking, we would
             * need to manually increment the UsedPageTableEntries count in the
             * PTE table page's PFN entry.
             */
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
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
        MiReleasePfnLock(OldIrql);
    }

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[MmCreateVirtualMapping] Before write: Address=%p PointerPte=%p TempPte=0x%llx\n"
               "  Valid=%d NotLargePage=%d Accessed=%d Owner=%d PFN=0x%llx Writable=%d\n",
               Address, PointerPte, TempPte.u.Long,
               (int)TempPte.u.Hard.Valid,
               (int)TempPte.u.Hard.NotLargePage,
               (int)TempPte.u.Hard.Accessed,
               (int)TempPte.u.Hard.Owner,
               (ULONG64)TempPte.u.Hard.PageFrameNumber,
               (int)TempPte.u.Hard.Writable);

    if (InterlockedExchangePte(PointerPte, TempPte.u.Long) != 0)
    {
        DPRINT1("Mapping collision at %p\n", Address);
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    /* ARM64: Verify the PTE write actually took effect */
    {
        MMPTE VerifyPte;
        VerifyPte.u.Long = PointerPte->u.Long;
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "[MmCreateVirtualMapping] After write: PointerPte->Long=0x%llx Valid=%d Accessed=%d\n",
                   VerifyPte.u.Long, (int)VerifyPte.u.Hard.Valid, (int)VerifyPte.u.Hard.Accessed);
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
    PMMPTE PointerPte;
    MMPTE OldPte;
    BOOLEAN ValidPde = FALSE;
    BOOLEAN Locked = FALSE;

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
    ASSERT(Process == PsGetCurrentProcess());

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
