/*
 * PROJECT:     ReactOS NDIS 6.20 miniport test driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * FILE:        drivers/network/tests/ndis6miniport/ndis6miniport.c
 * PURPOSE:     Minimal synthetic NDIS 6.20 miniport. Pretends to be an
 *              Ethernet NIC; has no real hardware. Exists primarily to
 *              link-test the NDIS 6 stub exports added in drivers/network/
 *              ndis/60stubs.c as part of the dev-nt6-1 branch upgrade.
 *
 *              DriverEntry registers the miniport characteristics with
 *              NdisMRegisterMiniportDriver. The stub ndis.sys returns
 *              NDIS_STATUS_SUCCESS, so the driver loads cleanly. It does
 *              not actually process any packets.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors.
 */

#include <ntifs.h>
#include <ndis.h>

static NDIS_HANDLE g_MiniportDriverHandle = NULL;

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
    characteristics.Header.Size = sizeof(characteristics);
    characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    characteristics.MajorNdisVersion = 6;
    characteristics.MinorNdisVersion = 20;
    characteristics.MajorDriverVersion = 1;
    characteristics.MinorDriverVersion = 0;
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
