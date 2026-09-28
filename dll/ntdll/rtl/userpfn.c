/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * PURPOSE:     USER client procedure table registration
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

static const VOID *RtlpUserPfnClientA;
static const VOID *RtlpUserPfnClientW;
static const VOID *RtlpUserPfnClientWorker;
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
    if (!ClientA || !ClientW || !ClientWorker ||
        !ClientASize || !ClientWSize || !ClientWorkerSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (InterlockedCompareExchange(&RtlpUserPfnInitialized, -1, FALSE) != FALSE)
        return STATUS_INVALID_PARAMETER;

    RtlpUserPfnClientA = ClientA;
    RtlpUserPfnClientW = ClientW;
    RtlpUserPfnClientWorker = ClientWorker;
    InterlockedExchange(&RtlpUserPfnInitialized, TRUE);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlRetrieveNtUserPfn(
    _Out_ const VOID **ClientA,
    _Out_ const VOID **ClientW,
    _Out_ const VOID **ClientWorker)
{
    if (ReadAcquire(&RtlpUserPfnInitialized) != TRUE)
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
    if (InterlockedCompareExchange(&RtlpUserPfnInitialized, FALSE, TRUE) != TRUE)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}
