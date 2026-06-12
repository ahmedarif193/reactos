/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite process information API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

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

START_TEST(PsProcessInfo)
{
    TestImageFileName();
    TestPreviousMode();
    TestProcessFlags();
    TestExitStatusQueries();
}
