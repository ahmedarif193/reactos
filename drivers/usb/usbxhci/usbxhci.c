/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <arif.ing@outlook.com>
 */
    
#include "usbxhci.h"

#define NDEBUG
#include <debug.h>

#ifndef XHCI_UNUSED
#ifdef __GNUC__
#define XHCI_UNUSED __attribute__((unused))
#else
#define XHCI_UNUSED
#endif
#endif

#define XHCI_COMMAND_TIMEOUT_MS 100
#define XHCI_COMMAND_POLL_INTERVAL_US 50
#ifndef VERBOSE_SHARED_IRQ
#define VERBOSE_SHARED_IRQ 0
#endif

#if VERBOSE_SHARED_IRQ
#define XHCI_DPRINT_SHARED(fmt, ...) DPRINT1(fmt, __VA_ARGS__)
#else
#define XHCI_DPRINT_SHARED(fmt, ...) do { } while (0)
#endif

#define XHCI_INVALID_LINK_STATE 0xFF
#define USBPORT_NO_HUB_ADDRESS 0xFFFF

USBPORT_REGISTRATION_PACKET XhciRegPacket;

/* TODO: fill out real interfaces; everything below is placeholder */

static MPSTATUS NTAPI XHCI_OpenEndpoint(PVOID MiniPortExtension, PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties, PVOID Endpoint);
static MPSTATUS XHCI_PerformEndpointOpen(PXHCI_EXTENSION Extension, PXHCI_ENDPOINT XhciEndpoint, PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties);
static MPSTATUS XHCI_DeferEndpointOpen(PXHCI_EXTENSION Extension, PXHCI_ENDPOINT Endpoint, PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties);
static VOID NTAPI XHCI_OpenEndpointWorker(PVOID Context);
static VOID NTAPI XHCI_CloseEndpoint(PVOID MiniPortExtension, PVOID Endpoint, BOOLEAN IsDoNotCallMiniport);
static MPSTATUS NTAPI XHCI_StartController(PVOID MiniPortExtension, PUSBPORT_RESOURCES UsbPortResources);
static VOID NTAPI XHCI_StopController(PVOID MiniPortExtension, BOOLEAN IsDoNotCallMiniport);
static BOOLEAN NTAPI XHCI_InterruptService(PVOID MiniPortExtension);
static VOID NTAPI XHCI_InterruptDpc(PVOID MiniPortExtension, BOOLEAN EnableInterrupts);
static VOID NTAPI XHCI_EnableInterrupts(PVOID MiniPortExtension);
static VOID NTAPI XHCI_DisableInterrupts(PVOID MiniPortExtension);
static VOID NTAPI XHCI_SuspendController(PVOID MiniPortExtension);
static MPSTATUS NTAPI XHCI_ResumeController(PVOID MiniPortExtension);
static MPSTATUS XHCI_RunController(PXHCI_EXTENSION Extension);
static MPSTATUS XHCI_HaltController(PXHCI_EXTENSION Extension, ULONG TimeoutUs);
static VOID XHCI_ShutdownController(PXHCI_EXTENSION Extension, BOOLEAN FullReset);
static MPSTATUS NTAPI XHCI_SubmitTransfer(PVOID MiniPortExtension,
                                          PVOID Endpoint,
                                          PUSBPORT_TRANSFER_PARAMETERS TransferParameters,
                                          PVOID Transfer,
                                          PUSBPORT_SCATTER_GATHER_LIST SgList);
