/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <arif.ing@outlook.com>
 */
    
#include "usbxhci.h"

#if defined(__GNUC__) && !defined(__cplusplus)
#undef __forceinline
#define __forceinline inline __attribute__((__always_inline__))
#endif

#define NDEBUG
#include <debug.h>

/*
 * XHCI ROADMAP (Windows 8/10-style behavior):
 * - Initialize DCBAA + scratchpads; size contexts via HCCPARAMS.
 * - Implement command ring + doorbell flow (Enable/Address/Configure/Stop/Set Dequeue).
 * - Implement event ring (ERST/ERDP) and command/transfer event handling.
 * - Build endpoint transfer rings; map TRBs; complete USBPORT transfers.
 * - Handle PORTSC state changes, reset/enable, and enumeration sequence.
 * - Add halt recovery and error paths (Stop Endpoint, Set TR Dequeue).
 */

#define XHCI_READ_REGISTER_ULONG(_reg) READ_REGISTER_ULONG((PULONG)(ULONG_PTR)(_reg))
#define XHCI_WRITE_REGISTER_ULONG(_reg, _val) WRITE_REGISTER_ULONG((PULONG)(ULONG_PTR)(_reg), (_val))
#define XHCI_READ_REGISTER_UCHAR(_reg) READ_REGISTER_UCHAR((PUCHAR)(ULONG_PTR)(_reg))

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
#define XHCI_TRANSFER_POLL_INTERVAL_US 500
#define XHCI_EP0_STALL_RESET_TIMEOUT_MS 200
#define XHCI_EP0_STALL_RESET_POLL_MS 1

USBPORT_REGISTRATION_PACKET XhciRegPacket;

/*
 * Registry-based quirk overrides.
 * Each quirk flag can be forced on (value=1) or off (value=0) via registry.
 * When XxxxValid is FALSE, the automatic hardware detection result is used.
 */
static BOOLEAN g_XhciStartupHceQuirkOverrideValid;
static BOOLEAN g_XhciStartupHceQuirkOverride;
static BOOLEAN g_XhciNonCoherentDmaOverrideValid;
static BOOLEAN g_XhciNonCoherentDmaOverride;
static BOOLEAN g_XhciForce32BitDmaOverrideValid;
static BOOLEAN g_XhciForce32BitDmaOverride;
static BOOLEAN g_XhciVBoxQuirksOverrideValid;
static BOOLEAN g_XhciVBoxQuirksOverride;
static BOOLEAN g_XhciQemuQuirksOverrideValid;
static BOOLEAN g_XhciQemuQuirksOverride;
static BOOLEAN g_XhciLimitU1U2OverrideValid;
static BOOLEAN g_XhciLimitU1U2Override;

/**
 * @brief Check if a specific quirk is enabled.
 *
 * This is the unified policy layer for quirk checks. It combines
 * hardware detection results with registry overrides.
 *
 * @param Extension Controller extension
 * @param QuirkFlag The quirk flag to check (XHCI_QUIRK_*)
 *
 * @return TRUE if quirk is enabled, FALSE otherwise
 */
FORCEINLINE
BOOLEAN
XHCI_QuirkEnabled(
    _In_ PXHCI_EXTENSION Extension,
    _In_ ULONG QuirkFlag)
{
    if (!Extension)
        return FALSE;

    return (Extension->Quirks & QuirkFlag) != 0;
}

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

#define XHCI_TRACE_EVENTS    0x00000001
#define XHCI_TRACE_TRANSFERS 0x00000002
#define XHCI_TRACE_COMMANDS  0x00000004
#define XHCI_TRACE_PORTS     0x00000008

#if DBG
static ULONG g_XhciTraceMask;

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
static BOOLEAN XHCI_Requires32BitDma(PXHCI_EXTENSION Extension);
static BOOLEAN XHCI_SgListHasHighAddress(PUSBPORT_SCATTER_GATHER_LIST SgList, PULONGLONG HighAddress);
static MPSTATUS XHCI_PrepareBounceBuffer(PXHCI_EXTENSION Extension,
                                         PXHCI_TRANSFER Transfer,
                                         ULONG Length,
                                         BOOLEAN DataIn);
static VOID XHCI_ReleaseBounceBuffer(PXHCI_TRANSFER Transfer);
static VOID XHCI_FinalizeBounceBuffer(PXHCI_TRANSFER Transfer);
static ULONG XHCI_CopySgListToBuffer(PUSBPORT_SCATTER_GATHER_LIST SgList,
                                     PVOID Buffer,
                                     ULONG BufferLength);
static ULONG XHCI_CopyBufferToSgList(PUSBPORT_SCATTER_GATHER_LIST SgList,
                                     const VOID *Buffer,
                                     ULONG BufferLength);
static MPSTATUS XHCI_InitBouncePool(PXHCI_EXTENSION Extension);
static VOID XHCI_FreeBouncePool(PXHCI_EXTENSION Extension);
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
static VOID NTAPI XHCI_TransferPollDpc(PKDPC Dpc,
                                       PVOID DeferredContext,
                                       PVOID SystemArg1,
                                       PVOID SystemArg2);
static VOID XHCI_ScheduleTransferPoll(PXHCI_EXTENSION Extension);
static VOID XHCI_QueueEp0StallReset(PXHCI_EXTENSION Extension, PXHCI_ENDPOINT Endpoint);
static MPSTATUS XHCI_WaitForEp0StallReset(PXHCI_EXTENSION Extension, PXHCI_ENDPOINT Endpoint);
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
/* No virtual-port emulation helpers. */
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
static VOID XHCI_DisablePciIntx(PXHCI_EXTENSION Extension);
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
#if !defined(_M_ARM64)
static BOOLEAN XHCI_EnableMsix(PXHCI_EXTENSION Extension);
#endif
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
    LONG RefCount;
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
    BOOLEAN ClearStallResetFlags;
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
    PXHCI_TRANSFER Transfer;
    PXHCI_ENDPOINT Endpoint;     /* Endpoint for which SwEnumRefCount was incremented */
    USBPORT_ENDPOINT_PROPERTIES EndpointProperties;
    UCHAR SlotId;
    BOOLEAN NeedsAddressDevice;  /* Issue ADDRESS_DEVICE at PASSIVE_LEVEL */
    UCHAR NewAddress;            /* USB address for SET_ADDRESS */
} XHCI_SWENUM_WORK, *PXHCI_SWENUM_WORK;

#define XHCI_DEFERRED_OPEN_TIMEOUT_US 1000000
#define XHCI_EP0_WORK_TIMEOUT_US 1000000
#define XHCI_SWENUM_WORK_TIMEOUT_US 1000000
#define XHCI_CLOSE_DRAIN_TIMEOUT_US 2000000
#define XHCI_CLOSE_DRAIN_POLL_US 1000

/*
 * XHCI_ReferenceEndpointForSwEnum - Acquire a reference for SW-enum work.
 *
 * This function atomically increments the endpoint's SwEnumRefCount if the
 * endpoint is not closing. It returns TRUE if the reference was acquired,
 * or FALSE if the endpoint is closing and no new work should be queued.
 *
 * The caller must call XHCI_DereferenceEndpointForSwEnum after the work
 * completes (either successfully or on cancellation).
 */
static BOOLEAN
XHCI_ReferenceEndpointForSwEnum(
    _In_opt_ PXHCI_ENDPOINT Endpoint)
{
    if (!Endpoint)
        return FALSE;

    /*
     * Increment the reference count first, then check if closing.
     * This eliminates the race window between check and increment.
     * If the endpoint is closing, we undo the increment immediately.
     */
    InterlockedIncrement(&Endpoint->SwEnumRefCount);

    if (InterlockedCompareExchange(&Endpoint->Closing, 0, 0) != 0)
    {
        /* Endpoint is closing - undo the increment and reject the reference */
        InterlockedDecrement(&Endpoint->SwEnumRefCount);
        return FALSE;
    }

    return TRUE;
}

/*
 * XHCI_DereferenceEndpointForSwEnum - Release a reference for SW-enum work.
 *
 * This function atomically decrements the endpoint's SwEnumRefCount.
 * When the count reaches zero and the endpoint is closing, ClosePipe
 * will be able to proceed with cleanup.
 */
static VOID
XHCI_DereferenceEndpointForSwEnum(
    _In_opt_ PXHCI_ENDPOINT Endpoint)
{
    if (!Endpoint)
        return;

    InterlockedDecrement(&Endpoint->SwEnumRefCount);
}

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
static VOID XHCI_FlushDeferredCompletionsForSlot(PXHCI_EXTENSION Extension, UCHAR SlotId);
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
static MEMORY_CACHING_TYPE XHCI_GetDmaCacheType(PXHCI_EXTENSION Extension);
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
#if !defined(_M_ARM64)
static BOOLEAN XHCI_WritePciConfig(PXHCI_EXTENSION Extension, ULONG Offset, PVOID Buffer, ULONG Length);
#endif
static VOID XHCI_BuildProtocolPortMap(PXHCI_EXTENSION Extension);
static volatile ULONG *XHCI_GetPortPowerRegister(PXHCI_EXTENSION Extension, USHORT Port);
static VOID XHCI_ConfigurePortLpm(PXHCI_EXTENSION Extension, USHORT Port);
static VOID XHCI_ConfigureAllPortsLpm(PXHCI_EXTENSION Extension);
static VOID XHCI_SuspendPorts(PXHCI_EXTENSION Extension);
static VOID XHCI_ResumePorts(PXHCI_EXTENSION Extension);
static VOID XHCI_ReprogramControllerState(PXHCI_EXTENSION Extension);
static ULONG XHCI_GetMaxStreamId(PXHCI_EXTENSION Extension);
static MPSTATUS XHCI_AllocateStreamResources(PXHCI_EXTENSION Extension,
                                             PXHCI_ENDPOINT Endpoint,
                                             USHORT MaxStreamId);
static VOID XHCI_FreeStreamResources(PXHCI_ENDPOINT Endpoint);
static PXHCI_RING XHCI_SelectStreamRing(PXHCI_ENDPOINT Endpoint,
                                        USHORT StreamId);
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
                                  USB_MINIPORT_FLAGS_WAKE_SUPPORT;

    /*
     * SuperSpeed Periodic Bandwidth Budgeting
     *
     * Per xHCI spec, SuperSpeed uses 125us microframes (same as USB 2.0 high-speed).
     * However, SuperSpeed has vastly higher raw bandwidth:
     * - USB 2.0 High-Speed: 480 Mbps = 400000 bits/ms (1000us frame)
     * - USB 3.0 SuperSpeed: 5 Gbps = 5000000 bits/ms
     * - USB 3.1 SuperSpeed+: 10 Gbps = 10000000 bits/ms
     *
     * For periodic bandwidth allocation, typical policy reserves 80% for
     * periodic transfers, leaving 20% for control/bulk.
     *
     * USBPORT's scheduler uses MiniPortBusBandwidth to track periodic
     * bandwidth consumption. We report a USB3-appropriate value that
     * reflects the higher bandwidth capacity while remaining compatible
     * with USBPORT's existing scheduling logic.
     *
     * Calculation for USB 3.0:
     * - 5 Gbps raw = 5,000,000,000 bits/sec
     * - Per microframe (125us): 5,000,000,000 * 0.000125 = 625,000 bits
     * - 80% for periodic: 500,000 bits per microframe
     * - Expressed in USBPORT units (bits per 1ms): 500,000 * 8 = 4,000,000
     *
     * For compatibility with USBPORT's existing allocator that expects
     * USB 2.0-scale values, we use a scaled value that allows proper
     * scheduling while not overflowing internal counters.
     */
#define XHCI_SUPERSPEED_BUS_BANDWIDTH   4000000  /* USB 3.0: ~4M bits/ms (80% of 5 Gbps) */

    XhciRegPacket.MiniPortBusBandwidth = XHCI_SUPERSPEED_BUS_BANDWIDTH;

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

    DPRINT("usbxhci: registering xHCI miniport ver=%lu flags=%08lx ext=%Iu ep=%Iu xfer=%Iu res=%Iu\n",
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
        /* MMIO read; avoid cached/stale values in tight wait loops. */
        ULONG value = READ_REGISTER_ULONG((PULONG)Reg);

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

    PortValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);
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

    OldValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);
    
    /*
     * "Clean Slate" Safe Write for Modern Intel xHCI (Ice/Tiger/Alder Lake):
     * 1. Preserve ONLY the Read/Write (RW) bits that hold configuration state.
     *    - PP (Port Power)
     *    - PLS (Port Link State)
     *    - PIC (Port Indicator Control)
     *    - Wake Bits (WCE, WDE, WOE)
     * 2. Action bits (PED, PR, WPR) must be 0 to avoid side effects.
     * 3. Unacknowledged Change bits must be 0 (writing 1 clears them).
     * 4. Speed is Read-Only, writing back is harmless/ignored.
     */
    ValueToWrite = OldValue & (XHCI_PORTSC_PP | 
                               XHCI_PORTSC_PLS_MASK | 
                               XHCI_PORTSC_PIC_MASK | 
                               XHCI_PORTSC_WCE | 
                               XHCI_PORTSC_WDE | 
                               XHCI_PORTSC_WOE);

    /* 5. Set the Change Bits we explicitly want to ACK (Write-1-to-Clear) */
    ValueToWrite |= (ChangeMask & XHCI_PORTSC_CHANGE_MASK);

    XHCI_WRITE_REGISTER_ULONG(PortStatusReg, ValueToWrite);

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

    Value = XHCI_READ_REGISTER_ULONG(PortStatusReg);

    /*
     * "Clean Slate" Safe Write:
     * Start with only the preserved configuration state from the current value.
     */
    NewValue = Value & (XHCI_PORTSC_PP | 
                        XHCI_PORTSC_PLS_MASK | 
                        XHCI_PORTSC_PIC_MASK | 
                        XHCI_PORTSC_WCE | 
                        XHCI_PORTSC_WDE | 
                        XHCI_PORTSC_WOE);

    /* Apply caller's modifications to the preserved state */
    NewValue |= SetMask;
    NewValue &= ~ClearMask;

    /*
     * Safety Check for Action Bits:
     * If the caller EXPLICITLY set an action bit in SetMask, allow it.
     * Otherwise, ensure they remain 0. (The Clean Slate init above acts as the default 0).
     */
    
    /* PED: Only allow 1 if explicitly setting it (implies Disable) */
    if (!(SetMask & XHCI_PORTSC_PED))
    {
        NewValue &= ~XHCI_PORTSC_PED;
    }

    /* PR: Only allow 1 if explicitly requesting Reset */
    if (!(SetMask & XHCI_PORTSC_PR))
    {
        NewValue &= ~XHCI_PORTSC_PR;
    }

    /* WPR: Only allow 1 if explicitly requesting Warm Reset */
    if (!(SetMask & XHCI_PORTSC_WPR))
    {
        NewValue &= ~XHCI_PORTSC_WPR;
    }

    /* Add in the ACK bits (Write-1-to-Clear) */
    NewValue |= (AckMask & XHCI_PORTSC_CHANGE_MASK);

    if (Port == 5)
        DPRINT("ModBits: P%u Read=%08lx Writing=%08lx\n", Port, Value, NewValue & XHCI_PORTSC_WRITE_MASK);
    XHCI_WRITE_REGISTER_ULONG(PortStatusReg, NewValue & XHCI_PORTSC_WRITE_MASK);

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

    Value = XHCI_READ_REGISTER_ULONG(PortStatusReg);
    
    /* Clear PLS field */
    NewValue = Value & ~XHCI_PORTSC_PLS_MASK;
    
    /* Set new PLS and LWS (Link Write Strobe) to activate it */
    NewValue |= XHCI_PORTSC_PLS(LinkState);
    NewValue |= XHCI_PORTSC_LWS;
    
    /* 
     * Safety: Mask out PED to prevent accidental disable.
     * Mask out PR/WPR to prevent accidental reset.
     */
    NewValue &= ~XHCI_PORTSC_PED;
    NewValue &= ~XHCI_PORTSC_PR;
    NewValue &= ~XHCI_PORTSC_WPR;

    /* Don't ACK any changes implicitly */
    NewValue &= ~XHCI_PORTSC_CHANGE_MASK;

    if (Port == 5)
        DPRINT("ModBits: P%u Read=%08lx Writing=%08lx\n", Port, Value, NewValue & XHCI_PORTSC_WRITE_MASK);
    XHCI_WRITE_REGISTER_ULONG(PortStatusReg, NewValue & XHCI_PORTSC_WRITE_MASK);

    return MP_STATUS_SUCCESS;
}

static
VOID
XHCI_SetPortWakeBits(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port,
    _In_ BOOLEAN Enable)
{
    ULONG WakeMask = XHCI_PORTSC_WCE | XHCI_PORTSC_WDE | XHCI_PORTSC_WOE;

    if (!Extension)
        return;

    if (Enable)
        (void)XHCI_ModifyPortBits(Extension, Port, WakeMask, 0, 0);
    else
        (void)XHCI_ModifyPortBits(Extension, Port, 0, WakeMask, 0);
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

    /* Check ACPI _OSC policy - firmware may control port power */
    if (!XHCI_ShouldControlPortPower(Extension))
        return;

    for (Port = 1; Port <= Extension->NumberOfPorts; Port++)
    {
        XHCI_RH_SetFeaturePortPower(Extension, Port);
    }
}

static
VOID
XHCI_SetPortLpmTimeouts(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port,
    _In_ ULONG U1Timeout,
    _In_ ULONG U2Timeout)
{
    volatile ULONG *PortPmReg;
    ULONG Value;
    ULONG NewValue;

    if (!Extension)
        return;

    if (Extension->Quirks & XHCI_QUIRK_LIMIT_U1U2)
        return;

    if (!XHCI_PortIsSuperSpeed(Extension, Port))
        return;

    PortPmReg = XHCI_GetPortPowerRegister(Extension, Port);
    if (!PortPmReg)
        return;

    Value = XHCI_READ_REGISTER_ULONG(PortPmReg);
    NewValue = Value;

    NewValue &= ~(XHCI_PORTPMSC_U1_TIMEOUT_MASK |
                  XHCI_PORTPMSC_U2_TIMEOUT_MASK);

    if (U1Timeout != 0)
        NewValue |= (U1Timeout << XHCI_PORTPMSC_U1_TIMEOUT_SHIFT);
    if (U2Timeout != 0)
        NewValue |= (U2Timeout << XHCI_PORTPMSC_U2_TIMEOUT_SHIFT);

    if (NewValue != Value)
        XHCI_WRITE_REGISTER_ULONG(PortPmReg, NewValue);
}

static
VOID
XHCI_ConfigurePortLpm(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ USHORT Port)
{
    ULONG U1Timeout;
    ULONG U2Timeout;

    if (!Extension)
        return;

    /* Do not enable U1/U2 on controllers that advertise limited support. */
    if (Extension->Quirks & XHCI_QUIRK_LIMIT_U1U2)
        return;

    /* Enforce ACPI _OSC policy: only manage U1/U2 if firmware granted control. */
    if (!XHCI_ShouldManageU1U2(Extension))
        return;

    /* Only SuperSpeed ports implement the U1/U2 timeout fields. */
    if (!XHCI_PortIsSuperSpeed(Extension, Port))
        return;

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

    XHCI_SetPortLpmTimeouts(Extension, Port, U1Timeout, U2Timeout);
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
VOID
XHCI_SuspendPorts(
    _In_ PXHCI_EXTENSION Extension)
{
    USHORT Port;
    ULONG SlotId;

    if (!Extension)
        return;

    if (!XHCI_ShouldManagePowerStates(Extension))
        return;

    /*
     * Before placing ports in U3, stop all endpoint rings to prevent
     * the controller from accessing device context memory while
     * transitioning to low power. This avoids data corruption and
     * ensures clean suspend state.
     */
    for (SlotId = 1; SlotId <= Extension->MaxSlots; SlotId++)
    {
        PXHCI_DEVICE_SLOT Slot = XHCI_GetSlot(Extension, (UCHAR)SlotId);
        UCHAR EpId;

        if (!Slot || !Slot->Addressed)
            continue;

        for (EpId = 1; EpId <= Slot->HighestEndpointId; EpId++)
        {
            PXHCI_ENDPOINT Endpoint = XHCI_GetSlotEndpoint(Slot, EpId);
            if (Endpoint && !Endpoint->DefaultControl)
            {
                (VOID)XHCI_StopEndpoint(Extension, Slot, EpId);
            }
        }
    }

    for (Port = 1; Port <= Extension->NumberOfPorts; Port++)
    {
        volatile ULONG *PortStatusReg = XHCI_GetPortStatusRegister(Extension, Port);
        ULONG PortValue;

        if (!PortStatusReg)
            continue;

        PortValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);
        if ((PortValue & XHCI_PORTSC_CCS) == 0 ||
            (PortValue & XHCI_PORTSC_PED) == 0)
        {
            continue;
        }

        XHCI_SetPortWakeBits(Extension, Port, TRUE);
        (VOID)XHCI_SetPortLinkState(Extension, Port, PORT_LINK_STATE_U3);
    }
}

static
VOID
XHCI_ResumePorts(
    _In_ PXHCI_EXTENSION Extension)
{
    USHORT Port;

    if (!Extension)
        return;

    if (!XHCI_ShouldManagePowerStates(Extension))
        return;

    for (Port = 1; Port <= Extension->NumberOfPorts; Port++)
    {
        XHCI_SetPortWakeBits(Extension, Port, FALSE);
    }

    XHCI_ConfigureAllPortsLpm(Extension);
}

static
VOID
XHCI_ApplyEndpointLpmPolicy(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ const USBPORT_ENDPOINT_PROPERTIES *EndpointProperties)
{
    ULONG LpmInfo;
    ULONG U1Timeout;
    ULONG U2Timeout;
    UCHAR U1ExitLatency;
    USHORT U2ExitLatency;
    BOOLEAN AllowU1;
    BOOLEAN AllowU2;

    if (!Extension || !EndpointProperties)
        return;

    if (EndpointProperties->DeviceSpeed != UsbSuperSpeed)
        return;

    LpmInfo = EndpointProperties->Reserved3;
    if ((LpmInfo & USBPORT_EP_LPM_VALID) == 0)
        return;

    if (Extension->Quirks & XHCI_QUIRK_LIMIT_U1U2)
        return;

    if (Extension->MaxU1ExitLatency == 0 && Extension->MaxU2ExitLatency == 0)
        return;

    AllowU1 = (LpmInfo & USBPORT_EP_LPM_ALLOW_U1) != 0;
    AllowU2 = (LpmInfo & USBPORT_EP_LPM_ALLOW_U2) != 0;
    U1ExitLatency = (UCHAR)((LpmInfo & USBPORT_EP_LPM_U1_MASK) >> USBPORT_EP_LPM_U1_SHIFT);
    U2ExitLatency = (USHORT)((LpmInfo & USBPORT_EP_LPM_U2_MASK) >> USBPORT_EP_LPM_U2_SHIFT);

    U1Timeout = 0;
    U2Timeout = 0;

    if (AllowU1 && Extension->MaxU1ExitLatency != 0)
    {
        U1Timeout = (U1ExitLatency != 0) ? U1ExitLatency : 1;
        if (U1Timeout > Extension->MaxU1ExitLatency)
            U1Timeout = Extension->MaxU1ExitLatency;
        if (U1Timeout > 0xFF)
            U1Timeout = 0xFF;
    }

    if (AllowU2 && Extension->MaxU2ExitLatency != 0)
    {
        U2Timeout = (U2ExitLatency != 0) ? U2ExitLatency : 1;
        if (U2Timeout > Extension->MaxU2ExitLatency)
            U2Timeout = Extension->MaxU2ExitLatency;
        if (U2Timeout > 0xFFFF)
            U2Timeout = 0xFFFF;
    }

    XHCI_SetPortLpmTimeouts(Extension,
                            EndpointProperties->PortNumber,
                            U1Timeout,
                            U2Timeout);
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

    DPRINT("usbxhci: programming DCBAA=%08lx:%08lx CRCR=%08lx:%08lx\n",
            DcbaaHigh, DcbaaLow, CrcrHigh, CrcrLow);
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->DcbaapLow, DcbaaLow);
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->DcbaapHigh, DcbaaHigh);
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->CrCrLow, CrcrLow);
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->CrCrHigh, CrcrHigh);
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->Config,
                         Extension->MaxSlots);

#if DBG
    {
        ULONGLONG HwDcbaa;

        HwDcbaa = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->DcbaapHigh) << 32) |
                  XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->DcbaapLow);

        if (HwDcbaa != Dcbaa)
        {
            DPRINT1("usbxhci: DCBAA mismatch after program "
                    "(expected DCBAA=%I64x, hw DCBAA=%I64x)\n",
                    Dcbaa,
                    HwDcbaa);
            ASSERT(HwDcbaa == Dcbaa);
        }
        /*
         * Note: CRCR (Command Ring Control Register) bits 6-63 are write-only
         * per xHCI spec section 5.4.5. The pointer reads as 0 when the Command
         * Ring is stopped (CRR=0). Do not assert on CRCR readback mismatch.
         */
    }
#endif

    DPRINT("usbxhci: USBCMD=%08lx USBSTS=%08lx CONFIG=%08lx\n",
            XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd),
            XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts),
            XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->Config));

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
    Supported = XHCI_READ_REGISTER_ULONG(PageSizeReg);

    if ((Supported & XHCI_PAGE_SIZE_4K) == 0)
    {
        DPRINT1("usbxhci: controller lacks 4KB page-size support (PS=0x%08lx)\n",
                Supported);
        return MP_STATUS_NOT_SUPPORTED;
    }

    XHCI_WRITE_REGISTER_ULONG(PageSizeReg, XHCI_PAGE_SIZE_4K);
    Extension->ConfiguredPageSize = XHCI_PAGE_SIZE_4K;
    DPRINT("usbxhci: configured page size mask=0x%08lx\n",
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

    PortValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);

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
    XHCI_WRITE_REGISTER_ULONG(&Extension->DoorbellArray->Doorbell[SlotIndex], Value);
    /*
     * Flush PCI posted writes by reading back the doorbell register.
     * This is critical for VirtualBox UEFI mode where the doorbell write
     * may be buffered and not actually delivered to the xHCI controller
     * until a read forces the write to complete. Without this flush,
     * the controller never sees the doorbell ring and the transfer hangs.
     * Linux does the same flush after every doorbell write.
     */
    (void)XHCI_READ_REGISTER_ULONG(&Extension->DoorbellArray->Doorbell[SlotIndex]);
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

/**
 * @brief Calculate available space in a transfer ring.
 *
 * Returns the number of TRBs available for enqueuing without overwriting
 * the dequeue pointer. The calculation accounts for the Link TRB at the
 * end of the ring which cannot be used for transfers.
 *
 * Formula: (DequeueIndex - EnqueueIndex - 1 + UsableTrbs) % UsableTrbs
 * where UsableTrbs = TrbCount - 1 (excluding Link TRB)
 *
 * @param Ring Pointer to the transfer ring
 * @return Number of available TRB slots, or 0 if ring is full/invalid
 */
static
ULONG
XHCI_GetRingAvailableSpace(
    _In_ const XHCI_RING *Ring)
{
    ULONG UsableTrbs;
    ULONG Available;

    if (!Ring || Ring->TrbCount < 2)
        return 0;

    /* Last TRB is reserved for the Link TRB */
    UsableTrbs = Ring->TrbCount - 1;

    /*
     * Calculate distance from enqueue to dequeue.
     * When EnqueueIndex == DequeueIndex, the ring is empty (all slots free).
     * We subtract 1 to ensure we never overwrite the dequeue position.
     */
    if (Ring->EnqueueIndex < Ring->DequeueIndex)
    {
        Available = Ring->DequeueIndex - Ring->EnqueueIndex - 1;
    }
    else
    {
        /* Enqueue is at or past dequeue - wrap around */
        Available = (UsableTrbs - Ring->EnqueueIndex) + Ring->DequeueIndex;
        if (Available > 0)
            Available--;
    }

    return Available;
}

/**
 * @brief Check if ring has enough space for a transfer descriptor.
 *
 * @param Ring Pointer to the transfer ring
 * @param TrbsNeeded Number of TRBs required for the TD
 * @return TRUE if space is available, FALSE otherwise
 */
static
BOOLEAN
XHCI_RingHasSpace(
    _In_ const XHCI_RING *Ring,
    _In_ ULONG TrbsNeeded)
{
    ULONG Available;

    if (!Ring || TrbsNeeded == 0)
        return FALSE;

    Available = XHCI_GetRingAvailableSpace(Ring);
    return (Available >= TrbsNeeded);
}

/**
 * @brief Estimate number of TRBs needed for a transfer.
 *
 * This provides a conservative upper bound on TRBs required, accounting for:
 * - Control transfers: Setup + Data (variable) + Status = at least 2-3 TRBs
 * - Bulk/Interrupt: Multiple TRBs based on transfer length and 64KB boundary splits
 *
 * Note: Currently used for documentation and future pre-allocation checks.
 * The actual ring space check happens in XHCI_GetTransferRingTrb.
 *
 * @param TransferLength Total bytes to transfer
 * @param IsControl TRUE if this is a control transfer
 * @return Estimated number of TRBs needed
 */
