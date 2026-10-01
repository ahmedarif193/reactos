/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC exception dispatch and unwinding
 */

#include <rtl.h>
#include "unwind.h"

#define MAX_FRAMES 256
/* The handler wrappers keep the active DISPATCHER_CONTEXT pointer 8 bytes
 * below their establisher frame (see context_asm.S). */
#define GUARD_DISPATCHER_OFFSET 8

/* NT PPC exposes only the first four words to language handlers. Keep the
 * bookkeeping needed by this dispatcher after that public ABI prefix. */
typedef struct _PPC_DISPATCHER_CONTEXT
{
    DISPATCHER_CONTEXT Public;
    ULONG_PTR TargetPc;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    PUNWIND_HISTORY_TABLE HistoryTable;
    ULONG ScopeIndex;
} PPC_DISPATCHER_CONTEXT, *PPPC_DISPATCHER_CONTEXT;

/* A Windows NT PowerPC function descriptor. */
typedef struct _PPC_FUNCTION_DESCRIPTOR
{
    ULONG_PTR EntryPoint;
    ULONG_PTR Toc;
} PPC_FUNCTION_DESCRIPTOR, *PPPC_FUNCTION_DESCRIPTOR;

static
DECLSPEC_NORETURN
VOID
Corruption(NTSTATUS Status)
{
    EXCEPTION_RECORD Record = {0};

    Record.ExceptionCode = Status;
    Record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    RtlRaiseException(&Record);
    for (;;);
}

static
BOOLEAN
ControlPcValid(ULONG_PTR Pc)
{
    return Pc && !(Pc & 3);
}

VOID
NTAPI
RtlInitializeContext(
    _Reserved_ HANDLE ProcessHandle,
    _Out_ PCONTEXT ThreadContext,
    _In_opt_ PVOID ThreadStartParam,
    _In_ PTHREAD_START_ROUTINE ThreadStartAddress,
    _In_ PINITIAL_TEB StackBase)
{
    PPPC_FUNCTION_DESCRIPTOR Start = (PPPC_FUNCTION_DESCRIPTOR)ThreadStartAddress;
    PPPC_FUNCTION_DESCRIPTOR Exit = (PPPC_FUNCTION_DESCRIPTOR)RtlExitUserThread;

    UNREFERENCED_PARAMETER(ProcessHandle);

    RtlZeroMemory(ThreadContext, sizeof(*ThreadContext));
    ThreadContext->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    ThreadContext->Iar = Start->EntryPoint;
    ThreadContext->Gpr2 = Start->Toc;
    /* Leave room for the callee's frame header below the initial stack. */
    ThreadContext->Gpr1 = ALIGN_DOWN_BY((ULONG_PTR)StackBase, 16) - 32;
    ThreadContext->Lr = Exit->EntryPoint;
    ThreadContext->Gpr3 = (ULONG_PTR)ThreadStartParam;
}

VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord)
{
    CONTEXT Context;
    ULONG Required = CONTEXT_CONTROL | CONTEXT_INTEGER;

    if (!ContextRecord)
        Corruption(STATUS_INVALID_PARAMETER);
    Context = *ContextRecord;
    if ((Context.ContextFlags & Required) != Required || !ControlPcValid(Context.Iar) ||
        (ExceptionRecord &&
         (ExceptionRecord->ExceptionCode == STATUS_UNWIND_CONSOLIDATE ||
          ExceptionRecord->ExceptionCode == STATUS_LONGJUMP)))
        Corruption(STATUS_INVALID_PARAMETER);

    RtlpPpcPrepareContextRestore(&Context);
    RtlpPpcRestoreContext(&Context);
}

/* Unwind one frame. Current is kept for handler calls and target restore;
 * Next receives the caller's state. */
static
NTSTATUS
Step(
    ULONG Type,
    PCONTEXT Current,
    PCONTEXT Next,
    PPPC_DISPATCHER_CONTEXT Dc)
{
    ULONG_PTR Pc = Current->Iar;

    if (!ControlPcValid(Pc))
        return STATUS_BAD_STACK;
    /* A return address belongs to the call instruction before it. */
    if (Current->ContextFlags & CONTEXT_UNWOUND_TO_CALL)
        Pc -= 4;
    Dc->Public.ControlPc = Pc;
    Dc->Public.FunctionEntry = RtlLookupFunctionEntry(Pc);
    Dc->Public.ContextRecord = Current;
    Dc->ScopeIndex = 0;
    *Next = *Current;
    Dc->LanguageHandler = RtlpPpcVirtualUnwind(Type, Pc, Dc->Public.FunctionEntry,
                                               Next, &Dc->HandlerData,
                                               &Dc->Public.EstablisherFrame, NULL,
                                               NULL, NULL, NULL, 0, 0xffffffffUL);
    if (Next->Gpr1 < Current->Gpr1)
        return STATUS_BAD_STACK;
    return STATUS_SUCCESS;
}

