/*
 * PROJECT:     ReactOS HID Class Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows Precision Touchpad capability validation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _HIDCLASS_PTP_H_
#define _HIDCLASS_PTP_H_

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

typedef
NTSTATUS
(NTAPI *PHIDCLASS_PTP_GET_FEATURE)(
    _In_ PVOID Context,
    _In_ UCHAR ReportId,
    _Inout_updates_bytes_(ReportLength) PUCHAR ReportBuffer,
    _In_ ULONG ReportLength);

NTSTATUS
HidClassPtpValidateCapabilities(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_GET_FEATURE GetFeature,
    _In_ PVOID Context,
    _Out_ PHIDCLASS_PTP_CAPABILITIES Capabilities);

#endif /* _HIDCLASS_PTP_H_ */