static
ULONG
__attribute__((unused))
XHCI_EstimateTrbsNeeded(
    _In_ ULONG TransferLength,
    _In_ BOOLEAN IsControl)
{
    ULONG TrbsNeeded;

    if (TransferLength == 0)
    {
        /* Zero-length transfer needs 1 TRB (+ setup/status for control) */
        return IsControl ? 3 : 1;
    }

    /*
     * Each TRB can transfer up to 64KB, but we may need extra TRBs for:
     * - 64KB boundary crossing in source buffer (split required)
     * - Conservative estimate: assume worst case of 2 TRBs per 64KB
     */
    TrbsNeeded = (TransferLength + XHCI_MAX_TRB_TRANSFER_LENGTH - 1) /
                 XHCI_MAX_TRB_TRANSFER_LENGTH;

    /* Add margin for 64KB boundary splits - worst case doubles TRB count */
    TrbsNeeded = TrbsNeeded * 2;

    /* Minimum of 1 data TRB */
    if (TrbsNeeded == 0)
        TrbsNeeded = 1;

    /* Control transfers need Setup + Data + Status */
    if (IsControl)
        TrbsNeeded += 2;

    return TrbsNeeded;
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

    /* Check if ring has space for at least one TRB */
    if (!XHCI_RingHasSpace(Ring, 1))
    {
        DPRINT1("usbxhci: transfer ring full (enq=%lu deq=%lu count=%lu)\n",
                Ring->EnqueueIndex, Ring->DequeueIndex, Ring->TrbCount);
        return NULL;
    }

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
XHCI_ResetRing(
    _Inout_ PXHCI_RING Ring)
{
    PXHCI_TRB LinkTrb;
    ULONGLONG LinkAddress;

    if (!Ring || !Ring->Base || Ring->TrbCount == 0)
        return;

    Ring->EnqueueIndex = 0;
    Ring->DequeueIndex = 0;
    Ring->CycleState = 1;

    /*
     * Zero ALL TRBs (including the Link slot) first.  Old TRBs from
     * previous transfers still carry CycleState == 1, which matches the
     * reset CycleState.  If we only touch the Link TRB, the xHC will
     * see stale TRBs with a matching cycle bit and process them after
     * SET_TR_DEQUEUE + doorbell, generating spurious transfer events
     * that cannot be matched to any active transfer (resulting in
     * "has no active transfer" errors and an infinite stall).
     */
    RtlZeroMemory(Ring->Base, Ring->TrbCount * sizeof(XHCI_TRB));

    LinkTrb = &Ring->Base[Ring->TrbCount - 1];
    LinkAddress = Ring->PhysicalAddress.QuadPart;

    LinkTrb->Parameter1 = (ULONG)(LinkAddress & 0xFFFFFFFF);
    LinkTrb->Parameter2 = (ULONG)(LinkAddress >> 32);
    LinkTrb->Status = 0;
    LinkTrb->Control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                       XHCI_TRB_TOGGLE_CYCLE |
                       XHCI_TRB_CYCLE;
}

static
VOID
XHCI_ResetEndpointRing(
    _Inout_ PXHCI_ENDPOINT Endpoint)
{
    ULONG StreamId;

    if (!Endpoint)
        return;

    XHCI_ResetRing(&Endpoint->TransferRing);

    if (Endpoint->StreamsEnabled && Endpoint->StreamRings)
    {
        for (StreamId = 1; StreamId <= Endpoint->StreamRingCount; StreamId++)
        {
            PXHCI_RING RingPtr = &Endpoint->StreamRings[StreamId];
            XHCI_ResetRing(RingPtr);

            if (Endpoint->StreamContexts &&
                StreamId <= Endpoint->StreamRingCount)
            {
                PXHCI_STREAM_CONTEXT StreamCtx = &Endpoint->StreamContexts[StreamId];
                StreamCtx->StreamInfo = XHCI_STREAM_CTX_TYPE_PRIMARY;
                StreamCtx->TrDequeuePointer =
                    (RingPtr->PhysicalAddress.QuadPart & ~0xFULL) |
                    (RingPtr->CycleState & 0x1);
            }
        }
    }
}

static
ULONG
XHCI_GetMaxStreamId(
    _In_ PXHCI_EXTENSION Extension)
{
    ULONG MaxStreams;

    if (!Extension || Extension->MaxPrimaryStreams == 0)
        return 0;

    if (Extension->MaxPrimaryStreams >= 16)
        return 0;

    MaxStreams = 1u << Extension->MaxPrimaryStreams;
    if (MaxStreams == 0)
        return 0;

    return MaxStreams;
}

static
MPSTATUS
XHCI_AllocateStreamResources(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _In_ USHORT MaxStreamId)
{
    SIZE_T StreamRingCount;
    SIZE_T ContextSize;
    SIZE_T AllocationSize;
    USHORT StreamId;
    PVOID StreamCtxBuffer;

    if (!Extension || !Endpoint || MaxStreamId == 0)
        return MP_STATUS_ERROR;

    StreamRingCount = (SIZE_T)MaxStreamId + 1;
    ContextSize = sizeof(XHCI_STREAM_CONTEXT) * StreamRingCount;
    AllocationSize = XHCI_ALIGN_UP(ContextSize, 64);

    {
        PHYSICAL_ADDRESS LowAddress;
        PHYSICAL_ADDRESS HighAddress;
        PHYSICAL_ADDRESS BoundaryAddress;

        LowAddress.QuadPart = 0;
        /*
         * xHCI spec requires Stream Context Array to be 64-byte aligned.
         * Use BoundaryAddress (alignment constraint) instead of SkipBytes.
         * MmAllocateContiguousMemorySpecifyCache with BoundaryAddress ensures
         * the allocation doesn't cross a boundary of that size, effectively
         * guaranteeing alignment to 64 bytes when the size is a multiple of 64.
         */
        BoundaryAddress.QuadPart = 64;
        if (Extension && Extension->Supports64Bit &&
            !(Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA))
        {
            HighAddress.QuadPart = 0xFFFFFFFFFFFFFFFFULL;
        }
        else
        {
            HighAddress.QuadPart = 0xFFFFFFFFULL;
        }

        StreamCtxBuffer = MmAllocateContiguousMemorySpecifyCache(AllocationSize,
                                                                LowAddress,
                                                                HighAddress,
                                                                BoundaryAddress,
                                                                XHCI_GetDmaCacheType(Extension));
    }
    if (!StreamCtxBuffer)
        return MP_STATUS_NO_RESOURCES;

    /* Verify 64-byte alignment (should be guaranteed by BoundaryAddress) */
    if (((ULONG_PTR)StreamCtxBuffer & 0x3F) != 0)
    {
        DPRINT1("usbxhci: Stream context buffer not 64-byte aligned: %p\n",
                StreamCtxBuffer);
        MmFreeContiguousMemory(StreamCtxBuffer);
        return MP_STATUS_NO_RESOURCES;
    }

    RtlZeroMemory(StreamCtxBuffer, AllocationSize);

    Endpoint->StreamRings = ExAllocatePoolWithTag(NonPagedPool,
                                                  sizeof(XHCI_RING) * StreamRingCount,
                                                  XHCI_TAG);
    if (!Endpoint->StreamRings)
    {
        MmFreeContiguousMemory(StreamCtxBuffer);
        return MP_STATUS_NO_RESOURCES;
    }

    RtlZeroMemory(Endpoint->StreamRings, sizeof(XHCI_RING) * StreamRingCount);

    Endpoint->StreamContexts = (PXHCI_STREAM_CONTEXT)StreamCtxBuffer;
    Endpoint->StreamContextsPhysical = MmGetPhysicalAddress(StreamCtxBuffer);
    Endpoint->StreamRingCount = MaxStreamId;

    for (StreamId = 1; StreamId <= MaxStreamId; StreamId++)
    {
        MPSTATUS Status = XHCI_AllocateTransferRing(Extension,
                                                    XHCI_EXTERNAL_EP_RING_TRBS,
                                                    FALSE,
                                                    &Endpoint->StreamRings[StreamId]);
        if (Status != MP_STATUS_SUCCESS)
        {
            Endpoint->StreamRingCount = StreamId - 1;
            XHCI_FreeStreamResources(Endpoint);
            return Status;
        }
    }

    Endpoint->StreamsEnabled = TRUE;
    Endpoint->ReservedStreamId = MaxStreamId;
    Endpoint->MaxStreamId = MaxStreamId;

    XHCI_ResetEndpointRing(Endpoint);

    return MP_STATUS_SUCCESS;
}

static
VOID
XHCI_FreeStreamResources(
    _Inout_ PXHCI_ENDPOINT Endpoint)
{
    ULONG StreamId;

    if (!Endpoint)
        return;

    if (Endpoint->StreamRings && Endpoint->StreamRingCount != 0)
    {
        for (StreamId = 1; StreamId <= Endpoint->StreamRingCount; StreamId++)
        {
            XHCI_FreeTransferRing(&Endpoint->StreamRings[StreamId]);
        }
    }

    if (Endpoint->StreamRings)
    {
        ExFreePoolWithTag(Endpoint->StreamRings, XHCI_TAG);
        Endpoint->StreamRings = NULL;
    }

    if (Endpoint->StreamContexts)
    {
        MmFreeContiguousMemory(Endpoint->StreamContexts);
        Endpoint->StreamContexts = NULL;
        Endpoint->StreamContextsPhysical.QuadPart = 0;
    }

    Endpoint->StreamRingCount = 0;
    Endpoint->ReservedStreamId = 0;
    Endpoint->MaxStreamId = 0;
    Endpoint->StreamsEnabled = FALSE;
}

static
PXHCI_RING
XHCI_SelectStreamRing(
    _In_ PXHCI_ENDPOINT Endpoint,
    _In_ USHORT StreamId)
{
    if (!Endpoint)
        return NULL;

    if (!Endpoint->StreamsEnabled || StreamId == 0 ||
        StreamId > Endpoint->StreamRingCount)
    {
        return &Endpoint->TransferRing;
    }

    return &Endpoint->StreamRings[StreamId];
}

static
ULONGLONG
XHCI_GetEndpointDequeuePointer(
    _In_ PXHCI_ENDPOINT Endpoint)
{
    if (!Endpoint)
        return 0;

    if (Endpoint->StreamsEnabled &&
        Endpoint->StreamContextsPhysical.QuadPart != 0)
    {
        return (Endpoint->StreamContextsPhysical.QuadPart & ~0xFULL) |
               XHCI_EPCTX_LSA;
    }

    return ((Endpoint->TransferRing.PhysicalAddress.QuadPart +
             ((ULONGLONG)Endpoint->TransferRing.DequeueIndex * sizeof(XHCI_TRB))) & ~0xFULL) |
           (Endpoint->TransferRing.CycleState & 0x1);
}

static MEMORY_CACHING_TYPE
XHCI_GetDmaCacheType(
    _In_opt_ PXHCI_EXTENSION Extension)
{
    if (Extension && (Extension->Quirks & XHCI_QUIRK_NON_COHERENT_DMA))
        return MmNonCached;

    return MmCached;
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
    MEMORY_CACHING_TYPE CacheType;

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
    /*
     * Use cached memory on coherent platforms; non-cached is opt-in for
     * non-coherent DMA configurations.
     */
    CacheType = XHCI_GetDmaCacheType(Extension);
    Buffer = MmAllocateContiguousMemorySpecifyCache(Length,
                                                    LowAddress,
                                                    HighAddress,
                                                    SkipBytes,
                                                    CacheType);
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

    ExpandAddFlags = Slot->Configured &&
                     (Slot->HighestEndpointId != 0) &&
                     (EndpointId < Slot->HighestEndpointId);
    if (ExpandAddFlags)
    {
        UCHAR Id;
        UCHAR StartId = (UCHAR)(EndpointId + 1);

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

    /*
     * Use DequeueIndex (not EnqueueIndex) when reconfiguring an active endpoint.
     * The dequeue pointer must point to where the controller should resume reading,
     * not where new TRBs are being added. Using EnqueueIndex would skip all pending
     * transfers that are queued between DequeueIndex and EnqueueIndex, causing those
     * transfers to be silently lost.
     */
    DequeuePtr = XHCI_GetEndpointDequeuePointer(Endpoint);

    XhciEndpointContextInit(EpCtx,
                            EndpointType,
                            MaxPacketSize,
                            BurstSize,
                            Interval,
                            Mult,
                            MaxEsitPayload,
                            AverageTrbLength,
                            DequeuePtr);

    /*
     * For stream-enabled endpoints, set MaxPStreams in EpInfo.
     * This tells the xHCI hardware how to index the Stream Context Array.
     * MaxPStreams = log2(MaxStreamId + 1) where MaxStreamId is a power of 2.
     * The LSA bit is already set in the dequeue pointer by XHCI_GetEndpointDequeuePointer.
     */
    if (Endpoint->StreamsEnabled && Endpoint->MaxStreamId != 0)
    {
        ULONG MaxPStreams = 0;
        USHORT StreamCount = Endpoint->MaxStreamId;

        /* Calculate log2(StreamCount + 1) - StreamCount is already validated as power of 2 */
        while (StreamCount > 0)
        {
            MaxPStreams++;
            StreamCount >>= 1;
        }

        EpCtx->EpInfo |= ((MaxPStreams & 0x1F) << XHCI_EPCTX_MAX_PSTREAMS_SHIFT);
        EpCtx->EpInfo |= XHCI_EPCTX_LSA_FLAG;

        DPRINT1("usbxhci: slot %u ep %u streams enabled: MaxStreamId=%u MaxPStreams=%lu\n",
                Slot->SlotId, EndpointId, Endpoint->MaxStreamId, MaxPStreams);
    }

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

            /* Same as above: use DequeueIndex to preserve pending transfers */
            ExistingDequeuePtr = XHCI_GetEndpointDequeuePointer(ExistingEndpoint);

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
    /*
     * All endpoints route to interrupter 0 until per-interrupter event rings
     * are implemented. See TODO_XHCI.md for multi-interrupter support status.
     */
    Endpoint->InterruptTarget = 0;

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
    /*
     * Do NOT clear EndpointTable[EndpointId] here.  Callers are
     * responsible for NULLing the entry after verifying that the
     * table still points to the endpoint being closed.  During
     * interface reconfiguration, OpenEndpoint (running on another
     * CPU without MiniportSpinLock) may have already replaced the
     * entry with a new endpoint while we were busy-polling for
     * the CONFIG_EP drop completion.
     */
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

    /*
     * Per xHCI spec section 6.4.3.9, the Set TR Dequeue Pointer TRB format:
     * - Bits 63:4: New TR Dequeue Pointer (16-byte aligned)
     * - Bits 3:1: Reserved
     * - Bit 0: Dequeue Cycle State (DCS)
     *
     * The DCS must be encoded in bit 0 of the parameter, not in a separate field.
     */
    ULONGLONG Dequeue = (Ring->PhysicalAddress.QuadPart & ~0xFULL) |
                        (Ring->CycleState & 0x1);

    return XHCI_SendCommand(Extension,
                            XHCI_TRB_TYPE_SET_DEQ,
                            Dequeue,
                            0,  /* Context/Status is reserved for this command */
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
    ULONG EpState = XHCI_EPCTX_STATE_DISABLED;
    PVOID DevCtx;
    BOOLEAN CanSetDequeue = FALSE;

    if (!Extension || !Endpoint || !Endpoint->Slot)
        return;

    if (Extension->FatalError || Extension->StoppingOrRemoved)
        return;

    /*
     * Do not issue any commands if the slot has already been disabled or
     * is being disabled. The xHC rejects commands for disabled slots with
     * completion code 11 (SLOT_NOT_ENABLED), which generates event ring
     * activity that keeps IMAN.IP asserted, potentially causing an
     * interrupt storm on legacy INTx.
     */
    if (!Endpoint->Slot->InUse || Endpoint->Slot->DisablePending)
    {
        DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u slot disabled (InUse=%u DisablePending=%u), bailing out\n",
                Endpoint->SlotId,
                Endpoint->EndpointId,
                Endpoint->Slot->InUse,
                Endpoint->Slot->DisablePending);
        return;
    }

    /*
     * Do NOT clear ActiveTransfer here.  SetEndpointDataToggle already
     * reset the software ring (zeroing TRBs so the xHC won't process
     * stale data).  Clearing ActiveTransfer at this point would lose
     * any transfer that USBPORT submitted between the reset request
     * and this reset execution, causing "has no active transfer" stalls.
     * The post-reset clear at the end of this function handles truly
     * stale transfers.
     */

    /* Read current endpoint state to determine correct command sequence.
     * Per xHCI spec:
     * - Halted state: Issue Reset Endpoint (Halted -> Stopped)
     * - Running state: Issue Stop Endpoint (Running -> Stopped)
     * - Stopped state: Already stopped, proceed to Set TR Dequeue
     */
    DevCtx = Endpoint->Slot->DeviceContext.VirtualAddress;
    if (DevCtx)
    {
        PXHCI_ENDPOINT_CONTEXT EpCtx = XHCI_GetDeviceEndpointContextVa(
            Extension, DevCtx, Endpoint->EndpointId - 1);
        if (EpCtx)
            EpState = EpCtx->EpInfo & XHCI_EPCTX_STATE_MASK;
    }

    DPRINT1("usbxhci: EndpointResetSequence ENTRY slot=%u ep=%u state=%lu (0=Dis,1=Run,2=Halt,3=Stop,4=Err)\n",
            Endpoint->SlotId,
            Endpoint->EndpointId,
            EpState);

    if (EpState == XHCI_EPCTX_STATE_HALTED)
    {
        MPSTATUS ResetStatus;
        /* Halted endpoint: Use Reset Endpoint command to transition to Stopped */
        DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u issuing RESET_ENDPOINT (state=Halted)\n",
                Endpoint->SlotId,
                Endpoint->EndpointId);
        ResetStatus = XHCI_ResetEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
        DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u RESET_ENDPOINT returned %lu\n",
                Endpoint->SlotId,
                Endpoint->EndpointId,
                ResetStatus);
        if (ResetStatus == MP_STATUS_SUCCESS)
            CanSetDequeue = TRUE;
    }
    else if (EpState == XHCI_EPCTX_STATE_RUNNING)
    {
        MPSTATUS StopStatus;
        /* Running endpoint: Use Stop Endpoint command to transition to Stopped */
        DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u issuing STOP_ENDPOINT (state=Running)\n",
                Endpoint->SlotId,
                Endpoint->EndpointId);
        StopStatus = XHCI_StopEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
        DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u STOP_ENDPOINT returned %lu\n",
                Endpoint->SlotId,
                Endpoint->EndpointId,
                StopStatus);
        if (StopStatus == MP_STATUS_SUCCESS)
            CanSetDequeue = TRUE;
    }
    else if (EpState == XHCI_EPCTX_STATE_STOPPED ||
             EpState == XHCI_EPCTX_STATE_ERROR)
    {
        /* Already in a state where Set TR Dequeue is valid */
        DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u already Stopped/Error, skipping cmd\n",
                Endpoint->SlotId,
                Endpoint->EndpointId);
        CanSetDequeue = TRUE;
    }
    else
    {
        DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u unexpected state=%lu, no cmd issued\n",
                Endpoint->SlotId,
                Endpoint->EndpointId,
                EpState);
    }
    /* For Disabled state, endpoint is not usable - skip Set TR Dequeue */

    /*
     * If the Stop/Reset command failed, re-read the endpoint state to see if
     * it transitioned to a valid state for Set TR Dequeue (Stopped or Error).
     * This handles races where the endpoint state changed between our initial
     * read and the command execution (e.g., transfer completed, endpoint halted).
     */
    DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u CanSetDequeue=%u after cmd\n",
            Endpoint->SlotId,
            Endpoint->EndpointId,
            CanSetDequeue);

    if (!CanSetDequeue)
    {
        DevCtx = Endpoint->Slot->DeviceContext.VirtualAddress;
        if (DevCtx)
        {
            PXHCI_ENDPOINT_CONTEXT EpCtx = XHCI_GetDeviceEndpointContextVa(
                Extension, DevCtx, Endpoint->EndpointId - 1);
            if (EpCtx)
            {
                EpState = EpCtx->EpInfo & XHCI_EPCTX_STATE_MASK;
                DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u re-read state=%lu\n",
                        Endpoint->SlotId,
                        Endpoint->EndpointId,
                        EpState);
                if (EpState == XHCI_EPCTX_STATE_STOPPED ||
                    EpState == XHCI_EPCTX_STATE_ERROR)
                {
                    CanSetDequeue = TRUE;
                    DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u state now valid for SetDequeue\n",
                            Endpoint->SlotId,
                            Endpoint->EndpointId);
                }
                else
                {
                    DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u state=%lu NOT valid for SetDequeue\n",
                            Endpoint->SlotId,
                            Endpoint->EndpointId,
                            EpState);
                }
            }
        }
    }
    /* For Stopped/Error/Disabled, no state transition command needed */

    XHCI_ResetEndpointRing(Endpoint);
    if (Endpoint->UsesStaticRing && Endpoint->Slot)
    {
        Endpoint->Slot->Ep0RingEnqueueIndex = Endpoint->TransferRing.EnqueueIndex;
        Endpoint->Slot->Ep0RingDequeueIndex = Endpoint->TransferRing.DequeueIndex;
        Endpoint->Slot->Ep0RingCycleState = Endpoint->TransferRing.CycleState;
    }

    if (Endpoint->StreamsEnabled && Endpoint->Slot)
    {
        (VOID)XHCI_ConfigureSlotEndpoint(Extension,
                                         Endpoint->Slot,
                                         Endpoint,
                                         Endpoint->EndpointId);
        CanSetDequeue = FALSE;
    }

    /*
     * After Stop/Reset and resetting the software ring, re-sync the hardware TR
     * Dequeue Pointer so the endpoint resumes from the correct ring head.
     *
     * QEMU's xHCI (1B36:000D) can wedge if SetTRDequeue is issued against the
     * static EP0 ring while the startup HCE quirk is active; skip in that case.
     */
    if (CanSetDequeue)
    {
        BOOLEAN SkipSetDequeue =
            Endpoint->UsesStaticRing &&
            Endpoint->DefaultControl &&
            ((Extension->Quirks & XHCI_QUIRK_IGNORE_STARTUP_HCE) != 0);

        if (!SkipSetDequeue)
        {
            MPSTATUS DeqStatus;
            DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u issuing SET_TR_DEQUEUE\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId);
            DeqStatus = XHCI_SetEndpointDequeue(Extension,
                                                         Endpoint->Slot,
                                                         Endpoint->EndpointId,
                                                         &Endpoint->TransferRing);
            if (DeqStatus != MP_STATUS_SUCCESS)
            {
                DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u SET_TR_DEQUEUE failed (status=%lu)\n",
                        Endpoint->SlotId,
                        Endpoint->EndpointId,
                        DeqStatus);
            }
            else
            {
                DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u SET_TR_DEQUEUE success\n",
                        Endpoint->SlotId,
                        Endpoint->EndpointId);
            }
        }
        else
        {
            DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u SET_TR_DEQUEUE skipped (QEMU quirk)\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId);
        }
    }
    else
    {
        DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u CanSetDequeue=FALSE, skipping SET_TR_DEQUEUE\n",
                Endpoint->SlotId,
                Endpoint->EndpointId);
    }

    XHCI_StartEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);

    /*
     * Clear ActiveTransfer after reset sequence completes. The endpoint
     * was stopped/reset, so any previous transfer is now aborted/invalid.
     * This prevents stale ActiveTransfer pointers from blocking new transfers.
     * Must hold Endpoint->Lock to avoid racing with transfer submission/completion.
     */
    {
        KIRQL OldIrql;
        KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
        if (Endpoint->ActiveTransfer)
        {
            DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u clearing stale ActiveTransfer %p\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId,
                    Endpoint->ActiveTransfer);
            Endpoint->ActiveTransfer = NULL;
        }
        KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
    }

    if (RingDoorbell)
    {
        DPRINT1("usbxhci: EndpointResetSequence slot=%u ep=%u ringing doorbell\n",
                Endpoint->SlotId,
                Endpoint->EndpointId);
        XHCI_RingEndpointDoorbell(Extension,
                                  Endpoint->SlotId,
                                  Endpoint->EndpointId,
                                  0);
    }

    DPRINT1("usbxhci: EndpointResetSequence EXIT slot=%u ep=%u\n",
            Endpoint->SlotId,
            Endpoint->EndpointId);
}

static
VOID
XHCI_QueueEp0StallReset(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint)
{
    PXHCI_DEVICE_SLOT Slot;
    PXHCI_EP_RESET_WORK Work;

    if (!Extension || !Endpoint || !Endpoint->DefaultControl)
        return;

    Slot = Endpoint->Slot;
    if (!Slot)
        return;

    /*
     * FIX: Check if a worker is already queued before trying to queue another.
     * The Ep0StallResetQueued flag is the authoritative indicator of whether
     * a reset worker is currently queued/running. If it's already 1, we don't
     * need to do anything - the existing worker will handle the reset.
     *
     * We check this FIRST, before setting Ep0NeedsStallReset, to avoid the
     * race where we unconditionally set the needs-reset flag even when a
     * reset just completed (which could cause redundant resets).
     *
     * If no worker is queued, we atomically claim the queued slot, set the
     * needs-reset flag, and proceed to allocate and queue the worker.
     */
    if (InterlockedCompareExchange(&Slot->Ep0StallResetQueued, 1, 0) != 0)
    {
        /* Worker already queued or running, nothing more to do */
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: slot %u EP0 stall reset worker already queued, skipping duplicate\n",
                 Endpoint->SlotId);
        return;
    }

    /*
     * We've claimed the queued slot. Now set the needs-reset flag and
     * reset the event before attempting allocation.
     */
    InterlockedExchange(&Slot->Ep0NeedsStallReset, 1);
    KeResetEvent(&Slot->Ep0StallResetEvent);

#if defined(NonPagedPoolNx)
    Work = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Work), XHCI_TAG);
#else
    Work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Work), XHCI_TAG);
#endif
    if (!Work)
    {
        InterlockedExchange(&Slot->Ep0StallResetQueued, 0);
        KeSetEvent(&Slot->Ep0StallResetEvent, IO_NO_INCREMENT, FALSE);
        return;
    }

    InterlockedIncrement(&Endpoint->PendingWorkCount);
    Work->Extension = Extension;
    Work->Endpoint = Endpoint;
    Work->RingDoorbell = FALSE;
    Work->ClearStallResetFlags = TRUE;
    ExInitializeWorkItem(&Work->Item,
                         XHCI_EndpointResetWorker,
                         Work);
    ExQueueWorkItem(&Work->Item, DelayedWorkQueue);

    XHCI_DBG(XHCI_TRACE_TRANSFERS,
             "usbxhci: queued EP0 stall reset work for slot %u\n",
             Endpoint->SlotId);
}

static
MPSTATUS
XHCI_WaitForEp0StallReset(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint)
{
    PXHCI_DEVICE_SLOT Slot;
    LARGE_INTEGER Timeout;
    NTSTATUS WaitStatus;

    if (!Extension || !Endpoint)
        return MP_STATUS_ERROR;

    Slot = Endpoint->Slot;
    if (!Slot || !Slot->Ep0NeedsStallReset)
        return MP_STATUS_SUCCESS;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return MP_STATUS_FAILURE;

    XHCI_QueueEp0StallReset(Extension, Endpoint);

    /* If nothing is queued (allocation failure), perform the reset synchronously here. */
    if (InterlockedCompareExchange(&Slot->Ep0StallResetQueued, 0, 0) == 0)
    {
        XHCI_PerformEndpointResetSequence(Extension, Endpoint, FALSE);
        InterlockedExchange(&Slot->Ep0NeedsStallReset, 0);
        KeSetEvent(&Slot->Ep0StallResetEvent, IO_NO_INCREMENT, FALSE);
        return MP_STATUS_SUCCESS;
    }

    Timeout.QuadPart = -(LONGLONG)XHCI_EP0_STALL_RESET_TIMEOUT_MS * 10000LL;
    WaitStatus = KeWaitForSingleObject(&Slot->Ep0StallResetEvent,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       &Timeout);
    if (WaitStatus != STATUS_SUCCESS)
    {
        /*
         * FIX: Timeout occurred, but the worker is likely still running.
         * We must NOT clear Ep0StallResetQueued here because:
         * 1. The worker will complete eventually and clear the flags itself
         * 2. Clearing it here would allow a second worker to be queued,
         *    causing races with the first worker still executing
         * 3. The endpoint context might be in an intermediate state
         *
         * Return failure to let the upper layer retry. On the next attempt:
         * - If the worker completed, Ep0NeedsStallReset will be 0 and we succeed
         * - If still running, we'll wait again with fresh timeout
         *
         * The PendingWorkCount ensures the endpoint won't be torn down while
         * the worker is running.
         */
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: slot %u EP0 stall reset wait timed out, worker still running\n",
                 Endpoint->SlotId);
        return MP_STATUS_FAILURE;
    }

    return MP_STATUS_SUCCESS;
}

static VOID NTAPI
XHCI_EndpointResetWorker(PVOID Context)
{
    PXHCI_EP_RESET_WORK Work = (PXHCI_EP_RESET_WORK)Context;
    LONG NewCount;

    if (!Work)
        return;

    /* Trace-level for endpoint reset worker entry - not an error condition */
    DPRINT("usbxhci: EndpointResetWorker ENTRY slot=%u ep=%u RingDoorbell=%u ClearFlags=%u\n",
            Work->Endpoint ? Work->Endpoint->SlotId : 0,
            Work->Endpoint ? Work->Endpoint->EndpointId : 0,
            Work->RingDoorbell,
            Work->ClearStallResetFlags);

    XHCI_PerformEndpointResetSequence(Work->Extension,
                                      Work->Endpoint,
                                      Work->RingDoorbell);

    if (Work->ClearStallResetFlags &&
        Work->Endpoint &&
        Work->Endpoint->Slot)
    {
        InterlockedExchange(&Work->Endpoint->Slot->Ep0NeedsStallReset, 0);
        InterlockedExchange(&Work->Endpoint->Slot->Ep0StallResetQueued, 0);
        KeSetEvent(&Work->Endpoint->Slot->Ep0StallResetEvent,
                   IO_NO_INCREMENT,
                   FALSE);
    }

    if (Work->Endpoint)
    {
        NewCount = InterlockedDecrement(&Work->Endpoint->PendingWorkCount);
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: EndpointResetWorker done, slot=%u ep=%u PendingWorkCount now=%ld\n",
                 Work->Endpoint->SlotId,
                 Work->Endpoint->EndpointId,
                 NewCount);
    }

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

    return (PUCHAR)SgList->MappedSystemVa + Element->SgOffset;
}

static
BOOLEAN
XHCI_DetermineDma64Bit(
    _In_opt_ PUSBPORT_RESOURCES Resources,
    _In_ ULONG HccParams)
{
    ULONG_PTR DmaFlags;
    BOOLEAN Supports64Bit;

    Supports64Bit = (BOOLEAN)(XHCI_HCC_64BIT_ADDR(HccParams) != 0);
    if (!Supports64Bit)
        return FALSE;

    if (!Resources)
        return Supports64Bit;

    DmaFlags = Resources->Reserved & USBPORT_RES_DMA_ADDR_MASK;
    if (DmaFlags == USBPORT_RES_DMA_ADDR_32BIT)
        return FALSE;
    if (DmaFlags == USBPORT_RES_DMA_ADDR_64BIT)
        return TRUE;

    return Supports64Bit;
}

static
BOOLEAN
XHCI_Requires32BitDma(
    _In_ PXHCI_EXTENSION Extension)
{
    if (!Extension)
        return FALSE;

    return (!Extension->Supports64Bit ||
            (Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA));
}

static
BOOLEAN
XHCI_SgListHasHighAddress(
    _In_ PUSBPORT_SCATTER_GATHER_LIST SgList,
    _Out_opt_ PULONGLONG HighAddress)
{
    ULONG Index;

    if (!SgList)
        return FALSE;

    for (Index = 0; Index < SgList->SgElementCount; Index++)
    {
        ULONGLONG Address = SgList->SgElement[Index].SgPhysicalAddress.QuadPart;
        ULONGLONG Length = SgList->SgElement[Index].SgTransferLength;
        ULONGLONG EndAddress;

        if (Length == 0)
            continue;

        EndAddress = Address + Length - 1;
        if (EndAddress < Address)
        {
            if (HighAddress)
                *HighAddress = EndAddress;
            return TRUE;
        }

        if ((Address >> 32) != 0 || (EndAddress >> 32) != 0)
        {
            if (HighAddress)
                *HighAddress = (EndAddress >> 32) ? EndAddress : Address;
            return TRUE;
        }
    }

    return FALSE;
}

static
ULONG
XHCI_CopySgListToBuffer(
    _In_ PUSBPORT_SCATTER_GATHER_LIST SgList,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength)
{
    ULONG Index;
    ULONG Copied = 0;
    PUCHAR Base;

    if (!SgList || !Buffer || BufferLength == 0 || !SgList->MappedSystemVa)
        return 0;

    Base = (PUCHAR)SgList->MappedSystemVa;

    for (Index = 0; Index < SgList->SgElementCount; Index++)
    {
        ULONG Offset = SgList->SgElement[Index].SgOffset;
        ULONG Length = SgList->SgElement[Index].SgTransferLength;

        if (Offset >= BufferLength)
            break;

        if (Offset + Length > BufferLength)
            Length = BufferLength - Offset;

        RtlCopyMemory((PUCHAR)Buffer + Offset, Base + Offset, Length);
        Copied += Length;

        if (Offset + Length >= BufferLength)
            break;
    }

    return Copied;
}

static
ULONG
XHCI_CopyBufferToSgList(
    _In_ PUSBPORT_SCATTER_GATHER_LIST SgList,
    _In_reads_bytes_(BufferLength) const VOID *Buffer,
    _In_ ULONG BufferLength)
{
    ULONG Index;
    ULONG Copied = 0;
    PUCHAR Base;

    if (!SgList || !Buffer || BufferLength == 0 || !SgList->MappedSystemVa)
        return 0;

    Base = (PUCHAR)SgList->MappedSystemVa;

    for (Index = 0; Index < SgList->SgElementCount; Index++)
    {
        ULONG Offset = SgList->SgElement[Index].SgOffset;
        ULONG Length = SgList->SgElement[Index].SgTransferLength;

        if (Offset >= BufferLength)
            break;

        if (Offset + Length > BufferLength)
            Length = BufferLength - Offset;

        RtlCopyMemory(Base + Offset, (const PUCHAR)Buffer + Offset, Length);
        Copied += Length;

        if (Offset + Length >= BufferLength)
            break;
    }

    return Copied;
}

static
MPSTATUS
XHCI_PrepareBounceBuffer(
    _In_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_TRANSFER Transfer,
    _In_ ULONG Length,
    _In_ BOOLEAN DataIn)
{
    ULONG Index;
    ULONG Mask;
    KIRQL OldIrql;
    PHYSICAL_ADDRESS Lowest;
    PHYSICAL_ADDRESS Highest;
    PHYSICAL_ADDRESS Boundary;
    PVOID Buffer;
    MEMORY_CACHING_TYPE CacheType;

    if (!Transfer || Length == 0)
        return MP_STATUS_SUCCESS;

    Transfer->BounceSlot = -1;

    /* Use preallocated low-memory buffers to avoid allocations at DISPATCH_LEVEL. */
    if (Extension &&
        Extension->BounceBufferSize >= Length &&
        Extension->BounceBuffers[0])
    {
        KeAcquireSpinLock(&Extension->BounceBufferLock, &OldIrql);
        for (Index = 0; Index < XHCI_BOUNCE_POOL_SLOTS; Index++)
        {
            Mask = (1u << Index);
            if ((Extension->BounceBuffersInUseMask & Mask) == 0 &&
                Extension->BounceBuffers[Index])
            {
                Extension->BounceBuffersInUseMask |= Mask;
                KeReleaseSpinLock(&Extension->BounceBufferLock, OldIrql);

                Transfer->BounceBuffer = Extension->BounceBuffers[Index];
                Transfer->BounceLength = Length;
                Transfer->BounceDataIn = DataIn;
                Transfer->BouncePhysicalAddress = Extension->BounceBuffersPhysical[Index];
                Transfer->BounceSlot = (LONG)Index;

                /*
                 * Zero the bounce buffer for IN transfers to prevent stale data
                 * from appearing if BytesTransferred exceeds actual received data.
                 * This is especially important for control transfers where the
                 * Status Stage event may report Remaining=0 even for short packets.
                 */
                if (DataIn)
                    RtlZeroMemory(Transfer->BounceBuffer, Length);

                DPRINT("usbxhci: bounce pool slot=%lu len=%lu VA=%p PA=%I64x dir=%s\n",
                       Index,
                       Length,
                       Transfer->BounceBuffer,
                       Transfer->BouncePhysicalAddress.QuadPart,
                       DataIn ? "IN" : "OUT");
                return MP_STATUS_SUCCESS;
            }
        }
        KeReleaseSpinLock(&Extension->BounceBufferLock, OldIrql);

        DPRINT1("usbxhci: bounce pool exhausted len=%lu irql=%lu\n",
                Length,
                (ULONG)KeGetCurrentIrql());
    }

    Lowest.QuadPart = 0;
    Highest.QuadPart = 0xFFFFFFFFull;
    Boundary.QuadPart = 0;

    CacheType = XHCI_GetDmaCacheType(Extension);
    Buffer = MmAllocateContiguousMemorySpecifyCache(Length,
                                                    Lowest,
                                                    Highest,
                                                    Boundary,
                                                    CacheType);
    if (!Buffer)
    {
        DPRINT1("usbxhci: bounce alloc failed len=%lu irql=%lu\n",
                Length,
                (ULONG)KeGetCurrentIrql());
        return MP_STATUS_NO_RESOURCES;
    }

    Transfer->BounceBuffer = Buffer;
    Transfer->BounceLength = Length;
    Transfer->BounceDataIn = DataIn;
    Transfer->BouncePhysicalAddress = MmGetPhysicalAddress(Buffer);

    /* Zero the bounce buffer for IN transfers to prevent stale data. */
    if (DataIn)
        RtlZeroMemory(Buffer, Length);

    if ((Transfer->BouncePhysicalAddress.QuadPart >> 32) != 0)
    {
        DPRINT1("usbxhci: bounce buffer above 4G (PA=%I64x len=%lu)\n",
                Transfer->BouncePhysicalAddress.QuadPart,
                Length);
        XHCI_ReleaseBounceBuffer(Transfer);
        return MP_STATUS_NO_RESOURCES;
    }

    DPRINT1("usbxhci: bounce buffer len=%lu VA=%p PA=%I64x dir=%s\n",
            Length,
            Buffer,
            Transfer->BouncePhysicalAddress.QuadPart,
            DataIn ? "IN" : "OUT");

    return MP_STATUS_SUCCESS;
}

static
VOID
XHCI_ReleaseBounceBuffer(
    _Inout_ PXHCI_TRANSFER Transfer)
{
    PXHCI_EXTENSION Extension;
    KIRQL OldIrql;

    if (!Transfer || !Transfer->BounceBuffer)
        return;

    Extension = (Transfer->Endpoint ? Transfer->Endpoint->Extension : NULL);
    if (Transfer->BounceSlot >= 0 &&
        Extension &&
        Transfer->BounceSlot < XHCI_BOUNCE_POOL_SLOTS)
    {
        KeAcquireSpinLock(&Extension->BounceBufferLock, &OldIrql);
        Extension->BounceBuffersInUseMask &= ~(1u << Transfer->BounceSlot);
        KeReleaseSpinLock(&Extension->BounceBufferLock, OldIrql);
    }
    else
    {
        MmFreeContiguousMemory(Transfer->BounceBuffer);
    }

    Transfer->BounceBuffer = NULL;
    Transfer->BounceLength = 0;
    Transfer->BouncePhysicalAddress.QuadPart = 0;
    Transfer->BounceDataIn = FALSE;
    Transfer->BounceSlot = -1;
}

static
VOID
XHCI_FinalizeBounceBuffer(
    _Inout_ PXHCI_TRANSFER Transfer)
{
    ULONG Length;
    ULONG Copied;

    if (!Transfer || !Transfer->BounceBuffer)
        return;

    if (Transfer->BounceDataIn)
    {
        Length = Transfer->BytesTransferred;
        if (Length > Transfer->BounceLength)
            Length = Transfer->BounceLength;

        Copied = XHCI_CopyBufferToSgList(Transfer->SgList,
                                         Transfer->BounceBuffer,
                                         Length);
        if (Copied < Length)
        {
            DPRINT1("usbxhci: bounce IN copy short (%lu/%lu)\n",
                    Copied,
                    Length);
        }
    }

    XHCI_ReleaseBounceBuffer(Transfer);
}

static
MPSTATUS
XHCI_InitBouncePool(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONG Index;
    PHYSICAL_ADDRESS Low;
    PHYSICAL_ADDRESS High;
    PHYSICAL_ADDRESS Boundary;
    MEMORY_CACHING_TYPE CacheType;

    if (!Extension)
        return MP_STATUS_ERROR;
    if (Extension->StoppingOrRemoved)
        return MP_STATUS_HW_ERROR;

    if (!XHCI_Requires32BitDma(Extension))
        return MP_STATUS_SUCCESS;

    Extension->BounceBufferSize = XHCI_BOUNCE_BUFFER_SIZE;
    Extension->BounceBuffersInUseMask = 0;
    KeInitializeSpinLock(&Extension->BounceBufferLock);

    Low.QuadPart = 0;
    High.QuadPart = 0xFFFFFFFFull;
    Boundary.QuadPart = 0;
    CacheType = XHCI_GetDmaCacheType(Extension);

    for (Index = 0; Index < XHCI_BOUNCE_POOL_SLOTS; Index++)
    {
        PVOID Buffer = MmAllocateContiguousMemorySpecifyCache(Extension->BounceBufferSize,
                                                             Low,
                                                             High,
                                                             Boundary,
                                                             CacheType);
        if (!Buffer)
        {
            DPRINT1("usbxhci: bounce pool alloc failed slot=%lu len=%lu\n",
                    Index,
                    Extension->BounceBufferSize);
            XHCI_FreeBouncePool(Extension);
            return MP_STATUS_NO_RESOURCES;
        }

        Extension->BounceBuffers[Index] = Buffer;
        Extension->BounceBuffersPhysical[Index] = MmGetPhysicalAddress(Buffer);

        if ((Extension->BounceBuffersPhysical[Index].QuadPart >> 32) != 0)
        {
            DPRINT1("usbxhci: bounce pool entry above 4G (slot=%lu PA=%I64x)\n",
                    Index,
                    Extension->BounceBuffersPhysical[Index].QuadPart);
            XHCI_FreeBouncePool(Extension);
            return MP_STATUS_NO_RESOURCES;
        }
    }

    DPRINT1("usbxhci: bounce pool ready slots=%u size=%lu\n",
            XHCI_BOUNCE_POOL_SLOTS,
            Extension->BounceBufferSize);
    return MP_STATUS_SUCCESS;
}

