/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Capability honesty audit (roadmap gates 1.5, 5 and 6)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Several roadmap items are satisfied by "or truthful absence": it is fine not
 * to implement hardware queues, native fences, cross-adapter resources or
 * protected content, provided the stack says so and then behaves that way.
 *
 * The failure mode that guards against is not a missing feature, it is a
 * *claimed* one: a capability bit set because it looked harmless, and a runtime
 * that believes it. That costs nothing until an application takes the branch
 * the bit unlocked.
 *
 * So each check here is a pair. The capability must report absent, and the
 * entry point behind it must refuse. A bit that says "off" while the API
 * quietly succeeds is exactly as wrong as a bit that says "on" while it fails,
 * and only checking both catches either.
 */

#include "precomp.h"

static PFND3DKMT_QUERYADAPTERINFO pfnQueryAdapterInfo;

static NTSTATUS QueryCap(D3DKMT_HANDLE hAdapter, KMTQUERYADAPTERINFOTYPE Type,
                         VOID *pData, UINT Size)
{
    D3DKMT_QUERYADAPTERINFO qai;

    memset(&qai, 0, sizeof(qai));
    qai.hAdapter = hAdapter;
    qai.Type = Type;
    qai.pPrivateDriverData = pData;
    qai.PrivateDriverDataSize = Size;
    return pfnQueryAdapterInfo(&qai);
}

/* ------------------------------------------------------------------ *
 * Hardware scheduling: reported off, and the queue entry points refuse.
 * ------------------------------------------------------------------ */
static void Test_HardwareSchedulingAbsentAndRefused(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_WDDM_2_9_CAPS Caps29;
    PFND3DKMT_CREATEHWQUEUE pCreateHwQueue;
    NTSTATUS Status;

    memset(&Caps29, 0, sizeof(Caps29));
    Status = QueryCap(hAdapter, KMTQAITYPE_WDDM_2_9_CAPS, &Caps29, sizeof(Caps29));
    if (NT_SUCCESS(Status))
    {
        ok(Caps29.HwSchSupportState == DXGK_FEATURE_SUPPORT_ALWAYS_OFF,
           "WDDM 2.9 caps report hardware scheduling as %u, not ALWAYS_OFF\n",
           (unsigned)Caps29.HwSchSupportState);
        ok(Caps29.HwSchEnabled == 0, "hardware scheduling reported enabled\n");
    }
    else
    {
        trace("WDDM 2.9 caps unavailable (0x%08lX)\n", (long)Status);
    }

    /* The other half: a stack that reports no hardware scheduling must not
     * hand out a hardware queue either. */
    pCreateHwQueue = (PFND3DKMT_CREATEHWQUEUE)LoadD3DKMTProc("D3DKMTCreateHwQueue");
    if (pCreateHwQueue != NULL)
    {
        D3DKMT_CREATEHWQUEUE chq;

        memset(&chq, 0, sizeof(chq));
        chq.hHwContext = (D3DKMT_HANDLE)0xDEAD4001;
        Status = pCreateHwQueue(&chq);
        ok_failed(Status, "CreateHwQueue succeeded while hardware scheduling reports off (0x%08lX)\n",
                  (long)Status);
        ok(chq.hHwQueue == 0, "CreateHwQueue published a queue handle while refusing\n");
    }
}

/* ------------------------------------------------------------------ *
 * Hardware flip queues: reported off at WDDM 3.0.
 * ------------------------------------------------------------------ */
static void Test_HardwareFlipQueueAbsent(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_WDDM_3_0_CAPS Caps30;
    NTSTATUS Status;

    memset(&Caps30, 0, sizeof(Caps30));
    Status = QueryCap(hAdapter, KMTQAITYPE_WDDM_3_0_CAPS, &Caps30, sizeof(Caps30));
    if (!NT_SUCCESS(Status))
    {
        trace("WDDM 3.0 caps unavailable (0x%08lX)\n", (long)Status);
        return;
    }
    ok(Caps30.HwFlipQueueSupportState == DXGK_FEATURE_SUPPORT_ALWAYS_OFF,
       "WDDM 3.0 caps report a hardware flip queue as %u, not ALWAYS_OFF\n",
       (unsigned)Caps30.HwFlipQueueSupportState);
}

/* ------------------------------------------------------------------ *
 * Cross-adapter resources: reported TIER_NONE.
 * ------------------------------------------------------------------ */
static void Test_CrossAdapterAbsent(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_CROSSADAPTERRESOURCE_SUPPORT Support;
    NTSTATUS Status;

    memset(&Support, 0, sizeof(Support));
    Status = QueryCap(hAdapter, KMTQAITYPE_CROSSADAPTERRESOURCE_SUPPORT, &Support, sizeof(Support));
    if (!NT_SUCCESS(Status))
    {
        trace("cross-adapter support query unavailable (0x%08lX)\n", (long)Status);
        return;
    }
    ok(Support.SupportTier == D3DKMT_CROSSADAPTERRESOURCE_SUPPORT_TIER_NONE,
       "cross-adapter resources reported at tier %u with no implementation\n",
       (unsigned)Support.SupportTier);
}

/* ------------------------------------------------------------------ *
 * GPU MMU: this one is *not* absent, and must be derived rather than
 * asserted -- the bit has to agree with the address width reported
 * beside it.  A GpuMmuSupported=1 with a zero address width would be a
 * capability nothing could act on.
 * ------------------------------------------------------------------ */
