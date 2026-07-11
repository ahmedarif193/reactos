/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     WaitOnAddress compatibility exports
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

typedef struct _RTL_WAIT_ON_ADDRESS_ENTRY
{
    LIST_ENTRY ListEntry;
    const VOID *Address;
    PVOID WaitKey;
    BOOLEAN Signaled;
} RTL_WAIT_ON_ADDRESS_ENTRY, *PRTL_WAIT_ON_ADDRESS_ENTRY;

static RTL_SRWLOCK RtlpWaitOnAddressLock = RTL_SRWLOCK_INIT;
static LIST_ENTRY RtlpWaitOnAddressList =
{
    &RtlpWaitOnAddressList,
    &RtlpWaitOnAddressList
};

static
BOOLEAN
RtlpIsValidWaitOnAddressSize(SIZE_T Size)
{
    return (Size == 1 || Size == 2 || Size == 4 || Size == 8);
}

static
BOOLEAN
RtlpWaitOnAddressMatches(const VOID *Address,
                         const VOID *CompareAddress,
                         SIZE_T Size)
{
    return RtlCompareMemory(Address, CompareAddress, Size) == Size;
}

/*
 * @implemented
 */
NTSTATUS
WINAPI
RtlWaitOnAddress(const VOID *Address,
                 const VOID *CompareAddress,
                 SIZE_T AddressSize,
                 const LARGE_INTEGER *Timeout)
{
    RTL_WAIT_ON_ADDRESS_ENTRY Entry;
    NTSTATUS Status;

    if (!RtlpIsValidWaitOnAddressSize(AddressSize))
        return STATUS_INVALID_PARAMETER;

    Entry.Address = Address;
    Entry.WaitKey = NULL;
    Entry.Signaled = FALSE;

    RtlAcquireSRWLockExclusive(&RtlpWaitOnAddressLock);

    if (!RtlpWaitOnAddressMatches(Address, CompareAddress, AddressSize))
    {
        RtlReleaseSRWLockExclusive(&RtlpWaitOnAddressLock);
        return STATUS_SUCCESS;
    }

    InsertTailList(&RtlpWaitOnAddressList, &Entry.ListEntry);

    RtlReleaseSRWLockExclusive(&RtlpWaitOnAddressLock);

    Status = NtWaitForKeyedEvent(NULL,
                                 &Entry.WaitKey,
                                 FALSE,
                                 (PLARGE_INTEGER)Timeout);

    RtlAcquireSRWLockExclusive(&RtlpWaitOnAddressLock);

    if (Entry.Signaled)
    {
        /* A waker has removed us from the list and is releasing the keyed event
         * with an infinite timeout. If our own wait did not already consume that
         * release (i.e. we timed out or failed first), consume it now so the
         * waker's NtReleaseKeyedEvent cannot block forever. */
        RtlReleaseSRWLockExclusive(&RtlpWaitOnAddressLock);

        if (Status != STATUS_SUCCESS)
        {
            NtWaitForKeyedEvent(NULL, &Entry.WaitKey, FALSE, NULL);
        }

        return STATUS_SUCCESS;
    }

    /* Not signaled: genuine timeout/failure and no wake is in flight, so it is
     * safe to remove ourselves from the wait list. */
    RemoveEntryList(&Entry.ListEntry);
    RtlReleaseSRWLockExclusive(&RtlpWaitOnAddressLock);

    return Status;
}

static
VOID
RtlpWakeAddress(const VOID *Address,
                BOOLEAN WakeAll)
{
    LIST_ENTRY WakeList;
    PLIST_ENTRY Current, Next;

    if (!Address)
        return;

    InitializeListHead(&WakeList);

    /* Under the lock, claim every matching waiter: remove it from the wait list,
     * mark it signaled and move it to a private list. */
    RtlAcquireSRWLockExclusive(&RtlpWaitOnAddressLock);

    Current = RtlpWaitOnAddressList.Flink;
    while (Current != &RtlpWaitOnAddressList)
    {
        PRTL_WAIT_ON_ADDRESS_ENTRY Entry;

        Entry = CONTAINING_RECORD(Current, RTL_WAIT_ON_ADDRESS_ENTRY, ListEntry);
        Next = Current->Flink;

        if (Entry->Address == Address)
        {
            RemoveEntryList(&Entry->ListEntry);
            Entry->Signaled = TRUE;
            InsertTailList(&WakeList, &Entry->ListEntry);

            if (!WakeAll)
                break;
        }

        Current = Next;
    }

    RtlReleaseSRWLockExclusive(&RtlpWaitOnAddressLock);

    /* Release the keyed events outside the lock with an infinite timeout so the
     * release reliably rendezvous with the (already committed) waiter -- this
     * closes the lost-wakeup window that a zero timeout would leave open. Once a
     * release pairs with its waiter, that waiter's stack-allocated entry may be
     * freed, so capture the next node before releasing the current one. */
    Current = WakeList.Flink;
    while (Current != &WakeList)
    {
        PRTL_WAIT_ON_ADDRESS_ENTRY Entry;

        Entry = CONTAINING_RECORD(Current, RTL_WAIT_ON_ADDRESS_ENTRY, ListEntry);
        Next = Current->Flink;

        NtReleaseKeyedEvent(NULL, &Entry->WaitKey, FALSE, NULL);

        Current = Next;
    }
}

/*
 * @implemented
 */
VOID
WINAPI
RtlWakeAddressAll(const VOID *Address)
{
    RtlpWakeAddress(Address, TRUE);
}

/*
 * @implemented
 */
VOID
WINAPI
RtlWakeAddressSingle(const VOID *Address)
{
    RtlpWakeAddress(Address, FALSE);
}
