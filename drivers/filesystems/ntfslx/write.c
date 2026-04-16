/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Conservative write-path helpers for staged ntfslx
 *
 * Integration note:
 * - This file is intentionally self-contained and not wired into dispatch yet.
 * - The main thread can later call NtfslxWriteFileObjectConservative() from
 *   IRP_MJ_WRITE once the surrounding file-object and cache-manager plumbing is
 *   ready.
 * - Only overwrite-only resident writes and overwrite-only nonresident writes
 *   are accepted. Anything that would require allocation growth, compression
 *   handling, sparse fill semantics, or journaling is rejected with
 *   deterministic NTSTATUS values instead of being faked.
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

#define NTFSLX_MAX_ULONG_LONG ((ULONGLONG)-1)
#define NTFSLX_ATTRIBUTE_FLAG_COMPRESSED 0x0001
#define NTFSLX_ATTRIBUTE_FLAG_SPARSE 0x0002
#define NTFSLX_ATTRIBUTE_FLAG_ENCRYPTED 0x4000

static
NTSTATUS
NtfslxValidateConservativeWriteContext(
    _In_ PFILE_OBJECT FileObject,
    _In_ PNTFSLX_FILE_CONTEXT FileContext);

static
BOOLEAN
NtfslxSafeAddUlongLong(
    _In_ ULONGLONG Left,
    _In_ ULONGLONG Right,
    _Out_ ULONGLONG *Result)
{
    if (Left > NTFSLX_MAX_ULONG_LONG - Right)
    {
        return FALSE;
    }

    if (Result != NULL)
    {
        *Result = Left + Right;
    }

    return TRUE;
}

static
BOOLEAN
NtfslxSafeMultiplyUlongLong(
    _In_ ULONGLONG Left,
    _In_ ULONGLONG Right,
    _Out_ ULONGLONG *Result)
{
    if (Left != 0 && Right > NTFSLX_MAX_ULONG_LONG / Left)
    {
        return FALSE;
    }

    if (Result != NULL)
    {
        *Result = Left * Right;
    }

    return TRUE;
}

static
LONGLONG
NtfslxRunlistVcnToLcn(
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ LONGLONG Vcn)
{
    ULONG Index;

    if (Runlist == NULL)
    {
        return NTFSLX_LCN_RL_NOT_MAPPED;
    }

    if (Vcn < Runlist[0].Vcn)
    {
        return NTFSLX_LCN_ENOENT;
    }

    for (Index = 0; Runlist[Index].Length != 0; ++Index)
    {
        if (Vcn < Runlist[Index + 1].Vcn)
        {
            if (Runlist[Index].Lcn >= 0)
            {
                return Runlist[Index].Lcn + (Vcn - Runlist[Index].Vcn);
            }

            return Runlist[Index].Lcn;
        }
    }

    return Runlist[Index].Lcn;
}

