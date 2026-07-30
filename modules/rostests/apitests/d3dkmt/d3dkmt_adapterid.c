/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Adapter identity and the WDDM capability staircase
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Six KMTQAITYPE classes that answer "which adapter is this, and what can it
 * do".  A runtime asks all of them before it commits to an adapter, and each
 * has a failure mode that is silent rather than loud:
 *
 *   ADAPTERADDRESS / PHYSICALADAPTERDEVICEIDS
 *       Used to pair this D3D adapter with a PCI device the caller found some
 *       other way.  A plausible-looking wrong answer pairs the wrong two
 *       things and nothing reports an error.
 *
 *   WDDM_1_2_CAPS / WDDM_1_3_CAPS
 *       The bottom two rungs of the capability staircase.  Every rung from 2.0
 *       up was answered while these returned NOT_SUPPORTED, so a caller walking
 *       upward from 1.2 -- which is where a Win8-era runtime starts -- fell off
 *       at its first step and never saw the rungs above it.
 *
 *   VIRTUALADDRESSINFO
 *       Must agree with WDDM_2_0_CAPS.GpuMmuSupported.  A caller that reserves
 *       GPU VA because one query said yes gets a failure from every later call
 *       if the other said no, and the two queries are asked by different layers.
 *
 *   ADAPTERREGISTRYINFO
 *       Display strings.  Only the adapter string has a real source here; the
 *       rest stay empty rather than invented, and this pins that choice.
 *
 * What is asserted is internal consistency and refusal behaviour, not specific
 * values: the bus location and device IDs are whatever this machine's adapter
 * actually reports, and a test that demanded particular ones would only pass on
 * the machine it was written on.
 */

#include "precomp.h"

static PFND3DKMT_QUERYADAPTERINFO pfnQueryAdapterInfo;

static NTSTATUS AdapterIdQuery(D3DKMT_HANDLE hAdapter, KMTQUERYADAPTERINFOTYPE Type,
                               PVOID Buffer, UINT Size)
{
    D3DKMT_QUERYADAPTERINFO qai;

    memset(&qai, 0, sizeof(qai));
    qai.hAdapter = hAdapter;
    qai.Type = Type;
    qai.pPrivateDriverData = Buffer;
    qai.PrivateDriverDataSize = Size;
    return pfnQueryAdapterInfo(&qai);
}

/* ------------------------------------------------------------------ *
 * Bus location and device identity
 * ------------------------------------------------------------------ */
static void Test_AdapterAddress(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_ADAPTERADDRESS Address;
    D3DKMT_ADAPTERADDRESS Again;
    NTSTATUS Status;

    memset(&Address, 0xCC, sizeof(Address));
    Status = AdapterIdQuery(hAdapter, KMTQAITYPE_ADAPTERADDRESS, &Address, sizeof(Address));
    if (!NT_SUCCESS(Status))
    {
        skip("adapter reports no bus location (0x%08lX)\n", (long)Status);
        return;
    }

    /* PCI is 32 devices of 8 functions.  A value outside that is not a bus
     * location at all -- most likely uninitialised memory returned as one. */
    ok(Address.DeviceNumber < 32, "device number %u is not a PCI device number\n",
       Address.DeviceNumber);
    ok(Address.FunctionNumber < 8, "function number %u is not a PCI function\n",
       Address.FunctionNumber);
    trace("adapter at PCI %u:%u.%u\n", Address.BusNumber, Address.DeviceNumber,
          Address.FunctionNumber);

    /* An adapter does not move.  Two queries disagreeing means one of them is
     * reading something other than the adapter's real location. */
    memset(&Again, 0xCC, sizeof(Again));
    Status = AdapterIdQuery(hAdapter, KMTQAITYPE_ADAPTERADDRESS, &Again, sizeof(Again));
    ok(NT_SUCCESS(Status), "second location query failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        ok(memcmp(&Address, &Again, sizeof(Address)) == 0,
           "adapter reported two different bus locations\n");
    }

    /* Short buffer must be refused, not partially filled: the caller sized it
     * from a struct definition, so a partial write lands past the end. */
    ok_failed(AdapterIdQuery(hAdapter, KMTQAITYPE_ADAPTERADDRESS, &Again, sizeof(Again) - 1),
              "undersized location buffer accepted\n");
}

