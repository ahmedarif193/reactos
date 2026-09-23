#pragma once

#include "intrin_i.h"

#define KiServiceExit2 KiExceptionExit

#define SYNCH_LEVEL DISPATCH_LEVEL
#define PCR                     ((KPCR * const)KIP0PCRADDRESS)

//
//Lockdown TLB entries
//
#define PCR_ENTRY            0
#define PDR_ENTRY            2

//
// BKPT is 4 bytes long
//
#define KD_BREAKPOINT_TYPE        ULONG
#define KD_BREAKPOINT_SIZE        sizeof(ULONG)
#define KD_BREAKPOINT_VALUE       0xDEFE

//
// Maximum IRQs
//
#define MAXIMUM_VECTOR          16

//
// Macros for getting and setting special purpose registers in portable code
//
#define KeGetContextPc(Context) \
    ((Context)->Pc)

#define KeSetContextPc(Context, ProgramCounter) \
    ((Context)->Pc = (ProgramCounter))

#define KeGetTrapFramePc(TrapFrame) \
    ((TrapFrame)->Pc)

#define KeGetContextReturnRegister(Context) \
    ((Context)->R0)

#define KeSetContextReturnRegister(Context, ReturnValue) \
    ((Context)->R0 = (ReturnValue))

//
// Macro to get trap and exception frame from a thread stack
//
#define KeGetTrapFrame(Thread) \
    (PKTRAP_FRAME)((ULONG_PTR)((Thread)->InitialStack) - \
                   sizeof(KTRAP_FRAME))

#define KeGetExceptionFrame(Thread) \
    (PKEXCEPTION_FRAME)((ULONG_PTR)KeGetTrapFrame(Thread) - \
                        sizeof(KEXCEPTION_FRAME))

//
// Macro to get context switches from the PRCB
// All architectures but x86 have it in the PRCB's KeContextSwitches
//
#define KeGetContextSwitches(Prcb)  \
    (Prcb)->KeContextSwitches

//
// Macro to get the second level cache size field name which differs between
// CISC and RISC architectures, as the former has unified I/D cache
//
#define KiGetSecondLevelDCacheSize() ((PKIPCR)KeGetPcr())->SecondLevelDcacheSize

//
// Returns the Interrupt State from a Trap Frame.
// ON = TRUE, OFF = FALSE
//
#define KeGetTrapFrameInterruptState(TrapFrame) 0

FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ARM_STATUS_REGISTER Flags;

    //
    // Get current interrupt state and disable interrupts
    //
    Flags = KeArmStatusRegisterGet();
    _disable();

    //
    // Return previous interrupt state
    //
    return Flags.IrqDisable;
}

FORCEINLINE
VOID
KeRestoreInterrupts(BOOLEAN WereEnabled)
{
    if (WereEnabled) _enable();
}

//
// Invalidates the TLB entry for a specified address
//
FORCEINLINE
VOID
KeInvalidateTlbEntry(IN PVOID Address)
{
    /* Invalidate the TLB entry for this address */
    KeArmInvalidateTlbEntry(Address);
}

FORCEINLINE
VOID
KeFlushProcessTb(VOID)
{
    KeArmFlushTlb();
}

FORCEINLINE
VOID
KeSweepICache(IN PVOID BaseAddress,
              IN SIZE_T FlushSize)
{
    //
    // Always sweep the whole cache
    //
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(FlushSize);
    _MoveToCoprocessor(0, CP15_ICIALLU);
}

FORCEINLINE
VOID
KiRundownThread(IN PKTHREAD Thread)
{
    /* FIXME */
}

VOID
KiPassiveRelease(
    VOID
);

VOID
KiSystemService(IN PKTHREAD Thread,
                IN PKTRAP_FRAME TrapFrame,
                IN ULONG Instruction);

VOID
KiApcInterrupt(
    VOID
);

#include "mm.h"

VOID
KeFlushTb(
    VOID
);

//
// Cache clean and flush
//
VOID
HalSweepDcache(
    VOID
);

VOID
HalSweepIcache(
    VOID
);

#define Ki386PerfEnd()
#define KiEndInterrupt(x,y)

#define KiGetLinkedTrapFrame(x) \
    (PKTRAP_FRAME)((x)->TrapFrame)

typedef struct _KI_SERVICE_EXCEPTION_STATE
{
    UCHAR Reserved;
} KI_SERVICE_EXCEPTION_STATE, *PKI_SERVICE_EXCEPTION_STATE;

FORCEINLINE
PKEXCEPTION_FRAME
KiEnterServiceException(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PKI_SERVICE_EXCEPTION_STATE State)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(State);
    Thread->TrapFrame = KiGetLinkedTrapFrame(TrapFrame);
    return ExceptionFrame;
}

FORCEINLINE
VOID
KiAbortServiceException(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKI_SERVICE_EXCEPTION_STATE State)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    UNREFERENCED_PARAMETER(State);
}

FORCEINLINE
VOID
KiLeaveServiceException(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(TrapFrame);
}

#define KiGetPreviousMode(tf) \
    ((tf->Cpsr & CPSRM_MASK) == CPSRM_USER) ? UserMode: KernelMode

FORCEINLINE
BOOLEAN
KiIsDpcInterruptRequested(
    _In_ PKPRCB Prcb)
{
    return Prcb->DpcInterruptRequested != FALSE;
}

FORCEINLINE
VOID
KiSetDpcInterruptRequested(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcInterruptRequested = TRUE;
}

FORCEINLINE
VOID
KiClearDpcInterruptRequested(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcInterruptRequested = FALSE;
}

FORCEINLINE
VOID
KiSetDpcPresent(
    _Inout_ PKPRCB Prcb)
{
    UNREFERENCED_PARAMETER(Prcb);
}

FORCEINLINE
VOID
KiClearDpcRequestState(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcInterruptRequested = FALSE;
}

#define KiIsUserModeTrap(TrapFrame) ((TrapFrame)->PreviousMode == UserMode)
