#include "k32_vista.h"

#define NDEBUG
#include <debug.h>

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

NTSTATUS
NTAPI
NtRemoveIoCompletionEx(HANDLE IoCompletionHandle,
                       PFILE_IO_COMPLETION_INFORMATION IoCompletionInformation,
                       ULONG Count,
                       PULONG NumEntriesRemoved,
                       PLARGE_INTEGER Timeout,
                       BOOLEAN Alertable);

BOOL
WINAPI
GetQueuedCompletionStatusEx(HANDLE CompletionPort,
                             LPOVERLAPPED_ENTRY CompletionEntries,
                             ULONG EntryCount,
                             PULONG EntriesRemoved,
                             DWORD Milliseconds,
                             BOOL Alertable)
{
    FILE_IO_COMPLETION_INFORMATION StackEntries[8];
    PFILE_IO_COMPLETION_INFORMATION IoEntries = StackEntries;
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER TimeoutPointer = NULL;
    PVOID ProcessHeap = RtlGetProcessHeap();
    NTSTATUS Status;
    ULONG i;

    if (!CompletionEntries || !EntryCount || !EntriesRemoved)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *EntriesRemoved = 0;

    if (EntryCount > ARRAYSIZE(StackEntries))
    {
        SIZE_T AllocationSize = EntryCount * sizeof(FILE_IO_COMPLETION_INFORMATION);
        IoEntries = RtlAllocateHeap(ProcessHeap, 0, AllocationSize);
        if (!IoEntries)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }

    if (Milliseconds != INFINITE)
    {
        Timeout.QuadPart = -(LONGLONG)Milliseconds * 10000;
        TimeoutPointer = &Timeout;
    }

    Status = NtRemoveIoCompletionEx(CompletionPort,
                                    IoEntries,
                                    EntryCount,
                                    EntriesRemoved,
                                    TimeoutPointer,
                                    Alertable);

    if (NT_SUCCESS(Status))
    {
        for (i = 0; i < *EntriesRemoved; ++i)
        {
            CompletionEntries[i].lpCompletionKey = (ULONG_PTR)IoEntries[i].KeyContext;
            CompletionEntries[i].lpOverlapped = (LPOVERLAPPED)IoEntries[i].ApcContext;
            CompletionEntries[i].Internal = (ULONG_PTR)IoEntries[i].IoStatusBlock.Status;
            CompletionEntries[i].dwNumberOfBytesTransferred = (DWORD)IoEntries[i].IoStatusBlock.Information;
        }

        if (IoEntries != StackEntries)
            RtlFreeHeap(ProcessHeap, 0, IoEntries);
        return TRUE;
    }

    if (IoEntries != StackEntries)
        RtlFreeHeap(ProcessHeap, 0, IoEntries);

    RtlZeroMemory(CompletionEntries, EntryCount * sizeof(*CompletionEntries));

    switch (Status)
    {
        case STATUS_TIMEOUT:
            SetLastError(WAIT_TIMEOUT);
            break;
        case STATUS_USER_APC:
        case STATUS_ALERTED:
            SetLastError(WAIT_IO_COMPLETION);
            break;
        case STATUS_ABANDONED:
            SetLastError(ERROR_ABANDONED_WAIT_0);
            break;
        default:
            BaseSetLastNTError(Status);
            break;
    }

    return FALSE;
}
