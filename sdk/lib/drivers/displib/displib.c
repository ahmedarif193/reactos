/*
 * PROJECT:     ReactOS Display Miniport Support Library
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Native-style runtime binding between display miniports and dxgkrnl
 */

#include <ntddk.h>
#include <dispmprt.h>

#define IOCTL_DXGKRNL_GET_LEGACY_FULL_INIT_ENTRY CTL_CODE(0x23, 0x0F, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_DXGKRNL_GET_DOD_INIT_ENTRY CTL_CODE(0x23, 0x10, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_DXGKRNL_GET_FULL_INIT_ENTRY CTL_CODE(0x23, 0x11, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_DXGKRNL_GET_UNINIT_ENTRY CTL_CODE(0x23, 0x12, METHOD_NEITHER, FILE_ANY_ACCESS)

C_ASSERT(IOCTL_DXGKRNL_GET_LEGACY_FULL_INIT_ENTRY == 0x23003F);
C_ASSERT(IOCTL_DXGKRNL_GET_DOD_INIT_ENTRY == 0x230043);
C_ASSERT(IOCTL_DXGKRNL_GET_FULL_INIT_ENTRY == 0x230047);
C_ASSERT(IOCTL_DXGKRNL_GET_UNINIT_ENTRY == 0x23004B);

typedef NTSTATUS (APIENTRY *PDISPLIB_INITIALIZE)(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath, _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData);
typedef NTSTATUS (APIENTRY *PDISPLIB_DOD_INITIALIZE)(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath, _In_ PKMDDOD_INITIALIZATION_DATA KmDodInitializationData);
typedef NTSTATUS (APIENTRY *PDISPLIB_UNINITIALIZE)(_In_ PDRIVER_OBJECT DriverObject);

static PDXGKDDI_START_DEVICE DisplibOriginalStartDevice;

typedef struct _DISPLIB_DXGKRNL_CONNECTION
{
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS LoadStatus;
} DISPLIB_DXGKRNL_CONNECTION, *PDISPLIB_DXGKRNL_CONNECTION;

static VOID DisplibInitializeServiceName(_Out_ PUNICODE_STRING ServiceName)
{
    RtlInitUnicodeString(ServiceName, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\DXGKrnl");
}

static BOOLEAN DisplibIsSupportedDodVersion(_In_ ULONG Version)
{
    switch (Version)
    {
        case DXGKDDI_INTERFACE_VERSION_WDDM3_2:
        case DXGKDDI_INTERFACE_VERSION_WDDM3_1:
        case DXGKDDI_INTERFACE_VERSION_WDDM3_0:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_9:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_8:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_7:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_6:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_5:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_4:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_3:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_2:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_1_6:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_1_5:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_1:
        case DXGKDDI_INTERFACE_VERSION_WDDM2_0:
        case 0x5022: /* Windows 10 10240 WDDM 2.0 */
        case DXGKDDI_INTERFACE_VERSION_WDDM1_3_PATH_INDEPENDENT_ROTATION:
        case DXGKDDI_INTERFACE_VERSION_WDDM1_3:
        case DXGKDDI_INTERFACE_VERSION_WIN8:
        case DXGKDDI_INTERFACE_VERSION_WIN7:
        case DXGKDDI_INTERFACE_VERSION_VISTA_SP1:
        case DXGKDDI_INTERFACE_VERSION_VISTA:
            return TRUE;
        default:
            return FALSE;
    }
}

static NTSTATUS APIENTRY DisplibStartDevice(_In_ PVOID MiniportDeviceContext, _In_ PDXGK_START_INFO DxgkStartInfo, _In_ PDXGK_INTERFACE DxgkInterface, _Out_ PULONG NumberOfVideoPresentSources, _Out_ PULONG NumberOfChildren)
{
    return DisplibOriginalStartDevice(MiniportDeviceContext, DxgkStartInfo, DxgkInterface, NumberOfVideoPresentSources, NumberOfChildren);
}

static NTSTATUS DisplibConnectDxgkrnl(_Out_ PDISPLIB_DXGKRNL_CONNECTION Connection)
{
    UNICODE_STRING ServiceName;
    UNICODE_STRING DeviceName;
    LARGE_INTEGER RetryDelay;
    NTSTATUS Status;
    ULONG Attempt;
    ULONG AttemptCount;

    RtlZeroMemory(Connection, sizeof(*Connection));
    DisplibInitializeServiceName(&ServiceName);
    Connection->LoadStatus = ZwLoadDriver(&ServiceName);
    if (!NT_SUCCESS(Connection->LoadStatus) && Connection->LoadStatus != STATUS_IMAGE_ALREADY_LOADED)
        return Connection->LoadStatus;
    RtlInitUnicodeString(&DeviceName, L"\\Device\\DxgKrnl");
    AttemptCount = Connection->LoadStatus == STATUS_IMAGE_ALREADY_LOADED ? 10 : 1;
    RetryDelay.QuadPart = -50000;
    Status = STATUS_DEVICE_DOES_NOT_EXIST;
    for (Attempt = 0; Attempt < AttemptCount; ++Attempt)
    {
        Status = IoGetDeviceObjectPointer(&DeviceName, GENERIC_READ | GENERIC_WRITE, &Connection->FileObject, &Connection->DeviceObject);
        if (NT_SUCCESS(Status))
            return Connection->LoadStatus;
        if (Connection->LoadStatus == STATUS_IMAGE_ALREADY_LOADED)
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &RetryDelay);
    }
    if (Connection->LoadStatus != STATUS_IMAGE_ALREADY_LOADED)
        (VOID)ZwUnloadDriver(&ServiceName);
    RtlZeroMemory(Connection, sizeof(*Connection));
    return Status;
}

