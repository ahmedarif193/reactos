/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_large.c
 * PURPOSE:     Large-page allocation host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include <nvs/include/misys.h>

#define LARGE_FLAGS (MI_MEM_RESERVE | MI_MEM_COMMIT | MI_MEM_LARGE_PAGES)

static void
LargeLifecycle(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    MI_MEMORY_INFORMATION Info;
    ULONG64 Base = 0, Size, Large, Pages, Physical, First, Other, Length;
    ULONG64 Available;
    MI_PTE Pte;
    MI_FRAME_NUMBER Frames[3];
    ULONG Old, Cpu;
    NTSTATUS Status;

    WorldCreate(&World, 4096, 4, 4096);
    ProcessCreate(&World, &Space);
    Large = World.System.Arch->LargePageSize;
    Pages = Large >> PAGE_SHIFT;
    Size = 2 * Large;
    Available = MiPfnAvailablePages(&World.System.Pfn);
    for (ULONG Frame = 512; Frame < World.System.Pfn.FrameCount; Frame++)
        memset(MachineFrame(&World.Machine, Frame), 0xD3, PAGE_SIZE);
    Status = MiAllocateVirtualMemory(&Space, &Base, &Size, LARGE_FLAGS, MI_PROT_READWRITE);
    CHECK(NT_SUCCESS(Status));
    if (!NT_SUCCESS(Status))
        goto Finish;
    CHECK(Base % Large == 0 && Size == 2 * Large);
    CHECK(Space.CommittedPages == (LONG64)(2 * Pages));
    CHECK(Space.ResidentPages == 0 && Space.PrivatePages == (LONG64)(2 * Pages));
    CHECK(Space.PageTablePages == World.System.Arch->PagingLevels - 2);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base, &Info)));
    CHECK(Info.State == MI_MEM_COMMIT && Info.Type == MI_MEM_PRIVATE);
    CHECK(Info.RegionSize == Size && Info.Protect == MI_PROT_READWRITE);
    CHECK(MiPtTranslate(&Space, Base, &First, &Pte));
    CHECK(First % Large == 0 && MiArchPteIsBlock(Pte, World.System.Arch->LargePageLevel));
    CHECK(MiPtTranslate(&Space, Base + Large - 1, &Physical, NULL));
    CHECK(Physical == First + Large - 1);
    CHECK(MachineProbe(&World.Machine, 0, Base + Large - 1, &Physical, NULL));
    CHECK(Physical == (First >> PAGE_SHIFT) + Pages - 1);
    for (Cpu = 0; Cpu < 4; Cpu++)
    {
        WorldAttach(&World, Cpu, &Space);
        CHECK(UserRead64(&World, Cpu, Base + (Cpu + 1) * PAGE_SIZE, &Status) == 0);
        CHECK(NT_SUCCESS(Status));
        CHECK(NT_SUCCESS(UserWrite64(&World, Cpu, Base + (Cpu + 1) * PAGE_SIZE, 0x100 + Cpu)));
        CHECK(UserRead64(&World, Cpu, Base + (Cpu + 1) * PAGE_SIZE, &Status) == 0x100 + Cpu);
    }
    CHECK(MiTrimAddressSpace(&Space, 1024, TRUE) == 0);
    CHECK(NT_SUCCESS(MiLockPages(&Space, Base + PAGE_SIZE, 3, TRUE, TRUE, Frames)));
    CHECK(Frames[0] == (First >> PAGE_SHIFT) + 1 && Frames[2] == Frames[0] + 2);
    MiUnlockFrames(&World.System, Frames, 3, TRUE);
    Other = Base + Large;
    Length = Large;
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Space, &Other, &Length, MI_PROT_READONLY, &Old)));
    CHECK(Old == MI_PROT_READWRITE);
    CHECK(UserWrite64(&World, 1, Other + PAGE_SIZE, 1) == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Other, &Info)) && Info.Protect == MI_PROT_READONLY);
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Space, &Other, &Length, MI_PROT_NOACCESS, &Old)));
    CHECK(Old == MI_PROT_READONLY);
    UserRead64(&World, 1, Other, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Space, &Other, &Length, MI_PROT_READWRITE, &Old)));
    CHECK(Old == MI_PROT_NOACCESS);
    CHECK(NT_SUCCESS(UserWrite64(&World, 1, Other + PAGE_SIZE, 0x1234)));
    Other = Base + PAGE_SIZE;
    Length = PAGE_SIZE;
    CHECK(!NT_SUCCESS(MiProtectVirtualMemory(&Space, &Other, &Length, MI_PROT_READONLY, &Old)));
    CHECK(!NT_SUCCESS(MiFreeVirtualMemory(&Space, &Other, &Length, MI_MEM_RELEASE)));
    Other = Base;
    Length = Size;
    CHECK(!NT_SUCCESS(MiFreeVirtualMemory(&Space, &Other, &Length, MI_MEM_DECOMMIT)));
    CHECK(MiPtCheck(&Space) == 0);
    CHECK(NT_SUCCESS(MiLockPages(&Space, Base + PAGE_SIZE, 1, TRUE, FALSE, Frames)));
    Length = 0;
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Space, &Base, &Length, MI_MEM_RELEASE)));
    CHECK(Space.CommittedPages == 0 && Space.PrivatePages == 0 && Space.PageTablePages == 0);
    CHECK(MiPfnAvailablePages(&World.System.Pfn) == Available - 1);
    CHECK(World.System.Pfn.Pfn[Frames[0]].ReferenceCount == 1);
    MiUnlockFrames(&World.System, Frames, 1, FALSE);
    CHECK(MiPfnAvailablePages(&World.System.Pfn) == Available);
    CHECK(Space.LargePages == 0);
    for (Cpu = 0; Cpu < 4; Cpu++)
    {
        UserRead64(&World, Cpu, Base + (Cpu + 1) * PAGE_SIZE, &Status);
        CHECK(Status == STATUS_ACCESS_VIOLATION);
    }
    CHECK(World.Machine.StaleTlbUses == 0 && World.Machine.BreakBeforeMakeViolations == 0);
