/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     HIDCLASS Precision Touchpad capability policy tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <hidpddi.h>

#include "HidP.h"
#include "ptp.h"

#define PTP_CAPABILITIES_REPORT_ID   2
#define PTP_CERTIFICATION_REPORT_ID  6
#define PTP_DEVICE_MODE_REPORT_ID    3
#define PTP_SELECTIVE_REPORT_ID      5

#define PTP_CONFIG_MODE_MAXIMUM_OFFSET       17
#define PTP_CONFIG_MODE_SIZE_OFFSET          19
#define PTP_CONFIG_SELECTIVE_ID_OFFSET       30
#define PTP_CONFIG_BUTTON_USAGE_OFFSET       34
#define PTP_CONFIG_SELECTIVE_MAXIMUM_OFFSET  38

#define PTP_HAPTIC_THRESHOLD_MAIN_ITEM_OFFSET       31
#define PTP_HAPTIC_DEVICE_INTENSITY_ID_OFFSET       33
#define PTP_HAPTIC_DEVICE_COLLECTION_TYPE_OFFSET    39
#define PTP_HAPTIC_DEVICE_INTENSITY_MAXIMUM_OFFSET  55
#define PTP_HAPTIC_DEVICE_INTENSITY_MAIN_OFFSET     61
#define PTP_HAPTIC_WAVEFORM_REPORT_COUNT_OFFSET     98
#define PTP_HAPTIC_DURATION_USAGE_MAXIMUM_OFFSET    115
#define PTP_HAPTIC_DURATION_UNIT_EXPONENT_OFFSET    124
#define PTP_HAPTIC_MANUAL_USAGE_OFFSET              148
#define PTP_HAPTIC_MANUAL_MAIN_ITEM_OFFSET          166
#define PTP_HAPTIC_HOST_INTENSITY_MAIN_OFFSET       188
#define PTP_HAPTIC_REPEAT_MAIN_ITEM_OFFSET           210
#define PTP_HAPTIC_RETRIGGER_MAIN_ITEM_OFFSET        235
#define PTP_HAPTIC_CUTOFF_MAIN_ITEM_OFFSET           262

static UCHAR PtpDescriptor[] =
{
    0x05, 0x0d,                         /* Usage Page (Digitizers) */
    0x09, 0x05,                         /* Usage (Touch Pad) */
    0xa1, 0x01,                         /* Collection (Application) */
    0x85, PTP_CAPABILITIES_REPORT_ID,   /*   Report ID (Capabilities) */
    0x09, 0x55,                         /*   Usage (Contact Count Maximum) */
    0x09, 0x59,                         /*   Usage (Pad Type) */
    0x15, 0x00,                         /*   Logical Minimum (0) */
    0x25, 0x0f,                         /*   Logical Maximum (15) */
    0x75, 0x04,                         /*   Report Size (4) */
    0x95, 0x02,                         /*   Report Count (2) */
    0xb1, 0x02,                         /*   Feature (Data, Variable, Absolute) */
    0x06, 0x00, 0xff,                   /*   Usage Page (Vendor Defined 0xff00) */
    0x85, PTP_CERTIFICATION_REPORT_ID,  /*   Report ID (Certification) */
    0x09, 0xc5,                         /*   Usage (Certification Status) */
    0x15, 0x00,                         /*   Logical Minimum (0) */
    0x26, 0xff, 0x00,                   /*   Logical Maximum (255) */
    0x75, 0x08,                         /*   Report Size (8) */
    0x96, 0x00, 0x01,                   /*   Report Count (256) */
    0xb1, 0x02,                         /*   Feature (Data, Variable, Absolute) */
    0xc0                                /* End Collection */
};

static UCHAR PtpDescriptorWithoutButtonType[] =
{
    0x05, 0x0d,                         /* Usage Page (Digitizers) */
    0x09, 0x05,                         /* Usage (Touch Pad) */
    0xa1, 0x01,                         /* Collection (Application) */
    0x85, PTP_CAPABILITIES_REPORT_ID,   /*   Report ID (Capabilities) */
    0x09, 0x55,                         /*   Usage (Contact Count Maximum) */
    0x15, 0x00,                         /*   Logical Minimum (0) */
    0x25, 0x0f,                         /*   Logical Maximum (15) */
    0x75, 0x04,                         /*   Report Size (4) */
    0x95, 0x01,                         /*   Report Count (1) */
    0xb1, 0x02,                         /*   Feature (Data, Variable, Absolute) */
    0x95, 0x01,                         /*   Report Count (1) */
    0xb1, 0x03,                         /*   Feature (Constant, Variable, Absolute) */
    0x06, 0x00, 0xff,                   /*   Usage Page (Vendor Defined 0xff00) */
    0x85, PTP_CERTIFICATION_REPORT_ID,  /*   Report ID (Certification) */
    0x09, 0xc5,                         /*   Usage (Certification Status) */
    0x15, 0x00,                         /*   Logical Minimum (0) */
    0x26, 0xff, 0x00,                   /*   Logical Maximum (255) */
    0x75, 0x08,                         /*   Report Size (8) */
    0x96, 0x00, 0x01,                   /*   Report Count (256) */
    0xb1, 0x02,                         /*   Feature (Data, Variable, Absolute) */
    0xc0                                /* End Collection */
};

static UCHAR PtpDescriptorWithoutCertification[] =
{
    0x05, 0x0d,                         /* Usage Page (Digitizers) */
    0x09, 0x05,                         /* Usage (Touch Pad) */
    0xa1, 0x01,                         /* Collection (Application) */
    0x85, PTP_CAPABILITIES_REPORT_ID,   /*   Report ID (Capabilities) */
    0x09, 0x55,                         /*   Usage (Contact Count Maximum) */
    0x09, 0x59,                         /*   Usage (Pad Type) */
    0x15, 0x00,                         /*   Logical Minimum (0) */
    0x25, 0x0f,                         /*   Logical Maximum (15) */
    0x75, 0x04,                         /*   Report Size (4) */
    0x95, 0x02,                         /*   Report Count (2) */
    0xb1, 0x02,                         /*   Feature (Data, Variable, Absolute) */
    0xc0                                /* End Collection */
};

static UCHAR PtpDescriptorWithShortCertification[] =
{
    0x05, 0x0d,                         /* Usage Page (Digitizers) */
    0x09, 0x05,                         /* Usage (Touch Pad) */
    0xa1, 0x01,                         /* Collection (Application) */
    0x85, PTP_CAPABILITIES_REPORT_ID,   /*   Report ID (Capabilities) */
    0x09, 0x55,                         /*   Usage (Contact Count Maximum) */
    0x09, 0x59,                         /*   Usage (Pad Type) */
    0x15, 0x00,                         /*   Logical Minimum (0) */
    0x25, 0x0f,                         /*   Logical Maximum (15) */
    0x75, 0x04,                         /*   Report Size (4) */
    0x95, 0x02,                         /*   Report Count (2) */
    0xb1, 0x02,                         /*   Feature (Data, Variable, Absolute) */
    0x06, 0x00, 0xff,                   /*   Usage Page (Vendor Defined 0xff00) */
    0x85, PTP_CERTIFICATION_REPORT_ID,  /*   Report ID (Certification) */
    0x09, 0xc5,                         /*   Usage (Certification Status) */
    0x15, 0x00,                         /*   Logical Minimum (0) */
    0x26, 0xff, 0x00,                   /*   Logical Maximum (255) */
    0x75, 0x08,                         /*   Report Size (8) */
    0x95, 0xff,                         /*   Report Count (255) */
    0xb1, 0x02,                         /*   Feature (Data, Variable, Absolute) */
    0xc0                                /* End Collection */
};

