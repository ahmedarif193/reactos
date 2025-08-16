/*
 * PROJECT:     FreeLoader UEFI Support for ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 UEFI Assembly Functions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* ARM64 UEFI Assembly - GNU AS syntax */
.text
.align 2

/* BootMain is defined in freeldr.c, not here */

/* UefiExitBootServices is defined in uefimem.c, not here */

/* Transfer control to the kernel */
.global KernelEntryPoint
/* .type KernelEntryPoint, %function */
KernelEntryPoint:
    /* x0 already contains LoaderBlock pointer */
    /* x1 contains kernel entry address */
    
    /* Disable interrupts */
    msr daifset, #0xf
    
    /* Flush instruction cache */
    ic iallu
    dsb sy
    isb
    
    /* Jump to kernel */
    br x1
    
    /* Should never reach here */
1:  b 1b

/* UEFI callback thunk for ARM64 */
.global UefiCallbackThunk
/* .type UefiCallbackThunk, %function */
UefiCallbackThunk:
    /* Save all registers according to UEFI ABI */
    stp x29, x30, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    
    /* Call the C callback handler */
    bl UefiCallbackHandler
    
    /* Restore registers */
    ldp x19, x20, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x29, x30, [sp], #16
    ret

/* Stack switching helper */
.global SwitchStack
/* .type SwitchStack, %function */
SwitchStack:
    /* x0 = new stack pointer */
    /* x1 = function to call */
    /* x2 = argument for function */
    
    /* Switch to new stack */
    mov sp, x0
    
    /* Setup argument */
    mov x0, x2
    
    /* Call function on new stack */
    blr x1
    
    /* Should not return */
1:  b 1b

/* Get current exception level */
.global GetCurrentEL
/* .type GetCurrentEL, %function */
GetCurrentEL:
    mrs x0, CurrentEL
    lsr x0, x0, #2
    ret

/* Memory barrier operations */
.global MemoryBarrier
/* .type MemoryBarrier, %function */
MemoryBarrier:
    dmb sy
    ret

.global InstructionBarrier
/* .type InstructionBarrier, %function */
InstructionBarrier:
    isb
    ret

.global DataSyncBarrier
/* .type DataSyncBarrier, %function */
DataSyncBarrier:
    dsb sy
    ret

/* Stack change helper for UEFI */
.global _changestack
_changestack:
    /* x0 = new stack base */
    /* x1 = entry point to call */
    /* Switch to new stack */
    mov sp, x0
    /* Call the entry point */
    br x1

/* Exit UEFI boot services */
.global _exituefi
_exituefi:
    /* Prepare for exiting UEFI boot services */
    /* For now, just return */
    ret

/* Bug check function */
.global FrLdrBugCheckWithMessage
FrLdrBugCheckWithMessage:
    /* Bug check - halt system */
    brk #0
1:
    wfi
    b 1b

/* Debug breakpoint */
.global DbgBreakPoint
DbgBreakPoint:
    brk #0
    ret

/* Internal debug breakpoint */
.global _InternalDebugBreakpoint
_InternalDebugBreakpoint:
    brk #0
    ret

/* UEFI Callback Handler stub */
.global UefiCallbackHandler
UefiCallbackHandler:
    /* For now, just return */
    ret
