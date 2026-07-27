/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <windef.h>
#include <usb100.h>
#include <usb200.h>
#include <usb.h>
#include <hubbusif.h>
#include <usbbusif.h>
#include <usbdlib.h>
#include <drivers/usbport/usbmport.h>

#include "hardware.h"

#define XHCI_TAG 'xhcu'

typedef struct _XHCI_ISO_PACKET_CONTEXT XHCI_ISO_PACKET_CONTEXT, *PXHCI_ISO_PACKET_CONTEXT;

#if DBG
#define XHCI_LOG_IRQL(Tag)                                                        \
    DPRINT("usbxhci[IRQL]: %s (IRQL=%lu)\n", Tag, (ULONG)KeGetCurrentIrql())
#define XHCI_ASSERT_PASSIVE(Tag)                                                  \
    do {                                                                          \
        KIRQL __irql = KeGetCurrentIrql();                                        \
        if (__irql > PASSIVE_LEVEL) {                                             \
            DPRINT1("usbxhci ASSERT: %s requires PASSIVE_LEVEL, current=%lu\n",     \
                    Tag, (ULONG)__irql);                                          \
        }                                                                         \
        ASSERT(__irql <= PASSIVE_LEVEL);                                          \
    } while (0)
#else
#define XHCI_LOG_IRQL(Tag) ((void)0)
#define XHCI_ASSERT_PASSIVE(Tag) ((void)0)
#endif

#define XHCI_QUIRK_FORCE_32BIT_DMA   0x00000001
#define XHCI_QUIRK_SLOW_HARD_RESET   0x00000002
#define XHCI_QUIRK_LEGACY_BIOS_HANDOFF 0x00000004
#define XHCI_QUIRK_NO_PORT_INDICATORS 0x00000008
#define XHCI_QUIRK_LIMIT_U1U2         0x00000010
#define XHCI_QUIRK_IGNORE_STARTUP_HCE 0x00000020
#define XHCI_QUIRK_QEMU_CONFIG_EP_ORDER 0x00000040
#define XHCI_QUIRK_NON_COHERENT_DMA   0x00000080
#define XHCI_QUIRK_VBOX_PORT_RESET    0x00000100
#define XHCI_QUIRK_VBOX_SPURIOUS_IMAN 0x00000200
#define XHCI_QUIRK_QEMU_PORT_RESET    0x00000400
#define XHCI_QUIRK_VBOX_POLL_XFERS    0x00000800
#define XHCI_QUIRK_QEMU_POLL_XFERS    0x00001000
#define XHCI_QUIRK_IS_QEMU_XHCI       0x00002000

/* Mask of quirks that force transfer polling for the fallback event drain.
 * Board-specific controllers must request polling via a registry quirk
 * override, not a hardcoded VID/DID match in this generic driver. */
#define XHCI_QUIRK_POLL_XFERS_MASK    (XHCI_QUIRK_VBOX_POLL_XFERS | \
                                       XHCI_QUIRK_QEMU_POLL_XFERS)
#define XHCI_BOUNCE_POOL_SLOTS 32
#define XHCI_BOUNCE_BUFFER_SIZE 0x10000

typedef struct DECLSPEC_ALIGN(PAGE_SIZE) _XHCI_SCRATCHPAD_PAGE {
    UCHAR Buffer[PAGE_SIZE];
} XHCI_SCRATCHPAD_PAGE, *PXHCI_SCRATCHPAD_PAGE;
C_ASSERT(sizeof(XHCI_SCRATCHPAD_PAGE) == PAGE_SIZE);

typedef struct DECLSPEC_ALIGN(PAGE_SIZE) _XHCI_HC_RESOURCES {
    ULONGLONG Dcbaa[XHCI_MAX_SLOTS + 1];
    ULONGLONG ScratchpadPointerArray[XHCI_MAX_SCRATCHPADS];
    XHCI_SCRATCHPAD_PAGE ScratchpadBuffers[XHCI_MAX_SCRATCHPADS];
    XHCI_TRB CommandRing[XHCI_COMMAND_RING_TRBS];
    XHCI_TRB EventRing[XHCI_EVENT_RING_TRBS];
    XHCI_ERST_ENTRY ErstEntries[XHCI_ERST_MAX_ENTRIES];
    XHCI_DEVICE_CONTEXT DeviceContexts[XHCI_MAX_SLOTS + 1];
    XHCI_INPUT_CONTEXT InputContexts[XHCI_MAX_SLOTS + 1];
    XHCI_TRB Ep0TransferRings[XHCI_MAX_SLOTS + 1][XHCI_STATIC_EP_RING_TRBS];
} XHCI_HC_RESOURCES, *PXHCI_HC_RESOURCES;

