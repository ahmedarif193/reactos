/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS NT Library
 * PURPOSE:         ARM64 usermode dispatcher stubs
 */

#include <asm.h>
#include <ksarm64.h>

    .text
    .p2align 2

    .extern NtContinue
    .extern ZwRaiseException
    .extern ZwCallbackReturn
    .extern RtlDispatchException
    .extern RtlRaiseStatus
    .extern KiUserExceptionDispatcherNestedArm64

    .global LdrInitializeThunk
LdrInitializeThunk:
    mov     x29, xzr
    mov     x0, x3
    b       LdrpInit

    .global KiUserApcDispatcher
KiUserApcDispatcher:
    mov     x16, x3
    mov     x3, sp
    blr     x16

    mov     x0, sp
    mov     w1, #1
    bl      NtContinue

1:
    brk     #0xf000
    b       1b

    .global KiUserCallbackDispatcher
KiUserCallbackDispatcher:
    ldr     x0, [sp, #CkBuffer]
    ldr     w1, [sp, #CkLength]
    ldr     w2, [sp, #CkApiNumber]

    mov     x3, x18
    ldr     x3, [x3, #TePeb]
    ldr     x3, [x3, #PeKernelCallbackTable]
    mov     x9, x3
    add     x16, x9, x2, lsl #3
    ldr     x16, [x16]
    blr     x16

    mov     w2, w0
    mov     x0, xzr
    mov     x1, xzr
    bl      ZwCallbackReturn

    mov     w9, w0
1:
    mov     w0, w9
    bl      RtlRaiseStatus
    b       1b

    .global KiUserExceptionDispatcher
KiUserExceptionDispatcher:
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    mov     x29, sp
    mov     x19, x0
    mov     x20, x1

    mov     x0, x19
    mov     x1, x20
    bl      RtlDispatchException
    cbz     w0, 2f

    mov     x0, x20
    mov     w1, #0
    bl      NtContinue
    b       3f
2:
    mov     x0, x19
    mov     x1, x20
    mov     w2, #0
    bl      ZwRaiseException
3:
    mov     w1, w0
    mov     x0, x19
    bl      KiUserExceptionDispatcherNestedArm64

    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret
