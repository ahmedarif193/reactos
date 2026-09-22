/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_sys.c
 * PURPOSE:     System memory host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include <nvs/include/miproc.h>
#include "ntmappingshim.h"

PMI_SYSTEM MiMappingTestSystem;
jmp_buf MiMappingTestBugCheck;
ULONG_PTR MiMappingTestBugCheckCode;

#define SYSPTE_PAGES 16384

static
NTSTATUS
KernelWrite64(TEST_WORLD *World, ULONG Cpu, ULONG64 Va, ULONG64 Value)
{
    return MachineAccessMemory(&World->Machine, Cpu, Va, &Value, sizeof(Value), MachineWrite, FALSE);
}

static
ULONG64
KernelRead64(TEST_WORLD *World, ULONG Cpu, ULONG64 Va, NTSTATUS *Status)
{
    ULONG64 Value = 0xDEADDEADDEADDEADULL;

    *Status = MachineAccessMemory(&World->Machine, Cpu, Va, &Value, sizeof(Value), MachineRead, FALSE);
    return Value;
}

static
void
SysWorldCreate(TEST_WORLD *World, ULONG Frames, ULONG Cpus)
{
    WorldCreate(World, Frames, Cpus, 1000000);
    World->Machine.StrictTlb = TRUE;
    WorldAttach(World, 0, NULL);
    CHECK(NT_SUCCESS(MiSystemPtesInitialize(&World->System, SYSPTE_PAGES, Cpus)));
}

static
void
SysWorldDestroy(TEST_WORLD *World, ULONG Frames)
{
    MiSystemPtesUninitialize(&World->System);
    CHECK(MI_ATOMIC_READ64(&World->System.SystemSpace.PageTablePages) == 0);
    CHECK(World->System.SystemSpace.VadRoot.NodeCount == 0);
    WorldExpectClean(World, Frames);
    WorldDestroy(World);
}

static
void
SysPteAllocator(void)
{
    static TEST_WORLD World;
    static ULONG64 Held[4096];
    PMI_SYSTEM_PTES Ptes;
    ULONG Sizes[] = { 1, 2, 3, 5, 8, 16, 17, 100, 1, 1, 64 };
    ULONG64 Va[RTL_NUMBER_OF(Sizes)];
    ULONG Count = 0;
    ULONG i, j;

    SysWorldCreate(&World, 1024, 2);
    Ptes = World.System.SystemPtes;
    CHECK(MI_ATOMIC_READ64(&World.System.SystemSpace.PageTablePages) <= 8);
    CHECK(Ptes->Base >= World.System.Arch->SystemAddressStart);

    for (i = 0; i < RTL_NUMBER_OF(Sizes); i++)
    {
        Va[i] = MiReserveSystemPtes(&World.System, Sizes[i]);
        CHECK(Va[i] >= Ptes->Base && Va[i] + (ULONG64)Sizes[i] * PAGE_SIZE <= Ptes->Base + SYSPTE_PAGES * 4096ULL);
        CHECK((Va[i] & (PAGE_SIZE - 1)) == 0);

        for (j = 0; j < i; j++)
            CHECK(Va[i] + (ULONG64)Sizes[i] * PAGE_SIZE <= Va[j] || Va[j] + (ULONG64)Sizes[j] * PAGE_SIZE <= Va[i]);
    }

    for (i = 0; i < RTL_NUMBER_OF(Sizes); i++)
        MiReleaseSystemPtes(&World.System, Va[i], Sizes[i]);

    CHECK(MiReserveSystemPtes(&World.System, 1) == Va[9]);
    MiReleaseSystemPtes(&World.System, Va[9], 1);
    CHECK(MiSystemPteCacheHits(&World.System) == 1);
    CHECK(MiReserveSystemPtes(&World.System, SYSPTE_PAGES + 1) == 0);
    CHECK(MiReserveSystemPtes(&World.System, 0) == 0);

    while (Count < RTL_NUMBER_OF(Held) && (Held[Count] = MiReserveSystemPtes(&World.System, 1000)) != 0)
        Count++;

    CHECK(Count >= 14 && Count <= 16);
    for (i = 0; i < Count; i += 2)
        MiReleaseSystemPtes(&World.System, Held[i], 1000);
    CHECK(MiReserveSystemPtes(&World.System, 1500) == 0);
    Va[0] = MiReserveSystemPtes(&World.System, 900);
    CHECK(Va[0] != 0);
    MiReleaseSystemPtes(&World.System, Va[0], 900);
    for (i = 1; i < Count; i += 2)
        MiReleaseSystemPtes(&World.System, Held[i], 1000);
    Va[0] = MiReserveSystemPtes(&World.System, 8000);
    CHECK(Va[0] != 0);
    MiReleaseSystemPtes(&World.System, Va[0], 8000);

    SysWorldDestroy(&World, 1024);
}

static void
SysReservedMapping(void)
{
    static TEST_WORLD World;
    struct { MDL Mdl; PFN_NUMBER Frames[2]; } Buffer = {0};
    ULONG Tag = 0x4E565354;
    ULONG64 Prefix, Header, Physical;
    PVOID Address;
    PMI_PTE First, Second;
    ULONG i;

    SysWorldCreate(&World, 1024, 1);
    MiMappingTestSystem = &World.System;
    Prefix = MiReserveSystemPtes(&World.System, 511);
    Address = MmAllocateMappingAddress(256 * PAGE_SIZE, Tag);
    CHECK(Address != NULL);
    Header = (ULONG64)(ULONG_PTR)Address - 2 * PAGE_SIZE;
    CHECK(Header == Prefix + 511 * PAGE_SIZE);
    First = MiPtLookup(&World.System.SystemSpace, Header, NULL);
    Second = MiPtLookup(&World.System.SystemSpace, Header + PAGE_SIZE, NULL);
    CHECK(((ULONG_PTR)First & (PAGE_SIZE - 1)) == PAGE_SIZE - sizeof(MI_PTE));
    CHECK(Second != First + 1);
    CHECK(MiArchPteRead(First) == (256 << 1));
    CHECK(MiArchPteRead(Second) == (MI_PTE)Tag << 1);

    MmInitializeMdl(&Buffer.Mdl, (PVOID)(ULONG_PTR)31, PAGE_SIZE);
    for (i = 0; i < 2; i++)
        Buffer.Frames[i] = MiPfnAllocatePage(&World.System.Pfn, 0);
    if (setjmp(MiMappingTestBugCheck) == 0)
    {
        MmMapLockedPagesWithReservedMapping(Address, Tag + 1, &Buffer.Mdl, MmCached);
        CHECK(FALSE);
    }
    CHECK(MiMappingTestBugCheckCode == 0x104);
    CHECK(MmMapLockedPagesWithReservedMapping(Address, Tag, &Buffer.Mdl, MmCached) == (PUCHAR)Address + 31);
    for (i = 0; i < 2; i++)
    {
        CHECK(MiPtTranslate(&World.System.SystemSpace, (ULONG_PTR)Address + i * PAGE_SIZE, &Physical, NULL));
        CHECK((Physical >> PAGE_SHIFT) == Buffer.Frames[i]);
    }
    MmUnmapReservedMapping(Address, Tag, &Buffer.Mdl);
    CHECK(Buffer.Mdl.MappedSystemVa == NULL);
    CHECK(!(Buffer.Mdl.MdlFlags & MDL_MAPPED_TO_SYSTEM_VA));
    Buffer.Mdl.ByteCount = 257 * PAGE_SIZE;
    CHECK(MmMapLockedPagesWithReservedMapping(Address, Tag, &Buffer.Mdl, MmCached) == NULL);
    if (setjmp(MiMappingTestBugCheck) == 0)
    {
        MmFreeMappingAddress(Address, Tag + 1);
        CHECK(FALSE);
    }
    CHECK(MiMappingTestBugCheckCode == 0x101);
    MmFreeMappingAddress(Address, Tag);
    CHECK(MiArchPteRead(First) == 0 && MiArchPteRead(Second) == 0);
    CHECK(MmAllocateMappingAddress(256 * PAGE_SIZE, Tag) == Address);
    MmFreeMappingAddress(Address, Tag);
    for (i = 0; i < 2; i++)
        MiPfnShareDecrement(&World.System.Pfn, (ULONG)Buffer.Frames[i], TRUE);
    MiReleaseSystemPtes(&World.System, Prefix, 511);
    SysWorldDestroy(&World, 1024);
    MiMappingTestSystem = NULL;
}

