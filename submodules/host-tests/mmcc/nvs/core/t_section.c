/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_section.c
 * PURPOSE:     Memory section host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"

#define KB64 0x10000ULL

typedef struct _SECTION_ADMISSION
{
    MI_RWLOCK Lock;
    volatile LONG Acquired;
    volatile LONG Release;
} SECTION_ADMISSION;

static void *
SectionAdmissionWriter(void *Context)
{
    SECTION_ADMISSION *State = Context;

    MI_RW_ACQUIRE_EXCLUSIVE(&State->Lock);
    MI_ATOMIC_ADD32(&State->Acquired, 1);
    while (!MI_ATOMIC_READ32(&State->Release))
        sched_yield();
    MI_RW_RELEASE_EXCLUSIVE(&State->Lock);
    return NULL;
}

static void
SectionWriterAdmission(void)
{
    SECTION_ADMISSION State = {0};
    pthread_t Worker;

    MI_RW_INIT(&State.Lock);
    MI_RW_ACQUIRE_EXCLUSIVE(&State.Lock);
    CHECK(MI_ATOMIC_READ32(&State.Lock.WriterGate.Held) == 0);
    MI_RW_RELEASE_EXCLUSIVE(&State.Lock);
    MI_RW_ACQUIRE_SHARED(&State.Lock);
    MI_ASSERT(pthread_create(&Worker, NULL, SectionAdmissionWriter, &State) == 0);
    while (!MI_ATOMIC_READ32(&State.Lock.Writers))
        sched_yield();
    CHECK(MI_ATOMIC_READ32(&State.Acquired) == 0);
    MI_RW_RELEASE_SHARED(&State.Lock);
    while (!MI_ATOMIC_READ32(&State.Acquired))
        sched_yield();
    CHECK(MI_ATOMIC_READ32(&State.Lock.Held) == -1);
    CHECK(MI_ATOMIC_READ32(&State.Lock.WriterGate.Held) == 0);
    MI_ATOMIC_ADD32(&State.Release, 1);
    MI_ASSERT(pthread_join(Worker, NULL) == 0);
    CHECK(MI_ATOMIC_READ32(&State.Lock.Held) == 0);
    CHECK(MI_ATOMIC_READ32(&State.Lock.Writers) == 0);
}

static void
SectionVadCache(void)
{
    MI_ADDRESS_SPACE Space = {0};
    PMI_VAD Vads[MI_VAD_CACHE_SIZE + 8];
    MI_VAD Empty = {0};
    ULONG Index;

    MI_RW_INIT(&Space.Lock);
    for (Index = 0; Index < RTL_NUMBER_OF(Vads); Index++)
    {
        Vads[Index] = MiVadAllocateAndLock(&Space);
        MI_RW_RELEASE_EXCLUSIVE(&Space.Lock);
        CHECK(Vads[Index] != NULL);
        memset(Vads[Index], 0xA5, sizeof(*Vads[Index]));
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Vads); Index++)
    {
        BOOLEAN Cached;

        MI_RW_ACQUIRE_EXCLUSIVE(&Space.Lock);
        Cached = MiVadCacheFree(&Space, Vads[Index]);
        CHECK(Cached == (Index < MI_VAD_CACHE_SIZE));
        MI_RW_RELEASE_EXCLUSIVE(&Space.Lock);
        if (!Cached)
            MI_FREE(Vads[Index]);
    }
    CHECK(Space.FreeVadCount == MI_VAD_CACHE_SIZE);
    for (Index = 0; Index < MI_VAD_CACHE_SIZE; Index++)
    {
        PMI_VAD Vad = MiVadAllocateAndLock(&Space);

        CHECK(Vad == Vads[MI_VAD_CACHE_SIZE - 1 - Index]);
        CHECK(memcmp(Vad, &Empty, sizeof(*Vad)) == 0);
        MI_RW_RELEASE_EXCLUSIVE(&Space.Lock);
        MI_FREE(Vad);
    }
    CHECK(Space.FreeVads == NULL && Space.FreeVadCount == 0);
    Vads[0] = MiVadAllocateAndLock(&Space);
    CHECK(MiVadCacheFree(&Space, Vads[0]));
    MiVadFlushCache(&Space);
    CHECK(Space.FreeVads == NULL && Space.FreeVadCount == 0);
    MI_RW_RELEASE_EXCLUSIVE(&Space.Lock);
}

typedef struct _SECTION_CHURN
{
    TEST_WORLD *World;
    PMI_ADDRESS_SPACE Space;
    PMI_SEGMENT Segment;
    volatile LONG *Ready;
    volatile LONG *Go;
    ULONG Cpu;
} SECTION_CHURN;

static void *
SectionChurnWorker(void *Context)
{
    SECTION_CHURN *Thread = Context;
    ULONG Iteration;

    MiHostCpu = Thread->Cpu;
    MachineCpu = Thread->Cpu;
    MI_ATOMIC_ADD32(Thread->Ready, 1);
    while (!MI_ATOMIC_READ32(Thread->Go))
        sched_yield();
    for (Iteration = 0; Iteration < 512; Iteration++)
    {
        ULONG64 Base = 0, Size = KB64;
        ULONG64 Reserve = 0, ReserveSize = 1024 * 1024;
        NTSTATUS Status;

        CHECK(NT_SUCCESS(MiMapView(Thread->Space, Thread->Segment, &Base, 0, &Size, MI_PROT_READONLY, 0)));
        CHECK(NT_SUCCESS(MiAllocateVirtualMemory(Thread->Space, &Reserve, &ReserveSize,
                                                 MI_MEM_RESERVE, MI_PROT_NOACCESS)));
        UserRead64(Thread->World, Thread->Cpu, Base + (Iteration % 16) * PAGE_SIZE, &Status);
        CHECK(NT_SUCCESS(Status));
        CHECK(NT_SUCCESS(MiUnmapView(Thread->Space, Base)));
        ReserveSize = 0;
        CHECK(NT_SUCCESS(MiFreeVirtualMemory(Thread->Space, &Reserve, &ReserveSize, MI_MEM_RELEASE)));
    }
    CHECK(MiHostIrql == 0);
    return NULL;
}

static void
SectionConcurrentChurn(void)
{
    TEST_WORLD World;
    TEST_FILE File;
    MI_ADDRESS_SPACE Space;
    PMI_SEGMENT Segment;
    SECTION_CHURN Threads[4];
    pthread_t Workers[4];
    volatile LONG Ready = 0, Go = 0;
    ULONG Cpu;

    WorldCreate(&World, 256, 4, 100000);
    World.Machine.StrictTlb = TRUE;
    FileCreate(&File, KB64);
    CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World.System, &Space)));
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, KB64, MI_PROT_READWRITE,
                                     &TestFileOps, &File, NULL, 0, &Segment)));
    for (Cpu = 0; Cpu < RTL_NUMBER_OF(Threads); Cpu++)
    {
        WorldAttach(&World, Cpu, &Space);
        Threads[Cpu] = (SECTION_CHURN){ &World, &Space, Segment, &Ready, &Go, Cpu };
        MI_ASSERT(pthread_create(&Workers[Cpu], NULL, SectionChurnWorker, &Threads[Cpu]) == 0);
    }
    while (MI_ATOMIC_READ32(&Ready) != RTL_NUMBER_OF(Threads))
        sched_yield();
    MI_ATOMIC_ADD32(&Go, 1);
    for (Cpu = 0; Cpu < RTL_NUMBER_OF(Threads); Cpu++)
    {
        MI_ASSERT(pthread_join(Workers[Cpu], NULL) == 0);
        WorldAttach(&World, Cpu, NULL);
    }
    CHECK(Space.VadRoot.Root == NULL && Segment->MappedViews == 0);
    CHECK(Space.FreeVadCount > 0 && Space.FreeVadCount <= MI_VAD_CACHE_SIZE);
    CHECK(Space.CommittedPages == 0 && Space.ResidentPages == 0);
    CHECK(MiPtCheck(&Space) == 0);
    MiAddressSpaceDestroy(&Space);
    CHECK(Space.FreeVadCount == 0 && Space.FreeVads == NULL);
    MiSegmentDereference(Segment);
    WorldExpectClean(&World, 256);
    FileDestroy(&File);
    WorldDestroy(&World);
}

