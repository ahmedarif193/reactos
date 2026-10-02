/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB connector manager class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _UCMFUNCENUM_H_
#define _UCMFUNCENUM_H_

extern PUCM_DRIVER_GLOBALS UcmDriverGlobals;

typedef enum _UCMFUNCENUM {

    UcmInitializeDeviceTableIndex = 0,
    UcmConnectorCreateTableIndex = 1,
    UcmConnectorTypeCAttachTableIndex = 2,
    UcmConnectorTypeCDetachTableIndex = 3,
    UcmConnectorTypeCCurrentAdChangedTableIndex = 4,
    UcmConnectorPdSourceCapsTableIndex = 5,
    UcmConnectorPdPartnerSourceCapsTableIndex = 6,
    UcmConnectorPdConnectionStateChangedTableIndex = 7,
    UcmConnectorChargingStateChangedTableIndex = 8,
    UcmConnectorDataDirectionChangedTableIndex = 9,
    UcmConnectorPowerDirectionChangedTableIndex = 10,
    UcmFunctionTableNumEntries = 11,
} UCMFUNCENUM;

#endif
