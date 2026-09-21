/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_fault_smp.c
 * PURPOSE:     Concurrent page-fault host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"

#define FAULT_WORKERS 4

typedef struct _FAULT_READER
{
    TEST_WORLD *World;
    PMI_ADDRESS_SPACE Space;
    ULONG64 Base;
    ULONG64 Stride;
    ULONG Index;
    ULONG Pages;
    volatile LONG *Ready;
    volatile LONG *Go;
    volatile LONG *Done;
    volatile LONG *Guards;
} FAULT_READER;

static
void *
FaultReader(void *Argument)
{
    FAULT_READER *Reader = Argument;
    ULONG Page;

    MiHostCpu = Reader->Index;
    MachineCpu = Reader->Index;
    MI_ATOMIC_ADD32(Reader->Ready, 1);
    while (!MI_ATOMIC_READ32(Reader->Go))
        sched_yield();

    for (Page = 0; Page < Reader->Pages; Page++)
    {
        ULONG64 Va = Reader->Base + (Page * FAULT_WORKERS + Reader->Index) * Reader->Stride;
        ULONG64 Value = 0xFA000000ULL + (Page * FAULT_WORKERS + Reader->Index);
        MI_MEMORY_INFORMATION Info;
        NTSTATUS Status = MiFault(Reader->Space, Va, MiFaultWrite, TRUE);

        if (Status == STATUS_GUARD_PAGE_VIOLATION)
        {
            MI_ATOMIC_ADD32(Reader->Guards, 1);
            Status = MiFault(Reader->Space, Va, MiFaultWrite, TRUE);
        }
        CHECK(NT_SUCCESS(Status));
        Va += Reader->Index * sizeof(ULONG64);
        CHECK(NT_SUCCESS(UserWrite64(Reader->World, Reader->Index, Va, Value)));
        CHECK(UserRead64(Reader->World, Reader->Index, Va, &Status) == Value && NT_SUCCESS(Status));
        if (Page == 0)
        {
            CHECK(NT_SUCCESS(MiQueryVirtualMemory(Reader->Space, Va, &Info)));
            CHECK(Info.State == MI_MEM_COMMIT && Info.Protect == MI_PROT_READWRITE);
        }
    }

    CHECK(MiHostIrql == 0);
    MI_ATOMIC_ADD32(Reader->Done, 1);
    return NULL;
}

static
ULONG
FaultRunReaders(TEST_WORLD *World, PMI_ADDRESS_SPACE Space, ULONG64 Base, ULONG64 Stride, ULONG Pages)
{
    FAULT_READER Readers[FAULT_WORKERS];
    pthread_t Threads[FAULT_WORKERS];
    volatile LONG Ready = 0, Go = 0, Done = 0, Guards = 0;
    double Deadline;
    ULONG i;

    MI_RW_ACQUIRE_SHARED(&Space->Lock);
    for (i = 0; i < FAULT_WORKERS; i++)
        WorldAttach(World, i, Space);
    for (i = 0; i < FAULT_WORKERS; i++)
    {
        Readers[i] = (FAULT_READER){ World, Space, Base, Stride, i, Pages, &Ready, &Go, &Done, &Guards };
        MI_ASSERT(pthread_create(&Threads[i], NULL, FaultReader, &Readers[i]) == 0);
    }
    while (MI_ATOMIC_READ32(&Ready) != FAULT_WORKERS)
        sched_yield();
    MI_ATOMIC_ADD32(&Go, 1);
    Deadline = NowSeconds() + 2.0;
    while (MI_ATOMIC_READ32(&Done) != FAULT_WORKERS && NowSeconds() < Deadline)
        sched_yield();
    if (MI_ATOMIC_READ32(&Done) != FAULT_WORKERS)
        fprintf(stderr, "shared fault timeout: stride=%llu pages=%u done=%d\n",
                (unsigned long long)Stride, Pages, MI_ATOMIC_READ32(&Done));
    CHECK(MI_ATOMIC_READ32(&Done) == FAULT_WORKERS);
    MI_RW_RELEASE_SHARED(&Space->Lock);

    for (i = 0; i < FAULT_WORKERS; i++)
        MI_ASSERT(pthread_join(Threads[i], NULL) == 0);
    CHECK(MiPtCheck(Space) == 0);
    CHECK(WorldCheck(World) == 0);
    return (ULONG)Guards;
}

