/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Type-C port controller interface class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _UCMTCPCIFUNCENUM_H_
#define _UCMTCPCIFUNCENUM_H_

extern PUCMTCPCI_DRIVER_GLOBALS UcmtcpciDriverGlobals;

typedef enum _UCMTCPCIFUNCENUM {

    UcmTcpciDeviceInitInitializeTableIndex = 0,
    UcmTcpciDeviceInitializeTableIndex = 1,
    UcmTcpciPortControllerCreateTableIndex = 2,
    UcmTcpciPortControllerSetHardwareRequestQueueTableIndex = 3,
    UcmTcpciPortControllerStartTableIndex = 4,
    UcmTcpciPortControllerStopTableIndex = 5,
    UcmTcpciPortControllerAlertTableIndex = 6,
    UcmtcpciFunctionTableNumEntries = 7,
} UCMTCPCIFUNCENUM;

#endif
