/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/env/mihost.h
 * PURPOSE:     Host-native environment for memory manager tests
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

typedef void VOID;
typedef void *PVOID;
typedef const char *PCSTR;
typedef char CHAR;
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
typedef SIZE_T *PSIZE_T;
typedef struct _EPROCESS_HOST *PEPROCESS;
typedef struct _LOADER_PARAMETER_BLOCK_HOST *PLOADER_PARAMETER_BLOCK;

#define TRUE 1
#define FALSE 0
#define PAGE_SHIFT 12
#define PAGE_SIZE  4096u

#define NT_HOST_BASE_ENVIRONMENT
#define STATUS_SUCCESS                   0
#define STATUS_PENDING                   ((NTSTATUS)0x00000103)
#define STATUS_CANT_WAIT                 ((NTSTATUS)0xC00000D8)
#define STATUS_NOT_SUPPORTED             ((NTSTATUS)0xC00000BB)
#define STATUS_GUARD_PAGE_VIOLATION      ((NTSTATUS)0x80000001)
#define STATUS_PARTIAL_COPY              ((NTSTATUS)0x8000000D)
#define STATUS_ACCESS_VIOLATION          ((NTSTATUS)0xC0000005)
#define STATUS_IN_PAGE_ERROR             ((NTSTATUS)0xC0000006)
#define STATUS_INVALID_PARAMETER         ((NTSTATUS)0xC000000D)
#define STATUS_NO_MEMORY                 ((NTSTATUS)0xC0000017)
#define STATUS_CONFLICTING_ADDRESSES     ((NTSTATUS)0xC0000018)
#define STATUS_NOT_MAPPED_VIEW           ((NTSTATUS)0xC0000019)
#define STATUS_UNABLE_TO_FREE_VM         ((NTSTATUS)0xC000001A)
#define STATUS_UNABLE_TO_DELETE_SECTION  ((NTSTATUS)0xC000001B)
#define STATUS_INVALID_VIEW_SIZE         ((NTSTATUS)0xC000001F)
#define STATUS_UNABLE_TO_DECOMMIT_VM     ((NTSTATUS)0xC000002C)
#define STATUS_NOT_COMMITTED             ((NTSTATUS)0xC000002D)
#define STATUS_SECTION_TOO_BIG           ((NTSTATUS)0xC0000040)
#define STATUS_INVALID_PAGE_PROTECTION   ((NTSTATUS)0xC0000045)
#define STATUS_SECTION_PROTECTION        ((NTSTATUS)0xC000004E)
#define STATUS_INSUFFICIENT_RESOURCES    ((NTSTATUS)0xC000009A)
#define STATUS_FREE_VM_NOT_AT_BASE       ((NTSTATUS)0xC000009F)
#define STATUS_MEMORY_NOT_ALLOCATED      ((NTSTATUS)0xC00000A0)
#define STATUS_COMMITMENT_LIMIT          ((NTSTATUS)0xC000012D)
#define STATUS_INVALID_ADDRESS           ((NTSTATUS)0xC0000141)
#define STATUS_WORKING_SET_QUOTA         ((NTSTATUS)0xC00000A1)
#define STATUS_UNEXPECTED_IO_ERROR       ((NTSTATUS)0xC00000E9)
#define STATUS_END_OF_FILE               ((NTSTATUS)0xC0000011)
#define STATUS_STACK_OVERFLOW            ((NTSTATUS)0xC00000FD)
#define STATUS_DYNAMIC_CODE_BLOCKED      ((NTSTATUS)0xC0000604)
#define STATUS_EXECUTABLE_MEMORY_WRITE   ((NTSTATUS)0xC0000723)
#define NT_SUCCESS(s)                    ((s) >= 0)

#define _In_
#define _Out_
#define _Inout_
#define _In_opt_
#define _Out_opt_
#define _Inout_opt_
#define _In_reads_(x)
#define _Out_writes_(x)
#define _Inout_updates_(x)
#define FORCEINLINE static inline
#define C_ASSERT(e) _Static_assert(e, #e)
#define RTL_NUMBER_OF(a) (sizeof(a) / sizeof((a)[0]))
#define FIELD_OFFSET(t, f) offsetof(t, f)
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define UNREFERENCED_PARAMETER(p) ((void)(p))
#define RtlZeroMemory(d, l) memset((d), 0, (l))
#define RtlCopyMemory(d, s, l) memcpy((d), (s), (l))
#define RtlFillMemory(d, l, f) memset((d), (f), (l))

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

typedef struct _MI_SPINLOCK
{
    volatile int Held;
} MI_SPINLOCK, *PMI_SPINLOCK, MI_MUTEX, *PMI_MUTEX;

typedef struct _MI_RWLOCK
{
    volatile LONG Held;
    volatile LONG Writers;
    MI_MUTEX WriterGate;
} MI_RWLOCK, *PMI_RWLOCK;

static inline void MiHostSpinAcquire(PMI_SPINLOCK Lock)
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

extern _Thread_local ULONG MiHostCpu;
extern _Thread_local KIRQL MiHostIrql;

typedef VOID (*MI_HOST_DPC_ROUTINE)(PVOID Context);

KIRQL MiHostRaiseIrql(KIRQL NewIrql);
VOID MiHostLowerIrql(KIRQL NewIrql);
VOID MiHostQueueDpc(MI_HOST_DPC_ROUTINE Routine, PVOID Context);

