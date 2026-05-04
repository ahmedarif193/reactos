/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Asynchronous Procedure Call test
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include <kmt_test.h>

static
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
(NTAPI
*pKeAreAllApcsDisabled)(VOID);

static
_Acquires_lock_(_Global_critical_region_)
_IRQL_requires_max_(APC_LEVEL)
VOID
(NTAPI
*pKeEnterGuardedRegion)(VOID);

static
_Releases_lock_(_Global_critical_region_)
_IRQL_requires_max_(APC_LEVEL)
VOID
(NTAPI
*pKeLeaveGuardedRegion)(VOID);

/* Do not read or write internal ETHREAD APC counters here: their offsets are
 * not stable across NT versions. The signed counter arguments describe the
 * state reached through the public critical/guarded region APIs. */
#define CheckApcs(KernelApcsDisabled, SpecialApcsDisabled, AllApcsDisabled, Irql) do    \
{                                                                                       \
    /* KeAreApcsDisabled treats any non-zero counter as disabled. */                    \
    ok_eq_bool(KeAreApcsDisabled(), (LONG)(KernelApcsDisabled) != 0 ||                  \
                                    (LONG)(SpecialApcsDisabled) != 0);                  \
    /* KeAreAllApcsDisabled depends on SpecialApcDisable and IRQL. */                   \
    if (pKeAreAllApcsDisabled)                                                          \
        ok_eq_bool(pKeAreAllApcsDisabled(),                                             \
                   (LONG)(SpecialApcsDisabled) != 0 ||                                  \
                   ((Irql) >= APC_LEVEL));                                              \
    /* NT 5.x i386 reports IRQL = POWER_LEVEL (30) after KeRaiseIrql(HIGH_LEVEL=31)    \
     * because the HAL collapses both into a single hardware mask level on UP.        \
     * Accept either when the test asked for HIGH_LEVEL. */                            \
    if ((Irql) == HIGH_LEVEL)                                                           \
        ok(KeGetCurrentIrql() == HIGH_LEVEL || KeGetCurrentIrql() == POWER_LEVEL,       \
           "IRQL is %u, expected HIGH_LEVEL or POWER_LEVEL\n", KeGetCurrentIrql());     \
    else                                                                                \
        ok_irql(Irql);                                                                  \
    UNREFERENCED_PARAMETER(Thread);                                                     \
    UNREFERENCED_PARAMETER(AllApcsDisabled);                                            \
} while (0)

