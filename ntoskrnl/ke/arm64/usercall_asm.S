#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .extern KiUserModeCallout

    .global KiCallUserMode
    .seh_proc KiCallUserMode
KiCallUserMode:
    /*
     * SEH: Stack must be 16-byte aligned. KEXCEPTION_FRAME_LENGTH (0xC8) is not aligned,
     * so we allocate 0xD0 (208 bytes) which is 16-byte aligned and matches SEH directive.
     */
    sub     sp, sp, #0xD0
    .seh_stackalloc 0xd0
    stp     x19, x20, [sp, #ExX19]
    .seh_save_regp x19, ExX19
    stp     x21, x22, [sp, #ExX21]
    .seh_save_regp x21, ExX21
    stp     x23, x24, [sp, #ExX23]
    .seh_save_regp x23, ExX23
    stp     x25, x26, [sp, #ExX25]
    .seh_save_regp x25, ExX25
    stp     x27, x28, [sp, #ExX27]
    .seh_save_regp x27, ExX27
    str     x29, [sp, #ExFp]
    .seh_save_reg x29, ExFp
    str     x30, [sp, #ExLr]
    .seh_save_reg x30, ExLr
    .seh_endprologue
    /* Save return address again (not tracked by SEH) */
    str     x30, [sp, #ExReturn]

    mrs     x9, fpcr
    str     x9, [sp, #ExFpcr]
    mrs     x9, fpsr
    str     x9, [sp, #ExFpsr]

    str     x0, [sp, #ExOutputBuffer]
    str     x1, [sp, #ExOutputLength]

    mov     x0, sp
    bl      KiUserModeCallout

    ldr     x9, [sp, #ExFpcr]
    msr     fpcr, x9
    ldr     x9, [sp, #ExFpsr]
    msr     fpsr, x9

    ldp     x19, x20, [sp, #ExX19]
    ldp     x21, x22, [sp, #ExX21]
    ldp     x23, x24, [sp, #ExX23]
    ldp     x25, x26, [sp, #ExX25]
    ldp     x27, x28, [sp, #ExX27]
    ldr     x29, [sp, #ExFp]
    ldr     x30, [sp, #ExLr]
    add     sp, sp, #0xD0
    ret
    .seh_endproc

    .global KiCallbackReturn
    .seh_proc KiCallbackReturn
KiCallbackReturn:
    /* SEH: This is a non-local return - restores frame from x0 */
    .seh_endprologue
    mov     sp, x0
    mov     x9, x1

    ldr     x10, [sp, #ExFpcr]
    msr     fpcr, x10
    ldr     x10, [sp, #ExFpsr]
    msr     fpsr, x10

    ldp     x19, x20, [sp, #ExX19]
    ldp     x21, x22, [sp, #ExX21]
    ldp     x23, x24, [sp, #ExX23]
    ldp     x25, x26, [sp, #ExX25]
    ldp     x27, x28, [sp, #ExX27]
    ldr     x29, [sp, #ExFp]
    ldr     x30, [sp, #ExLr]

    add     sp, sp, #0xD0

    mov     x0, x9
    ret
    .seh_endproc
