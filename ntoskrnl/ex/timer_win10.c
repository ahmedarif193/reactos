/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Windows 10 Timer API compatibility stubs
 * COPYRIGHT:       Copyright 2024 ReactOS Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

/* Forward declaration from timer.c */
NTSTATUS
NTAPI
NtSetTimer(IN HANDLE TimerHandle,
           IN PLARGE_INTEGER DueTime,
           IN PTIMER_APC_ROUTINE TimerApcRoutine OPTIONAL,
           IN PVOID TimerContext OPTIONAL,
           IN BOOLEAN WakeTimer,
           IN LONG Period OPTIONAL,
           OUT PBOOLEAN PreviousState OPTIONAL);

NTSTATUS
NTAPI
NtQueryTimer(IN HANDLE TimerHandle,
             IN TIMER_INFORMATION_CLASS TimerInformationClass,
             OUT PVOID TimerInformation,
             IN ULONG TimerInformationLength,
             OUT PULONG ReturnLength OPTIONAL)
{
    /* Windows 10 compatibility stub */
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
VOID
NTAPI
ExInitializeDeleteTimerParameters(
    IN PEXT_DELETE_PARAMETERS DeleteParameters)
{
    /* Windows 10 API - Initialize delete timer parameters */
    if (DeleteParameters)
    {
        RtlZeroMemory(DeleteParameters, sizeof(EXT_DELETE_PARAMETERS));
        DeleteParameters->Version = 1;
        DeleteParameters->Reserved = 0;
        DeleteParameters->DeleteCallback = NULL;
        DeleteParameters->DeleteContext = NULL;
    }
}

/* EOF */