/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ps/psmgr.c
 * PURPOSE:         Process Manager: Initialization Code
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <reactos/wow64shared.h>
#define NDEBUG
#include <debug.h>

extern ULONG ExpInitializationPhase;

PVOID KeUserPopEntrySListEnd;
PVOID KeUserPopEntrySListFault;
PVOID KeUserPopEntrySListResume;

GENERIC_MAPPING PspProcessMapping =
{
    STANDARD_RIGHTS_READ    | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
    STANDARD_RIGHTS_WRITE   | PROCESS_CREATE_PROCESS    | PROCESS_CREATE_THREAD   |
    PROCESS_VM_OPERATION    | PROCESS_VM_WRITE          | PROCESS_DUP_HANDLE      |
    PROCESS_TERMINATE       | PROCESS_SET_QUOTA         | PROCESS_SET_INFORMATION |
    PROCESS_SUSPEND_RESUME,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    PROCESS_ALL_ACCESS
};

GENERIC_MAPPING PspThreadMapping =
{
    STANDARD_RIGHTS_READ    | THREAD_GET_CONTEXT      | THREAD_QUERY_INFORMATION,
    STANDARD_RIGHTS_WRITE   | THREAD_TERMINATE        | THREAD_SUSPEND_RESUME    |
    THREAD_ALERT            | THREAD_SET_INFORMATION  | THREAD_SET_CONTEXT,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    THREAD_ALL_ACCESS
};

PVOID PspSystemDllBase;
PVOID PspSystemDllSection;
PVOID PspSystemDllEntryPoint;

UNICODE_STRING PsNtDllPathName =
    RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\ntdll.dll");

PHANDLE_TABLE PspCidTable;

PEPROCESS PsInitialSystemProcess = NULL;
PEPROCESS PsIdleProcess = NULL;
HANDLE PspInitialSystemProcessHandle = NULL;

ULONG PsMinimumWorkingSet, PsMaximumWorkingSet;
struct
{
    LIST_ENTRY List;
    KGUARDED_MUTEX Lock;
} PspWorkingSetChangeHead;
ULONG PspDefaultPagedLimit, PspDefaultNonPagedLimit, PspDefaultPagefileLimit;
BOOLEAN PspDoingGiveBacks;

/* PRIVATE FUNCTIONS *********************************************************/

static CODE_SEG("INIT")
NTSTATUS
PspLookupSystemDllEntryPoint(
    _In_ PCSTR Name,
    _Out_ PVOID* EntryPoint)
{
    /* Call the internal API */
    return RtlpFindExportedRoutineByName(PspSystemDllBase,
                                         Name,
                                         EntryPoint,
                                         NULL,
                                         STATUS_PROCEDURE_NOT_FOUND);
}

