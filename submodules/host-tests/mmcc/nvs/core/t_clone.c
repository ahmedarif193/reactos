/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_clone.c
 * PURPOSE:     Address-space cloning host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include <nvs/include/misys.h>

static void
CloneBasic(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Parent, Child;
    ULONG64 Base = USER_BASE, Size = 4 * PAGE_SIZE;
    ULONG64 ParentPa, ChildPa;
    NTSTATUS Status;

    WorldCreate(&World, 512, 2, 1024);
    World.Machine.StrictTlb = TRUE;
    ProcessCreate(&World, &Parent);
    ProcessCreate(&World, &Child);
    WorldAttach(&World, 0, &Parent);
    WorldAttach(&World, 1, &Child);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Parent, &Base, &Size,
                                             MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base, 0x11223344)));
    Status = MiCloneAddressSpace(&Parent, &Child);
    CHECK(Status == STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CHECK(UserRead64(&World, 1, Base, &Status) == 0x11223344 && NT_SUCCESS(Status));
        CHECK(MiPtTranslate(&Parent, Base, &ParentPa, NULL));
        CHECK(MiPtTranslate(&Child, Base, &ChildPa, NULL));
        CHECK(ParentPa == ChildPa);
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base, 0x55667788)));
        CHECK(UserRead64(&World, 1, Base, &Status) == 0x11223344);
        CHECK(NT_SUCCESS(UserWrite64(&World, 1, Base + PAGE_SIZE, 0xABCDEF)));
        CHECK(UserRead64(&World, 0, Base + PAGE_SIZE, &Status) == 0);
        CHECK(Parent.CopyOnWriteFaults == 1);
        CHECK(Parent.CommittedPages == 4 && Child.CommittedPages == 4);
    }
    WorldAttach(&World, 0, NULL);
    WorldAttach(&World, 1, NULL);
    ProcessDestroy(&World, &Child);
    ProcessDestroy(&World, &Parent);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

