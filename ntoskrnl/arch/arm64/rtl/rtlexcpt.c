/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/rtl/rtlexcpt.c
 * PURPOSE:         Exception helper stubs for ARM64 stack walking
 */

#include <ntoskrnl.h>
#include <ndk/rtltypes.h>

/*
 * ARM64 .pdata entries are 8 bytes (2 x DWORD):
 *   DWORD BeginAddress;
 *   DWORD UnwindData;
 */
typedef struct _IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY {
    DWORD BeginAddress;
    DWORD UnwindData;
} RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

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
 * RtlpSafeReadMemory - Safe memory read for stack walking.
 *
 * Keep this path independent from SEH: the debugger uses it while reporting
 * exceptions, so taking and handling another exception here can corrupt the
 * interrupted context before the original fault is printed.
 */
static
BOOLEAN
RtlpSafeReadMemory(
    OUT PVOID Dest,
    IN PVOID Src,
    IN SIZE_T Size)
{
    ULONG_PTR Start, End;

    if (Size == 0)
        return TRUE;

    if (Src == NULL)
        return FALSE;

    Start = (ULONG_PTR)Src;
    End = Start + Size - 1;
    if (End < Start)
        return FALSE;

    /*
     * On ARM64, we need to verify the address is mapped before reading.
     * We use MmIsAddressValid which is safe to call at any IRQL.
     *
     * Check both the start and end of the range for safety.
     */
    if (!MmIsAddressValid(Src))
        return FALSE;

    if (!MmIsAddressValid((PVOID)End))
        return FALSE;

    RtlCopyMemory(Dest, Src, Size);
    return TRUE;
}

ULONG
NTAPI
RtlWalkFrameChain(OUT PVOID *Callers,
                  IN ULONG Count,
                  IN ULONG Flags)
{
    CONTEXT Context;
    ULONG64 StackBegin = 0, StackEnd = 0;
    ULONG64 ControlPc;
    ULONG FramesToSkip, Captured = 0, i;
    BOOLEAN StackLimitsOk;
    const ULONG64 MinKernelAddress = 0xFFFF000000000000ULL;
    ULONG ProcessorNumber;

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
     * Do not use table-driven PE unwinding from the debugger stack walker yet.
     * The ARM64 unwinder can touch .pdata/.xdata and saved registers while the
     * exception system is already active; if that faults, the handled exception
     * path can resume the original trap frame with a bogus PC during phase 1.
     * FP walking gives KDB a stable best-effort trace without creating a
     * second exception while reporting the first one.
     */

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

    for (i = 0; i < FramesToSkip + Count; i++)
    {
        ULONG64 NewFp, NewPc;

        if (ControlPc < MinKernelAddress)
            break;

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

        /* Use validated reads instead of raw frame-pointer dereferences. */
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

        ControlPc = Context.Pc;
        if (ControlPc < MinKernelAddress)
            break;
        if (i >= FramesToSkip)
        {
            Callers[Captured++] = (PVOID)ControlPc;
        }
    }

    /* Release reentrancy guard */
    InterlockedExchange(&g_RtlWalkFrameChainActive[ProcessorNumber], 0);

    return Captured;
}

/*
 * Stub implementations for RTL functions not yet available in arm64 RTL library.
 * These will be replaced when the full arm64 RTL unwind support is implemented.
 */

typedef struct _UNWIND_HISTORY_TABLE {
    ULONG Count;
    UCHAR Search;
    ULONG64 LowAddress;
    ULONG64 HighAddress;
} UNWIND_HISTORY_TABLE, *PUNWIND_HISTORY_TABLE;

typedef struct _KNONVOLATILE_CONTEXT_POINTERS {
    PULONG64 X19, X20, X21, X22, X23, X24, X25, X26, X27, X28;
    PULONG64 Fp, Lr;
    PULONG64 D8, D9, D10, D11, D12, D13, D14, D15;
} KNONVOLATILE_CONTEXT_POINTERS, *PKNONVOLATILE_CONTEXT_POINTERS;
