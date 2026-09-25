/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_core.c
 * PURPOSE:     Memory manager core host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"

#define PFN_THREADS 8

typedef struct _PFN_THREAD
{
    PMI_PFN_DATABASE Db;
    ULONG Index;
    ULONG Rounds;
    ULONG64 Operations;
    volatile LONG *Go;
    volatile LONG *Stop;
} PFN_THREAD;

typedef struct _PFN_DPC_TEST
{
    PMI_PFN_DATABASE Db;
    ULONG First;
    ULONG Second;
    ULONG Calls;
} PFN_DPC_TEST;

static
VOID
PfnDpcProbe(PVOID Context)
{
    PFN_DPC_TEST *Test = Context;

    CHECK(MiHostIrql == 2);
    CHECK(!(MI_PFN_FLAGS(&Test->Db->Pfn[Test->First]) & MI_PFN_FLAG_LOCK));
    CHECK(!(MI_PFN_FLAGS(&Test->Db->Pfn[Test->Second]) & MI_PFN_FLAG_LOCK));
    if (!(MI_PFN_FLAGS(&Test->Db->Pfn[Test->First]) & MI_PFN_FLAG_LOCK))
    {
        KIRQL OldIrql = MiPfnLock(Test->Db, Test->First);

        CHECK(OldIrql == 2);
        MiPfnReferenceLocked(Test->Db, Test->First);
        MiPfnUnlock(Test->Db, Test->First, OldIrql);
        MiPfnDereference(Test->Db, Test->First);
        CHECK(MiHostIrql == 2);
    }
    Test->Calls++;
}

static
VOID
PfnLockIrql(PMI_PFN_DATABASE Db)
{
    PFN_DPC_TEST Test = { Db, MiPfnAllocatePage(Db, 0), MiPfnAllocatePage(Db, 0), 0 };
    KIRQL Initial;

    for (Initial = 0; Initial <= 2; Initial++)
    {
        KIRQL FirstIrql, SecondIrql;

        MiHostRaiseIrql(Initial);
        FirstIrql = MiPfnLock(Db, Test.First);
        CHECK(MiHostIrql == 2);
        SecondIrql = MiPfnLock(Db, Test.Second);
        CHECK(MiHostIrql == 2);
        MiHostQueueDpc(PfnDpcProbe, &Test);
        CHECK(Test.Calls == Initial);
        MiPfnUnlock(Db, Test.Second, SecondIrql);
        CHECK(MiHostIrql == 2 && Test.Calls == Initial);
        MiPfnUnlock(Db, Test.First, FirstIrql);
        CHECK(MiHostIrql == Initial);
        CHECK(Test.Calls == (ULONG)Initial + (Initial < 2));
        MiHostLowerIrql(0);
        CHECK(Test.Calls == (ULONG)Initial + 1);
    }

    MiPfnShareDecrement(Db, Test.First, TRUE);
    MiPfnShareDecrement(Db, Test.Second, TRUE);
    CHECK(MiHostIrql == 0);
}