ULONG
NTAPI
RtlWalkFrameChain(
    _Out_writes_to_(Count, return) PVOID *Callers,
    _In_ ULONG Count,
    _In_ ULONG Flags)
{
    CONTEXT Current, Next;
    PPC_DISPATCHER_CONTEXT Dc = {0};
    ULONG Captured = 0, Skip = Flags >> 8, Frames;

    if (!Callers || !Count || (Flags & 0xff))
        return 0;

    RtlCaptureContext(&Current);
    Current.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    for (Frames = 0; Frames < MAX_FRAMES && Captured < Count; ++Frames)
    {
        if (!NT_SUCCESS(Step(UNW_FLAG_NHANDLER, &Current, &Next, &Dc)) ||
            !ControlPcValid(Next.Iar) ||
            (Next.Gpr1 == Current.Gpr1 && Next.Iar == Current.Iar))
            break;
        Current = Next;
        if (Skip)
            --Skip;
        else
            Callers[Captured++] = (PVOID)Current.Iar;
    }
    return Captured;
}

VOID
NTAPI
RtlGetCallersAddress(
    _Out_ PVOID *Caller,
    _Out_ PVOID *CallersCaller)
{
    CONTEXT Current, Next;
    PPC_DISPATCHER_CONTEXT Dc = {0};

    *Caller = *CallersCaller = NULL;
    RtlCaptureContext(&Current);
    Current.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    if (!NT_SUCCESS(Step(UNW_FLAG_NHANDLER, &Current, &Next, &Dc)))
        return;
    Current = Next;
    if (!NT_SUCCESS(Step(UNW_FLAG_NHANDLER, &Current, &Next, &Dc)))
        return;
    *Caller = (PVOID)Next.Iar;
    Current = Next;
    if (NT_SUCCESS(Step(UNW_FLAG_NHANDLER, &Current, &Next, &Dc)))
        *CallersCaller = (PVOID)Next.Iar;
}

static
PPPC_DISPATCHER_CONTEXT
ActiveDispatcherContext(PVOID Frame)
{
    PDISPATCHER_CONTEXT Public =
        *(PDISPATCHER_CONTEXT *)((PUCHAR)Frame - GUARD_DISPATCHER_OFFSET);
    return CONTAINING_RECORD(Public, PPC_DISPATCHER_CONTEXT, Public);
}

/* Guard handler of RtlpExecuteHandlerForException: an exception raised by a
 * language handler is nested in the one being dispatched. */
EXCEPTION_DISPOSITION
NTAPI
RtlpPpcExceptionHandler(
    PEXCEPTION_RECORD Record,
    PVOID Frame,
    PCONTEXT Context,
    PDISPATCHER_CONTEXT Dc)
{
    UNREFERENCED_PARAMETER(Context);

    if (Record->ExceptionFlags & EXCEPTION_UNWIND)
        return ExceptionContinueSearch;
    Dc->EstablisherFrame = ActiveDispatcherContext(Frame)->Public.EstablisherFrame;
    return ExceptionNestedException;
}

/* Guard handler of RtlpExecuteHandlerForUnwind: an unwind that reaches a
 * termination handler still running collides with the active unwind. */
EXCEPTION_DISPOSITION
NTAPI
RtlpPpcUnwindHandler(
    PEXCEPTION_RECORD Record,
    PVOID Frame,
    PCONTEXT Context,
    PDISPATCHER_CONTEXT Dc)
{
    PPPC_DISPATCHER_CONTEXT Active = ActiveDispatcherContext(Frame);
    PPPC_DISPATCHER_CONTEXT Private =
        CONTAINING_RECORD(Dc, PPC_DISPATCHER_CONTEXT, Public);
    ULONG_PTR Target = Private->TargetPc;

    UNREFERENCED_PARAMETER(Context);

    if (!(Record->ExceptionFlags & EXCEPTION_UNWIND))
    {
        Dc->EstablisherFrame = Active->Public.EstablisherFrame;
        return ExceptionNestedException;
    }
    *Private = *Active;
    Private->TargetPc = Target;
    return ExceptionCollidedUnwind;
}

