/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/semphobj.c
 * PURPOSE:         Implements the Semaphore Dispatcher Object
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeSemaphore(IN PKSEMAPHORE Semaphore,
                      IN LONG Count,
                      IN LONG Limit)
{
    /* Simply Initialize the Header */
    Semaphore->Header.Type = SemaphoreObject;
    Semaphore->Header.Size = sizeof(KSEMAPHORE) / sizeof(ULONG);
    Semaphore->Header.SignalState = Count;
    InitializeListHead(&(Semaphore->Header.WaitListHead));

    /* Set the Limit */
    Semaphore->Limit = Limit;
}

/*
 * @implemented
 */
LONG
NTAPI
KeReadStateSemaphore(IN PKSEMAPHORE Semaphore)
{
    ASSERT_SEMAPHORE(Semaphore);

    /* Just return the Signal State */
    return Semaphore->Header.SignalState;
}

/*
 * @implemented
 */
LONG
NTAPI
KeReleaseSemaphore(IN PKSEMAPHORE Semaphore,
                   IN KPRIORITY Increment,
                   IN LONG Adjustment,
                   IN BOOLEAN Wait)
{
    LONG InitialState, State;
    KIRQL OldIrql;
    PKTHREAD CurrentThread;
    ASSERT_SEMAPHORE(Semaphore);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Semaphore->Header);

    InitialState = Semaphore->Header.SignalState;
    State = InitialState + Adjustment;

    if ((Semaphore->Limit < State) || (InitialState > State))
    {
        KiReleaseDispatcherObject(&Semaphore->Header);
        KiExitDispatcher(OldIrql);
        ExRaiseStatus(STATUS_SEMAPHORE_LIMIT_EXCEEDED);
    }

    Semaphore->Header.SignalState = State;

    if (!(InitialState) && !(IsListEmpty(&Semaphore->Header.WaitListHead)))
    {
        KiWaitTest(Semaphore, Increment);
    }

    KiReleaseDispatcherObject(&Semaphore->Header);

    if (Wait == FALSE)
    {
        KiExitDispatcher(OldIrql);
    }
    else
    {
        CurrentThread = KeGetCurrentThread();
        CurrentThread->WaitNext = TRUE;
        CurrentThread->WaitIrql = OldIrql;
    }

    return InitialState;
}

/* EOF */
