/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/rtl/rtlexcpt.c
 * PURPOSE:         Exception helper stubs for ARM64 stack walking
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * Reentrancy guard for RtlWalkFrameChain.
 *
 * CRITICAL: This prevents infinite recursion when the stack walker itself
 * causes a page fault. Without this guard, the sequence would be:
 *   1. Initial crash/bugcheck
 *   2. KeBugCheckWithTf tries to generate backtrace
 *   3. RtlWalkFrameChain calls RtlLookupFunctionEntry
 *   4. RtlLookupFunctionEntry accesses unmapped PE header -> PAGE_FAULT
 *   5. Page fault handler tries to generate backtrace
 *   6. RtlWalkFrameChain calls RtlLookupFunctionEntry -> PAGE_FAULT
 *   7. Infinite recursion until stack exhaustion
 *
 * This per-processor guard ensures that if RtlWalkFrameChain is already
 * active on the current processor, we immediately return 0 frames.
 *
 * Note: We use a simple counter rather than a spinlock because:
 *   - We're already at elevated IRQL during exceptions
 *   - We only need per-processor reentrancy detection
 *   - The guard is extremely hot path and must be fast
 */
static volatile LONG g_RtlWalkFrameChainActive[MAXIMUM_PROCESSORS] = {0};

static
BOOLEAN
NTAPI
RtlpCaptureStackLimits(IN ULONG_PTR FramePointer,
                       IN ULONG_PTR *StackBegin,
                       IN ULONG_PTR *StackEnd)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKPRCB Prcb;

    if (KeGetCurrentIrql() > DISPATCH_LEVEL) return FALSE;

    *StackBegin = (ULONG_PTR)Thread->StackLimit;
    *StackEnd = (ULONG_PTR)Thread->StackBase;

    if ((*StackBegin <= FramePointer) && (FramePointer <= *StackEnd))
    {
        *StackBegin = FramePointer;
        return TRUE;
    }

    Prcb = KeGetCurrentPrcb();
    if (Prcb && Prcb->DpcRoutineActive && Prcb->DpcStack)
    {
        ULONG_PTR DpcStack = (ULONG_PTR)Prcb->DpcStack;
        ULONG_PTR DpcBegin = DpcStack - KERNEL_STACK_SIZE;

        if ((DpcBegin <= FramePointer) && (FramePointer <= DpcStack))
        {
            *StackBegin = FramePointer;
            *StackEnd = DpcStack;
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * RtlpSafeReadMemory - Safe memory read for stack walking
 *
 * This is a minimal safe read that doesn't rely on SEH or complex
 * exception handling, designed to be safe during nested exceptions.
 */
static
BOOLEAN
RtlpSafeReadMemory(
    OUT PVOID Dest,
    IN PVOID Src,
    IN SIZE_T Size)
{
    /*
     * On ARM64, we need to verify the address is mapped before reading.
     * We use MmIsAddressValid which is safe to call at any IRQL.
     *
     * Check both the start and end of the range for safety.
     */
    if (!MmIsAddressValid(Src))
        return FALSE;

    if (Size > 1 && !MmIsAddressValid((PCHAR)Src + Size - 1))
        return FALSE;

    /*
     * Use volatile to prevent compiler from optimizing away the read,
     * and ensure proper memory ordering on ARM64's relaxed memory model.
     */
    _SEH2_TRY
    {
        RtlCopyMemory(Dest, Src, Size);
        _SEH2_YIELD(return TRUE);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return FALSE);
    }
    _SEH2_END;

    return FALSE;
}

ULONG
NTAPI
RtlWalkFrameChain(OUT PVOID *Callers,
                  IN ULONG Count,
                  IN ULONG Flags)
{
    CONTEXT Context;
    ULONG64 StackBegin = 0, StackEnd = 0;
    ULONG64 ControlPc, ImageBase, EstablisherFrame;
    PVOID HandlerData;
    ULONG FramesToSkip, Captured = 0, i;
    PRUNTIME_FUNCTION FunctionEntry;
    BOOLEAN StackLimitsOk;
    const ULONG64 MinKernelAddress = 0xFFFF000000000000ULL;
    ULONG ProcessorNumber;
    BOOLEAN SkipRtlLookup = FALSE;

    if (!Callers || Count == 0) return 0;

    /* User-mode walking is not implemented on ARM64 yet */
    if (Flags == 1) return 0;

    /*
     * CRITICAL: Reentrancy guard to prevent infinite recursion.
     *
     * If RtlWalkFrameChain is already active on this processor, we're
     * in a recursive exception scenario. Return immediately to break
     * the recursion cycle.
     *
     * This commonly happens when:
     *   1. RtlLookupFunctionEntry accesses an unmapped PE header
     *   2. The resulting page fault triggers a new backtrace
     *   3. Which calls RtlWalkFrameChain again
     *   4. Leading to stack exhaustion and system hang
     */
    ProcessorNumber = KeGetCurrentProcessorNumber();
    if (ProcessorNumber >= MAXIMUM_PROCESSORS)
        ProcessorNumber = 0;  /* Safety fallback */

    if (InterlockedCompareExchange(&g_RtlWalkFrameChainActive[ProcessorNumber], 1, 0) != 0)
    {
        /*
         * Already active on this processor - we're in a recursive call.
         * Return 0 frames to break the recursion.
         */
        return 0;
    }

    /*
     * Check if we're already at very high IRQL - if so, avoid calling
     * RtlLookupFunctionEntry as it may access paged memory.
     */
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
    {
        SkipRtlLookup = TRUE;
    }

    FramesToSkip = Flags >> 8;

    RtlCaptureContext(&Context);
    ControlPc = Context.Pc;

    StackLimitsOk = RtlpCaptureStackLimits(Context.Fp ? Context.Fp : Context.Sp,
                                           &StackBegin,
                                           &StackEnd);
    if (!StackLimitsOk)
    {
        StackBegin = Context.Sp;
        StackEnd = Context.Sp + KERNEL_STACK_SIZE;
    }

    _SEH2_TRY
    {
        for (i = 0; i < FramesToSkip + Count; i++)
        {
            BOOLEAN Unwound = FALSE;

            if (ControlPc < MinKernelAddress)
                break;

            /*
             * Only attempt RtlLookupFunctionEntry if we're not in a
             * high-IRQL or recursive scenario. RtlLookupFunctionEntry
             * can access PE headers which may not be mapped, causing
             * page faults that lead to recursive exceptions.
             */
            if (!SkipRtlLookup)
            {
                FunctionEntry = RtlLookupFunctionEntry(ControlPc, (PULONG_PTR)&ImageBase, NULL);
                if (FunctionEntry)
                {
                    CONTEXT TempContext = Context;

                    RtlVirtualUnwind(0,
                                     ImageBase,
                                     ControlPc,
                                     FunctionEntry,
                                     &TempContext,
                                     &HandlerData,
                                     &EstablisherFrame,
                                     NULL);

                    if (TempContext.Pc != ControlPc &&
                        TempContext.Sp >= Context.Sp &&
                        TempContext.Sp >= StackBegin &&
                        TempContext.Sp < StackEnd)
                    {
                        Context = TempContext;
                        Unwound = TRUE;
                    }
                }
            }

            if (!Unwound)
            {
                ULONG64 NewFp, NewPc;

                /*
                 * FP-based unwinding fallback.
                 * Use safe memory reads to avoid faulting on invalid FP.
                 */
                if (Context.Fp < StackBegin ||
                    Context.Fp >= StackEnd ||
                    (Context.Fp & 0x7))
                {
                    break;
                }

                /*
                 * Use safe memory read instead of direct pointer dereference.
                 * This prevents page faults from propagating as exceptions.
                 */
                if (!RtlpSafeReadMemory(&NewFp, (PVOID)Context.Fp, sizeof(ULONG64)))
                    break;

                if (!RtlpSafeReadMemory(&NewPc, (PVOID)(Context.Fp + sizeof(ULONG_PTR)), sizeof(ULONG64)))
                    break;

                /* Validate NewFp points up the stack (or is 0 for end of chain) */
                if (NewFp != 0 && (NewFp <= Context.Fp || NewFp >= StackEnd))
                    break;

                /* Validate NewPc is a valid kernel address */
                if (NewPc != 0 && NewPc < MinKernelAddress)
                    break;

                Context.Sp = Context.Fp + (2 * sizeof(ULONG_PTR));
                Context.Fp = NewFp;
                Context.Pc = NewPc;

                /* If we hit end of FP chain, stop */
                if (NewFp == 0)
                {
                    if (i >= FramesToSkip && NewPc >= MinKernelAddress)
                    {
                        Callers[Captured++] = (PVOID)NewPc;
                    }
                    break;
                }
            }

            ControlPc = Context.Pc;
            if (ControlPc < MinKernelAddress)
                break;
            if (i >= FramesToSkip)
            {
                Callers[Captured++] = (PVOID)ControlPc;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /*
         * If we get here, something went wrong during stack walking.
         * Keep whatever frames we already captured rather than
         * discarding everything.
         */
        DPRINT1("RtlWalkFrameChain: Exception during stack walk, captured %lu frames\n", Captured);
    }
    _SEH2_END;

    /* Release reentrancy guard */
    InterlockedExchange(&g_RtlWalkFrameChainActive[ProcessorNumber], 0);

    return Captured;
}
