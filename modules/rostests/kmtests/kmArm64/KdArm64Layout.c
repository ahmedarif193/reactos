/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 KD / KDBG layout
 *
 * Validates that the Win11 ARM64 KPRCB / KIPCR carry the Win11 PDB field
 * names (no x86 CpuType/CpuID/CFlushSize aliases) and that the MIDR_EL1
 * sub-field of the ProcessorState is reachable via KARM64_ARCH_STATE.
 */

#include <kmt_test.h>
#include <wdbgexts.h>

VOID Test_KdArm64Layout(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID KdArm64LayoutCheck(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    /* Win11 KDDEBUGGER_DATA64 size is at least 0x318 bytes; ROS builds with
     * NTDDI_VISTA which keeps the Win7+ block tail (>= 0x340 bytes). */
    ok(sizeof(KDDEBUGGER_DATA64) >= 0x318,
       "sizeof(KDDEBUGGER_DATA64) = 0x%Ix < 0x318\n",
       (SIZE_T)sizeof(KDDEBUGGER_DATA64));

    /* MmSystemRangeStart on ARM64 is the canonical kernel-half boundary. */
    {
        const ULONG_PTR Boundary = 0xFFFF000000000000ULL;
        ULONG_PTR Start = (ULONG_PTR)MM_SYSTEM_RANGE_START;
        ok(Start >= Boundary || Start == 0,
           "MM_SYSTEM_RANGE_START=0x%I64x outside kernel half\n",
           (ULONG64)Start);
    }

    /* KI_USER_SHARED_DATA must be a valid kernel-mapped page address. */
    ok((ULONG_PTR)SharedUserData >= 0xFFFF000000000000ULL,
       "SharedUserData=%p is not kernel-side\n", SharedUserData);

    /* ARM64 KPRCB must carry the Win11 names, NOT the legacy x86 ones. */
    ok_eq_size(sizeof(Prcb->ProcessorModel), (SIZE_T)2);
    ok_eq_size(sizeof(Prcb->ProcessorRevision), (SIZE_T)2);
    ok_eq_size(sizeof(Prcb->ProcessorVendorString), (SIZE_T)2);

    /* KARM64_ARCH_STATE has Midr_El1 as its first field. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KARM64_ARCH_STATE, Midr_El1),
                    0ULL);

    /* KARM64_ARCH_STATE size is 0x2a0. */
    ok_eq_ulonglong((ULONGLONG)sizeof(KARM64_ARCH_STATE), 0x2a0ULL);

    /* KPROCESSOR_STATE wraps it; ProcessorState lives at PRCB+0x40. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KPRCB, ProcessorState),
                    0x40ULL);
    ok_eq_ulonglong((ULONGLONG)sizeof(KPROCESSOR_STATE),
                    0x6d0ULL);

    /* Sanity: PRCB embeds in KPCR at +0x980. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KIPCR, Prcb), 0x980ULL);

    dump_trace("[arm64][KdArm64Layout] sizeof(KDDEBUGGER_DATA64)=0x%Ix\n",
               (SIZE_T)sizeof(KDDEBUGGER_DATA64));
}

#endif /* _M_ARM64 */

START_TEST(KdArm64Layout)
{
#ifndef _M_ARM64
    skip(FALSE, "KdArm64Layout is ARM64-only\n");
#else
    dump_trace("[arm64][KdArm64Layout] enter\n");
    KdArm64LayoutCheck();
#endif
}
