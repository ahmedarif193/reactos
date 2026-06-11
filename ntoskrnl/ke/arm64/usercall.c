/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/usercall.c
 * PURPOSE:         System call support for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#if (NTDDI_VERSION >= NTDDI_WIN8)
#define CalloutInitialStack Spare1
#define CalloutCallbackStack Spare2
#else
#define CalloutInitialStack InitialStack
#define CalloutCallbackStack CallbackStack
#endif

VOID
KiTrapReturn(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame);

/* Forward declaration - PsConvertToGuiThread is not in any header because
 * it's normally called only from architecture-specific assembly (x86-64) */
NTSTATUS NTAPI PsConvertToGuiThread(VOID);

/*
 * Return type is ULONG_PTR (not NTSTATUS) to preserve all 64 bits of the
 * return value.  Many Nt* syscalls return handles (pointer-sized values).
 * If the dispatch layer truncates through NTSTATUS (LONG, 32-bit signed),
 * handles with bit 31 set get sign-extended to 0xFFFFFFFF_xxxxxxxx when
 * stored back into TrapFrame->X0 (64-bit), producing corrupt handles
 * that fail GDI/USER validation.
 */
typedef ULONG_PTR (*PKI_SYSCALL_PARAM_HANDLER)(PVOID, PVOID *);

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

static
NTSTATUS
KiArm64CopyToCurrentUserBuffer(
    _In_ PVOID TargetAddress,
    _In_reads_bytes_(BufferSize) CONST VOID *Buffer,
    _In_ SIZE_T BufferSize,
    _In_z_ PCSTR Tag)
{
    ULONG_PTR CurrentVa;
    SIZE_T RemainingSize;
    EXCEPTION_RECORD ExceptionRecord;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Tag);

    CurrentVa = (ULONG_PTR)TargetAddress;
    RemainingSize = BufferSize;

    while (RemainingSize > 0)
    {
        PVOID PageAddress;
        SIZE_T PageOffset;
        SIZE_T ChunkSize;

        PageAddress = PAGE_ALIGN((PVOID)CurrentVa);
        PageOffset = BYTE_OFFSET(CurrentVa);
        ChunkSize = PAGE_SIZE - PageOffset;
        if (ChunkSize > RemainingSize)
        {
            ChunkSize = RemainingSize;
        }

        Status = MmAccessFaultEx(0x2, PageAddress, UserMode, NULL, FALSE);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        KeInvalidateTlbEntry(PageAddress);

        CurrentVa += ChunkSize;
        RemainingSize -= ChunkSize;
    }

    /*
     * Copy the APC frame through the actual EL0 VA once the pages are known to
     * be resident. This matches the established exception/callout paths and
     * removes the PFN-alias/full-page-DC-CIVAC workaround that can defer a
     * bad cache-maintenance side effect until the first ERET into user mode.
     */
    _SEH2_TRY
    {
        ProbeForWrite(TargetAddress, BufferSize, sizeof(ULONG64));
        RtlCopyMemory(TargetAddress, Buffer, BufferSize);
    }
    _SEH2_EXCEPT(ExceptionRecord = *_SEH2_GetExceptionInformation()->ExceptionRecord,
                 EXCEPTION_EXECUTE_HANDLER)
    {
        Status = ExceptionRecord.ExceptionCode;
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return STATUS_SUCCESS;
}

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
    ULONG_PTR RegisterArguments[8];
    PVOID *UserArguments = NULL;
    PVOID SystemCall;
    KIRQL OldIrql;
    ULONG GdiBatchCount = 0;

    ASSERT(Thread != NULL);
    ASSERT(TrapFrame != NULL);

    /* Account the system call */
    Prcb = KeGetCurrentPrcb();
    if (Prcb != NULL)
    {
        Prcb->KeSystemCalls++;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(RegisterArguments); Index++)
    {
        RegisterArguments[Index] = TrapFrame->X[Index];
    }

#if defined(_WIN64) && (NTDDI_VERSION >= NTDDI_LONGHORN)
    ServiceTable = (ULONG_PTR)(Thread->GuiThread ?
                               (PVOID)KeServiceDescriptorTableShadow :
                               (PVOID)KeServiceDescriptorTable);
#else
    ServiceTable = (ULONG_PTR)(PVOID)Thread->ServiceTable;
