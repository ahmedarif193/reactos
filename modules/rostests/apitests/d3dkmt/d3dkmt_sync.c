/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for D3DKMT synchronization objects
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests: D3DKMTCreateSynchronizationObject,
 *        D3DKMTDestroySynchronizationObject,
 *        D3DKMTWaitForSynchronizationObject,
 *        D3DKMTSignalSynchronizationObject,
 *        D3DKMTWaitForIdle
 */

#include "precomp.h"

static void Test_CreateSyncObject_NullParam(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateSynchronizationObject);

    Status = pfnD3DKMTCreateSynchronizationObject(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTCreateSynchronizationObject(NULL) returned 0x%lx\n", Status);
}

static void Test_DestroySyncObject_NullParam(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTDestroySynchronizationObject);

    Status = pfnD3DKMTDestroySynchronizationObject(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTDestroySynchronizationObject(NULL) returned 0x%lx\n", Status);
}

static void Test_DestroySyncObject_InvalidHandle(void)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroyData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTDestroySynchronizationObject);

    memset(&DestroyData, 0, sizeof(DestroyData));
    DestroyData.hSyncObject = 0xBAD0CAFE;
    Status = pfnD3DKMTDestroySynchronizationObject(&DestroyData);
    ok(!NT_SUCCESS(Status),
       "DestroySyncObject with invalid handle should fail, got 0x%lx\n", Status);
}

static void Test_CreateDestroySyncObject_Mutex(void)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT CreateData;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroyData;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateSynchronizationObject);
    LOAD_D3DKMT(D3DKMTDestroySynchronizationObject);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for sync object tests\n");
        return;
    }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("Cannot create device for sync object tests\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* Create a synchronization mutex */
    memset(&CreateData, 0, sizeof(CreateData));
    CreateData.hDevice = hDevice;
    CreateData.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
    CreateData.Info.SynchronizationMutex.InitialState = TRUE;

    Status = pfnD3DKMTCreateSynchronizationObject(&CreateData);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTCreateSynchronizationObject not implemented\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    ok(NT_SUCCESS(Status),
       "CreateSyncObject(Mutex) failed with 0x%lx\n", Status);

    if (!NT_SUCCESS(Status))
    {
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    ok(CreateData.hSyncObject != 0,
       "CreateSyncObject returned zero handle\n");

    /* Destroy */
    memset(&DestroyData, 0, sizeof(DestroyData));
    DestroyData.hSyncObject = CreateData.hSyncObject;
    Status = pfnD3DKMTDestroySynchronizationObject(&DestroyData);
    ok(NT_SUCCESS(Status), "DestroySyncObject failed with 0x%lx\n", Status);

    /* Double destroy */
    Status = pfnD3DKMTDestroySynchronizationObject(&DestroyData);
    ok(!NT_SUCCESS(Status),
       "Double DestroySyncObject should fail, got 0x%lx\n", Status);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void Test_CreateSyncObject_Semaphore(void)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT CreateData;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroyData;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateSynchronizationObject);
    LOAD_D3DKMT(D3DKMTDestroySynchronizationObject);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for semaphore test\n");
        return;
    }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("Cannot create device for semaphore test\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* Create a semaphore sync object */
    memset(&CreateData, 0, sizeof(CreateData));
    CreateData.hDevice = hDevice;
    CreateData.Info.Type = D3DDDI_SEMAPHORE;
    CreateData.Info.Semaphore.MaxCount = 4;
    CreateData.Info.Semaphore.InitialCount = 2;

    Status = pfnD3DKMTCreateSynchronizationObject(&CreateData);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTCreateSynchronizationObject not implemented\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    if (NT_SUCCESS(Status))
    {
        ok(CreateData.hSyncObject != 0,
           "CreateSyncObject(Semaphore) returned zero handle\n");

        memset(&DestroyData, 0, sizeof(DestroyData));
        DestroyData.hSyncObject = CreateData.hSyncObject;
        pfnD3DKMTDestroySynchronizationObject(&DestroyData);
    }

    /* Invalid device for creation */
    memset(&CreateData, 0, sizeof(CreateData));
    CreateData.hDevice = 0xDEADBEEF;
    CreateData.Info.Type = D3DDDI_SEMAPHORE;
    CreateData.Info.Semaphore.MaxCount = 1;
    CreateData.Info.Semaphore.InitialCount = 0;
    Status = pfnD3DKMTCreateSynchronizationObject(&CreateData);
    ok(!NT_SUCCESS(Status),
       "CreateSyncObject with invalid device should fail, got 0x%lx\n", Status);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void Test_WaitForSyncObject_NullParam(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTWaitForSynchronizationObject);

    Status = pfnD3DKMTWaitForSynchronizationObject(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTWaitForSynchronizationObject(NULL) returned 0x%lx\n", Status);
}

static void Test_SignalSyncObject_NullParam(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTSignalSynchronizationObject);

    Status = pfnD3DKMTSignalSynchronizationObject(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTSignalSynchronizationObject(NULL) returned 0x%lx\n", Status);
}

static void Test_WaitForIdle_NullParam(void)
{
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTWaitForIdle);

    Status = pfnD3DKMTWaitForIdle(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTWaitForIdle(NULL) returned 0x%lx\n", Status);
}

static void Test_WaitForIdle_InvalidHandle(void)
{
    D3DKMT_WAITFORIDLE WaitData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTWaitForIdle);

    memset(&WaitData, 0, sizeof(WaitData));
    WaitData.hDevice = 0xBAD0CAFE;
    Status = pfnD3DKMTWaitForIdle(&WaitData);
    ok(!NT_SUCCESS(Status),
       "WaitForIdle with invalid device should fail, got 0x%lx\n", Status);
}

START_TEST(D3dkmtSync)
{
    Test_CreateSyncObject_NullParam();
    Test_DestroySyncObject_NullParam();
    Test_DestroySyncObject_InvalidHandle();
    Test_CreateDestroySyncObject_Mutex();
    Test_CreateSyncObject_Semaphore();
    Test_WaitForSyncObject_NullParam();
    Test_SignalSyncObject_NullParam();
    Test_WaitForIdle_NullParam();
    Test_WaitForIdle_InvalidHandle();
}
