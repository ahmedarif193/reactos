/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Read-only walk of the authoritative Sv39 page tables
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <mm/ARM3/miarm.h>

/* Bootstrap access is replaced before loader storage can be reclaimed. */
static PMI_RISCV_MAP_TABLE_PAGE MiRiscvMapTablePage;
static PVOID MiRiscvTableMappingContext;
static PFN_NUMBER MiRiscvMaximumPageFrame;

CODE_SEG("INIT")
NTSTATUS
NTAPI
MiRiscvInitializePageTableAccess(
    _In_ PMI_RISCV_MAP_TABLE_PAGE MapTablePage,
    _In_opt_ PVOID MappingContext,
    _In_ PFN_NUMBER MaximumPageFrame)
{
    if (!MapTablePage || MaximumPageFrame > MI_RISCV_PFN_MAX)
        return STATUS_INVALID_PARAMETER;
    if (MiRiscvMapTablePage)
        return STATUS_INVALID_DEVICE_STATE;

    MiRiscvTableMappingContext = MappingContext;
    MiRiscvMaximumPageFrame = MaximumPageFrame;
    __atomic_store_n(&MiRiscvMapTablePage, MapTablePage, __ATOMIC_RELEASE);
    return STATUS_SUCCESS;
}

static
PMMPTE
NTAPI
MiRiscvMapResidentTablePage(
    _In_ PFN_NUMBER PageFrame,
    _In_opt_ PVOID Context)
{
    PMMPFN Pfn;
    PMMPTE Table;
    MMPTE Mapping;

    UNREFERENCED_PARAMETER(Context);
    if ((PageFrame > MmHighestPhysicalPage) ||
        (PageFrame >= (RISCV64_LOADER_DIRECT_MAP_SIZE >> PAGE_SHIFT)))
        return NULL;
    Pfn = MiGetPfnEntry(PageFrame);
    if (!MmIsAddressValid(Pfn) || !MmIsAddressValid((PUCHAR)Pfn + sizeof(*Pfn) - 1) ||
        !Pfn->u3.e2.ReferenceCount ||
        (Pfn->u3.e1.PageLocation != ActiveAndValid))
        return NULL;
    Table = MiRiscvTablePage(PageFrame);
    if (!MmIsAddressValid(Table)) return NULL;
    Mapping = *MiAddressToPte(Table);
    if (!Mapping.u.Hard.Read || Mapping.u.Hard.Owner ||
        (Mapping.u.Hard.PageFrameNumber != PageFrame))
        return NULL;
    return Table;
}

CODE_SEG("INIT")
VOID
NTAPI
MiRiscvAdoptPageTableAccess(VOID)
{
    ULONG_PTR Sstatus;

    /* Phase zero, one hart. Mask local interrupts while publishing all
     * fields so no interrupt walker can observe an intermediate state. */
    __asm__ __volatile__("csrrci %0, sstatus, 2" : "=r"(Sstatus) :: "memory");
    ASSERT(KeNumberProcessors == 1);
    ASSERT(MiRiscvWindowReady && MiPfnsInitialized);
    ASSERT(MiRiscvMapTablePage != MiRiscvMapResidentTablePage);
    MiRiscvTableMappingContext = NULL;
    /* Leaf PFNs are bounded by the Sv39 PTE encoding, not by the loader's
     * mapped-RAM high-water mark. Table pages still require resident RAM and
     * a checked direct-map alias; device leaves need not belong to RAM. */
    MiRiscvMaximumPageFrame = MI_RISCV_PFN_MAX;
    __atomic_store_n(&MiRiscvMapTablePage, MiRiscvMapResidentTablePage, __ATOMIC_RELEASE);
    if (Sstatus & RISCV_SSTATUS_SIE) _enable();
}

