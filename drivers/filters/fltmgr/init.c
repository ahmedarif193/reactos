/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry and control device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;

FLTP_GLOBALS FltGlobals;

static
NTSTATUS
FltpCreateNamedDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCWSTR DeviceName,
    _In_ PCWSTR LinkName,
    _Out_ PDEVICE_OBJECT *DeviceObject)
{
    UNICODE_STRING Name, Link;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, DeviceName);
    Status = IoCreateDevice(DriverObject,
                            0,
                            &Name,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlInitUnicodeString(&Link, LinkName);
    Status = IoCreateSymbolicLink(&Link, &Name);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(*DeviceObject);
        *DeviceObject = NULL;
        return Status;
    }

    (*DeviceObject)->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
NTSTATUS
FltpLoadUnload(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ BOOLEAN Load)
{
    PFILTER_NAME Input = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    UNICODE_STRING Name;

    if (Input == NULL ||
        InputLength < FIELD_OFFSET(FILTER_NAME, FilterName) ||
        Input->Length == 0 ||
        (Input->Length & 1) ||
        InputLength < FIELD_OFFSET(FILTER_NAME, FilterName) + (ULONG)Input->Length)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!SeSinglePrivilegeCheck(SeExports->SeLoadDriverPrivilege, Irp->RequestorMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Name.Buffer = Input->FilterName;
    Name.Length = Name.MaximumLength = Input->Length;

    return Load ? FltLoadFilter(&Name) : FltUnloadFilter(&Name);
}

NTSTATUS
NTAPI
FltpControlDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;

    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
        case IRP_MJ_CLEANUP:
        case IRP_MJ_CLOSE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MJ_DEVICE_CONTROL:
            switch (Stack->Parameters.DeviceIoControl.IoControlCode)
            {
                case IOCTL_FILTER_LOAD:
                    Status = FltpLoadUnload(Irp, Stack, TRUE);
                    break;

                case IOCTL_FILTER_UNLOAD:
                    Status = FltpLoadUnload(Irp, Stack, FALSE);
                    break;

                default:
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                    break;
            }
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING RoutineName = RTL_CONSTANT_STRING(L"IoIs32bitProcess");
    NTSTATUS Status;
    ULONG Index;

    RtlZeroMemory(&FltGlobals, sizeof(FltGlobals));
    FltGlobals.Is32bitProcess = MmGetSystemRoutineAddress(&RoutineName);
    ExInitializeResourceLite(&FltGlobals.Lock);
    InitializeListHead(&FltGlobals.FilterList);
    InitializeListHead(&FltGlobals.VolumeList);
    InitializeListHead(&FltGlobals.TransactionContextList);
    InitializeListHead(&FltGlobals.TransactionList);
    ExInitializeFastMutex(&FltGlobals.TransactionLock);
    KeInitializeGuardedMutex(&FltGlobals.TransactionCreateLock);
    ExInitializeFastMutex(&FltGlobals.AttachLock);
    FltpInitializeLock(&FltGlobals.ContextLock);
    FltpInitializeLock(&FltGlobals.NameLock);
    KeInitializeSpinLock(&FltGlobals.GeneratedLock);

    Status = FltpDuplicateString(&FltGlobals.ServiceKey, RegistryPath);
    if (!NT_SUCCESS(Status))
    {
        goto Fail;
    }

    Status = FltpCreateNamedDevice(DriverObject,
                                   L"\\FileSystem\\Filters\\FltMgr",
                                   L"\\DosDevices\\FltMgr",
                                   &FltGlobals.ControlDevice);
    if (!NT_SUCCESS(Status))
    {
        goto Fail;
    }

    Status = FltpCreateNamedDevice(DriverObject,
                                   L"\\FileSystem\\Filters\\FltMgrMsg",
                                   L"\\DosDevices\\FltMgrMsg",
                                   &FltGlobals.MessageDevice);
    if (!NT_SUCCESS(Status))
    {
        goto Fail;
    }

    Status = FltpInitializePorts(DriverObject);
    if (!NT_SUCCESS(Status))
    {
        goto Fail;
    }

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
    {
        DriverObject->MajorFunction[Index] = FltpDispatch;
    }

    FltpInitializeFastIo(DriverObject);

    Status = FltpRegisterFsFilterCallbacks(DriverObject);
    if (!NT_SUCCESS(Status))
    {
        goto Fail;
    }

    FltGlobals.DriverObject = DriverObject;

    Status = FltpInitializeVolumes(DriverObject);
    if (!NT_SUCCESS(Status))
    {
        FltGlobals.DriverObject = NULL;
        goto Fail;
    }

    return STATUS_SUCCESS;

Fail:
    if (FltGlobals.MessageDevice != NULL)
    {
        UNICODE_STRING Link = RTL_CONSTANT_STRING(L"\\DosDevices\\FltMgrMsg");
        IoDeleteSymbolicLink(&Link);
        IoDeleteDevice(FltGlobals.MessageDevice);
    }
    if (FltGlobals.ControlDevice != NULL)
    {
        UNICODE_STRING Link = RTL_CONSTANT_STRING(L"\\DosDevices\\FltMgr");
        IoDeleteSymbolicLink(&Link);
        IoDeleteDevice(FltGlobals.ControlDevice);
    }
    FltpFreeString(&FltGlobals.ServiceKey);
    ExDeleteResourceLite(&FltGlobals.Lock);
    return Status;
}
