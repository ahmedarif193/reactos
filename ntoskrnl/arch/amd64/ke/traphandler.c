/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         x64 trap handlers
 * PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <ndk/pstypes.h>

#define NDEBUG
#include <debug.h>

C_ASSERT(TABLE_NUMBER_BITS == 1);
C_ASSERT(TABLE_OFFSET_BITS == 12);
C_ASSERT(SSDT_MAX_ENTRIES == 2);

/* Win32k callouts installed at runtime */
extern PKWIN32_PROCESS_CALLOUT PspW32ProcessCallout;
extern PKWIN32_THREAD_CALLOUT PspW32ThreadCallout;

VOID
KiRetireDpcListInDpcStack(
    PKPRCB Prcb,
    PVOID DpcStack);

NTSTATUS
KiConvertToGuiThread(
    VOID);

#define MAX_SYSCALL_PARAMS 16

/* PRIVATE HELPERS ***********************************************************/

static __inline
PKTRAP_FRAME
KiGetCurrentSyscallTrapFrame(VOID)
{
    /*
     * KiSystemCallEntry64 stores the new trap frame pointer into
     * KTHREAD.TrapFrame before calling this routine. Use that instead
     * of the stack layout to avoid compiler-dependent assumptions.
     */
    PKTHREAD Thread = KeGetCurrentThread();
    ASSERT(Thread != NULL);
    ASSERT(Thread->TrapFrame != NULL);
    return Thread->TrapFrame;
}

/* PUBLIC FUNCTIONS **********************************************************/

_Requires_lock_not_held_(Prcb->PrcbLock)
VOID
NTAPI
KiDpcInterruptHandler(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD NewThread, OldThread;
    KIRQL OldIrql;

    /* Raise to DISPATCH_LEVEL */
    OldIrql = KfRaiseIrql(DISPATCH_LEVEL);

    /* Send an EOI (APIC_EOI write in the ASM stub is suppressed for DPC IRQ) */
    KiSendEOI();

    /* Check for pending timers, DPCs, or deferred ready threads */
    if ((Prcb->DpcData[0].DpcQueueDepth != 0) ||
        (Prcb->TimerRequest != FALSE) ||
        (Prcb->DeferredReadyListHead.Next != NULL))
    {
        /* Retire DPCs while running on the dedicated DPC stack */
        KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
    }

    /* Enable interrupts while we do the rest of the dispatcher work */
    _enable();

    /* Check for quantum end */
    if (Prcb->QuantumEnd)
    {
        /* Handle quantum end */
        Prcb->QuantumEnd = FALSE;
        KiQuantumEnd();
    }
    else if (Prcb->NextThread)
    {
        /*
         * We have a ready thread selected by the scheduler.
         * Protect the PRCB scheduling fields while we manipulate them.
         */
        KiAcquirePrcbLock(Prcb);

        /* Capture current/next thread */
        OldThread = Prcb->CurrentThread;
        NewThread = Prcb->NextThread;

        /* Remove the next thread from the PRCB */
        Prcb->NextThread = NULL;
        Prcb->CurrentThread = NewThread;

        /* The new thread is now considered running on this processor */
        NewThread->State = Running;

        /* Record why the old thread is leaving the CPU */
        OldThread->WaitReason = WrDispatchInt;

        /* Queue the old thread back to the ready queues */
        KxQueueReadyThread(OldThread, Prcb);

        /*
         * Swap context to the new thread.
         *
         * NOTE:
         *  The PRCB lock is intentionally left held here. The dispatcher /
         *  context switch path is responsible for releasing it as part of
         *  the standard KiExitDispatcher-style exit.
         */
        KiSwapContext(APC_LEVEL, OldThread);
    }

    /* Disable interrupts and return to the previous IRQL */
    _disable();
    KeLowerIrql(OldIrql);
}

VOID
KiNmiInterruptHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    BOOLEAN ManualSwapGs = FALSE;

    /*
     * If the NMI came from kernel mode, we may have been interrupted
     * in a window where GS still points to the user TEB. Detect that
     * using the GS base and manually issue SWAPGS if needed.
     */
    if ((TrapFrame->SegCs & MODE_MASK) == 0)
    {
        /*
         * Convention: kernel addresses live in the negative half of the
         * canonical address space. A non-negative GS base implies a user
         * GS base is currently loaded.
         */
        if ((LONG64)__readmsr(MSR_GS_BASE) >= 0)
        {
            __swapgs();
            ManualSwapGs = TRUE;
        }
    }

    /* Check if this is a processor freeze NMI */
    if (KiProcessorFreezeHandler(TrapFrame, ExceptionFrame))
    {
        /* NMI was handled by the freeze handler */
        goto Exit;
    }

    /* Normal NMI handling path */
    KiHandleNmi();

Exit:
    /* Restore GS if we swapped it above */
    if (ManualSwapGs)
    {
        __swapgs();
    }
}

NTSTATUS
NtSyscallFailure(VOID)
{
    /*
     * Common failure thunk for system services. The service return
     * value was preloaded into TrapFrame->Rax.
     */
    return (NTSTATUS)KeGetCurrentThread()->TrapFrame->Rax;
}

