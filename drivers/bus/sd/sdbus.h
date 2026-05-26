/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internal header for the SD bus driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _SDBUS_H_
#define _SDBUS_H_

#include <ntifs.h>
#include <ntstrsafe.h>
#include <wdmguid.h>

/* Pull in the public SD definitions */
#include <ntddsd.h>

/* Pull in internal SD and SDHCI definitions */
#include <reactos/drivers/sd/sddef.h>
#include <reactos/drivers/sd/sdhci.h>

/* Pool tag: 'SDBu' */
#define TAG_SDBUS 'uBDS'

/**
 * @brief Device state enumeration for FDO and PDO lifecycle tracking.
 */
typedef enum _SDBUS_DEVICE_STATE {
    SdBusDeviceStateStopped = 0,
    SdBusDeviceStateStarted,
    SdBusDeviceStateRemoved,
    SdBusDeviceStateSurpriseRemoved
} SDBUS_DEVICE_STATE;

/**
 * @brief Common device extension shared prefix for FDO and PDO.
 */
typedef struct _SDBUS_COMMON_EXTENSION {
    PDEVICE_OBJECT Self;
    BOOLEAN IsFdo;
    SDBUS_DEVICE_STATE DeviceState;
    DEVICE_POWER_STATE DevicePowerState;
} SDBUS_COMMON_EXTENSION, *PSDBUS_COMMON_EXTENSION;

/**
 * @brief FDO device extension for the SD host controller.
 */
typedef struct _FDO_EXTENSION {
    /* Common header -- must be first */
    SDBUS_COMMON_EXTENSION Common;

    /* Device stack */
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;

    /* SDHCI register mapping */
    PVOID RegisterBase;
    ULONG RegisterLength;
    BOOLEAN RegistersMapped;

    /* Interrupt handling */
    PKINTERRUPT InterruptObject;
    KDPC CardDetectDpc;
    volatile LONG PendingInterruptStatus;

    /* Command/data completion (interrupt-driven) */
    KDPC CommandDpc;                            /* DPC to signal CommandEvent from ISR */
    KEVENT CommandEvent;                        /* SynchronizationEvent, auto-reset */
    volatile LONG CommandInterruptStatus;       /* ISR accumulates cmd/data bits here */

    /* Synchronization */
    KSPIN_LOCK Lock;

    /* Serializes all SDHCI register access (commands, data transfers, property queries) */
    FAST_MUTEX CommandMutex;

    /* Child PDO list */
    LIST_ENTRY ChildPdoList;
    ULONG ChildPdoCount;

    volatile LONG InsertionGeneration;

    /* Host controller capabilities */
    ULONG HostCapabilities;
    ULONG HostCapabilities2;
    ULONG MaxClockFrequency;
    USHORT SpecVersion;
    UCHAR CurrentBusWidth;
    BOOLEAN EmmcPartitionConfigValid;
    UCHAR EmmcPartitionConfig;

    /* Host bus information (for DMA adapter initialization) */
    INTERFACE_TYPE BusInterfaceType;
    ULONG BusNumber;

    /* DMA adapter for map-register/scatter-gather translation */
    PDMA_ADAPTER DmaAdapter;
    ULONG DmaMapRegisters;
    BOOLEAN UseDmaMappingForAdma2;

    /* ADMA2 scatter-gather (preferred when controller supports it) */
    BOOLEAN UseAdma2;
    PSDHCI_ADMA2_DESCRIPTOR_32 Adma2Table;
    PHYSICAL_ADDRESS Adma2TablePhysical;
    ULONG Adma2MaxDescriptors;

    /* SDMA bounce buffer (fallback when ADMA2 is not available) */
    PVOID DmaBuffer;
    PHYSICAL_ADDRESS DmaPhysical;
    ULONG DmaBufferSize;
    BOOLEAN UseSdma;

    /* Remove lock */
    IO_REMOVE_LOCK RemoveLock;

    IO_CSQ RequestCsq;
    LIST_ENTRY RequestQueue;
    KSPIN_LOCK RequestQueueLock;
    KEVENT RequestArrived;
    HANDLE WorkerThreadHandle;
    PVOID WorkerThread;
    volatile LONG OutstandingRequestCount;
    volatile LONG WorkerShutdown;
    BOOLEAN WorkerStarted;
    BOOLEAN CsqInitialized;

    BOOLEAN UseMiniportDispatch;
    PVOID MiniportDispatchContext;
    NTSTATUS (NTAPI *MiniportIssueRequest)(PVOID Ctx, PVOID Request);
} FDO_EXTENSION, *PFDO_EXTENSION;

