/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/trapc.c
 * PURPOSE:         Trap handling stubs for ARM64
 */

#include <ntoskrnl.h>
#include <arm64trap.h>
#define NDEBUG
#include <debug.h>
#include <mm/ARM3/miarm.h>
#ifdef KDBG
#include <kdbg/kdb.h>
#endif

#define ARM64_STUB() UNIMPLEMENTED_DBGBREAK()

extern BOOLEAN KdDebuggerEnabled;
extern BOOLEAN KdDebuggerNotPresent;
extern BOOLEAN KdPitchDebugger;

static __inline VOID
KiArm64StageLogf(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[192];
    va_list Args;

    va_start(Args, Format);
    if (NT_SUCCESS(RtlStringCbVPrintfA(Buffer, sizeof(Buffer), Format, Args)))
    {
        DPRINT1("%s\n", Buffer);
    }
    va_end(Args);
}

#ifndef KI_ARM64_STAGE_LOGF
#define KI_ARM64_STAGE_LOGF(...) KiArm64StageLogf(__VA_ARGS__)
#endif

NTSTATUS
NTAPI
MmArmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation);

/*
 * MmAccessFault: The dispatch function that routes page faults to the
 * appropriate handler (MmArmAccessFault for ARM3 allocations, or
 * MmNotPresentFaultSectionView for ROS section views like VACB buffers).
 *
 * ARM64 MUST call this instead of MmArmAccessFault to properly handle
 * kernel section views created by the ReactOS memory manager.
 */
NTSTATUS
NTAPI
MmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation);

VOID
KiSystemService(
    _Inout_ PKTHREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Instruction);

VOID
NTAPI
KiSwapProcess(_Inout_ PKPROCESS NewProcess,
              _Inout_ PKPROCESS OldProcess)
{
    ASSERT(NewProcess != NULL);

#ifdef CONFIG_SMP
    {
        PKIPCR Pcr = KeGetPcr();
        if (Pcr != NULL)
        {
            KAFFINITY Member = Pcr->Prcb.SetMember;

            NewProcess->ActiveProcessors ^= Member;
            if (OldProcess != NULL)
            {
                OldProcess->ActiveProcessors ^= Member;
            }
        }
    }
#endif

    if (OldProcess == NewProcess)
    {
        return;
    }

    if ((OldProcess != NULL) &&
        (NewProcess->DirectoryTableBase[0] == OldProcess->DirectoryTableBase[0]))
    {
        return;
    }

    ASSERT(NewProcess->DirectoryTableBase[0] != 0);
    KiArm64WriteUserTtbr(NewProcess->DirectoryTableBase[0]);
}

typedef struct _ARM64_EARLY_SYNC_CONTEXT
{
    ARM64_EARLY_TRAP_STATE State;
    PKTRAP_FRAME TrapFramePointer;
    PKEXCEPTION_FRAME ExceptionFramePointer;
    KTRAP_FRAME TrapFrame;
    KEXCEPTION_FRAME ExceptionFrame;
} ARM64_EARLY_SYNC_CONTEXT, *PARM64_EARLY_SYNC_CONTEXT;

C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.VectorId) == 0x0);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.ExceptionSyndrome) == 0x8);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.FaultAddress) == 0x10);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Elr) == 0x18);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Spsr) == 0x20);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.X[0]) == 0x28);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.Sp) == 0x120);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.Pc) == 0x128);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.Pstate) == 0x130);
C_ASSERT(sizeof(ARM64_EARLY_TRAP_STATE) == 0x138);  /* State should end at 0x138 */
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, TrapFramePointer) == 0x138);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, ExceptionFramePointer) == 0x140);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, TrapFrame) == 0x148);
#define ARM64_EARLY_SYNC_CONTEXT_ALLOC_SIZE 0x380
C_ASSERT(sizeof(ARM64_EARLY_SYNC_CONTEXT) <= ARM64_EARLY_SYNC_CONTEXT_ALLOC_SIZE);

static
KPROCESSOR_MODE
KiArm64PreviousModeFromSpsr(_In_ ULONG64 SpsrValue)
{
    ULONG Mode = (ULONG)(SpsrValue & 0xFULL);
    return (Mode == 0) ? UserMode : KernelMode;
}

