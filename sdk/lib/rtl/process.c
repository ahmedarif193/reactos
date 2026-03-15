/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * FILE:              lib/rtl/process.c
 * PURPOSE:           Process functions
 * PROGRAMMER:        Alex Ionescu (alex@relsoft.net)
 *                    Ariadne (ariadne@xs4all.nl)
 *                    Eric Kohl
 */

/* INCLUDES ****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

/* INTERNAL FUNCTIONS *******************************************************/

NTSTATUS
NTAPI
RtlpMapFile(PUNICODE_STRING ImageFileName,
            ULONG Attributes,
            PHANDLE Section)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    HANDLE hFile = NULL;
    IO_STATUS_BLOCK IoStatusBlock;

    /* Open the Image File */
    InitializeObjectAttributes(&ObjectAttributes,
                               ImageFileName,
                               Attributes & (OBJ_CASE_INSENSITIVE | OBJ_INHERIT),
                               NULL,
                               NULL);
    Status = ZwOpenFile(&hFile,
                        SYNCHRONIZE | FILE_EXECUTE | FILE_READ_DATA,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_DELETE | FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to read image file from disk, Status = 0x%08X\n", Status);
        return Status;
    }

    /* Now create a section for this image */
    Status = ZwCreateSection(Section,
                             SECTION_ALL_ACCESS,
                             NULL,
                             NULL,
                             PAGE_EXECUTE,
                             SEC_IMAGE,
                             hFile);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create section for image file, Status = 0x%08X\n", Status);
    }

    ZwClose(hFile);
    return Status;
}

static
ULONG
NTAPI
RtlpDebugFnv1a32(
    _In_reads_bytes_(Size) CONST VOID *Buffer,
    _In_ SIZE_T Size)
{
    CONST UCHAR *Bytes;
    ULONG Hash;

    Bytes = (CONST UCHAR *)Buffer;
    Hash = 2166136261u;

    while (Size--)
    {
        Hash ^= *Bytes++;
        Hash *= 16777619u;
    }

    return Hash;
}

static
ULONG
NTAPI
RtlpHashProcessParameters(
    _In_ PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
    _Out_opt_ PSIZE_T HashSpan)
{
    SIZE_T Size;

    if (HashSpan) *HashSpan = 0;
    if (ProcessParameters == NULL) return 0;

    Size = ProcessParameters->MaximumLength;
    if ((Size < sizeof(*ProcessParameters)) || (Size > 0x100000))
    {
        Size = sizeof(*ProcessParameters);
    }

    if (HashSpan) *HashSpan = Size;
    return RtlpDebugFnv1a32(ProcessParameters, Size);
}

/* FUNCTIONS ****************************************************************/

