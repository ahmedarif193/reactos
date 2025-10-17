#include "k32_vista.h"

#include <threadpoolapiset.h>

NTSTATUS
NTAPI
TpAllocCleanupGroup(TP_CLEANUP_GROUP** Group);

typedef VOID (NTAPI *PTP_IO_CALLBACK_INTERNAL)(TP_CALLBACK_INSTANCE*, PVOID, PVOID, IO_STATUS_BLOCK*, TP_IO*);

NTSTATUS
NTAPI
TpAllocIoCompletion(TP_IO** Io,
                    HANDLE FileHandle,
                    PTP_IO_CALLBACK_INTERNAL Callback,
                    PVOID Context,
                    PTP_CALLBACK_ENVIRON CallbackEnvironment);

NTSTATUS
NTAPI
TpAllocPool(TP_POOL** Pool,
            PVOID Reserved);

NTSTATUS
NTAPI
TpAllocTimer(TP_TIMER** Timer,
             PTP_TIMER_CALLBACK Callback,
             PVOID Context,
             PTP_CALLBACK_ENVIRON CallbackEnvironment);

NTSTATUS
NTAPI
TpAllocWait(TP_WAIT** Wait,
            PTP_WAIT_CALLBACK Callback,
            PVOID Context,
            PTP_CALLBACK_ENVIRON CallbackEnvironment);

NTSTATUS
NTAPI
TpAllocWork(TP_WORK** Work,
            PTP_WORK_CALLBACK Callback,
            PVOID Context,
            PTP_CALLBACK_ENVIRON CallbackEnvironment);

void
NTAPI
TpCancelAsyncIoOperation(TP_IO* Io);

void
NTAPI
TpCallbackLeaveCriticalSectionOnCompletion(TP_CALLBACK_INSTANCE* Instance,
                                           RTL_CRITICAL_SECTION* CriticalSection);

NTSTATUS
NTAPI
TpCallbackMayRunLong(TP_CALLBACK_INSTANCE* Instance);

void
NTAPI
TpCallbackReleaseMutexOnCompletion(TP_CALLBACK_INSTANCE* Instance,
                                   HANDLE MutexHandle);

void
NTAPI
TpCallbackReleaseSemaphoreOnCompletion(TP_CALLBACK_INSTANCE* Instance,
                                       HANDLE SemaphoreHandle,
                                       DWORD ReleaseCount);

void
NTAPI
TpCallbackSetEventOnCompletion(TP_CALLBACK_INSTANCE* Instance,
                               HANDLE EventHandle);

void
NTAPI
TpCallbackUnloadDllOnCompletion(TP_CALLBACK_INSTANCE* Instance,
                                HMODULE ModuleHandle);

void
NTAPI
TpDisassociateCallback(TP_CALLBACK_INSTANCE* Instance);

BOOL
NTAPI
TpIsTimerSet(TP_TIMER* Timer);

void
NTAPI
TpPostWork(TP_WORK* Work);

void
NTAPI
TpReleaseCleanupGroup(TP_CLEANUP_GROUP* Group);

void
NTAPI
TpReleaseCleanupGroupMembers(TP_CLEANUP_GROUP* Group,
                             BOOL CancelPendingCallbacks,
                             PVOID CleanupContext);

void
NTAPI
TpReleaseIoCompletion(TP_IO* Io);

void
NTAPI
TpReleasePool(TP_POOL* Pool);

void
NTAPI
TpReleaseTimer(TP_TIMER* Timer);

void
NTAPI
TpReleaseWait(TP_WAIT* Wait);

void
NTAPI
TpReleaseWork(TP_WORK* Work);

BOOL
NTAPI
TpSetPoolMinThreads(TP_POOL* Pool,
                    DWORD Minimum);

void
NTAPI
TpSetPoolMaxThreads(TP_POOL* Pool,
                    DWORD Maximum);

void
NTAPI
TpSetTimer(TP_TIMER* Timer,
           LARGE_INTEGER* DueTime,
           LONG Period,
           LONG WindowLength);

void
NTAPI
TpSetWait(TP_WAIT* Wait,
          HANDLE Handle,
          LARGE_INTEGER* DueTime);

NTSTATUS
NTAPI
TpSimpleTryPost(PTP_SIMPLE_CALLBACK Callback,
                PVOID Context,
                PTP_CALLBACK_ENVIRON CallbackEnvironment);

void
NTAPI
TpStartAsyncIoOperation(TP_IO* Io);

void
NTAPI
TpWaitForIoCompletion(TP_IO* Io,
                      BOOL CancelPendingCallbacks);

void
NTAPI
TpWaitForTimer(TP_TIMER* Timer,
               BOOL CancelPendingCallbacks);

void
NTAPI
TpWaitForWait(TP_WAIT* Wait,
              BOOL CancelPendingCallbacks);