Finish:
    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 4096);
    WorldDestroy(&World);
}

static void
LargeFailures(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Base, Size, Large, Available;
    ULONG i;
    NTSTATUS Status;

    WorldCreate(&World, 2048, 1, 8192);
    ProcessCreate(&World, &Space);
    Large = World.System.Arch->LargePageSize;
    Available = MiPfnAvailablePages(&World.System.Pfn);
    for (i = 0; i < 6; i++)
    {
        Base = (i == 0) ? USER_BASE + PAGE_SIZE : 0;
        Size = (i == 1) ? Large + PAGE_SIZE : (i == 2) ? ~0ULL - Large + 1 : Large;
        Status = MiAllocateVirtualMemory(&Space, &Base, &Size,
                    (i == 3) ? MI_MEM_RESERVE | MI_MEM_LARGE_PAGES : LARGE_FLAGS,
                    (i == 4) ? MI_PROT_READWRITE | MI_PROT_GUARD :
                    (i == 5) ? MI_PROT_WRITECOPY : MI_PROT_READWRITE);
        CHECK(!NT_SUCCESS(Status));
        CHECK(Space.VadRoot.NodeCount == 0 && Space.CommittedPages == 0 && Space.PageTablePages == 0);
        CHECK(MiPfnAvailablePages(&World.System.Pfn) == Available);
    }
    Base = 0;
    Size = 4 * Large;
    CHECK(MiAllocateVirtualMemory(&Space, &Base, &Size, LARGE_FLAGS, MI_PROT_READWRITE) == STATUS_NO_MEMORY);
    CHECK(Base == 0 && Size == 4 * Large);
    CHECK(Space.VadRoot.NodeCount == 0 && Space.CommittedPages == 0 && Space.PageTablePages == 0);
    CHECK(MiPfnAvailablePages(&World.System.Pfn) == Available);
    World.System.CommitLimit = 1;
    Size = Large;
    CHECK(MiAllocateVirtualMemory(&Space, &Base, &Size, LARGE_FLAGS, MI_PROT_READWRITE) == STATUS_COMMITMENT_LIMIT);
    CHECK(MiPfnAvailablePages(&World.System.Pfn) == Available);
    World.System.CommitLimit = 8192;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Base, &Size, LARGE_FLAGS | MI_MEM_TOP_DOWN,
                                           MI_PROT_EXECUTE_READWRITE)));
    CHECK(Base % Large == 0);
    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 2048);
    WorldDestroy(&World);
}

typedef struct _LARGE_WORKER
{
    TEST_WORLD *World;
    PMI_ADDRESS_SPACE Space;
    ULONG Cpu;
    PMI_SEGMENT Segment;
} LARGE_WORKER;