/* A failed lazy population must return its VA extent, even after creating
 * some tables, and permit a retry when physical pages become available. */
static
void
SysPtePopulationFailure(void)
{
    static TEST_WORLD World;
    ULONG Held[128], Count = 0, Frame;
    ULONG64 Va;
    LONG64 Tables;
    KIRQL OldIrql;

    SysWorldCreate(&World, RTL_NUMBER_OF(Held), 1);
    Tables = World.System.SystemSpace.PageTablePages;
    while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
        Held[Count++] = Frame;
    CHECK(Count > 1);
    MiPfnShareDecrement(&World.System.Pfn, Held[--Count], TRUE);

    MI_RAISE_TO_DISPATCH(&OldIrql);
    CHECK(MiReserveSystemPtes(&World.System, 2048) == 0);
    CHECK(MiHostIrql == 2);
    MI_RESTORE_IRQL(OldIrql);
    CHECK(World.System.SystemPtes->Allocations.NodeCount == 0);
    CHECK(World.System.SystemPtes->FreePages == SYSPTE_PAGES);
    CHECK(World.System.SystemSpace.PageTablePages > Tables);

    while (Count != 0)
        MiPfnShareDecrement(&World.System.Pfn, Held[--Count], TRUE);
    MI_RAISE_TO_DISPATCH(&OldIrql);
    Va = MiReserveSystemPtes(&World.System, 2048);
    CHECK(Va == World.System.SystemPtes->Base);
    CHECK(MiPtLookup(&World.System.SystemSpace, Va + 2047ULL * PAGE_SIZE, NULL) != NULL);
    MiReleaseSystemPtes(&World.System, Va, 2048);
    CHECK(MiHostIrql == 2);
    MI_RESTORE_IRQL(OldIrql);
    SysWorldDestroy(&World, RTL_NUMBER_OF(Held));
}

/* Pi 3 desktop startup retains both miniports' contiguous slabs, then VidMm
 * maps the real adapter's slab again. These are virtual reservations, not a
 * second allocation of the underlying graphics RAM. */
static
void
SysGraphicsMappingCapacity(void)
{
    static TEST_WORLD World;
    const ULONG64 Capacities[] = { (468ULL << 20) >> PAGE_SHIFT, MI_SYSPTE_64BIT_PAGES };
    const ULONG BasicPages = (0x5EEC000 >> PAGE_SHIFT) + 1;
    const ULONG SlabPages = (192UL << 20) >> PAGE_SHIFT;
    const ULONG64 Physical = 0x1DFA9000;
    ULONG Case;

    for (Case = 0; Case < RTL_NUMBER_OF(Capacities); Case++)
    {
        ULONG64 Basic, Slab, Mapping = 0;
        NTSTATUS Status;

        /* A 1 TiB VA reservation must not allocate tables for the whole arena. */
        WorldCreate(&World, 4096, 1, 1000000);
        WorldAttach(&World, 0, NULL);
        CHECK(NT_SUCCESS(MiSystemPtesInitialize(&World.System, Capacities[Case], 1)));
        CHECK(World.System.SystemSpace.PageTablePages <= 8);
        Basic = MiReserveSystemPtes(&World.System, BasicPages);
        Slab = MiReserveSystemPtes(&World.System, SlabPages + 1);
        CHECK(Basic != 0 && Slab != 0);
        Status = MiMapIoSpace(&World.System, Physical, (ULONG64)SlabPages << PAGE_SHIFT,
                              MiCacheWriteCombined, &Mapping);
        if (Case == 0)
        {
            CHECK(Status == STATUS_INSUFFICIENT_RESOURCES);
            CHECK(Mapping == 0);
        }
        else
        {
            CHECK(NT_SUCCESS(Status));
            CHECK(Mapping != 0);
            if (NT_SUCCESS(Status))
            {
                CHECK(MiGetPhysicalAddress(&World.System.SystemSpace, Mapping) == Physical);
                CHECK(MiGetPhysicalAddress(&World.System.SystemSpace,
                                          Mapping + ((ULONG64)SlabPages << PAGE_SHIFT) - 1) ==
                      Physical + ((ULONG64)SlabPages << PAGE_SHIFT) - 1);
                MiUnmapIoSpace(&World.System, Mapping, (ULONG64)SlabPages << PAGE_SHIFT);
                CHECK(MiGetPhysicalAddress(&World.System.SystemSpace, Mapping) == 0);
            }
        }
        MiReleaseSystemPtes(&World.System, Slab, SlabPages + 1);
        MiReleaseSystemPtes(&World.System, Basic, BasicPages);
        MiSystemPtesUninitialize(&World.System);
        WorldExpectClean(&World, 4096);
        WorldDestroy(&World);
    }
}

