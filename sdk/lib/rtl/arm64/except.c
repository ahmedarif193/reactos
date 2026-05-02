/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 exception support
 */

#include <rtl.h>
#include <intrin.h>

#define NDEBUG
#include <debug.h>

VOID
NTAPI
RtlCaptureContext(
    _Out_ PCONTEXT ContextRecord)
{
    ULONG64 StackPointer;

    RtlZeroMemory(ContextRecord, sizeof(*ContextRecord));

    __asm__ __volatile__("mov %0, sp" : "=r"(StackPointer));

    ContextRecord->ContextFlags = CONTEXT_FULL;
    ContextRecord->Sp = StackPointer;
    ContextRecord->Fp = (ULONG64)__builtin_frame_address(0);
    ContextRecord->Lr = (ULONG64)_ReturnAddress();
    ContextRecord->Pc = (ULONG64)_ReturnAddress();
}

VOID
NTAPI
RtlGetCallersAddress(
    _Out_ PVOID *CallersAddress,
    _Out_ PVOID *CallersCaller)
{
    *CallersAddress = _ReturnAddress();
    *CallersCaller = NULL;
}

BOOLEAN
NTAPI
RtlDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(Context);

    ASSERT(FALSE);
    return FALSE;
}

VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    UNREFERENCED_PARAMETER(TargetFrame);
    UNREFERENCED_PARAMETER(TargetIp);
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ReturnValue);

    ASSERT(FALSE);
}

VOID
NTAPI
RtlInitializeContext(
    _In_ HANDLE ProcessHandle,
    _Out_ PCONTEXT ThreadContext,
    _In_opt_ PVOID ThreadStartParam,
    _In_ PTHREAD_START_ROUTINE ThreadStartAddress,
    _In_ PINITIAL_TEB StackBase)
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(ThreadStartParam);

    RtlZeroMemory(ThreadContext, sizeof(*ThreadContext));

    ThreadContext->ContextFlags = CONTEXT_FULL;
    ThreadContext->Pc = (ULONG64)ThreadStartAddress;
    ThreadContext->Sp = (ULONG64)StackBase->StackBase;
}
