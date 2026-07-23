/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Header file for the ntfs3g procs
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2024 Carl J. Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
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
#include <ntstrsafe.h>
#include <debug.h>

#include <ntfs3g_ros_km.h>
#include "include/dispatch.h"
#include "include/ctxblks.h"

#define GetUserBuffer(Irp) Irp->MdlAddress ?\
MmGetSystemAddressForMdlSafe(Irp->MdlAddress, ((Irp->Flags & IRP_PAGING_IO) ? HighPagePriority : NormalPagePriority)) :\
Irp->UserBuffer
#define GetBuffer(Irp) Irp->AssociatedIrp.SystemBuffer ? Irp->AssociatedIrp.SystemBuffer : GetUserBuffer(Irp)

#ifndef TAG_NTFS
#define TAG_NTFS 'NTFS'
#endif

#define GetDisposition(Options) ((UCHAR)((Options) >> 24))
#define GetCreateOptions(Options) ((Options) & 0x00FFFFFF)

extern PDEVICE_OBJECT NtfsDiskFileSystemDeviceObject;
extern FAST_IO_DISPATCH FastIoDispatch;

#define NTFS3G_FCB_NODE_TYPE 0x3347

FORCEINLINE NTSTATUS
NtfsCompleteRequest(_Inout_ PIRP Irp,
                    _In_ NTSTATUS Status,
                    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

#endif // _NTFSPROCS_