static
void
SysKernelStacks(void)
{
    static TEST_WORLD World;
    NTSTATUS Status;
    ULONG64 Top, Again, Large;
    LONG64 Commit;

    SysWorldCreate(&World, 1024, 2);
    Commit = MI_ATOMIC_READ64(&World.System.CommittedPages);

    CHECK(MiCreateKernelStack(&World.System, 6, 7, &Top) == STATUS_INVALID_PARAMETER);
    CHECK(NT_SUCCESS(MiCreateKernelStack(&World.System, 6, 6, &Top)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == Commit + 6);
    CHECK(NT_SUCCESS(KernelWrite64(&World, 0, Top - 8, 0x57AC)));
    CHECK(NT_SUCCESS(KernelWrite64(&World, 0, Top - 6 * 4096, 0x57AD)));
    CHECK(KernelRead64(&World, 0, Top - 8, &Status) == 0x57AC);
    CHECK(MI_ATOMIC_READ64(&World.Machine.Faults) == 0);
    KernelRead64(&World, 0, Top - 6 * 4096 - 8, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(MachineAccessMemory(&World.Machine, 0, Top - 8, NULL, 1, MachineRead, TRUE) == STATUS_ACCESS_VIOLATION);
    CHECK(MachineAccessMemory(&World.Machine, 0, Top - 8, NULL, 1, MachineExecute, FALSE) ==
          STATUS_ACCESS_VIOLATION);

    MiDeleteKernelStack(&World.System, Top, 6);
    CHECK(NT_SUCCESS(MiCreateKernelStack(&World.System, 6, 6, &Again)));
    CHECK(Again == Top);
    MiDeleteKernelStack(&World.System, Again, 6);

    CHECK(NT_SUCCESS(MiCreateKernelStack(&World.System, 16, 3, &Large)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == Commit + 6 + 3);
    CHECK(NT_SUCCESS(KernelWrite64(&World, 0, Large - 3 * 4096, 1)));
    CHECK(KernelWrite64(&World, 0, Large - 3 * 4096 - 8, 1) == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(MiGrowKernelStack(&World.System, Large, 16, Large - 9 * 4096 + 0x123)));
    CHECK(NT_SUCCESS(KernelWrite64(&World, 0, Large - 9 * 4096, 2)));
    CHECK(KernelRead64(&World, 0, Large - 3 * 4096, &Status) == 1);
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == Commit + 6 + 9);
    CHECK(NT_SUCCESS(MiGrowKernelStack(&World.System, Large, 16, Large - 4096)));
    CHECK(MiGrowKernelStack(&World.System, Large, 16, Large - 17 * 4096) == STATUS_STACK_OVERFLOW);
    CHECK(NT_SUCCESS(MiGrowKernelStack(&World.System, Large, 16, Large - 16 * 4096)));
    CHECK(KernelWrite64(&World, 0, Large - 16 * 4096 - 8, 1) == STATUS_ACCESS_VIOLATION);
    MiDeleteKernelStack(&World.System, Large, 16);
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == Commit + 6);

    CHECK(MiTrimAddressSpace(&World.System.SystemSpace, 1000, TRUE) == 0);

    {
        PMI_VAD Region;
        ULONG64 RegionBase;
        LONG64 Tables = MI_ATOMIC_READ64(&World.System.SystemSpace.PageTablePages);
        KIRQL OldIrql;

        CHECK(NT_SUCCESS(MiSystemRegionCreate(&World.System, 2048, TRUE, &Region, &RegionBase)));
        CHECK(MI_ATOMIC_READ64(&World.System.SystemSpace.PageTablePages) >= Tables + 4);
        Tables = MI_ATOMIC_READ64(&World.System.SystemSpace.PageTablePages);
        OldIrql = MiHostRaiseIrql(2);
        CHECK(NT_SUCCESS(MiSystemCommitPinned(&World.System, RegionBase + 0x5000, 64, MI_PROT_READWRITE)));
        CHECK(MI_ATOMIC_READ64(&World.System.SystemSpace.PageTablePages) == Tables);
        CHECK(MiSystemCommitPinned(&World.System, RegionBase + 0x6000, 4, MI_PROT_READWRITE) ==
              STATUS_CONFLICTING_ADDRESSES);
        CHECK(MiHostIrql == 2);
        MiHostLowerIrql(OldIrql);
        CHECK(NT_SUCCESS(KernelWrite64(&World, 0, RegionBase + 0x5000 + 63 * 4096, 0xF00)));
        CHECK(KernelRead64(&World, 0, RegionBase + 0x5000, &Status) == 0 && NT_SUCCESS(Status));
        KernelRead64(&World, 0, RegionBase + 0x4000, &Status);
        CHECK(Status == STATUS_ACCESS_VIOLATION);
        CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == Commit + 6 + 64);
        OldIrql = MiHostRaiseIrql(2);
        MiSystemDecommitPinned(&World.System, RegionBase + 0x5000, 64);
        CHECK(MiHostIrql == 2);
        MiHostLowerIrql(OldIrql);
        KernelRead64(&World, 0, RegionBase + 0x5000, &Status);
        CHECK(Status == STATUS_ACCESS_VIOLATION);
        CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == Commit + 6);
        CHECK(NT_SUCCESS(MiSystemCommitPinned(&World.System, RegionBase, 1, MI_PROT_EXECUTE_READWRITE)));
        CHECK(NT_SUCCESS(MiSystemCommitPinned(&World.System, RegionBase + PAGE_SIZE, 1, MI_PROT_READWRITE)));
        WorldAttach(&World, 1, NULL);
        for (ULONG Cpu = 0; Cpu < 2; Cpu++)
        {
            CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, Cpu, RegionBase, NULL, 1, MachineExecute, FALSE)));
            CHECK(MachineAccessMemory(&World.Machine, Cpu, RegionBase, NULL, 1, MachineExecute, TRUE) ==
                  STATUS_ACCESS_VIOLATION);
            CHECK(MachineAccessMemory(&World.Machine, Cpu, RegionBase + PAGE_SIZE, NULL, 1, MachineExecute, FALSE) ==
                  STATUS_ACCESS_VIOLATION);
        }
        MiSystemDecommitPinned(&World.System, RegionBase, 2);
        CHECK(NT_SUCCESS(MiSystemCommitPinned(&World.System, RegionBase, 1, MI_PROT_READWRITE)));
        for (ULONG Cpu = 0; Cpu < 2; Cpu++)
            CHECK(MachineAccessMemory(&World.Machine, Cpu, RegionBase, NULL, 1, MachineExecute, FALSE) ==
                  STATUS_ACCESS_VIOLATION);
        MiSystemDecommitPinned(&World.System, RegionBase, 1);
        WorldAttach(&World, 0, NULL);
        CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == Commit + 6);
        MiSystemRegionDelete(&World.System, Region, TRUE);
    }

    SysWorldDestroy(&World, 1024);
}

