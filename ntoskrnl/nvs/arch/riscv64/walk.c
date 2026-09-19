/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/riscv64/walk.c
 * PURPOSE:     Read-only walk of the live Sv39 page tables
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include "hardware.h"

/* Trap and user-copy paths validate translations before the memory manager
 * owns the tables, so the walk only trusts table pages inside the direct
 * map's RAM range. */
static PFN_NUMBER MiRiscvHighestTableFrame;

VOID
NTAPI
MiRiscvInitializePageTableAccess(
    _In_ PFN_NUMBER HighestTableFrame)
{
    ASSERT(HighestTableFrame < (RISCV64_LOADER_PHYSICAL_LIMIT >> PAGE_SHIFT));
    __atomic_store_n(&MiRiscvHighestTableFrame, HighestTableFrame, __ATOMIC_RELEASE);
}

NTSTATUS
NTAPI
MiRiscvWalkPageTables(
    _In_ PFN_NUMBER RootPageFrame,
    _In_ ULONG_PTR VirtualAddress,
    _Out_ PMI_RISCV_PAGE_WALK Result)
{
    PFN_NUMBER HighestTableFrame = __atomic_load_n(&MiRiscvHighestTableFrame, __ATOMIC_ACQUIRE);
    PFN_NUMBER TablePageFrame = RootPageFrame;
    BOOLEAN Global = FALSE;
    ULONG Level = MI_ARCH_PAGING_LEVELS;

    if (!Result || HighestTableFrame == 0)
        return STATUS_INVALID_DEVICE_STATE;

    if (!MiRiscvIsCanonicalAddress(VirtualAddress))
        return STATUS_INVALID_ADDRESS;

    while (Level-- != 0)
    {
        PMMPTE Table, Entry;
        MMPTE Value;
        ULONG Shift = PAGE_SHIFT + Level * 9;
        ULONG Index = (VirtualAddress >> Shift) & 511;
        PFN_NUMBER PageFrame;

        if (TablePageFrame > HighestTableFrame)
            return STATUS_INVALID_ADDRESS;

        Table = MI_RISCV_PFN_TO_VA(TablePageFrame);
        Entry = &Table[Index];
        Value.u.Long = __atomic_load_n(&Entry->u.Long, __ATOMIC_ACQUIRE);

        /* Invalid entries can contain arbitrary software payload bits. */
        if (!Value.u.Hard.Valid)
            return STATUS_NOT_MAPPED_VIEW;

        if ((Value.u.Long & (MI_RISCV_PTE_RESERVED | MI_RISCV_PTE_NAPOT)) ||
            (Value.u.Hard.Write && !Value.u.Hard.Read))
        {
            return STATUS_INVALID_ADDRESS;
        }

        PageFrame = (PFN_NUMBER)((Value.u.Long & MI_RISCV_PTE_PFN_MASK) >> MI_RISCV_PTE_PFN_SHIFT);
        Global = Global || Value.u.Hard.Global;
        if (Value.u.Long & MI_RISCV_PTE_LEAF_MASK)
        {
            PFN_NUMBER PageMask = ((PFN_NUMBER)1 << (Level * 9)) - 1;

            /* A large leaf must be aligned and fit the Sv39 PPN. */
            if ((PageFrame & PageMask) || (PageMask > MI_RISCV_PFN_MAX - PageFrame))
                return STATUS_INVALID_ADDRESS;

            Result->Entry = Entry;
            Result->Value = Value;
            Result->TablePageFrame = TablePageFrame;
            Result->PhysicalAddress.QuadPart = ((ULONG64)PageFrame << PAGE_SHIFT) |
                                               (VirtualAddress & (((ULONG64)1 << Shift) - 1));
            Result->Level = Level;
            Result->Global = Global;
            return STATUS_SUCCESS;
        }

        /* U/A/D are reserved in non-leaf entries. At level zero, a table
         * pointer is a fault, not a readable mapping of that page. */
        if ((Level == 0) || Value.u.Hard.Owner ||
            Value.u.Hard.Accessed || Value.u.Hard.Dirty)
        {
            return STATUS_INVALID_ADDRESS;
        }

        TablePageFrame = PageFrame;
    }

    return STATUS_NOT_MAPPED_VIEW;
}

NTSTATUS
NTAPI
MiRiscvWalkCurrentPageTables(
    _In_ PVOID Address,
    _Out_ PMI_RISCV_PAGE_WALK Result)
{
    ULONG_PTR Satp;

    __asm__ __volatile__("csrr %0, satp" : "=r"(Satp) :: "memory");
    if ((Satp & RISCV64_LOADER_SATP_MODE_MASK) != RISCV64_LOADER_SATP_MODE_SV39)
        return STATUS_NOT_SUPPORTED;

    return MiRiscvWalkPageTables((PFN_NUMBER)(Satp & RISCV64_LOADER_SATP_PPN_MASK), (ULONG_PTR)Address, Result);
}
