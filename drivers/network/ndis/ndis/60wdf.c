/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60wdf.c
 * PURPOSE:     Private NDIS/WDF class-extension bridge used by NetAdapterCx.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_internal.h"

#define NDIS6_WDF_TAG 'fWNn'

#ifdef _WIN64
C_ASSERT(sizeof(NDIS6_WDF_ADD_DEVICE_INFO) == 32);
C_ASSERT(sizeof(NDIS6_WDF_COMPLETE_ADD_PARAMS) == 88);
C_ASSERT(sizeof(NDIS6_WDF_CX_CHARACTERISTICS) == 120);
#endif

enum
{
    NdisWdfActionPnpStart = 0,
    NdisWdfActionPnpQueryStop = 1,
    NdisWdfActionPnpCancelStop = 2,
    NdisWdfActionPnpStop = 3,
    NdisWdfActionPnpQueryRemove = 4,
    NdisWdfActionPnpCancelRemove = 5,
    NdisWdfActionPnpSurpriseRemove = 6,
    NdisWdfActionPnpRemove = 7,
    NdisWdfActionPowerD0Implicit = 8,
    NdisWdfActionPowerD0 = 9,
    NdisWdfActionPowerDx = 10,
    NdisWdfActionStartPowerManagement = 11,
    NdisWdfActionStopPowerManagement = 12,
    NdisWdfActionPowerDxFinal = 13,
    NdisWdfActionPowerDxOnSystemSx = 14,
    NdisWdfActionPowerDxOnSystemShutdown = 15,
    NdisWdfActionPowerNone = 16,
    NdisWdfActionPreReleaseHardware = 17,
    NdisWdfActionPostReleaseHardware = 18,
    NdisWdfActionPnpRebalance = 19,
    NdisWdfActionDeviceObjectCleanup = 20
};

static volatile LONG Ndis6WdfLuidIndex;

static NTSTATUS
Ndis6WdfCompleteIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static BOOLEAN
Ndis6WdfReferenceAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;
    BOOLEAN Referenced = FALSE;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return FALSE;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ext->IsWdfManaged)
        return FALSE;

    KeAcquireSpinLock(&Ext->WdfReferenceLock, &OldIrql);
    if (!Ext->WdfRemoving)
    {
        if (Ext->WdfReferenceCount++ == 0)
            KeClearEvent(&Ext->WdfReferenceDrainEvent);
        Referenced = TRUE;
    }
    KeReleaseSpinLock(&Ext->WdfReferenceLock, OldIrql);

    return Referenced;
}

static VOID
Ndis6WdfDereferenceAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ext->IsWdfManaged)
        return;

    KeAcquireSpinLock(&Ext->WdfReferenceLock, &OldIrql);
    ASSERT(Ext->WdfReferenceCount > 0);
    if (--Ext->WdfReferenceCount == 0)
        KeSetEvent(&Ext->WdfReferenceDrainEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Ext->WdfReferenceLock, OldIrql);
}

