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

/*
 * void _exituefi(VOID)
 *
 * Exit UEFI boot services and return to caller.
 *
 * CRITICAL: After ExitBootServices, the memory regions used by UEFI
 * (including potentially our original stack) may be reclaimed. We must:
 * 1. Save our return address BEFORE calling into UEFI functions that
 *    allocate memory (which could overwrite our stack)
 * 2. Perform proper cache maintenance after ExitBootServices since
 *    the instruction cache may contain stale entries
 * 3. Use registers that survive across the C function call
 *
 * Per AAPCS64: x19-x28 are callee-saved, x30 is LR
 * We use x20 to hold our return address across the ExitBootServices call.
 */
.globl _exituefi
_exituefi:
    /*
     * Exit UEFI boot services and return to caller.
     *
     * CRITICAL ISSUE: After ExitBootServices, memory regions used by UEFI
     * (including potentially our stack) may be reclaimed or modified. If
     * our stack is in such a region, any values saved there (including LR)
     * will be corrupted when we try to restore them.
     *
     * SOLUTION: Keep critical values in callee-saved registers (x19-x28)
     * which UefiExitBootServices MUST preserve per AAPCS64. Specifically:
     *   - x20: Return address (LR) - preserved across call in register
     *   - x21: Original stack pointer - preserved across call in register
     *
     * After ExitBootServices returns, we:
     *   1. Perform cache maintenance (instruction cache may be stale)
     *   2. Restore SP from x21
     *   3. Copy LR from x20 to x30 BEFORE restoring x20 from stack
     *   4. Restore caller's x20/x21 from stack (for ABI compliance)
     *   5. Return using the LR we copied to x30
     */

    /* Set up frame and save LR */
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    /* Save caller's x20 and x21 (we will use these registers) */
    stp x20, x21, [sp, #-16]!

    /*
     * CRITICAL: Copy LR to x20 IMMEDIATELY. From this point on, x20 holds
     * our return address and will survive across any memory corruption.
     * This is safe because we just saved caller's x20 to the stack.
     */
    mov x20, x30

    /* Copy current SP to x21. This is our "safe" stack pointer. */
    mov x21, sp

    /* Check if UefiServiceStack is available for the call */
    ldr x0, =UefiServiceStack
    ldr x0, [x0]
    cbz x0, .use_current_stack
    mov sp, x0
    b .stack_ready

.use_current_stack:
    /* No separate service stack; continue with current stack */
    mov sp, x21

.stack_ready:
    /*
     * Align stack to 16 bytes per AAPCS64.
     * Cannot use BIC directly on SP; use temp register.
     */
    mov x0, sp
    bic x0, x0, #15
    mov sp, x0

    /*
     * Call UefiExitBootServices.
     * After this call, stack memory may be corrupted, but x20 and x21
     * will still have our saved LR and SP values per AAPCS64 guarantee.
     */
    bl UefiExitBootServices

    /*
     * POST-EXITBOOTSERVICES CACHE MAINTENANCE
     *
     * UEFI may have modified memory, and the instruction cache may contain
     * stale entries. On ARM64, instruction and data caches are NOT coherent,
     * so we must explicitly invalidate the instruction cache before fetching
     * instructions from memory that may have been modified.
     */
    dsb sy
    isb
    ic iallu
    dsb sy
    isb

    /* DEBUG: Output to PL011 UART that we returned from ExitBootServices */
    /* PL011 UART base = 0x09000000, DR at offset 0, FR at offset 0x18 */
    mov x0, #0x09000000
    /* Wait for TX FIFO not full (bit 5 of FR) and output '[' */
1:  ldr w1, [x0, #0x18]
    tbnz w1, #5, 1b
    mov w1, #'['
    str w1, [x0]
1:  ldr w1, [x0, #0x18]
    tbnz w1, #5, 1b
    mov w1, #'E'
    str w1, [x0]
1:  ldr w1, [x0, #0x18]
    tbnz w1, #5, 1b
    mov w1, #'X'
    str w1, [x0]
1:  ldr w1, [x0, #0x18]
    tbnz w1, #5, 1b
    mov w1, #'I'
    str w1, [x0]
1:  ldr w1, [x0, #0x18]
    tbnz w1, #5, 1b
    mov w1, #'T'
    str w1, [x0]
1:  ldr w1, [x0, #0x18]
    tbnz w1, #5, 1b
    mov w1, #']'
    str w1, [x0]
1:  ldr w1, [x0, #0x18]
    tbnz w1, #5, 1b
    mov w1, #'\r'
    str w1, [x0]
1:  ldr w1, [x0, #0x18]
    tbnz w1, #5, 1b
    mov w1, #'\n'
    str w1, [x0]

    /*
     * RESTORE STATE
     *
     * x21 contains our original SP (preserved by callee per AAPCS64).
     * x20 contains our original LR (preserved by callee per AAPCS64).
     */

    /* Restore SP from x21 */
    mov sp, x21

    /*
     * CRITICAL: Copy our saved LR from x20 to x30 BEFORE we restore
     * x20 from the stack. This ensures we have the correct return
     * address even if the stack was corrupted.
     */
    mov x30, x20

    /*
     * Restore caller's x20, x21 from stack for ABI compliance.
     * If stack was corrupted, these will be garbage, but that's
     * acceptable - the caller's state was on the same corrupted stack.
     */
    ldp x20, x21, [sp], #16

    /*
     * Pop frame pointer (x29). We already have the correct LR in x30
     * (from x20), so we just restore x29 and skip the stacked LR.
     * We use ldp to load x29 and a dummy register to maintain 16-byte alignment.
     */
    ldp x29, xzr, [sp], #16

    /* Final barrier before returning */
    isb

    ret

/*
 * void _changestack(VOID)
 *
 * Switch to BasicStack and call ExecuteLoaderCleanly.
 * This function does not return under normal operation.
 */
.globl _changestack
_changestack:
    /* Save old stack pointer as first argument for ExecuteLoaderCleanly */
    mov x0, sp

    /* Load new stack from BasicStack global */
    ldr x1, =BasicStack
    ldr x1, [x1]
    mov sp, x1

    /* Align stack to 16 bytes per AAPCS64 */
    mov x2, sp
    bic x2, x2, #15
    mov sp, x2

    /* Call ExecuteLoaderCleanly with old stack as parameter */
    bl ExecuteLoaderCleanly

    /* Should not return, but if it does, halt */
1:
    wfe
    b 1b

/* End of file */
