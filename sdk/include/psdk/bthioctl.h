/*
 * IOCTL definitions for interfacing with winebth.sys
 *
 * Copyright 2024 Vibhav Pant
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#ifndef __BTHIOCTL_H__
#define __BTHIOCTL_H__

#define IOCTL_BTH_GET_LOCAL_INFO    CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x00, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BTH_GET_DEVICE_INFO   CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x02, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BTH_DISCONNECT_DEVICE CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x03, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push,1)

typedef struct _BTH_RADIO_INFO
{
    ULONGLONG lmpSupportedFeatures;
    USHORT mfg;
    USHORT lmpSubversion;
    UCHAR lmpVersion;
} BTH_RADIO_INFO, *PBTH_RADIO_INFO;

typedef struct _BTH_LOCAL_RADIO_INFO
{
    BTH_DEVICE_INFO localInfo;
    ULONG flags;
    USHORT hciRevision;
    UCHAR hciVersion;
    BTH_RADIO_INFO radioInfo;
} BTH_LOCAL_RADIO_INFO, *PBTH_LOCAL_RADIO_INFO;

typedef struct _BTH_DEVICE_INFO_LIST
{
    ULONG numOfDevices;
    BTH_DEVICE_INFO deviceList[1];
} BTH_DEVICE_INFO_LIST, *PBTH_DEVICE_INFO_LIST;

#pragma pack(pop)

#if ((NTDDI_VERSION >= NTDDI_VISTA))

#define BTH_IOCTL_BASE      0

#define BTH_CTL(id)         CTL_CODE(FILE_DEVICE_BLUETOOTH,  \
                                     (id), \
                                     METHOD_BUFFERED,  \
                                     FILE_ANY_ACCESS)

#define BTH_KERNEL_CTL(id)  CTL_CODE(FILE_DEVICE_BLUETOOTH,  \
                                     (id), \
                                     METHOD_NEITHER,  \
                                     FILE_ANY_ACCESS)

#define IOCTL_INTERNAL_BTH_SUBMIT_BRB       BTH_KERNEL_CTL(BTH_IOCTL_BASE+0x00)

#define IOCTL_INTERNAL_BTHENUM_GET_DEVINFO  BTH_KERNEL_CTL(BTH_IOCTL_BASE+0x02)

#define IOCTL_BTH_SDP_CONNECT               BTH_CTL(BTH_IOCTL_BASE+0x80)

#define IOCTL_BTH_SDP_DISCONNECT            BTH_CTL(BTH_IOCTL_BASE+0x81)

#define IOCTL_BTH_SDP_SERVICE_ATTRIBUTE_SEARCH \
                                            BTH_CTL(BTH_IOCTL_BASE+0x84)

#define IOCTL_BTH_SDP_SUBMIT_RECORD         BTH_CTL(BTH_IOCTL_BASE+0x85)

#define IOCTL_BTH_SDP_REMOVE_RECORD         BTH_CTL(BTH_IOCTL_BASE+0x86)

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define IOCTL_BTH_GET_HOST_SUPPORTED_FEATURES BTH_CTL(BTH_IOCTL_BASE+0x88)

#endif

#define SDP_REQUEST_TO_DEFAULT      (0)

#if !defined(HANDLE_SDP_TYPE)

typedef ULONGLONG HANDLE_SDP, *PHANDLE_SDP;

#define HANDLE_SDP_TYPE         HANDLE_SDP

#define HANDLE_SDP_FIELD_NAME   hConnection

#define HANDLE_SDP_NULL     ((HANDLE_SDP)0x0)

#endif

#include <pshpack1.h>
typedef struct _BTH_SDP_CONNECT
{
    BTH_ADDR     bthAddress;
    ULONG       fSdpConnect;
    HANDLE_SDP_TYPE HANDLE_SDP_FIELD_NAME;
    UCHAR       requestTimeout;
} BTH_SDP_CONNECT,  *PBTH_SDP_CONNECT;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _BTH_SDP_DISCONNECT
{
    HANDLE_SDP_TYPE HANDLE_SDP_FIELD_NAME;
} BTH_SDP_DISCONNECT, *PBTH_SDP_DISCONNECT;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _BTH_SDP_SERVICE_ATTRIBUTE_SEARCH_REQUEST
{
    HANDLE_SDP_TYPE HANDLE_SDP_FIELD_NAME;
    ULONG searchFlags;
    SdpQueryUuid uuids[MAX_UUIDS_IN_QUERY];
    SdpAttributeRange range[1];
} BTH_SDP_SERVICE_ATTRIBUTE_SEARCH_REQUEST,
  *PBTH_SDP_SERVICE_ATTRIBUTE_SEARCH_REQUEST;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _BTH_SDP_STREAM_RESPONSE
{
    ULONG requiredSize;
    ULONG responseSize;
    UCHAR response[1];
} BTH_SDP_STREAM_RESPONSE, *PBTH_SDP_STREAM_RESPONSE;
#include <poppack.h>

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define BTH_HOST_FEATURE_ENHANCED_RETRANSMISSION_MODE          (0x0000000000000001)

#include <pshpack1.h>
typedef struct _BTH_HOST_FEATURE_MASK
{
    ULONGLONG Mask;
    ULONGLONG Reserved1;
    ULONGLONG Reserved2;
} BTH_HOST_FEATURE_MASK, *PBTH_HOST_FEATURE_MASK;
#include <poppack.h>

#endif
#endif

#endif /* __BTHIOCTL_H__ */
