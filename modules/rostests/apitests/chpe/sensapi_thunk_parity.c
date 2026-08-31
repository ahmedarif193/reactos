/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64-on-ARM64 sensapi loader and export-thunk parity test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

typedef BOOL (WINAPI *PIS_NETWORK_ALIVE)(LPDWORD Flags);
typedef BOOL (WINAPI *PIS_DESTINATION_REACHABLE_W)(LPCWSTR Destination, PVOID QocInfo);
typedef BOOLEAN (WINAPI *PRTL_IS_EC_CODE)(ULONG_PTR Address);

static int
log_result(PCSTR Format, ...)
{
    CHAR Buffer[512];
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    va_end(Arguments);

    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    fputs(Buffer, stdout);
    OutputDebugStringA(Buffer);
    return Length;
}

#define printf log_result

int
main(void)
{
    HMODULE SensApi, NtDll;
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS64 NtHeader;
    PIS_NETWORK_ALIVE IsNetworkAliveDynamic;
    PIS_DESTINATION_REACHABLE_W IsDestinationReachableWDynamic;
    PRTL_IS_EC_CODE RtlIsEcCode;
    DWORD Flags, NetworkError, DestinationError;
    BOOL NetworkAlive, DestinationReachable;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_SENSAPI_TEST_BEGIN\n");

    SensApi = LoadLibraryW(L"sensapi.dll");
    if (!SensApi)
    {
        printf("FAIL LoadLibrary sensapi error=%lu\n", GetLastError());
        return 1;
    }

    DosHeader = (PIMAGE_DOS_HEADER)SensApi;
    NtHeader = (PIMAGE_NT_HEADERS64)((PBYTE)SensApi + DosHeader->e_lfanew);
    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE ||
        NtHeader->Signature != IMAGE_NT_SIGNATURE ||
        NtHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        printf("FAIL invalid sensapi image module=%p\n", SensApi);
        return 2;
    }

    IsNetworkAliveDynamic = (PIS_NETWORK_ALIVE)GetProcAddress(SensApi, "IsNetworkAlive");
    IsDestinationReachableWDynamic = (PIS_DESTINATION_REACHABLE_W)GetProcAddress(SensApi, "IsDestinationReachableW");
    if (!IsNetworkAliveDynamic || !IsDestinationReachableWDynamic)
    {
        printf("FAIL GetProcAddress network=%p destination=%p error=%lu\n", IsNetworkAliveDynamic, IsDestinationReachableWDynamic, GetLastError());
        return 3;
    }

    NtDll = GetModuleHandleW(L"ntdll.dll");
    RtlIsEcCode = NtDll ? (PRTL_IS_EC_CODE)GetProcAddress(NtDll, "RtlIsEcCode") : NULL;
    printf("SENSAPI module=%p machine=0x%04x size=0x%lx network=%p network_ec=%d destination=%p destination_ec=%d\n", SensApi, NtHeader->FileHeader.Machine, NtHeader->OptionalHeader.SizeOfImage, IsNetworkAliveDynamic, RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)IsNetworkAliveDynamic) : -1, IsDestinationReachableWDynamic, RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)IsDestinationReachableWDynamic) : -1);

    Flags = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    NetworkAlive = IsNetworkAliveDynamic(&Flags);
    NetworkError = GetLastError();
    printf("CALL IsNetworkAlive result=%d error=%lu flags=0x%08lx\n", NetworkAlive, NetworkError, Flags);

    SetLastError(0xdeadbeef);
    DestinationReachable = IsDestinationReachableWDynamic(L"127.0.0.1", NULL);
    DestinationError = GetLastError();
    printf("CALL IsDestinationReachableW result=%d error=%lu\n", DestinationReachable, DestinationError);

    printf("CHPE_SENSAPI_TEST_PASS\n");
    return 0;
}