PVOID
KiSystemCallHandler(
    VOID)
{
    PKTRAP_FRAME TrapFrame;
    PKSERVICE_TABLE_DESCRIPTOR DescriptorTable;
    PKTHREAD Thread;
    PULONG64 KernelParams, UserParams;
    ULONG ServiceNumber, TableIndex, Count;
    ULONG64 UserRsp;
    volatile ULONG SehLastParamIndex = (ULONG)-1;
    volatile ULONG_PTR SehLastUserAddr = 0;

    /* Locate the trap frame for this syscall */
    TrapFrame = KiGetCurrentSyscallTrapFrame();

    /* Bump per-processor syscall count */
    __addgsdword(FIELD_OFFSET(KIPCR, Prcb.KeSystemCalls), 1);

    /* Get the current thread */
    Thread = KeGetCurrentThread();
    ASSERT(Thread != NULL);

    /*
     * Syscall path always comes from user-mode. PreviousMode on the
     * trap frame is not meaningful at this point on AMD64, so force
     * it to UserMode for the benefit of Nt* routines and probes.
     */
    TrapFrame->PreviousMode = UserMode;
    Thread->PreviousMode = UserMode;

    /* No exception frame is associated yet */
    TrapFrame->ExceptionFrame = 0ULL;

    /* Capture the user stack pointer from the trap frame */
    UserRsp = TrapFrame->Rsp;

    /* Enable interrupts before doing any potentially blocking work */
    _enable();

    /*
     * Guard against bogus user RSP values. We clamp it to the top of
     * user-mode address space to avoid probing kernel memory.
     */
    if (UserRsp > MmUserProbeAddress)
    {
        UserRsp = MmUserProbeAddress;
    }

    /* Compute parameter addresses */
    UserParams = (PULONG64)UserRsp + 1;
    KernelParams = (PULONG64)TrapFrame - MAX_SYSCALL_PARAMS;

    /*
     * Seed the first four parameters from the register state in the
     * trap frame. This mirrors the Windows AMD64 syscall contract:
     * RCX, RDX, R8, R9 are the first four arguments.
     */
    KernelParams[0] = TrapFrame->Rcx; /* param 1 */
    KernelParams[1] = TrapFrame->Rdx; /* param 2 */
    KernelParams[2] = TrapFrame->R8;  /* param 3 */
    KernelParams[3] = TrapFrame->R9;  /* param 4 */

    /* Decode the system call number */
    ServiceNumber = (ULONG)TrapFrame->Rax;
    TableIndex = (ServiceNumber >> TABLE_OFFSET_BITS) &
                 ((1u << TABLE_NUMBER_BITS) - 1u);
    ServiceNumber &= SERVICE_NUMBER_MASK;

    /* Handle win32k batching if this is a GDI/USER syscall */
    if (TableIndex == WIN32K_SERVICE_INDEX)
    {
        ULONG GdiBatchCount = 0;
        PTEB Teb = Thread->Teb;

        _SEH2_TRY
        {
            if (Teb != NULL)
                GdiBatchCount = Teb->GdiBatchCount;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            GdiBatchCount = 0;
        }
        _SEH2_END;

        if (GdiBatchCount != 0)
        {
            KeGdiFlushUserBatch();
        }
    }

    /* Validate table index */
    if (TableIndex >= SSDT_MAX_ENTRIES)
    {
        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return (PVOID)NtSyscallFailure;
    }

    if (Thread->ServiceTable == NULL)
    {
        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return (PVOID)NtSyscallFailure;
    }

    DescriptorTable =
        &((PKSERVICE_TABLE_DESCRIPTOR)Thread->ServiceTable)[TableIndex];
    if ((DescriptorTable->Base == NULL) ||
        (DescriptorTable->Number == NULL))
    {
        if ((TableIndex == WIN32K_SERVICE_INDEX) &&
            (Thread->ServiceTable == KeServiceDescriptorTable))
        {
            if ((PspW32ProcessCallout == NULL) ||
                (PspW32ThreadCallout == NULL))
            {
                TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
                return (PVOID)NtSyscallFailure;
            }

            return (PVOID)KiConvertToGuiThread;
        }

        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return (PVOID)NtSyscallFailure;
    }

    /* Validate the service number */
    if (ServiceNumber >= DescriptorTable->Limit)
    {
        /*
         * If this is a win32k call on a non-GUI thread, hand control
         * back to the syscall entry stub so it can expand the kernel
         * stack and convert the thread to a GUI thread.
         */
        if ((TableIndex == WIN32K_SERVICE_INDEX) &&
            (Thread->ServiceTable == KeServiceDescriptorTable))
        {
            /* If win32k isn't ready yet, fail the call gracefully */
            if ((PspW32ProcessCallout == NULL) ||
                (PspW32ThreadCallout == NULL))
            {
                TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
                return (PVOID)NtSyscallFailure;
            }

            return (PVOID)KiConvertToGuiThread;
        }

        /* Generic invalid service */
        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return (PVOID)NtSyscallFailure;
    }

    /* Compute parameter count (in qwords) */
    Count = DescriptorTable->Number[ServiceNumber] / sizeof(ULONG64);
    if (Count > MAX_SYSCALL_PARAMS)
    {
        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return (PVOID)NtSyscallFailure;
    }

    /*
     * Copy stack-based parameters from the user stack into the kernel
     * shadow buffer, under SEH so bad user pointers become exceptions
     * instead of kernel-mode AVs.
     *
     * The first four parameters are already in KernelParams[0..3].
     * Here we fill [4..Count-1], as required.
     */
    _SEH2_TRY
    {
#define COPY_UPARAM(i) \
        do { \
            SehLastParamIndex = (i); \
            SehLastUserAddr = (ULONG_PTR)&UserParams[(i)]; \
            KernelParams[(i)] = UserParams[(i)]; \
        } while (0)

        switch (Count)
        {
            case 16: COPY_UPARAM(15);
            case 15: COPY_UPARAM(14);
            case 14: COPY_UPARAM(13);
            case 13: COPY_UPARAM(12);
            case 12: COPY_UPARAM(11);
            case 11: COPY_UPARAM(10);
            case 10: COPY_UPARAM(9);
            case 9:  COPY_UPARAM(8);
            case 8:  COPY_UPARAM(7);
            case 7:  COPY_UPARAM(6);
            case 6:  COPY_UPARAM(5);
            case 5:  COPY_UPARAM(4);
            case 4:
            case 3:
            case 2:
            case 1:
            case 0:
                break;

            default:
                ASSERT(FALSE);
                break;
        }

#undef COPY_UPARAM
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DbgPrint("KiSystemCallHandler SEH: file %s line %d, SRsp=%p, "
                 "Tbl=%lu Svc=%lu Cnt=%lu, LastIdx=%lu, LastUser=%p, "
                 "Code=0x%08lx\n",
                 __FILE__, __LINE__, (PVOID)UserRsp,
                 (ULONG)TableIndex, (ULONG)ServiceNumber, (ULONG)Count,
                 (ULONG)SehLastParamIndex, (PVOID)SehLastUserAddr,
                 (ULONG)_SEH2_GetExceptionCode());

        TrapFrame->Rax = _SEH2_GetExceptionCode();
        return (PVOID)NtSyscallFailure;
    }
    _SEH2_END;

    /*
     * Return the target service routine. The syscall entry stub will
     * load RCX/RDX/R8/R9 from the trap frame, adjust the stack if
     * needed, and perform the actual call.
     */
    return (PVOID)DescriptorTable->Base[ServiceNumber];
}