#endif
    TableIndex = (Instruction >> SERVICE_TABLE_SHIFT) & SERVICE_TABLE_MASK;
    DescriptorTable = (PKSERVICE_TABLE_DESCRIPTOR)(ServiceTable + TableIndex);

    ServiceNumber = Instruction & SERVICE_NUMBER_MASK;

    if (ServiceNumber >= DescriptorTable->Limit)
    {
        /*
         * ARM64 FIX: GUI Thread Conversion for Win32K System Calls.
         *
         * When a user-mode thread makes its first win32k system call (table
         * index 1), the thread's ServiceTable still points to
         * KeServiceDescriptorTable, which only has the NT table (index 0)
         * populated. The win32k table (index 1) has Limit=0 and Base=NULL,
         * so every service number fails this check.
         *
         * On x86-64 this is handled by KiSystemCallEntry64/KiConvertToGuiThread
         * in assembly. On ARM64 we must handle it here in C.
         *
         * Call PsConvertToGuiThread() to:
         * 1. Allocate a large kernel stack if needed
         * 2. Call the win32k process callout (W32pProcessCallout)
         * 3. Switch ServiceTable to KeServiceDescriptorTableShadow
         * 4. Call the win32k thread callout (W32pThreadCallout)
         *
         * Then retry the service lookup with the new shadow table.
         */
        if ((TableIndex == SERVICE_TABLE_TEST) &&
#if defined(_WIN64) && (NTDDI_VERSION >= NTDDI_LONGHORN)
            !Thread->GuiThread
#else
            (Thread->ServiceTable == KeServiceDescriptorTable)
#endif
            )
        {
            /* Only convert if win32k callouts are registered */
            if (PspW32ProcessCallout != NULL && PspW32ThreadCallout != NULL)
            {
                NTSTATUS ConvertStatus = PsConvertToGuiThread();
                if (NT_SUCCESS(ConvertStatus) ||
                    ConvertStatus == STATUS_ALREADY_WIN32)
                {
                    /*
                     * ARM64 FIX: Re-read TrapFrame after stack switch.
                     *
                     * PsConvertToGuiThread() calls KeSwitchKernelStack() which
                     * copies the entire kernel stack to a new (larger) location
                     * and adjusts Thread->TrapFrame accordingly. Our local
                     * TrapFrame pointer still references the OLD stack which
                     * has been freed by MmDeleteKernelStack(). We MUST update
                     * our local pointer to the new location.
                     *
                     * TrapFrame->TrapFrame (the linked list pointer to the
                     * previous trap frame) was also adjusted by
                     * KeSwitchKernelStack, so the linked list is correct.
                     */
                    TrapFrame = Thread->TrapFrame;
                    for (Index = 0; Index < RTL_NUMBER_OF(RegisterArguments); Index++)
                    {
                        TrapFrame->X[Index] = RegisterArguments[Index];
                    }

                    /* Retry with the new service table */
#if defined(_WIN64) && (NTDDI_VERSION >= NTDDI_LONGHORN)
                    ServiceTable = (ULONG_PTR)(Thread->GuiThread ?
                                               (PVOID)KeServiceDescriptorTableShadow :
                                               (PVOID)KeServiceDescriptorTable);
#else
                    ServiceTable = (ULONG_PTR)(PVOID)Thread->ServiceTable;
#endif
                    DescriptorTable =
                        (PKSERVICE_TABLE_DESCRIPTOR)(ServiceTable + TableIndex);

                    if (ServiceNumber < DescriptorTable->Limit)
                    {
                        /* Success - fall through to the service dispatch below */
                        goto ServiceDispatch;
                    }
                }
            }
        }

        TrapFrame->X0 = STATUS_INVALID_SYSTEM_SERVICE;
        return;
    }

