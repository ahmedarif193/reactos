/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/mmharness.c
 * PURPOSE:     Memory manager host-test harness implementation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"

_Thread_local ULONG MiHostCpu;
_Thread_local KIRQL MiHostIrql;
_Thread_local ULONG64 MiHostIrqlRaises;
static _Thread_local MI_HOST_DPC_ROUTINE MiHostPendingDpc;
static _Thread_local PVOID MiHostDpcContext;
int TestFailures;
int TestChecks;

KIRQL
MiHostRaiseIrql(KIRQL NewIrql)
{
    KIRQL OldIrql = MiHostIrql;

    MI_ASSERT(NewIrql >= OldIrql && NewIrql <= 2);
    if (NewIrql > OldIrql)
        MiHostIrqlRaises++;
    MiHostIrql = NewIrql;
    return OldIrql;
}

VOID
MiHostLowerIrql(KIRQL NewIrql)
{
    MI_ASSERT(NewIrql <= MiHostIrql);
    MiHostIrql = NewIrql;

    while (MiHostIrql < 2 && MiHostPendingDpc != NULL)
    {
        MI_HOST_DPC_ROUTINE Routine = MiHostPendingDpc;
        PVOID Context = MiHostDpcContext;

        MiHostPendingDpc = NULL;
        MiHostIrql = 2;
        Routine(Context);
        MI_ASSERT(MiHostIrql == 2);
        MiHostIrql = NewIrql;
    }
}

VOID
MiHostQueueDpc(MI_HOST_DPC_ROUTINE Routine, PVOID Context)
{
    MI_ASSERT(MiHostPendingDpc == NULL && Routine != NULL);
    MiHostPendingDpc = Routine;
    MiHostDpcContext = Context;
    MiHostLowerIrql(MiHostIrql);
}

static
NTSTATUS
WorldFault(PVOID Context, ULONG Cpu, ULONG64 VirtualAddress, MACHINE_ACCESS Access, BOOLEAN UserMode)
{
    TEST_WORLD *World = Context;
    PMI_ADDRESS_SPACE Space = (VirtualAddress >= World->System.Arch->SystemAddressStart)
                                  ? &World->System.SystemSpace
                                  : __atomic_load_n(&World->CpuSpace[Cpu], __ATOMIC_SEQ_CST);

    if (Space == NULL)
        return STATUS_ACCESS_VIOLATION;

    return MiFault(Space, VirtualAddress, (MI_FAULT_ACCESS)Access, UserMode);
}

void
WorldEnableFaults(TEST_WORLD *World)
{
    World->Machine.Fault = WorldFault;
    World->Machine.FaultContext = World;
}

void
WorldCreate(TEST_WORLD *World, ULONG Frames, ULONG Cpus, LONG64 CommitLimit)
{
    memset(World, 0, sizeof(*World));
    MachineCreate(&World->Machine, Frames, Cpus);
    World->PfnArray = calloc(Frames, sizeof(MI_PFN));
    MiSystemInitialize(&World->System, World->PfnArray, Frames, Cpus, CommitLimit);
    MiPfnDbAddRange(&World->System.Pfn, 1, Frames - 1);
    if (!NT_SUCCESS(MiAddressSpaceCreate(&World->System, &World->System.SystemSpace)))
        abort();
    World->Machine.SystemRoot = World->System.SystemSpace.RootFrame;
    World->Machine.Fault = WorldFault;
    World->Machine.FaultContext = World;
}

