/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 exception and trap handlers
 * COPYRIGHT:   Copyright 2024 Ahmed ARIF (contact@eotics.com)
 */

#include <freeldr.h>
#include <arch/arm64/arm64.h>
#include <debug.h>

#if defined(UEFIBOOT)
#include <uefildr.h>
VOID
UefiArm64PrintBacktrace(
    ULONG_PTR FramePointer,
    ULONG_PTR StackTop,
    ULONG_PTR StackBottom);
#endif

/*
 * Use the runtime-detected early UART for trap output.
 * The early_uart.h provides EarlyUartPutc/EarlyUartPuts/EarlyUartPutHex
 * that use the platform-detected UART address.
 */
#include <reactos/arm64/early_uart.h>

/*
 * Trap UART output macros - use the runtime-detected early UART.
 * These work both under UEFI boot (after EarlyUartInitialize) and
 * non-UEFI boot (direct hardware access with detected address).
 */
#define TrapUartPutc(Ch)                EarlyUartPutc(Ch)
#define TrapUartPuts(String)            EarlyUartPuts(String)
#define TrapUartPutHex(Value, Nibbles)  EarlyUartPutHex((UINT64)(Value), (Nibbles))

static VOID TrapUartPutDec(ULONG Value)
{
    EarlyUartPutDec(Value);
}

DBG_DEFAULT_CHANNEL(WARNING);

/* Exception syndrome register bits */
#define ESR_ELx_EC_SHIFT    26
#define ESR_ELx_EC_MASK     0x3F
#define ESR_ELx_IL          (1UL << 25)
#define ESR_ELx_ISS_MASK    0x1FFFFFF

/* Exception classes */
#define ESR_ELx_EC_UNKNOWN      0x00
#define ESR_ELx_EC_WFx          0x01
#define ESR_ELx_EC_SVC64        0x15
#define ESR_ELx_EC_HVC64        0x16
#define ESR_ELx_EC_SMC64        0x17
#define ESR_ELx_EC_SYS64        0x18
#define ESR_ELx_EC_IABT_LOW     0x20
#define ESR_ELx_EC_IABT_CUR     0x21
#define ESR_ELx_EC_PC_ALIGN     0x22
#define ESR_ELx_EC_DABT_LOW     0x24
#define ESR_ELx_EC_DABT_CUR     0x25
#define ESR_ELx_EC_SP_ALIGN     0x26
#define ESR_ELx_EC_BREAKPT_CUR  0x31
#define ESR_ELx_EC_SOFTSTP_CUR  0x33
#define ESR_ELx_EC_WATCHPT_CUR  0x35
#define ESR_ELx_EC_BRK64        0x3C

/* Data abort ISS bits */
#define ESR_ELx_ISV             (1UL << 24)
#define ESR_ELx_SAS_SHIFT       22
#define ESR_ELx_SAS_MASK        0x3
#define ESR_ELx_SSE             (1UL << 21)
#define ESR_ELx_SRT_SHIFT       16
#define ESR_ELx_SRT_MASK        0x1F
#define ESR_ELx_SF              (1UL << 15)
#define ESR_ELx_AR              (1UL << 14)
#define ESR_ELx_EA              (1UL << 9)
#define ESR_ELx_CM              (1UL << 8)
#define ESR_ELx_S1PTW           (1UL << 7)
#define ESR_ELx_WNR             (1UL << 6)
#define ESR_ELx_DFSC_MASK       0x3F

/* Fault status codes */
#define ESR_ELx_FSC_ADDR_SIZE_0 0x00
#define ESR_ELx_FSC_ADDR_SIZE_1 0x01
#define ESR_ELx_FSC_ADDR_SIZE_2 0x02
#define ESR_ELx_FSC_ADDR_SIZE_3 0x03
#define ESR_ELx_FSC_TRANS_0     0x04
#define ESR_ELx_FSC_TRANS_1     0x05
#define ESR_ELx_FSC_TRANS_2     0x06
#define ESR_ELx_FSC_TRANS_3     0x07
#define ESR_ELx_FSC_ACCESS_0    0x08
#define ESR_ELx_FSC_ACCESS_1    0x09
#define ESR_ELx_FSC_ACCESS_2    0x0A
#define ESR_ELx_FSC_ACCESS_3    0x0B
#define ESR_ELx_FSC_PERM_0      0x0C
#define ESR_ELx_FSC_PERM_1      0x0D
#define ESR_ELx_FSC_PERM_2      0x0E
#define ESR_ELx_FSC_PERM_3      0x0F

