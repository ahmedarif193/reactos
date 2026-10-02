/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SoundWire audio class interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

DEFINE_GUID(SDCAXU_INTERFACE,
    0x6A000532, 0x8C2A, 0x4392, 0x9A, 0xD1, 0x3F, 0x03, 0x05, 0xB3, 0xCF, 0x88);

#define SDCAXU_INTERFACE_VERSION_0100   (0x0100)

#define SDCAXU_INTERFACE_VERSION_0101   (0x0101)

typedef NTSTATUS EVT_SDCAXU_SET_XU_ENTITIES
(
    _In_    PVOID   Context,
    _In_    ULONG   NumEntities,
    _In_reads_(NumEntities)
            ULONG   EntityIDs[]
);
typedef EVT_SDCAXU_SET_XU_ENTITIES *PFN_SDCAXU_SET_XU_ENTITIES;

#define MAX_EXTENSION_NUM_DATAPORTS (15)

typedef struct _SDCAXU_INTERRUPT_INFO
{
    ULONG   Size;
    ULONG   SCPInterruptMask;
    UINT8   DataPortInterrupts[MAX_EXTENSION_NUM_DATAPORTS];
    ULONG   SDCAInterruptMask;
} SDCAXU_INTERRUPT_INFO, * PSDCAXU_INTERRUPT_INFO;

typedef NTSTATUS EVT_SDCAXU_REGISTER_FOR_INTERRUPTS
(
    _In_    PVOID                   Context,
    _In_    PSDCAXU_INTERRUPT_INFO  InterruptInfo
);
typedef EVT_SDCAXU_REGISTER_FOR_INTERRUPTS *PFN_SDCAXU_REGISTER_FOR_INTERRUPTS;

typedef NTSTATUS EVT_SDCAXU_INTERRUPT_HANDLER
(
    _In_    PVOID                   Context,
    _Inout_ PSDCAXU_INTERRUPT_INFO  Interrupts
);
typedef EVT_SDCAXU_INTERRUPT_HANDLER *PFN_SDCAXU_INTERRUPT_HANDLER;

typedef enum _SDCAXU_NOTIFICATION_TYPE
{

    SdcaXuNotificationTypePowerPre = 0,

    SdcaXuNotificationTypePowerPost,

    SdcaXuNotificationTypeJackDetect,

    SdcaXuNotificationTypeHardwareReset,

    SdcaXuNotificationTypeCommitGroup,

    SdcaXuNotificationTypePosture,

    SdcaXuNotificationTypeCount
} SDCAXU_NOTIFICATION_TYPE;

typedef enum _SDCAXU_POWER_STATE
{
    SdcaXuPowerState0 = 0,
    SdcaXuPowerState1,
    SdcaXuPowerState2,
    SdcaXuPowerState3,
    SdcaXuPowerState4,

    SdcaXuPowerStateCount
} SDCAXU_POWER_STATE;

typedef struct _SDCAXU_NOTIFICATION_POWER
{
    ULONG               PowerDomainEntityId;
    SDCAXU_POWER_STATE  OldState;
    SDCAXU_POWER_STATE  NewState;
} SDCAXU_NOTIFICATION_POWER, *PSDCAXU_NOTIFICATION_POWER;

typedef enum _SDCAXU_JACK_EVENT
{
    SdcaXuJackEventUnplugged = 0,
    SdcaXuJackEventPlugged,

    SdcaXuJackEventCount
} SDCAXU_JACK_EVENT;

typedef struct _SDCAXU_NOTIFICATION_JACK_DETECT
{

    ULONG                   GroupEntityId;

    ULONG                   DetectedMode;

    SDCAXU_JACK_EVENT       JackEvent;
} SDCAXU_NOTIFICATION_JACK_DETECT, *PSDCAXU_NOTIFICATION_JACK_DETECT;

typedef struct _SDCAXU_NOTIFICATION_COMMIT_GROUP
{

    ULONG                   CommitGroupHandle;
} SDCAXU_NOTIFICATION_COMMIT_GROUP, *PSDCAXU_NOTIFICATION_COMMIT_GROUP;

