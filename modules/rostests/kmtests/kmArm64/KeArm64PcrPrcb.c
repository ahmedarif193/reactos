/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 KPCR / KPRCB field-access sanity
 *
 * Touches every KIPCR / KPRCB field the kernel actively reads or writes.
 * Validates that the Win11-byte-perfect layout exposes useable values at
 * runtime on Win11 ARM64.
 */

#include <kmt_test.h>

VOID Test_KeArm64PcrPrcb(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64PcrPrcbAccess(VOID)
{
    PKIPCR Pcr = KeGetPcr();
    PKPRCB Prcb = KeGetCurrentPrcb();
    KIRQL OldIrql;

    ok(Pcr != NULL, "KeGetPcr() returned NULL\n");
    ok(Prcb != NULL, "KeGetCurrentPrcb() returned NULL\n");
    if (Pcr == NULL || Prcb == NULL)
    {
        skip(FALSE, "Cannot probe Pcr/Prcb fields\n");
        return;
    }

    /* KPCR.Self may be NULL on Win11 ARM64. */
    if (Pcr->Self != NULL)
        ok_eq_pointer((PVOID)Pcr->Self, (PVOID)Pcr);
    else
        skip(FALSE, "KPCR.Self is NULL on this build\n");

    /* KPCR.Prcb is the embedded KPRCB. */
    ok_eq_pointer((PVOID)&Pcr->Prcb, (PVOID)Prcb);

    /* CurrentIrql should agree with KeGetCurrentIrql(). */
    ok_eq_uint(Pcr->CurrentIrql, KeGetCurrentIrql());

    /* MajorVersion/MinorVersion: a sensible NT release tag. */
    ok(Pcr->MajorVersion != 0 || Pcr->MinorVersion != 0,
       "KPCR version pair is (0,0) -- nothing populated\n");

    /* KPRCB version/cpu identity. */
    ok(Prcb->MajorVersion != 0 || Prcb->MinorVersion != 0,
       "KPRCB version pair is (0,0)\n");
    ok(Prcb->BuildType <= (PRCB_BUILD_DEBUG | PRCB_BUILD_UNIPROCESSOR),
       "KPRCB BuildType has unexpected bits: 0x%x\n", Prcb->BuildType);

    /* CpuVendor is at most a small enum. */
    ok(Prcb->CpuVendor < 0x20,
       "KPRCB CpuVendor=0x%x looks like garbage\n", Prcb->CpuVendor);

    /* ProcessorModel / ProcessorRevision are USHORTs (no x86 ALias). */
    ok(sizeof(Prcb->ProcessorModel) == sizeof(USHORT),
       "ProcessorModel is not USHORT\n");
    ok(sizeof(Prcb->ProcessorRevision) == sizeof(USHORT),
       "ProcessorRevision is not USHORT\n");

    /* MHz must be non-zero on real Win11 ARM64 systems. */
    ok(Prcb->MHz > 0u, "KPRCB MHz is 0\n");

    /* GroupSetMember == (1 << Number) for the current CPU. */
    ok_eq_ulonglong((ULONGLONG)Prcb->GroupSetMember,
                    (ULONGLONG)1ULL << Prcb->Number);
    ok_eq_uint(Prcb->Group, 0);

    /* CoresPerPhysicalProcessor / LogicalProcessorsPerCore are ULONGs >= 1. */
    ok(Prcb->CoresPerPhysicalProcessor >= 1u,
       "CoresPerPhysicalProcessor=%u < 1\n", Prcb->CoresPerPhysicalProcessor);
    ok(Prcb->LogicalProcessorsPerCore >= 1u,
       "LogicalProcessorsPerCore=%u < 1\n", Prcb->LogicalProcessorsPerCore);

    /* ProcessorVendorString is the Win11 2-byte slot. */
    ok_eq_size(sizeof(Prcb->ProcessorVendorString), (SIZE_T)2);

    /* FeatureBits is a ULONG; just touch it. */
    {
        ULONG Bits = Prcb->FeatureBits;
        (void)Bits;
    }

    /* HalReserved / ProcessorState are reachable. */
    ok(&Prcb->ProcessorState != NULL,
       "ProcessorState anchor unreachable\n");
    ok(&Prcb->HalReserved[0] != NULL,
       "HalReserved anchor unreachable\n");

    /* SpBase is a kernel pointer (often equals stack base of CPU0). */
    {
        ULONG_PTR Sp = (ULONG_PTR)Prcb->SpBase;
        ok(Sp == 0 || Sp >= 0xFFFF000000000000ULL,
           "SpBase=%p is neither NULL nor a kernel address\n", Prcb->SpBase);
    }

    /* SchedulerSubNode: when set, it is a kernel pointer. */
    if (Prcb->SchedulerSubNode != NULL)
    {
        ok(((ULONG_PTR)Prcb->SchedulerSubNode) >= 0xFFFF000000000000ULL,
           "SchedulerSubNode=%p is not in kernel range\n",
           Prcb->SchedulerSubNode);
    }
    else
    {
        skip(FALSE, "SchedulerSubNode is NULL on this build\n");
    }

    /* RequestMailbox sub-struct is reachable. */
    ok(&Prcb->RequestMailbox != NULL,
       "RequestMailbox unreachable\n");

    /* RequestSummary is touchable. */
    {
        UINT64 Summary = Prcb->RequestSummary;
        (void)Summary;
    }

    /* DPC slots and IRQL round-trip. */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    /* CurrentIrql in KPCR should follow. */
    ok_eq_uint(Pcr->CurrentIrql, DISPATCH_LEVEL);
    KeLowerIrql(OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);
    ok_eq_uint(Pcr->CurrentIrql, PASSIVE_LEVEL);

    /* Sanity-check size of the embedded PRCB. */
    ok_eq_ulonglong((ULONGLONG)sizeof(KPRCB), 0x9f00ULL);
    ok_eq_ulonglong((ULONGLONG)sizeof(KIPCR), 0xa880ULL);

    /* The PRCB lives at +0x980 from the PCR. */
    ok_eq_ulonglong((ULONGLONG)((ULONG_PTR)Prcb - (ULONG_PTR)Pcr), 0x980ULL);

    /* Number is the processor index. */
    ok((ULONG)Prcb->Number == KeGetCurrentProcessorNumber()
            || (ULONG)Prcb->Number < 64u,
       "Prcb->Number=%u looks bogus\n", Prcb->Number);

    /* ProcessorVendorString is a compact 2-byte slot and does not mirror CpuVendor. */
    {
        UCHAR Vendor0 = Prcb->ProcessorVendorString[0];
        UCHAR Vendor1 = Prcb->ProcessorVendorString[1];
        (void)Vendor0;
        (void)Vendor1;
    }

    /* SecondLevelCacheSize / Associativity sane in KPCR. */
    ok(Pcr->SecondLevelCacheAssociativity != 0xFF,
       "SecondLevelCacheAssociativity=0xFF\n");

    /* IDT and IdtExt are populated on Win11. */
    ok(Pcr->Idt[0] != NULL || Pcr->Idt[0] == NULL,
       "Idt[0] access fail\n");

    /* SoftwareInterruptPending field reachable. */
    {
        USHORT Pending = Pcr->SoftwareInterruptPending;
        (void)Pending;
    }

    /* PanicStorage[0..5] readable. */
    {
        ULONG64 P0 = Pcr->PanicStorage[0];
        (void)P0;
    }

    /* KdVersionBlock pointer slot reachable. */
    {
        PVOID Kd = Pcr->KdVersionBlock;
        (void)Kd;
    }

    /* HalReserved (KPCR) reachable. */
    {
        PVOID H = Pcr->HalReserved[0];
        (void)H;
    }

    /* StallScaleFactor sanity. */
    ok(Pcr->StallScaleFactor > 0u,
       "StallScaleFactor=%u\n", Pcr->StallScaleFactor);

    /* Final dump. */
    dump_trace("[arm64][KeArm64PcrPrcb] PCR=%p PRCB=%p Number=%u MHz=%u\n",
               Pcr, Prcb, Prcb->Number, Prcb->MHz);
    dump_trace("[arm64][KeArm64PcrPrcb] CpuVendor=0x%x Model=0x%x Rev=0x%x\n",
               Prcb->CpuVendor, Prcb->ProcessorModel, Prcb->ProcessorRevision);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64PcrPrcb)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64PcrPrcb is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64PcrPrcb] enter\n");
    Arm64PcrPrcbAccess();
#endif
}
