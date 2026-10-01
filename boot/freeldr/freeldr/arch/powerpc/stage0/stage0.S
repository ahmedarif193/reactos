/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Big-endian stage0 entry, endian switch and firmware gate
 */

/* Open Firmware enters with r3/r4 = initrd, r5 = client interface. */
    .section .text.entry,"ax"
    .globl _start
_start:
    /* The flat image carries no .bss: clear it (word aligned). */
    lis 6, __bss_start@ha
    addi 6, 6, __bss_start@l
    lis 7, __stage0_end@ha
    addi 7, 7, __stage0_end@l
    li 0, 0
1:  cmplw 6, 7
    bge 2f
    stw 0, 0(6)
    addi 6, 6, 4
    b 1b
2:  lis 1, BeStackTop@ha
    addi 1, 1, BeStackTop@l
    li 0, 0
    stwu 0, -16(1)
    b Stage0Main

/* EnterLe(Code, Toc, Arg, Stack): continue at Code in little-endian mode. */
    .text
    .globl EnterLe
EnterLe:
    mtsrr0 3
    mr 2, 4
    mr 3, 5
    mr 1, 6
    mfmsr 11
    ori 11, 11, 1
    mtsrr1 11
    sync
    rfi

/*
 * Entered big-endian from the loader's OfwGate: r3 = argument array,
 * r4 = client interface, r10 = little-endian resume address. The firmware
 * preserves r1 and r14-r31, so r10 is kept on the stack.
 */
    .globl BeGate
BeGate:
    stwu 1, -32(1)
    stw 10, 16(1)
    mtctr 4
    bctrl
    lwz 10, 16(1)
    addi 1, 1, 32
    mtsrr0 10
    mfmsr 11
    ori 11, 11, 1
    mtsrr1 11
    sync
    rfi

    .section .rodata
    .balign 4096
    .globl LoaderImage, LoaderImageEnd
LoaderImage:
    .incbin STAGE0_PAYLOAD
LoaderImageEnd:

    .section .bss
    .balign 16
    .space 32768
BeStackTop:
    .globl LeStackTop
    .space 262144
LeStackTop:
