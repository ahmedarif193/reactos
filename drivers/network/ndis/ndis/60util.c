/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60util.c
 * PURPOSE:     NDIS 6 utility primitives.
 *
 *              General-purpose primitives every NDIS 6 driver expects from
 *              ndis.sys but that don't fit cleanly into the lifecycle
 *              (60adapter.c), datapath (60thunk_*.c), or registration
 *              (60driver.c, 60stubs.c) files:
 *
 *                - Timer object API (NdisAllocateTimerObject etc.) —
 *                  KTIMER + KDPC wrapper used for periodic work
 *                - RW lock API (NdisAllocateRWLock etc.) — shared/exclusive
 *                  synchronization primitive on top of EX_PUSH_LOCK
 *                - NdisAllocateNetBufferMdlAndData — combined helper used
 *                  by drivers building synthetic NBs from a backing buffer
 *
 *              The legacy DDK header doesn't carry these declarations
 *              (NDIS_TIMER_CHARACTERISTICS, NDIS_RW_LOCK_EX, etc.), so
 *              ndis6_internal.h declares the prototypes locally and the
 *              .spec file exports them.
 *
 *              Created on the dev-nt6-1 branch as part of the NDIS 6.20
 *              utility surface (Phase 9 of the bridge plan).
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"

#define NDIS6_TIMER_TAG     'tTNn'  /* "nNTt" */
#define NDIS6_RWLOCK_TAG    'lWNn'  /* "nNWl" */

/* ============================================================================
 *  NDIS 6 timer object
 *
 *  An NDIS 6 timer object is a KTIMER + KDPC pair plus the driver's
 *  NDIS_TIMER_FUNCTION callback and a per-callback context. The bridge
 *  wraps it in a private struct that the driver receives as an opaque
 *  NDIS_HANDLE; on every Set/Cancel call we round-trip back through the
 *  handle to find the underlying kernel objects.
 * ============================================================================ */

typedef VOID (NTAPI *PNDIS6_TIMER_CALLBACK)(
    PVOID SystemSpecific1,
    PVOID FunctionContext,
    PVOID SystemSpecific2,
    PVOID SystemSpecific3);

typedef struct _NDIS6_TIMER_OBJECT
{
    ULONG                    Magic;
    KTIMER                   Timer;
    KDPC                     Dpc;
    PNDIS6_TIMER_CALLBACK    Function;
    PVOID                    FunctionContext;
    LONG                     PeriodMs;
} NDIS6_TIMER_OBJECT, *PNDIS6_TIMER_OBJECT;

#define NDIS6_TIMER_MAGIC   0xB16CB16D

static VOID NTAPI
Ndis6TimerDpcWrapper(
    _In_     PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PNDIS6_TIMER_OBJECT TimerObject = (PNDIS6_TIMER_OBJECT)DeferredContext;

    if (TimerObject == NULL || TimerObject->Magic != NDIS6_TIMER_MAGIC ||
        TimerObject->Function == NULL)
    {
        return;
    }

    /* NDIS_TIMER_FUNCTION signature is
     *   (SystemSpecific1, FunctionContext, SystemSpecific2, SystemSpecific3)
     * We pass the KDPC as SystemSpecific1 (matches Windows convention),
     * the user's stored FunctionContext as the second arg, and the
     * system DPC arguments through unchanged. */
    TimerObject->Function(
        Dpc,
        TimerObject->FunctionContext,
        SystemArgument1,
        SystemArgument2);
}

