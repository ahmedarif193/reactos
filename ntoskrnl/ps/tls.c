/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel thread-local storage compatible with the NT PsTls APIs
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINES *******************************************************************/

#define PSP_TLS_FIRST_INDEX 1
#define PSP_TLS_LAST_INDEX  239
#define PSP_TLS_SLOT_COUNT  (PSP_TLS_LAST_INDEX + 1)

/* TYPES *********************************************************************/

typedef VOID
(NTAPI *PPS_TLS_CALLBACK)(
    _In_opt_ PVOID Value
);

typedef struct _PSP_TLS_SLOT
{
    PPS_TLS_CALLBACK Callback;
    BOOLEAN Allocated;
    BOOLEAN Freeing;
} PSP_TLS_SLOT, *PPS_TLS_SLOT;

typedef struct _PSP_TLS_VALUE
{
    PVOID Value;
    PPS_TLS_CALLBACK Callback;
} PSP_TLS_VALUE, *PPS_TLS_VALUE;

typedef struct _PSP_TLS_THREAD_DATA
{
    LIST_ENTRY ListEntry;
    PETHREAD Thread;
    PSP_TLS_VALUE Slots[PSP_TLS_SLOT_COUNT];
} PSP_TLS_THREAD_DATA, *PPS_TLS_THREAD_DATA;

/* GLOBALS *******************************************************************/

static EX_PUSH_LOCK PspTlsLock;
static LIST_ENTRY PspTlsThreadList;
static PSP_TLS_SLOT PspTlsSlots[PSP_TLS_SLOT_COUNT];

/* PRIVATE FUNCTIONS *********************************************************/

static
PPS_TLS_THREAD_DATA
PspFindTlsThreadData(
    _In_ PETHREAD Thread
)
{
    PLIST_ENTRY Entry;
    PPS_TLS_THREAD_DATA ThreadData;

    for (Entry = PspTlsThreadList.Flink;
         Entry != &PspTlsThreadList;
         Entry = Entry->Flink)
    {
        ThreadData = CONTAINING_RECORD(Entry,
                                       PSP_TLS_THREAD_DATA,
                                       ListEntry);
        if (ThreadData->Thread == Thread)
            return ThreadData;
    }

    return NULL;
}

static
BOOLEAN
PspIsTlsIndexValid(
    _In_ ULONG TlsIndex
)
{
    return (TlsIndex >= PSP_TLS_FIRST_INDEX) &&
           (TlsIndex <= PSP_TLS_LAST_INDEX);
}

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
PspInitializeTls(VOID)
{
    ExInitializePushLock(&PspTlsLock);
    InitializeListHead(&PspTlsThreadList);
    RtlZeroMemory(PspTlsSlots, sizeof(PspTlsSlots));
}

VOID
NTAPI
PspTlsThreadCleanup(
    _In_ PETHREAD Thread
)
{
    PPS_TLS_THREAD_DATA ThreadData;
    PPS_TLS_CALLBACK Callback;
    PVOID Value;
    ULONG Index;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    KeEnterGuardedRegion();
    ExAcquirePushLockExclusive(&PspTlsLock);
    ThreadData = PspFindTlsThreadData(Thread);
    if (ThreadData)
        RemoveEntryList(&ThreadData->ListEntry);
    ExReleasePushLockExclusive(&PspTlsLock);
    KeLeaveGuardedRegion();

    if (!ThreadData)
        return;

    for (Index = PSP_TLS_FIRST_INDEX;
         Index <= PSP_TLS_LAST_INDEX;
         ++Index)
    {
        Value = ThreadData->Slots[Index].Value;
        Callback = ThreadData->Slots[Index].Callback;
        if (Value && Callback)
            Callback(Value);
    }

    ExFreePoolWithTag(ThreadData, TAG_PS_TLS);
}

NTSTATUS
NTAPI
PsTlsAlloc(
    _In_opt_ PVOID Callback,
    _In_ ULONG Flags,
    _Out_ PULONG TlsIndex
)
{
    ULONG Index;
    NTSTATUS Status = STATUS_NO_MEMORY;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    if (Flags != 0)
        return STATUS_INVALID_PARAMETER;

    KeEnterGuardedRegion();
    ExAcquirePushLockExclusive(&PspTlsLock);
    for (Index = PSP_TLS_FIRST_INDEX;
         Index <= PSP_TLS_LAST_INDEX;
         ++Index)
    {
        if (!PspTlsSlots[Index].Allocated &&
            !PspTlsSlots[Index].Freeing)
        {
            PspTlsSlots[Index].Callback = (PPS_TLS_CALLBACK)Callback;
            PspTlsSlots[Index].Allocated = TRUE;
            *TlsIndex = Index;
            Status = STATUS_SUCCESS;
            break;
        }
    }
    ExReleasePushLockExclusive(&PspTlsLock);
    KeLeaveGuardedRegion();

    return Status;
}

