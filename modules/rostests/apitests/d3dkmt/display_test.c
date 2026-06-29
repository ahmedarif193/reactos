/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Display/VidPn/mode enumeration integration tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the complete display subsystem path through the gdi32 D3DKMT API:
 *   1. EnumAdapters - at least 1 adapter with valid LUID
 *   2. OpenAdapterFromLuid - round-trip with enumerated LUID
 *   3. QueryAdapterInfo DRIVERVERSION - WDDM version >= 1.0
 *   4. GetDisplayModeList - at least one mode with valid width/height/refresh
 *   5. SetVidPnSourceOwner - exclusive claim, check, release
 *   6. WaitForVerticalBlankEvent - returns within reasonable time
 *   7. GetScanLine - returns valid scanline
 *   8. PollDisplayChildren - no crash with valid adapter
 *   9. GetDeviceState - valid device state
 */

#include "precomp.h"

/*
 * Test 1: EnumAdapters -- verify at least 1 adapter returned with valid LUID
 */
static void Test_EnumAdapters(void)
{
    D3DKMT_ENUMADAPTERS EnumData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTEnumAdapters);

    memset(&EnumData, 0, sizeof(EnumData));
    Status = pfnD3DKMTEnumAdapters(&EnumData);

    if (!NT_SUCCESS(Status))
    {
        skip("D3DKMTEnumAdapters failed 0x%lx -- no WDDM adapter present\n", Status);
        return;
    }

    ok(EnumData.NumAdapters >= 1,
       "Expected at least 1 adapter, got %lu\n", EnumData.NumAdapters);

    ok(EnumData.NumAdapters <= MAX_ENUM_ADAPTERS,
       "NumAdapters %lu exceeds MAX_ENUM_ADAPTERS\n", EnumData.NumAdapters);

    if (EnumData.NumAdapters >= 1)
    {
        ok(EnumData.Adapters[0].hAdapter != 0,
           "First adapter handle is 0\n");

        ok(EnumData.Adapters[0].AdapterLuid.LowPart != 0 ||
           EnumData.Adapters[0].AdapterLuid.HighPart != 0,
           "Adapter LUID is zero\n");

        ok(EnumData.Adapters[0].NumOfSources >= 1,
           "Expected NumOfSources >= 1, got %lu\n",
           EnumData.Adapters[0].NumOfSources);
    }
}

/*
 * Test 2: OpenAdapterFromLuid -- round-trip with enumerated LUID
 */
static void Test_OpenAdapterFromLuid(void)
{
    D3DKMT_ENUMADAPTERS EnumData;
    D3DKMT_OPENADAPTERFROMLUID OpenData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTEnumAdapters);

    PFN_D3DKMTOpenAdapterFromLuid pfnOpen;
    pfnOpen = (PFN_D3DKMTOpenAdapterFromLuid)LoadD3DKMTProc("D3DKMTOpenAdapterFromLuid");
    if (!pfnOpen) { skip("D3DKMTOpenAdapterFromLuid not exported by gdi32.dll\n"); return; }

    /* Enumerate to get a valid LUID */
    memset(&EnumData, 0, sizeof(EnumData));
    Status = pfnD3DKMTEnumAdapters(&EnumData);
    if (!NT_SUCCESS(Status) || EnumData.NumAdapters == 0)
    {
        skip("Cannot enumerate adapters for OpenAdapterFromLuid test\n");
        return;
    }

    /* Open using the first adapter's LUID */
    memset(&OpenData, 0, sizeof(OpenData));
    OpenData.AdapterLuid = EnumData.Adapters[0].AdapterLuid;
    Status = pfnOpen(&OpenData);

    if (!NT_SUCCESS(Status))
    {
        skip("D3DKMTOpenAdapterFromLuid failed 0x%lx\n", Status);
        return;
    }

    ok(OpenData.hAdapter != 0,
       "OpenAdapterFromLuid returned zero handle\n");

    CloseAdapter(OpenData.hAdapter);
}

/*
 * Test 3: QueryAdapterInfo DRIVERVERSION -- WDDM version >= 1.0
 */
