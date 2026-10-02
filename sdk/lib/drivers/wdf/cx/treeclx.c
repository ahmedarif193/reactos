/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Trusted execution environment KMDF class extension
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "classlibrary.h"
#include <initguid.h>
#include <wdmguid.h>
#include <trustedrt.h>
#include <trustedruntimeclx.h>

#define TR_TAG 'xlCT'
#define TR_GUID_STRING_LENGTH 38

typedef enum _TR_REQUEST_STATE
{
    TrRequestIdle,
    TrRequestProcessing,
    TrRequestPendingUnmarked,
    TrRequestPending,
    TrRequestUnmarking,
    TrRequestCancelling,
    TrRequestCancelled,
    TrRequestDone
} TR_REQUEST_STATE;

typedef struct _TR_MASTER_CONTEXT
{
    PWDF_DRIVER_GLOBALS ClientGlobals;
    TR_SECURE_DEVICE_CALLBACKS Callbacks;
    WDFQUEUE Queue;
    BOOLEAN ContextCreated;
    BOOLEAN Enumerated;
    BOOLEAN Connected;
} TR_MASTER_CONTEXT, *PTR_MASTER_CONTEXT;

typedef struct _TR_DEPENDENCY
{
    GUID Id;
    PVOID Notification;
} TR_DEPENDENCY, *PTR_DEPENDENCY;

typedef struct _TR_SERVICE
{
    LIST_ENTRY Entry;
    LONG References;
    GUID Guid;
    ULONG MajorVersion;
    ULONG MinorVersion;
    WDFDEVICE Master;
    WDFDEVICE Device;
    PTR_SECURE_SERVICE_CALLBACKS Callbacks;
    KMUTEX Lock;
    WDFQUEUE Queue;
    EX_RUNDOWN_REF Rundown;
    KSPIN_LOCK RequestLock;
    LIST_ENTRY Pending;
    LIST_ENTRY Files;
    BOOLEAN Removed;
    BOOLEAN Created;
    BOOLEAN Connected;
    BOOLEAN ResourceDependency;
    ULONG DependencyCount;
    TR_DEPENDENCY Dependencies[ANYSIZE_ARRAY];
} TR_SERVICE, *PTR_SERVICE;

typedef struct _TR_FILE_CONTEXT
{
    LIST_ENTRY Entry;
    PTR_SERVICE Service;
    WDFOBJECT Session;
    WDFQUEUE Queue;
    BOOLEAN Listed;
} TR_FILE_CONTEXT, *PTR_FILE_CONTEXT;

typedef struct _TR_REQUEST_CONTEXT
{
    LIST_ENTRY Entry;
    WDFREQUEST Request;
    TR_REQUEST_STATE State;
    BOOLEAN Prepared;
    BOOLEAN Listed;
    BOOLEAN ResultSet;
    BOOLEAN Asynchronous;
    BOOLEAN CancelArrived;
    BOOLEAN FinishOnCancel;
    NTSTATUS Result;
    ULONG_PTR BytesWritten;
    PTR_SERVICE Service;
    WDFOBJECT Session;
    PVOID MiniportContext;
    ULONG Flags;
    KPRIORITY Priority;
    PTR_SERVICE_REQUEST_RESPONSE Response;
    TR_SERVICE_REQUEST ServiceRequest;
    WORK_QUEUE_ITEM WorkItem;
} TR_REQUEST_CONTEXT, *PTR_REQUEST_CONTEXT;

typedef struct _TR_CALLOUT
{
    PTR_SERVICE Service;
    WDFREQUEST Request;
    PTR_REQUEST_CONTEXT Context;
    KPRIORITY Priority;
    ULONG_PTR BytesWritten;
    NTSTATUS Status;
} TR_CALLOUT, *PTR_CALLOUT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TR_MASTER_CONTEXT, TrGetMasterContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TR_FILE_CONTEXT, TrGetFileContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TR_REQUEST_CONTEXT, TrGetRequestContext)

WDFOBJECT TrBindContext;

static FAST_MUTEX TrLock;
static LIST_ENTRY TrServices;
static PDRIVER_OBJECT TrDriverObject;
static WDFDEVICE TrControlDevice;

static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL TrEvtSerializedIoDeviceControl;

static
NTSTATUS
TrCreateSerialQueue(
    _Out_ WDFQUEUE *Queue)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG Config;

    WDF_IO_QUEUE_CONFIG_INIT(&Config, WdfIoQueueDispatchSequential);
    Config.PowerManaged = WdfFalse;
    Config.EvtIoDeviceControl = TrEvtSerializedIoDeviceControl;
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    return WdfIoQueueCreate(TrControlDevice, &Config, &Attributes, Queue);
}

static
VOID
TrDeleteSerialQueue(
    _Inout_ WDFQUEUE *Queue)
{
    if (*Queue == NULL)
        return;

    WdfIoQueuePurgeSynchronously(*Queue);
    WdfObjectDelete(*Queue);
    *Queue = NULL;
}

static
VOID
TrCancelPendingRequests(
    _In_ PTR_SERVICE Service);

static
VOID
TrAcquire(
    _In_ PKMUTEX Mutex)
{
    KeWaitForSingleObject(Mutex, Executive, KernelMode, FALSE, NULL);
}

static
VOID
TrRelease(
    _In_ PKMUTEX Mutex)
{
    KeReleaseMutex(Mutex, FALSE);
}

static
VOID
TrReferenceService(
    _In_ PTR_SERVICE Service)
{
    InterlockedIncrement(&Service->References);
}

static
VOID
TrDereferenceService(
    _In_ PTR_SERVICE Service)
{
    if (InterlockedDecrement(&Service->References) == 0)
        ExFreePoolWithTag(Service, TR_TAG);
}

static
BOOLEAN
TrIsInterfacePresent(
    _In_ LPCGUID Guid)
{
    PWSTR List = NULL;
    BOOLEAN Present;

    if (!NT_SUCCESS(IoGetDeviceInterfaces(Guid, NULL, 0, &List)) || List == NULL)
        return FALSE;

    Present = List[0] != UNICODE_NULL;
    ExFreePool(List);
    return Present;
}

static
BOOLEAN
TrAreDependenciesPresent(
    _In_ PTR_SERVICE Service)
{
    ULONG Index;

    if (Service->ResourceDependency)
        return FALSE;

    for (Index = 0; Index < Service->DependencyCount; Index++)
    {
        if (!TrIsInterfacePresent(&Service->Dependencies[Index].Id))
            return FALSE;
    }

    return TRUE;
}

static
VOID
TrConnectService(
    _In_ PTR_SERVICE Service)
{
    if (!Service->Created || Service->Connected)
        return;

    if (Service->Callbacks->EvtTrConnectSecureService == NULL ||
        NT_SUCCESS(Service->Callbacks->EvtTrConnectSecureService(Service->Device)))
    {
        Service->Connected = TRUE;
    }
}