static
VOID
KiArm64InitializeTrapFrame(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context,
    _Out_ PKTRAP_FRAME TrapFrame)
{
    ULONG64 Fpcr = 0;
    ULONG64 Fpsr = 0;
    PKEXCEPTION_FRAME ExceptionFrame = &Context->ExceptionFrame;
    KIRQL CurrentIrql;

    RtlZeroMemory(TrapFrame, sizeof(*TrapFrame));

    /*
     * Capture IRQL BEFORE any potential IRQL changes during exception handling.
     * For synchronous exceptions (data/instruction abort), we're at the IRQL
     * that was active when the fault occurred. This is critical for Windows
     * ARM64 compliance - the trap frame must preserve the interrupted IRQL.
     */
    CurrentIrql = KeGetCurrentIrql();

    TrapFrame->PreviousMode = (CHAR)KiArm64PreviousModeFromSpsr(Context->State.Spsr);
    TrapFrame->PreviousIrql = (UCHAR)CurrentIrql;
    TrapFrame->TrapFrame = (ULONG64)(ULONG_PTR)TrapFrame;
    TrapFrame->FaultAddress = Context->State.FaultAddress;
    TrapFrame->Spsr = (ULONG)Context->State.Spsr;
    TrapFrame->Esr = (ULONG)Context->State.ExceptionSyndrome;
    TrapFrame->Sp = Context->State.Registers.Sp;
    TrapFrame->Pc = Context->State.Elr;
    TrapFrame->Lr = Context->State.Registers.X[30];
    TrapFrame->Fp = Context->State.Registers.X[29];

    RtlCopyMemory(TrapFrame->X,
                  Context->State.Registers.X,
                  sizeof(TrapFrame->X));

    RtlZeroMemory(ExceptionFrame, sizeof(*ExceptionFrame));
    ExceptionFrame->TrapFrame = (ULONG64)(ULONG_PTR)TrapFrame;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(Fpcr));
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(Fpsr));
    ExceptionFrame->Fpcr = Fpcr;
    ExceptionFrame->Fpsr = Fpsr;
    ExceptionFrame->X19 = Context->State.Registers.X[19];
    ExceptionFrame->X20 = Context->State.Registers.X[20];
    ExceptionFrame->X21 = Context->State.Registers.X[21];
    ExceptionFrame->X22 = Context->State.Registers.X[22];
    ExceptionFrame->X23 = Context->State.Registers.X[23];
    ExceptionFrame->X24 = Context->State.Registers.X[24];
    ExceptionFrame->X25 = Context->State.Registers.X[25];
    ExceptionFrame->X26 = Context->State.Registers.X[26];
    ExceptionFrame->X27 = Context->State.Registers.X[27];
    ExceptionFrame->X28 = Context->State.Registers.X[28];
    ExceptionFrame->Fp = Context->State.Registers.X[29];
    ExceptionFrame->Lr = Context->State.Registers.X[30];
    Context->ExceptionFramePointer = ExceptionFrame;
}

static LONG KiArm64SyncExceptionLogBudget = 128;
static volatile LONG KiArm64DataAbortOwner[MAXIMUM_PROCESSORS];
/*
 * One-shot trap guard per CPU to prevent recursive exception storms before
 * the debugger is fully operational. Set on first entry; any re-entry while
 * set triggers an immediate bugcheck with the captured context.
 */
static volatile LONG KiArm64TrapActive[MAXIMUM_PROCESSORS] = {0};

static
VOID
KiArm64ReleaseWorkingSetsForBugCheck(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS Process = PsGetCurrentProcess();
    BOOLEAN Raised = FALSE;
    KIRQL PreviousIrql = KeGetCurrentIrql();

    if (PreviousIrql < APC_LEVEL)
    {
        KeRaiseIrql(APC_LEVEL, &PreviousIrql);
        Raised = TRUE;
    }

    if (Thread->OwnsSystemWorkingSetExclusive || Thread->OwnsSystemWorkingSetShared)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   "[arm64] KiArm64ReleaseWorkingSets: releasing system WS=%p thread=%p mutex=%p count=0x%llx\n",
                   &MmSystemCacheWs,
                   Thread,
                   &MmSystemCacheWs.WorkingSetMutex,
                   (unsigned long long)MmSystemCacheWs.WorkingSetMutex.Value);
        MiUnlockWorkingSet(Thread, &MmSystemCacheWs);
    }

    if ((Thread->OwnsSessionWorkingSetExclusive || Thread->OwnsSessionWorkingSetShared) &&
        (MmSessionSpace != NULL))
    {
        MiUnlockWorkingSet(Thread, &MmSessionSpace->GlobalVirtualAddress->Vm);
    }

    if (Thread->OwnsProcessWorkingSetExclusive || Thread->OwnsProcessWorkingSetShared)
    {
        if (Process != NULL)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_TRACE_LEVEL,
                       "[arm64] KiArm64ReleaseWorkingSets: releasing process WS thread=%p process=%p vm=%p mutex=%p count=0x%llx\n",
                       Thread,
                       Process,
                       &Process->Vm,
                       &Process->Vm.WorkingSetMutex,
                       (unsigned long long)Process->Vm.WorkingSetMutex.Value);
            MiUnlockProcessWorkingSetUnsafe(Process, Thread);
        }
    }

    if (Raised)
    {
        KeLowerIrql(PreviousIrql);
    }
}

static
VOID
KiArm64ResetDataAbortGuard(VOID)
{
    ULONG ProcessorIndex = KeGetCurrentProcessorNumber();

    if (ProcessorIndex < MAXIMUM_PROCESSORS)
    {
        InterlockedExchange(&KiArm64DataAbortOwner[ProcessorIndex], 0);
    }
}

