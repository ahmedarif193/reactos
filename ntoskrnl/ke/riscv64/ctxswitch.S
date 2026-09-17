/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V thread context switch and first-run entry
 *
 * KiSwapContext saves the psABI callee-saved bank in a KSWITCH_FRAME on the
 * outgoing kernel stack, publishes that stack in KTHREAD.KernelStack, loads
 * the incoming thread's KernelStack and resumes it through the same frame.
 * A thread that has never run has a frame whose Ra is KiThreadStartup and a
 * KSTART_FRAME directly above it (KiInitializeContextThread).
 */

#include <asm/ksriscv64.h>

#define SSTATUS_SIE 0x2

    .section .text$KiRetireDpcListInDpcStack, "xr"
    .balign 4
    .global KiRetireDpcListInDpcStack
/*
 * VOID KiRetireDpcListInDpcStack(PKPRCB Prcb <a0>, PVOID DpcStack <a1>)
 *
 * The wrapper owns the stack transition; KiRetireDpcList preserves the
 * ordinary psABI callee-saved bank. Restore the thread stack before any
 * quantum-end scheduling or context switch is considered by the caller.
 */
KiRetireDpcListInDpcStack:
    mv t0, sp
    mv sp, a1
    addi sp, sp, -16
    sd t0, 0(sp)
    sd ra, 8(sp)
    call KiRetireDpcList
    ld t0, 0(sp)
    ld ra, 8(sp)
    mv sp, t0
    ret

    .section .text$KiSwapContext, "xr"
    .balign 4
    .global KiSwapContext
/*
 * BOOLEAN KiSwapContext(KIRQL WaitIrql <a0>, PKTHREAD OldThread <a1>)
 *
 * The scheduler has already stored the target in Prcb.CurrentThread. Returns
 * TRUE when the resumed thread must deliver kernel APCs. The interrupt
 * enable state is a processor property: it is masked across the stack switch
 * and the entry state is carried over to the incoming thread (as on amd64).
 */
KiSwapContext:
    addi sp, sp, -SwitchFrameLength
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11
    sd s\n, SwS0 + 8 * \n(sp)
    .endr
    sd ra, SwRa(sp)
    sb a0, SwApcBypass(sp)

    /* Mask interrupts for the switch; t0 keeps the entry SIE bit. */
    csrrci t0, sstatus, SSTATUS_SIE
    andi t0, t0, SSTATUS_SIE
    csrr t1, sscratch
    /* Zalrsc requires discarding the outgoing context's reservation. Entry
     * has finished, so the PCR's prologue scratch stack slot is disposable. */
    addi t2, t1, PcTrapStack
    sc.d zero, zero, (t2)
    ld a2, PcCurrentThread(t1)
    sd sp, ThKernelStack(a1)
    ld sp, ThKernelStack(a2)
    /* Kernel callers use NtCurrentTeb(), which reads tp. Carry the incoming
     * thread's TEB across a switch even while running in supervisor mode. */
    ld tp, ThTeb(a2)
    csrs sstatus, t0

    /* Now on the incoming stack: a0 = its saved WaitIrql (APC bypass). */
    lbu a0, SwApcBypass(sp)
    call KiSwapContextResume

    .irp n,0,1,2,3,4,5,6,7,8,9,10,11
    ld s\n, SwS0 + 8 * \n(sp)
    .endr
    ld ra, SwRa(sp)
    addi sp, sp, SwitchFrameLength
    ret

    .section .text$KiThreadStartup, "xr"
    .balign 4
    .global KiThreadStartup
/*
 * First instruction of every new thread. KiSwapContext has popped the
 * KSWITCH_FRAME (all s-registers zero), so sp addresses the KSTART_FRAME.
 * Lowers to APC_LEVEL and calls SystemRoutine(StartRoutine, StartContext);
 * PspSystemThreadStartup lowers to PASSIVE_LEVEL itself. The system routine
 * of a kernel thread never returns; user startup returns into its trap frame.
 */
KiThreadStartup:
    ld s1, SfSystemRoutine(sp)
    ld s2, SfStartRoutine(sp)
    ld s3, SfStartContext(sp)
    addi sp, sp, StartFrameLength
    li a0, APC_LEVEL
    call KfLowerIrql

    mv t0, s1
    mv a0, s2
    mv a1, s3
    mv s1, zero
    mv s2, zero
    mv s3, zero
    jalr t0

    /* A normal user startup returns after arranging the loader APC. The
     * helper rejects a returning system thread without a user trap frame. */
    tail KiRiscvStartUserThread