static
void
SysIoAndContiguous(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Process;
    ULONG64 Base = 0, Size;
    ULONG64 Va, Big;
    ULONG64 Frame, Leaf;
    NTSTATUS Status;
    ULONG Device;
    ULONG Held[2048];
    ULONG HeldCount = 0;
    ULONG i;

    SysWorldCreate(&World, 2048, 2);
    WorldAttachPageFile(&World, 2048);

    Device = MiPfnAllocatePage(&World.System.Pfn, MI_ALLOCATE_ZEROED);
    CHECK(NT_SUCCESS(MiMapIoSpace(&World.System, ((ULONG64)Device << PAGE_SHIFT) + 0x123, 0x1000, MiCacheNone, &Va)));
    CHECK((Va & 0xFFF) == 0x123);
    CHECK(NT_SUCCESS(KernelWrite64(&World, 0, Va, 0xD371CE)));
    memcpy(&Frame, MachineFrame(&World.Machine, Device) + 0x123, sizeof(Frame));
    CHECK(Frame == 0xD371CE);
    CHECK(MachineProbe(&World.Machine, 0, Va + 0x1000, &Frame, &Leaf) && Frame == Device + 1u);
    CHECK(MiGetPhysicalAddress(&World.System.SystemSpace, Va) == ((ULONG64)Device << PAGE_SHIFT) + 0x123);
    CHECK(World.PfnArray[Device].ShareCount == 1 && World.PfnArray[Device].ReferenceCount == 1);
    MiUnmapIoSpace(&World.System, Va, 0x1000);
    KernelRead64(&World, 0, Va, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(MiMapIoSpace(&World.System, 0x1000, 0, MiCacheNone, &Va) == STATUS_INVALID_PARAMETER);
    MiPfnShareDecrement(&World.System.Pfn, Device, TRUE);

    {
        const MI_ARCH_DESCRIPTOR *Arch = World.System.Arch;
        ULONG64 BlockSpan = 1ULL << Arch->Level[1].Shift;
        ULONG64 BlockVa = Arch->SystemAddressStart + 64 * BlockSpan;
        ULONG64 BlockFrame = 512;
        ULONG64 Physical;
        ULONG TableFrame, UpperFrame;
        PMI_PTE Probe = MiPtEnsure(&World.System.SystemSpace, BlockVa + BlockSpan, &TableFrame);
        PMI_PTE Upper;
        MI_PTE Leaf;

        CHECK(Probe != NULL);
        UpperFrame = World.PfnArray[TableFrame].PteFrame;
        Upper = (PMI_PTE)MiArchMapFrame(UpperFrame) +
                ((BlockVa >> Arch->Level[1].Shift) & Arch->Level[1].IndexMask);
        CHECK(MiArchPteRead(Upper) == 0);
        MiPtWrite(&World.System.SystemSpace, BlockVa, Upper, UpperFrame,
                  MiArchPteMakeBlock(BlockFrame, MI_PROT_READWRITE, MI_LEAF_GLOBAL | MI_LEAF_DIRTY));

        CHECK(MiPtTranslate(&World.System.SystemSpace, BlockVa + 0x5123, &Physical, &Leaf));
        CHECK(Physical == (BlockFrame << PAGE_SHIFT) + 0x5123 && MiArchPteIsBlock(Leaf, 1));
        CHECK(MiPtLookup(&World.System.SystemSpace, BlockVa + 0x5000, NULL) == NULL);
        CHECK(MiGetPhysicalAddress(&World.System.SystemSpace, BlockVa + BlockSpan) == 0);
        CHECK(NT_SUCCESS(KernelWrite64(&World, 0, BlockVa + 0x7008, 0xB10C)));
        CHECK(*(ULONG64 *)(MachineFrame(&World.Machine, BlockFrame + 7) + 8) == 0xB10C);
        CHECK(MiPtCheck(&World.System.SystemSpace) == 0);

        MiPtWrite(&World.System.SystemSpace, BlockVa, Upper, UpperFrame, 0);
        MiArchTlbInvalidateAll(TRUE);
        MiPtWrite(&World.System.SystemSpace, BlockVa + BlockSpan, Probe, TableFrame, MiSoftMake(MiSoftDecommitted, 0, 0));
        MiPtWrite(&World.System.SystemSpace, BlockVa + BlockSpan, Probe, TableFrame, 0);
    }

    CHECK(NT_SUCCESS(MiAllocateContiguousMemory(&World.System, 64 * 4096, 0, 0x3FFFFF, 64 * 4096, MiCacheFull,
                                                &Big)));
    for (i = 0; i < 64; i++)
    {
        ULONG64 Physical = MiGetPhysicalAddress(&World.System.SystemSpace, Big + i * 4096ULL);

        CHECK(Physical == MiGetPhysicalAddress(&World.System.SystemSpace, Big) + i * 4096ULL);
        CHECK(Physical + 4096 <= 0x400000);
        CHECK(NT_SUCCESS(KernelWrite64(&World, 0, Big + i * 4096ULL, i)));
    }
    CHECK((MiGetPhysicalAddress(&World.System.SystemSpace, Big) & (64 * 4096 - 1)) == 0);
    CHECK(MiAllocateContiguousMemory(&World.System, 4096ULL * 4096, 0, ~0ULL, 0, MiCacheFull, &Va) ==
          STATUS_INSUFFICIENT_RESOURCES);
    MiFreeContiguousMemory(&World.System, Big);

    while ((i = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
        Held[HeldCount++] = i;
    for (i = 0; i < HeldCount; i++)
    {
        if ((Held[i] & 7) != 3)
            MiPfnShareDecrement(&World.System.Pfn, Held[i], TRUE);
    }
    CHECK(MiAllocateContiguousMemory(&World.System, 8 * 4096, 0, ~0ULL, 0, MiCacheFull, &Va) ==
          STATUS_INSUFFICIENT_RESOURCES);
    CHECK(NT_SUCCESS(MiAllocateContiguousMemory(&World.System, 7 * 4096, 0, ~0ULL, 0, MiCacheFull, &Va)));
    MiFreeContiguousMemory(&World.System, Va);
    for (i = 0; i < HeldCount; i++)
    {
        if ((Held[i] & 7) == 3)
            MiPfnShareDecrement(&World.System.Pfn, Held[i], TRUE);
    }

    CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World.System, &Process)));
    WorldAttach(&World, 0, &Process);
    Size = 1800 * 4096ULL;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Process, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                             MI_PROT_READWRITE)));
    for (i = 0; i < 1800; i++)
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + i * 4096ULL, i ^ 0xABCD)));
    CHECK(MiTrimAddressSpace(&Process, 4000, TRUE) == 1800);
    CHECK(MiWriteModifiedPages(&World.System, 4000) == 1800);
    CHECK(NT_SUCCESS(MiAllocateContiguousMemory(&World.System, 512 * 4096, 0, ~0ULL, 0, MiCacheFull, &Big)));
    CHECK(MI_ATOMIC_READ64(&World.System.Pfn.Repurposed) != 0);
    for (i = 0; i < 1800; i += 7)
        CHECK(UserRead64(&World, 0, Base + i * 4096ULL, &Status) == (i ^ 0xABCD));
    MiFreeContiguousMemory(&World.System, Big);

    MiCleanAddressSpace(&Process);
    WorldAttach(&World, 0, NULL);
    MiAddressSpaceDestroy(&Process);
    SysWorldDestroy(&World, 2048);
}

