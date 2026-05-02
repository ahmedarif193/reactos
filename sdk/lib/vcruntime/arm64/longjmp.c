/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of longjmp for ARM64
 * COPYRIGHT:   Copyright 2025 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include <setjmp.h>

__declspec(noreturn)
void __longjmp_noframe(const _JUMP_BUFFER* _Buf, int _Value);

__declspec(noreturn)
void __cdecl longjmp(
    _In_reads_(_JBLEN) jmp_buf _Buf,
    _In_ int _Value)
{
    /* Ensure _Value is non-zero */
    _Value = (_Value == 0) ? 1 : _Value;

    __longjmp_noframe((const _JUMP_BUFFER*)_Buf, _Value);

    __builtin_unreachable();
}
