#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *SkipToken(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"')
    {
        p++;
        while (*p && *p != '"') p++;
        if (*p == '"') p++;
    }
    else
    {
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

int main(int argc, char *argv[])
{
    HANDLE Mutex;
    STARTUPINFOA StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    DWORD ExitCode = 0;
    char CommandLine[2048];
    const char *Rest;

    if (argc < 3)
    {
        fprintf(stderr, "usage: holdmutex <mutex-name> <command> [args...]\n");
        return 2;
    }

    Mutex = CreateMutexA(NULL, FALSE, argv[1]);
    if (Mutex == NULL)
    {
        fprintf(stderr, "holdmutex: CreateMutex(%s) failed, error %lu\n", argv[1], GetLastError());
        return 1;
    }
    printf("holdmutex: holding %s (%s)\n", argv[1],
           GetLastError() == ERROR_ALREADY_EXISTS ? "already existed" : "created");

    Rest = SkipToken(SkipToken(GetCommandLineA()));
    strncpy(CommandLine, Rest, sizeof(CommandLine) - 1);
    CommandLine[sizeof(CommandLine) - 1] = '\0';

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        fprintf(stderr, "holdmutex: CreateProcess(%s) failed, error %lu\n", CommandLine, GetLastError());
        CloseHandle(Mutex);
        return 1;
    }
    printf("holdmutex: started pid %lu: %s\n", ProcessInfo.dwProcessId, CommandLine);
    fflush(stdout);

    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    printf("holdmutex: pid %lu exited with %lu\n", ProcessInfo.dwProcessId, ExitCode);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
    CloseHandle(Mutex);
    return (int)ExitCode;
}
