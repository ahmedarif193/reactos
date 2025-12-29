/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/rtl/rtlexcpt.c
 * PURPOSE:         Exception helper stubs for ARM64 stack walking
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
RtlCaptureContext(_Out_ PCONTEXT ContextRecord)
{
    UINT64 Fp = 0, Lr = 0;

    if (!ContextRecord) return;

    RtlZeroMemory(ContextRecord, sizeof(*ContextRecord));
    ContextRecord->ContextFlags = CONTEXT_FULL;

    __asm__ __volatile__("mov %0, x29" : "=r"(Fp));
    __asm__ __volatile__("mov %0, x30" : "=r"(Lr));

    ContextRecord->Fp = Fp;
    ContextRecord->Lr = Lr;
    ContextRecord->Sp = (ULONG64)__builtin_frame_address(0);
    ContextRecord->Pc = (ULONG64)__builtin_return_address(0);
}
