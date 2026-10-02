/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntworker.c
 * PURPOSE:     Memory manager background worker integration
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

#define MI_ZERO_PAGE_BATCH 64

static KEVENT MiBalanceEvent;
static KEVENT MiModifiedWriterEvent;
static KEVENT MiZeroPageEvent;
static volatile LONG MiZeroPageWaiting;

static
VOID
NTAPI
MiBalanceSetManager(
    _In_ PVOID Context)
{
    LARGE_INTEGER Period;

    UNREFERENCED_PARAMETER(Context);

    Period.QuadPart = -10 * 1000 * 1000;
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY + 1);

    for (;;)
    {
        KeWaitForSingleObject(&MiBalanceEvent, WrFreePage, KernelMode, FALSE, &Period);

        if (MiBalanceMemory(&MiSystem, &MiProcessManager) != 0)
        {
            KeSetEvent(&MiModifiedWriterEvent, 0, FALSE);
            MiSignalMemoryAvailable();
        }

        MmAvailablePages = (PFN_COUNT)MiPfnAvailablePages(&MiSystem.Pfn);
        MmQuerySystemCommitCharge(NULL);
    }
}

static
VOID
NTAPI
MiModifiedPageWriter(
    _In_ PVOID Context)
{
    LARGE_INTEGER Period;

    UNREFERENCED_PARAMETER(Context);

    Period.QuadPart = -3 * 10 * 1000 * 1000;
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY + 1);

    for (;;)
    {
        KeWaitForSingleObject(&MiModifiedWriterEvent, WrPageOut, KernelMode, FALSE, &Period);

        while (MiWriteModifiedPages(&MiSystem, 256) != 0)
            MiSignalMemoryAvailable();
    }
}

VOID
MiWakeBalanceSetManager(VOID)
{
    /* Pool commits can fail before phase 1 creates the balance set manager
     * and initializes its event; there is nobody to wake yet. */
    if (MiBalanceEvent.Header.Type != SynchronizationEvent)
        return;
    KeSetEvent(&MiBalanceEvent, 0, FALSE);
}

NTSTATUS
MiWorkerThreadsInitialize(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    HANDLE Handle;

    KeInitializeEvent(&MiBalanceEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&MiModifiedWriterEvent, SynchronizationEvent, FALSE);
    MiMemoryEventInitialize();

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, MiBalanceSetManager,
                                  NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    ZwClose(Handle);

    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, MiModifiedPageWriter,
                                  NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    ZwClose(Handle);

    /* NVS's memory balancer does not scan scheduler ready queues. Start
     * the shared scheduler balance-set manager too, so threads readied at
     * their base priority cannot starve behind continuously boosted peers. */
    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL,
                                  KeBalanceSetManager, NULL);
    if (NT_SUCCESS(Status)) ZwClose(Handle);
    return Status;
}

static
VOID
MiZeroPageNotify(
    _Inout_ PMI_PFN_DATABASE Db)
{
    UNREFERENCED_PARAMETER(Db);

    if (InterlockedCompareExchange(&MiZeroPageWaiting, 0, 1) == 1)
        KeSetEvent(&MiZeroPageEvent, 0, FALSE);
}

VOID
NTAPI
MmZeroPageThread(VOID)
{
    LARGE_INTEGER Period;

    Period.QuadPart = -10 * 1000 * 1000;
    KeSetPriorityThread(KeGetCurrentThread(), 0);
    KeInitializeEvent(&MiZeroPageEvent, SynchronizationEvent, FALSE);
    MiSystem.Pfn.FreeNotify = MiZeroPageNotify;

    for (;;)
    {
        InterlockedExchange(&MiZeroPageWaiting, 1);
        if (MiPfnZeroFreePages(&MiSystem.Pfn, MI_ZERO_PAGE_BATCH) != 0)
            continue;

        KeWaitForSingleObject(&MiZeroPageEvent, WrFreePage, KernelMode, FALSE, &Period);
    }
}
