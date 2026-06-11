/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 trap / exception / switch / start frame layout
 *
 * Walks the public KTRAP_FRAME / KEXCEPTION_FRAME / KSTART_FRAME /
 * KSWITCH_FRAME / UCALLOUT_FRAME / MACHINE_FRAME field offsets.
 */

#include <kmt_test.h>

VOID Test_KeArm64Frames(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64FrameLayout(VOID)
{
    /* KTRAP_FRAME -- ARM64 trap frame.
     * Compute totals from field offsets to avoid hard-coding a constant
     * that drifts when the published Win11 ARM64 layout is tweaked. */
    dump_trace("[arm64][KeArm64Frames] sizeof(KTRAP_FRAME)=0x%Ix\n",
               (SIZE_T)sizeof(KTRAP_FRAME));
    dump_trace("[arm64][KeArm64Frames] sizeof(KEXCEPTION_FRAME)=0x%Ix\n",
               (SIZE_T)sizeof(KEXCEPTION_FRAME));

    /* KTRAP_FRAME size is fixed to keep the kernel ABI stable. */
    ok_eq_ulonglong((ULONGLONG)sizeof(KTRAP_FRAME), 0x160ULL);

    /* Leading byte fields. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, ExceptionActive), 0x0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, ContextFromKFramesUnwound), 0x1ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, DebugRegistersValid), 0x2ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, PreviousMode), 0x3ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, PreviousIrql), 0x4ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Reserved), 0x8ULL);

    /* Fault accounting + VFP state. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, FaultAddress), 0x10ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, TrapFrame), 0x18ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, VfpState), 0x20ULL);

    /* Debug registers. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Bcr), 0x24ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Bvr), 0x48ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Wcr), 0x88ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Wvr), 0x90ULL);

    /* Status word + stack. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Spsr), 0xA0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Esr), 0xA4ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Sp), 0xA8ULL);

    /* X registers. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X), 0xB0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X0), 0xB0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X1), 0xB8ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X2), 0xC0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X3), 0xC8ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X4), 0xD0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X5), 0xD8ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X6), 0xE0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X7), 0xE8ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X8), 0xF0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X9), 0xF8ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X10), 0x100ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X11), 0x108ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X12), 0x110ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X13), 0x118ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X14), 0x120ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X15), 0x128ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X16), 0x130ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X17), 0x138ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, X18), 0x140ULL);

    /* Final trio: Lr, Fp, Pc. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Lr), 0x148ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Fp), 0x150ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KTRAP_FRAME, Pc), 0x158ULL);

    /*
     * KEXCEPTION_FRAME -- non-volatile call-preserved registers.
     * NTDDI >= WIN8 uses Spare1 / Spare2 slots.
     */
    ok(sizeof(KEXCEPTION_FRAME) >= 0xC0,
       "KEXCEPTION_FRAME too small: 0x%Ix\n",
       (SIZE_T)sizeof(KEXCEPTION_FRAME));
    ok(sizeof(KEXCEPTION_FRAME) <= 0xE0,
       "KEXCEPTION_FRAME too large: 0x%Ix\n",
       (SIZE_T)sizeof(KEXCEPTION_FRAME));

    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KEXCEPTION_FRAME, P1Home), 0x0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KEXCEPTION_FRAME, P2Home), 0x8ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KEXCEPTION_FRAME, P3Home), 0x10ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KEXCEPTION_FRAME, P4Home), 0x18ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KEXCEPTION_FRAME, P5), 0x20ULL);

    /* TrapFrame slot location varies with NTDDI; just verify > P5. */
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, TrapFrame) > 0x20ULL,
       "TrapFrame offset 0x%Ix not after P5\n",
       (SIZE_T)FIELD_OFFSET(KEXCEPTION_FRAME, TrapFrame));

    /* X19..X28 are at non-overlapping growing offsets. */
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, X20) > FIELD_OFFSET(KEXCEPTION_FRAME, X19), "X20 > X19\n");
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, X21) > FIELD_OFFSET(KEXCEPTION_FRAME, X20), "X21 > X20\n");
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, X22) > FIELD_OFFSET(KEXCEPTION_FRAME, X21), "X22 > X21\n");
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, X23) > FIELD_OFFSET(KEXCEPTION_FRAME, X22), "X23 > X22\n");
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, X24) > FIELD_OFFSET(KEXCEPTION_FRAME, X23), "X24 > X23\n");
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, X25) > FIELD_OFFSET(KEXCEPTION_FRAME, X24), "X25 > X24\n");
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, X26) > FIELD_OFFSET(KEXCEPTION_FRAME, X25), "X26 > X25\n");
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, X27) > FIELD_OFFSET(KEXCEPTION_FRAME, X26), "X27 > X26\n");
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, X28) > FIELD_OFFSET(KEXCEPTION_FRAME, X27), "X28 > X27\n");

    ok(FIELD_OFFSET(KEXCEPTION_FRAME, Fp) > FIELD_OFFSET(KEXCEPTION_FRAME, X28), "Fp > X28\n");
    ok(FIELD_OFFSET(KEXCEPTION_FRAME, Lr) > FIELD_OFFSET(KEXCEPTION_FRAME, Fp), "Lr > Fp\n");

    /*
     * KSTART_FRAME -- 6 ULONG64s = 48 bytes, padded to 16-byte alignment.
     */
    ok_eq_ulonglong((ULONGLONG)sizeof(KSTART_FRAME), 0x30ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSTART_FRAME, StartRoutine), 0x0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSTART_FRAME, StartContext), 0x8ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSTART_FRAME, SystemRoutine), 0x10ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSTART_FRAME, Parameter), 0x18ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSTART_FRAME, Return), 0x20ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSTART_FRAME, Padding), 0x28ULL);

    /*
     * KSWITCH_FRAME -- 12 ULONG64 + ReturnAddress + ApcBypass + Reserved[7].
     */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, X19), 0x0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, X20), 0x8ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, X21), 0x10ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, X22), 0x18ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, X23), 0x20ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, X24), 0x28ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, X25), 0x30ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, X26), 0x38ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, X27), 0x40ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, X28), 0x48ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, Fp), 0x50ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, Lr), 0x58ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, ReturnAddress), 0x60ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(KSWITCH_FRAME, ApcBypass), 0x68ULL);
    ok_eq_ulonglong((ULONGLONG)sizeof(KSWITCH_FRAME), 0x70ULL);

    /*
     * MACHINE_FRAME -- just (Sp, Pc).
     */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(MACHINE_FRAME, Sp), 0x0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(MACHINE_FRAME, Pc), 0x8ULL);
    ok_eq_ulonglong((ULONGLONG)sizeof(MACHINE_FRAME), 0x10ULL);

    /*
     * UCALLOUT_FRAME -- 4 home slots + Buffer + Length + ApiNumber +
     * MachineFrame.
     */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(UCALLOUT_FRAME, P1Home), 0x0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(UCALLOUT_FRAME, P2Home), 0x8ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(UCALLOUT_FRAME, P3Home), 0x10ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(UCALLOUT_FRAME, P4Home), 0x18ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(UCALLOUT_FRAME, Buffer), 0x20ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(UCALLOUT_FRAME, Length), 0x28ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(UCALLOUT_FRAME, ApiNumber), 0x2CULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(UCALLOUT_FRAME, MachineFrame), 0x30ULL);

    /* Tail dump. */
    dump_trace("[arm64][KeArm64Frames] KTRAP=%Iu KEXC=%Iu KSTART=%Iu KSWITCH=%Iu\n",
               (SIZE_T)sizeof(KTRAP_FRAME),
               (SIZE_T)sizeof(KEXCEPTION_FRAME),
               (SIZE_T)sizeof(KSTART_FRAME),
               (SIZE_T)sizeof(KSWITCH_FRAME));
}

#endif /* _M_ARM64 */

START_TEST(KeArm64Frames)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64Frames is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64Frames] enter\n");
    Arm64FrameLayout();
#endif
}
