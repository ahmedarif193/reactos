/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G read-only write policy
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

NTSTATUS
NTAPI
NtfsFsdWrite(_In_ PDEVICE_OBJECT DeviceObject,
             _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return NtfsCompleteRequest(Irp, STATUS_MEDIA_WRITE_PROTECTED, 0);
}
