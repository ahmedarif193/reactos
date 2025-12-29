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
 *   x0 = NewStack
 *   x1 = Function pointer
 */
KdbpStackSwitchAndCall:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    mov     x9, sp            // save old SP
    mov     sp, x0            // switch to debugger stack
    blr     x1                // call function
    mov     sp, x9            // restore original stack
    ldp     x29, x30, [sp], #16
    ret

    // no .size for COFF/Mingw
