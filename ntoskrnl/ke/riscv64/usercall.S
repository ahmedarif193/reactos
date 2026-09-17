/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Native kernel call suspension and NT callback return
 */

#include <asm/ksriscv64.h>

#define SSTATUS_SIE 0x2
#define SSTATUS_FS  0x6000

    .text
    .balign 4
    .global KiRiscvCallUserMode
/* a0 = prepared user trap frame, a1/a2 = kernel result storage. */
KiRiscvCallUserMode:
    addi sp, sp, -CalloutFrameLength
    sd s0, CoS0(sp)
    sd ra, CoRa(sp)
    addi s0, sp, CalloutFrameLength
    .irp n,1,2,3,4,5,6,7,8,9,10,11
    sd s\n, CoS1 + 8 * (\n - 1)(sp)
    .endr
    sd gp, CoGp(sp)
    sd tp, CoTp(sp)
    csrr t0, sstatus
    sd t0, CoSstatus(sp)
    li t1, SSTATUS_FS
    and t1, t1, t0
    beqz t1, .Lcallout_saved
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11
    fsd fs\n, CoFs + 8 * \n(sp)
    .endr
    frcsr t1
    sw t1, CoFcsr(sp)
.Lcallout_saved:
    sd a1, CoOutputBuffer(sp)
    sd a2, CoOutputLength(sp)
    mv a1, sp
    call KiRiscvUserModeCallout
    /* Only preparation failure returns here; success exits through SRET. */
    j .Lcallback_resume

    .balign 4
    .global KiRiscvCallbackReturn
/* NtCallbackReturn restored the kernel-owned thread links with SIE clear.
 * Keep interrupts masked until the original SP and register bank agree. */
KiRiscvCallbackReturn:
    mv sp, a0
    mv a0, a1
.Lcallback_resume:
    csrci sstatus, SSTATUS_SIE
    ld t0, CoSstatus(sp)
    andi t1, t0, ~SSTATUS_SIE
    csrw sstatus, t1
    li t2, SSTATUS_FS
    and t2, t2, t0
    beqz t2, .Lcallback_integer
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11
    fld fs\n, CoFs + 8 * \n(sp)
    .endr
    lw t2, CoFcsr(sp)
    fscsr t2
.Lcallback_integer:
    .irp n,1,2,3,4,5,6,7,8,9,10,11
    ld s\n, CoS1 + 8 * (\n - 1)(sp)
    .endr
    ld gp, CoGp(sp)
    ld tp, CoTp(sp)
    ld s0, CoS0(sp)
    ld ra, CoRa(sp)
    addi sp, sp, CalloutFrameLength
    /* FP restores may have changed FS. Restore the saved kernel status,
     * enabling interrupts only after the suspended stack is active again. */
    csrw sstatus, t0
    ret
