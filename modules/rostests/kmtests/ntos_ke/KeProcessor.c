/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Executive Regressions KM-Test
 * PROGRAMMER:      Aleksey Bragin <aleksey@reactos.org>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
CheckStallDuration(
    _In_z_ PCSTR Description,
    _In_ ULONG MicroSeconds,
    _In_ ULONG Iterations)
{
    ULONG i;
    LARGE_INTEGER TimeStart, TimeFinish;
    LONGLONG ExpectedMs, ElapsedMs;

    KeQuerySystemTime(&TimeStart);
    for (i = 0; i < Iterations; i++)
    {
        KeStallExecutionProcessor(MicroSeconds);
    }
    KeQuerySystemTime(&TimeFinish);

    ExpectedMs = ((LONGLONG)MicroSeconds * Iterations) / 1000;
    ElapsedMs = (TimeFinish.QuadPart - TimeStart.QuadPart) / 10000;
    ok(ElapsedMs >= ExpectedMs - 50,
       "%s returned too early: %I64d ms, expected at least %I64d ms\n",
       Description, ElapsedMs, ExpectedMs - 50);
}

static VOID KeStallExecutionProcessorTest(VOID)
{
    CheckStallDuration("50us stalls", 50, 10000);
    CheckStallDuration("1000us stalls", 1000, 500);
    CheckStallDuration("1us stalls", 1, 200000);
    CheckStallDuration("one large stall", 500000, 1);
}

START_TEST(KeProcessor)
{
    KeStallExecutionProcessorTest();
}
