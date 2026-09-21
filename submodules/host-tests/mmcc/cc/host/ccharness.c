/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/cc/host/ccharness.c
 * PURPOSE:     Cache manager host-test harness implementation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccharness.h"

_Thread_local ULONG CcHostProcessor;
int TestFailures;
int TestChecks;

UCHAR
FilePattern(ULONG64 Offset)
{
    return (UCHAR)((Offset * 2654435761ULL) >> 13);
}

static
NTSTATUS
FileMapView(PVOID Context, ULONG64 Offset, SIZE_T Length, PVOID *Base)
{
    TEST_FILE *File = Context;

    if ((Offset & (PAGE_SIZE - 1)) != 0 || Offset >= File->PageCount * PAGE_SIZE)
    {
        __sync_fetch_and_add(&File->Errors, 1);
        return STATUS_INVALID_PARAMETER;
    }

    (void)Length;
    __sync_fetch_and_add(&File->MappedViews, 1);
    *Base = File->Pages + Offset;
    return STATUS_SUCCESS;
}

static
VOID
FileUnmapView(PVOID Context, PVOID Base)
{
    TEST_FILE *File = Context;

    if ((PUCHAR)Base < File->Pages || (PUCHAR)Base >= File->Pages + File->PageCount * PAGE_SIZE)
        __sync_fetch_and_add(&File->Errors, 1);

    if (__sync_fetch_and_add(&File->MappedViews, -1) <= 0)
        __sync_fetch_and_add(&File->Errors, 1);
}

static
BOOLEAN
FileIsResident(PVOID Context, ULONG64 Offset, ULONG Length)
{
    TEST_FILE *File = Context;
    ULONG64 Page;

    for (Page = Offset >> PAGE_SHIFT; Page <= (Offset + Length - 1) >> PAGE_SHIFT && Page < File->PageCount; Page++)
    {
        if (__atomic_load_n(&File->Resident[Page], __ATOMIC_SEQ_CST) != 2)
            return FALSE;
    }

    return TRUE;
}

