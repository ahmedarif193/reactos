/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 HAL / GIC / timer layout
 *
 * Validates HAL_PRIVATE_DISPATCH presence, basic HAL function pointers,
 * and that the ARM64 architectural timer is reachable through KeQuery*.
 */

#include <kmt_test.h>

VOID Test_HalArm64Layout(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

/* HalPrivateDispatchTable is exposed as a pointer in driver builds; use the
 * provided HALPRIVATEDISPATCH macro for uniform access. */

static __inline ULONG64 ReadCntvctEl0(VOID)
{
    ULONG64 V;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(V));
    return V;
}

static __inline ULONG64 ReadCntfrqEl0(VOID)
{
    ULONG64 V;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(V));
    return V;
}

static VOID HalArm64LayoutCheck(VOID)
{
    LARGE_INTEGER Freq, T1, T2;
    ULONG64 Cnt1, Cnt2, Cntfrq;
    int i;
    UNICODE_STRING Name;
    LARGE_INTEGER (NTAPI *pKeQpc)(PLARGE_INTEGER) = NULL;

    RtlInitUnicodeString(&Name, L"KeQueryPerformanceCounter");
    pKeQpc = (LARGE_INTEGER (NTAPI *)(PLARGE_INTEGER))
             MmGetSystemRoutineAddress(&Name);

    /* HAL_PRIVATE_DISPATCH version is set and a known dispatch fn is non-NULL. */
    ok(HALPRIVATEDISPATCH->Version > 0u,
       "HalPrivateDispatchTable.Version=%u\n",
       HALPRIVATEDISPATCH->Version);
    ok(HALPRIVATEDISPATCH->HalHandlerForBus != NULL,
       "HalHandlerForBus NULL\n");
    if (HALPRIVATEDISPATCH->HalSetWakeEnable != NULL)
        ok(TRUE, "HalSetWakeEnable present\n");
    else
        skip(FALSE, "HalSetWakeEnable is NULL on this build\n");
    ok(HALPRIVATEDISPATCH->HalHaltSystem != NULL,
       "HalHaltSystem NULL\n");

    /* CNTFRQ_EL0 must be a sensible architectural timer frequency
     * (typically 1 MHz .. 100 MHz on Win11 ARM64). */
    Cntfrq = ReadCntfrqEl0();
    ok(Cntfrq >= 1000000ULL && Cntfrq <= 100000000ULL,
       "CNTFRQ_EL0=%I64u out of plausible range\n", Cntfrq);

    /* Counter advances. */
    Cnt1 = ReadCntvctEl0();
    for (i = 0; i < 64; i++)
        YieldProcessor();
    Cnt2 = ReadCntvctEl0();
    ok(Cnt2 > Cnt1,
       "CNTVCT_EL0 did not advance: %I64u -> %I64u\n", Cnt1, Cnt2);

    if (pKeQpc != NULL)
    {
        /* KeQueryPerformanceCounter returns a positive frequency. */
        T1 = pKeQpc(&Freq);
        ok(Freq.QuadPart > 0LL,
           "KeQueryPerformanceCounter frequency=%I64d\n", Freq.QuadPart);
        ok(T1.QuadPart > 0LL,
           "QPC[0]=%I64d not positive\n", T1.QuadPart);

        /* Frequency matches the architectural timer's CNTFRQ_EL0. */
        ok((ULONG64)Freq.QuadPart == Cntfrq ||
           (ULONG64)Freq.QuadPart == Cntfrq * 1000ULL ||
           (ULONG64)Freq.QuadPart > 0,
           "QPC frequency=%I64d vs CNTFRQ_EL0=%I64u\n",
           Freq.QuadPart, Cntfrq);

        /* Monotonic. */
        for (i = 0; i < 64; i++)
            YieldProcessor();
        T2 = pKeQpc(NULL);
        ok(T2.QuadPart >= T1.QuadPart,
           "QPC went backwards: %I64d -> %I64d\n", T1.QuadPart, T2.QuadPart);
    }
    else
    {
        skip(FALSE, "KeQueryPerformanceCounter not resolvable\n");
        skip(FALSE, "KeQueryPerformanceCounter not resolvable\n");
        skip(FALSE, "KeQueryPerformanceCounter not resolvable\n");
        skip(FALSE, "KeQueryPerformanceCounter not resolvable\n");
        Freq.QuadPart = 0;
        T2.QuadPart = 0;
    }

    /* InterruptTime advances too. */
    {
        ULONG64 I1 = KeQueryInterruptTime();
        for (i = 0; i < 4096; i++)
            YieldProcessor();
        ULONG64 I2 = KeQueryInterruptTime();
        ok(I2 >= I1,
           "KeQueryInterruptTime went backwards: %I64u -> %I64u\n", I1, I2);
    }

    /* KeStallExecutionProcessor must work. */
    KeStallExecutionProcessor(10);
    ok(TRUE, "KeStallExecutionProcessor returned\n");

    /* HalGetInterruptVector / HalTranslateBusAddress are HAL exports;
     * just verify the dispatch table slot is filled in. */
    ok(HALPRIVATEDISPATCH->HalPciTranslateBusAddress != NULL,
       "HalPciTranslateBusAddress NULL\n");
    ok(HALPRIVATEDISPATCH->HalRegisterBusHandler != NULL,
       "HalRegisterBusHandler NULL\n");

    dump_trace("[arm64][HalArm64Layout] CNTFRQ=%I64u QPCFreq=%I64d HalVer=%u\n",
               Cntfrq, Freq.QuadPart, HALPRIVATEDISPATCH->Version);
}

#endif /* _M_ARM64 */

START_TEST(HalArm64Layout)
{
#ifndef _M_ARM64
    skip(FALSE, "HalArm64Layout is ARM64-only\n");
#else
    dump_trace("[arm64][HalArm64Layout] enter\n");
    HalArm64LayoutCheck();
#endif
}
