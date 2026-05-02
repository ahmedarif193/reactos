/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Switch to boot stack and enter C wrapper
 */

    .text
    .align  2
    .global KiArm64SwitchToBootStack
    .extern KiArm64SystemStartupBootStack

/*
 * VOID KiArm64SwitchToBootStack(ULONG_PTR InitialStack,
 *                                PLOADER_PARAMETER_BLOCK LoaderBlock)
 *   x0 = InitialStack (new SP)
 *   x1 = LoaderBlock (to become x0 for the C wrapper)
 */
    .seh_proc KiArm64SwitchToBootStack
KiArm64SwitchToBootStack:
    /* SEH: This is a tail-call that switches stacks - no return */
    .seh_endprologue
    /* Ensure SP_EL1 is selected before updating the stack pointer */
    msr     spsel, #1
    isb
    /* Set the new stack pointer */
    mov     sp, x0
    /* Move LoaderBlock into x0 as first argument to C wrapper */
    mov     x0, x1
    /* Branch to C wrapper; never returns */
    b       KiArm64SystemStartupBootStack
    .seh_endproc

    // no .type/.size directives to keep asms happy in mingw/clang env
