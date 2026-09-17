/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 user context and unavailable unwind boundaries
 */

#include <rtl.h>

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
    ThreadContext->Pc = (ULONG64)ThreadStartAddress;
    ThreadContext->Sp = ALIGN_DOWN_BY((ULONG64)StackBase, 16);
    ThreadContext->Ra = (ULONG64)RtlExitUserThread;
    ThreadContext->A0 = (ULONG64)ThreadStartParam;
}
