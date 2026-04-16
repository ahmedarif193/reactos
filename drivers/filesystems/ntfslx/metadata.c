/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     NTFS metadata reading and mapping-pairs decoding
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
NtfslxReadPrimaryMftRecordDirect(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _Out_writes_bytes_(VolumeInfo->BytesPerFileRecord) PNTFSLX_MFT_RECORD Record)
{
    ULONGLONG MftOffset;
    NTSTATUS Status;

    if (VolumeInfo->MftLcn > (ULONGLONG)(MAXLONGLONG / VolumeInfo->BytesPerCluster))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    MftOffset = VolumeInfo->MftLcn * VolumeInfo->BytesPerCluster;
    Status = NtfslxReadDisk(StorageDevice,
                            (LONGLONG)MftOffset,
                            VolumeInfo->BytesPerFileRecord,
                            VolumeInfo->BytesPerSector,
                            (PUCHAR)Record,
                            TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return NtfslxPostReadMstFixup(&Record->Ntfs, VolumeInfo->BytesPerFileRecord);
}

static
NTSTATUS
NtfslxEnsureRunlistCapacity(
    _Inout_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Inout_ PULONG Capacity,
    _In_ ULONG RequiredCount)
{
    PNTFSLX_RUNLIST_ELEMENT NewRunlist;
    ULONG NewCapacity;

    if (RequiredCount <= *Capacity)
    {
        return STATUS_SUCCESS;
    }

    NewCapacity = *Capacity;
    while (NewCapacity < RequiredCount)
    {
        if (NewCapacity > (MAXULONG / 2))
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        NewCapacity *= 2;
    }

    NewRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                       NewCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                       NTFSLX_TAG);
    if (NewRunlist == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(NewRunlist, NewCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT));
    RtlCopyMemory(NewRunlist,
                  *Runlist,
                  *Capacity * sizeof(NTFSLX_RUNLIST_ELEMENT));

    ExFreePoolWithTag(*Runlist, NTFSLX_TAG);
    *Runlist = NewRunlist;
    *Capacity = NewCapacity;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxMappingPairsDecompress(
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_ATTR_RECORD Attribute,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Out_ PULONG RunlistCount)
{
    PNTFSLX_RUNLIST_ELEMENT OutputRunlist;
    ULONG Capacity;
    ULONG Count;
    PUCHAR Buffer;
    PUCHAR AttributeEnd;
    LONGLONG Vcn;
    LONGLONG Lcn;
    LONGLONG Delta;
    ULONGLONG HighestVcn;
    ULONGLONG MaxCluster;
    UCHAR Header;
    UCHAR LengthBytes;
    UCHAR LcnBytes;
    ULONG Index;
    NTSTATUS Status;

    if (Attribute == NULL || Attribute->NonResident == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Capacity = 16;
    Count = 0;
    OutputRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                          Capacity * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                          NTFSLX_TAG);
    if (OutputRunlist == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(OutputRunlist, Capacity * sizeof(NTFSLX_RUNLIST_ELEMENT));

    Vcn = Attribute->Data.NonResident.LowestVcn;
    Lcn = 0;

    Buffer = (PUCHAR)Attribute + Attribute->Data.NonResident.MappingPairsOffset;
    AttributeEnd = (PUCHAR)Attribute + Attribute->Length;
    if (Buffer < (PUCHAR)Attribute || Buffer > AttributeEnd)
    {
        ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Vcn != 0)
    {
        OutputRunlist[Count].Vcn = 0;
        OutputRunlist[Count].Lcn = NTFSLX_LCN_RL_NOT_MAPPED;
        OutputRunlist[Count].Length = Vcn;
        Count++;
    }

    while (Buffer < AttributeEnd && *Buffer != 0)
    {
        Status = NtfslxEnsureRunlistCapacity(&OutputRunlist, &Capacity, Count + 3);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
            return Status;
        }

        Header = *Buffer++;
        LengthBytes = Header & 0x0F;
        LcnBytes = (Header >> 4) & 0x0F;
        if (LengthBytes == 0 || Buffer + LengthBytes + LcnBytes > AttributeEnd)
        {
            ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Delta = (CHAR)Buffer[LengthBytes - 1];
        for (Index = LengthBytes - 1; Index != 0; --Index)
        {
            Delta = (Delta << 8) + Buffer[Index - 1];
        }

        if (Delta < 0)
        {
            ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        OutputRunlist[Count].Vcn = Vcn;
        OutputRunlist[Count].Length = Delta;
        Vcn += Delta;

        if (LcnBytes == 0)
        {
            OutputRunlist[Count].Lcn = NTFSLX_LCN_HOLE;
        }
        else
        {
            Delta = (CHAR)Buffer[LengthBytes + LcnBytes - 1];
            for (Index = LengthBytes + LcnBytes - 1; Index > LengthBytes; --Index)
            {
                Delta = (Delta << 8) + Buffer[Index - 1];
            }

            Lcn += Delta;
            if (Lcn < -1)
            {
                ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            if (Lcn != -1 && OutputRunlist[Count].Length == 0)
            {
                ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            OutputRunlist[Count].Lcn = Lcn;
        }

        Buffer += LengthBytes + LcnBytes;
        if (OutputRunlist[Count].Length != 0)
        {
            Count++;
        }
    }

    if (Buffer >= AttributeEnd)
    {
        ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    HighestVcn = Attribute->Data.NonResident.HighestVcn;
    if (HighestVcn != 0 && Vcn - 1 != (LONGLONG)HighestVcn)
    {
        ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Attribute->Data.NonResident.LowestVcn == 0)
    {
        if (Attribute->Data.NonResident.AllocatedSize == 0)
        {
            MaxCluster = 0;
        }
        else
        {
            MaxCluster = ((Attribute->Data.NonResident.AllocatedSize +
                           VolumeInfo->BytesPerCluster - 1) /
                          VolumeInfo->BytesPerCluster) - 1;
        }

        if (HighestVcn != 0)
        {
            if (HighestVcn < MaxCluster)
            {
                OutputRunlist[Count].Vcn = Vcn;
                OutputRunlist[Count].Length = (LONGLONG)(MaxCluster - HighestVcn);
                OutputRunlist[Count].Lcn = NTFSLX_LCN_RL_NOT_MAPPED;
                Vcn += OutputRunlist[Count].Length;
                Count++;
            }
            else if (HighestVcn > MaxCluster)
            {
                ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
                return STATUS_FILE_CORRUPT_ERROR;
            }
        }

        OutputRunlist[Count].Lcn = NTFSLX_LCN_ENOENT;
    }
    else
    {
        OutputRunlist[Count].Lcn = NTFSLX_LCN_RL_NOT_MAPPED;
    }

    OutputRunlist[Count].Vcn = Vcn;
    OutputRunlist[Count].Length = 0;
    Count++;

    *Runlist = OutputRunlist;
    *RunlistCount = Count;
    return STATUS_SUCCESS;
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

NTSTATUS
NtfslxReadMappedAttributeData(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PUCHAR Buffer)
{
    ULONGLONG Remaining;
    ULONGLONG CurrentOffset;
    ULONG ClusterOffset;
    ULONG ChunkLength;
    LONGLONG Vcn;
    LONGLONG Lcn;
    NTSTATUS Status;

    Remaining = Length;
    CurrentOffset = Offset;
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
        if (Lcn == NTFSLX_LCN_HOLE)
        {
            RtlZeroMemory(Buffer, ChunkLength);
        }
        else if (Lcn >= 0)
        {
            Status = NtfslxReadDisk(StorageDevice,
                                    (LONGLONG)(Lcn * VolumeInfo->BytesPerCluster + ClusterOffset),
                                    ChunkLength,
                                    VolumeInfo->BytesPerSector,
                                    Buffer,
                                    TRUE);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
        }
        else
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Buffer += ChunkLength;
        CurrentOffset += ChunkLength;
        Remaining -= ChunkLength;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxFindAttribute(
    _In_ PNTFSLX_MFT_RECORD FileRecord,
    _In_ ULONG AttributeType,
    _In_reads_opt_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _Out_ PNTFSLX_ATTR_RECORD *Attribute)
{
    PUCHAR RecordBase;
    ULONG Offset;
    PNTFSLX_ATTR_RECORD CurrentAttribute;

    RecordBase = (PUCHAR)FileRecord;
    Offset = FileRecord->AttributesOffset;
    while (Offset + sizeof(ULONG) * 2 <= FileRecord->BytesInUse)
    {
        CurrentAttribute = (PNTFSLX_ATTR_RECORD)(RecordBase + Offset);
        if (CurrentAttribute->Type == NTFSLX_ATTRIBUTE_END)
        {
            break;
        }

        if (CurrentAttribute->Length == 0 ||
            Offset + CurrentAttribute->Length > FileRecord->BytesInUse)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (CurrentAttribute->Type == AttributeType &&
            CurrentAttribute->NameLength == NameLength)
        {
            if (NameLength == 0)
            {
                *Attribute = CurrentAttribute;
                return STATUS_SUCCESS;
            }

            if (CurrentAttribute->NameOffset + NameLength * sizeof(WCHAR) <= CurrentAttribute->Length &&
                RtlCompareMemory((PUCHAR)CurrentAttribute + CurrentAttribute->NameOffset,
                                 Name,
                                 NameLength * sizeof(WCHAR)) == NameLength * sizeof(WCHAR))
            {
                *Attribute = CurrentAttribute;
                return STATUS_SUCCESS;
            }
        }

        Offset += CurrentAttribute->Length;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS
NtfslxReadMftRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG RecordNumber,
    _Out_writes_bytes_(VolumeInfo->BytesPerFileRecord) PNTFSLX_MFT_RECORD Record)
{
    NTSTATUS Status;

    if (RecordNumber == NTFSLX_FILE_MFT || MftRunlist == NULL)
    {
        Status = NtfslxReadPrimaryMftRecordDirect(StorageDevice, VolumeInfo, Record);
    }
    else
    {
        Status = NtfslxReadMappedAttributeData(StorageDevice,
                                               VolumeInfo,
                                               MftRunlist,
                                               RecordNumber * VolumeInfo->BytesPerFileRecord,
                                               VolumeInfo->BytesPerFileRecord,
                                               (PUCHAR)Record);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        Status = NtfslxPostReadMstFixup(&Record->Ntfs, VolumeInfo->BytesPerFileRecord);
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Record->Ntfs.Magic != NTFSLX_RECORD_MAGIC_FILE ||
        Record->BytesInUse == 0 ||
        Record->BytesInUse > VolumeInfo->BytesPerFileRecord ||
        Record->BytesAllocated > VolumeInfo->BytesPerFileRecord ||
        Record->AttributesOffset >= Record->BytesInUse)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxBuildMftRunlist(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Out_ PULONG RunlistCount)
{
    PNTFSLX_MFT_RECORD MftRecord;
    PNTFSLX_ATTR_RECORD DataAttribute;
    NTSTATUS Status;

    MftRecord = ExAllocatePoolWithTag(NonPagedPool,
                                      VolumeInfo->BytesPerFileRecord,
                                      NTFSLX_TAG);
    if (MftRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(StorageDevice,
                                 VolumeInfo,
                                 NULL,
                                 NTFSLX_FILE_MFT,
                                 MftRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxFindAttribute(MftRecord,
                                 NTFSLX_ATTRIBUTE_DATA,
                                 NULL,
                                 0,
                                 &DataAttribute);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    if (DataAttribute->NonResident == 0)
    {
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = NtfslxMappingPairsDecompress(VolumeInfo,
                                          DataAttribute,
                                          Runlist,
                                          RunlistCount);
    ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
    return Status;
}

/*
 * Set or clear NTFSLX_VOLUME_IS_DIRTY in the on-disk $Volume /
 * $VOLUME_INFORMATION attribute and in the cached VolumeInfo.
 *
 * The dirty flag is chkdsk's only signal that a volume was in a
 * half-consistent state the last time software touched it. Windows
 * uses it to decide whether to run chkdsk automatically at boot, and
 * the NT loader uses it to decide whether it trusts the volume at
 * all. If we modify the filesystem without toggling this flag we
 * teach Windows that our intermediate state is normal — i.e. we
 * defeat the recovery safety net entirely.
 *
 * This helper writes through the primary $MFT; the caller is
 * expected to have DeviceExtension->MftRunlist set up already. On
 * success VolumeInfo->Flags is updated to match the on-disk state.
 */
NTSTATUS
NtfslxSetVolumeDirtyFlag(
    _In_ PDEVICE_OBJECT StorageDevice,
    _Inout_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ BOOLEAN Dirty)
{
    PNTFSLX_MFT_RECORD VolumeRecord;
    PNTFSLX_ATTR_RECORD Attribute;
    PNTFSLX_VOLUME_INFORMATION_ATTRIBUTE VolInfoAttr;
    USHORT NewFlags;
    NTSTATUS Status;

    if (StorageDevice == NULL || VolumeInfo == NULL || MftRunlist == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    VolumeRecord = ExAllocatePoolWithTag(NonPagedPool,
                                         VolumeInfo->BytesPerFileRecord,
                                         NTFSLX_TAG);
    if (VolumeRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(StorageDevice,
                                 VolumeInfo,
                                 MftRunlist,
                                 NTFSLX_FILE_VOLUME,
                                 VolumeRecord);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: SetVolumeDirty: read $Volume failed 0x%08lx\n", Status);
        ExFreePoolWithTag(VolumeRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxFindAttribute(VolumeRecord,
                                 NTFSLX_ATTRIBUTE_VOLUME_INFORMATION,
                                 NULL, 0, &Attribute);
    if (!NT_SUCCESS(Status) ||
        Attribute->NonResident != 0 ||
        Attribute->Data.Resident.ValueLength < sizeof(NTFSLX_VOLUME_INFORMATION_ATTRIBUTE))
    {
        NTFSDBG("ntfslx: SetVolumeDirty: $VOLUME_INFORMATION missing / malformed 0x%08lx\n",
                 Status);
        ExFreePoolWithTag(VolumeRecord, NTFSLX_TAG);
        return NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
    }

    VolInfoAttr = (PNTFSLX_VOLUME_INFORMATION_ATTRIBUTE)
        ((PUCHAR)Attribute + Attribute->Data.Resident.ValueOffset);

    NewFlags = VolInfoAttr->Flags;
    if (Dirty)
    {
        NewFlags |= NTFSLX_VOLUME_IS_DIRTY;
    }
    else
    {
        NewFlags &= (USHORT)~NTFSLX_VOLUME_IS_DIRTY;
    }

    if (NewFlags == VolInfoAttr->Flags)
    {
        /* Already in the requested state. */
        VolumeInfo->Flags = NewFlags;
        ExFreePoolWithTag(VolumeRecord, NTFSLX_TAG);
        return STATUS_SUCCESS;
    }

    VolInfoAttr->Flags = NewFlags;

    Status = NtfslxWriteMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                  NTFSLX_FILE_VOLUME, VolumeRecord);
    if (NT_SUCCESS(Status))
    {
        VolumeInfo->Flags = NewFlags;
        NTFSDBG("ntfslx: $Volume flags now 0x%04x (dirty=%u)\n",
                 NewFlags, Dirty);
    }
    else
    {
        NTFSDBG("ntfslx: SetVolumeDirty: write $Volume failed 0x%08lx\n", Status);
    }

    ExFreePoolWithTag(VolumeRecord, NTFSLX_TAG);
    return Status;
}

NTSTATUS
NtfslxLoadVolumeMetadata(
    _In_ PDEVICE_OBJECT StorageDevice,
    _Inout_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist)
{
    PNTFSLX_MFT_RECORD VolumeRecord;
    PNTFSLX_ATTR_RECORD Attribute;
    ULONG CopyLength;
    NTSTATUS Status;

    VolumeRecord = ExAllocatePoolWithTag(NonPagedPool,
                                         VolumeInfo->BytesPerFileRecord,
                                         NTFSLX_TAG);
    if (VolumeRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(StorageDevice,
                                 VolumeInfo,
                                 MftRunlist,
                                 NTFSLX_FILE_VOLUME,
                                 VolumeRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(VolumeRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxFindAttribute(VolumeRecord,
                                 NTFSLX_ATTRIBUTE_VOLUME_NAME,
                                 NULL,
                                 0,
                                 &Attribute);
    if (NT_SUCCESS(Status) && Attribute->NonResident == 0)
    {
        CopyLength = Attribute->Data.Resident.ValueLength;
        if (CopyLength > MAXIMUM_VOLUME_LABEL_LENGTH)
        {
            CopyLength = MAXIMUM_VOLUME_LABEL_LENGTH;
        }

        RtlZeroMemory(VolumeInfo->VolumeLabel, sizeof(VolumeInfo->VolumeLabel));
        RtlCopyMemory(VolumeInfo->VolumeLabel,
                      (PUCHAR)Attribute + Attribute->Data.Resident.ValueOffset,
                      CopyLength);
        VolumeInfo->VolumeLabelLength = (USHORT)CopyLength;
    }

    Status = NtfslxFindAttribute(VolumeRecord,
                                 NTFSLX_ATTRIBUTE_VOLUME_INFORMATION,
                                 NULL,
                                 0,
                                 &Attribute);
    if (NT_SUCCESS(Status) &&
        Attribute->NonResident == 0 &&
        Attribute->Data.Resident.ValueLength >= sizeof(NTFSLX_VOLUME_INFORMATION_ATTRIBUTE))
    {
        PNTFSLX_VOLUME_INFORMATION_ATTRIBUTE VolumeInformation;

        VolumeInformation = (PNTFSLX_VOLUME_INFORMATION_ATTRIBUTE)
            ((PUCHAR)Attribute + Attribute->Data.Resident.ValueOffset);
        VolumeInfo->MajorVersion = VolumeInformation->MajorVersion;
        VolumeInfo->MinorVersion = VolumeInformation->MinorVersion;
        VolumeInfo->Flags = VolumeInformation->Flags;
    }

    ExFreePoolWithTag(VolumeRecord, NTFSLX_TAG);
    return STATUS_SUCCESS;
}
