/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         KDBG helper to switch to debugger stack and call function
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 */

    .text
    .p2align  2
    .globl  KdbpStackSwitchAndCall

/*
 * VOID NTAPI KdbpStackSwitchAndCall(PVOID NewStack, VOID (*Function)(VOID))
 *   x0 = NewStack (must be 16-byte aligned with space for frame)
 *   x1 = Function pointer
 *
 * This function switches to a new stack, calls the function, then switches
 * back. We use x19 (callee-saved) to preserve the original SP across the call.
 */
    .seh_proc KdbpStackSwitchAndCall
KdbpStackSwitchAndCall:
    /* Save callee-saved registers and link register on current stack */
    stp     x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    stp     x19, x20, [sp, #16]
    .seh_save_regp x19, 16
    mov     x29, sp
    .seh_set_fp
    .seh_endprologue

    /* Save original SP in callee-saved register (survives the call) */
    mov     x19, sp

    /* Switch to the debugger stack */
    mov     sp, x0

    /* Set up a proper frame on the new stack for the callee */
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    /* Call the function */
    blr     x1

    /* Pop frame from debugger stack */
    ldp     x29, x30, [sp], #16

    /* Restore original stack */
    mov     sp, x19

    /* Restore callee-saved registers and return */
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret
    .seh_endproc

    /* no .size for COFF/Mingw */