static MPSTATUS XHCI_ResetController(PXHCI_EXTENSION Extension);
static BOOLEAN XHCI_WaitForRegisterBits(volatile ULONG *Reg, ULONG Mask, BOOLEAN WaitSet, ULONG TimeoutUs);
static VOID XHCI_HandleControllerError(PXHCI_EXTENSION Extension, ULONG PendingStatus);
static VOID XHCI_HandleCommandTimeout(PXHCI_EXTENSION Extension, ULONG CommandType);
static VOID NTAPI XHCI_RH_GetRootHubData(PVOID MiniPortExtension, PVOID RootHubData);
static MPSTATUS NTAPI XHCI_RH_GetStatus(PVOID MiniPortExtension, PUSHORT Status);
static MPSTATUS NTAPI XHCI_RH_GetPortStatus(PVOID MiniPortExtension, USHORT Port, PUSB_PORT_STATUS_AND_CHANGE PortStatus);
static MPSTATUS NTAPI XHCI_RH_GetHubStatus(PVOID MiniPortExtension, PUSB_HUB_STATUS_AND_CHANGE HubStatus);
static MPSTATUS NTAPI XHCI_RH_SetFeaturePortPower(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_ClearFeaturePortPower(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_SetFeaturePortReset(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_SetFeaturePortEnable(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_SetFeaturePortSuspend(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_ClearFeaturePortEnable(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_ClearFeaturePortEnableChange(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_ClearFeaturePortConnectChange(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_ClearFeaturePortResetChange(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_ClearFeaturePortSuspend(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_ClearFeaturePortSuspendChange(PVOID MiniPortExtension, USHORT Port);
static MPSTATUS NTAPI XHCI_RH_ClearFeaturePortOvercurrentChange(PVOID MiniPortExtension, USHORT Port);
static VOID NTAPI XHCI_RH_DisableIrq(PVOID MiniPortExtension);
static VOID NTAPI XHCI_RH_EnableIrq(PVOID MiniPortExtension);
static BOOLEAN XHCI_EventRingHasPendingTrb(PXHCI_EXTENSION Extension);
static VOID XHCI_PollForWork(PXHCI_EXTENSION Extension, BOOLEAN AllowCallbacks);
static VOID XHCI_DumpControllerState(PXHCI_EXTENSION Extension, PCSTR Reason);
/* Optional callbacks (safe stubs) */
static MPSTATUS NTAPI XHCI_ReopenEndpoint(PVOID MiniPortExtension,
                                         PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                                         PVOID EndpointHandle);
static MPSTATUS NTAPI XHCI_SubmitIsoTransfer(PVOID MiniPortExtension,
                                             PVOID EndpointHandle,
                                             PUSBPORT_TRANSFER_PARAMETERS TransferParameters,
                                             PVOID Param4,
                                             PVOID Param5);
static VOID NTAPI XHCI_AbortTransfer(PVOID MiniPortExtension,
                                     PVOID EndpointHandle,
                                     PVOID TransferHandle,
                                     PULONG BytesTransferred);
static VOID NTAPI XHCI_PollEndpoint(PVOID MiniPortExtension,
                                    PVOID EndpointHandle);
static VOID NTAPI XHCI_CheckController(PVOID MiniPortExtension);
static VOID NTAPI XHCI_PollController(PVOID MiniPortExtension);
static VOID NTAPI XHCI_SetEndpointDataToggle(PVOID MiniPortExtension,
                                             PVOID EndpointHandle,
                                             ULONG Toggle);
static ULONG NTAPI XHCI_GetEndpointStatus(PVOID MiniPortExtension,
                                          PVOID EndpointHandle);
static VOID NTAPI XHCI_SetEndpointStatus(PVOID MiniPortExtension,
                                         PVOID EndpointHandle,
                                         ULONG Status);
static VOID NTAPI XHCI_MpResetController(PVOID MiniPortExtension);
static MPSTATUS NTAPI XHCI_StartSendOnePacket(PVOID MiniPortExtension,
                                              PVOID Param1,
                                              PVOID Param2,
                                              PULONG Param3,
                                              PVOID Param4,
                                              PVOID Param5,
                                              ULONG Param6,
                                              USBD_STATUS *Param7);
static MPSTATUS NTAPI XHCI_EndSendOnePacket(PVOID MiniPortExtension,
                                            PVOID Param1,
                                            PVOID Param2,
                                            PULONG Param3,
                                            PVOID Param4,
                                            PVOID Param5,
                                            ULONG Param6,
                                            USBD_STATUS *Param7);
static MPSTATUS NTAPI XHCI_PassThru(PVOID MiniPortExtension,
                                    PVOID IoBuffer,
                                    ULONG IoControlCode,
                                    PVOID IoCtlParams);
static VOID NTAPI XHCI_RebalanceEndpoint(PVOID MiniPortExtension,
                                         PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                                         PVOID EndpointHandle);
static VOID NTAPI XHCI_FlushInterrupts(PVOID MiniPortExtension);
static MPSTATUS NTAPI XHCI_RH_ChirpRootPort(PVOID MiniPortExtension,
                                            USHORT Port);
static VOID NTAPI XHCI_TakePortControl(PVOID MiniPortExtension);
static BOOLEAN XHCI_IsValidPort(PXHCI_EXTENSION Extension, USHORT Port);
static volatile ULONG *XHCI_GetPortStatusRegister(PXHCI_EXTENSION Extension, USHORT Port);
static BOOLEAN XHCI_PortIsSuperSpeed(PXHCI_EXTENSION Extension, USHORT Port);
static VOID XHCI_RH_AckPortChange(PXHCI_EXTENSION Extension, USHORT Port, ULONG ChangeMask);
static MPSTATUS XHCI_ModifyPortBits(PXHCI_EXTENSION Extension, USHORT Port, ULONG SetMask, ULONG ClearMask, ULONG AckMask);
static MPSTATUS XHCI_SetPortLinkState(PXHCI_EXTENSION Extension, USHORT Port, ULONG LinkState);
static VOID XHCI_PowerOnAllPorts(PXHCI_EXTENSION Extension);
static MPSTATUS XHCI_ConfigurePageSize(PXHCI_EXTENSION Extension);
static VOID XHCI_TryWarmResetPort(PXHCI_EXTENSION Extension, USHORT Port);
static VOID XHCI_ResetCommandRingState(PXHCI_EXTENSION Extension);
static PXHCI_TRB XHCI_GetCommandRingTrb(PXHCI_EXTENSION Extension);
static VOID XHCI_AdvanceCommandRing(PXHCI_EXTENSION Extension);
static MPSTATUS XHCI_QueueCommand(PXHCI_EXTENSION Extension,
                                  ULONG TrbType,
                                  ULONGLONG Parameter,
                                  ULONGLONG Context,
                                  ULONG ControlFlags,
                                  PXHCI_COMMAND_CONTEXT CommandContext);
static VOID XHCI_RingCommandDoorbell(PXHCI_EXTENSION Extension);
static VOID XHCI_ServiceEventRing(PXHCI_EXTENSION Extension,
                                  BOOLEAN AcknowledgeInterrupt,
                                  BOOLEAN AllowCallbacks);
/* Async EP0 bring-up context and callback */
typedef struct _XHCI_EP0_BRINGUP_CTX {
    PXHCI_ENDPOINT Endpoint;
    USBPORT_ENDPOINT_PROPERTIES Props;
} XHCI_EP0_BRINGUP_CTX, *PXHCI_EP0_BRINGUP_CTX;
typedef struct _XHCI_DEFERRED_OPEN_WORK {
    WORK_QUEUE_ITEM Item;
    KEVENT CompletionEvent;
    PXHCI_ENDPOINT Endpoint;
    USBPORT_ENDPOINT_PROPERTIES Properties;
    MPSTATUS Status;
} XHCI_DEFERRED_OPEN_WORK, *PXHCI_DEFERRED_OPEN_WORK;
typedef struct _XHCI_EP0_WORK_WRAP {
    WORK_QUEUE_ITEM Item;
    XHCI_EP0_BRINGUP_CTX Ctx;
} XHCI_EP0_WORK_WRAP, *PXHCI_EP0_WORK_WRAP;
static VOID NTAPI XHCI_Ep0BringupCallback(IN PVOID MiniportExtension,
                                          IN PVOID CallBackContext);
static VOID NTAPI XHCI_Ep0BringupWorker(IN PVOID Context);
static VOID XHCI_HandleTransferEvent(PXHCI_EXTENSION Extension, PXHCI_TRB EventTrb);
static MPSTATUS XHCI_SendCommand(PXHCI_EXTENSION Extension,
                                 ULONG TrbType,
                                 ULONGLONG Parameter,
                                 ULONGLONG Context,
                                 ULONG ControlFlags,
                                 ULONG TimeoutMs,
                                 BOOLEAN AllowRetry,
                                 PUCHAR SlotIdOut,
                                 PULONG CompletionCodeOut);
static MPSTATUS XHCI_WaitForCommandCompletion(PXHCI_EXTENSION Extension,
                                              ULONG TimeoutMs,
                                              PXHCI_COMMAND_CONTEXT CommandContext,
                                              PUCHAR SlotIdOut,
                                              PULONG CompletionCodeOut);
static VOID XHCI_HandleCommandCompletion(PXHCI_EXTENSION Extension, PXHCI_TRB EventTrb);
static VOID XHCI_HandlePortStatusChangeEvent(PXHCI_EXTENSION Extension,
                                             PXHCI_TRB EventTrb,
                                             BOOLEAN NotifyHub);
static VOID XHCI_InitDeviceSlots(PXHCI_EXTENSION Extension);
static PXHCI_DEVICE_SLOT XHCI_GetSlot(PXHCI_EXTENSION Extension, UCHAR SlotId);
static MPSTATUS XHCI_AssignSlot(PXHCI_EXTENSION Extension, UCHAR SlotId);
static ULONG XHCI_MapDeviceSpeed(USB_DEVICE_SPEED Speed);
static ULONG XHCI_BuildRouteString(PXHCI_EXTENSION Extension,
                                   PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties);
static VOID XHCI_BuildErstTable(PXHCI_EXTENSION Extension);
static VOID XHCI_PrepareDefaultControlContext(PXHCI_EXTENSION Extension,
                                              PXHCI_DEVICE_SLOT Slot,
                                              PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties);
static MPSTATUS XHCI_BringupDefaultControlEndpoint(PXHCI_EXTENSION Extension,
                                                   PXHCI_ENDPOINT Endpoint,
                                                   PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties);
static MPSTATUS XHCI_InitializeScratchpads(PXHCI_EXTENSION Extension);
static PXHCI_ENDPOINT XHCI_GetSlotEndpoint(PXHCI_DEVICE_SLOT Slot, UCHAR EndpointId);
static VOID XHCI_RingEndpointDoorbell(PXHCI_EXTENSION Extension,
                                      UCHAR SlotId,
                                      UCHAR EndpointId,
                                      ULONG StreamId);
static PXHCI_TRB XHCI_GetTransferRingTrb(PXHCI_RING Ring, PULONGLONG PhysicalAddress);
static VOID XHCI_AdvanceTransferRing(PXHCI_RING Ring);
static VOID XHCI_ResetEndpointRing(PXHCI_ENDPOINT Endpoint);
static MPSTATUS XHCI_SubmitControlTransfer(PXHCI_EXTENSION Extension,
                                           PXHCI_ENDPOINT Endpoint,
                                           PXHCI_TRANSFER Transfer);
static MPSTATUS XHCI_SubmitBulkInterruptTransfer(PXHCI_EXTENSION Extension,
                                                 PXHCI_ENDPOINT Endpoint,
                                                 PXHCI_TRANSFER Transfer);
static MPSTATUS XHCI_AllocateTransferRing(PXHCI_EXTENSION Extension,
                                          ULONG TrbCount,
                                          BOOLEAN UseCommonBuffer,
                                          PXHCI_RING Ring);
static VOID XHCI_FreeTransferRing(PXHCI_RING Ring);
static UCHAR XHCI_EndpointIdFromProperties(PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties);
static ULONG XHCI_GetEndpointTypeFromProperties(PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties);
static PXHCI_DEVICE_SLOT XHCI_FindSlotByAddress(PXHCI_EXTENSION Extension, USHORT DeviceAddress);
static PXHCI_DEVICE_SLOT XHCI_FindSlotByPort(PXHCI_EXTENSION Extension, USHORT PortNumber);
static MPSTATUS XHCI_ConfigureSlotEndpoint(PXHCI_EXTENSION Extension,
                                           PXHCI_DEVICE_SLOT Slot,
                                           PXHCI_ENDPOINT Endpoint,
                                           UCHAR EndpointId);
static MPSTATUS XHCI_StopEndpoint(PXHCI_EXTENSION Extension,
                                  PXHCI_DEVICE_SLOT Slot,
                                  UCHAR EndpointId);
static MPSTATUS XHCI_StartEndpoint(PXHCI_EXTENSION Extension,
                                   PXHCI_DEVICE_SLOT Slot,
                                   UCHAR EndpointId);
static MPSTATUS XHCI_ResetEndpoint(PXHCI_EXTENSION Extension,
                                   PXHCI_DEVICE_SLOT Slot,
                                   UCHAR EndpointId);
static MPSTATUS XHCI_SetEndpointDequeue(PXHCI_EXTENSION Extension,
                                        PXHCI_DEVICE_SLOT Slot,
                                        UCHAR EndpointId,
                                        PXHCI_RING Ring);
static MPSTATUS XHCI_DropSlotEndpoint(PXHCI_EXTENSION Extension,
                                      PXHCI_DEVICE_SLOT Slot,
                                      UCHAR EndpointId);
static VOID XHCI_UpdateDeviceAddressMap(PXHCI_EXTENSION Extension,
                                        PXHCI_DEVICE_SLOT Slot,
                                        UCHAR NewAddress);
static VOID XHCI_InitDeviceAddressMap(PXHCI_EXTENSION Extension);
static VOID XHCI_HandleEnumerationTransfer(PXHCI_EXTENSION Extension,
                                           PXHCI_ENDPOINT Endpoint,
                                           PXHCI_TRANSFER Transfer);
static MPSTATUS XHCI_ResetDeviceOnPort(PXHCI_EXTENSION Extension, USHORT PortNumber);
static VOID XHCI_DetectHardwareQuirks(PXHCI_EXTENSION Extension);
static ULONG XHCI_FindExtendedCapability(PXHCI_EXTENSION Extension, UCHAR CapabilityId);
static VOID XHCI_DisableLegacySupport(PXHCI_EXTENSION Extension);
static VOID XHCI_ProbeMsiMsix(PXHCI_EXTENSION Extension);
static BOOLEAN XHCI_ReadPciConfig(PXHCI_EXTENSION Extension, ULONG Offset, PVOID Buffer, ULONG Length);
static VOID NTAPI XHCI_QueryEndpointRequirements(PVOID MiniPortExtension,
                                                PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                                                PUSBPORT_ENDPOINT_REQUIREMENTS Requirements);
static ULONG NTAPI XHCI_Get32BitFrameNumber(PVOID MiniPortExtension);
static VOID NTAPI XHCI_InterruptNextSOF(PVOID MiniPortExtension);
static VOID NTAPI XHCI_SetEndpointState(PVOID MiniPortExtension,
                                        PVOID EndpointHandle,
                                        ULONG State);
static ULONG NTAPI XHCI_GetEndpointState(PVOID MiniPortExtension,
                                         PVOID EndpointHandle);
static MPSTATUS
XHCI_SubmitSgTransfer(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _Inout_ PXHCI_TRANSFER Transfer,
    _In_ ULONG TrbType,
    _In_ BOOLEAN IsIsochronous);

static
VOID
NTAPI
XHCI_Unload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    DPRINT("usbxhci: unload stub\n");
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS Status;

    if (USBPORT_GetHciMn() != USBPORT_HCI_MN)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&XhciRegPacket, sizeof(XhciRegPacket));

    XhciRegPacket.MiniPortVersion = USB_MINIPORT_VERSION_XHCI;
    XhciRegPacket.MiniPortFlags = USB_MINIPORT_FLAGS_INTERRUPT |
                                  USB_MINIPORT_FLAGS_MEMORY_IO |
                                  USB_MINIPORT_FLAGS_USB3;

    /* Use a USB2-style bandwidth budget for now so
     * USBPORT's generic scheduler can place periodic
     * (interrupt/isochronous) endpoints. */
    XhciRegPacket.MiniPortBusBandwidth = TOTAL_USB20_BUS_BANDWIDTH;

    XhciRegPacket.MiniPortExtensionSize = sizeof(XHCI_EXTENSION);
    XhciRegPacket.MiniPortEndpointSize = sizeof(XHCI_ENDPOINT);
    XhciRegPacket.MiniPortTransferSize = sizeof(XHCI_TRANSFER);
    XhciRegPacket.MiniPortResourcesSize = sizeof(XHCI_HC_RESOURCES);

    XhciRegPacket.OpenEndpoint = XHCI_OpenEndpoint;
    XhciRegPacket.CloseEndpoint = XHCI_CloseEndpoint;
    XhciRegPacket.QueryEndpointRequirements = XHCI_QueryEndpointRequirements;
    XhciRegPacket.StartController = XHCI_StartController;
    XhciRegPacket.StopController = XHCI_StopController;
    XhciRegPacket.SuspendController = XHCI_SuspendController;
    XhciRegPacket.ResumeController = XHCI_ResumeController;
    XhciRegPacket.InterruptService = XHCI_InterruptService;
    XhciRegPacket.InterruptDpc = XHCI_InterruptDpc;
    XhciRegPacket.SubmitTransfer = XHCI_SubmitTransfer;
    XhciRegPacket.GetEndpointState = XHCI_GetEndpointState;
    XhciRegPacket.SetEndpointState = XHCI_SetEndpointState;
    XhciRegPacket.Get32BitFrameNumber = XHCI_Get32BitFrameNumber;
    XhciRegPacket.InterruptNextSOF = XHCI_InterruptNextSOF;
    XhciRegPacket.EnableInterrupts = XHCI_EnableInterrupts;
    XhciRegPacket.DisableInterrupts = XHCI_DisableInterrupts;
    XhciRegPacket.RH_GetRootHubData = XHCI_RH_GetRootHubData;
    XhciRegPacket.RH_GetStatus = XHCI_RH_GetStatus;
    XhciRegPacket.RH_GetPortStatus = XHCI_RH_GetPortStatus;
    XhciRegPacket.RH_GetHubStatus = XHCI_RH_GetHubStatus;
    XhciRegPacket.RH_SetFeaturePortReset = XHCI_RH_SetFeaturePortReset;
    XhciRegPacket.RH_SetFeaturePortPower = XHCI_RH_SetFeaturePortPower;
    XhciRegPacket.RH_SetFeaturePortEnable = XHCI_RH_SetFeaturePortEnable;
    XhciRegPacket.RH_SetFeaturePortSuspend = XHCI_RH_SetFeaturePortSuspend;
    XhciRegPacket.RH_ClearFeaturePortEnable = XHCI_RH_ClearFeaturePortEnable;
    XhciRegPacket.RH_ClearFeaturePortPower = XHCI_RH_ClearFeaturePortPower;
    XhciRegPacket.RH_ClearFeaturePortSuspend = XHCI_RH_ClearFeaturePortSuspend;
    XhciRegPacket.RH_ClearFeaturePortEnableChange = XHCI_RH_ClearFeaturePortEnableChange;
    XhciRegPacket.RH_ClearFeaturePortConnectChange = XHCI_RH_ClearFeaturePortConnectChange;
    XhciRegPacket.RH_ClearFeaturePortResetChange = XHCI_RH_ClearFeaturePortResetChange;
    XhciRegPacket.RH_ClearFeaturePortSuspendChange = XHCI_RH_ClearFeaturePortSuspendChange;
    XhciRegPacket.RH_ClearFeaturePortOvercurrentChange = XHCI_RH_ClearFeaturePortOvercurrentChange;
    XhciRegPacket.RH_DisableIrq = XHCI_RH_DisableIrq;
    XhciRegPacket.RH_EnableIrq = XHCI_RH_EnableIrq;

    /* Safe stubs for optional callbacks not yet implemented */
    XhciRegPacket.ReopenEndpoint = XHCI_ReopenEndpoint;
    XhciRegPacket.SubmitIsoTransfer = XHCI_SubmitIsoTransfer;
    XhciRegPacket.AbortTransfer = XHCI_AbortTransfer;
    XhciRegPacket.PollEndpoint = XHCI_PollEndpoint;
    XhciRegPacket.CheckController = XHCI_CheckController;
    XhciRegPacket.PollController = XHCI_PollController;
    XhciRegPacket.SetEndpointDataToggle = XHCI_SetEndpointDataToggle;
    XhciRegPacket.GetEndpointStatus = XHCI_GetEndpointStatus;
    XhciRegPacket.SetEndpointStatus = XHCI_SetEndpointStatus;
    XhciRegPacket.ResetController = XHCI_MpResetController;
    XhciRegPacket.StartSendOnePacket = XHCI_StartSendOnePacket;
    XhciRegPacket.EndSendOnePacket = XHCI_EndSendOnePacket;
    XhciRegPacket.PassThru = XHCI_PassThru;
    XhciRegPacket.RebalanceEndpoint = XHCI_RebalanceEndpoint;
    XhciRegPacket.FlushInterrupts = XHCI_FlushInterrupts;
    XhciRegPacket.RH_ChirpRootPort = XHCI_RH_ChirpRootPort;
    XhciRegPacket.TakePortControl = XHCI_TakePortControl;

    DriverObject->DriverUnload = XHCI_Unload;

    DPRINT1("usbxhci: registering xHCI miniport ver=%lu flags=%08lx ext=%Iu ep=%Iu xfer=%Iu res=%Iu\n",
            XhciRegPacket.MiniPortVersion,
            XhciRegPacket.MiniPortFlags,
            XhciRegPacket.MiniPortExtensionSize,
            XhciRegPacket.MiniPortEndpointSize,
            XhciRegPacket.MiniPortTransferSize,
            XhciRegPacket.MiniPortResourcesSize);

    Status = USBPORT_RegisterUSBPortDriver(DriverObject,
                                           USB20_MINIPORT_INTERFACE_VERSION,
                                           &XhciRegPacket);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("usbxhci: USBPORT_RegisterUSBPortDriver failed %lx\n", Status);
        DriverObject->DriverUnload = NULL;
    }

    return Status;
}

#define XHCI_STALL_INTERVAL_US 10

static
BOOLEAN
XHCI_WaitForRegisterBits(
    _In_ volatile ULONG *Reg,
    _In_ ULONG Mask,
    _In_ BOOLEAN WaitSet,
    _In_ ULONG TimeoutUs)
{
    ULONG loops;

    if (!Reg || !Mask)
        return FALSE;

    if (TimeoutUs == 0)
        TimeoutUs = XHCI_STALL_INTERVAL_US;

    loops = (TimeoutUs + (XHCI_STALL_INTERVAL_US - 1)) / XHCI_STALL_INTERVAL_US;

    while (loops--)
    {
        ULONG value = *Reg;

        if (WaitSet)
        {
            if ((value & Mask) == Mask)
                return TRUE;
        }
        else if ((value & Mask) == 0)
        {
            return TRUE;
        }

        KeStallExecutionProcessor(XHCI_STALL_INTERVAL_US);
    }

    return FALSE;
}

static
BOOLEAN
XHCI_IsValidPort(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port)
{
    if (!Extension || Port == 0)
        return FALSE;

    if (Extension->NumberOfPorts == 0)
        return FALSE;

    return Port <= Extension->NumberOfPorts;
}

static
volatile ULONG *
XHCI_GetPortStatusRegister(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port)
{
    if (!XHCI_IsValidPort(Extension, Port) || !Extension->OperationalRegisters)
        return NULL;

    return &Extension->OperationalRegisters->PortRegister[Port - 1].PortStatusAndControl;
}

static
BOOLEAN
XHCI_PortIsSuperSpeed(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port)
{
    volatile ULONG *PortStatusReg;
    ULONG PortValue;

    PortStatusReg = XHCI_GetPortStatusRegister(Extension, Port);
    if (!PortStatusReg)
        return FALSE;

    PortValue = READ_REGISTER_ULONG(PortStatusReg);
    return ((PortValue & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT) ==
           XHCI_PORTSC_SPEED_SUPER;
}

static
VOID
XHCI_RH_AckPortChange(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port,
    _In_ ULONG ChangeMask)
{
    volatile ULONG *PortStatusReg;

    if (!ChangeMask)
        return;

    PortStatusReg = XHCI_GetPortStatusRegister(Extension, Port);
    if (!PortStatusReg)
        return;

    WRITE_REGISTER_ULONG(PortStatusReg, ChangeMask & XHCI_PORTSC_WRITE_MASK);
}

static
MPSTATUS
XHCI_ModifyPortBits(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port,
    _In_ ULONG SetMask,
    _In_ ULONG ClearMask,
    _In_ ULONG AckMask)
{
    volatile ULONG *PortStatusReg;
    ULONG Value;
    ULONG NewValue;

    PortStatusReg = XHCI_GetPortStatusRegister(Extension, Port);
    if (!PortStatusReg)
        return MP_STATUS_ERROR;

    Value = READ_REGISTER_ULONG(PortStatusReg);
    NewValue = Value;

    NewValue |= SetMask;
    NewValue &= ~ClearMask;

    NewValue &= ~XHCI_PORTSC_CHANGE_MASK;
    NewValue |= (AckMask & XHCI_PORTSC_CHANGE_MASK);

    WRITE_REGISTER_ULONG(PortStatusReg, NewValue & XHCI_PORTSC_WRITE_MASK);
    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
XHCI_SetPortLinkState(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port,
    _In_ ULONG LinkState)
{
    volatile ULONG *PortStatusReg;
    ULONG Value;
    ULONG NewValue;

    PortStatusReg = XHCI_GetPortStatusRegister(Extension, Port);
    if (!PortStatusReg)
        return MP_STATUS_ERROR;

    Value = READ_REGISTER_ULONG(PortStatusReg);
    NewValue = Value & ~XHCI_PORTSC_PLS_MASK;
    NewValue |= XHCI_PORTSC_PLS(LinkState);
    NewValue |= XHCI_PORTSC_LWS;
    NewValue &= ~XHCI_PORTSC_CHANGE_MASK;

    WRITE_REGISTER_ULONG(PortStatusReg, NewValue & XHCI_PORTSC_WRITE_MASK);
    return MP_STATUS_SUCCESS;
}

static
VOID
XHCI_PowerOnAllPorts(
    _In_ PXHCI_EXTENSION Extension)
{
    USHORT Port;

    if (!Extension)
        return;

    if (!Extension->PortPowerControl)
        return;

    for (Port = 1; Port <= Extension->NumberOfPorts; Port++)
    {
        XHCI_RH_SetFeaturePortPower(Extension, Port);
    }
}

static
MPSTATUS
XHCI_ConfigurePageSize(
    _Inout_ PXHCI_EXTENSION Extension)
{
    volatile ULONG *PageSizeReg;
    ULONG Supported;

    if (!Extension || !Extension->OperationalRegisters)
        return MP_STATUS_ERROR;

    PageSizeReg = &Extension->OperationalRegisters->PageSize;
    Supported = READ_REGISTER_ULONG(PageSizeReg);

    if ((Supported & XHCI_PAGE_SIZE_4K) == 0)
    {
        DPRINT1("usbxhci: controller lacks 4KB page-size support (PS=0x%08lx)\n",
                Supported);
        return MP_STATUS_NOT_SUPPORTED;
    }

    WRITE_REGISTER_ULONG(PageSizeReg, XHCI_PAGE_SIZE_4K);
    Extension->ConfiguredPageSize = XHCI_PAGE_SIZE_4K;
    DPRINT1("usbxhci: configured page size mask=0x%08lx\n",
            Extension->ConfiguredPageSize);

    return MP_STATUS_SUCCESS;
}

static
VOID
XHCI_TryWarmResetPort(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port)
{
    volatile ULONG *PortStatusReg;
    ULONG PortValue;
    ULONG LinkState;

    if (!Extension)
        return;

    PortStatusReg = XHCI_GetPortStatusRegister(Extension, Port);
    if (!PortStatusReg)
        return;

    PortValue = READ_REGISTER_ULONG(PortStatusReg);

    if (!(PortValue & XHCI_PORTSC_CCS) ||
        (PortValue & XHCI_PORTSC_PED) ||
        (PortValue & XHCI_PORTSC_WPR))
    {
        return;
    }

    if (!XHCI_PortIsSuperSpeed(Extension, Port))
        return;

    LinkState = (PortValue & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;
    if (LinkState == PORT_LINK_STATE_RX_DETECT ||
        LinkState == PORT_LINK_STATE_POLLING ||
        LinkState == PORT_LINK_STATE_COMPLIANCE_MODE ||
        LinkState == PORT_LINK_STATE_INACTIVE)
    {
        DPRINT1("usbxhci: port %u stuck in link state %lu, issuing warm reset\n",
                Port,
                LinkState);
        XHCI_ModifyPortBits(Extension,
                            Port,
                            XHCI_PORTSC_WPR,
                            0,
                            XHCI_PORTSC_PLC | XHCI_PORTSC_PEC | XHCI_PORTSC_CEC);
    }
}

static
VOID
XHCI_CommandContextInit(
    _Out_ PXHCI_COMMAND_CONTEXT Context,
    _In_ ULONG CommandType)
{
    RtlZeroMemory(Context, sizeof(*Context));
    InitializeListHead(&Context->ListEntry);
    Context->CommandType = CommandType;
    Context->CompletionCode = XHCI_COMPLETION_SUCCESS;
    Context->Completed = FALSE;
    Context->InList = FALSE;
}

static
VOID
XHCI_CommandContextLink(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_COMMAND_CONTEXT Context)
{
    InsertTailList(&Extension->CommandContextList, &Context->ListEntry);
    Context->InList = TRUE;
}

static
VOID
XHCI_CommandContextUnlink(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_COMMAND_CONTEXT Context)
{
    if (!Context->InList)
        return;

    RemoveEntryList(&Context->ListEntry);
    InitializeListHead(&Context->ListEntry);
    Context->InList = FALSE;
}

static
PXHCI_COMMAND_CONTEXT
XHCI_CommandContextUnlinkByPointer(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ ULONGLONG CommandPointer)
{
    PLIST_ENTRY Entry;

    for (Entry = Extension->CommandContextList.Flink;
         Entry != &Extension->CommandContextList;
         Entry = Entry->Flink)
    {
        PXHCI_COMMAND_CONTEXT Context =
            CONTAINING_RECORD(Entry, XHCI_COMMAND_CONTEXT, ListEntry);

        if (Context->CommandPointer == CommandPointer)
        {
            XHCI_CommandContextUnlink(Extension, Context);
            return Context;
        }
    }

    return NULL;
}

static
VOID
XHCI_ResetCommandRingState(
    _In_ PXHCI_EXTENSION Extension)
{
    Extension->CommandRingEnqueueIndex = 0;
    Extension->CommandRingCycleState = 1;
}

static
PXHCI_TRB
XHCI_GetCommandRingTrb(
    _In_ PXHCI_EXTENSION Extension)
{
    if (!Extension->CommandRing || Extension->CommandRingTrbCount == 0)
        return NULL;

    return &Extension->CommandRing[Extension->CommandRingEnqueueIndex];
}

static __inline ULONG
XHCI_GetTrbType(
    _In_ const XHCI_TRB *Trb)
{
    return (Trb->Control & XHCI_TRB_TYPE_MASK) >> XHCI_TRB_TYPE_SHIFT;
}

static
VOID
XHCI_AdvanceCommandRing(
    _In_ PXHCI_EXTENSION Extension)
{
    if (!Extension->CommandRing || Extension->CommandRingTrbCount == 0)
        return;

    Extension->CommandRingEnqueueIndex++;
    if (Extension->CommandRingEnqueueIndex >= Extension->CommandRingTrbCount - 1)
    {
        Extension->CommandRingEnqueueIndex = 0;
        Extension->CommandRingCycleState ^= 1;
    }
}

static PXHCI_ENDPOINT
XHCI_GetSlotEndpoint(
    _In_ PXHCI_DEVICE_SLOT Slot,
    _In_ UCHAR EndpointId)
{
    if (!Slot)
        return NULL;

    if (EndpointId >= RTL_NUMBER_OF(Slot->EndpointTable))
        return NULL;

    return Slot->EndpointTable[EndpointId];
}

static
VOID
XHCI_RingEndpointDoorbell(
    _In_ PXHCI_EXTENSION Extension,
    _In_ UCHAR SlotId,
    _In_ UCHAR EndpointId,
    _In_ ULONG StreamId)
{
    ULONG Value;

    if (!Extension || !Extension->DoorbellArray)
        return;

    if (SlotId > XHCI_MAX_SLOTS)
        return;

    Value = EndpointId & 0x1F;
    Value |= (StreamId & 0xFFFF) << 16;
    WRITE_REGISTER_ULONG(&Extension->DoorbellArray->Doorbell[SlotId], Value);
}

static
PXHCI_TRB
XHCI_GetTransferRingTrb(
    _Inout_ PXHCI_RING Ring,
    _Out_opt_ PULONGLONG PhysicalAddress)
{
    ULONGLONG Address;

    if (!Ring || !Ring->Base || Ring->TrbCount < 2)
        return NULL;

    if (Ring->EnqueueIndex >= Ring->TrbCount - 1)
    {
        Ring->EnqueueIndex = 0;
        Ring->CycleState ^= 1;
    }

    Address = Ring->PhysicalAddress.QuadPart +
              ((ULONGLONG)Ring->EnqueueIndex * sizeof(XHCI_TRB));

    if (PhysicalAddress)
        *PhysicalAddress = Address;

    return &Ring->Base[Ring->EnqueueIndex];
}

static
VOID
XHCI_AdvanceTransferRing(
    _Inout_ PXHCI_RING Ring)
{
    if (!Ring || !Ring->Base || Ring->TrbCount == 0)
        return;

    Ring->EnqueueIndex++;
    if (Ring->EnqueueIndex >= Ring->TrbCount - 1)
    {
        Ring->EnqueueIndex = 0;
        Ring->CycleState ^= 1;
    }
}

static
VOID
XHCI_ResetEndpointRing(
    _Inout_ PXHCI_ENDPOINT Endpoint)
{
    if (!Endpoint)
        return;

    Endpoint->TransferRing.EnqueueIndex = 0;
    Endpoint->TransferRing.DequeueIndex = 0;
    Endpoint->TransferRing.CycleState = 1;
    Endpoint->ActiveTransfer = NULL;
}

static MPSTATUS
XHCI_AllocateTransferRing(
    _In_ PXHCI_EXTENSION Extension,
    _In_ ULONG TrbCount,
    _In_ BOOLEAN UseCommonBuffer,
    _Out_ PXHCI_RING Ring)
{
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS SkipBytes;
    SIZE_T Length;
    PXHCI_TRB Buffer;
    PXHCI_TRB LinkTrb;

    if (!Ring || TrbCount < 2)
        return MP_STATUS_ERROR;

    XHCI_LOG_IRQL("AllocateTransferRing entry");
    XHCI_ASSERT_PASSIVE("XHCI_AllocateTransferRing entry");

    RtlZeroMemory(Ring, sizeof(*Ring));

    Ring->TrbCount = TrbCount;
    Ring->UsesCommonBuffer = UseCommonBuffer;
    Ring->CycleState = 1;
    Ring->EnqueueIndex = 0;
    Ring->DequeueIndex = 0;
    Length = (SIZE_T)TrbCount * sizeof(XHCI_TRB);
    Ring->Length = Length;

    if (UseCommonBuffer)
    {
        // The caller should assign Ring->Base and Ring->PhysicalAddress directly.
        return MP_STATUS_SUCCESS;
    }

    LowAddress.QuadPart = 0;
    SkipBytes.QuadPart = 0;
    if (Extension && Extension->Supports64Bit &&
        !(Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA))
    {
        HighAddress.QuadPart = 0xFFFFFFFFFFFFFFFFULL;
    }
    else
    {
        HighAddress.QuadPart = 0xFFFFFFFFULL;
    }

    XHCI_ASSERT_PASSIVE("XHCI_AllocateTransferRing before MmAllocateContiguousMemorySpecifyCache");
    XHCI_LOG_IRQL("AllocateTransferRing before MmAllocateContiguousMemorySpecifyCache");
    Buffer = MmAllocateContiguousMemorySpecifyCache(Length,
                                                    LowAddress,
                                                    HighAddress,
                                                    SkipBytes,
                                                    MmCached);
    if (!Buffer)
        return MP_STATUS_NO_RESOURCES;

    RtlZeroMemory(Buffer, Length);
    Ring->Base = Buffer;
    Ring->PhysicalAddress = MmGetPhysicalAddress(Buffer);

    LinkTrb = &Ring->Base[TrbCount - 1];
    LinkTrb->Parameter1 = (ULONG)(Ring->PhysicalAddress.QuadPart & 0xFFFFFFFF);
    LinkTrb->Parameter2 = (ULONG)(Ring->PhysicalAddress.QuadPart >> 32);
    LinkTrb->Status = 0;
    LinkTrb->Control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                       XHCI_TRB_TOGGLE_CYCLE |
                       XHCI_TRB_CYCLE;

    return MP_STATUS_SUCCESS;
}

static VOID
XHCI_FreeTransferRing(
    _Inout_ PXHCI_RING Ring)
{
    if (!Ring)
        return;

    if (!Ring->UsesCommonBuffer && Ring->Base)
    {
        MmFreeContiguousMemory(Ring->Base);
    }

    RtlZeroMemory(Ring, sizeof(*Ring));
}

static VOID
XHCI_InitDeviceAddressMap(
    _Inout_ PXHCI_EXTENSION Extension)
{
    if (!Extension)
        return;

    RtlZeroMemory(Extension->DeviceAddressMap,
                  sizeof(Extension->DeviceAddressMap));
}

static VOID
XHCI_UpdateDeviceAddressMap(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_DEVICE_SLOT Slot,
    _In_ UCHAR NewAddress)
{
    if (!Extension || !Slot)
        return;

    if (Slot->UsbDeviceAddress < XHCI_MAX_DEVICE_ADDRESS &&
        Extension->DeviceAddressMap[Slot->UsbDeviceAddress] == Slot->SlotId)
    {
        Extension->DeviceAddressMap[Slot->UsbDeviceAddress] = 0;
    }

    Slot->UsbDeviceAddress = NewAddress;

    if (NewAddress < XHCI_MAX_DEVICE_ADDRESS)
        Extension->DeviceAddressMap[NewAddress] = Slot->SlotId;
}

static PXHCI_DEVICE_SLOT
XHCI_FindSlotByAddress(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT DeviceAddress)
{
    UCHAR SlotId;

    if (!Extension || DeviceAddress >= XHCI_MAX_DEVICE_ADDRESS)
        return NULL;

    SlotId = Extension->DeviceAddressMap[DeviceAddress];
    if (SlotId == 0 || SlotId > XHCI_MAX_SLOTS || SlotId > Extension->MaxSlots)
        return NULL;

    return XHCI_GetSlot(Extension, SlotId);
}

static PXHCI_DEVICE_SLOT
XHCI_FindSlotByPort(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT PortNumber)
{
    UCHAR SlotId;

    if (!Extension || PortNumber == 0)
        return NULL;

    for (SlotId = 1; SlotId <= Extension->MaxSlots && SlotId <= XHCI_MAX_SLOTS; SlotId++)
    {
        PXHCI_DEVICE_SLOT Slot = &Extension->DeviceSlots[SlotId];
        if (!Slot->InUse)
            continue;

        if (Slot->PortNumber == (UCHAR)PortNumber)
            return Slot;
    }

    return NULL;
}

static MPSTATUS
XHCI_ConfigureSlotEndpoint(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_DEVICE_SLOT Slot,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _In_ UCHAR EndpointId)
{
    PXHCI_INPUT_CONTEXT InputCtx;
    PXHCI_DEVICE_CONTEXT DeviceCtx;
    PXHCI_SLOT_CONTEXT SlotCtx;
    PXHCI_SLOT_CONTEXT ActiveSlotCtx;
    PXHCI_ENDPOINT_CONTEXT EpCtx;
    ULONG EndpointType;
    ULONG MaxPacketSize;
    ULONG BurstSize;
    ULONG Interval;
    ULONG MaxEsitPayload;
    ULONGLONG DequeuePtr;
    MPSTATUS Status;

    if (!Extension || !Slot || !Endpoint || EndpointId == 0)
        return MP_STATUS_ERROR;

    XHCI_LOG_IRQL("ConfigureSlotEndpoint entry");
    XHCI_ASSERT_PASSIVE("XHCI_ConfigureSlotEndpoint entry");

    InputCtx = (PXHCI_INPUT_CONTEXT)Slot->InputContext.VirtualAddress;
    DeviceCtx = (PXHCI_DEVICE_CONTEXT)Slot->DeviceContext.VirtualAddress;

    if (!InputCtx || !DeviceCtx)
        return MP_STATUS_ERROR;

    EndpointType = XHCI_GetEndpointTypeFromProperties(&Endpoint->EndpointProperties);
    if (EndpointType == XHCI_ENDPOINT_TYPE_INVALID)
        return MP_STATUS_ERROR;

    RtlZeroMemory(InputCtx, sizeof(XHCI_INPUT_CONTEXT));

    InputCtx->InputControlContext.AddContextFlags = (1 << 0) | (1 << EndpointId);
    InputCtx->InputControlContext.DropContextFlags = 0;

    ActiveSlotCtx = &DeviceCtx->SlotContext;
    SlotCtx = &InputCtx->SlotContext;
    RtlCopyMemory(SlotCtx, ActiveSlotCtx, sizeof(XHCI_SLOT_CONTEXT));

    if (XhciSlotContextGetLastCtx(SlotCtx) < EndpointId)
        XhciSlotContextSetLastCtx(SlotCtx, EndpointId);

    EpCtx = &InputCtx->EndpointContext[EndpointId - 1];
    RtlZeroMemory(EpCtx, sizeof(XHCI_ENDPOINT_CONTEXT));
    MaxPacketSize = Endpoint->EndpointProperties.MaxPacketSize ?
                    Endpoint->EndpointProperties.MaxPacketSize : 8;
    BurstSize = (Endpoint->EndpointProperties.TransactionPerMicroframe > 0) ?
                (Endpoint->EndpointProperties.TransactionPerMicroframe - 1) : 0;
    Interval = Endpoint->EndpointProperties.Period;
    MaxEsitPayload = Endpoint->EndpointProperties.MaxPacketSize ?
                     Endpoint->EndpointProperties.MaxPacketSize : MaxPacketSize;
    DequeuePtr =
        (Endpoint->TransferRing.PhysicalAddress.QuadPart & ~0xFULL) |
        (Endpoint->TransferRing.CycleState & 0x1);

    XhciEndpointContextInit(EpCtx,
                            EndpointType,
                            MaxPacketSize,
                            BurstSize,
                            Interval,
                            BurstSize & 0x3,
                            MaxEsitPayload,
                            Endpoint->EndpointProperties.MaxTransferSize ?
                                (Endpoint->EndpointProperties.MaxTransferSize & 0xFFFF) :
                                MaxPacketSize,
                            DequeuePtr);

    XHCI_ASSERT_PASSIVE("XHCI_ConfigureSlotEndpoint before XHCI_SendCommand");
    XHCI_LOG_IRQL("ConfigureSlotEndpoint before XHCI_SendCommand");

    if (Slot->Configured)
    {
        MPSTATUS StopStatus = XHCI_StopEndpoint(Extension, Slot, EndpointId);
        if (StopStatus != MP_STATUS_SUCCESS)
            DPRINT1("usbxhci: StopEndpoint failed for slot %u ep %u, continuing reconfigure\n",
                    Slot->SlotId,
                    EndpointId);
    }

    Status = XHCI_SendCommand(Extension,
                              Slot->Configured ? XHCI_TRB_TYPE_EVAL_CTX
                                               : XHCI_TRB_TYPE_CONFIG_EP,
                              Slot->InputContext.PhysicalAddress.QuadPart,
                              0,
                              XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                              XHCI_COMMAND_TIMEOUT_MS,
                              FALSE,
                              NULL,
                              NULL);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    Slot->Configured = TRUE;
    if (Slot->HighestEndpointId < EndpointId)
        Slot->HighestEndpointId = EndpointId;

    Slot->EndpointTable[EndpointId] = Endpoint;
    Endpoint->DoorbellTarget = EndpointId;

    if (Endpoint->TransferRing.PhysicalAddress.QuadPart)
    {
        MPSTATUS DeqStatus = XHCI_SetEndpointDequeue(Extension,
                                                     Slot,
                                                     EndpointId,
                                                     &Endpoint->TransferRing);
        if (DeqStatus != MP_STATUS_SUCCESS)
        {
            DPRINT1("usbxhci: SetTRDequeue failed for slot %u ep %u (status=%lx)\n",
                    Slot->SlotId,
                    EndpointId,
                    DeqStatus);
        }
        else
        {
            /* Kick the endpoint so hardware resumes at the freshly programmed dequeue. */
            XHCI_RingEndpointDoorbell(Extension, Slot->SlotId, EndpointId, 0);
        }
    }

    return MP_STATUS_SUCCESS;
}

static MPSTATUS
XHCI_DropSlotEndpoint(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_DEVICE_SLOT Slot,
    _In_ UCHAR EndpointId)
{
    PXHCI_INPUT_CONTEXT InputCtx;
    PXHCI_DEVICE_CONTEXT DeviceCtx;
    MPSTATUS Status;

    if (!Extension || !Slot || EndpointId == 0)
        return MP_STATUS_ERROR;

    InputCtx = (PXHCI_INPUT_CONTEXT)Slot->InputContext.VirtualAddress;
    DeviceCtx = (PXHCI_DEVICE_CONTEXT)Slot->DeviceContext.VirtualAddress;
    if (!InputCtx || !DeviceCtx)
        return MP_STATUS_ERROR;

    RtlZeroMemory(InputCtx, sizeof(XHCI_INPUT_CONTEXT));
    InputCtx->InputControlContext.DropContextFlags = (1 << EndpointId);
    InputCtx->InputControlContext.AddContextFlags = (1 << 0);
    RtlCopyMemory(&InputCtx->SlotContext,
                  &DeviceCtx->SlotContext,
                  sizeof(XHCI_SLOT_CONTEXT));

    Status = XHCI_SendCommand(Extension,
                              XHCI_TRB_TYPE_CONFIG_EP,
                              Slot->InputContext.PhysicalAddress.QuadPart,
                              0,
                              XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                              XHCI_COMMAND_TIMEOUT_MS,
                              FALSE,
                              NULL,
                              NULL);
    if (Status == MP_STATUS_SUCCESS)
        Slot->EndpointTable[EndpointId] = NULL;

    return Status;
}

static MPSTATUS
XHCI_StopEndpoint(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_DEVICE_SLOT Slot,
    _In_ UCHAR EndpointId)
{
    if (!Extension || !Slot || EndpointId == 0)
        return MP_STATUS_ERROR;

    return XHCI_SendCommand(Extension,
                            XHCI_TRB_TYPE_STOP_EP,
                            0,
                            0,
                            XHCI_COMMAND_SLOT_FIELD(Slot->SlotId) |
                                XHCI_COMMAND_EP_FIELD(EndpointId),
                            XHCI_COMMAND_TIMEOUT_MS,
                            TRUE,
                            NULL,
                            NULL);
}

static MPSTATUS
XHCI_ResetEndpoint(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_DEVICE_SLOT Slot,
    _In_ UCHAR EndpointId)
{
    if (!Extension || !Slot || EndpointId == 0)
        return MP_STATUS_ERROR;

    return XHCI_SendCommand(Extension,
                            XHCI_TRB_TYPE_RESET_EP,
                            0,
                            0,
                            XHCI_COMMAND_SLOT_FIELD(Slot->SlotId) |
                                XHCI_COMMAND_EP_FIELD(EndpointId),
                            XHCI_COMMAND_TIMEOUT_MS,
                            TRUE,
                            NULL,
                            NULL);
}

static MPSTATUS
XHCI_StartEndpoint(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_DEVICE_SLOT Slot,
    _In_ UCHAR EndpointId)
{
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Slot);
    UNREFERENCED_PARAMETER(EndpointId);

    /*
     * xHCI has no explicit START command. After Stop/Reset + SetTRDequeue,
     * ringing the doorbell transitions the endpoint back to Running.
     */
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
XHCI_SetEndpointDequeue(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_DEVICE_SLOT Slot,
    _In_ UCHAR EndpointId,
    _Inout_ PXHCI_RING Ring)
{
    if (!Extension || !Slot || !Ring || EndpointId == 0)
        return MP_STATUS_ERROR;

    ULONGLONG Dequeue = Ring->PhysicalAddress.QuadPart & ~0xFULL;
    ULONG DcsBit = Ring->CycleState & 0x1;

    return XHCI_SendCommand(Extension,
                            XHCI_TRB_TYPE_SET_DEQ,
                            Dequeue,
                            DcsBit,
                            XHCI_COMMAND_SLOT_FIELD(Slot->SlotId) |
                                XHCI_COMMAND_EP_FIELD(EndpointId),
                            XHCI_COMMAND_TIMEOUT_MS,
                            TRUE,
                            NULL,
                            NULL);
}

static VOID
XHCI_HandleEnumerationTransfer(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_ENDPOINT Endpoint,
    _In_ PXHCI_TRANSFER Transfer)
{
    USB_DEFAULT_PIPE_SETUP_PACKET *Setup;

    if (!Extension || !Endpoint || !Transfer)
        return;

    if ((Transfer->Flags & (XHCI_TRANSFER_FLAG_SET_ADDRESS | XHCI_TRANSFER_FLAG_GET_DESCRIPTOR)) == 0)
        return;

    if (Transfer->UsbdStatus != USBD_STATUS_SUCCESS)
        return;

    if (!Endpoint->Slot)
        return;

    if (!Transfer->TransferParameters)
        return;

    Setup = &Transfer->TransferParameters->SetupPacket;

    if (Transfer->Flags & XHCI_TRANSFER_FLAG_SET_ADDRESS)
    {
        XHCI_UpdateDeviceAddressMap(Extension,
                                    Endpoint->Slot,
                                    Transfer->NewAddress);

        DPRINT1("usbxhci: device on slot %u set address %u (bmR=0x%02x req=%02x)\n",
                Endpoint->Slot->SlotId,
                Transfer->NewAddress,
                Setup->bmRequestType.B,
                Setup->bRequest);
    }

    if (Transfer->Flags & XHCI_TRANSFER_FLAG_GET_DESCRIPTOR)
    {
        DPRINT1("usbxhci: GetDescriptor value=0x%04x index=0x%04x\n",
                Setup->wValue.W,
                Setup->wIndex.W);
    }
}

static MPSTATUS
XHCI_ResetDeviceOnPort(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT PortNumber)
{
    PXHCI_DEVICE_SLOT Slot;
    MPSTATUS Status;

    Slot = XHCI_FindSlotByPort(Extension, PortNumber);
    if (!Slot)
        return MP_STATUS_ERROR;

    Status = XHCI_SendCommand(Extension,
                              XHCI_TRB_TYPE_RESET_DEV,
                              0,
                              0,
                              XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                              XHCI_COMMAND_TIMEOUT_MS,
                              TRUE,
                              NULL,
                              NULL);
    return Status;
}

static UCHAR
XHCI_EndpointIdFromProperties(
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties)
{
    UCHAR EndpointNumber;
    UCHAR EndpointId;
    ULONG TransferType;

    if (!EndpointProperties)
        return 0;

    TransferType = EndpointProperties->TransferType;
    EndpointNumber = EndpointProperties->EndpointAddress & 0x0F;

    if (TransferType == USBPORT_TRANSFER_TYPE_CONTROL && EndpointNumber == 0)
        return 1;

    if (EndpointNumber == 0)
        return 0;

    EndpointId = (EndpointNumber << 1);

    if (EndpointProperties->Direction != USBPORT_TRANSFER_DIRECTION_OUT)
        EndpointId |= 1;

    if (EndpointId > XHCI_MAX_ENDPOINTS)
        return 0;

    return EndpointId;
}

static ULONG
XHCI_GetEndpointTypeFromProperties(
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties)
{
    ULONG TransferType;
    ULONG DirectionOut;

    if (!EndpointProperties)
        return XHCI_ENDPOINT_TYPE_INVALID;

    TransferType = EndpointProperties->TransferType;
    DirectionOut = (EndpointProperties->Direction == USBPORT_TRANSFER_DIRECTION_OUT);

    switch (TransferType)
    {
        case USBPORT_TRANSFER_TYPE_CONTROL:
            return XHCI_ENDPOINT_TYPE_CONTROL;
        case USBPORT_TRANSFER_TYPE_ISOCHRONOUS:
            return DirectionOut ? XHCI_ENDPOINT_TYPE_ISOCH_OUT : XHCI_ENDPOINT_TYPE_ISOCH_IN;
        case USBPORT_TRANSFER_TYPE_BULK:
            return DirectionOut ? XHCI_ENDPOINT_TYPE_BULK_OUT : XHCI_ENDPOINT_TYPE_BULK_IN;
        case USBPORT_TRANSFER_TYPE_INTERRUPT:
            return DirectionOut ? XHCI_ENDPOINT_TYPE_INTERRUPT_OUT : XHCI_ENDPOINT_TYPE_INTERRUPT_IN;
        default:
            return XHCI_ENDPOINT_TYPE_INVALID;
    }
}

static
VOID
XHCI_ServiceEventRing(
    _In_ PXHCI_EXTENSION Extension,
    _In_ BOOLEAN AcknowledgeInterrupt,
    _In_ BOOLEAN AllowCallbacks)
{
    ULONG Processed = 0;
    PXHCI_INTERRUPTER_REGISTER_SET Interrupter;
    BOOLEAN NotifyRootHub = FALSE;

    if (!Extension || !Extension->RuntimeRegisters ||
        !Extension->EventRing || Extension->EventRingTrbCount == 0 ||
        Extension->FatalError)
        return;

    while (TRUE)
    {
        PXHCI_TRB EventTrb = &Extension->EventRing[Extension->EventRingDequeueIndex];
        ULONG Cycle = EventTrb->Control & XHCI_TRB_CYCLE;
        ULONG TrbType;

        if (Cycle != Extension->EventRingCycleState)
            break;

        TrbType = XHCI_GetTrbType(EventTrb);

        DPRINT1("usbxhci: Event idx=%lu type=%lu ctrl=%08lx status=%08lx param=%08lx/%08lx AllowCb=%u\n",
                (ULONG)Extension->EventRingDequeueIndex,
                TrbType,
                EventTrb->Control,
                EventTrb->Status,
                EventTrb->Parameter1,
                EventTrb->Parameter2,
                AllowCallbacks ? 1 : 0);

        /*
         * When polling synchronously (AllowCallbacks == FALSE), avoid
         * invoking completion paths that may call back into USBPORT
         * at a time when it may be holding internal locks.
         * Specifically, skip TRANSFER_EVENT processing here; those
         * will be handled by the normal ISR/DPC path.
         */
        if (!AllowCallbacks && TrbType == XHCI_TRB_TYPE_TRANSFER_EVENT)
        {
            /* Do not consume transfer events while polling synchronously. */
            break;
        }

        switch (TrbType)
        {
            case XHCI_TRB_TYPE_TRANSFER_EVENT:
                XHCI_HandleTransferEvent(Extension, EventTrb);
                break;

            case XHCI_TRB_TYPE_COMMAND_COMPLETION:
                XHCI_HandleCommandCompletion(Extension, EventTrb);
                break;

            case XHCI_TRB_TYPE_PORT_STATUS_CHANGE:
                /* Record the change and defer hub notifications so we only
                 * ring USBPORT once per DPC, even if multiple ports changed. */
                XHCI_HandlePortStatusChangeEvent(Extension,
                                                 EventTrb,
                                                 FALSE);
                if (AllowCallbacks)
                    NotifyRootHub = TRUE;
                break;

            default:
                DPRINT1("usbxhci: unhandled event type %lu (ctrl=%08lx)\n",
                        TrbType,
                        EventTrb->Control);
                break;
        }

        Extension->EventRingDequeueIndex++;
        if (Extension->EventRingDequeueIndex >= Extension->EventRingTrbCount)
        {
            Extension->EventRingDequeueIndex = 0;
            Extension->EventRingCycleState ^= 1;
        }

        Processed++;
    }

    Extension->EventRingDequeuePointer =
        Extension->EventRingPhysical.QuadPart +
        ((ULONGLONG)Extension->EventRingDequeueIndex * sizeof(XHCI_TRB));

    /* Batch root-hub notifications so USBPORT only sees a single
     * invalidate call per DPC, even if several PORT_STATUS_CHANGE
     * events were serviced. */
    if (AllowCallbacks && NotifyRootHub && XhciRegPacket.UsbPortInvalidateRootHub)
    {
        XhciRegPacket.UsbPortInvalidateRootHub(Extension);
    }

    if (Processed || AcknowledgeInterrupt)
    {
        ULONG ErdpLow;
        BOOLEAN SetBusy = (Processed != 0) || AcknowledgeInterrupt;

        Interrupter = &Extension->RuntimeRegisters->Interrupter[0];
        WRITE_REGISTER_ULONG(&Interrupter->ErdpHigh,
                             (ULONG)(Extension->EventRingDequeuePointer >> 32));

        ErdpLow = (ULONG)(Extension->EventRingDequeuePointer & 0xFFFFFFFF);
        if (SetBusy)
            ErdpLow |= XHCI_ERDP_BUSY;

        WRITE_REGISTER_ULONG(&Interrupter->ErdpLow, ErdpLow);

        if (AcknowledgeInterrupt)
        {
            ULONG Iman = READ_REGISTER_ULONG(&Interrupter->Iman);
            Iman |= XHCI_IMAN_IP;
            WRITE_REGISTER_ULONG(&Interrupter->Iman, Iman);
        }
    }
}

static
BOOLEAN
XHCI_EventRingHasPendingTrb(
    _In_ PXHCI_EXTENSION Extension)
{
    PXHCI_TRB EventTrb;

    if (!Extension ||
        !Extension->EventRing ||
        Extension->EventRingTrbCount == 0)
    {
        return FALSE;
    }

    EventTrb = &Extension->EventRing[Extension->EventRingDequeueIndex];
    return ((EventTrb->Control & XHCI_TRB_CYCLE) ==
            Extension->EventRingCycleState);
}

static
VOID
XHCI_PollForWork(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ BOOLEAN AllowCallbacks)
{
    ULONG Pending;
    ULONG Iteration;

    if (!Extension || Extension->FatalError)
        return;

    for (Iteration = 0; Iteration < 4; Iteration++)
    {
        BOOLEAN DidWork = FALSE;

        Pending = (ULONG)InterlockedCompareExchange(
            (volatile LONG *)&Extension->PendingUsbSts,
            0,
            0);

        if (Pending || XHCI_InterruptService(Extension))
        {
            XHCI_InterruptDpc(Extension, FALSE);
            DidWork = TRUE;
        }

        if (XHCI_EventRingHasPendingTrb(Extension))
        {
            /*
             * Nothing latched in USBSTS (interrupts may be masked), but the
             * event ring still has work queued. Drain it so transfer
             * completions make progress even when we are polled.
             */
            XHCI_ServiceEventRing(Extension, FALSE, AllowCallbacks);
            DidWork = TRUE;
        }

        if (!DidWork)
            break;
    }
}

static
VOID
XHCI_DumpControllerState(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PCSTR Reason)
{
    PXHCI_OPERATIONAL_REGISTERS Ops;
    PXHCI_RUNTIME_REGISTERS Runtime;
    PXHCI_INTERRUPTER_REGISTER_SET Interrupter;
    ULONG UsbCmd, UsbSts, DnCtrl, Config;
    ULONGLONG Crcr, Dcbaap, ErstBase, Erdp;
    ULONG ErstSize;
    ULONG Port;

    if (!Extension || !Extension->OperationalRegisters)
        return;

    Ops = Extension->OperationalRegisters;
    UsbCmd = READ_REGISTER_ULONG(&Ops->UsbCmd);
    UsbSts = READ_REGISTER_ULONG(&Ops->UsbSts);
    DnCtrl = READ_REGISTER_ULONG(&Ops->DeviceNotificationControl);
    Config = READ_REGISTER_ULONG(&Ops->Config);
    Crcr = ((ULONGLONG)READ_REGISTER_ULONG(&Ops->CrCrHigh) << 32) |
           READ_REGISTER_ULONG(&Ops->CrCrLow);
    Dcbaap = ((ULONGLONG)READ_REGISTER_ULONG(&Ops->DcbaapHigh) << 32) |
             READ_REGISTER_ULONG(&Ops->DcbaapLow);

    Runtime = Extension->RuntimeRegisters;
    Interrupter = Runtime ? &Runtime->Interrupter[0] : NULL;

    if (Interrupter)
    {
        ErstSize = READ_REGISTER_ULONG(&Interrupter->ErstSize);
        ErstBase = ((ULONGLONG)READ_REGISTER_ULONG(&Interrupter->ErstBaseHigh) << 32) |
                   READ_REGISTER_ULONG(&Interrupter->ErstBaseLow);
        Erdp = ((ULONGLONG)READ_REGISTER_ULONG(&Interrupter->ErdpHigh) << 32) |
               READ_REGISTER_ULONG(&Interrupter->ErdpLow);
    }
    else
    {
        ErstSize = 0;
        ErstBase = 0;
        Erdp = 0;
    }

    DPRINT1("usbxhci: %s USBCMD=%08lx USBSTS=%08lx DNCTRL=%08lx CONFIG=%08lx\n",
            Reason, UsbCmd, UsbSts, DnCtrl, Config);
    DPRINT1("usbxhci: %s CRCR=%08lx:%08lx DCBAAP=%08lx:%08lx\n",
            Reason,
            (ULONG)(Crcr >> 32), (ULONG)(Crcr & 0xFFFFFFFF),
            (ULONG)(Dcbaap >> 32), (ULONG)(Dcbaap & 0xFFFFFFFF));
    DPRINT1("usbxhci: %s ERSTSZ=%lu ERSTBA=%08lx:%08lx ERDP=%08lx:%08lx\n",
            Reason,
            ErstSize,
            (ULONG)(ErstBase >> 32), (ULONG)(ErstBase & 0xFFFFFFFF),
            (ULONG)(Erdp >> 32), (ULONG)(Erdp & 0xFFFFFFFF));

    for (Port = 0; Port < Extension->NumberOfPorts && Port < 4; Port++)
    {
        ULONG PortSc = READ_REGISTER_ULONG(&Ops->PortRegister[Port].PortStatusAndControl);
        DPRINT1("usbxhci: %s PORT%lu=0x%08lx\n", Reason, Port + 1, PortSc);
    }
}

static
VOID
XHCI_RingCommandDoorbell(
    _In_ PXHCI_EXTENSION Extension)
{
    XHCI_RingEndpointDoorbell(Extension, 0, 0, 0);
}

static VOID
NTAPI
XHCI_Ep0BringupCallback(
    _In_ PVOID MiniportExtension,
    _In_ PVOID CallBackContext)
{
    UNREFERENCED_PARAMETER(MiniportExtension);
    PXHCI_EP0_BRINGUP_CTX Arg = (PXHCI_EP0_BRINGUP_CTX)CallBackContext;
    typedef struct _XHCI_EP0_WORK_WRAP {
        WORK_QUEUE_ITEM Item;
        XHCI_EP0_BRINGUP_CTX Ctx;
    } XHCI_EP0_WORK_WRAP, *PXHCI_EP0_WORK_WRAP;

    if (!Arg)
        return;

    XHCI_LOG_IRQL("Ep0BringupCallback entry");
    PXHCI_EP0_WORK_WRAP Wrap = ExAllocatePoolWithTag(NonPagedPool,
                                                     sizeof(*Wrap),
                                                     XHCI_TAG);
    if (!Wrap)
        return;

    RtlZeroMemory(Wrap, sizeof(*Wrap));
    Wrap->Ctx = *Arg;
    ExInitializeWorkItem(&Wrap->Item, XHCI_Ep0BringupWorker, Wrap);
    ExQueueWorkItem(&Wrap->Item, DelayedWorkQueue);
}

static VOID
NTAPI
XHCI_Ep0BringupWorker(
    _In_ PVOID Context)
{
    PXHCI_EP0_WORK_WRAP Wrap = (PXHCI_EP0_WORK_WRAP)Context;
    if (!Wrap)
        return;

    XHCI_LOG_IRQL("Ep0BringupWorker entry");
    XHCI_ASSERT_PASSIVE("XHCI_Ep0BringupWorker entry");

    PXHCI_EP0_BRINGUP_CTX Arg = &Wrap->Ctx;
    PXHCI_ENDPOINT Ep = Arg->Endpoint;
    PXHCI_EXTENSION Ext = Ep ? Ep->Extension : NULL;

    if (!Ext || !Ep)
        goto Exit;

    if (!Ep->Slot)
    {
        MPSTATUS WorkerStatus = XHCI_BringupDefaultControlEndpoint(Ext, Ep, &Arg->Props);
        DPRINT1("usbxhci: EP0 bring-up worker completed with %ld (slot=%u)\n",
                WorkerStatus,
                Ep->Slot ? Ep->SlotId : 0);
    }

Exit:
    ExFreePoolWithTag(Wrap, XHCI_TAG);
}

static
VOID
XHCI_HandleCommandCompletion(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_TRB EventTrb)
{
    ULONGLONG CommandPointer;
    ULONG CompletionCode;
    UCHAR SlotId;
    PXHCI_DEVICE_SLOT Slot;
    PXHCI_COMMAND_CONTEXT CommandContext = NULL;
    KIRQL OldIrql;

    CommandPointer = ((ULONGLONG)EventTrb->Parameter2 << 32) |
                     EventTrb->Parameter1;
    CompletionCode = XHCI_GET_COMPLETION_CODE(EventTrb->Status);
    SlotId = (UCHAR)XHCI_TRB_TO_SLOT_ID(EventTrb->Control);
    Slot = XHCI_GetSlot(Extension, SlotId);

    KeAcquireSpinLock(&Extension->CommandLock, &OldIrql);
    CommandContext = XHCI_CommandContextUnlinkByPointer(Extension, CommandPointer);
    KeReleaseSpinLock(&Extension->CommandLock, OldIrql);

    if (CommandContext)
    {
        CommandContext->CompletionCode = CompletionCode;
        CommandContext->SlotId = SlotId;
        CommandContext->Completed = TRUE;
    }

    DPRINT1("usbxhci: command completion code=%lu slot=%u cmdptr=%I64x\n",
            CompletionCode,
            SlotId,
            CommandPointer);

    if (!CommandContext)
    {
        DPRINT1("usbxhci: completion for unknown command pointer %I64x\n",
                CommandPointer);
        return;
    }

    if (CompletionCode == XHCI_COMPLETION_SUCCESS &&
        SlotId != 0 &&
        CommandContext->CommandType == XHCI_TRB_TYPE_ENABLE_SLOT)
    {
        XHCI_AssignSlot(Extension, SlotId);
    }
    else if (CompletionCode == XHCI_COMPLETION_SUCCESS &&
             SlotId != 0 &&
             CommandContext->CommandType == XHCI_TRB_TYPE_ADDRESS_DEV)
    {
        if (Slot)
        {
            Slot->Addressed = TRUE;
            DPRINT1("usbxhci: slot %u addressed\n", SlotId);
        }
    }
    else if (CompletionCode == XHCI_COMPLETION_SUCCESS &&
             Slot &&
             CommandContext->CommandType == XHCI_TRB_TYPE_CONFIG_EP)
    {
        Slot->Configured = TRUE;
        DPRINT1("usbxhci: slot %u configured\n", SlotId);
    }
    else if (CompletionCode == XHCI_COMPLETION_SUCCESS &&
             Slot &&
             CommandContext->CommandType == XHCI_TRB_TYPE_RESET_DEV)
    {
        Slot->Configured = FALSE;
        Slot->HighestEndpointId = 1;
        DPRINT1("usbxhci: slot %u reset\n", SlotId);
    }
}

static
VOID
XHCI_HandlePortStatusChangeEvent(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_TRB EventTrb,
    _In_ BOOLEAN NotifyHub)
{
    ULONG PortId;

    if (!Extension || !EventTrb)
        return;

    PortId = (EventTrb->Parameter1 >> 24) & 0xFF;
    if (PortId == 0 || PortId > Extension->NumberOfPorts)
    {
        DPRINT1("usbxhci: port status change event for invalid port %lu\n",
                PortId);
        return;
    }

    DPRINT("usbxhci: port status change event detected on port %lu\n", PortId);

    InterlockedOr((volatile LONG *)&Extension->PendingUsbSts, XHCI_USBSTS_PCD);

    XHCI_TryWarmResetPort(Extension, (USHORT)PortId);

    if (NotifyHub && XhciRegPacket.UsbPortInvalidateRootHub)
        XhciRegPacket.UsbPortInvalidateRootHub(Extension);
}

static VOID
XHCI_HandleTransferEvent(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_TRB EventTrb)
{
    ULONGLONG TrbPointer;
    ULONG CompletionCode;
    ULONG Remaining;
    ULONG BytesTransferred;
    UCHAR SlotId;
    UCHAR EndpointId;
    PXHCI_DEVICE_SLOT Slot;
    PXHCI_ENDPOINT Endpoint;
    PXHCI_TRANSFER Transfer;
    ULONG UsbdStatus;
    ULONG RequestedLength;

    if (!Extension || !EventTrb || Extension->FatalError)
        return;

    TrbPointer = ((ULONGLONG)EventTrb->Parameter2 << 32) |
                 EventTrb->Parameter1;
    CompletionCode = XHCI_GET_COMPLETION_CODE(EventTrb->Status);
    Remaining = EventTrb->Status & XHCI_TRB_LEN_MASK;
    SlotId = (UCHAR)XHCI_TRB_TO_SLOT_ID(EventTrb->Control);
    EndpointId = (UCHAR)XHCI_TRB_TO_EP_ID(EventTrb->Control);

    Slot = XHCI_GetSlot(Extension, SlotId);
    Endpoint = XHCI_GetSlotEndpoint(Slot, EndpointId);
    if (!Endpoint || !Endpoint->ActiveTransfer)
    {
        DPRINT1("usbxhci: transfer event slot=%u ep=%u has no active transfer (ptr=%I64x)\n",
                SlotId,
                EndpointId,
                TrbPointer);
        return;
    }

    Transfer = Endpoint->ActiveTransfer;
    Endpoint->ActiveTransfer = NULL;
    Endpoint->TransferRing.DequeueIndex = Endpoint->TransferRing.EnqueueIndex;

    if (Endpoint->DefaultControl && Endpoint->Slot)
    {
        Endpoint->Slot->Ep0RingCycleState = Endpoint->TransferRing.CycleState;
        Endpoint->Slot->Ep0RingEnqueueIndex = Endpoint->TransferRing.EnqueueIndex;
        Endpoint->Slot->Ep0RingDequeueIndex = Endpoint->TransferRing.DequeueIndex;
    }

    RequestedLength = Transfer->RequestedLength;
    if (RequestedLength == 0 && Transfer->TransferParameters)
        RequestedLength = Transfer->TransferParameters->TransferBufferLength;

    if (RequestedLength >= Remaining)
        BytesTransferred = RequestedLength - Remaining;
    else
        BytesTransferred = 0;

    switch (CompletionCode)
    {
        case XHCI_COMPLETION_SUCCESS:
        case XHCI_COMPLETION_SHORT_PACKET:
            UsbdStatus = USBD_STATUS_SUCCESS;
            break;
        case XHCI_COMPLETION_STALL_ERROR:
            UsbdStatus = USBD_STATUS_STALL_PID;
            break;
        default:
            UsbdStatus = USBD_STATUS_REQUEST_FAILED;
            break;
    }

    Transfer->CompletionTrbPointer = TrbPointer;
    Transfer->BytesTransferred = BytesTransferred;
    Transfer->UsbdStatus = UsbdStatus;

    /* Track aggregate bandwidth usage for periodic endpoints (iso/int). */
    if (Transfer->IsIsochronous ||
        Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
        Endpoint->TotalBytesTransferred += BytesTransferred;

    XHCI_HandleEnumerationTransfer(Extension, Endpoint, Transfer);

    if (Transfer->IsIsochronous && XhciRegPacket.UsbPortCompleteIsoTransfer)
    {
        XhciRegPacket.UsbPortCompleteIsoTransfer(Extension,
                                                 Endpoint,
                                                 Transfer->TransferParameters,
                                                 Transfer->BytesTransferred);
    }
    else if (XhciRegPacket.UsbPortCompleteTransfer)
    {
        XhciRegPacket.UsbPortCompleteTransfer(Extension,
                                              Endpoint,
                                              Transfer->TransferParameters,
                                              Transfer->UsbdStatus,
                                              Transfer->BytesTransferred);
    }

    /* TODO: If we ever start calling UsbPortInvalidateEndpoint from the xHCI
     * miniport (for example to nudge busy DMA endpoints), consider batching
     * those notifications similar to root-hub invalidation so USBPORT does not
     * see a storm of endpoint callbacks under heavy load. */
}

static
PXHCI_DEVICE_SLOT
XHCI_GetSlot(
    _In_ PXHCI_EXTENSION Extension,
    _In_ UCHAR SlotId)
{
    if (!Extension || SlotId == 0 || SlotId > XHCI_MAX_SLOTS)
        return NULL;

    return &Extension->DeviceSlots[SlotId];
}

static
MPSTATUS
XHCI_AssignSlot(
    _In_ PXHCI_EXTENSION Extension,
    _In_ UCHAR SlotId)
{
    PXHCI_DEVICE_SLOT Slot;

    Slot = XHCI_GetSlot(Extension, SlotId);
    if (!Slot)
        return MP_STATUS_ERROR;

    Slot->InUse = TRUE;
    Slot->Addressed = FALSE;
    Slot->Configured = FALSE;
    Slot->Ep0RingCycleState = 1;
    Slot->Ep0RingEnqueueIndex = 0;
    Slot->Ep0RingDequeueIndex = 0;
    Slot->UsbDeviceAddress = 0;
    Slot->PortNumber = 0;
    Slot->HighestEndpointId = 1;
    RtlZeroMemory(Slot->EndpointTable, sizeof(Slot->EndpointTable));

    if (Extension->HcResources)
    {
        Extension->HcResources->Dcbaa[SlotId] =
            Slot->DeviceContext.PhysicalAddress.QuadPart;
    }

    XHCI_UpdateDeviceAddressMap(Extension, Slot, 0);

    DPRINT1("usbxhci: slot %u assigned DCBAA=%I64x\n",
            SlotId,
            Slot->DeviceContext.PhysicalAddress.QuadPart);

    return MP_STATUS_SUCCESS;
}

static ULONG
XHCI_MapDeviceSpeed(
    _In_ USB_DEVICE_SPEED Speed)
{
    switch (Speed)
    {
        case UsbLowSpeed:
            return XHCI_PORTSC_SPEED_LOW;
        case UsbHighSpeed:
            return XHCI_PORTSC_SPEED_HIGH;
        case UsbSuperSpeed:
            return XHCI_PORTSC_SPEED_SUPER;
        case UsbFullSpeed:
        default:
            return XHCI_PORTSC_SPEED_FULL;
    }
}

static VOID
NTAPI
XHCI_QueryEndpointRequirements(
    _In_ PVOID MiniPortExtension,
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
    _Inout_ PUSBPORT_ENDPOINT_REQUIREMENTS Requirements)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);

    if (!EndpointProperties || !Requirements)
        return;

    Requirements->HeaderBufferSize = 0;
    switch (EndpointProperties->TransferType)
    {
        case USBPORT_TRANSFER_TYPE_CONTROL:
            Requirements->MaxTransferSize = 0x1000;    /* 4 KiB */
            break;
        case USBPORT_TRANSFER_TYPE_BULK:
        case USBPORT_TRANSFER_TYPE_INTERRUPT:
            Requirements->MaxTransferSize = 0x10000;   /* 64 KiB */
            break;
        case USBPORT_TRANSFER_TYPE_ISOCHRONOUS:
        default:
            Requirements->MaxTransferSize = 0x10000;
            break;
    }
}

static ULONG
NTAPI
XHCI_Get32BitFrameNumber(
    _In_ PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    if (!Extension || !Extension->RuntimeRegisters)
        return 0;
    {
        ULONG v = READ_REGISTER_ULONG(&Extension->RuntimeRegisters->MicroframeIndex);
        DPRINT1("usbxhci: Get32BitFrameNumber (IRQL=%lu) MFIDX=%08lx\n",
                (ULONG)KeGetCurrentIrql(), v);
        return v;
    }
}

static VOID
NTAPI
XHCI_InterruptNextSOF(
    _In_ PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    if (!Extension)
        return;
    DPRINT1("usbxhci: InterruptNextSOF (IRQL=%lu) InvalidateCtrl=%p\n",
            (ULONG)KeGetCurrentIrql(), XhciRegPacket.UsbPortInvalidateController);
    /*
     * This callback is invoked by USBPORT while holding its MiniportSpinLock
     * at DISPATCH_LEVEL. Do not process events or call into pageable paths
     * here. Just request a soft interrupt so our regular ISR/DPC flow runs.
     */
    if (XhciRegPacket.UsbPortInvalidateController)
        XhciRegPacket.UsbPortInvalidateController(Extension,
                                                  USBPORT_INVALIDATE_CONTROLLER_SOFT_INTERRUPT);
}

static VOID
NTAPI
XHCI_SetEndpointState(
    _In_ PVOID MiniPortExtension,
    _In_ PVOID EndpointHandle,
    _In_ ULONG State)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    PXHCI_ENDPOINT Endpoint = (PXHCI_ENDPOINT)EndpointHandle;

    if (!Extension || !Endpoint)
        return;

    switch (State)
    {
        case USBPORT_ENDPOINT_ACTIVE:
            /*
             * Resume a previously paused endpoint. For xHCI there is no
             * explicit START command; endpoints resume once their dequeue
             * pointer is programmed and the doorbell is rung. Clear-stall
             * paths are handled through SetEndpointStatus, so there is
             * nothing mandatory to do here for now.
             */
            break;

        case USBPORT_ENDPOINT_PAUSED:
            /*
             * USBPORT uses this to throttle traffic. Stop the endpoint so
             * hardware stops consuming TRBs from its ring.
             */
            if (Endpoint->Slot && Endpoint->EndpointId != 0)
            {
                XHCI_StopEndpoint(Extension,
                                  Endpoint->Slot,
                                  Endpoint->EndpointId);
            }
            break;

        case USBPORT_ENDPOINT_REMOVE:
            if (Endpoint->Slot && !Endpoint->DefaultControl &&
                Endpoint->EndpointId < RTL_NUMBER_OF(Endpoint->Slot->EndpointTable))
            {
                XHCI_DropSlotEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
            }
            break;

        default:
            break;
    }
}

static ULONG
NTAPI
XHCI_GetEndpointState(
    _In_ PVOID MiniPortExtension,
    _In_ PVOID EndpointHandle)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    PXHCI_ENDPOINT Endpoint = (PXHCI_ENDPOINT)EndpointHandle;
    PXHCI_DEVICE_SLOT Slot;
    PXHCI_DEVICE_CONTEXT DeviceCtx;
    PXHCI_ENDPOINT_CONTEXT EpCtx;
    ULONG EpState;

    if (!Extension || !Endpoint)
        return USBPORT_ENDPOINT_UNKNOWN;

    Slot = Endpoint->Slot;
    if (!Slot || Endpoint->EndpointId == 0 ||
        Endpoint->EndpointId >= RTL_NUMBER_OF(Slot->EndpointTable) ||
        Slot->EndpointTable[Endpoint->EndpointId] != Endpoint)
    {
        /* The endpoint is not currently configured in hardware. */
        return USBPORT_ENDPOINT_PAUSED;
    }

    DeviceCtx = (PXHCI_DEVICE_CONTEXT)Slot->DeviceContext.VirtualAddress;
    if (!DeviceCtx)
        return USBPORT_ENDPOINT_UNKNOWN;

    EpCtx = &DeviceCtx->EndpointContext[Endpoint->EndpointId - 1];
    EpState = EpCtx->EpInfo & XHCI_EPCTX_STATE_MASK;

    switch (EpState)
    {
        case XHCI_EPCTX_STATE_RUNNING:
            return USBPORT_ENDPOINT_ACTIVE;

        case XHCI_EPCTX_STATE_HALTED:
        case XHCI_EPCTX_STATE_STOPPED:
        case XHCI_EPCTX_STATE_ERROR:
            return USBPORT_ENDPOINT_PAUSED;

        case XHCI_EPCTX_STATE_DISABLED:
        default:
            return USBPORT_ENDPOINT_UNKNOWN;
    }
}

static ULONG
XHCI_BuildRouteString(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties)
{
    ULONG Route;

    if (!EndpointProperties)
        return 0;

    /*
     * Root-port devices encode the root port number directly in bits [3:0].
     * Devices behind hubs inherit their parent's route string and append the
     * downstream port number (one nibble per tier, up to five tiers).
     */
    if (EndpointProperties->HubAddr == USBPORT_NO_HUB_ADDRESS ||
        EndpointProperties->HubAddr == 0)
    {
        return (ULONG)(EndpointProperties->PortNumber & 0xF);
    }

    if (!Extension)
        return 0;

    PXHCI_DEVICE_SLOT HubSlot =
        XHCI_FindSlotByAddress(Extension, EndpointProperties->HubAddr);
    if (!HubSlot || !HubSlot->InUse)
    {
        DPRINT1("usbxhci: missing hub slot for address %u\n",
                EndpointProperties->HubAddr);
        return (ULONG)(EndpointProperties->PortNumber & 0xF);
    }

    Route = HubSlot->RouteString & 0xFFFFF;
    if ((Route & 0xF0000) != 0)
    {
        DPRINT1("usbxhci: route depth overflow for hub addr %u\n",
                EndpointProperties->HubAddr);
        return Route;
    }

    Route <<= 4;
    Route |= (EndpointProperties->PortNumber & 0xF);
    return Route & 0xFFFFF;
}

static VOID
XHCI_BuildErstTable(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONG Index;

    if (!Extension || !Extension->ErstTable)
        return;

    if (Extension->ErstEntryCount == 0)
        Extension->ErstEntryCount = 1;

    if (Extension->ErstEntryCount > XHCI_ERST_MAX_ENTRIES)
        Extension->ErstEntryCount = XHCI_ERST_MAX_ENTRIES;

    RtlZeroMemory(Extension->ErstTable,
                  sizeof(XHCI_ERST_ENTRY) * XHCI_ERST_MAX_ENTRIES);

    for (Index = 0; Index < Extension->ErstEntryCount; Index++)
    {
        ULONGLONG SegmentBase = Extension->EventRingPhysical.QuadPart +
            ((ULONGLONG)Index * XHCI_EVENT_RING_SEGMENT_TRBS * sizeof(XHCI_TRB));

        Extension->ErstTable[Index].RingSegmentBaseAddress = SegmentBase;
        Extension->ErstTable[Index].RingSegmentSize = XHCI_EVENT_RING_SEGMENT_TRBS;
        Extension->ErstTable[Index].Reserved = 0;
    }
}

static VOID
XHCI_PrepareDefaultControlContext(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_DEVICE_SLOT Slot,
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties)
{
    PXHCI_INPUT_CONTEXT InputCtx;
    PXHCI_DEVICE_CONTEXT DeviceCtx;
    PXHCI_SLOT_CONTEXT SlotCtx;
    PXHCI_ENDPOINT_CONTEXT EpCtx;
    ULONG SpeedCode;
    ULONG MaxPacketSize;
    ULONG RouteString;

    if (!Slot)
        return;

    InputCtx = (PXHCI_INPUT_CONTEXT)Slot->InputContext.VirtualAddress;
    DeviceCtx = (PXHCI_DEVICE_CONTEXT)Slot->DeviceContext.VirtualAddress;
    if (!InputCtx || !DeviceCtx)
        return;

    RtlZeroMemory(DeviceCtx, sizeof(XHCI_DEVICE_CONTEXT));
    RtlZeroMemory(InputCtx, sizeof(XHCI_INPUT_CONTEXT));

    InputCtx->InputControlContext.AddContextFlags = (1 << 0) | (1 << 1);
    InputCtx->InputControlContext.DropContextFlags = 0;

    SlotCtx = &InputCtx->SlotContext;
    SpeedCode = XHCI_MapDeviceSpeed(EndpointProperties->DeviceSpeed);
    RouteString = XHCI_BuildRouteString(Extension, EndpointProperties);
    XhciSlotContextSetRoute(SlotCtx, RouteString);
    XhciSlotContextSetSpeed(SlotCtx, SpeedCode);
    XhciSlotContextSetHub(SlotCtx,
                          (EndpointProperties->HubAddr != USBPORT_NO_HUB_ADDRESS &&
                           EndpointProperties->HubAddr != 0));
    XhciSlotContextSetMtt(SlotCtx, FALSE);
    XhciSlotContextSetLastCtx(SlotCtx, 1);
    XhciSlotContextSetRootPort(SlotCtx, EndpointProperties->PortNumber & 0xFF);
    Slot->PortNumber = (UCHAR)EndpointProperties->PortNumber;
    Slot->RouteString = RouteString;

    EpCtx = &InputCtx->EndpointContext[0];
    MaxPacketSize = EndpointProperties->MaxPacketSize ?
                    EndpointProperties->MaxPacketSize : 8;

    if (Slot->Ep0TransferRing.PhysicalAddress.QuadPart)
    {
        ULONGLONG Dequeue =
            (Slot->Ep0TransferRing.PhysicalAddress.QuadPart & ~0xFULL) |
            (Slot->Ep0RingCycleState & 0x1);
        XhciEndpointContextInit(EpCtx,
                                XHCI_ENDPOINT_TYPE_CONTROL,
                                MaxPacketSize,
                                0,
                                0,
                                0,
                                MaxPacketSize,
                                MaxPacketSize,
                                Dequeue);
    }
    else
    {
        XhciEndpointContextInit(EpCtx,
                                XHCI_ENDPOINT_TYPE_CONTROL,
                                MaxPacketSize,
                                0,
                                0,
                                0,
                                MaxPacketSize,
                                MaxPacketSize,
                                XHCI_TRB_CYCLE);
    }

    DPRINT1("usbxhci: prepared input context for slot %u (MPS=%lu, speed=%lu)\n",
            Slot->SlotId,
            MaxPacketSize,
            SpeedCode);
}

static MPSTATUS
XHCI_InitializeScratchpads(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONG Index;
    ULONGLONG BufferBase;

    if (!Extension || !Extension->HcResources)
        return MP_STATUS_ERROR;

    RtlZeroMemory(Extension->HcResources->ScratchpadPointerArray,
                  sizeof(Extension->HcResources->ScratchpadPointerArray));

    if (Extension->ScratchpadCount == 0)
    {
        Extension->HcResources->Dcbaa[0] = 0;
        return MP_STATUS_SUCCESS;
    }

    BufferBase = Extension->HcResourcesPhysical.QuadPart +
                 FIELD_OFFSET(XHCI_HC_RESOURCES, ScratchpadBuffers);

    for (Index = 0; Index < Extension->ScratchpadCount; Index++)
    {
        Extension->HcResources->ScratchpadPointerArray[Index] =
            BufferBase + ((ULONGLONG)Index * PAGE_SIZE);
    }

    Extension->HcResources->Dcbaa[0] = Extension->ScratchpadArrayPhysical.QuadPart;
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
XHCI_BringupDefaultControlEndpoint(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties)
{
    MPSTATUS Status;
    UCHAR SlotId = 0;
    ULONG CompletionCode = 0;
    PXHCI_DEVICE_SLOT Slot;

    if (!Extension || !Endpoint || !EndpointProperties)
        return MP_STATUS_ERROR;

    if (Endpoint->Slot)
        return MP_STATUS_SUCCESS;

    Status = XHCI_SendCommand(Extension,
                              XHCI_TRB_TYPE_ENABLE_SLOT,
                              0,
                              0,
                              0,
                              XHCI_COMMAND_TIMEOUT_MS,
                              TRUE,
                              &SlotId,
                              &CompletionCode);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    Slot = XHCI_GetSlot(Extension, SlotId);
    if (!Slot || !Slot->InUse)
        return MP_STATUS_ERROR;

    XHCI_PrepareDefaultControlContext(Extension, Slot, EndpointProperties);

    Status = XHCI_SendCommand(Extension,
                              XHCI_TRB_TYPE_ADDRESS_DEV,
                              Slot->InputContext.PhysicalAddress.QuadPart,
                              0,
                              XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                              XHCI_COMMAND_TIMEOUT_MS,
                              TRUE,
                              &SlotId,
                              &CompletionCode);
    if (Status != MP_STATUS_SUCCESS)
    {
        /* Best-effort cleanup – ignore failure. */
        XHCI_SendCommand(Extension,
                         XHCI_TRB_TYPE_DISABLE_SLOT,
                         0,
                         0,
                         XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                         XHCI_COMMAND_TIMEOUT_MS,
                         FALSE,
                         NULL,
                         NULL);
        return Status;
    }

    Endpoint->Slot = Slot;
    Endpoint->SlotId = Slot->SlotId;
    Endpoint->EndpointId = 1;
    Endpoint->DoorbellTarget = 1;
    Endpoint->DefaultControl = TRUE;
    Endpoint->UsesStaticRing = TRUE;
    Endpoint->TransferRing.Base = Slot->Ep0TransferRing.VirtualAddress;
    Endpoint->TransferRing.PhysicalAddress = Slot->Ep0TransferRing.PhysicalAddress;
    Endpoint->TransferRing.Length = Slot->Ep0TransferRing.Length;
    Endpoint->TransferRing.TrbCount = XHCI_STATIC_EP_RING_TRBS;
    Endpoint->TransferRing.UsesCommonBuffer = TRUE;
    XHCI_ResetEndpointRing(Endpoint);

    Slot->Ep0RingCycleState = Endpoint->TransferRing.CycleState;
    Slot->Ep0RingEnqueueIndex = Endpoint->TransferRing.EnqueueIndex;
    Slot->Ep0RingDequeueIndex = Endpoint->TransferRing.DequeueIndex;
    if (Endpoint->EndpointId < RTL_NUMBER_OF(Slot->EndpointTable))
        Slot->EndpointTable[Endpoint->EndpointId] = Endpoint;

    XHCI_UpdateDeviceAddressMap(Extension, Slot, 0);
    return MP_STATUS_SUCCESS;
}

static
VOID
XHCI_InitDeviceSlots(
    _In_ PXHCI_EXTENSION Extension)
{
    ULONGLONG DeviceCtxBase;
    ULONGLONG InputCtxBase;
    ULONGLONG RingBase;
    ULONG SlotId;

    if (!Extension || !Extension->HcResources)
        return;

    DeviceCtxBase = Extension->HcResourcesPhysical.QuadPart +
                    FIELD_OFFSET(XHCI_HC_RESOURCES, DeviceContexts);
    InputCtxBase = Extension->HcResourcesPhysical.QuadPart +
                   FIELD_OFFSET(XHCI_HC_RESOURCES, InputContexts);
    RingBase = Extension->HcResourcesPhysical.QuadPart +
               FIELD_OFFSET(XHCI_HC_RESOURCES, Ep0TransferRings);

    Extension->DeviceContextsPhysical.QuadPart = DeviceCtxBase;
    Extension->InputContextsPhysical.QuadPart = InputCtxBase;
    Extension->Ep0RingArrayPhysical.QuadPart = RingBase;

    for (SlotId = 0; SlotId <= XHCI_MAX_SLOTS; SlotId++)
    {
        PXHCI_DEVICE_SLOT Slot = &Extension->DeviceSlots[SlotId];

        RtlZeroMemory(Slot, sizeof(*Slot));
        Slot->SlotId = (UCHAR)SlotId;

        Slot->DeviceContext.VirtualAddress =
            &Extension->HcResources->DeviceContexts[SlotId];
        Slot->DeviceContext.PhysicalAddress.QuadPart =
            DeviceCtxBase +
            ((ULONGLONG)SlotId * sizeof(XHCI_DEVICE_CONTEXT));
        Slot->DeviceContext.Length = sizeof(XHCI_DEVICE_CONTEXT);

        Slot->InputContext.VirtualAddress =
            &Extension->HcResources->InputContexts[SlotId];
        Slot->InputContext.PhysicalAddress.QuadPart =
            InputCtxBase +
            ((ULONGLONG)SlotId * sizeof(XHCI_INPUT_CONTEXT));
        Slot->InputContext.Length = sizeof(XHCI_INPUT_CONTEXT);

        Slot->Ep0TransferRing.VirtualAddress =
            &Extension->HcResources->Ep0TransferRings[SlotId][0];
        Slot->Ep0TransferRing.PhysicalAddress.QuadPart =
            RingBase +
            ((ULONGLONG)SlotId * sizeof(XHCI_TRB) * XHCI_STATIC_EP_RING_TRBS);
        Slot->Ep0TransferRing.Length = sizeof(XHCI_TRB) * XHCI_STATIC_EP_RING_TRBS;
        Slot->Ep0RingCycleState = 1;
        Slot->Ep0RingEnqueueIndex = 0;
        Slot->Ep0RingDequeueIndex = 0;

        if (Slot->Ep0TransferRing.VirtualAddress)
        {
            RtlZeroMemory(Slot->Ep0TransferRing.VirtualAddress,
                          Slot->Ep0TransferRing.Length);

            if (XHCI_STATIC_EP_RING_TRBS > 0)
            {
                PXHCI_TRB LinkTrb =
                    &Extension->HcResources->Ep0TransferRings[SlotId][XHCI_STATIC_EP_RING_TRBS - 1];
                ULONGLONG LinkAddress = Slot->Ep0TransferRing.PhysicalAddress.QuadPart;
                LinkTrb->Parameter1 = (ULONG)(LinkAddress & 0xFFFFFFFF);
                LinkTrb->Parameter2 = (ULONG)(LinkAddress >> 32);
                LinkTrb->Status = 0;
                LinkTrb->Control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                                   XHCI_TRB_TOGGLE_CYCLE |
                                   XHCI_TRB_CYCLE;
            }
        }

        if (Extension->HcResources)
            Extension->HcResources->Dcbaa[SlotId] = 0;
    }

    DPRINT1("usbxhci: initialized %u device slots\n", XHCI_MAX_SLOTS);
}

static VOID
XHCI_DetectHardwareQuirks(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONG Hcc;
    USHORT VendorId = 0;
    USHORT DeviceId = 0;

    if (!Extension || !Extension->CapabilityRegisters)
        return;

    Hcc = Extension->CapabilityRegisters->HccParams;
    Extension->Quirks = 0;

    if (!Extension->Supports64Bit)
        Extension->Quirks |= XHCI_QUIRK_FORCE_32BIT_DMA;
    if (!XHCI_HCC_LIGHT_RESET(Hcc))
        Extension->Quirks |= XHCI_QUIRK_SLOW_HARD_RESET;
    if (Extension->Resources && Extension->Resources->LegacySupport)
        Extension->Quirks |= XHCI_QUIRK_LEGACY_BIOS_HANDOFF;
    if (!XHCI_HCC_PORT_INDICATORS(Hcc))
        Extension->Quirks |= XHCI_QUIRK_NO_PORT_INDICATORS;
    if (Extension->HciVersion <= 0x0100)
        Extension->Quirks |= XHCI_QUIRK_LIMIT_U1U2;

    /*
     * Inspect PCI VID/DID for a few well-known controllers that need
     * additional quirks beyond the generic capability-based ones.
     * This does not change behavior yet, but makes it easy to hook
     * future vendor-specific workarounds in a single place.
     */
    if (XHCI_ReadPciConfig(Extension, 0x00, &VendorId, sizeof(VendorId)) &&
        XHCI_ReadPciConfig(Extension, 0x02, &DeviceId, sizeof(DeviceId)))
    {
        DPRINT1("usbxhci: PCI VID=%04x DID=%04x HciVer=%04x\n",
                VendorId,
                DeviceId,
                Extension->HciVersion);

        /* Example: NEC/Renesas often requires strict 32‑bit DMA. */
        if ((VendorId == 0x1033 || VendorId == 0x1912) &&
            !(Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA))
        {
            Extension->Quirks |= XHCI_QUIRK_FORCE_32BIT_DMA;
        }

        /* Example: early Intel Series 7/8 controllers can be slow to reset. */
        if (VendorId == 0x8086 && Extension->HciVersion <= 0x0100)
        {
            Extension->Quirks |= XHCI_QUIRK_SLOW_HARD_RESET;
        }
    }

    DPRINT1("usbxhci: quirks=0x%lx (32b=%u slow=%u legacy=%u nopid=%u limitU=%u)\n",
            Extension->Quirks,
            (Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA) ? 1 : 0,
            (Extension->Quirks & XHCI_QUIRK_SLOW_HARD_RESET) ? 1 : 0,
            (Extension->Quirks & XHCI_QUIRK_LEGACY_BIOS_HANDOFF) ? 1 : 0,
            (Extension->Quirks & XHCI_QUIRK_NO_PORT_INDICATORS) ? 1 : 0,
            (Extension->Quirks & XHCI_QUIRK_LIMIT_U1U2) ? 1 : 0);
}

static ULONG
XHCI_FindExtendedCapability(
    _In_ PXHCI_EXTENSION Extension,
    _In_ UCHAR CapabilityId)
{
    ULONG Offset;
    ULONG CapValue;
    UCHAR Next;
    PUCHAR Base;
    ULONG Iterations = 0;

    if (!Extension || !Extension->CapabilityRegisters)
        return 0;

    Offset = XHCI_HCC_EXT_CAP_PTR(Extension->CapabilityRegisters->HccParams);
    if (Offset == 0)
        return 0;

    /* HCC extended-capability pointer is in dwords, convert to bytes */
    Offset <<= 2;

    Base = (PUCHAR)Extension->CapabilityRegisters;

    while (Offset)
    {
        volatile ULONG *CapReg = (volatile ULONG *)(Base + Offset);

        CapValue = READ_REGISTER_ULONG(CapReg);
        if (XHCI_EXT_CAP_ID(CapValue) == CapabilityId)
            return Offset;

        Next = (UCHAR)XHCI_EXT_CAP_NEXT(CapValue);
        if (Next == 0)
            break;

        Offset += ((ULONG)Next * sizeof(ULONG));
        if (++Iterations > 64)
            break;
    }

    return 0;
}

static
VOID
XHCI_DisableLegacySupport(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONG Offset;
    volatile ULONG *LegacySupport;
    volatile ULONG *LegacyControl;
    ULONG Value;
    ULONG Retry;

    if (!Extension || !Extension->CapabilityRegisters || !Extension->Resources)
        return;

    Offset = XHCI_FindExtendedCapability(Extension, XHCI_EXT_CAP_ID_LEGACY);
    if (!Offset)
        return;

    LegacySupport = (volatile ULONG *)((PUCHAR)Extension->CapabilityRegisters +
                                        Offset + XHCI_LEGACY_SUPPORT_OFFSET);
    LegacyControl = (volatile ULONG *)((PUCHAR)Extension->CapabilityRegisters +
                                        Offset + XHCI_LEGACY_CONTROL_OFFSET);

    Value = READ_REGISTER_ULONG(LegacySupport);
    if ((Value & XHCI_HC_BIOS_OWNED) == 0)
        return;

    Extension->Resources->LegacySupport = 1;
    WRITE_REGISTER_ULONG(LegacySupport, Value | XHCI_HC_OS_OWNED);

    for (Retry = 0; Retry < 1000; Retry++)
    {
        Value = READ_REGISTER_ULONG(LegacySupport);
        if ((Value & XHCI_HC_BIOS_OWNED) == 0)
            break;

        KeStallExecutionProcessor(100);
    }

    Value = READ_REGISTER_ULONG(LegacyControl);
    Value &= ~(XHCI_LEGACY_DISABLE_SMI | XHCI_LEGACY_SMI_EVENTS);
    WRITE_REGISTER_ULONG(LegacyControl, Value);
}

static BOOLEAN
XHCI_ReadPciConfig(
    _In_ PXHCI_EXTENSION Extension,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    if (!Extension || !Buffer || Length == 0)
        return FALSE;
    if (!XhciRegPacket.UsbPortReadWriteConfigSpace)
        return FALSE;
    return (XhciRegPacket.UsbPortReadWriteConfigSpace(Extension,
                                                      FALSE,
                                                      Buffer,
                                                      Offset,
                                                      Length) == MP_STATUS_SUCCESS);
}

static VOID
XHCI_ProbeMsiMsix(
    _Inout_ PXHCI_EXTENSION Extension)
{
    UCHAR CapPtr;
    UCHAR Status;

    if (!Extension)
        return;

    Extension->MsiSupported = FALSE;
    Extension->MsixSupported = FALSE;
    Extension->MsiEnabled = FALSE;
    Extension->MsixEnabled = FALSE;
    Extension->MsiCapOffset = 0;
    Extension->MsixCapOffset = 0;

    /* Read PCI Status to check if capabilities list exists */
    if (!XHCI_ReadPciConfig(Extension, 0x06, &Status, sizeof(Status)))
        return;

    /* Bit 4 of Status indicates Capabilities List */
    if ((Status & 0x10) == 0)
        return;

    /* Read Capabilities Pointer (offset 0x34) */
    if (!XHCI_ReadPciConfig(Extension, 0x34, &CapPtr, sizeof(CapPtr)))
        return;

    /* Walk the capability list */
    for (int i = 0; i < 48 && CapPtr >= 0x40; i++)
    {
        UCHAR CapId = 0, Next = 0;
        if (!XHCI_ReadPciConfig(Extension, CapPtr + 0, &CapId, sizeof(CapId)))
            break;
        if (!XHCI_ReadPciConfig(Extension, CapPtr + 1, &Next, sizeof(Next)))
            break;

        if (CapId == PCI_CAPABILITY_ID_MSI && Extension->MsiCapOffset == 0)
        {
            USHORT MsiControl = 0;
            Extension->MsiCapOffset = CapPtr;
            Extension->MsiSupported = TRUE;
            /* Control at offset +2 */
            if (XHCI_ReadPciConfig(Extension, CapPtr + 2, &MsiControl, sizeof(MsiControl)))
            {
                Extension->MsiEnabled = (MsiControl & 0x0001) ? TRUE : FALSE;
                DPRINT1("usbxhci: MSI control=0x%04x MMC=%u enabled=%u\n",
                        MsiControl,
                        (MsiControl >> 1) & 0x7,
                        Extension->MsiEnabled ? 1 : 0);
            }
        }
        else if (CapId == PCI_CAPABILITY_ID_MSIX && Extension->MsixCapOffset == 0)
        {
            USHORT MsixControl = 0;
            Extension->MsixCapOffset = CapPtr;
            Extension->MsixSupported = TRUE;
            if (XHCI_ReadPciConfig(Extension, CapPtr + 2, &MsixControl, sizeof(MsixControl)))
            {
                Extension->MsixEnabled = (MsixControl & 0x8000) ? TRUE : FALSE; /* FMask bit is not enable; MSI-X enable is bit 15? Specs: bit 15 is Enable */
                DPRINT1("usbxhci: MSI-X control=0x%04x TableSize=%u enabled=%u\n",
                        MsixControl,
                        (MsixControl & 0x07FF) + 1,
                        (MsixControl & 0x8000) ? 1 : 0);
            }
        }

        if (Next == 0 || Next == CapPtr)
            break;
        CapPtr = Next;
    }

    DPRINT1("usbxhci: PCI caps: MSI %s (enabled=%u) MSI-X %s (enabled=%u)\n",
            Extension->MsiSupported ? "yes" : "no",
            Extension->MsiEnabled ? 1 : 0,
            Extension->MsixSupported ? "yes" : "no",
            Extension->MsixEnabled ? 1 : 0);
}


static
MPSTATUS
XHCI_QueueCommand(
    _In_ PXHCI_EXTENSION Extension,
    _In_ ULONG TrbType,
    _In_ ULONGLONG Parameter,
    _In_ ULONGLONG Context,
    _In_ ULONG ControlFlags,
    _Inout_ PXHCI_COMMAND_CONTEXT CommandContext)
{
    PXHCI_TRB Trb;
    ULONGLONG CommandPointer;

    Trb = XHCI_GetCommandRingTrb(Extension);
    if (!Trb)
        return MP_STATUS_NO_RESOURCES;

    Trb->Parameter1 = (ULONG)(Parameter & 0xFFFFFFFF);
    Trb->Parameter2 = (ULONG)(Parameter >> 32);
    Trb->Status = (ULONG)(Context & 0xFFFFFFFF);
    Trb->Control = (ULONG)(Context >> 32);
    Trb->Control &= ~XHCI_TRB_TYPE_MASK;
    Trb->Control |= (TrbType << XHCI_TRB_TYPE_SHIFT) |
                    (Extension->CommandRingCycleState & XHCI_TRB_CYCLE) |
                    ControlFlags;

    CommandPointer = Extension->CommandRingPhysical.QuadPart +
                     ((ULONGLONG)Extension->CommandRingEnqueueIndex * sizeof(XHCI_TRB));

    if (CommandContext)
    {
        CommandContext->CommandPointer = CommandPointer;
        CommandContext->SlotId = 0;
        CommandContext->CompletionCode = XHCI_COMPLETION_SUCCESS;
        CommandContext->Completed = FALSE;
        XHCI_CommandContextLink(Extension, CommandContext);
    }

    DPRINT1("usbxhci: queue command type=%lu cmdptr=%I64x\n",
            TrbType,
            CommandPointer);

    XHCI_AdvanceCommandRing(Extension);
    XHCI_RingCommandDoorbell(Extension);
    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
XHCI_SendCommand(
    _In_ PXHCI_EXTENSION Extension,
    _In_ ULONG TrbType,
    _In_ ULONGLONG Parameter,
    _In_ ULONGLONG Context,
    _In_ ULONG ControlFlags,
    _In_ ULONG TimeoutMs,
    _In_ BOOLEAN AllowRetry,
    _Out_opt_ PUCHAR SlotIdOut,
    _Out_opt_ PULONG CompletionCodeOut)
{
    ULONG Attempts;
    MPSTATUS Status = MP_STATUS_ERROR;
    KIRQL OldIrql;
    XHCI_COMMAND_CONTEXT CommandContext;

    /*
     * This helper may be called at DISPATCH_LEVEL (for example from
     * AbortTransfer / SetEndpointStatus paths).  It relies on
     * XHCI_WaitForCommandCompletion, which busy-polls using
     * KeStallExecutionProcessor instead of waiting on kernel
     * synchronization primitives, so it does not block callers at
     * elevated IRQL.
     */
    if (!Extension)
        return MP_STATUS_ERROR;

    Attempts = AllowRetry ? 2 : 1;

    while (Attempts--)
    {
        XHCI_CommandContextInit(&CommandContext, TrbType);

        KeAcquireSpinLock(&Extension->CommandLock, &OldIrql);
        Status = XHCI_QueueCommand(Extension,
                                   TrbType,
                                   Parameter,
                                   Context,
                                   ControlFlags,
                                   &CommandContext);
        KeReleaseSpinLock(&Extension->CommandLock, OldIrql);

        if (Status != MP_STATUS_SUCCESS)
            break;

        Status = XHCI_WaitForCommandCompletion(Extension,
                                               TimeoutMs,
                                               &CommandContext,
                                               SlotIdOut,
                                               CompletionCodeOut);
        if (Status == MP_STATUS_SUCCESS)
            break;

        KeAcquireSpinLock(&Extension->CommandLock, &OldIrql);
        XHCI_CommandContextUnlink(Extension, &CommandContext);
        KeReleaseSpinLock(&Extension->CommandLock, OldIrql);

        if (!AllowRetry || Status != MP_STATUS_HW_ERROR)
            break;

        DPRINT1("usbxhci: command type %lu timed out, retrying...\n", TrbType);
    }

    return Status;
}

static
MPSTATUS
XHCI_WaitForCommandCompletion(
    _In_ PXHCI_EXTENSION Extension,
    _In_ ULONG TimeoutMs,
    _Inout_ PXHCI_COMMAND_CONTEXT CommandContext,
    _Out_opt_ PUCHAR SlotIdOut,
    _Out_opt_ PULONG CompletionCodeOut)
{
    ULONG Remaining;

    if (!Extension || !CommandContext)
        return MP_STATUS_ERROR;

    if (Extension->FatalError)
        return MP_STATUS_HW_ERROR;

    Remaining = (TimeoutMs * 1000) / XHCI_COMMAND_POLL_INTERVAL_US;
    if (Remaining == 0)
        Remaining = 1;

    while (Remaining--)
    {
        if (!CommandContext->Completed)
        {
            XHCI_ServiceEventRing(Extension, FALSE, FALSE);
        }

        if (CommandContext->Completed)
            break;

        KeStallExecutionProcessor(XHCI_COMMAND_POLL_INTERVAL_US);

        if (Extension->FatalError)
            return MP_STATUS_HW_ERROR;
    }

    if (!CommandContext->Completed)
    {
        DPRINT1("usbxhci: command completion timed out\n");
        XHCI_HandleCommandTimeout(Extension, CommandContext->CommandType);
        return MP_STATUS_HW_ERROR;
    }

    if (SlotIdOut)
        *SlotIdOut = CommandContext->SlotId;
    if (CompletionCodeOut)
        *CompletionCodeOut = CommandContext->CompletionCode;

    if (CommandContext->CompletionCode == XHCI_COMPLETION_SUCCESS)
        return MP_STATUS_SUCCESS;

    DPRINT1("usbxhci: command completion error code=%lu slot=%u\n",
            CommandContext->CompletionCode,
            CommandContext->SlotId);
    return MP_STATUS_ERROR;
}

static
MPSTATUS
XHCI_ResetController(
    _In_ PXHCI_EXTENSION Extension)
{
    volatile ULONG *UsbCmd;
    volatile ULONG *UsbSts;
    ULONG command;
    ULONG ResetTimeout;
    ULONG ReadyTimeout;

    if (!Extension || !Extension->OperationalRegisters)
        return MP_STATUS_ERROR;

    UsbCmd = &Extension->OperationalRegisters->UsbCmd;
    UsbSts = &Extension->OperationalRegisters->UsbSts;

    command = READ_REGISTER_ULONG(UsbCmd);
    if (command & XHCI_USBCMD_RS)
    {
        WRITE_REGISTER_ULONG(UsbCmd, command & ~XHCI_USBCMD_RS);
        if (!XHCI_WaitForRegisterBits(UsbSts,
                                      XHCI_USBSTS_HCH,
                                      TRUE,
                                      XHCI_WAIT_HALT_US))
        {
            DPRINT1("usbxhci: controller failed to halt before reset\n");
            return MP_STATUS_HW_ERROR;
        }
    }

    ResetTimeout = (Extension->Quirks & XHCI_QUIRK_SLOW_HARD_RESET) ?
                   (XHCI_WAIT_RESET_US * 2) : XHCI_WAIT_RESET_US;
    ReadyTimeout = (Extension->Quirks & XHCI_QUIRK_SLOW_HARD_RESET) ?
                   (XHCI_WAIT_CNR_US * 2) : XHCI_WAIT_CNR_US;

    WRITE_REGISTER_ULONG(UsbCmd, XHCI_USBCMD_HCRST);

    if (!XHCI_WaitForRegisterBits(UsbCmd,
                                  XHCI_USBCMD_HCRST,
                                  FALSE,
                                  ResetTimeout))
    {
        DPRINT1("usbxhci: controller reset timed out\n");
        return MP_STATUS_HW_ERROR;
    }

    if (!XHCI_WaitForRegisterBits(UsbSts,
                                  XHCI_USBSTS_CNR,
                                  FALSE,
                                  ReadyTimeout))
    {
        DPRINT1("usbxhci: controller not ready after reset\n");
        return MP_STATUS_HW_ERROR;
    }

    return MP_STATUS_SUCCESS;
}

static VOID
XHCI_HandleControllerError(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ ULONG PendingStatus)
{
    if (!Extension)
        return;

    XHCI_DumpControllerState(Extension, "controller error");
    Extension->FatalError = TRUE;
    DPRINT1("usbxhci: controller error detected (USBSTS=%08lx)\n", PendingStatus);
    XHCI_ShutdownController(Extension, TRUE);
}

static VOID
XHCI_HandleCommandTimeout(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ ULONG CommandType)
{
    if (!Extension)
        return;

    XHCI_DumpControllerState(Extension, "command timeout");
    Extension->FatalError = TRUE;
    DPRINT1("usbxhci: command type %lu timed out -- forcing controller reset\n",
            CommandType);
    XHCI_ShutdownController(Extension, TRUE);
}

static MPSTATUS NTAPI
XHCI_SubmitTransfer(PVOID MiniPortExtension,
                    PVOID EndpointHandle,
                    PUSBPORT_TRANSFER_PARAMETERS TransferParameters,
                    PVOID TransferHandle,
                    PUSBPORT_SCATTER_GATHER_LIST SgList)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    PXHCI_ENDPOINT Endpoint = EndpointHandle;
    PXHCI_TRANSFER Transfer = TransferHandle;

    if (!Extension || !Endpoint || !Transfer || !TransferParameters)
        return MP_STATUS_ERROR;

    if (Extension->FatalError)
        return MP_STATUS_HW_ERROR;

    if (Endpoint->EndpointProperties.TransferType ==
            USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
    {
        return XHCI_SubmitIsoTransfer(Extension,
                                      Endpoint,
                                      TransferParameters,
                                      TransferHandle,
                                      SgList);
    }

    RtlZeroMemory(Transfer, sizeof(*Transfer));
    Transfer->Endpoint = Endpoint;
    Transfer->TransferParameters = TransferParameters;
    Transfer->SgList = SgList;
    Transfer->TransferHandle = TransferHandle;
    Transfer->RequestedLength = TransferParameters->TransferBufferLength;
    Transfer->UsbdStatus = USBD_STATUS_SUCCESS;
    Transfer->Flags = 0;
    Transfer->NewAddress = 0;
    Transfer->IsIsochronous = FALSE;

    switch (Endpoint->EndpointProperties.TransferType)
    {
        case USBPORT_TRANSFER_TYPE_CONTROL:
            return XHCI_SubmitControlTransfer(Extension, Endpoint, Transfer);

        case USBPORT_TRANSFER_TYPE_BULK:
        case USBPORT_TRANSFER_TYPE_INTERRUPT:
            return XHCI_SubmitBulkInterruptTransfer(Extension, Endpoint, Transfer);

        default:
            DPRINT1("usbxhci: transfer type %lu not supported on endpoint %u\n",
                    Endpoint->EndpointProperties.TransferType,
                    Endpoint->EndpointId);
            Transfer->UsbdStatus = USBD_STATUS_NOT_SUPPORTED;
            return MP_STATUS_NOT_SUPPORTED;
    }
}

static MPSTATUS
XHCI_SubmitControlTransfer(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _Inout_ PXHCI_TRANSFER Transfer)
{
    PUSBPORT_TRANSFER_PARAMETERS TransferParameters;
    PUSBPORT_SCATTER_GATHER_LIST SgList;
    BOOLEAN HasDataStage;
    BOOLEAN DataIn;
    BOOLEAN StatusIn;
    PXHCI_TRB Trb;
    ULONGLONG PhysicalAddress;
    ULONG Control;
    ULONG SetupLow;
    ULONG SetupHigh;
    ULONG Remaining;
    ULONGLONG BufferAddress;
    ULONG Chunk;
    MPSTATUS Status = MP_STATUS_SUCCESS;
    BOOLEAN ProgrammedRing = FALSE;

    if (!Extension || !Endpoint || !Transfer)
        return MP_STATUS_ERROR;

    if (!Endpoint->Slot || !Endpoint->TransferRing.Base)
    {
        if (Endpoint->DefaultControl)
        {
            if (KeGetCurrentIrql() <= PASSIVE_LEVEL)
            {
                Status = XHCI_BringupDefaultControlEndpoint(Extension,
                                                             Endpoint,
                                                             &Endpoint->EndpointProperties);
                if (Status != MP_STATUS_SUCCESS)
                    return Status;
            }
            else if (XhciRegPacket.UsbPortRequestAsyncCallback)
            {
                XHCI_EP0_BRINGUP_CTX Ctx;
                RtlZeroMemory(&Ctx, sizeof(Ctx));
                Ctx.Endpoint = Endpoint;
                Ctx.Props = Endpoint->EndpointProperties;
                XhciRegPacket.UsbPortRequestAsyncCallback(
                    Extension,
                    0,
                    &Ctx,
                    sizeof(Ctx),
                    XHCI_Ep0BringupCallback);
                return MP_STATUS_ERROR;
            }
        }

        if (!Endpoint->Slot || !Endpoint->TransferRing.Base)
            return MP_STATUS_ERROR;
    }

    if (Endpoint->ActiveTransfer)
    {
        DPRINT1("usbxhci: endpoint %u already has an active transfer\n",
                Endpoint->EndpointId);
        return MP_STATUS_FAILURE;
    }

    TransferParameters = Transfer->TransferParameters;
    SgList = Transfer->SgList;
    HasDataStage = (TransferParameters->TransferBufferLength != 0);
    DataIn = (TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN) ? TRUE : FALSE;
    Transfer->Flags = 0;
    Transfer->NewAddress = 0;
    Transfer->IsControl = TRUE;

    if (TransferParameters->SetupPacket.bmRequestType.B == 0 &&
        TransferParameters->SetupPacket.bRequest == USB_REQUEST_SET_ADDRESS)
    {
        Transfer->Flags |= XHCI_TRANSFER_FLAG_SET_ADDRESS;
        Transfer->NewAddress = (UCHAR)(TransferParameters->SetupPacket.wValue.W);
    }

    if (TransferParameters->SetupPacket.bRequest == USB_REQUEST_GET_DESCRIPTOR)
        Transfer->Flags |= XHCI_TRANSFER_FLAG_GET_DESCRIPTOR;

    /*
     * We currently only support a single, contiguous SG element for the
     * default control pipe.  USBPORT provides a single element for most
     * enumeration traffic, so this is sufficient for bring-up.
     */
    BufferAddress = 0;

    if (HasDataStage)
    {
        if (!SgList || SgList->SgElementCount == 0)
        {
            DPRINT1("usbxhci: missing SG list for control transfer\n");
            return MP_STATUS_NO_RESOURCES;
        }

        if (SgList->SgElementCount != 1 || SgList->SgElement[0].SgOffset != 0)
        {
            DPRINT1("usbxhci: complex SG list not supported for EP0 yet (count=%lu offset=%lu)\n",
                    SgList->SgElementCount,
                    SgList->SgElement[0].SgOffset);
            return MP_STATUS_NOT_SUPPORTED;
        }

        if (SgList->SgElement[0].SgTransferLength < TransferParameters->TransferBufferLength)
        {
            DPRINT1("usbxhci: SG element shorter (%lu) than transfer length (%lu)\n",
                    SgList->SgElement[0].SgTransferLength,
                    TransferParameters->TransferBufferLength);
            return MP_STATUS_ERROR;
        }

        BufferAddress = SgList->SgElement[0].SgPhysicalAddress.QuadPart;
    }
    else
    {
        BufferAddress = 0;
    }

    Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing, &PhysicalAddress);
    if (!Trb)
    {
        Status = MP_STATUS_NO_RESOURCES;
        goto Failure;
    }

    RtlCopyMemory(&SetupLow,
                  &TransferParameters->SetupPacket,
                  sizeof(ULONG));
    RtlCopyMemory(&SetupHigh,
                  ((PUCHAR)&TransferParameters->SetupPacket) + sizeof(ULONG),
                  sizeof(ULONG));

    Trb->Parameter1 = SetupLow;
    Trb->Parameter2 = SetupHigh;
    Trb->Status = sizeof(USB_DEFAULT_PIPE_SETUP_PACKET);
    Control = (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) |
              (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE) |
              XHCI_TRB_IDT;

    if (!HasDataStage)
        Control |= XHCI_TRB_TRT_NO_DATA;
    else if (DataIn)
        Control |= XHCI_TRB_TRT_IN;
    else
        Control |= XHCI_TRB_TRT_OUT;

    Trb->Control = Control;
    ProgrammedRing = TRUE;
    XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

    Remaining = TransferParameters->TransferBufferLength;
    while (Remaining)
    {
        Chunk = Remaining;
        if (Chunk > XHCI_MAX_TRB_TRANSFER_LENGTH)
            Chunk = XHCI_MAX_TRB_TRANSFER_LENGTH;

        Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing, &PhysicalAddress);
        if (!Trb)
        {
            Status = MP_STATUS_NO_RESOURCES;
            goto Failure;
        }

        Trb->Parameter1 = (ULONG)(BufferAddress & 0xFFFFFFFF);
        Trb->Parameter2 = (ULONG)(BufferAddress >> 32);
        Trb->Status = Chunk;
        Control = (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT) |
                  (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE);

        if (DataIn)
            Control |= XHCI_TRB_DIR_IN;

        if (Chunk < Remaining)
            Control |= XHCI_TRB_CHAIN_BIT;

        Trb->Control = Control;
        XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

        BufferAddress += Chunk;
        Remaining -= Chunk;
    }

    StatusIn = !HasDataStage ? TRUE : !DataIn;

    Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing, &PhysicalAddress);
    if (!Trb)
    {
        Status = MP_STATUS_NO_RESOURCES;
        goto Failure;
    }

    Trb->Parameter1 = 0;
    Trb->Parameter2 = 0;
    Trb->Status = 0;
    Control = (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) |
              (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE) |
              XHCI_TRB_IOC;

    if (StatusIn)
        Control |= XHCI_TRB_DIR_IN;

    Trb->Control = Control;
    Transfer->CompletionTrbPointer = PhysicalAddress;
    XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

    Endpoint->ActiveTransfer = Transfer;

    KeMemoryBarrier();
    XHCI_RingEndpointDoorbell(Extension,
                               Endpoint->SlotId,
                               Endpoint->DoorbellTarget,
                               0);

    return MP_STATUS_SUCCESS;

Failure:
    if (ProgrammedRing)
        XHCI_ResetEndpointRing(Endpoint);

    Transfer->UsbdStatus = USBD_STATUS_REQUEST_FAILED;
    Endpoint->ActiveTransfer = NULL;
    return Status;
}

static MPSTATUS
XHCI_SubmitBulkInterruptTransfer(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _Inout_ PXHCI_TRANSFER Transfer)
{
    PUSBPORT_TRANSFER_PARAMETERS TransferParameters;
    ULONG Remaining;
    PXHCI_TRB Trb = NULL;
    ULONGLONG PhysicalAddress = 0;
    ULONG Control;
    BOOLEAN DirectionIn;

    if (!Extension || !Endpoint || !Transfer)
        return MP_STATUS_ERROR;

    if (!Endpoint->Slot || !Endpoint->TransferRing.Base)
        return MP_STATUS_ERROR;

    if (Endpoint->ActiveTransfer)
        return MP_STATUS_FAILURE;

    TransferParameters = Transfer->TransferParameters;
    DirectionIn = (Endpoint->EndpointProperties.Direction != USBPORT_TRANSFER_DIRECTION_OUT) ? TRUE : FALSE;

    Remaining = TransferParameters->TransferBufferLength;

    if (Remaining == 0)
    {
        Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing, &PhysicalAddress);
        if (!Trb)
            return MP_STATUS_NO_RESOURCES;

        Trb->Parameter1 = 0;
        Trb->Parameter2 = 0;
        Trb->Status = 0;
        Control = (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) |
                  (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE) |
                  XHCI_TRB_IOC;
        if (DirectionIn)
            Control |= XHCI_TRB_DIR_IN;
        Trb->Control = Control;
        XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

        Transfer->CompletionTrbPointer = PhysicalAddress;
        Endpoint->ActiveTransfer = Transfer;
        Transfer->Flags = 0;
        Transfer->IsControl = FALSE;

        KeMemoryBarrier();
        XHCI_RingEndpointDoorbell(Extension,
                                   Endpoint->SlotId,
                                   Endpoint->DoorbellTarget,
                                   0);
        return MP_STATUS_SUCCESS;
    }

    return XHCI_SubmitSgTransfer(Extension,
                                 Endpoint,
                                 Transfer,
                                 XHCI_TRB_TYPE_NORMAL,
                                 FALSE);
}

static MPSTATUS
XHCI_SubmitSgTransfer(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _Inout_ PXHCI_TRANSFER Transfer,
    _In_ ULONG TrbType,
    _In_ BOOLEAN IsIsochronous)
{
    PUSBPORT_TRANSFER_PARAMETERS TransferParameters;
    PUSBPORT_SCATTER_GATHER_LIST SgList;
    ULONG Remaining;
    ULONG SgIndex;
    ULONG Chunk;
    ULONGLONG BufferAddress;
    PXHCI_TRB Trb = NULL;
    ULONGLONG PhysicalAddress = 0;
    ULONG Control;
    BOOLEAN DirectionIn;

    if (!Extension || !Endpoint || !Transfer)
        return MP_STATUS_ERROR;

    if (!Endpoint->Slot || !Endpoint->TransferRing.Base)
        return MP_STATUS_ERROR;

    if (Endpoint->ActiveTransfer)
        return MP_STATUS_FAILURE;

    TransferParameters = Transfer->TransferParameters;
    SgList = Transfer->SgList;

    DirectionIn =
        (Endpoint->EndpointProperties.Direction != USBPORT_TRANSFER_DIRECTION_OUT) ?
            TRUE : FALSE;

    Remaining = TransferParameters ?
                TransferParameters->TransferBufferLength : 0;
    SgIndex = 0;

    if (Remaining == 0)
    {
        Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing, &PhysicalAddress);
        if (!Trb)
            return MP_STATUS_NO_RESOURCES;

        Trb->Parameter1 = 0;
        Trb->Parameter2 = 0;
        Trb->Status = 0;
        Control = (TrbType << XHCI_TRB_TYPE_SHIFT) |
                  (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE) |
                  XHCI_TRB_IOC;
        if (DirectionIn)
            Control |= XHCI_TRB_DIR_IN;
        if (IsIsochronous)
            Control |= XHCI_TRB_SIA;

        Trb->Control = Control;
        XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

        Transfer->CompletionTrbPointer = PhysicalAddress;
        Endpoint->ActiveTransfer = Transfer;
        Transfer->Flags = 0;
        Transfer->IsControl = FALSE;

        KeMemoryBarrier();
        XHCI_RingEndpointDoorbell(Extension,
                                   Endpoint->SlotId,
                                   Endpoint->DoorbellTarget,
                                   0);
        return MP_STATUS_SUCCESS;
    }

    if (!SgList || SgList->SgElementCount == 0)
        return MP_STATUS_NO_RESOURCES;

    while (Remaining && SgIndex < SgList->SgElementCount)
    {
        ULONG ElementRemaining = SgList->SgElement[SgIndex].SgTransferLength;
        PHYSICAL_ADDRESS ElementAddress = SgList->SgElement[SgIndex].SgPhysicalAddress;
        ULONG ElementOffset = SgList->SgElement[SgIndex].SgOffset;

        if (ElementOffset)
            ElementAddress.QuadPart += ElementOffset;

        if (ElementOffset >= ElementRemaining)
            ElementRemaining = 0;
        else
            ElementRemaining -= ElementOffset;

        while (ElementRemaining && Remaining)
        {
            Chunk = ElementRemaining;
            if (Chunk > Remaining)
                Chunk = Remaining;
            if (Chunk > XHCI_MAX_TRB_TRANSFER_LENGTH)
                Chunk = XHCI_MAX_TRB_TRANSFER_LENGTH;

            Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing, &PhysicalAddress);
            if (!Trb)
                return MP_STATUS_NO_RESOURCES;

            BufferAddress = ElementAddress.QuadPart;

            Trb->Parameter1 = (ULONG)(BufferAddress & 0xFFFFFFFF);
            Trb->Parameter2 = (ULONG)(BufferAddress >> 32);
            Trb->Status = Chunk;
            Control = (TrbType << XHCI_TRB_TYPE_SHIFT) |
                      (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE);

            if (DirectionIn)
                Control |= XHCI_TRB_DIR_IN;

            if (IsIsochronous)
                Control |= XHCI_TRB_SIA;

            if (Chunk < Remaining || ElementRemaining > Chunk)
                Control |= XHCI_TRB_CHAIN_BIT;

            Trb->Control = Control;
            XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

            ElementAddress.QuadPart += Chunk;
            ElementRemaining -= Chunk;
            Remaining -= Chunk;
        }

        SgIndex++;
    }

    if (Remaining != 0)
    {
        DPRINT1("usbxhci: SG mapping smaller than transfer length\n");
        return MP_STATUS_ERROR;
    }

    if (Trb)
        Trb->Control |= XHCI_TRB_IOC;

    Transfer->CompletionTrbPointer = PhysicalAddress;
    Endpoint->ActiveTransfer = Transfer;
    Transfer->Flags = 0;
    Transfer->IsControl = FALSE;

    KeMemoryBarrier();
    XHCI_RingEndpointDoorbell(Extension,
                               Endpoint->SlotId,
                               Endpoint->DoorbellTarget,
                               0);

    return MP_STATUS_SUCCESS;
}

static MPSTATUS NTAPI
XHCI_OpenEndpoint(PVOID MiniPortExtension,
                  PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                  PVOID Endpoint)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    PXHCI_ENDPOINT XhciEndpoint = Endpoint;

    DPRINT1("usbxhci: OpenEndpoint enter EP=%p DevAddr=%u EptAddr=0x%02x Type=%u IRQL=%lu\n",
            XhciEndpoint,
            EndpointProperties ? EndpointProperties->DeviceAddress : 0xFFFF,
            EndpointProperties ? EndpointProperties->EndpointAddress : 0xFF,
            EndpointProperties ? EndpointProperties->TransferType : 0xFF,
            KeGetCurrentIrql());
    XHCI_LOG_IRQL("OpenEndpoint entry");

    if (!Extension || !EndpointProperties || !XhciEndpoint)
        return MP_STATUS_ERROR;

    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
        return XHCI_DeferEndpointOpen(Extension, XhciEndpoint, EndpointProperties);

    return XHCI_PerformEndpointOpen(Extension, XhciEndpoint, EndpointProperties);
}

#define XHCI_DEFERRED_OPEN_SPIN_DELAY_US 50

static MPSTATUS
XHCI_DeferEndpointOpen(PXHCI_EXTENSION Extension,
                       PXHCI_ENDPOINT Endpoint,
                       PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties)
{
    PXHCI_DEFERRED_OPEN_WORK Work;

    if (!Extension || !Endpoint || !EndpointProperties)
        return MP_STATUS_ERROR;

    if (Extension->FatalError)
        return MP_STATUS_HW_ERROR;

    Work = ExAllocatePoolWithTag(NonPagedPool,
                                 sizeof(*Work),
                                 XHCI_TAG);
    if (!Work)
        return MP_STATUS_NO_RESOURCES;

    RtlZeroMemory(Work, sizeof(*Work));
    Work->Endpoint = Endpoint;
    Work->Properties = *EndpointProperties;
    Work->Status = MP_STATUS_ERROR;
    KeInitializeEvent(&Work->CompletionEvent, NotificationEvent, FALSE);
    ExInitializeWorkItem(&Work->Item, XHCI_OpenEndpointWorker, Work);
    ExQueueWorkItem(&Work->Item, DelayedWorkQueue);

    while (!KeReadStateEvent(&Work->CompletionEvent))
    {
        KeStallExecutionProcessor(XHCI_DEFERRED_OPEN_SPIN_DELAY_US);
    }

    {
        MPSTATUS Status = Work->Status;
        ExFreePoolWithTag(Work, XHCI_TAG);
        return Status;
    }
}

static VOID NTAPI
XHCI_OpenEndpointWorker(PVOID Context)
{
    PXHCI_DEFERRED_OPEN_WORK Work = (PXHCI_DEFERRED_OPEN_WORK)Context;

    if (!Work)
        return;

    XHCI_LOG_IRQL("OpenEndpointWorker entry");
    XHCI_ASSERT_PASSIVE("XHCI_OpenEndpointWorker entry");

    if (Work->Endpoint && Work->Endpoint->Extension)
    {
        Work->Status = XHCI_PerformEndpointOpen(Work->Endpoint->Extension,
                                                Work->Endpoint,
                                                &Work->Properties);
    }
    else
    {
        Work->Status = MP_STATUS_ERROR;
    }

    KeSetEvent(&Work->CompletionEvent, IO_NO_INCREMENT, FALSE);
}

static MPSTATUS
XHCI_PerformEndpointOpen(PXHCI_EXTENSION Extension,
                         PXHCI_ENDPOINT XhciEndpoint,
                         PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties)
{
    MPSTATUS Status;
    BOOLEAN IsDefaultPipe;
    PXHCI_DEVICE_SLOT Slot;
    UCHAR EndpointId;
    ULONG EndpointType;

    XHCI_ASSERT_PASSIVE("XHCI_PerformEndpointOpen entry");

    if (Extension->FatalError)
        return MP_STATUS_HW_ERROR;

    RtlZeroMemory(XhciEndpoint, sizeof(*XhciEndpoint));
    XhciEndpoint->Extension = Extension;
    XhciEndpoint->EndpointProperties = *EndpointProperties;
    IsDefaultPipe = (EndpointProperties->EndpointAddress == 0 &&
                     EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_CONTROL);

    if (IsDefaultPipe && EndpointProperties->DeviceAddress == 0)
    {
        if (KeGetCurrentIrql() <= PASSIVE_LEVEL)
            return XHCI_BringupDefaultControlEndpoint(Extension, XhciEndpoint, EndpointProperties);

        if (XhciRegPacket.UsbPortRequestAsyncCallback)
        {
            XHCI_EP0_BRINGUP_CTX Ctx;
            RtlZeroMemory(&Ctx, sizeof(Ctx));
            Ctx.Endpoint = XhciEndpoint;
            Ctx.Props = *EndpointProperties;

            XhciRegPacket.UsbPortRequestAsyncCallback(
                Extension,
                0,
                &Ctx,
                sizeof(Ctx),
                XHCI_Ep0BringupCallback);

            DPRINT1("usbxhci: deferred EP0 bring-up from IRQL=%lu\n", KeGetCurrentIrql());
            return MP_STATUS_SUCCESS;
        }

        DPRINT1("usbxhci: unable to schedule EP0 bring-up (no callback)\n");
        return MP_STATUS_NOT_SUPPORTED;
    }

    if (IsDefaultPipe && EndpointProperties->DeviceAddress != 0)
    {
        Slot = XHCI_FindSlotByAddress(Extension, EndpointProperties->DeviceAddress);
        if (!Slot)
            return MP_STATUS_ERROR;

        XhciEndpoint->Slot = Slot;
        XhciEndpoint->SlotId = Slot->SlotId;
        XhciEndpoint->EndpointId = 1;
        XhciEndpoint->DoorbellTarget = 1;
        XhciEndpoint->DefaultControl = TRUE;
        XhciEndpoint->UsesStaticRing = TRUE;
        XhciEndpoint->TransferRing.Base = Slot->Ep0TransferRing.VirtualAddress;
        XhciEndpoint->TransferRing.PhysicalAddress = Slot->Ep0TransferRing.PhysicalAddress;
        XhciEndpoint->TransferRing.TrbCount = XHCI_STATIC_EP_RING_TRBS;
        XhciEndpoint->TransferRing.Length = Slot->Ep0TransferRing.Length;
        XhciEndpoint->TransferRing.CycleState = Slot->Ep0RingCycleState;
        XhciEndpoint->TransferRing.EnqueueIndex = Slot->Ep0RingEnqueueIndex;
        XhciEndpoint->TransferRing.DequeueIndex = Slot->Ep0RingDequeueIndex;
        XhciEndpoint->TransferRing.UsesCommonBuffer = TRUE;
        Slot->EndpointTable[1] = XhciEndpoint;
        return MP_STATUS_SUCCESS;
    }

    Slot = XHCI_FindSlotByAddress(Extension, EndpointProperties->DeviceAddress);
    if (!Slot)
    {
        DPRINT1("usbxhci: no slot for device address %u\n",
                EndpointProperties->DeviceAddress);
        return MP_STATUS_ERROR;
    }

    EndpointId = XHCI_EndpointIdFromProperties(EndpointProperties);
    if (EndpointId == 0)
        return MP_STATUS_ERROR;

    EndpointType = XHCI_GetEndpointTypeFromProperties(EndpointProperties);

    if (EndpointId < RTL_NUMBER_OF(Slot->EndpointTable) &&
        Slot->EndpointTable[EndpointId] != NULL)
    {
        PXHCI_ENDPOINT Existing = Slot->EndpointTable[EndpointId];

        DPRINT1("usbxhci: endpoint %u already configured on slot %u -- refreshing context via EvaluateContext\n",
                EndpointId,
                Slot->SlotId);

        if (Existing && Existing != XhciEndpoint && !Existing->UsesStaticRing)
        {
            XHCI_FreeTransferRing(&Existing->TransferRing);
        }

        Slot->EndpointTable[EndpointId] = NULL;
    }

    XhciEndpoint->Slot = Slot;
    XhciEndpoint->SlotId = Slot->SlotId;
    XhciEndpoint->EndpointId = EndpointId;
    XhciEndpoint->DoorbellTarget = EndpointId;
    XhciEndpoint->DefaultControl = FALSE;
    XhciEndpoint->UsesStaticRing = FALSE;
    XhciEndpoint->Isochronous =
        (EndpointType == XHCI_ENDPOINT_TYPE_ISOCH_OUT ||
         EndpointType == XHCI_ENDPOINT_TYPE_ISOCH_IN);

    Status = XHCI_AllocateTransferRing(Extension,
                                       XHCI_EXTERNAL_EP_RING_TRBS,
                                       FALSE,
                                       &XhciEndpoint->TransferRing);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    Status = XHCI_ConfigureSlotEndpoint(Extension,
                                        Slot,
                                        XhciEndpoint,
                                        EndpointId);
    if (Status != MP_STATUS_SUCCESS)
    {
        XHCI_FreeTransferRing(&XhciEndpoint->TransferRing);
        return Status;
    }

    return MP_STATUS_SUCCESS;
}

static VOID NTAPI
XHCI_CloseEndpoint(PVOID MiniPortExtension,
                   PVOID Endpoint,
                   BOOLEAN IsDoNotCallMiniport)
{
    PXHCI_ENDPOINT XhciEndpoint = Endpoint;
    UNREFERENCED_PARAMETER(MiniPortExtension);
    UNREFERENCED_PARAMETER(IsDoNotCallMiniport);

    if (!XhciEndpoint)
        return;

    if (XhciEndpoint->Slot &&
        XhciEndpoint->EndpointId < RTL_NUMBER_OF(XhciEndpoint->Slot->EndpointTable))
    {
        if (!XhciEndpoint->DefaultControl)
            XHCI_DropSlotEndpoint(XhciEndpoint->Extension,
                                  XhciEndpoint->Slot,
                                  XhciEndpoint->EndpointId);

        XhciEndpoint->Slot->EndpointTable[XhciEndpoint->EndpointId] = NULL;
    }

    if (!XhciEndpoint->UsesStaticRing)
        XHCI_FreeTransferRing(&XhciEndpoint->TransferRing);

    XhciEndpoint->Slot = NULL;
    XhciEndpoint->ActiveTransfer = NULL;
}

static MPSTATUS NTAPI
XHCI_StartController(PVOID MiniPortExtension,
                     PUSBPORT_RESOURCES UsbPortResources)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    PUCHAR Base;
    ULONG DbOffset;
    ULONG RtOffset;
    ULONG HcsParams1;
    ULONG HcsParams2;
    ULONG HcsParams3;
    ULONG HccParams;
    ULONG Port;
    MPSTATUS Status;

    if (!Extension || !UsbPortResources ||
        !(UsbPortResources->ResourcesTypes & USBPORT_RESOURCES_MEMORY) ||
        !UsbPortResources->ResourceBase)
    {
        DPRINT1("usbxhci: StartController missing resources\n");
        return MP_STATUS_NOT_SUPPORTED;
    }

    Extension->Signature = 'ICHX';
    Extension->FatalError = FALSE;
    Extension->ControllerRunning = FALSE;
    Extension->Quirks = 0;
    Extension->PortPowerControl = FALSE;
    Extension->PortIndicatorsSupported = FALSE;
    XHCI_InitDeviceAddressMap(Extension);
    Extension->Resources = UsbPortResources;
    Extension->MmioBase = UsbPortResources->ResourceBase;

    Base = (PUCHAR)Extension->MmioBase;
    Extension->CapabilityRegisters = (PXHCI_CAPABILITY_REGISTERS)Base;
    Extension->CapabilityLength = Extension->CapabilityRegisters->CapLength;
    DPRINT1("usbxhci: MMIO base=%p CAPLEN=%lu\n", Extension->MmioBase, Extension->CapabilityLength);
    if (Extension->CapabilityLength < sizeof(XHCI_CAPABILITY_REGISTERS))
    {
        DPRINT1("usbxhci: invalid CAPLENGTH %lu\n", Extension->CapabilityLength);
        return MP_STATUS_ERROR;
    }
    Extension->OperationalRegisters =
        (PXHCI_OPERATIONAL_REGISTERS)(Base + Extension->CapabilityLength);

    DbOffset = Extension->CapabilityRegisters->DbOff & ~0x3UL;
    RtOffset = Extension->CapabilityRegisters->Rtsoff & ~0x1FUL;

    Extension->DoorbellArray = (PXHCI_DOORBELL_ARRAY)(Base + DbOffset);
    Extension->RuntimeRegisters = (PXHCI_RUNTIME_REGISTERS)(Base + RtOffset);
    DPRINT1("usbxhci: DB offset=%lu RT offset=%lu Doorbell=%p Runtime=%p\n",
            DbOffset, RtOffset, Extension->DoorbellArray, Extension->RuntimeRegisters);

    XHCI_DisableLegacySupport(Extension);
    /* Probe for MSI/MSI-X capabilities */
    XHCI_ProbeMsiMsix(Extension);

    if (UsbPortResources->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE)
    {
        ULONG Messages = UsbPortResources->InterruptMessageCount ?
                         UsbPortResources->InterruptMessageCount : 1;
        DPRINT1("usbxhci: using message interrupts (%lu vector%s)\n",
                Messages,
                (Messages == 1) ? "" : "s");
    }
    else
    {
        DPRINT1("usbxhci: using legacy IRQ vector 0x%lx (IRQL=%lu)\n",
                UsbPortResources->InterruptVector,
                (ULONG)UsbPortResources->InterruptLevel);
    }

    Extension->PendingUsbSts = 0;
    Extension->RhIrqEnabled = TRUE;
    Extension->InterruptsEnabled = FALSE;

    if (!UsbPortResources->StartVA)
    {
        DPRINT1("usbxhci: StartController missing common-buffer VA\n");
        return MP_STATUS_NO_RESOURCES;
    }

    Extension->HcResources = (PXHCI_HC_RESOURCES)UsbPortResources->StartVA;
    Extension->HcResourcesPhysical.QuadPart = UsbPortResources->StartPA;
    RtlZeroMemory(Extension->HcResources, sizeof(XHCI_HC_RESOURCES));
    KeInitializeSpinLock(&Extension->CommandLock);
    InitializeListHead(&Extension->CommandContextList);

    Extension->DcbaaPhysical.QuadPart =
        Extension->HcResourcesPhysical.QuadPart +
        FIELD_OFFSET(XHCI_HC_RESOURCES, Dcbaa);

    Extension->ScratchpadArrayPhysical.QuadPart =
        Extension->HcResourcesPhysical.QuadPart +
        FIELD_OFFSET(XHCI_HC_RESOURCES, ScratchpadPointerArray);

    XHCI_InitDeviceSlots(Extension);

    Extension->CommandRing = Extension->HcResources->CommandRing;
    Extension->CommandRingPhysical.QuadPart =
        Extension->HcResourcesPhysical.QuadPart +
        FIELD_OFFSET(XHCI_HC_RESOURCES, CommandRing);
    Extension->CommandRingTrbCount = XHCI_COMMAND_RING_TRBS;
    Extension->CommandRingCycleState = 1;

    Extension->EventRing = Extension->HcResources->EventRing;
    Extension->EventRingPhysical.QuadPart =
        Extension->HcResourcesPhysical.QuadPart +
        FIELD_OFFSET(XHCI_HC_RESOURCES, EventRing);
    Extension->EventRingTrbCount = XHCI_EVENT_RING_TRBS;
    Extension->EventRingDequeueIndex = 0;
    Extension->EventRingCycleState = 1;
    Extension->EventRingDequeuePointer = Extension->EventRingPhysical.QuadPart;

    Extension->ErstTable = Extension->HcResources->ErstEntries;
    Extension->ErstTablePhysical.QuadPart =
        Extension->HcResourcesPhysical.QuadPart +
        FIELD_OFFSET(XHCI_HC_RESOURCES, ErstEntries);
    Extension->ErstEntryCount = XHCI_ERST_MAX_ENTRIES;

    RtlZeroMemory(Extension->CommandRing,
                  sizeof(XHCI_TRB) * Extension->CommandRingTrbCount);
    RtlZeroMemory(Extension->EventRing,
                  sizeof(XHCI_TRB) * Extension->EventRingTrbCount);
    RtlZeroMemory(Extension->ErstTable,
                  sizeof(XHCI_ERST_ENTRY) * XHCI_ERST_MAX_ENTRIES);

    XHCI_ResetCommandRingState(Extension);

    if (Extension->CommandRingTrbCount)
    {
        PXHCI_TRB LinkTrb = &Extension->CommandRing[Extension->CommandRingTrbCount - 1];
        ULONGLONG LinkAddress = Extension->CommandRingPhysical.QuadPart;

        LinkTrb->Parameter1 = (ULONG)(LinkAddress & 0xFFFFFFFF);
        LinkTrb->Parameter2 = (ULONG)(LinkAddress >> 32);
        LinkTrb->Status = 0;
        LinkTrb->Control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                           XHCI_TRB_TOGGLE_CYCLE |
                           XHCI_TRB_CYCLE;
    }


    HcsParams1 = Extension->CapabilityRegisters->HcsParams1;
    HcsParams2 = Extension->CapabilityRegisters->HcsParams2;
    HcsParams3 = Extension->CapabilityRegisters->HcsParams3;
    HccParams = Extension->CapabilityRegisters->HccParams;

    Extension->HciVersion = Extension->CapabilityRegisters->HciVersion;
    Extension->MaxSlots = XHCI_HCS1_MAX_SLOTS(HcsParams1);
    Extension->NumberOfPorts = XHCI_HCS1_MAX_PORTS(HcsParams1);
    for (Port = 0; Port <= XHCI_MAX_PORTS; Port++)
        Extension->PortLinkState[Port] = XHCI_INVALID_LINK_STATE;
    if (Extension->NumberOfPorts > XHCI_MAX_PORTS)
    {
        DPRINT1("usbxhci: clamping port count from %lu to %lu\n",
                Extension->NumberOfPorts,
                XHCI_MAX_PORTS);
        Extension->NumberOfPorts = XHCI_MAX_PORTS;
    }
    Extension->PortPowerControl = (BOOLEAN)XHCI_HCS1_PPC(HcsParams1);

    Extension->MaxScratchpadBuffers = XHCI_HCS2_MAX_SCRATCH(HcsParams2);
    Extension->Supports64Bit = (BOOLEAN)(XHCI_HCC_64BIT_ADDR(HccParams) != 0);
    Extension->ContextSize = XHCI_HCC_64B_CONTEXT(HccParams) ? 64 : 32;
    if (Extension->ContextSize != 32)
    {
        DPRINT1("usbxhci: 64-byte contexts are not supported yet\n");
        return MP_STATUS_NOT_SUPPORTED;
    }
    Extension->ScratchpadCount = Extension->MaxScratchpadBuffers;
    if (Extension->ScratchpadCount > XHCI_MAX_SCRATCHPADS)
    {
        DPRINT1("usbxhci: clamping scratchpad count from %lu to %u\n",
            Extension->ScratchpadCount,
            XHCI_MAX_SCRATCHPADS);
        Extension->ScratchpadCount = XHCI_MAX_SCRATCHPADS;
    }

    {
        ULONG ErstCapValue = XHCI_HCS2_ERST_MAX(HcsParams2);
        ULONG HwErstEntries;

        if (ErstCapValue > 16)
            ErstCapValue = 16;

        HwErstEntries = 1u << ErstCapValue;
        if (HwErstEntries == 0)
            HwErstEntries = 1;
        if (HwErstEntries > XHCI_ERST_MAX_ENTRIES)
            HwErstEntries = XHCI_ERST_MAX_ENTRIES;

        Extension->ErstEntryCount = HwErstEntries;
        Extension->EventRingTrbCount = Extension->ErstEntryCount *
                                       XHCI_EVENT_RING_SEGMENT_TRBS;
        XHCI_BuildErstTable(Extension);
    }

    Extension->PortIndicatorsSupported =
        (BOOLEAN)XHCI_HCC_PORT_INDICATORS(HccParams);
    if (Extension->Quirks & XHCI_QUIRK_NO_PORT_INDICATORS)
        Extension->PortIndicatorsSupported = FALSE;

    XHCI_DetectHardwareQuirks(Extension);

    DPRINT1("usbxhci: v%04x CAPLEN %lu slots %lu ports %lu scratch %lu ctx %lu 64b=%u\n",
            Extension->HciVersion,
            Extension->CapabilityLength,
            Extension->MaxSlots,
            Extension->NumberOfPorts,
            Extension->MaxScratchpadBuffers,
            Extension->ContextSize,
            Extension->Supports64Bit);

    DPRINT("usbxhci: HCS1 %08lx HCS2 %08lx HCS3 %08lx HCC %08lx\n",
           HcsParams1,
           HcsParams2,
           HcsParams3,
           HccParams);

    DPRINT("usbxhci: DCBAA PA %I64x ScratchArr PA %I64x (scratchpads=%lu)\n",
           (ULONGLONG)Extension->DcbaaPhysical.QuadPart,
           (ULONGLONG)Extension->ScratchpadArrayPhysical.QuadPart,
           Extension->ScratchpadCount);

    Status = XHCI_InitializeScratchpads(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    Status = XHCI_ResetController(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    Status = XHCI_ConfigurePageSize(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    XHCI_PowerOnAllPorts(Extension);

    if (Extension->OperationalRegisters)
    {
        ULONGLONG Dcbaa = Extension->DcbaaPhysical.QuadPart;
        ULONGLONG Crcr = Extension->CommandRingPhysical.QuadPart & ~0x3FULL;
        ULONG DcbaaLow = (ULONG)(Dcbaa & 0xFFFFFFFF);
        ULONG DcbaaHigh = (ULONG)(Dcbaa >> 32);
        ULONG CrcrLow = (ULONG)(Crcr & 0xFFFFFFFF);
        ULONG CrcrHigh = (ULONG)(Crcr >> 32);

        /* Sanity: TRB ring addresses must be 16-byte aligned */
#if DBG
        if ((Extension->CommandRingPhysical.QuadPart & 0xFULL) != 0)
        {
            DPRINT1("usbxhci: WARNING command ring not 16-byte aligned: %I64x\n",
                    (ULONGLONG)Extension->CommandRingPhysical.QuadPart);
        }
        if ((Extension->EventRingPhysical.QuadPart & 0xFULL) != 0)
        {
            DPRINT1("usbxhci: WARNING event ring not 16-byte aligned: %I64x\n",
                    (ULONGLONG)Extension->EventRingPhysical.QuadPart);
        }
#endif

        CrcrLow |= (Extension->CommandRingCycleState & 0x1);

        DPRINT1("usbxhci: programming DCBAA=%08lx:%08lx CRCR=%08lx:%08lx\n",
                DcbaaHigh, DcbaaLow, CrcrHigh, CrcrLow);
        WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->DcbaapLow, DcbaaLow);
        WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->DcbaapHigh, DcbaaHigh);
        WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->CrCrLow, CrcrLow);
        WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->CrCrHigh, CrcrHigh);
        WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->Config,
                             Extension->MaxSlots);
        DPRINT1("usbxhci: USBCMD=%08lx USBSTS=%08lx CONFIG=%08lx\n",
                READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd),
                READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts),
                READ_REGISTER_ULONG(&Extension->OperationalRegisters->Config));
    }

    if (Extension->RuntimeRegisters)
    {
        PXHCI_INTERRUPTER_REGISTER_SET Interrupter =
            &Extension->RuntimeRegisters->Interrupter[0];
        ULONG Iman;
        
        WRITE_REGISTER_ULONG(&Interrupter->Imod, XHCI_IMOD_DEFAULT);
        WRITE_REGISTER_ULONG(&Interrupter->ErstSize, Extension->ErstEntryCount);
        WRITE_REGISTER_ULONG(&Interrupter->ErstBaseLow,
                             (ULONG)(Extension->ErstTablePhysical.QuadPart & 0xFFFFFFFF));
        WRITE_REGISTER_ULONG(&Interrupter->ErstBaseHigh,
                             (ULONG)(Extension->ErstTablePhysical.QuadPart >> 32));
        /* Program ERDP to the event ring base and set EHB (BUSY) to clear state */
        WRITE_REGISTER_ULONG(&Interrupter->ErdpHigh,
                             (ULONG)(Extension->EventRingPhysical.QuadPart >> 32));
        WRITE_REGISTER_ULONG(&Interrupter->ErdpLow,
                             ((ULONG)(Extension->EventRingPhysical.QuadPart & 0xFFFFFFFF)) |
                             XHCI_ERDP_BUSY);
        Extension->EventRingDequeuePointer = Extension->EventRingPhysical.QuadPart;

        Iman = READ_REGISTER_ULONG(&Interrupter->Iman);
        Iman |= XHCI_IMAN_IE;
        Iman |= XHCI_IMAN_IP;
        WRITE_REGISTER_ULONG(&Interrupter->Iman, Iman);
        DPRINT1("usbxhci: IMOD=%08lx ERST=%08lx:%08lx ERDP=%08lx:%08lx IMAN=%08lx\n",
                READ_REGISTER_ULONG(&Interrupter->Imod),
                READ_REGISTER_ULONG(&Interrupter->ErstBaseHigh),
                READ_REGISTER_ULONG(&Interrupter->ErstBaseLow),
                READ_REGISTER_ULONG(&Interrupter->ErdpHigh),
                READ_REGISTER_ULONG(&Interrupter->ErdpLow),
                READ_REGISTER_ULONG(&Interrupter->Iman));
    }

    XHCI_EnableInterrupts(Extension);

    Status = XHCI_RunController(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    return MP_STATUS_SUCCESS;
}