static
NTSTATUS
TrInstantiateService(
    _In_ PTR_SERVICE Service)
{
    DECLARE_CONST_UNICODE_STRING(Sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    PTR_MASTER_CONTEXT Master = TrGetMasterContext(Service->Master);
    PTR_SECURE_SERVICE_CALLBACKS Callbacks;
    PWDFDEVICE_INIT DeviceInit;
    WDFDEVICE Device;
    NTSTATUS Status;

    if (Service->Created)
        return STATUS_SUCCESS;

    if (Master->Callbacks.EvtTrQueryServiceCallbacks == NULL)
        return STATUS_NOT_SUPPORTED;

    Callbacks = Master->Callbacks.EvtTrQueryServiceCallbacks(Service->Master, &Service->Guid);
    if (Callbacks == NULL)
        return STATUS_NOT_SUPPORTED;

    DeviceInit = WDFCX_CALL(WdfControlDeviceInitAllocateTableIndex, PFN_WDFCONTROLDEVICEINITALLOCATE)(
        Master->ClientGlobals, WdfDeviceGetDriver(Service->Master), &Sddl);
    if (DeviceInit == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = WDFCX_CALL(WdfDeviceCreateTableIndex, PFN_WDFDEVICECREATE)(
        Master->ClientGlobals, &DeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &Device);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(DeviceInit);
        return Status;
    }

    WDFCX_CALL(WdfControlFinishInitializingTableIndex, PFN_WDFCONTROLFINISHINITIALIZING)(
        Master->ClientGlobals, Device);

    if ((Master->Callbacks.Flags & TR_DEVICE_SERIALIZE_MASK) == TR_DEVICE_SERIALIZE_PER_SERVICE)
    {
        Status = TrCreateSerialQueue(&Service->Queue);
        if (!NT_SUCCESS(Status))
        {
            WdfObjectDelete(Device);
            return Status;
        }
    }

    Service->Callbacks = Callbacks;
    Service->Device = Device;
    if (Callbacks->EvtTrCreateSecureServiceContext != NULL)
    {
        Status = Callbacks->EvtTrCreateSecureServiceContext(Service->Master, &Service->Guid, Device);
        if (!NT_SUCCESS(Status))
        {
            Service->Device = NULL;
            WdfObjectDelete(Device);
            TrDeleteSerialQueue(&Service->Queue);
            return Status;
        }
    }

    Service->Created = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TrInterfaceArrival(
    _In_ PVOID NotificationStructure,
    _Inout_opt_ PVOID Context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION Notification = NotificationStructure;
    PTR_SERVICE Service = Context;
    PTR_MASTER_CONTEXT Master;

    if (!IsEqualGUID(&Notification->Event, &GUID_DEVICE_INTERFACE_ARRIVAL))
        return STATUS_SUCCESS;

    TrAcquire(&Service->Lock);
    if (!Service->Removed && !Service->Created && TrAreDependenciesPresent(Service))
    {
        Master = TrGetMasterContext(Service->Master);
        if (NT_SUCCESS(TrInstantiateService(Service)) && Master->Connected)
            TrConnectService(Service);
    }
    TrRelease(&Service->Lock);
    return STATUS_SUCCESS;
}

static
VOID
TrEnumerateServices(
    _In_ WDFDEVICE Device)
{
    PTR_MASTER_CONTEXT Master = TrGetMasterContext(Device);
    PTR_SECURE_SERVICE Description;
    PTR_SERVICE Service;
    PUCHAR Buffer = NULL;
    ULONG BufferSize = 0;
    ULONG Size, Index, Dependency, Count;
    NTSTATUS Status;

    if (Master->Callbacks.EvtTrEnumerateSecureServices == NULL)
        return;

    for (Index = 0; ; Index++)
    {
        for (;;)
        {
            Size = BufferSize;
            Status = Master->Callbacks.EvtTrEnumerateSecureServices(Device, Index, Buffer, &Size);
            if (Status != STATUS_BUFFER_TOO_SMALL || Size <= BufferSize)
                break;

            if (Buffer != NULL)
                ExFreePoolWithTag(Buffer, TR_TAG);
            Buffer = ExAllocatePoolZero(PagedPool, Size, TR_TAG);
            if (Buffer == NULL)
                return;
            BufferSize = Size;
        }

        if (!NT_SUCCESS(Status) || Buffer == NULL ||
            BufferSize < FIELD_OFFSET(TR_SECURE_SERVICE, Dependencies))
        {
            break;
        }

        Description = (PTR_SECURE_SERVICE)Buffer;
        Count = Description->CountDependencies;
        if (Count > (BufferSize - FIELD_OFFSET(TR_SECURE_SERVICE, Dependencies)) / sizeof(TR_SECURE_DEPENDENCY))
            break;

        Service = ExAllocatePoolZero(NonPagedPoolNx,
                                     FIELD_OFFSET(TR_SERVICE, Dependencies) + (Count + 1) * sizeof(TR_DEPENDENCY),
                                     TR_TAG);
        if (Service == NULL)
            break;

        Service->References = 1;
        Service->Guid = Description->ServiceGuid;
        Service->MajorVersion = Description->MajorVersion;
        Service->MinorVersion = Description->MinorVersion;
        Service->Master = Device;
        KeInitializeMutex(&Service->Lock, 0);
        KeInitializeSpinLock(&Service->RequestLock);
        ExInitializeRundownProtection(&Service->Rundown);
        InitializeListHead(&Service->Pending);
        InitializeListHead(&Service->Files);
        for (Dependency = 0; Dependency < Count; Dependency++)
        {
            if (Description->Dependencies[Dependency].Type == TRSecureOSDependency)
                Service->Dependencies[Service->DependencyCount++].Id = Description->Dependencies[Dependency].Id;
            else
                Service->ResourceDependency = TRUE;
        }

        ExAcquireFastMutex(&TrLock);
        InsertTailList(&TrServices, &Service->Entry);
        ExReleaseFastMutex(&TrLock);
    }

    if (Buffer != NULL)
        ExFreePoolWithTag(Buffer, TR_TAG);
}

static
PTR_SERVICE
TrNextService(
    _In_ WDFDEVICE Device,
    _In_opt_ PTR_SERVICE Previous)
{
    PTR_SERVICE Service = NULL;
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&TrLock);
    for (Entry = Previous != NULL ? Previous->Entry.Flink : TrServices.Flink;
         Entry != &TrServices;
         Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, TR_SERVICE, Entry)->Master == Device)
        {
            Service = CONTAINING_RECORD(Entry, TR_SERVICE, Entry);
            TrReferenceService(Service);
            break;
        }
    }
    ExReleaseFastMutex(&TrLock);

    if (Previous != NULL)
        TrDereferenceService(Previous);
    return Service;
}