static
VOID
XHCI_FreeBouncePool(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONG Index;

    if (!Extension)
        return;

    for (Index = 0; Index < XHCI_BOUNCE_POOL_SLOTS; Index++)
    {
        if (Extension->BounceBuffers[Index])
        {
            MmFreeContiguousMemory(Extension->BounceBuffers[Index]);
            Extension->BounceBuffers[Index] = NULL;
            Extension->BounceBuffersPhysical[Index].QuadPart = 0;
        }
    }

    Extension->BounceBuffersInUseMask = 0;
    Extension->BounceBufferSize = 0;
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

    if (Extension->FatalError || Extension->StoppingOrRemoved)
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

static VOID
XHCI_CompleteSwEnumTransfer(
    _In_ PXHCI_EXTENSION Extension,
    _In_opt_ PXHCI_ENDPOINT Endpoint,
    _In_ PXHCI_TRANSFER Transfer)
{
    KIRQL OldIrql;
    ULONG PrevFlags;
    PXHCI_ENDPOINT ActiveEndpoint;

    if (!Extension || !Transfer)
        return;

    PrevFlags = InterlockedOr((volatile LONG *)&Transfer->Flags,
                              XHCI_TRANSFER_FLAG_SWENUM_DONE);
    if (PrevFlags & XHCI_TRANSFER_FLAG_SWENUM_DONE)
        return;

    InterlockedAnd((volatile LONG *)&Transfer->Flags,
                   ~XHCI_TRANSFER_FLAG_SWENUM_PENDING);

    ActiveEndpoint = Endpoint ? Endpoint : Transfer->Endpoint;
    if (ActiveEndpoint)
    {
        KeAcquireSpinLock(&ActiveEndpoint->Lock, &OldIrql);
        if (ActiveEndpoint->ActiveTransfer == Transfer)
            ActiveEndpoint->ActiveTransfer = NULL;
        KeReleaseSpinLock(&ActiveEndpoint->Lock, OldIrql);
    }

    if (ActiveEndpoint &&
        (Transfer->Flags & (XHCI_TRANSFER_FLAG_SET_ADDRESS | XHCI_TRANSFER_FLAG_GET_DESCRIPTOR)))
        XHCI_HandleEnumerationTransfer(Extension, ActiveEndpoint, Transfer);

    if (XhciRegPacket.UsbPortCompleteTransfer && Transfer->TransferParameters)
    {
        /* Mark completed before handing to USBPORT to prevent double-completion */
        if (!InterlockedBitTestAndSet((volatile LONG *)&Transfer->Flags,
                                      XHCI_TRANSFER_FLAG_COMPLETED_BIT))
        {
            XhciRegPacket.UsbPortCompleteTransfer(Extension,
                                                  ActiveEndpoint,
                                                  Transfer->TransferParameters,
                                                  Transfer->UsbdStatus,
                                                  Transfer->BytesTransferred);
        }
        else
        {
            DPRINT1("usbxhci: SW-enum completion skipping already-completed transfer %p\n",
                    Transfer);
        }
    }

    XHCI_DereferenceEndpointForSwEnum(ActiveEndpoint);
}

static VOID NTAPI
XHCI_SwEnumWorker(
    _In_ PVOID Context)
{
    PXHCI_SWENUM_WORK Work = (PXHCI_SWENUM_WORK)Context;
    PXHCI_EXTENSION Extension;
    PXHCI_TRANSFER Transfer;
    PXHCI_DEVICE_SLOT Slot = NULL;
    PXHCI_ENDPOINT ActiveEndpoint = NULL;
    BOOLEAN Canceled;

    if (!Work)
        return;

    Extension = Work->Extension;
    Transfer = Work->Transfer;

    if (!Extension || !Transfer)
        goto Done;

    /*
     * Use Work->Endpoint instead of Transfer->Endpoint to avoid UAF.
     * The refcount (SwEnumRefCount) was incremented on Work->Endpoint
     * at queue time. Transfer->Endpoint could become stale/different.
     */
    ActiveEndpoint = Work->Endpoint;
    if (Work->SlotId)
        Slot = XHCI_GetSlot(Extension, Work->SlotId);
    if (!Slot && ActiveEndpoint)
        Slot = ActiveEndpoint->Slot;

    Canceled = Extension->StoppingOrRemoved ||
               Extension->FatalError ||
               (Transfer->Flags & XHCI_TRANSFER_FLAG_SWENUM_CANCELED);
    if (Canceled)
    {
        Transfer->BytesTransferred = 0;
        Transfer->UsbdStatus = USBD_STATUS_CANCELED;
        XHCI_CompleteSwEnumTransfer(Extension, ActiveEndpoint, Transfer);
        goto Done;
    }

    /*
     * If NeedsAddressDevice is set, we need to issue ADDRESS_DEVICE at PASSIVE_LEVEL.
     * This was deferred from the SET_ADDRESS handler which runs at elevated IRQL.
     * At PASSIVE_LEVEL, XHCI_SendCommand uses full timeout (100ms) and retries.
     */
    if (Work->NeedsAddressDevice)
    {
        if (Transfer->Flags & XHCI_TRANSFER_FLAG_SWENUM_CANCELED)
        {
            Transfer->BytesTransferred = 0;
            Transfer->UsbdStatus = USBD_STATUS_CANCELED;
            XHCI_CompleteSwEnumTransfer(Extension, ActiveEndpoint, Transfer);
            goto Done;
        }

        if (!Slot || !Slot->InUse)
        {
            Transfer->BytesTransferred = 0;
            Transfer->UsbdStatus = USBD_STATUS_DEV_NOT_RESPONDING;
        }
        else if (Slot->Addressed && Slot->UsbDeviceAddress == Work->NewAddress)
        {
            /*
             * Guard against redundant deferred SET_ADDRESS work: if another path
             * already put the slot into the requested addressed state, do not
             * send ADDRESS_DEVICE again.
             */
            DPRINT1("usbxhci: deferred ADDRESS_DEVICE skipped for slot %u (already addressed addr=%u)\n",
                    Slot->SlotId, Work->NewAddress);

            if (ActiveEndpoint)
                ActiveEndpoint->EndpointProperties.DeviceAddress = Work->NewAddress;
            XHCI_UpdateDeviceAddressMap(Extension, Slot, Work->NewAddress);
            Transfer->BytesTransferred = 0;
            Transfer->UsbdStatus = USBD_STATUS_SUCCESS;
        }
        else if (!Slot->Addressed)
        {
            MPSTATUS AddrStatus;

            DPRINT("usbxhci: SwEnumWorker issuing deferred ADDRESS_DEVICE for slot %u\n",
                   Slot->SlotId);

            AddrStatus = XHCI_AddressDeviceSlot(Extension,
                                                 Slot,
                                                 &Work->EndpointProperties,
                                                 TRUE);  /* Disable slot on failure after retries exhausted */
            if (AddrStatus != MP_STATUS_SUCCESS)
            {
                DPRINT1("usbxhci: deferred ADDRESS_DEVICE failed (Status=%d), slot %u disabled\n",
                        AddrStatus, Slot->SlotId);

                /*
                 * ADDRESS_DEVICE failed after all retries. The slot has been disabled
                 * by XHCI_AddressDeviceSlot (DisableOnFailure=TRUE). Mark the slot as
                 * not in use to prevent any further access to this invalid slot state.
                 * Clear the DCBAA entry so the xHC doesn't try to access the context.
                 */
                if (Extension->Dcbaa)
                    Extension->Dcbaa[Slot->SlotId] = 0;
                Slot->InUse = FALSE;
                Slot->Addressed = FALSE;

                /*
                 * Unlink the endpoint from the slot to prevent CloseEndpoint from
                 * trying to access the disabled slot later.
                 */
                if (ActiveEndpoint && ActiveEndpoint->Slot == Slot)
                {
                    if (ActiveEndpoint->EndpointId < RTL_NUMBER_OF(Slot->EndpointTable))
                        Slot->EndpointTable[ActiveEndpoint->EndpointId] = NULL;
                    ActiveEndpoint->Slot = NULL;
                }

                Transfer->BytesTransferred = 0;
                Transfer->UsbdStatus = USBD_STATUS_DEV_NOT_RESPONDING;
                /* Fall through to complete transfer with error */
            }
            else
            {
                /* Update address maps with the new USB address */
                if (ActiveEndpoint)
                    ActiveEndpoint->EndpointProperties.DeviceAddress = Work->NewAddress;
                Slot->UsbDeviceAddress = Work->NewAddress;
                XHCI_UpdateDeviceAddressMap(Extension, Slot, Work->NewAddress);

                DPRINT("usbxhci: deferred slot %u SET_ADDRESS to addr=%u complete\n",
                       Slot->SlotId, Work->NewAddress);

                Transfer->BytesTransferred = 0;
                Transfer->UsbdStatus = USBD_STATUS_SUCCESS;
            }
        }
        else
        {
            DPRINT("usbxhci: deferred SET_ADDRESS map update for slot %u (old=%u new=%u)\n",
                   Slot->SlotId, Slot->UsbDeviceAddress, Work->NewAddress);
            if (ActiveEndpoint)
                ActiveEndpoint->EndpointProperties.DeviceAddress = Work->NewAddress;
            Slot->UsbDeviceAddress = Work->NewAddress;
            XHCI_UpdateDeviceAddressMap(Extension, Slot, Work->NewAddress);
            Transfer->BytesTransferred = 0;
            Transfer->UsbdStatus = USBD_STATUS_SUCCESS;
        }
    }

    XHCI_CompleteSwEnumTransfer(Extension, ActiveEndpoint, Transfer);

Done:
    if (Extension)
        InterlockedDecrement(&Extension->SwEnumWorkerCount);
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

        DPRINT("usbxhci: device on slot %u set address %u (bmR=0x%02x req=%02x)\n",
               Endpoint->Slot->SlotId,
               Transfer->NewAddress,
               Setup->bmRequestType.B,
               Setup->bRequest);
    }

    if (Transfer->Flags & XHCI_TRANSFER_FLAG_GET_DESCRIPTOR)
    {
        Buffer = XHCI_GetDescriptorBuffer(Transfer, &BufferLength);
        if (Buffer && MmIsAddressValid(Buffer) && BufferLength >= 2 && Endpoint->Slot)
        {
            UCHAR DescriptorType = Setup->wValue.HiByte;

            if (DescriptorType == USB_DEVICE_DESCRIPTOR_TYPE)
            {
                if (BufferLength >= sizeof(USB_DEVICE_DESCRIPTOR))
                {
                    PUSB_DEVICE_DESCRIPTOR D = (PUSB_DEVICE_DESCRIPTOR)Buffer;
                    PHYSICAL_ADDRESS Pa = MmGetPhysicalAddress(Buffer);
                    PHYSICAL_ADDRESS SgPa;
                    SgPa.QuadPart = 0;

                    if (Transfer->SgList && Transfer->SgList->SgElementCount > 0)
                         SgPa = Transfer->SgList->SgElement[0].SgPhysicalAddress;

                    DPRINT("XHCI: GetDescriptor Data: Len=%d Type=%x VID=%04x PID=%04x\n",
                            BufferLength, D->bDescriptorType, D->idVendor, D->idProduct);
                    DPRINT("XHCI: buffer debugging: VA=%p PA=%I64x SG_PA=%I64x\n",
                            Buffer, Pa.QuadPart, SgPa.QuadPart);
                    DPRINT("XHCI: Raw Bytes: %02x %02x %02x %02x\n",
                           ((PUCHAR)Buffer)[0], ((PUCHAR)Buffer)[1], ((PUCHAR)Buffer)[2], ((PUCHAR)Buffer)[3]);
                }
                else
                {
                     DPRINT("XHCI: GetDescriptor Data: Len=%d (Header Only/Short)\n", BufferLength);
                }
            }
            else if (DescriptorType == USB_CONFIGURATION_DESCRIPTOR_TYPE)
            {
                PHYSICAL_ADDRESS Pa = MmGetPhysicalAddress(Buffer);
                PHYSICAL_ADDRESS SgPa;
                SgPa.QuadPart = 0;

                if (Transfer->SgList && Transfer->SgList->SgElementCount > 0)
                    SgPa = Transfer->SgList->SgElement[0].SgPhysicalAddress;

                DPRINT("XHCI: GetDescriptor(CFG) len=%lu first=%02x %02x %02x %02x %02x %02x VA=%p PA=%I64x SG_PA=%I64x\n",
                        BufferLength,
                        (BufferLength > 0) ? Buffer[0] : 0,
                        (BufferLength > 1) ? Buffer[1] : 0,
                        (BufferLength > 2) ? Buffer[2] : 0,
                        (BufferLength > 3) ? Buffer[3] : 0,
                        (BufferLength > 4) ? Buffer[4] : 0,
                        (BufferLength > 5) ? Buffer[5] : 0,
                        Buffer,
                        Pa.QuadPart,
                        SgPa.QuadPart);
            }
            else
            {
                /* omit noisy trace for non-device descriptors */
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

    Slot = XHCI_FindSlotByPort(Extension, PortNumber);
    if (!Slot)
        return MP_STATUS_ERROR;

    /*
     * VirtualBox xHCI emulation has a bug where RESET_DEVICE command
     * doesn't properly reset the TR Dequeue Pointer, causing subsequent
     * transfers to fail with USB_TRANSACTION_ERROR. Skip the RESET_DEVICE
     * command entirely for VirtualBox - the device is already in a usable
     * state after the port reset.
     */
    if (Extension->Quirks & XHCI_QUIRK_VBOX_POLL_XFERS)
    {
        DPRINT1("usbxhci: ResetDeviceOnPort: Skipping RESET_DEV for VBox quirk (slot %u port %u)\n",
                Slot->SlotId, PortNumber);
        return MP_STATUS_SUCCESS;
    }

    /*
     * If the slot is already in Default state (Addressed=FALSE), the device
     * is already at USB address 0. Skip the redundant command.
     */
    if (!Slot->Addressed)
    {
        DPRINT1("usbxhci: ResetDeviceOnPort: slot %u already in Default state, skipping RESET_DEV\n",
                Slot->SlotId);
        return MP_STATUS_SUCCESS;
    }

    /*
     * The USB bus reset has physically put the device back to address 0.
     * We MUST issue RESET_DEVICE to put the xHCI slot context back to Default
     * state to match. If we skip RESET_DEVICE, the xHCI slot stays in Addressed
     * state while the USB device is at address 0, causing ADDRESS_DEVICE to fail
     * with CONTEXT_STATE_ERROR (19) during re-enumeration.
     *
     * Note: The previous logic skipped RESET_DEVICE for unconfigured slots to
     * avoid "undoing ADDRESS_DEV", but this is wrong because:
     * 1. The USB bus reset already undid the addressing on the USB side
     * 2. Skipping RESET_DEVICE leaves xHCI and USB states inconsistent
     * 3. Subsequent ADDRESS_DEVICE fails because xHCI slot is still Addressed
     */
    Status = XHCI_SendCommand(Extension,
                              XHCI_TRB_TYPE_RESET_DEV,
                              0,
                              0,
                              XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                              XHCI_COMMAND_TIMEOUT_MS,
                              TRUE,
                              NULL,
                              &CompletionCode);
    if (Status == MP_STATUS_SUCCESS)
    {
        /* RESET_DEVICE succeeded - slot is now in Default state */
        Slot->Addressed = FALSE;
        DPRINT1("usbxhci: ResetDeviceOnPort: RESET_DEV succeeded for slot %u on port %u, now in Default state\n",
                Slot->SlotId, PortNumber);
    }
    else
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
        ULONG Cycle;
        ULONG TrbType;

        /*
         * Ensure we read the latest value written by the xHCI controller.
         * The event ring is in DMA-accessible system memory (not MMIO), so we use
         * volatile pointer dereference rather than READ_REGISTER_ULONG (which is
         * for MMIO only). A memory barrier before the read ensures proper ordering
         * on weakly-ordered architectures (ARM64).
         *
         * Note: READ_REGISTER_ULONG was incorrect here as it's meant for MMIO
         * registers, not DMA buffers. On non-coherent DMA systems, the buffer
         * should be allocated with appropriate caching attributes.
         */
        KeMemoryBarrier();
        Cycle = *(volatile ULONG *)&EventTrb->Control & XHCI_TRB_CYCLE;
        KeMemoryBarrier();

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
        XHCI_WRITE_REGISTER_ULONG(&Interrupter->ErdpHigh,
                             (ULONG)(Extension->EventRingDequeuePointer >> 32));

        ErdpLow = (ULONG)(Extension->EventRingDequeuePointer & 0xFFFFFFFF);
        if (SetBusy)
            ErdpLow |= XHCI_ERDP_BUSY;

        XHCI_WRITE_REGISTER_ULONG(&Interrupter->ErdpLow, ErdpLow);

        if (AcknowledgeInterrupt)
        {
            ULONG Iman = XHCI_READ_REGISTER_ULONG(&Interrupter->Iman);
            /* Write IP=1 (RW1C to clear) while preserving IE, mask reserved bits */
            XHCI_WRITE_REGISTER_ULONG(&Interrupter->Iman,
                                      XHCI_IMAN_IP | (Iman & XHCI_IMAN_IE));
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
    ULONG Control;

    if (!Extension ||
        !Extension->EventRing ||
        Extension->EventRingTrbCount == 0)
    {
        return FALSE;
    }

    KeAcquireSpinLock(&Extension->EventRingLock, &OldIrql);
    EventTrb = &Extension->EventRing[Extension->EventRingDequeueIndex];

    /*
     * Ensure we read the latest value written by the xHCI controller.
     * The event ring is in DMA-accessible system memory (not MMIO), so we use
     * volatile pointer dereference rather than READ_REGISTER_ULONG. A memory
     * barrier before the read ensures proper ordering on weakly-ordered
     * architectures (ARM64), and after to prevent speculative reordering.
     */
    KeMemoryBarrier();
    Control = *(volatile ULONG *)&EventTrb->Control;
    KeMemoryBarrier();

    Pending = ((Control & XHCI_TRB_CYCLE) == Extension->EventRingCycleState);
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
    UsbCmd = XHCI_READ_REGISTER_ULONG(&Ops->UsbCmd);
    UsbSts = XHCI_READ_REGISTER_ULONG(&Ops->UsbSts);
    DnCtrl = XHCI_READ_REGISTER_ULONG(&Ops->DeviceNotificationControl);
    Config = XHCI_READ_REGISTER_ULONG(&Ops->Config);
    Crcr = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Ops->CrCrHigh) << 32) |
           XHCI_READ_REGISTER_ULONG(&Ops->CrCrLow);
    Dcbaap = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Ops->DcbaapHigh) << 32) |
             XHCI_READ_REGISTER_ULONG(&Ops->DcbaapLow);

    Runtime = Extension->RuntimeRegisters;
    Interrupter = Runtime ? &Runtime->Interrupter[0] : NULL;

    if (Interrupter)
    {
        ErstSize = XHCI_READ_REGISTER_ULONG(&Interrupter->ErstSize);
        ErstBase = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Interrupter->ErstBaseHigh) << 32) |
                   XHCI_READ_REGISTER_ULONG(&Interrupter->ErstBaseLow);
        Erdp = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Interrupter->ErdpHigh) << 32) |
               XHCI_READ_REGISTER_ULONG(&Interrupter->ErdpLow);
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
                XHCI_READ_REGISTER_ULONG(&Interrupter->Imod),
                XHCI_READ_REGISTER_ULONG(&Interrupter->Iman));
    }

    for (Port = 0; Port < Extension->NumberOfPorts; Port++)
    {
        ULONG PortSc = XHCI_READ_REGISTER_ULONG(&Ops->PortRegister[Port].PortStatusAndControl);
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
    UsbCmd = XHCI_READ_REGISTER_ULONG(&Ops->UsbCmd);
    UsbSts = XHCI_READ_REGISTER_ULONG(&Ops->UsbSts);
    Config = XHCI_READ_REGISTER_ULONG(&Ops->Config);
    Crcr = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Ops->CrCrHigh) << 32) |
           XHCI_READ_REGISTER_ULONG(&Ops->CrCrLow);

    if (Extension->DoorbellArray)
    {
        Doorbell0 = XHCI_READ_REGISTER_ULONG(&Extension->DoorbellArray->Doorbell[0]);
    }

    Interrupter = (Extension->RuntimeRegisters) ?
                  &Extension->RuntimeRegisters->Interrupter[0] : NULL;

    if (Interrupter)
    {
        ULONG Iman = XHCI_READ_REGISTER_ULONG(&Interrupter->Iman);
        ULONG Imod = XHCI_READ_REGISTER_ULONG(&Interrupter->Imod);
        ULONG ErstSize = XHCI_READ_REGISTER_ULONG(&Interrupter->ErstSize);
        ULONGLONG Erdp = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Interrupter->ErdpHigh) << 32) |
                         XHCI_READ_REGISTER_ULONG(&Interrupter->ErdpLow);
        ULONGLONG ErstBase = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Interrupter->ErstBaseHigh) << 32) |
                             XHCI_READ_REGISTER_ULONG(&Interrupter->ErstBaseLow);

        DPRINT("usbxhci: %s IMAN=%08lx IMOD=%08lx ERSTSZ=%lu ERSTBA=%08lx:%08lx "
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

    DPRINT("usbxhci: %s USBCMD=%08lx USBSTS=%08lx CONFIG=%08lx CRCR=%08lx:%08lx DOORBELL0=%08lx\n",
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

    Crcr = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Ops->CrCrHigh) << 32) |
           XHCI_READ_REGISTER_ULONG(&Ops->CrCrLow);
    ErstBase = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Interrupter->ErstBaseHigh) << 32) |
               XHCI_READ_REGISTER_ULONG(&Interrupter->ErstBaseLow);
    Erdp = ((ULONGLONG)XHCI_READ_REGISTER_ULONG(&Interrupter->ErdpHigh) << 32) |
           XHCI_READ_REGISTER_ULONG(&Interrupter->ErdpLow);
    ErstSize = XHCI_READ_REGISTER_ULONG(&Interrupter->ErstSize);
    Iman = XHCI_READ_REGISTER_ULONG(&Interrupter->Iman);

    DPRINT("usbxhci: %s type=%lu cmdptr=%I64x cmd_enq=%lu cyc=%lu "
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
        PortSc = XHCI_READ_REGISTER_ULONG(PortScReg);

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

    /* Log ALL command completions, especially errors */
    if (CommandContext)
    {
        DPRINT("usbxhci: CMD_COMPLETE type=%lu code=%lu slot=%u cmdptr=%I64x\n",
               CommandContext->CommandType,
               CompletionCode,
               SlotId,
               CommandPointer);
    }
    else
    {
        DPRINT1("usbxhci: CMD_COMPLETE ORPHAN code=%lu slot=%u cmdptr=%I64x (no context)\n",
                CompletionCode,
                SlotId,
                CommandPointer);
    }

    /* Log Context State Error, but demote to debug trace for Stop Endpoint
     * since it is benign (endpoint already stopped during shutdown). */
    if (CompletionCode == 19)
    {
        if (CommandContext && CommandContext->CommandType == XHCI_TRB_TYPE_STOP_EP)
        {
            DPRINT("usbxhci: CONTEXT_STATE_ERROR for StopEndpoint (already stopped) slot=%u\n",
                   SlotId);
        }
        else
        {
            DPRINT1("usbxhci: *** CONTEXT_STATE_ERROR (19) *** type=%lu slot=%u cmdptr=%I64x\n",
                    CommandContext ? CommandContext->CommandType : 0xFFFFFFFF,
                    SlotId,
                    CommandPointer);
        }
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
            DPRINT("usbxhci: slot %u addressed\n", SlotId);
        }
    }
    else if (CompletionCode == XHCI_COMPLETION_SUCCESS &&
             Slot &&
             CommandContext->CommandType == XHCI_TRB_TYPE_CONFIG_EP)
    {
        Slot->Configured = TRUE;
        DPRINT("usbxhci: slot %u configured\n", SlotId);
    }
    else if (CompletionCode == XHCI_COMPLETION_SUCCESS &&
             Slot &&
             CommandContext->CommandType == XHCI_TRB_TYPE_RESET_DEV)
    {
        Slot->Configured = FALSE;
        Slot->HighestEndpointId = 1;
        Slot->Addressed = FALSE;
        XHCI_UpdateDeviceAddressMap(Extension, Slot, 0);

        /*
         * RESET_DEVICE causes the xHC to reinitialize the Default Control
         * Endpoint context. According to xHCI spec 4.6.11, the TR Dequeue
         * Pointer in the endpoint context is reset to its original value
         * (pointing to the ring base at index 0). We must reset the software
         * ring state to match, otherwise subsequent transfers will queue TRBs
         * at the old EnqueueIndex while the xHC expects them at index 0,
         * causing TRB pointer mismatches and STALL errors.
         *
         * This is the same ring reset that XHCI_PrepareDefaultControlContext
         * performs before ADDRESS_DEVICE, but we must also do it here because
         * RESET_DEVICE can be issued after transfers have already advanced
         * the ring state.
         */
        Slot->Ep0RingCycleState = 1;
        Slot->Ep0RingEnqueueIndex = 0;
        Slot->Ep0RingDequeueIndex = 0;

        /* Clear stale TRBs from the EP0 ring to prevent cycle bit confusion */
        if (Slot->Ep0TransferRing.VirtualAddress && Slot->Ep0TransferRing.Length > 0)
        {
            PXHCI_TRB Ep0Ring = (PXHCI_TRB)Slot->Ep0TransferRing.VirtualAddress;
            ULONG TrbCount = (ULONG)(Slot->Ep0TransferRing.Length / sizeof(XHCI_TRB));
            ULONG i;

            /* Clear all TRBs except the last one (Link TRB) */
            for (i = 0; i + 1 < TrbCount; i++)
            {
                Ep0Ring[i].Parameter1 = 0;
                Ep0Ring[i].Parameter2 = 0;
                Ep0Ring[i].Status = 0;
                Ep0Ring[i].Control = 0;
            }

            /* Reinitialize the Link TRB at the end of the ring */
            if (TrbCount > 1)
            {
                PXHCI_TRB LinkTrb = &Ep0Ring[TrbCount - 1];
                ULONGLONG RingBase = Slot->Ep0TransferRing.PhysicalAddress.QuadPart;

                LinkTrb->Parameter1 = (ULONG)(RingBase & 0xFFFFFFFF);
                LinkTrb->Parameter2 = (ULONG)(RingBase >> 32);
                LinkTrb->Status = 0;
                LinkTrb->Control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                                   XHCI_TRB_TOGGLE_CYCLE |
                                   XHCI_TRB_CYCLE;
            }
        }

        /*
         * Also synchronize the endpoint object's TransferRing state if one
         * exists. EP0 uses EndpointTable[1] (xHCI endpoint ID 1).
         */
        if (Slot->EndpointTable[1] != NULL)
        {
            PXHCI_ENDPOINT Ep0 = Slot->EndpointTable[1];
            Ep0->TransferRing.CycleState = 1;
            Ep0->TransferRing.EnqueueIndex = 0;
            Ep0->TransferRing.DequeueIndex = 0;
            Ep0->ActiveTransfer = NULL;
        }

        InterlockedExchange(&Slot->Ep0NeedsStallReset, 0);
        InterlockedExchange(&Slot->Ep0StallResetQueued, 0);
        KeSetEvent(&Slot->Ep0StallResetEvent, IO_NO_INCREMENT, FALSE);

        DPRINT("usbxhci: slot %u reset (EP0 ring reset to index 0)\n", SlotId);
    }
    else if (CommandContext->CommandType == XHCI_TRB_TYPE_DISABLE_SLOT)
    {
        if (CompletionCode == XHCI_COMPLETION_SUCCESS && Slot)
        {
            UCHAR EpId;

            DPRINT("usbxhci: slot %u disabled\n", SlotId);

            /*
             * Drain any stale deferred transfer completions for this slot.
             * Non-DeviceGone transfers that were deferred (AllowCallbacks
             * was FALSE) should have been drained by
             * XHCI_DrainDeferredTransferCompletions already, but flush
             * any stragglers as a safety measure.
             */
            XHCI_FlushDeferredCompletionsForSlot(Extension, SlotId);

            /*
             * NULL out ActiveTransfer on all endpoints belonging to this
             * slot. Do NOT call UsbPortCompleteTransfer from here.
             *
             * USBPORT's own abort mechanism (AbortTransfers ->
             * DmaEndpointPaused -> FlushCancelList -> CompleteTransfer)
             * will handle completing and freeing these transfers. Calling
             * UsbPortCompleteTransfer from the miniport side races with
             * USBPORT_AbortTransfers: the miniport's QueueDoneTransfer
             * does RemoveEntryList on the TransferLink without holding
             * EndpointSpinLock, corrupting the TransferList that
             * DmaEndpointPaused iterates under EndpointSpinLock. This
             * list corruption causes use-after-free crashes.
             *
             * The miniport's only job here is to disown the transfers
             * (NULL ActiveTransfer) so XHCI_AbortTransfer knows there's
             * nothing to abort on the hardware side.
             */
            for (EpId = 1; EpId <= XHCI_MAX_ENDPOINTS; EpId++)
            {
                PXHCI_ENDPOINT Ep = Slot->EndpointTable[EpId];
                KIRQL EpIrql;

                if (!Ep)
                    continue;

                KeAcquireSpinLock(&Ep->Lock, &EpIrql);
                if (Ep->ActiveTransfer)
                {
                    DPRINT1("usbxhci: slot %u ep %u NULLing orphan ActiveTransfer %p (USBPORT will abort)\n",
                            SlotId, EpId, Ep->ActiveTransfer);
                    Ep->ActiveTransfer = NULL;
                }
                KeReleaseSpinLock(&Ep->Lock, EpIrql);
            }

            Slot->InUse = FALSE;
            Slot->Addressed = FALSE;
            Slot->Configured = FALSE;
            Slot->DisablePending = FALSE;
            Slot->UsbDeviceAddress = 0;
            Slot->PortNumber = 0;
            Slot->HighestEndpointId = 1;
            XHCI_UpdateDeviceAddressMap(Extension, Slot, 0);
            RtlZeroMemory(Slot->EndpointTable, sizeof(Slot->EndpointTable));
            RtlZeroMemory(Slot->DeferredEndpointTable, sizeof(Slot->DeferredEndpointTable));
            InterlockedExchange(&Slot->Ep0NeedsStallReset, 0);
            InterlockedExchange(&Slot->Ep0StallResetQueued, 0);
            KeSetEvent(&Slot->Ep0StallResetEvent, IO_NO_INCREMENT, FALSE);
        }
        else if (Slot)
        {
            DPRINT1("usbxhci: disable slot %u failed (code=%lu)\n",
                    SlotId, CompletionCode);
            Slot->DisablePending = FALSE;
        }
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
    PXHCI_DEVICE_SLOT Slot;
    MPSTATUS Status;

    if (!Extension || PortId == 0 || PortId > Extension->NumberOfPorts)
        return;

    PortScReg = XHCI_GetPortStatusRegister(Extension, PortId);
    if (PortScReg)
        PortSc = XHCI_READ_REGISTER_ULONG(PortScReg);

    DPRINT("usbxhci: port status change on port %u PortSC=0x%08lx\n",
           PortId,
           PortSc);

    /* If the device disconnected, proactively disable the slot to avoid stale state */
    if ((PortSc & XHCI_PORTSC_CCS) == 0)
    {
        Slot = XHCI_FindSlotByPort(Extension, PortId);
        if (Slot && Slot->InUse && !Slot->DisablePending)
        {
            Slot->DisablePending = TRUE;
            DPRINT1("usbxhci: port %u disconnect, disabling slot %u\n",
                    PortId, Slot->SlotId);

            Status = XHCI_SendCommand(Extension,
                                      XHCI_TRB_TYPE_DISABLE_SLOT,
                                      0,
                                      0,
                                      XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                                      XHCI_COMMAND_TIMEOUT_MS,
                                      FALSE,
                                      NULL,
                                      NULL);
            if (Status != MP_STATUS_SUCCESS)
            {
                DPRINT1("usbxhci: disable slot %u failed status=%lu\n",
                        Slot->SlotId, Status);
                Slot->DisablePending = FALSE;
            }
        }
    }

    ChangeMask = PortSc & XHCI_PORTSC_CHANGE_MASK;
    if (ChangeMask)
    {
        ULONG OriginalMask = ChangeMask;

        if (ChangeMask != 0 && PortId <= XHCI_MAX_PORTS)
        {
            ULONG PreviousMask = (ULONG)InterlockedOr(
                (volatile LONG *)&Extension->PortChangeMask[PortId],
                ChangeMask);

            if (((~PreviousMask) & ChangeMask) == 0)
                NotifyHub = FALSE;
        }

        XHCI_AckPortChangeInternal(Extension, PortId, OriginalMask, FALSE);
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
XHCI_ScheduleTransferPoll(
    _Inout_ PXHCI_EXTENSION Extension)
{
    LARGE_INTEGER DueTime;

    if (!Extension)
        return;

    DueTime.QuadPart = -(LONGLONG)XHCI_TRANSFER_POLL_INTERVAL_US * 10;
    KeSetTimer(&Extension->TransferPollTimer, DueTime, &Extension->TransferPollDpc);
}

static
VOID
NTAPI
XHCI_TransferPollDpc(
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

    if (InterlockedCompareExchange(&Extension->TransferPollCounter, 0, 0) > 0)
        XHCI_ScheduleTransferPoll(Extension);
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

        PortSc = XHCI_READ_REGISTER_ULONG(PortScReg);
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

/**
 * @brief Drain deferred transfer completions belonging to a specific slot.
 *
 * When a slot is being disabled due to device disconnect, any transfers that
 * were deferred (queued on DeferredTransferList because AllowCallbacks was
 * FALSE at the time of the transfer event) must be completed BEFORE the slot
 * state is torn down. Otherwise, the deferred drain that runs later would
 * pass stale TransferParameters pointers to USBPORT after the USBPORT_TRANSFER
 * structures have been freed by the abort path, causing a use-after-free crash.
 *
 * This function removes only transfers belonging to the specified slot and
 * completes them immediately. Transfers for other slots remain on the list.
 */
static
VOID
XHCI_FlushDeferredCompletionsForSlot(
    _Inout_ PXHCI_EXTENSION Extension,
    _In_ UCHAR SlotId)
{
    LIST_ENTRY SlotList;
    LIST_ENTRY KeepList;
    KIRQL OldIrql;

    if (!Extension)
        return;

    InitializeListHead(&SlotList);
    InitializeListHead(&KeepList);

    /* Partition the deferred list: transfers for this slot go to SlotList,
     * everything else goes to KeepList. */
    KeAcquireSpinLock(&Extension->DeferredTransferLock, &OldIrql);
    while (!IsListEmpty(&Extension->DeferredTransferList))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&Extension->DeferredTransferList);
        PXHCI_TRANSFER Transfer = CONTAINING_RECORD(Entry, XHCI_TRANSFER, ListEntry);

        if (Transfer->Endpoint && Transfer->Endpoint->SlotId == SlotId)
            InsertTailList(&SlotList, Entry);
        else
            InsertTailList(&KeepList, Entry);
    }

    /* Re-populate the deferred list with non-matching transfers */
    while (!IsListEmpty(&KeepList))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&KeepList);
        InsertTailList(&Extension->DeferredTransferList, Entry);
    }
    KeReleaseSpinLock(&Extension->DeferredTransferLock, OldIrql);

    /* Now complete the slot's deferred transfers outside the lock */
    while (!IsListEmpty(&SlotList))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&SlotList);
        PXHCI_TRANSFER Transfer = CONTAINING_RECORD(Entry, XHCI_TRANSFER, ListEntry);
        PXHCI_ENDPOINT Endpoint = Transfer->Endpoint;

        if (!Endpoint || !Transfer->TransferParameters)
            continue;

        /* Mark as completed to prevent any other path from double-completing */
        if (InterlockedBitTestAndSet((volatile LONG *)&Transfer->Flags,
                                     XHCI_TRANSFER_FLAG_COMPLETED_BIT))
        {
            DPRINT1("usbxhci: slot %u deferred flush skipping already-completed transfer %p\n",
                    SlotId, Transfer);
            continue;
        }

        DPRINT1("usbxhci: slot %u flushing deferred transfer %p ep=%u\n",
                SlotId, Transfer, Endpoint->EndpointId);

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
    }
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

        /*
         * Guard against double-completion. Another path (disable-slot
         * force-complete, SW-enum worker, or StopController drain) may
         * have already handed this transfer to USBPORT. Completing it
         * again would pass a stale TransferParameters pointer into
         * USBPORT_MiniportCompleteTransfer, causing a use-after-free
         * crash when USBPORT does CONTAINING_RECORD on freed memory.
         *
         * The XHCI_TRANSFER_FLAG_COMPLETED flag is set atomically by
         * whichever path completes the transfer first.
         */
        if (InterlockedBitTestAndSet((volatile LONG *)&Transfer->Flags,
                                     XHCI_TRANSFER_FLAG_COMPLETED_BIT))
        {
            DPRINT1("usbxhci: deferred drain skipping already-completed transfer %p (flags=0x%lx)\n",
                    Transfer, Transfer->Flags);
            continue;
        }

        if (!Transfer->TransferParameters)
        {
            DPRINT1("usbxhci: deferred drain skipping transfer %p with NULL TransferParameters\n",
                    Transfer);
            continue;
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
    PXHCI_RING Ring = NULL;
    ULONG UsbdStatus;
    ULONG RequestedLength;
    KIRQL OldIrql;
    BOOLEAN TraceBos = FALSE;
    BOOLEAN TraceDevDesc = FALSE;
    BOOLEAN DeviceGone = FALSE;

    if (!Extension || !EventTrb || Extension->FatalError)
        return;

    TrbPointer = ((ULONGLONG)EventTrb->Parameter2 << 32) |
                 EventTrb->Parameter1;
    CompletionCode = XHCI_GET_COMPLETION_CODE(EventTrb->Status);
    Remaining = EventTrb->Status & XHCI_TRB_LEN_MASK;
    SlotId = (UCHAR)XHCI_TRB_TO_SLOT_ID(EventTrb->Control);
    EndpointId = (UCHAR)XHCI_TRB_TO_EP_ID(EventTrb->Control);

    /* Log errors and short packets (code != 1 = success) */
    if (CompletionCode != 1)
    {
        if (CompletionCode == XHCI_COMPLETION_SHORT_PACKET)
        {
            DPRINT("xhci: XFER_EVT slot=%u ep=%u code=%lu remain=%lu ptr=%I64x\n",
                   SlotId, EndpointId, CompletionCode, Remaining, TrbPointer);
        }
        else
        {
            DPRINT1("xhci: XFER_EVT slot=%u ep=%u code=%lu remain=%lu ptr=%I64x\n",
                    SlotId, EndpointId, CompletionCode, Remaining, TrbPointer);
        }
    }

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
    Ring = XHCI_SelectStreamRing(Endpoint, Transfer->StreamId);

    /* Diagnostic: log bulk transfer event matching details */
    if (EndpointId >= 3 && CompletionCode != 1)
    {
        DPRINT1("xhci: BULK_TRACE slot=%u ep=%u code=%lu ptr=%I64x exp=%I64x first=%I64x reqLen=%lu isCtrl=%u\n",
                SlotId, EndpointId, CompletionCode,
                TrbPointer,
                (ULONGLONG)Transfer->CompletionTrbPointer,
                (ULONGLONG)Transfer->TdFirstTrbPointer,
                Transfer->RequestedLength,
                Transfer->IsControl);
    }

    {
        PUSBPORT_TRANSFER_PARAMETERS Params = Transfer->TransferParameters;
        if (Transfer->IsControl &&
            Params &&
            Params->SetupPacket.bRequest == USB_REQUEST_GET_DESCRIPTOR)
        {
            UCHAR DescType = Params->SetupPacket.wValue.HiByte;

            if (DescType == USB_BOS_DESCRIPTOR_TYPE)
                TraceBos = TRUE;
            else if (DescType == USB_DEVICE_DESCRIPTOR_TYPE)
                TraceDevDesc = TRUE;
        }
    }

    /* Calculate BytesTransferred early for the intermediate check */
    RequestedLength = Transfer->RequestedLength;
    if (RequestedLength == 0 && Transfer->TransferParameters)
        RequestedLength = Transfer->TransferParameters->TransferBufferLength;
    if (RequestedLength < Remaining)
        Remaining = RequestedLength;
    BytesTransferred = RequestedLength - Remaining;

    if (Slot && (Slot->DisablePending || !Slot->InUse))
        DeviceGone = TRUE;

    /*
     * Only complete the currently active transfer when the controller reports
     * completion for the TRB that we armed with IOC. QEMU can deliver stale or
     * intermediate transfer events; completing on those breaks enumeration.
     *
     * EXCEPTION: Error completions (STALL, Babble, etc.) terminate the TD
     * immediately at the faulting TRB, which won't be the IOC TRB. We must
     * always process error events or the endpoint stays halted forever.
     */
    if (!DeviceGone &&
        (CompletionCode == XHCI_COMPLETION_SUCCESS ||
         CompletionCode == XHCI_COMPLETION_SHORT_PACKET) &&
        (Transfer->CompletionTrbPointer != 0) &&
        (Transfer->CompletionTrbPointer != TrbPointer))
    {
        BOOLEAN InTdRange = (Transfer->TdFirstTrbPointer == 0);
        DPRINT("usbxhci: TRB_MISMATCH slot=%u ep=%u exp=%I64x got=%I64x first=%I64x isCtrl=%u code=%lu\n",
               SlotId, EndpointId,
               (ULONGLONG)Transfer->CompletionTrbPointer,
               (ULONGLONG)TrbPointer,
               (ULONGLONG)Transfer->TdFirstTrbPointer,
               Transfer->IsControl,
               CompletionCode);

        if (Transfer->TdFirstTrbPointer != 0)
        {
            ULONGLONG First = Transfer->TdFirstTrbPointer;
            ULONGLONG Last = Transfer->CompletionTrbPointer;

            if (First <= Last)
                InTdRange = (TrbPointer >= First && TrbPointer <= Last);
            else
                InTdRange = (TrbPointer >= First || TrbPointer <= Last);
        }

        if (!InTdRange)
        {
            if (EndpointId >= 3)
            {
                DPRINT1("xhci: BULK_DROPPED slot=%u ep=%u code=%lu ptr=%I64x exp=%I64x first=%I64x reqLen=%lu\n",
                        SlotId, EndpointId, CompletionCode,
                        TrbPointer,
                        (ULONGLONG)Transfer->CompletionTrbPointer,
                        (ULONGLONG)Transfer->TdFirstTrbPointer,
                        Transfer->RequestedLength);
            }
#if DBG
            XHCI_DBG(XHCI_TRACE_TRANSFERS,
                     "usbxhci: transfer event pointer mismatch slot=%u ep=%u exp=%I64x got=%I64x code=%lu\n",
                     SlotId,
                     EndpointId,
                     (ULONGLONG)Transfer->CompletionTrbPointer,
                     (ULONGLONG)TrbPointer,
                     CompletionCode);
            if (TraceBos)
            {
                XHCI_DBG(XHCI_TRACE_TRANSFERS,
                         "usbxhci: BOS mismatch slot=%u ep=%u exp=%I64x got=%I64x code=%lu\n",
                         SlotId,
                         EndpointId,
                         (ULONGLONG)Transfer->CompletionTrbPointer,
                         (ULONGLONG)TrbPointer,
                         CompletionCode);
            }
            if (TraceDevDesc)
            {
                XHCI_DBG(XHCI_TRACE_TRANSFERS,
                         "usbxhci: DEV mismatch slot=%u ep=%u exp=%I64x got=%I64x code=%lu\n",
                         SlotId,
                         EndpointId,
                         (ULONGLONG)Transfer->CompletionTrbPointer,
                         (ULONGLONG)TrbPointer,
                         CompletionCode);
            }
#endif
            KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
            return;
        }

        if ((CompletionCode == XHCI_COMPLETION_SHORT_PACKET ||
             CompletionCode == XHCI_COMPLETION_SUCCESS) &&
            Transfer->IsControl)
        {
#if DBG
            XHCI_DBG(XHCI_TRACE_TRANSFERS,
                     "usbxhci: intermediate control transfer event slot=%u ep=%u exp=%I64x got=%I64x code=%lu\n",
                     SlotId,
                     EndpointId,
                     (ULONGLONG)Transfer->CompletionTrbPointer,
                     (ULONGLONG)TrbPointer,
                     CompletionCode);
#endif
            /* Control transfer: Update transferred count but wait for the
             * Status Stage event. Set flag so the handler knows not to
             * recalculate. */
            Transfer->BytesTransferred = BytesTransferred;
            Transfer->Flags |= XHCI_TRANSFER_FLAG_DATA_STAGE_DONE;
            KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
            return;
        }

        /*
         * Non-control transfer (bulk/interrupt): a Short Packet IS the final
         * completion event.  Per xHCI spec section 4.10.1.1, after a short
         * packet the xHC skips remaining TRBs in the TD and does NOT generate
         * further events for this transfer.  We must complete the transfer now.
         * Falling through to the normal completion path handles this correctly.
         */

#if DBG
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: transfer event within TD range slot=%u ep=%u exp=%I64x got=%I64x code=%lu\n",
                 SlotId,
                 EndpointId,
                 (ULONGLONG)Transfer->CompletionTrbPointer,
                 (ULONGLONG)TrbPointer,
                 CompletionCode);
#endif
    }
    if (TraceBos)
    {
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: BOS complete slot=%u ep=%u trb=%I64x exp=%I64x bytes=%lu rem=%lu code=%lu\n",
                 SlotId,
                 EndpointId,
                 (ULONGLONG)TrbPointer,
                 (ULONGLONG)Transfer->CompletionTrbPointer,
                 BytesTransferred,
                 Remaining,
                 CompletionCode);
    }
    if (TraceDevDesc)
    {
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: DEV complete slot=%u ep=%u trb=%I64x exp=%I64x bytes=%lu rem=%lu code=%lu\n",
                 SlotId,
                 EndpointId,
                 (ULONGLONG)TrbPointer,
                 (ULONGLONG)Transfer->CompletionTrbPointer,
                 BytesTransferred,
                 Remaining,
                 CompletionCode);
    }

    /* Update dequeue pointer for the ring used by this transfer */
    Ring = XHCI_SelectStreamRing(Endpoint, Transfer->StreamId);
    if (Ring)
    {
        Ring->DequeueIndex = Ring->EnqueueIndex;
    }

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

    /*
     * For control transfers, the final completion event comes from the Status
     * Stage TRB which has 0 length, making Remaining=0 always. If a SHORT_PACKET
     * or SUCCESS event occurred during the Data Stage, Transfer->BytesTransferred
     * was already set correctly and XHCI_TRANSFER_FLAG_DATA_STAGE_DONE was set.
     * Use that value instead of recalculating as RequestedLength - 0 = RequestedLength,
     * which would incorrectly report the full buffer size instead of actual bytes received.
     *
     * This fixes corrupted string descriptors (serial numbers, product names)
     * when the device returns less data than the maximum requested.
     */
    if (Transfer->IsControl && (Transfer->Flags & XHCI_TRANSFER_FLAG_DATA_STAGE_DONE))
    {
        BytesTransferred = Transfer->BytesTransferred;
        DPRINT("usbxhci: control transfer using Data Stage BytesTransferred=%lu (flag set)\n",
               BytesTransferred);
    }
    else
    {
        BytesTransferred = RequestedLength - Remaining;
        if (Transfer->IsControl && RequestedLength > 0)
        {
            /*
             * Some controllers (e.g., Intel Alder Lake-N) may not generate
             * intermediate Data Stage events even with IOC set. Log this
             * situation for debugging. The bounce buffer zeroing ensures
             * garbage data beyond actual received bytes will be zeros.
             */
            DPRINT("usbxhci: control transfer no Data Stage event, BytesTransferred=%lu (req=%lu rem=%lu)\n",
                   BytesTransferred, RequestedLength, Remaining);
        }
    }

    if (DeviceGone)
    {
        DPRINT1("usbxhci: slot %u removed, dropping ep=%u transfer (device gone)\n",
                SlotId, EndpointId);

        /*
         * The device is gone (slot disabled or disable-pending). Do NOT
         * complete this transfer to USBPORT from the miniport side. Instead,
         * ActiveTransfer was already NULLed above (under the endpoint lock),
         * so USBPORT's own abort mechanism will handle the completion
         * through its AbortTransfers -> DmaEndpointPaused -> FlushCancelList
         * path.
         *
         * Completing from here (either directly or via deferred completion)
         * races with USBPORT_AbortTransfers which also processes this
         * transfer. The race causes USBPORT_QueueDoneTransfer's
         * RemoveEntryList (without EndpointSpinLock) to corrupt the
         * TransferList while USBPORT_DmaEndpointPaused iterates it with
         * EndpointSpinLock held. The list corruption leads to a
         * use-after-free crash when USBPORT follows a stale TransferLink
         * pointer to freed memory.
         *
         * Finalize the bounce buffer so USBPORT doesn't see stale DMA
         * state, and cancel the poll timer if needed.
         */
        XHCI_FinalizeBounceBuffer(Transfer);
        if (Transfer->Flags & XHCI_TRANSFER_FLAG_NEEDS_POLL)
        {
            Transfer->Flags &= ~XHCI_TRANSFER_FLAG_NEEDS_POLL;
            if (InterlockedDecrement(&Extension->TransferPollCounter) <= 0)
                KeCancelTimer(&Extension->TransferPollTimer);
        }
        return;
    }
    XHCI_DBG(XHCI_TRACE_TRANSFERS,
             "XHCI_Event: S%u E%u Code=%u Rem=%u Ptr=%I64x\n",
             SlotId,
             EndpointId,
             CompletionCode,
             Remaining,
             TrbPointer);

    if ((Transfer->Flags & XHCI_TRANSFER_FLAG_GET_DESCRIPTOR) &&
        Transfer->TransferParameters &&
        Transfer->TransferParameters->SetupPacket.wValue.HiByte == USB_CONFIGURATION_DESCRIPTOR_TYPE)
    {
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: cfg desc event S%u E%u code=%lu req=%lu rem=%lu bytes=%lu ptr=%I64x first=%I64x last=%I64x\n",
                 SlotId,
                 EndpointId,
                 CompletionCode,
                 RequestedLength,
                 Remaining,
                 BytesTransferred,
                 TrbPointer,
                 (ULONGLONG)Transfer->TdFirstTrbPointer,
                 (ULONGLONG)Transfer->CompletionTrbPointer);
    }

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
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: bulk xfer event S%u E%u Code=%lu Req=%lu Rem=%lu Bytes=%lu Ptr=%I64x\n",
                 SlotId,
                 EndpointId,
                 CompletionCode,
                 RequestedLength,
                 Remaining,
                 BytesTransferred,
                 TrbPointer);
    }

    /* Log all transfer completions for debugging */
    if (CompletionCode != XHCI_COMPLETION_SUCCESS)
    {
        PUSBPORT_TRANSFER_PARAMETERS TParams = Transfer->TransferParameters;
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: transfer complete slot=%u ep=%u code=%lu req=%lu bytes=%lu setupType=0x%02x bReq=0x%02x\n",
                 SlotId,
                 EndpointId,
                 CompletionCode,
                 RequestedLength,
                 BytesTransferred,
                 TParams ? TParams->SetupPacket.bmRequestType.B : 0xFF,
                 TParams ? TParams->SetupPacket.bRequest : 0xFF);
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

        case XHCI_COMPLETION_BABBLE_ERROR:
            /* Babble indicates the device sent more data than expected.
             * Map to BABBLE_DETECTED for proper upper-layer handling. */
            UsbdStatus = USBD_STATUS_BABBLE_DETECTED;
            break;

        case XHCI_COMPLETION_DATA_BUFFER_ERROR:
            /* Data buffer error: either overrun or underrun depending on direction */
            UsbdStatus = (Transfer && Transfer->TransferParameters &&
                         (Transfer->TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN))
                         ? USBD_STATUS_DATA_OVERRUN
                         : USBD_STATUS_DATA_UNDERRUN;
            break;

        case XHCI_COMPLETION_USB_TRANSACTION_ERROR:
            /* USB transaction errors (CRC, timeout, bitstuff, etc.) */
            UsbdStatus = USBD_STATUS_CRC;
            break;

        case XHCI_COMPLETION_STOPPED:
        case XHCI_COMPLETION_STOPPED_LENGTH_INVALID:
        case XHCI_COMPLETION_STOPPED_SHORT_PACKET:
        case XHCI_COMPLETION_COMMAND_ABORTED:
            /* Endpoint stopped (typically due to cancel/reset) - surface as a
             * canceled transfer rather than a generic failure to better match
             * Windows USBPORT semantics. */
            UsbdStatus = USBD_STATUS_CANCELED;
            break;

        case XHCI_COMPLETION_RING_UNDERRUN:
        case XHCI_COMPLETION_RING_OVERRUN:
        case XHCI_COMPLETION_MISSED_SERVICE:
        case XHCI_COMPLETION_ISOCH_BUFFER_OVERRUN:
            /* Isochronous/streaming timing errors */
            UsbdStatus = USBD_STATUS_ISOCH_REQUEST_FAILED;
            break;

        case XHCI_COMPLETION_NO_PING_RESPONSE:
            /* No ping response - typically a device timeout on SuperSpeed */
            UsbdStatus = USBD_STATUS_DEV_NOT_RESPONDING;
            break;

        case XHCI_COMPLETION_BANDWIDTH_ERROR:
        case XHCI_COMPLETION_BANDWIDTH_OVERRUN:
        case XHCI_COMPLETION_SECONDARY_BANDWIDTH:
            /* Bandwidth allocation failures */
            UsbdStatus = USBD_STATUS_NO_BANDWIDTH;
            break;

        case XHCI_COMPLETION_RESOURCE_ERROR:
        case XHCI_COMPLETION_NO_SLOTS_ERROR:
        case XHCI_COMPLETION_EVENT_RING_FULL:
        case XHCI_COMPLETION_VF_EVENT_RING_FULL:
            /* Resource exhaustion */
            UsbdStatus = USBD_STATUS_INSUFFICIENT_RESOURCES;
            break;

        case XHCI_COMPLETION_CONTEXT_ERROR:
        case XHCI_COMPLETION_CONTEXT_STATE_ERROR:
        case XHCI_COMPLETION_PARAMETER_ERROR:
        case XHCI_COMPLETION_SLOT_NOT_ENABLED:
        case XHCI_COMPLETION_ENDPOINT_NOT_ENABLED:
        case XHCI_COMPLETION_INVALID_STREAM_TYPE:
        case XHCI_COMPLETION_INVALID_STREAM_ID:
            /* Driver or hardware configuration errors */
            UsbdStatus = USBD_STATUS_INTERNAL_HC_ERROR;
            break;

        case XHCI_COMPLETION_INCOMPATIBLE_DEVICE:
            /* Device is incompatible with the port type/speed */
            UsbdStatus = USBD_STATUS_ERROR_BUSY;
            break;

        case XHCI_COMPLETION_SPLIT_TRANSACTION:
            /* Split transaction error (for HS/FS devices behind TT) */
            UsbdStatus = USBD_STATUS_XACT_ERROR;
            break;

        case XHCI_COMPLETION_MAX_EXIT_LATENCY_ERROR:
            /* Power management constraint violation */
            UsbdStatus = USBD_STATUS_ERROR_BUSY;
            break;

        case XHCI_COMPLETION_EVENT_LOST:
        case XHCI_COMPLETION_UNDEFINED_ERROR:
        default:
            UsbdStatus = USBD_STATUS_REQUEST_FAILED;
            break;
    }

    /*
     * Control endpoint errors can leave EP0 halted until a reset is issued.
     * Treat STALL and USB_TRANSACTION_ERROR the same so the next control
     * transfer does not hang waiting on a halted endpoint.
     */
    if ((CompletionCode == XHCI_COMPLETION_STALL_ERROR ||
         CompletionCode == XHCI_COMPLETION_USB_TRANSACTION_ERROR) &&
        Endpoint->DefaultControl &&
        Endpoint->Slot &&
        Endpoint->Slot->InUse &&
        !(Endpoint->Slot->DisablePending))
    {
        /*
         * EP0 STALL/transaction error leaves the endpoint in Halted state.
         * We must reset it before the next control transfer can proceed.
         *
         * Always do inline reset here in the completion handler. This ensures
         * the endpoint is ready before USBPORT tries to submit the next
         * enumeration transfer. If we deferred to an async worker, there's a
         * race where USBPORT submits before the worker runs, causing the
         * submission to fail and enumeration to stall (seen on Intel Alder Lake
         * with USB 2.0 devices that STALL on BOS descriptor requests).
         *
         * The inline reset uses polling-based command waits at DISPATCH_LEVEL,
         * which adds some latency to the DPC but avoids the race condition.
         * Modern xHCI controllers complete RESET_EP and SET_TR_DEQUEUE quickly
         * (typically under 50ms total).
         */
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: EP0 error code=%lu, doing inline reset\n",
                 CompletionCode);
        XHCI_PerformEndpointResetSequence(Extension, Endpoint, FALSE);
        InterlockedExchange(&Endpoint->Slot->Ep0NeedsStallReset, 0);
        InterlockedExchange(&Endpoint->Slot->Ep0StallResetQueued, 0);
        KeSetEvent(&Endpoint->Slot->Ep0StallResetEvent,
                   IO_NO_INCREMENT,
                   FALSE);
    }

    Transfer->CompletionTrbPointer = TrbPointer;
    Transfer->BytesTransferred = BytesTransferred;
    Transfer->UsbdStatus = UsbdStatus;
    XHCI_FinalizeBounceBuffer(Transfer);
    if (Transfer->Flags & XHCI_TRANSFER_FLAG_NEEDS_POLL)
    {
        Transfer->Flags &= ~XHCI_TRANSFER_FLAG_NEEDS_POLL;
        if (InterlockedDecrement(&Extension->TransferPollCounter) <= 0)
            KeCancelTimer(&Extension->TransferPollTimer);
    }

    if (Transfer->Flags & (XHCI_TRANSFER_FLAG_SET_ADDRESS | XHCI_TRANSFER_FLAG_GET_DESCRIPTOR))
    {
    }

    /* Track aggregate bandwidth usage for periodic endpoints (iso/int). */
    if (Transfer->IsIsochronous ||
        Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
        Endpoint->TotalBytesTransferred += BytesTransferred;

    if (Transfer->Flags & (XHCI_TRANSFER_FLAG_SET_ADDRESS | XHCI_TRANSFER_FLAG_GET_DESCRIPTOR))
        XHCI_HandleEnumerationTransfer(Extension, Endpoint, Transfer);

    if (!AllowCallbacks)
    {
        DPRINT1("usbxhci: DEFERRED completion slot=%u ep=%u code=%lu bytes=%lu\n",
                SlotId, EndpointId, CompletionCode, Transfer->BytesTransferred);
        XHCI_QueueDeferredTransferCompletion(Extension, Transfer);
        return;
    }

    /* Atomically test-and-set the COMPLETED flag before handing to USBPORT.
     * This prevents double-completion if another path (e.g., disable-slot
     * force-complete or StopController drain) races with us. If the bit
     * was already set, another path already completed this transfer. */
    if (InterlockedBitTestAndSet((volatile LONG *)&Transfer->Flags,
                                 XHCI_TRANSFER_FLAG_COMPLETED_BIT))
    {
        DPRINT1("usbxhci: transfer %p already completed, skipping double completion\n",
                Transfer);
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
        if (Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK)
        {
            XHCI_DBG(XHCI_TRACE_TRANSFERS, "xhci: BulkDone slot=%u ep=%u code=%lu usbd=0x%lx bytes=%lu\n",
                    SlotId, EndpointId, CompletionCode,
                    Transfer->UsbdStatus, Transfer->BytesTransferred);
        }
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
    Slot->DisablePending = FALSE;
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
    KeInitializeEvent(&Slot->Ep0StallResetEvent, NotificationEvent, TRUE);
    InterlockedExchange(&Slot->Ep0NeedsStallReset, 0);
    InterlockedExchange(&Slot->Ep0StallResetQueued, 0);

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
    ULONG RawMfIndex;
    ULONG Result;

    if (!Extension || !Extension->RuntimeRegisters)
        return 0;

    /*
     * xHCI MFINDEX is a 14-bit microframe counter that wraps every ~2 seconds.
     * USBPORT expects a monotonically increasing 32-bit value (used for
     * frame-boundary checks in endpoint state changes). Track wraps to
     * synthesize a continuous counter.
     *
     * This function is always called with MiniportSpinLock held, so no
     * additional synchronization is needed.
     */
    RawMfIndex = XHCI_READ_REGISTER_ULONG(&Extension->RuntimeRegisters->MicroframeIndex) & 0x3FFF;

    if (RawMfIndex < Extension->LastMfIndex)
        Extension->FrameHighBits += 0x4000;

    Extension->LastMfIndex = RawMfIndex;
    Result = Extension->FrameHighBits | RawMfIndex;

    XHCI_DBG(XHCI_TRACE_EVENTS,
             "usbxhci: Get32BitFrameNumber (IRQL=%lu) MFIDX=%04lx Result=%08lx\n",
             (ULONG)KeGetCurrentIrql(), RawMfIndex, Result);

    return Result;
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
                Endpoint->EndpointId < RTL_NUMBER_OF(Endpoint->Slot->EndpointTable) &&
                Endpoint->Slot->EndpointTable[Endpoint->EndpointId] == Endpoint)
            {
                XHCI_DropSlotEndpoint(Extension, Endpoint->Slot, Endpoint->EndpointId);
                /*
                 * Re-check ownership after the CONFIG_EP drop: OpenEndpoint on
                 * another CPU (running without MiniportSpinLock) may have replaced
                 * our entry during the busy-poll.  Only NULL if we still own it.
                 */
                if (Endpoint->Slot->EndpointTable[Endpoint->EndpointId] == Endpoint)
                    Endpoint->Slot->EndpointTable[Endpoint->EndpointId] = NULL;
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
        case XHCI_EPCTX_STATE_STOPPED:
            /*
             * Per xHCI spec, endpoints transition from Stopped to Running when
             * the doorbell is rung with pending TRBs. After Configure Endpoint
             * command, the endpoint is in Stopped state but is ready to accept
             * transfers. Report ACTIVE so USBPORT doesn't wait indefinitely
             * for a state transition that requires traffic to trigger.
             */
            return USBPORT_ENDPOINT_ACTIVE;

        case XHCI_EPCTX_STATE_HALTED:
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

    /*
     * ADDRESS_DEVICE causes the xHC to initialize the endpoint context with
     * the TR Dequeue Pointer from the Input Context, which we set to the
     * start of the ring (index 0). We must reset the software ring state to
     * match, otherwise after a re-address (e.g., after RESET_DEV or failed
     * enumeration), the software EnqueueIndex will be out of sync with the
     * hardware's dequeue pointer, causing the xHC to process stale TRBs.
     *
     * Also clear all TRBs in the EP0 ring to ensure no stale transfer TRBs
     * with matching cycle bits remain from previous attempts.
     */
    Slot->Ep0RingCycleState = 1;
    Slot->Ep0RingEnqueueIndex = 0;
    Slot->Ep0RingDequeueIndex = 0;

    if (Slot->Ep0TransferRing.VirtualAddress && Slot->Ep0TransferRing.Length > 0)
    {
        PXHCI_TRB Ep0Ring = (PXHCI_TRB)Slot->Ep0TransferRing.VirtualAddress;
        ULONG TrbCount = (ULONG)(Slot->Ep0TransferRing.Length / sizeof(XHCI_TRB));
        ULONG i;

        /* Clear all TRBs except the last one (Link TRB) */
        for (i = 0; i + 1 < TrbCount; i++)
        {
            Ep0Ring[i].Parameter1 = 0;
            Ep0Ring[i].Parameter2 = 0;
            Ep0Ring[i].Status = 0;
            Ep0Ring[i].Control = 0;
        }

        /* Reinitialize the Link TRB at the end of the ring */
        if (TrbCount > 1)
        {
            PXHCI_TRB LinkTrb = &Ep0Ring[TrbCount - 1];
            ULONGLONG RingBase = Slot->Ep0TransferRing.PhysicalAddress.QuadPart;

            LinkTrb->Parameter1 = (ULONG)(RingBase & 0xFFFFFFFF);
            LinkTrb->Parameter2 = (ULONG)(RingBase >> 32);
            LinkTrb->Status = 0;
            LinkTrb->Control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                               XHCI_TRB_TOGGLE_CYCLE |
                               XHCI_TRB_CYCLE;
        }
    }

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
            ULONG PortValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);
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

    DPRINT("usbxhci: prepared input context for slot %u (MPS=%lu, speed=%lu)\n",
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
    ULONG Attempt;
    ULONG DelayMs;
    LARGE_INTEGER Interval;

    /*
     * Maximum retries for USB_TRANSACTION_ERROR on ADDRESS_DEVICE.
     * Real hardware (e.g., Intel N100) may need extra time for devices
     * to stabilize after port reset, especially USB 2.0 devices behind
     * USB 3.x root hub ports.
     */
#define ADDR_DEV_MAX_RETRIES 3
#define ADDR_DEV_INITIAL_DELAY_MS 10
#define ADDR_DEV_MAX_DELAY_MS 100
#define ADDR_DEV_PORT_ENABLE_WAIT_MS 50
#define ADDR_DEV_PORT_ENABLE_POLL_MS 5

    if (!Extension || !Slot || !EndpointProperties)
        return MP_STATUS_ERROR;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return MP_STATUS_FAILURE;

    SlotId = Slot->SlotId;
    DelayMs = ADDR_DEV_INITIAL_DELAY_MS;

    /*
     * Verify the port is enabled before issuing ADDRESS_DEVICE.
     * After the fix in XHCI_RH_SetFeaturePortReset, the port should already
     * be enabled (PED=1) when we get here. This check is a safety net.
     *
     * If PED=0, the device failed to enumerate at the negotiated speed and
     * ADDRESS_DEVICE will fail with USB_TRANSACTION_ERROR. Fail early with
     * a clear error message rather than attempting the command.
     */
    if (EndpointProperties->PortNumber > 0 &&
        EndpointProperties->PortNumber <= Extension->NumberOfPorts)
    {
        volatile ULONG *PortScReg;
        ULONG PortSc;
        ULONG WaitMs = 0;

        PortScReg = XHCI_GetPortStatusRegister(Extension, EndpointProperties->PortNumber);
        if (PortScReg)
        {
            PortSc = XHCI_READ_REGISTER_ULONG(PortScReg);

            /* Brief wait for port enable - should already be set from reset */
            while ((PortSc & XHCI_PORTSC_CCS) &&
                   !(PortSc & XHCI_PORTSC_PED) &&
                   WaitMs < ADDR_DEV_PORT_ENABLE_WAIT_MS)
            {
                if (WaitMs == 0)
                {
                    DPRINT1("usbxhci: ADDRESS_DEVICE waiting for port %u enable (PortSC=0x%08lx)\n",
                            EndpointProperties->PortNumber, PortSc);
                }

                Interval.QuadPart = -(LONGLONG)ADDR_DEV_PORT_ENABLE_POLL_MS * 10000LL;
                KeDelayExecutionThread(KernelMode, FALSE, &Interval);
                WaitMs += ADDR_DEV_PORT_ENABLE_POLL_MS;

                PortSc = XHCI_READ_REGISTER_ULONG(PortScReg);
            }

            if (WaitMs > 0)
            {
                DPRINT1("usbxhci: ADDRESS_DEVICE port %u after %lu ms wait: PortSC=0x%08lx (PED=%u)\n",
                        EndpointProperties->PortNumber, WaitMs, PortSc,
                        (PortSc & XHCI_PORTSC_PED) ? 1 : 0);
            }

            /* If port disconnected during wait, fail immediately */
            if (!(PortSc & XHCI_PORTSC_CCS))
            {
                DPRINT1("usbxhci: ADDRESS_DEVICE port %u device disconnected\n",
                        EndpointProperties->PortNumber);
                return MP_STATUS_FAILURE;
            }

            /*
             * If port is still not enabled after waiting, fail now rather than
             * attempting ADDRESS_DEVICE which will certainly fail with
             * USB_TRANSACTION_ERROR (code 4).
             */
            if (!(PortSc & XHCI_PORTSC_PED))
            {
                ULONG LinkState = (PortSc & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;
                DPRINT1("usbxhci: ADDRESS_DEVICE port %u not enabled (PED=0 PLS=%lu), cannot address device\n",
                        EndpointProperties->PortNumber, LinkState);
                DPRINT1("usbxhci: Port %u PORTSC=0x%08lx - device may have failed speed negotiation\n",
                        EndpointProperties->PortNumber, PortSc);

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
                return MP_STATUS_FAILURE;
            }
        }
    }

    for (Attempt = 0; Attempt < ADDR_DEV_MAX_RETRIES; Attempt++)
    {
        if (Attempt > 0)
        {
            /*
             * Wait before retry. Use KeDelayExecutionThread since we're
             * at PASSIVE_LEVEL in the SwEnumWorker context.
             */
            DPRINT1("usbxhci: ADDRESS_DEVICE retry %lu for slot %u, waiting %lu ms\n",
                    Attempt, SlotId, DelayMs);
            Interval.QuadPart = -(LONGLONG)DelayMs * 10000LL;
            KeDelayExecutionThread(KernelMode, FALSE, &Interval);

            /* Re-prepare context in case first attempt corrupted it */
            XHCI_PrepareDefaultControlContext(Extension, Slot, EndpointProperties);

            /* Increase delay for next retry (exponential backoff) */
            DelayMs = min(DelayMs * 2, ADDR_DEV_MAX_DELAY_MS);
        }
        else
        {
            XHCI_PrepareDefaultControlContext(Extension, Slot, EndpointProperties);
        }

        DPRINT("usbxhci: EP0 bring-up: issuing ADDRESS_DEV for slot %u port=%u (attempt %lu)\n",
               SlotId,
               EndpointProperties->PortNumber,
               Attempt + 1);

        Status = XHCI_SendCommand(Extension,
                                  XHCI_TRB_TYPE_ADDRESS_DEV,
                                  Slot->InputContext.PhysicalAddress.QuadPart,
                                  0,
                                  XHCI_COMMAND_SLOT_FIELD(SlotId),
                                  XHCI_COMMAND_TIMEOUT_MS,
                                  TRUE,
                                  NULL,
                                  &CompletionCode);

        if (Status == MP_STATUS_SUCCESS)
            break;

        /*
         * USB_TRANSACTION_ERROR (code 4) is retriable - the device may have
         * stalled briefly during enumeration. This is common on real hardware
         * but rare in QEMU/VirtualBox.
         */
        if ((UCHAR)CompletionCode != XHCI_COMPLETION_USB_TRANSACTION_ERROR)
            break; /* Non-retriable error, stop trying */
    }
    if (Status != MP_STATUS_SUCCESS)
    {
        UCHAR Cc = (UCHAR)CompletionCode;

        DPRINT1("usbxhci: AddressDevice failed for slot %u (Status=%lu, CompletionCode=%lu, Port=%u, MPS=%lu)\n",
                SlotId,
                Status,
                CompletionCode,
                EndpointProperties->PortNumber,
                EndpointProperties->MaxPacketSize);

        /*
         * USB_TRANSACTION_ERROR (code 4) on ADDRESS_DEVICE is common on real
         * hardware when the device stalls during enumeration. According to
         * xHCI spec 4.6.5, this may be due to a Stall response from the device.
         * The spec recommends issuing a Disable Slot command followed by an
         * Enable Slot command to recover.
         *
         * This is different from QEMU/VirtualBox behavior where ADDRESS_DEVICE
         * almost never fails with USB_TRANSACTION_ERROR.
         */
        if (Cc == XHCI_COMPLETION_USB_TRANSACTION_ERROR)
        {
            DPRINT1("usbxhci: USB_TRANSACTION_ERROR on ADDRESS_DEVICE for slot %u, "
                    "device may have stalled during enumeration\n", SlotId);
            XHCI_DumpInputContextForAddress(Extension, Slot);
            XHCI_DumpAddressDeviceContext(Extension,
                                          Slot,
                                          0,
                                          EndpointProperties->PortNumber,
                                          Cc);

            Slot->Ep0TransactionErrorCount++;
            DPRINT1("usbxhci: EP0 USB_TRANSACTION_ERROR count for slot %u is now %lu\n",
                    SlotId,
                    Slot->Ep0TransactionErrorCount);

            /*
             * Don't escalate to fatal immediately; USB_TRANSACTION_ERROR is
             * usually recoverable by retrying the enumeration at a higher level.
             */
            Status = MP_STATUS_FAILURE;
        }
        else if (Cc == XHCI_COMPLETION_CONTEXT_ERROR)
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

    DPRINT("usbxhci: EP0 bring-up: issuing ENABLE_SLOT for port %u (MPS=%lu)\n",
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

/* Virtual-port emulation removed. */
static MPSTATUS
XHCI_SubmitControlTransferSwEnum(
    _Inout_ PXHCI_EXTENSION Extension,
    _Inout_ PXHCI_ENDPOINT Endpoint,
    _Inout_ PXHCI_TRANSFER Transfer)
{
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Endpoint);
    UNREFERENCED_PARAMETER(Transfer);
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

    /* VID/DID logging + QEMU auto-detection. qemu-xhci advertises
     * VID=0x1b36 (Red Hat) DID=0x000d. On QEMU, MSI interrupt delivery
     * for interrupt-type endpoints is unreliable, causing HID mouse
     * cursor lag (~64Hz vs >=125Hz expected). Force the poll fallback
     * to also cover bulk/interrupt transfers on QEMU. */
    if (XHCI_ReadPciConfig(Extension, 0x00, &VendorId, sizeof(VendorId)) &&
        XHCI_ReadPciConfig(Extension, 0x02, &DeviceId, sizeof(DeviceId)))
    {
        DPRINT("usbxhci: PCI VID=%04x DID=%04x HciVer=%04x\n",
                VendorId,
                DeviceId,
                Extension->HciVersion);

        if (VendorId == 0x1B36 && DeviceId == 0x000D)
        {
            Extension->Quirks |= XHCI_QUIRK_IS_QEMU_XHCI |
                                 XHCI_QUIRK_QEMU_POLL_XFERS |
                                 XHCI_QUIRK_IGNORE_STARTUP_HCE |
                                 XHCI_QUIRK_QEMU_CONFIG_EP_ORDER |
                                 XHCI_QUIRK_QEMU_PORT_RESET;
            DPRINT("usbxhci: detected qemu-xhci, enabling QEMU quirks + poll fallback\n");
        }
    }

    /*
     * Apply registry overrides for quirk flags.
     * This is the single policy layer where registry overrides take precedence
     * over hardware detection results.
     */

    /* Non-coherent DMA override */
    if (g_XhciNonCoherentDmaOverrideValid)
    {
        if (g_XhciNonCoherentDmaOverride)
            Extension->Quirks |= XHCI_QUIRK_NON_COHERENT_DMA;
        else
            Extension->Quirks &= ~XHCI_QUIRK_NON_COHERENT_DMA;
    }

    /* Startup HCE override */
    if (g_XhciStartupHceQuirkOverrideValid)
    {
        if (g_XhciStartupHceQuirkOverride)
            Extension->Quirks |= XHCI_QUIRK_IGNORE_STARTUP_HCE;
        else
            Extension->Quirks &= ~XHCI_QUIRK_IGNORE_STARTUP_HCE;
    }

    /* Force 32-bit DMA override */
    if (g_XhciForce32BitDmaOverrideValid)
    {
        if (g_XhciForce32BitDmaOverride)
            Extension->Quirks |= XHCI_QUIRK_FORCE_32BIT_DMA;
        else
            Extension->Quirks &= ~XHCI_QUIRK_FORCE_32BIT_DMA;
    }

    /* VirtualBox quirks override - controls all VBox-related quirks as a group */
    if (g_XhciVBoxQuirksOverrideValid)
    {
        ULONG VBoxQuirks = XHCI_QUIRK_VBOX_PORT_RESET |
                          XHCI_QUIRK_VBOX_SPURIOUS_IMAN |
                          XHCI_QUIRK_VBOX_POLL_XFERS;
        if (g_XhciVBoxQuirksOverride)
            Extension->Quirks |= VBoxQuirks;
        else
            Extension->Quirks &= ~VBoxQuirks;
    }

    /* QEMU quirks override - controls all QEMU-related quirks as a group */
    if (g_XhciQemuQuirksOverrideValid)
    {
        ULONG QemuQuirks = XHCI_QUIRK_IGNORE_STARTUP_HCE |
                          XHCI_QUIRK_QEMU_CONFIG_EP_ORDER |
                          XHCI_QUIRK_QEMU_PORT_RESET |
                          XHCI_QUIRK_QEMU_POLL_XFERS;
        if (g_XhciQemuQuirksOverride)
            Extension->Quirks |= QemuQuirks;
        else
            Extension->Quirks &= ~QemuQuirks;
    }

    /* U1/U2 LPM limiting override */
    if (g_XhciLimitU1U2OverrideValid)
    {
        if (g_XhciLimitU1U2Override)
            Extension->Quirks |= XHCI_QUIRK_LIMIT_U1U2;
        else
            Extension->Quirks &= ~XHCI_QUIRK_LIMIT_U1U2;
    }

    DPRINT("usbxhci: quirks=0x%lx (32b=%u slow=%u legacy=%u nopid=%u limitU=%u noncoh=%u vbox=%u qemu=%u)\n",
            Extension->Quirks,
            XHCI_QuirkEnabled(Extension, XHCI_QUIRK_FORCE_32BIT_DMA) ? 1 : 0,
            XHCI_QuirkEnabled(Extension, XHCI_QUIRK_SLOW_HARD_RESET) ? 1 : 0,
            XHCI_QuirkEnabled(Extension, XHCI_QUIRK_LEGACY_BIOS_HANDOFF) ? 1 : 0,
            XHCI_QuirkEnabled(Extension, XHCI_QUIRK_NO_PORT_INDICATORS) ? 1 : 0,
            XHCI_QuirkEnabled(Extension, XHCI_QUIRK_LIMIT_U1U2) ? 1 : 0,
            XHCI_QuirkEnabled(Extension, XHCI_QUIRK_NON_COHERENT_DMA) ? 1 : 0,
            XHCI_QuirkEnabled(Extension, XHCI_QUIRK_VBOX_PORT_RESET) ? 1 : 0,
            XHCI_QuirkEnabled(Extension, XHCI_QUIRK_QEMU_PORT_RESET) ? 1 : 0);
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

        CapValue = XHCI_READ_REGISTER_ULONG(CapReg);
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

        CapValue = XHCI_READ_REGISTER_ULONG(CapReg);

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
            Revision = XHCI_READ_REGISTER_ULONG(&ProtoCap->Revision);
            PortInfo = XHCI_READ_REGISTER_ULONG(&ProtoCap->PortInfo);

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

    Extension->AllocatedCommonBuffer = NULL;
    Extension->AllocatedCommonBufferPhysical.QuadPart = 0;
    Extension->AllocatedCommonBufferSize = 0;

    {
        BOOLEAN NeedCommonBufferAlloc = FALSE;

        if (Extension->Quirks & XHCI_QUIRK_NON_COHERENT_DMA)
        {
            NeedCommonBufferAlloc = TRUE;
        }
        else if ((Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA) &&
                 (BasePa + Layout.TotalSize - 1) >= 0x100000000ULL)
        {
            NeedCommonBufferAlloc = TRUE;
        }

        if (NeedCommonBufferAlloc)
        {
            PHYSICAL_ADDRESS Low, High, Skip;
            MEMORY_CACHING_TYPE CacheType = XHCI_GetDmaCacheType(Extension);

            Low.QuadPart = 0;
            if (Extension && Extension->Supports64Bit &&
                !(Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA))
            {
                High.QuadPart = 0xFFFFFFFFFFFFFFFFULL;
            }
            else
            {
                High.QuadPart = 0xFFFFFFFFULL;
            }
            Skip.QuadPart = 0;

            /*
             * Use cached common buffers on coherent platforms; opt-in non-cached
             * buffers for non-coherent DMA configurations.
             */
            BaseVa = MmAllocateContiguousMemorySpecifyCache(SizeToZero,
                                                            Low,
                                                            High,
                                                            Skip,
                                                            CacheType);
            if (!BaseVa)
                return MP_STATUS_NO_RESOURCES;

            BasePa = (ULONGLONG)MmGetPhysicalAddress(BaseVa).QuadPart;
            if ((Extension->Quirks & XHCI_QUIRK_FORCE_32BIT_DMA) &&
                (BasePa + Layout.TotalSize - 1) >= 0x100000000ULL)
            {
                MmFreeContiguousMemory(BaseVa);
                return MP_STATUS_NO_RESOURCES;
            }
            Extension->AllocatedCommonBuffer = BaseVa;
            Extension->AllocatedCommonBufferPhysical.QuadPart = BasePa;
            Extension->AllocatedCommonBufferSize = SizeToZero;
        }
    }

    RtlZeroMemory(BaseVa, SizeToZero);

    Extension->HcResources = (PXHCI_HC_RESOURCES)BaseVa;
    Extension->HcResourcesPhysical.QuadPart = BasePa;
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

    DPRINT("usbxhci: common buffer layout size %Iu/%Iu bytes (CmdRing=%I64x EventRing=%I64x ERST=%I64x)\n",
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

    Value = XHCI_READ_REGISTER_ULONG(LegacySupport);
    if ((Value & XHCI_HC_BIOS_OWNED) == 0)
        return MP_STATUS_SUCCESS;

    Extension->Resources->LegacySupport = 1;
    XHCI_WRITE_REGISTER_ULONG(LegacySupport, Value | XHCI_HC_OS_OWNED);

    for (Retry = 0; Retry < TimeoutUs / 100; Retry++)
    {
        Value = XHCI_READ_REGISTER_ULONG(LegacySupport);
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

    Value = XHCI_READ_REGISTER_ULONG(LegacyControl);
    Value &= ~(XHCI_LEGACY_DISABLE_SMI | XHCI_LEGACY_SMI_EVENTS);
    XHCI_WRITE_REGISTER_ULONG(LegacyControl, Value);

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

#if !defined(_M_ARM64)
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
#endif /* !_M_ARM64 */

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
        DPRINT("usbxhci: PCI MEM/BusMaster already enabled (cmd=%04x)\n", Command);
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
                DPRINT("usbxhci: MSI-X control=0x%04x TableSize=%u enabled=%u\n",
                        MsixControl,
                        (MsixControl & 0x07FF) + 1,
                        (MsixControl & 0x8000) ? 1 : 0);
            }
        }

        if (Next == 0 || Next == CapPtr)
            break;
        CapPtr = Next;
    }

    DPRINT("usbxhci: PCI caps: MSI %s (enabled=%u) MSI-X %s (enabled=%u)\n",
            Extension->MsiSupported ? "yes" : "no",
            Extension->MsiEnabled ? 1 : 0,
            Extension->MsixSupported ? "yes" : "no",
            Extension->MsixEnabled ? 1 : 0);
}

#if !defined(_M_ARM64)
/*
 * XHCI_EnableMsix - Enable MSI-X in the PCI device
 *
 * On x86/x64, the miniport driver can enable MSI-X directly because the APIC
 * MSI address format is fixed and doesn't require HAL involvement.
 *
 * On ARM64, MSI-X requires the GIC ITS (Interrupt Translation Service) which
 * must be set up by the HAL. If ITS setup fails (e.g., QEMU HVF doesn't support
 * ITS), USBPORT falls back to legacy INTx. In this case, the miniport should
 * NOT try to enable MSI-X because the MSI-X table contains invalid addresses.
 * Therefore, this function is not used on ARM64.
 */
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
#endif /* !_M_ARM64 */

/*
 * XHCI_DisablePciIntx - Set the Interrupt Disable bit in PCI Command register
 *
 * Per PCI 3.0 spec section 6.8.1, when MSI or MSI-X is enabled, the device
 * must not assert INTx. The OS must set bit 10 (Interrupt Disable) of the
 * PCI Command register to prevent the device from generating legacy INTx
 * interrupts on the PCI bus.
 *
 * This is critical because some emulators (e.g., QEMU xhci-pci) may assert
 * BOTH MSI and INTx simultaneously. If the ISR is only registered on the
 * MSI vector, the INTx goes to an unhandled vector causing an interrupt storm.
 *
 * PCI Command register bit 10 = Interrupt Disable:
 *   0 = INTx assertion enabled (default)
 *   1 = INTx assertion disabled
 */
static VOID
XHCI_DisablePciIntx(
    _Inout_ PXHCI_EXTENSION Extension)
{
    USHORT Command;

    if (!Extension || !XhciRegPacket.UsbPortReadWriteConfigSpace)
        return;

    if (!XHCI_ReadPciConfig(Extension,
                            PCI_COMMAND_OFFSET,
                            &Command,
                            sizeof(Command)))
    {
        DPRINT1("usbxhci: DisablePciIntx: failed to read PCI command register\n");
        return;
    }

    if (Command & 0x0400)
    {
        DPRINT("usbxhci: PCI INTx already disabled (cmd=0x%04x)\n", Command);
        return;
    }

    Command |= 0x0400; /* Interrupt Disable (bit 10) */

    if (XhciRegPacket.UsbPortReadWriteConfigSpace(Extension,
                                                   FALSE,
                                                   &Command,
                                                   PCI_COMMAND_OFFSET,
                                                   sizeof(Command)) != MP_STATUS_SUCCESS)
    {
        DPRINT1("usbxhci: DisablePciIntx: failed to write PCI command register\n");
        return;
    }

    DPRINT1("usbxhci: disabled PCI INTx (cmd=0x%04x)\n", Command);
}

static
VOID
XHCI_ProgramInterrupterState(
    _Inout_ PXHCI_EXTENSION Extension)
{
    PXHCI_INTERRUPTER_REGISTER_SET Interrupter;
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
        XHCI_WRITE_REGISTER_ULONG(&Interrupter->Imod, XHCI_IMOD_DEFAULT);

        /* Program ERST and ERDP for this interrupter; all share the same ring. */
        XHCI_WRITE_REGISTER_ULONG(&Interrupter->ErstSize, Extension->ErstEntryCount);
        XHCI_WRITE_REGISTER_ULONG(&Interrupter->ErstBaseLow,
                             (ULONG)(Extension->ErstTablePhysical.QuadPart & 0xFFFFFFFF));
        XHCI_WRITE_REGISTER_ULONG(&Interrupter->ErstBaseHigh,
                             (ULONG)(Extension->ErstTablePhysical.QuadPart >> 32));
        /* Program ERDP to the event ring base and set EHB (BUSY) to clear state */
        Extension->EventRingDequeuePointer = Extension->EventRingPhysical.QuadPart;
        XHCI_WRITE_REGISTER_ULONG(&Interrupter->ErdpHigh,
                             (ULONG)(Extension->EventRingDequeuePointer >> 32));
        XHCI_WRITE_REGISTER_ULONG(&Interrupter->ErdpLow,
                             ((ULONG)(Extension->EventRingDequeuePointer & 0xFFFFFFFF)) |
                             XHCI_ERDP_BUSY);

        /* Enable interrupter: set IE and clear any pending IP (RW1C) */
        XHCI_WRITE_REGISTER_ULONG(&Interrupter->Iman, XHCI_IMAN_IE | XHCI_IMAN_IP);

        DPRINT("usbxhci: intr%lu IMOD=%08lx ERST=%08lx:%08lx ERDP=%08lx:%08lx IMAN=%08lx\n",
                Index,
                XHCI_READ_REGISTER_ULONG(&Interrupter->Imod),
                XHCI_READ_REGISTER_ULONG(&Interrupter->ErstBaseHigh),
                XHCI_READ_REGISTER_ULONG(&Interrupter->ErstBaseLow),
                XHCI_READ_REGISTER_ULONG(&Interrupter->ErdpHigh),
                XHCI_READ_REGISTER_ULONG(&Interrupter->ErdpLow),
                XHCI_READ_REGISTER_ULONG(&Interrupter->Iman));
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

    /* Log ALL commands to track mysterious queued commands */
    /* Trace-level logging for command queueing - converted from DPRINT1 to reduce hot path noise */
    DPRINT("usbxhci: QUEUE_CMD type=%lu cmd_enq=%lu cmdptr=%I64x param=%I64x ctx=%I64x\n",
            TrbType,
            Extension->CommandRingEnqueueIndex,
            CommandPointer,
            Parameter,
            Context);

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

/**
 * @brief Send a command to the xHCI controller and wait for completion.
 *
 * IRQL: Can be called at PASSIVE_LEVEL through DISPATCH_LEVEL.
 *
 * At PASSIVE_LEVEL/APC_LEVEL: Uses event-based waiting with bounded timeout.
 * At DISPATCH_LEVEL: Uses bounded busy-polling with KeStallExecutionProcessor.
 *
 * Polling bounds at DISPATCH_LEVEL:
 * - Most commands: 5ms max (100 iterations * 50us)
 * - RESET_EP/SET_DEQ: 50ms max (1000 iterations * 50us) for stall recovery
 *
 * @param Extension Controller extension
 * @param TrbType Type of command TRB
 * @param Parameter Command-specific parameter
 * @param Context Command-specific context
 * @param ControlFlags Additional control flags
 * @param TimeoutMs Timeout in milliseconds (capped at DISPATCH_LEVEL)
 * @param AllowRetry If TRUE, retry once on timeout (PASSIVE only)
 * @param SlotIdOut Optional output for returned slot ID
 * @param CompletionCodeOut Optional output for completion code
 *
 * @return MP_STATUS_SUCCESS on success, error code otherwise
 */
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
     * IRQL BEHAVIOR DOCUMENTATION:
     *
     * This helper may be called at DISPATCH_LEVEL (for example from
     * AbortTransfer / SetEndpointStatus paths).  It relies on
     * XHCI_WaitForCommandCompletion, which busy-polls using
     * KeStallExecutionProcessor instead of waiting on kernel
     * synchronization primitives, so it does not block callers at
     * elevated IRQL.
     *
     * The polling is bounded to prevent excessive DPC time:
     * - Standard commands: max 5ms at DISPATCH_LEVEL
     * - RESET_EP/SET_DEQ: max 50ms (hardware may need 10-25ms)
     * - Total iterations = timeout_ms * 1000 / XHCI_COMMAND_POLL_INTERVAL_US
     */
    if (!Extension)
        return MP_STATUS_ERROR;
    if (Extension->FatalError || Extension->StoppingOrRemoved)
        return MP_STATUS_HW_ERROR;

    /* IRQL check: We can handle PASSIVE through DISPATCH_LEVEL, not higher */
    ASSERT(CurrentIrql <= DISPATCH_LEVEL);

    if (CurrentIrql > PASSIVE_LEVEL)
    {
        /*
         * DISPATCH_LEVEL BOUNDED POLLING:
         * Use polling with KeStallExecutionProcessor.
         * Keep timeouts short for most commands to avoid excessive DPC time.
         * However, RESET_EP and SET_DEQ commands for EP0 stall recovery need
         * more time on real hardware (observed 10-25ms on Intel Alder Lake).
         * Allow up to 50ms for these critical reset commands.
         *
         * Bounds analysis:
         * - XHCI_COMMAND_POLL_INTERVAL_US = 50us
         * - 5ms timeout = 100 iterations max
         * - 50ms timeout = 1000 iterations max
         */
        if (TrbType == XHCI_TRB_TYPE_RESET_EP || TrbType == XHCI_TRB_TYPE_SET_DEQ)
        {
            if (EffectiveTimeout > 50)
                EffectiveTimeout = 50;
        }
        else
        {
            if (EffectiveTimeout > 5)
                EffectiveTimeout = 5;
        }
        RetryCommands = FALSE;
    }

    if (TrbType == XHCI_TRB_TYPE_ENABLE_SLOT ||
        TrbType == XHCI_TRB_TYPE_ADDRESS_DEV ||
        TrbType == XHCI_TRB_TYPE_CONFIG_EP)
    {
        DPRINT("usbxhci: SendCommand type=%lu timeout=%lu ms retry=%u IRQL=%lu\n",
               TrbType,
               EffectiveTimeout,
               RetryCommands ? 1u : 0u,
               (ULONG)CurrentIrql);
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
            TrbType == XHCI_TRB_TYPE_ADDRESS_DEV ||
            TrbType == XHCI_TRB_TYPE_CONFIG_EP)
        {
            XHCI_TraceCommandRingState(Extension,
                                       "SendCommand queued",
                                       CommandContext.CommandPointer,
                                       TrbType);
            if (TrbType == XHCI_TRB_TYPE_ENABLE_SLOT)
            {
                XHCI_LogInterrupterState(Extension, "EnableSlot queued");
            }
            if (TrbType == XHCI_TRB_TYPE_CONFIG_EP)
            {
                DPRINT("usbxhci: CONFIG_EP queued, waiting for completion...\n");
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

        if (!RetryCommands || Status != MP_STATUS_HW_ERROR || Extension->StoppingOrRemoved)
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

/**
 * @brief Wait for a command to complete with bounded timeout.
 *
 * IRQL: Can be called at PASSIVE_LEVEL through DISPATCH_LEVEL.
 *
 * At PASSIVE/APC_LEVEL: Uses KeWaitForSingleObject with interval timeout.
 * At DISPATCH_LEVEL: Uses bounded busy-polling with KeStallExecutionProcessor.
 *
 * The polling loop is strictly bounded:
 * - Max iterations = (TimeoutMs * 1000) / XHCI_COMMAND_POLL_INTERVAL_US
 * - At 50us intervals: 5ms = 100 iterations, 50ms = 1000 iterations
 *
 * @param Extension Controller extension
 * @param TimeoutMs Timeout in milliseconds
 * @param CommandContext Command context to wait on
 * @param SlotIdOut Optional output for slot ID
 * @param CompletionCodeOut Optional output for completion code
 *
 * @return MP_STATUS_SUCCESS on completion, error on timeout/failure
 */
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

    if (Extension->FatalError || Extension->StoppingOrRemoved)
        return MP_STATUS_HW_ERROR;

    Irql = KeGetCurrentIrql();
    if (Irql <= APC_LEVEL)
    {
        UseEventWait = TRUE;
        KeInitializeEvent(&CompletionEvent, NotificationEvent, FALSE);
        CommandContext->CompletionEvent = &CompletionEvent;
        Interval.QuadPart = -(LONGLONG)XHCI_COMMAND_POLL_INTERVAL_US * 10;
    }

    /*
     * Calculate bounded iteration count for polling.
     * This ensures DISPATCH_LEVEL polling never exceeds the specified timeout.
     */
    Remaining = (TimeoutMs * 1000) / XHCI_COMMAND_POLL_INTERVAL_US;

    /* Diagnostic: log CONFIG_EP waits for tracing mass storage enumeration hangs */
    if (CommandContext->CommandType == XHCI_TRB_TYPE_CONFIG_EP)
    {
        DPRINT("usbxhci: WaitForCommandCompletion CONFIG_EP: UseEventWait=%u Remaining=%lu IRQL=%lu\n",
               UseEventWait ? 1u : 0u, Remaining, (ULONG)Irql);
    }
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

        if (Extension->StoppingOrRemoved)
        {
            Result = MP_STATUS_HW_ERROR;
            goto Exit;
        }

        if (Extension->FatalError)
        {
            Result = MP_STATUS_HW_ERROR;
            goto Exit;
        }
    }

    if (!CommandContext->Completed)
    {
        BOOLEAN PendingEvents = XHCI_EventRingHasPendingTrb(Extension);
        DPRINT1("usbxhci: command completion timed out type=%lu timeout=%lu ms UseEventWait=%u PendingEvents=%u\n",
                CommandContext->CommandType, TimeoutMs, UseEventWait ? 1u : 0u, PendingEvents ? 1u : 0u);

        /* If we timed out but there are pending events, try to drain them */
        if (PendingEvents)
        {
            DPRINT1("usbxhci: attempting to drain pending events after timeout\n");
            XHCI_ServiceEventRing(Extension, TRUE, FALSE);
            if (CommandContext->Completed)
            {
                DPRINT1("usbxhci: command completed after manual event ring drain\n");
                goto CheckCompletion;
            }
        }

        if (Irql <= PASSIVE_LEVEL)
            XHCI_HandleCommandTimeout(Extension, CommandContext);
        Result = MP_STATUS_HW_ERROR;
        goto Exit;
    }

CheckCompletion:
    if (SlotIdOut)
        *SlotIdOut = CommandContext->SlotId;
    if (CompletionCodeOut)
        *CompletionCodeOut = CommandContext->CompletionCode;

    if (CommandContext->CompletionCode == XHCI_COMPLETION_SUCCESS)
    {
        /* Diagnostic for CONFIG_EP success */
        if (CommandContext->CommandType == XHCI_TRB_TYPE_CONFIG_EP)
        {
            DPRINT("usbxhci: CONFIG_EP completed successfully slot=%u\n",
                   CommandContext->SlotId);
        }
        Result = MP_STATUS_SUCCESS;
    }
    else if (CommandContext->CompletionCode == XHCI_COMPLETION_CONTEXT_STATE_ERROR &&
             CommandContext->CommandType == XHCI_TRB_TYPE_STOP_EP)
    {
        /*
         * CONTEXT_STATE_ERROR (code 19) for Stop Endpoint means the endpoint
         * is already in a stopped/disabled state or was never running. This is
         * a benign condition during shutdown suspend when we try to stop
         * endpoints that have already been torn down. The endpoint is already
         * in the desired state, so treat this as success.
         */
        DPRINT("usbxhci: StopEndpoint got CONTEXT_STATE_ERROR - endpoint already stopped (slot=%u)\n",
               CommandContext->SlotId);
        Result = MP_STATUS_SUCCESS;
    }
    else
    {
        DPRINT1("usbxhci: command completion error type=%lu code=%lu slot=%u\n",
                CommandContext->CommandType,
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

    Command = XHCI_READ_REGISTER_ULONG(UsbCmd);
    if (Command & XHCI_USBCMD_RS)
    {
        XHCI_WRITE_REGISTER_ULONG(UsbCmd, Command & ~XHCI_USBCMD_RS);
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

    XHCI_WRITE_REGISTER_ULONG(UsbCmd, XHCI_USBCMD_HCRST);

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
        ULONG DebugCmd = XHCI_READ_REGISTER_ULONG(UsbCmd);
        ULONG DebugSts = XHCI_READ_REGISTER_ULONG(UsbSts);

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
    XHCI_WRITE_REGISTER_ULONG(UsbSts,
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

    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
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

/*
 * Registry parameter names for quirk overrides.
 * Each can be set to DWORD 1 to force-enable or 0 to force-disable the quirk.
 * If not present, automatic hardware detection is used.
 */
static const WCHAR XHCI_REG_TRACE_MASK[] = L"XhciTraceMask";
static const WCHAR XHCI_REG_STARTUP_HCE_QUIRK[] = L"XhciStartupHceQuirk";
static const WCHAR XHCI_REG_NON_COHERENT_DMA[] = L"XhciNonCoherentDma";
static const WCHAR XHCI_REG_FORCE_32BIT_DMA[] = L"XhciForce32BitDma";
static const WCHAR XHCI_REG_VBOX_QUIRKS[] = L"XhciVBoxQuirks";
static const WCHAR XHCI_REG_QEMU_QUIRKS[] = L"XhciQemuQuirks";
static const WCHAR XHCI_REG_LIMIT_U1U2[] = L"XhciLimitU1U2";

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

    ParameterValue = 0;
    MpStatus = XhciRegPacket.UsbPortGetMiniportRegistryKeyValue(
        Extension,
        TRUE,
        XHCI_REG_NON_COHERENT_DMA,
        sizeof(XHCI_REG_NON_COHERENT_DMA),
        &ParameterValue,
        sizeof(ParameterValue));

    if (MpStatus == MP_STATUS_SUCCESS)
    {
        g_XhciNonCoherentDmaOverrideValid = TRUE;
        g_XhciNonCoherentDmaOverride = (ParameterValue != 0);
        DPRINT("usbxhci: non-coherent DMA override %s via registry (value=%lu)\n",
                g_XhciNonCoherentDmaOverride ? "ENABLED" : "DISABLED",
                ParameterValue);
    }

    /* Force 32-bit DMA quirk */
    ParameterValue = 0;
    MpStatus = XhciRegPacket.UsbPortGetMiniportRegistryKeyValue(
        Extension,
        TRUE,
        XHCI_REG_FORCE_32BIT_DMA,
        sizeof(XHCI_REG_FORCE_32BIT_DMA),
        &ParameterValue,
        sizeof(ParameterValue));

    if (MpStatus == MP_STATUS_SUCCESS)
    {
        g_XhciForce32BitDmaOverrideValid = TRUE;
        g_XhciForce32BitDmaOverride = (ParameterValue != 0);
        DPRINT("usbxhci: Force32BitDma override %s via registry\n",
                g_XhciForce32BitDmaOverride ? "ENABLED" : "DISABLED");
    }

    /* VirtualBox quirks */
    ParameterValue = 0;
    MpStatus = XhciRegPacket.UsbPortGetMiniportRegistryKeyValue(
        Extension,
        TRUE,
        XHCI_REG_VBOX_QUIRKS,
        sizeof(XHCI_REG_VBOX_QUIRKS),
        &ParameterValue,
        sizeof(ParameterValue));

    if (MpStatus == MP_STATUS_SUCCESS)
    {
        g_XhciVBoxQuirksOverrideValid = TRUE;
        g_XhciVBoxQuirksOverride = (ParameterValue != 0);
        DPRINT("usbxhci: VBox quirks override %s via registry\n",
                g_XhciVBoxQuirksOverride ? "ENABLED" : "DISABLED");
    }

    /* QEMU quirks */
    ParameterValue = 0;
    MpStatus = XhciRegPacket.UsbPortGetMiniportRegistryKeyValue(
        Extension,
        TRUE,
        XHCI_REG_QEMU_QUIRKS,
        sizeof(XHCI_REG_QEMU_QUIRKS),
        &ParameterValue,
        sizeof(ParameterValue));

    if (MpStatus == MP_STATUS_SUCCESS)
    {
        g_XhciQemuQuirksOverrideValid = TRUE;
        g_XhciQemuQuirksOverride = (ParameterValue != 0);
        DPRINT("usbxhci: QEMU quirks override %s via registry\n",
                g_XhciQemuQuirksOverride ? "ENABLED" : "DISABLED");
    }

    /* U1/U2 LPM limiting quirk */
    ParameterValue = 0;
    MpStatus = XhciRegPacket.UsbPortGetMiniportRegistryKeyValue(
        Extension,
        TRUE,
        XHCI_REG_LIMIT_U1U2,
        sizeof(XHCI_REG_LIMIT_U1U2),
        &ParameterValue,
        sizeof(ParameterValue));

    if (MpStatus == MP_STATUS_SUCCESS)
    {
        g_XhciLimitU1U2OverrideValid = TRUE;
        g_XhciLimitU1U2Override = (ParameterValue != 0);
        DPRINT("usbxhci: LimitU1U2 override %s via registry\n",
                g_XhciLimitU1U2Override ? "ENABLED" : "DISABLED");
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
    static BOOLEAN Triggered = FALSE;
    if (Extension && Extension->RhIrqEnabled && !Triggered && XhciRegPacket.UsbPortInvalidateRootHub)
    {
        Triggered = TRUE;
        DPRINT("XHCI: Triggering RH Invalidate from SubmitTransfer\n");
        XhciRegPacket.UsbPortInvalidateRootHub(Extension);
    }


    if (!Extension || !Endpoint || !Transfer || !TransferParameters)
        return MP_STATUS_ERROR;

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
    if (Endpoint->ReservedStreamId != 0 &&
        Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK)
    {
        ULONG StreamId = TransferParameters->Reserved2;
        if (StreamId != 0 && StreamId <= Endpoint->ReservedStreamId)
            Transfer->StreamId = (USHORT)StreamId;
    }
    Transfer->RequestedLength = TransferParameters->TransferBufferLength;
    Transfer->UsbdStatus = USBD_STATUS_SUCCESS;
    Transfer->Flags = 0;
    Transfer->NewAddress = 0;
    Transfer->IsIsochronous = FALSE;
    Transfer->BounceSlot = -1;

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
    BOOLEAN UseBounce = FALSE;
    BOOLEAN ForceBounce = FALSE;
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
    ULONGLONG HighAddress = 0;
    BOOLEAN TraceBos = FALSE;
    BOOLEAN TraceDevDesc = FALSE;
    ULONG BosBytesProgrammed = 0;
    ULONG DevDescBytesProgrammed = 0;
    PXHCI_TRB SetupTrbDeferred = NULL;
    ULONG SetupCycleBitDeferred = 0;

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

    /*
     * After RESET_DEVICE, the slot may be in "not addressed" state. Transfers
     * using cached endpoint handles will bypass OpenEndpoint and arrive here
     * directly. We must re-address the device before any transfer can succeed.
     * This can only be done at PASSIVE_LEVEL.
     *
     * Note: For VirtualBox (XHCI_QUIRK_VBOX_POLL_XFERS), we don't clear the
     * Addressed flag on RESET_DEVICE since VBox doesn't properly implement it.
     */
    if (Endpoint->DefaultControl &&
        Endpoint->Slot &&
        !Endpoint->Slot->Addressed &&
        KeGetCurrentIrql() <= PASSIVE_LEVEL)
    {
        MPSTATUS AddrStatus;
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: slot %u not addressed, re-addressing before transfer\n",
                 Endpoint->SlotId);

        AddrStatus = XHCI_AddressDeviceSlot(Extension,
                                             Endpoint->Slot,
                                             &Endpoint->EndpointProperties,
                                             FALSE);
        if (AddrStatus != MP_STATUS_SUCCESS)
        {
            DPRINT1("usbxhci: slot %u re-address failed %d\n", Endpoint->SlotId, AddrStatus);
            return AddrStatus;
        }
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: slot %u re-addressed successfully\n",
                 Endpoint->SlotId);
        /* After re-address, the ring is reset to index 0 with cycle 1 */
        Endpoint->TransferRing.EnqueueIndex = 0;
        Endpoint->TransferRing.DequeueIndex = 0;
        Endpoint->TransferRing.CycleState = 1;
        if (Endpoint->Slot->Ep0NeedsDequeueReset)
            Endpoint->Slot->Ep0NeedsDequeueReset = FALSE;
    }
    else if (Endpoint->DefaultControl &&
             Endpoint->Slot &&
             !Endpoint->Slot->Addressed &&
             KeGetCurrentIrql() > PASSIVE_LEVEL)
    {
        /*
         * At DISPATCH_LEVEL with unaddressed slot. After RESET_DEVICE, the
         * slot is in Default state and EP0 should still work - the device
         * responds at USB address 0. The ring was already reset by the
         * RESET_DEVICE completion handler. Just log and proceed with the
         * transfer - re-addressing will happen later at PASSIVE_LEVEL.
         */
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: slot %u not addressed at DISPATCH_LEVEL - proceeding with default address\n",
                 Endpoint->SlotId);
    }

    /*
     * VirtualBox quirk: If EP0 needs a dequeue reset after RESET_DEVICE,
     * issue SET_TR_DEQUEUE_POINTER to synchronize the hardware with software.
     * The xHCI spec requires stopping the endpoint before SET_TR_DEQUEUE_POINTER.
     *
     * IMPORTANT: Commands can only be issued at PASSIVE_LEVEL. If we're at
     * DISPATCH_LEVEL (IRQL=2), we must defer the reset. However, hidusb.sys
     * expects synchronous transfer completion, so we need an alternative
     * approach for DISPATCH_LEVEL:
     *
     * VirtualBox has a bug where RESET_DEVICE doesn't properly reset the
     * TR Dequeue Pointer. At DISPATCH_LEVEL, we cannot issue commands, but
     * we CAN synchronize the software ring state to match what the hardware
     * thinks. By reading the hardware dequeue pointer and adjusting our
     * software enqueue pointer to match, we can continue without commands.
     */
    if (Endpoint->DefaultControl &&
        Endpoint->Slot &&
        Endpoint->Slot->Ep0NeedsDequeueReset)
    {
        if (KeGetCurrentIrql() <= PASSIVE_LEVEL)
        {
            /* At PASSIVE_LEVEL - can issue commands to properly reset */
            MPSTATUS DeqStatus;
            XHCI_DBG(XHCI_TRACE_TRANSFERS,
                     "usbxhci: slot %u EP0 dequeue reset (VBox quirk) at PASSIVE\n",
                     Endpoint->SlotId);

            DeqStatus = XHCI_StopEndpoint(Extension, Endpoint->Slot, 1);
            if (DeqStatus == MP_STATUS_SUCCESS)
            {
                DeqStatus = XHCI_SetEndpointDequeue(Extension,
                                                     Endpoint->Slot,
                                                     1,
                                                     &Endpoint->TransferRing);
                if (DeqStatus == MP_STATUS_SUCCESS)
                {
                    XHCI_DBG(XHCI_TRACE_TRANSFERS,
                             "usbxhci: slot %u EP0 dequeue reset complete\n",
                             Endpoint->SlotId);
                }
                else
                {
                    DPRINT1("usbxhci: slot %u SET_TR_DEQUEUE_POINTER failed %d\n",
                            Endpoint->SlotId, DeqStatus);
                }
            }
            else
            {
                DPRINT1("usbxhci: slot %u STOP_ENDPOINT for dequeue reset failed %d\n",
                        Endpoint->SlotId, DeqStatus);
            }
            Endpoint->Slot->Ep0NeedsDequeueReset = FALSE;
        }
        else
        {
            /*
             * At DISPATCH_LEVEL - cannot issue commands. VirtualBox didn't
             * reset the hardware dequeue pointer after RESET_DEVICE, so we
             * need to sync our software state to the hardware's stale pointer.
             * Read the hardware endpoint context and match our ring state.
             */
            PXHCI_ENDPOINT_CONTEXT EpCtx = XHCI_GetDeviceEndpointContextVa(
                Extension,
                Endpoint->Slot->DeviceContext.VirtualAddress,
                0);
            if (EpCtx)
            {
                ULONGLONG HwDequeue = EpCtx->TrDequeuePointer;
                ULONGLONG HwDeqAddr = HwDequeue & ~0xFULL;
                ULONG HwDeqDcs = (ULONG)(HwDequeue & 1);
                ULONGLONG RingBase = Endpoint->TransferRing.PhysicalAddress.QuadPart;
                ULONGLONG Offset;

                if (HwDeqAddr >= RingBase &&
                    HwDeqAddr < RingBase + Endpoint->TransferRing.Length)
                {
                    Offset = HwDeqAddr - RingBase;
                    Endpoint->TransferRing.EnqueueIndex = (ULONG)(Offset / sizeof(XHCI_TRB));
                    Endpoint->TransferRing.DequeueIndex = Endpoint->TransferRing.EnqueueIndex;
                    Endpoint->TransferRing.CycleState = HwDeqDcs;

                    /* Also update the slot's saved state */
                    Endpoint->Slot->Ep0RingEnqueueIndex = Endpoint->TransferRing.EnqueueIndex;
                    Endpoint->Slot->Ep0RingDequeueIndex = Endpoint->TransferRing.DequeueIndex;
                    Endpoint->Slot->Ep0RingCycleState = Endpoint->TransferRing.CycleState;

                    XHCI_DBG(XHCI_TRACE_TRANSFERS,
                             "usbxhci: slot %u EP0 sync to HW dequeue at DISPATCH: EnqIdx=%lu Cycle=%lu\n",
                             Endpoint->SlotId,
                             Endpoint->TransferRing.EnqueueIndex,
                             Endpoint->TransferRing.CycleState);
                }
                else
                {
                    DPRINT1("usbxhci: slot %u EP0 HW dequeue %I64x out of range (base=%I64x)\n",
                            Endpoint->SlotId, HwDeqAddr, RingBase);
                }
            }
            Endpoint->Slot->Ep0NeedsDequeueReset = FALSE;
        }
    }

    /*
     * Check if the endpoint is halted. After a stall error (e.g., HID SetIdle
     * rejection), the xHCI endpoint enters Halted state and won't process
     * transfers until reset.
     *
     * The stall completion handler queues an async reset. We need to wait for
     * it to complete before submitting new transfers. The endpoint transitions:
     * Halted -> (Reset Endpoint cmd) -> Stopped -> (SetTRDequeue) -> Stopped
     * -> (doorbell) -> Running.
     *
     * If endpoint is still Halted, either wait for reset or do it synchronously.
     * If endpoint is Stopped (reset in progress), wait briefly for SetTRDequeue.
     */
    if (Endpoint->DefaultControl &&
        Endpoint->Slot &&
        !Endpoint->Slot->Ep0NeedsStallReset &&
        KeGetCurrentIrql() <= PASSIVE_LEVEL)
    {
        ULONG EpState = XHCI_EPCTX_STATE_DISABLED;
        PVOID DevCtx = Endpoint->Slot->DeviceContext.VirtualAddress;

        if (DevCtx)
        {
            PXHCI_ENDPOINT_CONTEXT EpCtx = XHCI_GetDeviceEndpointContextVa(
                Extension,
                DevCtx,
                0);
            if (EpCtx)
                EpState = EpCtx->EpInfo & XHCI_EPCTX_STATE_MASK;
        }

        if (EpState == XHCI_EPCTX_STATE_HALTED)
        {
            DPRINT1("usbxhci: slot %u EP0 halted before submit, resetting\n",
                    Endpoint->SlotId);
            XHCI_PerformEndpointResetSequence(Extension, Endpoint, FALSE);
        }
    }

    if (Endpoint->DefaultControl && Endpoint->Slot)
    {
        /*
         * A previous control transfer stalled. The completion handler now does
         * inline reset, so Ep0NeedsStallReset should normally be FALSE here.
         * However, as a safety net (e.g., if inline reset was bypassed or
         * failed), we check and perform reset if needed:
         * - At PASSIVE_LEVEL: wait for any pending async worker
         * - At DISPATCH_LEVEL: do inline reset using polling
         */
        if (Endpoint->Slot->Ep0NeedsStallReset)
        {
            if (KeGetCurrentIrql() > PASSIVE_LEVEL)
            {
                /*
                 * At DISPATCH_LEVEL, we cannot wait for the async worker's event.
                 * Instead, perform the reset inline using polling-based command waits.
                 * This is the same approach used for VirtualBox and ensures the
                 * endpoint is ready before we return to USBPORT.
                 *
                 * If we returned failure here, USBPORT would fail the transfer and
                 * potentially abort device enumeration, causing the system to stall
                 * waiting for a device that will never finish enumerating.
                 *
                 * The inline reset uses KeStallExecutionProcessor for command waits,
                 * which is acceptable at DISPATCH_LEVEL for short durations. Modern
                 * xHCI controllers complete RESET_EP and SET_TR_DEQUEUE quickly.
                 */
                XHCI_DBG(XHCI_TRACE_TRANSFERS,
                         "usbxhci: slot %u EP0 stall reset needed at DISPATCH, doing inline reset\n",
                         Endpoint->SlotId);

                XHCI_PerformEndpointResetSequence(Extension, Endpoint, FALSE);
                InterlockedExchange(&Endpoint->Slot->Ep0NeedsStallReset, 0);
                InterlockedExchange(&Endpoint->Slot->Ep0StallResetQueued, 0);
                KeSetEvent(&Endpoint->Slot->Ep0StallResetEvent,
                           IO_NO_INCREMENT,
                           FALSE);
            }
            else
            {
                /* At PASSIVE_LEVEL, we can wait synchronously for the fix to complete */
                Status = XHCI_WaitForEp0StallReset(Extension, Endpoint);
                if (Status != MP_STATUS_SUCCESS)
                    return Status;
            }
        }
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
    Transfer->TdFirstTrbPointer = 0;
    Transfer->CompletionTrbPointer = 0;
    HasDataStage = (TransferParameters->TransferBufferLength != 0);
    DataIn = (TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN) ? TRUE : FALSE;
    Transfer->Flags = 0;
    Transfer->NewAddress = 0;
    Transfer->IsControl = TRUE;
    Transfer->TdFirstTrbPointer = 0;
    Transfer->CompletionTrbPointer = 0;

#if !defined(_WIN64)
    if (Endpoint->DefaultControl &&
        (Extension->Quirks & XHCI_QUIRK_IGNORE_STARTUP_HCE))
    {
        ForceBounce = TRUE;
    }
#endif

    if (TransferParameters->SetupPacket.bmRequestType.B == 0 &&
        TransferParameters->SetupPacket.bRequest == USB_REQUEST_SET_ADDRESS)
    {
        Transfer->Flags |= XHCI_TRANSFER_FLAG_SET_ADDRESS;
        Transfer->NewAddress = (UCHAR)(TransferParameters->SetupPacket.wValue.W);
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: SUBMIT SET_ADDRESS addr=%u port=%u hub=%u speed=%u len=%lu Slot=%p\n",
                 Transfer->NewAddress,
                 Endpoint->EndpointProperties.PortNumber,
                 Endpoint->EndpointProperties.HubAddr,
                 Endpoint->EndpointProperties.DeviceSpeed,
                 TransferParameters->TransferBufferLength,
                 Endpoint->Slot);

        /*
         * xHCI handles device addressing internally via ADDRESS_DEVICE command.
         * USB SET_ADDRESS packets are NOT sent on the wire. When USBPORT sends
         * SET_ADDRESS, we must intercept it and handle via the command ring.
         *
         * After RESET_DEV, the slot returns to Default state (Addressed=FALSE).
         * We must issue ADDRESS_DEVICE to transition to Addressed state before
         * any control transfers can complete. For an already-addressed slot,
         * we just update the address maps.
         *
         * This is a non-virtual port, so we handle it by issuing ADDRESS_DEVICE
         * if needed, updating address maps, and completing the transfer without
         * queuing USB TRBs to the transfer ring.
         */
        if (Endpoint->Slot)
        {
            PXHCI_SWENUM_WORK Work;

            XHCI_DBG(XHCI_TRACE_TRANSFERS,
                     "usbxhci: SET_ADDRESS handling slot=%u Addressed=%d\n",
                     Endpoint->Slot->SlotId,
                     Endpoint->Slot->Addressed);

            /*
             * Try to acquire a reference for the SW-enum work. If the endpoint
             * is closing, this will fail and we should not queue the work.
             */
            if (!XHCI_ReferenceEndpointForSwEnum(Endpoint))
            {
                KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
                Endpoint->ActiveTransfer = NULL;
                KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
                Transfer->UsbdStatus = USBD_STATUS_CANCELED;
                return MP_STATUS_FAILURE;
            }

            Work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Work), XHCI_TAG);
            if (!Work)
            {
                KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
                Endpoint->ActiveTransfer = NULL;
                KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
                XHCI_DereferenceEndpointForSwEnum(Endpoint);
                return MP_STATUS_NO_RESOURCES;
            }

            RtlZeroMemory(Work, sizeof(*Work));
            InterlockedAnd((volatile LONG *)&Transfer->Flags,
                           ~(XHCI_TRANSFER_FLAG_SWENUM_DONE |
                             XHCI_TRANSFER_FLAG_SWENUM_CANCELED));
            InterlockedOr((volatile LONG *)&Transfer->Flags,
                          XHCI_TRANSFER_FLAG_SWENUM_PENDING);
            Work->Extension = Extension;
            Work->Transfer = Transfer;
            Work->Endpoint = Endpoint;  /* Store endpoint for which reference was acquired */
            Work->EndpointProperties = Endpoint->EndpointProperties;
            Work->SlotId = Endpoint->Slot ? Endpoint->Slot->SlotId : 0;
            Work->NewAddress = Transfer->NewAddress;
            /* Reference already acquired above */

            /*
             * If the slot is not addressed (post RESET_DEV), we must issue
             * ADDRESS_DEVICE. However, SubmitTransfer runs at elevated IRQL
             * (DISPATCH_LEVEL), which causes XHCI_SendCommand to use a very
             * short timeout (5ms) that's insufficient for ADDRESS_DEVICE.
             *
             * Defer ADDRESS_DEVICE to the work item where it runs at PASSIVE_LEVEL
             * with proper timeout (100ms) and retry support.
             */
            if (!Endpoint->Slot->Addressed)
            {
                Work->NeedsAddressDevice = TRUE;
                XHCI_DBG(XHCI_TRACE_TRANSFERS,
                         "usbxhci: deferring ADDRESS_DEVICE to PASSIVE_LEVEL for slot %u\n",
                         Endpoint->Slot->SlotId);
            }
            else
            {
                /* Already addressed - just update address maps immediately */
                Endpoint->EndpointProperties.DeviceAddress = Transfer->NewAddress;
                Endpoint->Slot->UsbDeviceAddress = Transfer->NewAddress;
                XHCI_UpdateDeviceAddressMap(Extension, Endpoint->Slot, Transfer->NewAddress);

                XHCI_DBG(XHCI_TRACE_TRANSFERS,
                         "usbxhci: slot %u SET_ADDRESS to addr=%u (already addressed)\n",
                         Endpoint->Slot->SlotId,
                         Transfer->NewAddress);

                Transfer->BytesTransferred = 0;
                Transfer->UsbdStatus = USBD_STATUS_SUCCESS;
            }

            /* Queue async completion - USBPORT expects completion from DPC path */
            InterlockedIncrement(&Extension->SwEnumWorkerCount);
            ExInitializeWorkItem(&Work->Item, XHCI_SwEnumWorker, Work);
            ExQueueWorkItem(&Work->Item, DelayedWorkQueue);

            return MP_STATUS_SUCCESS;
        }
    }

    if (TransferParameters->SetupPacket.bRequest == USB_REQUEST_GET_DESCRIPTOR)
    {
        Transfer->Flags |= XHCI_TRANSFER_FLAG_GET_DESCRIPTOR;
    }

    if (TransferParameters &&
        TransferParameters->SetupPacket.bRequest == USB_REQUEST_GET_DESCRIPTOR)
    {
        UCHAR DescType = TransferParameters->SetupPacket.wValue.HiByte;

        if (DescType == USB_BOS_DESCRIPTOR_TYPE)
        {
            TraceBos = TRUE;
            XHCI_DBG(XHCI_TRACE_TRANSFERS,
                     "usbxhci: BOS submit len=%lu wLength=%u sgcount=%lu buf=%p\n",
                     TransferParameters->TransferBufferLength,
                     TransferParameters->SetupPacket.wLength,
                     SgList ? SgList->SgElementCount : 0,
                     SgList ? SgList->MappedSystemVa : NULL);
        }
        else if (DescType == USB_DEVICE_DESCRIPTOR_TYPE)
        {
            TraceDevDesc = TRUE;
            XHCI_DBG(XHCI_TRACE_TRANSFERS,
                     "usbxhci: DEV submit len=%lu wLength=%u sgcount=%lu buf=%p\n",
                     TransferParameters->TransferBufferLength,
                     TransferParameters->SetupPacket.wLength,
                     SgList ? SgList->SgElementCount : 0,
                     SgList ? SgList->MappedSystemVa : NULL);

            /*
             * Diagnostic: Check the xHC's endpoint context to verify the
             * TR Dequeue Pointer and endpoint state before submitting.
             * Also detect and attempt recovery from ring pointer mismatch.
             */
            if (Endpoint->Slot && Endpoint->Slot->DeviceContext.VirtualAddress)
            {
                PXHCI_ENDPOINT_CONTEXT EpCtxDiag =
                    XHCI_GetDeviceEndpointContextVa(Extension,
                                                     Endpoint->Slot->DeviceContext.VirtualAddress,
                                                     0);
                if (EpCtxDiag)
                {
                    ULONG EpState = EpCtxDiag->EpInfo & XHCI_EPCTX_STATE_MASK;
                    ULONGLONG HwDequeue = EpCtxDiag->TrDequeuePointer;
                    ULONGLONG HwDeqAddr = HwDequeue & ~0xFULL;
                    ULONG HwDeqDcs = (ULONG)(HwDequeue & 1);
                    ULONGLONG SwEnqueuePhys = Endpoint->TransferRing.PhysicalAddress.QuadPart +
                        ((ULONGLONG)Endpoint->TransferRing.EnqueueIndex * sizeof(XHCI_TRB));

                    XHCI_DBG(XHCI_TRACE_TRANSFERS,
                             "usbxhci: DEV pre-submit EpState=%lu HwDeqAddr=%I64x HwDcs=%lu SwEnqPhys=%I64x EnqIdx=%lu SwCycle=%lu\n",
                             EpState,
                             HwDeqAddr,
                             HwDeqDcs,
                             SwEnqueuePhys,
                             Endpoint->TransferRing.EnqueueIndex,
                             Endpoint->TransferRing.CycleState);

                    /*
                     * NOTE: Per xHCI spec section 6.2.3, the TR Dequeue Pointer in the
                     * endpoint context is ONLY valid when the endpoint is in Stopped or
                     * Error state. When the endpoint is in Running state (EpState=1),
                     * the TR Dequeue Pointer is STALE - it contains the value from when
                     * the endpoint was last stopped (e.g., initial configuration).
                     *
                     * The software's EnqueueIndex is authoritative for Running endpoints.
                     * Do NOT attempt to "recover" based on the stale hardware pointer,
                     * as this would corrupt the ring state and cause transfers to fail.
                     *
                     * A mismatch between HwDeqAddr and SwEnqPhys when EpState=1 is
                     * EXPECTED and NORMAL - it simply means transfers have completed
                     * since the endpoint was configured.
                     */
                    if (HwDeqAddr != SwEnqueuePhys && EpState == 1)
                    {
                        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                                 "usbxhci: DEV note: HwDeq != SwEnq (expected for Running EP, HW ptr is stale)\n");
                    }
                }
            }
        }
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

    if (HasDataStage &&
        (ForceBounce ||
         (XHCI_Requires32BitDma(Extension) &&
          XHCI_SgListHasHighAddress(SgList, &HighAddress))))
    {
        if (ForceBounce)
        {
            DPRINT1("usbxhci: forcing control bounce buffer for EP0 (len=%lu)\n",
                    TransferParameters->TransferBufferLength);
        }
        else
        {
            DPRINT1("usbxhci: control DMA above 4G (pa=%I64x len=%lu), using bounce buffer\n",
                    HighAddress,
                    TransferParameters->TransferBufferLength);
        }
        Status = XHCI_PrepareBounceBuffer(Extension,
                                          Transfer,
                                          TransferParameters->TransferBufferLength,
                                          DataIn);
        if (Status != MP_STATUS_SUCCESS)
            goto Failure;

        UseBounce = (Transfer->BounceBuffer != NULL);
        if (UseBounce && !DataIn)
        {
            ULONG Copied = XHCI_CopySgListToBuffer(SgList,
                                                   Transfer->BounceBuffer,
                                                   Transfer->BounceLength);
            if (Copied < Transfer->BounceLength)
            {
                DPRINT1("usbxhci: control OUT bounce copy short (%lu/%lu)\n",
                        Copied,
                        Transfer->BounceLength);
            }
        }
    }

    /*
     * Per xHCI spec section 4.11.2.2, a Control Transfer consists of separate
     * TDs: Setup Stage TD, optional Data Stage TD, and Status Stage TD.
     * The Setup Stage is ALWAYS a single-TRB TD.  Pass TdContinues=FALSE so
     * that if the ring wraps at this position, the Link TRB does NOT get the
     * Chain bit set.  A chained Link TRB between TDs confuses some controllers
     * (Intel Alder Lake-N xHCI) and causes the transfer to hang.
     */
    Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing, &PhysicalAddress, FALSE);
    if (!Trb)
    {
        Status = MP_STATUS_NO_RESOURCES;
        goto Failure;
    }
    Transfer->TdFirstTrbPointer = PhysicalAddress;

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
    /*
     * Per xHCI spec section 4.11.2.2: "The Chain (CH) bit shall be cleared
     * to '0' in a Setup Stage TRB." The Chain bit is only valid for Data
     * Stage TRBs that need to be chained together when spanning multiple TRBs.
     */
    /*
     * Deferred cycle bit pattern: write the Setup TRB Control WITHOUT the
     * cycle bit initially. This prevents the xHCI hardware from fetching
     * the Setup TRB (and racing ahead through the Link TRB) before the
     * Data and Status TRBs are programmed. The cycle bit is set later,
     * just before ringing the doorbell, after all TRBs are ready.
     * This follows the same pattern as Linux's giveback_first_trb().
     */
    {
        PXHCI_TRB SetupTrb = Trb;
        ULONG SetupCycleBit = Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE;

        Control = (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) |
                  XHCI_TRB_IDT;

        if (!HasDataStage)
            Control |= XHCI_TRB_TRT_NO_DATA;
        else if (DataIn)
            Control |= XHCI_TRB_TRT_IN;
        else
            Control |= XHCI_TRB_TRT_OUT;

        /* Write Control without cycle bit - hardware will NOT fetch this TRB yet */
        Trb->Control = Control;
        ProgrammedRing = TRUE;

        /* Save for deferred commit (local variables used after Status TRB programming) */
        SetupTrbDeferred = SetupTrb;
        SetupCycleBitDeferred = SetupCycleBit;
    }
    XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

    Remaining = TransferParameters->TransferBufferLength;

    if (HasDataStage)
    {
        if (Transfer->BounceBuffer)
        {
            ULONG ElementRemaining = Transfer->BounceLength;
            ULONGLONG ElementAddress = Transfer->BouncePhysicalAddress.QuadPart;

            while (ElementRemaining && Remaining)
            {
                Chunk = XHCI_CalcTrbTransferChunk(ElementAddress,
                                                  ElementRemaining,
                                                  Remaining,
                                                  0);

                /*
                 * TdContinues: TRUE if more data TRBs follow in this Data
                 * Stage TD, FALSE for the last data TRB (TD boundary).
                 * This controls the Chain bit on any Link TRB at the ring
                 * wrap point, ensuring correct TD boundary signaling.
                 */
                Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing,
                                              &PhysicalAddress,
                                              ((Remaining - Chunk) != 0));
                if (!Trb)
                {
                    Status = MP_STATUS_NO_RESOURCES;
                    goto Failure;
                }

                if (TraceBos)
                    BosBytesProgrammed += Chunk;
                if (TraceDevDesc)
                    DevDescBytesProgrammed += Chunk;
                Trb->Parameter1 = (ULONG)(ElementAddress & 0xFFFFFFFF);
                Trb->Parameter2 = (ULONG)(ElementAddress >> 32);
                Trb->Status = Chunk;
                Control = (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT) |
                          (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE);

                if (DataIn)
                    Control |= XHCI_TRB_DIR_IN;

                /*
                 * Per xHCI spec 4.11.5.2: "For a Control transfer, the TRB Chain bit
                 * shall be set on all TRBs in a Control Data Stage TD except the last
                 * TRB (even though the Data Stage is followed by a Status Stage, the
                 * last Data Stage TRB's Chain bit shall be '0')."
                 *
                 * Also add IOC to the last Data Stage TRB. This ensures we receive an
                 * event with the actual Remaining count when data transfer completes,
                 * before the Status Stage event (which always has Remaining=0).
                 * Without IOC on a non-chained TRB, short packets might not generate
                 * events on all controllers.
                 */
                if ((Remaining - Chunk) == 0)
                {
                    /* Last Data Stage TRB: no CHAIN, but add IOC */
                    Control |= XHCI_TRB_IOC;
                }
                else
                {
                    /* Not the last Data Stage TRB: set CHAIN, no IOC */
                    Control |= XHCI_TRB_CHAIN_BIT;
                }

                Trb->Control = Control;
                XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

                ElementAddress += Chunk;
                ElementRemaining -= Chunk;
                Remaining -= Chunk;
            }
        }
        else
        {
            SgIndex = 0;
            while (Remaining && SgIndex < SgList->SgElementCount)
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

                    /*
                     * TdContinues: TRUE if more data TRBs follow, FALSE for
                     * last data TRB (end of Data Stage TD).
                     */
                    Trb = XHCI_GetTransferRingTrb(&Endpoint->TransferRing,
                                                  &PhysicalAddress,
                                                  ((Remaining - Chunk) != 0));
                    if (!Trb)
                    {
                        Status = MP_STATUS_NO_RESOURCES;
                        goto Failure;
                    }

                    if (TraceBos)
                        BosBytesProgrammed += Chunk;
                    if (TraceDevDesc)
                        DevDescBytesProgrammed += Chunk;
                    Trb->Parameter1 = (ULONG)(ElementAddress & 0xFFFFFFFF);
                    Trb->Parameter2 = (ULONG)(ElementAddress >> 32);
                    Trb->Status = Chunk;
                    Control = (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT) |
                              (Endpoint->TransferRing.CycleState & XHCI_TRB_CYCLE);

                    if (DataIn)
                        Control |= XHCI_TRB_DIR_IN;

                    /*
                     * Per xHCI spec 4.11.5.2: "For a Control transfer, the TRB Chain bit
                     * shall be set on all TRBs in a Control Data Stage TD except the last
                     * TRB (even though the Data Stage is followed by a Status Stage, the
                     * last Data Stage TRB's Chain bit shall be '0')."
                     *
                     * Also add IOC to the last Data Stage TRB. This ensures we receive an
                     * event with the actual Remaining count when data transfer completes,
                     * before the Status Stage event (which always has Remaining=0).
                     * Without IOC on a non-chained TRB, short packets might not generate
                     * events on all controllers.
                     */
                    if ((Remaining - Chunk) == 0)
                    {
                        /* Last Data Stage TRB: no CHAIN, but add IOC */
                        Control |= XHCI_TRB_IOC;
                    }
                    else
                    {
                        /* Not the last Data Stage TRB: set CHAIN, no IOC */
                        Control |= XHCI_TRB_CHAIN_BIT;
                    }

                    Trb->Control = Control;
                    XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

                    ElementAddress += Chunk;
                    ElementRemaining -= Chunk;
                    Remaining -= Chunk;
                }

                SgIndex++;
            }
        }
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
    if (TraceBos)
    {
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: BOS trbs first=%I64x last=%I64x data=%lu hasData=%u dir=%s\n",
                 (ULONGLONG)Transfer->TdFirstTrbPointer,
                 (ULONGLONG)Transfer->CompletionTrbPointer,
                 BosBytesProgrammed,
                 HasDataStage ? 1 : 0,
                 DataIn ? "IN" : "OUT");
    }
    if (TraceDevDesc)
    {
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: DEV trbs first=%I64x last=%I64x data=%lu hasData=%u dir=%s\n",
                 (ULONGLONG)Transfer->TdFirstTrbPointer,
                 (ULONGLONG)Transfer->CompletionTrbPointer,
                 DevDescBytesProgrammed,
                 HasDataStage ? 1 : 0,
                 DataIn ? "IN" : "OUT");
    }
    XHCI_AdvanceTransferRing(&Endpoint->TransferRing);

    {
        USHORT DoorbellStreamId = XHCI_SelectDoorbellStreamId(Endpoint, Transfer);

        /* Log all control transfers for debugging HID class requests */
        XHCI_DBG(XHCI_TRACE_TRANSFERS,
                 "usbxhci: CTRL submit slot=%u ep=%u setupType=0x%02x bReq=0x%02x wVal=0x%04x len=%lu\n",
                 Endpoint->SlotId,
                 Endpoint->EndpointId,
                 TransferParameters->SetupPacket.bmRequestType.B,
                 TransferParameters->SetupPacket.bRequest,
                 TransferParameters->SetupPacket.wValue.W,
                 TransferParameters->TransferBufferLength);

        if (TraceDevDesc)
        {
            XHCI_DBG(XHCI_TRACE_TRANSFERS,
                     "usbxhci: DEV doorbell slot=%u target=%u EnqIdx=%lu Cycle=%lu\n",
                     Endpoint->SlotId,
                     Endpoint->DoorbellTarget,
                     Endpoint->TransferRing.EnqueueIndex,
                     Endpoint->TransferRing.CycleState);
        }

        /*
         * Commit the deferred cycle bit on the Setup TRB. All other TRBs
         * (Data Stage, Link, Status) are already fully written. Setting
         * the cycle bit here makes the entire control transfer visible to
         * the hardware atomically, preventing a race where the xHCI
         * controller fetches the Setup TRB and races through the Link TRB
         * before the Status TRB has been programmed.
         *
         * This follows the same "deferred first TRB" pattern used by Linux
         * xhci-ring.c (giveback_first_trb).
         */
        if (SetupTrbDeferred)
        {
            KeMemoryBarrier();
            SetupTrbDeferred->Control |= SetupCycleBitDeferred;
        }

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
     * Schedule a poll timer for ALL EP0 control transfers to drain the event
     * ring in case an MSI interrupt is missed. This was originally limited to
     * SET_ADDRESS and GET_DESCRIPTOR, but on real hardware (LattePanda Mu with
     * Intel xHCI), SET_CONFIGURATION hangs indefinitely without polling because
     * the completion interrupt is not always delivered. EP0 transfers are
     * sequential and infrequent, so polling overhead is negligible.
     */
    if (Endpoint->DefaultControl ||
        (Extension->Quirks & XHCI_QUIRK_POLL_XFERS_MASK))
    {
        LONG NewCount;
        Transfer->Flags |= XHCI_TRANSFER_FLAG_NEEDS_POLL;
        NewCount = InterlockedIncrement(&Extension->TransferPollCounter);
        if (NewCount == 1)
            XHCI_ScheduleTransferPoll(Extension);
    }

    return MP_STATUS_SUCCESS;

Failure:
    if (Transfer->Flags & XHCI_TRANSFER_FLAG_NEEDS_POLL)
    {
        Transfer->Flags &= ~XHCI_TRANSFER_FLAG_NEEDS_POLL;
        if (InterlockedDecrement(&Extension->TransferPollCounter) <= 0)
            KeCancelTimer(&Extension->TransferPollTimer);
    }
    if (ProgrammedRing)
        XHCI_ResetEndpointRing(Endpoint);

    XHCI_ReleaseBounceBuffer(Transfer);
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

    if (Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK)
    {
        DPRINT("xhci: BulkXfer ep=%u len=%lu\n",
               Endpoint->EndpointId,
               Transfer->TransferParameters ?
                   Transfer->TransferParameters->TransferBufferLength : 0);
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
    MPSTATUS Status = MP_STATUS_SUCCESS;
    KIRQL OldIrql;
    ULONG IsoPayloadLimit = 0;
    BOOLEAN DataIn = FALSE;
    BOOLEAN UseBounce = FALSE;
    ULONGLONG HighAddress = 0;
#if DBG
    ULONG IocTrbCount = 0;
#endif
    PXHCI_RING Ring;
    USHORT StreamId;
    BOOLEAN ShortPacketOk = FALSE;

    if (!Extension || !Endpoint || !Transfer)
        return MP_STATUS_ERROR;

    StreamId = Transfer->StreamId;
    Ring = XHCI_SelectStreamRing(Endpoint, StreamId);

    if (!Endpoint->Slot || !Ring || !Ring->Base)
        return MP_STATUS_ERROR;

    KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
    if (Endpoint->ActiveTransfer)
    {
        DPRINT1("usbxhci: SubmitSgTransfer REJECTED slot=%u ep=%u: ActiveTransfer=%p already set\n",
                Endpoint->SlotId,
                Endpoint->EndpointId,
                Endpoint->ActiveTransfer);
        KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
        return MP_STATUS_FAILURE;
    }
    Endpoint->ActiveTransfer = Transfer;
    KeReleaseSpinLock(&Endpoint->Lock, OldIrql);

    TransferParameters = Transfer->TransferParameters;
    SgList = Transfer->SgList;
    if (TransferParameters)
        DataIn = (TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN) ? TRUE : FALSE;
    if (!IsIsochronous &&
        TransferParameters &&
        (TransferParameters->TransferFlags & USBD_SHORT_TRANSFER_OK) &&
        (TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN))
    {
        ShortPacketOk = TRUE;
    }

    XHCI_DBG(XHCI_TRACE_TRANSFERS,
             "usbxhci: SubmitSgTransfer slot=%u ep=%u len=%lu sg=%lu shortok=%u isin=%u\n",
             Endpoint->SlotId,
             Endpoint->EndpointId,
             TransferParameters ? TransferParameters->TransferBufferLength : 0,
             SgList ? SgList->SgElementCount : 0,
             ShortPacketOk ? 1 : 0,
             DataIn ? 1 : 0);

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

            /* Trace-level diagnostic for BOT (Bulk-Only Transport) commands */
            DPRINT("usbxhci: BOT CBW sig=%08lx tag=%08lx xfer=%08lx flags=%02x lun=%02x cblen=%02x cdb=%02x %02x %02x %02x %02x %02x\n",
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
        Trb = XHCI_GetTransferRingTrb(Ring,
                                      &PhysicalAddress,
                                      FALSE);
        if (!Trb)
        {
            Status = MP_STATUS_NO_RESOURCES;
            goto Failure;
        }
        Transfer->TdFirstTrbPointer = PhysicalAddress;

        Trb->Parameter1 = 0;
        Trb->Parameter2 = 0;
        Trb->Status = 0;
        Control = (TrbType << XHCI_TRB_TYPE_SHIFT) |
                  (Ring->CycleState & XHCI_TRB_CYCLE) |
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
        XHCI_AdvanceTransferRing(Ring);

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

    if (Remaining &&
        XHCI_Requires32BitDma(Extension) &&
        XHCI_SgListHasHighAddress(SgList, &HighAddress))
    {
        DPRINT1("usbxhci: sg DMA above 4G (pa=%I64x len=%lu), using bounce buffer\n",
                HighAddress,
                Remaining);
        Status = XHCI_PrepareBounceBuffer(Extension, Transfer, Remaining, DataIn);
        if (Status != MP_STATUS_SUCCESS)
            goto Failure;

        UseBounce = (Transfer->BounceBuffer != NULL);
        if (UseBounce && !DataIn)
        {
            ULONG Copied = XHCI_CopySgListToBuffer(SgList,
                                                   Transfer->BounceBuffer,
                                                   Transfer->BounceLength);
            if (Copied < Transfer->BounceLength)
            {
                DPRINT1("usbxhci: sg OUT bounce copy short (%lu/%lu)\n",
                        Copied,
                        Transfer->BounceLength);
            }
        }
    }

    if (UseBounce)
    {
        ULONG ElementRemaining = Transfer->BounceLength;
        ULONGLONG ElementAddress = Transfer->BouncePhysicalAddress.QuadPart;

        while (ElementRemaining && Remaining)
        {
            BOOLEAN TdContinues;

            BufferAddress = ElementAddress;
            Chunk = XHCI_CalcTrbTransferChunk(BufferAddress,
                                              ElementRemaining,
                                              Remaining,
                                              IsoPayloadLimit);

            TdContinues = (Remaining > Chunk);
            Trb = XHCI_GetTransferRingTrb(Ring,
                                          &PhysicalAddress,
                                          TdContinues);
            if (!Trb)
            {
                Status = MP_STATUS_NO_RESOURCES;
                goto Failure;
            }
            if (Transfer->TdFirstTrbPointer == 0)
                Transfer->TdFirstTrbPointer = PhysicalAddress;

            Trb->Parameter1 = (ULONG)(BufferAddress & 0xFFFFFFFF);
            Trb->Parameter2 = (ULONG)(BufferAddress >> 32);
            Trb->Status = Chunk;
            Control = (TrbType << XHCI_TRB_TYPE_SHIFT) |
                      (Ring->CycleState & XHCI_TRB_CYCLE);
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
            if (ShortPacketOk)
                Control |= XHCI_TRB_ISP;

            Trb->Control = Control;
            XHCI_AdvanceTransferRing(Ring);


            ElementAddress += Chunk;
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
    }
    else
    {
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
                Trb = XHCI_GetTransferRingTrb(Ring,
                                              &PhysicalAddress,
                                              TdContinues);
                if (!Trb)
                {
                    Status = MP_STATUS_NO_RESOURCES;
                    goto Failure;
                }
                if (Transfer->TdFirstTrbPointer == 0)
                    Transfer->TdFirstTrbPointer = PhysicalAddress;

                Trb->Parameter1 = (ULONG)(BufferAddress & 0xFFFFFFFF);
                Trb->Parameter2 = (ULONG)(BufferAddress >> 32);
                Trb->Status = Chunk;
                Control = (TrbType << XHCI_TRB_TYPE_SHIFT) |
                          (Ring->CycleState & XHCI_TRB_CYCLE);
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
                if (ShortPacketOk)
                    Control |= XHCI_TRB_ISP;

                Trb->Control = Control;
                XHCI_AdvanceTransferRing(Ring);


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

        XHCI_DBG(XHCI_TRACE_TRANSFERS, "usbxhci: bulk submit S%u E%u Req=%lu LastTrb=%I64x Buf=%I64x Len=%lu Enq=%lu CS=%lu EpState=%lx EpInfo2=%08lx\n",
                 Endpoint->SlotId,
                 Endpoint->EndpointId,
                 Transfer->RequestedLength,
                 (ULONGLONG)Transfer->CompletionTrbPointer,
                 Buffer,
                 TrbLen,
                 Ring ? Ring->EnqueueIndex : 0,
                 (ULONG)(Ring ? Ring->CycleState : 0),
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

    /*
     * Enable the poll fallback for bulk/interrupt transfers on emulators
     * where MSI/INTx delivery is unreliable (VirtualBox UEFI, QEMU q35
     * xHCI). Without this, interrupt-type endpoints such as HID mouse IN
     * stall until some other DPC drains the event ring, producing very
     * laggy cursor movement.
     */
    if (Extension->Quirks & XHCI_QUIRK_POLL_XFERS_MASK)
    {
        Transfer->Flags |= XHCI_TRANSFER_FLAG_NEEDS_POLL;
        if (InterlockedIncrement(&Extension->TransferPollCounter) == 1)
            XHCI_ScheduleTransferPoll(Extension);
    }

    return MP_STATUS_SUCCESS;

Failure:
    XHCI_ReleaseBounceBuffer(Transfer);
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
    Work->RefCount = 2;
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
        if (InterlockedDecrement(&Work->RefCount) == 0)
            ExFreePoolWithTag(Work, XHCI_TAG);
        return MP_STATUS_UNSUCCESSFUL;
    }

    {
        MPSTATUS Status = Work->Status;
        if (InterlockedDecrement(&Work->RefCount) == 0)
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
    if (InterlockedDecrement(&Work->RefCount) == 0)
        ExFreePoolWithTag(Work, XHCI_TAG);
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
    XHCI_ApplyEndpointLpmPolicy(Extension, EndpointProperties);
    IsDefaultPipe = (EndpointProperties->EndpointAddress == 0 &&
                     EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_CONTROL);

    if (IsDefaultPipe)
    {
        Slot = XHCI_FindSlotByAddress(Extension, EndpointProperties->DeviceAddress);
        if (!Slot && EndpointProperties->PortNumber != 0)
        {
            /*
             * Fallback to port-based lookup. This is needed in two scenarios:
             * 1. DeviceAddress == 0: Initial enumeration when no address is assigned yet
             * 2. After RESET_DEVICE: The slot's USB address was cleared to 0 by
             *    XHCI_UpdateDeviceAddressMap(), but USBPORT still uses the old address.
             *    The address lookup fails, but the slot is still valid and mapped to
             *    its port number.
             */
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

            /*
             * ReopenPipe may be called to update EP0 MaxPacketSize after the
             * initial 8-byte device descriptor fetch. Check if the MPS in the
             * xHC's device context differs from the requested MPS and issue an
             * EVALUATE_CONTEXT command if so. Without this, the xHC will
             * continue using the old MPS (typically 8 bytes) and transfers
             * requesting more data will stall because the host expects one
             * packet size while the device uses another.
             */
            {
                ULONG RequestedMps = EndpointProperties->MaxPacketSize;
                ULONG CurrentMps = 0;
                PVOID DeviceCtxBase = Slot->DeviceContext.VirtualAddress;

                if (RequestedMps == 0)
                    RequestedMps = 8;

                if (DeviceCtxBase)
                {
                    PXHCI_ENDPOINT_CONTEXT ActiveEpCtx =
                        XHCI_GetDeviceEndpointContextVa(Extension, DeviceCtxBase, 0);
                    if (ActiveEpCtx)
                    {
                        CurrentMps = (ActiveEpCtx->EpInfo2 & XHCI_EPCTX_MAX_PACKET_MASK) >>
                                     XHCI_EPCTX_MAX_PACKET_SHIFT;
                    }
                }

                if (CurrentMps != 0 && CurrentMps != RequestedMps)
                {
                    DPRINT1("usbxhci: EP0 MPS changed %lu -> %lu on slot %u, issuing EVALUATE_CONTEXT\n",
                            CurrentMps, RequestedMps, Slot->SlotId);
                    Status = XHCI_UpdateEp0MaxPacketSize(Extension, Slot, RequestedMps);
                    if (Status != MP_STATUS_SUCCESS)
                    {
                        DPRINT1("usbxhci: EVALUATE_CONTEXT for EP0 MPS update failed: %d\n", Status);
                        /* Continue anyway; the transfer may still work or fail gracefully */
                    }
                    else
                    {
                        /*
                         * After EVALUATE_CONTEXT succeeds, verify the endpoint is still
                         * in Running state. Some xHC implementations may transition the
                         * endpoint state unexpectedly.
                         */
                        PXHCI_ENDPOINT_CONTEXT PostEvalEpCtx =
                            XHCI_GetDeviceEndpointContextVa(Extension, DeviceCtxBase, 0);
                        if (PostEvalEpCtx)
                        {
                            ULONG EpState = PostEvalEpCtx->EpInfo & XHCI_EPCTX_STATE_MASK;
                            DPRINT1("usbxhci: EP0 state after EVALUATE_CONTEXT: %lu (Running=1)\n",
                                    EpState);
                        }
                    }
                }
                else
                {
                    DPRINT("usbxhci: EP0 ReopenPipe slot %u - MPS unchanged (current=%lu, requested=%lu)\n",
                           Slot->SlotId, CurrentMps, RequestedMps);
                }
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

            DPRINT("usbxhci: EP0 ReopenPipe complete slot=%u EnqIdx=%lu DeqIdx=%lu Cycle=%lu\n",
                   Slot->SlotId,
                   XhciEndpoint->TransferRing.EnqueueIndex,
                   XhciEndpoint->TransferRing.DequeueIndex,
                   XhciEndpoint->TransferRing.CycleState);

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

                DPRINT("usbxhci: deferred EP0 bring-up from IRQL=%lu\n",
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

    /* Diagnostic: log all non-EP0 endpoint opens to trace mass storage enumeration */
    DPRINT("usbxhci: OpenEndpoint addr=%u port=%u epAddr=0x%02x type=%u speed=%u -> EpId=%u slot=%u\n",
           EndpointProperties->DeviceAddress,
           EndpointProperties->PortNumber,
           EndpointProperties->EndpointAddress,
           EndpointProperties->TransferType,
           EndpointProperties->DeviceSpeed,
           EndpointId,
           Slot->SlotId);
    if (EndpointId == 0)
        return MP_STATUS_ERROR;

    EndpointType = XHCI_GetEndpointTypeFromProperties(EndpointProperties);

    if (EndpointId < RTL_NUMBER_OF(Slot->EndpointTable) &&
        Slot->EndpointTable[EndpointId] != NULL)
    {
        PXHCI_ENDPOINT Existing = Slot->EndpointTable[EndpointId];

        DPRINT("usbxhci: endpoint %u already configured on slot %u -- refreshing context via EvaluateContext\n",
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

    DPRINT("usbxhci: OpenEndpoint calling ConfigureSlotEndpoint slot=%u ep=%u (before)\n",
           Slot->SlotId, EndpointId);

    Status = XHCI_ConfigureSlotEndpoint(Extension,
                                        Slot,
                                        XhciEndpoint,
                                        EndpointId);

    DPRINT("usbxhci: OpenEndpoint ConfigureSlotEndpoint returned status=%d slot=%u ep=%u\n",
           Status, Slot->SlotId, EndpointId);

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
    ULONG WaitIterations;
    LONG RefCount;
    KIRQL CurrentIrql;
    UNREFERENCED_PARAMETER(MiniPortExtension);
    UNREFERENCED_PARAMETER(IsDoNotCallMiniport);

    if (!XhciEndpoint)
        return;

#if DBG
    if (XhciEndpoint->SlotId == 1 && (XhciEndpoint->EndpointId == 3 || XhciEndpoint->EndpointId == 4))
    {
        DPRINT("usbxhci: CloseEndpoint slot=%u ep=%u addr=0x%02x\n",
                XhciEndpoint->SlotId,
                XhciEndpoint->EndpointId,
                (UCHAR)(XhciEndpoint->EndpointProperties.EndpointAddress & 0xFF));
    }
#endif

    /*
     * Mark the endpoint as closing. This prevents new SW-enum work from
     * being queued via XHCI_ReferenceEndpointForSwEnum.
     */
    InterlockedExchange(&XhciEndpoint->Closing, 1);

    /*
     * Wait for any pending SW-enum work to complete. We poll the reference
     * count with a timeout to avoid blocking indefinitely if something
     * goes wrong.
     *
     * IRQL handling:
     * - At PASSIVE_LEVEL or APC_LEVEL: We can safely sleep and wait for the
     *   work items to drain. Work items run at PASSIVE_LEVEL so they will
     *   complete and decrement SwEnumRefCount.
     * - At DISPATCH_LEVEL: We cannot wait because work items need PASSIVE_LEVEL
     *   to run. Busy-waiting at DISPATCH would starve the system. Instead, we
     *   just set Closing=1 (done above) and let StopController handle draining
     *   later at PASSIVE_LEVEL.
     */
    CurrentIrql = KeGetCurrentIrql();
    if (CurrentIrql > APC_LEVEL)
    {
        /*
         * At DISPATCH_LEVEL or higher - cannot wait for PASSIVE work items.
         * The Closing flag is set, preventing new work. Any pending work
         * will be drained by StopController at PASSIVE_LEVEL.
         */
        RefCount = InterlockedCompareExchange(&XhciEndpoint->SwEnumRefCount, 0, 0);
        if (RefCount != 0)
        {
            DPRINT1("usbxhci: CloseEndpoint at IRQL=%u, SwEnumRefCount=%ld - deferring drain to StopController\n",
                    (ULONG)CurrentIrql, RefCount);
        }
    }
    else
    {
        /* At PASSIVE_LEVEL or APC_LEVEL - safe to wait for drain */
        WaitIterations = XHCI_CLOSE_DRAIN_TIMEOUT_US / XHCI_CLOSE_DRAIN_POLL_US;
        while (WaitIterations > 0)
        {
            RefCount = InterlockedCompareExchange(&XhciEndpoint->SwEnumRefCount, 0, 0);
            if (RefCount == 0)
                break;

            /*
             * Sleep briefly to allow pending work items to complete.
             * Use KeDelayExecutionThread to yield the CPU properly.
             */
            {
                LARGE_INTEGER Delay;
                Delay.QuadPart = -10LL * XHCI_CLOSE_DRAIN_POLL_US; /* 100ns units, negative = relative */
                KeDelayExecutionThread(KernelMode, FALSE, &Delay);
            }
            WaitIterations--;
        }

        if (WaitIterations == 0)
        {
            RefCount = InterlockedCompareExchange(&XhciEndpoint->SwEnumRefCount, 0, 0);
            if (RefCount != 0)
            {
                DPRINT1("usbxhci: CloseEndpoint timeout waiting for SwEnumRefCount=%ld to drain\n",
                        RefCount);
            }
        }
    }

    if (XhciEndpoint->Slot &&
        XhciEndpoint->EndpointId < RTL_NUMBER_OF(XhciEndpoint->Slot->EndpointTable))
    {
        if (XhciEndpoint->Slot->DeferredEndpointTable[XhciEndpoint->EndpointId] == XhciEndpoint)
            XhciEndpoint->Slot->DeferredEndpointTable[XhciEndpoint->EndpointId] = NULL;

        /*
         * Only drop the endpoint from the xHC and clear the table entry if
         * WE are still the active endpoint for this EpId.  During interface
         * reconfiguration (e.g. mass-storage SET_INTERFACE), OpenEndpoint
         * may have already replaced the table entry with a new endpoint
         * object that has the same EpId.  Dropping here would remove the
         * NEW endpoint's xHC configuration, causing "has no endpoint"
         * failures on subsequent transfers.
         */
        if (XhciEndpoint->Slot->EndpointTable[XhciEndpoint->EndpointId] == XhciEndpoint)
        {
            if (!XhciEndpoint->DefaultControl)
                XHCI_DropSlotEndpoint(XhciEndpoint->Extension,
                                      XhciEndpoint->Slot,
                                      XhciEndpoint->EndpointId);

            /*
             * Re-check ownership: OpenEndpoint (running on another CPU
             * without MiniportSpinLock) may have replaced our entry
             * during the CONFIG_EP drop busy-poll.
             */
            if (XhciEndpoint->Slot->EndpointTable[XhciEndpoint->EndpointId] == XhciEndpoint)
                XhciEndpoint->Slot->EndpointTable[XhciEndpoint->EndpointId] = NULL;
        }
    }

    if (!XhciEndpoint->UsesStaticRing)
    {
        /*
         * Now that SwEnumRefCount has drained (or timed out), also check
         * PendingWorkCount for any other deferred work.
         */
        if (InterlockedCompareExchange(&XhciEndpoint->PendingWorkCount, 0, 0) != 0)
        {
            DPRINT1("usbxhci: CloseEndpoint skipping ring free while PendingWorkCount is non-zero\n");
        }
        else
            XHCI_FreeTransferRing(&XhciEndpoint->TransferRing);
    }

    if (XhciEndpoint->StreamsEnabled)
        XHCI_FreeStreamResources(XhciEndpoint);

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
    KeInitializeTimerEx(&Extension->TransferPollTimer, NotificationTimer);
    KeInitializeDpc(&Extension->TransferPollDpc, XHCI_TransferPollDpc, Extension);
    Extension->TransferPollCounter = 0;
    Extension->LastMfIndex = 0;
    Extension->FrameHighBits = 0;
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
        ULONG CapHeader0 = XHCI_READ_REGISTER_ULONG((volatile ULONG *)Base);
        Extension->CapabilityLength = CapHeader0 & 0xFF;
        /* HciVersion occupies bits 31:16 of the first dword (little-endian) */
        Extension->HciVersion = (USHORT)((CapHeader0 >> 16) & 0xFFFF);
    }
    DPRINT("usbxhci: MMIO base=%p CAPLEN=%lu\n", Extension->MmioBase, Extension->CapabilityLength);
    if (Extension->CapabilityLength < sizeof(XHCI_CAPABILITY_REGISTERS))
    {
        DPRINT1("usbxhci: invalid CAPLENGTH %lu\n", Extension->CapabilityLength);
        return MP_STATUS_ERROR;
    }

    DPRINT("usbxhci: resource base=%p iospace=%lu startVA=%p startPA=%08lx irq=%lx flags=%lx msgcnt=%lu\n",
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
    DPRINT("usbxhci: DB offset=%lu RT offset=%lu Doorbell=%p Runtime=%p\n",
            DbOffset, RtOffset, Extension->DoorbellArray, Extension->RuntimeRegisters);

    /* Dump the first 32 bytes of the capability header to catch mis-mapped BARs. */
    {
        ULONG CapDump[8] = {0};
        SIZE_T i;
        for (i = 0; i < RTL_NUMBER_OF(CapDump); i++)
        {
            CapDump[i] = XHCI_READ_REGISTER_ULONG((volatile ULONG *)(Base + (i * sizeof(ULONG))));
        }
        DPRINT("usbxhci: CAP dump 0x00-0x1F: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
                CapDump[0], CapDump[1], CapDump[2], CapDump[3],
                CapDump[4], CapDump[5], CapDump[6], CapDump[7]);
    }

    XHCI_GetRegistryParameters(Extension);

    /*
     * Evaluate USB _OSC before any hardware configuration.
     * This must be done at PASSIVE_LEVEL (StartController is called during
     * PnP IRP_MN_START_DEVICE which runs at PASSIVE_LEVEL).
     *
     * _OSC negotiation determines what controls the OS is allowed to exercise:
     * - Port power switching
     * - Link state management
     * - USB-C mux control
     * - Power state transitions
     * - Compliance mode recovery
     * - U1/U2 LPM entry
     *
     * If _OSC is not present (legacy platform) or fails, we assume full OS
     * control to maintain backwards compatibility.
     */
    {
        NTSTATUS OscStatus;

        DPRINT("usbxhci: Evaluating USB _OSC for capability negotiation\n");
        OscStatus = XHCI_EvaluateOsc(Extension);

        if (NT_SUCCESS(OscStatus))
        {
            DPRINT("usbxhci: _OSC negotiation complete - Granted=0x%lX, Requested=0x%lX, Available=0x%lX\n",
                    Extension->OscContext.ControlGranted,
                    Extension->OscContext.ControlRequested,
                    Extension->OscContext.ControlAvailable);

            if (Extension->OscContext.FirmwareFirst)
            {
                DPRINT1("usbxhci: Running in Firmware First mode (firmware controls USB)\n");
            }
        }
        else if (OscStatus == STATUS_NOT_FOUND ||
                 OscStatus == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            /* _OSC not present - legacy platform with full OS control */
            DPRINT("usbxhci: No USB _OSC method (legacy platform), assuming full OS control\n");
        }
        else
        {
            DPRINT1("usbxhci: _OSC evaluation failed: 0x%lX, assuming full OS control\n",
                    OscStatus);
        }
    }

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
#if !defined(_M_ARM64)
    /*
     * On x86/x64, we can enable MSI-X ourselves if the hardware supports it.
     * On ARM64, MSI-X requires the GIC ITS which may not be available or may have
     * failed initialization (e.g., QEMU HVF doesn't support ITS). On ARM64, we rely
     * on USBPORT to set up MSI-X; if USBPORT failed, MSI-X will not be enabled in
     * the PCI config and we should not try to enable it ourselves.
     */
    if (Extension->MsixSupported && !Extension->MsixEnabled)
    {
        if (!XHCI_EnableMsix(Extension))
            DPRINT1("usbxhci: failed to enable MSI-X\n");
    }
#else
    DPRINT1("usbxhci: ARM64: MSI-X enable delegated to USBPORT (MsixEnabled=%d)\n",
            Extension->MsixEnabled);
#endif
    /* MSI/MSI-X only: require message interrupt resources and enabled MSI/MSI-X. */
    if ((UsbPortResources->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE) == 0)
    {
        DPRINT1("usbxhci: MSI/MSI-X required but no message interrupt resource assigned\n");
        return MP_STATUS_NOT_SUPPORTED;
    }

    if (!Extension->MsixEnabled && !Extension->MsiEnabled)
    {
        DPRINT1("usbxhci: MSI/MSI-X required but not enabled in PCI config\n");
        return MP_STATUS_NOT_SUPPORTED;
    }

    /*
     * Disable legacy INTx interrupts now that MSI/MSI-X is confirmed active.
     * Per PCI 3.0 spec section 6.8.1, the device must not assert INTx when
     * MSI/MSI-X is enabled. Some emulators (notably QEMU xhci-pci) violate
     * this by asserting BOTH MSI and INTx simultaneously, causing an interrupt
     * storm on the unhandled legacy vector after device disconnect.
     *
     * USBPORT_ProgramMsixTable/USBPORT_ProgramMsiTable also set this bit,
     * but we set it again here as defense-in-depth in case:
     * 1. The PCI bus driver restored default Command register state
     * 2. USBPORT's MSI programming took a different code path
     * 3. The bit was cleared by a controller reset during startup
     */
    XHCI_DisablePciIntx(Extension);

    {
        ULONG Messages = UsbPortResources->InterruptMessageCount ?
                         UsbPortResources->InterruptMessageCount : 1;
        DPRINT("usbxhci: using message interrupts (%lu vector%s)\n",
                Messages,
                (Messages == 1) ? "" : "s");
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

    HcsParams1 = XHCI_READ_REGISTER_ULONG(&Extension->CapabilityRegisters->HcsParams1);
    HcsParams2 = XHCI_READ_REGISTER_ULONG(&Extension->CapabilityRegisters->HcsParams2);
    HcsParams3 = XHCI_READ_REGISTER_ULONG(&Extension->CapabilityRegisters->HcsParams3);
    HccParams = XHCI_READ_REGISTER_ULONG(&Extension->CapabilityRegisters->HccParams);

    {
        ULONG CapRaw0 = XHCI_READ_REGISTER_ULONG((volatile ULONG *)Extension->CapabilityRegisters);
        ULONG CapRaw4 = XHCI_READ_REGISTER_ULONG((volatile ULONG *)((PUCHAR)Extension->CapabilityRegisters + 4));
        ULONG CapRaw8 = XHCI_READ_REGISTER_ULONG((volatile ULONG *)((PUCHAR)Extension->CapabilityRegisters + 8));
        DPRINT("usbxhci: CAP dwords [0]=%08lx [4]=%08lx [8]=%08lx\n",
                CapRaw0, CapRaw4, CapRaw8);
    }

    DPRINT("usbxhci: CAP raw HciVer=%04x (xHCI %u.%02u) HCS1=%08lx HCS2=%08lx HCS3=%08lx HCC=%08lx CAPLEN=%lu DbOff=%lu RtOff=%lu\n",
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
                XHCI_READ_REGISTER_UCHAR((volatile UCHAR *)Base + 0),
                XHCI_READ_REGISTER_UCHAR((volatile UCHAR *)Base + 1),
                XHCI_READ_REGISTER_UCHAR((volatile UCHAR *)Base + 2),
                XHCI_READ_REGISTER_UCHAR((volatile UCHAR *)Base + 3),
                XHCI_READ_REGISTER_UCHAR((volatile UCHAR *)Base + 4),
                XHCI_READ_REGISTER_UCHAR((volatile UCHAR *)Base + 5),
                XHCI_READ_REGISTER_UCHAR((volatile UCHAR *)Base + 6),
                XHCI_READ_REGISTER_UCHAR((volatile UCHAR *)Base + 7));
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
    Extension->Supports64Bit = XHCI_DetermineDma64Bit(UsbPortResources, HccParams);
    Extension->ContextSize = XHCI_HCC_64B_CONTEXT(HccParams) ? 64 : 32;
    Extension->MaxPrimaryStreams = (UCHAR)XHCI_HCC_MAX_PSTREAMS(HccParams);
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
        /*
         * Force single interrupter until per-interrupter ERST/ERDP and event
         * ring servicing is implemented. See TODO_XHCI.md for details.
         */
        Extension->InterrupterCount = 1;
        (void)ActiveIntr; /* suppress unused-variable warning */
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

    Status = XHCI_InitBouncePool(Extension);
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

    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
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

        XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
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
    if (XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts) & XHCI_USBSTS_HCE)
    {
        ULONG UsbSts = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);
        ULONG UsbCmd = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd);

        DPRINT1("usbxhci: host controller error latched after start "
                "(USBSTS=%08lx USBCMD=%08lx) – attempting one recovery\n",
                UsbSts,
                UsbCmd);

        XHCI_HaltController(Extension, XHCI_WAIT_HALT_US);
        Status = XHCI_ResetController(Extension);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts,
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

    if (XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts) & XHCI_USBSTS_HCE)
    {
        ULONG UsbStsAfter = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);

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

    XHCI_PowerOnAllPorts(Extension);
    XHCI_ConfigureAllPortsLpm(Extension);

    /*
     * When the poll-fallback quirk is active (QEMU/VBox), the
     * XHCI_TransferPollDpc runs off a KTIMER whose DueTime is clamped
     * to the system clock quantum. ReactOS defaults to ~10 ms, which
     * pins HID interrupt-IN completions to ~100 Hz and makes the
     * cursor feel laggy. Request a 1 ms system clock resolution so
     * KeSetTimer can honour sub-10 ms intervals. This is reversed in
     * XHCI_StopController. Do NOT raise the resolution when real
     * interrupts are driving completions — polling isn't the hot path.
     */
    if ((Extension->Quirks & XHCI_QUIRK_POLL_XFERS_MASK) &&
        !Extension->TimerResolutionRaised)
    {
        ULONG Achieved = ExSetTimerResolution(10000 /* 100ns = 1ms */, TRUE);
        Extension->TimerResolutionRaised = TRUE;
        DPRINT("usbxhci: raised system timer resolution to %lu*100ns "
               "(target 10000) for poll-fallback quirk\n",
               Achieved);
    }

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
    KeCancelTimer(&Extension->TransferPollTimer);

    /* Reverse the ExSetTimerResolution raise performed at Start. */
    if (Extension->TimerResolutionRaised)
    {
        Extension->TimerResolutionRaised = FALSE;
        (VOID)ExSetTimerResolution(0, FALSE);
    }
    InterlockedExchange(&Extension->TransferPollCounter, 0);

    /* Mark any pending SW-enum transfers as canceled so workers exit cleanly. */
    {
        ULONG SlotIndex;

        for (SlotIndex = 1;
             SlotIndex <= (Extension->MaxSlots ? Extension->MaxSlots : XHCI_MAX_SLOTS);
             SlotIndex++)
        {
            PXHCI_DEVICE_SLOT Slot = &Extension->DeviceSlots[SlotIndex];
            PXHCI_ENDPOINT Ep0;
            PXHCI_TRANSFER Transfer;
            KIRQL OldIrqlEp;

            if (!Slot->InUse ||
                1 >= RTL_NUMBER_OF(Slot->EndpointTable))
                continue;

            Ep0 = Slot->EndpointTable[1];
            if (!Ep0)
                continue;

            KeAcquireSpinLock(&Ep0->Lock, &OldIrqlEp);
            Transfer = Ep0->ActiveTransfer;
            KeReleaseSpinLock(&Ep0->Lock, OldIrqlEp);

            if (Transfer && (Transfer->Flags & XHCI_TRANSFER_FLAG_SWENUM_PENDING))
            {
                InterlockedOr((volatile LONG *)&Transfer->Flags,
                              XHCI_TRANSFER_FLAG_SWENUM_CANCELED);
            }
        }
    }

    /* Give any pending EP0 bring-up work a chance to drain before we tear
     * down hardware/MMIO pointers. */
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        LARGE_INTEGER Interval;

        Interval.QuadPart = -(LONGLONG)100 * 10; /* 100us */

        WaitLoops = XHCI_EP0_WORK_TIMEOUT_US / 100;
        while (InterlockedCompareExchange(&Extension->Ep0WorkerCount, 0, 0) != 0 &&
               WaitLoops--)
        {
            KeDelayExecutionThread(KernelMode, FALSE, &Interval);
        }

        /* Drain any deferred SW-ENUM work before tearing down controller state. */
        WaitLoops = XHCI_SWENUM_WORK_TIMEOUT_US / 100;
        while (InterlockedCompareExchange(&Extension->SwEnumWorkerCount, 0, 0) != 0 &&
               WaitLoops--)
        {
            KeDelayExecutionThread(KernelMode, FALSE, &Interval);
        }
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

        /* Check if already completed by another path (disable-slot, etc.) */
        if (InterlockedBitTestAndSet((volatile LONG *)&Transfer->Flags,
                                     XHCI_TRANSFER_FLAG_COMPLETED_BIT))
        {
            DPRINT1("usbxhci: StopController drain skipping already-completed transfer %p\n",
                    Transfer);
            KeAcquireSpinLock(&Extension->DeferredTransferLock, &OldIrql);
            continue;
        }

        if (XhciRegPacket.UsbPortCompleteTransfer &&
            Transfer->Endpoint &&
            Transfer->TransferParameters)
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

    XHCI_FreeBouncePool(Extension);
    if (Extension->AllocatedCommonBuffer)
    {
        MmFreeContiguousMemory(Extension->AllocatedCommonBuffer);
        Extension->AllocatedCommonBuffer = NULL;
        Extension->AllocatedCommonBufferPhysical.QuadPart = 0;
        Extension->AllocatedCommonBufferSize = 0;
    }
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
    Extension->Signature = 0;
    Extension->Quirks = 0;
    RtlFillMemory(Extension->PortLinkState,
                  sizeof(Extension->PortLinkState),
                  XHCI_INVALID_LINK_STATE);
    RtlZeroMemory(Extension->PortConnectStatus,
                  sizeof(Extension->PortConnectStatus));
    XHCI_InitDeviceAddressMap(Extension);
}

static BOOLEAN NTAPI
XHCI_InterruptService(PVOID MiniPortExtension)
{
    PXHCI_EXTENSION Extension = MiniPortExtension;
    ULONG Status;
    ULONG AckMask;
    ULONG UsbSts;

    if (!Extension || !Extension->OperationalRegisters)
        return FALSE;

    /*
     * When FatalError or StoppingOrRemoved, we must still acknowledge the
     * hardware interrupt sources to prevent an interrupt storm. The controller
     * may be asserting IMAN.IP and/or USBSTS bits that keep the interrupt line
     * active. Simply returning FALSE leaves these bits set, and for level-
     * triggered interrupts (or emulators like QEMU that assert INTx alongside
     * MSI), the interrupt will fire again immediately.
     *
     * Clear IMAN.IP and USBSTS write-to-clear bits, then return TRUE to
     * claim the interrupt without queuing a DPC.
     */
    if (Extension->FatalError || Extension->StoppingOrRemoved)
    {
        ULONG StsAck;

        /* Clear IMAN.IP on interrupter 0 */
        if (Extension->RuntimeRegisters)
        {
            PXHCI_INTERRUPTER_REGISTER_SET Intr = &Extension->RuntimeRegisters->Interrupter[0];
            ULONG ImanVal = XHCI_READ_REGISTER_ULONG(&Intr->Iman);
            if (ImanVal & XHCI_IMAN_IP)
            {
                /* Write IP=1 (RW1C) to clear, keep IE as-is */
                XHCI_WRITE_REGISTER_ULONG(&Intr->Iman,
                                          XHCI_IMAN_IP | (ImanVal & XHCI_IMAN_IE));
            }
        }

        /* Clear all write-to-clear status bits */
        StsAck = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);
        StsAck &= (XHCI_USBSTS_EINT | XHCI_USBSTS_PCD |
                    XHCI_USBSTS_HSE | XHCI_USBSTS_HCE);
        if (StsAck)
        {
            XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts, StsAck);
        }

        return TRUE; /* Claim the interrupt, but do not queue DPC */
    }

    Status = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);

    AckMask = Status & (XHCI_USBSTS_EINT |
                        XHCI_USBSTS_PCD |
                        XHCI_USBSTS_HSE |
                        XHCI_USBSTS_HCE);

    if (!AckMask)
        return FALSE;

    /* If HCE/HSE are set, latch them into PendingUsbSts even when EINT is
     * clear so the DPC can decide whether to ignore or handle them. */
    UsbSts = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);
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

    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts, AckMask);

    /*
     * Note on IMAN.IP clearing:
     *
     * For XHCI, IMAN.IP (Interrupt Pending) needs to be cleared for level-triggered
     * legacy INTx interrupts. However, we must NOT clear it here in the ISR because:
     *
     * 1. The controller will re-assert IMAN.IP if there are pending events
     * 2. The DPC processes events and updates ERDP which naturally clears IP
     * 3. Prematurely clearing IP can cause lost interrupts
     *
     * For MSI/MSI-X (edge-triggered), IP auto-clears after the interrupt message
     * is sent, so we don't need to touch it.
     *
     * Avoid clearing IMAN.IP here; the DPC path updates ERDP and naturally
     * clears pending interrupts.
     */

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
    ULONG CommandAfter;
    PXHCI_INTERRUPTER_REGISTER_SET Interrupter;
    ULONG Iman;
    ULONG ImanAfter;

    if (!Extension || !Extension->OperationalRegisters)
        return;

    Command = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd);
    Command |= XHCI_USBCMD_INTE;
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd, Command);
    Extension->InterruptsEnabled = TRUE;

    CommandAfter = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd);
    DPRINT("usbxhci: EnableInterrupts USBCMD before=%08lx after=%08lx (INTE=%u)\n",
            Command & ~XHCI_USBCMD_INTE, CommandAfter, (CommandAfter & XHCI_USBCMD_INTE) ? 1 : 0);

    if (Extension->RuntimeRegisters)
    {
        Interrupter = &Extension->RuntimeRegisters->Interrupter[0];
        Iman = XHCI_READ_REGISTER_ULONG(&Interrupter->Iman);
        /* Enable interrupter: set IE and clear any pending IP (RW1C) */
        XHCI_WRITE_REGISTER_ULONG(&Interrupter->Iman, XHCI_IMAN_IE | XHCI_IMAN_IP);

        ImanAfter = XHCI_READ_REGISTER_ULONG(&Interrupter->Iman);
        DPRINT("usbxhci: EnableInterrupts IMAN before=%08lx after=%08lx (IE=%u IP=%u)\n",
                Iman, ImanAfter,
                (ImanAfter & XHCI_IMAN_IE) ? 1 : 0,
                (ImanAfter & XHCI_IMAN_IP) ? 1 : 0);
    }
}

/* ========================= Safe stub implementations ========================= */

static MPSTATUS NTAPI
XHCI_ReopenEndpoint(PVOID MiniPortExtension,
                    PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                    PVOID EndpointHandle)
{
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    PXHCI_ENDPOINT Endpoint = (PXHCI_ENDPOINT)EndpointHandle;
    USHORT MaxStreamId;
    ULONG MaxSupported;
    MPSTATUS Status;

    if (!Extension || !Endpoint || !EndpointProperties)
        return MP_STATUS_ERROR;

    /*
     * Stream resource allocation uses MmAllocateContiguousMemorySpecifyCache
     * which MUST be called at PASSIVE_LEVEL. Reject the call if we're at
     * elevated IRQL - the caller must defer to a work item.
     */
    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
    {
        DPRINT1("usbxhci: ReopenEndpoint called at IRQL %u, must be PASSIVE_LEVEL\n",
                (ULONG)KeGetCurrentIrql());
        return MP_STATUS_FAILURE;
    }

    if (!Endpoint->Slot)
        return MP_STATUS_ERROR;

    if (EndpointProperties->TransferType != USBPORT_TRANSFER_TYPE_BULK)
        return MP_STATUS_NOT_SUPPORTED;

    MaxStreamId = (USHORT)(EndpointProperties->Reserved6 & 0xFFFF);
    if (MaxStreamId == 0)
    {
        if (Endpoint->StreamsEnabled)
            XHCI_FreeStreamResources(Endpoint);

        Endpoint->ReservedStreamId = 0;
        Endpoint->MaxStreamId = 0;
        Endpoint->StreamsEnabled = FALSE;

        return XHCI_ConfigureSlotEndpoint(Extension,
                                          Endpoint->Slot,
                                          Endpoint,
                                          Endpoint->EndpointId);
    }

    if (EndpointProperties->DeviceSpeed != UsbSuperSpeed)
        return MP_STATUS_NOT_SUPPORTED;

    MaxSupported = XHCI_GetMaxStreamId(Extension);
    if (MaxSupported == 0 || MaxStreamId > MaxSupported)
        return MP_STATUS_NOT_SUPPORTED;

    if ((MaxStreamId & (MaxStreamId - 1)) != 0)
        return MP_STATUS_NOT_SUPPORTED;

    if (Endpoint->StreamsEnabled && Endpoint->MaxStreamId == MaxStreamId)
    {
        return XHCI_ConfigureSlotEndpoint(Extension,
                                          Endpoint->Slot,
                                          Endpoint,
                                          Endpoint->EndpointId);
    }

    if (Endpoint->StreamsEnabled)
        XHCI_FreeStreamResources(Endpoint);

    Status = XHCI_AllocateStreamResources(Extension, Endpoint, MaxStreamId);
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    return XHCI_ConfigureSlotEndpoint(Extension,
                                      Endpoint->Slot,
                                      Endpoint,
                                      Endpoint->EndpointId);
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
    PXHCI_TRANSFER Transfer = (PXHCI_TRANSFER)TransferHandle;
    KIRQL Irql;

    if (BytesTransferred)
        *BytesTransferred = 0;

    if (!Extension || !Endpoint || !Endpoint->Slot)
    {
        DPRINT1("usbxhci: AbortTransfer invalid args: Extension=%p Endpoint=%p Slot=%p (IRQL=%lu)\n",
                Extension,
                Endpoint,
                Endpoint ? Endpoint->Slot : NULL,
                (ULONG)KeGetCurrentIrql());
        /*
         * If the slot is NULL (device disconnected and slot freed), we cannot
         * perform any hardware abort operations. Return immediately - USBPORT
         * will handle this via the timeout mechanism that sets ENDPOINT_FLAG_NUKE
         * to force-complete remaining transfers.
         */
        return;
    }

    /*
     * If the slot has already been disabled (or is in the process of being
     * disabled), we must not issue any xHCI commands (Stop Endpoint, Reset
     * Endpoint, Set TR Dequeue Pointer) to the hardware. The xHC will
     * reject commands for disabled slots with completion code 11
     * (SLOT_NOT_ENABLED), and the resulting command completions keep the
     * event ring active, which in turn keeps IMAN.IP asserted and can
     * cause an interrupt storm on the legacy INTx vector.
     */
    if (!Endpoint->Slot->InUse || Endpoint->Slot->DisablePending)
    {
        DPRINT1("usbxhci: AbortTransfer: slot %u ep %u slot disabled (InUse=%u DisablePending=%u), skipping\n",
                Endpoint->SlotId,
                Endpoint->EndpointId,
                Endpoint->Slot->InUse,
                Endpoint->Slot->DisablePending);
        return;
    }

    DPRINT1("usbxhci: AbortTransfer ENTRY: slot %u ep %u Transfer=%p ActiveTransfer=%p Ep0StallReset=%u\n",
            Endpoint->SlotId,
            Endpoint->EndpointId,
            Transfer,
            Endpoint->ActiveTransfer,
            (Endpoint->Slot ? Endpoint->Slot->Ep0NeedsStallReset : 0));

    if (Transfer && (Transfer->Flags & XHCI_TRANSFER_FLAG_SWENUM_PENDING))
    {
        DPRINT1("usbxhci: AbortTransfer: slot %u ep %u SWENUM_PENDING, marking canceled\n",
                Endpoint->SlotId,
                Endpoint->EndpointId);
        InterlockedOr((volatile LONG *)&Transfer->Flags,
                      XHCI_TRANSFER_FLAG_SWENUM_CANCELED);
        return;
    }

    /*
     * Check if the transfer USBPORT wants to abort matches the currently active transfer.
     * USBPORT may call AbortTransfer for a transfer that already completed (e.g., BOS
     * descriptor failed with STALL, inline reset ran, transfer completed with error).
     * By the time USBPORT's timeout fires, a different transfer may be active on this
     * endpoint. In that case, the requested transfer already completed - nothing to abort.
     */
    DPRINT1("usbxhci: AbortTransfer: slot %u ep %u CHECK1: Transfer=%p ActiveTransfer=%p (same=%u)\n",
            Endpoint->SlotId,
            Endpoint->EndpointId,
            Transfer,
            Endpoint->ActiveTransfer,
            (Transfer && Endpoint->ActiveTransfer) ? (Transfer == Endpoint->ActiveTransfer) : 99);

    if (Transfer && Endpoint->ActiveTransfer && Endpoint->ActiveTransfer != Transfer)
    {
        DPRINT1("usbxhci: AbortTransfer: slot %u ep %u transfer %p not active (active=%p), already completed\n",
                Endpoint->SlotId,
                Endpoint->EndpointId,
                Transfer,
                Endpoint->ActiveTransfer);
        return;
    }

    /*
     * Even if Transfer == ActiveTransfer (pointer reuse), USBPORT may be timing out on
     * an OLD transfer while a NEW transfer is in progress using the same memory.
     * Check endpoint state: if it's Running with an active transfer, the transfer is
     * progressing normally and USBPORT's timeout is spurious. Skip the reset.
     */
    if (Endpoint->ActiveTransfer)
    {
        ULONG EpState = XHCI_EPCTX_STATE_RUNNING;

        DPRINT1("usbxhci: AbortTransfer: slot %u ep %u CHECK2: has ActiveTransfer, checking EP state\n",
                Endpoint->SlotId,
                Endpoint->EndpointId);

        if (Endpoint->Slot && Endpoint->Slot->DeviceContext.VirtualAddress &&
            Endpoint->EndpointId != 0)
        {
            PXHCI_ENDPOINT_CONTEXT EpCtx =
                XHCI_GetDeviceEndpointContextVa(Extension,
                                                Endpoint->Slot->DeviceContext.VirtualAddress,
                                                Endpoint->EndpointId - 1);
            if (EpCtx)
                EpState = EpCtx->EpInfo & XHCI_EPCTX_STATE_MASK;
        }

        DPRINT1("usbxhci: AbortTransfer: slot %u ep %u CHECK2: EP state=%lu (0=Disabled,1=Running,2=Halted,3=Stopped,4=Error)\n",
                Endpoint->SlotId,
                Endpoint->EndpointId,
                EpState);

        /*
         * Handle endpoint state:
         * - Halted (2) or Error (4): needs recovery via reset sequence
         * - Stopped (3): endpoint was already stopped, clear stale state
         * - Running (1) or Disabled (0): spurious abort, skip
         */
        if (EpState == XHCI_EPCTX_STATE_STOPPED)
        {
            /*
             * Endpoint is already Stopped - this means a previous STOP_ENDPOINT
             * command succeeded. Clear the stale ActiveTransfer pointer so new
             * transfers can proceed. The transfer was aborted by the stop.
             * Must hold Endpoint->Lock to avoid racing with transfer submission.
             */
            KIRQL OldIrql;
            DPRINT1("usbxhci: AbortTransfer: slot %u ep %u EP is Stopped (3), clearing stale ActiveTransfer %p\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId,
                    Endpoint->ActiveTransfer);
            KeAcquireSpinLock(&Endpoint->Lock, &OldIrql);
            Endpoint->ActiveTransfer = NULL;
            KeReleaseSpinLock(&Endpoint->Lock, OldIrql);
            return;
        }
        else if (EpState != XHCI_EPCTX_STATE_HALTED &&
                 EpState != XHCI_EPCTX_STATE_ERROR)
        {
            DPRINT1("usbxhci: AbortTransfer: slot %u ep %u has active transfer but EP state=%lu (Running), skipping spurious abort\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId,
                    EpState);
            return;
        }
        else
        {
            DPRINT1("usbxhci: AbortTransfer: slot %u ep %u EP state=%lu needs recovery, proceeding with abort\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId,
                    EpState);
        }
    }
    else
    {
        DPRINT1("usbxhci: AbortTransfer: slot %u ep %u CHECK2: NO ActiveTransfer, checking if reset needed\n",
                Endpoint->SlotId,
                Endpoint->EndpointId);
    }

    /*
     * Skip reset if there's no active transfer and the endpoint doesn't need recovery.
     * USBPORT may call AbortTransfer for orphan completion events (e.g., after
     * EP0 stall reset when the original transfer already completed, or when a
     * transfer times out after already completing on the hardware side).
     *
     * The endpoint reset sequence (Stop/Reset EP + Set TR Dequeue) is only valid
     * and necessary when:
     * 1. Endpoint is in Halted or Error state (recovery required), OR
     * 2. EP0 explicitly needs a stall reset (Ep0NeedsStallReset flag set), OR
     * 3. There IS an active transfer that we need to abort
     *
     * If the endpoint is in Running or Stopped state with no active transfer,
     * issuing Stop Endpoint or Set TR Dequeue can cause Context State Errors
     * (completion code 19) because the endpoint state may have changed since
     * we read it, or the endpoint is already idle.
     */
    if (!Endpoint->ActiveTransfer)
    {
        ULONG EpState = XHCI_EPCTX_STATE_DISABLED;
        BOOLEAN NeedsReset = FALSE;

        /* Check if EP0 needs explicit stall reset */
        if (Endpoint->DefaultControl &&
            Endpoint->Slot &&
            Endpoint->Slot->Ep0NeedsStallReset)
        {
            DPRINT1("usbxhci: AbortTransfer: slot %u ep %u CHECK3: Ep0NeedsStallReset=TRUE\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId);
            NeedsReset = TRUE;
        }

        /* Read endpoint state from device context */
        if (Endpoint->Slot && Endpoint->Slot->DeviceContext.VirtualAddress &&
            Endpoint->EndpointId != 0)
        {
            PXHCI_ENDPOINT_CONTEXT EpCtx =
                XHCI_GetDeviceEndpointContextVa(Extension,
                                                Endpoint->Slot->DeviceContext.VirtualAddress,
                                                Endpoint->EndpointId - 1);
            if (EpCtx)
                EpState = EpCtx->EpInfo & XHCI_EPCTX_STATE_MASK;
        }

        DPRINT1("usbxhci: AbortTransfer: slot %u ep %u CHECK3: EP state=%lu NeedsReset=%u\n",
                Endpoint->SlotId,
                Endpoint->EndpointId,
                EpState,
                NeedsReset);

        /* Only reset if endpoint is in a state requiring recovery */
        if (EpState == XHCI_EPCTX_STATE_HALTED ||
            EpState == XHCI_EPCTX_STATE_ERROR)
        {
            DPRINT1("usbxhci: AbortTransfer: slot %u ep %u CHECK3: EP state requires recovery\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId);
            NeedsReset = TRUE;
        }

        /*
         * If no reset is needed (endpoint is Running/Stopped/Disabled and no
         * stall reset flag), skip the reset sequence entirely. This prevents
         * issuing commands when the endpoint is idle or already recovered.
         */
        if (!NeedsReset)
        {
            DPRINT1("usbxhci: AbortTransfer: slot %u ep %u no active transfer, state=%lu, skipping reset\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId,
                    EpState);
            return;
        }
        else
        {
            DPRINT1("usbxhci: AbortTransfer: slot %u ep %u CHECK3: NeedsReset=TRUE, proceeding\n",
                    Endpoint->SlotId,
                    Endpoint->EndpointId);
        }
    }

    DPRINT1("usbxhci: AbortTransfer: slot %u ep %u PROCEEDING with reset (IRQL=%lu)\n",
            Endpoint->SlotId,
            Endpoint->EndpointId,
            (ULONG)KeGetCurrentIrql());

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
            Work->ClearStallResetFlags = FALSE;
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

    if (!Extension || Extension->FatalError || Extension->StoppingOrRemoved)
        return;

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

    /*
     * Do NOT issue SET_TR_DEQUEUE here.  USBPORT calls SetEndpointDataToggle
     * as part of the clear-stall sequence BEFORE calling SetEndpointStatus.
     * At this point the endpoint is typically Halted (the device stalled),
     * which is NOT a valid state for SET_TR_DEQUEUE (requires Stopped).
     * Issuing it here causes CONTEXT_STATE_ERROR (code 19) from the xHC.
     *
     * The subsequent SetEndpointStatus call performs the full reset sequence:
     *   RESET_ENDPOINT (Halted->Stopped) -> SET_TR_DEQUEUE -> doorbell
     * which correctly handles the endpoint state transitions.
     */
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

    /*
     * Clear-stall path: reset endpoint state and re-sync dequeue.
     *
     * Always perform the reset synchronously, even at DISPATCH_LEVEL.
     * XHCI_SendCommand handles DISPATCH_LEVEL with bounded busy-polling
     * (RESET_EP/SET_TR_DEQUEUE: max 50ms each, typically < 1ms on real HW).
     *
     * Previously this path queued async work at DISPATCH_LEVEL, but that
     * caused a race: SetEndpointStatus returned immediately, USBPORT/USBSTOR
     * submitted new transfers, and the async reset worker then wiped the
     * ring (destroying the new TRBs) and NULLed ActiveTransfer, causing
     * "has no active transfer" stalls.
     */
    DPRINT1("usbxhci: SetEndpointStatus synchronous reset for slot %u ep %u (IRQL=%lu)\n",
            Endpoint->SlotId,
            Endpoint->EndpointId,
            (ULONG)KeGetCurrentIrql());
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
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;

    XHCI_DBG(XHCI_TRACE_EVENTS,
             "usbxhci: FlushInterrupts (IRQL=%lu)\n",
             (ULONG)KeGetCurrentIrql());

    if (!Extension || !Extension->OperationalRegisters || Extension->FatalError)
        return;

    if (XHCI_EventRingHasPendingTrb(Extension))
    {
        XHCI_ServiceEventRing(Extension, FALSE, FALSE);
    }

    /* Scan for port status changes that may have been missed */
    XHCI_ScanPortStatusChanges(Extension, FALSE);
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
    ULONG StsAck;
    PXHCI_INTERRUPTER_REGISTER_SET Interrupter;

    if (!Extension || !Extension->OperationalRegisters)
        return;

    /* Clear INTE in USBCMD to globally disable xHCI interrupts */
    Command = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd);
    Command &= ~XHCI_USBCMD_INTE;
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd, Command);
    Extension->InterruptsEnabled = FALSE;

    /*
     * Clear all pending USBSTS write-to-clear bits. This deasserts the
     * interrupt at the controller level. Without this, the controller may
     * keep its interrupt output asserted, and emulators like QEMU that
     * assert INTx alongside MSI will cause an interrupt storm on the
     * unhandled legacy vector.
     */
    StsAck = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts);
    StsAck &= (XHCI_USBSTS_EINT | XHCI_USBSTS_PCD |
                XHCI_USBSTS_HSE | XHCI_USBSTS_HCE);
    if (StsAck)
    {
        XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts, StsAck);
    }

    if (Extension->RuntimeRegisters)
    {
        Interrupter = &Extension->RuntimeRegisters->Interrupter[0];
        /*
         * Clear IE to disable interrupter. Also write IP=1 (RW1C) to clear
         * any pending interrupt. Only write the defined bits, IE=0 disables.
         */
        XHCI_WRITE_REGISTER_ULONG(&Interrupter->Iman, XHCI_IMAN_IP);
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

    Status = XHCI_READ_REGISTER_ULONG(UsbSts);
    Command = XHCI_READ_REGISTER_ULONG(UsbCmd);

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
    XHCI_WRITE_REGISTER_ULONG(UsbCmd, Command);

    if (!XHCI_WaitForRegisterBits(UsbSts,
                                  XHCI_USBSTS_HCH,
                                  FALSE,
                                  XHCI_WAIT_HALT_US))
    {
        DPRINT1("usbxhci: controller failed to exit halt state\n");
        return MP_STATUS_HW_ERROR;
    }

    Status = XHCI_READ_REGISTER_ULONG(UsbSts);
#if DBG
    Command = XHCI_READ_REGISTER_ULONG(UsbCmd);
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

    Command = XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd);
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
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->UsbCmd, Command);

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

    XHCI_SuspendPorts(Extension);
    XHCI_DisableInterrupts(Extension);
    XHCI_HaltController(Extension, XHCI_WAIT_HALT_US);
}

