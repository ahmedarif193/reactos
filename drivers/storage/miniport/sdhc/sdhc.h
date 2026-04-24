/*
 * PROJECT:     ReactOS SD Host Controller Miniport Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SDHCI miniport internal definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ntddsd.h>
#include <reactos/drivers/sd/sdhci.h>
#include <reactos/drivers/sd/sddef.h>

/** @brief Pool tag: 'SDhC' */
#define TAG_SDHC 'ChDS'

/** @brief Maximum number of ADMA2 descriptors per transfer. */
#define SDHC_MAX_ADMA2_DESCRIPTORS      512

/** @brief Host reset timeout in microseconds (100ms). */
#define SDHC_RESET_TIMEOUT_US           100000
/** @brief Clock stabilization timeout in microseconds (20ms). */
#define SDHC_CLOCK_STABLE_TIMEOUT_US    20000
/** @brief Command timeout in microseconds (1 second). */
#define SDHC_CMD_TIMEOUT_US             1000000


#ifndef SDHCI_HC2_HOST_VER4_ENABLE
#define SDHCI_HC2_HOST_VER4_ENABLE      0x1000
#endif

#ifndef SDHCI_HC2_ADDRESSING_64BIT
#define SDHCI_HC2_ADDRESSING_64BIT      0x2000
#endif

#define SDHC_TUNING_LOOP_COUNT          40

#define SDHC_TUNING_BLOCK_SIZE          64

#define SDHC_QUIRK_POWER_ON_DELAY           0x00000001
#define SDHC_QUIRK_NO_HIGHSPEED_BIT         0x00000002
#define SDHC_QUIRK_O2_VENDOR_UNLOCK         0x00000004
#define SDHC_QUIRK_NO_CLOCK_MULT            0x00000008

typedef struct _SDHC_QUIRK_ENTRY {
    USHORT VendorId;
    USHORT DeviceId;
    ULONG  QuirkFlags;
} SDHC_QUIRK_ENTRY, *PSDHC_QUIRK_ENTRY;

/*
 * Forward declarations for sdport interface.
 * These types must match sdport.h definitions.
 */

/** @brief Type for SD port bus operation identifiers. */
typedef ULONG SDPORT_BUS_OPERATION_TYPE;

#define SdResetHost             0   /**< Reset the host controller */
#define SdSetClock              1   /**< Set the SD clock frequency */
#define SdSetVoltage            2   /**< Set the bus voltage */
#define SdSetBusWidth           3   /**< Set the bus width (1/4/8 bit) */
#define SdSetBusSpeed           4   /**< Set the bus speed mode */
#define SdSetSignalingVoltage   5   /**< Set the signaling voltage (3.3V/1.8V) */
#define SdSetDriveStrength      6   /**< Set the driver strength */
#define SdExecuteTuning         7   /**< Execute tuning procedure */

/**
 * @brief Describes a bus operation to be performed by the miniport.
 */
typedef struct _SDPORT_BUS_OPERATION {
    SDPORT_BUS_OPERATION_TYPE Type;     /**< Operation type identifier */
    union {
        ULONG FrequencyKhz;            /**< Clock frequency in KHz (for SdSetClock) */
        UCHAR Voltage;                 /**< Voltage level (for SdSetVoltage) */
        UCHAR BusWidth;                /**< Bus width in bits (for SdSetBusWidth) */
        UCHAR SpeedMode;               /**< Speed mode (for SdSetBusSpeed) */
        UCHAR SignalingVoltage;         /**< Signaling voltage (for SdSetSignalingVoltage) */
        UCHAR TuningCommand;
    } Parameters;
} SDPORT_BUS_OPERATION, *PSDPORT_BUS_OPERATION;

/**
 * @brief Describes an SD command request to be issued by the miniport.
 */
