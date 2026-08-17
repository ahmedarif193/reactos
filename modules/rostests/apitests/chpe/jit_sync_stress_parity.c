/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable AMD64-on-ARM64 JIT and loader synchronization stress probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define LOCAL_THREAD_COUNT 4
#define LOCAL_LOOP_COUNT 2000
#define LOADER_THREAD_COUNT 4
#define LOADER_LOOP_COUNT 250
#define REMOTE_THREAD_COUNT 4
#define REMOTE_LOOP_COUNT 512
#define TEST_TIMEOUT 120000

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((LONG)(Status)) >= 0)
#endif

typedef LONG NTSTATUS;
typedef INT (__cdecl *PCODE_FUNCTION)(VOID);

__declspec(dllimport) NTSTATUS NTAPI NtWriteVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, SIZE_T NumberOfBytesToWrite, PSIZE_T NumberOfBytesWritten);
__declspec(dllimport) NTSTATUS NTAPI NtFlushInstructionCache(HANDLE ProcessHandle, PVOID BaseAddress, SIZE_T NumberOfBytesToFlush);

typedef struct _LOCAL_ARGUMENT
{
    HANDLE StartEvent;
    INT ThreadIndex;
    volatile LONG Result;
} LOCAL_ARGUMENT, *PLOCAL_ARGUMENT;

typedef struct _REMOTE_SHARED
{
    volatile LONG Stage;
    ULONG_PTR CodeAddress[REMOTE_THREAD_COUNT];
    LONG Result[REMOTE_THREAD_COUNT];
    volatile LONG ChildError;
} REMOTE_SHARED, *PREMOTE_SHARED;

typedef struct _REMOTE_ARGUMENT
{
    HANDLE StartEvent;
    HANDLE ProcessHandle;
    PVOID CodeAddress;
    INT ThreadIndex;
    volatile LONG Result;
} REMOTE_ARGUMENT, *PREMOTE_ARGUMENT;

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

static VOID
make_return_code(BYTE Code[6], LONG Value)
{
    Code[0] = 0xb8;
    CopyMemory(&Code[1], &Value, sizeof(Value));
    Code[5] = 0xc3;
}

static LONG
remote_value(INT ThreadIndex, INT Iteration)
{
    return ((ThreadIndex + 1) << 20) | Iteration;
}

static DWORD WINAPI
local_jit_worker(PVOID Parameter)
{
    PLOCAL_ARGUMENT Argument = Parameter;
    PCODE_FUNCTION Function;
    PVOID CodePage;
    BYTE Code[6];
    SIZE_T Written;
    NTSTATUS Status;
    LONG Value;
    INT Iteration;

    CodePage = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!CodePage)
        return 1;
    if (WaitForSingleObject(Argument->StartEvent, TEST_TIMEOUT) != WAIT_OBJECT_0)
    {
        VirtualFree(CodePage, 0, MEM_RELEASE);
        return 2;
    }

    Function = (PCODE_FUNCTION)CodePage;
    for (Iteration = 1; Iteration <= LOCAL_LOOP_COUNT; ++Iteration)
    {
        Value = remote_value(Argument->ThreadIndex, Iteration);
        make_return_code(Code, Value);
        Written = 0;
        Status = NtWriteVirtualMemory(GetCurrentProcess(), CodePage, Code, sizeof(Code), &Written);
        if (!NT_SUCCESS(Status) || Written != sizeof(Code))
        {
            Argument->Result = 3;
            break;
        }
        Status = NtFlushInstructionCache(GetCurrentProcess(), CodePage, sizeof(Code));
        if (!NT_SUCCESS(Status))
        {
            Argument->Result = 4;
            break;
        }
        if (Function() != Value)
        {
            Argument->Result = 5;
            break;
        }
    }

    VirtualFree(CodePage, 0, MEM_RELEASE);
    return Argument->Result;
}