static VOID
Ndis6WdfWaitForReferences(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    KIRQL OldIrql;
    BOOLEAN Wait;

    KeAcquireSpinLock(&Ext->WdfReferenceLock, &OldIrql);
    Ext->WdfRemoving = TRUE;
    Wait = (Ext->WdfReferenceCount != 0);
    KeReleaseSpinLock(&Ext->WdfReferenceLock, OldIrql);

    if (Wait)
    {
        KeWaitForSingleObject(&Ext->WdfReferenceDrainEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }
}

/* Default execution-context tunables, matching NetAdapterCx's own
 * g_staticEcKnobs (netcx/ec/lib/executioncontext.cpp). */
static NDIS6_WDF_EC_RUNTIME_KNOBS Ndis6WdfEcKnobs = {
    sizeof(NDIS6_WDF_EC_RUNTIME_KNOBS),
    NDIS6_WDF_EC_FLAG_RUN_DPC_FOR_FIRST_LOOP,
    0,          /* MaxTimeAtDispatch */
    0,          /* DispatchTimeWarning */
    0,          /* DispatchTimeWarningInterval */
    80,         /* DpcWatchdogTimerThreshold */
    8,          /* WorkerThreadPriority */
    { 64, 64 }, /* MaxPacketsSend */
    { 64, 64 }, /* MaxPacketsSendComplete */
    { 64, 64 }, /* MaxPacketsReceive */
    { 64, 64 }  /* MaxPacketsReceiveComplete */
};

static VOID
Ndis6WdfBuildCompleteAddParameters(
    _In_ PLOGICAL_ADAPTER Adapter,
    _Out_ PNDIS6_WDF_COMPLETE_ADD_PARAMS Parameters)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);
    PUNICODE_STRING Name = &Adapter->NdisMiniportBlock.MiniportName;
    UNICODE_STRING GuidString;
    USHORT Index;

    RtlZeroMemory(Parameters, sizeof(*Parameters));

    /* The canonical miniport name is \Device\{xxxxxxxx-....}. Feed the
     * per-interface GUID back to NetAdapterCx when that form is present. */
    for (Index = 0; Index < Name->Length / sizeof(WCHAR); Index++)
    {
        if (Name->Buffer[Index] == L'{')
        {
            GuidString.Buffer = &Name->Buffer[Index];
            GuidString.Length = Name->Length - Index * sizeof(WCHAR);
            GuidString.MaximumLength = GuidString.Length;
            if (NT_SUCCESS(RtlGUIDFromString(&GuidString,
                                             &Parameters->InterfaceGuid)))
            {
                break;
            }
        }
    }

    Parameters->NetLuid.Info.IfType = 6; /* IF_TYPE_ETHERNET_CSMACD */
    Parameters->NetLuid.Info.NetLuidIndex =
        (ULONG64)(ULONG)InterlockedIncrement(&Ndis6WdfLuidIndex);
    Parameters->MediaType = NdisMedium802_3;
    Parameters->BaseName = *Name;
    Parameters->AdapterInstanceName = *Name;
    Parameters->DriverImageName = Ext->DriverBlock->DriverObject->DriverName;
    Parameters->ExecutionContextKnobs = &Ndis6WdfEcKnobs;
}