NTSTATUS
NTAPI
MiRiscvWalkCurrentPageTables(
    _In_ PVOID Address,
    _Out_ PMI_RISCV_PAGE_WALK Result)
{
    PMI_RISCV_MAP_TABLE_PAGE MapTablePage;
    ULONG_PTR Satp;

    MapTablePage = __atomic_load_n(&MiRiscvMapTablePage, __ATOMIC_ACQUIRE);
    if (!MapTablePage)
        return STATUS_INVALID_DEVICE_STATE;

    __asm__ __volatile__("csrr %0, satp" : "=r"(Satp) :: "memory");
    if ((Satp >> 60) != 8) /* Sv39 only; Bare and other modes are not aliases. */
        return STATUS_NOT_SUPPORTED;

    return MiRiscvWalkPageTables(Satp & MI_RISCV_PFN_MAX,
                                (ULONG_PTR)Address,
                                MiRiscvMaximumPageFrame,
                                MapTablePage,
                                MiRiscvTableMappingContext,
                                Result);
}

NTSTATUS
NTAPI
MiRiscvWalkPageTables(
    _In_ PFN_NUMBER RootPageFrame,
    _In_ ULONG_PTR VirtualAddress,
    _In_ PFN_NUMBER MaximumPageFrame,
    _In_ PMI_RISCV_MAP_TABLE_PAGE MapTablePage,
    _In_opt_ PVOID MappingContext,
    _Out_ PMI_RISCV_PAGE_WALK Result)
{
    PFN_NUMBER TablePageFrame = RootPageFrame;
    BOOLEAN Global = FALSE;
    ULONG Level = _MI_PAGING_LEVELS;

    if (!MapTablePage || !Result ||
        (MaximumPageFrame > MI_RISCV_PFN_MAX) ||
        (RootPageFrame > MaximumPageFrame))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!MiRiscvIsCanonicalAddress(VirtualAddress))
        return STATUS_INVALID_ADDRESS;

    while (Level-- != 0)
    {
        PMMPTE Table, Entry;
        MMPTE Value;
        ULONG Shift = PAGE_SHIFT + Level * 9;
        ULONG Index = (VirtualAddress >> Shift) & (PTE_PER_PAGE - 1);
        PFN_NUMBER PageFrame;

        Table = MapTablePage(TablePageFrame, MappingContext);
        if (!Table || ((ULONG_PTR)Table & (PAGE_SIZE - 1)))
            return STATUS_INVALID_ADDRESS;

        Entry = &Table[Index];
        Value.u.Long = __atomic_load_n(&Entry->u.Long, __ATOMIC_ACQUIRE);

        /* Invalid entries can contain arbitrary software payload bits. */
        if (!Value.u.Hard.Valid)
            return STATUS_NOT_MAPPED_VIEW;

        if ((Value.u.Long & MI_RISCV_PTE_RESERVED) ||
            (Value.u.Hard.Write && !Value.u.Hard.Read))
        {
            return STATUS_INVALID_ADDRESS;
        }

        PageFrame = Value.u.Hard.PageFrameNumber;
        if (PageFrame > MaximumPageFrame)
            return STATUS_INVALID_ADDRESS;

        Global = Global || Value.u.Hard.Global;
        if (Value.u.Hard.Read || Value.u.Hard.Execute)
        {
            PFN_NUMBER PageMask = ((PFN_NUMBER)1 << (Level * 9)) - 1;
            MI_RISCV_PAGE_WALK Walk;

            /* A large leaf must be aligned and its entire span must fit the
             * platform's physical address width, not just the queried byte. */
            if ((PageFrame & PageMask) ||
                (PageMask > MaximumPageFrame - PageFrame))
            {
                return STATUS_INVALID_ADDRESS;
            }

            Walk.Entry = Entry;
            Walk.Value = Value;
            Walk.TablePageFrame = TablePageFrame;
            Walk.PhysicalAddress.QuadPart = ((ULONG64)PageFrame << PAGE_SHIFT) |
                                           (VirtualAddress & (((ULONG64)1 << Shift) - 1));
            Walk.Level = Level;
            Walk.Global = Global;
            *Result = Walk;
            return STATUS_SUCCESS;
        }

        /* U/A/D are reserved in non-leaf entries. At level zero, a table
         * pointer is a fault, not a readable recursive mapping of that page. */
        if ((Level == 0) || Value.u.Hard.Owner ||
            Value.u.Hard.Accessed || Value.u.Hard.Dirty)
        {
            return STATUS_INVALID_ADDRESS;
        }

        TablePageFrame = PageFrame;
    }

    return STATUS_NOT_MAPPED_VIEW;
}
