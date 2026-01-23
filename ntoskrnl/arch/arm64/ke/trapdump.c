/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/trapdump.c
 * PURPOSE:         Rich diagnostics for early ARM64 exceptions
 */

#include <ntoskrnl.h>
#include <ntstrsafe.h>
#include <string.h>
#define NDEBUG
#include <debug.h>
#include <arm64trap.h>
#include <mm/ARM3/miarm.h>
#include <pseh/pseh2.h>

extern BOOLEAN KdDebuggerEnabled;
extern BOOLEAN KdDebuggerNotPresent;
#define KI_ARM64_MIN_KERNEL_ADDRESS 0xFFFF000000000000ULL


extern ULONG ExpPoolFlags;
extern POOL_DESCRIPTOR NonPagedPoolDescriptor;
extern PPOOL_DESCRIPTOR PoolVector[2];
/* Selected refptrs to validate at trap time (object manager globals) */
extern volatile ULONG * const ObpLUIDDeviceMapsEnabledPtr __asm__(".refptr.ObpLUIDDeviceMapsEnabled");
extern volatile PVOID  * const ObpNameBufferLookasideListPtr __asm__(".refptr.ObpNameBufferLookasideList");
extern volatile ULONG * const ObpObjectSecurityModePtr __asm__(".refptr.ObpObjectSecurityMode");
extern volatile ULONG * const ObpProtectionModePtr __asm__(".refptr.ObpProtectionMode");
volatile ULONG MiArm64LastFaultIrqlEntry;
volatile ULONG MiArm64LastFaultIrqlRaised;
volatile ULONG MiArm64LastFaultIrqlAfterDispatch;
volatile ULONG MiArm64LastFaultIrqlBeforeUnlock;
volatile ULONG MiArm64LastFaultIrqlAfterLower;
volatile ULONG MiArm64LastFaultStatus;
volatile LONG MiArm64LastSpecialApcDisableEntry;
volatile LONG MiArm64LastKernelApcDisableEntry;
volatile LONG MiArm64LastSpecialApcDisableBeforeUnlock;
volatile LONG MiArm64LastKernelApcDisableBeforeUnlock;
volatile ULONG MiArm64LastFaultPathFlags;
volatile ULONG MiArm64LastFaultLockIrql;
volatile LONG MiArm64LastSpecialApcDisableAfterUnlock;
volatile LONG MiArm64LastKernelApcDisableAfterUnlock;
volatile ULONG MiArm64LastFaultPfnOldIrql;
volatile ULONG MiArm64LastFaultPfnNewIrql;
volatile ULONG MiArm64LastFaultPfnReleaseIrql;
volatile ULONG MiArm64LastFaultPfnAfterReleaseIrql;
volatile ULONG_PTR MiArm64LastFaultPfnThread;
volatile ULONG_PTR MiArm64LastFaultPfnCaller;
volatile ULONG MiArm64LastGuardedLeaveIrql;
volatile ULONG_PTR MiArm64LastGuardedLeaveThread;
volatile LONG MiArm64LastGuardedLeaveSpecial;
volatile LONG MiArm64LastGuardedLeaveKernel;
volatile ULONG MiArm64LastGuardedAssertFlags;
volatile ULONG MiArm64LastGuardedAssertIrql;
volatile ULONG_PTR MiArm64LastGuardedAssertThread;
volatile ULONG_PTR MiArm64LastGuardedAssertCaller;
volatile LONG MiArm64LastGuardedAssertSpecial;
volatile LONG MiArm64LastGuardedAssertKernel;
volatile ULONG MiArm64LastMdlFreeFlags;
volatile ULONG MiArm64LastMdlFreeIrql;
volatile LONG MiArm64LastMdlFreeSpecial;
volatile LONG MiArm64LastMdlFreeKernel;
volatile ULONG_PTR MiArm64LastMdlFreeThread;
volatile ULONG_PTR MiArm64LastMdlFreeEThread;
volatile ULONG_PTR MiArm64LastMdlFreeCaller;
volatile ULONG_PTR MiArm64LastMdlFreeCaller2;
volatile ULONG_PTR MiArm64LastMdlFreeMdl;
volatile ULONG MiArm64LastIrqlRaiseFrom;
volatile ULONG MiArm64LastIrqlRaiseTo;
volatile ULONG MiArm64LastIrqlLowerFrom;
volatile ULONG MiArm64LastIrqlLowerTo;
volatile ULONG_PTR MiArm64LastIrqlRaiseCaller;
volatile ULONG_PTR MiArm64LastIrqlLowerCaller;
volatile ULONG_PTR MiArm64LastIrqlRaiseThread;
volatile ULONG_PTR MiArm64LastIrqlLowerThread;
volatile LONG MiArm64IrqlTraceBudget = 64;
volatile LONG MiArm64TrapTraceIndex;
volatile ULONG64 MiArm64TrapTraceElr[4];
volatile ULONG64 MiArm64TrapTraceFar[4];
volatile ULONG64 MiArm64TrapTraceEsr[4];
volatile ULONG64 MiArm64TrapTraceSpsr[4];
volatile ULONG64 MiArm64TrapTraceVector[4];
volatile ULONG64 MiArm64TrapTraceX16[4];
volatile ULONG64 MiArm64TrapTraceX17[4];
volatile ULONG64 MiArm64TrapTraceX0[4];
volatile ULONG64 MiArm64TrapTraceX1[4];
volatile ULONG64 MiArm64TrapTraceX8[4];
volatile ULONG64 MiArm64TrapTraceX9[4];
volatile ULONG64 MiArm64TrapTraceX20[4];
volatile ULONG64 MiArm64TrapTraceX21[4];
/* Pointers to our own guarded-leave diagnostics via .refptr */
extern volatile ULONG * const MiArm64LastGuardedLeaveIrqlPtr __asm__(".refptr.MiArm64LastGuardedLeaveIrql");
extern volatile ULONG_PTR * const MiArm64LastGuardedLeaveThreadPtr __asm__(".refptr.MiArm64LastGuardedLeaveThread");
extern volatile LONG * const MiArm64LastGuardedLeaveSpecialPtr __asm__(".refptr.MiArm64LastGuardedLeaveSpecial");
extern volatile LONG * const MiArm64LastGuardedLeaveKernelPtr __asm__(".refptr.MiArm64LastGuardedLeaveKernel");
volatile PVOID MiArm64RefptrGuardBase;
volatile SIZE_T MiArm64RefptrGuardSize;