BOOLEAN
NTAPI
RtlDispatchException(
    _In_ PEXCEPTION_RECORD Record,
    _In_ PCONTEXT Context)
{
    CONTEXT Current = *Context, Next;
    PPC_DISPATCHER_CONTEXT Dc = {0};
    ULONG Frames;
    ULONG_PTR Nested = 0;
    EXCEPTION_DISPOSITION Disposition;

    /* The shared RTL is built with _NTOSKRNL_ even when linked into ntdll.
     * The kernel supplies no-op hooks; ntdll supplies the user-mode handlers. */
    if (RtlCallVectoredExceptionHandlers(Record, Context))
    {
        if (Record->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
            Corruption(STATUS_NONCONTINUABLE_EXCEPTION);
        RtlCallVectoredContinueHandlers(Record, Context);
        return TRUE;
    }

    for (Frames = 0; Frames < MAX_FRAMES && Current.Iar; Frames++)
    {
        if (!NT_SUCCESS(Step(UNW_FLAG_EHANDLER, &Current, &Next, &Dc)))
        {
            Record->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return FALSE;
        }
        if (Dc.LanguageHandler)
        {
            ULONG_PTR Frame = Dc.Public.EstablisherFrame;
            Disposition = RtlpExecuteHandlerForException(Record, (PVOID)Frame, Context,
                                                         &Dc.Public, Dc.LanguageHandler);
            if (Frame == Nested)
            {
                Record->ExceptionFlags &= ~EXCEPTION_NESTED_CALL;
                Nested = 0;
            }
            switch (Disposition)
            {
            case ExceptionContinueExecution:
                if (Record->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
                    Corruption(STATUS_NONCONTINUABLE_EXCEPTION);
                RtlCallVectoredContinueHandlers(Record, Context);
                return TRUE;
            case ExceptionContinueSearch:
                break;
            case ExceptionNestedException:
                Nested = max(Nested, Dc.Public.EstablisherFrame);
                Record->ExceptionFlags |= EXCEPTION_NESTED_CALL;
                break;
            default:
                Corruption(STATUS_INVALID_DISPOSITION);
            }
        }
        Current = Next;
    }
    if (Frames == MAX_FRAMES)
        Record->ExceptionFlags |= EXCEPTION_STACK_INVALID;
    return FALSE;
}

VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD Record,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT Context,
    _In_opt_ PUNWIND_HISTORY_TABLE History)
{
    CONTEXT Current, Next;
    PPC_DISPATCHER_CONTEXT Dc = {0};
    EXCEPTION_RECORD Local = {0};
    EXCEPTION_DISPOSITION Disposition;
    ULONG Frames, Collisions;
    ULONG_PTR Target = (ULONG_PTR)TargetFrame;
    BOOLEAN AtTarget;

    if (!Context)
        Corruption(STATUS_INVALID_PARAMETER);
    RtlCaptureContext(&Current);
    Current.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    if (TargetFrame && !ControlPcValid((ULONG_PTR)TargetIp))
        Corruption(STATUS_INVALID_UNWIND_TARGET);
    if (!Record)
    {
        Local.ExceptionCode = STATUS_UNWIND;
        Local.ExceptionAddress = (PVOID)Current.Iar;
        Record = &Local;
    }
    Record->ExceptionFlags |= EXCEPTION_UNWINDING;
    if (!TargetFrame)
        Record->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;
    Dc.TargetPc = (ULONG_PTR)TargetIp;
    Dc.HistoryTable = History;

    for (Frames = 0; Frames < MAX_FRAMES && Current.Iar; Frames++)
    {
        if (!NT_SUCCESS(Step(UNW_FLAG_UHANDLER, &Current, &Next, &Dc)))
            Corruption(STATUS_BAD_STACK);
        if (TargetFrame && Current.Gpr1 > Target)
            Corruption(STATUS_INVALID_UNWIND_TARGET);
        AtTarget = TargetFrame && Dc.Public.EstablisherFrame == Target;
        for (Collisions = 0; Dc.LanguageHandler; Collisions++)
        {
            if (Collisions == MAX_FRAMES)
                Corruption(STATUS_BAD_STACK);
            if (AtTarget)
                Record->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;
            Disposition = RtlpExecuteHandlerForUnwind(Record, (PVOID)Dc.Public.EstablisherFrame,
                                                      &Current, &Dc.Public, Dc.LanguageHandler);
            Record->ExceptionFlags &= ~(EXCEPTION_TARGET_UNWIND | EXCEPTION_COLLIDED_UNWIND);
            if (Disposition == ExceptionContinueSearch)
                break;
            if (Disposition != ExceptionCollidedUnwind)
                Corruption(STATUS_INVALID_DISPOSITION);
            /* Resume the collided unwind from the active pass's context and
             * scope index, never re-running the handler in progress. */
            {
                ULONG ScopeIndex = Dc.ScopeIndex;
                Current = *Dc.Public.ContextRecord;
                if (!NT_SUCCESS(Step(UNW_FLAG_UHANDLER, &Current, &Next, &Dc)))
                    Corruption(STATUS_BAD_STACK);
                Dc.ScopeIndex = ScopeIndex;
                AtTarget = TargetFrame && Dc.Public.EstablisherFrame == Target;
                Record->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
            }
        }
        if (AtTarget)
        {
            *Context = Current;
            Context->Iar = (ULONG_PTR)TargetIp;
            Context->Gpr3 = (ULONG_PTR)ReturnValue;
            /* The target function saved its TOC at 4(entry SP). */
            Context->Gpr2 = *(PULONG)(Target + 4);
            /* Keep the floating-point state recovered from abandoned frames.
             * Their epilogues will not run to restore nonvolatile FPRs. */
            Context->ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
            RtlRestoreContext(Context, Record);
            Corruption(STATUS_INVALID_UNWIND_TARGET);
        }
        Current = Next;
    }
    Corruption(TargetFrame ? STATUS_INVALID_UNWIND_TARGET : STATUS_BAD_STACK);
}

/* Advance a captured context to the caller of the function that captured it. */
VOID
NTAPI
RtlpStepContextToCaller(
    _Inout_ PCONTEXT Context)
{
    CONTEXT Next;
    PPC_DISPATCHER_CONTEXT Dc = {0};

    Context->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    if (!NT_SUCCESS(Step(UNW_FLAG_NHANDLER, Context, &Next, &Dc)))
        Corruption(STATUS_BAD_STACK);
    *Context = Next;
}

VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    CONTEXT Context;
    RtlUnwindEx(TargetFrame, TargetIp, ExceptionRecord, ReturnValue, &Context, NULL);
}

