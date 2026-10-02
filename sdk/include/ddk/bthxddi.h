/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth extensible transport driver interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef __BTHXDDI_H__
#define __BTHXDDI_H__

#ifdef __cplusplus
extern "C" {
#endif

#define __BTHXDDI_H__

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define BTHX_DDI_VERSION_1  0x00000001

typedef enum _BTHX_HCI_PACKET_TYPE {
    HciPacketCommand    = 0x01,
    HciPacketAclData    = 0x02,
    HciPacketEvent      = 0x04
} BTHX_HCI_PACKET_TYPE;

#define BTHX_IOCTL_BASE      0

#define BTHX_CTL(id)  CTL_CODE(FILE_DEVICE_BLUETOOTH,  \
                                     (id), \
                                     METHOD_NEITHER,  \
                                     FILE_ANY_ACCESS)

#define IOCTL_BTHX_GET_VERSION              BTHX_CTL(BTHX_IOCTL_BASE+0x100)

#define IOCTL_BTHX_SET_VERSION              BTHX_CTL(BTHX_IOCTL_BASE+0x101)

#define IOCTL_BTHX_QUERY_CAPABILITIES       BTHX_CTL(BTHX_IOCTL_BASE+0x102)

#define IOCTL_BTHX_WRITE_HCI                BTHX_CTL(BTHX_IOCTL_BASE+0x103)

#define IOCTL_BTHX_READ_HCI                 BTHX_CTL(BTHX_IOCTL_BASE+0x104)

typedef struct _BTHX_VERSION {
    ULONG Version;
} BTHX_VERSION, *PBTHX_VERSION;

__declspec(selectany)
BTHX_VERSION Microsoft_BTHX_DDI_Version = { BTHX_DDI_VERSION_1 };

typedef enum _BTHX_SCO_SUPPORT {
    ScoSupportNone          = 0,
    ScoSupportHCI           = 1,
    ScoSupportHCIBypass     = 2,
} BTHX_SCO_SUPPORT, *PBTHX_SCO_SUPPORT;

typedef struct _BTHX_CAPABILITIES {
    ULONG               MaxAclTransferInSize;
    BTHX_SCO_SUPPORT    ScoSupport;
    ULONG               MaxScoChannels;
    BOOLEAN             IsDeviceIdleCapable;
    BOOLEAN             IsDeviceWakeCapable;
} BTHX_CAPABILITIES, *PBTHX_CAPABILITIES;

#include <pshpack1.h>
typedef struct _BTHX_HCI_READ_WRITE_CONTEXT {
    ULONG   DataLen;
    UCHAR   Type;
    _Field_size_bytes_(DataLen) UCHAR   Data[1];
} BTHX_HCI_READ_WRITE_CONTEXT, *PBTHX_HCI_READ_WRITE_CONTEXT;
#include <poppack.h>

#endif

#ifdef __cplusplus
}
#endif

#endif