/* Define .refptr entries for our own guarded-leave diagnostics so the
 * linker finds them on ARM64. These are read-only pointers whose symbol
 * names are .refptr.<name>, pointing at the actual globals above. */
__attribute__((used, section(".rdata$.refptr.MiArm64LastGuardedLeaveIrql")))
volatile ULONG * const __MiArm64LastGuardedLeaveIrqlRefptr __asm__(".refptr.MiArm64LastGuardedLeaveIrql") = &MiArm64LastGuardedLeaveIrql;

__attribute__((used, section(".rdata$.refptr.MiArm64LastGuardedLeaveThread")))
volatile ULONG_PTR * const __MiArm64LastGuardedLeaveThreadRefptr __asm__(".refptr.MiArm64LastGuardedLeaveThread") = &MiArm64LastGuardedLeaveThread;

__attribute__((used, section(".rdata$.refptr.MiArm64LastGuardedLeaveSpecial")))
volatile LONG * const __MiArm64LastGuardedLeaveSpecialRefptr __asm__(".refptr.MiArm64LastGuardedLeaveSpecial") = &MiArm64LastGuardedLeaveSpecial;

__attribute__((used, section(".rdata$.refptr.MiArm64LastGuardedLeaveKernel")))
volatile LONG * const __MiArm64LastGuardedLeaveKernelRefptr __asm__(".refptr.MiArm64LastGuardedLeaveKernel") = &MiArm64LastGuardedLeaveKernel;

