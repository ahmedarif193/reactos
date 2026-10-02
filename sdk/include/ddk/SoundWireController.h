/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SoundWire audio class interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _SOUNDWIRECONTROLLER_H_
#define _SOUNDWIRECONTROLLER_H_

#pragma warning(disable:4201)

#define SDCA_AUDIO_ADDRESS_VER_1        (1)
#define SOUNDWIRE_INTERRUPTS_VER_1      (1)
#define SOUNDWIRE_NOTIFICATIONS_VER_1   (1)

#define SOUNDWIRE_CAPABILITIES_VER_2    (2)

#define SOUNDWIRE_CONTROLLER_VER_1      (1)
#define SOUNDWIRE_CONTROLLER_VER_2      (2)
#define SOUNDWIRE_CONTROLLER_VER_3      (3)
#define SOUNDWIRE_CONTROLLER_VER_4      (4)

#define MAX_NUM_DATAPORTS               (15)
#define MAX_NUM_LANES                   (8)

typedef enum _SOUNDWIRE_COMMAND_PRIORITY
{
    SoundWireCommandPriorityInvalid     = 0,
    SoundWireCommandPriorityNormal      = 1,
    SoundWireCommandPriorityHigh        = 2,
    SoundWireCommandPriorityCritical    = 3,
} SOUNDWIRE_COMMAND_PRIORITY;

typedef enum _SOUNDWIRE_DUAL_RANK_ATTRIBUTE
{
    SoundWireDualRankAttributeCvr = 0,
    SoundWireDualRankAttributeNvr = 1,
} SOUNDWIRE_DUAL_RANK_ATTRIBUTE;

typedef struct _SDCA_AUDIO_ADDRESS
{
    ULONG                           Size;
    UINT8                           Version;
    union {
        UINT8                           FunctionId;
        UINT8                           FunctionNumber;
    };
    UINT8                           EntityId;
    UINT8                           ControlSelector;
    UINT8                           ControlNumber;
    SOUNDWIRE_DUAL_RANK_ATTRIBUTE   DualRankAttribute;
} SDCA_AUDIO_ADDRESS, *PSDCA_AUDIO_ADDRESS;

typedef struct _SDCA_AUDIO_CONTROL
{
    SDCA_AUDIO_ADDRESS          Address;
    ULONG                       ValueSize;
    LONGLONG                    Value;
    NTSTATUS                    Status;
} SDCA_AUDIO_CONTROL, *PSDCA_AUDIO_CONTROL;

typedef struct _SDCA_AUDIO_CONTROLS
{
    ULONG                       Size;
    SOUNDWIRE_COMMAND_PRIORITY  Priority;
    ULONG                       ControlCount;
    _Field_size_(ControlCount)
    SDCA_AUDIO_CONTROL          Control[ANYSIZE_ARRAY];
} SDCA_AUDIO_CONTROLS, *PSDCA_AUDIO_CONTROLS;

typedef struct _SDCA_AUDIO_CONTROLS_2
{
    ULONG                       Size;
    ULONG                       CommitGroupHandle;
    SOUNDWIRE_COMMAND_PRIORITY  Priority;
    ULONG                       ControlCount;
    _Field_size_(ControlCount)
    SDCA_AUDIO_CONTROL          Control[ANYSIZE_ARRAY];
} SDCA_AUDIO_CONTROLS_2, *PSDCA_AUDIO_CONTROLS_2;

typedef struct _SOUNDWIRE_MEMORY
{
    ULONG                       Size;
    SOUNDWIRE_COMMAND_PRIORITY  Priority;
    ULONG                       Address;
    ULONG                       BufferSize;
    _Field_size_(BufferSize)
    BYTE                        Buffer[ANYSIZE_ARRAY];
} SOUNDWIRE_MEMORY, *PSOUNDWIRE_MEMORY;

typedef struct _SOUNDWIRE_MEMORY_2
{
    ULONG                       Size;
    ULONG                       CommitGroupHandle;
    SOUNDWIRE_COMMAND_PRIORITY  Priority;
    ULONG                       Address;
    ULONG                       BufferSize;
    _Field_size_(BufferSize)
    BYTE                        Buffer[ANYSIZE_ARRAY];
} SOUNDWIRE_MEMORY_2, *PSOUNDWIRE_MEMORY_2;

