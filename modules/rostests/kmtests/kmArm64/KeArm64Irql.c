/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 KIRQL behaviour
 *
 * Round-trips KeRaiseIrql / KeLowerIrql across PASSIVE -> APC -> DISPATCH ->
 * ... -> HIGH_LEVEL, validates KeRaiseIrqlToDpcLevel / KeRaiseIrqlToSynchLevel
 * and that TPIDR_EL1-backed KPCR.CurrentIrql matches KeGetCurrentIrql().
 */

#include <kmt_test.h>

VOID Test_KeArm64Irql(VOID);

typedef VOID (FASTCALL *PHAL_SET_GIC_PRIORITY_MASK)(_In_ KIRQL Irql);
typedef ULONG (FASTCALL *PHAL_GET_GIC_PRIORITY_MASK)(VOID);
typedef VOID (FASTCALL *PHAL_REQUEST_SOFTWARE_INTERRUPT)(_In_ KIRQL SoftwareInterruptRequested);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64IrqlCheck(VOID)
{
    PKIPCR Pcr = KeGetPcr();
    KIRQL OldIrql;
    KIRQL OldIrql2;
    KIRQL Target;
    ULONG BaselinePriorityMask;
    ULONG MaskedPriorityMask;
    ULONG ReconciledPriorityMask;
    UNICODE_STRING RoutineName;
    PHAL_SET_GIC_PRIORITY_MASK HalSetGicPriorityMask;
    PHAL_GET_GIC_PRIORITY_MASK HalGetGicPriorityMask;
    PHAL_REQUEST_SOFTWARE_INTERRUPT HalRequestSoftwareInterrupt;
    static const KIRQL Targets[] = {
        PASSIVE_LEVEL,
        APC_LEVEL,
        DISPATCH_LEVEL,
        CMCI_LEVEL,
        SYNCH_LEVEL,
        CLOCK_LEVEL,
        IPI_LEVEL,
        PROFILE_LEVEL,
        HIGH_LEVEL,
    };
    int i;

    ok(Pcr != NULL, "KPCR is NULL\n");
    if (Pcr == NULL)
        return;

    /* We should be at PASSIVE on entry. */
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);
    ok_eq_uint(Pcr->CurrentIrql, PASSIVE_LEVEL);

    RtlInitUnicodeString(&RoutineName, L"HalSetGicPriorityMask");
    HalSetGicPriorityMask = (PHAL_SET_GIC_PRIORITY_MASK)MmGetSystemRoutineAddress(&RoutineName);
    RtlInitUnicodeString(&RoutineName, L"HalGetGicPriorityMask");
    HalGetGicPriorityMask = (PHAL_GET_GIC_PRIORITY_MASK)MmGetSystemRoutineAddress(&RoutineName);
    RtlInitUnicodeString(&RoutineName, L"HalRequestSoftwareInterrupt");
    HalRequestSoftwareInterrupt = (PHAL_REQUEST_SOFTWARE_INTERRUPT)MmGetSystemRoutineAddress(&RoutineName);

    /* Win11 takes the KfLowerIrql slow path for a same-level lower below
       DISPATCH_LEVEL. Verify that it reconciles a lazily masked GIC PMR even
       though the logical IRQL is already PASSIVE_LEVEL. The GIC helpers are
       HAL-private on Windows, so keep them out of the static import table. */
    if (!skip(HalSetGicPriorityMask != NULL && HalGetGicPriorityMask != NULL && HalRequestSoftwareInterrupt != NULL, "GIC priority-mask helpers are not exported\n"))
    {
        HalSetGicPriorityMask(PASSIVE_LEVEL);
        BaselinePriorityMask = HalGetGicPriorityMask();
        KeRaiseIrql(HIGH_LEVEL, &OldIrql);
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
        MaskedPriorityMask = HalGetGicPriorityMask();
        Pcr->CurrentIrql = PASSIVE_LEVEL;
        KeLowerIrql(PASSIVE_LEVEL);
        ReconciledPriorityMask = HalGetGicPriorityMask();
        HalSetGicPriorityMask(PASSIVE_LEVEL);

        ok(MaskedPriorityMask != BaselinePriorityMask, "PMR setup did not change the mask: 0x%lx\n", MaskedPriorityMask);
        ok_eq_ulong(ReconciledPriorityMask, BaselinePriorityMask);
    }

    /* Single hops up and back down. */
    for (i = 0; i < (int)(sizeof(Targets) / sizeof(Targets[0])); i++)
    {
        Target = Targets[i];

        KeRaiseIrql(Target, &OldIrql);
        ok_eq_uint(OldIrql, PASSIVE_LEVEL);
        ok_eq_uint(KeGetCurrentIrql(), Target);
        if (Pcr != NULL)
            ok_eq_uint(Pcr->CurrentIrql, Target);

        KeLowerIrql(OldIrql);
        ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);
        if (Pcr != NULL)
            ok_eq_uint(Pcr->CurrentIrql, PASSIVE_LEVEL);
    }

    /* Stacked raise: PASSIVE -> APC -> DISPATCH -> HIGH_LEVEL -> back. */
    KeRaiseIrql(APC_LEVEL, &OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), APC_LEVEL);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql2);
    ok_eq_uint(OldIrql2, APC_LEVEL);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    if (Pcr != NULL)
        ok_eq_uint(Pcr->CurrentIrql, DISPATCH_LEVEL);
    {
        KIRQL OldHigh;
        KeRaiseIrql(HIGH_LEVEL, &OldHigh);
        ok_eq_uint(OldHigh, DISPATCH_LEVEL);
        ok_eq_uint(KeGetCurrentIrql(), HIGH_LEVEL);
        if (Pcr != NULL)
            ok_eq_uint(Pcr->CurrentIrql, HIGH_LEVEL);
        KeLowerIrql(OldHigh);
        ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    }
    KeLowerIrql(OldIrql2);
    ok_eq_uint(KeGetCurrentIrql(), APC_LEVEL);
    KeLowerIrql(OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);

    /* KeRaiseIrqlToDpcLevel returns the previous IRQL and sets DISPATCH. */
    OldIrql = KeRaiseIrqlToDpcLevel();
    ok_eq_uint(OldIrql, PASSIVE_LEVEL);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    if (Pcr != NULL)
        ok_eq_uint(Pcr->CurrentIrql, DISPATCH_LEVEL);
    KeLowerIrql(OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);

    /* KeRaiseIrqlToSynchLevel returns the previous IRQL and sets SYNCH. */
    OldIrql = KeRaiseIrqlToSynchLevel();
    ok_eq_uint(OldIrql, PASSIVE_LEVEL);
    ok_eq_uint(KeGetCurrentIrql(), SYNCH_LEVEL);
    if (Pcr != NULL)
        ok_eq_uint(Pcr->CurrentIrql, SYNCH_LEVEL);
    KeLowerIrql(OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);

    /* Sanity-check that ARM64 IRQL constants match the published values. */
    ok_eq_uint(PASSIVE_LEVEL, 0);
    ok_eq_uint(APC_LEVEL, 1);
    ok_eq_uint(DISPATCH_LEVEL, 2);
    ok_eq_uint(SYNCH_LEVEL, 12);
    ok_eq_uint(CLOCK_LEVEL, 13);
    ok_eq_uint(IPI_LEVEL, 14);
    ok_eq_uint(HIGH_LEVEL, 15);

    dump_trace("[arm64][KeArm64Irql] final IRQL=%u\n", KeGetCurrentIrql());
}

#endif /* _M_ARM64 */

START_TEST(KeArm64Irql)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64Irql is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64Irql] enter\n");
    Arm64IrqlCheck();
#endif
}