/* Define .refptr entries for selected object manager globals so the
 * linker finds them on ARM64 when referenced via the Obp*Ptr aliases.
 * Clang already materializes these symbols for the asm-alias externs,
 * so we only emit the backing pointers for GCC/MinGW. */
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((used, section(".rdata$.refptr.ObpLUIDDeviceMapsEnabled")))
volatile ULONG * const __ObpLUIDDeviceMapsEnabledRefptr __asm__(".refptr.ObpLUIDDeviceMapsEnabled") = &ObpLUIDDeviceMapsEnabled;

__attribute__((used, section(".rdata$.refptr.ObpNameBufferLookasideList")))
volatile PVOID * const __ObpNameBufferLookasideListRefptr __asm__(".refptr.ObpNameBufferLookasideList") = (PVOID *)&ObpNameBufferLookasideList;

__attribute__((used, section(".rdata$.refptr.ObpObjectSecurityMode")))
volatile ULONG * const __ObpObjectSecurityModeRefptr __asm__(".refptr.ObpObjectSecurityMode") = &ObpObjectSecurityMode;

__attribute__((used, section(".rdata$.refptr.ObpProtectionMode")))
volatile ULONG * const __ObpProtectionModeRefptr __asm__(".refptr.ObpProtectionMode") = &ObpProtectionMode;
#endif


static const PCSTR KiArm64VectorNames[16] =
{
    [0]  = "Sync SP0",
    [1]  = "IRQ SP0",
    [2]  = "FIQ SP0",
    [3]  = "SError SP0",
    [4]  = "Sync SPx",
    [5]  = "IRQ SPx",
    [6]  = "FIQ SPx",
    [7]  = "SError SPx",
    [8]  = "Sync lower A64",
    [9]  = "IRQ lower A64",
    [10] = "FIQ lower A64",
    [11] = "SError lower A64",
    [12] = "Sync lower A32",
    [13] = "IRQ lower A32",
    [14] = "FIQ lower A32",
    [15] = "SError lower A32",
};

static const PCSTR KiArm64EsrClassNames[64] =
{
    [0x00] = "Unknown",
    [0x01] = "WFI/WFE trap",
    [0x03] = "CP15 RT trap",
    [0x04] = "CP15 R trap",
    [0x05] = "CP15 W trap",
    [0x07] = "FP/SIMD access",
    [0x08] = "MCRR/MRRC trap",
    [0x0C] = "SVE access",
    [0x11] = "SVC in AArch32",
    [0x12] = "HVC in AArch32",
    [0x13] = "SMC in AArch32",
    [0x15] = "SVC in AArch64",
    [0x16] = "HVC in AArch64",
    [0x17] = "SMC in AArch64",
    [0x18] = "MSR/MRS trap",
    [0x1C] = "Instruction abort (lower EL)",
    [0x1D] = "Instruction abort (same EL)",
    [0x20] = "Data abort (lower EL)",
    [0x21] = "Data abort (same EL)",
    [0x22] = "SP alignment fault",
    [0x24] = "FP exception",
    [0x26] = "FP exception (AArch64)",
    [0x2F] = "SError interrupt",
};

static PCSTR
KiArm64DescribeEsr(_In_ UINT64 ExceptionSyndrome)
{
    ULONG Class = (ULONG)((ExceptionSyndrome >> 26) & 0x3FULL);

    if (Class < RTL_NUMBER_OF(KiArm64EsrClassNames) &&
        KiArm64EsrClassNames[Class] != NULL)
    {
        return KiArm64EsrClassNames[Class];
    }

    return "Unknown";
}