typedef struct _XHCI_CONTEXT_ALLOCATION {
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    SIZE_T Length;
} XHCI_CONTEXT_ALLOCATION, *PXHCI_CONTEXT_ALLOCATION;

typedef struct _XHCI_RING {
    PXHCI_TRB Base;
    PHYSICAL_ADDRESS PhysicalAddress;
    SIZE_T Length;
    ULONG TrbCount;
    ULONG EnqueueIndex;
    ULONG DequeueIndex;
    ULONG CycleState;
    BOOLEAN UsesCommonBuffer;
} XHCI_RING, *PXHCI_RING;

struct _XHCI_ENDPOINT;
typedef struct _XHCI_ENDPOINT XHCI_ENDPOINT, *PXHCI_ENDPOINT;

typedef struct _XHCI_DEVICE_SLOT {
    XHCI_CONTEXT_ALLOCATION DeviceContext;
    XHCI_CONTEXT_ALLOCATION InputContext;
    XHCI_CONTEXT_ALLOCATION Ep0TransferRing;
    ULONG Ep0RingCycleState;
    ULONG Ep0RingEnqueueIndex;
    ULONG Ep0RingDequeueIndex;
    UCHAR SlotId;
    BOOLEAN InUse;
    BOOLEAN Addressed;
    BOOLEAN Configured;
    BOOLEAN PortResetSinceAddress;
    BOOLEAN DisablePending;
    volatile LONG DetachPending;
    ULONG Ep0ContextErrorCount;
    ULONG Ep0TransactionErrorCount;
    UCHAR UsbDeviceAddress;
    UCHAR PortNumber;
    ULONG RouteString;
    UCHAR HighestEndpointId;
    PXHCI_ENDPOINT EndpointTable[XHCI_MAX_ENDPOINTS + 1];
    PXHCI_ENDPOINT DeferredEndpointTable[XHCI_MAX_ENDPOINTS + 1];
    USHORT HubAddress;
    USB_DEVICE_SPEED DeviceSpeed;
    UCHAR HubPortCount;
    USHORT MaxExitLatency;
    UCHAR TtThinkTime;
    BOOLEAN MultiTt;
    BOOLEAN HasTtInfo;
    BOOLEAN IsHub;
    volatile LONG Ep0NeedsDequeueReset;
    volatile LONG Ep0NeedsStallReset;    /* Set on stall, cleared by next submit at PASSIVE */
    volatile LONG Ep0StallResetQueued;   /* Prevent duplicate stall-reset workers */
    KEVENT Ep0StallResetEvent;
} XHCI_DEVICE_SLOT, *PXHCI_DEVICE_SLOT;

typedef struct _XHCI_PROTOCOL_SEGMENT {
    UCHAR MajorRevision;
    UCHAR MinorRevision;
    UCHAR PortOffset;
    UCHAR PortCount;
} XHCI_PROTOCOL_SEGMENT, *PXHCI_PROTOCOL_SEGMENT;

#define XHCI_MAX_PROTOCOL_SEGMENTS 8

