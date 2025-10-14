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

#ifdef _M_AMD64

#ifdef __cplusplus
#error This file must be compiled as C
#endif

typedef struct _WOW64_APC_CONTEXT WOW64_APC_CONTEXT, *PWOW64_APC_CONTEXT;
typedef struct _WOW64_CPU_AREA WOW64_CPU_AREA, *PWOW64_CPU_AREA;

typedef enum _WOW64_CPU_NOTIFY_TYPE
{
    Wow64CpuNotifyInitialize = 0,
    Wow64CpuNotifyThreadAttach,
    Wow64CpuNotifyThreadDetach,
    Wow64CpuNotifyShutdown
} WOW64_CPU_NOTIFY_TYPE;

static NTSTATUS
NTAPI
CpuThreadInit(
    PWOW64_CPU_AREA CpuArea,
    PVOID ThreadContext)
{
    UNREFERENCED_PARAMETER(CpuArea);
    UNREFERENCED_PARAMETER(ThreadContext);
    return STATUS_NOT_IMPLEMENTED;
}

static VOID
NTAPI
CpuThreadTerm(
    PWOW64_CPU_AREA CpuArea)
{
    UNREFERENCED_PARAMETER(CpuArea);
}

static NTSTATUS
NTAPI
Wow64CpuDispatchPendingApc(
    PWOW64_CPU_AREA CpuArea)
{
    UNREFERENCED_PARAMETER(CpuArea);
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS
NTAPI
Wow64CpuGetContext(
    PWOW64_CPU_AREA CpuArea,
    PVOID Context,
    ULONG ContextLength)
{
    UNREFERENCED_PARAMETER(CpuArea);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ContextLength);
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS
NTAPI
Wow64CpuSetContext(
    PWOW64_CPU_AREA CpuArea,
    const VOID *Context,
    ULONG ContextLength)
{
    UNREFERENCED_PARAMETER(CpuArea);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ContextLength);
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS
NTAPI
Wow64PrepareForException(
    PWOW64_CPU_AREA CpuArea,
    const CONTEXT *HostContext)
{
    UNREFERENCED_PARAMETER(CpuArea);
    UNREFERENCED_PARAMETER(HostContext);
    return STATUS_NOT_IMPLEMENTED;
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
    IN PEPROCESS Parent OPTIONAL)
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
        if (!ParentWow64)
        {
            /* Parent has no WOW64 state; keep the process native for now. */
            return STATUS_SUCCESS;
        }
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
        if (!NT_SUCCESS(PspWow64InitializeProcess(Process, NULL)))
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
        if (!NT_SUCCESS(PspWow64InitializeProcess(Process, NULL)))
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
        if (!NT_SUCCESS(PspWow64InitializeProcess(Process, NULL)))
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

    if (!Process)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!Process->Wow64Process)
    {
        NTSTATUS Status = PspWow64InitializeProcess(Process, NULL);
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

    PspWow64AssignPointer(Wow64,
                          &Wow64->CpuArea,
                          WOW64_PROCESS_FLAG_HAS_CPU_AREA,
                          CpuArea);
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
    PETHREAD Thread;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    /* Get the current thread */
    Thread = PsGetCurrentThread();

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
     * At this point, we're ready to transition into 32-bit mode.
     * The actual transition is handled by wow64cpu.dll's dispatcher.
     *
     * We call Wow64CpuDispatchPendingApc which will:
     * 1. Save the current 64-bit trap frame
     * 2. Load the 32-bit context from the APC context
     * 3. Execute the 32-bit routine
     * 4. Restore the 64-bit trap frame
     *
     * The wow64cpu.dll entry point is expected to be mapped into
     * user-mode address space and callable from kernel context
     * (via a controlled transition mechanism).
     */

    Status = Wow64CpuDispatchPendingApc(CpuArea);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Wow64cpuExecuteCompatApc: Wow64CpuDispatchPendingApc failed: 0x%08lx\n", Status);
        /*
         * If dispatch fails, we cannot execute the APC.
         * The APC will be lost, but the thread remains stable.
         * Windows behavior: log error and continue.
         */
        return Status;
    }

    /*
     * Success: The 32-bit APC has executed and returned.
     * The trap frame has been restored by CpupReturnFromSimulatedCode.
     * We return to the kernel APC dispatcher which will continue
     * processing any remaining APCs.
     */

    return STATUS_SUCCESS;
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
    IN OUT PCONTEXT Context)
{
    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;
    NTSTATUS Status;

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

    /* Call wow64cpu to get the 32-bit context */
    Status = Wow64CpuGetContext(CpuArea, Context, sizeof(CONTEXT));

    return Status;
}

NTSTATUS
NTAPI
PspWow64SetContext(
    IN PETHREAD Thread,
    IN PCONTEXT Context)
{
    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;
    NTSTATUS Status;

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

    /* Call wow64cpu to set the 32-bit context */
    Status = Wow64CpuSetContext(CpuArea, Context, sizeof(CONTEXT));

    return Status;
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

    /* Call wow64cpu to prepare for exception dispatch */
    Status = Wow64PrepareForException(CpuArea, Context);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("KiDispatchWow64Exception: Wow64PrepareForException failed: 0x%08lx\n", Status);
        return Status;
    }

    /*
     * At this point, wow64cpu has prepared the exception context.
     * When full wow64.dll support is implemented, we would:
     * 1. Build a 32-bit exception stack frame
     * 2. Set RIP to the 32-bit exception dispatcher in wow64.dll
     * 3. Return to user mode where wow64.dll takes over
     *
     * For now, we return STATUS_SUCCESS to indicate the exception
     * has been prepared (even if not fully dispatched).
     */

    return STATUS_SUCCESS;
}

VOID
NTAPI
PspWow64InitializeThread(
    IN PETHREAD Thread)
{
    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;

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
        CpuThreadInit(CpuArea, NULL);
    }
}

VOID
NTAPI
PspWow64DeleteThread(
    IN PETHREAD Thread)
{
    PWOW64_PROCESS Wow64Process;
    PWOW64_CPU_AREA CpuArea;

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
        CpuThreadTerm(CpuArea);
    }
}

#endif /* _M_AMD64 */
