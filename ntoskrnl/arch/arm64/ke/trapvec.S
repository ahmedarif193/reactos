#include <asm.h>
#include <ksarm64.h>

    .text
    .align  11

    .extern KiArm64HandleSynchronousException
    .extern KiArm64BugCheckEntry
    .extern KiTrapReturn
    .extern KiArm64VectorLogOnly

    .global KiArm64VectorTable
KiArm64VectorTable:

.equ ARM64_EARLY_STATE_VECTORID,      0x0
.equ ARM64_EARLY_STATE_ESR,           0x8
.equ ARM64_EARLY_STATE_FAR,           0x10
.equ ARM64_EARLY_STATE_ELR,           0x18
.equ ARM64_EARLY_STATE_SPSR,          0x20
.equ ARM64_EARLY_STATE_REGS,          0x28
.equ ARM64_EARLY_STATE_REG_SP,        0x120
.equ ARM64_EARLY_STATE_REG_PC,        0x128
.equ ARM64_EARLY_STATE_REG_PSTATE,    0x130
.equ ARM64_EARLY_STATE_TRAP_PTR,      0x138
.equ ARM64_EARLY_STATE_EXC_PTR,       0x140
.equ ARM64_EARLY_STATE_TRAP_FRAME,    0x148
.equ ARM64_EARLY_STATE_SIZE,          0x380

/* Permanent table entries */
#define PERM_VECTOR_SYNC(label, id)                                      \
    .global label;                                                       \