static
NTSTATUS
NtfslxValidateConservativeWriteContext(
    _In_ PFILE_OBJECT FileObject,
    _In_ PNTFSLX_FILE_CONTEXT FileContext)
{
    if (FileObject == NULL ||
        FileContext == NULL ||
        FileContext->Signature != NTFSLX_FILE_CONTEXT_SIGNATURE ||
        FileContext->DeviceExtension == NULL ||
        FileContext->DeviceExtension->Signature != NTFSLX_TAG ||
        FileContext->DeviceExtension->DeviceObject == NULL ||
        FileContext->DeviceExtension->StorageDevice == NULL ||
        FileContext->FileRecord == NULL ||
        FileContext->DataAttribute == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (!NtfslxIsVolumeDevice(FileContext->DeviceExtension))
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (!FileObject->WriteAccess)
    {
        return STATUS_ACCESS_DENIED;
    }

    if (!FileContext->SupportsWrite)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (FileContext->DataSize > (ULONGLONG)MAXLONGLONG ||
        FileContext->AllocationSize > (ULONGLONG)MAXLONGLONG)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (FileObject->DeviceObject == NULL ||
        FileObject->DeviceObject != FileContext->DeviceExtension->DeviceObject ||
        FileObject->Vpb != FileContext->DeviceExtension->Vpb)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (FileObject->PrivateCacheMap != NULL ||
        (FileObject->SectionObjectPointer != NULL &&
         CcIsFileCached(FileObject)))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (FileContext->IsDirectory)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (!FileContext->ResidentData)
    {
        if (FileContext->DataRunlist == NULL || FileContext->DataRunlistCount == 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if ((FileContext->DataAttribute->Flags &
             (NTFSLX_ATTRIBUTE_FLAG_COMPRESSED |
              NTFSLX_ATTRIBUTE_FLAG_SPARSE |
              NTFSLX_ATTRIBUTE_FLAG_ENCRYPTED)) != 0 ||
            FileContext->DataAttribute->Data.NonResident.CompressionUnit != 0)
        {
            return STATUS_NOT_SUPPORTED;
        }

        if (FileContext->DataAttribute->Data.NonResident.DataSize != FileContext->DataSize ||
            FileContext->DataAttribute->Data.NonResident.AllocatedSize != FileContext->AllocationSize ||
            FileContext->DataAttribute->Data.NonResident.AllocatedSize <
                FileContext->DataAttribute->Data.NonResident.DataSize ||
            FileContext->DataAttribute->Data.NonResident.InitializedSize >
                FileContext->DataAttribute->Data.NonResident.DataSize)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (FileContext->DataAttribute->Data.NonResident.InitializedSize !=
            FileContext->DataAttribute->Data.NonResident.DataSize)
        {
            return STATUS_NOT_SUPPORTED;
        }
    }
    else
    {
        if (FileContext->DataAttribute->Flags != 0)
        {
            return STATUS_NOT_SUPPORTED;
        }

        if (FileContext->DataAttribute->Data.Resident.ValueLength != FileContext->DataSize ||
            FileContext->AllocationSize != FileContext->DataSize)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
    }

    ASSERT(FileContext->DeviceExtension->StorageDevice != NULL);
    ASSERT(FileContext->DeviceExtension->VolumeInfo.BytesPerSector != 0);

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxPreWriteMstFixupLocal(
    _Inout_ PNTFSLX_RECORD_HEADER Record,
    _In_ ULONG Size,
    _In_ ULONG SectorSize)
{
    USHORT UsaOffset;
    USHORT UsaCount;
    PUSHORT UsaEntry;
    PUSHORT DataEntry;
    USHORT UpdateSequenceNumber;
    ULONG SectorCount;
    ULONG Index;
    ULONG UsaBytes;

    if (Record == NULL || SectorSize == 0 || !NtfslxIsPowerOfTwo(SectorSize))
    {
        return STATUS_INVALID_PARAMETER;
    }

    UsaOffset = Record->UsaOffset;
    UsaCount = Record->UsaCount;
    if (UsaCount == 0)
    {
        return STATUS_SUCCESS;
    }

    if ((Size == 0) ||
        ((Size & (SectorSize - 1)) != 0) ||
        (UsaOffset & 1) != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if ((ULONG)UsaOffset > Size)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    UsaBytes = (ULONG)UsaCount * sizeof(USHORT);
    if (UsaBytes > Size ||
        (ULONG)UsaOffset > (Size - UsaBytes))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    SectorCount = Size / SectorSize;
    if ((ULONG)UsaCount != (SectorCount + 1))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    UsaEntry = (PUSHORT)((PUCHAR)Record + UsaOffset);
    UpdateSequenceNumber = UsaEntry[0];
    DataEntry = (PUSHORT)((PUCHAR)Record + SectorSize - sizeof(USHORT));

    for (Index = 0; Index < SectorCount; ++Index)
    {
        UsaEntry[Index + 1] = *DataEntry;
        *DataEntry = UpdateSequenceNumber;
        DataEntry = (PUSHORT)((PUCHAR)DataEntry + SectorSize);
    }

    return STATUS_SUCCESS;
}

/* NtfslxWriteDisk is now provided by diskwrite.c */

static
NTSTATUS
NtfslxWriteMappedData(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG ByteOffset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) PUCHAR Buffer)
{
    ULONGLONG Remaining;
    ULONGLONG CurrentOffset;
    LONGLONG Vcn;
    LONGLONG Lcn;
    ULONG ClusterOffset;
    ULONG ChunkLength;
    ULONGLONG PhysicalOffset;
    NTSTATUS Status;

    if (StorageDevice == NULL || VolumeInfo == NULL || Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (VolumeInfo->BytesPerSector == 0 || VolumeInfo->BytesPerCluster == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ASSERT(NtfslxIsPowerOfTwo(VolumeInfo->BytesPerSector));
    ASSERT(NtfslxIsPowerOfTwo(VolumeInfo->BytesPerCluster));

    if (Length == 0)
    {
        return STATUS_SUCCESS;
    }

    if (Runlist == NULL)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    Remaining = Length;
    CurrentOffset = ByteOffset;

    while (Remaining != 0)
    {
        Vcn = (LONGLONG)(CurrentOffset / VolumeInfo->BytesPerCluster);
        ClusterOffset = (ULONG)(CurrentOffset % VolumeInfo->BytesPerCluster);
        ChunkLength = VolumeInfo->BytesPerCluster - ClusterOffset;
        if ((ULONGLONG)ChunkLength > Remaining)
        {
            ChunkLength = (ULONG)Remaining;
        }

        Lcn = NtfslxRunlistVcnToLcn(Runlist, Vcn);
        if (Lcn == NTFSLX_LCN_HOLE ||
            Lcn == NTFSLX_LCN_RL_NOT_MAPPED ||
            Lcn == NTFSLX_LCN_ENOENT)
        {
            return STATUS_NOT_IMPLEMENTED;
        }

        if (Lcn < 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if ((ULONGLONG)Lcn > NTFSLX_MAX_ULONG_LONG / VolumeInfo->BytesPerCluster)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        PhysicalOffset = (ULONGLONG)Lcn * VolumeInfo->BytesPerCluster;
        if (PhysicalOffset > NTFSLX_MAX_ULONG_LONG - ClusterOffset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Status = NtfslxWriteDisk(StorageDevice,
                                 (LONGLONG)(PhysicalOffset + ClusterOffset),
                                 ChunkLength,
                                 VolumeInfo->BytesPerSector,
                                 Buffer,
                                 TRUE);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        Buffer += ChunkLength;
        CurrentOffset += ChunkLength;
        Remaining -= ChunkLength;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxPrepareResidentWriteBuffer(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG ByteOffset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _Outptr_ PVOID *OutputBuffer)
{
    PVOID RecordCopy;
    ULONG RecordLength;
    ULONG AttributeOffset;
    PNTFSLX_ATTR_RECORD Attribute;
    ULONGLONG CopyOffset;
    NTSTATUS Status;

    *OutputBuffer = NULL;

    ASSERT(FileContext != NULL);
    ASSERT(FileContext->FileRecord != NULL);
    ASSERT(FileContext->DataAttribute != NULL);
    ASSERT(FileContext->ResidentData);

    if (FileContext->DeviceExtension == NULL ||
        FileContext->DeviceExtension->VolumeInfo.BytesPerFileRecord == 0 ||
        FileContext->DeviceExtension->VolumeInfo.BytesPerSector == 0 ||
        !NtfslxIsPowerOfTwo(FileContext->DeviceExtension->VolumeInfo.BytesPerSector))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    RecordLength = FileContext->DeviceExtension->VolumeInfo.BytesPerFileRecord;
    if ((RecordLength % FileContext->DeviceExtension->VolumeInfo.BytesPerSector) != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    AttributeOffset = (ULONG)((PUCHAR)FileContext->DataAttribute - (PUCHAR)FileContext->FileRecord);
    if (AttributeOffset >= RecordLength)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (ByteOffset > FileContext->DataSize ||
        !NtfslxSafeAddUlongLong(ByteOffset, Length, &CopyOffset) ||
        CopyOffset > FileContext->DataSize)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    RecordCopy = ExAllocatePoolWithTag(NonPagedPool, RecordLength, NTFSLX_TAG);
    if (RecordCopy == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(RecordCopy, FileContext->FileRecord, RecordLength);

    Attribute = (PNTFSLX_ATTR_RECORD)((PUCHAR)RecordCopy + AttributeOffset);
    if (Attribute->NonResident != 0)
    {
        ExFreePoolWithTag(RecordCopy, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Attribute->Data.Resident.ValueLength != FileContext->DataSize ||
        FileContext->AllocationSize != FileContext->DataSize ||
        Attribute->Data.Resident.ValueOffset > Attribute->Length)
    {
        ExFreePoolWithTag(RecordCopy, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Attribute->Data.Resident.ValueOffset > NTFSLX_MAX_ULONG_LONG - CopyOffset ||
        (Attribute->Data.Resident.ValueOffset + CopyOffset) > Attribute->Length)
    {
        ExFreePoolWithTag(RecordCopy, NTFSLX_TAG);
        return STATUS_NOT_IMPLEMENTED;
    }

    RtlCopyMemory((PUCHAR)Attribute + Attribute->Data.Resident.ValueOffset + ByteOffset,
                  Buffer,
                  Length);

    Status = NtfslxPreWriteMstFixupLocal(&((PNTFSLX_MFT_RECORD)RecordCopy)->Ntfs,
                                    RecordLength,
                                    FileContext->DeviceExtension->VolumeInfo.BytesPerSector);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(RecordCopy, NTFSLX_TAG);
        return Status;
    }

    *OutputBuffer = RecordCopy;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxWriteResidentData(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG ByteOffset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) const VOID *Buffer)
{
    PVOID RecordCopy;
    ULONG RecordLength;
    ULONGLONG MftOffset;
    NTSTATUS Status;

    if (ByteOffset > FileContext->DataSize)
    {
        return STATUS_END_OF_FILE;
    }

    if (Length == 0)
    {
        return STATUS_SUCCESS;
    }

    if (!NtfslxSafeAddUlongLong(ByteOffset, Length, &MftOffset) ||
        MftOffset > FileContext->DataSize)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = NtfslxPrepareResidentWriteBuffer(FileContext,
                                              ByteOffset,
                                              Length,
                                              Buffer,
                                              &RecordCopy);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RecordLength = FileContext->DeviceExtension->VolumeInfo.BytesPerFileRecord;
    if (!NtfslxSafeMultiplyUlongLong(FileContext->MftIndex,
                                     RecordLength,
                                     &MftOffset))
    {
        ExFreePoolWithTag(RecordCopy, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = NtfslxWriteMappedData(FileContext->DeviceExtension->StorageDevice,
                                   &FileContext->DeviceExtension->VolumeInfo,
                                   FileContext->DeviceExtension->MftRunlist,
                                   MftOffset,
                                   RecordLength,
                                   (PUCHAR)RecordCopy);
    if (NT_SUCCESS(Status))
    {
        ULONG AttributeOffset;
        PNTFSLX_ATTR_RECORD DestinationAttribute;

        AttributeOffset = (ULONG)((PUCHAR)FileContext->DataAttribute - (PUCHAR)FileContext->FileRecord);
        DestinationAttribute = (PNTFSLX_ATTR_RECORD)((PUCHAR)FileContext->FileRecord + AttributeOffset);
        RtlCopyMemory((PUCHAR)DestinationAttribute + DestinationAttribute->Data.Resident.ValueOffset + ByteOffset,
                      Buffer,
                      Length);
    }

    ExFreePoolWithTag(RecordCopy, NTFSLX_TAG);
    return Status;
}

static
NTSTATUS
NtfslxWriteNonResidentData(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG ByteOffset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) const VOID *Buffer)
{
    ULONGLONG EndOffset;

    if (FileContext->DataRunlist == NULL)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    if (FileContext->DataAttribute->Flags != 0 ||
        FileContext->DataAttribute->Data.NonResident.CompressionUnit != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (FileContext->DataAttribute->Data.NonResident.DataSize != FileContext->DataSize ||
        FileContext->DataAttribute->Data.NonResident.AllocatedSize != FileContext->AllocationSize ||
        FileContext->DataAttribute->Data.NonResident.InitializedSize != FileContext->DataSize)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (ByteOffset > FileContext->DataSize)
    {
        return STATUS_END_OF_FILE;
    }

    if (Length == 0)
    {
        return STATUS_SUCCESS;
    }

    if (!NtfslxSafeAddUlongLong(ByteOffset, Length, &EndOffset) ||
        EndOffset > FileContext->DataSize)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    return NtfslxWriteMappedData(FileContext->DeviceExtension->StorageDevice,
                                 &FileContext->DeviceExtension->VolumeInfo,
                                 FileContext->DataRunlist,
                                 ByteOffset,
                                 Length,
                                 (PUCHAR)Buffer);
}

NTSTATUS
NtfslxWriteFileObjectConservative(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _Out_opt_ PULONG BytesWritten)
{
    PNTFSLX_FILE_CONTEXT FileContext;
    LARGE_INTEGER EffectiveOffset;
    BOOLEAN UsesCurrentPosition;
    NTSTATUS Status;

    if (BytesWritten != NULL)
    {
        *BytesWritten = 0;
    }

    if (FileObject == NULL || Buffer == NULL || ByteOffset == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxValidateOpenFileObject(FileObject, &FileContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ASSERT(FileContext != NULL);
    ASSERT(FileContext->DeviceExtension != NULL);
    ASSERT(FileContext->FileRecord != NULL);

    Status = NtfslxValidateConservativeWriteContext(FileObject, FileContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    UsesCurrentPosition = (ByteOffset->LowPart == FILE_USE_FILE_POINTER_POSITION &&
                           ByteOffset->HighPart == -1);
    if (UsesCurrentPosition)
    {
        EffectiveOffset = FileContext->CurrentByteOffset;
    }
    else
    {
        EffectiveOffset = *ByteOffset;
    }

    if (EffectiveOffset.QuadPart < 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Length == 0)
    {
        return STATUS_SUCCESS;
    }

    if (FileContext->ResidentData)
    {
        Status = NtfslxWriteResidentData(FileContext,
                                         (ULONGLONG)EffectiveOffset.QuadPart,
                                         Length,
                                         Buffer);
    }
    else
    {
        Status = NtfslxWriteNonResidentData(FileContext,
                                            (ULONGLONG)EffectiveOffset.QuadPart,
                                            Length,
                                            Buffer);
    }

    if (NT_SUCCESS(Status))
    {
        if (UsesCurrentPosition)
        {
            ULONGLONG CurrentOffset;

            if (!NtfslxSafeAddUlongLong((ULONGLONG)EffectiveOffset.QuadPart,
                                        Length,
                                        &CurrentOffset))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            FileContext->CurrentByteOffset.QuadPart = (LONGLONG)CurrentOffset;
        }

        if (BytesWritten != NULL)
        {
            *BytesWritten = Length;
        }
    }

    return Status;
}
