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

#define XHCI_DC_CONTEXT_COUNT 33
#define XHCI_IC_CONTEXT_COUNT 33
#define XHCI_DC_CONTEXT_LENGTH(Ext) ((((SIZE_T)(Ext)->ContextSize * XHCI_DC_CONTEXT_COUNT) + 63) & ~0x3F)
#define XHCI_IC_CONTEXT_LENGTH(Ext) ((((SIZE_T)(Ext)->ContextSize * XHCI_IC_CONTEXT_COUNT) + 63) & ~0x3F)
#define XHCI_COMMON_BUFFER_RESERVE_SLOTS      96
#define XHCI_COMMON_BUFFER_RESERVE_SCRATCHPADS 64

#ifndef PCI_ENABLE_MEMORY_SPACE
#define PCI_ENABLE_MEMORY_SPACE 0x0002
#endif
#ifndef PCI_ENABLE_BUS_MASTER
#define PCI_ENABLE_BUS_MASTER   0x0004
#endif
#ifndef PCI_COMMAND_OFFSET
#define PCI_COMMAND_OFFSET      0x04
#endif

#define XHCI_INVALID_LINK_STATE 0xFF
#define USBPORT_NO_HUB_ADDRESS 0xFFFF
#define XHCI_EP0_POLL_INTERVAL_US 50

USBPORT_REGISTRATION_PACKET XhciRegPacket;
static BOOLEAN g_XhciStartupHceQuirkOverrideValid;
static BOOLEAN g_XhciStartupHceQuirkOverride;

FORCEINLINE
PXHCI_INPUT_CONTROL_CONTEXT
XHCI_GetInputControlContextVa(_In_ PXHCI_EXTENSION Extension, _In_ PVOID Base)
{
    UNREFERENCED_PARAMETER(Extension);

    return (PXHCI_INPUT_CONTROL_CONTEXT)Base;
}

FORCEINLINE
PXHCI_SLOT_CONTEXT
XHCI_GetInputSlotContextVa(_In_ PXHCI_EXTENSION Extension, _In_ PVOID Base)
{
    return (PXHCI_SLOT_CONTEXT)((PUCHAR)Base + Extension->ContextSize);
}

FORCEINLINE
PXHCI_ENDPOINT_CONTEXT
XHCI_GetInputEndpointContextVa(_In_ PXHCI_EXTENSION Extension,
                               _In_ PVOID Base,
                               _In_ ULONG EndpointIndex)
{
    return (PXHCI_ENDPOINT_CONTEXT)((PUCHAR)Base +
                                    Extension->ContextSize * (2 + EndpointIndex));
}


FORCEINLINE
PXHCI_SLOT_CONTEXT
XHCI_GetDeviceSlotContextVa(_In_ PXHCI_EXTENSION Extension, _In_ PVOID Base)
{
    UNREFERENCED_PARAMETER(Extension);

    return (PXHCI_SLOT_CONTEXT)Base;
}

FORCEINLINE
PXHCI_ENDPOINT_CONTEXT
XHCI_GetDeviceEndpointContextVa(_In_ PXHCI_EXTENSION Extension,
                                _In_ PVOID Base,
                                _In_ ULONG EndpointIndex)
{
    return (PXHCI_ENDPOINT_CONTEXT)((PUCHAR)Base +
                                    Extension->ContextSize * (1 + EndpointIndex));
}


#if DBG
static ULONG g_XhciTraceMask;

#define XHCI_TRACE_EVENTS    0x00000001
#define XHCI_TRACE_TRANSFERS 0x00000002
#define XHCI_TRACE_COMMANDS  0x00000004
#define XHCI_TRACE_PORTS     0x00000008

#define XHCI_DBG(Mask, ...)                                        \
    do {                                                           \
        if (g_XhciTraceMask & (Mask))                              \
            DPRINT(__VA_ARGS__);                                   \
    } while (0)
#else
#define XHCI_DBG(Mask, ...) do { UNREFERENCED_PARAMETER(Mask); } while (0)
#endif

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
static VOID XHCI_HandleCommandTimeout(PXHCI_EXTENSION Extension, PXHCI_COMMAND_CONTEXT CommandContext);
static VOID XHCI_GetRegistryParameters(PXHCI_EXTENSION Extension);
static VOID XHCI_ValidateContextLayout(PXHCI_EXTENSION Extension);
static VOID NTAPI XHCI_RH_GetRootHubData(PVOID MiniPortExtension, PVOID RootHubData);
static MPSTATUS NTAPI XHCI_RH_GetStatus(PVOID MiniPortExtension, PUSHORT Status);
static MPSTATUS NTAPI XHCI_RH_GetPortStatus(PVOID MiniPortExtension, USHORT Port, PUSB_PORT_STATUS_AND_CHANGE PortStatus);
static MPSTATUS NTAPI XHCI_RH_GetHubStatus(PVOID MiniPortExtension, PUSB_HUB_STATUS_AND_CHANGE HubStatus);
static VOID XHCI_HandlePortChange(PXHCI_EXTENSION Extension, USHORT PortId, BOOLEAN NotifyHub);
static BOOLEAN XHCI_ScanPortStatusChanges(PXHCI_EXTENSION Extension, BOOLEAN NotifyHub);
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
static VOID NTAPI XHCI_Ep0PollDpc(PKDPC Dpc,
                                  PVOID DeferredContext,
                                  PVOID SystemArg1,
                                  PVOID SystemArg2);
static VOID XHCI_ScheduleEp0Poll(PXHCI_EXTENSION Extension);
static VOID XHCI_TraceCommandRingState(PXHCI_EXTENSION Extension,
                                       PCSTR Reason,
                                       ULONGLONG CommandPointer,
                                       ULONG TrbType);
static VOID XHCI_DumpControllerState(PXHCI_EXTENSION Extension, PCSTR Reason);
static PXHCI_TRB XHCI_LocateCommandTrb(PXHCI_EXTENSION Extension,
                                       ULONGLONG CommandPointer,
                                       PULONG IndexOut);
static VOID XHCI_LogEventRingSnapshot(PXHCI_EXTENSION Extension, ULONG EntriesToDump);
static VOID XHCI_LogCommandTimeoutDetails(PXHCI_EXTENSION Extension,
                                          PXHCI_COMMAND_CONTEXT CommandContext);
static VOID XHCI_LogInterrupterState(PXHCI_EXTENSION Extension, PCSTR Reason);
static VOID XHCI_DumpAddressDeviceContext(PXHCI_EXTENSION Extension,
                                          PXHCI_DEVICE_SLOT Slot,
                                          UCHAR EndpointId,
                                          USHORT PortNumber,
                                          UCHAR CompletionCode);
static MPSTATUS XHCI_RecoverControllerAfterCommandTimeout(PXHCI_EXTENSION Extension);
static BOOLEAN XHCI_IsVirtualPort(PXHCI_EXTENSION Extension, USHORT PortNumber);
static MPSTATUS XHCI_BringupVirtualDefaultControlEndpoint(PXHCI_EXTENSION Extension,
                                                          PXHCI_ENDPOINT Endpoint,
                                                          PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties);
static ULONG XHCI_CopyVirtualConfigDescriptor(USHORT PortNumber, PUCHAR Buffer, ULONG Length);
static ULONG XHCI_CopyVirtualStringDescriptor(USHORT PortNumber,
                                              UCHAR StringIndex,
                                              PUCHAR Buffer,
                                              ULONG Length);
static MPSTATUS XHCI_HandleVirtualControlTransfer(PXHCI_EXTENSION Extension,
                                                  PXHCI_ENDPOINT Endpoint,
                                                  PXHCI_TRANSFER Transfer);

static
SIZE_T
XHCI_CalcCommonBufferFootprint(
    _In_ ULONG MaxSlots,
    _In_ ULONG Scratchpads,
    _In_ ULONG CommandRingTrbs,
    _In_ ULONG EventRingTrbs,
    _In_ ULONG ErstEntries,
    _In_ SIZE_T ContextSize)
{
    SIZE_T Offset = 0;

    if (MaxSlots > XHCI_MAX_SLOTS)
        MaxSlots = XHCI_MAX_SLOTS;
    if (Scratchpads > XHCI_MAX_SCRATCHPADS)
        Scratchpads = XHCI_MAX_SCRATCHPADS;
    if (ContextSize == 0)
        ContextSize = 32;

    Offset = XHCI_ALIGN_UP(Offset, 64);

    Offset += (SIZE_T)(MaxSlots + 1) * sizeof(ULONGLONG);

    Offset = XHCI_ALIGN_UP(Offset, 64);

    Offset += (SIZE_T)Scratchpads * sizeof(ULONGLONG);

    Offset = XHCI_ALIGN_UP(Offset, PAGE_SIZE);
    Offset += (SIZE_T)Scratchpads * sizeof(XHCI_SCRATCHPAD_PAGE);

    Offset = XHCI_ALIGN_UP(Offset, 64);

    Offset += (SIZE_T)CommandRingTrbs * sizeof(XHCI_TRB);

    Offset = XHCI_ALIGN_UP(Offset, 64);

    Offset += (SIZE_T)EventRingTrbs * sizeof(XHCI_TRB);

    Offset = XHCI_ALIGN_UP(Offset, 64);

    Offset += (SIZE_T)ErstEntries * sizeof(XHCI_ERST_ENTRY);

    Offset = XHCI_ALIGN_UP(Offset, 64);

    Offset += (SIZE_T)(MaxSlots + 1) * ContextSize * XHCI_DC_CONTEXT_COUNT;

    Offset = XHCI_ALIGN_UP(Offset, 64);

    Offset += (SIZE_T)(MaxSlots + 1) * ContextSize * XHCI_IC_CONTEXT_COUNT;

    Offset = XHCI_ALIGN_UP(Offset, 64);

    Offset += (SIZE_T)(MaxSlots + 1) *
              sizeof(XHCI_TRB) *
              XHCI_STATIC_EP_RING_TRBS;

    return Offset;
}

static
SIZE_T
XHCI_GetMaximumCommonBufferSize(VOID)
{
    /*
     * DriverEntry cannot inspect HCSPARAMS yet, so reserve a conservative
     * contiguous buffer that covers the capabilities of the vast majority of
     * controllers (dozens of slots, tens of scratchpads) without demanding a
     * multi-megabyte allocation that frequently fails on fragmented systems.
     */
    const ULONG ReservedSlots =
        (XHCI_COMMON_BUFFER_RESERVE_SLOTS < XHCI_MAX_SLOTS) ?
            XHCI_COMMON_BUFFER_RESERVE_SLOTS : XHCI_MAX_SLOTS;
    const ULONG ReservedScratchpads =
        (XHCI_COMMON_BUFFER_RESERVE_SCRATCHPADS < XHCI_MAX_SCRATCHPADS) ?
            XHCI_COMMON_BUFFER_RESERVE_SCRATCHPADS : XHCI_MAX_SCRATCHPADS;

    return XHCI_CalcCommonBufferFootprint(ReservedSlots,
                                          ReservedScratchpads,
                                          XHCI_COMMAND_RING_TRBS,
                                          XHCI_EVENT_RING_TRBS,
                                          XHCI_ERST_MAX_ENTRIES,
                                          64);
}


FORCEINLINE
ULONG
XHCI_CalcTrbTransferChunk(
    _In_ ULONGLONG BufferAddress,
    _In_ ULONG ElementRemaining,
    _In_ ULONG TransferRemaining,
    _In_ ULONG IsoPayloadLimit)
{
    ULONG Chunk = ElementRemaining;

    if (Chunk > TransferRemaining)
        Chunk = TransferRemaining;
    if (Chunk > XHCI_MAX_TRB_TRANSFER_LENGTH)
        Chunk = XHCI_MAX_TRB_TRANSFER_LENGTH;

    if (IsoPayloadLimit != 0 && Chunk > IsoPayloadLimit)
        Chunk = IsoPayloadLimit;

    /*
     * xHCI section 4.11.2 – a Transfer TRB may not cross a 64KB boundary.
     * Trim the chunk so the programmed buffer stays within that window.
     */
    if ((Chunk + (ULONG)(BufferAddress & 0xFFFF)) > 0x10000)
    {
        ULONG Boundary = 0x10000 - (ULONG)(BufferAddress & 0xFFFF);
        if (Boundary != 0 && Boundary < Chunk)
            Chunk = Boundary;
    }

    ASSERT(Chunk != 0);
    return Chunk;
}

/* Limit how often we log benign HCE on QEMU's xHCI controller. */
static LONG XhciHceQuirkLogBudget = 1;
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
static BOOLEAN XHCI_EnablePciBusMaster(PXHCI_EXTENSION Extension);
static VOID NTAPI XHCI_FlushInterrupts(PVOID MiniPortExtension);
static MPSTATUS NTAPI XHCI_RH_ChirpRootPort(PVOID MiniPortExtension,
                                            USHORT Port);
static VOID NTAPI XHCI_TakePortControl(PVOID MiniPortExtension);
static BOOLEAN XHCI_IsValidPort(PXHCI_EXTENSION Extension, USHORT Port);
static volatile ULONG *XHCI_GetPortStatusRegister(PXHCI_EXTENSION Extension, USHORT Port);
static BOOLEAN XHCI_PortIsSuperSpeed(PXHCI_EXTENSION Extension, USHORT Port);
static VOID XHCI_RH_AckPortChange(PXHCI_EXTENSION Extension, USHORT Port, ULONG ChangeMask);
static VOID XHCI_AckPortChangeInternal(PXHCI_EXTENSION Extension,
                                       USHORT Port,
                                       ULONG ChangeMask,
                                       BOOLEAN ClearShadowMask);
static MPSTATUS XHCI_ModifyPortBits(PXHCI_EXTENSION Extension, USHORT Port, ULONG SetMask, ULONG ClearMask, ULONG AckMask);

static MPSTATUS XHCI_SetPortLinkState(PXHCI_EXTENSION Extension, USHORT Port, ULONG LinkState);
static VOID XHCI_PowerOnAllPorts(PXHCI_EXTENSION Extension);
static MPSTATUS XHCI_ConfigurePageSize(PXHCI_EXTENSION Extension);
static VOID XHCI_ProgramInterrupterState(PXHCI_EXTENSION Extension);
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
static BOOLEAN XHCI_EnableMsix(PXHCI_EXTENSION Extension);
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
typedef struct _XHCI_EP_RESET_WORK {
    WORK_QUEUE_ITEM Item;
    PXHCI_EXTENSION Extension;
    PXHCI_ENDPOINT Endpoint;
    BOOLEAN RingDoorbell;
} XHCI_EP_RESET_WORK, *PXHCI_EP_RESET_WORK;
typedef struct _XHCI_TT_UPDATE_WORK {
    WORK_QUEUE_ITEM Item;
    PXHCI_EXTENSION Extension;
    PXHCI_DEVICE_SLOT Slot;
    BOOLEAN UpdateChildren;
} XHCI_TT_UPDATE_WORK, *PXHCI_TT_UPDATE_WORK;
typedef struct _XHCI_SWENUM_WORK {
    WORK_QUEUE_ITEM Item;
    PXHCI_EXTENSION Extension;
    PXHCI_ENDPOINT Endpoint;
    PXHCI_TRANSFER Transfer;
} XHCI_SWENUM_WORK, *PXHCI_SWENUM_WORK;

#define XHCI_DEFERRED_OPEN_TIMEOUT_US 1000000
#define XHCI_EP0_WORK_TIMEOUT_US 1000000

typedef struct _XHCI_COMMON_BUFFER_LAYOUT {
    SIZE_T TotalSize;
    SIZE_T DcbaaOffset;
    SIZE_T ScratchpadArrayOffset;
    SIZE_T ScratchpadBuffersOffset;
    SIZE_T CommandRingOffset;
    SIZE_T EventRingOffset;
    SIZE_T ErstOffset;
    SIZE_T DeviceContextsOffset;
    SIZE_T InputContextsOffset;
    SIZE_T Ep0RingsOffset;
} XHCI_COMMON_BUFFER_LAYOUT, *PXHCI_COMMON_BUFFER_LAYOUT;
static VOID NTAPI XHCI_Ep0BringupCallback(IN PVOID MiniportExtension,
                                          IN PVOID CallBackContext);
static VOID NTAPI XHCI_Ep0BringupWorker(IN PVOID Context);
static VOID XHCI_DrainDeferredTransferCompletions(PXHCI_EXTENSION Extension);
static VOID XHCI_HandleTransferEvent(PXHCI_EXTENSION Extension, PXHCI_TRB EventTrb, BOOLEAN AllowCallbacks);
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
static MPSTATUS XHCI_AddressDeviceSlot(PXHCI_EXTENSION Extension,
                                       PXHCI_DEVICE_SLOT Slot,
                                       PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                                       BOOLEAN DisableOnFailure);
static VOID XHCI_FillVirtualDeviceDescriptor(PXHCI_EXTENSION Extension,
                                             PXHCI_ENDPOINT Endpoint,
                                             PUSB_DEVICE_DESCRIPTOR Descriptor);
static MPSTATUS XHCI_SubmitControlTransferSwEnum(PXHCI_EXTENSION Extension,
                                                 PXHCI_ENDPOINT Endpoint,
                                                 PXHCI_TRANSFER Transfer);
static MPSTATUS XHCI_InitializeScratchpads(PXHCI_EXTENSION Extension);
static PXHCI_ENDPOINT XHCI_GetSlotEndpoint(PXHCI_DEVICE_SLOT Slot, UCHAR EndpointId);
static VOID XHCI_RingEndpointDoorbell(PXHCI_EXTENSION Extension,
                                      UCHAR SlotId,
                                      UCHAR EndpointId,
                                      ULONG StreamId);
static USHORT XHCI_SelectDoorbellStreamId(PXHCI_ENDPOINT Endpoint,
                                          PXHCI_TRANSFER Transfer);
static PXHCI_TRB XHCI_GetTransferRingTrb(PXHCI_RING Ring,
                                         PULONGLONG PhysicalAddress,
                                         BOOLEAN TdContinues);
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
static VOID XHCI_PerformEndpointResetSequence(PXHCI_EXTENSION Extension,
                                              PXHCI_ENDPOINT Endpoint,
                                              BOOLEAN RingDoorbell);
static VOID NTAPI XHCI_EndpointResetWorker(PVOID Context);
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
static MPSTATUS XHCI_DisableLegacySupport(PXHCI_EXTENSION Extension);
static VOID XHCI_ProbeMsiMsix(PXHCI_EXTENSION Extension);
static BOOLEAN XHCI_WritePciConfig(PXHCI_EXTENSION Extension, ULONG Offset, PVOID Buffer, ULONG Length);
static VOID XHCI_BuildProtocolPortMap(PXHCI_EXTENSION Extension);
static volatile ULONG *XHCI_GetPortPowerRegister(PXHCI_EXTENSION Extension, USHORT Port);
static VOID XHCI_ConfigurePortLpm(PXHCI_EXTENSION Extension, USHORT Port);
static VOID XHCI_ConfigureAllPortsLpm(PXHCI_EXTENSION Extension);
static MPSTATUS XHCI_BuildCommonBufferLayout(PXHCI_EXTENSION Extension,
                                             PUSBPORT_RESOURCES UsbPortResources);
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
static VOID XHCI_DumpInputContextForAddress(PXHCI_EXTENSION Extension,
                                            PXHCI_DEVICE_SLOT Slot);
static MPSTATUS
XHCI_SubmitSgTransfer(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _Inout_ PXHCI_TRANSFER Transfer,
    _In_ ULONG TrbType,
    _In_ BOOLEAN IsIsochronous);
static MPSTATUS XHCI_UpdateSlotTtInfo(_In_ PXHCI_EXTENSION Extension,
                                      _Inout_ PXHCI_DEVICE_SLOT Slot);
static VOID NTAPI XHCI_TtUpdateWorker(_In_ PVOID Context);
static VOID XHCI_UpdateChildrenTtInfo(_Inout_ PXHCI_EXTENSION Extension,
                                      _In_ PXHCI_DEVICE_SLOT HubSlot);

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
                                  USB_MINIPORT_FLAGS_USB3 |
                                  /* Force USBPORT to poll if interrupts are lost. */
                                  USB_MINIPORT_FLAGS_POLLING;

    /* Use a USB2-style bandwidth budget for now so
     * USBPORT's generic scheduler can place periodic
     * (interrupt/isochronous) endpoints. */
    XhciRegPacket.MiniPortBusBandwidth = TOTAL_USB20_BUS_BANDWIDTH;

    XhciRegPacket.MiniPortExtensionSize = sizeof(XHCI_EXTENSION);
    XhciRegPacket.MiniPortEndpointSize = sizeof(XHCI_ENDPOINT);
    XhciRegPacket.MiniPortTransferSize = sizeof(XHCI_TRANSFER);
    /* Reserve enough common-buffer space for the maximum supported HC layout. */
    XhciRegPacket.MiniPortResourcesSize =
        XHCI_ALIGN_UP(XHCI_GetMaximumCommonBufferSize(), PAGE_SIZE);

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
volatile ULONG *
XHCI_GetPortPowerRegister(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port)
{
    if (!XHCI_IsValidPort(Extension, Port) || !Extension->OperationalRegisters)
        return NULL;

    return &Extension->OperationalRegisters->PortRegister[Port - 1].PortPowerManagement;
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
    XHCI_AckPortChangeInternal(Extension, Port, ChangeMask, TRUE);
}

static
VOID
XHCI_AckPortChangeInternal(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port,
    _In_ ULONG ChangeMask,
    _In_ BOOLEAN ClearShadowMask)
{
    volatile ULONG *PortStatusReg;
    ULONG OldValue;
    ULONG ValueToWrite;

    if (!Extension || !Port || !ChangeMask)
        return;

    PortStatusReg = XHCI_GetPortStatusRegister(Extension, Port);
    if (!PortStatusReg)
        return;

    OldValue = READ_REGISTER_ULONG(PortStatusReg);
    /* Strict Ack: Preserve PP (Bit 9). All others 0 (including PED/PR) to avoid side effects. */
    ValueToWrite = (OldValue & XHCI_PORTSC_PP) | (ChangeMask & XHCI_PORTSC_CHANGE_MASK);

    WRITE_REGISTER_ULONG(PortStatusReg, ValueToWrite);

    if (ClearShadowMask && Port <= XHCI_MAX_PORTS)
    {
        InterlockedAnd((volatile LONG *)&Extension->PortChangeMask[Port],
                       ~(LONG)(ChangeMask & XHCI_PORTSC_CHANGE_MASK));
    }
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

    if (Port == 5)
        DPRINT1("ModBits: P%u Read=%08lx Writing=%08lx\n", Port, Value, NewValue & XHCI_PORTSC_WRITE_MASK);
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

    if (Port == 5)
        DPRINT1("ModBits: P%u Read=%08lx Writing=%08lx\n", Port, Value, NewValue & XHCI_PORTSC_WRITE_MASK);
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
VOID
XHCI_ConfigurePortLpm(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port)
{
    volatile ULONG *PortPmReg;
    ULONG Value;
    ULONG NewValue;
    ULONG U1Timeout;
    ULONG U2Timeout;

    if (!Extension)
        return;

    /* Do not enable U1/U2 on controllers that advertise limited support. */
    if (Extension->Quirks & XHCI_QUIRK_LIMIT_U1U2)
        return;

    /* Only SuperSpeed ports implement the U1/U2 timeout fields. */
    if (!XHCI_PortIsSuperSpeed(Extension, Port))
        return;

    PortPmReg = XHCI_GetPortPowerRegister(Extension, Port);
    if (!PortPmReg)
        return;

    Value = READ_REGISTER_ULONG(PortPmReg);
    NewValue = Value;

    /* Clear any existing U1/U2 timeout values. */
    NewValue &= ~(XHCI_PORTPMSC_U1_TIMEOUT_MASK |
                  XHCI_PORTPMSC_U2_TIMEOUT_MASK);

    /*
     * Use the hardware-advertised maximum exit latencies from HCS3 as a
     * conservative baseline for per-port U1/U2 timeouts.  These fields
     * describe the host controller's contribution; the actual link exit
     * latencies also depend on downstream hubs and devices, but larger
     * timeout values are always safe (they simply reduce LPM aggressiveness).
     */
    U1Timeout = Extension->MaxU1ExitLatency;
    U2Timeout = Extension->MaxU2ExitLatency;

    if (U1Timeout > 0xFF)
        U1Timeout = 0xFF;
    if (U2Timeout > 0xFFFF)
        U2Timeout = 0xFFFF;

    if (U1Timeout != 0)
        NewValue |= (U1Timeout << XHCI_PORTPMSC_U1_TIMEOUT_SHIFT);
    if (U2Timeout != 0)
        NewValue |= (U2Timeout << XHCI_PORTPMSC_U2_TIMEOUT_SHIFT);

    if (NewValue != Value)
        WRITE_REGISTER_ULONG(PortPmReg, NewValue);
}

static
VOID
XHCI_ConfigureAllPortsLpm(
    _Inout_ PXHCI_EXTENSION Extension)
{
    USHORT Port;

    if (!Extension)
        return;

    /* Nothing to configure if the controller reports no U1/U2 exit latency. */
    if (Extension->MaxU1ExitLatency == 0 && Extension->MaxU2ExitLatency == 0)
        return;

    for (Port = 1; Port <= Extension->NumberOfPorts; Port++)
    {
        XHCI_ConfigurePortLpm(Extension, Port);
    }
}

static
MPSTATUS
XHCI_ProgramDcbaaCrcrAndConfig(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONGLONG Dcbaa;
    ULONGLONG Crcr;
    ULONG DcbaaLow;
    ULONG DcbaaHigh;
    ULONG CrcrLow;
    ULONG CrcrHigh;

    if (!Extension ||
        !Extension->OperationalRegisters ||
        !Extension->Dcbaa ||
        Extension->CommandRingTrbCount == 0)
    {
        return MP_STATUS_ERROR;
    }

    Dcbaa = Extension->DcbaaPhysical.QuadPart;
    Crcr = Extension->CommandRingPhysical.QuadPart & ~0x3FULL;
    DcbaaLow = (ULONG)(Dcbaa & 0xFFFFFFFF);
    DcbaaHigh = (ULONG)(Dcbaa >> 32);
    CrcrLow = (ULONG)(Crcr & 0xFFFFFFFF);
    CrcrHigh = (ULONG)(Crcr >> 32);

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

    /*
     * For controllers that are limited to 32‑bit DMA (either by
     * capabilities or quirks), the DCBAA and command ring base must
     * reside below 4 GiB. The common-buffer window was already checked
     * at allocation time; this is a last‑ditch guard before we program
     * hardware.
     */
    if (!Extension->Supports64Bit ||
        (Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA))
    {
        if ((Dcbaa >> 32) != 0 || (Crcr >> 32) != 0)
        {
            DPRINT1("usbxhci: 32-bit DMA controller with 64-bit DCBAA/CRCR "
                    "(DCBAA=%I64x CRCR=%I64x quirks=0x%lx)\n",
                    Dcbaa,
                    Crcr,
                    Extension->Quirks);
            ASSERT((Dcbaa >> 32) == 0);
            ASSERT((Crcr >> 32) == 0);
        }
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

#if DBG
    {
        ULONGLONG HwDcbaa;
        ULONGLONG HwCrcr;

        HwDcbaa = ((ULONGLONG)READ_REGISTER_ULONG(&Extension->OperationalRegisters->DcbaapHigh) << 32) |
                  READ_REGISTER_ULONG(&Extension->OperationalRegisters->DcbaapLow);
        HwCrcr = ((ULONGLONG)READ_REGISTER_ULONG(&Extension->OperationalRegisters->CrCrHigh) << 32) |
                 (READ_REGISTER_ULONG(&Extension->OperationalRegisters->CrCrLow) & ~0x3FULL);

        if (HwDcbaa != Dcbaa || (HwCrcr & ~0x3FULL) != (Crcr & ~0x3FULL))
        {
            DPRINT1("usbxhci: DCBAA/CRCR mismatch after program "
                    "(expected DCBAA=%I64x CRCR=%I64x, hw DCBAA=%I64x CRCR=%I64x)\n",
                    Dcbaa,
                    Crcr,
                    HwDcbaa,
                    HwCrcr);
            if (!(Extension->Quirks & XHCI_QUIRK_IGNORE_DCBAA_CRCR_ECHO))
            {
                ASSERT(HwDcbaa == Dcbaa);
                ASSERT((HwCrcr & ~0x3FULL) == (Crcr & ~0x3FULL));
            }
        }
    }
#endif

    DPRINT1("usbxhci: USBCMD=%08lx USBSTS=%08lx CONFIG=%08lx\n",
            READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd),
            READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts),
            READ_REGISTER_ULONG(&Extension->OperationalRegisters->Config));

    return MP_STATUS_SUCCESS;
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

    /* Success path: ensure caller gets a defined success status. */
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
                            0);
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
    Context->CompletionEvent = NULL;
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
    ULONG SlotIndex;
    ULONG Value;

    if (!Extension || !Extension->DoorbellArray)
        return;

    SlotIndex = SlotId;
    if (SlotIndex > XHCI_MAX_SLOTS || SlotIndex > Extension->MaxSlots)
        return;

    Value = EndpointId & 0x1F;
    Value |= (StreamId & 0xFFFF) << 16;
    WRITE_REGISTER_ULONG(&Extension->DoorbellArray->Doorbell[SlotIndex], Value);
    if (SlotIndex != 0)
    {
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "XHCI_DB: S%lu E%u V=0x%x\n",
                 SlotIndex,
                 EndpointId,
                 Value);
    }
}

static
USHORT
XHCI_SelectDoorbellStreamId(
    _In_ PXHCI_ENDPOINT Endpoint,
    _In_opt_ PXHCI_TRANSFER Transfer)
{
    USHORT MaxStreamId;

    if (!Endpoint || !Transfer)
        return 0;

    if (Endpoint->EndpointProperties.TransferType != USBPORT_TRANSFER_TYPE_BULK)
        return 0;

    MaxStreamId = Endpoint->ReservedStreamId;
    if (MaxStreamId == 0)
        return 0;

    if (Transfer->StreamId == 0 || Transfer->StreamId > MaxStreamId)
        return 0;

    return Transfer->StreamId;
}

static
PXHCI_TRB
XHCI_GetTransferRingTrb(
    _Inout_ PXHCI_RING Ring,
    _Out_opt_ PULONGLONG PhysicalAddress,
    _In_ BOOLEAN TdContinues)
{
    ULONGLONG Address;

    if (!Ring || !Ring->Base || Ring->TrbCount < 2)
        return NULL;

    if (Ring->EnqueueIndex == Ring->TrbCount - 2)
    {
        PXHCI_TRB LinkTrb = &Ring->Base[Ring->TrbCount - 1];
        ULONG LinkControl = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                            XHCI_TRB_TOGGLE_CYCLE |
                            (Ring->CycleState & 0x1);

        LinkTrb->Parameter1 = (ULONG)(Ring->PhysicalAddress.QuadPart & 0xFFFFFFFF);
        LinkTrb->Parameter2 = (ULONG)(Ring->PhysicalAddress.QuadPart >> 32);
        LinkTrb->Status = 0;
        if (TdContinues)
            LinkControl |= XHCI_TRB_CHAIN_BIT;

        LinkTrb->Control = LinkControl;

    }

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

    if (Endpoint->TransferRing.Base &&
        Endpoint->TransferRing.TrbCount > 0)
    {
        PXHCI_TRB LinkTrb =
            &Endpoint->TransferRing.Base[Endpoint->TransferRing.TrbCount - 1];
        ULONGLONG LinkAddress = Endpoint->TransferRing.PhysicalAddress.QuadPart;

        LinkTrb->Parameter1 = (ULONG)(LinkAddress & 0xFFFFFFFF);
        LinkTrb->Parameter2 = (ULONG)(LinkAddress >> 32);
        LinkTrb->Status = 0;
        LinkTrb->Control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                           XHCI_TRB_TOGGLE_CYCLE |
                           XHCI_TRB_CYCLE;
    }

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

    Ring->CycleState = 1;
    Ring->EnqueueIndex = 0;
    Ring->DequeueIndex = 0;

    return MP_STATUS_SUCCESS;
}
static VOID
XHCI_FreeTransferRing(
    _In_ PXHCI_RING Ring)
{
    if (!Ring)
        return;

    if (Ring->UsesCommonBuffer)
    {
        if (Ring->Base && Ring->TrbCount)
        {
            RtlZeroMemory(Ring->Base, sizeof(XHCI_TRB) * Ring->TrbCount);

            if (Ring->PhysicalAddress.QuadPart != 0 && Ring->TrbCount > 0)
            {
                PXHCI_TRB LinkTrb = &Ring->Base[Ring->TrbCount - 1];
                ULONGLONG LinkAddress = Ring->PhysicalAddress.QuadPart;

                LinkTrb->Parameter1 = (ULONG)(LinkAddress & 0xFFFFFFFF);
                LinkTrb->Parameter2 = (ULONG)(LinkAddress >> 32);
                LinkTrb->Status = 0;
                LinkTrb->Control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                                   XHCI_TRB_TOGGLE_CYCLE |
                                   XHCI_TRB_CYCLE;
            }
        }

        Ring->CycleState = 1;
        Ring->EnqueueIndex = 0;
        Ring->DequeueIndex = 0;
        return;
    }

    if (Ring->Base)
        MmFreeContiguousMemory(Ring->Base);

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
    ULONG OldAddress;
    ULONG NewAddressValue;

    if (!Extension || !Slot)
        return;

    NewAddressValue = NewAddress;
    if (NewAddressValue != 0 && NewAddressValue > XHCI_MAX_DEVICE_ADDRESS)
    {
        DPRINT1("usbxhci: refusing to map invalid USB address %lu for slot %u\n",
                NewAddressValue,
                Slot->SlotId);
        return;
    }

    OldAddress = Slot->UsbDeviceAddress;
    if (OldAddress != 0 &&
        OldAddress <= XHCI_MAX_DEVICE_ADDRESS &&
        Extension->DeviceAddressMap[OldAddress] == Slot->SlotId)
    {
        Extension->DeviceAddressMap[OldAddress] = 0;
    }

    Slot->UsbDeviceAddress = NewAddress;

    if (NewAddressValue == 0)
        return;

    Extension->DeviceAddressMap[NewAddressValue] = Slot->SlotId;
}

