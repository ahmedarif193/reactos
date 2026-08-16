/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * PURPOSE:           Condition Variable Routines
 * PROGRAMMERS:       Thomas Weidenmueller <w3seek@reactos.com>
 *                    Stephan A. Rüger
 *                    Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl_vista.h>

#define NDEBUG
#include <debug.h>

/*
 * @implemented
 */
VOID
NTAPI
RtlInitializeConditionVariable(
    _Out_ PRTL_CONDITION_VARIABLE ConditionVariable)
{
    ConditionVariable->Ptr = NULL;
}

/*
 * @implemented
 */
VOID
NTAPI
RtlWakeConditionVariable(
    _Inout_ PRTL_CONDITION_VARIABLE ConditionVariable)
{
    InterlockedIncrement((volatile LONG *)&ConditionVariable->Ptr);
    RtlWakeAddressSingle(ConditionVariable);
}

/*
 * @implemented
 */
VOID
NTAPI
RtlWakeAllConditionVariable(
    _Inout_ PRTL_CONDITION_VARIABLE ConditionVariable)
{
    InterlockedIncrement((volatile LONG *)&ConditionVariable->Ptr);
    RtlWakeAddressAll(ConditionVariable);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlSleepConditionVariableCS(
    _Inout_ PRTL_CONDITION_VARIABLE ConditionVariable,
    _Inout_ PRTL_CRITICAL_SECTION CriticalSection,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    ULONG Value = *(volatile ULONG *)&ConditionVariable->Ptr;
    NTSTATUS Status;

    RtlLeaveCriticalSection(CriticalSection);
    Status = RtlWaitOnAddress(&ConditionVariable->Ptr, &Value, sizeof(Value), Timeout);
    RtlEnterCriticalSection(CriticalSection);
    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlSleepConditionVariableSRW(
    _Inout_ PRTL_CONDITION_VARIABLE ConditionVariable,
    _Inout_ PRTL_SRWLOCK SRWLock,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ ULONG Flags)
{
    ULONG Value = *(volatile ULONG *)&ConditionVariable->Ptr;
    NTSTATUS Status;

    if (Flags & RTL_CONDITION_VARIABLE_LOCKMODE_SHARED)
        RtlReleaseSRWLockShared(SRWLock);
    else
        RtlReleaseSRWLockExclusive(SRWLock);

    Status = RtlWaitOnAddress(&ConditionVariable->Ptr, &Value, sizeof(Value), Timeout);

    if (Flags & RTL_CONDITION_VARIABLE_LOCKMODE_SHARED)
        RtlAcquireSRWLockShared(SRWLock);
    else
        RtlAcquireSRWLockExclusive(SRWLock);

    return Status;
}
