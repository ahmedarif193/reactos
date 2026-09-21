/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntglobal.c
 * PURPOSE:     NT memory manager globals and supporting interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

MI_SYSTEM MiSystem;
MI_PROCESS_MANAGER MiProcessManager;
ULONG MmProductType;

#define MI_BOOT_ARENA_BYTES (1024 * 1024)

BOOLEAN MiPoolReady;

static UCHAR MiBootArena[MI_BOOT_ARENA_BYTES];
static volatile LONG MiBootArenaUsed;

PVOID
MiKmAllocate(
    _In_ SIZE_T Bytes)
{
    LONG Size = (LONG)ROUND_UP(Bytes, 16);
    LONG Offset;

    if (MiPoolReady)
        return ExAllocatePoolWithTag(NonPagedPool, Bytes, MI_CORE_TAG);

    Offset = InterlockedExchangeAdd(&MiBootArenaUsed, Size);
    if (Offset + Size > MI_BOOT_ARENA_BYTES)
        return NULL;

    return &MiBootArena[Offset];
}

VOID
MiKmFree(
    _In_ PVOID Block)
{
    if ((PUCHAR)Block >= MiBootArena && (PUCHAR)Block < MiBootArena + MI_BOOT_ARENA_BYTES)
        return;

    ExFreePoolWithTag(Block, MI_CORE_TAG);
}

static KEVENT MiMemoryAvailableEvent;
static BOOLEAN MiMemoryEventReady;


VOID
MiMemoryEventInitialize(VOID)
{
    KeInitializeEvent(&MiMemoryAvailableEvent, NotificationEvent, FALSE);
    MiMemoryEventReady = TRUE;
}

VOID
MiSignalMemoryAvailable(VOID)
{
    if (MiMemoryEventReady)
        KePulseEvent(&MiMemoryAvailableEvent, 0, FALSE);
}

NTSTATUS
MiWaitForMemory(
    _In_ NTSTATUS Status,
    _Inout_ PULONG Attempts)
{
    LARGE_INTEGER Timeout;

    if (Status != STATUS_NO_MEMORY)
        return STATUS_SUCCESS;

    if (++*Attempts > 64 || KeGetCurrentIrql() > APC_LEVEL)
        return STATUS_NO_MEMORY;

    if (MiBalanceMemory(&MiSystem, &MiProcessManager) != 0)
        return STATUS_SUCCESS;

    if (!MiMemoryEventReady)
        return STATUS_NO_MEMORY;

    Timeout.QuadPart = -10 * 1000 * 10;
    KeWaitForSingleObject(&MiMemoryAvailableEvent, WrFreePage, KernelMode, FALSE, &Timeout);
    return STATUS_SUCCESS;
}