static
void
SysMdl(void)
{
    static TEST_WORLD World;
    TEST_FILE File;
    MI_ADDRESS_SPACE Process;
    PMI_SEGMENT Segment;
    PMI_MDL Mdl;
    ULONG64 Base = 0, Size = 0x10000;
    ULONG64 View = 0, ViewSize = 0;
    ULONG64 SystemVa, StackTop;
    LONG64 Available;
    NTSTATUS Status;
    ULONG Old;
    ULONG i;

    SysWorldCreate(&World, 1024, 2);
    WorldAttachPageFile(&World, 512);
    FileCreate(&File, 0x10000);
    CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World.System, &Process)));
    WorldAttach(&World, 0, &Process);

    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Process, &Base, &Size, MI_MEM_RESERVE, MI_PROT_READWRITE)));
    Mdl = MiMdlAllocate(&Process, Base + 0x1F00, 0x3000);
    CHECK(Mdl != NULL && Mdl->PageCount == 4 && Mdl->ByteOffset == 0xF00);
    CHECK(MiProbeAndLockPages(Mdl, TRUE, TRUE) == STATUS_ACCESS_VIOLATION);
    CHECK(!Mdl->Locked);

    Size = 0x10000;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Process, &Base, &Size, MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + 0x2000, 0x1234)));
    CHECK(NT_SUCCESS(MiProbeAndLockPages(Mdl, TRUE, TRUE)));
    CHECK(MiProbeAndLockPages(Mdl, TRUE, TRUE) == STATUS_INVALID_PARAMETER);
    CHECK(MI_ATOMIC_READ64(&Process.ResidentPages) == 4);
    for (i = 0; i < 4; i++)
        CHECK(World.PfnArray[Mdl->Frames[i]].ReferenceCount == 2);

    CHECK(MiTrimAddressSpace(&Process, 100, TRUE) == 4);
    CHECK(MI_ATOMIC_READ64(&Process.ResidentPages) == 0);
    for (i = 0; i < 4; i++)
    {
        CHECK(World.PfnArray[Mdl->Frames[i]].ReferenceCount == 1);
        CHECK(World.PfnArray[Mdl->Frames[i]].State == MiPageTransition);
    }
    CHECK(MiWriteModifiedPages(&World.System, 100) == 0);
    CHECK(UserRead64(&World, 0, Base + 0x2000, &Status) == 0x1234);
    CHECK(World.PfnArray[Mdl->Frames[1]].ReferenceCount == 2 && World.PfnArray[Mdl->Frames[1]].State == MiPageActive);

    CHECK(NT_SUCCESS(MiMapLockedPages(Mdl, MiCacheFull, &SystemVa)));
    CHECK((SystemVa & 0xFFF) == 0xF00);
    CHECK(KernelRead64(&World, 0, SystemVa + 0x100, &Status) == 0x1234);
    CHECK(NT_SUCCESS(KernelWrite64(&World, 0, SystemVa, 0xD0D0)));
    CHECK(UserRead64(&World, 0, Base + 0x1F00, &Status) == 0xD0D0);

    Available = (LONG64)MiPfnAvailablePages(&World.System.Pfn);
    Size = 0;
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Process, &Base, &Size, MI_MEM_RELEASE)));
    CHECK(KernelRead64(&World, 0, SystemVa, &Status) == 0xD0D0 && NT_SUCCESS(Status));
    for (i = 0; i < 4; i++)
        CHECK(World.PfnArray[Mdl->Frames[i]].ReferenceCount == 1 && World.PfnArray[Mdl->Frames[i]].ShareCount == 0);
    MiUnlockPages(Mdl);
    CHECK(Mdl->MappedSystemVa == 0);
    MiPfnDrainCaches(&World.System.Pfn);
    CHECK((LONG64)MiPfnAvailablePages(&World.System.Pfn) >= Available + 4);
    MiMdlFree(Mdl);

    Base = 0;
    Size = 0x4000;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Process, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                             MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Process, &(ULONG64){Base}, &(ULONG64){0x1000}, MI_PROT_READONLY,
                                            &Old)));
    Mdl = MiMdlAllocate(&Process, Base, 0x1000);
    CHECK(MiProbeAndLockPages(Mdl, TRUE, TRUE) == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(MiProbeAndLockPages(Mdl, TRUE, FALSE)));
    MiUnlockPages(Mdl);
    MiMdlFree(Mdl);

    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, 0x10000, MI_PROT_READONLY, &TestFileOps,
                                     &File, NULL, 0, &Segment)));
    CHECK(NT_SUCCESS(MiMapView(&Process, Segment, &View, 0, &ViewSize, MI_PROT_WRITECOPY, 0)));
    MiSegmentDereference(Segment);
    Mdl = MiMdlAllocate(&Process, View + 0x2000, 0x2000);
    CHECK(NT_SUCCESS(MiProbeAndLockPages(Mdl, TRUE, TRUE)));
    CHECK(MI_ATOMIC_READ64(&Process.CopyOnWriteFaults) == 2);
    CHECK(!(World.PfnArray[Mdl->Frames[0]].Flags & MI_PFN_FLAG_PROTOTYPE));
    MiUnlockPages(Mdl);
    CHECK(World.PfnArray[Mdl->Frames[0]].Flags & MI_PFN_FLAG_MODIFIED);
    MiMdlFree(Mdl);

    {
        ULONG64 UserMapping = 0;
        ULONG64 Payload = 0;
        ULONG64 PayloadSize = 0x3000;

        CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Process, &Payload, &PayloadSize, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                                 MI_PROT_READWRITE)));
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Payload + 0x1008, 0xFEED)));
        Mdl = MiMdlAllocate(&Process, Payload, 0x3000);
        CHECK(NT_SUCCESS(MiProbeAndLockPages(Mdl, TRUE, TRUE)));
        CHECK(NT_SUCCESS(MiMapFramesUser(&Process, Mdl->Frames, 3, MI_PROT_READWRITE, 0, TRUE, &UserMapping)));
        CHECK(UserMapping != 0 && UserMapping != Payload);
        CHECK(UserRead64(&World, 0, UserMapping + 0x1008, &Status) == 0xFEED);
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, UserMapping + 0x2000, 0xBEEF)));
        CHECK(UserRead64(&World, 0, Payload + 0x2000, &Status) == 0xBEEF);
        CHECK(MiTrimAddressSpace(&Process, 1000, TRUE) != 0);
        CHECK(UserRead64(&World, 0, UserMapping + 0x1008, &Status) == 0xFEED && NT_SUCCESS(Status));
        CHECK(MiFreeVirtualMemory(&Process, &(ULONG64){UserMapping}, &(ULONG64){0}, MI_MEM_RELEASE) ==
              STATUS_UNABLE_TO_DELETE_SECTION);
        CHECK(MiUnmapFramesUser(&Process, Payload, TRUE) == STATUS_NOT_MAPPED_VIEW);
        CHECK(MiUnmapFramesUser(&Process, UserMapping + 0x2FFF, FALSE) == STATUS_INVALID_PAGE_PROTECTION);
        CHECK(UserRead64(&World, 0, UserMapping + 0x1008, &Status) == 0xFEED && NT_SUCCESS(Status));
        CHECK(NT_SUCCESS(MiUnmapFramesUser(&Process, UserMapping, TRUE)));
        UserRead64(&World, 0, UserMapping, &Status);
        CHECK(Status == STATUS_ACCESS_VIOLATION);
        CHECK(NT_SUCCESS(MiMapFramesUser(&Process, Mdl->Frames, 3, MI_PROT_READONLY, 0, TRUE, &UserMapping)));
        CHECK(UserWrite64(&World, 0, UserMapping, 1) == STATUS_ACCESS_VIOLATION);
        MiUnlockPages(Mdl);
        MiMdlFree(Mdl);
    }

    Mdl = MiMdlAllocate(&Process, World.System.Arch->SystemAddressStart + 0x5000, 0x1000);
    CHECK(MiProbeAndLockPages(Mdl, TRUE, FALSE) == STATUS_ACCESS_VIOLATION);
    MiMdlFree(Mdl);

    CHECK(NT_SUCCESS(MiCreateKernelStack(&World.System, 6, 6, &StackTop)));
    Mdl = MiMdlAllocate(&World.System.SystemSpace, StackTop - 0x2010, 0x2000);
    CHECK(NT_SUCCESS(MiBuildMdlForSystemRange(Mdl)));
    CHECK(Mdl->PageCount == 3);
    CHECK(((ULONG64)Mdl->Frames[2] << PAGE_SHIFT) ==
          (MiGetPhysicalAddress(&World.System.SystemSpace, StackTop - 0x10) & ~0xFFFULL));
    CHECK(NT_SUCCESS(MiProbeAndLockPages(Mdl, FALSE, TRUE)));
    MiUnlockPages(Mdl);
    MiMdlFree(Mdl);
    {
        ULONG64 Addresses[] = { Base + PAGE_SIZE, Base + 3 * PAGE_SIZE, Base + PAGE_SIZE };
        MI_FRAME_NUMBER Frames[RTL_NUMBER_OF(Addresses)];
        MI_FRAME_NUMBER First;

        CHECK(NT_SUCCESS(MiLockSelectedPages(&Process, Addresses, 3, TRUE, TRUE, Frames)));
        CHECK(Frames[0] == Frames[2] && Frames[0] != Frames[1]);
        CHECK(World.PfnArray[Frames[0]].ReferenceCount == 3);
        CHECK(World.PfnArray[Frames[1]].ReferenceCount == 2);
        First = Frames[0];
        MiUnlockFrames(&World.System, Frames, 3, TRUE);
        CHECK(World.PfnArray[First].ReferenceCount == 1);

        Addresses[1] = Base;
        CHECK(MiLockSelectedPages(&Process, Addresses, 3, TRUE, TRUE, Frames) == STATUS_ACCESS_VIOLATION);
        CHECK(World.PfnArray[First].ReferenceCount == 1);
        CHECK(NT_SUCCESS(MiLockSelectedPages(&Process, Addresses, 3, TRUE, FALSE, Frames)));
        MiUnlockFrames(&World.System, Frames, 3, FALSE);
        Addresses[1] = StackTop - PAGE_SIZE;
        CHECK(MiLockSelectedPages(&Process, Addresses, 3, TRUE, FALSE, Frames) == STATUS_ACCESS_VIOLATION);
        CHECK(World.PfnArray[First].ReferenceCount == 1);
        CHECK(NT_SUCCESS(MiLockSelectedPages(&Process, Addresses, 3, FALSE, TRUE, Frames)));
        CHECK(Frames[1] == MiGetPhysicalAddress(&World.System.SystemSpace, StackTop - PAGE_SIZE) >> PAGE_SHIFT);
        MiUnlockFrames(&World.System, Frames, 3, TRUE);
        Addresses[1]++;
        CHECK(MiLockSelectedPages(&Process, Addresses, 3, FALSE, FALSE, Frames) == STATUS_INVALID_PARAMETER);
        CHECK(World.PfnArray[First].ReferenceCount == 1);
        CHECK(MiLockSelectedPages(&Process, NULL, 3, TRUE, FALSE, Frames) == STATUS_INVALID_PARAMETER);
        CHECK(MiLockSelectedPages(&Process, Addresses, 0, TRUE, FALSE, Frames) == STATUS_INVALID_PARAMETER);
        CHECK(MiLockPages(&World.System.SystemSpace, ~(ULONG64)(PAGE_SIZE - 1), 2, FALSE, FALSE, Frames) ==
              STATUS_ACCESS_VIOLATION);
    }
    MiDeleteKernelStack(&World.System, StackTop, 6);

    MiCleanAddressSpace(&Process);
    WorldAttach(&World, 0, NULL);
    MiAddressSpaceDestroy(&Process);
    FileDestroy(&File);
    SysWorldDestroy(&World, 1024);
}

