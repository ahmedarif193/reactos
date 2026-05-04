/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ps/process.c
 * PURPOSE:         Process Manager: Process Management
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Thomas Weidenmueller (w3seek@reactos.org
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern ULONG PsMinimumWorkingSet, PsMaximumWorkingSet;

POBJECT_TYPE PsProcessType = NULL;

LIST_ENTRY PsActiveProcessHead;
KGUARDED_MUTEX PspActiveProcessMutex;

LARGE_INTEGER ShortPsLockDelay;

ULONG PsRawPrioritySeparation;
ULONG PsPrioritySeparation;
CHAR PspForegroundQuantum[3];

/* Fixed quantum table */
CHAR PspFixedQuantums[6] =
{
    /* Short quantums */
    3 * 6, /* Level 1 */
    3 * 6, /* Level 2 */
    3 * 6, /* Level 3 */

    /* Long quantums */
    6 * 6, /* Level 1 */
    6 * 6, /* Level 2 */
    6 * 6  /* Level 3 */
};

/* Variable quantum table */
CHAR PspVariableQuantums[6] =
{
    /* Short quantums */
    1 * 6, /* Level 1 */
    2 * 6, /* Level 2 */
    3 * 6, /* Level 3 */

    /* Long quantums */
    2 * 6, /* Level 1 */
    4 * 6, /* Level 2 */
    6 * 6  /* Level 3 */
};

/* Priority table */
KPRIORITY PspPriorityTable[PROCESS_PRIORITY_CLASS_ABOVE_NORMAL + 1] =
{
    8,
    4,
    8,
    13,
    24,
    6,
    10
};

/* PRIVATE FUNCTIONS *********************************************************/

PETHREAD
NTAPI
PsGetNextProcessThread(IN PEPROCESS Process,
                       IN PETHREAD Thread OPTIONAL)
{
    PETHREAD FoundThread = NULL;
    PLIST_ENTRY ListHead, Entry;
    PAGED_CODE();
    PSTRACE(PS_PROCESS_DEBUG,
            "Process: %p Thread: %p\n", Process, Thread);

    /* Lock the process */
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&Process->ProcessLock);

    /* Check if we're already starting somewhere */
    if (Thread)
    {
        /* Start where we left off */
        Entry = Thread->ThreadListEntry.Flink;
    }
    else
    {
        /* Start at the beginning */
        Entry = Process->ThreadListHead.Flink;
    }

    /* Set the list head and start looping */
    ListHead = &Process->ThreadListHead;
    while (ListHead != Entry)
    {
        /* Get the Thread */
        FoundThread = CONTAINING_RECORD(Entry, ETHREAD, ThreadListEntry);

        /* Safe reference the thread */
        if (ObReferenceObjectSafe(FoundThread)) break;

        /* Nothing found, keep looping */
        FoundThread = NULL;
        Entry = Entry->Flink;
    }

    /* Unlock the process */
    ExReleasePushLockShared(&Process->ProcessLock);
    KeLeaveCriticalRegion();

    /* Check if we had a starting thread, and dereference it */
    if (Thread) ObDereferenceObject(Thread);

    /* Return what we found */
    return FoundThread;
}

PEPROCESS
NTAPI
PsGetNextProcess(IN PEPROCESS OldProcess)
{
    PLIST_ENTRY Entry;
    PEPROCESS FoundProcess = NULL;
    PAGED_CODE();
    PSTRACE(PS_PROCESS_DEBUG, "Process: %p\n", OldProcess);

    /* Acquire the Active Process Lock */
    KeAcquireGuardedMutex(&PspActiveProcessMutex);

    /* Check if we're already starting somewhere */
    if (OldProcess)
    {
        /* Start where we left off */
        Entry = OldProcess->ActiveProcessLinks.Flink;
    }
    else
    {
        /* Start at the beginning */
        Entry = PsActiveProcessHead.Flink;
    }

    /* Loop the process list */
    while (Entry != &PsActiveProcessHead)
    {
        /* Get the process */
        FoundProcess = CONTAINING_RECORD(Entry, EPROCESS, ActiveProcessLinks);

        /* Reference the process */
        if (ObReferenceObjectSafe(FoundProcess)) break;

        /* Nothing found, keep trying */
        FoundProcess = NULL;
        Entry = Entry->Flink;
    }

    /* Release the lock */
    KeReleaseGuardedMutex(&PspActiveProcessMutex);

    /* Dereference the Process we had referenced earlier */
    if (OldProcess) ObDereferenceObject(OldProcess);
    return FoundProcess;
}

KPRIORITY
NTAPI
PspComputeQuantumAndPriority(IN PEPROCESS Process,
                             IN PSPROCESSPRIORITYMODE Mode,
                             OUT PUCHAR Quantum)
{
    ULONG i;
    UCHAR LocalQuantum, MemoryPriority;
    PAGED_CODE();
    PSTRACE(PS_PROCESS_DEBUG, "Process: %p Mode: %lx\n", Process, Mode);

    /* Check if this is a foreground process */
    if (Mode == PsProcessPriorityForeground)
    {
        /* Set the memory priority and use priority separation */
        MemoryPriority = MEMORY_PRIORITY_FOREGROUND;
        i = PsPrioritySeparation;
    }
    else
    {
        /* Set the background memory priority and no separation */
        MemoryPriority = MEMORY_PRIORITY_BACKGROUND;
        i = 0;
    }

    /* Make sure that the process mode isn't spinning */
    if (Mode != PsProcessPrioritySpinning)
    {
        /* Set the priority */
        MmSetMemoryPriorityProcess(Process, MemoryPriority);
    }

    /* Make sure that the process isn't idle */
    if (Process->PriorityClass != PROCESS_PRIORITY_CLASS_IDLE)
    {
        /* Does the process have a job? */
        if ((Process->Job) && (PspUseJobSchedulingClasses))
        {
            /* Use job quantum */
            LocalQuantum = PspJobSchedulingClasses[Process->Job->
                                                   SchedulingClass];
        }
        else
        {
            /* Use calculated quantum */
            LocalQuantum = PspForegroundQuantum[i];
        }
    }
    else
    {
        /* Process is idle, use default quantum */
        LocalQuantum = 6;
    }

    /* Return quantum to caller */
    *Quantum = LocalQuantum;

    /* Return priority */
    return PspPriorityTable[Process->PriorityClass];
}

VOID
NTAPI
PsChangeQuantumTable(IN BOOLEAN Immediate,
                     IN ULONG PrioritySeparation)
{
    PEPROCESS Process = NULL;
    ULONG i;
    UCHAR Quantum;
    PCHAR QuantumTable;
    PAGED_CODE();
    PSTRACE(PS_PROCESS_DEBUG,
            "%lx PrioritySeparation: %lx\n", Immediate, PrioritySeparation);

    /* Write the current priority separation */
    PsPrioritySeparation = PspPrioritySeparationFromMask(PrioritySeparation);

    /* Normalize it if it was too high */
    if (PsPrioritySeparation == 3) PsPrioritySeparation = 2;

    /* Get the quantum table to use */
    if (PspQuantumTypeFromMask(PrioritySeparation) == PSP_VARIABLE_QUANTUMS)
    {
        /* Use a variable table */
        QuantumTable = PspVariableQuantums;
    }
    else if (PspQuantumTypeFromMask(PrioritySeparation) == PSP_FIXED_QUANTUMS)
    {
        /* Use fixed table */
        QuantumTable = PspFixedQuantums;
    }
    else
    {
        /* Use default for the type of system we're on */
        QuantumTable = MmIsThisAnNtAsSystem() ? PspFixedQuantums : PspVariableQuantums;
    }

    /* Now check if we should use long or short */
    if (PspQuantumLengthFromMask(PrioritySeparation) == PSP_LONG_QUANTUMS)
    {
        /* Use long quantums */
        QuantumTable += 3;
    }
    else if (PspQuantumLengthFromMask(PrioritySeparation) == PSP_SHORT_QUANTUMS)
    {
        /* Keep existing table */
        NOTHING;
    }
    else
    {
        /* Use default for the type of system we're on */
        QuantumTable += MmIsThisAnNtAsSystem() ? 3 : 0;
    }

    /* Check if we're using long fixed quantums */
    if (QuantumTable == &PspFixedQuantums[3])
    {
        /* Use Job scheduling classes */
         PspUseJobSchedulingClasses = TRUE;
    }
    else
    {
        /* Otherwise, we don't */
        PspUseJobSchedulingClasses = FALSE;
    }

    /* Copy the selected table into the Foreground Quantum table */
    RtlCopyMemory(PspForegroundQuantum,
                  QuantumTable,
                  sizeof(PspForegroundQuantum));

    /* Check if we should apply these changes real-time */
    if (Immediate)
    {
        /* We are...loop every process */
        Process = PsGetNextProcess(Process);
        while (Process)
        {
            /* Use the priority separation if this is a foreground process */
            i = (Process->Vm.Flags.MemoryPriority ==
                 MEMORY_PRIORITY_BACKGROUND) ?
                 0: PsPrioritySeparation;

            /* Make sure that the process isn't idle */
            if (Process->PriorityClass != PROCESS_PRIORITY_CLASS_IDLE)
            {
                /* Does the process have a job? */
                if ((Process->Job) && (PspUseJobSchedulingClasses))
                {
                    /* Use job quantum */
                    Quantum = PspJobSchedulingClasses[Process->Job->SchedulingClass];
                }
                else
                {
                    /* Use calculated quantum */
                    Quantum = PspForegroundQuantum[i];
                }
            }
            else
            {
                /* Process is idle, use default quantum */
                Quantum = 6;
            }

            /* Now set the quantum */
            KeSetQuantumProcess(&Process->Pcb, Quantum);

            /* Get the next process */
            Process = PsGetNextProcess(Process);
        }
    }
}