typedef struct _SOUNDWIRE_REGISTER
{
    ULONG                       Address;
    BYTE                        Value;
    BYTE                        Reserved1;
    BYTE                        Reserved2;
    BYTE                        Reserved3;
    NTSTATUS                    Status;
} SOUNDWIRE_REGISTER, *PSOUNDWIRE_REGISTER;

typedef struct _SOUNDWIRE_REGISTERS
{
    ULONG                       Size;
    SOUNDWIRE_COMMAND_PRIORITY  Priority;
    ULONG                       RegisterCount;
    _Field_size_(RegisterCount)
    SOUNDWIRE_REGISTER          Register[ANYSIZE_ARRAY];
} SOUNDWIRE_REGISTERS, *PSOUNDWIRE_REGISTERS;

typedef struct _SOUNDWIRE_REGISTERS_2
{
    ULONG                       Size;
    ULONG                       CommitGroupHandle;
    SOUNDWIRE_COMMAND_PRIORITY  Priority;
    ULONG                       RegisterCount;
    _Field_size_(RegisterCount)
    SOUNDWIRE_REGISTER          Register[ANYSIZE_ARRAY];
} SOUNDWIRE_REGISTERS_2, *PSOUNDWIRE_REGISTERS_2;

typedef struct _SOUNDWIRE_INTERRUPTS
{
    ULONG                       Size;
    UINT8                       Version;
    ULONG                       SCPInterrupts;
    UINT8                       DataPortInterrupts[MAX_NUM_DATAPORTS];
} SOUNDWIRE_INTERRUPTS, *PSOUNDWIRE_INTERRUPTS;

typedef NTSTATUS EVT_SOUNDWIRE_INTERRUPT_CALLBACK(
    _In_    PVOID                   Context,
    _In_    PSOUNDWIRE_INTERRUPTS   IntStats,
    _In_    ULONG                   Size
    );

typedef EVT_SOUNDWIRE_INTERRUPT_CALLBACK *PFNSOUNDWIRE_INTERRUPT_CALLBACK;

typedef struct _SOUNDWIRE_INTERRUPT_CALLBACK
{
    ULONG                                   Size;
    PVOID                                   Context;
    PFNSOUNDWIRE_INTERRUPT_CALLBACK         EvtInterruptCallback;
} SOUNDWIRE_INTERRUPT_CALLBACK, *PSOUNDWIRE_INTERRUPT_CALLBACK;

typedef enum _SOUNDWIRE_NOTIFICATION
{
    SoundWireNotificationInvalid = 0x00000000,
    SoundWireNotificationAttach  = 0x00000001,
} SOUNDWIRE_NOTIFICATION;

typedef struct _SOUNDWIRE_NOTIFICATIONS
{
    ULONG                       Size;
    UINT8                       Version;
    ULONG                       Notifications;
} SOUNDWIRE_NOTIFICATIONS, *PSOUNDWIRE_NOTIFICATIONS;

typedef NTSTATUS EVT_SOUNDWIRE_NOTIFICATION_CALLBACK(
    _In_    PVOID                       Context,
    _In_    PSOUNDWIRE_NOTIFICATIONS    Notifications,
    _In_    ULONG                       Size
    );

typedef EVT_SOUNDWIRE_NOTIFICATION_CALLBACK *PFNSOUNDWIRE_NOTIFICATION_CALLBACK;

typedef struct _SOUNDWIRE_NOTIFICATION_CALLBACK
{
    ULONG                                   Size;
    PVOID                                   Context;
    PFNSOUNDWIRE_NOTIFICATION_CALLBACK      EvtNotificationCallback;
} SOUNDWIRE_NOTIFICATION_CALLBACK, *PSOUNDWIRE_NOTIFICATION_CALLBACK;

typedef enum _SOUNDWIRE_PERIPHERAL_FLAGS
{
    SoundWirePeripheralFlagSubSystemIdPresent  = 0x00000001,
    SoundWirePeripheralFlagControllerIdPresent = 0x00000002,
    SoundWirePeripheralFlagLinkIdPresent       = 0x00000004,
    SoundWirePeripheralFlagPeripheralIdPresent = 0x00000008,
} SOUNDWIRE_PERIPHERAL_FLAGS;

