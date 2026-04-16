/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     VPB / FILE_OBJECT association hardening helpers for ntfslx
 *
 * Integration note:
 * - Wire NtfslxValidateMountVpb() from fsctl.c after the mount VPB is chosen
 *   but before the volume device is published.
 * - Wire NtfslxValidateOpenFileObjectVpb() from dispatch.c after FsContext,
 *   FsContext2, and Vpb are assigned in IRP_MJ_CREATE.
 * - Wire NtfslxValidateCloseFileObjectVpb() from dispatch.c before FsContext /
 *   FsContext2 are cleared in IRP_MJ_CLOSE and IRP_MJ_CLEANUP.
 * - Wire NtfslxValidateDismountVpb() from any explicit unmount / teardown path
 *   after Vpb->DeviceObject has been cleared and before final VPB release.
 * - In DBG builds, prefer the NtfslxAssert* wrappers at those handoff points so
 *   a stale or non-VPB binding is traced before the I/O manager frees it.
 */

#include "ntfslx.h"

#include <debug.h>

#if DBG
static
VOID
NtfslxDebugTraceVpbState(
    _In_ const char *Reason,
    _In_ PVPB Vpb,
    _In_opt_ PDEVICE_OBJECT RealDevice,
    _In_opt_ PDEVICE_OBJECT FileSystemDeviceObject)
{
    NTFSDBG("ntfslx: %s: Vpb=%p Type=%hu Size=%hu Flags=0x%lx Ref=%ld Real=%p Fs=%p Device=%p\n",
            Reason,
            Vpb,
            Vpb != NULL ? Vpb->Type : 0,
            Vpb != NULL ? Vpb->Size : 0,
            Vpb != NULL ? Vpb->Flags : 0UL,
            Vpb != NULL ? (LONG)Vpb->ReferenceCount : 0,
            RealDevice,
            FileSystemDeviceObject,
            Vpb != NULL ? Vpb->DeviceObject : NULL);
}

static
VOID
NtfslxDebugTraceFileObjectState(
    _In_ const char *Reason,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_CCB Ccb;

    FileContext = FileObject != NULL ? (PNTFSLX_FILE_CONTEXT)FileObject->FsContext : NULL;
    Ccb = FileObject != NULL ? (PNTFSLX_CCB)FileObject->FsContext2 : NULL;

    NTFSDBG("ntfslx: %s: FileObject=%p DevObj=%p Vpb=%p FsContext=%p(%lx) FsContext2=%p(%lx) DevExt=%p DevExtVpb=%p\n",
            Reason,
            FileObject,
            FileObject != NULL ? FileObject->DeviceObject : NULL,
            FileObject != NULL ? FileObject->Vpb : NULL,
            FileContext,
            FileContext != NULL ? FileContext->Signature : 0UL,
            Ccb,
            Ccb != NULL ? Ccb->Signature : 0UL,
            DeviceExtension,
            DeviceExtension != NULL ? DeviceExtension->Vpb : NULL);
}
#else
#define NtfslxDebugTraceVpbState(...) ((void)0)
#define NtfslxDebugTraceFileObjectState(...) ((void)0)
#endif

static
BOOLEAN
NtfslxIsPlausibleVpb(
    _In_opt_ PVPB Vpb)
{
    return (Vpb != NULL) &&
           (Vpb->Type == IO_TYPE_VPB) &&
           (Vpb->Size == sizeof(VPB));
}

NTSTATUS
NTAPI
NtfslxValidateMountVpb(
    _In_ PVPB Vpb,
    _In_ PDEVICE_OBJECT RealDevice,
    _In_ PDEVICE_OBJECT FileSystemDeviceObject)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(FileSystemDeviceObject);

#if DBG
    NtfslxDebugTraceVpbState("pre-publish mount validation", Vpb, RealDevice, FileSystemDeviceObject);
#endif

    if (Vpb == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!NtfslxIsPlausibleVpb(Vpb))
    {
        NtfslxDebugTraceVpbState("invalid VPB", Vpb, RealDevice, FileSystemDeviceObject);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IoAcquireVpbSpinLock(&OldIrql);

    Status = STATUS_SUCCESS;

    if (Vpb->RealDevice != NULL && Vpb->RealDevice != RealDevice)
    {
        IoReleaseVpbSpinLock(OldIrql);
        NtfslxDebugTraceVpbState("mount validation real-device mismatch",
                                 Vpb,
                                 RealDevice,
                                 FileSystemDeviceObject);
        return STATUS_WRONG_VOLUME;
    }

    if ((Vpb->Flags & VPB_MOUNTED) != 0)
    {
        IoReleaseVpbSpinLock(OldIrql);
        NtfslxDebugTraceVpbState("mount validation already mounted",
                                 Vpb,
                                 RealDevice,
                                 FileSystemDeviceObject);
        return STATUS_DEVICE_ALREADY_ATTACHED;
    }

    if ((Vpb->DeviceObject != NULL) &&
        (Vpb->DeviceObject != FileSystemDeviceObject))
    {
        IoReleaseVpbSpinLock(OldIrql);
        NtfslxDebugTraceVpbState("mount validation stale DeviceObject",
                                 Vpb,
                                 RealDevice,
                                 FileSystemDeviceObject);
        return STATUS_DEVICE_ALREADY_ATTACHED;
    }

    IoReleaseVpbSpinLock(OldIrql);
    return Status;
}