static VOID NTAPI
XHCI_StopController(PVOID MiniPortExtension,
                    BOOLEAN IsDoNotCallMiniport)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    PXHCI_HC_RESOURCES HcResources = NULL;
    ULONG SlotId;

    UNREFERENCED_PARAMETER(IsDoNotCallMiniport);

    if (!Extension)
        return;

    DPRINT1("usbxhci: StopController\n");

    XHCI_ShutdownController(Extension, TRUE);

    HcResources = Extension->HcResources;

    for (SlotId = 0; SlotId <= XHCI_MAX_SLOTS; SlotId++)
    {
        RtlZeroMemory(&Extension->DeviceSlots[SlotId], sizeof(XHCI_DEVICE_SLOT));
        if (HcResources)
            HcResources->Dcbaa[SlotId] = 0;
    }

    Extension->PendingUsbSts = 0;
    Extension->RhIrqEnabled = FALSE;
    Extension->InterruptsEnabled = FALSE;
    Extension->ControllerRunning = FALSE;
    Extension->FatalError = FALSE;

    while (!IsListEmpty(&Extension->CommandContextList))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&Extension->CommandContextList);
        PXHCI_COMMAND_CONTEXT Context =
            CONTAINING_RECORD(Entry, XHCI_COMMAND_CONTEXT, ListEntry);
        Context->InList = FALSE;
        Context->Completed = TRUE;
        Context->CompletionCode = XHCI_COMPLETION_STOPPED;
    }
    InitializeListHead(&Extension->CommandContextList);

    Extension->MmioBase = NULL;
    Extension->CapabilityRegisters = NULL;
    Extension->OperationalRegisters = NULL;
    Extension->RuntimeRegisters = NULL;
    Extension->DoorbellArray = NULL;
    Extension->Resources = NULL;
    Extension->CapabilityLength = 0;
    Extension->MaxSlots = 0;
    Extension->NumberOfPorts = 0;
    Extension->MaxScratchpadBuffers = 0;
    Extension->ContextSize = 0;
    Extension->Supports64Bit = FALSE;
    Extension->HciVersion = 0;
    Extension->HcResources = NULL;
    Extension->HcResourcesPhysical.QuadPart = 0;
    Extension->DcbaaPhysical.QuadPart = 0;
    Extension->ScratchpadArrayPhysical.QuadPart = 0;
    Extension->ScratchpadCount = 0;
    Extension->ConfiguredPageSize = 0;
    Extension->CommandRing = NULL;
    Extension->CommandRingPhysical.QuadPart = 0;
    Extension->CommandRingTrbCount = 0;
    Extension->CommandRingCycleState = 0;
    Extension->EventRing = NULL;
    Extension->EventRingPhysical.QuadPart = 0;
    Extension->EventRingTrbCount = 0;
    Extension->EventRingDequeueIndex = 0;
    Extension->EventRingCycleState = 0;
    Extension->EventRingDequeuePointer = 0;
    Extension->ErstTable = NULL;
    Extension->ErstTablePhysical.QuadPart = 0;
    Extension->ErstEntryCount = 0;
    Extension->DeviceContextsPhysical.QuadPart = 0;
    Extension->InputContextsPhysical.QuadPart = 0;
    Extension->Signature = 0;
    Extension->Quirks = 0;
    RtlFillMemory(Extension->PortLinkState,
                  sizeof(Extension->PortLinkState),
                  XHCI_INVALID_LINK_STATE);
    XHCI_InitDeviceAddressMap(Extension);
}