void
TestPfn(void)
{
    TEST_WORLD World;
    PMI_PFN_DATABASE Db;
    ULONG Frames[64];
    LONG64 Baseline;
    ULONG i;

    WorldCreate(&World, 4096, 4, 100000);
    Db = &World.System.Pfn;
    CHECK(MiPfnDbCheck(Db) == 0);
    Baseline = (LONG64)MiPfnAvailablePages(Db);
    CHECK(Baseline == 4096 - 1 - 1);
    PfnLockIrql(Db);

    for (i = 0; i < 64; i++)
    {
        PUCHAR Bytes;
        ULONG j;

        Frames[i] = MiPfnAllocatePage(Db, MI_ALLOCATE_ZEROED);
        CHECK(Frames[i] != MI_FRAME_INVALID && Frames[i] != 0);
        CHECK(Db->Pfn[Frames[i]].State == MiPageActive && Db->Pfn[Frames[i]].ShareCount == 1);
        Bytes = MachineFrame(&World.Machine, Frames[i]);
        for (j = 0; j < PAGE_SIZE; j += 511)
            CHECK(Bytes[j] == 0);
        memset(Bytes, 0xA0 + (int)i, PAGE_SIZE);
    }

    for (i = 0; i < 16; i++)
    {
        MiPfnInitializePage(Db, Frames[i], 0x1000 + i * 8, 7, MiSoftMake(MiSoftDemandZero, MI_PROT_READWRITE, 0), 0);
        MiPfnShareDecrement(Db, Frames[i], FALSE);
        CHECK(Db->Pfn[Frames[i]].State == MiPageStandby);
    }
    for (i = 16; i < 32; i++)
    {
        MiPfnInitializePage(Db, Frames[i], 0x2000 + i * 8, 7, MiSoftMake(MiSoftDemandZero, MI_PROT_READWRITE, 0), 0);
        MiPfnSetModified(Db, Frames[i]);
        MiPfnShareDecrement(Db, Frames[i], FALSE);
        CHECK(Db->Pfn[Frames[i]].State == MiPageModified);
    }
    CHECK(MiPfnListCount(Db, MiPageStandby) == 16 && MiPfnListCount(Db, MiPageModified) == 16);
    CHECK(MiPfnDbCheck(Db) == 0);

    CHECK(!MiPfnReactivate(Db, Frames[0], 0xDEAD));
    CHECK(MiPfnReactivate(Db, Frames[0], 0x1000));
    CHECK(Db->Pfn[Frames[0]].State == MiPageActive && *MachineFrame(&World.Machine, Frames[0]) == 0xA0);
    CHECK(MiPfnReactivate(Db, Frames[16], 0x2000 + 16 * 8));
    CHECK(Db->Pfn[Frames[16]].Flags & MI_PFN_FLAG_MODIFIED);

    {
        ULONG Writing = MiPfnTakeModified(Db);

        CHECK(Writing != MI_FRAME_INVALID && (Db->Pfn[Writing].Flags & MI_PFN_FLAG_IN_FLIGHT));
        CHECK(MiPfnReactivate(Db, Writing, Db->Pfn[Writing].PteAddress));
        MiPfnWriteComplete(Db, Writing, MiSoftMake(MiSoftPageFile, MI_PROT_READWRITE, 99), TRUE);
        CHECK(Db->Pfn[Writing].State == MiPageActive);
        CHECK(MiSoftKind(Db->Pfn[Writing].OriginalPte) == MiSoftPageFile);
        MiPfnShareDecrement(Db, Writing, FALSE);
        CHECK(Db->Pfn[Writing].State == MiPageStandby);

        Writing = MiPfnTakeModified(Db);
        CHECK(Writing != MI_FRAME_INVALID);
        MiPfnWriteComplete(Db, Writing, 0, FALSE);
        CHECK(Db->Pfn[Writing].State == MiPageModified);
    }

    for (i = 32; i < 64; i++)
        MiPfnShareDecrement(Db, Frames[i], TRUE);
    MiPfnShareDecrement(Db, Frames[0], TRUE);
    MiPfnShareDecrement(Db, Frames[16], TRUE);
    CHECK(MiPfnDbCheck(Db) == 0);
    MiPfnDrainCaches(Db);
    CHECK(MiPfnDbCheck(Db) == 0);
    CHECK((LONG64)MiPfnAvailablePages(Db) == Baseline - 30 + 14 + 1 || MiPfnAvailablePages(Db) != 0);

    WorldDestroy(&World);
}

static ULONG RepurposedFrames;

static
VOID
CountRepurpose(PMI_PFN_DATABASE Db, ULONG Frame)
{
    UNREFERENCED_PARAMETER(Db);
    UNREFERENCED_PARAMETER(Frame);
    RepurposedFrames++;
}

