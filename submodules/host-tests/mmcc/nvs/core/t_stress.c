/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_stress.c
 * PURPOSE:     Concurrent memory manager host-native stress tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include <unistd.h>

#if defined(TEST_LIGHT)
#define STRESS_THREADS     4
#define STRESS_ITERATIONS  1500
#define STRESS_FRAMES      512
#else
#define STRESS_THREADS     6
#define STRESS_ITERATIONS  12000
#define STRESS_FRAMES      1024
#endif

#define STRESS_REGIONS     12
#define STRESS_SHARED_PAGES 64

typedef struct _STRESS_SHARED
{
    TEST_WORLD *World;
    PMI_SEGMENT Shared;
    PMI_SEGMENT FileSegment;
    TEST_FILE *File;
    PMI_ADDRESS_SPACE Spaces[STRESS_THREADS];
    volatile LONG Stop;
    volatile LONG64 Trimmed;
    volatile LONG64 Written;
} STRESS_SHARED;

typedef struct _STRESS_THREAD
{
    STRESS_SHARED *Shared;
    ULONG Index;
    MI_ADDRESS_SPACE Space;
    ULONG64 Operations;
    ULONG64 Retries;
} STRESS_THREAD;

static
NTSTATUS
StressWrite(STRESS_THREAD *Thread, ULONG64 Va, ULONG64 Value)
{
    NTSTATUS Status;
    ULONG Attempt;

    for (Attempt = 0; Attempt < 100000; Attempt++)
    {
        Status = UserWrite64(Thread->Shared->World, Thread->Index, Va, Value);
        if (Status != STATUS_NO_MEMORY)
            return Status;

        Thread->Retries++;
        sched_yield();
    }

    return Status;
}

static
ULONG64
StressRead(STRESS_THREAD *Thread, ULONG64 Va, NTSTATUS *Status)
{
    ULONG64 Value = 0;
    ULONG Attempt;

    for (Attempt = 0; Attempt < 100000; Attempt++)
    {
        Value = UserRead64(Thread->Shared->World, Thread->Index, Va, Status);
        if (*Status != STATUS_NO_MEMORY)
            return Value;

        Thread->Retries++;
        sched_yield();
    }

    return Value;
}