static INT
test_local_jit(VOID)
{
    LOCAL_ARGUMENT Arguments[LOCAL_THREAD_COUNT];
    HANDLE Threads[LOCAL_THREAD_COUNT];
    HANDLE StartEvent;
    DWORD ExitCode;
    INT Index;
    INT Result = 0;

    ZeroMemory(Arguments, sizeof(Arguments));
    ZeroMemory(Threads, sizeof(Threads));
    StartEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!StartEvent)
        return 1;

    for (Index = 0; Index < LOCAL_THREAD_COUNT; ++Index)
    {
        Arguments[Index].StartEvent = StartEvent;
        Arguments[Index].ThreadIndex = Index;
        Threads[Index] = CreateThread(NULL, 0, local_jit_worker, &Arguments[Index], 0, NULL);
        if (!Threads[Index])
        {
            Result = 2;
            break;
        }
    }

    SetEvent(StartEvent);
    if (!Result && WaitForMultipleObjects(LOCAL_THREAD_COUNT, Threads, TRUE, TEST_TIMEOUT) != WAIT_OBJECT_0)
        Result = 3;

    for (Index = 0; Index < LOCAL_THREAD_COUNT; ++Index)
    {
        if (!Threads[Index])
            continue;
        if (!GetExitCodeThread(Threads[Index], &ExitCode) || ExitCode || Arguments[Index].Result)
            Result = Result ? Result : 4;
        CloseHandle(Threads[Index]);
    }
    CloseHandle(StartEvent);
    printf("LOCAL_JIT threads=%d loops=%d result=%d\n", LOCAL_THREAD_COUNT, LOCAL_LOOP_COUNT, Result);
    return Result;
}

static DWORD WINAPI
loader_worker(PVOID Parameter)
{
    PLOCAL_ARGUMENT Argument = Parameter;
    HMODULE Module;
    FARPROC Procedure;
    INT Iteration;

    if (WaitForSingleObject(Argument->StartEvent, TEST_TIMEOUT) != WAIT_OBJECT_0)
        return 1;

    for (Iteration = 0; Iteration < LOADER_LOOP_COUNT; ++Iteration)
    {
        Module = LoadLibraryA("version.dll");
        if (!Module)
        {
            Argument->Result = 2;
            break;
        }
        Procedure = GetProcAddress(Module, "GetFileVersionInfoSizeW");
        if (!Procedure)
            Argument->Result = 3;
        if (!FreeLibrary(Module) && !Argument->Result)
            Argument->Result = 4;
        if (Argument->Result)
            break;
    }
    return Argument->Result;
}

static INT
test_loader_stress(VOID)
{
    LOCAL_ARGUMENT Arguments[LOADER_THREAD_COUNT];
    HANDLE Threads[LOADER_THREAD_COUNT];
    HANDLE StartEvent;
    DWORD ExitCode;
    INT Index;
    INT Result = 0;

    ZeroMemory(Arguments, sizeof(Arguments));
    ZeroMemory(Threads, sizeof(Threads));
    StartEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!StartEvent)
        return 1;

    for (Index = 0; Index < LOADER_THREAD_COUNT; ++Index)
    {
        Arguments[Index].StartEvent = StartEvent;
        Arguments[Index].ThreadIndex = Index;
        Threads[Index] = CreateThread(NULL, 0, loader_worker, &Arguments[Index], 0, NULL);
        if (!Threads[Index])
        {
            Result = 2;
            break;
        }
    }

    SetEvent(StartEvent);
    if (!Result && WaitForMultipleObjects(LOADER_THREAD_COUNT, Threads, TRUE, TEST_TIMEOUT) != WAIT_OBJECT_0)
        Result = 3;

    for (Index = 0; Index < LOADER_THREAD_COUNT; ++Index)
    {
        if (!Threads[Index])
            continue;
        if (!GetExitCodeThread(Threads[Index], &ExitCode) || ExitCode || Arguments[Index].Result)
            Result = Result ? Result : 4;
        CloseHandle(Threads[Index]);
    }
    CloseHandle(StartEvent);
    printf("LOADER threads=%d loops=%d result=%d\n", LOADER_THREAD_COUNT, LOADER_LOOP_COUNT, Result);
    return Result;
}

