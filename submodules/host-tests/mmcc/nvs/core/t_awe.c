/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_awe.c
 * PURPOSE:     Address Windowing Extensions host-native tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include <nvs/include/misys.h>

static void
AweLifecycle(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space, Foreign;
    MI_MEMORY_INFORMATION Info;
    ULONG64 Base = 0, Window = 0, ForeignBase = 0, Size = 4 * PAGE_SIZE;
    ULONG64 Addresses[3];
    MI_FRAME_NUMBER Frames[6], Remap[3], Locked;
    ULONG Count = 6, Old, Cpu;
    NTSTATUS Status;

    WorldCreate(&World, 512, 4, 512);
    ProcessCreate(&World, &Space);
    Status = MiAllocateVirtualMemory(&Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_PHYSICAL, MI_PROT_READWRITE);
    CHECK(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base, &Info)));
        CHECK(Info.State == MI_MEM_RESERVE && Info.Type == MI_MEM_PRIVATE);
        UserRead64(&World, 0, Base, &Status);
        CHECK(Status == STATUS_ACCESS_VIOLATION);
    }
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Window, &Size, MI_MEM_RESERVE | MI_MEM_PHYSICAL,
                                             MI_PROT_READWRITE)));
    ProcessCreate(&World, &Foreign);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Foreign, &ForeignBase, &Size, MI_MEM_RESERVE | MI_MEM_PHYSICAL,
                                             MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(MiAweAllocatePages(&Space, &Count, Frames)) && Count == 6);
    CHECK(Space.AwePages == 6 && World.System.AwePages == 6 && Space.AweRoot.NodeCount == 6);
    CHECK(Space.CommittedPages == 0 && Space.ResidentPages == 0 && Space.PrivatePages == 0);
    CHECK(!NT_SUCCESS(MiAweMapPages(&Foreign, ForeignBase, NULL, 1, Frames)));
    CHECK(NT_SUCCESS(MiAweMapPages(&Space, Base, NULL, 3, Frames)));
    for (Cpu = 0; Cpu < 4; Cpu++)
    {
        WorldAttach(&World, Cpu, &Space);
        CHECK(UserRead64(&World, Cpu, Base, &Status) == 0 && NT_SUCCESS(Status));
        CHECK(UserRead64(&World, Cpu, Base + PAGE_SIZE, &Status) == 0 && NT_SUCCESS(Status));
    }
    for (Count = 0; Count < 3; Count++)
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + Count * PAGE_SIZE, 0x100 + Count)));
    Remap[0] = Frames[0];
    Remap[1] = Frames[0];
    CHECK(!NT_SUCCESS(MiAweMapPages(&Space, Base, NULL, 2, Remap)));
    CHECK(UserRead64(&World, 1, Base + PAGE_SIZE, &Status) == 0x101);
    Remap[1] = Space.RootFrame;
    CHECK(!NT_SUCCESS(MiAweMapPages(&Space, Base, NULL, 2, Remap)));
    CHECK(UserRead64(&World, 1, Base, &Status) == 0x100);
    CHECK(!NT_SUCCESS(MiAweMapPages(&Space, Window, NULL, 1, Frames)));
    Remap[0] = Frames[2];
    Remap[1] = Frames[1];
    Remap[2] = Frames[0];
    CHECK(NT_SUCCESS(MiAweMapPages(&Space, Base, NULL, 3, Remap)));
    for (Cpu = 0; Cpu < 4; Cpu++)
    {
        CHECK(UserRead64(&World, Cpu, Base, &Status) == 0x102);
        CHECK(UserRead64(&World, Cpu, Base + 2 * PAGE_SIZE, &Status) == 0x100);
    }
    Addresses[0] = Base + 3 * PAGE_SIZE;
    Addresses[1] = Window + 2 * PAGE_SIZE;
    Addresses[2] = Base;
    Remap[0] = Frames[3];
    Remap[1] = Frames[4];
    Remap[2] = 0;
    CHECK(NT_SUCCESS(MiAweMapPages(&Space, 0, Addresses, 3, Remap)));
    CHECK(UserRead64(&World, 2, Addresses[0], &Status) == 0 && NT_SUCCESS(Status));
    CHECK(NT_SUCCESS(UserWrite64(&World, 2, Addresses[1], 0xBEEF)));
    UserRead64(&World, 2, Base, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    Addresses[2] = Addresses[1];
    CHECK(!NT_SUCCESS(MiAweMapPages(&Space, 0, Addresses, 3, NULL)));
    CHECK(UserRead64(&World, 3, Addresses[1], &Status) == 0xBEEF);
    Addresses[2] = Base + 1;
    CHECK(!NT_SUCCESS(MiAweMapPages(&Space, 0, Addresses, 3, NULL)));
    CHECK(UserRead64(&World, 3, Addresses[1], &Status) == 0xBEEF);
    CHECK(MiTrimAddressSpace(&Space, 100, TRUE) == 0);
    CHECK(!NT_SUCCESS(MiProtectVirtualMemory(&Space, &Base, &Size, MI_PROT_READONLY, &Old)));
    CHECK(!NT_SUCCESS(MiFreeVirtualMemory(&Space, &Base, &Size, MI_MEM_DECOMMIT)));
    CHECK(!NT_SUCCESS(MiFreeVirtualMemory(&Space, &Base, &Size, MI_MEM_RELEASE)));
    CHECK(NT_SUCCESS(MiLockPages(&Space, Base + PAGE_SIZE, 1, TRUE, FALSE, &Locked)));
    CHECK(Locked == Frames[1]);
    Count = 1;
    CHECK(NT_SUCCESS(MiAweFreePages(&Space, &Count, &Frames[1])) && Count == 1);
    UserRead64(&World, 1, Base + PAGE_SIZE, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(World.PfnArray[Locked].ReferenceCount == 1 && World.PfnArray[Locked].ShareCount == 0);
    MiUnlockFrames(&World.System, &Locked, 1, FALSE);
    CHECK(!NT_SUCCESS(MiAweMapPages(&Space, Base, NULL, 1, &Frames[1])));
    Remap[0] = Frames[0];
    Remap[1] = Space.RootFrame;
    Remap[2] = Frames[2];
    Count = 3;
    CHECK(MiAweFreePages(&Space, &Count, Remap) == STATUS_INVALID_PARAMETER && Count == 1);
    Remap[0] = Remap[1] = Frames[2];
    Count = 2;
    CHECK(MiAweFreePages(&Space, &Count, Remap) == STATUS_INVALID_PARAMETER && Count == 1);
    CHECK(Space.AwePages == 3 && MiVadCheck(&Space.AweRoot) == 0);
    Size = 0;
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Space, &Window, &Size, MI_MEM_RELEASE)));
    CHECK(Space.AwePages == 3);
    CHECK(NT_SUCCESS(MiAweMapPages(&Space, Base, NULL, 1, &Frames[4])));
    CHECK(UserRead64(&World, 0, Base, &Status) == 0xBEEF);
    CHECK(MiPtCheck(&Space) == 0 && World.Machine.StaleTlbUses == 0);
    ProcessDestroy(&World, &Foreign);
    ProcessDestroy(&World, &Space);
    CHECK(World.System.AwePages == 0);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

