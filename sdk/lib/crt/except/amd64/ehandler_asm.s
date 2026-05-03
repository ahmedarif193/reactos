#include <asm.inc>
#include <ksamd64.inc>

.code64

/*
 * Reconstruct the nonvolatile machine state expected by compiler-generated
 * x64 SEH funclets before entering their synthetic helper entry point.
 */
PUBLIC __C_specific_handler_call_handler
#ifdef _USE_ML
__C_specific_handler_call_handler PROC FRAME
#else
.PROC __C_specific_handler_call_handler
#endif

    push r15
    .pushreg r15
    push r14
    .pushreg r14
    push r13
    .pushreg r13
    push r12
    .pushreg r12
    push rdi
    .pushreg rdi
    push rsi
    .pushreg rsi
    push rbp
    .pushreg rbp
    push rbx
    .pushreg rbx
    sub rsp, 200
    .allocstack 200

    movdqa [rsp + 32], xmm6
    .savexmm128 xmm6, 32
    movdqa [rsp + 48], xmm7
    .savexmm128 xmm7, 48
    movdqa [rsp + 64], xmm8
    .savexmm128 xmm8, 64
    movdqa [rsp + 80], xmm9
    .savexmm128 xmm9, 80
    movdqa [rsp + 96], xmm10
    .savexmm128 xmm10, 96
    movdqa [rsp + 112], xmm11
    .savexmm128 xmm11, 112
    movdqa [rsp + 128], xmm12
    .savexmm128 xmm12, 128
    movdqa [rsp + 144], xmm13
    .savexmm128 xmm13, 144
    movdqa [rsp + 160], xmm14
    .savexmm128 xmm14, 160
    movdqa [rsp + 176], xmm15
    .savexmm128 xmm15, 176
    stmxcsr [rsp + 192]
    fnstcw word ptr [rsp + 196]
    .endprolog

    mov r10, rcx
    mov r11, rdx

    mov rbx, [r11 + CxRbx]
    mov rbp, [r11 + CxRbp]
    mov rsi, [r11 + CxRsi]
    mov rdi, [r11 + CxRdi]
    mov r12, [r11 + CxR12]
    mov r13, [r11 + CxR13]
    mov r14, [r11 + CxR14]
    mov r15, [r11 + CxR15]
    movdqa xmm6, [r11 + CxXmm6]
    movdqa xmm7, [r11 + CxXmm7]
    movdqa xmm8, [r11 + CxXmm8]
    movdqa xmm9, [r11 + CxXmm9]
    movdqa xmm10, [r11 + CxXmm10]
    movdqa xmm11, [r11 + CxXmm11]
    movdqa xmm12, [r11 + CxXmm12]
    movdqa xmm13, [r11 + CxXmm13]
    movdqa xmm14, [r11 + CxXmm14]
    movdqa xmm15, [r11 + CxXmm15]
    ldmxcsr [r11 + CxMxCsr]
    fldcw word ptr [r11 + CxFltSave + LfControlWord]

    mov rcx, r8
    mov rdx, r9
    call r10

    fldcw word ptr [rsp + 196]
    ldmxcsr [rsp + 192]
    movdqa xmm6, [rsp + 32]
    movdqa xmm7, [rsp + 48]
    movdqa xmm8, [rsp + 64]
    movdqa xmm9, [rsp + 80]
    movdqa xmm10, [rsp + 96]
    movdqa xmm11, [rsp + 112]
    movdqa xmm12, [rsp + 128]
    movdqa xmm13, [rsp + 144]
    movdqa xmm14, [rsp + 160]
    movdqa xmm15, [rsp + 176]

    add rsp, 200
    pop rbx
    pop rbp
    pop rsi
    pop rdi
    pop r12
    pop r13
    pop r14
    pop r15
    ret

#ifdef _USE_ML
__C_specific_handler_call_handler ENDP
#else
.ENDP
#endif

END
