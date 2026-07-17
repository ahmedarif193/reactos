/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/trapdump.c
 * PURPOSE:         Rich diagnostics for early ARM64 exceptions
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <arm64trap.h>

extern BOOLEAN KdDebuggerEnabled;
extern BOOLEAN KdDebuggerNotPresent;

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
