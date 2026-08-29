/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Public SD host-controller miniport interface
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct _SD_MINIPORT;
struct _SDPORT_CAPABILITIES;
struct _SDPORT_BUS_OPERATION;
struct _SDPORT_REQUEST;
struct _SDPORT_COMMAND;

#define SDPORT_EVENT_CARD_RESPONSE              0x00000001
#define SDPORT_EVENT_CARD_RW_END                0x00000002
#define SDPORT_EVENT_BLOCKGAP                   0x00000004
#define SDPORT_EVENT_DMA_COMPLETE               0x00000008
#define SDPORT_EVENT_BUFFER_EMPTY               0x00000010
#define SDPORT_EVENT_BUFFER_FULL                0x00000020
#define SDPORT_EVENT_CARD_CHANGE                0x000000C0
#define SDPORT_EVENT_CARD_INTERRUPT             0x00000100
#define SDPORT_EVENT_TUNING                     0x00001000
#define SDPORT_EVENT_ERROR                      0x00008000
#define SDPORT_EVENT_SG_LIST                    0x00010000
#define SDPORT_EVENT_DMA_RESOURCES_ALLOCATED    0x00020000
#define SDPORT_EVENT_DMA_TRANSFER_CANCELLED     0x00040000
#define SDPORT_EVENT_CLOCK_STABLE               0x00080000
#define SDPORT_EVENT_SIGNALING_VOLTAGE_STABLE   0x00100000
#define SDPORT_EVENT_VCORE_STABLE               0x00200000
#define SDPORT_EVENT_SYSTEM_DMA_COMPLETE        0x00400000
#define SDPORT_EVENT_HPI                        0x00800000
#define SDPORT_EVENT_PSTATE_CHANGED             0x01000000
#define SDPORT_EVENT_ALL                        0xFFFFFFFF

#define SDPORT_ERROR_CMD_TIMEOUT                0x00000001
#define SDPORT_ERROR_CMD_CRC_ERROR              0x00000002
#define SDPORT_ERROR_CMD_END_BIT_ERROR          0x00000004
#define SDPORT_ERROR_CMD_INDEX_ERROR            0x00000008
#define SDPORT_ERROR_DATA_TIMEOUT               0x00000010
#define SDPORT_ERROR_DATA_CRC_ERROR             0x00000020
#define SDPORT_ERROR_DATA_END_BIT_ERROR         0x00000040
#define SDPORT_ERROR_BUS_POWER_ERROR            0x00000080
#define SDPORT_ERROR_AUTO_CMD12_ERROR           0x00000100
#define SDPORT_ERROR_ADMA_ERROR                 0x00000200
#define SDPORT_ERROR_ACMD12_NOT_EXECUTED        0x00010000
#define SDPORT_ERROR_ACMD12_RESPONSE_TIMEOUT    0x00020000
#define SDPORT_ERROR_ACMD12_RESPONSE_CRC_ERROR  0x00040000
#define SDPORT_ERROR_ACMD12_END_BIT_ERROR       0x00080000
#define SDPORT_ERROR_ACMD12_INDEX_ERROR         0x00100000
#define SDPORT_ERROR_ACMD12_CWODAT_NOT_EXECUTED 0x00800000
#define SDPORT_ERROR_SYSTEM_DMA_ERROR           0x20000000
#define SDPORT_GENERIC_IO_ERROR                 0x40000000
#define SDPORT_ERROR_UNKNOWN                    0x80000000

typedef enum _SDPORT_BUS_TYPE
{
    SdBusTypeUndefined = -1,
    SdBusTypeAcpi,
    SdBusTypePci
} SDPORT_BUS_TYPE;

