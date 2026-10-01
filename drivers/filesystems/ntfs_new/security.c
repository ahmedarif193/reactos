/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTFS security descriptor query support
 */

#include "ntfspch.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtfsFsdQuerySecurity)
#pragma alloc_text(PAGE, NtfsFsdSetSecurity)
#endif

NTSTATUS
NtfsCheckRecordAccess(
    _In_ PNtfsFileRecord File,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK PreviouslyGrantedAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PPRIVILEGE_SET* Privileges)
{
    PUCHAR Descriptor;
    ULONG DescriptorLength = 0;
    NTSTATUS Status;

    *Privileges = NULL;
    if (!DesiredAccess)
    {
        *GrantedAccess = PreviouslyGrantedAccess;
        return STATUS_SUCCESS;
    }

    Status = NtfsFileRecordReadSecurityDescriptor(File, NULL, &DescriptorLength);
    if (Status == STATUS_NOT_FOUND)
    {
        *GrantedAccess = PreviouslyGrantedAccess | (DesiredAccess & ~MAXIMUM_ALLOWED);
        if (DesiredAccess & MAXIMUM_ALLOWED)
            *GrantedAccess |= FILE_ALL_ACCESS;
        return STATUS_SUCCESS;
    }
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return Status;
    Descriptor = ExAllocatePoolWithTag(PagedPool, DescriptorLength, TAG_NTFS);
    if (!Descriptor)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = NtfsFileRecordReadSecurityDescriptor(File, Descriptor, &DescriptorLength);
    if (NT_SUCCESS(Status))
    {
        SeAccessCheck(Descriptor,
                       SubjectContext,
                       FALSE,
                       DesiredAccess,
                       PreviouslyGrantedAccess,
                       Privileges,
                       IoGetFileObjectGenericMapping(),
                       AccessMode,
                       GrantedAccess,
                       &Status);
    }
    ExFreePoolWithTag(Descriptor, TAG_NTFS);
    return Status;
}

static NTSTATUS
NtfsQuerySecurityDescriptor(
    _In_ PFileContextBlock FileCB,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Out_writes_bytes_to_opt_(OutputLength, *ResultLength) PVOID Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG ResultLength)
{
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    PUCHAR RawDescriptor;
    ULONG RawLength = 0;
    ULONG QueryLength;
    NTSTATUS Status;

    *ResultLength = 0;

    Status = NtfsFileRecordReadSecurityDescriptor(FileCB->FileRec,
                                                  NULL,
                                                  &RawLength);
    if (Status == STATUS_NOT_FOUND)
        return STATUS_NO_SECURITY_ON_OBJECT;
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return Status;

    RawDescriptor = (PUCHAR)ExAllocatePoolWithTag(PagedPool,
                                                  RawLength,
                                                  TAG_NTFS);
    if (!RawDescriptor)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfsFileRecordReadSecurityDescriptor(FileCB->FileRec,
                                                  RawDescriptor,
                                                  &RawLength);
    if (!NT_SUCCESS(Status))
        goto Done;

    SecurityDescriptor = (PSECURITY_DESCRIPTOR)RawDescriptor;
    QueryLength = OutputLength;
    Status = SeQuerySecurityDescriptorInfo(&SecurityInformation,
                                           (PSECURITY_DESCRIPTOR)Output,
                                           &QueryLength,
                                           &SecurityDescriptor);
    *ResultLength = QueryLength;
    if (Status == STATUS_BUFFER_TOO_SMALL)
        Status = STATUS_BUFFER_OVERFLOW;

Done:
    ExFreePoolWithTag(RawDescriptor, TAG_NTFS);
    return Status;
}

