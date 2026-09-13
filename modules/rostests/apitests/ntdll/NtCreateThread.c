/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     See COPYING in the top level directory
 * PURPOSE:     Test for NtCreateThread
 * PROGRAMMER:  Aleksandar Andrejevic <theflash AT sdf DOT lonestar DOT org>
 */

#include "precomp.h"

START_TEST(NtCreateThread)
{
    NTSTATUS Status;
    INITIAL_TEB InitialTeb;
    HANDLE ThreadHandle;
    OBJECT_ATTRIBUTES Attributes;

    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    ZeroMemory(&InitialTeb, sizeof(INITIAL_TEB));

    Status = NtCreateThread(&ThreadHandle,
                            0,
                            &Attributes,
                            NtCurrentProcess(),
                            NULL,
                            (PCONTEXT)0x70000000, /* Aligned usermode address */
                            &InitialTeb,
                            FALSE);

    ok_hex(Status, STATUS_ACCESS_VIOLATION);
}

typedef struct _NATIVE_THREAD_WIN32_RESULT
{
    BOOL Created;
    DWORD Error;
    DWORD Wait;
    DWORD ExitCode;
} NATIVE_THREAD_WIN32_RESULT;

static DWORD WINAPI
NativeThreadWin32Child(PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    return 37;
}

static ULONG NTAPI
NativeThreadWin32Start(PVOID Parameter)
{
    NATIVE_THREAD_WIN32_RESULT *Result = Parameter;
    HANDLE Thread;

    Thread = CreateThread(NULL, 0, NativeThreadWin32Child, NULL, 0, NULL);
    Result->Created = Thread != NULL;
    Result->Error = GetLastError();
    if (Thread)
    {
        Result->Wait = WaitForSingleObject(Thread, 5000);
        GetExitCodeThread(Thread, &Result->ExitCode);
        CloseHandle(Thread);
    }

    RtlExitUserThread(0);
    return 0;
}

START_TEST(NtCreateThreadWin32)
{
    /* Static storage also survives an unexpected timeout in the worker. */
    static NATIVE_THREAD_WIN32_RESULT Result;
    HANDLE Thread;
    NTSTATUS Status;
    DWORD Wait;
    UINT Iteration;

    for (Iteration = 0; Iteration < 8; ++Iteration)
    {
        ZeroMemory(&Result, sizeof(Result));
        Result.Wait = WAIT_FAILED;
        Status = RtlCreateUserThread(NtCurrentProcess(), NULL, FALSE, 0, 0, 0, NativeThreadWin32Start, &Result, &Thread, NULL);
        ok_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) return;

        Wait = WaitForSingleObject(Thread, 10000);
        CloseHandle(Thread);
        ok(Wait == WAIT_OBJECT_0, "iteration %u: native thread wait %lu\n", Iteration, Wait);
        if (Wait != WAIT_OBJECT_0) return;

        ok(Result.Created, "iteration %u: CreateThread failed in native thread: %lu\n", Iteration, Result.Error);
        ok(Result.Wait == WAIT_OBJECT_0, "iteration %u: child wait %lu\n", Iteration, Result.Wait);
        ok(Result.ExitCode == 37, "iteration %u: child exit %lu\n", Iteration, Result.ExitCode);
    }
}
