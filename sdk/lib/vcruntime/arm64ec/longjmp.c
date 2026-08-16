/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64EC longjmp with mixed x64/EC frame unwinding
 * COPYRIGHT:   Copyright 2025 Timo Kreuzer <timo.kreuzer@reactos.org>
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Adapted from ReactOS' AMD64 vcruntime longjmp implementation.
 */

#include <setjmp.h>
#include <windef.h>
#include <winnt.h>

__declspec(noreturn) NTSYSAPI VOID NTAPI RtlRaiseStatus(LONG Status);

__declspec(noreturn)
void __cdecl
longjmp(_In_reads_(_JBLEN) jmp_buf Buffer, _In_ int Value)
{
    const _JUMP_BUFFER *JumpBuffer = (const _JUMP_BUFFER *)Buffer;
    CONTEXT ContextRecord = { 0 };
    EXCEPTION_RECORD ExceptionRecord = { 0 };

    if (Value == 0)
        Value = 1;

    ExceptionRecord.ExceptionCode = STATUS_LONGJUMP;
    ExceptionRecord.NumberParameters = 1;
    ExceptionRecord.ExceptionInformation[0] = (ULONG_PTR)JumpBuffer;

    if (JumpBuffer->Frame != 0)
        RtlUnwind((PVOID)JumpBuffer->Frame, (PVOID)JumpBuffer->Rip, &ExceptionRecord, (PVOID)(LONG_PTR)Value);

    ContextRecord.ContextFlags = CONTEXT_FULL;
    ContextRecord.Rip = JumpBuffer->Rip;
    ContextRecord.Rsp = JumpBuffer->Rsp;
    if (RtlIsEcCode(JumpBuffer->Rip))
        ContextRecord.Rcx = (ULONG64)(LONG_PTR)Value;
    else
        ContextRecord.Rax = (ULONG64)(LONG_PTR)Value;
    RtlRestoreContext(&ContextRecord, &ExceptionRecord);
    RtlRaiseStatus((LONG)0xc0000001);
}