typedef struct _SDPORT_REQUEST {
    SDCMD_DESCRIPTOR Command;           /**< SD command descriptor */
    ULONG Argument;                     /**< Command argument */
    ULONG Response[4];                  /**< Response register values (R1/R2/R3/R6/R7) */
    PVOID DataBuffer;                   /**< Virtual address of the data buffer */
    PMDL DataMdl;                       /**< MDL for the data buffer */
    ULONG BlockSize;                    /**< Block size in bytes */
    ULONG BlockCount;                   /**< Number of blocks to transfer */
    ULONG BytesTransferred;             /**< Bytes already consumed for PIO transfers */
    NTSTATUS Status;                    /**< Completion status */
    BOOLEAN UseAdma;                    /**< TRUE to use ADMA2 for the transfer */
    PVOID AdmaDescriptorTable;          /**< Virtual address of ADMA2 descriptor table */
    PHYSICAL_ADDRESS AdmaDescriptorPhysical; /**< Physical address of ADMA2 descriptor table */
} SDPORT_REQUEST, *PSDPORT_REQUEST;

/**
 * @brief Host controller slot capabilities reported to the port driver.
 */
typedef struct _SDPORT_CAPABILITIES {
    ULONG MaximumBlockSize;             /**< Maximum supported block size in bytes */
    ULONG MaximumBlockCount;            /**< Maximum supported block count per transfer */
    BOOLEAN HighSpeedSupported;         /**< TRUE if high-speed mode is supported */
    BOOLEAN Sdr50Supported;             /**< TRUE if SDR50 mode is supported */
    BOOLEAN Sdr104Supported;            /**< TRUE if SDR104 mode is supported */
    BOOLEAN Ddr50Supported;             /**< TRUE if DDR50 mode is supported */
    BOOLEAN Hs200Supported;             /**< TRUE if HS200 mode is supported */
    BOOLEAN Hs400Supported;             /**< TRUE if HS400 mode is supported */
    BOOLEAN Adma2Supported;             /**< TRUE if ADMA2 is supported */
    BOOLEAN Adma3Supported;
    BOOLEAN SdmaSupported;              /**< TRUE if SDMA is supported */
    BOOLEAN V18Supported;               /**< TRUE if 1.8V signaling is supported */
    BOOLEAN V30Supported;               /**< TRUE if 3.0V supply is supported */
    BOOLEAN V33Supported;               /**< TRUE if 3.3V supply is supported */
    BOOLEAN EightBitSupported;          /**< TRUE if 8-bit bus width is supported */
    BOOLEAN DriverTypeASupported;
    BOOLEAN DriverTypeCSupported;
    BOOLEAN DriverTypeDSupported;
    ULONG BaseClockFrequencyKhz;        /**< Base clock frequency in KHz */
    ULONG ClockMultiplier;
    ULONG RetuneTimer;
    UCHAR SlotType;
} SDPORT_CAPABILITIES, *PSDPORT_CAPABILITIES;

/**
 * @brief Per-slot private extension stored by the SDHCI miniport.
 */
typedef struct _SDHC_EXTENSION {
    PUCHAR BaseAddress;                 /**< Memory-mapped register base address */
    ULONG BaseLength;                   /**< Length of the register mapping in bytes */
    ULONG Capabilities;                 /**< SDHCI Capabilities register value */
    ULONG Capabilities2;                /**< SDHCI Capabilities 2 register value */
    ULONG BaseClockKhz;                 /**< Base clock frequency in KHz */
    ULONG CurrentClockKhz;              /**< Current SD clock frequency in KHz */
    UCHAR SpecVersion;                  /**< SDHCI specification version */
    BOOLEAN CrashdumpMode;              /**< TRUE if operating in crashdump mode */
    PVOID AdmaDescriptorBuffer;         /**< Virtual address of the ADMA2 descriptor table */
    PHYSICAL_ADDRESS AdmaDescriptorPhysical; /**< Physical address of the ADMA2 descriptor table */
    ULONG AdmaDescriptorSize;           /**< Size of the ADMA2 descriptor buffer in bytes */

    ULONG ConsecutiveErrorCount;

    ULONG SavedIntStatusEnable;
    ULONG SavedIntSignalEnable;
    UCHAR SavedTimeoutControl;
    UCHAR SavedPowerControl;
    USHORT SavedClockControl;
    UCHAR SavedHostControl;
    USHORT SavedHostControl2;

    ULONG TimeoutClockKhz;

    BOOLEAN Use64BitDma;

    ULONG QuirkFlags;

    UCHAR SavedSpeedMode;

    ULONG SavedClockKhz;

    UCHAR SavedBusWidth;
} SDHC_EXTENSION, *PSDHC_EXTENSION;