typedef NTSTATUS SDPORT_GET_SLOT_COUNT(struct _SD_MINIPORT *, PUCHAR);
typedef SDPORT_GET_SLOT_COUNT *PSDPORT_GET_SLOT_COUNT;
typedef VOID SDPORT_GET_SLOT_CAPABILITIES(PVOID, struct _SDPORT_CAPABILITIES *);
typedef SDPORT_GET_SLOT_CAPABILITIES *PSDPORT_GET_SLOT_CAPABILITIES;
typedef NTSTATUS SDPORT_INITIALIZE(PVOID, PHYSICAL_ADDRESS, PVOID, ULONG, BOOLEAN);
typedef SDPORT_INITIALIZE *PSDPORT_INITIALIZE;
typedef NTSTATUS SDPORT_ISSUE_BUS_OPERATION(PVOID, struct _SDPORT_BUS_OPERATION *);
typedef SDPORT_ISSUE_BUS_OPERATION *PSDPORT_ISSUE_BUS_OPERATION;
typedef BOOLEAN SDPORT_GET_CARD_DETECT_STATE(PVOID);
typedef SDPORT_GET_CARD_DETECT_STATE *PSDPORT_GET_CARD_DETECT_STATE;
typedef BOOLEAN SDPORT_GET_WRITE_PROTECT_STATE(PVOID);
typedef SDPORT_GET_WRITE_PROTECT_STATE *PSDPORT_GET_WRITE_PROTECT_STATE;
typedef BOOLEAN SDPORT_INTERRUPT(PVOID, PULONG, PULONG, PBOOLEAN, PBOOLEAN, PBOOLEAN);
typedef SDPORT_INTERRUPT *PSDPORT_INTERRUPT;
typedef NTSTATUS SDPORT_ISSUE_REQUEST(PVOID, struct _SDPORT_REQUEST *);
typedef SDPORT_ISSUE_REQUEST *PSDPORT_ISSUE_REQUEST;
typedef VOID SDPORT_GET_RESPONSE(PVOID, struct _SDPORT_COMMAND *, PVOID);
typedef SDPORT_GET_RESPONSE *PSDPORT_GET_RESPONSE;
typedef VOID SDPORT_REQUEST_DPC(PVOID, struct _SDPORT_REQUEST *, ULONG, ULONG);
typedef SDPORT_REQUEST_DPC *PSDPORT_REQUEST_DPC;
typedef VOID SDPORT_SAVE_CONTEXT(PVOID);
typedef SDPORT_SAVE_CONTEXT *PSDPORT_SAVE_CONTEXT;
typedef VOID SDPORT_RESTORE_CONTEXT(PVOID);
typedef SDPORT_RESTORE_CONTEXT *PSDPORT_RESTORE_CONTEXT;
typedef VOID SDPORT_TOGGLE_EVENTS(PVOID, ULONG, BOOLEAN);
typedef SDPORT_TOGGLE_EVENTS *PSDPORT_TOGGLE_EVENTS;
typedef VOID SDPORT_CLEAR_EVENTS(PVOID, ULONG);
typedef SDPORT_CLEAR_EVENTS *PSDPORT_CLEAR_EVENTS;
typedef NTSTATUS SDPORT_PO_FX_POWER_CONTROL_CALLBACK(
    struct _SD_MINIPORT *, LPCGUID, PVOID, SIZE_T, PVOID, SIZE_T, PSIZE_T);
typedef SDPORT_PO_FX_POWER_CONTROL_CALLBACK *PSDPORT_PO_FX_POWER_CONTROL_CALLBACK;
typedef VOID SDPORT_CLEANUP(struct _SD_MINIPORT *);
typedef SDPORT_CLEANUP *PSDPORT_CLEANUP;

typedef UCHAR SDPORT_COMMAND_INDEX;

typedef enum _SDPORT_COMMAND_TYPE
{
    SdCommandTypeUndefined = 0,
    SdCommandTypeSuspend,
    SdCommandTypeResume,
    SdCommandTypeAbort
} SDPORT_COMMAND_TYPE;

typedef enum _SDPORT_COMMAND_CLASS
{
    SdCommandClassUndefined = 0,
    SdCommandClassStandard,
    SdCommandClassApp
} SDPORT_COMMAND_CLASS;

typedef enum _SDPORT_RESPONSE_TYPE
{
    SdResponseTypeUndefined = 0,
    SdResponseTypeNone,
    SdResponseTypeR1,
    SdResponseTypeR1B,
    SdResponseTypeR2,
    SdResponseTypeR3,
    SdResponseTypeR4,
    SdResponseTypeR5,
    SdResponseTypeR5B,
    SdResponseTypeR6
} SDPORT_RESPONSE_TYPE;

typedef enum _SDPORT_TRANSFER_TYPE
{
    SdTransferTypeUndefined = 0,
    SdTransferTypeNone,
    SdTransferTypeSingleBlock,
    SdTransferTypeMultiBlock,
    SdTransferTypeMultiBlockNoStop
} SDPORT_TRANSFER_TYPE;

