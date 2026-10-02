/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC trap dispatch
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#ifdef KDBG
#include <kdbg/kdb.h>
#endif

static
const CHAR *
KiPpcVectorName(
    _In_ ULONG Vector)
{
    switch (Vector)
    {
        case PPC_VECTOR_RESET: return "system reset";
        case PPC_VECTOR_MACHINE_CHECK: return "machine check";
        case PPC_VECTOR_DSI: return "data storage";
        case PPC_VECTOR_ISI: return "instruction storage";
        case PPC_VECTOR_EXTERNAL: return "external interrupt";
        case PPC_VECTOR_ALIGNMENT: return "alignment";
        case PPC_VECTOR_PROGRAM: return "program";
        case PPC_VECTOR_FP_UNAVAILABLE: return "floating-point unavailable";
        case PPC_VECTOR_DECREMENTER: return "decrementer";
        case PPC_VECTOR_SYSTEM_CALL: return "system call";
        case PPC_VECTOR_TRACE: return "trace";
        case PPC_VECTOR_PERFORMANCE: return "performance monitor";
        case PPC_VECTOR_BREAKPOINT: return "instruction address breakpoint";
        default: return "reserved";
    }
}

static
VOID
KiPpcDumpTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PULONG Gpr = &TrapFrame->Context.Gpr0;
    ULONG Index;

    DbgPrint("\n*** Unexpected trap: %s (vector %04lx)\n", KiPpcVectorName(TrapFrame->Vector), TrapFrame->Vector);
    DbgPrint("iar %08lx msr %08lx lr %08lx ctr %08lx\n", TrapFrame->Context.Iar, TrapFrame->Context.Msr, TrapFrame->Context.Lr, TrapFrame->Context.Ctr);
    DbgPrint("dar %08lx dsisr %08lx cr %08lx xer %08lx\n", TrapFrame->Dar, TrapFrame->Dsisr, TrapFrame->Context.Cr, TrapFrame->Context.Xer);
    for (Index = 0; Index < 32; Index++)
        DbgPrint("r%-2lu %08lx%s", Index, Gpr[Index], ((Index & 3) == 3) ? "\n" : "  ");
    DbgPrint("previous irql %u, trap frame %p\n", TrapFrame->PreviousIrql, TrapFrame);
}

DECLSPEC_NORETURN
static
VOID
KiPpcTrapStop(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiPpcDumpTrapFrame(TrapFrame);
    KeBugCheckEx(UNEXPECTED_KERNEL_MODE_TRAP, TrapFrame->Vector, TrapFrame->Context.Iar, TrapFrame->Dar, (ULONG_PTR)TrapFrame);
}

ULONG_PTR
NTAPI
KeGetTrapFramePc(_In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Context.Iar;
}

BOOLEAN
NTAPI
KeGetTrapFrameInterruptState(_In_ PKTRAP_FRAME TrapFrame)
{
    return (TrapFrame->Context.Msr & MSR_EE) != 0;
}

BOOLEAN
NTAPI
KiUserTrap(_In_ PKTRAP_FRAME TrapFrame)
{
    return (TrapFrame->Context.Msr & MSR_PR) != 0;
}

PKTRAP_FRAME
NTAPI
KeGetTrapFrame(_In_ PKTHREAD Thread)
{
    return Thread->TrapFrame;
}

PKEXCEPTION_FRAME
NTAPI
KeGetExceptionFrame(_In_ PKTHREAD Thread)
{
    /* Every register lives in KTRAP_FRAME.Context. */
    UNREFERENCED_PARAMETER(Thread);
    return NULL;
}

/* MM and exception dispatch run with the interrupt state of the code that
 * trapped; the caller masks interrupts again before it unlinks the frame. */
