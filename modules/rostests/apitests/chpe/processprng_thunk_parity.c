/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64-on-ARM64 ProcessPrng export-thunk parity test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *PPROCESS_PRNG)(PBYTE Data, SIZE_T Size);
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

static BOOL
buffer_changed(const BYTE *Buffer, SIZE_T Size, BYTE InitialValue)
{
    SIZE_T Index;

    for (Index = 0; Index < Size; ++Index)
    {
        if (Buffer[Index] != InitialValue)
            return TRUE;
    }
    return FALSE;
}

static BOOL
buffer_nonzero(const BYTE *Buffer, SIZE_T Size)
{
    SIZE_T Index;

    for (Index = 0; Index < Size; ++Index)
    {
        if (Buffer[Index])
            return TRUE;
    }
    return FALSE;
}

int
main(void)
{
    HMODULE Module, NtDll;
    PPROCESS_PRNG ProcessPrngDynamic;
    PRTL_IS_EC_CODE RtlIsEcCode;
    BYTE First[64], Second[64];
    BOOL FirstStatus, SecondStatus, ZeroStatus;
    BOOL Changed, Nonzero, Distinct;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_PROCESSPRNG_TEST_BEGIN\n");

    Module = LoadLibraryW(L"bcryptprimitives.dll");
    ProcessPrngDynamic = Module ? (PPROCESS_PRNG)GetProcAddress(Module, "ProcessPrng") : NULL;
    NtDll = GetModuleHandleW(L"ntdll.dll");
    RtlIsEcCode = NtDll ? (PRTL_IS_EC_CODE)GetProcAddress(NtDll, "RtlIsEcCode") : NULL;
    if (!ProcessPrngDynamic)
    {
        printf("FAIL GetProcAddress ProcessPrng error=%lu\n", GetLastError());
        if (Module) FreeLibrary(Module);
        return 1;
    }

    FillMemory(First, sizeof(First), 0xA5);
    FillMemory(Second, sizeof(Second), 0xA5);
    FirstStatus = ProcessPrngDynamic(First, sizeof(First));
    SecondStatus = ProcessPrngDynamic(Second, sizeof(Second));
    ZeroStatus = ProcessPrngDynamic(NULL, 0);
    Changed = buffer_changed(First, sizeof(First), 0xA5) && buffer_changed(Second, sizeof(Second), 0xA5);
    Nonzero = buffer_nonzero(First, sizeof(First)) && buffer_nonzero(Second, sizeof(Second));
    Distinct = memcmp(First, Second, sizeof(First)) != 0;

    printf("PROCESSPRNG export=1 ec=%d first=%d second=%d zero=%d changed=%d nonzero=%d distinct=%d\n", RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)ProcessPrngDynamic) : -1, FirstStatus, SecondStatus, ZeroStatus, Changed, Nonzero, Distinct);

    FreeLibrary(Module);
    if (!FirstStatus || !SecondStatus || !ZeroStatus || !Changed || !Nonzero || !Distinct)
    {
        printf("CHPE_PROCESSPRNG_TEST_FAIL\n");
        return 2;
    }

    printf("CHPE_PROCESSPRNG_TEST_PASS\n");
    return 0;
}