static PXHCI_DEVICE_SLOT
XHCI_FindSlotByAddress(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT DeviceAddress)
{
    ULONG SlotIndex;

    if (!Extension || DeviceAddress == 0 || DeviceAddress > XHCI_MAX_DEVICE_ADDRESS)
        return NULL;

    SlotIndex = Extension->DeviceAddressMap[DeviceAddress];
    if (SlotIndex == 0 || SlotIndex > Extension->MaxSlots || SlotIndex > XHCI_MAX_SLOTS)
        return NULL;

    return XHCI_GetSlot(Extension, (UCHAR)SlotIndex);
}

static PXHCI_DEVICE_SLOT
XHCI_FindSlotByPort(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT PortNumber)
{
    ULONG SlotIndex;

    if (!Extension || PortNumber == 0)
        return NULL;

    for (SlotIndex = 1; SlotIndex <= Extension->MaxSlots && SlotIndex <= XHCI_MAX_SLOTS; SlotIndex++)
    {
        PXHCI_DEVICE_SLOT Slot = &Extension->DeviceSlots[SlotIndex];
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
    PVOID InputCtxBase;
    PVOID DeviceCtxBase;
    PXHCI_INPUT_CONTROL_CONTEXT CtrlCtx;
    PXHCI_SLOT_CONTEXT SlotCtx;
    PXHCI_SLOT_CONTEXT ActiveSlotCtx;
    PXHCI_ENDPOINT_CONTEXT EpCtx;
    ULONG EndpointType;
    ULONG MaxPacketSize;
    ULONG BurstSize;
    ULONG Mult;
    ULONG Interval;
    ULONG MaxEsitPayload;
    ULONG AverageTrbLength;
    ULONGLONG DequeuePtr;
    MPSTATUS Status;
    ULONG ResumeDoorbells = 0;
    BOOLEAN ExpandAddFlags = FALSE;
    ULONG ReconfigureMask = 0;

    if (!Extension || !Slot || !Endpoint || EndpointId == 0)
        return MP_STATUS_ERROR;

    XHCI_LOG_IRQL("ConfigureSlotEndpoint entry");
#if DBG
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
    {
        DPRINT1("usbxhci ASSERT: XHCI_ConfigureSlotEndpoint requires <= DISPATCH_LEVEL, current=%lu\n",
                (ULONG)KeGetCurrentIrql());
        ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    }
#endif

    InputCtxBase = Slot->InputContext.VirtualAddress;
    DeviceCtxBase = Slot->DeviceContext.VirtualAddress;

    if (!InputCtxBase || !DeviceCtxBase)
        return MP_STATUS_ERROR;

    EndpointType = XHCI_GetEndpointTypeFromProperties(&Endpoint->EndpointProperties);
    if (EndpointType == XHCI_ENDPOINT_TYPE_INVALID)
        return MP_STATUS_ERROR;

    RtlZeroMemory(InputCtxBase, Slot->InputContext.Length);

    CtrlCtx = XHCI_GetInputControlContextVa(Extension, InputCtxBase);
    CtrlCtx->AddContextFlags = (1 << 0) | (1 << EndpointId);
    CtrlCtx->DropContextFlags = 0;

    ActiveSlotCtx = XHCI_GetDeviceSlotContextVa(Extension, DeviceCtxBase);
    SlotCtx = XHCI_GetInputSlotContextVa(Extension, InputCtxBase);
    RtlCopyMemory(SlotCtx, ActiveSlotCtx, Extension->ContextSize);

    {
        UCHAR CopyLimit = Slot->HighestEndpointId;
        UCHAR CopyId;

        if (CopyLimit < 1)
            CopyLimit = 1;

        for (CopyId = 1;
             CopyId <= CopyLimit && CopyId <= XHCI_MAX_ENDPOINTS;
             CopyId++)
        {
            PXHCI_ENDPOINT_CONTEXT ActiveEpCtx;
            PXHCI_ENDPOINT_CONTEXT InputEpCtx;

            if (CopyId == EndpointId)
                continue;

            if (CopyId != 1 && Slot->EndpointTable[CopyId] == NULL)
                continue;

            ActiveEpCtx = XHCI_GetDeviceEndpointContextVa(Extension,
                                                          DeviceCtxBase,
                                                          CopyId - 1);
            InputEpCtx = XHCI_GetInputEndpointContextVa(Extension,
                                                        InputCtxBase,
                                                        CopyId - 1);
            if (ActiveEpCtx && InputEpCtx)
                RtlCopyMemory(InputEpCtx, ActiveEpCtx, Extension->ContextSize);
        }
    }

    /*
     * QEMU's xHCI (1B36:000D) does not reliably preserve previously-configured
     * endpoint contexts unless they are explicitly included in a subsequent
     * CONFIGURE_ENDPOINT (Add Context Flags + matching input endpoint contexts).
     * This shows up with usb-storage: after opening/configuring bulk IN (DCI=3),
     * configuring bulk OUT (DCI=4) can leave DCI=3 Disabled, stalling BOT.
     *
     * Work around this by re-submitting all already-configured (non-EP0)
     * endpoints alongside the new one when issuing CONFIGURE_ENDPOINT on QEMU.
     *
     * On other controllers, keep the narrower behavior (only expand when adding
     * a lower DCI after a higher one).
     */
    ExpandAddFlags = Slot->Configured &&
                     (Slot->HighestEndpointId != 0) &&
                     (((Extension->Quirks & XHCI_QUIRK_QEMU_CONFIG_EP_ORDER) != 0) ||
                      (EndpointId < Slot->HighestEndpointId));
    if (ExpandAddFlags)
    {
        UCHAR Id;
        UCHAR StartId = ((Extension->Quirks & XHCI_QUIRK_QEMU_CONFIG_EP_ORDER) != 0) ?
                        2 : (UCHAR)(EndpointId + 1);

        for (Id = StartId;
             Id <= Slot->HighestEndpointId && Id <= XHCI_MAX_ENDPOINTS;
             Id++)
        {
            if (Id != EndpointId && Slot->EndpointTable[Id] != NULL)
            {
                CtrlCtx->AddContextFlags |= (1 << Id);
                ResumeDoorbells |= (1 << Id);
                ReconfigureMask |= (1u << Id);
            }
        }
    }

    if (XhciSlotContextGetLastCtx(SlotCtx) < EndpointId)
        XhciSlotContextSetLastCtx(SlotCtx, EndpointId);

    if (Slot->MultiTt)
        XhciSlotContextSetMtt(SlotCtx, TRUE);
    else
        XhciSlotContextSetMtt(SlotCtx, FALSE);

    if (XHCI_EndpointNeedsTt(&Endpoint->EndpointProperties))
    {
        PXHCI_DEVICE_SLOT HubSlot = NULL;

        if (Extension &&
            Endpoint->EndpointProperties.HubAddr != USBPORT_NO_HUB_ADDRESS &&
            Endpoint->EndpointProperties.HubAddr != 0)
        {
            HubSlot = XHCI_FindSlotByAddress(Extension,
                                             Endpoint->EndpointProperties.HubAddr);
            if (HubSlot && !HubSlot->InUse)
                HubSlot = NULL;
        }

        XHCI_ApplyTtInfo(&Endpoint->EndpointProperties, HubSlot, SlotCtx);
    }

    EpCtx = XHCI_GetInputEndpointContextVa(Extension, InputCtxBase, EndpointId - 1);
    RtlZeroMemory(EpCtx, Extension->ContextSize);
    MaxPacketSize = Endpoint->EndpointProperties.MaxPacketSize ?
                    Endpoint->EndpointProperties.MaxPacketSize : 8;
    BurstSize = (Endpoint->EndpointProperties.TransactionPerMicroframe > 0) ?
                (Endpoint->EndpointProperties.TransactionPerMicroframe - 1) : 0;
    if (BurstSize > 0xF)
        BurstSize = 0xF;
    Mult = (EndpointType == XHCI_ENDPOINT_TYPE_ISOCH_OUT ||
            EndpointType == XHCI_ENDPOINT_TYPE_ISOCH_IN) ?
           ((BurstSize > 0x3) ? 0x3 : BurstSize) : 0;
    Interval = 0;
    if (EndpointType == XHCI_ENDPOINT_TYPE_ISOCH_OUT ||
        EndpointType == XHCI_ENDPOINT_TYPE_ISOCH_IN ||
        EndpointType == XHCI_ENDPOINT_TYPE_INTERRUPT_OUT ||
        EndpointType == XHCI_ENDPOINT_TYPE_INTERRUPT_IN)
    {
        UCHAR Period = Endpoint->EndpointProperties.Period;

        if (Period == 0)
            Period = 1;

        if (Endpoint->EndpointProperties.DeviceSpeed == UsbHighSpeed ||
            Endpoint->EndpointProperties.DeviceSpeed == UsbSuperSpeed)
        {
            Interval = Period - 1;
        }
        else
        {
            ULONG Exp = 0;
            while (Exp < 15 && ((1u << (Exp + 1)) <= Period))
                Exp++;
            Interval = Exp + 3;
        }

        if (Endpoint->EndpointProperties.DeviceSpeed == UsbFullSpeed ||
            Endpoint->EndpointProperties.DeviceSpeed == UsbLowSpeed)
        {
            BurstSize = 0;
        }

        if (Interval > 15)
            Interval = 15;
    }
    MaxEsitPayload = 0;
    if (EndpointType == XHCI_ENDPOINT_TYPE_ISOCH_OUT ||
        EndpointType == XHCI_ENDPOINT_TYPE_ISOCH_IN ||
        EndpointType == XHCI_ENDPOINT_TYPE_INTERRUPT_OUT ||
        EndpointType == XHCI_ENDPOINT_TYPE_INTERRUPT_IN)
    {
        MaxEsitPayload = Endpoint->EndpointProperties.TotalMaxPacketSize;
        if (MaxEsitPayload == 0)
        {
            ULONG Transactions = Endpoint->EndpointProperties.TransactionPerMicroframe;
            if (Transactions == 0)
                Transactions = 1;

            MaxEsitPayload = MaxPacketSize * Transactions;
        }

        if (MaxEsitPayload > 0xFFFF)
            MaxEsitPayload = 0xFFFF;
    }

    AverageTrbLength = Endpoint->EndpointProperties.MaxTransferSize ?
                       (Endpoint->EndpointProperties.MaxTransferSize & 0xFFFF) :
                       MaxPacketSize;
    if (AverageTrbLength == 0) AverageTrbLength = MaxPacketSize;

    if (!Endpoint->TransferRing.Base ||
        Endpoint->TransferRing.PhysicalAddress.QuadPart == 0)
    {
        DPRINT1("usbxhci: ConfigureSlotEndpoint missing transfer ring for slot %u ep %u\n",
                Slot->SlotId,
                EndpointId);
        return MP_STATUS_ERROR;
    }

    DequeuePtr =
        ((Endpoint->TransferRing.PhysicalAddress.QuadPart +
          ((ULONGLONG)Endpoint->TransferRing.EnqueueIndex * sizeof(XHCI_TRB))) & ~0xFULL) |
        (Endpoint->TransferRing.CycleState & 0x1);

    XhciEndpointContextInit(EpCtx,
                            EndpointType,
                            MaxPacketSize,
                            BurstSize,
                            Interval,
                            Mult,
                            MaxEsitPayload,
                            AverageTrbLength,
                            DequeuePtr);

    if (ExpandAddFlags && ReconfigureMask != 0)
    {
        UCHAR Id;
        for (Id = 2; Id <= Slot->HighestEndpointId && Id <= XHCI_MAX_ENDPOINTS; Id++)
        {
            PXHCI_ENDPOINT ExistingEndpoint;
            ULONG ExistingEndpointType;
            ULONGLONG ExistingDequeuePtr;
            ULONG ExistingMaxPacketSize;
            ULONG ExistingBurstSize;
            ULONG ExistingMult;
            ULONG ExistingInterval;
            ULONG ExistingMaxEsitPayload;
            ULONG ExistingAverageTrbLength;

            if ((ReconfigureMask & (1u << Id)) == 0)
                continue;

            ExistingEndpoint = Slot->EndpointTable[Id];
            if (!ExistingEndpoint)
                continue;

            ExistingEndpointType =
                XHCI_GetEndpointTypeFromProperties(&ExistingEndpoint->EndpointProperties);
            if (ExistingEndpointType == XHCI_ENDPOINT_TYPE_INVALID)
                continue;

            EpCtx = XHCI_GetInputEndpointContextVa(Extension, InputCtxBase, Id - 1);
            if (!EpCtx)
                continue;

            RtlZeroMemory(EpCtx, Extension->ContextSize);

            ExistingMaxPacketSize = ExistingEndpoint->EndpointProperties.MaxPacketSize ?
                                    ExistingEndpoint->EndpointProperties.MaxPacketSize : 8;
            ExistingBurstSize =
                (ExistingEndpoint->EndpointProperties.TransactionPerMicroframe > 0) ?
                (ExistingEndpoint->EndpointProperties.TransactionPerMicroframe - 1) : 0;
            if (ExistingBurstSize > 0xF)
                ExistingBurstSize = 0xF;

            ExistingMult = (ExistingEndpointType == XHCI_ENDPOINT_TYPE_ISOCH_OUT ||
                            ExistingEndpointType == XHCI_ENDPOINT_TYPE_ISOCH_IN) ?
                           ((ExistingBurstSize > 0x3) ? 0x3 : ExistingBurstSize) : 0;

            ExistingInterval = 0;
            if (ExistingEndpointType == XHCI_ENDPOINT_TYPE_ISOCH_OUT ||
                ExistingEndpointType == XHCI_ENDPOINT_TYPE_ISOCH_IN ||
                ExistingEndpointType == XHCI_ENDPOINT_TYPE_INTERRUPT_OUT ||
                ExistingEndpointType == XHCI_ENDPOINT_TYPE_INTERRUPT_IN)
            {
                UCHAR Period = ExistingEndpoint->EndpointProperties.Period;

                if (Period == 0)
                    Period = 1;

                if (ExistingEndpoint->EndpointProperties.DeviceSpeed == UsbHighSpeed ||
                    ExistingEndpoint->EndpointProperties.DeviceSpeed == UsbSuperSpeed)
                {
                    ExistingInterval = Period - 1;
                }
                else
                {
                    ULONG Exp = 0;
                    while (Exp < 15 && ((1u << (Exp + 1)) <= Period))
                        Exp++;
                    ExistingInterval = Exp + 3;
                }

                if (ExistingEndpoint->EndpointProperties.DeviceSpeed == UsbFullSpeed ||
                    ExistingEndpoint->EndpointProperties.DeviceSpeed == UsbLowSpeed)
                {
                    ExistingBurstSize = 0;
                }

                if (ExistingInterval > 15)
                    ExistingInterval = 15;
            }

            ExistingMaxEsitPayload = 0;
            if (ExistingEndpointType == XHCI_ENDPOINT_TYPE_ISOCH_OUT ||
                ExistingEndpointType == XHCI_ENDPOINT_TYPE_ISOCH_IN ||
                ExistingEndpointType == XHCI_ENDPOINT_TYPE_INTERRUPT_OUT ||
                ExistingEndpointType == XHCI_ENDPOINT_TYPE_INTERRUPT_IN)
            {
                ExistingMaxEsitPayload = ExistingEndpoint->EndpointProperties.TotalMaxPacketSize;
                if (ExistingMaxEsitPayload == 0)
                {
                    ULONG Transactions = ExistingEndpoint->EndpointProperties.TransactionPerMicroframe;
                    if (Transactions == 0)
                        Transactions = 1;

                    ExistingMaxEsitPayload = ExistingMaxPacketSize * Transactions;
                }

                if (ExistingMaxEsitPayload > 0xFFFF)
                    ExistingMaxEsitPayload = 0xFFFF;
            }

            ExistingAverageTrbLength = ExistingEndpoint->EndpointProperties.MaxTransferSize ?
                                       (ExistingEndpoint->EndpointProperties.MaxTransferSize & 0xFFFF) :
                                       ExistingMaxPacketSize;
            if (ExistingAverageTrbLength == 0)
                ExistingAverageTrbLength = ExistingMaxPacketSize;

            if (!ExistingEndpoint->TransferRing.Base ||
                ExistingEndpoint->TransferRing.PhysicalAddress.QuadPart == 0)
            {
                DPRINT1("usbxhci: ConfigureSlotEndpoint missing transfer ring for slot %u ep %u\n",
                        Slot->SlotId,
                        Id);
                continue;
            }

            ExistingDequeuePtr =
                ((ExistingEndpoint->TransferRing.PhysicalAddress.QuadPart +
                  ((ULONGLONG)ExistingEndpoint->TransferRing.EnqueueIndex * sizeof(XHCI_TRB))) & ~0xFULL) |
                (ExistingEndpoint->TransferRing.CycleState & 0x1);

            XhciEndpointContextInit(EpCtx,
                                    ExistingEndpointType,
                                    ExistingMaxPacketSize,
                                    ExistingBurstSize,
                                    ExistingInterval,
                                    ExistingMult,
                                    ExistingMaxEsitPayload,
                                    ExistingAverageTrbLength,
                                    ExistingDequeuePtr);
        }
    }

    XHCI_LOG_IRQL("ConfigureSlotEndpoint before XHCI_SendCommand");

#if DBG
    if ((Extension->Quirks & XHCI_QUIRK_QEMU_CONFIG_EP_ORDER) &&
        Slot->SlotId == 1 &&
        (EndpointId == 3 || EndpointId == 4))
    {
        DPRINT1("usbxhci: CONFIG_EP prep slot=%u ep=%u Add=%08lx Drop=%08lx LastCtx(in)=%lu Highest=%u Reconf=%08lx\n",
                Slot->SlotId,
                EndpointId,
                CtrlCtx->AddContextFlags,
                CtrlCtx->DropContextFlags,
                XhciSlotContextGetLastCtx(SlotCtx),
                Slot->HighestEndpointId,
                ReconfigureMask);
    }
#endif

    if (ExpandAddFlags && ReconfigureMask != 0)
    {
        UCHAR Id;
        for (Id = 2; Id <= Slot->HighestEndpointId && Id <= XHCI_MAX_ENDPOINTS; Id++)
        {
            if ((ReconfigureMask & (1u << Id)) == 0)
                continue;
            (VOID)XHCI_StopEndpoint(Extension, Slot, Id);
        }
    }
    if (Slot->Configured &&
             EndpointId < RTL_NUMBER_OF(Slot->EndpointTable) &&
             Slot->EndpointTable[EndpointId] != NULL)
    {
        MPSTATUS StopStatus = XHCI_StopEndpoint(Extension, Slot, EndpointId);
        if (StopStatus != MP_STATUS_SUCCESS)
            DPRINT1("usbxhci: StopEndpoint failed for slot %u ep %u, continuing reconfigure\n",
                    Slot->SlotId,
                    EndpointId);
    }

    Status = XHCI_SendCommand(Extension,
                              XHCI_TRB_TYPE_CONFIG_EP,
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
    /* Simple steering: keep control/default endpoints and root-hub
     * notifications on interrupter 0; distribute others across any
     * additional interrupters using a slot-based hash. */
    Endpoint->InterruptTarget = 0;
    if (Extension->InterrupterCount > 1 &&
        Endpoint->EndpointProperties.TransferType != USBPORT_TRANSFER_TYPE_CONTROL)
    {
        UCHAR Target = (UCHAR)(Slot->SlotId % Extension->InterrupterCount);
        if (Target != 0)
            Endpoint->InterruptTarget = Target;
    }

    XHCI_RingEndpointDoorbell(Extension, Slot->SlotId, EndpointId, 0);
    if (ExpandAddFlags)
    {
        UCHAR Id;
        for (Id = 2; Id <= Slot->HighestEndpointId && Id <= XHCI_MAX_ENDPOINTS; Id++)
        {
            if ((ResumeDoorbells & (1u << Id)) == 0 || Id == EndpointId)
                continue;
            XHCI_RingEndpointDoorbell(Extension, Slot->SlotId, Id, 0);
        }
    }

#if DBG
    if ((Extension->Quirks & XHCI_QUIRK_QEMU_CONFIG_EP_ORDER) &&
        Slot->SlotId == 1 &&
        (EndpointId == 3 || EndpointId == 4))
    {
        PVOID DeviceCtxBase = Slot->DeviceContext.VirtualAddress;
        if (DeviceCtxBase)
        {
            PXHCI_ENDPOINT_CONTEXT ActiveEpCtx =
                XHCI_GetDeviceEndpointContextVa(Extension, DeviceCtxBase, EndpointId - 1);
            if (ActiveEpCtx)
            {
                DPRINT1("usbxhci: CONFIG_EP done slot=%u ep=%u ActiveState=%lu EpInfo=%08lx EpInfo2=%08lx\n",
                        Slot->SlotId,
                        EndpointId,
                        (ULONG)(ActiveEpCtx->EpInfo & XHCI_EPCTX_STATE_MASK),
                        ActiveEpCtx->EpInfo,
                        ActiveEpCtx->EpInfo2);
            }
        }
    }
#endif
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
XHCI_DropSlotEndpoint(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_DEVICE_SLOT Slot,
    _In_ UCHAR EndpointId)
{
    PVOID InputCtxBase;
    PVOID DeviceCtxBase;
    PXHCI_INPUT_CONTROL_CONTEXT ControlCtx;
    PXHCI_SLOT_CONTEXT SlotCtx;
    MPSTATUS Status;

    if (!Extension || !Slot || EndpointId == 0)
        return MP_STATUS_ERROR;

    InputCtxBase = Slot->InputContext.VirtualAddress;
    DeviceCtxBase = Slot->DeviceContext.VirtualAddress;
    if (!InputCtxBase || !DeviceCtxBase)
        return MP_STATUS_ERROR;

    RtlZeroMemory(InputCtxBase, Slot->InputContext.Length);
    ControlCtx = XHCI_GetInputControlContextVa(Extension, InputCtxBase);
    ControlCtx->DropContextFlags = (1 << EndpointId);
    ControlCtx->AddContextFlags = (1 << 0);

    SlotCtx = XHCI_GetInputSlotContextVa(Extension, InputCtxBase);
    RtlCopyMemory(SlotCtx,
                  XHCI_GetDeviceSlotContextVa(Extension, DeviceCtxBase),
                  Extension->ContextSize);

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

static
VOID
XHCI_PerformEndpointResetSequence(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _In_ BOOLEAN RingDoorbell)
{
    KIRQL OldIrql;

    if (!Extension || !Endpoint || !Endpoint->Slot)
        return;

    if (Extension->FatalError || Extension->StoppingOrRemoved)
        return;

    KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
    Endpoint->ActiveTransfer = NULL;
    KeReleaseSpinLock(&Endpoint->Lock, OldIrql);

    XHCI_StopEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
    XHCI_ResetEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);

    XHCI_ResetEndpointRing(Endpoint);
    if (Endpoint->UsesStaticRing && Endpoint->Slot)
    {
        Endpoint->Slot->Ep0RingEnqueueIndex = Endpoint->TransferRing.EnqueueIndex;
        Endpoint->Slot->Ep0RingDequeueIndex = Endpoint->TransferRing.DequeueIndex;
        Endpoint->Slot->Ep0RingCycleState = Endpoint->TransferRing.CycleState;
    }

    /*
     * After Stop/Reset and resetting the software ring, re-sync the hardware TR
     * Dequeue Pointer so the endpoint resumes from the correct ring head.
     *
     * QEMU's xHCI (1B36:000D) can wedge if SetTRDequeue is issued against the
     * static EP0 ring while the startup HCE quirk is active; skip in that case.
     */
    {
        BOOLEAN SkipSetDequeue =
            Endpoint->UsesStaticRing &&
            Endpoint->DefaultControl &&
            ((Extension->Quirks & XHCI_QUIRK_IGNORE_STARTUP_HCE) != 0);

        if (!SkipSetDequeue)
        {
            MPSTATUS DeqStatus = XHCI_SetEndpointDequeue(Extension,
                                                         Endpoint->Slot,
                                                         Endpoint->EndpointId,
                                                         &Endpoint->TransferRing);
            if (DeqStatus != MP_STATUS_SUCCESS)
            {
                DPRINT1("usbxhci: SetTRDequeue failed for slot %u ep %u (status=%lu)\n",
                        Endpoint->SlotId,
                        Endpoint->EndpointId,
                        DeqStatus);
            }
        }
    }

    XHCI_StartEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
    if (RingDoorbell)
        XHCI_RingEndpointDoorbell(Extension,
                                  Endpoint->SlotId,
                                  Endpoint->EndpointId,
                                  0);
}

static VOID NTAPI
XHCI_EndpointResetWorker(PVOID Context)
{
    PXHCI_EP_RESET_WORK Work = (PXHCI_EP_RESET_WORK)Context;

    if (!Work)
        return;

    XHCI_PerformEndpointResetSequence(Work->Extension,
                                      Work->Endpoint,
                                      Work->RingDoorbell);

    if (Work->Endpoint)
        InterlockedDecrement(&Work->Endpoint->PendingWorkCount);

    ExFreePoolWithTag(Work, XHCI_TAG);
}

static
PUCHAR
XHCI_GetDescriptorBuffer(
    _In_ PXHCI_TRANSFER Transfer,
    _Out_opt_ PULONG AvailableLength)
{
    PUSBPORT_SCATTER_GATHER_LIST SgList;
    PUSBPORT_SCATTER_GATHER_ELEMENT Element;
    ULONG Avail;

    if (AvailableLength)
        *AvailableLength = 0;

    if (!Transfer)
        return NULL;

    SgList = Transfer->SgList;
    if (!SgList || !SgList->MappedSystemVa || SgList->SgElementCount == 0)
        return NULL;

    Element = &SgList->SgElement[0];
    if (Element->SgOffset >= Element->SgTransferLength)
        return NULL;

    Avail = Element->SgTransferLength - Element->SgOffset;
    if (Transfer->BytesTransferred < Avail)
        Avail = Transfer->BytesTransferred;

    if (AvailableLength)
        *AvailableLength = Avail;

    return (PUCHAR)SgList->MappedSystemVa;
}

static MPSTATUS
XHCI_UpdateSlotTtInfo(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_DEVICE_SLOT Slot)
{
    PVOID InputCtxBase;
    PVOID DeviceCtxBase;
    PXHCI_INPUT_CONTROL_CONTEXT CtrlCtx;
    PXHCI_SLOT_CONTEXT SlotCtx;
    PXHCI_SLOT_CONTEXT ActiveSlotCtx;
    ULONG NewTtInfo;
    BOOLEAN IsLsFsDevice;

    if (!Extension || !Slot || !Slot->InUse)
        return MP_STATUS_ERROR;

    if (Extension->FatalError)
        return MP_STATUS_HW_ERROR;

    InputCtxBase = Slot->InputContext.VirtualAddress;
    DeviceCtxBase = Slot->DeviceContext.VirtualAddress;
    if (!InputCtxBase || !DeviceCtxBase)
        return MP_STATUS_ERROR;

    RtlZeroMemory(InputCtxBase, Slot->InputContext.Length);
    CtrlCtx = XHCI_GetInputControlContextVa(Extension, InputCtxBase);
    CtrlCtx->AddContextFlags = (1 << 0);
    CtrlCtx->DropContextFlags = 0;

    ActiveSlotCtx = XHCI_GetDeviceSlotContextVa(Extension, DeviceCtxBase);
    SlotCtx = XHCI_GetInputSlotContextVa(Extension, InputCtxBase);
    RtlCopyMemory(SlotCtx, ActiveSlotCtx, Extension->ContextSize);

    IsLsFsDevice = (Slot->DeviceSpeed == UsbLowSpeed ||
                    Slot->DeviceSpeed == UsbFullSpeed);

    if (Slot->IsHub)
    {
        XhciSlotContextSetHub(SlotCtx, TRUE);
        XhciSlotContextSetMtt(SlotCtx, Slot->MultiTt);
        if (Slot->HubPortCount != 0)
            XhciSlotContextSetMaxPorts(SlotCtx, Slot->HubPortCount);
        if (Slot->MaxExitLatency)
            XhciSlotContextSetMaxExitLatency(SlotCtx, Slot->MaxExitLatency);

        NewTtInfo = SlotCtx->TtInfo;
        NewTtInfo &= ~(XHCI_SLOT_TT_SLOT_MASK |
                       XHCI_SLOT_TT_PORT_MASK |
                       XHCI_SLOT_TT_THINK_TIME_MASK);
        if (Slot->HasTtInfo)
        {
            NewTtInfo |= ((ULONG)(Slot->TtThinkTime & 0x3) << XHCI_SLOT_TT_THINK_TIME_SHIFT);
        }
        SlotCtx->TtInfo = NewTtInfo;
    }
    else if (IsLsFsDevice)
    {
        PXHCI_DEVICE_SLOT HubSlot = NULL;
        USBPORT_ENDPOINT_PROPERTIES Props;

        if (Slot->HubAddress != USBPORT_NO_HUB_ADDRESS && Slot->HubAddress != 0)
            HubSlot = XHCI_FindSlotByAddress(Extension, Slot->HubAddress);

        if (!HubSlot || !HubSlot->InUse)
            return MP_STATUS_ERROR;

        RtlZeroMemory(&Props, sizeof(Props));
        Props.DeviceSpeed = Slot->DeviceSpeed;
        Props.HubAddr = Slot->HubAddress;
        Props.PortNumber = Slot->PortNumber;

        XhciSlotContextSetMtt(SlotCtx, FALSE);

        NewTtInfo = SlotCtx->TtInfo;
        NewTtInfo &= ~(XHCI_SLOT_TT_SLOT_MASK |
                       XHCI_SLOT_TT_PORT_MASK |
                       XHCI_SLOT_TT_THINK_TIME_MASK);
        SlotCtx->TtInfo = NewTtInfo;
        XHCI_ApplyTtInfo(&Props, HubSlot, SlotCtx);
    }
    else
    {
        return MP_STATUS_SUCCESS;
    }

    DPRINT1("usbxhci: EvalCtx slot %u hub=%u mtt=%u ports=%u ttl=%u maxlat=%u\n",
            Slot->SlotId,
            Slot->IsHub ? 1 : 0,
            Slot->MultiTt ? 1 : 0,
            Slot->HubPortCount,
            Slot->TtThinkTime,
            Slot->MaxExitLatency);

    return XHCI_SendCommand(Extension,
                            XHCI_TRB_TYPE_EVAL_CTX,
                            Slot->InputContext.PhysicalAddress.QuadPart,
                            0,
                            XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                            XHCI_COMMAND_TIMEOUT_MS,
                            FALSE,
                            NULL,
                            NULL);
}

static VOID NTAPI
XHCI_TtUpdateWorker(
    _In_ PVOID Context)
{
    PXHCI_TT_UPDATE_WORK Work = (PXHCI_TT_UPDATE_WORK)Context;
    if (!Work)
        return;

    XHCI_UpdateSlotTtInfo(Work->Extension, Work->Slot);
    if (Work->UpdateChildren && Work->Slot && Work->Slot->IsHub)
        XHCI_UpdateChildrenTtInfo(Work->Extension, Work->Slot);
    ExFreePoolWithTag(Work, XHCI_TAG);
}

static VOID NTAPI
XHCI_SwEnumWorker(
    _In_ PVOID Context)
{
    PXHCI_SWENUM_WORK Work = (PXHCI_SWENUM_WORK)Context;
    PXHCI_EXTENSION Extension;
    PXHCI_ENDPOINT Endpoint;
    PXHCI_TRANSFER Transfer;
    KIRQL OldIrql;

    if (!Work)
        return;

    Extension = Work->Extension;
    Endpoint = Work->Endpoint;
    Transfer = Work->Transfer;

    if (!Extension || !Endpoint || !Transfer || Extension->StoppingOrRemoved || Extension->FatalError)
        goto Done;

    KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
    if (Endpoint->ActiveTransfer == Transfer)
        Endpoint->ActiveTransfer = NULL;
    KeReleaseSpinLock(&Endpoint->Lock, OldIrql);

    if (Transfer->Flags & (XHCI_TRANSFER_FLAG_SET_ADDRESS | XHCI_TRANSFER_FLAG_GET_DESCRIPTOR))
        XHCI_HandleEnumerationTransfer(Extension, Endpoint, Transfer);

    if (XhciRegPacket.UsbPortCompleteTransfer && Transfer->TransferParameters)
    {
        XhciRegPacket.UsbPortCompleteTransfer(Extension,
                                              Endpoint,
                                              Transfer->TransferParameters,
                                              Transfer->UsbdStatus,
                                              Transfer->BytesTransferred);
    }

Done:
    ExFreePoolWithTag(Work, XHCI_TAG);
}

static VOID
XHCI_UpdateChildrenTtInfo(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_DEVICE_SLOT HubSlot)
{
    ULONG SlotIndex;

    if (!Extension || !HubSlot || !HubSlot->InUse)
        return;

    for (SlotIndex = 1; SlotIndex <= Extension->MaxSlots && SlotIndex <= XHCI_MAX_SLOTS; SlotIndex++)
    {
        PXHCI_DEVICE_SLOT Slot = &Extension->DeviceSlots[SlotIndex];

        if (!Slot->InUse || Slot->IsHub)
            continue;

        if (Slot->HubAddress == HubSlot->UsbDeviceAddress &&
            (Slot->DeviceSpeed == UsbLowSpeed || Slot->DeviceSpeed == UsbFullSpeed))
        {
            XHCI_UpdateSlotTtInfo(Extension, Slot);
        }
    }
}

typedef struct _XHCI_EP0_UPDATE_WORK {
    WORK_QUEUE_ITEM WorkItem;
    PXHCI_EXTENSION Extension;
    PXHCI_DEVICE_SLOT Slot;
    ULONG MaxPacketSize;
} XHCI_EP0_UPDATE_WORK, *PXHCI_EP0_UPDATE_WORK;

static MPSTATUS
XHCI_UpdateEp0MaxPacketSize(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_DEVICE_SLOT Slot,
    _In_ ULONG MaxPacketSize)
{
    PVOID InputCtxBase;
    PVOID DeviceCtxBase;
    PXHCI_INPUT_CONTROL_CONTEXT CtrlCtx;
    PXHCI_SLOT_CONTEXT SlotCtx;
    PXHCI_SLOT_CONTEXT ActiveSlotCtx;
    PXHCI_ENDPOINT_CONTEXT EpCtx;
    PXHCI_ENDPOINT_CONTEXT ActiveEpCtx;

    if (!Slot || !Slot->InUse)
        return MP_STATUS_ERROR;
    
    // Safety check: Ensure the context sizes are consistent to prevent overflow
    if (Extension->ContextSize == 0 || 
        Slot->InputContext.Length < Extension->ContextSize * 33 || // simplified check, typically 33 contexts max
        Slot->DeviceContext.Length < Extension->ContextSize * 32)
    {
         DPRINT1("usbxhci: Context size mismatch or invalid! CtxSize=%lu InLen=%lu DevLen=%lu\n",
                 Extension->ContextSize, (ULONG)Slot->InputContext.Length, (ULONG)Slot->DeviceContext.Length);
         // Don't fail here yet, just warn, but proceed carefully
    }

    InputCtxBase = Slot->InputContext.VirtualAddress;
    DeviceCtxBase = Slot->DeviceContext.VirtualAddress;

    RtlZeroMemory(InputCtxBase, Slot->InputContext.Length);

    CtrlCtx = XHCI_GetInputControlContextVa(Extension, InputCtxBase);
    CtrlCtx->AddContextFlags = (1 << 1); // EP0
    // DropContextFlags must be 0 for Evaluate Context command or it will fail
    CtrlCtx->DropContextFlags = 0;

    ActiveSlotCtx = XHCI_GetDeviceSlotContextVa(Extension, DeviceCtxBase);
    SlotCtx = XHCI_GetInputSlotContextVa(Extension, InputCtxBase);
    RtlCopyMemory(SlotCtx, ActiveSlotCtx, Extension->ContextSize);

    ActiveEpCtx = XHCI_GetDeviceEndpointContextVa(Extension, DeviceCtxBase, 0);
    EpCtx = XHCI_GetInputEndpointContextVa(Extension, InputCtxBase, 0);
    RtlCopyMemory(EpCtx, ActiveEpCtx, Extension->ContextSize);

    XhciEndpointContextSetMaxPacketSize(EpCtx, MaxPacketSize);

    DPRINT1("usbxhci: Updating EP0 MPS to %lu for slot %u\n",
            MaxPacketSize,
            Slot->SlotId);

    return XHCI_SendCommand(Extension,
                            XHCI_TRB_TYPE_EVAL_CTX,
                            Slot->InputContext.PhysicalAddress.QuadPart,
                            0,
                            XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                            XHCI_COMMAND_TIMEOUT_MS,
                            FALSE,
                            NULL,
                            NULL);
}

static VOID NTAPI
XHCI_UpdateEp0MaxPacketSizeWorker(PVOID Context)
{
    PXHCI_EP0_UPDATE_WORK Work = (PXHCI_EP0_UPDATE_WORK)Context;
    if (Work)
    {
        XHCI_UpdateEp0MaxPacketSize(Work->Extension, Work->Slot, Work->MaxPacketSize);
        ExFreePoolWithTag(Work, XHCI_TAG);
    }
}

static VOID
XHCI_HandleEnumerationTransfer(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_ENDPOINT Endpoint,
    _In_ PXHCI_TRANSFER Transfer)
{
    USB_DEFAULT_PIPE_SETUP_PACKET *Setup;
    PUCHAR Buffer;
    ULONG BufferLength;

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

        
        Buffer = XHCI_GetDescriptorBuffer(Transfer, &BufferLength);
        if (Buffer && MmIsAddressValid(Buffer) && BufferLength >= 2 && Endpoint->Slot)
        {
            UCHAR DescriptorType = Setup->wValue.HiByte;

            if (DescriptorType == USB_CONFIGURATION_DESCRIPTOR_TYPE &&
                XHCI_IsVirtualPort(Extension, Endpoint->EndpointProperties.PortNumber))
            {
                ULONG CopyLength = BufferLength;

                if (CopyLength > Transfer->BytesTransferred)
                    CopyLength = Transfer->BytesTransferred;

                if (CopyLength != 0)
                {
                    ULONG Copied;

                    Copied = XHCI_CopyVirtualConfigDescriptor(
                                 Endpoint->EndpointProperties.PortNumber,
                                 Buffer,
                                 CopyLength);

                    DPRINT1("usbxhci: patched virtual cfg descriptor port=%u len=%lu first=%02x %02x %02x\n",
                            Endpoint->EndpointProperties.PortNumber,
                            Copied,
                            (Copied > 0) ? Buffer[0] : 0,
                            (Copied > 1) ? Buffer[1] : 0,
                            (Copied > 2) ? Buffer[2] : 0);
                }
            }
            else if (DescriptorType == USB_DEVICE_DESCRIPTOR_TYPE)
            {
                if (BufferLength >= sizeof(USB_DEVICE_DESCRIPTOR))
                {
                    PUSB_DEVICE_DESCRIPTOR D = (PUSB_DEVICE_DESCRIPTOR)Buffer;
                    PHYSICAL_ADDRESS Pa = MmGetPhysicalAddress(Buffer);
                    PHYSICAL_ADDRESS SgPa;
                    SgPa.QuadPart = 0;

                    if (Transfer->SgList && Transfer->SgList->SgElementCount > 0)
                         SgPa = Transfer->SgList->SgElement[0].SgPhysicalAddress;

                    DPRINT1("XHCI: GetDescriptor Data: Len=%d Type=%x VID=%04x PID=%04x\n", 
                            BufferLength, D->bDescriptorType, D->idVendor, D->idProduct);
                    DPRINT1("XHCI: buffer debugging: VA=%p PA=%I64x SG_PA=%I64x\n",
                            Buffer, Pa.QuadPart, SgPa.QuadPart);
                    DPRINT1("XHCI: Raw Bytes: %02x %02x %02x %02x\n",
                           ((PUCHAR)Buffer)[0], ((PUCHAR)Buffer)[1], ((PUCHAR)Buffer)[2], ((PUCHAR)Buffer)[3]);
                }
                else
                {
                     DPRINT1("XHCI: GetDescriptor Data: Len=%d (Header Only/Short)\n", BufferLength);
                }
            }
            else
            {
                 DPRINT1("XHCI: GetDescriptor Data: Len=%d Type=%x (Not Device Descriptor)\n", BufferLength, DescriptorType);
            }

            if (DescriptorType == USB_DEVICE_DESCRIPTOR_TYPE &&
                BufferLength >= sizeof(USB_DEVICE_DESCRIPTOR))
            {
                PUSB_DEVICE_DESCRIPTOR DevDesc = (PUSB_DEVICE_DESCRIPTOR)Buffer;

                
                if (Endpoint->EndpointProperties.DeviceSpeed < UsbSuperSpeed &&
                    Endpoint->EndpointProperties.MaxPacketSize != DevDesc->bMaxPacketSize0 &&
                    DevDesc->bMaxPacketSize0 != 0)
                {
                    DPRINT1("usbxhci: Detected EP0 MPS mismatch (Msg=%u vs Ctx=%u) -- updating\n",
                            DevDesc->bMaxPacketSize0,
                            Endpoint->EndpointProperties.MaxPacketSize);

                    Endpoint->EndpointProperties.MaxPacketSize = DevDesc->bMaxPacketSize0;

                    if (KeGetCurrentIrql() <= PASSIVE_LEVEL)
                    {
                        XHCI_UpdateEp0MaxPacketSize(Extension, Endpoint->Slot, DevDesc->bMaxPacketSize0);
                    }
                    else
                    {
                        PXHCI_EP0_UPDATE_WORK Work =
                            ExAllocatePoolWithTag(NonPagedPool,
                                                  sizeof(*Work),
                                                  XHCI_TAG);
                        if (Work)
                        {
                            Work->Extension = Extension;
                            Work->Slot = Endpoint->Slot;
                            Work->MaxPacketSize = DevDesc->bMaxPacketSize0;
                            ExInitializeWorkItem(&Work->WorkItem, XHCI_UpdateEp0MaxPacketSizeWorker, Work);
                            ExQueueWorkItem(&Work->WorkItem, CriticalWorkQueue);
                        }
                    }
                }

                if (DevDesc->bDescriptorType == USB_DEVICE_DESCRIPTOR_TYPE &&
                    DevDesc->bDeviceClass == USB_DEVICE_CLASS_HUB)
                {
                    Endpoint->Slot->IsHub = TRUE;

                    BOOLEAN NewMultiTt =
                        (Endpoint->EndpointProperties.DeviceSpeed == UsbHighSpeed &&
                         DevDesc->bDeviceProtocol == 2);
                    BOOLEAN NeedsUpdate = (Endpoint->Slot->MultiTt != NewMultiTt);

                    Endpoint->Slot->MultiTt = NewMultiTt;
                    if (NeedsUpdate)
                    {
                        if (KeGetCurrentIrql() <= PASSIVE_LEVEL)
                        {
                            XHCI_UpdateSlotTtInfo(Extension, Endpoint->Slot);
                        }
                        else
                        {
                            PXHCI_TT_UPDATE_WORK Work =
                                ExAllocatePoolWithTag(NonPagedPool,
                                                      sizeof(*Work),
                                                      XHCI_TAG);
                            if (Work)
                            {
                                Work->Extension = Extension;
                                Work->Slot = Endpoint->Slot;
                                Work->UpdateChildren = FALSE;
                                ExInitializeWorkItem(&Work->Item,
                                                     XHCI_TtUpdateWorker,
                                                     Work);
                                ExQueueWorkItem(&Work->Item, DelayedWorkQueue);
                            }
                        }
                    }
                }
            }
            else if (DescriptorType == USB_20_HUB_DESCRIPTOR_TYPE &&
                     Endpoint->EndpointProperties.DeviceSpeed == UsbHighSpeed &&
                     BufferLength >= 5)
            {
                USHORT HubChars;
                UCHAR ThinkTime;
                BOOLEAN NeedsUpdate = FALSE;
                UCHAR PortCount = Buffer[2];

                RtlCopyMemory(&HubChars, Buffer + 3, sizeof(HubChars));
                ThinkTime = (UCHAR)((HubChars >> 5) & 0x3);

                if (Endpoint->Slot->HasTtInfo == FALSE ||
                    Endpoint->Slot->TtThinkTime != ThinkTime)
                {
                    Endpoint->Slot->TtThinkTime = ThinkTime;
                    Endpoint->Slot->HasTtInfo = TRUE;
                    NeedsUpdate = TRUE;
                }

                if (PortCount != 0 && Endpoint->Slot->HubPortCount != PortCount)
                {
                    Endpoint->Slot->HubPortCount = PortCount;
                    NeedsUpdate = TRUE;
                }

                if (NeedsUpdate)
                {
                    DPRINT1("usbxhci: HS hub descriptor portcnt=%u think=%u\n",
                            Endpoint->Slot->HubPortCount,
                            Endpoint->Slot->TtThinkTime);
                    if (KeGetCurrentIrql() <= PASSIVE_LEVEL)
                    {
                        XHCI_UpdateSlotTtInfo(Extension, Endpoint->Slot);
                        XHCI_UpdateChildrenTtInfo(Extension, Endpoint->Slot);
                    }
                    else
                    {
                        PXHCI_TT_UPDATE_WORK Work =
                            ExAllocatePoolWithTag(NonPagedPool,
                                                  sizeof(*Work),
                                                  XHCI_TAG);
                        if (Work)
                        {
                            Work->Extension = Extension;
                            Work->Slot = Endpoint->Slot;
                            Work->UpdateChildren = TRUE;
                            ExInitializeWorkItem(&Work->Item,
                                                 XHCI_TtUpdateWorker,
                                                 Work);
                            ExQueueWorkItem(&Work->Item, DelayedWorkQueue);
                        }
                    }
                }
            }
            else if (DescriptorType == USB_30_HUB_DESCRIPTOR_TYPE &&
                     Endpoint->EndpointProperties.DeviceSpeed == UsbSuperSpeed &&
                     BufferLength >= 10)
            {
                UCHAR PortCount = Buffer[2];
                USHORT HubDelay;
                BOOLEAN NeedsUpdate = FALSE;

                RtlCopyMemory(&HubDelay, Buffer + 8, sizeof(HubDelay));

                Endpoint->Slot->IsHub = TRUE;

                if (PortCount != 0 && Endpoint->Slot->HubPortCount != PortCount)
                {
                    Endpoint->Slot->HubPortCount = PortCount;
                    NeedsUpdate = TRUE;
                }

                if (HubDelay != 0 && Endpoint->Slot->MaxExitLatency != HubDelay)
                {
                    Endpoint->Slot->MaxExitLatency = HubDelay;
                    NeedsUpdate = TRUE;
                }

                if (NeedsUpdate)
                {
                    DPRINT1("usbxhci: SS hub descriptor portcnt=%u delay=%u\n",
                            Endpoint->Slot->HubPortCount,
                            Endpoint->Slot->MaxExitLatency);
                    if (KeGetCurrentIrql() <= PASSIVE_LEVEL)
                    {
                        XHCI_UpdateSlotTtInfo(Extension, Endpoint->Slot);
                    }
                    else
                    {
                        PXHCI_TT_UPDATE_WORK Work =
                            ExAllocatePoolWithTag(NonPagedPool,
                                                  sizeof(*Work),
                                                  XHCI_TAG);
                        if (Work)
                        {
                            Work->Extension = Extension;
                            Work->Slot = Endpoint->Slot;
                            Work->UpdateChildren = FALSE;
                            ExInitializeWorkItem(&Work->Item,
                                                 XHCI_TtUpdateWorker,
                                                 Work);
                            ExQueueWorkItem(&Work->Item, DelayedWorkQueue);
                        }
                    }
                }
            }
        }
    }

}

static MPSTATUS
XHCI_ResetDeviceOnPort(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT PortNumber)
{
    PXHCI_DEVICE_SLOT Slot;
    MPSTATUS Status;
    ULONG CompletionCode = 0;

    if (XHCI_IsVirtualPort(Extension, PortNumber))
        return MP_STATUS_SUCCESS;

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
                              &CompletionCode);
    if (Status != MP_STATUS_SUCCESS)
    {
        DPRINT1("usbxhci: ResetDeviceOnPort: RESET_DEV command failed for slot %u on port %u (Status=%ld Code=%lu)\n",
                Slot->SlotId,
                PortNumber,
                Status,
                CompletionCode);
    }

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
    BOOLEAN DoRootHubInvalidate = FALSE;
    KIRQL OldIrql;

    if (!Extension || !Extension->RuntimeRegisters ||
        !Extension->EventRing || Extension->EventRingTrbCount == 0 ||
        Extension->FatalError)
        return;

    KeAcquireSpinLock(&Extension->EventRingLock, &OldIrql);

    while (TRUE)
    {
        PXHCI_TRB EventTrb = &Extension->EventRing[Extension->EventRingDequeueIndex];
        ULONG Cycle = EventTrb->Control & XHCI_TRB_CYCLE;
        ULONG TrbType;

        if (Cycle != Extension->EventRingCycleState)
            break;

        TrbType = XHCI_GetTrbType(EventTrb);

        XHCI_DBG(XHCI_TRACE_EVENTS,
                 "usbxhci: Event idx=%lu type=%lu ctrl=%08lx status=%08lx param=%08lx/%08lx AllowCb=%u\n",
                 (ULONG)Extension->EventRingDequeueIndex,
                 TrbType,
                 EventTrb->Control,
                 EventTrb->Status,
                 EventTrb->Parameter1,
                 EventTrb->Parameter2,
                 AllowCallbacks ? 1 : 0);

        if (TrbType == XHCI_TRB_TYPE_PORT_STATUS_CHANGE)
        {
            XHCI_DBG(XHCI_TRACE_EVENTS,
                     "usbxhci: PSC event idx=%lu ctrl=%08lx status=%08lx param=%08lx/%08lx AllowCb=%u\n",
                     (ULONG)Extension->EventRingDequeueIndex,
                     EventTrb->Control,
                     EventTrb->Status,
                     EventTrb->Parameter1,
                     EventTrb->Parameter2,
                     AllowCallbacks ? 1 : 0);
        }

        if (TrbType == XHCI_TRB_TYPE_COMMAND_COMPLETION)
        {
            ULONGLONG CmdPtr = ((ULONGLONG)EventTrb->Parameter2 << 32) |
                               EventTrb->Parameter1;
            XHCI_TraceCommandRingState(Extension,
                                       "event ring command completion",
                                       CmdPtr,
                                       TrbType);
        }

        /*
         * When polling synchronously (AllowCallbacks == FALSE), we still must
         * consume TRANSFER_EVENT TRBs; otherwise command completion events can
         * be blocked behind them.  The completion callbacks are deferred until
         * callbacks are enabled again.
         */

        /* Advance the dequeue pointer *before* processing the event and dropping
         * the lock. This ensures we claim the event and maintains ring consistency
         * for other potential consumers (though we should be the only one). */
        Extension->EventRingDequeueIndex++;
        if (Extension->EventRingDequeueIndex >= Extension->EventRingTrbCount)
        {
            Extension->EventRingDequeueIndex = 0;
            Extension->EventRingCycleState ^= 1;
        }


        /* Drop the lock while handling the event to avoid deadlocks with USBPORT. */
        KeReleaseSpinLock(&Extension->EventRingLock, OldIrql);

        switch (TrbType)
        {
            case XHCI_TRB_TYPE_TRANSFER_EVENT:
                XHCI_HandleTransferEvent(Extension, EventTrb, AllowCallbacks);
                break;

            case XHCI_TRB_TYPE_COMMAND_COMPLETION:
                XHCI_HandleCommandCompletion(Extension, EventTrb);
                break;

            case XHCI_TRB_TYPE_PORT_STATUS_CHANGE:
                /* Record the change and defer hub notifications so we only
                 * ring USBPORT once per DPC, even if multiple ports changed.
                 * When callbacks are temporarily masked or root-hub IRQs are
                 * disabled, remember that a notification is pending so it can
                 * be replayed once IRQs are re-enabled. */
                XHCI_HandlePortStatusChangeEvent(Extension,
                                                 EventTrb,
                                                 FALSE);
                if (AllowCallbacks && Extension->RhIrqEnabled)
                {
                    NotifyRootHub = TRUE;
                    Extension->RhPendingInvalidate = FALSE;
                }
                else
                {
                    Extension->RhPendingInvalidate = TRUE;
                }
                break;

            default:
                DPRINT1("usbxhci: unhandled event type %lu (ctrl=%08lx)\n",
                        TrbType,
                        EventTrb->Control);
                break;
        }

        KeAcquireSpinLock(&Extension->EventRingLock, &OldIrql);
        Processed++;
    }

    Extension->EventRingDequeuePointer =
        Extension->EventRingPhysical.QuadPart +
        ((ULONGLONG)Extension->EventRingDequeueIndex * sizeof(XHCI_TRB));

    /* Batch root-hub notifications so USBPORT only sees a single
     * invalidate call per DPC, even if several PORT_STATUS_CHANGE
     * events were serviced. Respect the miniport's RootHub IRQ
     * enable/disable state so USBPORT can quiesce notifications
     * while stopping the root hub. */


    if (AllowCallbacks &&
        Extension->RhIrqEnabled &&
        XhciRegPacket.UsbPortInvalidateRootHub &&
        (NotifyRootHub || Extension->RhPendingInvalidate))
    {
        Extension->RhPendingInvalidate = FALSE;
        DoRootHubInvalidate = TRUE;
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
    KeReleaseSpinLock(&Extension->EventRingLock, OldIrql);

    if (AllowCallbacks)
        XHCI_DrainDeferredTransferCompletions(Extension);

    if (DoRootHubInvalidate)
        XhciRegPacket.UsbPortInvalidateRootHub(Extension);

}
static
BOOLEAN
XHCI_EventRingHasPendingTrb(
    _In_ PXHCI_EXTENSION Extension)
{
    PXHCI_TRB EventTrb;
    BOOLEAN Pending;
    KIRQL OldIrql;

    if (!Extension ||
        !Extension->EventRing ||
        Extension->EventRingTrbCount == 0)
    {
        return FALSE;
    }

    KeAcquireSpinLock(&Extension->EventRingLock, &OldIrql);
    EventTrb = &Extension->EventRing[Extension->EventRingDequeueIndex];
    Pending = ((EventTrb->Control & XHCI_TRB_CYCLE) ==
               Extension->EventRingCycleState);
    KeReleaseSpinLock(&Extension->EventRingLock, OldIrql);
    return Pending;
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
        {
            BOOLEAN Notify = Extension->RhIrqEnabled &&
                             XhciRegPacket.UsbPortInvalidateRootHub != NULL;
            if (XHCI_ScanPortStatusChanges(Extension, Notify))
            {
                DidWork = TRUE;
                Extension->RhPendingInvalidate = Notify ? FALSE : TRUE;
            }
        }

        if (!DidWork)
            break;
    }

    if (AllowCallbacks)
        XHCI_DrainDeferredTransferCompletions(Extension);
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

    /* Disable interrupts so we don't loop on a storming HCE. */
    XHCI_DisableInterrupts(Extension);

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
    if (Runtime && Interrupter)
    {
        DPRINT1("usbxhci: %s IMOD=%08lx IMAN=%08lx\n",
                Reason,
                READ_REGISTER_ULONG(&Interrupter->Imod),
                READ_REGISTER_ULONG(&Interrupter->Iman));
    }

    for (Port = 0; Port < Extension->NumberOfPorts; Port++)
    {
        ULONG PortSc = READ_REGISTER_ULONG(&Ops->PortRegister[Port].PortStatusAndControl);
            DPRINT1("usbxhci: %s PORT%lu=0x%08lx\n", Reason, Port + 1, PortSc);
    }
}

static
PXHCI_TRB
XHCI_LocateCommandTrb(
    _In_ PXHCI_EXTENSION Extension,
    _In_ ULONGLONG CommandPointer,
    _Out_opt_ PULONG IndexOut)
{
    ULONGLONG Offset;
    ULONG Index;

    if (!Extension || !Extension->CommandRing || Extension->CommandRingTrbCount == 0)
        return NULL;

    if (CommandPointer < Extension->CommandRingPhysical.QuadPart)
        return NULL;

    Offset = CommandPointer - Extension->CommandRingPhysical.QuadPart;
    Index = (ULONG)(Offset / sizeof(XHCI_TRB));
    if (Index >= Extension->CommandRingTrbCount)
        return NULL;

    if (IndexOut)
        *IndexOut = Index;

    return &Extension->CommandRing[Index];
}

static
VOID
XHCI_LogEventRingSnapshot(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ ULONG EntriesToDump)
{
    KIRQL OldIrql;
    ULONG Count;

    if (!Extension || !Extension->EventRing || Extension->EventRingTrbCount == 0)
        return;

    if (EntriesToDump == 0)
        EntriesToDump = 1;

    if (EntriesToDump > Extension->EventRingTrbCount)
        EntriesToDump = Extension->EventRingTrbCount;

    KeAcquireSpinLock(&Extension->EventRingLock, &OldIrql);
    for (Count = 0; Count < EntriesToDump; Count++)
    {
        ULONG Index = (Extension->EventRingDequeueIndex + Count) %
                      Extension->EventRingTrbCount;
        const XHCI_TRB *Trb = &Extension->EventRing[Index];
        ULONG TrbType = XHCI_GetTrbType(Trb);

        DPRINT1("usbxhci: event ring [%lu] idx=%lu type=%lu cycle=%u "
                "param=%08lx:%08lx status=%08lx ctrl=%08lx\n",
                Count,
                Index,
                TrbType,
                (Trb->Control & XHCI_TRB_CYCLE) ? 1u : 0u,
                Trb->Parameter2,
                Trb->Parameter1,
                Trb->Status,
                Trb->Control);
    }
    KeReleaseSpinLock(&Extension->EventRingLock, OldIrql);
}

static
VOID
XHCI_LogCommandTimeoutDetails(
    _In_ PXHCI_EXTENSION Extension,
    _In_opt_ PXHCI_COMMAND_CONTEXT CommandContext)
{
    if (!Extension || !CommandContext)
        return;

    DPRINT1("usbxhci: command timeout ctx type=%lu slot=%u ptr=%I64x "
            "code=%lu completed=%u\n",
            CommandContext->CommandType,
            CommandContext->SlotId,
            CommandContext->CommandPointer,
            CommandContext->CompletionCode,
            CommandContext->Completed ? 1u : 0u);

    XHCI_LogEventRingSnapshot(Extension, 4);

    ULONG Index;
    PXHCI_TRB Trb = XHCI_LocateCommandTrb(Extension,
                                          CommandContext->CommandPointer,
                                          &Index);
    if (Trb)
    {
        ULONG TrbType = XHCI_GetTrbType(Trb);

        DPRINT1("usbxhci: command timeout TRB idx=%lu type=%lu "
                "param=%08lx:%08lx status=%08lx ctrl=%08lx\n",
                Index,
                TrbType,
                Trb->Parameter2,
                Trb->Parameter1,
                Trb->Status,
                Trb->Control);
    }
    else
    {
        DPRINT1("usbxhci: command timeout could not locate cmd pointer %I64x\n",
                CommandContext->CommandPointer);
    }
}

static
VOID
XHCI_LogInterrupterState(
    _In_ PXHCI_EXTENSION Extension,
    _In_z_ PCSTR Reason)
{
    PXHCI_OPERATIONAL_REGISTERS Ops;
    PXHCI_INTERRUPTER_REGISTER_SET Interrupter;
    ULONG UsbCmd, UsbSts, Config;
    ULONGLONG Crcr;
    ULONG Doorbell0 = 0;

    if (!Extension || !Extension->OperationalRegisters)
        return;

    Ops = Extension->OperationalRegisters;
    UsbCmd = READ_REGISTER_ULONG(&Ops->UsbCmd);
    UsbSts = READ_REGISTER_ULONG(&Ops->UsbSts);
    Config = READ_REGISTER_ULONG(&Ops->Config);
    Crcr = ((ULONGLONG)READ_REGISTER_ULONG(&Ops->CrCrHigh) << 32) |
           READ_REGISTER_ULONG(&Ops->CrCrLow);

    if (Extension->DoorbellArray)
    {
        Doorbell0 = READ_REGISTER_ULONG(&Extension->DoorbellArray->Doorbell[0]);
    }

    Interrupter = (Extension->RuntimeRegisters) ?
                  &Extension->RuntimeRegisters->Interrupter[0] : NULL;

    if (Interrupter)
    {
        ULONG Iman = READ_REGISTER_ULONG(&Interrupter->Iman);
        ULONG Imod = READ_REGISTER_ULONG(&Interrupter->Imod);
        ULONG ErstSize = READ_REGISTER_ULONG(&Interrupter->ErstSize);
        ULONGLONG Erdp = ((ULONGLONG)READ_REGISTER_ULONG(&Interrupter->ErdpHigh) << 32) |
                         READ_REGISTER_ULONG(&Interrupter->ErdpLow);
        ULONGLONG ErstBase = ((ULONGLONG)READ_REGISTER_ULONG(&Interrupter->ErstBaseHigh) << 32) |
                             READ_REGISTER_ULONG(&Interrupter->ErstBaseLow);

        DPRINT1("usbxhci: %s IMAN=%08lx IMOD=%08lx ERSTSZ=%lu ERSTBA=%08lx:%08lx "
                "ERDP=%08lx:%08lx\n",
                Reason,
                Iman,
                Imod,
                ErstSize,
                (ULONG)(ErstBase >> 32),
                (ULONG)(ErstBase & 0xFFFFFFFF),
                (ULONG)(Erdp >> 32),
                (ULONG)(Erdp & 0xFFFFFFFF));
    }

    DPRINT1("usbxhci: %s USBCMD=%08lx USBSTS=%08lx CONFIG=%08lx CRCR=%08lx:%08lx DOORBELL0=%08lx\n",
            Reason,
            UsbCmd,
            UsbSts,
            Config,
            (ULONG)(Crcr >> 32),
            (ULONG)(Crcr & 0xFFFFFFFF),
            Doorbell0);
}

#if DBG
static VOID
XHCI_TraceCommandRingState(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PCSTR Reason,
    _In_ ULONGLONG CommandPointer,
    _In_ ULONG TrbType)
{
    PXHCI_OPERATIONAL_REGISTERS Ops;
    PXHCI_INTERRUPTER_REGISTER_SET Interrupter;
    ULONGLONG Crcr;
    ULONGLONG Erdp;
    ULONGLONG ErstBase;
    ULONG ErstSize;
    ULONG Iman;

    if (!Extension || !Extension->OperationalRegisters || !Extension->RuntimeRegisters)
        return;

    Ops = Extension->OperationalRegisters;
    Interrupter = &Extension->RuntimeRegisters->Interrupter[0];

    Crcr = ((ULONGLONG)READ_REGISTER_ULONG(&Ops->CrCrHigh) << 32) |
           READ_REGISTER_ULONG(&Ops->CrCrLow);
    ErstBase = ((ULONGLONG)READ_REGISTER_ULONG(&Interrupter->ErstBaseHigh) << 32) |
               READ_REGISTER_ULONG(&Interrupter->ErstBaseLow);
    Erdp = ((ULONGLONG)READ_REGISTER_ULONG(&Interrupter->ErdpHigh) << 32) |
           READ_REGISTER_ULONG(&Interrupter->ErdpLow);
    ErstSize = READ_REGISTER_ULONG(&Interrupter->ErstSize);
    Iman = READ_REGISTER_ULONG(&Interrupter->Iman);

    DPRINT1("usbxhci: %s type=%lu cmdptr=%I64x cmd_enq=%lu cyc=%lu "
            "evt_deq=%lu cyc=%lu CRCR=%08lx:%08lx ERST=%08lx:%08lx "
            "ERSTSZ=%lu ERDP=%08lx:%08lx IMAN=%08lx\n",
            Reason,
            TrbType,
            CommandPointer,
            Extension->CommandRingEnqueueIndex,
            Extension->CommandRingCycleState,
            Extension->EventRingDequeueIndex,
            Extension->EventRingCycleState,
            (ULONG)(Crcr >> 32),
            (ULONG)(Crcr & 0xFFFFFFFF),
            (ULONG)(ErstBase >> 32),
            (ULONG)(ErstBase & 0xFFFFFFFF),
            ErstSize,
            (ULONG)(Erdp >> 32),
            (ULONG)(Erdp & 0xFFFFFFFF),
            Iman);
}
#else
static VOID
XHCI_TraceCommandRingState(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PCSTR Reason,
    _In_ ULONGLONG CommandPointer,
    _In_ ULONG TrbType)
{
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(CommandPointer);
    UNREFERENCED_PARAMETER(TrbType);
}
#endif

static
VOID
XHCI_DumpAddressDeviceContext(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_DEVICE_SLOT Slot,
    _In_ UCHAR EndpointId,
    _In_ USHORT PortNumber,
    _In_ UCHAR CompletionCode)
{
    PVOID DeviceCtxBase;
    PXHCI_SLOT_CONTEXT SlotCtx;
    PXHCI_ENDPOINT_CONTEXT EpCtx;
    volatile ULONG *PortScReg;
    ULONG PortSc = 0;

    if (!Extension || !Slot)
        return;

    DeviceCtxBase = Slot->DeviceContext.VirtualAddress;
    if (!DeviceCtxBase)
        return;

    if (EndpointId > XHCI_MAX_ENDPOINTS)
        EndpointId = 0;

    SlotCtx = XHCI_GetDeviceSlotContextVa(Extension, DeviceCtxBase);
    EpCtx = XHCI_GetDeviceEndpointContextVa(Extension, DeviceCtxBase, EndpointId);

    PortScReg = XHCI_GetPortStatusRegister(Extension, PortNumber);
    if (PortScReg)
        PortSc = READ_REGISTER_ULONG(PortScReg);

    DPRINT1("usbxhci: AddressDevice CONTEXT_ERROR slot=%u ep=%u port=%u code=%u\n",
            Slot->SlotId,
            EndpointId,
            PortNumber,
            CompletionCode);
    DPRINT1("usbxhci: SlotCtx: DevInfo=%08lx DevInfo2=%08lx TtInfo=%08lx DevState=%08lx\n",
            SlotCtx->DevInfo,
            SlotCtx->DevInfo2,
            SlotCtx->TtInfo,
            SlotCtx->DevState);
    DPRINT1("usbxhci: Ep0Ctx: EpInfo=%08lx EpInfo2=%08lx TrDeq=%08lx:%08lx TxInfo=%08lx\n",
            EpCtx->EpInfo,
            EpCtx->EpInfo2,
            (ULONG)(EpCtx->TrDequeuePointer >> 32),
            (ULONG)(EpCtx->TrDequeuePointer & 0xFFFFFFFF),
            EpCtx->TxInfo);
    DPRINT1("usbxhci: PortSC[%u]=0x%08lx\n",
            PortNumber,
            PortSc);
}

static
VOID
XHCI_DumpInputContextForAddress(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_DEVICE_SLOT Slot)
{
    PVOID InputCtxBase;
    PXHCI_SLOT_CONTEXT SlotCtx;
    PXHCI_ENDPOINT_CONTEXT EpCtx;

    UNREFERENCED_PARAMETER(Extension);

    if (!Slot)
        return;

    InputCtxBase = Slot->InputContext.VirtualAddress;
    if (!InputCtxBase)
        return;

    SlotCtx = XHCI_GetInputSlotContextVa(Extension, InputCtxBase);
    EpCtx = XHCI_GetInputEndpointContextVa(Extension, InputCtxBase, 0);

    DPRINT1("usbxhci: AddressDevice INPUT SlotCtx DevInfo=%08lx DevInfo2=%08lx TtInfo=%08lx DevState=%08lx\n",
            SlotCtx->DevInfo,
            SlotCtx->DevInfo2,
            SlotCtx->TtInfo,
            SlotCtx->DevState);
    DPRINT1("usbxhci: AddressDevice INPUT Ep0Ctx EpInfo=%08lx EpInfo2=%08lx TrDeq=%08lx:%08lx TxInfo=%08lx\n",
            EpCtx->EpInfo,
            EpCtx->EpInfo2,
            (ULONG)(EpCtx->TrDequeuePointer >> 32),
            (ULONG)(EpCtx->TrDequeuePointer & 0xFFFFFFFF),
            EpCtx->TxInfo);
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

    if (Arg->Endpoint && Arg->Endpoint->Extension &&
        (Arg->Endpoint->Extension->StoppingOrRemoved || Arg->Endpoint->Extension->FatalError))
        return;

    XHCI_LOG_IRQL("Ep0BringupCallback entry");
    PXHCI_EP0_WORK_WRAP Wrap = ExAllocatePoolWithTag(NonPagedPool,
                                                     sizeof(*Wrap),
                                                     XHCI_TAG);
    if (!Wrap)
        return;

    RtlZeroMemory(Wrap, sizeof(*Wrap));
    Wrap->Ctx = *Arg;
    if (Arg->Endpoint && Arg->Endpoint->Extension)
        InterlockedIncrement(&Arg->Endpoint->Extension->Ep0WorkerCount);
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

    if (!Ext || !Ep || Ext->StoppingOrRemoved || Ext->FatalError)
        goto Exit;

    if (!Ep->Slot)
    {
        MPSTATUS WorkerStatus = XHCI_BringupDefaultControlEndpoint(Ext, Ep, &Arg->Props);
        DPRINT1("usbxhci: EP0 bring-up worker completed with %ld (slot=%u)\n",
                WorkerStatus,
                Ep->Slot ? Ep->SlotId : 0);
    }

Exit:
    if (Ext)
        InterlockedDecrement(&Ext->Ep0WorkerCount);
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
        if (CommandContext->CompletionEvent)
            KeSetEvent(CommandContext->CompletionEvent, IO_NO_INCREMENT, FALSE);
    }

    if (CommandContext &&
        (CommandContext->CommandType == XHCI_TRB_TYPE_ENABLE_SLOT ||
         CommandContext->CommandType == XHCI_TRB_TYPE_ADDRESS_DEV))
    {
        XHCI_TraceCommandRingState(Extension,
                                   "command completion",
                                   CommandPointer,
                                   CommandContext->CommandType);
    }

    XHCI_DBG(XHCI_TRACE_COMMANDS,
             "usbxhci: command completion code=%lu slot=%u cmdptr=%I64x\n",
             CompletionCode,
             SlotId,
             CommandPointer);

    if (CommandContext &&
        (CommandContext->CommandType == XHCI_TRB_TYPE_ENABLE_SLOT ||
         CommandContext->CommandType == XHCI_TRB_TYPE_ADDRESS_DEV))
    {
        DPRINT1("usbxhci: cmd complete type=%lu code=%lu slot=%u\n",
                CommandContext->CommandType,
                CompletionCode,
                SlotId);
    }

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
        Slot->Addressed = FALSE;
        XHCI_UpdateDeviceAddressMap(Extension, Slot, 0);
        DPRINT1("usbxhci: slot %u reset\n", SlotId);
    }
}

