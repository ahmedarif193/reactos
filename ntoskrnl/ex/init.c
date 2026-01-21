/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ex/init.c
 * PURPOSE:         Executive Initialization Code
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Eric Kohl
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <ntstrsafe.h>
#include <reactos/buildno.h>
#include "inbv/logo.h"

#define NDEBUG
#include <debug.h>

/* ARM64: Disable verbose initialization logging */
#define EXP_ARM64_LOG(Stage) do { } while (0)

/* This is the size that we can expect from the win 2003 loader */
#define LOADER_PARAMETER_EXTENSION_MIN_SIZE \
    RTL_SIZEOF_THROUGH_FIELD(LOADER_PARAMETER_EXTENSION, AcpiTableSize)

/* Temporary hack */
CODE_SEG("INIT")
BOOLEAN
NTAPI
MmArmInitSystem(
    IN ULONG Phase,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
);

typedef struct _INIT_BUFFER
{
    WCHAR DebugBuffer[256];
    CHAR VersionBuffer[256];
    CHAR BootlogHeader[256];
    CHAR VersionNumber[24];
    RTL_USER_PROCESS_INFORMATION ProcessInfo;
    WCHAR RegistryBuffer[256];
} INIT_BUFFER, *PINIT_BUFFER;

/* DATA **********************************************************************/

/* NT Version Info */
ULONG NtMajorVersion = VER_PRODUCTMAJORVERSION;
ULONG NtMinorVersion = VER_PRODUCTMINORVERSION;
#if DBG /* Checked Build */
ULONG NtBuildNumber = VER_PRODUCTBUILD | 0xC0000000;
#else   /* Free Build */
ULONG NtBuildNumber = VER_PRODUCTBUILD;
#endif

/* NT System Info */
ULONG NtGlobalFlag = 0;
ULONG ExSuiteMask;

/* Cm Version Info */
ULONG CmNtSpBuildNumber;
ULONG CmNtCSDVersion;
ULONG CmNtCSDReleaseType;
UNICODE_STRING CmVersionString;
UNICODE_STRING CmCSDVersionString;

CHAR NtBuildLab[] = KERNEL_VERSION_BUILD_STR "."
                    REACTOS_COMPILER_NAME "_" REACTOS_COMPILER_VERSION;

/* Init flags and settings */
ULONG ExpInitializationPhase;
BOOLEAN ExpInTextModeSetup;
BOOLEAN IoRemoteBootClient;
ULONG InitSafeBootMode;
BOOLEAN InitIsWinPEMode, InitWinPEModeType;
BOOLEAN SosEnabled; // Used by driver.c!IopDisplayLoadingMessage()

/* NT Boot Path */
UNICODE_STRING NtSystemRoot;

/* NT Initial User Application */
WCHAR NtInitialUserProcessBuffer[128] = L"\\SystemRoot\\System32\\smss.exe";
ULONG NtInitialUserProcessBufferLength = sizeof(NtInitialUserProcessBuffer) -
                                         sizeof(WCHAR);
ULONG NtInitialUserProcessBufferType = REG_SZ;

/* Boot NLS information */
PVOID ExpNlsTableBase;
ULONG ExpAnsiCodePageDataOffset, ExpOemCodePageDataOffset;
ULONG ExpUnicodeCaseTableDataOffset;
NLSTABLEINFO ExpNlsTableInfo;
SIZE_T ExpNlsTableSize;
PVOID ExpNlsSectionPointer;

/* CMOS Timer Sanity */
BOOLEAN ExCmosClockIsSane = TRUE;

/* CYCLE 30: Timer ISR diagnostic counters (defined in arch/arm64/ke/interrupt.c) */
#if defined(_M_ARM64) || defined(__aarch64__)
extern ULONG KiTimerIsrCallCount;
extern ULONG KiInitInterruptsCallCount;
extern ULONG KiTimerStartedFlag;
extern ULONG KiTimerCtlReadback;
#endif
BOOLEAN ExpRealTimeIsUniversal;

/* FUNCTIONS ****************************************************************/

static __inline BOOLEAN
ExpCommandLineIsBoundary(CHAR Character)
{
    return (Character == ANSI_NULL) ||
           (Character == ' ') ||
           (Character == '\t') ||
           (Character == '\r') ||
           (Character == '\n') ||
           (Character == '/') ||
           (Character == '-') ||
           (Character == '"') ||
           (Character == '=');
}

CODE_SEG("INIT")
BOOLEAN
ExpCommandLineHasOption(_In_opt_ PCSTR Options,
                        _In_ PCSTR Option)
{
    SIZE_T OptionLength;
    const CHAR *Current;

    if ((Options == NULL) || (Option == NULL))
    {
        return FALSE;
    }

    OptionLength = strlen(Option);
    if (OptionLength == 0)
    {
        return FALSE;
    }

    for (Current = Options; *Current != ANSI_NULL; Current++)
    {
        CHAR PreviousCharacter = (Current == Options) ? ' ' : *(Current - 1);
        SIZE_T Index;

        if (!ExpCommandLineIsBoundary(PreviousCharacter))
        {
            continue;
        }

        for (Index = 0; Index < OptionLength; Index++)
        {
            CHAR OptionCharacter = Current[Index];

            if (OptionCharacter == ANSI_NULL)
            {
                break;
            }

            if (RtlUpperChar(OptionCharacter) !=
                RtlUpperChar(Option[Index]))
            {
                break;
            }
        }

        if ((Index == OptionLength) &&
            ExpCommandLineIsBoundary(Current[Index]))
        {
            return TRUE;
        }
    }

    return FALSE;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
ExpCreateSystemRootLink(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNICODE_STRING LinkName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE LinkHandle;
    NTSTATUS Status;
    ANSI_STRING AnsiName;
    CHAR Buffer[256];
    ANSI_STRING TargetString;
    UNICODE_STRING TargetName;

    /* Initialize the ArcName tree */
    RtlInitUnicodeString(&LinkName, L"\\ArcName");
    InitializeObjectAttributes(&ObjectAttributes,
                               &LinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               SePublicDefaultUnrestrictedSd);

    /* Create it */
    Status = NtCreateDirectoryObject(&LinkHandle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        /* Failed */
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED, Status, 1, 0, 0);
    }

    /* Close the LinkHandle */
    NtClose(LinkHandle);

    /* Initialize the Device tree */
    RtlInitUnicodeString(&LinkName, L"\\Device");
    InitializeObjectAttributes(&ObjectAttributes,
                               &LinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               SePublicDefaultUnrestrictedSd);

    /* Create it */
    Status = NtCreateDirectoryObject(&LinkHandle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        /* Failed */
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED, Status, 2, 0, 0);
    }

    /* Close the LinkHandle */
    ObCloseHandle(LinkHandle, KernelMode);

    /* Create the system root symlink name */
    RtlInitAnsiString(&AnsiName, "\\SystemRoot");
    Status = RtlAnsiStringToUnicodeString(&LinkName, &AnsiName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        /* Failed */
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED, Status, 3, 0, 0);
    }

    /* Initialize the attributes for the link */
    InitializeObjectAttributes(&ObjectAttributes,
                               &LinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               SePublicDefaultUnrestrictedSd);

    /* Build the ARC name */
    sprintf(Buffer,
            "\\ArcName\\%s%s",
            LoaderBlock->ArcBootDeviceName,
            LoaderBlock->NtBootPathName);
    Buffer[strlen(Buffer) - 1] = ANSI_NULL;

    /* Convert it to Unicode */
    RtlInitString(&TargetString, Buffer);
    Status = RtlAnsiStringToUnicodeString(&TargetName,
                                          &TargetString,
                                          TRUE);
    if (!NT_SUCCESS(Status))
    {
        /* We failed, bugcheck */
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED, Status, 4, 0, 0);
    }

    /* Create it */
    Status = NtCreateSymbolicLinkObject(&LinkHandle,
                                        SYMBOLIC_LINK_ALL_ACCESS,
                                        &ObjectAttributes,
                                        &TargetName);

    /* Free the strings */
    RtlFreeUnicodeString(&LinkName);
    RtlFreeUnicodeString(&TargetName);

    /* Check if creating the link failed */
    if (!NT_SUCCESS(Status))
    {
        /* Failed */
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED, Status, 5, 0, 0);
    }

    /* Close the handle and return success */
    ObCloseHandle(LinkHandle, KernelMode);
    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
NTAPI
ExpInitNls(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    LARGE_INTEGER SectionSize;
    NTSTATUS Status;
    HANDLE NlsSection;
    PVOID SectionBase = NULL;
    SIZE_T ViewSize = 0;
    LARGE_INTEGER SectionOffset = {{0, 0}};
    PLIST_ENTRY ListHead, NextEntry;
    PMEMORY_ALLOCATION_DESCRIPTOR MdBlock;
    ULONG NlsTablesEncountered = 0;
    SIZE_T NlsTableSizes[3] = {0, 0, 0}; /* 3 NLS tables */

    /* Check if this is boot-time phase 0 initialization */
    if (!ExpInitializationPhase)
    {
        /* Loop the memory descriptors */
        ListHead = &LoaderBlock->MemoryDescriptorListHead;
        NextEntry = ListHead->Flink;
        while (NextEntry != ListHead)
        {
            /* Get the current block */
            MdBlock = CONTAINING_RECORD(NextEntry,
                                        MEMORY_ALLOCATION_DESCRIPTOR,
                                        ListEntry);

            /* Check if this is an NLS block */
            if (MdBlock->MemoryType == LoaderNlsData)
            {
                /* Increase the table size */
                ExpNlsTableSize += MdBlock->PageCount * PAGE_SIZE;

                /* FreeLdr-specific */
                NlsTableSizes[NlsTablesEncountered] = MdBlock->PageCount * PAGE_SIZE;
                NlsTablesEncountered++;
                ASSERT(NlsTablesEncountered < 4);
            }

            /* Go to the next block */
            NextEntry = MdBlock->ListEntry.Flink;
        }

        /* Allocate the a new buffer since loader memory will be freed */
        ExpNlsTableBase = ExAllocatePoolWithTag(NonPagedPool,
                                                ExpNlsTableSize,
                                                TAG_RTLI);
        if (!ExpNlsTableBase) KeBugCheck(PHASE0_INITIALIZATION_FAILED);

        /* Copy the codepage data in its new location. */
        if (NlsTablesEncountered == 1)
        {
            /* Ntldr-way boot process */
            RtlCopyMemory(ExpNlsTableBase,
                          LoaderBlock->NlsData->AnsiCodePageData,
                          ExpNlsTableSize);
        }
        else
        {
            /*
            * In NT, the memory blocks are contiguous, but in ReactOS they aren't,
            * so unless someone fixes FreeLdr, we'll have to use this icky hack.
            */
            RtlCopyMemory(ExpNlsTableBase,
                          LoaderBlock->NlsData->AnsiCodePageData,
                          NlsTableSizes[0]);

            RtlCopyMemory((PVOID)((ULONG_PTR)ExpNlsTableBase + NlsTableSizes[0]),
                          LoaderBlock->NlsData->OemCodePageData,
                          NlsTableSizes[1]);

            RtlCopyMemory((PVOID)((ULONG_PTR)ExpNlsTableBase + NlsTableSizes[0] +
                          NlsTableSizes[1]),
                          LoaderBlock->NlsData->UnicodeCodePageData,
                          NlsTableSizes[2]);
            /* End of Hack */
        }

        /* Initialize and reset the NLS TAbles */
        RtlInitNlsTables((PVOID)((ULONG_PTR)ExpNlsTableBase +
                                 ExpAnsiCodePageDataOffset),
                         (PVOID)((ULONG_PTR)ExpNlsTableBase +
                                 ExpOemCodePageDataOffset),
                         (PVOID)((ULONG_PTR)ExpNlsTableBase +
                                 ExpUnicodeCaseTableDataOffset),
                         &ExpNlsTableInfo);
        RtlResetRtlTranslations(&ExpNlsTableInfo);
        return;
    }

    /* Set the section size */
    SectionSize.QuadPart = ExpNlsTableSize;

    /* Create the NLS Section */
    Status = ZwCreateSection(&NlsSection,
                             SECTION_ALL_ACCESS,
                             NULL,
                             &SectionSize,
                             PAGE_READWRITE,
                             SEC_COMMIT | 0x1,
                             NULL);
    if (!NT_SUCCESS(Status))
    {
        /* Failed */
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 1, 0, 0);
    }

    /* Get a pointer to the section */
    Status = ObReferenceObjectByHandle(NlsSection,
                                       SECTION_ALL_ACCESS,
                                       MmSectionObjectType,
                                       KernelMode,
                                       &ExpNlsSectionPointer,
                                       NULL);
    ObCloseHandle(NlsSection, KernelMode);
    if (!NT_SUCCESS(Status))
    {
        /* Failed */
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 2, 0, 0);
    }

    /* Map the NLS Section in system space */
    Status = MmMapViewInSystemSpace(ExpNlsSectionPointer,
                                    &SectionBase,
                                    &ExpNlsTableSize);
    if (!NT_SUCCESS(Status))
    {
        /* Failed */
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 3, 0, 0);
    }

    /* Copy the codepage data in its new location. */
    ASSERT(SectionBase >= MmSystemRangeStart);