NDIS_STATUS
NTAPI
NdisAllocateTimerObject(
    _In_opt_ NDIS_HANDLE                  NdisHandle,
    _In_     PNDIS_TIMER_CHARACTERISTICS  TimerCharacteristics,
    _Out_    PNDIS_HANDLE                 pTimerObject)
{
    PNDIS6_TIMER_OBJECT TimerObject;

    UNREFERENCED_PARAMETER(NdisHandle);

    if (TimerCharacteristics == NULL || pTimerObject == NULL ||
        TimerCharacteristics->TimerFunction == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    *pTimerObject = NULL;

    TimerObject = (PNDIS6_TIMER_OBJECT)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_TIMER_OBJECT), NDIS6_TIMER_TAG);
    if (TimerObject == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(TimerObject, sizeof(*TimerObject));
    TimerObject->Magic           = NDIS6_TIMER_MAGIC;
    TimerObject->Function        = (PNDIS6_TIMER_CALLBACK)TimerCharacteristics->TimerFunction;
    TimerObject->FunctionContext = TimerCharacteristics->FunctionContext;
    TimerObject->PeriodMs        = 0;

    KeInitializeTimer(&TimerObject->Timer);
    KeInitializeDpc(&TimerObject->Dpc, Ndis6TimerDpcWrapper, TimerObject);

    *pTimerObject = (NDIS_HANDLE)TimerObject;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisFreeTimerObject(
    _In_ NDIS_HANDLE TimerObject)
{
    PNDIS6_TIMER_OBJECT t = (PNDIS6_TIMER_OBJECT)TimerObject;

    if (t == NULL || t->Magic != NDIS6_TIMER_MAGIC)
        return;

    /* Cancel any pending fire and flush queued DPCs. */
    KeCancelTimer(&t->Timer);
    KeRemoveQueueDpc(&t->Dpc);

    t->Magic = 0;
    ExFreePoolWithTag(t, NDIS6_TIMER_TAG);
}

BOOLEAN
NTAPI
NdisSetTimerObject(
    _In_     NDIS_HANDLE   TimerObject,
    _In_     LARGE_INTEGER DueTime,
    _In_opt_ LONG          MillisecondsPeriod,
    _In_opt_ PVOID         FunctionContext)
{
    PNDIS6_TIMER_OBJECT t = (PNDIS6_TIMER_OBJECT)TimerObject;
    BOOLEAN WasInQueue;

    if (t == NULL || t->Magic != NDIS6_TIMER_MAGIC)
        return FALSE;

    /* The driver may pass a fresh FunctionContext at each Set call to
     * override what was registered at allocation time. NULL preserves
     * the original. */
    if (FunctionContext != NULL)
        t->FunctionContext = FunctionContext;

    t->PeriodMs = MillisecondsPeriod;

    if (MillisecondsPeriod > 0)
    {
        WasInQueue = KeSetTimerEx(&t->Timer, DueTime,
                                  MillisecondsPeriod, &t->Dpc);
    }
    else
    {
        WasInQueue = KeSetTimer(&t->Timer, DueTime, &t->Dpc);
    }

    return WasInQueue;
}

BOOLEAN
NTAPI
NdisCancelTimerObject(
    _In_ NDIS_HANDLE TimerObject)
{
    PNDIS6_TIMER_OBJECT t = (PNDIS6_TIMER_OBJECT)TimerObject;

    if (t == NULL || t->Magic != NDIS6_TIMER_MAGIC)
        return FALSE;

    return KeCancelTimer(&t->Timer);
}

/* ============================================================================
 *  Status and interface-identity helpers used by NetAdapterCx.
 * ============================================================================ */

NTSTATUS
NTAPI
NdisConvertNdisStatusToNtStatus(
    _In_ NDIS_STATUS Status)
{
    /* Modern NDIS status values used by NetAdapterCx are NTSTATUS values.
     * Keep the conversion explicit so legacy 0xC001xxxx values do not get
     * silently reported as success. */
    switch (Status)
    {
        case NDIS_STATUS_CLOSING:
        case NDIS_STATUS_ADAPTER_NOT_READY:
            return STATUS_DEVICE_NOT_READY;

        case NDIS_STATUS_BAD_VERSION:
        case NDIS_STATUS_BAD_CHARACTERISTICS:
            return STATUS_REVISION_MISMATCH;

        case NDIS_STATUS_ADAPTER_NOT_FOUND:
            return STATUS_NO_SUCH_DEVICE;

        case NDIS_STATUS_INVALID_LENGTH:
        case NDIS_STATUS_BUFFER_TOO_SHORT:
            return STATUS_BUFFER_TOO_SMALL;

        case NDIS_STATUS_INVALID_DATA:
        case NDIS_STATUS_INVALID_OID:
            return STATUS_INVALID_PARAMETER;

        default:
            return (NTSTATUS)Status;
    }
}

NDIS_STATUS
NTAPI
NdisConvertNtStatusToNdisStatus(
    _In_ NTSTATUS Status)
{
    return (NDIS_STATUS)Status;
}

NDIS_STATUS
NTAPI
NdisIfGetInterfaceIndexFromNetLuid(
    _In_ NET_LUID NetLuid,
    _Out_ PNET_IFINDEX IfIndex)
{
    if (IfIndex == NULL || NetLuid.Info.NetLuidIndex == 0)
        return NDIS_STATUS_INTERFACE_NOT_FOUND;

    *IfIndex = (NET_IFINDEX)NetLuid.Info.NetLuidIndex;
    return NDIS_STATUS_SUCCESS;
}

/* ============================================================================
 *  NDIS 6 RW lock
 *
 *  Wraps ERESOURCE which gives us shared/exclusive semantics. The
 *  driver-visible PNDIS_RW_LOCK_EX is the bridge's wrapper; LOCK_STATE_EX
 *  is used to remember whether the caller acquired shared or exclusive
 *  so the matching Release path runs.
 * ============================================================================ */

struct _NDIS_RW_LOCK_EX
{
    ULONG       Magic;
    ERESOURCE   Resource;
};

#define NDIS6_RWLOCK_MAGIC  0xB16CB16E

PNDIS_RW_LOCK_EX
NTAPI
NdisAllocateRWLock(
    _In_opt_ NDIS_HANDLE NdisHandle)
{
    PNDIS_RW_LOCK_EX Lock;

    UNREFERENCED_PARAMETER(NdisHandle);

    Lock = (PNDIS_RW_LOCK_EX)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(*Lock), NDIS6_RWLOCK_TAG);
    if (Lock == NULL)
        return NULL;

    Lock->Magic = NDIS6_RWLOCK_MAGIC;
    ExInitializeResourceLite(&Lock->Resource);
    return Lock;
}

