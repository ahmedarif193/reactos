/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntworker.c
 * PURPOSE:     Memory manager background worker integration
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

static KEVENT MiBalanceEvent;
static KEVENT MiModifiedWriterEvent;

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
        MmTotalCommittedPages = (SIZE_T)MI_ATOMIC_READ64(&MiSystem.CommittedPages);

        if (MmTotalCommittedPages > MmPeakCommitment)
            MmPeakCommitment = MmTotalCommittedPages;
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
    return STATUS_SUCCESS;
}

VOID
NTAPI
MmZeroPageThread(VOID)
{
    LARGE_INTEGER Period;

    Period.QuadPart = -10 * 1000 * 1000;
    KeSetPriorityThread(KeGetCurrentThread(), 0);

    for (;;)
        KeDelayExecutionThread(KernelMode, FALSE, &Period);
}
