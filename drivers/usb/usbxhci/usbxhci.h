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

#if DBG
#define XHCI_LOG_IRQL(Tag)                                                        \
    DPRINT1("usbxhci[IRQL]: %s (IRQL=%lu)\n", Tag, (ULONG)KeGetCurrentIrql())
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

#define XHCI_MAX_DEVICE_ADDRESS 256

#define XHCI_QUIRK_FORCE_32BIT_DMA   0x00000001
#define XHCI_QUIRK_SLOW_HARD_RESET   0x00000002
#define XHCI_QUIRK_LEGACY_BIOS_HANDOFF 0x00000004
#define XHCI_QUIRK_NO_PORT_INDICATORS 0x00000008
#define XHCI_QUIRK_LIMIT_U1U2         0x00000010

typedef struct DECLSPEC_ALIGN(PAGE_SIZE) _XHCI_SCRATCHPAD_PAGE {
    UCHAR Buffer[PAGE_SIZE];
} XHCI_SCRATCHPAD_PAGE, *PXHCI_SCRATCHPAD_PAGE;

typedef struct DECLSPEC_ALIGN(64) _XHCI_HC_RESOURCES {
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
    ULONG Ep0ContextErrorCount;
    UCHAR UsbDeviceAddress;
    UCHAR PortNumber;
    ULONG RouteString;
    UCHAR HighestEndpointId;
    PXHCI_ENDPOINT EndpointTable[XHCI_MAX_ENDPOINTS + 1];
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
    PHYSICAL_ADDRESS DcbaaPhysical;
  PHYSICAL_ADDRESS ScratchpadArrayPhysical;
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
  PXHCI_ERST_ENTRY ErstTable;
  PHYSICAL_ADDRESS ErstTablePhysical;
  ULONG ErstEntryCount;
  ULONGLONG EventRingDequeuePointer;
  LIST_ENTRY CommandContextList;
  KSPIN_LOCK CommandLock;
    PHYSICAL_ADDRESS DeviceContextsPhysical;
    PHYSICAL_ADDRESS InputContextsPhysical;
    PHYSICAL_ADDRESS Ep0RingArrayPhysical;
    XHCI_DEVICE_SLOT DeviceSlots[XHCI_MAX_SLOTS + 1];
    ULONG PendingUsbSts;
    BOOLEAN RhIrqEnabled;
    BOOLEAN RhPendingInvalidate;
    BOOLEAN InterruptsEnabled;
  BOOLEAN ControllerRunning;
  BOOLEAN FatalError;
  ULONG Quirks;
  UCHAR DeviceAddressMap[XHCI_MAX_DEVICE_ADDRESS];
  UCHAR PortLinkState[XHCI_MAX_PORTS + 1];
  UCHAR MaxU1ExitLatency;
  USHORT MaxU2ExitLatency;
  UCHAR ProtocolSegmentCount;
  XHCI_PROTOCOL_SEGMENT ProtocolSegments[XHCI_MAX_PROTOCOL_SEGMENTS];
  UCHAR PortProtocol[XHCI_MAX_PORTS + 1];
  /* MSI/MSI-X discovery (message interrupts not yet connected on ReactOS) */
  BOOLEAN MsiSupported;
    BOOLEAN MsixSupported;
    BOOLEAN MsiEnabled;
    BOOLEAN MsixEnabled;
    UCHAR MsiCapOffset;
  UCHAR MsixCapOffset;
} XHCI_EXTENSION, *PXHCI_EXTENSION;

typedef struct _XHCI_COMMAND_CONTEXT {
  LIST_ENTRY ListEntry;
  ULONGLONG CommandPointer;
  ULONG CommandType;
  ULONG CompletionCode;
  UCHAR SlotId;
  BOOLEAN Completed;
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
    UCHAR SlotId;
    UCHAR EndpointId;
    UCHAR DoorbellTarget;
    USHORT ReservedStreamId;
    BOOLEAN DefaultControl;
    BOOLEAN UsesStaticRing;
    BOOLEAN Isochronous;
    ULONGLONG TotalBytesTransferred;
} XHCI_ENDPOINT, *PXHCI_ENDPOINT;

typedef struct _XHCI_TRANSFER {
    LIST_ENTRY ListEntry;
    PXHCI_ENDPOINT Endpoint;
    PUSBPORT_TRANSFER_PARAMETERS TransferParameters;
    PUSBPORT_SCATTER_GATHER_LIST SgList;
    PVOID TransferHandle;
    ULONGLONG CompletionTrbPointer;
    ULONG RequestedLength;
    ULONG BytesTransferred;
    ULONG UsbdStatus;
    ULONG Flags;
    BOOLEAN IsControl;
    BOOLEAN IsIsochronous;
    USHORT StreamId;
    UCHAR NewAddress;
    UCHAR Reserved[2];
} XHCI_TRANSFER, *PXHCI_TRANSFER;

#define XHCI_TRANSFER_FLAG_SET_ADDRESS   0x00000001
#define XHCI_TRANSFER_FLAG_GET_DESCRIPTOR 0x00000002

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath);
