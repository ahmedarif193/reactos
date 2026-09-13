/*
 * PROJECT:     ReactOS NT Layer DLL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 Control Flow Guard check and dispatch targets
 */

#include <asm.h>

    .text
    .align 4

    .extern LdrpValidateUserCallTarget

    .global LdrpGuardCheckIcall
    .seh_proc LdrpGuardCheckIcall
LdrpGuardCheckIcall:
    stp     x29, x30, [sp, #-288]!
    .seh_save_fplr_x 288
    mov     x29, sp
    .seh_set_fp
    .seh_endprologue
    stp     x0, x1, [sp, #16]
    stp     x2, x3, [sp, #32]
    stp     x4, x5, [sp, #48]
    stp     x6, x7, [sp, #64]
    stp     x8, x9, [sp, #80]
    stp     x10, x11, [sp, #96]
    stp     x12, x13, [sp, #112]
    stp     x14, x15, [sp, #128]
    stp     x16, x17, [sp, #144]
    stp     q0, q1, [sp, #160]
    stp     q2, q3, [sp, #192]
    stp     q4, q5, [sp, #224]
    stp     q6, q7, [sp, #256]

    /* Let the processor report a naturally misaligned branch target. */
    tst     x15, #3
    b.ne    1f

    mov     x0, x15
    bl      LdrpValidateUserCallTarget
    cbnz    w0, 1f
    mov     w0, #10
    brk     #0xf003

1:
    ldp     q0, q1, [sp, #160]
    ldp     q2, q3, [sp, #192]
    ldp     q4, q5, [sp, #224]
    ldp     q6, q7, [sp, #256]
    ldp     x0, x1, [sp, #16]
    ldp     x2, x3, [sp, #32]
    ldp     x4, x5, [sp, #48]
    ldp     x6, x7, [sp, #64]
    ldp     x8, x9, [sp, #80]
    ldp     x10, x11, [sp, #96]
    ldp     x12, x13, [sp, #112]
    ldp     x14, x15, [sp, #128]
    ldp     x16, x17, [sp, #144]
    .seh_startepilogue
    ldp     x29, x30, [sp], #288
    .seh_save_fplr_x 288
    .seh_endepilogue
    ret
    .seh_endproc

    .global LdrpGuardDispatchIcall
    .seh_proc LdrpGuardDispatchIcall
LdrpGuardDispatchIcall:
    .seh_endprologue
    mov     x17, x30
    mov     x15, x16
    bl      LdrpGuardCheckIcall
    mov     x30, x17
    br      x16
    .seh_endproc
