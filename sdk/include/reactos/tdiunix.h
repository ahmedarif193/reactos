/*
 * PROJECT:     ReactOS AF_UNIX local stream transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     TDI address definitions shared by afunix.sys, wshunix.dll and tdihelpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#ifndef _TDIUNIX_H
#define _TDIUNIX_H

#define AFUNIX_DEVICE_NAME L"\\Device\\Afunix"
#define AFUNIX_PATH_LENGTH 108

#include <pshpack1.h>
typedef struct _TDI_ADDRESS_UNIX {
    CHAR sun_path[AFUNIX_PATH_LENGTH];
} TDI_ADDRESS_UNIX, *PTDI_ADDRESS_UNIX;

typedef struct _TA_UNIX_ADDRESS {
    LONG TAAddressCount;
    struct _AddrUnix {
        USHORT AddressLength;
        USHORT AddressType;
        TDI_ADDRESS_UNIX Address[1];
    } Address[1];
} TA_UNIX_ADDRESS, *PTA_UNIX_ADDRESS;
#include <poppack.h>

#define TDI_ADDRESS_LENGTH_UNIX sizeof(TDI_ADDRESS_UNIX)

#endif /* _TDIUNIX_H */
