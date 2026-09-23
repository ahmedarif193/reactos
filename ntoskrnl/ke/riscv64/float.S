/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */
#include <asm/ksriscv64.h>

    .text
    .p2align 2
    .global KxSaveFloatingPointState
KxSaveFloatingPointState:
    csrr t0, sscratch
    lbu t1, PcIrql(t0)
    li t2, DISPATCH_LEVEL
    bgtu t1, t2, .Linvalid
    sb t1, FsIrql(a0)
    ld t1, PcCurrentThread(t0)
    sd t1, FsOwner(a0)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fsd f\n, FsF + 8 * \n(a0)
    .endr
    frcsr t0
    sw t0, FsFcsr(a0)
    fscsr zero
    li a0, 0
    ret

    .p2align 2
    .global KxRestoreFloatingPointState
KxRestoreFloatingPointState:
    csrr t0, sscratch
    ld t1, PcCurrentThread(t0)
    ld t2, FsOwner(a0)
    bne t1, t2, .Linvalid
    lbu t1, PcIrql(t0)
    lbu t2, FsIrql(a0)
    bne t1, t2, .Linvalid
    sd zero, FsOwner(a0)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fld f\n, FsF + 8 * \n(a0)
    .endr
    lw t0, FsFcsr(a0)
    andi t0, t0, 255
    fscsr t0
    li a0, 0
    ret
.Linvalid:
    li a0, STATUS_INVALID_DEVICE_STATE
    sext.w a0, a0
    ret
