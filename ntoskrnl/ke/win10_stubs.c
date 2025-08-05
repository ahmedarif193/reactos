/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Windows 10 Kernel compatibility stubs
 * COPYRIGHT:       Copyright 2024 ReactOS Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

/*
 * @unimplemented
 */
VOID
NTAPI
KeUnwaitThread(
    IN PETHREAD Thread,
    IN NTSTATUS WaitStatus,
    IN KPRIORITY Increment)
{
    PKTHREAD Kthread = &Thread->Tcb;
    
    /* Windows 10 API - wake the thread from its wait state */
    /* For now, just set the thread's wait status and signal it */
    Kthread->WaitStatus = WaitStatus;
    
    /* If thread is waiting, wake it */
    if (Kthread->State == Waiting)
    {
        /* Remove from wait blocks and ready the thread */
        KiUnwaitThread(Kthread, WaitStatus, Increment);
    }
}

/*
 * @unimplemented
 */
ULONG
NTAPI
KeQueryActiveProcessorCount(
    OUT PKAFFINITY ActiveProcessors OPTIONAL)
{
    KAFFINITY Affinity;
    ULONG Count = 0;
    ULONG i;
    
    /* Get the active processor affinity mask */
    Affinity = KeActiveProcessors;
    
    /* Count the number of set bits */
    for (i = 0; i < sizeof(KAFFINITY) * 8; i++)
    {
        if (Affinity & (1ULL << i))
        {
            Count++;
        }
    }
    
    /* Return the affinity mask if requested */
    if (ActiveProcessors)
    {
        *ActiveProcessors = Affinity;
    }
    
    return Count;
}

/* EOF */