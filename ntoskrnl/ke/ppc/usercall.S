/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC user-mode callout entry and return
 */

#include <asm/ksppc.h>
#include <asm/kxppc.h>

#define C_FRAME 64

/*
 * NTSTATUS KiPpcCallUserMode(PKTRAP_FRAME Frame <r3>, PVOID *OutputBuffer <r4>,
 *                            PULONG OutputLength <r5>)
 *
 * Suspends the kernel call in a KCALLOUT_FRAME and enters user mode through
 * KiPpcUserModeCallout. NtCallbackReturn resumes here through
 * KiPpcCallbackReturn; a failed preparation returns directly.
 */
LEAF_ENTRY(KiPpcCallUserMode)
    mflr 0
    subi 1, 1, CalloutFrameLength
    stw 0, CoLr(1)
    .irp n,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    stfd \n, CoFpr14 + 8 * (\n - 14)(1)
    stw \n, CoGpr14 + 4 * (\n - 14)(1)
    .endr
    mfcr 0
    stw 0, CoCr(1)
    stw 2, CoGpr2(1)
    mfmsr 0
    stw 0, CoMsr(1)
    stw 4, CoOutputBuffer(1)
    stw 5, CoOutputLength(1)
    mr 4, 1
    mr 31, 1
    subi 1, 1, C_FRAME
    stw 31, 0(1)
    bl ..KiPpcUserModeCallout
    addi 1, 1, C_FRAME
    b 1f

/*
 * DECLSPEC_NORETURN VOID KiPpcCallbackReturn(PKCALLOUT_FRAME CalloutFrame <r3>,
 *                                            NTSTATUS Status <r4>)
 *
 * NtCallbackReturn restored the thread links with interrupts disabled. Keep
 * them disabled until the suspended stack and register bank agree.
 */
    .globl ..KiPpcCallbackReturn
..KiPpcCallbackReturn:
    mr 1, 3
    mr 3, 4
1:  mfmsr 0
    rlwinm 0, 0, 0, 17, 15
    mtmsr 0
    isync
    lwz 0, CoCr(1)
    mtcrf 0xFF, 0
    .irp n,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    lfd \n, CoFpr14 + 8 * (\n - 14)(1)
    lwz \n, CoGpr14 + 4 * (\n - 14)(1)
    .endr
    lwz 2, CoGpr2(1)
    lwz 0, CoLr(1)
    mtlr 0
    lwz 0, CoMsr(1)
    addi 1, 1, CalloutFrameLength
    mtmsr 0
    isync
    blr
LEAF_END(KiPpcCallUserMode)

    .section .rdata,"dr"
    .p2align 2
    .globl KiPpcCallbackReturn
KiPpcCallbackReturn:
    .long ..KiPpcCallbackReturn
    .long .toc
    .text
