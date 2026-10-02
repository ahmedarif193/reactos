/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC interrupt vectors, trap entry and exit
 *
 * KiPpcVectorBlob is copied to physical address 0 by KiPpcInstallVectors.
 * The processor enters it little-endian (MSR[ILE]) in real mode with
 * external interrupts off. SPRG0 holds the KSEG0 address of the PCR, SPRG1
 * its physical address; SPRG2 carries the interrupted r1 into the trap path
 * and SPRG3 carries it through a hashed page table reload.
 *
 * A DSI or ISI that only misses the hashed page table is resolved in real
 * mode from the software page tables and never reaches C. Every other
 * interrupt stores the interrupted r1-r4, CR, XER, SRR0 and SRR1 in the PCR
 * and continues at KiPpcTrapEntry with translation on. That code builds a
 * KTRAP_FRAME on the kernel stack; a reload miss taken while it writes the
 * frame is resolved by the nested fast path, which keeps its own scratch.
 *
 * On the way out, KiPpcRestoreTrapFrame reloads everything from the frame
 * and stages the last registers in the PCR. The PCR is in KSEG0 (a BAT),
 * so the final loads after SRR0/SRR1 are written cannot fault.
 *
 * The hashed page table is big-endian: the MMU reads it independently of
 * MSR[LE]. With true little-endian data accesses (QEMU) its words are
 * therefore stored and loaded byte-reversed.
 */

#include <asm/ksppc.h>
#include <asm/kxppc.h>

#define MSR_EE          0x8000
#define MSR_PR          0x4000
#define KERNEL_MSR_HIGH 0x0001      /* ILE */
#define KERNEL_MSR_LOW  0x3033      /* FP | ME | IR | DR | RI | LE */

/* PcReloadScratch slots. */
#define RS_R2   0
#define RS_R3   4
#define RS_R4   8
#define RS_R5   12
#define RS_R6   16
#define RS_R7   20
#define RS_CR   24
#define RS_VEC  28

/* PcTrapScratch slots. */
#define TS_R1   0
#define TS_R2   4
#define TS_R3   8
#define TS_R4   12
#define TS_CR   16
#define TS_SRR0 20
#define TS_SRR1 24
#define TS_XER  28

/* A C frame below the trap frame: back chain, TOC and LR save, and the
 * callee's eight-word parameter home area. */
#define C_FRAME 64

    .section .text$KiPpcVectorBlob, "xr"
    .balign 4096
    .globl KiPpcVectorBlob
    .globl KiPpcVectorBlobEnd
KiPpcVectorBlob:

/* Every generic vector: park r1, save r2-r4 and CR, and join the common
 * real-mode path with the vector number in r3. */
.macro GENERIC_VECTOR Vector
    .org KiPpcVectorBlob + \Vector
    mtsprg 2, 1
    mfsprg 1, 1
    stw 2, PcTrapScratch + TS_R2(1)
    stw 3, PcTrapScratch + TS_R3(1)
    stw 4, PcTrapScratch + TS_R4(1)
    mfcr 2
    stw 2, PcTrapScratch + TS_CR(1)
    li 3, \Vector
    b KiPpcRealTrapCommon
.endm

/* DSI and ISI: try the reload first. r5 carries the store flag, r3 the
 * effective address. */
.macro RELOAD_VECTOR Vector
    .org KiPpcVectorBlob + \Vector
    mtsprg 3, 1
    mfsprg 1, 1
    stw 2, PcReloadScratch + RS_R2(1)
    stw 3, PcReloadScratch + RS_R3(1)
    stw 4, PcReloadScratch + RS_R4(1)
    stw 5, PcReloadScratch + RS_R5(1)
    stw 6, PcReloadScratch + RS_R6(1)
    stw 7, PcReloadScratch + RS_R7(1)
    mfcr 2
    stw 2, PcReloadScratch + RS_CR(1)
    li 2, \Vector
    stw 2, PcReloadScratch + RS_VEC(1)
.if \Vector == 0x300
    mfdsisr 2
    andis. 3, 2, 0x4000         /* translation not found */
    beq KiPpcReloadFail
    rlwinm 5, 2, 7, 31, 31      /* DSISR store bit (0x02000000) -> 1 */
    mfdar 3
.else
    mfsrr1 2
    andis. 3, 2, 0x4000
    beq KiPpcReloadFail
    li 5, 0
    mfsrr0 3
.endif
    b KiPpcHashReload
