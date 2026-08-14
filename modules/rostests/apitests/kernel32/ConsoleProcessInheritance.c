/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Exercise console process creation against control-event delivery
 */

#include "precomp.h"

#define STRESS_ITERATIONS 64
#define WORKER_TIMEOUT_MS 90000

typedef struct _CTRL_EVENT_CONTEXT
{
    HANDLE StopEvent;
    volatile LONG SuccessCount;
} CTRL_EVENT_CONTEXT, *PCTRL_EVENT_CONTEXT;

static volatile LONG CtrlHandlerCount;

static
BOOL
WINAPI
CtrlHandler(DWORD CtrlType)
{
    if (CtrlType == CTRL_BREAK_EVENT)
    {
        InterlockedIncrement(&CtrlHandlerCount);
        return TRUE;
    }
    return FALSE;
}

static
DWORD
WINAPI
CtrlEventThread(PVOID Parameter)
{
    PCTRL_EVENT_CONTEXT Context = Parameter;

    while (WaitForSingleObject(Context->StopEvent, 0) == WAIT_TIMEOUT)
    {
        if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetCurrentProcessId())) InterlockedIncrement(&Context->SuccessCount);
        Sleep(1);
    }

    return 0;
}

static
BOOL
PrepareConsoleHandles(VOID)
{
    SECURITY_ATTRIBUTES SecurityAttributes = { sizeof(SecurityAttributes), NULL, TRUE };
    HANDLE Input;
    HANDLE Output;

    Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &SecurityAttributes, OPEN_EXISTING, 0, NULL);
    if (Input == INVALID_HANDLE_VALUE) return FALSE;

    Output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &SecurityAttributes, OPEN_EXISTING, 0, NULL);
    if (Output == INVALID_HANDLE_VALUE)
    {
        CloseHandle(Input);
        return FALSE;
    }

    if (!SetStdHandle(STD_INPUT_HANDLE, Input) || !SetStdHandle(STD_OUTPUT_HANDLE, Output) || !SetStdHandle(STD_ERROR_HANDLE, Output))
    {
        CloseHandle(Output);
        CloseHandle(Input);
        return FALSE;
    }

    return TRUE;
}

static
BOOL
RunWorker(VOID)
{
    CTRL_EVENT_CONTEXT Context = {0};
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo;
    WCHAR Application[MAX_PATH];
    WCHAR CommandLine[MAX_PATH * 2];
    HANDLE Thread = NULL;
    DWORD ExitCode;
    DWORD WaitResult;
    DWORD Index;
    BOOL Success = FALSE;

    if (!PrepareConsoleHandles() && (!AllocConsole() || !PrepareConsoleHandles())) return FALSE;
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) return FALSE;

    Context.StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Context.StopEvent) goto Cleanup;

    Thread = CreateThread(NULL, 0, CtrlEventThread, &Context, 0, NULL);
    if (!Thread) goto Cleanup;

    SetLastError(ERROR_SUCCESS);
    if (!GetModuleFileNameW(NULL, Application, _countof(Application)) || GetLastError() == ERROR_INSUFFICIENT_BUFFER) goto Cleanup;

    for (Index = 0; Index < STRESS_ITERATIONS; Index++)
    {
        RtlZeroMemory(&StartupInfo, sizeof(StartupInfo));
        RtlZeroMemory(&ProcessInfo, sizeof(ProcessInfo));
        StartupInfo.cb = sizeof(StartupInfo);
        StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        if (FAILED(StringCchPrintfW(CommandLine, _countof(CommandLine), L"\"%s\" ConsoleProcessInheritance leaf", Application))) goto Cleanup;

        if (!CreateProcessW(Application, CommandLine, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &StartupInfo, &ProcessInfo)) goto Cleanup;

        WaitResult = WaitForSingleObject(ProcessInfo.hProcess, 10000);
        if (WaitResult != WAIT_OBJECT_0)
        {
            TerminateProcess(ProcessInfo.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(ProcessInfo.hProcess, 5000);
        }
        ExitCode = STILL_ACTIVE;
        if (!GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode)) WaitResult = WAIT_FAILED;

        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);
        if (WaitResult != WAIT_OBJECT_0 || ExitCode != 0) goto Cleanup;
    }

    Success = (Context.SuccessCount > 0 && CtrlHandlerCount > 0);

Cleanup:
    if (Context.StopEvent) SetEvent(Context.StopEvent);
    if (Thread)
    {
        if (WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0) CloseHandle(Thread);
        else return FALSE;
    }
    if (Context.StopEvent) CloseHandle(Context.StopEvent);
    SetConsoleCtrlHandler(CtrlHandler, FALSE);
    return Success;
}

static
BOOL
ValidateInheritedConsoleHandles(VOID)
{
    DWORD Mode;

    if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &Mode)) return FALSE;
    if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &Mode)) return FALSE;
    return TRUE;
}

START_TEST(ConsoleProcessInheritance)
{
    PROCESS_INFORMATION ProcessInfo = {0};
    STARTUPINFOW StartupInfo = {0};
    WCHAR Application[MAX_PATH];
    WCHAR CommandLine[MAX_PATH * 2];
    DWORD ExitCode = STILL_ACTIVE;
    DWORD WaitResult;
    int ArgumentCount;
    char **Arguments;
    BOOL Result;

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 3 && !strcmp(Arguments[2], "leaf")) ExitProcess(ValidateInheritedConsoleHandles() ? 0 : 2);
    if (ArgumentCount >= 3 && !strcmp(Arguments[2], "worker")) ExitProcess(RunWorker() ? 0 : 1);

    StartupInfo.cb = sizeof(StartupInfo);
    SetLastError(ERROR_SUCCESS);
    Result = GetModuleFileNameW(NULL, Application, _countof(Application)) != 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER;
    ok(Result, "GetModuleFileNameW failed with %lu\n", GetLastError());
    if (!Result) return;

    Result = SUCCEEDED(StringCchPrintfW(CommandLine, _countof(CommandLine), L"\"%s\" ConsoleProcessInheritance worker", Application));
    ok(Result, "Failed to format the worker command line\n");
    if (!Result) return;

    Result = CreateProcessW(Application, CommandLine, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &StartupInfo, &ProcessInfo);
    ok(Result, "CreateProcessW failed with %lu\n", GetLastError());
    if (!Result) return;

    WaitResult = WaitForSingleObject(ProcessInfo.hProcess, WORKER_TIMEOUT_MS);
    ok(WaitResult == WAIT_OBJECT_0, "Console process/control-event stress wait returned %#lx\n", WaitResult);
    if (WaitResult != WAIT_OBJECT_0)
    {
        TerminateProcess(ProcessInfo.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(ProcessInfo.hProcess, 5000);
    }

    Result = GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    ok(Result, "GetExitCodeProcess failed with %lu\n", GetLastError());
    if (Result && WaitResult == WAIT_OBJECT_0) ok(ExitCode == 0, "Worker exited with %#lx\n", ExitCode);

    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}
