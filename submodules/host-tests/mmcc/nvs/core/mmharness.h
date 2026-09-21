/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/mmharness.h
 * PURPOSE:     Memory manager host-test harness interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <nvs/include/mm.h>
#include "../machine/machine.h"
#include <pthread.h>
#include <time.h>

extern int TestFailures;
extern int TestChecks;
extern _Thread_local ULONG64 MiHostIrqlRaises;

#define CHECK(e) do { __sync_fetch_and_add(&TestChecks, 1); if (!(e)) { __sync_fetch_and_add(&TestFailures, 1); \
    fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #e); } } while (0)

typedef struct _TEST_PAGEFILE
{
    MI_PAGEFILE PageFile;
    PUCHAR Store;
    ULONG64 Slots;
    volatile LONG FailWrites;
    volatile LONG FailReads;
    PMI_RWLOCK ProbeLock;
    volatile LONG IoUnderLock;
} TEST_PAGEFILE;

typedef struct _TEST_FILE
{
    PUCHAR Data;
    ULONG64 Size;
    volatile LONG64 Reads;
    volatile LONG64 Writes;
    volatile LONG FailReads;
    volatile LONG FailWrites;
    PMI_RWLOCK ProbeLock;
    volatile LONG IoUnderLock;
} TEST_FILE;

typedef struct _TEST_WORLD
{
    MACHINE Machine;
    MI_SYSTEM System;
    PMI_PFN PfnArray;
    PMI_ADDRESS_SPACE CpuSpace[MACHINE_MAX_CPUS];
    TEST_PAGEFILE Paging;
} TEST_WORLD;

#define USER_BASE 0x10000000ULL

void WorldCreate(TEST_WORLD *World, ULONG Frames, ULONG Cpus, LONG64 CommitLimit);
void WorldDestroy(TEST_WORLD *World);
void WorldEnableFaults(TEST_WORLD *World);
ULONG64 Rng(ULONG64 *State);
double NowSeconds(void);

void WorldAttachPageFile(TEST_WORLD *World, ULONG64 Slots);
void WorldAttach(TEST_WORLD *World, ULONG Cpu, PMI_ADDRESS_SPACE Space);
ULONG WorldCheck(TEST_WORLD *World);
NTSTATUS UserWrite(TEST_WORLD *World, ULONG Cpu, ULONG64 Va, const void *Buffer, SIZE_T Length);
NTSTATUS UserRead(TEST_WORLD *World, ULONG Cpu, ULONG64 Va, void *Buffer, SIZE_T Length);
NTSTATUS UserWrite64(TEST_WORLD *World, ULONG Cpu, ULONG64 Va, ULONG64 Value);
ULONG64 UserRead64(TEST_WORLD *World, ULONG Cpu, ULONG64 Va, NTSTATUS *Status);
void FileCreate(TEST_FILE *File, ULONG64 Size);
void FileDestroy(TEST_FILE *File);
extern MI_FILE_OPS TestFileOps;
void ProcessCreate(TEST_WORLD *World, PMI_ADDRESS_SPACE Space);
void ProcessDestroy(TEST_WORLD *World, PMI_ADDRESS_SPACE Space);
void WorldExpectClean(TEST_WORLD *World, ULONG Frames);

void TestVm(void);
void TestLarge(void);
void TestAwe(void);
void TestClone(void);
void TestSession(void);
void TestFault(void);
void TestFaultSmp(void);
void TestPaging(void);
void TestSection(void);
void TestImage(void);
void TestVad(void);
void TestStress(void);
void TestCcOnMm(void);
void TestNtFile(void);
void TestWriteback(void);
void TestNtPaging(void);
void TestAsync(void);
void TestSys(void);
void TestProcess(void);
void TestBenchSys(void);
void TestBenchVm(void);
void TestBenchCcDirty(void);
void TestPfn(void);
void TestPageTable(void);
void TestBenchCore(void);
