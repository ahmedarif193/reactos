/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/fsrtl/faulttol.c
 * PURPOSE:         Provides Fault Tolerance support for File System Drivers
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "ntddft.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS **********************************************************/

/*++
 * @name FsRtlBalanceReads
 * @implemented NT 5.2
 *
 *     The FsRtlBalanceReads routine sends an IRP to an FTDISK Driver
 *     requesting the driver to balance read requests across a mirror set.
 *
 * @param TargetDevice
 *        A pointer to an FTDISK Device Object.
 *
 * @return The NTSTATUS error code returned by the FTDISK Driver.
 *
 * @remarks FTDISK is a Software RAID Implementation.
 *
 *--*/
static
NTSTATUS
NTAPI
FsRtlpBalanceReadsCompletion(IN PDEVICE_OBJECT DeviceObject,
                             IN PIRP Irp,
                             IN PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NTAPI
FsRtlBalanceReads(PDEVICE_OBJECT TargetDevice)
{
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;
    KIRQL OldIrql;

    /* Initialize the Local Event */
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    /* Build the special IOCTL */
    Irp = IoBuildDeviceIoControlRequest(FT_BALANCED_READ_MODE,
                                        TargetDevice,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (!Irp) return STATUS_INSUFFICIENT_RESOURCES;

    IoSetCompletionRoutine(Irp,
                           FsRtlpBalanceReadsCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    /* Send it */
    Status = IoCallDriver(TargetDevice, Irp);

    KeWaitForSingleObject(&Event,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    Status = Irp->IoStatus.Status;

    KeRaiseIrql(APC_LEVEL, &OldIrql);
    IopUnQueueIrpFromThread(Irp);
    KeLowerIrql(OldIrql);

    IoFreeIrp(Irp);

    /* Return the status */
    return Status;
}

/*++
 * @name FsRtlSyncVolumes
 * @implemented NT 5.2
 *
 *     The FsRtlSyncVolumes routine is deprecated.
 *
 * @return Always returns STATUS_SUCCESS.
 *
 * @remarks Deprecated.
 *
 *--*/
NTSTATUS
NTAPI
FsRtlSyncVolumes(ULONG Unknown0,
                 ULONG Unknown1,
                 ULONG Unknown2)
{
    /* Always return success */
    return STATUS_SUCCESS;
}
