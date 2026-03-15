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
 * that WOW64 exception dispatch is not wired yet.
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

static
BOOLEAN
KiDispatchExceptionToUser(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ PEXCEPTION_RECORD ExceptionRecord)
{
    EXCEPTION_RECORD LocalExceptRecord;
    ULONG_PTR UserSp;
    PKUSER_EXCEPTION_STACK UserStack;

    ASSERT(TrapFrame != NULL);
    ASSERT(Context != NULL);
    ASSERT(ExceptionRecord != NULL);

    if (Context->Sp < sizeof(KUSER_EXCEPTION_STACK))
    {
        DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: SP too low Sp=%p Need=%Ix Pc=%p Lr=%p Code=0x%08lx\n",
                (PVOID)(ULONG_PTR)Context->Sp,
                sizeof(KUSER_EXCEPTION_STACK),
                (PVOID)(ULONG_PTR)Context->Pc,
                (PVOID)(ULONG_PTR)Context->Lr,
                ExceptionRecord->ExceptionCode);
        return FALSE;
    }

    /*
     * ARM64 FIX (Bug #19): Recursion guard.
     *
     * Detect and break infinite exception dispatch loops:
     *   exception -> dispatch to user -> dispatcher crashes -> exception -> ...
     *
     * Only guard against faults within KiUserExceptionDispatcher itself
     * and its nested helper (about 0x200 bytes). The previous guard used
     * a 2.25MB window that covered ALL of ntdll, preventing legitimate
     * exceptions in ntdll functions (e.g., streamout/printf null pointer)
     * from being dispatched to SEH handlers.
     *
     * Faults in RtlDispatchException/RtlpUnwindInternal are handled by
     * their own SEH frames and KiUserExceptionDispatcherNestedArm64.
     */
    {
        PKTHREAD Thread = KeGetCurrentThread();
        PEPROCESS Process = (PEPROCESS)Thread->ApcState.Process;

        if ((ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION ||
             ExceptionRecord->ExceptionCode == STATUS_STACK_OVERFLOW) &&
            KeUserExceptionDispatcher != NULL &&
            Process->SystemDllBase != NULL)
        {
            PVOID UserDispatcher = KiConvertSystemDllAddressToUser(
                KeUserExceptionDispatcher, Process);
            if ((UserDispatcher == NULL) &&
                (KeUserExceptionDispatcher != NULL) &&
                ((ULONG_PTR)KeUserExceptionDispatcher < (ULONG_PTR)MmSystemRangeStart))
            {
                UserDispatcher = KeUserExceptionDispatcher;
            }

            if (UserDispatcher != NULL)
            {
                ULONG_PTR DispatcherAddr = (ULONG_PTR)UserDispatcher;
                ULONG_PTR FaultPc = (ULONG_PTR)Context->Pc;

                /*
                 * Guard 1: PC within KiUserExceptionDispatcher or its
                 * nested helper. These functions span about 0x200 bytes
                 * before the dispatcher entry point.
                 */
                ULONG_PTR DispLo = (DispatcherAddr > 0x200) ? (DispatcherAddr - 0x200) : 0;
                ULONG_PTR DispHi = DispatcherAddr + 0x100;

                if (FaultPc >= DispLo && FaultPc < DispHi)
                {
                    DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: recursion guard hit (dispatcher range) Pc=%p Lr=%p Disp=[%p,%p)\n",
                            (PVOID)FaultPc,
                            (PVOID)(ULONG_PTR)Context->Lr,
                            (PVOID)DispLo,
                            (PVOID)DispHi);
                    return FALSE;
                }

                /*
                 * Guard 2: NULL function pointer call from the dispatcher.
                 * Instruction fetch at VA near 0 means a branch to NULL.
                 * Check if the return address (LR) points into the dispatcher.
                 */
                if (FaultPc < PAGE_SIZE)
                {
                    ULONG_PTR CallerLr = (ULONG_PTR)Context->Lr;
                    if (CallerLr >= DispLo && CallerLr < DispHi)
                    {
                        DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: recursion guard hit (null-call) Pc=%p Lr=%p Disp=[%p,%p)\n",
                                (PVOID)FaultPc,
                                (PVOID)CallerLr,
                                (PVOID)DispLo,
                                (PVOID)DispHi);
                        return FALSE;
                    }
                }
            }
        }
    }

    /* Allocate a 16-byte aligned exception stack frame on the user stack */
    UserSp = (ULONG_PTR)(Context->Sp - sizeof(KUSER_EXCEPTION_STACK));
    UserSp &= ~0xFULL;
    UserStack = (PKUSER_EXCEPTION_STACK)UserSp;

    if ((UserSp + sizeof(KUSER_EXCEPTION_STACK) - 1) > (ULONG_PTR)MmUserProbeAddress)
    {
        RtlZeroMemory(&LocalExceptRecord, sizeof(LocalExceptRecord));
        LocalExceptRecord.ExceptionCode = STATUS_ACCESS_VIOLATION;
        LocalExceptRecord.ExceptionFlags = 0;
        LocalExceptRecord.ExceptionRecord = NULL;
        LocalExceptRecord.NumberParameters = 0;
        ExceptionRecord = &LocalExceptRecord;
    }

    /*
     * ARM64 FIX: PSEH2 uses label-based pseudo-SEH on ARM64 which CANNOT
     * catch hardware page faults. The _SEH2_TRY below will NOT protect
     * against faults when writing to unmapped user stack pages.
     *
     * Pre-validate that all pages of the user exception frame are present
     * in TTBR0 page tables by walking via KSEG0 direct mapping. If any
     * page is not mapped, return FALSE to force process termination
     * instead of causing an unhandled kernel fault that stalls the system.
     *
     * This handles the case where a thread's stack has been freed (VAD
     * removed, PTEs cleared) but the thread still tries to dispatch an
     * exception — e.g., svchost.exe accessing a freed thread stack.
     */
    {
        ULONG64 Ttbr0Val;
        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0Val));
        ULONG64 RootPa = Ttbr0Val & 0x0000FFFFFFFFF000ULL;
        ULONG_PTR BadPage = 0;

        if (RootPa == 0)
        {
            DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: TTBR0 root is zero Pc=%p Lr=%p Sp=%p\n",
                    (PVOID)(ULONG_PTR)Context->Pc,
                    (PVOID)(ULONG_PTR)Context->Lr,
                    (PVOID)(ULONG_PTR)Context->Sp);
            return FALSE;
        }

        ULONG_PTR StartPage = UserSp & ~(ULONG_PTR)0xFFF;
        ULONG_PTR EndPage = (UserSp + sizeof(KUSER_EXCEPTION_STACK) - 1) & ~(ULONG_PTR)0xFFF;

        for (ULONG_PTR ChkPage = StartPage; ChkPage <= EndPage; ChkPage += PAGE_SIZE)
        {
            volatile ULONG64 *T;
            ULONG64 E;

            /* L0 */
            T = (volatile ULONG64 *)(0xFFFF800000000000ULL | RootPa);
            E = T[(ChkPage >> 39) & 0x1FF];
            if ((E & 0x3ULL) != 0x3ULL) { BadPage = ChkPage; goto StackNotMapped; }

            /* L1 */
            T = (volatile ULONG64 *)(0xFFFF800000000000ULL | (E & 0x0000FFFFFFFFF000ULL));
            E = T[(ChkPage >> 30) & 0x1FF];
            if ((E & 0x3ULL) != 0x3ULL) { BadPage = ChkPage; goto StackNotMapped; }

            /* L2 */
            T = (volatile ULONG64 *)(0xFFFF800000000000ULL | (E & 0x0000FFFFFFFFF000ULL));
            E = T[(ChkPage >> 21) & 0x1FF];
            if ((E & 0x3ULL) != 0x3ULL) { BadPage = ChkPage; goto StackNotMapped; }

            /* L3 */
            T = (volatile ULONG64 *)(0xFFFF800000000000ULL | (E & 0x0000FFFFFFFFF000ULL));
            E = T[(ChkPage >> 12) & 0x1FF];
            if ((E & 0x3ULL) != 0x3ULL) { BadPage = ChkPage; goto StackNotMapped; }
        }
        goto StackOk;

    StackNotMapped:
        DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: user exception stack unmapped Start=%p End=%p BadPage=%p Pc=%p Lr=%p\n",
                (PVOID)StartPage,
                (PVOID)EndPage,
                (PVOID)BadPage,
                (PVOID)(ULONG_PTR)Context->Pc,
                (PVOID)(ULONG_PTR)Context->Lr);
        return FALSE;

    StackOk:
        ;
    }

    _enable();

    _SEH2_TRY
    {
        ProbeForWrite(UserStack,
                      sizeof(*UserStack),
                      TYPE_ALIGNMENT(KUSER_EXCEPTION_STACK));
        RtlZeroMemory(UserStack, sizeof(*UserStack));
        UserStack->Context = *Context;
        UserStack->ExceptionRecord = *ExceptionRecord;
        UserStack->MachineFrame.Sp = Context->Sp;
        UserStack->MachineFrame.Pc = Context->Pc;
    }
    _SEH2_EXCEPT((LocalExceptRecord = *_SEH2_GetExceptionInformation()->ExceptionRecord),
                 EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: ProbeForWrite failed Code=0x%08lx Addr=%p Pc=%p Lr=%p Sp=%p\n",
                LocalExceptRecord.ExceptionCode,
                LocalExceptRecord.ExceptionAddress,
                (PVOID)(ULONG_PTR)Context->Pc,
                (PVOID)(ULONG_PTR)Context->Lr,
                (PVOID)(ULONG_PTR)Context->Sp);
        _disable();
        return FALSE;
    }
    _SEH2_END;

    /*
     * ARM64 FIX: Convert KeUserExceptionDispatcher to user address.
     */
    {
        PKTHREAD Thread = KeGetCurrentThread();
        PEPROCESS Process = (PEPROCESS)Thread->ApcState.Process;
        PVOID UserExceptionDispatcher = KiConvertSystemDllAddressToUser(KeUserExceptionDispatcher, Process);
        if ((UserExceptionDispatcher == NULL) &&
            (KeUserExceptionDispatcher != NULL) &&
            ((ULONG_PTR)KeUserExceptionDispatcher < (ULONG_PTR)MmSystemRangeStart))
        {
            UserExceptionDispatcher = KeUserExceptionDispatcher;
        }
        if (UserExceptionDispatcher == NULL)
        {
            DPRINT1("[arm64][EXC] unresolved exception dispatcher: proc=%.16s KeUserExceptionDispatcher=%p SystemDllBase=%p PspSystemDllBase=%p TrapPc=%p CtxPc=%p CtxLr=%p\n",
                    PsGetCurrentProcess()->ImageFileName,
                    KeUserExceptionDispatcher,
                    Process ? Process->SystemDllBase : NULL,
                    PspSystemDllBase,
                    (PVOID)(ULONG_PTR)TrapFrame->Pc,
                    (PVOID)(ULONG_PTR)Context->Pc,
                    (PVOID)(ULONG_PTR)Context->Lr);
            _disable();
            return FALSE;
        }

        TrapFrame->X0 = (ULONG64)(ULONG_PTR)&UserStack->ExceptionRecord;
        TrapFrame->X1 = (ULONG64)(ULONG_PTR)&UserStack->Context;
        TrapFrame->Sp = UserSp;
        TrapFrame->Pc = (ULONG64)(ULONG_PTR)UserExceptionDispatcher;
        TrapFrame->Lr = (ULONG64)(ULONG_PTR)UserExceptionDispatcher;

    }

    _disable();
    return TRUE;
}