static
VOID
KiPpcPageFault(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ KPROCESSOR_MODE Mode)
{
    BOOLEAN Instruction = (TrapFrame->Vector == PPC_VECTOR_ISI);
    ULONG Status0 = Instruction ? TrapFrame->Context.Msr : TrapFrame->Dsisr;
    PVOID Address = Instruction ? (PVOID)TrapFrame->Context.Iar : (PVOID)TrapFrame->Dar;
    KIRQL Irql = KeGetCurrentIrql();
    EXCEPTION_RECORD Record;
    ULONG FaultCode = 0;
    NTSTATUS Status;

    if (Instruction)
        FaultCode |= MI_PPC_FAULT_EXECUTE;
    else if (Status0 & PPC_DSISR_STORE)
        FaultCode |= MI_PPC_FAULT_WRITE;
    if (!(Status0 & PPC_DSISR_NOT_FOUND))
        FaultCode |= MI_PPC_FAULT_PRESENT;

    if (KeGetTrapFrameInterruptState(TrapFrame))
        _enable();

    Status = MmAccessFault(FaultCode, Address, Mode, TrapFrame);
    if (NT_SUCCESS(Status))
    {
        KeInvalidateTlbEntry(Address);
        return;
    }
    if (Irql > APC_LEVEL)
        KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL, (ULONG_PTR)Address, Irql, (FaultCode & MI_PPC_FAULT_WRITE) ? 1 : 0, TrapFrame->Context.Iar);

    RtlZeroMemory(&Record, sizeof(Record));
    Record.ExceptionAddress = (PVOID)TrapFrame->Context.Iar;
    if ((Status == STATUS_ACCESS_VIOLATION) || (Status == STATUS_GUARD_PAGE_VIOLATION) || (Status == STATUS_STACK_OVERFLOW))
    {
        Record.ExceptionCode = Status;
        Record.NumberParameters = 2;
        Record.ExceptionInformation[0] = (FaultCode & MI_PPC_FAULT_WRITE) ? 1 : Instruction ? 8 : 0;
        Record.ExceptionInformation[1] = (ULONG_PTR)Address;
    }
    else
    {
        Record.ExceptionCode = STATUS_IN_PAGE_ERROR;
        Record.NumberParameters = 3;
        Record.ExceptionInformation[0] = (FaultCode & MI_PPC_FAULT_WRITE) ? 1 : 0;
        Record.ExceptionInformation[1] = (ULONG_PTR)Address;
        Record.ExceptionInformation[2] = Status;
    }
    KiDispatchException(&Record, NULL, TrapFrame, Mode, TRUE);
}

/* Decode a program interrupt into an NT exception record. Returns FALSE when
 * the trap was a debug service that is already complete. */
static
BOOLEAN
KiPpcProgramException(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ KPROCESSOR_MODE Mode,
    _Out_ PEXCEPTION_RECORD Record)
{
    ULONG Srr1 = TrapFrame->Context.Msr;
    ULONG Instruction = 0;

    RtlZeroMemory(Record, sizeof(*Record));
    Record->ExceptionAddress = (PVOID)TrapFrame->Context.Iar;

    if (Srr1 & PPC_SRR1_PROGRAM_FP)
    {
        ULONG Fpscr = (ULONG)(*(PULONGLONG)&TrapFrame->Context.Fpscr);

        if (Fpscr & 0x04000000) Record->ExceptionCode = STATUS_FLOAT_DIVIDE_BY_ZERO;
        else if (Fpscr & 0x10000000) Record->ExceptionCode = STATUS_FLOAT_OVERFLOW;
        else if (Fpscr & 0x08000000) Record->ExceptionCode = STATUS_FLOAT_UNDERFLOW;
        else if (Fpscr & 0x02000000) Record->ExceptionCode = STATUS_FLOAT_INEXACT_RESULT;
        else Record->ExceptionCode = STATUS_FLOAT_INVALID_OPERATION;
        return TRUE;
    }
    if (Srr1 & PPC_SRR1_PROGRAM_PRIVILEGED)
    {
        Record->ExceptionCode = STATUS_PRIVILEGED_INSTRUCTION;
        return TRUE;
    }
    if (Srr1 & PPC_SRR1_PROGRAM_ILLEGAL)
    {
        Record->ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
        return TRUE;
    }

    /* A trap instruction: twi 31,0,imm selects the service. */
    if (Mode == KernelMode)
        Instruction = *(PULONG)TrapFrame->Context.Iar;
    else if (!NT_SUCCESS(KiPpcCopyFromUser(&Instruction, (PVOID)TrapFrame->Context.Iar, sizeof(Instruction))))
        Instruction = 0;

    if ((Instruction & 0xFFFF0000UL) == 0x0FE00000UL)
    {
        switch (Instruction & 0xFFFF)
        {
            case PPC_FASTFAIL_TRAP:
                Record->ExceptionCode = STATUS_STACK_BUFFER_OVERRUN;
                Record->ExceptionFlags = EXCEPTION_NONCONTINUABLE;
                Record->NumberParameters = 1;
                Record->ExceptionInformation[0] = TrapFrame->Context.Gpr3;
                if (Mode == KernelMode)
                    KeBugCheckEx(KERNEL_SECURITY_CHECK_FAILURE, TrapFrame->Context.Gpr3, (ULONG_PTR)TrapFrame, (ULONG_PTR)Record, 0);
                return TRUE;

            case PPC_DEBUG_SERVICE_TRAP:
                /* DebugService: r3 = service, r4/r5/r6 = arguments. The
                 * debugger routine consumes it as a breakpoint whose first
                 * parameter is the service number. */
                Record->ExceptionCode = STATUS_BREAKPOINT;
                Record->NumberParameters = 3;
                Record->ExceptionInformation[0] = TrapFrame->Context.Gpr3;
                Record->ExceptionInformation[1] = TrapFrame->Context.Gpr4;
                Record->ExceptionInformation[2] = TrapFrame->Context.Gpr5;
                return TRUE;

            default:
                break;
        }
    }

    Record->ExceptionCode = STATUS_BREAKPOINT;
    Record->NumberParameters = 1;
    Record->ExceptionInformation[0] = BREAKPOINT_BREAK;
    return TRUE;
}