typedef enum _SDPORT_TRANSFER_DIRECTION
{
    SdTransferDirectionUndefined = 0,
    SdTransferDirectionRead,
    SdTransferDirectionWrite
} SDPORT_TRANSFER_DIRECTION;

typedef enum _SDPORT_TRANSFER_METHOD
{
    SdTransferMethodUndefined = 0,
    SdTransferMethodPio,
    SdTransferMethodSgDma
} SDPORT_TRANSFER_METHOD;

#define SDPORT_MAX_RESPONSE_LENGTH 16

typedef struct _SDPORT_COMMAND
{
    SDPORT_COMMAND_INDEX Index;
    SDPORT_COMMAND_TYPE Type;
    SDPORT_COMMAND_CLASS Class;
    SDPORT_RESPONSE_TYPE ResponseType;
    SDPORT_TRANSFER_TYPE TransferType;
    SDPORT_TRANSFER_DIRECTION TransferDirection;
    SDPORT_TRANSFER_METHOD TransferMethod;
    ULONG Argument;
    BOOLEAN UseAutoCmd12;
    BOOLEAN UseAutoCmd23;
    USHORT BlockSize;
    USHORT BlockCount;
    ULONG Length;
    PUCHAR DataBuffer;
    PVOID DmaVirtualAddress;
    PHYSICAL_ADDRESS DmaPhysicalAddress;
    PSCATTER_GATHER_LIST ScatterGatherList;
    ULONG ScatterGatherListSize;
} SDPORT_COMMAND, *PSDPORT_COMMAND;

typedef enum _SDPORT_REQUEST_TYPE
{
    SdRequestTypeUnspecified = 0,
    SdRequestTypeCommandNoTransfer,
    SdRequestTypeCommandWithTransfer,
    SdRequestTypeStartTransfer
} SDPORT_REQUEST_TYPE, *PSDPORT_REQUEST_TYPE;

typedef struct _SDPORT_REQUEST
{
    SDPORT_REQUEST_TYPE Type;
    SDPORT_COMMAND Command;
    ULONG RequiredEvents;
    NTSTATUS Status;
} SDPORT_REQUEST, *PSDPORT_REQUEST;

typedef enum _SDPORT_RESET_TYPE
{
    SdResetTypeUndefined = 0,
    SdResetTypeAll,
    SdResetTypeCmd,
    SdResetTypeDat
} SDPORT_RESET_TYPE;

typedef enum _SDPORT_BUS_VOLTAGE
{
    SdBusVoltageUndefined = 0,
    SdBusVoltage33,
    SdBusVoltage30,
    SdBusVoltage18,
    SdBusVoltageOff
} SDPORT_BUS_VOLTAGE;

typedef enum _SDPORT_BUS_WIDTH
{
    SdBusWidthUndefined = 0,
    SdBusWidth1Bit = 1,
    SdBusWidth4Bit = 4,
    SdBusWidth8Bit = 8
} SDPORT_BUS_WIDTH;

typedef enum _SDPORT_BUS_SPEED
{
    SdBusSpeedUndefined = 0,
    SdBusSpeedNormal,
    SdBusSpeedHigh,
    SdBusSpeedSDR12,
    SdBusSpeedSDR25,
    SdBusSpeedSDR50,
    SdBusSpeedDDR50,
    SdBusSpeedSDR104,
    SdBusSpeedHS200,
    SdBusSpeedHS400
} SDPORT_BUS_SPEED;

typedef enum _SDPORT_SIGNALING_VOLTAGE
{
    SdignalingVoltageUndefined = 0,
    SdSignalingVoltage33,
    SdSignalingVoltage18
} SDPORT_SIGNALING_VOLTAGE;

typedef enum _SDPORT_DRIVE_STRENGTH
{
    SdDriveStrengthUndefined = 0,
    SdDriveStrength200mA,
    SdDriveStrength400mA,
    SdDriveStrength600mA,
    SdDriveStrength800mA
} SDPORT_DRIVE_STRENGTH;

typedef enum _SDPORT_DRIVER_TYPE
{
    SdDriverTypeUndefined = 0,
    SdDriverTypeB,
    SdDriverTypeA,
    SdDriverTypeC,
    SdDriverTypeD
} SDPORT_DRIVER_TYPE;

