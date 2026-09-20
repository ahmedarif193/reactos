/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Process power policy query validation
 */

#include "precomp.h"

START_TEST(ProcessPowerThrottling)
{
    PROCESS_POWER_THROTTLING_STATE State;
    HANDLE Process;
    BOOL Result;

    State.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    State.ControlMask = State.StateMask = 0;
    Result = GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State));
    ok(Result, "GetProcessInformation failed: %lu\n", GetLastError());
    if (Result)
    {
        ok_long(State.Version, PROCESS_POWER_THROTTLING_CURRENT_VERSION);
        ok(!(State.ControlMask & ~PROCESS_POWER_THROTTLING_VALID_FLAGS), "ControlMask %lx\n", State.ControlMask);
        ok(!(State.StateMask & ~PROCESS_POWER_THROTTLING_VALID_FLAGS), "StateMask %lx\n", State.StateMask);
    }

    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    ok(Process != NULL, "OpenProcess failed: %lu\n", GetLastError());
    if (Process)
    {
        Result = GetProcessInformation(Process, ProcessPowerThrottling, &State, sizeof(State));
        ok(Result, "Limited query failed: %lu\n", GetLastError());
        CloseHandle(Process);
    }

    Result = GetProcessInformation((HANDLE)(ULONG_PTR)0x1234, ProcessPowerThrottling, &State, sizeof(State));
    ok(!Result, "Invalid handle accepted\n");
    ok_long(GetLastError(), ERROR_INVALID_HANDLE);

    Result = GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State) - 1);
    ok(!Result, "Short buffer accepted\n");
    ok_long(GetLastError(), ERROR_BAD_LENGTH);

    State.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION + 1;
    Result = GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State));
    ok(!Result, "Invalid version accepted\n");
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);
}
