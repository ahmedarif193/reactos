/*
 * PROJECT:         ReactOS kernel-mode tests
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 * LICENSE:         GPLv3 - See COPYING3 in the top level directory
 * PURPOSE:         Pool allocator throughput against processor count
 * PROGRAMMER:      Ahmed ARIF <arif193@gmail.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_POOLBENCH 'bloP'
#define BENCH_MAX_CPUS 64
#define BENCH_BATCH 64
#define BENCH_MS 1000

static const ULONG PoolBenchSizes[] = { 32, 128, 512, 2048 };

typedef struct _POOL_BENCH_CONTEXT
{
    ULONG Processor;
    ULONG Size;
    POOL_TYPE PoolType;
    PKEVENT Start;
    volatile LONG *Stop;
    ULONGLONG Operations;
    ULONG Failures;
} POOL_BENCH_CONTEXT, *PPOOL_BENCH_CONTEXT;

static
VOID
NTAPI
PoolBenchThread(
    _In_ PVOID Parameter)
{
    PPOOL_BENCH_CONTEXT Context = Parameter;
    PVOID Blocks[BENCH_BATCH];
    ULONGLONG Operations = 0;
    ULONG i;

    KeSetSystemAffinityThread((KAFFINITY)1 << Context->Processor);
    KeWaitForSingleObject(Context->Start, Executive, KernelMode, FALSE, NULL);

    while (*Context->Stop == 0)
    {
        for (i = 0; i < BENCH_BATCH; i++)
        {
            Blocks[i] = ExAllocatePoolWithTag(Context->PoolType,
                                              Context->Size,
                                              TAG_POOLBENCH);
            if (Blocks[i] == NULL)
                Context->Failures++;
        }

        for (i = 0; i < BENCH_BATCH; i++)
        {
            if (Blocks[i] != NULL)
            {
                ExFreePoolWithTag(Blocks[i], TAG_POOLBENCH);
                Operations++;
            }
        }
    }

    Context->Operations = Operations;
    KeRevertToUserAffinityThread();
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
PoolBenchRun(
    _In_ POOL_TYPE PoolType,
    _In_ PCSTR PoolName,
    _In_ ULONG Size,
    _In_ ULONG ProcessorCount)
{
    POOL_BENCH_CONTEXT Contexts[BENCH_MAX_CPUS];
    PVOID Objects[BENCH_MAX_CPUS];
    HANDLE Threads[BENCH_MAX_CPUS];
    KEVENT Start;
    volatile LONG Stop = 0;
    LARGE_INTEGER Frequency, Begin, End;
    ULONGLONG Total = 0, Elapsed;
    ULONG Failures = 0;
    NTSTATUS Status;
    ULONG i;
    ULONG Started = 0;

    KeInitializeEvent(&Start, NotificationEvent, FALSE);
    RtlZeroMemory(Contexts, sizeof(Contexts));

    for (i = 0; i < ProcessorCount; i++)
    {
        Contexts[i].Processor = i;
        Contexts[i].Size = Size;
        Contexts[i].PoolType = PoolType;
        Contexts[i].Start = &Start;
        Contexts[i].Stop = &Stop;

        Status = PsCreateSystemThread(&Threads[i], THREAD_ALL_ACCESS, NULL,
                                      NULL, NULL, PoolBenchThread, &Contexts[i]);
        if (!skip(NT_SUCCESS(Status), "thread %lu: %lx\n", i, Status))
            Started++;
        else
            break;

        Status = ObReferenceObjectByHandle(Threads[i], SYNCHRONIZE, NULL,
                                           KernelMode, &Objects[i], NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    if (Started == 0)
        return;

    KeQueryPerformanceCounter(&Frequency);
    Begin = KeQueryPerformanceCounter(NULL);
    KeSetEvent(&Start, IO_NO_INCREMENT, FALSE);
    KeDelayExecutionThread(KernelMode, FALSE, &(LARGE_INTEGER){ .QuadPart = -10LL * 1000 * BENCH_MS });
    InterlockedExchange((PLONG)&Stop, 1);
    End = KeQueryPerformanceCounter(NULL);

    for (i = 0; i < Started; i++)
    {
        KeWaitForSingleObject(Objects[i], Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Objects[i]);
        ZwClose(Threads[i]);
        Total += Contexts[i].Operations;
        Failures += Contexts[i].Failures;
    }

    Elapsed = (ULONGLONG)(End.QuadPart - Begin.QuadPart);
    if (Elapsed == 0)
        Elapsed = 1;

    trace("POOLBENCH %s size=%lu cpus=%lu ops=%I64u ops_per_sec=%I64u failures=%lu\n",
          PoolName, Size, Started, Total,
          (Total * (ULONGLONG)Frequency.QuadPart) / Elapsed, Failures);

    ok(Failures == 0, "%lu allocation failures\n", Failures);
}

START_TEST(ExPoolBench)
{
    ULONG ProcessorCount;
    ULONG i;

    ProcessorCount = KeQueryActiveProcessorCount(NULL);
    if (ProcessorCount > BENCH_MAX_CPUS)
        ProcessorCount = BENCH_MAX_CPUS;

    trace("POOLBENCH_BEGIN processors=%lu\n", ProcessorCount);

    for (i = 0; i < RTL_NUMBER_OF(PoolBenchSizes); i++)
    {
        PoolBenchRun(NonPagedPool, "nonpaged", PoolBenchSizes[i], 1);
        PoolBenchRun(NonPagedPool, "nonpaged", PoolBenchSizes[i], ProcessorCount);
    }

    for (i = 0; i < RTL_NUMBER_OF(PoolBenchSizes); i++)
    {
        PoolBenchRun(PagedPool, "paged", PoolBenchSizes[i], 1);
        PoolBenchRun(PagedPool, "paged", PoolBenchSizes[i], ProcessorCount);
    }

    trace("POOLBENCH_DONE\n");
}
