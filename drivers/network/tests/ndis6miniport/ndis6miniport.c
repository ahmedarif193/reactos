/*
 * PROJECT:     ReactOS NDIS 6.20 miniport test driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * FILE:        drivers/network/tests/ndis6miniport/ndis6miniport.c
 * PURPOSE:     Minimal synthetic NDIS 6.20 miniport. Pretends to be an
 *              Ethernet NIC; has no real hardware. Exists primarily to
 *              link-test the NDIS 6 registration, optional-handler, and
 *              datapath exports used by a native miniport.
 *
 *              DriverEntry registers the miniport characteristics with
 *              NdisMRegisterMiniportDriver. It does not actually process
 *              any packets.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors.
 */

#include <ntifs.h>
#include <ndis.h>

static NDIS_HANDLE g_MiniportDriverHandle = NULL;

static NDIS_STATUS NTAPI
TestMiniportAddDevice(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_HANDLE MiniportDriverContext)
{
    NDIS_MINIPORT_ADAPTER_ATTRIBUTES Attributes;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    RtlZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.AddDeviceRegistrationAttributes.Header.Type =
        NDIS_OBJECT_TYPE_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES;
    Attributes.AddDeviceRegistrationAttributes.Header.Revision =
        NDIS_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES_REVISION_1;
    Attributes.AddDeviceRegistrationAttributes.Header.Size =
        NDIS_SIZEOF_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES_REVISION_1;
    Attributes.AddDeviceRegistrationAttributes.MiniportAddDeviceContext =
        NdisMiniportHandle;
    return NdisMSetMiniportAttributes(NdisMiniportHandle, &Attributes);
}

static VOID NTAPI
TestMiniportRemoveDevice(
    _In_ NDIS_HANDLE MiniportAddDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportAddDeviceContext);
}

static NDIS_STATUS NTAPI
TestMiniportFilterResourceRequirements(
    _In_ NDIS_HANDLE MiniportAddDeviceContext,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(MiniportAddDeviceContext);
    UNREFERENCED_PARAMETER(Irp);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
TestMiniportStartDevice(
    _In_ NDIS_HANDLE MiniportAddDeviceContext,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(MiniportAddDeviceContext);
    UNREFERENCED_PARAMETER(Irp);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
TestMiniportSetOptions(
    _In_ NDIS_HANDLE NdisDriverHandle,
    _In_ NDIS_HANDLE MiniportDriverContext)
{
    NDIS_MINIPORT_PNP_CHARACTERISTICS PnpCharacteristics;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    RtlZeroMemory(&PnpCharacteristics, sizeof(PnpCharacteristics));
    PnpCharacteristics.Header.Type =
        NDIS_OBJECT_TYPE_MINIPORT_PNP_CHARACTERISTICS;
    PnpCharacteristics.Header.Revision =
        NDIS_MINIPORT_PNP_CHARACTERISTICS_REVISION_1;
    PnpCharacteristics.Header.Size =
        NDIS_SIZEOF_MINIPORT_PNP_CHARACTERISTICS_REVISION_1;
    PnpCharacteristics.MiniportAddDeviceHandler = TestMiniportAddDevice;
    PnpCharacteristics.MiniportRemoveDeviceHandler = TestMiniportRemoveDevice;
    PnpCharacteristics.MiniportFilterResourceRequirementsHandler =
        TestMiniportFilterResourceRequirements;
    PnpCharacteristics.MiniportStartDeviceHandler = TestMiniportStartDevice;
    return NdisSetOptionalHandlers(NdisDriverHandle, (PNDIS_DRIVER_OPTIONAL_HANDLERS)&PnpCharacteristics);
}

static NDIS_STATUS NTAPI
TestMiniportInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS InitParams)
{
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(InitParams);
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
TestMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(HaltAction);
}

static NDIS_STATUS NTAPI
TestMiniportPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(PauseParameters);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
TestMiniportRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RestartParameters);
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
TestMiniportSendNbl(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferList);
    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);
    /* A real driver would transmit the packet. This stub just drops. */
}

