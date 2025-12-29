/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     C specific exception/unwind handler for ARM64
 */

#include <precomp.h>
#include <winnt.h>

extern LONG *__seh_get_abnormal_termination_flag_pointer(VOID);

typedef EXCEPTION_DISPOSITION (__cdecl *PTERMINATION_HANDLER)(BOOLEAN, PVOID);
typedef EXCEPTION_DISPOSITION (__cdecl *PEXCEPTION_FILTER)(PEXCEPTION_POINTERS, PVOID);

VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_opt_ PCONTEXT ContextRecord,
    _In_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable);

_CRTIMP
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
                    (TargetIpOffset <  EndAddress))
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
                JumpTarget = (ImageBase + ScopeTable->ScopeRecord[i].JumpTarget);

                RtlUnwindEx(EstablisherFrame,
                            (PVOID)JumpTarget,
                            ExceptionRecord,
                            UlongToPtr(ExceptionRecord->ExceptionCode),
                            DispatcherContext->ContextRecord,
                            DispatcherContext->HistoryTable);

                return ExceptionContinueSearch;
            }
        }
    }

    return ExceptionContinueSearch;
}

void __cdecl _local_unwind(void* frame, void* target)
{
    RtlUnwind(frame, target, NULL, 0);
}
