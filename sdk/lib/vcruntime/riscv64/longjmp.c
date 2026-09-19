/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Implementation of longjmp for RISC-V 64
 */

#include <setjmp.h>

__declspec(noreturn)
void __longjmp_noframe(const _JUMP_BUFFER* Buffer, int Value);

__declspec(noreturn)
void __cdecl
longjmp(
    _In_reads_(_JBLEN) jmp_buf Buffer,
    _In_ int Value)
{
    __longjmp_noframe((const _JUMP_BUFFER*)Buffer, Value ? Value : 1);
    __builtin_unreachable();
}
