/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64-on-ARM64 win32u loader and syscall-thunk parity test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *PNT_USER_GET_THREAD_STATE)(DWORD Routine);
typedef BOOLEAN (WINAPI *PRTL_IS_EC_CODE)(ULONG_PTR Address);
typedef BOOL (WINAPI *PGET_INPUT_STATE)(VOID);

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
    HMODULE User32, Win32u, NtDll;
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS64 NtHeader;
    PNT_USER_GET_THREAD_STATE NtUserGetThreadState;
    PRTL_IS_EC_CODE RtlIsEcCode;
    PGET_INPUT_STATE GetInputStateDynamic;
    BOOL InputPending;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_WIN32U_TEST_BEGIN\n");

    User32 = LoadLibraryW(L"user32.dll");
    Win32u = LoadLibraryW(L"win32u.dll");
    if (!User32 || !Win32u)
    {
        printf("FAIL LoadLibrary user32=%p win32u=%p error=%lu\n",
               User32, Win32u, GetLastError());
        return 1;
    }

    DosHeader = (PIMAGE_DOS_HEADER)Win32u;
    NtHeader = (PIMAGE_NT_HEADERS64)((PBYTE)Win32u + DosHeader->e_lfanew);
    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE ||
        NtHeader->Signature != IMAGE_NT_SIGNATURE ||
        NtHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        printf("FAIL invalid win32u image module=%p\n", Win32u);
        return 2;
    }

    NtUserGetThreadState = (PNT_USER_GET_THREAD_STATE)GetProcAddress(Win32u, "NtUserGetThreadState");
    GetInputStateDynamic = (PGET_INPUT_STATE)GetProcAddress(User32, "GetInputState");
    if (!NtUserGetThreadState || !GetInputStateDynamic)
    {
        printf("FAIL GetProcAddress NtUserGetThreadState=%p GetInputState=%p error=%lu\n",
               NtUserGetThreadState, GetInputStateDynamic, GetLastError());
        return 3;
    }

    NtDll = GetModuleHandleW(L"ntdll.dll");
    RtlIsEcCode = NtDll ? (PRTL_IS_EC_CODE)GetProcAddress(NtDll, "RtlIsEcCode") : NULL;
    printf("WIN32U module=%p machine=0x%04x size=0x%lx target=%p ec=%d\n",
           Win32u,
           NtHeader->FileHeader.Machine,
           NtHeader->OptionalHeader.SizeOfImage,
           NtUserGetThreadState,
           RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)NtUserGetThreadState) : -1);

    /* GetInputState exercises user32's native call into win32u without using a private API directly. */
    InputPending = GetInputStateDynamic();
    printf("CALL GetInputState=%d\n", InputPending);

    printf("CHPE_WIN32U_TEST_PASS\n");
    return 0;
}