static
VOID
XHCI_ReprogramControllerState(
    _Inout_ PXHCI_EXTENSION Extension)
{
    ULONGLONG Dcbaa;
    ULONGLONG Crcr;
    ULONG DcbaaLow, DcbaaHigh;
    ULONG CrcrLow, CrcrHigh;

    if (!Extension || !Extension->OperationalRegisters)
        return;

    /*
     * After D3 resume, the xHCI controller may have lost all register state.
     * Reprogram DCBAA, CRCR, and CONFIG registers to restore communication
     * with device contexts and the command ring.
     */
    Dcbaa = Extension->DcbaaPhysical.QuadPart;
    Crcr = Extension->CommandRingPhysical.QuadPart & ~0x3FULL;

    DcbaaLow = (ULONG)(Dcbaa & 0xFFFFFFFF);
    DcbaaHigh = (ULONG)(Dcbaa >> 32);
    CrcrLow = (ULONG)(Crcr & 0xFFFFFFFF);
    CrcrHigh = (ULONG)(Crcr >> 32);

    /* Include current cycle state in CRCR */
    CrcrLow |= (Extension->CommandRingCycleState & 0x1);

    DPRINT1("usbxhci: resume reprogramming DCBAA=%08lx:%08lx CRCR=%08lx:%08lx\n",
            DcbaaHigh, DcbaaLow, CrcrHigh, CrcrLow);

    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->DcbaapLow, DcbaaLow);
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->DcbaapHigh, DcbaaHigh);
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->CrCrLow, CrcrLow);
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->CrCrHigh, CrcrHigh);
    XHCI_WRITE_REGISTER_ULONG(&Extension->OperationalRegisters->Config, Extension->MaxSlots);
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
     * On resume from D3, the xHCI controller loses all register state.
     * The PCI bus driver may also have restored default PCI config state,
     * clearing the INTx Disable bit. Re-establish bus mastering and INTx
     * disable before reprogramming controller registers.
     */
    XHCI_EnablePciBusMaster(Extension);
    if (Extension->MsixEnabled || Extension->MsiEnabled)
        XHCI_DisablePciIntx(Extension);

    XHCI_ReprogramControllerState(Extension);
    XHCI_ProgramInterrupterState(Extension);


    XHCI_EnableInterrupts(Extension);
    DPRINT1("usbxhci: First RunController attempt\n");

    KeStallExecutionProcessor(10000); // 10ms delay
    DPRINT1("usbxhci: About to Run Controller (Retry)\n");
    Status = XHCI_RunController(Extension);
    DPRINT1("usbxhci: RunController ret 0x%x STS=0x%x\n", Status, XHCI_READ_REGISTER_ULONG(&Extension->OperationalRegisters->UsbSts));
    if (Status != MP_STATUS_SUCCESS)
        return Status;

    XHCI_ServiceEventRing(Extension, TRUE, TRUE);
    XHCI_ResumePorts(Extension);

    return MP_STATUS_SUCCESS;
}

