/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64-on-ARM64 RaiseFailFastException export-thunk parity test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define STATUS_FAIL_FAST_EXCEPTION ((DWORD)0xC0000602)

typedef VOID (WINAPI *PRAISE_FAIL_FAST_EXCEPTION)(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT ContextRecord, DWORD Flags);
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
main(int ArgumentCount, char **Arguments)
{
    HMODULE Kernel32, NtDll;
    PRAISE_FAIL_FAST_EXCEPTION RaiseFailFastExceptionDynamic;
    PRTL_IS_EC_CODE RtlIsEcCode;
    CHAR ImagePath[MAX_PATH], CommandLine[MAX_PATH + 32];
    STARTUPINFOA StartupInfo;
    PROCESS_INFORMATION ProcessInformation;
    DWORD WaitStatus, ExitCode = 0;
    BOOL Created;

    setvbuf(stdout, NULL, _IONBF, 0);
    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    RaiseFailFastExceptionDynamic = Kernel32 ? (PRAISE_FAIL_FAST_EXCEPTION)GetProcAddress(Kernel32, "RaiseFailFastException") : NULL;
    if (ArgumentCount > 1 && !strcmp(Arguments[1], "--child"))
    {
        printf("RAISEFAILFASTEXCEPTION_CHILD_BEGIN export=%d\n", RaiseFailFastExceptionDynamic != NULL);
        if (!RaiseFailFastExceptionDynamic)
            return 90;
        RaiseFailFastExceptionDynamic(NULL, NULL, FAIL_FAST_NO_HARD_ERROR_DLG);
        printf("RAISEFAILFASTEXCEPTION_CHILD_RETURNED\n");
        return 91;
    }

    printf("CHPE_RAISEFAILFASTEXCEPTION_TEST_BEGIN\n");
    NtDll = GetModuleHandleW(L"ntdll.dll");
    RtlIsEcCode = NtDll ? (PRTL_IS_EC_CODE)GetProcAddress(NtDll, "RtlIsEcCode") : NULL;
    if (!RaiseFailFastExceptionDynamic)
    {
        printf("FAIL GetProcAddress RaiseFailFastException error=%lu\n", GetLastError());
        return 1;
    }

    if (!GetModuleFileNameA(NULL, ImagePath, ARRAYSIZE(ImagePath)))
    {
        printf("FAIL GetModuleFileName error=%lu\n", GetLastError());
        return 2;
    }
    snprintf(CommandLine, sizeof(CommandLine), "\"%s\" --child", ImagePath);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    ZeroMemory(&ProcessInformation, sizeof(ProcessInformation));
    StartupInfo.cb = sizeof(StartupInfo);
    Created = CreateProcessA(NULL, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &StartupInfo, &ProcessInformation);
    if (!Created)
    {
        printf("FAIL CreateProcess error=%lu\n", GetLastError());
        return 3;
    }

    WaitStatus = WaitForSingleObject(ProcessInformation.hProcess, 10000);
    if (WaitStatus == WAIT_OBJECT_0)
        GetExitCodeProcess(ProcessInformation.hProcess, &ExitCode);
    printf("RAISEFAILFASTEXCEPTION export=1 ec=%d create=1 wait=0x%08lx exit=0x%08lx\n", RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)RaiseFailFastExceptionDynamic) : -1, WaitStatus, ExitCode);

    CloseHandle(ProcessInformation.hThread);
    CloseHandle(ProcessInformation.hProcess);
    if (WaitStatus != WAIT_OBJECT_0 || ExitCode != STATUS_FAIL_FAST_EXCEPTION)
    {
        printf("CHPE_RAISEFAILFASTEXCEPTION_TEST_FAIL\n");
        return 4;
    }

    printf("CHPE_RAISEFAILFASTEXCEPTION_TEST_PASS\n");
    return 0;
}
