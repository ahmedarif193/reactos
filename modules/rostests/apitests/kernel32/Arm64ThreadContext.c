/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 cross-thread context capture and restore coverage
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

#define CONTEXT_ROUNDS 64

#ifdef _M_ARM64

C_ASSERT(TYPE_ALIGNMENT(CONTEXT) == 16);

static HANDLE WorkerReady;
static volatile LONG StopWorker;
static volatile LONG WorkerProgress;

static DWORD
WINAPI
ContextWorker(
    _In_opt_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    SetEvent(WorkerReady);
    while (InterlockedCompareExchange(&StopWorker, 0, 0) == 0)
    {
        InterlockedIncrement(&WorkerProgress);
        YieldProcessor();
    }

    return 0x1234;
}

static BOOL
CaptureContext(
    _In_ HANDLE Thread,
    _Out_ PCONTEXT Context)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    return GetThreadContext(Thread, Context);
}

static VOID
CheckUserContext(
    _In_ PCONTEXT Context,
    _In_ PCSTR Phase,
    _In_ ULONG Round)
{
    ok(Context->Pc != 0,
       "%s round %lu returned a NULL PC\n",
       Phase,
       Round);
    ok((Context->Pc & (sizeof(ULONG) - 1)) == 0,
       "%s round %lu returned an unaligned PC %I64x\n",
       Phase,
       Round,
       Context->Pc);
    ok(Context->Sp != 0,
       "%s round %lu returned a NULL SP\n",
       Phase,
       Round);
    ok((Context->Sp & 15) == 0,
       "%s round %lu returned an unaligned SP %I64x\n",
       Phase,
       Round,
       Context->Sp);
}

START_TEST(Arm64ThreadContext)
{
    CONTEXT Before, After;
    HANDLE Thread;
    DWORD ThreadId;
    DWORD Result;
    ULONG Round = 0;
    BOOL Success;

    trace("CONTEXT size %Iu, alignment %Iu, records %p/%p\n",
          sizeof(CONTEXT),
          (SIZE_T)TYPE_ALIGNMENT(CONTEXT),
          &Before,
          &After);
    ok(((ULONG_PTR)&Before & 15) == 0,
       "first CONTEXT record is not 16-byte aligned: %p\n",
       &Before);
    ok(((ULONG_PTR)&After & 15) == 0,
       "second CONTEXT record is not 16-byte aligned: %p\n",
       &After);

    WorkerReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(WorkerReady != NULL,
       "CreateEventW failed: %lu\n",
       GetLastError());
    if (WorkerReady == NULL)
        return;

    StopWorker = 0;
    WorkerProgress = 0;
    Thread = CreateThread(NULL,
                          0,
                          ContextWorker,
                          NULL,
                          CREATE_SUSPENDED,
                          &ThreadId);
    ok(Thread != NULL,
       "CreateThread failed: %lu\n",
       GetLastError());
    if (Thread == NULL)
    {
        CloseHandle(WorkerReady);
        return;
    }

    Success = CaptureContext(Thread, &Before);
    ok(Success,
       "GetThreadContext before first user entry failed: %lu\n",
       GetLastError());
    if (Success)
    {
        CheckUserContext(&Before, "initial", 0);
        Success = SetThreadContext(Thread, &Before);
        ok(Success,
           "SetThreadContext before first user entry failed: %lu\n",
           GetLastError());

        Success = CaptureContext(Thread, &After);
        ok(Success,
           "second initial GetThreadContext failed: %lu\n",
           GetLastError());
        if (Success)
        {
            CheckUserContext(&After, "initial-after-set", 0);
            ok(After.Pc == Before.Pc,
               "initial PC changed from %I64x to %I64x\n",
               Before.Pc,
               After.Pc);
            ok(After.Sp == Before.Sp,
               "initial SP changed from %I64x to %I64x\n",
               Before.Sp,
               After.Sp);
        }
    }

    Result = ResumeThread(Thread);
    ok(Result == 1,
       "ResumeThread returned %lu, expected 1\n",
       Result);
    Result = WaitForSingleObject(WorkerReady, 5000);
    ok(Result == WAIT_OBJECT_0,
       "worker did not enter user mode: %lu\n",
       Result);

    if (Result == WAIT_OBJECT_0)
    {
        for (Round = 0; Round < CONTEXT_ROUNDS; Round++)
        {
            Result = SuspendThread(Thread);
            ok(Result != (DWORD)-1,
               "SuspendThread round %lu failed: %lu\n",
               Round,
               GetLastError());
            if (Result == (DWORD)-1)
                break;

            Success = CaptureContext(Thread, &Before);
            ok(Success,
               "GetThreadContext round %lu failed: %lu\n",
               Round,
               GetLastError());
            if (Success)
            {
                CheckUserContext(&Before, "running", Round);
                Success = SetThreadContext(Thread, &Before);
                ok(Success,
                   "SetThreadContext round %lu failed: %lu\n",
                   Round,
                   GetLastError());

                Success = CaptureContext(Thread, &After);
                ok(Success,
                   "second GetThreadContext round %lu failed: %lu\n",
                   Round,
                   GetLastError());
                if (Success)
                {
                    CheckUserContext(&After, "running-after-set", Round);
                    ok(After.Pc == Before.Pc,
                       "round %lu PC changed from %I64x to %I64x\n",
                       Round,
                       Before.Pc,
                       After.Pc);
                    ok(After.Sp == Before.Sp,
                       "round %lu SP changed from %I64x to %I64x\n",
                       Round,
                       Before.Sp,
                       After.Sp);
                }
            }

            Result = ResumeThread(Thread);
            ok(Result != (DWORD)-1,
               "ResumeThread round %lu failed: %lu\n",
               Round,
               GetLastError());
            if (Result == (DWORD)-1)
                break;
        }
    }

    InterlockedExchange(&StopWorker, 1);
    Result = WaitForSingleObject(Thread, 5000);
    ok(Result == WAIT_OBJECT_0,
       "worker did not exit: %lu\n",
       Result);
    if (Result == WAIT_OBJECT_0)
    {
        DWORD ExitCode;

        Success = GetExitCodeThread(Thread, &ExitCode);
        ok(Success,
           "GetExitCodeThread failed: %lu\n",
           GetLastError());
        if (Success)
            ok(ExitCode == 0x1234,
               "worker exit code was %lx\n",
               ExitCode);
    }
    ok(WorkerProgress != 0,
       "worker made no user-mode progress\n");

    trace("exercised the initial frame and %lu running ARM64 context rounds\n",
          Round);
    CloseHandle(Thread);
    CloseHandle(WorkerReady);
}

#else

START_TEST(Arm64ThreadContext)
{
    skip("ARM64-specific context test\n");
}

#endif