static const char* Arm64ExceptionNames[] = {
    "Unknown",
    "WFI/WFE",
    "Unknown", "CP15 32-bit", "CP15 64-bit", "CP14 MRC/MCR", "CP14 LDC/STC", "FP/ASIMD",
    "CP10 ID", "Unknown", "Unknown", "Unknown", "CP14 64-bit", "Unknown", "ILL", "Unknown",
    "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "SVC", "HVC", "SMC",
    "SYS", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "IMP DEF",
    "IABT (lower)", "IABT (current)", "PC align", "Unknown", "DABT (lower)", "DABT (current)", "SP align", "Unknown",
    "FP exception (32-bit)", "Unknown", "Unknown", "Unknown", "FP exception (64-bit)", "Unknown", "Unknown", "SError",
    "Breakpoint (lower)", "Breakpoint (current)", "Soft step (lower)", "Soft step (current)", 
    "Watchpoint (lower)", "Watchpoint (current)", "Unknown", "Unknown",
    "BKPT (32-bit)", "Unknown", "Unknown", "Unknown", "BRK (64-bit)", "Unknown", "Unknown", "Unknown"
};

static const char* Arm64FaultStatusNames[] = {
    "Address size fault (level 0)", "Address size fault (level 1)", "Address size fault (level 2)", "Address size fault (level 3)",
    "Translation fault (level 0)", "Translation fault (level 1)", "Translation fault (level 2)", "Translation fault (level 3)",
    "Access flag fault (level 0)", "Access flag fault (level 1)", "Access flag fault (level 2)", "Access flag fault (level 3)",
    "Permission fault (level 0)", "Permission fault (level 1)", "Permission fault (level 2)", "Permission fault (level 3)"
};

static VOID TrapDescribeAbort(ULONG ExceptionClass, ULONG ISS, ULONGLONG FaultAddr)
{
    ULONG FaultStatus = ISS & ESR_ELx_DFSC_MASK;
    BOOLEAN Stage1Walk = (ISS & ESR_ELx_S1PTW) != 0;
    const char *FaultName = (FaultStatus < ARRAYSIZE(Arm64FaultStatusNames)) ?
                            Arm64FaultStatusNames[FaultStatus] : "Unknown fault";
    BOOLEAN IsInstrAbort = (ExceptionClass == ESR_ELx_EC_IABT_CUR || ExceptionClass == ESR_ELx_EC_IABT_LOW);
    BOOLEAN IsWrite = (ISS & ESR_ELx_WNR) != 0;

    ERR("Detail: %s abort at 0x%016llx (%s)%s [DFSC=0x%02lx]\n",
        IsInstrAbort ? "Instruction" : (IsWrite ? "Data (write)" : "Data (read)"),
        FaultAddr,
        FaultName,
        Stage1Walk ? " via table walk" : "",
        FaultStatus);

    TrapUartPuts("Detail: ");
    TrapUartPuts(IsInstrAbort ? "Instruction" : (IsWrite ? "Data (write)" : "Data (read)"));
    TrapUartPuts(" abort @0x");
    TrapUartPutHex(FaultAddr, 16);
    TrapUartPuts(" ");
    TrapUartPuts(FaultName);
    if (Stage1Walk)
        TrapUartPuts(" walk");
    TrapUartPuts(" DFSC=0x");
    TrapUartPutHex(FaultStatus, 2);
    TrapUartPuts("\n");
}

static VOID TrapDumpBacktrace(PARM64_CONTEXT Context)
{
#if defined(UEFIBOOT)
    ULONG_PTR FramePointer = (ULONG_PTR)Context->X[29];
    ULONG_PTR StackPointer = (ULONG_PTR)Context->SP;
    ULONG_PTR StackBottom = StackPointer & ~0xFFFFULL;
    ULONG_PTR StackTop    = StackBottom + 0x10000ULL;

    if (FramePointer < StackBottom || FramePointer >= StackTop)
        FramePointer = StackPointer;

    TrapUartPuts("Backtrace (UART)\n");
    {
        ULONG_PTR fp_walk = FramePointer;
        ULONG frames = 0;
        const ULONG max_frames = 32;

        while (frames < max_frames)
        {
            if ((fp_walk & 0xF) != 0)
                break;
            if (fp_walk < StackBottom || fp_walk + 16 > StackTop)
                break;

            ULONG_PTR *slot = (ULONG_PTR *)fp_walk;
            ULONG_PTR next_fp = slot[0];
            ULONG_PTR lr = slot[1];

            if (lr == 0 || next_fp <= fp_walk)
                break;

            TrapUartPuts("    0x");
            TrapUartPutHex(lr, 16);
            TrapUartPuts("\n");

            fp_walk = next_fp;
            frames++;
        }
    }

    UefiArm64PrintBacktrace(FramePointer, StackTop, StackBottom);
#else
    UNREFERENCED_PARAMETER(Context);
#endif
}

