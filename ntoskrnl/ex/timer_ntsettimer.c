/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         NtSetTimer implementation
 * COPYRIGHT:       Copyright 2024 ReactOS Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

/* Forward declaration from timer.c */
VOID
NTAPI
ExpTimerApcKernelRoutine(IN PKAPC Apc,
                         IN OUT PKNORMAL_ROUTINE* NormalRoutine,
                         IN OUT PVOID* NormalContext,
                         IN OUT PVOID* SystemArgument1,
                         IN OUT PVOID* SystemArguemnt2);

NTSTATUS
NTAPI
NtSetTimer(IN HANDLE TimerHandle,
           IN PLARGE_INTEGER DueTime,
           IN PTIMER_APC_ROUTINE TimerApcRoutine OPTIONAL,
           IN PVOID TimerContext OPTIONAL,
           IN BOOLEAN WakeTimer,
           IN LONG Period OPTIONAL,
           OUT PBOOLEAN PreviousState OPTIONAL)
{
    PETIMER Timer;
    KIRQL OldIrql;
    BOOLEAN State;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETHREAD Thread = PsGetCurrentThread();
    LARGE_INTEGER TimerDueTime;
    PETHREAD TimerThread;
    ULONG DerefsToDo = 1;
    NTSTATUS Status = STATUS_SUCCESS;
    PAGED_CODE();

    /* Check for a valid Period */
    if (Period < 0) return STATUS_INVALID_PARAMETER_6;

    /* Reference the Timer */
    Status = ObReferenceObjectByHandle(TimerHandle,
                                       TIMER_MODIFY_STATE,
                                       ExTimerType,
                                       PreviousMode,
                                       (PVOID*)&Timer,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        /* Lock the Timer */
        KeAcquireSpinLock(&Timer->Lock, &OldIrql);

        /* Cancel the Timer and get the State */
        State = KeCancelTimer(&Timer->KeTimer);

        /* Check if the timer has an APC */
        if (Timer->ApcAssociated)
        {
            /* Get the Thread */
            TimerThread = CONTAINING_RECORD(Timer->TimerApc.Thread,
                                            ETHREAD,
                                            Tcb);

            /* Dereference it */
            ObDereferenceObject(TimerThread);
            DerefsToDo++;
        }

        /* Check if we have a period */
        if (Period)
        {
            /* Set it */
            Timer->Period = Period;
            Timer->KeTimer.Period = Period;
        }

        /* Check if we have a DPC */
        if (TimerApcRoutine)
        {
            /* Initialize the APC */
            KeInitializeApc(&Timer->TimerApc,
                            &Thread->Tcb,
                            CurrentApcEnvironment,
                            ExpTimerApcKernelRoutine,
                            (PKRUNDOWN_ROUTINE)NULL,
                            (PKNORMAL_ROUTINE)TimerApcRoutine,
                            PreviousMode,
                            TimerContext);

            /* Lock the Thread's Active Timer List*/
            KeAcquireSpinLockAtDpcLevel(&Thread->ActiveTimerListLock);

            /* Make sure it's empty */
            if (!Timer->ApcAssociated)
            {
                /* Insert it into the Timer List */
                InsertTailList(&Thread->ActiveTimerListHead,
                               &Timer->ActiveTimerListEntry);
                Timer->ApcAssociated = TRUE;

                /* One less dereference to do */
                DerefsToDo--;
            }

            /* Unlock the list */
            KeReleaseSpinLockFromDpcLevel(&Thread->ActiveTimerListLock);
        }

        /* Enable and set the Timer */
        Timer->KeTimer.Header.Inserted = TRUE;
        Timer->WakeTimerListEntry.Flink = (PVOID)WakeTimer;
        if (DueTime)
        {
            /* Set the Due Time */
            TimerDueTime.QuadPart = DueTime->QuadPart;
        }
        else
        {
            /* No due time */
            TimerDueTime.QuadPart = 0;
        }

        /* Set the Timer */
        KeSetTimerEx(&Timer->KeTimer,
                     TimerDueTime,
                     Period,
                     TimerApcRoutine ? &Timer->TimerDpc : NULL);

        /* Release the timer lock */
        KeReleaseSpinLock(&Timer->Lock, OldIrql);

        /* Dereference if needed */
        if (DerefsToDo) ObDereferenceObject(Timer);

        /* Check if we need to return the State */
        if (PreviousState)
        {
            /* Return the Timer State */
            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    *PreviousState = State;
                }
                _SEH2_EXCEPT(ExSystemExceptionFilter())
                {
                    /* Do nothing */
                    (void)0;
                }
                _SEH2_END;
            }
            else
            {
                *PreviousState = State;
            }
        }
    }

    /* Return to Caller */
    return Status;
}

/* EOF */