static
NTSTATUS
NtfslxValidateFileContextBindings(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN RequireFileContext,
    _In_ BOOLEAN RequireCcb)
{
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_CCB Ccb;

    if (FileObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DeviceExtension != NULL && DeviceExtension->Signature != NTFSLX_TAG)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FileObject->Vpb == NULL)
    {
        NtfslxDebugTraceFileObjectState("missing VPB binding", FileObject, DeviceExtension);
        return STATUS_INVALID_PARAMETER;
    }

    if (!NtfslxIsPlausibleVpb(FileObject->Vpb))
    {
        NtfslxDebugTraceFileObjectState("invalid VPB binding", FileObject, DeviceExtension);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (DeviceExtension != NULL && FileObject->Vpb != DeviceExtension->Vpb)
    {
        NtfslxDebugTraceFileObjectState("VPB mismatch", FileObject, DeviceExtension);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    FileContext = (PNTFSLX_FILE_CONTEXT)FileObject->FsContext;
    if (FileContext == NULL)
    {
        if (RequireFileContext)
        {
            NtfslxDebugTraceFileObjectState("missing FsContext", FileObject, DeviceExtension);
            return STATUS_INVALID_DEVICE_REQUEST;
        }

        return STATUS_SUCCESS;
    }

    if (FileContext->Signature != NTFSLX_FILE_CONTEXT_SIGNATURE)
    {
        NtfslxDebugTraceFileObjectState("invalid file context", FileObject, DeviceExtension);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (DeviceExtension != NULL && FileContext->DeviceExtension != DeviceExtension)
    {
        NtfslxDebugTraceFileObjectState("file context device mismatch", FileObject, DeviceExtension);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Ccb = (PNTFSLX_CCB)FileObject->FsContext2;
    if (Ccb == NULL)
    {
        if (RequireCcb)
        {
            NTFSDBG("ntfslx: FileObject %p missing FsContext2\n", FileObject);
            return STATUS_INVALID_DEVICE_REQUEST;
        }

        return STATUS_SUCCESS;
    }

    if (Ccb->Signature != NTFSLX_CCB_SIGNATURE)
    {
        NtfslxDebugTraceFileObjectState("invalid CCB", FileObject, DeviceExtension);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxValidateMountedVpbState(
    _In_ PVPB Vpb,
    _In_opt_ PDEVICE_OBJECT RealDevice,
    _In_opt_ PDEVICE_OBJECT FileSystemDeviceObject,
    _In_ BOOLEAN ExpectMounted,
    _In_ BOOLEAN ExpectDetached)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Vpb == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!NtfslxIsPlausibleVpb(Vpb))
    {
        NtfslxDebugTraceVpbState("invalid VPB", Vpb, RealDevice, FileSystemDeviceObject);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IoAcquireVpbSpinLock(&OldIrql);

    Status = STATUS_SUCCESS;
    if (ExpectMounted)
    {
        if ((Vpb->Flags & VPB_MOUNTED) == 0)
        {
            NtfslxDebugTraceVpbState("expected mounted VPB is not mounted",
                                     Vpb,
                                     RealDevice,
                                     FileSystemDeviceObject);
            Status = STATUS_FILE_CORRUPT_ERROR;
        }

        if (Vpb->ReferenceCount == 0)
        {
            NtfslxDebugTraceVpbState("mounted VPB has zero references",
                                     Vpb,
                                     RealDevice,
                                     FileSystemDeviceObject);
            Status = STATUS_FILE_CORRUPT_ERROR;
        }
    }
    else if (Vpb->Flags & VPB_MOUNTED)
    {
        NtfslxDebugTraceVpbState("unexpectedly mounted VPB",
                                 Vpb,
                                 RealDevice,
                                 FileSystemDeviceObject);
        Status = STATUS_FILE_CORRUPT_ERROR;
    }

    if (RealDevice != NULL && Vpb->RealDevice != RealDevice)
    {
        NtfslxDebugTraceVpbState("real device mismatch",
                                 Vpb,
                                 RealDevice,
                                 FileSystemDeviceObject);
        Status = STATUS_FILE_CORRUPT_ERROR;
    }

    if (FileSystemDeviceObject != NULL && Vpb->DeviceObject != FileSystemDeviceObject)
    {
        NtfslxDebugTraceVpbState("filesystem device mismatch",
                                 Vpb,
                                 RealDevice,
                                 FileSystemDeviceObject);
        Status = STATUS_FILE_CORRUPT_ERROR;
    }

    if (ExpectDetached)
    {
        if (Vpb->DeviceObject != NULL)
        {
            NtfslxDebugTraceVpbState("expected detached VPB still has DeviceObject",
                                     Vpb,
                                     RealDevice,
                                     FileSystemDeviceObject);
            Status = STATUS_FILE_CORRUPT_ERROR;
        }
    }
    else if (Vpb->DeviceObject != NULL && Vpb->DeviceObject->Vpb != Vpb)
    {
        NtfslxDebugTraceVpbState("DeviceObject back-link mismatch",
                                 Vpb,
                                 RealDevice,
                                 FileSystemDeviceObject);
        Status = STATUS_FILE_CORRUPT_ERROR;
    }

    IoReleaseVpbSpinLock(OldIrql);
    return Status;
}

static
NTSTATUS
NtfslxValidateDetachedVpbState(
    _In_ PVPB Vpb,
    _In_opt_ PDEVICE_OBJECT RealDevice)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Vpb == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!NtfslxIsPlausibleVpb(Vpb))
    {
        NtfslxDebugTraceVpbState("invalid detached VPB", Vpb, RealDevice, NULL);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IoAcquireVpbSpinLock(&OldIrql);

    Status = STATUS_SUCCESS;
    if (Vpb->Flags & VPB_MOUNTED)
    {
        NtfslxDebugTraceVpbState("detached VPB still mounted", Vpb, RealDevice, NULL);
        Status = STATUS_FILE_CORRUPT_ERROR;
    }

    if (Vpb->DeviceObject != NULL)
    {
        NtfslxDebugTraceVpbState("detached VPB still has DeviceObject", Vpb, RealDevice, NULL);
        Status = STATUS_FILE_CORRUPT_ERROR;
    }

    if (RealDevice != NULL && Vpb->RealDevice != RealDevice)
    {
        NtfslxDebugTraceVpbState("detached VPB real device mismatch", Vpb, RealDevice, NULL);
        Status = STATUS_FILE_CORRUPT_ERROR;
    }

    IoReleaseVpbSpinLock(OldIrql);
    return Status;
}

NTSTATUS
NTAPI
NtfslxValidateOpenFileObjectVpb(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    return NtfslxValidateFileContextBindings(FileObject,
                                             DeviceExtension,
                                             TRUE,
                                             TRUE);
}

NTSTATUS
NTAPI
NtfslxValidateCloseFileObjectVpb(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    return NtfslxValidateFileContextBindings(FileObject,
                                             DeviceExtension,
                                             TRUE,
                                             TRUE);
}

NTSTATUS
NTAPI
NtfslxValidateDismountVpb(
    _In_ PVPB Vpb,
    _In_opt_ PDEVICE_OBJECT RealDevice)
{
    return NtfslxValidateDetachedVpbState(Vpb, RealDevice);
}

VOID
NTAPI
NtfslxAssertMountVpb(
    _In_ PVPB Vpb,
    _In_ PDEVICE_OBJECT RealDevice,
    _In_ PDEVICE_OBJECT FileSystemDeviceObject)
{
    NTSTATUS Status;

    Status = NtfslxValidateMountVpb(Vpb, RealDevice, FileSystemDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        NtfslxDebugTraceVpbState("mount assertion failed", Vpb, RealDevice, FileSystemDeviceObject);
    }
    ASSERT(NT_SUCCESS(Status));
}

VOID
NTAPI
NtfslxAssertOpenFileObjectVpb(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    Status = NtfslxValidateOpenFileObjectVpb(FileObject, DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        NtfslxDebugTraceFileObjectState("open assertion failed", FileObject, DeviceExtension);
    }
    ASSERT(NT_SUCCESS(Status));
}

VOID
NTAPI
NtfslxAssertCloseFileObjectVpb(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    Status = NtfslxValidateCloseFileObjectVpb(FileObject, DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        NtfslxDebugTraceFileObjectState("close assertion failed", FileObject, DeviceExtension);
    }
    ASSERT(NT_SUCCESS(Status));
}

VOID
NTAPI
NtfslxAssertDismountVpb(
    _In_ PVPB Vpb,
    _In_ PDEVICE_OBJECT RealDevice)
{
    NTSTATUS Status;

    Status = NtfslxValidateDismountVpb(Vpb, RealDevice);
    if (!NT_SUCCESS(Status))
    {
        NtfslxDebugTraceVpbState("dismount assertion failed", Vpb, RealDevice, NULL);
    }
    ASSERT(NT_SUCCESS(Status));
}