VOID Arm64DumpContext(PARM64_CONTEXT Context)
{
    ULONG i;
    
    ERR("ARM64 Exception Context:\n");
    TrapUartPuts("ARM64 Exception Context\n");
    ERR("PC: 0x%016llx  SP: 0x%016llx  PSTATE: 0x%016llx\n", 
        Context->PC, Context->SP, Context->PSTATE);
    TrapUartPuts("PC: 0x");
    TrapUartPutHex(Context->PC, 16);
    TrapUartPuts("  SP: 0x");
    TrapUartPutHex(Context->SP, 16);
    TrapUartPuts("  PSTATE: 0x");
    TrapUartPutHex(Context->PSTATE, 16);
    TrapUartPuts("\n");
    
    for (i = 0; i < 31; i += 2)
    {
        if (i < 30)
        {
            ERR("X%02lu: 0x%016llx  X%02lu: 0x%016llx\n",
                i, Context->X[i], i + 1, Context->X[i + 1]);
            TrapUartPuts("X");
            TrapUartPutDec(i);
            TrapUartPuts(": 0x");
            TrapUartPutHex(Context->X[i], 16);
            TrapUartPuts("  X");
            TrapUartPutDec(i + 1);
            TrapUartPuts(": 0x");
            TrapUartPutHex(Context->X[i + 1], 16);
            TrapUartPuts("\n");
        }
        else
        {
            ERR("X%02lu: 0x%016llx\n", i, Context->X[i]);
            TrapUartPuts("X");
            TrapUartPutDec(i);
            TrapUartPuts(": 0x");
            TrapUartPutHex(Context->X[i], 16);
            TrapUartPuts("\n");
        }
    }
}

VOID Arm64HandleSynchronousException(PARM64_CONTEXT Context, ULONGLONG Esr, ULONGLONG FaultAddr, ULONGLONG PC)
{
    ULONG ExceptionClass = (Esr >> ESR_ELx_EC_SHIFT) & ESR_ELx_EC_MASK;
    ULONG ISS = Esr & ESR_ELx_ISS_MASK;
    const char* ExceptionName;
    
    /* Get exception name */
    if (ExceptionClass < ARRAYSIZE(Arm64ExceptionNames))
    {
        ExceptionName = Arm64ExceptionNames[ExceptionClass];
    }
    else
    {
        ExceptionName = "Invalid";
    }
    
    ERR("ARM64 Synchronous Exception: %s (EC=0x%02lx)\n", ExceptionName, ExceptionClass);
    ERR("ESR: 0x%016llx  FAR: 0x%016llx  ELR: 0x%016llx  ISS: 0x%08lx  IL=%u\n",
        Esr, FaultAddr, PC, ISS, (Esr & ESR_ELx_IL) ? 1 : 0);
    TrapUartPuts("ARM64 FreeLDR Trap: ");
    TrapUartPuts(ExceptionName);
    TrapUartPuts(" (EC=0x");
    TrapUartPutHex(ExceptionClass, 2);
    TrapUartPuts(")\nESR=0x");
    TrapUartPutHex(Esr, 16);
    TrapUartPuts(" FAR=0x");
    TrapUartPutHex(FaultAddr, 16);
    TrapUartPuts(" ELR=0x");
    TrapUartPutHex(PC, 16);
    TrapUartPuts(" ISS=0x");
    TrapUartPutHex(ISS, 8);
    TrapUartPuts("\n");
    
    switch (ExceptionClass)
    {
        case ESR_ELx_EC_SYS64:
        {
            ULONG op0 = (ISS >> 20) & 0x3;
            ULONG op1 = (ISS >> 14) & 0x7;
            ULONG crn = (ISS >> 10) & 0xF;
            ULONG crm = (ISS >> 1) & 0xF;
            ULONG op2 = (ISS >> 17) & 0x7;
            BOOLEAN is_read = ((ISS & 1) == 1);
            ERR("System register access trap: %s, op0=%lu op1=%lu CRn=%lu CRm=%lu op2=%lu\n",
                is_read ? "MRS" : "MSR", op0, op1, crn, crm, op2);
            break;
        }

        case ESR_ELx_EC_DABT_CUR:
        case ESR_ELx_EC_DABT_LOW:
        {
            ULONG FaultStatus = ISS & ESR_ELx_DFSC_MASK;
            BOOLEAN IsWrite = (ISS & ESR_ELx_WNR) != 0;
            
            ERR("Data Abort: %s at 0x%016llx (%s)\n",
                IsWrite ? "Write" : "Read", FaultAddr,
                (FaultStatus < ARRAYSIZE(Arm64FaultStatusNames)) ? 
                Arm64FaultStatusNames[FaultStatus] : "Unknown fault");
            TrapDescribeAbort(ExceptionClass, ISS, FaultAddr);
            break;
        }
        
        case ESR_ELx_EC_IABT_CUR:
        case ESR_ELx_EC_IABT_LOW:
        {
            ULONG FaultStatus = ISS & ESR_ELx_DFSC_MASK;
            
            ERR("Instruction Abort at 0x%016llx (%s)\n", FaultAddr,
                (FaultStatus < ARRAYSIZE(Arm64FaultStatusNames)) ? 
                Arm64FaultStatusNames[FaultStatus] : "Unknown fault");
            TrapDescribeAbort(ExceptionClass, ISS, FaultAddr);
            break;
        }
        
        case ESR_ELx_EC_PC_ALIGN:
            ERR("PC Alignment fault at 0x%016llx\n", PC);
            break;
            
        case ESR_ELx_EC_SP_ALIGN:
            ERR("SP Alignment fault, SP=0x%016llx\n", Context->SP);
            break;
            
        case ESR_ELx_EC_SVC64:
            ERR("System call (SVC) with immediate 0x%04lx\n", ISS & 0xFFFF);
            /* Could handle system calls here if needed */
            break;
            
        case ESR_ELx_EC_BRK64:
            ERR("Software breakpoint (BRK) with immediate 0x%04lx\n", ISS & 0xFFFF);
            break;
            
        case ESR_ELx_EC_UNKNOWN:
        default:
            ERR("Unknown exception class 0x%02lx, ISS=0x%08lx\n", ExceptionClass, ISS);
            break;
    }
    
    /* Dump register context */
    Arm64DumpContext(Context);
    TrapDumpBacktrace(Context);
    
    /* For now, halt on any exception */
    ERR("ARM64: Halting due to unhandled exception\n");
    for (;;)
    {
        __asm__ volatile ("wfi");
    }
}