#if defined(TEST_LIGHT)
#define SYS_THREADS    4
#define SYS_ITERATIONS 400
#else
#define SYS_THREADS    6
#define SYS_ITERATIONS 6000
#endif

typedef struct _SYS_THREAD
{
    TEST_WORLD *World;
    ULONG Index;
    volatile LONG *Stop;
} SYS_THREAD;

static
void *
SysWorker(void *Argument)
{
    SYS_THREAD *Thread = Argument;
    TEST_WORLD *World = Thread->World;
    ULONG64 Seed = 0xA0761D6478BD642FULL * (Thread->Index + 1);
    MI_ADDRESS_SPACE Process;
    ULONG64 Base = 0, Size = 64 * 4096;
    NTSTATUS Status;
    ULONG i;

    MiHostCpu = Thread->Index;
    CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World->System, &Process)));
    WorldAttach(World, Thread->Index, &Process);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Process, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                             MI_PROT_READWRITE)));

    for (i = 0; i < SYS_ITERATIONS; i++)
    {
        ULONG Action = (ULONG)(Rng(&Seed) % 4);

        if (Action == 0)
        {
            ULONG Pages = (ULONG)(Rng(&Seed) % 40) + 1;
            ULONG64 Va = MiReserveSystemPtes(&World->System, Pages);

            CHECK(Va != 0);
            if (Va != 0)
            {
                MI_FRAME_NUMBER Frame = MiPfnAllocatePage(&World->System.Pfn, 0);

                if (Frame != MI_FRAME_INVALID)
                {
                    CHECK(NT_SUCCESS(MiSystemMapFrames(&World->System, Va, &Frame, 1, MI_PROT_READWRITE, 0, TRUE)));
                    CHECK(NT_SUCCESS(KernelWrite64(World, Thread->Index, Va + 8, Va)));
                    CHECK(KernelRead64(World, Thread->Index, Va + 8, &Status) == Va);
                    MiSystemUnmap(&World->System, Va, 1, TRUE);
                }

                MiReleaseSystemPtes(&World->System, Va, Pages);
            }
        }
        else if (Action == 1)
        {
            ULONG64 Top;

            Status = MiCreateKernelStack(&World->System, 6, 6, &Top);
            if (NT_SUCCESS(Status))
            {
                CHECK(NT_SUCCESS(KernelWrite64(World, Thread->Index, Top - 16, Top)));
                CHECK(KernelRead64(World, Thread->Index, Top - 16, &Status) == Top);
                MiDeleteKernelStack(&World->System, Top, 6);
            }
        }
        else
        {
            ULONG64 Offset = Rng(&Seed) % (60 * 4096);
            PMI_MDL Mdl = MiMdlAllocate(&Process, Base + Offset, (ULONG)(Rng(&Seed) % 0x3000) + 8);
            ULONG64 SystemVa;

            Status = MiProbeAndLockPages(Mdl, TRUE, TRUE);
            if (NT_SUCCESS(Status))
            {
                if (NT_SUCCESS(MiMapLockedPages(Mdl, MiCacheFull, &SystemVa)))
                {
                    CHECK(NT_SUCCESS(KernelWrite64(World, Thread->Index, SystemVa, Offset)));
                    CHECK(UserRead64(World, Thread->Index, Base + Offset, &Status) == Offset);
                }

                MiUnlockPages(Mdl);
            }
            else
            {
                CHECK(Status == STATUS_NO_MEMORY);
            }

            MiMdlFree(Mdl);
        }
    }

    MiCleanAddressSpace(&Process);
    WorldAttach(World, Thread->Index, NULL);
    MiAddressSpaceDestroy(&Process);
    return NULL;
}

static
void
SysSmp(void)
{
    static TEST_WORLD World;
    SYS_THREAD Threads[SYS_THREADS];
    pthread_t Handles[SYS_THREADS];
    ULONG i;

    WorldCreate(&World, 2048, MACHINE_MAX_CPUS, 1000000);
    World.Machine.StrictTlb = FALSE;
    CHECK(NT_SUCCESS(MiSystemPtesInitialize(&World.System, SYSPTE_PAGES, MACHINE_MAX_CPUS)));

    for (i = 0; i < SYS_THREADS; i++)
    {
        Threads[i].World = &World;
        Threads[i].Index = i;
        pthread_create(&Handles[i], NULL, SysWorker, &Threads[i]);
    }

    for (i = 0; i < SYS_THREADS; i++)
        pthread_join(Handles[i], NULL);

    MiHostCpu = 0;
    SysWorldDestroy(&World, 2048);
}

static
void
SysSelfMapFault(void)
{
    TEST_WORLD World;
    MI_VAD Vad;
    PMI_PTE Root;
    ULONG RootFrame;

    WorldCreate(&World, 1024, 1, 100000);
    RootFrame = World.System.SystemSpace.RootFrame;
    Root = MiArchMapFrame(RootFrame);
    MiArchPteWrite(&Root[0], MiArchPteMakeTable(RootFrame, 0));
    MiArchPteWrite(&Root[493], MiArchPteMakeTable(RootFrame, 0));
    MiArchPteWrite(&Root[494], MiArchPteMakeTable(RootFrame, 0));

    RtlZeroMemory(&Vad, sizeof(Vad));
    Vad.Type = MiVadSystem;
    Vad.Protection = MI_PROT_EXECUTE_READWRITE;
    Vad.Node.StartingVpn = 0xFFFFF68000000000ULL >> PAGE_SHIFT;
    Vad.Node.EndingVpn = 0xFFFFF6FFFFFFFFFFULL >> PAGE_SHIFT;
    CHECK(MiVadInsert(&World.System.SystemSpace.VadRoot, &Vad.Node));
    CHECK(MiFault(&World.System.SystemSpace, 0xFFFFF6FB80000000ULL, MiFaultRead, FALSE) ==
          STATUS_ACCESS_VIOLATION);
    MiVadRemove(&World.System.SystemSpace.VadRoot, &Vad.Node);

    MiArchPteWrite(&Root[0], 0);
    MiArchPteWrite(&Root[493], 0);
    MiArchPteWrite(&Root[494], 0);
    WorldDestroy(&World);
}