static
NTSTATUS
Map(PMI_ADDRESS_SPACE Space, PMI_SEGMENT Segment, ULONG64 *Base, ULONG64 Offset, ULONG64 Size, ULONG Protection)
{
    ULONG64 ViewSize = Size;

    return MiMapView(Space, Segment, Base, Offset, &ViewSize, Protection, 0);
}

static
void
SpaceCreate(TEST_WORLD *World, ULONG Cpu, PMI_ADDRESS_SPACE Space)
{
    CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World->System, Space)));
    WorldAttach(World, Cpu, Space);
}

static
void
SpaceDestroy(TEST_WORLD *World, ULONG Cpu, PMI_ADDRESS_SPACE Space)
{
    MiCleanAddressSpace(Space);
    CHECK(MI_ATOMIC_READ64(&Space->CommittedPages) == 0);
    CHECK(MI_ATOMIC_READ64(&Space->ResidentPages) == 0);
    CHECK(MI_ATOMIC_READ64(&Space->PageTablePages) == 0);
    WorldAttach(World, Cpu, NULL);
    MiAddressSpaceDestroy(Space);
}

static
void
SectionUnmapBatch(void)
{
    TEST_WORLD World;
    TEST_FILE File;
    MI_ADDRESS_SPACE Space;
    PMI_SEGMENT Segment;
    ULONG Iteration, Cpu, Page;
    NTSTATUS Status;

    WorldCreate(&World, 256, 4, 100000);
    World.Machine.StrictTlb = TRUE;
    FileCreate(&File, KB64);
    SpaceCreate(&World, 0, &Space);
    for (Cpu = 1; Cpu < 4; Cpu++)
        WorldAttach(&World, Cpu, &Space);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, KB64, MI_PROT_READWRITE,
                                     &TestFileOps, &File, NULL, 0, &Segment)));
    for (Iteration = 0; Iteration < 64; Iteration++)
    {
        ULONG64 Base = 0x40000000 + (ULONG64)Iteration * KB64;
        ULONG TableFrame, PreviousTable = 0;
        ULONG Pages = (Iteration & 1) ? 16 : 1;
        BOOLEAN Reuse = MiPtLookup(&Space, Base, &PreviousTable) != NULL;
        MACHINE_FAULT_ROUTINE Fault = World.Machine.Fault;
        LONG64 Invalidations;

        CHECK(NT_SUCCESS(Map(&Space, Segment, &Base, 0, KB64, MI_PROT_READONLY)));
        for (Page = 0; Page < Pages; Page++)
        {
            for (Cpu = 0; Cpu < 4; Cpu++)
            {
                CHECK(UserRead64(&World, Cpu, Base + (ULONG64)Page * PAGE_SIZE, &Status) ==
                      *(ULONG64 *)(File.Data + (ULONG64)Page * PAGE_SIZE));
                CHECK(NT_SUCCESS(Status));
            }
        }
        CHECK(MiPtLookup(&Space, Base, &TableFrame) != NULL);
        if (Reuse)
            CHECK(TableFrame == PreviousTable);
        Invalidations = World.Machine.TlbInvalidations;
        CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));
        CHECK(World.Machine.TlbInvalidations == Invalidations + 1);
        CHECK(MiPtLookup(&Space, Base, NULL) != NULL);
        CHECK(Space.ResidentPages == 0 && Space.PrivatePages == 0 && Segment->MappedViews == 0);
        CHECK(Space.PageTablePages >= World.System.Arch->PagingLevels - 1 &&
              Space.PageTablePages <= World.System.Arch->PagingLevels);
        World.Machine.Fault = NULL;
        for (Cpu = 0; Cpu < 4; Cpu++)
        {
            UserRead64(&World, Cpu, Base, &Status);
            CHECK(Status == STATUS_ACCESS_VIOLATION);
        }
        World.Machine.Fault = Fault;
        CHECK(MiPtCheck(&Space) == 0);
    }
    for (Cpu = 1; Cpu < 4; Cpu++)
        WorldAttach(&World, Cpu, NULL);
    SpaceDestroy(&World, 0, &Space);
    MiSegmentDereference(Segment);
    WorldExpectClean(&World, 256);
    FileDestroy(&File);
    WorldDestroy(&World);
}

