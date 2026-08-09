/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Battery telemetry unit conversion helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

static __inline BOOL
TmBatteryChargePercent(ULONG remaining, ULONG full, double* percent)
{
    if (remaining == BATTERY_UNKNOWN_CAPACITY || full == BATTERY_UNKNOWN_CAPACITY || !full || !percent) return FALSE;
    *percent = 100.0 * remaining / full;
    if (*percent > 100.0) *percent = 100.0;
    return TRUE;
}

static __inline BOOL
TmBatteryHealthPercent(const BATTERY_INFORMATION* information, double* percent)
{
    if (!information || !percent || information->FullChargedCapacity == BATTERY_UNKNOWN_CAPACITY || information->DesignedCapacity == BATTERY_UNKNOWN_CAPACITY || !information->DesignedCapacity) return FALSE;
    *percent = 100.0 * information->FullChargedCapacity / information->DesignedCapacity;
    return TRUE;
}

static __inline BOOL
TmBatteryPowerWatts(const BATTERY_INFORMATION* information, const BATTERY_STATUS* status, double* watts)
{
    if (!information || !status || !watts || (information->Capabilities & BATTERY_CAPACITY_RELATIVE) || status->Rate == (LONG)BATTERY_UNKNOWN_RATE) return FALSE;
    *watts = status->Rate / 1000.0;
    return TRUE;
}

static __inline BOOL
TmBatteryCurrentAmps(const BATTERY_INFORMATION* information, const BATTERY_STATUS* status, double* amperes)
{
    if (!information || !status || !amperes || (information->Capabilities & BATTERY_CAPACITY_RELATIVE) || status->Rate == (LONG)BATTERY_UNKNOWN_RATE || status->Voltage == BATTERY_UNKNOWN_VOLTAGE || !status->Voltage) return FALSE;
    *amperes = (double)status->Rate / status->Voltage;
    return TRUE;
}