typedef enum _SDCAXU_POSTURE
{
    SdcaXuPostureOrientationNotRotated = 0,
    SdcaXuPostureOrientationRotated90DegreesCounterClockwise,
    SdcaXuPostureOrientationRotated180DegreesCounterClockwise,
    SdcaXuPostureOrientationRotated270DegreesCounterClockwise,
    SdcaXuPostureLidClosed,

    SdcaXuPostureCount
} SDCAXU_POSTURE, *PSDCAXU_POSTURE;

typedef struct _SDCAXU_NOTIFICATION_POSTURE
{
    SDCAXU_POSTURE          Posture;
} SDCAXU_NOTIFICATION_POSTURE, *PSDCAXU_NOTIFICATION_POSTURE;

typedef NTSTATUS EVT_SDCAXU_CHANGE_NOTIFICATION
(
    _In_        PVOID                       Context,
    _In_        SDCAXU_NOTIFICATION_TYPE    NotificationType,
    _In_opt_    PVOID                       NotificationData,
    _In_        ULONG                       NotificationDataSize
);
typedef EVT_SDCAXU_CHANGE_NOTIFICATION *PFN_SDCAXU_CHANGE_NOTIFICATION;

typedef enum _SDCAXU_HW_CONFIG_TYPE
{

    SdcaXuHwConfigTypeAcpiBlob = 0,

    SdcaXuHwConfigTypeCount
} SDCAXU_HW_CONFIG_TYPE;

typedef NTSTATUS EVT_SDCAXU_SET_HW_CONFIG
(
    _In_        PVOID                   Context,
    _In_        SDCAXU_HW_CONFIG_TYPE   HwConfigType,
    _In_opt_    PVOID                   HwConfigData,
    _In_        ULONG                   HwConfigDataSize
);
typedef EVT_SDCAXU_SET_HW_CONFIG *PFN_SDCAXU_SET_HW_CONFIG;

typedef struct _SDCAXU_ACX_CIRCUIT_CONFIG
{

    ULONG cbSize;

    GUID ComponentID;

    UNICODE_STRING ComponentUri;

    GUID ContainerID;

    UNICODE_STRING CircuitName;

    ULONG CircuitType;

    PVOID CircuitContext;

} SDCAXU_ACX_CIRCUIT_CONFIG, *PSDCAXU_ACX_CIRCUIT_CONFIG;

typedef enum _SDCAXU_ENDPOINT_CONFIG_TYPE
{

    SdcaXuEndpointConfigTypeAcxCircuitConfig = 0,

    SdcaXuEndpointConfigTypeCount
} SDCAXU_ENDPOINT_CONFIG_TYPE;

typedef NTSTATUS EVT_SDCAXU_SET_ENDPOINT_CONFIG
(
    _In_        PVOID                           Context,
    _In_        SDCAXU_ENDPOINT_CONFIG_TYPE     EndpointConfigType,
    _In_opt_    PVOID                           EndpointConfigData,
    _In_        ULONG                           EndpointConfigDataSize
);
typedef EVT_SDCAXU_SET_ENDPOINT_CONFIG *PFN_SDCAXU_SET_ENDPOINT_CONFIG;

typedef NTSTATUS EVT_SDCAXU_REMOVE_ENDPOINT_CONFIG
(
    _In_        PVOID                           Context,
    _In_        SDCAXU_ENDPOINT_CONFIG_TYPE     EndpointConfigType,
    _In_opt_    PVOID                           EndpointConfigData,
    _In_        ULONG                           EndpointConfigDataSize
);
typedef EVT_SDCAXU_REMOVE_ENDPOINT_CONFIG *PFN_SDCAXU_REMOVE_ENDPOINT_CONFIG;

typedef NTSTATUS EVT_SDCAXU_SET_JACK_OVERRIDE
(
    _In_ PVOID      Context,
    _In_ BOOLEAN    Override
);
typedef EVT_SDCAXU_SET_JACK_OVERRIDE *PFN_SDCAXU_SET_JACK_OVERRIDE;

typedef NTSTATUS EVT_SDCAXU_SET_JACK_SELECTED_MODE
(
    _In_ PVOID Context,
    _In_ ULONG GroupEntityId,
    _In_ ULONG SelectedMode
);
typedef EVT_SDCAXU_SET_JACK_SELECTED_MODE *PFN_SDCAXU_SET_JACK_SELECTED_MODE;

typedef NTSTATUS EVT_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE
(
    _In_ PVOID                  Context,
    _In_ ULONG                  PowerDomainEntityId,
    _In_ SDCAXU_POWER_STATE     RequiredState
);
typedef EVT_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE *PFN_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE;