#if defined(_M_ARM64)
    /*
     * ARM64 CRITICAL FIX: Pre-fault all destination pages before memmove.
     *
     * On ARM64, memmove uses SIMD (NEON) registers for efficient memory copy.
     * However, the current trap frame does NOT save/restore SIMD registers (V0-V31).
     * If a page fault occurs mid-copy, the SIMD registers containing source data
     * are clobbered by the fault handler, causing zeros or garbage to be written.
     *
     * WORKAROUND: Touch each destination page with a simple scalar write before
     * the copy starts. This triggers the demand-zero page faults using scalar
     * registers which ARE properly saved/restored in the trap frame.
     *
     * TODO: The proper fix is to save/restore SIMD registers in KTRAP_FRAME.
     */
    {
        volatile PUCHAR PagePtr;
        SIZE_T Offset;

        for (Offset = 0; Offset < ExpNlsTableSize; Offset += PAGE_SIZE)
        {
            PagePtr = (volatile PUCHAR)((ULONG_PTR)SectionBase + Offset);
            /* Simple scalar write to trigger page fault */
            *PagePtr = 0;
            /* Memory barrier to ensure fault completes before next iteration */
            __dsb(_ARM64_BARRIER_SY);
        }
    }

    /* Now copy data with memmove - pages are already faulted in */
    memmove(SectionBase, ExpNlsTableBase, ExpNlsTableSize);
    __dsb(_ARM64_BARRIER_SY);
#else
    EXP_ARM64_LOG("[arm64] ExpInitNls Phase1: RtlCopyMemory to SectionBase");
    RtlCopyMemory(SectionBase, ExpNlsTableBase, ExpNlsTableSize);
#endif

    /* Free the previously allocated buffer and set the new location */
    EXP_ARM64_LOG("[arm64] ExpInitNls Phase1: before ExFreePoolWithTag");
    ExFreePoolWithTag(ExpNlsTableBase, TAG_RTLI);
    EXP_ARM64_LOG("[arm64] ExpInitNls Phase1: after ExFreePoolWithTag");
    ExpNlsTableBase = SectionBase;

    /* Initialize the NLS Tables */
    EXP_ARM64_LOG("[arm64] ExpInitNls Phase1: before RtlInitNlsTables");
    RtlInitNlsTables((PVOID)((ULONG_PTR)ExpNlsTableBase +
                             ExpAnsiCodePageDataOffset),
                     (PVOID)((ULONG_PTR)ExpNlsTableBase +
                             ExpOemCodePageDataOffset),
                     (PVOID)((ULONG_PTR)ExpNlsTableBase +
                             ExpUnicodeCaseTableDataOffset),
                     &ExpNlsTableInfo);
    EXP_ARM64_LOG("[arm64] ExpInitNls Phase1: after RtlInitNlsTables");
    RtlResetRtlTranslations(&ExpNlsTableInfo);
    EXP_ARM64_LOG("[arm64] ExpInitNls Phase1: after RtlResetRtlTranslations");

    /* Reset the base to 0 */
    SectionBase = NULL;

    /* Map the section in the system process */
    EXP_ARM64_LOG("[arm64] ExpInitNls Phase1: before MmMapViewOfSection");
    Status = MmMapViewOfSection(ExpNlsSectionPointer,
                                PsGetCurrentProcess(),
                                &SectionBase,
                                0L,
                                0L,
                                &SectionOffset,
                                &ViewSize,
                                ViewShare,
                                0L,
                                PAGE_READWRITE);
    EXP_ARM64_LOG("[arm64] ExpInitNls Phase1: after MmMapViewOfSection");
    if (!NT_SUCCESS(Status))
    {
        /* Failed */
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 5, 0, 0);
    }

    /*
     * Copy the table into the system process and set this as the base.
     *
     * ARM64 Note: On ARM64, kernel code cannot directly access user-space
     * addresses (SectionBase = 0x10000) because the kernel uses TTBR1
     * (addresses >= 0xFFFF800000000000) while user space uses TTBR0.
     * However, since both views map the same SEC_COMMIT section, the physical
     * pages are shared and the data written via the system-space mapping
     * is already visible in user space. The copy is therefore unnecessary.
     */
#if defined(_M_ARM64)
    DPRINT1("[arm64] ExpInitNls Phase1: SectionBase=%p (user) ExpNlsTableBase=%p (kernel) Size=0x%lx\n",
            SectionBase, ExpNlsTableBase, (ULONG)ExpNlsTableSize);
    DPRINT1("[arm64] ExpInitNls Phase1: Skipping redundant copy - section views share physical pages\n");
    /* On ARM64, we keep ExpNlsTableBase pointing to kernel space mapping */
    /* User-mode processes will use the user-space mapping at SectionBase */
#else
    RtlCopyMemory(SectionBase, ExpNlsTableBase, ExpNlsTableSize);
    ExpNlsTableBase = SectionBase;
#endif
    EXP_ARM64_LOG("[arm64] ExpInitNls Phase1: complete");
}

CODE_SEG("INIT")
VOID
NTAPI
ExpLoadInitialProcess(IN PINIT_BUFFER InitBuffer,
                      OUT PRTL_USER_PROCESS_PARAMETERS *ProcessParameters,
                      OUT PCHAR *ProcessEnvironment)
{
    NTSTATUS Status;
    SIZE_T Size;
    PWSTR p;
    UNICODE_STRING NullString = RTL_CONSTANT_STRING(L"");
    UNICODE_STRING SmssName, Environment, SystemDriveString, DebugString;
    PVOID EnvironmentPtr = NULL;
    PRTL_USER_PROCESS_INFORMATION ProcessInformation;
    PRTL_USER_PROCESS_PARAMETERS ProcessParams = NULL;

#if defined(_M_ARM64)
    DPRINT1("[arm64] ExpLoadInitialProcess: ENTRY\n");
#endif

    NullString.Length = sizeof(WCHAR);

    /* Use the initial buffer, after the strings */
    ProcessInformation = &InitBuffer->ProcessInfo;

#if defined(_M_ARM64)
    DPRINT1("[arm64] ExpLoadInitialProcess: Before allocating ProcessParams\n");

    /* ARM64 workaround: ZwAllocateVirtualMemory doesn't work properly for user-mode
     * addresses in the System process because the memory manager's PTE manipulation
     * functions (MiAddressToPte, etc.) use self-mapping which only works for TTBR1
     * (kernel space), not TTBR0 (user space).
     *
     * Instead, allocate the memory in kernel space (non-paged pool) and use it
     * directly. We'll copy it to the new process's user space later.
     */
    Size = sizeof(*ProcessParams) + ((MAX_WIN32_PATH * 6) * sizeof(WCHAR));
    ProcessParams = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Size, 'spmS');
    if (!ProcessParams)
    {
        /* Failed, display error */
        _snwprintf(InitBuffer->DebugBuffer,
                   sizeof(InitBuffer->DebugBuffer)/sizeof(WCHAR),
                   L"INIT: Unable to allocate Process Parameters (ARM64 pool)");
        RtlInitUnicodeString(&DebugString, InitBuffer->DebugBuffer);
        ZwDisplayString(&DebugString);

        /* Bugcheck the system */
        KeBugCheckEx(SESSION1_INITIALIZATION_FAILED, STATUS_NO_MEMORY, 0, 0, 0);
    }
    RtlZeroMemory(ProcessParams, (SIZE_T)Size);
    Status = STATUS_SUCCESS;
    DPRINT1("[arm64] ExpLoadInitialProcess: Allocated ProcessParams in kernel pool at %p Size=0x%lx\n",
            ProcessParams, (ULONG)Size);
