/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/ntdll/include/ntdll.h
 * PURPOSE:     NTDLL host-test types and runtime declarations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#define _In_
#define _Inout_updates_(n)
#define NTAPI
#define TRUE 1
#define FALSE 0
#define MAXULONG_PTR UINTPTR_MAX
#define HEAP_ZERO_MEMORY 8
#define MEM_COMMIT 0x1000
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define STATUS_SUCCESS ((int32_t)0)
#define STATUS_INVALID_PARAMETER ((int32_t)0xc000000d)
#define STATUS_NO_MEMORY ((int32_t)0xc0000017)
#define STATUS_INVALID_ADDRESS ((int32_t)0xc0000141)
#define STATUS_INTEGER_OVERFLOW ((int32_t)0xc0000095)
#define NT_SUCCESS(s) ((s) >= 0)
#define MemoryBasicInformation 0
#define FIELD_OFFSET(t, f) offsetof(t, f)
#define CONTAINING_RECORD(p, t, f) ((t *)((char *)(p) - offsetof(t, f)))
#define NtCurrentProcess() ((PVOID)(intptr_t)-1)
#define NtCurrentPeb() (&HostPeb)
#define RTL_SRWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER

typedef void VOID, *PVOID;
typedef uint8_t BOOLEAN, UCHAR;
typedef uint32_t ULONG;
typedef int32_t LONG, NTSTATUS;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef pthread_rwlock_t RTL_SRWLOCK;
typedef struct _LIST_ENTRY
{
    struct _LIST_ENTRY *Flink, *Blink;
} LIST_ENTRY, *PLIST_ENTRY;
typedef struct { PVOID ProcessHeap; } PEB;
typedef struct { ULONG State, Protect; } MEMORY_BASIC_INFORMATION;

extern PEB HostPeb;

static inline VOID InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Flink = Head;
    Entry->Blink = Head->Blink;
    Head->Blink->Flink = Entry;
    Head->Blink = Entry;
}

static inline VOID RemoveEntryList(PLIST_ENTRY Entry)
{
    Entry->Flink->Blink = Entry->Blink;
    Entry->Blink->Flink = Entry->Flink;
}

#define IsListEmpty(head) ((head)->Flink == (head))

static inline LONG InterlockedCompareExchange(volatile LONG *Value, LONG New, LONG Expected)
{
    __atomic_compare_exchange_n(Value, &Expected, New, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Expected;
}

static inline LONG InterlockedExchange(volatile LONG *Value, LONG New)
{
    return __atomic_exchange_n(Value, New, __ATOMIC_SEQ_CST);
}

VOID RtlAcquireSRWLockExclusive(RTL_SRWLOCK *Lock);
VOID RtlReleaseSRWLockExclusive(RTL_SRWLOCK *Lock);
VOID RtlAcquireSRWLockShared(RTL_SRWLOCK *Lock);
VOID RtlReleaseSRWLockShared(RTL_SRWLOCK *Lock);
PVOID RtlAllocateHeap(PVOID Heap, ULONG Flags, SIZE_T Size);
BOOLEAN RtlFreeHeap(PVOID Heap, ULONG Flags, PVOID Address);
NTSTATUS NtQueryVirtualMemory(PVOID Process, PVOID Address, ULONG Class,
                             MEMORY_BASIC_INFORMATION *Info, SIZE_T Size, SIZE_T *Returned);
NTSTATUS NTAPI RtlRegisterCfgTargetRange(PVOID Base, SIZE_T Size);
VOID NTAPI RtlUnregisterCfgTargetRange(PVOID Base);
BOOLEAN LdrpGuardDynamicTargetIsExecutable(PVOID Target);