/**
 * @brief PDO device extension for a child SD/eMMC/SDIO card.
 */
typedef struct _PDO_EXTENSION {
    /* Common header -- must be first */
    SDBUS_COMMON_EXTENSION Common;

    /* Parent FDO */
    PFDO_EXTENSION FdoExtension;

    /* Linked list entry on FDO's ChildPdoList */
    LIST_ENTRY ListEntry;

    /* Card identification and properties */
    SD_CARD_TYPE CardType;
    ULONG RelativeAddress;
    SD_CID Cid;
    SD_CSD Csd;
    UCHAR Scr[8];
    UCHAR ExtCsd[512];

    /* Computed card capacity */
    ULONGLONG TotalSectors;
    ULONG BytesPerSector;
    BOOLEAN HighCapacity;
    BOOLEAN WriteProtected;

    /* State tracking */
    BOOLEAN Present;
    BOOLEAN ReportedMissing;
    BOOLEAN Started;
    BOOLEAN EjectPending;

    ULONG InsertionGeneration;

    ULONG PagingPathCount;
    ULONG HibernationPathCount;
    ULONG DumpPathCount;

    /* Interface callback */
    PSDBUS_CALLBACK_ROUTINE CallbackRoutine;
    PVOID CallbackContext;
    ULONG FunctionNumber;

    USHORT SdioVendorId;
    USHORT SdioDeviceId;
    UCHAR SdioClass;

    UCHAR SdioCccrRev;
    UCHAR SdioSdSpecRev;
    UCHAR SdioCardCap;
    UCHAR SdioBusIfCtrl;
    UCHAR SdioUhsSupport;
    UCHAR SdioNumFunctions;
    ULONG SdioCommonCisPointer;

    BOOLEAN IsEmmcPartition;
    UCHAR EmmcPartitionId;
    ULONGLONG EmmcPartitionSizeBytes;

    /* Interface reference count */
    LONG InterfaceReferenceCount;

    /* Remove lock */
    IO_REMOVE_LOCK RemoveLock;
} PDO_EXTENSION, *PPDO_EXTENSION;

/**
 * @brief Interface context allocated per SdBusOpenInterface call.
 */
typedef struct _SDBUS_INTERFACE_CONTEXT {
    PPDO_EXTENSION PdoExtension;
    PSDBUS_CALLBACK_ROUTINE CallbackRoutine;
    PVOID CallbackContext;
    BOOLEAN DeviceGeneratesInterrupts;
} SDBUS_INTERFACE_CONTEXT, *PSDBUS_INTERFACE_CONTEXT;

/**
 * @brief sdbus.c -- Driver entry and dispatch routing.
 */

DRIVER_INITIALIZE DriverEntry;

DRIVER_ADD_DEVICE SdBusAddDevice;

NTSTATUS
NTAPI
SdBusAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject);

DRIVER_DISPATCH SdBusDispatchPnp;
DRIVER_DISPATCH SdBusDispatchPower;
DRIVER_DISPATCH SdBusDispatchInternalDeviceControl;
DRIVER_DISPATCH SdBusDispatchCreateClose;

/**
 * @brief fdo.c -- FDO PnP and dispatch.
 */

/**
 * @brief Handle PnP IRPs for the FDO.
 *
 * @param[in]     DeviceObject  Pointer to the FDO device object.
 * @param[in,out] Irp           Pointer to the PnP IRP to process.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdBusFdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

/**
 * @brief Handle power IRPs for the FDO.
 *
 * @param[in]     DeviceObject  Pointer to the FDO device object.
 * @param[in,out] Irp           Pointer to the power IRP to process.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdBusFdoPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

/**
 * @brief pdo.c -- PDO PnP and dispatch.
 */

