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
FAST_IO_RELEASE_FILE NtfsFastIoReleaseFileForNtCreateSection;

NTSTATUS
NtfsMountVolume(_In_ PDEVICE_OBJECT TargetDeviceObject,
                _In_ PVPB Vpb);
