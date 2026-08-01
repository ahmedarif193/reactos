/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DriverEntry, miniport/protocol registration, IM association
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"

#define NDEBUG
#include <debug.h>

NWIFI_GLOBALS gNwifi = { 0 };

/* Protocol name (must match the INF binding name); built at runtime because
 * the current ReactOS ndis.h lacks NDIS_STRING_CONST. */
static const WCHAR NwifiProtocolNameBuffer[] = L"Nwifi";
static NDIS_STRING NwifiProtocolName;

/* ===========================================================================
 *  Pool helpers
 * ===========================================================================
 */
PVOID
NwifiAllocate(
    _In_ SIZE_T Size)
{
    PVOID Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, NWIFI_TAG);
    if (Buffer != NULL)
    {
        RtlZeroMemory(Buffer, Size);
    }
    return Buffer;
}

VOID
NwifiFree(
    _In_ PVOID Buffer)
{
    if (Buffer != NULL)
    {
        ExFreePoolWithTag(Buffer, NWIFI_TAG);
    }
}

/* The caller uses the result only while servicing an IOCTL; interface
 * teardown is serialised by NDIS unbind at PASSIVE_LEVEL. */
PNWIFI_ADAPTER
NwifiFindAdapterByIndex(
    _In_ ULONG InterfaceIndex)
{
    PLIST_ENTRY Entry;
    PNWIFI_ADAPTER Found = NULL;

    NdisAcquireSpinLock(&gNwifi.AdapterLock);
    for (Entry = gNwifi.AdapterList.Flink;
         Entry != &gNwifi.AdapterList;
         Entry = Entry->Flink)
    {
        PNWIFI_ADAPTER Adapter = CONTAINING_RECORD(Entry, NWIFI_ADAPTER, Link);
        if (Adapter->InterfaceIndex == InterfaceIndex)
        {
            Found = Adapter;
            break;
        }
    }
    NdisReleaseSpinLock(&gNwifi.AdapterLock);
    return Found;
}

/* ===========================================================================
 *  Registration helpers
 * ===========================================================================
 */
static
NDIS_STATUS
NwifiRegisterMiniport(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Chars;

    RtlZeroMemory(&Chars, sizeof(Chars));
    Chars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Chars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);
    Chars.MajorNdisVersion = 6;
    Chars.MinorNdisVersion = 20;
    Chars.MajorDriverVersion = 1;
    Chars.MinorDriverVersion = 0;

    /* Upper-edge virtual 802.3 miniport entry points. */
    Chars.InitializeHandlerEx        = NwifiMiniportInitializeEx;
    Chars.HaltHandlerEx              = NwifiMiniportHaltEx;
    Chars.PauseHandler               = NwifiMiniportPauseEx;
    Chars.RestartHandler             = NwifiMiniportRestartEx;
    Chars.OidRequestHandler          = NwifiMiniportOidRequest;
    Chars.CancelOidRequestHandler    = NwifiMiniportCancelOidRequest;
    Chars.SendNetBufferListsHandler  = NwifiMiniportSendNetBufferLists;
    Chars.ReturnNetBufferListsHandler = NwifiMiniportReturnNetBufferLists;
    Chars.CancelSendHandler          = NwifiMiniportCancelSend;
    Chars.DevicePnPEventNotifyHandler = NwifiMiniportDevicePnPEventNotify;
    Chars.ShutdownHandlerEx          = NwifiMiniportShutdownEx;
    /* CheckForHang / Reset not needed for a virtual miniport. */

    return NdisMRegisterMiniportDriver(DriverObject,
                                       RegistryPath,
                                       NULL,
                                       &Chars,
                                       &gNwifi.MiniportDriverHandle);
}

