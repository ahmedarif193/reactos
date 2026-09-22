/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests standard handle inheritance by child processes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define CHILD_OUTPUT "StdHandleInheritance child output\r\n"

static
int
RunChild(VOID)
{
    HANDLE Output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD Written = 0;

    if (Output == NULL || Output == INVALID_HANDLE_VALUE)
        return 2;

    if (!WriteFile(Output, CHILD_OUTPUT, sizeof(CHILD_OUTPUT) - 1, &Written, NULL) ||
        Written != sizeof(CHILD_OUTPUT) - 1)
    {
        return 3;
    }

    return 0;
}

static
VOID
TestInheritance(
    _In_ PCWSTR Application,
    _In_ BOOL UseHandleList)
{
    SECURITY_ATTRIBUTES Security = { sizeof(Security), NULL, TRUE };
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes = NULL;
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOEXW Startup;
    WCHAR CommandLine[MAX_PATH + 64];
    HANDLE Read = NULL, Write = NULL, Previous;
    CHAR Buffer[128];
    DWORD Received = 0, ExitCode = MAXULONG;
    SIZE_T Size = 0;
    BOOL Success;

    Success = CreatePipe(&Read, &Write, &Security, 0);
    ok(Success, "[%d] CreatePipe failed with %lu\n", UseHandleList, GetLastError());
    if (!Success)
        return;

    SetHandleInformation(Read, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&Startup, sizeof(Startup));
    Startup.StartupInfo.cb = UseHandleList ? sizeof(Startup) : sizeof(Startup.StartupInfo);

    if (UseHandleList)
    {
        InitializeProcThreadAttributeList(NULL, 1, 0, &Size);
        Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
        Success = Attributes && InitializeProcThreadAttributeList(Attributes, 1, 0, &Size);
        ok(Success, "InitializeProcThreadAttributeList failed with %lu\n", GetLastError());
        if (Success)
        {
            Success = UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &Write,
                                                sizeof(Write), NULL, NULL);
            ok(Success, "UpdateProcThreadAttribute failed with %lu\n", GetLastError());
        }
        if (!Success)
            goto Cleanup;

        Startup.lpAttributeList = Attributes;
    }

    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" StdHandleInheritance child", Application);

    Previous = GetStdHandle(STD_OUTPUT_HANDLE);
    SetStdHandle(STD_OUTPUT_HANDLE, Write);
    Success = CreateProcessW(Application, CommandLine, NULL, NULL, TRUE,
                             UseHandleList ? EXTENDED_STARTUPINFO_PRESENT : 0, NULL, NULL,
                             &Startup.StartupInfo, &ProcessInfo);
    SetStdHandle(STD_OUTPUT_HANDLE, Previous);
    ok(Success, "[%d] CreateProcessW failed with %lu\n", UseHandleList, GetLastError());

    CloseHandle(Write);
    Write = NULL;
    if (!Success)
        goto Cleanup;

    ok_eq_ulong(WaitForSingleObject(ProcessInfo.hProcess, 60000), WAIT_OBJECT_0);
    ok(GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode), "GetExitCodeProcess failed with %lu\n", GetLastError());
    ok(ExitCode == 0, "[%d] child exit code %lu\n", UseHandleList, ExitCode);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);

    Success = ReadFile(Read, Buffer, sizeof(Buffer) - 1, &Received, NULL);
    ok(Success, "[%d] ReadFile failed with %lu\n", UseHandleList, GetLastError());
    Buffer[Success ? Received : 0] = ANSI_NULL;
    ok(!strcmp(Buffer, CHILD_OUTPUT), "[%d] child wrote '%s'\n", UseHandleList, Buffer);

Cleanup:
    if (Attributes)
    {
        DeleteProcThreadAttributeList(Attributes);
        HeapFree(GetProcessHeap(), 0, Attributes);
    }
    if (Write)
        CloseHandle(Write);
    CloseHandle(Read);
}

START_TEST(StdHandleInheritance)
{
    WCHAR Application[MAX_PATH];
    char **Arguments;
    int ArgumentCount;

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 3 && !strcmp(Arguments[2], "child"))
        TerminateProcess(GetCurrentProcess(), RunChild());

    if (!GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)))
    {
        ok(0, "GetModuleFileNameW failed with %lu\n", GetLastError());
        return;
    }

    TestInheritance(Application, FALSE);
    TestInheritance(Application, TRUE);
}