NTSTATUS
NTAPI
PspCreateProcess(OUT PHANDLE ProcessHandle,
                 IN ACCESS_MASK DesiredAccess,
                 IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
                 IN HANDLE ParentProcess OPTIONAL,
                 IN ULONG Flags,
                 IN HANDLE SectionHandle OPTIONAL,
                 IN HANDLE DebugPort OPTIONAL,
                 IN HANDLE ExceptionPort OPTIONAL,
                 IN BOOLEAN InJob)
{
    HANDLE hProcess;
    PEPROCESS Process, Parent;
    PVOID ExceptionPortObject;
    PDEBUG_OBJECT DebugObject;
    PSECTION SectionObject;
    NTSTATUS Status, AccessStatus;
    ULONG_PTR DirectoryTableBase[2] = {0,0};
    KAFFINITY Affinity;
    HANDLE_TABLE_ENTRY CidEntry;
    PETHREAD CurrentThread = PsGetCurrentThread();
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS CurrentProcess = PsGetCurrentProcess();
    ULONG MinWs, MaxWs;
    ACCESS_STATE LocalAccessState;
    PACCESS_STATE AccessState = &LocalAccessState;
    AUX_ACCESS_DATA AuxData;
    UCHAR Quantum;
    BOOLEAN Result, SdAllocated;
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    BOOLEAN NeedsPeb = FALSE;
    INITIAL_PEB InitialPeb;
    PAGED_CODE();
    PSTRACE(PS_PROCESS_DEBUG,
            "ProcessHandle: %p Parent: %p\n", ProcessHandle, ParentProcess);

#if defined(_M_ARM64)
    DPRINT("[arm64][ps] PspCreateProcess: begin parent=%p section=%p flags=0x%lx\n",
            ParentProcess,
            SectionHandle,
            Flags);
#endif

    /* Validate flags */
    if (Flags & ~PROCESS_CREATE_FLAGS_LEGAL_MASK) return STATUS_INVALID_PARAMETER;

    /* Check for parent */
    if (ParentProcess)
    {
        /* Reference it */
        Status = ObReferenceObjectByHandle(ParentProcess,
                                           PROCESS_CREATE_PROCESS,
                                           PsProcessType,
                                           PreviousMode,
                                           (PVOID*)&Parent,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;

        /* If this process should be in a job but the parent isn't */
        if ((InJob) && (!Parent->Job))
        {
            /* This is illegal. Dereference the parent and fail */
            ObDereferenceObject(Parent);
            return STATUS_INVALID_PARAMETER;
        }

        /* Inherit Parent process's Affinity. */
        Affinity = Parent->Pcb.Affinity;
    }
    else
    {
        /* We have no parent */
        Parent = NULL;
        Affinity = KeActiveProcessors;
    }

    /* Save working set data */
    MinWs = PsMinimumWorkingSet;
    MaxWs = PsMaximumWorkingSet;

    /* Create the Object */
    Status = ObCreateObject(PreviousMode,
                            PsProcessType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(EPROCESS),
                            0,
                            0,
                            (PVOID*)&Process);
#if defined(_M_ARM64)
    DPRINT("[arm64][ps] PspCreateProcess: ObCreateObject status=0x%08lx process=%p\n",
            Status,
            Process);
#endif
    if (!NT_SUCCESS(Status)) goto Cleanup;

    /* Clean up the Object */
    RtlZeroMemory(Process, sizeof(EPROCESS));

    /* Initialize pushlock and rundown protection */
    ExInitializeRundownProtection(&Process->RundownProtect);
    Process->ProcessLock.Value = 0;

    /* Setup the Thread List Head */
    InitializeListHead(&Process->ThreadListHead);

    /* Set up the Quota Block from the Parent */
    PspInheritQuota(Process, Parent);

    /* Set up Dos Device Map from the Parent */
    ObInheritDeviceMap(Parent, Process);

    /* Check if we have a parent */
    if (Parent)
    {
        /* Inherit PID and hard-error processing */
        Process->InheritedFromUniqueProcessId = Parent->UniqueProcessId;
        Process->DefaultHardErrorProcessing = Parent->DefaultHardErrorProcessing;
    }
    else
    {
        /* Use default hard-error processing */
        Process->DefaultHardErrorProcessing = SEM_FAILCRITICALERRORS;
    }

    /* Check for a section handle */
    if (SectionHandle)
    {
        /* Get a pointer to it */
        Status = ObReferenceObjectByHandle(SectionHandle,
                                           SECTION_MAP_EXECUTE,
                                           MmSectionObjectType,
                                           PreviousMode,
                                           (PVOID*)&SectionObject,
                                           NULL);
        if (!NT_SUCCESS(Status)) goto CleanupWithRef;
    }
    else
    {
        /* Assume no section object */
        SectionObject = NULL;

        /* Is the parent the initial process?
         * Check for NULL also, as at initialization PsInitialSystemProcess is NULL */
        if (Parent != PsInitialSystemProcess && (Parent != NULL))
        {
            /* It's not, so acquire the process rundown */
            if (ExAcquireRundownProtection(&Parent->RundownProtect))
            {
                /* If the parent has a section, use it */
                SectionObject = Parent->SectionObject;
                if (SectionObject) ObReferenceObject(SectionObject);

                /* Release process rundown */
                ExReleaseRundownProtection(&Parent->RundownProtect);
            }

            /* If we don't have a section object */
            if (!SectionObject)
            {
                /* Then the process is in termination, so fail */
                Status = STATUS_PROCESS_IS_TERMINATING;
                goto CleanupWithRef;
            }
        }
    }

    /* Save the pointer to the section object */
    Process->SectionObject = SectionObject;

    /* Check for the debug port */
    if (DebugPort)
    {
        /* Reference it */
        Status = ObReferenceObjectByHandle(DebugPort,
                                           DEBUG_OBJECT_ADD_REMOVE_PROCESS,
                                           DbgkDebugObjectType,
                                           PreviousMode,
                                           (PVOID*)&DebugObject,
                                           NULL);
        if (!NT_SUCCESS(Status)) goto CleanupWithRef;

        /* Save the debug object */
        Process->DebugPort = DebugObject;

        /* Check if the caller doesn't want the debug stuff inherited */
        if (Flags & PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT)
        {
            /* Set the process flag */
            InterlockedOr((PLONG)&Process->Flags, PSF_NO_DEBUG_INHERIT_BIT);
        }
    }
    else
    {
        /* Do we have a parent? Copy his debug port */
        if (Parent) DbgkCopyProcessDebugPort(Process, Parent);
    }

    /* Now check for an exception port */
    if (ExceptionPort)
    {
        /* Reference it */
        Status = ObReferenceObjectByHandle(ExceptionPort,
                                           PORT_ALL_ACCESS,
                                           LpcPortObjectType,
                                           PreviousMode,
                                           (PVOID*)&ExceptionPortObject,
                                           NULL);
        if (!NT_SUCCESS(Status)) goto CleanupWithRef;

        /* Save the exception port */
        Process->ExceptionPort = ExceptionPortObject;
    }

    /* Save the pointer to the section object */
    Process->SectionObject = SectionObject;

    /* Set default exit code */
    Process->ExitStatus = STATUS_PENDING;

    /* Check if this is the initial process being built */
    if (Parent)
    {
        /* Create the address space for the child */
        if (!MmCreateProcessAddressSpace(MinWs,
                                         Process,
                                         DirectoryTableBase))
        {
            /* Failed */
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto CleanupWithRef;
        }
    }
    else
    {
        /* Otherwise, we are the boot process, we're already semi-initialized */
#if defined(_M_ARM64)
        DPRINT("[arm64][ps] PspCreateProcess: MmInitializeHandBuiltProcess\n");
#endif
        Process->ObjectTable = CurrentProcess->ObjectTable;
        Status = MmInitializeHandBuiltProcess(Process, DirectoryTableBase);
#if defined(_M_ARM64)
        DPRINT("[arm64][ps] PspCreateProcess: MmInitializeHandBuiltProcess status=0x%08lx dtb=%p/%p\n",
                Status,
                (PVOID)DirectoryTableBase[0],
                (PVOID)DirectoryTableBase[1]);
#endif
        if (!NT_SUCCESS(Status)) goto CleanupWithRef;
    }

    /* We now have an address space */
    InterlockedOr((PLONG)&Process->Flags, PSF_HAS_ADDRESS_SPACE_BIT);

    /* Set the maximum WS */
    Process->Vm.MaximumWorkingSetSize = MaxWs;

    /* Now initialize the Kernel Process */
    KeInitializeProcess(&Process->Pcb,
                        PROCESS_PRIORITY_NORMAL,
                        Affinity,
                        DirectoryTableBase,
                        BooleanFlagOn(Process->DefaultHardErrorProcessing,
                                      SEM_NOALIGNMENTFAULTEXCEPT));

    /* Duplicate Parent Token */
    Status = PspInitializeProcessSecurity(Process, Parent);
#if defined(_M_ARM64)
    DPRINT("[arm64][ps] PspCreateProcess: PspInitializeProcessSecurity status=0x%08lx\n",
            Status);
#endif
    if (!NT_SUCCESS(Status)) goto CleanupWithRef;

    /* Set default priority class */
    Process->PriorityClass = PROCESS_PRIORITY_CLASS_NORMAL;

    /* Check if we have a parent */
    if (Parent)
    {
        /* Check our priority class */
        if (Parent->PriorityClass == PROCESS_PRIORITY_CLASS_IDLE ||
            Parent->PriorityClass == PROCESS_PRIORITY_CLASS_BELOW_NORMAL)
        {
            /* Normalize it */
            Process->PriorityClass = Parent->PriorityClass;
        }

        /* Initialize object manager for the process */
        Status = ObInitProcess(Flags & PROCESS_CREATE_FLAGS_INHERIT_HANDLES ?
                               Parent : NULL,
                               Process);
        if (!NT_SUCCESS(Status)) goto CleanupWithRef;
    }
    else
    {
        /* Do the second part of the boot process memory setup */
#if defined(_M_ARM64)
        DPRINT("[arm64][ps] PspCreateProcess: MmInitializeHandBuiltProcess2\n");
#endif
        Status = MmInitializeHandBuiltProcess2(Process);
#if defined(_M_ARM64)
        DPRINT("[arm64][ps] PspCreateProcess: MmInitializeHandBuiltProcess2 status=0x%08lx\n",
                Status);
#endif
        if (!NT_SUCCESS(Status)) goto CleanupWithRef;
    }

    /* Set success for now */
    Status = STATUS_SUCCESS;

    /* Check if this is a real user-mode process */
    if (SectionHandle)
    {
        /* Initialize the address space */
        Status = MmInitializeProcessAddressSpace(Process,
                                                 NULL,
                                                 SectionObject,
                                                 &Flags,
                                                 &Process->
                                                 SeAuditProcessCreationInfo.
                                                 ImageFileName);
        if (!NT_SUCCESS(Status)) goto CleanupWithRef;

        //
        // We need a PEB
        //
        NeedsPeb = TRUE;
    }
    else if (Parent)
    {
        /* Check if this is a child of the system process */
        if (Parent != PsInitialSystemProcess)
        {
            //
            // We need a PEB
            //
            NeedsPeb = TRUE;

            /* This is a clone! */
            ASSERTMSG("No support for cloning yet\n", FALSE);
        }
        else
        {
            /* This is the initial system process */
            Flags &= ~PROCESS_CREATE_FLAGS_LARGE_PAGES;
            Status = MmInitializeProcessAddressSpace(Process,
                                                     NULL,
                                                     NULL,
                                                     &Flags,
                                                     NULL);
            if (!NT_SUCCESS(Status)) goto CleanupWithRef;

            /* Create a dummy image file name */
            Process->SeAuditProcessCreationInfo.ImageFileName =
                ExAllocatePoolWithTag(PagedPool,
                                      sizeof(OBJECT_NAME_INFORMATION),
                                      TAG_SEPA);
            if (!Process->SeAuditProcessCreationInfo.ImageFileName)
            {
                /* Fail */
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto CleanupWithRef;
            }

            /* Zero it out */
            RtlZeroMemory(Process->SeAuditProcessCreationInfo.ImageFileName,
                          sizeof(OBJECT_NAME_INFORMATION));
        }
    }

#if MI_TRACE_PFNS
    /* Copy the process name now that we have it */
    memcpy(MiGetPfnEntry(Process->Pcb.DirectoryTableBase[0] >> PAGE_SHIFT)->ProcessName, Process->ImageFileName, 16);
    if (Process->Pcb.DirectoryTableBase[1]) memcpy(MiGetPfnEntry(Process->Pcb.DirectoryTableBase[1] >> PAGE_SHIFT)->ProcessName, Process->ImageFileName, 16);
    if (Process->WorkingSetPage) memcpy(MiGetPfnEntry(Process->WorkingSetPage)->ProcessName, Process->ImageFileName, 16);
#endif

    /* Check if we have a section object and map the system DLL */
    if (SectionObject) PspMapSystemDll(Process, NULL, FALSE);

    /* Create a handle for the Process */
    CidEntry.Object = Process;
    CidEntry.GrantedAccess = 0;
    Process->UniqueProcessId = ExCreateHandle(PspCidTable, &CidEntry);
#if defined(_M_ARM64)
    DPRINT("[arm64][ps] PspCreateProcess: UniqueProcessId=%p\n",
            Process->UniqueProcessId);
#endif
    if (!Process->UniqueProcessId)
    {
        /* Fail */
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto CleanupWithRef;
    }

    /* Set the handle table PID */
    Process->ObjectTable->UniqueProcessId = Process->UniqueProcessId;

    /* Check if we need to audit */
    if (SeDetailedAuditingWithToken(NULL)) SeAuditProcessCreate(Process);

    /* Check if the parent had a job */
    if ((Parent) && (Parent->Job))
    {
        /* FIXME: We need to insert this process */
        DPRINT1("Jobs not yet supported\n");
    }

    /* Create PEB only for User-Mode Processes */
    if ((Parent) && (NeedsPeb))
    {
        //
        // Set up the initial PEB
        //
        RtlZeroMemory(&InitialPeb, sizeof(INITIAL_PEB));
        InitialPeb.Mutant = (HANDLE)-1;
        InitialPeb.ImageUsesLargePages = 0; // FIXME: Not yet supported

        //
        // Create it only if we have an image section
        //
        if (SectionHandle)
        {
            //
            // Create it
            //
            Status = MmCreatePeb(Process, &InitialPeb, &Process->Peb);
            if (!NT_SUCCESS(Status)) goto CleanupWithRef;
        }
        else
        {
            //
            // We have to clone it
            //
            ASSERTMSG("No support for cloning yet\n", FALSE);
        }

    }

    /* The process can now be activated */
    KeAcquireGuardedMutex(&PspActiveProcessMutex);
    InsertTailList(&PsActiveProcessHead, &Process->ActiveProcessLinks);
    KeReleaseGuardedMutex(&PspActiveProcessMutex);

    /* Create an access state */
    Status = SeCreateAccessStateEx(CurrentThread,
                                   ((Parent) &&
                                   (Parent == PsInitialSystemProcess)) ?
                                    Parent : CurrentProcess,
                                   &LocalAccessState,
                                   &AuxData,
                                   DesiredAccess,
                                   &PsProcessType->TypeInfo.GenericMapping);
#if defined(_M_ARM64)
    DPRINT("[arm64][ps] PspCreateProcess: SeCreateAccessStateEx status=0x%08lx\n",
            Status);
#endif
    if (!NT_SUCCESS(Status)) goto CleanupWithRef;

    /* Insert the Process into the Object Directory */
#if defined(_M_ARM64)
    DPRINT("[arm64][ps] PspCreateProcess: ObInsertObject\n");
#endif
    Status = ObInsertObject(Process,
                            AccessState,
                            DesiredAccess,
                            1,
                            NULL,
                            &hProcess);
#if defined(_M_ARM64)
    DPRINT("[arm64][ps] PspCreateProcess: ObInsertObject status=0x%08lx handle=%p\n",
            Status,
            hProcess);
#endif

    /* Free the access state */
    if (AccessState) SeDeleteAccessState(AccessState);

    /* Cleanup on failure */
    if (!NT_SUCCESS(Status)) goto Cleanup;

    /* Compute Quantum and Priority */
    ASSERT(IsListEmpty(&Process->ThreadListHead) == TRUE);
    Process->Pcb.BasePriority =
        (SCHAR)PspComputeQuantumAndPriority(Process,
                                            PsProcessPriorityBackground,
                                            &Quantum);
    Process->Pcb.QuantumReset = Quantum;

    /* Check if we have a parent other then the initial system process */
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    /* GrantedAccess was replaced by ImagePathHash at Vista+ */
#define _ProcGrantedAccess ImagePathHash
#else
#define _ProcGrantedAccess GrantedAccess
#endif
    Process->_ProcGrantedAccess = PROCESS_TERMINATE;
    if ((Parent) && (Parent != PsInitialSystemProcess))
    {
        /* Get the process's SD */
        Status = ObGetObjectSecurity(Process,
                                     &SecurityDescriptor,
                                     &SdAllocated);
        if (!NT_SUCCESS(Status))
        {
            /* We failed, close the handle and clean up */
            ObCloseHandle(hProcess, PreviousMode);
            goto CleanupWithRef;
        }

        /* Create the subject context */
        SubjectContext.ProcessAuditId = Process;
        SubjectContext.PrimaryToken = PsReferencePrimaryToken(Process);
        SubjectContext.ClientToken = NULL;

        /* Do the access check */
        Result = SeAccessCheck(SecurityDescriptor,
                               &SubjectContext,
                               FALSE,
                               MAXIMUM_ALLOWED,
                               0,
                               NULL,
                               &PsProcessType->TypeInfo.GenericMapping,
                               PreviousMode,
                               &Process->_ProcGrantedAccess,
                               &AccessStatus);

        /* Dereference the token and let go the SD */
        ObFastDereferenceObject(&Process->Token,
                                SubjectContext.PrimaryToken);
        ObReleaseObjectSecurity(SecurityDescriptor, SdAllocated);

        /* Remove access if it failed */
        if (!Result) Process->_ProcGrantedAccess = 0;

        /* Give the process some basic access */
        Process->_ProcGrantedAccess |= (PROCESS_VM_OPERATION |
                                   PROCESS_VM_READ |
                                   PROCESS_VM_WRITE |
                                   PROCESS_QUERY_INFORMATION |
                                   PROCESS_TERMINATE |
                                   PROCESS_CREATE_THREAD |
                                   PROCESS_DUP_HANDLE |
                                   PROCESS_CREATE_PROCESS |
                                   PROCESS_SET_INFORMATION |
                                   STANDARD_RIGHTS_ALL |
                                   PROCESS_SET_QUOTA);
    }
    else
    {
        /* Set full granted access */
        Process->_ProcGrantedAccess = PROCESS_ALL_ACCESS;
    }
#undef _ProcGrantedAccess

    /* Set the Creation Time */
    KeQuerySystemTime(&Process->CreateTime);

    /* Protect against bad user-mode pointer */
    _SEH2_TRY
    {
        /* Hacky way of returning the PEB to the user-mode creator */
        if ((Process->Peb) && (CurrentThread->Tcb.Teb))
        {
            CurrentThread->Tcb.Teb->NtTib.ArbitraryUserPointer = Process->Peb;
        }

        /* Save the process handle */
       *ProcessHandle = hProcess;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Get the exception code */
       Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* Run the Notification Routines */
    PspRunCreateProcessNotifyRoutines(Process, TRUE);

    /* If 12 processes have been created, enough of user-mode is ready */
    if (++ProcessCount == 12) Ki386PerfEnd();

CleanupWithRef:
    /*
     * Dereference the process. For failures, kills the process and does
     * cleanup present in PspDeleteProcess. For success, kills the extra
     * reference added by ObInsertObject.
     */
    ObDereferenceObject(Process);

Cleanup:
    /* Dereference the parent */
    if (Parent) ObDereferenceObject(Parent);

    /* Return status to caller */
    return Status;
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
NTSTATUS
NTAPI
PsCreateSystemProcess(OUT PHANDLE ProcessHandle,
                      IN ACCESS_MASK DesiredAccess,
                      IN POBJECT_ATTRIBUTES ObjectAttributes)
{
    /* Call the internal API */
    return PspCreateProcess(ProcessHandle,
                            DesiredAccess,
                            ObjectAttributes,
                            NULL,
                            0,
                            NULL,
                            NULL,
                            NULL,
                            FALSE);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
PsLookupProcessByProcessId(IN HANDLE ProcessId,
                           OUT PEPROCESS *Process)
{
    PHANDLE_TABLE_ENTRY CidEntry;
    PEPROCESS FoundProcess;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;
    PAGED_CODE();
    PSTRACE(PS_PROCESS_DEBUG, "ProcessId: %p\n", ProcessId);
    KeEnterCriticalRegion();

    /* Get the CID Handle Entry */
    CidEntry = ExMapHandleToPointer(PspCidTable, ProcessId);
    if (CidEntry)
    {
        /* Get the Process */
        FoundProcess = CidEntry->Object;

        /* Make sure it's really a process */
        if (FoundProcess->Pcb.Header.Type == ProcessObject)
        {
            /* Safe Reference and return it */
            if (ObReferenceObjectSafe(FoundProcess))
            {
                *Process = FoundProcess;
                Status = STATUS_SUCCESS;
            }
        }

        /* Unlock the Entry */
        ExUnlockHandleTableEntry(PspCidTable, CidEntry);
    }

    /* Return to caller */
    KeLeaveCriticalRegion();
    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
PsLookupProcessThreadByCid(IN PCLIENT_ID Cid,
                           OUT PEPROCESS *Process OPTIONAL,
                           OUT PETHREAD *Thread)
{
    PHANDLE_TABLE_ENTRY CidEntry;
    PETHREAD FoundThread;
    NTSTATUS Status = STATUS_INVALID_CID;
    PAGED_CODE();
    PSTRACE(PS_PROCESS_DEBUG, "Cid: %p\n", Cid);
    KeEnterCriticalRegion();

    /* Get the CID Handle Entry */
    CidEntry = ExMapHandleToPointer(PspCidTable, Cid->UniqueThread);
    if (CidEntry)
    {
        /* Get the Process */
        FoundThread = CidEntry->Object;

        /* Make sure it's really a thread and this process' */
        if ((FoundThread->Tcb.Header.Type == ThreadObject) &&
            (FoundThread->Cid.UniqueProcess == Cid->UniqueProcess))
        {
            /* Safe Reference and return it */
            if (ObReferenceObjectSafe(FoundThread))
            {
                *Thread = FoundThread;
                Status = STATUS_SUCCESS;

                /* Check if we should return the Process too */
                if (Process)
                {
                    /* Return it and reference it */
                    *Process = (PEPROCESS)FoundThread->ThreadsProcess;
                    ObReferenceObject(*Process);
                }
            }
        }

        /* Unlock the Entry */
        ExUnlockHandleTableEntry(PspCidTable, CidEntry);
    }

    /* Return to caller */
    KeLeaveCriticalRegion();
    return Status;
}

/*
 * @implemented
 */
LARGE_INTEGER
NTAPI
PsGetProcessExitTime(VOID)
{
    return PsGetCurrentProcess()->ExitTime;
}

/*
 * @implemented
 */
LONGLONG
NTAPI
PsGetProcessCreateTimeQuadPart(PEPROCESS Process)
{
    return Process->CreateTime.QuadPart;
}

/*
 * @implemented
 */
PVOID
NTAPI
PsGetProcessDebugPort(PEPROCESS Process)
{
    return Process->DebugPort;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
PsGetProcessExitProcessCalled(PEPROCESS Process)
{
    return (BOOLEAN)Process->ProcessExiting;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
PsGetProcessExitStatus(PEPROCESS Process)
{
    return Process->ExitStatus;
}

/*
 * @implemented
 */
HANDLE
NTAPI
PsGetProcessId(PEPROCESS Process)
{
    return (HANDLE)Process->UniqueProcessId;
}

/*
 * @implemented
 */
LPSTR
NTAPI
PsGetProcessImageFileName(PEPROCESS Process)
{
    return (LPSTR)Process->ImageFileName;
}

/*
 * @implemented
 */
HANDLE
NTAPI
PsGetProcessInheritedFromUniqueProcessId(PEPROCESS Process)
{
    return Process->InheritedFromUniqueProcessId;
}

/*
 * @implemented
 */
PEJOB
NTAPI
PsGetProcessJob(PEPROCESS Process)
{
    return Process->Job;
}

/*
 * @implemented
 */
PPEB
NTAPI
PsGetProcessPeb(PEPROCESS Process)
{
    return Process->Peb;
}

/*
 * @implemented
 */
ULONG
NTAPI
PsGetProcessPriorityClass(PEPROCESS Process)
{
    return Process->PriorityClass;
}

/*
 * @implemented
 */
HANDLE
NTAPI
PsGetCurrentProcessId(VOID)
{
    return (HANDLE)PsGetCurrentProcess()->UniqueProcessId;
}

/*
 * @implemented
 */
ULONG
NTAPI
PsGetCurrentProcessSessionId(VOID)
{
    return MmGetSessionId(PsGetCurrentProcess());
}

/*
 * @implemented
 */
PVOID
NTAPI
PsGetProcessSectionBaseAddress(PEPROCESS Process)
{
    return Process->SectionBaseAddress;
}

/*
 * @implemented
 */
PVOID
NTAPI
PsGetProcessSecurityPort(PEPROCESS Process)
{
    return Process->SecurityPort;
}

/*
 * @implemented
 */
ULONG
NTAPI
PsGetProcessSessionId(IN PEPROCESS Process)
{
    return MmGetSessionId(Process);
}

/*
 * @implemented
 */
ULONG
NTAPI
PsGetProcessSessionIdEx(IN PEPROCESS Process)
{
    return MmGetSessionIdEx(Process);
}

/*
 * @implemented
 */
PVOID
NTAPI
PsGetCurrentProcessWin32Process(VOID)
{
    return PsGetCurrentProcess()->Win32Process;
}

/*
 * @implemented
 */
PVOID
NTAPI
PsGetProcessWin32Process(PEPROCESS Process)
{
    return Process->Win32Process;
}

/*
 * @implemented
 */
PVOID
NTAPI
PsGetProcessWin32WindowStation(PEPROCESS Process)
{
    return Process->Win32WindowStation;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
PsIsProcessBeingDebugged(PEPROCESS Process)
{
    return Process->DebugPort != NULL;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
PsIsSystemProcess(IN PEPROCESS Process)
{
    /* Return if this is the System Process */
    return Process == PsInitialSystemProcess;
}

/*
 * @implemented
 */
VOID
NTAPI
PsSetProcessPriorityClass(PEPROCESS Process,
                          ULONG PriorityClass)
{
    Process->PriorityClass = (UCHAR)PriorityClass;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
PsSetProcessSecurityPort(PEPROCESS Process,
                         PVOID SecurityPort)
{
    Process->SecurityPort = SecurityPort;
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
PsSetProcessWin32Process(
    _Inout_ PEPROCESS Process,
    _In_opt_ PVOID Win32Process,
    _In_opt_ PVOID OldWin32Process)
{
    NTSTATUS Status;

    /* Assume success */
    Status = STATUS_SUCCESS;

    /* Lock the process */
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Process->ProcessLock);

    /* Check if we set a new win32 process */
    if (Win32Process != NULL)
    {
        /* Check if the process is in the right state */
        if (((Process->Flags & PSF_PROCESS_DELETE_BIT) == 0) &&
            (Process->Win32Process == NULL))
        {
            /* Set the new win32 process */
            Process->Win32Process = Win32Process;
        }
        else
        {
            /* Otherwise fail */
            Status = STATUS_PROCESS_IS_TERMINATING;
        }
    }
    else
    {
        /* Reset the win32 process, did the caller specify the correct old value? */
        if (Process->Win32Process == OldWin32Process)
        {
            /* Yes, so reset the win32 process to NULL */
            Process->Win32Process = NULL;
        }
        else
        {
            /* Otherwise fail */
            Status = STATUS_UNSUCCESSFUL;
        }
    }

    /* Unlock the process */
    ExReleasePushLockExclusive(&Process->ProcessLock);
    KeLeaveCriticalRegion();

    return Status;
}

/*
 * @implemented
 */
VOID
NTAPI
PsSetProcessWindowStation(PEPROCESS Process,
                          PVOID WindowStation)
{
    Process->Win32WindowStation = WindowStation;
}

/*
 * @implemented
 */
VOID
NTAPI
PsSetProcessPriorityByClass(IN PEPROCESS Process,
                            IN PSPROCESSPRIORITYMODE Type)
{
    UCHAR Quantum;
    ULONG Priority;
    PSTRACE(PS_PROCESS_DEBUG, "Process: %p Type: %lx\n", Process, Type);

    /* Compute quantum and priority */
    Priority = PspComputeQuantumAndPriority(Process, Type, &Quantum);

    /* Set them */
    KeSetPriorityAndQuantumProcess(&Process->Pcb, Priority, Quantum);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtCreateProcessEx(OUT PHANDLE ProcessHandle,
                  IN ACCESS_MASK DesiredAccess,
                  IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
                  IN HANDLE ParentProcess,
                  IN ULONG Flags,
                  IN HANDLE SectionHandle OPTIONAL,
                  IN HANDLE DebugPort OPTIONAL,
                  IN HANDLE ExceptionPort OPTIONAL,
                  IN BOOLEAN InJob)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;
    PAGED_CODE();
    PSTRACE(PS_PROCESS_DEBUG,
            "ParentProcess: %p Flags: %lx\n", ParentProcess, Flags);

    /* Check if we came from user mode */
    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            /* Probe process handle */
            ProbeForWriteHandle(ProcessHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* Make sure there's a parent process */
    if (!ParentProcess)
    {
        /* Can't create System Processes like this */
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        /* Create a user Process */
        Status = PspCreateProcess(ProcessHandle,
                                  DesiredAccess,
                                  ObjectAttributes,
                                  ParentProcess,
                                  Flags,
                                  SectionHandle,
                                  DebugPort,
                                  ExceptionPort,
                                  InJob);
    }

    /* Return Status */
    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtCreateProcess(OUT PHANDLE ProcessHandle,
                IN ACCESS_MASK DesiredAccess,
                IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
                IN HANDLE ParentProcess,
                IN BOOLEAN InheritObjectTable,
                IN HANDLE SectionHandle OPTIONAL,
                IN HANDLE DebugPort OPTIONAL,
                IN HANDLE ExceptionPort OPTIONAL)
{
    ULONG Flags = 0;
    PSTRACE(PS_PROCESS_DEBUG,
            "Parent: %p Attributes: %p\n", ParentProcess, ObjectAttributes);

    /* Set new-style flags */
    if ((ULONG_PTR)SectionHandle & 1) Flags |= PROCESS_CREATE_FLAGS_BREAKAWAY;
    if ((ULONG_PTR)DebugPort & 1) Flags |= PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT;
    if (InheritObjectTable) Flags |= PROCESS_CREATE_FLAGS_INHERIT_HANDLES;

    /* Call the new API */
    return NtCreateProcessEx(ProcessHandle,
                             DesiredAccess,
                             ObjectAttributes,
                             ParentProcess,
                             Flags,
                             SectionHandle,
                             DebugPort,
                             ExceptionPort,
                             FALSE);
}

#if (NTDDI_VERSION >= NTDDI_LONGHORN)
/*
 * @implemented
 *
 * NtCreateUserProcess - Vista+ combined process-and-thread creation syscall.
 *
 * This replaces the separate NtCreateProcess + NtCreateThread sequence used
 * on XP/2003. It creates the process, section, PEB, initial thread, TEB,
 * and returns both handles in a single call.
 */
NTSTATUS
NTAPI
NtCreateUserProcess(OUT PHANDLE ProcessHandle,
                    OUT PHANDLE ThreadHandle,
                    IN ACCESS_MASK ProcessDesiredAccess,
                    IN ACCESS_MASK ThreadDesiredAccess,
                    IN POBJECT_ATTRIBUTES ProcessObjectAttributes OPTIONAL,
                    IN POBJECT_ATTRIBUTES ThreadObjectAttributes OPTIONAL,
                    IN ULONG ProcessFlags,
                    IN ULONG ThreadFlags,
                    IN PRTL_USER_PROCESS_PARAMETERS ProcessParameters OPTIONAL,
                    IN OUT PPS_CREATE_INFO CreateInfo,
                    IN OUT PPS_ATTRIBUTE_LIST AttributeList OPTIONAL)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE hProcess = NULL, hThread = NULL;
    HANDLE hSection = NULL, hFile = NULL;
    HANDLE ParentProcess = NtCurrentProcess();
    HANDLE DebugPort = NULL;
    HANDLE ExceptionPort = NULL;
    HANDLE TokenHandle = NULL;
    ULONG PspProcessFlags = 0;
    OBJECT_ATTRIBUTES LocalFileObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING ImageName;
    UNICODE_STRING CapturedImageName;
    PCLIENT_ID ClientIdPtr = NULL;
    PTEB *TebAddressPtr = NULL;
    PSECTION_IMAGE_INFORMATION ImageInfoPtr = NULL;
    PS_CREATE_INFO CapturedCreateInfo;
    SIZE_T AttributeCount, i;
    SECTION_IMAGE_INFORMATION ImageInformation;
    PEPROCESS Process = NULL;
    PETHREAD Thread = NULL;
    CLIENT_ID ClientId;
    INITIAL_TEB InitialTeb;
    CONTEXT ThreadContext;
    PROCESS_BASIC_INFORMATION ProcessBasicInfo;
    PAGED_CODE();
    PSTRACE(PS_PROCESS_DEBUG,
            "ProcessFlags: %lx ThreadFlags: %lx\n", ProcessFlags, ThreadFlags);

    RtlInitUnicodeString(&ImageName, NULL);
    RtlInitUnicodeString(&CapturedImageName, NULL);

    /* Validate user-mode parameters */
    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            /* Probe output handles */
            ProbeForWriteHandle(ProcessHandle);
            ProbeForWriteHandle(ThreadHandle);

            /* Probe and capture CreateInfo */
            ProbeForWrite(CreateInfo, sizeof(PS_CREATE_INFO), sizeof(ULONG));
            CapturedCreateInfo = *CreateInfo;

            /* Probe the attribute list if present */
            if (AttributeList)
            {
                ProbeForWrite(AttributeList, sizeof(PS_ATTRIBUTE_LIST), sizeof(ULONG_PTR));

                /* Calculate the number of attributes */
                if (AttributeList->TotalLength < sizeof(PS_ATTRIBUTE_LIST))
                {
                    _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                }
                AttributeCount = (AttributeList->TotalLength - FIELD_OFFSET(PS_ATTRIBUTE_LIST, Attributes)) /
                                  sizeof(PS_ATTRIBUTE);

                /* Probe the full attribute array */
                ProbeForWrite(AttributeList->Attributes,
                              AttributeCount * sizeof(PS_ATTRIBUTE),
                              sizeof(ULONG_PTR));
            }
            else
            {
                AttributeCount = 0;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        /* Kernel mode */
        CapturedCreateInfo = *CreateInfo;
        if (AttributeList && AttributeList->TotalLength >= sizeof(PS_ATTRIBUTE_LIST))
        {
            AttributeCount = (AttributeList->TotalLength - FIELD_OFFSET(PS_ATTRIBUTE_LIST, Attributes)) /
                              sizeof(PS_ATTRIBUTE);
        }
        else
        {
            AttributeCount = 0;
        }
    }

    /* Verify CreateInfo state is initial */
    if (CapturedCreateInfo.State != PsCreateInitialState)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Parse the attribute list to extract:
     * - Image name (required)
     * - Parent process handle (optional)
     * - Debug port (optional)
     * - Token (optional)
     * - Client ID output pointer
     * - TEB address output pointer
     * - Image info output pointer
     */
    _SEH2_TRY
    {
        for (i = 0; i < AttributeCount; i++)
        {
            ULONG_PTR AttributeNumber = AttributeList->Attributes[i].Attribute & 0x0000FFFF;

            switch (AttributeNumber)
            {
                case PsAttributeImageName:
                    /* Input: image path as a UNICODE_STRING buffer */
                    ImageName.Buffer = (PWSTR)AttributeList->Attributes[i].ValuePtr;
                    ImageName.Length = (USHORT)AttributeList->Attributes[i].Size;
                    ImageName.MaximumLength = ImageName.Length;
                    break;

                case PsAttributeParentProcess:
                    /* Input: parent process handle */
                    ParentProcess = (HANDLE)AttributeList->Attributes[i].Value;
                    break;

                case PsAttributeDebugPort:
                    /* Input: debug port handle */
                    DebugPort = (HANDLE)AttributeList->Attributes[i].Value;
                    break;

                case PsAttributeToken:
                    /* Input: token handle */
                    TokenHandle = (HANDLE)AttributeList->Attributes[i].Value;
                    break;

                case PsAttributeClientId:
                    /* Output: pointer to CLIENT_ID to receive the result */
                    ClientIdPtr = (PCLIENT_ID)AttributeList->Attributes[i].ValuePtr;
                    break;

                case PsAttributeTebAddress:
                    /* Output: pointer to PTEB to receive the TEB address */
                    TebAddressPtr = (PTEB*)AttributeList->Attributes[i].ValuePtr;
                    break;

                case PsAttributeImageInfo:
                    /* Output: pointer to SECTION_IMAGE_INFORMATION */
                    ImageInfoPtr = (PSECTION_IMAGE_INFORMATION)AttributeList->Attributes[i].ValuePtr;
                    break;

                default:
                    /* Ignore unknown attributes for forward compatibility */
                    break;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* Image name is required */
    if (!ImageName.Buffer || !ImageName.Length)
    {
        DPRINT1("NtCreateUserProcess: No image name provided\n");
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Capture the image name string to kernel memory.
     *
     * ImageName was populated from the attribute list inside the SEH try
     * block above: its Length/MaximumLength are already captured to the
     * kernel stack, while its Buffer still points into user space.
     * We cannot use ProbeAndCaptureUnicodeString here because that
     * function expects a UNICODE_STRING residing in user memory (it
     * probes the structure itself).  Instead, probe and copy the
     * Buffer contents directly.
     */
    if (PreviousMode != KernelMode)
    {
        PWCHAR CapturedBuffer;

        _SEH2_TRY
        {
            /* Probe the user-mode buffer */
            ProbeForRead(ImageName.Buffer, ImageName.Length, sizeof(WCHAR));

            /* Allocate kernel memory for the captured string */
            CapturedBuffer = ExAllocatePoolWithTag(PagedPool,
                                                   ImageName.Length + sizeof(WCHAR),
                                                   'RTSU');
            if (!CapturedBuffer)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                _SEH2_LEAVE;
            }

            /* Copy and null-terminate */
            RtlCopyMemory(CapturedBuffer, ImageName.Buffer, ImageName.Length);
            CapturedBuffer[ImageName.Length / sizeof(WCHAR)] = UNICODE_NULL;

            CapturedImageName.Buffer = CapturedBuffer;
            CapturedImageName.Length = ImageName.Length;
            CapturedImageName.MaximumLength = ImageName.Length + sizeof(WCHAR);
            Status = STATUS_SUCCESS;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("NtCreateUserProcess: Failed to capture image name, Status=0x%lx\n", Status);
            return Status;
        }
    }
    else
    {
        CapturedImageName = ImageName;
    }

    DPRINT("NtCreateUserProcess: Image='%wZ'\n", &CapturedImageName);

    /*
     * Step 1: Open the image file
     */
    InitializeObjectAttributes(&LocalFileObjectAttributes,
                               &CapturedImageName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenFile(&hFile,
                        SYNCHRONIZE | FILE_EXECUTE | FILE_READ_DATA,
                        &LocalFileObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_DELETE | FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateUserProcess: Failed to open image '%wZ', Status=0x%lx\n",
                &CapturedImageName, Status);

        /* Report failure at file open stage */
        _SEH2_TRY
        {
            CreateInfo->State = PsCreateFailOnFileOpen;
            CreateInfo->FailSection.FileHandle = NULL;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        goto Cleanup;
    }

    /*
     * Step 2: Create an image section from the file
     */
    Status = ZwCreateSection(&hSection,
                             SECTION_ALL_ACCESS,
                             NULL,
                             NULL,
                             PAGE_EXECUTE,
                             SEC_IMAGE,
                             hFile);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateUserProcess: Failed to create section, Status=0x%lx\n", Status);

        /* Report failure at section creation stage */
        _SEH2_TRY
        {
            CreateInfo->State = PsCreateFailOnSectionCreate;
            CreateInfo->FailSection.FileHandle = hFile;
            hFile = NULL; /* Caller now owns this handle */
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        goto Cleanup;
    }

    /*
     * Step 3: Convert process flags to internal flags
     */
    if (ProcessFlags & PROCESS_CREATE_FLAGS_INHERIT_HANDLES)
        PspProcessFlags |= PROCESS_CREATE_FLAGS_INHERIT_HANDLES;
    if (ProcessFlags & PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT)
        PspProcessFlags |= PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT;
    if (ProcessFlags & PROCESS_CREATE_FLAGS_BREAKAWAY)
        PspProcessFlags |= PROCESS_CREATE_FLAGS_BREAKAWAY;

    /*
     * Step 4: Create the process via PspCreateProcess
     *
     * This is the same internal function used by NtCreateProcessEx.
     * It creates the EPROCESS, address space, PEB, and inserts the
     * process object.
     */
    Status = PspCreateProcess(&hProcess,
                              ProcessDesiredAccess,
                              ProcessObjectAttributes,
                              ParentProcess,
                              PspProcessFlags,
                              hSection,
                              DebugPort,
                              ExceptionPort,
                              FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateUserProcess: PspCreateProcess failed, Status=0x%lx\n", Status);
        goto Cleanup;
    }

    DPRINT("NtCreateUserProcess: Process created, handle=%p\n", hProcess);

    /*
     * Step 5a: Query image information from the section AFTER process creation.
     *
     * The section has now been mapped into the new process's address space
     * by PspCreateProcess -> MmInitializeProcessAddressSpace -> MmMapViewOfSection.
     * The TransferAddress in SECTION_IMAGE_INFORMATION is computed from the PE
     * header's preferred ImageBase + AddressOfEntryPoint at section creation time.
     * If the image was relocated (mapped at a different base), we must adjust
     * the TransferAddress accordingly.
     */
    Status = ZwQuerySection(hSection,
                            SectionImageInformation,
                            &ImageInformation,
                            sizeof(SECTION_IMAGE_INFORMATION),
                            NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateUserProcess: ZwQuerySection failed, Status=0x%lx\n", Status);
        goto Cleanup;
    }

    /*
     * Step 5b: Get PEB address and SectionBaseAddress from the newly created process.
     *
     * Reference the EPROCESS to get SectionBaseAddress. If the image was
     * relocated (mapped at a different address than the PE header's preferred
     * ImageBase), we must adjust TransferAddress to reflect the actual base.
     */
    Status = ZwQueryInformationProcess(hProcess,
                                       ProcessBasicInformation,
                                       &ProcessBasicInfo,
                                       sizeof(ProcessBasicInfo),
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateUserProcess: QueryInformationProcess failed, Status=0x%lx\n", Status);
        goto Cleanup;
    }

    /*
     * Get the actual section base address from the EPROCESS.
     * The TransferAddress from ZwQuerySection is: PreferredImageBase + AddressOfEntryPoint.
     * If the image was relocated, we need: ActualImageBase + AddressOfEntryPoint.
     * Compute: ActualTransfer = TransferAddress + (ActualBase - PreferredBase)
     */
    Status = ObReferenceObjectByHandle(hProcess,
                                       PROCESS_QUERY_INFORMATION,
                                       PsProcessType,
                                       KernelMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        PVOID ActualBase = Process->SectionBaseAddress;

        DPRINT("NtCreateUserProcess: TransferAddress=%p, SectionBaseAddress=%p\n",
               ImageInformation.TransferAddress, ActualBase);

        if (ActualBase && ImageInformation.TransferAddress)
        {
            /*
             * Compute the RVA of the entry point from the original TransferAddress.
             * TransferAddress = PreferredBase + EntryPointRVA
             * We need: ActualBase + EntryPointRVA
             *
             * Use SECTION_IMAGE_INFORMATION to get the preferred base indirectly:
             * We can query it from the image headers at the actual base, but since
             * we are in the parent process context, we need to work with what we have.
             *
             * The SectionBaseAddress IS the actual mapped base. The difference
             * between the preferred base (from PE header) and actual base gives us
             * the relocation delta. If the image mapped at preferred base, delta = 0.
             *
             * For now, get the preferred ImageBase from the section object.
             */
            PSECTION SectionObject;
            Status = ObReferenceObjectByHandle(hSection,
                                               SECTION_QUERY,
                                               MmSectionObjectType,
                                               KernelMode,
                                               (PVOID*)&SectionObject,
                                               NULL);
            if (NT_SUCCESS(Status))
            {
                PMM_IMAGE_SECTION_OBJECT ImageSectionObject =
                    (PMM_IMAGE_SECTION_OBJECT)SectionObject->Segment;
                PVOID PreferredBase = ImageSectionObject->BasedAddress;
                LONG_PTR Delta = (LONG_PTR)ActualBase - (LONG_PTR)PreferredBase;

                DPRINT("NtCreateUserProcess: PreferredBase=%p, ActualBase=%p, Delta=%p\n",
                       PreferredBase, ActualBase, (PVOID)Delta);

                if (Delta != 0)
                {
                    /* Image was relocated; adjust TransferAddress */
                    ImageInformation.TransferAddress =
                        (PVOID)((LONG_PTR)ImageInformation.TransferAddress + Delta);

                    DPRINT("NtCreateUserProcess: Adjusted TransferAddress=%p\n",
                            ImageInformation.TransferAddress);
                }

                ObDereferenceObject(SectionObject);
            }
            Status = STATUS_SUCCESS; /* Don't fail if section query fails */
        }

        ObDereferenceObject(Process);
        Process = NULL;
    }
    else
    {
        DPRINT1("NtCreateUserProcess: Failed to reference process, Status=0x%lx\n", Status);
        goto Cleanup;
    }

    /*
     * Step 6: Write process parameters into the new process's address space.
     *
     * If the caller provided ProcessParameters, initialize the environment
     * in the child process, similar to RtlpInitEnvironment.
     */
    if (ProcessParameters && ProcessBasicInfo.PebBaseAddress)
    {
        PVOID BaseAddress = NULL;
        SIZE_T RegionSize;
        PVOID EnvironmentBase = NULL;
        SIZE_T EnvSize;
        PVOID SavedEnvironment;
        BOOLEAN WasNormalized;

        /* Remember the original environment pointer before we modify anything */
        SavedEnvironment = ProcessParameters->Environment;

        /* First, write the environment block if present */
        if (ProcessParameters->Environment)
        {
            PWCHAR Env = (PWCHAR)ProcessParameters->Environment;
            while (*Env) { while (*Env++); }
            Env++;
            EnvSize = (SIZE_T)((ULONG_PTR)Env - (ULONG_PTR)ProcessParameters->Environment);

            RegionSize = EnvSize;
            Status = ZwAllocateVirtualMemory(hProcess,
                                             &EnvironmentBase,
                                             0,
                                             &RegionSize,
                                             MEM_RESERVE | MEM_COMMIT,
                                             PAGE_READWRITE);
            if (NT_SUCCESS(Status))
            {
                ZwWriteVirtualMemory(hProcess,
                                     EnvironmentBase,
                                     ProcessParameters->Environment,
                                     EnvSize,
                                     NULL);
            }
        }

        /* Allocate space for the parameters block in the child */
        RegionSize = ProcessParameters->MaximumLength;
        Status = ZwAllocateVirtualMemory(hProcess,
                                         &BaseAddress,
                                         0,
                                         &RegionSize,
                                         MEM_RESERVE | MEM_COMMIT,
                                         PAGE_READWRITE);
        if (NT_SUCCESS(Status))
        {
            /*
             * The caller (RtlCreateUserProcess) passes normalized parameters
             * where string Buffer pointers are absolute addresses in the parent
             * process. We need to denormalize them (convert to offsets relative
             * to the parameters base) before writing to the child, because the
             * child's LDR will call RtlNormalizeProcessParams to convert them
             * to absolute addresses in the child's address space.
             *
             * We temporarily denormalize in place, write, then restore.
             */
            WasNormalized = (ProcessParameters->Flags & RTL_USER_PROCESS_PARAMETERS_NORMALIZED) != 0;

            /*
             * Set Environment to the child's copy. Environment is NOT part of
             * the normalize/denormalize set; it is always an absolute pointer.
             */
            ProcessParameters->Environment = EnvironmentBase;

            if (WasNormalized)
            {
                /* Denormalize: convert absolute parent pointers to relative offsets */
                #define DENORM_FIELD(field) \
                    if (ProcessParameters->field) \
                        ProcessParameters->field = (PVOID)((ULONG_PTR)ProcessParameters->field - (ULONG_PTR)ProcessParameters)

                DENORM_FIELD(CurrentDirectory.DosPath.Buffer);
                DENORM_FIELD(DllPath.Buffer);
                DENORM_FIELD(ImagePathName.Buffer);
                DENORM_FIELD(CommandLine.Buffer);
                DENORM_FIELD(WindowTitle.Buffer);
                DENORM_FIELD(DesktopInfo.Buffer);
                DENORM_FIELD(ShellInfo.Buffer);
                DENORM_FIELD(RuntimeData.Buffer);

                #undef DENORM_FIELD

                ProcessParameters->Flags &= ~RTL_USER_PROCESS_PARAMETERS_NORMALIZED;
            }

            /* Write the denormalized parameters to the child */
            ZwWriteVirtualMemory(hProcess,
                                 BaseAddress,
                                 ProcessParameters,
                                 ProcessParameters->Length,
                                 NULL);

            /* Restore the parent's parameters to their original state */
            if (WasNormalized)
            {
                #define RENORM_FIELD(field) \
                    if (ProcessParameters->field) \
                        ProcessParameters->field = (PVOID)((ULONG_PTR)ProcessParameters->field + (ULONG_PTR)ProcessParameters)

                RENORM_FIELD(CurrentDirectory.DosPath.Buffer);
                RENORM_FIELD(DllPath.Buffer);
                RENORM_FIELD(ImagePathName.Buffer);
                RENORM_FIELD(CommandLine.Buffer);
                RENORM_FIELD(WindowTitle.Buffer);
                RENORM_FIELD(DesktopInfo.Buffer);
                RENORM_FIELD(ShellInfo.Buffer);
                RENORM_FIELD(RuntimeData.Buffer);

                #undef RENORM_FIELD

                ProcessParameters->Flags |= RTL_USER_PROCESS_PARAMETERS_NORMALIZED;
            }

            /* Restore the parent's original environment pointer */
            ProcessParameters->Environment = SavedEnvironment;

            /* Write the pointer to parameters into the PEB */
            ZwWriteVirtualMemory(hProcess,
                                 &ProcessBasicInfo.PebBaseAddress->ProcessParameters,
                                 &BaseAddress,
                                 sizeof(BaseAddress),
                                 NULL);
        }
        else
        {
            DPRINT1("NtCreateUserProcess: Failed to allocate process parameters, Status=0x%lx\n", Status);
        }

        /* Reset Status since parameter writing is not critical */
        Status = STATUS_SUCCESS;
    }

    /*
     * Step 7: Create the initial thread.
     *
     * We use PspCreateThread, which is the same internal function used by
     * NtCreateThread. We need to build up a CONTEXT and INITIAL_TEB for it.
     *
     * The approach mirrors what RtlCreateUserThread does:
     * 1. Create the user-mode stack
     * 2. Initialize the context with the image entry point
     * 3. Call PspCreateThread
     */

    /*
     * Set up the initial thread stack.
     *
     * This mirrors RtlpCreateUserStack: reserve the full stack, then commit
     * the initial portion plus one guard page at the bottom. The guard page
     * is included in the committed size so that the usable committed area
     * equals the requested commit size.
     */
    {
        PVOID StackBase = NULL;
        ULONG_PTR StackTop;
        SIZE_T StackReserve = ImageInformation.MaximumStackSize;
        SIZE_T StackCommit = ImageInformation.CommittedStackSize;
        SIZE_T GuardPageSize = PAGE_SIZE;
        BOOLEAN UseGuard;

        /* Ensure reasonable defaults */
        if (StackReserve == 0) StackReserve = 0x100000;  /* 1MB default */
        if (StackCommit == 0) StackCommit = PAGE_SIZE;

        /* Ensure commit does not exceed reserve */
        if (StackCommit >= StackReserve)
        {
            StackReserve = ROUND_UP(StackCommit, 1024 * 1024);
        }

        /* Align to page boundaries */
        StackCommit = ROUND_UP(StackCommit, PAGE_SIZE);
        StackReserve = ROUND_UP(StackReserve, 64 * 1024); /* 64KB allocation granularity */

        /* Reserve stack memory */
        Status = ZwAllocateVirtualMemory(hProcess,
                                         &StackBase,
                                         0,
                                         &StackReserve,
                                         MEM_RESERVE,
                                         PAGE_READWRITE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("NtCreateUserProcess: Failed to reserve stack, Status=0x%lx\n", Status);
            goto Cleanup;
        }

        /* Calculate top of stack (highest address) */
        StackTop = (ULONG_PTR)StackBase + StackReserve;

        /*
         * Add a guard page if there's room. The guard page is included in the
         * total commit so the usable committed area stays at StackCommit bytes.
         */
        UseGuard = (StackReserve >= StackCommit + PAGE_SIZE);
        if (UseGuard)
        {
            StackCommit += PAGE_SIZE;
        }

        /* Commit from (StackTop - StackCommit) up to StackTop */
        {
            PVOID CommitBase = (PVOID)(StackTop - StackCommit);
            SIZE_T CommitSize = StackCommit;
            Status = ZwAllocateVirtualMemory(hProcess,
                                             &CommitBase,
                                             0,
                                             &CommitSize,
                                             MEM_COMMIT,
                                             PAGE_READWRITE);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("NtCreateUserProcess: Failed to commit stack, Status=0x%lx\n", Status);
                goto Cleanup;
            }
        }

        /* Set up a guard page at the bottom of the committed region */
        if (UseGuard)
        {
            PVOID GuardBase = (PVOID)(StackTop - StackCommit);
            ULONG OldProtect;
            ZwProtectVirtualMemory(hProcess,
                                   &GuardBase,
                                   &GuardPageSize,
                                   PAGE_READWRITE | PAGE_GUARD,
                                   &OldProtect);
        }

        /* Fill in the InitialTeb */
        RtlZeroMemory(&InitialTeb, sizeof(INITIAL_TEB));
        InitialTeb.AllocatedStackBase = StackBase;
        InitialTeb.StackBase = (PVOID)StackTop;

        /*
         * StackLimit points above the guard page, matching RtlpCreateUserStack.
         * The guard page sits at StackTop - StackCommit; usable stack starts
         * one page above that.
         */
        if (UseGuard)
        {
            InitialTeb.StackLimit = (PVOID)(StackTop - StackCommit + PAGE_SIZE);
        }
        else
        {
            InitialTeb.StackLimit = (PVOID)(StackTop - StackCommit);
        }
        InitialTeb.PreviousStackBase = NULL;
        InitialTeb.PreviousStackLimit = NULL;
    }

    /* Initialize the thread context */
    RtlZeroMemory(&ThreadContext, sizeof(CONTEXT));
    ThreadContext.ContextFlags = CONTEXT_FULL;

#ifdef _M_AMD64
    /* AMD64: Set up initial context for the thread */
    ThreadContext.Rip = (ULONG64)ImageInformation.TransferAddress;
    ThreadContext.Rsp = (ULONG64)InitialTeb.StackBase - 6 * sizeof(PVOID);
    ThreadContext.Rsp &= ~15ULL;
    ThreadContext.Rsp -= 8;  /* Unaligned on function entry per ABI */
    ThreadContext.EFlags = EFLAGS_INTERRUPT_MASK;
    ThreadContext.Rcx = (ULONG64)ProcessBasicInfo.PebBaseAddress;

    /* Set user-mode segments */
    ThreadContext.SegCs = KGDT64_R3_CODE | RPL_MASK;
    ThreadContext.SegDs = KGDT64_R3_DATA | RPL_MASK;
    ThreadContext.SegEs = KGDT64_R3_DATA | RPL_MASK;
    ThreadContext.SegFs = KGDT64_R3_CMTEB | RPL_MASK;
    ThreadContext.SegGs = KGDT64_R3_DATA | RPL_MASK;
    ThreadContext.SegSs = KGDT64_R3_DATA | RPL_MASK;
    ThreadContext.MxCsr = INITIAL_MXCSR;
#elif defined(_M_IX86)
    /* x86: Set up initial context for the thread */
    ThreadContext.Eip = (ULONG)ImageInformation.TransferAddress;
    ThreadContext.Esp = (ULONG)(ULONG_PTR)InitialTeb.StackBase - sizeof(PVOID);
    ThreadContext.EFlags = EFLAGS_INTERRUPT_MASK;

    /* Push PEB address as parameter */
    {
        ULONG PebParam = (ULONG)(ULONG_PTR)ProcessBasicInfo.PebBaseAddress;
        PVOID WriteAddr = (PVOID)(ULONG_PTR)(ThreadContext.Esp);
        ZwWriteVirtualMemory(hProcess, WriteAddr, &PebParam, sizeof(PebParam), NULL);
        ThreadContext.Esp -= sizeof(ULONG); /* Return address placeholder */
    }

    /* Set user-mode segments */
    ThreadContext.SegCs = KGDT_R3_CODE | RPL_MASK;
    ThreadContext.SegDs = KGDT_R3_DATA | RPL_MASK;
    ThreadContext.SegEs = KGDT_R3_DATA | RPL_MASK;
    ThreadContext.SegFs = KGDT_R3_TEB | RPL_MASK;
    ThreadContext.SegGs = 0;
    ThreadContext.SegSs = KGDT_R3_DATA | RPL_MASK;
#elif defined(_M_ARM64)
    /* ARM64: Set up initial context */
    ThreadContext.Pc = (ULONG64)ImageInformation.TransferAddress;
    ThreadContext.Sp = (ULONG64)InitialTeb.StackBase;
    ThreadContext.Sp &= ~15ULL;
    ThreadContext.X0 = (ULONG64)ProcessBasicInfo.PebBaseAddress;
    ThreadContext.Cpsr = 0;  /* User mode */
#elif defined(_M_ARM)
    /* ARM: Set up initial context */
    ThreadContext.Pc = (ULONG)ImageInformation.TransferAddress;
    ThreadContext.Sp = (ULONG)(ULONG_PTR)InitialTeb.StackBase;
    ThreadContext.R0 = (ULONG)(ULONG_PTR)ProcessBasicInfo.PebBaseAddress;
    ThreadContext.Cpsr = 0x10;  /* User mode */
#else
#error "Unsupported architecture"
#endif

    /* Create the initial thread via PspCreateThread */
    Status = PspCreateThread(&hThread,
                             ThreadDesiredAccess,
                             ThreadObjectAttributes,
                             hProcess,
                             NULL,
                             &ClientId,
                             &ThreadContext,
                             &InitialTeb,
                             (ThreadFlags & THREAD_CREATE_FLAGS_CREATE_SUSPENDED) ? TRUE : FALSE,
                             NULL,
                             NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateUserProcess: PspCreateThread failed, Status=0x%lx\n", Status);
        goto Cleanup;
    }

    DPRINT("NtCreateUserProcess: Thread created, handle=%p, TID=%p\n",
           hThread, ClientId.UniqueThread);

    /*
     * Step 8: Fill output structures
     */
    _SEH2_TRY
    {
        /* Return process and thread handles */
        *ProcessHandle = hProcess;
        *ThreadHandle = hThread;

        /* Fill the CreateInfo success output */
        CreateInfo->State = PsCreateSuccess;
        CreateInfo->SuccessState.OutputFlags = 0;
        CreateInfo->SuccessState.FileHandle = hFile;
        CreateInfo->SuccessState.SectionHandle = hSection;
        CreateInfo->SuccessState.UserProcessParametersNative = 0;
        CreateInfo->SuccessState.UserProcessParametersWow64 = 0;
        CreateInfo->SuccessState.CurrentParameterFlags = 0;
        CreateInfo->SuccessState.PebAddressNative = (ULONGLONG)(ULONG_PTR)ProcessBasicInfo.PebBaseAddress;
        CreateInfo->SuccessState.PebAddressWow64 = 0;
        CreateInfo->SuccessState.ManifestAddress = 0;
        CreateInfo->SuccessState.ManifestSize = 0;

        /* Write back CLIENT_ID if requested */
        if (ClientIdPtr)
        {
            if (PreviousMode != KernelMode)
                ProbeForWrite(ClientIdPtr, sizeof(CLIENT_ID), sizeof(ULONG));
            *ClientIdPtr = ClientId;
        }

        /* Write back image information if requested */
        if (ImageInfoPtr)
        {
            if (PreviousMode != KernelMode)
                ProbeForWrite(ImageInfoPtr, sizeof(SECTION_IMAGE_INFORMATION), sizeof(ULONG));
            *ImageInfoPtr = ImageInformation;
        }

        /* We do not write TEB address here; it was set by PspCreateThread internally */
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* On success, the caller owns the file and section handles */
    if (NT_SUCCESS(Status))
    {
        hFile = NULL;
        hSection = NULL;
        hProcess = NULL;
        hThread = NULL;
    }

Cleanup:
    /* Release captured image name */
    if (PreviousMode != KernelMode && CapturedImageName.Buffer != ImageName.Buffer)
    {
        ReleaseCapturedUnicodeString(&CapturedImageName, PreviousMode);
    }

    /* Close kernel handles on failure */
    if (hThread) ZwClose(hThread);
    if (hProcess) ZwClose(hProcess);
    if (hSection) ZwClose(hSection);
    if (hFile) ZwClose(hFile);

    return Status;
}
#endif /* NTDDI_VERSION >= NTDDI_LONGHORN */

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtOpenProcess(OUT PHANDLE ProcessHandle,
              IN ACCESS_MASK DesiredAccess,
              IN POBJECT_ATTRIBUTES ObjectAttributes,
              IN PCLIENT_ID ClientId)
{
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    CLIENT_ID SafeClientId;
    ULONG Attributes = 0;
    HANDLE hProcess;
    BOOLEAN HasObjectName = FALSE;
    PETHREAD Thread = NULL;
    PEPROCESS Process = NULL;
    NTSTATUS Status;
    ACCESS_STATE AccessState;
    AUX_ACCESS_DATA AuxData;
    PAGED_CODE();
    PSTRACE(PS_PROCESS_DEBUG,
            "ClientId: %p Attributes: %p\n", ClientId, ObjectAttributes);

    /* Check if we were called from user mode */
    if (PreviousMode != KernelMode)
    {
        /* Enter SEH for probing */
        _SEH2_TRY
        {
            /* Probe the thread handle */
            ProbeForWriteHandle(ProcessHandle);

            /* Check for a CID structure */
            if (ClientId)
            {
                /* Probe and capture it */
                ProbeForRead(ClientId, sizeof(CLIENT_ID), sizeof(ULONG));
                SafeClientId = *ClientId;
                ClientId = &SafeClientId;
            }

            /*
             * Just probe the object attributes structure, don't capture it
             * completely. This is done later if necessary
             */
            ProbeForRead(ObjectAttributes,
                         sizeof(OBJECT_ATTRIBUTES),
                         sizeof(ULONG));
            HasObjectName = (ObjectAttributes->ObjectName != NULL);

            /* Validate user attributes */
            Attributes = ObpValidateAttributes(ObjectAttributes->Attributes, PreviousMode);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        /* Otherwise just get the data directly */
        HasObjectName = (ObjectAttributes->ObjectName != NULL);

        /* Still have to sanitize attributes */
        Attributes = ObpValidateAttributes(ObjectAttributes->Attributes, PreviousMode);
    }

    /* Can't pass both, fail */
    if ((HasObjectName) && (ClientId)) return STATUS_INVALID_PARAMETER_MIX;

    /* Create an access state */
    Status = SeCreateAccessState(&AccessState,
                                 &AuxData,
                                 DesiredAccess,
                                 &PsProcessType->TypeInfo.GenericMapping);
    if (!NT_SUCCESS(Status)) return Status;

    /* Check if this is a debugger */
    if (SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
    {
        /* Did he want full access? */
        if (AccessState.RemainingDesiredAccess & MAXIMUM_ALLOWED)
        {
            /* Give it to him */
            AccessState.PreviouslyGrantedAccess |= PROCESS_ALL_ACCESS;
        }
        else
        {
            /* Otherwise just give every other access he could want */
            AccessState.PreviouslyGrantedAccess |=
                AccessState.RemainingDesiredAccess;
        }

        /* The caller desires nothing else now */
        AccessState.RemainingDesiredAccess = 0;
    }

    /* Open by name if one was given */
    if (HasObjectName)
    {
        /* Open it */
        Status = ObOpenObjectByName(ObjectAttributes,
                                    PsProcessType,
                                    PreviousMode,
                                    &AccessState,
                                    0,
                                    NULL,
                                    &hProcess);

        /* Get rid of the access state */
        SeDeleteAccessState(&AccessState);
    }
    else if (ClientId)
    {
        /* Open by Thread ID */
        if (ClientId->UniqueThread)
        {
            /* Get the Process */
            Status = PsLookupProcessThreadByCid(ClientId, &Process, &Thread);
        }
        else
        {
            /* Get the Process */
            Status = PsLookupProcessByProcessId(ClientId->UniqueProcess,
                                                &Process);
        }

        /* Check if we didn't find anything */
        if (!NT_SUCCESS(Status))
        {
            /* Get rid of the access state and return */
            SeDeleteAccessState(&AccessState);
            return Status;
        }

        /* Open the Process Object */
        Status = ObOpenObjectByPointer(Process,
                                       Attributes,
                                       &AccessState,
                                       0,
                                       PsProcessType,
                                       PreviousMode,
                                       &hProcess);

        /* Delete the access state */
        SeDeleteAccessState(&AccessState);

        /* Dereference the thread if we used it */
        if (Thread) ObDereferenceObject(Thread);

        /* Dereference the Process */
        ObDereferenceObject(Process);
    }
    else
    {
        /* neither an object name nor a client id was passed */
        return STATUS_INVALID_PARAMETER_MIX;
    }

    /* Check for success */
    if (NT_SUCCESS(Status))
    {
        /* Use SEH for write back */
        _SEH2_TRY
        {
            /* Write back the handle */
            *ProcessHandle = hProcess;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Get the exception code */
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    /* Return status */
    return Status;
}

/* EOF */
