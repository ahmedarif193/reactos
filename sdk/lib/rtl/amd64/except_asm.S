/*
 * PROJECT:     ReactOS Runtime Library (RTL)
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Exception support for AMD64
 * COPYRIGHT:   Copyright 2010-2026 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <sdkddkver.h>
#include <asm.inc>
#include <ksamd64.inc>

/* FUNCTIONS *****************************************************************/

.code64

/*
 * VOID
 * RtlCaptureContext(
 *     _Out_ PCONTEXT ContextRecord@<rcx>);
 */
PUBLIC RtlCaptureContext
.PROC RtlCaptureContext

    /* Push rflags */
    pushfq
    .ALLOCSTACK 8
    .ENDPROLOG

    /* Save rax first, we use it later to copy some data */
    mov [rcx + CxRax], rax

    /* Set ContextFlags */
    mov dword ptr [rcx + CxContextFlags], (CONTEXT_FULL OR CONTEXT_SEGMENTS)

    /* Store the basic register context */
    mov [rcx + CxRcx], rcx
    mov [rcx + CxRdx], rdx
    mov [rcx + CxRbx], rbx
    mov [rcx + CxRsi], rsi

    /* Load return address in rax */
    mov rax, [rsp + 8]

    mov [rcx + CxRdi], rdi
    mov [rcx + CxRbp], rbp
    mov [rcx + CxR8], r8
    mov [rcx + CxR9], r9
    mov [rcx + CxR10], r10

    /* Store the return address */
    mov [rcx + CxRip], rax

    mov [rcx + CxR11], r11
    mov [rcx + CxR12], r12
    mov [rcx + CxR13], r13
    mov [rcx + CxR14], r14
    mov [rcx + CxR15], r15

    /* Load former stack pointer in rax */
    lea rax, [rsp + 16]

    /* Store segment selectors */
    mov [rcx + CxSegCs], cs
    mov [rcx + CxSegDs], ds
    mov [rcx + CxSegEs], es
    mov [rcx + CxSegFs], fs
    mov [rcx + CxSegGs], gs
    mov [rcx + CxSegSs], ss

    /* Store stack pointer */
    mov [rcx + CxRsp], rax

    /* Load eflags into eax */
    mov eax, [rsp]

    /* Store mxcsr */
    stmxcsr [rcx + CxMxCsr]

    /* Store eflags */
    mov [rcx + CxEFlags], eax

    /* Check if we are in kernel mode */
    test byte ptr [rcx + CxSegCs], MODE_MASK
    jnz RtlCaptureContextUM

#if (NTDDI_VERSION >= NTDDI_WIN8)
    /* NT 6.2+ initializes the non-register part of the kernel FP save area. */
    mov dword ptr [rcx + CxFltSave + LfControlWord], HEX(27f)
    mov word ptr [rcx + CxFltSave + LfTagWord], 0
    mov eax, [rcx + CxMxCsr]
    mov [rcx + CxFltSave + LfMxCsr], eax
    mov eax, [rcx + CxEFlags]
#endif

    /* Store xmm registers */
    movaps [rcx + CxXmm0], xmm0
    movaps [rcx + CxXmm1], xmm1
    movaps [rcx + CxXmm2], xmm2
    movaps [rcx + CxXmm3], xmm3
    movaps [rcx + CxXmm4], xmm4
    movaps [rcx + CxXmm5], xmm5
    movaps [rcx + CxXmm6], xmm6
    movaps [rcx + CxXmm7], xmm7
    movaps [rcx + CxXmm8], xmm8
    movaps [rcx + CxXmm9], xmm9
    movaps [rcx + CxXmm10], xmm10
    movaps [rcx + CxXmm11], xmm11
    movaps [rcx + CxXmm12], xmm12
    movaps [rcx + CxXmm13], xmm13
    movaps [rcx + CxXmm14], xmm14
    movaps [rcx + CxXmm15], xmm15

    jmp RtlCaptureContextExit

RtlCaptureContextUM:

    /* Store MXCSR, legacy floating point, MMX and XMM registers */
    fxsave [rcx + CxFltSave]

#if (NTDDI_VERSION >= NTDDI_WIN8)
    /* Normalize fields that changed in NT 6.2. */
    mov byte ptr [rcx + CxFltSave + LfTagWord + 1], 0
    mov word ptr [rcx + CxFltSave + LfErrorOpcode], 0
    mov dword ptr [rcx + CxFltSave + LfErrorOffset], 0
    mov dword ptr [rcx + CxFltSave + LfErrorSelector], 0
    mov dword ptr [rcx + CxFltSave + LfDataOffset], 0
    mov dword ptr [rcx + CxFltSave + LfDataSelector], 0
    mov dword ptr [rcx + CxFltSave + LfMxCsr_Mask], HEX(2ffff)