#define MI_CACHE_ALIGNED                __attribute__((aligned(64)))
#define MI_CURRENT_CPU()                MiHostCpu
#define MI_RAISE_TO_DISPATCH(i)         (*(i) = MiHostRaiseIrql(2))
#define MI_RESTORE_IRQL(i)              MiHostLowerIrql(i)
#define MI_SPIN_INIT(l)                 ((l)->Held = 0)
#define MI_SPIN_ACQUIRE(l, i)           do { MI_RAISE_TO_DISPATCH(i); MiHostSpinAcquire(l); } while (0)
#define MI_SPIN_RELEASE(l, i)           do { __sync_lock_release(&(l)->Held); MI_RESTORE_IRQL(i); } while (0)
#define MI_MUTEX_INIT(m)                ((m)->Held = 0)
#define MI_MUTEX_ACQUIRE(m)             do { MI_ASSERT(MiHostIrql <= 1); MiHostSpinAcquire(m); } while (0)
#define MI_MUTEX_TRY_ACQUIRE(m)         __sync_bool_compare_and_swap(&(m)->Held, 0, 1)
#define MI_MUTEX_RELEASE(m)             __sync_lock_release(&(m)->Held)
#define MI_ALLOCATE(b)                  malloc(b)
#define MI_FREE(p)                      free(p)
#define MI_ATOMIC_ADD32(v, d)           __sync_fetch_and_add((v), (d))
#define MI_ATOMIC_ADD64(v, d)           __sync_fetch_and_add((v), (d))
#define MI_ATOMIC_READ8(v)              __atomic_load_n((v), __ATOMIC_SEQ_CST)
#define MI_ATOMIC_READ32(v)             __atomic_load_n((v), __ATOMIC_SEQ_CST)
#define MI_ATOMIC_WRITE32(v, n)          __atomic_store_n((v), (n), __ATOMIC_SEQ_CST)
#define MI_ATOMIC_READ64(v)             __atomic_load_n((v), __ATOMIC_SEQ_CST)
#define MI_ATOMIC_WRITE64(v, n)          __atomic_store_n((v), (n), __ATOMIC_SEQ_CST)
#define MI_ATOMIC_CAS32(v, n, o)        __sync_val_compare_and_swap((v), (o), (n))
#define MI_ATOMIC_OR8(v, m)             __sync_fetch_and_or((v), (UCHAR)(m))
#define MI_ATOMIC_AND8(v, m)            __sync_fetch_and_and((v), (UCHAR)(m))
static inline __attribute__((no_sanitize("thread"))) ULONG64 MiHostPeek(const void *Value, SIZE_T Size)
{
    return (Size == 8) ? *(const volatile ULONG64 *)Value : *(const volatile ULONG *)Value;
}

#define MI_PEEK(v)                      MiHostPeek(&(v), sizeof(v))
#define MI_ATOMIC_READ_POINTER(v)        __atomic_load_n((v), __ATOMIC_SEQ_CST)
#define MI_ATOMIC_WRITE_POINTER(v, n)    __atomic_store_n((v), (n), __ATOMIC_SEQ_CST)
#define MI_PAUSE()                      sched_yield()
#define MI_ASSERT(e)                    do { if (!(e)) { fprintf(stderr, "MI_ASSERT %s:%d %s\n", __FILE__, __LINE__, #e); abort(); } } while (0)

static inline void MiHostAcquireShared(PMI_RWLOCK Lock)
{
    MI_ASSERT(MiHostIrql <= 1);
    for (;;)
    {
        LONG Held = MI_ATOMIC_READ32(&Lock->Held);

        if (Held >= 0 && MI_ATOMIC_READ32(&Lock->Writers) == 0 &&
            MI_ATOMIC_CAS32(&Lock->Held, Held + 1, Held) == Held)
        {
            return;
        }
        MI_PAUSE();
    }
}

static inline void MiHostAcquireExclusive(PMI_RWLOCK Lock)
{
    MI_ASSERT(MiHostIrql <= 1);
    if (MI_ATOMIC_READ32(&Lock->Writers) == 0 && MI_ATOMIC_CAS32(&Lock->Held, -1, 0) == 0)
        return;

    MI_MUTEX_ACQUIRE(&Lock->WriterGate);
    MI_ATOMIC_ADD32(&Lock->Writers, 1);
    while (MI_ATOMIC_CAS32(&Lock->Held, -1, 0) != 0)
        MI_PAUSE();
    MI_ATOMIC_ADD32(&Lock->Writers, -1);
    MI_MUTEX_RELEASE(&Lock->WriterGate);
}

static inline void MiHostReleaseShared(PMI_RWLOCK Lock)
{
    MI_ASSERT(MI_ATOMIC_READ32(&Lock->Held) > 0);
    MI_ATOMIC_ADD32(&Lock->Held, -1);
}

static inline void MiHostReleaseExclusive(PMI_RWLOCK Lock)
{
    MI_ASSERT(MI_ATOMIC_READ32(&Lock->Held) == -1);
    __atomic_store_n(&Lock->Held, 0, __ATOMIC_SEQ_CST);
}

#define MI_RW_INIT(l)                   RtlZeroMemory((l), sizeof(*(l)))
#define MI_RW_ACQUIRE_SHARED(l)         MiHostAcquireShared(l)
#define MI_RW_RELEASE_SHARED(l)         MiHostReleaseShared(l)
#define MI_RW_ACQUIRE_EXCLUSIVE(l)      MiHostAcquireExclusive(l)
#define MI_RW_RELEASE_EXCLUSIVE(l)      MiHostReleaseExclusive(l)
