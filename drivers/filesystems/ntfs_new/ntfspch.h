/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Header file for the ntfs_new procs
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2024 Carl J. Bialorucki <carl.bialorucki@reactos.org>
 */

#ifndef _NTFSPROCS_
#define _NTFSPROCS_

#include <ntifs.h>
#include <ntddscsi.h>
#include <scsi.h>
#include <ntddcdrm.h>
#include <ntdddisk.h>
#include <ntddstor.h>
#include <ntintsafe.h>
#include <pseh/pseh2.h>
#include <ntstrsafe.h>
#include <debug.h>

#define NTFS_DEBUG
#include <ntfslib_new.h>
#include <ntfs_km.h>
#include "include/dispatch.h"
#include "include/ctxblks.h"
#include "include/reg.h"

#define GetUserBuffer(Irp) Irp->MdlAddress ?\
MmGetSystemAddressForMdlSafe(Irp->MdlAddress, ((Irp->Flags & IRP_PAGING_IO) ? HighPagePriority : NormalPagePriority)) :\
Irp->UserBuffer
#define GetBuffer(Irp) Irp->AssociatedIrp.SystemBuffer ? Irp->AssociatedIrp.SystemBuffer : GetUserBuffer(Irp)

#ifndef TAG_NTFS
#define TAG_NTFS 'NTFS'
#endif

typedef enum _TYPE_OF_OPEN {
    UnopenedFileObject = 1,
    UserFileOpen,
    UserDirectoryOpen,
    UserVolumeOpen,
    VirtualVolumeFile,
    DirectoryFile,
    EaFile,
} TYPE_OF_OPEN;

extern PDEVICE_OBJECT NtfsDiskFileSystemDeviceObject;
extern PDRIVER_OBJECT NtfsDriverObject;
extern FAST_IO_DISPATCH FastIoDispatch;

extern BOOLEAN gAllowExtChar8dot3;
extern BOOLEAN gShowVersionInfo;
extern BOOLEAN gBugCheckOnCorrupt;
extern BOOLEAN gDisable8dot3NameCreation;
extern INT gDisableLastAccessUpdate;
extern BOOLEAN gDisableLfsDowngrade;
extern BOOLEAN gDisableLfsUpgrade;
extern INT gMftZoneReservation;

#endif // _NTFSPROCS_