#endif

    /* FXSAVE stores 80-bit x87 values in 16-byte slots; clear the padding. */
    mov word ptr [rcx + CxFltSave + LfFloatRegisters + 10], 0
    mov dword ptr [rcx + CxFltSave + LfFloatRegisters + 12], 0
    mov word ptr [rcx + CxFltSave + LfFloatRegisters + 26], 0
    mov dword ptr [rcx + CxFltSave + LfFloatRegisters + 28], 0
    mov word ptr [rcx + CxFltSave + LfFloatRegisters + 42], 0
    mov dword ptr [rcx + CxFltSave + LfFloatRegisters + 44], 0
    mov word ptr [rcx + CxFltSave + LfFloatRegisters + 58], 0
    mov dword ptr [rcx + CxFltSave + LfFloatRegisters + 60], 0
    mov word ptr [rcx + CxFltSave + LfFloatRegisters + 74], 0
    mov dword ptr [rcx + CxFltSave + LfFloatRegisters + 76], 0
    mov word ptr [rcx + CxFltSave + LfFloatRegisters + 90], 0
    mov dword ptr [rcx + CxFltSave + LfFloatRegisters + 92], 0
    mov word ptr [rcx + CxFltSave + LfFloatRegisters + 106], 0
    mov dword ptr [rcx + CxFltSave + LfFloatRegisters + 108], 0
    mov word ptr [rcx + CxFltSave + LfFloatRegisters + 122], 0
    mov dword ptr [rcx + CxFltSave + LfFloatRegisters + 124], 0

RtlCaptureContextExit:

    /* Cleanup stack and return */
    add rsp, 8
    ret
.ENDP

/*
 * VOID
 * RtlpRestoreContextInternal(
 *     _In_ PCONTEXT ContextRecord@<rcx>);
 */
PUBLIC RtlpRestoreContextInternal
.PROC RtlpRestoreContextInternal

    /* Allocate space */
    sub rsp, HEX(8)
    .ALLOCSTACK 8
    .ENDPROLOG

    /* Load the target stack pointer into rdx */
    mov rdx, [rcx + CxRsp]

    /* Load EFlags into rax (zero extended) */
    mov eax, [rcx + CxEFlags]

    /* Load the return address into r8 */
    mov r8, [rcx + CxRip]

    /* Load rdx restore value into r9 */
    mov r9, [rcx + CxRdx]

    /* Check if we go to user mode */
    test byte ptr [rcx + CxSegCs], MODE_MASK
    jnz RestoreLegacyState

    /* Restore MXCSR */
    ldmxcsr [rcx + CxMxCsr]

    /* Restore xmm registers */
    movaps xmm0, [rcx + CxXmm0]
    movaps xmm1, [rcx + CxXmm1]
    movaps xmm2, [rcx + CxXmm2]
    movaps xmm3, [rcx + CxXmm3]
    movaps xmm4, [rcx + CxXmm4]
    movaps xmm5, [rcx + CxXmm5]
    movaps xmm6, [rcx + CxXmm6]
    movaps xmm7, [rcx + CxXmm7]
    movaps xmm8, [rcx + CxXmm8]
    movaps xmm9, [rcx + CxXmm9]
    movaps xmm10, [rcx + CxXmm10]
    movaps xmm11, [rcx + CxXmm11]
    movaps xmm12, [rcx + CxXmm12]
    movaps xmm13, [rcx + CxXmm13]
    movaps xmm14, [rcx + CxXmm14]
    movaps xmm15, [rcx + CxXmm15]

    jmp RestoreStateDone

RestoreLegacyState:

    /* Restore MXCSR, legacy floating point, MMX and XMM registers */
    fxrstor [rcx + CxFltSave]