#else
    DPRINT1("[arm64] ExpLoadInitialProcess: Before ZwAllocateVirtualMemory\n");
    /* Allocate memory for the process parameters */
    Size = sizeof(*ProcessParams) + ((MAX_WIN32_PATH * 6) * sizeof(WCHAR));
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     (PVOID*)&ProcessParams,
                                     0,
                                     &Size,
                                     MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READWRITE);
    DPRINT1("[arm64] ExpLoadInitialProcess: ZwAllocateVirtualMemory returned Status=0x%lx ProcessParams=%p\n",
            Status, ProcessParams);
    if (!NT_SUCCESS(Status))
    {
        /* Failed, display error */
        _snwprintf(InitBuffer->DebugBuffer,
                   sizeof(InitBuffer->DebugBuffer)/sizeof(WCHAR),
                   L"INIT: Unable to allocate Process Parameters. 0x%lx",
                   Status);
        RtlInitUnicodeString(&DebugString, InitBuffer->DebugBuffer);
        ZwDisplayString(&DebugString);

        /* Bugcheck the system */
        KeBugCheckEx(SESSION1_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }
#endif

    /* Setup the basic header, and give the process the low 1MB to itself */
    ProcessParams->Length = (ULONG)Size;
    ProcessParams->MaximumLength = (ULONG)Size;
    ProcessParams->Flags = RTL_USER_PROCESS_PARAMETERS_NORMALIZED |
                           RTL_USER_PROCESS_PARAMETERS_RESERVE_1MB;

    /* Allocate a page for the environment */
#if defined(_M_ARM64)
    /* ARM64: Same issue as ProcessParams - allocate in kernel pool */
    Size = PAGE_SIZE;
    EnvironmentPtr = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Size, 'vnES');
    if (!EnvironmentPtr)
    {
        /* Failed, display error */
        _snwprintf(InitBuffer->DebugBuffer,
                   sizeof(InitBuffer->DebugBuffer)/sizeof(WCHAR),
                   L"INIT: Unable to allocate Process Environment (ARM64 pool)");
        RtlInitUnicodeString(&DebugString, InitBuffer->DebugBuffer);
        ZwDisplayString(&DebugString);

        /* Bugcheck the system */
        KeBugCheckEx(SESSION2_INITIALIZATION_FAILED, STATUS_NO_MEMORY, 0, 0, 0);
    }
    RtlZeroMemory(EnvironmentPtr, (SIZE_T)Size);
    DPRINT1("[arm64] ExpLoadInitialProcess: Allocated Environment in kernel pool at %p Size=0x%lx\n",
            EnvironmentPtr, (ULONG)Size);
#else
    Size = PAGE_SIZE;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &EnvironmentPtr,
                                     0,
                                     &Size,
                                     MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        /* Failed, display error */
        _snwprintf(InitBuffer->DebugBuffer,
                   sizeof(InitBuffer->DebugBuffer)/sizeof(WCHAR),
                   L"INIT: Unable to allocate Process Environment. 0x%lx",
                   Status);
        RtlInitUnicodeString(&DebugString, InitBuffer->DebugBuffer);
        ZwDisplayString(&DebugString);

        /* Bugcheck the system */
        KeBugCheckEx(SESSION2_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }
#endif

    /* Write the pointer */
    ProcessParams->Environment = EnvironmentPtr;

    /* Make a buffer for the DOS path */
    p = (PWSTR)(ProcessParams + 1);
#if defined(_M_ARM64)
    DPRINT1("[arm64] ExpLoadInitialProcess: Step1 - ProcessParams+1=%p\n", p);
#endif
    ProcessParams->CurrentDirectory.DosPath.Buffer = p;
    ProcessParams->CurrentDirectory.DosPath.MaximumLength = MAX_WIN32_PATH *
                                                            sizeof(WCHAR);
#if defined(_M_ARM64)
    /* ARM64: Flush cache lines to ensure writes are visible */
    {
        ULONG_PTR addr = (ULONG_PTR)&ProcessParams->CurrentDirectory.DosPath;
        ULONG_PTR end = addr + sizeof(ProcessParams->CurrentDirectory.DosPath);
        for (ULONG_PTR va = addr & ~63ULL; va < end; va += 64)
        {
            __asm__ volatile("dc cvau, %0" : : "r"(va) : "memory");
        }
        __asm__ volatile("dsb ish\n\tisb" ::: "memory");
    }
    DPRINT1("[arm64] ExpLoadInitialProcess: Step2 - CurrentDirectory.DosPath.Buffer=%p Max=0x%x\n",
            ProcessParams->CurrentDirectory.DosPath.Buffer,
            ProcessParams->CurrentDirectory.DosPath.MaximumLength);
#endif

    /* Copy the DOS path */
#if defined(_M_ARM64)
    __asm__ volatile("dmb sy" ::: "memory");
    DPRINT1("[arm64] ExpLoadInitialProcess: Step2b - About to call RtlCopyUnicodeString\n");
    DPRINT1("[arm64] ExpLoadInitialProcess: Step2b - Dest.Buffer=%p Dest.Max=0x%x\n",
            ProcessParams->CurrentDirectory.DosPath.Buffer,
            ProcessParams->CurrentDirectory.DosPath.MaximumLength);
    DPRINT1("[arm64] ExpLoadInitialProcess: Step2b - Src.Buffer=%p Src.Length=0x%x\n",
            NtSystemRoot.Buffer, NtSystemRoot.Length);
#endif
    RtlCopyUnicodeString(&ProcessParams->CurrentDirectory.DosPath,
                         &NtSystemRoot);
#if defined(_M_ARM64)
    DPRINT1("[arm64] ExpLoadInitialProcess: Step3 - After RtlCopyUnicodeString (DOS path)\n");
#endif

    /* Make a buffer for the DLL Path */
    p = (PWSTR)((PCHAR)ProcessParams->CurrentDirectory.DosPath.Buffer +
                ProcessParams->CurrentDirectory.DosPath.MaximumLength);
#if defined(_M_ARM64)
    DPRINT1("[arm64] ExpLoadInitialProcess: Step4 - DllPath start=%p\n", p);
#endif
    ProcessParams->DllPath.Buffer = p;
    ProcessParams->DllPath.MaximumLength = MAX_WIN32_PATH * sizeof(WCHAR);

    /* Copy the DLL path and append the system32 directory */
    RtlCopyUnicodeString(&ProcessParams->DllPath,
                         &ProcessParams->CurrentDirectory.DosPath);
#if defined(_M_ARM64)
    DPRINT1("[arm64] ExpLoadInitialProcess: Step5 - After RtlCopyUnicodeString (DLL path)\n");
#endif
    RtlAppendUnicodeToString(&ProcessParams->DllPath, L"\\System32");
#if defined(_M_ARM64)
    DPRINT1("[arm64] ExpLoadInitialProcess: Step6 - After RtlAppendUnicodeToString\n");
#endif

    /* Make a buffer for the image name */
    p = (PWSTR)((PCHAR)ProcessParams->DllPath.Buffer +
                ProcessParams->DllPath.MaximumLength);
#if defined(_M_ARM64)
    DPRINT1("[arm64] ExpLoadInitialProcess: Step7 - DllPath.Buffer=%p DllPath.MaximumLength=0x%x\n",
            ProcessParams->DllPath.Buffer, ProcessParams->DllPath.MaximumLength);
    DPRINT1("[arm64] ExpLoadInitialProcess: Step8 - Calculated ImagePathName.Buffer=%p\n", p);