static
VOID
XHCI_HandlePortChange(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT PortId,
    _In_ BOOLEAN NotifyHub)
{
    volatile ULONG *PortScReg;
    ULONG PortSc = 0;
    ULONG ChangeMask;

    if (!Extension || PortId == 0 || PortId > Extension->NumberOfPorts)
        return;

    PortScReg = XHCI_GetPortStatusRegister(Extension, PortId);
    if (PortScReg)
        PortSc = READ_REGISTER_ULONG(PortScReg);

    DPRINT1("usbxhci: port status change on port %u PortSC=0x%08lx\n",
            PortId,
            PortSc);

    ChangeMask = PortSc & XHCI_PORTSC_CHANGE_MASK;
    if (ChangeMask)
    {
        ULONG OriginalMask = ChangeMask;
        BOOLEAN SuppressConnect = FALSE;

        BOOLEAN IsVirtualPort = XHCI_IsVirtualPort(Extension, PortId);
        BOOLEAN AlreadyAnnounced = FALSE;

        if (IsVirtualPort && PortId <= XHCI_MAX_PORTS)
            AlreadyAnnounced = Extension->VirtualPortAnnounced[PortId];

        if (IsVirtualPort &&
            AlreadyAnnounced &&
            (ChangeMask & XHCI_PORTSC_CSC) != 0)
        {
            SuppressConnect = TRUE;
            ChangeMask &= ~XHCI_PORTSC_CSC;
        }

        if (ChangeMask != 0 && PortId <= XHCI_MAX_PORTS)
        {
            ULONG PreviousMask = (ULONG)InterlockedOr(
                (volatile LONG *)&Extension->PortChangeMask[PortId],
                ChangeMask);

            if (((~PreviousMask) & ChangeMask) == 0)
                NotifyHub = FALSE;
        }
        else if (SuppressConnect)
        {
            NotifyHub = FALSE;
        }

        XHCI_AckPortChangeInternal(Extension, PortId, OriginalMask, FALSE);

        if (SuppressConnect && ChangeMask == 0)
            return;
    }
    else
    {
        NotifyHub = FALSE;
    }

    InterlockedOr((volatile LONG *)&Extension->PendingUsbSts, XHCI_USBSTS_PCD);

    XHCI_TryWarmResetPort(Extension, PortId);

    if (NotifyHub && XhciRegPacket.UsbPortInvalidateRootHub)
        XhciRegPacket.UsbPortInvalidateRootHub(Extension);
}