static BOOLEAN NTAPI
XHCI_InterruptService(PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    ULONG Status;
    ULONG AckMask;

    if (!Extension || !Extension->OperationalRegisters || Extension->FatalError)
        return FALSE;

    XHCI_DPRINT_SHARED("usbxhci: ISR (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());

    Status = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);
    AckMask = Status & (XHCI_USBSTS_EINT |
                        XHCI_USBSTS_PCD |
                        XHCI_USBSTS_HSE |
                        XHCI_USBSTS_HCE);

    if (!AckMask)
    {
        XHCI_DPRINT_SHARED("usbxhci: ISR no-ack (UsbSts=%08lx)\n", Status);
        return FALSE;
    }

    WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts, AckMask);

    InterlockedOr((volatile LONG *)&Extension->PendingUsbSts, AckMask);

    return TRUE;
}

static VOID NTAPI
XHCI_InterruptDpc(PVOID MiniPortExtension,
                  BOOLEAN EnableInterrupts)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    ULONG Pending;

    DPRINT1("usbxhci: DPC (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());

    if (!Extension)
        return;

    Pending = (ULONG)InterlockedExchange((volatile LONG *)&Extension->PendingUsbSts, 0);
    DPRINT1("usbxhci: DPC pending=%08lx RhIrqEnabled=%u IntsEnabled=%u\n",
            Pending, Extension->RhIrqEnabled ? 1 : 0, Extension->InterruptsEnabled ? 1 : 0);

    /* Host System Error (HSE) and Host Controller Error (HCE) are both fatal
     * conditions from the perspective of this miniport. If either is seen,
     * log it once and shut the controller down so we don't spin in a DPC
     * storm on a permanently-asserted error bit. */
    if (Pending & (XHCI_USBSTS_HSE | XHCI_USBSTS_HCE))
    {
        if (!Extension->FatalError)
        {
            DPRINT1("usbxhci: fatal controller error (USBSTS=%08lx) – shutting down controller\n",
                    Pending);
            XHCI_HandleControllerError(Extension, Pending);
        }
        return;
    }

    if (Pending & XHCI_USBSTS_EINT)
    {
        DPRINT1("usbxhci: DPC: EINT set, servicing events\n");
        XHCI_ServiceEventRing(Extension, TRUE, TRUE);
    }

    if (EnableInterrupts && !Extension->InterruptsEnabled)
    {
        XHCI_EnableInterrupts(Extension);
    }
}