#endif
    ProcessParams->ImagePathName.Buffer = p;
    ProcessParams->ImagePathName.MaximumLength = MAX_WIN32_PATH * sizeof(WCHAR);

    /* Make sure the buffer is a valid string which within the given length */
    if ((NtInitialUserProcessBufferType != REG_SZ) ||
        ((NtInitialUserProcessBufferLength != MAXULONG) &&
         ((NtInitialUserProcessBufferLength < sizeof(WCHAR)) ||
          (NtInitialUserProcessBufferLength >
           sizeof(NtInitialUserProcessBuffer) - sizeof(WCHAR)))))
    {
        /* Invalid initial process string, bugcheck */
        KeBugCheckEx(SESSION2_INITIALIZATION_FAILED,
                     STATUS_INVALID_PARAMETER,
                     NtInitialUserProcessBufferType,
                     NtInitialUserProcessBufferLength,
                     sizeof(NtInitialUserProcessBuffer));
    }

    /* Cut out anything after a space */
    p = NtInitialUserProcessBuffer;
    while ((*p) && (*p != L' ')) p++;

    /* Set the image path length */
    ProcessParams->ImagePathName.Length =
        (USHORT)((PCHAR)p - (PCHAR)NtInitialUserProcessBuffer);

    /* Copy the actual buffer */
    RtlCopyMemory(ProcessParams->ImagePathName.Buffer,
                  NtInitialUserProcessBuffer,
                  ProcessParams->ImagePathName.Length);

    /* Null-terminate it */
    ProcessParams->ImagePathName.Buffer[ProcessParams->ImagePathName.Length /
                                        sizeof(WCHAR)] = UNICODE_NULL;

    /* Make a buffer for the command line */
    p = (PWSTR)((PCHAR)ProcessParams->ImagePathName.Buffer +
                ProcessParams->ImagePathName.MaximumLength);
    ProcessParams->CommandLine.Buffer = p;
    ProcessParams->CommandLine.MaximumLength = MAX_WIN32_PATH * sizeof(WCHAR);

    /* Add the image name to the command line */
    RtlAppendUnicodeToString(&ProcessParams->CommandLine,
                             NtInitialUserProcessBuffer);

    /* Create the environment string */
    RtlInitEmptyUnicodeString(&Environment,
                              ProcessParams->Environment,
                              (USHORT)Size);

    /* Append the DLL path to it */
    RtlAppendUnicodeToString(&Environment, L"Path=");
    RtlAppendUnicodeStringToString(&Environment, &ProcessParams->DllPath);
    RtlAppendUnicodeStringToString(&Environment, &NullString);

    /* Create the system drive string */
    SystemDriveString = NtSystemRoot;
    SystemDriveString.Length = 2 * sizeof(WCHAR);

    /* Append it to the environment */
    RtlAppendUnicodeToString(&Environment, L"SystemDrive=");
    RtlAppendUnicodeStringToString(&Environment, &SystemDriveString);
    RtlAppendUnicodeStringToString(&Environment, &NullString);

    /* Append the system root to the environment */
    RtlAppendUnicodeToString(&Environment, L"SystemRoot=");
    RtlAppendUnicodeStringToString(&Environment, &NtSystemRoot);
    RtlAppendUnicodeStringToString(&Environment, &NullString);

    /* Prepare the prefetcher */
    //CcPfBeginBootPhase(150);

    /* Create SMSS process */
    SmssName = ProcessParams->ImagePathName;
    Status = RtlCreateUserProcess(&SmssName,
                                  OBJ_CASE_INSENSITIVE,
                                  RtlDeNormalizeProcessParams(ProcessParams),
                                  NULL,
                                  NULL,
                                  NULL,
                                  FALSE,
                                  NULL,
                                  NULL,
                                  ProcessInformation);
    if (!NT_SUCCESS(Status))
    {
        /* Failed, display error */
        _snwprintf(InitBuffer->DebugBuffer,
                   sizeof(InitBuffer->DebugBuffer)/sizeof(WCHAR),
                   L"INIT: Unable to create Session Manager. 0x%lx",
                   Status);
        RtlInitUnicodeString(&DebugString, InitBuffer->DebugBuffer);
        ZwDisplayString(&DebugString);

        /* Bugcheck the system */
        KeBugCheckEx(SESSION3_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }

    /* We kept the firmware splash alive until now; release the screen before usermode. */
    InbvFinalizeBootGraphics();

    /* Resume the thread */
    Status = ZwResumeThread(ProcessInformation->ThreadHandle, NULL);
    if (!NT_SUCCESS(Status))
    {
        /* Failed, display error */
        _snwprintf(InitBuffer->DebugBuffer,
                   sizeof(InitBuffer->DebugBuffer)/sizeof(WCHAR),
                   L"INIT: Unable to resume Session Manager. 0x%lx",
                   Status);
        RtlInitUnicodeString(&DebugString, InitBuffer->DebugBuffer);
        ZwDisplayString(&DebugString);

        /* Bugcheck the system */
        KeBugCheckEx(SESSION4_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }

    /* Return success */
    *ProcessParameters = ProcessParams;
    *ProcessEnvironment = EnvironmentPtr;
}

CODE_SEG("INIT")
ULONG
NTAPI
ExComputeTickCountMultiplier(IN ULONG ClockIncrement)
{
    ULONG MsRemainder = 0, MsIncrement;
    ULONG IncrementRemainder;
    ULONG i;

    /* Count the number of milliseconds for each clock interrupt */
    MsIncrement = ClockIncrement / (10 * 1000);

    /* Count the remainder from the division above, with 24-bit precision */
    IncrementRemainder = ClockIncrement - (MsIncrement * (10 * 1000));
    for (i= 0; i < 24; i++)
    {
        /* Shift the remainders */
        MsRemainder <<= 1;
        IncrementRemainder <<= 1;

        /* Check if we've went past 1 ms */
        if (IncrementRemainder >= (10 * 1000))
        {
            /* Increase the remainder by one, and substract from increment */
            IncrementRemainder -= (10 * 1000);
            MsRemainder |= 1;
        }
    }

    /* Return the increment */
    return (MsIncrement << 24) | MsRemainder;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitSystemPhase0(VOID)
{
    /* Initialize EXRESOURCE Support */
    ExpResourceInitialization();

    /* Initialize the environment lock */
    ExInitializeFastMutex(&ExpEnvironmentLock);

    /* Initialize the lookaside lists and locks */
    ExpInitLookasideLists();

    /* Initialize the Firmware Table resource and listhead */
    InitializeListHead(&ExpFirmwareTableProviderListHead);
    ExInitializeResourceLite(&ExpFirmwareTableResource);
    ExInitializeResourceLite(&ExpTimeRefreshLock);

    /* Set the suite mask to maximum and return */
    ExSuiteMask = 0xFFFFFFFF;
    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitSystemPhase1(VOID)
{
    /* Initialize worker threads */
    ExpInitializeWorkerThreads();

    /* Initialize pushlocks */
    ExpInitializePushLocks();

    /* Initialize events and event pairs */
    if (ExpInitializeEventImplementation() == FALSE)
    {
        DPRINT1("Executive: Event initialization failed\n");
        return FALSE;
    }
    if (ExpInitializeEventPairImplementation() == FALSE)
    {
        DPRINT1("Executive: Event Pair initialization failed\n");
        return FALSE;
    }

    /* Initialize mutants */
    if (ExpInitializeMutantImplementation() == FALSE)
    {
        DPRINT1("Executive: Mutant initialization failed\n");
        return FALSE;
    }

    /* Initialize callbacks */
    if (ExpInitializeCallbacks() == FALSE)
    {
        DPRINT1("Executive: Callback initialization failed\n");
        return FALSE;
    }

    /* Initialize semaphores */
    if (ExpInitializeSemaphoreImplementation() == FALSE)
    {
        DPRINT1("Executive: Semaphore initialization failed\n");
        return FALSE;
    }

    /* Initialize timers */
    if (ExpInitializeTimerImplementation() == FALSE)
    {
        DPRINT1("Executive: Timer initialization failed\n");
        return FALSE;
    }

    /* Initialize profiling */
    if (ExpInitializeProfileImplementation() == FALSE)
    {
        DPRINT1("Executive: Profile initialization failed\n");
        return FALSE;
    }

    /* Initialize UUIDs */
    if (ExpUuidInitialization() == FALSE)
    {
        DPRINT1("Executive: Uuid initialization failed\n");
        return FALSE;
    }

    /* Initialize keyed events */
    if (ExpInitializeKeyedEventImplementation() == FALSE)
    {
        DPRINT1("Executive: Keyed event initialization failed\n");
        return FALSE;
    }

    /* Initialize Win32K */
    if (ExpWin32kInit() == FALSE)
    {
        DPRINT1("Executive: Win32 initialization failed\n");
        return FALSE;
    }
    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExInitSystem(VOID)
{
    /* Check the initialization phase */
    switch (ExpInitializationPhase)
    {
        case 0:

            /* Do Phase 0 */
            return ExpInitSystemPhase0();

        case 1:

            /* Do Phase 1 */
            return ExpInitSystemPhase1();

        default:

            /* Don't know any other phase! Bugcheck! */
            KeBugCheck(UNEXPECTED_INITIALIZATION_CALL);
            return FALSE;
    }
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpIsLoaderValid(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_EXTENSION Extension;

    /* Get the loader extension */
    Extension = LoaderBlock->Extension;

    /* Validate the size (Windows 2003 loader doesn't provide more) */
    if (Extension->Size < LOADER_PARAMETER_EXTENSION_MIN_SIZE) return FALSE;

    /* Don't validate upper versions */
    if (Extension->MajorVersion > VER_PRODUCTMAJORVERSION) return TRUE;

    /* Fail if this is NT 4 */
    if (Extension->MajorVersion < VER_PRODUCTMAJORVERSION) return FALSE;

    /* Fail if this is XP */
    if (Extension->MinorVersion < VER_PRODUCTMINORVERSION) return FALSE;

    /* This is 2003 or newer, approve it */
    return TRUE;
}

static CODE_SEG("INIT")
VOID
ExpLoadBootSymbols(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY NextEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    NTSTATUS Status;
    ULONG i;
    ULONG Count, Length;
    PWCHAR Name;
    STRING ImageName;
    CHAR NameBuffer[256];

    /* Loop over the boot modules list */
    for (NextEntry = LoaderBlock->LoadOrderListHead.Flink, i = 0;
         NextEntry != &LoaderBlock->LoadOrderListHead;
         NextEntry = NextEntry->Flink, ++i)
    {
        /* Skip the first two images: HAL and kernel */
        if (i < 2)
            continue;

        /* Get the entry */
        LdrEntry = CONTAINING_RECORD(NextEntry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);
        if (LdrEntry->FullDllName.Buffer[0] == L'\\')
        {
            /* We have a name, read its data */
            Name = LdrEntry->FullDllName.Buffer;
            Length = LdrEntry->FullDllName.Length / sizeof(WCHAR);

            /* Check if our buffer can hold it */
            if (sizeof(NameBuffer) < Length + sizeof(ANSI_NULL))
            {
                /* It's too long */
                Status = STATUS_BUFFER_OVERFLOW;
            }
            else
            {
                /* Copy the name */
                Count = 0;
                do
                {
                    /* Do cheap Unicode to ANSI conversion */
                    NameBuffer[Count++] = (CHAR)*Name++;
                } while (Count < Length);

                /* Null-terminate */
                NameBuffer[Count] = ANSI_NULL;
                Status = STATUS_SUCCESS;
            }
        }
        else
        {
            /* Safely print the string into our buffer */
            Status = RtlStringCbPrintfA(NameBuffer,
                                        sizeof(NameBuffer),
                                        "%S\\System32\\Drivers\\%wZ",
                                        &SharedUserData->NtSystemRoot[2],
                                        &LdrEntry->BaseDllName);
        }

        /* Check if the buffer is OK */
        if (NT_SUCCESS(Status))
        {
            /* Load the symbols */
            RtlInitString(&ImageName, NameBuffer);
            DbgLoadImageSymbols(&ImageName,
                                LdrEntry->DllBase,
                                (ULONG_PTR)PsGetCurrentProcessId());
        }

#ifdef CONFIG_SMP
        /* Check that the image is safe to use if we have more than one CPU */
        if (!MmVerifyImageIsOkForMpUse(LdrEntry->DllBase))
        {
            KeBugCheckEx(UP_DRIVER_ON_MP_SYSTEM,
                         (ULONG_PTR)LdrEntry->DllBase,
                         0, 0, 0);
        }
#endif // CONFIG_SMP
    }
}

CODE_SEG("INIT")
VOID
NTAPI
ExBurnMemory(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
             IN ULONG_PTR PagesToDestroy,
             IN TYPE_OF_MEMORY MemoryType)
{
    PLIST_ENTRY ListEntry;
    PMEMORY_ALLOCATION_DESCRIPTOR MemDescriptor;

    DPRINT1("Burn RAM amount: %lu pages\n", PagesToDestroy);

    /* Loop the memory descriptors, beginning at the end */
    for (ListEntry = LoaderBlock->MemoryDescriptorListHead.Blink;
         ListEntry != &LoaderBlock->MemoryDescriptorListHead;
         ListEntry = ListEntry->Blink)
    {
        /* Get the memory descriptor structure */
        MemDescriptor = CONTAINING_RECORD(ListEntry,
                                          MEMORY_ALLOCATION_DESCRIPTOR,
                                          ListEntry);

        /* Is memory free there or is it temporary? */
        if (MemDescriptor->MemoryType == LoaderFree ||
            MemDescriptor->MemoryType == LoaderFirmwareTemporary)
        {
            /* Check if the descriptor has more pages than we want */
            if (MemDescriptor->PageCount > PagesToDestroy)
            {
                /* Change block's page count, ntoskrnl doesn't care much */
                MemDescriptor->PageCount -= PagesToDestroy;
                break;
            }
            else
            {
                /* Change block type */
                MemDescriptor->MemoryType = MemoryType;
                PagesToDestroy -= MemDescriptor->PageCount;

                /* Check if we are done */
                if (PagesToDestroy == 0) break;
            }
        }
    }
}

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeExecutive(IN ULONG Cpu,
                       IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PNLS_DATA_BLOCK NlsData;
    CHAR Buffer[256];
    ANSI_STRING AnsiPath;
    NTSTATUS Status;
    PCHAR CommandLine, PerfMem;
    ULONG PerfMemUsed;
    PLDR_DATA_TABLE_ENTRY NtosEntry;
    PMESSAGE_RESOURCE_ENTRY MsgEntry;
    ANSI_STRING CSDString;
    size_t Remaining = 0;
    PCHAR RcEnd = NULL;
    CHAR VersionBuffer[65];

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        /* ARM64 debug: entry point marker */
        DPRINT1("%s\n", "[arm64] ExpInitializeExecutive: entry");

        /* Check LoaderBlock pointer validity */
        if ((ULONG_PTR)LoaderBlock < 0xFFFF800000000000ULL)
        {
            DPRINT1("%s\n", "[arm64] ExpInitializeExecutive: WARNING LoaderBlock is PA!");
        }

        /* Memory barrier to ensure all previous memory accesses complete */
        __asm__ volatile("dsb sy" ::: "memory");
        __asm__ volatile("isb" ::: "memory");

        DPRINT1("%s\n", "[arm64] ExpInitializeExecutive: after DSB/ISB");
    }
#endif

    /* Validate Loader */
#if defined(_M_ARM64) || defined(__aarch64__)
    {
        DPRINT1("%s\n", "[arm64] ExpInitializeExecutive: before ExpIsLoaderValid");
    }
#endif
    if (!ExpIsLoaderValid(LoaderBlock))
    {
        /* Invalid loader version */
        KeBugCheckEx(MISMATCHED_HAL,
                     3,
                     LoaderBlock->Extension->Size,
                     LoaderBlock->Extension->MajorVersion,
                     LoaderBlock->Extension->MinorVersion);
    }
#if defined(_M_ARM64) || defined(__aarch64__)
    {
        DPRINT1("%s\n", "[arm64] ExpInitializeExecutive: after ExpIsLoaderValid");
    }
#endif

    /* Initialize PRCB pool lookaside pointers */
#if defined(_M_ARM64) || defined(__aarch64__)
    {
        DPRINT1("%s\n", "[arm64] ExpInitializeExecutive: before ExInitPoolLookasidePointers");
    }
#endif
    ExInitPoolLookasidePointers();
#if defined(_M_ARM64) || defined(__aarch64__)
    {
        DPRINT1("%s\n", "[arm64] ExpInitializeExecutive: after ExInitPoolLookasidePointers");
    }
#endif

    /* Check if this is an application CPU */
    if (Cpu)
    {
        /* Then simply initialize it with HAL */
        if (!HalInitSystem(ExpInitializationPhase, LoaderBlock))
        {
            /* Initialization failed */
            KeBugCheck(HAL_INITIALIZATION_FAILED);
        }

        /* We're done */
        return;
    }

    /* Assume no text-mode or remote boot */
    ExpInTextModeSetup = FALSE;
    IoRemoteBootClient = FALSE;

    /* Check if we have a setup loader block */
    if (LoaderBlock->SetupLdrBlock)
    {
        /* Check if this is text-mode setup */
        if (LoaderBlock->SetupLdrBlock->Flags & SETUPLDR_TEXT_MODE)
            ExpInTextModeSetup = TRUE;

        /* Check if this is network boot */
        if (LoaderBlock->SetupLdrBlock->Flags & SETUPLDR_REMOTE_BOOT)
        {
            /* Set variable */
            IoRemoteBootClient = TRUE;

            /* Make sure we're actually booting off the network */
            ASSERT(!_memicmp(LoaderBlock->ArcBootDeviceName, "net(0)", 6));
        }
    }

    /* Set phase to 0 */
    ExpInitializationPhase = 0;

    /* Get boot command line */
    CommandLine = LoaderBlock->LoadOptions;
    if (CommandLine)
    {
        /* Upcase it for comparison and check if we're in performance mode */
        _strupr(CommandLine);
        PerfMem = strstr(CommandLine, "PERFMEM");
        if (PerfMem)
        {
            /* Check if the user gave a number of bytes to use */
            PerfMem = strstr(PerfMem, "=");
            if (PerfMem)
            {
                /* Read the number of pages we'll use */
                PerfMemUsed = atol(PerfMem + 1) * (1024 * 1024 / PAGE_SIZE);
                if (PerfMemUsed)
                {
                    /* FIXME: TODO */
                    DPRINT1("BBT performance mode not yet supported."
                            "/PERFMEM option ignored.\n");
                }
            }
        }

        /* Check if we're burning memory */
        PerfMem = strstr(CommandLine, "BURNMEMORY");
        if (PerfMem)
        {
            /* Check if the user gave a number of bytes to use */
            PerfMem = strstr(PerfMem, "=");
            if (PerfMem)
            {
                /* Read the number of pages we'll use */
                PerfMemUsed = atol(PerfMem + 1) * (1024 * 1024 / PAGE_SIZE);
                if (PerfMemUsed) ExBurnMemory(LoaderBlock, PerfMemUsed, LoaderBad);
            }
        }
    }

    /* Setup NLS Base and offsets */
    NlsData = LoaderBlock->NlsData;
    ExpNlsTableBase = NlsData->AnsiCodePageData;
    ExpAnsiCodePageDataOffset = 0;
    ExpOemCodePageDataOffset = (ULONG)((ULONG_PTR)NlsData->OemCodePageData -
                                       (ULONG_PTR)NlsData->AnsiCodePageData);
    ExpUnicodeCaseTableDataOffset = (ULONG)((ULONG_PTR)NlsData->UnicodeCodePageData -
                                            (ULONG_PTR)NlsData->AnsiCodePageData);

    /* Initialize the NLS Tables */
    RtlInitNlsTables((PVOID)((ULONG_PTR)ExpNlsTableBase +
                             ExpAnsiCodePageDataOffset),
                     (PVOID)((ULONG_PTR)ExpNlsTableBase +
                             ExpOemCodePageDataOffset),
                     (PVOID)((ULONG_PTR)ExpNlsTableBase +
                             ExpUnicodeCaseTableDataOffset),
                     &ExpNlsTableInfo);
    RtlResetRtlTranslations(&ExpNlsTableInfo);

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: before HalInitSystem");
    /* Now initialize the HAL */
    if (!HalInitSystem(ExpInitializationPhase, LoaderBlock))
    {
        /* HAL failed to initialize, bugcheck */
        KeBugCheck(HAL_INITIALIZATION_FAILED);
    }
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: after HalInitSystem");

#if defined(_M_ARM64)
    /*
     * ARM64: Enable GIC priority masking for IRQL management.
     *
     * Now that HalInitSystem(0) has completed successfully, the HAL's exports
     * (including HalSetGicPriorityMask) are fully initialized and safe to call.
     * Setting KiHalInitialized allows the kernel's IRQL subsystem to use GIC
     * priority masking instead of binary DAIF masking.
     *
     * CRITICAL: This must be set by the kernel, NOT by the HAL, to avoid
     * circular import dependencies (HAL importing kernel exports).
     */
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: setting KiHalInitialized=TRUE");
    extern BOOLEAN KiHalInitialized;
    KiHalInitialized = TRUE;
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: KiHalInitialized set");
#endif

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: about to _enable()");
    /* Make sure interrupts are active now */
    _enable();
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: _enable() done");

    /* Clear the crypto exponent */
    SharedUserData->CryptoExponent = 0;
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: CryptoExponent cleared");

    /* Set global flags for the checked build */
#if DBG
    NtGlobalFlag |= FLG_ENABLE_CLOSE_EXCEPTIONS |
                    FLG_ENABLE_KDEBUG_SYMBOL_LOAD;
#endif

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: about to setup NtSystemRoot");
    /* Setup NT System Root Path */
    Status = RtlStringCbPrintfA(Buffer,
                                sizeof(Buffer),
                                "%c:%s%s",
                                ExpCommandLineHasOption(LoaderBlock->LoadOptions, "MININT") ? 'X' : 'C',
                                (LoaderBlock->NtBootPathName && (LoaderBlock->NtBootPathName[0] == '\\')) ? "" : "\\",
                                LoaderBlock->NtBootPathName ? LoaderBlock->NtBootPathName : "");
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(SESSION3_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: RtlStringCbPrintfA done");

    /* Convert to ANSI_STRING and null-terminate it */
    RtlInitString(&AnsiPath, Buffer);
    Buffer[--AnsiPath.Length] = ANSI_NULL;
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: AnsiPath set up");

    /* Get the string from KUSER_SHARED_DATA's buffer */
    RtlInitEmptyUnicodeString(&NtSystemRoot,
                              SharedUserData->NtSystemRoot,
                              sizeof(SharedUserData->NtSystemRoot));

    /* Now fill it in */
    Status = RtlAnsiStringToUnicodeString(&NtSystemRoot, &AnsiPath, FALSE);
    if (!NT_SUCCESS(Status)) KeBugCheck(SESSION3_INITIALIZATION_FAILED);
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: NtSystemRoot set up");

    /* Setup bugcheck messages */
    KiInitializeBugCheck();
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: KiInitializeBugCheck done");

    /* Setup initial system settings */
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: calling CmGetSystemControlValues");
    CmGetSystemControlValues(LoaderBlock->RegistryBase, CmControlVector);
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: CmGetSystemControlValues done");

    /* Set the Service Pack Number and add it to the CSD Version number if needed */
    CmNtSpBuildNumber = VER_PRODUCTBUILD_QFE;
    if (((CmNtCSDVersion & 0xFFFF0000) == 0) && (CmNtCSDReleaseType == 1))
    {
        CmNtCSDVersion |= (VER_PRODUCTBUILD_QFE << 16);
    }

    /* Add loaded CmNtGlobalFlag value */
    NtGlobalFlag |= CmNtGlobalFlag;

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: before ExInitSystem");
    /* Initialize the executive at phase 0 */
    if (!ExInitSystem()) KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: after ExInitSystem");

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: before MmArmInitSystem(0)");
    /* Initialize the memory manager at phase 0 */
    if (!MmArmInitSystem(0, LoaderBlock)) KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: after MmArmInitSystem(0)");

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: before ExpLoadBootSymbols");
    /* Load boot symbols */
    ExpLoadBootSymbols(LoaderBlock);
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: after ExpLoadBootSymbols");

    /* Check if we should break after symbol load */
    if (KdBreakAfterSymbolLoad)
        DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);

    /* Check if this loader is compatible with NT 5.2 */
    if (LoaderBlock->Extension->Size >= sizeof(LOADER_PARAMETER_EXTENSION))
    {
        /* Setup headless terminal settings */
        HeadlessInit(LoaderBlock);
    }

    /* Set system ranges */
#ifdef _M_AMD64
    SharedUserData->Reserved1 = MM_HIGHEST_USER_ADDRESS_WOW64;
    SharedUserData->Reserved3 = MM_SYSTEM_RANGE_START_WOW64;
#else
    SharedUserData->Reserved1 = (ULONG_PTR)MmHighestUserAddress;
    SharedUserData->Reserved3 = (ULONG_PTR)MmSystemRangeStart;
#endif

    /* Make a copy of the NLS Tables */
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: before ExpInitNls");
    ExpInitNls(LoaderBlock);
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: after ExpInitNls");

    /* Get the kernel's load entry */
    NtosEntry = CONTAINING_RECORD(LoaderBlock->LoadOrderListHead.Flink,
                                  LDR_DATA_TABLE_ENTRY,
                                  InLoadOrderLinks);

    /* Check if this is a service pack */
    if (CmNtCSDVersion & 0xFFFF)
    {
        /* Get the service pack string */
        Status = RtlFindMessage(NtosEntry->DllBase,
                                RT_MESSAGETABLE,
                                MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                                WINDOWS_NT_CSD_STRING,
                                &MsgEntry);
        if (NT_SUCCESS(Status))
        {
            /* Setup the string */
            RtlInitAnsiString(&CSDString, (PCHAR)MsgEntry->Text);

            /* Remove trailing newline */
            while ((CSDString.Length > 0) &&
                   ((CSDString.Buffer[CSDString.Length - 1] == '\r') ||
                    (CSDString.Buffer[CSDString.Length - 1] == '\n')))
            {
                /* Skip the trailing character */
                CSDString.Length--;
            }

            /* Fill the buffer with version information */
            Status = RtlStringCbPrintfA(Buffer,
                                        sizeof(Buffer),
                                        "%Z %u%c",
                                        &CSDString,
                                        (CmNtCSDVersion & 0xFF00) >> 8,
                                        (CmNtCSDVersion & 0xFF) ?
                                        'A' + (CmNtCSDVersion & 0xFF) - 1 :
                                        ANSI_NULL);
        }
        else
        {
            /* Build default string */
            Status = RtlStringCbPrintfA(Buffer,
                                        sizeof(Buffer),
                                        "CSD %04x",
                                        CmNtCSDVersion);
        }

        /* Check for success */
        if (!NT_SUCCESS(Status))
        {
            /* Fail */
            KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, Status, 0, 0, 0);
        }
    }
    else
    {
        /* Then this is a beta */
        Status = RtlStringCbCopyExA(Buffer,
                                    sizeof(Buffer),
                                    VER_PRODUCTBETA_STR,
                                    NULL,
                                    &Remaining,
                                    0);
        if (!NT_SUCCESS(Status))
        {
            /* Fail */
            KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, Status, 0, 0, 0);
        }

        /* Update length */
        CmCSDVersionString.MaximumLength = sizeof(Buffer) - (USHORT)Remaining;
    }

    /* Check if we have an RC number */
    if ((CmNtCSDVersion & 0xFFFF0000) && (CmNtCSDReleaseType == 1))
    {
        /* Check if we have no version data yet */
        if (!(*Buffer))
        {
            /* Set defaults */
            Remaining = sizeof(Buffer);
            RcEnd = Buffer;
        }
        else
        {
            /* Add comma and space */
            Status = RtlStringCbCatExA(Buffer,
                                       sizeof(Buffer),
                                       ", ",
                                       &RcEnd,
                                       &Remaining,
                                       0);
            if (!NT_SUCCESS(Status))
            {
                /* Fail */
                KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, Status, 0, 0, 0);
            }
        }

        /* Add the version format string */
        Status = RtlStringCbPrintfA(RcEnd,
                                    Remaining,
                                    "v.%u",
                                    (CmNtCSDVersion & 0xFFFF0000) >> 16);
        if (!NT_SUCCESS(Status))
        {
            /* Fail */
            KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, Status, 0, 0, 0);
        }
    }

    /* Now setup the final string */
    RtlInitAnsiString(&CSDString, Buffer);
    Status = RtlAnsiStringToUnicodeString(&CmCSDVersionString,
                                          &CSDString,
                                          TRUE);
    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }

    /* Add our version */
    Status = RtlStringCbPrintfA(VersionBuffer,
                                sizeof(VersionBuffer),
                                "%u.%u",
                                VER_PRODUCTMAJORVERSION,
                                VER_PRODUCTMINORVERSION);
    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }

    /* Build the final version string */
    RtlCreateUnicodeStringFromAsciiz(&CmVersionString, VersionBuffer);

    /* Check if the user wants a kernel stack trace database */
    if (NtGlobalFlag & FLG_KERNEL_STACK_TRACE_DB)
    {
        /* FIXME: TODO */
        DPRINT1("Kernel-mode stack trace support not yet present."
                "FLG_KERNEL_STACK_TRACE_DB flag ignored.\n");
    }

    /* Check if he wanted exception logging */
    if (NtGlobalFlag & FLG_ENABLE_EXCEPTION_LOGGING)
    {
        /* FIXME: TODO */
        DPRINT1("Kernel-mode exception logging support not yet present."
                "FLG_ENABLE_EXCEPTION_LOGGING flag ignored.\n");
    }

    /* Initialize the Handle Table */
    ExpInitializeHandleTables();