static void *
LargeWorker(void *Argument)
{
    LARGE_WORKER *Worker = Argument;
    ULONG64 Large = Worker->World->System.Arch->LargePageSize;
    ULONG i;

    MiHostCpu = Worker->Cpu;
    WorldAttach(Worker->World, Worker->Cpu, Worker->Space);
    for (i = 0; i < 16; i++)
    {
        ULONG64 Base = 0, Size = Large;
        ULONG64 Offset = Worker->Segment != NULL ? (Worker->Cpu + 1) * PAGE_SIZE : Large - 8;
        NTSTATUS Status = Worker->Segment != NULL
            ? MiMapView(Worker->Space, Worker->Segment, &Base, 0, &Size, MI_PROT_READWRITE, MI_MEM_LARGE_PAGES)
            : MiAllocateVirtualMemory(Worker->Space, &Base, &Size, LARGE_FLAGS, MI_PROT_READWRITE);
        CHECK(NT_SUCCESS(Status));
        if (!NT_SUCCESS(Status))
            continue;
        if (Worker->Segment == NULL || i == 0)
            CHECK(UserRead64(Worker->World, Worker->Cpu, Base + Offset, &Status) == 0 && NT_SUCCESS(Status));
        CHECK(NT_SUCCESS(UserWrite64(Worker->World, Worker->Cpu, Base + Offset, 0xCAFE + i)));
        CHECK(UserRead64(Worker->World, Worker->Cpu, Base + Offset, &Status) == 0xCAFE + i);
        Size = 0;
        CHECK(NT_SUCCESS(Worker->Segment != NULL ? MiUnmapView(Worker->Space, Base)
            : MiFreeVirtualMemory(Worker->Space, &Base, &Size, MI_MEM_RELEASE)));
    }
    WorldAttach(Worker->World, Worker->Cpu, NULL);
    return NULL;
}

static void
LargeConcurrent(BOOLEAN Shared)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    LARGE_WORKER Workers[4];
    pthread_t Threads[4];
    PMI_SEGMENT Segment = NULL;
    ULONG i;

    WorldCreate(&World, 8192, 4, 8192);
    ProcessCreate(&World, &Space);
    if (Shared)
    {
        CHECK(NT_SUCCESS(MiSegmentCreateLarge(&World.System, World.System.Arch->LargePageSize,
                                             MI_PROT_READWRITE, NULL, NULL, &Segment)));
        if (Segment == NULL)
            goto Finish;
    }
    for (i = 0; i < 4; i++)
    {
        Workers[i] = (LARGE_WORKER){ &World, &Space, i, Segment };
        MI_ASSERT(pthread_create(&Threads[i], NULL, LargeWorker, &Workers[i]) == 0);
    }
    for (i = 0; i < 4; i++)
        MI_ASSERT(pthread_join(Threads[i], NULL) == 0);
    CHECK(Space.LargePages == 0 && Space.CommittedPages == 0 && Space.PrivatePages == 0);
    CHECK(World.Machine.StaleTlbUses == 0 && MiPtCheck(&Space) == 0);
Finish:
    if (Segment != NULL)
        MiSegmentDereference(Segment);
    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 8192);
    WorldDestroy(&World);
}

