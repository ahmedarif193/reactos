/*
 * PROJECT:     ReactOS Raspberry Pi 5 graphics validation
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Relay the unmodified glmark2 Win32 benchmark to the debug log
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define GLMARK2_TIMEOUT_MILLISECONDS 60000

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

    while (PeekNamedPipe(Pipe, NULL, 0, NULL, &Available, NULL) &&
           Available != 0)
    {
        BytesToRead = Available;
        if (BytesToRead >= sizeof(Buffer))
            BytesToRead = sizeof(Buffer) - 1;
        if (!ReadFile(Pipe, Buffer, BytesToRead, &BytesRead, NULL) ||
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

int
main(VOID)
{
    CHAR ApplicationPath[MAX_PATH];
    CHAR CommandLine[MAX_PATH * 8];
    CHAR DataPath[MAX_PATH];
    CHAR SystemDirectory[MAX_PATH];
    SECURITY_ATTRIBUTES SecurityAttributes;
    STARTUPINFOA StartupInfo;
    PROCESS_INFORMATION ProcessInformation;
    HANDLE ReadPipe = NULL;
    HANDLE WritePipe = NULL;
    DWORD ExitCode = ERROR_GEN_FAILURE;
    DWORD StartTick;
    DWORD WaitStatus;
    UINT Length;
    BOOL Forced = FALSE;

    Length = GetSystemDirectoryA(SystemDirectory, sizeof(SystemDirectory));
    if (Length == 0 || Length >= sizeof(SystemDirectory))
    {
        RunnerPrint("RPI5_GLMARK2_ERROR get_system_directory=%lu\n",
                    GetLastError());
        return 1;
    }

    if (_snprintf(ApplicationPath,
                  sizeof(ApplicationPath),
                  "%s\\glmark2-win32.exe",
                  SystemDirectory) < 0 ||
        _snprintf(DataPath,
                  sizeof(DataPath),
                  "%s\\glmark2",
                  SystemDirectory) < 0 ||
        _snprintf(CommandLine,
                  sizeof(CommandLine),
                  "\"%s\" --data-path \"%s\" -s 800x600 "
                  "-b ideas:speed=duration:duration=3.0 "
                  "-b jellyfish:duration=3.0 "
                  "-b terrain:duration=3.0 "
                  "-b shadow:duration=3.0",
                  ApplicationPath,
                  DataPath) < 0)
    {
        RunnerPrint("RPI5_GLMARK2_ERROR path_too_long\n");
        return 1;
    }
    ApplicationPath[sizeof(ApplicationPath) - 1] = '\0';
    DataPath[sizeof(DataPath) - 1] = '\0';
    CommandLine[sizeof(CommandLine) - 1] = '\0';

    ZeroMemory(&SecurityAttributes, sizeof(SecurityAttributes));
    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.bInheritHandle = TRUE;
    if (!CreatePipe(&ReadPipe, &WritePipe, &SecurityAttributes, 0) ||
        !SetHandleInformation(ReadPipe, HANDLE_FLAG_INHERIT, 0))
    {
        RunnerPrint("RPI5_GLMARK2_ERROR create_pipe=%lu\n",
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

    RunnerPrint("RPI5_GLMARK2_BEGIN source=glmark2 "
                "commit=22c527cb0556f3a1ac4445aaa52cc532760928d5 "
                "tests=21-24 scenes=ideas,jellyfish,terrain,shadow "
                "size=800x600 duration_s=3\n");
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
        RunnerPrint("RPI5_GLMARK2_ERROR create_process=%lu\n",
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
    } while (GetTickCount() - StartTick < GLMARK2_TIMEOUT_MILLISECONDS);

    if (WaitStatus != WAIT_OBJECT_0)
    {
        Forced = TRUE;
        TerminateProcess(ProcessInformation.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(ProcessInformation.hProcess, 1000);
    }

    DrainChildOutput(ReadPipe);
    if (!GetExitCodeProcess(ProcessInformation.hProcess, &ExitCode))
        ExitCode = GetLastError();
    RunnerPrint("RPI5_GLMARK2_END exit=%lu runtime_ms=%lu forced=%lu\n",
                ExitCode,
                GetTickCount() - StartTick,
                Forced);

    CloseHandle(ReadPipe);
    CloseHandle(ProcessInformation.hProcess);
    return ExitCode == EXIT_SUCCESS && !Forced ? 0 : 1;
}
