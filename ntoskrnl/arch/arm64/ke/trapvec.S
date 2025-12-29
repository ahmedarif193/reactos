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

/* Permanent table entries */
#define PERM_VECTOR_SYNC(label, id)                                      \
    .global label;                                                       \
label:                                                                   \
    stp x29, x30, [sp, #-16]!;                                           \
    mov x29, sp;                                                         \
    /* Reuse early sync framing to build a KTRAP_FRAME */                \
    sub sp, sp, #0x2B0; /* ARM64_EARLY_STATE_SIZE */                     \
    /* Save a minimal GPR window and state expected by C helper */       \
    /* (Use the same layout as in earlyvec.S) */                         \
    /* Save x0-x3 to seed the state; the helper fills the rest */        \
    str x0, [sp, #0x28];                                                 \
    str x1, [sp, #0x30];                                                 \
    str x2, [sp, #0x38];                                                 \
    str x3, [sp, #0x40];                                                 \
    add x11, x29, #16;                                                   \
    str x11, [sp, #0x120]; /* SP */                                      \
    mrs x12, elr_el1;                                                    \
    str x12, [sp, #0x18]; /* ELR */                                      \
    str x12, [sp, #0x128]; /* PC */                                      \
    mrs x13, spsr_el1;                                                   \
    str x13, [sp, #0x20]; /* SPSR */                                     \
    mrs x14, esr_el1;                                                    \
    str x14, [sp, #0x8];  /* ESR */                                      \
    mrs x15, far_el1;                                                    \
    str x15, [sp, #0x10]; /* FAR */                                      \
    mov w16, #(id);                                                      \
    str x16, [sp, #0x0];  /* VectorId */                                 \
    mov x0, sp;                                                          \
    bl KiArm64HandleSynchronousException;                                \
    cbz x0, 1f;                                                          \
    add sp, sp, #0x2B0;                                                  \
    ldp x29, x30, [sp], #16;                                             \
    /* Return through KiTrapReturn */                                     \
    b KiTrapReturn;                                                      \
1:  /* Bugcheck path */                                                  \
    mov x0, sp;                                                          \
    bl KiArm64BugCheckEntry;                                             \
    b 1b

#define PERM_VECTOR_DEF(label, id)                                       \
    .global label;                                                       \
label:                                                                   \
    stp x29, x30, [sp, #-16]!;                                           \
    mov x29, sp;                                                         \
    /* Call the C veneer for HAL-aware dispatch (stub for now) */        \
    mov w0, #(id);                                                       \
    bl KiArm64InterruptDispatchEntry;                                    \
    mov sp, x29;                                                         \
    ldp x29, x30, [sp], #16;                                             \
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
PERM_VECTOR_DEF (KiArm64PermVectorIrqSp0,           1)
PERM_VECTOR_DEF (KiArm64PermVectorFiqSp0,           2)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorSp0,    3)
PERM_VECTOR_SYNC(KiArm64PermVectorSyncSpx,          4)
PERM_VECTOR_DEF (KiArm64PermVectorIrqSpx,           5)
PERM_VECTOR_DEF (KiArm64PermVectorFiqSpx,           6)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorSpx,    7)
PERM_VECTOR_SYNC(KiArm64PermVectorSyncLowerA64,     8)
PERM_VECTOR_DEF (KiArm64PermVectorIrqLowerA64,      9)
PERM_VECTOR_DEF (KiArm64PermVectorFiqLowerA64,     10)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorLowerA64,  11)
PERM_VECTOR_SYNC(KiArm64PermVectorSyncLowerA32,    12)
PERM_VECTOR_DEF (KiArm64PermVectorIrqLowerA32,     13)
PERM_VECTOR_DEF (KiArm64PermVectorFiqLowerA32,     14)
PERM_VECTOR_SYNC(KiArm64PermVectorSErrorLowerA32,  15)

#undef PERM_VECTOR_SYNC
#undef PERM_VECTOR_DEF
#undef PERM_VECTOR_BUGCHECK
