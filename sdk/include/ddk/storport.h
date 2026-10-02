/*
 * storport.h
 *
 * StorPort interface
 *
 * This file is part of the w32api package.
 *
 * Contributors:
 *   Created by Casper S. Hornstrup <chorns@users.sourceforge.net>
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#ifdef _NTSCSI_
  #error STORPORT.H must be included instead of SCSI.H
#endif

#ifdef _NTSRB_
  #error STORPORT.H must be included instead of SRB.H
#endif

#ifndef _NTSTORPORT_
#define _NTSTORPORT_

#ifdef __cplusplus
extern "C" {
#endif

#if (DBG)

#define DebugPrint(x) StorPortDebugPrint x

#endif
#if !(DBG)

#define DebugPrint(x)

#endif

#define SCSI_MAXIMUM_BUSES_PER_ADAPTER 255

#define SCSI_MAXIMUM_TARGETS_PER_BUS 128

#define SCSI_MAXIMUM_LUNS_PER_TARGET 255

#define SCSI_MINIMUM_PHYSICAL_BREAKS  16

#define SCSI_MAXIMUM_PHYSICAL_BREAKS 255

#define SCSI_MAXIMUM_BUSES 8

#define SCSI_MAXIMUM_TARGETS 8

#define SCSI_MAXIMUM_LOGICAL_UNITS 8

#define SP_UNINITIALIZED_VALUE ((ULONG) ~0)

#define SP_UNTAGGED ((UCHAR) ~0)

typedef struct _SCSI_REQUEST_BLOCK {
    USHORT Length;
    UCHAR Function;
    UCHAR SrbStatus;
    UCHAR ScsiStatus;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR QueueTag;
    UCHAR QueueAction;
    UCHAR CdbLength;
    UCHAR SenseInfoBufferLength;
    ULONG SrbFlags;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    _Field_size_bytes_(DataTransferLength)
    PVOID DataBuffer;
    PVOID SenseInfoBuffer;
    struct _SCSI_REQUEST_BLOCK *NextSrb;
    PVOID OriginalRequest;
    PVOID SrbExtension;
    union {
        ULONG InternalStatus;
        ULONG QueueSortKey;
        ULONG LinkTimeoutValue;
    };
#if defined(_WIN64)
    ULONG Reserved;
#endif
    UCHAR Cdb[16];
} SCSI_REQUEST_BLOCK, *PSCSI_REQUEST_BLOCK;

#define SCSI_REQUEST_BLOCK_SIZE sizeof(SCSI_REQUEST_BLOCK)

typedef struct _SCSI_WMI_REQUEST_BLOCK {
    USHORT Length;
    UCHAR Function;
    UCHAR SrbStatus;
    UCHAR WMISubFunction;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR Reserved1;
    UCHAR WMIFlags;
    UCHAR Reserved2[2];
    ULONG SrbFlags;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    PVOID DataBuffer;
    PVOID DataPath;
    PVOID Reserved3;
    PVOID OriginalRequest;
    PVOID SrbExtension;
    ULONG Reserved4;
#if (NTDDI_VERSION >= NTDDI_WS03SP1)
#if defined(_WIN64)
    ULONG Reserved6;
#endif
#endif
    UCHAR Reserved5[16];
} SCSI_WMI_REQUEST_BLOCK, *PSCSI_WMI_REQUEST_BLOCK;

typedef enum _STOR_DEVICE_POWER_STATE {
    StorPowerDeviceUnspecified = 0,
    StorPowerDeviceD0,
    StorPowerDeviceD1,
    StorPowerDeviceD2,
    StorPowerDeviceD3,
    StorPowerDeviceMaximum
} STOR_DEVICE_POWER_STATE, *PSTOR_DEVICE_POWER_STATE;

typedef enum {
    StorPowerActionNone = 0,
    StorPowerActionReserved,
    StorPowerActionSleep,
    StorPowerActionHibernate,
    StorPowerActionShutdown,
    StorPowerActionShutdownReset,
    StorPowerActionShutdownOff,
    StorPowerActionWarmEject
} STOR_POWER_ACTION, *PSTOR_POWER_ACTION;

typedef struct _SCSI_POWER_REQUEST_BLOCK {
    USHORT Length;
    UCHAR Function;
    UCHAR SrbStatus;
    UCHAR SrbPowerFlags;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    STOR_DEVICE_POWER_STATE DevicePowerState;
    ULONG SrbFlags;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    PVOID DataBuffer;
    PVOID SenseInfoBuffer;
    struct _SCSI_REQUEST_BLOCK *NextSrb;
    PVOID OriginalRequest;
    PVOID SrbExtension;
    STOR_POWER_ACTION PowerAction;
#if defined(_WIN64)
    ULONG Reserved;
#endif
    UCHAR Reserved5[16];
} SCSI_POWER_REQUEST_BLOCK, *PSCSI_POWER_REQUEST_BLOCK;

typedef enum {
    StorStartDevice = 0x0,
    StorRemoveDevice = 0x2,
    StorStopDevice  = 0x4,
    StorQueryCapabilities = 0x9,
    StorQueryResourceRequirements = 0xB,
    StorFilterResourceRequirements = 0xD,
    StorSurpriseRemoval = 0x17
} STOR_PNP_ACTION, *PSTOR_PNP_ACTION;

typedef struct _STOR_DEVICE_CAPABILITIES {
    USHORT Version;
    ULONG  DeviceD1:1;
    ULONG  DeviceD2:1;
    ULONG  LockSupported:1;
    ULONG  EjectSupported:1;
    ULONG  Removable:1;
    ULONG  DockDevice:1;
    ULONG  UniqueID:1;
    ULONG  SilentInstall:1;
    ULONG  SurpriseRemovalOK:1;
    ULONG  NoDisplayInUI:1;
} STOR_DEVICE_CAPABILITIES, *PSTOR_DEVICE_CAPABILITIES;

typedef struct _STOR_DEVICE_CAPABILITIES_EX {
    USHORT Version;
    USHORT Size;
    ULONG  DeviceD1:1;
    ULONG  DeviceD2:1;
    ULONG  LockSupported:1;
    ULONG  EjectSupported:1;
    ULONG  Removable:1;
    ULONG  DockDevice:1;
    ULONG  UniqueID:1;
    ULONG  SilentInstall:1;
    ULONG  RawDeviceOK:1;
    ULONG  SurpriseRemovalOK:1;
    ULONG  NoDisplayInUI:1;
    ULONG  DefaultWriteCacheEnabled:1;
    ULONG  Reserved0:20;
    ULONG  Address;
    ULONG  UINumber;
    ULONG  Reserved1[2];
} STOR_DEVICE_CAPABILITIES_EX, *PSTOR_DEVICE_CAPABILITIES_EX;

typedef struct _SCSI_PNP_REQUEST_BLOCK {
    USHORT Length;
    UCHAR Function;
    UCHAR SrbStatus;
    UCHAR PnPSubFunction;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    STOR_PNP_ACTION PnPAction;
    ULONG SrbFlags;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    PVOID DataBuffer;
    PVOID SenseInfoBuffer;
    struct _SCSI_REQUEST_BLOCK *NextSrb;
    PVOID OriginalRequest;
    PVOID SrbExtension;
    ULONG SrbPnPFlags;
#if defined(_WIN64)
    ULONG Reserved;
#endif
        UCHAR Reserved4[16];
} SCSI_PNP_REQUEST_BLOCK, *PSCSI_PNP_REQUEST_BLOCK;

#define SRB_FUNCTION_EXECUTE_SCSI           0x00

#define SRB_FUNCTION_CLAIM_DEVICE           0x01

#define SRB_FUNCTION_IO_CONTROL             0x02

#define SRB_FUNCTION_RECEIVE_EVENT          0x03

#define SRB_FUNCTION_RELEASE_QUEUE          0x04

#define SRB_FUNCTION_ATTACH_DEVICE          0x05

#define SRB_FUNCTION_RELEASE_DEVICE         0x06

#define SRB_FUNCTION_SHUTDOWN               0x07

#define SRB_FUNCTION_FLUSH                  0x08

#define SRB_FUNCTION_ABORT_COMMAND          0x10

#define SRB_FUNCTION_RELEASE_RECOVERY       0x11

#define SRB_FUNCTION_RESET_BUS              0x12

#define SRB_FUNCTION_RESET_DEVICE           0x13

#define SRB_FUNCTION_TERMINATE_IO           0x14

#define SRB_FUNCTION_FLUSH_QUEUE            0x15

#define SRB_FUNCTION_REMOVE_DEVICE          0x16

#define SRB_FUNCTION_WMI                    0x17

#define SRB_FUNCTION_LOCK_QUEUE             0x18

#define SRB_FUNCTION_UNLOCK_QUEUE           0x19

#define SRB_FUNCTION_RESET_LOGICAL_UNIT     0x20

#define SRB_FUNCTION_SET_LINK_TIMEOUT       0x21

#define SRB_FUNCTION_LINK_TIMEOUT_OCCURRED  0x22

#define SRB_FUNCTION_LINK_TIMEOUT_COMPLETE  0x23

#define SRB_FUNCTION_POWER                  0x24

#define SRB_FUNCTION_PNP                    0x25

#define SRB_FUNCTION_DUMP_POINTERS          0x26

#define SRB_FUNCTION_FREE_DUMP_POINTERS     0x27

#define SRB_FUNCTION_STORAGE_REQUEST_BLOCK  0x28

#define SRB_STATUS_PENDING                  0x00

#define SRB_STATUS_SUCCESS                  0x01

#define SRB_STATUS_ABORTED                  0x02

#define SRB_STATUS_ABORT_FAILED             0x03

#define SRB_STATUS_ERROR                    0x04

#define SRB_STATUS_BUSY                     0x05

#define SRB_STATUS_INVALID_REQUEST          0x06

#define SRB_STATUS_INVALID_PATH_ID          0x07

#define SRB_STATUS_NO_DEVICE                0x08

#define SRB_STATUS_TIMEOUT                  0x09

#define SRB_STATUS_SELECTION_TIMEOUT        0x0A

#define SRB_STATUS_COMMAND_TIMEOUT          0x0B

#define SRB_STATUS_MESSAGE_REJECTED         0x0D

#define SRB_STATUS_BUS_RESET                0x0E

#define SRB_STATUS_PARITY_ERROR             0x0F

#define SRB_STATUS_REQUEST_SENSE_FAILED     0x10

#define SRB_STATUS_NO_HBA                   0x11

#define SRB_STATUS_DATA_OVERRUN             0x12

#define SRB_STATUS_UNEXPECTED_BUS_FREE      0x13

#define SRB_STATUS_PHASE_SEQUENCE_FAILURE   0x14

#define SRB_STATUS_BAD_SRB_BLOCK_LENGTH     0x15

#define SRB_STATUS_REQUEST_FLUSHED          0x16

#define SRB_STATUS_INVALID_LUN              0x20

#define SRB_STATUS_INVALID_TARGET_ID        0x21

#define SRB_STATUS_BAD_FUNCTION             0x22

#define SRB_STATUS_ERROR_RECOVERY           0x23

#define SRB_STATUS_NOT_POWERED              0x24

#define SRB_STATUS_LINK_DOWN                0x25

#define SRB_STATUS_INTERNAL_ERROR           0x30

#define SRB_STATUS_QUEUE_FROZEN             0x40

#define SRB_STATUS_AUTOSENSE_VALID          0x80

#define SRB_STATUS(Status) (Status & ~(SRB_STATUS_AUTOSENSE_VALID | SRB_STATUS_QUEUE_FROZEN))

#define SRB_FLAGS_QUEUE_ACTION_ENABLE       0x00000002

#define SRB_FLAGS_DISABLE_DISCONNECT        0x00000004

#define SRB_FLAGS_DISABLE_SYNCH_TRANSFER    0x00000008

#define SRB_FLAGS_BYPASS_FROZEN_QUEUE       0x00000010

#define SRB_FLAGS_DISABLE_AUTOSENSE         0x00000020

#define SRB_FLAGS_DATA_IN                   0x00000040

#define SRB_FLAGS_DATA_OUT                  0x00000080

#define SRB_FLAGS_NO_DATA_TRANSFER          0x00000000

#define SRB_FLAGS_UNSPECIFIED_DIRECTION     (SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT)

#define SRB_FLAGS_NO_QUEUE_FREEZE           0x00000100

#define SRB_FLAGS_ADAPTER_CACHE_ENABLE      0x00000200

#define SRB_FLAGS_FREE_SENSE_BUFFER         0x00000400

#define SRB_FLAGS_D3_PROCESSING             0x00000800

#define SRB_FLAGS_IS_ACTIVE                 0x00010000

#define SRB_FLAGS_ALLOCATED_FROM_ZONE       0x00020000

#define SRB_FLAGS_SGLIST_FROM_POOL          0x00040000

#define SRB_FLAGS_BYPASS_LOCKED_QUEUE       0x00080000

#define SRB_FLAGS_NO_KEEP_AWAKE             0x00100000

#define SRB_FLAGS_PORT_DRIVER_ALLOCSENSE    0x00200000

#define SRB_FLAGS_PORT_DRIVER_SENSEHASPORT  0x00400000

#define SRB_FLAGS_DONT_START_NEXT_PACKET    0x00800000

#define SRB_FLAGS_PORT_DRIVER_RESERVED      0x0F000000

#define SRB_FLAGS_CLASS_DRIVER_RESERVED     0xF0000000

#define SRB_SIMPLE_TAG_REQUEST              0x20

#define SRB_HEAD_OF_QUEUE_TAG_REQUEST       0x21

#define SRB_ORDERED_QUEUE_TAG_REQUEST       0x22

#define SRB_WMI_FLAGS_ADAPTER_REQUEST       0x01

#define SRB_POWER_FLAGS_ADAPTER_REQUEST     0x01

#define SRB_PNP_FLAGS_ADAPTER_REQUEST       0x01

#define SRB_IOCTL_FLAGS_ADAPTER_REQUEST     0x01

#if ((NTDDI_VERSION >= NTDDI_WIN8))
#if (defined(_WIN64) || defined(_M_ALPHA))

#define SRB_ALIGN           DECLSPEC_ALIGN(8)

#define STOR_ADDRESS_ALIGN  DECLSPEC_ALIGN(8)

#define POINTER_ALIGN       DECLSPEC_ALIGN(8)

#endif
#if !(defined(_WIN64) || defined(_M_ALPHA))

#define SRB_ALIGN

#define STOR_ADDRESS_ALIGN

#define POINTER_ALIGN

#endif

typedef enum _SRBEXDATATYPE {
    SrbExDataTypeUnknown = 0,
    SrbExDataTypeBidirectional,
    SrbExDataTypeScsiCdb16 = 0x40,
    SrbExDataTypeScsiCdb32,
    SrbExDataTypeScsiCdbVar,
    SrbExDataTypeNvmeCommand,
    SrbExDataTypeNvmeofOperation,
    SrbExDataTypeWmi = 0x60,
    SrbExDataTypePower,
    SrbExDataTypePnP,
    SrbExDataTypeIoInfo = 0x80,
    SrbExDataTypePassthroughDirect = 0xa0,
    SrbExDataTypeMSReservedStart = 0xf0000000,
    SrbExDataTypeReserved = 0xffffffff
} SRBEXDATATYPE, *PSRBEXDATATYPE;

typedef struct SRB_ALIGN _SRBEX_DATA {
    SRBEXDATATYPE Type;
    ULONG Length;
    _Field_size_bytes_(Length) UCHAR Data[ANYSIZE_ARRAY];
} SRBEX_DATA, *PSRBEX_DATA;

#define SRBEX_DATA_SCSI_CDB16_LENGTH ((20 * sizeof(UCHAR)) + sizeof(ULONG) + sizeof(PVOID))

typedef struct SRB_ALIGN _SRBEX_DATA_SCSI_CDB16 {
    _Field_range_(SrbExDataTypeScsiCdb16, SrbExDataTypeScsiCdb16)
    SRBEXDATATYPE Type;
    _Field_range_(SRBEX_DATA_SCSI_CDB16_LENGTH, SRBEX_DATA_SCSI_CDB16_LENGTH)
    ULONG Length;
    UCHAR ScsiStatus;
    UCHAR SenseInfoBufferLength;
    UCHAR CdbLength;
    UCHAR Reserved;
    ULONG Reserved1;
    _Field_size_bytes_full_(SenseInfoBufferLength)
    PVOID POINTER_ALIGN SenseInfoBuffer;
    UCHAR POINTER_ALIGN Cdb[16];
} SRBEX_DATA_SCSI_CDB16, *PSRBEX_DATA_SCSI_CDB16;

#define SRBEX_DATA_SCSI_CDB32_LENGTH ((36 * sizeof(UCHAR)) + sizeof(ULONG) + sizeof(PVOID))

typedef struct SRB_ALIGN _SRBEX_DATA_SCSI_CDB32 {
    _Field_range_(SrbExDataTypeScsiCdb32, SrbExDataTypeScsiCdb32)
    SRBEXDATATYPE Type;
    _Field_range_(SRBEX_DATA_SCSI_CDB32_LENGTH, SRBEX_DATA_SCSI_CDB32_LENGTH)
    ULONG Length;
    UCHAR ScsiStatus;
    UCHAR SenseInfoBufferLength;
    UCHAR CdbLength;
    UCHAR Reserved;
    ULONG Reserved1;
    _Field_size_bytes_full_(SenseInfoBufferLength)
    PVOID POINTER_ALIGN SenseInfoBuffer;
    UCHAR POINTER_ALIGN Cdb[32];
} SRBEX_DATA_SCSI_CDB32, *PSRBEX_DATA_SCSI_CDB32;

#define SRBEX_DATA_SCSI_CDB_VAR_LENGTH_MIN ((4 * sizeof(UCHAR)) + (3 * sizeof(ULONG)) + sizeof(PVOID))

#define SRBEX_DATA_SCSI_CDB_VAR_LENGTH_MAX 0xffffffffUL

typedef struct SRB_ALIGN _SRBEX_DATA_SCSI_CDB_VAR {
    _Field_range_(SrbExDataTypeScsiCdbVar, SrbExDataTypeScsiCdbVar)
    SRBEXDATATYPE Type;
    _Field_range_(SRBEX_DATA_SCSI_CDB_VAR_LENGTH_MIN, SRBEX_DATA_SCSI_CDB_VAR_LENGTH_MAX)
    ULONG Length;
    UCHAR ScsiStatus;
    UCHAR SenseInfoBufferLength;
    UCHAR Reserved[2];
    ULONG CdbLength;
    ULONG Reserved1[2];
    _Field_size_bytes_full_(SenseInfoBufferLength)
    PVOID POINTER_ALIGN SenseInfoBuffer;
    _Field_size_bytes_full_(CdbLength)
    UCHAR POINTER_ALIGN Cdb[ANYSIZE_ARRAY];
} SRBEX_DATA_SCSI_CDB_VAR, *PSRBEX_DATA_SCSI_CDB_VAR;

#define SRBEX_DATA_WMI_LENGTH ((4 * sizeof(UCHAR)) + sizeof(ULONG) + sizeof(PVOID))

typedef struct SRB_ALIGN _SRBEX_DATA_WMI {
    _Field_range_(SrbExDataTypeWmi, SrbExDataTypeWmi)
    SRBEXDATATYPE Type;
    _Field_range_(SRBEX_DATA_WMI_LENGTH, SRBEX_DATA_WMI_LENGTH)
    ULONG Length;
    UCHAR WMISubFunction;
    UCHAR WMIFlags;
    UCHAR Reserved[2];
    ULONG Reserved1;
    PVOID POINTER_ALIGN DataPath;
} SRBEX_DATA_WMI, *PSRBEX_DATA_WMI;

#define SRBEX_DATA_POWER_LENGTH ((4 * sizeof(UCHAR)) + sizeof(STOR_DEVICE_POWER_STATE) + sizeof(STOR_POWER_ACTION))

typedef struct SRB_ALIGN _SRBEX_DATA_POWER {
    _Field_range_(SrbExDataTypePower, SrbExDataTypePower)
    SRBEXDATATYPE Type;
    _Field_range_(SRBEX_DATA_POWER_LENGTH, SRBEX_DATA_POWER_LENGTH)
    ULONG Length;
    UCHAR SrbPowerFlags;
    UCHAR Reserved[3];
    STOR_DEVICE_POWER_STATE DevicePowerState;
    STOR_POWER_ACTION PowerAction;
} SRBEX_DATA_POWER, *PSRBEX_DATA_POWER;

#define SRBEX_DATA_PNP_LENGTH ((4 * sizeof(UCHAR)) + sizeof(STOR_PNP_ACTION) + (2 * sizeof(ULONG)))

typedef struct SRB_ALIGN _SRBEX_DATA_PNP {
    _Field_range_(SrbExDataTypePnP, SrbExDataTypePnP)
    SRBEXDATATYPE Type;
    _Field_range_(SRBEX_DATA_PNP_LENGTH, SRBEX_DATA_PNP_LENGTH)
    ULONG Length;
    UCHAR PnPSubFunction;
    UCHAR Reserved[3];
    STOR_PNP_ACTION PnPAction;
    ULONG SrbPnPFlags;
    ULONG Reserved1;
} SRBEX_DATA_PNP, *PSRBEX_DATA_PNP;

#define SRBEX_DATA_IO_INFO_LENGTH ((5 * sizeof(ULONG)) + (4 * sizeof(UCHAR)))

#define REQUEST_INFO_PAGING_IO_FLAG                 0x00000002

#define REQUEST_INFO_HYBRID_WRITE_THROUGH_FLAG      0x00000020

#define REQUEST_INFO_VALID_CACHEPRIORITY_FLAG       0x80000000

typedef struct SRB_ALIGN _SRBEX_DATA_IO_INFO {
    _Field_range_(SrbExDataTypeIoInfo, SrbExDataTypeIoInfo)
    SRBEXDATATYPE Type;
    _Field_range_(SRBEX_DATA_IO_INFO_LENGTH, SRBEX_DATA_IO_INFO_LENGTH)
    ULONG Length;
    ULONG Flags;
    ULONG Key;
    ULONG RWLength;
    BOOLEAN IsWriteRequest;
    UCHAR CachePriority;
    UCHAR Reserved[2];
    ULONG Reserved1[2];
} SRBEX_DATA_IO_INFO, *PSRBEX_DATA_IO_INFO;

#define SRB_SIGNATURE 0x53524258

#define STORAGE_REQUEST_BLOCK_VERSION_1    0x1

typedef struct SRB_ALIGN _STORAGE_REQUEST_BLOCK_HEADER {
    USHORT Length;
    _Field_range_(SRB_FUNCTION_STORAGE_REQUEST_BLOCK, SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    UCHAR Function;
    UCHAR SrbStatus;
} STORAGE_REQUEST_BLOCK_HEADER, *PSTORAGE_REQUEST_BLOCK_HEADER;

typedef _Struct_size_bytes_(SrbLength) struct SRB_ALIGN _STORAGE_REQUEST_BLOCK {
    USHORT Length;
    _Field_range_(SRB_FUNCTION_STORAGE_REQUEST_BLOCK, SRB_FUNCTION_STORAGE_REQUEST_BLOCK)
    UCHAR Function;
    UCHAR SrbStatus;
    ULONG ReservedUlong1;
    _Field_range_(SRB_SIGNATURE, SRB_SIGNATURE)
    ULONG Signature;
    _Field_range_(STORAGE_REQUEST_BLOCK_VERSION_1, STORAGE_REQUEST_BLOCK_VERSION_1)
    ULONG Version;
    ULONG SrbLength;
    ULONG SrbFunction;
    ULONG SrbFlags;
    ULONG ReservedUlong2;
    ULONG RequestTag;
    USHORT RequestPriority;
    USHORT RequestAttribute;
    ULONG TimeOutValue;
#if (NTDDI_VERSION >= NTDDI_WIN10_CU)
    union {
        ULONG SystemStatus;
        ULONG RequestTagHigh4Bytes;
    } DUMMYUNIONNAME;
#else
    ULONG SystemStatus;
#endif
    ULONG ZeroGuard1;
    _Field_range_(sizeof(STORAGE_REQUEST_BLOCK), SrbLength - sizeof(STOR_ADDRESS))
    ULONG AddressOffset;
    ULONG NumSrbExData;
    ULONG DataTransferLength;
    _Field_size_bytes_full_(DataTransferLength)
    PVOID POINTER_ALIGN DataBuffer;
    PVOID POINTER_ALIGN ZeroGuard2;
    PVOID POINTER_ALIGN OriginalRequest;
    PVOID POINTER_ALIGN ClassContext;
    PVOID POINTER_ALIGN PortContext;
    PVOID POINTER_ALIGN MiniportContext;
    struct _STORAGE_REQUEST_BLOCK POINTER_ALIGN *NextSrb;
    _At_buffer_(SrbExDataOffset, _Iter_, NumSrbExData, _Field_range_(0, SrbLength - sizeof(SRBEX_DATA)))
    _Field_size_(NumSrbExData)
    ULONG SrbExDataOffset[ANYSIZE_ARRAY];
} STORAGE_REQUEST_BLOCK, *PSTORAGE_REQUEST_BLOCK;

#define SRB_TYPE_SCSI_REQUEST_BLOCK         0

#define SRB_TYPE_STORAGE_REQUEST_BLOCK      1

#endif

#include <pshpack1.h>
typedef union _CDB {
    struct _CDB6GENERIC {
       UCHAR  OperationCode;
       UCHAR  Immediate : 1;
       UCHAR  CommandUniqueBits : 4;
       UCHAR  LogicalUnitNumber : 3;
       UCHAR  CommandUniqueBytes[3];
       UCHAR  Link : 1;
       UCHAR  Flag : 1;
       UCHAR  Reserved : 4;
       UCHAR  VendorUnique : 2;
    } CDB6GENERIC;
    struct _CDB6READWRITE {
        UCHAR OperationCode;
        UCHAR LogicalBlockMsb1 : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR LogicalBlockMsb0;
        UCHAR LogicalBlockLsb;
        UCHAR TransferBlocks;
        UCHAR Control;
    } CDB6READWRITE;
    struct _CDB6INQUIRY {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR PageCode;
        UCHAR IReserved;
        UCHAR AllocationLength;
        UCHAR Control;
    } CDB6INQUIRY;
    struct _CDB6INQUIRY3 {
        UCHAR OperationCode;
        UCHAR EnableVitalProductData : 1;
        UCHAR CommandSupportData : 1;
        UCHAR Reserved1 : 6;
        UCHAR PageCode;
        UCHAR Reserved2;
        UCHAR AllocationLength;
        UCHAR Control;
    } CDB6INQUIRY3;
    struct _CDB6VERIFY {
        UCHAR OperationCode;
        UCHAR Fixed : 1;
        UCHAR ByteCompare : 1;
        UCHAR Immediate : 1;
        UCHAR Reserved : 2;
        UCHAR LogicalUnitNumber : 3;
        UCHAR VerificationLength[3];
        UCHAR Control;
    } CDB6VERIFY;
    struct _RECEIVE_DIAGNOSTIC {
        UCHAR OperationCode;
        UCHAR PageCodeValid : 1;
        UCHAR Reserved : 7;
        UCHAR PageCode;
        UCHAR AllocationLength[2];
        UCHAR Control;
    } RECEIVE_DIAGNOSTIC;
    struct _SEND_DIAGNOSTIC {
        UCHAR OperationCode;
        UCHAR UnitOffline : 1;
        UCHAR DeviceOffline : 1;
        UCHAR SelfTest : 1;
        UCHAR Reserved1 : 1;
        UCHAR PageFormat : 1;
        UCHAR SelfTestCode: 3;
        UCHAR Reserved2;
        UCHAR ParameterListLength[2];
        UCHAR Control;
    } SEND_DIAGNOSTIC;
    struct _CDB6FORMAT {
        UCHAR OperationCode;
        UCHAR FormatControl : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR FReserved1;
        UCHAR InterleaveMsb;
        UCHAR InterleaveLsb;
        UCHAR FReserved2;
    } CDB6FORMAT;
    struct _CDB10 {
        UCHAR OperationCode;
        UCHAR RelativeAddress : 1;
        UCHAR Reserved1 : 2;
        UCHAR ForceUnitAccess : 1;
        UCHAR DisablePageOut : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR LogicalBlockByte0;
        UCHAR LogicalBlockByte1;
        UCHAR LogicalBlockByte2;
        UCHAR LogicalBlockByte3;
        UCHAR Reserved2;
        UCHAR TransferBlocksMsb;
        UCHAR TransferBlocksLsb;
        UCHAR Control;
    } CDB10;
    struct _CDB12 {
        UCHAR OperationCode;
        UCHAR RelativeAddress : 1;
        UCHAR Reserved1 : 2;
        UCHAR ForceUnitAccess : 1;
        UCHAR DisablePageOut : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR LogicalBlock[4];
        UCHAR TransferLength[4];
        UCHAR Reserved2;
        UCHAR Control;
    } CDB12;
    struct _CDB16 {
        UCHAR OperationCode;
        UCHAR Reserved1        : 3;
        UCHAR ForceUnitAccess  : 1;
        UCHAR DisablePageOut   : 1;
        UCHAR Protection       : 3;
        UCHAR LogicalBlock[8];
        UCHAR TransferLength[4];
        UCHAR Reserved2;
        UCHAR Control;
    } CDB16;
    struct _READ_BUFFER_10 {
        UCHAR OperationCode;
        UCHAR Mode : 5;
        UCHAR ModeSpecific : 3;
        UCHAR BufferId;
        UCHAR BufferOffset[3];
        UCHAR AllocationLength[3];
        UCHAR Control;
    } READ_BUFFER_10;
    struct _READ_BUFFER_16 {
        UCHAR OperationCode;
        UCHAR Mode : 5;
        UCHAR ModeSpecific : 3;
        UCHAR BufferOffset[8];
        UCHAR AllocationLength[4];
        UCHAR BufferId;
        UCHAR Control;
    } READ_BUFFER_16;
    struct _SECURITY_PROTOCOL_IN {
        UCHAR OperationCode;
        UCHAR SecurityProtocol;
        UCHAR SecurityProtocolSpecific[2];
        UCHAR Reserved1 : 7;
        UCHAR INC_512 : 1;
        UCHAR Reserved2;
        UCHAR AllocationLength[4];
        UCHAR Reserved3;
        UCHAR Control;
    } SECURITY_PROTOCOL_IN;
    struct _SECURITY_PROTOCOL_OUT {
        UCHAR OperationCode;
        UCHAR SecurityProtocol;
        UCHAR SecurityProtocolSpecific[2];
        UCHAR Reserved1 : 7;
        UCHAR INC_512 : 1;
        UCHAR Reserved2;
        UCHAR AllocationLength[4];
        UCHAR Reserved3;
        UCHAR Control;
    } SECURITY_PROTOCOL_OUT;
    struct _UNMAP {
        UCHAR OperationCode;
        UCHAR Anchor        : 1;
        UCHAR Reserved1     : 7;
        UCHAR Reserved2[4];
        UCHAR GroupNumber   : 5;
        UCHAR Reserved3     : 3;
        UCHAR AllocationLength[2];
        UCHAR Control;
    } UNMAP;
    struct _SANITIZE {
        UCHAR OperationCode;
        UCHAR ServiceAction : 5;
        UCHAR AUSE          : 1;
        UCHAR Reserved1     : 1;
        UCHAR Immediate     : 1;
        UCHAR Reserved2[5];
        UCHAR ParameterListLength[2];
        UCHAR Control;
    } SANITIZE;
    struct _PAUSE_RESUME {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Reserved2[6];
        UCHAR Action;
        UCHAR Control;
    } PAUSE_RESUME;
    struct _READ_TOC {
        UCHAR OperationCode;
        UCHAR Reserved0 : 1;
        UCHAR Msf : 1;
        UCHAR Reserved1 : 3;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Format2 : 4;
        UCHAR Reserved2 : 4;
        UCHAR Reserved3[3];
        UCHAR StartingTrack;
        UCHAR AllocationLength[2];
        UCHAR Control : 6;
        UCHAR Format : 2;
    } READ_TOC;
    struct _READ_DISK_INFORMATION {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR Lun : 3;
        UCHAR Reserved2[5];
        UCHAR AllocationLength[2];
        UCHAR Control;
    } READ_DISK_INFORMATION, READ_DISC_INFORMATION;
    struct _READ_TRACK_INFORMATION {
        UCHAR OperationCode;
        UCHAR Track : 2;
        UCHAR Reserved4 : 3;
        UCHAR Lun : 3;
        UCHAR BlockAddress[4];
        UCHAR Reserved3;
        UCHAR AllocationLength[2];
        UCHAR Control;
    } READ_TRACK_INFORMATION;
    struct _RESERVE_TRACK_RZONE {
        UCHAR OperationCode;
        UCHAR Reserved1[4];
        UCHAR ReservationSize[4];
        UCHAR Control;
    } RESERVE_TRACK_RZONE;
    struct _SEND_OPC_INFORMATION {
        UCHAR OperationCode;
        UCHAR DoOpc : 1;
        UCHAR Reserved1 : 7;
        UCHAR Exclude0 : 1;
        UCHAR Exclude1 : 1;
        UCHAR Reserved2 : 6;
        UCHAR Reserved3[4];
        UCHAR ParameterListLength[2];
        UCHAR Reserved4;
    } SEND_OPC_INFORMATION;
    struct _REPAIR_TRACK {
        UCHAR OperationCode;
        UCHAR Immediate : 1;
        UCHAR Reserved1 : 7;
        UCHAR Reserved2[2];
        UCHAR TrackNumber[2];
        UCHAR Reserved3[3];
        UCHAR Control;
    } REPAIR_TRACK;
    struct _CLOSE_TRACK {
        UCHAR OperationCode;
        UCHAR Immediate : 1;
        UCHAR Reserved1 : 7;
        UCHAR Track     : 1;
        UCHAR Session   : 1;
        UCHAR Reserved2 : 6;
        UCHAR Reserved3;
        UCHAR TrackNumber[2];
        UCHAR Reserved4[3];
        UCHAR Control;
    } CLOSE_TRACK;
    struct _READ_BUFFER_CAPACITY {
        UCHAR OperationCode;
        UCHAR BlockInfo : 1;
        UCHAR Reserved1 : 7;
        UCHAR Reserved2[5];
        UCHAR AllocationLength[2];
        UCHAR Control;
    } READ_BUFFER_CAPACITY;
    struct _SEND_CUE_SHEET {
        UCHAR OperationCode;
        UCHAR Reserved[5];
        UCHAR CueSheetSize[3];
        UCHAR Control;
    } SEND_CUE_SHEET;
    struct _READ_HEADER {
        UCHAR OperationCode;
        UCHAR Reserved1 : 1;
        UCHAR Msf : 1;
        UCHAR Reserved2 : 3;
        UCHAR Lun : 3;
        UCHAR LogicalBlockAddress[4];
        UCHAR Reserved3;
        UCHAR AllocationLength[2];
        UCHAR Control;
    } READ_HEADER;
    struct _PLAY_AUDIO {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR StartingBlockAddress[4];
        UCHAR Reserved2;
        UCHAR PlayLength[2];
        UCHAR Control;
    } PLAY_AUDIO;
    struct _PLAY_AUDIO_MSF {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Reserved2;
        UCHAR StartingM;
        UCHAR StartingS;
        UCHAR StartingF;
        UCHAR EndingM;
        UCHAR EndingS;
        UCHAR EndingF;
        UCHAR Control;
    } PLAY_AUDIO_MSF;
    struct _BLANK_MEDIA {
        UCHAR OperationCode;
        UCHAR BlankType : 3;
        UCHAR Reserved1 : 1;
        UCHAR Immediate : 1;
        UCHAR Reserved2 : 3;
        UCHAR AddressOrTrack[4];
        UCHAR Reserved3[5];
        UCHAR Control;
    } BLANK_MEDIA;
    struct _PLAY_CD {
        UCHAR OperationCode;
        UCHAR Reserved1 : 1;
        UCHAR CMSF : 1;
        UCHAR ExpectedSectorType : 3;
        UCHAR Lun : 3;
        union {
            struct _LBA {
                UCHAR StartingBlockAddress[4];
                UCHAR PlayLength[4];
            } LBA;
            struct _MSF {
                UCHAR Reserved1;
                UCHAR StartingM;
                UCHAR StartingS;
                UCHAR StartingF;
                UCHAR EndingM;
                UCHAR EndingS;
                UCHAR EndingF;
                UCHAR Reserved2;
            } MSF;
        };
        UCHAR Audio : 1;
        UCHAR Composite : 1;
        UCHAR Port1 : 1;
        UCHAR Port2 : 1;
        UCHAR Reserved2 : 3;
        UCHAR Speed : 1;
        UCHAR Control;
    } PLAY_CD;
    struct _SCAN_CD {
        UCHAR OperationCode;
        UCHAR RelativeAddress : 1;
        UCHAR Reserved1 : 3;
        UCHAR Direct : 1;
        UCHAR Lun : 3;
        UCHAR StartingAddress[4];
        UCHAR Reserved2[3];
        UCHAR Reserved3 : 6;
        UCHAR Type : 2;
        UCHAR Reserved4;
        UCHAR Control;
    } SCAN_CD;
    struct _STOP_PLAY_SCAN {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR Lun : 3;
        UCHAR Reserved2[7];
        UCHAR Control;
    } STOP_PLAY_SCAN;
    struct _SUBCHANNEL {
        UCHAR OperationCode;
        UCHAR Reserved0 : 1;
        UCHAR Msf : 1;
        UCHAR Reserved1 : 3;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Reserved2 : 6;
        UCHAR SubQ : 1;
        UCHAR Reserved3 : 1;
        UCHAR Format;
        UCHAR Reserved4[2];
        UCHAR TrackNumber;
        UCHAR AllocationLength[2];
        UCHAR Control;
    } SUBCHANNEL;
    struct _READ_CD {
        UCHAR OperationCode;
        UCHAR RelativeAddress : 1;
        UCHAR Reserved0 : 1;
        UCHAR ExpectedSectorType : 3;
        UCHAR Lun : 3;
        UCHAR StartingLBA[4];
        UCHAR TransferBlocks[3];
        UCHAR Reserved2 : 1;
        UCHAR ErrorFlags : 2;
        UCHAR IncludeEDC : 1;
        UCHAR IncludeUserData : 1;
        UCHAR HeaderCode : 2;
        UCHAR IncludeSyncData : 1;
        UCHAR SubChannelSelection : 3;
        UCHAR Reserved3 : 5;
        UCHAR Control;
    } READ_CD;
    struct _READ_CD_MSF {
        UCHAR OperationCode;
        UCHAR RelativeAddress : 1;
        UCHAR Reserved1 : 1;
        UCHAR ExpectedSectorType : 3;
        UCHAR Lun : 3;
        UCHAR Reserved2;
        UCHAR StartingM;
        UCHAR StartingS;
        UCHAR StartingF;
        UCHAR EndingM;
        UCHAR EndingS;
        UCHAR EndingF;
        UCHAR Reserved4 : 1;
        UCHAR ErrorFlags : 2;
        UCHAR IncludeEDC : 1;
        UCHAR IncludeUserData : 1;
        UCHAR HeaderCode : 2;
        UCHAR IncludeSyncData : 1;
        UCHAR SubChannelSelection : 3;
        UCHAR Reserved5 : 5;
        UCHAR Control;
    } READ_CD_MSF;
    struct _PLXTR_READ_CDDA {
        UCHAR OperationCode;
        UCHAR Reserved0 : 5;
        UCHAR LogicalUnitNumber :3;
        UCHAR LogicalBlockByte0;
        UCHAR LogicalBlockByte1;
        UCHAR LogicalBlockByte2;
        UCHAR LogicalBlockByte3;
        UCHAR TransferBlockByte0;
        UCHAR TransferBlockByte1;
        UCHAR TransferBlockByte2;
        UCHAR TransferBlockByte3;
        UCHAR SubCode;
        UCHAR Control;
    } PLXTR_READ_CDDA;
    struct _NEC_READ_CDDA {
        UCHAR OperationCode;
        UCHAR Reserved0;
        UCHAR LogicalBlockByte0;
        UCHAR LogicalBlockByte1;
        UCHAR LogicalBlockByte2;
        UCHAR LogicalBlockByte3;
        UCHAR Reserved1;
        UCHAR TransferBlockByte0;
        UCHAR TransferBlockByte1;
        UCHAR Control;
    } NEC_READ_CDDA;
#if (NTDDI_VERSION >= NTDDI_WIN8)
    struct _MODE_SENSE {
        UCHAR OperationCode;
        UCHAR Reserved1 : 3;
        UCHAR Dbd : 1;
        UCHAR Reserved2 : 4;
        UCHAR PageCode : 6;
        UCHAR Pc : 2;
        UCHAR SubPageCode;
        UCHAR AllocationLength;
        UCHAR Control;
    } MODE_SENSE;
    struct _MODE_SENSE10 {
        UCHAR OperationCode;
        UCHAR Reserved1 : 3;
        UCHAR Dbd : 1;
        UCHAR LongLBAAccepted : 1;
        UCHAR Reserved2 : 3;
        UCHAR PageCode : 6;
        UCHAR Pc : 2;
        UCHAR SubPageCode;
        UCHAR Reserved3[3];
        UCHAR AllocationLength[2];
        UCHAR Control;
    } MODE_SENSE10;
#else
    struct _MODE_SENSE {
        UCHAR OperationCode;
        UCHAR Reserved1 : 3;
        UCHAR Dbd : 1;
        UCHAR Reserved2 : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR PageCode : 6;
        UCHAR Pc : 2;
        UCHAR Reserved3;
        UCHAR AllocationLength;
        UCHAR Control;
    } MODE_SENSE;
    struct _MODE_SENSE10 {
        UCHAR OperationCode;
        UCHAR Reserved1 : 3;
        UCHAR Dbd : 1;
        UCHAR Reserved2 : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR PageCode : 6;
        UCHAR Pc : 2;
        UCHAR Reserved3[4];
        UCHAR AllocationLength[2];
        UCHAR Control;
    } MODE_SENSE10;
#endif
    struct _MODE_SELECT {
        UCHAR OperationCode;
        UCHAR SPBit : 1;
        UCHAR Reserved1 : 3;
        UCHAR PFBit : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Reserved2[2];
        UCHAR ParameterListLength;
        UCHAR Control;
    } MODE_SELECT;
    struct _MODE_SELECT10 {
        UCHAR OperationCode;
        UCHAR SPBit : 1;
        UCHAR Reserved1 : 3;
        UCHAR PFBit : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Reserved2[5];
        UCHAR ParameterListLength[2];
        UCHAR Control;
    } MODE_SELECT10;
    struct _LOCATE {
        UCHAR OperationCode;
        UCHAR Immediate : 1;
        UCHAR CPBit : 1;
        UCHAR BTBit : 1;
        UCHAR Reserved1 : 2;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Reserved3;
        UCHAR LogicalBlockAddress[4];
        UCHAR Reserved4;
        UCHAR Partition;
        UCHAR Control;
    } LOCATE;
    struct _LOGSENSE {
        UCHAR OperationCode;
        UCHAR SPBit : 1;
        UCHAR PPCBit : 1;
        UCHAR Reserved1 : 3;
        UCHAR LogicalUnitNumber : 3;
        UCHAR PageCode : 6;
        UCHAR PCBit : 2;
        union {
            UCHAR SubPageCode;
            UCHAR Reserved2;
        };
        UCHAR Reserved3;
        UCHAR ParameterPointer[2];
        UCHAR AllocationLength[2];
        UCHAR Control;
    } LOGSENSE;
    struct _LOGSELECT {
        UCHAR OperationCode;
        UCHAR SPBit : 1;
        UCHAR PCRBit : 1;
        UCHAR Reserved1 : 3;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Reserved : 6;
        UCHAR PCBit : 2;
        UCHAR Reserved2[4];
        UCHAR ParameterListLength[2];
        UCHAR Control;
    } LOGSELECT;
    struct _PRINT {
        UCHAR OperationCode;
        UCHAR Reserved : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR TransferLength[3];
        UCHAR Control;
    } PRINT;
    struct _SEEK {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR LogicalBlockAddress[4];
        UCHAR Reserved2[3];
        UCHAR Control;
    } SEEK;
    struct _ERASE {
        UCHAR OperationCode;
        UCHAR Long : 1;
        UCHAR Immediate : 1;
        UCHAR Reserved1 : 3;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Reserved2[3];
        UCHAR Control;
    } ERASE;
    struct _START_STOP {
        UCHAR OperationCode;
        UCHAR Immediate: 1;
        UCHAR Reserved1 : 4;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Reserved2[2];
        UCHAR Start : 1;
        UCHAR LoadEject : 1;
        UCHAR Reserved3 : 6;
        UCHAR Control;
    } START_STOP;
    struct _MEDIA_REMOVAL {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR Reserved2[2];
        UCHAR Prevent : 1;
        UCHAR Persistant : 1;
        UCHAR Reserved3 : 6;
        UCHAR Control;
    } MEDIA_REMOVAL;
    struct _SEEK_BLOCK {
        UCHAR OperationCode;
        UCHAR Immediate : 1;
        UCHAR Reserved1 : 7;
        UCHAR BlockAddress[3];
        UCHAR Link : 1;
        UCHAR Flag : 1;
        UCHAR Reserved2 : 4;
        UCHAR VendorUnique : 2;
    } SEEK_BLOCK;
    struct _REQUEST_BLOCK_ADDRESS {
        UCHAR OperationCode;
        UCHAR Reserved1[3];
        UCHAR AllocationLength;
        UCHAR Link : 1;
        UCHAR Flag : 1;
        UCHAR Reserved2 : 4;
        UCHAR VendorUnique : 2;
    } REQUEST_BLOCK_ADDRESS;
    struct _PARTITION {
        UCHAR OperationCode;
        UCHAR Immediate : 1;
        UCHAR Sel: 1;
        UCHAR PartitionSelect : 6;
        UCHAR Reserved1[3];
        UCHAR Control;
    } PARTITION;
    struct _WRITE_TAPE_MARKS {
        UCHAR OperationCode;
        UCHAR Immediate : 1;
        UCHAR WriteSetMarks: 1;
        UCHAR Reserved : 3;
        UCHAR LogicalUnitNumber : 3;
        UCHAR TransferLength[3];
        UCHAR Control;
    } WRITE_TAPE_MARKS;
    struct _SPACE_TAPE_MARKS {
        UCHAR OperationCode;
        UCHAR Code : 3;
        UCHAR Reserved : 2;
        UCHAR LogicalUnitNumber : 3;
        UCHAR NumMarksMSB ;
        UCHAR NumMarks;
        UCHAR NumMarksLSB;
        union {
            UCHAR value;
            struct {
                UCHAR Link : 1;
                UCHAR Flag : 1;
                UCHAR Reserved : 4;
                UCHAR VendorUnique : 2;
            } Fields;
        } Byte6;
    } SPACE_TAPE_MARKS;
    struct _READ_POSITION {
        UCHAR Operation;
        UCHAR BlockType:1;
        UCHAR Reserved1:4;
        UCHAR Lun:3;
        UCHAR Reserved2[7];
        UCHAR Control;
    } READ_POSITION;
    struct _CDB6READWRITETAPE {
        UCHAR OperationCode;
        UCHAR VendorSpecific : 5;
        UCHAR Reserved : 3;
        UCHAR TransferLenMSB;
        UCHAR TransferLen;
        UCHAR TransferLenLSB;
        UCHAR Link : 1;
        UCHAR Flag : 1;
        UCHAR Reserved1 : 4;
        UCHAR VendorUnique : 2;
    } CDB6READWRITETAPE;
    struct _INIT_ELEMENT_STATUS {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR LogicalUnitNubmer : 3;
        UCHAR Reserved2[3];
        UCHAR Reserved3 : 7;
        UCHAR NoBarCode : 1;
    } INIT_ELEMENT_STATUS;
    struct _INITIALIZE_ELEMENT_RANGE {
        UCHAR OperationCode;
        UCHAR Range : 1;
        UCHAR Reserved1 : 4;
        UCHAR LogicalUnitNubmer : 3;
        UCHAR FirstElementAddress[2];
        UCHAR Reserved2[2];
        UCHAR NumberOfElements[2];
        UCHAR Reserved3;
        UCHAR Reserved4 : 7;
        UCHAR NoBarCode : 1;
    } INITIALIZE_ELEMENT_RANGE;
    struct _POSITION_TO_ELEMENT {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR TransportElementAddress[2];
        UCHAR DestinationElementAddress[2];
        UCHAR Reserved2[2];
        UCHAR Flip : 1;
        UCHAR Reserved3 : 7;
        UCHAR Control;
    } POSITION_TO_ELEMENT;
    struct _MOVE_MEDIUM {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR TransportElementAddress[2];
        UCHAR SourceElementAddress[2];
        UCHAR DestinationElementAddress[2];
        UCHAR Reserved2[2];
        UCHAR Flip : 1;
        UCHAR Reserved3 : 7;
        UCHAR Control;
    } MOVE_MEDIUM;
    struct _EXCHANGE_MEDIUM {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR LogicalUnitNumber : 3;
        UCHAR TransportElementAddress[2];
        UCHAR SourceElementAddress[2];
        UCHAR Destination1ElementAddress[2];
        UCHAR Destination2ElementAddress[2];
        UCHAR Flip1 : 1;
        UCHAR Flip2 : 1;
        UCHAR Reserved3 : 6;
        UCHAR Control;
    } EXCHANGE_MEDIUM;
    struct _READ_ELEMENT_STATUS {
        UCHAR OperationCode;
        UCHAR ElementType : 4;
        UCHAR VolTag : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR StartingElementAddress[2];
        UCHAR NumberOfElements[2];
        UCHAR Reserved1;
        UCHAR AllocationLength[3];
        UCHAR Reserved2;
        UCHAR Control;
    } READ_ELEMENT_STATUS;
    struct _SEND_VOLUME_TAG {
        UCHAR OperationCode;
        UCHAR ElementType : 4;
        UCHAR Reserved1 : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR StartingElementAddress[2];
        UCHAR Reserved2;
        UCHAR ActionCode : 5;
        UCHAR Reserved3 : 3;
        UCHAR Reserved4[2];
        UCHAR ParameterListLength[2];
        UCHAR Reserved5;
        UCHAR Control;
    } SEND_VOLUME_TAG;
    struct _REQUEST_VOLUME_ELEMENT_ADDRESS {
        UCHAR OperationCode;
        UCHAR ElementType : 4;
        UCHAR VolTag : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR StartingElementAddress[2];
        UCHAR NumberElements[2];
        UCHAR Reserved1;
        UCHAR AllocationLength[3];
        UCHAR Reserved2;
        UCHAR Control;
    } REQUEST_VOLUME_ELEMENT_ADDRESS;
    struct _LOAD_UNLOAD {
        UCHAR OperationCode;
        UCHAR Immediate : 1;
        UCHAR Reserved1 : 4;
        UCHAR Lun : 3;
        UCHAR Reserved2[2];
        UCHAR Start : 1;
        UCHAR LoadEject : 1;
        UCHAR Reserved3: 6;
        UCHAR Reserved4[3];
        UCHAR Slot;
        UCHAR Reserved5[3];
    } LOAD_UNLOAD;
    struct _MECH_STATUS {
        UCHAR OperationCode;
        UCHAR Reserved : 5;
        UCHAR Lun : 3;
        UCHAR Reserved1[6];
        UCHAR AllocationLength[2];
        UCHAR Reserved2[1];
        UCHAR Control;
    } MECH_STATUS;
    struct _SYNCHRONIZE_CACHE10 {
        UCHAR OperationCode;
        UCHAR RelAddr : 1;
        UCHAR Immediate : 1;
        UCHAR Reserved : 3;
        UCHAR Lun : 3;
        UCHAR LogicalBlockAddress[4];
        UCHAR Reserved2;
        UCHAR BlockCount[2];
        UCHAR Control;
    } SYNCHRONIZE_CACHE10;
    struct _GET_EVENT_STATUS_NOTIFICATION {
        UCHAR OperationCode;
        UCHAR Immediate : 1;
        UCHAR Reserved : 4;
        UCHAR Lun : 3;
        UCHAR Reserved2[2];
        UCHAR NotificationClassRequest;
        UCHAR Reserved3[2];
        UCHAR EventListLength[2];
        UCHAR Control;
    } GET_EVENT_STATUS_NOTIFICATION;
    struct _GET_PERFORMANCE {
        UCHAR OperationCode;
        UCHAR Except    : 2;
        UCHAR Write     : 1;
        UCHAR Tolerance : 2;
        UCHAR Reserved0 : 3;
        UCHAR StartingLBA[4];
        UCHAR Reserved1[2];
        UCHAR MaximumNumberOfDescriptors[2];
        UCHAR Type;
        UCHAR Control;
    } GET_PERFORMANCE;
    struct _READ_DVD_STRUCTURE {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR Lun : 3;
        UCHAR RMDBlockNumber[4];
        UCHAR LayerNumber;
        UCHAR Format;
        UCHAR AllocationLength[2];
        UCHAR Reserved3 : 6;
        UCHAR AGID : 2;
        UCHAR Control;
    } READ_DVD_STRUCTURE;
    struct _SET_STREAMING {
        UCHAR OperationCode;
        UCHAR Reserved[8];
        UCHAR ParameterListLength[2];
        UCHAR Control;
    } SET_STREAMING;
    struct _SEND_DVD_STRUCTURE {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR Lun : 3;
        UCHAR Reserved2[5];
        UCHAR Format;
        UCHAR ParameterListLength[2];
        UCHAR Reserved3;
        UCHAR Control;
    } SEND_DVD_STRUCTURE;
    struct _SEND_KEY {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR Lun : 3;
        UCHAR Reserved2[6];
        UCHAR ParameterListLength[2];
        UCHAR KeyFormat : 6;
        UCHAR AGID : 2;
        UCHAR Control;
    } SEND_KEY;
    struct _REPORT_KEY {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR Lun : 3;
        UCHAR LogicalBlockAddress[4];
        UCHAR Reserved2[2];
        UCHAR AllocationLength[2];
        UCHAR KeyFormat : 6;
        UCHAR AGID : 2;
        UCHAR Control;
    } REPORT_KEY;
    struct _SET_READ_AHEAD {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR Lun : 3;
        UCHAR TriggerLBA[4];
        UCHAR ReadAheadLBA[4];
        UCHAR Reserved2;
        UCHAR Control;
    } SET_READ_AHEAD;
    struct _READ_FORMATTED_CAPACITIES {
        UCHAR OperationCode;
        UCHAR Reserved1 : 5;
        UCHAR Lun : 3;
        UCHAR Reserved2[5];
        UCHAR AllocationLength[2];
        UCHAR Control;
    } READ_FORMATTED_CAPACITIES;
    struct _REPORT_LUNS {
        UCHAR OperationCode;
        UCHAR Reserved1[5];
        UCHAR AllocationLength[4];
        UCHAR Reserved2[1];
        UCHAR Control;
    } REPORT_LUNS;
    struct _PERSISTENT_RESERVE_IN {
        UCHAR OperationCode;
        UCHAR ServiceAction : 5;
        UCHAR Reserved1 : 3;
        UCHAR Reserved2[5];
        UCHAR AllocationLength[2];
        UCHAR Control;
    } PERSISTENT_RESERVE_IN;
    struct _PERSISTENT_RESERVE_OUT {
        UCHAR OperationCode;
        UCHAR ServiceAction : 5;
        UCHAR Reserved1 : 3;
        UCHAR Type : 4;
        UCHAR Scope : 4;
        UCHAR Reserved2[4];
        UCHAR ParameterListLength[2];
        UCHAR Control;
    } PERSISTENT_RESERVE_OUT;
    struct _REPORT_TIMESTAMP {
        UCHAR OperationCode;
        UCHAR ServiceAction : 5;
        UCHAR Reserved1 : 3;
        UCHAR Reserved2[4];
        UCHAR AllocationLength[4];
        UCHAR Reserved3;
        UCHAR Control;
    } REPORT_TIMESTAMP;
    struct _SET_TIMESTAMP {
        UCHAR OperationCode;
        UCHAR ServiceAction : 5;
        UCHAR Reserved1 : 3;
        UCHAR Reserved2[4];
        UCHAR ParameterListLength[4];
        UCHAR Reserved3;
        UCHAR Control;
    } SET_TIMESTAMP;
    struct _REPORT_SUPPORTED_OPERATION_CODES {
        UCHAR OperationCode;
        UCHAR ServiceAction                     : 5;
        UCHAR Reserved0                         : 3;
        UCHAR ReportOptions                     : 3;
        UCHAR Reserved1                         : 4;
        UCHAR ReturnCommandTimeoutsDescriptor   : 1;
        UCHAR RequestedOperationCode;
        UCHAR RequestedServiceAction[2];
        UCHAR AllocationLength[4];
        UCHAR Reserved2;
        UCHAR Control;
    } REPORT_SUPPORTED_OPERATION_CODES;
    struct _GET_CONFIGURATION {
        UCHAR OperationCode;
        UCHAR RequestType : 2;
        UCHAR Reserved1   : 6;
        UCHAR StartingFeature[2];
        UCHAR Reserved2[3];
        UCHAR AllocationLength[2];
        UCHAR Control;
    } GET_CONFIGURATION;
    struct _SET_CD_SPEED {
        UCHAR OperationCode;
        union {
            UCHAR Reserved1;
            struct {
                UCHAR RotationControl : 2;
                UCHAR Reserved3       : 6;
            };
        };
        UCHAR ReadSpeed[2];
        UCHAR WriteSpeed[2];
        UCHAR Reserved2[5];
        UCHAR Control;
    } SET_CD_SPEED;
    struct _READ12 {
        UCHAR OperationCode;
        UCHAR RelativeAddress   : 1;
        UCHAR Reserved1         : 2;
        UCHAR ForceUnitAccess   : 1;
        UCHAR DisablePageOut    : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR LogicalBlock[4];
        UCHAR TransferLength[4];
        UCHAR Reserved2 : 7;
        UCHAR Streaming : 1;
        UCHAR Control;
    } READ12;
    struct _WRITE12 {
        UCHAR OperationCode;
        UCHAR RelativeAddress   : 1;
        UCHAR Reserved1         : 1;
        UCHAR EBP               : 1;
        UCHAR ForceUnitAccess   : 1;
        UCHAR DisablePageOut    : 1;
        UCHAR LogicalUnitNumber : 3;
        UCHAR LogicalBlock[4];
        UCHAR TransferLength[4];
        UCHAR Reserved2 : 7;
        UCHAR Streaming : 1;
        UCHAR Control;
    } WRITE12;
    struct _ATA_PASSTHROUGH12 {
        UCHAR OperationCode;
        UCHAR Reserved1     : 1;
        UCHAR Protocol      : 4;
        UCHAR MultipleCount : 3;
        UCHAR TLength       : 2;
        UCHAR ByteBlock     : 1;
        UCHAR TDir          : 1;
        UCHAR Reserved2     : 1;
        UCHAR CkCond        : 1;
        UCHAR Offline       : 2;
        UCHAR Features;
        UCHAR SectorCount;
        UCHAR LbaLow;
        UCHAR LbaMid;
        UCHAR LbaHigh;
        UCHAR Device;
        UCHAR Command;
        UCHAR Reserved3;
        UCHAR Control;
    } ATA_PASSTHROUGH12;
    struct _READ16 {
        UCHAR OperationCode;
        UCHAR DurationLimitDescriptor2      : 1;
        UCHAR Reserved1                     : 1;
        UCHAR RebuildAssistRecoveryControl  : 1;
        UCHAR ForceUnitAccess               : 1;
        UCHAR DisablePageOut                : 1;
        UCHAR ReadProtect                   : 3;
        UCHAR LogicalBlock[8];
        UCHAR TransferLength[4];
        UCHAR Group                         : 6;
        UCHAR DurationLimitDescriptor0      : 1;
        UCHAR DurationLimitDescriptor1      : 1;
        UCHAR Control;
    } READ16;
    struct _WRITE16 {
        UCHAR OperationCode;
        UCHAR DurationLimitDescriptor2      : 1;
        UCHAR Reserved1                     : 2;
        UCHAR ForceUnitAccess               : 1;
        UCHAR DisablePageOut                : 1;
        UCHAR WriteProtect                  : 3;
        UCHAR LogicalBlock[8];
        UCHAR TransferLength[4];
        UCHAR Group                         : 6;
        UCHAR DurationLimitDescriptor0      : 1;
        UCHAR DurationLimitDescriptor1      : 1;
        UCHAR Control;
    } WRITE16;
    struct _VERIFY16 {
        UCHAR OperationCode;
        UCHAR Reserved1         : 1;
        UCHAR ByteCheck         : 1;
        UCHAR BlockVerify       : 1;
        UCHAR Reserved2         : 1;
        UCHAR DisablePageOut    : 1;
        UCHAR VerifyProtect     : 3;
        UCHAR LogicalBlock[8];
        UCHAR VerificationLength[4];
        UCHAR Reserved3         : 7;
        UCHAR Streaming         : 1;
        UCHAR Control;
    } VERIFY16;
    struct _SYNCHRONIZE_CACHE16 {
        UCHAR OperationCode;
        UCHAR Reserved1         : 1;
        UCHAR Immediate         : 1;
        UCHAR Reserved2         : 6;
        UCHAR LogicalBlock[8];
        UCHAR BlockCount[4];
        UCHAR Reserved3;
        UCHAR Control;
    } SYNCHRONIZE_CACHE16;
    struct _READ_CAPACITY16 {
        UCHAR OperationCode;
        UCHAR ServiceAction     : 5;
        UCHAR Reserved1         : 3;
        UCHAR LogicalBlock[8];
        UCHAR AllocationLength[4];
        UCHAR PMI               : 1;
        UCHAR Reserved2         : 7;
        UCHAR Control;
    } READ_CAPACITY16;
    struct _ATA_PASSTHROUGH16 {
        UCHAR OperationCode;
        UCHAR Extend            : 1;
        UCHAR Protocol          : 4;
        UCHAR MultipleCount     : 3;
        UCHAR TLength       : 2;
        UCHAR ByteBlock     : 1;
        UCHAR TDir          : 1;
        UCHAR Reserved1     : 1;
        UCHAR CkCond        : 1;
        UCHAR Offline       : 2;
        UCHAR Features15_8;
        UCHAR Features7_0;
        UCHAR SectorCount15_8;
        UCHAR SectorCount7_0;
        UCHAR LbaLow15_8;
        UCHAR LbaLow7_0;
        UCHAR LbaMid15_8;
        UCHAR LbaMid7_0;
        UCHAR LbaHigh15_8;
        UCHAR LbaHigh7_0;
        UCHAR Device;
        UCHAR Command;
        UCHAR Control;
    } ATA_PASSTHROUGH16;
    struct _GET_LBA_STATUS {
        UCHAR OperationCode;
        UCHAR ServiceAction : 5;
        UCHAR Reserved1     : 3;
        UCHAR StartingLBA[8];
        UCHAR AllocationLength[4];
        UCHAR Reserved2;
        UCHAR Control;
    } GET_LBA_STATUS;
    struct _TOKEN_OPERATION {
        UCHAR OperationCode;
        UCHAR ServiceAction : 5;
        UCHAR Reserved1     : 3;
        UCHAR Reserved2[4];
        UCHAR ListIdentifier[4];
        UCHAR ParameterListLength[4];
        UCHAR GroupNumber   : 5;
        UCHAR Reserved3     : 3;
        UCHAR Control;
    } TOKEN_OPERATION;
    struct _RECEIVE_TOKEN_INFORMATION {
        UCHAR OperationCode;
        UCHAR ServiceAction : 5;
        UCHAR Reserved1     : 3;
        UCHAR ListIdentifier[4];
        UCHAR Reserved2[4];
        UCHAR AllocationLength[4];
        UCHAR Reserved3;
        UCHAR Control;
    } RECEIVE_TOKEN_INFORMATION;
    struct _WRITE_BUFFER {
        UCHAR OperationCode;
        UCHAR Mode : 5;
        UCHAR ModeSpecific : 3;
        UCHAR BufferID;
        UCHAR BufferOffset[3];
        UCHAR ParameterListLength[3];
        UCHAR Control;
    } WRITE_BUFFER;
    struct _CLOSE_ZONE {
        UCHAR OperationCode;
        UCHAR ServiceAction     : 5;
        UCHAR Reserved1         : 3;
        UCHAR ZoneId[8];
        UCHAR Reserved2[4];
        UCHAR All               : 1;
        UCHAR Reserved3         : 7;
        UCHAR Control;
    } CLOSE_ZONE;
    struct _FINISH_ZONE {
        UCHAR OperationCode;
        UCHAR ServiceAction     : 5;
        UCHAR Reserved1         : 3;
        UCHAR ZoneId[8];
        UCHAR Reserved2[4];
        UCHAR All               : 1;
        UCHAR Reserved3         : 7;
        UCHAR Control;
    } FINISH_ZONE;
    struct _OPEN_ZONE {
        UCHAR OperationCode;
        UCHAR ServiceAction     : 5;
        UCHAR Reserved1         : 3;
        UCHAR ZoneId[8];
        UCHAR Reserved2[4];
        UCHAR All               : 1;
        UCHAR Reserved3         : 7;
        UCHAR Control;
    } OPEN_ZONE;
    struct _RESET_WRITE_POINTER {
        UCHAR OperationCode;
        UCHAR ServiceAction     : 5;
        UCHAR Reserved1         : 3;
        UCHAR ZoneId[8];
        UCHAR Reserved2[4];
        UCHAR All               : 1;
        UCHAR Reserved3         : 7;
        UCHAR Control;
    } RESET_WRITE_POINTER;
    struct _REPORT_ZONES {
        UCHAR OperationCode;
        UCHAR ServiceAction     : 5;
        UCHAR Reserved1         : 3;
        UCHAR ZoneStartLBA[8];
        UCHAR AllocationLength[4];
        UCHAR ReportingOptions  : 6;
        UCHAR Reserved3         : 1;
        UCHAR Partial           : 1;
        UCHAR Control;
    } REPORT_ZONES;
    struct _GET_PHYSICAL_ELEMENT_STATUS {
        UCHAR OperationCode;
        UCHAR ServiceAction     : 5;
        UCHAR Reserved1         : 3;
        UCHAR Reserved2[4];
        UCHAR StartingElement[4];
        UCHAR AllocationLength[4];
        UCHAR ReportType        : 4;
        UCHAR Reserved3         : 2;
        UCHAR Filter            : 2;
        UCHAR Control;
    } GET_PHYSICAL_ELEMENT_STATUS;
    struct _REMOVE_ELEMENT_AND_TRUNCATE {
        UCHAR OperationCode;
        UCHAR ServiceAction     : 5;
        UCHAR Reserved1         : 3;
        UCHAR RequestedCapacity[8];
        UCHAR ElementIdentifier[4];
        UCHAR Reserved2;
        UCHAR Control;
    } REMOVE_ELEMENT_AND_TRUNCATE;
    ULONG AsUlong[4];
    UCHAR AsByte[16];
} CDB, *PCDB;
#include <poppack.h>

#define CDB6GENERIC_LENGTH                   6

#define CDB10GENERIC_LENGTH                  10

#define CDB12GENERIC_LENGTH                  12

#define SETBITON                             1

#define SETBITOFF                            0

#define MODE_PAGE_VENDOR_SPECIFIC       0x00

#define MODE_PAGE_ERROR_RECOVERY        0x01

#define MODE_PAGE_DISCONNECT            0x02

#define MODE_PAGE_FORMAT_DEVICE         0x03

#define MODE_PAGE_MRW                   0x03

#define MODE_PAGE_RIGID_GEOMETRY        0x04

#define MODE_PAGE_FLEXIBILE             0x05

#define MODE_PAGE_WRITE_PARAMETERS      0x05

#define MODE_PAGE_VERIFY_ERROR          0x07

#define MODE_PAGE_CACHING               0x08

#define MODE_PAGE_PERIPHERAL            0x09

#define MODE_PAGE_CONTROL               0x0A

#define MODE_PAGE_MEDIUM_TYPES          0x0B

#define MODE_PAGE_NOTCH_PARTITION       0x0C

#define MODE_PAGE_CD_AUDIO_CONTROL      0x0E

#define MODE_PAGE_DATA_COMPRESS         0x0F

#define MODE_PAGE_DEVICE_CONFIG         0x10

#define MODE_PAGE_XOR_CONTROL           0x10

#define MODE_PAGE_MEDIUM_PARTITION      0x11

#define MODE_PAGE_ENCLOSURE_SERVICES_MANAGEMENT 0x14

#define MODE_PAGE_EXTENDED              0x15

#define MODE_PAGE_EXTENDED_DEVICE_SPECIFIC 0x16

#define MODE_PAGE_CDVD_FEATURE_SET      0x18

#define MODE_PAGE_PROTOCOL_SPECIFIC_LUN 0x18

#define MODE_PAGE_PROTOCOL_SPECIFIC_PORT 0x19

#define MODE_PAGE_POWER_CONDITION       0x1A

#define MODE_PAGE_LUN_MAPPING           0x1B

#define MODE_PAGE_FAULT_REPORTING       0x1C

#define MODE_PAGE_CDVD_INACTIVITY       0x1D

#define MODE_PAGE_ELEMENT_ADDRESS       0x1D

#define MODE_PAGE_TRANSPORT_GEOMETRY    0x1E

#define MODE_PAGE_DEVICE_CAPABILITIES   0x1F

#define MODE_PAGE_CAPABILITIES          0x2A

#define MODE_SENSE_RETURN_ALL           0x3f

#define MODE_SENSE_CURRENT_VALUES       0x00

#define MODE_SENSE_CHANGEABLE_VALUES    0x40

#define MODE_SENSE_DEFAULT_VAULES       0x80

#define MODE_SENSE_SAVED_VALUES         0xc0

#define SCSIOP_TEST_UNIT_READY          0x00

#define SCSIOP_REZERO_UNIT              0x01

#define SCSIOP_REWIND                   0x01

#define SCSIOP_REQUEST_BLOCK_ADDR       0x02

#define SCSIOP_REQUEST_SENSE            0x03

#define SCSIOP_FORMAT_UNIT              0x04

#define SCSIOP_READ_BLOCK_LIMITS        0x05

#define SCSIOP_REASSIGN_BLOCKS          0x07

#define SCSIOP_INIT_ELEMENT_STATUS      0x07

#define SCSIOP_READ6                    0x08

#define SCSIOP_RECEIVE                  0x08

#define SCSIOP_WRITE6                   0x0A

#define SCSIOP_PRINT                    0x0A

#define SCSIOP_SEND                     0x0A

#define SCSIOP_SEEK6                    0x0B

#define SCSIOP_TRACK_SELECT             0x0B

#define SCSIOP_SLEW_PRINT               0x0B

#define SCSIOP_SET_CAPACITY             0x0B

#define SCSIOP_SEEK_BLOCK               0x0C

#define SCSIOP_PARTITION                0x0D

#define SCSIOP_READ_REVERSE             0x0F

#define SCSIOP_WRITE_FILEMARKS          0x10

#define SCSIOP_FLUSH_BUFFER             0x10

#define SCSIOP_SPACE                    0x11

#define SCSIOP_INQUIRY                  0x12

#define SCSIOP_VERIFY6                  0x13

#define SCSIOP_RECOVER_BUF_DATA         0x14

#define SCSIOP_MODE_SELECT              0x15

#define SCSIOP_RESERVE_UNIT             0x16

#define SCSIOP_RELEASE_UNIT             0x17

#define SCSIOP_COPY                     0x18

#define SCSIOP_ERASE                    0x19

#define SCSIOP_MODE_SENSE               0x1A

#define SCSIOP_START_STOP_UNIT          0x1B

#define SCSIOP_STOP_PRINT               0x1B

#define SCSIOP_LOAD_UNLOAD              0x1B

#define SCSIOP_RECEIVE_DIAGNOSTIC       0x1C

#define SCSIOP_SEND_DIAGNOSTIC          0x1D

#define SCSIOP_MEDIUM_REMOVAL           0x1E

#define SCSIOP_READ_FORMATTED_CAPACITY  0x23

#define SCSIOP_READ_CAPACITY            0x25

#define SCSIOP_READ                     0x28

#define SCSIOP_WRITE                    0x2A

#define SCSIOP_SEEK                     0x2B

#define SCSIOP_LOCATE                   0x2B

#define SCSIOP_POSITION_TO_ELEMENT      0x2B

#define SCSIOP_WRITE_VERIFY             0x2E

#define SCSIOP_VERIFY                   0x2F

#define SCSIOP_SEARCH_DATA_HIGH         0x30

#define SCSIOP_SEARCH_DATA_EQUAL        0x31

#define SCSIOP_SEARCH_DATA_LOW          0x32

#define SCSIOP_SET_LIMITS               0x33

#define SCSIOP_READ_POSITION            0x34

#define SCSIOP_SYNCHRONIZE_CACHE        0x35

#define SCSIOP_COMPARE                  0x39

#define SCSIOP_COPY_COMPARE             0x3A

#define SCSIOP_WRITE_DATA_BUFF          0x3B

#define SCSIOP_READ_DATA_BUFF           0x3C

#define SCSIOP_WRITE_LONG               0x3F

#define SCSIOP_CHANGE_DEFINITION        0x40

#define SCSIOP_WRITE_SAME               0x41

#define SCSIOP_READ_SUB_CHANNEL         0x42

#define SCSIOP_UNMAP                    0x42

#define SCSIOP_READ_TOC                 0x43

#define SCSIOP_READ_HEADER              0x44

#define SCSIOP_REPORT_DENSITY_SUPPORT   0x44

#define SCSIOP_PLAY_AUDIO               0x45

#define SCSIOP_GET_CONFIGURATION        0x46

#define SCSIOP_PLAY_AUDIO_MSF           0x47

#define SCSIOP_PLAY_TRACK_INDEX         0x48

#define SCSIOP_PLAY_TRACK_RELATIVE      0x49

#define SCSIOP_GET_EVENT_STATUS         0x4A

#define SCSIOP_PAUSE_RESUME             0x4B

#define SCSIOP_LOG_SELECT               0x4C

#define SCSIOP_LOG_SENSE                0x4D

#define SCSIOP_STOP_PLAY_SCAN           0x4E

#define SCSIOP_XDWRITE                  0x50

#define SCSIOP_XPWRITE                  0x51

#define SCSIOP_READ_DISK_INFORMATION    0x51

#define SCSIOP_READ_DISC_INFORMATION    0x51

#define SCSIOP_READ_TRACK_INFORMATION   0x52

#define SCSIOP_XDWRITE_READ             0x53

#define SCSIOP_RESERVE_TRACK_RZONE      0x53

#define SCSIOP_SEND_OPC_INFORMATION     0x54

#define SCSIOP_MODE_SELECT10            0x55

#define SCSIOP_RESERVE_UNIT10           0x56

#define SCSIOP_RESERVE_ELEMENT          0x56

#define SCSIOP_RELEASE_UNIT10           0x57

#define SCSIOP_RELEASE_ELEMENT          0x57

#define SCSIOP_REPAIR_TRACK             0x58

#define SCSIOP_MODE_SENSE10             0x5A

#define SCSIOP_CLOSE_TRACK_SESSION      0x5B

#define SCSIOP_READ_BUFFER_CAPACITY     0x5C

#define SCSIOP_SEND_CUE_SHEET           0x5D

#define SCSIOP_PERSISTENT_RESERVE_IN    0x5E

#define SCSIOP_PERSISTENT_RESERVE_OUT   0x5F

#define SCSIOP_REPORT_LUNS              0xA0

#define SCSIOP_BLANK                    0xA1

#define SCSIOP_ATA_PASSTHROUGH12        0xA1

#define SCSIOP_SEND_EVENT               0xA2

#define SCSIOP_SECURITY_PROTOCOL_IN     0xA2

#define SCSIOP_SEND_KEY                 0xA3

#define SCSIOP_MAINTENANCE_IN           0xA3

#define SCSIOP_REPORT_KEY               0xA4

#define SCSIOP_MAINTENANCE_OUT          0xA4

#define SCSIOP_MOVE_MEDIUM              0xA5

#define SCSIOP_LOAD_UNLOAD_SLOT         0xA6

#define SCSIOP_EXCHANGE_MEDIUM          0xA6

#define SCSIOP_SET_READ_AHEAD           0xA7

#define SCSIOP_MOVE_MEDIUM_ATTACHED     0xA7

#define SCSIOP_READ12                   0xA8

#define SCSIOP_GET_MESSAGE              0xA8

#define SCSIOP_SERVICE_ACTION_OUT12     0xA9

#define SCSIOP_WRITE12                  0xAA

#define SCSIOP_SEND_MESSAGE             0xAB

#define SCSIOP_SERVICE_ACTION_IN12      0xAB

#define SCSIOP_GET_PERFORMANCE          0xAC

#define SCSIOP_READ_DVD_STRUCTURE       0xAD

#define SCSIOP_WRITE_VERIFY12           0xAE

#define SCSIOP_VERIFY12                 0xAF

#define SCSIOP_SEARCH_DATA_HIGH12       0xB0

#define SCSIOP_SEARCH_DATA_EQUAL12      0xB1

#define SCSIOP_SEARCH_DATA_LOW12        0xB2

#define SCSIOP_SET_LIMITS12             0xB3

#define SCSIOP_READ_ELEMENT_STATUS_ATTACHED 0xB4

#define SCSIOP_REQUEST_VOL_ELEMENT      0xB5

#define SCSIOP_SECURITY_PROTOCOL_OUT    0xB5

#define SCSIOP_SEND_VOLUME_TAG          0xB6

#define SCSIOP_SET_STREAMING            0xB6

#define SCSIOP_READ_DEFECT_DATA         0xB7

#define SCSIOP_READ_ELEMENT_STATUS      0xB8

#define SCSIOP_READ_CD_MSF              0xB9

#define SCSIOP_SCAN_CD                  0xBA

#define SCSIOP_REDUNDANCY_GROUP_IN      0xBA

#define SCSIOP_SET_CD_SPEED             0xBB

#define SCSIOP_REDUNDANCY_GROUP_OUT     0xBB

#define SCSIOP_PLAY_CD                  0xBC

#define SCSIOP_SPARE_IN                 0xBC

#define SCSIOP_MECHANISM_STATUS         0xBD

#define SCSIOP_SPARE_OUT                0xBD

#define SCSIOP_READ_CD                  0xBE

#define SCSIOP_VOLUME_SET_IN            0xBE

#define SCSIOP_SEND_DVD_STRUCTURE       0xBF

#define SCSIOP_VOLUME_SET_OUT           0xBF

#define SCSIOP_INIT_ELEMENT_RANGE       0xE7

#define SCSIOP_XDWRITE_EXTENDED16       0x80

#define SCSIOP_WRITE_FILEMARKS16        0x80

#define SCSIOP_REBUILD16                0x81

#define SCSIOP_READ_REVERSE16           0x81

#define SCSIOP_REGENERATE16             0x82

#define SCSIOP_EXTENDED_COPY            0x83

#define SCSIOP_RECEIVE_COPY_RESULTS     0x84

#define SCSIOP_ATA_PASSTHROUGH16        0x85

#define SCSIOP_ACCESS_CONTROL_IN        0x86

#define SCSIOP_ACCESS_CONTROL_OUT       0x87

#define SCSIOP_READ16                   0x88

#define SCSIOP_WRITE16                  0x8A

#define SCSIOP_READ_ATTRIBUTES          0x8C

#define SCSIOP_WRITE_ATTRIBUTES         0x8D

#define SCSIOP_WRITE_VERIFY16           0x8E

#define SCSIOP_VERIFY16                 0x8F

#define SCSIOP_PREFETCH16               0x90

#define SCSIOP_SYNCHRONIZE_CACHE16      0x91

#define SCSIOP_SPACE16                  0x91

#define SCSIOP_LOCK_UNLOCK_CACHE16      0x92

#define SCSIOP_LOCATE16                 0x92

#define SCSIOP_WRITE_SAME16             0x93

#define SCSIOP_ERASE16                  0x93

#define SCSIOP_READ_DATA_BUFF16         0x9B

#define SCSIOP_READ_CAPACITY16          0x9E

#define SCSIOP_SERVICE_ACTION_IN16      0x9E

#define SCSIOP_SERVICE_ACTION_OUT16     0x9F

#define SERVICE_ACTION_READ_CAPACITY16                                          0x10

#define SERVICE_ACTION_GET_PHYSICAL_ELEMENT_STATUS                              0x17

#define SERVICE_ACTION_REMOVE_ELEMENT_AND_TRUNCATE                              0x18

#define SCSIMESS_ABORT                0x06

#define SCSIMESS_ABORT_WITH_TAG       0x0D

#define SCSIMESS_BUS_DEVICE_RESET     0X0C

#define SCSIMESS_EXTENDED_MESSAGE     0X01

#define SCSIMESS_IDENTIFY             0X80

#define SCSIMESS_IDENTIFY_WITH_DISCON 0XC0

#define SCSIMESS_INIT_DETECTED_ERROR  0X05

#define SCSIMESS_MESS_PARITY_ERROR    0X09

#define SCSIMESS_TERMINATE_IO_PROCESS 0X11

#define SCSIMESS_SYNCHRONOUS_DATA_REQ 0X01

#define SCSIMESS_WIDE_DATA_REQUEST    0X03

#define SCSISTAT_GOOD                  0x00

#define SCSISTAT_CHECK_CONDITION       0x02

#define SCSISTAT_CONDITION_MET         0x04

#define SCSISTAT_BUSY                  0x08

#define SCSISTAT_INTERMEDIATE          0x10

#define SCSISTAT_INTERMEDIATE_COND_MET 0x14

#define SCSISTAT_RESERVATION_CONFLICT  0x18

#define SCSISTAT_COMMAND_TERMINATED    0x22

#define SCSISTAT_QUEUE_FULL            0x28

#define INQUIRYDATABUFFERSIZE 36

typedef USHORT VERSION_DESCRIPTOR, *PVERSION_DESCRIPTOR;

#if ((NTDDI_VERSION < NTDDI_WINXP))

typedef struct _INQUIRYDATA {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR DeviceTypeModifier : 7;
    UCHAR RemovableMedia : 1;
    UCHAR Versions;
    UCHAR ResponseDataFormat : 4;
    UCHAR HiSupport : 1;
    UCHAR NormACA : 1;
    UCHAR ReservedBit : 1;
    UCHAR AERC : 1;
    UCHAR AdditionalLength;
    UCHAR Reserved[2];
    UCHAR SoftReset : 1;
    UCHAR CommandQueue : 1;
    UCHAR Reserved2 : 1;
    UCHAR LinkedCommands : 1;
    UCHAR Synchronous : 1;
    UCHAR Wide16Bit : 1;
    UCHAR Wide32Bit : 1;
    UCHAR RelativeAddressing : 1;
    UCHAR VendorId[8];
    UCHAR ProductId[16];
    UCHAR ProductRevisionLevel[4];
    UCHAR VendorSpecific[20];
    UCHAR Reserved3[2];
    VERSION_DESCRIPTOR VersionDescriptors[8];
    UCHAR Reserved4[30];
} INQUIRYDATA, *PINQUIRYDATA;

#endif
#if !((NTDDI_VERSION < NTDDI_WINXP))

#include <pshpack1.h>
typedef struct _INQUIRYDATA {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    union {
        struct {
            UCHAR DeviceTypeModifier : 7;
            UCHAR ReservedField1 : 1;
        };
        struct {
            UCHAR ReservedField2 : 4;
            UCHAR HotPluggable : 2;
            UCHAR LU_CONG: 1;
            UCHAR RemovableMedia : 1;
        };
    };
    union {
        UCHAR Versions;
        struct {
            UCHAR ANSIVersion : 3;
            UCHAR ECMAVersion : 3;
            UCHAR ISOVersion : 2;
        };
    };
    UCHAR ResponseDataFormat : 4;
    UCHAR HiSupport : 1;
    UCHAR NormACA : 1;
    UCHAR TerminateTask : 1;
    UCHAR AERC : 1;
    UCHAR AdditionalLength;
    union {
        UCHAR Reserved;
        struct {
            UCHAR PROTECT : 1;
            UCHAR Reserved_1 : 2;
            UCHAR ThirdPartyCoppy : 1;
            UCHAR TPGS : 2;
            UCHAR ACC : 1;
            UCHAR SCCS : 1;
       };
    };
    UCHAR Addr16 : 1;
    UCHAR Addr32 : 1;
    UCHAR AckReqQ: 1;
    UCHAR MediumChanger : 1;
    UCHAR MultiPort : 1;
    UCHAR ReservedBit2 : 1;
    UCHAR EnclosureServices : 1;
    UCHAR ReservedBit3 : 1;
    UCHAR SoftReset : 1;
    UCHAR CommandQueue : 1;
    UCHAR TransferDisable : 1;
    UCHAR LinkedCommands : 1;
    UCHAR Synchronous : 1;
    UCHAR Wide16Bit : 1;
    UCHAR Wide32Bit : 1;
    UCHAR RelativeAddressing : 1;
    UCHAR VendorId[8];
    UCHAR ProductId[16];
    UCHAR ProductRevisionLevel[4];
    UCHAR VendorSpecific[20];
    UCHAR Reserved3[2];
    VERSION_DESCRIPTOR VersionDescriptors[8];
    UCHAR Reserved4[30];
} INQUIRYDATA, *PINQUIRYDATA;
#include <poppack.h>

#endif

#define DIRECT_ACCESS_DEVICE            0x00

#define SEQUENTIAL_ACCESS_DEVICE        0x01

#define PRINTER_DEVICE                  0x02

#define PROCESSOR_DEVICE                0x03

#define WRITE_ONCE_READ_MULTIPLE_DEVICE 0x04

#define READ_ONLY_DIRECT_ACCESS_DEVICE  0x05

#define SCANNER_DEVICE                  0x06

#define OPTICAL_DEVICE                  0x07

#define MEDIUM_CHANGER                  0x08

#define COMMUNICATION_DEVICE            0x09

#define ARRAY_CONTROLLER_DEVICE         0x0C

#define SCSI_ENCLOSURE_DEVICE           0x0D

#define REDUCED_BLOCK_DEVICE            0x0E

#define OPTICAL_CARD_READER_WRITER_DEVICE 0x0F

#define BRIDGE_CONTROLLER_DEVICE        0x10

#define OBJECT_BASED_STORAGE_DEVICE     0x11

#define LOGICAL_UNIT_NOT_PRESENT_DEVICE 0x7F

#define DEVICE_CONNECTED 0x00

#include <pshpack1.h>
typedef struct _VPD_MEDIA_SERIAL_NUMBER_PAGE {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR Reserved;
    UCHAR PageLength;
#if !defined(__midl)
    UCHAR SerialNumber[0];
#endif
} VPD_MEDIA_SERIAL_NUMBER_PAGE, *PVPD_MEDIA_SERIAL_NUMBER_PAGE;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _VPD_SERIAL_NUMBER_PAGE {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR Reserved;
    UCHAR PageLength;
#if !defined(__midl)
    UCHAR SerialNumber[0];
#endif
} VPD_SERIAL_NUMBER_PAGE, *PVPD_SERIAL_NUMBER_PAGE;
#include <poppack.h>

#include <pshpack1.h>
typedef enum _VPD_CODE_SET {
    VpdCodeSetReserved = 0,
    VpdCodeSetBinary = 1,
    VpdCodeSetAscii = 2,
    VpdCodeSetUTF8 = 3
} VPD_CODE_SET, *PVPD_CODE_SET;
#include <poppack.h>

#include <pshpack1.h>
typedef enum _VPD_ASSOCIATION {
    VpdAssocDevice = 0,
    VpdAssocPort = 1,
    VpdAssocTarget = 2,
    VpdAssocReserved1 = 3,
    VpdAssocReserved2 = 4
} VPD_ASSOCIATION, *PVPD_ASSOCIATION;
#include <poppack.h>

#include <pshpack1.h>
typedef enum _VPD_IDENTIFIER_TYPE {
    VpdIdentifierTypeVendorSpecific = 0,
    VpdIdentifierTypeVendorId = 1,
    VpdIdentifierTypeEUI64 = 2,
    VpdIdentifierTypeFCPHName = 3,
    VpdIdentifierTypePortRelative = 4,
    VpdIdentifierTypeTargetPortGroup = 5,
    VpdIdentifierTypeLogicalUnitGroup = 6,
    VpdIdentifierTypeMD5LogicalUnitId = 7,
    VpdIdentifierTypeSCSINameString = 8
} VPD_IDENTIFIER_TYPE, *PVPD_IDENTIFIER_TYPE;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _VPD_IDENTIFICATION_DESCRIPTOR {
    UCHAR CodeSet : 4;
    UCHAR Reserved : 4;
    UCHAR IdentifierType : 4;
    UCHAR Association : 2;
    UCHAR Reserved2 : 2;
    UCHAR Reserved3;
    UCHAR IdentifierLength;
#if !defined(__midl)
    UCHAR Identifier[0];
#endif
} VPD_IDENTIFICATION_DESCRIPTOR, *PVPD_IDENTIFICATION_DESCRIPTOR;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _VPD_IDENTIFICATION_PAGE {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR Reserved;
    UCHAR PageLength;
#if !defined(__midl)
    UCHAR Descriptors[0];
#endif
} VPD_IDENTIFICATION_PAGE, *PVPD_IDENTIFICATION_PAGE;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _VPD_EXTENDED_INQUIRY_DATA_PAGE {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR PageLength[2];
    UCHAR RefChk : 1;
    UCHAR AppChk : 1;
    UCHAR GrdChk : 1;
    UCHAR Spt : 3;
    UCHAR ActivateMicrocode : 2;
    UCHAR SimpSup : 1;
    UCHAR OrdSup : 1;
    UCHAR HeadSup : 1;
    UCHAR PriorSup : 1;
    UCHAR GroupSup : 1;
    UCHAR UaskSup : 1;
    UCHAR Reserved0 : 2;
    UCHAR VSup : 1;
    UCHAR NvSup : 1;
    UCHAR Obsolete0 : 1;
    UCHAR WuSup : 1;
    UCHAR Reserved1 : 4;
    UCHAR LuiClr : 1;
    UCHAR Reserved2 : 3;
    UCHAR PiiSup : 1;
    UCHAR NoPiChk : 1;
    UCHAR Reserved3 : 2;
    UCHAR Obsolete1 : 1;
    UCHAR HssRelef : 1;
    UCHAR Reserved4 : 1;
    UCHAR RtdSup : 1;
    UCHAR RSup : 1;
    UCHAR LuCollectionType : 3;
    UCHAR Multi_i_t_Nexus_Microcode_Download : 4;
    UCHAR Reserved5 : 4;
    UCHAR ExtendedSelfTestCompletionMinutes[2];
    UCHAR Reserved6 : 5;
    UCHAR VsaSup : 1;
    UCHAR HraSup : 1;
    UCHAR PoaSup : 1;
    UCHAR MaxSupportedSenseDataLength;
    UCHAR Nrd0 : 1;
    UCHAR Nrd1 : 1;
    UCHAR Sac : 1;
    UCHAR Reserved7 : 3;
    UCHAR Ias : 1;
    UCHAR Ibs : 1;
    UCHAR MaxInquiryChangeLogs[2];
    UCHAR MaxModePageChangeLogs[2];
    UCHAR Reserved8[45];
} VPD_EXTENDED_INQUIRY_DATA_PAGE, *PVPD_EXTENDED_INQUIRY_DATA_PAGE;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _VPD_ATA_INFORMATION_PAGE {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR PageLength[2];
    UCHAR Reserved0[4];
    UCHAR VendorId[8];
    UCHAR ProductId[16];
    UCHAR ProductRevisionLevel[4];
    UCHAR DeviceSignature[20];
    UCHAR CommandCode;
    UCHAR Reserved1[3];
    UCHAR IdentifyDeviceData[512];
} VPD_ATA_INFORMATION_PAGE, *PVPD_ATA_INFORMATION_PAGE;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _VPD_BLOCK_LIMITS_PAGE {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR PageLength[2];
    union {
        struct {
            UCHAR Reserved0;
            UCHAR MaximumCompareAndWriteLength;
            UCHAR OptimalTransferLengthGranularity[2];
            UCHAR MaximumTransferLength[4];
            UCHAR OptimalTransferLength[4];
            UCHAR MaxPrefetchXDReadXDWriteTransferLength[4];
            UCHAR MaximumUnmapLBACount[4];
            UCHAR MaximumUnmapBlockDescriptorCount[4];
            UCHAR OptimalUnmapGranularity[4];
            union {
                struct {
                    UCHAR UnmapGranularityAlignmentByte3 : 7;
                    UCHAR UGAValid : 1;
                    UCHAR UnmapGranularityAlignmentByte2;
                    UCHAR UnmapGranularityAlignmentByte1;
                    UCHAR UnmapGranularityAlignmentByte0;
                };
                UCHAR UnmapGranularityAlignment[4];
            };
            UCHAR Reserved1[28];
        };
#if !defined(__midl)
        UCHAR Descriptors[0];
#endif
    };
} VPD_BLOCK_LIMITS_PAGE, *PVPD_BLOCK_LIMITS_PAGE;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _VPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR Reserved0;
    UCHAR PageLength;
    UCHAR MediumRotationRateMsb;
    UCHAR MediumRotationRateLsb;
    UCHAR MediumProductType;
    UCHAR NominalFormFactor : 4;
    UCHAR WACEREQ           : 2;
    UCHAR WABEREQ           : 2;
    UCHAR VBULS             : 1;
    UCHAR FUAB              : 1;
    UCHAR BOCS              : 1;
    UCHAR Reserved1         : 1;
    UCHAR ZONED             : 2;
    UCHAR Reserved2         : 2;
    UCHAR Reserved3[3];
    UCHAR DepopulationTime[4];
    UCHAR Reserved4[48];
} VPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE, *PVPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _VPD_LOGICAL_BLOCK_PROVISIONING_PAGE {
    UCHAR DeviceType          : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR PageLength[2];
    UCHAR ThresholdExponent;
    UCHAR DP                : 1;
    UCHAR ANC_SUP           : 1;
    UCHAR LBPRZ             : 1;
    UCHAR Reserved0         : 2;
    UCHAR LBPWS10           : 1;
    UCHAR LBPWS             : 1;
    UCHAR LBPU              : 1;
    UCHAR ProvisioningType  : 3;
    UCHAR Reserved1         : 5;
    UCHAR Reserved2;
#if !defined(__midl)
    UCHAR ProvisioningGroupDescr[0];
#endif
} VPD_LOGICAL_BLOCK_PROVISIONING_PAGE, *PVPD_LOGICAL_BLOCK_PROVISIONING_PAGE;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _VPD_SUPPORTED_PAGES_PAGE {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR Reserved;
    UCHAR PageLength;
#if !defined(__midl)
    UCHAR SupportedPageList[0];
#endif
} VPD_SUPPORTED_PAGES_PAGE, *PVPD_SUPPORTED_PAGES_PAGE;
#include <poppack.h>

#define VPD_SUPPORTED_PAGES                0x00

#define VPD_SERIAL_NUMBER                  0x80

#define VPD_DEVICE_IDENTIFIERS             0x83

#define VPD_MEDIA_SERIAL_NUMBER            0x84

#define VPD_SOFTWARE_INTERFACE_IDENTIFIERS 0x84

#define VPD_NETWORK_MANAGEMENT_ADDRESSES   0x85

#define VPD_EXTENDED_INQUIRY_DATA          0x86

#define VPD_MODE_PAGE_POLICY               0x87

#define VPD_SCSI_PORTS                     0x88

#define VPD_ATA_INFORMATION                0x89

#define VPD_BLOCK_LIMITS                   0xB0

#define VPD_BLOCK_DEVICE_CHARACTERISTICS   0xB1

#define VPD_LOGICAL_BLOCK_PROVISIONING     0xB2

#define VER_DESCRIPTOR_1667_NOVERSION       0xFFC0

#define SCSI_WRITE_BUFFER_MODE_0D_MODE_SPECIFIC_HR_ACT    0x02

#define SCSI_WRITE_BUFFER_MODE_0D_MODE_SPECIFIC_PO_ACT    0x04

#include <pshpack1.h>
typedef struct _SENSE_DATA {
    UCHAR ErrorCode:7;
    UCHAR Valid:1;
    UCHAR SegmentNumber;
    UCHAR SenseKey:4;
    UCHAR Reserved:1;
    UCHAR IncorrectLength:1;
    UCHAR EndOfMedia:1;
    UCHAR FileMark:1;
    UCHAR Information[4];
    UCHAR AdditionalSenseLength;
    UCHAR CommandSpecificInformation[4];
    UCHAR AdditionalSenseCode;
    UCHAR AdditionalSenseCodeQualifier;
    UCHAR FieldReplaceableUnitCode;
    UCHAR SenseKeySpecific[3];
} SENSE_DATA, *PSENSE_DATA;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _SCSI_SENSE_DESCRIPTOR_HEADER {
    UCHAR DescriptorType;
    UCHAR AdditionalLength;
} SCSI_SENSE_DESCRIPTOR_HEADER, *PSCSI_SENSE_DESCRIPTOR_HEADER;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _SCSI_SENSE_DESCRIPTOR_ATA_STATUS_RETURN {
    SCSI_SENSE_DESCRIPTOR_HEADER Header;
    UCHAR Extend:1;
    UCHAR Reserved1:7;
    UCHAR Error;
    UCHAR SectorCount15_8;
    UCHAR SectorCount7_0;
    UCHAR LbaLow15_8;
    UCHAR LbaLow7_0;
    UCHAR LbaMid15_8;
    UCHAR LbaMid7_0;
    UCHAR LbaHigh15_8;
    UCHAR LbaHigh7_0;
    UCHAR Device;
    UCHAR Status;
} SCSI_SENSE_DESCRIPTOR_ATA_STATUS_RETURN, *PSCSI_SENSE_DESCRIPTOR_ATA_STATUS_RETURN;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _DESCRIPTOR_SENSE_DATA {
    UCHAR ErrorCode:7;
    UCHAR Reserved1:1;
    UCHAR SenseKey:4;
    UCHAR Reserved2:4;
    UCHAR AdditionalSenseCode;
    UCHAR AdditionalSenseCodeQualifier;
    UCHAR Reserved3[3];
    UCHAR AdditionalSenseLength;
    UCHAR DescriptorBuffer[ANYSIZE_ARRAY];
} DESCRIPTOR_SENSE_DATA, *PDESCRIPTOR_SENSE_DATA;
#include <poppack.h>

#define SENSE_BUFFER_SIZE sizeof(SENSE_DATA)

#define MAX_SENSE_BUFFER_SIZE 255

#define SCSI_SENSE_ERRORCODE_FIXED_CURRENT        0x70

#define SCSI_SENSE_ERRORCODE_DESCRIPTOR_CURRENT   0x72

#define SCSI_SENSE_DESCRIPTOR_TYPE_ATA_STATUS_RETURN            0x09

#define SCSI_SENSE_NO_SENSE         0x00

#define SCSI_SENSE_RECOVERED_ERROR  0x01

#define SCSI_SENSE_NOT_READY        0x02

#define SCSI_SENSE_MEDIUM_ERROR     0x03

#define SCSI_SENSE_HARDWARE_ERROR   0x04

#define SCSI_SENSE_ILLEGAL_REQUEST  0x05

#define SCSI_SENSE_UNIT_ATTENTION   0x06

#define SCSI_SENSE_DATA_PROTECT     0x07

#define SCSI_SENSE_BLANK_CHECK      0x08

#define SCSI_SENSE_UNIQUE           0x09

#define SCSI_SENSE_COPY_ABORTED     0x0A

#define SCSI_SENSE_ABORTED_COMMAND  0x0B

#define SCSI_SENSE_EQUAL            0x0C

#define SCSI_SENSE_VOL_OVERFLOW     0x0D

#define SCSI_SENSE_MISCOMPARE       0x0E

#define SCSI_SENSE_RESERVED         0x0F

#define SCSI_ADSENSE_LUN_COMMUNICATION                     0x08

#define SCSI_ADSENSE_ILLEGAL_BLOCK                         0x21

#define SCSI_ADSENSE_INVALID_CDB                           0x24

#define SCSI_ADSENSE_MEDIUM_CHANGED                        0x28

#define SCSI_ADSENSE_NO_MEDIA_IN_DEVICE                    0x3a

#define SCSI_ADSENSE_INTERNAL_TARGET_FAILURE               0x44

#define SCSI_ADSENSE_OPERATOR_REQUEST                      0x5a

#define SCSI_SESNEQ_COMM_CRC_ERROR               0x03

#define SCSI_SENSEQ_MEDIUM_REMOVAL                      0x01

#define FILE_DEVICE_SCSI 0x0000001b

#define IOCTL_SCSI_EXECUTE_IN   ((FILE_DEVICE_SCSI << 16) + 0x0011)

#define IOCTL_SCSI_EXECUTE_OUT  ((FILE_DEVICE_SCSI << 16) + 0x0012)

#define IOCTL_SCSI_EXECUTE_NONE ((FILE_DEVICE_SCSI << 16) + 0x0013)

#define IOCTL_SCSI_MINIPORT_SMART_VERSION           ((FILE_DEVICE_SCSI << 16) + 0x0500)

#define IOCTL_SCSI_MINIPORT_IDENTIFY                ((FILE_DEVICE_SCSI << 16) + 0x0501)

#define IOCTL_SCSI_MINIPORT_READ_SMART_ATTRIBS      ((FILE_DEVICE_SCSI << 16) + 0x0502)

#define IOCTL_SCSI_MINIPORT_READ_SMART_THRESHOLDS   ((FILE_DEVICE_SCSI << 16) + 0x0503)

#define IOCTL_SCSI_MINIPORT_ENABLE_SMART            ((FILE_DEVICE_SCSI << 16) + 0x0504)

#define IOCTL_SCSI_MINIPORT_DISABLE_SMART           ((FILE_DEVICE_SCSI << 16) + 0x0505)

#define IOCTL_SCSI_MINIPORT_RETURN_STATUS           ((FILE_DEVICE_SCSI << 16) + 0x0506)

#define IOCTL_SCSI_MINIPORT_ENABLE_DISABLE_AUTOSAVE ((FILE_DEVICE_SCSI << 16) + 0x0507)

#define IOCTL_SCSI_MINIPORT_SAVE_ATTRIBUTE_VALUES   ((FILE_DEVICE_SCSI << 16) + 0x0508)

#define IOCTL_SCSI_MINIPORT_EXECUTE_OFFLINE_DIAGS   ((FILE_DEVICE_SCSI << 16) + 0x0509)

#define IOCTL_SCSI_MINIPORT_ENABLE_DISABLE_AUTO_OFFLINE ((FILE_DEVICE_SCSI << 16) + 0x050a)

#define IOCTL_SCSI_MINIPORT_READ_SMART_LOG          ((FILE_DEVICE_SCSI << 16) + 0x050b)

#define IOCTL_SCSI_MINIPORT_WRITE_SMART_LOG         ((FILE_DEVICE_SCSI << 16) + 0x050c)

#define IOCTL_SCSI_MINIPORT_DSM_GENERAL                 ((FILE_DEVICE_SCSI << 16) + 0x0721)

#include <pshpack1.h>
typedef struct _READ_CAPACITY_DATA {
    ULONG LogicalBlockAddress;
    ULONG BytesPerBlock;
} READ_CAPACITY_DATA, *PREAD_CAPACITY_DATA;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _READ_CAPACITY_DATA_EX {
    LARGE_INTEGER LogicalBlockAddress;
    ULONG BytesPerBlock;
} READ_CAPACITY_DATA_EX, *PREAD_CAPACITY_DATA_EX;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _READ_CAPACITY16_DATA {
    LARGE_INTEGER LogicalBlockAddress;
    ULONG BytesPerBlock;
    UCHAR ProtectionEnable : 1;
    UCHAR ProtectionType : 3;
    UCHAR RcBasis  : 2;
    UCHAR Reserved : 2;
    UCHAR LogicalPerPhysicalExponent : 4;
    UCHAR ProtectionInfoExponent : 4;
    UCHAR LowestAlignedBlock_MSB : 6;
    UCHAR LBPRZ : 1;
    UCHAR LBPME : 1;
    UCHAR LowestAlignedBlock_LSB;
    UCHAR Reserved3[16];
} READ_CAPACITY16_DATA, *PREAD_CAPACITY16_DATA;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _MODE_PARAMETER_HEADER {
    UCHAR ModeDataLength;
    UCHAR MediumType;
    UCHAR DeviceSpecificParameter;
    UCHAR BlockDescriptorLength;
}MODE_PARAMETER_HEADER, *PMODE_PARAMETER_HEADER;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _MODE_PARAMETER_HEADER10 {
    UCHAR ModeDataLength[2];
    UCHAR MediumType;
    UCHAR DeviceSpecificParameter;
    UCHAR Reserved[2];
    UCHAR BlockDescriptorLength[2];
}MODE_PARAMETER_HEADER10, *PMODE_PARAMETER_HEADER10;
#include <poppack.h>

#define MODE_DSP_FUA_SUPPORTED  0x10

#define MODE_DSP_WRITE_PROTECT  0x80

#include <pshpack1.h>
typedef struct _MODE_PARAMETER_BLOCK {
    UCHAR DensityCode;
    UCHAR NumberOfBlocks[3];
    UCHAR Reserved;
    UCHAR BlockLength[3];
}MODE_PARAMETER_BLOCK, *PMODE_PARAMETER_BLOCK;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _MODE_CACHING_PAGE {
    UCHAR PageCode : 6;
    UCHAR Reserved : 1;
    UCHAR PageSavable : 1;
    UCHAR PageLength;
    UCHAR ReadDisableCache : 1;
    UCHAR MultiplicationFactor : 1;
    UCHAR WriteCacheEnable : 1;
    UCHAR Reserved2 : 5;
    UCHAR WriteRetensionPriority : 4;
    UCHAR ReadRetensionPriority : 4;
    UCHAR DisablePrefetchTransfer[2];
    UCHAR MinimumPrefetch[2];
    UCHAR MaximumPrefetch[2];
    UCHAR MaximumPrefetchCeiling[2];
}MODE_CACHING_PAGE, *PMODE_CACHING_PAGE;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _LUN_LIST {
    UCHAR LunListLength[4];
    UCHAR Reserved[4];
#if !defined(__midl)
    UCHAR Lun[0][8];
#endif
} LUN_LIST, *PLUN_LIST;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _UNMAP_BLOCK_DESCRIPTOR {
    UCHAR StartingLba[8];
    UCHAR LbaCount[4];
    UCHAR Reserved[4];
} UNMAP_BLOCK_DESCRIPTOR, *PUNMAP_BLOCK_DESCRIPTOR;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _UNMAP_LIST_HEADER {
    UCHAR DataLength[2];
    UCHAR BlockDescrDataLength[2];
    UCHAR Reserved[4];
#if !defined(__midl)
    UNMAP_BLOCK_DESCRIPTOR Descriptors[0];
#endif
} UNMAP_LIST_HEADER, *PUNMAP_LIST_HEADER;
#include <poppack.h>

#include <pshpack1.h>
typedef union _EIGHT_BYTE {
    struct {
        UCHAR Byte0;
        UCHAR Byte1;
        UCHAR Byte2;
        UCHAR Byte3;
        UCHAR Byte4;
        UCHAR Byte5;
        UCHAR Byte6;
        UCHAR Byte7;
    };
    ULONGLONG AsULongLong;
} EIGHT_BYTE, *PEIGHT_BYTE;
#include <poppack.h>

#include <pshpack1.h>
typedef union _FOUR_BYTE {
    struct {
        UCHAR Byte0;
        UCHAR Byte1;
        UCHAR Byte2;
        UCHAR Byte3;
    };
    ULONG AsULong;
} FOUR_BYTE, *PFOUR_BYTE;
#include <poppack.h>

#include <pshpack1.h>
typedef union _TWO_BYTE {
    struct {
        UCHAR Byte0;
        UCHAR Byte1;
    };
    USHORT AsUShort;
} TWO_BYTE, *PTWO_BYTE;
#include <poppack.h>

#define REVERSE_BYTES_QUAD REVERSE_BYTES_8

#define REVERSE_BYTES_8(Destination, Source) {              \
    PEIGHT_BYTE d = (PEIGHT_BYTE)(Destination);             \
    PEIGHT_BYTE s = (PEIGHT_BYTE)(Source);                  \
    d->Byte7 = s->Byte0;                                    \
    d->Byte6 = s->Byte1;                                    \
    d->Byte5 = s->Byte2;                                    \
    d->Byte4 = s->Byte3;                                    \
    d->Byte3 = s->Byte4;                                    \
    d->Byte2 = s->Byte5;                                    \
    d->Byte1 = s->Byte6;                                    \
    d->Byte0 = s->Byte7;                                    \
}

#define REVERSE_BYTES REVERSE_BYTES_4

#define REVERSE_BYTES_4(Destination, Source) {              \
    PFOUR_BYTE d = (PFOUR_BYTE)(Destination);               \
    PFOUR_BYTE s = (PFOUR_BYTE)(Source);                    \
    d->Byte3 = s->Byte0;                                    \
    d->Byte2 = s->Byte1;                                    \
    d->Byte1 = s->Byte2;                                    \
    d->Byte0 = s->Byte3;                                    \
}

#define REVERSE_BYTES_SHORT REVERSE_BYTES_2

#define REVERSE_BYTES_2(Destination, Source) {              \
    PTWO_BYTE d = (PTWO_BYTE)(Destination);                 \
    PTWO_BYTE s = (PTWO_BYTE)(Source);                      \
    d->Byte1 = s->Byte0;                                    \
    d->Byte0 = s->Byte1;                                    \
}

#define REVERSE_SHORT(Short) {          \
    UCHAR tmp;                          \
    PTWO_BYTE w = (PTWO_BYTE)(Short);   \
    tmp = w->Byte0;                     \
    w->Byte0 = w->Byte1;                \
    w->Byte1 = tmp;                     \
    }

#define REVERSE_LONG(Long) {            \
    UCHAR tmp;                          \
    PFOUR_BYTE l = (PFOUR_BYTE)(Long);  \
    tmp = l->Byte3;                     \
    l->Byte3 = l->Byte0;                \
    l->Byte0 = tmp;                     \
    tmp = l->Byte2;                     \
    l->Byte2 = l->Byte1;                \
    l->Byte1 = tmp;                     \
    }

typedef struct STOR_ADDRESS_ALIGN _STOR_ADDRESS {
    USHORT Type;
    USHORT Port;
    ULONG AddressLength;
    _Field_size_bytes_(AddressLength) UCHAR AddressData[ANYSIZE_ARRAY];
} STOR_ADDRESS, *PSTOR_ADDRESS;

#define STOR_ADDRESS_TYPE_UNKNOWN   0x0

#define STOR_ADDRESS_TYPE_BTL8      0x1

#define STOR_ADDRESS_TYPE_MAX       0xffff

#define STOR_ADDR_BTL8_ADDRESS_LENGTH    4

typedef struct STOR_ADDRESS_ALIGN _STOR_ADDR_BTL8 {
    _Field_range_(STOR_ADDRESS_TYPE_BTL8, STOR_ADDRESS_TYPE_BTL8)
    USHORT Type;
    USHORT Port;
    _Field_range_(STOR_ADDR_BTL8_ADDRESS_LENGTH, STOR_ADDR_BTL8_ADDRESS_LENGTH)
    ULONG AddressLength;
    UCHAR Path;
    UCHAR Target;
    UCHAR Lun;
    UCHAR Reserved;
} STOR_ADDR_BTL8, *PSTOR_ADDR_BTL8;

#include <pshpack1.h>
typedef struct _PHYSICAL_ELEMENT_STATUS_DATA_DESCRIPTOR {
    UCHAR Reserved1[4];
    UCHAR ElementIdentifier[4];
    UCHAR Reserved2[6];
    UCHAR PhysicalElementType;
    UCHAR PhysicalElementHealth;
    UCHAR AssociatedCapacity[8];
    UCHAR Reserved3[8];
} PHYSICAL_ELEMENT_STATUS_DATA_DESCRIPTOR, *PPHYSICAL_ELEMENT_STATUS_DATA_DESCRIPTOR;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _PHYSICAL_ELEMENT_STATUS_PARAMETER_DATA {
    UCHAR DescriptorCount[4];
    UCHAR ReturnedDescriptorCount[4];
    UCHAR ElementIdentifierBeingDepoped[4];
    UCHAR Reserved[20];
    PHYSICAL_ELEMENT_STATUS_DATA_DESCRIPTOR Descriptors[ANYSIZE_ARRAY];
} PHYSICAL_ELEMENT_STATUS_PARAMETER_DATA, *PPHYSICAL_ELEMENT_STATUS_PARAMETER_DATA;
#include <poppack.h>

#define READ_BUFFER_MODE_ERROR_HISTORY                                              0x1C

#define BUFFER_ID_RETURN_ERROR_HISTORY_DIRECTORY                                    0x0

#define BUFFER_ID_RETURN_ERROR_HISTORY_MINIMUM_THRESHOLD                            0x10

#define BUFFER_ID_RETURN_ERROR_HISTORY_MAXIMUM_THRESHOLD                            0xEF

#define BUFFER_FORMAT_CURRENT_INTERNAL_STATUS_DATA                                  0x1

#define BUFFER_SOURCE_CREATED_DUE_TO_CURRENT_COMMAND                                0x3

#include <pshpack1.h>
typedef struct _ERROR_HISTORY_DIRECTORY_ENTRY {
    UCHAR SupportedBufferId;
    UCHAR BufferFormat;
    UCHAR BufferSource : 4;
    UCHAR Reserved0 : 4;
    UCHAR Reserved1;
    UCHAR MaxAvailableLength[4];
} ERROR_HISTORY_DIRECTORY_ENTRY, *PERROR_HISTORY_DIRECTORY_ENTRY;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _ERROR_HISTORY_DIRECTORY {
    UCHAR T10VendorId[8];
    UCHAR ErrorHistoryVersion;
    UCHAR ClearSupport : 1;
    UCHAR ErrorHistorySource : 2;
    UCHAR ErrorHistoryRetrieved : 2;
    UCHAR Reserved0 : 3;
    UCHAR Reserved1[20];
    UCHAR DirectoryLength[2];
    ERROR_HISTORY_DIRECTORY_ENTRY ErrorHistoryDirectoryList[ANYSIZE_ARRAY];
} ERROR_HISTORY_DIRECTORY, *PERROR_HISTORY_DIRECTORY;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _CURRENT_INTERNAL_STATUS_PARAMETER_DATA {
    UCHAR Reserved0[4];
    UCHAR IEEECompanyId[4];
    UCHAR CurrentInternalStatusDataSetOneLength[2];
    UCHAR CurrentInternalStatusDataSetTwoLength[2];
    UCHAR CurrentInternalStatusDataSetThreeLength[2];
    UCHAR CurrentInternalStatusDataSetFourLength[4];
    UCHAR Reserved1[364];
    UCHAR NewSavedDataAvailable;
    UCHAR SavedDataGenerationNumber;
    UCHAR CurrentReasonIdentifier[128];
    UCHAR CurrentInternalStatusData[ANYSIZE_ARRAY];
} CURRENT_INTERNAL_STATUS_PARAMETER_DATA, *PCURRENT_INTERNAL_STATUS_PARAMETER_DATA;
#include <poppack.h>

typedef PHYSICAL_ADDRESS STOR_PHYSICAL_ADDRESS, *PSTOR_PHYSICAL_ADDRESS;

typedef struct _ACCESS_RANGE {
    STOR_PHYSICAL_ADDRESS RangeStart;
    ULONG RangeLength;
    BOOLEAN RangeInMemory;
} ACCESS_RANGE, *PACCESS_RANGE;

typedef struct _MEMORY_REGION {
    PUCHAR VirtualBase;
    PHYSICAL_ADDRESS PhysicalBase;
    ULONG Length;
} MEMORY_REGION, *PMEMORY_REGION;

typedef enum _STOR_SYNCHRONIZATION_MODEL {
    StorSynchronizeHalfDuplex,
    StorSynchronizeFullDuplex
} STOR_SYNCHRONIZATION_MODEL;

typedef enum _INTERRUPT_SYNCHRONIZATION_MODE {
    InterruptSupportNone,
    InterruptSynchronizeAll,
    InterruptSynchronizePerMessage
} INTERRUPT_SYNCHRONIZATION_MODE;

typedef
BOOLEAN
NTAPI
HW_MESSAGE_SIGNALED_INTERRUPT_ROUTINE (
    IN PVOID HwDeviceExtension,
    IN ULONG MessageId
    );

typedef HW_MESSAGE_SIGNALED_INTERRUPT_ROUTINE *PHW_MESSAGE_SIGNALED_INTERRUPT_ROUTINE;

typedef struct _MESSAGE_INTERRUPT_INFORMATION {
    ULONG MessageId;
    ULONG MessageData;
    STOR_PHYSICAL_ADDRESS MessageAddress;
    ULONG InterruptVector;
    ULONG InterruptLevel;
    KINTERRUPT_MODE InterruptMode;
} MESSAGE_INTERRUPT_INFORMATION, *PMESSAGE_INTERRUPT_INFORMATION;

#define STOR_MAP_NO_BUFFERS                         (0)

#define STOR_MAP_ALL_BUFFERS                        (1)

#define STOR_MAP_NON_READ_WRITE_BUFFERS             (2)

typedef struct _PORT_CONFIGURATION_INFORMATION {
    ULONG Length;
    ULONG SystemIoBusNumber;
    INTERFACE_TYPE  AdapterInterfaceType;
    ULONG BusInterruptLevel;
    ULONG BusInterruptVector;
    KINTERRUPT_MODE InterruptMode;
    ULONG MaximumTransferLength;
    ULONG NumberOfPhysicalBreaks;
    ULONG DmaChannel;
    ULONG DmaPort;
    DMA_WIDTH DmaWidth;
    DMA_SPEED DmaSpeed;
    ULONG AlignmentMask;
    ULONG NumberOfAccessRanges;
    ACCESS_RANGE (*AccessRanges)[];
#if (NTDDI_VERSION >= NTDDI_WIN8)
    PVOID MiniportDumpData;
#else
    PVOID Reserved;
#endif
    UCHAR NumberOfBuses;
    CCHAR InitiatorBusId[8];
    BOOLEAN ScatterGather;
    BOOLEAN Master;
    BOOLEAN CachesData;
    BOOLEAN AdapterScansDown;
    BOOLEAN AtdiskPrimaryClaimed;
    BOOLEAN AtdiskSecondaryClaimed;
    BOOLEAN Dma32BitAddresses;
    BOOLEAN DemandMode;
    UCHAR MapBuffers;
    BOOLEAN NeedPhysicalAddresses;
    BOOLEAN TaggedQueuing;
    BOOLEAN AutoRequestSense;
    BOOLEAN MultipleRequestPerLu;
    BOOLEAN ReceiveEvent;
    BOOLEAN RealModeInitialized;
    BOOLEAN BufferAccessScsiPortControlled;
    UCHAR MaximumNumberOfTargets;
#if (NTDDI_VERSION >= NTDDI_WIN8)
    UCHAR SrbType;
    UCHAR AddressType;
#else
    UCHAR ReservedUchars[2];
#endif
    ULONG SlotNumber;
    ULONG BusInterruptLevel2;
    ULONG BusInterruptVector2;
    KINTERRUPT_MODE InterruptMode2;
    ULONG DmaChannel2;
    ULONG DmaPort2;
    DMA_WIDTH DmaWidth2;
    DMA_SPEED DmaSpeed2;
    ULONG DeviceExtensionSize;
    ULONG SpecificLuExtensionSize;
    ULONG SrbExtensionSize;
    UCHAR Dma64BitAddresses;
    BOOLEAN ResetTargetSupported;
    UCHAR MaximumNumberOfLogicalUnits;
    BOOLEAN WmiDataProvider;
    STOR_SYNCHRONIZATION_MODEL SynchronizationModel;
    PHW_MESSAGE_SIGNALED_INTERRUPT_ROUTINE HwMSInterruptRoutine;
    INTERRUPT_SYNCHRONIZATION_MODE InterruptSynchronizationMode;
    MEMORY_REGION DumpRegion;
    ULONG RequestedDumpBufferSize;
    BOOLEAN VirtualDevice;
#if (NTDDI_VERSION >= NTDDI_WIN8)
    UCHAR DumpMode;
#if (NTDDI_VERSION >= NTDDI_WIN10_VB)
    UCHAR DmaAddressWidth;
#endif
#endif
    ULONG ExtendedFlags1;
    ULONG MaxNumberOfIO;
#if (NTDDI_VERSION >= NTDDI_WIN8)
    ULONG MaxIOsPerLun;
    ULONG InitialLunQueueDepth;
    ULONG BusResetHoldTime;
    ULONG FeatureSupport;
#endif
} PORT_CONFIGURATION_INFORMATION, *PPORT_CONFIGURATION_INFORMATION;

#define STOR_ADAPTER_FEATURE_DEVICE_TELEMETRY               0x00000001

#define STOR_ADAPTER_FEATURE_STOP_UNIT_DURING_POWER_DOWN    0x00000002

#define STOR_ADAPTER_DMA_V3_PREFERRED                       0x00000008

#define STOR_ADAPTER_FEATURE_RICH_TEMPERATURE_THRESHOLD     0x00000020

#define DUMP_MODE_CRASH             0x01

#define DUMP_MODE_HIBER             0x02

#define DUMP_MODE_MARK_MEMORY       0x03

#define DUMP_MODE_RESUME            0x04

typedef struct _STOR_SCATTER_GATHER_ELEMENT {
    STOR_PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Length;
    ULONG_PTR Reserved;
} STOR_SCATTER_GATHER_ELEMENT, *PSTOR_SCATTER_GATHER_ELEMENT;

typedef struct _STOR_SCATTER_GATHER_LIST {
    ULONG NumberOfElements;
    ULONG_PTR Reserved;
    STOR_SCATTER_GATHER_ELEMENT List[];
} STOR_SCATTER_GATHER_LIST, *PSTOR_SCATTER_GATHER_LIST;

typedef enum _GETSGSTATUS{
    SG_ALLOCATED = 0,
    SG_BUFFER_TOO_SMALL
} GETSGSTATUS, *PGETSGSTATUS;

#define SCSI_DMA64_SYSTEM_SUPPORTED     0x80

#define SCSI_DMA64_MINIPORT_SUPPORTED   0x01

#if ((NTDDI_VERSION >= NTDDI_WS03SP1))

#define SCSI_DMA64_MINIPORT_FULL64BIT_SUPPORTED 0x02

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define SCSI_DMA64_MINIPORT_FULL64BIT_NO_BOUNDARY_REQ_SUPPORTED 0x03

#endif

typedef enum _SCSI_ADAPTER_CONTROL_TYPE {
    ScsiQuerySupportedControlTypes = 0,
    ScsiStopAdapter,
    ScsiRestartAdapter,
    ScsiSetBootConfig,
    ScsiSetRunningConfig,
    ScsiPowerSettingNotification,
    ScsiAdapterPower,
    ScsiAdapterPoFxPowerRequired,
    ScsiAdapterPoFxPowerActive,
    ScsiAdapterPoFxPowerSetFState,
    ScsiAdapterPoFxPowerControl,
    ScsiAdapterPrepareForBusReScan,
    ScsiAdapterSystemPowerHints,
    ScsiAdapterFilterResourceRequirements,
    ScsiAdapterPoFxMaxOperationalPower,
    ScsiAdapterPoFxSetPerfState,
    ScsiAdapterSurpriseRemoval,
    ScsiAdapterSerialNumber,
    ScsiAdapterCryptoOperation,
    ScsiAdapterQueryFruId,
    ScsiAdapterSetEventLogging,
    ScsiAdapterReportInternalData,
    ScsiAdapterResetBusSynchronous,
    ScsiAdapterPostHwInitialize,
    ScsiAdapterPrepareEarlyDumpData,
    ScsiAdapterRestoreEarlyDumpData,
    ScsiAdapterKsrPowerDown,
    ScsiAdapterPreparePLDR,
    ScsiNvmeofAdapterOperation,
    ScsiAdapterControlMax,
    MakeAdapterControlTypeSizeOfUlong = 0xffffffff
} SCSI_ADAPTER_CONTROL_TYPE, *PSCSI_ADAPTER_CONTROL_TYPE;

typedef struct _STOR_POWER_CONTROL_HEADER {
    ULONG Version;
    ULONG Size;
    PSTOR_ADDRESS Address;
} STOR_POWER_CONTROL_HEADER, *PSTOR_POWER_CONTROL_HEADER;

typedef enum _SCSI_ADAPTER_CONTROL_STATUS {
    ScsiAdapterControlSuccess = 0,
    ScsiAdapterControlUnsuccessful,
    ScsiAdapterControlRetryNeeded,
    ScsiAdapterControlBufferTooSmall
} SCSI_ADAPTER_CONTROL_STATUS, *PSCSI_ADAPTER_CONTROL_STATUS;

typedef struct _STOR_POWER_SETTING_INFO {
    GUID    PowerSettingGuid;
    _Field_size_bytes_(ValueLength) PVOID Value;
    ULONG   ValueLength;
} STOR_POWER_SETTING_INFO, *PSTOR_POWER_SETTING_INFO;

typedef struct _STOR_ADAPTER_CONTROL_POWER {
    STOR_POWER_CONTROL_HEADER   Header;
    STOR_POWER_ACTION           PowerAction;
    STOR_DEVICE_POWER_STATE     PowerState;
} STOR_ADAPTER_CONTROL_POWER, *PSTOR_ADAPTER_CONTROL_POWER;

typedef enum _RAID_SYSTEM_POWER {
    RaidSystemPowerUnknown = 0,
    RaidSystemPowerLowest,
    RaidSystemPowerLow,
    RaidSystemPowerMedium,
    RaidSystemPowerHigh
} RAID_SYSTEM_POWER, *PRAID_SYSTEM_POWER;

typedef struct _STOR_SYSTEM_POWER_HINTS {
    ULONG Version;
    ULONG Size;
    RAID_SYSTEM_POWER SystemPower;
    ULONG ResumeLatencyMSec;
} STOR_SYSTEM_POWER_HINTS, *PSTOR_SYSTEM_POWER_HINTS;

typedef struct _STOR_FILTER_RESOURCE_REQUIREMENTS {
    ULONG Version;
    ULONG Size;
    PIO_RESOURCE_REQUIREMENTS_LIST IoResourceRequirementsList;
} STOR_FILTER_RESOURCE_REQUIREMENTS, *PSTOR_FILTER_RESOURCE_REQUIREMENTS;

#define STOR_FILTER_RESOURCE_REQUIREMENTS_V1 0x1

typedef enum _SCSI_UNIT_CONTROL_TYPE {
    ScsiQuerySupportedUnitControlTypes = 0,
    ScsiUnitUsage,
    ScsiUnitStart,
    ScsiUnitPower,
    ScsiUnitPoFxPowerInfo,
    ScsiUnitPoFxPowerRequired,
    ScsiUnitPoFxPowerActive,
    ScsiUnitPoFxPowerSetFState,
    ScsiUnitPoFxPowerControl,
    ScsiUnitRemove,
    ScsiUnitSurpriseRemoval,
    ScsiUnitRichDescription,
    ScsiUnitQueryBusType,
    ScsiUnitQueryFruId,
    ScsiUnitReportInternalData,
    ScsiUnitKsrPowerDown,
    ScsiUnitControlMax,
    MakeUnitControlTypeSizeOfUlong = 0xffffffff
} SCSI_UNIT_CONTROL_TYPE, *PSCSI_UNIT_CONTROL_TYPE;

typedef enum _SCSI_UNIT_CONTROL_STATUS {
    ScsiUnitControlSuccess = 0,
    ScsiUnitControlUnsuccessful
} SCSI_UNIT_CONTROL_STATUS, *PSCSI_UNIT_CONTROL_STATUS;

typedef struct _STOR_UNIT_CONTROL_POWER {
    PSTOR_ADDRESS               Address;
    STOR_POWER_ACTION           PowerAction;
    STOR_DEVICE_POWER_STATE     PowerState;
} STOR_UNIT_CONTROL_POWER, *PSTOR_UNIT_CONTROL_POWER;

typedef struct _STOR_POFX_UNIT_POWER_INFO {
    STOR_POWER_CONTROL_HEADER   Header;
    BOOLEAN                 IdlePowerEnabled;
} STOR_POFX_UNIT_POWER_INFO, *PSTOR_POFX_UNIT_POWER_INFO;

typedef struct _STOR_POFX_ACTIVE_CONTEXT {
    STOR_POWER_CONTROL_HEADER   Header;
    ULONG                       ComponentIndex;
    BOOLEAN                     Active;
} STOR_POFX_ACTIVE_CONTEXT, *PSTOR_POFX_ACTIVE_CONTEXT;

typedef struct _STOR_POFX_FSTATE_CONTEXT {
    STOR_POWER_CONTROL_HEADER   Header;
    ULONG                       ComponentIndex;
    ULONG                       FState;
} STOR_POFX_FSTATE_CONTEXT, *PSTOR_POFX_FSTATE_CONTEXT;

typedef struct _SCSI_SUPPORTED_CONTROL_TYPE_LIST {
    ULONG MaxControlType;
    BOOLEAN SupportedTypeList[0];
} SCSI_SUPPORTED_CONTROL_TYPE_LIST, *PSCSI_SUPPORTED_CONTROL_TYPE_LIST;

typedef struct _DPC_BUFFER {
    CSHORT Type;
    UCHAR Number;
    UCHAR Importance;
    struct {
        PVOID F;
        PVOID B;
    };
    PVOID DeferredRoutine;
    PVOID DeferredContext;
    PVOID SystemArgument1;
    PVOID SystemArgument2;
    PVOID DpcData;
} DPC_BUFFER;

typedef struct _STOR_DPC {
    DPC_BUFFER Dpc;
    ULONG_PTR Lock;
} STOR_DPC, *PSTOR_DPC;

typedef enum _STOR_SPINLOCK {
    InvalidLock = 0,
    DpcLock = 1,
    StartIoLock,
    InterruptLock,
    ThreadedDpcLock,
    DpcLevelLock
} STOR_SPINLOCK;

typedef struct _STOR_LOCK_HANDLE {
    STOR_SPINLOCK Lock;
    struct {
        struct {
            PVOID Next;
            PVOID Lock;
        } LockQueue;
        KIRQL OldIrql;
    } Context;
} STOR_LOCK_HANDLE, *PSTOR_LOCK_HANDLE;

#define STOR_PERF_DPC_REDIRECTION 0x00000001

#define STOR_PERF_CONCURRENT_CHANNELS 0x00000002

#define STOR_PERF_INTERRUPT_MESSAGE_RANGES 0x00000004

#define STOR_PERF_ADV_CONFIG_LOCALITY 0x00000008

#define STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO 0x00000010

#define STOR_PERF_DPC_REDIRECTION_CURRENT_CPU 0x00000020

#define STOR_PERF_NO_SGL 0x00000040

#define STOR_PERF_SOFT_NUMA 0x00000080

#define STOR_PERF_HETEROGENEOUS_CPU 0x00000100

#define STOR_PERF_VERSION_2 0x00000002

#define STOR_PERF_VERSION_3 0x00000003

#define STOR_PERF_VERSION_4 0x00000004

#define STOR_PERF_VERSION_5 0x00000005

#define STOR_PERF_VERSION_6 0x00000006

#define STOR_PERF_VERSION_7 0x00000007

#define STOR_PERF_VERSION_8 0x00000008

#define STOR_PERF_VERSION STOR_PERF_VERSION_8

typedef struct _PERF_CONFIGURATION_DATA {
    ULONG Version;
    ULONG Size;
    ULONG Flags;
    ULONG ConcurrentChannels;
    ULONG FirstRedirectionMessageNumber, LastRedirectionMessageNumber;
    ULONG DeviceNode;
    ULONG Reserved;
    PGROUP_AFFINITY MessageTargets;
} PERF_CONFIGURATION_DATA, *PPERF_CONFIGURATION_DATA;

typedef struct _STARTIO_PERFORMANCE_PARAMETERS {
    ULONG Version;
    ULONG Size;
    ULONG MessageNumber;
    ULONG ChannelNumber;
} STARTIO_PERFORMANCE_PARAMETERS, *PSTARTIO_PERFORMANCE_PARAMETERS;

typedef struct _STOR_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
#ifdef MIDL_PASS
    [size_is(MaximumLength / 2), length_is((Length) / 2) ] USHORT * Buffer;
#else
    _Field_size_bytes_part_opt_(MaximumLength, Length) PWCH   Buffer;
#endif
} STOR_UNICODE_STRING, *PSTOR_UNICODE_STRING;

typedef enum _STOR_IO_PRIORITY_HINT {
    StorIoPriorityVeryLow = 0,
    StorIoPriorityLow,
    StorIoPriorityNormal,
    StorIoPriorityHigh,
    StorIoPriorityCritical,
    StorIoMaxPriorityTypes,
    StorIoMaxPriorityValue = 0xffff
} STOR_IO_PRIORITY_HINT, *PSTOR_IO_PRIORITY_HINT;

typedef struct _STOR_REQUEST_INFO_V2 {
    USHORT Version;
    USHORT Size;
    STOR_IO_PRIORITY_HINT PriorityHint;
    ULONG Flags;
    ULONG Key;
    ULONG Length;
    BOOLEAN IsWriteRequest;
    UCHAR Reserved[3];
    PSTOR_UNICODE_STRING FileName;
    ULONG ProcessId;
} STOR_REQUEST_INFO_V2, *PSTOR_REQUEST_INFO_V2;

typedef STOR_REQUEST_INFO_V2 STOR_REQUEST_INFO, *PSTOR_REQUEST_INFO;

#define STATE_CHANGE_TARGET     0x2

#define RAID_ASYNC_NOTIFY_FLAG_MEDIA_STATUS        0x1

#define RAID_ASYNC_NOTIFY_FLAG_DEVICE_STATUS       0x2

#define RAID_ASYNC_NOTIFY_FLAG_DEVICE_OPERATION    0x4

typedef struct _STOR_LIST_ENTRY {
    struct _STOR_LIST_ENTRY *Flink;
    struct _STOR_LIST_ENTRY *Blink;
} STOR_LIST_ENTRY, *PSTOR_LIST_ENTRY;

typedef
ULONG
NTAPI
sp_DRIVER_INITIALIZE (
    _In_ PVOID DriverObject,
    _In_ PVOID RegistryPath
    );

typedef
BOOLEAN
NTAPI
HW_INITIALIZE (
    _In_ PVOID DeviceExtension
    );

typedef HW_INITIALIZE *PHW_INITIALIZE;

typedef
BOOLEAN
NTAPI
HW_BUILDIO (
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    );

typedef HW_BUILDIO *PHW_BUILDIO;

typedef
BOOLEAN
NTAPI
HW_STARTIO (
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    );

typedef HW_STARTIO *PHW_STARTIO;

typedef
BOOLEAN
NTAPI
HW_INTERRUPT (
    _In_ PVOID DeviceExtension
    );

typedef HW_INTERRUPT *PHW_INTERRUPT;

typedef
VOID
NTAPI
HW_TIMER (
    _In_ PVOID DeviceExtension
    );

typedef HW_TIMER *PHW_TIMER;

typedef
VOID
NTAPI
HW_TIMER_EX (
    _In_ PVOID DeviceExtension,
    _In_opt_ PVOID Context
    );

typedef HW_TIMER_EX *PHW_TIMER_EX;

typedef
VOID
NTAPI
HW_DMA_STARTED (
    _In_ PVOID DeviceExtension
    );

typedef HW_DMA_STARTED *PHW_DMA_STARTED;

typedef
ULONG
NTAPI
HW_FIND_ADAPTER (
    _In_ PVOID DeviceExtension,
    _In_ PVOID HwContext,
    _In_ PVOID BusInformation,
    _In_z_ PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _In_ PBOOLEAN Reserved3
    );

typedef HW_FIND_ADAPTER *PHW_FIND_ADAPTER;

typedef
BOOLEAN
NTAPI
HW_RESET_BUS (
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId
    );

typedef HW_RESET_BUS *PHW_RESET_BUS;

typedef
BOOLEAN
NTAPI
HW_ADAPTER_STATE (
    _In_ PVOID DeviceExtension,
    _In_ PVOID Context,
    _In_ BOOLEAN SaveState
    );

typedef HW_ADAPTER_STATE *PHW_ADAPTER_STATE;

typedef
SCSI_ADAPTER_CONTROL_STATUS
NTAPI
HW_ADAPTER_CONTROL (
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters
    );

typedef HW_ADAPTER_CONTROL *PHW_ADAPTER_CONTROL;

typedef
BOOLEAN
NTAPI
HW_PASSIVE_INITIALIZE_ROUTINE (
    _In_ PVOID DeviceExtension
    );

typedef HW_PASSIVE_INITIALIZE_ROUTINE *PHW_PASSIVE_INITIALIZE_ROUTINE;

typedef
VOID
NTAPI
HW_DPC_ROUTINE(
    _In_ PSTOR_DPC Dpc,
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
    );

typedef HW_DPC_ROUTINE *PHW_DPC_ROUTINE;

typedef
VOID
NTAPI
HW_WORKITEM (
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PVOID Context,
    _In_ PVOID Worker
    );

typedef HW_WORKITEM *PHW_WORKITEM;

typedef
VOID
NTAPI
HW_STATE_CHANGE (
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PVOID Context,
    _In_ SHORT AddressType,
    _In_ PVOID Address,
    _In_ ULONG Status
    );

typedef HW_STATE_CHANGE *PHW_STATE_CHANGE;

typedef
VOID
NTAPI
HW_TRACING_ENABLED (
    _In_ PVOID HwDeviceExtension,
    _In_ BOOLEAN Enabled
    );

typedef HW_TRACING_ENABLED *PHW_TRACING_ENABLED;

typedef
SCSI_UNIT_CONTROL_STATUS
NTAPI
HW_UNIT_CONTROL (
    _In_ PVOID DeviceExtension,
    _In_ SCSI_UNIT_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters
    );

typedef HW_UNIT_CONTROL *PHW_UNIT_CONTROL;

typedef
BOOLEAN
(NTAPI *PStorPortGetMessageInterruptInformation)(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG MessageId,
    _Out_ PMESSAGE_INTERRUPT_INFORMATION InterruptInfo
    );

typedef
VOID
(NTAPI *PStorPortPutScatterGatherList)(
    _In_ PVOID HwDeviceExtension,
    _In_ PSTOR_SCATTER_GATHER_LIST ScatterGatherList,
    _In_ BOOLEAN  WriteToDevice
    );

typedef
VOID
NTAPI
POST_SCATTER_GATHER_EXECUTE (
    _In_ PVOID  *DeviceObject,
    _In_ PVOID  *Irp,
    _In_ PSTOR_SCATTER_GATHER_LIST  ScatterGather,
    _In_ PVOID  Context
    );

typedef POST_SCATTER_GATHER_EXECUTE *PPOST_SCATTER_GATHER_EXECUTE;

typedef
GETSGSTATUS
(NTAPI *PStorPortBuildScatterGatherList)(
    _In_ PVOID  HwDeviceExtension,
    _In_ PVOID  Mdl,
    _In_reads_bytes_(Length) PVOID  CurrentVa,
    _In_ ULONG  Length,
    _In_ PPOST_SCATTER_GATHER_EXECUTE ExecutionRoutine,
    _In_ PVOID  Context,
    _In_ BOOLEAN  WriteToDevice,
    _Inout_updates_bytes_(ScatterGatherBufferLength) PVOID  ScatterGatherBuffer,
    _In_ ULONG  ScatterGatherBufferLength
    );

typedef
VOID
(NTAPI *PStorPortFreePool)(
    _In_ __drv_freesMem(Mem) PVOID PMemory,
    _In_ PVOID HwDeviceExtension,
    _In_opt_ __drv_freesMem(Mem) PVOID PMdl
    );

typedef
__drv_allocatesMem(Mem)
_Post_writable_byte_size_(NumberOfBytes)
PVOID
(NTAPI *PStorPortAllocatePool)(
    _In_ ULONG NumberOfBytes,
    _In_ ULONG Tag,
    _In_ PVOID HwDeviceExtension,
    _Out_ _At_(*PMdl, _When_(return==0, _Post_null_))
    _At_(*PMdl, _When_(return!=0, __drv_aliasesMem __drv_allocatesMem(Mem) _Post_notnull_))
        PVOID *PMdl
    );

typedef
PVOID
(NTAPI *PStorPortGetSystemAddress)(
    _In_ PSCSI_REQUEST_BLOCK Srb
    );

typedef
ULONG
(NTAPI *PStorPortAcquireMSISpinLock)(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG MessageID
    );

typedef
VOID
(NTAPI *PStorPortReleaseMSISpinLock)(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG MessageID,
    _In_ ULONG OldIrql
    );

typedef
VOID
(NTAPI *PStorPortCompleteServiceIrp)(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID  Irp
    );

typedef
PVOID
(NTAPI *PStorPortGetOriginalMdl)(
    _In_ PSCSI_REQUEST_BLOCK Srb
    );

typedef struct _STORPORT_EXTENDED_FUNCTIONS {
    ULONG Version;
    PStorPortGetMessageInterruptInformation GetMessageInterruptInformation;
    PStorPortPutScatterGatherList           PutScatterGatherList;
    PStorPortBuildScatterGatherList         BuildScatterGatherList;
    PStorPortFreePool                       FreePool;
    PStorPortAllocatePool                   AllocatePool;
    PStorPortGetSystemAddress               GetSystemAddress;
    PStorPortAcquireMSISpinLock             AcquireMSISpinLock;
    PStorPortReleaseMSISpinLock             ReleaseMSISpinLock;
    PStorPortCompleteServiceIrp             CompleteServiceIrp;
    PStorPortGetOriginalMdl                 GetOriginalMdl;
} STORPORT_EXTENDED_FUNCTIONS, *PSTORPORT_EXTENDED_FUNCTIONS;

typedef enum _STORPORT_FUNCTION_CODE {
    ExtFunctionAllocatePool,
    ExtFunctionFreePool,
    ExtFunctionAllocateMdl,
    ExtFunctionFreeMdl,
    ExtFunctionBuildMdlForNonPagedPool,
    ExtFunctionGetSystemAddress,
    ExtFunctionGetOriginalMdl,
    ExtFunctionCompleteServiceIrp,
    ExtFunctionGetDeviceObjects,
    ExtFunctionBuildScatterGatherList,
    ExtFunctionPutScatterGatherList,
    ExtFunctionAcquireMSISpinLock,
    ExtFunctionReleaseMSISpinLock,
    ExtFunctionGetMessageInterruptInformation,
    ExtFunctionInitializePerformanceOptimizations,
    ExtFunctionGetStartIoPerformanceParameters,
    ExtFunctionLogSystemEvent,
    ExtFunctionGetCurrentProcessorNumber,
    ExtFunctionGetActiveGroupCount,
    ExtFunctionGetGroupAffinity,
    ExtFunctionGetActiveNodeCount,
    ExtFunctionGetNodeAffinity,
    ExtFunctionGetHighestNodeNumber,
    ExtFunctionGetLogicalProcessorRelationship,
    ExtFunctionAllocateContiguousMemorySpecifyCacheNode,
    ExtFunctionFreeContiguousMemorySpecifyCache,
    ExtFunctionSetPowerSettingNotificationGuids,
    ExtFunctionInvokeAcpiMethod,
    ExtFunctionGetRequestInfo,
    ExtFunctionInitializeWorker,
    ExtFunctionQueueWorkItem,
    ExtFunctionFreeWorker,
    ExtFunctionInitializeTimer,
    ExtFunctionRequestTimer,
    ExtFunctionFreeTimer,
    ExtFunctionInitializeSListHead,
    ExtFunctionInterlockedFlushSList,
    ExtFunctionInterlockedPopEntrySList,
    ExtFunctionInterlockedPushEntrySList,
    ExtFunctionQueryDepthSList,
    ExtFunctionGetActivityId,
    ExtFunctionGetSystemPortNumber,
    ExtFunctionGetDataInBufferMdl,
    ExtFunctionGetDataInBufferSystemAddress,
    ExtFunctionGetDataInBufferScatterGatherList,
    ExtFunctionMarkDumpMemory,
    ExtFunctionSetUnitAttributes,
    ExtFunctionQueryPerformanceCounter,
    ExtFunctionInitializePoFxPower,
    ExtFunctionPoFxActivateComponent,
    ExtFunctionPoFxIdleComponent,
    ExtFunctionPoFxSetComponentLatency,
    ExtFunctionPoFxSetComponentResidency,
    ExtFunctionPoFxPowerControl,
    ExtFunctionFlushDataBufferMdl,
    ExtFunctionDeviceOperationAllowed,
    ExtFunctionGetProcessorIndexFromNumber,
    ExtFunctionPoFxSetIdleTimeout,
    ExtFunctionMiniportEtwEvent2,
    ExtFunctionMiniportEtwEvent4,
    ExtFunctionMiniportEtwEvent8,
    ExtFunctionCurrentOsInstallationUpgrade,
    ExtFunctionRegistryReadAdapterKey,
    ExtFunctionRegistryWriteAdapterKey,
    ExtFunctionSetAdapterBusType,
    ExtFunctionPoFxRegisterPerfStates,
    ExtFunctionPoFxSetPerfState,
    ExtFunctionGetD3ColdSupport,
    ExtFunctionInitializeRpmb,
    ExtFunctionAllocateHmb,
    ExtFunctionFreeHmb,
    ExtFunctionPropagateIrpExtension,
    ExtFunctionInterlockedInsertHeadList,
    ExtFunctionInterlockedInsertTailList,
    ExtFunctionInterlockedRemoveHeadList,
    ExtFunctionInitializeSpinlock,
    ExtFunctionGetPfns,
    ExtFunctionInitializeCryptoEngine,
    ExtFunctionGetRequestCryptoInfo,
    ExtFunctionMiniportTelemetry,
    ExtFunctionUpdateAdapterMaxIO,
    ExtFunctionDelayExecution,
    ExtFunctionAllocateDmaMemory,
    ExtFunctionFreeDmaMemory,
    ExtFunctionUpdateAdapterMaxIOInfo,
    ExtFunctionMiniportChannelEtwEvent2,
    ExtFunctionMiniportChannelEtwEvent4,
    ExtFunctionMiniportChannelEtwEvent8,
    ExtFunctionInitializeHighResolutionTimer,
    ExtFunctionRequestHighResolutionTimer,
    ExtFunctionCancelHighResolutionTimer,
    ExtFunctionFreeHighResolutionTimer,
    ExtFunctionGetCurrentProcessorIndex,
    ExtFunctionAcquireSpinLock,
    ExtFunctionGetProcessorCount,
    ExtFunctionCancelDpc,
    ExtFunctionMiniportTelemetryEx,
    ExtFunctionQueryConfiguration,
    ExtFunctionLogHardwareError,
    ExtFunctionInitializeEvent,
    ExtFunctionWaitForEvent,
    ExtFunctionSetEvent,
    ExtFunctionDeviceReset,
    ExtFunctionSetFeatureList,
    ExtFunctionCaptureLiveDump,
    ExtFunctionMiniportLogByteStream,
    ExtFunctionQueryDpcWatchdogInformation,
    ExtFunctionQueryTimerMinInterval,
    ExtFunctionMaskPciMsixEntry,
    ExtFunctionGetCurrentIrql,
    ExtFunctionCreateSystemThread,
    ExtFunctionSetPriorityThread,
    ExtFunctionSetSystemGroupAffinityThread,
    ExtFunctionRevertToUserGroupAffinityThread,
    ExtFunctionDeviceResetEx,
    ExtFunctionMiniportReportInternalData,
    ExtFunctionGetMessageInterruptIDFromProcessorIndex,
    ExtFunctionGetNodeAffinity2,
    ExtFunctionEnableRegistryKeyNotification,
    ExtFunctionPoFxRegisterPerfStatesEx,
    ExtFunctionReadRegistryKey,
    ExtFunctionGetDeviceBase2,
    ExtFunctionIsDriverHotSwapEnabled,
    ExtFunctionRegisterDriverProxy,
    ExtFunctionRegisterDriverProxyEndpoints,
    ExtFunctionGetDriverProxyEndpointWrapper,
    ExtFunctionNvmeIceIoStart,
    ExtFunctionNvmeIceIoComplete,
    ExtFunctionNvmeMiniportEvent,
    ExtFunctionNvmeMiniportTelemetry
} STORPORT_FUNCTION_CODE, *PSTORPORT_FUNCTION_CODE;

#define STOR_STATUS_SUCCESS                     (0x00000000L)

#define STOR_STATUS_UNSUCCESSFUL                (0xC1000001L)

#define STOR_STATUS_NOT_IMPLEMENTED             (0xC1000002L)

#define STOR_STATUS_INSUFFICIENT_RESOURCES      (0xC1000003L)

#define STOR_STATUS_BUFFER_TOO_SMALL            (0xC1000004L)

#define STOR_STATUS_ACCESS_DENIED               (0xC1000005L)

#define STOR_STATUS_INVALID_PARAMETER           (0xC1000006L)

#define STOR_STATUS_INVALID_DEVICE_REQUEST      (0xC1000007L)

#define STOR_STATUS_INVALID_IRQL                (0xC1000008L)

#define STOR_STATUS_INVALID_DEVICE_STATE        (0xC1000009L)

#define STOR_STATUS_INVALID_BUFFER_SIZE         (0xC100000AL)

#define STOR_STATUS_UNSUPPORTED_VERSION         (0xC100000BL)

#define STOR_STATUS_BUSY                        (0xC100000CL)

#define STOR_STATUS_THROTTLED_REQUEST           (0xC100000DL)

#define STOR_STATUS_TIMEOUT                     (0xC100000EL)

#define STOR_STATUS_INVALID_DATA                (0xC100000FL)

#define SP_UNEXPECTED_DISCONNECT     0x0002

#define SP_INTERNAL_ADAPTER_ERROR    0x0006

#define SP_IRQ_NOT_RESPONDING        0x0008

#define SP_RETURN_NOT_FOUND     0

#define SP_RETURN_FOUND         1

#define SP_RETURN_ERROR         2

#define SP_RETURN_BAD_CONFIG    3

typedef enum _SCSI_NOTIFICATION_TYPE {
    RequestComplete,
    NextRequest,
    NextLuRequest,
    ResetDetected,
    _obsolete1,
    _obsolete2,
    RequestTimerCall,
    BusChangeDetected,
    WMIEvent,
    WMIReregister,
    LinkUp,
    LinkDown,
    QueryTickCount,
    BufferOverrunDetected,
    TraceNotification,
    GetExtendedFunctionTable,
    EnablePassiveInitialization = 0x1000,
    InitializeDpc,
    IssueDpc,
    AcquireSpinLock,
    ReleaseSpinLock,
    StateChangeDetectedCall,
    IoTargetRequestServiceTime,
    AsyncNotificationDetected,
    RequestDirectComplete,
    InitializeDpcWithContext,
    InitializeThreadedDpc,
    SetTargetProcessorDpc,
    MarkDeviceFailed,
    MarkDeviceFailedEx,
    TerminateSystemThread,
    NvmeofNotification,
} SCSI_NOTIFICATION_TYPE, *PSCSI_NOTIFICATION_TYPE;

#if ((NTDDI_VERSION < NTDDI_WIN8))

typedef struct _HW_INITIALIZATION_DATA {
    ULONG HwInitializationDataSize;
    INTERFACE_TYPE  AdapterInterfaceType;
    PHW_INITIALIZE HwInitialize;
    PHW_STARTIO HwStartIo;
    PHW_INTERRUPT HwInterrupt;
    PHW_FIND_ADAPTER HwFindAdapter;
    PHW_RESET_BUS HwResetBus;
    PHW_DMA_STARTED HwDmaStarted;
    PHW_ADAPTER_STATE HwAdapterState;
    ULONG DeviceExtensionSize;
    ULONG SpecificLuExtensionSize;
    ULONG SrbExtensionSize;
    ULONG NumberOfAccessRanges;
    PVOID Reserved;
    UCHAR MapBuffers;
    BOOLEAN NeedPhysicalAddresses;
    BOOLEAN TaggedQueuing;
    BOOLEAN AutoRequestSense;
    BOOLEAN MultipleRequestPerLu;
    BOOLEAN ReceiveEvent;
    USHORT VendorIdLength;
    PVOID VendorId;
    union {
        USHORT ReservedUshort;
        USHORT PortVersionFlags;
    };
    USHORT DeviceIdLength;
    PVOID DeviceId;
    PHW_ADAPTER_CONTROL HwAdapterControl;
    PHW_BUILDIO HwBuildIo;
} HW_INITIALIZATION_DATA, *PHW_INITIALIZATION_DATA;

#endif

typedef
VOID
NTAPI
HW_FREE_ADAPTER_RESOURCES (
    _In_ PVOID DeviceExtension
    );

typedef HW_FREE_ADAPTER_RESOURCES *PHW_FREE_ADAPTER_RESOURCES;

typedef
VOID
NTAPI
HW_PROCESS_SERVICE_REQUEST (
    _In_ PVOID DeviceExtension,
    _In_ PVOID Irp
    );

typedef HW_PROCESS_SERVICE_REQUEST *PHW_PROCESS_SERVICE_REQUEST;

typedef
VOID
NTAPI
HW_COMPLETE_SERVICE_IRP (
    _In_ PVOID DeviceExtension
    );

typedef HW_COMPLETE_SERVICE_IRP *PHW_COMPLETE_SERVICE_IRP;

typedef
VOID
NTAPI
HW_INITIALIZE_TRACING (
    _In_ PVOID Arg1,
    _In_ PVOID Arg2
    );

typedef HW_INITIALIZE_TRACING *PHW_INITIALIZE_TRACING;

typedef
VOID
NTAPI
HW_CLEANUP_TRACING (
    _In_ PVOID  Arg1
    );

typedef HW_CLEANUP_TRACING *PHW_CLEANUP_TRACING;

#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef struct _HW_INITIALIZATION_DATA {
  ULONG                       HwInitializationDataSize;
  INTERFACE_TYPE              AdapterInterfaceType;
  PHW_INITIALIZE              HwInitialize;
  PHW_STARTIO                 HwStartIo;
  PHW_INTERRUPT               HwInterrupt;
  PVOID                       HwFindAdapter;
  PHW_RESET_BUS               HwResetBus;
  PHW_DMA_STARTED             HwDmaStarted;
  PHW_ADAPTER_STATE           HwAdapterState;
  ULONG                       DeviceExtensionSize;
  ULONG                       SpecificLuExtensionSize;
  ULONG                       SrbExtensionSize;
  ULONG                       NumberOfAccessRanges;
  PVOID                       Reserved;
  UCHAR                       MapBuffers;
  BOOLEAN                     NeedPhysicalAddresses;
  BOOLEAN                     TaggedQueuing;
  BOOLEAN                     AutoRequestSense;
  BOOLEAN                     MultipleRequestPerLu;
  BOOLEAN                     ReceiveEvent;
  USHORT                      VendorIdLength;
  PVOID                       VendorId;
  union {
    USHORT ReservedUshort;
    USHORT PortVersionFlags;
  } ;
  USHORT                      DeviceIdLength;
  PVOID                       DeviceId;
  PHW_ADAPTER_CONTROL         HwAdapterControl;
  PHW_BUILDIO                 HwBuildIo;
  PHW_FREE_ADAPTER_RESOURCES  HwFreeAdapterResources;
  PHW_PROCESS_SERVICE_REQUEST HwProcessServiceRequest;
  PHW_COMPLETE_SERVICE_IRP    HwCompleteServiceIrp;
  PHW_INITIALIZE_TRACING      HwInitializeTracing;
  PHW_CLEANUP_TRACING         HwCleanupTracing;
  PHW_TRACING_ENABLED         HwTracingEnabled;
  ULONG             FeatureSupport;
  ULONG             SrbTypeFlags;
  ULONG             AddressTypeFlags;
  ULONG             Reserved1;
  PHW_UNIT_CONTROL  HwUnitControl;
} HW_INITIALIZATION_DATA, *PHW_INITIALIZATION_DATA;

#define HW_INIT_DATA_SIZE_PHYSICAL  \
    FIELD_OFFSET(HW_INITIALIZATION_DATA, HwFreeAdapterResources)

#define HW_INIT_DATA_SIZE_VIRTUAL \
    FIELD_OFFSET(HW_INITIALIZATION_DATA, HwTracingEnabled)

#define STOR_FEATURE_VIRTUAL_MINIPORT                       0x00000001

#define STOR_FEATURE_ATA_PASS_THROUGH                       0x00000002

#define STOR_FEATURE_FULL_PNP_DEVICE_CAPABILITIES           0x00000004

#define STOR_FEATURE_DUMP_POINTERS                          0x00000008

#define STOR_FEATURE_DEVICE_NAME_NO_SUFFIX                  0x00000010

#define STOR_FEATURE_DUMP_RESUME_CAPABLE                    0x00000020

#define STOR_FEATURE_DEVICE_DESCRIPTOR_FROM_ATA_INFO_VPD    0x00000040

#define STOR_FEATURE_EXTRA_IO_INFORMATION                   0x00000080

#define STOR_FEATURE_ADAPTER_CONTROL_PRE_FINDADAPTER        0x00000100

#define STOR_FEATURE_ADAPTER_NOT_REQUIRE_IO_PORT            0x00000200

#define STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE             0x00000800

#define STOR_FEATURE_DUMP_INFO                              0x00001000

#define STOR_FEATURE_SUPPORTS_NVME_ADAPTER                  0x00004000

#define SRB_TYPE_FLAG_SCSI_REQUEST_BLOCK        0x1

#define SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK     0x2

#define ADDRESS_TYPE_FLAG_BTL8                  0x1

#endif

#define DUMP_MINIPORT_VERSION_1         0x0100

#define DUMP_MINIPORT_NAME_LENGTH       15

typedef struct _MINIPORT_DUMP_POINTERS {
    USHORT Version;
    USHORT Size;
    WCHAR DriverName[DUMP_MINIPORT_NAME_LENGTH];
    struct _ADAPTER_OBJECT *AdapterObject;
    PVOID MappedRegisterBase;
    ULONG CommonBufferSize;
    PVOID MiniportPrivateDumpData;
    ULONG SystemIoBusNumber;
    INTERFACE_TYPE AdapterInterfaceType;
    ULONG MaximumTransferLength;
    ULONG NumberOfPhysicalBreaks;
    ULONG AlignmentMask;
    ULONG NumberOfAccessRanges;
    ACCESS_RANGE (*AccessRanges)[];
    UCHAR NumberOfBuses;
    BOOLEAN  Master;
    BOOLEAN MapBuffers;
    UCHAR MaximumNumberOfTargets;
} MINIPORT_DUMP_POINTERS, *PMINIPORT_DUMP_POINTERS;

typedef enum _STOR_EVENT_ASSOCIATION_ENUM {
    StorEventAdapterAssociation = 0,
    StorEventLunAssociation,
    StorEventTargetAssociation,
    StorEventInvalidAssociation
} STOR_EVENT_ASSOCIATION_ENUM;

typedef struct _STOR_LOG_EVENT_DETAILS {
    ULONG                       InterfaceRevision;
    ULONG                       Size;
    ULONG                       Flags;
    STOR_EVENT_ASSOCIATION_ENUM EventAssociation;
    ULONG                       PathId;
    ULONG                       TargetId;
    ULONG                       LunId;
    BOOLEAN                     StorportSpecificErrorCode;
    ULONG                       ErrorCode;
    ULONG                       UniqueId;
    ULONG                       DumpDataSize;
    PVOID                       DumpData;
    ULONG                       StringCount;
    PWSTR                     * StringList;
} STOR_LOG_EVENT_DETAILS, *PSTOR_LOG_EVENT_DETAILS;

typedef struct _STOR_UNIT_ATTRIBUTES {
    ULONG   DeviceAttentionSupported : 1;
    ULONG   AsyncNotificationSupported: 1;
    ULONG   D3ColdNotSupported : 1;
    ULONG   BypassIOSupported: 1;
    ULONG   Reserved : 28;
} STOR_UNIT_ATTRIBUTES, *PSTOR_UNIT_ATTRIBUTES;

typedef
BOOLEAN
NTAPI
STOR_SYNCHRONIZED_ACCESS(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID Context
    );

typedef STOR_SYNCHRONIZED_ACCESS *PSTOR_SYNCHRONIZED_ACCESS;

#if !defined(_NTDDK_)

#define STORPORT_API DECLSPEC_IMPORT

#endif
#if !!defined(_NTDDK_)

#define STORPORT_API

#endif

STORPORT_API
ULONG
NTAPI
StorPortInitialize(
    _In_ PVOID Argument1,
    _In_ PVOID Argument2,
    _In_ struct _HW_INITIALIZATION_DATA *HwInitializationData,
    _In_opt_ PVOID HwContext
    );

STORPORT_API
VOID
NTAPI
StorPortFreeDeviceBase(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID MappedAddress
    );

STORPORT_API
ULONG
NTAPI
StorPortGetBusData(
    _In_ PVOID DeviceExtension,
    _In_ ULONG BusDataType,
    _In_ ULONG SystemIoBusNumber,
    _In_ ULONG SlotNumber,
    _Out_ _When_(Length!=0, _Out_writes_bytes_(Length))
    PVOID Buffer,
    _In_ ULONG Length
    );

STORPORT_API
ULONG
NTAPI
StorPortSetBusDataByOffset(
    _In_ PVOID DeviceExtension,
    _In_ ULONG BusDataType,
    _In_ ULONG SystemIoBusNumber,
    _In_ ULONG SlotNumber,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length
    );

STORPORT_API
PVOID
NTAPI
StorPortGetDeviceBase(
    _In_ PVOID HwDeviceExtension,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG SystemIoBusNumber,
    _In_ STOR_PHYSICAL_ADDRESS IoAddress,
    _In_ ULONG NumberOfBytes,
    _In_ BOOLEAN InIoSpace
    );

STORPORT_API
PVOID
NTAPI
StorPortGetLogicalUnit(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun
    );

STORPORT_API
PSTOR_SCATTER_GATHER_LIST
NTAPI
StorPortGetScatterGatherList(
    _In_ PVOID HwDeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    );

STORPORT_API
STOR_PHYSICAL_ADDRESS
NTAPI
StorPortGetPhysicalAddress(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_ PVOID VirtualAddress,
    _Out_ ULONG *Length
    );

STORPORT_API
PVOID
NTAPI
StorPortGetVirtualAddress(
    _In_ PVOID HwDeviceExtension,
    _In_ STOR_PHYSICAL_ADDRESS PhysicalAddress
    );

STORPORT_API
PVOID
NTAPI
StorPortGetUncachedExtension(
    _In_ PVOID HwDeviceExtension,
    _In_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _In_ ULONG NumberOfBytes
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortPauseDevice(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG Timeout
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortResumeDevice(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortPause(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG Timeout
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortResume(
    _In_ PVOID HwDeviceExtension
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortDeviceBusy(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG RequestsToComplete
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortDeviceReady(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortBusy(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG RequestsToComplete
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortReady(
    _In_ PVOID HwDeviceExtension
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortSetDeviceQueueDepth(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG Depth
    );

STORPORT_API
VOID
StorPortNotification(
    _In_ SCSI_NOTIFICATION_TYPE NotificationType,
    _In_ PVOID HwDeviceExtension,
    ...
    );

STORPORT_API
VOID
NTAPI
StorPortLogError(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG ErrorCode,
    _In_ ULONG UniqueId
    );

STORPORT_API
VOID
NTAPI
StorPortCompleteRequest(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ UCHAR SrbStatus
    );

STORPORT_API
VOID
NTAPI
StorPortStallExecution(
    _In_ ULONG Delay
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortSynchronizeAccess(
    _In_ PVOID HwDeviceExtension,
    _In_ PSTOR_SYNCHRONIZED_ACCESS SynchronizedAccessRoutine,
    _In_opt_ PVOID Context
    );

#if (( ((NTDDI_VERSION >= NTDDI_WINBLUE) && defined(_M_AMD64) && !defined(DBG) && !defined(DISABLE_INLINE_REGISTER_ACCESS)) || ((NTDDI_VERSION < NTDDI_WINBLUE) && defined(_M_AMD64)) ))

#define StorPortReadPortUchar(h, p) READ_PORT_UCHAR(p)

#define StorPortReadPortUshort(h, p) READ_PORT_USHORT(p)

#define StorPortReadPortUlong(h, p) READ_PORT_ULONG(p)

#define StorPortReadPortBufferUchar(h, p, b, c) READ_PORT_BUFFER_UCHAR(p, b, c)

#define StorPortReadPortBufferUshort(h, p, b, c) READ_PORT_BUFFER_USHORT(p, b, c)

#define StorPortReadPortBufferUlong(h, p, b, c) READ_PORT_BUFFER_ULONG(p, b, c)

#define StorPortReadRegisterUchar(h, r) READ_REGISTER_UCHAR(r)

#define StorPortReadRegisterUshort(h, r) READ_REGISTER_USHORT(r)

#define StorPortReadRegisterUlong(h, r) READ_REGISTER_ULONG(r)

#define StorPortReadRegisterBufferUchar(h, r, b, c) READ_REGISTER_BUFFER_UCHAR(r, b, c)

#define StorPortReadRegisterBufferUshort(h, r, b, c) READ_REGISTER_BUFFER_USHORT(r, b, c)

#define StorPortReadRegisterBufferUlong(h, r, b, c) READ_REGISTER_BUFFER_ULONG(r, b, c)

#define StorPortWritePortUchar(h, p, v) WRITE_PORT_UCHAR(p, v)

#define StorPortWritePortUshort(h, p, v) WRITE_PORT_USHORT(p, v)

#define StorPortWritePortUlong(h, p, v) WRITE_PORT_ULONG(p, v)

#define StorPortWritePortBufferUchar(h, p, b, c) WRITE_PORT_BUFFER_UCHAR(p, b, c)

#define StorPortWritePortBufferUshort(h, p, b, c) WRITE_PORT_BUFFER_USHORT(p, b, c)

#define StorPortWritePortBufferUlong(h, p, b, c) WRITE_PORT_BUFFER_ULONG(p, b, c)

#define StorPortWriteRegisterUchar(h, r, v) WRITE_REGISTER_UCHAR(r, v)

#define StorPortWriteRegisterUshort(h, r, v) WRITE_REGISTER_USHORT(r, v)

#define StorPortWriteRegisterUlong(h, r, v) WRITE_REGISTER_ULONG(r, v)

#define StorPortWriteRegisterBufferUchar(h, r, b, c) WRITE_REGISTER_BUFFER_UCHAR(r, b, c)

#define StorPortWriteRegisterBufferUshort(h, r, b, c) WRITE_REGISTER_BUFFER_USHORT(r, b, c)

#define StorPortWriteRegisterBufferUlong(h, r, b, c) WRITE_REGISTER_BUFFER_ULONG(r, b, c)

#define StorPortMoveMemory memmove

#endif
#if !(( ((NTDDI_VERSION >= NTDDI_WINBLUE) && defined(_M_AMD64) && !defined(DBG) && !defined(DISABLE_INLINE_REGISTER_ACCESS)) || ((NTDDI_VERSION < NTDDI_WINBLUE) && defined(_M_AMD64)) ))

STORPORT_API
UCHAR
NTAPI
StorPortReadPortUchar(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR Port
    );

STORPORT_API
USHORT
NTAPI
StorPortReadPortUshort(
    _In_ PVOID HwDeviceExtension,
    _In_ PUSHORT Port
    );

STORPORT_API
ULONG
NTAPI
StorPortReadPortUlong(
    _In_ PVOID HwDeviceExtension,
    _In_ PULONG Port
    );

STORPORT_API
VOID
NTAPI
StorPortReadPortBufferUchar(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR Port,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG  Count
    );

STORPORT_API
VOID
NTAPI
StorPortReadPortBufferUshort(
    _In_ PVOID HwDeviceExtension,
    _In_ PUSHORT Port,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count
    );

STORPORT_API
VOID
NTAPI
StorPortReadPortBufferUlong(
    _In_ PVOID HwDeviceExtension,
    _In_ PULONG Port,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count
    );

STORPORT_API
UCHAR
NTAPI
StorPortReadRegisterUchar(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR Register
    );

STORPORT_API
USHORT
NTAPI
StorPortReadRegisterUshort(
    _In_ PVOID HwDeviceExtension,
    _In_ PUSHORT Register
    );

STORPORT_API
ULONG
NTAPI
StorPortReadRegisterUlong(
    _In_ PVOID HwDeviceExtension,
    _In_ PULONG Register
    );

STORPORT_API
VOID
NTAPI
StorPortReadRegisterBufferUchar(
    _In_ PVOID HwDeviceExtension,
    _In_reads_(Count) PUCHAR Register,
    _Out_writes_all_(Count) PUCHAR Buffer,
    _In_ ULONG Count
    );

STORPORT_API
VOID
NTAPI
StorPortReadRegisterBufferUshort(
    _In_ PVOID HwDeviceExtension,
    _In_reads_(Count) PUSHORT Register,
    _Out_writes_all_(Count) PUSHORT Buffer,
    _In_ ULONG Count
    );

STORPORT_API
VOID
NTAPI
StorPortReadRegisterBufferUlong(
    _In_ PVOID HwDeviceExtension,
    _In_reads_(Count) PULONG Register,
    _Out_writes_all_(Count) PULONG Buffer,
    _In_ ULONG Count
    );

STORPORT_API
VOID
NTAPI
StorPortWritePortUchar(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR Port,
    _In_ UCHAR Value
    );

STORPORT_API
VOID
NTAPI
StorPortWritePortUshort(
    _In_ PVOID HwDeviceExtension,
    _In_ PUSHORT Port,
    _In_ USHORT Value
    );

STORPORT_API
VOID
NTAPI
StorPortWritePortUlong(
    _In_ PVOID HwDeviceExtension,
    _In_ PULONG Port,
    _In_ ULONG Value
    );

STORPORT_API
VOID
NTAPI
StorPortWritePortBufferUchar(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR Port,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG  Count
    );

STORPORT_API
VOID
NTAPI
StorPortWritePortBufferUshort(
    _In_ PVOID HwDeviceExtension,
    _In_ PUSHORT Port,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count
    );

STORPORT_API
VOID
NTAPI
StorPortWritePortBufferUlong(
    _In_ PVOID HwDeviceExtension,
    _In_ PULONG Port,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count
    );

STORPORT_API
VOID
NTAPI
StorPortWriteRegisterUchar(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR Register,
    _In_ UCHAR Value
    );

STORPORT_API
VOID
NTAPI
StorPortWriteRegisterUshort(
    _In_ PVOID HwDeviceExtension,
    _In_ PUSHORT Register,
    _In_ USHORT Value
    );

STORPORT_API
VOID
NTAPI
StorPortWriteRegisterUlong(
    _In_ PVOID HwDeviceExtension,
    _In_ PULONG Register,
    _In_ ULONG Value
    );

STORPORT_API
VOID
NTAPI
StorPortWriteRegisterBufferUchar(
    _In_ PVOID HwDeviceExtension,
    _Out_writes_(Count) PUCHAR Register,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG  Count
    );

STORPORT_API
VOID
NTAPI
StorPortWriteRegisterBufferUshort(
    _In_ PVOID HwDeviceExtension,
    _Out_writes_(Count) PUSHORT Register,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count
    );

STORPORT_API
VOID
NTAPI
StorPortWriteRegisterBufferUlong(
    _In_ PVOID HwDeviceExtension,
    _Out_writes_(Count) PULONG Register,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count
    );

STORPORT_API
VOID
NTAPI
StorPortMoveMemory(
    _Out_writes_bytes_(Length) PVOID WriteBuffer,
    _In_reads_bytes_(Length) PVOID ReadBuffer,
    _In_ ULONG Length
    );

#endif
#if ((NTDDI_VERSION >= NTDDI_WS03SP1))

#define StorPortCopyMemory(Destination,Source,Length) memcpy((Destination),(Source),(Length))

#endif

STORPORT_API
STOR_PHYSICAL_ADDRESS
NTAPI
StorPortConvertUlongToPhysicalAddress(
    _In_ ULONG_PTR UlongAddress
    );

STORPORT_API
ULONG
NTAPI
StorPortConvertPhysicalAddressToUlong(
    _In_ STOR_PHYSICAL_ADDRESS Address
    );

STORPORT_API
VOID
NTAPI
StorPortQuerySystemTime(
    _Out_ PLARGE_INTEGER CurrentTime
    );

#define StorPortConvertPhysicalAddressToUlong(Address) ((Address).LowPart)

#define MINIPORT_REG_BINARY     3

__drv_allocatesMem(Mem)
_Ret_maybenull_
_Post_writable_byte_size_(*Length)
STORPORT_API
PUCHAR
NTAPI
StorPortAllocateRegistryBuffer(
    _In_ PVOID HwDeviceExtension,
    _In_ PULONG Length
    );

STORPORT_API
VOID
NTAPI
StorPortFreeRegistryBuffer(
    _In_ PVOID HwDeviceExtension,
    _In_ __drv_freesMem(Mem) PUCHAR Buffer
    );

BOOLEAN
NTAPI
StorPortRegistryRead(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR ValueName,
    _In_ ULONG Global,
    _In_ ULONG Type,
    _Out_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength
    );

STORPORT_API
BOOLEAN
NTAPI
StorPortRegistryWrite(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR ValueName,
    _In_ ULONG Global,
    _In_ ULONG Type,
    _In_reads_bytes_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength
    );

__drv_preferredFunction("(see documentation)", "Obsolete")
STORPORT_API
BOOLEAN
NTAPI
StorPortValidateRange(
    _In_ PVOID HwDeviceExtension,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG SystemIoBusNumber,
    _In_ STOR_PHYSICAL_ADDRESS IoAddress,
    _In_ ULONG NumberOfBytes,
    _In_ BOOLEAN InIoSpace
    );

STORPORT_API
VOID
StorPortDebugPrint(
    _In_ ULONG DebugPrintLevel,
    _In_ PSTR  DebugMessage,
    ...
    );

BOOLEAN
FORCEINLINE
StorPortEnablePassiveInitialization(
    _In_ PVOID DeviceExtension,
    _In_ PHW_PASSIVE_INITIALIZE_ROUTINE HwPassiveInitializeRoutine
    )
{
    LONG Succ;
    Succ = FALSE;
    StorPortNotification (EnablePassiveInitialization,
                          DeviceExtension,
                          HwPassiveInitializeRoutine,
                          &Succ);
    return (BOOLEAN)Succ;
}

VOID
FORCEINLINE
StorPortInitializeDpc(
    _In_ PVOID DeviceExtension,
    _Out_ PSTOR_DPC Dpc,
    _In_ PHW_DPC_ROUTINE HwDpcRoutine
    )
{
    StorPortNotification (InitializeDpc,
                          DeviceExtension,
                          Dpc,
                          HwDpcRoutine);
}

BOOLEAN
FORCEINLINE
StorPortIssueDpc(
    _In_ PVOID DeviceExtension,
    _In_ PSTOR_DPC Dpc,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
    )
{
    LONG Succ;
    Succ = FALSE;
    StorPortNotification (IssueDpc,
                          DeviceExtension,
                          Dpc,
                          SystemArgument1,
                          SystemArgument2,
                          &Succ);
    return (BOOLEAN)Succ;
}

_Acquires_nonreentrant_lock_(*LockHandle)
VOID
FORCEINLINE
#pragma warning(suppress: 26166)
StorPortAcquireSpinLock(
    _In_ PVOID DeviceExtension,
    _In_ STOR_SPINLOCK SpinLock,
    _In_opt_ PVOID LockContext,
    _Inout_ PSTOR_LOCK_HANDLE LockHandle
    )
#pragma warning (suppress: 28104 26166)
{
    StorPortNotification (AcquireSpinLock,
                          DeviceExtension,
                          SpinLock,
                          LockContext,
                          LockHandle);
}

_Releases_nonreentrant_lock_(*LockHandle)
VOID
FORCEINLINE
#pragma warning(suppress: 26165)
StorPortReleaseSpinLock(
    _In_ PVOID DeviceExtension,
    _Inout_ PSTOR_LOCK_HANDLE LockHandle
    )
#pragma warning (suppress: 26165)
{
    StorPortNotification (ReleaseSpinLock,
                          DeviceExtension,
                          LockHandle);
}

STORPORT_API
ULONG
StorPortExtendedFunction(
    _In_ STORPORT_FUNCTION_CODE FunctionCode,
    _In_ PVOID HwDeviceExtension,
    ...
    );

_Success_(return == STOR_STATUS_SUCCESS)
ULONG
FORCEINLINE
#pragma warning(suppress: 6001 6101 6388 28194 28195)
StorPortAllocatePool(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG NumberOfBytes,
    _In_ ULONG Tag,
    _Out_ _At_(*BufferPointer,
        _When_(return!=STOR_STATUS_SUCCESS, _Post_null_)
        _When_(return==STOR_STATUS_SUCCESS, __drv_aliasesMem __drv_allocatesMem(Mem) _Post_notnull_
        _Post_writable_byte_size_(NumberOfBytes)))
    PVOID *BufferPointer
    )
{
    return StorPortExtendedFunction(ExtFunctionAllocatePool,
                                    HwDeviceExtension,
                                    NumberOfBytes,
                                    Tag,
                                    BufferPointer);
}

_Success_(return == STOR_STATUS_SUCCESS)
ULONG
FORCEINLINE
#pragma warning (suppress: 6014)
StorPortFreePool(
    _In_ PVOID HwDeviceExtension,
    _In_ __drv_freesMem(Mem) _Post_invalid_ PVOID BufferPointer
    )
{
    return StorPortExtendedFunction(ExtFunctionFreePool,
                                    HwDeviceExtension,
                                    BufferPointer);
}

_Success_(return == STOR_STATUS_SUCCESS)
ULONG
FORCEINLINE
#pragma warning(suppress: 6001 6101 6388 28194 28195)
StorPortAllocateMdl(
    _In_ PVOID HwDeviceExtension,
    _In_reads_bytes_(NumberOfBytes) PVOID BufferPointer,
    _In_ ULONG NumberOfBytes,
    _Out_ _At_(*Mdl,
        _When_(return!=STOR_STATUS_SUCCESS, _Post_null_)
        _When_(return==STOR_STATUS_SUCCESS, __drv_aliasesMem __drv_allocatesMem(Mem) _Post_notnull_))
    PVOID *Mdl
    )
{
    return StorPortExtendedFunction(ExtFunctionAllocateMdl,
                                    HwDeviceExtension,
                                    BufferPointer,
                                    NumberOfBytes,
                                    Mdl);
}

_Success_(return == STOR_STATUS_SUCCESS)
ULONG
FORCEINLINE
#pragma warning (suppress: 6014)
StorPortFreeMdl(
    _In_ PVOID HwDeviceExtension,
    _In_ __drv_freesMem(Mem) _Post_invalid_ PVOID Mdl
    )
{
    return StorPortExtendedFunction(ExtFunctionFreeMdl,
                                    HwDeviceExtension,
                                    Mdl);
}

ULONG
FORCEINLINE
StorPortBuildMdlForNonPagedPool(
    _In_ PVOID HwDeviceExtension,
    _Inout_ PVOID Mdl
    )
{
    return StorPortExtendedFunction(ExtFunctionBuildMdlForNonPagedPool,
                                    HwDeviceExtension,
                                    Mdl);
}

ULONG
FORCEINLINE
StorPortGetSystemAddress(
    _In_ PVOID HwDeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _Outptr_ PVOID *SystemAddress
    )
{
    return StorPortExtendedFunction(ExtFunctionGetSystemAddress,
                                    HwDeviceExtension,
                                    Srb,
                                    SystemAddress);
}

ULONG
FORCEINLINE
StorPortGetOriginalMdl(
    _In_ PVOID HwDeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _Outptr_ PVOID *Mdl
    )
{
    return StorPortExtendedFunction(ExtFunctionGetOriginalMdl,
                                    HwDeviceExtension,
                                    Srb,
                                    Mdl);
}

ULONG
FORCEINLINE
StorPortCompleteServiceIrp(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID Irp
    )
{
    return StorPortExtendedFunction(ExtFunctionCompleteServiceIrp,
                                    HwDeviceExtension,
                                    Irp);
}

ULONG
FORCEINLINE
#pragma warning(suppress: 6001 6101)
StorPortGetDeviceObjects(
    _In_ PVOID HwDeviceExtension,
    _Outptr_ PVOID *AdapterDeviceObject,
    _Outptr_ PVOID *PhysicalDeviceObject,
    _Outptr_ PVOID *LowerDeviceObject
    )
{
    return StorPortExtendedFunction(ExtFunctionGetDeviceObjects,
                                    HwDeviceExtension,
                                    AdapterDeviceObject,
                                    PhysicalDeviceObject,
                                    LowerDeviceObject);
}

ULONG
FORCEINLINE
StorPortBuildScatterGatherList(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID Mdl,
    _In_reads_bytes_(Length) PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ PPOST_SCATTER_GATHER_EXECUTE ExecutionRoutine,
    _In_ PVOID Context,
    _In_ BOOLEAN WriteToDevice,
    _Inout_updates_bytes_(ScatterGatherBufferLength) PVOID ScatterGatherBuffer,
    _In_ ULONG ScatterGatherBufferLength
    )
{
    return StorPortExtendedFunction(ExtFunctionBuildScatterGatherList,
                                    HwDeviceExtension,
                                    Mdl,
                                    CurrentVa,
                                    Length,
                                    ExecutionRoutine,
                                    Context,
                                    WriteToDevice,
                                    ScatterGatherBuffer,
                                    ScatterGatherBufferLength);
}

ULONG
FORCEINLINE
StorPortPutScatterGatherList(
    _In_ PVOID HwDeviceExtension,
    _In_ PSTOR_SCATTER_GATHER_LIST ScatterGatherList,
    _In_ BOOLEAN WriteToDevice
    )
{
    return StorPortExtendedFunction(ExtFunctionPutScatterGatherList,
                                    HwDeviceExtension,
                                    ScatterGatherList,
                                    WriteToDevice);
}

ULONG
FORCEINLINE
StorPortAcquireMSISpinLock(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG MessageId,
    _In_ PULONG OldIrql
    )
{
    return StorPortExtendedFunction(ExtFunctionAcquireMSISpinLock,
                                    HwDeviceExtension,
                                    MessageId,
                                    OldIrql);
}

ULONG
FORCEINLINE
StorPortReleaseMSISpinLock(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG MessageId,
    _In_ ULONG OldIrql
    )
{
    return StorPortExtendedFunction(ExtFunctionReleaseMSISpinLock,
                                    HwDeviceExtension,
                                    MessageId,
                                    OldIrql);
}

ULONG
FORCEINLINE
StorPortGetMSIInfo(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG MessageId,
    _Out_ PMESSAGE_INTERRUPT_INFORMATION InterruptInfo
    )
{
    return StorPortExtendedFunction(ExtFunctionGetMessageInterruptInformation,
                                    HwDeviceExtension,
                                    MessageId,
                                    InterruptInfo);
}

ULONG
FORCEINLINE
StorPortInitializePerfOpts(
    _In_ PVOID HwDeviceExtension,
    _In_ BOOLEAN Query,
    _Inout_ PPERF_CONFIGURATION_DATA PerfConfigData
    )
{
    return StorPortExtendedFunction(ExtFunctionInitializePerformanceOptimizations,
                                    HwDeviceExtension,
                                    Query,
                                    PerfConfigData);
}

ULONG
FORCEINLINE
StorPortGetStartIoPerfParams(
    _In_ PVOID HwDeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _Inout_ PSTARTIO_PERFORMANCE_PARAMETERS StartIoPerfParams
    )
{
    return StorPortExtendedFunction(ExtFunctionGetStartIoPerformanceParameters,
                                    HwDeviceExtension,
                                    Srb,
                                    StartIoPerfParams);
}

ULONG
FORCEINLINE
StorPortLogSystemEvent(
    _In_ PVOID HwDeviceExtension,
    _Inout_ PSTOR_LOG_EVENT_DETAILS LogDetails,
    _Inout_ PULONG MaximumSize
    )
{
    return StorPortExtendedFunction(ExtFunctionLogSystemEvent,
                                    HwDeviceExtension,
                                    LogDetails,
                                    MaximumSize);
}

ULONG
FORCEINLINE
StorPortGetGroupAffinity (
    _In_ PVOID HwDeviceExtension,
    _In_ USHORT GroupNumber,
    _Out_ PKAFFINITY GroupAffinityMask
    )
{
    return StorPortExtendedFunction(ExtFunctionGetGroupAffinity,
                                    HwDeviceExtension,
                                    GroupNumber,
                                    GroupAffinityMask);
}

ULONG
FORCEINLINE
StorPortSetPowerSettingNotificationGuids (
    _In_                   PVOID  HwDeviceExtension,
    _In_                   ULONG  GuidCount,
    _In_reads_(GuidCount) LPGUID Guid
    )
{
    return StorPortExtendedFunction(ExtFunctionSetPowerSettingNotificationGuids,
                                    HwDeviceExtension,
                                    GuidCount,
                                    Guid);
}

ULONG
FORCEINLINE
StorPortInvokeAcpiMethod (
    _In_      PVOID HwDeviceExtension,
    _In_opt_  PSTOR_ADDRESS Address,
    _In_      ULONG MethodName,
    _In_opt_  PVOID InputBuffer,
    _In_      ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_      ULONG OutputBufferLength,
    _Out_opt_ PULONG BytesReturned
    )
{
    return StorPortExtendedFunction(ExtFunctionInvokeAcpiMethod,
                                    HwDeviceExtension,
                                    Address,
                                    MethodName,
                                    InputBuffer,
                                    InputBufferLength,
                                    OutputBuffer,
                                    OutputBufferLength,
                                    BytesReturned);
}

ULONG
FORCEINLINE
StorPortRegistryReadAdapterKey(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PUCHAR SubKeyName,
    _In_ PUCHAR ValueName,
    _In_ ULONG ValueType,
    _Inout_ PVOID *ValueData,
    _Inout_ PULONG ValueDataLength
    )
{
    return StorPortExtendedFunction(ExtFunctionRegistryReadAdapterKey,
                                    HwDeviceExtension,
                                    SubKeyName,
                                    ValueName,
                                    ValueType,
                                    ValueData,
                                    ValueDataLength);
}

ULONG
FORCEINLINE
StorPortRegistryWriteAdapterKey(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PUCHAR SubKeyName,
    _In_ PUCHAR ValueName,
    _In_ ULONG ValueType,
    _In_ PVOID ValueData,
    _In_ ULONG ValueDataLength
    )
{
    return StorPortExtendedFunction(ExtFunctionRegistryWriteAdapterKey,
                                    HwDeviceExtension,
                                    SubKeyName,
                                    ValueName,
                                    ValueType,
                                    ValueData,
                                    ValueDataLength);
}

ULONG
FORCEINLINE
StorPortMarkDumpMemory(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID Address,
    _In_ ULONG_PTR Length,
    _In_ ULONG Flags
    )
{
    return StorPortExtendedFunction(ExtFunctionMarkDumpMemory,
                                    HwDeviceExtension,
                                    Address,
                                    Length,
                                    Flags);
}

ULONG
FORCEINLINE
StorPortSetUnitAttributes(
    _In_ PVOID HwDeviceExtension,
    _In_ PSTOR_ADDRESS Address,
    _In_ STOR_UNIT_ATTRIBUTES Attributes
    )
{
    return StorPortExtendedFunction(ExtFunctionSetUnitAttributes,
                                    HwDeviceExtension,
                                    Address,
                                    Attributes);
}

ULONG
FORCEINLINE
StorPortQueryPerformanceCounter(
    _In_ PVOID HwDeviceExtension,
    _Out_opt_  PLARGE_INTEGER PerformanceFrequency,
    _Out_ PLARGE_INTEGER PerformanceCounter
    )
{
    return StorPortExtendedFunction(ExtFunctionQueryPerformanceCounter,
                                    HwDeviceExtension,
                                    PerformanceFrequency,
                                    PerformanceCounter);
}

ULONG
FORCEINLINE
StorPortGetRequestInfo (
    _In_ PVOID HwDeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _Out_ PSTOR_REQUEST_INFO RequestInfo
    )
{
    return StorPortExtendedFunction(ExtFunctionGetRequestInfo,
                                    HwDeviceExtension,
                                    Srb,
                                    RequestInfo);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG
FORCEINLINE
StorPortInitializeWorker(
    _In_ PVOID HwDeviceExtension,
    _Out_ PVOID *Worker
    )
{
    return StorPortExtendedFunction(ExtFunctionInitializeWorker,
                                    HwDeviceExtension,
                                    Worker);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG
FORCEINLINE
StorPortQueueWorkItem(
    _In_ PVOID HwDeviceExtension,
    _In_ PHW_WORKITEM WorkItemCallback,
    _In_ PVOID Worker,
    _In_opt_ PVOID Context
    )
{
    return StorPortExtendedFunction(ExtFunctionQueueWorkItem,
                                    HwDeviceExtension,
                                    WorkItemCallback,
                                    Worker,
                                    Context);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG
FORCEINLINE
StorPortFreeWorker(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID Worker
    )
{
    return StorPortExtendedFunction(ExtFunctionFreeWorker,
                                    HwDeviceExtension,
                                    Worker);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG
FORCEINLINE
StorPortInitializeTimer(
    _In_ PVOID HwDeviceExtension,
    _Out_ PVOID *TimerHandle
    )
{
    return StorPortExtendedFunction(ExtFunctionInitializeTimer,
                                    HwDeviceExtension,
                                    TimerHandle);
}

ULONG
FORCEINLINE
StorPortRequestTimer(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID TimerHandle,
    _In_ PHW_TIMER_EX TimerCallback,
    _In_opt_ PVOID CallbackContext,
    _In_ ULONGLONG TimerValue,
    _In_ ULONGLONG TolerableDelay
    )
{
    return StorPortExtendedFunction(ExtFunctionRequestTimer,
                                    HwDeviceExtension,
                                    TimerHandle,
                                    TimerCallback,
                                    CallbackContext,
                                    TimerValue,
                                    TolerableDelay);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG
FORCEINLINE
StorPortFreeTimer(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID TimerHandle
    )
{
    return StorPortExtendedFunction(ExtFunctionFreeTimer,
                                    HwDeviceExtension,
                                    TimerHandle);
}

ULONG
FORCEINLINE
StorPortStateChangeDetected(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG ChangedEntity,
    _In_ PSTOR_ADDRESS Address,
    _In_ ULONG Attributes,
    _In_ PHW_STATE_CHANGE HwStateChange,
    _In_opt_ PVOID HwStateChangeContext
    )
{
    ULONG Status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN8)
    StorPortNotification (StateChangeDetectedCall,
                          HwDeviceExtension,
                          ChangedEntity,
                          Address,
                          Attributes,
                          HwStateChange,
                          HwStateChangeContext,
                          &Status);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(ChangedEntity);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Attributes);
    UNREFERENCED_PARAMETER(HwStateChange);
    UNREFERENCED_PARAMETER(HwStateChangeContext);
#endif
    return Status;
}

ULONG
FORCEINLINE
StorPortAsyncNotificationDetected(
    _In_ PVOID HwDeviceExtension,
    _In_ PSTOR_ADDRESS Address,
    _In_ ULONGLONG Flags
    )
{
    ULONG Status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN8)
    StorPortNotification (AsyncNotificationDetected,
                          HwDeviceExtension,
                          Address,
                          Flags,
                          &Status);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Flags);
#endif
    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Success_(return == STOR_STATUS_SUCCESS)
ULONG
FORCEINLINE
StorPortAllocateHostMemoryBuffer(
    _In_ PVOID HwDeviceExtension,
    _In_ SIZE_T MinimumBytes,
    _In_ SIZE_T PreferredBytes,
    _In_ ULONGLONG UtilizationBytes,
    _In_ ULONG AlignmentBytes,
    _In_ PHYSICAL_ADDRESS LowestAcceptableAddress,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress,
    _In_opt_ PHYSICAL_ADDRESS BoundaryAddressMultiple,
    _Out_writes_to_(*PhysicalAddressRangeCount, *PhysicalAddressRangeCount) PACCESS_RANGE PhysicalAddressRanges,
    _Inout_ PULONG PhysicalAddressRangeCount
    )
{
    return StorPortExtendedFunction(ExtFunctionAllocateHmb,
                                    HwDeviceExtension,
                                    MinimumBytes,
                                    PreferredBytes,
                                    UtilizationBytes,
                                    AlignmentBytes,
                                    LowestAcceptableAddress,
                                    HighestAcceptableAddress,
                                    BoundaryAddressMultiple,
                                    PhysicalAddressRanges,
                                    PhysicalAddressRangeCount);
}

_Success_(return == STOR_STATUS_SUCCESS)
ULONG
FORCEINLINE
StorPortFreeHostMemoryBuffer(
    _In_ PVOID HwDeviceExtension,
    _In_reads_(PhysicalAddressRangeCount) PACCESS_RANGE PhysicalAddressRanges,
    _In_ ULONG PhysicalAddressRangeCount
    )
{
    return StorPortExtendedFunction(ExtFunctionFreeHmb,
                                    HwDeviceExtension,
                                    PhysicalAddressRanges,
                                    PhysicalAddressRangeCount);
}

typedef struct _STOR_POFX_COMPONENT_IDLE_STATE {
    ULONG Version;
    ULONG Size;
    ULONGLONG TransitionLatency;
    ULONGLONG ResidencyRequirement;
    ULONG NominalPower;
} STOR_POFX_COMPONENT_IDLE_STATE, *PSTOR_POFX_COMPONENT_IDLE_STATE;

#define STOR_POFX_COMPONENT_IDLE_STATE_SIZE (sizeof(STOR_POFX_COMPONENT_IDLE_STATE))

#define STOR_POFX_COMPONENT_IDLE_STATE_VERSION_V1 1

#define STOR_POFX_UNKNOWN_POWER 0xFFFFFFFF

typedef struct _STOR_POFX_COMPONENT {
    ULONG Version;
    ULONG Size;
    ULONG FStateCount;
    ULONG DeepestWakeableFState;
    GUID Id;
    _Field_size_full_(FStateCount) STOR_POFX_COMPONENT_IDLE_STATE FStates[ANYSIZE_ARRAY];
} STOR_POFX_COMPONENT, *PSTOR_POFX_COMPONENT;

#define STOR_POFX_COMPONENT_SIZE ((ULONG)FIELD_OFFSET(STOR_POFX_COMPONENT, FStates))

#define STOR_POFX_COMPONENT_VERSION_V1 1

typedef struct _STOR_POFX_COMPONENT_V2 {
    ULONG Version;
    ULONG Size;
    ULONG FStateCount;
    ULONG DeepestWakeableFState;
    GUID Id;
    ULONG DeepestAdapterPowerRequiredFState;
    ULONG DeepestCrashDumpReadyFState;
    _Field_size_full_(FStateCount) STOR_POFX_COMPONENT_IDLE_STATE FStates[ANYSIZE_ARRAY];
} STOR_POFX_COMPONENT_V2, *PSTOR_POFX_COMPONENT_V2;

#define STOR_POFX_COMPONENT_V2_SIZE ((ULONG)FIELD_OFFSET(STOR_POFX_COMPONENT_V2, FStates))

#define STOR_POFX_COMPONENT_VERSION_V2 2

static const GUID STORPORT_POFX_ADAPTER_GUID = { 0xdcaf9c10, 0x895f, 0x481f, { 0xa4, 0x92, 0xd4, 0xce, 0xd2, 0xf5, 0x56, 0x33 } };

static const GUID STORPORT_POFX_LUN_GUID = { 0x585d326b, 0xb3a, 0x4088, { 0x89, 0x39, 0x88, 0xb0, 0xf, 0x69, 0x58, 0xbe } };

typedef struct _STOR_POFX_DEVICE {
    ULONG Version;
    ULONG Size;
    ULONG ComponentCount;
    ULONG Flags;
    _Field_size_full_(ComponentCount) STOR_POFX_COMPONENT Components[ANYSIZE_ARRAY];
} STOR_POFX_DEVICE, *PSTOR_POFX_DEVICE;

typedef struct _STOR_POFX_DEVICE_V2 {
    ULONG Version;
    ULONG Size;
    ULONG ComponentCount;
    ULONG Flags;
    union {
        ULONG UnitMinIdleTimeoutInMS;
        ULONG AdapterIdleTimeoutInMS;
    };
    _Field_size_full_(ComponentCount) STOR_POFX_COMPONENT Components[ANYSIZE_ARRAY];
} STOR_POFX_DEVICE_V2, *PSTOR_POFX_DEVICE_V2;

#define STOR_POFX_DEVICE_V2_SIZE ((ULONG)FIELD_OFFSET(STOR_POFX_DEVICE_V2, Components))

#define STOR_POFX_DEVICE_VERSION_V2 2

typedef struct _STOR_POFX_DEVICE_V3 {
    ULONG Version;
    ULONG Size;
    ULONG ComponentCount;
    ULONG Flags;
    union {
        ULONG UnitMinIdleTimeoutInMS;
        ULONG AdapterIdleTimeoutInMS;
    };
    ULONG MinimumPowerCyclePeriodInMS;
    _Field_size_full_(ComponentCount) STOR_POFX_COMPONENT Components[ANYSIZE_ARRAY];
} STOR_POFX_DEVICE_V3, *PSTOR_POFX_DEVICE_V3;

#define STOR_POFX_DEVICE_V3_SIZE ((ULONG)FIELD_OFFSET(STOR_POFX_DEVICE_V3, Components))

#define STOR_POFX_DEVICE_VERSION_V3 3

#define STOR_POFX_DEVICE_FLAG_ENABLE_D3_COLD            0x04

#define STOR_POFX_DEVICE_FLAG_NO_DUMP_ACTIVE            0x08

#define STOR_POFX_DEVICE_FLAG_IDLE_TIMEOUT              0x10

#define STOR_POFX_DEVICE_FLAG_ADAPTIVE_D3_IDLE_TIMEOUT  0x20

#define STOR_POFX_DEVICE_FLAG_NO_IDLE_DEBOUNCE          0x100

#define STOR_POFX_DEVICE_FLAG_ADAPTER_D3_WAKE           0x800

_IRQL_requires_max_(PASSIVE_LEVEL)
ULONG
FORCEINLINE
StorPortInitializePoFxPower(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS Address,
    _In_ PSTOR_POFX_DEVICE Device,
    _Inout_ PBOOLEAN D3ColdEnabled
)
{
    return StorPortExtendedFunction(ExtFunctionInitializePoFxPower,
                                    HwDeviceExtension,
                                    Address,
                                    Device,
                                    D3ColdEnabled);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG
FORCEINLINE
StorPortPoFxActivateComponent(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS Address,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_ ULONG Component,
    _In_ ULONG Flags
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN8)
    status = StorPortExtendedFunction(ExtFunctionPoFxActivateComponent,
                                      HwDeviceExtension,
                                      Address,
                                      Srb,
                                      Component,
                                      Flags);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Srb);
    UNREFERENCED_PARAMETER(Component);
    UNREFERENCED_PARAMETER(Flags);
#endif
    return status;
}

ULONG
FORCEINLINE
StorPortPoFxIdleComponent(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS Address,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_ ULONG Component,
    _In_ ULONG Flags
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN8)
    status = StorPortExtendedFunction(ExtFunctionPoFxIdleComponent,
                                      HwDeviceExtension,
                                      Address,
                                      Srb,
                                      Component,
                                      Flags);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Srb);
    UNREFERENCED_PARAMETER(Component);
    UNREFERENCED_PARAMETER(Flags);
#endif
    return status;
}

static const GUID STORPORT_DEVICEOPERATION_CACHED_SETTINGS_INIT_GUID = { 0x2b9443ac, 0xf89b, 0x48e8, { 0xb2, 0x92, 0x2c, 0xb6, 0xc9, 0x6e, 0xfd, 0x5a } };

ULONG
FORCEINLINE
StorPortIsDeviceOperationAllowed(
    _In_ PVOID HwDeviceExtension,
    _In_ PSTOR_ADDRESS Address,
    _In_ LPCGUID DeviceOperation,
    _Out_ ULONG *AllowedFlag
)
{
    return StorPortExtendedFunction(ExtFunctionDeviceOperationAllowed,
                                    HwDeviceExtension,
                                    Address,
                                    DeviceOperation,
                                    AllowedFlag);
}

ULONG
FORCEINLINE
StorPortGetD3ColdSupport(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PVOID Address,
    _Out_ PBOOLEAN Supported
)
{
    return StorPortExtendedFunction(ExtFunctionGetD3ColdSupport,
                                    HwDeviceExtension,
                                    Address,
                                    Supported);
}

typedef enum _STORPORT_ETW_LEVEL {
    StorportEtwLevelLogAlways = 0,
    StorportEtwLevelCritical = 1,
    StorportEtwLevelError = 2,
    StorportEtwLevelWarning = 3,
    StorportEtwLevelInformational = 4,
    StorportEtwLevelVerbose = 5,
    StorportEtwLevelMax = StorportEtwLevelVerbose
} STORPORT_ETW_LEVEL, *PSTORPORT_ETW_LEVEL;

#define STORPORT_ETW_EVENT_KEYWORD_IO                    0x0000000000000001

#define STORPORT_ETW_EVENT_KEYWORD_POWER                 0x0000000000000004

#define STORPORT_ETW_EVENT_KEYWORD_COMMAND_TRACE         0x0000000000000010

typedef enum _STORPORT_ETW_EVENT_OPCODE {
    StorportEtwEventOpcodeInfo = 0,
    StorportEtwEventOpcodeStart = 1,
    StorportEtwEventOpcodeStop = 2,
    StorportEtwEventOpcodeDC_Start = 3,
    StorportEtwEventOpcodeDC_Stop = 4,
    StorportEtwEventOpcodeExtension = 5,
    StorportEtwEventOpcodeReply = 6,
    StorportEtwEventOpcodeResume = 7,
    StorportEtwEventOpcodeSuspend = 8,
    StorportEtwEventOpcodeSend = 9,
    StorportEtwEventOpcodeReceive = 240
} STORPORT_ETW_EVENT_OPCODE, *PSTORPORT_ETW_EVENT_OPCODE;

typedef enum _STORPORT_ETW_EVENT_CHANNEL{
    StorportEtwEventDiagnostic = 0,
    StorportEtwEventOperational = 1,
    StorportEtwEventHealth = 2,
    StorportEtwEventIoPerformance = 3
} STORPORT_ETW_EVENT_CHANNEL, *PSTORPORT_ETW_EVENT_CHANNEL;

#if ((NTDDI_VERSION >= NTDDI_WIN10_VB))

#define STORPORT_ETW_MAX_DESCRIPTION_LENGTH    64

#endif
#if !((NTDDI_VERSION >= NTDDI_WIN10_VB))

#define STORPORT_ETW_MAX_DESCRIPTION_LENGTH    32

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN10_VB))

#define STORPORT_ETW_MAX_PARAM_NAME_LENGTH     32

#endif
#if !((NTDDI_VERSION >= NTDDI_WIN10_VB))

#define STORPORT_ETW_MAX_PARAM_NAME_LENGTH     16

#endif

ULONG
FORCEINLINE
StorPortEtwEvent2(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS Address,
    _In_ ULONG EventId,
    _In_reads_or_z_(STORPORT_ETW_MAX_DESCRIPTION_LENGTH) PWSTR EventDescription,
    _In_ ULONGLONG EventKeywords,
    _In_ STORPORT_ETW_LEVEL EventLevel,
    _In_ STORPORT_ETW_EVENT_OPCODE EventOpcode,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter1Name,
    _In_ ULONGLONG Parameter1Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter2Name,
    _In_ ULONGLONG Parameter2Value
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS5)
    status = StorPortExtendedFunction(ExtFunctionMiniportChannelEtwEvent2,
                                        HwDeviceExtension,
                                        Address,
                                        StorportEtwEventDiagnostic,
                                        EventId,
                                        EventDescription,
                                        EventKeywords,
                                        EventLevel,
                                        EventOpcode,
                                        Srb,
                                        Parameter1Name,
                                        Parameter1Value,
                                        Parameter2Name,
                                        Parameter2Value);
#elif (NTDDI_VERSION >= NTDDI_WINBLUE)
    status = StorPortExtendedFunction(ExtFunctionMiniportEtwEvent2,
                                        HwDeviceExtension,
                                        Address,
                                        EventId,
                                        EventDescription,
                                        EventKeywords,
                                        EventLevel,
                                        EventOpcode,
                                        Srb,
                                        Parameter1Name,
                                        Parameter1Value,
                                        Parameter2Name,
                                        Parameter2Value);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(EventId);
    UNREFERENCED_PARAMETER(EventDescription);
    UNREFERENCED_PARAMETER(EventKeywords);
    UNREFERENCED_PARAMETER(EventLevel);
    UNREFERENCED_PARAMETER(EventOpcode);
    UNREFERENCED_PARAMETER(Srb);
    UNREFERENCED_PARAMETER(Parameter1Name);
    UNREFERENCED_PARAMETER(Parameter1Value);
    UNREFERENCED_PARAMETER(Parameter2Name);
    UNREFERENCED_PARAMETER(Parameter2Value);
#endif
    return status;
}

ULONG
FORCEINLINE
StorPortEtwEvent4(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS Address,
    _In_ ULONG EventId,
    _In_reads_or_z_(STORPORT_ETW_MAX_DESCRIPTION_LENGTH) PWSTR EventDescription,
    _In_ ULONGLONG EventKeywords,
    _In_ STORPORT_ETW_LEVEL EventLevel,
    _In_ STORPORT_ETW_EVENT_OPCODE EventOpcode,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter1Name,
    _In_ ULONGLONG Parameter1Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter2Name,
    _In_ ULONGLONG Parameter2Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter3Name,
    _In_ ULONGLONG Parameter3Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter4Name,
    _In_ ULONGLONG Parameter4Value
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS5)
    status = StorPortExtendedFunction(ExtFunctionMiniportChannelEtwEvent4,
                                        HwDeviceExtension,
                                        Address,
                                        StorportEtwEventDiagnostic,
                                        EventId,
                                        EventDescription,
                                        EventKeywords,
                                        EventLevel,
                                        EventOpcode,
                                        Srb,
                                        Parameter1Name,
                                        Parameter1Value,
                                        Parameter2Name,
                                        Parameter2Value,
                                        Parameter3Name,
                                        Parameter3Value,
                                        Parameter4Name,
                                        Parameter4Value);
#elif (NTDDI_VERSION >= NTDDI_WINBLUE)
    status = StorPortExtendedFunction(ExtFunctionMiniportEtwEvent4,
                                        HwDeviceExtension,
                                        Address,
                                        EventId,
                                        EventDescription,
                                        EventKeywords,
                                        EventLevel,
                                        EventOpcode,
                                        Srb,
                                        Parameter1Name,
                                        Parameter1Value,
                                        Parameter2Name,
                                        Parameter2Value,
                                        Parameter3Name,
                                        Parameter3Value,
                                        Parameter4Name,
                                        Parameter4Value);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(EventId);
    UNREFERENCED_PARAMETER(EventDescription);
    UNREFERENCED_PARAMETER(EventKeywords);
    UNREFERENCED_PARAMETER(EventLevel);
    UNREFERENCED_PARAMETER(EventOpcode);
    UNREFERENCED_PARAMETER(Srb);
    UNREFERENCED_PARAMETER(Parameter1Name);
    UNREFERENCED_PARAMETER(Parameter1Value);
    UNREFERENCED_PARAMETER(Parameter2Name);
    UNREFERENCED_PARAMETER(Parameter2Value);
    UNREFERENCED_PARAMETER(Parameter3Name);
    UNREFERENCED_PARAMETER(Parameter3Value);
    UNREFERENCED_PARAMETER(Parameter4Name);
    UNREFERENCED_PARAMETER(Parameter4Value);
#endif
    return status;
}

ULONG
FORCEINLINE
StorPortEtwEvent8(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS Address,
    _In_ ULONG EventId,
    _In_reads_or_z_(STORPORT_ETW_MAX_DESCRIPTION_LENGTH) PWSTR EventDescription,
    _In_ ULONGLONG EventKeywords,
    _In_ STORPORT_ETW_LEVEL EventLevel,
    _In_ STORPORT_ETW_EVENT_OPCODE EventOpcode,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter1Name,
    _In_ ULONGLONG Parameter1Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter2Name,
    _In_ ULONGLONG Parameter2Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter3Name,
    _In_ ULONGLONG Parameter3Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter4Name,
    _In_ ULONGLONG Parameter4Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter5Name,
    _In_ ULONGLONG Parameter5Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter6Name,
    _In_ ULONGLONG Parameter6Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter7Name,
    _In_ ULONGLONG Parameter7Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter8Name,
    _In_ ULONGLONG Parameter8Value
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS5)
    status = StorPortExtendedFunction(ExtFunctionMiniportChannelEtwEvent8,
                                    HwDeviceExtension,
                                    Address,
                                    StorportEtwEventDiagnostic,
                                    EventId,
                                    EventDescription,
                                    EventKeywords,
                                    EventLevel,
                                    EventOpcode,
                                    Srb,
                                    Parameter1Name,
                                    Parameter1Value,
                                    Parameter2Name,
                                    Parameter2Value,
                                    Parameter3Name,
                                    Parameter3Value,
                                    Parameter4Name,
                                    Parameter4Value,
                                    Parameter5Name,
                                    Parameter5Value,
                                    Parameter6Name,
                                    Parameter6Value,
                                    Parameter7Name,
                                    Parameter7Value,
                                    Parameter8Name,
                                    Parameter8Value);
#elif (NTDDI_VERSION >= NTDDI_WINBLUE)
    status = StorPortExtendedFunction(ExtFunctionMiniportEtwEvent8,
                                    HwDeviceExtension,
                                    Address,
                                    EventId,
                                    EventDescription,
                                    EventKeywords,
                                    EventLevel,
                                    EventOpcode,
                                    Srb,
                                    Parameter1Name,
                                    Parameter1Value,
                                    Parameter2Name,
                                    Parameter2Value,
                                    Parameter3Name,
                                    Parameter3Value,
                                    Parameter4Name,
                                    Parameter4Value,
                                    Parameter5Name,
                                    Parameter5Value,
                                    Parameter6Name,
                                    Parameter6Value,
                                    Parameter7Name,
                                    Parameter7Value,
                                    Parameter8Name,
                                    Parameter8Value);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(EventId);
    UNREFERENCED_PARAMETER(EventDescription);
    UNREFERENCED_PARAMETER(EventKeywords);
    UNREFERENCED_PARAMETER(EventLevel);
    UNREFERENCED_PARAMETER(EventOpcode);
    UNREFERENCED_PARAMETER(Srb);
    UNREFERENCED_PARAMETER(Parameter1Name);
    UNREFERENCED_PARAMETER(Parameter1Value);
    UNREFERENCED_PARAMETER(Parameter2Name);
    UNREFERENCED_PARAMETER(Parameter2Value);
    UNREFERENCED_PARAMETER(Parameter3Name);
    UNREFERENCED_PARAMETER(Parameter3Value);
    UNREFERENCED_PARAMETER(Parameter4Name);
    UNREFERENCED_PARAMETER(Parameter4Value);
    UNREFERENCED_PARAMETER(Parameter5Name);
    UNREFERENCED_PARAMETER(Parameter5Value);
    UNREFERENCED_PARAMETER(Parameter6Name);
    UNREFERENCED_PARAMETER(Parameter6Value);
    UNREFERENCED_PARAMETER(Parameter7Name);
    UNREFERENCED_PARAMETER(Parameter7Value);
    UNREFERENCED_PARAMETER(Parameter8Name);
    UNREFERENCED_PARAMETER(Parameter8Value);
#endif
    return status;
}

ULONG
FORCEINLINE
StorPortEtwChannelEvent2(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS Address,
    _In_ STORPORT_ETW_EVENT_CHANNEL EventChannel,
    _In_ ULONG EventId,
    _In_reads_or_z_(STORPORT_ETW_MAX_DESCRIPTION_LENGTH) PWSTR EventDescription,
    _In_ ULONGLONG EventKeywords,
    _In_ STORPORT_ETW_LEVEL EventLevel,
    _In_ STORPORT_ETW_EVENT_OPCODE EventOpcode,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter1Name,
    _In_ ULONGLONG Parameter1Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter2Name,
    _In_ ULONGLONG Parameter2Value
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS5)
    status = StorPortExtendedFunction(ExtFunctionMiniportChannelEtwEvent2,
                                        HwDeviceExtension,
                                        Address,
                                        EventChannel,
                                        EventId,
                                        EventDescription,
                                        EventKeywords,
                                        EventLevel,
                                        EventOpcode,
                                        Srb,
                                        Parameter1Name,
                                        Parameter1Value,
                                        Parameter2Name,
                                        Parameter2Value);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(EventChannel);
    UNREFERENCED_PARAMETER(EventId);
    UNREFERENCED_PARAMETER(EventDescription);
    UNREFERENCED_PARAMETER(EventKeywords);
    UNREFERENCED_PARAMETER(EventLevel);
    UNREFERENCED_PARAMETER(EventOpcode);
    UNREFERENCED_PARAMETER(Srb);
    UNREFERENCED_PARAMETER(Parameter1Name);
    UNREFERENCED_PARAMETER(Parameter1Value);
    UNREFERENCED_PARAMETER(Parameter2Name);
    UNREFERENCED_PARAMETER(Parameter2Value);
#endif
    return status;
}

ULONG
FORCEINLINE
StorPortEtwChannelEvent8(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS Address,
    _In_ STORPORT_ETW_EVENT_CHANNEL EventChannel,
    _In_ ULONG EventId,
    _In_reads_or_z_(STORPORT_ETW_MAX_DESCRIPTION_LENGTH) PWSTR EventDescription,
    _In_ ULONGLONG EventKeywords,
    _In_ STORPORT_ETW_LEVEL EventLevel,
    _In_ STORPORT_ETW_EVENT_OPCODE EventOpcode,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter1Name,
    _In_ ULONGLONG Parameter1Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter2Name,
    _In_ ULONGLONG Parameter2Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter3Name,
    _In_ ULONGLONG Parameter3Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter4Name,
    _In_ ULONGLONG Parameter4Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter5Name,
    _In_ ULONGLONG Parameter5Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter6Name,
    _In_ ULONGLONG Parameter6Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter7Name,
    _In_ ULONGLONG Parameter7Value,
    _In_reads_or_z_opt_(STORPORT_ETW_MAX_PARAM_NAME_LENGTH) PWSTR Parameter8Name,
    _In_ ULONGLONG Parameter8Value
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS5)
    status = StorPortExtendedFunction(ExtFunctionMiniportChannelEtwEvent8,
                                    HwDeviceExtension,
                                    Address,
                                    EventChannel,
                                    EventId,
                                    EventDescription,
                                    EventKeywords,
                                    EventLevel,
                                    EventOpcode,
                                    Srb,
                                    Parameter1Name,
                                    Parameter1Value,
                                    Parameter2Name,
                                    Parameter2Value,
                                    Parameter3Name,
                                    Parameter3Value,
                                    Parameter4Name,
                                    Parameter4Value,
                                    Parameter5Name,
                                    Parameter5Value,
                                    Parameter6Name,
                                    Parameter6Value,
                                    Parameter7Name,
                                    Parameter7Value,
                                    Parameter8Name,
                                    Parameter8Value);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(EventChannel);
    UNREFERENCED_PARAMETER(EventId);
    UNREFERENCED_PARAMETER(EventDescription);
    UNREFERENCED_PARAMETER(EventKeywords);
    UNREFERENCED_PARAMETER(EventLevel);
    UNREFERENCED_PARAMETER(EventOpcode);
    UNREFERENCED_PARAMETER(Srb);
    UNREFERENCED_PARAMETER(Parameter1Name);
    UNREFERENCED_PARAMETER(Parameter1Value);
    UNREFERENCED_PARAMETER(Parameter2Name);
    UNREFERENCED_PARAMETER(Parameter2Value);
    UNREFERENCED_PARAMETER(Parameter3Name);
    UNREFERENCED_PARAMETER(Parameter3Value);
    UNREFERENCED_PARAMETER(Parameter4Name);
    UNREFERENCED_PARAMETER(Parameter4Value);
    UNREFERENCED_PARAMETER(Parameter5Name);
    UNREFERENCED_PARAMETER(Parameter5Value);
    UNREFERENCED_PARAMETER(Parameter6Name);
    UNREFERENCED_PARAMETER(Parameter6Value);
    UNREFERENCED_PARAMETER(Parameter7Name);
    UNREFERENCED_PARAMETER(Parameter7Value);
    UNREFERENCED_PARAMETER(Parameter8Name);
    UNREFERENCED_PARAMETER(Parameter8Value);
#endif
    return status;
}

#define EVENT_BUFFER_MAX_LENGTH     4096

#define EVENT_NAME_MAX_LENGTH       32

#define EVENT_MAX_PARAM_NAME_LEN    32

typedef struct _STORPORT_TELEMETRY_EVENT {
    ULONG   DriverVersion;
    ULONG   EventId;
    UCHAR   EventName[EVENT_NAME_MAX_LENGTH];
    ULONG   EventVersion;
    ULONG   Flags;
    _Field_range_(0, EVENT_BUFFER_MAX_LENGTH)
    ULONG   EventBufferLength;
    _Field_size_bytes_(EventBufferLength)
    PUCHAR  EventBuffer;
    UCHAR   ParameterName0[EVENT_MAX_PARAM_NAME_LEN];
    ULONGLONG ParameterValue0;
    UCHAR   ParameterName1[EVENT_MAX_PARAM_NAME_LEN];
    ULONGLONG ParameterValue1;
    UCHAR   ParameterName2[EVENT_MAX_PARAM_NAME_LEN];
    ULONGLONG ParameterValue2;
    UCHAR   ParameterName3[EVENT_MAX_PARAM_NAME_LEN];
    ULONGLONG ParameterValue3;
    UCHAR   ParameterName4[EVENT_MAX_PARAM_NAME_LEN];
    ULONGLONG ParameterValue4;
    UCHAR   ParameterName5[EVENT_MAX_PARAM_NAME_LEN];
    ULONGLONG ParameterValue5;
    UCHAR   ParameterName6[EVENT_MAX_PARAM_NAME_LEN];
    ULONGLONG ParameterValue6;
    UCHAR   ParameterName7[EVENT_MAX_PARAM_NAME_LEN];
    ULONGLONG ParameterValue7;
} STORPORT_TELEMETRY_EVENT, *PSTORPORT_TELEMETRY_EVENT;

ULONG
FORCEINLINE
StorPortLogTelemetry(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS StorAddress,
    _In_ PSTORPORT_TELEMETRY_EVENT Event
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    status = StorPortExtendedFunction(ExtFunctionMiniportTelemetry,
                                      HwDeviceExtension,
                                      StorAddress,
                                      Event);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(StorAddress);
    UNREFERENCED_PARAMETER(Event);
#endif
    return status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Success_(return == STOR_STATUS_SUCCESS)
ULONG
FORCEINLINE
StorPortAllocateDmaMemory(
    _In_ PVOID HwDeviceExtension,
    _In_ SIZE_T NumberOfBytes,
    _In_ PHYSICAL_ADDRESS LowestAcceptableAddress,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress,
    _In_opt_ PHYSICAL_ADDRESS BoundaryAddressMultiple,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ NODE_REQUIREMENT PreferredNode,
    _Out_ _At_(*BufferPointer,
        _When_(return!=STOR_STATUS_SUCCESS, _Post_null_)
        _When_(return==STOR_STATUS_SUCCESS, _Post_notnull_ _Post_writable_byte_size_(NumberOfBytes)))
         PVOID* BufferPointer,
    _Out_ PPHYSICAL_ADDRESS PhysicalAddress
    )
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS4)
    status = StorPortExtendedFunction(ExtFunctionAllocateDmaMemory,
                                      HwDeviceExtension,
                                      NumberOfBytes,
                                      LowestAcceptableAddress,
                                      HighestAcceptableAddress,
                                      BoundaryAddressMultiple,
                                      CacheType,
                                      PreferredNode,
                                      BufferPointer,
                                      PhysicalAddress);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(LowestAcceptableAddress);
    UNREFERENCED_PARAMETER(HighestAcceptableAddress);
    UNREFERENCED_PARAMETER(BoundaryAddressMultiple);
    UNREFERENCED_PARAMETER(CacheType);
    UNREFERENCED_PARAMETER(PreferredNode);
    UNREFERENCED_PARAMETER(BufferPointer);
    UNREFERENCED_PARAMETER(PhysicalAddress);
#endif
    return status;
}

_Success_(return == STOR_STATUS_SUCCESS)
ULONG
FORCEINLINE
StorPortFreeDmaMemory(
    _In_ PVOID HwDeviceExtension,
    _In_reads_bytes_(NumberOfBytes) _Post_invalid_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_opt_ PHYSICAL_ADDRESS PhysicalAddress
    )
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS4)
    status = StorPortExtendedFunction(ExtFunctionFreeDmaMemory,
                                      HwDeviceExtension,
                                      BaseAddress,
                                      NumberOfBytes,
                                      CacheType,
                                      PhysicalAddress);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(CacheType);
    UNREFERENCED_PARAMETER(PhysicalAddress);
#endif
    return status;
}

#define STORPORT_MAX_ADDITIONAL_DATA_SIZE       1024

#define STORPORT_MAX_CRITICAL_DATA_SIZE         8

ULONG
FORCEINLINE
StorPortMarkDeviceFailedEx(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS StorAddress,
    _In_ ULONG Flags,
    _In_ USHORT FaultCode,
    _In_ PWSTR FaultDescription,
    _In_range_(0, STORPORT_MAX_ADDITIONAL_DATA_SIZE) USHORT AdditionalDataSize,
    _In_reads_bytes_opt_(AdditionalDataSize) PUCHAR AdditionalData,
    _In_range_(0, STORPORT_MAX_CRITICAL_DATA_SIZE) USHORT CriticalDataSize,
    _In_reads_bytes_opt_(CriticalDataSize) PUCHAR CriticalData
    )
{
    ULONG Status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_VB)
    StorPortNotification(MarkDeviceFailedEx,
                         HwDeviceExtension,
                         StorAddress,
                         Flags,
                         FaultCode,
                         FaultDescription,
                         AdditionalDataSize,
                         AdditionalData,
                         CriticalDataSize,
                         CriticalData,
                         &Status);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(StorAddress);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(FaultCode);
    UNREFERENCED_PARAMETER(FaultDescription);
    UNREFERENCED_PARAMETER(AdditionalDataSize);
    UNREFERENCED_PARAMETER(AdditionalData);
    UNREFERENCED_PARAMETER(CriticalDataSize);
    UNREFERENCED_PARAMETER(CriticalData);
#endif
    return Status;
}

VOID
FORCEINLINE
StorPortMarkDeviceFailed(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSTOR_ADDRESS StorAddress,
    _In_ ULONG Flags,
    _In_ PWSTR FailReason
    )
{
#if (NTDDI_VERSION > NTDDI_WIN10_19H1)
    StorPortMarkDeviceFailedEx(HwDeviceExtension,
                               StorAddress,
                               Flags,
                               0xFFFF,
                               FailReason,
                               0,
                               NULL,
                               0,
                               NULL);
#elif (NTDDI_VERSION == NTDDI_WIN10_19H1)
    StorPortNotification(MarkDeviceFailed,
                         HwDeviceExtension,
                         StorAddress,
                         Flags,
                         FailReason);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(StorAddress);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(FailReason);
#endif
}

typedef struct _STOR_DISPATCHER_HEADER {
    union {
        struct {
            UCHAR Type;
            UCHAR Flags;
            UCHAR Size;
            union {
                UCHAR Inserted;
                BOOLEAN DebugActive;
            };
        }Data;
        volatile LONG Lock;
    };
    LONG SignalState;
    STOR_LIST_ENTRY WaitListHead;
} STOR_DISPATCHER_HEADER, *PSTOR_DISPATCHER_HEADER;

typedef struct _STOR_EVENT {
    STOR_DISPATCHER_HEADER Header;
} STOR_EVENT, *PSTOR_EVENT, *PRSTOR_EVENT;

typedef enum _STOR_EVENT_TYPE {
    StorNotificationEvent = 0,
    StorSynchronizationEvent = 1
} STOR_EVENT_TYPE, *PSTOR_EVENT_TYPE;

ULONG
FORCEINLINE
StorPortInitializeEvent(
    _In_ PVOID HwDeviceExtension,
    _In_ PSTOR_EVENT Event,
    _In_ STOR_EVENT_TYPE Type,
    _In_ BOOLEAN State
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_VB)
    status = StorPortExtendedFunction(ExtFunctionInitializeEvent,
                                      HwDeviceExtension,
                                      Event,
                                      Type,
                                      State);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(Type);
    UNREFERENCED_PARAMETER(State);
#endif
    return status;
}

ULONG
FORCEINLINE
StorPortWaitForSingleObject(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID Object,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_VB)
    status = StorPortExtendedFunction(ExtFunctionWaitForEvent,
                                      HwDeviceExtension,
                                      Object,
                                      Alertable,
                                      Timeout);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(Alertable);
    UNREFERENCED_PARAMETER(Timeout);
#endif
    return status;
}

ULONG
FORCEINLINE
StorPortSetEvent(
    _In_ PVOID HwDeviceExtension,
    _In_ PSTOR_EVENT Event
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_VB)
    status = StorPortExtendedFunction(ExtFunctionSetEvent,
                                      HwDeviceExtension,
                                      Event);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Event);
#endif
    return status;
}

typedef struct _STOR_DPC_WATCHDOG_INFORMATION {
    ULONG DpcTimeLimit;
    ULONG DpcTimeCount;
    ULONG DpcWatchdogLimit;
    ULONG DpcWatchdogCount;
    ULONG Reserved;
} STOR_DPC_WATCHDOG_INFORMATION, *PSTOR_DPC_WATCHDOG_INFORMATION;

_IRQL_requires_same_
ULONG
FORCEINLINE
StorPortQueryDpcWatchdogInformation(
    _In_ PVOID HwDeviceExtension,
    _Out_ PSTOR_DPC_WATCHDOG_INFORMATION DpcWatchdogInformation
)
{
    ULONG status = STOR_STATUS_NOT_IMPLEMENTED;
#if (NTDDI_VERSION >= NTDDI_WIN10_MN)
    status = StorPortExtendedFunction(ExtFunctionQueryDpcWatchdogInformation,
                                      HwDeviceExtension,
                                      DpcWatchdogInformation);
#else
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(DpcWatchdogInformation);
#endif
    return status;
}

#ifdef __cplusplus
}
#endif

#endif /* _NTSTORPORT_ */
