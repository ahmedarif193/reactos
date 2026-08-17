/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable AMD64-on-ARM64 thread, process, and remote-JIT parity probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define THREAD_COUNT 8
#define THREAD_LOOPS 10000
#define PROCESS_COUNT 4
#define TEST_TIMEOUT 60000

typedef INT (__cdecl *PCODE_FUNCTION)(INT Value);

typedef struct _THREAD_ARGUMENT
{
    INT Index;
    LONG Result;
} THREAD_ARGUMENT, *PTHREAD_ARGUMENT;

typedef struct _CROSS_PROCESS_SHARED
{
    volatile LONG Stage;
    ULONG_PTR CodeAddress;
    LONG FirstResult;
    LONG SecondResult;
    LONG ChildError;
} CROSS_PROCESS_SHARED, *PCROSS_PROCESS_SHARED;

static HANDLE ThreadStartEvent;
static DWORD ThreadTlsIndex;
static PCODE_FUNCTION ThreadCode;
static volatile LONG64 ThreadTotal;

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

static PVOID
create_code_buffer(const BYTE *Code, SIZE_T Length)
{
    PVOID Buffer;
    DWORD OldProtect;

    Buffer = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Buffer)
        return NULL;

    CopyMemory(Buffer, Code, Length);
    if (!VirtualProtect(Buffer, 0x1000, PAGE_EXECUTE_READ, &OldProtect) || !FlushInstructionCache(GetCurrentProcess(), Buffer, Length))
    {
        VirtualFree(Buffer, 0, MEM_RELEASE);
        return NULL;
    }

    return Buffer;
}

static DWORD WINAPI
thread_worker(PVOID Parameter)
{
    PTHREAD_ARGUMENT Argument = Parameter;
    LONG64 LocalTotal = 0;
    INT Expected = Argument->Index + 1;
    INT Index;

    if (!TlsSetValue(ThreadTlsIndex, (PVOID)(ULONG_PTR)Expected))
        return 1;
    if (WaitForSingleObject(ThreadStartEvent, TEST_TIMEOUT) != WAIT_OBJECT_0)
        return 2;
    if ((ULONG_PTR)TlsGetValue(ThreadTlsIndex) != (ULONG_PTR)Expected)
        return 3;

    for (Index = 0; Index < THREAD_LOOPS; ++Index)
    {
        INT Value = ThreadCode(Argument->Index);

        if (Value != Expected)
            return 4;
        LocalTotal += Value;
    }

    InterlockedExchangeAdd64(&ThreadTotal, LocalTotal);
    Argument->Result = 0;
    return 0;
}

static DWORD WINAPI
blocking_thread(PVOID Parameter)
{
    SetEvent((HANDLE)Parameter);
    for (;;)
        Sleep(10);
}

static DWORD WINAPI
quick_thread(PVOID Parameter)
{
    return (DWORD)(ULONG_PTR)Parameter;
}

static DWORD WINAPI
exit_thread(PVOID Parameter)
{
    ExitThread((DWORD)(ULONG_PTR)Parameter);
    return 0;
}

static INT
test_threads(VOID)
{
    static const BYTE Code[] = {0x8d, 0x41, 0x01, 0xc3};
    THREAD_ARGUMENT Arguments[THREAD_COUNT];
    HANDLE Threads[THREAD_COUNT];
    DWORD ExitCode;
    LONG64 ExpectedTotal;
    INT Index, Result = 0;

    ThreadCode = (PCODE_FUNCTION)create_code_buffer(Code, sizeof(Code));
    ThreadStartEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    ThreadTlsIndex = TlsAlloc();
    if (!ThreadCode || !ThreadStartEvent || ThreadTlsIndex == TLS_OUT_OF_INDEXES)
        return 1;

    ThreadTotal = 0;
    ZeroMemory(Threads, sizeof(Threads));
    for (Index = 0; Index < THREAD_COUNT; ++Index)
    {
        Arguments[Index].Index = Index;
        Arguments[Index].Result = -1;
        Threads[Index] = CreateThread(NULL, 0, thread_worker, &Arguments[Index], 0, NULL);
        if (!Threads[Index])
        {
            Result = 2;
            break;
        }
    }

    SetEvent(ThreadStartEvent);
    if (!Result && WaitForMultipleObjects(THREAD_COUNT, Threads, TRUE, TEST_TIMEOUT) != WAIT_OBJECT_0)
        Result = 3;

    for (Index = 0; Index < THREAD_COUNT; ++Index)
    {
        if (!Threads[Index])
            continue;
        if (!GetExitCodeThread(Threads[Index], &ExitCode) || ExitCode || Arguments[Index].Result)
            Result = 4;
        CloseHandle(Threads[Index]);
    }

    ExpectedTotal = (LONG64)THREAD_LOOPS * THREAD_COUNT * (THREAD_COUNT + 1) / 2;
    if (ThreadTotal != ExpectedTotal)
        Result = 5;

    TlsFree(ThreadTlsIndex);
    CloseHandle(ThreadStartEvent);
    VirtualFree((PVOID)ThreadCode, 0, MEM_RELEASE);
    printf("THREADS count=%d loops=%d total=%lld expected=%lld result=%d\n", THREAD_COUNT, THREAD_LOOPS, ThreadTotal, ExpectedTotal, Result);
    return Result;
}

