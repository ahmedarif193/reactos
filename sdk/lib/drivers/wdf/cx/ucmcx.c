/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB connector manager KMDF class extension
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "classlibrary.h"
#include <ucm/1.0/UcmCx.h>

#define UCMCX_MAX_PDOS 7

typedef struct _UCMCX_DEVICE_CONTEXT
{
    BOOLEAN Initialized;
    KSPIN_LOCK Lock;
    LIST_ENTRY Connectors;
} UCMCX_DEVICE_CONTEXT, *PUCMCX_DEVICE_CONTEXT;

typedef struct _UCMCX_CONNECTOR_CONTEXT
{
    LIST_ENTRY Entry;
    WDFDEVICE Device;
    UCMCONNECTOR Connector;
    ULONGLONG ConnectorId;
    KSPIN_LOCK Lock;
    BOOLEAN TypeCSupported;
    ULONG SupportedOperatingModes;
    ULONG SupportedPowerSourcingCapabilities;
    BOOLEAN AudioAccessoryCapable;
    PFN_UCM_CONNECTOR_SET_DATA_ROLE EvtSetDataRole;
    BOOLEAN PdSupported;
    ULONG SupportedPowerRoles;
    PFN_UCM_CONNECTOR_SET_POWER_ROLE EvtSetPowerRole;
    BOOLEAN Attached;
    UCM_TYPEC_PARTNER Partner;
    UCM_TYPEC_CURRENT CurrentAdvertisement;
    UCM_CHARGING_STATE ChargingState;
    UCM_DATA_ROLE DataRole;
    UCM_POWER_ROLE PowerRole;
    UCM_PD_CONN_STATE PdConnState;
    UCM_PD_REQUEST_DATA_OBJECT Rdo;
    UCHAR SourceCapsCount;
    UCM_PD_POWER_DATA_OBJECT SourceCaps[UCMCX_MAX_PDOS];
    UCHAR PartnerSourceCapsCount;
    UCM_PD_POWER_DATA_OBJECT PartnerSourceCaps[UCMCX_MAX_PDOS];
} UCMCX_CONNECTOR_CONTEXT, *PUCMCX_CONNECTOR_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UCMCX_DEVICE_CONTEXT, UcmCxGetDeviceContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UCMCX_CONNECTOR_CONTEXT, UcmCxGetConnectorContext)

static
VOID
UcmCxResetPartner(
    _Inout_ PUCMCX_CONNECTOR_CONTEXT Context)
{
    Context->Attached = FALSE;
    Context->Partner = UcmTypeCPartnerInvalid;
    Context->CurrentAdvertisement = UcmTypeCCurrentInvalid;
    Context->ChargingState = UcmChargingStateInvalid;
    Context->DataRole = UcmDataRoleInvalid;
    Context->PowerRole = UcmPowerRoleInvalid;
    Context->PdConnState = UcmPdConnStateInvalid;
    Context->Rdo.Ul = 0;
    Context->SourceCapsCount = 0;
    Context->PartnerSourceCapsCount = 0;
}

static
NTSTATUS
NTAPI
UcmCxInitializeDevice(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFDEVICE WdfDevice,
    _In_ PUCM_MANAGER_CONFIG Config)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    PUCMCX_DEVICE_CONTEXT Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (WdfDevice == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, UCMCX_DEVICE_CONTEXT);
    Status = WdfObjectAllocateContext(WdfDevice, &Attributes, (PVOID *)&Context);
    if (Status == STATUS_OBJECT_NAME_EXISTS)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    KeInitializeSpinLock(&Context->Lock);
    InitializeListHead(&Context->Connectors);
    Context->Initialized = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
UcmCxEvtConnectorCleanup(
    _In_ WDFOBJECT Object)
{
    PUCMCX_CONNECTOR_CONTEXT Context = UcmCxGetConnectorContext(Object);
    PUCMCX_DEVICE_CONTEXT DeviceContext;
    KIRQL Irql;

    if (Context->Device == NULL)
        return;

    DeviceContext = UcmCxGetDeviceContext(Context->Device);
    KeAcquireSpinLock(&DeviceContext->Lock, &Irql);
    RemoveEntryList(&Context->Entry);
    KeReleaseSpinLock(&DeviceContext->Lock, Irql);
}