static
VOID
TrWatchDependencies(
    _In_ PTR_SERVICE Service)
{
    PTR_DEPENDENCY Dependency;
    ULONG Index;

    if (Service->ResourceDependency)
        return;

    for (Index = 0; Index < Service->DependencyCount; Index++)
    {
        Dependency = &Service->Dependencies[Index];
        if (Dependency->Notification != NULL)
            continue;

        if (!NT_SUCCESS(IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                                       0,
                                                       &Dependency->Id,
                                                       TrDriverObject,
                                                       TrInterfaceArrival,
                                                       Service,
                                                       &Dependency->Notification)))
        {
            Dependency->Notification = NULL;
        }
    }
}

static
VOID
TrStartServices(
    _In_ WDFDEVICE Device,
    _In_ BOOLEAN Instantiate)
{
    PTR_SERVICE Service;

    if (Instantiate)
    {
        for (Service = TrNextService(Device, NULL); Service != NULL; Service = TrNextService(Device, Service))
        {
            TrAcquire(&Service->Lock);
            if (!TrAreDependenciesPresent(Service))
                TrWatchDependencies(Service);
            if (TrAreDependenciesPresent(Service))
                TrInstantiateService(Service);
            TrRelease(&Service->Lock);
        }
    }

    for (Service = TrNextService(Device, NULL); Service != NULL; Service = TrNextService(Device, Service))
    {
        TrAcquire(&Service->Lock);
        TrConnectService(Service);
        TrRelease(&Service->Lock);
    }
}

static
VOID
TrDisconnectService(
    _In_ PTR_SERVICE Service)
{
    if (!Service->Connected)
        return;

    if (Service->Callbacks->EvtTrDisconnectSecureService != NULL)
        Service->Callbacks->EvtTrDisconnectSecureService(Service->Device);
    Service->Connected = FALSE;
}

static
VOID
TrRemoveService(
    _In_ PTR_SERVICE Service)
{
    PTR_FILE_CONTEXT File;
    ULONG Index;
    KIRQL Irql;

    TrAcquire(&Service->Lock);
    KeAcquireSpinLock(&Service->RequestLock, &Irql);
    Service->Removed = TRUE;
    KeReleaseSpinLock(&Service->RequestLock, Irql);
    TrRelease(&Service->Lock);

    for (Index = 0; Index < Service->DependencyCount; Index++)
    {
        if (Service->Dependencies[Index].Notification != NULL)
        {
            IoUnregisterPlugPlayNotification(Service->Dependencies[Index].Notification);
            Service->Dependencies[Index].Notification = NULL;
        }
    }

    if (!Service->Created)
        return;

    TrCancelPendingRequests(Service);
    TrDeleteSerialQueue(&Service->Queue);
    ExWaitForRundownProtectionRelease(&Service->Rundown);

    TrAcquire(&Service->Lock);
    TrDisconnectService(Service);
    while (!IsListEmpty(&Service->Files))
    {
        File = CONTAINING_RECORD(RemoveHeadList(&Service->Files), TR_FILE_CONTEXT, Entry);
        File->Listed = FALSE;
        if (Service->Callbacks->EvtTrDestroySecureSessionContext != NULL)
            Service->Callbacks->EvtTrDestroySecureSessionContext(Service->Device, &File->Session);
    }

    if (Service->Callbacks->EvtTrDestroySecureServiceContext != NULL)
        Service->Callbacks->EvtTrDestroySecureServiceContext(Service->Device);
    WdfObjectDelete(Service->Device);
    Service->Device = NULL;
    Service->Created = FALSE;
    TrRelease(&Service->Lock);
}

static
VOID
TrStopServices(
    _In_ WDFDEVICE Device,
    _In_ BOOLEAN Remove)
{
    PTR_SERVICE Service;
    PLIST_ENTRY Entry;

    if (!Remove)
    {
        for (Service = TrNextService(Device, NULL); Service != NULL; Service = TrNextService(Device, Service))
        {
            TrAcquire(&Service->Lock);
            TrDisconnectService(Service);
            TrRelease(&Service->Lock);
        }
        return;
    }

    for (;;)
    {
        Service = NULL;
        ExAcquireFastMutex(&TrLock);
        for (Entry = TrServices.Flink; Entry != &TrServices; Entry = Entry->Flink)
        {
            if (CONTAINING_RECORD(Entry, TR_SERVICE, Entry)->Master == Device)
            {
                Service = CONTAINING_RECORD(Entry, TR_SERVICE, Entry);
                RemoveEntryList(Entry);
                break;
            }
        }
        ExReleaseFastMutex(&TrLock);

        if (Service == NULL)
            break;

        TrRemoveService(Service);
        TrDereferenceService(Service);
    }
}

static
NTSTATUS
NTAPI
TrEvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PTR_MASTER_CONTEXT Master = TrGetMasterContext(Device);

    if (Master->Callbacks.EvtTrPrepareHardwareSecureEnvironment == NULL)
        return STATUS_SUCCESS;

    return Master->Callbacks.EvtTrPrepareHardwareSecureEnvironment(Device, ResourcesRaw, ResourcesTranslated);
}

static
NTSTATUS
NTAPI
TrEvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PTR_MASTER_CONTEXT Master = TrGetMasterContext(Device);

    if (Master->Callbacks.EvtTrReleaseHardwareSecureEnvironment == NULL)
        return STATUS_SUCCESS;

    return Master->Callbacks.EvtTrReleaseHardwareSecureEnvironment(Device, ResourcesTranslated);
}