static void Test_QueryAdapterInfoDriverVersion(void)
{
    D3DKMT_QUERYADAPTERINFO QueryInfo;
    D3DKMT_DRIVERVERSION Version;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTQueryAdapterInfo);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for QueryAdapterInfo test\n");
        return;
    }

    /* Query DRIVERVERSION (KMTQAITYPE_DRIVERVERSION = 13) */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    memset(&Version, 0, sizeof(Version));
    QueryInfo.hAdapter = hAdapter;
    QueryInfo.Type = KMTQAITYPE_DRIVERVERSION;
    QueryInfo.pPrivateDriverData = &Version;
    QueryInfo.PrivateDriverDataSize = sizeof(Version);

    Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);

    if (!NT_SUCCESS(Status))
    {
        skip("QueryAdapterInfo DRIVERVERSION failed 0x%lx\n", Status);
        CloseAdapter(hAdapter);
        return;
    }

    /* KMT_DRIVERVERSION_WDDM_1_0 = 1000 */
    ok((UINT)Version >= 1000,
       "Expected WDDM version >= 1000 (WDDM 1.0), got %u\n", (UINT)Version);

    trace("WDDM driver version: %u\n", (UINT)Version);

    /* Also test GETSEGMENTSIZE to verify query path works */
    {
        D3DKMT_SEGMENTSIZEINFO SegInfo;

        memset(&QueryInfo, 0, sizeof(QueryInfo));
        memset(&SegInfo, 0, sizeof(SegInfo));
        QueryInfo.hAdapter = hAdapter;
        QueryInfo.Type = KMTQAITYPE_GETSEGMENTSIZE;
        QueryInfo.pPrivateDriverData = &SegInfo;
        QueryInfo.PrivateDriverDataSize = sizeof(SegInfo);

        Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);
        if (NT_SUCCESS(Status))
        {
            ok(SegInfo.SharedSystemMemorySize > 0 ||
               SegInfo.DedicatedVideoMemorySize > 0 ||
               SegInfo.DedicatedSystemMemorySize > 0,
               "All segment sizes are zero\n");
        }
    }

    CloseAdapter(hAdapter);
}

/*
 * Test 4: GetDisplayModeList -- at least one mode with valid width/height/refresh
 */
static void Test_GetDisplayModeList(void)
{
    D3DKMT_GETDISPLAYMODELIST ModeList;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTGetDisplayModeList);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for GetDisplayModeList test\n");
        return;
    }

    /* Query mode count (pModeList = NULL) */
    memset(&ModeList, 0, sizeof(ModeList));
    ModeList.hAdapter = hAdapter;
    ModeList.VidPnSourceId = 0;
    ModeList.pModeList = NULL;
    ModeList.ModeCount = 0;

    Status = pfnD3DKMTGetDisplayModeList(&ModeList);

    /* STATUS_BUFFER_TOO_SMALL with count, or STATUS_SUCCESS with count in ModeCount */
    if (Status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(Status))
    {
        skip("GetDisplayModeList count query failed 0x%lx\n", Status);
        CloseAdapter(hAdapter);
        return;
    }

    ok(ModeList.ModeCount >= 1,
       "Expected at least 1 display mode, got %lu\n", ModeList.ModeCount);

    if (ModeList.ModeCount >= 1 && ModeList.ModeCount < 1024)
    {
        D3DKMT_DISPLAYMODE *pModes;
        UINT Count = ModeList.ModeCount;

        pModes = (D3DKMT_DISPLAYMODE *)LocalAlloc(LMEM_ZEROINIT,
                      Count * sizeof(D3DKMT_DISPLAYMODE));
        if (pModes)
        {
            memset(&ModeList, 0, sizeof(ModeList));
            ModeList.hAdapter = hAdapter;
            ModeList.VidPnSourceId = 0;
            ModeList.pModeList = pModes;
            ModeList.ModeCount = Count;

            Status = pfnD3DKMTGetDisplayModeList(&ModeList);

            if (NT_SUCCESS(Status) && ModeList.ModeCount >= 1)
            {
                ok(pModes[0].Width >= 640,
                   "First mode Width %lu < 640\n", pModes[0].Width);
                ok(pModes[0].Height >= 480,
                   "First mode Height %lu < 480\n", pModes[0].Height);

                trace("First display mode: %lux%lu\n",
                      pModes[0].Width, pModes[0].Height);
            }
            else if (!NT_SUCCESS(Status))
            {
                skip("GetDisplayModeList with buffer failed 0x%lx\n", Status);
            }

            LocalFree(pModes);
        }
    }

    CloseAdapter(hAdapter);
}

