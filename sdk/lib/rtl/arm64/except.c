/*
 * PROJECT:         ReactOS Run-Time Library
 * LICENSE:         MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:         Minimal exception helpers for ARM64 user-mode runtime
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

BOOLEAN
NTAPI
RtlpUnwindInternal(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable,
    _In_ ULONG Flags);

VOID
NTAPI
RtlInitializeContext(
    _Reserved_ HANDLE ProcessHandle,
    _Out_ PCONTEXT ThreadContext,
    _In_opt_ PVOID ThreadStartParam,
    _In_ PTHREAD_START_ROUTINE ThreadStartAddress,
    _In_ PINITIAL_TEB StackBase)
{
    UNREFERENCED_PARAMETER(ProcessHandle);

    RtlZeroMemory(ThreadContext, sizeof(*ThreadContext));

    ThreadContext->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    ThreadContext->Pc = (DWORD64)(ULONG_PTR)ThreadStartAddress;
    ThreadContext->Sp = (DWORD64)(ULONG_PTR)StackBase->StackBase;
    ThreadContext->X[0] = (DWORD64)(ULONG_PTR)ThreadStartParam;
    ThreadContext->Cpsr = 0x20000000;
}

BOOLEAN
NTAPI
RtlDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT ContextRecord)
{
    BOOLEAN Handled;

    if (RtlCallVectoredExceptionHandlers(ExceptionRecord, ContextRecord))
    {
        RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
        return TRUE;
    }

    Handled = RtlpUnwindInternal(NULL,
                                 NULL,
                                 ExceptionRecord,
                                 0,
                                 ContextRecord,
                                 NULL,
                                 UNW_FLAG_EHANDLER);

    RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);

    return Handled;
}

/* RtlUnwind is provided by unwind.c for ARM64 */