typedef struct _XHCI_EXTENSION {
  ULONG Signature;
  PDEVICE_OBJECT FunctionalDeviceObject;
    PVOID MmioBase;
    PXHCI_CAPABILITY_REGISTERS CapabilityRegisters;
    PXHCI_OPERATIONAL_REGISTERS OperationalRegisters;
    PXHCI_RUNTIME_REGISTERS RuntimeRegisters;
    PXHCI_DOORBELL_ARRAY DoorbellArray;
    ULONG CapabilityLength;
    PUSBPORT_RESOURCES Resources;
    ULONG MaxSlots;
    ULONG NumberOfPorts;
  ULONG MaxScratchpadBuffers;
  ULONG ContextSize;
  BOOLEAN Supports64Bit;
    BOOLEAN PortPowerControl;
    BOOLEAN PortIndicatorsSupported;
  USHORT HciVersion;
  PXHCI_HC_RESOURCES HcResources;
  PHYSICAL_ADDRESS HcResourcesPhysical;
  SIZE_T CommonBufferSize;
    PULONGLONG Dcbaa;
    PHYSICAL_ADDRESS DcbaaPhysical;
    PULONGLONG ScratchpadPointerArray;
    PHYSICAL_ADDRESS ScratchpadArrayPhysical;
    PXHCI_SCRATCHPAD_PAGE ScratchpadBuffers;
    PHYSICAL_ADDRESS ScratchpadBuffersPhysical;
    ULONG ScratchpadCount;
    ULONG ConfiguredPageSize;
    PXHCI_TRB CommandRing;
    PHYSICAL_ADDRESS CommandRingPhysical;
    ULONG CommandRingTrbCount;
    ULONG CommandRingCycleState;
    ULONG CommandRingEnqueueIndex;
    PXHCI_TRB EventRing;
    PHYSICAL_ADDRESS EventRingPhysical;
    ULONG EventRingTrbCount;
    ULONG EventRingDequeueIndex;
    ULONG EventRingCycleState;
    UCHAR InterrupterCount;
    UCHAR ReservedInterrupt[3];
  PXHCI_ERST_ENTRY ErstTable;
  PHYSICAL_ADDRESS ErstTablePhysical;
  ULONG ErstEntryCount;
  ULONGLONG EventRingDequeuePointer;
  LIST_ENTRY CommandContextList;
  KSPIN_LOCK CommandLock;
  KSPIN_LOCK EventRingLock;
  LIST_ENTRY DeferredTransferList;
  KSPIN_LOCK DeferredTransferLock;
    PHYSICAL_ADDRESS DeviceContextsPhysical;
    PHYSICAL_ADDRESS InputContextsPhysical;
    PHYSICAL_ADDRESS Ep0RingArrayPhysical;
    PXHCI_DEVICE_CONTEXT DeviceContexts;
    PXHCI_INPUT_CONTEXT InputContexts;
    PXHCI_TRB Ep0TransferRings;
    XHCI_DEVICE_SLOT DeviceSlots[XHCI_MAX_SLOTS + 1];
    ULONG PendingUsbSts;
    BOOLEAN RhIrqEnabled;
    BOOLEAN RhPendingInvalidate;
  BOOLEAN InterruptsEnabled;
  BOOLEAN ControllerRunning;
  BOOLEAN FatalError;
  BOOLEAN StartupHcePersistent;
  ULONG Quirks;
    UCHAR DeviceAddressMap[XHCI_MAX_DEVICE_ADDRESS + 1];
  /* Port arrays use 1-based indexing: [0] unused, [1]..[XHCI_MAX_PORTS] used */
  UCHAR PortLinkState[XHCI_MAX_PORTS + 1];
  BOOLEAN PortConnectStatus[XHCI_MAX_PORTS + 1];
  ULONG PortChangeMask[XHCI_MAX_PORTS + 1];
  UCHAR MaxU1ExitLatency;
  USHORT MaxU2ExitLatency;
  UCHAR MaxPrimaryStreams;
  UCHAR ReservedStreamCaps[3];
  UCHAR ProtocolSegmentCount;
  XHCI_PROTOCOL_SEGMENT ProtocolSegments[XHCI_MAX_PROTOCOL_SEGMENTS];
  UCHAR PortProtocol[XHCI_MAX_PORTS + 1];
  PVOID AllocatedCommonBuffer;
  PHYSICAL_ADDRESS AllocatedCommonBufferPhysical;
  SIZE_T AllocatedCommonBufferSize;
  PVOID BounceBuffers[XHCI_BOUNCE_POOL_SLOTS];
  PHYSICAL_ADDRESS BounceBuffersPhysical[XHCI_BOUNCE_POOL_SLOTS];
  ULONG BounceBufferSize;
  ULONG BounceBuffersInUseMask;
  KSPIN_LOCK BounceBufferLock;
  /* MSI/MSI-X discovery (message interrupts not yet connected on ReactOS) */
  BOOLEAN MsiSupported;
    BOOLEAN MsixSupported;
    BOOLEAN MsiEnabled;
  BOOLEAN MsixEnabled;
  UCHAR MsiCapOffset;
  UCHAR MsixCapOffset;
  /* PnP/synchronization */
  volatile LONG Ep0WorkerCount;
  volatile LONG SwEnumWorkerCount;
  volatile LONG WorkerRundownCount;
  KEVENT WorkerRundownEvent;
  KSPIN_LOCK WorkerRundownLock;
  volatile LONG StoppingOrRemoved;
  KTIMER TransferPollTimer;
  KDPC TransferPollDpc;
  volatile LONG TransferPollCounter;
  volatile LONG TransferPollScheduled;
  KEVENT TransferPollDpcEvent;
  KSPIN_LOCK TransferPollLock;
  /* Frame number wrap tracking for Get32BitFrameNumber.
   * xHCI MFINDEX is only 14 bits and wraps every ~2 seconds.
   * USBPORT expects a monotonically increasing 32-bit value. */
  ULONG LastMfIndex;
  ULONG FrameHighBits;
  /* Debug counters */
  volatile ULONG IsrCallCount;
  /* TRUE if we raised the system clock resolution via ExSetTimerResolution
   * during StartController and must restore it on StopController. */
  BOOLEAN TimerResolutionRaised;
} XHCI_EXTENSION, *PXHCI_EXTENSION;

typedef struct _XHCI_COMMAND_CONTEXT {
  LIST_ENTRY ListEntry;
  volatile LONG References;
  KEVENT CompletionEvent;
  ULONGLONG CommandPointer;
  ULONG CommandType;
  ULONG CompletionCode;
  UCHAR SlotId;
  volatile LONG Completed;
  BOOLEAN InList;
} XHCI_COMMAND_CONTEXT, *PXHCI_COMMAND_CONTEXT;