static VOID NTAPI
XHCI_EnableInterrupts(PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    ULONG Command;
    PXHCI_INTERRUPTER_REGISTER_SET Interrupter;
    ULONG Iman;

    if (!Extension || !Extension->OperationalRegisters)
        return;

    Command = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd);
    Command |= XHCI_USBCMD_INTE;
    WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd, Command);
    Extension->InterruptsEnabled = TRUE;

    if (Extension->RuntimeRegisters)
    {
        Interrupter = &Extension->RuntimeRegisters->Interrupter[0];
        Iman = READ_REGISTER_ULONG(&Interrupter->Iman);
        Iman |= XHCI_IMAN_IE;
        Iman |= XHCI_IMAN_IP;
        WRITE_REGISTER_ULONG(&Interrupter->Iman, Iman);
    }
}

/* ========================= Safe stub implementations ========================= */

static MPSTATUS NTAPI
XHCI_ReopenEndpoint(PVOID MiniPortExtension,
                    PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                    PVOID EndpointHandle)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);
    UNREFERENCED_PARAMETER(EndpointProperties);
    UNREFERENCED_PARAMETER(EndpointHandle);
    DPRINT1("usbxhci: ReopenEndpoint (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());
    return MP_STATUS_NOT_SUPPORTED;
}

static MPSTATUS NTAPI
XHCI_SubmitIsoTransfer(PVOID MiniPortExtension,
                       PVOID EndpointHandle,
                       PUSBPORT_TRANSFER_PARAMETERS TransferParameters,
                       PVOID TransferHandle,
                       PVOID IsoParameters)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    PXHCI_ENDPOINT Endpoint = (PXHCI_ENDPOINT)EndpointHandle;
    PXHCI_TRANSFER Transfer = (PXHCI_TRANSFER)TransferHandle;
    PUSBPORT_SCATTER_GATHER_LIST SgList =
        (PUSBPORT_SCATTER_GATHER_LIST)IsoParameters;

    if (!Extension || !Endpoint || !Endpoint->Slot ||
        !TransferParameters || !Transfer)
    {
        return MP_STATUS_ERROR;
    }

    if (!Endpoint->Isochronous ||
        Endpoint->EndpointProperties.TransferType != USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
    {
        return MP_STATUS_NOT_SUPPORTED;
    }

    if (Extension->FatalError)
        return MP_STATUS_HW_ERROR;

    if (!SgList || SgList->SgElementCount == 0)
    {
        if (TransferParameters->TransferBufferLength != 0)
            return MP_STATUS_NO_RESOURCES;
    }

    if (Endpoint->ActiveTransfer)
        return MP_STATUS_FAILURE;

    RtlZeroMemory(Transfer, sizeof(*Transfer));
    Transfer->Endpoint = Endpoint;
    Transfer->TransferParameters = TransferParameters;
    Transfer->SgList = SgList;
    Transfer->TransferHandle = TransferHandle;
    Transfer->RequestedLength = TransferParameters->TransferBufferLength;
    Transfer->UsbdStatus = USBD_STATUS_SUCCESS;
    Transfer->Flags = 0;
    Transfer->NewAddress = 0;
    Transfer->IsIsochronous = TRUE;
    Transfer->IsControl = FALSE;

    return XHCI_SubmitSgTransfer(Extension,
                                 Endpoint,
                                 Transfer,
                                 XHCI_TRB_TYPE_ISOCH,
                                 TRUE);
}

