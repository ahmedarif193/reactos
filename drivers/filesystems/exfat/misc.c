/*
 * PROJECT:     ReactOS exFAT filesystem driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     FatFs, block-device, path, and FCB support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "exfat.h"

#define NDEBUG
#include <debug.h>

PARTITION VolToPart[FF_VOLUMES] = { { 0, 0 } };

typedef struct _EXFAT_IO_CONTEXT
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
} EXFAT_IO_CONTEXT, *PEXFAT_IO_CONTEXT;

NTSTATUS
ExFatMapResult(
    FRESULT Result)
{
    switch (Result)
    {
        case FR_OK:
            return STATUS_SUCCESS;
        case FR_DISK_ERR:
            return STATUS_IO_DEVICE_ERROR;
        case FR_INT_ERR:
            return STATUS_FILE_CORRUPT_ERROR;
        case FR_NOT_READY:
            return STATUS_DEVICE_NOT_READY;
        case FR_NO_FILE:
            return STATUS_OBJECT_NAME_NOT_FOUND;
        case FR_NO_PATH:
            return STATUS_OBJECT_PATH_NOT_FOUND;
        case FR_INVALID_NAME:
        case FR_INVALID_PARAMETER:
            return STATUS_OBJECT_NAME_INVALID;
        case FR_DENIED:
            return STATUS_ACCESS_DENIED;
        case FR_EXIST:
            return STATUS_OBJECT_NAME_COLLISION;
        case FR_INVALID_OBJECT:
            return STATUS_FILE_INVALID;
        case FR_WRITE_PROTECTED:
            return STATUS_MEDIA_WRITE_PROTECTED;
        case FR_INVALID_DRIVE:
        case FR_NOT_ENABLED:
            return STATUS_VOLUME_DISMOUNTED;
        case FR_NO_FILESYSTEM:
            return STATUS_UNRECOGNIZED_VOLUME;
        case FR_TIMEOUT:
            return STATUS_IO_TIMEOUT;
        case FR_LOCKED:
            return STATUS_SHARING_VIOLATION;
        case FR_NOT_ENOUGH_CORE:
            return STATUS_INSUFFICIENT_RESOURCES;
        case FR_TOO_MANY_OPEN_FILES:
            return STATUS_TOO_MANY_OPENED_FILES;
        default:
            return STATUS_UNSUCCESSFUL;
    }
}

VOID
ExFatAcquireFatFs(
    PEXFAT_VCB Vcb)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->FatFsResource, TRUE);
}

VOID
ExFatReleaseFatFs(
    PEXFAT_VCB Vcb)
{
    ExReleaseResourceLite(&Vcb->FatFsResource);
    KeLeaveCriticalRegion();
}

VOID
ExFatInvalidateFcbClusterMap(
    PEXFAT_FCB Fcb)
{
#if FF_USE_FASTSEEK
    Fcb->FatFile.cltbl = NULL;
#endif
    if (Fcb->ClusterMap)
    {
        ExFreePoolWithTag(Fcb->ClusterMap, TAG_EXFAT_FATFS);
        Fcb->ClusterMap = NULL;
    }
}

FRESULT
ExFatEnsureFcbFile(
    PEXFAT_FCB Fcb,
    BOOLEAN WriteAccess)
{
    PCHAR Path;
    BYTE Mode;
    FRESULT Result;

    if (Fcb->FatFileOpen && (!WriteAccess || Fcb->FatFileWritable))
        return FR_OK;

    if (Fcb->FatFileOpen)
    {
        ExFatInvalidateFcbClusterMap(Fcb);
        Result = f_close(&Fcb->FatFile);
        Fcb->FatFileOpen = FALSE;
        Fcb->FatFileWritable = FALSE;
        if (Result != FR_OK)
            return Result;
    }

    Path = ExFatBuildFatPath(Fcb->Vcb, &Fcb->PathName);
    if (!Path)
        return FR_NOT_ENOUGH_CORE;

    Mode = FA_READ | FA_OPEN_EXISTING;
    if (WriteAccess)
        Mode |= FA_WRITE;
    Result = f_open(&Fcb->FatFile, Path, Mode);
    ExFreePoolWithTag(Path, TAG_EXFAT_PATH);
    if (Result == FR_OK)
    {
        Fcb->FatFileOpen = TRUE;
        Fcb->FatFileWritable = WriteAccess;
    }
    return Result;
}

#if FF_USE_FASTSEEK
static FRESULT
ExFatBuildClusterMap(
    PEXFAT_FCB Fcb)
{
    DWORD Required = 0;
    PDWORD ClusterMap;
    FRESULT Result;

    Fcb->FatFile.cltbl = &Required;
    Result = f_lseek(&Fcb->FatFile, CREATE_LINKMAP);
    Fcb->FatFile.cltbl = NULL;
    if (Result != FR_NOT_ENOUGH_CORE || Required < 2 ||
        Required > MAXULONG / sizeof(*ClusterMap))
    {
        return Result;
    }

    ClusterMap = ExAllocatePoolWithTag(NonPagedPool,
                                       Required * sizeof(*ClusterMap),
                                       TAG_EXFAT_FATFS);
    if (!ClusterMap)
        return FR_NOT_ENOUGH_CORE;

    ClusterMap[0] = Required;
    Fcb->FatFile.cltbl = ClusterMap;
    Result = f_lseek(&Fcb->FatFile, CREATE_LINKMAP);
    if (Result != FR_OK)
    {
        Fcb->FatFile.cltbl = NULL;
        ExFreePoolWithTag(ClusterMap, TAG_EXFAT_FATFS);
        return Result;
    }

    Fcb->ClusterMap = ClusterMap;
    return FR_OK;
}
#endif

BOOLEAN
ExFatFileIsContiguous(
    PEXFAT_FCB Fcb)
{
    return Fcb->Vcb->FileSystem.fs_type == FS_EXFAT &&
           Fcb->FatFile.obj.stat == 2 &&
           Fcb->FatFile.obj.sclust >= 2;
}

FRESULT
ExFatSeekFcbFile(
    PEXFAT_FCB Fcb,
    FSIZE_t Offset)
{
#if FF_USE_FASTSEEK
    FRESULT Result;

    /* Contiguous chains are computed without FAT access; a map buys nothing. */
    if (!Fcb->ClusterMap && !ExFatFileIsContiguous(Fcb) &&
        Offset < f_tell(&Fcb->FatFile) && f_size(&Fcb->FatFile))
    {
        Result = ExFatBuildClusterMap(Fcb);
        if (Result != FR_OK && Result != FR_NOT_ENOUGH_CORE)
            return Result;
    }