static void
LargeSectionLifecycle(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Parent, Peer, Child, Excluded;
    PMI_SEGMENT Segment = NULL;
    MI_MEMORY_INFORMATION Info;
    ULONG64 Large, Pages, Base = 0, Other = 0, Small = 0, Copy = 0, Size, Region, Length;
    ULONG64 First, Physical;
    MI_FRAME_NUMBER Locked = MI_FRAME_INVALID;
    MI_PTE Pte;
    ULONG Old;
    NTSTATUS Status;

    WorldCreate(&World, 8192, 4, 8192);
    ProcessCreate(&World, &Parent);
    ProcessCreate(&World, &Peer);
    ProcessCreate(&World, &Child);
    ProcessCreate(&World, &Excluded);
    Large = World.System.Arch->LargePageSize;
    Pages = Large >> PAGE_SHIFT;
    CHECK(NT_SUCCESS(MiSegmentCreateLarge(&World.System, 2 * Large, MI_PROT_READWRITE, NULL, NULL, &Segment)));
    if (Segment == NULL)
        goto Finish;
    CHECK(World.System.CommittedPages == (LONG64)(2 * Pages));
    CHECK(MiSegmentIsResident(Segment, 0, 2 * Large));
    CHECK(!MiSegmentPurge(Segment, 0, 0));
    CHECK(MiSegmentExtend(Segment, 3 * Large) == STATUS_INVALID_PARAMETER);
    Size = 0;
    CHECK(NT_SUCCESS(MiMapView(&Parent, Segment, &Base, 0, &Size, MI_PROT_READWRITE, MI_MEM_LARGE_PAGES)));
    CHECK(Size == 2 * Large && Base % Large == 0);
    CHECK(Parent.CommittedPages == 0 && Parent.PrivatePages == 0 && Parent.ResidentPages == 0);
    CHECK(Parent.LargePages == (LONG64)(2 * Pages));
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Parent, Base, &Info)));
    CHECK(Info.Type == MI_MEM_MAPPED && Info.State == MI_MEM_COMMIT && Info.RegionSize == Size);
    CHECK(MiPtTranslate(&Parent, Base, &First, &Pte));
    CHECK(First % Large == 0 && MiArchPteIsBlock(Pte, World.System.Arch->LargePageLevel));
    CHECK(MiPtTranslate(&Parent, Base + Large - 1, &Physical, NULL) && Physical == First + Large - 1);
    Size = 0;
    CHECK(NT_SUCCESS(MiMapViewEx(&Peer, Segment, &Other, Large, &Size, MI_PROT_READONLY,
                                 MI_MEM_LARGE_PAGES, ~0ULL, MI_PROT_READONLY, FALSE)));
    CHECK(Size == Large && Peer.LargePages == (LONG64)Pages);
    Size = 2 * PAGE_SIZE;
    CHECK(NT_SUCCESS(MiMapViewEx(&Parent, Segment, &Small, Large + MI_ALLOCATION_GRANULARITY, &Size,
                                 MI_PROT_READWRITE, 0, ~0ULL, MI_PROT_READWRITE, FALSE)));
    Size = PAGE_SIZE;
    CHECK(NT_SUCCESS(MiMapViewEx(&Parent, Segment, &Copy, Large + MI_ALLOCATION_GRANULARITY, &Size,
                                 MI_PROT_WRITECOPY, 0, ~0ULL, MI_PROT_READWRITE, FALSE)));
    WorldAttach(&World, 0, &Parent);
    WorldAttach(&World, 1, &Parent);
    WorldAttach(&World, 2, &Peer);
    CHECK(UserRead64(&World, 0, Base + Large + MI_ALLOCATION_GRANULARITY, &Status) == 0 && NT_SUCCESS(Status));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + Large + MI_ALLOCATION_GRANULARITY, 0xDECADE)));
    CHECK(UserRead64(&World, 1, Small, &Status) == 0xDECADE && NT_SUCCESS(Status));
    CHECK(UserRead64(&World, 2, Other + MI_ALLOCATION_GRANULARITY, &Status) == 0xDECADE && NT_SUCCESS(Status));
    CHECK(MiPtTranslate(&Parent, Small, &Physical, &Pte) && !MiArchPteIsBlock(Pte, 0));
    CHECK(NT_SUCCESS(UserWrite64(&World, 1, Copy, 0xABCDEF)));
    CHECK(UserRead64(&World, 1, Small, &Status) == 0xDECADE);
    CHECK(UserRead64(&World, 1, Copy, &Status) == 0xABCDEF);
    Region = Other;
    Length = Large;
    CHECK(MiProtectVirtualMemory(&Peer, &Region, &Length, MI_PROT_READWRITE, &Old) == STATUS_SECTION_PROTECTION);
    Region = Base;
    Length = 0;
    CHECK(MiFreeVirtualMemory(&Parent, &Region, &Length, MI_MEM_RELEASE) == STATUS_UNABLE_TO_DELETE_SECTION);
    Region = Base;
    Length = Large;
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Parent, &Region, &Length, MI_PROT_NOACCESS, &Old)));
    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Parent, &Child)));
    CHECK(Child.VadRoot.NodeCount == 1 && Child.LargePages == (LONG64)(2 * Pages));
    CHECK(Child.CommittedPages == 0 && Child.PrivatePages == 0 && Child.ResidentPages == 0);
    WorldAttach(&World, 3, &Child);
    UserRead64(&World, 3, Base, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(UserRead64(&World, 3, Base + Large + MI_ALLOCATION_GRANULARITY, &Status) == 0xDECADE);
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Parent, &Region, &Length, MI_PROT_READWRITE, &Old)));
    CHECK(Old == MI_PROT_NOACCESS);
    Region = Base + Large;
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Parent, &Region, &Length, MI_PROT_READONLY, &Old)));
    CHECK(UserWrite64(&World, 0, Region + MI_ALLOCATION_GRANULARITY, 1) == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(UserWrite64(&World, 3, Region + MI_ALLOCATION_GRANULARITY, 0xF00D)));
    CHECK(UserRead64(&World, 0, Region + MI_ALLOCATION_GRANULARITY, &Status) == 0xF00D);
    CHECK(MiTrimAddressSpace(&Child, 10000, TRUE) == 0);
    CHECK(NT_SUCCESS(MiLockPages(&Peer, Other + MI_ALLOCATION_GRANULARITY, 1, TRUE, FALSE, &Locked)));
    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Peer, &Excluded)));
    CHECK(Excluded.VadRoot.NodeCount == 0 && Excluded.LargePages == 0);
    CHECK(MiPtCheck(&Parent) == 0 && MiPtCheck(&Peer) == 0 && MiPtCheck(&Child) == 0);
    MiSegmentDereference(Segment);
    Segment = NULL;
    CHECK(NT_SUCCESS(MiUnmapView(&Parent, Base + PAGE_SIZE)));
    CHECK(NT_SUCCESS(MiUnmapView(&Parent, Small)));
    CHECK(NT_SUCCESS(MiUnmapView(&Parent, Copy)));
    CHECK(NT_SUCCESS(MiUnmapView(&Peer, Other)));
    CHECK(NT_SUCCESS(MiUnmapView(&Child, Base)));
    CHECK(World.System.CommittedPages == 0);
    CHECK(World.System.Pfn.Pfn[Locked].ReferenceCount == 1);
    CHECK(*(ULONG64 *)MachineFrame(&World.Machine, (ULONG)Locked) == 0xF00D);
    MiUnlockFrames(&World.System, &Locked, 1, FALSE);
    Locked = MI_FRAME_INVALID;
    CHECK(World.Machine.StaleTlbUses == 0 && World.Machine.BreakBeforeMakeViolations == 0);