.endm

    GENERIC_VECTOR 0x100
    GENERIC_VECTOR 0x200
    RELOAD_VECTOR 0x300
    RELOAD_VECTOR 0x400
    GENERIC_VECTOR 0x500
    GENERIC_VECTOR 0x600
    GENERIC_VECTOR 0x700
    GENERIC_VECTOR 0x800
    GENERIC_VECTOR 0x900
    GENERIC_VECTOR 0xA00
    GENERIC_VECTOR 0xB00
    GENERIC_VECTOR 0xC00
    GENERIC_VECTOR 0xD00
    GENERIC_VECTOR 0xE00
    GENERIC_VECTOR 0xF00
    GENERIC_VECTOR 0x1000
    GENERIC_VECTOR 0x1100
    GENERIC_VECTOR 0x1200
    GENERIC_VECTOR 0x1300
    GENERIC_VECTOR 0x1400

    .org KiPpcVectorBlob + 0x1500

/*
 * Real-mode hashed page table reload.
 * r1 = PCR (physical), r3 = effective address, r5 = 1 for a store.
 * r2, r4, r6, r7 and CR are free. On success the entry is inserted and the
 * access is retried; otherwise KiPpcReloadFail takes the slow path.
 */
KiPpcHashReload:
    /* Root entry: VA[31:30]. Every table is physical memory. */
    lwz 4, PcPageTableRoot(1)
    rlwinm 6, 3, 5, 27, 28
    lwzx 4, 4, 6
    andi. 6, 4, 1
    beq KiPpcReloadFail
    rlwinm 4, 4, 0, 0, 19
    /* Middle entry: VA[29:21]. */
    rlwinm 6, 3, 14, 20, 28
    lwzx 4, 4, 6
    andi. 6, 4, 1
    beq KiPpcReloadFail
    rlwinm 4, 4, 0, 0, 19
    /* Leaf entry: VA[20:12]. r7 keeps its address for the A/D update. */
    rlwinm 6, 3, 23, 20, 28
    add 7, 4, 6
    lwz 4, 0(7)
    andi. 6, 4, 1
    beq KiPpcReloadFail
    /* Problem-state access to a supervisor page is the memory manager's. */
    mfsrr1 6
    andi. 6, 6, MSR_PR
    beq 1f
    andi. 6, 4, 4
    beq KiPpcReloadFail
1:  /* A store to a read-only leaf (copy-on-write, protection) likewise. */
    cmpwi 5, 0
    beq 2f
    andi. 6, 4, 2
    beq KiPpcReloadFail
2:  /* Record the access. A writable page is recorded dirty when it enters
     * the hashed table writable. */
    ori 4, 4, 0x20
    andi. 6, 4, 2
    beq 3f
    ori 4, 4, 0x40
3:  stw 4, 0(7)

    /* Second word: RPN | R | C | WIMG | PP. */
    rlwinm 7, 4, 0, 0, 19
    ori 7, 7, 0x180
    andi. 6, 4, 0x10            /* CacheDisable -> I | G */
    beq 4f
    ori 7, 7, 0x28
4:  andi. 6, 4, 0x08            /* WriteThrough -> W */
    beq 5f
    ori 7, 7, 0x40
    /* Segments carry Ks = 0 and Kp = 1, so no PP value makes a page
     * read-only to supervisor code yet inaccessible to user code. Kernel
     * pages therefore enter with PP 00 and are supervisor-writable once in
     * the table; the store check above enforces read-only only on a miss. */
5:  andi. 6, 4, 4               /* Owner: PP 10 (RW) or 11 (RO); else 00 */
    beq 6f
    andi. 6, 4, 2
    li 6, 3
    beq 7f
    li 6, 2
7:  or 7, 7, 6
6:
    /* First word: V | VSID << 7 | API. r5 is free from here on. */
    mfsrin 4, 3
    rlwinm 5, 4, 0, 13, 31      /* low 19 VSID bits for the hash */
    rlwinm 4, 4, 7, 1, 24
    rlwinm 6, 3, 10, 26, 31
    or 4, 4, 6
    oris 4, 4, 0x8000
    rlwinm 6, 3, 20, 16, 31     /* page index */
    xor 5, 5, 6                 /* primary hash */

    /* The effective address is no longer needed: r3 counts the loops, since
     * CTR and XER belong to the interrupted code and are not saved here. */

    /* Primary PTEG: the first invalid slot. */
    lwz 6, PcHashTableMask(1)
    and 6, 5, 6
    slwi 6, 6, 6
    lwz 2, PcHashTable(1)
    or 2, 2, 6
    li 3, 8
8:  lwbrx 6, 0, 2
    andis. 6, 6, 0x8000
    beq KiPpcReloadInsert
    addi 2, 2, 8
    subi 3, 3, 1
    cmpwi 3, 0
    bne 8b

    /* Secondary PTEG with H set. */
    not 5, 5
    lwz 6, PcHashTableMask(1)
    and 6, 5, 6
    slwi 6, 6, 6
    lwz 2, PcHashTable(1)
    or 2, 2, 6
    ori 4, 4, 0x40
    li 3, 8
