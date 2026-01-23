/*
 * PROJECT:         ReactOS Run-Time Library
 * LICENSE:         MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:         Minimal exception helpers for ARM64 user-mode runtime
 */

#include <rtl.h>
#include <setjmp.h>

#define NDEBUG
#include <debug.h>

BOOLEAN
NTAPI
RtlpUnwindInternal(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable,
    _In_ ULONG Flags);

VOID
NTAPI
RtlInitializeContext(
    _Reserved_ HANDLE ProcessHandle,
    _Out_ PCONTEXT ThreadContext,
    _In_opt_ PVOID ThreadStartParam,
    _In_ PTHREAD_START_ROUTINE ThreadStartAddress,
    _In_ PINITIAL_TEB StackBase)
{
    /*
     * Note: Despite the PINITIAL_TEB type, the caller passes InitialTeb.StackBase
     * (a PVOID) directly. This is a historical API quirk shared across all
     * architectures - the parameter is actually the stack base address, not
     * a pointer to an INITIAL_TEB structure.
     */
    UNREFERENCED_PARAMETER(ProcessHandle);

    RtlZeroMemory(ThreadContext, sizeof(*ThreadContext));

    ThreadContext->ContextFlags = CONTEXT_ARM64 | CONTEXT_CONTROL | CONTEXT_INTEGER;
    ThreadContext->Pc = (DWORD64)(ULONG_PTR)ThreadStartAddress;
    /* ARM64 requires 16-byte stack alignment */
    ThreadContext->Sp = (DWORD64)(ULONG_PTR)StackBase & ~(DWORD64)15;
    ThreadContext->X[0] = (DWORD64)(ULONG_PTR)ThreadStartParam;
    /* EL0t, interrupts enabled, NZCV = 0 */
    ThreadContext->Cpsr = 0;
}

BOOLEAN
NTAPI
RtlDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT ContextRecord)
{
    BOOLEAN Handled;

    if (RtlCallVectoredExceptionHandlers(ExceptionRecord, ContextRecord))
    {
        RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
        return TRUE;
    }

    Handled = RtlpUnwindInternal(NULL,
                                 NULL,
                                 ExceptionRecord,
                                 0,
                                 ContextRecord,
                                 NULL,
                                 UNW_FLAG_EHANDLER);

    RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);

    return Handled;
}

VOID
CDECL
RtlRestoreContext(
    _Inout_ PCONTEXT ContextRecord,
    _Inout_opt_ PEXCEPTION_RECORD ExceptionRecord)
{
    NTSTATUS Status;

    if (ExceptionRecord != NULL)
    {
        if ((ExceptionRecord->ExceptionCode == STATUS_UNWIND_CONSOLIDATE) &&
            (ExceptionRecord->NumberParameters >= 1))
        {
            PVOID (*Consolidate)(PEXCEPTION_RECORD) =
                (PVOID)ExceptionRecord->ExceptionInformation[0];

            ContextRecord->Pc = (ULONG64)Consolidate(ExceptionRecord);
        }
        else if ((ExceptionRecord->ExceptionCode == STATUS_LONGJUMP) &&
                 (ExceptionRecord->NumberParameters >= 1))
        {
            const _JUMP_BUFFER *JumpBuffer =
                (const _JUMP_BUFFER *)ExceptionRecord->ExceptionInformation[0];

            ContextRecord->X19 = JumpBuffer->X19;
            ContextRecord->X20 = JumpBuffer->X20;
            ContextRecord->X21 = JumpBuffer->X21;
            ContextRecord->X22 = JumpBuffer->X22;
            ContextRecord->X23 = JumpBuffer->X23;
            ContextRecord->X24 = JumpBuffer->X24;
            ContextRecord->X25 = JumpBuffer->X25;
            ContextRecord->X26 = JumpBuffer->X26;
            ContextRecord->X27 = JumpBuffer->X27;
            ContextRecord->X28 = JumpBuffer->X28;
            ContextRecord->Fp = JumpBuffer->Fp;
            ContextRecord->Lr = JumpBuffer->Lr;
            ContextRecord->Sp = JumpBuffer->Sp;
            ContextRecord->Pc = JumpBuffer->Lr;
            ContextRecord->Fpcr = JumpBuffer->Fpcr;
            ContextRecord->Fpsr = JumpBuffer->Fpsr;

            ContextRecord->V[8].D[0] = JumpBuffer->D[0];
            ContextRecord->V[9].D[0] = JumpBuffer->D[1];
            ContextRecord->V[10].D[0] = JumpBuffer->D[2];
            ContextRecord->V[11].D[0] = JumpBuffer->D[3];
            ContextRecord->V[12].D[0] = JumpBuffer->D[4];
            ContextRecord->V[13].D[0] = JumpBuffer->D[5];
            ContextRecord->V[14].D[0] = JumpBuffer->D[6];
            ContextRecord->V[15].D[0] = JumpBuffer->D[7];
        }
    }

    Status = NtContinue(ContextRecord, FALSE);
    RtlRaiseStatus(Status);
}

/* RtlUnwind is provided by unwind.c for ARM64 */
