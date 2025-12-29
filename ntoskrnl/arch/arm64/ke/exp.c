/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/exp.c
 * PURPOSE:         Exception handling stubs for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#ifdef KDBG
#include <kdbg/kdb.h>
#endif

/*
 * The AMD64 port already carries the full exception-dispatch machinery.
 * For ARM64 we reuse the same high-level policy so that assertions and
 * bugchecks behave identically.  The only architecture-specific details are
 * the register names (PC instead of RIP, 4-byte breakpoints) and the fact
 * that we currently lack a user-mode exception dispatcher.  Until the latter
 * is implemented we terminate the offending process on an unhandled
 * user-mode second chance exception instead of trying to build the WOW64
 * stack frame.
 */

static __attribute__((unused)) VOID KiArm64DbgPrintBacktraceImpl(_In_ PCONTEXT Ctx)
{
    if (!Ctx || !(Ctx->ContextFlags & CONTEXT_ARM64)) return;

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[arm64] Backtrace: PC=%p SP=%p CPSR=0x%08lx\n",
               (PVOID)(ULONG_PTR)Ctx->Pc,
               (PVOID)(ULONG_PTR)Ctx->Sp,
               (ULONG)Ctx->Cpsr);
}

VOID
NTAPI
KiDispatchException(_In_ PEXCEPTION_RECORD ExceptionRecord,
                    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
                    _Inout_ PKTRAP_FRAME TrapFrame,
                    _In_ KPROCESSOR_MODE PreviousMode,
                    _In_ BOOLEAN FirstChance)
{
    CONTEXT Context;
    BOOLEAN Handled = FALSE;

    ASSERT(ExceptionRecord != NULL);
    ASSERT(TrapFrame != NULL);

    /* Match the behaviour of other architectures for accounting. */
    KeGetCurrentPrcb()->KeExceptionDispatchCount++;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS |
                           CONTEXT_X18 | CONTEXT_ARM64;

    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Context);

    switch (ExceptionRecord->ExceptionCode)
    {
        case STATUS_BREAKPOINT:
            /* BRK consumes 4 bytes on ARM64.  Rewind so the debugger sees it. */
            if (Context.Pc >= 4)
            {
                Context.Pc -= 4;
            }
            break;

        case KI_EXCEPTION_ACCESS_VIOLATION:
            ExceptionRecord->ExceptionCode = STATUS_ACCESS_VIOLATION;
            break;
    }

    if (PreviousMode == KernelMode)
    {
        if (FirstChance)
        {
            if (KiDebugRoutine(TrapFrame,
                               ExceptionFrame,
                               ExceptionRecord,
                               &Context,
                               PreviousMode,
                               FALSE))
            {
                Handled = TRUE;
                goto HandledExit;
            }

            if (RtlDispatchException(ExceptionRecord, &Context))
            {
                Handled = TRUE;
                goto HandledExit;
            }
        }

        if (KiDebugRoutine(TrapFrame,
                           ExceptionFrame,
                           ExceptionRecord,
                           &Context,
                           PreviousMode,
                           TRUE))
        {
            Handled = TRUE;
            goto HandledExit;
        }

#ifdef KDBG
        /*
         * If no KD host is attached, fall back to the integrated KDBG CLI
         * to allow local interactive debugging (e.g. bt).
         */
        if (!KdDebuggerEnabled || KdDebuggerNotPresent)
        {
            /* Print a minimal banner/backtrace so something is visible. */
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[arm64] Exception (second-chance) code=0x%08lx at %p\n",
                       ExceptionRecord->ExceptionCode,
                       ExceptionRecord->ExceptionAddress);
            KiArm64DbgPrintBacktraceImpl(&Context);

            EXCEPTION_RECORD64 Rec64;
            ULONG i;

            RtlZeroMemory(&Rec64, sizeof(Rec64));
            Rec64.ExceptionCode = ExceptionRecord->ExceptionCode;
            Rec64.ExceptionFlags = ExceptionRecord->ExceptionFlags;
            Rec64.ExceptionAddress = (ULONG64)(ULONG_PTR)ExceptionRecord->ExceptionAddress;
            Rec64.NumberParameters = ExceptionRecord->NumberParameters;
            if (Rec64.NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS)
                Rec64.NumberParameters = EXCEPTION_MAXIMUM_PARAMETERS;
            for (i = 0; i < Rec64.NumberParameters; i++)
            {
                Rec64.ExceptionInformation[i] = (ULONG64)ExceptionRecord->ExceptionInformation[i];
            }

            if (KdbEnterDebuggerException(&Rec64,
                                          PreviousMode,
                                          &Context,
                                          FALSE) == kdHandleException)
            {
                Handled = TRUE;
                goto HandledExit;
            }
        }
#endif

        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                     ExceptionRecord->ExceptionCode,
                     (ULONG_PTR)ExceptionRecord->ExceptionAddress,
                     (ULONG_PTR)TrapFrame,
                     0);
    }
    else
    {
        if (FirstChance)
        {
            if (DbgkForwardException(ExceptionRecord, TRUE, FALSE))
            {
                return;
            }
        }

        if (DbgkForwardException(ExceptionRecord, TRUE, TRUE))
        {
            return;
        }

        if (DbgkForwardException(ExceptionRecord, FALSE, TRUE))
        {
            return;
        }

        DPRINT1("ARM64: terminating process %.16s due to unhandled exception %lx @ %p\n",
                PsGetCurrentProcess()->ImageFileName,
                ExceptionRecord->ExceptionCode,
                ExceptionRecord->ExceptionAddress);

        ZwTerminateProcess(NtCurrentProcess(), ExceptionRecord->ExceptionCode);

#ifdef KDBG
        /* As a last resort for user-mode unhandled exceptions, try KDBG CLI
         * if no KD host is attached. */
        if (!KdDebuggerEnabled || KdDebuggerNotPresent)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[arm64] User-mode unhandled exception code=0x%08lx at %p\n",
                       ExceptionRecord->ExceptionCode,
                       ExceptionRecord->ExceptionAddress);
            KiArm64DbgPrintBacktraceImpl(&Context);

            EXCEPTION_RECORD64 Rec64;
            ULONG i;

            RtlZeroMemory(&Rec64, sizeof(Rec64));
            Rec64.ExceptionCode = ExceptionRecord->ExceptionCode;
            Rec64.ExceptionFlags = ExceptionRecord->ExceptionFlags;
            Rec64.ExceptionAddress = (ULONG64)(ULONG_PTR)ExceptionRecord->ExceptionAddress;
            Rec64.NumberParameters = ExceptionRecord->NumberParameters;
            if (Rec64.NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS)
                Rec64.NumberParameters = EXCEPTION_MAXIMUM_PARAMETERS;
            for (i = 0; i < Rec64.NumberParameters; i++)
            {
                Rec64.ExceptionInformation[i] = (ULONG64)ExceptionRecord->ExceptionInformation[i];
            }

            if (KdbEnterDebuggerException(&Rec64,
                                          PreviousMode,
                                          &Context,
                                          FALSE) == kdHandleException)
            {
                Handled = TRUE;
                goto HandledExit;
            }
        }
#endif
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                     ExceptionRecord->ExceptionCode,
                     (ULONG_PTR)ExceptionRecord->ExceptionAddress,
                     (ULONG_PTR)TrapFrame,
                     0);
    }

HandledExit:
    if (Handled)
    {
        KeContextToTrapFrame(&Context,
                             ExceptionFrame,
                             TrapFrame,
                             Context.ContextFlags,
                             PreviousMode);
    }
}