NTSTATUS
NTAPI
NdisWdfRegisterCx(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PVOID DriverContext,
    _In_ PNDIS6_WDF_CX_CHARACTERISTICS Characteristics,
    _Out_ PVOID* NdisWdfCxDriverHandle)
{
    PNDIS6_WDF_CX_DRIVER CxDriver;
    ULONG Size;

    UNREFERENCED_PARAMETER(RegistryPath);

    if (DriverObject == NULL || Characteristics == NULL ||
        NdisWdfCxDriverHandle == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Size = Characteristics->Header.Size;
    if (Size < sizeof(NDIS_OBJECT_HEADER) ||
        Size > sizeof(NDIS6_WDF_CX_CHARACTERISTICS))
    {
        return STATUS_INVALID_PARAMETER;
    }

    CxDriver = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(*CxDriver),
                                     NDIS6_WDF_TAG);
    if (CxDriver == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(CxDriver, sizeof(*CxDriver));
    CxDriver->Signature = NDIS6_WDF_CX_SIGNATURE;
    CxDriver->DriverObject = DriverObject;
    CxDriver->DriverContext = DriverContext;
    RtlCopyMemory(&CxDriver->Characteristics, Characteristics, Size);

    *NdisWdfCxDriverHandle = CxDriver;
    DbgPrint("NDIS6-WDF: registered NetAdapterCx callbacks (size=%lu)\n",
             Size);
    return STATUS_SUCCESS;
}

VOID
NTAPI
NdisWdfDeregisterCx(
    _In_ PVOID NdisWdfCxDriverHandle)
{
    PNDIS6_WDF_CX_DRIVER CxDriver = NdisWdfCxDriverHandle;

    if (CxDriver == NULL ||
        CxDriver->Signature != NDIS6_WDF_CX_SIGNATURE)
    {
        return;
    }

    InterlockedExchange(&CxDriver->Deregistering, TRUE);
    if (CxDriver->ClientCount != 0)
    {
        DbgPrint("NDIS6-WDF: refusing to free active Cx (%ld clients)\n",
                 CxDriver->ClientCount);
        return;
    }

    CxDriver->Signature = 0;
    ExFreePoolWithTag(CxDriver, NDIS6_WDF_TAG);
}

NTSTATUS
NTAPI
NdisWdfRegisterMiniportDriver(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PVOID NdisWdfCxDriverHandle,
    _In_opt_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics,
    _Out_ PNDIS_HANDLE NdisMiniportDriverHandle)
{
    PNDIS6_WDF_CX_DRIVER CxDriver = NdisWdfCxDriverHandle;

    if (CxDriver == NULL ||
        CxDriver->Signature != NDIS6_WDF_CX_SIGNATURE ||
        CxDriver->Deregistering)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return (NTSTATUS)Ndis6RegisterMiniportDriverInternal(
        DriverObject,
        RegistryPath,
        MiniportDriverContext,
        Characteristics,
        CxDriver,
        NdisMiniportDriverHandle);
}

NTSTATUS
NTAPI
NdisWdfPnPAddDevice(
    _In_ PNDIS6_WDF_ADD_DEVICE_INFO AddDeviceInfo,
    _Out_ PNDIS_HANDLE NdisMiniportAdapterHandle)
{
    PNDIS6_DRIVER_BLOCK DriverBlock;
    PLOGICAL_ADAPTER Adapter;
    PNDIS6_WDF_CX_DRIVER CxDriver;
    NDIS6_WDF_COMPLETE_ADD_PARAMS CompleteAdd;
    NDIS_STATUS Status;

    if (AddDeviceInfo == NULL || NdisMiniportAdapterHandle == NULL ||
        AddDeviceInfo->DriverObject == NULL ||
        AddDeviceInfo->PhysicalDeviceObject == NULL ||
        AddDeviceInfo->MiniportAdapterContext == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DriverBlock = Ndis6FindDriverBlock(AddDeviceInfo->DriverObject);
    if (DriverBlock == NULL || !DriverBlock->IsWdfManaged)
        return STATUS_DEVICE_NOT_READY;

    Status = Ndis6CreateWdfLogicalAdapter(DriverBlock,
                                          AddDeviceInfo,
                                          &Adapter);
    if (!NT_SUCCESS(Status))
        return (NTSTATUS)Status;

    CxDriver = DriverBlock->WdfCxDriver;
    Ndis6WdfBuildCompleteAddParameters(Adapter, &CompleteAdd);
    if (CxDriver->Characteristics.EvtCxMiniportCompleteAdd)
    {
        CxDriver->Characteristics.EvtCxMiniportCompleteAdd(
            AddDeviceInfo->MiniportAdapterContext,
            &CompleteAdd);
    }

    *NdisMiniportAdapterHandle = Adapter;
    DbgPrint("NDIS6-WDF: added miniport %p for PDO %p, FDO %p\n",
             Adapter,
             AddDeviceInfo->PhysicalDeviceObject,
             Adapter->NdisMiniportBlock.DeviceObject);
    return STATUS_SUCCESS;
}

PVOID
NTAPI
NdisWdfGetAdapterContextFromAdapterHandle(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle)
{
    PLOGICAL_ADAPTER Adapter = NdisMiniportAdapterHandle;
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NULL;

    Ext = NDIS6_EXT(Adapter);
    return (Ext && Ext->IsWdfManaged)
        ? Ext->MiniportAdapterContext
        : NULL;
}

NTSTATUS
NTAPI
NdisWdfPnpPowerEventHandler(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ ULONG PnpAction,
    _In_ ULONG PowerAction)
{
    PLOGICAL_ADAPTER Adapter = NdisMiniportAdapterHandle;
    PNDIS6_ADAPTER_EXT Ext;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(PowerAction);

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    /* NetAdapterCx may report remove again from its destructor after NDIS
     * has already torn down the logical state. */
    if (!Adapter->IsNdis6)
        return (PnpAction == NdisWdfActionPnpRemove)
            ? STATUS_SUCCESS
            : STATUS_INVALID_DEVICE_STATE;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ext->IsWdfManaged)
        return STATUS_INVALID_DEVICE_STATE;

    switch (PnpAction)
    {
        case NdisWdfActionPnpStart:
            if (!Ext->Initialized)
            {
                Status = Ndis6CallMiniportInitializeEx(Adapter);
                if (!NT_SUCCESS(Status))
                    return (NTSTATUS)Status;

                Ext->Initialized = TRUE;
                if (Ext->DriverBlock->WdfCxDriver->Characteristics.EvtCxDeviceStartComplete)
                {
                    Ext->DriverBlock->WdfCxDriver->Characteristics.EvtCxDeviceStartComplete(
                        Ext->MiniportAdapterContext);
                }
            }
            return STATUS_SUCCESS;

        case NdisWdfActionPnpStop:
        case NdisWdfActionPowerDx:
        case NdisWdfActionPowerDxFinal:
        case NdisWdfActionPowerDxOnSystemSx:
        case NdisWdfActionPowerDxOnSystemShutdown:
            return (NTSTATUS)Ndis6CallMiniportPauseEx(Adapter);

        case NdisWdfActionPowerD0:
        case NdisWdfActionPowerD0Implicit:
            return (NTSTATUS)Ndis6CallMiniportRestartEx(Adapter);

        case NdisWdfActionPnpSurpriseRemove:
            (VOID)Ndis6CallMiniportPauseEx(Adapter);
            return STATUS_SUCCESS;

        case NdisWdfActionPnpRemove:
            Ndis6WdfWaitForReferences(Ext);
            if (Ext->Initialized)
            {
                Ndis6CallMiniportHaltEx(Adapter, NdisHaltDeviceRemoved);
                Ext->Initialized = FALSE;
            }
            Ndis6DestroyLogicalAdapter(Adapter);
            return STATUS_SUCCESS;

        case NdisWdfActionPnpQueryStop:
        case NdisWdfActionPnpCancelStop:
        case NdisWdfActionPnpQueryRemove:
        case NdisWdfActionPnpCancelRemove:
        case NdisWdfActionStartPowerManagement:
        case NdisWdfActionStopPowerManagement:
        case NdisWdfActionPreReleaseHardware:
        case NdisWdfActionPostReleaseHardware:
        case NdisWdfActionPnpRebalance:
        case NdisWdfActionDeviceObjectCleanup:
        case NdisWdfActionPowerNone:
        default:
            return STATUS_SUCCESS;
    }
}

VOID
NTAPI
NdisWdfMiniportStarted(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle)
{
    PLOGICAL_ADAPTER Adapter = NdisMiniportAdapterHandle;
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ext->IsWdfManaged || !Ext->Initialized)
        return;

    if (!Ext->WdfBindingsStarted)
    {
        extern VOID Ndis6BindAllProtocolsToAdapter(PLOGICAL_ADAPTER);
        extern VOID Ndis6AttachFiltersToAdapter(PLOGICAL_ADAPTER);

        Ndis6BindAllProtocolsToAdapter(Adapter);
        Ndis6AttachFiltersToAdapter(Adapter);
        Ext->WdfBindingsStarted = TRUE;
    }

    (VOID)Ndis6CallMiniportRestartEx(Adapter);
}