#endif
    return f_lseek(&Fcb->FatFile, Offset);
}

FRESULT
ExFatZeroFileRange(
    PEXFAT_FCB Fcb,
    FSIZE_t Start,
    FSIZE_t End)
{
    PVOID ZeroBuffer;
    FSIZE_t Remaining;
    UINT Chunk;
    UINT Written;
    FRESULT Result;

    if (End <= Start)
        return FR_OK;

    ZeroBuffer = ExAllocatePoolWithTag(NonPagedPool, 64 * 1024, TAG_EXFAT_IO);
    if (!ZeroBuffer)
        return FR_NOT_ENOUGH_CORE;
    RtlZeroMemory(ZeroBuffer, 64 * 1024);

    ExFatInvalidateFcbClusterMap(Fcb);
    Result = f_lseek(&Fcb->FatFile, Start);
    Remaining = End - Start;
    while (Result == FR_OK && Remaining != 0)
    {
        Chunk = (UINT)min(Remaining, (FSIZE_t)(64 * 1024));
        Written = 0;
        Result = f_write(&Fcb->FatFile, ZeroBuffer, Chunk, &Written);
        if (Result == FR_OK && Written != Chunk)
            Result = FR_DISK_ERR;
        Remaining -= Written;
    }

    ExFreePoolWithTag(ZeroBuffer, TAG_EXFAT_IO);
    return Result;
}

PVOID
ExFatGetUserBuffer(
    PIRP Irp,
    BOOLEAN PagingIo)
{
    if (Irp->MdlAddress)
    {
        return MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
                                            PagingIo ? HighPagePriority : NormalPagePriority);
    }

    if (Irp->AssociatedIrp.SystemBuffer)
        return Irp->AssociatedIrp.SystemBuffer;

    return Irp->UserBuffer;
}