static
void
SysBootAdoption(void)
{
    static TEST_WORLD World;
    static MI_PROCESS_MANAGER Manager;
    static MI_PROCESS Process;
    const MI_ARCH_DESCRIPTOR *Arch;
    PMI_SYSTEM System = &World.System;
    ULONG64 TopSpan, BlockSpan, BootVa, BlockVa;
    ULONG Top, TopIndex;
    PMI_PTE Table;
    NTSTATUS Status;
    ULONG64 Value;
    ULONG Frames = 4096;
    ULONG Level;
    ULONG Frame;
    ULONG i;

    memset(&World, 0, sizeof(World));
    MachineCreate(&World.Machine, Frames, 2);
    World.Machine.StrictTlb = TRUE;
    World.PfnArray = calloc(Frames, sizeof(MI_PFN));
    MiSystemInitialize(System, World.PfnArray, Frames, 2, 1000000);
    Arch = System->Arch;
    Top = Arch->PagingLevels - 1;
    TopSpan = 1ULL << Arch->Level[Top].Shift;
    BlockSpan = 1ULL << Arch->Level[1].Shift;
    BootVa = Arch->SystemAddressStart + 5 * TopSpan + 0x1000;
    BlockVa = Arch->SystemAddressStart + 5 * TopSpan + 3 * BlockSpan;
    TopIndex = (ULONG)((BootVa >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);

    Frame = 2;
    for (Level = Top; Level >= 1; Level--)
    {
        Table = MiArchMapFrame(Frame);
        Table[(BootVa >> Arch->Level[Level].Shift) & Arch->Level[Level].IndexMask] = MiArchPteMakeTable(Frame + 1, 0);

        if (Level == 1)
        {
            Table[(BlockVa >> Arch->Level[1].Shift) & Arch->Level[1].IndexMask] =
                MiArchPteMakeBlock(512, MI_PROT_READWRITE, MI_LEAF_GLOBAL | MI_LEAF_DIRTY);
        }

        Frame++;
    }

    Table = MiArchMapFrame(Frame);
    Table[(BootVa >> PAGE_SHIFT) & Arch->Level[0].IndexMask] =
        MiArchPteMakeLeaf(9, MI_PROT_READWRITE, MI_LEAF_GLOBAL | MI_LEAF_DIRTY);
    *(ULONG64 *)MachineFrame(&World.Machine, 9) = 0xB007B007;

    Table = MiArchMapFrame(2);
    Table[TopIndex - 2] = MiArchPteMakeTable(2, 0);

    World.Machine.SystemRoot = 2;
    CHECK(NT_SUCCESS(MiAddressSpaceAdopt(System, &System->SystemSpace, 2, TRUE)));
    MiPfnMarkInUse(&System->Pfn, 1, 15);
    MiPfnMarkInUse(&System->Pfn, 512, 512);
    MiPfnDbAddRange(&System->Pfn, 16, 512 - 16);
    MiPfnDbAddRange(&System->Pfn, 1024, Frames - 1024);
    CHECK(NT_SUCCESS(MiSystemAdoptBootMappings(System)));
    CHECK(NT_SUCCESS(MiSystemPopulateTopLevel(System)));
    WorldEnableFaults(&World);
    WorldAttach(&World, 0, NULL);

    CHECK(System->SystemSpace.VadRoot.NodeCount == 2);
    CHECK(MI_ATOMIC_READ64(&System->SystemSpace.PageTablePages) ==
          (LONG64)(Top + 254) - (LONG64)((Arch->SystemReservedEnd > Arch->SystemAddressStart)
                                             ? (Arch->SystemReservedEnd - Arch->SystemAddressStart) / TopSpan
                                             : 0));
    CHECK(MiArchPteFrame(Table[TopIndex - 2]) == 2);
    for (i = 3; i <= 2 + Top; i++)
    {
        CHECK(World.PfnArray[i].State == MiPageActive);
        CHECK((World.PfnArray[i].Flags & (MI_PFN_FLAG_PAGE_TABLE | MI_PFN_FLAG_PINNED)) ==
              (MI_PFN_FLAG_PAGE_TABLE | MI_PFN_FLAG_PINNED));
        CHECK(World.PfnArray[i].PteFrame == i - 1);
    }
    CHECK(MiPtCheck(&System->SystemSpace) == 0);

    Value = 0;
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, BootVa, &Value, 8, MachineRead, FALSE)));
    CHECK(Value == 0xB007B007);
    CHECK(NT_SUCCESS(KernelWrite64(&World, 0, BlockVa + 0x3008, 0xB10C)));
    CHECK(*(ULONG64 *)(MachineFrame(&World.Machine, 515) + 8) == 0xB10C);
    CHECK(MiGetPhysicalAddress(&System->SystemSpace, BlockVa + 0x3008) == (515ULL << PAGE_SHIFT) + 8);
    CHECK(MiFault(&System->SystemSpace, BootVa + 0x5000, MiFaultRead, FALSE) == STATUS_ACCESS_VIOLATION);

    CHECK(NT_SUCCESS(MiSystemPtesInitialize(System, 2048, 2)));
    CHECK((System->SystemPtes->Base >> Arch->Level[Top].Shift) != (BootVa >> Arch->Level[Top].Shift));
    CHECK(NT_SUCCESS(MiProcessManagerInitialize(System, &Manager)));
    CHECK(NT_SUCCESS(MiProcessCreate(System, &Manager, &Process)));
    WorldAttach(&World, 1, &Process.Space);

    {
        PMI_PTE SystemRoot = MiArchMapFrame(System->SystemSpace.RootFrame);
        PMI_PTE ProcessRoot = MiArchMapFrame(Process.Space.RootFrame);
        ULONG First = (ULONG)((Arch->SystemAddressStart >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);

        CHECK(ProcessRoot[TopIndex] == SystemRoot[TopIndex] && SystemRoot[TopIndex] != 0);
        CHECK(ProcessRoot[First + 100] == SystemRoot[First + 100] && SystemRoot[First + 100] != 0);
        CHECK(MiArchPteIsValid(ProcessRoot[493]));
        CHECK(MiArchPteFrame(ProcessRoot[493]) == Process.Space.RootFrame);
        CHECK(!MiArchPteIsUser(ProcessRoot[493]));
        CHECK(ProcessRoot[494] == 0);
    }

    CHECK(UserWrite64(&World, 1, MI_SHARED_USER_DATA_VA, 0) == STATUS_ACCESS_VIOLATION);
    CHECK(UserRead64(&World, 1, MI_SHARED_USER_DATA_VA, &Status) == 0 && NT_SUCCESS(Status));
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 1, BootVa, &Value, 8, MachineRead, FALSE)));
    CHECK(MiPtCheck(&Process.Space) == 0);

    WorldAttach(&World, 1, NULL);
    MiProcessDelete(&Manager, &Process);
    MiProcessManagerUninitialize(System, &Manager);
    MiSystemPtesUninitialize(System);
    CHECK(MiPtCheck(&System->SystemSpace) == 0);
    CHECK(WorldCheck(&World) == 0);
    WorldDestroy(&World);
}

/* The host models PTE attributes explicitly: checking just shared contents
 * would miss the incoherent cached/uncached aliases seen on real hardware. */
static
void
SysMdlCacheAttributes(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Process;
    MI_FRAME_NUMBER Frames[3];
    ULONG64 Allocations[3], Kernel, User, Physical;
    const MI_CACHE_TYPE Types[] = { MiCacheFull, MiCacheNone, MiCacheWriteCombined };
    const ULONG Flags[] = { 0, MI_LEAF_NOCACHE, MI_LEAF_WRITECOMBINE };
    struct { MDL Mdl; PFN_NUMBER Frames[3]; } Native;
    PVOID Reserved;
    MI_PTE Pte;
    NTSTATUS Status;
    ULONG i, Requested;

    SysWorldCreate(&World, 1024, 2);
    CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World.System, &Process)));
    WorldAttach(&World, 0, &Process);
    MiMappingTestSystem = &World.System;
    RtlZeroMemory(&Native, sizeof(Native));
    Native.Mdl.ByteCount = sizeof(Frames) / sizeof(Frames[0]) * PAGE_SIZE;
    Reserved = MmAllocateMappingAddress(Native.Mdl.ByteCount, 0x43414348);
    CHECK(Reserved != NULL);

    for (i = 0; i < RTL_NUMBER_OF(Frames); i++)
    {
        CHECK(NT_SUCCESS(MiAllocateContiguousMemory(&World.System, PAGE_SIZE, 0, ~0ULL, 0,
                                                    Types[i], &Allocations[i])));
        CHECK(MiPtTranslate(&World.System.SystemSpace, Allocations[i], &Physical, &Pte));
        CHECK(MiArchPteLeafFlags(Pte) == Flags[i]);
        Frames[i] = Physical >> PAGE_SHIFT;
        Native.Frames[i] = Frames[i];
    }

    /* Mixed-cache MDLs must resolve every PFN, not just the first one.
     * A conflicting request must not change an existing RAM cache type. */
    for (Requested = 0; Requested < RTL_NUMBER_OF(Types); Requested++)
    {
        User = 0;
        CHECK(NT_SUCCESS(MiMapFrames(&World.System, Frames, 3, Types[Requested], MI_PROT_READWRITE, &Kernel)));
        CHECK(NT_SUCCESS(MiMapFramesUser(&Process, Frames, 3, MI_PROT_READWRITE, Flags[Requested], TRUE, &User)));
        CHECK(MmMapLockedPagesWithReservedMapping(Reserved, 0x43414348, &Native.Mdl,
                  Requested == 0 ? MmCached : Requested == 1 ? MmNonCached : MmWriteCombined) == Reserved);
        for (i = 0; i < RTL_NUMBER_OF(Frames); i++)
        {
            CHECK(MiPtTranslate(&World.System.SystemSpace, Kernel + i * PAGE_SIZE, &Physical, &Pte));
            CHECK(Physical >> PAGE_SHIFT == Frames[i]);
            CHECK(MiArchPteLeafFlags(Pte) == Flags[i]);
            CHECK(MiPtTranslate(&Process, User + i * PAGE_SIZE, &Physical, &Pte));
            CHECK(Physical >> PAGE_SHIFT == Frames[i]);
            CHECK(MiArchPteLeafFlags(Pte) == Flags[i]);
            CHECK(MiPtTranslate(&World.System.SystemSpace, (ULONG_PTR)Reserved + i * PAGE_SIZE, &Physical, &Pte));
            CHECK(MiArchPteLeafFlags(Pte) == Flags[i]);
            CHECK(NT_SUCCESS(UserWrite64(&World, 0, User + i * PAGE_SIZE, 0xA4A5A6A7)));
            CHECK(KernelRead64(&World, 0, Allocations[i], &Status) == 0xA4A5A6A7 && NT_SUCCESS(Status));
        }
        MmUnmapReservedMapping(Reserved, 0x43414348, &Native.Mdl);
        CHECK(NT_SUCCESS(MiUnmapFramesUser(&Process, User, TRUE)));
        MiUnmapFrames(&World.System, Kernel, 3);
    }

    /* Raw I/O PFNs have no established RAM cache attributes. */
    Frames[0] = World.System.Pfn.FrameCount + 1;
    CHECK(NT_SUCCESS(MiMapFrames(&World.System, Frames, 1, MiCacheNone, MI_PROT_READWRITE, &Kernel)));
    CHECK(MiPtTranslate(&World.System.SystemSpace, Kernel, &Physical, &Pte));
    CHECK(MiArchPteLeafFlags(Pte) == MI_LEAF_NOCACHE);
    MiUnmapFrames(&World.System, Kernel, 1);

    MmFreeMappingAddress(Reserved, 0x43414348);
    MiMappingTestSystem = NULL;
    for (i = 0; i < RTL_NUMBER_OF(Allocations); i++)
        MiFreeContiguousMemory(&World.System, Allocations[i]);
    MiCleanAddressSpace(&Process);
    WorldAttach(&World, 0, NULL);
    MiAddressSpaceDestroy(&Process);
    SysWorldDestroy(&World, 1024);
}