static
void
SectionSharedDataFile(void)
{
    TEST_WORLD World;
    TEST_FILE File;
    MI_ADDRESS_SPACE A, B;
    MI_MEMORY_INFORMATION Info;
    PMI_SEGMENT Segment;
    PMI_SEGMENT ReadOnly;
    ULONG64 BaseA = 0, BaseB = 0, Base = 0;
    ULONG64 FrameA, FrameB, Leaf;
    ULONG64 Size = 0x40000 + 100;
    ULONG64 ViewSize;
    NTSTATUS Status;
    UCHAR Byte;
    ULONG Old;
    ULONG i;

    WorldCreate(&World, 2048, 2, 100000);
    World.Machine.StrictTlb = TRUE;
    FileCreate(&File, 0x100000);
    SpaceCreate(&World, 0, &A);
    SpaceCreate(&World, 1, &B);

    CHECK(MiSegmentCreate(&World.System, MiSegmentDataFile, 0, MI_PROT_READWRITE, &TestFileOps, &File, NULL, 0,
                          &Segment) == STATUS_SECTION_TOO_BIG);
    CHECK(MiSegmentCreate(&World.System, MiSegmentDataFile, Size, MI_PROT_READWRITE, NULL, NULL, NULL, 0,
                          &Segment) == STATUS_INVALID_PARAMETER);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, Size, MI_PROT_READWRITE, &TestFileOps, &File,
                                     NULL, 0, &Segment)));
    CHECK(Segment->PageCount == 65);

    CHECK(Map(&A, Segment, &BaseA, 0x1000, 0, MI_PROT_READWRITE) == STATUS_INVALID_PARAMETER);
    CHECK(Map(&A, Segment, &BaseA, 0, 0x100000, MI_PROT_READWRITE) == STATUS_INVALID_VIEW_SIZE);
    CHECK(Map(&A, Segment, &BaseA, 0x100000, 0, MI_PROT_READWRITE) == STATUS_INVALID_VIEW_SIZE);
    CHECK(Map(&A, Segment, &BaseA, 0, 0, MI_PROT_NONE) == STATUS_INVALID_PAGE_PROTECTION);

    ViewSize = 0;
    CHECK(NT_SUCCESS(MiMapView(&A, Segment, &BaseA, 0, &ViewSize, MI_PROT_READWRITE, 0)));
    CHECK(ViewSize == 65 * 4096 && (BaseA & (KB64 - 1)) == 0);
    CHECK(NT_SUCCESS(Map(&B, Segment, &BaseB, 0, 0, MI_PROT_READONLY)));
    CHECK(Segment->MappedViews == 2);
    CHECK(Map(&B, Segment, &BaseB, 0, 0, MI_PROT_READONLY) == STATUS_CONFLICTING_ADDRESSES);

    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, BaseA + 0x2000, &Info)));
    CHECK(Info.State == MI_MEM_COMMIT && Info.Type == MI_MEM_MAPPED && Info.Protect == MI_PROT_READWRITE);
    CHECK(Info.AllocationBase == BaseA && Info.RegionSize == 63 * 4096);

    for (i = 0; i < 8; i++)
    {
        CHECK(NT_SUCCESS(UserRead(&World, 1, BaseB + i * 4096 + 5, &Byte, 1)));
        CHECK(Byte == File.Data[i * 4096 + 5]);
    }
    CHECK(File.Reads == 8);

    CHECK(NT_SUCCESS(UserRead(&World, 0, BaseA + 0x40000 + 99, &Byte, 1)) && Byte == File.Data[0x40000 + 99]);
    CHECK(NT_SUCCESS(UserRead(&World, 0, BaseA + 0x40000 + 100, &Byte, 1)) && Byte == 0);
    CHECK(UserRead(&World, 0, BaseA + 0x41000, &Byte, 1) == STATUS_ACCESS_VIOLATION);

    CHECK(NT_SUCCESS(UserWrite64(&World, 0, BaseA + 0x3000, 0xFEEDFACECAFEBEEFULL)));
    CHECK(UserRead64(&World, 1, BaseB + 0x3000, &Status) == 0xFEEDFACECAFEBEEFULL);
    CHECK(File.Reads == 9);
    CHECK(MachineProbe(&World.Machine, 0, BaseA + 0x3000, &FrameA, &Leaf));
    CHECK(MachineProbe(&World.Machine, 1, BaseB + 0x3000, &FrameB, &Leaf));
    CHECK(FrameA == FrameB);
    CHECK(World.PfnArray[FrameA].ShareCount == 2 && (World.PfnArray[FrameA].Flags & MI_PFN_FLAG_PROTOTYPE));
    CHECK(UserWrite64(&World, 1, BaseB + 0x3000, 1) == STATUS_ACCESS_VIOLATION);

    CHECK(File.Writes == 0);
    {
        ULONG64 FlushBase = BaseA + 0x3010;
        ULONG64 FlushSize = 0x20;
        ULONG64 CommitBase = BaseA + 0x1000;
        ULONG64 CommitSize = 0x2000;

        CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&A, &CommitBase, &CommitSize, MI_MEM_COMMIT, MI_PROT_READWRITE)));
        CHECK(NT_SUCCESS(MiFlushVirtualMemory(&A, &FlushBase, &FlushSize)));
        CHECK(FlushBase == BaseA + 0x3000 && FlushSize == 0x1000);
        CHECK(File.Writes == 1 && *(ULONG64 *)(File.Data + 0x3000) == 0xFEEDFACECAFEBEEFULL);
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, BaseA + 0x3000, 0xFEEDFACECAFEBEEFULL)));
        File.Writes = 0;
        FlushBase = 0x1000;
        CHECK(MiFlushVirtualMemory(&A, &FlushBase, &FlushSize) == STATUS_NOT_MAPPED_VIEW);
    }
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 0, Size)));
    CHECK(File.Writes == 1);
    CHECK(*(ULONG64 *)(File.Data + 0x3000) == 0xFEEDFACECAFEBEEFULL);
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 0, Size)));
    CHECK(File.Writes == 1);

    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&A, &(ULONG64){BaseA + 0x3000}, &(ULONG64){0x1000}, MI_PROT_READONLY,
                                            &Old)));
    CHECK(Old == MI_PROT_READWRITE);
    CHECK(UserWrite64(&World, 0, BaseA + 0x3000, 1) == STATUS_ACCESS_VIOLATION);
    CHECK(UserRead64(&World, 0, BaseA + 0x3000, &Status) == 0xFEEDFACECAFEBEEFULL);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, BaseA + 0x3000, &Info)));
    CHECK(Info.Protect == MI_PROT_READONLY && Info.RegionSize == 0x1000);
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&A, &(ULONG64){BaseA + 0x3000}, &(ULONG64){0x1000}, MI_PROT_READWRITE,
                                            &Old)));
    CHECK(Old == MI_PROT_READONLY);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, BaseA + 0x3008, 0x77)));
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&B, &(ULONG64){BaseB}, &(ULONG64){0x1000},
                                            MI_PROT_READWRITE | MI_PROT_GUARD, &Old)));
    CHECK(UserRead(&World, 1, BaseB + 5, &Byte, 1) == STATUS_GUARD_PAGE_VIOLATION);
    CHECK(NT_SUCCESS(UserRead(&World, 1, BaseB + 5, &Byte, 1)) && Byte == File.Data[5]);

    CHECK(MiFreeVirtualMemory(&A, &(ULONG64){BaseA}, &(ULONG64){0}, MI_MEM_RELEASE) == STATUS_UNABLE_TO_DELETE_SECTION);
    CHECK(MiSegmentPurge(Segment, 0, 0) == FALSE);

    CHECK(MiTrimAddressSpace(&A, 1000, TRUE) != 0);
    CHECK(MI_ATOMIC_READ64(&A.ResidentPages) == 0);
    CHECK(World.PfnArray[FrameA].ShareCount == 1);
    CHECK(UserRead64(&World, 0, BaseA + 0x3008, &Status) == 0x77);
    CHECK(File.Reads == 9);

    CHECK(NT_SUCCESS(MiUnmapView(&A, BaseA + 0x5000)));
    CHECK(MiUnmapView(&A, BaseA) == STATUS_NOT_MAPPED_VIEW);
    CHECK(World.PfnArray[FrameA].State == MiPageActive);
    CHECK(NT_SUCCESS(MiUnmapView(&B, BaseB)));
    CHECK(Segment->MappedViews == 0);
    CHECK(World.PfnArray[FrameA].State == MiPageModified);
    CHECK(MiSegmentIsResident(Segment, 0, 0x8000));
    CHECK(!MiSegmentIsResident(Segment, 0, 0x10000));

    CHECK(MiWriteModifiedPages(&World.System, 100) == 1);
    CHECK(File.Writes == 2 && *(ULONG64 *)(File.Data + 0x3008) == 0x77);
    CHECK(World.PfnArray[FrameA].State == MiPageStandby);

    BaseA = 0;
    CHECK(NT_SUCCESS(Map(&A, Segment, &BaseA, 0, 0x10000, MI_PROT_READONLY)));
    CHECK(UserRead64(&World, 0, BaseA + 0x3008, &Status) == 0x77);
    CHECK(File.Reads == 9);
    CHECK(MI_ATOMIC_READ64(&A.PrototypeFaults) != 0);
    CHECK(NT_SUCCESS(MiUnmapView(&A, BaseA)));

    CHECK(MiSegmentPurge(Segment, 0, 0) == TRUE);
    CHECK(!MiSegmentIsResident(Segment, 0, 0x1000));
    CHECK(NT_SUCCESS(MiSegmentMakeResident(Segment, 0, 0x10000)));
    CHECK(File.Reads == 25);
    CHECK(MiSegmentIsResident(Segment, 0, 0x10000));
    BaseA = 0;
    CHECK(NT_SUCCESS(Map(&A, Segment, &BaseA, 0, 0x10000, MI_PROT_READWRITE)));
    for (i = 0; i < 16; i++)
        UserRead64(&World, 0, BaseA + i * 4096, &Status);
    CHECK(File.Reads == 25);

    CHECK(NT_SUCCESS(MiSegmentMarkDirty(Segment, 0x5000, 0x2000)));
    File.Writes = 0;
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 0x5000, 0x1000)));
    CHECK(File.Writes == 1);
    File.FailWrites = 1;
    CHECK(!NT_SUCCESS(MiSegmentFlush(Segment, 0, Size)));
    File.FailWrites = 0;
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 0, Size)));
    CHECK(File.Writes == 2);

    CHECK(MiSegmentCreate(&World.System, MiSegmentDataFile, Size, MI_PROT_READONLY, &TestFileOps, &File, NULL, 0,
                          &ReadOnly) == STATUS_SUCCESS);
    CHECK(Map(&B, ReadOnly, &Base, 0, 0, MI_PROT_READWRITE) == STATUS_SECTION_PROTECTION);
    CHECK(NT_SUCCESS(Map(&B, ReadOnly, &Base, 0, 0, MI_PROT_READONLY)));
    CHECK(MiProtectVirtualMemory(&B, &(ULONG64){Base}, &(ULONG64){0x1000}, MI_PROT_READWRITE, &Old) ==
          STATUS_SECTION_PROTECTION);
    File.FailReads = 1;
    UserRead64(&World, 1, Base, &Status);
    CHECK(Status == STATUS_IN_PAGE_ERROR);
    File.FailReads = 0;
    UserRead64(&World, 1, Base, &Status);
    CHECK(NT_SUCCESS(Status));
    MiSegmentDereference(ReadOnly);

    {
        ULONG64 Limited = 0;
        ULONG64 LimitedSize = 0x10000;

        CHECK(MiMapViewEx(&B, Segment, &Limited, 0, &LimitedSize, MI_PROT_READWRITE, 0, ~0ULL, MI_PROT_READONLY, TRUE) ==
              STATUS_SECTION_PROTECTION);
        CHECK(NT_SUCCESS(MiMapViewEx(&B, Segment, &Limited, 0, &LimitedSize, MI_PROT_READONLY, 0, ~0ULL,
                                     MI_PROT_READONLY, TRUE)));
        CHECK(MiProtectVirtualMemory(&B, &(ULONG64){Limited}, &(ULONG64){0x1000}, MI_PROT_READWRITE, &Old) ==
              STATUS_SECTION_PROTECTION);
        CHECK(NT_SUCCESS(MiProtectVirtualMemory(&B, &(ULONG64){Limited}, &(ULONG64){0x1000}, MI_PROT_WRITECOPY,
                                                &Old)));
        CHECK(NT_SUCCESS(MiUnmapView(&B, Limited)));
    }

    CHECK(NT_SUCCESS(MiSegmentExtend(Segment, 0x80000)));
    CHECK(Segment->PageCount == 128);
    Base = 0;
    CHECK(NT_SUCCESS(Map(&B, Segment, &Base, 0x40000, 0x40000, MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(UserRead(&World, 1, Base + 200, &Byte, 1)) && Byte == File.Data[0x40000 + 200]);
    CHECK(NT_SUCCESS(UserWrite64(&World, 1, Base + 0x20000, 0xABCDEF)));
    File.Writes = 0;

    MiSegmentDereference(Segment);
    SpaceDestroy(&World, 0, &A);
    SpaceDestroy(&World, 1, &B);
    CHECK(File.Writes == 1 && *(ULONG64 *)(File.Data + 0x60000) == 0xABCDEF);
    CHECK(IsListEmpty(&World.System.SegmentList));

    WorldExpectClean(&World, 2048);
    FileDestroy(&File);
    WorldDestroy(&World);
}

static
void
SectionPageFileBacked(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE A, B;
    PMI_SEGMENT Segment;
    PMI_SEGMENT Big;
    ULONG64 BaseA = 0, BaseB = 0;
    NTSTATUS Status;
    ULONG Frame;
    ULONG Held[512];
    ULONG HeldCount = 0;
    ULONG i;

    WorldCreate(&World, 512, 2, 1000);
    World.Machine.StrictTlb = TRUE;
    WorldAttachPageFile(&World, 1024);
    SpaceCreate(&World, 0, &A);
    SpaceCreate(&World, 1, &B);

    CHECK(MiSegmentCreate(&World.System, MiSegmentPageFileBacked, 2000 * 4096, MI_PROT_READWRITE, NULL, NULL, NULL,
                          0, &Big) == STATUS_COMMITMENT_LIMIT);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentPageFileBacked, 64 * 4096, MI_PROT_READWRITE, NULL,
                                     NULL, NULL, 0, &Segment)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 64);
    CHECK(MiSegmentExtend(Segment, 2000 * 4096) == STATUS_COMMITMENT_LIMIT);
    CHECK(NT_SUCCESS(MiSegmentExtend(Segment, 128 * 4096)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 128);

    CHECK(NT_SUCCESS(Map(&A, Segment, &BaseA, 0, 0, MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(Map(&B, Segment, &BaseB, 0, 0, MI_PROT_READWRITE)));

    for (i = 0; i < 128; i++)
        CHECK(NT_SUCCESS(UserWrite64(&World, i & 1, ((i & 1) ? BaseB : BaseA) + i * 4096, 0x5EC70000 + i)));
    for (i = 0; i < 128; i++)
        CHECK(UserRead64(&World, !(i & 1), ((i & 1) ? BaseA : BaseB) + i * 4096, &Status) == 0x5EC70000 + i);

    CHECK(MiTrimAddressSpace(&A, 1000, TRUE) == 128);
    CHECK(MiPfnListCount(&World.System.Pfn, MiPageModified) == 0);
    CHECK(MiTrimAddressSpace(&B, 1000, TRUE) == 128);
    CHECK(MiPfnListCount(&World.System.Pfn, MiPageModified) == 128);
    CHECK(MiWriteModifiedPages(&World.System, 1000) == 128);
    CHECK(World.Paging.PageFile.SlotsInUse == 128);

    while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
        Held[HeldCount++] = Frame;
    CHECK(MiPfnListCount(&World.System.Pfn, MiPageStandby) == 0);
    for (i = 0; i < HeldCount; i++)
        MiPfnShareDecrement(&World.System.Pfn, Held[i], TRUE);

    for (i = 0; i < 128; i++)
        CHECK(UserRead64(&World, 0, BaseA + i * 4096, &Status) == 0x5EC70000 + i);
    CHECK(MI_ATOMIC_READ64(&World.Paging.PageFile.PagesRead) == 128);

    CHECK(NT_SUCCESS(MiUnmapView(&A, BaseA)));
    CHECK(NT_SUCCESS(MiUnmapView(&B, BaseB)));
    MiSegmentDereference(Segment);
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 0);

    SpaceDestroy(&World, 0, &A);
    SpaceDestroy(&World, 1, &B);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

static
void
SectionReservedPageFile(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE A;
    MI_MEMORY_INFORMATION Info;
    PMI_SEGMENT Segment;
    ULONG64 Base = 0;
    ULONG64 Limited = 0;
    ULONG64 LimitedSize = 16 * 4096;
    ULONG64 Commit;
    ULONG64 CommitSize;
    ULONG Old;
    NTSTATUS Status;

    WorldCreate(&World, 512, 1, 1000);
    World.Machine.StrictTlb = TRUE;
    WorldAttachPageFile(&World, 1024);
    SpaceCreate(&World, 0, &A);

    CHECK(NT_SUCCESS(MiSegmentCreateReserved(&World.System, 16 * 4096, MI_PROT_EXECUTE_READWRITE, NULL, NULL,
                                             &Segment)));
    CHECK(Segment->Reserved);
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 0);
    CHECK(NT_SUCCESS(Map(&A, Segment, &Base, 0, 0, MI_PROT_READWRITE)));

    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, Base, &Info)));
    CHECK(Info.State == MI_MEM_RESERVE && Info.RegionSize == 16 * 4096 && Info.Protect == 0);
    UserRead64(&World, 0, Base, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);

    CHECK(NT_SUCCESS(MiSegmentCommitPages(Segment, 2, 4)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 4);
    CHECK(NT_SUCCESS(MiSegmentCommitPages(Segment, 4, 4)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 6);
    CHECK(NT_SUCCESS(MiSegmentCommitPages(Segment, 14, 100)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 8);
    CHECK(NT_SUCCESS(MiSegmentCommitPages(Segment, 40, 1)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 8);

    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, Base + 2 * 4096, &Info)));
    CHECK(Info.State == MI_MEM_COMMIT && Info.RegionSize == 6 * 4096 && Info.Protect == MI_PROT_READWRITE);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, Base + 8 * 4096, &Info)));
    CHECK(Info.State == MI_MEM_RESERVE && Info.RegionSize == 6 * 4096);

    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + 2 * 4096, 0x5EC0000000000002ULL)));
    CHECK(UserRead64(&World, 0, Base + 2 * 4096, &Status) == 0x5EC0000000000002ULL);
    CHECK(UserRead64(&World, 0, Base + 15 * 4096, &Status) == 0 && NT_SUCCESS(Status));
    UserRead64(&World, 0, Base + 8 * 4096, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);

    Commit = Base + 8 * 4096;
    CommitSize = 2 * 4096;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&A, &Commit, &CommitSize, MI_MEM_COMMIT, MI_PROT_READONLY)));
    CHECK(Commit == Base + 8 * 4096 && CommitSize == 2 * 4096);
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 10);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, Base + 8 * 4096, &Info)));
    CHECK(Info.State == MI_MEM_COMMIT && Info.RegionSize == 2 * 4096 && Info.Protect == MI_PROT_READONLY);
    CHECK(UserRead64(&World, 0, Base + 9 * 4096, &Status) == 0 && NT_SUCCESS(Status));
    CHECK(UserWrite64(&World, 0, Base + 9 * 4096, 1) == STATUS_ACCESS_VIOLATION);

    CHECK(NT_SUCCESS(MiMapViewEx(&A, Segment, &Limited, 0, &LimitedSize, MI_PROT_NOACCESS, 0, ~0ULL,
                                 MI_PROT_READWRITE, TRUE)));
    Commit = Limited + 12 * 4096;
    CommitSize = 4096;
    Old = 0;
    CHECK(MiProtectVirtualMemory(&A, &Commit, &CommitSize, MI_PROT_READWRITE, &Old) == STATUS_SECTION_PROTECTION);
    CHECK(Old == MI_PROT_NOACCESS);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&A, &Commit, &CommitSize, MI_MEM_COMMIT, MI_PROT_EXECUTE_READWRITE)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 11);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, Limited + 12 * 4096, &Info)));
    CHECK(Info.State == MI_MEM_COMMIT && Info.RegionSize == 4096 && Info.Protect == MI_PROT_EXECUTE_READWRITE);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Limited + 12 * 4096, 0x5EC000000000000CULL)));
    CHECK(UserRead64(&World, 0, Base + 12 * 4096, &Status) == 0x5EC000000000000CULL);
    Commit = Limited + 2 * 4096;
    CommitSize = 4096;
    CHECK(MiProtectVirtualMemory(&A, &Commit, &CommitSize, MI_PROT_EXECUTE_READWRITE, &Old) ==
          STATUS_SECTION_PROTECTION);
    CHECK(NT_SUCCESS(MiUnmapView(&A, Limited)));

    CHECK(NT_SUCCESS(MiSegmentExtend(Segment, 32 * 4096)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 11);
    CHECK(NT_SUCCESS(MiSegmentCommitPages(Segment, 30, 2)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 13);

    CHECK(NT_SUCCESS(MiUnmapView(&A, Base)));
    MiSegmentDereference(Segment);
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 0);

    SpaceDestroy(&World, 0, &A);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

static
void
SectionCopyOnWrite(void)
{
    TEST_WORLD World;
    TEST_FILE File;
    MI_ADDRESS_SPACE A, B;
    MI_MEMORY_INFORMATION Info;
    PMI_SEGMENT Segment;
    ULONG64 BaseA = 0, BaseB = 0;
    ULONG64 Original;
    NTSTATUS Status;

    WorldCreate(&World, 1024, 2, 100000);
    World.Machine.StrictTlb = TRUE;
    WorldAttachPageFile(&World, 256);
    FileCreate(&File, 0x20000);
    SpaceCreate(&World, 0, &A);
    SpaceCreate(&World, 1, &B);

    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, 0x20000, MI_PROT_READONLY, &TestFileOps,
                                     &File, NULL, 0, &Segment)));
    CHECK(NT_SUCCESS(Map(&A, Segment, &BaseA, 0, 0, MI_PROT_WRITECOPY)));
    CHECK(NT_SUCCESS(Map(&B, Segment, &BaseB, 0, 0, MI_PROT_WRITECOPY)));
    MiSegmentDereference(Segment);

    Original = *(ULONG64 *)(File.Data + 0x4000);
    CHECK(UserRead64(&World, 0, BaseA + 0x4000, &Status) == Original);
    CHECK(UserRead64(&World, 1, BaseB + 0x4000, &Status) == Original);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, BaseA + 0x4000, &Info)));
    CHECK(Info.Protect == MI_PROT_WRITECOPY);

    CHECK(NT_SUCCESS(UserWrite64(&World, 0, BaseA + 0x4000, 0x1111)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, BaseA + 0x6000, 0x2222)));
    CHECK(MI_ATOMIC_READ64(&A.CopyOnWriteFaults) == 2);
    CHECK(MI_ATOMIC_READ64(&A.CommittedPages) == 2);
    CHECK(UserRead64(&World, 0, BaseA + 0x4000, &Status) == 0x1111);
    CHECK(UserRead64(&World, 0, BaseA + 0x4008, &Status) == *(ULONG64 *)(File.Data + 0x4008));
    CHECK(UserRead64(&World, 1, BaseB + 0x4000, &Status) == Original);
    CHECK(UserRead64(&World, 1, BaseB + 0x6000, &Status) == *(ULONG64 *)(File.Data + 0x6000));
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, BaseA + 0x4000, &Info)));
    CHECK(Info.Protect == MI_PROT_READWRITE && Info.RegionSize == 0x1000);

    CHECK(MiTrimAddressSpace(&A, 1000, TRUE) != 0);
    CHECK(MiWriteModifiedPages(&World.System, 1000) == 2);
    CHECK(File.Writes == 0);
    CHECK(UserRead64(&World, 0, BaseA + 0x4000, &Status) == 0x1111);
    CHECK(UserRead64(&World, 0, BaseA + 0x6000, &Status) == 0x2222);

    World.System.CommitLimit = MI_ATOMIC_READ64(&World.System.CommittedPages);
    CHECK(UserWrite64(&World, 1, BaseB + 0x8000, 1) == STATUS_COMMITMENT_LIMIT);
    World.System.CommitLimit = 100000;
    CHECK(NT_SUCCESS(UserWrite64(&World, 1, BaseB + 0x8000, 1)));

    CHECK(NT_SUCCESS(MiUnmapView(&A, BaseA)));
    CHECK(MI_ATOMIC_READ64(&A.CommittedPages) == 0);
    SpaceDestroy(&World, 0, &A);
    SpaceDestroy(&World, 1, &B);
    CHECK(File.Writes == 0 && *(ULONG64 *)(File.Data + 0x4000) == Original);

    WorldExpectClean(&World, 1024);
    FileDestroy(&File);
    WorldDestroy(&World);
}