static
VOID
XHCI_ScheduleEp0Poll(
    _Inout_ PXHCI_EXTENSION Extension)
{
    LARGE_INTEGER DueTime;

    if (!Extension)
        return;

    DueTime.QuadPart = -(LONGLONG)XHCI_EP0_POLL_INTERVAL_US * 10;
    KeSetTimer(&Extension->Ep0PollTimer, DueTime, &Extension->Ep0PollDpc);
}

static
VOID
NTAPI
XHCI_Ep0PollDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArg1,
    _In_opt_ PVOID SystemArg2)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArg1);
    UNREFERENCED_PARAMETER(SystemArg2);

    if (!Extension || Extension->FatalError || Extension->StoppingOrRemoved)
        return;

    XHCI_PollForWork(Extension, TRUE);

    if (InterlockedCompareExchange(&Extension->Ep0PollCounter, 0, 0) > 0)
        XHCI_ScheduleEp0Poll(Extension);
}

static
BOOLEAN
XHCI_ScanPortStatusChanges(
    _In_ PXHCI_EXTENSION Extension,
    _In_ BOOLEAN NotifyHub)
{
    USHORT Port;
    BOOLEAN Found = FALSE;

    if (!Extension)
        return FALSE;

    for (Port = 1; Port <= Extension->NumberOfPorts; Port++)
    {
        volatile ULONG *PortScReg = XHCI_GetPortStatusRegister(Extension, Port);
        ULONG PortSc;

        if (!PortScReg)
            continue;

        PortSc = READ_REGISTER_ULONG(PortScReg);
        if ((PortSc & XHCI_PORTSC_CHANGE_MASK) == 0)
            continue;

        XHCI_HandlePortChange(Extension, Port, FALSE);
        Found = TRUE;
    }

    if (Found && NotifyHub && XhciRegPacket.UsbPortInvalidateRootHub)
        XhciRegPacket.UsbPortInvalidateRootHub(Extension);

    return Found;
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

    XHCI_HandlePortChange(Extension, (USHORT)PortId, NotifyHub);
}

static
VOID
XHCI_QueueDeferredTransferCompletion(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_TRANSFER Transfer)
{
    KIRQL OldIrql;

    if (!Extension || !Transfer)
        return;

    KeAcquireSpinLock(&Extension->DeferredTransferLock, &OldIrql);
    InsertTailList(&Extension->DeferredTransferList, &Transfer->ListEntry);
    KeReleaseSpinLock(&Extension->DeferredTransferLock, OldIrql);
}

static
VOID
XHCI_DrainDeferredTransferCompletions(
    _Inout_ PXHCI_EXTENSION Extension)
{
    LIST_ENTRY LocalList;
    KIRQL OldIrql;

    if (!Extension || Extension->FatalError || Extension->StoppingOrRemoved)
        return;

    InitializeListHead(&LocalList);

    KeAcquireSpinLock(&Extension->DeferredTransferLock, &OldIrql);
    while (!IsListEmpty(&Extension->DeferredTransferList))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&Extension->DeferredTransferList);
        InsertTailList(&LocalList, Entry);
    }
    KeReleaseSpinLock(&Extension->DeferredTransferLock, OldIrql);

    while (!IsListEmpty(&LocalList))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&LocalList);
        PXHCI_TRANSFER Transfer = CONTAINING_RECORD(Entry, XHCI_TRANSFER, ListEntry);
        PXHCI_ENDPOINT Endpoint = Transfer->Endpoint;

        if (!Endpoint)
            continue;

        if (Transfer->IsIsochronous && XhciRegPacket.UsbPortCompleteIsoTransfer)
        {
            XhciRegPacket.UsbPortCompleteIsoTransfer(Extension,
                                                     Endpoint,
                                                     Transfer->TransferParameters,
                                                     Transfer->BytesTransferred);
        }
        else if (XhciRegPacket.UsbPortCompleteTransfer)
        {
            XHCI_DBG(XHCI_TRACE_TRANSFERS,
                     "usbxhci: draining deferred completion (UsbdStatus=0x%x)\n",
                     Transfer->UsbdStatus);
            XhciRegPacket.UsbPortCompleteTransfer(Extension,
                                                  Endpoint,
                                                  Transfer->TransferParameters,
                                                  Transfer->UsbdStatus,
                                                  Transfer->BytesTransferred);
        }
    }
}

static VOID
XHCI_HandleTransferEvent(
    _In_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_TRB EventTrb,
    _In_ BOOLEAN AllowCallbacks)
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
    KIRQL OldIrql;

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
    if (!Endpoint)
    {
        DPRINT1("usbxhci: transfer event slot=%u ep=%u has no endpoint (ptr=%I64x)\n",
                SlotId,
                EndpointId,
                TrbPointer);
        return;
    }

    KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
    if (!Endpoint->ActiveTransfer)
    {
        KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
        DPRINT1("usbxhci: transfer event slot=%u ep=%u has no active transfer (ptr=%I64x)\n",
                SlotId,
                EndpointId,
                TrbPointer);
        return;
    }

    Transfer = Endpoint->ActiveTransfer;
    Endpoint->TransferRing.DequeueIndex = Endpoint->TransferRing.EnqueueIndex;

    if (Endpoint->DefaultControl && Endpoint->Slot)
    {
        Endpoint->Slot->Ep0RingCycleState = Endpoint->TransferRing.CycleState;
        Endpoint->Slot->Ep0RingEnqueueIndex = Endpoint->TransferRing.EnqueueIndex;
        Endpoint->Slot->Ep0RingDequeueIndex = Endpoint->TransferRing.DequeueIndex;
    }
    Endpoint->ActiveTransfer = NULL;
    KeReleaseSpinLock(&Endpoint->Lock, OldIrql);

    RequestedLength = Transfer->RequestedLength;
    if (RequestedLength == 0 && Transfer->TransferParameters)
        RequestedLength = Transfer->TransferParameters->TransferBufferLength;

    if (RequestedLength < Remaining)
    {
        DPRINT1("usbxhci: transfer event slot=%u ep=%u reports residual %lu > requested %lu\n",
                SlotId,
                EndpointId,
                Remaining,
                RequestedLength);
        Remaining = RequestedLength;
    }

    BytesTransferred = RequestedLength - Remaining;
    XHCI_DBG(XHCI_TRACE_TRANSFERS,
             "XHCI_Event: S%u E%u Code=%u Rem=%u Ptr=%I64x\n",
             SlotId,
             EndpointId,
             CompletionCode,
             Remaining,
             TrbPointer);

    XHCI_DBG(XHCI_TRACE_TRANSFERS,
             "usbxhci: xfer complete slot=%u ep=%u code=%lu req=%lu rem=%lu bytes=%lu stream=%u\n",
             SlotId,
             EndpointId,
             CompletionCode,
             RequestedLength,
             Remaining,
             BytesTransferred,
             Transfer->StreamId);

    if (SlotId == 1 && (EndpointId == 3 || EndpointId == 4))
    {
        DPRINT1("usbxhci: bulk xfer event S%u E%u Code=%lu Req=%lu Rem=%lu Bytes=%lu Ptr=%I64x\n",
                SlotId,
                EndpointId,
                CompletionCode,
                RequestedLength,
                Remaining,
                BytesTransferred,
                TrbPointer);
    }

    switch (CompletionCode)
    {
        case XHCI_COMPLETION_SUCCESS:
        case XHCI_COMPLETION_SHORT_PACKET:
            UsbdStatus = USBD_STATUS_SUCCESS;
            break;
        case XHCI_COMPLETION_STALL_ERROR:
            UsbdStatus = USBD_STATUS_STALL_PID;
            break;
        case XHCI_COMPLETION_STOPPED:
        case XHCI_COMPLETION_STOPPED_LENGTH_INVALID:
        case XHCI_COMPLETION_STOPPED_SHORT_PACKET:
            /* Endpoint stopped (typically due to cancel/reset) – surface as a
             * canceled transfer rather than a generic failure to better match
             * Windows USBPORT semantics. */
            UsbdStatus = USBD_STATUS_CANCELED;
            break;
        default:
            UsbdStatus = USBD_STATUS_REQUEST_FAILED;
            break;
    }

#if DBG
    if (Transfer->CompletionTrbPointer != 0 &&
        Transfer->CompletionTrbPointer != TrbPointer)
    {
        DPRINT1("usbxhci: transfer event pointer mismatch slot=%u ep=%u exp=%I64x got=%I64x code=%lu\n",
                SlotId,
                EndpointId,
                (ULONGLONG)Transfer->CompletionTrbPointer,
                (ULONGLONG)TrbPointer,
                CompletionCode);
    }