/**
 * @brief Handle PnP IRPs for a child PDO.
 *
 * @param[in]     DeviceObject  Pointer to the PDO device object.
 * @param[in,out] Irp           Pointer to the PnP IRP to process.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdBusPdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

/**
 * @brief Handle power IRPs for a child PDO.
 *
 * @param[in]     DeviceObject  Pointer to the PDO device object.
 * @param[in,out] Irp           Pointer to the power IRP to process.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdBusPdoPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

/**
 * @brief Handle internal device control IRPs for a child PDO.
 *
 * @param[in]     DeviceObject  Pointer to the PDO device object.
 * @param[in,out] Irp           Pointer to the internal IOCTL IRP containing the SD request packet.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdBusPdoInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

/**
 * @brief enum.c -- Card enumeration.
 */

/**
 * @brief Perform the full SD/eMMC card initialization sequence.
 *
 * Sends CMD0, CMD8, ACMD41/CMD1, CMD2, CMD3, CMD9, CMD7, and
 * card-type-specific configuration to bring the card to transfer state.
 *
 * @param[in]  FdoExtension  Pointer to the host controller FDO extension.
 * @param[out] PdoExtension  Pointer to the child PDO extension to populate with card data.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdBusEnumerateCard(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_ PPDO_EXTENSION PdoExtension);

/**
 * @brief Raise SDHCI clock from 400 kHz init speed to transfer speed.
 *
 * @param[in] FdoExtension  Pointer to the host controller FDO extension.
 * @param[in] PdoExtension  Pointer to the child PDO extension (for card type).
 *
 * @return STATUS_SUCCESS or STATUS_IO_TIMEOUT.
 */
NTSTATUS
SdBusSetTransferClock(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension);

/**
 * @brief Send a single SD/MMC command via the SDHCI controller.
 *
 * @param[in]  FdoExtension  Pointer to the host controller FDO extension.
 * @param[in]  CommandIndex   The SD command index (e.g. 0 for CMD0).
 * @param[in]  Argument       The 32-bit command argument.
 * @param[in]  CommandFlags   SDHCI command register flags (response type, checks).
 * @param[out] Response       Optional pointer to receive the response (4 ULONGs for R2).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdBusSendCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _Out_opt_ PULONG Response);

/**
 * @brief Send an application-specific command (CMD55 + ACMDxx).
 *
 * @param[in]  FdoExtension  Pointer to the host controller FDO extension.
 * @param[in]  Rca            The relative card address (0 during init).
 * @param[in]  CommandIndex   The application command index.
 * @param[in]  Argument       The 32-bit command argument.
 * @param[in]  CommandFlags   SDHCI command register flags for the ACMD.
 * @param[out] Response       Optional pointer to receive the response.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdBusSendAppCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Rca,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _Out_opt_ PULONG Response);

/**
 * @brief Parse a 128-bit CID from SDHCI response registers.
 *
 * @param[in]  RawResponse  Pointer to a 4-element ULONG array from the SDHCI response registers.
 * @param[out] Cid          Pointer to the SD_CID structure to populate.
 */
VOID
SdBusParseCid(
    _In_ PULONG RawResponse,
    _Out_ PSD_CID Cid);

/**
 * @brief Parse a 128-bit CSD from SDHCI response registers.
 *
 * @param[in]  RawResponse  Pointer to a 4-element ULONG array from the SDHCI response registers.
 * @param[out] Csd          Pointer to the SD_CSD structure to populate.
 */
VOID
SdBusParseCsd(
    _In_ PULONG RawResponse,
    _Out_ PSD_CSD Csd);

/**
 * @brief hotplug.c -- Card detect ISR and DPC.
 */

KSERVICE_ROUTINE SdBusInterruptService;

/**
 * @brief SDHCI interrupt service routine for card detect and SDIO events.
 *
 * @param[in] Interrupt       Pointer to the interrupt object.
 * @param[in] ServiceContext  Pointer to the FDO_EXTENSION (ISR context).
 *
 * @return TRUE if the interrupt was handled, FALSE otherwise.
 */
