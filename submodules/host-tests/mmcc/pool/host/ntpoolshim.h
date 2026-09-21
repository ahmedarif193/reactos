/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/pool/host/ntpoolshim.h
 * PURPOSE:     NT pool allocator host-test compatibility definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <nvs/pool/include/poolenv.h>

typedef ULONG POOL_TYPE, EX_POOL_PRIORITY;
typedef ULONG64 POOL_FLAGS;
typedef const void *PCPOOL_EXTENDED_PARAMETER;
typedef struct _EPROCESS { SIZE_T Quota[2]; LONG References; } EPROCESS, *PEPROCESS;

#define IN
#define OUT
#define NTAPI
#define CODE_SEG(s)
#define DBG 1
#define DPRINT1(...) ((void)0)
#define NonPagedPool 0
#define NonPagedPoolExecute 0
#define PagedPool 1
#define NonPagedPoolNx 0x200
#define CACHE_ALIGNED_POOL_MASK 4
#define MUST_SUCCEED_POOL_MASK 2
#define POOL_QUOTA_FAIL_INSTEAD_OF_RAISE 8
#define POOL_RAISE_IF_ALLOCATION_FAILURE 0x10
#define POOL_NX_ALLOCATION 0x200
#define POOL_ZERO_ALLOCATION 0x400
#define POOL_FLAG_USE_QUOTA 1ULL
#define POOL_FLAG_UNINITIALIZED 2ULL
#define POOL_FLAG_CACHE_ALIGNED 8ULL
#define POOL_FLAG_RAISE_ON_FAILURE 0x20ULL
#define POOL_FLAG_NON_PAGED 0x40ULL
#define POOL_FLAG_NON_PAGED_EXECUTE 0x80ULL
#define POOL_FLAG_PAGED 0x100ULL
#define STATUS_COMMITMENT_LIMIT ((NTSTATUS)0xC000012D)
#define MUST_SUCCEED_POOL_EMPTY 0x41
#define BAD_POOL_CALLER 0xC2
#define TAG_NONE 0x656e6f4e
#define APC_LEVEL 1
#define MI_PROT_READWRITE 4
#define MI_PROT_EXECUTE_READWRITE 6
#define InterlockedCompareExchange(p, v, c) __sync_val_compare_and_swap(p, c, v)
#define InterlockedExchange(p, v) __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST)

extern ULONG KeNumberProcessors;
extern PEPROCESS PsInitialSystemProcess;
extern int MiSystem;

ULONG64 KeQueryInterruptTime(VOID);
KIRQL KeGetCurrentIrql(VOID);
_Noreturn VOID KeBugCheckEx(ULONG Code, ULONG_PTR A, ULONG_PTR B, ULONG_PTR C, ULONG_PTR D);
_Noreturn VOID ExRaiseStatus(NTSTATUS Status);
BOOLEAN MmUseSpecialPool(SIZE_T Size, ULONG Tag);
PVOID MmAllocateSpecialPool(SIZE_T Size, ULONG Tag, POOL_TYPE Type, ULONG Mode);
BOOLEAN MmIsSpecialPoolAddress(PVOID Block);
VOID MmFreeSpecialPool(PVOID Block);
PEPROCESS PsGetCurrentProcess(VOID);
NTSTATUS PsChargeProcessPoolQuota(PEPROCESS Process, ULONG Type, SIZE_T Size);
VOID PsReturnPoolQuota(PEPROCESS Process, ULONG Type, SIZE_T Size);
VOID ObReferenceObject(PEPROCESS Process);
VOID ObDereferenceObject(PEPROCESS Process);
NTSTATUS MiSystemCommitPinned(int *System, ULONG64 Base, ULONG Pages, ULONG Protection);
VOID MiSystemDecommitPinned(int *System, ULONG64 Base, ULONG Pages);
VOID MiWakeBalanceSetManager(VOID);
VOID InitializePool(POOL_TYPE Type, ULONG Threshold);
PVOID ExAllocatePoolWithTag(POOL_TYPE Type, SIZE_T Size, ULONG Tag);
PVOID ExAllocatePool2(POOL_FLAGS Flags, SIZE_T Size, ULONG Tag);
PVOID ExAllocatePool3(POOL_FLAGS Flags, SIZE_T Size, ULONG Tag,
                     PCPOOL_EXTENDED_PARAMETER Parameters, ULONG Count);
PVOID ExAllocatePoolWithQuotaTag(POOL_TYPE Type, SIZE_T Size, ULONG Tag);
VOID ExFreePoolWithTag(PVOID Block, ULONG Tag);
VOID ExReturnPoolQuota(PVOID Block);
