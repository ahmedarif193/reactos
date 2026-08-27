/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite process information API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

typedef struct _TEST_PROCESS_TELEMETRY_ID_INFORMATION
{
    ULONG HeaderSize;
    ULONG ProcessId;
    ULONGLONG ProcessStartKey;
    ULONGLONG CreateTime;
    ULONGLONG CreateInterruptTime;
    ULONGLONG CreateUnbiasedInterruptTime;
    ULONGLONG ProcessSequenceNumber;
    ULONGLONG SessionCreateTime;
    ULONG SessionId;
    ULONG BootId;
    ULONG ImageChecksum;
    ULONG ImageTimeDateStamp;
    ULONG UserSidOffset;
    ULONG ImagePathOffset;
    ULONG PackageNameOffset;
    ULONG RelativeAppNameOffset;
    ULONG CommandLineOffset;
} TEST_PROCESS_TELEMETRY_ID_INFORMATION,
 *PTEST_PROCESS_TELEMETRY_ID_INFORMATION;

C_ASSERT(sizeof(TEST_PROCESS_TELEMETRY_ID_INFORMATION) == 0x60);

static
VOID
TestImageFileName(VOID)
{
    PCHAR Name;

    Name = (PCHAR)PsGetProcessImageFileName(PsGetCurrentProcess());
    ok(Name != NULL, "image name NULL\n");
    if (Name == NULL) return;

    ok(strstr(Name, "kmtest") != NULL, "unexpected process image: %s\n", Name);

    Name = (PCHAR)PsGetProcessImageFileName(PsInitialSystemProcess);
    ok(Name != NULL, "system image name NULL\n");
    if (Name != NULL)
        ok(_stricmp(Name, "System") == 0, "system process named %s\n", Name);
}

static
VOID
TestPreviousMode(VOID)
{
    ok_eq_uint(ExGetPreviousMode(), UserMode);
}

static
VOID
TestProcessFlags(VOID)
{
    ok_bool_false(PsIsSystemThread(PsGetCurrentThread()), "ioctl thread is system");
    ok_bool_false(PsGetCurrentProcessWow64Process() != NULL, "wow64 process");
    ok_eq_pointer(PsGetProcessWow64Process(PsGetCurrentProcess()), NULL);
    ok_eq_pointer(PsGetCurrentThreadProcess(), PsGetCurrentProcess());
    ok_eq_pointer(PsGetCurrentThreadProcessId(), PsGetCurrentProcessId());
}

static
VOID
TestExitStatusQueries(VOID)
{
    ok_eq_hex(PsGetProcessExitStatus(PsGetCurrentProcess()), STATUS_PENDING);
}

