/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Transactional RISC-V64 RVUW version 1 frame decoder
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

#include "unwind.h"

#ifndef CONTEXT_UNWOUND_TO_CALL
#define CONTEXT_UNWOUND_TO_CALL 0x20000000UL
#endif

#define RTL_RISCV64_MAX_RVUW_RECORD_SIZE (1024UL * 1024UL)
#define RTL_RISCV64_MAX_EPILOG_SCOPES 4096
#define RTL_RISCV64_MAX_CHAIN_DEPTH 8
#define RTL_RISCV64_UNWIND_FLAG_MASK                                            \
    (RISCV64_UNW_FLAG_EHANDLER | RISCV64_UNW_FLAG_UHANDLER |                   \
     RISCV64_UNW_FLAG_CHAININFO)
#define RTL_RISCV64_UNWIND_STATE_MASK                                           \
    (RISCV64_UNW_STATE_INTEGER | RISCV64_UNW_STATE_FLOATING |                  \
     RISCV64_UNW_STATE_VECTOR)
#define RTL_RISCV64_OPERAND_MASK 0x001FFFFFUL

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

typedef struct _RTL_RISCV64_RECORD_LAYOUT
{
    RISCV64_UNWIND_INFO_V1 Header;
    ULONG PrologCodesRva;
    ULONG EpilogScopesRva;
    ULONG EpilogCodesRva;
    ULONG TailRva;
} RTL_RISCV64_RECORD_LAYOUT;

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

static BOOLEAN
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

static NTSTATUS
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

static NTSTATUS
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

static BOOLEAN
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

static NTSTATUS
NTAPI
RtlpRiscv64ReadLocalMemory(
    _In_opt_ PVOID ReadContext,
    _In_ ULONG64 Address,
    _Out_writes_bytes_all_(Size) PVOID Buffer,
    _In_ SIZE_T Size)
{
    UNREFERENCED_PARAMETER(ReadContext);
    return RtlpSafeCopyMemory(Buffer, (PVOID)(ULONG_PTR)Address, Size);
}

static PIMAGE_SECTION_HEADER
RtlpRiscv64FindImageSection(
    _In_ PIMAGE_NT_HEADERS NtHeaders,
    _In_ ULONG Rva,
    _In_ ULONG Size)
{
    PIMAGE_SECTION_HEADER Section;
    ULONG Index, SectionSize;
    ULONG64 RangeEnd, SectionEnd;

    RangeEnd = (ULONG64)Rva + Size;
    if ((RangeEnd > NtHeaders->OptionalHeader.SizeOfImage) ||
        (RangeEnd < Rva))
    {
        return NULL;
    }

    Section = IMAGE_FIRST_SECTION(NtHeaders);
    for (Index = 0; Index < NtHeaders->FileHeader.NumberOfSections;
         Index++, Section++)
    {
        SectionSize = max(Section->Misc.VirtualSize, Section->SizeOfRawData);
        SectionEnd = (ULONG64)Section->VirtualAddress + SectionSize;
        if ((Rva >= Section->VirtualAddress) &&
            (RangeEnd <= SectionEnd))
        {
            return Section;
        }
    }

    return NULL;
}

static BOOLEAN
RtlpRiscv64ValidateImageRange(
    _In_ PIMAGE_NT_HEADERS NtHeaders,
    _In_ ULONG Rva,
    _In_ ULONG Size,
    _In_ ULONG RequiredCharacteristics,
    _In_ ULONG ForbiddenCharacteristics)
{
    PIMAGE_SECTION_HEADER Section;

    Section = RtlpRiscv64FindImageSection(NtHeaders, Rva, Size);
    if (Section == NULL)
        return FALSE;

    return ((Section->Characteristics & RequiredCharacteristics) ==
            RequiredCharacteristics) &&
           ((Section->Characteristics & ForbiddenCharacteristics) == 0);
}