static __inline VOID
KiArm64ClearTrapActive(VOID)
{
    ULONG ProcessorIndex = KeGetCurrentProcessorNumber();

    if (ProcessorIndex < MAXIMUM_PROCESSORS)
    {
        InterlockedExchange(&KiArm64TrapActive[ProcessorIndex], 0);
    }
}

#define KI_ARM64_ACCESS_READ    0
#define KI_ARM64_ACCESS_WRITE   1
#define KI_ARM64_ACCESS_EXECUTE 8

static __inline ULONG_PTR
KiArm64AccessTypeToExceptionInfo(
    _In_ BOOLEAN WriteAccess,
    _In_ BOOLEAN InstructionFetch)
{
    if (InstructionFetch)
    {
        return KI_ARM64_ACCESS_EXECUTE;
    }

    return WriteAccess ? KI_ARM64_ACCESS_WRITE : KI_ARM64_ACCESS_READ;
}

static
ULONG
KiArm64BuildFaultCode(
    _In_ ULONG FaultStatus,
    _In_ BOOLEAN WriteAccess,
    _In_ BOOLEAN InstructionFetch,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Code = 0;

    /* Prevent unused warnings for bring-up-only debug guards on GCC/MinGW. */
    if (0)
    {
        (void)KiArm64SyncExceptionLogBudget;
        KiArm64ReleaseWorkingSetsForBugCheck();
    }

    switch (FaultStatus & 0x3FULL)
    {
        case 0x00: /* Address size fault level 0 */
        case 0x01: /* Address size fault level 1 */
        case 0x02: /* Address size fault level 2 */
        case 0x03: /* Address size fault level 3 */
        case 0x04: /* Translation fault level 0 */
        case 0x05: /* Translation fault level 1 */
        case 0x06: /* Translation fault level 2 */
        case 0x07: /* Translation fault level 3 */
            /* Treat as not-present fault (bit 0 cleared) */
            break;

        default:
            Code |= 0x1; /* Present */
            break;
    }

    if (WriteAccess)
    {
        Code |= 0x2;
    }

    if (InstructionFetch)
    {
        Code |= 0x20;
    }

    if (PreviousMode == UserMode)
    {
        Code |= 0x4;
    }

    return Code;
}

static
VOID
KiArm64ReportUnhandledSyncException(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context,
    _In_ ULONG Esr)
{
    ULONG EsrClass = (Esr >> 26) & 0x3FULL;
    ULONG Iss = Esr & 0x01FFFFFFUL;

    /*
     * Log unhandled exception details for debugging.
     * ESR=0 typically means this is not a real synchronous exception or
     * the CPU/emulator doesn't properly set ESR for this exception type.
     *
     * Common ESR classes:
     *   0x00: Unknown/uncategorized
     *   0x07: SVE/SIMD/FP trap (CPACR)
     *   0x0E: Illegal execution state
     *   0x18: MSR/MRS trap
     *   0x20-21: Instruction abort
     *   0x24-25: Data abort
     *   0x2F: SError
     *   0x3C: BRK
     */
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[arm64] UNHANDLED SYNC EXCEPTION: ESR=0x%08lx (Class=0x%02lx ISS=0x%06lx)\n",
               Esr, EsrClass, Iss);
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[arm64]   ELR=%p FAR=%p VectorID=%lu\n",
               (PVOID)(ULONG_PTR)Context->State.Elr,
               (PVOID)(ULONG_PTR)Context->State.FaultAddress,
               (ULONG)Context->State.VectorId);
}

static volatile LONG KiArm64FirstCrashPrinted;

