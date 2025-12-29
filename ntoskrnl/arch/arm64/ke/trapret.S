#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .global KiTrapReturn

KiTrapReturn:
    mov     x20, x0                  /* Trap frame */
    mov     x21, x1                  /* Exception frame */

    /* Preserve fallback frame pointer/link register */
    ldr     x24, [x20, #TfFp]
    ldr     x25, [x20, #TfLr]

    /* Restore volatile registers from the trap frame */
    ldr     x18, [x20, #TfX18]
    ldp     x16, x17, [x20, #TfX16]
    ldp     x14, x15, [x20, #TfX14]
    ldp     x12, x13, [x20, #TfX12]
    ldp     x10, x11, [x20, #TfX10]
    ldp     x8,  x9,  [x20, #TfX8]
    ldp     x6,  x7,  [x20, #TfX6]
    ldp     x4,  x5,  [x20, #TfX4]
    ldp     x2,  x3,  [x20, #TfX2]
    ldp     x0,  x1,  [x20, #TfX0]

    /* Program status and target address */
    ldr     w22, [x20, #TfSpsr]
    ldr     x23, [x20, #TfPc]
    ldr     x26, [x20, #TfSp]
    msr     spsr_el1, x22
    msr     elr_el1,  x23

    /* Select appropriate stack pointer */
    ands    w27, w22, #0xF
    b.eq    1f
    msr     sp_el0, x26              /* Returning to EL0 */
    /* Ensure x18/TPIDR_EL0 track the current TEB for user-mode ABI */
    adrp    x16, KeArm64CurrentThread
    ldr     x16, [x16, #:lo12:KeArm64CurrentThread]
    cbz     x16, 2f
    ldr     x18, [x16, #ThTeb]
    msr     tpidr_el0, x18
2:
    ldr     x16, [x20, #TfX16]
    b       3f
1:
    mov     sp, x26                  /* Returning to EL1 */
3:
    /* Restore non-volatiles when an exception frame is present */
    cbz     x21, 3f
    mov     x11, x21
    ldr     x19, [x11, #ExX19]
    ldr     x20, [x11, #ExX20]
    ldr     x21, [x11, #ExX21]
    ldr     x22, [x11, #ExX22]
    ldr     x23, [x11, #ExX23]
    ldr     x24, [x11, #ExX24]
    ldr     x25, [x11, #ExX25]
    ldr     x26, [x11, #ExX26]
    ldr     x27, [x11, #ExX27]
    ldr     x28, [x11, #ExX28]
    ldr     x29, [x11, #ExFp]
    ldr     x30, [x11, #ExLr]
    b       4f
3:
    mov     x29, x24
    mov     x30, x25
4:
    dsb     ish
    isb
    eret


    .align 4
    .global KiArm64BugCheckEntry
KiArm64BugCheckEntry:
    ldr     x16, =KiArm64BugCheckSynchronousException
    br      x16

    .ltorg
