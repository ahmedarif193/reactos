/*
 * Kernel synchronization objects
 *
 * Copyright 1998 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stdio.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winternl.h>
#include <ddk/wdm.h>

#include "wine/kernelbase.h"
#include "wine/exception.h"

static NTSTATUS
BaseGetNamedObjectDirectory(
    _Out_ HANDLE *Directory)
{
    static HANDLE BaseDirectory;
    WCHAR Buffer[64];
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!BaseDirectory)
    {
        HANDLE NewDirectory;

        swprintf(Buffer, ARRAY_SIZE(Buffer), L"\\Sessions\\%u\\BaseNamedObjects", NtCurrentTeb()->Peb->SessionId);
        RtlInitUnicodeString(&Name, Buffer);
        InitializeObjectAttributes(&ObjectAttributes, &Name, 0, 0, NULL);
        Status = NtOpenDirectoryObject(&NewDirectory, DIRECTORY_CREATE_OBJECT | DIRECTORY_TRAVERSE, &ObjectAttributes);
        if (!Status && InterlockedCompareExchangePointer(&BaseDirectory, NewDirectory, 0) != 0) CloseHandle(NewDirectory);
    }
    *Directory = BaseDirectory;
    return Status;
}

static void
GetCreateObjectAttributes(
    _Out_ OBJECT_ATTRIBUTES *ObjectAttributes,
    _Out_ UNICODE_STRING *ObjectName,
    _In_opt_ SECURITY_ATTRIBUTES *SecurityAttributes,
    _In_opt_ const WCHAR *Name)
{
    ObjectAttributes->Length = sizeof(*ObjectAttributes);
    ObjectAttributes->RootDirectory = 0;
    ObjectAttributes->ObjectName = NULL;
    ObjectAttributes->Attributes = OBJ_OPENIF | ((SecurityAttributes && SecurityAttributes->bInheritHandle) ? OBJ_INHERIT : 0);
    ObjectAttributes->SecurityDescriptor = SecurityAttributes ? SecurityAttributes->lpSecurityDescriptor : NULL;
    ObjectAttributes->SecurityQualityOfService = NULL;
    if (Name)
    {
        RtlInitUnicodeString(ObjectName, Name);
        ObjectAttributes->ObjectName = ObjectName;
        BaseGetNamedObjectDirectory(&ObjectAttributes->RootDirectory);
    }
}

HANDLE
WINAPI
CreateEventExA(
    _In_opt_ SECURITY_ATTRIBUTES *SecurityAttributes,
    _In_opt_ LPCSTR Name,
    _In_ DWORD Flags,
    _In_ DWORD DesiredAccess)
{
    WCHAR Buffer[MAX_PATH];

    if (!Name) return CreateEventExW(SecurityAttributes, NULL, Flags, DesiredAccess);
    if (!MultiByteToWideChar(CP_ACP, 0, Name, -1, Buffer, MAX_PATH))
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    return CreateEventExW(SecurityAttributes, Buffer, Flags, DesiredAccess);
}

HANDLE
WINAPI
CreateEventExW(
    _In_opt_ SECURITY_ATTRIBUTES *SecurityAttributes,
    _In_opt_ LPCWSTR Name,
    _In_ DWORD Flags,
    _In_ DWORD DesiredAccess)
{
    HANDLE Event = NULL;
    UNICODE_STRING ObjectName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    __TRY
    {
        GetCreateObjectAttributes(&ObjectAttributes, &ObjectName, SecurityAttributes, Name);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    __ENDTRY

    Status = NtCreateEvent(&Event, DesiredAccess, &ObjectAttributes, (Flags & CREATE_EVENT_MANUAL_RESET) ? NotificationEvent : SynchronizationEvent, (Flags & CREATE_EVENT_INITIAL_SET) != 0);
    if (Status == STATUS_OBJECT_NAME_EXISTS) SetLastError(ERROR_ALREADY_EXISTS);
    else SetLastError(RtlNtStatusToDosError(Status));
    return Event;
}

HANDLE
WINAPI
CreateMutexExA(
    _In_opt_ SECURITY_ATTRIBUTES *SecurityAttributes,
    _In_opt_ LPCSTR Name,
    _In_ DWORD Flags,
    _In_ DWORD DesiredAccess)
{
    ANSI_STRING AnsiName;
    NTSTATUS Status;

    if (!Name) return CreateMutexExW(SecurityAttributes, NULL, Flags, DesiredAccess);
    RtlInitAnsiString(&AnsiName, Name);
    Status = RtlAnsiStringToUnicodeString(&NtCurrentTeb()->StaticUnicodeString, &AnsiName, FALSE);
    if (Status != STATUS_SUCCESS)
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    return CreateMutexExW(SecurityAttributes, NtCurrentTeb()->StaticUnicodeString.Buffer, Flags, DesiredAccess);
}

HANDLE
WINAPI
CreateMutexExW(
    _In_opt_ SECURITY_ATTRIBUTES *SecurityAttributes,
    _In_opt_ LPCWSTR Name,
    _In_ DWORD Flags,
    _In_ DWORD DesiredAccess)
{
    HANDLE Mutex = NULL;
    UNICODE_STRING ObjectName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    GetCreateObjectAttributes(&ObjectAttributes, &ObjectName, SecurityAttributes, Name);
    Status = NtCreateMutant(&Mutex, DesiredAccess, &ObjectAttributes, (Flags & CREATE_MUTEX_INITIAL_OWNER) != 0);
    if (Status == STATUS_OBJECT_NAME_EXISTS) SetLastError(ERROR_ALREADY_EXISTS);
    else SetLastError(RtlNtStatusToDosError(Status));
    return Mutex;
}