static void
AweExhaustion(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Base = 0, Size = PAGE_SIZE, Available;
    ULONG Held[128], HeldCount = 0, Frame, Count = 1;
    MI_FRAME_NUMBER Page, Extra[4];

    WorldCreate(&World, 128, 1, 1);
    ProcessCreate(&World, &Space);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_PHYSICAL,
                                             MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(MiAweAllocatePages(&Space, &Count, &Page)));
    while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
        Held[HeldCount++] = Frame;
    for (Count = 0; Count < 2; Count++)
        MiPfnShareDecrement(&World.System.Pfn, Held[--HeldCount], TRUE);
    Available = MiPfnAvailablePages(&World.System.Pfn);
    CHECK(MiAweMapPages(&Space, Base, NULL, 1, &Page) == STATUS_NO_MEMORY);
    CHECK(Space.PageTablePages == 0 && MiPfnAvailablePages(&World.System.Pfn) == Available);
    CHECK(MiPtCheck(&Space) == 0 && Space.AwePages == 1);
    Count = 4;
    CHECK(NT_SUCCESS(MiAweAllocatePages(&Space, &Count, Extra)) && Count == 2);
    CHECK(Space.AwePages == 3 && Space.CommittedPages == 0);
    CHECK(NT_SUCCESS(MiAweFreePages(&Space, &Count, Extra)) && Count == 2);
    while (HeldCount != 0)
        MiPfnShareDecrement(&World.System.Pfn, Held[--HeldCount], TRUE);
    CHECK(NT_SUCCESS(MiAweMapPages(&Space, Base, NULL, 1, &Page)));
    {
        ULONG64 Other = Base + (1ULL << 30), Addresses[2], TablePages;
        NTSTATUS Status;

        Size = PAGE_SIZE;
        Count = 2;
        CHECK(NT_SUCCESS(MiAweAllocatePages(&Space, &Count, Extra)) && Count == 2);
        CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Other, &Size,
                        MI_MEM_RESERVE | MI_MEM_PHYSICAL, MI_PROT_READWRITE)));
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base, 0xD3D3)));
        while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
            Held[HeldCount++] = Frame;
        MiPfnShareDecrement(&World.System.Pfn, Held[--HeldCount], TRUE);
        Available = MiPfnAvailablePages(&World.System.Pfn);
        TablePages = Space.PageTablePages;
        Addresses[0] = Base;
        Addresses[1] = Other;
        CHECK(MiAweMapPages(&Space, 0, Addresses, 2, Extra) == STATUS_NO_MEMORY);
        CHECK(UserRead64(&World, 0, Base, &Status) == 0xD3D3 && NT_SUCCESS(Status));
        CHECK((ULONG64)Space.PageTablePages == TablePages && MiPtCheck(&Space) == 0);
        CHECK(MiPfnAvailablePages(&World.System.Pfn) == Available);
        while (HeldCount != 0)
            MiPfnShareDecrement(&World.System.Pfn, Held[--HeldCount], TRUE);
    }
    ProcessDestroy(&World, &Space);
    CHECK(World.System.AwePages == 0);
    WorldExpectClean(&World, 128);
    WorldDestroy(&World);
}

