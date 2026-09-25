/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite MmProbeAndLockPages at DISPATCH_LEVEL test
 */
#include <kmt_test.h>

#define MDL_DISPATCH_TAG 'dDmK'
#define MDL_DISPATCH_PAGES 16

typedef struct
{
    PMDL Mdl;
    KEVENT Started;
    volatile LONG Stop;
    volatile LONG Rounds;
    NTSTATUS Status;
} MDL_DISPATCH_WORK;

static VOID NTAPI
MdlPassiveWorker(PVOID Parameter)
{
    MDL_DISPATCH_WORK *Work = Parameter;

    if (KeQueryActiveProcessors() & 2)
        KeSetSystemAffinityThread(2);
    while (!InterlockedCompareExchange(&Work->Stop, 0, 0))
    {
        _SEH2_TRY
        {
            MmProbeAndLockPages(Work->Mdl, KernelMode, IoReadAccess);
            MmUnlockPages(Work->Mdl);
            if (InterlockedIncrement(&Work->Rounds) == 1)
                KeSetEvent(&Work->Started, IO_NO_INCREMENT, FALSE);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Work->Status = _SEH2_GetExceptionCode();
            KeSetEvent(&Work->Started, IO_NO_INCREMENT, FALSE);
            break;
        }
        _SEH2_END;
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(MmMdlDispatch)
{
    MDL_DISPATCH_WORK Work = {0};
    PVOID Allocation;
    PUCHAR Base;
    PMDL Mdl;
    HANDLE Thread = NULL;
    PFN_NUMBER Expected[MDL_DISPATCH_PAGES + 1];
    ULONG Round, i, Pages;
    NTSTATUS Status;
    KIRQL OldIrql, AfterProbe, AfterUnlock;
    CSHORT LockedFlags, UnlockedFlags;
    BOOLEAN PfnsMatch;
    PEPROCESS Process;

    Allocation = ExAllocatePoolWithTag(NonPagedPool,
        (MDL_DISPATCH_PAGES + 2) * PAGE_SIZE, MDL_DISPATCH_TAG);
    if (!ok(Allocation != NULL, "Nonpaged allocation failed\n"))
        return;
    Base = (PUCHAR)PAGE_ALIGN((PUCHAR)Allocation + PAGE_SIZE);
    RtlFillMemory(Base, MDL_DISPATCH_PAGES * PAGE_SIZE, 0x5a);
    /* Deliberately unaligned within the first page. */
    Mdl = IoAllocateMdl(Base + 0xb36, (MDL_DISPATCH_PAGES - 1) * PAGE_SIZE,
                        FALSE, FALSE, NULL);
    Work.Mdl = IoAllocateMdl(Base, MDL_DISPATCH_PAGES * PAGE_SIZE, FALSE, FALSE, NULL);
    if (!ok(Mdl != NULL && Work.Mdl != NULL, "MDL allocation failed\n"))
        goto Cleanup;
    Pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), Mdl->ByteCount);
    for (i = 0; i < Pages; i++)
        Expected[i] = (PFN_NUMBER)(MmGetPhysicalAddress(Base + i * PAGE_SIZE).QuadPart >> PAGE_SHIFT);

    KeInitializeEvent(&Work.Started, NotificationEvent, FALSE);
    Status = PsCreateSystemThread(&Thread, SYNCHRONIZE, NULL, NULL, NULL,
                                   MdlPassiveWorker, &Work);
    if (!ok_eq_hex(Status, STATUS_SUCCESS))
        goto Cleanup;
    KeWaitForSingleObject(&Work.Started, Executive, KernelMode, FALSE, NULL);
    /* Concurrent PASSIVE probes contend with the old address-space mutex
     * path. Only nonpageable pool memory is supplied at DISPATCH_LEVEL. */
    for (Round = 0; Round < 2048; Round++)
    {
        LOCK_OPERATION Operation = (LOCK_OPERATION)(Round % 3);
        Status = STATUS_SUCCESS;
        LockedFlags = UnlockedFlags = 0;
        PfnsMatch = FALSE;
        Process = (PEPROCESS)(ULONG_PTR)-1;
        AfterProbe = AfterUnlock = PASSIVE_LEVEL;
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        _SEH2_TRY
        {
            MmProbeAndLockPages(Mdl, KernelMode, Operation);
            AfterProbe = KeGetCurrentIrql();
            LockedFlags = Mdl->MdlFlags;
            Process = Mdl->Process;
            PfnsMatch = TRUE;
            for (i = 0; i < Pages; i++)
                if (MmGetMdlPfnArray(Mdl)[i] != Expected[i])
                    PfnsMatch = FALSE;
            MmUnlockPages(Mdl);
            AfterUnlock = KeGetCurrentIrql();
            UnlockedFlags = Mdl->MdlFlags;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        KeLowerIrql(OldIrql);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            break;
        ok_eq_uint(AfterProbe, DISPATCH_LEVEL);
        ok_eq_uint(AfterUnlock, DISPATCH_LEVEL);
        ok((LockedFlags & MDL_PAGES_LOCKED) != 0, "Round %lu: missing locked flag\n", Round);
        ok((UnlockedFlags & MDL_PAGES_LOCKED) == 0, "Round %lu: still locked\n", Round);
        ok(((LockedFlags & MDL_WRITE_OPERATION) != 0) == (Operation != IoReadAccess),
           "Round %lu: incorrect write flag\n", Round);
        ok(Process == NULL, "Round %lu: kernel MDL has a process\n", Round);
        ok(PfnsMatch, "Round %lu: physical pages changed\n", Round);
        ok_eq_uint(Mdl->ByteOffset, 0xb36);
    }
    InterlockedExchange(&Work.Stop, 1);
    ZwWaitForSingleObject(Thread, FALSE, NULL);
    ZwClose(Thread);
    ok_eq_hex(Work.Status, STATUS_SUCCESS);
    ok(Work.Rounds > 0, "Concurrent probe worker did no work\n");
    for (i = 0; i < MDL_DISPATCH_PAGES * PAGE_SIZE; i++)
        if (Base[i] != 0x5a)
            break;
    ok_eq_uint(i, MDL_DISPATCH_PAGES * PAGE_SIZE);
Cleanup:
    if (Mdl) IoFreeMdl(Mdl);
    if (Work.Mdl) IoFreeMdl(Work.Mdl);
    ExFreePoolWithTag(Allocation, MDL_DISPATCH_TAG);
}