NTSTATUS NTAPI
RtlpRiscv64PrimaryEntry(ULONG64 ImageBase, PRUNTIME_FUNCTION *Entry)
{
    RUNTIME_FUNCTION Function;
    RISCV64_UNWIND_INFO_V1 Header;
    ULONG Depth;
    /* Called only after RtlVirtualUnwind2 has validated the complete chain.
     * Recheck every read, offset and depth before exposing the primary entry. */
    for (Depth = 0; *Entry && Depth <= RTL_RISCV64_MAX_CHAIN_DEPTH; Depth++) {
        if (!NT_SUCCESS(RtlpSafeCopyMemory(&Function, *Entry, sizeof(Function))) ||
            !NT_SUCCESS(RtlpSafeCopyMemory(&Header,
                (PVOID)(ImageBase + Function.UnwindData), sizeof(Header))) ||
            Header.Magic != RISCV64_UNWIND_MAGIC ||
            Header.Version != RISCV64_UNWIND_VERSION ||
            Header.RecordSize < sizeof(Header) ||
            Header.RecordSize > RTL_RISCV64_MAX_RVUW_RECORD_SIZE)
            return STATUS_BAD_FUNCTION_TABLE;
        if (!(Header.Flags & RISCV64_UNW_FLAG_CHAININFO)) return STATUS_SUCCESS;
        if (Header.RecordSize < sizeof(Header) + sizeof(Function))
            return STATUS_BAD_FUNCTION_TABLE;
        *Entry = (PVOID)(ImageBase + Function.UnwindData + Header.RecordSize - sizeof(Function));
    }
    return *Entry ? STATUS_BAD_FUNCTION_TABLE : STATUS_SUCCESS;
}