static
VOID
NTAPI
XHCI_RH_GetRootHubData(
    _In_ PVOID MiniPortExtension,
    _In_ PVOID RootHubData)
{
    DPRINT("XHCI_RH_GetRootHubData: Called\n");
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
        Hcs1 = XHCI_READ_REGISTER_ULONG(&Extension->CapabilityRegisters->HcsParams1);
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
    }
    else
    {
        PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_POWER;
    }

    Speed = (PortValue & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;

    /*
     * For USB 3.0 ports (Protocol >= 3), we set NegotiatedDeviceSpeed in the
     * USB 3.0 port status format. The hub driver checks this field first to
     * detect SuperSpeed devices.
     *
     * Note: USB_PORT_STATUS is a union where USB 2.0 and USB 3.0 fields overlap:
     * - USB 3.0 NegotiatedDeviceSpeed (bits 10-12) overlaps USB 2.0 HighSpeed (bit 10)
     * - USB 3.0 PortPower (bit 9) overlaps USB 2.0 LowSpeed (bit 9)
     *
     * For USB 3.0 ports, we ONLY set NegotiatedDeviceSpeed (not USB 2.0 speed bits)
     * because the hub driver now checks NegotiatedDeviceSpeed >= 4 for SuperSpeed.
     * For USB 2.0 ports, we use the traditional speed bits.
     */
    if (Protocol >= 3)
    {
        /* USB 3.0 port: set NegotiatedDeviceSpeed (bits 10-12) */
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
    else
    {
        /* USB 2.0 port: set traditional speed bits */
        if (Speed == XHCI_PORTSC_SPEED_LOW)
            PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_LOW_SPEED;
        else if (Speed == XHCI_PORTSC_SPEED_HIGH)
            PortStatus->PortStatus.AsUshort16 |= USB_PORT_STATUS_HIGH_SPEED;
    }

    if ((PortValue & XHCI_PORTSC_CSC) || (CurrentConnect != PreviousConnect))
    {
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

    PortValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);
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
            PortValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);
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

    /*
     * Trace port status for debugging - only log when there's an actual change
     * to avoid spamming the log. Using DPRINT instead of DPRINT1 to reduce
     * hot path verbosity (GetPortStatus is called frequently during enumeration).
     */
    if (PortStatus->PortChange.AsUshort16 != 0)
    {
        DPRINT("usbxhci: RH_GetPortStatus port=%u PortSC=0x%08lx (CCS=%u PED=%u Speed=%lu) Status=0x%04x Change=0x%04x\n",
                Port,
                PortValue,
                (PortValue & XHCI_PORTSC_CCS) ? 1 : 0,
                (PortValue & XHCI_PORTSC_PED) ? 1 : 0,
                (PortValue & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT,
                PortStatus->PortStatus.AsUshort16,
                PortStatus->PortChange.AsUshort16);
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
    DPRINT("XHCI_RH_SetFeaturePortPower: Port=%u\n", Port);
    PXHCI_EXTENSION Extension = MiniPortExtension;

    if (!Extension)
        return MP_STATUS_ERROR;

    /* On controllers without per-port power switching, treat this as a no-op. */
    if (!Extension->PortPowerControl)
        return MP_STATUS_SUCCESS;

    /* Check ACPI _OSC policy - firmware may control port power */
    if (!XHCI_ShouldControlPortPower(Extension))
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

    /* Check ACPI _OSC policy - firmware may control port power */
    if (!XHCI_ShouldControlPortPower(Extension))
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
    volatile ULONG *PortStatusReg;
    ULONG PortValue;

    if (!Extension)
        return MP_STATUS_ERROR;

    PortStatusReg = XHCI_GetPortStatusRegister(Extension, Port);
    if (!PortStatusReg)
        return MP_STATUS_ERROR;

    PortValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);
    DPRINT("XHCI_RH_SetFeaturePortReset: Port=%u PORTSC=%08lx (CCS=%u PED=%u Speed=%lu)\n",
            Port,
            PortValue,
            (PortValue & XHCI_PORTSC_CCS) ? 1 : 0,
            (PortValue & XHCI_PORTSC_PED) ? 1 : 0,
            (PortValue & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT);

    /* SuperSpeed ports use Warm Port Reset (WPR) */
    if (XHCI_PortIsSuperSpeed(Extension, Port))
    {
        MPSTATUS Status;
        ULONG PostValue;
        ULONG WaitAttempts;
        BOOLEAN ResetComplete = FALSE;

        DPRINT("XHCI: Port %u is SuperSpeed, using Warm Port Reset (WPR)\n", Port);

        /*
         * Issue RESET_DEVICE before warm port reset if the slot is in
         * Addressed/Configured state.  A warm reset puts the USB device
         * back to Default state; the xHC slot must be transitioned to
         * Default as well, otherwise the next OpenEndpoint will skip
         * Address Device and transfers will fail (endpoint halts
         * immediately because the device expects re-addressing).
         *
         * This mirrors the logic already present in the USB 2.0 standard
         * port reset path below.
         */
        {
            PXHCI_DEVICE_SLOT Slot = XHCI_FindSlotByPort(Extension, Port);
            if (Slot && Slot->Addressed)
            {
                ULONG ResetCompletionCode = 0;
                MPSTATUS ResetStatus;

                DPRINT("XHCI: SS Port %u has slot %u in Addressed state, issuing RESET_DEVICE before WPR\n",
                        Port, Slot->SlotId);

                ResetStatus = XHCI_SendCommand(Extension,
                                               XHCI_TRB_TYPE_RESET_DEV,
                                               0,
                                               0,
                                               XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                                               XHCI_COMMAND_TIMEOUT_MS,
                                               TRUE,
                                               NULL,
                                               &ResetCompletionCode);

                if (ResetStatus == MP_STATUS_SUCCESS)
                {
                    Slot->Addressed = FALSE;
                    DPRINT("XHCI: SS RESET_DEVICE succeeded for slot %u, now in Default state\n",
                            Slot->SlotId);
                }
                else
                {
                    DPRINT1("XHCI: SS RESET_DEVICE failed for slot %u (Status=%ld Code=%lu) - continuing with WPR\n",
                            Slot->SlotId, ResetStatus, ResetCompletionCode);
                }
            }
        }

        Status = XHCI_ModifyPortBits(Extension, Port, XHCI_PORTSC_WPR, 0, 0);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        /*
         * Wait for Warm Port Reset completion. Per xHCI spec 4.19.5.1, warm
         * reset puts the link through all link states (Rx.Detect, Polling,
         * Training, then U0). The controller sets WRC (Warm Reset Change)
         * when the warm reset completes.
         *
         * USB 3.0 spec requires up to 100ms for warm reset completion (tRESET),
         * plus additional time for link training. We use 500ms total timeout.
         */
        #define SS_RESET_TIMEOUT_ATTEMPTS 2500
        #define SS_RESET_POLL_US          200

        for (WaitAttempts = 0; WaitAttempts < SS_RESET_TIMEOUT_ATTEMPTS; WaitAttempts++)
        {
            KeStallExecutionProcessor(SS_RESET_POLL_US);

            PostValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);

            /* Check if device disconnected during reset */
            if (!(PostValue & XHCI_PORTSC_CCS))
            {
                DPRINT1("XHCI: Port %u (SS) device disconnected during warm reset\n", Port);
                return MP_STATUS_ERROR;
            }

            /*
             * Warm reset is complete when:
             * - WRC (Warm Reset Change) is set, AND
             * - PED (Port Enabled) is set (link is up)
             *
             * Note: PRC may also be set, but WRC is the definitive indicator
             * for warm reset completion on SuperSpeed ports.
             */
            if ((PostValue & XHCI_PORTSC_WRC) && (PostValue & XHCI_PORTSC_PED))
            {
                ResetComplete = TRUE;
                DPRINT("XHCI: Port %u (SS) warm reset complete after %lu us: PORTSC=0x%08lx (PED=1 WRC=1)\n",
                        Port, (WaitAttempts + 1) * SS_RESET_POLL_US, PostValue);
                break;
            }

            /*
             * If WRC is set but PED is not, the warm reset completed but
             * link training failed. This could indicate a device that doesn't
             * support SuperSpeed or a connection issue.
             */
            if (PostValue & XHCI_PORTSC_WRC)
            {
                ULONG LinkState = (PostValue & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;
                DPRINT("XHCI: Port %u (SS) WRC set but PED=0, PLS=%lu - continuing to wait\n",
                        Port, LinkState);
                /* Continue waiting - link training may still be in progress */
            }
        }

        PostValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);

        if (!ResetComplete)
        {
            ULONG LinkState = (PostValue & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;
            DPRINT1("XHCI: Port %u (SS) warm reset timeout: PORTSC=0x%08lx (CCS=%u PED=%u WRC=%u PLS=%lu)\n",
                    Port,
                    PostValue,
                    (PostValue & XHCI_PORTSC_CCS) ? 1 : 0,
                    (PostValue & XHCI_PORTSC_PED) ? 1 : 0,
                    (PostValue & XHCI_PORTSC_WRC) ? 1 : 0,
                    LinkState);
        }

        /* Latch change bits for hub driver polling */
        if (Port >= 1 && Port <= XHCI_MAX_PORTS)
        {
            ULONG ChangeBits = 0;
            if (PostValue & XHCI_PORTSC_PRC)
                ChangeBits |= XHCI_PORTSC_PRC;
            if (PostValue & XHCI_PORTSC_WRC)
                ChangeBits |= XHCI_PORTSC_WRC;

            if (ChangeBits)
            {
                InterlockedOr((volatile LONG *)&Extension->PortChangeMask[Port], ChangeBits);
            }
        }

        return ResetComplete ? MP_STATUS_SUCCESS : MP_STATUS_ERROR;
    }

    /* Standard Behavior: Set PR (Port Reset) and wait for completion */
    DPRINT("XHCI: Port %u standard reset - writing PORTSC_PR\n", Port);

    {
        MPSTATUS Status;
        ULONG PostValue;
        ULONG WaitAttempts;
        BOOLEAN ResetComplete = FALSE;
        PXHCI_DEVICE_SLOT Slot;

        /*
         * Per xHCI spec section 4.6.11 (Reset Device) and Linux kernel xhci.c
         * xhci_discover_or_reset_device(): when a USB bus reset occurs on a
         * port that has an associated slot in Addressed or Configured state,
         * the xHC must be notified via RESET_DEVICE command to transition
         * the slot back to Default state.
         *
         * On Intel Alder Lake-N (8086:464e, 8086:54ed), if we perform a USB
         * bus reset while the slot is still in Addressed state, the second
         * port reset (issued by usbhub after device addressing) fails with
         * PED=0 and PLS=7 (Polling) - the port never transitions to Enabled.
         *
         * The fix is to issue RESET_DEVICE BEFORE the port reset when there's
         * an associated slot in Addressed state. This puts the slot back into
         * Default state, allowing the USB bus reset to complete normally.
         *
         * Note: We skip RESET_DEVICE for VirtualBox and for slots already in
         * Default state.
         */
        if (!(Extension->Quirks & XHCI_QUIRK_VBOX_POLL_XFERS))
        {
            Slot = XHCI_FindSlotByPort(Extension, Port);
            if (Slot && Slot->Addressed)
            {
                ULONG ResetCompletionCode = 0;
                MPSTATUS ResetStatus;

                DPRINT("XHCI: Port %u has slot %u in Addressed state, issuing RESET_DEVICE before port reset\n",
                        Port, Slot->SlotId);

                ResetStatus = XHCI_SendCommand(Extension,
                                               XHCI_TRB_TYPE_RESET_DEV,
                                               0,
                                               0,
                                               XHCI_COMMAND_SLOT_FIELD(Slot->SlotId),
                                               XHCI_COMMAND_TIMEOUT_MS,
                                               TRUE,
                                               NULL,
                                               &ResetCompletionCode);

                if (ResetStatus == MP_STATUS_SUCCESS)
                {
                    /*
                     * RESET_DEVICE succeeded - slot is now in Default state.
                     * Mark it as not addressed so we don't issue redundant
                     * RESET_DEVICE commands later.
                     */
                    Slot->Addressed = FALSE;
                    DPRINT("XHCI: RESET_DEVICE succeeded for slot %u, now in Default state\n",
                            Slot->SlotId);
                }
                else
                {
                    /*
                     * RESET_DEVICE failed. Per xHCI spec, this can happen if
                     * the slot is not in Addressed/Configured state (e.g.,
                     * already in Default state due to hardware auto-reset).
                     * Log but continue with port reset - it may still work.
                     */
                    DPRINT1("XHCI: RESET_DEVICE failed for slot %u (Status=%ld Code=%lu) - continuing with port reset\n",
                            Slot->SlotId, ResetStatus, ResetCompletionCode);
                }
            }
        }

        /*
         * Initiate port reset by writing PR bit. Per xHCI spec 4.19.4, for USB 2.0
         * devices the controller drives reset signaling for 50ms, then transitions
         * the port from Polling to Enabled state, setting PED=1 and PRC=1.
         *
         * On real Intel hardware (Alder Lake-N), we must wait for PRC and PED to
         * ensure the reset completes before ADDRESS_DEVICE. Without this wait,
         * ADDRESS_DEVICE fails with USB_TRANSACTION_ERROR because the port is
         * still in Polling state.
         */
        Status = XHCI_ModifyPortBits(Extension, Port, XHCI_PORTSC_PR, 0, 0);
        if (Status != MP_STATUS_SUCCESS)
            return Status;

        /*
         * Poll for reset completion. USB 2.0 spec requires 10ms reset signaling,
         * but xHCI typically uses 50ms for USB 2.0 ports. Add margin for device
         * recovery time (TRSTRCY). Total timeout: 200ms should be plenty.
         * Poll every 1ms (1000 attempts at 200us each = 200ms total).
         */
        #define PORT_RESET_TIMEOUT_ATTEMPTS 1000
        #define PORT_RESET_POLL_US          200

        for (WaitAttempts = 0; WaitAttempts < PORT_RESET_TIMEOUT_ATTEMPTS; WaitAttempts++)
        {
            KeStallExecutionProcessor(PORT_RESET_POLL_US);

            PostValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);

            /* Check if device disconnected during reset */
            if (!(PostValue & XHCI_PORTSC_CCS))
            {
                DPRINT1("XHCI: Port %u device disconnected during reset\n", Port);
                return MP_STATUS_ERROR;
            }

            /*
             * Reset is complete when:
             * - PR bit clears (reset signaling done), AND
             * - PRC bit is set (Port Reset Change), AND
             * - PED bit is set (Port Enabled) for USB 2.0 devices
             *
             * For USB 2.0 devices behind xHCI, successful reset always results
             * in PED=1. If PED remains 0 after reset, the device failed to
             * enumerate at this speed.
             */
            if (!(PostValue & XHCI_PORTSC_PR))
            {
                /* PR cleared - reset signaling is done */
                if (PostValue & XHCI_PORTSC_PRC)
                {
                    /* PRC set - controller acknowledged reset completion */
                    if (PostValue & XHCI_PORTSC_PED)
                    {
                        /* PED set - port is enabled, reset successful */
                        ResetComplete = TRUE;
                        DPRINT("XHCI: Port %u reset complete after %lu us: PORTSC=0x%08lx (PED=1 PRC=1)\n",
                                Port, (WaitAttempts + 1) * PORT_RESET_POLL_US, PostValue);
                        break;
                    }
                    else
                    {
                        /*
                         * PRC set but PED=0: Reset signaling completed but port
                         * did not enable. This can happen if:
                         * 1. Device doesn't support the current speed
                         * 2. Cable/connection issues
                         * 3. Device needs more reset time
                         *
                         * On Intel Alder Lake-N, USB 2.0 devices may need the
                         * link to transition out of Polling state. Check PLS.
                         */
                        ULONG LinkState = (PostValue & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;

                        if (LinkState == 7) /* Still in Polling */
                        {
                            /* Continue waiting - device may still be negotiating */
                            continue;
                        }
                        else
                        {
                            DPRINT1("XHCI: Port %u reset done but PED=0 (PLS=%lu): PORTSC=0x%08lx\n",
                                    Port, LinkState, PostValue);
                            /* Continue to wait a bit more */
                        }
                    }
                }
            }
        }

        /*
         * Log final port state and latch the change bits so the hub driver sees
         * ResetChange when it polls GetPortStatus.
         */
        PostValue = XHCI_READ_REGISTER_ULONG(PortStatusReg);

        if (!ResetComplete)
        {
            ULONG LinkState = (PostValue & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;
            DPRINT1("XHCI: Port %u reset timeout after %lu us: PORTSC=0x%08lx (CCS=%u PED=%u PR=%u PRC=%u PLS=%lu)\n",
                    Port,
                    PORT_RESET_TIMEOUT_ATTEMPTS * PORT_RESET_POLL_US,
                    PostValue,
                    (PostValue & XHCI_PORTSC_CCS) ? 1 : 0,
                    (PostValue & XHCI_PORTSC_PED) ? 1 : 0,
                    (PostValue & XHCI_PORTSC_PR) ? 1 : 0,
                    (PostValue & XHCI_PORTSC_PRC) ? 1 : 0,
                    LinkState);

            /*
             * Even on timeout, latch PRC if present so hub driver can clear it.
             * Return success to allow hub layer to poll and potentially retry.
             */
        }

        /* Latch change bits for hub driver polling */
        if ((PostValue & XHCI_PORTSC_PRC) && Port >= 1 && Port <= XHCI_MAX_PORTS)
        {
            InterlockedOr((volatile LONG *)&Extension->PortChangeMask[Port],
                          XHCI_PORTSC_PRC);
        }

        return MP_STATUS_SUCCESS;
    }
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

    /* Arm port wake events before entering U3 so remote wake propagates. */
    XHCI_SetPortWakeBits(Extension, Port, TRUE);

    /*
     * Enter U3 (suspend) on the target port. The controller will signal
     * a port-status-change event when the link transitions into or out
     * of U3 so USBPORT can observe Suspend/SuspendChange via
     * XHCI_RH_GetPortStatus and generate wake notifications.
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
    DPRINT("XHCI_RH_ClearFeaturePortConnectChange: Port=%u\n", Port);
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
    PXHCI_EXTENSION Extension = (PXHCI_EXTENSION)MiniPortExtension;
    PXHCI_DEVICE_SLOT Slot;

    DPRINT("XHCI_RH_ClearFeaturePortResetChange: Port=%u\n", Port);
    XHCI_RH_AckPortChange(Extension, Port,
                          XHCI_PORTSC_PRC | XHCI_PORTSC_WRC);

    /*
     * Handle spontaneous device resets.
     *
     * When a USB device spontaneously resets (e.g., firmware crash, power
     * glitch), the hub driver detects PRC (Port Reset Change) and calls
     * ClearFeaturePortResetChange to acknowledge it. At this point, the
     * xHCI slot is still in Configured state, but the physical USB device
     * has reverted to Default state (address 0).
     *
     * We MUST call XHCI_ResetDeviceOnPort to issue the xHCI RESET_DEVICE
     * command, which puts the slot back into Default state to match the
     * physical device. Without this, the slot stays in Configured/Addressed
     * state while the device is at address 0, causing TRANSACTION_ERROR on
     * the next I/O attempt.
     *
     * We ONLY do this when the slot is in Configured state, which indicates
     * the device was previously fully set up (i.e., this is a spontaneous
     * reset, not initial enumeration). During initial enumeration,
     * XHCI_RH_SetFeaturePortReset already issues RESET_DEVICE inline before
     * the port reset, so calling it again here would be redundant and could
     * race with ADDRESS_DEVICE that the enumeration worker is performing.
     */
    Slot = XHCI_FindSlotByPort(Extension, Port);
    if (Slot && Slot->Configured)
    {
        DPRINT1("XHCI_RH_ClearFeaturePortResetChange: Port %u slot %u is Configured, issuing RESET_DEVICE for spontaneous reset\n",
                Port, Slot->SlotId);
        XHCI_ResetDeviceOnPort(Extension, Port);
    }

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
    {
        XHCI_SetPortWakeBits(Extension, Port, FALSE);
        XHCI_RH_AckPortChange(Extension, Port, XHCI_PORTSC_PLC);
    }

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
    if (Extension->RhPendingInvalidate &&
        XhciRegPacket.UsbPortInvalidateRootHub)
    {
        Extension->RhPendingInvalidate = FALSE;
        XhciRegPacket.UsbPortInvalidateRootHub(Extension);
    }
}
// CHECK_STRING_12345
