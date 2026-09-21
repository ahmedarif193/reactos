/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/engine/copy.c
 * PURPOSE:     Cached data transfer and view access
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/ccengine.h"

NTSTATUS
CcCopyRange(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 FileOffset,
    _In_ ULONG64 Length,
    _In_ BOOLEAN ForWrite,
    _In_ CC_MOVE_ROUTINE Move,
    _In_opt_ PVOID MoveContext,
    _Out_opt_ PULONG64 BytesMoved)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG64 Moved = 0;

    while (Length != 0)
    {
        ULONG Chunk = (Length > CC_VIEW_SIZE) ? CC_VIEW_SIZE : (ULONG)Length;
        CC_VIEW_RANGE Range;

        Status = CcViewAcquire(Map, FileOffset, Chunk, &Range);
        if (!NT_SUCCESS(Status))
            break;

        if (Map->Ops.MakeViewResident != NULL)
            Status = Map->Ops.MakeViewResident(Map->Context, Range.Address, Range.Length);
        else
            Status = CcViewMakeResident(Map, &Range, FALSE);
        if (NT_SUCCESS(Status))
        {
            CC_RESOURCE_ACQUIRE_SHARED(&Range.View->IoResource);
            Status = Move(MoveContext, Range.Address, Range.FileOffset, Range.Length);
            if (NT_SUCCESS(Status) && ForWrite)
                Status = CcDirtyMark(Map, Range.FileOffset, Range.Length);
            CC_RESOURCE_RELEASE(&Range.View->IoResource);
        }

        Chunk = Range.Length;
        CcViewRelease(&Range);

        if (!NT_SUCCESS(Status))
            break;

        FileOffset += Chunk;
        Length -= Chunk;
        Moved += Chunk;
    }

    if (BytesMoved != NULL)
        *BytesMoved = Moved;

    return Status;
}

NTSTATUS
CcPrefetchRange(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 FileOffset,
    _In_ ULONG64 Length)
{
    while (Length != 0)
    {
        ULONG Chunk = CC_VIEW_SIZE - (ULONG)(FileOffset & CC_VIEW_MASK);
        NTSTATUS Status;

        if (Chunk > Length)
            Chunk = (ULONG)Length;

        if (!Map->Ops.IsResident(Map->Context, FileOffset, Chunk))
        {
            Status = Map->Ops.MakeResident(Map->Context, FileOffset, Chunk, Map->ValidDataLength);
            if (!NT_SUCCESS(Status))
                return Status;
        }

        FileOffset += Chunk;
        Length -= Chunk;
    }

    return STATUS_SUCCESS;
}
