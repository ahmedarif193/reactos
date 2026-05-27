/*
 * PROJECT:     ReactOS NTDLL ARM64
 * LICENSE:     MIT
 * PURPOSE:     C specific exception/unwind handler for ARM64 SEH metadata
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

typedef struct _ARM64_SCOPE_TABLE
{
    ULONG Count;
    struct
    {
        ULONG BeginAddress;
        ULONG EndAddress;
        ULONG HandlerAddress;
        ULONG JumpTarget;
    } ScopeRecord[1];
} ARM64_SCOPE_TABLE, *PARM64_SCOPE_TABLE;

typedef struct _ARM64_DISPATCHER_CONTEXT
{
    ULONG_PTR ControlPc;
    ULONG_PTR ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG_PTR EstablisherFrame;
    ULONG_PTR TargetPc;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    struct _UNWIND_HISTORY_TABLE *HistoryTable;
    ULONG ScopeIndex;
    BOOLEAN ControlPcIsUnwound;
    PUCHAR NonVolatileRegisters;
} ARM64_DISPATCHER_CONTEXT, *PARM64_DISPATCHER_CONTEXT;

typedef EXCEPTION_DISPOSITION (__cdecl *PTERMINATION_HANDLER)(BOOLEAN, PVOID);
typedef EXCEPTION_DISPOSITION (__cdecl *PEXCEPTION_FILTER)(PEXCEPTION_POINTERS, PVOID);

NTSYSAPI
VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _Inout_opt_ PVOID HistoryTable);

EXCEPTION_DISPOSITION
__cdecl
__C_specific_handler(
    _In_ struct _EXCEPTION_RECORD *ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ struct _CONTEXT *ContextRecord,
    _Inout_ struct _DISPATCHER_CONTEXT *DispatcherContext)
{
    PARM64_SCOPE_TABLE ScopeTable;
    PARM64_DISPATCHER_CONTEXT Arm64DispatcherContext;
    EXCEPTION_POINTERS ExceptionPointers;
    ULONG Index;
    ULONG BeginAddress;
    ULONG EndAddress;
    ULONG HandlerAddress;
    ULONG64 ImageBase;
    ULONG64 IpOffset;
    ULONG64 TargetIpOffset;
    ULONG64 JumpTarget;
    PTERMINATION_HANDLER TerminationHandler;
    PEXCEPTION_FILTER ExceptionFilter;
    LONG FilterResult;

    Arm64DispatcherContext = (PARM64_DISPATCHER_CONTEXT)DispatcherContext;

    if ((ExceptionRecord == NULL) ||
        (ContextRecord == NULL) ||
        (Arm64DispatcherContext == NULL) ||
        (Arm64DispatcherContext->HandlerData == NULL))
    {
        return ExceptionContinueSearch;
    }

    ImageBase = Arm64DispatcherContext->ImageBase;
    IpOffset = Arm64DispatcherContext->ControlPc - ImageBase;
    TargetIpOffset = Arm64DispatcherContext->TargetPc - ImageBase;
    ScopeTable = (PARM64_SCOPE_TABLE)Arm64DispatcherContext->HandlerData;

    ExceptionPointers.ExceptionRecord = ExceptionRecord;
    ExceptionPointers.ContextRecord = ContextRecord;

    while (Arm64DispatcherContext->ScopeIndex < ScopeTable->Count)
    {
        Index = Arm64DispatcherContext->ScopeIndex++;
        BeginAddress = ScopeTable->ScopeRecord[Index].BeginAddress;
        EndAddress = ScopeTable->ScopeRecord[Index].EndAddress;

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

            if (ScopeTable->ScopeRecord[Index].JumpTarget == 0)
            {
                HandlerAddress = ScopeTable->ScopeRecord[Index].HandlerAddress;
                TerminationHandler = (PTERMINATION_HANDLER)(ImageBase + HandlerAddress);
                TerminationHandler(TRUE, EstablisherFrame);
            }
            else if (ScopeTable->ScopeRecord[Index].JumpTarget == TargetIpOffset)
            {
                return ExceptionContinueSearch;
            }
        }
        else
        {
            if (ScopeTable->ScopeRecord[Index].JumpTarget == 0)
            {
                continue;
            }

            HandlerAddress = ScopeTable->ScopeRecord[Index].HandlerAddress;
            if (HandlerAddress == EXCEPTION_EXECUTE_HANDLER)
            {
                FilterResult = EXCEPTION_EXECUTE_HANDLER;
            }
            else
            {
                ExceptionFilter = (PEXCEPTION_FILTER)(ImageBase + HandlerAddress);
                FilterResult = ExceptionFilter(&ExceptionPointers, EstablisherFrame);
            }

            if (FilterResult < 0)
            {
                return ExceptionContinueExecution;
            }

            if (FilterResult > 0)
            {
                JumpTarget = ImageBase + ScopeTable->ScopeRecord[Index].JumpTarget;

                RtlUnwindEx(EstablisherFrame,
                            (PVOID)JumpTarget,
                            ExceptionRecord,
                            UlongToPtr(ExceptionRecord->ExceptionCode),
                            Arm64DispatcherContext->ContextRecord,
                            Arm64DispatcherContext->HistoryTable);

                /*
                 * The ARM64 user-mode unwinder currently returns after target
                 * unwind. Resume at the selected handler explicitly.
                 */
                Arm64DispatcherContext->ContextRecord->Pc = JumpTarget;
                Arm64DispatcherContext->ContextRecord->X0 = ExceptionRecord->ExceptionCode;
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
