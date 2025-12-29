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
    CONTEXT Context = { 0 };
    ULONG_PTR Stack;

    UNREFERENCED_PARAMETER(ExceptionFrame);

    Context.ContextFlags = CONTEXT_FULL | CONTEXT_INTEGER;
    KeTrapFrameToContext(TrapFrame, NULL, &Context);

    Stack = (Context.Sp & ~0xF) - sizeof(CONTEXT);
    ProbeForWrite((PVOID)Stack, sizeof(CONTEXT), sizeof(QUAD));
    RtlMoveMemory((PVOID)Stack, &Context, sizeof(CONTEXT));

    TrapFrame->X0 = (ULONG_PTR)NormalContext;
    TrapFrame->X1 = (ULONG_PTR)SystemArgument1;
    TrapFrame->X2 = (ULONG_PTR)SystemArgument2;
    TrapFrame->X3 = (ULONG_PTR)NormalRoutine;
    TrapFrame->Sp = Stack;
    TrapFrame->Pc = (ULONG_PTR)KeUserApcDispatcher;
    TrapFrame->Lr = (ULONG_PTR)KeUserApcDispatcher;
}
