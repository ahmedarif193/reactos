/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Kernel Internal Definitions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#pragma once

/* Include ARM64 stubs and compatibility definitions */
#include "stubs.h"

/* ARM64 specific definitions */

/* Interrupt levels */
#define SYNCH_LEVEL    2
#define CLOCK_LEVEL    13
#define IPI_LEVEL      14
#define HIGH_LEVEL     15

/* ARM64 specific constants */
#define KPCR_SELF_PCR_OFFSET     0x18
#define KPCR_CURRENT_PRCB_OFFSET 0x20
#define KPCR_PRCB_OFFSET         0x180

/* Kernel stack sizes */
#define KERNEL_STACK_SIZE        0x6000
#define KERNEL_LARGE_STACK_SIZE  0x12000
#define KERNEL_LARGE_STACK_COMMIT KERNEL_STACK_SIZE

/* Exception constants */
#define EXCEPTION_CHAIN_END      ((PVOID)-1)

/* Breakpoint type - ARM64 uses 32-bit breakpoint instructions */
#define KD_BREAKPOINT_TYPE       ULONG
#define KD_BREAKPOINT_VALUE      0xD4200000  /* BRK #0 instruction */

/* Performance counter stub - ARM64 doesn't have x86-style performance counters */
VOID NTAPI Ki386PerfEnd(VOID);

/* Processor Control Region access */
/* ARM64 stores PCR in TPIDR_EL1 */
#ifndef FORCEINLINE
#define FORCEINLINE static __inline
#endif

/* KeGetPcr is already defined in ndk/arm64/ketypes.h */
#if 0
FORCEINLINE
struct _KPCR *
KeGetPcr(VOID)
{
    struct _KPCR *Pcr;
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r" (Pcr));
    return Pcr;
}
#endif

#define PCR (KeGetPcr())
/* Don't define Prcb as a macro since it's used as parameter name */
/* #define Prcb (PCR->Prcb) */

/* Kernel tick count - simplified for ARM64 */
extern volatile KSYSTEM_TIME KeTickCount;

/* ARM64 specific functions */
#ifdef __cplusplus
extern "C" {
#endif

VOID
NTAPI
KiInitializeMachineType(VOID);

VOID
NTAPI
KiInitializeKernel(
    IN PKPROCESS InitProcess,
    IN PKTHREAD InitThread,
    IN PVOID IdleStack,
    IN PKPRCB Prcb,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock);

/* Already declared in ke.h */
#if 0
VOID
NTAPI
KiSystemStartup(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock);
#endif

/* KiIdleLoop - Already declared elsewhere */

/* Context switching - Already declared elsewhere, commenting out */
/*
VOID
NTAPI
KiSwapContext(
    IN PKTHREAD OldThread,
    IN PKTHREAD NewThread);
*/

/* Trap frame functions */
ULONG_PTR
NTAPI
KeGetTrapFramePc(IN PKTRAP_FRAME TrapFrame);

BOOLEAN
NTAPI
KiUserTrap(IN PKTRAP_FRAME TrapFrame);

PKTRAP_FRAME
NTAPI
KiGetLinkedTrapFrame(IN PKTRAP_FRAME TrapFrame);

VOID
NTAPI
KiExceptionExit(IN PKTRAP_FRAME TrapFrame,
                IN PKEXCEPTION_FRAME ExceptionFrame);

/* Helper functions */
FORCEINLINE
ULONG
KeGetContextSwitches(IN PKPRCB Prcb)
{
    return Prcb->KeContextSwitches;
}

VOID
NTAPI
KiThreadStartup(
    IN PKTHREAD Thread);

VOID
NTAPI
KiRundownThread(
    IN PKTHREAD Thread);

/* Context manipulation functions for ARM64 */
FORCEINLINE
ULONG_PTR
KeGetContextPc(IN PCONTEXT Context)
{
    return Context->Pc;
}

FORCEINLINE
VOID
KeSetContextPc(IN PCONTEXT Context, IN ULONG_PTR Pc)
{
    Context->Pc = Pc;
}

FORCEINLINE
VOID
KeSetContextReturnRegister(IN PCONTEXT Context, IN ULONG_PTR Value)
{
    /* ARM64 uses X0 for return value */
    Context->X0 = Value;
}

/* Interrupt handling - Already declared elsewhere */
/*
VOID
NTAPI
KiInterruptDispatch(VOID);
*/

VOID
NTAPI
KiUnexpectedInterrupt(VOID);

/* Exception handling - Already declared in ke.h */
#if 0
VOID
NTAPI
KiDispatchException(
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PKTRAP_FRAME TrapFrame,
    IN KPROCESSOR_MODE PreviousMode,
    IN BOOLEAN FirstChance);
#endif

/* System call */
VOID
NTAPI
KiSystemService(
    IN PKTRAP_FRAME TrapFrame);

VOID
NTAPI
KiServiceExit(
    IN PKTRAP_FRAME TrapFrame,
    IN NTSTATUS Status);

/* Processor initialization */
VOID
NTAPI
KiInitializeProcessor(
    IN PKPRCB Prcb);

VOID
NTAPI
KeFlushCurrentTb(VOID);

/* Stubs for now */
static __inline VOID KeFlushProcessTb(VOID) { KeFlushCurrentTb(); }

/* Frame/Stack register helpers */
FORCEINLINE
ULONG_PTR
KeGetContextFrameRegister(PCONTEXT Context)
{
    return Context->Fp;
}

FORCEINLINE
VOID
KeSetContextFrameRegister(PCONTEXT Context, ULONG_PTR Frame)
{
    Context->Fp = Frame;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameStackRegister(PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Sp;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameFrameRegister(PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Fp;
}

/* Debug */
VOID
NTAPI
KiDumpTrapFrame(
    IN PKTRAP_FRAME TrapFrame);

/* ARM64 specific processor block structure */
/* Note: KPCR is already defined in NDK, we just extend it here */
/* Remove our local definition to avoid conflicts */

#ifdef __cplusplus
}
#endif

/* EOF */