/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/ppc/walk.c
 * PURPOSE:     Windows NT PowerPC read-only page table walk
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include "hardware.h"

NTSTATUS
NTAPI
MiPpcWalkPageTables(
    _In_ PFN_NUMBER RootPageFrame,
    _In_ ULONG_PTR VirtualAddress,
    _Out_ PMI_PPC_PAGE_WALK Result)
{
    static const UCHAR Shift[3] = { MI_ARCH_L0_SHIFT, MI_ARCH_L1_SHIFT, MI_ARCH_L2_SHIFT };
    static const ULONG Mask[3] = { 511, 511, 3 };
    PFN_NUMBER TablePageFrame = RootPageFrame;
    ULONG Level = MI_ARCH_PAGING_LEVELS;

    if (!Result)
        return STATUS_INVALID_PARAMETER;

    while (Level-- != 0)
    {
        PMMPTE Table, Entry;
        MMPTE Value;

        if (TablePageFrame >= MI_PPC_KSEG0_PAGES)
            return STATUS_INVALID_ADDRESS;

        Table = MI_PPC_PFN_TO_VA(TablePageFrame);
        Entry = &Table[(VirtualAddress >> Shift[Level]) & Mask[Level]];
        Value.u.Long = MiArchPteRead((PMI_PTE)Entry);
        if (!Value.u.Hard.Valid)
            return STATUS_NOT_MAPPED_VIEW;

        if (Level == 0)
        {
            Result->Entry = Entry;
            Result->Value = Value;
            Result->TablePageFrame = TablePageFrame;
            Result->PhysicalAddress.QuadPart = (Value.u.Long & MI_PPC_PTE_FRAME_MASK) | (VirtualAddress & (PAGE_SIZE - 1));
            Result->Level = 0;
            return STATUS_SUCCESS;
        }
        TablePageFrame = (PFN_NUMBER)((Value.u.Long & MI_PPC_PTE_FRAME_MASK) >> MI_PPC_PTE_FRAME_SHIFT);
    }

    return STATUS_NOT_MAPPED_VIEW;
}

NTSTATUS
NTAPI
MiPpcWalkCurrentPageTables(
    _In_ PVOID Address,
    _Out_ PMI_PPC_PAGE_WALK Result)
{
    return MiPpcWalkPageTables((PFN_NUMBER)(KeGetPcr()->PageTableRoot >> PAGE_SHIFT), (ULONG_PTR)Address, Result);
}
