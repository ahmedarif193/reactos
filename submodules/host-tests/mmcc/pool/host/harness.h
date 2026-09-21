/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/pool/host/harness.h
 * PURPOSE:     Pool allocator host-test harness interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <nvs/pool/include/poolenv.h>
#include <nvs/pool/include/poolheap.h>
#include <nvs/pool/include/pooltrack.h>

#include <pthread.h>
#include <time.h>

typedef struct _TEST_ARENA
{
    PUCHAR Base;
    SIZE_T Bytes;
    volatile UCHAR *PageState;
    volatile LONG64 CommitCalls;
    volatile LONG64 DecommitCalls;
    volatile LONG64 CommittedPages;
    volatile LONG64 FailAfter;
    volatile LONG Errors;
    POOL_BACKING Backing;
} TEST_ARENA;

extern int TestFailures;
extern int TestChecks;

#define CHECK(e) do { __sync_fetch_and_add(&TestChecks, 1); if (!(e)) { __sync_fetch_and_add(&TestFailures, 1); \
    fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #e); } } while (0)

void ArenaCreate(TEST_ARENA *Arena, SIZE_T Bytes);
void ArenaDestroy(TEST_ARENA *Arena);
void ArenaFailAfter(TEST_ARENA *Arena, LONG64 Commits);
double NowSeconds(void);
ULONG64 Rng(ULONG64 *State);
void RunThreads(ULONG Count, void *(*Routine)(void *), void **Args);

void TestVa(void);
void TestSegment(void);
void TestLfh(void);
void TestVs(void);
void TestLarge(void);
void TestHeap(void);
void TestExpansion(void);
void TestSmp(void);
void TestTrack(void);
void TestBench(void);
