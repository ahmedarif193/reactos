/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/usercall.c
 * PURPOSE:         System call support for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
KiTrapReturn(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame);

typedef NTSTATUS (*PKI_SYSCALL_PARAM_HANDLER)(PVOID, PVOID *);

#define BUILD_SYSCALLS                                                        \
SYSCALL(00, ())                                                               \
SYSCALL(01, (_1))                                                             \
SYSCALL(02, (_1, _2))                                                         \
SYSCALL(03, (_1, _2, _3))                                                     \
SYSCALL(04, (_1, _2, _3, _4))                                                 \
SYSCALL(05, (_1, _2, _3, _4, _5))                                             \
SYSCALL(06, (_1, _2, _3, _4, _5, _6))                                         \
SYSCALL(07, (_1, _2, _3, _4, _5, _6, _7))                                     \
SYSCALL(08, (_1, _2, _3, _4, _5, _6, _7, _8))                                 \
SYSCALL(09, (_1, _2, _3, _4, _5, _6, _7, _8, _9))                             \
SYSCALL(0A, (_1, _2, _3, _4, _5, _6, _7, _8, _9, a))                          \
SYSCALL(0B, (_1, _2, _3, _4, _5, _6, _7, _8, _9, a, b))                       \
SYSCALL(0C, (_1, _2, _3, _4, _5, _6, _7, _8, _9, a, b, c))                    \
SYSCALL(0D, (_1, _2, _3, _4, _5, _6, _7, _8, _9, a, b, c, d))                 \
SYSCALL(0E, (_1, _2, _3, _4, _5, _6, _7, _8, _9, a, b, c, d, e))              \
SYSCALL(0F, (_1, _2, _3, _4, _5, _6, _7, _8, _9, a, b, c, d, e, f))           \
SYSCALL(10, (_1, _2, _3, _4, _5, _6, _7, _8, _9, a, b, c, d, e, f, _10))      \
SYSCALL(11, (_1, _2, _3, _4, _5, _6, _7, _8, _9, a, b, c, d, e, f, _10, _11))

#define PROTO
#include "ke_i.h"
BUILD_SYSCALLS

#define FUNC
#include "ke_i.h"
BUILD_SYSCALLS

static const PKI_SYSCALL_PARAM_HANDLER KiSyscallHandlers[] =
{
    KiSyscall00Param,
    KiSyscall01Param,
    KiSyscall02Param,
    KiSyscall03Param,
    KiSyscall04Param,
    KiSyscall05Param,
    KiSyscall06Param,
    KiSyscall07Param,
    KiSyscall08Param,
    KiSyscall09Param,
    KiSyscall0AParam,
    KiSyscall0BParam,
    KiSyscall0CParam,
    KiSyscall0DParam,
    KiSyscall0EParam,
    KiSyscall0FParam,
    KiSyscall10Param,
    KiSyscall11Param,
};

C_ASSERT(RTL_NUMBER_OF(KiSyscallHandlers) == 0x12);

