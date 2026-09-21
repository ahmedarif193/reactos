/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/engine/cachemap.c
 * PURPOSE:     Shared cache map management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/ccengine.h"

VOID
CcMapInitialize(
    _Out_ PCC_MAP Map,
    _In_ PCC_CACHE Cache,
    _In_ PCC_BACKING_OPS Ops,
    _In_ PVOID Context,
    _In_ ULONG64 SectionSize,
    _In_ ULONG64 FileSize,
    _In_ ULONG64 ValidDataLength)
{
    RtlZeroMemory(Map, sizeof(*Map));
    Map->NodeByteSize = sizeof(*Map);
    Map->Cache = Cache;
    Map->Ops = *Ops;
    Map->Context = Context;
    Map->SectionSize = SectionSize;
    Map->FileSize = FileSize;
    Map->ValidDataLength = ValidDataLength;
    CC_LOCK_INIT(&Map->IndexLock);
    CC_LOCK_INIT(&Map->BcbLock);
    InitializeListHead(&Map->BcbList);
    InitializeListHead(&Map->DirtyLink);
}

VOID
CcMapSetSizes(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 SectionSize,
    _In_ ULONG64 FileSize,
    _In_ ULONG64 ValidDataLength)
{
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Map->IndexLock, &OldIrql);
    CC_ATOMIC_WRITE64(&Map->SectionSize, SectionSize);
    Map->FileSize = FileSize;
    Map->ValidDataLength = ValidDataLength;
    CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);
}

BOOLEAN
CcMapUninitialize(
    _Inout_ PCC_MAP Map)
{
    ULONG i;

    if (CcBcbRangeBusy(Map, 0, (ULONG64)-1))
        return FALSE;

    CcBcbCompleteFlush(Map, 0, (ULONG64)-1, (ULONG64)-1);

    if (!CcMapDetachViews(Map, 0, (ULONG64)-1))
        return FALSE;

    CcDirtyDiscard(Map, 0, (ULONG64)-1);

    for (i = 0; i < Map->LeafCount; i++)
    {
        if (Map->Leaf[i] != NULL)
            CC_FREE(Map->Leaf[i]);
    }

    if (Map->Leaf != NULL)
        CC_FREE(Map->Leaf);

    Map->Leaf = NULL;
    CC_ATOMIC_WRITE32(&Map->LeafCount, 0);
    return TRUE;
}