9:  lwbrx 6, 0, 2
    andis. 6, 6, 0x8000
    beq KiPpcReloadInsert
    addi 2, 2, 8
    subi 3, 3, 1
    cmpwi 3, 0
    bne 9b

    /* Both full: evict a round-robin slot of the secondary group. The
     * victim's page index is not kept, so purge every TLB congruence
     * class (the 604 selects the class from EA[14:19]). */
    subi 2, 2, 64
    lwz 6, PcHashReplace(1)
    addi 6, 6, 1
    stw 6, PcHashReplace(1)
    rlwinm 6, 6, 3, 26, 28
    add 2, 2, 6
    li 6, 0
    stwbrx 6, 0, 2
    sync
    li 3, 64
    li 6, 0
10: tlbie 6
    addi 6, 6, 0x1000
    subi 3, 3, 1
    cmpwi 3, 0
    bne 10b
    sync

KiPpcReloadInsert:
    addi 6, 2, 4
    stwbrx 7, 0, 6
    eieio
    stwbrx 4, 0, 2
    sync
    /* Keep MSR[ILE] across the rfi (some implementations drop it from
     * SRR1). */
    mfsrr1 2
    oris 2, 2, KERNEL_MSR_HIGH
    mtsrr1 2
    lwz 2, PcReloadScratch + RS_CR(1)
    mtcrf 0xFF, 2
    lwz 2, PcReloadScratch + RS_R2(1)
    lwz 3, PcReloadScratch + RS_R3(1)
    lwz 4, PcReloadScratch + RS_R4(1)
    lwz 5, PcReloadScratch + RS_R5(1)
    lwz 6, PcReloadScratch + RS_R6(1)
    lwz 7, PcReloadScratch + RS_R7(1)
    mfsprg 1, 3
    rfi

/* The reload does not apply: rebuild the generic vector state and trap. */
KiPpcReloadFail:
    mfsprg 2, 3
    mtsprg 2, 2
    lwz 2, PcReloadScratch + RS_R2(1)
    stw 2, PcTrapScratch + TS_R2(1)
    lwz 2, PcReloadScratch + RS_R3(1)
    stw 2, PcTrapScratch + TS_R3(1)
    lwz 2, PcReloadScratch + RS_R4(1)
    stw 2, PcTrapScratch + TS_R4(1)
    lwz 2, PcReloadScratch + RS_CR(1)
    stw 2, PcTrapScratch + TS_CR(1)
    lwz 5, PcReloadScratch + RS_R5(1)
    lwz 6, PcReloadScratch + RS_R6(1)
    lwz 7, PcReloadScratch + RS_R7(1)
    lwz 3, PcReloadScratch + RS_VEC(1)

/*
 * Common real-mode trap path. r1 = PCR (physical), r3 = vector, SPRG2 = the
 * interrupted r1; r2-r4 and CR are in PcTrapScratch. Record the remaining
 * interrupt state and resume at KiPpcTrapEntry with translation on.
 */
KiPpcRealTrapCommon:
    stw 3, PcTrapVector(1)
    mfsprg 2, 2
    stw 2, PcTrapScratch + TS_R1(1)
    mfsrr0 2
    stw 2, PcTrapScratch + TS_SRR0(1)
    mfsrr1 2
    stw 2, PcTrapScratch + TS_SRR1(1)
    mfxer 2
    stw 2, PcTrapScratch + TS_XER(1)
    mfdar 2
    stw 2, PcTrapDar(1)
    mfdsisr 2
    stw 2, PcTrapDsisr(1)
    lis 2, ..KiPpcTrapEntry@ha
    addi 2, 2, ..KiPpcTrapEntry@l
    mtsrr0 2
    lis 2, KERNEL_MSR_HIGH
    ori 2, 2, KERNEL_MSR_LOW
    mtsrr1 2
    rfi

KiPpcVectorBlobEnd:

/*
 * Translation is on, external interrupts are off. Build the KTRAP_FRAME on
 * the kernel stack: the interrupted one for a kernel-mode trap, the thread's
 * initial stack for a problem-state trap.
 */
    .section .text$KiPpcTrapEntry, "xr"
    .balign 4
    .globl ..KiPpcTrapEntry
..KiPpcTrapEntry:
    mfsprg 1, 0
    lwz 3, PcTrapScratch + TS_SRR1(1)
    andi. 3, 3, MSR_PR
    lwz 4, PcTrapScratch + TS_R1(1)
    beq 1f
    lwz 4, PcInitialStack(1)
    b 2f
1:  /* Leave room below the interrupted kernel stack pointer for a leaf that
     * stores beneath it. */
    subi 4, 4, 256
