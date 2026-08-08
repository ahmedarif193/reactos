/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Application restart registration support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <k32_vista.h>
#include <ntstrsafe.h>

#ifndef RESTART_MAX_CMD_LINE
#define RESTART_MAX_CMD_LINE 1024
#endif
#ifndef RESTART_NO_CRASH
#define RESTART_NO_CRASH  0x1
#define RESTART_NO_HANG   0x2
#define RESTART_NO_PATCH  0x4
#define RESTART_NO_REBOOT 0x8
#endif

#define BASE_RESTART_SIGNATURE 0x54535252

typedef struct _BASE_RESTART_DATA
{
    ULONG Signature;
    DWORD Flags;
    DWORD Length;
    WCHAR CommandLine[RESTART_MAX_CMD_LINE + 1];
} BASE_RESTART_DATA, *PBASE_RESTART_DATA;

static RTL_SRWLOCK BaseRestartLock = RTL_SRWLOCK_INIT;
static HANDLE BaseRestartMapping = NULL;
static PBASE_RESTART_DATA BaseRestartData = NULL;

static
HRESULT
BaseGetRestartMappingName(
    _In_ DWORD ProcessId,
    _Out_writes_(Count) PWSTR Name,
    _In_ SIZE_T Count)
{
    NTSTATUS Status;

    Status = RtlStringCchPrintfW(Name, Count, L"ReactOS.ApplicationRestart.%lu", ProcessId);
    return NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
}

HRESULT
WINAPI
UnregisterApplicationRestart(VOID);

HRESULT
WINAPI
GetApplicationRestartSettings(
    _In_ HANDLE hProcess,
    _Out_writes_opt_(*pcchSize) PWSTR pwzCommandline,
    _Inout_ PDWORD pcchSize,
    _Out_opt_ PDWORD pdwFlags);

HRESULT
WINAPI
RegisterApplicationRestart(
    _In_opt_ PCWSTR pwzCommandline,
    _In_ DWORD dwFlags)
{
    WCHAR MappingName[64];
    HRESULT Result;
    SIZE_T Length = 0;

    if (dwFlags & ~(RESTART_NO_CRASH | RESTART_NO_HANG | RESTART_NO_PATCH | RESTART_NO_REBOOT))
        return E_INVALIDARG;

    if (pwzCommandline != NULL)
    {
        Length = wcslen(pwzCommandline);
        if (Length > RESTART_MAX_CMD_LINE)
            return E_INVALIDARG;
    }

    Result = BaseGetRestartMappingName(GetCurrentProcessId(), MappingName, RTL_NUMBER_OF(MappingName));
    if (FAILED(Result))
        return Result;

    RtlAcquireSRWLockExclusive(&BaseRestartLock);
    if (!BaseRestartMapping)
    {
        BaseRestartMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*BaseRestartData), MappingName);
        if (BaseRestartMapping)
            BaseRestartData = MapViewOfFile(BaseRestartMapping, FILE_MAP_WRITE, 0, 0, sizeof(*BaseRestartData));
        if (!BaseRestartData)
        {
            DWORD Error = GetLastError();

            if (BaseRestartMapping)
                CloseHandle(BaseRestartMapping);
            BaseRestartMapping = NULL;
            RtlReleaseSRWLockExclusive(&BaseRestartLock);
            return HRESULT_FROM_WIN32(Error);
        }
    }

    BaseRestartData->Signature = 0;
    BaseRestartData->Flags = dwFlags;
    BaseRestartData->Length = (DWORD)Length;
    if (Length)
        RtlCopyMemory(BaseRestartData->CommandLine, pwzCommandline, Length * sizeof(WCHAR));
    BaseRestartData->CommandLine[Length] = UNICODE_NULL;
    MemoryBarrier();
    BaseRestartData->Signature = BASE_RESTART_SIGNATURE;
    RtlReleaseSRWLockExclusive(&BaseRestartLock);

    return S_OK;
}

HRESULT
WINAPI
UnregisterApplicationRestart(VOID)
{
    RtlAcquireSRWLockExclusive(&BaseRestartLock);
    if (BaseRestartData)
    {
        BaseRestartData->Signature = 0;
        UnmapViewOfFile(BaseRestartData);
    }
    BaseRestartData = NULL;
    if (BaseRestartMapping)
        CloseHandle(BaseRestartMapping);
    BaseRestartMapping = NULL;
    RtlReleaseSRWLockExclusive(&BaseRestartLock);

    return S_OK;
}

HRESULT
WINAPI
GetApplicationRestartSettings(
    _In_ HANDLE hProcess,
    _Out_writes_opt_(*pcchSize) PWSTR pwzCommandline,
    _Inout_ PDWORD pcchSize,
    _Out_opt_ PDWORD pdwFlags)
{
    WCHAR MappingName[64];
    PBASE_RESTART_DATA Data;
    HANDLE Mapping = NULL;
    DWORD ProcessId;
    DWORD Needed;
    HRESULT hr;

    if (pcchSize == NULL)
        return E_INVALIDARG;

    ProcessId = hProcess == NtCurrentProcess() ? GetCurrentProcessId() : GetProcessId(hProcess);
    if (!ProcessId)
        return HRESULT_FROM_WIN32(GetLastError());

    hr = BaseGetRestartMappingName(ProcessId, MappingName, RTL_NUMBER_OF(MappingName));
    if (FAILED(hr))
        return hr;

    RtlAcquireSRWLockShared(&BaseRestartLock);
    if (ProcessId == GetCurrentProcessId())
    {
        Data = BaseRestartData;
    }
    else
    {
        Mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName);
        Data = Mapping ? MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, sizeof(*Data)) : NULL;
    }

    if (!Data || Data->Signature != BASE_RESTART_SIGNATURE || Data->Length > RESTART_MAX_CMD_LINE)
    {
        hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    else
    {
        MemoryBarrier();
        Needed = Data->Length + 1;
        if (pwzCommandline == NULL || *pcchSize < Needed)
        {
            *pcchSize = Needed;
            hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        else
        {
            RtlCopyMemory(pwzCommandline,
                          Data->CommandLine,
                          Needed * sizeof(WCHAR));
            *pcchSize = Needed;
            if (pdwFlags != NULL)
                *pdwFlags = Data->Flags;
            hr = S_OK;
        }
    }

    if (Mapping)
    {
        if (Data)
            UnmapViewOfFile(Data);
        CloseHandle(Mapping);
    }
    RtlReleaseSRWLockShared(&BaseRestartLock);

    return hr;
}
