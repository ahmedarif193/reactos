/*
 * PROJECT:     ReactOS TCP/IP protocol driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * FILE:        drivers/network/tcpip/tcpip/ndis6_protocol.c
 * PURPOSE:     NDIS 6 protocol driver scaffold for tcpip.sys.
 *
 *              The legacy TDI-era tcpip.sys registers with NDIS 5.x via
 *              NdisRegisterProtocol in tcpip/main.c. This file adds a
 *              parallel NDIS 6 registration via NdisRegisterProtocolDriver
 *              so that Win7-level code paths can be brought online
 *              incrementally. Compiled only when TCPIP_NDIS6_PORT is
 *              defined; default tcpip.sys build still uses the legacy path.
 *
 *              This is a SCAFFOLD. The callbacks do not actually process
 *              traffic — they log + return NDIS_STATUS_SUCCESS so a
 *              bound miniport can make the registration succeed. The full
 *              port (NET_BUFFER_LIST receive path, TCP/IP state migration)
 *              is deferred to the next phase of the dev-nt6-1 branch.
 */

#ifdef TCPIP_NDIS6_PORT

#define NDIS620          1
#define NDIS620_PROTOCOL 1

#include <ntifs.h>
#include <ndis.h>

static NDIS_HANDLE g_Ndis6ProtocolHandle = NULL;

static NDIS_STATUS NTAPI
TcpipNdis6BindAdapter(
    _In_ NDIS_HANDLE ProtocolDriverContext,
    _In_ NDIS_HANDLE BindContext,
    _In_ PNDIS_BIND_PARAMETERS BindParameters)
{
    UNREFERENCED_PARAMETER(ProtocolDriverContext);
    UNREFERENCED_PARAMETER(BindContext);
    UNREFERENCED_PARAMETER(BindParameters);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
TcpipNdis6UnbindAdapter(
    _In_ NDIS_HANDLE UnbindContext,
    _In_ NDIS_HANDLE ProtocolBindingContext)
{
    UNREFERENCED_PARAMETER(UnbindContext);
    UNREFERENCED_PARAMETER(ProtocolBindingContext);
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
TcpipNdis6OpenComplete(_In_ NDIS_HANDLE Ctx, _In_ NDIS_STATUS Status)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Status);
}

static VOID NTAPI
TcpipNdis6CloseComplete(_In_ NDIS_HANDLE Ctx)
{
    UNREFERENCED_PARAMETER(Ctx);
}

static VOID NTAPI
TcpipNdis6ReceiveNbl(
    _In_ NDIS_HANDLE Ctx,
    _In_ PNET_BUFFER_LIST Nbls,
    _In_ NDIS_PORT_NUMBER Port,
    _In_ ULONG Count,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Nbls);
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Flags);
}

static VOID NTAPI
TcpipNdis6SendNblComplete(
    _In_ NDIS_HANDLE Ctx,
    _In_ PNET_BUFFER_LIST Nbls,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Nbls);
    UNREFERENCED_PARAMETER(Flags);
}

static VOID NTAPI
TcpipNdis6OidRequestComplete(
    _In_ NDIS_HANDLE Ctx,
    _In_ PNDIS_OID_REQUEST Req,
    _In_ NDIS_STATUS Status)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Req);
    UNREFERENCED_PARAMETER(Status);
}

static VOID NTAPI
TcpipNdis6Status(
    _In_ NDIS_HANDLE Ctx,
    _In_ PNDIS_STATUS_INDICATION Ind)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Ind);
}

static NDIS_STATUS NTAPI
TcpipNdis6NetPnpEvent(
    _In_ NDIS_HANDLE Ctx,
    _In_ PVOID Ev)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Ev);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
TcpipNdis6RegisterProtocol(VOID)
{
    NDIS_PROTOCOL_DRIVER_CHARACTERISTICS characteristics;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"Tcpip6");

    RtlZeroMemory(&characteristics, sizeof(characteristics));
    characteristics.Header.Type = NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS;
    characteristics.Header.Size = sizeof(characteristics);
    characteristics.Header.Revision = NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    characteristics.MajorNdisVersion = 6;
    characteristics.MinorNdisVersion = 20;
    characteristics.MajorDriverVersion = 1;
    characteristics.MinorDriverVersion = 0;
    characteristics.BindAdapterHandlerEx = TcpipNdis6BindAdapter;
    characteristics.UnbindAdapterHandlerEx = TcpipNdis6UnbindAdapter;
    characteristics.OpenAdapterCompleteHandlerEx = TcpipNdis6OpenComplete;
    characteristics.CloseAdapterCompleteHandlerEx = TcpipNdis6CloseComplete;
    characteristics.ReceiveNetBufferListsHandler = TcpipNdis6ReceiveNbl;
    characteristics.SendNetBufferListsCompleteHandler = TcpipNdis6SendNblComplete;
    characteristics.OidRequestCompleteHandler = TcpipNdis6OidRequestComplete;
    characteristics.StatusHandlerEx = TcpipNdis6Status;
    characteristics.NetPnPEventHandler = TcpipNdis6NetPnpEvent;
    characteristics.Name = &name;

    return NdisRegisterProtocolDriver(NULL, &characteristics, &g_Ndis6ProtocolHandle);
}

VOID
TcpipNdis6DeregisterProtocol(VOID)
{
    if (g_Ndis6ProtocolHandle != NULL)
    {
        NdisDeregisterProtocolDriver(g_Ndis6ProtocolHandle);
        g_Ndis6ProtocolHandle = NULL;
    }
}

#endif /* TCPIP_NDIS6_PORT */

/* EOF */
