/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64-on-ARM64 WerGetFlags and WerSetFlags parity test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <werapi.h>
#include <stdarg.h>
#include <stdio.h>

typedef HRESULT (WINAPI *PWER_GET_FLAGS)(HANDLE Process, PDWORD Flags);
typedef HRESULT (WINAPI *PWER_SET_FLAGS)(DWORD Flags);
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
    HMODULE Kernel32, NtDll;
    PWER_GET_FLAGS WerGetFlagsDynamic;
    PWER_SET_FLAGS WerSetFlagsDynamic;
    PRTL_IS_EC_CODE RtlIsEcCode;
    DWORD InitialFlags = 0xDEADBEEF, SetFlags = 0xDEADBEEF, ResetFlags = 0xDEADBEEF;
    DWORD InvalidSetFlags = 0xDEADBEEF, NullHandleFlags = 0xDEADBEEF, InvalidHandleFlags = 0xDEADBEEF;
    HRESULT InitialStatus, SetStatus, GetSetStatus, InvalidSetStatus, GetInvalidSetStatus;
    HRESULT ResetStatus, GetResetStatus, NullHandleStatus, InvalidHandleStatus;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_WERFLAGS_TEST_BEGIN\n");

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    WerGetFlagsDynamic = Kernel32 ? (PWER_GET_FLAGS)GetProcAddress(Kernel32, "WerGetFlags") : NULL;
    WerSetFlagsDynamic = Kernel32 ? (PWER_SET_FLAGS)GetProcAddress(Kernel32, "WerSetFlags") : NULL;
    NtDll = GetModuleHandleW(L"ntdll.dll");
    RtlIsEcCode = NtDll ? (PRTL_IS_EC_CODE)GetProcAddress(NtDll, "RtlIsEcCode") : NULL;
    if (!WerGetFlagsDynamic || !WerSetFlagsDynamic)
    {
        printf("FAIL GetProcAddress get=%d set=%d error=%lu\n", WerGetFlagsDynamic != NULL, WerSetFlagsDynamic != NULL, GetLastError());
        return 1;
    }

    InitialStatus = WerGetFlagsDynamic(GetCurrentProcess(), &InitialFlags);
    SetStatus = WerSetFlagsDynamic(WER_FAULT_REPORTING_FLAG_NOHEAP | WER_FAULT_REPORTING_NO_UI);
    GetSetStatus = WerGetFlagsDynamic(GetCurrentProcess(), &SetFlags);
    InvalidSetStatus = WerSetFlagsDynamic(0x80000000);
    GetInvalidSetStatus = WerGetFlagsDynamic(GetCurrentProcess(), &InvalidSetFlags);
    ResetStatus = WerSetFlagsDynamic(0);
    GetResetStatus = WerGetFlagsDynamic(GetCurrentProcess(), &ResetFlags);
    NullHandleStatus = WerGetFlagsDynamic(NULL, &NullHandleFlags);
    InvalidHandleStatus = WerGetFlagsDynamic((HANDLE)(ULONG_PTR)0x1234, &InvalidHandleFlags);

    printf("WERFLAGS export=1 get_ec=%d set_ec=%d initial_hr=0x%08lx initial=0x%08lx set_hr=0x%08lx get_set_hr=0x%08lx set=0x%08lx invalid_set_hr=0x%08lx get_invalid_hr=0x%08lx after_invalid=0x%08lx reset_hr=0x%08lx get_reset_hr=0x%08lx reset=0x%08lx null_hr=0x%08lx null=0x%08lx invalid_handle_hr=0x%08lx invalid_handle=0x%08lx\n", RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)WerGetFlagsDynamic) : -1, RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)WerSetFlagsDynamic) : -1, InitialStatus, InitialFlags, SetStatus, GetSetStatus, SetFlags, InvalidSetStatus, GetInvalidSetStatus, InvalidSetFlags, ResetStatus, GetResetStatus, ResetFlags, NullHandleStatus, NullHandleFlags, InvalidHandleStatus, InvalidHandleFlags);

    if (InitialStatus != HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || InitialFlags != 0xDEADBEEF ||
        SetStatus != S_OK || GetSetStatus != S_OK || SetFlags != (WER_FAULT_REPORTING_FLAG_NOHEAP | WER_FAULT_REPORTING_NO_UI) ||
        InvalidSetStatus != S_OK || GetInvalidSetStatus != S_OK || InvalidSetFlags != 0x80000000 ||
        ResetStatus != S_OK || GetResetStatus != S_OK || ResetFlags != 0 ||
        NullHandleStatus != E_INVALIDARG || NullHandleFlags != 0xDEADBEEF ||
        InvalidHandleStatus != HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE) || InvalidHandleFlags != 0xDEADBEEF)
    {
        printf("CHPE_WERFLAGS_TEST_FAIL\n");
        return 2;
    }

    printf("CHPE_WERFLAGS_TEST_PASS\n");
    return 0;
}
