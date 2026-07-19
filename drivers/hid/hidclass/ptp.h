/*
 * PROJECT:     ReactOS HID Class Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows Precision Touchpad capability and configuration policy
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _HIDCLASS_PTP_H_
#define _HIDCLASS_PTP_H_

#define HIDCLASS_PTP_MOUSE_MODE               0
#define HIDCLASS_PTP_PRECISION_TOUCHPAD_MODE  3
#define HIDCLASS_PTP_HAPTIC_NONE_ORDINAL      1
#define HIDCLASS_PTP_HAPTIC_STOP_ORDINAL      2

typedef struct _HIDCLASS_PTP_CAPABILITIES
{
    BOOLEAN Present;
    BOOLEAN Valid;
    BOOLEAN HasButtonType;
    BOOLEAN CertificationReportValid;
    UCHAR MaximumContactCount;
    UCHAR ButtonType;
    UCHAR CollectionNumber;
    UCHAR CapabilitiesReportId;
    UCHAR CertificationReportId;
} HIDCLASS_PTP_CAPABILITIES, *PHIDCLASS_PTP_CAPABILITIES;

typedef struct _HIDCLASS_PTP_CONFIGURATION
{
    BOOLEAN Valid;
    BOOLEAN DeviceModeKnown;
    BOOLEAN SelectiveReportingKnown;
    BOOLEAN SurfaceReportingEnabled;
    BOOLEAN ButtonReportingEnabled;
    UCHAR DeviceMode;
    UCHAR CollectionNumber;
    UCHAR DeviceModeReportId;
    UCHAR SelectiveReportingReportId;
    USHORT DeviceModeLinkCollection;
    USHORT SurfaceSwitchLinkCollection;
    USHORT ButtonSwitchLinkCollection;
    USHORT DeviceModeReportLength;
    USHORT SelectiveReportingReportLength;
} HIDCLASS_PTP_CONFIGURATION, *PHIDCLASS_PTP_CONFIGURATION;

typedef struct _HIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY
{
    HIDP_VALUE_CAPS ValueCaps;
    USHORT ReportLength;
} HIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY,
  *PHIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY;

typedef struct _HIDCLASS_PTP_HAPTIC_LIST_CAPABILITY
{
    UCHAR ReportId;
    USHORT LinkCollection;
    USHORT ReportLength;
    USHORT BitSize;
    USHORT ReportCount;
    USAGE UsageMinimum;
    USAGE UsageMaximum;
    LONG LogicalMinimum;
    LONG LogicalMaximum;
    LONG PhysicalMinimum;
    LONG PhysicalMaximum;
    ULONG Units;
    ULONG UnitsExponent;
} HIDCLASS_PTP_HAPTIC_LIST_CAPABILITY,
  *PHIDCLASS_PTP_HAPTIC_LIST_CAPABILITY;

typedef struct _HIDCLASS_PTP_HAPTICS_CAPABILITIES
{
    BOOLEAN Present;
    BOOLEAN Valid;
    BOOLEAN HasButtonPressThreshold;
    BOOLEAN HasDeviceIntensity;
    BOOLEAN HasHostInitiated;
    BOOLEAN HasRepeatControl;
    UCHAR CollectionNumber;
    HIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY ButtonPressThreshold;
    HIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY DeviceIntensity;
    HIDCLASS_PTP_HAPTIC_LIST_CAPABILITY WaveformList;
    HIDCLASS_PTP_HAPTIC_LIST_CAPABILITY DurationList;
    HIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY ManualTrigger;
    HIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY HostIntensity;
    HIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY RepeatCount;
    HIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY RetriggerPeriod;
    HIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY WaveformCutoffTime;
} HIDCLASS_PTP_HAPTICS_CAPABILITIES,
  *PHIDCLASS_PTP_HAPTICS_CAPABILITIES;

typedef struct _HIDCLASS_PTP_HAPTIC_WAVEFORM
{
    ULONG Ordinal;
    ULONG WaveformUsage;
    ULONG DurationMilliseconds;
} HIDCLASS_PTP_HAPTIC_WAVEFORM,
  *PHIDCLASS_PTP_HAPTIC_WAVEFORM;

typedef struct _HIDCLASS_PTP_HAPTIC_OUTPUT
{
    ULONG WaveformOrdinal;
    ULONG Intensity;
    ULONG RepeatCount;
    ULONG RetriggerPeriodMilliseconds;
    ULONG WaveformCutoffTimeMilliseconds;
} HIDCLASS_PTP_HAPTIC_OUTPUT,
  *PHIDCLASS_PTP_HAPTIC_OUTPUT;

typedef
NTSTATUS
(NTAPI *PHIDCLASS_PTP_GET_FEATURE)(
    _In_ PVOID Context,
    _In_ UCHAR ReportId,
    _Inout_updates_bytes_(ReportLength) PUCHAR ReportBuffer,
    _In_ ULONG ReportLength);

typedef
NTSTATUS
(NTAPI *PHIDCLASS_PTP_SET_FEATURE)(
    _In_ PVOID Context,
    _In_ UCHAR ReportId,
    _In_reads_bytes_(ReportLength) PUCHAR ReportBuffer,
    _In_ ULONG ReportLength);

typedef
NTSTATUS
(NTAPI *PHIDCLASS_PTP_WRITE_OUTPUT)(
    _In_ PVOID Context,
    _In_ UCHAR ReportId,
    _In_reads_bytes_(ReportLength) PUCHAR ReportBuffer,
    _In_ ULONG ReportLength);

NTSTATUS
HidClassPtpValidateCapabilities(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_GET_FEATURE GetFeature,
    _In_ PVOID Context,
    _Out_ PHIDCLASS_PTP_CAPABILITIES Capabilities);

NTSTATUS
HidClassPtpDiscoverHaptics(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _Out_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities);

NTSTATUS
HidClassPtpSetHapticButtonPressThreshold(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities,
    _In_ ULONG Threshold,
    _In_ PHIDCLASS_PTP_SET_FEATURE SetFeature,
    _In_ PVOID Context);

NTSTATUS
HidClassPtpSetDeviceHapticIntensity(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities,
    _In_ ULONG Intensity,
    _In_ PHIDCLASS_PTP_SET_FEATURE SetFeature,
    _In_ PVOID Context);

NTSTATUS
HidClassPtpGetHapticWaveforms(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities,
    _In_ PHIDCLASS_PTP_GET_FEATURE GetFeature,
    _In_ PVOID Context,
    _Out_writes_to_opt_(WaveformCapacity, *WaveformCount) PHIDCLASS_PTP_HAPTIC_WAVEFORM Waveforms,
    _In_ ULONG WaveformCapacity,
    _Out_ PULONG WaveformCount);

NTSTATUS
HidClassPtpSendHapticOutput(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities,
    _In_ PHIDCLASS_PTP_HAPTIC_OUTPUT Output,
    _In_ PHIDCLASS_PTP_WRITE_OUTPUT WriteOutput,
    _In_ PVOID Context);

NTSTATUS
HidClassPtpInitializeConfiguration(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_CAPABILITIES Capabilities,
    _Out_ PHIDCLASS_PTP_CONFIGURATION Configuration);

NTSTATUS
HidClassPtpSetDeviceMode(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _Inout_ PHIDCLASS_PTP_CONFIGURATION Configuration,
    _In_ UCHAR DeviceMode,
    _In_ PHIDCLASS_PTP_SET_FEATURE SetFeature,
    _In_ PVOID Context);

NTSTATUS
HidClassPtpSetSelectiveReporting(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _Inout_ PHIDCLASS_PTP_CONFIGURATION Configuration,
    _In_ BOOLEAN EnableSurfaceReporting,
    _In_ BOOLEAN EnableButtonReporting,
    _In_ PHIDCLASS_PTP_SET_FEATURE SetFeature,
    _In_ PVOID Context);

#endif /* _HIDCLASS_PTP_H_ */