static
NTSTATUS
PagingRead(PVOID Context, ULONG64 Slot, PVOID PageBuffer)
{
    TEST_PAGEFILE *Paging = Context;

    MI_ASSERT(MiHostIrql <= 1);
    if (Paging->ProbeLock != NULL && MI_ATOMIC_READ32(&Paging->ProbeLock->Held))
        __sync_fetch_and_add(&Paging->IoUnderLock, 1);

    if (Paging->FailReads || Slot == 0 || Slot >= Paging->Slots)
        return STATUS_UNEXPECTED_IO_ERROR;

    MachineCopyToRam(MachineCurrent, PageBuffer, Paging->Store + Slot * PAGE_SIZE, PAGE_SIZE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
PagingWrite(PVOID Context, ULONG64 Slot, PVOID PageBuffer)
{
    TEST_PAGEFILE *Paging = Context;

    MI_ASSERT(MiHostIrql <= 1);
    if (Paging->FailWrites || Slot == 0 || Slot >= Paging->Slots)
        return STATUS_UNEXPECTED_IO_ERROR;

    MachineCopyFromRam(MachineCurrent, Paging->Store + Slot * PAGE_SIZE, PageBuffer, PAGE_SIZE);
    return STATUS_SUCCESS;
}

void
WorldAttachPageFile(TEST_WORLD *World, ULONG64 Slots)
{
    MI_PAGEFILE_OPS Ops = { PagingRead, PagingWrite };

    World->Paging.Slots = Slots;
    World->Paging.Store = calloc(Slots, PAGE_SIZE);
    if (!NT_SUCCESS(MiPageFileInitialize(&World->Paging.PageFile, &Ops, &World->Paging, Slots)))
        abort();
    World->System.PageFile = &World->Paging.PageFile;
}

void
WorldAttach(TEST_WORLD *World, ULONG Cpu, PMI_ADDRESS_SPACE Space)
{
    __atomic_store_n(&World->CpuSpace[Cpu], Space, __ATOMIC_SEQ_CST);
    MachineSetUserRoot(&World->Machine, Cpu, Space ? Space->RootFrame : 0);
}

ULONG
WorldCheck(TEST_WORLD *World)
{
    return MiPfnDbCheck(&World->System.Pfn) + (ULONG)World->Machine.StaleTlbUses +
           (ULONG)World->Machine.BreakBeforeMakeViolations + (ULONG)World->Machine.BadFrameAccesses;
}

NTSTATUS
UserWrite(TEST_WORLD *World, ULONG Cpu, ULONG64 Va, const void *Buffer, SIZE_T Length)
{
    return MachineAccessMemory(&World->Machine, Cpu, Va, (PVOID)Buffer, Length, MachineWrite, TRUE);
}

NTSTATUS
UserRead(TEST_WORLD *World, ULONG Cpu, ULONG64 Va, void *Buffer, SIZE_T Length)
{
    return MachineAccessMemory(&World->Machine, Cpu, Va, Buffer, Length, MachineRead, TRUE);
}

NTSTATUS
UserWrite64(TEST_WORLD *World, ULONG Cpu, ULONG64 Va, ULONG64 Value)
{
    return UserWrite(World, Cpu, Va, &Value, sizeof(Value));
}

ULONG64
UserRead64(TEST_WORLD *World, ULONG Cpu, ULONG64 Va, NTSTATUS *Status)
{
    ULONG64 Value = 0xDEADDEADDEADDEADULL;
    NTSTATUS Local = UserRead(World, Cpu, Va, &Value, sizeof(Value));

    if (Status != NULL)
        *Status = Local;

    return Value;
}

static
NTSTATUS
FileRead(PVOID Context, ULONG64 Offset, ULONG Length, PVOID Buffer)
{
    TEST_FILE *File = Context;

    MI_ASSERT(MiHostIrql <= 1);
    if (File->ProbeLock != NULL && MI_ATOMIC_READ32(&File->ProbeLock->Held))
        __sync_fetch_and_add(&File->IoUnderLock, 1);

    if (File->FailReads || Offset + Length > File->Size)
        return STATUS_UNEXPECTED_IO_ERROR;

    MachineCopyToRam(MachineCurrent, Buffer, File->Data + Offset, Length);
    __sync_fetch_and_add(&File->Reads, 1);
    return STATUS_SUCCESS;
}

static
NTSTATUS
FileWrite(PVOID Context, ULONG64 Offset, ULONG Length, PVOID Buffer)
{
    TEST_FILE *File = Context;

    MI_ASSERT(MiHostIrql <= 1);
    if (File->FailWrites || Offset + Length > File->Size)
        return STATUS_UNEXPECTED_IO_ERROR;

    MachineCopyFromRam(MachineCurrent, File->Data + Offset, Buffer, Length);
    __sync_fetch_and_add(&File->Writes, 1);
    return STATUS_SUCCESS;
}

MI_FILE_OPS TestFileOps = { .Read = FileRead, .Write = FileWrite };

void
FileCreate(TEST_FILE *File, ULONG64 Size)
{
    ULONG64 i;

    memset(File, 0, sizeof(*File));
    File->Size = Size;
    File->Data = malloc(Size);
    for (i = 0; i < Size; i++)
        File->Data[i] = (UCHAR)((i >> PAGE_SHIFT) * 31 + i);
}

void
FileDestroy(TEST_FILE *File)
{
    free(File->Data);
}

void
WorldDestroy(TEST_WORLD *World)
{
    CHECK(MiHostIrql == 0 && MiHostPendingDpc == NULL);
    if (World->System.PageFile != NULL)
    {
        MiPageFileUninitialize(&World->Paging.PageFile);
        free(World->Paging.Store);
    }

    MiVadFlushCache(&World->System.SystemSpace);

    MachineDestroy(&World->Machine);
    free(World->PfnArray);
}

ULONG64
Rng(ULONG64 *State)
{
    ULONG64 x = *State;

    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *State = x;
    return x;
}

double
NowSeconds(void)
{
    struct timespec Ts;

    clock_gettime(CLOCK_MONOTONIC, &Ts);
    return (double)Ts.tv_sec + (double)Ts.tv_nsec / 1e9;
}

typedef struct _TEST_ENTRY
{
    const char *Name;
    void (*Routine)(void);
    int Bench;
} TEST_ENTRY;

static const TEST_ENTRY Tests[] =
{
    { "pfn", TestPfn, 0 },
    { "pagetable", TestPageTable, 0 },
    { "vad", TestVad, 0 },
    { "vm", TestVm, 0 },
    { "large", TestLarge, 0 },
    { "awe", TestAwe, 0 },
    { "clone", TestClone, 0 },
    { "session", TestSession, 0 },
    { "fault", TestFault, 0 },
    { "fault_smp", TestFaultSmp, 0 },
    { "paging", TestPaging, 0 },
    { "section", TestSection, 0 },
    { "image", TestImage, 0 },
    { "stress", TestStress, 0 },
    { "ccmm", TestCcOnMm, 0 },
    { "ntfile", TestNtFile, 0 },
    { "writeback", TestWriteback, 0 },
    { "ntpaging", TestNtPaging, 0 },
    { "async", TestAsync, 0 },
    { "sys", TestSys, 0 },
    { "process", TestProcess, 0 },
    { "benchsys", TestBenchSys, 1 },
    { "benchvm", TestBenchVm, 1 },
    { "benchcc", TestBenchCcDirty, 1 },
    { "bench", TestBenchCore, 1 },
};

int
main(int argc, char **argv)
{
    size_t i;
    int Ran = 0;

    for (i = 0; i < sizeof(Tests) / sizeof(Tests[0]); i++)
    {
        if (argc > 1 && strcmp(argv[1], Tests[i].Name) != 0)
            continue;
        if (argc <= 1 && Tests[i].Bench)
            continue;

        Tests[i].Routine();
        CHECK(MiHostIrql == 0 && MiHostPendingDpc == NULL);
        Ran++;
    }

    printf("%s [%s]: %d checks, %d failures\n", argc > 1 ? argv[1] : "all", MiArchDescribe()->Name,
           TestChecks, TestFailures);
    return (TestFailures != 0 || Ran == 0) ? 1 : 0;
}
