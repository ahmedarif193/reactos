/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for D3DKMT present and render operations
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests: D3DKMTPresent, D3DKMTRender, D3DKMTGetPresentHistory
 */

#include "precomp.h"

static void Test_Present_NullParam(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTPresent);

    EXPECT_NULL_REJECTED(pfnD3DKMTPresent, "D3DKMTPresent");
}

static void Test_Present_InvalidDevice(void)
{
    D3DKMT_PRESENT PresentData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTPresent);

    memset(&PresentData, 0, sizeof(PresentData));
    PresentData.hDevice = 0xBAD0CAFE;
    PresentData.Flags.Blt = 1;
    Status = pfnD3DKMTPresent(&PresentData);
    ok_failed(Status,
       "Present with invalid device should fail, got 0x%lx\n", Status);
}

static void Test_Present_NoSourceNoColor(void)
{
    D3DKMT_PRESENT PresentData;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTPresent);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for present test\n");
        return;
    }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("Cannot create device for present test\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* Present with no source allocation and no color fill */
    memset(&PresentData, 0, sizeof(PresentData));
    PresentData.hDevice = hDevice;
    PresentData.hSource = 0;
    PresentData.Flags.Value = 0;
    Status = pfnD3DKMTPresent(&PresentData);
    /* This should fail since no valid operation is specified */
    if (Status != STATUS_PROCEDURE_NOT_FOUND)
    {
        ok_failed(Status,
           "Present with no flags should fail, got 0x%lx\n", Status);
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void Test_Render_NullParam(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTRender);

    EXPECT_NULL_REJECTED(pfnD3DKMTRender, "D3DKMTRender");
}

static void Test_Render_InvalidContext(void)
{
    D3DKMT_RENDER RenderData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTRender);

    memset(&RenderData, 0, sizeof(RenderData));
    RenderData.hContext = 0xBAD0CAFE;
    Status = pfnD3DKMTRender(&RenderData);
    ok_failed(Status,
       "Render with invalid context should fail, got 0x%lx\n", Status);
}

static void Test_GetPresentHistory_NullParam(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTGetPresentHistory);

    EXPECT_NULL_REJECTED(pfnD3DKMTGetPresentHistory, "D3DKMTGetPresentHistory");
}

static void Test_GetPresentHistory_InvalidDevice(void)
{
    D3DKMT_GETPRESENTHISTORY HistData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTGetPresentHistory);

    memset(&HistData, 0, sizeof(HistData));
    HistData.hAdapter = (D3DKMT_HANDLE)0xBAD0CAFE;
    Status = pfnD3DKMTGetPresentHistory(&HistData);
    ok_failed(Status,
       "GetPresentHistory with invalid device should fail, got 0x%lx\n",
       Status);
}

START_TEST(D3dkmtPresent)
{
    Test_Present_NullParam();
    Test_Present_InvalidDevice();
    Test_Present_NoSourceNoColor();
    Test_Render_NullParam();
    Test_Render_InvalidContext();
    Test_GetPresentHistory_NullParam();
    Test_GetPresentHistory_InvalidDevice();
}