static
void *
StressWorker(void *Argument)
{
    STRESS_THREAD *Thread = Argument;
    STRESS_SHARED *Shared = Thread->Shared;
    TEST_WORLD *World = Shared->World;
    ULONG64 Seed = 0x9E3779B97F4A7C15ULL * (Thread->Index + 1);
    ULONG64 Region[STRESS_REGIONS] = { 0 };
    ULONG RegionPages[STRESS_REGIONS] = { 0 };
    ULONG64 SharedBase = 0;
    ULONG64 FileBase = 0;
    ULONG64 ViewSize = 0;
    NTSTATUS Status;
    ULONG i;

    MiHostCpu = Thread->Index;
    for (i = 0; i < 100000; i++)
    {
        Status = MiAddressSpaceCreate(&World->System, &Thread->Space);
        if (Status != STATUS_NO_MEMORY)
            break;
        Thread->Retries++;
        sched_yield();
    }
    CHECK(NT_SUCCESS(Status));
    if (!NT_SUCCESS(Status))
        abort();
    WorldAttach(World, Thread->Index, &Thread->Space);
    __atomic_store_n(&Shared->Spaces[Thread->Index], &Thread->Space, __ATOMIC_SEQ_CST);

    CHECK(NT_SUCCESS(MiMapView(&Thread->Space, Shared->Shared, &SharedBase, 0, &ViewSize, MI_PROT_READWRITE, 0)));
    ViewSize = 0;
    CHECK(NT_SUCCESS(MiMapView(&Thread->Space, Shared->FileSegment, &FileBase, 0, &ViewSize, MI_PROT_READWRITE, 0)));

    for (i = 0; i < STRESS_ITERATIONS; i++)
    {
        ULONG Slot = (ULONG)(Rng(&Seed) % STRESS_REGIONS);
        ULONG Action = (ULONG)(Rng(&Seed) % 10);

        if (Region[Slot] == 0)
        {
            ULONG64 Size = ((Rng(&Seed) % 32) + 1) * PAGE_SIZE;
            ULONG64 Base = 0;

            Status = MiAllocateVirtualMemory(&Thread->Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                             MI_PROT_READWRITE);
            CHECK(NT_SUCCESS(Status));
            if (NT_SUCCESS(Status))
            {
                ULONG Page;

                Region[Slot] = Base;
                RegionPages[Slot] = (ULONG)(Size >> PAGE_SHIFT);
                for (Page = 0; Page < RegionPages[Slot]; Page++)
                {
                    Status = StressWrite(Thread, Base + (ULONG64)Page * PAGE_SIZE, Base + Page);
                    if (!NT_SUCCESS(Status) && getenv("MM_DEBUG"))
                    {
                        PMI_PFN_DATABASE Db = &World->System.Pfn;

                        fprintf(stderr, "status %x zero %llu free %llu standby %llu modified %llu avail %llu inuse %llu\n",
                                Status, MiPfnListCount(Db, MiPageZeroed), MiPfnListCount(Db, MiPageFree),
                                MiPfnListCount(Db, MiPageStandby), MiPfnListCount(Db, MiPageModified),
                                MiPfnAvailablePages(Db), World->Paging.PageFile.SlotsInUse);
                        fprintf(stderr, "trimmed %lld written %lld resident", (LONG64)Shared->Trimmed,
                                (LONG64)Shared->Written);
                        for (ULONG t = 0; t < STRESS_THREADS; t++)
                        {
                            PMI_ADDRESS_SPACE Other = Shared->Spaces[t];

                            fprintf(stderr, " [%lld r %lld pt]", Other ? (LONG64)Other->ResidentPages : -1,
                                    Other ? (LONG64)Other->PageTablePages : -1);
                        }
                        fprintf(stderr, "\n");
                        sleep(getenv("MM_HANG") ? 25 : 1);
                        fprintf(stderr, "after 1s: trimmed %lld written %lld avail %llu\n", (LONG64)Shared->Trimmed,
                                (LONG64)Shared->Written, MiPfnAvailablePages(Db));
                        abort();
                    }
                    CHECK(NT_SUCCESS(Status));
                }
            }
        }
        else if (Action < 5)
        {
            ULONG Page = (ULONG)(Rng(&Seed) % RegionPages[Slot]);
            ULONG64 Va = Region[Slot] + (ULONG64)Page * PAGE_SIZE;

            CHECK(StressRead(Thread, Va, &Status) == Region[Slot] + Page);
            CHECK(NT_SUCCESS(Status));
        }
        else if (Action < 7)
        {
            ULONG Page = (ULONG)(Rng(&Seed) % STRESS_SHARED_PAGES);
            ULONG64 Va = SharedBase + (ULONG64)Page * PAGE_SIZE + Thread->Index * 16;
            ULONG64 Value = ((ULONG64)Thread->Index << 56) | i;

            CHECK(NT_SUCCESS(StressWrite(Thread, Va, Value)));
            CHECK(StressRead(Thread, Va, &Status) == Value);
        }
        else if (Action < 9)
        {
            ULONG Page = (ULONG)(Rng(&Seed) % STRESS_SHARED_PAGES);
            ULONG64 Va = FileBase + (ULONG64)Page * PAGE_SIZE + Thread->Index * 16;
            ULONG64 Value = ((ULONG64)Thread->Index << 48) | Page;

            CHECK(NT_SUCCESS(StressWrite(Thread, Va, Value)));
            CHECK(StressRead(Thread, Va, &Status) == Value);
        }
        else
        {
            ULONG64 Base = Region[Slot];
            ULONG64 Size = 0;

            CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Thread->Space, &Base, &Size, MI_MEM_RELEASE)));
            Region[Slot] = 0;
        }

        Thread->Operations++;
    }

    for (i = 0; i < STRESS_SHARED_PAGES; i++)
    {
        ULONG64 Value = StressRead(Thread, FileBase + (ULONG64)i * PAGE_SIZE + Thread->Index * 16, &Status);

        CHECK(NT_SUCCESS(Status));
        CHECK(Value == 0 || Value == (((ULONG64)Thread->Index << 48) | i) ||
              Value == *(ULONG64 *)(Shared->File->Data + (ULONG64)i * PAGE_SIZE + Thread->Index * 16));
    }

    __atomic_store_n(&Shared->Spaces[Thread->Index], NULL, __ATOMIC_SEQ_CST);
    return NULL;
}