VOID
NTAPI
PsTlsFree(
    _In_ ULONG TlsIndex
)
{
    PPS_TLS_THREAD_DATA ThreadData;
    PPS_TLS_CALLBACK Callback;
    PLIST_ENTRY Entry;
    PVOID Value;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    if (!PspIsTlsIndexValid(TlsIndex))
        return;

    KeEnterGuardedRegion();
    ExAcquirePushLockExclusive(&PspTlsLock);
    if (!PspTlsSlots[TlsIndex].Allocated ||
        PspTlsSlots[TlsIndex].Freeing)
    {
        ExReleasePushLockExclusive(&PspTlsLock);
        KeLeaveGuardedRegion();
        return;
    }

    PspTlsSlots[TlsIndex].Freeing = TRUE;
    ExReleasePushLockExclusive(&PspTlsLock);
    KeLeaveGuardedRegion();

    for (;;)
    {
        Value = NULL;
        Callback = NULL;

        KeEnterGuardedRegion();
        ExAcquirePushLockExclusive(&PspTlsLock);
        for (Entry = PspTlsThreadList.Flink;
             Entry != &PspTlsThreadList;
             Entry = Entry->Flink)
        {
            ThreadData = CONTAINING_RECORD(Entry,
                                           PSP_TLS_THREAD_DATA,
                                           ListEntry);
            if (ThreadData->Slots[TlsIndex].Value)
            {
                Value = ThreadData->Slots[TlsIndex].Value;
                Callback = ThreadData->Slots[TlsIndex].Callback;
                ThreadData->Slots[TlsIndex].Value = NULL;
                ThreadData->Slots[TlsIndex].Callback = NULL;
                break;
            }
        }

        if (!Value)
        {
            PspTlsSlots[TlsIndex].Callback = NULL;
            PspTlsSlots[TlsIndex].Allocated = FALSE;
            PspTlsSlots[TlsIndex].Freeing = FALSE;
        }
        ExReleasePushLockExclusive(&PspTlsLock);
        KeLeaveGuardedRegion();

        if (!Value)
            break;
        if (Callback)
            Callback(Value);
    }
}

NTSTATUS
NTAPI
PsTlsGetValue(
    _In_ ULONG TlsIndex,
    _Out_ PVOID *Value
)
{
    PPS_TLS_THREAD_DATA ThreadData;
    PETHREAD Thread;
    NTSTATUS Status;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    Thread = PsGetCurrentThread();
    if (PsIsThreadTerminating(Thread))
        return STATUS_THREAD_IS_TERMINATING;

    if (!PspIsTlsIndexValid(TlsIndex))
        return STATUS_INVALID_PARAMETER;

    KeEnterGuardedRegion();
    ExAcquirePushLockExclusive(&PspTlsLock);
    ThreadData = PspFindTlsThreadData(Thread);
    *Value = ThreadData ? ThreadData->Slots[TlsIndex].Value : NULL;
    Status = STATUS_SUCCESS;
    ExReleasePushLockExclusive(&PspTlsLock);
    KeLeaveGuardedRegion();

    return Status;
}

NTSTATUS
NTAPI
PsTlsSetValue(
    _In_ ULONG TlsIndex,
    _In_opt_ PVOID Value
)
{
    PPS_TLS_THREAD_DATA ThreadData, NewThreadData = NULL;
    PETHREAD Thread;
    NTSTATUS Status;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    Thread = PsGetCurrentThread();
    if (PsIsThreadTerminating(Thread))
        return STATUS_THREAD_IS_TERMINATING;

    if (!PspIsTlsIndexValid(TlsIndex))
        return STATUS_INVALID_PARAMETER;

    for (;;)
    {
        KeEnterGuardedRegion();
        ExAcquirePushLockExclusive(&PspTlsLock);
        ThreadData = PspFindTlsThreadData(Thread);
        if (ThreadData)
        {
            ThreadData->Slots[TlsIndex].Value = Value;
            ThreadData->Slots[TlsIndex].Callback =
                (Value && PspTlsSlots[TlsIndex].Allocated) ?
                    PspTlsSlots[TlsIndex].Callback : NULL;
            Status = STATUS_SUCCESS;
        }
        else if (NewThreadData)
        {
            NewThreadData->Thread = Thread;
            InsertTailList(&PspTlsThreadList,
                           &NewThreadData->ListEntry);
            NewThreadData->Slots[TlsIndex].Value = Value;
            NewThreadData->Slots[TlsIndex].Callback =
                (Value && PspTlsSlots[TlsIndex].Allocated) ?
                    PspTlsSlots[TlsIndex].Callback : NULL;
            NewThreadData = NULL;
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_MORE_PROCESSING_REQUIRED;
        }
        ExReleasePushLockExclusive(&PspTlsLock);
        KeLeaveGuardedRegion();

        if (Status != STATUS_MORE_PROCESSING_REQUIRED)
            break;

        NewThreadData = ExAllocatePoolZero(NonPagedPoolNx,
                                           sizeof(*NewThreadData),
                                           TAG_PS_TLS);
        if (!NewThreadData)
            return STATUS_NO_MEMORY;
    }

    if (NewThreadData)
        ExFreePoolWithTag(NewThreadData, TAG_PS_TLS);

    return Status;
}