static
void
FaultSharedPte(ULONG Mode)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    PMI_SEGMENT Segment = NULL;
    ULONG64 Base = USER_BASE, Size = PAGE_SIZE;
    ULONG i;

    WorldCreate(&World, 256, FAULT_WORKERS, 100000);
    ProcessCreate(&World, &Space);
    if (Mode == 4)
        WorldAttachPageFile(&World, 256);

    if (Mode == 5)
    {
        CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentPageFileBacked, PAGE_SIZE,
                                         MI_PROT_READWRITE, NULL, NULL, NULL, 0, &Segment)));
        CHECK(NT_SUCCESS(MiMapView(&Space, Segment, &Base, 0, &Size, MI_PROT_WRITECOPY, 0)));
    }
    else
    {
        CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                                 MI_PROT_READWRITE | ((Mode == 1) ? MI_PROT_GUARD : 0))));
    }

    if (Mode >= 2 && Mode <= 4)
        CHECK(NT_SUCCESS(MiFault(&Space, Base, Mode == 4 ? MiFaultWrite : MiFaultRead, TRUE)));
    if (Mode == 3 || Mode == 4)
        CHECK(MiTrimAddressSpace(&Space, 1, TRUE) == 1);
    if (Mode == 4)
    {
        ULONG Held[256];
        ULONG Count = 0;
        ULONG Frame;

        CHECK(MiWriteModifiedPages(&World.System, 1) == 1);
        while ((Frame = MiPfnAllocatePage(&World.System.Pfn, MI_ALLOCATE_NO_RECLAIM)) != MI_FRAME_INVALID)
        {
            MI_ASSERT(Count < RTL_NUMBER_OF(Held));
            Held[Count++] = Frame;
        }
        Frame = MiPfnAllocatePage(&World.System.Pfn, 0);
        CHECK(Frame != MI_FRAME_INVALID);
        if (Frame != MI_FRAME_INVALID)
            Held[Count++] = Frame;
        CHECK(MiSoftKind(MiArchPteRead(MiPtLookup(&Space, Base, NULL))) == MiSoftPageFile);
        for (i = 0; i < Count; i++)
            MiPfnFreePage(&World.System.Pfn, Held[i]);
    }

    CHECK(FaultRunReaders(&World, &Space, Base, 0, 1) == (ULONG)(Mode == 1));
    CHECK(Space.PrivatePages == 1 && Space.ResidentPages == 1);
    for (i = 0; i < FAULT_WORKERS; i++)
    {
        NTSTATUS Status;

        CHECK(UserRead64(&World, 0, Base + i * sizeof(ULONG64), &Status) == 0xFA000000ULL + i);
        CHECK(NT_SUCCESS(Status));
    }
    if (Mode == 5)
    {
        ULONG64 Alias = 0;
        NTSTATUS Status;

        CHECK(Space.CopyOnWriteFaults == 1);
        CHECK(NT_SUCCESS(MiMapView(&Space, Segment, &Alias, 0, &Size, MI_PROT_READONLY, 0)));
        CHECK(UserRead64(&World, 0, Alias, &Status) == 0 && NT_SUCCESS(Status));
    }
    if (Mode == 4)
        CHECK(Space.PageFileFaults == 1);

    for (i = 0; i < FAULT_WORKERS; i++)
        WorldAttach(&World, i, NULL);
    ProcessDestroy(&World, &Space);
    if (Segment != NULL)
        MiSegmentDereference(Segment);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
}

static
void
FaultParallelTables(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Base = USER_BASE;
    ULONG64 Span;
    ULONG64 Size;
    ULONG i;

    WorldCreate(&World, 2048, FAULT_WORKERS, 1000000);
    ProcessCreate(&World, &Space);
    Span = 1ULL << World.System.Arch->Level[1].Shift;
    Size = Span * FAULT_WORKERS * 64;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Base, &Size,
                                             MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(FaultRunReaders(&World, &Space, Base, Span, 64) == 0);
    CHECK(Space.PrivatePages == FAULT_WORKERS * 64 && Space.ResidentPages == FAULT_WORKERS * 64);
    for (i = 0; i < FAULT_WORKERS; i++)
        WorldAttach(&World, i, NULL);
    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 2048);
    WorldDestroy(&World);
}

void
TestFaultSmp(void)
{
    ULONG Mode;

    for (Mode = 0; Mode < 6; Mode++)
        FaultSharedPte(Mode);
    FaultParallelTables();
}