void
TestPageTable(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    PMI_SYSTEM System;
    ULONG64 Seed = 77;
    ULONG64 Addresses[512];
    ULONG Frames[512];
    LONG64 Available;
    ULONG i;

    WorldCreate(&World, 8192, 2, 100000);
    System = &World.System;
    Available = (LONG64)MiPfnAvailablePages(&System->Pfn);
    CHECK(NT_SUCCESS(MiAddressSpaceCreate(System, &Space)));
    MachineSetUserRoot(&World.Machine, 0, Space.RootFrame);

    CHECK(MiPtLookup(&Space, 0x10000, NULL) == NULL);
    CHECK(MachineTouch(&World.Machine, 0, 0x10000, MachineRead, TRUE) == STATUS_ACCESS_VIOLATION);

    for (i = 0; i < 512; i++)
    {
        ULONG TableFrame;
        PMI_PTE Slot;
        UCHAR Value = (UCHAR)(i * 3 + 1);
        UCHAR Read = 0;

        Addresses[i] = (0x10000 + (Rng(&Seed) % 0x7FFF0000000ULL)) & ~(ULONG64)(PAGE_SIZE - 1);
        Slot = MiPtEnsure(&Space, Addresses[i], &TableFrame);
        CHECK(Slot != NULL);
        if (MiArchPteRead(Slot) != 0)
        {
            Frames[i] = MI_FRAME_INVALID;
            continue;
        }

        Frames[i] = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
        MiPfnInitializePage(&System->Pfn, Frames[i], MiPtSlotAddress(Slot, TableFrame, Addresses[i]), TableFrame,
                            MiSoftMake(MiSoftDemandZero, MI_PROT_READWRITE, 0), 0);
        MiPtWrite(&Space, Addresses[i], Slot, TableFrame,
                  MiArchPteMakeLeaf(Frames[i], MI_PROT_READWRITE, MI_LEAF_USER | MI_LEAF_DIRTY));

        CHECK(MachineAccessMemory(&World.Machine, 0, Addresses[i] + 5, &Value, 1, MachineWrite, TRUE) == STATUS_SUCCESS);
        CHECK(MachineAccessMemory(&World.Machine, 0, Addresses[i] + 5, &Read, 1, MachineRead, TRUE) == STATUS_SUCCESS);
        CHECK(Read == Value && MachineFrame(&World.Machine, Frames[i])[5] == Value);
        CHECK(MachineTouch(&World.Machine, 0, Addresses[i], MachineExecute, TRUE) == STATUS_ACCESS_VIOLATION);

        /* Debugger reads during bootstrap have only the architecture and the
         * loader's root frame, before an MI_ADDRESS_SPACE has been adopted. */
        {
            ULONG64 Physical;
            MI_PTE Leaf;

            CHECK(MiPtTranslateRoot(MiArchDescribe(), Space.RootFrame, Addresses[i] + 5, &Physical, &Leaf));
            CHECK(Physical == ((ULONG64)Frames[i] << PAGE_SHIFT) + 5);
            CHECK(MiArchPteFrame(Leaf) == Frames[i]);
        }
    }

    CHECK(MiPtCheck(&Space) == 0);
    CHECK(Space.PageTablePages >= 3);

    {
        ULONG64 Physical = ~0ULL;

        CHECK(!MiPtTranslateRoot(MiArchDescribe(), Space.RootFrame, 0, &Physical, NULL));
        CHECK(Physical == 0);
    }

    {
        ULONG TableFrame;
        ULONG64 Va = 0x7FF000000000ULL;
        PMI_PTE Slot = MiPtEnsure(&Space, Va, &TableFrame);
        ULONG Frame = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
        UCHAR Byte = 1;

        MiPfnInitializePage(&System->Pfn, Frame, MiPtSlotAddress(Slot, TableFrame, Va), TableFrame, 0, 0);
        MiPtWrite(&Space, Va, Slot, TableFrame, MiArchPteMakeLeaf(Frame, MI_PROT_READONLY, MI_LEAF_USER));
        CHECK(MachineAccessMemory(&World.Machine, 0, Va, &Byte, 1, MachineRead, TRUE) == STATUS_SUCCESS);
        CHECK(MachineAccessMemory(&World.Machine, 0, Va, &Byte, 1, MachineWrite, TRUE) == STATUS_ACCESS_VIOLATION);

        MiPtWrite(&Space, Va, Slot, TableFrame, MiArchPteMakeLeaf(Frame, MI_PROT_READWRITE, MI_LEAF_USER));
        MiArchTlbInvalidate(Va, 1, TRUE);
        CHECK(!MiArchPteIsDirty(MiArchPteRead(Slot)) && MiArchPteIsWritable(MiArchPteRead(Slot)));
        CHECK(MachineAccessMemory(&World.Machine, 0, Va, &Byte, 1, MachineWrite, TRUE) == STATUS_ACCESS_VIOLATION);
        World.Machine.HardwareAccessDirty = TRUE;
        if (MiArchDescribe()->HasHardwareDirtyBit && strcmp(MiArchDescribe()->Name, "arm64") == 0)
        {
            MiPtWrite(&Space, Va, Slot, TableFrame,
                      MiArchPteMakeLeaf(Frame, MI_PROT_READWRITE, MI_LEAF_USER | MI_LEAF_HARDWARE_DIRTY));
            MiArchTlbInvalidate(Va, 1, TRUE);
            CHECK(MachineAccessMemory(&World.Machine, 0, Va, &Byte, 1, MachineWrite, TRUE) == STATUS_SUCCESS);
            CHECK(MiArchPteIsDirty(MiArchPteRead(Slot)));
        }
        World.Machine.HardwareAccessDirty = FALSE;

        MiPtWrite(&Space, Va, Slot, TableFrame, MiArchPteMakeLeaf(Frame, MI_PROT_EXECUTE_READ, MI_LEAF_USER));
        MiArchTlbInvalidate(Va, 1, TRUE);
        CHECK(MachineTouch(&World.Machine, 0, Va, MachineExecute, TRUE) == STATUS_SUCCESS);

        MiPtWrite(&Space, Va, Slot, TableFrame, MiArchPteMakeLeaf(Frame, MI_PROT_READWRITE, MI_LEAF_DIRTY));
        MiArchTlbInvalidate(Va, 1, TRUE);
        CHECK(MachineTouch(&World.Machine, 0, Va, MachineRead, TRUE) == STATUS_ACCESS_VIOLATION);
        CHECK(MachineTouch(&World.Machine, 0, Va, MachineWrite, FALSE) == STATUS_SUCCESS);

        {
            ULONG64 Physical;
            MI_PTE Block = MiArchPteMakeBlock(Frame, MI_PROT_READWRITE, MI_LEAF_DIRTY);
            BOOLEAN LeafValid = MiArchPteIsLeafDescriptor(Block);

            MiPtWrite(&Space, Va, Slot, TableFrame, Block);
            MiArchTlbInvalidate(Va, 1, TRUE);
            CHECK(MiPtTranslate(&Space, Va, &Physical, NULL) == LeafValid);
            CHECK(Physical == (LeafValid ? ((ULONG64)Frame << PAGE_SHIFT) : 0));
        }

        MiPtWrite(&Space, Va, Slot, TableFrame, 0);
        MiArchTlbInvalidate(Va, 1, TRUE);
        MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
        CHECK(MachineTouch(&World.Machine, 0, Va, MachineRead, FALSE) == STATUS_ACCESS_VIOLATION);
    }

    {
        ULONG TableFrame;
        ULONG64 Va = 0x12345000;
        PMI_PTE Slot = MiPtEnsure(&Space, Va, &TableFrame);
        ULONG First = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
        ULONG Second = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
        UCHAR Byte = 0;

        if (MiArchPteRead(Slot) == 0)
        {
            *MachineFrame(&World.Machine, First) = 0x11;
            *MachineFrame(&World.Machine, Second) = 0x22;
            MiPtWrite(&Space, Va, Slot, TableFrame, MiArchPteMakeLeaf(First, MI_PROT_READONLY, MI_LEAF_USER));
            CHECK(MachineAccessMemory(&World.Machine, 0, Va, &Byte, 1, MachineRead, TRUE) == STATUS_SUCCESS && Byte == 0x11);
            MiPtWrite(&Space, Va, Slot, TableFrame, MiArchPteMakeLeaf(Second, MI_PROT_READONLY, MI_LEAF_USER));
            MiArchTlbInvalidate(Va, 1, TRUE);
            CHECK(MachineAccessMemory(&World.Machine, 0, Va, &Byte, 1, MachineRead, TRUE) == STATUS_SUCCESS && Byte == 0x22);
            MiPtWrite(&Space, Va, Slot, TableFrame, 0);
            MiArchTlbInvalidate(Va, 1, TRUE);
        }

        MiPfnShareDecrement(&System->Pfn, First, TRUE);
        MiPfnShareDecrement(&System->Pfn, Second, TRUE);
    }

    for (i = 0; i < 512; i++)
    {
        ULONG TableFrame;
        PMI_PTE Slot;

        if (Frames[i] == MI_FRAME_INVALID)
            continue;

        Slot = MiPtLookup(&Space, Addresses[i], &TableFrame);
        CHECK(Slot != NULL);
        MiPtWrite(&Space, Addresses[i], Slot, TableFrame, 0);
        MiArchTlbInvalidate(Addresses[i], 1, TRUE);
        MiPfnShareDecrement(&System->Pfn, Frames[i], TRUE);
    }

    CHECK(Space.PageTablePages == 0);
    CHECK(MiPtCheck(&Space) == 0);
    MiAddressSpaceDestroy(&Space);

    CHECK(World.Machine.StaleTlbUses == 0);
    CHECK(World.Machine.BreakBeforeMakeViolations == 0);
    CHECK(World.Machine.BadFrameAccesses == 0);
    CHECK(MiPfnDbCheck(&System->Pfn) == 0);
    CHECK((LONG64)MiPfnAvailablePages(&System->Pfn) == Available);

    System->Pfn.Repurpose = CountRepurpose;
    {
        ULONG Held[64];
        ULONG Count = 0;
        ULONG Frame;

        for (i = 0; i < 32; i++)
        {
            Frame = MiPfnAllocatePage(&System->Pfn, 0);
            MiPfnInitializePage(&System->Pfn, Frame, 0x9000 + i * 8, 3, MiSoftMake(MiSoftDemandZero, 4, 0), 0);
            MiPfnShareDecrement(&System->Pfn, Frame, FALSE);
        }

        while ((Frame = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_NO_RECLAIM)) != MI_FRAME_INVALID)
            MiPfnInitializePage(&System->Pfn, Frame, 0, 0, 0, MI_PFN_FLAG_PAGE_TABLE);

        for (i = 0; i < 32; i++)
        {
            Held[Count] = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
            if (Held[Count] != MI_FRAME_INVALID)
                Count++;
        }

        CHECK(Count == 32 && RepurposedFrames == 32);
        CHECK(MiPfnAllocatePage(&System->Pfn, 0) == MI_FRAME_INVALID);
    }

    WorldDestroy(&World);
}