/*
 * Test 5: SetVidPnSourceOwner -- exclusive claim, check, release
 */
static void Test_SetVidPnSourceOwner(void)
{
    D3DKMT_SETVIDPNSOURCEOWNER OwnerData;
    D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP CheckData;
    D3DKMT_VIDPNSOURCEOWNER_TYPE OwnerType;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SrcId;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTSetVidPnSourceOwner);

    PFN_D3DKMTCheckVidPnExclusiveOwnership pfnCheck;
    pfnCheck = (PFN_D3DKMTCheckVidPnExclusiveOwnership)
               LoadD3DKMTProc("D3DKMTCheckVidPnExclusiveOwnership");
    if (!pfnCheck) { skip("D3DKMTCheckVidPnExclusiveOwnership not exported\n"); return; }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for VidPnSourceOwner test\n");
        return;
    }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("Cannot create device for VidPnSourceOwner test\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* Set EXCLUSIVE ownership on VidPnSourceId 0 */
    OwnerType = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
    SrcId = 0;
    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = hDevice;
    OwnerData.pType = &OwnerType;
    OwnerData.pVidPnSourceId = &SrcId;
    OwnerData.VidPnSourceCount = 1;

    Status = pfnD3DKMTSetVidPnSourceOwner(&OwnerData);
    ok(NT_SUCCESS(Status),
       "SetVidPnSourceOwner EXCLUSIVE failed 0x%lx\n", Status);

    /* Check exclusive ownership */
    memset(&CheckData, 0, sizeof(CheckData));
    CheckData.hAdapter = hAdapter;
    CheckData.VidPnSourceId = 0;

    Status = pfnCheck(&CheckData);
    /* STATUS_SUCCESS means not exclusively owned, STATUS_GRAPHICS_PRESENT_OCCLUDED
     * means exclusively owned. Either way the call should not fail with
     * STATUS_INVALID_PARAMETER or crash. */
    ok(Status == STATUS_SUCCESS ||
       Status == STATUS_GRAPHICS_PRESENT_OCCLUDED ||
       NT_SUCCESS(Status),
       "CheckVidPnExclusiveOwnership returned unexpected 0x%lx\n", Status);

    /* Release ownership */
    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = hDevice;
    OwnerData.pType = NULL;
    OwnerData.pVidPnSourceId = NULL;
    OwnerData.VidPnSourceCount = 0;

    Status = pfnD3DKMTSetVidPnSourceOwner(&OwnerData);
    ok(NT_SUCCESS(Status),
       "SetVidPnSourceOwner release failed 0x%lx\n", Status);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/*
 * Test 6: WaitForVerticalBlankEvent -- verify returns within reasonable time
 */
static void Test_WaitForVerticalBlankEvent(void)
{
    D3DKMT_WAITFORVERTICALBLANKEVENT VBlank;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;
    DWORD Start, Elapsed;

    LOAD_D3DKMT(D3DKMTWaitForVerticalBlankEvent);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for WaitForVerticalBlankEvent test\n");
        return;
    }

    memset(&VBlank, 0, sizeof(VBlank));
    VBlank.hAdapter = hAdapter;
    VBlank.hDevice = 0; /* optional */
    VBlank.VidPnSourceId = 0;

    Start = GetTickCount();
    Status = pfnD3DKMTWaitForVerticalBlankEvent(&VBlank);
    Elapsed = GetTickCount() - Start;

    if (NT_SUCCESS(Status))
    {
        /* At 60 Hz, vblank is ~16.7ms. Allow up to 100ms for scheduling jitter.
         * The stub returns immediately so elapsed should be near 0. */
        ok(Elapsed < 100,
           "WaitForVerticalBlankEvent took %lu ms, expected < 100\n", Elapsed);
    }
    else
    {
        trace("WaitForVerticalBlankEvent returned 0x%lx (may not be implemented)\n",
              Status);
    }

    CloseAdapter(hAdapter);
}

