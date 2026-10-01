/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     User-mode hooks for Windows NT PowerPC exception unwinding
 */

#include <ntdll.h>
#include <ndk/ppc/exception.h>

/* The code entries of KiUserExceptionDispatcher (dispatch.S). */
extern UCHAR KiUserExceptionDispatcherEntry[] __asm__("..KiUserExceptionDispatcher");
extern UCHAR KiPpcUserExceptionDispatcherEnd[] __asm__("..KiPpcUserExceptionDispatcherEnd");

/* Unwind through KiUserExceptionDispatcher into the CONTEXT the kernel saved
 * in its KUSER_EXCEPTION_STACK. */
NTSTATUS
NTAPI
RtlpPpcUnwindSpecialFrame(
    _In_ ULONG_PTR ControlPc,
    _Inout_ PCONTEXT Context)
{
    PKUSER_EXCEPTION_STACK Frame;
    ULONG Required = CONTEXT_CONTROL | CONTEXT_INTEGER;

    if (ControlPc < (ULONG_PTR)KiUserExceptionDispatcherEntry ||
        ControlPc >= (ULONG_PTR)KiPpcUserExceptionDispatcherEnd + 4)
        return STATUS_NOT_FOUND;

    Frame = (PKUSER_EXCEPTION_STACK)Context->Gpr1;
    if ((ULONG_PTR)Frame & 15 ||
        (Frame->Context.ContextFlags & Required) != Required)
        return STATUS_BAD_STACK;

    *Context = Frame->Context;
    return STATUS_SUCCESS;
}

/* User mode has no kernel trap frames to retire. */
VOID
NTAPI
RtlpPpcPrepareContextRestore(
    _In_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
}
