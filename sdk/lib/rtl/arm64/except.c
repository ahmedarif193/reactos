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
#ifndef UNW_FLAG_EHANDLER
#define UNW_FLAG_EHANDLER 0x1
#define UNW_FLAG_UHANDLER 0x2
#endif

NTSYSAPI
PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Inout_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable);

NTSYSAPI
PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT Context,
    _Out_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Inout_opt_ PVOID ContextPointers);

static
BOOLEAN
RtlpArm64IsStackPointerValid(
    _In_ ULONG_PTR Pointer,
    _In_ ULONG_PTR StackLow,
    _In_ ULONG_PTR StackHigh)
{
    return (Pointer >= StackLow) &&
           (Pointer < StackHigh) &&
           ((Pointer & (sizeof(ULONG64) - 1)) == 0);
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
    ULONG EpilogueScopes = 0;
    ULONG Offset;
    ULONG HeaderWords;
    ULONG HandlerRva;

    Xdata = RtlpArm64Xdata(ImageBase, FunctionEntry);
    if (Xdata == NULL)
        return FALSE;

    Header = Xdata[0];
    if ((Header & ARM64_XDATA_EXCEPTION_DATA) == 0)
        return FALSE;

    CodeWords = (Header >> ARM64_XDATA_CODE_WORDS_SHIFT) &
                ARM64_XDATA_CODE_WORDS_MASK;

    /*
     * Handle extended .xdata header: when both CodeWords and EpilogCount
     * in word 0 are 0 and the E bit is not set, word 1 contains
     * Extended Epilog Count (low 16 bits) and Extended Code Words (high 8 bits).
     */
    if (CodeWords == 0 &&
        ((Header >> ARM64_XDATA_EPILOGUE_COUNT_SHIFT) &
         ARM64_XDATA_EPILOGUE_COUNT_MASK) == 0 &&
        (Header & ARM64_XDATA_EPILOGUE_PACKED) == 0)
    {
        HeaderWords = 2;
        CodeWords = (Xdata[1] >> 16) & 0xFF;
        EpilogueScopes = Xdata[1] & 0xFFFF;
    }
    else
    {
        HeaderWords = 1;
        if ((Header & ARM64_XDATA_EPILOGUE_PACKED) == 0)
        {
            EpilogueScopes = (Header >> ARM64_XDATA_EPILOGUE_COUNT_SHIFT) &
                             ARM64_XDATA_EPILOGUE_COUNT_MASK;
        }
    }

    Offset = HeaderWords + EpilogueScopes + CodeWords;

    HandlerRva = Xdata[Offset++];
    *ExceptionRoutine = (PEXCEPTION_ROUTINE)(ImageBase + HandlerRva);
    *HandlerData = &Xdata[Offset];
    return TRUE;
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
    ULONG64 EstablisherFrame;
    ULONG_PTR StackLow;
    ULONG_PTR StackHigh;

    if (RtlCallVectoredExceptionHandlers(ExceptionRecord, ContextRecord))
    {
        RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
        return TRUE;
    }

    UnwindContext = *ContextRecord;
    RtlpGetStackLimits(&StackLow, &StackHigh);

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

        /*
         * Try RtlVirtualUnwind for proper PE/COFF unwinding first.
         * If it returns meaningful results, use those instead of FP fallback.
         */
        {
            CONTEXT VuContext = UnwindContext;
            ULONG_PTR FrameControlPc = UnwindContext.Pc;
            ULONG64 VuEstablisherFrame;
            PEXCEPTION_ROUTINE VuRoutine;
            PVOID VuHandlerData;
            BOOLEAN UsingVu = FALSE;

            VuRoutine = RtlVirtualUnwind(UNW_FLAG_EHANDLER,
                                         ImageBase,
                                         UnwindContext.Pc,
                                         FunctionEntry,
                                         &VuContext,
                                         &VuHandlerData,
                                         &VuEstablisherFrame,
                                         NULL);

            if (VuContext.Pc != UnwindContext.Pc &&
                VuContext.Sp > UnwindContext.Sp)
            {
                UnwindContext = VuContext;
                EstablisherFrame = VuEstablisherFrame;
                ExceptionRoutine = VuRoutine;
                HandlerData = VuHandlerData;
                UsingVu = TRUE;
            }

            if (!UsingVu)
            {
                ULONG64 FrameFp = UnwindContext.Fp;
                ULONG64 NextFp;
                ULONG64 NextLr;

                HandlerData = NULL;
                ExceptionRoutine = NULL;
                if (!RtlpArm64GetExceptionHandler(ImageBase,
                                                   FunctionEntry,
                                                   &ExceptionRoutine,
                                                   &HandlerData))
                {
                    ExceptionRoutine = NULL;
                    HandlerData = NULL;
                }

                if (!RtlpArm64IsStackPointerValid((ULONG_PTR)FrameFp,
                                                  StackLow,
                                                  StackHigh) ||
                    !RtlpArm64IsStackPointerValid((ULONG_PTR)(FrameFp + sizeof(ULONG64)),
                                                  StackLow,
                                                  StackHigh))
                {
                    break;
                }

                EstablisherFrame = FrameFp;
                NextFp = *(PULONG64)(ULONG_PTR)FrameFp;
                NextLr = *(PULONG64)(ULONG_PTR)(FrameFp + sizeof(ULONG64));

                if ((NextLr == 0) ||
                    ((NextFp != 0) &&
                     (!RtlpArm64IsStackPointerValid((ULONG_PTR)NextFp,
                                                    StackLow,
                                                    StackHigh) ||
                      (NextFp <= FrameFp))))
                {
                    break;
                }

                UnwindContext.Lr = NextLr;
                UnwindContext.Sp = UnwindContext.Fp + (2 * sizeof(ULONG64));
                UnwindContext.Fp = NextFp;
                UnwindContext.Pc = UnwindContext.Lr;

            }

            if (ExceptionRoutine != NULL)
            {
                RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));
                DispatcherContext.ControlPc = FrameControlPc;
                DispatcherContext.ImageBase = ImageBase;
                DispatcherContext.FunctionEntry = FunctionEntry;
                DispatcherContext.EstablisherFrame = EstablisherFrame;
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
        }
    }

    RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
    return FALSE;
}

VOID
NTAPI
RtlInitializeContext(
    _In_ HANDLE ProcessHandle,
    _Out_ PCONTEXT ThreadContext,
    _In_opt_ PVOID ThreadStartParam,
    _In_ PTHREAD_START_ROUTINE ThreadStartAddress,
    _In_ PINITIAL_TEB InitialStack)
{
    UNREFERENCED_PARAMETER(ProcessHandle);

    RtlZeroMemory(ThreadContext, sizeof(*ThreadContext));

    ThreadContext->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_ARM64;
    ThreadContext->Pc = (ULONG64)ThreadStartAddress;
    ThreadContext->Sp = ALIGN_DOWN_BY((ULONG64)InitialStack, 16);
    ThreadContext->X0 = (ULONG64)ThreadStartParam;
}