typedef struct _SOUNDWIRE_PERIPHERAL_INFORMATION
{
    ULONG                       Size;
    ULONG                       Flags;
    ULONG                       SubSystemId;
    UINT8                       ControllerId;
    UINT8                       LinkId;
    UINT8                       PeripheralId;
} SOUNDWIRE_PERIPHERAL_INFORMATION, *PSOUNDWIRE_PERIPHERAL_INFORMATION;

typedef enum _SOUNDWIRE_DATAPORT_TYPE
{
    SoundWireDataPortTypeInvalid    = 0,
    SoundWireDataPortTypeFull       = 1,
    SoundWireDataPortTypeSimplified = 2,
    SoundWireDataPortTypeReduced    = 3,
} SOUNDWIRE_DATAPORT_TYPE;

typedef enum _SOUNDWIRE_CHANNEL_PREPARE_TYPE
{
    SoundWireChannelPrepareTypeInvalid          = 0,
    SoundWireChannelPrepareTypeCpSm             = 1,
    SoundWireChannelPrepareTypeSimplifiedCpSm   = 2,
} SOUNDWIRE_CHANNEL_PREPARE_TYPE;

typedef enum _SOUNDWIRE_DATAPORT_MODE
{
    SoundWireDataPortModeInvalid            = 0x00000000,
    SoundWireDataPortModeIsochronous        = 0x00000001,
    SoundWireDataPortModeTxControlled       = 0x00000002,
    SoundWireDataPortModeRxControlled       = 0x00000004,
    SoundWireDataPortModeFullAsynchronous   = 0x00000008,
} SOUNDWIRE_DATAPORT_MODE;

typedef enum _SOUNDWIRE_DATAPORT_DIRECTION
{
    SoundWireDataPortDirectionInvalid   = 0,
    SoundWireDataPortDirectionSink      = 1,
    SoundWireDataPortDirectionSource    = 2,
} SOUNDWIRE_DATAPORT_DIRECTION;

typedef struct _SOUNDWIRE_DATAPORT_CONFIGURATION
{
    ULONG                       Size;
    ULONG                       DataPortNumber;
    ULONG                       EndpointId;
    ULONG                       Modes;
    ULONG                       ChannelMask;
} SOUNDWIRE_DATAPORT_CONFIGURATION, *PSOUNDWIRE_DATAPORT_CONFIGURATION;

typedef struct SOUNDWIRE_DATAPORT_CAPABILITIES
{
    ULONG                            Size;
    ULONG                            DataPortNumber;
    ULONG                            EndpointId;
    SOUNDWIRE_DATAPORT_TYPE          DataPortType;
    SOUNDWIRE_CHANNEL_PREPARE_TYPE   ChannelPrepareType;
    SOUNDWIRE_DATAPORT_DIRECTION     Direction;
} SOUNDWIRE_DATAPORT_CAPABILITIES, *PSOUNDWIRE_DATAPORT_CAPABILITIES;

typedef enum _SOUNDWIRE_TRIGGER_PURPOSE
{
    SoundWireTriggerPurposeInvalid      = 0,
    SoundWireTriggerPurposeUltrasound   = 1,
    SoundWireTriggerPurposeSpeech       = 2,
    SoundWireTriggerPurposeVoice        = 3,
} SOUNDWIRE_TRIGGER_PURPOSE;

typedef struct _SOUNDWIRE_TRIGGER_CONFIGURATION
{
    ULONG                           Size;
    ULONG                           DataPortNumber;
    ULONG                           EndpointId;
    SOUNDWIRE_TRIGGER_PURPOSE       Purpose;
} SOUNDWIRE_TRIGGER_CONFIGURATION, *PSOUNDWIRE_TRIGGER_CONFIGURATION;

typedef struct _SOUNDWIRE_DATAPORT_STARTSTOP
{
    ULONG                           Size;
    ULONG                           CommitGroupHandle;
    ULONG                           DataPortNumber;
} SOUNDWIRE_DATAPORT_STARTSTOP, *PSOUNDWIRE_DATAPORT_STARTSTOP;

