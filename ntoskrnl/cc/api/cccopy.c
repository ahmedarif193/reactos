/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/api/cccopy.c
 * PURPOSE:     NT cached copy interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccnt.h"

typedef struct _CC_NT_MOVE
{
    PUCHAR Buffer;
    ULONG64 BaseOffset;
    BOOLEAN ToCache;
    BOOLEAN Zero;
} CC_NT_MOVE, *PCC_NT_MOVE;

static
NTSTATUS
CcNtMove(
    _In_opt_ PVOID Context,
    _In_ PVOID CacheAddress,
    _In_ ULONG64 FileOffset,
    _In_ ULONG Length)
{
    PCC_NT_MOVE Move = Context;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Move->Zero && Move->Buffer == NULL)
        return STATUS_INVALID_USER_BUFFER;

    _SEH2_TRY
    {
        if (Move->Zero)
            RtlZeroMemory(CacheAddress, Length);
        else if (Move->ToCache)
            RtlCopyMemory(CacheAddress, Move->Buffer + (FileOffset - Move->BaseOffset), Length);
        else
            RtlCopyMemory(Move->Buffer + (FileOffset - Move->BaseOffset), CacheAddress, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        if (Status == STATUS_ACCESS_VIOLATION)
            Status = STATUS_INVALID_USER_BUFFER;
    }
    _SEH2_END;

    return Status;
}

static
VOID
CcNtReadAhead(
    _In_ PFILE_OBJECT FileObject,
    _Inout_ PCC_NT_MAP NtMap,
    _In_ ULONG64 Offset,
    _In_ ULONG Length)
{
    PCC_NT_PRIVATE Private = FileObject->PrivateCacheMap;
    ULONG64 AheadOffset;
    ULONG AheadLength;

    if (Private == NULL || (FileObject->Flags & FO_RANDOM_ACCESS))
        return;

    if (CcReadAheadNote(&Private->ReadAhead, Offset, Length, NtMap->Map.FileSize, &AheadOffset, &AheadLength))
        CcPrefetchRange(&NtMap->Map, AheadOffset, AheadLength);
}

BOOLEAN
NTAPI
CcCopyRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _Out_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    PCC_NT_MAP NtMap;
    ULONG64 Offset;
    CC_NT_MOVE Move = { Buffer, 0, FALSE, FALSE };
    ULONG64 Moved = 0;
    NTSTATUS Status;

    if (FileObject == NULL || FileOffset == NULL)
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    Offset = (ULONG64)FileOffset->QuadPart;
    Move.BaseOffset = Offset;
    NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);

    if (NtMap == NULL)
        ExRaiseStatus(STATUS_INVALID_PARAMETER);

    if (Offset >= NtMap->Map.FileSize)
    {
        Length = 0;
    }
    else if (Offset + Length > NtMap->Map.FileSize)
    {
        Length = (ULONG)(NtMap->Map.FileSize - Offset);
    }

    if (!Wait && Length != 0 && !NtMap->Map.Ops.IsResident(NtMap->Map.Context, Offset, Length))
    {
        CcFastReadNoWait++;
        if (NtMap->Map.Ops.Prefetch != NULL)
            NtMap->Map.Ops.Prefetch(NtMap->Map.Context, Offset, Length);
        CcNtDereferenceMap(NtMap);
        return FALSE;
    }

    if (Wait)
        CcFastReadWait++;

    Status = (Length != 0) ? CcCopyRange(&NtMap->Map, Offset, Length, FALSE, CcNtMove, &Move, &Moved)
                           : STATUS_SUCCESS;

    if (NT_SUCCESS(Status) && Length != 0)
        CcNtReadAhead(FileObject, NtMap, Offset, Length);

    CcNtDereferenceMap(NtMap);

    if (!NT_SUCCESS(Status))
        ExRaiseStatus(Status);

    if (IoStatus == NULL)
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = (ULONG_PTR)Moved;
    return TRUE;
}

BOOLEAN
NTAPI
CcCopyWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ PVOID Buffer)
{
    PCC_NT_MAP NtMap;
    ULONG64 Offset;
    CC_NT_MOVE Move = { Buffer, 0, TRUE, FALSE };
    NTSTATUS Status;

    if (FileObject == NULL || FileOffset == NULL)
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    Offset = (ULONG64)FileOffset->QuadPart;
    Move.BaseOffset = Offset;
    NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);

    if (NtMap == NULL)
        ExRaiseStatus(STATUS_INVALID_PARAMETER);

    if (!Wait && Length != 0 && !NtMap->Map.Ops.IsResident(NtMap->Map.Context, Offset, Length))
    {
        if (NtMap->Map.Ops.Prefetch != NULL)
            NtMap->Map.Ops.Prefetch(NtMap->Map.Context, Offset, Length);
        CcNtDereferenceMap(NtMap);
        return FALSE;
    }

    Status = (Length != 0) ? CcCopyRange(&NtMap->Map, Offset, Length, TRUE, CcNtMove, &Move, NULL)
                           : STATUS_SUCCESS;

    if (NT_SUCCESS(Status) && Offset + Length > NtMap->Map.ValidDataLength)
    {
        NtMap->Map.ValidDataLength = Offset + Length;
    }

    if (CC_ATOMIC_READ64(&CcNtCache.TotalDirtyPages) > CcNtCache.DirtyPageThreshold / 2)
        CcNtKickLazyWriter();

    CcNtDereferenceMap(NtMap);

    if (!NT_SUCCESS(Status))
        ExRaiseStatus(Status);

    return TRUE;
}