static UCHAR NonPtpDescriptor[] =
{
    0x05, 0x01,                         /* Usage Page (Generic Desktop) */
    0x09, 0x02,                         /* Usage (Mouse) */
    0xa1, 0x01,                         /* Collection (Application) */
    0x05, 0x09,                         /*   Usage Page (Button) */
    0x09, 0x01,                         /*   Usage (Button 1) */
    0x15, 0x00,                         /*   Logical Minimum (0) */
    0x25, 0x01,                         /*   Logical Maximum (1) */
    0x75, 0x01,                         /*   Report Size (1) */
    0x95, 0x01,                         /*   Report Count (1) */
    0x81, 0x02,                         /*   Input (Data, Variable, Absolute) */
    0x75, 0x07,                         /*   Report Size (7) */
    0x95, 0x01,                         /*   Report Count (1) */
    0x81, 0x03,                         /*   Input (Constant, Variable, Absolute) */
    0xc0                                /* End Collection */
};

static UCHAR PtpConfigurationDescriptor[] =
{
    0x05, 0x0d,                         /* Usage Page (Digitizers) */
    0x09, 0x0e,                         /* Usage (Device Configuration) */
    0xa1, 0x01,                         /* Collection (Application) */
    0x85, PTP_DEVICE_MODE_REPORT_ID,    /*   Report ID (Device Mode) */
    0x09, 0x22,                         /*   Usage (Finger) */
    0xa1, 0x02,                         /*   Collection (Logical) */
    0x09, 0x52,                         /*     Usage (Device Mode) */
    0x15, 0x00,                         /*     Logical Minimum (0) */
    0x25, 0x0a,                         /*     Logical Maximum (10) */
    0x75, 0x10,                         /*     Report Size (16) */
    0x95, 0x01,                         /*     Report Count (1) */
    0xb1, 0x02,                         /*     Feature (Data, Variable, Absolute) */
    0xc0,                               /*   End Collection */
    0x09, 0x22,                         /*   Usage (Finger) */
    0xa1, 0x00,                         /*   Collection (Physical) */
    0x85, PTP_SELECTIVE_REPORT_ID,      /*     Report ID (Selective Reporting) */
    0x09, 0x57,                         /*     Usage (Surface Switch) */
    0x09, 0x58,                         /*     Usage (Button Switch) */
    0x15, 0x00,                         /*     Logical Minimum (0) */
    0x25, 0x01,                         /*     Logical Maximum (1) */
    0x75, 0x01,                         /*     Report Size (1) */
    0x95, 0x02,                         /*     Report Count (2) */
    0xb1, 0x02,                         /*     Feature (Data, Variable, Absolute) */
    0x95, 0x0e,                         /*     Report Count (14) */
    0xb1, 0x03,                         /*     Feature (Constant, Variable, Absolute) */
    0xc0,                               /*   End Collection */
    0xc0                                /* End Collection */
};

static UCHAR PtpHapticThresholdOnlyDescriptor[] =
{
    0x05, 0x0d,                         /* Usage Page (Digitizers) */
    0x09, 0x05,                         /* Usage (Touch Pad) */
    0xa1, 0x01,                         /* Collection (Application) */
    0x85, 0x40,                         /*   Report ID (Button Threshold) */
    0x05, 0x0d,                         /*   Usage Page (Digitizers) */
    0x09, 0xb0,                         /*   Usage (Button Press Threshold) */
    0x35, 0x6e,                         /*   Physical Minimum (110) */
    0x46, 0xbe, 0x00,                   /*   Physical Maximum (190) */
    0x66, 0x01, 0x01,                   /*   Unit (Gram) */
    0x55, 0x00,                         /*   Unit Exponent (0) */
    0x15, 0x01,                         /*   Logical Minimum (1) */
    0x25, 0x03,                         /*   Logical Maximum (3) */
    0x95, 0x01,                         /*   Report Count (1) */
    0x75, 0x08,                         /*   Report Size (8) */
    0xb1, 0x02,                         /*   Feature (Data, Variable, Absolute) */
    0xc0                                /* End Collection */
};

static UCHAR PtpHapticIntensityOnlyDescriptor[] =
{
    0x05, 0x0d,                         /* Usage Page (Digitizers) */
    0x09, 0x05,                         /* Usage (Touch Pad) */
    0xa1, 0x01,                         /* Collection (Application) */
    0x85, 0x41,                         /*   Report ID (Haptic Intensity) */
    0x05, 0x0e,                         /*   Usage Page (Haptics) */
    0x09, 0x01,                         /*   Usage (Simple Haptic Controller) */
    0xa1, 0x02,                         /*   Collection (Logical) */
    0x05, 0x0e,                         /*     Usage Page (Haptics) */
    0x09, 0x23,                         /*     Usage (Intensity) */
    0x15, 0x00,                         /*     Logical Minimum (0) */
    0x25, 0x04,                         /*     Logical Maximum (4) */
    0x95, 0x01,                         /*     Report Count (1) */
    0x75, 0x08,                         /*     Report Size (8) */
    0xb1, 0x02,                         /*     Feature (Data, Variable, Absolute) */
    0xc0,                               /*   End Collection */
    0xc0                                /* End Collection */
};

typedef struct _PTP_TEST_CONTEXT
{
    UCHAR MaximumContactCount;
    UCHAR ButtonType;
    UCHAR FailingReportId;
    UCHAR CorruptReportId;
    ULONG Calls;
    ULONG CapabilitiesCalls;
    ULONG CertificationCalls;
} PTP_TEST_CONTEXT, *PPTP_TEST_CONTEXT;

typedef struct _PTP_CONFIGURATION_TRANSFER
{
    UCHAR ReportId;
    UCHAR Report[4];
    ULONG ReportLength;
} PTP_CONFIGURATION_TRANSFER, *PPTP_CONFIGURATION_TRANSFER;

typedef struct _PTP_CONFIGURATION_TEST_CONTEXT
{
    ULONG Calls;
    ULONG FailureMask;
    PTP_CONFIGURATION_TRANSFER Transfers[8];
} PTP_CONFIGURATION_TEST_CONTEXT, *PPTP_CONFIGURATION_TEST_CONTEXT;

typedef struct _PTP_HAPTIC_TEST_CONTEXT
{
    NTSTATUS GetStatus;
    NTSTATUS SetStatus;
    NTSTATUS OutputStatus;
    BOOLEAN CorruptReportId;
    ULONG GetCalls;
    ULONG SetCalls;
    ULONG OutputCalls;
    ULONG WaveformUsage[5];
    ULONG DurationMilliseconds[5];
    UCHAR LastReportId;
    UCHAR LastReport[16];
    ULONG LastReportLength;
} PTP_HAPTIC_TEST_CONTEXT, *PPTP_HAPTIC_TEST_CONTEXT;