NTSTATUS NTAPI
RtlpRiscv64ReadScope(PDISPATCHER_CONTEXT Dispatcher, ULONG Index,
                    PULONG Count, RISCV64_SCOPE_RECORD *Scope)
{
    PIMAGE_NT_HEADERS Nt;
    ULONG64 Rva;
    ULONG Size;
    NTSTATUS Status;

    Nt = RtlImageNtHeader((PVOID)Dispatcher->ImageBase);
    Rva = (ULONG64)Dispatcher->HandlerData - Dispatcher->ImageBase;
    if (!Nt || Rva > MAXULONG || (Rva & 3) ||
        !RtlpRiscv64ValidateImageRange(Nt, (ULONG)Rva, sizeof(ULONG),
            IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))
        return STATUS_BAD_FUNCTION_TABLE;
    Status = RtlpSafeCopyMemory(Count, Dispatcher->HandlerData, sizeof(*Count));
    if (!NT_SUCCESS(Status) || *Count > 256) return STATUS_BAD_FUNCTION_TABLE;
    Size = sizeof(ULONG) + *Count * sizeof(*Scope);
    if (!RtlpRiscv64ValidateImageRange(Nt, (ULONG)Rva, Size,
            IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))
        return STATUS_BAD_FUNCTION_TABLE;
    if (!Scope) return STATUS_SUCCESS;
    if (Index >= *Count) return STATUS_BAD_FUNCTION_TABLE;
    Status = RtlpSafeCopyMemory(Scope,
        (PUCHAR)Dispatcher->HandlerData + sizeof(ULONG) + Index * sizeof(*Scope),
        sizeof(*Scope));
    if (!NT_SUCCESS(Status) || Scope->BeginAddress >= Scope->EndAddress ||
        ((Scope->BeginAddress | Scope->EndAddress | Scope->JumpTarget) & 1) ||
        !RtlpRiscv64ValidateImageRange(Nt, Scope->BeginAddress,
            Scope->EndAddress - Scope->BeginAddress,
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_WRITE) ||
        (Scope->HandlerAddress != 1 &&
         ((Scope->HandlerAddress & 1) || !Scope->HandlerAddress ||
          !RtlpRiscv64ValidateImageRange(Nt, Scope->HandlerAddress, 2,
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_WRITE))) ||
        (!Scope->JumpTarget && Scope->HandlerAddress == 1) ||
        (Scope->JumpTarget && !RtlpRiscv64ValidateImageRange(Nt,
            Scope->JumpTarget, 2, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE,
            IMAGE_SCN_MEM_WRITE)))
        return STATUS_BAD_FUNCTION_TABLE;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpRiscv64ValidateRecordImageRanges(
    _In_ const RTL_RISCV64_UNWIND_VIEW *View,
    _In_ PIMAGE_NT_HEADERS NtHeaders,
    _In_ const RUNTIME_FUNCTION *FunctionEntry,
    _Inout_updates_(RTL_RISCV64_MAX_CHAIN_DEPTH + 1)
        RUNTIME_FUNCTION *SeenEntries,
    _In_ ULONG Depth)
{
    RTL_RISCV64_RECORD_LAYOUT Layout;
    RISCV64_UNWIND_HANDLER_V1 Handler;
    RUNTIME_FUNCTION ParentEntry;
    ULONG Index;
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

    if (!RtlpRiscv64ValidateImageRange(
            NtHeaders,
            FunctionEntry->BeginAddress,
            FunctionEntry->EndAddress - FunctionEntry->BeginAddress,
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE,
            IMAGE_SCN_MEM_WRITE) ||
        !RtlpRiscv64ValidateImageRange(
            NtHeaders,
            FunctionEntry->UnwindData,
            Layout.Header.RecordSize,
            IMAGE_SCN_MEM_READ,
            IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    if (Layout.Header.Flags & RISCV64_UNW_FLAG_CHAININFO)
    {
        Status = RtlpRiscv64ReadImage(View,
                                      Layout.TailRva,
                                      &ParentEntry,
                                      sizeof(ParentEntry));
        if (!NT_SUCCESS(Status))
            return Status;

        return RtlpRiscv64ValidateRecordImageRanges(View,
                                                     NtHeaders,
                                                     &ParentEntry,
                                                     SeenEntries,
                                                     Depth + 1);
    }

    if (Layout.Header.Flags &
        (RISCV64_UNW_FLAG_EHANDLER | RISCV64_UNW_FLAG_UHANDLER))
    {
        Status = RtlpRiscv64ReadImage(View,
                                      Layout.TailRva,
                                      &Handler,
                                      sizeof(Handler));
        if (!NT_SUCCESS(Status))
            return Status;

        if (!RtlpRiscv64ValidateImageRange(
                NtHeaders,
                Handler.ExceptionHandlerRva,
                1,
                IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE,
                IMAGE_SCN_MEM_WRITE) ||
            ((Handler.HandlerDataRva != 0) &&
             !RtlpRiscv64ValidateImageRange(
                 NtHeaders,
                 Handler.HandlerDataRva,
                 sizeof(ULONG),
                 IMAGE_SCN_MEM_READ,
                 IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE)))
        {
            return STATUS_BAD_FUNCTION_TABLE;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpRiscv64GetStaticFunctionTable(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Out_ PRUNTIME_FUNCTION *FunctionTable,
    _Out_ PULONG EntryCount,
    _Out_opt_ PIMAGE_NT_HEADERS *ReturnedNtHeaders)
{
    PIMAGE_DATA_DIRECTORY Directory;
    PIMAGE_NT_HEADERS NtHeaders;
    PVOID BaseAddress;
    ULONG64 ImageEnd;

    *ImageBase = 0;
    *FunctionTable = NULL;
    *EntryCount = 0;
    if (ReturnedNtHeaders != NULL)
        *ReturnedNtHeaders = NULL;

    if (!RtlPcToFileHeader((PVOID)(ULONG_PTR)ControlPc, &BaseAddress))
        return STATUS_NOT_FOUND;

    NtHeaders = RtlImageNtHeader(BaseAddress);
    if ((NtHeaders == NULL) ||
        (NtHeaders->OptionalHeader.NumberOfRvaAndSizes <=
         IMAGE_DIRECTORY_ENTRY_EXCEPTION) ||
        !RtlpRiscv64AddUnsigned((ULONG64)(ULONG_PTR)BaseAddress,
                                NtHeaders->OptionalHeader.SizeOfImage,
                                &ImageEnd) ||
        (ControlPc < (ULONG64)(ULONG_PTR)BaseAddress) ||
        (ControlPc >= ImageEnd))
    {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    *ImageBase = (ULONG64)(ULONG_PTR)BaseAddress;
    if (ReturnedNtHeaders != NULL)
        *ReturnedNtHeaders = NtHeaders;

    Directory = &NtHeaders->OptionalHeader.DataDirectory
                    [IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if ((Directory->VirtualAddress == 0) && (Directory->Size == 0))
        return STATUS_NOT_FOUND;
    if ((Directory->VirtualAddress == 0) ||
        (Directory->Size == 0) ||
        (Directory->Size % sizeof(RUNTIME_FUNCTION)) ||
        !RtlpRiscv64ValidateImageRange(
            NtHeaders,
            Directory->VirtualAddress,
            Directory->Size,
            IMAGE_SCN_MEM_READ,
            IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    *FunctionTable = (PRUNTIME_FUNCTION)(ULONG_PTR)
                     (*ImageBase + Directory->VirtualAddress);
    *EntryCount = Directory->Size / sizeof(RUNTIME_FUNCTION);
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpRiscv64ValidateFunctionTable(
    _In_ ULONG64 ImageBase,
    _In_ PIMAGE_NT_HEADERS NtHeaders,
    _In_reads_(EntryCount) PRUNTIME_FUNCTION FunctionTable,
    _In_ ULONG EntryCount)
{
    RUNTIME_FUNCTION Entry;
    ULONG Index, PreviousEnd = 0;
    NTSTATUS Status;

    for (Index = 0; Index < EntryCount; Index++)
    {
        Status = RtlpSafeCopyMemory(&Entry,
                                    &FunctionTable[Index],
                                    sizeof(Entry));
        if (!NT_SUCCESS(Status))
            return STATUS_BAD_FUNCTION_TABLE;

        if (((Entry.BeginAddress | Entry.EndAddress) & 1) ||
            (Entry.BeginAddress >= Entry.EndAddress) ||
            (Entry.EndAddress > NtHeaders->OptionalHeader.SizeOfImage) ||
            (Index && (Entry.BeginAddress < PreviousEnd)) ||
            (Entry.UnwindData & 3) ||
            !RtlpRiscv64ValidateImageRange(
                NtHeaders,
                Entry.BeginAddress,
                Entry.EndAddress - Entry.BeginAddress,
                IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE,
                IMAGE_SCN_MEM_WRITE) ||
            !RtlpRiscv64ValidateImageRange(
                NtHeaders,
                Entry.UnwindData,
                sizeof(RISCV64_UNWIND_INFO_V1),
                IMAGE_SCN_MEM_READ,
                IMAGE_SCN_MEM_WRITE))
        {
            UNREFERENCED_PARAMETER(ImageBase);
            return STATUS_BAD_FUNCTION_TABLE;
        }

        PreviousEnd = Entry.EndAddress;
    }

    return STATUS_SUCCESS;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Out_ PULONG Length)
{
    PRUNTIME_FUNCTION FunctionTable;
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG64 Base;
    ULONG EntryCount;
    NTSTATUS Status;

    *ImageBase = 0;
    *Length = 0;
    Status = RtlpRiscv64GetStaticFunctionTable(ControlPc,
                                                &Base,
                                                &FunctionTable,
                                                &EntryCount,
                                                &NtHeaders);
    if (!NT_SUCCESS(Status))
        return NULL;

    Status = RtlpRiscv64ValidateFunctionTable(Base,
                                              NtHeaders,
                                              FunctionTable,
                                              EntryCount);
    if (!NT_SUCCESS(Status))
        return NULL;

    *ImageBase = (ULONG_PTR)Base;
    *Length = EntryCount * sizeof(RUNTIME_FUNCTION);
    return FunctionTable;
}

NTSTATUS
NTAPI
RtlpRiscv64LookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Out_ PRUNTIME_FUNCTION *FunctionEntry)
{
    PRUNTIME_FUNCTION FunctionTable;
    RUNTIME_FUNCTION Entry;
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG64 ControlRva;
    ULONG EntryCount, Low, High, Middle;
    NTSTATUS Status;

    *FunctionEntry = NULL;
    *ImageBase = 0;
    Status = RtlpRiscv64GetStaticFunctionTable(ControlPc,
                                                ImageBase,
                                                &FunctionTable,
                                                &EntryCount,
                                                &NtHeaders);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlpRiscv64ValidateFunctionTable(*ImageBase,
                                              NtHeaders,
                                              FunctionTable,
                                              EntryCount);
    if (!NT_SUCCESS(Status))
    {
        *ImageBase = 0;
        return Status;
    }

    ControlRva = ControlPc - *ImageBase;
    Low = 0;
    High = EntryCount;
    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        Status = RtlpSafeCopyMemory(&Entry,
                                    &FunctionTable[Middle],
                                    sizeof(Entry));
        if (!NT_SUCCESS(Status))
        {
            *ImageBase = 0;
            return STATUS_BAD_FUNCTION_TABLE;
        }

        if (ControlRva < Entry.BeginAddress)
        {
            High = Middle;
        }
        else if (ControlRva >= Entry.EndAddress)
        {
            Low = Middle + 1;
        }
        else
        {
            *FunctionEntry = &FunctionTable[Middle];
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable)
{
    PRUNTIME_FUNCTION Entry;
    UNREFERENCED_PARAMETER(HistoryTable);
    if (!NT_SUCCESS(RtlpRiscv64LookupFunctionEntry(ControlPc, ImageBase, &Entry)))
        return NULL;
    return Entry;
}

NTSTATUS
NTAPI
RtlVirtualUnwind2(
    _In_ ULONG HandlerType,
    _In_ ULONG_PTR ImageBase,
    _In_ ULONG_PTR ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_opt_ PBOOLEAN MachineFrameUnwound,
    _Out_opt_ PVOID *HandlerData,
    _Out_opt_ PULONG_PTR EstablisherFrame,
    _Out_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _Out_opt_ PULONG_PTR LowLimit,
    _Out_opt_ PULONG_PTR HighLimit,
    _Out_opt_ PEXCEPTION_ROUTINE *HandlerRoutine,
    _In_ ULONG UnwindFlags)
{
    RTL_RISCV64_UNWIND_RESULT Result;
    RTL_RISCV64_UNWIND_VIEW View;
    RUNTIME_FUNCTION LocalFunctionEntry;
    RUNTIME_FUNCTION SeenEntries[RTL_RISCV64_MAX_CHAIN_DEPTH + 1];
    PIMAGE_NT_HEADERS NtHeaders;
    PRUNTIME_FUNCTION DecodedFunctionEntry;
    ULONG_PTR StackLow, StackHigh;
    NTSTATUS Status;

    if ((ContextRecord == NULL) || (UnwindFlags != 0))
        return STATUS_INVALID_PARAMETER;

    RtlpGetStackLimits(&StackLow, &StackHigh);
    Status = RtlpRiscv64UnwindUserException(ControlPc, StackLow, StackHigh, ContextRecord);
    if (Status != STATUS_NOT_FOUND) {
        if (!NT_SUCCESS(Status)) return Status;
        if (MachineFrameUnwound) *MachineFrameUnwound = TRUE;
        if (HandlerData) *HandlerData = NULL;
        if (HandlerRoutine) *HandlerRoutine = NULL;
        if (EstablisherFrame) *EstablisherFrame = ContextRecord->Sp;
        if (ContextPointers) RtlZeroMemory(ContextPointers, sizeof(*ContextPointers));
        if (LowLimit) *LowLimit = StackLow;
        if (HighLimit) *HighLimit = StackHigh;
        return STATUS_SUCCESS;
    }
    RtlZeroMemory(&View, sizeof(View));
    View.ImageBase = ImageBase;
    View.StackLow = StackLow;
    View.StackHigh = StackHigh;
    View.ReadMemory = RtlpRiscv64ReadLocalMemory;
    DecodedFunctionEntry = FunctionEntry;

    if (FunctionEntry != NULL)
    {
        Status = RtlpSafeCopyMemory(&LocalFunctionEntry,
                                    FunctionEntry,
                                    sizeof(LocalFunctionEntry));
        if (!NT_SUCCESS(Status))
            return STATUS_BAD_FUNCTION_TABLE;

        NtHeaders = RtlImageNtHeader((PVOID)ImageBase);
        if (NtHeaders == NULL)
            return STATUS_INVALID_IMAGE_FORMAT;
        View.ImageSize = NtHeaders->OptionalHeader.SizeOfImage;

        RtlZeroMemory(SeenEntries, sizeof(SeenEntries));
        Status = RtlpRiscv64ValidateRecordImageRanges(
            &View,
            NtHeaders,
            &LocalFunctionEntry,
            SeenEntries,
            0);
        if (!NT_SUCCESS(Status))
            return Status;

        DecodedFunctionEntry = &LocalFunctionEntry;
    }

    Status = RtlpRiscv64UnwindFrame(HandlerType,
                                     ControlPc,
                                     DecodedFunctionEntry,
                                     &View,
                                     ContextRecord,
                                     &Result,
                                     ContextPointers);
    if (!NT_SUCCESS(Status))
        return Status;

    if (MachineFrameUnwound != NULL)
        *MachineFrameUnwound = FALSE;
    if (HandlerData != NULL)
        *HandlerData = Result.HandlerData;
    if (EstablisherFrame != NULL)
        *EstablisherFrame = (ULONG_PTR)Result.EstablisherFrame;
    if (LowLimit != NULL)
        *LowLimit = StackLow;
    if (HighLimit != NULL)
        *HighLimit = StackHigh;
    if (HandlerRoutine != NULL)
        *HandlerRoutine = Result.LanguageHandler;

    return STATUS_SUCCESS;
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_opt_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Out_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    PEXCEPTION_ROUTINE Handler;
    NTSTATUS Status;

    Handler = NULL;
    Status = RtlVirtualUnwind2(HandlerType,
                               (ULONG_PTR)ImageBase,
                               (ULONG_PTR)ControlPc,
                               FunctionEntry,
                               ContextRecord,
                               NULL,
                               HandlerData,
                               (PULONG_PTR)EstablisherFrame,
                               ContextPointers,
                               NULL,
                               NULL,
                               &Handler,
                               0);
    if (!NT_SUCCESS(Status))
        __fastfail(FAST_FAIL_FATAL_APP_EXIT);

    return Handler;
}
