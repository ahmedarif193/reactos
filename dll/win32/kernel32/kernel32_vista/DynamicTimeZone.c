/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Dynamic time zone information
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "k32_vista.h"
#include <ndk/cmfuncs.h>
#include <ntstrsafe.h>

#ifndef REG_SZ
#define REG_SZ     1
#define REG_BINARY 3
#define REG_DWORD  4
#endif

typedef struct _K32_REG_TZI_FORMAT
{
    LONG Bias;
    LONG StandardBias;
    LONG DaylightBias;
    SYSTEMTIME StandardDate;
    SYSTEMTIME DaylightDate;
} K32_REG_TZI_FORMAT, *PK32_REG_TZI_FORMAT;

static
NTSTATUS
K32QueryValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _In_ ULONG Type,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    UCHAR ValueBuffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 512];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)ValueBuffer;
    UNICODE_STRING ValueName;
    ULONG ResultLength;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Information, sizeof(ValueBuffer), &ResultLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information->Type != Type || Information->DataLength > Size)
        return STATUS_OBJECT_TYPE_MISMATCH;

    RtlZeroMemory(Buffer, Size);
    RtlCopyMemory(Buffer, Information->Data, Information->DataLength);
    return STATUS_SUCCESS;
}

static
NTSTATUS
K32OpenTimeZoneKey(
    _In_ PCWSTR KeyName,
    _Out_ PHANDLE Key)
{
    WCHAR KeyPath[512];
    UNICODE_STRING KeyPathString;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    Status = RtlStringCchPrintfW(KeyPath, RTL_NUMBER_OF(KeyPath), L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\%s", KeyName);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyPathString, KeyPath);
    InitializeObjectAttributes(&ObjectAttributes, &KeyPathString, OBJ_CASE_INSENSITIVE, NULL, NULL);
    return NtOpenKey(Key, KEY_QUERY_VALUE, &ObjectAttributes);
}

static
VOID
K32QueryDynamicSettings(
    _Out_writes_z_(Length) LPWSTR KeyName,
    _In_ DWORD Length,
    _Out_ PBOOLEAN Disabled)
{
    UNICODE_STRING KeyPath = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key;
    ULONG Value = 0;

    KeyName[0] = UNICODE_NULL;
    *Disabled = FALSE;
    InitializeObjectAttributes(&ObjectAttributes, &KeyPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(NtOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes)))
        return;

    (VOID)K32QueryValue(Key, L"TimeZoneKeyName", REG_SZ, KeyName, Length * sizeof(WCHAR));
    if (NT_SUCCESS(K32QueryValue(Key, L"DynamicDaylightTimeDisabled", REG_DWORD, &Value, sizeof(Value))))
        *Disabled = Value != 0;

    NtClose(Key);
}

DWORD
WINAPI
GetDynamicTimeZoneInformation(
    _Out_ PDYNAMIC_TIME_ZONE_INFORMATION pTimeZoneInformation)
{
    TIME_ZONE_INFORMATION TimeZoneInformation;
    DWORD Result;

    if (!pTimeZoneInformation)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return TIME_ZONE_ID_INVALID;
    }

    Result = GetTimeZoneInformation(&TimeZoneInformation);
    if (Result == TIME_ZONE_ID_INVALID)
        return Result;

    RtlZeroMemory(pTimeZoneInformation, sizeof(*pTimeZoneInformation));
    pTimeZoneInformation->Bias = TimeZoneInformation.Bias;
    RtlCopyMemory(pTimeZoneInformation->StandardName, TimeZoneInformation.StandardName, sizeof(TimeZoneInformation.StandardName));
    pTimeZoneInformation->StandardDate = TimeZoneInformation.StandardDate;
    pTimeZoneInformation->StandardBias = TimeZoneInformation.StandardBias;
    RtlCopyMemory(pTimeZoneInformation->DaylightName, TimeZoneInformation.DaylightName, sizeof(TimeZoneInformation.DaylightName));
    pTimeZoneInformation->DaylightDate = TimeZoneInformation.DaylightDate;
    pTimeZoneInformation->DaylightBias = TimeZoneInformation.DaylightBias;

    K32QueryDynamicSettings(pTimeZoneInformation->TimeZoneKeyName, RTL_NUMBER_OF(pTimeZoneInformation->TimeZoneKeyName), &pTimeZoneInformation->DynamicDaylightTimeDisabled);
    if (pTimeZoneInformation->TimeZoneKeyName[0] == UNICODE_NULL)
    {
        RtlCopyMemory(pTimeZoneInformation->TimeZoneKeyName, TimeZoneInformation.StandardName, sizeof(TimeZoneInformation.StandardName));
    }

    return Result;
}