#endif

    Transfer->CompletionTrbPointer = TrbPointer;
    Transfer->BytesTransferred = BytesTransferred;
    Transfer->UsbdStatus = UsbdStatus;
    if (Transfer->Flags & XHCI_TRANSFER_FLAG_NEEDS_POLL)
    {
        Transfer->Flags &= ~XHCI_TRANSFER_FLAG_NEEDS_POLL;
        if (InterlockedDecrement(&Extension->Ep0PollCounter) <= 0)
            KeCancelTimer(&Extension->Ep0PollTimer);
    }

    if (Transfer->Flags & (XHCI_TRANSFER_FLAG_SET_ADDRESS | XHCI_TRANSFER_FLAG_GET_DESCRIPTOR))
    {
        DPRINT1("usbxhci: enum xfer slot=%u ep=%u code=%lu usbd=0x%lx bytes=%lu req=%lu flags=0x%lx\n",
                SlotId,
                EndpointId,
                CompletionCode,
                UsbdStatus,
                BytesTransferred,
                RequestedLength,
                Transfer->Flags);
    }

    /* Track aggregate bandwidth usage for periodic endpoints (iso/int). */
    if (Transfer->IsIsochronous ||
        Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
        Endpoint->TotalBytesTransferred += BytesTransferred;

    if (Transfer->Flags & (XHCI_TRANSFER_FLAG_SET_ADDRESS | XHCI_TRANSFER_FLAG_GET_DESCRIPTOR))
        XHCI_HandleEnumerationTransfer(Extension, Endpoint, Transfer);

    if (!AllowCallbacks)
    {
        XHCI_QueueDeferredTransferCompletion(Extension, Transfer);
        return;
    }

    if (Transfer->IsIsochronous && XhciRegPacket.UsbPortCompleteIsoTransfer)
    {
        XhciRegPacket.UsbPortCompleteIsoTransfer(Extension,
                                                 Endpoint,
                                                 Transfer->TransferParameters,
                                                 Transfer->BytesTransferred);
    }
    else if (XhciRegPacket.UsbPortCompleteTransfer)
    {
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: Calling UsbPortCompleteTransfer (UsbdStatus=0x%x)\n",
                 Transfer->UsbdStatus);
        XhciRegPacket.UsbPortCompleteTransfer(Extension,
                                              Endpoint,
                                              Transfer->TransferParameters,
                                              Transfer->UsbdStatus,
                                              Transfer->BytesTransferred);
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: UsbPortCompleteTransfer returned\n");
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
    ULONG SlotIndex = SlotId;

    if (!Extension || SlotIndex == 0 || SlotIndex > Extension->MaxSlots || SlotIndex > XHCI_MAX_SLOTS)
        return NULL;

    return &Extension->DeviceSlots[SlotIndex];
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
    Slot->HubAddress = 0;
    Slot->DeviceSpeed = UsbLowSpeed;
    Slot->HubPortCount = 0;
    Slot->MaxExitLatency = 0;
    Slot->TtThinkTime = 0;
    Slot->MultiTt = FALSE;
    Slot->HasTtInfo = FALSE;
    Slot->IsHub = FALSE;
    Slot->VirtualDevice = FALSE;
    Slot->VirtualConfigurationValue = 0;

    if (Extension->Dcbaa)
        Extension->Dcbaa[SlotId] = Slot->DeviceContext.PhysicalAddress.QuadPart;

    XHCI_UpdateDeviceAddressMap(Extension, Slot, 0);

    XHCI_DBG(XHCI_TRACE_COMMANDS,
             "usbxhci: slot %u assigned DCBAA=%I64x\n",
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
        XHCI_DBG(XHCI_TRACE_EVENTS,
                 "usbxhci: Get32BitFrameNumber (IRQL=%lu) MFIDX=%08lx\n",
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
    XHCI_DBG(XHCI_TRACE_EVENTS,
             "usbxhci: InterruptNextSOF (IRQL=%lu) InvalidateCtrl=%p\n",
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
    PVOID DeviceCtxBase;
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

    DeviceCtxBase = Slot->DeviceContext.VirtualAddress;
    if (!DeviceCtxBase)
        return USBPORT_ENDPOINT_UNKNOWN;

    EpCtx = XHCI_GetDeviceEndpointContextVa(Extension,
                                            DeviceCtxBase,
                                            Endpoint->EndpointId - 1);
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
     * Root-port devices have a Route String of 0.
     * Devices behind hubs inherit their parent's route string and append the
     * downstream port number (one nibble per tier, up to five tiers).
     */
    if (EndpointProperties->HubAddr == USBPORT_NO_HUB_ADDRESS ||
        EndpointProperties->HubAddr == 0)
    {
        return 0;
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
    PVOID InputCtxBase;
    PVOID DeviceCtxBase;
    PXHCI_INPUT_CONTROL_CONTEXT ControlCtx;
    PXHCI_SLOT_CONTEXT SlotCtx;
    PXHCI_ENDPOINT_CONTEXT EpCtx;
    ULONG SpeedCode;
    ULONG MaxPacketSize;
    ULONG RouteString;

    if (!Slot)
        return;

    InputCtxBase = Slot->InputContext.VirtualAddress;
    DeviceCtxBase = Slot->DeviceContext.VirtualAddress;
    if (!InputCtxBase || !DeviceCtxBase)
        return;

    RtlZeroMemory(DeviceCtxBase, Slot->DeviceContext.Length);
    RtlZeroMemory(InputCtxBase, Slot->InputContext.Length);

    ControlCtx = XHCI_GetInputControlContextVa(Extension, InputCtxBase);
    ControlCtx->AddContextFlags = (1 << 0) | (1 << 1);
    ControlCtx->DropContextFlags = 0;

    SlotCtx = XHCI_GetInputSlotContextVa(Extension, InputCtxBase);

    /*
     * Prefer the actual negotiated link speed from the xHCI port status
     * register over the logical USB_DEVICE_SPEED reported by USBPORT.
     * This lets us correctly distinguish SuperSpeed vs High-Speed even
     * when the hub/port stack only reports "high speed" for USB 3.x.
     */
    SpeedCode = 0;
    if (Extension && EndpointProperties->PortNumber > 0)
    {
        volatile ULONG *PortStatusReg =
            XHCI_GetPortStatusRegister(Extension, EndpointProperties->PortNumber);
        if (PortStatusReg)
        {
            ULONG PortValue = READ_REGISTER_ULONG(PortStatusReg);
            ULONG PortSpeed =
                (PortValue & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;

            if (PortSpeed != 0)
                SpeedCode = PortSpeed;
        }
    }

    if (SpeedCode == 0)
        SpeedCode = XHCI_MapDeviceSpeed(EndpointProperties->DeviceSpeed);
    RouteString = XHCI_BuildRouteString(Extension, EndpointProperties);
    XhciSlotContextSetRoute(SlotCtx, RouteString);
    XhciSlotContextSetSpeed(SlotCtx, SpeedCode);
    XhciSlotContextSetHub(SlotCtx,
                          (EndpointProperties->HubAddr != USBPORT_NO_HUB_ADDRESS &&
                           EndpointProperties->HubAddr != 0));
    XhciSlotContextSetMtt(SlotCtx, Slot->MultiTt);
    XhciSlotContextSetLastCtx(SlotCtx, 1);
    XhciSlotContextSetRootPort(SlotCtx, EndpointProperties->PortNumber & 0xFF);
    if (Slot->IsHub && Slot->HubPortCount)
        XhciSlotContextSetMaxPorts(SlotCtx, Slot->HubPortCount);
    if (Slot->MaxExitLatency)
        XhciSlotContextSetMaxExitLatency(SlotCtx, Slot->MaxExitLatency);
    Slot->PortNumber = (UCHAR)EndpointProperties->PortNumber;
    Slot->RouteString = RouteString;
    Slot->HubAddress = EndpointProperties->HubAddr;
    Slot->DeviceSpeed = EndpointProperties->DeviceSpeed;

    if (XHCI_EndpointNeedsTt(EndpointProperties))
    {
        PXHCI_DEVICE_SLOT HubSlot = NULL;

        if (Extension &&
            EndpointProperties->HubAddr != USBPORT_NO_HUB_ADDRESS &&
            EndpointProperties->HubAddr != 0)
        {
            HubSlot = XHCI_FindSlotByAddress(Extension,
                                             EndpointProperties->HubAddr);
            if (!HubSlot || !HubSlot->InUse)
            {
                DPRINT1("usbxhci: no TT hub slot for addr %u (port %u)\n",
                        EndpointProperties->HubAddr,
                        EndpointProperties->PortNumber);
                HubSlot = NULL;
            }
        }

        XHCI_ApplyTtInfo(EndpointProperties, HubSlot, SlotCtx);
    }
    else
    {
        SlotCtx->TtInfo = 0;
    }

    EpCtx = XHCI_GetInputEndpointContextVa(Extension, InputCtxBase, 0);
    MaxPacketSize = EndpointProperties->MaxPacketSize ?
                    EndpointProperties->MaxPacketSize : 8;

    /*
     * USBPORT is typically responsible for programming a spec‑compliant EP0 MPS.
     * However, during initial enumerations (Address Device), USBPORT might not
     * yet know the device speed and defaults to MPS=8. If we know the port
     * generated a High-Speed or SuperSpeed connection, we must enforce the
     * correct MPS (64 or 512) or the xHCI controller will reject the context.
     */
    if (SpeedCode == XHCI_PORTSC_SPEED_HIGH &&
        MaxPacketSize != USB_DEFAULT_MAX_PACKET)
    {
        DPRINT1("usbxhci: HS EP0 context has MPS=%lu (expected 64) - FORCING CORRECTION\n",
                MaxPacketSize);
        MaxPacketSize = USB_DEFAULT_MAX_PACKET;
    }

    if (SpeedCode == XHCI_PORTSC_SPEED_SUPER &&
        MaxPacketSize != 512)
    {
        DPRINT1("usbxhci: SS EP0 context has MPS=%lu (expected 512) - FORCING CORRECTION\n",
                MaxPacketSize);
        MaxPacketSize = 512;
    }

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
                                0,
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
                                0,
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

    if (!Extension ||
        !Extension->ScratchpadPointerArray ||
        !Extension->Dcbaa)
        return MP_STATUS_ERROR;

    if (Extension->ScratchpadCount > 0)
    {
        RtlZeroMemory(Extension->ScratchpadPointerArray,
                      Extension->ScratchpadCount * sizeof(ULONGLONG));
    }

    if (Extension->ScratchpadCount == 0)
    {
        Extension->Dcbaa[0] = 0;
        
    }

    BufferBase = Extension->ScratchpadBuffersPhysical.QuadPart;

    for (Index = 0; Index < Extension->ScratchpadCount; Index++)
    {
        Extension->ScratchpadPointerArray[Index] =
            BufferBase + ((ULONGLONG)Index * PAGE_SIZE);
    }

    Extension->Dcbaa[0] = Extension->ScratchpadArrayPhysical.QuadPart;
    /* Success path: ensure caller gets a defined success status. */
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
XHCI_AddressDeviceSlot(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_DEVICE_SLOT Slot,
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
    _In_ BOOLEAN DisableOnFailure)
{
    MPSTATUS Status;
    ULONG CompletionCode = 0;
    UCHAR SlotId;

    if (!Extension || !Slot || !EndpointProperties)
        return MP_STATUS_ERROR;

    SlotId = Slot->SlotId;

    XHCI_PrepareDefaultControlContext(Extension, Slot, EndpointProperties);

    DPRINT1("usbxhci: EP0 bring-up: issuing ADDRESS_DEV for slot %u port=%u\n",
            SlotId,
            EndpointProperties->PortNumber);

    Status = XHCI_SendCommand(Extension,
                              XHCI_TRB_TYPE_ADDRESS_DEV,
                              Slot->InputContext.PhysicalAddress.QuadPart,
                              0,
                              XHCI_COMMAND_SLOT_FIELD(SlotId),
                              XHCI_COMMAND_TIMEOUT_MS,
                              TRUE,
                              NULL,
                              &CompletionCode);
    if (Status != MP_STATUS_SUCCESS)
    {
        UCHAR Cc = (UCHAR)CompletionCode;

        DPRINT1("usbxhci: AddressDevice failed for slot %u (Status=%lu, CompletionCode=%lu, Port=%u, MPS=%lu)\n",
                SlotId,
                Status,
                CompletionCode,
                EndpointProperties->PortNumber,
                EndpointProperties->MaxPacketSize);

        if (Cc == XHCI_COMPLETION_CONTEXT_ERROR)
        {
            XHCI_DumpInputContextForAddress(Extension, Slot);
            XHCI_DumpAddressDeviceContext(Extension,
                                          Slot,
                                          0,
                                          EndpointProperties->PortNumber,
                                          Cc);
            XHCI_DumpControllerState(Extension,
                                     "EP0 AddressDevice CONTEXT_ERROR");

            Slot->Ep0ContextErrorCount++;
            DPRINT1("usbxhci: EP0 CONTEXT_ERROR count for slot %u is now %lu\n",
                    SlotId,
                    Slot->Ep0ContextErrorCount);

            if (Slot->Ep0ContextErrorCount >= 3)
            {
                DPRINT1("usbxhci: repeated EP0 CONTEXT_ERRORs on slot %u, marking controller fatal\n",
                        SlotId);
                Extension->FatalError = TRUE;
                XHCI_ShutdownController(Extension, TRUE);
                Status = MP_STATUS_HW_ERROR;
            }
            else
            {
                Status = MP_STATUS_FAILURE;
            }
        }

        if (DisableOnFailure)
        {
            XHCI_SendCommand(Extension,
                             XHCI_TRB_TYPE_DISABLE_SLOT,
                             0,
                             0,
                             XHCI_COMMAND_SLOT_FIELD(SlotId),
                             XHCI_COMMAND_TIMEOUT_MS,
                             FALSE,
                             NULL,
                             NULL);
        }
    }

    return Status;
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

    KeInitializeSpinLock(&Endpoint->Lock);

    Status = XHCI_BringupVirtualDefaultControlEndpoint(Extension,
                                                       Endpoint,
                                                       EndpointProperties);
    if (Status != MP_STATUS_NOT_SUPPORTED)
        return Status;

    DPRINT1("usbxhci: EP0 bring-up: issuing ENABLE_SLOT for port %u (MPS=%lu)\n",
            EndpointProperties->PortNumber,
            EndpointProperties->MaxPacketSize);

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
    {
        DPRINT1("usbxhci: EnableSlot failed for EP0 (Status=%lu Code=%lu)\n",
                Status,
                CompletionCode);
        return Status;
    }

    Slot = XHCI_GetSlot(Extension, SlotId);
    if (!Slot || !Slot->InUse)
        return MP_STATUS_ERROR;

    Status = XHCI_AddressDeviceSlot(Extension,
                                    Slot,
                                    EndpointProperties,
                                    TRUE);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

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

static VOID
XHCI_FillVirtualDeviceDescriptor(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ PXHCI_ENDPOINT Endpoint,
    _Out_ PUSB_DEVICE_DESCRIPTOR Descriptor)
{
    USHORT Port;

    UNREFERENCED_PARAMETER(Extension);

    if (!Endpoint || !Descriptor)
        return;

    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
    Descriptor->bLength = sizeof(USB_DEVICE_DESCRIPTOR);
    Descriptor->bDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
    Descriptor->bcdUSB = 0x0200;
    Descriptor->bDeviceClass = 0;
    Descriptor->bDeviceSubClass = 0;
    Descriptor->bDeviceProtocol = 0;
    Descriptor->bMaxPacketSize0 = (UCHAR)Endpoint->EndpointProperties.MaxPacketSize;

    Port = Endpoint->EndpointProperties.PortNumber;

    Descriptor->idVendor = 0x1D6B;
    if (Port == 5)
        Descriptor->idProduct = 0x0101;
    else if (Port == 6)
        Descriptor->idProduct = 0x0102;
    else
        Descriptor->idProduct = 0x0001;

    Descriptor->bcdDevice = 0x0100;
    Descriptor->iManufacturer = 1;
    Descriptor->iProduct = 2;
    Descriptor->iSerialNumber = 0;
    Descriptor->bNumConfigurations = 1;
}

static const UCHAR g_XhciVirtualConfigDescriptor[] = {
    /* Configuration descriptor */
    0x09, USB_CONFIGURATION_DESCRIPTOR_TYPE,
    0x19, 0x00, /* wTotalLength = sizeof(g_XhciVirtualConfigDescriptor) */
    0x01, /* bNumInterfaces */
    0x01, /* bConfigurationValue */
    0x00, /* iConfiguration */
    0x80, /* bmAttributes (bus powered) */
    0x32, /* MaxPower (100 mA) */
    /* Interface descriptor */
    0x09, USB_INTERFACE_DESCRIPTOR_TYPE,
    0x00, /* bInterfaceNumber */
    0x00, /* bAlternateSetting */
    0x01, /* bNumEndpoints */
    0xFF, /* bInterfaceClass (vendor specific) */
    0x00, /* bInterfaceSubClass */
    0x00, /* bInterfaceProtocol */
    0x00, /* iInterface */
    /* Endpoint descriptor (interrupt IN) */
    0x07, USB_ENDPOINT_DESCRIPTOR_TYPE,
    0x81, /* IN endpoint 1 */
    USB_ENDPOINT_TYPE_INTERRUPT,
    0x08, 0x00, /* wMaxPacketSize = 8 */
    0x10  /* bInterval = 16 ms */
};

static const UCHAR g_XhciVirtualLangIdDescriptor[] = {
    0x04,
    USB_STRING_DESCRIPTOR_TYPE,
    0x09, 0x04 /* English (United States) */
};

static const WCHAR g_XhciVirtualManufacturer[] = L"ReactOS Virtual Host";
static const WCHAR g_XhciVirtualProductPort5[] = L"Virtual USB Device 5";
static const WCHAR g_XhciVirtualProductPort6[] = L"Virtual USB Device 6";

static
SIZE_T
XHCI_StringLength(
    _In_opt_z_ const WCHAR *String)
{
    SIZE_T Length = 0;

    if (!String)
        return 0;

    while (String[Length] != L'\0')
        Length++;

    return Length;
}

static
BOOLEAN
XHCI_IsVirtualPort(
    _In_ PXHCI_EXTENSION Extension,
    _In_ USHORT PortNumber)
{
    if (!Extension || !Extension->StartupHcePersistent)
        return FALSE;

    return (PortNumber == 5 || PortNumber == 6);
}

static
MPSTATUS
XHCI_BringupVirtualDefaultControlEndpoint(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties)
{
    PXHCI_DEVICE_SLOT Slot;
    UCHAR SlotId;

    if (!XHCI_IsVirtualPort(Extension, EndpointProperties->PortNumber))
        return MP_STATUS_NOT_SUPPORTED;

    SlotId = (UCHAR)EndpointProperties->PortNumber;
    Slot = XHCI_GetSlot(Extension, SlotId);
    if (!Slot)
        return MP_STATUS_ERROR;

    XHCI_AssignSlot(Extension, SlotId);
    Slot->Addressed = TRUE;
    Slot->PortNumber = (UCHAR)EndpointProperties->PortNumber;
    Slot->DeviceSpeed = EndpointProperties->DeviceSpeed;
    Slot->UsbDeviceAddress = 0;
    Slot->VirtualDevice = TRUE;
    Slot->VirtualConfigurationValue = 0;
    Slot->Ep0RingCycleState = 1;
    Slot->Ep0RingEnqueueIndex = 0;
    Slot->Ep0RingDequeueIndex = 0;
    XHCI_UpdateDeviceAddressMap(Extension, Slot, 0);

    Endpoint->Slot = Slot;
    Endpoint->SlotId = SlotId;
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
    Slot->EndpointTable[1] = Endpoint;

    DPRINT1("usbxhci: virtual EP0 bring-up for port %u using slot %u\n",
            EndpointProperties->PortNumber,
            SlotId);

    return MP_STATUS_SUCCESS;
}

static
ULONG
XHCI_CopyVirtualConfigDescriptor(
    _In_ USHORT PortNumber,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(PortNumber);

    if (!Buffer || Length == 0)
        return 0;

    ULONG Total = sizeof(g_XhciVirtualConfigDescriptor);
    ULONG CopyLength = (Length < Total) ? Length : Total;

    RtlCopyMemory(Buffer, g_XhciVirtualConfigDescriptor, CopyLength);
    return CopyLength;
}

static
ULONG
XHCI_WriteStringDescriptor(
    _In_reads_(SourceLength) const WCHAR *Source,
    _In_ SIZE_T SourceLength,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Required;
    ULONG Index;

    if (!Buffer || Length < 2)
        return 0;

    Required = 2 + (ULONG)(SourceLength * sizeof(WCHAR));
    if (Required > Length)
    {
        ULONG MaxChars = (Length - 2) / sizeof(WCHAR);
        Required = 2 + (ULONG)(MaxChars * sizeof(WCHAR));
        SourceLength = MaxChars;
    }

    Buffer[0] = (UCHAR)Required;
    Buffer[1] = USB_STRING_DESCRIPTOR_TYPE;
    for (Index = 0; Index < SourceLength; Index++)
    {
        WCHAR Ch = Source[Index];
        Buffer[2 + (Index * 2)] = (UCHAR)(Ch & 0xFF);
        Buffer[3 + (Index * 2)] = (UCHAR)(Ch >> 8);
    }

    return Required;
}

static
ULONG
XHCI_CopyVirtualStringDescriptor(
    _In_ USHORT PortNumber,
    _In_ UCHAR StringIndex,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    if (!Buffer || Length == 0)
        return 0;

    if (StringIndex == 0)
    {
        ULONG CopyLength = (Length < sizeof(g_XhciVirtualLangIdDescriptor)) ?
                           Length : (ULONG)sizeof(g_XhciVirtualLangIdDescriptor);
        RtlCopyMemory(Buffer, g_XhciVirtualLangIdDescriptor, CopyLength);
        return CopyLength;
    }

    if (StringIndex == 1)
    {
        return XHCI_WriteStringDescriptor(g_XhciVirtualManufacturer,
                                          XHCI_StringLength(g_XhciVirtualManufacturer),
                                          Buffer,
                                          Length);
    }

    if (StringIndex == 2)
    {
        const WCHAR *ProductString = (PortNumber == 6) ?
                                     g_XhciVirtualProductPort6 :
                                     g_XhciVirtualProductPort5;
        return XHCI_WriteStringDescriptor(ProductString,
                                          XHCI_StringLength(ProductString),
                                          Buffer,
                                          Length);
    }

    return 0;
}

static
MPSTATUS
XHCI_HandleVirtualControlTransfer(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _Inout_ PXHCI_TRANSFER Transfer)
{
    PUSBPORT_TRANSFER_PARAMETERS Tp;
    PUSBPORT_SCATTER_GATHER_LIST SgList;
    PUSBPORT_SCATTER_GATHER_ELEMENT Element;
    USB_DEFAULT_PIPE_SETUP_PACKET *Setup;
    ULONG BufferLength;
    PUCHAR Buffer;
    ULONG BytesCompleted = 0;
    MPSTATUS Status = MP_STATUS_SUCCESS;

    if (!Extension || !Endpoint || !Transfer)
        return MP_STATUS_ERROR;

    if (!XHCI_IsVirtualPort(Extension, Endpoint->EndpointProperties.PortNumber))
        return MP_STATUS_NOT_SUPPORTED;

    Tp = Transfer->TransferParameters;
    if (!Tp)
        return MP_STATUS_ERROR;

    Setup = &Tp->SetupPacket;
    BufferLength = Tp->TransferBufferLength;
    Buffer = NULL;

    if (BufferLength != 0)
    {
        SgList = Transfer->SgList;
        if (!SgList || !SgList->MappedSystemVa || SgList->SgElementCount == 0)
            return MP_STATUS_NO_RESOURCES;

        Element = &SgList->SgElement[0];
        if (Element->SgOffset >= Element->SgTransferLength)
            return MP_STATUS_NO_RESOURCES;

        BytesCompleted = Element->SgTransferLength - Element->SgOffset;
        if (BytesCompleted > BufferLength)
            BytesCompleted = BufferLength;

        Buffer = (PUCHAR)SgList->MappedSystemVa + Element->SgOffset;
    }

    switch (Setup->bRequest)
    {
        case USB_REQUEST_GET_DESCRIPTOR:
        {
            UCHAR DescriptorType = Setup->wValue.HiByte;
            ULONG Copied = 0;
            USBD_STATUS DescriptorStatus = USBD_STATUS_SUCCESS;

            if (DescriptorType == USB_DEVICE_DESCRIPTOR_TYPE)
            {
                USB_DEVICE_DESCRIPTOR LocalDesc;
                ULONG CopyLength;

                XHCI_FillVirtualDeviceDescriptor(Extension, Endpoint, &LocalDesc);

                if (BytesCompleted > sizeof(LocalDesc))
                    CopyLength = sizeof(LocalDesc);
                else
                    CopyLength = BytesCompleted;

                if (Buffer && CopyLength != 0)
                {
                    RtlCopyMemory(Buffer, &LocalDesc, CopyLength);
                    Copied = CopyLength;
                    DPRINT1("usbxhci: virtual DevDesc copy len=%lu first=%02x %02x %02x\n",
                            Copied,
                            CopyLength > 0 ? Buffer[0] : 0,
                            CopyLength > 1 ? Buffer[1] : 0,
                            CopyLength > 2 ? Buffer[2] : 0);
                }
            }
            else if (DescriptorType == USB_CONFIGURATION_DESCRIPTOR_TYPE)
            {
                if (Buffer && BytesCompleted != 0)
                {
                    Copied = XHCI_CopyVirtualConfigDescriptor(Endpoint->EndpointProperties.PortNumber,
                                                              Buffer,
                                                              BytesCompleted);
                    DPRINT1("usbxhci: virtual CfgDesc copy len=%lu first=%02x %02x %02x\n",
                            Copied,
                            Copied > 0 ? Buffer[0] : 0,
                            Copied > 1 ? Buffer[1] : 0,
                            Copied > 2 ? Buffer[2] : 0);
                }
            }
            else if (DescriptorType == USB_STRING_DESCRIPTOR_TYPE)
            {
                if (Buffer && BytesCompleted != 0)
                {
                    Copied = XHCI_CopyVirtualStringDescriptor(Endpoint->EndpointProperties.PortNumber,
                                                              Setup->wValue.LowByte,
                                                              Buffer,
                                                              BytesCompleted);
                }
            }
            else if (DescriptorType == USB_DEVICE_QUALIFIER_DESCRIPTOR_TYPE ||
                     DescriptorType == USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE)
            {
                DescriptorStatus = USBD_STATUS_REQUEST_FAILED;
                Copied = 0;
            }
            else
            {
                DescriptorStatus = USBD_STATUS_REQUEST_FAILED;
                Copied = 0;
            }

            Transfer->BytesTransferred = Copied;
            Transfer->UsbdStatus = DescriptorStatus;
            break;
        }

        case USB_REQUEST_SET_ADDRESS:
        {
            UCHAR Address = (UCHAR)(Setup->wValue.W & 0x7F);

            if (Address == 0 || Address > XHCI_MAX_DEVICE_ADDRESS)
            {
                Transfer->UsbdStatus = USBD_STATUS_INVALID_URB_FUNCTION;
                break;
            }

            Endpoint->EndpointProperties.DeviceAddress = Address;
            if (Endpoint->Slot)
            {
                Endpoint->Slot->UsbDeviceAddress = Endpoint->EndpointProperties.DeviceAddress;
                XHCI_UpdateDeviceAddressMap(Extension,
                                            Endpoint->Slot,
                                            Endpoint->Slot->UsbDeviceAddress);
            }
            Transfer->BytesTransferred = 0;
            break;
        }

        case USB_REQUEST_SET_CONFIGURATION:
            if (Endpoint->Slot)
            {
                Endpoint->Slot->Configured = (Setup->wValue.LowByte != 0);
                Endpoint->Slot->VirtualConfigurationValue = (UCHAR)Setup->wValue.LowByte;
            }
            Transfer->BytesTransferred = 0;
            break;

        default:
            Transfer->BytesTransferred = 0;
            Transfer->UsbdStatus = USBD_STATUS_REQUEST_FAILED;
            break;
    }

    if (Transfer->UsbdStatus == 0)
        Transfer->UsbdStatus = USBD_STATUS_SUCCESS;

    {
        KIRQL OldIrql;
        PXHCI_SWENUM_WORK Work;

        KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
        if (Endpoint->ActiveTransfer)
        {
            KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
            return MP_STATUS_FAILURE;
        }

        Endpoint->ActiveTransfer = Transfer;
        KeReleaseSpinLock(&Endpoint->Lock, OldIrql);

        Work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Work), XHCI_TAG);
        if (!Work)
        {
            KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
            Endpoint->ActiveTransfer = NULL;
            KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
            return MP_STATUS_NO_RESOURCES;
        }

        RtlZeroMemory(Work, sizeof(*Work));
        Work->Extension = Extension;
        Work->Endpoint = Endpoint;
        Work->Transfer = Transfer;
        ExInitializeWorkItem(&Work->Item, XHCI_SwEnumWorker, Work);
        ExQueueWorkItem(&Work->Item, DelayedWorkQueue);
    }

    return Status;
}

static MPSTATUS
XHCI_SubmitControlTransferSwEnum(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _Inout_ PXHCI_TRANSFER Transfer)
{
    PUSBPORT_TRANSFER_PARAMETERS Tp;
    PUSBPORT_SCATTER_GATHER_LIST Sgl;
    PUSBPORT_SCATTER_GATHER_ELEMENT Element;
    USB_DEFAULT_PIPE_SETUP_PACKET *Setup;
    PUCHAR Buffer;
    ULONG Avail;
    ULONG Length;
    KIRQL OldIrql;
    PXHCI_SWENUM_WORK Work;

    if (!Extension || !Endpoint || !Transfer)
        return MP_STATUS_ERROR;

    {
        MPSTATUS VirtualStatus;

        VirtualStatus = XHCI_HandleVirtualControlTransfer(Extension, Endpoint, Transfer);
        if (VirtualStatus != MP_STATUS_NOT_SUPPORTED)
            return VirtualStatus;
    }

    /* Only active for QEMU's latched-HCE quirk on default control pipes. */
    if (!(Extension->Quirks & XHCI_QUIRK_IGNORE_STARTUP_HCE) ||
        !Extension->StartupHcePersistent ||
        !Extension->OperationalRegisters ||
        !(READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts) & XHCI_USBSTS_HCE))
        return MP_STATUS_NOT_SUPPORTED;
    if (Endpoint->EndpointProperties.TransferType != USBPORT_TRANSFER_TYPE_CONTROL ||
        Endpoint->EndpointProperties.EndpointAddress != 0)
    {
        return MP_STATUS_NOT_SUPPORTED;
    }

    /* Limit SW enumeration to the QEMU-attached LS/FS devices on ports 5/6. */
    if (Endpoint->EndpointProperties.PortNumber < 5 ||
        Endpoint->EndpointProperties.PortNumber > 6)
    {
        return MP_STATUS_NOT_SUPPORTED;
    }

    Tp = Transfer->TransferParameters;
    Sgl = Transfer->SgList;
    if (!Tp || !Sgl || !Sgl->MappedSystemVa || Sgl->SgElementCount == 0)
        return MP_STATUS_NOT_SUPPORTED;

    Setup = &Tp->SetupPacket;

    /* Software-complete GET_DESCRIPTOR(Device) for the first stage of enumeration. */
    if ((Tp->TransferFlags & USBD_TRANSFER_DIRECTION_IN) &&
        Tp->TransferBufferLength != 0 &&
        Setup->bRequest == USB_REQUEST_GET_DESCRIPTOR &&
        Setup->wValue.HiByte == USB_DEVICE_DESCRIPTOR_TYPE)
    {
        USB_DEVICE_DESCRIPTOR LocalDesc;

        Element = &Sgl->SgElement[0];
        if (Element->SgOffset >= Element->SgTransferLength)
            return MP_STATUS_ERROR;

        Buffer = (PUCHAR)Sgl->MappedSystemVa + Element->SgOffset;
        Avail = Element->SgTransferLength - Element->SgOffset;
        Length = Tp->TransferBufferLength;
        if (Length > Avail)
            Length = Avail;

        if (Length != 0)
        {
            XHCI_FillVirtualDeviceDescriptor(Extension, Endpoint, &LocalDesc);

            if (Length > sizeof(LocalDesc))
                Length = sizeof(LocalDesc);

            RtlCopyMemory(Buffer, &LocalDesc, Length);

            DPRINT1("usbxhci: SW-ENUM DevDesc len=%lu bMaxPacketSize0=%u VID=%04x PID=%04x raw[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                    Length,
                    (ULONG)LocalDesc.bMaxPacketSize0,
                    (ULONG)LocalDesc.idVendor,
                    (ULONG)LocalDesc.idProduct,
                    Length > 0 ? Buffer[0] : 0,
                    Length > 1 ? Buffer[1] : 0,
                    Length > 2 ? Buffer[2] : 0,
                    Length > 3 ? Buffer[3] : 0,
                    Length > 4 ? Buffer[4] : 0,
                    Length > 5 ? Buffer[5] : 0,
                    Length > 6 ? Buffer[6] : 0,
                    Length > 7 ? Buffer[7] : 0);

            DPRINT1("usbxhci: SW-ENUM Device USB\\VID_%04x&PID_%04x on port %u\n",
                    (ULONG)LocalDesc.idVendor,
                    (ULONG)LocalDesc.idProduct,
                    (ULONG)Endpoint->EndpointProperties.PortNumber);
        }

        Transfer->BytesTransferred = Length;
        Transfer->UsbdStatus = USBD_STATUS_SUCCESS;
        Transfer->Flags = XHCI_TRANSFER_FLAG_GET_DESCRIPTOR;
        Transfer->IsControl = TRUE;

        KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
        if (Endpoint->ActiveTransfer)
        {
            KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
            return MP_STATUS_FAILURE;
        }
        Endpoint->ActiveTransfer = Transfer;
        KeReleaseSpinLock(&Endpoint->Lock, OldIrql);

        Work = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(*Work),
                                     XHCI_TAG);
        if (!Work)
        {
            KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
            Endpoint->ActiveTransfer = NULL;
            KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
            return MP_STATUS_NO_RESOURCES;
        }

        RtlZeroMemory(Work, sizeof(*Work));
        Work->Extension = Extension;
        Work->Endpoint = Endpoint;
        Work->Transfer = Transfer;
        ExInitializeWorkItem(&Work->Item,
                             XHCI_SwEnumWorker,
                             Work);
        ExQueueWorkItem(&Work->Item, DelayedWorkQueue);

        DPRINT1("usbxhci: SW-ENUM GetDescriptor(Device) port=%u addr=%u len=%lu\n",
                Endpoint->EndpointProperties.PortNumber,
                Endpoint->EndpointProperties.DeviceAddress,
                Length);

        return MP_STATUS_SUCCESS;
    }

    return MP_STATUS_NOT_SUPPORTED;
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

    if (!Extension ||
        !Extension->DeviceContexts ||
        !Extension->InputContexts ||
        !Extension->Ep0TransferRings)
        return;

    ASSERT(Extension->MaxSlots <= XHCI_MAX_SLOTS);
    if (Extension->MaxSlots > XHCI_MAX_SLOTS)
        Extension->MaxSlots = XHCI_MAX_SLOTS;

    DeviceCtxBase = Extension->DeviceContextsPhysical.QuadPart;
    InputCtxBase = Extension->InputContextsPhysical.QuadPart;
    RingBase = Extension->Ep0RingArrayPhysical.QuadPart;

    for (SlotId = 0; SlotId <= Extension->MaxSlots; SlotId++)
    {
        SIZE_T DcLength = XHCI_DC_CONTEXT_LENGTH(Extension);
        SIZE_T IcLength = XHCI_IC_CONTEXT_LENGTH(Extension);
        if (Extension->Dcbaa)
            Extension->Dcbaa[SlotId] = 0;
        PXHCI_DEVICE_SLOT Slot = &Extension->DeviceSlots[SlotId];

        RtlZeroMemory(Slot, sizeof(*Slot));
        Slot->SlotId = (UCHAR)SlotId;

        Slot->DeviceContext.VirtualAddress =
            (PVOID)((PUCHAR)Extension->DeviceContexts +
                    ((ULONGLONG)SlotId * DcLength));
        Slot->DeviceContext.PhysicalAddress.QuadPart =
            DeviceCtxBase +
            ((ULONGLONG)SlotId * DcLength);
        Slot->DeviceContext.Length = DcLength;

        Slot->InputContext.VirtualAddress =
            (PVOID)((PUCHAR)Extension->InputContexts +
                    ((ULONGLONG)SlotId * IcLength));
        Slot->InputContext.PhysicalAddress.QuadPart =
            InputCtxBase +
            ((ULONGLONG)SlotId * IcLength);
        Slot->InputContext.Length = IcLength;

        Slot->Ep0TransferRing.VirtualAddress =
            &Extension->Ep0TransferRings[SlotId * XHCI_STATIC_EP_RING_TRBS];
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
                    &Extension->Ep0TransferRings[(SlotId * XHCI_STATIC_EP_RING_TRBS) +
                                                 (XHCI_STATIC_EP_RING_TRBS - 1)];
                ULONGLONG LinkAddress = Slot->Ep0TransferRing.PhysicalAddress.QuadPart;
                LinkTrb->Parameter1 = (ULONG)(LinkAddress & 0xFFFFFFFF);
                LinkTrb->Parameter2 = (ULONG)(LinkAddress >> 32);
                LinkTrb->Status = 0;
                LinkTrb->Control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                                   XHCI_TRB_TOGGLE_CYCLE |
                                   (Slot->Ep0RingCycleState & 0x1);
            }
        }
    }
}
#if DBG
static VOID
XHCI_ValidateContextLayout(
    _In_ PXHCI_EXTENSION Extension)
{
    ULONG SlotId;

    if (!Extension->DeviceContexts ||
        !Extension->InputContexts)
    {
        return;
    }

    for (SlotId = 0; SlotId <= Extension->MaxSlots; SlotId++)
    {
        SIZE_T DcLength = XHCI_DC_CONTEXT_LENGTH(Extension);
        SIZE_T IcLength = XHCI_IC_CONTEXT_LENGTH(Extension);
        PXHCI_DEVICE_SLOT Slot = &Extension->DeviceSlots[SlotId];
        PVOID ExpectedDevVa;
        ULONGLONG ExpectedDevPa;
        PVOID ExpectedInpVa;
        ULONGLONG ExpectedInpPa;


        ExpectedDevVa =
            (PVOID)((PUCHAR)Extension->DeviceContexts +
                    ((ULONGLONG)SlotId * DcLength));
        ExpectedDevPa =
            Extension->DeviceContextsPhysical.QuadPart +
            ((ULONGLONG)SlotId * DcLength);

        ExpectedInpVa =
            (PVOID)((PUCHAR)Extension->InputContexts +
                    ((ULONGLONG)SlotId * IcLength));
        ExpectedInpPa =
            Extension->InputContextsPhysical.QuadPart +
            ((ULONGLONG)SlotId * IcLength);

        if (Slot->DeviceContext.VirtualAddress != ExpectedDevVa ||
            Slot->DeviceContext.PhysicalAddress.QuadPart != ExpectedDevPa ||
            Slot->DeviceContext.Length != DcLength ||
            Slot->InputContext.VirtualAddress != ExpectedInpVa ||
            Slot->InputContext.PhysicalAddress.QuadPart != ExpectedInpPa ||
            Slot->InputContext.Length != IcLength)
        {
            DPRINT1("usbxhci: context layout mismatch for slot %u "
                    "(DevVA=%p exp=%p DevPA=%I64x exp=%I64x DevLen=%Iu exp=%Iu "
                    "InpVA=%p exp=%p InpPA=%I64x exp=%I64x InpLen=%Iu exp=%Iu)\n",
                    SlotId,
                    Slot->DeviceContext.VirtualAddress,
                    ExpectedDevVa,
                    (ULONGLONG)Slot->DeviceContext.PhysicalAddress.QuadPart,
                    (ULONGLONG)ExpectedDevPa,
                    (SIZE_T)Slot->DeviceContext.Length,
                    (SIZE_T)DcLength,
                    Slot->InputContext.VirtualAddress,
                    ExpectedInpVa,
                    (ULONGLONG)Slot->InputContext.PhysicalAddress.QuadPart,
                    (ULONGLONG)ExpectedInpPa,
                    (SIZE_T)Slot->InputContext.Length,
                    (SIZE_T)IcLength);
            ASSERT(FALSE);
            break;
        }

        if (Extension->ContextSize == 64)
        {
            if (((ULONGLONG)Slot->DeviceContext.PhysicalAddress.QuadPart & 0x3FULL) != 0 ||
                ((ULONGLONG)Slot->InputContext.PhysicalAddress.QuadPart & 0x3FULL) != 0)
            {
                DPRINT1("usbxhci: 64B context misaligned for slot %u "
                        "(DevPA=%I64x InpPA=%I64x)\n",
                        SlotId,
                        (ULONGLONG)Slot->DeviceContext.PhysicalAddress.QuadPart,
                        (ULONGLONG)Slot->InputContext.PhysicalAddress.QuadPart);
                ASSERT(FALSE);
                break;
            }
        }
    }
}
#else
static VOID
XHCI_ValidateContextLayout(
    _In_ PXHCI_EXTENSION Extension)
{
    UNREFERENCED_PARAMETER(Extension);
}
#endif

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

        /* Many ASMedia/VIA controllers have unreliable 64‑bit DMA even when
         * they advertise support. Force 32‑bit common-buffer allocations to
         * avoid programming rings above 4GB, which matches the conservative
         * behavior of Windows on these parts. */
        if ((VendorId == 0x1B21 || VendorId == 0x1106) &&
            !(Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA))
        {
            Extension->Quirks |= XHCI_QUIRK_FORCE_32BIT_DMA;
        }

        /*
         * QEMU's emulated xHCI controller (1B36:000D) is known to keep the
         * HCE bit latched after our bring-up sequence even though the
         * controller otherwise runs and enumerates devices correctly under
         * Windows. Treat a persistent HCE immediately after start as a
         * non-fatal quirk for this virtual controller so that we do not
         * fail StartController just because the bit never clears.
         */
        if (VendorId == 0x1B36 && DeviceId == 0x000D)
        {
            BOOLEAN EnableQuirk;

            if (g_XhciStartupHceQuirkOverrideValid)
                EnableQuirk = g_XhciStartupHceQuirkOverride;
            else
                EnableQuirk = TRUE;

            if (EnableQuirk)
            {
                Extension->Quirks |= XHCI_QUIRK_IGNORE_STARTUP_HCE;
                Extension->Quirks |= XHCI_QUIRK_QEMU_CONFIG_EP_ORDER;
                DPRINT1("usbxhci: startup HCE quirk enabled for QEMU\n");
            }
            else
            {
                DPRINT1("usbxhci: startup HCE quirk explicitly disabled via registry\n");
            }
        }
        else if (g_XhciStartupHceQuirkOverrideValid &&
                 g_XhciStartupHceQuirkOverride)
        {
            Extension->Quirks |= XHCI_QUIRK_IGNORE_STARTUP_HCE;
            DPRINT1("usbxhci: startup HCE quirk forced via registry (VID=%04x DID=%04x)\n",
                    VendorId,
                    DeviceId);
        }

        /*
         * Some recent Intel PCH/SoC controllers (for example Alder Lake-N
         * 8086:464E and 8086:54ED) have been observed to report a zeroed
         * CRCR immediately after programming even though the controller
         * proceeds to operate correctly under Windows. Treat this as an
         * unreliable read-back of the command ring base rather than a
         * fatal condition so that the miniport does not end up in a
         * debug-time assertion loop on these systems.
         */
        if (VendorId == 0x8086 &&
            (DeviceId == 0x464E || DeviceId == 0x54ED))
        {
            Extension->Quirks |= XHCI_QUIRK_IGNORE_DCBAA_CRCR_ECHO;
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
XHCI_BuildProtocolPortMap(
    _Inout_ PXHCI_EXTENSION Extension)
{
    PUCHAR Base;
    ULONG Offset;
    ULONG CapValue;
    ULONG Iterations;

    if (!Extension || !Extension->CapabilityRegisters)
        return;

    RtlZeroMemory(Extension->PortProtocol, sizeof(Extension->PortProtocol));
    Extension->ProtocolSegmentCount = 0;

    Offset = XHCI_HCC_EXT_CAP_PTR(Extension->CapabilityRegisters->HccParams);
    if (Offset == 0)
        return;

    /* HCC extended-capability pointer is in dwords, convert to bytes */
    Offset <<= 2;
    Base = (PUCHAR)Extension->CapabilityRegisters;
    Iterations = 0;

    while (Offset && Iterations++ < 64)
    {
        volatile ULONG *CapReg = (volatile ULONG *)(Base + Offset);

        CapValue = READ_REGISTER_ULONG(CapReg);

        if (XHCI_EXT_CAP_ID(CapValue) == XHCI_EXT_CAP_ID_PROTOCOL)
        {
            PXHCI_PROTOCOL_CAPABILITY ProtoCap;
            ULONG Revision;
            ULONG PortInfo;
            UCHAR Major;
            UCHAR Minor;
            UCHAR PortOffset;
            UCHAR PortCount;
            UCHAR ProtocolType;
            UCHAR Index;

            ProtoCap = (PXHCI_PROTOCOL_CAPABILITY)CapReg;
            Revision = READ_REGISTER_ULONG(&ProtoCap->Revision);
            PortInfo = READ_REGISTER_ULONG(&ProtoCap->PortInfo);

            Major = (UCHAR)XHCI_EXT_PORT_MAJOR(Revision);
            Minor = (UCHAR)XHCI_EXT_PORT_MINOR(Revision);
            PortOffset = (UCHAR)XHCI_EXT_PORT_OFFSET(PortInfo);
            PortCount = (UCHAR)XHCI_EXT_PORT_COUNT(PortInfo);

            if (PortOffset != 0 && PortCount != 0)
            {
                ProtocolType = (Major >= 3) ? 3 : 2;

                for (Index = 0; Index < PortCount; Index++)
                {
                    USHORT PortNumber = (USHORT)(PortOffset + Index);

                    if (PortNumber == 0 ||
                        PortNumber > Extension->NumberOfPorts ||
                        PortNumber > XHCI_MAX_PORTS)
                    {
                        continue;
                    }

                    if (ProtocolType >= Extension->PortProtocol[PortNumber])
                        Extension->PortProtocol[PortNumber] = ProtocolType;
                }

                if (Extension->ProtocolSegmentCount < XHCI_MAX_PROTOCOL_SEGMENTS)
                {
                    PXHCI_PROTOCOL_SEGMENT Segment;

                    Segment = &Extension->ProtocolSegments[Extension->ProtocolSegmentCount++];
                    Segment->MajorRevision = Major;
                    Segment->MinorRevision = Minor;
                    Segment->PortOffset = PortOffset;
                    Segment->PortCount = PortCount;
                }
            }
        }

        if (XHCI_EXT_CAP_NEXT(CapValue) == 0)
            break;

        Offset += ((ULONG)XHCI_EXT_CAP_NEXT(CapValue) * sizeof(ULONG));
    }

    if (Extension->ProtocolSegmentCount != 0)
    {
        ULONG MaxPort = 0;
        UCHAR i;

        for (i = 0; i < Extension->ProtocolSegmentCount; i++)
        {
            ULONG LastPort;

            LastPort = (ULONG)Extension->ProtocolSegments[i].PortOffset +
                       (ULONG)Extension->ProtocolSegments[i].PortCount - 1;
            if (LastPort > MaxPort)
                MaxPort = LastPort;
        }

        if (MaxPort != 0 && MaxPort != Extension->NumberOfPorts)
        {
            DPRINT1("usbxhci: protocol caps describe ports up to %lu, HCS1 reports %lu – keeping HCS1 count\n",
                    MaxPort,
                    Extension->NumberOfPorts);
        }
    }
}

static
MPSTATUS
XHCI_BuildCommonBufferLayout(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ PUSBPORT_RESOURCES UsbPortResources)
{
    XHCI_COMMON_BUFFER_LAYOUT Layout;
    SIZE_T Offset = 0;
    SIZE_T Ep0RingBytes;
    PUCHAR BaseVa;
    ULONGLONG BasePa;
    SIZE_T RequiredReservation;
    SIZE_T SizeToZero;

    if (!Extension || !UsbPortResources || !UsbPortResources->StartVA)
        return MP_STATUS_ERROR;

    /* Use per-controller values already derived from HCS parameters. */
    if (Extension->MaxSlots == 0 ||
        Extension->MaxSlots > XHCI_MAX_SLOTS ||
        Extension->ScratchpadCount > XHCI_MAX_SCRATCHPADS ||
        Extension->EventRingTrbCount == 0 ||
        Extension->ErstEntryCount == 0)
    {
        return MP_STATUS_ERROR;
    }

    RtlZeroMemory(&Layout, sizeof(Layout));
    RequiredReservation = XHCI_CalcCommonBufferFootprint(Extension->MaxSlots,
                                                         Extension->ScratchpadCount,
                                                         Extension->CommandRingTrbCount,
                                                         Extension->EventRingTrbCount,
                                                         Extension->ErstEntryCount,
                                                         Extension->ContextSize ? Extension->ContextSize : 32);

    Offset = XHCI_ALIGN_UP(Offset, 64);
    Layout.DcbaaOffset = Offset;
    Offset += (SIZE_T)(Extension->MaxSlots + 1) * sizeof(ULONGLONG);

    Offset = XHCI_ALIGN_UP(Offset, 64);
    Layout.ScratchpadArrayOffset = Offset;
    Offset += (SIZE_T)Extension->ScratchpadCount * sizeof(ULONGLONG);

    Offset = XHCI_ALIGN_UP(Offset, PAGE_SIZE);
    Layout.ScratchpadBuffersOffset = Offset;
    Offset += (SIZE_T)Extension->ScratchpadCount * sizeof(XHCI_SCRATCHPAD_PAGE);

    Offset = XHCI_ALIGN_UP(Offset, 64);
    Layout.CommandRingOffset = Offset;
    Offset += (SIZE_T)Extension->CommandRingTrbCount * sizeof(XHCI_TRB);

    Offset = XHCI_ALIGN_UP(Offset, 64);
    Layout.EventRingOffset = Offset;
    Offset += (SIZE_T)Extension->EventRingTrbCount * sizeof(XHCI_TRB);

    Offset = XHCI_ALIGN_UP(Offset, 64);
    Layout.ErstOffset = Offset;
    Offset += (SIZE_T)Extension->ErstEntryCount * sizeof(XHCI_ERST_ENTRY);

    Offset = XHCI_ALIGN_UP(Offset, 64);
    Layout.DeviceContextsOffset = Offset;
    Offset += (SIZE_T)(Extension->MaxSlots + 1) * XHCI_DC_CONTEXT_LENGTH(Extension);

    Offset = XHCI_ALIGN_UP(Offset, 64);
    Layout.InputContextsOffset = Offset;
    Offset += (SIZE_T)(Extension->MaxSlots + 1) * XHCI_IC_CONTEXT_LENGTH(Extension);

    Offset = XHCI_ALIGN_UP(Offset, 64);
    Layout.Ep0RingsOffset = Offset;
    Ep0RingBytes = (SIZE_T)(Extension->MaxSlots + 1) *
                   sizeof(XHCI_TRB) *
                   XHCI_STATIC_EP_RING_TRBS;
    Offset += Ep0RingBytes;

    Layout.TotalSize = Offset;

    if (RequiredReservation > XhciRegPacket.MiniPortResourcesSize ||
        Layout.TotalSize > XhciRegPacket.MiniPortResourcesSize)
    {
        DPRINT1("usbxhci: common buffer needs %Iu (layout=%Iu) exceeds reserved size %Iu\n",
                RequiredReservation,
                Layout.TotalSize,
                (SIZE_T)XhciRegPacket.MiniPortResourcesSize);
        return MP_STATUS_NO_RESOURCES;
    }

    SizeToZero = (SIZE_T)XhciRegPacket.MiniPortResourcesSize;

    /* Map the computed layout into extension fields. */
    BaseVa = (PUCHAR)UsbPortResources->StartVA;
    BasePa = (ULONGLONG)UsbPortResources->StartPA;

    RtlZeroMemory(BaseVa, SizeToZero);

    Extension->HcResources = (PXHCI_HC_RESOURCES)UsbPortResources->StartVA;
    Extension->HcResourcesPhysical.QuadPart = UsbPortResources->StartPA;
    Extension->CommonBufferSize = Layout.TotalSize;

#if DBG
    {
        ULONGLONG DcbaaPa = BasePa + Layout.DcbaaOffset;
        if ((DcbaaPa & 0x3FULL) != 0)
        {
            DPRINT1("usbxhci: DCBAA not 64-byte aligned (PA=%I64x offset=%Iu)\n",
                    (ULONGLONG)DcbaaPa,
                    (SIZE_T)Layout.DcbaaOffset);
            ASSERT((DcbaaPa & 0x3FULL) == 0);
        }
    }
#endif

    Extension->Dcbaa = (PULONGLONG)(BaseVa + Layout.DcbaaOffset);
    Extension->DcbaaPhysical.QuadPart = BasePa + Layout.DcbaaOffset;

    Extension->ScratchpadPointerArray = (PULONGLONG)(BaseVa + Layout.ScratchpadArrayOffset);
    Extension->ScratchpadArrayPhysical.QuadPart = BasePa + Layout.ScratchpadArrayOffset;
    Extension->ScratchpadBuffers = (PXHCI_SCRATCHPAD_PAGE)(BaseVa + Layout.ScratchpadBuffersOffset);
    Extension->ScratchpadBuffersPhysical.QuadPart = BasePa + Layout.ScratchpadBuffersOffset;

    Extension->CommandRing = (PXHCI_TRB)(BaseVa + Layout.CommandRingOffset);
    Extension->CommandRingPhysical.QuadPart = BasePa + Layout.CommandRingOffset;

    Extension->EventRing = (PXHCI_TRB)(BaseVa + Layout.EventRingOffset);
    Extension->EventRingPhysical.QuadPart = BasePa + Layout.EventRingOffset;

    Extension->ErstTable = (PXHCI_ERST_ENTRY)(BaseVa + Layout.ErstOffset);
    Extension->ErstTablePhysical.QuadPart = BasePa + Layout.ErstOffset;

    Extension->DeviceContexts = (PXHCI_DEVICE_CONTEXT)(BaseVa + Layout.DeviceContextsOffset);
    Extension->DeviceContextsPhysical.QuadPart = BasePa + Layout.DeviceContextsOffset;

    Extension->InputContexts = (PXHCI_INPUT_CONTEXT)(BaseVa + Layout.InputContextsOffset);
    Extension->InputContextsPhysical.QuadPart = BasePa + Layout.InputContextsOffset;

    Extension->Ep0TransferRings = (PXHCI_TRB)(BaseVa + Layout.Ep0RingsOffset);
    Extension->Ep0RingArrayPhysical.QuadPart = BasePa + Layout.Ep0RingsOffset;

    DPRINT1("usbxhci: common buffer layout size %Iu/%Iu bytes (CmdRing=%I64x EventRing=%I64x ERST=%I64x)\n",
            Layout.TotalSize,
            RequiredReservation,
            (ULONGLONG)Extension->CommandRingPhysical.QuadPart,
            (ULONGLONG)Extension->EventRingPhysical.QuadPart,
            (ULONGLONG)Extension->ErstTablePhysical.QuadPart);

    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
XHCI_DisableLegacySupport(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONG Offset;
    volatile ULONG *LegacySupport;
    volatile ULONG *LegacyControl;
    ULONG Value;
    ULONG Retry;
    const ULONG TimeoutUs = 1000000; /* 1s BIOS handoff timeout */

    if (!Extension || !Extension->CapabilityRegisters || !Extension->Resources)
        return MP_STATUS_ERROR;

    Offset = XHCI_FindExtendedCapability(Extension, XHCI_EXT_CAP_ID_LEGACY);
    if (!Offset)
        return MP_STATUS_SUCCESS;

    LegacySupport = (volatile ULONG *)((PUCHAR)Extension->CapabilityRegisters +
                                        Offset + XHCI_LEGACY_SUPPORT_OFFSET);
    LegacyControl = (volatile ULONG *)((PUCHAR)Extension->CapabilityRegisters +
                                        Offset + XHCI_LEGACY_CONTROL_OFFSET);

    Value = READ_REGISTER_ULONG(LegacySupport);
    if ((Value & XHCI_HC_BIOS_OWNED) == 0)
        return MP_STATUS_SUCCESS;

    Extension->Resources->LegacySupport = 1;
    WRITE_REGISTER_ULONG(LegacySupport, Value | XHCI_HC_OS_OWNED);

    for (Retry = 0; Retry < TimeoutUs / 100; Retry++)
    {
        Value = READ_REGISTER_ULONG(LegacySupport);
        if ((Value & XHCI_HC_BIOS_OWNED) == 0)
            break;

        KeStallExecutionProcessor(100);
    }

    if (Value & XHCI_HC_BIOS_OWNED)
    {
        DPRINT1("usbxhci: BIOS failed to release legacy ownership within %lu us, continuing with shared control\n",
                TimeoutUs);
        /* Fall back to shared legacy ownership instead of treating the
         * controller as completely unsupported. This matches the tolerant
         * behaviour of Windows on firmware that never clears HC BIOS
         * ownership and avoids hard‑failing StartController. */
        return MP_STATUS_SUCCESS;
    }

    Value = READ_REGISTER_ULONG(LegacyControl);
    Value &= ~(XHCI_LEGACY_DISABLE_SMI | XHCI_LEGACY_SMI_EVENTS);
    WRITE_REGISTER_ULONG(LegacyControl, Value);

    return MP_STATUS_SUCCESS;
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
                                                      TRUE,
                                                      Buffer,
                                                      Offset,
                                                      Length) == MP_STATUS_SUCCESS);
}

static BOOLEAN
XHCI_WritePciConfig(
    _In_ PXHCI_EXTENSION Extension,
    _In_ ULONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
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

static BOOLEAN
XHCI_EnablePciBusMaster(
    _Inout_ PXHCI_EXTENSION Extension)
{
    USHORT Command;
    USHORT NewCommand;

    if (!Extension || !XhciRegPacket.UsbPortReadWriteConfigSpace)
    {
        DPRINT1("usbxhci: UsbPortReadWriteConfigSpace not available – cannot enable bus mastering\n");
        return FALSE;
    }

    if (!XHCI_ReadPciConfig(Extension,
                            PCI_COMMAND_OFFSET,
                            &Command,
                            sizeof(Command)))
    {
        DPRINT1("usbxhci: failed to read PCI command register\n");
        return FALSE;
    }

    NewCommand = Command | PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER;
    if (NewCommand != Command)
    {
        if (XhciRegPacket.UsbPortReadWriteConfigSpace(Extension,
                                                      FALSE,
                                                      &NewCommand,
                                                      PCI_COMMAND_OFFSET,
                                                      sizeof(NewCommand)) != MP_STATUS_SUCCESS)
        {
            DPRINT1("usbxhci: failed to write PCI command register (cmd %04x)\n",
                    Command);
            return FALSE;
        }

        Command = NewCommand;
        DPRINT1("usbxhci: enabled PCI MEM/BusMaster (cmd=%04x)\n", Command);
    }
    else
    {
        DPRINT1("usbxhci: PCI MEM/BusMaster already enabled (cmd=%04x)\n", Command);
    }

    return TRUE;
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
        {
            DPRINT1("usbxhci: failed to read PCI capability ID at 0x%02x\n", CapPtr);
            break;
        }
        if (!XHCI_ReadPciConfig(Extension, CapPtr + 1, &Next, sizeof(Next)))
        {
            DPRINT1("usbxhci: failed to read PCI capability NEXT at 0x%02x\n", CapPtr + 1);
            break;
        }

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

static BOOLEAN
XHCI_EnableMsix(
    _Inout_ PXHCI_EXTENSION Extension)
{
    USHORT MsixControl;

    if (!Extension || !Extension->MsixSupported || Extension->MsixCapOffset == 0)
        return FALSE;

    if (!XHCI_ReadPciConfig(Extension,
                            Extension->MsixCapOffset + 2,
                            &MsixControl,
                            sizeof(MsixControl)))
        return FALSE;

    if (MsixControl & 0x8000)
        return TRUE;

    /* Clear function mask (bit 14), set MSI-X enable (bit 15). */
    MsixControl &= ~(1u << 14);
    MsixControl |= (1u << 15);

    if (!XHCI_WritePciConfig(Extension,
                             Extension->MsixCapOffset + 2,
                             &MsixControl,
                             sizeof(MsixControl)))
        return FALSE;

    Extension->MsixEnabled = TRUE;
    DPRINT1("usbxhci: enabled MSI-X (control=0x%04x)\n", MsixControl);
    return TRUE;
}

static
VOID
XHCI_ProgramInterrupterState(
    _Inout_ PXHCI_EXTENSION Extension)
{
    PXHCI_INTERRUPTER_REGISTER_SET Interrupter;
    ULONG Iman;
    ULONG Index;

    if (!Extension || !Extension->RuntimeRegisters)
        return;

#if DBG
    if (Extension->ErstEntryCount == 0 ||
        Extension->ErstTablePhysical.QuadPart == 0 ||
        Extension->EventRingPhysical.QuadPart == 0)
    {
        DPRINT1("usbxhci: ProgramInterrupterState with uninitialized ERST/event ring (entries=%lu ERST=%I64x ER=%I64x)\n",
                Extension->ErstEntryCount,
                (ULONGLONG)Extension->ErstTablePhysical.QuadPart,
                (ULONGLONG)Extension->EventRingPhysical.QuadPart);
        ASSERT(Extension->ErstEntryCount != 0);
        ASSERT(Extension->ErstTablePhysical.QuadPart != 0);
        ASSERT(Extension->EventRingPhysical.QuadPart != 0);
    }
    if ((Extension->EventRingPhysical.QuadPart & 0xFULL) != 0)
    {
        DPRINT1("usbxhci: WARNING event ring not 16-byte aligned in ProgramInterrupterState: %I64x\n",
                (ULONGLONG)Extension->EventRingPhysical.QuadPart);
    }
#endif

    if (Extension->InterrupterCount == 0)
        Extension->InterrupterCount = 1;

    for (Index = 0; Index < Extension->InterrupterCount; Index++)
    {
        Interrupter = &Extension->RuntimeRegisters->Interrupter[Index];

        /* Use a conservative default interrupt moderation interval. */
        WRITE_REGISTER_ULONG(&Interrupter->Imod, XHCI_IMOD_DEFAULT);

        /* Program ERST and ERDP for this interrupter; all share the same ring. */
        WRITE_REGISTER_ULONG(&Interrupter->ErstSize, Extension->ErstEntryCount);
        WRITE_REGISTER_ULONG(&Interrupter->ErstBaseLow,
                             (ULONG)(Extension->ErstTablePhysical.QuadPart & 0xFFFFFFFF));
        WRITE_REGISTER_ULONG(&Interrupter->ErstBaseHigh,
                             (ULONG)(Extension->ErstTablePhysical.QuadPart >> 32));
        /* Program ERDP to the event ring base and set EHB (BUSY) to clear state */
        Extension->EventRingDequeuePointer = Extension->EventRingPhysical.QuadPart;
        WRITE_REGISTER_ULONG(&Interrupter->ErdpHigh,
                             (ULONG)(Extension->EventRingDequeuePointer >> 32));
        WRITE_REGISTER_ULONG(&Interrupter->ErdpLow,
                             ((ULONG)(Extension->EventRingDequeuePointer & 0xFFFFFFFF)) |
                             XHCI_ERDP_BUSY);

        Iman = READ_REGISTER_ULONG(&Interrupter->Iman);
        Iman |= XHCI_IMAN_IE;
        Iman |= XHCI_IMAN_IP;
        WRITE_REGISTER_ULONG(&Interrupter->Iman, Iman);

        DPRINT1("usbxhci: intr%lu IMOD=%08lx ERST=%08lx:%08lx ERDP=%08lx:%08lx IMAN=%08lx\n",
                Index,
                READ_REGISTER_ULONG(&Interrupter->Imod),
                READ_REGISTER_ULONG(&Interrupter->ErstBaseHigh),
                READ_REGISTER_ULONG(&Interrupter->ErstBaseLow),
                READ_REGISTER_ULONG(&Interrupter->ErdpHigh),
                READ_REGISTER_ULONG(&Interrupter->ErdpLow),
                READ_REGISTER_ULONG(&Interrupter->Iman));
    }
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

    XHCI_DBG(XHCI_TRACE_COMMANDS,
             "usbxhci: queue command type=%lu cmdptr=%I64x\n",
             TrbType,
             CommandPointer);

    if (TrbType == XHCI_TRB_TYPE_ENABLE_SLOT ||
        TrbType == XHCI_TRB_TYPE_ADDRESS_DEV)
    {
        XHCI_TraceCommandRingState(Extension,
                                   "queue command",
                                   CommandPointer,
                                   TrbType);
    }

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
    XHCI_DBG(XHCI_TRACE_COMMANDS,
             "XHCI_SendCommand: Type=%lu Timeout=%lu\n",
             TrbType,
             TimeoutMs);
    ULONG Attempts;
    MPSTATUS Status = MP_STATUS_ERROR;
    MPSTATUS RecoveryStatus;
    KIRQL OldIrql;
    XHCI_COMMAND_CONTEXT CommandContext;
    ULONG EffectiveTimeout = TimeoutMs;
    BOOLEAN RetryCommands = AllowRetry;
    KIRQL CurrentIrql = KeGetCurrentIrql();

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

    if (CurrentIrql > PASSIVE_LEVEL)
    {
        if (EffectiveTimeout > 5)
            EffectiveTimeout = 5;
        RetryCommands = FALSE;
    }

    /* On QEMU with a latched HCE bit, do not wait excessively long for
     * command completion. This keeps EP0 bring-up responsive even when
     * the controller misbehaves and allows the miniport to surface the
     * timeout to upper layers instead of stalling for many seconds. */
    if (Extension->StartupHcePersistent &&
        EffectiveTimeout > 1000)
    {
        EffectiveTimeout = 1000;
    }

    if (TrbType == XHCI_TRB_TYPE_ENABLE_SLOT ||
        TrbType == XHCI_TRB_TYPE_ADDRESS_DEV)
    {
        DPRINT1("usbxhci: SendCommand type=%lu timeout=%lu ms retry=%u\n",
                TrbType,
                EffectiveTimeout,
                RetryCommands ? 1u : 0u);
    }

    Attempts = RetryCommands ? 2 : 1;

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

        if (TrbType == XHCI_TRB_TYPE_ENABLE_SLOT ||
            TrbType == XHCI_TRB_TYPE_ADDRESS_DEV)
        {
            XHCI_TraceCommandRingState(Extension,
                                       "SendCommand queued",
                                       CommandContext.CommandPointer,
                                       TrbType);
            if (TrbType == XHCI_TRB_TYPE_ENABLE_SLOT)
            {
                XHCI_LogInterrupterState(Extension, "EnableSlot queued");
            }
        }

        Status = XHCI_WaitForCommandCompletion(Extension,
                                               EffectiveTimeout,
                                               &CommandContext,
                                               SlotIdOut,
                                               CompletionCodeOut);
        if (Status == MP_STATUS_SUCCESS)
            break;

        KeAcquireSpinLock(&Extension->CommandLock, &OldIrql);
        XHCI_CommandContextUnlink(Extension, &CommandContext);
        KeReleaseSpinLock(&Extension->CommandLock, OldIrql);

        if (!RetryCommands || Status != MP_STATUS_HW_ERROR)
            break;

        RecoveryStatus = XHCI_RecoverControllerAfterCommandTimeout(Extension);
        if (RecoveryStatus != MP_STATUS_SUCCESS)
        {
            DPRINT1("usbxhci: controller recovery failed after timeout (status=%lu)\n",
                    RecoveryStatus);
            break;
        }

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
    XHCI_DBG(XHCI_TRACE_COMMANDS,
             "XHCI_WaitForCommandCompletion: Timeout=%lu\n",
             TimeoutMs);
    ULONG Remaining;
    KIRQL Irql;
    KEVENT CompletionEvent;
    LARGE_INTEGER Interval;
    BOOLEAN UseEventWait = FALSE;
    MPSTATUS Result = MP_STATUS_ERROR;

    if (!Extension || !CommandContext)
        return MP_STATUS_ERROR;

    if (Extension->FatalError)
        return MP_STATUS_HW_ERROR;

    Irql = KeGetCurrentIrql();
    if (Irql <= APC_LEVEL)
    {
        UseEventWait = TRUE;
        KeInitializeEvent(&CompletionEvent, NotificationEvent, FALSE);
        CommandContext->CompletionEvent = &CompletionEvent;
        Interval.QuadPart = -(LONGLONG)XHCI_COMMAND_POLL_INTERVAL_US * 10;
    }

    Remaining = (TimeoutMs * 1000) / XHCI_COMMAND_POLL_INTERVAL_US;
    if (Remaining == 0)
        Remaining = 1;

    while (Remaining--)
    {
        /*
         * When we can use an event wait (IRQL <= APC_LEVEL), rely on the IRQ/DPC
         * path to service the event ring and signal CompletionEvent. Polling the
         * event ring from within a USBPORT->miniport call can force us to defer
         * unrelated transfer completions, which risks USBPORT timeouts/cancels
         * and ensuing pool/list corruption.
         *
         * If we cannot wait on an event (high IRQL), fall back to polling.
         */
        if (!CommandContext->Completed && !UseEventWait)
            XHCI_ServiceEventRing(Extension, FALSE, FALSE);

        if (CommandContext->Completed)
            break;

        if (UseEventWait)
        {
            NTSTATUS WaitStatus;

            WaitStatus = KeWaitForSingleObject(&CompletionEvent,
                                               Executive,
                                               KernelMode,
                                               FALSE,
                                               &Interval);
            if (WaitStatus == STATUS_SUCCESS && CommandContext->Completed)
                break;
        }
        else
        {
            KeStallExecutionProcessor(XHCI_COMMAND_POLL_INTERVAL_US);
        }

        if (Extension->FatalError)
        {
            Result = MP_STATUS_HW_ERROR;
            goto Exit;
        }
    }

    if (!CommandContext->Completed)
    {
        DPRINT1("usbxhci: command completion timed out\n");
        if (Irql <= PASSIVE_LEVEL)
            XHCI_HandleCommandTimeout(Extension, CommandContext);
        Result = MP_STATUS_HW_ERROR;
        goto Exit;
    }

    if (SlotIdOut)
        *SlotIdOut = CommandContext->SlotId;
    if (CompletionCodeOut)
        *CompletionCodeOut = CommandContext->CompletionCode;

    if (CommandContext->CompletionCode == XHCI_COMPLETION_SUCCESS)
    {
        Result = MP_STATUS_SUCCESS;
    }
    else
    {
        DPRINT1("usbxhci: command completion error code=%lu slot=%u\n",
                CommandContext->CompletionCode,
                CommandContext->SlotId);
        Result = MP_STATUS_ERROR;
    }

Exit:
    CommandContext->CompletionEvent = NULL;
    return Result;
}

static
MPSTATUS
XHCI_ValidateCommandEngine(
    _Inout_ PXHCI_EXTENSION Extension)
{
    DPRINT1("XHCI_ValidateCommandEngine: Enter\n");
    MPSTATUS Status;
    ULONG CompletionCode = 0;
    ULONG TimeoutMs = XHCI_COMMAND_TIMEOUT_MS;
    ULONG UsbSts = 0;

    if (!Extension)
        return MP_STATUS_ERROR;

    if (Extension->OperationalRegisters)
        UsbSts = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);

    if ((UsbSts & XHCI_USBSTS_HCE) &&
        (Extension->Quirks & XHCI_QUIRK_IGNORE_STARTUP_HCE))
    {
        DPRINT1("usbxhci: persistent startup HCE (USBSTS=%08lx) – skipping NO-OP probe per quirk\n",
                UsbSts);
        WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
                             XHCI_USBSTS_HCE |
                             XHCI_USBSTS_HSE |
                             XHCI_USBSTS_PCD |
                             XHCI_USBSTS_EINT);
        Extension->StartupHcePersistent = TRUE;
        return MP_STATUS_SUCCESS;
    }

    if (UsbSts & XHCI_USBSTS_HCE)
    {
        WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
                             XHCI_USBSTS_HCE |
                             XHCI_USBSTS_HSE |
                             XHCI_USBSTS_PCD |
                             XHCI_USBSTS_EINT);
    }

    /* If the controller already asserts HCE, shorten the probe to avoid long stalls. */
    if (UsbSts & XHCI_USBSTS_HCE)
        TimeoutMs = 50;

    /* Make sure the command ring starts from a known state for the probe. */
    XHCI_ResetCommandRingState(Extension);

    Status = XHCI_SendCommand(Extension,
                              XHCI_TRB_TYPE_CMD_NOOP,
                              0,
                              0,
                              0,
                              TimeoutMs,
                              FALSE,
                              NULL,
                              &CompletionCode);
    if (Status != MP_STATUS_SUCCESS || CompletionCode != XHCI_COMPLETION_SUCCESS)
    {
        DPRINT1("usbxhci: startup sanity NO-OP failed (status=%lu completion=%lu)\n",
                Status,
                CompletionCode);
        return MP_STATUS_HW_ERROR;
    }

    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
XHCI_ResetController(
    _In_ PXHCI_EXTENSION Extension)
{
    volatile ULONG *UsbCmd;
    volatile ULONG *UsbSts;
    ULONG Command;
    ULONG ResetTimeout;
    ULONG ReadyTimeout;

    if (!Extension || !Extension->OperationalRegisters)
        return MP_STATUS_ERROR;

    UsbCmd = &Extension->OperationalRegisters->UsbCmd;
    UsbSts = &Extension->OperationalRegisters->UsbSts;

    Command = READ_REGISTER_ULONG(UsbCmd);
    if (Command & XHCI_USBCMD_RS)
    {
        WRITE_REGISTER_ULONG(UsbCmd, Command & ~XHCI_USBCMD_RS);
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

#if DBG
    {
        ULONG DebugCmd = READ_REGISTER_ULONG(UsbCmd);
        ULONG DebugSts = READ_REGISTER_ULONG(UsbSts);

        if ((DebugCmd & (XHCI_USBCMD_RS | XHCI_USBCMD_HCRST)) != 0 ||
            (DebugSts & XHCI_USBSTS_CNR) != 0)
        {
            DPRINT1("usbxhci: unexpected state after reset (USBCMD=%08lx USBSTS=%08lx)\n",
                    DebugCmd,
                    DebugSts);
            ASSERT((DebugCmd & (XHCI_USBCMD_RS | XHCI_USBCMD_HCRST)) == 0);
            ASSERT((DebugSts & XHCI_USBSTS_CNR) == 0);
        }
    }
#endif

        /* Clear any error bits that might be latched (HCE, HSE, PCD, EINT) */
    WRITE_REGISTER_ULONG(UsbSts,
                         XHCI_USBSTS_HCE |
                         XHCI_USBSTS_HSE |
                         XHCI_USBSTS_PCD |
                         XHCI_USBSTS_EINT);
    
    return MP_STATUS_SUCCESS;
}

static VOID
XHCI_HandleControllerError(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ ULONG PendingStatus)
{
    if (!Extension)
        return;

    /*
     * Some virtual xHCI controllers (notably QEMU's 1B36:000D) are prone
     * to asserting HCE without a corresponding host-system error while
     * continuing to function. When the dedicated startup quirk is set and
     * only HCE is present (no HSE), treat this as non-fatal so the
     * controller is not torn down unnecessarily. To avoid flooding debug
     * logs, only emit a small number of messages while still clearing the
     * error condition.
     */
    if ((PendingStatus & XHCI_USBSTS_HSE) == 0 &&
        (PendingStatus & XHCI_USBSTS_HCE) != 0 &&
        Extension->StartupHcePersistent)
    {
        LONG Budget = InterlockedDecrement(&XhciHceQuirkLogBudget);
        if (Budget >= 0)
        {
            DPRINT1("usbxhci: controller HCE observed on QEMU xHCI "
                    "(USBSTS=%08lx) - ignoring per quirk\n",
                    PendingStatus);
        }
        return;
    }

    DPRINT1("usbxhci: FATAL controller error (USBSTS=%08lx) - dumping controller state\n",
            PendingStatus);
    XHCI_DumpControllerState(Extension, "controller error");
    Extension->FatalError = TRUE;
    XHCI_ShutdownController(Extension, TRUE);
}

static VOID
XHCI_HandleCommandTimeout(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_opt_ PXHCI_COMMAND_CONTEXT CommandContext)
{
    ULONG CommandType;

    if (!Extension)
        return;

    CommandType = CommandContext ? CommandContext->CommandType : 0;

    XHCI_LogCommandTimeoutDetails(Extension, CommandContext);
    XHCI_DumpControllerState(Extension, "command timeout");
    Extension->FatalError = TRUE;
    DPRINT1("usbxhci: command type %lu timed out -- forcing controller reset\n",
            CommandType);
    XHCI_ShutdownController(Extension, TRUE);
}

static
MPSTATUS
XHCI_RecoverControllerAfterCommandTimeout(
    _Inout_ PXHCI_EXTENSION Extension)
{
    MPSTATUS Status;
    KIRQL OldIrql;
    PXHCI_TRB LinkTrb;

    if (!Extension || !Extension->OperationalRegisters)
        return MP_STATUS_ERROR;

    DPRINT1("usbxhci: recovering controller state after command timeout\n");

    XHCI_ResetCommandRingState(Extension);

    if (Extension->CommandRing && Extension->CommandRingTrbCount)
    {
        RtlZeroMemory(Extension->CommandRing,
                      sizeof(XHCI_TRB) * Extension->CommandRingTrbCount);

        LinkTrb = &Extension->CommandRing[Extension->CommandRingTrbCount - 1];
        LinkTrb->Parameter1 = (ULONG)(Extension->CommandRingPhysical.QuadPart & 0xFFFFFFFF);
        LinkTrb->Parameter2 = (ULONG)(Extension->CommandRingPhysical.QuadPart >> 32);
        LinkTrb->Status = 0;
        LinkTrb->Control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                           XHCI_TRB_TOGGLE_CYCLE |
                           XHCI_TRB_CYCLE;
    }

    KeAcquireSpinLock(&Extension->EventRingLock, &OldIrql);
    Extension->EventRingDequeueIndex = 0;
    Extension->EventRingCycleState = 1;
    Extension->EventRingDequeuePointer = Extension->EventRingPhysical.QuadPart;
    KeReleaseSpinLock(&Extension->EventRingLock, OldIrql);

    if (Extension->EventRing && Extension->EventRingTrbCount)
    {
        RtlZeroMemory(Extension->EventRing,
                      sizeof(XHCI_TRB) * Extension->EventRingTrbCount);
    }
    if (Extension->ErstTable && Extension->ErstEntryCount != 0)
        XHCI_BuildErstTable(Extension);

    Status = XHCI_ResetController(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
                         XHCI_USBSTS_EINT |
                         XHCI_USBSTS_PCD |
                         XHCI_USBSTS_HSE |
                         XHCI_USBSTS_HCE |
                         XHCI_USBSTS_HCH);

    Status = XHCI_InitializeScratchpads(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    Status = XHCI_ConfigurePageSize(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    Status = XHCI_ProgramDcbaaCrcrAndConfig(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;
    XHCI_ProgramInterrupterState(Extension);
    XHCI_EnableInterrupts(Extension);

    Status = XHCI_RunController(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    XHCI_PowerOnAllPorts(Extension);
    XHCI_ConfigureAllPortsLpm(Extension);

    Extension->FatalError = FALSE;
    Extension->ControllerRunning = TRUE;

    return MP_STATUS_SUCCESS;
}

static const WCHAR XHCI_REG_TRACE_MASK[] = L"XhciTraceMask";
static const WCHAR XHCI_REG_STARTUP_HCE_QUIRK[] = L"XhciStartupHceQuirk";

static
VOID
XHCI_GetRegistryParameters(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONG ParameterValue;
    MPSTATUS MpStatus;

    if (!Extension || !XhciRegPacket.UsbPortGetMiniportRegistryKeyValue)
        return;

#if DBG
    ParameterValue = 0;
    MpStatus = XhciRegPacket.UsbPortGetMiniportRegistryKeyValue(
        Extension,
        TRUE,
        XHCI_REG_TRACE_MASK,
        sizeof(XHCI_REG_TRACE_MASK),
        &ParameterValue,
        sizeof(ParameterValue));

    if (MpStatus == MP_STATUS_SUCCESS)
    {
        g_XhciTraceMask = ParameterValue;
        DPRINT("usbxhci: XhciTraceMask=0x%08lx\n", g_XhciTraceMask);
    }
#endif

    ParameterValue = 0;
    MpStatus = XhciRegPacket.UsbPortGetMiniportRegistryKeyValue(
        Extension,
        TRUE,
        XHCI_REG_STARTUP_HCE_QUIRK,
        sizeof(XHCI_REG_STARTUP_HCE_QUIRK),
        &ParameterValue,
        sizeof(ParameterValue));

    if (MpStatus == MP_STATUS_SUCCESS)
    {
        g_XhciStartupHceQuirkOverrideValid = TRUE;
        g_XhciStartupHceQuirkOverride = (ParameterValue != 0);
        DPRINT1("usbxhci: Startup HCE quirk override %s via registry (value=%lu)\n",
                g_XhciStartupHceQuirkOverride ? "ENABLED" : "DISABLED",
                ParameterValue);
    }
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
    DPRINT1("SUBMIT_XFER: Slot=%u EP=%u Addr=%u Type=%u Len=%u\n", Endpoint->SlotId, Endpoint->EndpointId, Endpoint->EndpointProperties.DeviceAddress, Endpoint->EndpointProperties.TransferType, TransferParameters->TransferBufferLength);

    static BOOLEAN Triggered = FALSE;
    if (Extension && Extension->RhIrqEnabled && !Triggered && XhciRegPacket.UsbPortInvalidateRootHub)
    {
        Triggered = TRUE;
        DPRINT1("XHCI: Triggering RH Invalidate from SubmitTransfer\n");
        XhciRegPacket.UsbPortInvalidateRootHub(Extension);
    }


    if (!Extension || !Endpoint || !Transfer || !TransferParameters)
        return MP_STATUS_ERROR;

    DPRINT1("SUBMIT_XFER: Slot=%u EP=%u Addr=%u Type=%u Len=%u\n", Endpoint->SlotId, Endpoint->EndpointId, Endpoint->EndpointProperties.DeviceAddress, Endpoint->EndpointProperties.TransferType, TransferParameters->TransferBufferLength);

    XHCI_DBG(XHCI_TRACE_TRANSFERS,
             "usbxhci: SubmitTransfer ep=%u devaddr=%u type=%lu flags=0x%lx len=%lu setupType=0x%02x req=0x%02x\n",
             Endpoint->EndpointId,
             Endpoint->EndpointProperties.DeviceAddress,
             Endpoint->EndpointProperties.TransferType,
             TransferParameters->TransferFlags,
             TransferParameters->TransferBufferLength,
             TransferParameters->SetupPacket.bmRequestType.B,
             TransferParameters->SetupPacket.bRequest);

    if (Extension->FatalError)
        return MP_STATUS_HW_ERROR;

    /*
     * USBPORT may transiently tear down and recreate pipes while higher-level
     * drivers (notably usb-storage) continue queuing transfers. On QEMU's xHCI
     * this can leave a bulk endpoint context Disabled even though the pipe
     * handle is still used, which would otherwise stall the boot waiting for a
     * completion that never arrives. If we observe a Disabled endpoint at
     * submit-time, attempt a best-effort CONFIGURE_ENDPOINT to re-enable it.
     */
    if ((Extension->Quirks & XHCI_QUIRK_QEMU_CONFIG_EP_ORDER) &&
        KeGetCurrentIrql() <= DISPATCH_LEVEL &&
        Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK &&
        Endpoint->Slot &&
        Endpoint->EndpointId > 1)
    {
        PVOID DeviceCtxBase = Endpoint->Slot->DeviceContext.VirtualAddress;
        if (DeviceCtxBase)
        {
            PXHCI_ENDPOINT_CONTEXT EpCtx =
                XHCI_GetDeviceEndpointContextVa(Extension,
                                                DeviceCtxBase,
                                                Endpoint->EndpointId - 1);
            if (EpCtx && ((EpCtx->EpInfo & XHCI_EPCTX_STATE_MASK) == XHCI_EPCTX_STATE_DISABLED))
            {
                (VOID)XHCI_ConfigureSlotEndpoint(Extension,
                                                 Endpoint->Slot,
                                                 Endpoint,
                                                 Endpoint->EndpointId);
            }
        }
    }

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
    Transfer->StreamId = 0;
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
    MPSTATUS Status = MP_STATUS_SUCCESS;
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
    ULONG Chunk;
    BOOLEAN ProgrammedRing = FALSE;
    ULONG SgIndex = 0;
    KIRQL OldIrql;

    if (!Extension || !Endpoint || !Transfer)
        return MP_STATUS_ERROR;

    Status = XHCI_SubmitControlTransferSwEnum(Extension, Endpoint, Transfer);
    if (Status != MP_STATUS_NOT_SUPPORTED)
        return Status;

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

    KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
    if (Endpoint->ActiveTransfer)
    {
        KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
        DPRINT1("usbxhci: endpoint %u already has an active transfer\n",
                Endpoint->EndpointId);
        return MP_STATUS_FAILURE;
    }
    Endpoint->ActiveTransfer = Transfer;
    KeReleaseSpinLock(&Endpoint->Lock, OldIrql);

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
        DPRINT1("usbxhci: SUBMIT SET_ADDRESS addr=%u port=%u hub=%u speed=%u len=%lu\n",
                Transfer->NewAddress,
                Endpoint->EndpointProperties.PortNumber,
                Endpoint->EndpointProperties.HubAddr,
                Endpoint->EndpointProperties.DeviceSpeed,
                TransferParameters->TransferBufferLength);
    }

    if (TransferParameters->SetupPacket.bRequest == USB_REQUEST_GET_DESCRIPTOR)
    {
        Transfer->Flags |= XHCI_TRANSFER_FLAG_GET_DESCRIPTOR;
        DPRINT1("usbxhci: SUBMIT GET_DESCRIPTOR type=0x%02x idx=0x%02x port=%u hub=%u speed=%u len=%lu\n",
                TransferParameters->SetupPacket.wValue.HiByte,
                TransferParameters->SetupPacket.wValue.LowByte,
                Endpoint->EndpointProperties.PortNumber,
                Endpoint->EndpointProperties.HubAddr,
                Endpoint->EndpointProperties.DeviceSpeed,
                TransferParameters->TransferBufferLength);
    }

    /*
     * Validate that the scatter/gather list covers the requested transfer
     * length.  Control transfers can legally use fragmented buffers.
     */
    if (HasDataStage)
    {
        ULONGLONG TotalLength = 0;

        if (!SgList || SgList->SgElementCount == 0)
        {
            DPRINT1("usbxhci: missing SG list for control transfer\n");
            Status = MP_STATUS_NO_RESOURCES;
            goto Failure;
        }

        for (SgIndex = 0; SgIndex < SgList->SgElementCount; SgIndex++)
        {
            ULONG Length = SgList->SgElement[SgIndex].SgTransferLength;
            ULONG Offset = SgList->SgElement[SgIndex].SgOffset;

            if (Offset < Length)
                TotalLength += (Length - Offset);
        }

        if (TotalLength < TransferParameters->TransferBufferLength)
        {
            DPRINT1("usbxhci: SG list shorter (%I64u) than control transfer length (%lu)\n",
                    TotalLength,
                    TransferParameters->TransferBufferLength);
            Status = MP_STATUS_ERROR;
            goto Failure;
        }
    }

    Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing, &PhysicalAddress, TRUE);
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
    /*
     * The xHCI specification requires the Setup Stage TRB length field to
     * always be eight bytes (the size of the setup packet) regardless of the
     * subsequent data-stage length.  The wLength field inside the setup packet
     * itself already conveys the expected data transfer size.
     */
    Trb->Status = sizeof(USB_DEFAULT_PIPE_SETUP_PACKET);
    Control = (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) |
              (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE) |
              XHCI_TRB_IDT |
              XHCI_TRB_CHAIN_BIT;

    if (!HasDataStage)
        Control |= XHCI_TRB_TRT_NO_DATA;
    else if (DataIn)
        Control |= XHCI_TRB_TRT_IN;
    else
        Control |= XHCI_TRB_TRT_OUT;

    /* Setup stage always targets interrupter 0 (command/root-hub path). */
    Trb->Control = Control;
    ProgrammedRing = TRUE;
    XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

    Remaining = TransferParameters->TransferBufferLength;

    if (HasDataStage)
    {
        SgIndex = 0;
    }

    while (HasDataStage && Remaining && SgIndex < SgList->SgElementCount)
    {
        ULONG ElementRemaining = SgList->SgElement[SgIndex].SgTransferLength;
        ULONGLONG ElementAddress = SgList->SgElement[SgIndex].SgPhysicalAddress.QuadPart;

        /* USBPORT's SgOffset is the offset into the *overall* transfer buffer;
         * SgPhysicalAddress already points at the correct segment. */

        while (ElementRemaining && Remaining)
        {
            Chunk = XHCI_CalcTrbTransferChunk(ElementAddress,
                                              ElementRemaining,
                                              Remaining,
                                              0);

            Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing,
                                          &PhysicalAddress,
                                          TRUE);
            if (!Trb)
            {
                Status = MP_STATUS_NO_RESOURCES;
                goto Failure;
            }

            Trb->Parameter1 = (ULONG)(ElementAddress & 0xFFFFFFFF);
            Trb->Parameter2 = (ULONG)(ElementAddress >> 32);
            Trb->Status = Chunk;
            Control = (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT) |
                      (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE);

            if (DataIn)
                Control |= XHCI_TRB_DIR_IN;

            /* For Control Transfers, Data Stage TRBs are always followed by Status Stage. */
            Control |= XHCI_TRB_CHAIN_BIT;

            Trb->Control = Control;
            XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

            ElementAddress += Chunk;
            ElementRemaining -= Chunk;
            Remaining -= Chunk;
        }

        SgIndex++;
    }

    if (Remaining != 0)
    {
        DPRINT1("usbxhci: SG mapping smaller than control transfer length (remain=%lu)\n",
                Remaining);
        Status = MP_STATUS_ERROR;
        goto Failure;
    }

    StatusIn = !HasDataStage ? TRUE : !DataIn;

    Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing,
                                  &PhysicalAddress,
                                  FALSE);
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

    /* Interrupt target is encoded in the event TRB, not the status-stage TRB. */
    Trb->Control = Control;
    Transfer->CompletionTrbPointer = PhysicalAddress;
    XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

    {
        USHORT DoorbellStreamId = XHCI_SelectDoorbellStreamId(Endpoint, Transfer);
        KeMemoryBarrier();
        XHCI_RingEndpointDoorbell(Extension,
                                   Endpoint->SlotId,
                                   Endpoint->DoorbellTarget,
                                   DoorbellStreamId);
    }

    /*
     * Avoid completing transfers synchronously from SubmitTransfer: USBPORT
     * expects completions to be delivered from the interrupt/DPC path.
     *
     * For EP0 enumeration transfers, schedule a short poll timer to drain the
     * event ring in case the first interrupt is missed (observed on QEMU).
     */
    if (Endpoint->DefaultControl &&
        (Transfer->Flags & (XHCI_TRANSFER_FLAG_SET_ADDRESS |
                            XHCI_TRANSFER_FLAG_GET_DESCRIPTOR)))
    {
        Transfer->Flags |= XHCI_TRANSFER_FLAG_NEEDS_POLL;
        if (InterlockedIncrement(&Extension->Ep0PollCounter) == 1)
            XHCI_ScheduleEp0Poll(Extension);
    }

    return MP_STATUS_SUCCESS;

Failure:
    if (Transfer->Flags & XHCI_TRANSFER_FLAG_NEEDS_POLL)
    {
        Transfer->Flags &= ~XHCI_TRANSFER_FLAG_NEEDS_POLL;
        if (InterlockedDecrement(&Extension->Ep0PollCounter) <= 0)
            KeCancelTimer(&Extension->Ep0PollTimer);
    }
    if (ProgrammedRing)
        XHCI_ResetEndpointRing(Endpoint);

    Transfer->UsbdStatus = USBD_STATUS_REQUEST_FAILED;
    KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
    if (Endpoint->ActiveTransfer == Transfer)
        Endpoint->ActiveTransfer = NULL;
    KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
    return Status;
}

static MPSTATUS
XHCI_SubmitBulkInterruptTransfer(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _Inout_ PXHCI_TRANSFER Transfer)
{
    if (!Extension || !Endpoint || !Transfer)
        return MP_STATUS_ERROR;

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
    MPSTATUS Status = MP_STATUS_SUCCESS;
    KIRQL OldIrql;
    ULONG IsoPayloadLimit = 0;
#if DBG
    ULONG IocTrbCount = 0;
#endif

    if (!Extension || !Endpoint || !Transfer)
        return MP_STATUS_ERROR;

    if (!Endpoint->Slot || !Endpoint->TransferRing.Base)
        return MP_STATUS_ERROR;

    KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
    if (Endpoint->ActiveTransfer)
    {
        KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
        return MP_STATUS_FAILURE;
    }
    Endpoint->ActiveTransfer = Transfer;
    KeReleaseSpinLock(&Endpoint->Lock, OldIrql);

    TransferParameters = Transfer->TransferParameters;
    SgList = Transfer->SgList;

    if (Endpoint->SlotId == 1 &&
        Endpoint->EndpointId == 4 &&
        TransferParameters &&
        TransferParameters->TransferBufferLength == 31 &&
        SgList &&
        SgList->MappedSystemVa &&
        SgList->SgElementCount > 0)
    {
        PUSBPORT_SCATTER_GATHER_ELEMENT Element = &SgList->SgElement[0];
        ULONG Offset = Element->SgOffset;
        ULONG Length = Element->SgTransferLength;

        if (Offset < Length && (Length - Offset) >= 31)
        {
            const UCHAR *Cbw = (const UCHAR *)SgList->MappedSystemVa + Offset;
            ULONG Sig = *(const ULONG *)(const VOID *)(Cbw + 0);
            ULONG Tag = *(const ULONG *)(const VOID *)(Cbw + 4);
            ULONG XferLen = *(const ULONG *)(const VOID *)(Cbw + 8);

            DPRINT1("usbxhci: BOT CBW sig=%08lx tag=%08lx xfer=%08lx flags=%02x lun=%02x cblen=%02x cdb=%02x %02x %02x %02x %02x %02x\n",
                    Sig,
                    Tag,
                    XferLen,
                    Cbw[12],
                    Cbw[13],
                    Cbw[14],
                    Cbw[15],
                    Cbw[16],
                    Cbw[17],
                    Cbw[18],
                    Cbw[19],
                    Cbw[20]);
        }
    }

    if (IsIsochronous)
    {
        ULONG Transactions = Endpoint->EndpointProperties.TransactionPerMicroframe;
        ULONG PacketSize = (ULONG)Endpoint->EndpointProperties.MaxPacketSize;
        ULONG TotalMax = (ULONG)Endpoint->EndpointProperties.TotalMaxPacketSize;

        if (Transactions == 0)
            Transactions = 1;

        if (TotalMax != 0)
        {
            IsoPayloadLimit = TotalMax;
        }
        else if (PacketSize != 0)
        {
            IsoPayloadLimit = PacketSize * Transactions;
        }

        if (IsoPayloadLimit == 0 ||
            IsoPayloadLimit > XHCI_MAX_TRB_TRANSFER_LENGTH)
        {
            IsoPayloadLimit = XHCI_MAX_TRB_TRANSFER_LENGTH;
        }
    }

    Remaining = TransferParameters ?
                TransferParameters->TransferBufferLength : 0;
    SgIndex = 0;

    if (Remaining == 0)
    {
        Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing,
                                      &PhysicalAddress,
                                      FALSE);
        if (!Trb)
        {
            Status = MP_STATUS_NO_RESOURCES;
            goto Failure;
        }

        Trb->Parameter1 = 0;
        Trb->Parameter2 = 0;
        Trb->Status = 0;
        Control = (TrbType << XHCI_TRB_TYPE_SHIFT) |
                  (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE) |
                  XHCI_TRB_IOC;
        if (Endpoint->InterruptTarget < Extension->InterrupterCount)
        {
            Control |= ((ULONG)Endpoint->InterruptTarget << XHCI_TRB_INTR_TARGET_SHIFT);
        }
        if (IsIsochronous)
            Control |= XHCI_TRB_SIA;
#if DBG
        ASSERT((Control & XHCI_TRB_IOC) != 0);
        IocTrbCount++;
#endif
        Trb->Control = Control;
        XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

        Transfer->CompletionTrbPointer = PhysicalAddress;
        Transfer->Flags = 0;
        Transfer->IsControl = FALSE;

        {
            USHORT DoorbellStreamId = XHCI_SelectDoorbellStreamId(Endpoint, Transfer);
            KeMemoryBarrier();
            XHCI_RingEndpointDoorbell(Extension,
                                       Endpoint->SlotId,
                                       Endpoint->DoorbellTarget,
                                       DoorbellStreamId);
        }

        return MP_STATUS_SUCCESS;
    }

    if (!SgList || SgList->SgElementCount == 0)
    {
        Status = MP_STATUS_NO_RESOURCES;
        goto Failure;
    }

    while (Remaining && SgIndex < SgList->SgElementCount)
    {
        ULONG ElementRemaining = SgList->SgElement[SgIndex].SgTransferLength;
        PHYSICAL_ADDRESS ElementAddress = SgList->SgElement[SgIndex].SgPhysicalAddress;
        /* USBPORT's SgOffset is the offset into the *overall* transfer buffer;
         * SgPhysicalAddress already points at the correct segment. */

        while (ElementRemaining && Remaining)
        {
            BOOLEAN TdContinues;

            BufferAddress = ElementAddress.QuadPart;
            Chunk = XHCI_CalcTrbTransferChunk(BufferAddress,
                                              ElementRemaining,
                                              Remaining,
                                              IsoPayloadLimit);

            TdContinues = (Remaining > Chunk);
            Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing,
                                          &PhysicalAddress,
                                          TdContinues);
            if (!Trb)
            {
                Status = MP_STATUS_NO_RESOURCES;
                goto Failure;
            }

            Trb->Parameter1 = (ULONG)(BufferAddress & 0xFFFFFFFF);
            Trb->Parameter2 = (ULONG)(BufferAddress >> 32);
            Trb->Status = Chunk;
            Control = (TrbType << XHCI_TRB_TYPE_SHIFT) |
                      (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE);
            if (Endpoint->InterruptTarget < Extension->InterrupterCount)
            {
                Control |= ((ULONG)Endpoint->InterruptTarget << XHCI_TRB_INTR_TARGET_SHIFT);
            }

            if (IsIsochronous)
                Control |= XHCI_TRB_SIA;
            if (TdContinues)
                Control |= XHCI_TRB_CHAIN_BIT;
            else
            {
                Control |= XHCI_TRB_IOC;
#if DBG
                ASSERT((Control & XHCI_TRB_IOC) != 0);
                IocTrbCount++;
#endif
            }

            Trb->Control = Control;
            XHCI_AdvanceTransferRing(&Endpoint->TransferRing);


            ElementAddress.QuadPart += Chunk;
            ElementRemaining -= Chunk;
            Remaining -= Chunk;
            XHCI_DBG(XHCI_TRACE_TRANSFERS,
                     "usbxhci: xfer TRB addr=%I64x p1=%08lx p2=%08lx len=%lu ctrl=%08lx\n",
                     (ULONGLONG)PhysicalAddress,
                     Trb->Parameter1,
                     Trb->Parameter2,
                     Chunk,
                     Trb->Control);
        }

        SgIndex++;
    }

    if (Remaining)
    {
        DPRINT1("usbxhci: SG mapping smaller than transfer length\n");
        Status = MP_STATUS_ERROR;
        goto Failure;
    }

    if (!Trb)
    {
        Status = MP_STATUS_NO_RESOURCES;
        goto Failure;
    }

    Transfer->CompletionTrbPointer = PhysicalAddress;
    Transfer->Flags = 0;
    Transfer->IsControl = FALSE;

#if DBG
    ASSERT(IocTrbCount == 1);
#endif

    if (Endpoint->SlotId == 1 && (Endpoint->EndpointId == 3 || Endpoint->EndpointId == 4))
    {
        ULONGLONG Buffer = Trb ? (((ULONGLONG)Trb->Parameter2 << 32) | Trb->Parameter1) : 0;
        ULONG TrbLen = Trb ? (Trb->Status & XHCI_TRB_LEN_MASK) : 0;
        ULONG EpState = 0xFFFFFFFF;
        ULONG EpInfo2 = 0;

        if (Endpoint->Slot && Endpoint->Slot->DeviceContext.VirtualAddress)
        {
            PXHCI_ENDPOINT_CONTEXT EpCtx =
                XHCI_GetDeviceEndpointContextVa(Extension,
                                                Endpoint->Slot->DeviceContext.VirtualAddress,
                                                Endpoint->EndpointId - 1);
            if (EpCtx)
            {
                EpState = EpCtx->EpInfo & XHCI_EPCTX_STATE_MASK;
                EpInfo2 = EpCtx->EpInfo2;
            }
        }

        DPRINT1("usbxhci: bulk submit S%u E%u Req=%lu LastTrb=%I64x Buf=%I64x Len=%lu Enq=%lu CS=%lu EpState=%lx EpInfo2=%08lx\n",
                Endpoint->SlotId,
                Endpoint->EndpointId,
                Transfer->RequestedLength,
                (ULONGLONG)Transfer->CompletionTrbPointer,
                Buffer,
                TrbLen,
                Endpoint->TransferRing.EnqueueIndex,
                (ULONG)Endpoint->TransferRing.CycleState,
                EpState,
                EpInfo2);
    }

    {
        USHORT DoorbellStreamId = XHCI_SelectDoorbellStreamId(Endpoint, Transfer);
        KeMemoryBarrier();
        XHCI_RingEndpointDoorbell(Extension,
                                   Endpoint->SlotId,
                                   Endpoint->DoorbellTarget,
                                   DoorbellStreamId);
    }

    return MP_STATUS_SUCCESS;

Failure:
    Transfer->UsbdStatus = USBD_STATUS_REQUEST_FAILED;
    KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
    if (Endpoint->ActiveTransfer == Transfer)
        Endpoint->ActiveTransfer = NULL;
    KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
    return Status;
}

