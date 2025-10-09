/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS Run-Time Library
 * PURPOSE:           Byte swap functions (AMD64)
 * FILE:              lib/rtl/amd64/rtlswap.S
 * PROGRAMMER:        Ahmed ARIF (arif.ing@outlook.com)
 */

#include <asm.inc>

PUBLIC RtlUshortByteSwap
PUBLIC RtlUlongByteSwap
PUBLIC RtlUlonglongByteSwap

/* FUNCTIONS ***************************************************************/
.code

/* USHORT */
/* RCX = input, RAX = return (zero-extended) */
FUNC RtlUshortByteSwap
    FPO 0, 0, 0, 0, 0, FRAME_FPO

    ror     cx, 8         /* swap byte order in low 16 bits of RCX */
    movzx   eax, cx       /* zero-extend to 32/64-bit return in RAX */
    ret
ENDFUNC

/* ULONG */
/* ECX = input, EAX = return */
FUNC RtlUlongByteSwap
    FPO 0, 0, 0, 0, 0, FRAME_FPO

    mov     eax, ecx
    bswap   eax
    ret
ENDFUNC

/* ULONGLONG */
/* RCX = input, RAX = return */
FUNC RtlUlonglongByteSwap
    FPO 0, 0, 0, 0, 0, FRAME_FPO

    mov     rax, rcx
    bswap   rax
    ret
ENDFUNC

END
