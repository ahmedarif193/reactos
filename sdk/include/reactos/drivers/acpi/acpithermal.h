/*
 * PROJECT:         ReactOS ACPI thermal zone interface
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Extended thermal zone information shared with user mode
 */

#pragma once

typedef struct _ACPI_THERMAL_INFORMATION_EX
{
    ULONG ThermalStamp;
    ULONG ThermalConstant1;
    ULONG ThermalConstant2;
    ULONG SamplingPeriod;
    ULONG CurrentTemperature;
    ULONG PassiveTripPoint;
    ULONG ThermalStandbyTripPoint;
    ULONG CriticalTripPoint;
    UCHAR ActiveTripPointCount;
    UCHAR PassiveCoolingDevicesPresent;
    USHORT Reserved;
    ULONG ActiveTripPoint[MAX_ACTIVE_COOLING_LEVELS];
    ULONG S4TransitionTripPoint;
    ULONG MinimumThrottle;
    ULONG OverThrottleThreshold;
    ULONG PollingPeriod;
} ACPI_THERMAL_INFORMATION_EX, *PACPI_THERMAL_INFORMATION_EX;

C_ASSERT(sizeof(ACPI_THERMAL_INFORMATION_EX) == 92);
