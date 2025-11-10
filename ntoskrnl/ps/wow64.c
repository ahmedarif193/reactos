/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ps/wow64.c
 * PURPOSE:         Process Manager wow64 helpers
 * PROGRAMMERS:     (c) 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <reactos/wow64apc.h>
#include <reactos/wow64cpu.h>

#ifdef _M_AMD64

#ifdef __cplusplus
#error This file must be compiled as C
#endif

#define WOW64_CPU_AREA_FLAG_COMPAT_CONTEXT   0x00000001
#define WOW64_CPU_AREA_FLAG_NATIVE_CONTEXT   0x00000002
#define WOW64_CPU_AREA_FLAG_PENDING_APC      0x00000004
#define WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS  0x00000008
#define WOW64_CPU_AREA_FLAG_HOST_CONTEXT     0x00000010

typedef struct _PSP_WOW64_CPU_AREA_SNAPSHOT
{
    ULONG Size;
    ULONG Flags;
    ULONG_PTR CompatContext;
    ULONG CompatContextLength;
    ULONG_PTR NativeContext;
    ULONG NativeContextLength;
    ULONG_PTR HostContext;
    ULONG HostContextLength;
    ULONG_PTR PendingUserContext;
    ULONG_PTR PendingUserRoutine;
    ULONG_PTR PendingSystemArgument1;
    ULONG_PTR PendingSystemArgument2;
} PSP_WOW64_CPU_AREA_SNAPSHOT, *PPSP_WOW64_CPU_AREA_SNAPSHOT;

static
NTSTATUS
PspWow64ReadCpuArea(
    _In_ PWOW64_CPU_AREA CpuArea,
    _Out_ PPSP_WOW64_CPU_AREA_SNAPSHOT Snapshot)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!CpuArea || !Snapshot)
    {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        ProbeForRead(CpuArea, sizeof(*Snapshot), sizeof(ULONG));
        RtlCopyMemory(Snapshot, CpuArea, sizeof(*Snapshot));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Snapshot->Size < sizeof(*Snapshot))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