_Function_class_(IRP_MJ_QUERY_SECURITY)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdQuerySecurity(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                     _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    PIO_STACK_LOCATION IrpSp;
    PFileContextBlock FileCB;
    PVolumeContextBlock VolCB;
    SECURITY_INFORMATION SecurityInformation;
    PVOID Output;
    ULONG OutputLength;
    ULONG ResultLength = 0;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    if (VolumeDeviceObject == NtfsDiskFileSystemDeviceObject ||
        !IrpSp->FileObject ||
        !IrpSp->FileObject->FsContext)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Done;
    }

    FileCB = NtfsGetFileContext(IrpSp->FileObject);
    VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    if (!FileCB->FileRec || !VolCB || !VolCB->DiskVolume)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }

    SecurityInformation =
        IrpSp->Parameters.QuerySecurity.SecurityInformation;
    if (Irp->RequestorMode == UserMode)
    {
        if ((SecurityInformation &
             (OWNER_SECURITY_INFORMATION |
              GROUP_SECURITY_INFORMATION |
              DACL_SECURITY_INFORMATION)) &&
            !(FileCB->DesiredAccess & READ_CONTROL))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Done;
        }

        if ((SecurityInformation & SACL_SECURITY_INFORMATION) &&
            !(FileCB->DesiredAccess & ACCESS_SYSTEM_SECURITY))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Done;
        }
    }

    OutputLength = IrpSp->Parameters.QuerySecurity.Length;
    Output = GetBuffer(Irp);
    if (OutputLength != 0 && !Output)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto Done;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(NtfsGetMainResource(FileCB), TRUE);
    NtfsAcquireMetadata(VolCB);
    Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
    if (NT_SUCCESS(Status))
        Status = NtfsQuerySecurityDescriptor(FileCB,
                                             SecurityInformation,
                                             Output,
                                             OutputLength,
                                             &ResultLength);
    NtfsReleaseMetadata(VolCB);
    ExReleaseResourceLite(NtfsGetMainResource(FileCB));
    KeLeaveCriticalRegion();

Done:
    Irp->IoStatus.Status = Status;
    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_OVERFLOW)
        Irp->IoStatus.Information = ResultLength;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

static NTSTATUS
NtfsSetSecurityDescriptor(
    _In_ PFileContextBlock FileCB,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR ModificationDescriptor)
{
    PSECURITY_DESCRIPTOR SecurityDescriptor = NULL;
    SECURITY_DESCRIPTOR NormalizedDescriptor;
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    PACL Sacl;
    BOOLEAN Present, Defaulted;
    PUCHAR RawDescriptor = NULL;
    ULONG RawLength = 0;
    NTSTATUS Status;

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
    {
        Status = RtlGetSaclSecurityDescriptor(ModificationDescriptor, &Present, &Sacl, &Defaulted);
        if (!NT_SUCCESS(Status)) return Status;
        if (Present && Sacl && !Sacl->AceCount)
        {
            Status = RtlCreateSecurityDescriptor(&NormalizedDescriptor, SECURITY_DESCRIPTOR_REVISION);
            if (!NT_SUCCESS(Status)) return Status;
            NormalizedDescriptor.Control = ((PISECURITY_DESCRIPTOR)ModificationDescriptor)->Control & ~SE_SELF_RELATIVE;
            NormalizedDescriptor.Sbz1 = ((PISECURITY_DESCRIPTOR)ModificationDescriptor)->Sbz1;
            Status = RtlGetOwnerSecurityDescriptor(ModificationDescriptor, &NormalizedDescriptor.Owner, &Defaulted);
            if (!NT_SUCCESS(Status)) return Status;
            Status = RtlGetGroupSecurityDescriptor(ModificationDescriptor, &NormalizedDescriptor.Group, &Defaulted);
            if (!NT_SUCCESS(Status)) return Status;
            Status = RtlGetDaclSecurityDescriptor(ModificationDescriptor, &Present, &NormalizedDescriptor.Dacl, &Defaulted);
            if (!NT_SUCCESS(Status)) return Status;
            ModificationDescriptor = &NormalizedDescriptor;
        }
    }

    Status = NtfsFileRecordReadSecurityDescriptor(FileCB->FileRec,
                                                  NULL,
                                                  &RawLength);
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        RawDescriptor = (PUCHAR)ExAllocatePoolWithTag(PagedPool,
                                                      RawLength,
                                                      TAG_NTFS);
        if (!RawDescriptor)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = NtfsFileRecordReadSecurityDescriptor(FileCB->FileRec,
                                                      RawDescriptor,
                                                      &RawLength);
        if (!NT_SUCCESS(Status))
            goto Done;

        SecurityDescriptor = (PSECURITY_DESCRIPTOR)RawDescriptor;
        Status = SeSetSecurityDescriptorInfo(NULL,
                                             &SecurityInformation,
                                             ModificationDescriptor,
                                             &SecurityDescriptor,
                                             PagedPool,
                                             IoGetFileObjectGenericMapping());
        if (!NT_SUCCESS(Status))
        {
            SecurityDescriptor = NULL;
            goto Done;
        }
    }
    else if (Status == STATUS_NOT_FOUND)
    {
        SeCaptureSubjectContext(&SubjectContext);
        Status = SeAssignSecurity(NULL,
                                  ModificationDescriptor,
                                  &SecurityDescriptor,
                                  !!(NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY),
                                  &SubjectContext,
                                  IoGetFileObjectGenericMapping(),
                                  PagedPool);
        SeReleaseSubjectContext(&SubjectContext);
        if (!NT_SUCCESS(Status))
        {
            SecurityDescriptor = NULL;
            goto Done;
        }
    }
    else
    {
        goto Done;
    }

    Status = NtfsFileRecordSetSecurityDescriptor(FileCB->FileRec,
                                                 (const UCHAR*)SecurityDescriptor,
                                                 RtlLengthSecurityDescriptor(SecurityDescriptor));

