/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for D3DKMT display management
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests: D3DKMTSetVidPnSourceOwner, D3DKMTGetDisplayModeList,
 *        D3DKMTSetDisplayMode, D3DKMTWaitForVerticalBlankEvent,
 *        D3DKMTGetScanLine, D3DKMTPollDisplayChildren,
 *        D3DKMTInvalidateActiveVidPn, D3DKMTSetGammaRamp, D3DKMTEscape
 */

#include "precomp.h"

static void Test_GetDisplayModeList(void)
{
    D3DKMT_GETDISPLAYMODELIST ModeList;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTGetDisplayModeList);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTGetDisplayModeList, "D3DKMTGetDisplayModeList");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for display mode list test\n");
        return;
    }

    /* Query mode count first (pModeList = NULL, ModeCount = 0) */
    memset(&ModeList, 0, sizeof(ModeList));
    ModeList.hAdapter = hAdapter;
    ModeList.VidPnSourceId = 0;
    ModeList.pModeList = NULL;
    ModeList.ModeCount = 0;

    Status = pfnD3DKMTGetDisplayModeList(&ModeList);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTGetDisplayModeList not implemented\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* STATUS_BUFFER_TOO_SMALL is expected when pModeList is NULL but modes exist */
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        ok(ModeList.ModeCount > 0,
           "STATUS_BUFFER_TOO_SMALL but ModeCount is 0\n");

        /* Now allocate and query */
        if (ModeList.ModeCount > 0 && ModeList.ModeCount < 1024)
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
                ok_succeeded(Status,
                   "D3DKMTGetDisplayModeList with buffer failed 0x%lx\n", Status);

                if (NT_SUCCESS(Status) && ModeList.ModeCount > 0)
                {
                    ok(pModes[0].Width > 0,
                       "First mode width is 0\n");
                    ok(pModes[0].Height > 0,
                       "First mode height is 0\n");
                }

                LocalFree(pModes);
            }
        }
    }
    else if (NT_SUCCESS(Status))
    {
        trace("GetDisplayModeList returned %lu modes\n", ModeList.ModeCount);
    }

    /* Invalid adapter */
    memset(&ModeList, 0, sizeof(ModeList));
    ModeList.hAdapter = 0xBAD0CAFE;
    ModeList.VidPnSourceId = 0;
    Status = pfnD3DKMTGetDisplayModeList(&ModeList);
    ok_failed(Status,
       "GetDisplayModeList with invalid adapter should fail, got 0x%lx\n",
       Status);

    CloseAdapter(hAdapter);
}

static void Test_SetVidPnSourceOwner(void)
{
    D3DKMT_SETVIDPNSOURCEOWNER OwnerData;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTSetVidPnSourceOwner);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTSetVidPnSourceOwner, "D3DKMTSetVidPnSourceOwner");

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

    /* Release ownership (empty arrays) - should succeed */
    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = hDevice;
    OwnerData.pType = NULL;
    OwnerData.pVidPnSourceId = NULL;
    OwnerData.VidPnSourceCount = 0;
    Status = pfnD3DKMTSetVidPnSourceOwner(&OwnerData);
    /* Releasing with no prior ownership is fine */
    if (Status != STATUS_PROCEDURE_NOT_FOUND)
    {
        ok_succeeded(Status,
           "SetVidPnSourceOwner (release) failed with 0x%lx\n", Status);
    }

    /* Invalid device */
    memset(&OwnerData, 0, sizeof(OwnerData));
    OwnerData.hDevice = 0xBAD0CAFE;
    OwnerData.VidPnSourceCount = 0;
    Status = pfnD3DKMTSetVidPnSourceOwner(&OwnerData);
    ok_failed(Status,
       "SetVidPnSourceOwner with invalid device should fail, got 0x%lx\n",
       Status);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void Test_SetDisplayMode(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTSetDisplayMode);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTSetDisplayMode, "D3DKMTSetDisplayMode");
}

static void Test_WaitForVerticalBlankEvent(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTWaitForVerticalBlankEvent);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTWaitForVerticalBlankEvent, "D3DKMTWaitForVerticalBlankEvent");
}