static
NDIS_STATUS
NwifiRegisterProtocol(VOID)
{
    NDIS_PROTOCOL_DRIVER_CHARACTERISTICS Chars;

    NwifiProtocolName.Buffer = (PWCH)NwifiProtocolNameBuffer;
    NwifiProtocolName.Length = (USHORT)(wcslen(NwifiProtocolNameBuffer) * sizeof(WCHAR));
    NwifiProtocolName.MaximumLength =
        (USHORT)(NwifiProtocolName.Length + sizeof(WCHAR));

    RtlZeroMemory(&Chars, sizeof(Chars));
    Chars.Header.Type = NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS;
    Chars.Header.Revision = NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.Header.Size = NDIS_SIZEOF_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.MajorNdisVersion = 6;
    Chars.MinorNdisVersion = 20;
    Chars.MajorDriverVersion = 1;
    Chars.MinorDriverVersion = 0;
    Chars.Name = NwifiProtocolName;

    /* Lower-edge protocol entry points (binds the dot11 miniport). */
    Chars.SetOptionsHandler                 = NwifiProtocolSetOptions;
    Chars.BindAdapterHandlerEx              = NwifiBindAdapterEx;
    Chars.UnbindAdapterHandlerEx           = NwifiUnbindAdapterEx;
    Chars.OpenAdapterCompleteHandlerEx     = NwifiOpenAdapterCompleteEx;
    Chars.CloseAdapterCompleteHandlerEx    = NwifiCloseAdapterCompleteEx;
    Chars.SendNetBufferListsCompleteHandler = NwifiProtocolSendNblComplete;
    Chars.ReceiveNetBufferListsHandler     = NwifiProtocolReceiveNbl;
    Chars.OidRequestCompleteHandler        = NwifiProtocolOidRequestComplete;
    Chars.StatusHandlerEx                  = NwifiProtocolStatusEx;
    Chars.NetPnPEventHandler               = NwifiProtocolNetPnPEvent;
    Chars.UninstallHandler                 = NwifiProtocolUninstall;

    return NdisRegisterProtocolDriver(NULL,
                                      &Chars,
                                      &gNwifi.ProtocolHandle);
}

/* ===========================================================================
 *  Unload
 * ===========================================================================
 */
VOID
NTAPI
NwifiUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    DPRINT1("NWIFI: Unload\n");

    /* Remove the control device + drain any pended notification IRPs first. */
    NwifiDeleteControlDevice();

    /* Tear down the protocol first so NDIS unbinds the lower miniports
     * (driving UnbindAdapterEx -> miniport teardown), then the miniport. */
    if (gNwifi.ProtocolHandle != NULL)
    {
        NdisDeregisterProtocolDriver(gNwifi.ProtocolHandle);
        gNwifi.ProtocolHandle = NULL;
    }

    if (gNwifi.MiniportDriverHandle != NULL)
    {
        NdisMDeregisterMiniportDriver(gNwifi.MiniportDriverHandle);
        gNwifi.MiniportDriverHandle = NULL;
    }

    NdisFreeSpinLock(&gNwifi.AdapterLock);
}

/* ===========================================================================
 *  DriverEntry
 * ===========================================================================
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_STATUS Status;

    DPRINT1("NWIFI: DriverEntry\n");

    RtlZeroMemory(&gNwifi, sizeof(gNwifi));
    gNwifi.DriverObject = DriverObject;
    InitializeListHead(&gNwifi.AdapterList);
    NdisAllocateSpinLock(&gNwifi.AdapterLock);
    InitializeListHead(&gNwifi.NotifyIrpQueue);
    KeInitializeSpinLock(&gNwifi.NotifyLock);
    gNwifi.NextInterfaceIndex = 0;

    /* Both registrations must exist before NdisIMAssociateMiniport pairs
     * them, ahead of any bind callback. */
    Status = NwifiRegisterMiniport(DriverObject, RegistryPath);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: NdisMRegisterMiniportDriver failed 0x%08X\n", Status);
        NdisFreeSpinLock(&gNwifi.AdapterLock);
        return (NTSTATUS)Status;
    }

    Status = NwifiRegisterProtocol();
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: NdisRegisterProtocolDriver failed 0x%08X\n", Status);
        NdisMDeregisterMiniportDriver(gNwifi.MiniportDriverHandle);
        gNwifi.MiniportDriverHandle = NULL;
        NdisFreeSpinLock(&gNwifi.AdapterLock);
        return (NTSTATUS)Status;
    }

    /* Pair the two edges so NDIS treats nwifi as a single IM driver. */
    NdisIMAssociateMiniport(gNwifi.MiniportDriverHandle, gNwifi.ProtocolHandle);

    /* Non-fatal on failure: the data path works without the wlansvc channel. */
    Status = NwifiCreateControlDevice();
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: control device creation failed 0x%08X (continuing)\n", Status);
    }

    DriverObject->DriverUnload = NwifiUnload;

    DPRINT1("NWIFI: DriverEntry succeeded\n");
    return STATUS_SUCCESS;
}
