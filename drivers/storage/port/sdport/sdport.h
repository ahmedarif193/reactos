/*
 * PROJECT:     ReactOS SD Port Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internal header for the SD port driver (sdport.sys)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <wdm.h>
#include <stdio.h>
#include <ntstrsafe.h>
#include <ntddsd.h>
#include <reactos/drivers/sd/sdhci.h>
#include <reactos/drivers/sd/sddef.h>

/* Pool tags */
#define SDPORT_TAG_FDO      'FdpS'    /* SdpF - FDO extension */
#define SDPORT_TAG_SLOT     'SdpS'    /* SdpS - Slot extension */
#define SDPORT_TAG_REQUEST  'RdpS'    /* SdpR - Request */
#define SDPORT_TAG_ADMA     'AdpS'    /* SdpA - ADMA descriptors */
#define SDPORT_TAG_DMA      'DdpS'    /* SdpD - DMA buffer */

/* Maximum number of slots per host controller */
#define SDPORT_MAX_SLOTS    6

/* Maximum ADMA2 descriptors per transfer (covers 1MB with 64KB segments) */
#define SDPORT_MAX_ADMA2_DESCRIPTORS    32

/* Device states */
typedef enum _SDPORT_DEVICE_STATE {
    SdPortDeviceStateNotStarted = 0,
    SdPortDeviceStateStarted,
    SdPortDeviceStateStopPending,
    SdPortDeviceStateStopped,
    SdPortDeviceStateRemovePending,
    SdPortDeviceStateSurpriseRemoval,
    SdPortDeviceStateRemoved
} SDPORT_DEVICE_STATE;

typedef enum _SDPORT_REQUEST_STATE {
    SdPortRequestStateIdle = 0,
    SdPortRequestStateCommand,
    SdPortRequestStateResponse,
    SdPortRequestStateData,
    SdPortRequestStateBusyEnd,
    SdPortRequestStateErrorRecovery,
    SdPortRequestStateCompleted
} SDPORT_REQUEST_STATE;

/**
 * @brief Miniport callback typedefs.
 */

typedef
NTSTATUS
(*PSDPORT_GET_SLOT_COUNT)(
    _In_ PVOID PrivateExtension,
    _Out_ PUCHAR SlotCount);

typedef
VOID
(*PSDPORT_GET_SLOT_CAPABILITIES)(
    _In_ PVOID PrivateExtension,
    _In_ UCHAR SlotIndex,
    _Out_ PVOID Capabilities);

typedef
NTSTATUS
(*PSDPORT_INITIALIZE)(
    _In_ PVOID PrivateExtension,
    _In_ PHYSICAL_ADDRESS PhysicalBase,
    _In_ PVOID VirtualBase,
    _In_ ULONG Length,
    _In_ BOOLEAN CrashdumpMode);

typedef
NTSTATUS
(*PSDPORT_ISSUE_BUS_OPERATION)(
    _In_ PVOID PrivateExtension,
    _In_ PVOID Request);

typedef
BOOLEAN
(*PSDPORT_GET_CARD_DETECT_STATE)(
    _In_ PVOID PrivateExtension);

typedef
BOOLEAN
(*PSDPORT_INTERRUPT)(
    _In_ PVOID PrivateExtension,
    _Out_ PULONG Events,
    _Out_ PULONG Errors,
    _Out_ PBOOLEAN NotifyCardChange,
    _Out_ PBOOLEAN NotifySdioInterrupt,
    _Out_ PBOOLEAN NotifyTuning);

typedef
NTSTATUS
(*PSDPORT_ISSUE_REQUEST)(
    _In_ PVOID PrivateExtension,
    _In_ PVOID Request);

typedef
VOID
(*PSDPORT_REQUEST_DPC)(
    _In_ PVOID PrivateExtension,
    _In_ PVOID Request,
    _In_ ULONG Events,
    _In_ ULONG Errors);

typedef
NTSTATUS
(*PSDPORT_SAVE_CONTEXT)(
    _In_ PVOID PrivateExtension);

typedef
NTSTATUS
(*PSDPORT_RESTORE_CONTEXT)(
    _In_ PVOID PrivateExtension);