static INT
test_thread_termination(VOID)
{
    HANDLE Thread, Duplicate, Started;
    DWORD ExitCode;
    BOOL SuspendedResult = FALSE, RunningResult = FALSE, DeadResult = FALSE, ExitResult = FALSE;
    INT Result = 0;

    Thread = CreateThread(NULL, 0, quick_thread, (PVOID)(ULONG_PTR)0x30, CREATE_SUSPENDED, NULL);
    if (Thread)
    {
        SuspendedResult = TerminateThread(Thread, 0x31);
        if (SuspendedResult && WaitForSingleObject(Thread, TEST_TIMEOUT) == WAIT_OBJECT_0 && GetExitCodeThread(Thread, &ExitCode) && ExitCode == 0x31)
            Result |= 0;
        else
            Result |= 1;
        CloseHandle(Thread);
    }
    else
    {
        Result |= 1;
    }

    Started = CreateEventA(NULL, TRUE, FALSE, NULL);
    Thread = Started ? CreateThread(NULL, 0, blocking_thread, Started, 0, NULL) : NULL;
    Duplicate = NULL;
    if (Thread && WaitForSingleObject(Started, TEST_TIMEOUT) == WAIT_OBJECT_0 && DuplicateHandle(GetCurrentProcess(), Thread, GetCurrentProcess(), &Duplicate, THREAD_ALL_ACCESS, FALSE, 0))
    {
        RunningResult = TerminateThread(Duplicate, 0x32);
        if (!RunningResult || WaitForSingleObject(Thread, TEST_TIMEOUT) != WAIT_OBJECT_0 || !GetExitCodeThread(Thread, &ExitCode) || ExitCode != 0x32)
            Result |= 2;
    }
    else
    {
        Result |= 2;
    }
    if (Duplicate)
        CloseHandle(Duplicate);
    if (Thread)
        CloseHandle(Thread);
    if (Started)
        CloseHandle(Started);

    Thread = CreateThread(NULL, 0, quick_thread, (PVOID)(ULONG_PTR)0x33, 0, NULL);
    if (Thread && WaitForSingleObject(Thread, TEST_TIMEOUT) == WAIT_OBJECT_0)
    {
        DeadResult = TerminateThread(Thread, 0x34);
        if (!GetExitCodeThread(Thread, &ExitCode) || ExitCode != 0x33)
            Result |= 4;
        CloseHandle(Thread);
    }
    else
    {
        Result |= 4;
    }

    Thread = CreateThread(NULL, 0, exit_thread, (PVOID)(ULONG_PTR)0x35, 0, NULL);
    if (Thread && WaitForSingleObject(Thread, TEST_TIMEOUT) == WAIT_OBJECT_0 && GetExitCodeThread(Thread, &ExitCode) && ExitCode == 0x35)
        ExitResult = TRUE;
    else
        Result |= 8;
    if (Thread)
        CloseHandle(Thread);

    printf("TERMINATE suspended=%u running=%u dead=%u selfexit=%u result=%d\n", SuspendedResult, RunningResult, DeadResult, ExitResult, Result);
    return Result;
}

static BOOL
create_self_process(PCSTR Arguments, PPROCESS_INFORMATION ProcessInformation)
{
    CHAR Executable[MAX_PATH];
    CHAR CommandLine[MAX_PATH * 2];
    STARTUPINFOA StartupInfo;

    if (!GetModuleFileNameA(NULL, Executable, sizeof(Executable)))
        return FALSE;
    if (snprintf(CommandLine, sizeof(CommandLine), "\"%s\" %s", Executable, Arguments) < 0)
        return FALSE;

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    ZeroMemory(ProcessInformation, sizeof(*ProcessInformation));
    return CreateProcessA(Executable, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, ProcessInformation);
}

static INT
quick_child(INT Index)
{
    static const BYTE Code[] = {0x8d, 0x41, 0x01, 0xc3};
    PCODE_FUNCTION Function = (PCODE_FUNCTION)create_code_buffer(Code, sizeof(Code));
    INT Result;

    if (!Function)
        return 0xff;
    Result = Function(Index);
    VirtualFree((PVOID)Function, 0, MEM_RELEASE);
    return Result == Index + 1 ? 0x40 + Index : 0xff;
}

