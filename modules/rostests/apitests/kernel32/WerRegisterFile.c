/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the kernel32 WerRegisterFile/WerUnregisterFile exports return
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <werapi.h>

typedef HRESULT (WINAPI *FN_WerRegisterFile)(PCWSTR, WER_REGISTER_FILE_TYPE, DWORD);
typedef HRESULT (WINAPI *FN_WerUnregisterFile)(PCWSTR);

static FN_WerRegisterFile pWerRegisterFile;
static FN_WerUnregisterFile pWerUnregisterFile;
static HRESULT RegisterResult, UnregisterResult;

static DWORD WINAPI
WerThread(PVOID Parameter)
{
    PCWSTR Path = Parameter;

    RegisterResult = pWerRegisterFile(Path, WerRegFileTypeOther, 0);
    UnregisterResult = pWerUnregisterFile(Path);
    return 0;
}

START_TEST(WerRegisterFile)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    WCHAR Path[MAX_PATH];
    HANDLE Thread;
    DWORD Wait;

    pWerRegisterFile = (FN_WerRegisterFile)GetProcAddress(Kernel32, "WerRegisterFile");
    pWerUnregisterFile = (FN_WerUnregisterFile)GetProcAddress(Kernel32, "WerUnregisterFile");
    if (!pWerRegisterFile || !pWerUnregisterFile)
    {
        skip("WerRegisterFile/WerUnregisterFile not exported\n");
        return;
    }

    ok(GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path)) != 0, "GetModuleFileNameW failed: %lu\n", GetLastError());

    Thread = CreateThread(NULL, 0, WerThread, Path, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        return;

    Wait = WaitForSingleObject(Thread, 10000);
    ok(Wait == WAIT_OBJECT_0, "WerRegisterFile/WerUnregisterFile did not return (%lu)\n", Wait);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateThread(Thread, 0);
        CloseHandle(Thread);
        return;
    }
    CloseHandle(Thread);

    ok(RegisterResult == S_OK || RegisterResult == E_NOTIMPL, "WerRegisterFile returned 0x%lx\n", RegisterResult);
    ok(UnregisterResult == S_OK || UnregisterResult == E_NOTIMPL, "WerUnregisterFile returned 0x%lx\n", UnregisterResult);
}
