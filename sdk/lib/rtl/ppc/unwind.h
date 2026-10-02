/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC private unwind interface
 */

#pragma once

/* ReactOS-private frame decoder. The exported NT PPC RtlVirtualUnwind has
 * the NT4 caller-PC contract, not this handler-returning interface. */
PEXCEPTION_ROUTINE
NTAPI
RtlpPpcVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG_PTR ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_opt_ PVOID *HandlerData,
    _Out_ PULONG_PTR EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _Out_opt_ PBOOLEAN InFunction,
    _Out_opt_ PBOOLEAN FaultFrame,
    _Out_opt_ PBOOLEAN Epilogue,
    _In_ ULONG LowStackLimit,
    _In_ ULONG HighStackLimit);

/* Unwind a frame the kernel built (user exception dispatch, trap frames).
 * Returns STATUS_NOT_FOUND when ControlPc is not such a frame. Provided by
 * the environment (ntdll or the kernel). */
NTSTATUS
NTAPI
RtlpPpcUnwindSpecialFrame(
    _In_ ULONG_PTR ControlPc,
    _Inout_ PCONTEXT Context);

/* Called right before a context is restored: the kernel retires the trap
 * frames the unwind abandoned. Provided by the environment. */
VOID
NTAPI
RtlpPpcPrepareContextRestore(
    _In_ PCONTEXT Context);

LONG
NTAPI
RtlpPpcCallFunclet(
    _In_ PVOID Argument,
    _In_ PVOID EstablisherFrame,
    _In_ ULONG_PTR CodeAddress,
    _In_ ULONG_PTR R2);

DECLSPEC_NORETURN
VOID
NTAPI
RtlpPpcRestoreContext(
    _In_ PCONTEXT Context);
