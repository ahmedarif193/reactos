/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC thread context switch and first-run entry
 *
 * KiSwapContext saves the nonvolatile bank (f14-f31, r14-r31, CR, LR, r2) in
 * a KSWITCH_FRAME on the outgoing kernel stack, publishes that stack in
 * KTHREAD.KernelStack, loads the incoming thread's KernelStack and resumes it
 * through the same frame. A thread that has never run has a frame whose Lr
 * is ..KiThreadStartup and a KSTART_FRAME directly above it.
 */

#include <asm/ksppc.h>
#include <asm/kxppc.h>

/* Back chain, TOC and LR save and the parameter home area of a callee. */
#define C_FRAME 64

/*
 * VOID KiRetireDpcListInDpcStack(PKPRCB Prcb <r3>, PVOID DpcStack <r4>)
 */
LEAF_ENTRY(KiRetireDpcListInDpcStack)
    mflr 0
    stw 0, 8(1)
    mr 11, 1
    subi 1, 4, C_FRAME
    stw 11, 0(1)
    bl ..KiRetireDpcList
    lwz 1, 0(1)
    lwz 0, 8(1)
    mtlr 0
    blr
LEAF_END(KiRetireDpcListInDpcStack)

/*
 * BOOLEAN KiSwapContext(KIRQL WaitIrql <r3>, PKTHREAD OldThread <r4>)
 *
 * The scheduler has already stored the target in Prcb.CurrentThread.
 * Returns TRUE when the resumed thread must deliver kernel APCs. The
 * interrupt enable state carries over to the incoming thread.
 */
LEAF_ENTRY(KiSwapContext)
    mflr 0
    subi 1, 1, SwitchFrameLength
    stw 0, SwLr(1)
    .irp n,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    stfd \n, SwFpr14 + 8 * (\n - 14)(1)
    stw \n, SwGpr14 + 4 * (\n - 14)(1)
    .endr
    mfcr 0
    stw 0, SwCr(1)
    stw 2, SwGpr2(1)
    stb 3, SwApcBypass(1)

    /* Mask interrupts for the switch; r11 keeps the entry MSR. */
    mfmsr 11
    rlwinm 0, 11, 0, 17, 15
    mtmsr 0
    isync
    mfsprg 12, 0
    lwz 5, PcCurrentThread(12)
    stw 1, ThKernelStack(4)
    lwz 1, ThKernelStack(5)

    /* Traps from the incoming thread's user mode land on its initial
     * stack; kernel callers of NtCurrentTeb read r13. */
    lwz 6, ThInitialStack(5)
    stw 6, PcInitialStack(12)
    lwz 13, ThTeb(5)
    mtmsr 11
    isync

    /* On the incoming stack: KiSwapContextResume(ApcBypass, Old, New). */
    lbz 3, SwApcBypass(1)
    mr 31, 1
    subi 1, 1, C_FRAME
    stw 31, 0(1)
    bl ..KiSwapContextResume
    addi 1, 1, C_FRAME

    lwz 0, SwCr(1)
    mtcrf 0xFF, 0
    .irp n,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    lfd \n, SwFpr14 + 8 * (\n - 14)(1)
    lwz \n, SwGpr14 + 4 * (\n - 14)(1)
    .endr
    lwz 2, SwGpr2(1)
    lwz 0, SwLr(1)
    mtlr 0
    addi 1, 1, SwitchFrameLength
    blr
LEAF_END(KiSwapContext)

/*
 * First instruction of every new thread. KiSwapContext has popped the
 * KSWITCH_FRAME, so r1 addresses the KSTART_FRAME. Lowers to APC_LEVEL and
 * calls SystemRoutine(StartRoutine, StartContext) through its descriptor.
 * A kernel thread's system routine never returns; user startup returns and
 * enters user mode.
 */
LEAF_ENTRY(KiThreadStartup)
    lwz 14, SfSystemRoutine(1)
    lwz 15, SfStartRoutine(1)
    lwz 16, SfStartContext(1)
    addi 1, 1, StartFrameLength
    subi 1, 1, C_FRAME
    li 0, 0
    stw 0, 0(1)
    li 3, APC_LEVEL
    bl ..KfLowerIrql

    lwz 0, 0(14)
    lwz 2, 4(14)
    mtctr 0
    mr 3, 15
    mr 4, 16
    li 14, 0
    li 15, 0
    li 16, 0
    bctrl
    b ..KiPpcStartUserThread
LEAF_END(KiThreadStartup)