#if DBG
    /* On checked builds, allocate the system call count table */
    KeServiceDescriptorTable[0].Count =
        ExAllocatePoolWithTag(NonPagedPool,
                              KiServiceLimit * sizeof(ULONG),
                              'llaC');

    /* Use it for the shadow table too */
    KeServiceDescriptorTableShadow[0].Count = KeServiceDescriptorTable[0].Count;

    /* Make sure allocation succeeded */
    if (KeServiceDescriptorTable[0].Count)
    {
        /* Zero the call counts to 0 */
        RtlZeroMemory(KeServiceDescriptorTable[0].Count,
                      KiServiceLimit * sizeof(ULONG));
    }
#endif

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: before ObInitSystem");
    /* Create the Basic Object Manager Types to allow new Object Types */
    if (!ObInitSystem()) KeBugCheck(OBJECT_INITIALIZATION_FAILED);
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: after ObInitSystem");

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: before SeInitSystem");
    /* Load basic Security for other Managers */
    if (!SeInitSystem()) KeBugCheck(SECURITY_INITIALIZATION_FAILED);
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: after SeInitSystem");

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: before PsInitSystem");
    /* Initialize the Process Manager */
    if (!PsInitSystem(LoaderBlock)) KeBugCheck(PROCESS_INITIALIZATION_FAILED);
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: after PsInitSystem");

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: before PpInitSystem");
    /* Initialize the PnP Manager */
    if (!PpInitSystem()) KeBugCheck(PP0_INITIALIZATION_FAILED);
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: after PpInitSystem");

    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: before DbgkInitialize");
    /* Initialize the User-Mode Debugging Subsystem */
    DbgkInitialize();
    EXP_ARM64_LOG("[arm64] ExpInitializeExecutive: after DbgkInitialize");

    /* Calculate the tick count multiplier */
    ExpTickCountMultiplier = ExComputeTickCountMultiplier(KeMaximumIncrement);
    SharedUserData->TickCountMultiplier = ExpTickCountMultiplier;

    /* Set the OS Version */
    SharedUserData->NtMajorVersion = NtMajorVersion;
    SharedUserData->NtMinorVersion = NtMinorVersion;

    /* Set the machine type */
    SharedUserData->ImageNumberLow = IMAGE_FILE_MACHINE_NATIVE;
    SharedUserData->ImageNumberHigh = IMAGE_FILE_MACHINE_NATIVE;

    /* ReactOS magic */
    *(PULONG)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) = 0x8eac705;
}