typedef
VOID
(*PSDPORT_TOGGLE_EVENTS)(
    _In_ PVOID PrivateExtension,
    _In_ ULONG EventMask,
    _In_ BOOLEAN Enable);

typedef
VOID
(*PSDPORT_CLEAR_EVENTS)(
    _In_ PVOID PrivateExtension,
    _In_ ULONG EventMask);

typedef
VOID
(*PSDPORT_CLEANUP)(
    _In_ PVOID PrivateExtension);

typedef
NTSTATUS
(*PSDPORT_POFX_POWER_CONTROL_CALLBACK)(
    _In_ ULONG PowerControlCode,
    _In_ ULONG InputBufferSize,
    _Inout_ PULONG InputBuffer,
    _Inout_ PULONG OutputBuffer);

typedef
VOID
(*PSDPORT_SDMA_BOUNDARY_CALLBACK)(
    _In_ PVOID PrivateExtension,
    _In_ PVOID Request);

typedef
VOID
(*PSDPORT_SET_DEFAULT_INTERRUPT_MASK)(
    _In_ PVOID PrivateExtension,
    _Inout_ PULONG EventMask);

typedef
VOID
(*PSDPORT_SET_TRANSFER_INTERRUPT_MASK)(
    _In_ PVOID PrivateExtension,
    _In_ PVOID Request,
    _Inout_ PULONG EventMask);

/**
 * @brief Miniport initialization data provided by the miniport driver.
 */

typedef struct _SDPORT_INITIALIZATION_DATA {
    ULONG                                   StructureSize;
    PSDPORT_GET_SLOT_COUNT                  GetSlotCount;
    PSDPORT_GET_SLOT_CAPABILITIES           GetSlotCapabilities;
    PSDPORT_INITIALIZE                      Initialize;
    PSDPORT_ISSUE_BUS_OPERATION             IssueBusOperation;
    PSDPORT_GET_CARD_DETECT_STATE           GetCardDetectState;
    PSDPORT_INTERRUPT                       Interrupt;
    PSDPORT_ISSUE_REQUEST                   IssueRequest;
    PSDPORT_REQUEST_DPC                     RequestDpc;
    PSDPORT_SAVE_CONTEXT                    SaveContext;
    PSDPORT_RESTORE_CONTEXT                 RestoreContext;
    PSDPORT_TOGGLE_EVENTS                   ToggleEvents;
    PSDPORT_CLEAR_EVENTS                    ClearEvents;
    PSDPORT_CLEANUP                         Cleanup;
    PSDPORT_POFX_POWER_CONTROL_CALLBACK     PoFxPowerControlCallback;
    PSDPORT_SDMA_BOUNDARY_CALLBACK          SdmaBoundaryCallback;
    PSDPORT_SET_DEFAULT_INTERRUPT_MASK      SetDefaultInterruptMask;
    PSDPORT_SET_TRANSFER_INTERRUPT_MASK     SetTransferInterruptMask;
    ULONG                                   PrivateExtensionSize;
} SDPORT_INITIALIZATION_DATA, *PSDPORT_INITIALIZATION_DATA;

/**
 * @brief Bus operation types for miniport IssueBusOperation requests.
 */

typedef enum _SDPORT_BUS_OPERATION_TYPE {
    SdResetHost = 0,
    SdSetClock,
    SdSetVoltage,
    SdSetBusWidth,
    SdSetBusSpeed,
    SdSetSignalingVoltage,
    SdSetDriveStrength,
    SdExecuteTuning
} SDPORT_BUS_OPERATION_TYPE;

/* Bus operation request passed to miniport IssueBusOperation */
typedef struct _SDPORT_BUS_OPERATION {
    SDPORT_BUS_OPERATION_TYPE Type;
    union {
        ULONG FrequencyKhz;          /* SdSetClock */
        UCHAR Voltage;                /* SdSetVoltage (SDHCI power control value) */
        UCHAR BusWidth;               /* SdSetBusWidth (1, 4, 8) */
        UCHAR SpeedMode;              /* SdSetBusSpeed */
        UCHAR SignalingVoltage;       /* SdSetSignalingVoltage (0=3.3V, 1=1.8V) */
        UCHAR TuningCommand;
    } Parameters;
} SDPORT_BUS_OPERATION, *PSDPORT_BUS_OPERATION;

