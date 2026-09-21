/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/amd64/t_boot.c
 * PURPOSE:     AMD64 memory manager bootstrap regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "bootshim.h"
#include <nvs/arch/amd64/archdef.h>

static PUCHAR Ram;
static ULONG Failures, Checks, Invalidations;
static LONG AllocationBudget;
static PVOID Allocations[1024];
static ULONG AllocationCount;

#define CHECK(e) do { Checks++; if (!(e)) { Failures++; fprintf(stderr, "FAIL %u: %s\n", __LINE__, #e); } } while (0)

PVOID MiArchMapFrame(ULONG64 Frame)
{
    MI_ASSERT(Frame < 4096);
    return Ram + Frame * PAGE_SIZE;
}

VOID MiArchUnmapFrame(PVOID Mapping) { UNREFERENCED_PARAMETER(Mapping); }
MI_PTE MiArchPteRead(PMI_PTE Slot) { return *Slot; }
VOID MiArchPteWrite(PMI_PTE Slot, MI_PTE Value) { *Slot = Value; }

PMI_PTE MiAmd64BootSlot(ULONG64 Address, ULONG Level)
{
    PMI_PTE Table = MiArchMapFrame(1);
    ULONG Current;

    for (Current = 3; Current > Level; Current--)
    {
        MI_PTE Pte = Table[(Address >> (12 + Current * 9)) & 511];
        MI_ASSERT(MiArchPteIsValid(Pte) && !MiArchPteIsBlock(Pte, Current));
        Table = MiArchMapFrame(MiArchPteFrame(Pte));
    }
    return &Table[(Address >> (12 + Level * 9)) & 511];
}

VOID MiBootInvalidate(PVOID Address)
{
    CHECK(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);
    Invalidations++;
}

PVOID MiKmAllocate(SIZE_T Bytes)
{
    if (AllocationBudget == 0)
        return NULL;
    if (AllocationBudget > 0)
        AllocationBudget--;
    MI_ASSERT(AllocationCount < RTL_NUMBER_OF(Allocations));
    return Allocations[AllocationCount++] = malloc(Bytes);
}

VOID MiKmFree(PVOID Block)
{
    ULONG Index;
    for (Index = 0; Index < AllocationCount; Index++)
    {
        if (Allocations[Index] == Block)
        {
            free(Block);
            Allocations[Index] = NULL;
            return;
        }
    }
    MI_ASSERT(FALSE);
}

static void Reset(LOADER_PARAMETER_BLOCK *Loader)
{
    ULONG Index;
    for (Index = 0; Index < AllocationCount; Index++)
        free(Allocations[Index]);
    AllocationCount = 0;
    AllocationBudget = -1;
    Invalidations = 0;
    memset(Ram, 0xA5, 4096 * PAGE_SIZE);
    memset(MiArchMapFrame(1), 0, PAGE_SIZE);
    InitializeListHead(&Loader->MemoryDescriptorListHead);
}

static void Add(LOADER_PARAMETER_BLOCK *Loader, MEMORY_ALLOCATION_DESCRIPTOR *Descriptor,
                TYPE_OF_MEMORY Type, ULONG64 First, ULONG64 Count)
{
    Descriptor->MemoryType = Type;
    Descriptor->BasePage = First;
    Descriptor->PageCount = Count;
    InsertTailList(&Loader->MemoryDescriptorListHead, &Descriptor->ListEntry);
}

static MI_PTE Leaf(ULONG64 Frame, PULONG Level)
{
    ULONG Current;
    ULONG64 Address = MI_AMD64_DIRECT_BASE + Frame * PAGE_SIZE;
    for (Current = 3; Current != 0; Current--)
    {
        MI_PTE Pte = *MiAmd64BootSlot(Address, Current);
        if (!MiArchPteIsValid(Pte) || MiArchPteIsBlock(Pte, Current))
        {
            *Level = Current;
            return Pte;
        }
    }
    *Level = 0;
    return *MiAmd64BootSlot(Address, 0);
}

