/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntpool.c
 * PURPOSE:     NT pool allocator integration
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

PVOID MmNonPagedPoolStart;
PVOID MmNonPagedPoolExpansionStart;
PVOID MmNonPagedPoolEnd;
PVOID MmPagedPoolStart;
PVOID MmPagedPoolEnd;
SIZE_T MmSizeOfNonPagedPoolInBytes;
SIZE_T MmMaximumNonPagedPoolInBytes;
SIZE_T MmSizeOfPagedPoolInBytes;
SIZE_T MmPagedPoolCommit;
PFN_NUMBER MmAllocatedNonPagedPool;
ULONG MmConsumedPoolPercentage;
ULONG MmProtectFreedNonPagedPool;
ULONG MmSpecialPoolTag;
MM_PAGED_POOL_INFO MmPagedPoolInfo;
BOOLEAN ExpArm64PoolBootstrapMode;

BOOLEAN
NTAPI
MmUseSpecialPool(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag)
{
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(Tag);
    return FALSE;
}

BOOLEAN
NTAPI
MmIsSpecialPoolAddress(
    _In_ PVOID P)
{
    UNREFERENCED_PARAMETER(P);
    return FALSE;
}

BOOLEAN
NTAPI
MmIsSpecialPoolAddressFree(
    _In_ PVOID P)
{
    UNREFERENCED_PARAMETER(P);
    return FALSE;
}

PVOID
NTAPI
MmAllocateSpecialPool(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag,
    _In_ POOL_TYPE PoolType,
    _In_ ULONG SpecialType)
{
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(Tag);
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(SpecialType);
    return NULL;
}

VOID
NTAPI
MmFreeSpecialPool(
    _In_ PVOID P)
{
    UNREFERENCED_PARAMETER(P);
}

BOOLEAN
NTAPI
MmRaisePoolQuota(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T CurrentMaxQuota,
    _Out_ PSIZE_T NewMaxQuota)
{
    SIZE_T Limit = ((PoolType & 1) == PagedPool) ? MmSizeOfPagedPoolInBytes
                                                                   : MmMaximumNonPagedPoolInBytes;

    if (CurrentMaxQuota + 64 * _1KB > Limit / 2)
        return FALSE;

    *NewMaxQuota = CurrentMaxQuota + 64 * _1KB;
    return TRUE;
}

VOID
NTAPI
MmReturnPoolQuota(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T QuotaToReturn)
{
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(QuotaToReturn);
}