NTSTATUS
NTAPI
RtlpInitEnvironment(HANDLE ProcessHandle,
                    PPEB Peb,
                    PRTL_USER_PROCESS_PARAMETERS ProcessParameters)
{
    NTSTATUS Status;
    PVOID BaseAddress = NULL;
    SIZE_T EnviroSize;
    SIZE_T Size;
    PWCHAR Environment = NULL;
    DPRINT("RtlpInitEnvironment(ProcessHandle: %p, Peb: %p Params: %p)\n",
            ProcessHandle, Peb, ProcessParameters);

    /* Give the caller 1MB if he requested it */
    if (ProcessParameters->Flags & RTL_USER_PROCESS_PARAMETERS_RESERVE_1MB)
    {
        /* Give 1MB starting at 0x4 */
        BaseAddress = (PVOID)4;
        EnviroSize = (1024 * 1024) - 256;
        Status = ZwAllocateVirtualMemory(ProcessHandle,
                                         &BaseAddress,
                                         0,
                                         &EnviroSize,
                                         MEM_RESERVE,
                                         PAGE_READWRITE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to reserve 1MB of space\n");
            return Status;
        }
    }

    /* Find the end of the Enviroment Block */
    if ((Environment = (PWCHAR)ProcessParameters->Environment))
    {
        while (*Environment++) while (*Environment++);

        /* Calculate the size of the block */
        EnviroSize = (ULONG)((ULONG_PTR)Environment -
                             (ULONG_PTR)ProcessParameters->Environment);

        /* Allocate and Initialize new Environment Block */
        Size = EnviroSize;
        Status = ZwAllocateVirtualMemory(ProcessHandle,
                                         &BaseAddress,
                                         0,
                                         &Size,
                                         MEM_RESERVE | MEM_COMMIT,
                                         PAGE_READWRITE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to allocate Environment Block\n");
            return Status;
        }

        /* Write the Environment Block */
        ZwWriteVirtualMemory(ProcessHandle,
                             BaseAddress,
                             ProcessParameters->Environment,
                             EnviroSize,
                             NULL);

        /* Save pointer */
        ProcessParameters->Environment = BaseAddress;
    }

    /* Now allocate space for the Parameter Block */
    BaseAddress = NULL;
    Size = ProcessParameters->MaximumLength;
    Status = ZwAllocateVirtualMemory(ProcessHandle,
                                     &BaseAddress,
                                     0,
                                     &Size,
                                     MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to allocate Parameter Block\n");
        return Status;
    }

    /* Write the Parameter Block */
    Status = ZwWriteVirtualMemory(ProcessHandle,
                                  BaseAddress,
                                  ProcessParameters,
                                  ProcessParameters->Length,
                                  NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to write the Parameter Block\n");
        return Status;
    }

    /* Write pointer to Parameter Block */
    Status = ZwWriteVirtualMemory(ProcessHandle,
                                  &Peb->ProcessParameters,
                                  &BaseAddress,
                                  sizeof(BaseAddress),
                                  NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to write pointer to Parameter Block\n");
        return Status;
    }

    /* Return */
    return STATUS_SUCCESS;
}

/*
 * @implemented
 *
 * Creates a process and its initial thread.
 *
 * NOTES:
 *  - The first thread is created suspended, so it needs a manual resume!!!
 *  - If ParentProcess is NULL, current process is used
 *  - ProcessParameters must be normalized
 *  - Attributes are object attribute flags used when opening the ImageFileName.
 *    Valid flags are OBJ_INHERIT and OBJ_CASE_INSENSITIVE.
 *
 * -Gunnar
 */