Finish:
    ProcessDestroy(&World, &Parent);
    ProcessDestroy(&World, &Peer);
    ProcessDestroy(&World, &Child);
    ProcessDestroy(&World, &Excluded);
    if (Segment != NULL)
        MiSegmentDereference(Segment);
    if (Locked != MI_FRAME_INVALID)
        MiUnlockFrames(&World.System, &Locked, 1, FALSE);
    WorldExpectClean(&World, 8192);
    WorldDestroy(&World);
}

static void
LargeSectionReleased(PVOID Context)
{
    (*(ULONG *)Context)++;
}

static void
LargeSectionFailures(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space, Child;
    PMI_SEGMENT Segment = NULL;
    MI_FILE_OPS Ops = { .Release = LargeSectionReleased };
    ULONG Releases = 0, Held[2048], Count, Keep;
    ULONG64 Large, Available, Base, Size, Before;

    WorldCreate(&World, 2048, 1, 8192);
    ProcessCreate(&World, &Space);
    ProcessCreate(&World, &Child);
    Large = World.System.Arch->LargePageSize;
    Available = MiPfnAvailablePages(&World.System.Pfn);
    CHECK(MiSegmentCreateLarge(&World.System, Large + PAGE_SIZE, MI_PROT_READWRITE, &Ops, &Releases,
                                &Segment) == STATUS_INVALID_PARAMETER);
    CHECK(MiSegmentCreateLarge(&World.System, Large, MI_PROT_WRITECOPY, &Ops, &Releases,
                                &Segment) == STATUS_INVALID_PAGE_PROTECTION);
    World.System.CommitLimit = 1;
    CHECK(MiSegmentCreateLarge(&World.System, Large, MI_PROT_READWRITE, &Ops, &Releases,
                                &Segment) == STATUS_COMMITMENT_LIMIT);
    World.System.CommitLimit = 8192;
    CHECK(MiSegmentCreateLarge(&World.System, 5 * Large, MI_PROT_READWRITE, &Ops, &Releases,
                                &Segment) == STATUS_NO_MEMORY);
    CHECK(Segment == NULL && Releases == 0 && World.System.CommittedPages == 0);
    CHECK(MiPfnAvailablePages(&World.System.Pfn) == Available);
    CHECK(NT_SUCCESS(MiSegmentCreateLarge(&World.System, 2 * Large, MI_PROT_READWRITE, &Ops, &Releases, &Segment)));
    if (Segment == NULL)
        goto Finish;
    for (Keep = 0; Keep < 4; Keep++)
    {
        Base = Keep == 0 ? USER_BASE + PAGE_SIZE : 0;
        Size = Keep == 1 ? Large + PAGE_SIZE : Large;
        CHECK(!NT_SUCCESS(MiMapView(&Space, Segment, &Base, Keep == 2 ? MI_ALLOCATION_GRANULARITY : 0,
                                     &Size, Keep == 3 ? MI_PROT_WRITECOPY : MI_PROT_READWRITE, MI_MEM_LARGE_PAGES)));
        CHECK(Space.VadRoot.NodeCount == 0 && Space.PageTablePages == 0 && Segment->ReferenceCount == 1);
    }
    Before = MiPfnAvailablePages(&World.System.Pfn);
    for (Keep = 0; Keep < 3; Keep++)
    {
        ULONG Frame;

        Count = 0;
        while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
            Held[Count++] = Frame;
        for (ULONG i = 0; i < Keep; i++)
            MiPfnShareDecrement(&World.System.Pfn, Held[--Count], TRUE);
        Base = Large * (PAGE_SIZE / sizeof(MI_PTE)) - Large;
        Size = 2 * Large;
        CHECK(MiMapView(&Space, Segment, &Base, 0, &Size, MI_PROT_READWRITE, MI_MEM_LARGE_PAGES) == STATUS_NO_MEMORY);
        CHECK(Space.VadRoot.NodeCount == 0 && Space.PageTablePages == 0 && Space.LargePages == 0);
        CHECK(Segment->MappedViews == 0 && Segment->ReferenceCount == 1);
        CHECK(Segment->TruncationViews == 0);
        CHECK(MiPfnAvailablePages(&World.System.Pfn) == Keep);
        while (Count != 0)
            MiPfnShareDecrement(&World.System.Pfn, Held[--Count], TRUE);
        CHECK(MiPfnAvailablePages(&World.System.Pfn) == Before);
    }
    Base = Large * (PAGE_SIZE / sizeof(MI_PTE)) - Large;
    Size = 2 * Large;
    CHECK(NT_SUCCESS(MiMapView(&Space, Segment, &Base, 0, &Size, MI_PROT_READWRITE, MI_MEM_LARGE_PAGES)));
    Before = MiPfnAvailablePages(&World.System.Pfn);
    for (Keep = 0; Keep < 3; Keep++)
    {
        ULONG Frame;

        Count = 0;
        while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
            Held[Count++] = Frame;
        for (ULONG i = 0; i < Keep; i++)
            MiPfnShareDecrement(&World.System.Pfn, Held[--Count], TRUE);
        CHECK(MiCloneAddressSpace(&Space, &Child) == STATUS_NO_MEMORY);
        CHECK(Child.VadRoot.NodeCount == 0 && Child.PageTablePages == 0 && Child.LargePages == 0);
        CHECK(Segment->MappedViews == 1 && Segment->ReferenceCount == 2);
        CHECK(Segment->TruncationViews == 1);
        CHECK(MiPfnAvailablePages(&World.System.Pfn) == Keep);
        while (Count != 0)
            MiPfnShareDecrement(&World.System.Pfn, Held[--Count], TRUE);
        CHECK(MiPfnAvailablePages(&World.System.Pfn) == Before);
    }
    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Space, &Child)));
    CHECK(Segment->TruncationViews == 2);
    CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));
    CHECK(NT_SUCCESS(MiUnmapView(&Child, Base)));
    CHECK(Segment->TruncationViews == 0);
    MiSegmentDereferenceAndClose(Segment);
    CHECK(Releases == 1 && World.System.CommittedPages == 0);
    CHECK(MiPfnAvailablePages(&World.System.Pfn) == Available);
Finish:
    ProcessDestroy(&World, &Space);
    ProcessDestroy(&World, &Child);
    WorldExpectClean(&World, 2048);
    WorldDestroy(&World);
}

void
TestLarge(void)
{
    LargeLifecycle();
    LargeFailures();
    LargeSectionLifecycle();
    LargeSectionFailures();
    LargeConcurrent(FALSE);
    LargeConcurrent(TRUE);
}