NTSTATUS
ExFatLockUserBuffer(
    PIRP Irp,
    ULONG Length,
    LOCK_OPERATION Operation)
{
    if (Irp->MdlAddress || Length == 0)
        return STATUS_SUCCESS;

    IoAllocateMdl(Irp->UserBuffer, Length, FALSE, FALSE, Irp);
    if (!Irp->MdlAddress)
        return STATUS_INSUFFICIENT_RESOURCES;

    _SEH2_TRY
    {
        MmProbeAndLockPages(Irp->MdlAddress, Irp->RequestorMode, Operation);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        IoFreeMdl(Irp->MdlAddress);
        Irp->MdlAddress = NULL;
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ExFatReadWriteCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    PEXFAT_IO_CONTEXT IoContext = Context;
    PMDL Mdl;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoContext->IoStatus = Irp->IoStatus;

    if (Irp->Flags & IRP_BUFFERED_IO)
    {
        if ((Irp->Flags & IRP_INPUT_OPERATION) &&
            Irp->IoStatus.Status != STATUS_VERIFY_REQUIRED &&
            !NT_ERROR(Irp->IoStatus.Status))
        {
            RtlCopyMemory(Irp->UserBuffer,
                          Irp->AssociatedIrp.SystemBuffer,
                          Irp->IoStatus.Information);
        }

        if (Irp->Flags & IRP_DEALLOCATE_BUFFER)
            ExFreePool(Irp->AssociatedIrp.SystemBuffer);
    }

    while ((Mdl = Irp->MdlAddress) != NULL)
    {
        Irp->MdlAddress = Mdl->Next;
        MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }

    IoFreeIrp(Irp);
    KeSetEvent(&IoContext->Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
ExFatReadWriteDevice(
    PDEVICE_OBJECT DeviceObject,
    UCHAR MajorFunction,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER Offset,
    BOOLEAN OverrideVerify)
{
    EXFAT_IO_CONTEXT IoContext;
    PVOID Allocation = NULL;
    PVOID IoBuffer = Buffer;
    PIO_STACK_LOCATION Stack;
    PIRP Irp;
    ULONG AlignmentMask;
    NTSTATUS Status;

    RtlZeroMemory(&IoContext, sizeof(IoContext));
    KeInitializeEvent(&IoContext.Event, NotificationEvent, FALSE);

    AlignmentMask = DeviceObject->AlignmentRequirement;
    if (Length != 0 && ((ULONG_PTR)Buffer & AlignmentMask) != 0)
    {
        if (Length > MAXULONG - AlignmentMask)
            return STATUS_INVALID_BUFFER_SIZE;

        Allocation = ExAllocatePoolWithTag(NonPagedPool,
                                           Length + AlignmentMask,
                                           TAG_EXFAT_IO);
        if (!Allocation)
            return STATUS_INSUFFICIENT_RESOURCES;

        IoBuffer = (PVOID)(((ULONG_PTR)Allocation + AlignmentMask) &
                           ~(ULONG_PTR)AlignmentMask);
        if (MajorFunction == IRP_MJ_WRITE)
            RtlCopyMemory(IoBuffer, Buffer, Length);
    }

    /*
     * FatFs can issue block I/O from a paging fault at APC_LEVEL, where a
     * normal synchronous completion APC cannot run.
     */
    Irp = IoBuildAsynchronousFsdRequest(MajorFunction,
                                        DeviceObject,
                                        IoBuffer,
                                        Length,
                                        Offset,
                                        NULL);
    if (!Irp)
    {
        if (Allocation)
            ExFreePoolWithTag(Allocation, TAG_EXFAT_IO);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (OverrideVerify)
    {
        Stack = IoGetNextIrpStackLocation(Irp);
        Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    }

    IoSetCompletionRoutine(Irp,
                           ExFatReadWriteCompletion,
                           &IoContext,
                           TRUE,
                           TRUE,
                           TRUE);
    IoCallDriver(DeviceObject, Irp);
    KeWaitForSingleObject(&IoContext.Event, Executive, KernelMode, FALSE, NULL);

    Status = IoContext.IoStatus.Status;

    if (Allocation &&
        MajorFunction == IRP_MJ_READ &&
        NT_SUCCESS(Status) &&
        IoContext.IoStatus.Information <= Length)
    {
        RtlCopyMemory(Buffer, IoBuffer, IoContext.IoStatus.Information);
    }

    if (Allocation)
        ExFreePoolWithTag(Allocation, TAG_EXFAT_IO);

    if (NT_SUCCESS(Status) && Length != 0 && IoContext.IoStatus.Information != Length)
        Status = STATUS_DEVICE_DATA_ERROR;

    return Status;
}

NTSTATUS
ExFatFlushStorageDevice(
    PEXFAT_VCB Vcb)
{
    return ExFatReadWriteDevice(Vcb->StorageDevice,
                                IRP_MJ_FLUSH_BUFFERS,
                                NULL,
                                0,
                                NULL,
                                TRUE);
}

NTSTATUS
ExFatDeviceIoControl(
    PDEVICE_OBJECT DeviceObject,
    ULONG ControlCode,
    PVOID InputBuffer,
    ULONG InputLength,
    PVOID OutputBuffer,
    PULONG OutputLength,
    BOOLEAN OverrideVerify)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(ControlCode,
                                        DeviceObject,
                                        InputBuffer,
                                        InputLength,
                                        OutputBuffer,
                                        OutputLength ? *OutputLength : 0,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (OverrideVerify)
    {
        Stack = IoGetNextIrpStackLocation(Irp);
        Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    }

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (OutputLength)
        *OutputLength = (ULONG)IoStatus.Information;

    return Status;
}

VOID
ExFatFreeUnicodeString(
    PUNICODE_STRING String)
{
    if (String->Buffer)
        ExFreePoolWithTag(String->Buffer, TAG_EXFAT_PATH);
    RtlZeroMemory(String, sizeof(*String));
}

NTSTATUS
ExFatBuildFullPath(
    PFILE_OBJECT FileObject,
    PUNICODE_STRING FullPath)
{
    PEXFAT_FCB RelatedFcb = NULL;
    USHORT RelatedLength = 0;
    USHORT NameLength = FileObject->FileName.Length;
    USHORT SeparatorLength = 0;
    USHORT PrefixLength = 0;
    ULONG TotalLength;
    PWCHAR Destination;
    ULONG Index;

    RtlZeroMemory(FullPath, sizeof(*FullPath));

    if (FileObject->RelatedFileObject)
    {
        RelatedFcb = FileObject->RelatedFileObject->FsContext;
        if (!RelatedFcb || RelatedFcb->IsVolume)
            return STATUS_INVALID_PARAMETER;
        if (NameLength && (FileObject->FileName.Buffer[0] == L'\\' || FileObject->FileName.Buffer[0] == L'/'))
            return STATUS_INVALID_PARAMETER;
        RelatedLength = RelatedFcb->PathName.Length;
        if (NameLength && RelatedLength > sizeof(WCHAR))
            SeparatorLength = sizeof(WCHAR);
    }
    else if (!NameLength)
    {
        return STATUS_SUCCESS;
    }
    else if (FileObject->FileName.Buffer[0] != L'\\' && FileObject->FileName.Buffer[0] != L'/')
    {
        PrefixLength = sizeof(WCHAR);
    }

    TotalLength = RelatedLength + SeparatorLength + PrefixLength + NameLength;
    if (TotalLength > MAXUSHORT - sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    FullPath->Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                             TotalLength + sizeof(WCHAR),
                                             TAG_EXFAT_PATH);
    if (!FullPath->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    Destination = FullPath->Buffer;
    if (RelatedLength)
    {
        RtlCopyMemory(Destination, RelatedFcb->PathName.Buffer, RelatedLength);
        Destination += RelatedLength / sizeof(WCHAR);
    }
    if (SeparatorLength || PrefixLength)
        *Destination++ = L'\\';
    if (NameLength)
    {
        RtlCopyMemory(Destination, FileObject->FileName.Buffer, NameLength);
        Destination += NameLength / sizeof(WCHAR);
    }
    *Destination = UNICODE_NULL;

    FullPath->Length = (USHORT)TotalLength;
    FullPath->MaximumLength = (USHORT)(TotalLength + sizeof(WCHAR));
    for (Index = 0; Index < FullPath->Length / sizeof(WCHAR); ++Index)
    {
        if (FullPath->Buffer[Index] == L'/')
            FullPath->Buffer[Index] = L'\\';
    }

    while (FullPath->Length > sizeof(WCHAR) &&
           FullPath->Buffer[FullPath->Length / sizeof(WCHAR) - 1] == L'\\')
    {
        FullPath->Length -= sizeof(WCHAR);
        FullPath->Buffer[FullPath->Length / sizeof(WCHAR)] = UNICODE_NULL;
    }

    return STATUS_SUCCESS;
}

PCHAR
ExFatBuildFatPath(
    PEXFAT_VCB Vcb,
    PUNICODE_STRING PathName)
{
    NTSTATUS Status;
    ULONG Utf8Length;
    ULONG ConvertedLength;
    PCHAR Path;
    ULONG Index;

    Status = RtlUnicodeToUTF8N(NULL,
                               MAXULONG,
                               &Utf8Length,
                               PathName->Buffer,
                               PathName->Length);
    if (!NT_SUCCESS(Status) || Utf8Length > MAXULONG - 3)
        return NULL;

    Path = ExAllocatePoolWithTag(NonPagedPool, Utf8Length + 4, TAG_EXFAT_PATH);
    if (!Path)
        return NULL;

    Path[0] = '0' + Vcb->DriveNumber;
    Path[1] = ':';
    Status = RtlUnicodeToUTF8N(&Path[2],
                               Utf8Length,
                               &ConvertedLength,
                               PathName->Buffer,
                               PathName->Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Path, TAG_EXFAT_PATH);
        return NULL;
    }

    if (ConvertedLength == 0)
        Path[ConvertedLength++ + 2] = '/';
    Path[ConvertedLength + 2] = ANSI_NULL;
    for (Index = 2; Index < ConvertedLength + 2; ++Index)
    {
        if (Path[Index] == '\\')
            Path[Index] = '/';
    }

    return Path;
}

NTSTATUS
ExFatUtf8ToUnicode(
    PCSTR Source,
    PUNICODE_STRING Destination)
{
    const UCHAR* Input;
    ULONG CodePoint;
    ULONG Continuations;
    ULONG CharacterCount = 0;
    ULONG OutputIndex = 0;
    ULONG Index;
    ULONG Utf8Length = (ULONG)strlen(Source);

    RtlZeroMemory(Destination, sizeof(*Destination));
    Input = (const UCHAR*)Source;

    for (Index = 0; Index < Utf8Length; )
    {
        UCHAR Lead = Input[Index++];

        if (Lead < 0x80)
        {
            CodePoint = Lead;
            Continuations = 0;
        }
        else if ((Lead & 0xE0) == 0xC0)
        {
            CodePoint = Lead & 0x1F;
            Continuations = 1;
        }
        else if ((Lead & 0xF0) == 0xE0)
        {
            CodePoint = Lead & 0x0F;
            Continuations = 2;
        }
        else if ((Lead & 0xF8) == 0xF0)
        {
            CodePoint = Lead & 0x07;
            Continuations = 3;
        }
        else
        {
            return STATUS_ILLEGAL_CHARACTER;
        }

        if (Continuations > Utf8Length - Index)
            return STATUS_ILLEGAL_CHARACTER;
        for (ULONG Part = 0; Part < Continuations; ++Part)
        {
            UCHAR Byte = Input[Index++];
            if ((Byte & 0xC0) != 0x80)
                return STATUS_ILLEGAL_CHARACTER;
            CodePoint = (CodePoint << 6) | (Byte & 0x3F);
        }

        if ((Continuations == 1 && CodePoint < 0x80) ||
            (Continuations == 2 && CodePoint < 0x800) ||
            (Continuations == 3 && CodePoint < 0x10000) ||
            CodePoint > 0x10FFFF ||
            (CodePoint >= 0xD800 && CodePoint <= 0xDFFF))
        {
            return STATUS_ILLEGAL_CHARACTER;
        }
        CharacterCount += (CodePoint >= 0x10000) ? 2 : 1;
    }

    if (CharacterCount > (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    Destination->Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                                 (CharacterCount + 1) * sizeof(WCHAR),
                                                 TAG_EXFAT_PATH);
    if (!Destination->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Utf8Length; )
    {
        UCHAR Lead = Input[Index++];

        if (Lead < 0x80)
        {
            CodePoint = Lead;
            Continuations = 0;
        }
        else if ((Lead & 0xE0) == 0xC0)
        {
            CodePoint = Lead & 0x1F;
            Continuations = 1;
        }
        else if ((Lead & 0xF0) == 0xE0)
        {
            CodePoint = Lead & 0x0F;
            Continuations = 2;
        }
        else
        {
            CodePoint = Lead & 0x07;
            Continuations = 3;
        }
        for (ULONG Part = 0; Part < Continuations; ++Part)
            CodePoint = (CodePoint << 6) | (Input[Index++] & 0x3F);

        if (CodePoint < 0x10000)
        {
            Destination->Buffer[OutputIndex++] = (WCHAR)CodePoint;
        }
        else
        {
            CodePoint -= 0x10000;
            Destination->Buffer[OutputIndex++] = (WCHAR)(0xD800 | (CodePoint >> 10));
            Destination->Buffer[OutputIndex++] = (WCHAR)(0xDC00 | (CodePoint & 0x3FF));
        }
    }

    Destination->Length = (USHORT)(OutputIndex * sizeof(WCHAR));
    Destination->MaximumLength = (USHORT)((CharacterCount + 1) * sizeof(WCHAR));
    Destination->Buffer[OutputIndex] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

ULONG
ExFatFatAttributesToNt(
    BYTE Attributes)
{
    ULONG NtAttributes = 0;

    if (Attributes & AM_RDO)
        NtAttributes |= FILE_ATTRIBUTE_READONLY;
    if (Attributes & AM_HID)
        NtAttributes |= FILE_ATTRIBUTE_HIDDEN;
    if (Attributes & AM_SYS)
        NtAttributes |= FILE_ATTRIBUTE_SYSTEM;
    if (Attributes & AM_ARC)
        NtAttributes |= FILE_ATTRIBUTE_ARCHIVE;
    if (Attributes & AM_DIR)
        NtAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    if (!NtAttributes)
        NtAttributes = FILE_ATTRIBUTE_NORMAL;

    return NtAttributes;
}

BYTE
ExFatNtAttributesToFat(
    ULONG Attributes)
{
    BYTE FatAttributes = 0;

    if (Attributes & FILE_ATTRIBUTE_READONLY)
        FatAttributes |= AM_RDO;
    if (Attributes & FILE_ATTRIBUTE_HIDDEN)
        FatAttributes |= AM_HID;
    if (Attributes & FILE_ATTRIBUTE_SYSTEM)
        FatAttributes |= AM_SYS;
    if (Attributes & FILE_ATTRIBUTE_ARCHIVE)
        FatAttributes |= AM_ARC;
    return FatAttributes;
}

LARGE_INTEGER
ExFatFatTimeToSystemTime(
    WORD Date,
    WORD Time)
{
    TIME_FIELDS Fields;
    LARGE_INTEGER LocalTime;
    LARGE_INTEGER SystemTime;

    SystemTime.QuadPart = 0;
    if (!Date)
        return SystemTime;

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Year = (CSHORT)(1980 + ((Date >> 9) & 0x7F));
    Fields.Month = (CSHORT)((Date >> 5) & 0x0F);
    Fields.Day = (CSHORT)(Date & 0x1F);
    Fields.Hour = (CSHORT)((Time >> 11) & 0x1F);
    Fields.Minute = (CSHORT)((Time >> 5) & 0x3F);
    Fields.Second = (CSHORT)((Time & 0x1F) * 2);
    if (!RtlTimeFieldsToTime(&Fields, &LocalTime))
        return SystemTime;

    ExLocalTimeToSystemTime(&LocalTime, &SystemTime);
    return SystemTime;
}

VOID
ExFatSystemTimeToFatTime(
    PLARGE_INTEGER SystemTime,
    PWORD Date,
    PWORD Time)
{
    LARGE_INTEGER LocalTime;
    TIME_FIELDS Fields;

    ExSystemTimeToLocalTime(SystemTime, &LocalTime);
    RtlTimeToTimeFields(&LocalTime, &Fields);
    if (Fields.Year < 1980)
        Fields.Year = 1980;
    if (Fields.Year > 2107)
        Fields.Year = 2107;

    *Date = (WORD)(((Fields.Year - 1980) << 9) | (Fields.Month << 5) | Fields.Day);
    *Time = (WORD)((Fields.Hour << 11) | (Fields.Minute << 5) | (Fields.Second / 2));
}

ULONGLONG
ExFatRoundUp(
    ULONGLONG Value,
    ULONG Alignment)
{
    if (!Value || !Alignment)
        return Value;
    return ((Value - 1) / Alignment + 1) * Alignment;
}

ULONGLONG
ExFatHashPath(
    PUNICODE_STRING PathName)
{
    ULONGLONG Hash = 1469598103934665603ULL;
    ULONG Index;
    WCHAR Character;

    for (Index = 0; Index < PathName->Length / sizeof(WCHAR); ++Index)
    {
        Character = RtlUpcaseUnicodeChar(PathName->Buffer[Index]);
        Hash ^= Character;
        Hash *= 1099511628211ULL;
    }
    return Hash;
}

NTSTATUS
ExFatSetFcbPath(
    PEXFAT_FCB Fcb,
    PUNICODE_STRING PathName)
{
    PWCHAR Buffer;

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   PathName->Length + sizeof(WCHAR),
                                   TAG_EXFAT_PATH);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, PathName->Buffer, PathName->Length);
    Buffer[PathName->Length / sizeof(WCHAR)] = UNICODE_NULL;
    if (Fcb->PathName.Buffer)
        ExFreePoolWithTag(Fcb->PathName.Buffer, TAG_EXFAT_PATH);
    Fcb->PathName.Buffer = Buffer;
    Fcb->PathName.Length = PathName->Length;
    Fcb->PathName.MaximumLength = PathName->Length + sizeof(WCHAR);
    Fcb->IndexNumber = ExFatHashPath(PathName);
    return STATUS_SUCCESS;
}

VOID
ExFatUpdateFcbFromInfo(
    PEXFAT_FCB Fcb,
    FILINFO* Information)
{
    ULONG ClusterSize = Fcb->Vcb->FileSystem.csize * Fcb->Vcb->BytesPerSector;

    Fcb->IsDirectory = !!(Information->fattrib & AM_DIR);
    Fcb->FileAttributes = ExFatFatAttributesToNt(Information->fattrib);
    Fcb->Header.FileSize.QuadPart = Fcb->IsDirectory ? 0 : Information->fsize;
    Fcb->Header.ValidDataLength = Fcb->Header.FileSize;
    Fcb->Header.AllocationSize.QuadPart = Fcb->IsDirectory ? 0 : ExFatRoundUp(Information->fsize, ClusterSize);
    Fcb->CreationTime = ExFatFatTimeToSystemTime(Information->crdate, Information->crtime);
    Fcb->LastWriteTime = ExFatFatTimeToSystemTime(Information->fdate, Information->ftime);
    Fcb->LastAccessTime = Fcb->LastWriteTime;
    Fcb->ChangeTime = Fcb->LastWriteTime;
}

PEXFAT_FCB
ExFatCreateFcb(
    PEXFAT_VCB Vcb,
    PUNICODE_STRING PathName,
    FILINFO* Information,
    BOOLEAN IsVolume)
{
    PEXFAT_FCB Fcb;

    Fcb = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Fcb), TAG_EXFAT_FCB);
    if (!Fcb)
        return NULL;
    RtlZeroMemory(Fcb, sizeof(*Fcb));

    Fcb->Header.NodeTypeCode = EXFAT_FCB_SIGNATURE;
    Fcb->Header.NodeByteSize = sizeof(*Fcb);
    Fcb->Header.IsFastIoPossible = FastIoIsQuestionable;
    ExInitializeResourceLite(&Fcb->MainResource);
    ExInitializeResourceLite(&Fcb->PagingIoResource);
    Fcb->Header.Resource = &Fcb->MainResource;
    Fcb->Header.PagingIoResource = &Fcb->PagingIoResource;
    FsRtlInitializeFileLock(&Fcb->FileLock, NULL, NULL);
    Fcb->Vcb = Vcb;
    Fcb->ReferenceCount = 1;
    Fcb->IsVolume = IsVolume;

    if (!NT_SUCCESS(ExFatSetFcbPath(Fcb, PathName)))
    {
        FsRtlUninitializeFileLock(&Fcb->FileLock);
        ExDeleteResourceLite(&Fcb->PagingIoResource);
        ExDeleteResourceLite(&Fcb->MainResource);
        ExFreePoolWithTag(Fcb, TAG_EXFAT_FCB);
        return NULL;
    }

    if (IsVolume)
    {
        Fcb->Header.FileSize.QuadPart = Vcb->SectorCount * Vcb->BytesPerSector;
        Fcb->Header.ValidDataLength = Fcb->Header.FileSize;
        Fcb->Header.AllocationSize = Fcb->Header.FileSize;
    }
    else
    {
        ExFatUpdateFcbFromInfo(Fcb, Information);
    }

    InsertTailList(&Vcb->FcbListHead, &Fcb->ListEntry);
    return Fcb;
}

PEXFAT_FCB
ExFatFindFcb(
    PEXFAT_VCB Vcb,
    PUNICODE_STRING PathName)
{
    PLIST_ENTRY Entry;
    PEXFAT_FCB Fcb;

    for (Entry = Vcb->FcbListHead.Flink;
         Entry != &Vcb->FcbListHead;
         Entry = Entry->Flink)
    {
        Fcb = CONTAINING_RECORD(Entry, EXFAT_FCB, ListEntry);
        if (RtlEqualUnicodeString(&Fcb->PathName, PathName, TRUE))
        {
            ExFatReferenceFcb(Fcb);
            return Fcb;
        }
    }
    return NULL;
}

VOID
ExFatReferenceFcb(
    PEXFAT_FCB Fcb)
{
    InterlockedIncrement(&Fcb->ReferenceCount);
}

VOID
ExFatDereferenceFcb(
    PEXFAT_FCB Fcb)
{
    if (InterlockedDecrement(&Fcb->ReferenceCount) != 0)
        return;

    RemoveEntryList(&Fcb->ListEntry);
    if (Fcb->FatFileOpen)
    {
        ExFatAcquireFatFs(Fcb->Vcb);
        ExFatInvalidateFcbClusterMap(Fcb);
        f_close(&Fcb->FatFile);
        Fcb->FatFileOpen = FALSE;
        Fcb->FatFileWritable = FALSE;
        ExFatReleaseFatFs(Fcb->Vcb);
    }
    else
    {
        ExFatInvalidateFcbClusterMap(Fcb);
    }
    FsRtlUninitializeFileLock(&Fcb->FileLock);
    ExDeleteResourceLite(&Fcb->PagingIoResource);
    ExDeleteResourceLite(&Fcb->MainResource);
    ExFatFreeUnicodeString(&Fcb->PathName);
    ExFreePoolWithTag(Fcb, TAG_EXFAT_FCB);
}

BOOLEAN
NTAPI
ExFatFastIoCheckIfPossible(
    PFILE_OBJECT FileObject,
    PLARGE_INTEGER FileOffset,
    ULONG Length,
    BOOLEAN Wait,
    ULONG LockKey,
    BOOLEAN CheckForReadOperation,
    PIO_STATUS_BLOCK IoStatus,
    PDEVICE_OBJECT DeviceObject)
{
    PEXFAT_FCB Fcb = FileObject->FsContext;
    PEXFAT_CCB Ccb = FileObject->FsContext2;
    LARGE_INTEGER LargeLength;

    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(IoStatus);
    UNREFERENCED_PARAMETER(DeviceObject);

    if (!Fcb || !Ccb || Ccb->CleanedUp || !Ccb->HandleOpen ||
        Fcb->IsDirectory || Fcb->IsVolume || Fcb->DeletePending ||
        FileOffset->QuadPart < 0 || Length > MAXLONGLONG - FileOffset->QuadPart)
    {
        return FALSE;
    }

    LargeLength.QuadPart = Length;
    if (CheckForReadOperation)
    {
        if (!(Ccb->DesiredAccess & (FILE_READ_DATA | FILE_EXECUTE)))
            return FALSE;
        return FsRtlFastCheckLockForRead(&Fcb->FileLock,
                                         FileOffset,
                                         &LargeLength,
                                         LockKey,
                                         FileObject,
                                         PsGetCurrentProcess());
    }

    if (Fcb->Vcb->ReadOnly ||
        !(Ccb->DesiredAccess & (FILE_WRITE_DATA | FILE_APPEND_DATA)))
    {
        return FALSE;
    }
    return FsRtlFastCheckLockForWrite(&Fcb->FileLock,
                                      FileOffset,
                                      &LargeLength,
                                      LockKey,
                                      FileObject,
                                      PsGetCurrentProcess());
}

BOOLEAN
NTAPI
ExFatAcquireForLazyWrite(
    PVOID Context,
    BOOLEAN Wait)
{
    PEXFAT_FCB Fcb = Context;

    if (!ExAcquireResourceExclusiveLite(&Fcb->MainResource, Wait))
        return FALSE;
    ASSERT(IoGetTopLevelIrp() == NULL);
    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    return TRUE;
}

VOID
NTAPI
ExFatReleaseFromLazyWrite(
    PVOID Context)
{
    PEXFAT_FCB Fcb = Context;

    ASSERT(IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    IoSetTopLevelIrp(NULL);
    ExReleaseResourceLite(&Fcb->MainResource);
}

BOOLEAN
NTAPI
ExFatAcquireForReadAhead(
    PVOID Context,
    BOOLEAN Wait)
{
    PEXFAT_FCB Fcb = Context;

    if (!ExAcquireResourceSharedLite(&Fcb->MainResource, Wait))
        return FALSE;
    ASSERT(IoGetTopLevelIrp() == NULL);
    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    return TRUE;
}

VOID
NTAPI
ExFatReleaseFromReadAhead(
    PVOID Context)
{
    PEXFAT_FCB Fcb = Context;

    ASSERT(IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    IoSetTopLevelIrp(NULL);
    ExReleaseResourceLite(&Fcb->MainResource);
}

VOID
NTAPI
ExFatAcquireFileForNtCreateSection(
    PFILE_OBJECT FileObject)
{
    PEXFAT_FCB Fcb = FileObject->FsContext;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Fcb->MainResource, TRUE);
}

VOID
NTAPI
ExFatReleaseFileForNtCreateSection(
    PFILE_OBJECT FileObject)
{
    PEXFAT_FCB Fcb = FileObject->FsContext;

    ExReleaseResourceLite(&Fcb->MainResource);
    KeLeaveCriticalRegion();
}

DSTATUS
disk_initialize(
    BYTE PhysicalDrive)
{
    PEXFAT_VCB Vcb;

    if (!ExFatGlobalData || PhysicalDrive >= FF_VOLUMES)
        return STA_NOINIT;
    Vcb = ExFatGlobalData->Volumes[PhysicalDrive];
    if (!Vcb || !Vcb->Mounted)
        return STA_NOINIT;
    return Vcb->ReadOnly ? STA_PROTECT : 0;
}

DSTATUS
disk_status(
    BYTE PhysicalDrive)
{
    return disk_initialize(PhysicalDrive);
}

VOID
ExFatInvalidateSectorCache(
    PEXFAT_VCB Vcb)
{
    ULONG Index;

    for (Index = 0; Index < Vcb->SectorCacheEntries; Index++)
        Vcb->SectorCacheTags[Index] = EXFAT_SECTOR_CACHE_EMPTY;
    Vcb->SectorCacheNext = 0;
}

VOID
ExFatInvalidateSectorCacheRange(
    PEXFAT_VCB Vcb,
    LBA_t Sector,
    UINT Count)
{
    ULONG Index;

    for (Index = 0; Index < Vcb->SectorCacheEntries; Index++)
    {
        if (Vcb->SectorCacheTags[Index] >= Sector &&
            Vcb->SectorCacheTags[Index] - Sector < Count)
        {
            Vcb->SectorCacheTags[Index] = EXFAT_SECTOR_CACHE_EMPTY;
        }
    }
}

static BOOLEAN
ExFatEnsureSectorCache(
    PEXFAT_VCB Vcb)
{
    ULONG AlignmentMask;
    ULONG Entries;
    ULONG CacheSize;
    ULONG TagsSize;
    ULONG Index;

    if (Vcb->SectorCacheBuffer)
        return TRUE;

    AlignmentMask = Vcb->StorageDevice->AlignmentRequirement;
    Entries = EXFAT_SECTOR_CACHE_SIZE / Vcb->BytesPerSector;
    if (!Entries)
        return FALSE;
    CacheSize = Entries * Vcb->BytesPerSector;
    TagsSize = Entries * sizeof(LBA_t);
    if (CacheSize > MAXULONG - AlignmentMask - TagsSize)
        return FALSE;

    Vcb->SectorCacheAllocation = ExAllocatePoolWithTag(NonPagedPool,
                                                       TagsSize + CacheSize + AlignmentMask,
                                                       TAG_EXFAT_IO);
    if (!Vcb->SectorCacheAllocation)
        return FALSE;
    Vcb->SectorCacheTags = Vcb->SectorCacheAllocation;
    for (Index = 0; Index < Entries; Index++)
        Vcb->SectorCacheTags[Index] = EXFAT_SECTOR_CACHE_EMPTY;
    Vcb->SectorCacheBuffer = (PVOID)(((ULONG_PTR)Vcb->SectorCacheAllocation + TagsSize +
                                      AlignmentMask) & ~(ULONG_PTR)AlignmentMask);
    Vcb->SectorCacheNext = 0;
    Vcb->SectorCacheEntries = Entries;
    return TRUE;
}

DRESULT
disk_read(
    BYTE PhysicalDrive,
    BYTE* Buffer,
    LBA_t Sector,
    UINT Count)
{
    PEXFAT_VCB Vcb;
    LARGE_INTEGER Offset;
    PUCHAR CacheSlot;
    ULONG Index;
    ULONG Length;

    if (!ExFatGlobalData || PhysicalDrive >= FF_VOLUMES || !Buffer || !Count)
        return RES_PARERR;
    Vcb = ExFatGlobalData->Volumes[PhysicalDrive];
    if (!Vcb || !Vcb->Mounted)
        return RES_NOTRDY;
    if (Sector >= Vcb->SectorCount || Count > Vcb->SectorCount - Sector ||
        Count > MAXULONG / Vcb->BytesPerSector)
    {
        return RES_PARERR;
    }

    if (Count == 1 && ExFatEnsureSectorCache(Vcb))
    {
        /* Cache demanded sectors; speculative runs amplify random metadata I/O. */
        for (Index = 0; Index < Vcb->SectorCacheEntries; Index++)
        {
            if (Vcb->SectorCacheTags[Index] == Sector)
            {
                RtlCopyMemory(Buffer,
                              (PUCHAR)Vcb->SectorCacheBuffer + Index * Vcb->BytesPerSector,
                              Vcb->BytesPerSector);
                return RES_OK;
            }
        }

        Index = Vcb->SectorCacheNext++ % Vcb->SectorCacheEntries;
        Vcb->SectorCacheTags[Index] = EXFAT_SECTOR_CACHE_EMPTY;
        CacheSlot = (PUCHAR)Vcb->SectorCacheBuffer + Index * Vcb->BytesPerSector;
        Offset.QuadPart = Sector * Vcb->BytesPerSector;
        if (NT_SUCCESS(ExFatReadWriteDevice(Vcb->StorageDevice,
                                            IRP_MJ_READ,
                                            CacheSlot,
                                            Vcb->BytesPerSector,
                                            &Offset,
                                            TRUE)))
        {
            Vcb->SectorCacheTags[Index] = Sector;
            RtlCopyMemory(Buffer, CacheSlot, Vcb->BytesPerSector);
            return RES_OK;
        }
        return RES_ERROR;
    }

    Offset.QuadPart = Sector * Vcb->BytesPerSector;
    Length = Count * Vcb->BytesPerSector;
    return NT_SUCCESS(ExFatReadWriteDevice(Vcb->StorageDevice,
                                           IRP_MJ_READ,
                                           Buffer,
                                           Length,
                                           &Offset,
                                           TRUE)) ? RES_OK : RES_ERROR;
}

DRESULT
disk_write(
    BYTE PhysicalDrive,
    const BYTE* Buffer,
    LBA_t Sector,
    UINT Count)
{
    PEXFAT_VCB Vcb;
    LARGE_INTEGER Offset;
    PUCHAR CacheSlot;
    ULONG Index;
    ULONG Length;

    if (!ExFatGlobalData || PhysicalDrive >= FF_VOLUMES || !Buffer || !Count)
        return RES_PARERR;
    Vcb = ExFatGlobalData->Volumes[PhysicalDrive];
    if (!Vcb || !Vcb->Mounted)
        return RES_NOTRDY;
    if (Vcb->ReadOnly)
        return RES_WRPRT;
    if (Sector >= Vcb->SectorCount || Count > Vcb->SectorCount - Sector ||
        Count > MAXULONG / Vcb->BytesPerSector)
    {
        return RES_PARERR;
    }

    Offset.QuadPart = Sector * Vcb->BytesPerSector;
    Length = Count * Vcb->BytesPerSector;
    ExFatInvalidateSectorCacheRange(Vcb, Sector, Count);
    if (!NT_SUCCESS(ExFatReadWriteDevice(Vcb->StorageDevice,
                                         IRP_MJ_WRITE,
                                         (PVOID)Buffer,
                                         Length,
                                         &Offset,
                                         TRUE)))
    {
        return RES_ERROR;
    }

    if (Count == 1 && ExFatEnsureSectorCache(Vcb))
    {
        /* Keep rewritten FAT/bitmap/directory sectors hot instead of evicting. */
        Index = Vcb->SectorCacheNext++ % Vcb->SectorCacheEntries;
        CacheSlot = (PUCHAR)Vcb->SectorCacheBuffer + Index * Vcb->BytesPerSector;
        RtlCopyMemory(CacheSlot, Buffer, Vcb->BytesPerSector);
        Vcb->SectorCacheTags[Index] = Sector;
    }
    return RES_OK;
}

DRESULT
disk_ioctl(
    BYTE PhysicalDrive,
    BYTE Command,
    void* Buffer)
{
    PEXFAT_VCB Vcb;

    if (!ExFatGlobalData || PhysicalDrive >= FF_VOLUMES)
        return RES_PARERR;
    Vcb = ExFatGlobalData->Volumes[PhysicalDrive];
    if (!Vcb || !Vcb->Mounted)
        return RES_NOTRDY;

    switch (Command)
    {
        case CTRL_SYNC:
            /*
             * FatFs raises this after every metadata update (f_sync, f_close,
             * f_unlink, f_mkdir, ...). A device cache flush here would cost a
             * full ATA FLUSH per file operation; NT filesystems only flush the
             * device on explicit IRP_MJ_FLUSH_BUFFERS and at shutdown, which
             * ExFatFlushBuffers and ExFatShutdown implement.
             */
            return RES_OK;
        case GET_SECTOR_COUNT:
            if (!Buffer)
                return RES_PARERR;
            *(LBA_t*)Buffer = Vcb->SectorCount;
            return RES_OK;
        case GET_SECTOR_SIZE:
            if (!Buffer)
                return RES_PARERR;
            *(WORD*)Buffer = (WORD)Vcb->BytesPerSector;
            return RES_OK;
        case GET_BLOCK_SIZE:
            if (!Buffer)
                return RES_PARERR;
            *(DWORD*)Buffer = 1;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

DWORD
get_fattime(VOID)
{
    LARGE_INTEGER SystemTime;
    WORD Date;
    WORD Time;

    KeQuerySystemTime(&SystemTime);
    ExFatSystemTimeToFatTime(&SystemTime, &Date, &Time);
    return ((DWORD)Date << 16) | Time;
}

void*
ff_memalloc(
    UINT Size)
{
    PEXFAT_FATFS_ALLOCATION_HEADER Header;

    if (Size > MAXUINT - sizeof(*Header))
        return NULL;

    if (Size == EXFAT_FATFS_NAME_BUFFER_SIZE)
    {
        Header = ExAllocateFromNPagedLookasideList(&ExFatGlobalData->FatFsNameBufferLookaside);
        if (Header)
            Header->Fields.FromLookaside = TRUE;
    }
    else
    {
        Header = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(*Header) + Size,
                                       TAG_EXFAT_FATFS);
        if (Header)
            Header->Fields.FromLookaside = FALSE;
    }

    if (!Header)
        return NULL;
    Header->Fields.Signature = EXFAT_FATFS_ALLOCATION_SIGNATURE;
    return Header + 1;
}

void
ff_memfree(
    void* Allocation)
{
    PEXFAT_FATFS_ALLOCATION_HEADER Header;

    if (!Allocation)
        return;

    Header = (PEXFAT_FATFS_ALLOCATION_HEADER)Allocation - 1;
    ASSERT(Header->Fields.Signature == EXFAT_FATFS_ALLOCATION_SIGNATURE);
    if (Header->Fields.FromLookaside)
        ExFreeToNPagedLookasideList(&ExFatGlobalData->FatFsNameBufferLookaside, Header);
    else
        ExFreePoolWithTag(Header, TAG_EXFAT_FATFS);
}
