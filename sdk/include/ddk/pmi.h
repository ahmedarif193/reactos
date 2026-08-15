/*
 * Power Meter Interface (PMI)
 *
 * This file is part of the ReactOS DDK.
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* {E849804E-C719-43D8-AC88-96B894C191E2} */
DEFINE_GUID(GUID_DEVICE_POWER_METER,
  0xe849804e, 0xc719, 0x43d8, 0xac, 0x88, 0x96, 0xb8, 0x94, 0xc1, 0x91, 0xe2);

#ifndef _PMI_
#define _PMI_

#define IOCTL_PMI_GET_CAPABILITIES      CTL_CODE(FILE_DEVICE_PMI, 0, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_PMI_GET_CONFIGURATION     CTL_CODE(FILE_DEVICE_PMI, 1, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_PMI_SET_CONFIGURATION     CTL_CODE(FILE_DEVICE_PMI, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_PMI_GET_MEASUREMENT       CTL_CODE(FILE_DEVICE_PMI, 3, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_PMI_REGISTER_EVENT_NOTIFY CTL_CODE(FILE_DEVICE_PMI, 4, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define PMI_CAPABILITIES_SUPPORT_MEASUREMENT   0x00000001
#define PMI_CAPABILITIES_SUPPORT_THRESHOLDS    0x00000002
#define PMI_CAPABILITIES_SUPPORT_BUDGETING     0x00000004
#define PMI_CAPABILITIES_SUPPORT_BUDGET_NOTIFY 0x00000008
#define PMI_CAPABILITIES_DISCHARGE_ONLY        0x00000100

#define PMI_NUM_THRESHOLDS 2
#define PMI_NAME_MAX 16
#define PMI_DEVICE_PATH_DELIMITER 0x00
#define PMI_VERSION 0x01

typedef enum _PMI_CAPABILITIES_TYPE
{
    PmiReportedCapabilities,
    PmiMeteredHardware,
    PmiCapabilitiesMax
} PMI_CAPABILITIES_TYPE;

typedef enum _PMI_MEASUREMENT_TYPE
{
    PmiMeasurementTypeInput,
    PmiMeasurementTypeOutput,
    PmiMeasurementTypeMax
} PMI_MEASUREMENT_TYPE;

typedef enum _PMI_MEASUREMENT_UNIT
{
    PmiMeasurementUnitMilliWatt,
    PmiMeasurementUnitMax
} PMI_MEASUREMENT_UNIT;

typedef enum _PMI_CONFIGURATION_TYPE
{
    PmiMeasurementConfiguration,
    PmiBudgetingConfiguration,
    PmiThresholdConfiguration,
    PmiConfigurationMax
} PMI_CONFIGURATION_TYPE;

typedef enum _PMI_EVENT_TYPE
{
    PmiCapabilitiesChangedEvent,
    PmiThresholdEvent,
    PmiConfigurationChangedEvent,
    PmiBudgetEvent,
    PmiAveragingIntervalChangedEvent,
    PmiEventMax
} PMI_EVENT_TYPE;

typedef struct _PMI_REPORTED_CAPABILITIES
{
    ULONG Flags;
    PMI_MEASUREMENT_UNIT MeasurementUnit;
    PMI_MEASUREMENT_TYPE MeasurementType;
    ULONG Accuracy;
    ULONG SamplingPeriod;
    ULONG MinimumAverageInterval;
    ULONG MaximumAverageInterval;
    ULONG Hysteresis;
    BOOLEAN Writeable;
    ULONG MinBudget;
    ULONG MaxBudget;
    WCHAR ModelNumber[PMI_NAME_MAX];
    WCHAR SerialNumber[PMI_NAME_MAX];
    WCHAR OEMInformation[PMI_NAME_MAX];
} PMI_REPORTED_CAPABILITIES, *PPMI_REPORTED_CAPABILITIES;

typedef struct _PMI_METERED_HARDWARE_INFORMATION
{
    ULONG MeteredHardwareCount;
    WCHAR MeteredHardware[ANYSIZE_ARRAY];
} PMI_METERED_HARDWARE_INFORMATION, *PPMI_METERED_HARDWARE_INFORMATION;

typedef struct _PMI_CAPABILITIES
{
    ULONG Version;
    ULONG Size;
    PMI_CAPABILITIES_TYPE CapabilityType;
    union
    {
        PMI_REPORTED_CAPABILITIES ReportedCapabilities;
        PMI_METERED_HARDWARE_INFORMATION MeteredHardwareInformation;
    } Capabilities;
} PMI_CAPABILITIES, *PPMI_CAPABILITIES;

typedef struct _PMI_MEASUREMENT_CONFIGURATION
{
    ULONG AveragingInterval;
} PMI_MEASUREMENT_CONFIGURATION, *PPMI_MEASUREMENT_CONFIGURATION;

typedef struct _PMI_BUDGETING_CONFIGURATION
{
    ULONG ConfiguredBudget;
} PMI_BUDGETING_CONFIGURATION, *PPMI_BUDGETING_CONFIGURATION;

typedef struct _PMI_THRESHOLD_CONFIGURATION
{
    ULONG LowerThreshold;
    ULONG UpperThreshold;
} PMI_THRESHOLD_CONFIGURATION, *PPMI_THRESHOLD_CONFIGURATION;

typedef struct _PMI_CONFIGURATION
{
    ULONG Version;
    USHORT Size;
    PMI_CONFIGURATION_TYPE ConfigurationType;
    union
    {
        PMI_MEASUREMENT_CONFIGURATION MeasurementConfiguration;
        PMI_BUDGETING_CONFIGURATION BudgetingConfiguration;
        PMI_THRESHOLD_CONFIGURATION ThresholdConfiguration;
    } Configuration;
} PMI_CONFIGURATION, *PPMI_CONFIGURATION;

typedef struct _PMI_MEASUREMENT_DATA
{
    ULONG Version;
    ULONG CurrentPower;
} PMI_MEASUREMENT_DATA, *PPMI_MEASUREMENT_DATA;

typedef struct _PMI_EVENT
{
    ULONG Version;
    PMI_EVENT_TYPE EventType;
} PMI_EVENT, *PPMI_EVENT;

#endif /* _PMI_ */