static void Test_GetScanLine(void)
{
    D3DKMT_GETSCANLINE ScanData;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTGetScanLine);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTGetScanLine, "D3DKMTGetScanLine");

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
    /* May succeed or return not-implemented */
    if (NT_SUCCESS(Status))
    {
        trace("ScanLine: InVerticalBlank=%d, ScanLine=%lu\n",
              ScanData.InVerticalBlank, ScanData.ScanLine);
    }

    /* Invalid adapter */
    memset(&ScanData, 0, sizeof(ScanData));
    ScanData.hAdapter = 0xBAD0CAFE;
    Status = pfnD3DKMTGetScanLine(&ScanData);
    ok_failed(Status,
       "GetScanLine with invalid adapter should fail, got 0x%lx\n", Status);

    CloseAdapter(hAdapter);
}

static void Test_PollDisplayChildren(void)
{
    D3DKMT_POLLDISPLAYCHILDREN PollData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTPollDisplayChildren);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTPollDisplayChildren, "D3DKMTPollDisplayChildren");

    /* Invalid adapter */
    memset(&PollData, 0, sizeof(PollData));
    PollData.hAdapter = 0xBAD0CAFE;
    Status = pfnD3DKMTPollDisplayChildren(&PollData);
    ok_failed(Status,
       "PollDisplayChildren with invalid adapter should fail, got 0x%lx\n",
       Status);

    memset(&PollData, 0, sizeof(PollData));
    PollData.hAdapter = 0xBAD0CAFE;
    PollData.PollAllAdapters = 1;
    Status = pfnD3DKMTPollDisplayChildren(&PollData);
    ok_failed(Status, "PollDisplayChildren PollAllAdapters accepted an invalid anchor adapter, status 0x%lx\n", Status);

    /* Reserved flag bits are rejected before adapter selection. */
    memset(&PollData, 0, sizeof(PollData));
    PollData.hAdapter = 0xBAD0CAFE;
    PollData.Reserved = 1;
    Status = pfnD3DKMTPollDisplayChildren(&PollData);
    ok(Status == STATUS_INVALID_PARAMETER, "PollDisplayChildren with reserved flags returned 0x%lx\n", Status);

    memset(&PollData, 0, sizeof(PollData));
    PollData.hAdapter = 0xBAD0CAFE;
    PollData.DisableModeReset = 1;
    Status = pfnD3DKMTPollDisplayChildren(&PollData);
    ok(Status == STATUS_INVALID_PARAMETER, "PollDisplayChildren accepted DisableModeReset without SynchronousPolling, status 0x%lx\n", Status);
}

static void Test_InvalidateActiveVidPn(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTInvalidateActiveVidPn);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTInvalidateActiveVidPn, "D3DKMTInvalidateActiveVidPn");
}

static void Test_SetGammaRamp(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTSetGammaRamp);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTSetGammaRamp, "D3DKMTSetGammaRamp");
}

static void Test_Escape(void)
{
    D3DKMT_ESCAPE EscapeData;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTEscape);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTEscape, "D3DKMTEscape");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for Escape test\n");
        return;
    }

    /* Escape with no data */
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter = hAdapter;
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.pPrivateDriverData = NULL;
    EscapeData.PrivateDriverDataSize = 0;
    Status = pfnD3DKMTEscape(&EscapeData);
    /* May succeed (driver-specific) or fail */
    trace("Escape(DRIVERPRIVATE, NULL, 0) returned 0x%lx\n", Status);

    /* Invalid adapter */
    memset(&EscapeData, 0, sizeof(EscapeData));
    EscapeData.hAdapter = 0xBAD0CAFE;
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Status = pfnD3DKMTEscape(&EscapeData);
    ok_failed(Status,
       "Escape with invalid adapter should fail, got 0x%lx\n", Status);

    CloseAdapter(hAdapter);
}

START_TEST(D3dkmtDisplay)
{
    Test_GetDisplayModeList();
    Test_SetVidPnSourceOwner();
    Test_SetDisplayMode();
    Test_WaitForVerticalBlankEvent();
    Test_GetScanLine();
    Test_PollDisplayChildren();
    Test_InvalidateActiveVidPn();
    Test_SetGammaRamp();
    Test_Escape();
}
