/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64EC setjmp register capture
 * COPYRIGHT:   Copyright 2023 Alexandre Julliard
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Adapted from Wine's dlls/ntdll/signal_arm64ec.c.
 */

    .text
    .align 2

    .global "#_setjmp"
    .global "#_setjmpex"
    .global "#__intrinsic_setjmp"
    .global "#__intrinsic_setjmpex"

    .seh_proc "#_setjmpex"
"#_setjmp":
"#_setjmpex":
"#__intrinsic_setjmp":
"#__intrinsic_setjmpex":
    .seh_endprologue

    /* AMD64-compatible _JUMP_BUFFER mapped through the public ARM64EC ABI. */
    stp     x1, x27, [x0, #0x00]
    mov     x3, sp
    stp     x3, x29, [x0, #0x10]
    stp     x25, x26, [x0, #0x20]
    stp     x19, x20, [x0, #0x30]
    stp     x21, x22, [x0, #0x40]
    str     x30, [x0, #0x50]

    /* Only the low halves of v8-v15 are nonvolatile for a direct EC caller.
       A following unwind replaces XMM6-XMM15 with the complete x64 context
       when the caller frame belongs to emulated AMD64 code. */
    stp     d8, d9, [x0, #0x80]
    stp     d10, d11, [x0, #0xa0]
    stp     d12, d13, [x0, #0xc0]
    stp     d14, d15, [x0, #0xe0]

    mrs     x1, fpcr
    mrs     x2, fpsr
    b       "#ChpepSetJmpFinalize"
    .seh_endproc