BOOLEAN
NTAPI
SdBusInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext);

KDEFERRED_ROUTINE SdBusCommandCompletionDpc;
KDEFERRED_ROUTINE SdBusCardDetectDpc;

NTSTATUS
SdBusEnumerateInsertedCard(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN NotifyPnp,
    _In_ BOOLEAN Debounce);

/**
 * @brief DPC for card detect events, queues a work item at PASSIVE_LEVEL.
 *
 * @param[in] Dpc              Pointer to the DPC object.
 * @param[in] DeferredContext   Pointer to the FDO_EXTENSION.
 * @param[in] SystemArgument1  System-supplied argument (unused).
 * @param[in] SystemArgument2  System-supplied argument (unused).
 */
VOID
NTAPI
SdBusCardDetectDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2);

/**
 * @brief request.c -- Request processing.
 */

/**
 * @brief Dispatch an SDBUS_REQUEST_PACKET from a class driver.
 *
 * Serializes access to the SDHCI controller via a FAST_MUTEX and
 * dispatches property get/set, device commands, and soft reset requests.
 *
 * @param[in]     PdoExtension   Pointer to the child PDO extension for the target card.
 * @param[in,out] RequestPacket  Pointer to the SD bus request packet to process.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdBusHandleRequest(
    _In_ PPDO_EXTENSION PdoExtension,
    _Inout_ PSDBUS_REQUEST_PACKET RequestPacket);

/**
 * @brief Wait for SDHCI interrupt bits via the ISR-driven completion event.
 *
 * @param[in]  FdoExtension  Host controller FDO extension.
 * @param[in]  WantedBits    Interrupt status bits to wait for.
 * @param[in]  ErrorBits     Error bits that also terminate the wait.
 * @param[in]  TimeoutMs     Maximum wait time in milliseconds.
 * @param[out] ResultBits    Receives the matched bits.
 *
 * @return STATUS_SUCCESS, STATUS_IO_DEVICE_ERROR, or STATUS_IO_TIMEOUT.
 */
NTSTATUS
SdBusWaitForInterrupt(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG WantedBits,
    _In_ ULONG ErrorBits,
    _In_ ULONG TimeoutMs,
    _Out_ PULONG ResultBits);

/**
 * @brief Execute an SDHCI command with optional PIO data transfer.
 *
 * @param[in]      FdoExtension  Pointer to the host controller FDO extension.
 * @param[in]      CmdDesc       Pointer to the command descriptor (command, response type, transfer type).
 * @param[in]      Argument      The 32-bit command argument.
 * @param[in,out]  Mdl           Optional MDL describing the data buffer for read/write transfers.
 * @param[in]      DataLength    Length of the data transfer in bytes (0 for command-only).
 * @param[in]      BlockSize     SDHCI block size for data transfers.
 * @param[out]     Response      Optional pointer to receive response data (4 ULONGs for R2).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdBusSendSdhciCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PSDCMD_DESCRIPTOR CmdDesc,
    _In_ ULONG Argument,
    _Inout_opt_ PMDL Mdl,
    _In_ ULONG DataLength,
    _In_ ULONG BlockSize,
    _Out_opt_ PULONG Response);

NTSTATUS
SdBusInitializeRequestQueue(
    _In_ PFDO_EXTENSION FdoExtension);

VOID
SdBusTearDownRequestQueue(
    _In_ PFDO_EXTENSION FdoExtension);

/**
 * @brief interface.c -- SDBUS_INTERFACE_STANDARD implementation.
 */

/**
 * @brief Open the SD bus interface and populate the interface structure.
 *
 * @param[in]  PdoExtension  Pointer to the child PDO extension.
 * @param[out] Interface     Pointer to the SDBUS_INTERFACE_STANDARD structure to fill in.
 * @param[in]  Size          Size of the interface structure provided by the caller.
 * @param[in]  Version       Interface version requested by the caller.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdBusOpenInterfaceImpl(
    _In_ PPDO_EXTENSION PdoExtension,
    _Out_ PSDBUS_INTERFACE_STANDARD Interface,
    _In_ USHORT Size,
    _In_ USHORT Version);

/**
 * @brief Increment the interface reference count.
 *
 * @param[in] Context  Pointer to the SDBUS_INTERFACE_CONTEXT for this instance.
 */