VOID
NTAPI
NdisFreeRWLock(
    _In_ PNDIS_RW_LOCK_EX Lock)
{
    if (Lock == NULL || Lock->Magic != NDIS6_RWLOCK_MAGIC)
        return;
    ExDeleteResourceLite(&Lock->Resource);
    Lock->Magic = 0;
    ExFreePoolWithTag(Lock, NDIS6_RWLOCK_TAG);
}

VOID
NTAPI
NdisAcquireRWLockRead(
    _In_  PNDIS_RW_LOCK_EX Lock,
    _Out_ PLOCK_STATE_EX   LockState,
    _In_  UCHAR            Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    if (Lock == NULL || Lock->Magic != NDIS6_RWLOCK_MAGIC || LockState == NULL)
        return;

    /* ERESOURCE requires APC disabled before acquire. */
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Lock->Resource, TRUE);

    LockState->Reserved[0] = (PVOID)(ULONG_PTR)1;  /* "shared" */
    LockState->Reserved[1] = NULL;
}

VOID
NTAPI
NdisAcquireRWLockWrite(
    _In_  PNDIS_RW_LOCK_EX Lock,
    _Out_ PLOCK_STATE_EX   LockState,
    _In_  UCHAR            Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    if (Lock == NULL || Lock->Magic != NDIS6_RWLOCK_MAGIC || LockState == NULL)
        return;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Lock->Resource, TRUE);

    LockState->Reserved[0] = (PVOID)(ULONG_PTR)2;  /* "exclusive" */
    LockState->Reserved[1] = NULL;
}

VOID
NTAPI
NdisReleaseRWLock(
    _In_ PNDIS_RW_LOCK_EX Lock,
    _In_ PLOCK_STATE_EX   LockState)
{
    if (Lock == NULL || Lock->Magic != NDIS6_RWLOCK_MAGIC || LockState == NULL)
        return;

    ExReleaseResourceLite(&Lock->Resource);
    KeLeaveCriticalRegion();

    LockState->Reserved[0] = NULL;
}

/* ============================================================================
 *  NdisAllocateNetBufferMdlAndData
 *
 *  Combined helper that allocates a NET_BUFFER from the given pool with
 *  a freshly created MDL and a backing nonpaged buffer. The buffer's
 *  size comes from the pool's DataSize parameter set at pool creation.
 *  Returns NULL on any failure.
 * ============================================================================ */

