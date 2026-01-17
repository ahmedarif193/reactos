/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS kernel
 * PURPOSE:     kernel memory management functions
 * PROGRAMMERS: David Welch <welch@cwcom.net>
 *              Cameron Gutman <cameron.gutman@reactos.org>
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "ARM3/miarm.h"


/* TYPES ********************************************************************/
typedef struct _MM_ALLOCATION_REQUEST
{
    PFN_NUMBER Page;
    LIST_ENTRY ListEntry;
    KEVENT Event;
}
MM_ALLOCATION_REQUEST, *PMM_ALLOCATION_REQUEST;
/* GLOBALS ******************************************************************/

MM_MEMORY_CONSUMER MiMemoryConsumers[MC_MAXIMUM];
static ULONG MiMinimumAvailablePages;
static ULONG MiMinimumPagesPerRun;
static CLIENT_ID MiBalancerThreadId;
static HANDLE MiBalancerThreadHandle = NULL;
static KEVENT MiBalancerEvent;
static BOOLEAN MiBalancerInitialized;
static KEVENT MiBalancerDoneEvent;
static KTIMER MiBalancerTimer;

static LONG PageOutThreadActive;

/* FUNCTIONS ****************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
MmInitializeBalancer(ULONG NrAvailablePages, ULONG NrSystemPages)
{
    memset(MiMemoryConsumers, 0, sizeof(MiMemoryConsumers));

    /* Set up targets. */
    MiMinimumAvailablePages = 256;
    MiMinimumPagesPerRun = 256;
    MiMemoryConsumers[MC_USER].PagesTarget = NrAvailablePages / 2;
}

CODE_SEG("INIT")
VOID
NTAPI
MmInitializeMemoryConsumer(
    ULONG Consumer,
    NTSTATUS (*Trim)(ULONG Target, ULONG Priority, PULONG NrFreed))
{
    MiMemoryConsumers[Consumer].Trim = Trim;
}

VOID
NTAPI
MiZeroPhysicalPage(
    IN PFN_NUMBER PageFrameIndex
);

