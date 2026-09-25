/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-Mode Test Suite NDIS RW lock test
 */

#include <kmt_test.h>

/* Verify the published ABI independently of NDIS's private definitions. */
typedef struct { KIRQL OldIrql; UCHAR LockState; UCHAR Flags; } TEST_LOCK_STATE;
__declspec(dllimport) PVOID NTAPI NdisAllocateRWLock(PVOID);
__declspec(dllimport) VOID NTAPI NdisFreeRWLock(PVOID);
__declspec(dllimport) VOID NTAPI NdisAcquireRWLockRead(PVOID, TEST_LOCK_STATE *, UCHAR);
__declspec(dllimport) VOID NTAPI NdisAcquireRWLockWrite(PVOID, TEST_LOCK_STATE *, UCHAR);
__declspec(dllimport) VOID NTAPI NdisReleaseRWLock(PVOID, TEST_LOCK_STATE *);

typedef struct { UCHAR Before; TEST_LOCK_STATE State; UCHAR After[16]; } GUARDED_STATE;
typedef struct { PVOID Lock; KEVENT Done; } DPC_TEST;
C_ASSERT(sizeof(TEST_LOCK_STATE) == 3);

static VOID CheckGuards(GUARDED_STATE *State)
{
    ULONG i;
    ok_eq_uint(State->Before, 0xa5);
    for (i = 0; i < sizeof(State->After); ++i)
        ok_eq_uint(State->After[i], 0xa5);
}

static VOID CheckLock(PVOID Lock, UCHAR Flags)
{
    GUARDED_STATE Outer, Inner;
    KIRQL Initial = KeGetCurrentIrql();
    RtlFillMemory(&Outer, sizeof(Outer), 0xa5);
    RtlFillMemory(&Inner, sizeof(Inner), 0xa5);
    NdisAcquireRWLockRead(Lock, &Outer.State, Flags);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    NdisAcquireRWLockRead(Lock, &Inner.State, 1);
    NdisReleaseRWLock(Lock, &Inner.State);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    NdisReleaseRWLock(Lock, &Outer.State);
    ok_eq_uint(KeGetCurrentIrql(), Initial);
    CheckGuards(&Outer); CheckGuards(&Inner);

    NdisAcquireRWLockWrite(Lock, &Outer.State, Flags);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    NdisAcquireRWLockWrite(Lock, &Inner.State, 1);
    NdisReleaseRWLock(Lock, &Inner.State);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    NdisAcquireRWLockRead(Lock, &Inner.State, 1);
    NdisReleaseRWLock(Lock, &Inner.State);
    NdisReleaseRWLock(Lock, &Outer.State);
    ok_eq_uint(KeGetCurrentIrql(), Initial);
    CheckGuards(&Outer); CheckGuards(&Inner);
}

static VOID NTAPI TestDpc(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2)
{
    DPC_TEST *Test = Context;
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    CheckLock(Test->Lock, 0);
    CheckLock(Test->Lock, 1);
    KeSetEvent(&Test->Done, IO_NO_INCREMENT, FALSE);
}

START_TEST(NdisRwLock)
{
    DPC_TEST Test;
    KDPC Dpc;
    Test.Lock = NdisAllocateRWLock(NULL);
    if (skip(Test.Lock != NULL, "Could not allocate NDIS RW lock\n")) return;
    CheckLock(Test.Lock, 0);
    KeInitializeEvent(&Test.Done, NotificationEvent, FALSE);
    KeInitializeDpc(&Dpc, TestDpc, &Test);
    ok(KeInsertQueueDpc(&Dpc, NULL, NULL), "Could not queue test DPC\n");
    KeWaitForSingleObject(&Test.Done, Executive, KernelMode, FALSE, NULL);
    /* The event signals before the DPC has returned: keep its stack data live. */
    KeFlushQueuedDpcs();
    NdisFreeRWLock(Test.Lock);
}