VOID
KiSystemService(
    _Inout_ PKTHREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Instruction)
{
    PKPRCB Prcb;
    PKSERVICE_TABLE_DESCRIPTOR DescriptorTable;
    ULONG_PTR ServiceTable;
    ULONG TableIndex;
    ULONG ServiceNumber;
    ULONG ArgumentCount;
    ULONG Index;
    PVOID KernelArguments[RTL_NUMBER_OF(KiSyscallHandlers)];
    PVOID *UserArguments = NULL;
    PVOID SystemCall;
    KIRQL OldIrql;

    ASSERT(Thread != NULL);
    ASSERT(TrapFrame != NULL);

    /* Account the system call */
    Prcb = KeGetCurrentPrcb();
    if (Prcb != NULL)
    {
        Prcb->KeSystemCalls++;
    }

    ServiceTable = (ULONG_PTR)Thread->ServiceTable;
    TableIndex = (Instruction >> SERVICE_TABLE_SHIFT) & SERVICE_TABLE_MASK;
    DescriptorTable = (PKSERVICE_TABLE_DESCRIPTOR)(ServiceTable + TableIndex);

    ServiceNumber = Instruction & SERVICE_NUMBER_MASK;
    if (ServiceNumber >= DescriptorTable->Limit)
    {
        TrapFrame->X0 = STATUS_INVALID_SYSTEM_SERVICE;
        return;
    }

    SystemCall = (PVOID)DescriptorTable->Base[ServiceNumber];
    ArgumentCount = DescriptorTable->Number[ServiceNumber] / sizeof(ULONG_PTR);
    if (ArgumentCount >= RTL_NUMBER_OF(KiSyscallHandlers))
    {
        ArgumentCount = RTL_NUMBER_OF(KiSyscallHandlers) - 1;
    }

    for (Index = 0; (Index < ArgumentCount) && (Index < 8); Index++)
    {
        KernelArguments[Index] = (PVOID)TrapFrame->X[Index];
    }

    if (ArgumentCount > 8)
    {
        if (KiGetPreviousMode(TrapFrame) == UserMode)
        {
            UserArguments = (PVOID*)TrapFrame->Sp;
            ProbeForRead(UserArguments,
                         (ArgumentCount - 8) * sizeof(PVOID),
                         sizeof(PVOID));
        }
        else
        {
            UserArguments = (PVOID*)(TrapFrame + 1);
        }

        for (Index = 8; Index < ArgumentCount; Index++)
        {
            KernelArguments[Index] = UserArguments[Index - 8];
        }
    }

    /* Ensure IRQs are enabled while we execute the service */
    KeRestoreInterrupts(TRUE);

    TrapFrame->X0 = KiSyscallHandlers[ArgumentCount](SystemCall, KernelArguments);

    if (KiGetPreviousMode(TrapFrame) == UserMode)
    {
        OldIrql = KeGetCurrentIrql();
        if (OldIrql != PASSIVE_LEVEL)
        {
            KeGetPcr()->CurrentIrql = PASSIVE_LEVEL;
            KeRestoreInterrupts(TRUE);
            KeBugCheckEx(IRQL_GT_ZERO_AT_SYSTEM_SERVICE,
                         (ULONG_PTR)SystemCall,
                         OldIrql,
                         0,
                         0);
        }

        if ((Thread->ApcStateIndex != CurrentApcEnvironment) ||
            (Thread->CombinedApcDisable != 0))
        {
            KeBugCheckEx(APC_INDEX_MISMATCH,
                         (ULONG_PTR)SystemCall,
                         Thread->ApcStateIndex,
                         Thread->CombinedApcDisable,
                         0);
        }
    }

    Thread->TrapFrame = KiGetLinkedTrapFrame(TrapFrame);
}

DECLSPEC_NORETURN
VOID
KiUserCallbackExit(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiTrapReturn(TrapFrame, NULL);
    UNREACHABLE;
}

DECLSPEC_NORETURN
VOID
KiExceptionExit(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    KiTrapReturn(TrapFrame, ExceptionFrame);
    UNREACHABLE;
}

VOID
NTAPI
KiInitializeUserApc(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKNORMAL_ROUTINE NormalRoutine,
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    PUAPC_FRAME ApcFrame;
    CONTEXT LocalContext = { 0 };
    ULONG_PTR Stack;

    UNREFERENCED_PARAMETER(ExceptionFrame);

    ApcFrame = (PUAPC_FRAME)ALIGN_DOWN_POINTER_BY(TrapFrame->Sp - sizeof(*ApcFrame), 16);

    LocalContext.ContextFlags = CONTEXT_FULL | CONTEXT_INTEGER | CONTEXT_ARM64;
    KeTrapFrameToContext(TrapFrame, NULL, &LocalContext);

    Stack = (ULONG_PTR)ApcFrame;
    ProbeForWrite(ApcFrame, sizeof(*ApcFrame), TYPE_ALIGNMENT(UAPC_FRAME));
    RtlMoveMemory(&ApcFrame->Context, &LocalContext, sizeof(LocalContext));
    ApcFrame->MachineFrame.Pc = TrapFrame->Pc;
    ApcFrame->MachineFrame.Sp = TrapFrame->Sp;

    TrapFrame->X0 = (ULONG_PTR)NormalContext;
    TrapFrame->X1 = (ULONG_PTR)SystemArgument1;
    TrapFrame->X2 = (ULONG_PTR)SystemArgument2;
    TrapFrame->X3 = (ULONG_PTR)NormalRoutine;
    TrapFrame->Sp = Stack;
    TrapFrame->Pc = (ULONG_PTR)KeUserApcDispatcher;
    TrapFrame->Lr = (ULONG_PTR)KeUserApcDispatcher;
}

