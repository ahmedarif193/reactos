/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of Vista IDN helper APIs
 * COPYRIGHT:   Copyright 2025 ReactOS contributors
 */

#include "k32_vista.h"

NTSYSAPI
NTSTATUS
NTAPI
RtlIdnToAscii(
    _In_ ULONG Flags,
    _In_reads_(SourceStringLength) PCWSTR SourceString,
    _In_ LONG SourceStringLength,
    _Out_writes_opt_(*DestinationStringLength) PWSTR DestinationString,
    _Inout_ PLONG DestinationStringLength);

NTSYSAPI
NTSTATUS
NTAPI
RtlIdnToNameprepUnicode(
    _In_ ULONG Flags,
    _In_reads_(SourceStringLength) PCWSTR SourceString,
    _In_ LONG SourceStringLength,
    _Out_writes_opt_(*DestinationStringLength) PWSTR DestinationString,
    _Inout_ PLONG DestinationStringLength);

NTSYSAPI
NTSTATUS
NTAPI
RtlIdnToUnicode(
    _In_ ULONG Flags,
    _In_reads_(SourceStringLength) PCWSTR SourceString,
    _In_ LONG SourceStringLength,
    _Out_writes_opt_(*DestinationStringLength) PWSTR DestinationString,
    _Inout_ PLONG DestinationStringLength);

static BOOL
K32VistaHandleIdnStatus(
    _In_ NTSTATUS Status)
{
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    if (Status == STATUS_INVALID_PARAMETER)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

INT
WINAPI
IdnToAscii(
    _In_ DWORD dwFlags,
    _In_reads_(cchUnicodeChar) LPCWSTR lpUnicodeCharStr,
    _In_ INT cchUnicodeChar,
    _Out_writes_opt_(cchASCIIChar) LPWSTR lpASCIICharStr,
    _In_ INT cchASCIIChar)
{
    LONG DestinationLength = cchASCIIChar;
    NTSTATUS Status;

    Status = RtlIdnToAscii(dwFlags,
                           lpUnicodeCharStr,
                           cchUnicodeChar,
                           lpASCIICharStr,
                           &DestinationLength);
    if (!K32VistaHandleIdnStatus(Status))
        return 0;

    return (INT)DestinationLength;
}

INT
WINAPI
IdnToNameprepUnicode(
    _In_ DWORD dwFlags,
    _In_reads_(cchUnicodeChar) LPCWSTR lpUnicodeCharStr,
    _In_ INT cchUnicodeChar,
    _Out_writes_opt_(cchNameprepChar) LPWSTR lpNameprepCharStr,
    _In_ INT cchNameprepChar)
{
    LONG DestinationLength = cchNameprepChar;
    NTSTATUS Status;

    Status = RtlIdnToNameprepUnicode(dwFlags,
                                     lpUnicodeCharStr,
                                     cchUnicodeChar,
                                     lpNameprepCharStr,
                                     &DestinationLength);
    if (!K32VistaHandleIdnStatus(Status))
        return 0;

    return (INT)DestinationLength;
}

INT
WINAPI
IdnToUnicode(
    _In_ DWORD dwFlags,
    _In_reads_(cchUnicodeChar) LPCWSTR lpASCIICharStr,
    _In_ INT cchASCIIChar,
    _Out_writes_opt_(cchUnicodeCharStr) LPWSTR lpUnicodeCharStr,
    _In_ INT cchUnicodeCharStr)
{
    LONG DestinationLength = cchUnicodeCharStr;
    NTSTATUS Status;

    Status = RtlIdnToUnicode(dwFlags,
                             lpASCIICharStr,
                             cchASCIIChar,
                             lpUnicodeCharStr,
                             &DestinationLength);
    if (!K32VistaHandleIdnStatus(Status))
        return 0;

    return (INT)DestinationLength;
}