VOID
NTAPI
SdBusInterfaceReference(
    _In_ PVOID Context);

/**
 * @brief Decrement the interface reference count, freeing context at zero.
 *
 * @param[in] Context  Pointer to the SDBUS_INTERFACE_CONTEXT for this instance.
 */
VOID
NTAPI
SdBusInterfaceDereference(
    _In_ PVOID Context);

/**
 * @brief Initialize the interface with function driver callback parameters.
 *
 * @param[in] Context              Pointer to the SDBUS_INTERFACE_CONTEXT for this instance.
 * @param[in] InterfaceParameters  Pointer to SDBUS_INTERFACE_PARAMETERS from the function driver.
 *
 * @return STATUS_SUCCESS on success, or STATUS_INVALID_PARAMETER.
 */
NTSTATUS
NTAPI
SdBusInitializeInterfaceImpl(
    _In_ PVOID Context,
    _In_ PSDBUS_INTERFACE_PARAMETERS InterfaceParameters);

/**
 * @brief Re-enable SDIO card interrupt after the function driver finishes processing.
 *
 * @param[in] Context  Pointer to the SDBUS_INTERFACE_CONTEXT for this instance.
 */
NTSTATUS
NTAPI
SdBusAcknowledgeInterruptImpl(
    _In_ PVOID Context);

/**
 * @brief power.c -- Power management.
 *
 * Forward declarations used within power.c; public API is SdBusFdoPower /
 * SdBusPdoPower declared above.
 */


NTSTATUS
SdBusSdioCmd52(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR Function,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN RawMode,
    _In_ ULONG Address,
    _In_ UCHAR DataIn,
    _Out_opt_ PUCHAR DataOut);

NTSTATUS
SdBusSdioReadCccr(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Address,
    _Out_ PUCHAR DataOut);

NTSTATUS
SdBusSdioWriteCccr(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Address,
    _In_ UCHAR Data);

NTSTATUS
SdBusSdioCmd53(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension,
    _In_ UCHAR Function,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN BlockMode,
    _In_ BOOLEAN Increment,
    _In_ ULONG Address,
    _In_ ULONG Count,
    _In_ ULONG BlockSize,
    _Inout_opt_ PMDL Mdl);

NTSTATUS
SdBusSdioEnumerateFunctions(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION HostPdo,
    _In_ ULONG NumFunctions);


NTSTATUS
SdBusEmmcSwitch(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR Access,
    _In_ UCHAR Index,
    _In_ UCHAR Value,
    _In_ UCHAR CmdSet);

NTSTATUS
SdBusEmmcSwitchByRca(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Rca,
    _In_ UCHAR Access,
    _In_ UCHAR Index,
    _In_ UCHAR Value,
    _In_ UCHAR CmdSet,
    _In_ ULONG TimeoutMs);

NTSTATUS
SdBusEmmcWaitReadyByRca(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Rca,
    _In_ ULONG TimeoutMs);

NTSTATUS
SdBusEmmcSelectPartition(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension,
    _In_ UCHAR PartitionId);

NTSTATUS
SdBusEmmcSanitize(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension);

NTSTATUS
SdBusEmmcEnableBkops(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension);

NTSTATUS
SdBusEmmcEnableCache(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension);

NTSTATUS
SdBusEmmcEnumeratePartitions(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION MainPdo);

NTSTATUS
SdhcRegisterBusCallbacks(
    _In_ PVOID FdoExt,
    _In_ PVOID MiniportExt,
    _In_ PVOID IssueRequestFunc);

NTSTATUS
SdBusResetHost(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR ResetMask);

/**
 * @brief SDHCI register access helpers (inline).
 */

/**
 * @brief Read a 32-bit SDHCI register.
 *
 * @param[in] FdoExtension  Pointer to the host controller FDO extension.
 * @param[in] Register      The SDHCI register offset.
 *
 * @return The 32-bit register value.
 */
