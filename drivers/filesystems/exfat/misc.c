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

/*
 * Mirrors of ff.c R0.16 internals (FA_MODIFIED, the FF_FS_TINY win/wflag
 * direct-transfer coherence rules in f_read()/f_write(), and the FFOBJID.stat
 * contiguous-chain encoding). On any FatFs upgrade, re-verify each against
 * ff.c before bumping this assert.
 */
C_ASSERT(FFCONF_DEF == 80386);
#define EXFAT_FA_MODIFIED 0x40 /* ff.c FA_MODIFIED: dir entry needs f_sync() */

#define EXFAT_SECTOR_CACHE_SIZE  (128 * 1024)
#define EXFAT_SECTOR_CACHE_EMPTY ((LBA_t)~0ULL)

typedef struct _EXFAT_IO_CONTEXT
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    BOOLEAN UnlockPages;
} EXFAT_IO_CONTEXT, *PEXFAT_IO_CONTEXT;

static VOID ExFatInvalidateSectorCache(PEXFAT_VCB Vcb);
static VOID ExFatInvalidateSectorCacheRange(PEXFAT_VCB Vcb, LBA_t Sector, UINT Count);

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
ExFatCloseFcbFile(
    PEXFAT_FCB Fcb)
{
    FRESULT Result;

    if (!Fcb->FatFileOpen)
        return FR_OK;

    ExFatInvalidateFcbClusterMap(Fcb);
    Result = f_close(&Fcb->FatFile);
    Fcb->FatFileOpen = FALSE;
    Fcb->FatFileWritable = FALSE;
    return Result;
}

