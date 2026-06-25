/*
 * PROJECT:     ReactOS Broadcom/Cypress CYW43455 Native 802.11 Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NDIS 6.20 miniport lifecycle and SDIO bring-up orchestration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"

#define NDEBUG
#include <debug.h>

extern NDIS_OID CywSupportedOids[];
extern ULONG CywSupportedOidCount;

static NDIS_HANDLE gMiniportDriverHandle = NULL;

static
NDIS_STATUS
CywSetMiniportAttributes(
    _In_ PCYW_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RegAttr;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GenAttr;
    NDIS_STATUS Status;

    RtlZeroMemory(&RegAttr, sizeof(RegAttr));
    RegAttr.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegAttr.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegAttr.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES);
    RegAttr.MiniportAdapterContext = (NDIS_HANDLE)Adapter;
    RegAttr.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND |
                             NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    RegAttr.CheckForHangTimeInSeconds = 0;
    RegAttr.InterfaceType = NdisInterfaceInternal;

    Status = NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RegAttr);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("CYW: SetMiniportAttributes(registration) failed 0x%08X\n", Status);
        return Status;
    }

    RtlZeroMemory(&GenAttr, sizeof(GenAttr));
    GenAttr.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GenAttr.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttr.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES);

    GenAttr.MediaType = NdisMediumNative802_11;
    GenAttr.PhysicalMediumType = (NDIS_MEDIUM)NdisPhysicalMediumNative802_11;
    GenAttr.MtuSize = CYW_MTU_SIZE;
    GenAttr.MaxXmitLinkSpeed = CYW_LINK_SPEED_BPS;
    GenAttr.MaxRcvLinkSpeed = CYW_LINK_SPEED_BPS;
    GenAttr.XmitLinkSpeed = CYW_LINK_SPEED_BPS;
    GenAttr.RcvLinkSpeed = CYW_LINK_SPEED_BPS;
    GenAttr.MediaConnectState = MediaConnectStateDisconnected;
    GenAttr.MediaDuplexState = MediaDuplexStateFull;
    GenAttr.LookaheadSize = CYW_MAX_FRAME_SIZE;
    GenAttr.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    GenAttr.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST;
    GenAttr.MaxMulticastListSize = 32;
    GenAttr.MacAddressLength = CYW_ADDRESS_LENGTH;
    RtlCopyMemory(GenAttr.PermanentMacAddress, Adapter->PermanentAddress, CYW_ADDRESS_LENGTH);
    RtlCopyMemory(GenAttr.CurrentMacAddress, Adapter->CurrentAddress, CYW_ADDRESS_LENGTH);
    GenAttr.RecvScaleCapabilities = NULL;
    GenAttr.AccessType = NET_IF_ACCESS_BROADCAST;
    GenAttr.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GenAttr.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    GenAttr.IfType = IF_TYPE_IEEE80211;
    GenAttr.IfConnectorPresent = TRUE;
    GenAttr.SupportedStatistics = 0;
    GenAttr.SupportedOidList = CywSupportedOids;
    GenAttr.SupportedOidListLength = CywSupportedOidCount * sizeof(NDIS_OID);

    Status = NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&GenAttr);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("CYW: SetMiniportAttributes(general) failed 0x%08X\n", Status);
    }
    return Status;
}

NDIS_STATUS
NTAPI
CywMiniportInitializeEx(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PCYW_ADAPTER Adapter;
    NDIS_STATUS Status;
    NTSTATUS NtStatus;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    DPRINT1("CYW: MiniportInitializeEx (CYW43455 SDIO)\n");

    Adapter = CywAllocate(sizeof(CYW_ADAPTER));
    if (Adapter == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    Adapter->MiniportAdapterHandle = NdisMiniportHandle;
    NdisAllocateSpinLock(&Adapter->Lock);
    Adapter->CurrentPhyType = dot11_phy_type_ht;
    Adapter->CurrentOperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
    Adapter->RadioOn = TRUE;
    Adapter->CurrentBackplaneWindow = 0xFFFFFFFF;

    Adapter->PermanentAddress[0] = 0x00;
    Adapter->PermanentAddress[1] = 0x90;
    Adapter->PermanentAddress[2] = 0x4C;
    Adapter->PermanentAddress[3] = 0xC5;
    Adapter->PermanentAddress[4] = 0x12;
    Adapter->PermanentAddress[5] = 0x38;
    RtlCopyMemory(Adapter->CurrentAddress, Adapter->PermanentAddress, CYW_ADDRESS_LENGTH);

    Adapter->ControlBuffer = CywAllocate(CYW_CONTROL_BUFFER_SIZE);
    Adapter->RxBuffer = CywAllocate(CYW_CONTROL_BUFFER_SIZE);
    Adapter->TxBuffer = CywAllocate(CYW_CONTROL_BUFFER_SIZE);
    if (Adapter->ControlBuffer == NULL || Adapter->RxBuffer == NULL ||
        Adapter->TxBuffer == NULL)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }
    KeInitializeMutex(&Adapter->F2Lock, 0);
    KeInitializeMutex(&Adapter->CmdLock, 0);
    KeInitializeEvent(&Adapter->CtrlEvent, NotificationEvent, FALSE);

    {
        NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
        RtlZeroMemory(&PoolParams, sizeof(PoolParams));
        PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
        PoolParams.Header.Size = sizeof(PoolParams);
        PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
        PoolParams.fAllocateNetBuffer = TRUE;
        PoolParams.PoolTag = CYW_TAG;
        PoolParams.DataSize = CYW_MAX_FRAME_SIZE;
        Adapter->RxNblPool = NdisAllocateNetBufferListPool(NdisMiniportHandle, &PoolParams);
        if (Adapter->RxNblPool == NULL)
        {
            Status = NDIS_STATUS_RESOURCES;
            goto Fail;
        }
    }

    NdisMGetDeviceProperty(NdisMiniportHandle,
                           &Adapter->Pdo,
                           NULL,
                           &Adapter->NextDeviceObject,
                           NULL,
                           NULL);
    if (Adapter->Pdo == NULL)
    {
        DPRINT1("CYW: no physical device object\n");
        Status = NDIS_STATUS_FAILURE;
        goto Fail;
    }

    NtStatus = CywSdioOpen(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NDIS_STATUS_OPEN_FAILED;
        goto Fail;
    }

    NtStatus = CywChipBringUp(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        DPRINT1("CYW: chip bring-up failed 0x%08lx\n", NtStatus);
        Status = NDIS_STATUS_HARD_ERRORS;
        goto Fail;
    }

    Status = CywSetMiniportAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        goto Fail;
    }

    Adapter->InterruptWorkItem = NdisAllocateIoWorkItem(NdisMiniportHandle);
    if (Adapter->InterruptWorkItem == NULL)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    NtStatus = CywStartRxThread(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        DPRINT1("CYW: RxThread start failed 0x%08lx\n", NtStatus);
        Status = NDIS_STATUS_FAILURE;
        goto Fail;
    }

    DPRINT1("CYW: MiniportInitializeEx succeeded (chip 0x%lx rev %lu)\n",
            Adapter->ChipId, Adapter->ChipRev);
    return NDIS_STATUS_SUCCESS;

Fail:
    CywStopRxThread(Adapter);
    if (Adapter->InterruptWorkItem != NULL)
    {
        NdisFreeIoWorkItem(Adapter->InterruptWorkItem);
    }
    CywSdioClose(Adapter);
    CywFree(Adapter->ControlBuffer);
    CywFree(Adapter->RxBuffer);
    CywFree(Adapter->TxBuffer);
    if (Adapter->RxNblPool != NULL)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
    }
    NdisFreeSpinLock(&Adapter->Lock);
    CywFree(Adapter);
    return Status;
}

VOID
NTAPI
CywMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    DPRINT1("CYW: MiniportHaltEx\n");

    Adapter->Halting = TRUE;

    CywStopRxThread(Adapter);

    if (Adapter->InterruptWorkItem != NULL)
    {
        NdisFreeIoWorkItem(Adapter->InterruptWorkItem);
        Adapter->InterruptWorkItem = NULL;
    }

    CywSdioClose(Adapter);
    CywFree(Adapter->ControlBuffer);
    CywFree(Adapter->RxBuffer);
    CywFree(Adapter->TxBuffer);
    if (Adapter->RxNblPool != NULL)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
    }
    NdisFreeSpinLock(&Adapter->Lock);
    CywFree(Adapter);
}

NDIS_STATUS
NTAPI
CywMiniportPauseEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS MiniportPauseParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(MiniportPauseParameters);

    DPRINT1("CYW: MiniportPauseEx\n");
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
CywMiniportRestartEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS MiniportRestartParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(MiniportRestartParameters);

    DPRINT1("CYW: MiniportRestartEx\n");
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
CywMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(ShutdownAction);
}

VOID
NTAPI
CywMiniportDevicePnpEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

typedef struct _CYW_TX_WORK
{
    PCYW_ADAPTER Adapter;
    PNET_BUFFER_LIST Nbl;
    NDIS_HANDLE WorkItem;
    ULONG EthLen;
    UCHAR Eth[CYW_MAX_FRAME_SIZE];
} CYW_TX_WORK, *PCYW_TX_WORK;

static
ULONG
CywBuildEthFromNbl(
    _In_ PNET_BUFFER Nb,
    _Out_writes_(CYW_MAX_FRAME_SIZE) PUCHAR Scratch,
    _Out_writes_(CYW_MAX_FRAME_SIZE) PUCHAR Eth)
{
    ULONG FrameLen = NET_BUFFER_DATA_LENGTH(Nb);
    PUCHAR Frame;
    PCYW_DOT11_HEADER Dot11;
    PCYW_SNAP_HEADER Snap;
    ULONG PayloadLen;

    if (FrameLen < sizeof(CYW_DOT11_HEADER) + sizeof(CYW_SNAP_HEADER) ||
        FrameLen > CYW_MAX_FRAME_SIZE)
    {
        return 0;
    }

    Frame = NdisGetDataBuffer(Nb, FrameLen, Scratch, 1, 0);
    if (Frame == NULL)
    {
        return 0;
    }

    Dot11 = (PCYW_DOT11_HEADER)Frame;
    Snap = (PCYW_SNAP_HEADER)(Frame + sizeof(CYW_DOT11_HEADER));
    PayloadLen = FrameLen - sizeof(CYW_DOT11_HEADER) - sizeof(CYW_SNAP_HEADER);

    RtlCopyMemory(Eth, Dot11->Address3, CYW_ADDRESS_LENGTH);
    RtlCopyMemory(Eth + CYW_ADDRESS_LENGTH, Dot11->Address2, CYW_ADDRESS_LENGTH);
    Eth[12] = Snap->EtherType[0];
    Eth[13] = Snap->EtherType[1];
    RtlCopyMemory(Eth + sizeof(CYW_ETHER_HEADER),
                  Frame + sizeof(CYW_DOT11_HEADER) + sizeof(CYW_SNAP_HEADER),
                  PayloadLen);
    return sizeof(CYW_ETHER_HEADER) + PayloadLen;
}

static
VOID
CywSendWorker(
    _In_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PCYW_TX_WORK Work = (PCYW_TX_WORK)WorkItemContext;
    PCYW_ADAPTER Adapter = Work->Adapter;
    NDIS_STATUS NblStatus = NDIS_STATUS_FAILURE;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    if (NT_SUCCESS(CywSdpcmSendData(Adapter, Work->Eth, Work->EthLen)))
    {
        NblStatus = NDIS_STATUS_SUCCESS;
    }

    NET_BUFFER_LIST_STATUS(Work->Nbl) = NblStatus;
    NET_BUFFER_LIST_NEXT_NBL(Work->Nbl) = NULL;
    NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle, Work->Nbl, 0);

    NdisFreeIoWorkItem(Work->WorkItem);
    CywFree(Work);
}

VOID
NTAPI
CywMiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl = NetBufferLists;
    ULONG CompleteFlags = 0;

    UNREFERENCED_PARAMETER(PortNumber);

    if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags))
    {
        CompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;
    }

    while (Nbl != NULL)
    {
        PNET_BUFFER_LIST Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        PNET_BUFFER Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
        NDIS_STATUS NblStatus = NDIS_STATUS_FAILURE;
        BOOLEAN Deferred = FALSE;

        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        if (Adapter->Associated && Nb != NULL)
        {
            if (KeGetCurrentIrql() == PASSIVE_LEVEL)
            {
                UCHAR Scratch[CYW_MAX_FRAME_SIZE];
                UCHAR Eth[CYW_MAX_FRAME_SIZE];
                ULONG EthLen = CywBuildEthFromNbl(Nb, Scratch, Eth);

                if (EthLen != 0 && NT_SUCCESS(CywSdpcmSendData(Adapter, Eth, EthLen)))
                {
                    NblStatus = NDIS_STATUS_SUCCESS;
                }
            }
            else
            {
                PCYW_TX_WORK Work = CywAllocate(sizeof(CYW_TX_WORK));

                if (Work != NULL)
                {
                    UCHAR Scratch[CYW_MAX_FRAME_SIZE];

                    Work->EthLen = CywBuildEthFromNbl(Nb, Scratch, Work->Eth);
                    if (Work->EthLen != 0)
                    {
                        Work->WorkItem = NdisAllocateIoWorkItem(Adapter->MiniportAdapterHandle);
                        if (Work->WorkItem != NULL)
                        {
                            Work->Adapter = Adapter;
                            Work->Nbl = Nbl;
                            NdisQueueIoWorkItem(Work->WorkItem, CywSendWorker, Work);
                            Deferred = TRUE;
                        }
                    }
                    if (!Deferred)
                    {
                        CywFree(Work);
                    }
                }
            }
        }

        if (!Deferred)
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NblStatus;
            NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle, Nbl, CompleteFlags);
        }

        Nbl = Next;
    }
}

VOID
NTAPI
CywMiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

VOID
NTAPI
CywMiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

VOID
NTAPI
CywMiniportUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    DPRINT1("CYW: DriverUnload\n");

    if (gMiniportDriverHandle != NULL)
    {
        NdisMDeregisterMiniportDriver(gMiniportDriverHandle);
        gMiniportDriverHandle = NULL;
    }
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Chars;
    NDIS_STATUS Status;

    DPRINT1("CYW: DriverEntry\n");

    RtlZeroMemory(&Chars, sizeof(Chars));
    Chars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Chars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);
    Chars.MajorNdisVersion = 6;
    Chars.MinorNdisVersion = 20;
    Chars.MajorDriverVersion = 1;
    Chars.MinorDriverVersion = 0;

    Chars.InitializeHandlerEx = CywMiniportInitializeEx;
    Chars.HaltHandlerEx = CywMiniportHaltEx;
    Chars.UnloadHandler = CywMiniportUnload;
    Chars.PauseHandler = CywMiniportPauseEx;
    Chars.RestartHandler = CywMiniportRestartEx;
    Chars.OidRequestHandler = CywMiniportOidRequest;
    Chars.SendNetBufferListsHandler = CywMiniportSendNetBufferLists;
    Chars.ReturnNetBufferListsHandler = CywMiniportReturnNetBufferLists;
    Chars.CancelSendHandler = CywMiniportCancelSend;
    Chars.CheckForHangHandlerEx = NULL;
    Chars.ResetHandlerEx = NULL;
    Chars.DevicePnPEventNotifyHandler = CywMiniportDevicePnpEventNotify;
    Chars.ShutdownHandlerEx = CywMiniportShutdownEx;
    Chars.CancelOidRequestHandler = CywMiniportCancelOidRequest;

    Status = NdisMRegisterMiniportDriver(DriverObject,
                                         RegistryPath,
                                         NULL,
                                         &Chars,
                                         &gMiniportDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("CYW: NdisMRegisterMiniportDriver failed 0x%08X\n", Status);
    }

    return (NTSTATUS)Status;
}
