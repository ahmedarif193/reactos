/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 context restoration support
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <rtl.h>

typedef PVOID (CALLBACK *PRTL_CONSOLIDATE_CALLBACK)(
    _In_ PEXCEPTION_RECORD ExceptionRecord);

DECLSPEC_NORETURN
VOID
NTAPI
RtlpRestoreContextInternal(
    _In_ PCONTEXT ContextRecord);

DECLSPEC_NORETURN
VOID
NTAPI
RtlpArm64ConsolidateCallback(
    _In_ PCONTEXT ContextRecord,
    _In_ PRTL_CONSOLIDATE_CALLBACK Callback,
    _In_ PEXCEPTION_RECORD ExceptionRecord);

/* The ARM64 jump buffer, as laid out by _JUMP_BUFFER in <setjmp.h>. Repeated
 * here because the CRT header is not available to the kernel-mode RTL. */
typedef struct _ARM64_JUMP_BUFFER
{
    ULONG64 Frame;
    ULONG64 Reserved;
    ULONG64 GpRegs[10];
    ULONG64 Fp;
    ULONG64 Lr;
    ULONG64 Sp;
    ULONG Fpcr;
    ULONG Fpsr;
    double FpRegs[8];
} ARM64_JUMP_BUFFER, *PARM64_JUMP_BUFFER;

VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord)
{
    if ((ExceptionRecord != NULL) &&
        (ExceptionRecord->ExceptionCode == STATUS_LONGJUMP) &&
        (ExceptionRecord->NumberParameters >= 1))
    {
        PARM64_JUMP_BUFFER JumpBuffer = (PARM64_JUMP_BUFFER)ExceptionRecord->ExceptionInformation[0];
        ULONG Index;

        /* The unwind stopped in the frame that called setjmp; the buffer holds
         * the register state to return to. */
        RtlCopyMemory(&ContextRecord->X19, JumpBuffer->GpRegs, sizeof(JumpBuffer->GpRegs));
        ContextRecord->Fp = JumpBuffer->Fp;
        ContextRecord->Sp = JumpBuffer->Sp;
        ContextRecord->Pc = JumpBuffer->Lr;
        ContextRecord->Fpcr = JumpBuffer->Fpcr;
        ContextRecord->Fpsr = JumpBuffer->Fpsr;

        for (Index = 0; Index < RTL_NUMBER_OF(JumpBuffer->FpRegs); Index++)
        {
            ContextRecord->V[Index + 8].D[0] = JumpBuffer->FpRegs[Index];
            ContextRecord->V[Index + 8].D[1] = 0.0;
        }
    }

    if ((ExceptionRecord != NULL) &&
        (ExceptionRecord->ExceptionCode == STATUS_UNWIND_CONSOLIDATE) &&
        (ExceptionRecord->NumberParameters >= 1))
    {
        PRTL_CONSOLIDATE_CALLBACK Consolidate;

        Consolidate = (PRTL_CONSOLIDATE_CALLBACK)
            ExceptionRecord->ExceptionInformation[0];
        RtlpArm64ConsolidateCallback(ContextRecord,
                                     Consolidate,
                                     ExceptionRecord);
    }

    RtlpRestoreContextInternal(ContextRecord);
}
