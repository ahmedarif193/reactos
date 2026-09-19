/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* Native RISC-V64 RVUW search and unwind passes, integer LP64 ABI. */
#include <rtl.h>
#include "unwind.h"

#define MAX_FRAMES 256
#define GUARD_DISPATCHER_OFFSET 112

static DECLSPEC_NORETURN VOID Corruption(NTSTATUS Status)
{
    /* Do not recursively raise through the metadata that just failed. */
    RtlpRiscv64RaiseFatal(Status);
}

static BOOLEAN ControlPcValid(ULONG64 Pc)
{
    return Pc && !(Pc & 1) &&
        ((LONG64)Pc >> 38 == ((Pc >> 38) & 1 ? -1 : 0));
}

static BOOLEAN UnwindTargetMatches(ULONG64 Target, ULONG64 ImageBase,
                                    PRUNTIME_FUNCTION TargetFunction,
                                    PDISPATCHER_CONTEXT Dc)
{
    PRUNTIME_FUNCTION Function = Dc->FunctionEntry;
    if (!Target || Dc->EstablisherFrame != Target) return FALSE;
    if (!TargetFunction) return Function == NULL;
    if (Dc->ImageBase != ImageBase || !Function) return FALSE;
    if (!NT_SUCCESS(RtlpRiscv64PrimaryEntry(Dc->ImageBase, &Function)))
        Corruption(STATUS_BAD_FUNCTION_TABLE);
    return Function->BeginAddress == TargetFunction->BeginAddress &&
           Function->EndAddress == TargetFunction->EndAddress &&
           Function->UnwindData == TargetFunction->UnwindData;
}

static VOID Snapshot(PRISCV64_NONVOLATILE_CONTEXT_V1 Nv, PCONTEXT Context)
{
    ULONG i;
    Nv->Version = RISCV64_NONVOLATILE_CONTEXT_VERSION;
    Nv->Size = sizeof(*Nv);
    Nv->S[0] = Context->S0;
    Nv->S[1] = Context->S1;
    for (i = 2; i < 12; i++) Nv->S[i] = Context->X[16 + i];
}

/* Current is retained for target restoration and collided unwind; Next is
 * the transactional caller state. The snapshot belongs to Current. */
static NTSTATUS Step(ULONG Type, PCONTEXT Current, PCONTEXT Next,
                     PDISPATCHER_CONTEXT Dc,
                     PRISCV64_NONVOLATILE_CONTEXT_V1 Nv)
{
    ULONG64 Pc = Current->Pc;
    NTSTATUS Status;
    if (!ControlPcValid(Pc)) return STATUS_BAD_STACK;
    Dc->ControlPc = Pc;
    Dc->ControlPcIsUnwound = !!(Current->ContextFlags & CONTEXT_UNWOUND_TO_CALL);
    if (Dc->ControlPcIsUnwound) {
        if (Pc < 2) return STATUS_BAD_STACK;
        Pc -= 2;
    }
    Status = RtlpRiscv64LookupFunctionEntry(Pc, &Dc->ImageBase, &Dc->FunctionEntry);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND) return Status;
    Snapshot(Nv, Current);
    Dc->NonVolatileRegisters = (PUCHAR)Nv;
    Dc->ContextRecord = Current;
    Dc->ScopeIndex = 0;
    *Next = *Current;
    Status = RtlVirtualUnwind2(Type, Dc->ImageBase, Pc, Dc->FunctionEntry,
        Next, NULL, &Dc->HandlerData, (PULONG_PTR)&Dc->EstablisherFrame,
        NULL, NULL, NULL, &Dc->LanguageHandler, 0);
    if (!NT_SUCCESS(Status)) return Status;
    if (Next->Sp < Current->Sp) return STATUS_BAD_STACK;
    if (Dc->LanguageHandler)
        return RtlpRiscv64PrimaryEntry(Dc->ImageBase, &Dc->FunctionEntry);
    return STATUS_SUCCESS;
}