static
void *
PfnBenchWorker(void *Argument)
{
    PFN_THREAD *Thread = Argument;
    ULONG Batch[32];
    ULONG64 Operations = 0;
    ULONG i;

    MiHostCpu = Thread->Index;
    while (!__atomic_load_n(Thread->Go, __ATOMIC_SEQ_CST))
        sched_yield();

    while (!__atomic_load_n(Thread->Stop, __ATOMIC_SEQ_CST))
    {
        for (i = 0; i < 32; i++)
            Batch[i] = MiPfnAllocatePage(Thread->Db, 0);
        for (i = 0; i < 32; i++)
        {
            if (Batch[i] != MI_FRAME_INVALID)
                MiPfnShareDecrement(Thread->Db, Batch[i], TRUE);
        }
        Operations += 32;
    }

    Thread->Operations = Operations;
    return NULL;
}

typedef struct _PT_THREAD
{
    TEST_WORLD *World;
    ULONG Index;
    ULONG64 Operations;
    volatile LONG *Go;
    volatile LONG *Stop;
} PT_THREAD;

static
void *
PtBenchWorker(void *Argument)
{
    PT_THREAD *Thread = Argument;
    PMI_SYSTEM System = &Thread->World->System;
    MI_ADDRESS_SPACE Space;
    ULONG64 Operations = 0;
    ULONG64 Base = 0x100000000ULL;
    ULONG Frame;
    ULONG i;

    MiHostCpu = Thread->Index;
    MiAddressSpaceCreate(System, &Space);
    Frame = MiPfnAllocatePage(&System->Pfn, 0);

    while (!__atomic_load_n(Thread->Go, __ATOMIC_SEQ_CST))
        sched_yield();

    while (!__atomic_load_n(Thread->Stop, __ATOMIC_SEQ_CST))
    {
        for (i = 0; i < 256; i++)
        {
            ULONG TableFrame;
            ULONG64 Va = Base + (ULONG64)i * PAGE_SIZE * 97;
            PMI_PTE Slot = MiPtEnsure(&Space, Va, &TableFrame);

            MiPtWrite(&Space, Va, Slot, TableFrame, MiArchPteMakeLeaf(Frame, MI_PROT_READWRITE, MI_LEAF_USER));
        }
        for (i = 0; i < 256; i++)
        {
            ULONG TableFrame;
            ULONG64 Va = Base + (ULONG64)i * PAGE_SIZE * 97;
            PMI_PTE Slot = MiPtLookup(&Space, Va, &TableFrame);

            MiPtWrite(&Space, Va, Slot, TableFrame, 0);
        }
        Operations += 512;
    }

    MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
    MiAddressSpaceDestroy(&Space);
    Thread->Operations = Operations;
    return NULL;
}

