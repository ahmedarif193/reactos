/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         C specific exception handler for SEH metadata
 */

#include <ntoskrnl.h>
#include <ndk/rtltypes.h>

typedef struct _RUNTIME_FUNCTION {
    ULONG BeginAddress;
    ULONG UnwindData;
} RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

#define NDEBUG
#include <debug.h>
#include <debug.h>

#ifndef _ARM64_DISPATCHER_CONTEXT_DEFINED
#define _ARM64_DISPATCHER_CONTEXT_DEFINED 1

typedef struct _SCOPE_TABLE_ARM64 {
    DWORD Count;
    struct
    {
        DWORD BeginAddress;
        DWORD EndAddress;
        DWORD HandlerAddress;
        DWORD JumpTarget;
    } ScopeRecord[1];
} SCOPE_TABLE_ARM64, *PSCOPE_TABLE_ARM64;

typedef SCOPE_TABLE_ARM64 SCOPE_TABLE, *PSCOPE_TABLE;

typedef struct _DISPATCHER_CONTEXT {
    ULONG_PTR ControlPc;
    ULONG_PTR ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG_PTR EstablisherFrame;
    ULONG_PTR TargetPc;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    struct _UNWIND_HISTORY_TABLE *HistoryTable;
    DWORD ScopeIndex;
    BOOLEAN ControlPcIsUnwound;
    PUCHAR NonVolatileRegisters;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;
#endif

NTSYSAPI
VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_opt_ PCONTEXT ContextRecord,
    _In_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable);

typedef EXCEPTION_DISPOSITION (__cdecl *PTERMINATION_HANDLER)(BOOLEAN, PVOID);
typedef EXCEPTION_DISPOSITION (__cdecl *PEXCEPTION_FILTER)(PEXCEPTION_POINTERS, PVOID);

static LONG KiSehAbnormalTerminationFlag[MAXIMUM_PROCESSORS];

LONG *
__cdecl
__seh_get_abnormal_termination_flag_pointer(VOID)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();
    if (Cpu < MAXIMUM_PROCESSORS)
    {
        return &KiSehAbnormalTerminationFlag[Cpu];
    }

    /* Fallback to CPU0 slot if the CPU number is out of range. */
    return &KiSehAbnormalTerminationFlag[0];
}

EXCEPTION_DISPOSITION
__cdecl
__C_specific_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    struct _CONTEXT *ContextRecord,
    struct _DISPATCHER_CONTEXT *DispatcherContext)
{
    PSCOPE_TABLE ScopeTable;
    ULONG i, BeginAddress, EndAddress, Handler;
    ULONG64 ImageBase, JumpTarget, IpOffset, TargetIpOffset;
    EXCEPTION_POINTERS ExceptionPointers;
    LONG PreviousAbnormalState;
    LONG *AbnormalFlagPtr;
    PTERMINATION_HANDLER TerminationHandler;
    PEXCEPTION_FILTER ExceptionFilter;
    LONG FilterResult;

    ExceptionPointers.ExceptionRecord = ExceptionRecord;
    ExceptionPointers.ContextRecord = ContextRecord;

    AbnormalFlagPtr = __seh_get_abnormal_termination_flag_pointer();
    ImageBase = (ULONG64)DispatcherContext->ImageBase;

    IpOffset = DispatcherContext->ControlPc - ImageBase;
    TargetIpOffset = DispatcherContext->TargetPc - ImageBase;
    ScopeTable = (PSCOPE_TABLE)DispatcherContext->HandlerData;

    if (ExceptionRecord->NumberParameters >= 2 &&
        ExceptionRecord->ExceptionInformation[1] == 0x42424242)
    {
        ULONG _si;
        DPRINT1("[SEHCSH] IpOff=%p ib=%p ctlpc=%p scopes=%lu hd=%p flags=%lx\n",
                (PVOID)IpOffset, (PVOID)ImageBase, (PVOID)DispatcherContext->ControlPc,
                ScopeTable->Count, ScopeTable, ExceptionRecord->ExceptionFlags);
        for (_si = 0; _si < ScopeTable->Count && _si < 8; _si++)
            DPRINT1("[SEHCSH]  [%lu] beg=%lx end=%lx h=%lx jt=%lx\n", _si,
                    ScopeTable->ScopeRecord[_si].BeginAddress,
                    ScopeTable->ScopeRecord[_si].EndAddress,
                    ScopeTable->ScopeRecord[_si].HandlerAddress,
                    ScopeTable->ScopeRecord[_si].JumpTarget);
    }

    while (DispatcherContext->ScopeIndex < ScopeTable->Count)
    {
        i = DispatcherContext->ScopeIndex++;

        BeginAddress = ScopeTable->ScopeRecord[i].BeginAddress;
        EndAddress = ScopeTable->ScopeRecord[i].EndAddress;

        if ((IpOffset < BeginAddress) || (IpOffset >= EndAddress))
        {
            continue;
        }

        if (ExceptionRecord->ExceptionFlags & EXCEPTION_UNWIND)
        {
            if (ExceptionRecord->ExceptionFlags & EXCEPTION_TARGET_UNWIND)
            {
                if ((TargetIpOffset >= BeginAddress) &&
                    (TargetIpOffset < EndAddress))
                {
                    return ExceptionContinueSearch;
                }
            }

            if (ScopeTable->ScopeRecord[i].JumpTarget == 0)
            {
                Handler = ScopeTable->ScopeRecord[i].HandlerAddress;
                TerminationHandler = (PTERMINATION_HANDLER)(ImageBase + Handler);

                PreviousAbnormalState = *AbnormalFlagPtr;
                *AbnormalFlagPtr = TRUE;
                TerminationHandler(TRUE, EstablisherFrame);
                *AbnormalFlagPtr = PreviousAbnormalState;
            }
            else if (ScopeTable->ScopeRecord[i].JumpTarget == TargetIpOffset)
            {
                return ExceptionContinueSearch;
            }
        }
        else
        {
            if (ScopeTable->ScopeRecord[i].JumpTarget == 0)
            {
                continue;
            }

            Handler = ScopeTable->ScopeRecord[i].HandlerAddress;

            if (Handler == EXCEPTION_EXECUTE_HANDLER)
            {
                FilterResult = EXCEPTION_EXECUTE_HANDLER;
            }
            else
            {
                ExceptionFilter = (PEXCEPTION_FILTER)(ImageBase + Handler);
                PreviousAbnormalState = *AbnormalFlagPtr;
                *AbnormalFlagPtr = FALSE;
                FilterResult = ExceptionFilter(&ExceptionPointers, EstablisherFrame);
                *AbnormalFlagPtr = PreviousAbnormalState;
            }

            if (FilterResult < 0)
            {
                return ExceptionContinueExecution;
            }

            if (FilterResult > 0)
            {
                JumpTarget = ImageBase + ScopeTable->ScopeRecord[i].JumpTarget;

                RtlUnwindEx(EstablisherFrame,
                            (PVOID)JumpTarget,
                            ExceptionRecord,
                            UlongToPtr(ExceptionRecord->ExceptionCode),
                            DispatcherContext->ContextRecord,
                            DispatcherContext->HistoryTable);

                DispatcherContext->ContextRecord->Pc = JumpTarget;
                DispatcherContext->ContextRecord->X[0] = ExceptionRecord->ExceptionCode;
                return ExceptionContinueExecution;
            }
        }
    }

    return ExceptionContinueSearch;
}

VOID
__cdecl
_local_unwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp)
{
    RtlUnwind(TargetFrame, TargetIp, NULL, 0);
}
