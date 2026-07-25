/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file-system driver dispatch declarations
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD NtfsUnload;

DRIVER_DISPATCH NtfsFsdCleanup;
DRIVER_DISPATCH NtfsFsdClose;
DRIVER_DISPATCH NtfsFsdCreate;
DRIVER_DISPATCH NtfsFsdDeviceControl;
DRIVER_DISPATCH NtfsFsdDirectoryControl;
DRIVER_DISPATCH NtfsFsdFileSystemControl;
DRIVER_DISPATCH NtfsFsdFlushBuffers;
DRIVER_DISPATCH NtfsFsdLockControl;
DRIVER_DISPATCH NtfsFsdQueryEa;
DRIVER_DISPATCH NtfsFsdQueryInformation;
DRIVER_DISPATCH NtfsFsdQueryVolumeInformation;
DRIVER_DISPATCH NtfsFsdRead;
DRIVER_DISPATCH NtfsFsdSetEa;
DRIVER_DISPATCH NtfsFsdSetInformation;
DRIVER_DISPATCH NtfsFsdSetVolumeInformation;
DRIVER_DISPATCH NtfsFsdShutdown;
DRIVER_DISPATCH NtfsFsdWrite;

FAST_IO_ACQUIRE_FILE NtfsFastIoAcquireFileForNtCreateSection;
FAST_IO_CHECK_IF_POSSIBLE NtfsFastIoCheckIfPossible;
FAST_IO_RELEASE_FILE NtfsFastIoReleaseFileForNtCreateSection;
FAST_IO_WRITE NtfsFastIoWrite;

BOOLEAN
NTAPI
NtfsAcquireForLazyWrite(_In_ PVOID Context,
                        _In_ BOOLEAN Wait);

VOID
NTAPI
NtfsReleaseFromLazyWrite(_In_ PVOID Context);

BOOLEAN
NTAPI
NtfsAcquireForReadAhead(_In_ PVOID Context,
                        _In_ BOOLEAN Wait);

VOID
NTAPI
NtfsReleaseFromReadAhead(_In_ PVOID Context);

NTSTATUS
NtfsMountVolume(_In_ PDEVICE_OBJECT TargetDeviceObject,
                _In_ PVPB Vpb);

VOID
NtfsReferenceFcb(_Inout_ PFileContextBlock Fcb);

VOID
NtfsDereferenceFcb(_Inout_ PFileContextBlock Fcb);

VOID
NtfsCleanupFileObject(_Inout_ PFILE_OBJECT FileObject,
                      _In_opt_ PEPROCESS Process);

NTSTATUS
NtfsResizeFile(_Inout_ PFILE_OBJECT FileObject,
               _Inout_ PFileContextBlock File,
               _In_ PLARGE_INTEGER NewSize,
               _In_ BOOLEAN AllocationOnly);

NTSTATUS
NtfsSetEaBuffer(
    _Inout_ PFILE_OBJECT FileObject,
    _In_ NTFS3G_ROS_FILE *CoreFile,
    _In_reads_bytes_(InputLength)
        PFILE_FULL_EA_INFORMATION Input,
    _In_ ULONG InputLength);
