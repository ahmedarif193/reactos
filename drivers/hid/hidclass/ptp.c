/*
 * PROJECT:     ReactOS HID Class Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows Precision Touchpad capability and configuration policy
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

#define HIDCLASS_PTP_CERTIFICATION_USAGE_PAGE  0xFF00
#define HIDCLASS_PTP_CERTIFICATION_USAGE       0x00C5
#define HIDCLASS_PTP_CERTIFICATION_SIZE        256
#define HIDCLASS_PTP_MINIMUM_CONTACT_COUNT     3
#define HIDCLASS_PTP_MAXIMUM_CONTACT_COUNT     5

static
PHIDP_COLLECTION_DESC
HidClassPtpFindCollection(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ USAGE UsagePage,
    _In_ USAGE Usage)
{
    ULONG Index;

    for (Index = 0; Index < DeviceDescription->CollectionDescLength; Index++)
    {
        if (DeviceDescription->CollectionDesc[Index].UsagePage == UsagePage &&
            DeviceDescription->CollectionDesc[Index].Usage == Usage)
        {
            return &DeviceDescription->CollectionDesc[Index];
        }
    }

    return NULL;
}

static
PHIDP_REPORT_IDS
HidClassPtpFindFeatureReport(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ UCHAR CollectionNumber,
    _In_ UCHAR ReportId)
{
    ULONG Index;

    for (Index = 0; Index < DeviceDescription->ReportIDsLength; Index++)
    {
        if (DeviceDescription->ReportIDs[Index].CollectionNumber == CollectionNumber &&
            DeviceDescription->ReportIDs[Index].ReportID == ReportId &&
            DeviceDescription->ReportIDs[Index].FeatureLength != 0)
        {
            return &DeviceDescription->ReportIDs[Index];
        }
    }

    return NULL;
}

static
BOOLEAN
HidClassPtpIsScalarCapability(
    _In_ PHIDP_VALUE_CAPS ValueCaps)
{
    return !ValueCaps->IsRange &&
           ValueCaps->ReportCount == 1 &&
           ValueCaps->BitSize != 0 &&
           ValueCaps->BitSize <= sizeof(ULONG) * 8 &&
           ValueCaps->LogicalMin >= 0 &&
           ValueCaps->LogicalMax >= ValueCaps->LogicalMin;
}

static
NTSTATUS
HidClassPtpReadFeature(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDP_COLLECTION_DESC Collection,
    _In_ UCHAR ReportId,
    _In_ PHIDCLASS_PTP_GET_FEATURE GetFeature,
    _In_ PVOID Context,
    _Outptr_result_bytebuffer_(*ReportLength) PUCHAR *ReportBuffer,
    _Out_ PULONG ReportLength)
{
    PHIDP_REPORT_IDS ReportDescription;
    PUCHAR Buffer;
    NTSTATUS Status;

    *ReportBuffer = NULL;
    *ReportLength = 0;

    ReportDescription = HidClassPtpFindFeatureReport(DeviceDescription,
                                                      Collection->CollectionNumber,
                                                      ReportId);
    if (ReportDescription == NULL || ReportDescription->FeatureLength < 1)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   ReportDescription->FeatureLength,
                                   HIDCLASS_TAG);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Buffer, ReportDescription->FeatureLength);
    Buffer[0] = ReportId;
    Status = GetFeature(Context,
                        ReportId,
                        Buffer,
                        ReportDescription->FeatureLength);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, HIDCLASS_TAG);
        return Status;
    }

    if (Buffer[0] != ReportId)
    {
        ExFreePoolWithTag(Buffer, HIDCLASS_TAG);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    *ReportBuffer = Buffer;
    *ReportLength = ReportDescription->FeatureLength;
    return STATUS_SUCCESS;
}

NTSTATUS
HidClassPtpValidateCapabilities(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_GET_FEATURE GetFeature,
    _In_ PVOID Context,
    _Out_ PHIDCLASS_PTP_CAPABILITIES Capabilities)
{
    PHIDP_COLLECTION_DESC Collection;
    PHIDP_REPORT_IDS CertificationReportDescription;
    HIDP_VALUE_CAPS ContactCountCaps;
    HIDP_VALUE_CAPS ButtonTypeCaps;
    HIDP_VALUE_CAPS CertificationCaps;
    PUCHAR CapabilitiesReport = NULL;
    PUCHAR ButtonTypeReport = NULL;
    PUCHAR CertificationReport = NULL;
    PUCHAR ButtonTypeReportToParse;
    UCHAR CertificationValue[HIDCLASS_PTP_CERTIFICATION_SIZE];
    ULONG CapabilitiesReportLength = 0;
    ULONG ButtonTypeReportLength = 0;
    ULONG CertificationReportLength = 0;
    ULONG ButtonTypeReportLengthToParse;
    ULONG Value;
    USHORT CapsLength;
    NTSTATUS Status;

    if (DeviceDescription == NULL || GetFeature == NULL || Capabilities == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Capabilities, sizeof(*Capabilities));

    Collection = HidClassPtpFindCollection(DeviceDescription,
                                            HID_USAGE_PAGE_DIGITIZER,
                                            HID_USAGE_DIGITIZER_TOUCH_PAD);
    if (Collection == NULL)
        return STATUS_NOT_FOUND;

    Capabilities->Present = TRUE;
    Capabilities->CollectionNumber = Collection->CollectionNumber;

    CapsLength = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Feature,
                                       HID_USAGE_PAGE_DIGITIZER,
                                       HIDP_LINK_COLLECTION_UNSPECIFIED,
                                       HID_USAGE_DIGITIZER_CONTACT_COUNT_MAXIMUM,
                                       &ContactCountCaps,
                                       &CapsLength,
                                       Collection->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS || CapsLength != 1 ||
        !HidClassPtpIsScalarCapability(&ContactCountCaps))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    Status = HidClassPtpReadFeature(DeviceDescription,
                                    Collection,
                                    ContactCountCaps.ReportID,
                                    GetFeature,
                                    Context,
                                    &CapabilitiesReport,
                                    &CapabilitiesReportLength);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Value = 0;
    Status = HidP_GetUsageValue(HidP_Feature,
                                HID_USAGE_PAGE_DIGITIZER,
                                ContactCountCaps.LinkCollection,
                                HID_USAGE_DIGITIZER_CONTACT_COUNT_MAXIMUM,
                                &Value,
                                Collection->PreparsedData,
                                (PCHAR)CapabilitiesReport,
                                CapabilitiesReportLength);
    if (Status != HIDP_STATUS_SUCCESS ||
        Value < HIDCLASS_PTP_MINIMUM_CONTACT_COUNT ||
        Value > HIDCLASS_PTP_MAXIMUM_CONTACT_COUNT ||
        Value < (ULONG)ContactCountCaps.LogicalMin ||
        Value > (ULONG)ContactCountCaps.LogicalMax)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    Capabilities->MaximumContactCount = (UCHAR)Value;
    Capabilities->CapabilitiesReportId = ContactCountCaps.ReportID;

    CapsLength = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Feature,
                                       HID_USAGE_PAGE_DIGITIZER,
                                       HIDP_LINK_COLLECTION_UNSPECIFIED,
                                       HID_USAGE_DIGITIZER_PAD_TYPE,
                                       &ButtonTypeCaps,
                                       &CapsLength,
                                       Collection->PreparsedData);
    if (Status == HIDP_STATUS_SUCCESS)
    {
        if (CapsLength != 1 || !HidClassPtpIsScalarCapability(&ButtonTypeCaps))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        if (ButtonTypeCaps.ReportID == ContactCountCaps.ReportID)
        {
            ButtonTypeReportToParse = CapabilitiesReport;
            ButtonTypeReportLengthToParse = CapabilitiesReportLength;
        }
        else
        {
            Status = HidClassPtpReadFeature(DeviceDescription,
                                            Collection,
                                            ButtonTypeCaps.ReportID,
                                            GetFeature,
                                            Context,
                                            &ButtonTypeReport,
                                            &ButtonTypeReportLength);
            if (!NT_SUCCESS(Status))
                goto Cleanup;

            ButtonTypeReportToParse = ButtonTypeReport;
            ButtonTypeReportLengthToParse = ButtonTypeReportLength;
        }

        Value = 0;
        Status = HidP_GetUsageValue(HidP_Feature,
                                    HID_USAGE_PAGE_DIGITIZER,
                                    ButtonTypeCaps.LinkCollection,
                                    HID_USAGE_DIGITIZER_PAD_TYPE,
                                    &Value,
                                    Collection->PreparsedData,
                                    (PCHAR)ButtonTypeReportToParse,
                                    ButtonTypeReportLengthToParse);
        if (Status != HIDP_STATUS_SUCCESS || Value > 2 ||
            Value < (ULONG)ButtonTypeCaps.LogicalMin ||
            Value > (ULONG)ButtonTypeCaps.LogicalMax)
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        Capabilities->HasButtonType = TRUE;
        Capabilities->ButtonType = (UCHAR)Value;
    }
    else if (Status != HIDP_STATUS_USAGE_NOT_FOUND)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    CapsLength = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Feature,
                                       HIDCLASS_PTP_CERTIFICATION_USAGE_PAGE,
                                       HIDP_LINK_COLLECTION_UNSPECIFIED,
                                       HIDCLASS_PTP_CERTIFICATION_USAGE,
                                       &CertificationCaps,
                                       &CapsLength,
                                       Collection->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS || CapsLength != 1 ||
        CertificationCaps.IsRange || CertificationCaps.BitSize != 8 ||
        CertificationCaps.ReportCount != HIDCLASS_PTP_CERTIFICATION_SIZE ||
        CertificationCaps.LogicalMin != 0 || CertificationCaps.LogicalMax != 0xFF)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    CertificationReportDescription = HidClassPtpFindFeatureReport(
                                         DeviceDescription,
                                         Collection->CollectionNumber,
                                         CertificationCaps.ReportID);
    if (CertificationReportDescription == NULL ||
        CertificationReportDescription->FeatureLength !=
            HIDCLASS_PTP_CERTIFICATION_SIZE + 1)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    Status = HidClassPtpReadFeature(DeviceDescription,
                                    Collection,
                                    CertificationCaps.ReportID,
                                    GetFeature,
                                    Context,
                                    &CertificationReport,
                                    &CertificationReportLength);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = HidP_GetUsageValueArray(HidP_Feature,
                                     HIDCLASS_PTP_CERTIFICATION_USAGE_PAGE,
                                     CertificationCaps.LinkCollection,
                                     HIDCLASS_PTP_CERTIFICATION_USAGE,
                                     (PCHAR)CertificationValue,
                                     sizeof(CertificationValue),
                                     Collection->PreparsedData,
                                     (PCHAR)CertificationReport,
                                     CertificationReportLength);
    if (Status != HIDP_STATUS_SUCCESS)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    Capabilities->CertificationReportId = CertificationCaps.ReportID;
    Capabilities->CertificationReportValid = TRUE;
    Capabilities->Valid = TRUE;
    Status = STATUS_SUCCESS;

Cleanup:
    if (CertificationReport != NULL)
        ExFreePoolWithTag(CertificationReport, HIDCLASS_TAG);
    if (ButtonTypeReport != NULL)
        ExFreePoolWithTag(ButtonTypeReport, HIDCLASS_TAG);
    if (CapabilitiesReport != NULL)
        ExFreePoolWithTag(CapabilitiesReport, HIDCLASS_TAG);

    return Status;
}

NTSTATUS
HidClassPtpInitializeConfiguration(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_CAPABILITIES Capabilities,
    _Out_ PHIDCLASS_PTP_CONFIGURATION Configuration)
{
    PHIDP_COLLECTION_DESC Collection;
    PHIDP_REPORT_IDS ModeReportDescription;
    PHIDP_REPORT_IDS SelectiveReportDescription;
    HIDP_VALUE_CAPS ModeCaps;
    HIDP_VALUE_CAPS SurfaceCaps;
    HIDP_VALUE_CAPS ButtonCaps;
    PUCHAR ModeReport = NULL;
    PUCHAR SelectiveReport = NULL;
    USHORT CapsLength;
    NTSTATUS Status;

    if (DeviceDescription == NULL || Capabilities == NULL || Configuration == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Configuration, sizeof(*Configuration));

    if (!Capabilities->Present || !Capabilities->Valid)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Collection = HidClassPtpFindCollection(DeviceDescription,
                                            HID_USAGE_PAGE_DIGITIZER,
                                            HID_USAGE_DIGITIZER_DEVICE_CONFIGURATION);
    if (Collection == NULL)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    CapsLength = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Feature,
                                       HID_USAGE_PAGE_DIGITIZER,
                                       HIDP_LINK_COLLECTION_UNSPECIFIED,
                                       HID_USAGE_DIGITIZER_DEVICE_MODE,
                                       &ModeCaps,
                                       &CapsLength,
                                       Collection->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS || CapsLength != 1 ||
        !HidClassPtpIsScalarCapability(&ModeCaps) ||
        ModeCaps.LogicalMin != 0 || ModeCaps.LogicalMax != 10)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    CapsLength = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Feature,
                                       HID_USAGE_PAGE_DIGITIZER,
                                       HIDP_LINK_COLLECTION_UNSPECIFIED,
                                       HID_USAGE_DIGITIZER_SURFACE_SWITCH,
                                       &SurfaceCaps,
                                       &CapsLength,
                                       Collection->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS || CapsLength != 1 ||
        !HidClassPtpIsScalarCapability(&SurfaceCaps) ||
        SurfaceCaps.LogicalMin != 0 || SurfaceCaps.LogicalMax != 1)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    CapsLength = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Feature,
                                       HID_USAGE_PAGE_DIGITIZER,
                                       HIDP_LINK_COLLECTION_UNSPECIFIED,
                                       HID_USAGE_DIGITIZER_BUTTON_SWITCH,
                                       &ButtonCaps,
                                       &CapsLength,
                                       Collection->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS || CapsLength != 1 ||
        !HidClassPtpIsScalarCapability(&ButtonCaps) ||
        ButtonCaps.LogicalMin != 0 || ButtonCaps.LogicalMax != 1 ||
        ButtonCaps.ReportID != SurfaceCaps.ReportID ||
        ModeCaps.ReportID == SurfaceCaps.ReportID)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ModeReportDescription = HidClassPtpFindFeatureReport(DeviceDescription,
                                                          Collection->CollectionNumber,
                                                          ModeCaps.ReportID);
    SelectiveReportDescription = HidClassPtpFindFeatureReport(DeviceDescription,
                                                               Collection->CollectionNumber,
                                                               SurfaceCaps.ReportID);
    if (ModeReportDescription == NULL || SelectiveReportDescription == NULL)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    ModeReport = ExAllocatePoolWithTag(NonPagedPool,
                                       ModeReportDescription->FeatureLength,
                                       HIDCLASS_TAG);
    SelectiveReport = ExAllocatePoolWithTag(NonPagedPool,
                                            SelectiveReportDescription->FeatureLength,
                                            HIDCLASS_TAG);
    if (ModeReport == NULL || SelectiveReport == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    RtlZeroMemory(ModeReport, ModeReportDescription->FeatureLength);
    ModeReport[0] = ModeCaps.ReportID;
    Status = HidP_SetUsageValue(HidP_Feature,
                                HID_USAGE_PAGE_DIGITIZER,
                                ModeCaps.LinkCollection,
                                HID_USAGE_DIGITIZER_DEVICE_MODE,
                                3,
                                Collection->PreparsedData,
                                (PCHAR)ModeReport,
                                ModeReportDescription->FeatureLength);
    if (Status != HIDP_STATUS_SUCCESS)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    RtlZeroMemory(SelectiveReport, SelectiveReportDescription->FeatureLength);
    SelectiveReport[0] = SurfaceCaps.ReportID;
    Status = HidP_SetUsageValue(HidP_Feature,
                                HID_USAGE_PAGE_DIGITIZER,
                                SurfaceCaps.LinkCollection,
                                HID_USAGE_DIGITIZER_SURFACE_SWITCH,
                                1,
                                Collection->PreparsedData,
                                (PCHAR)SelectiveReport,
                                SelectiveReportDescription->FeatureLength);
    if (Status != HIDP_STATUS_SUCCESS)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    Status = HidP_SetUsageValue(HidP_Feature,
                                HID_USAGE_PAGE_DIGITIZER,
                                ButtonCaps.LinkCollection,
                                HID_USAGE_DIGITIZER_BUTTON_SWITCH,
                                1,
                                Collection->PreparsedData,
                                (PCHAR)SelectiveReport,
                                SelectiveReportDescription->FeatureLength);
    if (Status != HIDP_STATUS_SUCCESS)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    Configuration->Valid = TRUE;
    Configuration->CollectionNumber = Collection->CollectionNumber;
    Configuration->DeviceModeReportId = ModeCaps.ReportID;
    Configuration->SelectiveReportingReportId = SurfaceCaps.ReportID;
    Configuration->DeviceModeLinkCollection = ModeCaps.LinkCollection;
    Configuration->SurfaceSwitchLinkCollection = SurfaceCaps.LinkCollection;
    Configuration->ButtonSwitchLinkCollection = ButtonCaps.LinkCollection;
    Configuration->DeviceModeReportLength = ModeReportDescription->FeatureLength;
    Configuration->SelectiveReportingReportLength = SelectiveReportDescription->FeatureLength;
    Status = STATUS_SUCCESS;

Cleanup:
    if (SelectiveReport != NULL)
        ExFreePoolWithTag(SelectiveReport, HIDCLASS_TAG);
    if (ModeReport != NULL)
        ExFreePoolWithTag(ModeReport, HIDCLASS_TAG);
    return Status;
}

NTSTATUS
HidClassPtpSetDeviceMode(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _Inout_ PHIDCLASS_PTP_CONFIGURATION Configuration,
    _In_ UCHAR DeviceMode,
    _In_ PHIDCLASS_PTP_SET_FEATURE SetFeature,
    _In_ PVOID Context)
{
    PHIDP_COLLECTION_DESC Collection;
    PHIDP_REPORT_IDS ReportDescription;
    PUCHAR ReportBuffer;
    NTSTATUS Status;

    if (DeviceDescription == NULL || Configuration == NULL || SetFeature == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!Configuration->Valid)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    if (DeviceMode != HIDCLASS_PTP_MOUSE_MODE &&
        DeviceMode != HIDCLASS_PTP_PRECISION_TOUCHPAD_MODE)
        return STATUS_INVALID_PARAMETER;

    Collection = HidClassPtpFindCollection(DeviceDescription,
                                            HID_USAGE_PAGE_DIGITIZER,
                                            HID_USAGE_DIGITIZER_DEVICE_CONFIGURATION);
    if (Collection == NULL || Collection->CollectionNumber != Configuration->CollectionNumber)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    ReportDescription = HidClassPtpFindFeatureReport(DeviceDescription,
                                                      Collection->CollectionNumber,
                                                      Configuration->DeviceModeReportId);
    if (ReportDescription == NULL ||
        ReportDescription->FeatureLength != Configuration->DeviceModeReportLength)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ReportBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                         ReportDescription->FeatureLength,
                                         HIDCLASS_TAG);
    if (ReportBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(ReportBuffer, ReportDescription->FeatureLength);
    ReportBuffer[0] = Configuration->DeviceModeReportId;
    Status = HidP_SetUsageValue(HidP_Feature,
                                HID_USAGE_PAGE_DIGITIZER,
                                Configuration->DeviceModeLinkCollection,
                                HID_USAGE_DIGITIZER_DEVICE_MODE,
                                DeviceMode,
                                Collection->PreparsedData,
                                (PCHAR)ReportBuffer,
                                ReportDescription->FeatureLength);
    if (Status == HIDP_STATUS_SUCCESS)
    {
        Status = SetFeature(Context,
                            Configuration->DeviceModeReportId,
                            ReportBuffer,
                            ReportDescription->FeatureLength);
        if (NT_SUCCESS(Status))
        {
            Configuration->DeviceMode = DeviceMode;
            Configuration->DeviceModeKnown = TRUE;
        }
        else
        {
            Configuration->DeviceModeKnown = FALSE;
        }
    }
    else
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ExFreePoolWithTag(ReportBuffer, HIDCLASS_TAG);
    return Status;
}

NTSTATUS
HidClassPtpSetSelectiveReporting(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _Inout_ PHIDCLASS_PTP_CONFIGURATION Configuration,
    _In_ BOOLEAN EnableSurfaceReporting,
    _In_ BOOLEAN EnableButtonReporting,
    _In_ PHIDCLASS_PTP_SET_FEATURE SetFeature,
    _In_ PVOID Context)
{
    PHIDP_COLLECTION_DESC Collection;
    PHIDP_REPORT_IDS ReportDescription;
    PUCHAR ReportBuffer;
    NTSTATUS Status;

    if (DeviceDescription == NULL || Configuration == NULL || SetFeature == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!Configuration->Valid)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Collection = HidClassPtpFindCollection(DeviceDescription,
                                            HID_USAGE_PAGE_DIGITIZER,
                                            HID_USAGE_DIGITIZER_DEVICE_CONFIGURATION);
    if (Collection == NULL || Collection->CollectionNumber != Configuration->CollectionNumber)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    ReportDescription = HidClassPtpFindFeatureReport(DeviceDescription,
                                                      Collection->CollectionNumber,
                                                      Configuration->SelectiveReportingReportId);
    if (ReportDescription == NULL ||
        ReportDescription->FeatureLength != Configuration->SelectiveReportingReportLength)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ReportBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                         ReportDescription->FeatureLength,
                                         HIDCLASS_TAG);
    if (ReportBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(ReportBuffer, ReportDescription->FeatureLength);
    ReportBuffer[0] = Configuration->SelectiveReportingReportId;
    Status = HidP_SetUsageValue(HidP_Feature,
                                HID_USAGE_PAGE_DIGITIZER,
                                Configuration->SurfaceSwitchLinkCollection,
                                HID_USAGE_DIGITIZER_SURFACE_SWITCH,
                                EnableSurfaceReporting ? 1 : 0,
                                Collection->PreparsedData,
                                (PCHAR)ReportBuffer,
                                ReportDescription->FeatureLength);
    if (Status != HIDP_STATUS_SUCCESS)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    Status = HidP_SetUsageValue(HidP_Feature,
                                HID_USAGE_PAGE_DIGITIZER,
                                Configuration->ButtonSwitchLinkCollection,
                                HID_USAGE_DIGITIZER_BUTTON_SWITCH,
                                EnableButtonReporting ? 1 : 0,
                                Collection->PreparsedData,
                                (PCHAR)ReportBuffer,
                                ReportDescription->FeatureLength);
    if (Status != HIDP_STATUS_SUCCESS)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    Status = SetFeature(Context,
                        Configuration->SelectiveReportingReportId,
                        ReportBuffer,
                        ReportDescription->FeatureLength);
    if (NT_SUCCESS(Status))
    {
        Configuration->SelectiveReportingKnown = TRUE;
        Configuration->SurfaceReportingEnabled = EnableSurfaceReporting;
        Configuration->ButtonReportingEnabled = EnableButtonReporting;
    }
    else
    {
        Configuration->SelectiveReportingKnown = FALSE;
    }

Cleanup:
    ExFreePoolWithTag(ReportBuffer, HIDCLASS_TAG);
    return Status;
}
