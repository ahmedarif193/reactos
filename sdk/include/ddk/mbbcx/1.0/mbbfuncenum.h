/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     MBBCx 1.0 class extension interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _MBBFUNCENUM_H_
#define _MBBFUNCENUM_H_

extern PMBB_DRIVER_GLOBALS MbbDriverGlobals;

typedef enum _MBBFUNCENUM {

    MbbDeviceInitConfigTableIndex = 0,
    MbbDeviceInitializeTableIndex = 1,
    MbbDeviceSetMbimParametersTableIndex = 2,
    MbbDeviceResponseAvailableTableIndex = 3,
    MbbDeviceSendDeviceServiceSessionDataCompleteTableIndex = 4,
    MbbDeviceReceiveDeviceServiceSessionDataTableIndex = 5,
    MbbAdapterInitializeTableIndex = 6,
    MbbAdapterGetSessionIdTableIndex = 7,
    MbbRequestGetBufferTableIndex = 8,
    MbbRequestGetCorrelationIdTableIndex = 9,
    MbbRequestCompleteTableIndex = 10,
    MbbRequestCompleteWithInformationTableIndex = 11,
    MbbDeviceSetWakeCapabilitiesTableIndex = 12,
    MbbDeviceReportWakeReasonTableIndex = 13,
    MbbDeviceSetOptionalParametersTableIndex = 14,
    MbbFunctionTableNumEntries = 15,
} MBBFUNCENUM;

#endif
