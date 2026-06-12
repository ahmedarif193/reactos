/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite ZwQuerySystemInformation API
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'iSmK'

static
VOID
TestBasicInformation(VOID)
{
    SYSTEM_BASIC_INFORMATION Basic;
    ULONG ReturnLength = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Basic, sizeof(Basic));
    Status = ZwQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, (ULONG)sizeof(Basic));
    ok_eq_ulong(Basic.PageSize, (ULONG)PAGE_SIZE);
    ok(Basic.NumberOfProcessors >= 1, "processors %u\n", Basic.NumberOfProcessors);
    ok(Basic.NumberOfPhysicalPages > 0x1000, "physical pages %lu\n", Basic.NumberOfPhysicalPages);
    ok((ULONG_PTR)Basic.MaximumUserModeAddress > (ULONG_PTR)Basic.MinimumUserModeAddress, "user range %p..%p\n", (PVOID)Basic.MinimumUserModeAddress, (PVOID)Basic.MaximumUserModeAddress);

    Status = ZwQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic) - 1, &ReturnLength);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
}

static
VOID
TestProcessInformation(VOID)
{
    PSYSTEM_PROCESS_INFORMATION ProcessInfo;
    PUCHAR Buffer;
    ULONG Size = 256 * 1024;
    ULONG ReturnLength = 0;
    NTSTATUS Status;
    ULONG Count;
    BOOLEAN FoundSystem;

    Buffer = ExAllocatePoolWithTag(PagedPool, Size, TAG_TEST);
    ok(Buffer != NULL, "no pool\n");
    if (Buffer == NULL) return;

    Status = ZwQuerySystemInformation(SystemProcessInformation, Buffer, Size, &ReturnLength);
    if (Status == STATUS_INFO_LENGTH_MISMATCH)
    {
        ExFreePoolWithTag(Buffer, TAG_TEST);
        Size = ReturnLength + 64 * 1024;
        Buffer = ExAllocatePoolWithTag(PagedPool, Size, TAG_TEST);
        ok(Buffer != NULL, "no pool retry\n");
        if (Buffer == NULL) return;
        Status = ZwQuerySystemInformation(SystemProcessInformation, Buffer, Size, &ReturnLength);
    }
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status))
    {
        Count = 0;
        FoundSystem = FALSE;
        ProcessInfo = (PSYSTEM_PROCESS_INFORMATION)Buffer;
        for (;;)
        {
            Count++;
            if (ProcessInfo->UniqueProcessId == (HANDLE)(ULONG_PTR)4)
                FoundSystem = TRUE;
            if (ProcessInfo->NextEntryOffset == 0)
                break;
            ProcessInfo = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)ProcessInfo + ProcessInfo->NextEntryOffset);
        }
        ok(Count >= 3, "only %lu processes\n", Count);
        ok_bool_true(FoundSystem, "System process present");
    }

    ExFreePoolWithTag(Buffer, TAG_TEST);
}

START_TEST(ZwSystemInfoKM)
{
    TestBasicInformation();
    TestProcessInformation();
}