struct _SDPORT_REQUEST;
struct _SDPORT_FDO_EXTENSION;

typedef
VOID
(*PSDPORT_REQUEST_COMPLETION_ROUTINE)(
    _In_ struct _SDPORT_FDO_EXTENSION *FdoExtension,
    _In_ struct _SDPORT_REQUEST *Request,
    _In_ PVOID Context);

/**
 * @brief SD command request passed to the miniport for execution.
 */

typedef struct _SDPORT_REQUEST {
    SDCMD_DESCRIPTOR    Command;
    ULONG               Argument;
    ULONG               Response[4];
    PVOID               DataBuffer;
    PMDL                DataMdl;
    ULONG               BlockSize;
    ULONG               BlockCount;
    ULONG               BytesTransferred;
    NTSTATUS            Status;
    BOOLEAN             UseAdma;
    PVOID               AdmaDescriptorTable;
    PHYSICAL_ADDRESS    AdmaDescriptorPhysical;
    UCHAR               RetryCount;
    UCHAR               MaxRetries;

    SDPORT_REQUEST_STATE        State;

    LIST_ENTRY                  QueueLink;
    PSDPORT_REQUEST_COMPLETION_ROUTINE  CompletionRoutine;
    PVOID                       CompletionContext;

    ULONG                       PioBytesDone;
    ULONG                       PioBytesPerBlock;
} SDPORT_REQUEST, *PSDPORT_REQUEST;

/**
 * @brief Capabilities reported by the miniport for a given slot.
 */

typedef struct _SDPORT_CAPABILITIES {
    ULONG       MaximumBlockSize;
    ULONG       MaximumBlockCount;
    BOOLEAN     HighSpeedSupported;
    BOOLEAN     Sdr50Supported;
    BOOLEAN     Sdr104Supported;
    BOOLEAN     Ddr50Supported;
    BOOLEAN     Hs200Supported;
    BOOLEAN     Hs400Supported;
    BOOLEAN     Adma2Supported;
    BOOLEAN     SdmaSupported;
    BOOLEAN     V18Supported;
    BOOLEAN     V30Supported;
    BOOLEAN     V33Supported;
    BOOLEAN     EightBitSupported;
    ULONG       BaseClockFrequencyKhz;
} SDPORT_CAPABILITIES, *PSDPORT_CAPABILITIES;

/**
 * @brief Per-slot context (one per card slot on the host controller).
 */

typedef struct _SDPORT_SLOT_EXTENSION {
    UCHAR                   SlotIndex;
    BOOLEAN                 CardPresent;
    BOOLEAN                 Initialized;
    SD_CARD_TYPE            CardType;
    SDPORT_CAPABILITIES     Capabilities;

    /* Card identity and configuration registers */
    ULONG                   Rca;            /* Relative Card Address (upper 16 bits) */
    SD_CID                  Cid;
    SD_CSD                  Csd;
    SD_SCR                  Scr;
    UCHAR                   ExtCsd[512];    /* eMMC EXT_CSD */

    /* Current bus parameters */
    ULONG                   CurrentClockKhz;
    UCHAR                   CurrentBusWidth;
    UCHAR                   CurrentSpeedMode;
    BOOLEAN                 HighCapacity;   /* SDHC/SDXC or eMMC sector mode */

    /* ADMA2 descriptor table for the slot */
    PVOID                   AdmaDescriptorBuffer;
    PHYSICAL_ADDRESS        AdmaDescriptorPhysical;
    ULONG                   AdmaDescriptorSize;

    /* Pointer back to the FDO extension */
    PVOID                   FdoExtension;

    /* Synchronization event for command completion */
    KEVENT                  RequestEvent;
    NTSTATUS                RequestStatus;

    volatile LONG           LastEvents;
    volatile LONG           LastErrors;
    PSDPORT_REQUEST         CurrentRequest;
    BOOLEAN                 RequestHasData;

    SDPORT_REQUEST_STATE    State;

    KSPIN_LOCK              RequestLock;
    LIST_ENTRY              PendingRequests;
    PSDPORT_REQUEST         ActiveRequest;

    BOOLEAN                 UsePortPio;
} SDPORT_SLOT_EXTENSION, *PSDPORT_SLOT_EXTENSION;