NTSTATUS
NTAPI
MmReleasePageMemoryConsumer(ULONG Consumer, PFN_NUMBER Page)
{
    KIRQL OldIrql;

    if (Page == 0)
    {
        DPRINT1("Tried to release page zero.\n");
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    (void)InterlockedDecrementUL(&MiMemoryConsumers[Consumer].PagesUsed);
    UpdateTotalCommittedPages(-1);

    OldIrql = MiAcquirePfnLock();

    MmDereferencePage(Page);

    MiReleasePfnLock(OldIrql);

    return(STATUS_SUCCESS);
}

ULONG
NTAPI
MiTrimMemoryConsumer(ULONG Consumer, ULONG InitialTarget)
{
    ULONG Target = InitialTarget;
    ULONG NrFreedPages = 0;
    NTSTATUS Status;

    /* Make sure we can trim this consumer */
    if (!MiMemoryConsumers[Consumer].Trim)
    {
        /* Return the unmodified initial target */
        return InitialTarget;
    }

    if (MmAvailablePages < MiMinimumAvailablePages)
    {
        /* Global page limit exceeded */
        Target = (ULONG)max(Target, MiMinimumAvailablePages - MmAvailablePages);
    }
    else if (MiMemoryConsumers[Consumer].PagesUsed > MiMemoryConsumers[Consumer].PagesTarget)
    {
        /* Consumer page limit exceeded */
        Target = max(Target, MiMemoryConsumers[Consumer].PagesUsed - MiMemoryConsumers[Consumer].PagesTarget);
    }

    if (Target)
    {
        /* Now swap the pages out */
        Status = MiMemoryConsumers[Consumer].Trim(Target, MmAvailablePages < MiMinimumAvailablePages, &NrFreedPages);

        DPRINT("Trimming consumer %lu: Freed %lu pages with a target of %lu pages\n", Consumer, NrFreedPages, Target);

        if (!NT_SUCCESS(Status))
        {
            KeBugCheck(MEMORY_MANAGEMENT);
        }
    }

    /* Return the page count needed to be freed to meet the initial target */
    return (InitialTarget > NrFreedPages) ? (InitialTarget - NrFreedPages) : 0;
}

NTSTATUS
MmTrimUserMemory(ULONG Target, ULONG Priority, PULONG NrFreedPages)
{
    PFN_NUMBER FirstPage, CurrentPage;
    NTSTATUS Status;

    (*NrFreedPages) = 0;

    DPRINT("MM BALANCER: %s\n", Priority ? "Paging out!" : "Removing access bit!");

    FirstPage = MmGetLRUFirstUserPage();
    CurrentPage = FirstPage;
    while (CurrentPage != 0 && Target > 0)
    {
        if (Priority)
        {
            Status = MmPageOutPhysicalAddress(CurrentPage);
            if (NT_SUCCESS(Status))
            {
                DPRINT("Succeeded\n");
                Target--;
                (*NrFreedPages)++;
                if (CurrentPage == FirstPage)
                {
                    FirstPage = 0;
                }
            }
        }
        else
        {
            /* When not paging-out agressively, just reset the accessed bit */
            PEPROCESS Process = NULL;
            PVOID Address = NULL;
            BOOLEAN Accessed = FALSE;

            /*
             * We have a lock-ordering problem here. We cant lock the PFN DB before the Process address space.
             * So we must use circonvoluted loops.
             * Well...
             */
            while (TRUE)
            {
                KAPC_STATE ApcState;
                KIRQL OldIrql = MiAcquirePfnLock();
                PMM_RMAP_ENTRY Entry = MmGetRmapListHeadPage(CurrentPage);
                while (Entry)
                {
                    if (RMAP_IS_SEGMENT(Entry->Address))
                    {
                        Entry = Entry->Next;
                        continue;
                    }

                    /* Check that we didn't treat this entry before */
                    if (Entry->Address < Address)
                    {
                        Entry = Entry->Next;
                        continue;
                    }

                    if ((Entry->Address == Address) && (Entry->Process <= Process))
                    {
                        Entry = Entry->Next;
                        continue;
                    }

                    break;
                }

                if (!Entry)
                {
                    MiReleasePfnLock(OldIrql);
                    break;
                }

                Process = Entry->Process;
                Address = Entry->Address;

                ObReferenceObject(Process);

                if (!ExAcquireRundownProtection(&Process->RundownProtect))
                {
                    ObDereferenceObject(Process);
                    MiReleasePfnLock(OldIrql);
                    continue;
                }

                MiReleasePfnLock(OldIrql);

                KeStackAttachProcess(&Process->Pcb, &ApcState);
                MiLockProcessWorkingSet(Process, PsGetCurrentThread());

                /* Be sure this is still valid. */
                if (MmIsAddressValid(Address))
                {
                    PMMPTE Pte = MiAddressToPte(Address);
                    Accessed = Accessed || Pte->u.Hard.Accessed;
                    Pte->u.Hard.Accessed = 0;

                    /* There is no need to invalidate, the balancer thread is never on a user process */
                    //KeInvalidateTlbEntry(Address);
                }

                MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());

                KeUnstackDetachProcess(&ApcState);
                ExReleaseRundownProtection(&Process->RundownProtect);
                ObDereferenceObject(Process);
            }

            if (!Accessed)
            {
                /* Nobody accessed this page since the last time we check. Time to clean up */

                Status = MmPageOutPhysicalAddress(CurrentPage);
                if (NT_SUCCESS(Status))
                {
                    if (CurrentPage == FirstPage)
                    {
                        FirstPage = 0;
                    }
                }
                // DPRINT1("Paged-out one page: %s\n", NT_SUCCESS(Status) ? "Yes" : "No");
            }

            /* Done for this page. */
            Target--;
        }

        CurrentPage = MmGetLRUNextUserPage(CurrentPage, TRUE);
        if (FirstPage == 0)
        {
            FirstPage = CurrentPage;
        }
        else if (CurrentPage == FirstPage)
        {
            DPRINT1("We are back at the start, abort!\n");
            return STATUS_SUCCESS;
        }
    }

    if (CurrentPage)
    {
        KIRQL OldIrql = MiAcquirePfnLock();
        MmDereferencePage(CurrentPage);
        MiReleasePfnLock(OldIrql);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
MmRebalanceMemoryConsumers(VOID)
{
    /* ARM64: Use acquire barrier when checking initialization flag to ensure we see
     * the initialized event structures. This pairs with the release barrier in
     * MiInitBalancerThread.
     */
#if defined(_M_ARM64) || defined(__aarch64__)
    __dmb(_ARM64_BARRIER_ISH);  /* Data Memory Barrier - Inner Shareable */
#endif
    if (!MiBalancerInitialized) return;
#if defined(_M_ARM64) || defined(__aarch64__)
    __dmb(_ARM64_BARRIER_ISH);  /* Ensure all event initializations are visible */
#endif

    if (InterlockedCompareExchange(&PageOutThreadActive, 1, 0) == 0)
    {
        KeSetEvent(&MiBalancerEvent, IO_NO_INCREMENT, FALSE);
    }
}

VOID
NTAPI
MmRebalanceMemoryConsumersAndWait(VOID)
{
    /* ARM64: Use acquire barrier when checking initialization flag to ensure we see
     * the initialized event structures. This pairs with the release barrier in
     * MiInitBalancerThread.
     */
#if defined(_M_ARM64) || defined(__aarch64__)
    __dmb(_ARM64_BARRIER_ISH);  /* Data Memory Barrier - Inner Shareable */
#endif
    if (!MiBalancerInitialized)
    {
        /* Balancer thread not ready yet; nothing to do */
        return;
    }
#if defined(_M_ARM64) || defined(__aarch64__)
    __dmb(_ARM64_BARRIER_ISH);  /* Ensure all event initializations are visible */
#endif

    ASSERT(PsGetCurrentProcess()->AddressCreationLock.Owner != KeGetCurrentThread());
    ASSERT(!MM_ANY_WS_LOCK_HELD(PsGetCurrentThread()));
    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    KeResetEvent(&MiBalancerDoneEvent);
    MmRebalanceMemoryConsumers();
    KeWaitForSingleObject(&MiBalancerDoneEvent, Executive, KernelMode, FALSE, NULL);
}

NTSTATUS
NTAPI
MmRequestPageMemoryConsumer(ULONG Consumer, BOOLEAN CanWait,
                            PPFN_NUMBER AllocatedPage)
{
    PFN_NUMBER Page;

    /* Delay some requests for the Memory Manager to recover pages (CORE-17624).
     * FIXME: This is suboptimal.
     */
    static INT i = 0;
    static LARGE_INTEGER TinyTime = {{-1L, -1L}};
    if (i++ >= 100)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &TinyTime);
        i = 0;
    }

    /*
     * Actually allocate the page.
     */
    Page = MmAllocPage(Consumer);
    if (Page == 0)
    {
        *AllocatedPage = 0;
        return STATUS_NO_MEMORY;
    }
    *AllocatedPage = Page;

    /* Update the target */
    InterlockedIncrementUL(&MiMemoryConsumers[Consumer].PagesUsed);
    UpdateTotalCommittedPages(1);

    return(STATUS_SUCCESS);
}