/*
 * Generic service dispatcher stub for non-syscall paths (e.g. legacy
 * INT 2E or internal callers). On AMD64 ReactOS all external system
 * calls are expected to use KiSystemCallHandler + KiSystemCallEntry64.
 *
 * We nevertheless provide a conservative implementation that validates
 * the service number and updates the trap frame's RAX with either the
 * service status code or an error. The actual calling convention for
 * arguments is the caller's responsibility.
 */
VOID
KiSystemService(
    IN PKTHREAD Thread,
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG Instruction)
{
    PKSERVICE_TABLE_DESCRIPTOR DescriptorTable;
    ULONG ServiceNumber, TableIndex;
    PVOID ServiceRoutine;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Instruction);

    ASSERT(Thread == KeGetCurrentThread());
    ASSERT(TrapFrame != NULL);

    /* Decode the system call number */
    ServiceNumber = (ULONG)TrapFrame->Rax;
    TableIndex = (ServiceNumber >> TABLE_OFFSET_BITS) &
                 ((1u << TABLE_NUMBER_BITS) - 1u);
    ServiceNumber &= SERVICE_NUMBER_MASK;

    /* Validate table index */
    if ((Thread->ServiceTable == NULL) ||
        (TableIndex >= SSDT_MAX_ENTRIES))
    {
        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return;
    }

    DescriptorTable =
        &((PKSERVICE_TABLE_DESCRIPTOR)Thread->ServiceTable)[TableIndex];

    /* Validate service index */
    if (ServiceNumber >= DescriptorTable->Limit)
    {
        TrapFrame->Rax = STATUS_INVALID_SYSTEM_SERVICE;
        return;
    }

    /*
     * For now we only support kernel-mode callers here. PreviousMode
     * must already be set by the caller (e.g. Zw stub) and we do not
     * attempt to probe or copy user-mode parameters.
     */
    ASSERT(Thread->PreviousMode == KernelMode ||
           Thread->PreviousMode == UserMode);

    ServiceRoutine = (PVOID)(ULONG_PTR)DescriptorTable->Base[ServiceNumber];
    ASSERT(ServiceRoutine != NULL);

    /*
     * Call the service routine with whatever argument state the caller
     * has set up. From the compiler's point of view this is a function
     * with no explicit parameters, but in practice the caller will
     * have prepared RCX/RDX/R8/R9/stack according to the AMD64 ABI.
     */
    Status = ((NTSTATUS (NTAPI *)(VOID))ServiceRoutine)();

    /* Propagate the return status into the trap frame */
    TrapFrame->Rax = Status;
}