VOID
NTAPI
CcFastCopyRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG FileOffset,
    _In_ ULONG Length,
    _In_ ULONG PageCount,
    _Out_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    LARGE_INTEGER Offset;

    UNREFERENCED_PARAMETER(PageCount);

    Offset.QuadPart = FileOffset;
    CcCopyRead(FileObject, &Offset, Length, TRUE, Buffer, IoStatus);
}

VOID
NTAPI
CcFastCopyWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG FileOffset,
    _In_ ULONG Length,
    _In_ PVOID Buffer)
{
    LARGE_INTEGER Offset;

    Offset.QuadPart = FileOffset;
    CcCopyWrite(FileObject, &Offset, Length, TRUE, Buffer);
}

BOOLEAN
NTAPI
CcCopyReadEx(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _Out_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PETHREAD IoIssuerThread)
{
    UNREFERENCED_PARAMETER(IoIssuerThread);
    return CcCopyRead(FileObject, FileOffset, Length, Wait, Buffer, IoStatus);
}

BOOLEAN
NTAPI
CcCopyWriteEx(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ PVOID Buffer,
    _In_ PETHREAD IoIssuerThread)
{
    UNREFERENCED_PARAMETER(IoIssuerThread);
    return CcCopyWrite(FileObject, FileOffset, Length, Wait, Buffer);
}

BOOLEAN
NTAPI
CcCanIWrite(
    _In_opt_ PFILE_OBJECT FileObject,
    _In_ ULONG BytesToWrite,
    _In_ BOOLEAN Wait,
    _In_ UCHAR Retrying)
{
    PCC_NT_MAP NtMap = (FileObject != NULL) ? CcNtReferenceMap(FileObject->SectionObjectPointer) : NULL;
    LARGE_INTEGER Delay;
    BOOLEAN Allowed;
    ULONG Round = 0;

    UNREFERENCED_PARAMETER(Retrying);

    Delay.QuadPart = -20 * 10000;

    for (;;)
    {
        Allowed = CcDirtyCanWrite(&CcNtCache, (NtMap != NULL) ? &NtMap->Map : NULL, BytesToWrite);
        if (Allowed || !Wait || Round++ > 500)
            break;

        CcNtKickLazyWriter();
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    if (NtMap != NULL)
        CcNtDereferenceMap(NtMap);

    return (BOOLEAN)(Allowed || Wait);
}

BOOLEAN
NTAPI
CcZeroData(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER StartOffset,
    _In_ PLARGE_INTEGER EndOffset,
    _In_ BOOLEAN Wait)
{
    PCC_NT_MAP NtMap;
    ULONG64 Start;
    ULONG64 End;
    NTSTATUS Status = STATUS_SUCCESS;

    if (FileObject == NULL || StartOffset == NULL || EndOffset == NULL)
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    Start = (ULONG64)StartOffset->QuadPart;
    End = (ULONG64)EndOffset->QuadPart;
    NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);

    if (End <= Start)
    {
        if (NtMap != NULL)
            CcNtDereferenceMap(NtMap);

        return TRUE;
    }

    if (NtMap != NULL)
    {
        CC_NT_MOVE Move = { NULL, Start, TRUE, TRUE };

        if (!Wait)
        {
            CcNtDereferenceMap(NtMap);
            return FALSE;
        }

        Status = CcCopyRange(&NtMap->Map, Start, End - Start, TRUE, CcNtMove, &Move, NULL);
        CcNtDereferenceMap(NtMap);
    }
    else
    {
        PUCHAR Zero = ExAllocatePoolWithTag(NonPagedPool, 64 * 1024, 'oZcC');

        if (Zero == NULL)
            ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

        RtlZeroMemory(Zero, 64 * 1024);

        while (Start < End && NT_SUCCESS(Status))
        {
            ULONG Chunk = (ULONG)min(End - Start, 64 * 1024ULL);
            ULONG Transferred;

            Status = MiPagingIo(FileObject, Start, ROUND_UP(Chunk, 512), Zero, TRUE, &Transferred);
            Start += Chunk;
        }

        ExFreePoolWithTag(Zero, 'oZcC');
    }

    if (!NT_SUCCESS(Status))
        ExRaiseStatus(Status);

    return TRUE;
}

VOID
NTAPI
CcScheduleReadAhead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length)
{
    PCC_NT_MAP NtMap;
    ULONG64 Offset;

    if (FileObject == NULL || FileOffset == NULL)
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    Offset = (ULONG64)FileOffset->QuadPart;
    NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);

    if (NtMap == NULL)
        return;

    CcNtReadAhead(FileObject, NtMap, Offset, Length);
    CcNtDereferenceMap(NtMap);
}
