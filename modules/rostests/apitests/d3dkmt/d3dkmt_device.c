/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for D3DKMT device and context lifecycle
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests: D3DKMTCreateDevice, D3DKMTDestroyDevice, D3DKMTGetDeviceState,
 *        D3DKMTCreateContext, D3DKMTDestroyContext
 */

#include "precomp.h"

static void Test_CreateDestroyDevice(void)
{
    D3DKMT_CREATEDEVICE CreateData;
    D3DKMT_DESTROYDEVICE DestroyData;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateDevice);
    LOAD_D3DKMT(D3DKMTDestroyDevice);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTCreateDevice, "D3DKMTCreateDevice");

    EXPECT_NULL_REJECTED(pfnD3DKMTDestroyDevice, "D3DKMTDestroyDevice");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for device tests\n");
        return;
    }

    /* Valid create */
    memset(&CreateData, 0, sizeof(CreateData));
    CreateData.hAdapter = hAdapter;
    Status = pfnD3DKMTCreateDevice(&CreateData);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTCreateDevice not implemented\n");
        CloseAdapter(hAdapter);
        return;
    }

    ok(NT_SUCCESS(Status),
       "D3DKMTCreateDevice failed with 0x%lx\n", Status);

    if (!NT_SUCCESS(Status))
    {
        CloseAdapter(hAdapter);
        return;
    }

    ok(CreateData.hDevice != 0, "CreateDevice returned zero handle\n");
    ok(CreateData.hDevice != hAdapter,
       "Device handle should differ from adapter handle\n");

    /* Destroy */
    memset(&DestroyData, 0, sizeof(DestroyData));
    DestroyData.hDevice = CreateData.hDevice;
    Status = pfnD3DKMTDestroyDevice(&DestroyData);
    ok(NT_SUCCESS(Status), "DestroyDevice failed with 0x%lx\n", Status);

    /* Double destroy should fail */
    Status = pfnD3DKMTDestroyDevice(&DestroyData);
    ok(!NT_SUCCESS(Status),
       "Double DestroyDevice should fail, got 0x%lx\n", Status);

    CloseAdapter(hAdapter);
}

static void Test_CreateDeviceInvalidAdapter(void)
{
    D3DKMT_CREATEDEVICE CreateData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateDevice);

    /* Invalid adapter handle */
    memset(&CreateData, 0, sizeof(CreateData));
    CreateData.hAdapter = 0xDEADBEEF;
    Status = pfnD3DKMTCreateDevice(&CreateData);
    ok(!NT_SUCCESS(Status),
       "D3DKMTCreateDevice with invalid adapter should fail, got 0x%lx\n", Status);

    /* Zero adapter handle */
    memset(&CreateData, 0, sizeof(CreateData));
    CreateData.hAdapter = 0;
    Status = pfnD3DKMTCreateDevice(&CreateData);
    ok(!NT_SUCCESS(Status),
       "D3DKMTCreateDevice with zero adapter should fail, got 0x%lx\n", Status);
}

static void Test_DestroyDeviceInvalidHandle(void)
{
    D3DKMT_DESTROYDEVICE DestroyData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTDestroyDevice);

    /* Invalid handle */
    memset(&DestroyData, 0, sizeof(DestroyData));
    DestroyData.hDevice = 0xBAD0CAFE;
    Status = pfnD3DKMTDestroyDevice(&DestroyData);
    ok(!NT_SUCCESS(Status),
       "D3DKMTDestroyDevice with invalid handle should fail, got 0x%lx\n", Status);

    /* Zero handle */
    memset(&DestroyData, 0, sizeof(DestroyData));
    DestroyData.hDevice = 0;
    Status = pfnD3DKMTDestroyDevice(&DestroyData);
    ok(!NT_SUCCESS(Status),
       "D3DKMTDestroyDevice with zero handle should fail, got 0x%lx\n", Status);
}

static void Test_MultipleDevices(void)
{
    D3DKMT_CREATEDEVICE CreateData;
    D3DKMT_DESTROYDEVICE DestroyData;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE Devices[4];
    NTSTATUS Status;
    ULONG i, j;

    LOAD_D3DKMT(D3DKMTCreateDevice);
    LOAD_D3DKMT(D3DKMTDestroyDevice);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for multiple device test\n");
        return;
    }

    for (i = 0; i < 4; ++i)
    {
        memset(&CreateData, 0, sizeof(CreateData));
        CreateData.hAdapter = hAdapter;
        Status = pfnD3DKMTCreateDevice(&CreateData);
        if (!NT_SUCCESS(Status))
        {
            skip("CreateDevice #%lu failed (0x%lx)\n", i, Status);
            while (i-- > 0)
            {
                DestroyData.hDevice = Devices[i];
                pfnD3DKMTDestroyDevice(&DestroyData);
            }
            CloseAdapter(hAdapter);
            return;
        }
        Devices[i] = CreateData.hDevice;
    }

    /* All handles unique and non-zero */
    for (i = 0; i < 4; ++i)
    {
        ok(Devices[i] != 0, "Device[%lu] is zero\n", i);
        for (j = i + 1; j < 4; ++j)
        {
            ok(Devices[i] != Devices[j],
               "Device[%lu] and Device[%lu] have same handle 0x%lx\n",
               i, j, (unsigned long)Devices[i]);
        }
    }

    for (i = 0; i < 4; ++i)
    {
        DestroyData.hDevice = Devices[i];
        Status = pfnD3DKMTDestroyDevice(&DestroyData);
        ok(NT_SUCCESS(Status), "DestroyDevice[%lu] failed\n", i);
    }

    CloseAdapter(hAdapter);
}

