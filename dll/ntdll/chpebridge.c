/*
 * PROJECT:     ReactOS ARM64EC runtime
 * PURPOSE:     Native NTDLL call bridges for emulated AMD64 imports
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

/* Fixed-argument native entry point used to carry an ARM64EC va_list. */
ULONG NTAPI vDbgPrintEx(ULONG ComponentId, ULONG Level, PCCH Format, va_list Arguments);

ULONG CDECL
ChpeDbgPrint(PCCH Format, ...)
{
    va_list Arguments;
    ULONG Status;

    va_start(Arguments, Format);
    Status = vDbgPrintEx((ULONG)-1, DPFLTR_ERROR_LEVEL, Format, Arguments);
    va_end(Arguments);
    return Status;
}

ULONG CDECL
ChpeDbgPrintEx(ULONG ComponentId, ULONG Level, PCCH Format, ...)
{
    va_list Arguments;
    ULONG Status;

    va_start(Arguments, Format);
    Status = vDbgPrintEx(ComponentId, Level, Format, Arguments);
    va_end(Arguments);
    return Status;
}

INT CDECL
ChpeSnprintf(PCHAR Buffer, SIZE_T Count, PCSTR Format, ...)
{
    va_list Arguments;
    INT Result;

    va_start(Arguments, Format);
    Result = _vsnprintf(Buffer, Count, Format, Arguments);
    va_end(Arguments);
    return Result;
}

INT CDECL
ChpeSnwprintf(PWCHAR Buffer, SIZE_T Count, PCWSTR Format, ...)
{
    va_list Arguments;
    INT Result;

    va_start(Arguments, Format);
    Result = _vsnwprintf(Buffer, Count, Format, Arguments);
    va_end(Arguments);
    return Result;
}

INT CDECL
ChpeSprintf(PCHAR Buffer, PCSTR Format, ...)
{
    va_list Arguments;
    INT Result;

    va_start(Arguments, Format);
    Result = vsprintf(Buffer, Format, Arguments);
    va_end(Arguments);
    return Result;
}

INT CDECL
ChpeSwprintf(PWCHAR Buffer, PCWSTR Format, ...)
{
    va_list Arguments;
    INT Result;

    va_start(Arguments, Format);
    Result = _vsnwprintf(Buffer, MAXLONG, Format, Arguments);
    va_end(Arguments);
    return Result;
}

PVOID NTAPI
ChpeRtlAllocateHeap(PVOID HeapHandle, ULONG Flags, SIZE_T Size)
{
    return RtlAllocateHeap(HeapHandle, Flags, Size);
}

NTSTATUS NTAPI
ChpeRtlDeleteCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)
{
    return RtlDeleteCriticalSection(CriticalSection);
}

NTSTATUS NTAPI
ChpeRtlEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)
{
    return RtlEnterCriticalSection(CriticalSection);
}

ULONG NTAPI
ChpeRtlGetLastWin32Error(VOID)
{
    return RtlGetLastWin32Error();
}

VOID NTAPI
ChpeRtlSetLastWin32Error(ULONG Win32Error)
{
    RtlSetLastWin32Error(Win32Error);
}

BOOLEAN NTAPI
ChpeRtlFreeHeap(HANDLE HeapHandle, ULONG Flags, PVOID Pointer)
{
    return RtlFreeHeap(HeapHandle, Flags, Pointer);
}

NTSTATUS NTAPI
ChpeRtlLeaveCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)
{
    return RtlLeaveCriticalSection(CriticalSection);
}

PVOID NTAPI
ChpeRtlReAllocateHeap(HANDLE HeapHandle, ULONG Flags, PVOID Pointer, SIZE_T Size)
{
    return RtlReAllocateHeap(HeapHandle, Flags, Pointer, Size);
}

SIZE_T NTAPI
ChpeRtlSizeHeap(PVOID HeapHandle, ULONG Flags, PVOID Pointer)
{
    return RtlSizeHeap(HeapHandle, Flags, Pointer);
}

BOOLEAN CDECL
ChpeRtlAddFunctionTable(PRUNTIME_FUNCTION FunctionTable, ULONG EntryCount, ULONG_PTR BaseAddress)
{
    return RtlAddFunctionTable(FunctionTable, EntryCount, BaseAddress);
}

VOID NTAPI
ChpeRtlCaptureContext(PCONTEXT ContextRecord)
{
    RtlCaptureContext(ContextRecord);
}

PRUNTIME_FUNCTION NTAPI
ChpeRtlLookupFunctionEntry(ULONG_PTR ControlPc, PULONG_PTR ImageBase, PUNWIND_HISTORY_TABLE HistoryTable)
{
    return RtlLookupFunctionEntry(ControlPc, ImageBase, HistoryTable);
}

PEXCEPTION_ROUTINE NTAPI
ChpeRtlVirtualUnwind(ULONG HandlerType, ULONG_PTR ImageBase, ULONG_PTR ControlPc, PRUNTIME_FUNCTION FunctionEntry, PCONTEXT ContextRecord, PVOID *HandlerData, PULONG_PTR EstablisherFrame, PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    return RtlVirtualUnwind(HandlerType, ImageBase, ControlPc, FunctionEntry, ContextRecord, HandlerData, EstablisherFrame, ContextPointers);
}