static
NTSTATUS
NTAPI
PtpTestGetFeature(
    _In_ PVOID Context,
    _In_ UCHAR ReportId,
    _Inout_updates_bytes_(ReportLength) PUCHAR ReportBuffer,
    _In_ ULONG ReportLength)
{
    PPTP_TEST_CONTEXT TestContext = Context;
    ULONG Index;

    TestContext->Calls++;
    if (TestContext->FailingReportId == ReportId)
        return STATUS_IO_DEVICE_ERROR;

    RtlZeroMemory(ReportBuffer, ReportLength);
    ReportBuffer[0] = ReportId;

    if (ReportId == PTP_CAPABILITIES_REPORT_ID)
    {
        TestContext->CapabilitiesCalls++;
        if (ReportLength != 2)
            return STATUS_INFO_LENGTH_MISMATCH;

        ReportBuffer[1] = TestContext->MaximumContactCount |
                          (TestContext->ButtonType << 4);
    }
    else if (ReportId == PTP_CERTIFICATION_REPORT_ID)
    {
        TestContext->CertificationCalls++;
        if (ReportLength != 257)
            return STATUS_INFO_LENGTH_MISMATCH;

        for (Index = 1; Index < ReportLength; Index++)
            ReportBuffer[Index] = (UCHAR)(Index - 1);
    }
    else
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (TestContext->CorruptReportId == ReportId)
        ReportBuffer[0]++;

    return STATUS_SUCCESS;
}

