/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Transactional RISC-V64 RVUW version 1 frame decoder
 */

#include <rtl.h>

#include "unwind_private.h"

typedef enum _RTL_RISCV64_RULE_KIND
{
    RtlRiscv64RuleSame,
    RtlRiscv64RuleMemory,
    RtlRiscv64RuleRegister
} RTL_RISCV64_RULE_KIND;

typedef struct _RTL_RISCV64_GPR_RULE
{
    RTL_RISCV64_RULE_KIND Kind;
    UCHAR SourceRegister;
    LONG Offset;
} RTL_RISCV64_GPR_RULE;

typedef struct _RTL_RISCV64_FRAME_STATE
{
    UCHAR CfaRegister;
    LONG CfaOffset;
    RTL_RISCV64_GPR_RULE Gpr[13];
} RTL_RISCV64_FRAME_STATE;


static const UCHAR RtlpRiscv64RecoverableGprs[13] =
{
    1, 8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27
};

static BOOLEAN
RtlpRiscv64IsCanonicalAddress(
    _In_ ULONG64 Address)
{
    ULONG64 Sign, Upper;

    Sign = (Address >> 38) & 1;
    Upper = Address >> 39;
    return Upper == (Sign ? ((1ULL << 25) - 1) : 0);
}

static BOOLEAN
RtlpRiscv64IsControlPc(
    _In_ ULONG64 Address)
{
    /* The RVA20/RVA23 common C extension permits 2-byte alignment. */
    return RtlpRiscv64IsCanonicalAddress(Address) && !(Address & 1);
}

BOOLEAN
RtlpRiscv64AddUnsigned(
    _In_ ULONG64 Base,
    _In_ ULONG64 Offset,
    _Out_ PULONG64 Result)
{
    if (Base > MAXULONGLONG - Offset)
        return FALSE;

    *Result = Base + Offset;
    return TRUE;
}

static BOOLEAN
RtlpRiscv64AddSigned(
    _In_ ULONG64 Base,
    _In_ LONG Offset,
    _Out_ PULONG64 Result)
{
    LONGLONG SignedOffset;

    SignedOffset = Offset;
    if (SignedOffset >= 0)
        return RtlpRiscv64AddUnsigned(Base, (ULONG64)SignedOffset, Result);

    if (Base < (ULONG64)(-SignedOffset))
        return FALSE;

    *Result = Base - (ULONG64)(-SignedOffset);
    return TRUE;
}

static NTSTATUS
RtlpRiscv64Read(
    _In_ const RTL_RISCV64_UNWIND_VIEW *View,
    _In_ ULONG64 Address,
    _Out_writes_bytes_all_(Size) PVOID Buffer,
    _In_ SIZE_T Size)
{
    if ((View == NULL) || (View->ReadMemory == NULL))
        return STATUS_INVALID_PARAMETER;

    return View->ReadMemory(View->ReadContext, Address, Buffer, Size);
}

NTSTATUS
RtlpRiscv64ReadImage(
    _In_ const RTL_RISCV64_UNWIND_VIEW *View,
    _In_ ULONG Rva,
    _Out_writes_bytes_all_(Size) PVOID Buffer,
    _In_ SIZE_T Size)
{
    ULONG64 Address;

    if (((ULONG64)Rva > View->ImageSize) ||
        ((ULONG64)Size > View->ImageSize - Rva) ||
        !RtlpRiscv64AddUnsigned(View->ImageBase, Rva, &Address))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    if (!NT_SUCCESS(RtlpRiscv64Read(View, Address, Buffer, Size)))
        return STATUS_BAD_FUNCTION_TABLE;

    return STATUS_SUCCESS;
}

static BOOLEAN
RtlpRiscv64IsIntegerNonvolatile(
    _In_ ULONG Register)
{
    return (Register == 8) || (Register == 9) ||
           ((Register >= 18) && (Register <= 27));
}

static BOOLEAN
RtlpRiscv64IsCfaRegister(
    _In_ ULONG Register)
{
    return (Register == 2) || RtlpRiscv64IsIntegerNonvolatile(Register);
}

