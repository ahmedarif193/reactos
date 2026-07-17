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
#ifndef UNW_FLAG_NHANDLER
#define UNW_FLAG_NHANDLER 0x0
#endif
#ifndef UNW_FLAG_EHANDLER
#define UNW_FLAG_EHANDLER 0x1
#define UNW_FLAG_UHANDLER 0x2
#endif

/* RtlLookupFunctionEntry/RtlVirtualUnwind prototypes come from the NDK */

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
BOOLEAN
RtlpArm64UnwindFrameChain(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR StackLow,
    _In_ ULONG_PTR StackHigh,
    _Out_opt_ PULONG64 EstablisherFrame)
{
    ULONG64 FrameFp = Context->Fp;
    ULONG64 NextFp;
    ULONG64 NextLr;

    if (!RtlpArm64IsStackPointerValid((ULONG_PTR)FrameFp,
                                      StackLow,
                                      StackHigh) ||
        !RtlpArm64IsStackPointerValid((ULONG_PTR)(FrameFp + sizeof(ULONG64)),
                                      StackLow,
                                      StackHigh))
    {
        return FALSE;
    }

    NextFp = *(PULONG64)(ULONG_PTR)FrameFp;
    NextLr = *(PULONG64)(ULONG_PTR)(FrameFp + sizeof(ULONG64));

    if ((NextLr == 0) ||
        ((NextFp != 0) &&
         (!RtlpArm64IsStackPointerValid((ULONG_PTR)NextFp,
                                        StackLow,
                                        StackHigh) ||
          (NextFp <= FrameFp))))
    {
        return FALSE;
    }

    if (EstablisherFrame != NULL)
        *EstablisherFrame = FrameFp;

    Context->Lr = NextLr;
    Context->Sp = FrameFp + (2 * sizeof(ULONG64));
    Context->Fp = NextFp;
    Context->Pc = NextLr;
    return TRUE;
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

/*
 * Advance a freshly captured CONTEXT up exactly one frame so it describes this
 * routine's caller instead of the helper that captured it.
 *
 * RtlRaiseStatus / RtlRaiseException capture their own register state with
 * RtlCaptureContext, which records Pc/Sp/Fp pointing INTO the raise helper. The
 * exception logically originates at the helper's call site, so dispatch must
 * begin in the caller (where the __try scope lives). amd64 fixes this by
 * rewriting Rip/Rsp/Rbp from intrinsics; arm64's return address lives in a
 * frame-relative slot rather than at a fixed stack offset, so do one virtual
 * unwind step instead - it yields a fully self-consistent caller context
 * (Pc, Sp, Fp, Lr) regardless of the helper's frame layout.
 */
VOID
NTAPI
RtlpArm64StepContextToCaller(
    _Inout_ PCONTEXT Context)
{
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG_PTR ImageBase = 0;
    ULONG64 EstablisherFrame = 0;
    PVOID HandlerData = NULL;

    FunctionEntry = RtlLookupFunctionEntry(Context->Pc,
                                           (PULONG_PTR)&ImageBase,
                                           NULL);
    if (FunctionEntry != NULL)
    {
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,
                         (ULONG64)ImageBase,
                         Context->Pc,
                         FunctionEntry,
                         Context,
                         &HandlerData,
                         &EstablisherFrame,
                         NULL);
    }
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
    ULONG_PTR LookupPc;

    if (RtlCallVectoredExceptionHandlers(ExceptionRecord, ContextRecord))
    {
        RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
        return TRUE;
    }

    UnwindContext = *ContextRecord;
    RtlpGetStackLimits(&StackLow, &StackHigh);

    for (Frames = 0; Frames < 128; Frames++)
    {
        ImageBase = 0;
        LookupPc = (Frames == 0) ? UnwindContext.Pc : (UnwindContext.Pc - 4);
        FunctionEntry = RtlLookupFunctionEntry(LookupPc,
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

        EstablisherFrame = 0;
        ExceptionRoutine = RtlVirtualUnwind(UNW_FLAG_EHANDLER,
                                            (ULONG64)ImageBase,
                                            LookupPc,
                                            FunctionEntry,
                                            &UnwindContext,
                                            &HandlerData,
                                            &EstablisherFrame,
                                            NULL);

        if ((EstablisherFrame < StackLow) ||
            (EstablisherFrame >= StackHigh) ||
            (EstablisherFrame & (sizeof(ULONG64) - 1)))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            break;
        }

        if (ExceptionRoutine != NULL)
        {
            RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));
            DispatcherContext.ControlPc = LookupPc;
            DispatcherContext.ImageBase = ImageBase;
            DispatcherContext.FunctionEntry = FunctionEntry;
            DispatcherContext.EstablisherFrame = EstablisherFrame;
            DispatcherContext.ContextRecord = ContextRecord;
            DispatcherContext.LanguageHandler = ExceptionRoutine;
            DispatcherContext.HandlerData = HandlerData;
            DispatcherContext.ScopeIndex = 0;

            Disposition = ExceptionRoutine(ExceptionRecord,
                                           (PVOID)(ULONG_PTR)EstablisherFrame,
                                           ContextRecord,
                                           &DispatcherContext);

            if (Disposition == ExceptionContinueExecution)
            {
                if (ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
                {
                    RtlRaiseStatus(STATUS_NONCONTINUABLE_EXCEPTION);
                }

                RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
                return TRUE;
            }

            if ((Disposition != ExceptionContinueSearch) &&
                (Disposition != ExceptionNestedException))
            {
                break;
            }
        }

        if (UnwindContext.Pc == 0)
        {
            break;
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
    ThreadContext->Lr = (ULONG64)RtlExitUserThread;
}
