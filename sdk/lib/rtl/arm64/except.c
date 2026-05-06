/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 exception support
 */

#include <rtl.h>
#include <intrin.h>

#define NDEBUG
#include <debug.h>

#define ARM64_UNWIND_FLAG_MASK 0x3UL
#define ARM64_XDATA_FUNCTION_LENGTH_MASK 0x3FFFFUL
#define ARM64_XDATA_EPILOGUE_PACKED (1UL << 21)
#define ARM64_XDATA_EXCEPTION_DATA  (1UL << 20)
#define ARM64_XDATA_EPILOGUE_COUNT_SHIFT 22
#define ARM64_XDATA_EPILOGUE_COUNT_MASK 0x1FUL
#define ARM64_XDATA_CODE_WORDS_SHIFT 27
#define ARM64_XDATA_CODE_WORDS_MASK 0x1FUL

NTSYSAPI
PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Inout_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable);

static
BOOLEAN
RtlpArm64IsKernelPointer(
    _In_ ULONG_PTR Pointer)
{
    return Pointer >= 0xFFFF000000000000ULL;
}

static
PULONG
RtlpArm64Xdata(
    _In_ ULONG_PTR ImageBase,
    _In_ PRUNTIME_FUNCTION FunctionEntry)
{
    if ((FunctionEntry == NULL) ||
        ((FunctionEntry->UnwindData & ARM64_UNWIND_FLAG_MASK) != 0))
    {
        return NULL;
    }

    return (PULONG)(ImageBase + FunctionEntry->UnwindData);
}

static
BOOLEAN
RtlpArm64GetExceptionHandler(
    _In_ ULONG_PTR ImageBase,
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Out_ PEXCEPTION_ROUTINE *ExceptionRoutine,
    _Out_ PVOID *HandlerData)
{
    PULONG Xdata;
    ULONG Header;
    ULONG CodeWords;
    ULONG EpilogueScopes;
    ULONG Offset;
    ULONG HandlerRva;

    Xdata = RtlpArm64Xdata(ImageBase, FunctionEntry);
    if (Xdata == NULL)
        return FALSE;

    Header = Xdata[0];
    if ((Header & ARM64_XDATA_EXCEPTION_DATA) == 0)
        return FALSE;

    CodeWords = (Header >> ARM64_XDATA_CODE_WORDS_SHIFT) &
                ARM64_XDATA_CODE_WORDS_MASK;
    Offset = 1;

    if ((Header & ARM64_XDATA_EPILOGUE_PACKED) == 0)
    {
        EpilogueScopes = (Header >> ARM64_XDATA_EPILOGUE_COUNT_SHIFT) &
                         ARM64_XDATA_EPILOGUE_COUNT_MASK;
        Offset += EpilogueScopes;
    }

    Offset += CodeWords;

    HandlerRva = Xdata[Offset++];
    *ExceptionRoutine = (PEXCEPTION_ROUTINE)(ImageBase + HandlerRva);
    *HandlerData = &Xdata[Offset];
    return TRUE;
}

VOID
NTAPI
RtlCaptureContext(
    _Out_ PCONTEXT ContextRecord)
{
    ULONG64 StackPointer;

    RtlZeroMemory(ContextRecord, sizeof(*ContextRecord));

    __asm__ __volatile__("mov %0, sp" : "=r"(StackPointer));

    ContextRecord->ContextFlags = CONTEXT_FULL;
    ContextRecord->Sp = StackPointer;
    ContextRecord->Fp = (ULONG64)__builtin_frame_address(0);
    ContextRecord->Lr = (ULONG64)_ReturnAddress();
    ContextRecord->Pc = (ULONG64)_ReturnAddress();
}

VOID
NTAPI
RtlGetCallersAddress(
    _Out_ PVOID *CallersAddress,
    _Out_ PVOID *CallersCaller)
{
    *CallersAddress = _ReturnAddress();
    *CallersCaller = NULL;
}

BOOLEAN
NTAPI
RtlDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT ContextRecord)
{
    CONTEXT UnwindContext;
    ULONG_PTR ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    PEXCEPTION_ROUTINE ExceptionRoutine;
    DISPATCHER_CONTEXT DispatcherContext;
    PVOID HandlerData;
    EXCEPTION_DISPOSITION Disposition;
    ULONG Frames;

    if (RtlCallVectoredExceptionHandlers(ExceptionRecord, ContextRecord))
    {
        RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
        return TRUE;
    }

    UnwindContext = *ContextRecord;

    for (Frames = 0; Frames < 64; Frames++)
    {
        ImageBase = 0;
        FunctionEntry = RtlLookupFunctionEntry(UnwindContext.Pc,
                                               (PULONG_PTR)&ImageBase,
                                               NULL);

        if (FunctionEntry == NULL)
        {
            if ((UnwindContext.Lr == 0) ||
                (UnwindContext.Lr == UnwindContext.Pc))
            {
                break;
            }

            UnwindContext.Pc = UnwindContext.Lr;
            continue;
        }

        if (RtlpArm64GetExceptionHandler(ImageBase,
                                         FunctionEntry,
                                         &ExceptionRoutine,
                                         &HandlerData))
        {
            RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));
            DispatcherContext.ControlPc = UnwindContext.Pc;
            DispatcherContext.ImageBase = ImageBase;
            DispatcherContext.FunctionEntry = FunctionEntry;
            DispatcherContext.EstablisherFrame = UnwindContext.Fp;
            DispatcherContext.ContextRecord = ContextRecord;
            DispatcherContext.LanguageHandler = ExceptionRoutine;
            DispatcherContext.HandlerData = HandlerData;

            Disposition = ExceptionRoutine(ExceptionRecord,
                                           (PVOID)DispatcherContext.EstablisherFrame,
                                           ContextRecord,
                                           &DispatcherContext);
            if (Disposition == ExceptionContinueExecution)
            {
                RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
                return TRUE;
            }

            if (Disposition != ExceptionContinueSearch)
            {
                break;
            }
        }

        if (!RtlpArm64IsKernelPointer((ULONG_PTR)UnwindContext.Fp) ||
            (UnwindContext.Fp & (sizeof(ULONG64) - 1)))
        {
            break;
        }

        UnwindContext.Lr = *(PULONG64)(ULONG_PTR)(UnwindContext.Fp + sizeof(ULONG64));
        UnwindContext.Sp = UnwindContext.Fp + (2 * sizeof(ULONG64));
        UnwindContext.Fp = *(PULONG64)(ULONG_PTR)UnwindContext.Fp;

        if (UnwindContext.Lr == 0)
            break;

        UnwindContext.Pc = UnwindContext.Lr;
    }

    RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
    return FALSE;
}

VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    UNREFERENCED_PARAMETER(TargetFrame);
    UNREFERENCED_PARAMETER(TargetIp);
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ReturnValue);

    ASSERT(FALSE);
}

VOID
NTAPI
RtlInitializeContext(
    _In_ HANDLE ProcessHandle,
    _Out_ PCONTEXT ThreadContext,
    _In_opt_ PVOID ThreadStartParam,
    _In_ PTHREAD_START_ROUTINE ThreadStartAddress,
    _In_ PINITIAL_TEB StackBase)
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(ThreadStartParam);

    RtlZeroMemory(ThreadContext, sizeof(*ThreadContext));

    ThreadContext->ContextFlags = CONTEXT_FULL;
    ThreadContext->Pc = (ULONG64)ThreadStartAddress;
    ThreadContext->Sp = (ULONG64)StackBase->StackBase;
}
