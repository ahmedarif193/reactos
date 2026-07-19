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

NTSTATUS
HidClassPtpValidateCapabilities(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_GET_FEATURE GetFeature,
    _In_ PVOID Context,
    _Out_ PHIDCLASS_PTP_CAPABILITIES Capabilities);

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