static CODE_SEG("INIT")
NTSTATUS
PspLookupKernelUserEntryPoints(VOID)
{
    NTSTATUS Status;

    /* Get user-mode APC trampoline */
    Status = PspLookupSystemDllEntryPoint("KiUserApcDispatcher",
                                          &KeUserApcDispatcher);
    if (!NT_SUCCESS(Status)) return Status;

    /* Get user-mode exception dispatcher */
    Status = PspLookupSystemDllEntryPoint("KiUserExceptionDispatcher",
                                          &KeUserExceptionDispatcher);
    if (!NT_SUCCESS(Status)) return Status;

    /* Get user-mode callback dispatcher */
    Status = PspLookupSystemDllEntryPoint("KiUserCallbackDispatcher",
                                          &KeUserCallbackDispatcher);
    if (!NT_SUCCESS(Status)) return Status;

    /* Get user-mode exception raise trampoline */
    Status = PspLookupSystemDllEntryPoint("KiRaiseUserExceptionDispatcher",
                                          &KeRaiseUserExceptionDispatcher);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * Get user-mode SLIST exception functions for page fault rollback race hack.
     *
     * These exports are labels within the assembly SLIST pop code that mark:
     * - ExpInterlockedPopEntrySListResume: start of retry loop
     * - ExpInterlockedPopEntrySListFault: point where page fault can occur
     * - ExpInterlockedPopEntrySListEnd: end of critical section
     *
     * The kernel uses these to detect page faults during SLIST operations and
     * restart the operation if the list header changed.
     *
     * ARM64 uses a C-based SLIST implementation with proper atomic operations
     * (__atomic_compare_exchange_n for 128-bit CAS) which doesn't have this
     * race condition. The C code handles all necessary memory barriers and
     * retry logic internally, so these markers are not needed or exported.
     */
#if !defined(_M_ARM64)
    Status = PspLookupSystemDllEntryPoint("ExpInterlockedPopEntrySListEnd",
                                          &KeUserPopEntrySListEnd);
    if (!NT_SUCCESS(Status)) return Status;
    Status = PspLookupSystemDllEntryPoint("ExpInterlockedPopEntrySListFault",
                                          &KeUserPopEntrySListFault);
    if (!NT_SUCCESS(Status)) return Status;
    Status = PspLookupSystemDllEntryPoint("ExpInterlockedPopEntrySListResume",
                                          &KeUserPopEntrySListResume);
    if (!NT_SUCCESS(Status)) return Status;
#else
    /* ARM64: Set to NULL - not used on this architecture */
    KeUserPopEntrySListEnd = NULL;
    KeUserPopEntrySListFault = NULL;
    KeUserPopEntrySListResume = NULL;
#endif

#if defined(_M_AMD64)
    /*
     * Clear the WOW64 dispatcher slots. They will be populated later by the
     * WOW64 user-mode bootstrap once 32-bit thunks are available.
     */
    {
        ULONG i;
        for (i = 0; i < RTL_NUMBER_OF(SharedUserData->Wow64SharedInformation); i++)
        {
            SharedUserData->Wow64SharedInformation[i] = 0;
        }
    }
#endif

    /* On x86, there are multiple ways to do a system call, find the right stubs */
#if defined(_X86_)
    /* Check if this is a machine that supports SYSENTER */
    if (KeFeatureBits & KF_FAST_SYSCALL)
    {
        /* Get user-mode sysenter stub */
        SharedUserData->SystemCall = (PsNtosImageBase >> (PAGE_SHIFT + 1));
        Status = PspLookupSystemDllEntryPoint("KiFastSystemCall",
                                              (PVOID)&SharedUserData->
                                              SystemCall);
        if (!NT_SUCCESS(Status)) return Status;

        /* Get user-mode sysenter return stub */
        Status = PspLookupSystemDllEntryPoint("KiFastSystemCallRet",
                                              (PVOID)&SharedUserData->
                                              SystemCallReturn);
        if (!NT_SUCCESS(Status)) return Status;
    }
    else
    {
        /* Get the user-mode interrupt stub */
        Status = PspLookupSystemDllEntryPoint("KiIntSystemCall",
                                              (PVOID)&SharedUserData->
                                              SystemCall);
        if (!NT_SUCCESS(Status)) return Status;
    }

    /* Set the test instruction */
    SharedUserData->TestRetInstruction = 0xC3;
#endif

    /* Return the status */
    return Status;
}

