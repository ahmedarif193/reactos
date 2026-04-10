/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Pure disk-enumeration parsing helpers.
 */

#include "snapshotenum.h"

#include <stdlib.h>
#include <wctype.h>

static int __cdecl
DmSnapshotEnumCompareUlong(
    _In_ const void *Left,
    _In_ const void *Right)
{
    const ULONG A = *(const ULONG *)Left;
    const ULONG B = *(const ULONG *)Right;

    if (A < B)
        return -1;
    if (A > B)
        return 1;
    return 0;
}

BOOL
DmSnapshotEnumParsePhysicalDriveNumber(
    _In_ PCWSTR Name,
    _Out_ PULONG DiskNumber)
{
    static const WCHAR Prefix[] = L"PhysicalDrive";
    PCWSTR Suffix;
    ULONG Value;

    if (Name == NULL || DiskNumber == NULL)
        return FALSE;

    if (_wcsnicmp(Name, Prefix, ARRAYSIZE(Prefix) - 1) != 0)
        return FALSE;

    Suffix = Name + ARRAYSIZE(Prefix) - 1;
    if (*Suffix == UNICODE_NULL)
        return FALSE;

    Value = 0;
    while (*Suffix != UNICODE_NULL)
    {
        ULONG Digit;

        if (!iswdigit(*Suffix))
            return FALSE;

        Digit = (ULONG)(*Suffix - L'0');
        if (Value > (((ULONG)~0UL - Digit) / 10))
            return FALSE;

        Value = (Value * 10) + Digit;
        Suffix++;
    }

    *DiskNumber = Value;
    return TRUE;
}

BOOL
DmSnapshotEnumParseDiskNumbers(
    _In_ PCWSTR DeviceList,
    _Outptr_result_buffer_maybenull_(*DiskCount) PULONG *DiskNumbers,
    _Out_ PULONG DiskCount)
{
    PCWSTR Name;
    PULONG Result;
    ULONG ResultCount;
    SIZE_T ResultCapacity;

    if (DeviceList == NULL || DiskNumbers == NULL || DiskCount == NULL)
        return FALSE;

    *DiskNumbers = NULL;
    *DiskCount = 0;

    Result = NULL;
    ResultCount = 0;
    ResultCapacity = 0;

    for (Name = DeviceList; *Name != UNICODE_NULL; Name += wcslen(Name) + 1)
    {
        ULONG DiskNumber;

        if (!DmSnapshotEnumParsePhysicalDriveNumber(Name, &DiskNumber))
            continue;

        if (ResultCount == ResultCapacity)
        {
            PULONG NewResult;
            SIZE_T NewCapacity;
            SIZE_T NewSize;

            NewCapacity = (ResultCapacity == 0) ? 8 : ResultCapacity * 2;
            NewSize = NewCapacity * sizeof(Result[0]);
            if (Result == NULL)
            {
                NewResult = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NewSize);
            }
            else
            {
                NewResult = HeapReAlloc(GetProcessHeap(),
                                        HEAP_ZERO_MEMORY,
                                        Result,
                                        NewSize);
            }

            if (NewResult == NULL)
            {
                HeapFree(GetProcessHeap(), 0, Result);
                return FALSE;
            }

            Result = NewResult;
            ResultCapacity = NewCapacity;
        }

        Result[ResultCount++] = DiskNumber;
    }

    if (ResultCount == 0)
        return TRUE;

    qsort(Result, ResultCount, sizeof(Result[0]), DmSnapshotEnumCompareUlong);

    if (ResultCount > 1)
    {
        ULONG ReadIndex;
        ULONG WriteIndex;

        WriteIndex = 1;
        for (ReadIndex = 1; ReadIndex < ResultCount; ReadIndex++)
        {
            if (Result[ReadIndex] != Result[WriteIndex - 1])
                Result[WriteIndex++] = Result[ReadIndex];
        }

        ResultCount = WriteIndex;
    }

    *DiskNumbers = Result;
    *DiskCount = ResultCount;
    return TRUE;
}