RestoreStateDone:

    /* Copy Return address (now in r8) to the target stack */
    mov [rdx - 2 * 8], r8

    /* Copy Eflags (now in rax) to the target stack */
    mov [rdx - 3 * 8], rax

    /* Copy the value for rdx (now in r9) to the target stack */
    mov [rdx - 4 * 8], r9

    /* Restore the integer registers */
    mov rbx, [rcx + CxRbx]
    mov rsi, [rcx + CxRsi]
    mov rdi, [rcx + CxRdi]
    mov rbp, [rcx + CxRbp]
    mov r10, [rcx + CxR10]
    mov r11, [rcx + CxR11]
    mov r12, [rcx + CxR12]
    mov r13, [rcx + CxR13]
    mov r14, [rcx + CxR14]
    mov r15, [rcx + CxR15]

    /* Restore segment selectors */
    mov ds, [rcx + CxSegDs] // FIXME: WOW64 context?
    mov es, [rcx + CxSegEs]
    mov fs, [rcx + CxSegFs]
    //mov gs, [rcx + CxSegGs]
    //mov ss, [rcx + CxSegSs]

    /* Restore the registers we used */
    mov r8,  [rcx + CxR8]
    mov r9,  [rcx + CxR9]

    /* Check if we go to a different cs */
    mov ax, cs
    cmp [rcx + CxSegCs], ax
    jne ReturnFar

    /* Restore rax and rcx */
    mov rax, [rcx + CxRax]
    mov rcx, [rcx + CxRcx]

    /* Switch to the target stack */
    lea rsp, [rdx - 4 * 8]

    /* Pop rdx from the stack */
    pop rdx

    /* Pop Eflags from the stack */
    popfq

    /* Return and fix up the stack */
    ret 8

ReturnFar:

    // We should never need this path
    int HEX(2c)

    /* Put cs on the stack for the far return */
    mov ax, [rcx + CxSegCs]
    mov [rdx - 1 * 8], ax

    /* Load ss */
    mov ss, [rcx + CxSegSs]

    /* Restore rax and rcx */
    mov rax, [rcx + CxRax]
    mov rcx, [rcx + CxRcx]

    /* Switch to the target stack */
    lea rsp, [rdx - 4 * 8]

    /* Pop rdx from the stack */
    pop rdx

    /* Pop Eflags from the stack */
    popfq

    /* Return far */
    .byte HEX(48), HEX(CB)

.ENDP

EXTERN RtlpCheckForActiveDebugger:PROC
EXTERN RtlDispatchException:PROC
EXTERN ZwContinue:PROC
EXTERN ZwRaiseException:PROC
EXTERN RtlRaiseStatus:PROC

/*
 * VOID
 * RtlRaiseException (
 *     _In_ PEXCEPTION_RECORD ExceptionRecord);
 */
PUBLIC RtlRaiseException
.PROC RtlRaiseException

    /* Allocate stack space for a CONTEXT record */
    sub rsp, CONTEXT_FRAME_LENGTH + 8
    .ALLOCSTACK CONTEXT_FRAME_LENGTH + 8

    /* Save the ExceptionRecord pointer */
    mov [rsp + CONTEXT_FRAME_LENGTH + 8 + P1Home], rcx

    .ENDPROLOG

    /* Save the return address in EXCEPTION_RECORD.ExceptionAddress */
    mov rdx, [rsp + CONTEXT_FRAME_LENGTH + 8]
    mov [rcx + ErExceptionAddress], rdx

    /* Capture the current context */
    mov rcx, rsp
    call RtlCaptureContext

    /* Fix up CONTEXT.Rip for the caller (RtlCaptureContext doesn't change rdx!) */
    mov [rsp + CxRip], rdx

    /* Fix up CONTEXT.Rsp for the caller (+8 for the return address) */
    lea rdx, [rsp + CONTEXT_FRAME_LENGTH + 8 + 8]
    mov [rsp + CxRsp], rdx

    /* Check if a user mode debugger is active */
    call RtlpCheckForActiveDebugger
    test al, al
    mov r8b, 1
    jnz RaiseException

    /* Dispatch the exception */
    mov rcx, [rsp + CONTEXT_FRAME_LENGTH + 8 + P1Home]
    mov rdx, rsp
    call RtlDispatchException

    /* Check if it was handled */
    test al, al
    mov r8b, 0
    jz RaiseException

    /* It was handled, continue with the updated context */
    mov rcx, rsp
    mov dl, 0
    call  ZwContinue
    jmp RaiseStatus

RaiseException:

    mov rcx, [rsp + CONTEXT_FRAME_LENGTH + 8 + P1Home]
    mov rdx, rsp
    call ZwRaiseException

RaiseStatus:

    mov rcx, rax
    mov rdx, rsp
    call RtlRaiseStatus

    int HEX(2c)
.ENDP


END