VOID
NTAPI
MmFreeLoaderBlock(IN PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
VOID
NTAPI
Phase1InitializationDiscard(IN PVOID Context)
{
    PLOADER_PARAMETER_BLOCK LoaderBlock = Context;
    NTSTATUS Status, MsgStatus;
    TIME_FIELDS TimeFields;
    LARGE_INTEGER SystemBootTime, UniversalBootTime, OldTime, Timeout;
    BOOLEAN NoGuiBoot, ResetBias = FALSE, AlternateShell = FALSE;
    PLDR_DATA_TABLE_ENTRY NtosEntry;
    PMESSAGE_RESOURCE_ENTRY MsgEntry;
    PCHAR CommandLine, Y2KHackRequired, SafeBoot, Environment;
    PCHAR StringBuffer, EndBuffer, BeginBuffer, MpString = "";
    PINIT_BUFFER InitBuffer;
    ANSI_STRING TempString;
    ULONG LastTzBias, Length, YearHack = 0, Disposition, MessageCode = 0;
    SIZE_T Size;
    size_t Remaining;
    PRTL_USER_PROCESS_INFORMATION ProcessInfo;
    KEY_VALUE_PARTIAL_INFORMATION KeyPartialInfo;
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle, OptionHandle;
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters = NULL;

    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: ENTRY");

    /* Allocate the initialization buffer */
    InitBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(INIT_BUFFER),
                                       TAG_INIT);
    if (!InitBuffer)
    {
        /* Bugcheck */
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, STATUS_NO_MEMORY, 8, 0, 0);
    }
    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: InitBuffer allocated");

    /* Set to phase 1 */
    ExpInitializationPhase = 1;

    /* Set us at maximum priority */
    KeSetPriorityThread(KeGetCurrentThread(), HIGH_PRIORITY);

    /* Do Phase 1 HAL Initialization */
    if (!HalInitSystem(1, LoaderBlock)) KeBugCheck(HAL1_INITIALIZATION_FAILED);

    /* Get the command line and upcase it */
    CommandLine = (LoaderBlock->LoadOptions ? _strupr(LoaderBlock->LoadOptions) : NULL);

    /* Check if GUI Boot is enabled */
    NoGuiBoot = (CommandLine && strstr(CommandLine, "NOGUIBOOT") != NULL);

    /* Get the SOS setting */
    SosEnabled = (CommandLine && strstr(CommandLine, "SOS") != NULL);

    /* Setup the boot video driver */
    InbvEnableBootDriver(!NoGuiBoot);
    InbvDriverInitialize(LoaderBlock, IDB_MAX_RESOURCES);

    /* Check if GUI boot is enabled */
    if (!NoGuiBoot)
    {
        /* It is, display the boot logo and enable printing strings */
        InbvEnableDisplayString(SosEnabled);
        DisplayBootBitmap(SosEnabled);
    }
    else
    {
        /* Release display ownership if not using GUI boot */
        InbvNotifyDisplayOwnershipLost(NULL);

        /* Don't allow boot-time strings */
        InbvEnableDisplayString(FALSE);
    }

    /* Check if this is LiveCD (WinPE) mode */
    if (CommandLine && strstr(CommandLine, "MININT") != NULL)
    {
        /* Setup WinPE Settings */
        InitIsWinPEMode = TRUE;
        InitWinPEModeType |= (strstr(CommandLine, "INRAM") != NULL) ? 0x80000000 : 0x00000001;
    }

    /* Get the kernel's load entry */
    NtosEntry = CONTAINING_RECORD(LoaderBlock->LoadOrderListHead.Flink,
                                  LDR_DATA_TABLE_ENTRY,
                                  InLoadOrderLinks);

    /* Find the banner message */
    MsgStatus = RtlFindMessage(NtosEntry->DllBase,
                               RT_MESSAGETABLE,
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                               WINDOWS_NT_BANNER,
                               &MsgEntry);

    /* Setup defaults and check if we have a version string */
    StringBuffer = InitBuffer->VersionBuffer;
    BeginBuffer = StringBuffer;
    EndBuffer = StringBuffer;
    Remaining = sizeof(InitBuffer->VersionBuffer);
    if (CmCSDVersionString.Length)
    {
        /* Print the version string */
        Status = RtlStringCbPrintfExA(StringBuffer,
                                      Remaining,
                                      &EndBuffer,
                                      &Remaining,
                                      0,
                                      ": %wZ",
                                      &CmCSDVersionString);
        if (!NT_SUCCESS(Status))
        {
            /* Bugcheck */
            KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 7, 0, 0);
        }
    }
    else
    {
        /* No version */
        *EndBuffer = ANSI_NULL; /* Null-terminate the string */
    }

    /* Skip over the null-terminator to start a new string */
    ++EndBuffer;
    --Remaining;

    /* Build the version number */
    StringBuffer = InitBuffer->VersionNumber;
    Status = RtlStringCbPrintfA(StringBuffer,
                                sizeof(InitBuffer->VersionNumber),
                                "%u.%u",
                                VER_PRODUCTMAJORVERSION,
                                VER_PRODUCTMINORVERSION);
    if (!NT_SUCCESS(Status))
    {
        /* Bugcheck */
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 7, 0, 0);
    }

    /* Check if we had found a banner message */
    if (NT_SUCCESS(MsgStatus))
    {
        /* Create the banner message */
        /* ReactOS specific: Report ReactOS version, NtBuildLab information and reported NT kernel version */
        Status = RtlStringCbPrintfA(EndBuffer,
                                    Remaining,
                                    (PCHAR)MsgEntry->Text,
                                    KERNEL_VERSION_STR,
                                    NtBuildLab,
                                    StringBuffer,
                                    NtBuildNumber & 0xFFFF,
                                    BeginBuffer);
        if (!NT_SUCCESS(Status))
        {
            /* Bugcheck */
            KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 7, 0, 0);
        }
    }
    else
    {
        /* Use hard-coded banner message */
        Status = RtlStringCbCopyA(EndBuffer, Remaining, "REACTOS (R)\r\n");
        if (!NT_SUCCESS(Status))
        {
            /* Bugcheck */
            KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 7, 0, 0);
        }
    }

    /* Display the version string on-screen */
    InbvDisplayString(EndBuffer);

    /* Initialize Power Subsystem in Phase 0 */
    if (!PoInitSystem(0)) KeBugCheck(INTERNAL_POWER_ERROR);

    /* Check for Y2K hack */
    Y2KHackRequired = CommandLine ? strstr(CommandLine, "YEAR") : NULL;
    if (Y2KHackRequired) Y2KHackRequired = strstr(Y2KHackRequired, "=");
    if (Y2KHackRequired) YearHack = atol(Y2KHackRequired + 1);

    /* Query the clock */
    if ((ExCmosClockIsSane) && (HalQueryRealTimeClock(&TimeFields)))
    {
        /* Check if we're using the Y2K hack */
        if (Y2KHackRequired) TimeFields.Year = (CSHORT)YearHack;

        /* Convert to time fields */
        RtlTimeFieldsToTime(&TimeFields, &SystemBootTime);
        UniversalBootTime = SystemBootTime;

        /* Check if real time is GMT */
        if (!ExpRealTimeIsUniversal)
        {
            /* Check if we don't have a valid bias */
            if (ExpLastTimeZoneBias == MAXULONG)
            {
                /* Reset */
                ResetBias = TRUE;
                ExpLastTimeZoneBias = ExpAltTimeZoneBias;
            }

            /* Calculate the bias in seconds */
            ExpTimeZoneBias.QuadPart = Int32x32To64(ExpLastTimeZoneBias * 60,
                                                    10000000);

            /* Set the boot time-zone bias */
            SharedUserData->TimeZoneBias.High2Time = ExpTimeZoneBias.HighPart;
            SharedUserData->TimeZoneBias.LowPart = ExpTimeZoneBias.LowPart;
            SharedUserData->TimeZoneBias.High1Time = ExpTimeZoneBias.HighPart;

            /* Convert the boot time to local time, and set it */
            UniversalBootTime.QuadPart = SystemBootTime.QuadPart +
                                         ExpTimeZoneBias.QuadPart;
        }

        /* Update the system time and notify the system */
        KeSetSystemTime(&UniversalBootTime, &OldTime, FALSE, NULL);
        PoNotifySystemTimeSet();

        /* Remember this as the boot time */
        KeBootTime = UniversalBootTime;
        KeBootTimeBias = 0;
    }

