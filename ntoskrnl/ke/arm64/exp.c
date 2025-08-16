/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Exception Handling
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PROTOTYPES ****************************************************************/

VOID
NTAPI
KiHandleSGI(IN ULONG IntId, IN PKTRAP_FRAME TrapFrame);

VOID
NTAPI
KiHandleDeviceInterrupt(IN ULONG IntId, IN PKTRAP_FRAME TrapFrame);

VOID
NTAPI
KiIpiProcessRequests(VOID);

VOID
NTAPI
KiFlushProcessTb(VOID);

VOID
NTAPI
KiIpiCallFunction(VOID);

/* Global interrupt table declaration moved to globals section */

/* GLOBALS *******************************************************************/

ULONG KiARM64ExceptionCount[32] = {0};

/* Interrupt table - defined in interrupt.c for ARM64 */
extern PVOID KiInterruptTable[1020];

/* Exception syndrome register (ESR) exception class values */
#define ESR_EC_UNKNOWN          0x00
#define ESR_EC_WFX              0x01
#define ESR_EC_CP15_32          0x03
#define ESR_EC_CP15_64          0x04
#define ESR_EC_CP14_MR          0x05
#define ESR_EC_CP14_LS          0x06
#define ESR_EC_FP_ASIMD         0x07
#define ESR_EC_CP10_ID          0x08
#define ESR_EC_PAC              0x09
#define ESR_EC_CP14_64          0x0C
#define ESR_EC_BTI              0x0D
#define ESR_EC_ILL              0x0E
#define ESR_EC_SVC32            0x11
#define ESR_EC_SVC64            0x15
#define ESR_EC_SYS64            0x18
#define ESR_EC_SVE              0x19
#define ESR_EC_FPAC             0x1C
#define ESR_EC_IMP_DEF          0x1F
#define ESR_EC_IABT_LOW         0x20
#define ESR_EC_IABT_CUR         0x21
#define ESR_EC_PC_ALIGN         0x22
#define ESR_EC_DABT_LOW         0x24
#define ESR_EC_DABT_CUR         0x25
#define ESR_EC_SP_ALIGN         0x26
#define ESR_EC_FP_EXC32         0x28
#define ESR_EC_FP_EXC64         0x2C
#define ESR_EC_SERROR           0x2F
#define ESR_EC_BREAKPT_LOW      0x30
#define ESR_EC_BREAKPT_CUR      0x31
#define ESR_EC_SOFTSTP_LOW      0x32
#define ESR_EC_SOFTSTP_CUR      0x33
#define ESR_EC_WATCHPT_LOW      0x34
#define ESR_EC_WATCHPT_CUR      0x35
#define ESR_EC_BKPT32           0x38
#define ESR_EC_BRK64            0x3C

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
KiInitializeExceptionHandling(VOID)
{
    /* Initialize exception vector table */
    /* The exception vector table is at a fixed location in ARM64 */
    /* Each vector is 128 bytes apart */
    
    /* Set VBAR_EL1 (Vector Base Address Register) */
    extern VOID KiARM64ExceptionVectors(VOID);
    ULONG64 VectorBase = (ULONG64)&KiARM64ExceptionVectors;
    
    __asm__ __volatile__("msr vbar_el1, %0" : : "r" (VectorBase));
    __asm__ __volatile__("isb");
    
    DPRINT1("ARM64 Exception vectors installed at 0x%llx\n", VectorBase);
}

