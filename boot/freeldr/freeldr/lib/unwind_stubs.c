#include <freeldr.h>
#include <ndk/rtltypes.h>

#if defined(_M_ARM64) || defined(__aarch64__)

PRUNTIME_FUNCTION
RtlLookupFunctionEntry(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Inout_opt_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    UNREFERENCED_PARAMETER(ControlPc);
    UNREFERENCED_PARAMETER(ImageBase);
    UNREFERENCED_PARAMETER(HistoryTable);
    return NULL;
}

VOID
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT OriginalContext,
    _In_opt_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    UNREFERENCED_PARAMETER(TargetFrame);
    UNREFERENCED_PARAMETER(TargetIp);
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ReturnValue);
    UNREFERENCED_PARAMETER(OriginalContext);
    UNREFERENCED_PARAMETER(HistoryTable);

    /* No structured exception support is available in the boot environment. */
    for (;;) {
        __asm__ volatile("wfi");
    }
}

BOOLEAN
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    UNREFERENCED_PARAMETER(HandlerType);
    UNREFERENCED_PARAMETER(ImageBase);
    UNREFERENCED_PARAMETER(ControlPc);
    UNREFERENCED_PARAMETER(FunctionEntry);
    UNREFERENCED_PARAMETER(ContextRecord);
    UNREFERENCED_PARAMETER(HandlerData);
    UNREFERENCED_PARAMETER(EstablisherFrame);
    UNREFERENCED_PARAMETER(ContextPointers);
    return FALSE;
}

void
__chkstk(void)
{
    /* Stack probing is unnecessary in the loader; the stack is pre-reserved. */
}

#endif /* ARM64 */