static
void
SectionSystemSpaceView(void)
{
    TEST_WORLD World;
    TEST_FILE File;
    PMI_ADDRESS_SPACE System;
    PMI_SEGMENT Segment;
    ULONG64 Base = 0;
    ULONG64 Value = 0;
    ULONG64 Leaf, Frame;

    WorldCreate(&World, 1024, 1, 100000);
    World.Machine.StrictTlb = TRUE;
    FileCreate(&File, 0x80000);
    System = &World.System.SystemSpace;
    WorldAttach(&World, 0, NULL);

    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, 0x80000, MI_PROT_READWRITE, &TestFileOps,
                                     &File, NULL, 0, &Segment)));
    CHECK(NT_SUCCESS(Map(System, Segment, &Base, 0x40000, 0x40000, MI_PROT_READWRITE)));
    CHECK(Base >= World.System.Arch->SystemAddressStart);

    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + 0x1000, &Value, 8, MachineRead, FALSE)));
    CHECK(Value == *(ULONG64 *)(File.Data + 0x41000));
    CHECK(MachineAccessMemory(&World.Machine, 0, Base + 0x1000, &Value, 8, MachineRead, TRUE) ==
          STATUS_ACCESS_VIOLATION);

    Value = 0x600DF00D;
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + 0x2000, &Value, 8, MachineWrite, FALSE)));
    CHECK(MachineProbe(&World.Machine, 0, Base + 0x2000, &Frame, &Leaf) && MiArchPteIsDirty(Leaf));
    CHECK(NT_SUCCESS(MiSetRangeModified(System, Base + 0x2000, 0x1000)));
    CHECK(MachineProbe(&World.Machine, 0, Base + 0x2000, &Frame, &Leaf) && !MiArchPteIsDirty(Leaf));
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 0x42000, 0x1000)));
    CHECK(File.Writes == 1 && *(ULONG64 *)(File.Data + 0x42000) == 0x600DF00D);

    Value = 0xBADC0DE;
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base + 0x2000, &Value, 8, MachineWrite, FALSE)));
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 0x42000, 0x1000)));
    CHECK(File.Writes == 2 && *(ULONG64 *)(File.Data + 0x42000) == 0xBADC0DE);

    CHECK(NT_SUCCESS(MiUnmapView(System, Base)));
    MiSegmentDereference(Segment);
    CHECK(MI_ATOMIC_READ64(&System->PageTablePages) == 0);

    MiPfnDrainCaches(&World.System.Pfn);
    CHECK(WorldCheck(&World) == 0);
    CHECK(MiPfnAvailablePages(&World.System.Pfn) == 1024 - 2);
    FileDestroy(&File);
    WorldDestroy(&World);
}

