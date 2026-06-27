/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     ARM64EC C specific exception handler
 */

#include <ntdllp.h>

#define NDEBUG
#include <debug.h>

typedef VOID (__cdecl *PTERMINATION_HANDLER)(BOOLEAN AbnormalTermination,
                                             PVOID EstablisherFrame);
typedef LONG (__cdecl *PEXCEPTION_FILTER_ROUTINE)(PEXCEPTION_POINTERS ExceptionPointers,
                                                  PVOID EstablisherFrame);

static
LONG
__attribute__((naked, used))
ChpepExecuteArm64Filter(
    _In_opt_ PVOID Argument,
    _In_ PVOID EstablisherFrame,
    _In_ PEXCEPTION_FILTER_ROUTINE Filter,
    _In_ PUCHAR NonVolatileRegisters)
{
    __asm__(
        ".seh_proc \"#ChpepExecuteArm64Filter\"\n\t"
        "stp x29, x30, [sp, #-80]!\n\t"
        ".seh_save_fplr_x 80\n\t"
        "stp x19, x20, [sp, #16]\n\t"
        ".seh_save_regp x19, 16\n\t"
        "stp x21, x22, [sp, #32]\n\t"
        ".seh_save_regp x21, 32\n\t"
        "stp x25, x26, [sp, #48]\n\t"
        ".seh_save_regp x25, 48\n\t"
        "str x27, [sp, #64]\n\t"
        ".seh_save_reg x27, 64\n\t"
        ".seh_endprologue\n\t"
        "ldp x19, x20, [x3, #0]\n\t"
        "ldp x21, x22, [x3, #16]\n\t"
        "ldp x25, x26, [x3, #48]\n\t"
        "ldr x27, [x3, #64]\n\t"
        "ldr x1, [x3, #80]\n\t"
        "blr x2\n\t"
        "ldp x19, x20, [sp, #16]\n\t"
        "ldp x21, x22, [sp, #32]\n\t"
        "ldp x25, x26, [sp, #48]\n\t"
        "ldr x27, [sp, #64]\n\t"
        "ldp x29, x30, [sp], #80\n\t"
        "ret\n\t"
        ".seh_endproc");
}

VOID
__cdecl
_local_unwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp)
{
    RtlUnwind(TargetFrame, TargetIp, NULL, 0);
}