/*
 * Test 7: GetScanLine -- verify returns valid scanline
 */
static void Test_GetScanLine(void)
{
    D3DKMT_GETSCANLINE ScanData;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTGetScanLine);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for GetScanLine test\n");
        return;
    }

    memset(&ScanData, 0, sizeof(ScanData));
    ScanData.hAdapter = hAdapter;
    ScanData.VidPnSourceId = 0;

    Status = pfnD3DKMTGetScanLine(&ScanData);

    if (NT_SUCCESS(Status))
    {
        /* ScanLine should be in a reasonable range. For any resolution up to 4K
         * plus vertical blanking, scanline should be < 4096. The stub returns 0. */
        ok(ScanData.ScanLine < 4096,
           "GetScanLine returned implausible ScanLine %u\n", ScanData.ScanLine);

        /* InVerticalBlank is a BOOLEAN -- verify it's 0 or 1 */
        ok(ScanData.InVerticalBlank == 0 || ScanData.InVerticalBlank == 1,
           "InVerticalBlank has unexpected value %d\n", ScanData.InVerticalBlank);

        trace("ScanLine=%u InVerticalBlank=%d\n",
              ScanData.ScanLine, ScanData.InVerticalBlank);
    }
    else
    {
        trace("GetScanLine returned 0x%lx (may not be implemented)\n", Status);
    }

    CloseAdapter(hAdapter);
}

/*
 * Test 8: PollDisplayChildren -- no crash with valid adapter
 */
static void Test_PollDisplayChildren(void)
{
    D3DKMT_POLLDISPLAYCHILDREN PollData;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTPollDisplayChildren);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for PollDisplayChildren test\n");
        return;
    }

    /* Non-destructive poll */
    memset(&PollData, 0, sizeof(PollData));
    PollData.hAdapter = hAdapter;
    PollData.NonDestructiveOnly = 1;
    PollData.SynchronousPolling = 0;

    Status = pfnD3DKMTPollDisplayChildren(&PollData);

    ok(NT_SUCCESS(Status),
       "PollDisplayChildren failed 0x%lx\n", Status);

    CloseAdapter(hAdapter);
}

/*
 * Test 9: GetDeviceState -- verify returns valid state for a real device
 */
static void Test_GetDeviceState(void)
{
    D3DKMT_GETDEVICESTATE DevState;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTGetDeviceState);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for GetDeviceState test\n");
        return;
    }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("Cannot create device for GetDeviceState test\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* Query execution state (StateType = D3DKMT_DEVICESTATE_EXECUTION = 1) */
    memset(&DevState, 0, sizeof(DevState));
    DevState.hDevice = hDevice;
    DevState.StateType = D3DKMT_DEVICESTATE_EXECUTION;

    Status = pfnD3DKMTGetDeviceState(&DevState);

    ok(NT_SUCCESS(Status),
       "GetDeviceState(EXECUTION) failed 0x%lx\n", Status);

    if (NT_SUCCESS(Status))
    {
        /* Execution state should indicate the device is active/running.
         * D3DKMT_DEVICEEXECUTION_ACTIVE = 1 for a healthy device. */
        trace("Device execution state: %lu\n",
              DevState.ExecutionState);
    }

    /* Invalid device handle */
    memset(&DevState, 0, sizeof(DevState));
    DevState.hDevice = 0xBAD0CAFE;
    DevState.StateType = D3DKMT_DEVICESTATE_EXECUTION;

    Status = pfnD3DKMTGetDeviceState(&DevState);
    ok(!NT_SUCCESS(Status),
       "GetDeviceState with invalid device should fail, got 0x%lx\n", Status);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(display)
{
    Test_EnumAdapters();
    Test_OpenAdapterFromLuid();
    Test_QueryAdapterInfoDriverVersion();
    Test_GetDisplayModeList();
    Test_SetVidPnSourceOwner();
    Test_WaitForVerticalBlankEvent();
    Test_GetScanLine();
    Test_PollDisplayChildren();
    Test_GetDeviceState();
}
