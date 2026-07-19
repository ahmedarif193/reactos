/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     HIDCLASS Precision Touchpad capability policy tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <hidpddi.h>

#include "ptp.h"

#define PTP_CAPABILITIES_REPORT_ID   2
#define PTP_CERTIFICATION_REPORT_ID  6

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
