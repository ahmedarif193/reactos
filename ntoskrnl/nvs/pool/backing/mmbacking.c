/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/backing/mmbacking.c
 * PURPOSE:     Memory manager backing for pool allocations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#if defined(POOL_HOST_TEST)
#include <mmcc/pool/host/ntpoolshim.h>
#else
#include <nvs/nt/mint.h>
#endif
#include "../include/poolenv.h"
#include "../include/poolva.h"
#include "../include/poolback.h"

POOL_KM_LAYOUT MiPoolLayout;

static
NTSTATUS
PoolBackCommit(
    _In_opt_ PVOID Context,
    _In_ PVOID Base,
    _In_ SIZE_T Bytes)
{
    NTSTATUS Status;

    Status = MiSystemCommitPinned(&MiSystem, (ULONG64)(ULONG_PTR)Base, (ULONG)(Bytes >> PAGE_SHIFT),
                                  (ULONG)(ULONG_PTR)Context);

    if (Status == STATUS_NO_MEMORY || Status == STATUS_COMMITMENT_LIMIT)
        MiWakeBalanceSetManager();

    return Status;
}

static
VOID
PoolBackDecommit(
    _In_opt_ PVOID Context,
    _In_ PVOID Base,
    _In_ SIZE_T Bytes)
{
    UNREFERENCED_PARAMETER(Context);
    MiSystemDecommitPinned(&MiSystem, (ULONG64)(ULONG_PTR)Base, (ULONG)(Bytes >> PAGE_SHIFT));
}

static
NTSTATUS
PoolBackResidentCommit(
    _In_opt_ PVOID Context,
    _In_ PVOID Base,
    _In_ SIZE_T Bytes)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Base);
    UNREFERENCED_PARAMETER(Bytes);
    return STATUS_SUCCESS;
}

static
VOID
PoolBackResidentDecommit(
    _In_opt_ PVOID Context,
    _In_ PVOID Base,
    _In_ SIZE_T Bytes)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Base);
    UNREFERENCED_PARAMETER(Bytes);
}

VOID
PoolBackDemandBacking(
    _Out_ PPOOL_BACKING Backing,
    _In_ BOOLEAN Executable)
{
    Backing->Commit = PoolBackCommit;
    Backing->Decommit = PoolBackDecommit;
    Backing->Context = (PVOID)(ULONG_PTR)(Executable ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE);
}

VOID
PoolBackResidentBacking(
    _Out_ PPOOL_BACKING Backing)
{
    Backing->Commit = PoolBackResidentCommit;
    Backing->Decommit = PoolBackResidentDecommit;
    Backing->Context = NULL;
}

VOID
PoolBackQueryLayout(
    _Out_ PPOOL_KM_LAYOUT Layout)
{
    *Layout = MiPoolLayout;
}
