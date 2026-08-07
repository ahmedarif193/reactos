/*
 * PROJECT:     ReactOS Kernel Transaction Manager API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Transaction API entry points
 * COPYRIGHT:   Copyright 2010 Vincent Povirk for CodeWeavers
 * COPYRIGHT:   Adapted from Wine dlls/ktmw32/ktmw32_main.c
 */

#include <stdarg.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <ndk/rtlfuncs.h>
#include <ndk/obfuncs.h>

#include <wine/debug.h>

#define TRANSACTION_ALL_ACCESS 0x001F0022

WINE_DEFAULT_DEBUG_CHANNEL(ktmw32);

NTSYSAPI
NTSTATUS
NTAPI
NtCreateTransaction(
    _Out_ PHANDLE TransactionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ LPGUID Uow,
    _In_opt_ HANDLE TmHandle,
    _In_opt_ ULONG CreateOptions,
    _In_opt_ ULONG IsolationLevel,
    _In_opt_ ULONG IsolationFlags,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_opt_ PUNICODE_STRING Description);

NTSYSAPI
NTSTATUS
NTAPI
NtCommitTransaction(
    _In_ HANDLE TransactionHandle,
    _In_ BOOLEAN Wait);

NTSYSAPI
NTSTATUS
NTAPI
NtRollbackTransaction(
    _In_ HANDLE TransactionHandle,
    _In_ BOOLEAN Wait);

static BOOL SetStatus(NTSTATUS Status)
{
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }
    return TRUE;
}

HANDLE
WINAPI
CreateTransaction(
    _In_opt_ LPSECURITY_ATTRIBUTES SecurityAttributes,
    _In_opt_ LPGUID Uow,
    _In_opt_ DWORD CreateOptions,
    _In_opt_ DWORD IsolationLevel,
    _In_opt_ DWORD IsolationFlags,
    _In_opt_ DWORD Timeout,
    _In_opt_ LPWSTR Description)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DescriptionString;
    LARGE_INTEGER TimeoutValue;
    PLARGE_INTEGER TimeoutPointer = NULL;
    HANDLE Handle = NULL;
    ULONG Attributes = OBJ_CASE_INSENSITIVE;

    TRACE("(%p %p %lu %lu %lu %lu %s)\n", SecurityAttributes, Uow, CreateOptions, IsolationLevel, IsolationFlags, Timeout, debugstr_w(Description));

    if (SecurityAttributes && SecurityAttributes->bInheritHandle)
        Attributes |= OBJ_INHERIT;

    InitializeObjectAttributes(&ObjectAttributes, NULL, Attributes, NULL, SecurityAttributes ? SecurityAttributes->lpSecurityDescriptor : NULL);

    if (Description)
        RtlInitUnicodeString(&DescriptionString, Description);

    if (Timeout != INFINITE)
    {
        TimeoutValue.QuadPart = (LONGLONG)Timeout * -10000;
        TimeoutPointer = &TimeoutValue;
    }

    Status = NtCreateTransaction(&Handle, TRANSACTION_ALL_ACCESS, &ObjectAttributes, Uow, NULL, CreateOptions, IsolationLevel, IsolationFlags, TimeoutPointer, Description ? &DescriptionString : NULL);
    if (!SetStatus(Status))
        return INVALID_HANDLE_VALUE;

    return Handle;
}

BOOL
WINAPI
CommitTransaction(
    _In_ HANDLE TransactionHandle)
{
    TRACE("(%p)\n", TransactionHandle);
    return SetStatus(NtCommitTransaction(TransactionHandle, TRUE));
}

BOOL
WINAPI
RollbackTransaction(
    _In_ HANDLE TransactionHandle)
{
    TRACE("(%p)\n", TransactionHandle);
    return SetStatus(NtRollbackTransaction(TransactionHandle, TRUE));
}
