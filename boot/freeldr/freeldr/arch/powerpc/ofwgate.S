/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Little-endian side of the Open Firmware client interface gate
 */

/*
 * LONG OfwGate(PULONG Args, ULONG BeGate, ULONG OfEntry)
 *
 * Leaves little-endian mode through rfi into the big-endian gate provided by
 * stage0, which calls the client interface with r3 = Args and resumes here
 * with MSR[LE] set and the firmware result in r3. The firmware preserves
 * r1 and r14-r31; r2 and r13 are restored here.
 */
    .text
    .p2align 2
    .globl ..OfwGate
..OfwGate:
    mflr 0
    stw 0, 8(1)
    stwu 1, -64(1)
    stw 2, 4(1)
    stw 13, 16(1)
    mfmsr 11
    stw 11, 20(1)
    mtsrr0 4
    rlwinm 11, 11, 0, 0, 30
    mtsrr1 11
    mr 4, 5
    bl 1f
1:  mflr 10
    addi 10, 10, 2f - 1b
    sync
    rfi
2:  lwz 13, 16(1)
    lwz 2, 4(1)
    addi 1, 1, 64
    lwz 0, 8(1)
    mtlr 0
    blr

    .section .rdata,"dr"
    .p2align 2
    .globl OfwGate
OfwGate:
    .long ..OfwGate
    .long .toc
