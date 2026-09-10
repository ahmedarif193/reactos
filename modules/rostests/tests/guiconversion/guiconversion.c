/*
 * PROJECT:     ReactOS regression tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Exercise failed GUI thread conversion after thread teardown
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

/* Keep USER32 out of the import table so the child can report a load failure. */
static DWORD WINAPI
WorkerThread(PVOID Event)
{
    return WaitForSingleObject(Event, INFINITE);
}

int
main(int argc, char **argv)
{
    WCHAR Path[MAX_PATH], Command[MAX_PATH + 32], Desktop[100];
    STARTUPINFOW Startup = {0};
    PROCESS_INFORMATION Process = {0};
    HANDLE Event, Threads[32];
    DWORD Length, Error, Wait, ExitCode = 0;
    ULONG Index, ThreadCount = 0, Failures = 0;
    BOOL Created;

    UNREFERENCED_PARAMETER(argv);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    if (argc > 1)
    {
        HMODULE User32 = LoadLibraryW(L"user32.dll");
        Error = GetLastError();
        printf("GUI_LOAD module=%p error=%lu\n", (PVOID)User32, Error);
        if (User32)
            FreeLibrary(User32);
        return !User32 && Error == ERROR_DLL_INIT_FAILED ? 47 : 48;
    }

    Length = GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    if (!Length || Length >= ARRAYSIZE(Path))
        return 1;

    _snwprintf(Command, ARRAYSIZE(Command), L"\"%ls\" child", Path);
    _snwprintf(Desktop, ARRAYSIZE(Desktop), L"WinSta0\\ReactOS_Missing_Desktop_%lu", GetCurrentProcessId());
    Startup.cb = sizeof(Startup);
    Startup.lpDesktop = Desktop;
    Created = CreateProcessW(Path, Command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &Startup, &Process);
    Error = GetLastError();
    printf("GUI_CREATE ok=%d error=%lu pid=%lu\n", Created, Error, Process.dwProcessId);
    if (!Created)
        return 1;

    /*
     * Retire several ordinary threads while the child is suspended. This
     * exercises conversion with both cached and released small kernel stacks.
     * The child must survive rejection of its inaccessible startup desktop.
     */
    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Event)
    {
        ++Failures;
        goto Cleanup;
    }

    for (Index = 0; Index < ARRAYSIZE(Threads); ++Index)
    {
        HANDLE Thread = CreateThread(NULL, 0, WorkerThread, Event, 0, NULL);
        if (!Thread)
            break;
        Threads[ThreadCount++] = Thread;
    }

    if (!SetEvent(Event))
        ++Failures;
    if (ThreadCount && WaitForMultipleObjects(ThreadCount, Threads, TRUE, 10000) != WAIT_OBJECT_0)
        ++Failures;
    for (Index = 0; Index < ThreadCount; ++Index)
        CloseHandle(Threads[Index]);
    CloseHandle(Event);
    if (ThreadCount != ARRAYSIZE(Threads))
        ++Failures;
    Sleep(500);
    printf("GUI_CHURN threads=%lu\n", ThreadCount);

    if (ResumeThread(Process.hThread) != 1)
        ++Failures;
    Wait = WaitForSingleObject(Process.hProcess, 10000);
    if (!GetExitCodeProcess(Process.hProcess, &ExitCode))
        ++Failures;
    printf("GUI_EXIT wait=%lx code=%08lx\n", Wait, ExitCode);
    if (Wait != WAIT_OBJECT_0 || ExitCode != 47)
        ++Failures;

Cleanup:
    if (WaitForSingleObject(Process.hProcess, 0) != WAIT_OBJECT_0)
    {
        TerminateProcess(Process.hProcess, 99);
        WaitForSingleObject(Process.hProcess, 1000);
    }
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    printf("GUI_CONVERSION_DONE failures=%lu\n", Failures);
    return Failures != 0;
}
