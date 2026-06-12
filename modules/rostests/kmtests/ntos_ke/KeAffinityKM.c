/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite processor affinity API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestProcessorCounts(VOID)
{
    KAFFINITY Active = 0;
    ULONG Count, CountEx, Maximum;
    KAFFINITY Mask;
    ULONG i, Bits;

    Count = KeQueryActiveProcessorCount(&Active);
    ok(Count >= 1, "active count %lu\n", Count);
    ok(Active != 0, "active mask zero\n");
    ok_eq_ulonglong((ULONGLONG)Active, (ULONGLONG)KeQueryActiveProcessors());

    Bits = 0;
    Mask = Active;
    for (i = 0; i < sizeof(KAFFINITY) * 8; i++)
        if (Mask & ((KAFFINITY)1 << i)) Bits++;
    ok_eq_ulong(Bits, Count);

    CountEx = KeQueryActiveProcessorCountEx(0);
    ok_eq_ulong(CountEx, Count);
    CountEx = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    ok_eq_ulong(CountEx, Count);
    CountEx = KeQueryActiveProcessorCountEx(63);
    ok_eq_ulong(CountEx, 0UL);

    Maximum = KeQueryMaximumProcessorCount();
    ok(Maximum >= Count, "maximum %lu < active %lu\n", Maximum, Count);
}

static
VOID
TestAffinityPinning(VOID)
{
    KAFFINITY Active = KeQueryActiveProcessors();
    KAFFINITY Old;
    ULONG OriginalCpu, Cpu, i;

    OriginalCpu = KeGetCurrentProcessorNumberEx(NULL);

    for (i = 0; i < sizeof(KAFFINITY) * 8; i++)
    {
        if ((Active & ((KAFFINITY)1 << i)) == 0) continue;

        Old = KeSetSystemAffinityThreadEx((KAFFINITY)1 << i);
        Cpu = KeGetCurrentProcessorNumberEx(NULL);
        ok_eq_ulong(Cpu, i);
        KeRevertToUserAffinityThreadEx(Old);
    }

    KeSetSystemAffinityThread((KAFFINITY)1 << OriginalCpu);
    ok_eq_ulong(KeGetCurrentProcessorNumberEx(NULL), OriginalCpu);
    KeRevertToUserAffinityThread();
}

static
VOID
TestNestedAffinity(VOID)
{
    KAFFINITY Active = KeQueryActiveProcessors();
    KAFFINITY Old1, Old2;
    ULONG FirstCpu, SecondCpu, i;

    FirstCpu = 0;
    SecondCpu = 0;
    for (i = 0; i < sizeof(KAFFINITY) * 8; i++)
    {
        if (Active & ((KAFFINITY)1 << i))
        {
            FirstCpu = i;
            break;
        }
    }
    for (i = FirstCpu + 1; i < sizeof(KAFFINITY) * 8; i++)
    {
        if (Active & ((KAFFINITY)1 << i))
        {
            SecondCpu = i;
            break;
        }
    }
    if (SecondCpu == 0)
    {
        skip(FALSE, "uniprocessor, nested affinity not exercised\n");
        return;
    }

    Old1 = KeSetSystemAffinityThreadEx((KAFFINITY)1 << FirstCpu);
    ok_eq_ulonglong((ULONGLONG)Old1, 0ULL);
    ok_eq_ulong(KeGetCurrentProcessorNumberEx(NULL), FirstCpu);

    Old2 = KeSetSystemAffinityThreadEx((KAFFINITY)1 << SecondCpu);
    ok_eq_ulonglong((ULONGLONG)Old2, (ULONGLONG)((KAFFINITY)1 << FirstCpu));
    ok_eq_ulong(KeGetCurrentProcessorNumberEx(NULL), SecondCpu);

    KeRevertToUserAffinityThreadEx(Old2);
    ok_eq_ulong(KeGetCurrentProcessorNumberEx(NULL), FirstCpu);

    KeRevertToUserAffinityThreadEx(Old1);
}

START_TEST(KeAffinityKM)
{
    TestProcessorCounts();
    TestAffinityPinning();
    TestNestedAffinity();
}