static void Test_PhysicalAdapterDeviceIds(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_QUERY_DEVICE_IDS Query;
    NTSTATUS Status;

    memset(&Query, 0, sizeof(Query));
    Query.PhysicalAdapterIndex = 0;
    Status = AdapterIdQuery(hAdapter, KMTQAITYPE_PHYSICALADAPTERDEVICEIDS, &Query, sizeof(Query));
    if (!NT_SUCCESS(Status))
    {
        skip("adapter reports no device IDs (0x%08lX)\n", (long)Status);
        return;
    }

    /* 0xFFFF is the bus answering "nothing is there"; 0 is not a vendor.
     * Either one returned as an ID names a device that cannot exist. */
    ok(Query.DeviceIds.VendorID != 0xFFFF && Query.DeviceIds.VendorID != 0,
       "vendor ID 0x%04X is the absent-device answer, not a vendor\n",
       Query.DeviceIds.VendorID);
    trace("adapter is %04X:%04X rev %02X (subsys %04X:%04X)\n",
          Query.DeviceIds.VendorID, Query.DeviceIds.DeviceID, Query.DeviceIds.RevisionID,
          Query.DeviceIds.SubVendorID, Query.DeviceIds.SubSystemID);

    /* An index past the last physical adapter names nothing.  Answering it with
     * adapter 0's IDs is how a caller enumerating physical adapters concludes
     * there are more of them than exist. */
    memset(&Query, 0, sizeof(Query));
    Query.PhysicalAdapterIndex = 0xFFFF;
    ok_failed(AdapterIdQuery(hAdapter, KMTQAITYPE_PHYSICALADAPTERDEVICEIDS, &Query, sizeof(Query)),
              "out-of-range physical adapter index accepted\n");
}

/* ------------------------------------------------------------------ *
 * The capability staircase, from its bottom rung
 * ------------------------------------------------------------------ */
static void Test_CapabilityStaircaseStartsAt_1_2(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_WDDM_1_2_CAPS Caps12;
    D3DKMT_WDDM_1_3_CAPS Caps13;
    D3DKMT_WDDM_2_0_CAPS Caps20;
    NTSTATUS Status12, Status13, Status20;

    memset(&Caps12, 0xCC, sizeof(Caps12));
    Status12 = AdapterIdQuery(hAdapter, KMTQAITYPE_WDDM_1_2_CAPS, &Caps12, sizeof(Caps12));

    memset(&Caps13, 0xCC, sizeof(Caps13));
    Status13 = AdapterIdQuery(hAdapter, KMTQAITYPE_WDDM_1_3_CAPS, &Caps13, sizeof(Caps13));

    memset(&Caps20, 0xCC, sizeof(Caps20));
    Status20 = AdapterIdQuery(hAdapter, KMTQAITYPE_WDDM_2_0_CAPS, &Caps20, sizeof(Caps20));

    /*
     * The staircase must have no gaps below the top rung it claims.  An adapter
     * that answers 2.0 but refuses 1.2 is claiming to be a WDDM 2.0 adapter to
     * a caller that never got far enough to ask.
     */
    if (NT_SUCCESS(Status20))
    {
        ok(NT_SUCCESS(Status12),
           "adapter answers WDDM 2.0 caps but refuses 1.2 (0x%08lX) -- a caller walking "
           "the staircase upward stops at the gap\n", (long)Status12);
        ok(NT_SUCCESS(Status13),
           "adapter answers WDDM 2.0 caps but refuses 1.3 (0x%08lX)\n", (long)Status13);
    }

    if (NT_SUCCESS(Status12))
    {
        trace("WDDM 1.2: NonVGA=%u SmoothRotation=%u PerEngineTDR=%u GammaRamp=%u "
              "HWCursor=%u HWVSync=%u KMCmdBuffer=%u CCD=%u\n",
              Caps12.SupportNonVGA, Caps12.SupportSmoothRotation, Caps12.SupportPerEngineTDR,
              Caps12.SupportGammaRamp, Caps12.SupportHWCursor, Caps12.SupportHWVSync,
              Caps12.SupportKernelModeCommandBuffer, Caps12.SupportCCD);
        /* Reserved bits are how a later version extends the word.  Set ones
         * become someone else's meaning the moment that version ships. */
        ok(Caps12.Reserved == 0, "WDDM 1.2 caps has reserved bits set (0x%08X)\n", Caps12.Value);
        ok_failed(AdapterIdQuery(hAdapter, KMTQAITYPE_WDDM_1_2_CAPS, &Caps12, sizeof(Caps12) - 1),
                  "undersized WDDM 1.2 caps buffer accepted\n");
    }

    if (NT_SUCCESS(Status13))
    {
        trace("WDDM 1.3: Miracast=%u HybridIntegrated=%u HybridDiscrete=%u PStates=%u "
              "VirtualModes=%u CrossAdapter=%u\n",
              Caps13.SupportMiracast, Caps13.IsHybridIntegratedGPU, Caps13.IsHybridDiscreteGPU,
              Caps13.SupportPowerManagementPStates, Caps13.SupportVirtualModes,
              Caps13.SupportCrossAdapterResource);
        ok(Caps13.Reserved == 0, "WDDM 1.3 caps has reserved bits set (0x%08X)\n", Caps13.Value);
        /* One adapter cannot be both halves of a hybrid pair. */
        ok(!(Caps13.IsHybridIntegratedGPU && Caps13.IsHybridDiscreteGPU),
           "adapter claims to be both the integrated and the discrete GPU\n");
    }
}