static
VOID
TestModernProcessInformation(VOID)
{
    TEST_PROCESS_TELEMETRY_ID_INFORMATION Header;
    PTEST_PROCESS_TELEMETRY_ID_INFORMATION Information;
    PVOID Buffer;
    PSID UserSid;
    ULONG Subsystem;
    ULONG ReturnLength;
    ULONG RequiredLength;
    NTSTATUS Status;

    Subsystem = MAXULONG;
    ReturnLength = MAXULONG;
    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessSubsystemInformation, &Subsystem, sizeof(Subsystem), &ReturnLength);
    trace("ProcessSubsystemInformation returned 0x%08lx, subsystem %lu, length %lu\n", Status, Subsystem, ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Subsystem, 0);
    ok_eq_ulong(ReturnLength, sizeof(Subsystem));

    RequiredLength = MAXULONG;
    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessTelemetryIdInformation, NULL, 0, &RequiredLength);
    trace("ProcessTelemetryIdInformation(size query) returned 0x%08lx, length %lu\n", Status, RequiredLength);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok(RequiredLength > sizeof(Header), "telemetry length %lu omitted variable data\n", RequiredLength);
    if (RequiredLength <= sizeof(Header))
        return;

    RtlFillMemory(&Header, sizeof(Header), 0xA5);
    ReturnLength = MAXULONG;
    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessTelemetryIdInformation, &Header, sizeof(Header), &ReturnLength);
    trace("ProcessTelemetryIdInformation(header) returned 0x%08lx, length %lu\n", Status, ReturnLength);
    ok_eq_hex(Status, STATUS_BUFFER_OVERFLOW);
    ok_eq_ulong(ReturnLength, RequiredLength);
    ok_eq_ulong(Header.HeaderSize, sizeof(Header));
    ok_eq_ulong(Header.ProcessId, HandleToUlong(PsGetCurrentProcessId()));
    ok_eq_ulonglong(Header.ProcessStartKey, PsGetProcessStartKey(PsGetCurrentProcess()));
    ok_eq_ulonglong(Header.ProcessSequenceNumber & 0x0000FFFFFFFFFFFFULL, Header.ProcessStartKey & 0x0000FFFFFFFFFFFFULL);
    ok_eq_ulong(Header.SessionId, PsGetProcessSessionId(PsGetCurrentProcess()));
    ok_eq_ulong(Header.BootId, SharedUserData->BootId);
    ok_eq_ulong(Header.UserSidOffset, 0);

    Buffer = ExAllocatePoolZero(PagedPool, RequiredLength, 'tImK');
    ok(Buffer != NULL, "failed to allocate %lu telemetry bytes\n", RequiredLength);
    if (Buffer == NULL)
        return;

    ReturnLength = MAXULONG;
    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessTelemetryIdInformation, Buffer, RequiredLength, &ReturnLength);
    trace("ProcessTelemetryIdInformation(full) returned 0x%08lx, length %lu\n", Status, ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, RequiredLength);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, 'tImK');
        return;
    }

    Information = Buffer;
    ok_eq_ulong(Information->HeaderSize, sizeof(*Information));
    ok_eq_ulong(Information->ProcessId, HandleToUlong(PsGetCurrentProcessId()));
    ok_eq_ulonglong(Information->ProcessStartKey, PsGetProcessStartKey(PsGetCurrentProcess()));
    ok_eq_ulonglong(Information->ProcessSequenceNumber & 0x0000FFFFFFFFFFFFULL, Information->ProcessStartKey & 0x0000FFFFFFFFFFFFULL);
    ok_eq_ulong(Information->SessionId, PsGetProcessSessionId(PsGetCurrentProcess()));
    ok_eq_ulong(Information->BootId, SharedUserData->BootId);
    ok_eq_ulong(Information->UserSidOffset, sizeof(*Information));
    ok(Information->ImagePathOffset > Information->UserSidOffset, "image path offset %lu did not follow SID offset %lu\n", Information->ImagePathOffset, Information->UserSidOffset);
    ok(Information->PackageNameOffset > Information->ImagePathOffset, "package offset %lu did not follow image path offset %lu\n", Information->PackageNameOffset, Information->ImagePathOffset);
    ok(Information->RelativeAppNameOffset >= Information->PackageNameOffset, "relative app offset %lu preceded package offset %lu\n", Information->RelativeAppNameOffset, Information->PackageNameOffset);
    ok(Information->CommandLineOffset >= Information->RelativeAppNameOffset, "command line offset %lu preceded relative app offset %lu\n", Information->CommandLineOffset, Information->RelativeAppNameOffset);
    ok(Information->CommandLineOffset + sizeof(WCHAR) <= RequiredLength, "command line offset %lu exceeded length %lu\n", Information->CommandLineOffset, RequiredLength);

    UserSid = (PSID)((PUCHAR)Buffer + Information->UserSidOffset);
    ok(RtlValidSid(UserSid), "telemetry user SID was invalid\n");
    ExFreePoolWithTag(Buffer, 'tImK');
}

START_TEST(PsProcessInfo)
{
    TestImageFileName();
    TestPreviousMode();
    TestProcessFlags();
    TestExitStatusQueries();
    TestModernProcessInformation();
}