ServiceDispatch:

    if ((KiGetPreviousMode(TrapFrame) == UserMode) &&
        (TableIndex == SERVICE_TABLE_TEST) &&
        (KeGdiFlushUserBatch != NULL))
    {
        _SEH2_TRY
        {
            GdiBatchCount = Thread->Teb->GdiBatchCount;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            GdiBatchCount = 0;
        }
        _SEH2_END;

        if (GdiBatchCount != 0)
        {
            KeGdiFlushUserBatch();
        }
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

        if ((Thread->ApcStateIndex != OriginalApcEnvironment) ||
            (Thread->CombinedApcDisable != 0))
        {
            KeBugCheckEx(APC_INDEX_MISMATCH,
                         (ULONG_PTR)SystemCall,
                         Thread->ApcStateIndex,
                         Thread->CombinedApcDisable,
                         0);
        }
    }

    /*
     * NOTE: Thread->TrapFrame is NOT restored here.
     *
     * On x86-64, the assembly exit code (trap.S) restores Thread->TrapFrame
     * from TrapFrame->TrapFrame after KiSystemService returns. On ARM64,
     * the equivalent is done by the SVC handler in trapc.c after this
     * function returns.
     *
     * We cannot do it here because after a GUI thread conversion
     * (KeSwitchKernelStack), the local TrapFrame parameter is stale
     * (points to the freed old stack). The caller (trapc.c) re-reads
     * TrapFrame from Thread->TrapFrame which was adjusted by
     * KeSwitchKernelStack, and then restores the linked list.
     */
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
    UAPC_FRAME LocalApcFrame = { 0 };
    CONTEXT LocalContext = { 0 };
    ULONG_PTR Stack;
    PKTHREAD Thread;
    PEPROCESS Process;
    PVOID UserApcDispatcher;
    PVOID UserNormalRoutine;
    NTSTATUS Status;

    /*
     * ARM64 FIX: Set X18 to TEB for user-mode ABI compliance.
     *
     * On ARM64, the platform ABI requires X18 to point to the Thread Environment Block (TEB)
     * when executing in user mode. The kernel must ensure X18 is properly set before
     * returning to user mode.
     *
     * Without this, user-mode code that accesses thread-local storage via X18 will fault
     * or access garbage data, causing crashes or hangs.
     */
    Thread = KeGetCurrentThread();
    Process = (PEPROCESS)Thread->ApcState.Process;
    TrapFrame->X18 = (ULONG_PTR)Thread->Teb;

    /*
     * ARM64 FIX: Convert KeUserApcDispatcher from its kernel-mapped ntdll
     * address to the user-space equivalent. If conversion fails but the
     * global already contains a user VA, use it directly.
     */
    UserApcDispatcher = KiConvertSystemDllAddressToUser(KeUserApcDispatcher, Process);
    if (!UserApcDispatcher)
    {
        if ((KeUserApcDispatcher != NULL) &&
            ((ULONG_PTR)KeUserApcDispatcher < (ULONG_PTR)MmSystemRangeStart))
        {
            UserApcDispatcher = KeUserApcDispatcher;
        }
        if (!UserApcDispatcher)
        {
            DPRINT1("[arm64][APC] unresolved APC dispatcher: proc=%.16s KeUserApcDispatcher=%p SystemDllBase=%p PspSystemDllBase=%p TrapPc=%p TrapSp=%p\n",
                    PsGetCurrentProcess()->ImageFileName,
                    KeUserApcDispatcher,
                    Process ? PspSystemDllBase : NULL,
                    PspSystemDllBase,
                    (PVOID)(ULONG_PTR)TrapFrame->Pc,
                    (PVOID)(ULONG_PTR)TrapFrame->Sp);
            return;
        }
    }

    /*
     * ARM64 FIX: Convert NormalRoutine only if it is a kernel ntdll address.
     *
     * For LdrInitializeThunk APCs, NormalRoutine points into the kernel
     * mapping of ntdll and must be converted. For ordinary user APCs,
     * NormalRoutine is already a user-mode address (not within the kernel
     * ntdll mapping). The range-checked helper returns NULL for addresses
     * outside the kernel ntdll range, so we fall back to the original
     * address in that case.
     */
    UserNormalRoutine = KiConvertSystemDllAddressToUser(NormalRoutine, Process);
    if (!UserNormalRoutine)
    {
        /* NormalRoutine is not a kernel ntdll address -- use it directly */
        UserNormalRoutine = NormalRoutine;
    }

    ApcFrame = (PUAPC_FRAME)ALIGN_DOWN_POINTER_BY(TrapFrame->Sp - sizeof(*ApcFrame), 16);



    LocalContext.ContextFlags = CONTEXT_FULL | CONTEXT_INTEGER | CONTEXT_ARM64;
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &LocalContext);


    Stack = (ULONG_PTR)ApcFrame;
    if (((Stack & (TYPE_ALIGNMENT(UAPC_FRAME) - 1)) != 0) ||
        (Stack + sizeof(*ApcFrame) - 1 >= (ULONG_PTR)MmUserProbeAddress))
    {
        return;
    }

    LocalApcFrame.Context = LocalContext;
    LocalApcFrame.MachineFrame.Pc = TrapFrame->Pc;
    LocalApcFrame.MachineFrame.Sp = TrapFrame->Sp;
    Status = KiArm64CopyToCurrentUserBuffer(ApcFrame,
                                            &LocalApcFrame,
                                            sizeof(LocalApcFrame),
                                            "uapc");
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    /*
     * ARM64 FIX: SystemArgument1 may be PspSystemDllBase (a kernel address
     * pointing into the kernel ntdll mapping). If it is a kernel-range
     * address, attempt conversion; if the conversion returns NULL (the
     * address is in kernel space but outside ntdll), keep the original
     * value -- the caller presumably knows what it is doing.
     */
    PVOID UserSystemArgument1 = SystemArgument1;
    if (SystemArgument1 != NULL &&
        (ULONG_PTR)SystemArgument1 >= (ULONG_PTR)MmSystemRangeStart)
    {
        PVOID Converted = KiConvertSystemDllAddressToUser(SystemArgument1, Process);
        if (Converted != NULL)
        {
            UserSystemArgument1 = Converted;
        }
    }

    TrapFrame->X0 = (ULONG_PTR)NormalContext;
    TrapFrame->X1 = (ULONG_PTR)UserSystemArgument1;  /* Use converted user address if needed */
    TrapFrame->X2 = (ULONG_PTR)SystemArgument2;
    TrapFrame->X3 = (ULONG_PTR)UserNormalRoutine;  /* Use converted user address */
    TrapFrame->Sp = Stack;
    TrapFrame->Pc = (ULONG_PTR)UserApcDispatcher;  /* Use converted user address */
    TrapFrame->Lr = (ULONG_PTR)UserApcDispatcher;  /* Use converted user address */

}