static
EXCEPTION_DISPOSITION
__cdecl
ChpepCSpecificHandlerArm64(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PARM64_NT_CONTEXT ContextRecord,
    _Inout_ PDISPATCHER_CONTEXT_ARM64 DispatcherContext)
{
    PSCOPE_TABLE ScopeTable;
    EXCEPTION_POINTERS ExceptionPointers;
    ULONG_PTR ImageBase;
    ULONG_PTR ControlPc;
    ULONG_PTR TargetPc;
    ULONG Index;

    ScopeTable = DispatcherContext->HandlerData;
    if (ScopeTable == NULL)
        return ExceptionContinueSearch;

    ImageBase = DispatcherContext->ImageBase;
    ControlPc = DispatcherContext->ControlPc;
    TargetPc = DispatcherContext->TargetPc;

    if (DispatcherContext->ControlPcIsUnwound)
        ControlPc -= sizeof(ULONG);

    ExceptionPointers.ExceptionRecord = ExceptionRecord;
    ExceptionPointers.ContextRecord = (PCONTEXT)ContextRecord;

    for (Index = DispatcherContext->ScopeIndex; Index < ScopeTable->Count; Index++)
    {
        PVOID Handler;

        if (ControlPc < ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress)
            continue;
        if (ControlPc >= ImageBase + ScopeTable->ScopeRecord[Index].EndAddress)
            continue;

        DispatcherContext->ScopeIndex = Index + 1;

        if (ExceptionRecord->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        {
            if (ScopeTable->ScopeRecord[Index].JumpTarget != 0)
                continue;

            if ((ExceptionRecord->ExceptionFlags & EXCEPTION_TARGET_UNWIND) &&
                (TargetPc >= ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress) &&
                (TargetPc < ImageBase + ScopeTable->ScopeRecord[Index].EndAddress))
            {
                return ExceptionContinueSearch;
            }

            Handler = (PVOID)(ImageBase + ScopeTable->ScopeRecord[Index].HandlerAddress);
            ChpepExecuteArm64Filter(ULongToPtr(TRUE),
                                    EstablisherFrame,
                                    Handler,
                                    DispatcherContext->NonVolatileRegisters);
            continue;
        }

        if (ScopeTable->ScopeRecord[Index].JumpTarget == 0)
            continue;

        Handler = (PVOID)(ImageBase + ScopeTable->ScopeRecord[Index].HandlerAddress);
        if (Handler != ULongToPtr(EXCEPTION_EXECUTE_HANDLER))
        {
            LONG FilterResult;

            FilterResult = ChpepExecuteArm64Filter(&ExceptionPointers,
                                                   EstablisherFrame,
                                                   Handler,
                                                   DispatcherContext->NonVolatileRegisters);
            if (FilterResult < 0)
                return ExceptionContinueExecution;
            if (FilterResult == 0)
                continue;
        }

        RtlUnwindEx(EstablisherFrame,
                    (PVOID)(ImageBase + ScopeTable->ScopeRecord[Index].JumpTarget),
                    ExceptionRecord,
                    UlongToPtr(ExceptionRecord->ExceptionCode),
                    (PCONTEXT)DispatcherContext->ContextRecord,
                    DispatcherContext->HistoryTable);
    }

    return ExceptionContinueSearch;
}

EXCEPTION_DISPOSITION
__cdecl
__C_specific_handler(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _Inout_ PDISPATCHER_CONTEXT DispatcherContext)
{
    PSCOPE_TABLE ScopeTable;
    EXCEPTION_POINTERS ExceptionPointers;
    ULONG_PTR ImageBase;
    ULONG_PTR ControlPc;
    ULONG_PTR TargetIp;
    ULONG Index;

    if (RtlIsEcCode(DispatcherContext->ControlPc))
    {
        return ChpepCSpecificHandlerArm64(ExceptionRecord,
                                          EstablisherFrame,
                                          (PARM64_NT_CONTEXT)ContextRecord,
                                          (PDISPATCHER_CONTEXT_ARM64)DispatcherContext);
    }

    ScopeTable = DispatcherContext->HandlerData;
    if (ScopeTable == NULL)
        return ExceptionContinueSearch;

    ImageBase = DispatcherContext->ImageBase;
    ControlPc = DispatcherContext->ControlPc;
    TargetIp = DispatcherContext->TargetIp;

    ExceptionPointers.ExceptionRecord = ExceptionRecord;
    ExceptionPointers.ContextRecord = ContextRecord;

    for (Index = DispatcherContext->ScopeIndex; Index < ScopeTable->Count; Index++)
    {
        PVOID Handler;

        if (ControlPc < ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress)
            continue;
        if (ControlPc >= ImageBase + ScopeTable->ScopeRecord[Index].EndAddress)
            continue;

        DispatcherContext->ScopeIndex = Index + 1;

        if (ExceptionRecord->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        {
            if (ScopeTable->ScopeRecord[Index].JumpTarget != 0)
                continue;

            if ((ExceptionRecord->ExceptionFlags & EXCEPTION_TARGET_UNWIND) &&
                (TargetIp >= ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress) &&
                (TargetIp < ImageBase + ScopeTable->ScopeRecord[Index].EndAddress))
            {
                return ExceptionContinueSearch;
            }

            Handler = (PVOID)(ImageBase + ScopeTable->ScopeRecord[Index].HandlerAddress);
            ((PTERMINATION_HANDLER)Handler)(TRUE, EstablisherFrame);
            continue;
        }

        if (ScopeTable->ScopeRecord[Index].JumpTarget == 0)
            continue;

        Handler = (PVOID)(ImageBase + ScopeTable->ScopeRecord[Index].HandlerAddress);
        if (Handler != ULongToPtr(EXCEPTION_EXECUTE_HANDLER))
        {
            LONG FilterResult;

            FilterResult = ((PEXCEPTION_FILTER_ROUTINE)Handler)(&ExceptionPointers,
                                                                EstablisherFrame);
            if (FilterResult < 0)
                return ExceptionContinueExecution;
            if (FilterResult == 0)
                continue;
        }

        RtlUnwindEx(EstablisherFrame,
                    (PVOID)(ImageBase + ScopeTable->ScopeRecord[Index].JumpTarget),
                    ExceptionRecord,
                    UlongToPtr(ExceptionRecord->ExceptionCode),
                    DispatcherContext->ContextRecord,
                    DispatcherContext->HistoryTable);
    }

    return ExceptionContinueSearch;
}
