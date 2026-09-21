/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/api/ccmdl.c
 * PURPOSE:     MDL-based cached file access interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccnt.h"

static
PMDL
CcNtBuildMdl(
    _Inout_ PCC_NT_MAP NtMap,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _In_ LOCK_OPERATION Operation,
    _Inout_ PMDL *Chain)
{
    while (Length != 0)
    {
        CC_VIEW_RANGE Range;
        NTSTATUS Status;
        PMDL Mdl, Tail;

        Status = CcViewAcquire(&NtMap->Map, Offset, Length, &Range);
        if (NT_SUCCESS(Status))
        {
            Status = CcViewMakeResident(&NtMap->Map, &Range, (BOOLEAN)(Operation != IoReadAccess));
            if (!NT_SUCCESS(Status))
                CcViewRelease(&Range);
        }

        if (!NT_SUCCESS(Status))
            ExRaiseStatus(Status);

        Mdl = IoAllocateMdl(Range.Address, Range.Length, FALSE, FALSE, NULL);
        if (Mdl == NULL)
        {
            CcViewRelease(&Range);
            ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
        }

        _SEH2_TRY
        {
            MmProbeAndLockPages(Mdl, KernelMode, Operation);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            IoFreeMdl(Mdl);
            CcViewRelease(&Range);
            ExRaiseStatus(_SEH2_GetExceptionCode());
        }
        _SEH2_END;

        CcViewRelease(&Range);

        if (*Chain == NULL)
        {
            *Chain = Mdl;
        }
        else
        {
            for (Tail = *Chain; Tail->Next != NULL; Tail = Tail->Next)
                ;

            Tail->Next = Mdl;
        }

        Offset += Range.Length;
        Length -= Range.Length;
    }

    return *Chain;
}

static
VOID
CcNtFreeMdlChain(
    _In_ PMDL Chain)
{
    while (Chain != NULL)
    {
        PMDL Next = Chain->Next;

        MmUnlockPages(Chain);
        IoFreeMdl(Chain);
        Chain = Next;
    }
}

VOID
NTAPI
CcMdlRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _Out_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);
    ULONG64 Offset = (ULONG64)FileOffset->QuadPart;

    *MdlChain = NULL;

    if (NtMap == NULL)
        ExRaiseStatus(STATUS_INVALID_PARAMETER);

    if (Offset >= NtMap->Map.FileSize)
        Length = 0;
    else if (Offset + Length > NtMap->Map.FileSize)
        Length = (ULONG)(NtMap->Map.FileSize - Offset);

    _SEH2_TRY
    {
        CcNtBuildMdl(NtMap, Offset, Length, IoReadAccess, MdlChain);
    }
    _SEH2_FINALLY
    {
        if (_SEH2_AbnormalTermination() && *MdlChain != NULL)
        {
            CcNtFreeMdlChain(*MdlChain);
            *MdlChain = NULL;
        }

        CcNtDereferenceMap(NtMap);
    }
    _SEH2_END;

    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = Length;
}

VOID
NTAPI
CcMdlReadComplete2(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL MemoryDescriptorList)
{
    UNREFERENCED_PARAMETER(FileObject);
    CcNtFreeMdlChain(MemoryDescriptorList);
}

VOID
NTAPI
CcMdlReadComplete(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL MdlChain)
{
    PDEVICE_OBJECT DeviceObject = IoGetRelatedDeviceObject(FileObject);
    PFAST_IO_DISPATCH FastDispatch = DeviceObject->DriverObject->FastIoDispatch;

    if (FastDispatch != NULL && FastDispatch->MdlReadComplete != NULL &&
        FastDispatch->MdlReadComplete(FileObject, MdlChain, DeviceObject))
    {
        return;
    }

    CcMdlReadComplete2(FileObject, MdlChain);
}

VOID
NTAPI
CcPrepareMdlWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _Out_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);

    *MdlChain = NULL;

    if (NtMap == NULL)
        ExRaiseStatus(STATUS_INVALID_PARAMETER);

    _SEH2_TRY
    {
        CcNtBuildMdl(NtMap, (ULONG64)FileOffset->QuadPart, Length, IoWriteAccess, MdlChain);
    }
    _SEH2_FINALLY
    {
        if (_SEH2_AbnormalTermination() && *MdlChain != NULL)
        {
            CcNtFreeMdlChain(*MdlChain);
            *MdlChain = NULL;
        }

        CcNtDereferenceMap(NtMap);
    }
    _SEH2_END;

    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = Length;
}

VOID
NTAPI
CcMdlWriteComplete2(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PMDL MdlChain)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);
    ULONG64 Offset = (ULONG64)FileOffset->QuadPart;
    PMDL Mdl;

    for (Mdl = MdlChain; Mdl != NULL && NtMap != NULL; Mdl = Mdl->Next)
    {
        CcDirtyMark(&NtMap->Map, Offset, MmGetMdlByteCount(Mdl));
        Offset += MmGetMdlByteCount(Mdl);
    }

    CcNtFreeMdlChain(MdlChain);

    if (NtMap != NULL)
        CcNtDereferenceMap(NtMap);
}

VOID
NTAPI
CcMdlWriteComplete(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PMDL MdlChain)
{
    PDEVICE_OBJECT DeviceObject = IoGetRelatedDeviceObject(FileObject);
    PFAST_IO_DISPATCH FastDispatch = DeviceObject->DriverObject->FastIoDispatch;

    if (FastDispatch != NULL && FastDispatch->MdlWriteComplete != NULL &&
        FastDispatch->MdlWriteComplete(FileObject, FileOffset, MdlChain, DeviceObject))
    {
        return;
    }

    CcMdlWriteComplete2(FileObject, FileOffset, MdlChain);
}

VOID
NTAPI
CcMdlWriteAbort(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL MdlChain)
{
    UNREFERENCED_PARAMETER(FileObject);
    CcNtFreeMdlChain(MdlChain);
}
