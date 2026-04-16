/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     File open/read/write helpers for staged ntfslx integration
 *
 * Integration note:
 * - Add public prototypes for the helpers below to ntfslx.h when dispatch
 *   starts using them.
 * - Wire file-object FsContext/FsContext2 ownership in IRP_MJ_CREATE/CLOSE.
 * - Dispatch should call NtfslxOpenFileObjectByIndex(), NtfslxReadFileObject(),
 *   NtfslxWriteFileObject(), and NtfslxCloseFileObject() once path traversal
 *   resolves a target MFT record.
 * - Attribute-list extent handling is implemented here conservatively so the
 *   main thread can later swap in a richer open-path resolver without changing
 *   the file I/O surface.
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

#define NTFSLX_FILE_CONTEXT_SIGNATURE 'xCfN'
#include <pshpack1.h>
typedef struct _NTFSLX_ATTRIBUTE_LIST_ENTRY
{
    ULONG Type;
    USHORT Length;
    UCHAR NameLength;
    UCHAR NameOffset;
    ULONGLONG LowestVcn;
    ULONGLONG MftReference;
    USHORT Instance;
    WCHAR Name[1];
} NTFSLX_ATTRIBUTE_LIST_ENTRY, *PNTFSLX_ATTRIBUTE_LIST_ENTRY;
#include <poppack.h>

static
LONGLONG
NtfslxDecodeSignedInteger(
    _In_reads_(Count) const UCHAR *Buffer,
    _In_ UCHAR Count)
{
    LONGLONG Value;
    UCHAR Index;

    Value = (signed char)Buffer[Count - 1];
    for (Index = Count - 1; Index > 0; --Index)
    {
        Value = (Value << 8) | Buffer[Index - 1];
    }

    return Value;
}

static
BOOLEAN
NtfslxAttributeNameMatches(
    _In_ PNTFSLX_ATTR_RECORD Attribute,
    _In_opt_ PCWSTR Name,
    _In_ ULONG NameLength)
{
    if (Attribute->NameLength != NameLength)
    {
        return FALSE;
    }

    if (NameLength == 0)
    {
        return TRUE;
    }

    return RtlCompareMemory((PUCHAR)Attribute + Attribute->NameOffset,
                            Name,
                            NameLength * sizeof(WCHAR)) ==
           (NameLength * sizeof(WCHAR));
}

static
BOOLEAN
NtfslxSameMftReference(
    _In_ ULONGLONG Lhs,
    _In_ ULONGLONG Rhs)
{
    return (Lhs & 0x0000FFFFFFFFFFFFULL) ==
           (Rhs & 0x0000FFFFFFFFFFFFULL);
}

static
VOID
NtfslxFreeRunlist(
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT Runlist)
{
    if (Runlist != NULL)
    {
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
    }
}

static
VOID
NtfslxFreeFileContext(
    _In_opt_ PNTFSLX_FILE_CONTEXT FileContext)
{
    if (FileContext == NULL)
    {
        return;
    }

    if (FileContext->CacheContext != NULL)
    {
        (VOID)NtfslxCacheRuntimeDestroy(FileContext->CacheContext);
        FileContext->CacheContext = NULL;
    }

    NtfslxFreeRunlist(FileContext->DataRunlist);
    if (FileContext->FileRecord != NULL)
    {
        ExFreePoolWithTag(FileContext->FileRecord, NTFSLX_TAG);
    }

    if (FileContext->FullPath.Buffer != NULL)
    {
        ExFreePoolWithTag(FileContext->FullPath.Buffer, NTFSLX_TAG);
        FileContext->FullPath.Buffer = NULL;
        FileContext->FullPath.Length = 0;
        FileContext->FullPath.MaximumLength = 0;
    }

    if (FileContext->MainResourceInitialized)
    {
        ExDeleteResourceLite(&FileContext->MainResource);
        FileContext->MainResourceInitialized = FALSE;
    }
    if (FileContext->PagingIoResourceInitialized)
    {
        ExDeleteResourceLite(&FileContext->PagingIoResource);
        FileContext->PagingIoResourceInitialized = FALSE;
    }

    ExFreePoolWithTag(FileContext, NTFSLX_TAG);
}

