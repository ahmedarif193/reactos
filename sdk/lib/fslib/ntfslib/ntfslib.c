/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NTFS FS library
 * FILE:        lib/fslib/ntfslib/ntfslib.c
 * PURPOSE:     NTFS lib
 * PROGRAMMERS: Pierre Schweitzer
 */

#define NTOS_MODE_USER
#include <ndk/umtypes.h>
#include <ndk/iofuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <fmifs/fmifs.h>

#include <ntfsformat.h>

#define NDEBUG
#include <debug.h>

#ifndef FSCTL_ALLOW_EXTENDED_DASD_IO
#define FSCTL_ALLOW_EXTENDED_DASD_IO \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 32, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif

typedef struct _NTFS_FORMAT_CONTEXT
{
    HANDLE FileHandle;
    PFMIFSCALLBACK Callback;
    ULONG Percent;
} NTFS_FORMAT_CONTEXT, *PNTFS_FORMAT_CONTEXT;

static
NTSTATUS
NtfsFormatWrite(
    IN PVOID Context,
    IN ULONGLONG Offset,
    IN ULONG Length,
    IN const VOID* Buffer)
{
    PNTFS_FORMAT_CONTEXT FormatContext = (PNTFS_FORMAT_CONTEXT)Context;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER ByteOffset;
    NTSTATUS Status;

    ByteOffset.QuadPart = (LONGLONG)Offset;

    Status = NtWriteFile(FormatContext->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         (PVOID)Buffer,
                         Length,
                         &ByteOffset,
                         NULL);
    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(FormatContext->FileHandle, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = IoStatusBlock.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtWriteFile() failed at offset %I64u (Status 0x%08x)\n",
                Offset, Status);
        return Status;
    }

    if (IoStatusBlock.Information != Length)
    {
        DPRINT1("Short write at offset %I64u: %lu of %lu bytes\n",
                Offset, (ULONG)IoStatusBlock.Information, Length);
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

static
PVOID
NtfsFormatAllocate(
    IN PVOID Context,
    IN ULONG Length)
{
    UNREFERENCED_PARAMETER(Context);
    return RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
}

static
VOID
NtfsFormatFree(
    IN PVOID Context,
    IN PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Context);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
}

static
BOOLEAN
NtfsFormatProgress(
    IN PVOID Context,
    IN ULONG PercentComplete)
{
    PNTFS_FORMAT_CONTEXT FormatContext = (PNTFS_FORMAT_CONTEXT)Context;

    if (!FormatContext->Callback)
        return TRUE;

    FormatContext->Percent = PercentComplete;

    return FormatContext->Callback(PROGRESS,
                                   0,
                                   (PVOID)&FormatContext->Percent);
}