typedef struct _XHCI_ENDPOINT {
    ULONG Signature;
    PXHCI_EXTENSION Extension;
    PXHCI_DEVICE_SLOT Slot;
    USBPORT_ENDPOINT_PROPERTIES EndpointProperties;
    XHCI_RING TransferRing;
    PUSBPORT_TRANSFER_PARAMETERS PendingParameters;
    PUSBPORT_SCATTER_GATHER_LIST PendingSgList;
    struct _XHCI_TRANSFER *ActiveTransfer;
    LIST_ENTRY ActiveTransferList;
    volatile LONG PendingWorkCount;
    volatile LONG ResetInProgress;
    /*
     * SwEnumRefCount: Miniport-owned reference count for SW-enum work items.
     * Incremented when queuing async work, decremented on completion.
     * ClosePipe waits until this reaches zero before freeing resources.
     */
    volatile LONG SwEnumRefCount;
    /*
     * Closing: Set by ClosePipe to prevent new work from being queued.
     * Once set, XHCI_ReferenceEndpointForSwEnum returns FALSE.
     */
    volatile LONG Closing;
    KSPIN_LOCK Lock;
    UCHAR SlotId;
    UCHAR EndpointId;
    UCHAR DoorbellTarget;
    UCHAR InterruptTarget;
    USHORT ReservedStreamId;
    USHORT MaxStreamId;
    BOOLEAN StreamsEnabled;
    UCHAR StreamReserved[1];
    PXHCI_STREAM_CONTEXT StreamContexts;
    PHYSICAL_ADDRESS StreamContextsPhysical;
    PXHCI_RING StreamRings;
    ULONG StreamRingCount;
    BOOLEAN DefaultControl;
    BOOLEAN UsesStaticRing;
    BOOLEAN Isochronous;
    ULONGLONG TotalBytesTransferred;
} XHCI_ENDPOINT, *PXHCI_ENDPOINT;

typedef struct _XHCI_TRANSFER {
    LIST_ENTRY ListEntry;
    LIST_ENTRY EndpointListEntry;
    PXHCI_ENDPOINT Endpoint;
    PUSBPORT_TRANSFER_PARAMETERS TransferParameters;
    PUSBPORT_SCATTER_GATHER_LIST SgList;
    PVOID TransferHandle;
    ULONGLONG TdFirstTrbPointer;
    ULONGLONG CompletionTrbPointer;
    ULONG RequestedLength;
    ULONG BytesTransferred;
    ULONG UsbdStatus;
    ULONG Flags;
    BOOLEAN IsControl;
    BOOLEAN IsIsochronous;
    PVOID IsoBlock;
    PXHCI_ISO_PACKET_CONTEXT IsoPacketContext;
    ULONG IsoPacketCount;
    ULONG IsoPacketsCompleted;
    ULONG IsoCompletedLength;
    USHORT StreamId;
    UCHAR NewAddress;
    UCHAR Reserved[2];
    PVOID BounceBuffer;
    PHYSICAL_ADDRESS BouncePhysicalAddress;
    ULONG BounceLength;
    LONG BounceSlot;
    BOOLEAN BounceDataIn;
    UCHAR BounceReserved[3];
} XHCI_TRANSFER, *PXHCI_TRANSFER;

#define XHCI_TRANSFER_FLAG_SET_ADDRESS   0x00000001
#define XHCI_TRANSFER_FLAG_GET_DESCRIPTOR 0x00000002
#define XHCI_TRANSFER_FLAG_NEEDS_POLL    0x00000004
#define XHCI_TRANSFER_FLAG_SWENUM_PENDING 0x00000008
#define XHCI_TRANSFER_FLAG_SWENUM_CANCELED 0x00000010
#define XHCI_TRANSFER_FLAG_SWENUM_DONE    0x00000020
#define XHCI_TRANSFER_FLAG_DATA_STAGE_DONE 0x00000040  /* BytesTransferred saved from Data Stage SHORT_PACKET */
#define XHCI_TRANSFER_FLAG_COMPLETED      0x00000080  /* Transfer already completed to USBPORT; prevents double-completion */
#define XHCI_TRANSFER_FLAG_NEEDS_POLL_BIT 2           /* Bit position for InterlockedBitTestAndReset */
#define XHCI_TRANSFER_FLAG_COMPLETED_BIT  7           /* Bit position for InterlockedBitTestAndSet */

BOOLEAN
XHCI_EndpointNeedsTt(
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties);

VOID
XHCI_ApplyTtInfo(
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
    _In_opt_ PXHCI_DEVICE_SLOT HubSlot,
    _Inout_ PXHCI_SLOT_CONTEXT SlotContext);

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath);
