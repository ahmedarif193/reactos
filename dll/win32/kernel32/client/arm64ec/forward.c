/*
 * PROJECT:     ReactOS Kernel32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64EC local wrappers for x64-callable kernel32 exports
 */

#include <k32.h>
#include <rtlsupportapi.h>

#ifdef __arm64ec__

#undef HeapAlloc
#undef HeapFree

DECLSPEC_IMPORT
BOOLEAN
CDECL
NtdllRtlAddFunctionTable(PRUNTIME_FUNCTION FunctionTable,
                         DWORD EntryCount,
                         DWORD64 BaseAddress) __asm__("RtlAddFunctionTable");

DECLSPEC_IMPORT
VOID
WINAPI
NtdllRtlCaptureContext(PCONTEXT ContextRecord) __asm__("RtlCaptureContext");

DECLSPEC_IMPORT
PRUNTIME_FUNCTION
WINAPI
NtdllRtlLookupFunctionEntry(DWORD64 ControlPc,
                            PDWORD64 ImageBase,
                            PUNWIND_HISTORY_TABLE HistoryTable) __asm__("RtlLookupFunctionEntry");

DECLSPEC_IMPORT
VOID
WINAPI
NtdllRtlUnwind(PVOID TargetFrame,
               PVOID TargetIp,
               PEXCEPTION_RECORD ExceptionRecord,
               PVOID ReturnValue) __asm__("RtlUnwind");

DECLSPEC_IMPORT
VOID
WINAPI
NtdllRtlUnwindEx(PVOID TargetFrame,
                 PVOID TargetIp,
                 PEXCEPTION_RECORD ExceptionRecord,
                 PVOID ReturnValue,
                 PCONTEXT ContextRecord,
                 PUNWIND_HISTORY_TABLE HistoryTable) __asm__("RtlUnwindEx");

DECLSPEC_IMPORT
PEXCEPTION_ROUTINE
WINAPI
NtdllRtlVirtualUnwind(ULONG HandlerType,
                      DWORD64 ImageBase,
                      DWORD64 ControlPc,
                      PRUNTIME_FUNCTION FunctionEntry,
                      PCONTEXT ContextRecord,
                      PVOID *HandlerData,
                      PDWORD64 EstablisherFrame,
                      PKNONVOLATILE_CONTEXT_POINTERS ContextPointers) __asm__("RtlVirtualUnwind");

VOID
WINAPI
DeleteCriticalSection(PCRITICAL_SECTION CriticalSection)
{
    RtlDeleteCriticalSection(CriticalSection);
}

VOID
WINAPI
EnterCriticalSection(PCRITICAL_SECTION CriticalSection)
{
    RtlEnterCriticalSection(CriticalSection);
}

DWORD
WINAPI
GetLastError(VOID)
{
    return RtlGetLastWin32Error();
}

LPVOID
WINAPI
HeapAlloc(HANDLE Heap, DWORD Flags, SIZE_T Size)
{
    return RtlAllocateHeap(Heap, Flags, Size);
}

BOOL
WINAPI
HeapFree(HANDLE Heap, DWORD Flags, LPVOID Memory)
{
    return RtlFreeHeap(Heap, Flags, Memory);
}

VOID
WINAPI
LeaveCriticalSection(PCRITICAL_SECTION CriticalSection)
{
    RtlLeaveCriticalSection(CriticalSection);
}

BOOLEAN
CDECL
RtlAddFunctionTable(PRUNTIME_FUNCTION FunctionTable,
                    DWORD EntryCount,
                    DWORD64 BaseAddress)
{
    return NtdllRtlAddFunctionTable(FunctionTable, EntryCount, BaseAddress);
}

VOID
WINAPI
RtlCaptureContext(PCONTEXT ContextRecord)
{
    NtdllRtlCaptureContext(ContextRecord);
}

PRUNTIME_FUNCTION
WINAPI
RtlLookupFunctionEntry(DWORD64 ControlPc,
                       PDWORD64 ImageBase,
                       PUNWIND_HISTORY_TABLE HistoryTable)
{
    return NtdllRtlLookupFunctionEntry(ControlPc, ImageBase, HistoryTable);
}

VOID
WINAPI
RtlUnwind(PVOID TargetFrame,
          PVOID TargetIp,
          PEXCEPTION_RECORD ExceptionRecord,
          PVOID ReturnValue)
{
    NtdllRtlUnwind(TargetFrame, TargetIp, ExceptionRecord, ReturnValue);
}

VOID
WINAPI
RtlUnwindEx(PVOID TargetFrame,
            PVOID TargetIp,
            PEXCEPTION_RECORD ExceptionRecord,
            PVOID ReturnValue,
            PCONTEXT ContextRecord,
            PUNWIND_HISTORY_TABLE HistoryTable)
{
    NtdllRtlUnwindEx(TargetFrame, TargetIp, ExceptionRecord, ReturnValue,
                     ContextRecord, HistoryTable);
}

PEXCEPTION_ROUTINE
WINAPI
RtlVirtualUnwind(ULONG HandlerType,
                 DWORD64 ImageBase,
                 DWORD64 ControlPc,
                 PRUNTIME_FUNCTION FunctionEntry,
                 PCONTEXT ContextRecord,
                 PVOID *HandlerData,
                 PDWORD64 EstablisherFrame,
                 PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    return NtdllRtlVirtualUnwind(HandlerType, ImageBase, ControlPc, FunctionEntry,
                                 ContextRecord, HandlerData, EstablisherFrame,
                                 ContextPointers);
}

VOID
WINAPI
SetLastError(DWORD Error)
{
    RtlSetLastWin32Error(Error);
}

BOOL
WINAPI
TryEnterCriticalSection(PCRITICAL_SECTION CriticalSection)
{
    return RtlTryEnterCriticalSection(CriticalSection);
}

VOID
__cdecl
_local_unwind(PVOID TargetFrame,
              PVOID TargetIp)
{
    NtdllRtlUnwind(TargetFrame, TargetIp, NULL, 0);
}

#endif /* __arm64ec__ */