static
NTSTATUS
NTAPI
TrEvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PTR_MASTER_CONTEXT Master = TrGetMasterContext(Device);
    BOOLEAN First;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(PreviousState);

    if (Master->Callbacks.EvtTrConnectSecureEnvironment != NULL)
    {
        Status = Master->Callbacks.EvtTrConnectSecureEnvironment(Device);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Master->Connected = TRUE;
    First = !Master->Enumerated;
    if (First)
    {
        Master->Enumerated = TRUE;
        TrEnumerateServices(Device);
    }

    TrStartServices(Device, First);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TrEvtD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PTR_MASTER_CONTEXT Master = TrGetMasterContext(Device);

    UNREFERENCED_PARAMETER(TargetState);

    TrStopServices(Device, FALSE);
    Master->Connected = FALSE;
    if (Master->Callbacks.EvtTrDisconnectSecureEnvironment != NULL)
        Master->Callbacks.EvtTrDisconnectSecureEnvironment(Device);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
TrEvtMasterCleanup(
    _In_ WDFOBJECT Object)
{
    WDFDEVICE Device = (WDFDEVICE)Object;
    PTR_MASTER_CONTEXT Master = TrGetMasterContext(Device);

    TrStopServices(Device, TRUE);
    TrDeleteSerialQueue(&Master->Queue);
    if (Master->ContextCreated && Master->Callbacks.EvtTrDestroySecureDeviceContext != NULL)
        Master->Callbacks.EvtTrDestroySecureDeviceContext(Device);
    Master->ContextCreated = FALSE;
}

static
VOID
NTAPI
TrEvtMasterIoDefault(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request)
{
    WDFDEVICE Device = WdfIoQueueGetDevice(Queue);
    PTR_MASTER_CONTEXT Master = TrGetMasterContext(Device);

    if (Master->Callbacks.EvtTrProcessOtherDeviceIo == NULL)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    Master->Callbacks.EvtTrProcessOtherDeviceIo(Device, Request);
}

static
NTSTATUS
NTAPI
TrClxHandoffMasterDeviceControl(
    _In_ WDFOBJECT BindContextObject,
    _Inout_ PWDFDEVICE_INIT DeviceInit,
    _In_ PTR_SECURE_DEVICE_CALLBACKS Callbacks,
    _Out_opt_ WDFDEVICE *MasterDevice)
{
    PWDF_DRIVER_GLOBALS ClientGlobals = (PWDF_DRIVER_GLOBALS)BindContextObject;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpCallbacks;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    PTR_MASTER_CONTEXT Master;
    WDFDEVICE Device;
    NTSTATUS Status;

    if (MasterDevice != NULL)
        *MasterDevice = NULL;

    if (ClientGlobals == NULL || DeviceInit == NULL || Callbacks == NULL)
        return STATUS_INVALID_PARAMETER;

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpCallbacks);
    PnpCallbacks.EvtDevicePrepareHardware = TrEvtPrepareHardware;
    PnpCallbacks.EvtDeviceReleaseHardware = TrEvtReleaseHardware;
    PnpCallbacks.EvtDeviceD0Entry = TrEvtD0Entry;
    PnpCallbacks.EvtDeviceD0Exit = TrEvtD0Exit;
    WDFCX_CALL(WdfDeviceInitSetPnpPowerEventCallbacksTableIndex, PFN_WDFDEVICEINITSETPNPPOWEREVENTCALLBACKS)(
        ClientGlobals, DeviceInit, &PnpCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, TR_MASTER_CONTEXT);
    Attributes.EvtCleanupCallback = TrEvtMasterCleanup;
    Status = WDFCX_CALL(WdfDeviceCreateTableIndex, PFN_WDFDEVICECREATE)(
        ClientGlobals, &DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Master = TrGetMasterContext(Device);
    Master->ClientGlobals = ClientGlobals;
    Master->Callbacks = *Callbacks;
    if ((Callbacks->Flags & TR_DEVICE_SERIALIZE_MASK) == TR_DEVICE_SERIALIZE_ALL_REQUESTS)
    {
        Status = TrCreateSerialQueue(&Master->Queue);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
    QueueConfig.EvtIoDefault = TrEvtMasterIoDefault;
    Status = WDFCX_CALL(WdfIoQueueCreateTableIndex, PFN_WDFIOQUEUECREATE)(
        ClientGlobals, Device, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Callbacks->EvtTrCreateSecureDeviceContext != NULL)
    {
        Status = Callbacks->EvtTrCreateSecureDeviceContext(Device);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Master->ContextCreated = TRUE;
    if (MasterDevice != NULL)
        *MasterDevice = Device;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TrClxHandoffServiceDeviceControl(
    _In_ WDFOBJECT BindContextObject,
    _Inout_ PWDFDEVICE_INIT DeviceInit,
    _In_ LPGUID ServiceGuid,
    _In_opt_ PTR_SECURE_SERVICE_CALLBACKS Callbacks,
    _Out_opt_ WDFDEVICE *ServiceDevice)
{
    UNREFERENCED_PARAMETER(BindContextObject);
    UNREFERENCED_PARAMETER(DeviceInit);
    UNREFERENCED_PARAMETER(ServiceGuid);
    UNREFERENCED_PARAMETER(Callbacks);

    if (ServiceDevice != NULL)
        *ServiceDevice = NULL;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
TrClxLogMessage(
    _In_ WDFOBJECT BindContext,
    _In_ WDFDEVICE Device,
    _In_ ULONG Severity,
    _In_ PCSTR Message,
    _In_ va_list Arguments)
{
    ULONG Level;

    UNREFERENCED_PARAMETER(BindContext);
    UNREFERENCED_PARAMETER(Device);

    if (Message == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (Severity)
    {
        case STATUS_SEVERITY_ERROR:
            Level = DPFLTR_ERROR_LEVEL;
            break;
        case STATUS_SEVERITY_WARNING:
            Level = DPFLTR_WARNING_LEVEL;
            break;
        case STATUS_SEVERITY_INFORMATIONAL:
            Level = DPFLTR_INFO_LEVEL;
            break;
        default:
            Level = DPFLTR_TRACE_LEVEL;
            break;
    }

    vDbgPrintExWithPrefix("TrEE: ", DPFLTR_IHVDRIVER_ID, Level, Message, Arguments);
    return STATUS_SUCCESS;
}

static
NTSTATUS
TrCallOsService(
    _In_ LPCGUID Guid,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID Input,
    _In_ ULONG InputLength,
    _Out_ PVOID Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    UNICODE_STRING Name;
    IO_STATUS_BLOCK IoStatus;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    PWSTR List = NULL;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    *Information = 0;
    Status = IoGetDeviceInterfaces(Guid, NULL, 0, &List);
    if (!NT_SUCCESS(Status))
        return Status;

    if (List == NULL || List[0] == UNICODE_NULL)
    {
        if (List != NULL)
            ExFreePool(List);
        return STATUS_NOT_FOUND;
    }

    RtlInitUnicodeString(&Name, List);
    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA | FILE_WRITE_DATA, &FileObject, &DeviceObject);
    ExFreePool(List);
    if (!NT_SUCCESS(Status))
        return Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        DeviceObject,
                                        Input,
                                        InputLength,
                                        Output,
                                        OutputLength,
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL)
    {
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoGetNextIrpStackLocation(Irp)->FileObject = FileObject;
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (NT_SUCCESS(Status))
        *Information = IoStatus.Information;
    ObDereferenceObject(FileObject);
    return Status;
}

static
NTSTATUS
NTAPI
TrClxQueryOSService(
    _In_ WDFOBJECT BindContext,
    _In_ WDFDEVICE Device,
    _In_ LPCGUID OSServiceGuid,
    _Out_ PTR_SERVICE_INFORMATION Information)
{
    ULONG_PTR Bytes;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(BindContext);
    UNREFERENCED_PARAMETER(Device);

    if (OSServiceGuid == NULL || Information == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = TrCallOsService(OSServiceGuid, IOCTL_TR_SERVICE_QUERY, NULL, 0, Information, sizeof(*Information), &Bytes);
    if (NT_SUCCESS(Status) && Bytes < sizeof(*Information))
        Status = STATUS_INVALID_DEVICE_REQUEST;
    return Status;
}

static
NTSTATUS
NTAPI
TrClxCallOSService(
    _In_ WDFOBJECT BindContext,
    _In_ WDFDEVICE Device,
    _In_ LPCGUID OSServiceGuid,
    _In_ PTR_SERVICE_REQUEST CallData,
    _Out_opt_ ULONG_PTR *BytesWritten)
{
    TR_SERVICE_REQUEST_RESPONSE Response;
    ULONG_PTR Bytes;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(BindContext);
    UNREFERENCED_PARAMETER(Device);

    if (BytesWritten != NULL)
        *BytesWritten = 0;

    if (OSServiceGuid == NULL || CallData == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Response, sizeof(Response));
    Status = TrCallOsService(OSServiceGuid,
                             IOCTL_TR_EXECUTE_FUNCTION,
                             CallData,
                             sizeof(*CallData),
                             &Response,
                             sizeof(Response),
                             &Bytes);
    if (NT_SUCCESS(Status) && Bytes >= sizeof(Response) && BytesWritten != NULL)
        *BytesWritten = (ULONG_PTR)Response.BytesWritten;
    return Status;
}

static
VOID
TrSetDone(
    _In_ PTR_REQUEST_CONTEXT Context)
{
    Context->State = TrRequestDone;
    if (Context->Listed)
    {
        RemoveEntryList(&Context->Entry);
        Context->Listed = FALSE;
    }
}

static
VOID
TrFinishRequest(
    _In_ WDFREQUEST Request,
    _In_ PTR_REQUEST_CONTEXT Context)
{
    PTR_SERVICE Service = Context->Service;
    NTSTATUS Status = Context->Result;

    if (Status == STATUS_PENDING)
        Status = STATUS_UNSUCCESSFUL;

    if (NT_SUCCESS(Status) || Context->Asynchronous)
    {
        Context->Response->InterfaceVersion = TRUSTED_RUNTIME_INTERFACE_VERSION;
        Context->Response->BytesWritten = Context->BytesWritten;
        WdfRequestCompleteWithInformation(Request, Status, sizeof(TR_SERVICE_REQUEST_RESPONSE));
    }
    else
    {
        WdfRequestComplete(Request, Status);
    }

    ExReleaseRundownProtection(&Service->Rundown);
    TrDereferenceService(Service);
}

static
VOID
NTAPI
TrCancelWorker(
    _In_ PVOID Parameter)
{
    WDFREQUEST Request = Parameter;
    PTR_REQUEST_CONTEXT Context = TrGetRequestContext(Request);
    PTR_SERVICE Service = Context->Service;
    BOOLEAN Finish;
    KIRQL Irql;

    if (Service->Callbacks->EvtTrCancelSecureServiceRequest != NULL)
    {
        Service->Callbacks->EvtTrCancelSecureServiceRequest(Service->Device,
                                                            Context->Session,
                                                            Request,
                                                            &Context->MiniportContext);
    }

    KeAcquireSpinLock(&Service->RequestLock, &Irql);
    Finish = Context->ResultSet;
    if (Finish)
        TrSetDone(Context);
    else
        Context->State = TrRequestCancelled;
    KeReleaseSpinLock(&Service->RequestLock, Irql);

    if (Finish)
        TrFinishRequest(Request, Context);
}

static
VOID
TrCancelOrFinish(
    _In_ WDFREQUEST Request)
{
    PTR_REQUEST_CONTEXT Context = TrGetRequestContext(Request);
    PTR_SERVICE Service = Context->Service;
    KIRQL Irql;

    KeAcquireSpinLock(&Service->RequestLock, &Irql);
    if (Context->State == TrRequestDone ||
        Context->State == TrRequestCancelling ||
        Context->State == TrRequestCancelled)
    {
        KeReleaseSpinLock(&Service->RequestLock, Irql);
        return;
    }

    if (Context->ResultSet)
    {
        TrSetDone(Context);
        KeReleaseSpinLock(&Service->RequestLock, Irql);
        TrFinishRequest(Request, Context);
        return;
    }

    Context->State = TrRequestCancelling;
    KeReleaseSpinLock(&Service->RequestLock, Irql);

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        ExInitializeWorkItem(&Context->WorkItem, TrCancelWorker, Request);
        ExQueueWorkItem(&Context->WorkItem, DelayedWorkQueue);
        return;
    }

    TrCancelWorker(Request);
}

static
VOID
NTAPI
TrEvtRequestCancel(
    _In_ WDFREQUEST Request)
{
    PTR_REQUEST_CONTEXT Context = TrGetRequestContext(Request);
    PTR_SERVICE Service = Context->Service;
    KIRQL Irql;

    KeAcquireSpinLock(&Service->RequestLock, &Irql);
    if (Context->State == TrRequestUnmarking && !Context->FinishOnCancel)
    {
        Context->CancelArrived = TRUE;
        KeReleaseSpinLock(&Service->RequestLock, Irql);
        return;
    }
    KeReleaseSpinLock(&Service->RequestLock, Irql);

    TrCancelOrFinish(Request);
}

static
VOID
TrTakeFromCancel(
    _In_ WDFREQUEST Request)
{
    PTR_REQUEST_CONTEXT Context = TrGetRequestContext(Request);
    PTR_SERVICE Service = Context->Service;
    KIRQL Irql;

    if (WdfRequestUnmarkCancelable(Request) == STATUS_CANCELLED)
    {
        KeAcquireSpinLock(&Service->RequestLock, &Irql);
        if (!Context->CancelArrived)
        {
            Context->FinishOnCancel = TRUE;
            KeReleaseSpinLock(&Service->RequestLock, Irql);
            return;
        }
        KeReleaseSpinLock(&Service->RequestLock, Irql);
    }

    TrCancelOrFinish(Request);
}

static
VOID
TrCancelPendingRequests(
    _In_ PTR_SERVICE Service)
{
    PTR_REQUEST_CONTEXT Context;
    WDFREQUEST Request;
    PLIST_ENTRY Entry;
    KIRQL Irql;

    for (;;)
    {
        Request = NULL;
        KeAcquireSpinLock(&Service->RequestLock, &Irql);
        for (Entry = Service->Pending.Flink; Entry != &Service->Pending; Entry = Entry->Flink)
        {
            Context = CONTAINING_RECORD(Entry, TR_REQUEST_CONTEXT, Entry);
            if (Context->State == TrRequestPending)
            {
                Context->State = TrRequestUnmarking;
                Request = Context->Request;
                WdfObjectReference(Request);
                break;
            }
        }
        KeReleaseSpinLock(&Service->RequestLock, Irql);

        if (Request == NULL)
            break;

        TrTakeFromCancel(Request);
        WdfObjectDereference(Request);
    }
}

static
NTSTATUS
NTAPI
TrClxCompleteAsyncRequest(
    _In_ WDFOBJECT BindContext,
    _In_ PVOID RequestHandle,
    _In_ NTSTATUS Result,
    _In_ ULONG_PTR BytesWritten)
{
    WDFREQUEST Request = (WDFREQUEST)RequestHandle;
    PTR_REQUEST_CONTEXT Context;
    TR_REQUEST_STATE State;
    PTR_SERVICE Service;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(BindContext);

    if (Request == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = TrGetRequestContext(Request);
    Service = Context->Service;
    if (Service == NULL)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&Service->RequestLock, &Irql);
    State = Context->State;
    if (State == TrRequestIdle || State == TrRequestDone || Context->ResultSet)
    {
        KeReleaseSpinLock(&Service->RequestLock, Irql);
        return STATUS_INVALID_DEVICE_STATE;
    }

    Context->Result = Result;
    Context->BytesWritten = BytesWritten;
    Context->ResultSet = TRUE;
    Context->Asynchronous = TRUE;
    if (State == TrRequestPending)
        Context->State = TrRequestUnmarking;
    else if (State == TrRequestCancelled)
        TrSetDone(Context);
    KeReleaseSpinLock(&Service->RequestLock, Irql);

    if (State == TrRequestPending)
        TrTakeFromCancel(Request);
    else if (State == TrRequestCancelled)
        TrFinishRequest(Request, Context);

    return STATUS_SUCCESS;
}

static
VOID
NTAPI
TrProcessCallout(
    _In_ PVOID Parameter)
{
    PTR_CALLOUT Callout = Parameter;
    PTR_REQUEST_CONTEXT Context = Callout->Context;
    PTR_SERVICE Service = Callout->Service;

    Callout->Status = Service->Callbacks->EvtTrProcessSecureServiceRequest(Service->Device,
                                                                           Context->Session,
                                                                           Callout->Request,
                                                                           Callout->Priority,
                                                                           &Context->ServiceRequest,
                                                                           Context->Flags,
                                                                           &Callout->BytesWritten,
                                                                           &Context->MiniportContext);
}

static
BOOLEAN
TrPrepareExecute(
    _In_ WDFREQUEST Request)
{
    PTR_REQUEST_CONTEXT Context = TrGetRequestContext(Request);
    PTR_SERVICE_REQUEST Input;
    WDFMEMORY Memory;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(TR_SERVICE_REQUEST), (PVOID *)&Input, NULL);
    if (NT_SUCCESS(Status))
    {
        Status = WdfRequestRetrieveOutputBuffer(Request,
                                                sizeof(TR_SERVICE_REQUEST_RESPONSE),
                                                (PVOID *)&Context->Response,
                                                NULL);
    }

    if (!NT_SUCCESS(Status))
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return FALSE;
    }

    Context->ServiceRequest = *Input;
    Context->Priority = KeQueryPriorityThread(KeGetCurrentThread());
    Context->Flags = 0;
    if (WdfRequestGetRequestorMode(Request) != UserMode)
    {
        Context->Prepared = TRUE;
        return TRUE;
    }

    Context->Flags = TR_SERVICE_REQUEST_FROM_USERMODE;
    if ((Context->ServiceRequest.InputBufferSize != 0 && Context->ServiceRequest.InputBuffer == NULL) ||
        (Context->ServiceRequest.OutputBufferSize != 0 && Context->ServiceRequest.OutputBuffer == NULL))
    {
        WdfRequestComplete(Request, STATUS_ACCESS_VIOLATION);
        return FALSE;
    }

    if (Context->ServiceRequest.InputBufferSize != 0)
    {
        Status = WdfRequestProbeAndLockUserBufferForRead(Request,
                                                         (PVOID)(ULONG_PTR)Context->ServiceRequest.InputBuffer,
                                                         (size_t)Context->ServiceRequest.InputBufferSize,
                                                         &Memory);
        if (!NT_SUCCESS(Status))
        {
            WdfRequestComplete(Request, Status);
            return FALSE;
        }

        Context->ServiceRequest.InputBuffer = WdfMemoryGetBuffer(Memory, NULL);
    }

    if (Context->ServiceRequest.OutputBufferSize != 0)
    {
        Status = WdfRequestProbeAndLockUserBufferForWrite(Request,
                                                          (PVOID)(ULONG_PTR)Context->ServiceRequest.OutputBuffer,
                                                          (size_t)Context->ServiceRequest.OutputBufferSize,
                                                          &Memory);
        if (!NT_SUCCESS(Status))
        {
            WdfRequestComplete(Request, Status);
            return FALSE;
        }

        Context->ServiceRequest.OutputBuffer = WdfMemoryGetBuffer(Memory, NULL);
    }

    Context->Prepared = TRUE;
    return TRUE;
}

static
VOID
TrExecute(
    _In_ WDFREQUEST Request,
    _In_ PTR_FILE_CONTEXT File)
{
    PTR_REQUEST_CONTEXT Context = TrGetRequestContext(Request);
    PTR_SERVICE Service = File->Service;
    PTR_MASTER_CONTEXT Master;
    BOOLEAN Finish = FALSE, Take = FALSE, Cancel = FALSE;
    TR_CALLOUT Callout;
    NTSTATUS Status;
    ULONG Reserve;
    KIRQL Irql;

    if (!Context->Prepared)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    if (!ExAcquireRundownProtection(&Service->Rundown))
    {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;
    }

    if (!Service->Created)
    {
        ExReleaseRundownProtection(&Service->Rundown);
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;
    }

    if (Service->Callbacks->EvtTrProcessSecureServiceRequest == NULL)
    {
        ExReleaseRundownProtection(&Service->Rundown);
        WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
        return;
    }

    Master = TrGetMasterContext(Service->Master);
    switch (Master->Callbacks.Flags & TR_DEVICE_STACK_RESERVE_MASK)
    {
        case TR_DEVICE_STACK_RESERVE_2K:
            Reserve = 2048;
            break;
        case TR_DEVICE_STACK_RESERVE_4K:
            Reserve = 4096;
            break;
        case TR_DEVICE_STACK_RESERVE_8K:
            Reserve = 8192;
            break;
        default:
            Reserve = 0;
            break;
    }

    WdfObjectReference(Request);
    TrReferenceService(Service);
    TrReferenceService(Service);
    Context->Request = Request;
    Context->Service = Service;
    Context->Session = File->Session;
    Context->State = TrRequestProcessing;

    Callout.Service = Service;
    Callout.Request = Request;
    Callout.Context = Context;
    Callout.Priority = Context->Priority;
    Callout.BytesWritten = 0;
    Callout.Status = STATUS_UNSUCCESSFUL;

    if (Reserve == 0 || !NT_SUCCESS(KeExpandKernelStackAndCallout(TrProcessCallout, &Callout, Reserve)))
        TrProcessCallout(&Callout);

    Status = Callout.Status;
    KeAcquireSpinLock(&Service->RequestLock, &Irql);
    if (Status != STATUS_PENDING)
    {
        Context->Result = Status;
        Context->BytesWritten = Callout.BytesWritten;
        Context->ResultSet = TRUE;
        Context->Asynchronous = FALSE;
        TrSetDone(Context);
        Finish = TRUE;
    }
    else if (Context->ResultSet)
    {
        TrSetDone(Context);
        Finish = TRUE;
    }
    else
    {
        Context->State = TrRequestPendingUnmarked;
        InsertTailList(&Service->Pending, &Context->Entry);
        Context->Listed = TRUE;
    }
    KeReleaseSpinLock(&Service->RequestLock, Irql);

    if (Finish)
    {
        TrFinishRequest(Request, Context);
        WdfObjectDereference(Request);
        TrDereferenceService(Service);
        return;
    }

    Status = WdfRequestMarkCancelableEx(Request, TrEvtRequestCancel);
    KeAcquireSpinLock(&Service->RequestLock, &Irql);
    if (Context->State == TrRequestPendingUnmarked)
    {
        if (!NT_SUCCESS(Status))
        {
            Cancel = TRUE;
        }
        else if (Context->ResultSet || Service->Removed)
        {
            Context->State = TrRequestUnmarking;
            Take = TRUE;
        }
        else
        {
            Context->State = TrRequestPending;
        }
    }
    KeReleaseSpinLock(&Service->RequestLock, Irql);

    if (Cancel)
        TrCancelOrFinish(Request);
    else if (Take)
        TrTakeFromCancel(Request);

    WdfObjectDereference(Request);
    TrDereferenceService(Service);
}

static
VOID
TrRootEnumerate(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength)
{
    ULONG_PTR Information = 0;
    PLIST_ENTRY Entry;
    ULONG Count = 0, Index = 0;
    LPGUID Output;
    NTSTATUS Status;

    ExAcquireFastMutex(&TrLock);
    for (Entry = TrServices.Flink; Entry != &TrServices; Entry = Entry->Flink)
        Count++;

    if (Count == 0)
    {
        Status = STATUS_SUCCESS;
    }
    else if (OutputBufferLength < Count * sizeof(GUID))
    {
        Status = STATUS_BUFFER_OVERFLOW;
        if (OutputBufferLength == 0)
            Information = Count * sizeof(GUID);
    }
    else
    {
        Status = WdfRequestRetrieveOutputBuffer(Request, Count * sizeof(GUID), (PVOID *)&Output, NULL);
        if (NT_SUCCESS(Status))
        {
            for (Entry = TrServices.Flink; Entry != &TrServices; Entry = Entry->Flink)
                Output[Index++] = CONTAINING_RECORD(Entry, TR_SERVICE, Entry)->Guid;
            Information = Count * sizeof(GUID);
        }
    }
    ExReleaseFastMutex(&TrLock);

    WdfRequestCompleteWithInformation(Request, Status, Information);
}

static
VOID
NTAPI
TrEvtSerializedIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(IoControlCode);

    TrExecute(Request, TrGetFileContext(WdfRequestGetFileObject(Request)));
}

static
VOID
TrDispatchExecute(
    _In_ WDFREQUEST Request,
    _In_ PTR_FILE_CONTEXT File)
{
    PTR_SERVICE Service = File->Service;
    WDFQUEUE Queue = NULL;
    NTSTATUS Status;

    if (ExAcquireRundownProtection(&Service->Rundown))
    {
        if (File->Queue != NULL)
            Queue = File->Queue;
        else if (Service->Queue != NULL)
            Queue = Service->Queue;
        else if (Service->Created)
            Queue = TrGetMasterContext(Service->Master)->Queue;

        if (Queue != NULL)
        {
            Status = WdfRequestForwardToIoQueue(Request, Queue);
            ExReleaseRundownProtection(&Service->Rundown);
            if (!NT_SUCCESS(Status))
                WdfRequestComplete(Request, Status);
            return;
        }

        ExReleaseRundownProtection(&Service->Rundown);
    }

    TrExecute(Request, File);
}

static
VOID
NTAPI
TrEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PTR_SERVICE_INFORMATION Information;
    PTR_FILE_CONTEXT File;
    PTR_SERVICE Service;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(InputBufferLength);

    if (WdfRequestGetFileObject(Request) == NULL)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    File = TrGetFileContext(WdfRequestGetFileObject(Request));
    Service = File->Service;
    if (Service == NULL)
    {
        if (IoControlCode == IOCTL_TR_ENUMERATE_SERVICES)
            TrRootEnumerate(Request, OutputBufferLength);
        else
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    switch (IoControlCode)
    {
        case IOCTL_TR_SERVICE_QUERY:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Information), (PVOID *)&Information, NULL);
            if (!NT_SUCCESS(Status))
            {
                WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
                return;
            }

            Information->InterfaceVersion = TRUSTED_RUNTIME_INTERFACE_VERSION;
            Information->ServiceMajorVersion = Service->MajorVersion;
            Information->ServiceMinorVersion = Service->MinorVersion;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(*Information));
            return;

        case IOCTL_TR_EXECUTE_FUNCTION:
            TrDispatchExecute(Request, File);
            return;

        default:
            if (!ExAcquireRundownProtection(&Service->Rundown))
            {
                WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
                return;
            }

            if (!Service->Created || Service->Callbacks->EvtTrProcessOtherSecureServiceIo == NULL)
                WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
            else
                Service->Callbacks->EvtTrProcessOtherSecureServiceIo(Service->Device, File->Session, Request);
            ExReleaseRundownProtection(&Service->Rundown);
            return;
    }
}

