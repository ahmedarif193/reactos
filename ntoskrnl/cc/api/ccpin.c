/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/api/ccpin.c
 * PURPOSE:     Cached file pinning interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccnt.h"

VOID
CcNtBcbCreated(
    _Inout_ PCC_BCB Bcb)
{
    PCC_NT_BCB NtBcb = (PCC_NT_BCB)Bcb;

    ExInitializeResourceLite(&NtBcb->Engine.Resource);
    NtBcb->Public.NodeTypeCode = 0x2FD;
    NtBcb->Public.NodeByteSize = 0;
}

VOID
CcNtBcbDeleting(
    _Inout_ PCC_BCB Bcb)
{
    ExDeleteResourceLite(&Bcb->Resource);
}

static
BOOLEAN
CcNtAcquireBcb(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _In_ BOOLEAN Pinned,
    _Out_ PCC_NT_BCB *OutBcb,
    _Out_ PVOID *Buffer)
{
    PCC_NT_MAP NtMap;
    ULONG64 Offset;
    PCC_NT_BCB NtBcb;
    BOOLEAN Created;
    BOOLEAN Wait = (BOOLEAN)((Flags & PIN_WAIT) != 0);
    NTSTATUS Status;
    PCC_BCB Bcb;

    *OutBcb = NULL;
    if (FileObject == NULL || FileOffset == NULL || Buffer == NULL)
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    *Buffer = NULL;
    Offset = (ULONG64)FileOffset->QuadPart;
    NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);

    if (NtMap == NULL)
        ExRaiseStatus(STATUS_INVALID_PARAMETER);

    if (!(Flags & PIN_WAIT) && !NtMap->Map.Ops.IsResident(NtMap->Map.Context, Offset, Length))
    {
        CcNtDereferenceMap(NtMap);
        return FALSE;
    }

    Status = CcBcbAcquire(&NtMap->Map, Offset, Length, Pinned, (BOOLEAN)((Flags & PIN_IF_BCB) != 0), &Bcb,
                          &Created);
    if (Status == STATUS_CANT_WAIT)
    {
        CcNtDereferenceMap(NtMap);
        return FALSE;
    }

    if (NT_SUCCESS(Status) && Wait && !(Flags & PIN_NO_READ))
    {
        Status = CcBcbMakeResident(Bcb, FALSE);
        if (!NT_SUCCESS(Status))
            CcBcbDereference(Bcb);
    }

    if (!NT_SUCCESS(Status))
    {
        CcNtDereferenceMap(NtMap);
        ExRaiseStatus(Status);
    }

    NtBcb = (PCC_NT_BCB)Bcb;

    if (Pinned)
    {
        BOOLEAN Acquired;

        if (Wait && (Flags & PIN_EXCLUSIVE))
            Acquired = ExAcquireResourceExclusiveLite(&NtBcb->Engine.Resource, TRUE);
        else
            Acquired = ExAcquireResourceSharedLite(&NtBcb->Engine.Resource, Wait);

        if (!Acquired)
        {
            CcBcbDereference(Bcb);
            CcNtDereferenceMap(NtMap);
            return FALSE;
        }

        CcBcbPin(Bcb);
    }

    InterlockedIncrement(&NtMap->ReferenceCount);
    CcNtDereferenceMap(NtMap);

    *OutBcb = NtBcb;
    *Buffer = CcBcbAddress(Bcb, Offset);
    return TRUE;
}

static
VOID
CcNtReleaseBcb(
    _Inout_ PCC_NT_BCB NtBcb,
    _In_ BOOLEAN Pinned,
    _In_opt_ ERESOURCE_THREAD Thread)
{
    PCC_NT_MAP NtMap = CONTAINING_RECORD(NtBcb->Engine.Map, CC_NT_MAP, Map);

    if (Pinned)
    {
        if (Thread != 0)
            ExReleaseResourceForThreadLite(&NtBcb->Engine.Resource, Thread);
        else
            ExReleaseResourceLite(&NtBcb->Engine.Resource);

        CcBcbUnpin(&NtBcb->Engine);
    }

    CcBcbDereference(&NtBcb->Engine);
    CcNtDereferenceMap(NtMap);
}

BOOLEAN
NTAPI
CcMapData(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _Out_ PVOID *Bcb,
    _Out_ PVOID *Buffer)
{
    PCC_NT_BCB NtBcb;

    *Bcb = NULL;

    if (Flags & MAP_WAIT)
        CcMapDataWait++;
    else
        CcMapDataNoWait++;

    if (!CcNtAcquireBcb(FileObject, FileOffset, Length, (Flags & MAP_WAIT) ? PIN_WAIT : 0, FALSE, &NtBcb, Buffer))
        return FALSE;

    *Bcb = &NtBcb->Public;
    return TRUE;
}