VOID
NTAPI
KiPpcTrapDispatch(_Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KPROCESSOR_MODE Mode = KiUserTrap(TrapFrame) ? UserMode : KernelMode;
    EXCEPTION_RECORD Record;

    TrapFrame->PreviousMode = Mode;
    TrapFrame->PreviousTrapFrame = Thread->TrapFrame;
    Thread->TrapFrame = TrapFrame;

    switch (TrapFrame->Vector)
    {
        case PPC_VECTOR_EXTERNAL:
            KiPpcExternalInterrupt(TrapFrame);
            break;

        case PPC_VECTOR_DECREMENTER:
            KiPpcClockInterrupt(TrapFrame);
            break;

        case PPC_VECTOR_SYSTEM_CALL:
            if (Mode != UserMode)
                KiPpcTrapStop(TrapFrame);
            KiPpcSystemServiceDispatch(TrapFrame);
            break;

        case PPC_VECTOR_DSI:
        case PPC_VECTOR_ISI:
            KiPpcPageFault(TrapFrame, Mode);
            break;

        case PPC_VECTOR_PROGRAM:
        case PPC_VECTOR_ALIGNMENT:
        case PPC_VECTOR_TRACE:
            if (TrapFrame->Vector == PPC_VECTOR_PROGRAM)
            {
                KiPpcProgramException(TrapFrame, Mode, &Record);
            }
            else
            {
                RtlZeroMemory(&Record, sizeof(Record));
                Record.ExceptionAddress = (PVOID)TrapFrame->Context.Iar;
                if (TrapFrame->Vector == PPC_VECTOR_ALIGNMENT)
                {
                    Record.ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
                    Record.NumberParameters = 2;
                    Record.ExceptionInformation[0] = (TrapFrame->Dsisr >> 8) & 1;
                    Record.ExceptionInformation[1] = TrapFrame->Dar;
                }
                else
                {
                    Record.ExceptionCode = STATUS_SINGLE_STEP;
                    TrapFrame->Context.Msr &= ~PPC_MSR_SE;
                }
            }
            if (KeGetTrapFrameInterruptState(TrapFrame))
                _enable();
            KiDispatchException(&Record, NULL, TrapFrame, Mode, TRUE);
            break;

        default:
            Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
            KiPpcTrapStop(TrapFrame);
    }

    _disable();

    /* Deliver pending user APCs on the way back to user mode. */
    if ((Mode == UserMode) && Thread->ApcState.UserApcPending)
    {
        KIRQL OldIrql;

        KfRaiseIrql(APC_LEVEL);
        _enable();
        KiDeliverApc(UserMode, NULL, TrapFrame);
        _disable();
        OldIrql = PASSIVE_LEVEL;
        KfLowerIrql(OldIrql);
    }

    Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
}