ULONG
NTAPI
RtlWalkFrameChain(_Out_writes_to_(Count, return) PVOID *Callers,
                  _In_ ULONG Count,
                  _In_ ULONG Flags)
{
    CONTEXT Current, Next;
    DISPATCHER_CONTEXT Dc = {0};
    RISCV64_NONVOLATILE_CONTEXT_V1 Nv;
    ULONG Captured = 0, Skip = Flags >> 8, Frames;

    /* Bit zero requests a kernel-to-user stack transition. The current
     * stack's image lookup and limits cannot be reused for that operation. */
    if (!Callers || !Count || (Flags & 0xff))
        return 0;

    RtlCaptureContext(&Current);
    Current.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    for (Frames = 0; Frames < MAX_FRAMES && Captured < Count; ++Frames)
    {
        if (!NT_SUCCESS(Step(UNW_FLAG_NHANDLER, &Current, &Next, &Dc, &Nv)) ||
            !ControlPcValid(Next.Pc) ||
            (Next.Sp == Current.Sp && Next.Pc == Current.Pc))
            break;
        Current = Next;
        if (Skip)
            --Skip;
        else
            Callers[Captured++] = (PVOID)(ULONG_PTR)Current.Pc;
    }
    return Captured;
}

static VOID ReadActive(PVOID Frame, PDISPATCHER_CONTEXT Active)
{
    PDISPATCHER_CONTEXT Pointer;
    ULONG_PTR Low, High, Address = (ULONG_PTR)Frame;
    RtlpGetStackLimits(&Low, &High);
    if (Address < Low || Address > High || High - Address < 128 ||
        !NT_SUCCESS(RtlpRiscv64ReadMemory(&Pointer,
            (PUCHAR)Frame + GUARD_DISPATCHER_OFFSET, sizeof(Pointer))) ||
        (ULONG_PTR)Pointer < Low || (ULONG_PTR)Pointer > High ||
        High - (ULONG_PTR)Pointer < sizeof(*Active) ||
        !NT_SUCCESS(RtlpRiscv64ReadMemory(Active, Pointer, sizeof(*Active))))
        Corruption(STATUS_BAD_STACK);
}

EXCEPTION_DISPOSITION NTAPI
RtlpRiscv64ExceptionHandler(PEXCEPTION_RECORD Record, PVOID Frame,
                           PCONTEXT Context, PDISPATCHER_CONTEXT Dc)
{
    DISPATCHER_CONTEXT Active;
    UNREFERENCED_PARAMETER(Context);
    if (Record->ExceptionFlags & EXCEPTION_UNWIND) return ExceptionContinueSearch;
    ReadActive(Frame, &Active);
    Dc->EstablisherFrame = Active.EstablisherFrame;
    return ExceptionNestedException;
}

EXCEPTION_DISPOSITION NTAPI
RtlpRiscv64UnwindHandler(PEXCEPTION_RECORD Record, PVOID Frame,
                        PCONTEXT Context, PDISPATCHER_CONTEXT Dc)
{
    DISPATCHER_CONTEXT Active;
    ULONG64 Target = Dc->TargetPc;
    UNREFERENCED_PARAMETER(Context);
    ReadActive(Frame, &Active);
    if (!(Record->ExceptionFlags & EXCEPTION_UNWIND)) {
        Dc->EstablisherFrame = Active.EstablisherFrame;
        return ExceptionNestedException;
    }
    *Dc = Active;
    Dc->TargetPc = Target;
    return ExceptionCollidedUnwind;
}

