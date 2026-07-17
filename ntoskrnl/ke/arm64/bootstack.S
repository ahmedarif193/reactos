/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Switch to boot stack and enter C wrapper
 */

    .text
    .align  2
    .global KiSystemStartup
    .global KiArm64SwitchToBootStack
    .extern KiArm64SystemStartupC
    .extern KiArm64SystemStartupBootStack

/* Enable mandatory base FP/ASIMD before entering compiler-generated C. */
    .seh_proc KiSystemStartup
KiSystemStartup:
    .seh_endprologue
    mrs     x9, cpacr_el1
    mov     x10, #3
    lsl     x10, x10, #20
    orr     x9, x9, x10
    msr     cpacr_el1, x9
    isb
    b       KiArm64SystemStartupC
    .seh_endproc

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