FRESULT
ExFatEnsureFcbFile(
    PEXFAT_FCB Fcb,
    BOOLEAN WriteAccess)
{
    BYTE Mode;
    FRESULT Result;

    if (Fcb->FatFileOpen && (!WriteAccess || Fcb->FatFileWritable))
        return FR_OK;

    Result = ExFatCloseFcbFile(Fcb);
    if (Result != FR_OK)
        return Result;

    Mode = FA_READ | FA_OPEN_EXISTING;
    if (WriteAccess)
        Mode |= FA_WRITE;
    Result = f_open(&Fcb->FatFile, Fcb->FatPath, Mode);
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
    PEXFAT_VCB Vcb = Fcb->Vcb;
    FSIZE_t Remaining;
    UINT Chunk;
    UINT Written;
    FRESULT Result;

    if (End <= Start)
        return FR_OK;

    /* Callers hold the FatFs lock, which also guards this lazy allocation. */
    if (!Vcb->ZeroBuffer)
    {
        Vcb->ZeroBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                                64 * 1024,
                                                TAG_EXFAT_IO);
        if (!Vcb->ZeroBuffer)
            return FR_NOT_ENOUGH_CORE;
        RtlZeroMemory(Vcb->ZeroBuffer, 64 * 1024);
    }

    ExFatInvalidateFcbClusterMap(Fcb);
    Result = f_lseek(&Fcb->FatFile, Start);
    Remaining = End - Start;
    while (Result == FR_OK && Remaining != 0)
    {
        Chunk = (UINT)min(Remaining, (FSIZE_t)(64 * 1024));
        Written = 0;
        Result = f_write(&Fcb->FatFile, Vcb->ZeroBuffer, Chunk, &Written);
        if (Result == FR_OK && Written != Chunk)
            Result = FR_DISK_ERR;
        Remaining -= Written;
    }

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

    while ((Mdl = Irp->MdlAddress) != NULL)
    {
        Irp->MdlAddress = Mdl->Next;
        if (IoContext->UnlockPages)
            MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }

    IoFreeIrp(Irp);
    KeSetEvent(&IoContext->Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * Submit an I/O request built around a caller-prepared MDL and wait for it.
 * Takes ownership of the MDL (and unlocks its pages if UnlockPages). The IRP
 * is hand-built and completed via event: FatFs can issue block I/O from a
 * paging fault at APC_LEVEL, where a synchronous request's completion APC
 * cannot run.
 */
static NTSTATUS
ExFatSubmitDeviceIo(
    PDEVICE_OBJECT DeviceObject,
    UCHAR MajorFunction,
    PMDL Mdl,
    BOOLEAN UnlockPages,
    ULONG Length,
    PLARGE_INTEGER Offset,
    BOOLEAN OverrideVerify)
{
    EXFAT_IO_CONTEXT IoContext;
    PIO_STACK_LOCATION Stack;
    PIRP Irp;

    KeInitializeEvent(&IoContext.Event, NotificationEvent, FALSE);
    IoContext.IoStatus.Status = STATUS_UNSUCCESSFUL;
    IoContext.IoStatus.Information = 0;
    IoContext.UnlockPages = UnlockPages;

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (!Irp)
    {
        while (Mdl)
        {
            PMDL Next = Mdl->Next;
            if (UnlockPages)
                MmUnlockPages(Mdl);
            IoFreeMdl(Mdl);
            Mdl = Next;
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->MdlAddress = Mdl;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->RequestorMode = KernelMode;
    if (MajorFunction == IRP_MJ_READ)
        Irp->Flags = IRP_READ_OPERATION;
    else if (MajorFunction == IRP_MJ_WRITE)
        Irp->Flags = IRP_WRITE_OPERATION;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = MajorFunction;
    if (MajorFunction == IRP_MJ_READ || MajorFunction == IRP_MJ_WRITE)
    {
        /* Parameters.Read and Parameters.Write share this layout. */
        Stack->Parameters.Read.Length = Length;
        Stack->Parameters.Read.ByteOffset = *Offset;
    }
    if (OverrideVerify)
        Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;

    IoSetCompletionRoutine(Irp,
                           ExFatReadWriteCompletion,
                           &IoContext,
                           TRUE,
                           TRUE,
                           TRUE);
    IoCallDriver(DeviceObject, Irp);
    KeWaitForSingleObject(&IoContext.Event, Executive, KernelMode, FALSE, NULL);

    if (NT_SUCCESS(IoContext.IoStatus.Status) && Length != 0 &&
        IoContext.IoStatus.Information != Length)
    {
        return STATUS_DEVICE_DATA_ERROR;
    }
    return IoContext.IoStatus.Status;
}

/* For buffers known to be nonpaged: no PFN-lock probe/unlock cycle. */
static NTSTATUS
ExFatPoolReadWriteDevice(
    PDEVICE_OBJECT DeviceObject,
    UCHAR MajorFunction,
    PVOID PoolBuffer,
    ULONG Length,
    PLARGE_INTEGER Offset,
    BOOLEAN OverrideVerify)
{
    PMDL Mdl;

    Mdl = IoAllocateMdl(PoolBuffer, Length, FALSE, FALSE, NULL);
    if (!Mdl)
        return STATUS_INSUFFICIENT_RESOURCES;
    MmBuildMdlForNonPagedPool(Mdl);
    return ExFatSubmitDeviceIo(DeviceObject,
                               MajorFunction,
                               Mdl,
                               FALSE,
                               Length,
                               Offset,
                               OverrideVerify);
}

/* For a sub-range of pages already locked by the caller's MDL. */
static NTSTATUS
ExFatMdlReadWriteDevice(
    PDEVICE_OBJECT DeviceObject,
    UCHAR MajorFunction,
    PMDL SourceMdl,
    ULONG MdlOffset,
    ULONG Length,
    PLARGE_INTEGER Offset)
{
    PVOID PartialVa;
    PMDL Mdl;

    PartialVa = (PUCHAR)MmGetMdlVirtualAddress(SourceMdl) + MdlOffset;
    Mdl = IoAllocateMdl(PartialVa, Length, FALSE, FALSE, NULL);
    if (!Mdl)
        return STATUS_INSUFFICIENT_RESOURCES;
    IoBuildPartialMdl(SourceMdl, Mdl, PartialVa, Length);
    return ExFatSubmitDeviceIo(DeviceObject,
                               MajorFunction,
                               Mdl,
                               FALSE,
                               Length,
                               Offset,
                               TRUE);
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
    PVOID Allocation = NULL;
    PVOID IoBuffer = Buffer;
    PMDL Mdl = NULL;
    BOOLEAN UnlockPages = FALSE;
    ULONG AlignmentMask;
    NTSTATUS Status;

    if (Length != 0)
    {
        AlignmentMask = DeviceObject->AlignmentRequirement;
        if (((ULONG_PTR)Buffer & AlignmentMask) != 0)
        {
            if (Length > MAXULONG - AlignmentMask)
                return STATUS_INVALID_BUFFER_SIZE;

            Allocation = ExAllocatePoolWithTag(NonPagedPool,
                                               Length + AlignmentMask,
                                               TAG_EXFAT_IO);
            if (!Allocation)
                return STATUS_INSUFFICIENT_RESOURCES;

            IoBuffer = ALIGN_UP_POINTER_BY(Allocation, AlignmentMask + 1);
            if (MajorFunction == IRP_MJ_WRITE)
                RtlCopyMemory(IoBuffer, Buffer, Length);
        }

        Mdl = IoAllocateMdl(IoBuffer, Length, FALSE, FALSE, NULL);
        if (!Mdl)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        if (Allocation)
        {
            MmBuildMdlForNonPagedPool(Mdl);
        }
        else
        {
            _SEH2_TRY
            {
                MmProbeAndLockPages(Mdl,
                                    KernelMode,
                                    (MajorFunction == IRP_MJ_READ) ? IoWriteAccess
                                                                   : IoReadAccess);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                IoFreeMdl(Mdl);
                Status = _SEH2_GetExceptionCode();
                _SEH2_YIELD(goto Cleanup);
            }
            _SEH2_END;
            UnlockPages = TRUE;
        }
    }

    Status = ExFatSubmitDeviceIo(DeviceObject,
                                 MajorFunction,
                                 Mdl,
                                 UnlockPages,
                                 Length,
                                 Offset,
                                 OverrideVerify);

Cleanup:
    if (Allocation)
    {
        if (MajorFunction == IRP_MJ_READ && NT_SUCCESS(Status))
            RtlCopyMemory(Buffer, IoBuffer, Length);
        ExFreePoolWithTag(Allocation, TAG_EXFAT_IO);
    }
    return Status;
}

/*
 * Transfer whole sectors of a contiguous (exFAT NoFatChain) file with a single
 * device request instead of FatFs's one-IRP-per-cluster loop. Mirrors the
 * FF_FS_TINY window coherence rules of f_read()/f_write() (see the ff.c
 * internals note at the top of this file). Runs under the volume's FatFs
 * lock. Returns FALSE to make the caller fall back to FatFs.
 */
BOOLEAN
ExFatDirectFileIo(
    PEXFAT_VCB Vcb,
    PEXFAT_FCB Fcb,
    UCHAR MajorFunction,
    PVOID Buffer,
    PMDL SourceMdl,
    ULONG MdlOffset,
    ULONGLONG ByteOffset,
    ULONG Length)
{
    FATFS* FileSystem = &Vcb->FileSystem;
    LARGE_INTEGER DeviceOffset;
    LBA_t Sector;
    LBA_t HeapEnd;
    ULONG Sectors = Length / Vcb->BytesPerSector;
    NTSTATUS Status;

    /* FatFs would reject an aborted file object; keep parity. */
    if (Fcb->FatFile.err)
        return FALSE;

    Sector = FileSystem->database +
             (LBA_t)(Fcb->FatFile.obj.sclust - 2) * FileSystem->csize +
             (LBA_t)(ByteOffset / Vcb->BytesPerSector);
    HeapEnd = FileSystem->database +
              (LBA_t)(FileSystem->n_fatent - 2) * FileSystem->csize;
    if (Sector < FileSystem->database ||
        Sector >= HeapEnd ||
        Sectors > HeapEnd - Sector ||
        Sector >= Vcb->SectorCount ||
        Sectors > Vcb->SectorCount - Sector)
    {
        return FALSE;
    }

    if (MajorFunction == IRP_MJ_WRITE)
        ExFatInvalidateSectorCacheRange(Vcb, Sector, Sectors);
    DeviceOffset.QuadPart = Sector * Vcb->BytesPerSector;
    if (SourceMdl &&
        MdlOffset <= MmGetMdlByteCount(SourceMdl) &&
        Length <= MmGetMdlByteCount(SourceMdl) - MdlOffset)
    {
        Status = ExFatMdlReadWriteDevice(Vcb->StorageDevice,
                                         MajorFunction,
                                         SourceMdl,
                                         MdlOffset,
                                         Length,
                                         &DeviceOffset);
    }
    else
    {
        Status = ExFatReadWriteDevice(Vcb->StorageDevice,
                                      MajorFunction,
                                      Buffer,
                                      Length,
                                      &DeviceOffset,
                                      TRUE);
    }
    if (!NT_SUCCESS(Status))
        return FALSE;

    if (FileSystem->winsect >= Sector && FileSystem->winsect - Sector < Sectors)
    {
        PUCHAR WindowCopy = (PUCHAR)Buffer +
                            (ULONG)(FileSystem->winsect - Sector) * Vcb->BytesPerSector;

        if (MajorFunction == IRP_MJ_READ)
        {
            /* The window holds a newer copy of one of the read sectors. */
            if (FileSystem->wflag)
                RtlCopyMemory(WindowCopy, FileSystem->win, Vcb->BytesPerSector);
        }
        else
        {
            /* The direct write superseded the window contents. */
            RtlCopyMemory(FileSystem->win, WindowCopy, Vcb->BytesPerSector);
            FileSystem->wflag = 0;
        }
    }

    if (MajorFunction == IRP_MJ_WRITE)
        Fcb->FatFile.flag |= EXFAT_FA_MODIFIED;
    return TRUE;
}

NTSTATUS
ExFatRawWriteDevice(
    PEXFAT_VCB Vcb,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER Offset)
{
    /* Raw writes bypass disk_write(); keep the LBA cache coherent here. */
    ExFatInvalidateSectorCache(Vcb);
    return ExFatReadWriteDevice(Vcb->StorageDevice,
                                IRP_MJ_WRITE,
                                Buffer,
                                Length,
                                Offset,
                                FALSE);
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
    PULONG OutputLength)
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

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;

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
ExFatBuildDrivePath(
    PEXFAT_VCB Vcb,
    CHAR Path[3])
{
    Path[0] = '0' + Vcb->DriveNumber;
    Path[1] = ':';
    Path[2] = ANSI_NULL;
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
    ULONG Utf8Length = (ULONG)strlen(Source);
    ULONG Required = 0;
    ULONG Written = 0;
    NTSTATUS Status;

    RtlZeroMemory(Destination, sizeof(*Destination));

    Status = RtlUTF8ToUnicodeN(NULL, 0, &Required, Source, Utf8Length);
    if (Status == STATUS_SOME_NOT_MAPPED)
        Status = STATUS_ILLEGAL_CHARACTER;
    if (!NT_SUCCESS(Status))
        return Status;
    if (Required > MAXUSHORT - sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    Destination->Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                                Required + sizeof(WCHAR),
                                                TAG_EXFAT_PATH);
    if (!Destination->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = RtlUTF8ToUnicodeN(Destination->Buffer,
                               Required,
                               &Written,
                               Source,
                               Utf8Length);
    if (Status == STATUS_SOME_NOT_MAPPED)
        Status = STATUS_ILLEGAL_CHARACTER;
    if (!NT_SUCCESS(Status))
    {
        ExFatFreeUnicodeString(Destination);
        return Status;
    }

    Destination->Length = (USHORT)Written;
    Destination->MaximumLength = (USHORT)(Required + sizeof(WCHAR));
    Destination->Buffer[Written / sizeof(WCHAR)] = UNICODE_NULL;
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

static NTSTATUS
ExFatSetFcbPath(
    PEXFAT_FCB Fcb,
    PUNICODE_STRING PathName)
{
    PWCHAR Buffer;
    PCHAR FatPath;

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   PathName->Length + sizeof(WCHAR),
                                   TAG_EXFAT_PATH);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    FatPath = ExFatBuildFatPath(Fcb->Vcb, PathName);
    if (!FatPath)
    {
        ExFreePoolWithTag(Buffer, TAG_EXFAT_PATH);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Buffer, PathName->Buffer, PathName->Length);
    Buffer[PathName->Length / sizeof(WCHAR)] = UNICODE_NULL;
    if (Fcb->PathName.Buffer)
        ExFreePoolWithTag(Fcb->PathName.Buffer, TAG_EXFAT_PATH);
    if (Fcb->FatPath)
        ExFreePoolWithTag(Fcb->FatPath, TAG_EXFAT_PATH);
    Fcb->PathName.Buffer = Buffer;
    Fcb->PathName.Length = PathName->Length;
    Fcb->PathName.MaximumLength = PathName->Length + sizeof(WCHAR);
    Fcb->FatPath = FatPath;
    Fcb->IndexNumber = ExFatHashPath(PathName);
    return STATUS_SUCCESS;
}

VOID
ExFatUpdateFcbFromInfo(
    PEXFAT_FCB Fcb,
    FILINFO* Information)
{
    Fcb->IsDirectory = !!(Information->fattrib & AM_DIR);
    Fcb->FileAttributes = ExFatFatAttributesToNt(Information->fattrib);
    Fcb->Header.FileSize.QuadPart = Fcb->IsDirectory ? 0 : Information->fsize;
    Fcb->Header.ValidDataLength = Fcb->Header.FileSize;
    Fcb->Header.AllocationSize.QuadPart = Fcb->IsDirectory ? 0 :
        ExFatRoundUp(Information->fsize, Fcb->Vcb->BytesPerCluster);
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
    ULONGLONG Hash = ExFatHashPath(PathName);
    PLIST_ENTRY Entry;
    PEXFAT_FCB Fcb;

    for (Entry = Vcb->FcbListHead.Flink;
         Entry != &Vcb->FcbListHead;
         Entry = Entry->Flink)
    {
        Fcb = CONTAINING_RECORD(Entry, EXFAT_FCB, ListEntry);
        if (Fcb->IndexNumber == Hash &&
            Fcb->PathName.Length == PathName->Length &&
            RtlEqualUnicodeString(&Fcb->PathName, PathName, TRUE))
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
        ExFatCloseFcbFile(Fcb);
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
    if (Fcb->FatPath)
        ExFreePoolWithTag(Fcb->FatPath, TAG_EXFAT_PATH);
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

    if (!Fcb || !Ccb || Ccb->CleanedUp ||
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

static VOID
ExFatInvalidateSectorCache(
    PEXFAT_VCB Vcb)
{
    ULONG Index;

    for (Index = 0; Index < Vcb->SectorCacheEntries; Index++)
        Vcb->SectorCacheTags[Index] = EXFAT_SECTOR_CACHE_EMPTY;
    Vcb->SectorCacheNext = 0;
}

static VOID
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
    Vcb->SectorCacheBuffer = ALIGN_UP_POINTER_BY((PUCHAR)Vcb->SectorCacheAllocation + TagsSize,
                                                 AlignmentMask + 1);
    Vcb->SectorCacheNext = 0;
    Vcb->SectorCacheEntries = Entries;
    return TRUE;
}

VOID
ExFatFreeSectorCache(
    PEXFAT_VCB Vcb)
{
    if (Vcb->SectorCacheAllocation)
        ExFreePoolWithTag(Vcb->SectorCacheAllocation, TAG_EXFAT_IO);
    Vcb->SectorCacheAllocation = NULL;
    Vcb->SectorCacheBuffer = NULL;
    Vcb->SectorCacheTags = NULL;
    Vcb->SectorCacheEntries = 0;
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
        if (NT_SUCCESS(ExFatPoolReadWriteDevice(Vcb->StorageDevice,
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

    if (Count == 1 && ExFatEnsureSectorCache(Vcb))
    {
        /*
         * Keep rewritten FAT/bitmap/directory sectors hot instead of
         * evicting; staging the data in the pool slot first also lets the
         * device write skip the page probe/lock cycle.
         */
        Index = Vcb->SectorCacheNext++ % Vcb->SectorCacheEntries;
        CacheSlot = (PUCHAR)Vcb->SectorCacheBuffer + Index * Vcb->BytesPerSector;
        Vcb->SectorCacheTags[Index] = EXFAT_SECTOR_CACHE_EMPTY;
        RtlCopyMemory(CacheSlot, Buffer, Vcb->BytesPerSector);
        if (!NT_SUCCESS(ExFatPoolReadWriteDevice(Vcb->StorageDevice,
                                                 IRP_MJ_WRITE,
                                                 CacheSlot,
                                                 Length,
                                                 &Offset,
                                                 TRUE)))
        {
            return RES_ERROR;
        }
        Vcb->SectorCacheTags[Index] = Sector;
        return RES_OK;
    }

    return NT_SUCCESS(ExFatReadWriteDevice(Vcb->StorageDevice,
                                           IRP_MJ_WRITE,
                                           (PVOID)Buffer,
                                           Length,
                                           &Offset,
                                           TRUE)) ? RES_OK : RES_ERROR;
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