EXCEPTION_DISPOSITION
__cdecl
__C_specific_handler(
    _In_ PEXCEPTION_RECORD Record,
    _In_ PVOID Frame,
    _Inout_ PCONTEXT Context,
    _Inout_ PDISPATCHER_CONTEXT Dc)
{
    PPPC_DISPATCHER_CONTEXT Private =
        CONTAINING_RECORD(Dc, PPC_DISPATCHER_CONTEXT, Public);
    PSCOPE_TABLE Table = (PSCOPE_TABLE)Private->HandlerData;
    EXCEPTION_POINTERS Pointers = {Record, Context};
    ULONG_PTR Pc, Target;
    LONG Filter;

    if (!Table)
        Corruption(STATUS_BAD_FUNCTION_TABLE);

    Pc = Dc->ControlPc;
    Target = Private->TargetPc;


    while (Private->ScopeIndex < Table->Count)
    {
        PPPC_SCOPE_RECORD Scope = &Table->ScopeRecord[Private->ScopeIndex++];

        if (Pc < Scope->BeginAddress || Pc >= Scope->EndAddress)
            continue;

        if (Record->ExceptionFlags & EXCEPTION_UNWIND)
        {
            if ((Record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) &&
                ((Target >= Scope->BeginAddress && Target < Scope->EndAddress) ||
                 (Scope->JumpTarget && Target == Scope->JumpTarget)))
                return ExceptionContinueSearch;
            if (!Scope->JumpTarget)
            {
                RtlpPpcCallFunclet((PVOID)TRUE, Frame, Scope->HandlerAddress, (ULONG_PTR)Frame);
            }
        }
        else if (Scope->JumpTarget)
        {
            Filter = Scope->HandlerAddress == EXCEPTION_EXECUTE_HANDLER ?
                EXCEPTION_EXECUTE_HANDLER :
                RtlpPpcCallFunclet(&Pointers, Frame, Scope->HandlerAddress, (ULONG_PTR)Frame);
            if (Filter < 0)
                return ExceptionContinueExecution;
            if (Filter > 0)
            {
                RtlUnwindEx(Frame, (PVOID)Scope->JumpTarget, Record,
                            UlongToPtr(Record->ExceptionCode), Dc->ContextRecord,
                            Private->HistoryTable);
                Corruption(STATUS_INVALID_UNWIND_TARGET);
            }
        }
    }
    return ExceptionContinueSearch;
}
