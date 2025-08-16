/*
 * PROJECT:     FreeLoader for ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Trap Handling
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#include <freeldr.h>
#include <arch/arm64/arm64.h>

/* Debug support */
#include <debug.h>

/* Exception frame structure */
typedef struct _ARM64_EXCEPTION_FRAME
{
    ULONG64 X0;
    ULONG64 X1;
    ULONG64 X2;
    ULONG64 X3;
    ULONG64 X4;
    ULONG64 X5;
    ULONG64 X6;
    ULONG64 X7;
    ULONG64 X8;
    ULONG64 X9;
    ULONG64 X10;
    ULONG64 X11;
    ULONG64 X12;
    ULONG64 X13;
    ULONG64 X14;
    ULONG64 X15;
    ULONG64 X16;
    ULONG64 X17;
    ULONG64 X18;
    ULONG64 X19;
    ULONG64 X20;
    ULONG64 X21;
    ULONG64 X22;
    ULONG64 X23;
    ULONG64 X24;
    ULONG64 X25;
    ULONG64 X26;
    ULONG64 X27;
    ULONG64 X28;
    ULONG64 X29;  /* Frame pointer */
    ULONG64 X30;  /* Link register */
    ULONG64 SP;   /* Stack pointer */
    ULONG64 PC;   /* Program counter */
    ULONG64 SPSR; /* Saved program status register */
    ULONG64 ESR;  /* Exception syndrome register */
    ULONG64 FaultAddress;  /* Fault address register */
} ARM64_EXCEPTION_FRAME, *PARM64_EXCEPTION_FRAME;

/* Exception handler called from assembly */
VOID
HandleException(PARM64_EXCEPTION_FRAME ExceptionFrame)
{
    ULONG64 ESR, FaultAddr, ELR;
    ULONG ExceptionClass;
    
    /* TODO: Read exception syndrome register */
    ESR = 0; /* Will be filled by assembly handler */
    ExceptionClass = (ESR >> 26) & 0x3F;
    
    /* TODO: Read fault address register */
    FaultAddr = 0; /* Will be filled by assembly handler */
    
    /* TODO: Read exception link register */
    ELR = 0; /* Will be filled by assembly handler */
    
    /* Save in frame */
    ExceptionFrame->ESR = ESR;
    ExceptionFrame->FaultAddress = FaultAddr;
    ExceptionFrame->PC = ELR;
    
    /* Print exception information */
    printf("ARM64 Exception!\n");
    printf("Exception Class: 0x%x\n", ExceptionClass);
    printf("ESR: 0x%llx\n", ESR);
    printf("FAR: 0x%llx\n", FaultAddr);
    printf("ELR: 0x%llx\n", ELR);
    
    /* Print register dump */
    printf("Register dump:\n");
    printf("X0:  0x%016llx X1:  0x%016llx\n", ExceptionFrame->X0, ExceptionFrame->X1);
    printf("X2:  0x%016llx X3:  0x%016llx\n", ExceptionFrame->X2, ExceptionFrame->X3);
    printf("X4:  0x%016llx X5:  0x%016llx\n", ExceptionFrame->X4, ExceptionFrame->X5);
    printf("X6:  0x%016llx X7:  0x%016llx\n", ExceptionFrame->X6, ExceptionFrame->X7);
    printf("X8:  0x%016llx X9:  0x%016llx\n", ExceptionFrame->X8, ExceptionFrame->X9);
    printf("X10: 0x%016llx X11: 0x%016llx\n", ExceptionFrame->X10, ExceptionFrame->X11);
    printf("X12: 0x%016llx X13: 0x%016llx\n", ExceptionFrame->X12, ExceptionFrame->X13);
    printf("X14: 0x%016llx X15: 0x%016llx\n", ExceptionFrame->X14, ExceptionFrame->X15);
    printf("X16: 0x%016llx X17: 0x%016llx\n", ExceptionFrame->X16, ExceptionFrame->X17);
    printf("X18: 0x%016llx X19: 0x%016llx\n", ExceptionFrame->X18, ExceptionFrame->X19);
    printf("X20: 0x%016llx X21: 0x%016llx\n", ExceptionFrame->X20, ExceptionFrame->X21);
    printf("X22: 0x%016llx X23: 0x%016llx\n", ExceptionFrame->X22, ExceptionFrame->X23);
    printf("X24: 0x%016llx X25: 0x%016llx\n", ExceptionFrame->X24, ExceptionFrame->X25);
    printf("X26: 0x%016llx X27: 0x%016llx\n", ExceptionFrame->X26, ExceptionFrame->X27);
    printf("X28: 0x%016llx X29: 0x%016llx\n", ExceptionFrame->X28, ExceptionFrame->X29);
    printf("X30: 0x%016llx SP:  0x%016llx\n", ExceptionFrame->X30, ExceptionFrame->SP);
    
    /* Halt system */
    printf("System halted.\n");
    for (;;) ;
}

/* Initialize exception handling */
VOID
TrapInit(VOID)
{
    extern VOID VectorTable(VOID);
    ULONG64 VBAR = (ULONG64)&VectorTable;
    
    printf("Initializing ARM64 exception handling\n");
    printf("Vector table at: 0x%llx\n", VBAR);
    
    /* TODO: Set vector base address register */
    /* This should be done in assembly code during initialization */
}