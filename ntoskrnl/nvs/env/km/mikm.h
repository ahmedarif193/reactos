/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/env/km/mikm.h
 * PURPOSE:     Kernel environment for the memory manager
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <ntoskrnl.h>

#define MI_CORE_TAG                     'rCmM'

PVOID MiKmAllocate(_In_ SIZE_T Bytes);
VOID MiKmFree(_In_ PVOID Block);

typedef KSPIN_LOCK MI_SPINLOCK, *PMI_SPINLOCK;
typedef KGUARDED_MUTEX MI_MUTEX, *PMI_MUTEX;
typedef struct _MI_RWLOCK
{
    EX_PUSH_LOCK Readers;
    KGUARDED_MUTEX Writers;
} MI_RWLOCK, *PMI_RWLOCK;

FORCEINLINE
VOID
MiKmAcquireExclusive(PMI_RWLOCK Lock)
{
    KeEnterCriticalRegion();
    if (InterlockedCompareExchangePointer(&Lock->Readers.Ptr, (PVOID)EX_PUSH_LOCK_LOCK, NULL) == NULL)
        return;

    KeAcquireGuardedMutex(&Lock->Writers);
    ExAcquirePushLockExclusive(&Lock->Readers);
    KeReleaseGuardedMutex(&Lock->Writers);
}

#define MI_CACHE_ALIGNED                DECLSPEC_CACHEALIGN
#define MI_CURRENT_CPU()                KeGetCurrentProcessorNumber()
#define MI_RAISE_TO_DISPATCH(i)         KeRaiseIrql(DISPATCH_LEVEL, (i))
#define MI_RESTORE_IRQL(i)              KeLowerIrql(i)
#define MI_SPIN_INIT(l)                 KeInitializeSpinLock(l)
#define MI_SPIN_ACQUIRE(l, i)           KeAcquireSpinLock((l), (i))
#define MI_SPIN_RELEASE(l, i)           KeReleaseSpinLock((l), (i))
#define MI_MUTEX_INIT(m)                KeInitializeGuardedMutex(m)
#define MI_MUTEX_ACQUIRE(m)             KeAcquireGuardedMutex(m)
#define MI_MUTEX_TRY_ACQUIRE(m)         KeTryToAcquireGuardedMutex(m)
#define MI_MUTEX_RELEASE(m)             KeReleaseGuardedMutex(m)
#define MI_RW_INIT(l)                   do { ExInitializePushLock(&(l)->Readers); KeInitializeGuardedMutex(&(l)->Writers); } while (0)
#define MI_RW_ACQUIRE_SHARED(l)         do { KeEnterCriticalRegion(); ExAcquirePushLockShared(&(l)->Readers); } while (0)
#define MI_RW_RELEASE_SHARED(l)         do { ExReleasePushLockShared(&(l)->Readers); KeLeaveCriticalRegion(); } while (0)
#define MI_RW_ACQUIRE_EXCLUSIVE(l)      MiKmAcquireExclusive(l)
#define MI_RW_RELEASE_EXCLUSIVE(l)      do { ExReleasePushLockExclusive(&(l)->Readers); KeLeaveCriticalRegion(); } while (0)
#define MI_ALLOCATE(b)                  MiKmAllocate(b)
#define MI_FREE(p)                      MiKmFree(p)
#define MI_ATOMIC_ADD32(v, d)           InterlockedExchangeAdd((v), (d))
#define MI_ATOMIC_ADD64(v, d)           InterlockedExchangeAdd64((v), (d))
#define MI_ATOMIC_READ8(v)              (*(volatile UCHAR *)(v))
#define MI_ATOMIC_READ32(v)             (*(volatile LONG *)(v))
#define MI_ATOMIC_READ64(v)             (*(volatile LONG64 *)(v))
#define MI_ATOMIC_WRITE64(v, n)          ((VOID)InterlockedExchange64((v), (n)))
#define MI_ATOMIC_CAS32(v, n, o)        InterlockedCompareExchange((v), (n), (o))
#define MI_ATOMIC_OR8(v, m)             InterlockedOr8((volatile CHAR *)(v), (CHAR)(m))
#define MI_ATOMIC_AND8(v, m)            InterlockedAnd8((volatile CHAR *)(v), (CHAR)(m))
#define MI_PEEK(v)                      (v)
#define MI_ATOMIC_READ_POINTER(v)        (*(v))
#define MI_ATOMIC_WRITE_POINTER(v, n)    ((VOID)InterlockedExchangePointer((PVOID volatile *)(v), (n)))
#define MI_PAUSE()                      YieldProcessor()
#define MI_ASSERT(e)                    ASSERT(e)