PNET_BUFFER
NTAPI
NdisAllocateNetBufferMdlAndData(
    _In_ NDIS_HANDLE PoolHandle)
{
    /* Walk into the NB pool descriptor (defined in 60nbl.c). The bridge
     * stores DataSize there from NdisAllocateNetBufferPool. */
    typedef struct _NDIS6_NB_POOL_PEEK
    {
        ULONG       Magic;
        ULONG       PoolTag;
        ULONG       DataSize;
    } NDIS6_NB_POOL_PEEK, *PNDIS6_NB_POOL_PEEK;

    #define NDIS6_NB_POOL_MAGIC_LOCAL   0xB1601001

    PNDIS6_NB_POOL_PEEK Pool = (PNDIS6_NB_POOL_PEEK)PoolHandle;
    PNET_BUFFER         Nb;
    PMDL                Mdl;
    PVOID               DataBuffer;
    ULONG               DataSize;

    if (Pool == NULL || Pool->Magic != NDIS6_NB_POOL_MAGIC_LOCAL)
        return NULL;

    DataSize = Pool->DataSize;
    if (DataSize == 0)
        DataSize = 2048;  /* sensible default */

    DataBuffer = ExAllocatePoolWithTag(NonPagedPool, DataSize, Pool->PoolTag);
    if (DataBuffer == NULL)
        return NULL;

    Mdl = IoAllocateMdl(DataBuffer, DataSize, FALSE, FALSE, NULL);
    if (Mdl == NULL)
    {
        ExFreePoolWithTag(DataBuffer, Pool->PoolTag);
        return NULL;
    }
    MmBuildMdlForNonPagedPool(Mdl);

    Nb = NdisAllocateNetBuffer(PoolHandle, Mdl, 0, DataSize);
    if (Nb == NULL)
    {
        IoFreeMdl(Mdl);
        ExFreePoolWithTag(DataBuffer, Pool->PoolTag);
        return NULL;
    }

    return Nb;
}

/* ============================================================================
 *  E3: miscellaneous utility APIs
 *
 *  NdisMSleep and NdisGetCurrentProcessorCounts are already implemented
 *  in the legacy NDIS 5 library (miniport.c / misc.c) and callable from
 *  NDIS 6 drivers unchanged — nothing extra needed here.
 *
 *  NdisGetSystemUpTimeEx is new (NDIS 6.0+) and returns a LARGE_INTEGER
 *  in 100ns units since boot. Wraps KeQueryTickCount * KeQueryTimeIncrement.
 * ============================================================================ */

VOID
NTAPI
NdisGetSystemUpTimeEx(
    _Out_ PLARGE_INTEGER pSystemUpTime)
{
    LARGE_INTEGER TickCount;
    if (pSystemUpTime == NULL)
        return;
    KeQueryTickCount(&TickCount);
    /* TickCount is in units of KeQueryTimeIncrement (100ns). The
     * result is a straight 100ns-since-boot value. */
    pSystemUpTime->QuadPart = TickCount.QuadPart * KeQueryTimeIncrement();
}

/* ============================================================================
 *  E4: NDIS 6.30+ polling API — NdisRegisterPoll / NdisMPollComplete
 *
 *  The polling API lets a driver run its RX/TX work via a kernel poll
 *  callback instead of DPC-based dispatch. We model each registered poll
 *  as a KDPC that the driver can request via NdisRequestPoll; the DPC
 *  calls back into the driver's NDIS_POLL routine, which does work and
 *  optionally re-requests.
 *
 *  This is a minimal functional implementation — no NUMA affinity, no
 *  budget tracking, no separate DPC queue priority. Sufficient for
 *  drivers that opt-in but not full-performance poll mode.
 * ============================================================================ */

typedef VOID (NTAPI *PNDIS6_POLL_HANDLER)(
    _In_ PVOID PollContext,
    _In_ PVOID Parameters);

typedef struct _NDIS6_POLL_CONTEXT
{
    ULONG                   Magic;
    KDPC                    Dpc;
    PNDIS6_POLL_HANDLER     PollHandler;
    PVOID                   PollContext;
    LONG                    RequestCount;       /* atomic: pending requests */
    BOOLEAN                 Unregistered;
} NDIS6_POLL_CONTEXT, *PNDIS6_POLL_CONTEXT;