NTSTATUS
NTAPI
KiHandleSynchronousException(IN PKTRAP_FRAME TrapFrame)
{
    ULONG64 ESR, FaultAddr, ExceptionLR;
    ULONG ExceptionClass;
    NTSTATUS Status = STATUS_SUCCESS;
    
    /* Read Exception Syndrome Register */
    __asm__ __volatile__("mrs %0, esr_el1" : "=r" (ESR));
    
    /* Read Fault Address Register */
    __asm__ __volatile__("mrs %0, far_el1" : "=r" (FaultAddr));
    
    /* Read Exception Link Register (return address) */
    __asm__ __volatile__("mrs %0, elr_el1" : "=r" (ExceptionLR));
    
    /* Extract exception class from ESR */
    ExceptionClass = (ESR >> 26) & 0x3F;
    
    /* Update statistics */
    KiARM64ExceptionCount[ExceptionClass & 0x1F]++;
    
    /* Update trap frame */
    TrapFrame->Esr = ESR;
    TrapFrame->FaultAddress = FaultAddr;
    TrapFrame->Pc = ExceptionLR;
    
    /* Handle based on exception class */
    switch (ExceptionClass)
    {
        case ESR_EC_SVC64:
            /* System call from 64-bit state */
            KiSystemService(TrapFrame);
            Status = STATUS_SUCCESS;
            break;
            
        case ESR_EC_DABT_CUR:
        case ESR_EC_DABT_LOW:
            /* Data abort (page fault) */
            Status = MmAccessFault(FaultAddr, TrapFrame, FALSE, (PVOID)(ULONG_PTR)(TrapFrame->Spsr & 0xF));
            break;
            
        case ESR_EC_IABT_CUR:
        case ESR_EC_IABT_LOW:
            /* Instruction abort (page fault) */
            Status = MmAccessFault(FaultAddr, TrapFrame, TRUE, (PVOID)(ULONG_PTR)(TrapFrame->Spsr & 0xF));
            break;
            
        case ESR_EC_PC_ALIGN:
            /* PC alignment fault */
            /* TODO: Properly dispatch exception */
            Status = STATUS_DATATYPE_MISALIGNMENT;
            break;
            
        case ESR_EC_SP_ALIGN:
            /* Stack pointer alignment fault */
            /* TODO: Properly dispatch exception */
            Status = STATUS_DATATYPE_MISALIGNMENT;
            break;
            
        case ESR_EC_ILL:
            /* Illegal instruction */
            /* TODO: Properly dispatch exception */
            Status = STATUS_ILLEGAL_INSTRUCTION;
            break;
            
        case ESR_EC_BRK64:
            /* Software breakpoint */
            Status = KiHandleBreakpoint(TrapFrame);
            break;
            
        case ESR_EC_WATCHPT_CUR:
        case ESR_EC_WATCHPT_LOW:
            /* Watchpoint */
            Status = KiHandleWatchpoint(TrapFrame, FaultAddr);
            break;
            
        case ESR_EC_FP_EXC64:
            /* Floating point exception */
            Status = KiHandleFloatingPointException(TrapFrame);
            break;
            
        default:
            /* Unknown or unhandled exception */
            DPRINT1("Unhandled exception: Class=0x%02x, ESR=0x%llx, FAR=0x%llx, ELR=0x%llx\n",
                    ExceptionClass, ESR, FaultAddr, ExceptionLR);
            KeBugCheckEx(UNEXPECTED_KERNEL_MODE_TRAP,
                         ExceptionClass,
                         ESR,
                         FaultAddr,
                         ExceptionLR);
            break;
    }
    
    return Status;
}

VOID
NTAPI
KiHandleInterrupt(IN PKTRAP_FRAME TrapFrame)
{
    ULONG IntId;
    KIRQL OldIrql;
    
    /* Read the interrupt ID from the interrupt controller */
    /* For GICv3, this would be from ICC_IAR1_EL1 */
    __asm__ __volatile__("mrs %0, s3_0_c12_c12_0" : "=r" (IntId));
    
    /* Check for spurious interrupt */
    if ((IntId & 0x3FF) == 0x3FF)
    {
        /* Spurious interrupt, just return */
        return;
    }
    
    /* Raise IRQL */
    OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
    
    /* Dispatch the interrupt */
    if (IntId < 32)
    {
        /* Software Generated Interrupt (SGI) */
        KiHandleSGI(IntId, TrapFrame);
    }
    else if (IntId < 1020)
    {
        /* Shared Peripheral Interrupt (SPI) or Private Peripheral Interrupt (PPI) */
        KiHandleDeviceInterrupt(IntId, TrapFrame);
    }
    
    /* End of interrupt - write to ICC_EOIR1_EL1 */
    __asm__ __volatile__("msr s3_0_c12_c12_1, %0" : : "r" (IntId));
    
    /* Lower IRQL */
    KfLowerIrql(OldIrql);
}