label:                                                                   \
    stp x29, x30, [sp, #-16]!;                                           \
    mov x29, sp;                                                         \
    sub sp, sp, #ARM64_EARLY_STATE_SIZE;                                 \
    str x0,  [sp, #(ARM64_EARLY_STATE_REGS + 0 * 8)];                    \
    str x1,  [sp, #(ARM64_EARLY_STATE_REGS + 1 * 8)];                    \
    str x2,  [sp, #(ARM64_EARLY_STATE_REGS + 2 * 8)];                    \
    str x3,  [sp, #(ARM64_EARLY_STATE_REGS + 3 * 8)];                    \
    str x4,  [sp, #(ARM64_EARLY_STATE_REGS + 4 * 8)];                    \
    str x5,  [sp, #(ARM64_EARLY_STATE_REGS + 5 * 8)];                    \
    str x6,  [sp, #(ARM64_EARLY_STATE_REGS + 6 * 8)];                    \
    str x7,  [sp, #(ARM64_EARLY_STATE_REGS + 7 * 8)];                    \
    str x8,  [sp, #(ARM64_EARLY_STATE_REGS + 8 * 8)];                    \
    str x9,  [sp, #(ARM64_EARLY_STATE_REGS + 9 * 8)];                    \
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 10 * 8)];                   \
    str x11, [sp, #(ARM64_EARLY_STATE_REGS + 11 * 8)];                   \
    str x12, [sp, #(ARM64_EARLY_STATE_REGS + 12 * 8)];                   \
    str x13, [sp, #(ARM64_EARLY_STATE_REGS + 13 * 8)];                   \
    str x14, [sp, #(ARM64_EARLY_STATE_REGS + 14 * 8)];                   \
    str x15, [sp, #(ARM64_EARLY_STATE_REGS + 15 * 8)];                   \
    str x16, [sp, #(ARM64_EARLY_STATE_REGS + 16 * 8)];                   \
    str x17, [sp, #(ARM64_EARLY_STATE_REGS + 17 * 8)];                   \
    str x18, [sp, #(ARM64_EARLY_STATE_REGS + 18 * 8)];                   \
    str x19, [sp, #(ARM64_EARLY_STATE_REGS + 19 * 8)];                   \
    str x20, [sp, #(ARM64_EARLY_STATE_REGS + 20 * 8)];                   \
    str x21, [sp, #(ARM64_EARLY_STATE_REGS + 21 * 8)];                   \
    str x22, [sp, #(ARM64_EARLY_STATE_REGS + 22 * 8)];                   \
    str x23, [sp, #(ARM64_EARLY_STATE_REGS + 23 * 8)];                   \
    str x24, [sp, #(ARM64_EARLY_STATE_REGS + 24 * 8)];                   \
    str x25, [sp, #(ARM64_EARLY_STATE_REGS + 25 * 8)];                   \
    str x26, [sp, #(ARM64_EARLY_STATE_REGS + 26 * 8)];                   \
    str x27, [sp, #(ARM64_EARLY_STATE_REGS + 27 * 8)];                   \
    str x28, [sp, #(ARM64_EARLY_STATE_REGS + 28 * 8)];                   \
    ldr x9, [x29];                                                       \
    str x9, [sp, #(ARM64_EARLY_STATE_REGS + 29 * 8)];                    \
    ldr x10, [x29, #8];                                                  \
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 30 * 8)];                   \
    add x11, x29, #16;                                                   \
    str x11, [sp, #ARM64_EARLY_STATE_REG_SP];                            \
    mrs x12, elr_el1;                                                    \
    str x12, [sp, #ARM64_EARLY_STATE_ELR];                               \
    str x12, [sp, #ARM64_EARLY_STATE_REG_PC];                            \
    mrs x13, spsr_el1;                                                   \
    str x13, [sp, #ARM64_EARLY_STATE_SPSR];                              \
    str x13, [sp, #ARM64_EARLY_STATE_REG_PSTATE];                        \
    mrs x14, esr_el1;                                                    \
    str x14, [sp, #ARM64_EARLY_STATE_ESR];                               \
    mrs x15, far_el1;                                                    \
    str x15, [sp, #ARM64_EARLY_STATE_FAR];                               \
    mov w16, #(id);                                                      \
    str x16, [sp, #ARM64_EARLY_STATE_VECTORID];                          \
    str xzr, [sp, #ARM64_EARLY_STATE_TRAP_PTR];                          \
    str xzr, [sp, #ARM64_EARLY_STATE_EXC_PTR];                           \
    /* Memory barriers to ensure exception state is visible to C code */ \
    dsb sy;                                                              \
    isb;                                                                 \
    mov x0, sp;                                                          \
    bl KiArm64HandleSynchronousException;                                \
    /* Memory barrier after C handler returns */                         \
    dsb sy;                                                              \
    isb;                                                                 \
    cbz x0, 1f;                                                          \
    ldr x2, [sp, #ARM64_EARLY_STATE_TRAP_PTR];                           \
    ldr x3, [sp, #ARM64_EARLY_STATE_EXC_PTR];                            \
    add sp, sp, #ARM64_EARLY_STATE_SIZE;                                 \
    ldp x29, x30, [sp], #16;                                             \
    mov x0, x2;                                                          \
    mov x1, x3;                                                          \
    b KiTrapReturn;                                                      \
1:  mov x0, sp;                                                          \
    bl KiArm64BugCheckEntry;                                             \
    b 1b

#define PERM_VECTOR_ASYNC(label, id)                                     \
    .global label;                                                       \
label:                                                                   \
    /* Preserve volatile state for IRQ/FIQ dispatch */                   \
    sub sp, sp, #0xB0; /* 22 regs * 8 bytes */                           \
    stp x0,  x1,  [sp, #0x00];                                           \
    stp x2,  x3,  [sp, #0x10];                                           \
    stp x4,  x5,  [sp, #0x20];                                           \
    stp x6,  x7,  [sp, #0x30];                                           \
    stp x8,  x9,  [sp, #0x40];                                           \
    stp x10, x11, [sp, #0x50];                                           \
    stp x12, x13, [sp, #0x60];                                           \
    stp x14, x15, [sp, #0x70];                                           \
    stp x16, x17, [sp, #0x80];                                           \
    stp x18, x19, [sp, #0x90];                                           \
    stp x29, x30, [sp, #0xA0];                                           \
    /* Memory barrier before calling C code to ensure register saves visible */ \
    dsb sy;                                                              \
    isb;                                                                 \
    /* Call the C veneer for HAL-aware dispatch */                       \
    mov w0, #(id);                                                       \
    bl KiArm64InterruptDispatchEntry;                                    \
    /* Memory barrier before restoring to ensure interrupt handled */    \
    dsb sy;                                                              \
    isb;                                                                 \
    /* Restore volatile state */                                         \
    ldp x29, x30, [sp, #0xA0];                                           \
    ldp x18, x19, [sp, #0x90];                                           \
    ldp x16, x17, [sp, #0x80];                                           \
    ldp x14, x15, [sp, #0x70];                                           \
    ldp x12, x13, [sp, #0x60];                                           \
    ldp x10, x11, [sp, #0x50];                                           \
    ldp x8,  x9,  [sp, #0x40];                                           \
    ldp x6,  x7,  [sp, #0x30];                                           \
    ldp x4,  x5,  [sp, #0x20];                                           \
    ldp x2,  x3,  [sp, #0x10];                                           \
    ldp x0,  x1,  [sp, #0x00];                                           \
    add sp, sp, #0xB0;                                                   \
    /* Final barrier before exception return */                          \
    dsb sy;                                                              \
    isb;                                                                 \
    eret

#define PERM_VECTOR_BUGCHECK(label, id)                                  \
    .global label;                                                       \
label:                                                                   \
    stp x29, x30, [sp, #-16]!;                                           \
    mov x29, sp;                                                         \
    /* Enter bugcheck handler for SError */                              \
    mov x0, xzr;                                                         \
    bl KiArm64BugCheckEntry;                                             \
    b label

/*
 * ARM64 Exception Vector Table
 * Each entry must be 128 bytes (0x80) apart.
 * We use small stubs that branch to the full handlers.
 */

/* Vector stub macro - branches to handler, padded to 0x80 bytes */
#define VECTOR_STUB(label)    \
    .balign 0x80;             \
    b label

/* Vector table: 16 entries at 128-byte intervals */
    VECTOR_STUB(KiArm64PermVectorSyncSp0_Handler)       /* 0x000: Sync, Current EL SP0 */
    VECTOR_STUB(KiArm64PermVectorIrqSp0_Handler)        /* 0x080: IRQ, Current EL SP0 */
    VECTOR_STUB(KiArm64PermVectorFiqSp0_Handler)        /* 0x100: FIQ, Current EL SP0 */
    VECTOR_STUB(KiArm64PermVectorSErrorSp0_Handler)     /* 0x180: SError, Current EL SP0 */
    VECTOR_STUB(KiArm64PermVectorSyncSpx_Handler)       /* 0x200: Sync, Current EL SPx */
    VECTOR_STUB(KiArm64PermVectorIrqSpx_Handler)        /* 0x280: IRQ, Current EL SPx */
    VECTOR_STUB(KiArm64PermVectorFiqSpx_Handler)        /* 0x300: FIQ, Current EL SPx */
    VECTOR_STUB(KiArm64PermVectorSErrorSpx_Handler)     /* 0x380: SError, Current EL SPx */
    VECTOR_STUB(KiArm64PermVectorSyncLowerA64_Handler)  /* 0x400: Sync, Lower EL A64 */
    VECTOR_STUB(KiArm64PermVectorIrqLowerA64_Handler)   /* 0x480: IRQ, Lower EL A64 */
    VECTOR_STUB(KiArm64PermVectorFiqLowerA64_Handler)   /* 0x500: FIQ, Lower EL A64 */
    VECTOR_STUB(KiArm64PermVectorSErrorLowerA64_Handler) /* 0x580: SError, Lower EL A64 */
    VECTOR_STUB(KiArm64PermVectorSyncLowerA32_Handler)  /* 0x600: Sync, Lower EL A32 */
    VECTOR_STUB(KiArm64PermVectorIrqLowerA32_Handler)   /* 0x680: IRQ, Lower EL A32 */
    VECTOR_STUB(KiArm64PermVectorFiqLowerA32_Handler)   /* 0x700: FIQ, Lower EL A32 */
    VECTOR_STUB(KiArm64PermVectorSErrorLowerA32_Handler) /* 0x780: SError, Lower EL A32 */

#undef VECTOR_STUB

/* Full handlers - these can be any size since they're jumped to, not at fixed offsets */

/* Sync handlers */
PERM_VECTOR_SYNC(KiArm64PermVectorSyncSp0_Handler,          0)
PERM_VECTOR_SYNC(KiArm64PermVectorSyncSpx_Handler,          4)
PERM_VECTOR_SYNC(KiArm64PermVectorSyncLowerA64_Handler,     8)
PERM_VECTOR_SYNC(KiArm64PermVectorSyncLowerA32_Handler,    12)

/* SError handlers - treated like sync for debugging */
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorSp0_Handler,    3)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorSpx_Handler,    7)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorLowerA64_Handler,  11)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorLowerA32_Handler,  15)

/* Async handlers (IRQ/FIQ) */
PERM_VECTOR_ASYNC(KiArm64PermVectorIrqSp0_Handler,           1)
PERM_VECTOR_ASYNC(KiArm64PermVectorFiqSp0_Handler,           2)
PERM_VECTOR_ASYNC(KiArm64PermVectorIrqSpx_Handler,           5)
PERM_VECTOR_ASYNC(KiArm64PermVectorFiqSpx_Handler,           6)
PERM_VECTOR_ASYNC(KiArm64PermVectorIrqLowerA64_Handler,      9)
PERM_VECTOR_ASYNC(KiArm64PermVectorFiqLowerA64_Handler,     10)
PERM_VECTOR_ASYNC(KiArm64PermVectorIrqLowerA32_Handler,     13)
PERM_VECTOR_ASYNC(KiArm64PermVectorFiqLowerA32_Handler,     14)

/* Legacy symbols for external references */
.global KiArm64PermVectorSyncSp0
.set KiArm64PermVectorSyncSp0, KiArm64PermVectorSyncSp0_Handler
.global KiArm64PermVectorSyncSpx
.set KiArm64PermVectorSyncSpx, KiArm64PermVectorSyncSpx_Handler

#undef PERM_VECTOR_SYNC
#undef PERM_VECTOR_ASYNC
#undef PERM_VECTOR_BUGCHECK