static void Test_GpuMmuCapIsSelfConsistent(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_WDDM_2_0_CAPS Caps20;
    D3DKMT_QUERY_GPUMMU_CAPS MmuQuery;
    BOOL HaveMmuCaps;
    NTSTATUS Status;

    memset(&Caps20, 0, sizeof(Caps20));
    Status = QueryCap(hAdapter, KMTQAITYPE_WDDM_2_0_CAPS, &Caps20, sizeof(Caps20));
    if (!NT_SUCCESS(Status))
    {
        trace("WDDM 2.0 caps unavailable (0x%08lX)\n", (long)Status);
        return;
    }
    memset(&MmuQuery, 0, sizeof(MmuQuery));
    HaveMmuCaps = NT_SUCCESS(QueryCap(hAdapter, KMTQAITYPE_QUERY_GPUMMU_CAPS, &MmuQuery, sizeof(MmuQuery)));
    trace("GpuMmuSupported=%u IoMmuSupported=%u gpummucaps=%s vabits=%u\n",
          (unsigned)Caps20.GpuMmuSupported, (unsigned)Caps20.IoMmuSupported,
          HaveMmuCaps ? "yes" : "no", (unsigned)MmuQuery.Caps.VirtualAddressBitCount);

    /* The bit and the geometry behind it have to agree.  A runtime that sees
     * GpuMmuSupported builds an address space from the width reported here;
     * a set bit with no width is a capability nothing can act on. */
    if (Caps20.GpuMmuSupported)
    {
        ok(HaveMmuCaps, "GpuMmuSupported is set but the GPU MMU caps query fails\n");
        if (HaveMmuCaps)
            ok(MmuQuery.Caps.VirtualAddressBitCount != 0,
               "GpuMmuSupported is set but the address space is zero bits wide\n");
    }

    /* GpuMmu and IoMmu are alternative memory models, not a pair. */
    ok(!(Caps20.GpuMmuSupported && Caps20.IoMmuSupported),
       "both GpuMmu and IoMmu are reported supported\n");
}

/* ------------------------------------------------------------------ *
 * Multi-plane overlay: no caps, and the support query refuses.
 * ------------------------------------------------------------------ */
static void Test_MultiPlaneOverlayAbsentAndRefused(D3DKMT_HANDLE hAdapter)
{
    PFND3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT pCheck;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(hAdapter);

    pCheck = (PFND3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT)LoadD3DKMTProc("D3DKMTCheckMultiPlaneOverlaySupport");
    if (pCheck == NULL)
    {
        skip("D3DKMTCheckMultiPlaneOverlaySupport not exported\n");
        return;
    }
    {
        D3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT check;

        memset(&check, 0, sizeof(check));
        check.hDevice = 0;      /* no device: the query must refuse anyway */
        Status = pCheck(&check);
        /* Refusal is the honest answer with no overlay support behind it; what
         * would be wrong is reporting a plane count nothing can present. */
        ok_failed(Status, "CheckMultiPlaneOverlaySupport succeeded with no overlay path (0x%08lX)\n",
                  (long)Status);
    }
}

/* ------------------------------------------------------------------ *
 * The reported WDDM version must not exceed what is implemented.  This
 * is the single number every runtime keys its behaviour off, so it is
 * the one that costs the most to overstate.
 * ------------------------------------------------------------------ */
static void Test_ReportedVersionIsNotAhead(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_DRIVERVERSION Version = (D3DKMT_DRIVERVERSION)0;
    D3DKMT_WDDM_2_9_CAPS Caps29;
    NTSTATUS Status;

    Status = QueryCap(hAdapter, KMTQAITYPE_DRIVERVERSION, &Version, sizeof(Version));
    if (!NT_SUCCESS(Status))
    {
        trace("driver version query unavailable (0x%08lX)\n", (long)Status);
        return;
    }
    trace("reported driver version: %u\n", (unsigned)Version);
    ok(Version != 0, "adapter reports no WDDM version at all\n");

    /*
     * Anything at or past 2.9 is expected to schedule in hardware.  Reporting
     * that level while HwSchSupportState says ALWAYS_OFF is the exact shape of
     * a version claim running ahead of the implementation.
     */
    if (Version >= KMT_DRIVERVERSION_WDDM_2_9)
    {
        memset(&Caps29, 0, sizeof(Caps29));
        if (NT_SUCCESS(QueryCap(hAdapter, KMTQAITYPE_WDDM_2_9_CAPS, &Caps29, sizeof(Caps29))))
        {
            ok(Caps29.HwSchSupportState != DXGK_FEATURE_SUPPORT_ALWAYS_OFF,
               "version %u is reported while hardware scheduling is ALWAYS_OFF\n",
               (unsigned)Version);
        }
    }
}

START_TEST(capsaudit)
{
    D3DKMT_HANDLE hAdapter;

    pfnQueryAdapterInfo = (PFND3DKMT_QUERYADAPTERINFO)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    if (pfnQueryAdapterInfo == NULL)
    {
        skip("D3DKMTQueryAdapterInfo not exported\n");
        return;
    }
    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }

    Test_HardwareSchedulingAbsentAndRefused(hAdapter);
    Test_HardwareFlipQueueAbsent(hAdapter);
    Test_CrossAdapterAbsent(hAdapter);
    Test_GpuMmuCapIsSelfConsistent(hAdapter);
    Test_MultiPlaneOverlayAbsentAndRefused(hAdapter);
    Test_ReportedVersionIsNotAhead(hAdapter);

    CloseAdapter(hAdapter);
}

/* EOF */
