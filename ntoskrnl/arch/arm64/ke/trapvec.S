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
    mov x0, sp;                                                          \
    bl KiArm64HandleSynchronousException;                                \
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
    /* Call the C veneer for HAL-aware dispatch */                       \
    mov w0, #(id);                                                       \
    bl KiArm64InterruptDispatchEntry;                                    \
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

/* Table entries for all 16 vectors */
PERM_VECTOR_SYNC(KiArm64PermVectorSyncSp0,          0)
PERM_VECTOR_ASYNC (KiArm64PermVectorIrqSp0,           1)
PERM_VECTOR_ASYNC (KiArm64PermVectorFiqSp0,           2)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorSp0,    3)
PERM_VECTOR_SYNC(KiArm64PermVectorSyncSpx,          4)
PERM_VECTOR_ASYNC (KiArm64PermVectorIrqSpx,           5)
PERM_VECTOR_ASYNC (KiArm64PermVectorFiqSpx,           6)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorSpx,    7)
PERM_VECTOR_SYNC(KiArm64PermVectorSyncLowerA64,     8)
PERM_VECTOR_ASYNC (KiArm64PermVectorIrqLowerA64,      9)
PERM_VECTOR_ASYNC (KiArm64PermVectorFiqLowerA64,     10)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorLowerA64,  11)
PERM_VECTOR_SYNC(KiArm64PermVectorSyncLowerA32,    12)
PERM_VECTOR_ASYNC (KiArm64PermVectorIrqLowerA32,     13)
PERM_VECTOR_ASYNC (KiArm64PermVectorFiqLowerA32,     14)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorLowerA32,  15)

#undef PERM_VECTOR_SYNC
#undef PERM_VECTOR_ASYNC
#undef PERM_VECTOR_BUGCHECK
