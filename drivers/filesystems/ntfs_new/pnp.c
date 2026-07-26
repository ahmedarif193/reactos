/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new entry point
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2024 Carl J. Bialorucki <carl.bialorucki@reactos.org>
 */

#include "ntfspch.h"

/* GLOBALS *****************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtfsFsdPnp)
#endif

/* FUNCTIONS ****************************************************************/

_Function_class_(IRP_MJ_PNP)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdPnp(_In_ PDEVICE_OBJECT VolumeDeviceObject,
           _Inout_ PIRP Irp)
{
    /* Overview:
     * Handles PnP requests.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-pnp
     */
    UNREFERENCED_PARAMETER(VolumeDeviceObject);

    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}
