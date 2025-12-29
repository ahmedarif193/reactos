/*
 * Minimal ARM64 unwind stubs for user-mode ntdll
 */

#include <windef.h>
#include <winnt.h>

#ifndef PUNWIND_HISTORY_TABLE
typedef struct _UNWIND_HISTORY_TABLE UNWIND_HISTORY_TABLE, *PUNWIND_HISTORY_TABLE;
#endif

BOOLEAN
NTAPI
RtlAddFunctionTable(PRUNTIME_FUNCTION FunctionTable,
                    DWORD EntryCount,
                    DWORD64 BaseAddress)
{
    UNREFERENCED_PARAMETER(FunctionTable);
    UNREFERENCED_PARAMETER(EntryCount);
    UNREFERENCED_PARAMETER(BaseAddress);
    return FALSE;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(ULONG_PTR ControlPc,
                       ULONG_PTR *ImageBase,
                       PUNWIND_HISTORY_TABLE HistoryTable)
{
    UNREFERENCED_PARAMETER(ControlPc);
    UNREFERENCED_PARAMETER(ImageBase);
    UNREFERENCED_PARAMETER(HistoryTable);
    return NULL;
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(ULONG HandlerType,
                 ULONG_PTR ImageBase,
                 ULONG_PTR ControlPc,
                 PRUNTIME_FUNCTION FunctionEntry,
                 PCONTEXT ContextRecord,
                 PVOID *HandlerData,
                 PULONG_PTR EstablisherFrame,
                 PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    UNREFERENCED_PARAMETER(HandlerType);
    UNREFERENCED_PARAMETER(ImageBase);
    UNREFERENCED_PARAMETER(ControlPc);
    UNREFERENCED_PARAMETER(FunctionEntry);
    UNREFERENCED_PARAMETER(ContextRecord);
    UNREFERENCED_PARAMETER(HandlerData);
    UNREFERENCED_PARAMETER(EstablisherFrame);
    UNREFERENCED_PARAMETER(ContextPointers);
    return NULL;
}

/* RtlDispatchException/RtlUnwind provided by shared librtl on arm64. */