static void
CloneGenerations(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Space[4];
    ULONG64 Base = USER_BASE, Size = 8 * PAGE_SIZE;
    ULONG64 Va, Length;
    ULONG i, Old;
    NTSTATUS Status;
    MI_MEMORY_INFORMATION Info;

    WorldCreate(&World, 512, 4, 1024);
    World.Machine.StrictTlb = TRUE;
    WorldAttachPageFile(&World, 1024);
    for (i = 0; i < 4; i++)
    {
        ProcessCreate(&World, &Space[i]);
    }
    for (i = 0; i < 4; i++)
        WorldAttach(&World, i, &Space[i]);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space[0], &Base, &Size,
                                             MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    for (i = 0; i < 8; i++)
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + i * PAGE_SIZE, 0x100 + i)));
    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Space[0], &Space[1])));
    CHECK(NT_SUCCESS(UserWrite64(&World, 1, Base, 0x200)));
    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Space[1], &Space[2])));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + PAGE_SIZE, 0x300)));
    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Space[0], &Space[3])));
    CHECK(UserRead64(&World, 2, Base, &Status) == 0x200);
    CHECK(UserRead64(&World, 3, Base, &Status) == 0x100);
    CHECK(UserRead64(&World, 3, Base + PAGE_SIZE, &Status) == 0x300);
    CHECK(UserRead64(&World, 2, Base + PAGE_SIZE, &Status) == 0x101);

    Va = Base + 2 * PAGE_SIZE; Length = PAGE_SIZE;
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Space[2], &Va, &Length, MI_PROT_READONLY, &Old)));
    CHECK(Old == MI_PROT_READWRITE);
    CHECK(UserWrite64(&World, 2, Va, 0x400) == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Space[2], &Va, &Length, MI_PROT_READWRITE | MI_PROT_GUARD, &Old)));
    CHECK(UserWrite64(&World, 2, Va, 0x400) == STATUS_GUARD_PAGE_VIOLATION);
    CHECK(NT_SUCCESS(UserWrite64(&World, 2, Va, 0x400)));
    CHECK(UserRead64(&World, 1, Va, &Status) == 0x102);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space[3], Base, &Info)));
    CHECK(Info.Type == MI_MEM_PRIVATE && Info.Protect == MI_PROT_READWRITE);

    Va = Base + 3 * PAGE_SIZE; Length = 2 * PAGE_SIZE;
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Space[1], &Va, &Length, MI_MEM_DECOMMIT)));
    UserRead64(&World, 1, Va, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(UserRead64(&World, 2, Va, &Status) == 0x103);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space[1], &Va, &Length, MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(UserRead64(&World, 1, Va, &Status) == 0 && NT_SUCCESS(Status));

    for (i = 0; i < 4; i++)
    {
        WorldAttach(&World, i, NULL);
        ProcessDestroy(&World, &Space[i]);
        CHECK(WorldCheck(&World) == 0);
    }
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

static void
ClonePagingAndMdl(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Parent, Child, Grandchild;
    ULONG64 Base = USER_BASE, Size = 16 * PAGE_SIZE;
    ULONG Held[512], Count = 0, Frame, i;
    MI_FRAME_NUMBER Locked;
    NTSTATUS Status;

    WorldCreate(&World, 512, 3, 1024);
    World.Machine.StrictTlb = TRUE;
    WorldAttachPageFile(&World, 1024);
    ProcessCreate(&World, &Parent);
    ProcessCreate(&World, &Child);
    ProcessCreate(&World, &Grandchild);
    WorldAttach(&World, 0, &Parent);
    WorldAttach(&World, 1, &Child);
    WorldAttach(&World, 2, &Grandchild);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Parent, &Base, &Size,
                                             MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    for (i = 0; i < 16; i++)
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + i * PAGE_SIZE, 0x1000 + i)));
    CHECK(NT_SUCCESS(MiLockPages(&Parent, Base, 1, TRUE, TRUE, &Locked)));
    CHECK(MiTrimAddressSpace(&Parent, 16, TRUE) == 16);
    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Parent, &Child)));
    *(ULONG64 *)MachineFrame(&World.Machine, (ULONG)Locked) = 0x2000;
    CHECK(UserRead64(&World, 0, Base, &Status) == 0x2000);
    CHECK(UserRead64(&World, 1, Base, &Status) == 0x1000);
    MiUnlockFrames(&World.System, &Locked, 1, TRUE);
    CHECK(MiTrimAddressSpace(&Parent, 16, TRUE) == 1);
    CHECK(MiTrimAddressSpace(&Child, 16, TRUE) == 1);
    while (MiWriteModifiedPages(&World.System, 1024) != 0)
        ;
    while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
        Held[Count++] = Frame;
    for (i = 0; i < Count; i++)
        MiPfnShareDecrement(&World.System.Pfn, Held[i], TRUE);
    CHECK(World.Paging.PageFile.SlotsInUse == 17);
    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Parent, &Grandchild)));
    World.Paging.FailReads = 1;
    UserRead64(&World, 1, Base + PAGE_SIZE, &Status);
    CHECK(Status == STATUS_IN_PAGE_ERROR);
    World.Paging.FailReads = 0;
    World.Paging.ProbeLock = &Child.Lock;
    for (i = 0; i < 16; i++)
        CHECK(UserRead64(&World, 1, Base + i * PAGE_SIZE, &Status) == 0x1000 + i && NT_SUCCESS(Status));
    CHECK(World.Paging.IoUnderLock == 0);
    World.Paging.ProbeLock = NULL;
    CHECK(UserRead64(&World, 2, Base, &Status) == 0x2000);
    CHECK(NT_SUCCESS(UserWrite64(&World, 2, Base + PAGE_SIZE, 0x3000)));
    CHECK(UserRead64(&World, 1, Base + PAGE_SIZE, &Status) == 0x1001);

    CHECK(NT_SUCCESS(MiLockPages(&Child, Base + 2 * PAGE_SIZE, 1, TRUE, FALSE, &Locked)));
    WorldAttach(&World, 0, NULL);
    WorldAttach(&World, 1, NULL);
    WorldAttach(&World, 2, NULL);
    ProcessDestroy(&World, &Parent);
    ProcessDestroy(&World, &Grandchild);
    ProcessDestroy(&World, &Child);
    CHECK(*(ULONG64 *)MachineFrame(&World.Machine, (ULONG)Locked) == 0x1002);
    MiUnlockFrames(&World.System, &Locked, 1, FALSE);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

