/*
 * PROJECT:     FreeLoader for ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Cache Management Functions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* ARM64 Cache Management - GNU AS syntax */
.text
.align 2

/* VOID InvalidateICache(VOID) */
.global InvalidateICache
/* .type InvalidateICache, %function */
InvalidateICache:
    ic iallu                    /* Invalidate all instruction caches */
    dsb sy                      /* Data synchronization barrier */
    isb                         /* Instruction synchronization barrier */
    ret

/* VOID InvalidateDCache(VOID) */
.global InvalidateDCache
/* .type InvalidateDCache, %function */
InvalidateDCache:
    dmb sy                      /* Data memory barrier */
    
    /* Get cache level */
    mrs x0, clidr_el1
    and w3, w0, #0x07000000     /* Get Level of Coherency */
    lsr w3, w3, #23
    cbz w3, finished
    mov w10, #0                 /* w10 = cache level */
    mov w8, #1

loop_level:
    add w2, w10, w10, lsl #1    /* Calculate 3 * cache level */
    lsr w1, w0, w2              /* Get cache type for this level */
    and w1, w1, #0x7
    cmp w1, #2
    b.lt next_level             /* No cache or instruction only */
    
    msr csselr_el1, x10         /* Select cache level */
    isb
    
    mrs x1, ccsidr_el1          /* Read cache size ID register */
    and w2, w1, #0x7
    add w2, w2, #4              /* Line size offset */
    mov w4, #0x3ff
    and w4, w4, w1, lsr #3      /* Number of ways - 1 */
    clz w5, w4
    mov w7, #0x00007fff
    and w7, w7, w1, lsr #13     /* Number of sets - 1 */

loop_set:
    mov w9, w4                  /* w9 = way counter */

loop_way:
    lsl w11, w9, w5
    orr w11, w10, w11
    lsl w6, w7, w2
    orr w11, w11, w6            /* Combine set/way/level */
    dc cisw, x11                /* Clean & invalidate by set/way */
    subs w9, w9, #1
    b.ge loop_way
    subs w7, w7, #1
    b.ge loop_set

next_level:
    add w10, w10, #2
    cmp w3, w10
    dsb sy
    b.gt loop_level

finished:
    mov x10, #0
    msr csselr_el1, x10
    dsb sy
    isb
    ret

/* VOID EnableCaches(VOID) */
.global EnableCaches
/* .type EnableCaches, %function */
EnableCaches:
    mrs x0, sctlr_el1
    orr x0, x0, #(1 << 2)       /* Enable D-cache */
    orr x0, x0, #(1 << 12)      /* Enable I-cache */
    msr sctlr_el1, x0
    isb
    ret

/* VOID DisableCaches(VOID) */
.global DisableCaches
/* .type DisableCaches, %function */
DisableCaches:
    mrs x0, sctlr_el1
    bic x0, x0, #(1 << 2)       /* Disable D-cache */
    bic x0, x0, #(1 << 12)      /* Disable I-cache */
    msr sctlr_el1, x0
    isb
    ret