BOOLEAN
NTAPI
NtfsFormat(
    IN PUNICODE_STRING DriveRoot,
    IN PFMIFSCALLBACK Callback,
    IN BOOLEAN QuickFormat,
    IN BOOLEAN BackwardCompatible,
    IN MEDIA_TYPE MediaType,
    IN PUNICODE_STRING Label,
    IN ULONG ClusterSize)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    DISK_GEOMETRY DiskGeometry;
    PARTITION_INFORMATION_EX PartitionInfo;
    IO_STATUS_BLOCK IoStatusBlock;
    NTFS_FORMAT_CONTEXT Context;
    NtfsFormatParameters Parameters;
    WCHAR LabelBuffer[33];
    LARGE_INTEGER SystemTime;
    ULONGLONG PartitionLength;
    NTSTATUS Status, LockStatus;

    DPRINT("NtfsFormat(DriveRoot '%wZ')\n", DriveRoot);

    /* NTFS has no backwards-compatible on-disk variant to select. */
    UNREFERENCED_PARAMETER(BackwardCompatible);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Callback = Callback;

    InitializeObjectAttributes(&ObjectAttributes,
                               DriveRoot,
                               0,
                               NULL,
                               NULL);

    Status = NtOpenFile(&Context.FileHandle,
                        FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenFile() failed with status 0x%08x\n", Status);
        return FALSE;
    }

    Status = NtDeviceIoControlFile(Context.FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                   NULL,
                                   0,
                                   &DiskGeometry,
                                   sizeof(DiskGeometry));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IOCTL_DISK_GET_DRIVE_GEOMETRY failed with status 0x%08x\n", Status);
        NtClose(Context.FileHandle);
        return FALSE;
    }

    if (DiskGeometry.MediaType == FixedMedia)
    {
        Status = NtDeviceIoControlFile(Context.FileHandle,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &IoStatusBlock,
                                       IOCTL_DISK_GET_PARTITION_INFO_EX,
                                       NULL,
                                       0,
                                       &PartitionInfo,
                                       sizeof(PartitionInfo));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IOCTL_DISK_GET_PARTITION_INFO_EX failed with status 0x%08x\n", Status);
            NtClose(Context.FileHandle);
            return FALSE;
        }

        PartitionLength = (ULONGLONG)PartitionInfo.PartitionLength.QuadPart;
    }
    else
    {
        RtlZeroMemory(&PartitionInfo, sizeof(PartitionInfo));
        PartitionInfo.PartitionStyle = PARTITION_STYLE_MBR;

        PartitionLength = DiskGeometry.Cylinders.QuadPart *
                          (ULONGLONG)DiskGeometry.TracksPerCylinder *
                          (ULONGLONG)DiskGeometry.SectorsPerTrack *
                          (ULONGLONG)DiskGeometry.BytesPerSector;
    }

    if (DiskGeometry.BytesPerSector == 0)
    {
        DPRINT1("Device reported a zero sector size\n");
        NtClose(Context.FileHandle);
        return FALSE;
    }

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.TotalSectors = PartitionLength / DiskGeometry.BytesPerSector;
    Parameters.BytesPerSector = DiskGeometry.BytesPerSector;

    /* ClusterSize arrives in bytes; 0 lets the formatter choose. */
    if (ClusterSize != 0)
        Parameters.SectorsPerCluster = ClusterSize / DiskGeometry.BytesPerSector;

    Parameters.SectorsPerTrack = (USHORT)DiskGeometry.SectorsPerTrack;
    Parameters.NumberOfHeads = (USHORT)DiskGeometry.TracksPerCylinder;

    if (PartitionInfo.PartitionStyle == PARTITION_STYLE_MBR)
        Parameters.HiddenSectors = PartitionInfo.Mbr.HiddenSectors;

    /* MediaType is already an NT MEDIA_TYPE here: FormatVolume() converts the
     * FMIFS media flag before calling us. */
    Parameters.MediaDescriptor = (MediaType == FixedMedia) ? 0xF8 : 0xF0;

    NtQuerySystemTime(&SystemTime);
    Parameters.CurrentTime = (ULONGLONG)SystemTime.QuadPart;

    if (Label && Label->Buffer && Label->Length != 0)
    {
        ULONG LabelLength = Label->Length / sizeof(WCHAR);

        if (LabelLength > RTL_NUMBER_OF(LabelBuffer) - 1)
            LabelLength = RTL_NUMBER_OF(LabelBuffer) - 1;

        RtlCopyMemory(LabelBuffer, Label->Buffer, LabelLength * sizeof(WCHAR));
        LabelBuffer[LabelLength] = UNICODE_NULL;
        Parameters.VolumeLabel = LabelBuffer;
    }

    Parameters.QuickFormat = QuickFormat;
    Parameters.IoContext = &Context;
    Parameters.Write = NtfsFormatWrite;
    Parameters.Allocate = NtfsFormatAllocate;
    Parameters.Free = NtfsFormatFree;
    Parameters.Progress = NtfsFormatProgress;

    if (Callback != NULL)
    {
        Context.Percent = 0;
        Callback(PROGRESS, 0, (PVOID)&Context.Percent);
    }

    LockStatus = NtFsControlFile(Context.FileHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_LOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("Failed to lock volume for formatting (Status: 0x%x)\n",
                LockStatus);
        NtClose(Context.FileHandle);
        return FALSE;
    }

    /*
     * A mounted filesystem normally bounds raw I/O to the size recorded in
     * its own boot sector. That size can be smaller than the current
     * partition after delete/recreate or resize operations. Allow the
     * formatter to address the complete partition reported above.
     */
    LockStatus = NtFsControlFile(Context.FileHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_ALLOW_EXTENDED_DASD_IO,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    /* RawFS and filesystems that already expose the full device need not
     * implement this control. */
    if (!NT_SUCCESS(LockStatus) &&
        LockStatus != STATUS_INVALID_PARAMETER &&
        LockStatus != STATUS_INVALID_DEVICE_REQUEST &&
        LockStatus != STATUS_NOT_SUPPORTED)
    {
        DPRINT1("Failed to enable extended raw volume I/O (Status: 0x%x)\n",
                LockStatus);
        NtFsControlFile(Context.FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        FSCTL_UNLOCK_VOLUME,
                        NULL,
                        0,
                        NULL,
                        0);
        NtClose(Context.FileHandle);
        return FALSE;
    }

    Status = NtfsVolumeFormat(&Parameters);
    if (!NT_SUCCESS(Status))
        DPRINT1("NtfsVolumeFormat() failed with status 0x%08x\n", Status);

    /* Attempt to dismount the formatted volume */
    LockStatus = NtFsControlFile(Context.FileHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_DISMOUNT_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
        DPRINT1("Failed to umount volume (Status: 0x%x)\n", LockStatus);

    LockStatus = NtFsControlFile(Context.FileHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_UNLOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
        DPRINT1("Failed to unlock volume (Status: 0x%x)\n", LockStatus);

    NtClose(Context.FileHandle);

    DPRINT("NtfsFormat() done. Status 0x%08x\n", Status);
    return NT_SUCCESS(Status);
}

BOOLEAN
NTAPI
NtfsChkdsk(
    IN PUNICODE_STRING DriveRoot,
    IN PFMIFSCALLBACK Callback,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PVOID pUnknown1,
    IN PVOID pUnknown2,
    IN PVOID pUnknown3,
    IN PVOID pUnknown4,
    IN PULONG ExitStatus)
{
    /*
     * There is no consistency checker yet: this reports "nothing to repair"
     * without actually verifying the volume. Formatting does not depend on
     * it, but a real chkdsk is still missing.
     */
    UNIMPLEMENTED;
    *ExitStatus = (ULONG)STATUS_SUCCESS;
    return TRUE;
}