NTSTATUS
FASTCALL
KiUserModeCallout(
    _Out_ PKCALLOUT_FRAME CalloutFrame)
{
    PKTHREAD CurrentThread;
    PKTRAP_FRAME TrapFrame;
    KTRAP_FRAME CallbackTrapFrame;
    PKIPCR Pcr;
    ULONG_PTR InitialStack;
    NTSTATUS Status;

    CurrentThread = KeGetCurrentThread();

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT((CurrentThread->ApcStateIndex == OriginalApcEnvironment) &&
           (CurrentThread->CombinedApcDisable == 0));

    InitialStack = (ULONG_PTR)ALIGN_DOWN_POINTER_BY(CalloutFrame, 16);

    if ((InitialStack - KERNEL_STACK_SIZE) < (ULONG_PTR)CurrentThread->StackLimit)
    {
        Status = MmGrowKernelStack((PVOID)InitialStack);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    CalloutFrame->CallbackStack = (ULONG_PTR)CurrentThread->CallbackStack;
    CalloutFrame->InitialStack = (ULONG_PTR)CurrentThread->InitialStack;

    TrapFrame = CurrentThread->TrapFrame;
    CalloutFrame->TrapFrame = (ULONG_PTR)TrapFrame;

    CurrentThread->CallbackStack = CalloutFrame;

    _disable();

    CurrentThread->InitialStack = (PVOID)InitialStack;

    CallbackTrapFrame = *TrapFrame;

    Pcr = (PKIPCR)KeGetPcr();
    if (Pcr != NULL)
    {
        Pcr->Prcb.RspBase = InitialStack;
    }

    CallbackTrapFrame.Pc = (ULONG_PTR)KeUserCallbackDispatcher;

    _enable();

    KiUserCallbackExit(&CallbackTrapFrame);
}

VOID
KiSetupUserCalloutFrame(
    _Out_ PUCALLOUT_FRAME UserCalloutFrame,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG ApiNumber,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength)
{
    UserCalloutFrame->Buffer = Buffer;
    UserCalloutFrame->Length = BufferLength;
    UserCalloutFrame->ApiNumber = ApiNumber;
    UserCalloutFrame->MachineFrame.Pc = TrapFrame->Pc;
    UserCalloutFrame->MachineFrame.Sp = TrapFrame->Sp;
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
    ULONG_PTR OldStack;
    PUCHAR UserArguments;
    PUCALLOUT_FRAME CalloutFrame;
    PULONG_PTR UserStackPointer;
    NTSTATUS CallbackStatus;
    PTEB Teb;
    ULONG GdiBatchCount = 0;

    ASSERT(KeGetCurrentThread()->ApcState.KernelApcInProgress == FALSE);
    ASSERT(KeGetPreviousMode() == UserMode);

    UserStackPointer = KiGetUserModeStackAddress();
    OldStack = *UserStackPointer;

    _SEH2_TRY
    {
        UserArguments = (PUCHAR)ALIGN_DOWN_POINTER_BY(OldStack - ArgumentLength, 16);
        CalloutFrame = ((PUCALLOUT_FRAME)UserArguments) - 1;

        ProbeForWrite(CalloutFrame,
                      sizeof(*CalloutFrame) + ArgumentLength,
                      sizeof(PVOID));

        RtlCopyMemory(UserArguments, Argument, ArgumentLength);

        KiSetupUserCalloutFrame(CalloutFrame,
                                KeGetCurrentThread()->TrapFrame,
                                RoutineIndex,
                                UserArguments,
                                ArgumentLength);

        Teb = KeGetCurrentThread()->Teb;

        *UserStackPointer = (ULONG_PTR)CalloutFrame;
        CallbackStatus = KiCallUserMode(Result, ResultLength);
        if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
        {
            OldStack = *UserStackPointer;
        }

        GdiBatchCount = Teb->GdiBatchCount;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (GdiBatchCount)
    {
        *UserStackPointer -= 256;
        KeGdiFlushUserBatch();
    }

    *UserStackPointer = OldStack;
    return CallbackStatus;
}

NTSTATUS
NTAPI
NtCallbackReturn(
    _In_ PVOID Result,
    _In_ ULONG ResultLength,
    _In_ NTSTATUS CallbackStatus)
{
    PKTHREAD CurrentThread;
    PKCALLOUT_FRAME CalloutFrame;
    PKTRAP_FRAME CallbackTrapFrame, TrapFrame;
    PKIPCR Pcr;

    CurrentThread = KeGetCurrentThread();
    CalloutFrame = CurrentThread->CallbackStack;
    if (CalloutFrame == NULL)
    {
        return STATUS_NO_CALLBACK_ACTIVE;
    }

    *((PVOID*)CalloutFrame->OutputBuffer) = Result;
    *((ULONG*)CalloutFrame->OutputLength) = ResultLength;

    CallbackTrapFrame = CurrentThread->TrapFrame;

    _disable();

    Pcr = (PKIPCR)KeGetPcr();

    TrapFrame = (PKTRAP_FRAME)CalloutFrame->TrapFrame;

    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
    {
        *TrapFrame = *CallbackTrapFrame;
    }

    if (Pcr != NULL)
    {
        Pcr->Prcb.RspBase = CalloutFrame->InitialStack;
    }

    CurrentThread->InitialStack = (PVOID)CalloutFrame->InitialStack;
    CurrentThread->TrapFrame = TrapFrame;
    CurrentThread->CallbackStack = (PVOID)CalloutFrame->CallbackStack;

    _enable();

    KiCallbackReturn(CalloutFrame, CallbackStatus);
}
