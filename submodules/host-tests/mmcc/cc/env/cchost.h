/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/cc/env/cchost.h
 * PURPOSE:     Host-native environment for cache manager tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sched.h>
#include <pthread.h>

#if !defined(NT_HOST_BASE_ENVIRONMENT)
#define NT_HOST_BASE_ENVIRONMENT

typedef void VOID;
typedef void *PVOID;
typedef unsigned char UCHAR, BOOLEAN, KIRQL;
typedef unsigned short USHORT;
typedef unsigned int ULONG;
typedef int LONG;
typedef unsigned long long ULONG64;
typedef long long LONG64;
typedef size_t SIZE_T;
typedef uintptr_t ULONG_PTR;
typedef int NTSTATUS;
typedef ULONG *PULONG;
typedef ULONG64 *PULONG64;
typedef UCHAR *PUCHAR;
typedef KIRQL *PKIRQL;
typedef BOOLEAN *PBOOLEAN;

#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS                   0
#define STATUS_PENDING                   ((NTSTATUS)0x00000103)
#define STATUS_END_OF_FILE               ((NTSTATUS)0xC0000011)
#define STATUS_INVALID_PARAMETER         ((NTSTATUS)0xC000000D)
#define STATUS_INSUFFICIENT_RESOURCES    ((NTSTATUS)0xC000009A)
#define STATUS_NO_MEMORY                 ((NTSTATUS)0xC0000017)
#define STATUS_UNEXPECTED_IO_ERROR       ((NTSTATUS)0xC00000E9)
#define STATUS_CANT_WAIT                 ((NTSTATUS)0xC00000D8)
#define NT_SUCCESS(s)                    ((s) >= 0)

#ifndef CC_HOST_PAGE_SHIFT
#define CC_HOST_PAGE_SHIFT 12
#endif
#define PAGE_SHIFT CC_HOST_PAGE_SHIFT
#define PAGE_SIZE  (1u << PAGE_SHIFT)

#define _In_
#define _Out_
#define _Inout_
#define _Out_opt_
#define _In_opt_
#define _Inout_opt_
#define FORCEINLINE static inline
#define C_ASSERT(e) _Static_assert(e, #e)
#define RTL_NUMBER_OF(a) (sizeof(a) / sizeof((a)[0]))
#define FIELD_OFFSET(t, f) offsetof(t, f)
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define UNREFERENCED_PARAMETER(p) ((void)(p))
#define RtlZeroMemory(d, l) memset((d), 0, (l))
#define RtlCopyMemory(d, s, l) memcpy((d), (s), (l))

typedef struct _LIST_ENTRY
{
    struct _LIST_ENTRY *Flink, *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

static inline void InitializeListHead(PLIST_ENTRY h) { h->Flink = h->Blink = h; }
static inline int IsListEmpty(const LIST_ENTRY *h) { return h->Flink == h; }
static inline void InsertHeadList(PLIST_ENTRY h, PLIST_ENTRY e)
{ e->Flink = h->Flink; e->Blink = h; h->Flink->Blink = e; h->Flink = e; }
static inline void InsertTailList(PLIST_ENTRY h, PLIST_ENTRY e)
{ e->Blink = h->Blink; e->Flink = h; h->Blink->Flink = e; h->Blink = e; }
static inline void RemoveEntryList(PLIST_ENTRY e)
{ e->Blink->Flink = e->Flink; e->Flink->Blink = e->Blink; }

#endif

typedef struct _CC_LOCK
{
    volatile int Held;
} CC_LOCK, *PCC_LOCK;

typedef union _CC_RESOURCE
{
    struct
    {
        pthread_rwlock_t *Native;
        volatile LONG Waiters;
    };
    ULONG64 Layout[13];
} CC_RESOURCE;

static inline void CcHostLockAcquire(PCC_LOCK Lock)
{
    unsigned Spins = 0;

    while (__sync_lock_test_and_set(&Lock->Held, 1))
    {
        if (++Spins > 64)
        {
            sched_yield();
            Spins = 0;
        }
    }
}

extern _Thread_local ULONG CcHostProcessor;

#define CC_CACHE_ALIGNED                __attribute__((aligned(64)))
#define CC_CURRENT_PROCESSOR()          CcHostProcessor
#define CC_LOCK_INIT(l)                 ((l)->Held = 0)
#define CC_LOCK_ACQUIRE(l, i)           do { CcHostLockAcquire(l); *(i) = 0; } while (0)
#define CC_LOCK_RELEASE(l, i)           do { __sync_lock_release(&(l)->Held); (void)(i); } while (0)
#define CC_ALLOCATE(b)                  malloc(b)
#define CC_FREE(p)                      free(p)
#define CC_ATOMIC_ADD32(v, d)           __sync_fetch_and_add((v), (d))
#define CC_ATOMIC_ADD64(v, d)           __sync_fetch_and_add((v), (d))
#define CC_ATOMIC_READ32(v)             __atomic_load_n((v), __ATOMIC_SEQ_CST)
#define CC_ATOMIC_WRITE32(v, n)         __atomic_store_n((v), (n), __ATOMIC_SEQ_CST)
#define CC_ATOMIC_READ64(v)             __atomic_load_n((v), __ATOMIC_SEQ_CST)
#define CC_ATOMIC_WRITE64(v, n)         __atomic_store_n((v), (n), __ATOMIC_SEQ_CST)
#define CC_POPCOUNT64(x)                ((ULONG)__builtin_popcountll(x))
#define CC_ASSERT(e)                    do { if (!(e)) { fprintf(stderr, "CC_ASSERT %s:%d %s\n", __FILE__, __LINE__, #e); abort(); } } while (0)