typedef struct _OWNED_FILE
{
    TEST_FILE File;
    volatile LONG Released;
    volatile LONG ReadsAfterRelease;
} OWNED_FILE;

static
NTSTATUS
OwnedRead(PVOID Context, ULONG64 Offset, ULONG Length, PVOID Buffer)
{
    OWNED_FILE *Owned = Context;

    if (Owned->Released != 0)
        __sync_fetch_and_add(&Owned->ReadsAfterRelease, 1);

    return TestFileOps.Read(&Owned->File, Offset, Length, Buffer);
}

static
NTSTATUS
OwnedWrite(PVOID Context, ULONG64 Offset, ULONG Length, PVOID Buffer)
{
    OWNED_FILE *Owned = Context;

    if (Owned->Released != 0)
        __sync_fetch_and_add(&Owned->ReadsAfterRelease, 1);

    return TestFileOps.Write(&Owned->File, Offset, Length, Buffer);
}

static
VOID
OwnedRelease(PVOID Context)
{
    OWNED_FILE *Owned = Context;

    __sync_fetch_and_add(&Owned->Released, 1);
}

static
void
SectionViewOutlivesOwner(void)
{
    static MI_FILE_OPS OwnedOps = { .Read = OwnedRead, .Write = OwnedWrite, .Release = OwnedRelease };
    TEST_WORLD World;
    MI_ADDRESS_SPACE A;
    OWNED_FILE Owned;
    PMI_SEGMENT Segment;
    PMI_SEGMENT Failed;
    ULONG64 Base = 0;
    NTSTATUS Status;
    ULONG i;

    memset(&Owned, 0, sizeof(Owned));
    WorldCreate(&World, 512, 1, 1000);
    SpaceCreate(&World, 0, &A);
    FileCreate(&Owned.File, 32 * 4096);

    CHECK(MiSegmentCreate(&World.System, MiSegmentImage, 32 * 4096, MI_PROT_READONLY, &OwnedOps, &Owned, NULL, 0,
                          &Failed) != STATUS_SUCCESS);
    CHECK(Owned.Released == 0);

    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, 32 * 4096, MI_PROT_READWRITE, &OwnedOps,
                                     &Owned, NULL, 0, &Segment)));
    CHECK(NT_SUCCESS(Map(&A, Segment, &Base, 0, 0, MI_PROT_READWRITE)));
    Owned.File.ProbeLock = &A.Lock;

    CHECK(MiSegmentTryReference(Segment));
    MiSegmentDereference(Segment);
    MiSegmentDereference(Segment);
    CHECK(Owned.Released == 0);

    for (i = 0; i < 32; i++)
        CHECK(UserRead64(&World, 0, Base + i * 4096, &Status) == *(ULONG64 *)(Owned.File.Data + i * 4096));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + 0x3000, 0x0DDBA11)));
    CHECK(Owned.File.Reads == 32 && Owned.Released == 0);
    CHECK(Owned.File.IoUnderLock == 0);

    CHECK(NT_SUCCESS(MiUnmapView(&A, Base)));
    CHECK(Owned.Released == 1 && Owned.ReadsAfterRelease == 0);
    CHECK(*(ULONG64 *)(Owned.File.Data + 0x3000) == 0x0DDBA11);
    CHECK(IsListEmpty(&World.System.SegmentList));

    SpaceDestroy(&World, 0, &A);
    WorldExpectClean(&World, 512);
    FileDestroy(&Owned.File);
    WorldDestroy(&World);
}

