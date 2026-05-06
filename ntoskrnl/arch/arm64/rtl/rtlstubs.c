/*
 * PROJECT:     ReactOS Kernel ARM64
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     RTL function stubs until full arm64 RTL unwind is implemented
 */

#include <ntoskrnl.h>

typedef struct _RUNTIME_FUNCTION
{
    ULONG BeginAddress;
    ULONG UnwindData;
} RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

NTSYSAPI
PVOID
NTAPI
RtlPcToFileHeader(
    _In_ PVOID PcValue,
    _Out_ PVOID *BaseOfImage);

#define ARM64_UNWIND_FLAG_MASK 0x3UL
#define ARM64_PACKED_FUNCTION_LENGTH_SHIFT 2
#define ARM64_PACKED_FUNCTION_LENGTH_MASK 0x7FFUL
#define ARM64_XDATA_FUNCTION_LENGTH_MASK 0x3FFFFUL

static
ULONG
RtlpArm64FunctionLength(
    _In_ ULONG_PTR ImageBase,
    _In_ PRUNTIME_FUNCTION FunctionEntry)
{
    ULONG UnwindData;
    PULONG Xdata;

    UnwindData = FunctionEntry->UnwindData;
    if ((UnwindData & ARM64_UNWIND_FLAG_MASK) != 0)
    {
        return ((UnwindData >> ARM64_PACKED_FUNCTION_LENGTH_SHIFT) &
                ARM64_PACKED_FUNCTION_LENGTH_MASK) * sizeof(ULONG);
    }

    Xdata = (PULONG)(ImageBase + UnwindData);
    return (Xdata[0] & ARM64_XDATA_FUNCTION_LENGTH_MASK) * sizeof(ULONG);
}

static
PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Out_ PULONG Length)
{
    PVOID Table;
    ULONG Size;

    if (!RtlPcToFileHeader((PVOID)(ULONG_PTR)ControlPc, (PVOID *)ImageBase))
    {
        *Length = 0;
        return NULL;
    }

    Table = RtlImageDirectoryEntryToData((PVOID)(ULONG_PTR)*ImageBase,
                                         TRUE,
                                         IMAGE_DIRECTORY_ENTRY_EXCEPTION,
                                         &Size);
    if (Table == NULL)
    {
        *Length = 0;
        return NULL;
    }

    *Length = Size / sizeof(RUNTIME_FUNCTION);
    return Table;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ PVOID HistoryTable)
{
    PRUNTIME_FUNCTION FunctionTable, FunctionEntry;
    ULONG TableLength;
    ULONG_PTR ControlRva;
    ULONG IndexLow, IndexHigh, IndexMid;
    ULONG FunctionLength;

    (VOID)HistoryTable;

    FunctionTable = RtlLookupFunctionTable(ControlPc, ImageBase, &TableLength);
    if (FunctionTable == NULL)
        return NULL;

    ControlRva = (ULONG_PTR)ControlPc - (ULONG_PTR)*ImageBase;
    IndexLow = 0;
    IndexHigh = TableLength;

    while (IndexHigh > IndexLow)
    {
        IndexMid = (IndexLow + IndexHigh) / 2;
        FunctionEntry = &FunctionTable[IndexMid];

        if (ControlRva < FunctionEntry->BeginAddress)
        {
            IndexHigh = IndexMid;
            continue;
        }

        FunctionLength = RtlpArm64FunctionLength((ULONG_PTR)*ImageBase,
                                                FunctionEntry);
        if (ControlRva >= (FunctionEntry->BeginAddress + FunctionLength))
        {
            IndexLow = IndexMid + 1;
            continue;
        }

        return FunctionEntry;
    }

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
    if (HandlerData) *HandlerData = NULL;

    if (ContextRecord != NULL)
    {
        PCONTEXT Context = (PCONTEXT)ContextRecord;

        if (EstablisherFrame) *EstablisherFrame = Context->Fp;
        Context->Pc = Context->Lr;
    }
    else if (EstablisherFrame)
    {
        *EstablisherFrame = 0;
    }

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