static
void *
StressReclaimer(void *Argument)
{
    STRESS_SHARED *Shared = Argument;
    ULONG Round = 0;

    MiHostCpu = MACHINE_MAX_CPUS - 1;

    while (!__atomic_load_n(&Shared->Stop, __ATOMIC_SEQ_CST))
    {
        PMI_ADDRESS_SPACE Space = __atomic_load_n(&Shared->Spaces[Round++ % STRESS_THREADS], __ATOMIC_SEQ_CST);

        if (Space != NULL && MiPfnAvailablePages(&Shared->World->System.Pfn) < STRESS_FRAMES / 4)
            __sync_fetch_and_add(&Shared->Trimmed, MiTrimAddressSpace(Space, 64, (BOOLEAN)(Round & 1)));

        __sync_fetch_and_add(&Shared->Written, MiWriteModifiedPages(&Shared->World->System, 64));

        if ((Round & 63) == 0)
            MiSegmentFlush(Shared->FileSegment, 0, (ULONG64)STRESS_SHARED_PAGES * PAGE_SIZE);

        sched_yield();
    }

    return NULL;
}

#if defined(TEST_LIGHT)
#define SHARED_SPACE_THREADS     3
#define SHARED_SPACE_ITERATIONS  150
#else
#define SHARED_SPACE_THREADS     4
#define SHARED_SPACE_ITERATIONS  1500
#endif

typedef struct _SHARED_SPACE_THREAD
{
    TEST_WORLD *World;
    PMI_ADDRESS_SPACE Space;
    ULONG Index;
    ULONG64 Retries;
} SHARED_SPACE_THREAD;

static
BOOLEAN
SharedSpaceWrite(SHARED_SPACE_THREAD *Thread, ULONG64 Va, ULONG64 Value)
{
    NTSTATUS Status = STATUS_NO_MEMORY;
    ULONG Attempt;

    for (Attempt = 0; Attempt < 100000 && Status == STATUS_NO_MEMORY; Attempt++)
    {
        Status = UserWrite64(Thread->World, Thread->Index, Va, Value);
        if (Status == STATUS_NO_MEMORY)
            sched_yield();
    }

    return (BOOLEAN)NT_SUCCESS(Status);
}

static
ULONG64
SharedSpaceRead(SHARED_SPACE_THREAD *Thread, ULONG64 Va, NTSTATUS *Status)
{
    ULONG64 Value = 0;
    ULONG Attempt;

    *Status = STATUS_NO_MEMORY;
    for (Attempt = 0; Attempt < 100000 && *Status == STATUS_NO_MEMORY; Attempt++)
    {
        Value = UserRead64(Thread->World, Thread->Index, Va, Status);
        if (*Status == STATUS_NO_MEMORY)
            sched_yield();
    }

    return Value;
}