BOOLEAN
NTAPI
CcPinRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _Out_ PVOID *Bcb,
    _Out_ PVOID *Buffer)
{
    PCC_NT_BCB NtBcb;

    *Bcb = NULL;

    if (Flags & PIN_WAIT)
        CcPinReadWait++;
    else
        CcPinReadNoWait++;

    if (!CcNtAcquireBcb(FileObject, FileOffset, Length, Flags, TRUE, &NtBcb, Buffer))
        return FALSE;

    *Bcb = &NtBcb->Public;
    return TRUE;
}

BOOLEAN
NTAPI
CcPinMappedData(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _Inout_ PVOID *Bcb)
{
    PCC_NT_BCB Mapped = CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)*Bcb);
    PCC_NT_BCB NtBcb;
    PVOID Buffer;

    CcPinMappedDataCount++;

    if (Mapped->Engine.Pinned)
        return TRUE;

    if (!CcNtAcquireBcb(FileObject, FileOffset, Length, Flags, TRUE, &NtBcb, &Buffer))
        return FALSE;

    CcNtReleaseBcb(Mapped, FALSE, 0);
    *Bcb = &NtBcb->Public;
    return TRUE;
}

BOOLEAN
NTAPI
CcPreparePinWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Zero,
    _In_ ULONG Flags,
    _Out_ PVOID *Bcb,
    _Out_ PVOID *Buffer)
{
    if (!CcPinRead(FileObject, FileOffset, Length, Flags | PIN_EXCLUSIVE, Bcb, Buffer))
        return FALSE;

    if (Zero)
        RtlZeroMemory(*Buffer, Length);

    CcSetDirtyPinnedData(*Bcb, NULL);
    return TRUE;
}

VOID
NTAPI
CcSetDirtyPinnedData(
    _In_ PVOID BcbVoid,
    _In_opt_ PLARGE_INTEGER Lsn)
{
    PCC_NT_BCB NtBcb = CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)BcbVoid);
    NTSTATUS Status = CcBcbSetDirty(&NtBcb->Engine, (Lsn != NULL) ? (ULONG64)Lsn->QuadPart : 0);

    if (!NT_SUCCESS(Status))
        ExRaiseStatus(Status);
}

VOID
NTAPI
CcUnpinData(
    _In_ PVOID Bcb)
{
    PCC_NT_BCB NtBcb = CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb);

    CcNtReleaseBcb(NtBcb, NtBcb->Engine.Pinned, 0);
}

VOID
NTAPI
CcUnpinDataForThread(
    _In_ PVOID Bcb,
    _In_ ERESOURCE_THREAD ResourceThreadId)
{
    PCC_NT_BCB NtBcb = CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb);

    CcNtReleaseBcb(NtBcb, NtBcb->Engine.Pinned, ResourceThreadId);
}

VOID
NTAPI
CcSetBcbOwnerPointer(
    _In_ PVOID Bcb,
    _In_ PVOID OwnerPointer)
{
    PCC_NT_BCB NtBcb = CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb);

    if (NtBcb->Engine.Pinned)
        ExSetResourceOwnerPointer(&NtBcb->Engine.Resource, OwnerPointer);
}

VOID
NTAPI
CcRepinBcb(
    _In_ PVOID Bcb)
{
    PCC_NT_BCB NtBcb = CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb);
    PCC_NT_MAP NtMap = CONTAINING_RECORD(NtBcb->Engine.Map, CC_NT_MAP, Map);

    CcBcbReference(&NtBcb->Engine);
    InterlockedIncrement(&NtMap->ReferenceCount);
}

VOID
NTAPI
CcUnpinRepinnedBcb(
    _In_ PVOID Bcb,
    _In_ BOOLEAN WriteThrough,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    PCC_NT_BCB NtBcb = CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb);
    PCC_NT_MAP NtMap = CONTAINING_RECORD(NtBcb->Engine.Map, CC_NT_MAP, Map);

    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = 0;

    if (WriteThrough && NtBcb->Engine.Dirty)
        IoStatus->Status = CcNtFlushMap(NtMap, NtBcb->Engine.FileOffset, NtBcb->Engine.Length, ~0u, NULL);

    CcBcbDereference(&NtBcb->Engine);
    CcNtDereferenceMap(NtMap);
}

PVOID
NTAPI
CcRemapBcb(
    _In_ PVOID Bcb)
{
    CcRepinBcb(Bcb);
    return Bcb;
}
