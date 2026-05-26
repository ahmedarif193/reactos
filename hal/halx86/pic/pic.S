/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     HAL PIC Management and Control Code
 * COPYRIGHT:   Copyright 2016-2018 Thomas Faber <thomas.faber@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>

#include <ks386.inc>

/* FUNCTIONS *****************************************************************/

.code

MACRO(DEFINE_END_INTERRUPT_WRAPPER, WrapperName, HandlerName)
#ifdef _USE_ML
EXTERN @&HandlerName&@8:PROC
PUBLIC _&WrapperName&@8
.PROC _&WrapperName&@8
#else
EXTERN @\HandlerName\()@8:PROC
PUBLIC _\WrapperName\()@8
.PROC _\WrapperName\()@8
#endif
    FPO 0, 2, 0, 0, 0, FRAME_FPO

    /* Call the C function with the same arguments we got */
    mov ecx, [esp+4]
    mov edx, [esp+8]
#ifdef _USE_ML
    call @&HandlerName&@8
#else
    call @\HandlerName\()@8
#endif

    /* Check if we got a pointer back */
    test eax, eax
#ifdef _USE_ML
    jnz WrapperName&_CallIntHandler
#else
    jnz \WrapperName\()_CallIntHandler
#endif

    /* No? Just return */
    ret 8

#ifdef _USE_ML
WrapperName&_CallIntHandler:
#else
\WrapperName\()_CallIntHandler:
#endif
    /* We got a pointer to call. Since it won't return, reset the stack to
       the location of the stack frame. This frees up our own stack as well
       as that of the functions above us, and avoids an overflow due to
       excessive recursion.
       The next function takes the trap frame as its (fastcall) argument. */
    mov ecx, [esp+8]
    mov esp, ecx
    mov ebp, esp
    jmp eax
.ENDP
ENDM

MACRO(DEFINE_INTERRUPT_WRAPPER, WrapperName, HandlerName)
#ifdef _USE_ML
EXTERN _&HandlerName:PROC
PUBLIC _&WrapperName
.PROC _&WrapperName
#else
EXTERN _\HandlerName:PROC
PUBLIC _\WrapperName
.PROC _\WrapperName
#endif
    FPO 0, 0, 0, 0, 0, FRAME_FPO

    /* Call the C function */
#ifdef _USE_ML
    call _&HandlerName
#else
    call _\HandlerName
#endif

    /* Check if we got a pointer back */
    test eax, eax
#ifdef _USE_ML
    jnz WrapperName&_CallIntHandler
#else
    jnz \WrapperName\()_CallIntHandler
#endif

    /* No? Just return */
    ret

#ifdef _USE_ML
WrapperName&_CallIntHandler:
#else
\WrapperName\()_CallIntHandler:
#endif
    /* Optimize the tail call to avoid stack overflow */
    jmp eax
.ENDP
ENDM


DEFINE_END_INTERRUPT_WRAPPER HalpEndSoftwareInterrupt, HalpEndSoftwareInterrupt2
DEFINE_END_INTERRUPT_WRAPPER HalEndSystemInterrupt, HalEndSystemInterrupt2

DEFINE_INTERRUPT_WRAPPER HalpDispatchInterrupt, HalpDispatchInterrupt2
DEFINE_INTERRUPT_WRAPPER HalpHardwareInterruptLevel, HalpHardwareInterruptLevel2

END
