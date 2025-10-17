#include "k32_vista.h"
#include "../include/base_x.h"

#define NDEBUG
#include <debug.h>

NTSYSAPI
NTSTATUS
NTAPI
NtCreateEvent(PHANDLE EventHandle,
              ACCESS_MASK DesiredAccess,
              POBJECT_ATTRIBUTES ObjectAttributes,
              EVENT_TYPE EventType,
              BOOLEAN InitialState);

NTSYSAPI
NTSTATUS
NTAPI
NtCreateMutant(PHANDLE MutantHandle,
               ACCESS_MASK DesiredAccess,
               POBJECT_ATTRIBUTES ObjectAttributes,
               BOOLEAN InitialOwner);

NTSYSAPI
NTSTATUS
NTAPI
NtCreateTimer(PHANDLE TimerHandle,
              ACCESS_MASK DesiredAccess,
              POBJECT_ATTRIBUTES ObjectAttributes,
              TIMER_TYPE TimerType);

NTSYSAPI
NTSTATUS
NTAPI
NtQuerySystemInformation(
    _In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
    _Out_writes_bytes_to_opt_(SystemInformationLength, *ReturnLength) PVOID SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Out_opt_ PULONG ReturnLength);

VOID
WINAPI
AcquireSRWLockExclusive(PSRWLOCK Lock)
{
    RtlAcquireSRWLockExclusive((PRTL_SRWLOCK)Lock);
}

VOID
WINAPI
AcquireSRWLockShared(PSRWLOCK Lock)
{
    RtlAcquireSRWLockShared((PRTL_SRWLOCK)Lock);
}

VOID
WINAPI
InitializeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    RtlInitializeConditionVariable((PRTL_CONDITION_VARIABLE)ConditionVariable);
}

VOID
WINAPI
InitializeSRWLock(PSRWLOCK Lock)
{
    RtlInitializeSRWLock((PRTL_SRWLOCK)Lock);
}

VOID
WINAPI
ReleaseSRWLockExclusive(PSRWLOCK Lock)
{
    RtlReleaseSRWLockExclusive((PRTL_SRWLOCK)Lock);
}

VOID
WINAPI
ReleaseSRWLockShared(PSRWLOCK Lock)
{
    RtlReleaseSRWLockShared((PRTL_SRWLOCK)Lock);
}

FORCEINLINE
PLARGE_INTEGER
GetNtTimeout(PLARGE_INTEGER Time, DWORD Timeout)
{
    if (Timeout == INFINITE) return NULL;
    Time->QuadPart = (ULONGLONG)Timeout * -10000;
    return Time;
}

BOOL
WINAPI
SleepConditionVariableCS(PCONDITION_VARIABLE ConditionVariable, PCRITICAL_SECTION CriticalSection, DWORD Timeout)
{
    NTSTATUS Status;
    LARGE_INTEGER Time;

    Status = RtlSleepConditionVariableCS(ConditionVariable, (PRTL_CRITICAL_SECTION)CriticalSection, GetNtTimeout(&Time, Timeout));
    if (!NT_SUCCESS(Status) || Status == STATUS_TIMEOUT)
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
SleepConditionVariableSRW(PCONDITION_VARIABLE ConditionVariable, PSRWLOCK Lock, DWORD Timeout, ULONG Flags)
{
    NTSTATUS Status;
    LARGE_INTEGER Time;

    Status = RtlSleepConditionVariableSRW(ConditionVariable, Lock, GetNtTimeout(&Time, Timeout), Flags);
    if (!NT_SUCCESS(Status) || Status == STATUS_TIMEOUT)
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }
    return TRUE;
}

VOID
WINAPI
WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    RtlWakeAllConditionVariable((PRTL_CONDITION_VARIABLE)ConditionVariable);
}

VOID
WINAPI
WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    RtlWakeConditionVariable((PRTL_CONDITION_VARIABLE)ConditionVariable);
}


/*
 * @implemented
 */