static __inline ULONG
SdBusReadReg32(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Register)
{
    return SDHCI_READ32(FdoExtension->RegisterBase, Register);
}

/**
 * @brief Write a 32-bit SDHCI register.
 *
 * @param[in] FdoExtension  Pointer to the host controller FDO extension.
 * @param[in] Register      The SDHCI register offset.
 * @param[in] Value         The 32-bit value to write.
 */
static __inline VOID
SdBusWriteReg32(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    SDHCI_WRITE32(FdoExtension->RegisterBase, Register, Value);
}

typedef struct _SDBUS_SIGNAL_ENABLE_UPDATE
{
    PFDO_EXTENSION FdoExtension;
    ULONG SetMask;
    ULONG ClearMask;
    ULONG Value;
} SDBUS_SIGNAL_ENABLE_UPDATE, *PSDBUS_SIGNAL_ENABLE_UPDATE;

static __inline BOOLEAN
NTAPI
SdBusUpdateInterruptSignalEnableSynchronized(
    _In_ PVOID Context)
{
    PSDBUS_SIGNAL_ENABLE_UPDATE Update = (PSDBUS_SIGNAL_ENABLE_UPDATE)Context;

    Update->Value = SdBusReadReg32(Update->FdoExtension, SDHCI_INT_SIGNAL_ENABLE);
    Update->Value |= Update->SetMask;
    Update->Value &= ~Update->ClearMask;
    SdBusWriteReg32(Update->FdoExtension, SDHCI_INT_SIGNAL_ENABLE, Update->Value);
    return TRUE;
}

static __inline ULONG
SdBusUpdateInterruptSignalEnable(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG SetMask,
    _In_ ULONG ClearMask)
{
    SDBUS_SIGNAL_ENABLE_UPDATE Update;

    if (FdoExtension == NULL || FdoExtension->RegisterBase == NULL)
    {
        return 0;
    }

    Update.FdoExtension = FdoExtension;
    Update.SetMask = SetMask;
    Update.ClearMask = ClearMask;
    Update.Value = 0;

    if (FdoExtension != NULL && FdoExtension->InterruptObject != NULL)
    {
        KeSynchronizeExecution(FdoExtension->InterruptObject,
                               SdBusUpdateInterruptSignalEnableSynchronized,
                               &Update);
    }
    else
    {
        SdBusUpdateInterruptSignalEnableSynchronized(&Update);
    }

    return Update.Value;
}

/**
 * @brief Read a 16-bit SDHCI register.
 *
 * @param[in] FdoExtension  Pointer to the host controller FDO extension.
 * @param[in] Register      The SDHCI register offset.
 *
 * @return The 16-bit register value.
 */
static __inline USHORT
SdBusReadReg16(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ USHORT Register)
{
    return SDHCI_READ16(FdoExtension->RegisterBase, Register);
}

/**
 * @brief Write a 16-bit SDHCI register.
 *
 * @param[in] FdoExtension  Pointer to the host controller FDO extension.
 * @param[in] Register      The SDHCI register offset.
 * @param[in] Value         The 16-bit value to write.
 */
static __inline VOID
SdBusWriteReg16(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ USHORT Register,
    _In_ USHORT Value)
{
    SDHCI_WRITE16(FdoExtension->RegisterBase, Register, Value);
}

/**
 * @brief Read an 8-bit SDHCI register.
 *
 * @param[in] FdoExtension  Pointer to the host controller FDO extension.
 * @param[in] Register      The SDHCI register offset.
 *
 * @return The 8-bit register value.
 */
static __inline UCHAR
SdBusReadReg8(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Register)
{
    return SDHCI_READ8(FdoExtension->RegisterBase, Register);
}

/**
 * @brief Write an 8-bit SDHCI register.
 *
 * @param[in] FdoExtension  Pointer to the host controller FDO extension.
 * @param[in] Register      The SDHCI register offset.
 * @param[in] Value         The 8-bit value to write.
 */
static __inline VOID
SdBusWriteReg8(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Register,
    _In_ UCHAR Value)
{
    SDHCI_WRITE8(FdoExtension->RegisterBase, Register, Value);
}

#endif /* _SDBUS_H_ */