VOID
NTAPI
NdisWdfMiniportDataPathStart(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle)
{
    PLOGICAL_ADAPTER Adapter = NdisMiniportAdapterHandle;

    if (Adapter && Adapter->IsNdis6)
        (VOID)Ndis6CallMiniportRestartEx(Adapter);
}

VOID
NTAPI
NdisWdfMiniportSetPower(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ POWER_ACTION SystemPowerAction,
    _In_ DEVICE_POWER_STATE DevicePowerState)
{
    PLOGICAL_ADAPTER Adapter = NdisMiniportAdapterHandle;

    UNREFERENCED_PARAMETER(SystemPowerAction);

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    if (DevicePowerState == PowerDeviceD0)
        (VOID)Ndis6CallMiniportRestartEx(Adapter);
    else
        (VOID)Ndis6CallMiniportPauseEx(Adapter);
}

BOOLEAN
NTAPI
NdisWdfMiniportTryReference(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle)
{
    return Ndis6WdfReferenceAdapter(NdisMiniportAdapterHandle);
}

VOID
NTAPI
NdisWdfMiniportDereference(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle)
{
    Ndis6WdfDereferenceAdapter(NdisMiniportAdapterHandle);
}

VOID
NTAPI
NdisWdfAsyncPowerReferenceCompleteNotification(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ NTSTATUS Status)
{
    UNREFERENCED_PARAMETER(NdisMiniportAdapterHandle);
    UNREFERENCED_PARAMETER(Status);
}

VOID
NTAPI
NdisWdfNotifyWmiAdapterArrival(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle)
{
    UNREFERENCED_PARAMETER(NdisMiniportAdapterHandle);
}

NTSTATUS
NTAPI
NdisWdfCreateIrpHandler(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;

    if (!Ndis6WdfReferenceAdapter(NdisMiniportAdapterHandle))
        return Ndis6WdfCompleteIrp(Irp, STATUS_DELETE_PENDING);

    Stack = IoGetCurrentIrpStackLocation(Irp);
    if (Stack->FileObject == NULL)
    {
        Ndis6WdfDereferenceAdapter(NdisMiniportAdapterHandle);
        return Ndis6WdfCompleteIrp(Irp, STATUS_INVALID_PARAMETER);
    }

    Stack->FileObject->FsContext = NdisMiniportAdapterHandle;
    return Ndis6WdfCompleteIrp(Irp, STATUS_SUCCESS);
}