static VOID NTAPI
XHCI_AbortTransfer(PVOID MiniPortExtension,
                   PVOID EndpointHandle,
                   PVOID TransferHandle,
                   PULONG BytesTransferred)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    PXHCI_ENDPOINT Endpoint = (PXHCI_ENDPOINT)EndpointHandle;

    UNREFERENCED_PARAMETER(TransferHandle);

    if (BytesTransferred)
        *BytesTransferred = 0;

    if (!Extension || !Endpoint || !Endpoint->Slot)
    {
        DPRINT1("usbxhci: AbortTransfer invalid args (IRQL=%lu)\n",
                (ULONG)KeGetCurrentIrql());
        return;
    }

    /* Stop and reset the target endpoint so hardware drops any in-flight TRBs. */
    XHCI_StopEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
    XHCI_ResetEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);

    XHCI_ResetEndpointRing(Endpoint);
    if (Endpoint->UsesStaticRing && Endpoint->Slot)
    {
        Endpoint->Slot->Ep0RingEnqueueIndex = Endpoint->TransferRing.EnqueueIndex;
        Endpoint->Slot->Ep0RingDequeueIndex = Endpoint->TransferRing.DequeueIndex;
        Endpoint->Slot->Ep0RingCycleState = Endpoint->TransferRing.CycleState;
    }

    XHCI_SetEndpointDequeue(Extension,
                            Endpoint->Slot,
                            Endpoint->EndpointId,
                            &Endpoint->TransferRing);

    XHCI_StartEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
    XHCI_RingEndpointDoorbell(Extension, Endpoint->SlotId, Endpoint->EndpointId, 0);

    DPRINT1("usbxhci: AbortTransfer done for slot %u ep %u (IRQL=%lu)\n",
            Endpoint->SlotId,
            Endpoint->EndpointId,
            (ULONG)KeGetCurrentIrql());
}

