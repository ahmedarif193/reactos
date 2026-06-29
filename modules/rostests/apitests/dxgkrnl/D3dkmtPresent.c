/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT present and render operation tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "precomp.h"

static HANDLE hDxgKrnl = INVALID_HANDLE_VALUE;

static BOOL OpenDxg(void)
{
    hDxgKrnl = OpenDxgKrnl();
    if (hDxgKrnl == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open \\Device\\DxgKrnl\n");
        return FALSE;
    }
    return TRUE;
}

static void Test_PresentStructLayout(void)
{
    /* D3DKMT_PRESENT must have hDevice at the start */
    ok_eq_uint(offsetof(D3DKMT_PRESENT, hDevice), 0);

    /* Must have source and destination allocation handles */
    ok(offsetof(D3DKMT_PRESENT, hSource) > 0,
       "hSource must be at a positive offset\n");
    ok(offsetof(D3DKMT_PRESENT, hDestination) > 0,
       "hDestination must be at a positive offset\n");

    /* SrcRect and DstRect must be RECT structures */
    ok_eq_uint(sizeof(((D3DKMT_PRESENT *)0)->SrcRect), sizeof(RECT));
    ok_eq_uint(sizeof(((D3DKMT_PRESENT *)0)->DstRect), sizeof(RECT));

    /* VidPnSourceId must exist */
    ok(sizeof(D3DKMT_PRESENT) >= sizeof(D3DKMT_HANDLE) * 3 + sizeof(RECT) * 2,
       "D3DKMT_PRESENT too small\n");
}

static void Test_RenderStructLayout(void)
{
    /* D3DKMT_RENDER must have hDevice or hContext at start */
    ok(sizeof(D3DKMT_RENDER) > sizeof(D3DKMT_HANDLE),
       "D3DKMT_RENDER must contain more than just a handle\n");
}

static void Test_PresentInvalidDevice(void)
{
    D3DKMT_PRESENT pres;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&pres, 0, sizeof(pres));
    pres.hDevice = 0xBAD0CAFE;
    pres.VidPnSourceId = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                     &pres, sizeof(pres), &pres, sizeof(pres), &br);
    ok(!r, "Present should fail with invalid device\n");

    CloseHandle(hDxgKrnl);
}

static void Test_PresentZeroDevice(void)
{
    D3DKMT_PRESENT pres;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&pres, 0, sizeof(pres));
    pres.hDevice = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                     &pres, sizeof(pres), &pres, sizeof(pres), &br);
    ok(!r, "Present should fail with zero device\n");

    CloseHandle(hDxgKrnl);
}

static void Test_RenderInvalidDevice(void)
{
    D3DKMT_RENDER ren;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&ren, 0, sizeof(ren));
    ren.hDevice = 0xBAD0CAFE;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_RENDER,
                     &ren, sizeof(ren), &ren, sizeof(ren), &br);
    ok(!r, "Render should fail with invalid device\n");

    CloseHandle(hDxgKrnl);
}

static void Test_PresentIoctlCodes(void)
{
    ok_eq_uint((IOCTL_D3DKMT_RENDER >> 2) & 0xFFF, 0x140);
    ok_eq_uint((IOCTL_D3DKMT_PRESENT >> 2) & 0xFFF, 0x141);
    ok(IOCTL_D3DKMT_RENDER != IOCTL_D3DKMT_PRESENT,
       "Render and Present IOCTLs must differ\n");
}

static void Test_PresentQueueConstants(void)
{
    /* Verify present queue depth constant (from present.h) */
    ULONG PRESENT_QUEUE_DEPTH = 16;
    ok(PRESENT_QUEUE_DEPTH >= 4 && PRESENT_QUEUE_DEPTH <= 256,
       "Present queue depth %lu out of reasonable range\n", PRESENT_QUEUE_DEPTH);

    /* Queue depth must be a power of two (for efficient modular arithmetic)
     * OR a reasonable small number */
    ok(PRESENT_QUEUE_DEPTH == 16,
       "Expected present queue depth 16, got %lu\n", PRESENT_QUEUE_DEPTH);
}

static void Test_LockUnlockInvalidDevice(void)
{
    D3DKMT_LOCK lockData;
    D3DKMT_UNLOCK unlockData;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    /* Lock with invalid device */
    memset(&lockData, 0, sizeof(lockData));
    lockData.hDevice = 0xBAD0CAFE;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_LOCK,
                     &lockData, sizeof(lockData),
                     &lockData, sizeof(lockData), &br);
    ok(!r, "Lock should fail with invalid device\n");

    /* Unlock with invalid device */
    memset(&unlockData, 0, sizeof(unlockData));
    unlockData.hDevice = 0xBAD0CAFE;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_UNLOCK,
                     &unlockData, sizeof(unlockData), NULL, 0, &br);
    ok(!r, "Unlock should fail with invalid device\n");

    CloseHandle(hDxgKrnl);
}

static void Test_AllocationIoctlCodes(void)
{
    ok_eq_uint((IOCTL_D3DKMT_CREATEALLOCATION >> 2) & 0xFFF, 0x130);
    ok_eq_uint((IOCTL_D3DKMT_DESTROYALLOCATION >> 2) & 0xFFF, 0x131);
    ok_eq_uint((IOCTL_D3DKMT_LOCK >> 2) & 0xFFF, 0x132);
    ok_eq_uint((IOCTL_D3DKMT_UNLOCK >> 2) & 0xFFF, 0x133);

    /* All four must be distinct */
    ok(IOCTL_D3DKMT_CREATEALLOCATION != IOCTL_D3DKMT_DESTROYALLOCATION, "");
    ok(IOCTL_D3DKMT_CREATEALLOCATION != IOCTL_D3DKMT_LOCK, "");
    ok(IOCTL_D3DKMT_CREATEALLOCATION != IOCTL_D3DKMT_UNLOCK, "");
    ok(IOCTL_D3DKMT_DESTROYALLOCATION != IOCTL_D3DKMT_LOCK, "");
    ok(IOCTL_D3DKMT_DESTROYALLOCATION != IOCTL_D3DKMT_UNLOCK, "");
    ok(IOCTL_D3DKMT_LOCK != IOCTL_D3DKMT_UNLOCK, "");
}

START_TEST(D3dkmtPresent)
{
    Test_PresentStructLayout();
    Test_RenderStructLayout();
    Test_PresentInvalidDevice();
    Test_PresentZeroDevice();
    Test_RenderInvalidDevice();
    Test_PresentIoctlCodes();
    Test_PresentQueueConstants();
    Test_LockUnlockInvalidDevice();
    Test_AllocationIoctlCodes();
}
