/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/amd64/boot.c
 * PURPOSE:     AMD64 memory manager bootstrap
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef MM_HOST_TEST
#include <mmcc/nvs/amd64/bootshim.h>
#else
#include <nvs/nt/mint.h>
#endif
#include "hardware.h"

#ifndef MM_HOST_TEST
static
PMI_PTE
MiAmd64BootSlot(ULONG64 Address, ULONG Level)
{
    return (PMI_PTE)(ULONG_PTR)MiAmd64SelfMapSlotAddress(Address, Level);
}
#endif

typedef struct _MI_AMD64_BOOT_ALLOCATOR
{
    PLOADER_PARAMETER_BLOCK Loader;
    PMEMORY_ALLOCATION_DESCRIPTOR Last;
} MI_AMD64_BOOT_ALLOCATOR;

static
BOOLEAN
MiAmd64BootAllocate(MI_AMD64_BOOT_ALLOCATOR *Allocator, PULONG64 Frame)
{
    PMEMORY_ALLOCATION_DESCRIPTOR Largest = NULL;
    PMEMORY_ALLOCATION_DESCRIPTOR Reserved = Allocator->Last;
    PLIST_ENTRY Entry;

    for (Entry = Allocator->Loader->MemoryDescriptorListHead.Flink;
         Entry != &Allocator->Loader->MemoryDescriptorListHead; Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor = CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR,
                                                                     ListEntry);
        if (Descriptor->MemoryType == LoaderFree && Descriptor->PageCount != 0 &&
            (Largest == NULL || Descriptor->PageCount > Largest->PageCount))
            Largest = Descriptor;
    }
    if (Largest == NULL)
        return FALSE;

    *Frame = Largest->BasePage + Largest->PageCount - 1;
    if (Reserved == NULL || Reserved->BasePage != *Frame + 1)
    {
        Reserved = MiKmAllocate(sizeof(*Reserved));
        if (Reserved == NULL)
            return FALSE;
        Reserved->MemoryType = LoaderMemoryData;
        Reserved->BasePage = *Frame + 1;
        Reserved->PageCount = 0;
        InsertHeadList(&Allocator->Loader->MemoryDescriptorListHead, &Reserved->ListEntry);
        Allocator->Last = Reserved;
    }
    Reserved->BasePage--;
    Reserved->PageCount++;
    Largest->PageCount--;
    return TRUE;
}

static
NTSTATUS
MiAmd64BootMap(MI_AMD64_BOOT_ALLOCATOR *Allocator, ULONG64 Frame, ULONG TargetLevel)
{
    ULONG64 Address = MI_AMD64_DIRECT_BASE + (Frame << PAGE_SHIFT);
    ULONG Level;
    PMI_PTE Slot;

    for (Level = 3; Level > TargetLevel; Level--)
    {
        Slot = MiAmd64BootSlot(Address, Level);
        if (!MiArchPteIsValid(*Slot))
        {
            ULONG64 Child;
            PVOID Table;

            if (!MiAmd64BootAllocate(Allocator, &Child))
                return STATUS_NO_MEMORY;
            MiArchPteWrite(Slot, MiArchPteMakeTable(Child, 0));
            Table = (PVOID)((ULONG_PTR)MiAmd64BootSlot(Address, Level - 1) & ~((ULONG_PTR)PAGE_SIZE - 1));
            __invlpg(Table);
            RtlZeroMemory(Table, PAGE_SIZE);
        }
        else if (MiArchPteIsBlock(*Slot, Level))
        {
            return STATUS_CONFLICTING_ADDRESSES;
        }
    }

    Slot = MiAmd64BootSlot(Address, TargetLevel);
    if (*Slot != 0)
        return STATUS_CONFLICTING_ADDRESSES;
    MiArchPteWrite(Slot, TargetLevel == 1 ?
                         MiArchPteMakeBlock(Frame, MI_PROT_READWRITE, MI_LEAF_GLOBAL | MI_LEAF_DIRTY) :
                         MiArchPteMakeLeaf(Frame, MI_PROT_READWRITE, MI_LEAF_GLOBAL | MI_LEAF_DIRTY));
    return STATUS_SUCCESS;
}

NTSTATUS
MiAmd64PreparePhysicalMap(PLOADER_PARAMETER_BLOCK Loader)
{
    MI_AMD64_BOOT_ALLOCATOR Allocator = { Loader, NULL };
    PMI_PHYSICAL_RANGE Ranges;
    ULONG Count = 0, Index = 0;
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_SUCCESS;

    for (Entry = Loader->MemoryDescriptorListHead.Flink; Entry != &Loader->MemoryDescriptorListHead;
         Entry = Entry->Flink)
        Count++;
    if (Count == 0 || Count > (ULONG)(1024 * 1024 / sizeof(*Ranges)))
        return STATUS_INVALID_PARAMETER;
    Ranges = MiKmAllocate(Count * sizeof(*Ranges));
    if (Ranges == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Entry = Loader->MemoryDescriptorListHead.Flink; Entry != &Loader->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor = CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR,
                                                                     ListEntry);
        ULONG64 First = Descriptor->BasePage;
        ULONG64 Pages = Descriptor->PageCount;
        ULONG64 Maximum = MI_AMD64_DIRECT_BYTES >> PAGE_SHIFT;
        ULONG64 Address, Last;

        if (Pages == 0 || Descriptor->MemoryType == LoaderBad ||
            Descriptor->MemoryType == LoaderFirmwarePermanent || Descriptor->MemoryType == LoaderSpecialMemory ||
            Descriptor->MemoryType == LoaderHALCachedMemory || Descriptor->MemoryType == LoaderBBTMemory)
            continue;
        if (First >= Maximum || Pages > Maximum - First)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        Address = (MI_AMD64_DIRECT_BASE + (First << PAGE_SHIFT)) & ~((1ULL << 39) - 1);
        Last = MI_AMD64_DIRECT_BASE + ((First + Pages - 1) << PAGE_SHIFT);
        for (; Address <= Last; Address += 1ULL << 39)
        {
            if (*MiAmd64BootSlot(Address, 3) != 0)
            {
                Status = STATUS_CONFLICTING_ADDRESSES;
                goto Exit;
            }
        }
        Ranges[Index].BasePage = First;
        Ranges[Index].PageCount = Pages;
        Index++;
    }

    Count = Index;
    for (Index = 0; Index < Count; Index++)
    {
        ULONG64 Frame = Ranges[Index].BasePage;
        ULONG64 End = Frame + Ranges[Index].PageCount;

        while (Frame < End)
        {
            BOOLEAN Large = (Frame & 511) == 0 && End - Frame >= 512;

            Status = MiAmd64BootMap(&Allocator, Frame, Large ? 1 : 0);
            if (!NT_SUCCESS(Status))
                goto Exit;
            Frame += Large ? 512 : 1;
        }
    }

Exit:
    MiKmFree(Ranges);
    return Status;
}

#ifndef MM_HOST_TEST
VOID
NTAPI
MiInitializeKernelVaLayout(_In_ const LOADER_PARAMETER_BLOCK *LoaderBlock)
{
    NTSTATUS Status;

    MmSystemRangeStart = (PVOID)(ULONG_PTR)MiArchDescribe()->SystemAddressStart;
    Status = MiAmd64PreparePhysicalMap((PLOADER_PARAMETER_BLOCK)LoaderBlock);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x444D4150, (ULONG_PTR)Status, 0, 0);
}
#endif