static VOID NTAPI
XHCI_PollEndpoint(PVOID MiniPortExtension,
                  PVOID EndpointHandle)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    UNREFERENCED_PARAMETER(EndpointHandle);

    XHCI_PollForWork(Extension, TRUE);
}

static VOID NTAPI
XHCI_CheckController(PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    if (!Extension)
        return;
    if (Extension->FatalError)
        return;

    /*
     * USBPORT calls this at DISPATCH_LEVEL to let the miniport make
     * progress even if interrupts are not delivered. Reuse the
     * polling helper so we both emulate the ISR/DPC handshake and
     * drain any pending TRBs that never triggered USBSTS.EINT.
     */
    XHCI_PollForWork(Extension, TRUE);
}

static VOID NTAPI
XHCI_PollController(PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;

    XHCI_PollForWork(Extension, TRUE);
}

static VOID NTAPI
XHCI_SetEndpointDataToggle(PVOID MiniPortExtension,
                           PVOID EndpointHandle,
                           ULONG Toggle)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    PXHCI_ENDPOINT Endpoint = (PXHCI_ENDPOINT)EndpointHandle;

    if (!Extension || !Endpoint || !Endpoint->Slot)
        return;

    /*
     * xHCI manages DATA toggles in hardware. USBPORT calls this after
     * clear-stall/reset paths to put the hardware back in sync with the
     * software ring head. When Toggle == 0, rewind the software ring so
     * new traffic starts from a clean boundary and then reprogram TR Dequeue.
     */
    if (Toggle == 0)
    {
        XHCI_ResetEndpointRing(Endpoint);

        if (Endpoint->UsesStaticRing && Endpoint->Slot)
        {
            Endpoint->Slot->Ep0RingEnqueueIndex = Endpoint->TransferRing.EnqueueIndex;
            Endpoint->Slot->Ep0RingDequeueIndex = Endpoint->TransferRing.DequeueIndex;
            Endpoint->Slot->Ep0RingCycleState = Endpoint->TransferRing.CycleState;
        }
    }

    /* Re-sync hardware dequeue to the current ring head. */
    XHCI_SetEndpointDequeue(Extension,
                            Endpoint->Slot,
                            Endpoint->EndpointId,
                            &Endpoint->TransferRing);
}

