/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SoundWire audio class interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef ANYSIZE_ARRAY
#define ANYSIZE_ARRAY 1
#endif

#define MAX_PATH_DESCRIPTORS 16
#define MAX_DATAPORT_PER_TERMINAL 16

#define STATIC_KSPROPERTYSETID_Sdca\
    0x55eff601, 0x40d2, 0x4fc9, 0xb4, 0x1d, 0x9, 0xb1, 0x8c, 0x6b, 0xfb, 0x1f
DEFINE_GUIDSTRUCT("55EFF601-40D2-4FC9-B41D-09B18C6BFB1F", KSPROPERTYSETID_Sdca);
#define KSPROPERTYSETID_Sdca DEFINE_GUIDNAMED(KSPROPERTYSETID_Sdca)

typedef enum {
    KSPROPERTY_SDCA_FUNCTION_INFORMATION    = 1,
    KSPROPERTY_SDCA_VENDOR_SPECIFIC         = 2,
    KSPROPERTY_SDCA_FUNCTION_CAPABILITY     = 3,
    KSPROPERTY_SDCA_PATH_DESCRIPTORS        = 4,
    KSPROPERTY_SDCA_CREATE_PATH             = 5,
    KSPROPERTY_SDCA_DESTROY_PATH            = 6,
    KSPROPERTY_SDCA_START_PATH              = 7,
    KSPROPERTY_SDCA_STOP_PATH               = 8,
    KSPROPERTY_SDCA_ACCESS_EVENTS           = 9,
} KSPROPERTY_SDCA;

typedef enum _SDCA_PATH {
    SdcaPathUltrasoundRender                = 0x1,
    SdcaPathUltrasoundCapture               = 0x2,
    SdcaPathReferenceStream                 = 0x4,
    SdcaPathIvSense                         = 0x8,
} SDCA_PATH, *PSDCA_PATH;

typedef enum _SDCA_STREAMING_FUNCTION_INFORMATION_FLAGS
{
    SdcaFunctionInformationFlagSubSystemIdPresent  = 0x00000001,
    SdcaFunctionInformationFlagControllerIdPresent = 0x00000002,
    SdcaFunctionInformationFlagLinkIdPresent       = 0x00000004,
    SdcaFunctionInformationFlagPeripheralIdPresent = 0x00000008,
} SDCA_STREAMING_FUNCTION_INFORMATION_FLAGS;

typedef struct _SDCA_STREAMING_FUNCTION_INFORMATION
{
    ULONG Size;
    ULONG Flags;
    ULONG FunctionInformationId;
    UINT8 FunctionNumber;
    UINT8 FunctionSdcaVersion;
    UINT8 FunctionSdcaRevision;
    UINT8 FunctionType;
    UINT16 FunctionManufacturerId;
    UINT16 FunctionId;
    UINT8 FunctionVersion;
    ULONG SubSystemId;
    UINT8 ControllerId;
    UINT8 LinkId;
    UINT8 PeripheralId;
    UINT8 UniqueId;
} SDCA_STREAMING_FUNCTION_INFORMATION, *PSDCA_STREAMING_FUNCTION_INFORMATION;

typedef struct
{
    ULONG FunctionCount;
    SDCA_STREAMING_FUNCTION_INFORMATION FunctionInfoList[ANYSIZE_ARRAY];
} SDCA_FUNCTION_INFORMATION_LIST, * PSDCA_FUNCTION_INFORMATION_LIST;

typedef struct
{
    ULONG Size;
    ULONG FunctionInformationId;
    ULONG DataPortCount;
    UINT8 DataPorts[MAX_DATAPORT_PER_TERMINAL];
    ULONG FormatCount;
    WAVEFORMATEXTENSIBLE Formats[ANYSIZE_ARRAY];
} SDCA_PATH_DESCRIPTOR, * PSDCA_PATH_DESCRIPTOR;

typedef struct
{
    ULONG Size;
    SDCA_PATH SdcaPath;
    ULONG EndpointId;
    ULONG DescriptorCount;
} SDCA_PATH_DESCRIPTORS, * PSDCA_PATH_DESCRIPTORS;

#define STATIC_KSPROPERTYSETID_SdcaKws\
    0x947d5153, 0x1286, 0x468c, 0xa6, 0x8d, 0xf8, 0x39, 0x56, 0x3, 0x7f, 0xfd
