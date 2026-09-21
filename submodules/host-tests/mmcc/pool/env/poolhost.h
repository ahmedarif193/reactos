/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/pool/env/poolhost.h
 * PURPOSE:     Host-native environment for pool allocator tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <sched.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
typedef SIZE_T *PSIZE_T;
typedef ULONG64 *PULONG64;
typedef UCHAR *PUCHAR;
typedef KIRQL *PKIRQL;

#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS                   0
#define STATUS_INVALID_PARAMETER         ((NTSTATUS)0xC000000D)
#define STATUS_INSUFFICIENT_RESOURCES    ((NTSTATUS)0xC000009A)
#define STATUS_NO_MEMORY                 ((NTSTATUS)0xC0000017)
#define NT_SUCCESS(s)                    ((s) >= 0)

#ifndef POOL_HOST_PAGE_SHIFT
#define POOL_HOST_PAGE_SHIFT 12
#endif
#define PAGE_SHIFT POOL_HOST_PAGE_SHIFT
#define PAGE_SIZE  (1u << PAGE_SHIFT)
#define ANYSIZE_ARRAY 1

#define _In_
#define _Out_
#define _Inout_
#define _Out_opt_
#define _In_opt_
#define FORCEINLINE static inline
#define C_ASSERT(e) _Static_assert(e, #e)
#define RTL_NUMBER_OF(a) (sizeof(a) / sizeof((a)[0]))
#define FIELD_OFFSET(t, f) offsetof(t, f)
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define UNREFERENCED_PARAMETER(p) ((void)(p))
#define RtlZeroMemory(d, l) memset((d), 0, (l))
#define RtlFillMemory(d, l, f) memset((d), (f), (l))
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

typedef BOOLEAN *PBOOLEAN;

typedef struct _POOL_LOCK
{
    volatile int Held;
} POOL_LOCK, *PPOOL_LOCK;

static inline void PoolHostLockAcquire(PPOOL_LOCK Lock)
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

static inline void PoolHostLockRelease(PPOOL_LOCK Lock)
{
    __sync_lock_release(&Lock->Held);
}

extern _Thread_local ULONG PoolHostProcessor;

#define POOL_CACHE_ALIGNED              __attribute__((aligned(64)))
#define POOL_CURRENT_PROCESSOR()        PoolHostProcessor
#define POOL_LOCK_INIT(l, p)            do { (l)->Held = 0; (void)(p); } while (0)
#define POOL_LOCK_ACQUIRE(l, i)         do { PoolHostLockAcquire(l); *(i) = 0; } while (0)
#define POOL_LOCK_RELEASE(l, i)         do { PoolHostLockRelease(l); (void)(i); } while (0)
#define POOL_ATOMIC_ADD64(v, d)         __sync_fetch_and_add((v), (d))
#define POOL_ATOMIC_ADD32(v, d)         __sync_fetch_and_add((v), (d))
#define POOL_ATOMIC_READ32(v)            __atomic_load_n((v), __ATOMIC_SEQ_CST)
#define POOL_ATOMIC_READ64(v)            __atomic_load_n((v), __ATOMIC_SEQ_CST)
#define POOL_ATOMIC_CAS64(v, n, o)       __sync_val_compare_and_swap((v), (o), (n))
#define POOL_CTZ64(x)                   ((ULONG)__builtin_ctzll(x))
#define POOL_ASSERT(e)                  do { if (!(e)) { fprintf(stderr, "POOL_ASSERT %s:%d %s\n", __FILE__, __LINE__, #e); abort(); } } while (0)

#define POOL_ATOMIC_CAS32(v, n, o)      __sync_val_compare_and_swap((v), (o), (n))