BOOL
WINAPI
GetTimeZoneInformationForYear(
    _In_ USHORT wYear,
    _In_opt_ PDYNAMIC_TIME_ZONE_INFORMATION pdtzi,
    _Out_ LPTIME_ZONE_INFORMATION ptzi)
{
    DYNAMIC_TIME_ZONE_INFORMATION DynamicTimeZoneInformation;
    K32_REG_TZI_FORMAT Data;
    WCHAR Year[16];
    HANDLE Key;
    HANDLE DynamicKey = NULL;
    UNICODE_STRING DynamicName = RTL_CONSTANT_STRING(L"Dynamic DST");
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    if (!ptzi)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!pdtzi)
    {
        if (GetDynamicTimeZoneInformation(&DynamicTimeZoneInformation) == TIME_ZONE_ID_INVALID)
            return FALSE;
        pdtzi = &DynamicTimeZoneInformation;
    }

    Status = K32OpenTimeZoneKey(pdtzi->TimeZoneKeyName, &Key);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    RtlZeroMemory(ptzi, sizeof(*ptzi));
    if (!NT_SUCCESS(K32QueryValue(Key, L"Std", REG_SZ, ptzi->StandardName, sizeof(ptzi->StandardName))))
        RtlCopyMemory(ptzi->StandardName, pdtzi->StandardName, sizeof(ptzi->StandardName));
    if (!NT_SUCCESS(K32QueryValue(Key, L"Dlt", REG_SZ, ptzi->DaylightName, sizeof(ptzi->DaylightName))))
        RtlCopyMemory(ptzi->DaylightName, pdtzi->DaylightName, sizeof(ptzi->DaylightName));

    Status = STATUS_OBJECT_NAME_NOT_FOUND;
    if (!pdtzi->DynamicDaylightTimeDisabled)
    {
        InitializeObjectAttributes(&ObjectAttributes, &DynamicName, OBJ_CASE_INSENSITIVE, Key, NULL);
        if (NT_SUCCESS(NtOpenKey(&DynamicKey, KEY_QUERY_VALUE, &ObjectAttributes)))
        {
            if (NT_SUCCESS(RtlStringCchPrintfW(Year, RTL_NUMBER_OF(Year), L"%u", wYear)))
                Status = K32QueryValue(DynamicKey, Year, REG_BINARY, &Data, sizeof(Data));
            NtClose(DynamicKey);
        }
    }

    if (!NT_SUCCESS(Status))
        Status = K32QueryValue(Key, L"TZI", REG_BINARY, &Data, sizeof(Data));
    NtClose(Key);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    ptzi->Bias = Data.Bias;
    ptzi->StandardBias = Data.StandardBias;
    ptzi->DaylightBias = Data.DaylightBias;
    ptzi->StandardDate = Data.StandardDate;
    ptzi->DaylightDate = Data.DaylightDate;
    return TRUE;
}

BOOL
WINAPI
SetDynamicTimeZoneInformation(
    _In_ CONST DYNAMIC_TIME_ZONE_INFORMATION *lpTimeZoneInformation)
{
    TIME_ZONE_INFORMATION TimeZoneInformation;
    HANDLE Key;
    NTSTATUS Status;
    ULONG Disabled;
    SIZE_T KeyNameLength;

    if (!lpTimeZoneInformation)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!lpTimeZoneInformation->TimeZoneKeyName[0])
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Status = K32OpenTimeZoneKey(lpTimeZoneInformation->TimeZoneKeyName, &Key);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    NtClose(Key);

    RtlZeroMemory(&TimeZoneInformation, sizeof(TimeZoneInformation));
    TimeZoneInformation.Bias = lpTimeZoneInformation->Bias;
    RtlCopyMemory(TimeZoneInformation.StandardName, lpTimeZoneInformation->StandardName, sizeof(TimeZoneInformation.StandardName));
    TimeZoneInformation.StandardDate = lpTimeZoneInformation->StandardDate;
    TimeZoneInformation.StandardBias = lpTimeZoneInformation->StandardBias;
    RtlCopyMemory(TimeZoneInformation.DaylightName, lpTimeZoneInformation->DaylightName, sizeof(TimeZoneInformation.DaylightName));
    TimeZoneInformation.DaylightDate = lpTimeZoneInformation->DaylightDate;
    TimeZoneInformation.DaylightBias = lpTimeZoneInformation->DaylightBias;

    if (!SetTimeZoneInformation(&TimeZoneInformation))
        return FALSE;

    KeyNameLength = (wcslen(lpTimeZoneInformation->TimeZoneKeyName) + 1) * sizeof(WCHAR);
    Status = RtlWriteRegistryValue(RTL_REGISTRY_CONTROL, L"TimeZoneInformation", L"TimeZoneKeyName", REG_SZ, (PVOID)lpTimeZoneInformation->TimeZoneKeyName, (ULONG)KeyNameLength);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    Disabled = lpTimeZoneInformation->DynamicDaylightTimeDisabled != FALSE;
    Status = RtlWriteRegistryValue(RTL_REGISTRY_CONTROL, L"TimeZoneInformation", L"DynamicDaylightTimeDisabled", REG_DWORD, &Disabled, sizeof(Disabled));
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}