static
void *
SharedSpaceWorker(void *Argument)
{
    SHARED_SPACE_THREAD *Thread = Argument;
    ULONG64 Seed = 0xD1B54A32D192ED03ULL * (Thread->Index + 1);
    NTSTATUS Status;
    ULONG Iteration;

    MiHostCpu = Thread->Index;
    WorldAttach(Thread->World, Thread->Index, Thread->Space);

    for (Iteration = 0; Iteration < SHARED_SPACE_ITERATIONS; Iteration++)
    {
        ULONG Pages = (ULONG)(Rng(&Seed) % 40) + 8;
        ULONG First = Pages / 4;
        ULONG Last = Pages - Pages / 4;
        ULONG64 Tag = ((ULONG64)(Thread->Index + 1) << 56) | ((ULONG64)Iteration << 16);
        ULONG64 Size = (ULONG64)Pages * PAGE_SIZE;
        ULONG64 Base = 0;
        ULONG64 Part;
        ULONG64 PartSize;
        ULONG i;

        Status = MiAllocateVirtualMemory(Thread->Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                         MI_PROT_READWRITE);
        if (Status == STATUS_NO_MEMORY || Status == STATUS_COMMITMENT_LIMIT)
        {
            Thread->Retries++;
            sched_yield();
            continue;
        }

        CHECK(NT_SUCCESS(Status));

        for (i = 0; i < Pages; i++)
            CHECK(SharedSpaceWrite(Thread, Base + (ULONG64)i * PAGE_SIZE + 8 * (i & 31), Tag | i));

        for (i = 0; i < Pages; i++)
            CHECK(SharedSpaceRead(Thread, Base + (ULONG64)i * PAGE_SIZE + 8 * (i & 31), &Status) == (Tag | i));

        Part = Base + (ULONG64)First * PAGE_SIZE;
        PartSize = (ULONG64)(Last - First) * PAGE_SIZE;
        CHECK(NT_SUCCESS(MiFreeVirtualMemory(Thread->Space, &Part, &PartSize, MI_MEM_DECOMMIT)));

        for (i = 0; i < Pages; i++)
        {
            ULONG64 Value = SharedSpaceRead(Thread, Base + (ULONG64)i * PAGE_SIZE + 8 * (i & 31), &Status);

            if (i >= First && i < Last)
                CHECK(Status == STATUS_ACCESS_VIOLATION);
            else
                CHECK(NT_SUCCESS(Status) && Value == (Tag | i));
        }

        Part = Base + (ULONG64)First * PAGE_SIZE;
        PartSize = (ULONG64)(Last - First) * PAGE_SIZE;
        Status = MiAllocateVirtualMemory(Thread->Space, &Part, &PartSize, MI_MEM_COMMIT, MI_PROT_READWRITE);

        if (NT_SUCCESS(Status))
        {
            for (i = First; i < Last; i++)
            {
                CHECK(SharedSpaceRead(Thread, Base + (ULONG64)i * PAGE_SIZE + 8 * (i & 31), &Status) == 0);
                CHECK(SharedSpaceWrite(Thread, Base + (ULONG64)i * PAGE_SIZE + 8 * (i & 31), ~(Tag | i)));
            }

            for (i = 0; i < Pages; i++)
            {
                ULONG64 Expected = (i >= First && i < Last) ? ~(Tag | i) : (Tag | i);

                CHECK(SharedSpaceRead(Thread, Base + (ULONG64)i * PAGE_SIZE + 8 * (i & 31), &Status) == Expected);
            }
        }

        Size = 0;
        CHECK(NT_SUCCESS(MiFreeVirtualMemory(Thread->Space, &Base, &Size, MI_MEM_RELEASE)));
    }

    WorldAttach(Thread->World, Thread->Index, NULL);
    return NULL;
}

