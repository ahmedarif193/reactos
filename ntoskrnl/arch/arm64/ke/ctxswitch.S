#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .global KiThreadStartup
    .global KiSwapContext
    .global KiSwapContextInternal
    .extern KiArm64BootStageLog

KiThreadStartup:
    // Log entry to KiThreadStartup for ARM64 bring-up
    adrp    x0, .Lkts_msg1
    add     x0, x0, :lo12:.Lkts_msg1
    bl      KiArm64BootStageLog
    /* Clear non-volatile registers */
    mov     x19, xzr
    mov     x20, xzr
    mov     x21, xzr
    mov     x22, xzr
    mov     x23, xzr
    mov     x24, xzr
    mov     x25, xzr
    mov     x26, xzr
    mov     x27, xzr
    mov     x28, xzr
    mov     x29, xzr

    /* Load startup parameters */
    ldr     x2, [sp, #SfSystemRoutine]
    ldr     x0, [sp, #SfStartRoutine]
    ldr     x1, [sp, #SfStartContext]

    /* Pop the start frame before invoking the system routine */
    add     sp, sp, #KSTART_FRAME_LENGTH

    // Log right before jumping into the system routine
    adrp    x3, .Lkts_msg2
    add     x3, x3, :lo12:.Lkts_msg2
    mov     x4, x2                // save SystemRoutine
    mov     x5, x0                // save StartRoutine
    mov     x6, x1                // save StartContext
    mov     x0, x3
    bl      KiArm64BootStageLog
    mov     x2, x4
    mov     x0, x5
    mov     x1, x6

    /* Start the thread */
    blr     x2

    /* Should never return */
    brk     #0xf000


    .extern KiSwapContextResume
    .extern KeArm64CurrentPcr

/*
 * BOOLEAN
 * KiSwapContext(
 *     _In_ KIRQL WaitIrql,
 *     _Inout_ PKTHREAD CurrentThread);
 */
KiSwapContext:
    /* Reserve space for the switch frame and save non-volatile registers */
    sub     sp, sp, #KSWITCH_FRAME_LENGTH
    stp     x19, x20, [sp, #SwX19]
    stp     x21, x22, [sp, #SwX21]
    stp     x23, x24, [sp, #SwX23]
    stp     x25, x26, [sp, #SwX25]
    stp     x27, x28, [sp, #SwX27]
    str     x29, [sp, #SwFp]
    str     x30, [sp, #SwLr]
    str     x30, [sp, #SwReturn]

    /* Locate the target (new) thread from the current PCR */
    adrp    x9, KeArm64CurrentPcr
    ldr     x9, [x9, #:lo12:KeArm64CurrentPcr]
    cbz     x9, .Lfallback_thread
    ldr     x2, [x9, #PcCurrentThread]
    cbz     x2, .Lfallback_thread
    b       .Lcall_internal

.Lfallback_thread:
    /* Fallback to the caller's thread if the PCR path is unavailable */
    mov     x2, x1

.Lcall_internal:
    bl      KiSwapContextInternal

    /* Restore the saved non-volatile register bank */
    ldp     x19, x20, [sp, #SwX19]
    ldp     x21, x22, [sp, #SwX21]
    ldp     x23, x24, [sp, #SwX23]
    ldp     x25, x26, [sp, #SwX25]
    ldp     x27, x28, [sp, #SwX27]
    ldr     x29, [sp, #SwFp]
    ldr     x30, [sp, #SwLr]
    ldr     x9,  [sp, #SwReturn]
    add     sp, sp, #KSWITCH_FRAME_LENGTH
    ret     x9

// Read-only strings for boot stage logs
.align 3
.Lkts_msg1:
    .asciz "[arm64] KiThreadStartup: entry"
.align 3
.Lkts_msg2:
    .asciz "[arm64] KiThreadStartup: calling system routine"

/*
 * BOOLEAN
 * KiSwapContextInternal(
 *     _In_ KIRQL WaitIrql,
 *     _Inout_ PKTHREAD OldThread,
 *     _Inout_ PKTHREAD NewThread);
 */
KiSwapContextInternal:
    /* Wait until the incoming thread clears its SwapBusy bit */
.Lswapbusy_wait:
    ldrb    w3, [x2, #ThSwapBusy]
    cbz     w3, .Lswapbusy_clear
    yield
    b       .Lswapbusy_wait

.Lswapbusy_clear:
    /* Record the wait IRQL in the outgoing switch frame */
    strb    w0, [sp, #SwApcBypass]

    /* Publish the outgoing kernel stack */
    mov     x11, sp
    str     x11, [x1, #ThKernelStack]

    /* Switch to the incoming kernel stack */
    ldr     x3, [x2, #ThKernelStack]
    cbz     x3, .Lresume_call      /* Should not happen, but avoid faulting */
    mov     sp, x3

    /* Reload the wait IRQL requested by the target thread */
    ldrb    w0, [sp, #SwApcBypass]

.Lresume_call:
    bl      KiSwapContextResume
    ret