typedef enum _SOUNDWIRE_PORT15_READ_BEHAVIOR
{
    SoundWirePort15ReadBehaviorInvalid          = 0,
    SoundWirePort15ReadBehaviorCommandIgnored   = 1,
    SoundWirePort15ReadBehaviorCommandOk        = 2,
} SOUNDWIRE_PORT15_READ_BEHAVIOR, *PSOUNDWIRE_PORT15_READ_BEHAVIOR;

#define LANE_MAPPING_MAX_LENGTH 31
typedef struct _SOUNDWIRE_PERIPHERAL_CAPABILITIES
{
    ULONG                           Size;
    ULONG                           Version;
    union
    {
        ULONG                       SwInterfaceRevision;
        struct
        {
            ULONG                   SwInterfaceDisCoMinorVersion            : 16;
            ULONG                   SwInterfaceDisCoMajorVersion            : 16;
        };
    };
    union
    {
        ULONG                       SdcaInterfaceRevision;
        struct
        {
            ULONG                   SdcaInterfaceDraftRevision              : 8;
            ULONG                   SdcaInterfaceMinorVersion               : 8;
            ULONG                   SdcaInterfaceMajorVersion               : 16;
        };
    };
    BOOLEAN                         WakeUpUnavailable;
    BOOLEAN                         TestModeSupported;
    BOOLEAN                         ClockStopMode1Supported;
    BOOLEAN                         SimplifiedClockStopPrepareSMSupported;
    ULONG                           ClockStopPrepareTimeoutInMS;
    ULONG                           PeripheralChannelPrepareTimeoutInMS;
    union
    {
        ULONG                       ClockStopPrepareHardResetBehavior;
        struct
        {
            ULONG                   ClockStopPrepareHardResetKeepSMStatus   : 1;
            ULONG                   Reserved                                : 31;
        };
    };
    BOOLEAN                         HighPHYCapable;
    BOOLEAN                         PagingSupported;
    BOOLEAN                         BankDelaySupported;
    SOUNDWIRE_PORT15_READ_BEHAVIOR  Port15ReadBehavior;
    union
    {
        ULONG                       LaneBusHolder;
        struct
        {
            ULONG                   LaneBusHolderReserved0                  : 1;
            ULONG                   LaneBusHolder1                          : 1;
            ULONG                   LaneBusHolder2                          : 1;
            ULONG                   LaneBusHolder3                          : 1;
            ULONG                   LaneBusHolder4                          : 1;
            ULONG                   LaneBusHolder5                          : 1;
            ULONG                   LaneBusHolder6                          : 1;
            ULONG                   LaneBusHolder7                          : 1;
            ULONG                   LaneBusHolderReservedN                  : 24;
        };
    };

    CHAR                            LaneMapping[MAX_NUM_LANES][LANE_MAPPING_MAX_LENGTH];

    ULONG                           SdcaInterruptRegisterList;
    ULONG                           DataPortSourceList;
    ULONG                           DataPortSinkList;
    BOOLEAN                         DataPort0Supported;
    BOOLEAN                         CommitRegisterSupported;
} SOUNDWIRE_PERIPHERAL_CAPABILITIES, *PSOUNDWIRE_PERIPHERAL_CAPABILITIES;

typedef struct _SOUNDWIRE_BRA_MODE
{
    ULONG           Size;
    ULONG           BRAMode;
    ULONG           BusFrequencyMin;
    ULONG           BusFrequencyMax;
    ULONG           DataPerFrameMax;
    ULONG           TransactionDelayInUS;
    ULONG           BandwidthMax;
    ULONG           BlockAlignment;
    ULONG           BusFrequencyListCount;
    _Field_size_(BusFrequencyListCount)
    ULONG           BusFrequencyList[ANYSIZE_ARRAY];
} SOUNDWIRE_BRA_MODE, *PSOUNDWIRE_BRA_MODE;

typedef struct _SOUNDWIRE_DATAPORT_CAPS_HEADER
{
    ULONG           Size;
    ULONG           Version;
    ULONG           DataPortNumber;
} SOUNDWIRE_DATAPORT_CAPS_HEADER, *PSOUNDWIRE_DATAPORT_CAPS_HEADER;