typedef enum _SDPORT_BUS_OPERATION_TYPE
{
    SdBusOperationUndefined = 0,
    SdResetHw,
    SdResetHost,
    SdSetClock,
    SdSetVoltage,
    SdSetPower,
    SdSetBusWidth,
    SdSetBusSpeed,
    SdSetSignalingVoltage,
    SdSetDriveStrength,
    SdSetDriverType,
    SdSetPresetValue,
    SdSetBlockGapInterrupt,
    SdExecuteTuning
} SDPORT_BUS_OPERATION_TYPE;

typedef struct _SDPORT_BUS_OPERATION
{
    SDPORT_BUS_OPERATION_TYPE Type;
    union
    {
        SDPORT_RESET_TYPE ResetType;
        ULONG FrequencyKhz;
        SDPORT_BUS_VOLTAGE Voltage;
        BOOLEAN PowerEnabled;
        SDPORT_BUS_WIDTH BusWidth;
        SDPORT_BUS_SPEED BusSpeed;
        SDPORT_SIGNALING_VOLTAGE SignalingVoltage;
        UCHAR DriveStrength;
        SDPORT_DRIVER_TYPE DriverType;
        BOOLEAN PresetValueEnabled;
        BOOLEAN BlockGapIntEnabled;
    } Parameters;
} SDPORT_BUS_OPERATION, *PSDPORT_BUS_OPERATION;

typedef struct _SDPORT_INITIALIZATION_DATA
{
    ULONG StructureSize;
    PSDPORT_GET_SLOT_COUNT GetSlotCount;
    PSDPORT_GET_SLOT_CAPABILITIES GetSlotCapabilities;
    PSDPORT_INTERRUPT Interrupt;
    PSDPORT_ISSUE_REQUEST IssueRequest;
    PSDPORT_GET_RESPONSE GetResponse;
    PSDPORT_REQUEST_DPC RequestDpc;
    PSDPORT_TOGGLE_EVENTS ToggleEvents;
    PSDPORT_CLEAR_EVENTS ClearEvents;
    PSDPORT_SAVE_CONTEXT SaveContext;
    PSDPORT_RESTORE_CONTEXT RestoreContext;
    PSDPORT_INITIALIZE Initialize;
    PSDPORT_ISSUE_BUS_OPERATION IssueBusOperation;
    PSDPORT_GET_CARD_DETECT_STATE GetCardDetectState;
    PSDPORT_GET_WRITE_PROTECT_STATE GetWriteProtectState;
    PSDPORT_PO_FX_POWER_CONTROL_CALLBACK PowerControlCallback;
    PSDPORT_CLEANUP Cleanup;
    ULONG PrivateExtensionSize;
    BOOLEAN CrashdumpSupported;
} SDPORT_INITIALIZATION_DATA, *PSDPORT_INITIALIZATION_DATA;

#define MAX_SD_SLOTS 8

typedef struct _SDPORT_SLOT_EXTENSION
{
    struct _SD_MINIPORT *Miniport;
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) UCHAR PrivateExtension[0];
} SDPORT_SLOT_EXTENSION, *PSDPORT_SLOT_EXTENSION;

typedef struct _SDPORT_PCI_CONFIG_INFO
{
    ULONG VendorID;
    ULONG DeviceID;
    ULONG RevisionID;
    ULONG SubsysID;
} SDPORT_PCI_CONFIG_INFO, *PSDPORT_PCI_CONFIG_INFO;

typedef struct _SDPORT_CAPABILITIES
{
    UCHAR SpecVersion;
    UCHAR MaximumOutstandingRequests;
    USHORT MaximumBlockSize;
    USHORT MaximumBlockCount;
    ULONG BaseClockFrequencyKhz;
    ULONG TuningTimerCountInSeconds;
    ULONG DmaDescriptorSize;
    ULONG AlignmentRequirement;
    ULONG PioTransferMaxThreshold;
    struct
    {
        ULONG ScatterGatherDma:1;
        ULONG Address64Bit:1;
        ULONG BusWidth8Bit:1;
        ULONG HighSpeed:1;
        ULONG SignalingVoltage18V:1;
        ULONG SDR50:1;
        ULONG DDR50:1;
        ULONG SDR104:1;
        ULONG HS200:1;
        ULONG HS400:1;
        ULONG Reserved:5;
        ULONG DriverTypeA:1;
        ULONG DriverTypeB:1;
        ULONG DriverTypeC:1;
        ULONG DriverTypeD:1;
        ULONG TuningForSDR50:1;
        ULONG SoftwareTuning:1;
        ULONG AutoCmd12:1;
        ULONG AutoCmd23:1;
        ULONG Voltage18V:1;
        ULONG Voltage30V:1;
        ULONG Voltage33V:1;
        ULONG Limit200mA:1;
        ULONG Limit400mA:1;
        ULONG Limit600mA:1;
        ULONG Limit800mA:1;
        ULONG SaveContext:1;
        ULONG Reserved1:3;
    } Supported;
    struct
    {
        ULONG UsePioForRead:1;
        ULONG UsePioForWrite:1;
        ULONG Reserved:30;
    } Flags;
} SDPORT_CAPABILITIES, *PSDPORT_CAPABILITIES;