static
void
StressSharedSpace(void)
{
    static TEST_WORLD World;
    static MI_ADDRESS_SPACE Space;
    SHARED_SPACE_THREAD Threads[SHARED_SPACE_THREADS];
    pthread_t Workers[SHARED_SPACE_THREADS];
    ULONG64 Retries = 0;
    ULONG i;

    WorldCreate(&World, 1024, MACHINE_MAX_CPUS, 1000000);
    World.Machine.StrictTlb = TRUE;
    CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World.System, &Space)));

    for (i = 0; i < SHARED_SPACE_THREADS; i++)
    {
        Threads[i].World = &World;
        Threads[i].Space = &Space;
        Threads[i].Index = i;
        Threads[i].Retries = 0;
        pthread_create(&Workers[i], NULL, SharedSpaceWorker, &Threads[i]);
    }

    for (i = 0; i < SHARED_SPACE_THREADS; i++)
    {
        pthread_join(Workers[i], NULL);
        Retries += Threads[i].Retries;
    }

    MiHostCpu = 0;
    CHECK(MI_ATOMIC_READ64(&Space.CommittedPages) == 0);
    CHECK(MI_ATOMIC_READ64(&Space.ResidentPages) == 0);
    CHECK(MI_ATOMIC_READ64(&Space.PrivatePages) == 0);
    CHECK(Space.VadRoot.NodeCount == 0);
    CHECK(MiPtCheck(&Space) == 0);
    CHECK(World.Machine.StaleTlbUses == 0 && World.Machine.BadFrameAccesses == 0);
    MiAddressSpaceDestroy(&Space);

    printf("  shared-space stress: %u threads, %llu oom-retries, faults %lld\n", SHARED_SPACE_THREADS, Retries,
           (LONG64)World.Machine.Faults);

    WorldExpectClean(&World, 1024);
    WorldDestroy(&World);
}

void
TestStress(void)
{
    static TEST_WORLD World;
    static STRESS_THREAD Threads[STRESS_THREADS];
    STRESS_SHARED Shared;
    TEST_FILE File;
    pthread_t Workers[STRESS_THREADS];
    pthread_t Reclaimer;
    ULONG64 Operations = 0;
    ULONG64 Retries = 0;
    ULONG i;

    memset(&Shared, 0, sizeof(Shared));
    WorldCreate(&World, STRESS_FRAMES, MACHINE_MAX_CPUS, 1000000);
    World.Machine.StrictTlb = FALSE;
    WorldAttachPageFile(&World, 16384);
    FileCreate(&File, (ULONG64)STRESS_SHARED_PAGES * PAGE_SIZE);
    memset(File.Data, 0, File.Size);

    Shared.World = &World;
    Shared.File = &File;
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentPageFileBacked, (ULONG64)STRESS_SHARED_PAGES * PAGE_SIZE,
                                     MI_PROT_READWRITE, NULL, NULL, NULL, 0, &Shared.Shared)));
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, File.Size, MI_PROT_READWRITE, &TestFileOps,
                                     &File, NULL, 0, &Shared.FileSegment)));

    pthread_create(&Reclaimer, NULL, StressReclaimer, &Shared);

    for (i = 0; i < STRESS_THREADS; i++)
    {
        Threads[i].Shared = &Shared;
        Threads[i].Index = i;
        pthread_create(&Workers[i], NULL, StressWorker, &Threads[i]);
    }

    for (i = 0; i < STRESS_THREADS; i++)
        pthread_join(Workers[i], NULL);

    __atomic_store_n(&Shared.Stop, 1, __ATOMIC_SEQ_CST);
    pthread_join(Reclaimer, NULL);

    for (i = 0; i < STRESS_THREADS; i++)
    {
        Operations += Threads[i].Operations;
        Retries += Threads[i].Retries;
        MiHostCpu = i;
        MiCleanAddressSpace(&Threads[i].Space);
        CHECK(MI_ATOMIC_READ64(&Threads[i].Space.CommittedPages) == 0);
        CHECK(MI_ATOMIC_READ64(&Threads[i].Space.ResidentPages) == 0);
        CHECK(MiPtCheck(&Threads[i].Space) == 0);
        MiAddressSpaceDestroy(&Threads[i].Space);
    }

    MiHostCpu = 0;
    MiSegmentDereference(Shared.Shared);
    MiSegmentDereference(Shared.FileSegment);

    for (i = 0; i < STRESS_SHARED_PAGES; i++)
    {
        ULONG t;

        for (t = 0; t < STRESS_THREADS; t++)
        {
            ULONG64 Value = *(ULONG64 *)(File.Data + (ULONG64)i * PAGE_SIZE + t * 16);

            CHECK(Value == 0 || Value == (((ULONG64)t << 48) | i));
        }
    }

    printf("  stress: %llu ops, %llu oom-retries, trimmed %lld, written %lld, repurposed %lld, faults %lld\n",
           Operations, Retries, (LONG64)Shared.Trimmed, (LONG64)Shared.Written,
           (LONG64)World.System.Pfn.Repurposed, (LONG64)World.Machine.Faults);

    while (MiWriteModifiedPages(&World.System, 1024) != 0)
        ;

    WorldExpectClean(&World, STRESS_FRAMES);
    FileDestroy(&File);
    WorldDestroy(&World);

    StressSharedSpace();
}