NTSTATUS
NTAPI
NdisWdfCloseIrpHandler(
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PLOGICAL_ADAPTER Adapter = NULL;

    if (Stack->FileObject)
    {
        Adapter = Stack->FileObject->FsContext;
        Stack->FileObject->FsContext = NULL;
    }

    if (Adapter)
        Ndis6WdfDereferenceAdapter(Adapter);

    return Ndis6WdfCompleteIrp(Irp, STATUS_SUCCESS);
}

NTSTATUS
NTAPI
NdisWdfDeviceControlIrpHandler(
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    if (Stack->DeviceObject &&
        Ndis6TryDispatchAdapterFdoIrp(Stack->DeviceObject, Irp, &Status))
    {
        return Status;
    }

    return Ndis6WdfCompleteIrp(Irp, STATUS_NOT_SUPPORTED);
}

NTSTATUS
NTAPI
NdisWdfDeviceInternalControlIrpHandler(
    _Inout_ PIRP Irp)
{
    return Ndis6WdfCompleteIrp(Irp, STATUS_NOT_SUPPORTED);
}

VOID
NTAPI
NdisWdfGetGuidToOidMap(
    _In_reads_(OidCount) PNDIS_OID OidList,
    _In_ USHORT OidCount,
    _Out_writes_opt_(*GuidCount) PNDIS_GUID GuidMap,
    _Inout_ PUSHORT GuidCount)
{
    USHORT Index;
    USHORT Capacity;

    if (GuidCount == NULL)
        return;

    Capacity = GuidMap ? *GuidCount : 0;
    *GuidCount = OidCount;

    if (GuidMap == NULL || OidList == NULL)
        return;

    for (Index = 0; Index < OidCount && Index < Capacity; Index++)
    {
        RtlZeroMemory(&GuidMap[Index], sizeof(GuidMap[Index]));
        GuidMap[Index].Guid.Data1 = OidList[Index];
        GuidMap[Index].Guid.Data2 = 0x4e44;
        GuidMap[Index].Guid.Data3 = 0x4953;
        GuidMap[Index].Guid.Data4[0] = 0x57;
        GuidMap[Index].Guid.Data4[1] = 0x44;
        GuidMap[Index].u.Oid = OidList[Index];
        GuidMap[Index].Size = sizeof(ULONG);
        GuidMap[Index].Flags = NDIS_GUID_TO_OID;
    }
}

NTSTATUS
NTAPI
NdisWdfQueryAllData(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ PNDIS_GUID NdisGuid,
    _In_ PGUID Guid,
    _Inout_updates_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG ReturnSize)
{
    UNREFERENCED_PARAMETER(NdisMiniportAdapterHandle);
    UNREFERENCED_PARAMETER(NdisGuid);
    UNREFERENCED_PARAMETER(Guid);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferSize);
    if (ReturnSize)
        *ReturnSize = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NdisWdfQuerySingleInstance(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ PNDIS_GUID NdisGuid,
    _Inout_ PVOID Wnode,
    _In_ ULONG BufferSize,
    _Out_ PULONG ReturnSize)
{
    UNREFERENCED_PARAMETER(NdisMiniportAdapterHandle);
    UNREFERENCED_PARAMETER(NdisGuid);
    UNREFERENCED_PARAMETER(Wnode);
    UNREFERENCED_PARAMETER(BufferSize);
    if (ReturnSize)
        *ReturnSize = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NdisWdfChangeSingleInstance(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ PNDIS_GUID NdisGuid,
    _Inout_ PVOID Wnode)
{
    UNREFERENCED_PARAMETER(NdisMiniportAdapterHandle);
    UNREFERENCED_PARAMETER(NdisGuid);
    UNREFERENCED_PARAMETER(Wnode);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NdisWdfExecuteMethod(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ PNDIS_GUID NdisGuid,
    _Inout_ PVOID Wnode,
    _In_ ULONG BufferSize,
    _Out_ PULONG ReturnSize)
{
    return NdisWdfQuerySingleInstance(NdisMiniportAdapterHandle,
                                      NdisGuid,
                                      Wnode,
                                      BufferSize,
                                      ReturnSize);
}

VOID
NTAPI
NdisWdfReadConfiguration(
    _Out_ PNDIS_STATUS Status,
    _Out_ PNDIS_CONFIGURATION_PARAMETER* ParameterValue,
    _In_ NDIS_HANDLE ConfigurationHandle,
    _In_ PNDIS_STRING Keyword,
    _In_ NDIS_PARAMETER_TYPE ParameterType)
{
    NdisReadConfiguration(Status,
                          ParameterValue,
                          ConfigurationHandle,
                          Keyword,
                          ParameterType);
}