static
void
SectionUnusedCache(void)
{
    static MI_FILE_OPS OwnedOps = { .Read = OwnedRead, .Write = OwnedWrite, .Release = OwnedRelease };
    TEST_WORLD World;
    MI_ADDRESS_SPACE A;
    OWNED_FILE Owned[3];
    PMI_SEGMENT Segment[3];
    ULONG64 Base;
    NTSTATUS Status;
    ULONG i;

    memset(Owned, 0, sizeof(Owned));
    WorldCreate(&World, 512, 1, 1000);
    World.System.UnusedSegmentLimit = 2;
    SpaceCreate(&World, 0, &A);

    for (i = 0; i < 3; i++)
    {
        FileCreate(&Owned[i].File, 8 * 4096);
        CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, 8 * 4096, MI_PROT_READWRITE, &OwnedOps,
                                         &Owned[i], NULL, 0, &Segment[i])));
    }

    Base = 0;
    CHECK(NT_SUCCESS(Map(&A, Segment[0], &Base, 0, 0, MI_PROT_READWRITE)));
    CHECK(UserRead64(&World, 0, Base + 0x2000, &Status) == *(ULONG64 *)(Owned[0].File.Data + 0x2000));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + 0x3000, 0xCAC4ED)));
    CHECK(Owned[0].File.Reads == 2);
    CHECK(NT_SUCCESS(MiUnmapView(&A, Base)));
    MiSegmentDereference(Segment[0]);

    CHECK(Owned[0].Released == 0);
    CHECK(World.System.UnusedSegmentCount == 1 && Segment[0]->State == MI_SEGMENT_UNUSED);

    CHECK(MiSegmentTryReference(Segment[0]));
    CHECK(World.System.UnusedSegmentCount == 0 && Segment[0]->State == MI_SEGMENT_ACTIVE);
    CHECK(MI_ATOMIC_READ64(&World.System.UnusedSegmentHits) == 1);

    Base = 0;
    CHECK(NT_SUCCESS(Map(&A, Segment[0], &Base, 0, 0, MI_PROT_READWRITE)));
    CHECK(UserRead64(&World, 0, Base + 0x2000, &Status) == *(ULONG64 *)(Owned[0].File.Data + 0x2000));
    CHECK(UserRead64(&World, 0, Base + 0x3000, &Status) == 0xCAC4ED);
    CHECK(Owned[0].File.Reads == 2);
    CHECK(NT_SUCCESS(MiUnmapView(&A, Base)));
    MiSegmentDereference(Segment[0]);
    CHECK(World.System.UnusedSegmentCount == 1);

    MiSegmentDereference(Segment[1]);
    CHECK(World.System.UnusedSegmentCount == 2 && Owned[0].Released == 0 && Owned[1].Released == 0);

    MiSegmentDereference(Segment[2]);
    CHECK(World.System.UnusedSegmentCount == 2);
    CHECK(Owned[0].Released == 1 && Owned[1].Released == 0 && Owned[2].Released == 0);
    CHECK(*(ULONG64 *)(Owned[0].File.Data + 0x3000) == 0xCAC4ED);

    CHECK(MiSegmentTryReference(Segment[1]));
    CHECK(MiSegmentDereferenceAndClose(Segment[1]));
    CHECK(Owned[1].Released == 1 && World.System.UnusedSegmentCount == 1);

    CHECK(MiSegmentTryReference(Segment[2]));
    MiSegmentReference(Segment[2]);
    CHECK(!MiSegmentDereferenceAndClose(Segment[2]));
    CHECK(Owned[2].Released == 0);
    MiSegmentDereference(Segment[2]);
    CHECK(World.System.UnusedSegmentCount == 1);

    CHECK(MiSegmentPurgeUnused(&World.System, ~0u) == 1);
    CHECK(Owned[2].Released == 1 && World.System.UnusedSegmentCount == 0);
    CHECK(IsListEmpty(&World.System.SegmentList));

    SpaceDestroy(&World, 0, &A);
    WorldExpectClean(&World, 512);
    for (i = 0; i < 3; i++)
        FileDestroy(&Owned[i].File);
    WorldDestroy(&World);
}