static __attribute__((unused))
VOID
KiPageInDirectory(
    _In_ PVOID ImageBase,
    _In_ USHORT Directory)
{
    volatile CHAR *Pointer;
    ULONG Size;

    Pointer = RtlImageDirectoryEntryToData(ImageBase, TRUE, Directory, &Size);
    if (!Pointer) return;

    while ((LONG)Size > 0)
    {
        (void)*Pointer;
        Pointer += PAGE_SIZE;
        Size -= PAGE_SIZE;
    }
}

static
VOID
KiPrepareUserDebugData(VOID)
{
    /*
     * ARM64: Skip pre-paging of user-mode debug/exception directories.
     *
     * On ARM64 with Clang/mingw, PSEH2 uses label-based pseudo-SEH that
     * cannot catch hardware exceptions (page faults). KiPageInDirectory
     * reads user-mode pages to force them in, but if a page fault occurs
     * on an unmapped user page, the _SEH2_EXCEPT handler won't catch it,
     * causing a last-chance exception crash in KiDispatchException.
     *
     * This is only an optimization - the .pdata (exception) and .debug
     * directories will be demand-paged when the unwinder/debugger
     * actually accesses them.
     */
    return;
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

    DPRINT1("[arm64][EXC] KiDispatchException: Code=0x%08lx FirstChance=%lu Mode=%s PC=%p LR=%p SP=%p Addr=%p P0=%p P1=%p\n",
            ExceptionRecord->ExceptionCode,
            (ULONG)FirstChance,
            (PreviousMode == KernelMode) ? "K" : "U",
            (PVOID)(ULONG_PTR)Context.Pc,
            (PVOID)(ULONG_PTR)Context.Lr,
            (PVOID)(ULONG_PTR)Context.Sp,
            ExceptionRecord->ExceptionAddress,
            (ExceptionRecord->NumberParameters >= 1) ? (PVOID)ExceptionRecord->ExceptionInformation[0] : NULL,
            (ExceptionRecord->NumberParameters >= 2) ? (PVOID)ExceptionRecord->ExceptionInformation[1] : NULL);

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
            if ((!(PsGetCurrentProcess()->DebugPort) &&
                 !(KdIgnoreUmExceptions)) ||
                 (KdIsThisAKdTrap(ExceptionRecord, &Context, PreviousMode)))
            {
                KiPrepareUserDebugData();
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
            }

            if (DbgkForwardException(ExceptionRecord, TRUE, FALSE))
            {
                return;
            }

            if (KiDispatchExceptionToUser(TrapFrame, &Context, ExceptionRecord))
            {
                return;
            }

            DPRINT1("[arm64][EXC] KiDispatchException: first-chance user dispatch not handled Code=0x%08lx PC=%p LR=%p\n",
                    ExceptionRecord->ExceptionCode,
                    (PVOID)(ULONG_PTR)Context.Pc,
                    (PVOID)(ULONG_PTR)Context.Lr);
        }

        if (DbgkForwardException(ExceptionRecord, TRUE, TRUE))
        {
            return;
        }

        if (DbgkForwardException(ExceptionRecord, FALSE, TRUE))
        {
            return;
        }

        /* 3rd strike, kill the process */
        DPRINT1("Kill %.16s, ExceptionCode: %lx, ExceptionAddress: %p, BaseAddress: %p\n",
                PsGetCurrentProcess()->ImageFileName,
                ExceptionRecord->ExceptionCode,
                ExceptionRecord->ExceptionAddress,
                PsGetCurrentProcess()->SectionBaseAddress);

        ZwTerminateProcess(NtCurrentProcess(), ExceptionRecord->ExceptionCode);

        /* ZwTerminateProcess should never return for self-termination.
         * If we get here, the process could not be killed -- bugcheck.
         * NOTE: Do NOT insert KDBG/debugger fallback between ZwTerminateProcess
         * and KeBugCheckEx. KdbEnterDebuggerException can return kdHandleException
         * for user-mode exceptions without entering the CLI, which would cause
         * KiDispatchException to return to user mode and re-execute the faulting
         * instruction in an infinite loop. This matches x86/amd64 behavior. */
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
