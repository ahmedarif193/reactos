/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared private interface for the RPi5 display miniport and ICD
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#ifndef _REACTOS_RPI5VC4_XPDM_H_
#define _REACTOS_RPI5VC4_XPDM_H_

#define IOCTL_VIDEO_RPI5VC4_LATCH_SCANOUT \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x830, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_QUERY_V3D \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x831, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define RPI5VC4_ESCAPE_QUERY_V3D 0x52505633 /* "RPV3" */

#define RPI5VC4_XPDM_ABI_VERSION 1

#define RPI5VC4_V3D_FLAG_SMS_MAPPED  (1u << 0)
#define RPI5VC4_V3D_FLAG_POWERED     (1u << 1)
#define RPI5VC4_V3D_FLAG_REGS_MAPPED (1u << 2)
#define RPI5VC4_V3D_FLAG_IDENT_VALID (1u << 3)

typedef struct _RPI5VC4_V3D_INFO
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Flags;
    ULONG Version;
    ULONG CoreCount;
    ULONG SmsReeCs;
    ULONG SmsTeeCs;
    ULONG HubIdent[4];
    ULONG CoreIdent[3];
    ULONG MmuDebugInfo;
    ULONG Reserved[8];
} RPI5VC4_V3D_INFO, *PRPI5VC4_V3D_INFO;

#endif /* _REACTOS_RPI5VC4_XPDM_H_ */