void
NTAPI
TpWaitForWork(TP_WORK* Work,
              BOOL CancelPendingCallbacks);

static BOOL
K32VistaSetNtStatus(NTSTATUS Status)
{
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

static VOID WINAPI
K32VistaIoShimCallback(PTP_CALLBACK_INSTANCE Instance,
                       PVOID Context,
                       PVOID Overlapped,
                       IO_STATUS_BLOCK* IoStatus,
                       PTP_IO Io)
{
    PTP_WIN32_IO_CALLBACK Original = *(PTP_WIN32_IO_CALLBACK*)Io;
    Original(Instance,
             Context,
             Overlapped,
             RtlNtStatusToDosError(IoStatus->Status),
             IoStatus->Information,
             Io);
}

PTP_POOL
WINAPI
CreateThreadpool(PVOID Reserved)
{
    TP_POOL* Pool;

    if (!K32VistaSetNtStatus(TpAllocPool(&Pool, Reserved)))
        return NULL;

    return Pool;
}

VOID
WINAPI
CloseThreadpool(PTP_POOL Pool)
{
    TpReleasePool(Pool);
}

PTP_CLEANUP_GROUP
WINAPI
CreateThreadpoolCleanupGroup(VOID)
{
    TP_CLEANUP_GROUP* Group;

    if (!K32VistaSetNtStatus(TpAllocCleanupGroup(&Group)))
        return NULL;

    return Group;
}

VOID
WINAPI
CloseThreadpoolCleanupGroup(PTP_CLEANUP_GROUP Group)
{
    TpReleaseCleanupGroup(Group);
}

VOID
WINAPI
CloseThreadpoolCleanupGroupMembers(PTP_CLEANUP_GROUP Group,
                                   BOOL CancelPendingCallbacks,
                                   PVOID CleanupContext)
{
    TpReleaseCleanupGroupMembers(Group, CancelPendingCallbacks, CleanupContext);
}

PTP_IO
WINAPI
CreateThreadpoolIo(HANDLE FileHandle,
                   PTP_WIN32_IO_CALLBACK Callback,
                   PVOID Context,
                   PTP_CALLBACK_ENVIRON CallbackEnvironment)
{
    TP_IO* Io;

    if (!K32VistaSetNtStatus(TpAllocIoCompletion(&Io,
                                                 FileHandle,
                                                 K32VistaIoShimCallback,
                                                 Context,
                                                 CallbackEnvironment)))
        return NULL;

    *(PTP_WIN32_IO_CALLBACK*)Io = Callback;
    return Io;
}

VOID
WINAPI
CloseThreadpoolIo(PTP_IO Io)
{
    TpReleaseIoCompletion(Io);
}

VOID
WINAPI
StartThreadpoolIo(PTP_IO Io)
{
    TpStartAsyncIoOperation(Io);
}

VOID
WINAPI
CancelThreadpoolIo(PTP_IO Io)
{
    TpCancelAsyncIoOperation(Io);
}

VOID
WINAPI
WaitForThreadpoolIoCallbacks(PTP_IO Io,
                             BOOL CancelPendingCallbacks)
{
    TpWaitForIoCompletion(Io, CancelPendingCallbacks);
}

PTP_TIMER
WINAPI
CreateThreadpoolTimer(PTP_TIMER_CALLBACK Callback,
                      PVOID Context,
                      PTP_CALLBACK_ENVIRON CallbackEnvironment)
{
    TP_TIMER* Timer;

    if (!K32VistaSetNtStatus(TpAllocTimer(&Timer,
                                          Callback,
                                          Context,
                                          CallbackEnvironment)))
        return NULL;

    return Timer;
}

VOID
WINAPI
CloseThreadpoolTimer(PTP_TIMER Timer)
{
    TpReleaseTimer(Timer);
}

VOID
WINAPI
SetThreadpoolTimer(PTP_TIMER Timer,
                   PFILETIME DueTime,
                   DWORD Period,
                   DWORD WindowLength)
{
    LARGE_INTEGER DueTimeLi;
    LARGE_INTEGER* DueTimePointer = NULL;

    if (DueTime)
    {
        DueTimeLi.u.LowPart = DueTime->dwLowDateTime;
        DueTimeLi.u.HighPart = DueTime->dwHighDateTime;
        DueTimePointer = &DueTimeLi;
    }

    TpSetTimer(Timer, DueTimePointer, Period, WindowLength);
}

BOOL
WINAPI
IsThreadpoolTimerSet(PTP_TIMER Timer)
{
    return TpIsTimerSet(Timer);
}

VOID
WINAPI
WaitForThreadpoolTimerCallbacks(PTP_TIMER Timer,
                                BOOL CancelPendingCallbacks)
{
    TpWaitForTimer(Timer, CancelPendingCallbacks);
}

PTP_WAIT
WINAPI
CreateThreadpoolWait(PTP_WAIT_CALLBACK Callback,
                     PVOID Context,
                     PTP_CALLBACK_ENVIRON CallbackEnvironment)
{
    TP_WAIT* Wait;

    if (!K32VistaSetNtStatus(TpAllocWait(&Wait,
                                         Callback,
                                         Context,
                                         CallbackEnvironment)))
        return NULL;

    return Wait;
}

VOID
WINAPI
CloseThreadpoolWait(PTP_WAIT Wait)
{
    TpReleaseWait(Wait);
}

VOID
WINAPI
SetThreadpoolWait(PTP_WAIT Wait,
                  HANDLE Handle,
                  PFILETIME DueTime)
{
    LARGE_INTEGER DueTimeLi;
    LARGE_INTEGER* DueTimePointer = NULL;

    if (DueTime)
    {
        DueTimeLi.u.LowPart = DueTime->dwLowDateTime;
        DueTimeLi.u.HighPart = DueTime->dwHighDateTime;
        DueTimePointer = &DueTimeLi;
    }

    TpSetWait(Wait, Handle, DueTimePointer);
}

VOID
WINAPI
WaitForThreadpoolWaitCallbacks(PTP_WAIT Wait,
                               BOOL CancelPendingCallbacks)
{
    TpWaitForWait(Wait, CancelPendingCallbacks);
}

PTP_WORK
WINAPI
CreateThreadpoolWork(PTP_WORK_CALLBACK Callback,
                     PVOID Context,
                     PTP_CALLBACK_ENVIRON CallbackEnvironment)
{
    TP_WORK* Work;

    if (!K32VistaSetNtStatus(TpAllocWork(&Work,
                                         Callback,
                                         Context,
                                         CallbackEnvironment)))
        return NULL;

    return Work;
}

VOID
WINAPI
CloseThreadpoolWork(PTP_WORK Work)
{
    TpReleaseWork(Work);
}

VOID
WINAPI
SubmitThreadpoolWork(PTP_WORK Work)
{
    TpPostWork(Work);
}

VOID
WINAPI
WaitForThreadpoolWorkCallbacks(PTP_WORK Work,
                               BOOL CancelPendingCallbacks)
{
    TpWaitForWork(Work, CancelPendingCallbacks);
}

BOOL
WINAPI
SetThreadpoolThreadMinimum(PTP_POOL Pool,
                           DWORD Minimum)
{
    if (!TpSetPoolMinThreads(Pool, Minimum))
        return FALSE;

    return TRUE;
}

VOID
WINAPI
SetThreadpoolThreadMaximum(PTP_POOL Pool,
                           DWORD Maximum)
{
    TpSetPoolMaxThreads(Pool, Maximum);
}

BOOL
WINAPI
TrySubmitThreadpoolCallback(PTP_SIMPLE_CALLBACK Callback,
                            PVOID Context,
                            PTP_CALLBACK_ENVIRON CallbackEnvironment)
{
    return K32VistaSetNtStatus(TpSimpleTryPost(Callback, Context, CallbackEnvironment));
}

BOOL
WINAPI
CallbackMayRunLong(PTP_CALLBACK_INSTANCE Instance)
{
    return K32VistaSetNtStatus(TpCallbackMayRunLong(Instance));
}

VOID
WINAPI
DisassociateCurrentThreadFromCallback(PTP_CALLBACK_INSTANCE Instance)
{
    TpDisassociateCallback(Instance);
}

VOID
WINAPI
FreeLibraryWhenCallbackReturns(PTP_CALLBACK_INSTANCE Instance,
                               HMODULE Module)
{
    TpCallbackUnloadDllOnCompletion(Instance, Module);
}

VOID
WINAPI
LeaveCriticalSectionWhenCallbackReturns(PTP_CALLBACK_INSTANCE Instance,
                                        PCRITICAL_SECTION CriticalSection)
{
    TpCallbackLeaveCriticalSectionOnCompletion(Instance, CriticalSection);
}

VOID
WINAPI
ReleaseMutexWhenCallbackReturns(PTP_CALLBACK_INSTANCE Instance,
                                HANDLE Mutex)
{
    TpCallbackReleaseMutexOnCompletion(Instance, Mutex);
}

VOID
WINAPI
ReleaseSemaphoreWhenCallbackReturns(PTP_CALLBACK_INSTANCE Instance,
                                    HANDLE Semaphore,
                                    DWORD ReleaseCount)
{
    TpCallbackReleaseSemaphoreOnCompletion(Instance, Semaphore, ReleaseCount);
}

VOID
WINAPI
SetEventWhenCallbackReturns(PTP_CALLBACK_INSTANCE Instance,
                            HANDLE EventHandle)
{
    TpCallbackSetEventOnCompletion(Instance, EventHandle);
}