static
NTSTATUS
FileMakeResident(PVOID Context, ULONG64 Offset, ULONG Length, ULONG64 ValidDataLength)
{
    TEST_FILE *File = Context;
    ULONG64 Page;

    for (Page = Offset >> PAGE_SHIFT; Page <= (Offset + Length - 1) >> PAGE_SHIFT && Page < File->PageCount; Page++)
    {
        ULONG64 Start = Page << PAGE_SHIFT;
        ULONG64 i;

        if (!__sync_bool_compare_and_swap(&File->Resident[Page], 0, 1))
        {
            while (__atomic_load_n(&File->Resident[Page], __ATOMIC_SEQ_CST) == 1)
                sched_yield();
            continue;
        }

        for (i = Start; i < Start + PAGE_SIZE; i++)
            File->Pages[i] = (i < File->Size && i < ValidDataLength) ? File->Disk[i] : 0;

        __sync_fetch_and_add(&File->DiskReads, 1);
        __atomic_store_n(&File->Resident[Page], 2, __ATOMIC_SEQ_CST);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
FileMarkDirty(PVOID Context, ULONG64 Offset, ULONG Length)
{
    TEST_FILE *File = Context;
    ULONG64 Page;

    for (Page = Offset >> PAGE_SHIFT; Page <= (Offset + Length - 1) >> PAGE_SHIFT && Page < File->PageCount; Page++)
    {
        if (__atomic_load_n(&File->Resident[Page], __ATOMIC_SEQ_CST) != 2)
            __sync_fetch_and_add(&File->Errors, 1);

        __atomic_store_n(&File->Dirty[Page], 1, __ATOMIC_SEQ_CST);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
FilePrefetch(PVOID Context, ULONG64 Offset, ULONG Length)
{
    TEST_FILE *File = Context;

    File->PrefetchCalls++;
    File->PrefetchOffset = Offset;
    File->PrefetchLength = Length;
    if (!NT_SUCCESS(File->PrefetchStatus) || File->DeferPrefetch)
        return File->PrefetchStatus;
    return FileMakeResident(Context, Offset, Length, File->Size);
}

static
NTSTATUS
FileFlush(PVOID Context, ULONG64 Offset, ULONG Length)
{
    TEST_FILE *File = Context;
    ULONG64 Page;

    if (__atomic_load_n(&File->FlushFailuresLeft, __ATOMIC_SEQ_CST) > 0)
    {
        __sync_fetch_and_add(&File->FlushFailuresLeft, -1);
        return STATUS_UNEXPECTED_IO_ERROR;
    }

    for (Page = Offset >> PAGE_SHIFT; Page <= (Offset + Length - 1) >> PAGE_SHIFT && Page < File->PageCount; Page++)
    {
        ULONG64 Start = Page << PAGE_SHIFT;
        ULONG64 End = Start + PAGE_SIZE;

        if (!__sync_lock_test_and_set(&File->Dirty[Page], 0))
            continue;

        if (End > File->Size)
            End = File->Size;
        if (End > Start)
            memcpy(File->Disk + Start, File->Pages + Start, (size_t)(End - Start));

        __sync_fetch_and_add(&File->DiskWrites, 1);
    }

    return STATUS_SUCCESS;
}

static
BOOLEAN
FilePurge(PVOID Context, ULONG64 Offset, ULONG64 Length)
{
    TEST_FILE *File = Context;
    ULONG64 End = (Length == (ULONG64)-1) ? File->PageCount * PAGE_SIZE : Offset + Length;
    ULONG64 Page;

    for (Page = (Offset + PAGE_SIZE - 1) >> PAGE_SHIFT; Page < (End >> PAGE_SHIFT) && Page < File->PageCount; Page++)
    {
        __atomic_store_n(&File->Dirty[Page], 0, __ATOMIC_SEQ_CST);
        __atomic_store_n(&File->Resident[Page], 0, __ATOMIC_SEQ_CST);
        memset(File->Pages + (Page << PAGE_SHIFT), 0xEE, PAGE_SIZE);
    }

    return TRUE;
}

void
FileCreate(TEST_FILE *File, ULONG64 Size)
{
    ULONG64 i;

    memset(File, 0, sizeof(*File));
    File->Size = Size;
    File->PageCount = ((Size + CC_VIEW_SIZE - 1) / CC_VIEW_SIZE) * CC_VIEW_PAGES + 2 * CC_VIEW_PAGES;
    File->Disk = malloc((size_t)(File->PageCount * PAGE_SIZE));
    File->Pages = malloc((size_t)(File->PageCount * PAGE_SIZE));
    File->Resident = calloc((size_t)File->PageCount, 1);
    File->Dirty = calloc((size_t)File->PageCount, 1);

    for (i = 0; i < File->PageCount * PAGE_SIZE; i++)
        File->Disk[i] = (i < Size) ? FilePattern(i) : 0;

    memset(File->Pages, 0xEE, (size_t)(File->PageCount * PAGE_SIZE));

    File->Ops.MapView = FileMapView;
    File->Ops.UnmapView = FileUnmapView;
    File->Ops.IsResident = FileIsResident;
    File->Ops.MakeResident = FileMakeResident;
    File->Ops.MarkDirty = FileMarkDirty;
    File->Ops.Flush = FileFlush;
    File->Ops.Purge = FilePurge;
    File->Ops.Prefetch = FilePrefetch;
}

void
FileDestroy(TEST_FILE *File)
{
    free(File->Disk);
    free(File->Pages);
    free((void *)File->Resident);
    free((void *)File->Dirty);
}

NTSTATUS
MoveToBuffer(PVOID Context, PVOID CacheAddress, ULONG64 FileOffset, ULONG Length)
{
    MOVE_CONTEXT *Move = Context;

    memcpy(Move->Buffer + (FileOffset - Move->BaseOffset), CacheAddress, Length);
    return STATUS_SUCCESS;
}

NTSTATUS
MoveFromBuffer(PVOID Context, PVOID CacheAddress, ULONG64 FileOffset, ULONG Length)
{
    MOVE_CONTEXT *Move = Context;

    memcpy(CacheAddress, Move->Buffer + (FileOffset - Move->BaseOffset), Length);
    return STATUS_SUCCESS;
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
} TEST_ENTRY;

static const TEST_ENTRY Tests[] =
{
    { "views", TestViews },
    { "dirty", TestDirty },
    { "bcb", TestBcb },
    { "ntpin", TestNtPin },
    { "ntcopy", TestNtCopy },
    { "ntlayout", TestNtLayout },
    { "readahead", TestReadAhead },
    { "coherency", TestCoherency },
    { "smp", TestSmp },
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
