/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/cc/host/ccharness.h
 * PURPOSE:     Cache manager host-test harness interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <cc/include/ccengine.h>
#include <pthread.h>
#include <time.h>

typedef struct _TEST_FILE
{
    PUCHAR Disk;
    PUCHAR Pages;
    volatile UCHAR *Resident;
    volatile UCHAR *Dirty;
    ULONG64 Size;
    ULONG64 PageCount;
    volatile LONG MappedViews;
    volatile LONG64 DiskReads;
    volatile LONG64 DiskWrites;
    volatile LONG64 FlushFailuresLeft;
    ULONG PrefetchCalls;
    ULONG64 PrefetchOffset;
    ULONG PrefetchLength;
    NTSTATUS PrefetchStatus;
    BOOLEAN DeferPrefetch;
    volatile LONG Errors;
    CC_BACKING_OPS Ops;
} TEST_FILE;

extern int TestFailures;
extern int TestChecks;

#define CHECK(e) do { __sync_fetch_and_add(&TestChecks, 1); if (!(e)) { __sync_fetch_and_add(&TestFailures, 1); \
    fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #e); } } while (0)

void FileCreate(TEST_FILE *File, ULONG64 Size);
void FileDestroy(TEST_FILE *File);
UCHAR FilePattern(ULONG64 Offset);
NTSTATUS MoveToBuffer(PVOID Context, PVOID CacheAddress, ULONG64 FileOffset, ULONG Length);
NTSTATUS MoveFromBuffer(PVOID Context, PVOID CacheAddress, ULONG64 FileOffset, ULONG Length);
ULONG64 Rng(ULONG64 *State);
double NowSeconds(void);

typedef struct _MOVE_CONTEXT
{
    PUCHAR Buffer;
    ULONG64 BaseOffset;
} MOVE_CONTEXT;

void TestViews(void);
void TestDirty(void);
void TestBcb(void);
void TestNtPin(void);
void TestNtCopy(void);
void TestNtLayout(void);
void TestReadAhead(void);
void TestCoherency(void);
void TestSmp(void);
void TestBench(void);