/*
 * SD port driver callback typedefs and initialization structure.
 * These mirror the definitions in sdport.h so the miniport can
 * call SdPortInitialize without including the port driver's internal header.
 */

typedef NTSTATUS (*PSDPORT_GET_SLOT_COUNT)(_In_ PVOID, _Out_ PUCHAR);
typedef VOID (*PSDPORT_GET_SLOT_CAPABILITIES)(_In_ PVOID, _In_ UCHAR, _Out_ PVOID);
typedef NTSTATUS (*PSDPORT_INITIALIZE)(_In_ PVOID, _In_ PHYSICAL_ADDRESS, _In_ PVOID, _In_ ULONG, _In_ BOOLEAN);
typedef NTSTATUS (*PSDPORT_ISSUE_BUS_OPERATION)(_In_ PVOID, _In_ PVOID);
typedef BOOLEAN (*PSDPORT_GET_CARD_DETECT_STATE)(_In_ PVOID);
typedef BOOLEAN (*PSDPORT_INTERRUPT)(_In_ PVOID, _Out_ PULONG, _Out_ PULONG, _Out_ PBOOLEAN, _Out_ PBOOLEAN, _Out_ PBOOLEAN);
typedef NTSTATUS (*PSDPORT_ISSUE_REQUEST)(_In_ PVOID, _In_ PVOID);
typedef VOID (*PSDPORT_REQUEST_DPC)(_In_ PVOID, _In_ PVOID, _In_ ULONG, _In_ ULONG);
typedef NTSTATUS (*PSDPORT_SAVE_CONTEXT)(_In_ PVOID);
typedef NTSTATUS (*PSDPORT_RESTORE_CONTEXT)(_In_ PVOID);
typedef VOID (*PSDPORT_TOGGLE_EVENTS)(_In_ PVOID, _In_ ULONG, _In_ BOOLEAN);
typedef VOID (*PSDPORT_CLEAR_EVENTS)(_In_ PVOID, _In_ ULONG);
typedef VOID (*PSDPORT_CLEANUP)(_In_ PVOID);

typedef struct _SDPORT_INITIALIZATION_DATA {
    ULONG                           StructureSize;
    PSDPORT_GET_SLOT_COUNT          GetSlotCount;
    PSDPORT_GET_SLOT_CAPABILITIES   GetSlotCapabilities;
    PSDPORT_INITIALIZE              Initialize;
    PSDPORT_ISSUE_BUS_OPERATION     IssueBusOperation;
    PSDPORT_GET_CARD_DETECT_STATE   GetCardDetectState;
    PSDPORT_INTERRUPT               Interrupt;
    PSDPORT_ISSUE_REQUEST           IssueRequest;
    PSDPORT_REQUEST_DPC             RequestDpc;
    PSDPORT_SAVE_CONTEXT            SaveContext;
    PSDPORT_RESTORE_CONTEXT         RestoreContext;
    PSDPORT_TOGGLE_EVENTS           ToggleEvents;
    PSDPORT_CLEAR_EVENTS            ClearEvents;
    PSDPORT_CLEANUP                 Cleanup;
    ULONG                           PrivateExtensionSize;
} SDPORT_INITIALIZATION_DATA, *PSDPORT_INITIALIZATION_DATA;

/** @brief Imported from sdport.sys -- register miniport with port driver. */
NTSTATUS
NTAPI
SdPortInitialize(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PSDPORT_INITIALIZATION_DATA InitializationData);

