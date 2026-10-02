/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sideband audio device interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _SIDEBANDAUDIO_DEVICE_DESCRIPTOR
{

    ULONG               NumberOfEndpoints;
}SIDEBANDAUDIO_DEVICE_DESCRIPTOR, *PSIDEBANDAUDIO_DEVICE_DESCRIPTOR;

typedef struct _SIDEBANDAUDIO_ENDPOINT_CAPABILITIES
{
    BOOL    Volume;
    BOOL    Mute;
    BOOL    Sidetone;
    BOOL    Feedback;
}SIDEBANDAUDIO_ENDPOINT_CAPABILITIES;

typedef struct _SIDEBANDAUDIO_ENDPOINT_DESCRIPTOR
{

    ULONG                                   CbSize;

    GUID                                    ContainerId;

    GUID                                    Category;

    KSPIN_DATAFLOW                          Direction;

    SIDEBANDAUDIO_ENDPOINT_CAPABILITIES     Capabilities;

    UNICODE_STRING                          FriendlyName;

    ULONG                                   VolumePropertyValuesSize;

    ULONG                                   SidetoneVolumePropertyValueSize;

    ULONG                                   MutePropertyValuesSize;

}SIDEBANDAUDIO_ENDPOINT_DESCRIPTOR, *PSIDEBANDAUDIO_ENDPOINT_DESCRIPTOR;

#if (NTDDI_VERSION >= NTDDI_WIN10_FE)

typedef struct _SIDEBANDAUDIO_ENDPOINT_DESCRIPTOR2
{

    ULONG                                   CbSize;

    GUID                                    ContainerId;

    GUID                                    Category;

    KSPIN_DATAFLOW                          Direction;

    SIDEBANDAUDIO_ENDPOINT_CAPABILITIES     Capabilities;

    UNICODE_STRING                          FriendlyName;

    ULONG                                   VolumePropertyValuesSize;

    ULONG                                   SidetoneVolumePropertyValueSize;

    ULONG                                   MutePropertyValuesSize;

    ULONG                                   FilterInterfacePropertyCount;

    DEVPROPERTY*                            FilterInterfaceProperties;
}SIDEBANDAUDIO_ENDPOINT_DESCRIPTOR2, *PSIDEBANDAUDIO_ENDPOINT_DESCRIPTOR2;
#endif

typedef struct _SIDEBANDAUDIO_VOLUME_PARAMS
{

    ULONG   EpIndex;

    BOOL    Immediate;

    LONG    Channel;

    LONG    Value;
}SIDEBANDAUDIO_VOLUME_PARAMS, *PSIDEBANDAUDIO_VOLUME_PARAMS;

typedef struct _SIDEBANDAUDIO_MUTE_PARAMS
{

    ULONG   EpIndex;

    BOOL    Immediate;

    LONG    Channel;

    BOOL    Value;
}SIDEBANDAUDIO_MUTE_PARAMS, *PSIDEBANDAUDIO_MUTE_PARAMS;

typedef struct _SIDEBANDAUDIO_STREAM_STATUS_PARAMS
{

    ULONG       EpIndex;

    BOOL        Immediate;

    NTSTATUS    Status;
}SIDEBANDAUDIO_STREAM_STATUS_PARAMS, *PSIDEBANDAUDIO_STREAM_STATUS_PARAMS;

typedef struct _SIDEBANDAUDIO_CONNECTION_PARAMS
{

    ULONG   EpIndex;

    BOOL    Immediate;

    BOOL    Connected;
}SIDEBANDAUDIO_CONNECTION_PARAMS, *PSIDEBANDAUDIO_CONNECTION_PARAMS;

typedef struct _SIDEBANDAUDIO_SUPPORTED_FORMATS
{

    ULONG               CbSize;

    ULONG               EpIndex;

    ULONG               NumFormats;

    PKSDATAFORMAT       *Formats;
}SIDEBANDAUDIO_SUPPORTED_FORMATS, *PSIDEBANDAUDIO_SUPPORTED_FORMATS;

DEFINE_GUID(SIDEBANDAUDIO_PARAMS_SET_STANDARD,
    0xbf34616b, 0x8265, 0x4d70, 0xad, 0xb2, 0x91, 0xb3, 0x50, 0xcc, 0xd5, 0xd2);

typedef enum _SIDEBANDAUDIO_PARAMS_MSFT_TYPE_ID
{
    SBAUD_PARAMS_TYPE_RESERVED

}SIDEBANDAUDIO_PARAMS_MSFT_TYPE_ID;

typedef union
{
    struct
    {

        GUID                ParamSet;

        ULONG               TypeId;

        ULONG               Size;
    };
    LONGLONG Alignment;
}SIDEBANDAUDIO_IO_PARAM_HEADER, *PSIDEBANDAUDIO_IO_PARAM_HEADER;

typedef union
{
    struct
    {

        ULONG                           EpIndex;

        SIDEBANDAUDIO_IO_PARAM_HEADER   RequestedSiop;
    };
    LONGLONG Alignment;
}SIDEBANDAUDIO_SIOP_REQUEST_PARAM, *PSIDEBANDAUDIO_SIOP_REQUEST_PARAM;

