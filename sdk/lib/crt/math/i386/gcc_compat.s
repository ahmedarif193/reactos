/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     GCC compatibility wrappers for 64-bit arithmetic
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 *
 * NOTES:       GCC 15.2 emits calls to __divdi3, __udivdi3, etc. for 64-bit arithmetic on i386.
 *              These functions wrap the existing MSVC-style _alldiv, _aulldiv, etc. implementations.
 */

#include <asm.inc>

PUBLIC ___divdi3
PUBLIC ___udivdi3
PUBLIC ___moddi3
PUBLIC ___umoddi3
PUBLIC ___udivmoddi4
PUBLIC ___divmoddi4
PUBLIC __alloca

/* Triple underscores survive the GAS MASM preprocessor and still
 * map to the two-underscore exports expected by msvcrt/msvcrtex. */

EXTERN __alldiv:PROC
EXTERN __aulldiv:PROC
EXTERN __allrem:PROC
EXTERN __aullrem:PROC
EXTERN __aulldvrm:PROC
EXTERN __alldvrm:PROC
EXTERN __alloca_probe:PROC

.code

/*
 * __divdi3 - signed 64-bit division
 * GCC passes both 64-bit operands on the stack using cdecl.
 */
___divdi3:
    push dword ptr [esp + 16]    /* divisor high */
    push dword ptr [esp + 16]    /* divisor low */
    push dword ptr [esp + 16]    /* dividend high */
    push dword ptr [esp + 16]    /* dividend low */
    call __alldiv                /* returns quotient in EDX:EAX, pops its args */
    ret

/*
 * __udivdi3 - unsigned 64-bit division
 */
___udivdi3:
    push dword ptr [esp + 16]
    push dword ptr [esp + 16]
    push dword ptr [esp + 16]
    push dword ptr [esp + 16]
    call __aulldiv
    ret

/*
 * __moddi3 - signed 64-bit modulo
 */
___moddi3:
    push dword ptr [esp + 16]
    push dword ptr [esp + 16]
    push dword ptr [esp + 16]
    push dword ptr [esp + 16]
    call __allrem
    ret

/*
 * __umoddi3 - unsigned 64-bit modulo
 */
___umoddi3:
    push dword ptr [esp + 16]
    push dword ptr [esp + 16]
    push dword ptr [esp + 16]
    push dword ptr [esp + 16]
    call __aullrem
    ret

/*
 * __udivmoddi4 - unsigned 64-bit division with remainder pointer
 */
___udivmoddi4:
    push ebx
    push esi

    mov esi, [esp + 28]          /* remainder pointer */

    push dword ptr [esp + 24]    /* divisor high */
    push dword ptr [esp + 24]    /* divisor low */
    push dword ptr [esp + 24]    /* dividend high */
    push dword ptr [esp + 24]    /* dividend low */
    call __aulldvrm              /* quotient in EDX:EAX, remainder in EBX:ECX */

    test esi, esi
    jz .L_no_udiv_remainder
    mov [esi], ecx               /* remainder low */
    mov [esi + 4], ebx           /* remainder high */

.L_no_udiv_remainder:
    pop esi
    pop ebx
    ret

/*
 * __divmoddi4 - signed 64-bit division with remainder pointer
 */
___divmoddi4:
    push ebx
    push esi

    mov esi, [esp + 28]          /* remainder pointer */

    push dword ptr [esp + 24]    /* divisor high */
    push dword ptr [esp + 24]    /* divisor low */
    push dword ptr [esp + 24]    /* dividend high */
    push dword ptr [esp + 24]    /* dividend low */
    call __alldvrm               /* quotient in EDX:EAX, remainder in EBX:ECX */

    test esi, esi
    jz .L_no_div_remainder
    mov [esi], ecx               /* remainder low */
    mov [esi + 4], ebx           /* remainder high */

.L_no_div_remainder:
    pop esi
    pop ebx
    ret

/*
 * __alloca - MS-compatible alloca thunk used by Clang in some builds.
 */
__alloca:
    jmp __alloca_probe

END
