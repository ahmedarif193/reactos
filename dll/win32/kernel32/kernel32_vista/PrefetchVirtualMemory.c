/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     PrefetchVirtualMemory
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/memory.c
 */

#include "k32_vista.h"

typedef MEMORY_RANGE_ENTRY K32_MEMORY_RANGE_ENTRY;
typedef PMEMORY_RANGE_ENTRY PK32_MEMORY_RANGE_ENTRY;

BOOL
WINAPI
PrefetchVirtualMemory(
    _In_ HANDLE hProcess,
    _In_ ULONG_PTR NumberOfEntries,
    _In_ PK32_MEMORY_RANGE_ENTRY VirtualAddresses,
    _In_ ULONG Flags)
{
    SYSTEM_INFO SystemInfo;
    ULONG_PTR EntryIndex;

    if (Flags != 0 || NumberOfEntries == 0 || !VirtualAddresses)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

#if defined(_M_AMD64) || defined(_M_ARM64)
    NTSTATUS Status;
    ULONG PrefetchFlags = Flags;

    Status = NtSetInformationVirtualMemory(hProcess, 0, NumberOfEntries, VirtualAddresses, &PrefetchFlags, sizeof(PrefetchFlags));
    if (Status != STATUS_NOT_SUPPORTED)
    {
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }
        return TRUE;
    }
#endif

    GetSystemInfo(&SystemInfo);

    for (EntryIndex = 0; EntryIndex < NumberOfEntries; EntryIndex++)
    {
        ULONG_PTR Address = (ULONG_PTR)VirtualAddresses[EntryIndex].VirtualAddress;
        SIZE_T Remaining = VirtualAddresses[EntryIndex].NumberOfBytes;

        if (!Address || !Remaining || Address + Remaining < Address)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }

        while (Remaining)
        {
            UCHAR Byte;
            SIZE_T Step = SystemInfo.dwPageSize - (Address & (SystemInfo.dwPageSize - 1));
            NTSTATUS Status = NtReadVirtualMemory(hProcess, (PVOID)Address, &Byte, sizeof(Byte), NULL);

            if (!NT_SUCCESS(Status))
            {
                BaseSetLastNTError(Status);
                return FALSE;
            }

            if (Step > Remaining)
                Step = Remaining;
            Address += Step;
            Remaining -= Step;
        }
    }

    return TRUE;
}