typedef union
{
    struct
    {

        ULONG                           EpIndex;

        PKSDATAFORMAT                   Format;

        ULONG                           SiopCount;
    };
    LONGLONG Alignment;
}SIDEBANDAUDIO_STREAM_OPEN_PARAMS, *PSIDEBANDAUDIO_STREAM_OPEN_PARAMS;

typedef struct _SIDEBANDAUDIO_SIDETONE_DESCRIPTOR
{
    LONG                Volume;
    BOOL                Mute;
}SIDEBANDAUDIO_SIDETONE_DESCRIPTOR, *PSIDEBANDAUDIO_SIDETONE_DESCRIPTOR;

typedef struct _SIDEBANDAUDIO_SIDETONE_PARAMS
{

    ULONG                               EpIndex;

    BOOL                                Immediate;

    LONG                                Channel;

    SIDEBANDAUDIO_SIDETONE_DESCRIPTOR   Sidetone;
}SIDEBANDAUDIO_SIDETONE_PARAMS, *PSIDEBANDAUDIO_SIDETONE_PARAMS;

typedef struct _SIDEBANDAUDIO_DEVICE_ERROR
{

    ULONG               EpIndex;

    BOOL                Immediate;

    NTSTATUS            Status;
}SIDEBANDAUDIO_DEVICE_ERROR, *PSIDEBANDAUDIO_DEVICE_ERROR;

#define SIDEBANDAUDIO_IOCTL(_index_) \
    CTL_CODE (FILE_DEVICE_UNKNOWN, _index_, METHOD_NEITHER, FILE_ANY_ACCESS)

#define IOCTL_SBAUD_GET_DEVICE_DESCRIPTOR                   SIDEBANDAUDIO_IOCTL (1)
#define IOCTL_SBAUD_GET_ERROR_STATUS_UPDATE                 SIDEBANDAUDIO_IOCTL (2)
#define IOCTL_SBAUD_GET_ENDPOINT_DESCRIPTOR                 SIDEBANDAUDIO_IOCTL (3)
#define IOCTL_SBAUD_GET_CONNECTION_STATUS_UPDATE            SIDEBANDAUDIO_IOCTL (4)
#define IOCTL_SBAUD_GET_VOLUMEPROPERTYVALUES                SIDEBANDAUDIO_IOCTL (5)
#define IOCTL_SBAUD_SET_VOLUME                              SIDEBANDAUDIO_IOCTL (6)
#define IOCTL_SBAUD_GET_VOLUME_STATUS_UPDATE                SIDEBANDAUDIO_IOCTL (7)
#define IOCTL_SBAUD_GET_MUTEPROPERTYVALUES                  SIDEBANDAUDIO_IOCTL (8)
#define IOCTL_SBAUD_SET_MUTE                                SIDEBANDAUDIO_IOCTL (9)
#define IOCTL_SBAUD_GET_MUTE_STATUS_UPDATE                  SIDEBANDAUDIO_IOCTL (10)
#define IOCTL_SBAUD_GET_SIDETONE_VOLUMEPROPERTYVALUES       SIDEBANDAUDIO_IOCTL (11)
#define IOCTL_SBAUD_GET_SIDETONE_STATUS_UPDATE              SIDEBANDAUDIO_IOCTL (12)
#define IOCTL_SBAUD_SET_SIDETONE_PROPERTY                   SIDEBANDAUDIO_IOCTL (13)
#define IOCTL_SBAUD_STREAM_OPEN                             SIDEBANDAUDIO_IOCTL (14)
#define IOCTL_SBAUD_STREAM_CLOSE                            SIDEBANDAUDIO_IOCTL (15)
#define IOCTL_SBAUD_STREAM_START                            SIDEBANDAUDIO_IOCTL (16)
#define IOCTL_SBAUD_STREAM_SUSPEND                          SIDEBANDAUDIO_IOCTL (17)
#define IOCTL_SBAUD_GET_STREAM_STATUS_UPDATE                SIDEBANDAUDIO_IOCTL (18)
#define IOCTL_SBAUD_GET_SUPPORTED_FORMATS                   SIDEBANDAUDIO_IOCTL (19)
#define IOCTL_SBAUD_GET_SIOP                                SIDEBANDAUDIO_IOCTL (20)
#define IOCTL_SBAUD_GET_SIOP_UPDATE                         SIDEBANDAUDIO_IOCTL (21)
#define IOCTL_SBAUD_SET_SIOP                                SIDEBANDAUDIO_IOCTL (23)
#define IOCTL_SBAUD_SET_DEVICE_CLAIMED                      SIDEBANDAUDIO_IOCTL (24)

#if (NTDDI_VERSION >= NTDDI_WIN10_FE)
#define IOCTL_SBAUD_GET_ENDPOINT_DESCRIPTOR2                SIDEBANDAUDIO_IOCTL (25)
#endif