static ULONG NTAPI
XHCI_GetEndpointStatus(PVOID MiniPortExtension,
                       PVOID EndpointHandle)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);
    UNREFERENCED_PARAMETER(EndpointHandle);
    DPRINT1("usbxhci: GetEndpointStatus (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());
    return USBPORT_ENDPOINT_UNKNOWN;
}

static VOID NTAPI
XHCI_SetEndpointStatus(PVOID MiniPortExtension,
                       PVOID EndpointHandle,
                       ULONG Status)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    PXHCI_ENDPOINT Endpoint = (PXHCI_ENDPOINT)EndpointHandle;

    if (!Extension || !Endpoint || !Endpoint->Slot)
        return;

    /* Clear-stall path: reset endpoint state and re-sync dequeue. */
    XHCI_StopEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
    XHCI_ResetEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
    XHCI_ResetEndpointRing(Endpoint);
    if (Endpoint->UsesStaticRing && Endpoint->Slot)
    {
        Endpoint->Slot->Ep0RingEnqueueIndex = Endpoint->TransferRing.EnqueueIndex;
        Endpoint->Slot->Ep0RingDequeueIndex = Endpoint->TransferRing.DequeueIndex;
        Endpoint->Slot->Ep0RingCycleState = Endpoint->TransferRing.CycleState;
    }
    XHCI_SetEndpointDequeue(Extension,
                            Endpoint->Slot,
                            Endpoint->EndpointId,
                            &Endpoint->TransferRing);

    XHCI_StartEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
    XHCI_RingEndpointDoorbell(Extension, Endpoint->SlotId, Endpoint->EndpointId, 0);
}

static VOID NTAPI
XHCI_MpResetController(PVOID MiniPortExtension)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);
    DPRINT1("usbxhci: ResetController (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());
}

static MPSTATUS NTAPI
XHCI_StartSendOnePacket(PVOID MiniPortExtension,
                        PVOID Param1,
                        PVOID Param2,
                        PULONG Param3,
                        PVOID Param4,
                        PVOID Param5,
                        ULONG Param6,
                        USBD_STATUS *Param7)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);
    UNREFERENCED_PARAMETER(Param1);
    UNREFERENCED_PARAMETER(Param2);
    UNREFERENCED_PARAMETER(Param3);
    UNREFERENCED_PARAMETER(Param4);
    UNREFERENCED_PARAMETER(Param5);
    UNREFERENCED_PARAMETER(Param6);
    UNREFERENCED_PARAMETER(Param7);
    DPRINT1("usbxhci: StartSendOnePacket (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());
    return MP_STATUS_NOT_SUPPORTED;
}

static MPSTATUS NTAPI
XHCI_EndSendOnePacket(PVOID MiniPortExtension,
                      PVOID Param1,
                      PVOID Param2,
                      PULONG Param3,
                      PVOID Param4,
                      PVOID Param5,
                      ULONG Param6,
                      USBD_STATUS *Param7)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);
    UNREFERENCED_PARAMETER(Param1);
    UNREFERENCED_PARAMETER(Param2);
    UNREFERENCED_PARAMETER(Param3);
    UNREFERENCED_PARAMETER(Param4);
    UNREFERENCED_PARAMETER(Param5);
    UNREFERENCED_PARAMETER(Param6);
    UNREFERENCED_PARAMETER(Param7);
    DPRINT1("usbxhci: EndSendOnePacket (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());
    return MP_STATUS_NOT_SUPPORTED;
}

static MPSTATUS NTAPI
XHCI_PassThru(PVOID MiniPortExtension,
              PVOID IoBuffer,
              ULONG IoControlCode,
              PVOID IoCtlParams)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);
    UNREFERENCED_PARAMETER(IoBuffer);
    UNREFERENCED_PARAMETER(IoControlCode);
    UNREFERENCED_PARAMETER(IoCtlParams);
    DPRINT1("usbxhci: PassThru (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());
    return MP_STATUS_NOT_SUPPORTED;
}

static VOID NTAPI
XHCI_RebalanceEndpoint(PVOID MiniPortExtension,
                       PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                       PVOID EndpointHandle)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);
    UNREFERENCED_PARAMETER(EndpointProperties);
    UNREFERENCED_PARAMETER(EndpointHandle);
    DPRINT1("usbxhci: RebalanceEndpoint (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());
}

static VOID NTAPI
XHCI_FlushInterrupts(PVOID MiniPortExtension)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);
    DPRINT1("usbxhci: FlushInterrupts (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());
}

static MPSTATUS NTAPI
XHCI_RH_ChirpRootPort(PVOID MiniPortExtension,
                      USHORT Port)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);
    UNREFERENCED_PARAMETER(Port);
    DPRINT1("usbxhci: RH_ChirpRootPort (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());
    return MP_STATUS_NOT_SUPPORTED;
}

static VOID NTAPI
XHCI_TakePortControl(PVOID MiniPortExtension)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);
    DPRINT1("usbxhci: TakePortControl (IRQL=%lu)\n", (ULONG)KeGetCurrentIrql());
}

static VOID NTAPI
XHCI_DisableInterrupts(PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    ULONG Command;
    PXHCI_INTERRUPTER_REGISTER_SET Interrupter;
    ULONG Iman;

    if (!Extension || !Extension->OperationalRegisters)
        return;

    Command = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd);
    Command &= ~XHCI_USBCMD_INTE;
    WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd, Command);
    Extension->InterruptsEnabled = FALSE;

    if (Extension->RuntimeRegisters)
    {
        Interrupter = &Extension->RuntimeRegisters->Interrupter[0];
        Iman = READ_REGISTER_ULONG(&Interrupter->Iman);
        Iman &= ~XHCI_IMAN_IE;
        WRITE_REGISTER_ULONG(&Interrupter->Iman, Iman);
    }
}

static MPSTATUS
XHCI_RunController(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONG Command;
    ULONG Status;

    if (!Extension || !Extension->OperationalRegisters)
        return MP_STATUS_ERROR;

    Status = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);
    Command = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd);

    if ((Command & XHCI_USBCMD_RS) && !(Status & XHCI_USBSTS_HCH))
    {
        Extension->ControllerRunning = TRUE;
        return MP_STATUS_SUCCESS;
    }

    Command |= XHCI_USBCMD_RS;
    WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd, Command);

    if (!XHCI_WaitForRegisterBits(&Extension->OperationalRegisters->UsbSts,
                                  XHCI_USBSTS_HCH,
                                  FALSE,
                                  XHCI_WAIT_HALT_US))
    {
        DPRINT1("usbxhci: controller failed to exit halt state\n");
        return MP_STATUS_HW_ERROR;
    }

    Extension->ControllerRunning = TRUE;
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
XHCI_HaltController(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ ULONG TimeoutUs)
{
    ULONG Command;

    if (!Extension || !Extension->OperationalRegisters)
        return MP_STATUS_ERROR;

    Command = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd);
    if ((Command & XHCI_USBCMD_RS) == 0)
    {
        if (XHCI_WaitForRegisterBits(&Extension->OperationalRegisters->UsbSts,
                                     XHCI_USBSTS_HCH,
                                     TRUE,
                                     TimeoutUs ? TimeoutUs : XHCI_WAIT_HALT_US))
        {
            Extension->ControllerRunning = FALSE;
            return MP_STATUS_SUCCESS;
        }

        DPRINT1("usbxhci: controller already halted but status not updating\n");
        return MP_STATUS_HW_ERROR;
    }

    Command &= ~XHCI_USBCMD_RS;
    WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd, Command);

    if (!XHCI_WaitForRegisterBits(&Extension->OperationalRegisters->UsbSts,
                                  XHCI_USBSTS_HCH,
                                  TRUE,
                                  TimeoutUs ? TimeoutUs : XHCI_WAIT_HALT_US))
    {
        DPRINT1("usbxhci: halt timed out\n");
        return MP_STATUS_HW_ERROR;
    }

    Extension->ControllerRunning = FALSE;
    return MP_STATUS_SUCCESS;
}

static VOID
XHCI_ShutdownController(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ BOOLEAN FullReset)
{
    if (!Extension)
        return;

    XHCI_DisableInterrupts(Extension);

    if (Extension->OperationalRegisters)
    {
        if (XHCI_HaltController(Extension, XHCI_WAIT_HALT_US) != MP_STATUS_SUCCESS)
        {
            DPRINT1("usbxhci: warning - halt failed during shutdown\n");
        }
    }

    if (FullReset)
    {
        MPSTATUS Status = XHCI_ResetController(Extension);
        if (Status != MP_STATUS_SUCCESS)
            DPRINT1("usbxhci: warning - reset failed during shutdown (status=%lu)\n", Status);
    }
}

static
VOID
NTAPI
XHCI_SuspendController(
    _In_ PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;

    if (!Extension)
        return;

    XHCI_DisableInterrupts(Extension);
    XHCI_HaltController(Extension, XHCI_WAIT_HALT_US);
}

static
MPSTATUS
NTAPI
XHCI_ResumeController(
    _In_ PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    MPSTATUS Status;

    if (!Extension)
        return MP_STATUS_ERROR;

    Status = XHCI_RunController(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    XHCI_EnableInterrupts(Extension);
    XHCI_ServiceEventRing(Extension, TRUE, TRUE);
    return MP_STATUS_SUCCESS;
}

static
VOID
NTAPI
XHCI_RH_GetRootHubData(
    _In_ PVOID MiniPortExtension,
    _In_ PVOID RootHubData)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    PUSBPORT_ROOT_HUB_DATA HubData = RootHubData;

    if (!Extension || !HubData)
        return;

    RtlZeroMemory(HubData, sizeof(*HubData));

    HubData->NumberOfPorts = Extension->NumberOfPorts;
    HubData->HubCharacteristics.AsUSHORT = 0;
    if (Extension->PortPowerControl)
    {
        HubData->HubCharacteristics.Usb30HubCharacteristics.PowerControlMode = 1;
        HubData->HubCharacteristics.Usb30HubCharacteristics.NoPowerSwitching = 0;
    }
    else
    {
        HubData->HubCharacteristics.Usb30HubCharacteristics.PowerControlMode = 0;
        HubData->HubCharacteristics.Usb30HubCharacteristics.NoPowerSwitching = 1;
    }
    HubData->HubCharacteristics.Usb20HubCharacteristics.PortIndicatorsSupported =
        Extension->PortIndicatorsSupported ? 1 : 0;
    HubData->PowerOnToPowerGood = 2; // 4 ms typical
    HubData->HubControlCurrent = 0;
}

static
MPSTATUS
NTAPI
XHCI_RH_GetStatus(
    _In_ PVOID MiniPortExtension,
    _Out_ PUSHORT Status)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);

    if (!Status)
        return MP_STATUS_ERROR;

    *Status = USB_GETSTATUS_SELF_POWERED;
    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
NTAPI
XHCI_RH_GetHubStatus(
    _In_ PVOID MiniPortExtension,
    _Out_ PUSB_HUB_STATUS_AND_CHANGE HubStatus)
{
    UNREFERENCED_PARAMETER(MiniPortExtension);

    if (!HubStatus)
        return MP_STATUS_ERROR;

    RtlZeroMemory(HubStatus, sizeof(*HubStatus));
    return MP_STATUS_SUCCESS;
}

static
VOID
XHCI_RH_UpdatePortStatusFields(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT PortNumber,
    _In_ ULONG PortValue,
    _Inout_ PUSB_PORT_STATUS_AND_CHANGE PortStatus)
{
    ULONG Speed;
    ULONG LinkState;
    UCHAR PreviousLinkState = XHCI_INVALID_LINK_STATE;
    BOOLEAN ReportedLinkChange = FALSE;
    PUSB_30_PORT_STATUS PortStatus30 = &PortStatus->PortStatus.Usb30PortStatus;
    PUSB_30_PORT_CHANGE PortChange30 = &PortStatus->PortChange.Usb30PortChange;

    if (PortValue & XHCI_PORTSC_CCS)
    {
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_CONNECT;
        PortStatus30->CurrentConnectStatus = 1;
    }

    if (PortValue & XHCI_PORTSC_PED)
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_ENABLE;
    PortStatus30->PortEnabledDisabled = (PortValue & XHCI_PORTSC_PED) ? 1 : 0;

    LinkState = (PortValue & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;
    if (LinkState == PORT_LINK_STATE_U3)
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_SUSPEND;
    PortStatus30->PortLinkState = (USHORT)LinkState;
    if (Extension && PortNumber > 0 && PortNumber <= XHCI_MAX_PORTS)
    {
        PreviousLinkState = Extension->PortLinkState[PortNumber];
        Extension->PortLinkState[PortNumber] = (UCHAR)LinkState;
    }

    if (PortValue & XHCI_PORTSC_OCA)
    {
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_OVER_CURRENT;
        PortStatus30->OverCurrent = 1;
    }

    if (PortValue & XHCI_PORTSC_PR)
    {
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_RESET;
        PortStatus30->Reset = 1;
    }

    if (Extension && Extension->PortPowerControl)
    {
        if (PortValue & XHCI_PORTSC_PP)
            PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_POWER;
        PortStatus30->PortPower = (PortValue & XHCI_PORTSC_PP) ? 1 : 0;
    }
    else
    {
        PortStatus30->PortPower = 1;
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_POWER;
    }

    Speed = (PortValue & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
    if (Speed == XHCI_PORTSC_SPEED_LOW)
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_LOW_SPEED;
    else if (Speed == XHCI_PORTSC_SPEED_HIGH || Speed == XHCI_PORTSC_SPEED_SUPER)
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_HIGH_SPEED;

    switch (Speed)
    {
        case XHCI_PORTSC_SPEED_LOW:
            PortStatus30->NegotiatedDeviceSpeed = 2;
            break;
        case XHCI_PORTSC_SPEED_FULL:
            PortStatus30->NegotiatedDeviceSpeed = 1;
            break;
        case XHCI_PORTSC_SPEED_HIGH:
            PortStatus30->NegotiatedDeviceSpeed = 3;
            break;
        case XHCI_PORTSC_SPEED_SUPER:
            PortStatus30->NegotiatedDeviceSpeed = 4;
            break;
        default:
            PortStatus30->NegotiatedDeviceSpeed = 0;
            break;
    }

    if (PortValue & XHCI_PORTSC_CSC)
    {
        PortStatus->PortChange.Usb20PortChange.ConnectStatusChange = 1;
        PortChange30->ConnectStatusChange = 1;
    }

    if (PortValue & XHCI_PORTSC_PEC)
        PortStatus->PortChange.Usb20PortChange.PortEnableDisableChange = 1;

    if (PortValue & XHCI_PORTSC_PLC)
    {
        PortStatus->PortChange.Usb20PortChange.SuspendChange = 1;
        PortChange30->PortLinkStateChange = 1;
        ReportedLinkChange = TRUE;
    }

    if (PortValue & XHCI_PORTSC_OCC)
    {
        PortStatus->PortChange.Usb20PortChange.OverCurrentIndicatorChange = 1;
        PortChange30->OverCurrentIndicatorChange = 1;
    }

    if (PortValue & XHCI_PORTSC_PRC)
    {
        PortStatus->PortChange.Usb20PortChange.ResetChange = 1;
        PortChange30->ResetChange = 1;
    }

    if (PortValue & XHCI_PORTSC_WRC)
    {
        PortStatus->PortChange.Usb20PortChange.ResetChange = 1;
        PortChange30->BHResetChange = 1;
    }

    if (PortValue & XHCI_PORTSC_CEC)
        PortChange30->PortConfigErrorChange = 1;

    if (!ReportedLinkChange &&
        PreviousLinkState != XHCI_INVALID_LINK_STATE &&
        PreviousLinkState != LinkState)
    {
        PortChange30->PortLinkStateChange = 1;
        if ((PreviousLinkState == PORT_LINK_STATE_U3 && LinkState != PORT_LINK_STATE_U3) ||
            (LinkState == PORT_LINK_STATE_U3 && PreviousLinkState != PORT_LINK_STATE_U3))
        {
            PortStatus->PortChange.Usb20PortChange.SuspendChange = 1;
        }
    }
}

static
MPSTATUS
NTAPI
XHCI_RH_GetPortStatus(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port,
    _Out_ PUSB_PORT_STATUS_AND_CHANGE PortStatus)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    volatile ULONG *PortStatusReg;
    ULONG PortValue;
    BOOLEAN PoweredOn = FALSE;

    if (!Extension || !PortStatus)
        return MP_STATUS_ERROR;

    PortStatusReg = XHCI_GetPortStatusRegister(Extension, Port);
    if (!PortStatusReg)
        return MP_STATUS_ERROR;

    PortValue = READ_REGISTER_ULONG(PortStatusReg);
    if (Extension->PortPowerControl &&
        (PortValue & XHCI_PORTSC_PP) == 0)
    {
        /* Port lost power; try to re-enable so status reflects reality. */
        if (XHCI_ModifyPortBits(Extension, Port, XHCI_PORTSC_PP, 0, 0) == MP_STATUS_SUCCESS)
        {
            PortValue = READ_REGISTER_ULONG(PortStatusReg);
            PoweredOn = TRUE;
        }
    }

    RtlZeroMemory(PortStatus, sizeof(*PortStatus));
    XHCI_RH_UpdatePortStatusFields(Extension, Port, PortValue, PortStatus);

    if (PoweredOn)
    {
        /* Make the power bit visible immediately after repowering. */
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_POWER;
        PortStatus->PortStatus.Usb30PortStatus.PortPower = 1;
    }

    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
NTAPI
XHCI_RH_SetFeaturePortPower(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    if (!Extension || !Extension->PortPowerControl)
        return MP_STATUS_SUCCESS;
    return XHCI_ModifyPortBits(Extension, Port, XHCI_PORTSC_PP, 0, 0);
}

static
MPSTATUS
NTAPI
XHCI_RH_ClearFeaturePortPower(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    if (!Extension || !Extension->PortPowerControl)
        return MP_STATUS_SUCCESS;
    return XHCI_ModifyPortBits(Extension, Port, 0, XHCI_PORTSC_PP, 0);
}

static
MPSTATUS
NTAPI
XHCI_RH_SetFeaturePortReset(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    if (!Extension)
        return MP_STATUS_ERROR;

    if (XHCI_PortIsSuperSpeed(Extension, Port))
        return XHCI_ModifyPortBits(Extension, Port, XHCI_PORTSC_WPR, 0, 0);

    return XHCI_ModifyPortBits(Extension, Port, XHCI_PORTSC_PR, 0, 0);
}

static
MPSTATUS
NTAPI
XHCI_RH_SetFeaturePortEnable(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    MPSTATUS Status;

    if (!Extension)
        return MP_STATUS_ERROR;

    Status = XHCI_ModifyPortBits(Extension,
                                 Port,
                                 0,
                                 XHCI_PORTSC_DR,
                                 XHCI_PORTSC_PEC);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    return XHCI_SetPortLinkState(Extension, Port, PORT_LINK_STATE_U0);
}

static
MPSTATUS
NTAPI
XHCI_RH_SetFeaturePortSuspend(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    /*
     * Enter U3 (suspend) on the target port. The controller will signal
     * a port-status-change event when the link transitions into or out
     * of U3 so USBPORT can observe Suspend/SuspendChange via
     * XHCI_RH_GetPortStatus and generate wake notifications.
     *
     * TODO: For hardware that requires explicit per-port wake enable,
     * consider programming the WCE/WDE/WOE bits here once ReactOS has
     * a clear policy for selective suspend and remote-wake support.
     */
    return XHCI_SetPortLinkState(Extension, Port, PORT_LINK_STATE_U3);
}

static
MPSTATUS
NTAPI
XHCI_RH_ClearFeaturePortEnable(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;

    if (!Extension)
        return MP_STATUS_ERROR;

    return XHCI_ModifyPortBits(Extension,
                               Port,
                               XHCI_PORTSC_DR,
                               0,
                               XHCI_PORTSC_PEC);
}

static
MPSTATUS
NTAPI
XHCI_RH_ClearFeaturePortEnableChange(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    XHCI_RH_AckPortChange((PXHCI_EXTENSION)MiniPortExtension, Port, XHCI_PORTSC_PEC);
    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
NTAPI
XHCI_RH_ClearFeaturePortConnectChange(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    XHCI_RH_AckPortChange((PXHCI_EXTENSION)MiniPortExtension, Port, XHCI_PORTSC_CSC);
    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
NTAPI
XHCI_RH_ClearFeaturePortResetChange(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    XHCI_RH_AckPortChange((PXHCI_EXTENSION)MiniPortExtension, Port,
                          XHCI_PORTSC_PRC | XHCI_PORTSC_WRC);
    XHCI_ResetDeviceOnPort((PXHCI_EXTENSION)MiniPortExtension, Port);
    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
NTAPI
XHCI_RH_ClearFeaturePortSuspend(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    MPSTATUS Status;

    Status = XHCI_SetPortLinkState(Extension, Port, PORT_LINK_STATE_U0);
    if (Status == MP_STATUS_SUCCESS)
        XHCI_RH_AckPortChange(Extension, Port, XHCI_PORTSC_PLC);

    return Status;
}

static
MPSTATUS
NTAPI
XHCI_RH_ClearFeaturePortSuspendChange(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    XHCI_RH_AckPortChange((PXHCI_EXTENSION)MiniPortExtension, Port, XHCI_PORTSC_PLC);
    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
NTAPI
XHCI_RH_ClearFeaturePortOvercurrentChange(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    XHCI_RH_AckPortChange((PXHCI_EXTENSION)MiniPortExtension, Port, XHCI_PORTSC_OCC);
    return MP_STATUS_SUCCESS;
}

static
VOID
NTAPI
XHCI_RH_DisableIrq(
    _In_ PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;

    if (!Extension)
        return;

    Extension->RhIrqEnabled = FALSE;
}

static
VOID
NTAPI
XHCI_RH_EnableIrq(
    _In_ PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;

    if (!Extension)
        return;

    Extension->RhIrqEnabled = TRUE;
}
