/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Kernel hooks for Windows NT PowerPC exception unwinding
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Code entries of the trap path (ke/ppc/trap.S). KiPpcTrapEntry calls
 * KiPpcTrapDispatch with the KTRAP_FRAME right above a 64-byte C frame. */
extern UCHAR KiPpcTrapEntryCode[] __asm__("..KiPpcTrapEntry");
extern UCHAR KiPpcTrapEntryEnd[];

#define KI_PPC_TRAP_C_FRAME 64

/* Unwind through the trap entry into the interrupted kernel frame. */
NTSTATUS
NTAPI
RtlpPpcUnwindSpecialFrame(
    _In_ ULONG_PTR ControlPc,
    _Inout_ PCONTEXT Context)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME Frame;

    /* The restore routine is in a different COFF section and may be linked
     * before the entry routine. Bound this range within the entry section. */
    if ((ControlPc < (ULONG_PTR)KiPpcTrapEntryCode) || (ControlPc >= (ULONG_PTR)KiPpcTrapEntryEnd))
        return STATUS_NOT_FOUND;

    Frame = (PKTRAP_FRAME)(Context->Gpr1 + KI_PPC_TRAP_C_FRAME);
    if (((ULONG_PTR)Frame & 15) || ((ULONG_PTR)Frame < Thread->StackLimit) || ((ULONG_PTR)(Frame + 1) > (ULONG_PTR)Thread->InitialStack) || KiUserTrap(Frame))
        return STATUS_BAD_STACK;

    *Context = Frame->Context;
    return STATUS_SUCCESS;
}

/* A kernel SEH unwind can bypass trap dispatch and its return path. Retire
 * every abandoned kernel trap frame below the target and recover the
 * interrupt state the outermost of them interrupted. */
VOID
NTAPI
RtlpPpcPrepareContextRestore(
    _In_ PCONTEXT Context)
{
    PKTHREAD Thread = KeGetCurrentThread();
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKTRAP_FRAME Frame;

    while (((Frame = Thread->TrapFrame) != NULL) && !KiUserTrap(Frame) && ((ULONG_PTR)Frame >= Thread->StackLimit) && ((ULONG_PTR)Frame < (ULONG_PTR)Thread->InitialStack) && (Context->Gpr1 >= Frame->Context.Gpr1))
    {
        Interrupts = (Frame->Context.Msr & MSR_EE) != 0;
        Thread->TrapFrame = Frame->PreviousTrapFrame;
    }
    KeRestoreInterrupts(Interrupts);
}