NTSTATUS
NTAPI
RtlCreateUserProcess(IN PUNICODE_STRING ImageFileName,
                     IN ULONG Attributes,
                     IN OUT PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
                     IN PSECURITY_DESCRIPTOR ProcessSecurityDescriptor OPTIONAL,
                     IN PSECURITY_DESCRIPTOR ThreadSecurityDescriptor OPTIONAL,
                     IN HANDLE ParentProcess OPTIONAL,
                     IN BOOLEAN InheritHandles,
                     IN HANDLE DebugPort OPTIONAL,
                     IN HANDLE ExceptionPort OPTIONAL,
                     OUT PRTL_USER_PROCESS_INFORMATION ProcessInfo)
{
    NTSTATUS Status;
    HANDLE hSection;
    PROCESS_BASIC_INFORMATION ProcessBasicInfo;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DebugString = RTL_CONSTANT_STRING(L"\\WindowsSS");
    HANDLE StdInBefore, StdOutBefore, StdErrBefore, CwdHandleBefore;
    SIZE_T ProcessParamsHashSize;
    ULONG ProcessParamsHashBefore, ProcessParamsHashAfter;
    /* Map and Load the File */
    DPRINT1("[arm64][SMSS] ENTRY StdIn=%p ProcessParams=%p\n",
            ProcessParameters->StandardInput, ProcessParameters);
    DPRINT1("[arm64][SMSS] RtlpMapFile('%wZ') begin\n", ImageFileName);
    Status = RtlpMapFile(ImageFileName,
                         Attributes,
                         &hSection);
    DPRINT1("[arm64][SMSS] RtlpMapFile returned 0x%lx StdIn=%p\n",
            Status, ProcessParameters->StandardInput);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Could not map process image\n");
        return Status;
    }

    /* Clean out the current directory handle if we won't use it */
    if (!InheritHandles) ProcessParameters->CurrentDirectory.Handle = NULL;

    /* Use us as parent if none other specified */
    if (!ParentProcess) ParentProcess = NtCurrentProcess();

    /* Initialize the Object Attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               0,
                               NULL,
                               ProcessSecurityDescriptor);

    /*
     * If FLG_ENABLE_CSRDEBUG is used, then CSRSS is created under the
     * watch of WindowsSS
     */
    if ((RtlGetNtGlobalFlags() & FLG_ENABLE_CSRDEBUG) &&
        (wcsstr(ImageFileName->Buffer, L"csrss")))
    {
        ObjectAttributes.ObjectName = &DebugString;
    }

    StdInBefore = ProcessParameters->StandardInput;
    StdOutBefore = ProcessParameters->StandardOutput;
    StdErrBefore = ProcessParameters->StandardError;
    CwdHandleBefore = ProcessParameters->CurrentDirectory.Handle;
    ProcessParamsHashBefore = RtlpHashProcessParameters(ProcessParameters,
                                                        &ProcessParamsHashSize);
    DPRINT1("[arm64][SMSS] PP snapshot before ZwCreateProcess: Hash=0x%lx Size=0x%Ix "
            "StdIn=%p StdOut=%p StdErr=%p CurDirH=%p\n",
            ProcessParamsHashBefore,
            ProcessParamsHashSize,
            StdInBefore,
            StdOutBefore,
            StdErrBefore,
            CwdHandleBefore);

    /* Create Kernel Process Object */
    DPRINT1("[arm64][SMSS] ZwCreateProcess begin\n");
    Status = ZwCreateProcess(&ProcessInfo->ProcessHandle,
                             PROCESS_ALL_ACCESS,
                             &ObjectAttributes,
                             ParentProcess,
                             InheritHandles,
                             hSection,
                             DebugPort,
                             ExceptionPort);
    ProcessParamsHashAfter = RtlpHashProcessParameters(ProcessParameters, NULL);
    DPRINT1("[arm64][SMSS] ZwCreateProcess returned 0x%lx StdIn=%p Hash=0x%lx->0x%lx\n",
            Status,
            ProcessParameters->StandardInput,
            ProcessParamsHashBefore,
            ProcessParamsHashAfter);

    if ((ProcessParamsHashBefore != ProcessParamsHashAfter) ||
        (StdInBefore != ProcessParameters->StandardInput) ||
        (StdOutBefore != ProcessParameters->StandardOutput) ||
        (StdErrBefore != ProcessParameters->StandardError) ||
        (CwdHandleBefore != ProcessParameters->CurrentDirectory.Handle))
    {
        DPRINT1("[arm64][SMSS][CORRUPTION] ProcessParameters mutated across ZwCreateProcess!\n");
        DPRINT1("[arm64][SMSS][CORRUPTION] StdIn %p -> %p, StdOut %p -> %p, StdErr %p -> %p, CurDirH %p -> %p, Hash 0x%lx -> 0x%lx\n",
                StdInBefore,
                ProcessParameters->StandardInput,
                StdOutBefore,
                ProcessParameters->StandardOutput,
                StdErrBefore,
                ProcessParameters->StandardError,
                CwdHandleBefore,
                ProcessParameters->CurrentDirectory.Handle,
                ProcessParamsHashBefore,
                ProcessParamsHashAfter);
#if DBG
        DbgBreakPoint();
#endif
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Could not create Kernel Process Object\n");
        ZwClose(hSection);
        return Status;
    }

    /* Get some information on the image */
    DPRINT1("[arm64][SMSS] ZwQuerySection begin\n");
    Status = ZwQuerySection(hSection,
                            SectionImageInformation,
                            &ProcessInfo->ImageInformation,
                            sizeof(SECTION_IMAGE_INFORMATION),
                            NULL);
    DPRINT1("[arm64][SMSS] ZwQuerySection returned 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Could not query Section Info\n");
        ZwClose(ProcessInfo->ProcessHandle);
        ZwClose(hSection);
        return Status;
    }

    /* Get some information about the process */
    DPRINT1("[arm64][SMSS] ZwQueryInformationProcess begin\n");
    Status = ZwQueryInformationProcess(ProcessInfo->ProcessHandle,
                                       ProcessBasicInformation,
                                       &ProcessBasicInfo,
                                       sizeof(ProcessBasicInfo),
                                       NULL);
    DPRINT1("[arm64][SMSS] ZwQueryInformationProcess returned 0x%lx PebBase=%p\n",
            Status, ProcessBasicInfo.PebBaseAddress);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Could not query Process Info\n");
        ZwClose(ProcessInfo->ProcessHandle);
        ZwClose(hSection);
        return Status;
    }

    /* Duplicate the standard handles */
    DPRINT1("[arm64][SMSS] StdIn=%p StdOut=%p StdErr=%p ParentProc=%p\n",
            ProcessParameters->StandardInput,
            ProcessParameters->StandardOutput,
            ProcessParameters->StandardError,
            ParentProcess);
    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        if (ProcessParameters->StandardInput)
        {
            DPRINT1("[arm64][SMSS] ZwDuplicateObject(StdIn=%p) begin\n",
                    ProcessParameters->StandardInput);
            Status = ZwDuplicateObject(ParentProcess,
                                       ProcessParameters->StandardInput,
                                       ProcessInfo->ProcessHandle,
                                       &ProcessParameters->StandardInput,
                                       0,
                                       0,
                                       DUPLICATE_SAME_ACCESS |
                                       DUPLICATE_SAME_ATTRIBUTES);
            DPRINT1("[arm64][SMSS] ZwDuplicateObject(StdIn) returned 0x%lx\n", Status);
            if (!NT_SUCCESS(Status))
            {
                _SEH2_LEAVE;
            }
        }

        if (ProcessParameters->StandardOutput)
        {
            DPRINT1("[arm64][SMSS] ZwDuplicateObject(StdOut=%p) begin\n",
                    ProcessParameters->StandardOutput);
            Status = ZwDuplicateObject(ParentProcess,
                                       ProcessParameters->StandardOutput,
                                       ProcessInfo->ProcessHandle,
                                       &ProcessParameters->StandardOutput,
                                       0,
                                       0,
                                       DUPLICATE_SAME_ACCESS |
                                       DUPLICATE_SAME_ATTRIBUTES);
            DPRINT1("[arm64][SMSS] ZwDuplicateObject(StdOut) returned 0x%lx\n", Status);
            if (!NT_SUCCESS(Status))
            {
                _SEH2_LEAVE;
            }
        }

        if (ProcessParameters->StandardError)
        {
            DPRINT1("[arm64][SMSS] ZwDuplicateObject(StdErr=%p) begin\n",
                    ProcessParameters->StandardError);
            Status = ZwDuplicateObject(ParentProcess,
                                       ProcessParameters->StandardError,
                                       ProcessInfo->ProcessHandle,
                                       &ProcessParameters->StandardError,
                                       0,
                                       0,
                                       DUPLICATE_SAME_ACCESS |
                                       DUPLICATE_SAME_ATTRIBUTES);
            DPRINT1("[arm64][SMSS] ZwDuplicateObject(StdErr) returned 0x%lx\n", Status);
            if (!NT_SUCCESS(Status))
            {
                _SEH2_LEAVE;
            }
        }
    }
    _SEH2_FINALLY
    {
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("[arm64][SMSS] Handle dup failed: 0x%lx\n", Status);
            ZwClose(ProcessInfo->ProcessHandle);
            ZwClose(hSection);
        }
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        return Status;

    /* Create Process Environment */
    DPRINT1("[arm64][SMSS] RtlpInitEnvironment begin\n");
    Status = RtlpInitEnvironment(ProcessInfo->ProcessHandle,
                                 ProcessBasicInfo.PebBaseAddress,
                                 ProcessParameters);
    DPRINT1("[arm64][SMSS] RtlpInitEnvironment returned 0x%lx\n", Status);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Could not Create Process Environment\n");
        ZwClose(ProcessInfo->ProcessHandle);
        ZwClose(hSection);
        return Status;
    }

    /* Create the first Thread */
    DPRINT1("[arm64][SMSS] RtlCreateUserThread begin\n");
    Status = RtlCreateUserThread(ProcessInfo->ProcessHandle,
                                 ThreadSecurityDescriptor,
                                 TRUE,
                                 ProcessInfo->ImageInformation.ZeroBits,
                                 ProcessInfo->ImageInformation.MaximumStackSize,
                                 ProcessInfo->ImageInformation.CommittedStackSize,
                                 ProcessInfo->ImageInformation.TransferAddress,
                                 ProcessBasicInfo.PebBaseAddress,
                                 &ProcessInfo->ThreadHandle,
                                 &ProcessInfo->ClientId);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Could not Create Thread\n");
        ZwClose(ProcessInfo->ProcessHandle);
        ZwClose(hSection); /* Don't try to optimize this on top! */
        return Status;
    }

    /* Close the Section Handle and return */
    ZwClose(hSection);
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlEncodePointer(IN PVOID Pointer)
{
    ULONG Cookie;

    if (!SharedUserData)
        return Pointer;

    Cookie = SharedUserData->Cookie;

    /* Avoid early-boot failures: if no cookie is available, return the pointer unchanged */
    if (Cookie == 0)
        return Pointer;

    return (PVOID)((ULONG_PTR)Pointer ^ Cookie);
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlDecodePointer(IN PVOID Pointer)
{
    return RtlEncodePointer(Pointer);
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlEncodeSystemPointer(IN PVOID Pointer)
{
    ULONG Cookie;

    if (!SharedUserData)
        return Pointer;

    Cookie = SharedUserData->Cookie;

    if (Cookie == 0)
        return Pointer;

    return (PVOID)((ULONG_PTR)Pointer ^ Cookie);
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlDecodeSystemPointer(IN PVOID Pointer)
{
    return RtlEncodeSystemPointer(Pointer);
}

/*
 * @implemented
 *
 * NOTES:
 *   Implementation based on the documentation from:
 *   http://www.geoffchappell.com/studies/windows/win32/ntdll/api/rtl/peb/setprocessiscritical.htm
 */
NTSTATUS
__cdecl
RtlSetProcessIsCritical(IN BOOLEAN NewValue,
                        OUT PBOOLEAN OldValue OPTIONAL,
                        IN BOOLEAN NeedBreaks)
{
    ULONG BreakOnTermination;

    /* Initialize to FALSE */
    if (OldValue) *OldValue = FALSE;

    /* Fail, if the critical breaks flag is required but is not set */
    if ((NeedBreaks) &&
        !(NtCurrentPeb()->NtGlobalFlag & FLG_ENABLE_SYSTEM_CRIT_BREAKS))
    {
        return STATUS_UNSUCCESSFUL;
    }

    /* Check if the caller wants the old value */
    if (OldValue)
    {
        /* Query and return the old break on termination flag for the process */
        ZwQueryInformationProcess(NtCurrentProcess(),
                                  ProcessBreakOnTermination,
                                  &BreakOnTermination,
                                  sizeof(ULONG),
                                  NULL);
        *OldValue = (BOOLEAN)BreakOnTermination;
    }

    /* Set the break on termination flag for the process */
    BreakOnTermination = NewValue;
    return ZwSetInformationProcess(NtCurrentProcess(),
                                   ProcessBreakOnTermination,
                                   &BreakOnTermination,
                                   sizeof(ULONG));
}

ULONG
NTAPI
RtlGetCurrentProcessorNumber(VOID)
{
    /* Forward to kernel */
    return NtGetCurrentProcessorNumber();
}

_IRQL_requires_max_(APC_LEVEL)
ULONG
NTAPI
RtlRosGetAppcompatVersion(VOID)
{
    /* Get the current PEB */
    PPEB Peb = RtlGetCurrentPeb();
    if (Peb == NULL)
    {
        /* Default to Server 2003 */
        return _WIN32_WINNT_WS03;
    }

    /* Calculate OS version from PEB fields */
    return (Peb->OSMajorVersion << 8) | Peb->OSMinorVersion;
}
