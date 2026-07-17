/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Win11-style eager KARM64_VFP_STATE transfer leaves.
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text
    .align  2

/* VOID KiSaveVfpState(PKARM64_VFP_STATE State); */
    .global KiSaveVfpState
    .seh_proc KiSaveVfpState
KiSaveVfpState:
    .seh_endprologue
    mrs     x1, fpcr
    str     w1, [x0, #0x08]
    mrs     x1, fpsr
    str     w1, [x0, #0x0C]
    stp     q0, q1, [x0, #0x010]
    stp     q2, q3, [x0, #0x030]
    stp     q4, q5, [x0, #0x050]
    stp     q6, q7, [x0, #0x070]
    stp     q8, q9, [x0, #0x090]
    stp     q10, q11, [x0, #0x0B0]
    stp     q12, q13, [x0, #0x0D0]
    stp     q14, q15, [x0, #0x0F0]
    stp     q16, q17, [x0, #0x110]
    stp     q18, q19, [x0, #0x130]
    stp     q20, q21, [x0, #0x150]
    stp     q22, q23, [x0, #0x170]
    stp     q24, q25, [x0, #0x190]
    stp     q26, q27, [x0, #0x1B0]
    stp     q28, q29, [x0, #0x1D0]
    stp     q30, q31, [x0, #0x1F0]
    ret
    .seh_endproc

/* VOID KiRestoreVfpState(PKARM64_VFP_STATE State); */
    .global KiRestoreVfpState
    .seh_proc KiRestoreVfpState
KiRestoreVfpState:
    .seh_endprologue
    ldr     w1, [x0, #0x08]
    msr     fpcr, x1
    ldr     w1, [x0, #0x0C]
    msr     fpsr, x1
    ldp     q0, q1, [x0, #0x010]
    ldp     q2, q3, [x0, #0x030]
    ldp     q4, q5, [x0, #0x050]
    ldp     q6, q7, [x0, #0x070]
    ldp     q8, q9, [x0, #0x090]
    ldp     q10, q11, [x0, #0x0B0]
    ldp     q12, q13, [x0, #0x0D0]
    ldp     q14, q15, [x0, #0x0F0]
    ldp     q16, q17, [x0, #0x110]
    ldp     q18, q19, [x0, #0x130]
    ldp     q20, q21, [x0, #0x150]
    ldp     q22, q23, [x0, #0x170]
    ldp     q24, q25, [x0, #0x190]
    ldp     q26, q27, [x0, #0x1B0]
    ldp     q28, q29, [x0, #0x1D0]
    ldp     q30, q31, [x0, #0x1F0]
    ret
    .seh_endproc
