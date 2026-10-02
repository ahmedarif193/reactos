/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Switch the MMU from Open Firmware to the kernel and enter it
 */

/* PPC_HANDOFF layout (ntldr/arch/ppc/winldr.c). */
#define HO_SDR1  0
#define HO_SR    4
#define HO_IBAT  68
#define HO_DBAT  100
#define HO_MSR   132
#define HO_ENTRY 136
#define HO_TOC   140
#define HO_STACK 144
#define HO_ARG   148

/*
 * VOID PpcEnterKernel(PPPC_HANDOFF Handoff)
 *
 * The loader runs 1:1 under Open Firmware, so this code and Handoff have
 * the same virtual and physical addresses. Drop to real mode, replace BATs,
 * segment registers and SDR1, flush the TLB and rfi into the kernel.
 */
    .text
    .p2align 2
    .globl ..PpcEnterKernel
..PpcEnterKernel:
    mr 31, 3

    /* Interrupts off, then continue below in real mode (still LE). */
    mfmsr 11
    rlwinm 11, 11, 0, 17, 15        /* clear EE */
    mtmsr 11
    isync
    bl 1f
1:  mflr 12
    addi 12, 12, 2f - 1b
    rlwinm 11, 11, 0, 28, 25        /* clear IR and DR */
    mtsrr0 12
    mtsrr1 11
    sync
    rfi

2:  /* Invalidate every BAT before loading new ones. */
    li 0, 0
    mtspr 528, 0                    /* IBAT0U */
    mtspr 530, 0
    mtspr 532, 0
    mtspr 534, 0
    mtspr 536, 0                    /* DBAT0U */
    mtspr 538, 0
    mtspr 540, 0
    mtspr 542, 0
    isync

    lwz 4, HO_IBAT + 4(31)
    mtspr 529, 4                    /* IBAT0L */
    lwz 4, HO_IBAT + 0(31)
    mtspr 528, 4
    lwz 4, HO_IBAT + 12(31)
    mtspr 531, 4
    lwz 4, HO_IBAT + 8(31)
    mtspr 530, 4
    lwz 4, HO_IBAT + 20(31)
    mtspr 533, 4
    lwz 4, HO_IBAT + 16(31)
    mtspr 532, 4
    lwz 4, HO_IBAT + 28(31)
    mtspr 535, 4
    lwz 4, HO_IBAT + 24(31)
    mtspr 534, 4

    lwz 4, HO_DBAT + 4(31)
    mtspr 537, 4                    /* DBAT0L */
    lwz 4, HO_DBAT + 0(31)
    mtspr 536, 4
    lwz 4, HO_DBAT + 12(31)
    mtspr 539, 4
    lwz 4, HO_DBAT + 8(31)
    mtspr 538, 4
    lwz 4, HO_DBAT + 20(31)
    mtspr 541, 4
    lwz 4, HO_DBAT + 16(31)
    mtspr 540, 4
    lwz 4, HO_DBAT + 28(31)
    mtspr 543, 4
    lwz 4, HO_DBAT + 24(31)
    mtspr 542, 4
    isync

    lwz 4, HO_SR + 0(31)
    mtsr 0, 4
    lwz 4, HO_SR + 4(31)
    mtsr 1, 4
    lwz 4, HO_SR + 8(31)
    mtsr 2, 4
    lwz 4, HO_SR + 12(31)
    mtsr 3, 4
    lwz 4, HO_SR + 16(31)
    mtsr 4, 4
    lwz 4, HO_SR + 20(31)
    mtsr 5, 4
    lwz 4, HO_SR + 24(31)
    mtsr 6, 4
    lwz 4, HO_SR + 28(31)
    mtsr 7, 4
    lwz 4, HO_SR + 32(31)
    mtsr 8, 4
    lwz 4, HO_SR + 36(31)
    mtsr 9, 4
    lwz 4, HO_SR + 40(31)
    mtsr 10, 4
    lwz 4, HO_SR + 44(31)
    mtsr 11, 4
    lwz 4, HO_SR + 48(31)
    mtsr 12, 4
    lwz 4, HO_SR + 52(31)
    mtsr 13, 4
    lwz 4, HO_SR + 56(31)
    mtsr 14, 4
    lwz 4, HO_SR + 60(31)
    mtsr 15, 4

    lwz 4, HO_SDR1(31)
    sync
    mtsdr1 4
    isync

    /* Flush every TLB congruence class. */
    li 4, 256
    mtctr 4
    li 4, 0
3:  tlbie 4
    addi 4, 4, 0x1000
    bdnz 3b
    sync
    isync

    /* The stack is a KSEG0 address: no store through it in real mode.
     * The loader zeroed it, so its back chain is already terminated. */
    lwz 1, HO_STACK(31)
    lwz 2, HO_TOC(31)
    lwz 3, HO_ARG(31)
    lwz 4, HO_ENTRY(31)
    lwz 5, HO_MSR(31)
    mtsrr0 4
    mtsrr1 5
    li 4, 0
    li 5, 0
    li 31, 0
    sync
    rfi

    .section .rdata,"dr"
    .p2align 2
    .globl PpcEnterKernel
PpcEnterKernel:
    .long ..PpcEnterKernel
    .long .toc
