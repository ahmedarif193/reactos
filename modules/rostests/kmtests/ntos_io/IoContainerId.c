/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Plug and Play container identifier tests
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntddstor.h>

START_TEST(IoContainerId)
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PFILE_OBJECT FileObject;
    PWSTR SymbolicLinks;
    PWSTR SymbolicLink;
    PWSTR ContainerId;
    UNICODE_STRING SymbolicLinkName;
    UNICODE_STRING ContainerIdString;
    GUID ContainerGuid;
    ULONG Length;
    NTSTATUS Status;
    BOOLEAN Tested;

    SymbolicLinks = NULL;
    Status = IoGetDeviceInterfaces(&GUID_DEVINTERFACE_DISK, NULL, 0, &SymbolicLinks);
    if (skip(NT_SUCCESS(Status) && (SymbolicLinks != NULL) && (*SymbolicLinks != UNICODE_NULL), "no disk device interface is available\n"))
        return;

    Tested = FALSE;
    for (SymbolicLink = SymbolicLinks; *SymbolicLink != UNICODE_NULL; SymbolicLink += wcslen(SymbolicLink) + 1)
    {
        RtlInitUnicodeString(&SymbolicLinkName, SymbolicLink);
        Status = IoGetDeviceObjectPointer(&SymbolicLinkName, FILE_READ_ATTRIBUTES, &FileObject, &DeviceObject);
        if (!NT_SUCCESS(Status))
            continue;

        PhysicalDeviceObject = IoGetDeviceAttachmentBaseRef(DeviceObject);
        ObDereferenceObject(FileObject);
        if (PhysicalDeviceObject == NULL)
            continue;

        Length = 0;
        Status = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyContainerID, 0, NULL, &Length);
        if (Status != STATUS_BUFFER_TOO_SMALL)
        {
            ObDereferenceObject(PhysicalDeviceObject);
            continue;
        }

        ok(Length >= 39 * sizeof(WCHAR), "container ID length %lu is too small\n", Length);
        ContainerId = ExAllocatePoolZero(PagedPool, Length, 'dIcK');
        if (skip(ContainerId != NULL, "could not allocate the container ID buffer\n"))
        {
            ObDereferenceObject(PhysicalDeviceObject);
            break;
        }

        Status = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyContainerID, Length, ContainerId, &Length);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status) && (Length >= sizeof(UNICODE_NULL)))
        {
            ok(ContainerId[Length / sizeof(WCHAR) - 1] == UNICODE_NULL, "container ID is not terminated\n");
            ContainerIdString.Buffer = ContainerId;
            ContainerIdString.Length = (USHORT)(Length - sizeof(UNICODE_NULL));
            ContainerIdString.MaximumLength = (USHORT)Length;
            Status = RtlGUIDFromString(&ContainerIdString, &ContainerGuid);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Tested = TRUE;
        }
        else if (NT_SUCCESS(Status))
        {
            ok(FALSE, "container ID length %lu is invalid\n", Length);
        }

        ExFreePoolWithTag(ContainerId, 'dIcK');
        ObDereferenceObject(PhysicalDeviceObject);
        if (Tested)
            break;
    }

    ExFreePool(SymbolicLinks);
    skip(Tested, "no disk PDO exposed a container ID\n");
}
