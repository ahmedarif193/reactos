/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* RISC-V64 native context restoration. */
#include <rtl.h>

DECLSPEC_NORETURN VOID NTAPI RtlpRiscv64RestoreContext(PCONTEXT Context);

VOID NTAPI
RtlRestoreContext(PCONTEXT ContextRecord, PEXCEPTION_RECORD ExceptionRecord)
{
    CONTEXT Context;
    EXCEPTION_RECORD Record;
    ULONG_PTR Low, High;
    ULONG Required = CONTEXT_CONTROL | CONTEXT_INTEGER;
    ULONG Allowed = Required | CONTEXT_FLOATING_POINT | CONTEXT_UNWOUND_TO_CALL;

    if (!ContextRecord ||
        !NT_SUCCESS(RtlpSafeCopyMemory(&Context, ContextRecord, sizeof(Context))))
        RtlpRiscv64RaiseFatal(STATUS_INVALID_PARAMETER);

    if (ExceptionRecord &&
        !NT_SUCCESS(RtlpSafeCopyMemory(&Record, ExceptionRecord, sizeof(Record))))
        RtlpRiscv64RaiseFatal(STATUS_INVALID_PARAMETER);

    RtlpGetStackLimits(&Low, &High);
    if ((Context.ContextFlags & Required) != Required ||
        (Context.ContextFlags & ~Allowed) ||
        !Context.Pc || (Context.Pc & 1) ||
        (LONG64)Context.Pc >> 38 != ((Context.Pc >> 38) & 1 ? -1 : 0) ||
        (LONG64)Context.Sp >> 38 != ((Context.Sp >> 38) & 1 ? -1 : 0) ||
        (Context.Sp & 15) || Context.Sp < Low || Context.Sp > High ||
        (ExceptionRecord &&
         (Record.ExceptionCode == STATUS_UNWIND_CONSOLIDATE ||
          Record.ExceptionCode == STATUS_LONGJUMP)))
        RtlpRiscv64RaiseFatal(STATUS_INVALID_PARAMETER);

    RtlpRiscv64RestoreContext(&Context);
}