static
void
SectionViewProtection(void)
{
    TEST_WORLD World;
    TEST_FILE File;
    MI_ADDRESS_SPACE Space;
    MI_MEMORY_INFORMATION Info;
    PMI_SEGMENT Segment;
    ULONG64 Base = 0;
    ULONG64 Size = 2 * PAGE_SIZE;
    NTSTATUS Status;
    ULONG Old;

    WorldCreate(&World, 512, 1, 100000);
    World.Machine.StrictTlb = TRUE;
    FileCreate(&File, Size);
    SpaceCreate(&World, 0, &Space);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, Size, MI_PROT_EXECUTE_READWRITE,
                                     &TestFileOps, &File, NULL, 0, &Segment)));

    CHECK(MiMapViewEx(&Space, Segment, &Base, 0, &Size, MI_PROT_READONLY, 0, ~0ULL, MI_PROT_EXECUTE, TRUE) ==
          STATUS_SECTION_PROTECTION);
    CHECK(MiMapViewEx(&Space, Segment, &Base, 0, &Size, MI_PROT_WRITECOPY, 0, ~0ULL, MI_PROT_EXECUTE, TRUE) ==
          STATUS_SECTION_PROTECTION);
    CHECK(NT_SUCCESS(MiMapViewEx(&Space, Segment, &Base, 0, &Size, MI_PROT_EXECUTE, 0, ~0ULL, MI_PROT_EXECUTE, TRUE)));
    CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));

    Base = 0;
    CHECK(NT_SUCCESS(Map(&Space, Segment, &Base, 0, Size, MI_PROT_NOACCESS)));
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base, &Info)));
    CHECK(Info.Protect == MI_PROT_NOACCESS && Info.AllocationProtect == MI_PROT_NOACCESS);
    UserRead64(&World, 0, Base, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION && File.Reads == 0);
    CHECK(UserWrite64(&World, 0, Base, 1) == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Space, &Base, &Size, MI_PROT_READWRITE, &Old)));
    CHECK(Old == MI_PROT_NOACCESS);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base, 0xC0FFEE)));
    CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));

    Base = 0;
    CHECK(NT_SUCCESS(Map(&Space, Segment, &Base, 0, Size, MI_PROT_READWRITE | MI_PROT_GUARD)));
    CHECK(UserWrite64(&World, 0, Base, 1) == STATUS_GUARD_PAGE_VIOLATION);
    CHECK(UserRead64(&World, 0, Base, &Status) == 0xC0FFEE && NT_SUCCESS(Status));
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base, &Info)));
    CHECK(Info.Protect == MI_PROT_READWRITE);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + PAGE_SIZE, &Info)));
    CHECK(Info.Protect == (MI_PROT_READWRITE | MI_PROT_GUARD));
    UserRead64(&World, 0, Base + PAGE_SIZE, &Status);
    CHECK(Status == STATUS_GUARD_PAGE_VIOLATION);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + PAGE_SIZE, 0xFEED)));
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Space, &Base, &Size, MI_PROT_READWRITE | MI_PROT_GUARD, &Old)));
    UserRead64(&World, 0, Base, &Status);
    CHECK(Status == STATUS_GUARD_PAGE_VIOLATION);
    CHECK(UserRead64(&World, 0, Base, &Status) == 0xC0FFEE && NT_SUCCESS(Status));
    CHECK(MiTrimAddressSpace(&Space, 2, TRUE) != 0);
    CHECK(UserRead64(&World, 0, Base, &Status) == 0xC0FFEE && NT_SUCCESS(Status));
    CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));

    Base = 0;
    CHECK(NT_SUCCESS(Map(&Space, Segment, &Base, 0, Size, MI_PROT_WRITECOPY)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base, 0xC0FFEE42)));
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Space, &Base, &(ULONG64){PAGE_SIZE},
                                            MI_PROT_WRITECOPY | MI_PROT_GUARD, &Old)));
    UserRead64(&World, 0, Base, &Status);
    CHECK(Status == STATUS_GUARD_PAGE_VIOLATION);
    CHECK(UserRead64(&World, 0, Base, &Status) == 0xC0FFEE42 && NT_SUCCESS(Status));
    CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));

    MiSegmentDereference(Segment);
    SpaceDestroy(&World, 0, &Space);
    WorldExpectClean(&World, 512);
    FileDestroy(&File);
    WorldDestroy(&World);
}

typedef struct _CLUSTER_FILE
{
    TEST_FILE File;
    ULONG Calls, Count;
    ULONG64 Offset;
    BOOLEAN Fail;
} CLUSTER_FILE;

static NTSTATUS
ClusterRead(PVOID Context, ULONG64 Offset, const ULONG *Frames, ULONG Count)
{
    CLUSTER_FILE *File = Context;
    ULONG i;

    File->Calls++;
    File->Count = Count;
    File->Offset = Offset;
    CHECK(Count > 1 && Count <= MI_MAX_FILE_IO_PAGES);
    CHECK(Offset + Count * PAGE_SIZE <= File->File.Size);
    if (File->Fail)
        return STATUS_UNEXPECTED_IO_ERROR;
    for (i = 0; i < Count; i++)
    {
        PVOID Buffer = MiArchMapFrame(Frames[i]);
        memcpy(Buffer, File->File.Data + Offset + i * PAGE_SIZE, PAGE_SIZE);
        MiArchUnmapFrame(Buffer);
    }
    return STATUS_SUCCESS;
}

static void
SectionClusterRead(void)
{
    TEST_WORLD World;
    CLUSTER_FILE File = {0};
    MI_FILE_OPS Ops = TestFileOps;
    MI_ADDRESS_SPACE Space;
    PMI_SEGMENT Segment;
    ULONG64 Base = 0;
    NTSTATUS Status;
    ULONG i;
    MI_SEGMENT_LAYOUT Layout[] =
    {
        {0, 1, 0, 1024, MI_PROT_READONLY},
        {1, 4, 1024, 3 * PAGE_SIZE + 37, MI_PROT_READONLY},
    };

    WorldCreate(&World, 512, 1, 100000);
    FileCreate(&File.File, 20 * PAGE_SIZE + 37);
    SpaceCreate(&World, 0, &Space);
    Ops.ReadPages = ClusterRead;
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, File.File.Size,
                                     MI_PROT_READONLY, &Ops, &File, NULL, 0, &Segment)));
    CHECK(NT_SUCCESS(Map(&Space, Segment, &Base, 0, 0, MI_PROT_READONLY)));
    for (i = 2; i < 20; i++)
    {
        CHECK(UserRead64(&World, 0, Base + i * PAGE_SIZE, &Status) ==
              *(ULONG64 *)(File.File.Data + i * PAGE_SIZE));
        CHECK(NT_SUCCESS(Status));
    }
    CHECK(File.Calls == 2 && File.Count == 2 && File.Offset == 18 * PAGE_SIZE);
    CHECK(File.File.Reads == 0);
    CHECK(UserRead64(&World, 0, Base + 20 * PAGE_SIZE, &Status) ==
          *(ULONG64 *)(File.File.Data + 20 * PAGE_SIZE));
    CHECK(UserRead64(&World, 0, Base + 20 * PAGE_SIZE + 40, &Status) == 0);
    CHECK(File.File.Reads == 1 && File.Calls == 2);
    CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));
    MiSegmentDereference(Segment);

    /* Failed read-ahead must not poison a readable faulting page. */
    Base = 0; File.Fail = TRUE; File.Calls = 0;
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, File.File.Size,
                                     MI_PROT_READONLY, &Ops, &File, NULL, 0, &Segment)));
    CHECK(NT_SUCCESS(Map(&Space, Segment, &Base, 0, 0, MI_PROT_READONLY)));
    CHECK(UserRead64(&World, 0, Base, &Status) == *(ULONG64 *)File.File.Data);
    CHECK(NT_SUCCESS(Status) && File.Calls == 1);
    CHECK(!MiSegmentIsResident(Segment, PAGE_SIZE, PAGE_SIZE));
    CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));
    MiSegmentDereference(Segment);

    /* Cache reads may not bring backing bytes beyond valid data into memory. */
    Base = 0; File.Fail = FALSE; File.Calls = 0;
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, File.File.Size,
                                     MI_PROT_READONLY, &Ops, &File, NULL, 0, &Segment)));
    CHECK(NT_SUCCESS(Map(&Space, Segment, &Base, 0, 0, MI_PROT_READONLY)));
    CHECK(NT_SUCCESS(MiSegmentMakeResidentBeyond(Segment, 0, 4 * PAGE_SIZE, 2 * PAGE_SIZE)));
    CHECK(File.Calls == 1 && File.Count == 2);
    CHECK(UserRead64(&World, 0, Base + 2 * PAGE_SIZE, &Status) == 0);
    CHECK(UserRead64(&World, 0, Base + 3 * PAGE_SIZE, &Status) == 0);
    CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));
    MiSegmentDereference(Segment);

    /* Image raw offsets can be 512-byte aligned; stop before a partial tail. */
    Base = 0; File.Calls = 0;
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentImage, 5 * PAGE_SIZE,
                                     MI_PROT_READONLY, &Ops, &File, Layout, 2, &Segment)));
    CHECK(NT_SUCCESS(Map(&Space, Segment, &Base, 0, 0, MI_PROT_READONLY)));
    CHECK(UserRead64(&World, 0, Base + PAGE_SIZE, &Status) == *(ULONG64 *)(File.File.Data + 1024));
    CHECK(NT_SUCCESS(Status) && File.Calls == 1 && File.Count == 3 && File.Offset == 1024);
    CHECK(UserRead64(&World, 0, Base + 4 * PAGE_SIZE, &Status) ==
          *(ULONG64 *)(File.File.Data + 1024 + 3 * PAGE_SIZE));
    CHECK(UserRead64(&World, 0, Base + 4 * PAGE_SIZE + 40, &Status) == 0);
    CHECK(File.Calls == 1);
    CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));
    MiSegmentDereference(Segment);

    SpaceDestroy(&World, 0, &Space);
    WorldExpectClean(&World, 512);
    FileDestroy(&File.File);
    WorldDestroy(&World);
}