2:  subi 4, 4, TrapFrameLength
    rlwinm 4, 4, 0, 0, 27

    /* r4 = trap frame, r1 = PCR. Nonvolatile and scratch registers go in
     * directly; r1-r4 and the interrupt state come from the PCR. */
    stw 0, CxGpr0(4)
    .irp n,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    stw \n, CxGpr0 + 4 * \n(4)
    .endr
    lwz 3, PcTrapScratch + TS_R1(1)
    stw 3, CxGpr1(4)
    lwz 3, PcTrapScratch + TS_R2(1)
    stw 3, CxGpr2(4)
    lwz 3, PcTrapScratch + TS_R3(1)
    stw 3, CxGpr3(4)
    lwz 3, PcTrapScratch + TS_R4(1)
    stw 3, CxGpr0 + 16(4)
    lwz 3, PcTrapScratch + TS_CR(1)
    stw 3, CxCr(4)
    lwz 3, PcTrapScratch + TS_XER(1)
    stw 3, CxXer(4)
    lwz 3, PcTrapScratch + TS_SRR0(1)
    stw 3, CxIar(4)
    lwz 3, PcTrapScratch + TS_SRR1(1)
    stw 3, CxMsr(4)
    mflr 3
    stw 3, CxLr(4)
    mfctr 3
    stw 3, CxCtr(4)
    lwz 3, PcTrapVector(1)
    stw 3, TfVector(4)
    lwz 3, PcTrapDar(1)
    stw 3, TfDar(4)
    lwz 3, PcTrapDsisr(1)
    stw 3, TfDsisr(4)
    lbz 3, PcIrql(1)
    stb 3, TfPreviousIrql(4)
    li 3, CONTEXT_FULL
    stw 3, CxContextFlags(4)
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    stfd \n, CxFpr0 + 8 * \n(4)
    .endr
    mffs 0
    stfd 0, CxFpscr(4)

    /* Call KiPpcTrapDispatch(TrapFrame) on the stack below the frame. */
    mr 31, 4
    subi 1, 4, C_FRAME
    li 0, 0
    stw 0, 0(1)
    lis 2, KiPpcTrapDispatch@ha
    lwz 2, KiPpcTrapDispatch@l + 4(2)
    mr 3, 31
    bl ..KiPpcTrapDispatch
    mr 3, 31
    b ..KiPpcRestoreTrapFrame
    .globl KiPpcTrapEntryEnd
KiPpcTrapEntryEnd:

/*
 * DECLSPEC_NORETURN VOID KiPpcRestoreTrapFrame(PKTRAP_FRAME TrapFrame <r3>)
 */
LEAF_ENTRY(KiPpcRestoreTrapFrame)
    mfmsr 0
    rlwinm 0, 0, 0, 17, 15      /* clear EE */
    mtmsr 0
    isync
    mfsprg 4, 0

    /* Stage the registers that are live across the final rfi. */
    lwz 5, CxGpr1(3)
    stw 5, PcExitScratch + 0(4)
    lwz 5, CxGpr2(3)
    stw 5, PcExitScratch + 4(4)
    lwz 5, CxGpr3(3)
    stw 5, PcExitScratch + 8(4)
    lwz 5, CxGpr0 + 16(3)
    stw 5, PcExitScratch + 12(4)
    lwz 5, CxCr(3)
    stw 5, PcExitScratch + 16(4)
    lwz 5, CxIar(3)
    stw 5, PcExitScratch + 20(4)
    lwz 5, CxMsr(3)
    stw 5, PcExitScratch + 24(4)

    lfd 0, CxFpscr(3)
    mtfsf 0xFF, 0
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    lfd \n, CxFpr0 + 8 * \n(3)
    .endr
    lwz 5, CxLr(3)
    mtlr 5
    lwz 5, CxCtr(3)
    mtctr 5
    lwz 5, CxXer(3)
    mtxer 5
    lwz 0, CxGpr0(3)
    .irp n,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    lwz \n, CxGpr0 + 4 * \n(3)
    .endr

    /* From here on only the PCR is read: no translation miss can clobber
     * SRR0/SRR1. */
    mfsprg 1, 0
    lwz 2, PcExitScratch + 20(1)
    mtsrr0 2
    lwz 2, PcExitScratch + 24(1)
    oris 2, 2, KERNEL_MSR_HIGH
    mtsrr1 2
    lwz 2, PcExitScratch + 16(1)
    mtcrf 0xFF, 2
    lwz 2, PcExitScratch + 4(1)
    lwz 3, PcExitScratch + 8(1)
    lwz 4, PcExitScratch + 12(1)
    lwz 1, PcExitScratch + 0(1)
    rfi
LEAF_END(KiPpcRestoreTrapFrame)