int main(void)
{
    LOADER_PARAMETER_BLOCK Loader;
    MEMORY_ALLOCATION_DESCRIPTOR Descriptors[8];
    ULONG Frame, Level;
    ULONG64 Pages;
    PLIST_ENTRY Entry;

    MI_ASSERT(posix_memalign((void **)&Ram, PAGE_SIZE, 4096 * PAGE_SIZE) == 0);
    Reset(&Loader);
    Add(&Loader, &Descriptors[0], LoaderMemoryData, 1, 16);
    Add(&Loader, &Descriptors[1], LoaderFree, 17, 300);
    Add(&Loader, &Descriptors[2], LoaderMemoryData, 512, 512);
    Add(&Loader, &Descriptors[3], LoaderFree, 1025, 1600);
    Add(&Loader, &Descriptors[4], LoaderBad, 317, 195);
    Add(&Loader, &Descriptors[5], LoaderSpecialMemory, 1024, 1);
    Add(&Loader, &Descriptors[6], LoaderFirmwarePermanent, 3000, 50);
    CHECK(MiAmd64PreparePhysicalMap(&Loader) == STATUS_SUCCESS);
    CHECK(Invalidations >= 4);

    for (Frame = 0; Frame < 4096; Frame++)
    {
        BOOLEAN Expected = (Frame >= 1 && Frame < 317) || (Frame >= 512 && Frame < 1024) ||
                           (Frame >= 1025 && Frame < 2625);
        MI_PTE Pte = Leaf(Frame, &Level);
        CHECK(MiArchPteIsValid(Pte) == Expected);
        if (Expected)
        {
            CHECK(MiArchPteFrame(Pte) + (Frame & ((1u << (Level * 9)) - 1)) == Frame);
            CHECK(!MiArchPteIsUser(Pte) && !MiArchPteIsExecutable(Pte, FALSE));
            CHECK(MiArchPteIsWritable(Pte) && MiArchPteIsDirty(Pte));
            CHECK((Pte & ((1ULL << 1) | (1ULL << 8))) == ((1ULL << 1) | (1ULL << 8)));
        }
    }
    CHECK(MiArchPteIsBlock(Leaf(512, &Level), 1) && Level == 1);
    Pages = 0;
    for (Entry = Loader.MemoryDescriptorListHead.Flink; Entry != &Loader.MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor = CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR,
                                                                     ListEntry);
        Pages += Descriptor->PageCount;
        CHECK(Descriptor->BasePage < 4096 && Descriptor->PageCount < 4096);
    }
    CHECK(Pages == 2674);
    CHECK(Descriptors[3].PageCount < 1600);

    Reset(&Loader);
    Add(&Loader, &Descriptors[0], LoaderFree, 512, 2048);
    Add(&Loader, &Descriptors[1], LoaderMemoryData, 1ULL << 20, 512);
    Add(&Loader, &Descriptors[2], LoaderMemoryData, 1ULL << 27, 512);
    Add(&Loader, &Descriptors[3], LoaderMemoryData, (MI_AMD64_DIRECT_BYTES >> PAGE_SHIFT) - 512, 512);
    CHECK(MiAmd64PreparePhysicalMap(&Loader) == STATUS_SUCCESS);
    CHECK(MiArchPteFrame(Leaf(1ULL << 20, &Level)) == (1ULL << 20) && Level == 1);
    CHECK(MiArchPteFrame(Leaf(1ULL << 27, &Level)) == (1ULL << 27) && Level == 1);
    CHECK(MiArchPteFrame(Leaf((MI_AMD64_DIRECT_BYTES >> PAGE_SHIFT) - 512, &Level)) ==
          (MI_AMD64_DIRECT_BYTES >> PAGE_SHIFT) - 512 && Level == 1);

    Reset(&Loader);
    CHECK(MiAmd64PreparePhysicalMap(&Loader) == STATUS_INVALID_PARAMETER);
    Add(&Loader, &Descriptors[0], LoaderFree, 1, 100);
    AllocationBudget = 0;
    CHECK(MiAmd64PreparePhysicalMap(&Loader) == STATUS_INSUFFICIENT_RESOURCES);
    AllocationBudget = 1;
    CHECK(MiAmd64PreparePhysicalMap(&Loader) == STATUS_NO_MEMORY);
    CHECK(Descriptors[0].PageCount == 100);

    Reset(&Loader);
    Add(&Loader, &Descriptors[0], LoaderMemoryData, 1, 100);
    CHECK(MiAmd64PreparePhysicalMap(&Loader) == STATUS_NO_MEMORY);
    Reset(&Loader);
    Add(&Loader, &Descriptors[0], LoaderFree, MI_AMD64_DIRECT_BYTES >> PAGE_SHIFT, 1);
    CHECK(MiAmd64PreparePhysicalMap(&Loader) == STATUS_INVALID_PARAMETER);
    Reset(&Loader);
    Add(&Loader, &Descriptors[0], LoaderFree, (MI_AMD64_DIRECT_BYTES >> PAGE_SHIFT) - 1, 2);
    CHECK(MiAmd64PreparePhysicalMap(&Loader) == STATUS_INVALID_PARAMETER);
    Reset(&Loader);
    Add(&Loader, &Descriptors[0], LoaderFree, 1, 100);
    *MiAmd64BootSlot(MI_AMD64_DIRECT_BASE, 3) = MiArchPteMakeTable(1, 0);
    CHECK(MiAmd64PreparePhysicalMap(&Loader) == STATUS_CONFLICTING_ADDRESSES);
    CHECK(Descriptors[0].PageCount == 100);
    Reset(&Loader);
    free(Ram);
    printf("amd64 bootstrap: %u checks, %u failures\n", Checks, Failures);
    return Failures != 0;
}