typedef struct _SOUNDWIRE_DATAPORT0_CAPABILITIES_2
{
    SOUNDWIRE_DATAPORT_CAPS_HEADER  Header;

    ULONGLONG                       WordLengthsSupported;
    BOOLEAN                         BRAFlowControlled;
    BOOLEAN                         BRAImpDefResponseSupported;
    ULONG                           BRARoleSupported;
    BOOLEAN                         SimplifiedChannelPrepareStateMachine;
    BOOLEAN                         ChannelPrepareTimeoutPresent;
    ULONG                           ChannelPrepareTimeoutInMS;
    union
    {
        ULONG                       ImpDefDp0InterruptsSupported;
        struct
        {
            ULONG                   ImpDefDp0Interrupt1Supported    : 1;
            ULONG                   ImpDefDp0Interrupt2Supported    : 1;
            ULONG                   ImpDefDp0Interrupt3Supported    : 1;
            ULONG                   ImpDefDp0InterruptReserved      : 29;
        };
    };
    BOOLEAN                         ImpDefBptSupported;
    ULONG                           LaneListCount;
    UINT8                           LaneList[MAX_NUM_LANES];
    ULONG                           BRAModeCount;
    _Field_size_(BRAModeCount)
    ULONG                           BRAModeByteOffsetFromBeginningOfThisStructure[ANYSIZE_ARRAY];
} SOUNDWIRE_DATAPORT0_CAPABILITIES_2, *PSOUNDWIRE_DATAPORT0_CAPABILITIES_2;

typedef struct _SOUNDWIRE_DATAPORTN_CAPABILITIES_2
{
    SOUNDWIRE_DATAPORT_CAPS_HEADER  Header;

    SOUNDWIRE_DATAPORT_DIRECTION    Direction;
    ULONGLONG                       WordLengthsSupported;
    SOUNDWIRE_DATAPORT_TYPE         DataPortType;
    BOOLEAN                         MaxGroupingSupportedPresent;
    ULONG                           MaxGroupingSupported;
    BOOLEAN                         SimplifiedChannelPrepareStateMachine;
    BOOLEAN                         ChannelPrepareTimeoutPresent;
    ULONG                           ChannelPrepareTimeoutInMS;
    union
    {
        ULONG                       ImpDefDpNInterruptsSupported;
        struct
        {
            ULONG                   ImpDefDpNInterrupt1Supported    : 1;
            ULONG                   ImpDefDpNInterrupt2Supported    : 1;
            ULONG                   ImpDefDpNInterrupt3Supported    : 1;
            ULONG                   ImpDefDpNInterruptReserved      : 29;
        };
    };
    ULONG                           ChannelNumberList;
    BOOLEAN                         ModesSupportedPresent;
    union
    {
        ULONG                       ModesSupported;
        struct
        {
            ULONG                   ModesSupportedIsochronous       : 1;
            ULONG                   ModesSupportedTx                : 1;
            ULONG                   ModesSupportedRx                : 1;
            ULONG                   ModesSupportedAsync             : 1;
            ULONG                   ModesSupportedReserved          : 28;
        };
    };
    ULONG                           MaxAsyncBuffer;
    BOOLEAN                         BlockPackingModePresent;
    BOOLEAN                         BlockPackingMode;
    BOOLEAN                         PortEncodingTypePresent;
    union
    {
        ULONG                       PortEncodingType;
        struct
        {
            ULONG                   PortEncodingTypeTwosComplement  : 1;
            ULONG                   PortEncodingTypeSignMagnitude   : 1;
            ULONG                   PortEncodingTypeIeee32          : 1;
            ULONG                   PortEncodingTypeReserved        : 29;
        };
    };
    ULONG                           LaneListCount;
    UINT8                           LaneList[MAX_NUM_LANES];
    ULONG                           ChannelCombinationListCount;
    _Field_size_(ChannelCombinationListCount)
    ULONG                           ChannelCombinationList[ANYSIZE_ARRAY];
} SOUNDWIRE_DATAPORTN_CAPABILITIES_2, *PSOUNDWIRE_DATAPORTN_CAPABILITIES_2;

#define SOUNDWIRE_IOCTL(_index_) \
    CTL_CODE (FILE_DEVICE_SOUNDWIRE, _index_, METHOD_NEITHER, FILE_ANY_ACCESS)