/* Miniport entry points */

/**
 * @brief Query the number of SD slots supported by this host controller.
 *
 * @param[in]  Extension  Pointer to the miniport slot extension.
 * @param[out] SlotCount  Receives the number of slots.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcGetSlotCount(
    _In_ PSDHC_EXTENSION Extension,
    _Out_ PUCHAR SlotCount);

/**
 * @brief Query the capabilities of a specific slot.
 *
 * @param[in]  Extension     Pointer to the miniport slot extension.
 * @param[in]  SlotIndex     Zero-based slot index.
 * @param[out] Capabilities  Receives the slot capabilities.
 */
VOID
SdhcGetSlotCapabilities(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR SlotIndex,
    _Out_ PSDPORT_CAPABILITIES Capabilities);

/**
 * @brief Initialize a host controller slot.
 *
 * @param[in,out] Extension      Pointer to the miniport slot extension.
 * @param[in]     PhysicalBase   Physical base address of the SDHCI registers.
 * @param[in]     VirtualBase    Memory-mapped virtual base address.
 * @param[in]     Length         Length of the register mapping in bytes.
 * @param[in]     CrashdumpMode  TRUE if initializing for crashdump.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcSlotInitialize(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PHYSICAL_ADDRESS PhysicalBase,
    _In_ PVOID VirtualBase,
    _In_ ULONG Length,
    _In_ BOOLEAN CrashdumpMode);

/**
 * @brief Issue a bus operation (reset, clock, voltage, width, speed).
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 * @param[in] Operation  Pointer to the bus operation descriptor.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcSlotIssueBusOperation(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_BUS_OPERATION Operation);

/**
 * @brief Check whether a card is inserted in the slot.
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 *
 * @return TRUE if a card is detected, FALSE otherwise.
 */
BOOLEAN
SdhcSlotGetCardDetectState(
    _In_ PSDHC_EXTENSION Extension);

/**
 * @brief Process a slot interrupt and determine its cause.
 *
 * @param[in]  Extension            Pointer to the miniport slot extension.
 * @param[out] Events               Receives the event bitmask.
 * @param[out] Errors               Receives the error bitmask.
 * @param[out] NotifyCardChange     Set to TRUE if a card insertion/removal occurred.
 * @param[out] NotifySdioInterrupt  Set to TRUE if an SDIO interrupt occurred.
 * @param[out] NotifyTuning         Set to TRUE if a tuning event occurred.
 *
 * @return TRUE if the interrupt was handled, FALSE otherwise.
 */
BOOLEAN
SdhcSlotInterrupt(
    _In_ PSDHC_EXTENSION Extension,
    _Out_ PULONG Events,
    _Out_ PULONG Errors,
    _Out_ PBOOLEAN NotifyCardChange,
    _Out_ PBOOLEAN NotifySdioInterrupt,
    _Out_ PBOOLEAN NotifyTuning);

/**
 * @brief Issue an SD command request to the host controller.
 *
 * @param[in]     Extension  Pointer to the miniport slot extension.
 * @param[in,out] Request    Pointer to the SD port request to issue.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcSlotIssueRequest(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request);

/**
 * @brief Complete request processing in the DPC after an interrupt.
 *
 * @param[in]     Extension  Pointer to the miniport slot extension.
 * @param[in,out] Request    Pointer to the in-progress SD port request.
 * @param[in]     Events     Event bitmask from the ISR.
 * @param[in]     Errors     Error bitmask from the ISR.
 */
VOID
SdhcSlotRequestDpc(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG Events,
    _In_ ULONG Errors);

/**
 * @brief Save the host controller context before a power transition.
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcSlotSaveContext(
    _In_ PSDHC_EXTENSION Extension);

/**
 * @brief Restore the host controller context after a power transition.
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcSlotRestoreContext(
    _In_ PSDHC_EXTENSION Extension);

/**
 * @brief Enable or disable specific interrupt events.
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 * @param[in] EventMask  Bitmask of events to toggle.
 * @param[in] Enable     TRUE to enable, FALSE to disable.
 */