NTSTATUS
FASTCALL
KiUserModeCallout(
    _Out_ PKCALLOUT_FRAME CalloutFrame)
{
    PKTHREAD CurrentThread;
    PEPROCESS Process;
    PKTRAP_FRAME TrapFrame;
    KTRAP_FRAME CallbackTrapFrame;
    PKIPCR Pcr;
    PVOID UserCallbackDispatcher;
    ULONG_PTR InitialStack;
    NTSTATUS Status;

    CurrentThread = KeGetCurrentThread();
    Process = (PEPROCESS)CurrentThread->ApcState.Process;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT((CurrentThread->ApcStateIndex == OriginalApcEnvironment) &&
           (CurrentThread->CombinedApcDisable == 0));

    UserCallbackDispatcher = KiConvertSystemDllAddressToUser(KeUserCallbackDispatcher, Process);
    if (!UserCallbackDispatcher &&
        (KeUserCallbackDispatcher != NULL) &&
        ((ULONG_PTR)KeUserCallbackDispatcher < (ULONG_PTR)MmSystemRangeStart))
    {
        UserCallbackDispatcher = KeUserCallbackDispatcher;
    }

    if (!UserCallbackDispatcher)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    InitialStack = (ULONG_PTR)ALIGN_DOWN_POINTER_BY(CalloutFrame, 16);

    if ((InitialStack - KERNEL_STACK_SIZE) < (ULONG_PTR)CurrentThread->StackLimit)
    {
        Status = MmGrowKernelStack((PVOID)InitialStack);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    CalloutFrame->CalloutCallbackStack = (ULONG_PTR)CurrentThread->CallbackStack;
    CalloutFrame->CalloutInitialStack = (ULONG_PTR)CurrentThread->InitialStack;

    TrapFrame = CurrentThread->TrapFrame;
    CalloutFrame->TrapFrame = (ULONG_PTR)TrapFrame;

    CurrentThread->CallbackStack = CalloutFrame;

    _disable();

    CurrentThread->InitialStack = (PVOID)InitialStack;

    CallbackTrapFrame = *TrapFrame;

    Pcr = (PKIPCR)KeGetPcr();
    if (Pcr != NULL)
    {
        Pcr->Prcb.SpBase = (PVOID)InitialStack;
    }

    CallbackTrapFrame.Pc = (ULONG_PTR)UserCallbackDispatcher;
    CallbackTrapFrame.Lr = (ULONG_PTR)UserCallbackDispatcher;
    CallbackTrapFrame.X18 = (ULONG_PTR)CurrentThread->Teb;

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

        /*
         * ARM64 ntdll currently uses the generic C callback dispatcher, which
         * receives its arguments in x0-x2. The stack UCALLOUT_FRAME is still
         * kept for NtCallbackReturn and debugger/unwind state, but unlike
         * amd64 there is no assembly dispatcher reading arguments from it.
         */
        KeGetCurrentThread()->TrapFrame->X0 = RoutineIndex;
        KeGetCurrentThread()->TrapFrame->X1 = (ULONG_PTR)UserArguments;
        KeGetCurrentThread()->TrapFrame->X2 = ArgumentLength;
        KeGetCurrentThread()->TrapFrame->X18 = (ULONG_PTR)Teb;

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
        Pcr->Prcb.SpBase = (PVOID)CalloutFrame->CalloutInitialStack;
    }

    CurrentThread->InitialStack = (PVOID)CalloutFrame->CalloutInitialStack;
    CurrentThread->TrapFrame = TrapFrame;
    CurrentThread->CallbackStack = (PVOID)CalloutFrame->CalloutCallbackStack;

    _enable();

    KiCallbackReturn(CalloutFrame, CallbackStatus);
}