static VOID
KiArm64DumpRegisterState(_In_ const ARM64_EARLY_TRAP_STATE *State,
                         _In_opt_ PARM64_EARLY_LOG_SINK Sink)
{
    ULONG Base;

    UNREFERENCED_PARAMETER(Sink);

    for (Base = 0; Base < 28; Base += 4)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "  x%02u=0x%016llx x%02u=0x%016llx x%02u=0x%016llx x%02u=0x%016llx\n",
                   Base, State->Registers.X[Base],
                   Base + 1, State->Registers.X[Base + 1],
                   Base + 2, State->Registers.X[Base + 2],
                   Base + 3, State->Registers.X[Base + 3]);
    }

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "  x28=0x%016llx x29=0x%016llx x30=0x%016llx\n",
               State->Registers.X[28],
               State->Registers.X[29],
               State->Registers.X[30]);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "  sp =0x%016llx pc =0x%016llx pstate=0x%016llx\n",
               State->Registers.Sp,
               State->Registers.Pc,
               State->Registers.Pstate);
}

static BOOLEAN
KiArm64DumpStackSnapshot(_In_ const ARM64_EARLY_TRAP_STATE *State,
                         _In_opt_ PARM64_EARLY_LOG_SINK Sink)
{
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Sink);
    return FALSE;
}

VOID
KiArm64DumpEarlyTrapState(_In_ const ARM64_EARLY_TRAP_STATE *State,
                          _In_opt_ PARM64_EARLY_LOG_SINK Sink)
{
    PCSTR VectorName;
    PCSTR EsrDesc;
    ULONG Iss;

    UNREFERENCED_PARAMETER(Sink);

    if (!State)
        return;

    VectorName = (State->VectorId < RTL_NUMBER_OF(KiArm64VectorNames) &&
                  KiArm64VectorNames[State->VectorId])
                     ? KiArm64VectorNames[State->VectorId]
                     : "Unknown";

    EsrDesc = KiArm64DescribeEsr(State->ExceptionSyndrome);
    Iss = (ULONG)(State->ExceptionSyndrome & 0x1FFFFFFULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "Exception caught at elr=0x%016llx (vector %llu %s) esr=0x%016llx (%s) iss=0x%08lx far=0x%016llx spsr=0x%016llx\n",
               State->Elr,
               State->VectorId,
               VectorName,
               State->ExceptionSyndrome,
               EsrDesc,
               Iss,
               State->FaultAddress,
               State->Spsr);

    {
        LONG traceSlot = InterlockedIncrement(&MiArm64TrapTraceIndex);
        ULONG slot = (ULONG)traceSlot & 3u;

        MiArm64TrapTraceVector[slot] = State->VectorId;
        MiArm64TrapTraceElr[slot] = State->Elr;
        MiArm64TrapTraceFar[slot] = State->FaultAddress;
        MiArm64TrapTraceEsr[slot] = State->ExceptionSyndrome;
        MiArm64TrapTraceSpsr[slot] = State->Spsr;
        MiArm64TrapTraceX16[slot] = State->Registers.X[16];
        MiArm64TrapTraceX17[slot] = State->Registers.X[17];
        MiArm64TrapTraceX0[slot] = State->Registers.X[0];
        MiArm64TrapTraceX1[slot] = State->Registers.X[1];
        MiArm64TrapTraceX8[slot] = State->Registers.X[8];
        MiArm64TrapTraceX9[slot] = State->Registers.X[9];
        MiArm64TrapTraceX20[slot] = State->Registers.X[20];
        MiArm64TrapTraceX21[slot] = State->Registers.X[21];
    }

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "Registers:\n");
    KiArm64DumpRegisterState(State, Sink);

    KiArm64DumpStackSnapshot(State, Sink);
}

static
VOID
KiArm64InitializeBugCheckState(
    _Out_ PARM64_EARLY_TRAP_STATE State,
    _In_ ULONG BugCheckCode)
{
    RtlZeroMemory(State, sizeof(*State));

    State->VectorId = (ULONG64)BugCheckCode;
}

