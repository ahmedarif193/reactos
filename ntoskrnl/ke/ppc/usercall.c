/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC user-mode transitions: APCs, callbacks,
 *              thread startup and the return to user mode
 *
 * The ntdll dispatchers are exported as function descriptors {code, TOC}.
 * Entering one sets the trap frame's Iar and r2 from its descriptor.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

NTSTATUS NTAPI KiPpcCallUserMode(PKTRAP_FRAME Frame, PVOID *OutputBuffer, PULONG OutputLength);
DECLSPEC_NORETURN VOID NTAPI KiPpcCallbackReturn(PKCALLOUT_FRAME CalloutFrame, NTSTATUS Status);

/* KiUserApcDispatcher finds the CONTEXT above a 32-byte frame header. */
#define KI_PPC_USER_FRAME_HEADER 32

static
BOOLEAN
KiPpcUserControlAddress(_In_ ULONG_PTR Address)
{
    return (Address >= MM_ALLOCATION_GRANULARITY) && (Address < (ULONG_PTR)MmUserProbeAddress);
}

NTSTATUS
NTAPI
KiPpcSetUserEntry(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ PVOID Descriptor)
{
    ULONG Entry[2];
    NTSTATUS Status;

    if (!KiPpcUserControlAddress((ULONG_PTR)Descriptor) || ((ULONG_PTR)Descriptor & 3))
        return STATUS_ACCESS_VIOLATION;
    Status = KiPpcCopyFromUser(Entry, Descriptor, sizeof(Entry));
    if (!NT_SUCCESS(Status))
        return Status;
    if (!KiPpcUserControlAddress(Entry[0]) || (Entry[0] & 3))
        return STATUS_ACCESS_VIOLATION;
    TrapFrame->Context.Iar = Entry[0];
    TrapFrame->Context.Gpr2 = Entry[1];
    return STATUS_SUCCESS;
}

static
VOID
KiPpcUserTransitionFailure(_Inout_ PKTRAP_FRAME Frame, _In_ NTSTATUS Status)
{
    EXCEPTION_RECORD Record = {0};

    Record.ExceptionCode = Status;
    Record.ExceptionAddress = (PVOID)Frame->Context.Iar;
    KiDispatchException(&Record, NULL, Frame, UserMode, TRUE);
}

NTSTATUS
NTAPI
KeRaiseUserException(_In_ NTSTATUS ExceptionCode)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME Frame = Thread->TrapFrame;
    NTSTATUS Status;

    if (!Thread->Teb || !Frame || !KiUserTrap(Frame) || !KeRaiseUserExceptionDispatcher)
        return STATUS_UNSUCCESSFUL;

    Status = KiPpcCopyToUser(&Thread->Teb->ExceptionCode, &ExceptionCode, sizeof(ExceptionCode));
    if (!NT_SUCCESS(Status))
        return Status;

    /* The stub's LR still returns to the system service's caller. */
    Status = KiPpcSetUserEntry(Frame, KeRaiseUserExceptionDispatcher);
    if (!NT_SUCCESS(Status))
        return Status;
    return ExceptionCode;
}

VOID
NTAPI
KiInitializeUserApc(
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME Frame,
    _In_ PKNORMAL_ROUTINE NormalRoutine,
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    CONTEXT Context = {0};
    ULONG_PTR Stack;
    NTSTATUS Status;

    ASSERT(KiUserTrap(Frame));
    if (!KiPpcUserControlAddress(Frame->Context.Gpr1) || (Frame->Context.Gpr1 < MM_ALLOCATION_GRANULARITY + sizeof(Context) + KI_PPC_USER_FRAME_HEADER) || !KiPpcUserControlAddress((ULONG_PTR)NormalRoutine) || ((ULONG_PTR)NormalRoutine & 3))
    {
        KiPpcUserTransitionFailure(Frame, STATUS_ACCESS_VIOLATION);
        return;
    }

    Stack = ALIGN_DOWN_BY(Frame->Context.Gpr1 - sizeof(Context) - KI_PPC_USER_FRAME_HEADER, 16);
    Context.ContextFlags = CONTEXT_FULL;
    KeTrapFrameToContext(Frame, ExceptionFrame, &Context);
    Status = KiPpcCopyToUser((PVOID)(Stack + KI_PPC_USER_FRAME_HEADER), &Context, sizeof(Context));
    if (NT_SUCCESS(Status))
        Status = KiPpcSetUserEntry(Frame, KeUserApcDispatcher);
    if (!NT_SUCCESS(Status))
    {
        KiPpcUserTransitionFailure(Frame, Status);
        return;
    }

    /* KiUserApcDispatcher calls NormalRoutine(NormalContext, SystemArgument1,
     * SystemArgument2, Context) through the descriptor in r6. */
    Frame->Context.Gpr3 = (ULONG_PTR)NormalContext;
    Frame->Context.Gpr4 = (ULONG_PTR)SystemArgument1;
    Frame->Context.Gpr5 = (ULONG_PTR)SystemArgument2;
    Frame->Context.Gpr6 = (ULONG_PTR)NormalRoutine;
    Frame->Context.Gpr1 = Stack;
    Frame->Context.Lr = 0;
    Frame->Context.Msr = PPC_USER_MSR;
}