void
TestSys(void)
{
    SysSelfMapFault();
    SysBootAdoption();
    SysPteAllocator();
    SysReservedMapping();
    SysPtePopulationFailure();
    SysGraphicsMappingCapacity();
    SysKernelStacks();
    SysIoAndContiguous();
    SysMdl();
    SysMdlCacheAttributes();
    SysSmp();
}

typedef struct _SYSBENCH_THREAD
{
    TEST_WORLD *World;
    ULONG Index;
    ULONG Mode;
    ULONG64 Operations;
    volatile LONG *Go;
    volatile LONG *Stop;
} SYSBENCH_THREAD;

static const char *SysBenchNames[] =
{
    "system PTE reserve+release 1 page (pairs/s)",
    "system PTE reserve+release 24 pages (pairs/s)",
    "kernel stack create+delete, cached (pairs/s)",
    "kernel stack create+delete 16/3 pages (pairs/s)",
    "MDL probe+lock+unlock 16 resident pages (pages/s)",
    "MDL map+unmap locked 16 pages (pages/s)",
};

static
void *
SysBenchWorker(void *Argument)
{
    SYSBENCH_THREAD *Thread = Argument;
    TEST_WORLD *World = Thread->World;
    MI_ADDRESS_SPACE Process;
    ULONG64 Base = 0, Size = 16 * 4096;
    ULONG64 Operations = 0;
    ULONG64 Va;
    PMI_MDL Mdl;
    ULONG i;

    MiHostCpu = Thread->Index;
    MiAddressSpaceCreate(&World->System, &Process);
    WorldAttach(World, Thread->Index, &Process);
    MiAllocateVirtualMemory(&Process, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE);
    for (i = 0; i < 16; i++)
        MiFault(&Process, Base + i * 4096ULL, MiFaultWrite, TRUE);
    Mdl = MiMdlAllocate(&Process, Base, 16 * 4096);
    if (Thread->Mode == 5)
        MiProbeAndLockPages(Mdl, TRUE, TRUE);

    while (!__atomic_load_n(Thread->Go, __ATOMIC_SEQ_CST))
        sched_yield();

    while (!__atomic_load_n(Thread->Stop, __ATOMIC_SEQ_CST))
    {
        switch (Thread->Mode)
        {
            case 0:
                Va = MiReserveSystemPtes(&World->System, 1);
                MiReleaseSystemPtes(&World->System, Va, 1);
                Operations++;
                break;
            case 1:
                Va = MiReserveSystemPtes(&World->System, 24);
                MiReleaseSystemPtes(&World->System, Va, 24);
                Operations++;
                break;
            case 2:
                MiCreateKernelStack(&World->System, 6, 6, &Va);
                MiDeleteKernelStack(&World->System, Va, 6);
                Operations++;
                break;
            case 3:
                MiCreateKernelStack(&World->System, 16, 3, &Va);
                MiDeleteKernelStack(&World->System, Va, 16);
                Operations++;
                break;
            case 4:
                MiProbeAndLockPages(Mdl, TRUE, TRUE);
                MiUnlockPages(Mdl);
                Operations += 16;
                break;
            default:
                MiMapLockedPages(Mdl, MiCacheFull, &Va);
                MiUnmapLockedPages(Mdl);
                Operations += 16;
                break;
        }
    }

    if (Mdl->Locked)
        MiUnlockPages(Mdl);
    MiMdlFree(Mdl);
    Thread->Operations = Operations;
    MiCleanAddressSpace(&Process);
    MiAddressSpaceDestroy(&Process);
    return NULL;
}

void
TestBenchSys(void)
{
    static const ULONG Counts[] = { 1, 2, 4, 8 };
    static TEST_WORLD World;
    ULONG Mode, c, i;

    for (Mode = 0; Mode < RTL_NUMBER_OF(SysBenchNames); Mode++)
    {
        printf("  %-52s", SysBenchNames[Mode]);

        for (c = 0; c < RTL_NUMBER_OF(Counts); c++)
        {
            SYSBENCH_THREAD Threads[8];
            pthread_t Handles[8];
            volatile LONG Go = 0, Stop = 0;
            ULONG64 Total = 0;
            double Start, Elapsed;

            WorldCreate(&World, 16384, MACHINE_MAX_CPUS, 100000000);
            World.Machine.TlbDisabled = TRUE;
            MiSystemPtesInitialize(&World.System, 65536, MACHINE_MAX_CPUS);

            for (i = 0; i < Counts[c]; i++)
            {
                Threads[i].World = &World;
                Threads[i].Index = i;
                Threads[i].Mode = Mode;
                Threads[i].Operations = 0;
                Threads[i].Go = &Go;
                Threads[i].Stop = &Stop;
                pthread_create(&Handles[i], NULL, SysBenchWorker, &Threads[i]);
            }

            Start = NowSeconds();
            __atomic_store_n(&Go, 1, __ATOMIC_SEQ_CST);
            while (NowSeconds() - Start < 0.4)
                sched_yield();
            __atomic_store_n(&Stop, 1, __ATOMIC_SEQ_CST);

            for (i = 0; i < Counts[c]; i++)
            {
                pthread_join(Handles[i], NULL);
                Total += Threads[i].Operations;
            }

            Elapsed = NowSeconds() - Start;
            printf(" %ut=%8.2fM", Counts[c], (double)Total / Elapsed / 1e6);

            MiSystemPtesUninitialize(&World.System);
            WorldDestroy(&World);
        }

        printf("\n");
    }
}
