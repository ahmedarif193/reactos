/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SetThreadInformation thread power throttling
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

C_ASSERT(sizeof(THREAD_POWER_THROTTLING_STATE) == 3 * sizeof(ULONG));

static
VOID
CheckPowerThrottling(
    _In_ HANDLE Thread,
    _In_opt_ THREAD_POWER_THROTTLING_STATE *State,
    _In_ DWORD Size,
    _In_ BOOL ExpectedResult,
    _In_ DWORD ExpectedError)
{
    BOOL Result;

    SetLastError(0xdeadbeef);
    Result = SetThreadInformation(Thread, ThreadPowerThrottling, State, Size);
    ok(Result == ExpectedResult,
       "SetThreadInformation returned %u, expected %u\n",
       Result,
       ExpectedResult);
    if (!ExpectedResult)
        ok_long(GetLastError(), ExpectedError);
}

START_TEST(ThreadPowerThrottling)
{
    THREAD_POWER_THROTTLING_STATE State;

    ZeroMemory(&State, sizeof(State));
    State.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;

    State.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    State.StateMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    CheckPowerThrottling(GetCurrentThread(), &State, sizeof(State), TRUE, ERROR_SUCCESS);

    State.StateMask = 0;
    CheckPowerThrottling(GetCurrentThread(), &State, sizeof(State), TRUE, ERROR_SUCCESS);

    State.ControlMask = 0;
    CheckPowerThrottling(GetCurrentThread(), &State, sizeof(State), TRUE, ERROR_SUCCESS);

    State.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION + 1;
    CheckPowerThrottling(GetCurrentThread(), &State, sizeof(State), FALSE, ERROR_INVALID_PARAMETER);

    State.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    State.ControlMask = THREAD_POWER_THROTTLING_VALID_FLAGS << 1;
    CheckPowerThrottling(GetCurrentThread(), &State, sizeof(State), FALSE, ERROR_INVALID_PARAMETER);

    State.ControlMask = 0;
    State.StateMask = THREAD_POWER_THROTTLING_VALID_FLAGS << 1;
    CheckPowerThrottling(GetCurrentThread(), &State, sizeof(State), FALSE, ERROR_INVALID_PARAMETER);

    State.StateMask = 0;
    CheckPowerThrottling(GetCurrentThread(), &State, sizeof(State) - 1, FALSE, ERROR_BAD_LENGTH);
    CheckPowerThrottling((HANDLE)(ULONG_PTR)0x1234, &State, sizeof(State), FALSE, ERROR_INVALID_HANDLE);
    CheckPowerThrottling(GetCurrentThread(), NULL, sizeof(State), FALSE, ERROR_NOACCESS);
}
