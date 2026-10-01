/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Frames outside the direct map: allocation zones and the frame window
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "mmharness.h"

#define HM_FRAMES 2048
#define HM_DIRECT 1024

static BOOLEAN
FrameIsFilled(TEST_WORLD *World, ULONG Frame, UCHAR Value)
{
    PUCHAR Bytes = MachineFrame(&World->Machine, Frame);
    ULONG i;

    for (i = 0; i < PAGE_SIZE; i++)
    {
        if (Bytes[i] != Value)
            return FALSE;
    }
    return TRUE;
}

static void
HighMemZones(void)
{
    static TEST_WORLD World;
    static ULONG Held[HM_FRAMES];
    PMI_PFN_DATABASE Db;
    ULONG Count = 0, Frame, i, HighSeen = 0, LowSeen = 0, Cursor;
    ULONG64 HighFree;
    BOOLEAN Ordered = TRUE;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, 2, 100000);
    Db = &World.System.Pfn;
    CHECK(Db->DirectFrames == HM_DIRECT && Db->Window.SlotCount != 0);
    CHECK(MiPfnDbCheck(Db) == 0);

    while ((Frame = MiPfnAllocatePage(Db, MI_ALLOCATE_NO_RECLAIM)) != MI_FRAME_INVALID)
    {
        CHECK(Frame < HM_DIRECT);
        Held[Count++] = Frame;
    }
    HighFree = MiPfnAvailablePages(Db);
    CHECK(HighFree == HM_FRAMES - HM_DIRECT);
    CHECK(MiPfnAllocatePage(Db, MI_ALLOCATE_ZEROED) == MI_FRAME_INVALID);
    CHECK(MiPfnAllocateContiguous(Db, 2, 0, HM_FRAMES - 1, 0) == MI_FRAME_INVALID);
    Cursor = HM_DIRECT;
    CHECK(MiPfnAllocatePageInRange(Db, HM_DIRECT, HM_FRAMES - 1, &Cursor) == MI_FRAME_INVALID);

    Frame = MiPfnAllocatePage(Db, MI_ALLOCATE_HIGH);
    CHECK(Frame != MI_FRAME_INVALID && Frame >= HM_DIRECT);
    CHECK(MiPfnSetCache(Db, Frame, MI_LEAF_NOCACHE) == STATUS_SUCCESS);
    CHECK((ULONG)Db->Pfn[Frame].CacheFlags == MI_LEAF_NOCACHE && World.Machine.FrameCache[Frame] == 0);
    MiPfnShareDecrement(Db, Frame, TRUE);
    CHECK(Db->Pfn[Frame].State == MiPageFree);
    CHECK(MiPfnAvailablePages(Db) == HighFree);

    while (Count != 0)
        MiPfnShareDecrement(Db, Held[--Count], TRUE);
    MiPfnDrainCaches(Db);

    while ((Frame = MiPfnAllocatePage(Db, MI_ALLOCATE_HIGH | MI_ALLOCATE_NO_RECLAIM)) != MI_FRAME_INVALID)
    {
        if (Frame >= HM_DIRECT)
        {
            HighSeen++;
            if (LowSeen != 0)
                Ordered = FALSE;
        }
        else
        {
            LowSeen++;
        }
        memset(MachineFrame(&World.Machine, Frame), 0xEE, PAGE_SIZE);
        Held[Count++] = Frame;
    }
    CHECK(Ordered && HighSeen == HM_FRAMES - HM_DIRECT && LowSeen != 0);
    CHECK(MiPfnAvailablePages(Db) == 0);
    while (Count != 0)
        MiPfnShareDecrement(Db, Held[--Count], TRUE);
    MiPfnDrainCaches(Db);

    for (i = 0; i < 3 * Db->Window.SlotCount; i++)
    {
        Frame = MiPfnAllocatePage(Db, MI_ALLOCATE_HIGH | MI_ALLOCATE_ZEROED);
        CHECK(Frame >= HM_DIRECT && Frame != MI_FRAME_INVALID);
        if (Frame == MI_FRAME_INVALID)
            break;
        CHECK(FrameIsFilled(&World, Frame, 0));
        memset(MachineFrame(&World.Machine, Frame), 0xEE, PAGE_SIZE);
        MiPfnShareDecrement(Db, Frame, TRUE);
    }
    CHECK(Db->Window.Busy == 0 && Db->Window.Maps >= 3 * Db->Window.SlotCount && Db->Window.Flushes >= 2);
    CHECK(MiPfnDbCheck(Db) == 0);
    WorldExpectClean(&World, HM_FRAMES);
    WorldDestroy(&World);
}

