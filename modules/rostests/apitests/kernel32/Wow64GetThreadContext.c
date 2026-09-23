/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Wow64GetThreadContext on a 32-bit x86 child process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(Wow64GetThreadContext)
{
#ifdef _WIN64
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 2];
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    WOW64_CONTEXT Context;
    BOOL Wow64Process = FALSE, Success = FALSE;
    DWORD Error = 0;
    UINT Length, Attempt;

    Length = GetSystemWow64DirectoryW(Path, ARRAYSIZE(Path));
    if (!Length || Length + ARRAYSIZE(L"\\notepad.exe") > ARRAYSIZE(Path))
    {
        skip("No SysWOW64 directory (%lu)\n", GetLastError());
        return;
    }
    wcscat(Path, L"\\notepad.exe");
    if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
    {
        skip("%ls not found\n", Path);
        return;
    }

    CommandLine[0] = L'"';
    wcscpy(CommandLine + 1, Path);
    wcscat(CommandLine, L"\"");
    if (!CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcessW failed with %lu\n", GetLastError());
        return;
    }

    ok(IsWow64Process(ProcessInfo.hProcess, &Wow64Process), "IsWow64Process failed with %lu\n", GetLastError());
    ok(Wow64Process, "Child is not a WoW64 process\n");

    for (Attempt = 0; Attempt < 50; Attempt++)
    {
        ok(SuspendThread(ProcessInfo.hThread) != (DWORD)-1, "SuspendThread failed with %lu\n", GetLastError());
        ZeroMemory(&Context, sizeof(Context));
        Context.ContextFlags = WOW64_CONTEXT_CONTROL | WOW64_CONTEXT_INTEGER;
        Success = Wow64GetThreadContext(ProcessInfo.hThread, &Context);
        Error = GetLastError();
        ResumeThread(ProcessInfo.hThread);
        if (Success)
            break;
        Sleep(200);
    }

    ok(Success, "Wow64GetThreadContext failed with %lu\n", Error);
    ok(Context.Eip != 0, "Eip is 0\n");
    ok(Context.Esp != 0, "Esp is 0\n");
    ok((Context.ContextFlags & WOW64_CONTEXT_CONTROL) == WOW64_CONTEXT_CONTROL,
       "ContextFlags is 0x%lx\n", Context.ContextFlags);

    TerminateProcess(ProcessInfo.hProcess, 0);
    WaitForSingleObject(ProcessInfo.hProcess, 5000);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
#else
    skip("Wow64GetThreadContext needs a 64-bit caller\n");
#endif
}