static
UCMCONNECTOR
UcmCxFindConnector(
    _In_ PUCMCX_DEVICE_CONTEXT DeviceContext,
    _In_ ULONGLONG ConnectorId)
{
    PUCMCX_CONNECTOR_CONTEXT Context;
    PLIST_ENTRY Entry;

    for (Entry = DeviceContext->Connectors.Flink; Entry != &DeviceContext->Connectors; Entry = Entry->Flink)
    {
        Context = CONTAINING_RECORD(Entry, UCMCX_CONNECTOR_CONTEXT, Entry);
        if (Context->ConnectorId == ConnectorId)
            return Context->Connector;
    }

    return NULL;
}

static
NTSTATUS
NTAPI
UcmCxConnectorCreate(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFDEVICE WdfDevice,
    _In_ PUCM_CONNECTOR_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES Attributes,
    _Out_ UCMCONNECTOR *Connector)
{
    WDF_OBJECT_ATTRIBUTES ObjectAttributes;
    WDF_OBJECT_ATTRIBUTES ContextAttributes;
    PUCMCX_DEVICE_CONTEXT DeviceContext;
    PUCMCX_CONNECTOR_CONTEXT Context;
    PUCM_CONNECTOR_TYPEC_CONFIG TypeC;
    PUCM_CONNECTOR_PD_CONFIG Pd;
    UCMCONNECTOR Existing;
    WDFOBJECT Object;
    NTSTATUS Status;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (WdfDevice == NULL || Config == NULL || Connector == NULL)
        return STATUS_INVALID_PARAMETER;

    *Connector = NULL;
    DeviceContext = UcmCxGetDeviceContext(WdfDevice);
    if (DeviceContext == NULL || !DeviceContext->Initialized)
        return STATUS_INVALID_DEVICE_STATE;

    TypeC = Config->TypeCConfig;
    Pd = Config->PdConfig;
    if (TypeC == NULL || !TypeC->IsSupported)
        return STATUS_INVALID_PARAMETER;

    if ((TypeC->SupportedOperatingModes & UcmTypeCOperatingModeDrp) && TypeC->EvtSetDataRole == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Pd != NULL && Pd->Size != sizeof(UCM_CONNECTOR_PD_CONFIG))
        return STATUS_INFO_LENGTH_MISMATCH;

    KeAcquireSpinLock(&DeviceContext->Lock, &Irql);
    Existing = UcmCxFindConnector(DeviceContext, Config->ConnectorId);
    KeReleaseSpinLock(&DeviceContext->Lock, Irql);
    if (Existing != NULL)
    {
        *Connector = Existing;
        return STATUS_OBJECT_NAME_EXISTS;
    }

    if (Attributes != NULL)
        ObjectAttributes = *Attributes;
    else
        WDF_OBJECT_ATTRIBUTES_INIT(&ObjectAttributes);

    if (ObjectAttributes.ParentObject == NULL)
        ObjectAttributes.ParentObject = WdfDevice;

    Status = WdfObjectCreate(&ObjectAttributes, &Object);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&ContextAttributes, UCMCX_CONNECTOR_CONTEXT);
    ContextAttributes.EvtCleanupCallback = UcmCxEvtConnectorCleanup;
    Status = WdfObjectAllocateContext(Object, &ContextAttributes, (PVOID *)&Context);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Object);
        return Status;
    }

    Context->Connector = (UCMCONNECTOR)Object;
    Context->ConnectorId = Config->ConnectorId;
    KeInitializeSpinLock(&Context->Lock);
    UcmCxResetPartner(Context);
    Context->TypeCSupported = TRUE;
    Context->SupportedOperatingModes = TypeC->SupportedOperatingModes;
    Context->SupportedPowerSourcingCapabilities = TypeC->SupportedPowerSourcingCapabilities;
    Context->AudioAccessoryCapable = TypeC->AudioAccessoryCapable;
    Context->EvtSetDataRole = TypeC->EvtSetDataRole;
    if (Pd != NULL && Pd->IsSupported)
    {
        Context->PdSupported = TRUE;
        Context->SupportedPowerRoles = Pd->SupportedPowerRoles;
        Context->EvtSetPowerRole = Pd->EvtSetPowerRole;
    }

    KeAcquireSpinLock(&DeviceContext->Lock, &Irql);
    Existing = UcmCxFindConnector(DeviceContext, Config->ConnectorId);
    if (Existing == NULL)
    {
        InsertTailList(&DeviceContext->Connectors, &Context->Entry);
        Context->Device = WdfDevice;
    }
    KeReleaseSpinLock(&DeviceContext->Lock, Irql);

    if (Existing != NULL)
    {
        WdfObjectDelete(Object);
        *Connector = Existing;
        return STATUS_OBJECT_NAME_EXISTS;
    }

    *Connector = (UCMCONNECTOR)Object;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UcmCxConnectorTypeCAttach(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMCONNECTOR Connector,
    _In_ PUCM_CONNECTOR_TYPEC_ATTACH_PARAMS Params)
{
    PUCMCX_CONNECTOR_CONTEXT Context;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (Connector == NULL || Params == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = UcmCxGetConnectorContext(Connector);
    KeAcquireSpinLock(&Context->Lock, &Irql);
    Context->Attached = TRUE;
    Context->Partner = Params->Partner;
    Context->CurrentAdvertisement = Params->CurrentAdvertisement;
    Context->ChargingState = Params->ChargingState;
    if (Params->Partner == UcmTypeCPartnerDfp)
    {
        Context->DataRole = UcmDataRoleUfp;
        Context->PowerRole = UcmPowerRoleSink;
    }
    else
    {
        Context->DataRole = UcmDataRoleDfp;
        Context->PowerRole = UcmPowerRoleSource;
    }
    KeReleaseSpinLock(&Context->Lock, Irql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UcmCxConnectorTypeCDetach(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMCONNECTOR Connector)
{
    PUCMCX_CONNECTOR_CONTEXT Context;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (Connector == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = UcmCxGetConnectorContext(Connector);
    KeAcquireSpinLock(&Context->Lock, &Irql);
    UcmCxResetPartner(Context);
    KeReleaseSpinLock(&Context->Lock, Irql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UcmCxConnectorTypeCCurrentAdChanged(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMCONNECTOR Connector,
    _In_ UCM_TYPEC_CURRENT CurrentAdvertisement)
{
    PUCMCX_CONNECTOR_CONTEXT Context;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (Connector == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = UcmCxGetConnectorContext(Connector);
    KeAcquireSpinLock(&Context->Lock, &Irql);
    Context->CurrentAdvertisement = CurrentAdvertisement;
    KeReleaseSpinLock(&Context->Lock, Irql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
UcmCxStoreCaps(
    _In_ UCMCONNECTOR Connector,
    _In_reads_(PdoCount) UCM_PD_POWER_DATA_OBJECT Pdos[],
    _In_ UCHAR PdoCount,
    _In_ BOOLEAN Partner)
{
    PUCMCX_CONNECTOR_CONTEXT Context;
    UCHAR Stored = min(PdoCount, UCMCX_MAX_PDOS);
    KIRQL Irql;

    if (Connector == NULL || Pdos == NULL || PdoCount == 0)
        return STATUS_INVALID_PARAMETER;

    Context = UcmCxGetConnectorContext(Connector);
    KeAcquireSpinLock(&Context->Lock, &Irql);
    if (Partner)
    {
        RtlCopyMemory(Context->PartnerSourceCaps, Pdos, Stored * sizeof(Pdos[0]));
        Context->PartnerSourceCapsCount = PdoCount;
    }
    else
    {
        RtlCopyMemory(Context->SourceCaps, Pdos, Stored * sizeof(Pdos[0]));
        Context->SourceCapsCount = PdoCount;
    }
    KeReleaseSpinLock(&Context->Lock, Irql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UcmCxConnectorPdSourceCaps(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMCONNECTOR Connector,
    _In_reads_(PdoCount) UCM_PD_POWER_DATA_OBJECT Pdos[],
    _In_ UCHAR PdoCount)
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    return UcmCxStoreCaps(Connector, Pdos, PdoCount, FALSE);
}

static
NTSTATUS
NTAPI
UcmCxConnectorPdPartnerSourceCaps(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMCONNECTOR Connector,
    _In_reads_(PdoCount) UCM_PD_POWER_DATA_OBJECT Pdos[],
    _In_ UCHAR PdoCount)
{
    UNREFERENCED_PARAMETER(DriverGlobals);
    return UcmCxStoreCaps(Connector, Pdos, PdoCount, TRUE);
}

static
NTSTATUS
NTAPI
UcmCxConnectorPdConnectionStateChanged(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMCONNECTOR Connector,
    _In_ PUCM_CONNECTOR_PD_CONN_STATE_CHANGED_PARAMS Params)
{
    PUCMCX_CONNECTOR_CONTEXT Context;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Position, Count;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (Connector == NULL || Params == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = UcmCxGetConnectorContext(Connector);
    KeAcquireSpinLock(&Context->Lock, &Irql);
    if (Params->PdConnState == UcmPdConnStateNegotiationSucceeded)
    {
        Position = Params->Rdo.Common.ObjectPosition;
        Count = Context->PowerRole == UcmPowerRoleSink ? Context->PartnerSourceCapsCount : Context->SourceCapsCount;
        if (Position == 0 || Position > Count)
            Status = STATUS_NOT_FOUND;
    }

    if (NT_SUCCESS(Status))
    {
        Context->PdConnState = Params->PdConnState;
        Context->Rdo = Params->Rdo;
        Context->ChargingState = Params->ChargingState;
    }
    KeReleaseSpinLock(&Context->Lock, Irql);
    return Status;
}

static
NTSTATUS
NTAPI
UcmCxConnectorChargingStateChanged(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMCONNECTOR Connector,
    _In_ UCM_CHARGING_STATE ChargingState)
{
    PUCMCX_CONNECTOR_CONTEXT Context;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (Connector == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = UcmCxGetConnectorContext(Connector);
    KeAcquireSpinLock(&Context->Lock, &Irql);
    if (Context->PowerRole != UcmPowerRoleSink)
        Status = STATUS_INVALID_DEVICE_STATE;
    else if (ChargingState == UcmChargingStateInvalid)
        Status = STATUS_INVALID_PARAMETER;
    else
        Context->ChargingState = ChargingState;
    KeReleaseSpinLock(&Context->Lock, Irql);
    return Status;
}

static
VOID
NTAPI
UcmCxConnectorDataDirectionChanged(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMCONNECTOR Connector,
    _In_ BOOLEAN Success,
    _In_ UCM_DATA_ROLE CurrentDataRole)
{
    PUCMCX_CONNECTOR_CONTEXT Context;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (Connector == NULL || !Success)
        return;

    Context = UcmCxGetConnectorContext(Connector);
    KeAcquireSpinLock(&Context->Lock, &Irql);
    if (Context->Attached)
        Context->DataRole = CurrentDataRole;
    KeReleaseSpinLock(&Context->Lock, Irql);
}

static
VOID
NTAPI
UcmCxConnectorPowerDirectionChanged(
    _In_ PUCM_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMCONNECTOR Connector,
    _In_ BOOLEAN Success,
    _In_ UCM_POWER_ROLE CurrentPowerRole)
{
    PUCMCX_CONNECTOR_CONTEXT Context;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (Connector == NULL || !Success)
        return;

    Context = UcmCxGetConnectorContext(Connector);
    KeAcquireSpinLock(&Context->Lock, &Irql);
    if (Context->Attached)
        Context->PowerRole = CurrentPowerRole;
    KeReleaseSpinLock(&Context->Lock, Irql);
}

static PVOID UcmCxFunctions[UcmFunctionTableNumEntries] =
{
    UcmCxInitializeDevice,
    UcmCxConnectorCreate,
    UcmCxConnectorTypeCAttach,
    UcmCxConnectorTypeCDetach,
    UcmCxConnectorTypeCCurrentAdChanged,
    UcmCxConnectorPdSourceCaps,
    UcmCxConnectorPdPartnerSourceCaps,
    UcmCxConnectorPdConnectionStateChanged,
    UcmCxConnectorChargingStateChanged,
    UcmCxConnectorDataDirectionChanged,
    UcmCxConnectorPowerDirectionChanged
};

static
NTSTATUS
NTAPI
UcmCxLibraryBindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    return WdfCxBindClient(ClassBindInfo,
                           ClientGlobals,
                           UcmCxFunctions,
                           RTL_NUMBER_OF(UcmCxFunctions),
                           1);
}

static
VOID
NTAPI
UcmCxLibraryUnbindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    WdfCxUnbindClient(ClassBindInfo);
}

static WDF_CLASS_LIBRARY_INFO UcmCxLibraryInfo =
{
    sizeof(WDF_CLASS_LIBRARY_INFO),
    {1, 0, 0},
    NULL,
    NULL,
    UcmCxLibraryBindClient,
    UcmCxLibraryUnbindClient
};

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    return WdfCxRegisterLibrary(DriverObject,
                                RegistryPath,
                                L"\\Device\\UcmCx",
                                &UcmCxLibraryInfo);
}
