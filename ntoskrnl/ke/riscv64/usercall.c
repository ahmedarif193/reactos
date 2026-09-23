/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS private RV64 NT user APC and context-return contract. */
#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

NTSTATUS NTAPI KiRiscvCallUserMode(PKTRAP_FRAME Frame, PVOID *OutputBuffer, PULONG OutputLength);
DECLSPEC_NORETURN VOID NTAPI KiRiscvCallbackReturn(PKCALLOUT_FRAME CalloutFrame, NTSTATUS Status);

NTSTATUS
NTAPI
KeRaiseUserException(_In_ NTSTATUS ExceptionCode)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME Frame = Thread->TrapFrame;
    NTSTATUS Status;

    if (!Thread->Teb || !Frame || !KiUserTrap(Frame) ||
        !KeRaiseUserExceptionDispatcher)
        return STATUS_UNSUCCESSFUL;

    Status = KiRiscvCopyToUser(&Thread->Teb->ExceptionCode,
                              &ExceptionCode, sizeof(ExceptionCode));
    if (!NT_SUCCESS(Status))
        return Status;

    /* The ecall stub has not changed ra: the C dispatcher returns directly
     * to the system service's user caller, bypassing the stub's ret. */
    Frame->Context.Pc = (ULONG_PTR)KeRaiseUserExceptionDispatcher;
    return ExceptionCode;
}

static
BOOLEAN
KiRiscvUserControlAddress(_In_ ULONG_PTR Address)
{
    return (Address >= MM_ALLOCATION_GRANULARITY) && (Address < (ULONG_PTR)MmUserProbeAddress);
}

static
VOID
KiRiscvUserTransitionFailure(_Inout_ PKTRAP_FRAME Frame, _In_ NTSTATUS Status)
{
    EXCEPTION_RECORD Record = {0};

    Record.ExceptionCode = Status;
    Record.ExceptionAddress = (PVOID)Frame->Context.Pc;
    KiDispatchException(&Record, NULL, Frame, UserMode, TRUE);
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
    if (!KiRiscvUserControlAddress(Frame->Context.Sp) ||
        (Frame->Context.Sp < MM_ALLOCATION_GRANULARITY + sizeof(Context)) ||
        !KiRiscvUserControlAddress((ULONG_PTR)NormalRoutine) ||
        !KiRiscvUserControlAddress((ULONG_PTR)KeUserApcDispatcher) ||
        ((ULONG_PTR)NormalRoutine & 1) || ((ULONG_PTR)KeUserApcDispatcher & 1))
    {
        KiRiscvUserTransitionFailure(Frame, STATUS_ACCESS_VIOLATION);
        return;
    }

    Stack = ALIGN_DOWN_BY(Frame->Context.Sp - sizeof(Context), 16);
    Context.ContextFlags = CONTEXT_FULL;
    KeTrapFrameToContext(Frame, ExceptionFrame, &Context);
    Status = KiRiscvCopyToUser((PVOID)Stack, &Context, sizeof(Context));
    if (!NT_SUCCESS(Status))
    {
        KiRiscvUserTransitionFailure(Frame, Status);
        return;
    }

    /* The user dispatcher supplies the saved CONTEXT as the fourth normal
     * routine argument; the first three keep the NT APC calling convention. */
    Frame->Context.A0 = (ULONG_PTR)NormalContext;
    Frame->Context.A1 = (ULONG_PTR)SystemArgument1;
    Frame->Context.A2 = (ULONG_PTR)SystemArgument2;
    Frame->Context.A3 = (ULONG_PTR)NormalRoutine;
    Frame->Context.Sp = Stack;
    Frame->Context.Pc = (ULONG_PTR)KeUserApcDispatcher;
    Frame->Context.Ra = 0;
    Frame->Sstatus = RISCV_USER_SSTATUS;
}

/* Fault in the initial execution/stack/TEB pages and verify actual U/R/W/X
 * permissions. Hardware still enforces the permissions after this check. */
