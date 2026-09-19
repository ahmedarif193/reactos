/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Kernel hooks for RISC-V exception dispatch and unwinding
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
RtlpRiscv64ReadMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ SIZE_T Length)
{
    return KiRiscvReadMemory(Destination, Source, Length);
}

DECLSPEC_NORETURN
VOID
NTAPI
RtlpRiscv64RaiseFatal(
    _In_ NTSTATUS Status)
{
    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, Status, 0, 0, 0);
}

/* Unwind through the trap handler into the interrupted supervisor frame. */
NTSTATUS
NTAPI
RtlpRiscv64UnwindUserException(
    _In_ ULONG_PTR Pc,
    _In_ ULONG_PTR Low,
    _In_ ULONG_PTR High,
    _Inout_ PCONTEXT Context)
{
    extern UCHAR KiRiscvTrapHandlerCall[], KiRiscvTrapHandlerReturn[];
    KTRAP_FRAME Frame;
    ULONG_PTR Address = Context->Sp;

    if (Pc < (ULONG_PTR)KiRiscvTrapHandlerCall ||
        Pc >= (ULONG_PTR)KiRiscvTrapHandlerReturn)
        return STATUS_NOT_FOUND;
    if ((Address & 15) || Address < Low || Address > High ||
        High - Address < sizeof(Frame) ||
        !NT_SUCCESS(KiRiscvReadMemory(&Frame, (PVOID)Address, sizeof(Frame))) ||
        KiUserTrap(&Frame) || Frame.Context.Sp < Address + sizeof(Frame) ||
        Frame.Context.Sp > High || (Frame.Context.Sp & 15) ||
        Frame.Context.Pc < (ULONG_PTR)MmSystemRangeStart || (Frame.Context.Pc & 1))
        return STATUS_BAD_STACK;

    *Context = Frame.Context;
    /* This is an interrupted instruction, not a call return address. */
    Context->ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
    return STATUS_SUCCESS;
}

VOID
NTAPI
RtlpRiscv64PrepareContextRestore(
    _In_ PCONTEXT Context)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME Frame;
    BOOLEAN Interrupts = KeDisableInterrupts();

    /* A kernel SEH unwind can bypass trap dispatch and its sret. Retire
     * every abandoned supervisor frame and recover the interrupted SIE
     * state before continuing in the selected handler. */
    while ((Frame = Thread->TrapFrame) != NULL && !KiUserTrap(Frame) &&
           (ULONG_PTR)Frame >= Thread->StackLimit &&
           (ULONG_PTR)Frame < (ULONG_PTR)Thread->InitialStack &&
           Context->Sp >= Frame->Context.Sp)
    {
        if (Frame->Context.Sp < (ULONG_PTR)Frame + sizeof(*Frame) ||
            Frame->Context.Sp > (ULONG_PTR)Thread->InitialStack)
            RtlpRiscv64RaiseFatal(STATUS_BAD_STACK);
        Interrupts = !!(Frame->Sstatus & RISCV_SSTATUS_SPIE);
        Thread->TrapFrame = Frame->PreviousTrapFrame;
    }
    KeRestoreInterrupts(Interrupts);
}