/* Fault in a user page and verify it is present with the needed access.
 * Hardware still enforces the permissions afterwards. */
static
NTSTATUS
KiPpcCheckUserPage(_In_ PVOID Address, _In_ BOOLEAN Write, _Inout_ PKTRAP_FRAME Frame)
{
    ULONG Needed = (ULONG)(MI_PPC_PTE_OWNER | (Write ? MI_PPC_PTE_WRITE : 0));
    ULONG Fault = Write ? MI_PPC_FAULT_WRITE : 0;
    MI_PPC_PAGE_WALK Walk;
    NTSTATUS Status;

    Status = MiPpcWalkCurrentPageTables(Address, &Walk);
    if (NT_SUCCESS(Status))
    {
        if (((ULONG)Walk.Value.u.Long & Needed) == Needed)
            return STATUS_SUCCESS;
        Fault |= MI_PPC_FAULT_PRESENT;
    }
    Status = MmAccessFault(Fault, Address, UserMode, Frame);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Status == STATUS_PAGE_FAULT_GUARD_PAGE)
    {
        Status = MmAccessFault(Fault, Address, UserMode, Frame);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = MiPpcWalkCurrentPageTables(Address, &Walk);
    if (!NT_SUCCESS(Status) || (((ULONG)Walk.Value.u.Long & Needed) != Needed))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

DECLSPEC_NORETURN
VOID
NTAPI
KiPpcReturnToUser(_Inout_ PKTRAP_FRAME Frame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(KiUserTrap(Frame));
Retry:
    for (;;)
    {
        _disable();
        if (!Thread->ApcState.UserApcPending)
            break;
        KfRaiseIrql(APC_LEVEL);
        _enable();
        KiDeliverApc(UserMode, NULL, Frame);
        KfLowerIrql(PASSIVE_LEVEL);
    }
    _enable();

    if (!KiPpcUserControlAddress(Frame->Context.Iar) || (Frame->Context.Iar & 3) || !KiPpcUserControlAddress(Frame->Context.Gpr1))
        KiPpcUserTransitionFailure(Frame, STATUS_ACCESS_VIOLATION);
    Status = KiPpcCheckUserPage((PVOID)Frame->Context.Iar, FALSE, Frame);
    if (NT_SUCCESS(Status))
        Status = KiPpcCheckUserPage((PVOID)(Frame->Context.Gpr1 - 1), TRUE, Frame);
    if (NT_SUCCESS(Status))
        Status = KiPpcCheckUserPage(Thread->Teb, TRUE, Frame);
    if (!NT_SUCCESS(Status))
        KiPpcUserTransitionFailure(Frame, Status);

    _disable();
    if (Thread->ApcState.UserApcPending)
        goto Retry;
    Frame->Context.Msr = PPC_USER_MSR | (Frame->Context.Msr & PPC_USER_MSR_MASK);
    Frame->Context.Gpr13 = (ULONG_PTR)Thread->Teb;
    Thread->PreviousMode = UserMode;
    Thread->TrapFrame = Frame->PreviousTrapFrame;

    /* Traps from user mode build their frame below the current initial
     * stack, which a callback moves. */
    KeGetPcr()->InitialStack = (ULONG_PTR)Thread->InitialStack;
    KiPpcRestoreTrapFrame(Frame);
}

DECLSPEC_NORETURN
VOID
NTAPI
KiPpcStartUserThread(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    if (!Thread->Teb || !Thread->TrapFrame || (Thread->TrapFrame != ((PKTRAP_FRAME)Thread->InitialStack) - 1))
        KiPpcUnimplemented("KiPpcStartUserThread/invalid-frame");
    KiPpcReturnToUser(Thread->TrapFrame);
}

/* NtRaiseException and NtContinue leave through the system service's user
 * frame. Kernel-mode continuation resumes through RtlRestoreContext. */
DECLSPEC_NORETURN
VOID
KiExceptionExit(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    UNREFERENCED_PARAMETER(ExceptionFrame);
    KiPpcReturnToUser(TrapFrame);
}

NTSTATUS
NTAPI
KiPpcUserModeCallout(_Inout_ PKTRAP_FRAME Frame, _Out_ PKCALLOUT_FRAME CalloutFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR InitialStack = (ULONG_PTR)CalloutFrame;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(!(InitialStack & 15));

    if (InitialStack - Thread->StackLimit < KERNEL_STACK_SIZE)
    {
        if (!Thread->LargeStack)
            return STATUS_STACK_OVERFLOW;
        Status = MmGrowKernelStack(CalloutFrame);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    CalloutFrame->CallbackStack = Thread->CallbackStack;
    CalloutFrame->InitialStack = Thread->InitialStack;
    CalloutFrame->TrapFrame = Thread->TrapFrame;
    Frame->PreviousTrapFrame = Thread->TrapFrame;

    _disable();
    Thread->CallbackStack = CalloutFrame;
    Thread->InitialStack = CalloutFrame;
    Thread->TrapFrame = Frame;
    KiPpcReturnToUser(Frame);
}

NTSTATUS
NTAPI
KeUserModeCallback(
    _In_ ULONG RoutineIndex,
    _In_ PVOID Argument,
    _In_ ULONG ArgumentLength,
    _Out_ PVOID *Result,
    _Out_ PULONG ResultLength)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME OuterFrame = Thread->TrapFrame;
    KTRAP_FRAME CallbackFrame;
    UCALLOUT_FRAME UserFrame;
    ULONG_PTR OldStack, UserArguments, UserStack;
    ULONG GdiBatchCount;
    NTSTATUS Status, CallbackStatus;

    if ((KeGetCurrentIrql() != PASSIVE_LEVEL) || (Thread->PreviousMode != UserMode) || Thread->ApcState.KernelApcInProgress || (Thread->ApcStateIndex != OriginalApcEnvironment) || Thread->CombinedApcDisable || !OuterFrame || !KiUserTrap(OuterFrame))
        return STATUS_INVALID_DEVICE_STATE;
    if (!Result || !ResultLength || (ArgumentLength && !Argument))
        return STATUS_INVALID_PARAMETER;

    OldStack = OuterFrame->Context.Gpr1;
    if (!KiPpcUserControlAddress(OldStack) || (OldStack < MM_ALLOCATION_GRANULARITY + (ULONG_PTR)ArgumentLength + sizeof(UserFrame)))
        return STATUS_ACCESS_VIOLATION;
    UserArguments = ALIGN_DOWN_BY(OldStack - ArgumentLength, 16);
    UserStack = UserArguments - sizeof(UserFrame);

    Status = KiPpcCopyToUser((PVOID)UserArguments, Argument, ArgumentLength);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlZeroMemory(&UserFrame, sizeof(UserFrame));
    UserFrame.Buffer = (PVOID)UserArguments;
    UserFrame.Length = ArgumentLength;
    UserFrame.ApiNumber = RoutineIndex;
    UserFrame.Lr = OuterFrame->Context.Lr;
    UserFrame.Sp = OldStack;
    Status = KiPpcCopyToUser((PVOID)UserStack, &UserFrame, sizeof(UserFrame));
    if (!NT_SUCCESS(Status))
        return Status;

    /* KiUserCallbackDispatcher(ApiNumber, Buffer, Length) on the new stack. */
    CallbackFrame = *OuterFrame;
    Status = KiPpcSetUserEntry(&CallbackFrame, KeUserCallbackDispatcher);
    if (!NT_SUCCESS(Status))
        return STATUS_PROCEDURE_NOT_FOUND;
    CallbackFrame.Context.Gpr1 = UserStack;
    CallbackFrame.Context.Lr = 0;
    CallbackFrame.Context.Gpr13 = (ULONG_PTR)Thread->Teb;
    CallbackFrame.Context.Gpr3 = RoutineIndex;
    CallbackFrame.Context.Gpr4 = UserArguments;
    CallbackFrame.Context.Gpr5 = ArgumentLength;
    CallbackFrame.Context.Msr = PPC_USER_MSR;
    Status = KiPpcCheckUserPage((PVOID)CallbackFrame.Context.Iar, FALSE, &CallbackFrame);
    if (NT_SUCCESS(Status))
        Status = KiPpcCheckUserPage((PVOID)(UserStack - 1), TRUE, &CallbackFrame);
    if (NT_SUCCESS(Status))
        Status = KiPpcCheckUserPage(Thread->Teb, TRUE, &CallbackFrame);
    if (!NT_SUCCESS(Status))
        return Status;

    CallbackStatus = KiPpcCallUserMode(&CallbackFrame, Result, ResultLength);
    ASSERT(Thread->TrapFrame == OuterFrame);
    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
        OldStack = OuterFrame->Context.Gpr1;

    Status = KiPpcCopyFromUser(&GdiBatchCount, (PUCHAR)Thread->Teb + FIELD_OFFSET(TEB, GdiBatchCount), sizeof(GdiBatchCount));
    if (!NT_SUCCESS(Status))
        return Status;
    if (GdiBatchCount)
    {
        if (!KeGdiFlushUserBatch || (OldStack < MM_ALLOCATION_GRANULARITY + 256))
            return STATUS_INVALID_DEVICE_STATE;
        OuterFrame->Context.Gpr1 = OldStack - 256;
        KeGdiFlushUserBatch();
    }
    OuterFrame->Context.Gpr1 = OldStack;
    return CallbackStatus;
}

NTSTATUS
NTAPI
NtCallbackReturn(_In_ PVOID Result, _In_ ULONG ResultLength, _In_ NTSTATUS CallbackStatus)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKCALLOUT_FRAME CalloutFrame = Thread->CallbackStack;
    PKTRAP_FRAME CurrentFrame = Thread->TrapFrame;
    PKTRAP_FRAME OuterFrame;

    if (!CalloutFrame)
        return STATUS_NO_CALLBACK_ACTIVE;
    if ((KeGetCurrentIrql() != PASSIVE_LEVEL) || (Thread->PreviousMode != UserMode) || (Thread->ApcStateIndex != OriginalApcEnvironment) || Thread->CombinedApcDisable || !CurrentFrame || !KiUserTrap(CurrentFrame))
        return STATUS_INVALID_DEVICE_STATE;
    OuterFrame = CalloutFrame->TrapFrame;
    ASSERT(OuterFrame && KiUserTrap(OuterFrame));

    if ((CallbackStatus == STATUS_CALLBACK_POP_STACK) && (!KiPpcUserControlAddress(CurrentFrame->Context.Iar) || (CurrentFrame->Context.Iar & 3) || !KiPpcUserControlAddress(CurrentFrame->Context.Gpr1)))
        return STATUS_INVALID_PARAMETER;

    *CalloutFrame->OutputBuffer = Result;
    *CalloutFrame->OutputLength = ResultLength;
    _disable();
    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
    {
        OuterFrame->Context = CurrentFrame->Context;
        OuterFrame->Context.Msr = PPC_USER_MSR;
    }
    Thread->InitialStack = CalloutFrame->InitialStack;
    Thread->TrapFrame = OuterFrame;
    Thread->CallbackStack = CalloutFrame->CallbackStack;
    Thread->PreviousMode = UserMode;
    KeGetPcr()->InitialStack = (ULONG_PTR)Thread->InitialStack;
    KiPpcCallbackReturn(CalloutFrame, CallbackStatus);
}