static VOID
make_cross_names(PCSTR Suffix, PCHAR MapName, PCHAR ReadyName, PCHAR GoName)
{
    snprintf(MapName, 96, "CHPEStressMap_%s", Suffix);
    snprintf(ReadyName, 96, "CHPEStressReady_%s", Suffix);
    snprintf(GoName, 96, "CHPEStressGo_%s", Suffix);
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
remote_child(PCSTR Suffix)
{
    CHAR MapName[96];
    CHAR ReadyName[96];
    CHAR GoName[96];
    PREMOTE_SHARED Shared;
    PCODE_FUNCTION Functions[REMOTE_THREAD_COUNT];
    HANDLE Mapping;
    HANDLE Ready;
    HANDLE Go;
    BYTE Code[6];
    DWORD OldProtect;
    INT Index;
    INT Result = 0;

    ZeroMemory(Functions, sizeof(Functions));
    make_cross_names(Suffix, MapName, ReadyName, GoName);
    Mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, MapName);
    Ready = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, ReadyName);
    Go = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, GoName);
    Shared = Mapping ? MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*Shared)) : NULL;
    if (!Shared || !Ready || !Go)
        Result = 1;

    for (Index = 0; !Result && Index < REMOTE_THREAD_COUNT; ++Index)
    {
        Functions[Index] = (PCODE_FUNCTION)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!Functions[Index])
        {
            Result = 2;
            break;
        }
        make_return_code(Code, 0);
        CopyMemory((PVOID)Functions[Index], Code, sizeof(Code));
        if (!VirtualProtect((PVOID)Functions[Index], 0x1000, PAGE_EXECUTE_READWRITE, &OldProtect) || !FlushInstructionCache(GetCurrentProcess(), (PVOID)Functions[Index], sizeof(Code)) || Functions[Index]() != 0)
        {
            Result = 3;
            break;
        }
        Shared->CodeAddress[Index] = (ULONG_PTR)Functions[Index];
    }

    if (!Result)
    {
        InterlockedExchange(&Shared->Stage, 1);
        SetEvent(Ready);
        if (WaitForSingleObject(Go, TEST_TIMEOUT) != WAIT_OBJECT_0)
            Result = 4;
    }

    if (!Result)
    {
        Sleep(0);
        for (Index = 0; Index < REMOTE_THREAD_COUNT; ++Index)
        {
            Shared->Result[Index] = Functions[Index]();
            if (Shared->Result[Index] != remote_value(Index, REMOTE_LOOP_COUNT))
                Result = 5;
        }
    }

    if (Shared)
    {
        Shared->ChildError = Result;
        InterlockedExchange(&Shared->Stage, 2);
    }
    for (Index = 0; Index < REMOTE_THREAD_COUNT; ++Index)
    {
        if (Functions[Index])
            VirtualFree((PVOID)Functions[Index], 0, MEM_RELEASE);
    }
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

static DWORD WINAPI
remote_writer(PVOID Parameter)
{
    PREMOTE_ARGUMENT Argument = Parameter;
    BYTE Code[6];
    SIZE_T Written;
    NTSTATUS Status;
    LONG Value;
    INT Iteration;

    if (WaitForSingleObject(Argument->StartEvent, TEST_TIMEOUT) != WAIT_OBJECT_0)
        return 1;
    for (Iteration = 1; Iteration <= REMOTE_LOOP_COUNT; ++Iteration)
    {
        Value = remote_value(Argument->ThreadIndex, Iteration);
        make_return_code(Code, Value);
        Written = 0;
        Status = NtWriteVirtualMemory(Argument->ProcessHandle, Argument->CodeAddress, Code, sizeof(Code), &Written);
        if (!NT_SUCCESS(Status) || Written != sizeof(Code))
        {
            Argument->Result = 2;
            break;
        }
        Status = NtFlushInstructionCache(Argument->ProcessHandle, Argument->CodeAddress, sizeof(Code));
        if (!NT_SUCCESS(Status))
        {
            Argument->Result = 3;
            break;
        }
    }
    return Argument->Result;
}

