/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Executive Regressions KM-Test
 * PROGRAMMER:      Aleksey Bragin <aleksey@reactos.org>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define STALL_SECONDS 10

static
VOID
CheckStallDuration(
    _In_z_ PCSTR Description,
    _In_ ULONG MicroSeconds,
    _In_ ULONG Iterations)
{
    ULONG i;
    LARGE_INTEGER TimeStart, TimeFinish;
    LONGLONG ExpectedSeconds, ElapsedSeconds;

    DPRINT1("Waiting for %d secs with %s...\n", STALL_SECONDS, Description);
    KeQuerySystemTime(&TimeStart);
    for (i = 0; i < Iterations; i++)
    {
        KeStallExecutionProcessor(MicroSeconds);
    }
    KeQuerySystemTime(&TimeFinish);

    ExpectedSeconds = ((LONGLONG)MicroSeconds * Iterations) / 1000000;
    ElapsedSeconds = (TimeFinish.QuadPart - TimeStart.QuadPart) / 10000000;
    ok(ElapsedSeconds >= ExpectedSeconds - 1,
       "%s returned too early: %I64d seconds, expected at least %I64d seconds\n",
       Description, ElapsedSeconds, ExpectedSeconds - 1);
}

static VOID KeStallExecutionProcessorTest(VOID)
{
    CheckStallDuration("50us stalls", 50, STALL_SECONDS * 1000 * 20);
    CheckStallDuration("1000us stalls", 1000, STALL_SECONDS * 1000);
    CheckStallDuration("1us stalls", 1, STALL_SECONDS * 1000 * 1000);
    CheckStallDuration("one huge stall", STALL_SECONDS * 1000000, 1);
}

START_TEST(KeProcessor)
{
    KeStallExecutionProcessorTest();
}
