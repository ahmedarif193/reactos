/*
 * PROJECT:     ReactOS MSVCRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 exception-runtime failure boundaries
 */

#include <intrin.h>
#include "include/fpieee.h"
#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "msvcrt.h"
#include "excpt.h"

static DECLSPEC_NORETURN void
RiscVExceptionRuntimeUnavailable(void)
{
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

void *
call_catch_handler(EXCEPTION_RECORD *ExceptionRecord)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    RiscVExceptionRuntimeUnavailable();
}

void *
call_unwind_handler(
    void *Handler,
    ULONG_PTR Frame,
    DISPATCHER_CONTEXT *DispatcherContext)
{
    UNREFERENCED_PARAMETER(Handler);
    UNREFERENCED_PARAMETER(Frame);
    UNREFERENCED_PARAMETER(DispatcherContext);
    RiscVExceptionRuntimeUnavailable();
}

ULONG_PTR
get_exception_pc(DISPATCHER_CONTEXT *DispatcherContext)
{
    /* Use an address inside the call instruction for scope lookup. Both
     * two-byte and four-byte instructions have the same enclosing scope. */
    return DispatcherContext->ControlPc -
           (DispatcherContext->ControlPcIsUnwound ? 2 : 0);
}

int
handle_fpieee_flt(
    __msvcrt_ulong ExceptionCode,
    EXCEPTION_POINTERS *ExceptionPointers,
    int (__cdecl *Handler)(_FPIEEE_RECORD *))
{
    UNREFERENCED_PARAMETER(ExceptionCode);
    UNREFERENCED_PARAMETER(ExceptionPointers);
    UNREFERENCED_PARAMETER(Handler);

    /* No floating-point exception record was decoded or handled. */
    return EXCEPTION_CONTINUE_SEARCH;
}

void
__cdecl
_local_unwind(void *TargetFrame, void *TargetIp)
{
    RtlUnwind(TargetFrame, TargetIp, NULL, NULL);
}
