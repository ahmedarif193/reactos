/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/env/km/poolkm.h
 * PURPOSE:     Kernel environment for the pool allocator
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <ntoskrnl.h>

typedef struct _POOL_LOCK
{
    KSPIN_LOCK Spin;
    KGUARDED_MUTEX Mutex;
    BOOLEAN Paged;
} POOL_LOCK, *PPOOL_LOCK;

FORCEINLINE
VOID
PoolLockInitialize(PPOOL_LOCK Lock, BOOLEAN Paged)
{
    Lock->Paged = Paged;
    KeInitializeSpinLock(&Lock->Spin);
    KeInitializeGuardedMutex(&Lock->Mutex);
}

FORCEINLINE
VOID
PoolLockAcquire(PPOOL_LOCK Lock, PKIRQL OldIrql)
{
    if (Lock->Paged)
    {
        *OldIrql = PASSIVE_LEVEL;
        KeAcquireGuardedMutex(&Lock->Mutex);
    }
    else
    {
        KeAcquireSpinLock(&Lock->Spin, OldIrql);
    }
}

FORCEINLINE
VOID
PoolLockRelease(PPOOL_LOCK Lock, KIRQL OldIrql)
{
    if (Lock->Paged)
        KeReleaseGuardedMutex(&Lock->Mutex);
    else
        KeReleaseSpinLock(&Lock->Spin, OldIrql);
}

FORCEINLINE
ULONG
PoolCountTrailingZeros64(ULONG64 Value)
{
    return (ULONG)__builtin_ctzll(Value);
}

#define POOL_CACHE_ALIGNED              DECLSPEC_CACHEALIGN
#define POOL_CURRENT_PROCESSOR()        KeGetCurrentProcessorNumber()
#define POOL_LOCK_INIT(l, p)            PoolLockInitialize((l), (p))
#define POOL_LOCK_ACQUIRE(l, i)         PoolLockAcquire((l), (i))
#define POOL_LOCK_RELEASE(l, i)         PoolLockRelease((l), (i))
#define POOL_ATOMIC_ADD64(v, d)         InterlockedExchangeAdd64((v), (d))
#define POOL_ATOMIC_ADD32(v, d)         InterlockedExchangeAdd((v), (d))
#define POOL_ATOMIC_CAS32(v, n, o)      InterlockedCompareExchange((v), (n), (o))
#define POOL_ATOMIC_READ32(v)           (*(volatile LONG *)(v))
#define POOL_ATOMIC_READ64(v)           (*(volatile LONG64 *)(v))
#define POOL_ATOMIC_CAS64(v, n, o)      InterlockedCompareExchange64((v), (n), (o))
#define POOL_CTZ64(x)                   PoolCountTrailingZeros64(x)
#define POOL_ASSERT(e)                  ASSERT(e)