VOID
SdhcSlotToggleEvents(
    _In_ PSDHC_EXTENSION Extension,
    _In_ ULONG EventMask,
    _In_ BOOLEAN Enable);

/**
 * @brief Clear pending interrupt event status bits.
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 * @param[in] EventMask  Bitmask of events to clear.
 */
VOID
SdhcSlotClearEvents(
    _In_ PSDHC_EXTENSION Extension,
    _In_ ULONG EventMask);

/**
 * @brief Clean up slot resources during device removal.
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 */
VOID
SdhcSlotCleanup(
    _In_ PSDHC_EXTENSION Extension);

/* Internal helpers */

/**
 * @brief Perform a software reset of the host controller.
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 * @param[in] ResetType  SDHCI reset type (all, CMD, or DAT line).
 *
 * @return STATUS_SUCCESS or STATUS_IO_TIMEOUT if the reset did not complete.
 */
NTSTATUS
SdhcResetHost(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR ResetType);

/**
 * @brief Set the SD clock frequency.
 *
 * @param[in] Extension     Pointer to the miniport slot extension.
 * @param[in] FrequencyKhz  Desired clock frequency in KHz.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcSetClock(
    _In_ PSDHC_EXTENSION Extension,
    _In_ ULONG FrequencyKhz);

/**
 * @brief Set the SD bus power voltage.
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 * @param[in] Voltage    Voltage level to set.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcSetVoltage(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR Voltage);

/**
 * @brief Set the SD bus width.
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 * @param[in] Width      Bus width in bits (1, 4, or 8).
 */
VOID
SdhcSetBusWidth(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR Width);

/**
 * @brief Set the SD bus speed mode.
 *
 * @param[in] Extension  Pointer to the miniport slot extension.
 * @param[in] SpeedMode  Speed mode identifier.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcSetBusSpeed(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR SpeedMode);

/**
 * @brief Send an SD command to the host controller hardware.
 *
 * @param[in]     Extension  Pointer to the miniport slot extension.
 * @param[in,out] Request    Pointer to the SD port request containing the command.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcSendCommand(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request);

/**
 * @brief Set up the ADMA2 descriptor table for a data transfer.
 *
 * @param[in]     Extension  Pointer to the miniport slot extension.
 * @param[in,out] Request    Pointer to the SD port request with the data MDL.
 *
 * @return STATUS_SUCCESS or STATUS_INSUFFICIENT_RESOURCES.
 */
NTSTATUS
SdhcSetupAdma2Transfer(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request);

/**
 * @brief Read the command response registers into the request structure.
 *
 * @param[in]     Extension  Pointer to the miniport slot extension.
 * @param[in,out] Request    Pointer to the SD port request to receive the response.
 */
VOID
SdhcReadResponse(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request);

/**
 * @brief Transfer data using programmed I/O (PIO) mode.
 *
 * @param[in]     Extension  Pointer to the miniport slot extension.
 * @param[in,out] Request    Pointer to the SD port request with the data buffer.
 * @param[in]     IsRead     TRUE for a read transfer, FALSE for a write.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SdhcTransferDataPio(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ BOOLEAN IsRead);

NTSTATUS
SdhcSlotSetTimeoutFromCsd(
    _In_ PSDHC_EXTENSION Extension,
    _In_ ULONG TaacNs,
    _In_ ULONG Nsac);

VOID
SdhcLookupQuirks(
    _In_ PSDHC_EXTENSION Extension,
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId);

NTSTATUS
NTAPI
SdhcIssueRequestFromBus(
    _In_ PVOID Context,
    _In_ PVOID Request);

NTSTATUS
SdhcRegisterBusCallbacks(
    _In_ PVOID FdoExt,
    _In_ PVOID MiniportExt,
    _In_ PVOID IssueRequestFunc);