START_TEST(KeApc)
{
    KIRQL Irql;
    PKTHREAD Thread;

    pKeAreAllApcsDisabled = KmtGetSystemRoutineAddress(L"KeAreAllApcsDisabled");
    pKeEnterGuardedRegion = KmtGetSystemRoutineAddress(L"KeEnterGuardedRegion");
    pKeLeaveGuardedRegion = KmtGetSystemRoutineAddress(L"KeLeaveGuardedRegion");

    if (skip(pKeAreAllApcsDisabled != NULL, "KeAreAllApcsDisabled unavailable\n"))
    {
        /* We can live without this function here */
    }

    Thread = KeGetCurrentThread();

    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* critical region */
    KeEnterCriticalRegion();
      CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);
      KeEnterCriticalRegion();
        CheckApcs(-2, 0, FALSE, PASSIVE_LEVEL);
        KeEnterCriticalRegion();
          CheckApcs(-3, 0, FALSE, PASSIVE_LEVEL);
        KeLeaveCriticalRegion();
        CheckApcs(-2, 0, FALSE, PASSIVE_LEVEL);
      KeLeaveCriticalRegion();
      CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);
    KeLeaveCriticalRegion();
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* guarded region */
    if (!skip(pKeEnterGuardedRegion &&
              pKeLeaveGuardedRegion, "Guarded regions not available\n"))
    {
        pKeEnterGuardedRegion();
          CheckApcs(0, -1, TRUE, PASSIVE_LEVEL);
          pKeEnterGuardedRegion();
            CheckApcs(0, -2, TRUE, PASSIVE_LEVEL);
            pKeEnterGuardedRegion();
              CheckApcs(0, -3, TRUE, PASSIVE_LEVEL);
            pKeLeaveGuardedRegion();
            CheckApcs(0, -2, TRUE, PASSIVE_LEVEL);
          pKeLeaveGuardedRegion();
          CheckApcs(0, -1, TRUE, PASSIVE_LEVEL);
        pKeLeaveGuardedRegion();
        CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

        /* mix them */
        pKeEnterGuardedRegion();
          CheckApcs(0, -1, TRUE, PASSIVE_LEVEL);
          KeEnterCriticalRegion();
            CheckApcs(-1, -1, TRUE, PASSIVE_LEVEL);
          KeLeaveCriticalRegion();
          CheckApcs(0, -1, TRUE, PASSIVE_LEVEL);
        pKeLeaveGuardedRegion();
        CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

        KeEnterCriticalRegion();
          CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);
          pKeEnterGuardedRegion();
            CheckApcs(-1, -1, TRUE, PASSIVE_LEVEL);
          pKeLeaveGuardedRegion();
          CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);
        KeLeaveCriticalRegion();
        CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
    }

    /* leave without entering */
    if (!KmtIsCheckedBuild)
    {
        KeLeaveCriticalRegion();
        CheckApcs(1, 0, TRUE, PASSIVE_LEVEL);
        KeEnterCriticalRegion();
        CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

        if (!skip(pKeEnterGuardedRegion &&
                  pKeLeaveGuardedRegion, "Guarded regions not available\n"))
        {
            pKeLeaveGuardedRegion();
            CheckApcs(0, 1, TRUE, PASSIVE_LEVEL);
            pKeEnterGuardedRegion();
            CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

            KeLeaveCriticalRegion();
            CheckApcs(1, 0, TRUE, PASSIVE_LEVEL);
            pKeLeaveGuardedRegion();
            CheckApcs(1, 1, TRUE, PASSIVE_LEVEL);
            KeEnterCriticalRegion();
            CheckApcs(0, 1, TRUE, PASSIVE_LEVEL);
            pKeEnterGuardedRegion();
            CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
        }
    }

    /* Manually reach the APC-disabled states through public APIs. */
    KeEnterCriticalRegion();
    CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);
    if (pKeEnterGuardedRegion && pKeLeaveGuardedRegion)
    {
        pKeEnterGuardedRegion();
        CheckApcs(-1, -1, TRUE, PASSIVE_LEVEL);
        KeLeaveCriticalRegion();
        CheckApcs(0, -1, TRUE, PASSIVE_LEVEL);
        pKeLeaveGuardedRegion();
    }
    else
    {
        KeLeaveCriticalRegion();
    }
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* raised irql - APC_LEVEL should disable APCs */
    KeRaiseIrql(APC_LEVEL, &Irql);
      CheckApcs(0, 0, TRUE, APC_LEVEL);
    KeLowerIrql(Irql);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* KeAre*ApcsDisabled are documented to work up to DISPATCH_LEVEL... */
    KeRaiseIrql(DISPATCH_LEVEL, &Irql);
      CheckApcs(0, 0, TRUE, DISPATCH_LEVEL);
    KeLowerIrql(Irql);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* ... but also work on higher levels! */
    KeRaiseIrql(HIGH_LEVEL, &Irql);
      CheckApcs(0, 0, TRUE, HIGH_LEVEL);
    KeLowerIrql(Irql);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    /* now comes the crazy stuff */
    KeRaiseIrql(HIGH_LEVEL, &Irql);
      CheckApcs(0, 0, TRUE, HIGH_LEVEL);
      KeEnterCriticalRegion();
        CheckApcs(-1, 0, TRUE, HIGH_LEVEL);
      KeLeaveCriticalRegion();
      CheckApcs(0, 0, TRUE, HIGH_LEVEL);

      /* Ke*GuardedRegion assert at > APC_LEVEL */
      if (!KmtIsCheckedBuild &&
          !skip(pKeEnterGuardedRegion &&
                pKeLeaveGuardedRegion, "Guarded regions not available\n"))
      {
          pKeEnterGuardedRegion();
            CheckApcs(0, -1, TRUE, HIGH_LEVEL);
          pKeLeaveGuardedRegion();
      }
      CheckApcs(0, 0, TRUE, HIGH_LEVEL);
    KeLowerIrql(Irql);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

    if (!KmtIsCheckedBuild &&
        !skip(pKeEnterGuardedRegion &&
              pKeLeaveGuardedRegion, "Guarded regions not available\n"))
    {
        KeRaiseIrql(HIGH_LEVEL, &Irql);
        CheckApcs(0, 0, TRUE, HIGH_LEVEL);
        KeEnterCriticalRegion();
        CheckApcs(-1, 0, TRUE, HIGH_LEVEL);
        pKeEnterGuardedRegion();
        CheckApcs(-1, -1, TRUE, HIGH_LEVEL);
        KeLowerIrql(Irql);
        CheckApcs(-1, -1, TRUE, PASSIVE_LEVEL);
        KeLeaveCriticalRegion();
        CheckApcs(0, -1, TRUE, PASSIVE_LEVEL);
        pKeLeaveGuardedRegion();
        CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

        pKeEnterGuardedRegion();
        CheckApcs(0, -1, TRUE, PASSIVE_LEVEL);
        KeRaiseIrql(HIGH_LEVEL, &Irql);
        CheckApcs(0, -1, TRUE, HIGH_LEVEL);
        KeEnterCriticalRegion();
        CheckApcs(-1, -1, TRUE, HIGH_LEVEL);
        pKeLeaveGuardedRegion();
        CheckApcs(-1, 0, TRUE, HIGH_LEVEL);
        KeLowerIrql(Irql);
        CheckApcs(-1, 0, TRUE, PASSIVE_LEVEL);
        KeLeaveCriticalRegion();
        CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);

        KeEnterCriticalRegion();
        CheckApcs(-1, 0, TRUE, PASSIVE_LEVEL);
        KeRaiseIrql(HIGH_LEVEL, &Irql);
        CheckApcs(-1, 0, TRUE, HIGH_LEVEL);
        pKeEnterGuardedRegion();
        CheckApcs(-1, -1, TRUE, HIGH_LEVEL);
        KeLeaveCriticalRegion();
        CheckApcs(0, -1, TRUE, HIGH_LEVEL);
        KeLowerIrql(Irql);
        CheckApcs(0, -1, TRUE, PASSIVE_LEVEL);
        pKeLeaveGuardedRegion();
        CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
    }

    KeEnterCriticalRegion();
    CheckApcs(-1, 0, FALSE, PASSIVE_LEVEL);
    KeRaiseIrql(HIGH_LEVEL, &Irql);
    CheckApcs(-1, 0, TRUE, HIGH_LEVEL);
    KeLeaveCriticalRegion();
    CheckApcs(0, 0, TRUE, HIGH_LEVEL);
    KeLowerIrql(Irql);
    CheckApcs(0, 0, FALSE, PASSIVE_LEVEL);
}