void
TestSection(void)
{
    SectionWriterAdmission();
    SectionVadCache();
    SectionConcurrentChurn();
    SectionUnmapBatch();
    SectionSharedDataFile();
    SectionPageFileBacked();
    SectionReservedPageFile();
    SectionCopyOnWrite();
    SectionSystemSpaceView();
    SectionViewOutlivesOwner();
    SectionUnusedCache();
    SectionViewProtection();
    SectionClusterRead();
}

void
TestImage(void)
{
    static MI_SEGMENT_LAYOUT Layout[] =
    {
        { 0, 1, 0, 0x400, MI_PROT_READONLY },
        { 1, 4, 0x400, 0x3A00, MI_PROT_EXECUTE_READ },
        { 5, 4, 0x3E00, 0x1234, MI_PROT_READWRITE },
        { 10, 2, 0x5200, 0x2000, MI_PROT_READONLY },
    };
    TEST_WORLD World;
    TEST_FILE File;
    MI_ADDRESS_SPACE A, B;
    MI_MEMORY_INFORMATION Info;
    PMI_SEGMENT Image;
    ULONG64 BaseA = 0, BaseB = 0;
    NTSTATUS Status;
    ULONG Old;

    WorldCreate(&World, 1024, 2, 100000);
    World.Machine.StrictTlb = TRUE;
    WorldAttachPageFile(&World, 256);
    FileCreate(&File, 9 * 4096);
    SpaceCreate(&World, 0, &A);
    SpaceCreate(&World, 1, &B);

    CHECK(MiSegmentCreate(&World.System, MiSegmentImage, 12 * 4096, MI_PROT_EXECUTE_READ, &TestFileOps, &File, NULL,
                          0, &Image) == STATUS_INVALID_PARAMETER);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentImage, 12 * 4096, MI_PROT_EXECUTE_READ, &TestFileOps,
                                     &File, Layout, 4, &Image)));
    CHECK(MiSegmentExtend(Image, 64 * 4096) == STATUS_INVALID_PARAMETER);
    CHECK(NT_SUCCESS(Map(&A, Image, &BaseA, 0, 0, MI_PROT_READONLY)));
    CHECK(NT_SUCCESS(Map(&B, Image, &BaseB, 0, 0, MI_PROT_READONLY)));
    MiSegmentDereference(Image);

    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, BaseA, &Info)));
    CHECK(Info.Type == MI_MEM_IMAGE && Info.Protect == MI_PROT_READONLY && Info.RegionSize == 0x1000);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, BaseA + 0x1000, &Info)));
    CHECK(Info.Protect == MI_PROT_EXECUTE_READ && Info.RegionSize == 0x4000);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, BaseA + 0x5000, &Info)));
    CHECK(Info.Protect == MI_PROT_WRITECOPY && Info.RegionSize == 0x4000);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&A, BaseA + 0x9000, &Info)));
    CHECK(Info.Protect == MI_PROT_NOACCESS && Info.RegionSize == 0x1000);

    CHECK(NT_SUCCESS(MachineTouch(&World.Machine, 0, BaseA + 0x2000, MachineExecute, TRUE)));
    CHECK(MachineTouch(&World.Machine, 0, BaseA, MachineExecute, TRUE) == STATUS_ACCESS_VIOLATION);
    CHECK(UserWrite64(&World, 0, BaseA + 0x2000, 1) == STATUS_ACCESS_VIOLATION);
    CHECK(UserRead64(&World, 0, BaseA + 0x2010, &Status) == *(ULONG64 *)(File.Data + 0x1410));
    CHECK(UserRead64(&World, 0, BaseA + 0x3F8, &Status) == *(ULONG64 *)(File.Data + 0x3F8));
    CHECK(UserRead64(&World, 0, BaseA + 0x400, &Status) == 0 && NT_SUCCESS(Status));
    CHECK(UserRead64(&World, 0, BaseA + 0x49F8, &Status) == *(ULONG64 *)(File.Data + 0x400 + 0x39F8));
    CHECK(UserRead64(&World, 0, BaseA + 0x4A00, &Status) == 0 && NT_SUCCESS(Status));
    CHECK(UserRead64(&World, 1, BaseB + 0x6228, &Status) == *(ULONG64 *)(File.Data + 0x4E00 + 0x228));
    CHECK(UserRead64(&World, 1, BaseB + 0x6238, &Status) == 0 && NT_SUCCESS(Status));
    CHECK(UserRead64(&World, 0, BaseA + 0xA010, &Status) == *(ULONG64 *)(File.Data + 0x5210));
    UserRead64(&World, 0, BaseA + 0x9000, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);

    CHECK(UserRead64(&World, 0, BaseA + 0x7000, &Status) == 0 && NT_SUCCESS(Status));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, BaseA + 0x7000, 0xB55)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, BaseA + 0x5000, 0xDA7A)));
    CHECK(UserRead64(&World, 1, BaseB + 0x7000, &Status) == 0);
    CHECK(UserRead64(&World, 1, BaseB + 0x5000, &Status) == *(ULONG64 *)(File.Data + 0x3E00));
    CHECK(MI_ATOMIC_READ64(&A.CopyOnWriteFaults) == 2 && MI_ATOMIC_READ64(&A.CommittedPages) == 2);

    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&A, &(ULONG64){BaseA + 0x1000}, &(ULONG64){0x1000},
                                            MI_PROT_EXECUTE_READWRITE, &Old)));
    CHECK(Old == MI_PROT_EXECUTE_READ);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, BaseA + 0x1000, 0x9090909090909090ULL)));
    CHECK(NT_SUCCESS(MachineTouch(&World.Machine, 0, BaseA + 0x1000, MachineExecute, TRUE)));
    CHECK(UserRead64(&World, 1, BaseB + 0x1000, &Status) == *(ULONG64 *)(File.Data + 0x400));
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&A, &(ULONG64){BaseA + 0x1000}, &(ULONG64){0x1000},
                                            MI_PROT_EXECUTE_READ, &Old)));
    CHECK(Old == MI_PROT_EXECUTE_READWRITE);
    CHECK(UserWrite64(&World, 0, BaseA + 0x1000, 1) == STATUS_ACCESS_VIOLATION);
    CHECK(UserRead64(&World, 0, BaseA + 0x1000, &Status) == 0x9090909090909090ULL);

    CHECK(MiTrimAddressSpace(&A, 1000, TRUE) != 0);
    MiWriteModifiedPages(&World.System, 1000);
    CHECK(File.Writes == 0);
    CHECK(UserRead64(&World, 0, BaseA + 0x1000, &Status) == 0x9090909090909090ULL);
    CHECK(UserRead64(&World, 0, BaseA + 0x7000, &Status) == 0xB55);

    SpaceDestroy(&World, 0, &A);
    SpaceDestroy(&World, 1, &B);
    CHECK(IsListEmpty(&World.System.SegmentList));
    WorldExpectClean(&World, 1024);
    FileDestroy(&File);
    WorldDestroy(&World);
}
