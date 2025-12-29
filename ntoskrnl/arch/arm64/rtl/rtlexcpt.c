/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/rtl/rtlexcpt.c
 * PURPOSE:         Exception helper stubs for ARM64 stack walking
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
RtlCaptureContext(_Out_ PCONTEXT ContextRecord)
{
    UINT64 Fp = 0, Lr = 0;

    if (!ContextRecord) return;

    RtlZeroMemory(ContextRecord, sizeof(*ContextRecord));
    ContextRecord->ContextFlags = CONTEXT_FULL;

    __asm__ __volatile__("mov %0, x29" : "=r"(Fp));
    __asm__ __volatile__("mov %0, x30" : "=r"(Lr));

    ContextRecord->Fp = Fp;
    ContextRecord->Lr = Lr;
    ContextRecord->Sp = (ULONG64)__builtin_frame_address(0);
    ContextRecord->Pc = (ULONG64)__builtin_return_address(0);
}

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

ULONG
NTAPI
RtlWalkFrameChain(OUT PVOID *Callers,
                  IN ULONG Count,
                  IN ULONG Flags)
{
    ULONG_PTR Stack, NewStack, StackBegin, StackEnd = 0;
    ULONG_PTR Pc;
    ULONG i = 0;
    BOOLEAN Result;

    if (!Callers || Count == 0) return 0;

    /* User-mode walking is not implemented on ARM64 yet */
    if (Flags == 1) return 0;

    __asm__ __volatile__("mov %0, x29" : "=r"(Stack));

    StackBegin = Stack;

    if (!Flags)
    {
        Result = RtlpCaptureStackLimits(Stack, &StackBegin, &StackEnd);
        if (!Result) return 0;
    }

    _SEH2_TRY
    {
        for (i = 0; i < Count; i++)
        {
            if ((Stack >= StackEnd) ||
                (!i ? (Stack < StackBegin) : (Stack <= StackBegin)) ||
                ((StackEnd - Stack) < (2 * sizeof(ULONG_PTR))))
            {
                break;
            }

            NewStack = *(PULONG_PTR)Stack;
            Pc = *(PULONG_PTR)(Stack + sizeof(ULONG_PTR));

            if (!((Stack < NewStack) && (NewStack < StackEnd)))
            {
                break;
            }

            Callers[i] = (PVOID)Pc;
            Stack = NewStack;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        i = 0;
    }
    _SEH2_END;

    return i;
}
