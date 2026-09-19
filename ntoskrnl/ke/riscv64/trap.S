/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V supervisor trap entry and resumable exit
 *
 * Supervisor traps use the interrupted stack; user traps select the current
 * thread's resident kernel stack. Both save a KTRAP_FRAME and
 * call KiRiscvTrapHandler. If it returns, the frame is restored and the
 * trap returns with sret. A trap that arrives while the prologue is still
 * saving state (PcTrapActive set: the interrupted stack itself is bad)
 * is captured in the PCR panic frame on the panic stack and stopped.
 */

#include <asm/ksriscv64.h>

#define SSTATUS_FS_MASK 0x6000
#define SSTATUS_SIE     0x2
#define SSTATUS_SPP     0x100

    .section .text$KiRiscvTrapEntry, "xr"
    .balign 4
    .global KiRiscvTrapEntry
KiRiscvTrapEntry:
    /* sp = PCR, sscratch = interrupted sp. */
    csrrw sp, sscratch, sp
    sd t0, PcTrapScratch(sp)
    sd t1, PcTrapScratch2(sp)
    lbu t0, PcTrapActive(sp)
    bnez t0, .Lpanic_trap
    li t0, 1
    sb t0, PcTrapActive(sp)

    /* Save the interrupted sp in resident, kernel-owned storage. A user sp
     * is data only: it must never select the supervisor frame address. */
    csrr t0, sscratch
    sd t0, PcTrapStack(sp)
    /* A frame store may fault. Nested entry must still find the PCR,
     * not the interrupted (possibly exhausted) stack in sscratch. */
    csrw sscratch, sp
    csrr t0, sstatus
    andi t0, t0, SSTATUS_SPP
    bnez t0, .Lsupervisor_stack
    ld t0, PcCurrentThread(sp)
    ld t0, ThInitialStack(t0)
    j .Lallocate_frame
.Lsupervisor_stack:
    /* Timer and external interrupts run on the processor's DPC stack. This
     * keeps asynchronous frames and ISR/DPC call depth off a nearly exhausted
     * thread stack. A nested interrupt already in that stack continues below
     * its interrupted frame. Software interrupts stay on the thread stack so
     * their scheduler handoff retains ordinary thread-stack ownership. */
    csrr t0, scause
    bgez t0, .Lcurrent_stack
    slli t0, t0, 1
    srli t0, t0, 1
    addi t0, t0, -5
    beqz t0, .Linterrupt_stack
    addi t0, t0, -4
    bnez t0, .Lcurrent_stack
.Linterrupt_stack:
    li t0, PcPrcb + PbDpcStack
    add t0, sp, t0
    ld t0, 0(t0)
    beqz t0, .Lcurrent_stack
    ld t1, PcTrapStack(sp)
    bgeu t1, t0, .Linterrupt_stack_top
    sub t1, t0, t1
    li t0, KERNEL_STACK_SIZE
    bleu t1, t0, .Lcurrent_stack
.Linterrupt_stack_top:
    li t0, PcPrcb + PbDpcStack
    add t0, sp, t0
    ld t0, 0(t0)
    j .Lallocate_frame
.Lcurrent_stack:
    ld t0, PcTrapStack(sp)
.Lallocate_frame:
    addi t0, t0, -TrapFrameLength
    .irp n,1,3,4,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd x\n, CxX + 8 * \n(t0)
    .endr
    sd zero, CxX + 8 * 0(t0)
    ld t1, PcTrapStack(sp)
    sd t1, CxX + 8 * 2(t0)
    ld t1, PcTrapScratch(sp)
    sd t1, CxX + 8 * 5(t0)
    ld t1, PcTrapScratch2(sp)
    sd t1, CxX + 8 * 6(t0)

    csrr t1, sepc
    sd t1, CxPc(t0)
    csrr t1, scause
    sd t1, TfScause(t0)
    csrr t1, stval
    sd t1, TfStval(t0)
    csrr t1, sstatus
    sd t1, TfSstatus(t0)
    lbu t2, PcIrql(sp)
    sb t2, TfPreviousIrql(t0)
    sd zero, TfPreviousTrapFrame(t0)

    li t2, SSTATUS_FS_MASK
    and t1, t1, t2
    beqz t1, .Lno_fp
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fsd f\n, CxF + 8 * \n(t0)
    .endr
    frcsr t1
    sw t1, CxFcsr(t0)
    li t1, CONTEXT_FULL
    j .Lcaptured