/*
 * The cross-adapter answer is given by two different queries, asked by
 * different layers.  They have to agree or one layer allocates something the
 * other will refuse to use.
 */
static void Test_CrossAdapterAnswerIsConsistent(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_WDDM_1_3_CAPS Caps13;
    D3DKMT_CROSSADAPTERRESOURCE_SUPPORT Support;
    NTSTATUS CapsStatus, SupportStatus;

    memset(&Caps13, 0, sizeof(Caps13));
    CapsStatus = AdapterIdQuery(hAdapter, KMTQAITYPE_WDDM_1_3_CAPS, &Caps13, sizeof(Caps13));
    memset(&Support, 0, sizeof(Support));
    SupportStatus = AdapterIdQuery(hAdapter, KMTQAITYPE_CROSSADAPTERRESOURCE_SUPPORT,
                                   &Support, sizeof(Support));

    if (!NT_SUCCESS(CapsStatus) || !NT_SUCCESS(SupportStatus))
    {
        skip("cross-adapter support not reported by both queries\n");
        return;
    }
    ok((Caps13.SupportCrossAdapterResource != 0) ==
       (Support.SupportTier != D3DKMT_CROSSADAPTERRESOURCE_SUPPORT_TIER_NONE),
       "WDDM 1.3 caps says cross-adapter=%u but the support tier says %u\n",
       Caps13.SupportCrossAdapterResource, (unsigned)Support.SupportTier);
}

/*
 * Same requirement for GPU virtual addressing, which is asked one way by the
 * memory manager and another by the capability path.
 */
static void Test_VirtualAddressAnswerIsConsistent(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_VIRTUALADDRESSINFO Info;
    D3DKMT_WDDM_2_0_CAPS Caps20;
    NTSTATUS InfoStatus, CapsStatus;

    memset(&Info, 0xCC, sizeof(Info));
    InfoStatus = AdapterIdQuery(hAdapter, KMTQAITYPE_VIRTUALADDRESSINFO, &Info, sizeof(Info));
    if (!NT_SUCCESS(InfoStatus))
    {
        skip("adapter reports no virtual address info (0x%08lX)\n", (long)InfoStatus);
        return;
    }
    ok(Info.VirtualAddressFlags.Reserved == 0,
       "virtual address flags has reserved bits set\n");

    memset(&Caps20, 0, sizeof(Caps20));
    CapsStatus = AdapterIdQuery(hAdapter, KMTQAITYPE_WDDM_2_0_CAPS, &Caps20, sizeof(Caps20));
    if (NT_SUCCESS(CapsStatus))
    {
        ok((Info.VirtualAddressFlags.VirtualAddressSupported != 0) ==
           (Caps20.GpuMmuSupported != 0),
           "VIRTUALADDRESSINFO says GPU VA=%u but WDDM 2.0 caps says GpuMmuSupported=%u -- "
           "a caller that reserves GPU VA on one answer fails on every later call\n",
           Info.VirtualAddressFlags.VirtualAddressSupported, Caps20.GpuMmuSupported);
    }
    trace("GPU virtual addressing supported: %u\n",
          Info.VirtualAddressFlags.VirtualAddressSupported);
}

static void Test_AdapterRegistryInfo(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_ADAPTERREGISTRYINFO Info;
    NTSTATUS Status;

    memset(&Info, 0xCC, sizeof(Info));
    Status = AdapterIdQuery(hAdapter, KMTQAITYPE_ADAPTERREGISTRYINFO, &Info, sizeof(Info));
    if (!NT_SUCCESS(Status))
    {
        skip("adapter reports no registry info (0x%08lX)\n", (long)Status);
        return;
    }

    /* Every field is a fixed-width string the caller will display.  One left
     * unterminated is read past its end by whatever displays it. */
    ok(Info.AdapterString[ARRAYSIZE(Info.AdapterString) - 1] == UNICODE_NULL,
       "adapter string is not terminated\n");
    ok(Info.BiosString[ARRAYSIZE(Info.BiosString) - 1] == UNICODE_NULL,
       "BIOS string is not terminated\n");
    ok(Info.DacType[ARRAYSIZE(Info.DacType) - 1] == UNICODE_NULL,
       "DAC type is not terminated\n");
    ok(Info.ChipType[ARRAYSIZE(Info.ChipType) - 1] == UNICODE_NULL,
       "chip type is not terminated\n");
    trace("adapter string: \"%S\"\n", Info.AdapterString);
}

START_TEST(adapterid)
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

    Test_AdapterAddress(hAdapter);
    Test_PhysicalAdapterDeviceIds(hAdapter);
    Test_CapabilityStaircaseStartsAt_1_2(hAdapter);
    Test_CrossAdapterAnswerIsConsistent(hAdapter);
    Test_VirtualAddressAnswerIsConsistent(hAdapter);
    Test_AdapterRegistryInfo(hAdapter);

    CloseAdapter(hAdapter);
}

/* EOF */