BOOLEAN NTAPI
RtlDispatchException(PEXCEPTION_RECORD Record, PCONTEXT Context)
{
    CONTEXT Current = *Context, Next;
    DISPATCHER_CONTEXT Dc = {0};
    RISCV64_NONVOLATILE_CONTEXT_V1 Nv;
    ULONG Frames;
    ULONG64 Nested = 0;
    EXCEPTION_DISPOSITION Disposition;
    NTSTATUS Status;

    if (RtlCallVectoredExceptionHandlers(Record, Context)) {
        if (Record->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
            Corruption(STATUS_NONCONTINUABLE_EXCEPTION);
        RtlCallVectoredContinueHandlers(Record, Context);
        return TRUE;
    }
    for (Frames = 0; Frames < MAX_FRAMES && Current.Pc; Frames++) {
        Status = Step(UNW_FLAG_EHANDLER, &Current, &Next, &Dc, &Nv);
        if (!NT_SUCCESS(Status)) {
            Record->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return FALSE;
        }
        if (Dc.LanguageHandler) {
            ULONG64 Frame = Dc.EstablisherFrame;
            Disposition = RtlpExecuteHandlerForException(Record, (PVOID)Frame,
                Context, &Dc, Dc.LanguageHandler);
            if (Frame == Nested) {
                Record->ExceptionFlags &= ~EXCEPTION_NESTED_CALL;
                Nested = 0;
            }
            switch (Disposition) {
            case ExceptionContinueExecution:
                if (Record->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
                    Corruption(STATUS_NONCONTINUABLE_EXCEPTION);
                RtlCallVectoredContinueHandlers(Record, Context);
                return TRUE;
            case ExceptionContinueSearch:
                break;
            case ExceptionNestedException:
                Nested = max(Nested, Dc.EstablisherFrame);
                Record->ExceptionFlags |= EXCEPTION_NESTED_CALL;
                break;
            default:
                Corruption(STATUS_INVALID_DISPOSITION);
            }
        }
        Current = Next;
    }
    if (Frames == MAX_FRAMES) Record->ExceptionFlags |= EXCEPTION_STACK_INVALID;
    return FALSE;
}

VOID NTAPI
RtlUnwindEx(PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD Record,
            PVOID ReturnValue, PCONTEXT Context, PUNWIND_HISTORY_TABLE History)
{
    CONTEXT Current, Next;
    DISPATCHER_CONTEXT Dc = {0};
    RISCV64_NONVOLATILE_CONTEXT_V1 Nv;
    EXCEPTION_RECORD Local = {0};
    NTSTATUS Status;
    EXCEPTION_DISPOSITION Disposition;
    ULONG Frames, Collisions;
    ULONG_PTR Low, High;
    ULONG64 Target = (ULONG64)TargetFrame;
    ULONG64 TargetImage = 0;
    PRUNTIME_FUNCTION TargetFunction = NULL;
    BOOLEAN AtTarget;

    if (!Context) Corruption(STATUS_INVALID_PARAMETER);
    RtlCaptureContext(&Current);
    Current.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    RtlpGetStackLimits(&Low, &High);
    if (TargetFrame && (Target < Low || Target > High || (Target & 15) ||
                        !ControlPcValid((ULONG64)TargetIp)))
        Corruption(STATUS_INVALID_UNWIND_TARGET);
    if (TargetFrame) {
        Status = RtlpRiscv64LookupFunctionEntry((ULONG64)TargetIp,
                                               &TargetImage, &TargetFunction);
        if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND)
            Corruption(Status);
        if (TargetFunction &&
            !NT_SUCCESS(RtlpRiscv64PrimaryEntry(TargetImage, &TargetFunction)))
            Corruption(STATUS_BAD_FUNCTION_TABLE);
    }
    if (!Record) {
        Local.ExceptionCode = STATUS_UNWIND;
        Local.ExceptionAddress = (PVOID)Current.Pc;
        Record = &Local;
    }
    Record->ExceptionFlags |= EXCEPTION_UNWINDING;
    if (!TargetFrame) Record->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;
    Dc.TargetPc = (ULONG64)TargetIp;
    Dc.HistoryTable = History;
    for (Frames = 0; Frames < MAX_FRAMES && Current.Pc; Frames++) {
        Status = Step(UNW_FLAG_UHANDLER, &Current, &Next, &Dc, &Nv);
        if (!NT_SUCCESS(Status)) Corruption(Status);
        if (TargetFrame && Current.Sp > Target)
            Corruption(STATUS_INVALID_UNWIND_TARGET);
        /* RVUW's handler base can be the frame pointer or the fixed SP.
         * A callee's CFA can equal its caller's handler base. Do not stop
         * at that callee: the target must also belong to the same function. */
        AtTarget = UnwindTargetMatches(Target, TargetImage, TargetFunction, &Dc);
        for (Collisions = 0; Dc.LanguageHandler; Collisions++) {
            if (Collisions == MAX_FRAMES) Corruption(STATUS_BAD_STACK);
            if (AtTarget)
                Record->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;
            Disposition = RtlpExecuteHandlerForUnwind(Record,
                (PVOID)Dc.EstablisherFrame, &Current, &Dc, Dc.LanguageHandler);
            Record->ExceptionFlags &= ~(EXCEPTION_TARGET_UNWIND | EXCEPTION_COLLIDED_UNWIND);
            if (Disposition == ExceptionContinueSearch) break;
            if (Disposition != ExceptionCollidedUnwind)
                Corruption(STATUS_INVALID_DISPOSITION);
            /* The guard supplies the active pre-unwind context and advanced
             * scope index. Reconstruct its caller, then resume remaining
             * scopes, never the termination handler already in progress. */
            {
                ULONG ScopeIndex = Dc.ScopeIndex;
                if (!NT_SUCCESS(RtlpRiscv64ReadMemory(&Current, Dc.ContextRecord,
                                                   sizeof(Current))))
                    Corruption(STATUS_BAD_STACK);
                Status = Step(UNW_FLAG_UHANDLER, &Current, &Next, &Dc, &Nv);
                if (!NT_SUCCESS(Status)) Corruption(Status);
                Dc.ScopeIndex = ScopeIndex;
                AtTarget = UnwindTargetMatches(Target, TargetImage, TargetFunction, &Dc);
                Record->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
            }
        }
        if (AtTarget) {
            *Context = Current;
            Context->Pc = (ULONG64)TargetIp;
            Context->A0 = (ULONG64)ReturnValue;
            Context->ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
            /* An LP64 unwind restores integer callee state only. FP storage
             * in a trap snapshot does not establish FP unwind ownership. */
            Context->ContextFlags &= ~(CONTEXT_FLOATING_POINT & ~CONTEXT_RISCV64);
            RtlRestoreContext(Context, Record);
            Corruption(STATUS_INVALID_UNWIND_TARGET);
        }
        Current = Next;
    }
    Corruption(TargetFrame ? STATUS_INVALID_UNWIND_TARGET : STATUS_BAD_STACK);
}

VOID NTAPI RtlUnwind(PVOID Frame, PVOID Ip, PEXCEPTION_RECORD Record, PVOID Value)
{
    CONTEXT Context;
    RtlUnwindEx(Frame, Ip, Record, Value, &Context, NULL);
}

VOID __cdecl _local_unwind(PVOID Frame, PVOID Ip)
{
    RtlUnwind(Frame, Ip, NULL, NULL);
}

VOID NTAPI RtlGetCallersAddress(PVOID *Caller, PVOID *CallersCaller)
{
    CONTEXT Current, Next;
    DISPATCHER_CONTEXT Dc = {0};
    RISCV64_NONVOLATILE_CONTEXT_V1 Nv;
    *Caller = *CallersCaller = NULL;
    RtlCaptureContext(&Current);
    Current.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    if (!NT_SUCCESS(Step(0, &Current, &Next, &Dc, &Nv))) return;
    *Caller = (PVOID)Next.Pc;
    Current = Next;
    if (NT_SUCCESS(Step(0, &Current, &Next, &Dc, &Nv)))
        *CallersCaller = (PVOID)Next.Pc;
}

VOID NTAPI RtlpRiscv64StepContextToCaller(PCONTEXT Context)
{
    CONTEXT Next;
    DISPATCHER_CONTEXT Dc = {0};
    RISCV64_NONVOLATILE_CONTEXT_V1 Nv;
    NTSTATUS Status;
    Context->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    Status = Step(0, Context, &Next, &Dc, &Nv);
    if (!NT_SUCCESS(Status)) Corruption(Status);
    *Context = Next;
}

EXCEPTION_DISPOSITION __cdecl
__C_specific_handler(PEXCEPTION_RECORD Record, PVOID Frame,
                     PCONTEXT Context, PDISPATCHER_CONTEXT Dc)
{
    RISCV64_SCOPE_RECORD Scope, Earlier;
    RISCV64_NONVOLATILE_CONTEXT_V1 Nv;
    EXCEPTION_POINTERS Pointers = {Record, Context};
    ULONG Count, i, j, OtherCount;
    ULONG64 Pc, Target;
    LONG Filter;

    if (!Dc || !Dc->HandlerData ||
        !NT_SUCCESS(RtlpRiscv64ReadMemory(&Nv, Dc->NonVolatileRegisters, sizeof(Nv))) ||
        Nv.Version != RISCV64_NONVOLATILE_CONTEXT_VERSION || Nv.Size != sizeof(Nv) ||
        !NT_SUCCESS(RtlpRiscv64ReadScope(Dc, 0, &Count, NULL)))
        Corruption(STATUS_BAD_FUNCTION_TABLE);
    /* Validate the entire table before executing any user code. Nested and
     * equal ranges are legal; crossing ranges or outer-before-inner are not. */
    for (i = 0; i < Count; i++) {
        if (!NT_SUCCESS(RtlpRiscv64ReadScope(Dc, i, &OtherCount, &Scope)) || OtherCount != Count)
            Corruption(STATUS_BAD_FUNCTION_TABLE);
        for (j = 0; j < i; j++) {
            if (!NT_SUCCESS(RtlpRiscv64ReadScope(Dc, j, &OtherCount, &Earlier)) || OtherCount != Count)
                Corruption(STATUS_BAD_FUNCTION_TABLE);
            if (Earlier.BeginAddress < Scope.EndAddress && Scope.BeginAddress < Earlier.EndAddress &&
                (Earlier.BeginAddress < Scope.BeginAddress || Earlier.EndAddress > Scope.EndAddress))
                Corruption(STATUS_BAD_FUNCTION_TABLE);
        }
    }
    Pc = Dc->ControlPc;
    if (!ControlPcValid(Pc) || (Dc->ControlPcIsUnwound && Pc < 2))
        Corruption(STATUS_BAD_FUNCTION_TABLE);
    if (Dc->ControlPcIsUnwound) Pc -= 2;
    if (Pc < Dc->ImageBase) Corruption(STATUS_BAD_FUNCTION_TABLE);
    Pc -= Dc->ImageBase;
    Target = Dc->TargetPc - Dc->ImageBase;
    while (Dc->ScopeIndex < Count) {
        i = Dc->ScopeIndex++;
        if (!NT_SUCCESS(RtlpRiscv64ReadScope(Dc, i, &OtherCount, &Scope)) || OtherCount != Count)
            Corruption(STATUS_BAD_FUNCTION_TABLE);
        if (Pc < Scope.BeginAddress || Pc >= Scope.EndAddress) continue;
        if (Record->ExceptionFlags & EXCEPTION_UNWIND) {
            if ((Record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) &&
                ((Target >= Scope.BeginAddress && Target < Scope.EndAddress) ||
                 (Scope.JumpTarget && Target == Scope.JumpTarget)))
                return ExceptionContinueSearch;
            if (!Scope.JumpTarget)
                RtlpRiscv64CallFunclet((PVOID)1, Frame,
                    (PVOID)(Dc->ImageBase + Scope.HandlerAddress), &Nv);
        } else if (Scope.JumpTarget) {
            Filter = Scope.HandlerAddress == 1 ? EXCEPTION_EXECUTE_HANDLER :
                RtlpRiscv64CallFunclet(&Pointers, Frame,
                    (PVOID)(Dc->ImageBase + Scope.HandlerAddress), &Nv);
            if (Filter < 0) return ExceptionContinueExecution;
            if (Filter > 0) {
                RtlUnwindEx(Frame, (PVOID)(Dc->ImageBase + Scope.JumpTarget),
                    Record, UlongToPtr(Record->ExceptionCode), Dc->ContextRecord, Dc->HistoryTable);
                Corruption(STATUS_INVALID_UNWIND_TARGET);
            }
        }
    }
    return ExceptionContinueSearch;
}