VOID
CcRosTrimCache(
    _In_ ULONG Target,
    _Out_ PULONG NrFreed);

VOID
NTAPI
MiBalancerThread(PVOID Unused)
{
    PVOID WaitObjects[2];
    NTSTATUS Status;

    WaitObjects[0] = &MiBalancerEvent;
    WaitObjects[1] = &MiBalancerTimer;

    while (TRUE)
    {
        KeSetEvent(&MiBalancerDoneEvent, IO_NO_INCREMENT, FALSE);
        Status = KeWaitForMultipleObjects(_countof(WaitObjects),
                                          WaitObjects,
                                          WaitAny,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL,
                                          NULL);

        if (Status == STATUS_WAIT_0 || Status == STATUS_WAIT_1)
        {
            ULONG InitialTarget = 0;
            ULONG Target;
            ULONG NrFreedPages;

            do
            {
                ULONG OldTarget = InitialTarget;

                /* Trim each consumer */
                for (ULONG i = 0; i < MC_MAXIMUM; i++)
                {
                    InitialTarget = MiTrimMemoryConsumer(i, InitialTarget);
                }

                /* Trim cache */
                {
                    LONGLONG Delta = (LONGLONG)MiMinimumAvailablePages - (LONGLONG)MmAvailablePages;
                    PFN_NUMBER DeltaAbs = (PFN_NUMBER)((Delta >= 0) ? Delta : -Delta);
                    Target = max(InitialTarget, DeltaAbs);
                }
                if (Target)
                {
                    CcRosTrimCache(Target, &NrFreedPages);
                    InitialTarget -= min(NrFreedPages, InitialTarget);
                }

                /* No pages left to swap! */
                if (InitialTarget != 0 &&
                    InitialTarget == OldTarget)
                {
                    /* Game over */
                    KeBugCheck(NO_PAGES_AVAILABLE);
                }
            }
            while (InitialTarget != 0);

            if (Status == STATUS_WAIT_0)
            {
                LONG Active = InterlockedExchange(&PageOutThreadActive, 0);
                ASSERT(Active == 1);
                DBG_UNREFERENCED_LOCAL_VARIABLE(Active);
            }
        }
        else
        {
            DPRINT1("KeWaitForMultipleObjects failed, status = %x\n", Status);
            KeBugCheck(MEMORY_MANAGEMENT);
        }
    }
}

