/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/ex/exfree.c
 * PURPOSE:     Executive pool deallocation interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "expool.h"

#define NDEBUG
#if !defined(POOL_HOST_TEST)
#include <debug.h>
#endif

VOID
NTAPI
ExFreePoolWithTag(
    IN PVOID P,
    IN ULONG TagToFree)
{
    POOL_BLOCK_INFO Info;
    PPOOL_HEAP Heap;
    ULONG HeapIndex;

    UNREFERENCED_PARAMETER(TagToFree);

    if (P == NULL)
        return;

    if ((ExpPoolFlags & EX_POOL_FLAG_SPECIAL_POOL) && MmIsSpecialPoolAddress(P))
    {
        MmFreeSpecialPool(P);
        return;
    }

    Heap = ExpPoolHeapFromAddress(P, &HeapIndex);
    if (Heap == NULL)
        KeBugCheckEx(BAD_POOL_CALLER, 0x46, (ULONG_PTR)P, 0, 0);

    if (HeapIndex == EX_POOL_HEAP_PAGED && KeGetCurrentIrql() > APC_LEVEL)
        KeBugCheckEx(BAD_POOL_CALLER, 0x09, KeGetCurrentIrql(), PagedPool, (ULONG_PTR)P);

    if (!PoolHeapFree(Heap, P, &Info))
        KeBugCheckEx(BAD_POOL_CALLER, 0x07, (ULONG_PTR)P, HeapIndex, 0);

    PoolTagRelease(&ExpPoolState.Tracker, Info.Tag, ExpPoolClass(HeapIndex), Info.Size);

    if (Info.Flags & EX_POOL_BLOCK_QUOTA)
    {
        PEPROCESS Process = (PEPROCESS)Info.Trailer;

        if (Process != NULL)
        {
            PsReturnPoolQuota(Process, ExpPoolClass(HeapIndex), Info.Size);
            ObDereferenceObject(Process);
        }
    }
}

VOID
NTAPI
ExFreePool(
    PVOID P)
{
    ExFreePoolWithTag(P, 0);
}

VOID
NTAPI
ExFreePool2(
    IN PVOID P,
    IN ULONG Tag,
    IN PCPOOL_EXTENDED_PARAMETER ExtendedParameters,
    IN ULONG ExtendedParametersCount)
{
    UNREFERENCED_PARAMETER(ExtendedParameters);
    UNREFERENCED_PARAMETER(ExtendedParametersCount);
    ExFreePoolWithTag(P, Tag);
}