Done:
    if (SecurityDescriptor && SecurityDescriptor != (PSECURITY_DESCRIPTOR)RawDescriptor)
        ExFreePool(SecurityDescriptor);
    if (RawDescriptor)
        ExFreePoolWithTag(RawDescriptor, TAG_NTFS);
    return Status;
}

_Function_class_(IRP_MJ_SET_SECURITY)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdSetSecurity(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                   _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    PIO_STACK_LOCATION IrpSp;
    PFileContextBlock FileCB;
    PVolumeContextBlock VolCB;
    SECURITY_INFORMATION SecurityInformation;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    if (VolumeDeviceObject == NtfsDiskFileSystemDeviceObject ||
        !IrpSp->FileObject ||
        !IrpSp->FileObject->FsContext)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Done;
    }

    FileCB = NtfsGetFileContext(IrpSp->FileObject);
    VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    if (!FileCB->FileRec || !VolCB || !VolCB->DiskVolume ||
        !IrpSp->Parameters.SetSecurity.SecurityDescriptor)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }

    SecurityInformation = IrpSp->Parameters.SetSecurity.SecurityInformation;
    if (Irp->RequestorMode == UserMode)
    {
        if ((SecurityInformation &
             (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION)) &&
            !(FileCB->DesiredAccess & WRITE_OWNER))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Done;
        }

        if ((SecurityInformation & DACL_SECURITY_INFORMATION) &&
            !(FileCB->DesiredAccess & WRITE_DAC))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Done;
        }

        if ((SecurityInformation & SACL_SECURITY_INFORMATION) &&
            !(FileCB->DesiredAccess & ACCESS_SYSTEM_SECURITY))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Done;
        }
    }

    if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
    {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto Done;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
    NtfsAcquireMetadata(VolCB);
    Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsSetSecurityDescriptor(FileCB,
                                           SecurityInformation,
                                           IrpSp->Parameters.SetSecurity.SecurityDescriptor);
        if (NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY)
            InterlockedIncrement(&VolCB->DirGeneration);
    }
    NtfsReleaseMetadata(VolCB);
    ExReleaseResourceLite(NtfsGetMainResource(FileCB));
    KeLeaveCriticalRegion();

Done:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}
