/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite ETW provider registration API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static const GUID KmtEtwProviderGuid = { 0x9f5ebcd1, 0x6c2f, 0x4f93, { 0xb1, 0x07, 0x42, 0x88, 0x1f, 0x3a, 0x5e, 0x21 } };

START_TEST(EtwRegisterKM)
{
    REGHANDLE RegHandle = 0;
    EVENT_DESCRIPTOR EventDescriptor;
    NTSTATUS Status;

    Status = EtwRegister(&KmtEtwProviderGuid, NULL, NULL, &RegHandle);
    if (Status == STATUS_NOT_IMPLEMENTED)
    {
        skip(FALSE, "ETW provider registration not available in this context\n");
        return;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ok(RegHandle != 0, "reg handle zero\n");

    RtlZeroMemory(&EventDescriptor, sizeof(EventDescriptor));
    EventDescriptor.Id = 1;
    EventDescriptor.Version = 0;
    EventDescriptor.Level = 4;

    Status = EtwWrite(RegHandle, &EventDescriptor, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = EtwUnregister(RegHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
}
