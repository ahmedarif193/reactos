/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT synchronization object tests
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

static void Test_SyncObjectStructLayout(void)
{
    /* D3DKMT_CREATESYNCHRONIZATIONOBJECT must have hDevice at offset 0 */
    ok_eq_uint(offsetof(D3DKMT_CREATESYNCHRONIZATIONOBJECT, hDevice), 0);

    /* Must have Info and hSyncObject fields */
    ok(sizeof(D3DKMT_CREATESYNCHRONIZATIONOBJECT) >
       sizeof(D3DKMT_HANDLE),
       "CREATESYNCHRONIZATIONOBJECT must be larger than a single handle\n");

    /* D3DKMT_DESTROYSYNCHRONIZATIONOBJECT */
    ok_eq_uint(offsetof(D3DKMT_DESTROYSYNCHRONIZATIONOBJECT, hSyncObject), 0);
    ok_eq_uint(sizeof(D3DKMT_DESTROYSYNCHRONIZATIONOBJECT),
               sizeof(D3DKMT_HANDLE));

    /* D3DKMT_SIGNALSYNCHRONIZATIONOBJECT must have hContext */
    ok_eq_uint(offsetof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECT, hContext), 0);

    /* D3DKMT_WAITFORSYNCHRONIZATIONOBJECT */
    ok_eq_uint(offsetof(D3DKMT_WAITFORSYNCHRONIZATIONOBJECT, hContext), 0);

    /* D3DDDI_MAX_OBJECT_SIGNALED must be a reasonable limit */
    ok(D3DDDI_MAX_OBJECT_SIGNALED >= 1 && D3DDDI_MAX_OBJECT_SIGNALED <= 256,
       "D3DDDI_MAX_OBJECT_SIGNALED out of expected range: %u\n",
       D3DDDI_MAX_OBJECT_SIGNALED);

    /* D3DDDI_MAX_OBJECT_WAITED_ON must be a reasonable limit */
    ok(D3DDDI_MAX_OBJECT_WAITED_ON >= 1 && D3DDDI_MAX_OBJECT_WAITED_ON <= 256,
       "D3DDDI_MAX_OBJECT_WAITED_ON out of expected range: %u\n",
       D3DDDI_MAX_OBJECT_WAITED_ON);
}

static void Test_SyncObjectConstants(void)
{
    /* Sync object IOCTL codes must use function 0x150-0x151 */
    ok_eq_uint((IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT >> 2) & 0xFFF, 0x150);
    ok_eq_uint((IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT >> 2) & 0xFFF, 0x151);

    /* Both must be distinct */
    ok(IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT !=
       IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT,
       "Wait and Signal IOCTLs must differ\n");
}

static void Test_SignalInvalidSync(void)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT sig;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    /* Signal with invalid context handle */
    memset(&sig, 0, sizeof(sig));
    sig.hContext = 0xBAD0CAFE;
    sig.ObjectCount = 1;
    sig.ObjectHandleArray[0] = 0x12345678;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT,
                     &sig, sizeof(sig), NULL, 0, &br);
    ok(!r, "Signal should fail with invalid context\n");

    CloseHandle(hDxgKrnl);
}

static void Test_WaitInvalidSync(void)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT wait;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&wait, 0, sizeof(wait));
    wait.hContext = 0xBAD0CAFE;
    wait.ObjectCount = 1;
    wait.ObjectHandleArray[0] = 0x12345678;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT,
                     &wait, sizeof(wait), NULL, 0, &br);
    ok(!r, "Wait should fail with invalid context\n");

    CloseHandle(hDxgKrnl);
}

static void Test_SignalZeroObjects(void)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT sig;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&sig, 0, sizeof(sig));
    sig.ObjectCount = 0; /* Invalid — must be >= 1 */

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT,
                     &sig, sizeof(sig), NULL, 0, &br);
    ok(!r, "Signal should fail with ObjectCount=0\n");

    CloseHandle(hDxgKrnl);
}

static void Test_WaitZeroObjects(void)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT wait;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&wait, 0, sizeof(wait));
    wait.ObjectCount = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT,
                     &wait, sizeof(wait), NULL, 0, &br);
    ok(!r, "Wait should fail with ObjectCount=0\n");

    CloseHandle(hDxgKrnl);
}

static void Test_SignalTooManyObjects(void)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT sig;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&sig, 0, sizeof(sig));
    sig.ObjectCount = D3DDDI_MAX_OBJECT_SIGNALED + 1;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT,
                     &sig, sizeof(sig), NULL, 0, &br);
    ok(!r, "Signal should fail when ObjectCount exceeds max\n");

    CloseHandle(hDxgKrnl);
}

START_TEST(D3dkmtSync)
{
    Test_SyncObjectStructLayout();
    Test_SyncObjectConstants();
    Test_SignalInvalidSync();
    Test_WaitInvalidSync();
    Test_SignalZeroObjects();
    Test_WaitZeroObjects();
    Test_SignalTooManyObjects();
}