static
NTSTATUS
PtpTestValidateDescriptor(
    _In_reads_bytes_(DescriptorLength) PUCHAR Descriptor,
    _In_ ULONG DescriptorLength,
    _Inout_ PPTP_TEST_CONTEXT TestContext,
    _Out_ PHIDCLASS_PTP_CAPABILITIES Capabilities)
{
    HIDP_DEVICE_DESC DeviceDescription;
    NTSTATUS Status;

    Status = HidP_GetCollectionDescription(Descriptor,
                                           DescriptorLength,
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = HidClassPtpValidateCapabilities(&DeviceDescription,
                                             PtpTestGetFeature,
                                             TestContext,
                                             Capabilities);
    HidP_FreeCollectionDescription(&DeviceDescription);
    return Status;
}

static
NTSTATUS
NTAPI
PtpTestSetFeature(
    _In_ PVOID Context,
    _In_ UCHAR ReportId,
    _In_reads_bytes_(ReportLength) PUCHAR ReportBuffer,
    _In_ ULONG ReportLength)
{
    PPTP_CONFIGURATION_TEST_CONTEXT TestContext = Context;
    PPTP_CONFIGURATION_TRANSFER Transfer;
    ULONG Call = TestContext->Calls++;

    if (Call < RTL_NUMBER_OF(TestContext->Transfers))
    {
        Transfer = &TestContext->Transfers[Call];
        Transfer->ReportId = ReportId;
        Transfer->ReportLength = ReportLength;
        RtlCopyMemory(Transfer->Report,
                      ReportBuffer,
                      min(ReportLength, sizeof(Transfer->Report)));
    }

    if (TestContext->FailureMask & (1UL << Call))
        return STATUS_IO_DEVICE_ERROR;

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
PtpTestGetHapticFeature(
    _In_ PVOID Context,
    _In_ UCHAR ReportId,
    _Inout_updates_bytes_(ReportLength) PUCHAR ReportBuffer,
    _In_ ULONG ReportLength)
{
    PPTP_HAPTIC_TEST_CONTEXT TestContext = Context;
    ULONG Index;

    TestContext->GetCalls++;
    if (!NT_SUCCESS(TestContext->GetStatus))
        return TestContext->GetStatus;
    if (ReportId != 0x42 || ReportLength != 16)
        return STATUS_INFO_LENGTH_MISMATCH;

    RtlZeroMemory(ReportBuffer, ReportLength);
    ReportBuffer[0] = TestContext->CorruptReportId ? ReportId + 1 : ReportId;
    for (Index = 0; Index < RTL_NUMBER_OF(TestContext->WaveformUsage); Index++)
    {
        ReportBuffer[1 + Index * 2] = (UCHAR)TestContext->WaveformUsage[Index];
        ReportBuffer[2 + Index * 2] = (UCHAR)(TestContext->WaveformUsage[Index] >> 8);
        ReportBuffer[11 + Index] = (UCHAR)TestContext->DurationMilliseconds[Index];
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
PtpTestSetHapticFeature(
    _In_ PVOID Context,
    _In_ UCHAR ReportId,
    _In_reads_bytes_(ReportLength) PUCHAR ReportBuffer,
    _In_ ULONG ReportLength)
{
    PPTP_HAPTIC_TEST_CONTEXT TestContext = Context;

    TestContext->SetCalls++;
    TestContext->LastReportId = ReportId;
    TestContext->LastReportLength = ReportLength;
    RtlZeroMemory(TestContext->LastReport, sizeof(TestContext->LastReport));
    RtlCopyMemory(TestContext->LastReport,
                  ReportBuffer,
                  min(ReportLength, sizeof(TestContext->LastReport)));
    return TestContext->SetStatus;
}

static
NTSTATUS
NTAPI
PtpTestWriteHapticOutput(
    _In_ PVOID Context,
    _In_ UCHAR ReportId,
    _In_reads_bytes_(ReportLength) PUCHAR ReportBuffer,
    _In_ ULONG ReportLength)
{
    PPTP_HAPTIC_TEST_CONTEXT TestContext = Context;

    TestContext->OutputCalls++;
    TestContext->LastReportId = ReportId;
    TestContext->LastReportLength = ReportLength;
    RtlZeroMemory(TestContext->LastReport, sizeof(TestContext->LastReport));
    RtlCopyMemory(TestContext->LastReport,
                  ReportBuffer,
                  min(ReportLength, sizeof(TestContext->LastReport)));
    return TestContext->OutputStatus;
}

static
NTSTATUS
PtpTestValidateConfigurationDescriptor(
    _In_reads_bytes_(DescriptorLength) PUCHAR Descriptor,
    _In_ ULONG DescriptorLength,
    _Out_ PHIDCLASS_PTP_CONFIGURATION Configuration)
{
    HIDCLASS_PTP_CAPABILITIES Capabilities;
    HIDP_DEVICE_DESC DeviceDescription;
    NTSTATUS Status;

    Status = HidP_GetCollectionDescription(Descriptor,
                                           DescriptorLength,
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Capabilities, sizeof(Capabilities));
    Capabilities.Present = TRUE;
    Capabilities.Valid = TRUE;
    Status = HidClassPtpInitializeConfiguration(&DeviceDescription,
                                                &Capabilities,
                                                Configuration);
    HidP_FreeCollectionDescription(&DeviceDescription);
    return Status;
}

static
NTSTATUS
PtpTestDiscoverHaptics(
    _In_reads_bytes_(DescriptorLength) PUCHAR Descriptor,
    _In_ ULONG DescriptorLength,
    _Out_ PHIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities)
{
    HIDP_DEVICE_DESC DeviceDescription;
    NTSTATUS Status;

    Status = HidP_GetCollectionDescription(Descriptor,
                                           DescriptorLength,
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = HidClassPtpDiscoverHaptics(&DeviceDescription,
                                        HapticsCapabilities);
    HidP_FreeCollectionDescription(&DeviceDescription);
    return Status;
}

VOID
TestHidClassPtpCapabilities(VOID)
{
    HIDCLASS_PTP_CAPABILITIES Capabilities;
    PTP_TEST_CONTEXT Context;
    NTSTATUS Status;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 5;
    Context.ButtonType = 1;
    Status = PtpTestValidateDescriptor(PtpDescriptor,
                                       sizeof(PtpDescriptor),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Capabilities.Present, "Precision Touchpad was not detected\n");
    ok(Capabilities.Valid, "Precision Touchpad capabilities are not valid\n");
    ok(Capabilities.CertificationReportValid,
       "Precision Touchpad certification report is not valid\n");
    ok(Capabilities.HasButtonType, "Button type was not detected\n");
    ok_eq_uint(Capabilities.MaximumContactCount, 5);
    ok_eq_uint(Capabilities.ButtonType, 1);
    ok_eq_uint(Capabilities.CapabilitiesReportId, PTP_CAPABILITIES_REPORT_ID);
    ok_eq_uint(Capabilities.CertificationReportId, PTP_CERTIFICATION_REPORT_ID);
    ok_eq_ulong(Context.Calls, 2);
    ok_eq_ulong(Context.CapabilitiesCalls, 1);
    ok_eq_ulong(Context.CertificationCalls, 1);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 3;
    Status = PtpTestValidateDescriptor(PtpDescriptorWithoutButtonType,
                                       sizeof(PtpDescriptorWithoutButtonType),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Capabilities.Valid, "External-button touchpad capabilities are not valid\n");
    ok(!Capabilities.HasButtonType, "Optional button type was reported present\n");
    ok_eq_uint(Capabilities.MaximumContactCount, 3);
    ok_eq_ulong(Context.Calls, 2);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 2;
    Status = PtpTestValidateDescriptor(PtpDescriptor,
                                       sizeof(PtpDescriptor),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
    ok(Capabilities.Present, "Invalid touchpad was not detected\n");
    ok(!Capabilities.Valid, "Two-contact touchpad was accepted\n");
    ok_eq_ulong(Context.Calls, 1);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 6;
    Status = PtpTestValidateDescriptor(PtpDescriptor,
                                       sizeof(PtpDescriptor),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
    ok(!Capabilities.Valid, "Six-contact touchpad was accepted\n");
    ok_eq_ulong(Context.Calls, 1);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 5;
    Context.ButtonType = 3;
    Status = PtpTestValidateDescriptor(PtpDescriptor,
                                       sizeof(PtpDescriptor),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
    ok(!Capabilities.Valid, "Invalid button type was accepted\n");
    ok_eq_ulong(Context.Calls, 1);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 5;
    Context.FailingReportId = PTP_CAPABILITIES_REPORT_ID;
    Status = PtpTestValidateDescriptor(PtpDescriptor,
                                       sizeof(PtpDescriptor),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_IO_DEVICE_ERROR);
    ok(!Capabilities.Valid, "Failed capabilities transfer was accepted\n");
    ok_eq_ulong(Context.Calls, 1);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 5;
    Context.FailingReportId = PTP_CERTIFICATION_REPORT_ID;
    Status = PtpTestValidateDescriptor(PtpDescriptor,
                                       sizeof(PtpDescriptor),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_IO_DEVICE_ERROR);
    ok(!Capabilities.Valid, "Failed certification transfer was accepted\n");
    ok_eq_ulong(Context.Calls, 2);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 5;
    Context.CorruptReportId = PTP_CAPABILITIES_REPORT_ID;
    Status = PtpTestValidateDescriptor(PtpDescriptor,
                                       sizeof(PtpDescriptor),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
    ok(!Capabilities.Valid, "Mismatched capabilities report ID was accepted\n");

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 5;
    Context.CorruptReportId = PTP_CERTIFICATION_REPORT_ID;
    Status = PtpTestValidateDescriptor(PtpDescriptor,
                                       sizeof(PtpDescriptor),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
    ok(!Capabilities.Valid, "Mismatched certification report ID was accepted\n");

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 5;
    Status = PtpTestValidateDescriptor(PtpDescriptorWithoutCertification,
                                       sizeof(PtpDescriptorWithoutCertification),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
    ok(!Capabilities.Valid, "Missing certification report was accepted\n");
    ok_eq_ulong(Context.Calls, 1);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.MaximumContactCount = 5;
    Status = PtpTestValidateDescriptor(PtpDescriptorWithShortCertification,
                                       sizeof(PtpDescriptorWithShortCertification),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
    ok(!Capabilities.Valid, "Short certification report was accepted\n");
    ok_eq_ulong(Context.Calls, 1);

    RtlZeroMemory(&Context, sizeof(Context));
    Status = PtpTestValidateDescriptor(NonPtpDescriptor,
                                       sizeof(NonPtpDescriptor),
                                       &Context,
                                       &Capabilities);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    ok(!Capabilities.Present, "Non-touchpad collection was detected as a touchpad\n");
    ok_eq_ulong(Context.Calls, 0);
}

VOID
TestHidClassPtpHaptics(VOID)
{
    static const struct
    {
        USHORT Offset;
        UCHAR Value;
        PCSTR Name;
    } InvalidCases[] =
    {
        {PTP_HAPTIC_THRESHOLD_MAIN_ITEM_OFFSET, 0x03, "missing button threshold"},
        {PTP_HAPTIC_DEVICE_INTENSITY_ID_OFFSET, 0x40, "shared device report ID"},
        {PTP_HAPTIC_DEVICE_COLLECTION_TYPE_OFFSET, 0x00, "non-logical intensity collection"},
        {PTP_HAPTIC_DEVICE_INTENSITY_MAXIMUM_OFFSET, 0x03, "three-level intensity"},
        {PTP_HAPTIC_DEVICE_INTENSITY_MAIN_OFFSET, 0x03, "missing device intensity"},
        {PTP_HAPTIC_WAVEFORM_REPORT_COUNT_OFFSET, 0x06, "waveform ordinal mismatch"},
        {PTP_HAPTIC_DURATION_USAGE_MAXIMUM_OFFSET, 0x06, "duration ordinal mismatch"},
        {PTP_HAPTIC_DURATION_UNIT_EXPONENT_OFFSET, 0x00, "non-millisecond duration"},
        {PTP_HAPTIC_MANUAL_USAGE_OFFSET, 0x20, "forbidden auto trigger"},
        {PTP_HAPTIC_MANUAL_MAIN_ITEM_OFFSET, 0x03, "missing manual trigger"},
        {PTP_HAPTIC_HOST_INTENSITY_MAIN_OFFSET, 0x03, "missing host intensity"},
        {PTP_HAPTIC_REPEAT_MAIN_ITEM_OFFSET, 0x03, "partial repeat controls"},
        {PTP_HAPTIC_CUTOFF_MAIN_ITEM_OFFSET, 0x03, "partial cutoff controls"}
    };
    HIDCLASS_PTP_HAPTICS_CAPABILITIES HapticsCapabilities;
    HIDCLASS_PTP_HAPTIC_WAVEFORM Waveforms[5];
    HIDCLASS_PTP_HAPTIC_OUTPUT HapticOutput;
    PTP_HAPTIC_TEST_CONTEXT HapticContext;
    HIDP_DEVICE_DESC DeviceDescription;
    UCHAR Descriptor[sizeof(HidPTestHapticTouchpadDescriptor)];
    ULONG WaveformCount;
    ULONG Calls;
    ULONG Index;
    NTSTATUS Status;

    Status = HidClassPtpDiscoverHaptics(NULL, &HapticsCapabilities);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = PtpTestDiscoverHaptics(HidPTestHapticTouchpadDescriptor,
                                    sizeof(HidPTestHapticTouchpadDescriptor),
                                    &HapticsCapabilities);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(HapticsCapabilities.Present, "Haptic touchpad was not detected\n");
    ok(HapticsCapabilities.Valid, "Haptic touchpad was not validated\n");
    ok(HapticsCapabilities.HasButtonPressThreshold,
       "Button Press Threshold report was not discovered\n");
    ok(HapticsCapabilities.HasDeviceIntensity,
       "Device-initiated intensity report was not discovered\n");
    ok(HapticsCapabilities.HasHostInitiated,
       "Host-initiated reports were not discovered\n");
    ok(HapticsCapabilities.HasRepeatControl,
       "Optional repeat controls were not discovered\n");
    ok_eq_uint(HapticsCapabilities.CollectionNumber, 1);

    ok_eq_uint(HapticsCapabilities.ButtonPressThreshold.ValueCaps.ReportID, 0x40);
    ok_eq_uint(HapticsCapabilities.ButtonPressThreshold.ValueCaps.LinkCollection, 0);
    ok_eq_uint(HapticsCapabilities.ButtonPressThreshold.ReportLength, 2);
    ok_eq_long(HapticsCapabilities.ButtonPressThreshold.ValueCaps.LogicalMin, 1);
    ok_eq_long(HapticsCapabilities.ButtonPressThreshold.ValueCaps.LogicalMax, 3);
    ok_eq_long(HapticsCapabilities.ButtonPressThreshold.ValueCaps.PhysicalMin, 110);
    ok_eq_long(HapticsCapabilities.ButtonPressThreshold.ValueCaps.PhysicalMax, 190);

    ok_eq_uint(HapticsCapabilities.DeviceIntensity.ValueCaps.ReportID, 0x41);
    ok_eq_uint(HapticsCapabilities.DeviceIntensity.ValueCaps.LinkCollection, 1);
    ok_eq_uint(HapticsCapabilities.DeviceIntensity.ReportLength, 2);
    ok_eq_long(HapticsCapabilities.DeviceIntensity.ValueCaps.LogicalMin, 0);
    ok_eq_long(HapticsCapabilities.DeviceIntensity.ValueCaps.LogicalMax, 4);

    ok_eq_uint(HapticsCapabilities.WaveformList.ReportId, 0x42);
    ok_eq_uint(HapticsCapabilities.WaveformList.LinkCollection, 3);
    ok_eq_uint(HapticsCapabilities.WaveformList.ReportLength, 16);
    ok_eq_uint(HapticsCapabilities.WaveformList.UsageMinimum, 3);
    ok_eq_uint(HapticsCapabilities.WaveformList.UsageMaximum, 7);
    ok_eq_uint(HapticsCapabilities.WaveformList.ReportCount, 5);
    ok_eq_uint(HapticsCapabilities.DurationList.ReportId, 0x42);
    ok_eq_uint(HapticsCapabilities.DurationList.LinkCollection, 4);
    ok_eq_uint(HapticsCapabilities.DurationList.ReportLength, 16);
    ok_eq_long(HapticsCapabilities.DurationList.LogicalMinimum, 0);
    ok_eq_long(HapticsCapabilities.DurationList.LogicalMaximum, 50);
    ok_eq_ulong(HapticsCapabilities.DurationList.Units, 0x1001);
    ok_eq_ulong(HapticsCapabilities.DurationList.UnitsExponent, 0x0d);

    ok_eq_uint(HapticsCapabilities.ManualTrigger.ValueCaps.ReportID, 0x43);
    ok_eq_uint(HapticsCapabilities.ManualTrigger.ValueCaps.LinkCollection, 5);
    ok_eq_uint(HapticsCapabilities.ManualTrigger.ReportLength, 8);
    ok_eq_long(HapticsCapabilities.ManualTrigger.ValueCaps.LogicalMin, 1);
    ok_eq_long(HapticsCapabilities.ManualTrigger.ValueCaps.LogicalMax, 7);
    ok_eq_uint(HapticsCapabilities.HostIntensity.ValueCaps.ReportID, 0x43);
    ok_eq_uint(HapticsCapabilities.RepeatCount.ValueCaps.ReportID, 0x43);
    ok_eq_uint(HapticsCapabilities.RetriggerPeriod.ValueCaps.ReportID, 0x43);
    ok_eq_uint(HapticsCapabilities.WaveformCutoffTime.ValueCaps.ReportID, 0x43);

    RtlZeroMemory(&HapticContext, sizeof(HapticContext));
    Status = HidP_GetCollectionDescription(HidPTestHapticTouchpadDescriptor,
                                           sizeof(HidPTestHapticTouchpadDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        HapticContext.WaveformUsage[0] = HID_USAGE_HAPTICS_WAVEFORM_PRESS;
        HapticContext.WaveformUsage[1] = HID_USAGE_HAPTICS_WAVEFORM_RELEASE;
        HapticContext.WaveformUsage[2] = HID_USAGE_HAPTICS_WAVEFORM_HOVER;
        HapticContext.WaveformUsage[3] = HID_USAGE_HAPTICS_WAVEFORM_SUCCESS;
        HapticContext.WaveformUsage[4] = HID_USAGE_HAPTICS_WAVEFORM_ERROR;
        HapticContext.DurationMilliseconds[0] = 50;
        HapticContext.DurationMilliseconds[1] = 50;
        HapticContext.DurationMilliseconds[2] = 20;
        HapticContext.DurationMilliseconds[3] = 30;
        HapticContext.DurationMilliseconds[4] = 30;

        WaveformCount = 0;
        Status = HidClassPtpGetHapticWaveforms(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     PtpTestGetHapticFeature,
                     &HapticContext,
                     NULL,
                     0,
                     &WaveformCount);
        ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
        ok_eq_ulong(WaveformCount, RTL_NUMBER_OF(Waveforms));
        ok_eq_ulong(HapticContext.GetCalls, 0);

        WaveformCount = 0;
        Status = HidClassPtpGetHapticWaveforms(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     PtpTestGetHapticFeature,
                     &HapticContext,
                     Waveforms,
                     RTL_NUMBER_OF(Waveforms) - 1,
                     &WaveformCount);
        ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
        ok_eq_ulong(WaveformCount, RTL_NUMBER_OF(Waveforms));
        ok_eq_ulong(HapticContext.GetCalls, 0);

        WaveformCount = 0;
        Status = HidClassPtpGetHapticWaveforms(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     PtpTestGetHapticFeature,
                     &HapticContext,
                     Waveforms,
                     RTL_NUMBER_OF(Waveforms),
                     &WaveformCount);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(WaveformCount, RTL_NUMBER_OF(Waveforms));
        ok_eq_ulong(HapticContext.GetCalls, 1);
        for (Index = 0; Index < RTL_NUMBER_OF(Waveforms); Index++)
        {
            ok_eq_ulong(Waveforms[Index].Ordinal, Index + 3);
            ok_eq_ulong(Waveforms[Index].WaveformUsage,
                        HapticContext.WaveformUsage[Index]);
            ok_eq_ulong(Waveforms[Index].DurationMilliseconds,
                        HapticContext.DurationMilliseconds[Index]);
        }

        Status = HidClassPtpSetHapticButtonPressThreshold(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     2,
                     PtpTestSetHapticFeature,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(HapticContext.SetCalls, 1);
        ok_eq_uint(HapticContext.LastReportId, 0x40);
        ok_eq_ulong(HapticContext.LastReportLength, 2);
        ok_eq_uint(HapticContext.LastReport[0], 0x40);
        ok_eq_uint(HapticContext.LastReport[1], 2);

        Calls = HapticContext.SetCalls;
        Status = HidClassPtpSetHapticButtonPressThreshold(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     0,
                     PtpTestSetHapticFeature,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        ok_eq_ulong(HapticContext.SetCalls, Calls);

        Status = HidClassPtpSetDeviceHapticIntensity(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     4,
                     PtpTestSetHapticFeature,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(HapticContext.SetCalls, Calls + 1);
        ok_eq_uint(HapticContext.LastReportId, 0x41);
        ok_eq_ulong(HapticContext.LastReportLength, 2);
        ok_eq_uint(HapticContext.LastReport[0], 0x41);
        ok_eq_uint(HapticContext.LastReport[1], 4);

        Calls = HapticContext.SetCalls;
        Status = HidClassPtpSetDeviceHapticIntensity(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     5,
                     PtpTestSetHapticFeature,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        ok_eq_ulong(HapticContext.SetCalls, Calls);

        HapticContext.SetStatus = STATUS_IO_DEVICE_ERROR;
        Status = HidClassPtpSetDeviceHapticIntensity(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     3,
                     PtpTestSetHapticFeature,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_IO_DEVICE_ERROR);
        ok_eq_ulong(HapticContext.SetCalls, Calls + 1);
        HapticContext.SetStatus = STATUS_SUCCESS;

        RtlZeroMemory(&HapticOutput, sizeof(HapticOutput));
        HapticOutput.WaveformOrdinal = 5;
        HapticOutput.Intensity = 4;
        HapticOutput.RepeatCount = 2;
        HapticOutput.RetriggerPeriodMilliseconds = 500;
        HapticOutput.WaveformCutoffTimeMilliseconds = 3000;
        Status = HidClassPtpSendHapticOutput(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     &HapticOutput,
                     PtpTestWriteHapticOutput,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(HapticContext.OutputCalls, 1);
        ok_eq_uint(HapticContext.LastReportId, 0x43);
        ok_eq_ulong(HapticContext.LastReportLength, 8);
        ok_eq_uint(HapticContext.LastReport[0], 0x43);
        ok_eq_uint(HapticContext.LastReport[1], 5);
        ok_eq_uint(HapticContext.LastReport[2], 4);
        ok_eq_uint(HapticContext.LastReport[3], 2);
        ok_eq_uint(HapticContext.LastReport[4], 0xf4);
        ok_eq_uint(HapticContext.LastReport[5], 0x01);
        ok_eq_uint(HapticContext.LastReport[6], 0xb8);
        ok_eq_uint(HapticContext.LastReport[7], 0x0b);

        Calls = HapticContext.OutputCalls;
        HapticOutput.Intensity = 0;
        Status = HidClassPtpSendHapticOutput(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     &HapticOutput,
                     PtpTestWriteHapticOutput,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        ok_eq_ulong(HapticContext.OutputCalls, Calls);

        HapticOutput.WaveformOrdinal = HIDCLASS_PTP_HAPTIC_STOP_ORDINAL;
        HapticOutput.RepeatCount = 0;
        HapticOutput.RetriggerPeriodMilliseconds = 0;
        HapticOutput.WaveformCutoffTimeMilliseconds = 1000;
        Status = HidClassPtpSendHapticOutput(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     &HapticOutput,
                     PtpTestWriteHapticOutput,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(HapticContext.OutputCalls, Calls + 1);
        ok_eq_uint(HapticContext.LastReport[1], HIDCLASS_PTP_HAPTIC_STOP_ORDINAL);
        ok_eq_uint(HapticContext.LastReport[2], 0);

        Calls = HapticContext.OutputCalls;
        HapticOutput.WaveformOrdinal = 5;
        HapticOutput.Intensity = 4;
        HapticOutput.WaveformCutoffTimeMilliseconds = 999;
        Status = HidClassPtpSendHapticOutput(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     &HapticOutput,
                     PtpTestWriteHapticOutput,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        ok_eq_ulong(HapticContext.OutputCalls, Calls);

        HapticOutput.WaveformCutoffTimeMilliseconds = 3000;
        HapticContext.OutputStatus = STATUS_IO_DEVICE_ERROR;
        Status = HidClassPtpSendHapticOutput(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     &HapticOutput,
                     PtpTestWriteHapticOutput,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_IO_DEVICE_ERROR);
        ok_eq_ulong(HapticContext.OutputCalls, Calls + 1);
        HapticContext.OutputStatus = STATUS_SUCCESS;

        HapticContext.CorruptReportId = TRUE;
        WaveformCount = 0;
        Status = HidClassPtpGetHapticWaveforms(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     PtpTestGetHapticFeature,
                     &HapticContext,
                     Waveforms,
                     RTL_NUMBER_OF(Waveforms),
                     &WaveformCount);
        ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
        ok_eq_ulong(Waveforms[0].Ordinal, 0);
        HapticContext.CorruptReportId = FALSE;

        HapticContext.GetStatus = STATUS_IO_DEVICE_ERROR;
        Status = HidClassPtpGetHapticWaveforms(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     PtpTestGetHapticFeature,
                     &HapticContext,
                     Waveforms,
                     RTL_NUMBER_OF(Waveforms),
                     &WaveformCount);
        ok_eq_hex(Status, STATUS_IO_DEVICE_ERROR);
        ok_eq_ulong(Waveforms[0].Ordinal, 0);
        HapticContext.GetStatus = STATUS_SUCCESS;

        HapticContext.WaveformUsage[0] = HID_USAGE_HAPTICS_WAVEFORM_CLICK;
        Status = HidClassPtpGetHapticWaveforms(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     PtpTestGetHapticFeature,
                     &HapticContext,
                     Waveforms,
                     RTL_NUMBER_OF(Waveforms),
                     &WaveformCount);
        ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
        HapticContext.WaveformUsage[0] = HID_USAGE_HAPTICS_WAVEFORM_PRESS;

        HapticContext.DurationMilliseconds[0] = 0;
        Status = HidClassPtpGetHapticWaveforms(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     PtpTestGetHapticFeature,
                     &HapticContext,
                     Waveforms,
                     RTL_NUMBER_OF(Waveforms),
                     &WaveformCount);
        ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
        HapticContext.DurationMilliseconds[0] = 50;

        HapticContext.WaveformUsage[1] = HID_USAGE_HAPTICS_WAVEFORM_HOVER;
        Status = HidClassPtpGetHapticWaveforms(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     PtpTestGetHapticFeature,
                     &HapticContext,
                     Waveforms,
                     RTL_NUMBER_OF(Waveforms),
                     &WaveformCount);
        ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
        HapticContext.WaveformUsage[1] = HID_USAGE_HAPTICS_WAVEFORM_RELEASE;

        HidP_FreeCollectionDescription(&DeviceDescription);
    }

    Status = PtpTestDiscoverHaptics(PtpHapticThresholdOnlyDescriptor,
                                    sizeof(PtpHapticThresholdOnlyDescriptor),
                                    &HapticsCapabilities);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(HapticsCapabilities.Valid, "Threshold-only haptics were rejected\n");
    ok(HapticsCapabilities.HasButtonPressThreshold,
       "Threshold-only report was not discovered\n");
    ok(!HapticsCapabilities.HasDeviceIntensity,
       "Threshold-only descriptor gained intensity support\n");
    ok(!HapticsCapabilities.HasHostInitiated,
       "Threshold-only descriptor gained host-initiated support\n");

    Status = PtpTestDiscoverHaptics(PtpHapticIntensityOnlyDescriptor,
                                    sizeof(PtpHapticIntensityOnlyDescriptor),
                                    &HapticsCapabilities);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(HapticsCapabilities.Valid, "Intensity-only haptics were rejected\n");
    ok(!HapticsCapabilities.HasButtonPressThreshold,
       "Intensity-only descriptor gained threshold support\n");
    ok(HapticsCapabilities.HasDeviceIntensity,
       "Intensity-only report was not discovered\n");
    ok(!HapticsCapabilities.HasHostInitiated,
       "Intensity-only descriptor gained host-initiated support\n");

    Status = PtpTestDiscoverHaptics(PtpDescriptor,
                                    sizeof(PtpDescriptor),
                                    &HapticsCapabilities);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    ok(!HapticsCapabilities.Present,
       "A non-haptic Precision Touchpad gained haptic capabilities\n");
    ok(!HapticsCapabilities.Valid,
       "A non-haptic Precision Touchpad gained valid haptics\n");

    RtlCopyMemory(Descriptor,
                  HidPTestHapticTouchpadDescriptor,
                  sizeof(Descriptor));
    Descriptor[PTP_HAPTIC_REPEAT_MAIN_ITEM_OFFSET] = 0x03;
    Descriptor[PTP_HAPTIC_RETRIGGER_MAIN_ITEM_OFFSET] = 0x03;
    Descriptor[PTP_HAPTIC_CUTOFF_MAIN_ITEM_OFFSET] = 0x03;
    Status = PtpTestDiscoverHaptics(Descriptor,
                                    sizeof(Descriptor),
                                    &HapticsCapabilities);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(HapticsCapabilities.HasHostInitiated,
       "Host haptics without optional repeat controls were rejected\n");
    ok(!HapticsCapabilities.HasRepeatControl,
       "Constant optional fields were reported as repeat controls\n");

    Status = HidP_GetCollectionDescription(Descriptor,
                                           sizeof(Descriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&HapticOutput, sizeof(HapticOutput));
        HapticOutput.WaveformOrdinal = 5;
        HapticOutput.Intensity = 4;
        Calls = HapticContext.OutputCalls;
        Status = HidClassPtpSendHapticOutput(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     &HapticOutput,
                     PtpTestWriteHapticOutput,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(HapticContext.OutputCalls, Calls + 1);
        ok_eq_uint(HapticContext.LastReport[1], 5);
        ok_eq_uint(HapticContext.LastReport[2], 4);
        ok_eq_uint(HapticContext.LastReport[3], 0);
        ok_eq_uint(HapticContext.LastReport[7], 0);

        Calls = HapticContext.OutputCalls;
        HapticOutput.RepeatCount = 1;
        Status = HidClassPtpSendHapticOutput(
                     &DeviceDescription,
                     &HapticsCapabilities,
                     &HapticOutput,
                     PtpTestWriteHapticOutput,
                     &HapticContext);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        ok_eq_ulong(HapticContext.OutputCalls, Calls);
        HidP_FreeCollectionDescription(&DeviceDescription);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(InvalidCases); Index++)
    {
        RtlCopyMemory(Descriptor,
                      HidPTestHapticTouchpadDescriptor,
                      sizeof(Descriptor));
        Descriptor[InvalidCases[Index].Offset] = InvalidCases[Index].Value;
        Status = PtpTestDiscoverHaptics(Descriptor,
                                        sizeof(Descriptor),
                                        &HapticsCapabilities);
        ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR,
           "%s returned %lx instead of STATUS_DEVICE_CONFIGURATION_ERROR\n",
           InvalidCases[Index].Name,
           Status);
        ok(HapticsCapabilities.Present,
           "%s was not recognized as malformed haptics\n",
           InvalidCases[Index].Name);
        ok(!HapticsCapabilities.Valid,
           "%s was accepted as valid haptics\n",
           InvalidCases[Index].Name);
    }

    Status = HidP_GetCollectionDescription(HidPTestHapticTouchpadDescriptor,
                                           sizeof(HidPTestHapticTouchpadDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < DeviceDescription.ReportIDsLength; Index++)
        {
            if (DeviceDescription.ReportIDs[Index].ReportID == 0x43)
                DeviceDescription.ReportIDs[Index].OutputLength = 1;
        }
        Status = HidClassPtpDiscoverHaptics(&DeviceDescription,
                                            &HapticsCapabilities);
        ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
        ok(HapticsCapabilities.Present,
           "The short output report was not recognized as haptics\n");
        ok(!HapticsCapabilities.Valid,
           "The short output report was accepted\n");
        HidP_FreeCollectionDescription(&DeviceDescription);
    }
}

VOID
TestHidClassPtpConfiguration(VOID)
{
    HIDCLASS_PTP_CONFIGURATION Configuration;
    HIDCLASS_PTP_CAPABILITIES Capabilities;
    PTP_CONFIGURATION_TEST_CONTEXT Context;
    HIDP_DEVICE_DESC DeviceDescription;
    UCHAR Descriptor[sizeof(PtpConfigurationDescriptor)];
    ULONG Selection;
    ULONG Index;
    NTSTATUS Status;

    ok_eq_uint(PtpConfigurationDescriptor[PTP_CONFIG_MODE_MAXIMUM_OFFSET], 0x0a);
    ok_eq_uint(PtpConfigurationDescriptor[PTP_CONFIG_MODE_SIZE_OFFSET], 0x10);
    ok_eq_uint(PtpConfigurationDescriptor[PTP_CONFIG_SELECTIVE_ID_OFFSET], PTP_SELECTIVE_REPORT_ID);
    ok_eq_uint(PtpConfigurationDescriptor[PTP_CONFIG_BUTTON_USAGE_OFFSET], 0x58);
    ok_eq_uint(PtpConfigurationDescriptor[PTP_CONFIG_SELECTIVE_MAXIMUM_OFFSET], 0x01);

    Status = HidP_GetCollectionDescription(PtpConfigurationDescriptor,
                                           sizeof(PtpConfigurationDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlZeroMemory(&Capabilities, sizeof(Capabilities));
    Capabilities.Present = TRUE;
    Capabilities.Valid = TRUE;
    Status = HidClassPtpInitializeConfiguration(&DeviceDescription,
                                                &Capabilities,
                                                &Configuration);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Configuration.Valid, "Configuration descriptor was not validated\n");
    ok(!Configuration.DeviceModeKnown, "Device mode was recorded before a successful report\n");
    ok(!Configuration.SelectiveReportingKnown,
       "Selective-reporting state was recorded before a successful report\n");
    ok_eq_uint(Configuration.CollectionNumber, 1);
    ok_eq_uint(Configuration.DeviceModeReportId, PTP_DEVICE_MODE_REPORT_ID);
    ok_eq_uint(Configuration.SelectiveReportingReportId, PTP_SELECTIVE_REPORT_ID);

    RtlZeroMemory(&Context, sizeof(Context));
    Status = HidClassPtpSetDeviceMode(&DeviceDescription,
                                      &Configuration,
                                      3,
                                      PtpTestSetFeature,
                                      &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Configuration.DeviceModeKnown, "Successful mode report was not recorded\n");
    ok_eq_uint(Configuration.DeviceMode, 3);
    ok_eq_ulong(Context.Calls, 1);
    ok_eq_uint(Context.Transfers[0].ReportId, PTP_DEVICE_MODE_REPORT_ID);
    ok_eq_ulong(Context.Transfers[0].ReportLength, 3);
    ok_eq_uint(Context.Transfers[0].Report[0], PTP_DEVICE_MODE_REPORT_ID);
    ok_eq_uint(Context.Transfers[0].Report[1], 3);
    ok_eq_uint(Context.Transfers[0].Report[2], 0);

    Status = HidClassPtpSetDeviceMode(&DeviceDescription,
                                      &Configuration,
                                      0,
                                      PtpTestSetFeature,
                                      &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_uint(Configuration.DeviceMode, 0);
    ok_eq_ulong(Context.Calls, 2);
    ok_eq_uint(Context.Transfers[1].Report[1], 0);

    Status = HidClassPtpSetDeviceMode(&DeviceDescription,
                                      &Configuration,
                                      2,
                                      PtpTestSetFeature,
                                      &Context);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_uint(Configuration.DeviceMode, 0);
    ok_eq_ulong(Context.Calls, 2);

    for (Selection = 0; Selection < 4; Selection++)
    {
        RtlZeroMemory(&Context, sizeof(Context));
        Status = HidClassPtpSetSelectiveReporting(&DeviceDescription,
                                                  &Configuration,
                                                  !!(Selection & 1),
                                                  !!(Selection & 2),
                                                  PtpTestSetFeature,
                                                  &Context);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(Configuration.SelectiveReportingKnown,
           "Successful selective report was not recorded\n");
        ok_eq_uint(Configuration.SurfaceReportingEnabled, !!(Selection & 1));
        ok_eq_uint(Configuration.ButtonReportingEnabled, !!(Selection & 2));
        ok_eq_ulong(Context.Calls, 1);
        ok_eq_uint(Context.Transfers[0].ReportId, PTP_SELECTIVE_REPORT_ID);
        ok_eq_ulong(Context.Transfers[0].ReportLength, 3);
        ok_eq_uint(Context.Transfers[0].Report[0], PTP_SELECTIVE_REPORT_ID);
        ok_eq_uint(Context.Transfers[0].Report[1] & 3, Selection);
        ok_eq_uint(Context.Transfers[0].Report[2], 0);
    }

    /* A failed transfer leaves the corresponding applied state unknown. */
    RtlZeroMemory(&Context, sizeof(Context));
    Context.FailureMask = 1;
    Status = HidClassPtpSetDeviceMode(&DeviceDescription,
                                      &Configuration,
                                      3,
                                      PtpTestSetFeature,
                                      &Context);
    ok_eq_hex(Status, STATUS_IO_DEVICE_ERROR);
    ok(!Configuration.DeviceModeKnown, "Failed mode report left state known\n");
    ok_eq_ulong(Context.Calls, 1);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.FailureMask = 1;
    Status = HidClassPtpSetSelectiveReporting(&DeviceDescription,
                                              &Configuration,
                                              FALSE,
                                              FALSE,
                                              PtpTestSetFeature,
                                              &Context);
    ok_eq_hex(Status, STATUS_IO_DEVICE_ERROR);
    ok(!Configuration.SelectiveReportingKnown,
       "Failed selective report left state known\n");
    ok_eq_ulong(Context.Calls, 1);

    HidP_FreeCollectionDescription(&DeviceDescription);

    /* An eight-bit Device Mode field and a high report ID are both valid. */
    RtlCopyMemory(Descriptor,
                  PtpConfigurationDescriptor,
                  sizeof(PtpConfigurationDescriptor));
    Descriptor[PTP_CONFIG_MODE_SIZE_OFFSET] = 8;
    Descriptor[PTP_CONFIG_SELECTIVE_ID_OFFSET] = 0x15;
    Status = HidP_GetCollectionDescription(Descriptor,
                                           sizeof(Descriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = HidClassPtpInitializeConfiguration(&DeviceDescription,
                                                &Capabilities,
                                                &Configuration);
    ok_eq_hex(Status, STATUS_SUCCESS);
    RtlZeroMemory(&Context, sizeof(Context));
    Status = HidClassPtpSetDeviceMode(&DeviceDescription,
                                      &Configuration,
                                      3,
                                      PtpTestSetFeature,
                                      &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = HidClassPtpSetSelectiveReporting(&DeviceDescription,
                                              &Configuration,
                                              TRUE,
                                              TRUE,
                                              PtpTestSetFeature,
                                              &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Context.Calls, 2);
    ok_eq_ulong(Context.Transfers[0].ReportLength, 2);
    ok_eq_uint(Context.Transfers[0].Report[1], 3);
    ok_eq_uint(Context.Transfers[1].ReportId, 0x15);
    HidP_FreeCollectionDescription(&DeviceDescription);

    /* Invalid PTP capability state must not validate the configuration. */
    Status = HidP_GetCollectionDescription(PtpConfigurationDescriptor,
                                           sizeof(PtpConfigurationDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Capabilities, sizeof(Capabilities));
        Status = HidClassPtpInitializeConfiguration(&DeviceDescription,
                                                    &Capabilities,
                                                    &Configuration);
        ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
        HidP_FreeCollectionDescription(&DeviceDescription);
    }

    /* Reject incomplete or contradictory Windows PTP descriptors. */
    Status = PtpTestValidateConfigurationDescriptor(NonPtpDescriptor,
                                                    sizeof(NonPtpDescriptor),
                                                    &Configuration);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);

    RtlCopyMemory(Descriptor,
                  PtpConfigurationDescriptor,
                  sizeof(PtpConfigurationDescriptor));
    Descriptor[PTP_CONFIG_MODE_MAXIMUM_OFFSET] = 2;
    Status = PtpTestValidateConfigurationDescriptor(Descriptor,
                                                    sizeof(Descriptor),
                                                    &Configuration);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);

    RtlCopyMemory(Descriptor,
                  PtpConfigurationDescriptor,
                  sizeof(PtpConfigurationDescriptor));
    Descriptor[PTP_CONFIG_BUTTON_USAGE_OFFSET] = 0x57;
    Status = PtpTestValidateConfigurationDescriptor(Descriptor,
                                                    sizeof(Descriptor),
                                                    &Configuration);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);

    RtlCopyMemory(Descriptor,
                  PtpConfigurationDescriptor,
                  sizeof(PtpConfigurationDescriptor));
    Descriptor[PTP_CONFIG_SELECTIVE_MAXIMUM_OFFSET] = 2;
    Status = PtpTestValidateConfigurationDescriptor(Descriptor,
                                                    sizeof(Descriptor),
                                                    &Configuration);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);

    RtlCopyMemory(Descriptor,
                  PtpConfigurationDescriptor,
                  sizeof(PtpConfigurationDescriptor));
    Descriptor[PTP_CONFIG_SELECTIVE_ID_OFFSET] = PTP_DEVICE_MODE_REPORT_ID;
    Status = PtpTestValidateConfigurationDescriptor(Descriptor,
                                                    sizeof(Descriptor),
                                                    &Configuration);
    ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);

    /* A parser-visible short report is rejected before the first transfer. */
    Status = HidP_GetCollectionDescription(PtpConfigurationDescriptor,
                                           sizeof(PtpConfigurationDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Capabilities, sizeof(Capabilities));
        Capabilities.Present = TRUE;
        Capabilities.Valid = TRUE;
        for (Index = 0; Index < DeviceDescription.ReportIDsLength; Index++)
        {
            if (DeviceDescription.ReportIDs[Index].ReportID == PTP_SELECTIVE_REPORT_ID)
                DeviceDescription.ReportIDs[Index].FeatureLength = 1;
        }
        Status = HidClassPtpInitializeConfiguration(&DeviceDescription,
                                                    &Capabilities,
                                                    &Configuration);
        ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
        HidP_FreeCollectionDescription(&DeviceDescription);
    }
}