/* Simple IRQ dispatch table (GIC supports up to 1020 valid IDs) */
#define ARM64_MAX_IRQS 1024
static ARM64_IRQ_HANDLER Arm64IrqTable[ARM64_MAX_IRQS] = { 0 };

VOID Arm64RegisterIrqHandler(ULONG IntId, ARM64_IRQ_HANDLER Handler)
{
    if (IntId < ARM64_MAX_IRQS)
        Arm64IrqTable[IntId] = Handler;
}

VOID Arm64HandleIrq(PARM64_CONTEXT Context)
{
    /* IRQ handling - for now just acknowledge and return */
    /* In a full implementation, this would: */
    /* 1. Read interrupt controller (GICv3/GICv2) */
    /* 2. Identify interrupt source */
    /* 3. Call appropriate handler */
    /* 4. EOI (End of Interrupt) */

    ULONG IntId = Arm64GicAcknowledgeInterrupt();
    if (IntId != (ULONG)~0U)
    {
        if (IntId < ARM64_MAX_IRQS && Arm64IrqTable[IntId])
        {
            Arm64IrqTable[IntId](IntId, Context);
        }
        Arm64GicEndOfInterrupt(IntId);
    }
    TRACE("ARM64: IRQ received\n");
}

VOID Arm64HandleFiq(PARM64_CONTEXT Context)
{
    /* FIQ handling - fast interrupt request */
    /* Usually used for critical/high-priority interrupts */
    /* FIQ dispatch logic can be added here if needed */
    TRACE("ARM64: FIQ received (not implemented)\n");
    
    /* FIQs are typically disabled in boot loader */
}

VOID Arm64HandleSerror(PARM64_CONTEXT Context, ULONGLONG Esr)
{
    ERR("ARM64 System Error (SError) Exception\n");
    ERR("ESR: 0x%016llx\n", Esr);
    
    /* SError is an asynchronous external abort */
    /* Usually indicates serious system problems */
    
    /* Dump context for debugging */
    Arm64DumpContext(Context);
    
    ERR("ARM64: System error - halting\n");
    for (;;)
    {
        __asm__ volatile ("wfi");
    }
}

#if !defined(UEFIBOOT)
/* Initialize ARM64 exception handling */
extern VOID arm64_exception_vectors(VOID);

VOID Arm64InitializeExceptions(VOID)
{
    TRACE("ARM64: Programming exception vectors\n");

    UINT64 vector_base = (UINT64)(uintptr_t)&arm64_exception_vectors;
    __asm__ volatile("msr vbar_el1, %0" :: "r"(vector_base) : "memory");
    ARM64_ISB();

    TRACE("ARM64: VBAR_EL1 set to 0x%llx\n", (unsigned long long)vector_base);
}
#endif /* !UEFIBOOT */