static
BOOLEAN
KiPpcDeliverUserException(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ PEXCEPTION_RECORD Record)
{
    KUSER_EXCEPTION_STACK UserFrame;
    ULONG_PTR Stack, Entry = (ULONG_PTR)KeUserExceptionDispatcher;
    NTSTATUS Status;

    if (!Entry || (Entry & 3) || (Entry >= MmUserProbeAddress) || (Context->Gpr1 < MM_ALLOCATION_GRANULARITY + sizeof(UserFrame)) ||
        (Context->Gpr1 >= MmUserProbeAddress) || (Context->Gpr1 & 7) || (Record->NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS))
    {
        return FALSE;
    }

    /* KiUserExceptionDispatcher finds the CONTEXT and the EXCEPTION_RECORD
     * above a 32-byte frame header at its entry stack pointer. */
    Stack = (Context->Gpr1 - sizeof(UserFrame)) & ~15UL;
    RtlZeroMemory(&UserFrame, sizeof(UserFrame));
    UserFrame.Context = *Context;
    UserFrame.ExceptionRecord = *Record;
    Status = KiPpcCopyToUser((PVOID)Stack, &UserFrame, sizeof(UserFrame));
    if (!NT_SUCCESS(Status))
        return FALSE;

    /* The export is a function descriptor: enter its code with ntdll's TOC. */
    Status = KiPpcSetUserEntry(TrapFrame, (PVOID)Entry);
    if (!NT_SUCCESS(Status))
        return FALSE;
    TrapFrame->Context.Gpr1 = Stack;
    TrapFrame->Context.Gpr3 = Stack + FIELD_OFFSET(KUSER_EXCEPTION_STACK, ExceptionRecord);
    TrapFrame->Context.Gpr4 = Stack + FIELD_OFFSET(KUSER_EXCEPTION_STACK, Context);
    TrapFrame->Context.Lr = 0;
    return TRUE;
}

VOID
KiDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN FirstChance)
{
    CONTEXT Context;

    KeGetCurrentPrcb()->KeExceptionDispatchCount++;
    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL;
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Context);

    if (PreviousMode != KernelMode)
    {
        if (FirstChance)
        {
            if ((!PsGetCurrentProcess()->DebugPort && !KdIgnoreUmExceptions) || KdIsThisAKdTrap(ExceptionRecord, &Context, PreviousMode))
            {
                if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord, &Context, PreviousMode, FALSE))
                    goto Handled;
            }
            if (DbgkForwardException(ExceptionRecord, TRUE, FALSE))
                return;
            if (KiPpcDeliverUserException(TrapFrame, &Context, ExceptionRecord))
                return;
        }
        if (DbgkForwardException(ExceptionRecord, TRUE, TRUE) || DbgkForwardException(ExceptionRecord, FALSE, TRUE))
            return;
        DPRINT1("PPC terminating %.16s: exception %lx at %p\n", PsGetCurrentProcess()->ImageFileName, ExceptionRecord->ExceptionCode, ExceptionRecord->ExceptionAddress);
        ZwTerminateProcess(NtCurrentProcess(), ExceptionRecord->ExceptionCode);
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, ExceptionRecord->ExceptionCode, (ULONG_PTR)ExceptionRecord->ExceptionAddress, (ULONG_PTR)TrapFrame, PreviousMode);
    }

    if (FirstChance)
    {
        if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord, &Context, PreviousMode, FALSE))
            goto Handled;
        if (RtlDispatchException(ExceptionRecord, &Context))
            goto Handled;
    }
    if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord, &Context, PreviousMode, TRUE))
        goto Handled;

    KiPpcDumpTrapFrame(TrapFrame);
    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, ExceptionRecord->ExceptionCode, (ULONG_PTR)ExceptionRecord->ExceptionAddress, (ULONG_PTR)TrapFrame, 0);

Handled:
    KeContextToTrapFrame(&Context, ExceptionFrame, TrapFrame, Context.ContextFlags, PreviousMode);
}
