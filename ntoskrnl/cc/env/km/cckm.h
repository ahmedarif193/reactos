/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/env/km/cckm.h
 * PURPOSE:     Kernel environment for the cache manager
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <ntoskrnl.h>

#define CC_ENGINE_TAG                   'gnEC'

typedef KSPIN_LOCK CC_LOCK, *PCC_LOCK;
typedef ERESOURCE CC_RESOURCE;

FORCEINLINE
BOOLEAN
CcResourceInitialize(_Out_ CC_RESOURCE *Resource)
{
    return (BOOLEAN)NT_SUCCESS(ExInitializeResourceLite(Resource));
}

FORCEINLINE
VOID
CcResourceDelete(_Inout_ CC_RESOURCE *Resource)
{
    ExDeleteResourceLite(Resource);
}

FORCEINLINE
VOID
CcResourceAcquireShared(_Inout_ CC_RESOURCE *Resource)
{
    KeEnterCriticalRegion();
    NT_VERIFY(ExAcquireResourceSharedLite(Resource, TRUE));
}

FORCEINLINE
VOID
CcResourceAcquireExclusive(_Inout_ CC_RESOURCE *Resource)
{
    KeEnterCriticalRegion();
    NT_VERIFY(ExAcquireResourceExclusiveLite(Resource, TRUE));
}

FORCEINLINE
VOID
CcResourceRelease(_Inout_ CC_RESOURCE *Resource)
{
    ExReleaseResourceLite(Resource);
    KeLeaveCriticalRegion();
}

#define CC_CACHE_ALIGNED                DECLSPEC_CACHEALIGN
#define CC_CURRENT_PROCESSOR()          KeGetCurrentProcessorNumber()
#define CC_LOCK_INIT(l)                 KeInitializeSpinLock(l)
#define CC_LOCK_ACQUIRE(l, i)           KeAcquireSpinLock((l), (i))
#define CC_LOCK_RELEASE(l, i)           KeReleaseSpinLock((l), (i))
#define CC_RESOURCE_INIT(r)             CcResourceInitialize(r)
#define CC_RESOURCE_DELETE(r)           CcResourceDelete(r)
#define CC_RESOURCE_ACQUIRE_SHARED(r)   CcResourceAcquireShared(r)
#define CC_RESOURCE_ACQUIRE_EXCLUSIVE(r) CcResourceAcquireExclusive(r)
#define CC_RESOURCE_RELEASE(r)          CcResourceRelease(r)
#define CC_ALLOCATE(b)                  ExAllocatePoolWithTag(NonPagedPool, (b), CC_ENGINE_TAG)
#define CC_FREE(p)                      ExFreePoolWithTag((p), CC_ENGINE_TAG)
#define CC_ATOMIC_ADD32(v, d)           InterlockedExchangeAdd((v), (d))
#define CC_ATOMIC_ADD64(v, d)           InterlockedExchangeAdd64((v), (d))
#define CC_ATOMIC_READ32(v)             (*(volatile LONG *)(v))
#define CC_ATOMIC_WRITE32(v, n)         InterlockedExchange((volatile LONG *)(v), (LONG)(n))
#define CC_ATOMIC_READ64(v)             (*(volatile LONG64 *)(v))
#define CC_ATOMIC_WRITE64(v, n)         InterlockedExchange64((volatile LONG64 *)(v), (LONG64)(n))
#define CC_POPCOUNT64(x)                ((ULONG)__builtin_popcountll(x))
#define CC_ASSERT(e)                    ASSERT(e)
