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
#include <reactos/unaligned.h>

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

static __inline ULONG64 ReadSctlrEl1(VOID)
{
    ULONG64 V;
    __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(V));
    return V;
}

static volatile ULONG64 Arm64SctlrByProcessor[MAXIMUM_PROCESSORS];

static ULONG_PTR NTAPI Arm64CaptureSctlrIpi(ULONG_PTR Context)
{
    ULONG Processor = KeGetCurrentProcessorNumber();

    UNREFERENCED_PARAMETER(Context);
    if (Processor < RTL_NUMBER_OF(Arm64SctlrByProcessor))
        Arm64SctlrByProcessor[Processor] = ReadSctlrEl1();
    return Processor;
}

static VOID Arm64UnalignedHelpersCheck(VOID)
{
    UCHAR ReadBuffer[16];
    UCHAR WriteBuffer[16];
    ULONG Offset;

    for (Offset = 0; Offset < RTL_NUMBER_OF(ReadBuffer); ++Offset)
        ReadBuffer[Offset] = (UCHAR)(0x11U + (Offset * 0x13U));

    for (Offset = 0; Offset < 8; ++Offset)
    {
        USHORT Expected16 = (USHORT)(((USHORT)ReadBuffer[Offset]) | ((USHORT)ReadBuffer[Offset + 1] << 8));
        ULONG Expected32 = ((ULONG)ReadBuffer[Offset]) | ((ULONG)ReadBuffer[Offset + 1] << 8) | ((ULONG)ReadBuffer[Offset + 2] << 16) | ((ULONG)ReadBuffer[Offset + 3] << 24);
        ULONG64 Expected64 = ((ULONG64)ReadBuffer[Offset]) | ((ULONG64)ReadBuffer[Offset + 1] << 8) | ((ULONG64)ReadBuffer[Offset + 2] << 16) | ((ULONG64)ReadBuffer[Offset + 3] << 24) | ((ULONG64)ReadBuffer[Offset + 4] << 32) | ((ULONG64)ReadBuffer[Offset + 5] << 40) | ((ULONG64)ReadBuffer[Offset + 6] << 48) | ((ULONG64)ReadBuffer[Offset + 7] << 56);

        ok_eq_uint(ReadUnalignedU16((const USHORT *)&ReadBuffer[Offset]), Expected16);
        ok_eq_ulong(ReadUnalignedU32((const ULONG *)&ReadBuffer[Offset]), Expected32);
        ok_eq_ulonglong(ReadUnalignedU64((const ULONG64 *)&ReadBuffer[Offset]), Expected64);

        RtlFillMemory(WriteBuffer, sizeof(WriteBuffer), 0xCC);
        WriteUnalignedU16((USHORT *)&WriteBuffer[Offset], Expected16);
        ok_eq_uint(WriteBuffer[Offset], (UCHAR)Expected16);
        ok_eq_uint(WriteBuffer[Offset + 1], (UCHAR)(Expected16 >> 8));

        RtlFillMemory(WriteBuffer, sizeof(WriteBuffer), 0xCC);
        WriteUnalignedU32((ULONG *)&WriteBuffer[Offset], Expected32);
        ok_eq_uint(WriteBuffer[Offset], (UCHAR)Expected32);
        ok_eq_uint(WriteBuffer[Offset + 1], (UCHAR)(Expected32 >> 8));
        ok_eq_uint(WriteBuffer[Offset + 2], (UCHAR)(Expected32 >> 16));
        ok_eq_uint(WriteBuffer[Offset + 3], (UCHAR)(Expected32 >> 24));

        RtlFillMemory(WriteBuffer, sizeof(WriteBuffer), 0xCC);
        WriteUnalignedU64((ULONG64 *)&WriteBuffer[Offset], Expected64);
        ok_eq_uint(WriteBuffer[Offset], (UCHAR)Expected64);
        ok_eq_uint(WriteBuffer[Offset + 1], (UCHAR)(Expected64 >> 8));
        ok_eq_uint(WriteBuffer[Offset + 2], (UCHAR)(Expected64 >> 16));
        ok_eq_uint(WriteBuffer[Offset + 3], (UCHAR)(Expected64 >> 24));
        ok_eq_uint(WriteBuffer[Offset + 4], (UCHAR)(Expected64 >> 32));
        ok_eq_uint(WriteBuffer[Offset + 5], (UCHAR)(Expected64 >> 40));
        ok_eq_uint(WriteBuffer[Offset + 6], (UCHAR)(Expected64 >> 48));
        ok_eq_uint(WriteBuffer[Offset + 7], (UCHAR)(Expected64 >> 56));
    }
}

static VOID Arm64IntrinsicsCheck(VOID)
{
    ULONG64 Tpidr, Midr;
    ULONG64 Cnt1, Cnt2;
    ULONG64 Int1, Int2;
    ULONG64 Sctlr;
    ULONG Processor, ProcessorCount;
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

    RtlZeroMemory((PVOID)Arm64SctlrByProcessor, sizeof(Arm64SctlrByProcessor));
    KeIpiGenericCall(Arm64CaptureSctlrIpi, 0);
    ProcessorCount = KeQueryActiveProcessorCount(NULL);
    for (Processor = 0; Processor < min(ProcessorCount, RTL_NUMBER_OF(Arm64SctlrByProcessor)); ++Processor)
    {
        Sctlr = Arm64SctlrByProcessor[Processor];
        ok(Sctlr != 0, "CPU %lu did not publish SCTLR_EL1\n", Processor);
        ok((Sctlr & (1ULL << 1)) == 0, "CPU %lu SCTLR_EL1.A is set: 0x%I64x\n", Processor, Sctlr);
        dump_trace("[arm64][KeArm64Intrinsics] CPU %lu SCTLR_EL1=0x%I64x\n", Processor, Sctlr);
    }
    Arm64UnalignedHelpersCheck();

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