/* KiSystemService moved to syscall.c */

NTSTATUS
NTAPI
KiHandleBreakpoint(IN PKTRAP_FRAME TrapFrame)
{
    /* Check if kernel debugger is connected */
    if (KdDebuggerEnabled)
    {
        /* Break into debugger */
        DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);
    }
    else
    {
        /* No debugger, raise exception */
        /* TODO: Properly dispatch exception */
        return STATUS_BREAKPOINT;
    }
    
    /* Skip the breakpoint instruction (4 bytes) */
    TrapFrame->Elr += 4;
    
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KiHandleWatchpoint(IN PKTRAP_FRAME TrapFrame, IN ULONG64 FaultAddress)
{
    /* Handle hardware watchpoint */
    if (KdDebuggerEnabled)
    {
        /* Report to debugger */
        DPRINT1("Watchpoint hit at 0x%llx\n", FaultAddress);
        DbgBreakPoint();
    }
    
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KiHandleFloatingPointException(IN PKTRAP_FRAME TrapFrame)
{
    ULONG64 FPSR;
    
    /* Read Floating-point Status Register */
    __asm__ __volatile__("mrs %0, fpsr" : "=r" (FPSR));
    
    /* Check exception flags */
    if (FPSR & 0x01) /* Invalid operation */
    {
        /* TODO: Properly dispatch exception */
        return STATUS_FLOAT_INVALID_OPERATION;
    }
    else if (FPSR & 0x02) /* Division by zero */
    {
        /* TODO: Properly dispatch exception */
        return STATUS_FLOAT_DIVIDE_BY_ZERO;
    }
    else if (FPSR & 0x04) /* Overflow */
    {
        /* TODO: Properly dispatch exception */
        return STATUS_FLOAT_OVERFLOW;
    }
    else if (FPSR & 0x08) /* Underflow */
    {
        /* TODO: Properly dispatch exception */
        return STATUS_FLOAT_UNDERFLOW;
    }
    else if (FPSR & 0x10) /* Inexact */
    {
        /* TODO: Properly dispatch exception */
        return STATUS_FLOAT_INEXACT_RESULT;
    }
    
    /* Clear exception flags */
    FPSR &= ~0x1F;
    __asm__ __volatile__("msr fpsr, %0" : : "r" (FPSR));
    
    return STATUS_SUCCESS;
}

/* KiDispatchException wrapper removed - use the real function directly */

/* IPI helper functions */
VOID
NTAPI
KiIpiProcessRequests(VOID)
{
    /* Process pending IPI requests */
    /* TODO: Implement IPI request processing */
    DPRINT1("ARM64: KiIpiProcessRequests stub\n");
}

VOID
NTAPI
KiFlushProcessTb(VOID)
{
    /* Flush process TLB */
    KeFlushCurrentTb();
}

VOID
NTAPI
KiIpiCallFunction(VOID)
{
    /* Call function on other processors */
    /* TODO: Implement IPI function calls */
    DPRINT1("ARM64: KiIpiCallFunction stub\n");
}

VOID
NTAPI
KiHandleSGI(IN ULONG IntId, IN PKTRAP_FRAME TrapFrame)
{
    /* Handle Software Generated Interrupts (IPI) */
    switch (IntId)
    {
        case 0:
            /* Rescheduling IPI */
            KiIpiProcessRequests();
            break;
            
        case 1:
            /* TLB flush IPI */
            KiFlushProcessTb();
            break;
            
        case 2:
            /* Call function IPI */
            KiIpiCallFunction();
            break;
            
        default:
            DPRINT1("Unknown SGI: %d\n", IntId);
            break;
    }
}

/* KiHandleDeviceInterrupt is implemented in interrupt.c */