static
VOID
NTAPI
TrEvtIoInCallerContext(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request)
{
    WDF_REQUEST_PARAMETERS Parameters;
    WDFFILEOBJECT FileObject = WdfRequestGetFileObject(Request);
    NTSTATUS Status;

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);
    if (Parameters.Type == WdfRequestTypeDeviceControl &&
        Parameters.Parameters.DeviceIoControl.IoControlCode == IOCTL_TR_EXECUTE_FUNCTION &&
        FileObject != NULL &&
        TrGetFileContext(FileObject)->Service != NULL &&
        !TrPrepareExecute(Request))
    {
        return;
    }

    Status = WdfDeviceEnqueueRequest(Device, Request);
    if (!NT_SUCCESS(Status))
        WdfRequestComplete(Request, Status);
}

static
VOID
NTAPI
TrEvtFileCreate(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject)
{
    PTR_FILE_CONTEXT File = TrGetFileContext(FileObject);
    PUNICODE_STRING Name = WdfFileObjectGetFileName(FileObject);
    UNICODE_STRING GuidString;
    PTR_SERVICE Service = NULL;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    GUID Guid;

    UNREFERENCED_PARAMETER(Device);

    File->Queue = NULL;
    File->Service = NULL;
    File->Session = NULL;
    File->Listed = FALSE;
    RtlInitEmptyUnicodeString(&GuidString, NULL, 0);
    if (Name != NULL)
        GuidString = *Name;
    while (GuidString.Length != 0 && GuidString.Buffer[0] == L'\\')
    {
        GuidString.Buffer++;
        GuidString.Length -= sizeof(WCHAR);
        GuidString.MaximumLength -= sizeof(WCHAR);
    }

    if (GuidString.Length != TR_GUID_STRING_LENGTH * sizeof(WCHAR) ||
        !NT_SUCCESS(RtlGUIDFromString(&GuidString, &Guid)))
    {
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    ExAcquireFastMutex(&TrLock);
    for (Entry = TrServices.Flink; Entry != &TrServices; Entry = Entry->Flink)
    {
        if (IsEqualGUID(&CONTAINING_RECORD(Entry, TR_SERVICE, Entry)->Guid, &Guid))
        {
            Service = CONTAINING_RECORD(Entry, TR_SERVICE, Entry);
            TrReferenceService(Service);
            break;
        }
    }
    ExReleaseFastMutex(&TrLock);

    if (Service == NULL)
    {
        WdfRequestComplete(Request, STATUS_NOT_FOUND);
        return;
    }

    TrAcquire(&Service->Lock);
    if (Service->Removed || !Service->Created)
        Status = STATUS_DEVICE_NOT_READY;
    else if ((TrGetMasterContext(Service->Master)->Callbacks.Flags & TR_DEVICE_SERIALIZE_MASK) ==
             TR_DEVICE_SERIALIZE_PER_SESSION)
        Status = TrCreateSerialQueue(&File->Queue);
    else
        Status = STATUS_SUCCESS;

    if (NT_SUCCESS(Status) && Service->Callbacks->EvtTrCreateSecureSessionContext != NULL)
    {
        Status = Service->Callbacks->EvtTrCreateSecureSessionContext(Service->Device, &File->Session);
        if (!NT_SUCCESS(Status))
            TrDeleteSerialQueue(&File->Queue);
    }

    if (NT_SUCCESS(Status))
    {
        File->Service = Service;
        File->Listed = TRUE;
        InsertTailList(&Service->Files, &File->Entry);
    }
    TrRelease(&Service->Lock);

    if (!NT_SUCCESS(Status))
    {
        File->Session = NULL;
        TrDereferenceService(Service);
    }

    WdfRequestComplete(Request, Status);
}

static
VOID
NTAPI
TrEvtFileClose(
    _In_ WDFFILEOBJECT FileObject)
{
    PTR_FILE_CONTEXT File = TrGetFileContext(FileObject);
    PTR_SERVICE Service = File->Service;

    if (Service == NULL)
        return;

    TrAcquire(&Service->Lock);
    if (File->Listed)
    {
        RemoveEntryList(&File->Entry);
        File->Listed = FALSE;
        if (Service->Callbacks->EvtTrDestroySecureSessionContext != NULL)
            Service->Callbacks->EvtTrDestroySecureSessionContext(Service->Device, &File->Session);
    }
    TrRelease(&Service->Lock);

    TrDeleteSerialQueue(&File->Queue);
    File->Service = NULL;
    File->Session = NULL;
    TrDereferenceService(Service);
}

TRFUNC TrFunctions[TrFunctionTableNumEntries] =
{
    (TRFUNC)TrClxHandoffMasterDeviceControl,
    (TRFUNC)TrClxHandoffServiceDeviceControl,
    (TRFUNC)TrClxLogMessage,
    (TRFUNC)TrClxQueryOSService,
    (TRFUNC)TrClxCallOSService,
    (TRFUNC)TrClxCompleteAsyncRequest
};

static
NTSTATUS
NTAPI
TrLibraryBindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    return WdfCxBindClient(ClassBindInfo,
                           ClientGlobals,
                           (PVOID const *)TrFunctions,
                           RTL_NUMBER_OF(TrFunctions),
                           1);
}

