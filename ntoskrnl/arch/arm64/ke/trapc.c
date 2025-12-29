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
        KiArm64BootStageLog(Buffer);
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

    RtlZeroMemory(TrapFrame, sizeof(*TrapFrame));

    TrapFrame->PreviousMode = (CHAR)KiArm64PreviousModeFromSpsr(Context->State.Spsr);
    TrapFrame->PreviousIrql = KeGetCurrentIrql();
    TrapFrame->TrapFrame = (ULONG64)(ULONG_PTR)TrapFrame;
    TrapFrame->FaultAddress = Context->State.FaultAddress;
    TrapFrame->Spsr = (ULONG)Context->State.Spsr;
    TrapFrame->Esr = (ULONG)Context->State.ExceptionSyndrome;
    TrapFrame->Sp = Context->State.Registers.Sp;
    TrapFrame->Pc = Context->State.Registers.Pc;
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
#if defined(_M_ARM64) || defined(__aarch64__)
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   "[arm64] KiArm64ReleaseWorkingSets: releasing system WS=%p thread=%p mutex=%p count=0x%llx\n",
                   &MmSystemCacheWs,
                   Thread,
                   &MmSystemCacheWs.WorkingSetMutex,
                   (unsigned long long)MmSystemCacheWs.WorkingSetMutex.Value);
#endif
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
#if defined(_M_ARM64) || defined(__aarch64__)
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_TRACE_LEVEL,
                       "[arm64] KiArm64ReleaseWorkingSets: releasing process WS thread=%p process=%p vm=%p mutex=%p count=0x%llx\n",
                       Thread,
                       Process,
                       &Process->Vm,
                       &Process->Vm.WorkingSetMutex,
                       (unsigned long long)Process->Vm.WorkingSetMutex.Value);
#endif
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
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Esr);
    /*
     * Temporarily suppress verbose trap logging to avoid re-entrancy while
     * the early ARM64 serial path and stage logger are still being hardened.
     * KD (when attached) will get full state via KiDispatchException.
     */
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
    /* If ELR is in very low VA, proceed straight to bugcheck; avoid any
       extra logging that could re-enter during early boot. */
    (void)Context;

    KTRAP_FRAME TrapFrame;

    /* Reconstruct a trap frame so the bugcheck dump has architectural state. */
    KiArm64InitializeTrapFrame(Context, &TrapFrame);

    /* Emit a concise, recognizable banner akin to other architectures */
    /* No early prints here; KDBG/KD will report the exception. */
#if defined(_M_ARM64) || defined(__aarch64__)
    /* Switch KD to passive mode; do not emit any trap dump here to avoid
     * re-entering the fault path during early boot. */
    KdDebuggerEnabled = FALSE;
    KdDebuggerNotPresent = TRUE;
    SharedUserData->KdDebuggerEnabled = FALSE;
#endif

    /* Avoid touching working-set structures or pool during a hard stop. */
    KiArm64ResetDataAbortGuard();

    /* Print a single compact first-crash line to help locate faults */
    if (InterlockedCompareExchange(&KiArm64FirstCrashPrinted, 1, 0) == 0)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[arm64] FirstCrash: vec=%lu esr=0x%lx elr=%p far=%p\n",
                   (ULONG)Context->State.VectorId,
                   (ULONG)Context->State.ExceptionSyndrome,
                   (PVOID)(ULONG_PTR)Context->State.Elr,
                   (PVOID)(ULONG_PTR)Context->State.FaultAddress);
    }

    KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN,
                     (ULONG_PTR)Context->State.VectorId,
                     (ULONG_PTR)Context->State.ExceptionSyndrome,
                     (ULONG_PTR)Context->State.FaultAddress,
                     (ULONG_PTR)Context->State.Elr,
                     &TrapFrame);

    __builtin_unreachable();
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

    Context->TrapFramePointer = NULL;
    Context->ExceptionFramePointer = NULL;

    /* Fast path: kernel BRK with no debugger attached —
     * skip the breakpoint to keep boot moving (amd64 parity). */
    {
        KPROCESSOR_MODE FastMode = KiArm64PreviousModeFromSpsr(Context->State.Spsr);
        if ((EsrClass == 0x3C) && (FastMode == KernelMode) && (!KdDebuggerEnabled || KdDebuggerNotPresent))
        {
            /* Advance ELR by 4 bytes to step over BRK. */
            Context->State.Elr += 4;
            return TRUE;
        }
    }

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