NTSTATUS
NTAPI
PspMapSystemDll(IN PEPROCESS Process,
                IN PVOID *DllBase,
                IN BOOLEAN UseLargePages)
{
    NTSTATUS Status;
    LARGE_INTEGER Offset = {{0, 0}};
    SIZE_T ViewSize = 0;
    PVOID ImageBase = 0;

    /* Map the System DLL */
    Status = MmMapViewOfSection(PspSystemDllSection,
                                Process,
                                (PVOID*)&ImageBase,
                                0,
                                0,
                                &Offset,
                                &ViewSize,
                                ViewShare,
                                0,
                                PAGE_READWRITE);
    if (Status != STATUS_SUCCESS)
    {
        /* Normalize status code */
        Status = STATUS_CONFLICTING_ADDRESSES;
    }

    /* Write the image base and return status */
    if (DllBase) *DllBase = ImageBase;
    return Status;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
PsLocateSystemDll(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle, SectionHandle;
    NTSTATUS Status;
    ULONG_PTR HardErrorParameters;
    ULONG HardErrorResponse;

#if defined(_M_ARM64)
    DPRINT1("========== ARM64 DEADLOCK TRACE: PsLocateSystemDll START ==========\n");
    DPRINT1("[arm64] PsLocateSystemDll: ENTRY at IRQL=%u Thread=%p\n",
            KeGetCurrentIrql(), PsGetCurrentThread());
    DPRINT1("[arm64] PsLocateSystemDll: Thread TID=%p PID=%p State=%u WaitReason=%u\n",
            PsGetThreadId(PsGetCurrentThread()),
            PsGetThreadProcessId(PsGetCurrentThread()),
            PsGetCurrentThread()->Tcb.State,
            PsGetCurrentThread()->Tcb.WaitReason);
    DPRINT1("[arm64] PsLocateSystemDll: Opening file %wZ\n", &PsNtDllPathName);
#endif

    /* Locate and open NTDLL to determine ImageBase and LdrStartup */
    InitializeObjectAttributes(&ObjectAttributes,
                               &PsNtDllPathName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

#if defined(_M_ARM64)
    DPRINT1("[arm64] PsLocateSystemDll: *** ABOUT TO CALL ZwOpenFile ***\n");
    DPRINT1("[arm64] PsLocateSystemDll: Pre-call IRQL=%u Thread State=%u\n",
            KeGetCurrentIrql(), PsGetCurrentThread()->Tcb.State);
    /* Memory barrier to ensure all previous operations are visible */
    __asm__ __volatile__("dmb sy" ::: "memory");
#endif

    Status = ZwOpenFile(&FileHandle,
                        FILE_READ_ACCESS,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        0);

#if defined(_M_ARM64)
    DPRINT1("[arm64] PsLocateSystemDll: *** ZwOpenFile RETURNED ***\n");
    DPRINT1("[arm64] PsLocateSystemDll: Status=0x%lx IRQL=%u\n", Status, KeGetCurrentIrql());
    DPRINT1("[arm64] PsLocateSystemDll: Thread State=%u WaitReason=%u\n",
            PsGetCurrentThread()->Tcb.State, PsGetCurrentThread()->Tcb.WaitReason);
    DPRINT1("[arm64] PsLocateSystemDll: FileHandle=%p IoStatusBlock.Status=0x%lx Info=0x%lx\n",
            FileHandle, IoStatusBlock.Status, IoStatusBlock.Information);
#endif

    if (!NT_SUCCESS(Status))
    {
#if defined(_M_ARM64)
        DPRINT1("[arm64] PsLocateSystemDll: ZwOpenFile FAILED with status 0x%lx\n", Status);
        DPRINT1("========== ARM64 DEADLOCK TRACE: PsLocateSystemDll FAILED ==========\n");
#endif
        /* Failed, bugcheck */
        KeBugCheckEx(PROCESS1_INITIALIZATION_FAILED, Status, 2, 0, 0);
    }

#if defined(_M_ARM64)
    DPRINT1("[arm64] PsLocateSystemDll: File opened successfully, checking image\n");
#endif

    /* Check if the image is valid */
#if defined(_M_ARM64)
    DPRINT1("[arm64] PsLocateSystemDll: *** Calling MmCheckSystemImage, IRQL=%u ***\n",
            KeGetCurrentIrql());
#endif

    Status = MmCheckSystemImage(FileHandle);

#if defined(_M_ARM64)
    DPRINT1("[arm64] PsLocateSystemDll: *** MmCheckSystemImage returned 0x%lx ***\n", Status);
#endif

    if (Status == STATUS_IMAGE_CHECKSUM_MISMATCH || Status == STATUS_INVALID_IMAGE_PROTECT)
    {
        /* Raise a hard error */
        HardErrorParameters = (ULONG_PTR)&PsNtDllPathName;
        NtRaiseHardError(Status,
                         1,
                         1,
                         &HardErrorParameters,
                         OptionOk,
                         &HardErrorResponse);
        return Status;
    }

    /* Create a section for NTDLL */
#if defined(_M_ARM64)
    DPRINT1("[arm64] PsLocateSystemDll: *** Creating section, IRQL=%u ***\n",
            KeGetCurrentIrql());
#endif

    Status = ZwCreateSection(&SectionHandle,
                             SECTION_ALL_ACCESS,
                             NULL,
                             NULL,
                             PAGE_EXECUTE,
                             SEC_IMAGE,
                             FileHandle);
    ZwClose(FileHandle);

#if defined(_M_ARM64)
    DPRINT1("[arm64] PsLocateSystemDll: *** ZwCreateSection returned 0x%lx ***\n", Status);
#endif

    if (!NT_SUCCESS(Status))
    {
#if defined(_M_ARM64)
        DPRINT1("[arm64] PsLocateSystemDll: ZwCreateSection FAILED\n");
        DPRINT1("========== ARM64 DEADLOCK TRACE: PsLocateSystemDll FAILED ==========\n");
#endif
        /* Failed, bugcheck */
        KeBugCheckEx(PROCESS1_INITIALIZATION_FAILED, Status, 3, 0, 0);
    }

#if defined(_M_ARM64)
    DPRINT1("[arm64] PsLocateSystemDll: Referencing section object\n");
#endif

    /* Reference the Section */
    Status = ObReferenceObjectByHandle(SectionHandle,
                                       SECTION_ALL_ACCESS,
                                       MmSectionObjectType,
                                       KernelMode,
                                       (PVOID*)&PspSystemDllSection,
                                       NULL);
    ZwClose(SectionHandle);
    if (!NT_SUCCESS(Status))
    {
#if defined(_M_ARM64)
        DPRINT1("[arm64] PsLocateSystemDll: ObReferenceObjectByHandle FAILED with 0x%lx\n", Status);
        DPRINT1("========== ARM64 DEADLOCK TRACE: PsLocateSystemDll FAILED ==========\n");
#endif
        /* Failed, bugcheck */
        KeBugCheckEx(PROCESS1_INITIALIZATION_FAILED, Status, 4, 0, 0);
    }

#if defined(_M_ARM64)
    DPRINT1("[arm64] PsLocateSystemDll: Mapping system DLL into current process\n");
#endif

    /* Map it */
    Status = PspMapSystemDll(PsGetCurrentProcess(), &PspSystemDllBase, FALSE);
    if (!NT_SUCCESS(Status))
    {
#if defined(_M_ARM64)
        DPRINT1("[arm64] PsLocateSystemDll: PspMapSystemDll FAILED with 0x%lx\n", Status);
        DPRINT1("========== ARM64 DEADLOCK TRACE: PsLocateSystemDll FAILED ==========\n");
#endif
        /* Failed, bugcheck */
        KeBugCheckEx(PROCESS1_INITIALIZATION_FAILED, Status, 5, 0, 0);
    }

#if defined(_M_ARM64)
    DPRINT1("[arm64] PsLocateSystemDll: SUCCESS! PspSystemDllBase=%p\n", PspSystemDllBase);
    DPRINT1("========== ARM64 DEADLOCK TRACE: PsLocateSystemDll COMPLETE ==========\n");
#endif

    /* Return status */
    return Status;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
PspInitializeSystemDll(VOID)
{
    NTSTATUS Status;

    /* Get user-mode startup thunk */
    Status = PspLookupSystemDllEntryPoint("LdrInitializeThunk",
                                          &PspSystemDllEntryPoint);
    if (!NT_SUCCESS(Status))
    {
        /* Failed, bugcheck */
        KeBugCheckEx(PROCESS1_INITIALIZATION_FAILED, Status, 7, 0, 0);
    }

    /* Get all the other entrypoints */
    Status = PspLookupKernelUserEntryPoints();
    if (!NT_SUCCESS(Status))
    {
        /* Failed, bugcheck */
        KeBugCheckEx(PROCESS1_INITIALIZATION_FAILED, Status, 8, 0, 0);
    }

    /* Let KD know we are done */
    KdUpdateDataBlock();

    /* Return status */
    return Status;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PspInitPhase1(VOID)
{
    /* Initialize the System DLL and return status of operation */
    if (!NT_SUCCESS(PspInitializeSystemDll())) return FALSE;
    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PspInitPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE SysThreadHandle;
    PETHREAD SysThread;
    MM_SYSTEMSIZE SystemSize;
    UNICODE_STRING Name;
    ULONG i;

    /* Get the system size */
    SystemSize = MmQuerySystemSize();

    /* Setup some memory options */
    PspDefaultPagefileLimit = -1;
    switch (SystemSize)
    {
        /* Medimum systems */
        case MmMediumSystem:

            /* Increase the WS sizes a bit */
            PsMinimumWorkingSet += 10;
            PsMaximumWorkingSet += 100;

        /* Large systems */
        case MmLargeSystem:

            /* Increase the WS sizes a bit more */
            PsMinimumWorkingSet += 30;
            PsMaximumWorkingSet += 300;

        /* Small and other systems */
        default:
            break;
    }

    /* Setup callbacks */
    for (i = 0; i < PSP_MAX_CREATE_THREAD_NOTIFY; i++)
    {
        ExInitializeCallBack(&PspThreadNotifyRoutine[i]);
    }
    for (i = 0; i < PSP_MAX_CREATE_PROCESS_NOTIFY; i++)
    {
        ExInitializeCallBack(&PspProcessNotifyRoutine[i]);
    }
    for (i = 0; i < PSP_MAX_LOAD_IMAGE_NOTIFY; i++)
    {
        ExInitializeCallBack(&PspLoadImageNotifyRoutine[i]);
    }

    /* Setup the quantum table */
    PsChangeQuantumTable(FALSE, PsRawPrioritySeparation);

    /* Set quota settings */
    if (!PspDefaultPagedLimit) PspDefaultPagedLimit = 0;
    if (!PspDefaultNonPagedLimit) PspDefaultNonPagedLimit = 0;
    if (!(PspDefaultNonPagedLimit) && !(PspDefaultPagedLimit))
    {
        /* Enable give-backs */
        PspDoingGiveBacks = TRUE;
    }
    else
    {
        /* Disable them */
        PspDoingGiveBacks = FALSE;
    }

    /* Now multiply limits by 1MB */
    PspDefaultPagedLimit <<= 20;
    PspDefaultNonPagedLimit <<= 20;
    if (PspDefaultPagefileLimit != MAXULONG) PspDefaultPagefileLimit <<= 20;

    /* Initialize the Active Process List */
    InitializeListHead(&PsActiveProcessHead);
    KeInitializeGuardedMutex(&PspActiveProcessMutex);

    /* Get the idle process */
    PsIdleProcess = PsGetCurrentProcess();

    /* Setup the locks */
    PsIdleProcess->ProcessLock.Value = 0;
    ExInitializeRundownProtection(&PsIdleProcess->RundownProtect);

    /* Initialize the address creation lock (required for MM operations) */
    KeInitializeGuardedMutex(&PsIdleProcess->AddressCreationLock);

    /* Initialize the thread list */
    InitializeListHead(&PsIdleProcess->ThreadListHead);

    /* Clear kernel time */
    PsIdleProcess->Pcb.KernelTime = 0;

    /*
     * ARM64 FIX: Use separate OBJECT_TYPE_INITIALIZER variables to avoid
     * compiler bug where structure pointer is offset by 8 bytes on reuse.
     * See ARM64_STRUCTURE_POINTER_OFFSET_BUG.md for details.
     */

    /* Initialize the Process type */
    {
        OBJECT_TYPE_INITIALIZER ProcessTypeInitializer;
        RtlZeroMemory(&ProcessTypeInitializer, sizeof(ProcessTypeInitializer));

        RtlInitUnicodeString(&Name, L"Process");
        ProcessTypeInitializer.Length = sizeof(ProcessTypeInitializer);
        ProcessTypeInitializer.InvalidAttributes = OBJ_PERMANENT |
                                                   OBJ_EXCLUSIVE |
                                                   OBJ_OPENIF;
        ProcessTypeInitializer.PoolType = NonPagedPool;
        ProcessTypeInitializer.SecurityRequired = TRUE;
        ProcessTypeInitializer.DefaultNonPagedPoolCharge = sizeof(EPROCESS);
        ProcessTypeInitializer.GenericMapping = PspProcessMapping;
        ProcessTypeInitializer.ValidAccessMask = PROCESS_ALL_ACCESS;
        ProcessTypeInitializer.DeleteProcedure = PspDeleteProcess;
        ProcessTypeInitializer.CaseInsensitive = TRUE;

        ObCreateObjectType(&Name, &ProcessTypeInitializer, NULL, &PsProcessType);
    }

    /* Initialize the Thread type */
    {
        OBJECT_TYPE_INITIALIZER ThreadTypeInitializer;
        RtlZeroMemory(&ThreadTypeInitializer, sizeof(ThreadTypeInitializer));

        RtlInitUnicodeString(&Name, L"Thread");
        ThreadTypeInitializer.Length = sizeof(ThreadTypeInitializer);
        ThreadTypeInitializer.InvalidAttributes = OBJ_PERMANENT |
                                                  OBJ_EXCLUSIVE |
                                                  OBJ_OPENIF;
        ThreadTypeInitializer.PoolType = NonPagedPool;
        ThreadTypeInitializer.SecurityRequired = TRUE;
        ThreadTypeInitializer.DefaultNonPagedPoolCharge = sizeof(ETHREAD);
        ThreadTypeInitializer.GenericMapping = PspThreadMapping;
        ThreadTypeInitializer.ValidAccessMask = THREAD_ALL_ACCESS;
        ThreadTypeInitializer.DeleteProcedure = PspDeleteThread;
        ThreadTypeInitializer.CaseInsensitive = TRUE;

        ObCreateObjectType(&Name, &ThreadTypeInitializer, NULL, &PsThreadType);
    }

    /* Initialize the Job type */
    {
        OBJECT_TYPE_INITIALIZER JobTypeInitializer;
        RtlZeroMemory(&JobTypeInitializer, sizeof(JobTypeInitializer));

        RtlInitUnicodeString(&Name, L"Job");
        JobTypeInitializer.Length = sizeof(JobTypeInitializer);
        JobTypeInitializer.InvalidAttributes = 0;
        JobTypeInitializer.PoolType = NonPagedPool;
        JobTypeInitializer.SecurityRequired = TRUE;
        JobTypeInitializer.DefaultNonPagedPoolCharge = sizeof(EJOB);
        JobTypeInitializer.GenericMapping = PspJobMapping;
        JobTypeInitializer.ValidAccessMask = JOB_OBJECT_ALL_ACCESS;
        JobTypeInitializer.DeleteProcedure = PspDeleteJob;
        JobTypeInitializer.CaseInsensitive = TRUE;

        ObCreateObjectType(&Name, &JobTypeInitializer, NULL, &PsJobType);
    }

    /* Initialize job structures external to this file */
    PspInitializeJobStructures();

    /* Initialize the Working Set data */
    InitializeListHead(&PspWorkingSetChangeHead.List);
    KeInitializeGuardedMutex(&PspWorkingSetChangeHead.Lock);

    /* Create the CID Handle table */
    PspCidTable = ExCreateHandleTable(NULL);
    if (!PspCidTable) return FALSE;

    /* FIXME: Initialize LDT/VDM support */

    /* Setup the reaper */
    ExInitializeWorkItem(&PspReaperWorkItem, PspReapRoutine, NULL);

    /* Set the boot access token */
    PspBootAccessToken = (PTOKEN)(PsIdleProcess->Token.Value & ~MAX_FAST_REFS);

    /* Setup default object attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               0,
                               NULL,
                               NULL);

    /* Create the Initial System Process */
    Status = PspCreateProcess(&PspInitialSystemProcessHandle,
                              PROCESS_ALL_ACCESS,
                              &ObjectAttributes,
                              0,
                              FALSE,
                              0,
                              0,
                              0,
                              FALSE);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Get a reference to it */
    ObReferenceObjectByHandle(PspInitialSystemProcessHandle,
                              0,
                              PsProcessType,
                              KernelMode,
                              (PVOID*)&PsInitialSystemProcess,
                              NULL);

    /* Copy the process names */
    strcpy(PsIdleProcess->ImageFileName, "Idle");
    strcpy(PsInitialSystemProcess->ImageFileName, "System");

    /* Allocate a structure for the audit name */
    PsInitialSystemProcess->SeAuditProcessCreationInfo.ImageFileName =
        ExAllocatePoolWithTag(PagedPool,
                              sizeof(OBJECT_NAME_INFORMATION),
                              TAG_SEPA);
    if (!PsInitialSystemProcess->SeAuditProcessCreationInfo.ImageFileName)
    {
        /* Allocation failed */
        return FALSE;
    }

    /* Zero it */
    RtlZeroMemory(PsInitialSystemProcess->
                  SeAuditProcessCreationInfo.ImageFileName,
                  sizeof(OBJECT_NAME_INFORMATION));

    /* Setup the system initialization thread */
    Status = PsCreateSystemThread(&SysThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  &ObjectAttributes,
                                  0,
                                  NULL,
                                  Phase1Initialization,
                                  LoaderBlock);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Create a handle to it */
    ObReferenceObjectByHandle(SysThreadHandle,
                              0,
                              PsThreadType,
                              KernelMode,
                              (PVOID*)&SysThread,
                              NULL);
    ObCloseHandle(SysThreadHandle, KernelMode);

    /* Return success */
    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PsInitSystem(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /* Check the initialization phase */
    switch (ExpInitializationPhase)
    {
    case 0:

        /* Do Phase 0 */
        return PspInitPhase0(LoaderBlock);

    case 1:

        /* Do Phase 1 */
        return PspInitPhase1();

    default:

        /* Don't know any other phase! Bugcheck! */
        KeBugCheckEx(UNEXPECTED_INITIALIZATION_CALL,
                     1,
                     ExpInitializationPhase,
                     0,
                     0);
        return FALSE;
    }
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
BOOLEAN
NTAPI
PsGetVersion(OUT PULONG MajorVersion OPTIONAL,
             OUT PULONG MinorVersion OPTIONAL,
             OUT PULONG BuildNumber  OPTIONAL,
             OUT PUNICODE_STRING CSDVersion OPTIONAL)
{
    if (MajorVersion) *MajorVersion = NtMajorVersion;
    if (MinorVersion) *MinorVersion = NtMinorVersion;
    if (BuildNumber ) *BuildNumber  = NtBuildNumber & 0x3FFF;

    if (CSDVersion)
    {
        CSDVersion->Length = CmCSDVersionString.Length;
        CSDVersion->MaximumLength = CmCSDVersionString.MaximumLength;
        CSDVersion->Buffer = CmCSDVersionString.Buffer;
    }

    /* Return TRUE if this is a Checked Build */
    return (NtBuildNumber >> 28) == 0xC;
}

/* EOF */