static INT
test_process_lifecycle(VOID)
{
    PROCESS_INFORMATION Processes[PROCESS_COUNT];
    HANDLE Handles[PROCESS_COUNT];
    CHAR Arguments[64];
    DWORD ExitCode;
    INT Index, Round, Result = 0;

    for (Round = 0; Round < PROCESS_COUNT; ++Round)
    {
        snprintf(Arguments, sizeof(Arguments), "--quick %d", Round);
        if (!create_self_process(Arguments, &Processes[0]))
        {
            Result = 1;
            break;
        }
        CloseHandle(Processes[0].hThread);
        if (WaitForSingleObject(Processes[0].hProcess, TEST_TIMEOUT) != WAIT_OBJECT_0 || !GetExitCodeProcess(Processes[0].hProcess, &ExitCode) || ExitCode != (DWORD)(0x40 + Round))
            Result = 2;
        CloseHandle(Processes[0].hProcess);
        if (Result)
            break;
    }

    ZeroMemory(Processes, sizeof(Processes));
    ZeroMemory(Handles, sizeof(Handles));
    if (!Result)
    {
        for (Index = 0; Index < PROCESS_COUNT; ++Index)
        {
            snprintf(Arguments, sizeof(Arguments), "--quick %d", Index);
            if (!create_self_process(Arguments, &Processes[Index]))
            {
                Result = 3;
                break;
            }
            CloseHandle(Processes[Index].hThread);
            Handles[Index] = Processes[Index].hProcess;
        }
    }

    if (!Result && WaitForMultipleObjects(PROCESS_COUNT, Handles, TRUE, TEST_TIMEOUT) != WAIT_OBJECT_0)
        Result = 4;
    for (Index = 0; Index < PROCESS_COUNT; ++Index)
    {
        if (!Handles[Index])
            continue;
        if (!GetExitCodeProcess(Handles[Index], &ExitCode) || ExitCode != (DWORD)(0x40 + Index))
            Result = 5;
        CloseHandle(Handles[Index]);
    }

    printf("PROCESSES sequential=%d concurrent=%d result=%d\n", PROCESS_COUNT, PROCESS_COUNT, Result);
    return Result;
}

static VOID
make_cross_names(PCSTR Suffix, PCHAR MapName, PCHAR ReadyName, PCHAR GoName)
{
    snprintf(MapName, 96, "CHPEParityMap_%s", Suffix);
    snprintf(ReadyName, 96, "CHPEParityReady_%s", Suffix);
    snprintf(GoName, 96, "CHPEParityGo_%s", Suffix);
}

static INT
cross_child(PCSTR Suffix)
{
    static const BYTE InitialCode[] = {0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3};
    CHAR MapName[96], ReadyName[96], GoName[96];
    PCROSS_PROCESS_SHARED Shared;
    PCODE_FUNCTION Function;
    HANDLE Mapping, Ready, Go;
    INT Result = 0;

    make_cross_names(Suffix, MapName, ReadyName, GoName);
    Mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, MapName);
    Ready = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, ReadyName);
    Go = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, GoName);
    Shared = Mapping ? MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*Shared)) : NULL;
    Function = (PCODE_FUNCTION)create_code_buffer(InitialCode, sizeof(InitialCode));
    if (!Shared || !Ready || !Go || !Function)
        Result = 1;

    if (!Result)
    {
        Shared->FirstResult = Function(0);
        Shared->CodeAddress = (ULONG_PTR)Function;
        InterlockedExchange(&Shared->Stage, 1);
        SetEvent(Ready);
        if (WaitForSingleObject(Go, TEST_TIMEOUT) != WAIT_OBJECT_0)
            Result = 2;
        Sleep(0);
        Shared->SecondResult = Function(0);
        if (Shared->FirstResult != 1 || Shared->SecondResult != 2)
            Result = 3;
    }

    if (Shared)
    {
        Shared->ChildError = Result;
        InterlockedExchange(&Shared->Stage, 2);
    }
    if (Function)
        VirtualFree((PVOID)Function, 0, MEM_RELEASE);
    if (Shared)
        UnmapViewOfFile(Shared);
    if (Go)
        CloseHandle(Go);
    if (Ready)
        CloseHandle(Ready);
    if (Mapping)
        CloseHandle(Mapping);
    return Result;
}