static VOID NTAPI
TestMiniportReturnNbl(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

static VOID NTAPI
TestMiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static BOOLEAN NTAPI
TestMiniportCheckForHang(_In_ NDIS_HANDLE Ctx)
{
    UNREFERENCED_PARAMETER(Ctx);
    return FALSE;
}

static NDIS_STATUS NTAPI
TestMiniportReset(_In_ NDIS_HANDLE Ctx, _Out_ PBOOLEAN AddressingReset)
{
    UNREFERENCED_PARAMETER(Ctx);
    if (AddressingReset) *AddressingReset = FALSE;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
TestMiniportOidRequest(
    _In_ NDIS_HANDLE Ctx,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(OidRequest);
    return NDIS_STATUS_NOT_SUPPORTED;
}

static VOID NTAPI
TestMiniportCancelOidRequest(_In_ NDIS_HANDLE Ctx, _In_ PVOID Id)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Id);
}

static NDIS_STATUS NTAPI
TestMiniportDirectOidRequest(
    _In_ NDIS_HANDLE Ctx,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    return TestMiniportOidRequest(Ctx, OidRequest);
}

static VOID NTAPI
TestMiniportCancelDirectOidRequest(_In_ NDIS_HANDLE Ctx, _In_ PVOID Id)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Id);
}

static VOID NTAPI
TestMiniportDevicePnpEvent(_In_ NDIS_HANDLE Ctx, _In_ struct _NET_DEVICE_PNP_EVENT* Ev)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Ev);
}

static VOID NTAPI
TestMiniportShutdown(_In_ NDIS_HANDLE Ctx, _In_ NDIS_SHUTDOWN_ACTION Action)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Action);
}

static VOID NTAPI
TestMiniportUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    if (g_MiniportDriverHandle)
    {
        NdisMDeregisterMiniportDriver(g_MiniportDriverHandle);
        g_MiniportDriverHandle = NULL;
    }
}

NTSTATUS NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS characteristics;

    RtlZeroMemory(&characteristics, sizeof(characteristics));
    characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    characteristics.MajorNdisVersion = 6;
    characteristics.MinorNdisVersion = 20;
    characteristics.MajorDriverVersion = 1;
    characteristics.MinorDriverVersion = 0;
    characteristics.SetOptionsHandler = TestMiniportSetOptions;
    characteristics.InitializeHandlerEx = TestMiniportInitializeEx;
    characteristics.HaltHandlerEx = TestMiniportHaltEx;
    characteristics.UnloadHandler = TestMiniportUnload;
    characteristics.PauseHandler = TestMiniportPause;
    characteristics.RestartHandler = TestMiniportRestart;
    characteristics.SendNetBufferListsHandler = TestMiniportSendNbl;
    characteristics.ReturnNetBufferListsHandler = TestMiniportReturnNbl;
    characteristics.CancelSendHandler = TestMiniportCancelSend;
    characteristics.CheckForHangHandlerEx = TestMiniportCheckForHang;
    characteristics.ResetHandlerEx = TestMiniportReset;
    characteristics.OidRequestHandler = TestMiniportOidRequest;
    characteristics.CancelOidRequestHandler = TestMiniportCancelOidRequest;
    characteristics.DirectOidRequestHandler = TestMiniportDirectOidRequest;
    characteristics.CancelDirectOidRequestHandler = TestMiniportCancelDirectOidRequest;
    characteristics.DevicePnPEventNotifyHandler = TestMiniportDevicePnpEvent;
    characteristics.ShutdownHandlerEx = TestMiniportShutdown;

    return NdisMRegisterMiniportDriver(
        DriverObject,
        RegistryPath,
        NULL,
        &characteristics,
        &g_MiniportDriverHandle);
}

/* EOF */
