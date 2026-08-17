/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable AMD64-on-ARM64 context, unwind, and fiber parity probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>

#define TEST_EXCEPTION ((DWORD)0xe0424348)

static jmp_buf JumpBuffer;
static PVOID MainFiber;
static volatile LONG FiberStage;
static volatile LONG FinallyStage;

static int
log_result(PCSTR Format, ...)
{
    CHAR Buffer[512];
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    va_end(Arguments);

    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    fputs(Buffer, stdout);
    OutputDebugStringA(Buffer);
    return Length;
}

#define printf log_result

static DECLSPEC_NOINLINE VOID
jump_from_nested_frame(INT Value)
{
    volatile ULONG_PTR StackNoise[32];

    StackNoise[0] = (ULONG_PTR)&StackNoise;
    longjmp(JumpBuffer, Value);
}

static INT
test_setjmp(VOID)
{
    volatile LONG Stage = 0;
    INT Value;

    Value = setjmp(JumpBuffer);
    if (Stage == 0)
    {
        if (Value != 0)
            return 1;
        Stage = 1;
        jump_from_nested_frame(0x12345678);
        return 2;
    }

    if (Stage != 1 || Value != 0x12345678)
        return 3;

    Stage = 2;
    Value = setjmp(JumpBuffer);
    if (Stage == 2)
    {
        if (Value != 0)
            return 4;
        Stage = 3;
        jump_from_nested_frame(0);
        return 5;
    }

    return (Stage == 3 && Value == 1) ? 0 : 6;
}

static INT
test_seh(VOID)
{
    volatile LONG HandlerStage = 0;

    __try
    {
        __try
        {
            RaiseException(TEST_EXCEPTION, 0, 0, NULL);
        }
        __finally
        {
            FinallyStage = AbnormalTermination() ? 1 : -1;
        }
    }
    __except (GetExceptionCode() == TEST_EXCEPTION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        HandlerStage = 1;
    }

    return (HandlerStage == 1 && FinallyStage == 1) ? 0 : 1;
}

static VOID WINAPI
fiber_entry(PVOID Parameter)
{
    volatile ULONG_PTR StackNoise[64];

    StackNoise[0] = (ULONG_PTR)Parameter;
    FiberStage = StackNoise[0] == 0xfeedbeef ? 1 : -1;
    SwitchToFiber(MainFiber);
    FiberStage = 2;
    SwitchToFiber(MainFiber);
}

static INT
test_fibers(VOID)
{
    PVOID TestFiber;

    MainFiber = ConvertThreadToFiber(NULL);
    if (!MainFiber && GetLastError() != ERROR_ALREADY_FIBER)
        return 1;

    TestFiber = CreateFiber(0, fiber_entry, (PVOID)(ULONG_PTR)0xfeedbeef);
    if (!TestFiber)
        return 2;

    SwitchToFiber(TestFiber);
    if (FiberStage != 1)
    {
        DeleteFiber(TestFiber);
        return 3;
    }

    SwitchToFiber(TestFiber);
    if (FiberStage != 2)
    {
        DeleteFiber(TestFiber);
        return 4;
    }

    DeleteFiber(TestFiber);
    if (!ConvertFiberToThread())
        return 5;

    return 0;
}

int
main(void)
{
    CONTEXT Context;
    ULONG_PTR StackMarker;
    INT CaptureResult, SetjmpResult, SehResult, FiberResult;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_CONTEXT_UNWIND_BEGIN\n");

    RtlZeroMemory(&Context, sizeof(Context));
    RtlCaptureContext(&Context);
    StackMarker = (ULONG_PTR)&Context;
    CaptureResult = ((Context.ContextFlags & CONTEXT_FULL) == CONTEXT_FULL && Context.Rip != 0 && Context.Rsp != 0 && Context.Rsp <= StackMarker + 0x10000 && Context.Rsp >= StackMarker - 0x10000) ? 0 : 1;
    printf("CAPTURE flags=0x%08lx rip=%p rsp=%p result=%d\n", Context.ContextFlags, (PVOID)(ULONG_PTR)Context.Rip, (PVOID)(ULONG_PTR)Context.Rsp, CaptureResult);

    SetjmpResult = test_setjmp();
    printf("SETJMP result=%d\n", SetjmpResult);

    SehResult = test_seh();
    printf("SEH finally=%ld result=%d\n", FinallyStage, SehResult);

    FiberResult = test_fibers();
    printf("FIBER stage=%ld result=%d\n", FiberStage, FiberResult);

    if (CaptureResult || SetjmpResult || SehResult || FiberResult)
    {
        printf("CHPE_CONTEXT_UNWIND_FAIL capture=%d setjmp=%d seh=%d fiber=%d\n", CaptureResult, SetjmpResult, SehResult, FiberResult);
        return 1;
    }

    printf("CHPE_CONTEXT_UNWIND_PASS\n");
    return 0;
}
