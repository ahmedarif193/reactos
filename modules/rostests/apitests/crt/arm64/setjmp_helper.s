/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Test helper for ARM64 setjmp
 */

    .text
    .align 2

/*
 * ULONG_PTR get_sp(void)
 * Returns the caller's stack pointer.
 * On ARM64, BL doesn't push to the stack, so SP is the caller's SP.
 */
    .global get_sp
get_sp:
    mov     x0, sp
    ret

/*
 * void call_setjmp(_JUMP_BUFFER *Buf)
 * Sets up known values in callee-saved registers, then calls _setjmp.
 */
    .extern _setjmp
    .global setjmp_return_address
    .global call_setjmp

call_setjmp:
    /* Save callee-saved registers (we will clobber them with test values) */
    sub     sp, sp, #176
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     x29, x30, [sp, #80]
    stp     d8,  d9,  [sp, #96]
    stp     d10, d11, [sp, #112]
    stp     d12, d13, [sp, #128]
    stp     d14, d15, [sp, #144]
    /* Save the buffer pointer (x0) */
    str     x0, [sp, #160]

    /* Set up known values in non-volatile general purpose registers */
    ldr     x19, =0xB1B1B1B1B1B1B1B1
    ldr     x20, =0xB2B2B2B2B2B2B2B2
    ldr     x21, =0xB3B3B3B3B3B3B3B3
    ldr     x22, =0xB4B4B4B4B4B4B4B4
    ldr     x23, =0xB5B5B5B5B5B5B5B5
    ldr     x24, =0xB6B6B6B6B6B6B6B6
    ldr     x25, =0xB7B7B7B7B7B7B7B7
    ldr     x26, =0xB8B8B8B8B8B8B8B8
    ldr     x27, =0xB9B9B9B9B9B9B9B9
    ldr     x28, =0xBABABABABABABABA

    /* Set up known values in non-volatile SIMD registers D8-D15.
       LLVM assembler doesn't support ldr d-reg, =literal syntax,
       so load via a GP temp register and fmov. */
    ldr     x9, =0xD8D8D8D8D8D8D8D8
    fmov    d8, x9
    ldr     x9, =0xD9D9D9D9D9D9D9D9
    fmov    d9, x9
    ldr     x9, =0xDADADADADADADADA
    fmov    d10, x9
    ldr     x9, =0xDBDBDBDBDBDBDBDB
    fmov    d11, x9
    ldr     x9, =0xDCDCDCDCDCDCDCDC
    fmov    d12, x9
    ldr     x9, =0xDDDDDDDDDDDDDDDD
    fmov    d13, x9
    ldr     x9, =0xDEDEDEDEDEDEDEDE
    fmov    d14, x9
    ldr     x9, =0xDFDFDFDFDFDFDFDF
    fmov    d15, x9

    /* Restore buffer pointer to x0 */
    ldr     x0, [sp, #160]
    /* Set x1 = sp (frame pointer argument for _setjmp) */
    mov     x1, sp

    /* Call _setjmp(x0=buffer, x1=frame) */
    bl      _setjmp
setjmp_return_address:

    /* Restore callee-saved registers */
    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    ldp     x29, x30, [sp, #80]
    ldp     d8,  d9,  [sp, #96]
    ldp     d10, d11, [sp, #112]
    ldp     d12, d13, [sp, #128]
    ldp     d14, d15, [sp, #144]
    add     sp, sp, #176
    ret