CODE_SEG("INIT")
VOID
NTAPI
MiInitBalancerThread(VOID)
{
    KPRIORITY Priority;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;

    DPRINT1("[MM] MiInitBalancerThread: Entry\n");
    DPRINT1("[MM] MiInitBalancerThread: &MiBalancerEvent=%p &MiBalancerDoneEvent=%p &MiBalancerTimer=%p\n",
            &MiBalancerEvent, &MiBalancerDoneEvent, &MiBalancerTimer);
    DPRINT1("[MM] MiInitBalancerThread: &MiBalancerThreadHandle=%p &MiBalancerThreadId=%p\n",
            &MiBalancerThreadHandle, &MiBalancerThreadId);
    DPRINT1("[MM] MiInitBalancerThread: Initializing event at %p\n", &MiBalancerEvent);
    KeInitializeEvent(&MiBalancerEvent, SynchronizationEvent, FALSE);
    DPRINT1("[MM] MiInitBalancerThread: Event MiBalancerEvent initialized\n");
    KeInitializeEvent(&MiBalancerDoneEvent, SynchronizationEvent, FALSE);
    DPRINT1("[MM] MiInitBalancerThread: Event MiBalancerDoneEvent initialized\n");
    KeInitializeTimerEx(&MiBalancerTimer, SynchronizationTimer);
    DPRINT1("[MM] MiInitBalancerThread: Timer initialized\n");

    Timeout.QuadPart = -20000000; /* 2 sec */
    KeSetTimerEx(&MiBalancerTimer,
                 Timeout,
                 2000,         /* 2 sec */
                 NULL);
    DPRINT1("[MM] MiInitBalancerThread: Timer set\n");

    DPRINT1("[MM] MiInitBalancerThread: About to create system thread\n");
    Status = PsCreateSystemThread(&MiBalancerThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  &MiBalancerThreadId,
                                  MiBalancerThread,
                                  NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[MM] MiInitBalancerThread: PsCreateSystemThread FAILED with status 0x%lx\n", Status);
        KeBugCheck(MEMORY_MANAGEMENT);
    }
    DPRINT1("[MM] MiInitBalancerThread: Thread created successfully, handle=%p\n", MiBalancerThreadHandle);

    /* ARM64: Ensure all event initializations are visible before setting the initialized flag.
     * On ARM64's relaxed memory model, other CPUs might see MiBalancerInitialized = TRUE
     * before they see the initialized event structures, leading to crashes when they try
     * to wait on uninitialized events. Use a release barrier to ensure proper ordering.
     */
#if defined(_M_ARM64) || defined(__aarch64__)
    __dmb(_ARM64_BARRIER_ISH);  /* Data Memory Barrier - Inner Shareable */
#endif
    MiBalancerInitialized = TRUE;
#if defined(_M_ARM64) || defined(__aarch64__)
    __dmb(_ARM64_BARRIER_ISH);  /* Ensure flag write is visible before any subsequent operations */
#endif

    Priority = LOW_REALTIME_PRIORITY + 1;
    DPRINT1("[MM] MiInitBalancerThread: About to set thread priority to %d\n", Priority);
    NtSetInformationThread(MiBalancerThreadHandle,
                           ThreadPriority,
                           &Priority,
                           sizeof(Priority));
    DPRINT1("[MM] MiInitBalancerThread: Exit successfully\n");

}


/* EOF */