PspWow64CopyFromUser(
    _Out_writes_bytes_(Length) PVOID Destination,
    _In_ PVOID Source,
    _In_ SIZE_T Length)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Destination || !Source || Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        ProbeForRead(Source, Length, sizeof(ULONG));
        RtlCopyMemory(Destination, Source, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
NTSTATUS
PspWow64CopyToUser(
    _In_ PVOID Destination,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ SIZE_T Length)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Destination || !Source || Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        ProbeForWrite(Destination, Length, sizeof(ULONG));
        RtlCopyMemory(Destination, Source, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
NTSTATUS
PspWow64SetCpuAreaFlags(
    _In_ PWOW64_CPU_AREA CpuArea,
    _In_ ULONG FlagsToSet)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Flags;

    if (!CpuArea)
    {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        ProbeForRead(&CpuArea->Flags, sizeof(Flags), sizeof(ULONG));
        Flags = CpuArea->Flags | FlagsToSet;
        ProbeForWrite(&CpuArea->Flags, sizeof(Flags), sizeof(ULONG));
        CpuArea->Flags = Flags;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
Wow64CpuSetPendingApc(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_ const WOW64_APC_CONTEXT *ApcContext,
    _In_ ULONG_PTR SystemArgument1,
    _In_ ULONG_PTR SystemArgument2)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!CpuArea || !ApcContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        ProbeForWrite(&CpuArea->PendingUserContext, sizeof(CpuArea->PendingUserContext), sizeof(ULONG));
        ProbeForWrite(&CpuArea->PendingUserRoutine, sizeof(CpuArea->PendingUserRoutine), sizeof(ULONG));
        ProbeForWrite(&CpuArea->PendingSystemArgument1, sizeof(CpuArea->PendingSystemArgument1), sizeof(ULONG));
        ProbeForWrite(&CpuArea->PendingSystemArgument2, sizeof(CpuArea->PendingSystemArgument2), sizeof(ULONG));

        CpuArea->PendingUserContext = ApcContext->UserContext;
        CpuArea->PendingUserRoutine = ApcContext->UserRoutine;
        CpuArea->PendingSystemArgument1 = SystemArgument1;
        CpuArea->PendingSystemArgument2 = SystemArgument2;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return PspWow64SetCpuAreaFlags(CpuArea, WOW64_CPU_AREA_FLAG_PENDING_APC);
}

typedef struct _PSP_WOW64_GET_SET_CTX_CONTEXT
{
    KAPC Apc;
    KEVENT Event;
    WOW64_CONTEXT Context;
    NTSTATUS Status;
} PSP_WOW64_GET_SET_CTX_CONTEXT, *PPSP_WOW64_GET_SET_CTX_CONTEXT;

static
VOID
PspWow64GetOrSetContextKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2);

NTSTATUS
NTAPI
CpuThreadInit(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_opt_ PVOID ThreadContext)
{
    PSP_WOW64_CPU_AREA_SNAPSHOT Area;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ThreadContext);

    if (!CpuArea)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = PspWow64ReadCpuArea(CpuArea, &Area);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
CpuThreadTerm(
    _Inout_ PWOW64_CPU_AREA CpuArea)
{
    UNREFERENCED_PARAMETER(CpuArea);
}

NTSTATUS
NTAPI
Wow64CpuDispatchPendingApc(
    _Inout_ PWOW64_CPU_AREA CpuArea)
{
    PSP_WOW64_CPU_AREA_SNAPSHOT Area;
    NTSTATUS Status;

    if (!CpuArea)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = PspWow64ReadCpuArea(CpuArea, &Area);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!(Area.Flags & WOW64_CPU_AREA_FLAG_PENDING_APC))
    {
        return STATUS_NOT_FOUND;
    }

    if (!(Area.Flags & WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS))
    {
        Status = PspWow64SetCpuAreaFlags(CpuArea, WOW64_CPU_AREA_FLAG_APC_IN_PROGRESS);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Wow64CpuGetContext(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _Out_writes_bytes_(ContextLength) PVOID Context,
    _In_ ULONG ContextLength)
{
    PSP_WOW64_CPU_AREA_SNAPSHOT Area;
    SIZE_T BytesToCopy;
    NTSTATUS Status;

    if (!CpuArea || !Context || ContextLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = PspWow64ReadCpuArea(CpuArea, &Area);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!(Area.Flags & WOW64_CPU_AREA_FLAG_COMPAT_CONTEXT) ||
        !Area.CompatContext ||
        Area.CompatContextLength == 0)
    {
        return STATUS_NOT_FOUND;
    }

    if (Area.CompatContextLength < ContextLength)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    BytesToCopy = ContextLength;
    Status = PspWow64CopyFromUser(Context,
                                  (PVOID)(ULONG_PTR)Area.CompatContext,
                                  BytesToCopy);

    return Status;
}

NTSTATUS
NTAPI
Wow64CpuSetContext(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_reads_bytes_(ContextLength) const VOID *Context,
    _In_ ULONG ContextLength)
{
    PSP_WOW64_CPU_AREA_SNAPSHOT Area;
    NTSTATUS Status;

    if (!CpuArea || !Context || ContextLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = PspWow64ReadCpuArea(CpuArea, &Area);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!Area.CompatContext || Area.CompatContextLength == 0)
    {
        return STATUS_NOT_FOUND;
    }

    if (Area.CompatContextLength < ContextLength)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Status = PspWow64CopyToUser((PVOID)(ULONG_PTR)Area.CompatContext,
                                 Context,
                                 ContextLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = PspWow64SetCpuAreaFlags(CpuArea, WOW64_CPU_AREA_FLAG_COMPAT_CONTEXT);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    _SEH2_TRY
    {
        ProbeForWrite(&CpuArea->CompatContextLength, sizeof(ULONG), sizeof(ULONG));
        CpuArea->CompatContextLength = ContextLength;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
Wow64PrepareForException(
    _Inout_ PWOW64_CPU_AREA CpuArea,
    _In_reads_bytes_(sizeof(CONTEXT)) const CONTEXT *HostContext)
{
    PSP_WOW64_CPU_AREA_SNAPSHOT Area;
    NTSTATUS Status;

    if (!CpuArea || !HostContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = PspWow64ReadCpuArea(CpuArea, &Area);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!Area.NativeContext)
    {
        return STATUS_NOT_FOUND;
    }

    if (Area.NativeContextLength < sizeof(CONTEXT))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Status = PspWow64CopyToUser((PVOID)(ULONG_PTR)Area.NativeContext,
                                 HostContext,
                                 sizeof(CONTEXT));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = PspWow64SetCpuAreaFlags(CpuArea, WOW64_CPU_AREA_FLAG_NATIVE_CONTEXT);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    _SEH2_TRY
    {
        ProbeForWrite(&CpuArea->NativeContextLength, sizeof(ULONG), sizeof(ULONG));
        CpuArea->NativeContextLength = sizeof(CONTEXT);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
VOID
PspWow64GetOrSetContextKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    PPSP_WOW64_GET_SET_CTX_CONTEXT Context;
    PETHREAD Thread;
    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;
    NTSTATUS Status;
    BOOLEAN SetOperation;

    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);

    Context = CONTAINING_RECORD(Apc, PSP_WOW64_GET_SET_CTX_CONTEXT, Apc);
    Thread = (PETHREAD)*SystemArgument2;
    SetOperation = (*SystemArgument1 != NULL);

    Context->Status = STATUS_UNSUCCESSFUL;

    Wow64Process = PsGetProcessWow64Process(Thread->ThreadsProcess);
    if (!Wow64Process ||
        !(Wow64Process->Flags & WOW64_PROCESS_FLAG_HAS_CPU_AREA) ||
        !Wow64Process->CpuArea)
    {
        Context->Status = STATUS_NOT_SUPPORTED;
        KeSetEvent(&Context->Event, IO_NO_INCREMENT, FALSE);
        return;
    }

    CpuArea = (PWOW64_CPU_AREA)WOW64_CPU_AREA_DECODE_POINTER(Wow64Process->CpuArea);
    if (!CpuArea)
    {
        Context->Status = STATUS_INVALID_PARAMETER;
        KeSetEvent(&Context->Event, IO_NO_INCREMENT, FALSE);
        return;
    }

    if (SetOperation)
    {
        Status = Wow64CpuSetContext(CpuArea,
                                     &Context->Context,
                                     sizeof(WOW64_CONTEXT));
    }
    else
    {
        RtlZeroMemory(&Context->Context, sizeof(WOW64_CONTEXT));
        Status = Wow64CpuGetContext(CpuArea,
                                     &Context->Context,
                                     sizeof(WOW64_CONTEXT));
        if (NT_SUCCESS(Status))
        {
            Context->Context.ContextFlags |= WOW64_CONTEXT_FULL;
        }
    }

    Context->Status = Status;
    KeSetEvent(&Context->Event, IO_NO_INCREMENT, FALSE);
}

static
VOID
PspWow64AssignPointer(
    _Inout_ PWOW64_PROCESS Wow64,
    _Inout_ PVOID *Field,
    _In_ ULONG Flag,
    _In_opt_ PVOID Value)
{
    *Field = Value;

    if (Value)
    {
        Wow64->Flags |= Flag;
    }
    else
    {
        Wow64->Flags &= ~Flag;
    }
}

static
VOID
PspWow64CloneState(
    _Inout_ PWOW64_PROCESS Target,
    _In_ PWOW64_PROCESS Source)
{
    if (!Target)
    {
        return;
    }

    if (!Source)
    {
        PspWow64AssignPointer(Target, &Target->Wow64, WOW64_PROCESS_FLAG_HAS_WOW64INFO, NULL);
        PspWow64AssignPointer(Target, &Target->Peb32, WOW64_PROCESS_FLAG_HAS_PEB32, NULL);
        PspWow64AssignPointer(Target, &Target->Teb32, WOW64_PROCESS_FLAG_HAS_TEB32, NULL);
        PspWow64AssignPointer(Target, &Target->CpuArea, WOW64_PROCESS_FLAG_HAS_CPU_AREA, NULL);
        return;
    }

    PspWow64AssignPointer(Target,
                          &Target->Wow64,
                          WOW64_PROCESS_FLAG_HAS_WOW64INFO,
                          (Source->Flags & WOW64_PROCESS_FLAG_HAS_WOW64INFO) ? Source->Wow64 : NULL);

    PspWow64AssignPointer(Target,
                          &Target->Peb32,
                          WOW64_PROCESS_FLAG_HAS_PEB32,
                          (Source->Flags & WOW64_PROCESS_FLAG_HAS_PEB32) ? Source->Peb32 : NULL);

    PspWow64AssignPointer(Target,
                          &Target->Teb32,
                          WOW64_PROCESS_FLAG_HAS_TEB32,
                          (Source->Flags & WOW64_PROCESS_FLAG_HAS_TEB32) ? Source->Teb32 : NULL);

    PspWow64AssignPointer(Target,
                          &Target->CpuArea,
                          WOW64_PROCESS_FLAG_HAS_CPU_AREA,
                          (Source->Flags & WOW64_PROCESS_FLAG_HAS_CPU_AREA) ? Source->CpuArea : NULL);
}

PWOW64_PROCESS
NTAPI
PsGetProcessWow64Process(
    IN PEPROCESS Process)
{
    if (!Process)
    {
        return NULL;
    }

    return (PWOW64_PROCESS)Process->Wow64Process;
}

PWOW64_PROCESS
NTAPI
PsGetCurrentProcessWow64Process(VOID)
{
    return PsGetProcessWow64Process(PsGetCurrentProcess());
}

NTSTATUS
NTAPI
PspWow64InitializeProcess(
    IN PEPROCESS Process,
    IN PEPROCESS Parent OPTIONAL,
    IN BOOLEAN ForceCreation)
{
    PWOW64_PROCESS Wow64;
    PWOW64_PROCESS ParentWow64 = NULL;

    if (!Process)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Wow64 = (PWOW64_PROCESS)Process->Wow64Process;
    if (Wow64)
    {
        /* Already initialized; refresh inheritance if needed. */
        if (Parent)
        {
            ParentWow64 = PsGetProcessWow64Process(Parent);
            if (ParentWow64)
            {
                Wow64->Flags |= WOW64_PROCESS_FLAG_INHERITED;
                PspWow64CloneState(Wow64, ParentWow64);
            }
        }

        return STATUS_SUCCESS;
    }

    if (Parent)
    {
        ParentWow64 = PsGetProcessWow64Process(Parent);
        if (!ParentWow64 && !ForceCreation)
        {
            /* Parent has no WOW64 state; keep the process native for now. */
            return STATUS_SUCCESS;
        }
    }
    else if (!ForceCreation)
    {
        /* No parent context and no explicit request to create WOW64 state. */
        return STATUS_SUCCESS;
    }

    Wow64 = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Wow64), TAG_WOW64_PROCESS);
    if (!Wow64)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Wow64, sizeof(*Wow64));
    Wow64->Flags = WOW64_PROCESS_FLAG_INITIALIZED;

    if (ParentWow64)
    {
        Wow64->Flags |= WOW64_PROCESS_FLAG_INHERITED;
        PspWow64CloneState(Wow64, ParentWow64);
    }
    else
    {
        PspWow64CloneState(Wow64, NULL);
    }

    Process->Wow64Process = Wow64;
    return STATUS_SUCCESS;
}

VOID
NTAPI
PspWow64DeleteProcess(
    IN PEPROCESS Process)
{
    PWOW64_PROCESS Wow64;

    if (!Process)
    {
        return;
    }

    Wow64 = PsGetProcessWow64Process(Process);
    if (!Wow64)
    {
        return;
    }

    PspWow64CloneState(Wow64, NULL);
    Process->Wow64Process = NULL;
    ExFreePoolWithTag(Wow64, TAG_WOW64_PROCESS);
}

VOID
NTAPI
PspWow64SetProcessWow64Info(
    IN PEPROCESS Process,
    IN PVOID Wow64Info OPTIONAL)
{
    PWOW64_PROCESS Wow64;

    if (!Process)
    {
        return;
    }

    if (!Process->Wow64Process)
    {
        if (!NT_SUCCESS(PspWow64InitializeProcess(Process, NULL, TRUE)))
        {
            return;
        }
    }

    Wow64 = PsGetProcessWow64Process(Process);
    if (!Wow64)
    {
        return;
    }

    PspWow64AssignPointer(Wow64,
                          &Wow64->Wow64,
                          WOW64_PROCESS_FLAG_HAS_WOW64INFO,
                          Wow64Info);
}

VOID
NTAPI
PspWow64SetProcessPeb32(
    IN PEPROCESS Process,
    IN PVOID Peb32 OPTIONAL)
{
    PWOW64_PROCESS Wow64;

    if (!Process)
    {
        return;
    }

    if (!Process->Wow64Process)
    {
        if (!NT_SUCCESS(PspWow64InitializeProcess(Process, NULL, TRUE)))
        {
            return;
        }
    }

    Wow64 = PsGetProcessWow64Process(Process);
    if (!Wow64)
    {
        return;
    }

    PspWow64AssignPointer(Wow64,
                          &Wow64->Peb32,
                          WOW64_PROCESS_FLAG_HAS_PEB32,
                          Peb32);
}

VOID
NTAPI
PspWow64SetProcessTeb32(
    IN PEPROCESS Process,
    IN PVOID Teb32 OPTIONAL)
{
    PWOW64_PROCESS Wow64;

    if (!Process)
    {
        return;
    }

    if (!Process->Wow64Process)
    {
        if (!NT_SUCCESS(PspWow64InitializeProcess(Process, NULL, TRUE)))
        {
            return;
        }
    }

    Wow64 = PsGetProcessWow64Process(Process);
    if (!Wow64)
    {
        return;
    }

    PspWow64AssignPointer(Wow64,
                          &Wow64->Teb32,
                          WOW64_PROCESS_FLAG_HAS_TEB32,
                          Teb32);
}

NTSTATUS
NTAPI
PspWow64SetProcessCpuArea(
    IN PEPROCESS Process,
    IN PVOID CpuArea OPTIONAL)
{
    PWOW64_PROCESS Wow64;
    PVOID EncodedArea;
    NTSTATUS Status;
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;
    PSP_WOW64_CPU_AREA_SNAPSHOT Snapshot;

    if (!Process)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!Process->Wow64Process)
    {
        NTSTATUS Status = PspWow64InitializeProcess(Process, NULL, TRUE);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    Wow64 = PsGetProcessWow64Process(Process);
    if (!Wow64)
    {
        return STATUS_UNSUCCESSFUL;
    }

    if (CpuArea)
    {
        if (PsGetCurrentProcess() != Process)
        {
            KeStackAttachProcess(&Process->Pcb, &ApcState);
            Attached = TRUE;
        }

        Status = PspWow64ReadCpuArea((PWOW64_CPU_AREA)CpuArea, &Snapshot);

        if (Attached)
        {
            KeUnstackDetachProcess(&ApcState);
        }

        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        EncodedArea = (PVOID)WOW64_CPU_AREA_ENCODE_POINTER(CpuArea);
    }
    else
    {
        EncodedArea = NULL;
    }

    PspWow64AssignPointer(Wow64,
                          &Wow64->CpuArea,
                          WOW64_PROCESS_FLAG_HAS_CPU_AREA,
                          EncodedArea);
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
PsIsWow64Process(
    IN PEPROCESS Process OPTIONAL)
{
    PWOW64_PROCESS Wow64;

    if (!Process)
    {
        Process = PsGetCurrentProcess();
    }

    Wow64 = PsGetProcessWow64Process(Process);
    if (!Wow64)
    {
        return FALSE;
    }

    /* A process is WOW64 if it has both WOW64 state and a CPU area */
    return (Wow64->Flags & WOW64_PROCESS_FLAG_HAS_WOW64INFO) &&
           (Wow64->Flags & WOW64_PROCESS_FLAG_HAS_CPU_AREA);
}

NTSTATUS
NTAPI
Wow64cpuExecuteCompatApc(
    IN PVOID NormalContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    /*
     * Wow64cpuExecuteCompatApc - WOW64 Compatibility APC Execution Stub
     *
     * This kernel-mode APC routine is the bridge that transitions a 64-bit thread
     * into 32-bit compatibility mode to execute a user-mode APC callback.
     *
     * BACKGROUND:
     * -----------
     * On Windows x64, when a 32-bit (WOW64) thread is running under a 64-bit kernel,
     * user-mode APCs must be delivered in 32-bit mode. The kernel cannot directly
     * execute 32-bit code - it requires wow64cpu.dll to manage the transition.
     *
     * EXECUTION FLOW:
     * --------------
     * 1. Kernel queues a user APC targeting a WOW64 thread
     * 2. PsWrapApcWow64Thread intercepts and wraps the APC context
     * 3. KiInitializeUserApc queues this routine as the KernelRoutine
     * 4. Thread returns from kernel mode at APC_LEVEL
     * 5. This routine executes at PASSIVE_LEVEL in kernel context
     * 6. We call into wow64cpu.dll to:
     *    a) Save the current 64-bit context
     *    b) Build a 32-bit context from NormalContext
     *    c) Transition to compatibility mode
     *    d) Execute the 32-bit APC routine
     *    e) Return via CpupReturnFromSimulatedCode
     *
     * PARAMETERS:
     * ----------
     * NormalContext    - WOW64_APC_CONTEXT structure prepared by PsWrapApcWow64Thread
     * SystemArgument1  - First system argument (typically from NtQueueApcThread)
     * SystemArgument2  - Second system argument
     *
     * NOTES:
     * -----
     * - This routine runs at PASSIVE_LEVEL but after transitioning from APC_LEVEL
     * - The actual mode switch happens in wow64cpu.dll (not in kernel)
     * - Trap frame preservation is critical - we must restore exactly on return
     * - If wow64cpu fails, we cannot execute the APC and must return gracefully
     * - This is a KERNEL-mode routine that hands off to user-mode wow64cpu.dll
     */

    PWOW64_APC_CONTEXT ApcContext;
    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;

    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    /* Validate we're in a WOW64 process */
    Wow64Process = PsGetCurrentProcessWow64Process();
    if (!Wow64Process ||
        !(Wow64Process->Flags & WOW64_PROCESS_FLAG_HAS_CPU_AREA) ||
        !Wow64Process->CpuArea)
    {
        DPRINT1("Wow64cpuExecuteCompatApc: Not a WOW64 process or no CPU area\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Decode the CPU area pointer (it may be tagged) */
    CpuArea = (PWOW64_CPU_AREA)WOW64_CPU_AREA_DECODE_POINTER(Wow64Process->CpuArea);
    if (!CpuArea)
    {
        DPRINT1("Wow64cpuExecuteCompatApc: Invalid CPU area pointer\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* NormalContext should point to the WOW64_APC_CONTEXT */
    ApcContext = (PWOW64_APC_CONTEXT)NormalContext;
    if (!ApcContext)
    {
        DPRINT1("Wow64cpuExecuteCompatApc: No APC context provided\n");
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Record the pending APC in the CPU area so that wow64.dll can
     * pick it up via Wow64CpuTakePendingApc and drive the 32-bit
     * APC dispatch on return to user mode.
     */
    return Wow64CpuSetPendingApc(CpuArea,
                                 (const WOW64_APC_CONTEXT *)ApcContext,
                                 (ULONG_PTR)SystemArgument1,
                                 (ULONG_PTR)SystemArgument2);
}

BOOLEAN
NTAPI
PsIsWow64Thread(
    IN PETHREAD Thread OPTIONAL)
{
    if (!Thread)
    {
        Thread = PsGetCurrentThread();
    }

    return PsIsWow64Process(Thread->ThreadsProcess);
}

NTSTATUS
NTAPI
PspWow64GetContext(
    IN PETHREAD Thread,
    _Out_writes_bytes_(sizeof(WOW64_CONTEXT)) PWOW64_CONTEXT Context)
{
    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;
    NTSTATUS Status;
    PSP_WOW64_GET_SET_CTX_CONTEXT GetSetContext;

    /* Validate that this is a WOW64 thread */
    Wow64Process = PsGetProcessWow64Process(Thread->ThreadsProcess);
    if (!Wow64Process ||
        !(Wow64Process->Flags & WOW64_PROCESS_FLAG_HAS_CPU_AREA) ||
        !Wow64Process->CpuArea)
    {
        return STATUS_NOT_SUPPORTED;
    }

    /* Decode the CPU area pointer */
    CpuArea = (PWOW64_CPU_AREA)WOW64_CPU_AREA_DECODE_POINTER(Wow64Process->CpuArea);
    if (!CpuArea)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Thread == PsGetCurrentThread())
    {
        Status = Wow64CpuGetContext(CpuArea, Context, sizeof(WOW64_CONTEXT));
        if (NT_SUCCESS(Status))
        {
            Context->ContextFlags |= WOW64_CONTEXT_FULL;
        }

        return Status;
    }

    RtlZeroMemory(&GetSetContext, sizeof(GetSetContext));
    KeInitializeEvent(&GetSetContext.Event, NotificationEvent, FALSE);
    GetSetContext.Status = STATUS_UNSUCCESSFUL;

    KeInitializeApc(&GetSetContext.Apc,
                    &Thread->Tcb,
                    OriginalApcEnvironment,
                    PspWow64GetOrSetContextKernelRoutine,
                    NULL,
                    NULL,
                    KernelMode,
                    NULL);

    if (!KeInsertQueueApc(&GetSetContext.Apc,
                          NULL,
                          Thread,
                          2))
    {
        return STATUS_UNSUCCESSFUL;
    }

    Status = KeWaitForSingleObject(&GetSetContext.Event,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = GetSetContext.Status;
    if (NT_SUCCESS(Status))
    {
        *Context = GetSetContext.Context;
    }

    return Status;
}

NTSTATUS
NTAPI
PspWow64SetContext(
    IN PETHREAD Thread,
    _In_reads_bytes_(sizeof(WOW64_CONTEXT)) const WOW64_CONTEXT *Context)
{
    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;
    NTSTATUS Status;
    PSP_WOW64_GET_SET_CTX_CONTEXT GetSetContext;

    /* Validate that this is a WOW64 thread */
    Wow64Process = PsGetProcessWow64Process(Thread->ThreadsProcess);
    if (!Wow64Process ||
        !(Wow64Process->Flags & WOW64_PROCESS_FLAG_HAS_CPU_AREA) ||
        !Wow64Process->CpuArea)
    {
        return STATUS_NOT_SUPPORTED;
    }

    /* Decode the CPU area pointer */
    CpuArea = (PWOW64_CPU_AREA)WOW64_CPU_AREA_DECODE_POINTER(Wow64Process->CpuArea);
    if (!CpuArea)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Context->ContextFlags & WOW64_CONTEXT_FULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Thread == PsGetCurrentThread())
    {
        return Wow64CpuSetContext(CpuArea, Context, sizeof(WOW64_CONTEXT));
    }

    RtlZeroMemory(&GetSetContext, sizeof(GetSetContext));
    KeInitializeEvent(&GetSetContext.Event, NotificationEvent, FALSE);
    GetSetContext.Status = STATUS_UNSUCCESSFUL;
    GetSetContext.Context = *Context;

    KeInitializeApc(&GetSetContext.Apc,
                    &Thread->Tcb,
                    OriginalApcEnvironment,
                    PspWow64GetOrSetContextKernelRoutine,
                    NULL,
                    NULL,
                    KernelMode,
                    NULL);

    if (!KeInsertQueueApc(&GetSetContext.Apc,
                          (PVOID)1,
                          Thread,
                          2))
    {
        return STATUS_UNSUCCESSFUL;
    }

    Status = KeWaitForSingleObject(&GetSetContext.Event,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return GetSetContext.Status;
}

BOOLEAN
NTAPI
KiIsWow64TrapFrame(
    IN PKTRAP_FRAME TrapFrame)
{
    /*
     * Detect if a trap frame originated from a WOW64 (32-bit compatibility mode) context.
     *
     * On AMD64, the code segment selector indicates the execution mode:
     * - KGDT64_R3_CODE (0x33)   = 64-bit user mode (long mode)
     * - KGDT64_R3_CMCODE (0x23) = 32-bit compatibility mode
     *
     * This function is critical for exception dispatching - if we detect WOW64,
     * we must redirect faults to wow64.dll instead of the native exception dispatcher.
     */

#ifndef _M_AMD64
    UNREFERENCED_PARAMETER(TrapFrame);
    return FALSE;
#else
    if (!TrapFrame)
    {
        return FALSE;
    }

    /* Check if CS indicates compatibility mode */
    return (TrapFrame->SegCs == (KGDT64_R3_CMCODE | RPL_MASK));
#endif
}

NTSTATUS
NTAPI
KiDispatchWow64Exception(
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PKTRAP_FRAME TrapFrame,
    IN PCONTEXT Context)
{
    /*
     * KiDispatchWow64Exception - WOW64 Exception Dispatcher
     *
     * When a 32-bit thread faults, we cannot use the native 64-bit exception
     * dispatcher. Instead, we must hand off to wow64.dll which will:
     * 1. Convert the exception record to 32-bit format
     * 2. Convert the context to 32-bit WOW64_CONTEXT
     * 3. Dispatch to the 32-bit ntdll exception handler
     * 4. If unhandled, return control to the kernel
     *
     * This is a STUB implementation that prepares for full wow64.dll integration.
     * Currently, we call wow64cpu to prepare the exception context.
     */

    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;
    NTSTATUS Status;
    UNREFERENCED_PARAMETER(ExceptionRecord);

    /* Verify we're in a WOW64 process */
    Wow64Process = PsGetCurrentProcessWow64Process();
    if (!Wow64Process ||
        !(Wow64Process->Flags & WOW64_PROCESS_FLAG_HAS_CPU_AREA) ||
        !Wow64Process->CpuArea)
    {
        return STATUS_NOT_SUPPORTED;
    }

    /* Decode the CPU area */
    CpuArea = (PWOW64_CPU_AREA)WOW64_CPU_AREA_DECODE_POINTER(Wow64Process->CpuArea);
    if (!CpuArea)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Stash the host context so wow64cpu sees the native view */
    Status = Wow64PrepareForException(CpuArea, Context);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("KiDispatchWow64Exception: Wow64PrepareForException failed: 0x%08lx\n", Status);
        return Status;
    }

        return STATUS_SUCCESS;
}

VOID
NTAPI
PspWow64InitializeThread(
    IN PETHREAD Thread)
{
    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;

    /* Check if this is a WOW64 process */
    Wow64Process = PsGetProcessWow64Process(Thread->ThreadsProcess);
    if (!Wow64Process ||
        !(Wow64Process->Flags & WOW64_PROCESS_FLAG_HAS_CPU_AREA))
    {
        return;
    }

    /* Decode and initialize the CPU area for this thread */
    CpuArea = (PWOW64_CPU_AREA)WOW64_CPU_AREA_DECODE_POINTER(Wow64Process->CpuArea);
    if (CpuArea)
    {
        /* Call wow64cpu to initialize thread-specific state */
        if (PsGetCurrentProcess() != Thread->ThreadsProcess)
        {
            KeStackAttachProcess(&Thread->ThreadsProcess->Pcb, &ApcState);
            Attached = TRUE;
        }

        CpuThreadInit(CpuArea, NULL);

        if (Attached)
        {
            KeUnstackDetachProcess(&ApcState);
        }
    }
}

VOID
NTAPI
PspWow64DeleteThread(
    IN PETHREAD Thread)
{
    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;

    /* Check if this is a WOW64 process */
    Wow64Process = PsGetProcessWow64Process(Thread->ThreadsProcess);
    if (!Wow64Process ||
        !(Wow64Process->Flags & WOW64_PROCESS_FLAG_HAS_CPU_AREA))
    {
        return;
    }

    /* Decode and clean up the CPU area for this thread */
    CpuArea = (PWOW64_CPU_AREA)WOW64_CPU_AREA_DECODE_POINTER(Wow64Process->CpuArea);
    if (CpuArea)
    {
        /* Call wow64cpu to clean up thread-specific state */
        if (PsGetCurrentProcess() != Thread->ThreadsProcess)
        {
            KeStackAttachProcess(&Thread->ThreadsProcess->Pcb, &ApcState);
            Attached = TRUE;
        }

        CpuThreadTerm(CpuArea);

        if (Attached)
        {
            KeUnstackDetachProcess(&ApcState);
        }
    }
}

#endif /* _M_AMD64 */