static
NTSTATUS
KiRiscvCheckUserPage(_In_ PVOID Address, _In_ ULONG Access, _Inout_ PKTRAP_FRAME Frame)
{
    MI_RISCV_PAGE_WALK Walk;
    NTSTATUS Status;
    ULONG Fault = (Access == MI_RISCV_PTE_EXECUTE) ? MI_RISCV_FAULT_EXECUTE :
                  (Access == MI_RISCV_PTE_WRITE) ? MI_RISCV_FAULT_WRITE : 0;

    Status = MiRiscvWalkCurrentPageTables(Address, &Walk);
    if (NT_SUCCESS(Status))
    {
        if ((Walk.Value.u.Long & (MI_RISCV_PTE_OWNER | Access)) == (MI_RISCV_PTE_OWNER | Access))
            return STATUS_SUCCESS;
        Fault |= MI_RISCV_FAULT_PRESENT;
    }
    Status = MmAccessFault(Fault, Address, UserMode, Frame);
    if (!NT_SUCCESS(Status)) return Status;

    /* A stack guard fault is a successful first half of stack growth: the
     * memory manager consumes the old guard, installs the next one and leaves
     * this page as demand-zero. Complete that second fault before checking
     * the leaf. */
    if (Status == STATUS_PAGE_FAULT_GUARD_PAGE)
    {
        Status = MmAccessFault(Fault, Address, UserMode, Frame);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = MiRiscvWalkCurrentPageTables(Address, &Walk);
    if (!NT_SUCCESS(Status) || ((Walk.Value.u.Long & (MI_RISCV_PTE_OWNER | Access)) != (MI_RISCV_PTE_OWNER | Access)))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

DECLSPEC_NORETURN
VOID
NTAPI
KiRiscvReturnToUser(_Inout_ PKTRAP_FRAME Frame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(KiUserTrap(Frame));
Retry:
    for (;;)
    {
        _disable();
        if (!Thread->ApcState.UserApcPending) break;
        KfRaiseIrql(APC_LEVEL);
        _enable();
        KiDeliverApc(UserMode, NULL, Frame);
        KfLowerIrql(PASSIVE_LEVEL);
    }
    _enable();

    if (!KiRiscvUserControlAddress(Frame->Context.Pc) || (Frame->Context.Pc & 1) ||
        !KiRiscvUserControlAddress(Frame->Context.Sp) || (Frame->Context.Sp & 15))
    {
        KiRiscvUserTransitionFailure(Frame, STATUS_ACCESS_VIOLATION);
    }
    Status = KiRiscvCheckUserPage((PVOID)Frame->Context.Pc, MI_RISCV_PTE_EXECUTE, Frame);
    if (NT_SUCCESS(Status))
        Status = KiRiscvCheckUserPage((PVOID)(Frame->Context.Sp - 1), MI_RISCV_PTE_WRITE, Frame);
    if (NT_SUCCESS(Status))
        Status = KiRiscvCheckUserPage(Thread->Teb, MI_RISCV_PTE_WRITE, Frame);
    if (!NT_SUCCESS(Status))
        KiRiscvUserTransitionFailure(Frame, Status);

    _disable();
    if (Thread->ApcState.UserApcPending) goto Retry;
    Frame->Sstatus = RISCV_USER_SSTATUS;
    Thread->PreviousMode = UserMode;
    Thread->TrapFrame = Frame->PreviousTrapFrame;
    KiRiscvRestoreTrapFrame(Frame);
}

DECLSPEC_NORETURN
VOID
NTAPI
KiRiscvStartUserThread(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    if (!Thread->Teb || !Thread->TrapFrame ||
        (Thread->TrapFrame != ((PKTRAP_FRAME)Thread->InitialStack) - 1))
        KiRiscvUnimplemented("KiRiscvStartUserThread/invalid-frame");
    KiRiscvReturnToUser(Thread->TrapFrame);
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
    KiRiscvReturnToUser(TrapFrame);
}

NTSTATUS
NTAPI
KiRiscvUserModeCallout(_Inout_ PKTRAP_FRAME Frame, _Out_ PKCALLOUT_FRAME CalloutFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR InitialStack = (ULONG_PTR)CalloutFrame;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(!(InitialStack & 15));
    ASSERT(InitialStack >= Thread->StackLimit);
    ASSERT(InitialStack + sizeof(*CalloutFrame) <= (ULONG_PTR)Thread->InitialStack);

    /* U-origin traps below the suspended call need a full committed kernel
     * stack. Grow only the existing GUI reservation, never a small stack. */
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
    /* The prepared frame is above CalloutFrame, not in the reusable entry
     * stack below it. ReturnToUser restores its previous (outer) frame link. */
    KiRiscvReturnToUser(Frame);
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

    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Thread->PreviousMode != UserMode ||
        Thread->ApcState.KernelApcInProgress || Thread->ApcStateIndex != OriginalApcEnvironment ||
        Thread->CombinedApcDisable || !OuterFrame || !KiUserTrap(OuterFrame))
        return STATUS_INVALID_DEVICE_STATE;
    if (!Result || !ResultLength || (ArgumentLength && !Argument))
        return STATUS_INVALID_PARAMETER;
    if (!KiRiscvUserControlAddress((ULONG_PTR)KeUserCallbackDispatcher) ||
        ((ULONG_PTR)KeUserCallbackDispatcher & 1))
        return STATUS_PROCEDURE_NOT_FOUND;

    OldStack = OuterFrame->Context.Sp;
    if (!KiRiscvUserControlAddress(OldStack) ||
        OldStack < MM_ALLOCATION_GRANULARITY + (ULONG_PTR)ArgumentLength + sizeof(UserFrame))
        return STATUS_ACCESS_VIOLATION;
    if (OldStack & 15)
        return STATUS_DATATYPE_MISALIGNMENT;
    UserArguments = ALIGN_DOWN_BY(OldStack - ArgumentLength, 16);
    UserStack = UserArguments - sizeof(UserFrame);

    /* Only publish the callback after both protected copies succeed. A
     * failed copy may leave a user-buffer prefix, never a new kernel link. */
    Status = KiRiscvCopyToUser((PVOID)UserArguments, Argument, ArgumentLength);
    if (!NT_SUCCESS(Status))
        return Status;
    UserFrame.Buffer = (PVOID)UserArguments;
    UserFrame.Length = ArgumentLength;
    UserFrame.ApiNumber = RoutineIndex;
    UserFrame.Pc = OuterFrame->Context.Pc;
    UserFrame.Sp = OldStack;
    Status = KiRiscvCopyToUser((PVOID)UserStack, &UserFrame, sizeof(UserFrame));
    if (!NT_SUCCESS(Status))
        return Status;

    CallbackFrame = *OuterFrame;
    CallbackFrame.Context.Pc = (ULONG_PTR)KeUserCallbackDispatcher;
    CallbackFrame.Context.Sp = UserStack;
    CallbackFrame.Context.Ra = 0;
    CallbackFrame.Context.Tp = (ULONG_PTR)Thread->Teb;
    CallbackFrame.Context.A0 = RoutineIndex;
    CallbackFrame.Context.A1 = UserArguments;
    CallbackFrame.Context.A2 = ArgumentLength;
    CallbackFrame.Sstatus = RISCV_USER_SSTATUS;
    Status = KiRiscvCheckUserPage((PVOID)CallbackFrame.Context.Pc, MI_RISCV_PTE_EXECUTE, &CallbackFrame);
    if (NT_SUCCESS(Status))
        Status = KiRiscvCheckUserPage((PVOID)(UserStack - 1), MI_RISCV_PTE_WRITE, &CallbackFrame);
    if (NT_SUCCESS(Status))
        Status = KiRiscvCheckUserPage(Thread->Teb, MI_RISCV_PTE_WRITE, &CallbackFrame);
    if (!NT_SUCCESS(Status))
        return Status;

    CallbackStatus = KiRiscvCallUserMode(&CallbackFrame, Result, ResultLength);
    ASSERT(Thread->TrapFrame == OuterFrame);
    /* POP_STACK supplies a new user continuation; ordinary callbacks leave
     * the interrupted context untouched, including its complete FP bank. */
    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
        OldStack = OuterFrame->Context.Sp;

    Status = KiRiscvCopyFromUser(&GdiBatchCount, (PUCHAR)Thread->Teb + FIELD_OFFSET(TEB, GdiBatchCount), sizeof(GdiBatchCount));
    if (!NT_SUCCESS(Status))
        return Status;
    if (GdiBatchCount)
    {
        if (!KeGdiFlushUserBatch || OldStack < MM_ALLOCATION_GRANULARITY + 256)
            return STATUS_INVALID_DEVICE_STATE;
        OuterFrame->Context.Sp = OldStack - 256;
        KeGdiFlushUserBatch();
    }
    OuterFrame->Context.Sp = OldStack;
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
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Thread->PreviousMode != UserMode ||
        Thread->ApcStateIndex != OriginalApcEnvironment || Thread->CombinedApcDisable ||
        !CurrentFrame || !KiUserTrap(CurrentFrame))
        return STATUS_INVALID_DEVICE_STATE;
    ASSERT((PVOID)CalloutFrame == Thread->InitialStack);
    ASSERT((ULONG_PTR)CalloutFrame >= Thread->StackLimit);
    ASSERT((ULONG_PTR)CalloutFrame + sizeof(*CalloutFrame) <= (ULONG_PTR)Thread->StackBase);
    OuterFrame = CalloutFrame->TrapFrame;
    ASSERT(OuterFrame && KiUserTrap(OuterFrame));

    if (CallbackStatus == STATUS_CALLBACK_POP_STACK &&
        (!KiRiscvUserControlAddress(CurrentFrame->Context.Pc) || (CurrentFrame->Context.Pc & 1) ||
         !KiRiscvUserControlAddress(CurrentFrame->Context.Sp) || (CurrentFrame->Context.Sp & 15)))
        return STATUS_INVALID_PARAMETER;

    /* The tuple is user data, not a kernel buffer to dereference. Its NT
     * caller remains responsible for probing/capturing any returned bytes. */
    *CalloutFrame->OutputBuffer = Result;
    *CalloutFrame->OutputLength = ResultLength;
    _disable();
    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
    {
        OuterFrame->Context = CurrentFrame->Context;
        OuterFrame->Sstatus = RISCV_USER_SSTATUS;
        /* In particular, never copy CurrentFrame's PreviousTrapFrame link:
         * it belongs to the callback stack that is being discarded. */
    }
    Thread->InitialStack = CalloutFrame->InitialStack;
    Thread->TrapFrame = OuterFrame;
    Thread->CallbackStack = CalloutFrame->CallbackStack;
    Thread->PreviousMode = UserMode;
    KiRiscvCallbackReturn(CalloutFrame, CallbackStatus);
}
