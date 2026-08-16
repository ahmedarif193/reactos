/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Wait-on-address synchronization primitives
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl_vista.h>

#define NDEBUG
#include <debug.h>

#define RTL_WAIT_BUCKET_COUNT 256
#define RTL_WAIT_BUCKET_MASK (RTL_WAIT_BUCKET_COUNT - 1)
#define RTL_WAKE_BATCH_SIZE 256

C_ASSERT((RTL_WAIT_BUCKET_COUNT & RTL_WAIT_BUCKET_MASK) == 0);

typedef struct _RTL_WAIT_ON_ADDRESS_ENTRY
{
    LIST_ENTRY ListEntry;
    const VOID *Address;
    HANDLE ThreadId;
} RTL_WAIT_ON_ADDRESS_ENTRY, *PRTL_WAIT_ON_ADDRESS_ENTRY;

typedef struct _RTL_WAIT_ON_ADDRESS_BUCKET
{
    LIST_ENTRY ListHead;
    volatile LONG Lock;
} RTL_WAIT_ON_ADDRESS_BUCKET, *PRTL_WAIT_ON_ADDRESS_BUCKET;

static RTL_WAIT_ON_ADDRESS_BUCKET RtlpWaitOnAddressBuckets[RTL_WAIT_BUCKET_COUNT];

static
PRTL_WAIT_ON_ADDRESS_BUCKET
RtlpGetWaitOnAddressBucket(
    _In_ const VOID *Address)
{
    ULONG_PTR Value = (ULONG_PTR)Address;

    return &RtlpWaitOnAddressBuckets[(Value >> 4) & RTL_WAIT_BUCKET_MASK];
}

static
VOID
RtlpAcquireWaitOnAddressBucket(
    _Inout_ PRTL_WAIT_ON_ADDRESS_BUCKET Bucket)
{
    while (InterlockedCompareExchange(&Bucket->Lock, 1, 0) != 0)
        YieldProcessor();

    if (Bucket->ListHead.Flink == NULL)
        InitializeListHead(&Bucket->ListHead);
}

static
VOID
RtlpReleaseWaitOnAddressBucket(
    _Inout_ PRTL_WAIT_ON_ADDRESS_BUCKET Bucket)
{
    InterlockedExchange(&Bucket->Lock, 0);
}

static
BOOLEAN
RtlpWaitOnAddressMatches(
    _In_ const VOID *Address,
    _In_ const VOID *CompareAddress,
    _In_ SIZE_T Size)
{
    switch (Size)
    {
        case sizeof(UCHAR):
            return *(const volatile UCHAR *)Address == *(const UCHAR *)CompareAddress;

        case sizeof(USHORT):
            return *(const volatile USHORT *)Address == *(const USHORT *)CompareAddress;

        case sizeof(ULONG):
            return *(const volatile ULONG *)Address == *(const ULONG *)CompareAddress;

        case sizeof(ULONG64):
            return *(const volatile ULONG64 *)Address == *(const ULONG64 *)CompareAddress;

        default:
            return FALSE;
    }
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlWaitOnAddress(
    _In_ const VOID *Address,
    _In_ const VOID *CompareAddress,
    _In_ SIZE_T AddressSize,
    _In_opt_ const LARGE_INTEGER *Timeout)
{
    PRTL_WAIT_ON_ADDRESS_BUCKET Bucket;
    RTL_WAIT_ON_ADDRESS_ENTRY Entry;
    NTSTATUS Status;

    if (AddressSize != sizeof(UCHAR) && AddressSize != sizeof(USHORT) && AddressSize != sizeof(ULONG) && AddressSize != sizeof(ULONG64))
        return STATUS_INVALID_PARAMETER;

    Bucket = RtlpGetWaitOnAddressBucket(Address);
    Entry.Address = Address;
    Entry.ThreadId = NtCurrentTeb()->ClientId.UniqueThread;

    RtlpAcquireWaitOnAddressBucket(Bucket);
    if (!RtlpWaitOnAddressMatches(Address, CompareAddress, AddressSize))
    {
        RtlpReleaseWaitOnAddressBucket(Bucket);
        return STATUS_SUCCESS;
    }

    InsertTailList(&Bucket->ListHead, &Entry.ListEntry);
    RtlpReleaseWaitOnAddressBucket(Bucket);

    Status = NtWaitForAlertByThreadId(NULL, (PLARGE_INTEGER)Timeout);

    RtlpAcquireWaitOnAddressBucket(Bucket);
    if (Entry.Address != NULL)
    {
        RemoveEntryList(&Entry.ListEntry);
        Entry.Address = NULL;
    }
    RtlpReleaseWaitOnAddressBucket(Bucket);

    return Status == STATUS_ALERTED ? STATUS_SUCCESS : Status;
}

static
VOID
RtlpWakeAddress(
    _In_ const VOID *Address,
    _In_ BOOLEAN WakeAll)
{
    PRTL_WAIT_ON_ADDRESS_BUCKET Bucket;
    HANDLE ThreadIds[RTL_WAKE_BATCH_SIZE];
    ULONG ThreadCount = 0;
    PLIST_ENTRY Current;

    if (Address == NULL)
        return;

    Bucket = RtlpGetWaitOnAddressBucket(Address);
    RtlpAcquireWaitOnAddressBucket(Bucket);

    Current = Bucket->ListHead.Flink;
    while (Current != &Bucket->ListHead)
    {
        PRTL_WAIT_ON_ADDRESS_ENTRY Entry;
        PLIST_ENTRY Next;

        Entry = CONTAINING_RECORD(Current, RTL_WAIT_ON_ADDRESS_ENTRY, ListEntry);
        Next = Current->Flink;

        if (Entry->Address == Address)
        {
            Entry->Address = NULL;
            RemoveEntryList(&Entry->ListEntry);

            /* Keep system calls out of the spin lock for the common case. */
            if (ThreadCount < RTL_WAKE_BATCH_SIZE)
                ThreadIds[ThreadCount++] = Entry->ThreadId;
            else
                NtAlertThreadByThreadId(Entry->ThreadId);

            if (!WakeAll)
                break;
        }

        Current = Next;
    }

    RtlpReleaseWaitOnAddressBucket(Bucket);

    {
        ULONG Index;

        for (Index = 0; Index < ThreadCount; ++Index)
            NtAlertThreadByThreadId(ThreadIds[Index]);
    }
}

/*
 * @implemented
 */
VOID
NTAPI
RtlWakeAddressAll(
    _In_ const VOID *Address)
{
    RtlpWakeAddress(Address, TRUE);
}

/*
 * @implemented
 */
VOID
NTAPI
RtlWakeAddressSingle(
    _In_ const VOID *Address)
{
    RtlpWakeAddress(Address, FALSE);
}
