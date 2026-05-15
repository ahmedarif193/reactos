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
    BOOLEAN ListRemovalHandled;
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
    Entry.ListRemovalHandled = FALSE;

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

    if (!Entry.ListRemovalHandled)
    {
        RtlAcquireSRWLockExclusive(&RtlpWaitOnAddressLock);

        if (!Entry.ListRemovalHandled)
        {
            RemoveEntryList(&Entry.ListEntry);
            Entry.ListRemovalHandled = TRUE;
        }

        RtlReleaseSRWLockExclusive(&RtlpWaitOnAddressLock);
    }

    return Status;
}

static
VOID
RtlpWakeAddress(const VOID *Address,
                BOOLEAN WakeAll)
{
    PLIST_ENTRY Current;
    LARGE_INTEGER Timeout;

    if (!Address)
        return;

    Timeout.QuadPart = 0;

    RtlAcquireSRWLockExclusive(&RtlpWaitOnAddressLock);

    Current = RtlpWaitOnAddressList.Flink;
    while (Current != &RtlpWaitOnAddressList)
    {
        PRTL_WAIT_ON_ADDRESS_ENTRY Entry;
        PLIST_ENTRY Next;
        NTSTATUS Status;

        Entry = CONTAINING_RECORD(Current, RTL_WAIT_ON_ADDRESS_ENTRY, ListEntry);
        Next = Current->Flink;

        if (Entry->Address == Address)
        {
            Status = NtReleaseKeyedEvent(NULL,
                                         &Entry->WaitKey,
                                         FALSE,
                                         &Timeout);
            if (NT_SUCCESS(Status))
            {
                RemoveEntryList(&Entry->ListEntry);
                Entry->Address = NULL;
                Entry->ListRemovalHandled = TRUE;

                if (!WakeAll)
                    break;
            }
        }

        Current = Next;
    }

    RtlReleaseSRWLockExclusive(&RtlpWaitOnAddressLock);
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