#ifdef CONFIG_SMP
    /*
     * IMPORTANT NOTE:
     * Because ReactOS is a "nice" OS, we do not care _at all_
     * about any number of registered/licensed processors:
     * no usage of KeRegisteredProcessors nor KeLicensedProcessors.
     */
    if (CommandLine)
    {
        PSTR Option;

        /* Check for NUMPROC: maximum number of logical processors
         * that can be started (including dynamically) at run-time */
        Option = strstr(CommandLine, "NUMPROC");
        if (Option) Option = strstr(Option, "=");
        if (Option) KeNumprocSpecified = atol(Option + 1);

        /* Check for BOOTPROC (NT6+ and ReactOS): maximum number
         * of logical processors that can be started at boot-time */
        Option = strstr(CommandLine, "BOOTPROC");
        if (Option) Option = strstr(Option, "=");
        if (Option) KeBootprocSpecified = atol(Option + 1);

        /* Check for MAXPROC (NT6+ and ReactOS): forces the kernel to report
         * as existing the maximum number of processors that can be handled */
        if (strstr(CommandLine, "MAXPROC"))
            KeMaximumProcessors = MAXIMUM_PROCESSORS;
    }

    /* Start Application Processors */
    KeStartAllProcessors();
#endif

    /* Initialize all processors */
    if (!HalAllProcessorsStarted()) KeBugCheck(HAL1_INITIALIZATION_FAILED);

#ifdef CONFIG_SMP
    /* HACK: We should use RtlFindMessage and not only fallback to this */
    MpString = "MultiProcessor Kernel\r\n";
#endif

    /* Setup the "MP" String */
    RtlInitAnsiString(&TempString, MpString);

    /* Make sure to remove the \r\n if we actually have a string */
    while ((TempString.Length > 0) &&
           ((TempString.Buffer[TempString.Length - 1] == '\r') ||
            (TempString.Buffer[TempString.Length - 1] == '\n')))
    {
        /* Skip the trailing character */
        TempString.Length--;
    }

    /* Get the information string from our resource file */
    MsgStatus = RtlFindMessage(NtosEntry->DllBase,
                               RT_MESSAGETABLE,
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                               KeNumberProcessors > 1 ?
                               WINDOWS_NT_INFO_STRING_PLURAL :
                               WINDOWS_NT_INFO_STRING,
                               &MsgEntry);

    /* Get total RAM size, in MiB */
    /* Round size up. Assumed to better match actual physical RAM size */
    Size = KdpGetMemorySizeInMBs(LoaderBlock);

    /* Create the string */
    StringBuffer = InitBuffer->VersionBuffer;
    Status = RtlStringCbPrintfA(StringBuffer,
                                sizeof(InitBuffer->VersionBuffer),
                                NT_SUCCESS(MsgStatus) ?
                                (PCHAR)MsgEntry->Text :
                                "%u System Processor [%Iu MB Memory] %Z\r\n",
                                KeNumberProcessors,
                                Size,
                                &TempString);
    if (!NT_SUCCESS(Status))
    {
        /* Bugcheck */
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 4, 0, 0);
    }

    /* Display RAM and CPU count */
    InbvDisplayString(StringBuffer);

    /* Update the progress bar */
    InbvUpdateProgressBar(5);

    /* Call OB initialization again */
    if (!ObInitSystem()) KeBugCheck(OBJECT1_INITIALIZATION_FAILED);

    /* Initialize Basic System Objects and Worker Threads */
    if (!ExInitSystem()) KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, 0, 0, 1, 0);

    /* Initialize the later stages of the kernel */
    if (!KeInitSystem()) KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, 0, 0, 2, 0);

    /* Call KD Providers at Phase 1 */
    if (!KdInitSystem(ExpInitializationPhase, KeLoaderBlock))
    {
        /* Failed, bugcheck */
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, 0, 0, 3, 0);
    }

    /* Initialize the SRM in Phase 1 */
    if (!SeInitSystem()) KeBugCheck(SECURITY1_INITIALIZATION_FAILED);

    /* Update the progress bar */
    InbvUpdateProgressBar(10);

    /* Create SystemRoot Link */
    Status = ExpCreateSystemRootLink(LoaderBlock);
    if (!NT_SUCCESS(Status))
    {
        /* Failed to create the system root link */
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }

    /* Set up Region Maps, Sections and the Paging File */
    if (!MmInitSystem(1, LoaderBlock)) KeBugCheck(MEMORY1_INITIALIZATION_FAILED);

    /* Create NLS section */
    ExpInitNls(LoaderBlock);

    /* Initialize Cache Views */
    if (!CcInitializeCacheManager()) KeBugCheck(CACHE_INITIALIZATION_FAILED);

    /* Initialize the Registry */
    if (!CmInitSystem1()) KeBugCheck(CONFIG_INITIALIZATION_FAILED);

    /* Initialize Prefetcher */
    CcPfInitializePrefetcher();

    /* Update progress bar */
    InbvUpdateProgressBar(15);

    /* Update timezone information */
    LastTzBias = ExpLastTimeZoneBias;
    ExRefreshTimeZoneInformation(&SystemBootTime);

    /* Check if we're resetting timezone data */
    if (ResetBias)
    {
        /* Convert the local time to system time */
        ExLocalTimeToSystemTime(&SystemBootTime, &UniversalBootTime);
        KeBootTime = UniversalBootTime;
        KeBootTimeBias = 0;

        /* Set the new time */
        KeSetSystemTime(&UniversalBootTime, &OldTime, FALSE, NULL);
    }
    else
    {
        /* Check if the timezone switched and update the time */
        if (LastTzBias != ExpLastTimeZoneBias)
            ZwSetSystemTime(NULL, NULL);
    }

    /* Initialize the File System Runtime Library */
    if (!FsRtlInitSystem()) KeBugCheck(FILE_INITIALIZATION_FAILED);

    /* Initialize range lists */
    RtlInitializeRangeListPackage();

    /* Report all resources used by HAL */
    HalReportResourceUsage();

    /* Call the debugger DLL */
    KdDebuggerInitialize1(LoaderBlock);

    /* Setup PnP Manager in phase 1 */
    if (!PpInitSystem()) KeBugCheck(PP1_INITIALIZATION_FAILED);

    /* Update progress bar */
    InbvUpdateProgressBar(20);

    /* Initialize LPC */
    if (!LpcInitSystem()) KeBugCheck(LPC_INITIALIZATION_FAILED);

    /* Make sure we have a command line */
    if (CommandLine)
    {
        /* Check if this is a safe mode boot */
        SafeBoot = strstr(CommandLine, "SAFEBOOT:");
        if (SafeBoot)
        {
            /* Check what kind of boot this is */
            SafeBoot += 9;
            if (!strncmp(SafeBoot, "MINIMAL", 7))
            {
                /* Minimal mode */
                InitSafeBootMode = 1;
                SafeBoot += 7;
                MessageCode = BOOTING_IN_SAFEMODE_MINIMAL;
            }
            else if (!strncmp(SafeBoot, "NETWORK", 7))
            {
                /* With Networking */
                InitSafeBootMode = 2;
                SafeBoot += 7;
                MessageCode = BOOTING_IN_SAFEMODE_NETWORK;
            }
            else if (!strncmp(SafeBoot, "DSREPAIR", 8))
            {
                /* Domain Server Repair */
                InitSafeBootMode = 3;
                SafeBoot += 8;
                MessageCode = BOOTING_IN_SAFEMODE_DSREPAIR;

            }
            else
            {
                /* Invalid */
                InitSafeBootMode = 0;
            }

            /* Check if there's any settings left */
            if (*SafeBoot)
            {
                /* Check if an alternate shell was requested */
                if (!strncmp(SafeBoot, "(ALTERNATESHELL)", 16))
                {
                    /* Remember this for later */
                    AlternateShell = TRUE;
                }
            }

            /* Find the message to print out */
            Status = RtlFindMessage(NtosEntry->DllBase,
                                    RT_MESSAGETABLE,
                                    MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                                    MessageCode,
                                    &MsgEntry);
            if (NT_SUCCESS(Status))
            {
                /* Display it */
                InbvDisplayString((PCHAR)MsgEntry->Text);
            }
        }
    }

    /* Make sure we have a command line */
    if (CommandLine)
    {
        /* Check if bootlogging is enabled */
        if (strstr(CommandLine, "BOOTLOG"))
        {
            /* Find the message to print out */
            Status = RtlFindMessage(NtosEntry->DllBase,
                                    RT_MESSAGETABLE,
                                    MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                                    BOOTLOG_ENABLED,
                                    &MsgEntry);
            if (NT_SUCCESS(Status))
            {
                /* Display it */
                InbvDisplayString((PCHAR)MsgEntry->Text);
            }

            /* Setup boot logging */
            //IopInitializeBootLogging(LoaderBlock, InitBuffer->BootlogHeader);
        }
    }

    /* Setup the Executive in Phase 2 */
    //ExInitSystemPhase2();

    /* Update progress bar */
    InbvUpdateProgressBar(25);

    /* No KD Time Slip is pending */
    KdpTimeSlipPending = 0;

    /* Initialize in-place execution support */
    XIPInit(LoaderBlock);

    /* Set maximum update to 75% */
    InbvSetProgressBarSubset(25, 75);

    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: before IoInitSystem");
    /* Initialize the I/O Subsystem */
    if (!IoInitSystem(LoaderBlock)) KeBugCheck(IO1_INITIALIZATION_FAILED);
    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: after IoInitSystem");

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * CYCLE 30: Timer ISR Diagnostics
     * Print timer state AFTER IoInitSystem when logging is fully working.
     * These counters help diagnose why timer interrupts aren't firing:
     * - KiInitInterruptsCallCount: Should be 1 if KeInitInterrupts was called
     * - KiTimerStartedFlag: Should be 1 if timer was configured
     * - KiTimerIsrCallCount: Should be >0 if timer ISR is firing
     * - KiTimerCtlReadback: Timer control register state
     */
    DPRINT1("========== CYCLE 30: TIMER ISR DIAGNOSTICS ==========\n");
    DPRINT1("[TIMER DIAG] KeInitInterrupts called: %lu\n", KiInitInterruptsCallCount);
    DPRINT1("[TIMER DIAG] Timer started: %lu\n", KiTimerStartedFlag);
    DPRINT1("[TIMER DIAG] Timer ISR count: %lu\n", KiTimerIsrCallCount);
    DPRINT1("[TIMER DIAG] Timer CTL readback: 0x%lx (ENABLE=%lu, IMASK=%lu, ISTATUS=%lu)\n",
            KiTimerCtlReadback,
            KiTimerCtlReadback & 1,
            (KiTimerCtlReadback >> 1) & 1,
            (KiTimerCtlReadback >> 2) & 1);
    DPRINT1("====================================================\n");
