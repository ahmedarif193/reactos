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
#define HIDCLASS_PTP_COLLECTION_LOGICAL        2
#define HIDCLASS_PTP_MILLISECOND_UNIT           0x1001
#define HIDCLASS_PTP_MILLISECOND_UNIT_EXPONENT  0x0D

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
PHIDP_REPORT_IDS
HidClassPtpFindOutputReport(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ UCHAR CollectionNumber,
    _In_ UCHAR ReportId)
{
    ULONG Index;

    for (Index = 0; Index < DeviceDescription->ReportIDsLength; Index++)
    {
        if (DeviceDescription->ReportIDs[Index].CollectionNumber == CollectionNumber &&
            DeviceDescription->ReportIDs[Index].ReportID == ReportId &&
            DeviceDescription->ReportIDs[Index].OutputLength != 0)
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
HidClassPtpGetValueCapabilities(
    _In_ PHIDP_COLLECTION_DESC Collection,
    _In_ HIDP_REPORT_TYPE ReportType,
    _In_ USHORT ExpectedCount,
    _Out_ PHIDP_VALUE_CAPS *ValueCaps)
{
    PHIDP_VALUE_CAPS Buffer;
    USHORT Count;
    NTSTATUS Status;

    *ValueCaps = NULL;
    if (ExpectedCount == 0)
        return STATUS_SUCCESS;

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   ExpectedCount * sizeof(*Buffer),
                                   HIDCLASS_TAG);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Count = ExpectedCount;
    Status = HidP_GetSpecificValueCaps(ReportType,
                                       HID_USAGE_PAGE_UNDEFINED,
                                       HIDP_LINK_COLLECTION_UNSPECIFIED,
                                       HID_USAGE_PAGE_UNDEFINED,
                                       Buffer,
                                       &Count,
                                       Collection->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS || Count != ExpectedCount)
    {
        ExFreePoolWithTag(Buffer, HIDCLASS_TAG);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    *ValueCaps = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HidClassPtpGetButtonCapabilities(
    _In_ PHIDP_COLLECTION_DESC Collection,
    _In_ HIDP_REPORT_TYPE ReportType,
    _In_ USHORT ExpectedCount,
    _Out_ PHIDP_BUTTON_CAPS *ButtonCaps)
{
    PHIDP_BUTTON_CAPS Buffer;
    USHORT Count;
    NTSTATUS Status;

    *ButtonCaps = NULL;
    if (ExpectedCount == 0)
        return STATUS_SUCCESS;

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   ExpectedCount * sizeof(*Buffer),
                                   HIDCLASS_TAG);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Count = ExpectedCount;
    Status = HidP_GetSpecificButtonCaps(ReportType,
                                        HID_USAGE_PAGE_UNDEFINED,
                                        HIDP_LINK_COLLECTION_UNSPECIFIED,
                                        HID_USAGE_PAGE_UNDEFINED,
                                        Buffer,
                                        &Count,
                                        Collection->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS || Count != ExpectedCount)
    {
        ExFreePoolWithTag(Buffer, HIDCLASS_TAG);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    *ButtonCaps = Buffer;
    return STATUS_SUCCESS;
}

static
BOOLEAN
HidClassPtpCapabilityContainsUsage(
    _In_ PHIDP_VALUE_CAPS ValueCaps,
    _In_ USAGE UsagePage,
    _In_ USAGE Usage)
{
    if (ValueCaps->UsagePage != UsagePage)
        return FALSE;

    if (ValueCaps->IsRange)
    {
        return Usage >= ValueCaps->Range.UsageMin &&
               Usage <= ValueCaps->Range.UsageMax;
    }

    return ValueCaps->NotRange.Usage == Usage;
}

static
PHIDP_VALUE_CAPS
HidClassPtpFindValueCapability(
    _In_reads_(ValueCapsCount) PHIDP_VALUE_CAPS ValueCaps,
    _In_ USHORT ValueCapsCount,
    _In_ USAGE UsagePage,
    _In_ USAGE Usage,
    _Out_ PULONG MatchCount)
{
    PHIDP_VALUE_CAPS Match = NULL;
    ULONG Index;

    *MatchCount = 0;
    for (Index = 0; Index < ValueCapsCount; Index++)
    {
        if (HidClassPtpCapabilityContainsUsage(&ValueCaps[Index],
                                               UsagePage,
                                               Usage))
        {
            if (Match == NULL)
                Match = &ValueCaps[Index];
            (*MatchCount)++;
        }
    }

    return Match;
}

static
PHIDP_VALUE_CAPS
HidClassPtpFindLinkedValueCapability(
    _In_reads_(ValueCapsCount) PHIDP_VALUE_CAPS ValueCaps,
    _In_ USHORT ValueCapsCount,
    _In_ USAGE UsagePage,
    _In_ USAGE LinkUsagePage,
    _In_ USAGE LinkUsage,
    _Out_ PULONG MatchCount)
{
    PHIDP_VALUE_CAPS Match = NULL;
    ULONG Index;

    *MatchCount = 0;
    for (Index = 0; Index < ValueCapsCount; Index++)
    {
        if (ValueCaps[Index].UsagePage == UsagePage &&
            ValueCaps[Index].LinkUsagePage == LinkUsagePage &&
            ValueCaps[Index].LinkUsage == LinkUsage)
        {
            if (Match == NULL)
                Match = &ValueCaps[Index];
            (*MatchCount)++;
        }
    }

    return Match;
}

static
BOOLEAN
HidClassPtpReportHasExactCapabilities(
    _In_reads_(ValueCapsCount) PHIDP_VALUE_CAPS ValueCaps,
    _In_ USHORT ValueCapsCount,
    _In_reads_(ButtonCapsCount) PHIDP_BUTTON_CAPS ButtonCaps,
    _In_ USHORT ButtonCapsCount,
    _In_ UCHAR ReportId,
    _In_ ULONG ExpectedValueCaps)
{
    ULONG ValueCount = 0;
    ULONG ButtonCount = 0;
    ULONG Index;

    for (Index = 0; Index < ValueCapsCount; Index++)
    {
        if (ValueCaps[Index].ReportID == ReportId)
            ValueCount++;
    }

    for (Index = 0; Index < ButtonCapsCount; Index++)
    {
        if (ButtonCaps[Index].ReportID == ReportId)
            ButtonCount++;
    }

    return ValueCount == ExpectedValueCaps && ButtonCount == 0;
}

static
BOOLEAN
HidClassPtpIsLogicalCollection(
    _In_reads_(NodeCount) PHIDP_LINK_COLLECTION_NODE Nodes,
    _In_ ULONG NodeCount,
    _In_ USHORT LinkCollection,
    _In_ USAGE UsagePage,
    _In_ USAGE Usage)
{
    return LinkCollection < NodeCount &&
           Nodes[LinkCollection].CollectionType == HIDCLASS_PTP_COLLECTION_LOGICAL &&
           Nodes[LinkCollection].LinkUsagePage == UsagePage &&
           Nodes[LinkCollection].LinkUsage == Usage;
}

static
BOOLEAN
HidClassPtpIsScalarHapticCapability(
    _In_ PHIDP_VALUE_CAPS ValueCaps)
{
    return HidClassPtpIsScalarCapability(ValueCaps) &&
           ValueCaps->IsAbsolute &&
           ValueCaps->ReportID != 0;
}

static
BOOLEAN
HidClassPtpNormalizeListCapabilities(
    _In_reads_(ValueCapsCount) PHIDP_VALUE_CAPS ValueCaps,
    _In_ USHORT ValueCapsCount,
    _In_ USAGE LinkUsage,
    _Out_ PHIDCLASS_PTP_HAPTIC_LIST_CAPABILITY ListCapability)
{
    PHIDP_VALUE_CAPS First = NULL;
    USAGE UsageMinimum = MAXUSHORT;
    USAGE UsageMaximum = 0;
    USAGE ItemUsageMinimum;
    USAGE ItemUsageMaximum;
    ULONG TotalUsageCount = 0;
    ULONG ItemUsageCount;
    ULONG Index;

    RtlZeroMemory(ListCapability, sizeof(*ListCapability));

    for (Index = 0; Index < ValueCapsCount; Index++)
    {
        if (ValueCaps[Index].UsagePage != HID_USAGE_PAGE_ORDINAL ||
            ValueCaps[Index].LinkUsagePage != HID_USAGE_PAGE_HAPTICS ||
            ValueCaps[Index].LinkUsage != LinkUsage)
        {
            continue;
        }

        if (First == NULL)
        {
            First = &ValueCaps[Index];
        }

        if (!ValueCaps[Index].IsAbsolute || ValueCaps[Index].ReportID == 0 ||
            ValueCaps[Index].BitSize == 0 ||
            ValueCaps[Index].BitSize > sizeof(ULONG) * 8 ||
            ValueCaps[Index].ReportCount == 0 ||
            ValueCaps[Index].LogicalMin < 0 ||
            ValueCaps[Index].LogicalMax < ValueCaps[Index].LogicalMin ||
            ValueCaps[Index].ReportID != First->ReportID ||
            ValueCaps[Index].LinkCollection != First->LinkCollection ||
            ValueCaps[Index].BitSize != First->BitSize ||
            ValueCaps[Index].LogicalMin != First->LogicalMin ||
            ValueCaps[Index].LogicalMax != First->LogicalMax ||
            ValueCaps[Index].PhysicalMin != First->PhysicalMin ||
            ValueCaps[Index].PhysicalMax != First->PhysicalMax ||
            ValueCaps[Index].Units != First->Units ||
            ValueCaps[Index].UnitsExp != First->UnitsExp)
        {
            return FALSE;
        }

        if (ValueCaps[Index].IsRange)
        {
            ItemUsageMinimum = ValueCaps[Index].Range.UsageMin;
            ItemUsageMaximum = ValueCaps[Index].Range.UsageMax;
        }
        else
        {
            ItemUsageMinimum = ValueCaps[Index].NotRange.Usage;
            ItemUsageMaximum = ItemUsageMinimum;
        }

        if (ItemUsageMinimum == 0 || ItemUsageMaximum < ItemUsageMinimum)
            return FALSE;

        if (TotalUsageCount == 0)
        {
            UsageMinimum = ItemUsageMinimum;
        }
        else if ((ULONG)UsageMaximum + 1 != ItemUsageMinimum)
        {
            return FALSE;
        }

        ItemUsageCount = (ULONG)ItemUsageMaximum - ItemUsageMinimum + 1;
        if (ItemUsageCount != ValueCaps[Index].ReportCount ||
            TotalUsageCount > MAXUSHORT - ItemUsageCount)
        {
            return FALSE;
        }

        TotalUsageCount += ItemUsageCount;
        UsageMaximum = ItemUsageMaximum;
    }

    if (First == NULL ||
        TotalUsageCount != (ULONG)UsageMaximum - UsageMinimum + 1)
    {
        return FALSE;
    }

    ListCapability->ReportId = First->ReportID;
    ListCapability->LinkCollection = First->LinkCollection;
    ListCapability->BitSize = First->BitSize;
    ListCapability->ReportCount = (USHORT)TotalUsageCount;
    ListCapability->UsageMinimum = UsageMinimum;
    ListCapability->UsageMaximum = UsageMaximum;
    ListCapability->LogicalMinimum = First->LogicalMin;
    ListCapability->LogicalMaximum = First->LogicalMax;
    ListCapability->PhysicalMinimum = First->PhysicalMin;
    ListCapability->PhysicalMaximum = First->PhysicalMax;
    ListCapability->Units = First->Units;
    ListCapability->UnitsExponent = First->UnitsExp;
    return TRUE;
}

static
BOOLEAN
HidClassPtpHasMillisecondPhysicalRange(
    _In_ PHIDP_VALUE_CAPS ValueCaps)
{
    if (ValueCaps->PhysicalMin == 0 && ValueCaps->PhysicalMax == 0)
        return TRUE;

    return ValueCaps->PhysicalMin == ValueCaps->LogicalMin &&
           ValueCaps->PhysicalMax == ValueCaps->LogicalMax &&
           ValueCaps->Units == HIDCLASS_PTP_MILLISECOND_UNIT &&
           ValueCaps->UnitsExp == HIDCLASS_PTP_MILLISECOND_UNIT_EXPONENT;
}

static
BOOLEAN
HidClassPtpListHasMillisecondPhysicalRange(
    _In_ PHIDCLASS_PTP_HAPTIC_LIST_CAPABILITY ListCapability)
{
    if (ListCapability->PhysicalMinimum == 0 &&
        ListCapability->PhysicalMaximum == 0)
    {
        return TRUE;
    }

    return ListCapability->PhysicalMinimum == ListCapability->LogicalMinimum &&
           ListCapability->PhysicalMaximum == ListCapability->LogicalMaximum &&
           ListCapability->Units == HIDCLASS_PTP_MILLISECOND_UNIT &&
           ListCapability->UnitsExponent ==
               HIDCLASS_PTP_MILLISECOND_UNIT_EXPONENT;
}

static
VOID
HidClassPtpStoreHapticCapability(
    _Out_ PHIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY Destination,
    _In_ PHIDP_VALUE_CAPS Source,
    _In_ USHORT ReportLength)
{
    Destination->ValueCaps = *Source;
    Destination->ReportLength = ReportLength;
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
HidClassPtpDiscoverHaptics(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _Out_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities)
{
    HIDCLASS_PTP_HAPTICS_CAPABILITIES Result;
    PHIDP_COLLECTION_DESC Collection;
    PHIDP_LINK_COLLECTION_NODE Nodes = NULL;
    PHIDP_VALUE_CAPS FeatureCaps = NULL;
    PHIDP_VALUE_CAPS OutputCaps = NULL;
    PHIDP_BUTTON_CAPS FeatureButtonCaps = NULL;
    PHIDP_BUTTON_CAPS OutputButtonCaps = NULL;
    PHIDP_VALUE_CAPS ButtonPressThreshold;
    PHIDP_VALUE_CAPS DeviceIntensity;
    PHIDP_VALUE_CAPS ManualTrigger;
    PHIDP_VALUE_CAPS HostIntensity;
    PHIDP_VALUE_CAPS RepeatCount;
    PHIDP_VALUE_CAPS RetriggerPeriod;
    PHIDP_VALUE_CAPS WaveformCutoffTime;
    PHIDP_REPORT_IDS ReportDescription;
    HIDP_CAPS Caps;
    ULONG ButtonPressThresholdCount;
    ULONG DeviceIntensityCount;
    ULONG WaveformListCount;
    ULONG DurationListCount;
    ULONG ManualTriggerCount;
    ULONG HostIntensityCount;
    ULONG RepeatCountCount;
    ULONG RetriggerPeriodCount;
    ULONG WaveformCutoffTimeCount;
    ULONG NodeCount;
    ULONG Index;
    ULONG HostCapabilityCount;
    ULONG OptionalControlCount;
    USHORT HostInformationController;
    USHORT ManualTriggerController;
    USHORT HostControllerParent;
    BOOLEAN ForbiddenUsagePresent = FALSE;
    NTSTATUS Status;

    if (DeviceDescription == NULL || HapticsCapabilities == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(HapticsCapabilities, sizeof(*HapticsCapabilities));
    RtlZeroMemory(&Result, sizeof(Result));

    Collection = HidClassPtpFindCollection(DeviceDescription,
                                            HID_USAGE_PAGE_DIGITIZER,
                                            HID_USAGE_DIGITIZER_TOUCH_PAD);
    if (Collection == NULL)
        return STATUS_NOT_FOUND;

    Status = HidP_GetCaps(Collection->PreparsedData, &Caps);
    if (Status != HIDP_STATUS_SUCCESS || Caps.NumberLinkCollectionNodes == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    NodeCount = Caps.NumberLinkCollectionNodes;
    Nodes = ExAllocatePoolWithTag(NonPagedPool,
                                  NodeCount * sizeof(*Nodes),
                                  HIDCLASS_TAG);
    if (Nodes == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = HidP_GetLinkCollectionNodes(Nodes,
                                         &NodeCount,
                                         Collection->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS ||
        NodeCount != Caps.NumberLinkCollectionNodes ||
        Nodes[0].LinkUsagePage != HID_USAGE_PAGE_DIGITIZER ||
        Nodes[0].LinkUsage != HID_USAGE_DIGITIZER_TOUCH_PAD)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    Status = HidClassPtpGetValueCapabilities(Collection,
                                              HidP_Feature,
                                              Caps.NumberFeatureValueCaps,
                                              &FeatureCaps);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = HidClassPtpGetValueCapabilities(Collection,
                                              HidP_Output,
                                              Caps.NumberOutputValueCaps,
                                              &OutputCaps);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = HidClassPtpGetButtonCapabilities(Collection,
                                               HidP_Feature,
                                               Caps.NumberFeatureButtonCaps,
                                               &FeatureButtonCaps);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = HidClassPtpGetButtonCapabilities(Collection,
                                               HidP_Output,
                                               Caps.NumberOutputButtonCaps,
                                               &OutputButtonCaps);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    ButtonPressThreshold = HidClassPtpFindValueCapability(
                               FeatureCaps,
                               Caps.NumberFeatureValueCaps,
                               HID_USAGE_PAGE_DIGITIZER,
                               HID_USAGE_DIGITIZER_BUTTON_PRESS_THRESHOLD,
                               &ButtonPressThresholdCount);
    DeviceIntensity = HidClassPtpFindValueCapability(
                          FeatureCaps,
                          Caps.NumberFeatureValueCaps,
                          HID_USAGE_PAGE_HAPTICS,
                          HID_USAGE_HAPTICS_INTENSITY,
                          &DeviceIntensityCount);
    (VOID)HidClassPtpFindLinkedValueCapability(
              FeatureCaps,
              Caps.NumberFeatureValueCaps,
              HID_USAGE_PAGE_ORDINAL,
              HID_USAGE_PAGE_HAPTICS,
              HID_USAGE_HAPTICS_WAVEFORM_LIST,
              &WaveformListCount);
    (VOID)HidClassPtpFindLinkedValueCapability(
              FeatureCaps,
              Caps.NumberFeatureValueCaps,
              HID_USAGE_PAGE_ORDINAL,
              HID_USAGE_PAGE_HAPTICS,
              HID_USAGE_HAPTICS_DURATION_LIST,
              &DurationListCount);
    ManualTrigger = HidClassPtpFindValueCapability(
                        OutputCaps,
                        Caps.NumberOutputValueCaps,
                        HID_USAGE_PAGE_HAPTICS,
                        HID_USAGE_HAPTICS_MANUAL_TRIGGER,
                        &ManualTriggerCount);
    HostIntensity = HidClassPtpFindValueCapability(
                        OutputCaps,
                        Caps.NumberOutputValueCaps,
                        HID_USAGE_PAGE_HAPTICS,
                        HID_USAGE_HAPTICS_INTENSITY,
                        &HostIntensityCount);
    RepeatCount = HidClassPtpFindValueCapability(
                      OutputCaps,
                      Caps.NumberOutputValueCaps,
                      HID_USAGE_PAGE_HAPTICS,
                      HID_USAGE_HAPTICS_REPEAT_COUNT,
                      &RepeatCountCount);
    RetriggerPeriod = HidClassPtpFindValueCapability(
                          OutputCaps,
                          Caps.NumberOutputValueCaps,
                          HID_USAGE_PAGE_HAPTICS,
                          HID_USAGE_HAPTICS_RETRIGGER_PERIOD,
                          &RetriggerPeriodCount);
    WaveformCutoffTime = HidClassPtpFindValueCapability(
                             OutputCaps,
                             Caps.NumberOutputValueCaps,
                             HID_USAGE_PAGE_HAPTICS,
                             HID_USAGE_HAPTICS_WAVEFORM_CUTOFF_TIME,
                             &WaveformCutoffTimeCount);

    HostCapabilityCount = WaveformListCount + DurationListCount +
                          ManualTriggerCount + HostIntensityCount +
                          RepeatCountCount + RetriggerPeriodCount +
                          WaveformCutoffTimeCount;
    if (ButtonPressThresholdCount == 0 && DeviceIntensityCount == 0 &&
        HostCapabilityCount == 0)
    {
        Status = STATUS_NOT_FOUND;
        goto Cleanup;
    }

    Result.Present = TRUE;
    Result.CollectionNumber = Collection->CollectionNumber;

    if (ButtonPressThresholdCount > 1 || DeviceIntensityCount > 1 ||
        ManualTriggerCount > 1 || HostIntensityCount > 1 ||
        RepeatCountCount > 1 || RetriggerPeriodCount > 1 ||
        WaveformCutoffTimeCount > 1)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    for (Index = 0; Index < Caps.NumberFeatureValueCaps; Index++)
    {
        if (HidClassPtpCapabilityContainsUsage(&FeatureCaps[Index],
                                               HID_USAGE_PAGE_HAPTICS,
                                               HID_USAGE_HAPTICS_AUTO_TRIGGER) ||
            HidClassPtpCapabilityContainsUsage(&FeatureCaps[Index],
                                               HID_USAGE_PAGE_HAPTICS,
                                               HID_USAGE_HAPTICS_AUTO_ASSOCIATED_CONTROL))
        {
            ForbiddenUsagePresent = TRUE;
        }
    }

    for (Index = 0; Index < Caps.NumberOutputValueCaps; Index++)
    {
        if (HidClassPtpCapabilityContainsUsage(&OutputCaps[Index],
                                               HID_USAGE_PAGE_HAPTICS,
                                               HID_USAGE_HAPTICS_AUTO_TRIGGER) ||
            HidClassPtpCapabilityContainsUsage(&OutputCaps[Index],
                                               HID_USAGE_PAGE_HAPTICS,
                                               HID_USAGE_HAPTICS_AUTO_ASSOCIATED_CONTROL))
        {
            ForbiddenUsagePresent = TRUE;
        }
    }

    if (ForbiddenUsagePresent)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    if (ButtonPressThresholdCount == 1)
    {
        ReportDescription = HidClassPtpFindFeatureReport(
                                DeviceDescription,
                                Collection->CollectionNumber,
                                ButtonPressThreshold->ReportID);
        if (!HidClassPtpIsScalarHapticCapability(ButtonPressThreshold) ||
            ButtonPressThreshold->LinkCollection !=
                HIDP_LINK_COLLECTION_UNSPECIFIED ||
            ButtonPressThreshold->LogicalMin >= ButtonPressThreshold->LogicalMax ||
            ButtonPressThreshold->PhysicalMin >= ButtonPressThreshold->PhysicalMax ||
            ReportDescription == NULL || ReportDescription->FeatureLength < 2 ||
            ReportDescription->InputLength != 0 ||
            ReportDescription->OutputLength != 0 ||
            !HidClassPtpReportHasExactCapabilities(
                 FeatureCaps,
                 Caps.NumberFeatureValueCaps,
                 FeatureButtonCaps,
                 Caps.NumberFeatureButtonCaps,
                 ButtonPressThreshold->ReportID,
                 1))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        Result.HasButtonPressThreshold = TRUE;
        HidClassPtpStoreHapticCapability(&Result.ButtonPressThreshold,
                                         ButtonPressThreshold,
                                         ReportDescription->FeatureLength);
    }

    if (DeviceIntensityCount == 1)
    {
        ReportDescription = HidClassPtpFindFeatureReport(
                                DeviceDescription,
                                Collection->CollectionNumber,
                                DeviceIntensity->ReportID);
        if (!HidClassPtpIsScalarHapticCapability(DeviceIntensity) ||
            DeviceIntensity->LogicalMin != 0 ||
            DeviceIntensity->LogicalMax < 4 ||
            !HidClassPtpIsLogicalCollection(
                 Nodes,
                 NodeCount,
                 DeviceIntensity->LinkCollection,
                 HID_USAGE_PAGE_HAPTICS,
                 HID_USAGE_HAPTICS_SIMPLE_CONTROLLER) ||
            Nodes[DeviceIntensity->LinkCollection].Parent != 0 ||
            ReportDescription == NULL || ReportDescription->FeatureLength < 2 ||
            ReportDescription->InputLength != 0 ||
            ReportDescription->OutputLength != 0 ||
            !HidClassPtpReportHasExactCapabilities(
                 FeatureCaps,
                 Caps.NumberFeatureValueCaps,
                 FeatureButtonCaps,
                 Caps.NumberFeatureButtonCaps,
                 DeviceIntensity->ReportID,
                 1))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        for (Index = 0; Index < Caps.NumberFeatureValueCaps; Index++)
        {
            if (FeatureCaps[Index].LinkCollection ==
                    DeviceIntensity->LinkCollection &&
                (HidClassPtpCapabilityContainsUsage(
                     &FeatureCaps[Index],
                     HID_USAGE_PAGE_HAPTICS,
                     HID_USAGE_HAPTICS_AUTO_TRIGGER) ||
                 HidClassPtpCapabilityContainsUsage(
                     &FeatureCaps[Index],
                     HID_USAGE_PAGE_HAPTICS,
                     HID_USAGE_HAPTICS_MANUAL_TRIGGER)))
            {
                Status = STATUS_DEVICE_CONFIGURATION_ERROR;
                goto Cleanup;
            }
        }

        Result.HasDeviceIntensity = TRUE;
        HidClassPtpStoreHapticCapability(&Result.DeviceIntensity,
                                         DeviceIntensity,
                                         ReportDescription->FeatureLength);
    }

    if (Result.HasButtonPressThreshold && Result.HasDeviceIntensity &&
        ButtonPressThreshold->ReportID == DeviceIntensity->ReportID)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    if (HostCapabilityCount != 0)
    {
        OptionalControlCount = RepeatCountCount + RetriggerPeriodCount +
                               WaveformCutoffTimeCount;
        if (WaveformListCount == 0 || DurationListCount == 0 ||
            ManualTriggerCount != 1 || HostIntensityCount != 1 ||
            (OptionalControlCount != 0 && OptionalControlCount != 3) ||
            !Result.HasButtonPressThreshold || !Result.HasDeviceIntensity)
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        if (!HidClassPtpNormalizeListCapabilities(
                 FeatureCaps,
                 Caps.NumberFeatureValueCaps,
                 HID_USAGE_HAPTICS_WAVEFORM_LIST,
                 &Result.WaveformList) ||
            !HidClassPtpNormalizeListCapabilities(
                 FeatureCaps,
                 Caps.NumberFeatureValueCaps,
                 HID_USAGE_HAPTICS_DURATION_LIST,
                 &Result.DurationList) ||
            Result.WaveformList.PhysicalMinimum != 0 ||
            Result.WaveformList.PhysicalMaximum != 0 ||
            Result.WaveformList.Units != 0 ||
            Result.WaveformList.LogicalMinimum >
                HID_USAGE_HAPTICS_WAVEFORM_NONE ||
            Result.WaveformList.LogicalMaximum <
                HID_USAGE_HAPTICS_WAVEFORM_STOP ||
            Result.DurationList.UsageMinimum !=
                Result.WaveformList.UsageMinimum ||
            Result.DurationList.UsageMaximum !=
                Result.WaveformList.UsageMaximum ||
            Result.DurationList.ReportCount !=
                Result.WaveformList.ReportCount ||
            Result.DurationList.LogicalMinimum != 0 ||
            Result.DurationList.LogicalMaximum <= 0 ||
            !HidClassPtpListHasMillisecondPhysicalRange(&Result.DurationList) ||
            Result.WaveformList.ReportId != Result.DurationList.ReportId ||
            !HidClassPtpIsLogicalCollection(
                 Nodes,
                 NodeCount,
                 Result.WaveformList.LinkCollection,
                 HID_USAGE_PAGE_HAPTICS,
                 HID_USAGE_HAPTICS_WAVEFORM_LIST) ||
            !HidClassPtpIsLogicalCollection(
                 Nodes,
                 NodeCount,
                 Result.DurationList.LinkCollection,
                 HID_USAGE_PAGE_HAPTICS,
                 HID_USAGE_HAPTICS_DURATION_LIST) ||
            Nodes[Result.WaveformList.LinkCollection].Parent !=
                Nodes[Result.DurationList.LinkCollection].Parent)
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        HostInformationController =
            Nodes[Result.WaveformList.LinkCollection].Parent;
        ManualTriggerController = ManualTrigger->LinkCollection;
        if (!HidClassPtpIsLogicalCollection(
                 Nodes,
                 NodeCount,
                 HostInformationController,
                 HID_USAGE_PAGE_HAPTICS,
                 HID_USAGE_HAPTICS_SIMPLE_CONTROLLER) ||
            !HidClassPtpIsLogicalCollection(
                 Nodes,
                 NodeCount,
                 ManualTriggerController,
                 HID_USAGE_PAGE_HAPTICS,
                 HID_USAGE_HAPTICS_SIMPLE_CONTROLLER) ||
            HostInformationController == ManualTriggerController)
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        HostControllerParent = Nodes[HostInformationController].Parent;
        if (HostControllerParent != Nodes[ManualTriggerController].Parent ||
            (HostControllerParent != 0 &&
             !HidClassPtpIsLogicalCollection(
                  Nodes,
                  NodeCount,
                  HostControllerParent,
                  HID_USAGE_PAGE_HAPTICS,
                  HID_USAGE_HAPTICS_SIMPLE_CONTROLLER)) ||
            HostControllerParent == DeviceIntensity->LinkCollection ||
            HostInformationController == DeviceIntensity->LinkCollection ||
            ManualTriggerController == DeviceIntensity->LinkCollection)
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        if (!HidClassPtpIsScalarHapticCapability(ManualTrigger) ||
            !HidClassPtpIsScalarHapticCapability(HostIntensity) ||
            ManualTrigger->ReportID != HostIntensity->ReportID ||
            ManualTrigger->LinkCollection != HostIntensity->LinkCollection ||
            ManualTrigger->LogicalMin > 1 ||
            ManualTrigger->LogicalMax < Result.WaveformList.UsageMaximum ||
            ManualTrigger->PhysicalMin != 0 || ManualTrigger->PhysicalMax != 0 ||
            ManualTrigger->Units != 0 ||
            HostIntensity->LogicalMin != 0 ||
            HostIntensity->LogicalMax < 4 || HostIntensity->LogicalMax > 100 ||
            HostIntensity->PhysicalMin != 0 || HostIntensity->PhysicalMax != 0 ||
            HostIntensity->Units != 0)
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        if (OptionalControlCount == 3)
        {
            if (!HidClassPtpIsScalarHapticCapability(RepeatCount) ||
                !HidClassPtpIsScalarHapticCapability(RetriggerPeriod) ||
                !HidClassPtpIsScalarHapticCapability(WaveformCutoffTime) ||
                RepeatCount->ReportID != ManualTrigger->ReportID ||
                RetriggerPeriod->ReportID != ManualTrigger->ReportID ||
                WaveformCutoffTime->ReportID != ManualTrigger->ReportID ||
                RepeatCount->LinkCollection != ManualTriggerController ||
                RetriggerPeriod->LinkCollection != ManualTriggerController ||
                WaveformCutoffTime->LinkCollection != ManualTriggerController ||
                RepeatCount->LogicalMin != 0 || RepeatCount->LogicalMax <= 0 ||
                RepeatCount->PhysicalMin != 0 || RepeatCount->PhysicalMax != 0 ||
                RepeatCount->Units != 0 ||
                RetriggerPeriod->LogicalMin != 0 ||
                RetriggerPeriod->LogicalMax < 1000 ||
                !HidClassPtpHasMillisecondPhysicalRange(RetriggerPeriod) ||
                WaveformCutoffTime->LogicalMin <= 0 ||
                WaveformCutoffTime->LogicalMax < WaveformCutoffTime->LogicalMin ||
                !HidClassPtpHasMillisecondPhysicalRange(WaveformCutoffTime))
            {
                Status = STATUS_DEVICE_CONFIGURATION_ERROR;
                goto Cleanup;
            }
        }

        ReportDescription = HidClassPtpFindFeatureReport(
                                DeviceDescription,
                                Collection->CollectionNumber,
                                Result.WaveformList.ReportId);
        if (ReportDescription == NULL || ReportDescription->FeatureLength < 2 ||
            ReportDescription->InputLength != 0 ||
            ReportDescription->OutputLength != 0 ||
            !HidClassPtpReportHasExactCapabilities(
                 FeatureCaps,
                 Caps.NumberFeatureValueCaps,
                 FeatureButtonCaps,
                 Caps.NumberFeatureButtonCaps,
                 Result.WaveformList.ReportId,
                 WaveformListCount + DurationListCount))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        Result.WaveformList.ReportLength = ReportDescription->FeatureLength;
        Result.DurationList.ReportLength = ReportDescription->FeatureLength;

        ReportDescription = HidClassPtpFindOutputReport(
                                DeviceDescription,
                                Collection->CollectionNumber,
                                ManualTrigger->ReportID);
        if (ReportDescription == NULL || ReportDescription->OutputLength < 2 ||
            ReportDescription->InputLength != 0 ||
            ReportDescription->FeatureLength != 0 ||
            !HidClassPtpReportHasExactCapabilities(
                 OutputCaps,
                 Caps.NumberOutputValueCaps,
                 OutputButtonCaps,
                 Caps.NumberOutputButtonCaps,
                 ManualTrigger->ReportID,
                 OptionalControlCount == 3 ? 5 : 2))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        if (Result.WaveformList.ReportId == ManualTrigger->ReportID ||
            Result.WaveformList.ReportId == ButtonPressThreshold->ReportID ||
            Result.WaveformList.ReportId == DeviceIntensity->ReportID ||
            ManualTrigger->ReportID == ButtonPressThreshold->ReportID ||
            ManualTrigger->ReportID == DeviceIntensity->ReportID)
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        HidClassPtpStoreHapticCapability(&Result.ManualTrigger,
                                         ManualTrigger,
                                         ReportDescription->OutputLength);
        HidClassPtpStoreHapticCapability(&Result.HostIntensity,
                                         HostIntensity,
                                         ReportDescription->OutputLength);
        if (OptionalControlCount == 3)
        {
            Result.HasRepeatControl = TRUE;
            HidClassPtpStoreHapticCapability(&Result.RepeatCount,
                                             RepeatCount,
                                             ReportDescription->OutputLength);
            HidClassPtpStoreHapticCapability(&Result.RetriggerPeriod,
                                             RetriggerPeriod,
                                             ReportDescription->OutputLength);
            HidClassPtpStoreHapticCapability(&Result.WaveformCutoffTime,
                                             WaveformCutoffTime,
                                             ReportDescription->OutputLength);
        }

        Result.HasHostInitiated = TRUE;
    }

    Result.Valid = TRUE;
    *HapticsCapabilities = Result;
    Status = STATUS_SUCCESS;

Cleanup:
    if (!NT_SUCCESS(Status) && Result.Present)
    {
        HapticsCapabilities->Present = TRUE;
        HapticsCapabilities->CollectionNumber = Result.CollectionNumber;
    }
    if (OutputButtonCaps != NULL)
        ExFreePoolWithTag(OutputButtonCaps, HIDCLASS_TAG);
    if (FeatureButtonCaps != NULL)
        ExFreePoolWithTag(FeatureButtonCaps, HIDCLASS_TAG);
    if (OutputCaps != NULL)
        ExFreePoolWithTag(OutputCaps, HIDCLASS_TAG);
    if (FeatureCaps != NULL)
        ExFreePoolWithTag(FeatureCaps, HIDCLASS_TAG);
    if (Nodes != NULL)
        ExFreePoolWithTag(Nodes, HIDCLASS_TAG);
    return Status;
}

static
BOOLEAN
HidClassPtpHapticValueCapabilityMatches(
    _In_ PHIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY Capability,
    _In_ USAGE UsagePage,
    _In_ USAGE Usage)
{
    return Capability->ReportLength >= 2 &&
           HidClassPtpIsScalarHapticCapability(&Capability->ValueCaps) &&
           Capability->ValueCaps.UsagePage == UsagePage &&
           Capability->ValueCaps.NotRange.Usage == Usage;
}

static
BOOLEAN
HidClassPtpHapticValueInRange(
    _In_ PHIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY Capability,
    _In_ ULONG Value)
{
    return Capability->ValueCaps.LogicalMin >= 0 &&
           Capability->ValueCaps.LogicalMax >= Capability->ValueCaps.LogicalMin &&
           Value >= (ULONG)Capability->ValueCaps.LogicalMin &&
           Value <= (ULONG)Capability->ValueCaps.LogicalMax;
}

static
NTSTATUS
HidClassPtpWriteHapticFeature(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities,
    _In_ PHIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY Capability,
    _In_ USAGE UsagePage,
    _In_ USAGE Usage,
    _In_ ULONG Value,
    _In_ PHIDCLASS_PTP_SET_FEATURE SetFeature,
    _In_ PVOID Context)
{
    PHIDP_COLLECTION_DESC Collection;
    PHIDP_REPORT_IDS ReportDescription;
    PUCHAR ReportBuffer;
    NTSTATUS Status;

    if (!HidClassPtpHapticValueCapabilityMatches(Capability, UsagePage, Usage))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    if (!HidClassPtpHapticValueInRange(Capability, Value))
        return STATUS_INVALID_PARAMETER;

    Collection = HidClassPtpFindCollection(DeviceDescription,
                                            HID_USAGE_PAGE_DIGITIZER,
                                            HID_USAGE_DIGITIZER_TOUCH_PAD);
    if (Collection == NULL ||
        Collection->CollectionNumber != HapticsCapabilities->CollectionNumber)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ReportDescription = HidClassPtpFindFeatureReport(
                            DeviceDescription,
                            Collection->CollectionNumber,
                            Capability->ValueCaps.ReportID);
    if (ReportDescription == NULL ||
        ReportDescription->FeatureLength != Capability->ReportLength)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ReportBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                         ReportDescription->FeatureLength,
                                         HIDCLASS_TAG);
    if (ReportBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(ReportBuffer, ReportDescription->FeatureLength);
    ReportBuffer[0] = Capability->ValueCaps.ReportID;
    Status = HidP_SetUsageValue(HidP_Feature,
                                UsagePage,
                                Capability->ValueCaps.LinkCollection,
                                Usage,
                                Value,
                                Collection->PreparsedData,
                                (PCHAR)ReportBuffer,
                                ReportDescription->FeatureLength);
    if (Status == HIDP_STATUS_SUCCESS)
    {
        Status = SetFeature(Context,
                            Capability->ValueCaps.ReportID,
                            ReportBuffer,
                            ReportDescription->FeatureLength);
    }
    else
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ExFreePoolWithTag(ReportBuffer, HIDCLASS_TAG);
    return Status;
}

NTSTATUS
HidClassPtpSetHapticButtonPressThreshold(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities,
    _In_ ULONG Threshold,
    _In_ PHIDCLASS_PTP_SET_FEATURE SetFeature,
    _In_ PVOID Context)
{
    if (DeviceDescription == NULL || HapticsCapabilities == NULL ||
        SetFeature == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!HapticsCapabilities->Valid)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!HapticsCapabilities->HasButtonPressThreshold)
        return STATUS_NOT_SUPPORTED;

    return HidClassPtpWriteHapticFeature(
               DeviceDescription,
               HapticsCapabilities,
               &HapticsCapabilities->ButtonPressThreshold,
               HID_USAGE_PAGE_DIGITIZER,
               HID_USAGE_DIGITIZER_BUTTON_PRESS_THRESHOLD,
               Threshold,
               SetFeature,
               Context);
}

NTSTATUS
HidClassPtpSetDeviceHapticIntensity(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities,
    _In_ ULONG Intensity,
    _In_ PHIDCLASS_PTP_SET_FEATURE SetFeature,
    _In_ PVOID Context)
{
    if (DeviceDescription == NULL || HapticsCapabilities == NULL ||
        SetFeature == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!HapticsCapabilities->Valid)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!HapticsCapabilities->HasDeviceIntensity)
        return STATUS_NOT_SUPPORTED;

    return HidClassPtpWriteHapticFeature(
               DeviceDescription,
               HapticsCapabilities,
               &HapticsCapabilities->DeviceIntensity,
               HID_USAGE_PAGE_HAPTICS,
               HID_USAGE_HAPTICS_INTENSITY,
               Intensity,
               SetFeature,
               Context);
}

static
BOOLEAN
HidClassPtpIsForbiddenTouchpadWaveform(
    _In_ ULONG Waveform)
{
    switch (Waveform)
    {
        case HID_USAGE_HAPTICS_WAVEFORM_CLICK:
        case HID_USAGE_HAPTICS_WAVEFORM_BUZZ:
        case HID_USAGE_HAPTICS_WAVEFORM_RUMBLE:
        case HID_USAGE_HAPTICS_WAVEFORM_INK_CONTINUOUS:
        case HID_USAGE_HAPTICS_WAVEFORM_PENCIL_CONTINUOUS:
        case HID_USAGE_HAPTICS_WAVEFORM_MARKER_CONTINUOUS:
        case HID_USAGE_HAPTICS_WAVEFORM_CHISEL_MARKER_CONTINUOUS:
        case HID_USAGE_HAPTICS_WAVEFORM_BRUSH_CONTINUOUS:
        case HID_USAGE_HAPTICS_WAVEFORM_ERASER_CONTINUOUS:
        case HID_USAGE_HAPTICS_WAVEFORM_SPARKLE_CONTINUOUS:
            return TRUE;

        default:
            return FALSE;
    }
}

NTSTATUS
HidClassPtpGetHapticWaveforms(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities,
    _In_ PHIDCLASS_PTP_GET_FEATURE GetFeature,
    _In_ PVOID Context,
    _Out_writes_to_opt_(WaveformCapacity, *WaveformCount) PHIDCLASS_PTP_HAPTIC_WAVEFORM Waveforms,
    _In_ ULONG WaveformCapacity,
    _Out_ PULONG WaveformCount)
{
    PHIDP_COLLECTION_DESC Collection;
    PUCHAR ReportBuffer = NULL;
    ULONG ReportLength = 0;
    ULONG RequiredCount;
    ULONG Index;
    ULONG Ordinal;
    ULONG Waveform;
    ULONG Duration;
    BOOLEAN HasPress = FALSE;
    BOOLEAN HasRelease = FALSE;
    BOOLEAN HasSuccess = FALSE;
    BOOLEAN HasError = FALSE;
    NTSTATUS Status;

    if (DeviceDescription == NULL || HapticsCapabilities == NULL ||
        GetFeature == NULL || WaveformCount == NULL ||
        (Waveforms == NULL && WaveformCapacity != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *WaveformCount = 0;
    if (!HapticsCapabilities->Valid)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!HapticsCapabilities->HasHostInitiated)
        return STATUS_NOT_SUPPORTED;

    RequiredCount = HapticsCapabilities->WaveformList.ReportCount;
    if (RequiredCount == 0 ||
        RequiredCount != HapticsCapabilities->DurationList.ReportCount ||
        HapticsCapabilities->WaveformList.ReportId == 0 ||
        HapticsCapabilities->WaveformList.ReportId !=
            HapticsCapabilities->DurationList.ReportId ||
        HapticsCapabilities->WaveformList.ReportLength !=
            HapticsCapabilities->DurationList.ReportLength)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    *WaveformCount = RequiredCount;
    if (Waveforms == NULL || WaveformCapacity < RequiredCount)
        return STATUS_BUFFER_TOO_SMALL;

    Collection = HidClassPtpFindCollection(DeviceDescription,
                                            HID_USAGE_PAGE_DIGITIZER,
                                            HID_USAGE_DIGITIZER_TOUCH_PAD);
    if (Collection == NULL ||
        Collection->CollectionNumber != HapticsCapabilities->CollectionNumber)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RtlZeroMemory(Waveforms, RequiredCount * sizeof(*Waveforms));
    Status = HidClassPtpReadFeature(
                 DeviceDescription,
                 Collection,
                 HapticsCapabilities->WaveformList.ReportId,
                 GetFeature,
                 Context,
                 &ReportBuffer,
                 &ReportLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (ReportLength != HapticsCapabilities->WaveformList.ReportLength)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    for (Index = 0; Index < RequiredCount; Index++)
    {
        Ordinal = HapticsCapabilities->WaveformList.UsageMinimum + Index;
        Waveform = 0;
        Status = HidP_GetUsageValue(
                     HidP_Feature,
                     HID_USAGE_PAGE_ORDINAL,
                     HapticsCapabilities->WaveformList.LinkCollection,
                     (USAGE)Ordinal,
                     &Waveform,
                     Collection->PreparsedData,
                     (PCHAR)ReportBuffer,
                     ReportLength);
        if (Status != HIDP_STATUS_SUCCESS)
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        Duration = 0;
        Status = HidP_GetUsageValue(
                     HidP_Feature,
                     HID_USAGE_PAGE_ORDINAL,
                     HapticsCapabilities->DurationList.LinkCollection,
                     (USAGE)Ordinal,
                     &Duration,
                     Collection->PreparsedData,
                     (PCHAR)ReportBuffer,
                     ReportLength);
        if (Status != HIDP_STATUS_SUCCESS)
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        if (Waveform < (ULONG)HapticsCapabilities->WaveformList.LogicalMinimum ||
            Waveform > (ULONG)HapticsCapabilities->WaveformList.LogicalMaximum ||
            Duration < (ULONG)HapticsCapabilities->DurationList.LogicalMinimum ||
            Duration > (ULONG)HapticsCapabilities->DurationList.LogicalMaximum ||
            HidClassPtpIsForbiddenTouchpadWaveform(Waveform) ||
            (Ordinal == HIDCLASS_PTP_HAPTIC_NONE_ORDINAL &&
             Waveform != HID_USAGE_HAPTICS_WAVEFORM_NONE) ||
            (Ordinal == HIDCLASS_PTP_HAPTIC_STOP_ORDINAL &&
             Waveform != HID_USAGE_HAPTICS_WAVEFORM_STOP) ||
            (Ordinal != HIDCLASS_PTP_HAPTIC_STOP_ORDINAL &&
             Waveform == HID_USAGE_HAPTICS_WAVEFORM_STOP) ||
            ((Waveform == HID_USAGE_HAPTICS_WAVEFORM_NONE ||
              Waveform == HID_USAGE_HAPTICS_WAVEFORM_STOP) && Duration != 0) ||
            (Waveform != HID_USAGE_HAPTICS_WAVEFORM_NONE &&
             Waveform != HID_USAGE_HAPTICS_WAVEFORM_STOP && Duration == 0))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Cleanup;
        }

        if (Waveform == HID_USAGE_HAPTICS_WAVEFORM_PRESS)
            HasPress = TRUE;
        else if (Waveform == HID_USAGE_HAPTICS_WAVEFORM_RELEASE)
            HasRelease = TRUE;
        else if (Waveform == HID_USAGE_HAPTICS_WAVEFORM_SUCCESS)
            HasSuccess = TRUE;
        else if (Waveform == HID_USAGE_HAPTICS_WAVEFORM_ERROR)
            HasError = TRUE;

        Waveforms[Index].Ordinal = Ordinal;
        Waveforms[Index].WaveformUsage = Waveform;
        Waveforms[Index].DurationMilliseconds = Duration;
    }

    if (HasPress != HasRelease || HasSuccess != HasError)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    Status = STATUS_SUCCESS;

Cleanup:
    if (!NT_SUCCESS(Status))
        RtlZeroMemory(Waveforms, RequiredCount * sizeof(*Waveforms));
    ExFreePoolWithTag(ReportBuffer, HIDCLASS_TAG);
    return Status;
}

static
NTSTATUS
HidClassPtpSetHapticOutputValue(
    _In_ PHIDP_COLLECTION_DESC Collection,
    _In_ PHIDCLASS_PTP_HAPTIC_VALUE_CAPABILITY Capability,
    _In_ USAGE Usage,
    _In_ ULONG Value,
    _Inout_updates_bytes_(ReportLength) PUCHAR ReportBuffer,
    _In_ ULONG ReportLength)
{
    NTSTATUS Status;

    if (!HidClassPtpHapticValueCapabilityMatches(Capability,
                                                  HID_USAGE_PAGE_HAPTICS,
                                                  Usage) ||
        Capability->ReportLength != ReportLength)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (!HidClassPtpHapticValueInRange(Capability, Value))
        return STATUS_INVALID_PARAMETER;

    Status = HidP_SetUsageValue(HidP_Output,
                                HID_USAGE_PAGE_HAPTICS,
                                Capability->ValueCaps.LinkCollection,
                                Usage,
                                Value,
                                Collection->PreparsedData,
                                (PCHAR)ReportBuffer,
                                ReportLength);
    return Status == HIDP_STATUS_SUCCESS ?
           STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS
HidClassPtpSendHapticOutput(
    _In_ PHIDP_DEVICE_DESC DeviceDescription,
    _In_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities,
    _In_ PHIDCLASS_PTP_HAPTIC_OUTPUT Output,
    _In_ PHIDCLASS_PTP_WRITE_OUTPUT WriteOutput,
    _In_ PVOID Context)
{
    PHIDP_COLLECTION_DESC Collection;
    PHIDP_REPORT_IDS ReportDescription;
    PUCHAR ReportBuffer;
    NTSTATUS Status;

    if (DeviceDescription == NULL || HapticsCapabilities == NULL ||
        Output == NULL || WriteOutput == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!HapticsCapabilities->Valid)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!HapticsCapabilities->HasHostInitiated)
        return STATUS_NOT_SUPPORTED;

    if (!HidClassPtpHapticValueInRange(&HapticsCapabilities->ManualTrigger,
                                       Output->WaveformOrdinal) ||
        !HidClassPtpHapticValueInRange(&HapticsCapabilities->HostIntensity,
                                       Output->Intensity) ||
        ((Output->Intensity == 0) !=
         (Output->WaveformOrdinal == HIDCLASS_PTP_HAPTIC_STOP_ORDINAL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (HapticsCapabilities->HasRepeatControl)
    {
        if (!HidClassPtpHapticValueInRange(&HapticsCapabilities->RepeatCount,
                                           Output->RepeatCount) ||
            !HidClassPtpHapticValueInRange(&HapticsCapabilities->RetriggerPeriod,
                                           Output->RetriggerPeriodMilliseconds) ||
            !HidClassPtpHapticValueInRange(&HapticsCapabilities->WaveformCutoffTime,
                                           Output->WaveformCutoffTimeMilliseconds))
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    else if (Output->RepeatCount != 0 ||
             Output->RetriggerPeriodMilliseconds != 0 ||
             Output->WaveformCutoffTimeMilliseconds != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Collection = HidClassPtpFindCollection(DeviceDescription,
                                            HID_USAGE_PAGE_DIGITIZER,
                                            HID_USAGE_DIGITIZER_TOUCH_PAD);
    if (Collection == NULL ||
        Collection->CollectionNumber != HapticsCapabilities->CollectionNumber)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ReportDescription = HidClassPtpFindOutputReport(
                            DeviceDescription,
                            Collection->CollectionNumber,
                            HapticsCapabilities->ManualTrigger.ValueCaps.ReportID);
    if (ReportDescription == NULL ||
        ReportDescription->OutputLength !=
            HapticsCapabilities->ManualTrigger.ReportLength)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ReportBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                         ReportDescription->OutputLength,
                                         HIDCLASS_TAG);
    if (ReportBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(ReportBuffer, ReportDescription->OutputLength);
    ReportBuffer[0] = HapticsCapabilities->ManualTrigger.ValueCaps.ReportID;
    Status = HidClassPtpSetHapticOutputValue(
                 Collection,
                 &HapticsCapabilities->ManualTrigger,
                 HID_USAGE_HAPTICS_MANUAL_TRIGGER,
                 Output->WaveformOrdinal,
                 ReportBuffer,
                 ReportDescription->OutputLength);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = HidClassPtpSetHapticOutputValue(
                 Collection,
                 &HapticsCapabilities->HostIntensity,
                 HID_USAGE_HAPTICS_INTENSITY,
                 Output->Intensity,
                 ReportBuffer,
                 ReportDescription->OutputLength);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (HapticsCapabilities->HasRepeatControl)
    {
        Status = HidClassPtpSetHapticOutputValue(
                     Collection,
                     &HapticsCapabilities->RepeatCount,
                     HID_USAGE_HAPTICS_REPEAT_COUNT,
                     Output->RepeatCount,
                     ReportBuffer,
                     ReportDescription->OutputLength);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        Status = HidClassPtpSetHapticOutputValue(
                     Collection,
                     &HapticsCapabilities->RetriggerPeriod,
                     HID_USAGE_HAPTICS_RETRIGGER_PERIOD,
                     Output->RetriggerPeriodMilliseconds,
                     ReportBuffer,
                     ReportDescription->OutputLength);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        Status = HidClassPtpSetHapticOutputValue(
                     Collection,
                     &HapticsCapabilities->WaveformCutoffTime,
                     HID_USAGE_HAPTICS_WAVEFORM_CUTOFF_TIME,
                     Output->WaveformCutoffTimeMilliseconds,
                     ReportBuffer,
                     ReportDescription->OutputLength);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Status = WriteOutput(
                 Context,
                 HapticsCapabilities->ManualTrigger.ValueCaps.ReportID,
                 ReportBuffer,
                 ReportDescription->OutputLength);

Cleanup:
    ExFreePoolWithTag(ReportBuffer, HIDCLASS_TAG);
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