DECLSPEC_NORETURN
VOID
KiArm64BugCheckSynchronousException(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context)
{
    /*
     * Even if ELR is in low VA (e.g. NULL), proceed to a controlled
     * bugcheck with a reconstructed trap frame. Spinning here hides the
     * original fault and trips the watchdog.
     */
    KTRAP_FRAME TrapFrame;
    ULONG Esr = (ULONG)(Context->State.ExceptionSyndrome & 0xFFFFFFFFULL);
    ULONG EsrClass = (Esr >> 26) & 0x3FULL;
    BOOLEAN WriteAccess = (Esr & (1u << 6)) != 0;

    /* Reconstruct a trap frame so the bugcheck dump has architectural state. */
    KiArm64InitializeTrapFrame(Context, &TrapFrame);

    /*
     * Enable KD so "*** Fatal System Error" prints via DbgPrint in KeBugCheckWithTf.
     * Set KdPitchDebugger to prevent KD re-initialization attempts that could
     * cause faults during the bugcheck path. This ensures we get crash output
     * without risking infinite fault loops from KdEnableDebuggerWithLock.
     */
    KdDebuggerEnabled = TRUE;
    KdDebuggerNotPresent = TRUE;  /* No interactive debugger attached */
    KdPitchDebugger = TRUE;       /* Prevent KD re-init which can fault */

    /* Avoid touching working-set structures or pool during a hard stop. */
    KiArm64ResetDataAbortGuard();

    /* Print first crash info */
    if (InterlockedCompareExchange(&KiArm64FirstCrashPrinted, 1, 0) == 0)
    {
        KI_ARM64_STAGE_LOGF("[arm64] FirstCrash: vec=%lu esr=0x%lx elr=%p far=%p",
                           (ULONG)Context->State.VectorId,
                           (ULONG)Context->State.ExceptionSyndrome,
                           (PVOID)(ULONG_PTR)Context->State.Elr,
                           (PVOID)(ULONG_PTR)Context->State.FaultAddress);
    }

#ifdef KDBG
    /*
     * Call KDBG to display crash diagnostics before bugcheck.
     * This is the last safe point to show crash info.
     */
    {
        EXCEPTION_RECORD64 ExceptionRecord64;
        CONTEXT KdbContext;

        RtlZeroMemory(&ExceptionRecord64, sizeof(ExceptionRecord64));
        /* Determine exception code based on ESR class */
        if (EsrClass == 0x24 || EsrClass == 0x25)
        {
            ExceptionRecord64.ExceptionCode = STATUS_ACCESS_VIOLATION;
            ExceptionRecord64.NumberParameters = 2;
            ExceptionRecord64.ExceptionInformation[0] = WriteAccess ? 1 : 0;
            ExceptionRecord64.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;
        }
        else
        {
            ExceptionRecord64.ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
            ExceptionRecord64.NumberParameters = 0;
        }
        ExceptionRecord64.ExceptionFlags = 0;
        ExceptionRecord64.ExceptionAddress = Context->State.Elr;

        RtlZeroMemory(&KdbContext, sizeof(KdbContext));
        KdbContext.ContextFlags = CONTEXT_FULL | CONTEXT_ARM64;
        KeTrapFrameToContext(&TrapFrame, NULL, &KdbContext);

        KdbEnterDebuggerException(&ExceptionRecord64, KernelMode, &KdbContext, FALSE);
    }
#endif /* KDBG */

    KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN,
                     (ULONG_PTR)Context->State.VectorId,
                     (ULONG_PTR)Context->State.ExceptionSyndrome,
                     (ULONG_PTR)Context->State.FaultAddress,
                     (ULONG_PTR)Context->State.Elr,
                     &TrapFrame);

    /* ARM64: __builtin_unreachable() generates trap instruction, avoid it */
    while (1) { }
}