static LONG
RtlpRiscv64GetGprRuleIndex(
    _In_ ULONG Register)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(RtlpRiscv64RecoverableGprs); Index++)
    {
        if (RtlpRiscv64RecoverableGprs[Index] == Register)
            return (LONG)Index;
    }

    return -1;
}

static ULONG
RtlpRiscv64GetDestination(
    _In_ ULONG Opcode,
    _In_ ULONG Register)
{
    if ((Opcode == RISCV64_UNW_OP_SET_CFA) ||
        (Opcode == RISCV64_UNW_OP_SET_CFA_LARGE))
    {
        return 0;
    }

    return 1 + (ULONG)RtlpRiscv64GetGprRuleIndex(Register);
}

static VOID
RtlpRiscv64InitializeState(
    _Out_ RTL_RISCV64_FRAME_STATE *State)
{
    ULONG Index;

    RtlZeroMemory(State, sizeof(*State));
    State->CfaRegister = 2;
    for (Index = 0; Index < RTL_NUMBER_OF(State->Gpr); Index++)
    {
        State->Gpr[Index].Kind = RtlRiscv64RuleSame;
        State->Gpr[Index].SourceRegister = RtlpRiscv64RecoverableGprs[Index];
    }
}

static NTSTATUS
RtlpRiscv64DecodeProgram(
    _In_ const RTL_RISCV64_UNWIND_VIEW *View,
    _In_ ULONG ProgramRva,
    _In_ USHORT SlotCount,
    _In_ ULONG CodeLimit,
    _In_ ULONG ApplyLimit,
    _In_ BOOLEAN Apply,
    _Inout_ RTL_RISCV64_FRAME_STATE *State)
{
    RISCV64_UNWIND_CODE_V1 Code, Extension;
    ULONG Slot, Opcode, Register, RawOperand, Destination;
    ULONG PreviousOffset = 0;
    ULONG64 DestinationsAtOffset = 0;
    LONG Operand, RuleIndex;
    BOOLEAN HavePrevious = FALSE;
    UCHAR UsedSlots;
    NTSTATUS Status;

    for (Slot = 0; Slot < SlotCount; Slot += UsedSlots)
    {
        Status = RtlpRiscv64ReadImage(View,
                                      ProgramRva + Slot * sizeof(Code),
                                      &Code,
                                      sizeof(Code));
        if (!NT_SUCCESS(Status))
            return Status;

        Opcode = Code.OpInfo & RISCV64_UNW_OP_MASK;
        Register = (Code.OpInfo & RISCV64_UNW_REG_MASK) >>
                   RISCV64_UNW_REG_SHIFT;
        RawOperand = (Code.OpInfo >> RISCV64_UNW_OPERAND_SHIFT) &
                     RTL_RISCV64_OPERAND_MASK;
        Operand = (LONG)RawOperand;
        if (RawOperand & (1UL << 20))
            Operand |= ~((LONG)RTL_RISCV64_OPERAND_MASK);

        UsedSlots = 1;
        if ((Opcode == RISCV64_UNW_OP_SET_CFA_LARGE) ||
            (Opcode == RISCV64_UNW_OP_SAVE_GPR_LARGE) ||
            (Opcode == RISCV64_UNW_OP_SAVE_FPR_LARGE) ||
            (Opcode == RISCV64_UNW_OP_SAVE_FCSR_LARGE))
        {
            if ((RawOperand != 0) || (Slot + 1 >= SlotCount))
                return STATUS_BAD_FUNCTION_TABLE;

            Status = RtlpRiscv64ReadImage(
                View,
                ProgramRva + (Slot + 1) * sizeof(Extension),
                &Extension,
                sizeof(Extension));
            if (!NT_SUCCESS(Status))
                return Status;
            if (Extension.OpInfo != 0)
                return STATUS_BAD_FUNCTION_TABLE;

            Operand = (LONG)Extension.CodeOffset;
            UsedSlots = 2;
        }

        if ((Code.CodeOffset & 1) || (Code.CodeOffset > CodeLimit) ||
            (HavePrevious && (Code.CodeOffset < PreviousOffset)))
        {
            return STATUS_BAD_FUNCTION_TABLE;
        }

        RuleIndex = RtlpRiscv64GetGprRuleIndex(Register);
        switch (Opcode)
        {
            case RISCV64_UNW_OP_SET_CFA:
            case RISCV64_UNW_OP_SET_CFA_LARGE:
                if (!RtlpRiscv64IsCfaRegister(Register) ||
                    (Operand == MINLONG))
                {
                    return STATUS_BAD_FUNCTION_TABLE;
                }
                break;

            case RISCV64_UNW_OP_SAVE_GPR:
            case RISCV64_UNW_OP_SAVE_GPR_LARGE:
                if ((RuleIndex < 0) || (Operand & 7))
                    return STATUS_BAD_FUNCTION_TABLE;
                break;

            case RISCV64_UNW_OP_SAME_GPR:
                if ((RuleIndex < 0) || (Operand != 0))
                    return STATUS_BAD_FUNCTION_TABLE;
                break;

            case RISCV64_UNW_OP_GPR_FROM_GPR:
                if ((RuleIndex < 0) || (RawOperand > 31) ||
                    (RawOperand == 3) || (RawOperand == 4))
                {
                    return STATUS_BAD_FUNCTION_TABLE;
                }
                Operand = (LONG)RawOperand;
                break;

            default:
                /* Version 1 NT images currently own integer state only. */
                return STATUS_BAD_FUNCTION_TABLE;
        }

        Destination = RtlpRiscv64GetDestination(Opcode, Register);
        if (Destination >= 64)
            return STATUS_BAD_FUNCTION_TABLE;
        if (!HavePrevious || (Code.CodeOffset != PreviousOffset))
            DestinationsAtOffset = 0;
        if (DestinationsAtOffset & (1ULL << Destination))
            return STATUS_BAD_FUNCTION_TABLE;
        DestinationsAtOffset |= 1ULL << Destination;

        if (Apply && (Code.CodeOffset <= ApplyLimit))
        {
            switch (Opcode)
            {
                case RISCV64_UNW_OP_SET_CFA:
                case RISCV64_UNW_OP_SET_CFA_LARGE:
                    State->CfaRegister = (UCHAR)Register;
                    State->CfaOffset = Operand;
                    break;

                case RISCV64_UNW_OP_SAVE_GPR:
                case RISCV64_UNW_OP_SAVE_GPR_LARGE:
                    State->Gpr[RuleIndex].Kind = RtlRiscv64RuleMemory;
                    State->Gpr[RuleIndex].Offset = Operand;
                    break;

                case RISCV64_UNW_OP_SAME_GPR:
                    State->Gpr[RuleIndex].Kind = RtlRiscv64RuleSame;
                    State->Gpr[RuleIndex].SourceRegister = (UCHAR)Register;
                    break;

                case RISCV64_UNW_OP_GPR_FROM_GPR:
                    State->Gpr[RuleIndex].Kind = RtlRiscv64RuleRegister;
                    State->Gpr[RuleIndex].SourceRegister = (UCHAR)Operand;
                    break;
            }
        }

        PreviousOffset = Code.CodeOffset;
        HavePrevious = TRUE;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
RtlpRiscv64ReadRecordLayout(
    _In_ const RTL_RISCV64_UNWIND_VIEW *View,
    _In_ const RUNTIME_FUNCTION *FunctionEntry,
    _Out_ RTL_RISCV64_RECORD_LAYOUT *Layout)
{
    ULONG64 Size, TailSize;
    NTSTATUS Status;

    if (((FunctionEntry->BeginAddress | FunctionEntry->EndAddress) & 1) ||
        (FunctionEntry->BeginAddress >= FunctionEntry->EndAddress) ||
        (FunctionEntry->EndAddress > View->ImageSize) ||
        (FunctionEntry->UnwindData & 3))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    Status = RtlpRiscv64ReadImage(View,
                                  FunctionEntry->UnwindData,
                                  &Layout->Header,
                                  sizeof(Layout->Header));
    if (!NT_SUCCESS(Status))
        return Status;

    if ((Layout->Header.Magic != RISCV64_UNWIND_MAGIC) ||
        (Layout->Header.Version != RISCV64_UNWIND_VERSION) ||
        (Layout->Header.HeaderSize != RISCV64_UNWIND_INFO_V1_SIZE) ||
        (Layout->Header.RecordSize < sizeof(Layout->Header)) ||
        (Layout->Header.RecordSize > RTL_RISCV64_MAX_RVUW_RECORD_SIZE) ||
        (Layout->Header.RecordSize & 3) ||
        (Layout->Header.Flags & ~RTL_RISCV64_UNWIND_FLAG_MASK) ||
        ((Layout->Header.Flags & RISCV64_UNW_FLAG_CHAININFO) &&
         (Layout->Header.Flags &
          (RISCV64_UNW_FLAG_EHANDLER | RISCV64_UNW_FLAG_UHANDLER))) ||
        ((Layout->Header.RequiredState & RISCV64_UNW_STATE_INTEGER) == 0) ||
        (Layout->Header.RequiredState & ~RTL_RISCV64_UNWIND_STATE_MASK) ||
        (Layout->Header.RequiredState &
         (RISCV64_UNW_STATE_FLOATING | RISCV64_UNW_STATE_VECTOR)) ||
        (Layout->Header.PrologEndOffset & 1) ||
        (Layout->Header.PrologEndOffset >
         FunctionEntry->EndAddress - FunctionEntry->BeginAddress) ||
        (Layout->Header.EpilogScopeCount > RTL_RISCV64_MAX_EPILOG_SCOPES))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    TailSize = 0;
    if (Layout->Header.Flags & RISCV64_UNW_FLAG_CHAININFO)
        TailSize = sizeof(RUNTIME_FUNCTION);
    else if (Layout->Header.Flags &
             (RISCV64_UNW_FLAG_EHANDLER | RISCV64_UNW_FLAG_UHANDLER))
        TailSize = sizeof(RISCV64_UNWIND_HANDLER_V1);

    Size = sizeof(Layout->Header);
    Size += (ULONG64)Layout->Header.PrologCodeSlots *
            sizeof(RISCV64_UNWIND_CODE_V1);
    Layout->PrologCodesRva = FunctionEntry->UnwindData + sizeof(Layout->Header);
    if ((Size > MAXULONG) ||
        (FunctionEntry->UnwindData > MAXULONG - (ULONG)Size))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    Layout->EpilogScopesRva = FunctionEntry->UnwindData + (ULONG)Size;
    Size += (ULONG64)Layout->Header.EpilogScopeCount *
            sizeof(RISCV64_UNWIND_EPILOG_SCOPE_V1);
    if ((Size > MAXULONG) ||
        (FunctionEntry->UnwindData > MAXULONG - (ULONG)Size))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    Layout->EpilogCodesRva = FunctionEntry->UnwindData + (ULONG)Size;
    Size += (ULONG64)Layout->Header.EpilogCodeSlots *
            sizeof(RISCV64_UNWIND_CODE_V1);
    if ((Size > MAXULONG) ||
        (FunctionEntry->UnwindData > MAXULONG - (ULONG)Size))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    Layout->TailRva = FunctionEntry->UnwindData + (ULONG)Size;
    Size += TailSize;
    Size = ALIGN_UP_BY(Size, 4);
    if ((Size != Layout->Header.RecordSize) ||
        ((ULONG64)FunctionEntry->UnwindData + Size > View->ImageSize))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    return STATUS_SUCCESS;
}

BOOLEAN
RtlpRiscv64SameFunctionEntry(
    _In_ const RUNTIME_FUNCTION *Left,
    _In_ const RUNTIME_FUNCTION *Right)
{
    return (Left->BeginAddress == Right->BeginAddress) &&
           (Left->EndAddress == Right->EndAddress) &&
           (Left->UnwindData == Right->UnwindData);
}

static NTSTATUS
RtlpRiscv64ApplyRecordInternal(
    _In_ ULONG HandlerType,
    _In_ ULONG ControlOffset,
    _In_ const RUNTIME_FUNCTION *FunctionEntry,
    _In_ PRUNTIME_FUNCTION FunctionEntryAddress,
    _In_ const RTL_RISCV64_UNWIND_VIEW *View,
    _Inout_ RTL_RISCV64_FRAME_STATE *State,
    _Out_ PRTL_RISCV64_UNWIND_RESULT Result,
    _Inout_updates_(RTL_RISCV64_MAX_CHAIN_DEPTH + 1)
        RUNTIME_FUNCTION *SeenEntries,
    _In_ ULONG Depth,
    _In_ BOOLEAN IsActualEntry,
    _In_ BOOLEAN HandlerActive)
{
    RTL_RISCV64_RECORD_LAYOUT Layout;
    RISCV64_UNWIND_EPILOG_SCOPE_V1 Scope, ActiveEpilogScope;
    RISCV64_UNWIND_HANDLER_V1 Handler;
    RUNTIME_FUNCTION ParentEntry;
    PRUNTIME_FUNCTION ParentEntryAddress;
    ULONG ApplyLimit, Index, PreviousEnd = 0, FunctionLength;
    LONG ActiveScope = -1;
    NTSTATUS Status;

    if (Depth > RTL_RISCV64_MAX_CHAIN_DEPTH)
        return STATUS_BAD_FUNCTION_TABLE;

    for (Index = 0; Index < Depth; Index++)
    {
        if (RtlpRiscv64SameFunctionEntry(FunctionEntry,
                                         &SeenEntries[Index]))
        {
            return STATUS_BAD_FUNCTION_TABLE;
        }
    }
    SeenEntries[Depth] = *FunctionEntry;

    Status = RtlpRiscv64ReadRecordLayout(View, FunctionEntry, &Layout);
    if (!NT_SUCCESS(Status))
        return Status;

    FunctionLength = FunctionEntry->EndAddress - FunctionEntry->BeginAddress;
    if (IsActualEntry && (ControlOffset >= FunctionLength))
        return STATUS_BAD_FUNCTION_TABLE;

    Status = RtlpRiscv64DecodeProgram(View,
                                      Layout.PrologCodesRva,
                                      Layout.Header.PrologCodeSlots,
                                      Layout.Header.PrologEndOffset,
                                      0,
                                      FALSE,
                                      State);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < Layout.Header.EpilogScopeCount; Index++)
    {
        Status = RtlpRiscv64ReadImage(
            View,
            Layout.EpilogScopesRva +
                Index * sizeof(RISCV64_UNWIND_EPILOG_SCOPE_V1),
            &Scope,
            sizeof(Scope));
        if (!NT_SUCCESS(Status))
            return Status;

        if (((Scope.StartOffset | Scope.EndOffset) & 1) ||
            (Scope.StartOffset >= Scope.EndOffset) ||
            (Scope.EndOffset > FunctionLength) ||
            (Index && (Scope.StartOffset < PreviousEnd)) ||
            (Scope.Reserved != 0) ||
            ((ULONG)Scope.FirstCodeSlot + Scope.CodeSlots >
             Layout.Header.EpilogCodeSlots))
        {
            return STATUS_BAD_FUNCTION_TABLE;
        }

        Status = RtlpRiscv64DecodeProgram(
            View,
            Layout.EpilogCodesRva +
                Scope.FirstCodeSlot * sizeof(RISCV64_UNWIND_CODE_V1),
            Scope.CodeSlots,
            Scope.EndOffset - Scope.StartOffset,
            0,
            FALSE,
            State);
        if (!NT_SUCCESS(Status))
            return Status;

        if (IsActualEntry &&
            (ControlOffset >= Scope.StartOffset) &&
            (ControlOffset < Scope.EndOffset))
        {
            ActiveScope = (LONG)Index;
            ActiveEpilogScope = Scope;
        }

        PreviousEnd = Scope.EndOffset;
    }

    if (IsActualEntry)
    {
        HandlerActive =
            (ControlOffset >= Layout.Header.PrologEndOffset) &&
            (ActiveScope < 0);
    }

    if (Layout.Header.Flags & RISCV64_UNW_FLAG_CHAININFO)
    {
        if ((Layout.Header.PrologEndOffset != 0) ||
            (Layout.Header.PrologCodeSlots != 0) ||
            (Layout.Header.EstablisherFrameOffset != 0))
        {
            return STATUS_BAD_FUNCTION_TABLE;
        }

        Status = RtlpRiscv64ReadImage(View,
                                      Layout.TailRva,
                                      &ParentEntry,
                                      sizeof(ParentEntry));
        if (!NT_SUCCESS(Status))
            return Status;

        ParentEntryAddress = (PRUNTIME_FUNCTION)(ULONG_PTR)
                             (View->ImageBase + Layout.TailRva);
        Status = RtlpRiscv64ApplyRecordInternal(
            HandlerType,
            0,
            &ParentEntry,
            ParentEntryAddress,
            View,
            State,
            Result,
            SeenEntries,
            Depth + 1,
            FALSE,
            HandlerActive);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    else
    {
        ApplyLimit = Layout.Header.PrologEndOffset;
        if (IsActualEntry &&
            (ControlOffset < Layout.Header.PrologEndOffset))
        {
            ApplyLimit = ControlOffset;
        }

        Status = RtlpRiscv64DecodeProgram(View,
                                          Layout.PrologCodesRva,
                                          Layout.Header.PrologCodeSlots,
                                          Layout.Header.PrologEndOffset,
                                          ApplyLimit,
                                          TRUE,
                                          State);
        if (!NT_SUCCESS(Status))
            return Status;

        if (Layout.Header.Flags &
            (RISCV64_UNW_FLAG_EHANDLER | RISCV64_UNW_FLAG_UHANDLER))
        {
            Status = RtlpRiscv64ReadImage(View,
                                          Layout.TailRva,
                                          &Handler,
                                          sizeof(Handler));
            if (!NT_SUCCESS(Status))
                return Status;

            if ((Handler.ExceptionHandlerRva == 0) ||
                (Handler.ExceptionHandlerRva & 1) ||
                (Handler.ExceptionHandlerRva >= View->ImageSize) ||
                (Handler.HandlerDataRva & 3) ||
                ((Handler.HandlerDataRva != 0) &&
                 (Handler.HandlerDataRva >= View->ImageSize)))
            {
                return STATUS_BAD_FUNCTION_TABLE;
            }

            if (HandlerActive &&
                (Layout.Header.Flags & HandlerType &
                 (RISCV64_UNW_FLAG_EHANDLER |
                  RISCV64_UNW_FLAG_UHANDLER)))
            {
                Result->LanguageHandler = (PEXCEPTION_ROUTINE)(ULONG_PTR)
                                          (View->ImageBase +
                                           Handler.ExceptionHandlerRva);
                if (Handler.HandlerDataRva != 0)
                {
                    Result->HandlerData = (PVOID)(ULONG_PTR)
                                          (View->ImageBase +
                                           Handler.HandlerDataRva);
                }
            }
        }

        Result->EstablisherFrameOffset =
            Layout.Header.EstablisherFrameOffset;
        Result->PrimaryFunctionEntry = FunctionEntryAddress;
    }

    if (IsActualEntry &&
        (ControlOffset >= Layout.Header.PrologEndOffset) &&
        (ActiveScope >= 0))
    {
        Status = RtlpRiscv64DecodeProgram(
            View,
            Layout.EpilogCodesRva +
                ActiveEpilogScope.FirstCodeSlot *
                sizeof(RISCV64_UNWIND_CODE_V1),
            ActiveEpilogScope.CodeSlots,
            ActiveEpilogScope.EndOffset - ActiveEpilogScope.StartOffset,
            ControlOffset - ActiveEpilogScope.StartOffset,
            TRUE,
            State);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpRiscv64ApplyRecord(
    _In_ ULONG HandlerType,
    _In_ ULONG ControlOffset,
    _In_ const RUNTIME_FUNCTION *FunctionEntry,
    _In_ PRUNTIME_FUNCTION FunctionEntryAddress,
    _In_ const RTL_RISCV64_UNWIND_VIEW *View,
    _Inout_ RTL_RISCV64_FRAME_STATE *State,
    _Out_ PRTL_RISCV64_UNWIND_RESULT Result)
{
    RUNTIME_FUNCTION SeenEntries[RTL_RISCV64_MAX_CHAIN_DEPTH + 1];

    RtlZeroMemory(SeenEntries, sizeof(SeenEntries));
    return RtlpRiscv64ApplyRecordInternal(HandlerType,
                                          ControlOffset,
                                          FunctionEntry,
                                          FunctionEntryAddress,
                                          View,
                                          State,
                                          Result,
                                          SeenEntries,
                                          0,
                                          TRUE,
                                          FALSE);
}

static VOID
RtlpRiscv64SetContextPointer(
    _Inout_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ ULONG Index,
    _In_ PULONG64 Pointer)
{
    PULONG64 *Pointers;

    Pointers = (PULONG64 *)ContextPointers;
    Pointers[Index] = Pointer;
}

NTSTATUS
NTAPI
RtlpRiscv64UnwindFrame(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _In_ const RTL_RISCV64_UNWIND_VIEW *View,
    _Inout_ PCONTEXT ContextRecord,
    _Out_ PRTL_RISCV64_UNWIND_RESULT Result,
    _Out_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    RTL_RISCV64_FRAME_STATE State;
    RTL_RISCV64_UNWIND_RESULT LocalResult;
    ULONG64 Cfa, Address, ControlRva, NewPc;
    ULONG64 Recovered[13], SavedAddress[13];
    UCHAR PointerRegister[13];
    ULONG Index, Register;
    NTSTATUS Status;

    if ((View == NULL) || (ContextRecord == NULL) || (Result == NULL) ||
        (View->StackLow > View->StackHigh) ||
        !RtlpRiscv64IsCanonicalAddress(View->StackLow) ||
        !RtlpRiscv64IsCanonicalAddress(View->StackHigh))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&LocalResult, sizeof(LocalResult));

    /* A valid CFA cannot repair an invalid interrupted stack pointer. This
     * also applies to FP-based recipes and partially executed prologues. */
    if (!RtlpRiscv64IsCanonicalAddress(ContextRecord->Sp) ||
        (ContextRecord->Sp & 15) || ContextRecord->Sp < View->StackLow ||
        ContextRecord->Sp > View->StackHigh)
        return STATUS_BAD_STACK;

    if (FunctionEntry == NULL)
    {
        Cfa = ContextRecord->Sp;
        if ((Cfa < View->StackLow) || (Cfa > View->StackHigh) ||
            (Cfa & 15) || !RtlpRiscv64IsCanonicalAddress(Cfa) ||
            !RtlpRiscv64IsControlPc(ContextRecord->Ra) ||
            (ContextRecord->Ra == ContextRecord->Pc))
        {
            return STATUS_BAD_STACK;
        }

        ContextRecord->Pc = ContextRecord->Ra;
        ContextRecord->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
        LocalResult.EstablisherFrame = Cfa;
        if (ContextPointers != NULL)
        {
            for (Index = 0;
                 Index < RTL_NUMBER_OF(RtlpRiscv64RecoverableGprs);
                 Index++)
            {
                RtlpRiscv64SetContextPointer(
                    ContextPointers,
                    Index,
                    &ContextRecord->X[RtlpRiscv64RecoverableGprs[Index]]);
            }
        }
        *Result = LocalResult;
        return STATUS_SUCCESS;
    }

    if ((ControlPc < View->ImageBase) ||
        (ControlPc - View->ImageBase > MAXULONG) ||
        !RtlpRiscv64IsControlPc(ControlPc) ||
        !RtlpRiscv64AddUnsigned(View->ImageBase,
                                View->ImageSize,
                                &Address) ||
        !RtlpRiscv64IsCanonicalAddress(View->ImageBase) ||
        !RtlpRiscv64IsCanonicalAddress(Address))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    ControlRva = ControlPc - View->ImageBase;
    if ((ControlRva < FunctionEntry->BeginAddress) ||
        (ControlRva >= FunctionEntry->EndAddress))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    RtlpRiscv64InitializeState(&State);
    Status = RtlpRiscv64ApplyRecord(
        HandlerType,
        (ULONG)(ControlRva - FunctionEntry->BeginAddress),
        FunctionEntry,
        FunctionEntry,
        View,
        &State,
        &LocalResult);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!RtlpRiscv64AddSigned(ContextRecord->X[State.CfaRegister],
                              State.CfaOffset,
                              &Cfa) ||
        !RtlpRiscv64IsCanonicalAddress(Cfa) ||
        (Cfa & 15) ||
        (Cfa < ContextRecord->Sp) ||
        (Cfa < View->StackLow) ||
        (Cfa > View->StackHigh))
    {
        return STATUS_BAD_STACK;
    }

    if (!RtlpRiscv64AddSigned(Cfa,
                              LocalResult.LanguageHandler != NULL ?
                                  LocalResult.EstablisherFrameOffset : 0,
                              &LocalResult.EstablisherFrame) ||
        (LocalResult.EstablisherFrame < View->StackLow) ||
        (LocalResult.EstablisherFrame > View->StackHigh))
    {
        return STATUS_BAD_STACK;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(State.Gpr); Index++)
    {
        Register = RtlpRiscv64RecoverableGprs[Index];
        SavedAddress[Index] = 0;
        PointerRegister[Index] = (UCHAR)Register;
        switch (State.Gpr[Index].Kind)
        {
            case RtlRiscv64RuleSame:
                Recovered[Index] = ContextRecord->X[Register];
                break;

            case RtlRiscv64RuleRegister:
                PointerRegister[Index] = State.Gpr[Index].SourceRegister;
                Recovered[Index] =
                    ContextRecord->X[State.Gpr[Index].SourceRegister];
                break;

            case RtlRiscv64RuleMemory:
                if (!RtlpRiscv64AddSigned(Cfa,
                                          State.Gpr[Index].Offset,
                                          &Address) ||
                    (Address & 7) ||
                    (Address < View->StackLow) ||
                    (Address > View->StackHigh) ||
                    (View->StackHigh - Address < sizeof(ULONG64)))
                {
                    return STATUS_BAD_STACK;
                }

                Status = RtlpRiscv64Read(View,
                                         Address,
                                         &Recovered[Index],
                                         sizeof(Recovered[Index]));
                if (!NT_SUCCESS(Status))
                    return STATUS_BAD_STACK;
                SavedAddress[Index] = Address;
                break;
        }
    }

    /* Ra is the first recoverable register. Validate the complete result
     * before committing any part of the caller context. */
    NewPc = Recovered[0];
    if (!RtlpRiscv64IsControlPc(NewPc) ||
        ((NewPc == ContextRecord->Pc) && (Cfa == ContextRecord->Sp)))
    {
        return STATUS_BAD_STACK;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(State.Gpr); Index++)
    {
        Register = RtlpRiscv64RecoverableGprs[Index];
        ContextRecord->X[Register] = Recovered[Index];
    }
    ContextRecord->Sp = Cfa;
    ContextRecord->Pc = NewPc;
    ContextRecord->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    if (ContextPointers != NULL)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(State.Gpr); Index++)
        {
            if (SavedAddress[Index] != 0)
            {
                RtlpRiscv64SetContextPointer(
                    ContextPointers,
                    Index,
                    (PULONG64)(ULONG_PTR)SavedAddress[Index]);
            }
            else
            {
                RtlpRiscv64SetContextPointer(
                    ContextPointers,
                    Index,
                    &ContextRecord->X[PointerRegister[Index]]);
            }
        }
    }

    *Result = LocalResult;

    return STATUS_SUCCESS;
}
