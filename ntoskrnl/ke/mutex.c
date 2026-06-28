/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/mutex.c
 * PURPOSE:         Implements the Mutant Dispatcher Object
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
KeInitializeMutant(IN PKMUTANT Mutant,
                   IN BOOLEAN InitialOwner)
{
    PKTHREAD CurrentThread;
    KIRQL OldIrql;

    /* Check if we have an initial owner */
    if (InitialOwner)
    {
        /* We also need to associate a thread */
        CurrentThread = KeGetCurrentThread();
        Mutant->OwnerThread = CurrentThread;

        OldIrql = KeRaiseIrqlToSynchLevel();
        KiAcquireThreadLock(CurrentThread);
        InsertTailList(&CurrentThread->MutantListHead,
                       &Mutant->MutantListEntry);
        KiReleaseThreadLock(CurrentThread);
        KeLowerIrql(OldIrql);
    }
    else
    {
        /* In this case, we don't have an owner yet. Windows 10+ zeroes the
         * mutant list entry of an unowned mutant (it is only linked when owned). */
        Mutant->OwnerThread = NULL;
        Mutant->MutantListEntry.Flink = NULL;
        Mutant->MutantListEntry.Blink = NULL;
    }

    /*
     * Set up the Dispatcher Header. Windows 10+ leaves the Size/Abandoned/
     * DpcActive header bytes zero (older NT kept the dword object size in
     * Header.Size), so zero the leading bytes before setting Type/SignalState.
     */
    RtlZeroMemory(&Mutant->Header, FIELD_OFFSET(DISPATCHER_HEADER, SignalState));
    Mutant->Header.Type = MutantObject;
    Mutant->Header.SignalState = InitialOwner ? 0 : 1;
    InitializeListHead(&(Mutant->Header.WaitListHead));

    /* Initialize the default data */
    Mutant->Abandoned = FALSE;
    Mutant->ApcDisable = 0;
}

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeMutex(IN PKMUTEX Mutex,
                  IN ULONG Level)
{
    /* Set up the Dispatcher Header (Windows 10+ leaves Size/Abandoned/DpcActive
     * zero - see KeInitializeMutant). */
    RtlZeroMemory(&Mutex->Header, FIELD_OFFSET(DISPATCHER_HEADER, SignalState));
    Mutex->Header.Type = MutantObject;
    Mutex->Header.SignalState = 1;
    InitializeListHead(&(Mutex->Header.WaitListHead));

    /* Initialize the default data (an unowned mutex has an empty list entry on
     * Windows 10+). */
    Mutex->OwnerThread = NULL;
    Mutex->MutantListEntry.Flink = NULL;
    Mutex->MutantListEntry.Blink = NULL;
    Mutex->Abandoned = FALSE;
    Mutex->ApcDisable = 1;
}

/*
 * @implemented
 */
LONG
NTAPI
KeReadStateMutant(IN PKMUTANT Mutant)
{
    /* Return the Signal State */
    return Mutant->Header.SignalState;
}

/*
 * @implemented
 */
LONG
NTAPI
KeReleaseMutant(IN PKMUTANT Mutant,
                IN KPRIORITY Increment,
                IN BOOLEAN Abandon,
                IN BOOLEAN Wait)
{
    KIRQL OldIrql;
    LONG PreviousState;
    PKTHREAD CurrentThread = KeGetCurrentThread();
    BOOLEAN EnableApc = FALSE;
    ASSERT_MUTANT(Mutant);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Mutant->Header);

    PreviousState = Mutant->Header.SignalState;

    if (Abandon == FALSE)
    {
        if (Mutant->OwnerThread != CurrentThread)
        {
            KiReleaseDispatcherObject(&Mutant->Header);
            KiExitDispatcher(OldIrql);
            ExRaiseStatus(Mutant->Abandoned ? STATUS_ABANDONED :
                                              STATUS_MUTANT_NOT_OWNED);
        }

        Mutant->Header.SignalState++;
    }
    else
    {
        Mutant->Header.SignalState = 1;
        Mutant->Abandoned = TRUE;
    }

    if (Mutant->Header.SignalState == 1)
    {
        if (PreviousState <= 0)
        {
            KiAcquireThreadLock(CurrentThread);
            RemoveEntryList(&Mutant->MutantListEntry);
            KiReleaseThreadLock(CurrentThread);
            EnableApc = Mutant->ApcDisable;
        }

        Mutant->OwnerThread = NULL;

        if (!IsListEmpty(&Mutant->Header.WaitListHead))
        {
            KiWaitTest(Mutant, Increment);
        }
    }

    KiReleaseDispatcherObject(&Mutant->Header);

    if (Wait == FALSE)
    {
        KiExitDispatcher(OldIrql);
    }
    else
    {
        CurrentThread->WaitNext = TRUE;
        CurrentThread->WaitIrql = OldIrql;
    }

    /* Check if we need to re-enable APCs */
    if (EnableApc) KeLeaveCriticalRegion();

    /* Return the previous state */
    return PreviousState;
}

/*
 * @implemented
 */
LONG
NTAPI
KeReleaseMutex(IN PKMUTEX Mutex,
               IN BOOLEAN Wait)
{
    ASSERT_MUTANT(Mutex);

    /* There's no difference at this level between the two */
    return KeReleaseMutant(Mutex, MUTANT_INCREMENT, FALSE, Wait);
}

/* EOF */