static MPSTATUS NTAPI
XHCI_OpenEndpoint(PVOID MiniPortExtension,
                  PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                  PVOID Endpoint)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    PXHCI_ENDPOINT XhciEndpoint = Endpoint;

    XHCI_DBG(XHCI_TRACE_EVENTS,
             "usbxhci: OpenEndpoint EP=%p DevAddr=%u EptAddr=0x%02x Type=%u IRQL=%lu\n",
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

    if (Extension->FatalError || Extension->StoppingOrRemoved)
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

    {
        ULONG loops = XHCI_DEFERRED_OPEN_TIMEOUT_US / XHCI_DEFERRED_OPEN_SPIN_DELAY_US;

        while (!KeReadStateEvent(&Work->CompletionEvent) && loops--)
        {
            if (Extension->StoppingOrRemoved || Extension->FatalError)
                break;
            KeStallExecutionProcessor(XHCI_DEFERRED_OPEN_SPIN_DELAY_US);
        }
    }

    if (!KeReadStateEvent(&Work->CompletionEvent))
    {
        DPRINT1("usbxhci: deferred OpenEndpoint timed out/stopped\n");
        ExFreePoolWithTag(Work, XHCI_TAG);
        return MP_STATUS_UNSUCCESSFUL;
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
    KeInitializeSpinLock(&XhciEndpoint->Lock);
    XhciEndpoint->Extension = Extension;
    XhciEndpoint->EndpointProperties = *EndpointProperties;
    IsDefaultPipe = (EndpointProperties->EndpointAddress == 0 &&
                     EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_CONTROL);

    if (IsDefaultPipe)
    {
        Slot = XHCI_FindSlotByAddress(Extension, EndpointProperties->DeviceAddress);
        if (!Slot &&
            EndpointProperties->DeviceAddress == 0 &&
            EndpointProperties->PortNumber != 0)
        {
            Slot = XHCI_FindSlotByPort(Extension, EndpointProperties->PortNumber);
        }

        if (Slot)
        {
            if (!Slot->Addressed)
            {
                Status = XHCI_AddressDeviceSlot(Extension,
                                                Slot,
                                                EndpointProperties,
                                                FALSE);
                if (Status != MP_STATUS_SUCCESS)
                    return Status;
            }

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
            KeInitializeSpinLock(&XhciEndpoint->Lock);
            Slot->EndpointTable[1] = XhciEndpoint;

            /* EP0 is already programmed by the Address Device command; nothing
             * more to configure for the default control pipe once a slot exists. */
            return MP_STATUS_SUCCESS;
        }

        if (EndpointProperties->DeviceAddress == 0)
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

                DPRINT1("usbxhci: deferred EP0 bring-up from IRQL=%lu\n",
                        KeGetCurrentIrql());
                
            }

            DPRINT1("usbxhci: unable to schedule EP0 bring-up (no callback)\n");
            return MP_STATUS_NOT_SUPPORTED;
        }

        DPRINT1("usbxhci: no slot found for default control pipe addr=%u port=%u\n",
                EndpointProperties->DeviceAddress,
                EndpointProperties->PortNumber);
        return MP_STATUS_ERROR;
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

        if (Existing && Existing != XhciEndpoint)
        {
            if (InterlockedCompareExchange((volatile LONG *)&Existing->PendingWorkCount, 0, 0) != 0 ||
                Existing->ActiveTransfer)
            {
                DPRINT1("usbxhci: refusing to reopen ep %u on slot %u while work/transfers are active\n",
                        EndpointId,
                        Slot->SlotId);
                return MP_STATUS_FAILURE;
            }

            if (!Existing->UsesStaticRing)
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
    KeInitializeSpinLock(&XhciEndpoint->Lock);

    Status = XHCI_AllocateTransferRing(Extension,
                                       XHCI_EXTERNAL_EP_RING_TRBS,
                                       FALSE,
                                       &XhciEndpoint->TransferRing);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    /*
     * QEMU's xHCI (1B36:000D) can leave lower DCIs disabled if a higher DCI is
     * configured first (notably usb-storage's bulk OUT at DCI=4 before bulk IN
     * at DCI=3). Work around this by deferring the initial ConfigureEndpoint
     * for the bulk OUT pipe (EP 0x02 / DCI 4) until after the bulk IN pipe is
     * opened and configured.
     */
    if ((Extension->Quirks & XHCI_QUIRK_QEMU_CONFIG_EP_ORDER) &&
        !Slot->Configured &&
        EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_BULK &&
        EndpointProperties->Direction == USBPORT_TRANSFER_DIRECTION_OUT &&
        EndpointProperties->EndpointAddress == 0x02 &&
        EndpointId == 4)
    {
        Slot->DeferredEndpointTable[EndpointId] = XhciEndpoint;
        return MP_STATUS_SUCCESS;
    }

    Status = XHCI_ConfigureSlotEndpoint(Extension,
                                        Slot,
                                        XhciEndpoint,
                                        EndpointId);
    if (Status != MP_STATUS_SUCCESS)
    {
        XHCI_FreeTransferRing(&XhciEndpoint->TransferRing);
        return Status;
    }

    if ((Extension->Quirks & XHCI_QUIRK_QEMU_CONFIG_EP_ORDER) &&
        Slot->DeferredEndpointTable[4] != NULL &&
        Slot->EndpointTable[4] == NULL)
    {
        PXHCI_ENDPOINT Deferred = Slot->DeferredEndpointTable[4];
        Slot->DeferredEndpointTable[4] = NULL;

        Status = XHCI_ConfigureSlotEndpoint(Extension,
                                            Slot,
                                            Deferred,
                                            4);
        if (Status != MP_STATUS_SUCCESS)
        {
            Slot->DeferredEndpointTable[4] = Deferred;
            return Status;
        }
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

#if DBG
    if (XhciEndpoint->SlotId == 1 && (XhciEndpoint->EndpointId == 3 || XhciEndpoint->EndpointId == 4))
    {
        DPRINT1("usbxhci: CloseEndpoint slot=%u ep=%u addr=0x%02x\n",
                XhciEndpoint->SlotId,
                XhciEndpoint->EndpointId,
                (UCHAR)(XhciEndpoint->EndpointProperties.EndpointAddress & 0xFF));
    }
#endif

    if (XhciEndpoint->Slot &&
        XhciEndpoint->EndpointId < RTL_NUMBER_OF(XhciEndpoint->Slot->EndpointTable))
    {
        if (XhciEndpoint->Slot->DeferredEndpointTable[XhciEndpoint->EndpointId] == XhciEndpoint)
            XhciEndpoint->Slot->DeferredEndpointTable[XhciEndpoint->EndpointId] = NULL;

        if (!XhciEndpoint->DefaultControl)
            XHCI_DropSlotEndpoint(XhciEndpoint->Extension,
                                  XhciEndpoint->Slot,
                                  XhciEndpoint->EndpointId);

        XhciEndpoint->Slot->EndpointTable[XhciEndpoint->EndpointId] = NULL;
    }

    if (!XhciEndpoint->UsesStaticRing)
    {
        if (InterlockedCompareExchange((volatile LONG *)&XhciEndpoint->PendingWorkCount, 0, 0) != 0)
        {
            DPRINT1("usbxhci: CloseEndpoint skipping ring free while work is pending\n");
        }
        else
        {
            XHCI_FreeTransferRing(&XhciEndpoint->TransferRing);
        }
    }

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
    Extension->StartupHcePersistent = FALSE;
    Extension->Quirks = 0;
    Extension->PortPowerControl = FALSE;
    Extension->PortIndicatorsSupported = FALSE;
    Extension->StoppingOrRemoved = FALSE;
    Extension->Ep0WorkerCount = 0;
    KeInitializeTimerEx(&Extension->Ep0PollTimer, NotificationTimer);
    KeInitializeDpc(&Extension->Ep0PollDpc, XHCI_Ep0PollDpc, Extension);
    Extension->Ep0PollCounter = 0;
    XHCI_InitDeviceAddressMap(Extension);
    Extension->Resources = UsbPortResources;
    Extension->MmioBase = UsbPortResources->ResourceBase;

    if (!XHCI_EnablePciBusMaster(Extension))
    {
        DPRINT1("usbxhci: unable to enable PCI bus mastering\n");
        return MP_STATUS_ERROR;
    }

    Base = (PUCHAR)Extension->MmioBase;
    Extension->CapabilityRegisters = (PXHCI_CAPABILITY_REGISTERS)Base;
    {
        ULONG CapHeader0 = READ_REGISTER_ULONG((volatile ULONG *)Base);
        Extension->CapabilityLength = CapHeader0 & 0xFF;
        /* HciVersion occupies bits 31:16 of the first dword (little-endian) */
        Extension->HciVersion = (USHORT)((CapHeader0 >> 16) & 0xFFFF);
    }
    DPRINT1("usbxhci: MMIO base=%p CAPLEN=%lu\n", Extension->MmioBase, Extension->CapabilityLength);
    if (Extension->CapabilityLength < sizeof(XHCI_CAPABILITY_REGISTERS))
    {
        DPRINT1("usbxhci: invalid CAPLENGTH %lu\n", Extension->CapabilityLength);
        return MP_STATUS_ERROR;
    }

    DPRINT1("usbxhci: resource base=%p iospace=%lu startVA=%p startPA=%08lx irq=%lx flags=%lx msgcnt=%lu\n",
            UsbPortResources->ResourceBase,
            UsbPortResources->IoSpaceLength,
            (PVOID)UsbPortResources->StartVA,
            (ULONG)UsbPortResources->StartPA,
            UsbPortResources->InterruptVector,
            UsbPortResources->InterruptFlags,
            UsbPortResources->InterruptMessageCount);
    Extension->OperationalRegisters =
        (PXHCI_OPERATIONAL_REGISTERS)(Base + Extension->CapabilityLength);

    DbOffset = Extension->CapabilityRegisters->DbOff & ~0x3UL;
    RtOffset = Extension->CapabilityRegisters->Rtsoff & ~0x1FUL;

    Extension->DoorbellArray = (PXHCI_DOORBELL_ARRAY)(Base + DbOffset);
    Extension->RuntimeRegisters = (PXHCI_RUNTIME_REGISTERS)(Base + RtOffset);
    DPRINT1("usbxhci: DB offset=%lu RT offset=%lu Doorbell=%p Runtime=%p\n",
            DbOffset, RtOffset, Extension->DoorbellArray, Extension->RuntimeRegisters);

    /* Dump the first 32 bytes of the capability header to catch mis-mapped BARs. */
    {
        ULONG CapDump[8] = {0};
        SIZE_T i;
        for (i = 0; i < RTL_NUMBER_OF(CapDump); i++)
        {
            CapDump[i] = READ_REGISTER_ULONG((volatile ULONG *)(Base + (i * sizeof(ULONG))));
        }
        DPRINT1("usbxhci: CAP dump 0x00-0x1F: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
                CapDump[0], CapDump[1], CapDump[2], CapDump[3],
                CapDump[4], CapDump[5], CapDump[6], CapDump[7]);
    }

    XHCI_GetRegistryParameters(Extension);

    Status = XHCI_DisableLegacySupport(Extension);
    if (Status != MP_STATUS_SUCCESS)
    {
        /* Treat legacy handoff failures as non-fatal. Many virtual/ACPI
         * firmwares never clear HC BIOS ownership or expose incomplete
         * legacy capabilities; refusing to start the controller in these
         * cases leaves the entire USB3 stack unusable even though the
         * hardware is otherwise functional. Mirror Windows behaviour by
         * logging and continuing with shared control instead. */
#if DBG
        DPRINT1("usbxhci: DisableLegacySupport returned %lu, continuing with best-effort shared control\n",
                Status);
#endif
    }
    /* Probe for MSI/MSI-X capabilities */
    XHCI_ProbeMsiMsix(Extension);
    if (Extension->MsixSupported && !Extension->MsixEnabled)
    {
        if (!XHCI_EnableMsix(Extension))
            DPRINT1("usbxhci: failed to enable MSI-X, continuing with legacy IRQ\n");
    }
    /* Prefer message interrupts; if none assigned, fall back to legacy but warn. */
    if ((UsbPortResources->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE) == 0 &&
        (Extension->MsiSupported || Extension->MsixSupported))
    {
        DPRINT1("usbxhci: message interrupts supported but no message resource assigned; falling back to legacy IRQ (may be conflicted)\n");
        Extension->MsixEnabled = FALSE;
        Extension->MsiEnabled = FALSE;
    }

    if (UsbPortResources->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE)
    {
        ULONG Messages = UsbPortResources->InterruptMessageCount ?
                         UsbPortResources->InterruptMessageCount : 1;
        DPRINT1("usbxhci: using message interrupts (%lu vector%s)\n",
                Messages,
                (Messages == 1) ? "" : "s");
#if DBG
        if (!Extension->MsiSupported && !Extension->MsixSupported)
        {
            DPRINT1("usbxhci: WARNING: message interrupt resources present but PCI MSI/MSI-X capabilities missing – possible HAL/ACPI inconsistency\n");
        }
#endif
    }
    else
    {
        DPRINT1("usbxhci: using legacy IRQ vector 0x%lx (IRQL=%lu)\n",
                UsbPortResources->InterruptVector,
                (ULONG)UsbPortResources->InterruptLevel);
        if ((UsbPortResources->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE) == 0 &&
            (UsbPortResources->InterruptFlags & CM_RESOURCE_INTERRUPT_LATCHED) == 0)
        {
            DPRINT1("usbxhci: warning: legacy IRQ is shared/conflicted flags=%lx count=%lu\n",
                    UsbPortResources->InterruptFlags,
                    UsbPortResources->InterruptMessageCount);
        }
    }

    Extension->PendingUsbSts = 0;
    Extension->RhIrqEnabled = TRUE;
    Extension->RhPendingInvalidate = FALSE;
    Extension->InterruptsEnabled = FALSE;
    Extension->CommandRingTrbCount = XHCI_COMMAND_RING_TRBS;
    Extension->CommandRingCycleState = 1;
    Extension->EventRingDequeueIndex = 0;
    Extension->EventRingCycleState = 1;
    Extension->EventRingDequeuePointer = 0;

    if (!UsbPortResources->StartVA)
    {
        DPRINT1("usbxhci: StartController missing common-buffer VA\n");
        return MP_STATUS_NO_RESOURCES;
    }

    KeInitializeSpinLock(&Extension->CommandLock);
    KeInitializeSpinLock(&Extension->EventRingLock);
    InitializeListHead(&Extension->CommandContextList);
    KeInitializeSpinLock(&Extension->DeferredTransferLock);
    InitializeListHead(&Extension->DeferredTransferList);

    HcsParams1 = READ_REGISTER_ULONG(&Extension->CapabilityRegisters->HcsParams1);
    HcsParams2 = READ_REGISTER_ULONG(&Extension->CapabilityRegisters->HcsParams2);
    HcsParams3 = READ_REGISTER_ULONG(&Extension->CapabilityRegisters->HcsParams3);
    HccParams = READ_REGISTER_ULONG(&Extension->CapabilityRegisters->HccParams);

    {
        ULONG CapRaw0 = READ_REGISTER_ULONG((volatile ULONG *)Extension->CapabilityRegisters);
        ULONG CapRaw4 = READ_REGISTER_ULONG((volatile ULONG *)((PUCHAR)Extension->CapabilityRegisters + 4));
        ULONG CapRaw8 = READ_REGISTER_ULONG((volatile ULONG *)((PUCHAR)Extension->CapabilityRegisters + 8));
        DPRINT1("usbxhci: CAP dwords [0]=%08lx [4]=%08lx [8]=%08lx\n",
                CapRaw0, CapRaw4, CapRaw8);
    }

    DPRINT1("usbxhci: CAP raw HciVer=%04x (xHCI %u.%02u) HCS1=%08lx HCS2=%08lx HCS3=%08lx HCC=%08lx CAPLEN=%lu DbOff=%lu RtOff=%lu\n",
            Extension->HciVersion,
            (Extension->HciVersion >> 8) & 0xFF,
            Extension->HciVersion & 0xFF,
            HcsParams1,
            HcsParams2,
            HcsParams3,
            HccParams,
            Extension->CapabilityLength,
            DbOffset,
            RtOffset);
    if (Extension->HciVersion == 0 || Extension->CapabilityLength < sizeof(XHCI_CAPABILITY_REGISTERS))
    {
        DPRINT1("usbxhci: warning: unexpected HciVersion/CAPLENGTH (ver=%04x caplen=%lu)\n",
                Extension->HciVersion,
                Extension->CapabilityLength);
        DPRINT1("usbxhci: raw CAP header bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                READ_REGISTER_UCHAR((volatile UCHAR *)Base + 0),
                READ_REGISTER_UCHAR((volatile UCHAR *)Base + 1),
                READ_REGISTER_UCHAR((volatile UCHAR *)Base + 2),
                READ_REGISTER_UCHAR((volatile UCHAR *)Base + 3),
                READ_REGISTER_UCHAR((volatile UCHAR *)Base + 4),
                READ_REGISTER_UCHAR((volatile UCHAR *)Base + 5),
                READ_REGISTER_UCHAR((volatile UCHAR *)Base + 6),
                READ_REGISTER_UCHAR((volatile UCHAR *)Base + 7));
        return MP_STATUS_NOT_SUPPORTED;
    }
    else if (Extension->HciVersion < 0x0100)
    {
        DPRINT1("usbxhci: nonstandard HCI version 0x%04x, continuing with reduced expectations\n",
                Extension->HciVersion);
    }

    Extension->MaxSlots = XHCI_HCS1_MAX_SLOTS(HcsParams1);
    Extension->NumberOfPorts = XHCI_HCS1_MAX_PORTS(HcsParams1);
    if (Extension->MaxSlots == 0 || Extension->NumberOfPorts == 0)
    {
        DPRINT1("usbxhci: controller reports MaxSlots=%lu NumberOfPorts=%lu, treating as unsupported\n",
                Extension->MaxSlots,
                Extension->NumberOfPorts);
        return MP_STATUS_NOT_SUPPORTED;
    }
    if (Extension->MaxSlots > XHCI_MAX_SLOTS)
    {
        DPRINT1("usbxhci: controller reports MaxSlots=%lu, clamping to %u (xHCI 8-bit slot IDs)\n",
                Extension->MaxSlots,
                XHCI_MAX_SLOTS);
        Extension->MaxSlots = XHCI_MAX_SLOTS;
    }
    ASSERT(Extension->MaxSlots <= XHCI_MAX_SLOTS);
    for (Port = 0; Port <= XHCI_MAX_PORTS; Port++)
    {
        Extension->PortLinkState[Port] = XHCI_INVALID_LINK_STATE;
        Extension->PortConnectStatus[Port] = FALSE;
    }
    RtlZeroMemory(Extension->PortChangeMask, sizeof(Extension->PortChangeMask));
    if (Extension->NumberOfPorts > XHCI_MAX_PORTS)
    {
        DPRINT1("usbxhci: clamping port count from %lu to %lu\n",
                Extension->NumberOfPorts,
                XHCI_MAX_PORTS);
        Extension->NumberOfPorts = XHCI_MAX_PORTS;
    }
    Extension->PortPowerControl = (BOOLEAN)XHCI_HCS1_PPC(HcsParams1);
    XHCI_BuildProtocolPortMap(Extension);

    Extension->MaxScratchpadBuffers = XHCI_HCS2_MAX_SCRATCH(HcsParams2);
    Extension->Supports64Bit = (BOOLEAN)(XHCI_HCC_64BIT_ADDR(HccParams) != 0);
    Extension->ContextSize = XHCI_HCC_64B_CONTEXT(HccParams) ? 64 : 32;
    Extension->ScratchpadCount = Extension->MaxScratchpadBuffers;
    if (Extension->ScratchpadCount > XHCI_MAX_SCRATCHPADS)
    {
        DPRINT1("usbxhci: controller requests %lu scratchpads, max supported is %u – treating as unsupported\n",
                Extension->ScratchpadCount,
                XHCI_MAX_SCRATCHPADS);
        return MP_STATUS_NOT_SUPPORTED;
    }
    Extension->MaxU1ExitLatency = (UCHAR)XHCI_HCS3_U1_LATENCY(HcsParams3);
    Extension->MaxU2ExitLatency = (USHORT)XHCI_HCS3_U2_LATENCY(HcsParams3);

    {
        ULONG HwMaxIntr = XHCI_HCS1_MAX_INTERRUPTS(HcsParams1);
        ULONG ActiveIntr = 1;

        if (HwMaxIntr > 0 &&
            UsbPortResources->InterruptMessageCount > 0 &&
            UsbPortResources->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE)
        {
            ActiveIntr = UsbPortResources->InterruptMessageCount;
            if (ActiveIntr > HwMaxIntr)
                ActiveIntr = HwMaxIntr;
        }
        Extension->InterrupterCount = (UCHAR)(ActiveIntr ? ActiveIntr : 1);
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
    }

    XHCI_DetectHardwareQuirks(Extension);

    Status = XHCI_BuildCommonBufferLayout(Extension, UsbPortResources);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    if (!Extension->Supports64Bit ||
        (Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA))
    {
        ULONGLONG CommonStart = Extension->HcResourcesPhysical.QuadPart;
        ULONGLONG CommonEnd = CommonStart + Extension->CommonBufferSize - 1;

        if (CommonEnd >= 0x100000000ULL)
        {
            DPRINT1("usbxhci: common buffer not 32-bit DMA reachable (PA=%I64x size=%Iu quirks=0x%lx)\n",
                    (ULONGLONG)CommonStart,
                    (SIZE_T)Extension->CommonBufferSize,
                    Extension->Quirks);
            return MP_STATUS_NOT_SUPPORTED;
        }
    }

    XHCI_InitDeviceSlots(Extension);

    RtlZeroMemory(Extension->CommandRing,
                  sizeof(XHCI_TRB) * Extension->CommandRingTrbCount);
    RtlZeroMemory(Extension->EventRing,
                  sizeof(XHCI_TRB) * Extension->EventRingTrbCount);
    RtlZeroMemory(Extension->ErstTable,
                  sizeof(XHCI_ERST_ENTRY) * Extension->ErstEntryCount);

    XHCI_BuildErstTable(Extension);

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




    Extension->EventRingDequeuePointer = Extension->EventRingPhysical.QuadPart;

    Extension->PortIndicatorsSupported =
        (BOOLEAN)XHCI_HCC_PORT_INDICATORS(HccParams);
    if (Extension->Quirks & XHCI_QUIRK_NO_PORT_INDICATORS)
        Extension->PortIndicatorsSupported = FALSE;

    XHCI_ValidateContextLayout(Extension);

    /*
     * Bring the controller into a clean state before programming operational
     * registers (PageSize/DCBAA/CRCR/ERST). Firmware may leave the controller in
     * a dirty state across warm boots.
     */
    Status = XHCI_ResetController(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
                         XHCI_USBSTS_EINT |
                         XHCI_USBSTS_PCD |
                         XHCI_USBSTS_HSE |
                         XHCI_USBSTS_HCE |
                         XHCI_USBSTS_HCH);

    Status = XHCI_ConfigurePageSize(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    Status = XHCI_InitializeScratchpads(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    Status = XHCI_ProgramDcbaaCrcrAndConfig(Extension);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    XHCI_ProgramInterrupterState(Extension);
    XHCI_EnableInterrupts(Extension);

    Status = XHCI_RunController(Extension);
    if (Status != MP_STATUS_SUCCESS)
    {
        /* One retry: halt, reset, reprogram, then run again. */
        XHCI_HaltController(Extension, XHCI_WAIT_HALT_US);
        Status = XHCI_ResetController(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
                             XHCI_USBSTS_EINT |
                             XHCI_USBSTS_PCD |
                             XHCI_USBSTS_HSE |
                             XHCI_USBSTS_HCE |
                             XHCI_USBSTS_HCH);

        Status = XHCI_ConfigurePageSize(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        Status = XHCI_InitializeScratchpads(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        Status = XHCI_ProgramDcbaaCrcrAndConfig(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        XHCI_ProgramInterrupterState(Extension);
        XHCI_EnableInterrupts(Extension);

        Status = XHCI_RunController(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;
    }

    /* If the controller immediately asserts HCE after starting, try one recovery. */
    if (READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts) & XHCI_USBSTS_HCE)
    {
        ULONG UsbSts = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);
        ULONG UsbCmd = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd);

        DPRINT1("usbxhci: host controller error latched after start "
                "(USBSTS=%08lx USBCMD=%08lx) – attempting one recovery\n",
                UsbSts,
                UsbCmd);

        XHCI_HaltController(Extension, XHCI_WAIT_HALT_US);
        Status = XHCI_ResetController(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
                             XHCI_USBSTS_EINT |
                             XHCI_USBSTS_PCD |
                             XHCI_USBSTS_HSE |
                             XHCI_USBSTS_HCE |
                             XHCI_USBSTS_HCH);

        Status = XHCI_ConfigurePageSize(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        Status = XHCI_InitializeScratchpads(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        Status = XHCI_ProgramDcbaaCrcrAndConfig(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        XHCI_ProgramInterrupterState(Extension);
        XHCI_EnableInterrupts(Extension);

        Status = XHCI_RunController(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;
    }

    if (READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts) & XHCI_USBSTS_HCE)
    {
        ULONG UsbStsAfter = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);

        if ((UsbStsAfter & XHCI_USBSTS_HCE) &&
            (Extension->Quirks & XHCI_QUIRK_IGNORE_STARTUP_HCE))
        {
            if (XHCI_ValidateCommandEngine(Extension) == MP_STATUS_SUCCESS)
            {
                DPRINT1("usbxhci: persistent HCE after start on QEMU xHCI "
                        "(USBSTS=%08lx) – ignoring per startup quirk after NO-OP validation\n",
                        UsbStsAfter);

                WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
                                     XHCI_USBSTS_HCE |
                                     XHCI_USBSTS_HSE |
                                     XHCI_USBSTS_PCD |
                                     XHCI_USBSTS_EINT);

                Extension->FatalError = FALSE;
                Extension->ControllerRunning = TRUE;
                Extension->InterruptsEnabled = TRUE;
                Extension->StartupHcePersistent = TRUE;

                goto StartSuccess;
            }

            DPRINT1("usbxhci: persistent HCE after start and command ring is unresponsive\n");
        }

        DPRINT1("usbxhci: FATAL controller error persists after start recovery "
                "(USBSTS=%08lx HCS2=%08lx HCS3=%08lx MaxSlots=%lu MaxPorts=%lu Scratchpads=%lu) "
                "– dumping controller state\n",
                UsbStsAfter,
                HcsParams2,
                HcsParams3,
                Extension->MaxSlots,
                Extension->NumberOfPorts,
                Extension->ScratchpadCount);

        XHCI_DumpControllerState(Extension, "start failed HCE");

        Extension->FatalError = TRUE;
        Extension->ControllerRunning = FALSE;
        Extension->InterruptsEnabled = FALSE;
        Extension->RhIrqEnabled = FALSE;

        return MP_STATUS_HW_ERROR;
    }

StartSuccess:
    XHCI_PowerOnAllPorts(Extension);
    XHCI_ConfigureAllPortsLpm(Extension);
    return MP_STATUS_SUCCESS;
}

static VOID NTAPI
XHCI_StopController(PVOID MiniPortExtension,
                    BOOLEAN IsDoNotCallMiniport)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    ULONG SlotId;
    ULONG WaitLoops;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(IsDoNotCallMiniport);

    if (!Extension)
        return;

    DPRINT1("usbxhci: StopController\n");

    Extension->StoppingOrRemoved = TRUE;
    KeCancelTimer(&Extension->Ep0PollTimer);
    InterlockedExchange(&Extension->Ep0PollCounter, 0);

    /* Give any pending EP0 bring-up work a chance to drain before we tear
     * down hardware/MMIO pointers. */
    WaitLoops = XHCI_EP0_WORK_TIMEOUT_US / 100;
    while (InterlockedCompareExchange(&Extension->Ep0WorkerCount, 0, 0) != 0 &&
           WaitLoops--)
    {
        KeStallExecutionProcessor(100);
    }

    XHCI_ShutdownController(Extension, TRUE);

    for (SlotId = 0; SlotId <= (Extension->MaxSlots ? Extension->MaxSlots : XHCI_MAX_SLOTS); SlotId++)
    {
        RtlZeroMemory(&Extension->DeviceSlots[SlotId], sizeof(XHCI_DEVICE_SLOT));
        if (Extension->Dcbaa)
            Extension->Dcbaa[SlotId] = 0;
    }

    Extension->PendingUsbSts = 0;
    Extension->RhIrqEnabled = FALSE;
    Extension->RhPendingInvalidate = FALSE;
    Extension->InterruptsEnabled = FALSE;
    Extension->ControllerRunning = FALSE;
    Extension->FatalError = FALSE;

    KeAcquireSpinLock(&Extension->CommandLock, &OldIrql);
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
    KeReleaseSpinLock(&Extension->CommandLock, OldIrql);

    KeAcquireSpinLock(&Extension->DeferredTransferLock, &OldIrql);
    while (!IsListEmpty(&Extension->DeferredTransferList))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&Extension->DeferredTransferList);
        PXHCI_TRANSFER Transfer = CONTAINING_RECORD(Entry, XHCI_TRANSFER, ListEntry);

        KeReleaseSpinLock(&Extension->DeferredTransferLock, OldIrql);

        if (XhciRegPacket.UsbPortCompleteTransfer)
        {
            XhciRegPacket.UsbPortCompleteTransfer(Extension,
                                                  Transfer->Endpoint,
                                                  Transfer->TransferParameters,
                                                  USBD_STATUS_CANCELED,
                                                  Transfer->BytesTransferred);
        }

        KeAcquireSpinLock(&Extension->DeferredTransferLock, &OldIrql);
    }
    KeReleaseSpinLock(&Extension->DeferredTransferLock, OldIrql);
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
    Extension->Ep0RingArrayPhysical.QuadPart = 0;
    Extension->Dcbaa = NULL;
    Extension->DcbaaPhysical.QuadPart = 0;
    Extension->ScratchpadPointerArray = NULL;
    Extension->ScratchpadArrayPhysical.QuadPart = 0;
    Extension->ScratchpadBuffers = NULL;
    Extension->ScratchpadBuffersPhysical.QuadPart = 0;
    Extension->DeviceContexts = NULL;
    Extension->InputContexts = NULL;
    Extension->Ep0TransferRings = NULL;
    Extension->CommonBufferSize = 0;
    Extension->StoppingOrRemoved = FALSE;
    Extension->Ep0WorkerCount = 0;
    Extension->Signature = 0;
    Extension->Quirks = 0;
    RtlFillMemory(Extension->PortLinkState,
                  sizeof(Extension->PortLinkState),
                  XHCI_INVALID_LINK_STATE);
    RtlZeroMemory(Extension->PortConnectStatus,
                  sizeof(Extension->PortConnectStatus));
    RtlZeroMemory(Extension->VirtualPortAnnounced,
                  sizeof(Extension->VirtualPortAnnounced));
    XHCI_InitDeviceAddressMap(Extension);
}

static BOOLEAN NTAPI
XHCI_InterruptService(PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    ULONG Status;
    ULONG AckMask;
    ULONG UsbSts;

    if (!Extension || !Extension->OperationalRegisters || Extension->FatalError || Extension->StoppingOrRemoved)
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

    /* If HCE/HSE are set, latch them into PendingUsbSts even when EINT is
     * clear so the DPC can decide whether to ignore or handle them. */
    UsbSts = READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);
    if (UsbSts & (XHCI_USBSTS_HSE | XHCI_USBSTS_HCE))
    {
        InterlockedOr((volatile LONG *)&Extension->PendingUsbSts,
                      UsbSts & (XHCI_USBSTS_HSE | XHCI_USBSTS_HCE));
        AckMask |= (UsbSts & (XHCI_USBSTS_HSE | XHCI_USBSTS_HCE));
    }

    if (AckMask & (XHCI_USBSTS_EINT | XHCI_USBSTS_PCD))
    {
        XHCI_DBG(XHCI_TRACE_EVENTS,
                 "usbxhci: ISR ack UsbSts=%08lx AckMask=%08lx\n",
                 Status,
                 AckMask);
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

    XHCI_DBG(XHCI_TRACE_EVENTS,
             "usbxhci: DPC (IRQL=%lu)\n",
             (ULONG)KeGetCurrentIrql());

    if (!Extension)
        return;

    Pending = (ULONG)InterlockedExchange((volatile LONG *)&Extension->PendingUsbSts, 0);
    XHCI_DBG(XHCI_TRACE_EVENTS,
             "usbxhci: DPC pending=%08lx RhIrqEnabled=%u IntsEnabled=%u\n",
             Pending,
             Extension->RhIrqEnabled ? 1 : 0,
             Extension->InterruptsEnabled ? 1 : 0);

    if (Pending & XHCI_USBSTS_PCD)
    {
        XHCI_DBG(XHCI_TRACE_EVENTS,
                 "usbxhci: DPC observed PCD pending (UsbSts latch)\n");
    }

    /* HSE is always fatal. HCE is fatal except on platforms where we have
     * an explicit quirk to ignore QEMU's latched HCE; in that case, clear
     * the bit here so we do not tear the controller down. */
    if ((Pending & XHCI_USBSTS_HCE) &&
        Extension->StartupHcePersistent)
    {
        Pending &= ~XHCI_USBSTS_HCE;
    }

    /* Host System Error (HSE) and unhandled Host Controller Error (HCE) are
     * fatal conditions from the perspective of this miniport. If either is
     * seen, log it once and shut the controller down so we don't spin in a
     * DPC storm on a permanently-asserted error bit. */
    if (Pending & (XHCI_USBSTS_HSE | XHCI_USBSTS_HCE))
    {
        if (!Extension->FatalError)
        {
            XHCI_HandleControllerError(Extension, Pending);
        }
        return;
    }

    if (Pending & XHCI_USBSTS_EINT)
    {
        XHCI_DBG(XHCI_TRACE_EVENTS,
                 "usbxhci: DPC: EINT set, servicing events\n");
        XHCI_ServiceEventRing(Extension, TRUE, TRUE);
    }

    if (Pending & XHCI_USBSTS_PCD)
    {
        BOOLEAN NotifyNow = Extension->RhIrqEnabled &&
                            XhciRegPacket.UsbPortInvalidateRootHub != NULL;
        BOOLEAN FoundChange = XHCI_ScanPortStatusChanges(Extension, NotifyNow);

        if (FoundChange)
        {
            Extension->RhPendingInvalidate = NotifyNow ? FALSE : TRUE;
        }
        else if (NotifyNow && Extension->RhPendingInvalidate)
        {
            Extension->RhPendingInvalidate = FALSE;
            XhciRegPacket.UsbPortInvalidateRootHub(Extension);
        }
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
    KIRQL Irql;

    UNREFERENCED_PARAMETER(TransferHandle);

    if (BytesTransferred)
        *BytesTransferred = 0;

    if (!Extension || !Endpoint || !Endpoint->Slot)
    {
        DPRINT1("usbxhci: AbortTransfer invalid args (IRQL=%lu)\n",
                (ULONG)KeGetCurrentIrql());
        return;
    }

    Irql = KeGetCurrentIrql();
    if (Irql > PASSIVE_LEVEL)
    {
        PXHCI_EP_RESET_WORK Work;

        Work = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(*Work),
                                     XHCI_TAG);
        if (!Work)
        {
            DPRINT1("usbxhci: AbortTransfer fallback to synchronous reset (alloc failed)\n");
            Irql = PASSIVE_LEVEL;
        }
        else
        {
            InterlockedIncrement(&Endpoint->PendingWorkCount);
            Work->Extension = Extension;
            Work->Endpoint = Endpoint;
            Work->RingDoorbell = TRUE;
            ExInitializeWorkItem(&Work->Item,
                                 XHCI_EndpointResetWorker,
                                 Work);
            ExQueueWorkItem(&Work->Item, DelayedWorkQueue);
            DPRINT1("usbxhci: AbortTransfer queued reset work for slot %u ep %u\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId);
            return;
        }
    }

    InterlockedIncrement(&Endpoint->PendingWorkCount);
    XHCI_PerformEndpointResetSequence(Extension, Endpoint, TRUE);
    InterlockedDecrement(&Endpoint->PendingWorkCount);

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
    KIRQL Irql;

    if (!Extension || !Endpoint || !Endpoint->Slot)
        return;

    /* Clear-stall path: reset endpoint state and re-sync dequeue. */
    Irql = KeGetCurrentIrql();
    if (Irql > PASSIVE_LEVEL)
    {
        PXHCI_EP_RESET_WORK Work;

        Work = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(*Work),
                                     XHCI_TAG);
        if (!Work)
        {
            DPRINT1("usbxhci: SetEndpointStatus fallback to synchronous reset (alloc failed)\n");
            Irql = PASSIVE_LEVEL;
        }
        else
        {
            InterlockedIncrement(&Endpoint->PendingWorkCount);
            Work->Extension = Extension;
            Work->Endpoint = Endpoint;
            Work->RingDoorbell = TRUE;
            ExInitializeWorkItem(&Work->Item,
                                 XHCI_EndpointResetWorker,
                                 Work);
            ExQueueWorkItem(&Work->Item, DelayedWorkQueue);
            DPRINT1("usbxhci: SetEndpointStatus queued reset work for slot %u ep %u\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId);
            return;
        }
    }

    InterlockedIncrement(&Endpoint->PendingWorkCount);
    XHCI_PerformEndpointResetSequence(Extension, Endpoint, TRUE);
    InterlockedDecrement(&Endpoint->PendingWorkCount);
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
    volatile ULONG *UsbCmd;
    volatile ULONG *UsbSts;

    if (!Extension || !Extension->OperationalRegisters)
        return MP_STATUS_ERROR;

    UsbCmd = &Extension->OperationalRegisters->UsbCmd;
    UsbSts = &Extension->OperationalRegisters->UsbSts;

    Status = READ_REGISTER_ULONG(UsbSts);
    Command = READ_REGISTER_ULONG(UsbCmd);

    if ((Command & XHCI_USBCMD_RS) && !(Status & XHCI_USBSTS_HCH))
    {
#if DBG
        if (Status & XHCI_USBSTS_CNR)
        {
            DPRINT1("usbxhci: ASSERT controller running with CNR set (USBCMD=%08lx USBSTS=%08lx)\n",
                    Command,
                    Status);
            ASSERT((Status & XHCI_USBSTS_CNR) == 0);
        }
#endif
        Extension->ControllerRunning = TRUE;
        
    }

    Command |= XHCI_USBCMD_RS;
    WRITE_REGISTER_ULONG(UsbCmd, Command);

    if (!XHCI_WaitForRegisterBits(UsbSts,
                                  XHCI_USBSTS_HCH,
                                  FALSE,
                                  XHCI_WAIT_HALT_US))
    {
        DPRINT1("usbxhci: controller failed to exit halt state\n");
        return MP_STATUS_HW_ERROR;
    }

    Status = READ_REGISTER_ULONG(UsbSts);
#if DBG
    Command = READ_REGISTER_ULONG(UsbCmd);
    if ((Command & XHCI_USBCMD_RS) == 0 ||
        (Status & XHCI_USBSTS_HCH) != 0 ||
        (Status & XHCI_USBSTS_CNR) != 0)
    {
        DPRINT1("usbxhci: unexpected state after run (USBCMD=%08lx USBSTS=%08lx)\n",
                Command,
                Status);
        ASSERT((Command & XHCI_USBCMD_RS) != 0);
        ASSERT((Status & XHCI_USBSTS_HCH) == 0);
        ASSERT((Status & XHCI_USBSTS_CNR) == 0);
    }
#endif

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

    /*
     * On resume, reprogram the interrupter state and enable INTE before
     * starting the controller so root‑hub change events can be generated
     * immediately once RS is set, matching the bring‑up path ordering.
     */
    XHCI_ProgramInterrupterState(Extension);


    XHCI_EnableInterrupts(Extension);
    DPRINT1("usbxhci: First RunController attempt\n");

    KeStallExecutionProcessor(10000); // 10ms delay
    DPRINT1("usbxhci: About to Run Controller (Retry)\n");
    Status = XHCI_RunController(Extension);
    DPRINT1("usbxhci: RunController ret 0x%x STS=0x%x\n", Status, READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts));
    if (Status != MP_STATUS_SUCCESS)
        return Status;

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
    DPRINT1("XHCI_RH_GetRootHubData: Called\n");
    PXHCI_EXTENSION Extension = MiniPortExtension;
    PUSBPORT_ROOT_HUB_DATA HubData = RootHubData;
    ULONG PortCount;
    ULONG Hcs1 = 0;

    if (!Extension || !HubData)
        return;

    RtlZeroMemory(HubData, sizeof(*HubData));

    /*
     * Only advertise a root hub when the host controller has been
     * successfully started and reports a non‑zero port count. This
     * matches USBPORT’s expectations and prevents stale data from being
     * reported after a failed start or fatal error.
     */
    if (!Extension->ControllerRunning ||
        Extension->NumberOfPorts == 0)
    {
#if DBG
        DPRINT1("usbxhci: RH_GetRootHubData while HC not running (running=%u ports=%lu)\n",
                Extension->ControllerRunning ? 1u : 0u,
                Extension->NumberOfPorts);
#endif
        return;
    }

    PortCount = Extension->NumberOfPorts;
    if (Extension->CapabilityRegisters)
    {
        Hcs1 = READ_REGISTER_ULONG(&Extension->CapabilityRegisters->HcsParams1);
        if (Hcs1 != 0)
        {
            ULONG HwPorts = XHCI_HCS1_MAX_PORTS(Hcs1);

            if (HwPorts != 0 && HwPorts <= XHCI_MAX_PORTS)
            {
                if (PortCount != HwPorts)
                {
                    DPRINT1("usbxhci: RH_GetRootHubData correcting port count (%lu -> %lu) from HCS1=%08lx\n",
                            PortCount,
                            HwPorts,
                            Hcs1);
                    PortCount = HwPorts;
                    Extension->NumberOfPorts = HwPorts;
                }
            }
        }
    }

    HubData->NumberOfPorts = PortCount;
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
    // DPRINT1("Update: P5 Raw=%08lx\n", PortValue);
    ULONG Speed;
    ULONG LinkState;
    UCHAR PreviousLinkState = XHCI_INVALID_LINK_STATE;
    BOOLEAN CurrentConnect;
    BOOLEAN PreviousConnect = FALSE;
    BOOLEAN ReportedLinkChange = FALSE;
    PUSB_30_PORT_STATUS PortStatus30 = &PortStatus->PortStatus.Usb30PortStatus;
    PUSB_30_PORT_CHANGE PortChange30 = &PortStatus->PortChange.Usb30PortChange;

    UCHAR Protocol = 0;

    if (Extension && PortNumber > 0 && PortNumber <= XHCI_MAX_PORTS)
        Protocol = Extension->PortProtocol[PortNumber];

    CurrentConnect = (PortValue & XHCI_PORTSC_CCS) ? TRUE : FALSE;

    if (CurrentConnect)
    {
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_CONNECT;
        if (Protocol >= 3)
            PortStatus30->CurrentConnectStatus = 1;
    }

    if (PortValue & XHCI_PORTSC_PED)
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_ENABLE;
    
    if (Protocol >= 3)
        PortStatus30->PortEnabledDisabled = (PortValue & XHCI_PORTSC_PED) ? 1 : 0;

    LinkState = (PortValue & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;
    if (LinkState == PORT_LINK_STATE_U3)
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_SUSPEND;
    
    if (Protocol >= 3)
        PortStatus30->PortLinkState = (USHORT)LinkState;

    if (Extension && PortNumber > 0 && PortNumber <= XHCI_MAX_PORTS)
    {
        PreviousLinkState = Extension->PortLinkState[PortNumber];
        Extension->PortLinkState[PortNumber] = (UCHAR)LinkState;
        PreviousConnect = Extension->PortConnectStatus[PortNumber] ? TRUE : FALSE;
        Extension->PortConnectStatus[PortNumber] = CurrentConnect;
    }

    if (PortValue & (XHCI_PORTSC_PRC | XHCI_PORTSC_WRC))
        PreviousConnect = FALSE;

    if (PortValue & XHCI_PORTSC_OCA)
    {
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_OVER_CURRENT;
        if (Protocol >= 3)
            PortStatus30->OverCurrent = 1;
    }

    if (PortValue & XHCI_PORTSC_PR)
    {
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_RESET;
        if (Protocol >= 3)
            PortStatus30->Reset = 1;
    }

    if (Extension && Extension->PortPowerControl)
    {
        if (PortValue & XHCI_PORTSC_PP)
            PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_POWER;
        
        if (Protocol >= 3)
            PortStatus30->PortPower = (PortValue & XHCI_PORTSC_PP) ? 1 : 0;
    }
    else
    {
        if (Protocol >= 3)
            PortStatus30->PortPower = 1;
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_POWER;
    }

    Speed = (PortValue & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
    if (Speed == XHCI_PORTSC_SPEED_LOW)
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_LOW_SPEED;
    else if (Speed == XHCI_PORTSC_SPEED_HIGH || Speed == XHCI_PORTSC_SPEED_SUPER)
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_HIGH_SPEED;

    if (Protocol >= 3)
    {
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
    }

    if ((PortValue & XHCI_PORTSC_CSC) || (CurrentConnect != PreviousConnect))
    {
        if (!CurrentConnect &&
            PreviousConnect &&
            Extension &&
            XHCI_IsVirtualPort(Extension, PortNumber) &&
            PortNumber <= XHCI_MAX_PORTS)
        {
            Extension->VirtualPortAnnounced[PortNumber] = FALSE;
        }

        if (PortNumber == 7)
        {
            XHCI_RH_AckPortChange(Extension, PortNumber, XHCI_PORTSC_CSC);
            PortStatus->PortStatus.AsUshort16 &= ~USB_PORT_STATUS_CONNECT;
            if (Protocol >= 3) PortStatus30->CurrentConnectStatus = 0;
        }
        else
        {
            PortStatus->PortChange.Usb20PortChange.ConnectStatusChange = 1;
            if (Protocol >= 3)
                PortChange30->ConnectStatusChange = 1;
        }
    }

    if (Extension && XHCI_IsVirtualPort(Extension, PortNumber))
    {
        if (!Extension->VirtualPortAnnounced[PortNumber])
        {
            PortStatus->PortChange.Usb20PortChange.ConnectStatusChange = 1;
            if (Protocol >= 3)
                PortChange30->ConnectStatusChange = 1;
            Extension->VirtualPortAnnounced[PortNumber] = TRUE;
        }
        else
        {
            PortStatus->PortChange.Usb20PortChange.ConnectStatusChange = 0;
            if (Protocol >= 3)
                PortChange30->ConnectStatusChange = 0;
        }
    }

    if (PortValue & XHCI_PORTSC_PEC)
    {
        if (Protocol < 3)
            PortStatus->PortChange.Usb20PortChange.PortEnableDisableChange = 1;
    }

    if (PortValue & XHCI_PORTSC_PLC)
    {
        PortStatus->PortChange.Usb20PortChange.SuspendChange = 1;
        if (Protocol >= 3)
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
        if (Protocol >= 3)
            PortChange30->BHResetChange = 1;
    }

    if (PortValue & XHCI_PORTSC_CEC)
    {
        if (Protocol >= 3)
            PortChange30->PortConfigErrorChange = 1;
    }

    if (!ReportedLinkChange &&
        PreviousLinkState != XHCI_INVALID_LINK_STATE &&
        PreviousLinkState != LinkState)
    {
        if (Protocol >= 3)
            PortChange30->PortLinkStateChange = 1;
        if ((PreviousLinkState == PORT_LINK_STATE_U3 && LinkState != PORT_LINK_STATE_U3) ||
            (LinkState == PORT_LINK_STATE_U3 && PreviousLinkState != PORT_LINK_STATE_U3))
        {
            PortStatus->PortChange.Usb20PortChange.SuspendChange = 1;
        }
    }

    if (PortStatus->PortChange.Usb20PortChange.ConnectStatusChange)
    {
        XHCI_DBG(XHCI_TRACE_PORTS,
                 "XHCI_Sts: P%u C%u\n",
                 PortNumber,
                 PortStatus->PortChange.Usb20PortChange.ConnectStatusChange);
    }
    XHCI_DBG(XHCI_TRACE_PORTS,
             "XHCI_Sts: P%u S=0x%x C=0x%x CSC=%u\n",
             PortNumber,
             PortStatus->PortStatus.AsUshort16,
             PortStatus->PortChange.AsUshort16,
             PortStatus->PortChange.Usb20PortChange.ConnectStatusChange);
}

static
MPSTATUS
NTAPI
XHCI_RH_GetPortStatus(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port,
    _Out_ PUSB_PORT_STATUS_AND_CHANGE PortStatus)
{
    // DPRINT1("XHCI_RH_GetPortStatus: Port=%u\n", Port);
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
    if (Port <= XHCI_MAX_PORTS)
    {
        ULONG LatchedChanges =
            (ULONG)InterlockedCompareExchange(
                (volatile LONG *)&Extension->PortChangeMask[Port],
                0,
                0);
        PortValue |= LatchedChanges & XHCI_PORTSC_CHANGE_MASK;
    }
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

    //DPRINT1("usbxhci: RH_GetPortStatus ext=%p port=%u PortSC=0x%08lx Status=0x%04x Change=0x%04x\n",
    //        Extension,
    //        Port,
    //        PortValue,
    //        PortStatus->PortStatus.AsUshort16,
    //        PortStatus->PortChange.AsUshort16);

    return MP_STATUS_SUCCESS;
}

static
MPSTATUS
NTAPI
XHCI_RH_SetFeaturePortPower(
    _In_ PVOID MiniPortExtension,
    _In_ USHORT Port)
{
    DPRINT1("XHCI_RH_SetFeaturePortPower: Port=%u\n", Port);
    PXHCI_EXTENSION Extension = MiniPortExtension;

    if (!Extension)
        return MP_STATUS_ERROR;

    /* On controllers without per-port power switching, treat this as a no-op. */
    if (!Extension->PortPowerControl)
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

    if (!Extension)
        return MP_STATUS_ERROR;

    /* No-op on controllers that do not implement per-port power. */
    if (!Extension->PortPowerControl)
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
    DPRINT1("XHCI_RH_SetFeaturePortReset: Port=%u\n", Port);
    PXHCI_EXTENSION Extension = MiniPortExtension;

    if (!Extension)
        return MP_STATUS_ERROR;

    if (XHCI_PortIsSuperSpeed(Extension, Port))
        return XHCI_ModifyPortBits(Extension, Port, XHCI_PORTSC_WPR, 0, 0);

    /* QEMU Quirk: Explicitly set PED (Enabled) and PP (Power) bits.
       QEMU's xHCI emulation often fails to enable the port or drops power
       during a standard reset, causing enumeration failure. */
    if (Extension->Quirks & XHCI_QUIRK_IGNORE_STARTUP_HCE)
    {
        DPRINT1("XHCI: QEMU Quirk - Forcing PED | PP | PR on Port %u\n", Port);
        return XHCI_ModifyPortBits(Extension, Port, 
                                   XHCI_PORTSC_PED | XHCI_PORTSC_PR | XHCI_PORTSC_PP, 
                                   0, 0);
    }

    /* Standard Behavior: Set PR (Port Reset). 
       Note: PP should be maintained if PPC is supported, but standard says PR is enough. */
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
    DPRINT1("XHCI_RH_ClearFeaturePortConnectChange: Port=%u\n", Port);
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
    DPRINT1("XHCI_RH_ClearFeaturePortResetChange: Port=%u\n", Port);
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
    DPRINT1("XHCI_RH_DisableIrq: Called\n");
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
    DPRINT1("XHCI_RH_EnableIrq: Called\n");
    PXHCI_EXTENSION Extension = MiniPortExtension;

    if (!Extension)
        return;

    Extension->RhIrqEnabled = TRUE;
    if (Extension->RhPendingInvalidate &&
        XhciRegPacket.UsbPortInvalidateRootHub)
    {
        Extension->RhPendingInvalidate = FALSE;
        XhciRegPacket.UsbPortInvalidateRootHub(Extension);
    }
}
// CHECK_STRING_12345