BOOLEAN
KiArm64HandleSynchronousException(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context)
{
    ULONG Esr = (ULONG)(Context->State.ExceptionSyndrome & 0xFFFFFFFFULL);
    ULONG EsrClass = (Esr >> 26) & 0x3FULL;
    ULONG Iss = Esr & 0x01FFFFFFUL;
    ULONG FaultStatus = Iss & 0x3FULL;
    PKTRAP_FRAME TrapFrame;
    KPROCESSOR_MODE PreviousMode;
    NTSTATUS Status;
    BOOLEAN WriteAccess;

    /* Log ALL synchronous exceptions to diagnose illegal instruction */
    if (EsrClass != 0x15 && EsrClass != 0x11) /* Skip logging for SVC */
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "[arm64] SyncException: Class=0x%02lx ESR=0x%08lx ELR=%p FAR=%p\n",
                   EsrClass, Esr,
                   (PVOID)(ULONG_PTR)Context->State.Elr,
                   (PVOID)(ULONG_PTR)Context->State.FaultAddress);
    }

    /*
     * ARM64 memory model: Ensure exception state from assembly is visible.
     * The DSB/ISB in assembly ensures system register reads are complete,
     * but we need a compiler barrier to prevent C code reordering.
     */
    __asm__ __volatile__("" ::: "memory");

    Context->TrapFramePointer = NULL;
    Context->ExceptionFramePointer = NULL;

    /* One-shot guard to avoid recursive exception storms while KD/logging
     * is not fully reliable during bring-up. Do this before any further logging. */
    {
        ULONG CpuIndex = KeGetCurrentProcessorNumber();
        if (CpuIndex < MAXIMUM_PROCESSORS)
        {
            if (InterlockedCompareExchange(&KiArm64TrapActive[CpuIndex], 1, 0) != 0)
            {
                KiArm64BugCheckSynchronousException(Context);
                return TRUE; /* not reached */
            }
        }
    }

    switch (EsrClass)
    {
        case 0x11: /* SVC from lower EL */
        case 0x15: /* SVC from same EL */
        {
            ULONG ServiceNumber;
            ULONG TableIndexShifted;
            ULONG Instruction;
            PKTHREAD Thread;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            /*
             * For ARM64 system calls, X8 carries the service number while the
             * SVC immediate selects the service table.  The current stubs emit
             * SVC #0 (NT table), but keep the bits so KiSystemService can grow
             * into additional tables later on.
             */
            ServiceNumber = (ULONG)(TrapFrame->X[8] & SERVICE_NUMBER_MASK);
            TableIndexShifted = (((ULONG)(Iss & 0xFFFF)) << SERVICE_TABLE_SHIFT) & SERVICE_TABLE_MASK;
            Instruction = TableIndexShifted | ServiceNumber;

            /*
             * ARM64 FIX: Clear trap-active BEFORE calling KiSystemService.
             *
             * System calls can legitimately call nested system calls (e.g.,
             * NtCreateSymbolicLinkObject calls ObInsertObject which calls
             * other Nt* functions via Zw* wrappers). Each Zw* wrapper uses
             * SVC #0 to enter the kernel, even when already in kernel mode.
             *
             * If we don't clear the trap flag before KiSystemService, the
             * nested SVC will see KiArm64TrapActive as set and incorrectly
             * treat it as a recursive exception, causing a spurious crash.
             *
             * This is safe because:
             * 1. The trap frame is already initialized
             * 2. KiSystemService handles its own exception safety
             * 3. Any real fault during the syscall will set its own flag
             */
            KiArm64ClearTrapActive();

            Thread = KeGetCurrentThread();
            KiSystemService(Thread, TrapFrame, Instruction);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            return TRUE;
        }

        case 0x20: /* Instruction abort, lower EL */
        case 0x21: /* Instruction abort, same EL  */
        {
            EXCEPTION_RECORD ExceptionRecord;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            PreviousMode = KiArm64PreviousModeFromSpsr(Context->State.Spsr);
            WriteAccess = FALSE;

            /*
             * Use MmAccessFault (not MmArmAccessFault) to properly dispatch
             * the fault to the correct handler. MmAccessFault routes faults
             * to MmNotPresentFaultSectionView for ROS section views (like
             * VACB buffers) or MmArmAccessFault for ARM3 allocations.
             */
            Status = MmAccessFault(KiArm64BuildFaultCode(FaultStatus,
                                                         WriteAccess,
                                                         TRUE,
                                                         PreviousMode),
                                   (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                                   PreviousMode,
                                   TrapFrame);

            if (NT_SUCCESS(Status))
            {
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            if ((PreviousMode == KernelMode) && (!KdDebuggerEnabled || KdDebuggerNotPresent))
            {
                KiArm64BugCheckSynchronousException(Context);
                /* not reached */
            }

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_ACCESS_VIOLATION;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 2;
            ExceptionRecord.ExceptionInformation[0] = KiArm64AccessTypeToExceptionInfo(WriteAccess, TRUE);
            ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                KiArm64ReportUnhandledSyncException(Context, Esr);

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                PreviousMode,
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        case 0x24: /* Data abort, lower EL */
        case 0x25: /* Data abort, same EL  */
        {
            PETHREAD CurrentThread;
            ULONG64 CurrentSp;
            PVOID StackLimit;

#if DBG
            /* Debug: Detect repeated faults on the same address */
            static volatile PVOID LastFaultAddress = NULL;
            static volatile LONG LastFaultCount = 0;
            PVOID CurrentFaultAddr = (PVOID)(ULONG_PTR)Context->State.FaultAddress;

            if (CurrentFaultAddr == LastFaultAddress)
            {
                LONG Count = InterlockedIncrement(&LastFaultCount);
                if (Count >= 2 && Count <= 5)
                {
                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                               "[arm64] DA REPEATED FAULT #%ld: addr=%p elr=%p\n",
                               Count, CurrentFaultAddr, (PVOID)(ULONG_PTR)Context->State.Elr);
                }
                else if (Count > 10)
                {
                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                               "[arm64] DA FAULT LOOP DETECTED: addr=%p count=%ld - BUGCHECK!\n",
                               CurrentFaultAddr, Count);
                    KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                                 (ULONG_PTR)CurrentFaultAddr,
                                 (ULONG_PTR)Context->State.Elr,
                                 Count,
                                 0xFA017100UL);
                }
            }
            else
            {
                LastFaultAddress = CurrentFaultAddr;
                InterlockedExchange(&LastFaultCount, 1);
            }
#endif

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            /* Check for stack exhaustion early */
            __asm__ __volatile__("mov %0, sp" : "=r"(CurrentSp));
            CurrentThread = PsGetCurrentThread();
            StackLimit = (PVOID)CurrentThread->Tcb.StackLimit;

            if (CurrentSp < (ULONG64)StackLimit + 0x800)
            {
                /* Stack exhausted - bugcheck before we overflow */
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                           "[arm64] DataAbort: STACK EXHAUSTED! SP=%p StackLimit=%p FAR=%p\n",
                           (PVOID)CurrentSp, StackLimit, (PVOID)(ULONG_PTR)Context->State.FaultAddress);
                KeBugCheckEx(KERNEL_STACK_INPAGE_ERROR,
                             CurrentSp,
                             (ULONG_PTR)StackLimit,
                             Context->State.FaultAddress,
                             0xDEAD5743);
            }

            /*
             * Verbose data abort logging removed - demand paging is working correctly.
             * The logging for user/kernel data aborts and System View Space access
             * was for bring-up debugging and is no longer needed.
             */

            PreviousMode = KiArm64PreviousModeFromSpsr(Context->State.Spsr);
            WriteAccess = (Iss & (1u << 6)) != 0;

            /* ARM64: skip accessed-bit fast path to avoid dereferencing
             * an unmapped PTE alias during bring-up. Fallback to the
             * general fault handler below. */

            {
                ULONG ProcessorIndex = KeGetCurrentProcessorNumber();
                BOOLEAN OwnsAbortGuard = FALSE;
#if DBG && defined(ARM64_TRAP_TRACE)
                LONG GuardSnapshot = -1;
#endif

#if DBG
                /* BUG #15: Log ALL data aborts to low user addresses */
                if ((ULONG64)Context->State.FaultAddress < 0x100000ULL)
                {
                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                               "[arm64] DA LOW USER: esr=0x%lx far=%p elr=%p write=%d mode=%d\n",
                               Esr,
                               (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                               (PVOID)(ULONG_PTR)Context->State.Elr,
                               WriteAccess,
                               PreviousMode);
                }
#endif

                /* Keep logging minimal in trap path to avoid reentry */
#if DBG && defined(ARM64_TRAP_TRACE)
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                           "[arm64] DA: esr=0x%lx far=%p elr=%p cpu=%lu guard=%ld\n",
                           Esr,
                           (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                           (PVOID)(ULONG_PTR)Context->State.Elr,
                           ProcessorIndex,
                           GuardSnapshot);
#endif

                if (ProcessorIndex < MAXIMUM_PROCESSORS)
                {
                    OwnsAbortGuard = (InterlockedCompareExchange(&KiArm64DataAbortOwner[ProcessorIndex],
                                                                  1,
                                                                  0) == 0);
                    if (!OwnsAbortGuard)
                    {
                        /* Nested data abort - always log this */
                        PVOID LrPointer = (PVOID)(ULONG_PTR)Context->State.Registers.X[30];
                        PVOID SpPointer = (PVOID)(ULONG_PTR)Context->State.Registers.Sp;

                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DA NESTED: esr=0x%lx far=%p elr=%p lr=%p sp=%p cpu=%lu\n",
                                   Esr,
                                   (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                                   (PVOID)(ULONG_PTR)Context->State.Elr,
                                   LrPointer,
                                   SpPointer,
                                   ProcessorIndex);
                        /* Nested abort detected - bugcheck to prevent infinite loop */
                        KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                                     (ULONG_PTR)Context->State.FaultAddress,
                                     (ULONG_PTR)Context->State.Elr,
                                     (ULONG_PTR)LrPointer,
                                     0xDA0DEAD);
                    }
                }

                {
                    ULONG FaultCodeArg = KiArm64BuildFaultCode(FaultStatus, WriteAccess, FALSE, PreviousMode);
                    PVOID AddressArg = (PVOID)(ULONG_PTR)Context->State.FaultAddress;
                    extern volatile LONG MmArmAccessFaultEntryCount;
                    extern volatile PVOID MmArmAccessFaultLastAddress;
                    extern volatile LONG MmArmAccessFaultInFunction;
                    LONG CountBefore = MmArmAccessFaultEntryCount;
                    LONG InFunctionBefore = MmArmAccessFaultInFunction;

                    /* Log for user-mode addresses that keep faulting */
                    if ((ULONG64)AddressArg >= 0x000007FFB7000000ULL &&
                        (ULONG64)AddressArg < 0x000007FFB8000000ULL)
                    {
                        static volatile LONG UserFaultLogBudget = 50;
                        if (UserFaultLogBudget > 0)
                        {
                            InterlockedDecrement(&UserFaultLogBudget);
                            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                       "[arm64] UserDA: ESR=0x%lx DFSC=0x%lx FAR=%p FaultCode=0x%lx write=%d mode=%d\n",
                                       Esr, FaultStatus, AddressArg, FaultCodeArg, WriteAccess, PreviousMode);
                        }
                    }

                    /* Log for low user-space addresses (ProcessParams area) - BUG #15 */
                    if ((ULONG64)AddressArg >= 0x30000ULL &&
                        (ULONG64)AddressArg < 0x50000ULL)
                    {
                        static volatile LONG LowUserFaultLogBudget = 20;
                        if (LowUserFaultLogBudget > 0)
                        {
                            InterlockedDecrement(&LowUserFaultLogBudget);
                            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                       "[arm64] LowUserDA: ESR=0x%lx DFSC=0x%lx FAR=%p FaultCode=0x%lx write=%d mode=%d\n",
                                       Esr, FaultStatus, AddressArg, FaultCodeArg, WriteAccess, PreviousMode);
                        }
                    }

                    /* Only log for specific problem address to reduce output */
                    if ((ULONG64)AddressArg >= 0xFFFF8000BCC00000ULL &&
                        (ULONG64)AddressArg < 0xFFFF8000BD000000ULL)
                    {
                        ULONG64 CurrentSp;
                        __asm__ __volatile__("mov %0, sp" : "=r"(CurrentSp));
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DataAbort: VACB far=%p SP=%p Count=%ld InFunc=%ld\n",
                                   AddressArg, (PVOID)CurrentSp, CountBefore, InFunctionBefore);
                        /* Clear InFunction before call */
                        InterlockedExchange(&MmArmAccessFaultInFunction, 0);
                    }

                    /*
                     * Use MmAccessFault (not MmArmAccessFault) to properly dispatch
                     * the fault to the correct handler. MmAccessFault routes faults
                     * to MmNotPresentFaultSectionView for ROS section views (like
                     * VACB buffers) or MmArmAccessFault for ARM3 allocations.
                     */
                    Status = MmAccessFault(FaultCodeArg, AddressArg, PreviousMode, TrapFrame);

                    /* Log after call only for the problem address */
                    if ((ULONG64)AddressArg >= 0xFFFF8000BCC00000ULL &&
                        (ULONG64)AddressArg < 0xFFFF8000BD000000ULL)
                    {
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DataAbort: VACB returned Status=0x%lx Count=%ld InFunc=%ld Addr=%p\n",
                                   Status, MmArmAccessFaultEntryCount, MmArmAccessFaultInFunction, MmArmAccessFaultLastAddress);
                    }
                }

                if (OwnsAbortGuard)
                {
                    InterlockedExchange(&KiArm64DataAbortOwner[ProcessorIndex], 0);
                }