static
VOID
NTAPI
TrLibraryUnbindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    WdfCxUnbindClient(ClassBindInfo);
}

static WDF_CLASS_LIBRARY_INFO TrLibraryInfo =
{
    sizeof(WDF_CLASS_LIBRARY_INFO),
    {1, 0, 0},
    NULL,
    NULL,
    TrLibraryBindClient,
    TrLibraryUnbindClient
};

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    DECLARE_CONST_UNICODE_STRING(Sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    DECLARE_CONST_UNICODE_STRING(DeviceName, WTR_DEVICE_NAME);
    DECLARE_CONST_UNICODE_STRING(LinkName, WTR_DEVICE_NAME_USER_LINK);
    WDF_OBJECT_ATTRIBUTES FileAttributes, RequestAttributes;
    WDF_FILEOBJECT_CONFIG FileConfig;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    WDF_DRIVER_CONFIG DriverConfig;
    PWDFDEVICE_INIT DeviceInit;
    UNICODE_STRING ObjectName;
    WDFDRIVER Driver;
    WDFDEVICE Device;
    NTSTATUS Status;

    TrDriverObject = DriverObject;
    ExInitializeFastMutex(&TrLock);
    InitializeListHead(&TrServices);

    WDF_DRIVER_CONFIG_INIT(&DriverConfig, NULL);
    DriverConfig.DriverInitFlags = WdfDriverInitNonPnpDriver;
    DriverConfig.EvtDriverUnload = WdfCxEvtDriverUnload;
    DriverConfig.DriverPoolTag = TR_TAG;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &DriverConfig, &Driver);
    if (!NT_SUCCESS(Status))
        return Status;

    DeviceInit = WdfControlDeviceInitAllocate(Driver, &Sddl);
    if (DeviceInit == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    WDF_FILEOBJECT_CONFIG_INIT(&FileConfig, TrEvtFileCreate, TrEvtFileClose, WDF_NO_EVENT_CALLBACK);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&FileAttributes, TR_FILE_CONTEXT);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &FileConfig, &FileAttributes);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&RequestAttributes, TR_REQUEST_CONTEXT);
    WdfDeviceInitSetRequestAttributes(DeviceInit, &RequestAttributes);
    WdfDeviceInitSetIoInCallerContextCallback(DeviceInit, TrEvtIoInCallerContext);

    Status = WdfDeviceInitAssignName(DeviceInit, &DeviceName);
    if (NT_SUCCESS(Status))
        Status = WdfDeviceCreate(&DeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &Device);

    if (!NT_SUCCESS(Status))
    {
        if (DeviceInit != NULL)
            WdfDeviceInitFree(DeviceInit);
        return Status;
    }

    Status = WdfDeviceCreateSymbolicLink(Device, &LinkName);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
    QueueConfig.PowerManaged = WdfFalse;
    QueueConfig.EvtIoDeviceControl = TrEvtIoDeviceControl;
    Status = WdfIoQueueCreate(Device, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(Status))
        return Status;

    TrControlDevice = Device;
    WdfControlFinishInitializing(Device);

    RtlInitUnicodeString(&ObjectName, WTR_DEVICE_NAME);
    return WdfRegisterClassLibrary(&TrLibraryInfo, RegistryPath, &ObjectName);
}
