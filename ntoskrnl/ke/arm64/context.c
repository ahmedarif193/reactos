/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/context.c
 * PURPOSE:         Context management stubs for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* NOTE: The ARM64 trap frame mirrors the hardware-saved volatile registers
 * (x0-x18, FP/LR, SP, PC) while the exception frame carries the non-volatile
 * bank (x19-x28).  The helpers below translate between the user visible
 * CONTEXT record and those internal frames.  They run at APC_LEVEL to make
 * sure the state cannot be pre-empted while partially updated. */

VOID
NTAPI
KeContextToTrapFrame(_In_ PCONTEXT Context,
                     _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
                     _Inout_ PKTRAP_FRAME TrapFrame,
                     _In_ ULONG ContextFlags,
                     _In_ KPROCESSOR_MODE PreviousMode)
{
    KIRQL OldIrql;
    KIRQL PreviousIrql;
    ULONG Index;

    ASSERT(Context != NULL);
    ASSERT(TrapFrame != NULL);
    ASSERT(ContextFlags & CONTEXT_ARM64);

    /* Drop the architecture tag so the bitfield map matches */
    ContextFlags &= ~CONTEXT_ARM64;

    PreviousIrql = TrapFrame->PreviousIrql;
    OldIrql = KeGetCurrentIrql();
    if (OldIrql < APC_LEVEL)
    {
        KeRaiseIrql(APC_LEVEL, &OldIrql);
    }

    /* Integer/volatile register bank (x0-x18 + FP/LR) */
    if (ContextFlags & (CONTEXT_INTEGER | CONTEXT_CONTROL | CONTEXT_X18))
    {
        for (Index = 0; Index <= 18; Index++)
        {
            TrapFrame->X[Index] = Context->X[Index];
        }

        TrapFrame->Fp = Context->Fp;
        TrapFrame->Lr = Context->Lr;
    }

    /* Non-volatile registers live in the exception frame */
    if ((ContextFlags & CONTEXT_INTEGER) && (ExceptionFrame != NULL))
    {
        ExceptionFrame->X19 = Context->X[19];
        ExceptionFrame->X20 = Context->X[20];
        ExceptionFrame->X21 = Context->X[21];
        ExceptionFrame->X22 = Context->X[22];
        ExceptionFrame->X23 = Context->X[23];
        ExceptionFrame->X24 = Context->X[24];
        ExceptionFrame->X25 = Context->X[25];
        ExceptionFrame->X26 = Context->X[26];
        ExceptionFrame->X27 = Context->X[27];
        ExceptionFrame->X28 = Context->X[28];
    }

    if ((ContextFlags & (CONTEXT_INTEGER | CONTEXT_CONTROL)) &&
        (ExceptionFrame != NULL))
    {
        ExceptionFrame->Fp = Context->Fp;
        ExceptionFrame->Lr = Context->Lr;
    }

    if (ContextFlags & CONTEXT_CONTROL)
    {
        TrapFrame->Pc = Context->Pc;
        TrapFrame->Sp = Context->Sp;
        TrapFrame->Spsr = Context->Cpsr;
        if (PreviousMode != KernelMode)
        {
            TrapFrame->Spsr |= ARM64_PSTATE_ASYNC_ABORT_MASK;
        }

        TrapFrame->PreviousMode = (CHAR)PreviousMode;
        TrapFrame->PreviousIrql = PreviousIrql;
    }

    if (ContextFlags & CONTEXT_DEBUG_REGISTERS)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(TrapFrame->Bcr); Index++)
        {
            TrapFrame->Bcr[Index] = Context->Bcr[Index];
            TrapFrame->Bvr[Index] = Context->Bvr[Index];
        }

        for (Index = 0; Index < RTL_NUMBER_OF(TrapFrame->Wcr); Index++)
        {
            TrapFrame->Wcr[Index] = Context->Wcr[Index];
            TrapFrame->Wvr[Index] = Context->Wvr[Index];
        }

        TrapFrame->DebugRegistersValid = TRUE;
    }

    /* Floating point/NEON state: mirror into the per-CPU processor state so
     * KD (and later context swaps) can observe modifications. We avoid
     * touching hardware registers here; the arch save/restore helpers are
     * responsible for syncing ProcessorState <-> hardware. */
    if (ContextFlags & CONTEXT_FLOATING_POINT)
    {
        PKPRCB Prcb = KeGetCurrentPrcb();
        if (Prcb != NULL)
        {
            RtlCopyMemory(Prcb->ProcessorState.ContextFrame.V,
                          Context->V,
                          sizeof(Context->V));
            Prcb->ProcessorState.ContextFrame.Fpcr = Context->Fpcr;
            Prcb->ProcessorState.ContextFrame.Fpsr = Context->Fpsr;
        }
    }

    if (OldIrql < APC_LEVEL)
    {
        KeLowerIrql(OldIrql);
    }
}