/**
 * @brief FDO device extension for the SD port driver.
 */

typedef struct _SDPORT_FDO_EXTENSION {
    /* Standard device object pointers */
    PDEVICE_OBJECT          DeviceObject;
    PDEVICE_OBJECT          PhysicalDeviceObject;
    PDEVICE_OBJECT          LowerDevice;

    /* Device state */
    SDPORT_DEVICE_STATE     DeviceState;
    IO_REMOVE_LOCK          RemoveLock;

    /* Miniport callback table (copied from initialization data) */
    SDPORT_INITIALIZATION_DATA  MiniportInitData;

    /* Miniport private extension (allocated after FDO extension) */
    PVOID                   MiniportPrivateExtension;
    ULONG                   MiniportPrivateSize;

    /* Resource information from PnP START_DEVICE */
    PHYSICAL_ADDRESS        MappedPhysicalBase;
    PVOID                   MappedVirtualBase;
    ULONG                   MappedLength;
    BOOLEAN                 MappedMemory;

    /* Interrupt */
    PKINTERRUPT             InterruptObject;
    ULONG                   InterruptVector;
    KIRQL                   InterruptLevel;
    KINTERRUPT_MODE         InterruptMode;
    KAFFINITY               InterruptAffinity;
    BOOLEAN                 InterruptConnected;
    KDPC                    InterruptDpc;

    /* Slot state */
    UCHAR                   SlotCount;
    SDPORT_SLOT_EXTENSION   Slots[SDPORT_MAX_SLOTS];

    /* Card change DPC */
    KDPC                    CardChangeDpc;
    PIO_WORKITEM            CardChangeWorkItem;
    volatile LONG           CardChangeWorkItemQueued;
    volatile LONG           CardChangePending;
} SDPORT_FDO_EXTENSION, *PSDPORT_FDO_EXTENSION;

/**
 * @brief Global data shared across the SD port driver.
 */

extern SDPORT_INITIALIZATION_DATA SdPortMiniportInitData;
extern PDRIVER_OBJECT SdPortDriverObject;
extern ULONG SdPortNumber;

/**
 * @brief sdport.c - Main driver entry and PnP.
 */

/**
 * @brief Register the miniport and set up driver dispatch routines.
 *
 * @param DriverObject The driver object passed to the miniport DriverEntry.
 * @param RegistryPath The registry path for the miniport driver.
 * @param InitializationData Miniport callback table and configuration.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
NTAPI
SdPortInitialize(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PSDPORT_INITIALIZATION_DATA InitializationData);

/**
 * @brief PnP AddDevice routine that creates the FDO and attaches to the stack.
 *
 * @param DriverObject The port driver object.
 * @param PhysicalDeviceObject The PDO created by the parent bus driver.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
NTAPI
SdPortAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject);

/**
 * @brief Dispatch routine for IRP_MJ_PNP.
 *
 * @param DeviceObject The FDO device object.
 * @param Irp The PnP IRP to handle.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
NTAPI
SdPortDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp);

/**
 * @brief Dispatch routine for IRP_MJ_POWER.
 *
 * @param DeviceObject The FDO device object.
 * @param Irp The power IRP to handle.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
NTAPI
SdPortDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp);

/**
 * @brief protocol.c - SD/eMMC card initialization.
 */

/**
 * @brief Detect the card type and run the appropriate initialization sequence.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the slot containing the card.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortInitializeCard(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension);

/**
 * @brief Run the full SD memory card initialization sequence.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the slot containing the card.
 * @param IsV2Card TRUE if the card responded to CMD8 (SD v2+).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortInitializeSdCard(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ BOOLEAN IsV2Card);

/**
 * @brief Run the full eMMC card initialization sequence.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the slot containing the card.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortInitializeEmmcCard(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension);

/**
 * @brief Negotiate the highest bus speed supported by both card and host.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the slot containing the card.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSetBusSpeed(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension);

/**
 * @brief command.c - Command building and execution.
 */