static void
HighMemWindow(void)
{
    static TEST_WORLD World;
    PMI_PFN_DATABASE Db;
    PVOID Held[16];
    ULONG Frames[16];
    ULONG i;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, 1, 100000);
    Db = &World.System.Pfn;

    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
    {
        Frames[i] = MiPfnAllocatePage(Db, MI_ALLOCATE_HIGH);
        CHECK(Frames[i] != MI_FRAME_INVALID && Frames[i] >= HM_DIRECT);
        Held[i] = MiPfnMapFrame(Db, Frames[i]);
        CHECK(Held[i] != NULL);
        memset(Held[i], (int)(0x40 + i), PAGE_SIZE);
    }
    CHECK(Db->Window.Busy == RTL_NUMBER_OF(Held));
    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
    {
        ULONG j;

        for (j = i + 1; j < RTL_NUMBER_OF(Held); j++)
            CHECK(Held[i] != Held[j]);
    }
    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
        MiPfnUnmapFrame(Db, Held[i]);
    CHECK(Db->Window.Busy == 0 && Db->Window.Stale == RTL_NUMBER_OF(Held));
    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
        CHECK(FrameIsFilled(&World, Frames[i], (UCHAR)(0x40 + i)));

    {
        ULONG Low = MiPfnAllocatePage(Db, 0);
        PVOID Direct;

        CHECK(Low != MI_FRAME_INVALID && Low < HM_DIRECT);
        Direct = MiPfnMapFrame(Db, Low);
        CHECK(Direct == MachineFrame(&World.Machine, Low));
        MiPfnUnmapFrame(Db, Direct);
        CHECK(Db->Window.Busy == 0);
        MiPfnShareDecrement(Db, Low, TRUE);
    }

    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
        MiPfnShareDecrement(Db, Frames[i], TRUE);
    WorldExpectClean(&World, HM_FRAMES);
    WorldDestroy(&World);
}

static void
HighMemUser(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Parent, Child;
    ULONG64 Base = USER_BASE, Size = 64 * PAGE_SIZE, Physical;
    static ULONG Held[HM_FRAMES];
    ULONG Count = 0, Frame, TableFrame, i;
    NTSTATUS Status;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, 2, 100000);
    WorldAttachPageFile(&World, 1024);
    ProcessCreate(&World, &Parent);
    ProcessCreate(&World, &Child);
    WorldAttach(&World, 0, &Parent);
    WorldAttach(&World, 1, &Child);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Parent, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                             MI_PROT_READWRITE)));
    for (i = 0; i < 64; i++)
    {
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + i * PAGE_SIZE, 0x1000 + i)));
        CHECK(MiPtTranslate(&Parent, Base + i * PAGE_SIZE, &Physical, NULL));
        CHECK((Physical >> PAGE_SHIFT) >= HM_DIRECT);
        CHECK(MiPtLookup(&Parent, Base + i * PAGE_SIZE, &TableFrame) != NULL && TableFrame < HM_DIRECT);
    }
    CHECK(Parent.RootFrame < HM_DIRECT);

    CHECK(MiTrimAddressSpace(&Parent, 64, TRUE) == 64);
    while (MiWriteModifiedPages(&World.System, 1024) != 0)
        ;
    while ((Frame = MiPfnAllocatePage(&World.System.Pfn, MI_ALLOCATE_HIGH)) != MI_FRAME_INVALID)
        Held[Count++] = Frame;
    while (Count != 0)
        MiPfnShareDecrement(&World.System.Pfn, Held[--Count], TRUE);
    CHECK(World.Paging.PageFile.SlotsInUse == 64);
    for (i = 0; i < 64; i++)
    {
        CHECK(UserRead64(&World, 0, Base + i * PAGE_SIZE, &Status) == 0x1000 + i && NT_SUCCESS(Status));
        CHECK(MiPtTranslate(&Parent, Base + i * PAGE_SIZE, &Physical, NULL));
        CHECK((Physical >> PAGE_SHIFT) >= HM_DIRECT);
    }
    CHECK(MI_ATOMIC_READ64(&Parent.PageFileFaults) == 64);

    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Parent, &Child)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 1, Base, 0xC0DE)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + PAGE_SIZE, 0xF00D)));
    CHECK(UserRead64(&World, 0, Base, &Status) == 0x1000 && NT_SUCCESS(Status));
    CHECK(UserRead64(&World, 1, Base, &Status) == 0xC0DE && NT_SUCCESS(Status));
    CHECK(UserRead64(&World, 1, Base + PAGE_SIZE, &Status) == 0x1001 && NT_SUCCESS(Status));
    CHECK(UserRead64(&World, 0, Base + PAGE_SIZE, &Status) == 0xF00D && NT_SUCCESS(Status));
    for (i = 2; i < 64; i++)
        CHECK(UserRead64(&World, 1, Base + i * PAGE_SIZE, &Status) == 0x1000 + i && NT_SUCCESS(Status));

    CHECK(World.System.Pfn.Window.Maps != 0 && World.System.Pfn.Window.Busy == 0);
    CHECK(World.Machine.DirectViolations == 0);
    WorldAttach(&World, 1, NULL);
    ProcessDestroy(&World, &Child);
    ProcessDestroy(&World, &Parent);
    WorldExpectClean(&World, HM_FRAMES);
    WorldDestroy(&World);
}

void
TestHighMem(void)
{
    HighMemZones();
    HighMemWindow();
    HighMemUser();
}