static INT
test_remote_queue_stress(VOID)
{
    CHAR Suffix[48];
    CHAR MapName[96];
    CHAR ReadyName[96];
    CHAR GoName[96];
    CHAR ArgumentsText[96];
    PREMOTE_SHARED Shared = NULL;
    REMOTE_ARGUMENT Arguments[REMOTE_THREAD_COUNT];
    PROCESS_INFORMATION ProcessInformation;
    HANDLE Threads[REMOTE_THREAD_COUNT];
    HANDLE Mapping = NULL;
    HANDLE Ready = NULL;
    HANDLE Go = NULL;
    HANDLE StartEvent = NULL;
    DWORD ExitCode;
    INT Index;
    INT Result = 0;

    ZeroMemory(&ProcessInformation, sizeof(ProcessInformation));
    ZeroMemory(Arguments, sizeof(Arguments));
    ZeroMemory(Threads, sizeof(Threads));
    snprintf(Suffix, sizeof(Suffix), "%lu_%lu", GetCurrentProcessId(), GetTickCount());
    make_cross_names(Suffix, MapName, ReadyName, GoName);
    Mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*Shared), MapName);
    Ready = CreateEventA(NULL, TRUE, FALSE, ReadyName);
    Go = CreateEventA(NULL, TRUE, FALSE, GoName);
    StartEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    Shared = Mapping ? MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*Shared)) : NULL;
    if (!Mapping || !Ready || !Go || !StartEvent || !Shared)
        Result = 1;
    if (!Result)
        ZeroMemory(Shared, sizeof(*Shared));

    snprintf(ArgumentsText, sizeof(ArgumentsText), "--remote-child %s", Suffix);
    if (!Result && !create_self_process(ArgumentsText, &ProcessInformation))
        Result = 2;
    if (!Result)
    {
        CloseHandle(ProcessInformation.hThread);
        if (WaitForSingleObject(Ready, TEST_TIMEOUT) != WAIT_OBJECT_0 || Shared->Stage != 1)
            Result = 3;
    }

    for (Index = 0; !Result && Index < REMOTE_THREAD_COUNT; ++Index)
    {
        Arguments[Index].StartEvent = StartEvent;
        Arguments[Index].ProcessHandle = ProcessInformation.hProcess;
        Arguments[Index].CodeAddress = (PVOID)Shared->CodeAddress[Index];
        Arguments[Index].ThreadIndex = Index;
        Threads[Index] = CreateThread(NULL, 0, remote_writer, &Arguments[Index], 0, NULL);
        if (!Threads[Index])
            Result = 4;
    }

    if (StartEvent)
        SetEvent(StartEvent);
    if (!Result && WaitForMultipleObjects(REMOTE_THREAD_COUNT, Threads, TRUE, TEST_TIMEOUT) != WAIT_OBJECT_0)
        Result = 5;
    for (Index = 0; Index < REMOTE_THREAD_COUNT; ++Index)
    {
        if (!Threads[Index])
            continue;
        if (!GetExitCodeThread(Threads[Index], &ExitCode) || ExitCode || Arguments[Index].Result)
            Result = Result ? Result : 6;
        CloseHandle(Threads[Index]);
    }

    if (Go)
        SetEvent(Go);
    if (ProcessInformation.hProcess)
    {
        if (WaitForSingleObject(ProcessInformation.hProcess, TEST_TIMEOUT) != WAIT_OBJECT_0 || !GetExitCodeProcess(ProcessInformation.hProcess, &ExitCode) || ExitCode)
            Result = Result ? Result : 7;
        CloseHandle(ProcessInformation.hProcess);
    }
    if (!Result && (Shared->Stage != 2 || Shared->ChildError))
        Result = 8;
    for (Index = 0; !Result && Index < REMOTE_THREAD_COUNT; ++Index)
    {
        if (Shared->Result[Index] != remote_value(Index, REMOTE_LOOP_COUNT))
            Result = 9;
    }

    printf("REMOTE_QUEUE writers=%d loops=%d last0=%ld last3=%ld child=%ld result=%d\n", REMOTE_THREAD_COUNT, REMOTE_LOOP_COUNT, Shared ? Shared->Result[0] : -1, Shared ? Shared->Result[REMOTE_THREAD_COUNT - 1] : -1, Shared ? Shared->ChildError : -1, Result);
    if (Shared)
        UnmapViewOfFile(Shared);
    if (StartEvent)
        CloseHandle(StartEvent);
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
    INT LocalResult;
    INT LoaderResult;
    INT RemoteResult;

    if (argc == 3 && !lstrcmpiA(argv[1], "--remote-child"))
        return remote_child(argv[2]);

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_JIT_SYNC_STRESS_BEGIN\n");
    LocalResult = test_local_jit();
    LoaderResult = test_loader_stress();
    RemoteResult = test_remote_queue_stress();
    if (LocalResult || LoaderResult || RemoteResult)
    {
        printf("CHPE_JIT_SYNC_STRESS_FAIL local=%d loader=%d remote=%d\n", LocalResult, LoaderResult, RemoteResult);
        return 1;
    }
    printf("CHPE_JIT_SYNC_STRESS_PASS\n");
    return 0;
}
