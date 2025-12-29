/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 assembly UEFI escape helper functions
 * COPYRIGHT:   Copyright 2024 Ahmed ARIF (contact@eotics.com)
 */

/* No x86 asm includes on ARM64 */

.extern UefiServiceStack
.extern BasicStack
.extern ExecuteLoaderCleanly
.extern UefiExitBootServices

.text

// void _exituefi(VOID)
.globl _exituefi
_exituefi:
    /* Save callee-saved registers */
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!

    /* Save the current stack pointer */
    mov x19, sp

    /* Load the UEFI service stack if available */
    ldr x0, =UefiServiceStack
    ldr x0, [x0]
    cbz x0, .use_current_stack
    mov sp, x0
    b .stack_ready

.use_current_stack:
    /* UefiServiceStack is NULL, use current stack */
    mov sp, x19

.stack_ready:
    /* Call UefiExitBootServices directly (function symbol, not a pointer) */
    bl UefiExitBootServices
    /* Restore the old stack */
    mov sp, x19

    /* Restore callee-saved registers */
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16

    ret

// void _changestack(VOID)
.globl _changestack
_changestack:
    /* Save old stack pointer as first argument */
    mov x0, sp
    
    /* Load new stack */
    ldr x1, =BasicStack
    ldr x1, [x1]
    mov sp, x1
    
    /* Align stack to 16 bytes: copy sp to x2, mask low bits, then update sp */
    mov x2, sp
    bic x2, x2, #15
    mov sp, x2
    mov sp, x2
    
    /* Call ExecuteLoaderCleanly with old stack as parameter */
    bl ExecuteLoaderCleanly
    
    ret

/* End of file */
