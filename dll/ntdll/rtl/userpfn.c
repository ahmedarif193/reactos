/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * PURPOSE:     USER client procedure table registration
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

static ULONG64 RtlpUserPfnClientA[32];
static ULONG64 RtlpUserPfnClientW[32];
static ULONG64 RtlpUserPfnClientWorker[16];
static LONG RtlpUserPfnInitialized;

NTSTATUS
NTAPI
RtlInitializeNtUserPfn(
    _In_reads_bytes_(ClientASize) const VOID *ClientA,
    _In_ ULONG ClientASize,
    _In_reads_bytes_(ClientWSize) const VOID *ClientW,
    _In_ ULONG ClientWSize,
    _In_reads_bytes_(ClientWorkerSize) const VOID *ClientWorker,
    _In_ ULONG ClientWorkerSize)
{
    if (ClientASize > sizeof(RtlpUserPfnClientA) ||
        ClientWSize > sizeof(RtlpUserPfnClientW) ||
        ClientWorkerSize > sizeof(RtlpUserPfnClientWorker))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (RtlpUserPfnInitialized)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(RtlpUserPfnClientA, ClientA, ClientASize);
    RtlCopyMemory(RtlpUserPfnClientW, ClientW, ClientWSize);
    RtlCopyMemory(RtlpUserPfnClientWorker, ClientWorker, ClientWorkerSize);

    if (InterlockedCompareExchange(&RtlpUserPfnInitialized, TRUE, FALSE))
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlRetrieveNtUserPfn(
    _Out_ const VOID **ClientA,
    _Out_ const VOID **ClientW,
    _Out_ const VOID **ClientWorker)
{
    if (!RtlpUserPfnInitialized)
        return STATUS_INVALID_PARAMETER;

    *ClientA = RtlpUserPfnClientA;
    *ClientW = RtlpUserPfnClientW;
    *ClientWorker = RtlpUserPfnClientWorker;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlResetNtUserPfn(VOID)
{
    if (!InterlockedExchange(&RtlpUserPfnInitialized, FALSE))
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(RtlpUserPfnClientA, sizeof(RtlpUserPfnClientA));
    RtlZeroMemory(RtlpUserPfnClientW, sizeof(RtlpUserPfnClientW));
    RtlZeroMemory(RtlpUserPfnClientWorker, sizeof(RtlpUserPfnClientWorker));
    return STATUS_SUCCESS;
}