typedef NTSTATUS EVT_SDCAXU_PDE_POWER_REFERENCE_RELEASE
(
    _In_ PVOID                  Context,
    _In_ ULONG                  PowerDomainEntityId,
    _In_ SDCAXU_POWER_STATE     ReleasedState
);
typedef EVT_SDCAXU_PDE_POWER_REFERENCE_RELEASE *PFN_SDCAXU_PDE_POWER_REFERENCE_RELEASE;

typedef NTSTATUS EVT_SDCAXU_READ_DEFERRED_AUDIO_CONTROLS
(
    _In_ PVOID                          Context,
    _Inout_ PSDCA_AUDIO_CONTROLS        Controls
);
typedef EVT_SDCAXU_READ_DEFERRED_AUDIO_CONTROLS *PFN_SDCAXU_READ_DEFERRED_AUDIO_CONTROLS;

typedef NTSTATUS EVT_SDCAXU_WRITE_DEFERRED_AUDIO_CONTROLS
(
    _In_ PVOID                          Context,
    _Inout_ PSDCA_AUDIO_CONTROLS        Controls
);
typedef EVT_SDCAXU_WRITE_DEFERRED_AUDIO_CONTROLS *PFN_SDCAXU_WRITE_DEFERRED_AUDIO_CONTROLS;

typedef struct _SDCAXU_INTERFACE_V0100
{

    INTERFACE InterfaceHeader;
    PFN_SDCAXU_SET_HW_CONFIG                EvtSetHwConfig;
    PFN_SDCAXU_SET_ENDPOINT_CONFIG          EvtSetEndpointConfig;
    PFN_SDCAXU_REMOVE_ENDPOINT_CONFIG       EvtRemoveEndpointConfig;
    PFN_SDCAXU_INTERRUPT_HANDLER            EvtInterruptHandler;
    PFN_SDCAXU_CHANGE_NOTIFICATION          EvtChangeNotification;

    PFN_SDCAXU_SET_XU_ENTITIES              EvtSetXUEntities;
    PFN_SDCAXU_REGISTER_FOR_INTERRUPTS      EvtRegisterForInterrupts;
    PFN_SDCAXU_SET_JACK_OVERRIDE            EvtSetJackOverride;
    PFN_SDCAXU_SET_JACK_SELECTED_MODE       EvtSetJackSelectedMode;
    PFN_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE  EvtPDEPowerReferenceAcquire;
    PFN_SDCAXU_PDE_POWER_REFERENCE_RELEASE  EvtPDEPowerReferenceRelease;
} SDCAXU_INTERFACE_V0100, *PSDCAXU_INTERFACE_V0100;

typedef struct _SDCAXU_INTERFACE_V0101
{

    INTERFACE InterfaceHeader;
    PFN_SDCAXU_SET_HW_CONFIG                        EvtSetHwConfig;
    PFN_SDCAXU_SET_ENDPOINT_CONFIG                  EvtSetEndpointConfig;
    PFN_SDCAXU_REMOVE_ENDPOINT_CONFIG               EvtRemoveEndpointConfig;
    PFN_SDCAXU_INTERRUPT_HANDLER                    EvtInterruptHandler;
    PFN_SDCAXU_CHANGE_NOTIFICATION                  EvtChangeNotification;

    PFN_SDCAXU_SET_XU_ENTITIES                      EvtSetXUEntities;
    PFN_SDCAXU_REGISTER_FOR_INTERRUPTS              EvtRegisterForInterrupts;
    PFN_SDCAXU_SET_JACK_OVERRIDE                    EvtSetJackOverride;
    PFN_SDCAXU_SET_JACK_SELECTED_MODE               EvtSetJackSelectedMode;
    PFN_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE          EvtPDEPowerReferenceAcquire;
    PFN_SDCAXU_PDE_POWER_REFERENCE_RELEASE          EvtPDEPowerReferenceRelease;
    PFN_SDCAXU_READ_DEFERRED_AUDIO_CONTROLS         EvtReadDeferredAudioControls;
    PFN_SDCAXU_WRITE_DEFERRED_AUDIO_CONTROLS        EvtWriteDeferredAudioControls;
} SDCAXU_INTERFACE_V0101, *PSDCAXU_INTERFACE_V0101;
