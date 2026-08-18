/*
 * PROJECT:     ReactOS Raspberry Pi 5 OpenGL test harness
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Run the unmodified Mesa WGL gears demo for a bounded interval
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define WGLGEARS_RUN_MILLISECONDS 16000
#define WGLGEARS_CLOSE_MILLISECONDS 5000

typedef struct _WGLGEARS_CLOSE_CONTEXT
{
    DWORD ProcessId;
    ULONG WindowCount;
} WGLGEARS_CLOSE_CONTEXT, *PWGLGEARS_CLOSE_CONTEXT;

static VOID
RunnerPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[1024];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 1] = '\0';
    OutputDebugStringA(Buffer);
    fputs(Buffer, stdout);
    fflush(stdout);
}

static VOID
DrainChildOutput(
    _In_ HANDLE Pipe)
{
    CHAR Buffer[1024];
    DWORD Available;
    DWORD BytesRead;
    DWORD BytesToRead;

    while (PeekNamedPipe(Pipe,
                         NULL,
                         0,
                         NULL,
                         &Available,
                         NULL) &&
           Available != 0)
    {
        BytesToRead = Available;
        if (BytesToRead >= sizeof(Buffer))
            BytesToRead = sizeof(Buffer) - 1;
        if (!ReadFile(Pipe,
                      Buffer,
                      BytesToRead,
                      &BytesRead,
                      NULL) ||
            BytesRead == 0)
        {
            break;
        }

        Buffer[BytesRead] = '\0';
        OutputDebugStringA(Buffer);
        fwrite(Buffer, 1, BytesRead, stdout);
        fflush(stdout);
    }
}

static BOOL CALLBACK
CloseProcessWindow(
    _In_ HWND Window,
    _In_ LPARAM Parameter)
{
    PWGLGEARS_CLOSE_CONTEXT Context =
        (PWGLGEARS_CLOSE_CONTEXT)Parameter;
    DWORD ProcessId;

    GetWindowThreadProcessId(Window, &ProcessId);
    if (ProcessId == Context->ProcessId)
    {
        PostMessageW(Window, WM_CLOSE, 0, 0);
        Context->WindowCount++;
    }
    return TRUE;
}

int
main(VOID)
{
    CHAR ApplicationPath[MAX_PATH];
    CHAR CommandLine[(MAX_PATH * 2) + 16];
    CHAR SystemDirectory[MAX_PATH];
    SECURITY_ATTRIBUTES SecurityAttributes;
    STARTUPINFOA StartupInfo;
    PROCESS_INFORMATION ProcessInformation;
    WGLGEARS_CLOSE_CONTEXT CloseContext;
    HANDLE ReadPipe = NULL;
    HANDLE WritePipe = NULL;
    DWORD StartTick;
    DWORD CloseTick;
    DWORD ExitCode = ERROR_GEN_FAILURE;
    DWORD WaitStatus;
    UINT Length;
    BOOL Forced = FALSE;

    Length = GetSystemDirectoryA(SystemDirectory,
                                 sizeof(SystemDirectory));
    if (Length == 0 || Length >= sizeof(SystemDirectory))
    {
        RunnerPrint("RPI5_WGLGEARS_ERROR get_system_directory=%lu\n",
                    GetLastError());
        return 1;
    }

    if (_snprintf(ApplicationPath,
                  sizeof(ApplicationPath),
                  "%s\\wglgears.exe",
                  SystemDirectory) < 0 ||
        _snprintf(CommandLine,
                  sizeof(CommandLine),
                  "\"%s\" -info",
                  ApplicationPath) < 0)
    {
        RunnerPrint("RPI5_WGLGEARS_ERROR path_too_long\n");
        return 1;
    }
    ApplicationPath[sizeof(ApplicationPath) - 1] = '\0';
    CommandLine[sizeof(CommandLine) - 1] = '\0';

    ZeroMemory(&SecurityAttributes, sizeof(SecurityAttributes));
    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.bInheritHandle = TRUE;
    if (!CreatePipe(&ReadPipe,
                    &WritePipe,
                    &SecurityAttributes,
                    0) ||
        !SetHandleInformation(ReadPipe, HANDLE_FLAG_INHERIT, 0))
    {
        RunnerPrint("RPI5_WGLGEARS_ERROR create_pipe=%lu\n",
                    GetLastError());
        if (ReadPipe != NULL)
            CloseHandle(ReadPipe);
        if (WritePipe != NULL)
            CloseHandle(WritePipe);
        return 1;
    }

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    StartupInfo.hStdOutput = WritePipe;
    StartupInfo.hStdError = WritePipe;
    ZeroMemory(&ProcessInformation, sizeof(ProcessInformation));

    RunnerPrint("RPI5_WGLGEARS_BEGIN source=mesa-demos "
                "commit=10418e7636cdd9595dda18c3d78a562d7248f734 "
                "duration_ms=%lu\n",
                (ULONG)WGLGEARS_RUN_MILLISECONDS);
    if (!CreateProcessA(ApplicationPath,
                        CommandLine,
                        NULL,
                        NULL,
                        TRUE,
                        0,
                        NULL,
                        NULL,
                        &StartupInfo,
                        &ProcessInformation))
    {
        RunnerPrint("RPI5_WGLGEARS_ERROR create_process=%lu\n",
                    GetLastError());
        CloseHandle(ReadPipe);
        CloseHandle(WritePipe);
        return 1;
    }

    CloseHandle(WritePipe);
    WritePipe = NULL;
    CloseHandle(ProcessInformation.hThread);

    StartTick = GetTickCount();
    do
    {
        WaitStatus = WaitForSingleObject(ProcessInformation.hProcess, 20);
        DrainChildOutput(ReadPipe);
        if (WaitStatus == WAIT_OBJECT_0)
            break;
    } while (GetTickCount() - StartTick < WGLGEARS_RUN_MILLISECONDS);

    if (WaitStatus != WAIT_OBJECT_0)
    {
        ZeroMemory(&CloseContext, sizeof(CloseContext));
        CloseContext.ProcessId = ProcessInformation.dwProcessId;
        EnumWindows(CloseProcessWindow, (LPARAM)&CloseContext);
        RunnerPrint("RPI5_WGLGEARS_CLOSE windows=%lu\n",
                    CloseContext.WindowCount);

        CloseTick = GetTickCount();
        do
        {
            WaitStatus = WaitForSingleObject(ProcessInformation.hProcess, 20);
            DrainChildOutput(ReadPipe);
            if (WaitStatus == WAIT_OBJECT_0)
                break;
        } while (GetTickCount() - CloseTick <
                 WGLGEARS_CLOSE_MILLISECONDS);
    }

    if (WaitStatus != WAIT_OBJECT_0)
    {
        Forced = TRUE;
        TerminateProcess(ProcessInformation.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(ProcessInformation.hProcess, 1000);
    }

    DrainChildOutput(ReadPipe);
    if (!GetExitCodeProcess(ProcessInformation.hProcess, &ExitCode))
        ExitCode = GetLastError();
    RunnerPrint("RPI5_WGLGEARS_END exit=%lu runtime_ms=%lu forced=%lu\n",
                ExitCode,
                GetTickCount() - StartTick,
                Forced);

    CloseHandle(ReadPipe);
    CloseHandle(ProcessInformation.hProcess);
    return ExitCode == EXIT_SUCCESS && !Forced ? 0 : 1;
}