VOID
NtfslxCloseFileObject(
    _Inout_ PFILE_OBJECT FileObject)
{
    PNTFSLX_FILE_CONTEXT FileContext;

    if (FileObject == NULL)
    {
        return;
    }

    FileContext = (PNTFSLX_FILE_CONTEXT)FileObject->FsContext;

    /*
     * Destroy the cache-runtime context BEFORE nulling FileObject pointers,
     * because CcUninitializeCacheMap still needs FileObject->PrivateCacheMap
     * and SectionObjectPointer to be wired up to find the shared cache map.
     */
    if (FileContext != NULL && FileContext->CacheContext != NULL)
    {
        (VOID)NtfslxCacheRuntimeDestroy(FileContext->CacheContext);
        FileContext->CacheContext = NULL;
    }

    FileObject->FsContext = NULL;
    FileObject->FsContext2 = NULL;
    FileObject->SectionObjectPointer = NULL;
    FileObject->Vpb = NULL;
    NtfslxFreeFileContext(FileContext);
}

static
NTSTATUS
NtfslxValidateFileContext(
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ PNTFSLX_FILE_CONTEXT *FileContext)
{
    PNTFSLX_FILE_CONTEXT Context;

    if (FileContext != NULL)
    {
        *FileContext = NULL;
    }

    if (FileObject == NULL || FileObject->FsContext == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Context = (PNTFSLX_FILE_CONTEXT)FileObject->FsContext;
    if (Context->Signature != NTFSLX_FILE_CONTEXT_SIGNATURE)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (Context->DeviceExtension == NULL ||
        Context->DeviceExtension->Signature != NTFSLX_TAG)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (FileContext != NULL)
    {
        *FileContext = Context;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxCopyResidentAttributeValue(
    _In_ PNTFSLX_ATTR_RECORD Attribute,
    _In_ ULONGLONG ByteOffset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _Out_opt_ PULONG BytesRead)
{
    ULONG ValueLength;
    ULONG CopyLength;

    if (BytesRead != NULL)
    {
        *BytesRead = 0;
    }

    ValueLength = Attribute->Data.Resident.ValueLength;
    if (ByteOffset >= ValueLength)
    {
        return STATUS_END_OF_FILE;
    }

    CopyLength = (ULONG)min((ULONGLONG)Length, (ULONGLONG)(ValueLength - ByteOffset));
    RtlCopyMemory(Buffer,
                  (PUCHAR)Attribute + Attribute->Data.Resident.ValueOffset + ByteOffset,
                  CopyLength);

    if (BytesRead != NULL)
    {
        *BytesRead = CopyLength;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxAppendRunlist(
    _Inout_ PNTFSLX_RUNLIST_ELEMENT *DestRunlist,
    _Inout_ PULONG DestCount,
    _In_ PNTFSLX_RUNLIST_ELEMENT SrcRunlist,
    _In_ ULONG SrcCount)
{
    PNTFSLX_RUNLIST_ELEMENT NewRunlist;
    ULONG NewCount;
    ULONG CopyCount;

    if (SrcCount == 0 || SrcRunlist == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (*DestRunlist == NULL)
    {
        NewRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                           SrcCount * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                           NTFSLX_TAG);
        if (NewRunlist == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(NewRunlist,
                      SrcRunlist,
                      SrcCount * sizeof(NTFSLX_RUNLIST_ELEMENT));
        *DestRunlist = NewRunlist;
        *DestCount = SrcCount;
        return STATUS_SUCCESS;
    }

    if (SrcCount < 2 || (*DestCount < 1))
    {
        return STATUS_INVALID_PARAMETER;
    }

    CopyCount = SrcCount - 1;
    NewCount = *DestCount + CopyCount;
    NewRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                       (NewCount + 1) * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                       NTFSLX_TAG);
    if (NewRunlist == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(NewRunlist, (NewCount + 1) * sizeof(NTFSLX_RUNLIST_ELEMENT));
    RtlCopyMemory(NewRunlist,
                  *DestRunlist,
                  *DestCount * sizeof(NTFSLX_RUNLIST_ELEMENT));
    RtlCopyMemory(NewRunlist + *DestCount,
                  SrcRunlist,
                  CopyCount * sizeof(NTFSLX_RUNLIST_ELEMENT));

    ExFreePoolWithTag(*DestRunlist, NTFSLX_TAG);
    *DestRunlist = NewRunlist;
    *DestCount = NewCount;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxBuildRunlistForAttribute(
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
    UCHAR Header;
    UCHAR LengthBytes;
    UCHAR LcnBytes;
    NTSTATUS Status;

    if (Runlist != NULL)
    {
        *Runlist = NULL;
    }

    if (RunlistCount != NULL)
    {
        *RunlistCount = 0;
    }

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
        Status = STATUS_SUCCESS;
        if (((Count + 3) * sizeof(NTFSLX_RUNLIST_ELEMENT)) > (Capacity * sizeof(NTFSLX_RUNLIST_ELEMENT)))
        {
            PNTFSLX_RUNLIST_ELEMENT NewRunlist;
            ULONG NewCapacity;

            NewCapacity = Capacity * 2;
            NewRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                               NewCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                               NTFSLX_TAG);
            if (NewRunlist == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                RtlZeroMemory(NewRunlist,
                              NewCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT));
                RtlCopyMemory(NewRunlist,
                              OutputRunlist,
                              Count * sizeof(NTFSLX_RUNLIST_ELEMENT));
                ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
                OutputRunlist = NewRunlist;
                Capacity = NewCapacity;
            }
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
                return Status;
            }
        }

        Header = *Buffer++;
        LengthBytes = Header & 0x0F;
        LcnBytes = (Header >> 4) & 0x0F;
        if (LengthBytes == 0 || Buffer + LengthBytes + LcnBytes > AttributeEnd)
        {
            ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Delta = NtfslxDecodeSignedInteger(Buffer, LengthBytes);
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
            Delta = NtfslxDecodeSignedInteger(Buffer + LengthBytes, LcnBytes);
            Lcn += Delta;
            if (Lcn < -1)
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
    if (Attribute->Data.NonResident.LowestVcn == 0)
    {
        if (HighestVcn != 0 && HighestVcn + 1 > VolumeInfo->ClusterCount)
        {
            ExFreePoolWithTag(OutputRunlist, NTFSLX_TAG);
            return STATUS_FILE_CORRUPT_ERROR;
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

static
NTSTATUS
NtfslxReadRunlistData(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG ByteOffset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _Out_opt_ PULONG BytesRead)
{
    ULONGLONG Remaining;
    ULONGLONG CurrentOffset;
    LONGLONG Vcn;
    LONGLONG Lcn;
    ULONG ClusterOffset;
    ULONG ChunkLength;
    NTSTATUS Status;

    if (BytesRead != NULL)
    {
        *BytesRead = 0;
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
        else if (Lcn == NTFSLX_LCN_ENOENT)
        {
            return STATUS_END_OF_FILE;
        }
        else
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Buffer += ChunkLength;
        CurrentOffset += ChunkLength;
        Remaining -= ChunkLength;
    }

    if (BytesRead != NULL)
    {
        *BytesRead = Length;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxBuildAttributeDataRunlist(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ PNTFSLX_MFT_RECORD FileRecord,
    _In_ PNTFSLX_ATTR_RECORD Attribute,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Out_ PULONG RunlistCount)
{
    PNTFSLX_RUNLIST_ELEMENT BaseRunlist;
    PNTFSLX_RUNLIST_ELEMENT ExtentRunlist;
    PNTFSLX_ATTR_RECORD CurrentAttribute;
    PNTFSLX_MFT_RECORD RemoteRecord;
    PNTFSLX_ATTRIBUTE_LIST_ENTRY Entry;
    PUCHAR ListBase;
    ULONG ListLength;
    ULONG Offset;
    ULONG ExtentCount;
    NTSTATUS Status;

    *Runlist = NULL;
    *RunlistCount = 0;

    Status = NtfslxBuildRunlistForAttribute(&DeviceExtension->VolumeInfo,
                                            Attribute,
                                            &BaseRunlist,
                                            &ExtentCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    *Runlist = BaseRunlist;
    *RunlistCount = ExtentCount;

    Status = NtfslxFindAttribute(FileRecord,
                                 NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST,
                                 NULL,
                                 0,
                                 &CurrentAttribute);
    if (!NT_SUCCESS(Status))
    {
        return STATUS_SUCCESS;
    }

    if (CurrentAttribute->NonResident != 0)
    {
        return STATUS_SUCCESS;
    }

    ListBase = (PUCHAR)CurrentAttribute + CurrentAttribute->Data.Resident.ValueOffset;
    ListLength = CurrentAttribute->Data.Resident.ValueLength;

    for (Offset = 0; Offset + sizeof(NTFSLX_ATTRIBUTE_LIST_ENTRY) <= ListLength;)
    {
        Entry = (PNTFSLX_ATTRIBUTE_LIST_ENTRY)(ListBase + Offset);
        if (Entry->Length == 0 || Offset + Entry->Length > ListLength)
        {
            NtfslxFreeRunlist(*Runlist);
            *Runlist = NULL;
            *RunlistCount = 0;
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (!NtfslxAttributeNameMatches(Attribute, (PCWSTR)Entry->Name, Entry->NameLength) ||
            Entry->Type != Attribute->Type)
        {
            Offset += Entry->Length;
            continue;
        }

        if (NtfslxSameMftReference(Entry->MftReference,
                                   FileRecord->MftRecordNumber))
        {
            Offset += Entry->Length;
            continue;
        }

        RemoteRecord = ExAllocatePoolWithTag(NonPagedPool,
                                             DeviceExtension->VolumeInfo.BytesPerFileRecord,
                                             NTFSLX_TAG);
        if (RemoteRecord == NULL)
        {
            NtfslxFreeRunlist(*Runlist);
            *Runlist = NULL;
            *RunlistCount = 0;
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Status = NtfslxReadMftRecord(DeviceExtension->StorageDevice,
                                    &DeviceExtension->VolumeInfo,
                                    DeviceExtension->MftRunlist,
                                    MREF(Entry->MftReference),
                                    RemoteRecord);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(RemoteRecord, NTFSLX_TAG);
            NtfslxFreeRunlist(*Runlist);
            *Runlist = NULL;
            *RunlistCount = 0;
            return Status;
        }

        Status = NtfslxFindAttribute(RemoteRecord,
                                     Attribute->Type,
                                     (PCWSTR)Entry->Name,
                                     Entry->NameLength,
                                     &CurrentAttribute);
        if (NT_SUCCESS(Status) && CurrentAttribute->NonResident != 0)
        {
            Status = NtfslxBuildRunlistForAttribute(&DeviceExtension->VolumeInfo,
                                                    CurrentAttribute,
                                                    &ExtentRunlist,
                                                    &ExtentCount);
            if (NT_SUCCESS(Status))
            {
                Status = NtfslxAppendRunlist(Runlist, RunlistCount, ExtentRunlist, ExtentCount);
                NtfslxFreeRunlist(ExtentRunlist);
            }
        }

        ExFreePoolWithTag(RemoteRecord, NTFSLX_TAG);
        if (!NT_SUCCESS(Status))
        {
            NtfslxFreeRunlist(*Runlist);
            *Runlist = NULL;
            *RunlistCount = 0;
            return Status;
        }

        Offset += Entry->Length;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxOpenFileContext(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG MftIndex,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR StreamName,
    _In_ ULONG StreamNameLength,
    _Outptr_ PNTFSLX_FILE_CONTEXT *FileContext)
{
    PNTFSLX_FILE_CONTEXT Context;
    PNTFSLX_MFT_RECORD FileRecord;
    PNTFSLX_ATTR_RECORD DataAttribute;
    NTSTATUS Status;

    *FileContext = NULL;

    Context = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(NTFSLX_FILE_CONTEXT),
                                    NTFSLX_TAG);
    if (Context == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context, sizeof(*Context));

    /*
     * Initialize the FCB's MainResource / PagingIoResource. The cache manager
     * callbacks in cache_runtime.c lock their own internal resource, but
     * FsRtl's fast-I/O paths dereference FcbHeader.Resource / PagingIoResource
     * at fixed offsets — they must be valid ERESOURCEs, not NULL, if we want
     * to be able to CcInitializeCacheMap safely.
     */
    if (!NT_SUCCESS(ExInitializeResourceLite(&Context->MainResource)))
    {
        ExFreePoolWithTag(Context, NTFSLX_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Context->MainResourceInitialized = TRUE;

    if (!NT_SUCCESS(ExInitializeResourceLite(&Context->PagingIoResource)))
    {
        ExDeleteResourceLite(&Context->MainResource);
        Context->MainResourceInitialized = FALSE;
        ExFreePoolWithTag(Context, NTFSLX_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Context->PagingIoResourceInitialized = TRUE;

    Context->FcbHeader.NodeTypeCode = 0x0502; /* FSRTL_FCB_HEADER_TYPE */
    Context->FcbHeader.NodeByteSize = sizeof(NTFSLX_FILE_CONTEXT);
    Context->FcbHeader.Resource = &Context->MainResource;
    Context->FcbHeader.PagingIoResource = &Context->PagingIoResource;
    Context->FcbHeader.AllocationSize.QuadPart = 0;
    Context->FcbHeader.FileSize.QuadPart = 0;
    Context->FcbHeader.ValidDataLength.QuadPart = 0;
    Context->Signature = NTFSLX_FILE_CONTEXT_SIGNATURE;
    Context->DeviceExtension = DeviceExtension;
    Context->MftIndex = MftIndex;
    Context->ParentMftIndex = NTFSLX_FILE_ROOT;
    Context->SupportsWrite = TRUE;

    FileRecord = ExAllocatePoolWithTag(NonPagedPool,
                                      DeviceExtension->VolumeInfo.BytesPerFileRecord,
                                      NTFSLX_TAG);
    if (FileRecord == NULL)
    {
        NtfslxFreeFileContext(Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DeviceExtension->StorageDevice,
                                &DeviceExtension->VolumeInfo,
                                DeviceExtension->MftRunlist,
                                MftIndex,
                                FileRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
        NtfslxFreeFileContext(Context);
        return Status;
    }

    Context->FileRecord = FileRecord;
    Context->IsDirectory = ((FileRecord->Flags & NTFSLX_MFT_RECORD_IS_DIRECTORY) != 0);

    Status = NtfslxFindAttribute(FileRecord,
                                 AttributeType,
                                 StreamName,
                                 StreamNameLength,
                                 &DataAttribute);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        if (StreamNameLength != 0 || AttributeType != NTFSLX_ATTRIBUTE_DATA)
        {
            NtfslxFreeFileContext(Context);
            return Status;
        }

        /* Some NTFS files are directories or metadata records without $DATA. */
        *FileContext = Context;
        return STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(Status))
    {
        NtfslxFreeFileContext(Context);
        return Status;
    }

    Context->DataAttribute = DataAttribute;
    Context->ResidentData = (DataAttribute->NonResident == 0);
    Context->DataSize = Context->ResidentData
        ? DataAttribute->Data.Resident.ValueLength
        : DataAttribute->Data.NonResident.DataSize;
    Context->AllocationSize = Context->ResidentData
        ? DataAttribute->Data.Resident.ValueLength
        : DataAttribute->Data.NonResident.AllocatedSize;
    Context->SupportsWrite = TRUE;
    Context->FcbHeader.AllocationSize.QuadPart = (LONGLONG)Context->AllocationSize;
    Context->FcbHeader.FileSize.QuadPart = (LONGLONG)Context->DataSize;
    Context->FcbHeader.ValidDataLength.QuadPart = (LONGLONG)Context->DataSize;

    if (!Context->ResidentData)
    {
        Status = NtfslxBuildAttributeDataRunlist(DeviceExtension,
                                                 FileRecord,
                                                 DataAttribute,
                                                 &Context->DataRunlist,
                                                 &Context->DataRunlistCount);
        if (!NT_SUCCESS(Status))
        {
            NtfslxFreeFileContext(Context);
            return Status;
        }
    }

    *FileContext = Context;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxOpenFileObjectByIndex(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONGLONG MftIndex,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR StreamName,
    _In_ ULONG StreamNameLength)
{
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PNTFSLX_FILE_CONTEXT Context;
    NTSTATUS Status;

    if (DeviceObject == NULL || FileObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DeviceExtension = DeviceObject->DeviceExtension;
    if (DeviceExtension == NULL ||
        DeviceExtension->Signature != NTFSLX_TAG ||
        !NtfslxIsVolumeDevice(DeviceExtension))
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Status = NtfslxOpenFileContext(DeviceExtension,
                                   MftIndex,
                                   AttributeType,
                                   StreamName,
                                   StreamNameLength,
                                   &Context);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    FileObject->FsContext = Context;
    FileObject->FsContext2 = NULL;
    FileObject->Vpb = DeviceExtension->Vpb;
    /*
     * SectionObjectPointer must be non-NULL so NtCreateSection and the cache
     * manager can find the shared-cache-map and data-section fields for this
     * file. We point it at the per-FileContext SECTION_OBJECT_POINTERS, and
     * then call NtfslxCacheRuntimeAttach to initialize a CC cache map on
     * this FileObject so page faults on mapped views get serviced.
     */
    FileObject->SectionObjectPointer = &Context->SectionObjectPointers;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxEnsureCacheInitialized(
    _In_ PFILE_OBJECT FileObject)
{
    PNTFSLX_FILE_CONTEXT Context;
    CC_FILE_SIZES FileSizes;
    NTSTATUS Status;

    Status = NtfslxValidateFileContext(FileObject, &Context);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Context->IsDirectory)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (Context->CacheContext != NULL)
    {
        return STATUS_SUCCESS;
    }

    if (FileObject->SectionObjectPointer == NULL)
    {
        FileObject->SectionObjectPointer = &Context->SectionObjectPointers;
    }

    FileSizes.AllocationSize.QuadPart = (LONGLONG)Context->AllocationSize;
    FileSizes.FileSize.QuadPart = (LONGLONG)Context->DataSize;
    FileSizes.ValidDataLength.QuadPart = (LONGLONG)Context->DataSize;

    /*
     * CcInitializeCacheMap requires AllocationSize >= FileSize. For resident
     * data or small files the two may both be 0 which is fine. For paging to
     * actually work, AllocationSize must be at least PAGE_SIZE — we round up
     * so the cache manager hands out full pages even for tiny files.
     */
    if (FileSizes.AllocationSize.QuadPart < FileSizes.FileSize.QuadPart)
    {
        FileSizes.AllocationSize.QuadPart = FileSizes.FileSize.QuadPart;
    }
    if (FileSizes.AllocationSize.QuadPart < PAGE_SIZE &&
        FileSizes.FileSize.QuadPart > 0)
    {
        FileSizes.AllocationSize.QuadPart = PAGE_SIZE;
    }

    DbgPrint("ntfslx: CacheAttach mft=%I64u file=%I64d alloc=%I64d vdl=%I64d\n",
             Context->MftIndex,
             FileSizes.FileSize.QuadPart,
             FileSizes.AllocationSize.QuadPart,
             FileSizes.ValidDataLength.QuadPart);

    Status = NtfslxCacheRuntimeAttach(FileObject,
                                      &FileSizes,
                                      FALSE,
                                      PAGE_SIZE,
                                      &Context->CacheContext);
    DbgPrint("ntfslx: CacheAttach mft=%I64u result=0x%08lx cacheCtx=%p\n",
             Context->MftIndex, Status, Context->CacheContext);
    return Status;
}

NTSTATUS
NtfslxReadFileObject(
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_ BOOLEAN PagingIo,
    _Out_opt_ PULONG BytesRead)
{
    PNTFSLX_FILE_CONTEXT Context;
    LARGE_INTEGER LocalOffset;
    ULONG LocalBytesRead;
    ULONG RequestedLength;
    ULONG DataRead;
    NTSTATUS Status;

    if (BytesRead != NULL)
    {
        *BytesRead = 0;
    }

    if (Buffer == NULL || FileObject == NULL || ByteOffset == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxValidateFileContext(FileObject, &Context);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: Read: ValidateFileContext failed 0x%08lx\n", Status);
        return Status;
    }

    /*
     * We intentionally DO NOT reject reads when a cache map is present. The
     * cache manager's paging I/O (triggered by a page fault on a mapped view)
     * enters here with IRP_PAGING_IO set and no FsContext caller, and needs
     * to be satisfied from disk by the runlist path below. For normal reads
     * the caller may have gone through CcCopyRead; this path is only reached
     * when the cache punts to disk.
     */

    if (Context->IsDirectory)
    {
        DbgPrint("ntfslx: Read mft=%I64u: is directory\n", Context->MftIndex);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    LocalOffset = *ByteOffset;
    if (LocalOffset.QuadPart < 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Length == 0)
    {
        return STATUS_SUCCESS;
    }

    DbgPrint("ntfslx: Read mft=%I64u ofs=%I64u len=%lu dataSize=%I64u allocSize=%I64u resident=%u\n",
             Context->MftIndex, (ULONGLONG)LocalOffset.QuadPart, Length,
             Context->DataSize, Context->AllocationSize, Context->ResidentData);

    RequestedLength = Length;
    LocalBytesRead = 0;

    /*
     * Paging I/O must satisfy the full requested length — MM fills whole
     * pages and does not retry partial reads. Regular reads just get what
     * the file has and see STATUS_END_OF_FILE when starting past EOF.
     */
    if ((ULONGLONG)LocalOffset.QuadPart >= Context->DataSize)
    {
        if (PagingIo)
        {
            RtlZeroMemory(Buffer, RequestedLength);
            if (BytesRead != NULL)
            {
                *BytesRead = RequestedLength;
            }
            DbgPrint("ntfslx: Read mft=%I64u: paging-io past DataSize; zero-fill %lu bytes\n",
                     Context->MftIndex, RequestedLength);
            return STATUS_SUCCESS;
        }

        DbgPrint("ntfslx: Read mft=%I64u: EOF ofs=%I64u >= dataSize=%I64u\n",
                 Context->MftIndex, (ULONGLONG)LocalOffset.QuadPart,
                 Context->DataSize);
        return STATUS_END_OF_FILE;
    }

    DataRead = RequestedLength;
    if ((ULONGLONG)LocalOffset.QuadPart + DataRead > Context->DataSize)
    {
        DataRead = (ULONG)(Context->DataSize - (ULONGLONG)LocalOffset.QuadPart);
        /*
         * Non-paging readers get only the bytes that exist in the file and
         * see a short-read count. Paging readers still need the full length
         * to populate their page, so the zero-fill step at the end of this
         * function extends the result up to RequestedLength.
         */
        if (!PagingIo)
        {
            RequestedLength = DataRead;
        }
    }

    if (Context->ResidentData)
    {
        Status = NtfslxCopyResidentAttributeValue(Context->DataAttribute,
                                                  (ULONGLONG)LocalOffset.QuadPart,
                                                  DataRead,
                                                  Buffer,
                                                  &LocalBytesRead);
    }
    else
    {
        Status = NtfslxReadRunlistData(Context->DeviceExtension->StorageDevice,
                                      &Context->DeviceExtension->VolumeInfo,
                                      Context->DataRunlist,
                                      (ULONGLONG)LocalOffset.QuadPart,
                                      DataRead,
                                      Buffer,
                                      &LocalBytesRead);
    }

    if (NT_SUCCESS(Status))
    {
        /* Zero-fill anything beyond DataSize that the caller asked for. */
        if (RequestedLength > LocalBytesRead)
        {
            RtlZeroMemory((PUCHAR)Buffer + LocalBytesRead,
                          RequestedLength - LocalBytesRead);
            LocalBytesRead = RequestedLength;
        }
    }

    DbgPrint("ntfslx: Read mft=%I64u result=0x%08lx bytesRead=%lu (req=%lu)\n",
             Context->MftIndex, Status,
             NT_SUCCESS(Status) ? LocalBytesRead : 0,
             RequestedLength);

    if (NT_SUCCESS(Status) && BytesRead != NULL)
    {
        *BytesRead = LocalBytesRead;
    }

    return Status;
}

NTSTATUS
NtfslxWriteFileObject(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _Out_opt_ PULONG BytesWritten)
{
    return NtfslxExtendedWrite(FileObject, Buffer, Length, ByteOffset, BytesWritten);
}

NTSTATUS
NtfslxValidateOpenFileObject(
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ PNTFSLX_FILE_CONTEXT *FileContext)
{
    return NtfslxValidateFileContext(FileObject, FileContext);
}