#if DBG && defined(ARM64_TRAP_TRACE)
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                           "[arm64] DA exit: status=0x%lx cpu=%lu\n",
                           Status,
                           ProcessorIndex);
#endif
            }

            if (NT_SUCCESS(Status))
            {
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            /* Not resolved by Mm - this is an unhandled data abort. */

#ifdef KDBG
            /*
             * Call KDBG to display crash diagnostics (registers, stack trace,
             * modules) for kernel-mode faults. This runs regardless of KD state
             * since KDBG provides valuable crash info even when KD is "enabled"
             * for serial output but no interactive debugger is attached.
             */
            if (PreviousMode == KernelMode)
            {
                EXCEPTION_RECORD64 ExceptionRecord64;
                CONTEXT KdbContext;

                RtlZeroMemory(&ExceptionRecord64, sizeof(ExceptionRecord64));
                ExceptionRecord64.ExceptionCode = STATUS_ACCESS_VIOLATION;
                ExceptionRecord64.ExceptionFlags = 0;
                ExceptionRecord64.ExceptionAddress = Context->State.Elr;
                ExceptionRecord64.NumberParameters = 2;
                ExceptionRecord64.ExceptionInformation[0] = KiArm64AccessTypeToExceptionInfo(WriteAccess, FALSE);
                ExceptionRecord64.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

                RtlZeroMemory(&KdbContext, sizeof(KdbContext));
                KdbContext.ContextFlags = CONTEXT_FULL | CONTEXT_ARM64;

                KeTrapFrameToContext(TrapFrame, Context->ExceptionFramePointer, &KdbContext);

                KdbEnterDebuggerException(&ExceptionRecord64,
                                          PreviousMode,
                                          &KdbContext,
                                          TRUE);
            }
#endif /* KDBG */

            /* If we're in kernel mode and no debugger is attached, bugcheck
             * immediately to avoid recursive faults while trying to log/dispatch.
             * This mirrors amd64 behavior when KD is unavailable during early boot. */
            if ((PreviousMode == KernelMode) && (!KdDebuggerEnabled || KdDebuggerNotPresent))
            {
                KiArm64BugCheckSynchronousException(Context);
                /* not reached */
            }

            /* Otherwise, dispatch an access violation through KiDispatchException
             * so KD can catch first/second chance and print the crash context. */
            {
                EXCEPTION_RECORD ExceptionRecord;
                RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
                ExceptionRecord.ExceptionCode = STATUS_ACCESS_VIOLATION;
                ExceptionRecord.ExceptionFlags = 0;
                ExceptionRecord.ExceptionRecord = NULL;
                ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
                ExceptionRecord.NumberParameters = 2;
                ExceptionRecord.ExceptionInformation[0] = KiArm64AccessTypeToExceptionInfo(WriteAccess, FALSE);
                ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

                KiDispatchException(&ExceptionRecord,
                                    Context->ExceptionFramePointer,
                                    TrapFrame,
                                    PreviousMode,
                                    TRUE);

                /* Return with updated trap frame (either resumed, or KD/bugcheck handled). */
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }
        }

        case 0x22: /* PC alignment fault */
        case 0x26: /* SP alignment fault */
        {
            EXCEPTION_RECORD ExceptionRecord;
            KPROCESSOR_MODE Mode;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            Mode = KiArm64PreviousModeFromSpsr(Context->State.Spsr);

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 0;

            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                KiArm64ReportUnhandledSyncException(Context, Esr);

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                Mode,
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        case 0x2F: /* SError */
        {
            EXCEPTION_RECORD ExceptionRecord;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_HARDWARE_MEMORY_ERROR;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 0;

            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                KiArm64ReportUnhandledSyncException(Context, Esr);

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                KiArm64PreviousModeFromSpsr(Context->State.Spsr),
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        case 0x3C: /* BRK instruction */
        {
            KPROCESSOR_MODE Mode = KiArm64PreviousModeFromSpsr(Context->State.Spsr);
#ifdef KDBG
            KD_CONTINUE_TYPE KdbResult = kdHandleException;
#endif

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

#ifdef KDBG
            /*
             * When KDBG is compiled in, call KdbEnterDebuggerException to
             * handle the breakpoint interactively. KDBG provides a built-in
             * debugger that works even when no external debugger is attached.
             *
             * If KdbEnterDebuggerException returns kdContinue, the breakpoint
             * was handled and we should skip past the BRK instruction.
             * If it returns kdHandleException, we need to dispatch through
             * the normal exception path for an external debugger.
             */
            {
                EXCEPTION_RECORD64 ExceptionRecord64;
                CONTEXT KdbContext;

                /* Build EXCEPTION_RECORD64 for KDBG */
                RtlZeroMemory(&ExceptionRecord64, sizeof(ExceptionRecord64));
                ExceptionRecord64.ExceptionCode = STATUS_BREAKPOINT;
                ExceptionRecord64.ExceptionFlags = 0;
                ExceptionRecord64.ExceptionAddress = Context->State.Elr;
                ExceptionRecord64.NumberParameters = 1;
                ExceptionRecord64.ExceptionInformation[0] = Context->State.Elr;

                /* Build CONTEXT from trap frame for KDBG */
                RtlZeroMemory(&KdbContext, sizeof(KdbContext));
                KdbContext.ContextFlags = CONTEXT_FULL | CONTEXT_ARM64;
                KeTrapFrameToContext(TrapFrame, Context->ExceptionFramePointer, &KdbContext);

                /* Call KDBG to handle the breakpoint */
                KdbResult = KdbEnterDebuggerException(&ExceptionRecord64,
                                                      Mode,
                                                      &KdbContext,
                                                      TRUE);

                /*
                 * If KDBG handled the breakpoint (kdContinue), propagate any
                 * PC changes from KdbContext back to TrapFrame. KDBG may have
                 * advanced PC past the BRK instruction.
                 */
                if (KdbResult == kdContinue)
                {
                    KeContextToTrapFrame(&KdbContext,
                                         Context->ExceptionFramePointer,
                                         TrapFrame,
                                         KdbContext.ContextFlags,
                                         Mode);

                    /*
                     * Ensure PC is advanced past the BRK instruction.
                     * KdbEnterDebuggerException should have done this, but
                     * verify and fix if needed to prevent infinite loops.
                     */
                    if (TrapFrame->Pc == Context->State.Elr)
                    {
                        TrapFrame->Pc += 4;
                    }
                    Context->State.Elr = TrapFrame->Pc;
                    Context->TrapFramePointer = TrapFrame;
                    Context->ExceptionFramePointer = &Context->ExceptionFrame;
                    KiArm64ClearTrapActive();
                    return TRUE;
                }
            }
#endif /* KDBG */

            /*
             * KDBG did not handle the breakpoint (or KDBG is not compiled in).
             * Check if we should skip the breakpoint or dispatch to an external
             * debugger.
             */
            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
            {
                /* No external debugger - skip the BRK instruction */
                TrapFrame->Pc = (ULONG64)((ULONG_PTR)Context->State.Elr + 4);
                Context->State.Elr += 4;
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            /* External debugger attached - dispatch through normal path */
            {
                EXCEPTION_RECORD ExceptionRecord;

                RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
                ExceptionRecord.ExceptionCode = STATUS_BREAKPOINT;
                ExceptionRecord.ExceptionFlags = 0;
                ExceptionRecord.ExceptionRecord = NULL;
                ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
                ExceptionRecord.NumberParameters = 1;
                ExceptionRecord.ExceptionInformation[0] = (ULONG_PTR)Context->State.Elr;

                KI_ARM64_STAGE_LOGF("[arm64] TrapDiag: forwarding BRK to KD esr=0x%lx elr=%p",
                                    Esr,
                                    (PVOID)(ULONG_PTR)Context->State.Elr);

                KiDispatchException(&ExceptionRecord,
                                    Context->ExceptionFramePointer,
                                    TrapFrame,
                                    Mode,
                                    TRUE);
            }

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        default:
        {
            EXCEPTION_RECORD ExceptionRecord;
            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            /*
             * ALWAYS log unhandled exceptions for debugging, regardless of KD state.
             * This is critical for diagnosing early boot crashes where KD may not
             * be fully initialized or responsive.
             */
            KiArm64ReportUnhandledSyncException(Context, Esr);

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 0;

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                KiArm64PreviousModeFromSpsr(Context->State.Spsr),
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }
    }
}
