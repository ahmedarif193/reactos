/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Exception test
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#define KMT_EMULATE_KERNEL
#include <kmt_test.h>

#ifdef KMT_USER_MODE

#define VECTORED_TEST_EXCEPTION ((NTSTATUS)0xE0425051)
static ULONG VectoredTrace;
static BOOLEAN VectoredContinue;

static LONG NTAPI
FirstVectoredHandler(PEXCEPTION_POINTERS Pointers)
{
    if (Pointers->ExceptionRecord->ExceptionCode == VECTORED_TEST_EXCEPTION)
        VectoredTrace = (VectoredTrace << 4) | 1;
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG NTAPI
SecondVectoredHandler(PEXCEPTION_POINTERS Pointers)
{
    if (Pointers->ExceptionRecord->ExceptionCode != VECTORED_TEST_EXCEPTION)
        return EXCEPTION_CONTINUE_SEARCH;
    VectoredTrace = (VectoredTrace << 4) | 2;
    return VectoredContinue ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

static LONG NTAPI
VectoredContinueHandler(PEXCEPTION_POINTERS Pointers)
{
    if (Pointers->ExceptionRecord->ExceptionCode == VECTORED_TEST_EXCEPTION)
        VectoredTrace = (VectoredTrace << 4) | 3;
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG
ContinueExceptionFilter(NTSTATUS Code)
{
    if (Code != VECTORED_TEST_EXCEPTION)
        return EXCEPTION_EXECUTE_HANDLER;
    VectoredTrace = (VectoredTrace << 4) | 4;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static VOID
TestVectoredHandlers(VOID)
{
    EXCEPTION_RECORD Record = {0};
    PVOID First, Second, Continue;

    Record.ExceptionCode = VECTORED_TEST_EXCEPTION;
    VectoredContinue = TRUE;
    First = RtlAddVectoredExceptionHandler(1, FirstVectoredHandler);
    Second = RtlAddVectoredExceptionHandler(0, SecondVectoredHandler);
    Continue = RtlAddVectoredContinueHandler(1, VectoredContinueHandler);
    ok(First != NULL, "Failed to register first vectored handler\n");
    ok(Second != NULL, "Failed to register second vectored handler\n");
    ok(Continue != NULL, "Failed to register vectored continue handler\n");
    if (!First || !Second || !Continue)
        goto Cleanup;

    /* Vectored handlers run in registration order before frame handlers. */
    VectoredTrace = 0;
    KmtStartSeh()
        RtlRaiseException(&Record);
    KmtEndSeh(STATUS_SUCCESS);
    ok_eq_hex(VectoredTrace, 0x123);

    /* Continuing in a frame filter still invokes the continue handlers. */
    VectoredContinue = FALSE;
    VectoredTrace = 0;
    Record.ExceptionFlags = 0;
    _SEH2_TRY
    {
        RtlRaiseException(&Record);
    }
    _SEH2_EXCEPT(ContinueExceptionFilter(_SEH2_GetExceptionCode()))
    {
        ok(FALSE, "Unexpected exception %08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
    ok_eq_hex(VectoredTrace, 0x1243);

    ok_eq_ulong(RtlRemoveVectoredExceptionHandler(First), 1);
    First = NULL;
    VectoredContinue = TRUE;
    VectoredTrace = 0;
    Record.ExceptionFlags = 0;
    KmtStartSeh()
        RtlRaiseException(&Record);
    KmtEndSeh(STATUS_SUCCESS);
    ok_eq_hex(VectoredTrace, 0x23);

Cleanup:
    if (First) ok_eq_ulong(RtlRemoveVectoredExceptionHandler(First), 1);
    if (Second) ok_eq_ulong(RtlRemoveVectoredExceptionHandler(Second), 1);
    if (Continue) ok_eq_ulong(RtlRemoveVectoredContinueHandler(Continue), 1);
}

#endif

START_TEST(RtlException)
{
    PCHAR Buffer[128];

#ifdef KMT_USER_MODE
    TestVectoredHandlers();
#endif

    /* Access a valid pointer - must not trigger SEH */
    KmtStartSeh()
        RtlFillMemory(Buffer, sizeof(Buffer), 0x12);
    KmtEndSeh(STATUS_SUCCESS);

    /* Read from a NULL pointer - must cause an access violation */
    KmtStartSeh()
        (void)*(volatile CHAR *)NULL;
    KmtEndSeh(STATUS_ACCESS_VIOLATION);

    /* Write to a NULL pointer - must cause an access violation */
    KmtStartSeh()
        *(volatile CHAR *)NULL = 5;
    KmtEndSeh(STATUS_ACCESS_VIOLATION);

    /* TODO: Find where MmBadPointer is defined - gives an unresolved external */
#if 0 //def KMT_KERNEL_MODE
    /* Read from MmBadPointer - must cause an access violation */
    KmtStartSeh()
        (void)*(volatile CHAR *)MmBadPointer;
    KmtEndSeh(STATUS_ACCESS_VIOLATION);

    /* Write to MmBadPointer - must cause an access violation */
    KmtStartSeh()
        *(volatile CHAR *)MmBadPointer = 5;
    KmtEndSeh(STATUS_ACCESS_VIOLATION);
#endif

    KmtStartSeh()
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    KmtEndSeh(STATUS_ACCESS_VIOLATION);

    KmtStartSeh()
        ExRaiseStatus(STATUS_TIMEOUT);
    KmtEndSeh(STATUS_TIMEOUT);

    KmtStartSeh()
        ExRaiseStatus(STATUS_STACK_OVERFLOW);
    KmtEndSeh(STATUS_STACK_OVERFLOW);

    KmtStartSeh()
        ExRaiseStatus(STATUS_GUARD_PAGE_VIOLATION);
    KmtEndSeh(STATUS_GUARD_PAGE_VIOLATION);

    /* Nested _SEH2 unwinding is compiler/runtime behavior, not an NT API contract. */

    /* We cannot test this in kernel mode easily - the stack is just "somewhere"
     * in system space, and there's no guard page below it */
#ifdef KMT_USER_MODE
    /* Overflow the stack - must cause a special exception */
    KmtStartSeh()
        volatile CHAR *Pointer;

        while (1)
        {
            Pointer = _alloca(1024);
            *Pointer = 5;
        }
    KmtEndSeh(STATUS_STACK_OVERFLOW);
#endif
}
