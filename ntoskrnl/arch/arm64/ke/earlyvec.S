#include <asm.h>
#include <ksarm64.h>

    .text
    .align 11

    .extern KiArm64EarlyVectorHandler
    .extern KiArm64HandleSynchronousException
    .extern KiArm64BugCheckEntry
    .extern KiTrapReturn
    .global KiArm64EarlyVectorTable
KiArm64EarlyVectorTable:
#define VECTOR_ENTRY(label) \
    b label;                \
    .rept 7; nop; .endr

    VECTOR_ENTRY(KiArm64VectorSyncSp0)
    VECTOR_ENTRY(KiArm64VectorIrqSp0)
    VECTOR_ENTRY(KiArm64VectorFiqSp0)
    VECTOR_ENTRY(KiArm64VectorSErrorSp0)
    VECTOR_ENTRY(KiArm64VectorSyncSpx)
    VECTOR_ENTRY(KiArm64VectorIrqSpx)
    VECTOR_ENTRY(KiArm64VectorFiqSpx)
    VECTOR_ENTRY(KiArm64VectorSErrorSpx)
    VECTOR_ENTRY(KiArm64VectorSyncLowerA64)
    VECTOR_ENTRY(KiArm64VectorIrqLowerA64)
    VECTOR_ENTRY(KiArm64VectorFiqLowerA64)
    VECTOR_ENTRY(KiArm64VectorSErrorLowerA64)
    VECTOR_ENTRY(KiArm64VectorSyncLowerA32)
    VECTOR_ENTRY(KiArm64VectorIrqLowerA32)
    VECTOR_ENTRY(KiArm64VectorFiqLowerA32)
    VECTOR_ENTRY(KiArm64VectorSErrorLowerA32)

#undef VECTOR_ENTRY

    .align 2

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

#define VECTOR_HANDLER_SYNC(label, id)                                   \
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
    mov x19, sp;                                                         \
    bl KiArm64HandleSynchronousException;                                \
    cbz x0, KiArm64VectorBugCheck##id;                                   \
    ldr x2, [sp, #ARM64_EARLY_STATE_TRAP_PTR];                           \
    ldr x3, [sp, #ARM64_EARLY_STATE_EXC_PTR];                            \
    add sp, sp, #ARM64_EARLY_STATE_SIZE;                                 \
    ldp x29, x30, [sp], #16;                                             \
    mov x0, x2;                                                          \
    mov x1, x3;                                                          \
    bl KiTrapReturn;                                                     \
KiArm64VectorHang##id:                                                   \
    wfe;                                                                \
    b KiArm64VectorHang##id;                                             \
KiArm64VectorBugCheck##id:                                               \
    mov x0, x19;                                                         \
    bl KiArm64BugCheckEntry;                                             \
    b KiArm64VectorHang##id

#define VECTOR_HANDLER_DEFAULT(label, id)                                \
    .global label;                                                       \
label:                                                                   \
    stp x29, x30, [sp, #-16]!;                                           \
    mov x29, sp;                                                         \
    sub sp, sp, #40;                                                     \
    stp x0, x1, [sp, #0];                                                \
    stp x2, x3, [sp, #16];                                               \
    add x4, sp, #40;                                                     \
    str x4, [sp, #32];                                                   \
    mov x3, sp;                                                          \
    mrs x1, esr_el1;                                                     \
    mrs x2, far_el1;                                                     \
    mov w0, #(id);                                                       \
    bl KiArm64EarlyVectorHandler;                                        \
    /* If final vectors are active, return instead of hanging */         \
    ldr x9, =KiArm64FinalVectorsInstalled;                               \
    ldrb w9, [x9];                                                       \
    cbz w9, 1f;                                                          \
    mov sp, x29;                                                         \
    ldp x29, x30, [sp], #16;                                             \
    eret;                                                                \
1:  mov sp, x29;                                                         \
    ldp x29, x30, [sp], #16;                                             \
KiArm64VectorHangDefault##id:                                            \
    wfe;                                                                \
    b KiArm64VectorHangDefault##id

VECTOR_HANDLER_SYNC(KiArm64VectorSyncSp0,          0)
VECTOR_HANDLER_DEFAULT(KiArm64VectorIrqSp0,        1)
VECTOR_HANDLER_DEFAULT(KiArm64VectorFiqSp0,        2)
VECTOR_HANDLER_DEFAULT(KiArm64VectorSErrorSp0,     3)
VECTOR_HANDLER_SYNC(KiArm64VectorSyncSpx,          4)
VECTOR_HANDLER_DEFAULT(KiArm64VectorIrqSpx,        5)
VECTOR_HANDLER_DEFAULT(KiArm64VectorFiqSpx,        6)
VECTOR_HANDLER_DEFAULT(KiArm64VectorSErrorSpx,     7)
VECTOR_HANDLER_SYNC(KiArm64VectorSyncLowerA64,     8)
VECTOR_HANDLER_DEFAULT(KiArm64VectorIrqLowerA64,   9)
VECTOR_HANDLER_DEFAULT(KiArm64VectorFiqLowerA64,  10)
VECTOR_HANDLER_DEFAULT(KiArm64VectorSErrorLowerA64, 11)
VECTOR_HANDLER_DEFAULT(KiArm64VectorSyncLowerA32, 12)
VECTOR_HANDLER_DEFAULT(KiArm64VectorIrqLowerA32,  13)
VECTOR_HANDLER_DEFAULT(KiArm64VectorFiqLowerA32,  14)
VECTOR_HANDLER_DEFAULT(KiArm64VectorSErrorLowerA32, 15)

#undef VECTOR_HANDLER_SYNC
#undef VECTOR_HANDLER_DEFAULT