typedef struct _BENCH_THREAD
{
    TEST_WORLD *World;
    PMI_SEGMENT Segment;
    PMI_ADDRESS_SPACE SharedSpace;
    ULONG Index;
    ULONG Mode;
    ULONG64 Operations;
    volatile LONG *Ready;
    volatile LONG *Go;
    volatile LONG *Stop;
} BENCH_THREAD;

enum
{
    BenchDemandZero = 0,
    BenchReserveRelease,
    BenchCommitDecommit,
    BenchSharedSoftFault,
    BenchSharedDisjoint,
    BenchProtect,
    BenchQuery,
    BenchCommitFaultDecommit,
    BenchModeCount
};

static const char *BenchNames[BenchModeCount] =
{
    "demand-zero fault+free (pages/s)",
    "reserve+release 64K (pairs/s)",
    "commit+decommit 16 pages (pairs/s)",
    "shared section map+soft-fault 16+unmap (pages/s)",
    "same section, per-thread window, 16 faults (pages/s)",
    "protect 1 page RW<->RO (ops/s)",
    "query 1MB committed region (ops/s)",
    "commit+fault+decommit 64K rotating (pairs/s)",
};

static
void *
BenchWorker(void *Argument)
{
    BENCH_THREAD *Thread = Argument;
    TEST_WORLD *World = Thread->World;
    MI_ADDRESS_SPACE PrivateSpace;
    PMI_ADDRESS_SPACE Space = Thread->SharedSpace;
    MI_MEMORY_INFORMATION Info;
    ULONG64 Operations = 0;
    ULONG64 Arena = 0;
    ULONG64 ArenaSize = 1 << 20;
    ULONG Old;
    ULONG i;
    BOOLEAN CommitFault = Thread->Mode == BenchCommitFaultDecommit;

    MiHostCpu = Thread->Index;
    if (Space == NULL)
    {
        Space = &PrivateSpace;
        MI_ASSERT(NT_SUCCESS(MiAddressSpaceCreate(&World->System, Space)));
    }
    WorldAttach(World, Thread->Index, Space);
    if (CommitFault)
        ArenaSize = 32 * 1024 * 1024;
    MI_ASSERT(NT_SUCCESS(MiAllocateVirtualMemory(Space, &Arena, &ArenaSize,
                         MI_MEM_RESERVE | (CommitFault ? 0 : MI_MEM_COMMIT),
                         CommitFault ? MI_PROT_NOACCESS : MI_PROT_READWRITE)));
    if (!CommitFault)
        MI_ASSERT(NT_SUCCESS(MiFault(Space, Arena, MiFaultWrite, TRUE)));
    MI_ATOMIC_ADD32(Thread->Ready, 1);

    while (!__atomic_load_n(Thread->Go, __ATOMIC_SEQ_CST))
        sched_yield();

    while (!__atomic_load_n(Thread->Stop, __ATOMIC_SEQ_CST))
    {
        ULONG64 Base = 0;
        ULONG64 Size;

        switch (Thread->Mode)
        {
            case BenchDemandZero:
                Size = 64 * PAGE_SIZE;
                MI_ASSERT(NT_SUCCESS(MiAllocateVirtualMemory(Space, &Base, &Size,
                                     MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
                for (i = 0; i < 64; i++)
                    MI_ASSERT(NT_SUCCESS(MiFault(Space, Base + (ULONG64)i * PAGE_SIZE, MiFaultWrite, TRUE)));
                Size = 0;
                MI_ASSERT(NT_SUCCESS(MiFreeVirtualMemory(Space, &Base, &Size, MI_MEM_RELEASE)));
                Operations += 64;
                break;

            case BenchReserveRelease:
                Size = 0x10000;
                MI_ASSERT(NT_SUCCESS(MiAllocateVirtualMemory(Space, &Base, &Size, MI_MEM_RESERVE, MI_PROT_READWRITE)));
                Size = 0;
                MI_ASSERT(NT_SUCCESS(MiFreeVirtualMemory(Space, &Base, &Size, MI_MEM_RELEASE)));
                Operations++;
                break;

            case BenchCommitDecommit:
                Base = Arena + 0x10000;
                Size = 16 * PAGE_SIZE;
                MI_ASSERT(NT_SUCCESS(MiFreeVirtualMemory(Space, &Base, &Size, MI_MEM_DECOMMIT)));
                MI_ASSERT(NT_SUCCESS(MiAllocateVirtualMemory(Space, &Base, &Size, MI_MEM_COMMIT, MI_PROT_READWRITE)));
                Operations++;
                break;

            case BenchSharedSoftFault:
                Size = 16 * PAGE_SIZE;
                MI_ASSERT(NT_SUCCESS(MiMapView(Space, Thread->Segment, &Base, 0, &Size, MI_PROT_READWRITE, 0)));
                for (i = 0; i < 16; i++)
                    MI_ASSERT(NT_SUCCESS(MiFault(Space, Base + (ULONG64)i * PAGE_SIZE, MiFaultRead, TRUE)));
                MI_ASSERT(NT_SUCCESS(MiUnmapView(Space, Base)));
                Operations += 16;
                break;

            case BenchCommitFaultDecommit:
                Base = Arena + (Operations * 0x10000) % ArenaSize;
                Size = 16 * PAGE_SIZE;
                MI_ASSERT(NT_SUCCESS(MiAllocateVirtualMemory(Space, &Base, &Size,
                                     MI_MEM_COMMIT, MI_PROT_READWRITE)));
                MI_ASSERT(NT_SUCCESS(MiFault(Space, Base, MiFaultWrite, TRUE)));
                MI_ASSERT(NT_SUCCESS(MiFreeVirtualMemory(Space, &Base, &Size, MI_MEM_DECOMMIT)));
                Operations++;
                break;

            case BenchSharedDisjoint:
                Size = 16 * PAGE_SIZE;
                MI_ASSERT(NT_SUCCESS(MiMapView(Space, Thread->Segment, &Base,
                                     (ULONG64)(Thread->Index + 1) * 0x10000, &Size, MI_PROT_READWRITE, 0)));
                for (i = 0; i < 16; i++)
                    MI_ASSERT(NT_SUCCESS(MiFault(Space, Base + (ULONG64)i * PAGE_SIZE, MiFaultRead, TRUE)));
                MI_ASSERT(NT_SUCCESS(MiUnmapView(Space, Base)));
                Operations += 16;
                break;

            case BenchProtect:
                Base = Arena;
                Size = PAGE_SIZE;
                MI_ASSERT(NT_SUCCESS(MiProtectVirtualMemory(Space, &Base, &Size,
                                     (Operations & 1) ? MI_PROT_READWRITE : MI_PROT_READONLY, &Old)));
                Operations++;
                break;

            default:
                MI_ASSERT(NT_SUCCESS(MiQueryVirtualMemory(Space, Arena, &Info)));
                Operations++;
                break;
        }
    }

    Thread->Operations = Operations;
    ArenaSize = 0;
    MI_ASSERT(NT_SUCCESS(MiFreeVirtualMemory(Space, &Arena, &ArenaSize, MI_MEM_RELEASE)));
    WorldAttach(World, Thread->Index, NULL);
    if (Thread->SharedSpace == NULL)
        MiAddressSpaceDestroy(Space);
    CHECK(MiHostIrql == 0);
    return NULL;
}

void
TestBenchVm(void)
{
    static const ULONG Counts[] = { 1, 2, 4, 8 };
    static TEST_WORLD World;
    BOOLEAN Shared = (BOOLEAN)(getenv("MM_BENCH_SHARED") && atoi(getenv("MM_BENCH_SHARED")) != 0);
    ULONG Mode, c, i;

    printf("VM benchmark: %s address space, TLB simulation disabled\n", Shared ? "shared" : "per-thread");
    for (Mode = 0; Mode < BenchModeCount; Mode++)
    {
        if (getenv("MM_BENCH_MODE") && (ULONG)atoi(getenv("MM_BENCH_MODE")) != Mode)
            continue;

        printf("  %-52s", BenchNames[Mode]);

        for (c = 0; c < RTL_NUMBER_OF(Counts); c++)
        {
            BENCH_THREAD Threads[8];
            pthread_t Handles[8];
            volatile LONG Go = 0, Stop = 0, Ready = 0;
            PMI_SEGMENT Segment = NULL;
            MI_ADDRESS_SPACE SharedSpace;
            ULONG64 Total = 0;
            double Start, Elapsed;

            WorldCreate(&World, 16384, MACHINE_MAX_CPUS, 100000000);
            World.Machine.TlbDisabled = TRUE;
            MI_ASSERT(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentPageFileBacked, 0x100000,
                                 MI_PROT_READWRITE, NULL, NULL, NULL, 0, &Segment)));
            MI_ASSERT(NT_SUCCESS(MiSegmentMakeResident(Segment, 0, 0x100000)));
            if (Shared)
                MI_ASSERT(NT_SUCCESS(MiAddressSpaceCreate(&World.System, &SharedSpace)));

            for (i = 0; i < Counts[c]; i++)
            {
                Threads[i].World = &World;
                Threads[i].Segment = Segment;
                Threads[i].SharedSpace = Shared ? &SharedSpace : NULL;
                Threads[i].Index = i;
                Threads[i].Mode = Mode;
                Threads[i].Operations = 0;
                Threads[i].Ready = &Ready;
                Threads[i].Go = &Go;
                Threads[i].Stop = &Stop;
                MI_ASSERT(pthread_create(&Handles[i], NULL, BenchWorker, &Threads[i]) == 0);
            }

            while (MI_ATOMIC_READ32(&Ready) != (LONG)Counts[c])
                sched_yield();
            Start = NowSeconds();
            __atomic_store_n(&Go, 1, __ATOMIC_SEQ_CST);
            while (NowSeconds() - Start < (getenv("MM_BENCH_LONG") ? 6.0 : 0.4))
                sched_yield();
            __atomic_store_n(&Stop, 1, __ATOMIC_SEQ_CST);

            for (i = 0; i < Counts[c]; i++)
            {
                MI_ASSERT(pthread_join(Handles[i], NULL) == 0);
                Total += Threads[i].Operations;
            }

            Elapsed = NowSeconds() - Start;
            printf(" %ut=%8.2fM", Counts[c], (double)Total / Elapsed / 1e6);

            if (Shared)
            {
                CHECK(SharedSpace.PrivatePages == 0 && SharedSpace.CommittedPages == 0);
                MiAddressSpaceDestroy(&SharedSpace);
            }
            MiSegmentDereference(Segment);
            CHECK(WorldCheck(&World) == 0 && World.System.CommittedPages == 0);
            WorldDestroy(&World);
        }

        printf("\n");
    }
}