static
VOID
KiArm64AugmentStateFromContext(
    _Inout_ PARM64_EARLY_TRAP_STATE State,
    _In_opt_ const CONTEXT *Context)
{
    ULONG Index;

    if (Context == NULL)
    {
        return;
    }

    for (Index = 0; Index < ARM64_EARLY_TRAP_REGISTER_COUNT; ++Index)
    {
        State->Registers.X[Index] = Context->X[Index];
    }

    State->Registers.Sp = Context->Sp;
    State->Registers.Pc = Context->Pc;
    State->Registers.Pstate = Context->Cpsr;

    if (State->Elr == 0)
    {
        State->Elr = Context->Pc;
    }

    if (State->Spsr == 0)
    {
        State->Spsr = Context->Cpsr;
    }
}

static
VOID
KiArm64AugmentStateFromTrapFrame(
    _Inout_ PARM64_EARLY_TRAP_STATE State,
    _In_opt_ const KTRAP_FRAME *TrapFrame)
{
    ULONG Index;

    if (TrapFrame == NULL)
    {
        return;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(TrapFrame->X) &&
                    Index < ARM64_EARLY_TRAP_REGISTER_COUNT;
         ++Index)
    {
        State->Registers.X[Index] = TrapFrame->X[Index];
    }

    if (ARM64_EARLY_TRAP_REGISTER_COUNT > 29)
    {
        State->Registers.X[29] = TrapFrame->Fp;
    }

    if (ARM64_EARLY_TRAP_REGISTER_COUNT > 30)
    {
        State->Registers.X[30] = TrapFrame->Lr;
    }

    State->Registers.Sp = TrapFrame->Sp;
    State->Registers.Pc = TrapFrame->Pc;
    State->Registers.Pstate = TrapFrame->Spsr;
    State->ExceptionSyndrome = TrapFrame->Esr;
    State->FaultAddress = TrapFrame->FaultAddress;
    State->Elr = TrapFrame->Pc;
    State->Spsr = TrapFrame->Spsr;
}

VOID
KiArm64DumpTrapStateToLog(_In_ const ARM64_EARLY_TRAP_STATE *State)
{
    UNREFERENCED_PARAMETER(State);
    /* Bring-up: suppress trap-state emission entirely unless explicitly
       re-enabled with a live KD connection. */
    return;
}

VOID
KiArm64DumpBugCheckState(
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR BugCheckParameter1,
    _In_ ULONG_PTR BugCheckParameter2,
    _In_ ULONG_PTR BugCheckParameter3,
    _In_ ULONG_PTR BugCheckParameter4,
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_opt_ const CONTEXT *Context)
{
    ARM64_EARLY_TRAP_STATE State;

    /* If no debugger is attached, keep this lightweight to avoid re-entry
       during early boot. Emit a single line and skip the full state dump. */
    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[arm64] BugCheck: code=%lx params=%p,%p,%p,%p (KD absent)\n",
                   BugCheckCode,
                   (PVOID)BugCheckParameter1,
                   (PVOID)BugCheckParameter2,
                   (PVOID)BugCheckParameter3,
                   (PVOID)BugCheckParameter4);
        return;
    }

    KiArm64InitializeBugCheckState(&State, BugCheckCode);

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[arm64] BugCheck snapshot: code=%lx params=%p,%p,%p,%p\n",
               BugCheckCode,
               (PVOID)BugCheckParameter1,
               (PVOID)BugCheckParameter2,
               (PVOID)BugCheckParameter3,
               (PVOID)BugCheckParameter4);

    KiArm64AugmentStateFromContext(&State, Context);
    KiArm64AugmentStateFromTrapFrame(&State, TrapFrame);

    if ((Context == NULL) && (TrapFrame == NULL))
    {
        State.Elr = (ULONG64)_ReturnAddress();
    }

    KiArm64DumpTrapStateToLog(&State);
}