DEFINE_GUIDSTRUCT("947D5153-1286-468C-A68D-F83956037FFD", KSPROPERTYSETID_SdcaKws);
#define KSPROPERTYSETID_SdcaKws DEFINE_GUIDNAMED(KSPROPERTYSETID_SdcaKws)

#define MAX_SDCA_ENTITY_LABEL_LENGTH 12

#define MAX_INPUT_PIN_COUNT 4

typedef enum {
    KSPROPERTY_SDCAKWS_DEVICE_CAPABILITY    = 1,
    KSPROPERTY_SDCAKWS_VAD_CAPABILITY       = 2,
    KSPROPERTY_SDCAKWS_VAD_ENTITIES         = 3,
    KSPROPERTY_SDCAKWS_ACCESS_EVENTS        = 4,
    KSPROPERTY_SDCAKWS_CONFIGURE_VAD_PORT   = 5,
    KSPROPERTY_SDCAKWS_CLEANUP_VAD_PORT     = 6,
} KSPROPERTY_SDCAKWS;

typedef enum _DATA_PATHS
{
    SupportedDataPathsRawPCM        = 0x1,
    SupportedDataPathsBufferedRaw   = 0x2
} DATA_PATHS, *PDATA_PATHS;

typedef enum _HISTORY_BUFFER_MODES
{
    HistoryBufferModesFlowThrough   = 0x1,
    HistoryBufferModesFastStream    = 0x2,
    HistoryBufferModesRead          = 0x4
} HISTORY_BUFFER_MODES, *PHISTORY_BUFFER_MODES;

typedef enum _VAD_ACCESS_MODE
{
    VADAccessModeRW     = 0x0,
    VADAccessModeDual   = 0x1,
    VADAccessModeRW1C   = 0x2,
    VADAccessModeRO     = 0x3,
    VADAccessModeW1S    = 0x4,
    VADAccessModeDC     = 0x5,
} VAD_ACCESS_MODE, *PVAD_ACCESS_MODE;

typedef struct _DEVICE_KWS_CAPABILITY_DESCRIPTOR
{
    ULONG DataPathsSupported;
} DEVICE_KWS_CAPABILITY_DESCRIPTOR, * PDEVICE_KWS_CAPABILITY_DESCRIPTOR;

typedef struct _VAD_DESCRIPTOR
{
    UINT8 VadInterruptPosition;
    ULONG SupportedHistoryBufferModes;
    ULONG HistoryBufferPreamble;
    ULONG HistoryBufferPreambleOverFlow;
    VAD_ACCESS_MODE HistoryBufferMessageOffsetAccessMode;
    ULONG HistoryBufferMessageOffsetDCValue;
    VAD_ACCESS_MODE HistoryBufferMessageLengthAccessMode;
    ULONG HistoryBufferMessageLengthDCValue;
    ULONG FormatCount;
    WAVEFORMATEXTENSIBLE Format[ANYSIZE_ARRAY];
} VAD_DESCRIPTOR, *PVAD_DESCRIPTOR;

typedef struct _ENTITY_INFO
{
    UINT8 EntityType;
    UINT8 EntityId;
    char EntityLabel[MAX_SDCA_ENTITY_LABEL_LENGTH];
    UINT8 InputPin[MAX_INPUT_PIN_COUNT];
} ENTITY_INFO, * PENTITY_INFO;

typedef struct _VAD_ENTITIES
{
    ULONG EntityCount;
    ENTITY_INFO SDCAVadEntities[ANYSIZE_ARRAY];
} VAD_ENTITIES, *PVAD_ENTITIES;

typedef struct _SDCA_KWS_NOTIFICATIONS
{
    KEVENT Suspend;
    KEVENT Resume;
} SDCA_KWS_NOTIFICATIONS, *PSDCA_KWS_NOTIFICATIONS, SDCA_ACCESS_EVENTS, *PSDCA_ACCESS_EVENTS;

typedef enum
{
    VadModeStreaming,
    VadModeSharedHardware
} VAD_MODE;

typedef struct _SDCA_KWS_PREPARE_PARAMS
{
    VAD_MODE VadMode;
    ULONG EndpointId;
    WAVEFORMATEXTENSIBLE DetectionFormat;
} SDCA_KWS_PREPARE_PARAMS, *PSDCA_KWS_PREPARE_PARAMS;