VOID
NTAPI
KeTrapFrameToContext(_In_ PKTRAP_FRAME TrapFrame,
                     _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
                     _Inout_ PCONTEXT Context)
{
    ULONG ContextFlags;
    KIRQL OldIrql;
    ULONG Index;

    ASSERT(TrapFrame != NULL);
    ASSERT(Context != NULL);

    ContextFlags = Context->ContextFlags;
    ASSERT(ContextFlags & CONTEXT_ARM64);
    ContextFlags &= ~CONTEXT_ARM64;

    OldIrql = KeGetCurrentIrql();
    if (OldIrql < APC_LEVEL)
    {
        KeRaiseIrql(APC_LEVEL, &OldIrql);
    }

    if (ContextFlags & (CONTEXT_INTEGER | CONTEXT_CONTROL | CONTEXT_X18))
    {
        for (Index = 0; Index <= 18; Index++)
        {
            Context->X[Index] = TrapFrame->X[Index];
        }

        Context->Fp = TrapFrame->Fp;
        Context->Lr = TrapFrame->Lr;
    }

    if ((ContextFlags & CONTEXT_INTEGER) && (ExceptionFrame != NULL))
    {
        Context->X[19] = ExceptionFrame->X19;
        Context->X[20] = ExceptionFrame->X20;
        Context->X[21] = ExceptionFrame->X21;
        Context->X[22] = ExceptionFrame->X22;
        Context->X[23] = ExceptionFrame->X23;
        Context->X[24] = ExceptionFrame->X24;
        Context->X[25] = ExceptionFrame->X25;
        Context->X[26] = ExceptionFrame->X26;
        Context->X[27] = ExceptionFrame->X27;
        Context->X[28] = ExceptionFrame->X28;
    }

    if (ContextFlags & CONTEXT_CONTROL)
    {
        Context->Pc = TrapFrame->Pc;
        Context->Sp = TrapFrame->Sp;
        Context->Cpsr = TrapFrame->Spsr;
    }

    if ((ContextFlags & CONTEXT_DEBUG_REGISTERS) && TrapFrame->DebugRegistersValid)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Context->Bcr); Index++)
        {
            Context->Bcr[Index] = TrapFrame->Bcr[Index];
            Context->Bvr[Index] = TrapFrame->Bvr[Index];
        }

        for (Index = 0; Index < RTL_NUMBER_OF(Context->Wcr); Index++)
        {
            Context->Wcr[Index] = TrapFrame->Wcr[Index];
            Context->Wvr[Index] = TrapFrame->Wvr[Index];
        }
    }

    /* Floating point/NEON state: provide a view from the per-CPU snapshot.
     * This keeps KD context queries consistent even before full FP save/restore
     * is implemented in the trap path. */
    if (ContextFlags & CONTEXT_FLOATING_POINT)
    {
        PKPRCB Prcb = KeGetCurrentPrcb();
        if (Prcb != NULL)
        {
            RtlCopyMemory(Context->V,
                          Prcb->ProcessorState.ContextFrame.V,
                          sizeof(Context->V));
            Context->Fpcr = Prcb->ProcessorState.ContextFrame.Fpcr;
            Context->Fpsr = Prcb->ProcessorState.ContextFrame.Fpsr;
        }
    }

    if (OldIrql < APC_LEVEL)
    {
        KeLowerIrql(OldIrql);
    }
}

VOID
KiGetTrapContextInternal(_In_ PKTRAP_FRAME TrapFrame,
                         _Out_ PCONTEXT Context)
{
    ASSERT(TrapFrame != NULL);
    ASSERT(Context != NULL);

    /*
     * Keep bugcheck/KD trap capture to the state that is actually present in
     * the trap frame. CONTEXT_FULL includes CONTEXT_FLOATING_POINT on ARM64,
     * which currently has to be synthesized from per-CPU PRCB state and can
     * fault again while we are already handling a fatal trap.
     */
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_ARM64;
    KeTrapFrameToContext(TrapFrame, NULL, Context);
}

VOID
KiSetTrapContextInternal(_Out_ PKTRAP_FRAME TrapFrame,
                         _In_ PCONTEXT Context,
                         _In_ KPROCESSOR_MODE RequestorMode)
{
    ASSERT(TrapFrame != NULL);
    ASSERT(Context != NULL);

    KeContextToTrapFrame(Context, NULL, TrapFrame, Context->ContextFlags, RequestorMode);
}
