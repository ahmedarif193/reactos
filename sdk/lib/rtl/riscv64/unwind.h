/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Private RISC-V64 RVUW decoder interface
 */

#pragma once

NTSTATUS NTAPI
RtlpRiscv64LookupFunctionEntry(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Out_ PRUNTIME_FUNCTION *FunctionEntry);

NTSTATUS NTAPI
RtlpRiscv64ReadScope(_In_ PDISPATCHER_CONTEXT Dispatcher,
                   _In_ ULONG Index, _Out_ PULONG Count,
                   _Out_opt_ RISCV64_SCOPE_RECORD *Scope);
NTSTATUS NTAPI RtlpRiscv64PrimaryEntry(ULONG64 ImageBase,
                                     PRUNTIME_FUNCTION *Entry);

VOID NTAPI RtlRestoreContext(PCONTEXT Context, PEXCEPTION_RECORD ExceptionRecord);
NTSTATUS NTAPI RtlVirtualUnwind2(ULONG HandlerType, ULONG_PTR ImageBase,
    ULONG_PTR ControlPc, PRUNTIME_FUNCTION FunctionEntry, PCONTEXT Context,
    PBOOLEAN MachineFrameUnwound, PVOID *HandlerData, PULONG_PTR EstablisherFrame,
    PKNONVOLATILE_CONTEXT_POINTERS Pointers, PULONG_PTR Low, PULONG_PTR High,
    PEXCEPTION_ROUTINE *Handler, ULONG Flags);
LONG NTAPI RtlpRiscv64CallFunclet(PVOID Argument, PVOID Frame, PVOID Function,
                               PRISCV64_NONVOLATILE_CONTEXT_V1 Registers);

typedef NTSTATUS
(NTAPI *PRTL_RISCV64_READ_MEMORY)(
    _In_opt_ PVOID ReadContext,
    _In_ ULONG64 Address,
    _Out_writes_bytes_all_(Size) PVOID Buffer,
    _In_ SIZE_T Size);

typedef struct _RTL_RISCV64_UNWIND_VIEW
{
    ULONG64 ImageBase;
    ULONG64 ImageSize;
    ULONG64 StackLow;
    ULONG64 StackHigh;
    PRTL_RISCV64_READ_MEMORY ReadMemory;
    PVOID ReadContext;
} RTL_RISCV64_UNWIND_VIEW, *PRTL_RISCV64_UNWIND_VIEW;

typedef struct _RTL_RISCV64_UNWIND_RESULT
{
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    ULONG64 EstablisherFrame;
    PRUNTIME_FUNCTION PrimaryFunctionEntry;
    LONG EstablisherFrameOffset;
} RTL_RISCV64_UNWIND_RESULT, *PRTL_RISCV64_UNWIND_RESULT;

NTSTATUS
NTAPI
RtlpRiscv64UnwindFrame(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _In_ const RTL_RISCV64_UNWIND_VIEW *View,
    _Inout_ PCONTEXT ContextRecord,
    _Out_ PRTL_RISCV64_UNWIND_RESULT Result,
    _Out_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers);