#define NDIS6_POLL_MAGIC   0x504F4C4CU  /* 'POLL' */
#define NDIS6_POLL_TAG     'PlNn'

static VOID NTAPI
Ndis6PollDpcRoutine(
    _In_     PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PNDIS6_POLL_CONTEXT Poll = (PNDIS6_POLL_CONTEXT)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Poll == NULL || Poll->Magic != NDIS6_POLL_MAGIC || Poll->Unregistered)
        return;

    if (Poll->PollHandler != NULL)
    {
        /* Windows passes a NDIS_POLL_PARAMETERS struct. Most driver-side
         * NDIS_POLL routines tolerate NULL (they only look at
         * Parameters->MaxNblsToIndicate and fall through to the "no
         * limit" path when it's 0). */
        Poll->PollHandler(Poll->PollContext, NULL);
    }

    /* If the driver re-requested a poll while we were inside the handler,
     * queue another DPC round. Otherwise clear the count. */
    if (InterlockedExchange(&Poll->RequestCount, 0) > 1)
    {
        InterlockedIncrement(&Poll->RequestCount);
        KeInsertQueueDpc(&Poll->Dpc, NULL, NULL);
    }
}

NDIS_STATUS
NTAPI
NdisRegisterPoll(
    _In_     NDIS_HANDLE   NdisHandle,
    _In_opt_ PVOID         PollContext,
    _In_     PVOID         PollCharacteristics,  /* NDIS_POLL_CHARACTERISTICS */
    _Out_    PNDIS_HANDLE  PollHandle)
{
    PNDIS6_POLL_CONTEXT Poll;
    PNDIS6_POLL_HANDLER Handler = NULL;

    UNREFERENCED_PARAMETER(NdisHandle);

    if (PollHandle == NULL || PollCharacteristics == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* NDIS_POLL_CHARACTERISTICS layout: Header + PollHandler +
     * SetPollNotificationHandler. We read PollHandler at offset
     * sizeof(NDIS_OBJECT_HEADER). */
    Handler = *(PNDIS6_POLL_HANDLER*)((PUCHAR)PollCharacteristics +
                                      sizeof(NDIS_OBJECT_HEADER));
    if (Handler == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Poll = (PNDIS6_POLL_CONTEXT)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_POLL_CONTEXT), NDIS6_POLL_TAG);
    if (Poll == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Poll, sizeof(*Poll));
    Poll->Magic        = NDIS6_POLL_MAGIC;
    Poll->PollHandler  = Handler;
    Poll->PollContext  = PollContext;
    Poll->RequestCount = 0;
    KeInitializeDpc(&Poll->Dpc, Ndis6PollDpcRoutine, Poll);

    *PollHandle = (NDIS_HANDLE)Poll;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisDeregisterPoll(
    _In_ NDIS_HANDLE PollHandle)
{
    PNDIS6_POLL_CONTEXT Poll = (PNDIS6_POLL_CONTEXT)PollHandle;
    if (Poll == NULL || Poll->Magic != NDIS6_POLL_MAGIC)
        return;

    Poll->Unregistered = TRUE;
    /* Flush any pending DPC. */
    KeRemoveQueueDpc(&Poll->Dpc);

    Poll->Magic = 0;
    ExFreePoolWithTag(Poll, NDIS6_POLL_TAG);
}

VOID
NTAPI
NdisRequestPoll(
    _In_ NDIS_HANDLE PollHandle,
    _In_opt_ PVOID   Reserved)
{
    PNDIS6_POLL_CONTEXT Poll = (PNDIS6_POLL_CONTEXT)PollHandle;

    UNREFERENCED_PARAMETER(Reserved);

    if (Poll == NULL || Poll->Magic != NDIS6_POLL_MAGIC || Poll->Unregistered)
        return;

    /* Only queue a DPC on the 0→1 transition. Subsequent requests just
     * bump the counter; the running handler will re-queue itself on exit
     * if RequestCount > 1. */
    if (InterlockedIncrement(&Poll->RequestCount) == 1)
        KeInsertQueueDpc(&Poll->Dpc, NULL, NULL);
}

/* EOF */