#define IOCTL_SOUNDWIRE_GET_CONTROLLER_VERSION                               SOUNDWIRE_IOCTL (0)

#define IOCTL_SOUNDWIRE_GET_PERIPHERAL_INFORMATION                           SOUNDWIRE_IOCTL (1)

#define IOCTL_SOUNDWIRE_SET_DATAPORT_CAPABILITIES                            SOUNDWIRE_IOCTL (2)

#define IOCTL_SDCA_WRITE_AUDIO_CONTROLS                                      SOUNDWIRE_IOCTL (3)

#define IOCTL_SDCA_READ_AUDIO_CONTROLS                                       SOUNDWIRE_IOCTL (4)

#define IOCTL_SOUNDWIRE_WRITE_REGISTERS                                      SOUNDWIRE_IOCTL (5)

#define IOCTL_SOUNDWIRE_READ_REGISTERS                                       SOUNDWIRE_IOCTL (6)

#define IOCTL_SOUNDWIRE_WRITE_MEMORY                                         SOUNDWIRE_IOCTL (7)

#define IOCTL_SOUNDWIRE_READ_MEMORY                                          SOUNDWIRE_IOCTL (8)

#define IOCTL_SOUNDWIRE_MASK_NOTIFICATIONS                                   SOUNDWIRE_IOCTL (9)

#define IOCTL_SOUNDWIRE_REGISTER_NOTIFICATION_CALLBACK                       SOUNDWIRE_IOCTL (10)

#define IOCTL_SOUNDWIRE_UNREGISTER_NOTIFICATION_CALLBACK                     SOUNDWIRE_IOCTL (11)

#define IOCTL_SOUNDWIRE_MASK_INTERRUPTS                                      SOUNDWIRE_IOCTL (12)

#define IOCTL_SOUNDWIRE_REGISTER_INTERRUPT_CALLBACK                          SOUNDWIRE_IOCTL (13)

#define IOCTL_SOUNDWIRE_UNREGISTER_INTERRUPT_CALLBACK                        SOUNDWIRE_IOCTL (14)

#define IOCTL_SOUNDWIRE_PREPARE_DATAPORT                                     SOUNDWIRE_IOCTL (15)

#define IOCTL_SOUNDWIRE_DEPREPARE_DATAPORT                                   SOUNDWIRE_IOCTL (16)

#define IOCTL_SOUNDWIRE_START_DATAPORT                                       SOUNDWIRE_IOCTL (17)

#define IOCTL_SOUNDWIRE_STOP_DATAPORT                                        SOUNDWIRE_IOCTL (18)

#define IOCTL_SDCA_WRITE_AUDIO_CONTROLS_2                                    SOUNDWIRE_IOCTL (19)

#define IOCTL_SOUNDWIRE_WRITE_REGISTERS_2                                    SOUNDWIRE_IOCTL (20)

#define IOCTL_SOUNDWIRE_WRITE_MEMORY_2                                       SOUNDWIRE_IOCTL (21)

#define IOCTL_SOUNDWIRE_START_DATAPORT_2                                     SOUNDWIRE_IOCTL (22)

#define IOCTL_SOUNDWIRE_STOP_DATAPORT_2                                      SOUNDWIRE_IOCTL (23)

#define IOCTL_SOUNDWIRE_PREPARE_TRIGGER                                      SOUNDWIRE_IOCTL (24)

#define IOCTL_SOUNDWIRE_DEPREPARE_TRIGGER                                    SOUNDWIRE_IOCTL (25)

#define IOCTL_SOUNDWIRE_VENDOR_SPECIFIC                                      SOUNDWIRE_IOCTL (26)

#define IOCTL_SOUNDWIRE_SET_PERIPHERAL_CAPABILITIES                         SOUNDWIRE_IOCTL (27)

#define IOCTL_SOUNDWIRE_SET_DATAPORT_CAPABILITIES_2                         SOUNDWIRE_IOCTL (28)

#define IOCTL_SOUNDWIRE_ACQUIRE_CLOCK_REFERENCE                             SOUNDWIRE_IOCTL (29)

#define IOCTL_SOUNDWIRE_RELEASE_CLOCK_REFERENCE                             SOUNDWIRE_IOCTL (30)

#endif
