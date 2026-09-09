/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Preserve the current ARM64 PCR across context restoration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _CONTEXT_PCR_RESULT
{
    ULONG64 Captured;
    ULONG64 Expected;
    ULONG64 Restored;
    ULONG64 Current;
} CONTEXT_PCR_RESULT;

/* Offsets used by RtlArm64Context-asm.S. */
C_ASSERT(FIELD_OFFSET(CONTEXT_PCR_RESULT, Captured) == 0);
C_ASSERT(FIELD_OFFSET(CONTEXT_PCR_RESULT, Expected) == 8);
C_ASSERT(FIELD_OFFSET(CONTEXT_PCR_RESULT, Restored) == 16);
C_ASSERT(FIELD_OFFSET(CONTEXT_PCR_RESULT, Current) == 24);

VOID RtlArm64ContextRestore(CONTEXT_PCR_RESULT *Result, KAFFINITY Affinity);
VOID RtlArm64ContextReadAndRepair(CONTEXT_PCR_RESULT *Result);
ULONG64 RtlArm64ContextReadPcr(VOID);

static VOID CheckPcr(const CONTEXT_PCR_RESULT *Result)
{
    trace("captured=%I64x expected=%I64x restored=%I64x current=%I64x\n", Result->Captured, Result->Expected, Result->Restored, Result->Current);
    ok(Result->Captured != Result->Expected, "The test did not migrate to another processor\n");
    ok_eq_ulonglong(Result->Expected, Result->Current);
    ok_eq_ulonglong(Result->Restored, Result->Expected);
}

START_TEST(RtlArm64Context)
{
    KAFFINITY Active = KeQueryActiveProcessors();
    KAFFINITY First = Active & -Active;
    KAFFINITY Other = Active ^ First;
    KAFFINITY Second = Other & -Other;
    KAFFINITY Previous;
    CONTEXT_PCR_RESULT Direct = {0};
    CONTEXT_PCR_RESULT Unwind = {0};
    volatile NTSTATUS Status = STATUS_SUCCESS;

    if (skip(Second != 0, "At least two processors are required\n"))
        return;

    Previous = KeSetSystemAffinityThreadEx(First);
    RtlArm64ContextRestore(&Direct, Second);
    KeSetSystemAffinityThreadEx(First);

    /* The finally handler migrates after the unwinder captured its context. */
    Unwind.Captured = RtlArm64ContextReadPcr();
    _SEH2_TRY
    {
        _SEH2_TRY
        {
            ExRaiseStatus(STATUS_SEMAPHORE_LIMIT_EXCEEDED);
        }
        _SEH2_FINALLY
        {
            KeSetSystemAffinityThreadEx(Second);
            Unwind.Expected = RtlArm64ContextReadPcr();
        }
        _SEH2_END;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        RtlArm64ContextReadAndRepair(&Unwind);
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    KeRevertToUserAffinityThreadEx(Previous);
    CheckPcr(&Direct);
    CheckPcr(&Unwind);
    ok_eq_hex(Status, STATUS_SEMAPHORE_LIMIT_EXCEEDED);
}
