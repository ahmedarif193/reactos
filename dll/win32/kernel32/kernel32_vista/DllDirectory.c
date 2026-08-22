/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Process DLL search directories
 * COPYRIGHT:   Adapted from Wine dlls/ntdll/loader.c
 */

#include "k32_vista.h"

DLL_DIRECTORY_COOKIE
WINAPI
AddDllDirectory(
    _In_ PCWSTR NewDirectory)
{
    PBASE_DLL_DIRECTORY_ENTRY Entry;
    FILE_BASIC_INFORMATION Information;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING NtPath;
    RTL_PATH_TYPE PathType;
    NTSTATUS Status;
    SIZE_T Size;

    if (!NewDirectory)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    PathType = RtlDetermineDosPathNameType_U(NewDirectory);
    if (PathType != RtlPathTypeRooted && PathType != RtlPathTypeDriveAbsolute && PathType != RtlPathTypeUncAbsolute && PathType != RtlPathTypeLocalDevice)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    Status = RtlDosPathNameToNtPathName_U_WithStatus(NewDirectory, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return NULL;
    }

    Size = FIELD_OFFSET(BASE_DLL_DIRECTORY_ENTRY, NtPath) + NtPath.Length + sizeof(WCHAR);
    Entry = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
    if (!Entry)
    {
        RtlFreeUnicodeString(&NtPath);
        BaseSetLastNTError(STATUS_NO_MEMORY);
        return NULL;
    }

    RtlCopyMemory(Entry->NtPath, NtPath.Buffer, NtPath.Length);
    Entry->NtPath[NtPath.Length / sizeof(WCHAR)] = UNICODE_NULL;
    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtQueryAttributesFile(&ObjectAttributes, &Information);
    RtlFreeUnicodeString(&NtPath);

    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
        BaseSetLastNTError(Status);
        return NULL;
    }

    RtlEnterCriticalSection(&BaseDllDirectoryLock);
    InsertHeadList(&BaseDllDirectoryList, &Entry->ListEntry);
    RtlLeaveCriticalSection(&BaseDllDirectoryLock);
    return Entry;
}

BOOL
WINAPI
RemoveDllDirectory(
    _In_ DLL_DIRECTORY_COOKIE Cookie)
{
    PBASE_DLL_DIRECTORY_ENTRY Entry = Cookie;

    RtlEnterCriticalSection(&BaseDllDirectoryLock);
    RemoveEntryList(&Entry->ListEntry);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
    RtlLeaveCriticalSection(&BaseDllDirectoryLock);
    return TRUE;
}
