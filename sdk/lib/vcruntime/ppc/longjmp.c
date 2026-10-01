/*
 * PROJECT:     LiberNT vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Implementation of longjmp for Windows NT PowerPC
 */

#include <setjmp.h>
#include <windef.h>

__declspec(noreturn)
void __longjmp_noframe(const _JUMP_BUFFER* Buffer, int Value);

__declspec(noreturn)
void __cdecl
longjmp(
    _In_reads_(_JBLEN) jmp_buf Buffer,
    _In_ int Value)
{
    const _JUMP_BUFFER *JumpBuffer = (const _JUMP_BUFFER *)Buffer;

    Value = Value ? Value : 1;
    if (JumpBuffer->Type)
    {
        ULONG Frame, Continuation;

        __builtin_memcpy(&Frame, Buffer, sizeof(Frame));
        __builtin_memcpy(&Continuation, (const char *)Buffer + sizeof(Frame),
                         sizeof(Continuation));
        RtlUnwind((PVOID)(ULONG_PTR)Frame,
                  (PVOID)(ULONG_PTR)Continuation,
                  NULL, (PVOID)(ULONG_PTR)Value);
    }

    __longjmp_noframe(JumpBuffer, Value);
    __builtin_unreachable();
}
