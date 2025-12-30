#pragma once

#include <ndk/arm64/ketypes.h>
#include "intrin_i.h"

typedef struct _KSWITCHFRAME
{
    ULONG64 Dummy;
} KSWITCHFRAME, *PKSWITCHFRAME;

extern PVOID MmSystemRangeStart;
extern PVOID MmHighestUserAddress;

#define SYNCH_LEVEL DISPATCH_LEVEL

#define KD_BREAKPOINT_TYPE        ULONG
#define KD_BREAKPOINT_SIZE        sizeof(ULONG)
#define KD_BREAKPOINT_VALUE       0xD43E0000
#define MM_SYSTEM_RANGE_START         MmSystemRangeStart

// Interrupt state helper. The DAIF.I bit (bit 7) mirrors the PSR interrupt
// mask, but for now treat trap frames as having interrupts disabled until the
// real trap exit code is in place.
#define KeGetTrapFrameInterruptState(TrapFrame) 0
#define KeGetContextSwitches(Prcb)  ((Prcb)->KeContextSwitches)

// HAL DMA entry points are not declared by MinGW for arm64. Mirror the
// Windows kernel prototypes so the I/O manager can call into the HAL.
NTHALAPI
NTSTATUS
NTAPI
HalAllocateAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PWAIT_CONTEXT_BLOCK Wcb,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine);

FORCEINLINE
VOID
KeInvalidateTlbEntry(
    _In_ PVOID Address)
{
    ULONG_PTR Va = (ULONG_PTR)Address >> PAGE_SHIFT;

    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vaae1is, %0" :: "r"(Va));
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
KeFlushProcessTb(VOID)
{
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
KiArm64WriteUserTtbr(
    _In_ ULONGLONG DirectoryBase)
{
    ULONGLONG MaskedBase = DirectoryBase & ~((ULONGLONG)PAGE_SIZE - 1ULL);

    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("msr ttbr0_el1, %0" :: "r"(MaskedBase) : "memory");
    __asm__ __volatile__("isb" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(FlushSize);

    __asm__ __volatile__("ic iallu" ::: "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID);

ULONG
NTAPI
KeGetCurrentProcessorNumberEx(
    _Out_opt_ PPROCESSOR_NUMBER ProcNumber);

FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ULONG_PTR Flags;

    __asm__ __volatile__("mrs %0, daif" : "=r"(Flags));
    __asm__ __volatile__("msr daifset, #2" ::: "memory");

    return ((Flags & (1ULL << 7)) == 0);
}

FORCEINLINE
VOID
KeRestoreInterrupts(
    _In_ BOOLEAN WereEnabled)
{
    if (WereEnabled)
    {
        __asm__ __volatile__("msr daifclr, #2" ::: "memory");
    }
}

FORCEINLINE
VOID
KiRundownThread(
    _In_ PKTHREAD Thread)
{
    UNREFERENCED_PARAMETER(Thread);
}

FORCEINLINE
ULONG_PTR
KeGetContextPc(
    _In_ PCONTEXT Context)
{
    return Context->Pc;
}

FORCEINLINE
VOID
KeSetContextPc(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR ProgramCounter)
{
    Context->Pc = ProgramCounter;
}

FORCEINLINE
ULONG_PTR
KeGetContextReturnRegister(
    _In_ PCONTEXT Context)
{
    return Context->X0;
}

FORCEINLINE
VOID
KeSetContextReturnRegister(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR ReturnValue)
{
    Context->X0 = ReturnValue;
}

FORCEINLINE
ULONG_PTR
KeGetContextStackRegister(
    _In_ PCONTEXT Context)
{
    return Context->Sp;
}

FORCEINLINE
ULONG_PTR
KeGetContextFrameRegister(
    _In_ PCONTEXT Context)
{
    return Context->Fp;
}

FORCEINLINE
VOID
KeSetContextFrameRegister(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR Frame)
{
    Context->Fp = Frame;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFramePc(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Pc;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameStackRegister(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Sp;
}

FORCEINLINE
PULONG_PTR
KiGetUserModeStackAddress(void)
{
    return &PsGetCurrentThread()->Tcb.TrapFrame->Sp;
}

FORCEINLINE
PKTRAP_FRAME
KiGetLinkedTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return (PKTRAP_FRAME)(TrapFrame->TrapFrame);
}

#define KeGetTrapFrame(Thread) ((PKTRAP_FRAME)((Thread)->TrapFrame))
#define KeGetExceptionFrame(Thread) ((PKEXCEPTION_FRAME)((Thread)->TrapFrame))

#define KiGetPreviousMode(TrapFrame) \
    (((TrapFrame)->Spsr & 0xF) == 0 ? UserMode : KernelMode)

VOID
KeFlushTb(VOID);

VOID
HalSweepDcache(VOID);

VOID
HalSweepIcache(VOID);

VOID
KiArm64BootStageLog(_In_z_ PCSTR Stage);

/* Final exception/interrupt readiness flags (for bring-up diagnostics) */
extern BOOLEAN KiArm64FinalVectorsInstalled;
extern BOOLEAN KiArm64SvcConfigured;
extern BOOLEAN KiArm64IrqFiqConfigured;

/* Debug register counts from ID_AA64DFR0_EL1 */
extern ULONG KiArm64NumBreakpoints;
extern ULONG KiArm64NumWatchpoints;

VOID
KiInitializeDebugRegisterCounts(VOID);

#define Ki386PerfEnd()
#define KiEndInterrupt(TrapFrame, TrapStatus)

DECLSPEC_NORETURN
VOID
KiUserCallbackExit(
    _In_ PKTRAP_FRAME TrapFrame);

DECLSPEC_NORETURN
VOID
KiExceptionExit(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

BOOLEAN
KiProcessorFreezeHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

/* Debug CPU features banner */
#if DBG
VOID KiReportCpuFeatures(IN PKPRCB Prcb);
#endif