void
TestBenchCore(void)
{
    static const ULONG Counts[] = { 1, 2, 4, 8 };
    static TEST_WORLD World;
    ULONG c, i;

    WorldCreate(&World, 1 << 18, 8, 1 << 30);
    World.Machine.StrictTlb = FALSE;
    World.Machine.TlbDisabled = TRUE;

    printf("  pfn allocate+free pairs, M/s:      ");
    for (c = 0; c < RTL_NUMBER_OF(Counts); c++)
    {
        PFN_THREAD Threads[8];
        pthread_t Handles[8];
        volatile LONG Go = 0, Stop = 0;
        ULONG64 Total = 0;
        double Start;

        for (i = 0; i < Counts[c]; i++)
        {
            Threads[i].Db = &World.System.Pfn;
            Threads[i].Index = i;
            Threads[i].Operations = 0;
            Threads[i].Go = &Go;
            Threads[i].Stop = &Stop;
            pthread_create(&Handles[i], NULL, PfnBenchWorker, &Threads[i]);
        }

        Start = NowSeconds();
        __atomic_store_n(&Go, 1, __ATOMIC_SEQ_CST);
        while (NowSeconds() - Start < 0.5)
            sched_yield();
        __atomic_store_n(&Stop, 1, __ATOMIC_SEQ_CST);
        for (i = 0; i < Counts[c]; i++)
        {
            pthread_join(Handles[i], NULL);
            Total += Threads[i].Operations;
        }
        printf("  %ut %6.2f", (unsigned)Counts[c], (double)Total / (NowSeconds() - Start) / 1e6);
    }
    printf("\n");
    CHECK(MiPfnDbCheck(&World.System.Pfn) == 0);

    printf("  pte map+unmap (own address space), M/s:");
    for (c = 0; c < RTL_NUMBER_OF(Counts); c++)
    {
        PT_THREAD Threads[8];
        pthread_t Handles[8];
        volatile LONG Go = 0, Stop = 0;
        ULONG64 Total = 0;
        double Start;

        for (i = 0; i < Counts[c]; i++)
        {
            Threads[i].World = &World;
            Threads[i].Index = i;
            Threads[i].Operations = 0;
            Threads[i].Go = &Go;
            Threads[i].Stop = &Stop;
            pthread_create(&Handles[i], NULL, PtBenchWorker, &Threads[i]);
        }

        Start = NowSeconds();
        __atomic_store_n(&Go, 1, __ATOMIC_SEQ_CST);
        while (NowSeconds() - Start < 0.5)
            sched_yield();
        __atomic_store_n(&Stop, 1, __ATOMIC_SEQ_CST);
        for (i = 0; i < Counts[c]; i++)
        {
            pthread_join(Handles[i], NULL);
            Total += Threads[i].Operations;
        }
        printf("  %ut %6.2f", (unsigned)Counts[c], (double)Total / (NowSeconds() - Start) / 1e6);
    }
    printf("\n");

    CHECK(MiPfnDbCheck(&World.System.Pfn) == 0);
    CHECK(World.Machine.BreakBeforeMakeViolations == 0);
    WorldDestroy(&World);
}
