/*
 * PROJECT:     ReactOS Kernel ARM64
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     RTL function stubs until full arm64 RTL unwind is implemented
 */

#include <ntoskrnl.h>

typedef struct _RUNTIME_FUNCTION { ULONG BeginAddress; ULONG EndAddress; ULONG UnwindData; } RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ PVOID HistoryTable)
{
    (VOID)ControlPc;
    if (ImageBase) *ImageBase = 0;
    (VOID)HistoryTable;
    return NULL;
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_ PVOID FunctionEntry,
    _Inout_ PVOID ContextRecord,
    _Out_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Inout_opt_ PVOID ContextPointers)
{
    (VOID)HandlerType;
    (VOID)ImageBase;
    (VOID)ControlPc;
    (VOID)FunctionEntry;
    (VOID)ContextRecord;
    if (HandlerData) *HandlerData = NULL;
    if (EstablisherFrame) *EstablisherFrame = 0;
    (VOID)ContextPointers;
    return NULL;
}

VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PVOID ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PVOID OriginalContext,
    _Inout_opt_ PVOID HistoryTable)
{
    (VOID)TargetFrame;
    (VOID)TargetIp;
    (VOID)ExceptionRecord;
    (VOID)ReturnValue;
    (VOID)OriginalContext;
    (VOID)HistoryTable;
}

VOID NTAPI RtlRestoreContext(PCONTEXT Context, PEXCEPTION_RECORD ExceptionRecord)
{
    (VOID)Context;
    (VOID)ExceptionRecord;
}

void _local_unwind2(void) {}
void _global_unwind2(void) {}
int _except_handler2(void) { return 1; }
int _except_handler3(void) { return 1; }
unsigned long long __ull_rshift(unsigned long long v, int s) { return v >> s; }
unsigned long long __ll_rshift(unsigned long long v, int s) { return (unsigned long long)((long long)v >> s); }
unsigned long long __ll_lshift(unsigned long long v, int s) { return v << s; }

__asm__(
    ".text\n"
    ".globl _abnormal_termination\n"
    ".def _abnormal_termination; .scl 2; .type 32; .endef\n"
    "_abnormal_termination:\n"
    "    mov w0, wzr\n"
    "    ret\n");