/**
 * @brief Fill in an SDPORT_REQUEST for a given SD command.
 *
 * @param Request The request structure to initialize.
 * @param CommandIndex The SD command index (e.g., SDCMD_GO_IDLE_STATE).
 * @param Argument The 32-bit command argument.
 * @param ResponseType The expected response type (R1, R2, etc.).
 * @param TransferType The transfer type (command-only, single block, etc.).
 * @param TransferDirection The data transfer direction (read or write).
 */
VOID
SdPortBuildCommand(
    _Out_ PSDPORT_REQUEST Request,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ SD_RESPONSE_TYPE ResponseType,
    _In_ SD_TRANSFER_TYPE TransferType,
    _In_ SD_TRANSFER_DIRECTION TransferDirection);

/**
 * @brief Send a single SD command and wait for completion.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the target slot.
 * @param Request The command request to send (updated with response on return).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSendCommand(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request);

/**
 * @brief Send an application command (ACMDxx) preceded by CMD55.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the target slot.
 * @param Request The application command request to send.
 * @param Rca The relative card address (0 before RCA is assigned).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSendAppCommand(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG Rca);

/**
 * @brief Send a command with a data transfer phase (PIO or ADMA2).
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the target slot.
 * @param Request The command request describing the data operation.
 * @param DataBuffer Pointer to the data buffer for the transfer.
 * @param BlockSize Size of each data block in bytes.
 * @param BlockCount Number of blocks to transfer.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSendCommandWithData(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ PVOID DataBuffer,
    _In_ ULONG BlockSize,
    _In_ ULONG BlockCount);

NTSTATUS
SdPortQueueRequest(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ UCHAR SlotIndex,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ PSDPORT_REQUEST_COMPLETION_ROUTINE CompletionRoutine,
    _In_opt_ PVOID CompletionContext);

VOID
SdPortEnableManagedPio(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension);

/**
 * @brief transfer.c - DMA and PIO transfer setup.
 */

/**
 * @brief Configure a request for PIO data transfer.
 *
 * @param SlotExtension The slot extension for the target slot.
 * @param Request The request to configure with PIO transfer state.
 * @param Buffer Pointer to the data buffer.
 * @param Length Total transfer length in bytes.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSetupPioTransfer(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ PVOID Buffer,
    _In_ SIZE_T Length);

/**
 * @brief Configure a request for ADMA2 DMA data transfer.
 *
 * @param SlotExtension The slot extension for the target slot.
 * @param Request The request to configure with DMA transfer state.
 * @param Buffer Pointer to the data buffer.
 * @param Length Total transfer length in bytes.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSetupDmaTransfer(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ PVOID Buffer,
    _In_ SIZE_T Length);

/**
 * @brief Allocate a physically contiguous buffer for ADMA2 descriptors.
 *
 * @param SlotExtension The slot extension to allocate descriptors for.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortAllocateAdmaDescriptors(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension);

/**
 * @brief Free the ADMA2 descriptor buffer for a slot.
 *
 * @param SlotExtension The slot extension whose descriptors are freed.
 */
VOID
SdPortFreeAdmaDescriptors(
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension);

/**
 * @brief power.c - Power and clock management.
 */

/**
 * @brief Set the bus voltage for a slot via the miniport.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the target slot.
 * @param Voltage SDHCI power control register value (e.g., SDHCI_PC_BUS_VOLTAGE_330).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSetSlotPower(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ UCHAR Voltage);

/**
 * @brief Set the SD bus clock frequency for a slot via the miniport.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the target slot.
 * @param FrequencyKhz Desired clock frequency in kilohertz.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSetSlotClock(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ ULONG FrequencyKhz);

/**
 * @brief Issue a full host controller reset via the miniport.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the target slot.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortResetHost(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension);

/**
 * @brief Change the data bus width for a slot via the miniport.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the target slot.
 * @param BusWidth Desired bus width (1, 4, or 8 bits).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSetBusWidth(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ UCHAR BusWidth);

/**
 * @brief Switch the signaling voltage for UHS-I modes via the miniport.
 *
 * @param FdoExtension The FDO device extension.
 * @param SlotExtension The slot extension for the target slot.
 * @param Voltage Signaling voltage selector (0 = 3.3V, 1 = 1.8V).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortSetSignalingVoltage(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ UCHAR Voltage);

NTSTATUS
SdPortExecuteTuning(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ UCHAR TuningCommand);