typedef struct _SDPORT_PORT_CONFIG_INFO
{
    PVOID DeviceObject;
    SDPORT_BUS_TYPE BusType;
    union
    {
        SDPORT_PCI_CONFIG_INFO PciConfigInfo;
    } BusInfo;
} SDPORT_PORT_CONFIG_INFO, *PSDPORT_PORT_CONFIG_INFO;

typedef struct _SD_MINIPORT
{
    PSDPORT_INITIALIZATION_DATA InitializationData;
    SDPORT_PORT_CONFIG_INFO ConfigurationInfo;
    UCHAR SlotCount;
    PSDPORT_SLOT_EXTENSION SlotExtensionList[MAX_SD_SLOTS];
} SD_MINIPORT, *PSD_MINIPORT;

NTSTATUS NTAPI SdPortInitialize(PVOID Argument1, PVOID Argument2,
                                PSDPORT_INITIALIZATION_DATA InitializationData);
VOID NTAPI SdPortCompleteRequest(PSDPORT_REQUEST Request, NTSTATUS Status);
NTSTATUS NTAPI SdPortPoFxPowerControl(PVOID PrivateExtension, LPCGUID PowerControlCode,
                                     PVOID InBuffer, SIZE_T InBufferSize,
                                     PVOID OutBuffer, SIZE_T OutBufferSize,
                                     PSIZE_T BytesReturned);
NTSTATUS NTAPI SdPortGetPciConfigSpace(PSD_MINIPORT Miniport, UCHAR Offset,
                                       PUCHAR Buffer, ULONG Length);
NTSTATUS NTAPI SdPortSetPciConfigSpace(PSD_MINIPORT Miniport, UCHAR Offset,
                                       PUCHAR Buffer, ULONG Length);
VOID NTAPI SdPortWait(ULONG Microseconds);
VOID NTAPI SdPortWriteRegisterUlong(PVOID BaseAddress, ULONG Register, ULONG Data);
VOID NTAPI SdPortWriteRegisterUshort(PVOID BaseAddress, ULONG Register, USHORT Data);
VOID NTAPI SdPortWriteRegisterUchar(PVOID BaseAddress, ULONG Register, UCHAR Data);
ULONG NTAPI SdPortReadRegisterUlong(PVOID BaseAddress, ULONG Register);
USHORT NTAPI SdPortReadRegisterUshort(PVOID BaseAddress, ULONG Register);
UCHAR NTAPI SdPortReadRegisterUchar(PVOID BaseAddress, ULONG Register);
VOID NTAPI SdPortReadRegisterBufferUlong(PVOID BaseAddress, ULONG Register,
                                         PULONG Buffer, ULONG Length);
VOID NTAPI SdPortReadRegisterBufferUshort(PVOID BaseAddress, ULONG Register,
                                          PUSHORT Buffer, ULONG Length);
VOID NTAPI SdPortReadRegisterBufferUchar(PVOID BaseAddress, ULONG Register,
                                         PUCHAR Buffer, ULONG Length);
VOID NTAPI SdPortWriteRegisterBufferUlong(PVOID BaseAddress, ULONG Register,
                                          PULONG Buffer, ULONG Length);
VOID NTAPI SdPortWriteRegisterBufferUshort(PVOID BaseAddress, ULONG Register,
                                           PUSHORT Buffer, ULONG Length);
VOID NTAPI SdPortWriteRegisterBufferUchar(PVOID BaseAddress, ULONG Register,
                                          PUCHAR Buffer, ULONG Length);

#ifdef __cplusplus
}
#endif
