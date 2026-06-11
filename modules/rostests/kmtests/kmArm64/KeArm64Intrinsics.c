/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 intrinsics: system registers, barriers, interlocked
 *
 * Exercises TPIDR_EL1 (PCR), MIDR_EL1 (CPU identity), CNTVCT_EL0 (counter),
 * YieldProcessor/__dmb/__dsb/__isb, InterlockedExchange/Add/Or, and validates that
 * KeQueryInterruptTime / KeQueryPerformanceCounter are monotonic.
 */

#include <kmt_test.h>

VOID Test_KeArm64Intrinsics(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static __inline ULONG64 ReadTpidrEl1(VOID)
{
    ULONG64 V;
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r"(V));
    return V;
}

static __inline ULONG64 ReadMidrEl1(VOID)
{
    ULONG64 V;
    __asm__ __volatile__("mrs %0, midr_el1" : "=r"(V));
    return V;
}

static __inline ULONG64 ReadCntvctEl0(VOID)
{
    ULONG64 V;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(V));
    return V;
}

static VOID Arm64IntrinsicsCheck(VOID)
{
    ULONG64 Tpidr, Midr;
    ULONG64 Cnt1, Cnt2;
    ULONG64 Int1, Int2;
    PKIPCR Pcr;
    LONG Long32;
    LONG64 Long64Val;
    int i;

    /* TPIDR_EL1 must equal KeGetPcr(). */
    Pcr = KeGetPcr();
    Tpidr = ReadTpidrEl1();
    ok(Pcr != NULL, "KeGetPcr() is NULL\n");
    if (Pcr != NULL)
        ok_eq_ulonglong(Tpidr, (ULONG64)(ULONG_PTR)Pcr);

    /* MIDR_EL1 is non-zero on real silicon. */
    Midr = ReadMidrEl1();
    ok(Midr != 0ULL, "MIDR_EL1 is zero\n");
    dump_trace("[arm64][KeArm64Intrinsics] MIDR_EL1=0x%I64x\n", Midr);

    /* CNTVCT_EL0 monotonic. */
    Cnt1 = ReadCntvctEl0();
    for (i = 0; i < 16; i++)
        YieldProcessor();
    Cnt2 = ReadCntvctEl0();
    ok(Cnt2 >= Cnt1,
       "CNTVCT went backwards: %I64u -> %I64u\n", Cnt1, Cnt2);

    /* Barriers must not crash. */
    __dmb(_ARM64_BARRIER_SY);
    __dsb(_ARM64_BARRIER_SY);
    __isb(_ARM64_BARRIER_SY);
    ok(TRUE, "barriers executed\n");

    /* InterlockedExchange round-trip. */
    Long32 = 0;
    {
        LONG Prev = InterlockedExchange(&Long32, 0x1234);
        ok_eq_long(Prev, 0L);
        ok_eq_long(Long32, 0x1234L);
    }

    /* InterlockedAdd via ExchangeAdd: Long32 was 0x1234. */
    {
        LONG Prev = InterlockedExchangeAdd(&Long32, 1);
        ok_eq_long(Prev, 0x1234L);
        ok_eq_long(Long32, 0x1235L);
    }

    /* InterlockedOr. */
    Long32 = 0;
    {
        LONG Prev = InterlockedOr(&Long32, 0xF);
        ok_eq_long(Prev, 0L);
        ok_eq_long(Long32, 0xFL);
    }

    /* 64-bit Interlocked. */
    Long64Val = 0;
    {
        LONG64 Prev = _InterlockedExchange64(&Long64Val, 0xDEADBEEFCAFEBABELL);
        ok_eq_longlong(Prev, 0LL);
        ok_eq_longlong(Long64Val, 0xDEADBEEFCAFEBABELL);
    }

    /* KeQueryInterruptTime monotonic. */
    Int1 = KeQueryInterruptTime();
    for (i = 0; i < 4096; i++)
        YieldProcessor();
    Int2 = KeQueryInterruptTime();
    ok(Int2 >= Int1,
       "KeQueryInterruptTime went backwards: %I64u -> %I64u\n", Int1, Int2);

    /* Resolve KeQueryPerformanceCounter at runtime so the driver still loads
     * on Win11 ARM64 (where this routine is in ntoskrnl rather than hal). */
    {
        UNICODE_STRING Name;
        LARGE_INTEGER (NTAPI *pKeQpc)(PLARGE_INTEGER) = NULL;
        RtlInitUnicodeString(&Name, L"KeQueryPerformanceCounter");
        pKeQpc = (LARGE_INTEGER (NTAPI *)(PLARGE_INTEGER))
                 MmGetSystemRoutineAddress(&Name);
        if (pKeQpc != NULL)
        {
            LARGE_INTEGER Perf1 = pKeQpc(NULL);
            for (i = 0; i < 16; i++)
                YieldProcessor();
            LARGE_INTEGER Perf2 = pKeQpc(NULL);
            ok(Perf2.QuadPart >= Perf1.QuadPart,
               "KeQueryPerformanceCounter went backwards: %I64d -> %I64d\n",
               Perf1.QuadPart, Perf2.QuadPart);
        }
        else
        {
            skip(FALSE, "KeQueryPerformanceCounter not resolvable\n");
        }
    }

    dump_trace("[arm64][KeArm64Intrinsics] CNTVCT=0x%I64x INT=0x%I64x\n",
               Cnt2, Int2);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64Intrinsics)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64Intrinsics is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64Intrinsics] enter\n");
    Arm64IntrinsicsCheck();
#endif
}
