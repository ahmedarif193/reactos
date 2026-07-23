/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file-system controls
 * COPYRIGHT:   Copyright 2024 Carl J. Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

NTSTATUS
NTAPI
NtfsFsdFileSystemControl(_In_ PDEVICE_OBJECT DeviceObject,
                         _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    BOOLEAN TopLevel = IoGetTopLevelIrp() == NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    PAGED_CODE();
    FsRtlEnterFileSystem();
    if (TopLevel)
        IoSetTopLevelIrp(Irp);

    switch (IrpSp->MinorFunction) {
        case IRP_MN_MOUNT_VOLUME:
            Status = NtfsMountVolume(IrpSp->Parameters.MountVolume.DeviceObject,
                                     IrpSp->Parameters.MountVolume.Vpb);
            break;
        case IRP_MN_VERIFY_VOLUME:
            Status = STATUS_WRONG_VOLUME;
            break;
        case IRP_MN_USER_FS_REQUEST:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    if (TopLevel)
        IoSetTopLevelIrp(NULL);
    FsRtlExitFileSystem();
    return NtfsCompleteRequest(Irp, Status, 0);
}

NTSTATUS
NTAPI
NtfsFsdFlushBuffers(_In_ PDEVICE_OBJECT DeviceObject,
                    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
}