.Lno_fp:
    sw zero, CxFcsr(t0)
    li t1, CONTEXT_CONTROL | CONTEXT_INTEGER
.Lcaptured:
    sw t1, CxFlags(t0)

    /* Kernel code reads the current TEB through tp (NtCurrentTeb). After a
     * user trap, use the thread's TEB rather than the value user mode left. */
    ld t1, TfSstatus(t0)
    andi t1, t1, SSTATUS_SPP
    bnez t1, .Lkernel_tp
    ld t1, PcCurrentThread(sp)
    ld tp, ThTeb(t1)
.Lkernel_tp:

    /* State is safe on the stack: nested traps may now take new frames. */
    sb zero, PcTrapActive(sp)
    mv sp, t0
    /* End this trap's C frame chain; interrupted s0 is in the context. */
    mv s0, zero
    mv a0, sp
    .global KiRiscvTrapHandlerCall
KiRiscvTrapHandlerCall:
    call KiRiscvTrapHandler
    .global KiRiscvTrapHandlerReturn
KiRiscvTrapHandlerReturn:

    /* Resume: sp is still the frame. Interrupts stay masked until sret. */
.Lrestore_frame:
    ld t1, TfSstatus(sp)
    andi t1, t1, ~SSTATUS_SIE
    csrw sstatus, t1
    ld t1, CxPc(sp)
    csrw sepc, t1
    lw t1, CxFlags(sp)
    li t2, CONTEXT_FLOATING_POINT
    and t1, t1, t2
    li t2, CONTEXT_RISCV64
    beq t1, t2, .Lrestore_int
    lw t1, CxFcsr(sp)
    fscsr t1
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fld f\n, CxF + 8 * \n(sp)
    .endr
.Lrestore_int:
    .irp n,1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ld x\n, CxX + 8 * \n(sp)
    .endr
    /* An SC clears any reservation even when it fails. The already-consumed
     * flags/FCSR word is scratch now; do not carry a reservation into U mode. */
    sc.d zero, zero, (sp)
    ld sp, CxX + 8 * 2(sp)
    sret

    .global KiRiscvRestoreTrapFrame
KiRiscvRestoreTrapFrame:
    csrci sstatus, SSTATUS_SIE
    mv sp, a0
    j .Lrestore_frame

.Lpanic_trap:
    /* The interrupted state cannot be trusted: capture into the PCR frame
     * on the panic stack and stop. sp = PCR, t0 saved in PcTrapScratch. */
    .irp n,0,1,3,4,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd x\n, PcPanicFrame + CxX + 8 * \n(sp)
    .endr
    csrr t0, sscratch
    sd t0, PcPanicFrame + CxX + 8 * 2(sp)
    ld t0, PcTrapScratch(sp)
    sd t0, PcPanicFrame + CxX + 8 * 5(sp)
    ld t0, PcTrapScratch2(sp)
    sd t0, PcPanicFrame + CxX + 8 * 6(sp)
    csrw sscratch, sp
    csrr t0, sepc
    sd t0, PcPanicFrame + CxPc(sp)
    csrr t0, scause
    sd t0, PcPanicFrame + TfScause(sp)
    csrr t0, stval
    sd t0, PcPanicFrame + TfStval(sp)
    csrr t0, sstatus
    sd t0, PcPanicFrame + TfSstatus(sp)
    lbu t1, PcIrql(sp)
    sb t1, PcPanicFrame + TfPreviousIrql(sp)
    sd zero, PcPanicFrame + TfPreviousTrapFrame(sp)
    sw zero, PcPanicFrame + CxFcsr(sp)
    li t0, CONTEXT_CONTROL | CONTEXT_INTEGER
    sw t0, PcPanicFrame + CxFlags(sp)
    li t0, 2
    sb t0, PcTrapActive(sp)
    csrw sie, zero
    addi a0, sp, PcPanicFrame
    ld t0, PcPanicStack(sp)
    /* The panic frame is complete and the next stack is trusted. Allow
     * bugcheck's debugger breakpoint to use normal nested entry. */
    sb zero, PcTrapActive(sp)
    mv sp, t0
    tail KiRiscvTrapStop