static void Test_GetDeviceState(void)
{
    D3DKMT_GETDEVICESTATE StateData;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTGetDeviceState);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTGetDeviceState, "D3DKMTGetDeviceState");

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

    /* Query execution state */
    memset(&StateData, 0, sizeof(StateData));
    StateData.hDevice = hDevice;
    StateData.StateType = D3DKMT_DEVICESTATE_EXECUTION;
    Status = pfnD3DKMTGetDeviceState(&StateData);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTGetDeviceState not implemented\n");
    }
    else if (NT_SUCCESS(Status))
    {
        ok(StateData.ExecutionState == D3DKMT_DEVICEEXECUTION_ACTIVE ||
           StateData.ExecutionState == D3DKMT_DEVICEEXECUTION_STOPPED ||
           StateData.ExecutionState == D3DKMT_DEVICEEXECUTION_RESET ||
           StateData.ExecutionState == D3DKMT_DEVICEEXECUTION_ERROR_OUTOFMEMORY ||
           StateData.ExecutionState == D3DKMT_DEVICEEXECUTION_HUNG,
           "Unexpected execution state %d\n", StateData.ExecutionState);
    }

    /* Invalid device handle */
    memset(&StateData, 0, sizeof(StateData));
    StateData.hDevice = 0xBAD0CAFE;
    StateData.StateType = D3DKMT_DEVICESTATE_EXECUTION;
    Status = pfnD3DKMTGetDeviceState(&StateData);
    ok(!NT_SUCCESS(Status),
       "GetDeviceState with invalid device should fail, got 0x%lx\n", Status);

    memset(&StateData, 0, sizeof(StateData));
    StateData.hDevice = hDevice;
    StateData.StateType = (D3DKMT_DEVICESTATE_TYPE)0x7fffffff;
    Status = pfnD3DKMTGetDeviceState(&StateData);
    ok(Status == STATUS_INVALID_PARAMETER, "GetDeviceState with invalid state type returned 0x%lx, expected STATUS_INVALID_PARAMETER\n", Status);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void Test_CreateDestroyContext(void)
{
    D3DKMT_CREATECONTEXT CreateCtx;
    D3DKMT_DESTROYCONTEXT DestroyCtx;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateContext);
    LOAD_D3DKMT(D3DKMTDestroyContext);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTCreateContext, "D3DKMTCreateContext");

    EXPECT_NULL_REJECTED(pfnD3DKMTDestroyContext, "D3DKMTDestroyContext");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for context tests\n");
        return;
    }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("Cannot create device for context tests\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* Create context on device */
    memset(&CreateCtx, 0, sizeof(CreateCtx));
    CreateCtx.hDevice = hDevice;
    CreateCtx.NodeOrdinal = 0;
    CreateCtx.EngineAffinity = 1;
    Status = pfnD3DKMTCreateContext(&CreateCtx);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTCreateContext not implemented\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    /*
     * A graphics context needs a render node. On a display-only adapter
     * (e.g. the Microsoft Basic Display Adapter exposed by \\.\DISPLAY1, or a
     * QEMU display-only guest) there is no render node, so Win11 legitimately
     * refuses context creation -- skip the create-dependent checks rather than
     * fail. The negative-path checks below still run unconditionally.
     */
    if (!NT_SUCCESS(Status))
    {
        skip("CreateContext not supported on this device (0x%lx) - "
             "no render node (display-only adapter)\n", Status);
    }
    else
    {
        ok(CreateCtx.hContext != 0, "CreateContext returned zero handle\n");

        /* Destroy context */
        memset(&DestroyCtx, 0, sizeof(DestroyCtx));
        DestroyCtx.hContext = CreateCtx.hContext;
        Status = pfnD3DKMTDestroyContext(&DestroyCtx);
        ok(NT_SUCCESS(Status), "DestroyContext failed with 0x%lx\n", Status);

        /* Double destroy */
        Status = pfnD3DKMTDestroyContext(&DestroyCtx);
        ok(!NT_SUCCESS(Status),
           "Double DestroyContext should fail, got 0x%lx\n", Status);
    }

    /* Invalid device handle for context creation */
    memset(&CreateCtx, 0, sizeof(CreateCtx));
    CreateCtx.hDevice = 0xDEADBEEF;
    Status = pfnD3DKMTCreateContext(&CreateCtx);
    ok(!NT_SUCCESS(Status),
       "CreateContext with invalid device should fail, got 0x%lx\n", Status);

    /* Invalid context handle for destroy */
    memset(&DestroyCtx, 0, sizeof(DestroyCtx));
    DestroyCtx.hContext = 0xBAD0CAFE;
    Status = pfnD3DKMTDestroyContext(&DestroyCtx);
    ok(!NT_SUCCESS(Status),
       "DestroyContext with invalid handle should fail, got 0x%lx\n", Status);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(D3dkmtDevice)
{
    Test_CreateDestroyDevice();
    Test_CreateDeviceInvalidAdapter();
    Test_DestroyDeviceInvalidHandle();
    Test_MultipleDevices();
    Test_GetDeviceState();
    Test_CreateDestroyContext();
}