typedef struct _AWE_WORKER
{
    TEST_WORLD *World;
    PMI_ADDRESS_SPACE Space;
    ULONG Cpu;
} AWE_WORKER;

static void *
AweWorker(void *Argument)
{
    AWE_WORKER *Worker = Argument;
    ULONG64 Base = 0, Size = 2 * PAGE_SIZE;
    MI_FRAME_NUMBER Frames[2], Swapped[2];
    ULONG Count = 2, i;
    NTSTATUS Status;

    MiHostCpu = Worker->Cpu;
    WorldAttach(Worker->World, Worker->Cpu, Worker->Space);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(Worker->Space, &Base, &Size,
                    MI_MEM_RESERVE | MI_MEM_PHYSICAL, MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(MiAweAllocatePages(Worker->Space, &Count, Frames)) && Count == 2);
    Swapped[0] = Frames[1];
    Swapped[1] = Frames[0];
    for (i = 0; i < 32; i++)
    {
        CHECK(NT_SUCCESS(MiAweMapPages(Worker->Space, Base, NULL, 2, Frames)));
        CHECK(NT_SUCCESS(UserWrite64(Worker->World, Worker->Cpu, Base, i)));
        CHECK(NT_SUCCESS(UserWrite64(Worker->World, Worker->Cpu, Base + PAGE_SIZE, 0x1000 + i)));
        CHECK(NT_SUCCESS(MiAweMapPages(Worker->Space, Base, NULL, 2, Swapped)));
        CHECK(UserRead64(Worker->World, Worker->Cpu, Base, &Status) == 0x1000 + i);
        CHECK(UserRead64(Worker->World, Worker->Cpu, Base + PAGE_SIZE, &Status) == i);
    }
    CHECK(NT_SUCCESS(MiAweFreePages(Worker->Space, &Count, Frames)));
    Size = 0;
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(Worker->Space, &Base, &Size, MI_MEM_RELEASE)));
    WorldAttach(Worker->World, Worker->Cpu, NULL);
    return NULL;
}

static void
AweConcurrent(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    AWE_WORKER Workers[4];
    pthread_t Threads[4];
    ULONG i;

    WorldCreate(&World, 512, 4, 512);
    ProcessCreate(&World, &Space);
    for (i = 0; i < 4; i++)
    {
        Workers[i] = (AWE_WORKER){ &World, &Space, i };
        MI_ASSERT(pthread_create(&Threads[i], NULL, AweWorker, &Workers[i]) == 0);
    }
    for (i = 0; i < 4; i++)
        MI_ASSERT(pthread_join(Threads[i], NULL) == 0);
    CHECK(Space.AwePages == 0 && Space.AweRoot.NodeCount == 0 && Space.PageTablePages == 0);
    CHECK(World.Machine.StaleTlbUses == 0 && World.Machine.BreakBeforeMakeViolations == 0);
    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

void
TestAwe(void)
{
    AweLifecycle();
    AweExhaustion();
    AweConcurrent();
}