static void
CloneSectionsAndRollback(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Parent, Child;
    PMI_SEGMENT Segment;
    ULONG64 Shared = USER_BASE, Copy = USER_BASE + 0x10000, Omit = USER_BASE + 0x20000;
    ULONG64 Private = USER_BASE + 0x1000000, Size;
    ULONG Held[512], Count = 0, Frame, i;
    NTSTATUS Status;
    LONG64 Commit;

    WorldCreate(&World, 512, 2, 1024);
    World.Machine.StrictTlb = TRUE;
    ProcessCreate(&World, &Parent);
    ProcessCreate(&World, &Child);
    WorldAttach(&World, 0, &Parent);
    WorldAttach(&World, 1, &Child);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentPageFileBacked, 4 * PAGE_SIZE,
                                     MI_PROT_READWRITE, NULL, NULL, NULL, 0, &Segment)));
    Size = 4 * PAGE_SIZE;
    CHECK(NT_SUCCESS(MiMapView(&Parent, Segment, &Shared, 0, &Size, MI_PROT_READWRITE, 0)));
    CHECK(NT_SUCCESS(MiMapView(&Parent, Segment, &Copy, 0, &Size, MI_PROT_WRITECOPY, 0)));
    CHECK(NT_SUCCESS(MiMapViewEx(&Parent, Segment, &Omit, 0, &Size, MI_PROT_READWRITE, 0, ~0ULL, 0, FALSE)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Shared, 10)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Copy, 20)));
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Parent, &Private, &Size,
                                             MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Private, 30)));
    Commit = World.System.CommittedPages;
    World.System.CommitLimit = Commit;
    CHECK(MiCloneAddressSpace(&Parent, &Child) == STATUS_COMMITMENT_LIMIT);
    CHECK(Child.VadRoot.NodeCount == 0 && Parent.CloneRoot.NodeCount == 0);
    CHECK(World.System.CommittedPages == Commit);
    World.System.CommitLimit = 1024;
    while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
        Held[Count++] = Frame;
    CHECK(Count > 2);
    for (i = 0; i < 2; i++)
        MiPfnShareDecrement(&World.System.Pfn, Held[--Count], TRUE);
    CHECK(MiCloneAddressSpace(&Parent, &Child) == STATUS_NO_MEMORY);
    CHECK(Child.VadRoot.NodeCount == 0 && Child.PageTablePages == 0 && Child.CommittedPages == 0);
    CHECK(Parent.CloneRoot.NodeCount == 0 && World.System.CommittedPages == Commit);
    for (i = 0; i < Count; i++)
        MiPfnShareDecrement(&World.System.Pfn, Held[i], TRUE);
    CHECK(UserRead64(&World, 0, Private, &Status) == 30);
    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Parent, &Child)));
    CHECK(UserRead64(&World, 1, Copy, &Status) == 20);
    UserRead64(&World, 1, Omit, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Shared, 40)));
    CHECK(UserRead64(&World, 1, Shared, &Status) == 40);
    CHECK(NT_SUCCESS(UserWrite64(&World, 1, Copy, 50)));
    CHECK(UserRead64(&World, 0, Copy, &Status) == 20);
    CHECK(UserRead64(&World, 0, Shared, &Status) == 40);
    CHECK(NT_SUCCESS(MiUnmapView(&Parent, Copy)));
    CHECK(UserRead64(&World, 1, Copy, &Status) == 50);
    WorldAttach(&World, 0, NULL);
    WorldAttach(&World, 1, NULL);
    ProcessDestroy(&World, &Parent);
    ProcessDestroy(&World, &Child);
    MiSegmentDereference(Segment);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

typedef struct _CLONE_THREAD
{
    TEST_WORLD *World;
    ULONG Cpu;
} CLONE_THREAD;

static void *
CloneWriter(void *Argument)
{
    CLONE_THREAD *Thread = Argument;
    ULONG i;
    NTSTATUS Status;

    for (i = 0; i < 128; i++)
    {
        ULONG64 Va = USER_BASE + i * PAGE_SIZE;
        ULONG64 Value = ((ULONG64)Thread->Cpu << 32) | i;

        CHECK(UserRead64(Thread->World, Thread->Cpu, Va, &Status) == i);
        CHECK(NT_SUCCESS(UserWrite64(Thread->World, Thread->Cpu, Va, Value)));
        CHECK(UserRead64(Thread->World, Thread->Cpu, Va, &Status) == Value);
    }
    return NULL;
}

static void
CloneSmp(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Space[4];
    pthread_t Threads[4];
    CLONE_THREAD Args[4];
    ULONG64 Base = USER_BASE, Size = 128 * PAGE_SIZE;
    ULONG i;

    WorldCreate(&World, 2048, 4, 4096);
    World.Machine.StrictTlb = TRUE;
    for (i = 0; i < 4; i++)
    {
        ProcessCreate(&World, &Space[i]);
    }
    for (i = 0; i < 4; i++)
        WorldAttach(&World, i, &Space[i]);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space[0], &Base, &Size,
                                             MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    for (i = 0; i < 128; i++)
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + i * PAGE_SIZE, i)));
    for (i = 1; i < 4; i++)
        CHECK(NT_SUCCESS(MiCloneAddressSpace(&Space[0], &Space[i])));
    for (i = 0; i < 4; i++)
    {
        Args[i].World = &World;
        Args[i].Cpu = i;
        CHECK(pthread_create(&Threads[i], NULL, CloneWriter, &Args[i]) == 0);
    }
    for (i = 0; i < 4; i++)
        CHECK(pthread_join(Threads[i], NULL) == 0);
    for (i = 0; i < 4; i++)
    {
        CHECK(Space[i].CloneRoot.NodeCount == 0 && Space[i].CopyOnWriteFaults == 128);
        WorldAttach(&World, i, NULL);
        ProcessDestroy(&World, &Space[i]);
    }
    WorldExpectClean(&World, 2048);
    WorldDestroy(&World);
}

void
TestClone(void)
{
    CloneBasic();
    CloneGenerations();
    ClonePagingAndMdl();
    CloneSectionsAndRollback();
    CloneSmp();
}
