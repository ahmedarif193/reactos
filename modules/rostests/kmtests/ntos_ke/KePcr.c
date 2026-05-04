/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     KPCR layout validation test
 *
 * x64 KPCR:
 *   0x00 NtTib (GdtBase, TssBase, UserRsp, Self, CurrentPrcb, ...)
 *   0x18 Self
 *   0x20 CurrentPrcb
 *   0x50 Irql
 *   0x180 Prcb (embedded KPRCB; PRCB.Number is the processor index)
 *   GS_BASE in kernel-mode points at the KPCR.
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(KePcr)
{
#if !defined(_M_AMD64)
    skip(FALSE, "KPCR layout test is x64-only\n");
#else
    PKPCR Pcr;
    ULONG_PTR PcrFromSeg;
    KIRQL CurrentIrql;
    ULONG ProcNum;
    KIRQL OldIrql;
    PKPRCB CurrentPrcb;

    trace("KePcr: enter\n");

    if (skip(GetNTVersion() >= _WIN32_WINNT_VISTA,
             "x64 KPCR layout test requires NT 6+\n"))
    {
        return;
    }

    PcrFromSeg = (ULONG_PTR)__readgsqword(0x18);
    ok(PcrFromSeg != 0, "GS:[0x18] (KPCR.Self) is NULL\n");

    Pcr = (PKPCR)PcrFromSeg;
    trace("KePcr: PcrFromSeg=%p\n", (PVOID)PcrFromSeg);

    ok(Pcr->NtTib.Self != NULL, "Pcr->NtTib.Self is NULL\n");
    trace("KePcr: NtTib.Self=%p\n", Pcr->NtTib.Self);

    {
        PKPCR PcrInline = KeGetPcr();
        ok_eq_pointer((PVOID)PcrInline, (PVOID)Pcr);
    }
    CurrentPrcb = Pcr->CurrentPrcb;
    trace("KePcr: CurrentPrcb=%p\n", CurrentPrcb);

    ok(CurrentPrcb != NULL, "CurrentPrcb is NULL\n");
    ok((ULONG_PTR)CurrentPrcb >= (ULONG_PTR)Pcr,
       "CurrentPrcb %p < KPCR %p\n", CurrentPrcb, Pcr);
    ok((ULONG_PTR)CurrentPrcb < (ULONG_PTR)Pcr + 0x6000,
       "CurrentPrcb %p too far from KPCR %p\n", CurrentPrcb, Pcr);

    CurrentIrql = KeGetCurrentIrql();
    ok_eq_uint(CurrentIrql, PASSIVE_LEVEL);

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    KeLowerIrql(OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);
    trace("KePcr: IRQL transitions OK\n");

    if (Pcr->KdVersionBlock == NULL)
        trace("KdVersionBlock is NULL (acceptable on some kernels)\n");
    else
        trace("KdVersionBlock = %p\n", Pcr->KdVersionBlock);

    ProcNum = KeGetCurrentProcessorNumber();
    trace("KePcr: ProcNum from helper = %lu\n", ProcNum);

    /* On UP guests, reading the embedded PRCB's Number is sufficient. */
    if (KmtIsMultiProcessorBuild && KeNumberProcessors > 1)
    {
        KAFFINITY OneCpu = (KAFFINITY)1 << ProcNum;
        KeSetSystemAffinityThread(OneCpu);
        ProcNum = KeGetCurrentProcessorNumber();
        ok_eq_ulong(ProcNum, (ULONG)CurrentPrcb->Number);
        KeRevertToUserAffinityThread();
    }
    else
    {
        ok_eq_ulong(ProcNum, 0u);
        ok_eq_ulong(ProcNum, (ULONG)CurrentPrcb->Number);
    }

    trace("KPCR=%p Self=%p Prcb=%p ProcessorNumber=%lu\n",
          Pcr, Pcr->NtTib.Self, CurrentPrcb, ProcNum);
#endif
}
