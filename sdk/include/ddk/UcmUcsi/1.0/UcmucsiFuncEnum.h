/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Type-C connector system software interface class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _UCMUCSIFUNCENUM_H_
#define _UCMUCSIFUNCENUM_H_

extern PUCMUCSI_DRIVER_GLOBALS UcmucsiDriverGlobals;

typedef enum _UCMUCSIFUNCENUM {

    UcmUcsiDeviceInitInitializeTableIndex = 0,
    UcmUcsiDeviceInitializeTableIndex = 1,
    UcmUcsiConnectorCollectionCreateTableIndex = 2,
    UcmUcsiConnectorCollectionAddConnectorTableIndex = 3,
    UcmUcsiPpmCreateTableIndex = 4,
    UcmUcsiPpmSetUcsiCommandRequestQueueTableIndex = 5,
    UcmUcsiPpmStartTableIndex = 6,
    UcmUcsiPpmStopTableIndex = 7,
    UcmUcsiPpmNotificationTableIndex = 8,
    UcmucsiFunctionTableNumEntries = 9,
} UCMUCSIFUNCENUM;

#endif