#if defined(_M_ARM64) || defined(__aarch64__)
    if ((EsrClass != 0x11) && (EsrClass != 0x15) && (EsrClass != 0x3C))
    {
        KI_ARM64_STAGE_LOGF("[arm64] TrapDiag: KiArm64HandleSync class=0x%lx esr=0x%lx far=%p elr=%p vector=%lu",
                            (ULONG)EsrClass,
                            (ULONG)Esr,
                            (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                            (PVOID)(ULONG_PTR)Context->State.Elr,
                            (ULONG)Context->State.VectorId);
    }
#endif

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

            Thread = KeGetCurrentThread();
            KiSystemService(Thread, TrapFrame, Instruction);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
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

            Status = MmArmAccessFault(KiArm64BuildFaultCode(FaultStatus,
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
            ExceptionRecord.ExceptionCode = KI_EXCEPTION_ACCESS_VIOLATION;
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
            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            PreviousMode = KiArm64PreviousModeFromSpsr(Context->State.Spsr);
            WriteAccess = (Iss & (1u << 6)) != 0;

            /* ARM64: skip accessed-bit fast path to avoid dereferencing
             * an unmapped PTE alias during bring-up. Fallback to the
             * general fault handler below. */

            {
                ULONG ProcessorIndex = KeGetCurrentProcessorNumber();
                LONG GuardSnapshot = -1;
                BOOLEAN OwnsAbortGuard = FALSE;

                if (ProcessorIndex < MAXIMUM_PROCESSORS)
                {
                    GuardSnapshot = KiArm64DataAbortOwner[ProcessorIndex];
                }

                /* Keep logging minimal in trap path to avoid reentry */
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                           "[arm64] DA: esr=0x%lx far=%p elr=%p cpu=%lu guard=%ld\n",
                           Esr,
                           (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                           (PVOID)(ULONG_PTR)Context->State.Elr,
                           ProcessorIndex,
                           GuardSnapshot);

                if (ProcessorIndex < MAXIMUM_PROCESSORS)
                {
                    OwnsAbortGuard = (InterlockedCompareExchange(&KiArm64DataAbortOwner[ProcessorIndex],
                                                                  1,
                                                                  0) == 0);
                    if (!OwnsAbortGuard)
                    {
                        PVOID LrPointer = (PVOID)(ULONG_PTR)Context->State.Registers.X[30];
                        PVOID SpPointer = (PVOID)(ULONG_PTR)Context->State.Registers.Sp;

                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DA nested: esr=0x%lx far=%p elr=%p lr=%p sp=%p cpu=%lu\n",
                                   Esr,
                                   (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                                   (PVOID)(ULONG_PTR)Context->State.Elr,
                                   LrPointer,
                                   SpPointer,
                                   ProcessorIndex);
                    }
                }

                Status = MmArmAccessFault(KiArm64BuildFaultCode(FaultStatus,
                                                                 WriteAccess,
                                                                 FALSE,
                                                                 PreviousMode),
                                           (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                                           PreviousMode,
                                           TrapFrame);

                if (OwnsAbortGuard)
                {
                    InterlockedExchange(&KiArm64DataAbortOwner[ProcessorIndex], 0);
                }

                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                           "[arm64] DA exit: status=0x%lx cpu=%lu\n",
                           Status,
                           ProcessorIndex);
            }

            if (NT_SUCCESS(Status))
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                           "[arm64] DA resolved: 0x%lx\n", Status);
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            /* Not resolved by Mm. If we're in kernel mode and no debugger is
             * attached, bugcheck immediately to avoid recursive faults while
             * trying to log/dispatch. This mirrors amd64 behavior when KD is
             * unavailable during early boot. */
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
                ExceptionRecord.ExceptionCode = KI_EXCEPTION_ACCESS_VIOLATION;
                ExceptionRecord.ExceptionFlags = 0;
                ExceptionRecord.ExceptionRecord = NULL;
                ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
                ExceptionRecord.NumberParameters = 2;
                ExceptionRecord.ExceptionInformation[0] = KiArm64AccessTypeToExceptionInfo(WriteAccess, FALSE);
                ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

                if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                    KiArm64ReportUnhandledSyncException(Context, Esr);

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
            /* If no debugger is attached, mirror amd64 behavior by skipping
             * kernel-mode breakpoints instead of trapping into KD/bugcheck. */
            KPROCESSOR_MODE Mode = KiArm64PreviousModeFromSpsr(Context->State.Spsr);
            if ((!KdDebuggerEnabled || KdDebuggerNotPresent) && (Mode == KernelMode))
            {
                TrapFrame = &Context->TrapFrame;
                KiArm64InitializeTrapFrame(Context, TrapFrame);
                TrapFrame->Pc = (ULONG64)((ULONG_PTR)Context->State.Elr + 4);
                Context->State.Elr += 4;
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            EXCEPTION_RECORD ExceptionRecord;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_BREAKPOINT;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 1;
            ExceptionRecord.ExceptionInformation[0] = (ULONG_PTR)Context->State.Elr;

            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
            {
                KiArm64ReportUnhandledSyncException(Context, Esr);
            }
            else
            {
                KI_ARM64_STAGE_LOGF("[arm64] TrapDiag: forwarding BRK to KD esr=0x%lx elr=%p",
                                    Esr,
                                    (PVOID)(ULONG_PTR)Context->State.Elr);
            }

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

        default:
        {
            EXCEPTION_RECORD ExceptionRecord;
            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
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
    }
}