#endif

    /* Set maximum update to 100% */
    InbvSetProgressBarSubset(0, 100);

    /* Are we in safe mode? */
    if (InitSafeBootMode)
    {
        /* Open the safe boot key */
        RtlInitUnicodeString(&KeyName,
                             L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET"
                             L"\\CONTROL\\SAFEBOOT");
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE,
                                   NULL,
                                   NULL);
        Status = ZwOpenKey(&KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes);
        if (NT_SUCCESS(Status))
        {
            /* First check if we have an alternate shell */
            if (AlternateShell)
            {
                /* Make sure that the registry has one setup */
                RtlInitUnicodeString(&KeyName, L"AlternateShell");
                Status = NtQueryValueKey(KeyHandle,
                                         &KeyName,
                                         KeyValuePartialInformation,
                                         &KeyPartialInfo,
                                         sizeof(KeyPartialInfo),
                                         &Length);
                if (!(NT_SUCCESS(Status) || Status == STATUS_BUFFER_OVERFLOW))
                {
                    AlternateShell = FALSE;
                }
            }

            /* Create the option key */
            RtlInitUnicodeString(&KeyName, L"Option");
            InitializeObjectAttributes(&ObjectAttributes,
                                       &KeyName,
                                       OBJ_CASE_INSENSITIVE,
                                       KeyHandle,
                                       NULL);
            Status = ZwCreateKey(&OptionHandle,
                                 KEY_ALL_ACCESS,
                                 &ObjectAttributes,
                                 0,
                                 NULL,
                                 REG_OPTION_VOLATILE,
                                 &Disposition);
            NtClose(KeyHandle);

            /* Check if the key create worked */
            if (NT_SUCCESS(Status))
            {
                /* Write the safe boot type */
                RtlInitUnicodeString(&KeyName, L"OptionValue");
                NtSetValueKey(OptionHandle,
                              &KeyName,
                              0,
                              REG_DWORD,
                              &InitSafeBootMode,
                              sizeof(InitSafeBootMode));

                /* Check if we have to use an alternate shell */
                if (AlternateShell)
                {
                    /* Remember this for later */
                    Disposition = TRUE;
                    RtlInitUnicodeString(&KeyName, L"UseAlternateShell");
                    NtSetValueKey(OptionHandle,
                                  &KeyName,
                                  0,
                                  REG_DWORD,
                                  &Disposition,
                                  sizeof(Disposition));
                }

                /* Close the options key handle */
                NtClose(OptionHandle);
            }
        }
    }

    /* Are we in Win PE mode? */
    if (InitIsWinPEMode)
    {
        /* Open the safe control key */
        RtlInitUnicodeString(&KeyName,
                             L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET"
                             L"\\CONTROL");
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE,
                                   NULL,
                                   NULL);
        Status = ZwOpenKey(&KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            /* Bugcheck */
            KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 6, 0, 0);
        }

        /* Create the MiniNT key */
        RtlInitUnicodeString(&KeyName, L"MiniNT");
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE,
                                   KeyHandle,
                                   NULL);
        Status = ZwCreateKey(&OptionHandle,
                             KEY_ALL_ACCESS,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_VOLATILE,
                             &Disposition);
        if (!NT_SUCCESS(Status))
        {
            /* Bugcheck */
            KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, Status, 6, 0, 0);
        }

        /* Close the handles */
        NtClose(KeyHandle);
        NtClose(OptionHandle);
    }

    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: before MmArmInitSystem(2)");
    /* FIXME: This doesn't do anything for now */
    MmArmInitSystem(2, LoaderBlock);
    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: after MmArmInitSystem(2)");

    /* Update progress bar */
    InbvUpdateProgressBar(80);

    /* Initialize VDM support */
#if defined(_M_IX86)
    KeI386VdmInitialize();
#endif

    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: before PoInitSystem(1)");
    /* Initialize Power Subsystem in Phase 1*/
    if (!PoInitSystem(1)) KeBugCheck(INTERNAL_POWER_ERROR);
    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: after PoInitSystem(1)");

    /* Update progress bar */
    InbvUpdateProgressBar(90);

    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: before PsInitSystem(Phase1)");
    /* Initialize the Process Manager at Phase 1 */
    if (!PsInitSystem(LoaderBlock)) KeBugCheck(PROCESS1_INITIALIZATION_FAILED);
    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: after PsInitSystem(Phase1)");

    /* Make sure nobody touches the loader block again */
    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: before MmFreeLoaderBlock");
    if (LoaderBlock == KeLoaderBlock) KeLoaderBlock = NULL;
    MmFreeLoaderBlock(LoaderBlock);
    LoaderBlock = Context = NULL;
    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: after MmFreeLoaderBlock");

    /* Initialize the SRM in phase 1 */
    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: before SeRmInitPhase1");
    if (!SeRmInitPhase1()) KeBugCheck(PROCESS1_INITIALIZATION_FAILED);
    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: after SeRmInitPhase1");

    /* Update progress bar */
    InbvUpdateProgressBar(100);

    /* Clear the screen */
    if (InbvBootDriverInstalled) FinalizeBootLogo();

    /* Allow strings to be displayed */
    InbvEnableDisplayString(TRUE);

    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: before ExpLoadInitialProcess (SMSS)");
    /* Launch initial process */
    ProcessInfo = &InitBuffer->ProcessInfo;
    ExpLoadInitialProcess(InitBuffer, &ProcessParameters, &Environment);
    EXP_ARM64_LOG("[arm64] Phase1InitializationDiscard: after ExpLoadInitialProcess (SMSS started!)");

    /* Wait 5 seconds for initial process to initialize */
    Timeout.QuadPart = Int32x32To64(5, -10000000);
    Status = ZwWaitForSingleObject(ProcessInfo->ProcessHandle, FALSE, &Timeout);
    if (Status == STATUS_SUCCESS)
    {
        /* Failed, display error */
        DPRINT1("INIT: Session Manager terminated.\n");

        /* Bugcheck the system if SMSS couldn't initialize */
        KeBugCheck(SESSION5_INITIALIZATION_FAILED);
    }

    /* Close process handles */
    ZwClose(ProcessInfo->ThreadHandle);
    ZwClose(ProcessInfo->ProcessHandle);

    /* Free the initial process environment */
    Size = 0;
    ZwFreeVirtualMemory(NtCurrentProcess(),
                        (PVOID*)&Environment,
                        &Size,
                        MEM_RELEASE);

    /* Free the initial process parameters */
    Size = 0;
    ZwFreeVirtualMemory(NtCurrentProcess(),
                        (PVOID*)&ProcessParameters,
                        &Size,
                        MEM_RELEASE);

    /* Increase init phase */
    ExpInitializationPhase++;

    /* Free the boot buffer */
    ExFreePoolWithTag(InitBuffer, TAG_INIT);
}

VOID
NTAPI
Phase1Initialization(IN PVOID Context)
{
    /* Do the .INIT part of Phase 1 which we can free later */
    Phase1InitializationDiscard(Context);

    /* Jump into zero page thread */
    MmZeroPageThread();
}
