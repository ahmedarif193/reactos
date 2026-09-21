/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/pool/host/harness.c
 * PURPOSE:     Pool allocator host-test harness implementation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "harness.h"
#include <sys/mman.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define TEST_POISON(a, l)   ASAN_POISON_MEMORY_REGION((a), (l))
#define TEST_UNPOISON(a, l) ASAN_UNPOISON_MEMORY_REGION((a), (l))
#endif
#endif
#ifndef TEST_POISON
#define TEST_POISON(a, l)   ((void)0)
#define TEST_UNPOISON(a, l) ((void)0)
#endif

_Thread_local ULONG PoolHostProcessor;
int TestFailures;
int TestChecks;

static
NTSTATUS
ArenaCommit(PVOID Context, PVOID Base, SIZE_T Bytes)
{
    TEST_ARENA *Arena = Context;
    SIZE_T First = ((PUCHAR)Base - Arena->Base) >> PAGE_SHIFT;
    SIZE_T Count = Bytes >> PAGE_SHIFT;
    SIZE_T i;

    if ((PUCHAR)Base < Arena->Base || (PUCHAR)Base + Bytes > Arena->Base + Arena->Bytes ||
        (Bytes & (PAGE_SIZE - 1)) != 0 || (((ULONG_PTR)Base) & (PAGE_SIZE - 1)) != 0 || Bytes == 0)
    {
        __sync_fetch_and_add(&Arena->Errors, 1);
        return STATUS_INVALID_PARAMETER;
    }

    if (Arena->FailAfter >= 0 && __sync_fetch_and_add(&Arena->FailAfter, -1) <= 0)
    {
        Arena->FailAfter = 0;
        return STATUS_NO_MEMORY;
    }

    for (i = First; i < First + Count; i++)
    {
        if (__sync_lock_test_and_set(&Arena->PageState[i], 1) != 0)
            __sync_fetch_and_add(&Arena->Errors, 1);
    }

    TEST_UNPOISON(Base, Bytes);
    memset(Base, 0xCC, Bytes);
    __sync_fetch_and_add(&Arena->CommitCalls, 1);
    __sync_fetch_and_add(&Arena->CommittedPages, (LONG64)Count);
    return STATUS_SUCCESS;
}

static
VOID
ArenaDecommit(PVOID Context, PVOID Base, SIZE_T Bytes)
{
    TEST_ARENA *Arena = Context;
    SIZE_T First = ((PUCHAR)Base - Arena->Base) >> PAGE_SHIFT;
    SIZE_T Count = Bytes >> PAGE_SHIFT;
    SIZE_T i;

    if ((PUCHAR)Base < Arena->Base || (PUCHAR)Base + Bytes > Arena->Base + Arena->Bytes ||
        (Bytes & (PAGE_SIZE - 1)) != 0 || (((ULONG_PTR)Base) & (PAGE_SIZE - 1)) != 0 || Bytes == 0)
    {
        __sync_fetch_and_add(&Arena->Errors, 1);
        return;
    }

    memset(Base, 0xDD, Bytes);

    for (i = First; i < First + Count; i++)
    {
        if (__sync_lock_test_and_set(&Arena->PageState[i], 0) != 1)
            __sync_fetch_and_add(&Arena->Errors, 1);
    }

    TEST_POISON(Base, Bytes);
    __sync_fetch_and_add(&Arena->DecommitCalls, 1);
    __sync_fetch_and_add(&Arena->CommittedPages, -(LONG64)Count);
}

void
ArenaCreate(TEST_ARENA *Arena, SIZE_T Bytes)
{
    memset(Arena, 0, sizeof(*Arena));
    Arena->Bytes = Bytes;
    Arena->Base = mmap(NULL, Bytes + POOL_UNIT_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (Arena->Base == MAP_FAILED)
    {
        perror("mmap");
        abort();
    }

    Arena->PageState = calloc(Bytes >> PAGE_SHIFT, 1);
    Arena->FailAfter = -1;
    Arena->Backing.Commit = ArenaCommit;
    Arena->Backing.Decommit = ArenaDecommit;
    Arena->Backing.Context = Arena;
    TEST_POISON(Arena->Base, Bytes);
}

void
ArenaDestroy(TEST_ARENA *Arena)
{
    TEST_UNPOISON(Arena->Base, Arena->Bytes);
    munmap(Arena->Base, Arena->Bytes + POOL_UNIT_SIZE);
    free((void *)Arena->PageState);
}

void
ArenaFailAfter(TEST_ARENA *Arena, LONG64 Commits)
{
    Arena->FailAfter = Commits;
}

double
NowSeconds(void)
{
    struct timespec Ts;

    clock_gettime(CLOCK_MONOTONIC, &Ts);
    return (double)Ts.tv_sec + (double)Ts.tv_nsec / 1e9;
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

void
RunThreads(ULONG Count, void *(*Routine)(void *), void **Args)
{
    pthread_t Threads[64];
    ULONG i;

    for (i = 0; i < Count; i++)
        pthread_create(&Threads[i], NULL, Routine, Args[i]);

    for (i = 0; i < Count; i++)
        pthread_join(Threads[i], NULL);
}

typedef struct _TEST_ENTRY
{
    const char *Name;
    void (*Routine)(void);
} TEST_ENTRY;

static const TEST_ENTRY Tests[] =
{
    { "va", TestVa },
    { "segment", TestSegment },
    { "lfh", TestLfh },
    { "vs", TestVs },
    { "large", TestLarge },
    { "heap", TestHeap },
    { "expansion", TestExpansion },
    { "smp", TestSmp },
    { "track", TestTrack },
    { "bench", TestBench },
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

        if (argc <= 1 && strcmp(Tests[i].Name, "bench") == 0)
            continue;

        Tests[i].Routine();
        Ran++;
    }

    printf("%s: %d checks, %d failures\n", argc > 1 ? argv[1] : "all", TestChecks, TestFailures);
    return (TestFailures != 0 || Ran == 0) ? 1 : 0;
}