static INT
test_cross_process(VOID)
{
    static const BYTE UpdatedCode[] = {0xb8, 0x02, 0x00, 0x00, 0x00, 0xc3};
    CHAR Suffix[48], MapName[96], ReadyName[96], GoName[96], Arguments[96];
    PCROSS_PROCESS_SHARED Shared = NULL;
    PROCESS_INFORMATION ProcessInformation;
    HANDLE Mapping = NULL, Ready = NULL, Go = NULL;
    PVOID Scratch = NULL;
    DWORD OldProtect, ExitCode;
    SIZE_T Written;
    INT Result = 0;

    ZeroMemory(&ProcessInformation, sizeof(ProcessInformation));
    snprintf(Suffix, sizeof(Suffix), "%lu_%lu", GetCurrentProcessId(), GetTickCount());
    make_cross_names(Suffix, MapName, ReadyName, GoName);
    Mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*Shared), MapName);
    Ready = CreateEventA(NULL, TRUE, FALSE, ReadyName);
    Go = CreateEventA(NULL, TRUE, FALSE, GoName);
    Shared = Mapping ? MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*Shared)) : NULL;
    if (!Mapping || !Ready || !Go || !Shared)
        Result = 1;
    if (!Result)
        ZeroMemory(Shared, sizeof(*Shared));

    snprintf(Arguments, sizeof(Arguments), "--cross-child %s", Suffix);
    if (!Result && !create_self_process(Arguments, &ProcessInformation))
        Result = 2;
    if (!Result)
    {
        CloseHandle(ProcessInformation.hThread);
        if (WaitForSingleObject(Ready, TEST_TIMEOUT) != WAIT_OBJECT_0 || Shared->Stage != 1 || Shared->FirstResult != 1 || !Shared->CodeAddress)
            Result = 3;
    }

    if (!Result)
    {
        Scratch = VirtualAllocEx(ProcessInformation.hProcess, NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!Scratch || !WriteProcessMemory(ProcessInformation.hProcess, Scratch, UpdatedCode, sizeof(UpdatedCode), &Written) || Written != sizeof(UpdatedCode) || !VirtualProtectEx(ProcessInformation.hProcess, Scratch, 0x1000, PAGE_READONLY, &OldProtect) || !VirtualFreeEx(ProcessInformation.hProcess, Scratch, 0, MEM_RELEASE))
            Result = 4;
    }

    if (!Result)
    {
        if (!VirtualProtectEx(ProcessInformation.hProcess, (PVOID)Shared->CodeAddress, 0x1000, PAGE_READWRITE, &OldProtect) || !WriteProcessMemory(ProcessInformation.hProcess, (PVOID)Shared->CodeAddress, UpdatedCode, sizeof(UpdatedCode), &Written) || Written != sizeof(UpdatedCode) || !VirtualProtectEx(ProcessInformation.hProcess, (PVOID)Shared->CodeAddress, 0x1000, PAGE_EXECUTE_READ, &OldProtect) || !FlushInstructionCache(ProcessInformation.hProcess, (PVOID)Shared->CodeAddress, sizeof(UpdatedCode)))
            Result = 5;
    }

    if (!Result)
        SetEvent(Go);
    else if (Go)
        SetEvent(Go);

    if (ProcessInformation.hProcess)
    {
        if (WaitForSingleObject(ProcessInformation.hProcess, TEST_TIMEOUT) != WAIT_OBJECT_0 || !GetExitCodeProcess(ProcessInformation.hProcess, &ExitCode) || ExitCode != 0)
            Result = Result ? Result : 6;
        CloseHandle(ProcessInformation.hProcess);
    }
    if (!Result && (Shared->Stage != 2 || Shared->FirstResult != 1 || Shared->SecondResult != 2 || Shared->ChildError))
        Result = 7;

    printf("CROSS first=%ld second=%ld child=%ld result=%d\n", Shared ? Shared->FirstResult : -1, Shared ? Shared->SecondResult : -1, Shared ? Shared->ChildError : -1, Result);
    if (Shared)
        UnmapViewOfFile(Shared);
    if (Go)
        CloseHandle(Go);
    if (Ready)
        CloseHandle(Ready);
    if (Mapping)
        CloseHandle(Mapping);
    return Result;
}

int
main(int argc, char **argv)
{
    INT ThreadResult, TerminateResult, ProcessResult, CrossResult;

    if (argc == 3 && !lstrcmpiA(argv[1], "--quick"))
        return quick_child(atoi(argv[2]));
    if (argc == 3 && !lstrcmpiA(argv[1], "--cross-child"))
        return cross_child(argv[2]);

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_LIFECYCLE_CROSSPROCESS_BEGIN\n");
    ThreadResult = test_threads();
    TerminateResult = test_thread_termination();
    ProcessResult = test_process_lifecycle();
    CrossResult = test_cross_process();

    if (ThreadResult || TerminateResult || ProcessResult || CrossResult)
    {
        printf("CHPE_LIFECYCLE_CROSSPROCESS_FAIL threads=%d terminate=%d process=%d cross=%d\n", ThreadResult, TerminateResult, ProcessResult, CrossResult);
        return 1;
    }

    printf("CHPE_LIFECYCLE_CROSSPROCESS_PASS\n");
    return 0;
}
