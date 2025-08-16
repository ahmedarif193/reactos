/*
 * ARM64 Unwind Function Declarations
 * These declarations are for functions stubbed on ARM64
 */

#ifndef _ARM64_UNWIND_STUBS_H
#define _ARM64_UNWIND_STUBS_H

#ifdef _M_ARM64

/* Forward declarations for ARM64 stubs */
PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    IN ULONG64 ControlPc,
    OUT PULONG64 ImageBase,
    IN OUT PVOID HistoryTable OPTIONAL);

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    IN ULONG HandlerType,
    IN ULONG64 ImageBase,
    IN ULONG64 ControlPc,
    IN PRUNTIME_FUNCTION FunctionEntry,
    IN OUT PCONTEXT ContextRecord,
    OUT PVOID *HandlerData,
    OUT PULONG64 EstablisherFrame,
    IN OUT PVOID ContextPointers OPTIONAL);

BOOLEAN
NTAPI
RtlAddFunctionTable(
    IN PRUNTIME_FUNCTION FunctionTable,
    IN ULONG EntryCount,
    IN ULONG64 BaseAddress);

#endif /* _M_ARM64 */

#endif /* _ARM64_UNWIND_STUBS_H */