BOOL
WINAPI
QueryIdleProcessorCycleTime(PULONG BufferLength, PULONGLONG CycleTimes)
{
    NTSTATUS Status;
    ULONG ReturnLength = 0;
    PVOID Buffer;

    if (!BufferLength)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (*BufferLength && !CycleTimes)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Buffer = *BufferLength ? (PVOID)CycleTimes : NULL;

    Status = NtQuerySystemInformation(SystemProcessorIdleCycleTimeInformation,
                                      Buffer,
                                      *BufferLength,
                                      &ReturnLength);

    *BufferLength = ReturnLength;

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
* @implemented
*/
BOOL WINAPI InitializeCriticalSectionEx(OUT LPCRITICAL_SECTION lpCriticalSection,
                                        IN DWORD dwSpinCount,
                                        IN DWORD flags)
{
    NTSTATUS Status;

    /* FIXME: Flags ignored */

    /* Initialize the critical section */
    Status = RtlInitializeCriticalSectionAndSpinCount(
        (PRTL_CRITICAL_SECTION)lpCriticalSection,
        dwSpinCount);
    if (!NT_SUCCESS(Status))
    {
        /* Set failure code */
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    /* Success */
    return TRUE;
}


/*
 * @implemented
 */
HANDLE
WINAPI
CreateEventExA(LPSECURITY_ATTRIBUTES lpEventAttributes OPTIONAL,
               LPCSTR lpName OPTIONAL,
               DWORD dwFlags,
               DWORD dwDesiredAccess)
{
    PUNICODE_STRING NameU;

    if (!lpName)
        return CreateEventExW(lpEventAttributes, NULL, dwFlags, dwDesiredAccess);

    NameU = K32VistaAnsiToStaticUnicode(lpName);
    if (!NameU)
        return NULL;

    return CreateEventExW(lpEventAttributes, NameU->Buffer, dwFlags, dwDesiredAccess);
}


/*
 * @implemented
 */
HANDLE
WINAPI
CreateEventExW(LPSECURITY_ATTRIBUTES lpEventAttributes OPTIONAL,
               LPCWSTR lpName OPTIONAL,
               DWORD dwFlags,
               DWORD dwDesiredAccess)
{
    BOOLEAN InitialState;
    EVENT_TYPE EventType;

    if (dwFlags & ~(CREATE_EVENT_MANUAL_RESET | CREATE_EVENT_INITIAL_SET))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    if (!dwDesiredAccess)
        dwDesiredAccess = EVENT_ALL_ACCESS;

    EventType = (dwFlags & CREATE_EVENT_MANUAL_RESET) ? NotificationEvent
                                                      : SynchronizationEvent;
    InitialState = (dwFlags & CREATE_EVENT_INITIAL_SET) != 0;

    CreateNtObjectFromWin32Api(Event,
                               Event,
                               dwDesiredAccess,
                               lpEventAttributes,
                               lpName,
                               EventType,
                               InitialState);
}


/*
 * @implemented
 */
HANDLE
WINAPI
CreateMutexExA(LPSECURITY_ATTRIBUTES lpMutexAttributes OPTIONAL,
               LPCSTR lpName OPTIONAL,
               DWORD dwFlags,
               DWORD dwDesiredAccess)
{
    PUNICODE_STRING NameU;

    if (!lpName)
        return CreateMutexExW(lpMutexAttributes, NULL, dwFlags, dwDesiredAccess);

    NameU = K32VistaAnsiToStaticUnicode(lpName);
    if (!NameU)
        return NULL;

    return CreateMutexExW(lpMutexAttributes, NameU->Buffer, dwFlags, dwDesiredAccess);
}


/*
 * @implemented
 */
HANDLE
WINAPI
CreateMutexExW(LPSECURITY_ATTRIBUTES lpMutexAttributes OPTIONAL,
               LPCWSTR lpName OPTIONAL,
               DWORD dwFlags,
               DWORD dwDesiredAccess)
{
    if (dwFlags & ~CREATE_MUTEX_INITIAL_OWNER)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    if (!dwDesiredAccess)
        dwDesiredAccess = MUTEX_ALL_ACCESS;

    CreateNtObjectFromWin32Api(Mutex,
                               Mutant,
                               dwDesiredAccess,
                               lpMutexAttributes,
                               lpName,
                               (dwFlags & CREATE_MUTEX_INITIAL_OWNER) != 0);
}


/*
 * @implemented
 */
HANDLE
WINAPI
CreateWaitableTimerExA(LPSECURITY_ATTRIBUTES lpTimerAttributes OPTIONAL,
                       LPCSTR lpTimerName OPTIONAL,
                       DWORD dwFlags,
                       DWORD dwDesiredAccess)
{
    PUNICODE_STRING NameU;

    if (!lpTimerName)
        return CreateWaitableTimerExW(lpTimerAttributes, NULL, dwFlags, dwDesiredAccess);

    NameU = K32VistaAnsiToStaticUnicode(lpTimerName);
    if (!NameU)
        return NULL;

    return CreateWaitableTimerExW(lpTimerAttributes, NameU->Buffer, dwFlags, dwDesiredAccess);
}


/*
 * @implemented
 */
HANDLE
WINAPI
CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES lpTimerAttributes OPTIONAL,
                       LPCWSTR lpTimerName OPTIONAL,
                       DWORD dwFlags,
                       DWORD dwDesiredAccess)
{
    if (dwFlags & ~CREATE_WAITABLE_TIMER_MANUAL_RESET)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    if (!dwDesiredAccess)
        dwDesiredAccess = TIMER_ALL_ACCESS;

    CreateNtObjectFromWin32Api(WaitableTimer,
                               Timer,
                               dwDesiredAccess,
                               lpTimerAttributes,
                               lpTimerName,
                               (dwFlags & CREATE_WAITABLE_TIMER_MANUAL_RESET) ? NotificationTimer
                                                                               : SynchronizationTimer);
}