static VOID DisplibDisconnectDxgkrnl(_Inout_ PDISPLIB_DXGKRNL_CONNECTION Connection)
{
    if (Connection->FileObject != NULL)
        ObDereferenceObject(Connection->FileObject);
    Connection->FileObject = NULL;
    Connection->DeviceObject = NULL;
}

static NTSTATUS DisplibResolveEntry(_In_ PDEVICE_OBJECT DeviceObject, _In_ ULONG IoControlCode, _Out_ PVOID *EntryPoint)
{
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    PIRP Irp;
    PVOID ResolvedEntry;
    NTSTATUS Status;

    *EntryPoint = NULL;
    ResolvedEntry = NULL;
    IoStatusBlock.Status = STATUS_NOT_SUPPORTED;
    IoStatusBlock.Information = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, DeviceObject, NULL, 0, &ResolvedEntry, sizeof(ResolvedEntry), TRUE, &Event, &IoStatusBlock);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Irp->RequestorMode = KernelMode;
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = IoStatusBlock.Status;
    }
    if (NT_SUCCESS(Status))
        *EntryPoint = ResolvedEntry;
    return Status;
}

static VOID DisplibUnloadDxgkrnl(VOID)
{
    UNICODE_STRING ServiceName;

    DisplibInitializeServiceName(&ServiceName);
    (VOID)ZwUnloadDriver(&ServiceName);
}

NTSTATUS APIENTRY DxgkInitialize(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath, _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData)
{
    DISPLIB_DXGKRNL_CONNECTION Connection;
    PDISPLIB_INITIALIZE Initialize;
    PVOID EntryPoint;
    NTSTATUS Status;

    if (DriverObject == NULL || RegistryPath == NULL || DriverInitializationData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DisplibConnectDxgkrnl(&Connection);
    if (!NT_SUCCESS(Status) && Status != STATUS_IMAGE_ALREADY_LOADED)
        return Status;
    /* This library targets NT10, for which native Displib selects 0x230047. */
    Status = DisplibResolveEntry(Connection.DeviceObject, IOCTL_DXGKRNL_GET_FULL_INIT_ENTRY, &EntryPoint);
    if (Status == STATUS_INVALID_DEVICE_REQUEST)
        Status = DisplibResolveEntry(Connection.DeviceObject, IOCTL_DXGKRNL_GET_LEGACY_FULL_INIT_ENTRY, &EntryPoint);
    if (NT_SUCCESS(Status))
    {
        DisplibOriginalStartDevice = DriverInitializationData->DxgkDdiStartDevice;
        DriverInitializationData->DxgkDdiStartDevice = DisplibStartDevice;
        Initialize = (PDISPLIB_INITIALIZE)EntryPoint;
        Status = Initialize(DriverObject, RegistryPath, DriverInitializationData);
    }
    DisplibDisconnectDxgkrnl(&Connection);
    if (!NT_SUCCESS(Status) && Connection.LoadStatus != STATUS_IMAGE_ALREADY_LOADED)
        DisplibUnloadDxgkrnl();
    return Status;
}

NTSTATUS APIENTRY DxgkInitializeDisplayOnlyDriver(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath, _In_ PKMDDOD_INITIALIZATION_DATA KmDodInitializationData)
{
    DISPLIB_DXGKRNL_CONNECTION Connection;
    PDISPLIB_DOD_INITIALIZE Initialize;
    PVOID EntryPoint;
    NTSTATUS Status;

    if (DriverObject == NULL || RegistryPath == NULL || KmDodInitializationData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!DisplibIsSupportedDodVersion(KmDodInitializationData->Version))
        return STATUS_REVISION_MISMATCH;
    Status = DisplibConnectDxgkrnl(&Connection);
    if (!NT_SUCCESS(Status) && Status != STATUS_IMAGE_ALREADY_LOADED)
        return Status;
    Status = DisplibResolveEntry(Connection.DeviceObject, IOCTL_DXGKRNL_GET_DOD_INIT_ENTRY, &EntryPoint);
    if (NT_SUCCESS(Status))
    {
        DisplibOriginalStartDevice = KmDodInitializationData->DxgkDdiStartDevice;
        KmDodInitializationData->DxgkDdiStartDevice = DisplibStartDevice;
        Initialize = (PDISPLIB_DOD_INITIALIZE)EntryPoint;
        Status = Initialize(DriverObject, RegistryPath, KmDodInitializationData);
    }
    DisplibDisconnectDxgkrnl(&Connection);
    if (!NT_SUCCESS(Status) && Connection.LoadStatus != STATUS_IMAGE_ALREADY_LOADED)
        DisplibUnloadDxgkrnl();
    return Status;
}

NTSTATUS APIENTRY DxgkUnInitialize(_In_ PDRIVER_OBJECT DriverObject)
{
    DISPLIB_DXGKRNL_CONNECTION Connection;
    PDISPLIB_UNINITIALIZE UnInitialize;
    PVOID EntryPoint;
    NTSTATUS Status;

    if (DriverObject == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DisplibConnectDxgkrnl(&Connection);
    if (!NT_SUCCESS(Status) && Status != STATUS_IMAGE_ALREADY_LOADED)
        return Status;
    Status = DisplibResolveEntry(Connection.DeviceObject, IOCTL_DXGKRNL_GET_UNINIT_ENTRY, &EntryPoint);
    if (NT_SUCCESS(Status))
    {
        UnInitialize = (PDISPLIB_UNINITIALIZE)EntryPoint;
        (VOID)UnInitialize(DriverObject);
    }
    DisplibDisconnectDxgkrnl(&Connection);
    if (Connection.LoadStatus != STATUS_IMAGE_ALREADY_LOADED)
        DisplibUnloadDxgkrnl();
    return Status;
}
