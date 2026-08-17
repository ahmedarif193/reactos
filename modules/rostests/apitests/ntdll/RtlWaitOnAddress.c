/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests wait-on-address handoff and timeout behavior
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define HANDOFF_ROUNDS 512
#define RACE_ROUNDS 2048
#define TEST_TIMEOUT_MS 10000

typedef NTSTATUS (NTAPI *PFN_RTL_WAIT_ON_ADDRESS)(const VOID *, const VOID *, SIZE_T, const LARGE_INTEGER *);
typedef VOID (NTAPI *PFN_RTL_WAKE_ADDRESS)(const VOID *);

static PFN_RTL_WAIT_ON_ADDRESS pRtlWaitOnAddress;
static PFN_RTL_WAKE_ADDRESS pRtlWakeAddressAll;
static volatile LONG HandoffAddress;
static volatile LONG HandoffReady;
static volatile LONG HandoffCompleted;
static volatile LONG HandoffTimeouts;
static volatile LONG HandoffFailures;
static volatile LONG HandoffSpuriousWakes;
static volatile LONG RaceStop;
static volatile LONG RaceCompleted;
static volatile LONG RaceFailures;
static LONG RaceAddress;

static
BOOLEAN
WaitForValue(
    _In_ volatile LONG *Value,
    _In_ LONG Expected,
    _In_ DWORD Timeout)
{
    DWORD Start = GetTickCount();

    while (InterlockedCompareExchange((volatile LONG *)Value, 0, 0) < Expected)
    {
        if (GetTickCount() - Start >= Timeout)
            return FALSE;

        Sleep(0);
    }

    return TRUE;
}

static
DWORD
WINAPI
HandoffWaiter(
    _In_ LPVOID Parameter)
{
    LONG Round;

    UNREFERENCED_PARAMETER(Parameter);

    for (Round = 1; Round <= HANDOFF_ROUNDS; ++Round)
    {
        LARGE_INTEGER Timeout;
        LONG Compare = Round - 1;
        LONG Value;
        NTSTATUS Status;
        DWORD Start;

        InterlockedExchange(&HandoffReady, Round);
        Start = GetTickCount();
        do
        {
            DWORD Elapsed = GetTickCount() - Start;
            DWORD Remaining = Elapsed < 1000 ? 1000 - Elapsed : 0;

            Timeout.QuadPart = -(LONGLONG)Remaining * 10000;
            Status = pRtlWaitOnAddress((const VOID *)&HandoffAddress, &Compare, sizeof(Compare), &Timeout);
            if (Status == STATUS_TIMEOUT)
            {
                InterlockedIncrement(&HandoffTimeouts);
                break;
            }
            if (Status != STATUS_SUCCESS)
            {
                InterlockedIncrement(&HandoffFailures);
                break;
            }

            Value = InterlockedCompareExchange(&HandoffAddress, 0, 0);
            if (Value == Compare)
                InterlockedIncrement(&HandoffSpuriousWakes);
        } while (Value == Compare);

        if (InterlockedCompareExchange(&HandoffAddress, 0, 0) != Round)
            InterlockedIncrement(&HandoffFailures);
        InterlockedExchange(&HandoffCompleted, Round);
    }

    return 0;
}

static
DWORD
WINAPI
RaceWaiter(
    _In_ LPVOID Parameter)
{
    LONG Round;

    UNREFERENCED_PARAMETER(Parameter);

    for (Round = 1; Round <= RACE_ROUNDS; ++Round)
    {
        LARGE_INTEGER Timeout;
        LONG Compare = 0;
        NTSTATUS Status;

        Timeout.QuadPart = -10000LL;
        Status = pRtlWaitOnAddress(&RaceAddress, &Compare, sizeof(Compare), &Timeout);
        if (Status != STATUS_SUCCESS && Status != STATUS_TIMEOUT)
            InterlockedIncrement(&RaceFailures);
        InterlockedExchange(&RaceCompleted, Round);
    }

    return 0;
}

static
DWORD
WINAPI
RaceWaker(
    _In_ LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    while (!InterlockedCompareExchange(&RaceStop, 0, 0))
    {
        pRtlWakeAddressAll(&RaceAddress);
        Sleep(0);
    }

    return 0;
}

START_TEST(RtlWaitOnAddress)
{
    PFN_RTL_WAKE_ADDRESS pRtlWakeAddressSingle;
    LARGE_INTEGER Timeout;
    HMODULE Ntdll;
    HANDLE HandoffThread;
    HANDLE RaceThread;
    HANDLE WakerThread;
    NTSTATUS Status;
    DWORD WaitStatus;
    LONG Address;
    LONG Compare;
    LONG Round;

    Ntdll = GetModuleHandleW(L"ntdll.dll");
    ok(Ntdll != NULL, "GetModuleHandleW failed: %lu\n", GetLastError());
    if (!Ntdll)
        return;

    pRtlWaitOnAddress = (PFN_RTL_WAIT_ON_ADDRESS)GetProcAddress(Ntdll, "RtlWaitOnAddress");
    pRtlWakeAddressAll = (PFN_RTL_WAKE_ADDRESS)GetProcAddress(Ntdll, "RtlWakeAddressAll");
    pRtlWakeAddressSingle = (PFN_RTL_WAKE_ADDRESS)GetProcAddress(Ntdll, "RtlWakeAddressSingle");
    if (!pRtlWaitOnAddress || !pRtlWakeAddressAll || !pRtlWakeAddressSingle)
    {
        skip("Wait-on-address exports are unavailable\n");
        return;
    }

    Address = 1;
    Compare = 0;
    Status = pRtlWaitOnAddress(&Address, &Compare, sizeof(Address), NULL);
    ok_hex(Status, STATUS_SUCCESS);

    Status = pRtlWaitOnAddress(&Address, &Address, 3, NULL);
    ok_hex(Status, STATUS_INVALID_PARAMETER);

    pRtlWakeAddressSingle(&Address);
    Timeout.QuadPart = -10000LL;
    Status = pRtlWaitOnAddress(&Address, &Address, sizeof(Address), &Timeout);
    ok_hex(Status, STATUS_TIMEOUT);

    HandoffAddress = 0;
    HandoffReady = 0;
    HandoffCompleted = 0;
    HandoffTimeouts = 0;
    HandoffFailures = 0;
    HandoffSpuriousWakes = 0;
    HandoffThread = CreateThread(NULL, 0, HandoffWaiter, NULL, 0, NULL);
    ok(HandoffThread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!HandoffThread)
        return;

    for (Round = 1; Round <= HANDOFF_ROUNDS; ++Round)
    {
        if (!WaitForValue(&HandoffReady, Round, TEST_TIMEOUT_MS))
        {
            ok(0, "handoff waiter did not publish round %ld\n", Round);
            break;
        }

        InterlockedExchange(&HandoffAddress, Round);
        pRtlWakeAddressAll((const VOID *)&HandoffAddress);
        if (!WaitForValue(&HandoffCompleted, Round, TEST_TIMEOUT_MS))
        {
            ok(0, "handoff waiter did not complete round %ld\n", Round);
            break;
        }
    }

    WaitStatus = WaitForSingleObject(HandoffThread, TEST_TIMEOUT_MS);
    ok_long(WaitStatus, WAIT_OBJECT_0);
    ok_long(HandoffCompleted, HANDOFF_ROUNDS);
    ok_long(HandoffTimeouts, 0);
    ok_long(HandoffFailures, 0);
    trace("handoff spurious wakes: %ld\n", HandoffSpuriousWakes);
    CloseHandle(HandoffThread);

    RaceAddress = 0;
    RaceStop = 0;
    RaceCompleted = 0;
    RaceFailures = 0;
    RaceThread = CreateThread(NULL, 0, RaceWaiter, NULL, 0, NULL);
    WakerThread = CreateThread(NULL, 0, RaceWaker, NULL, 0, NULL);
    ok(RaceThread != NULL && WakerThread != NULL, "race thread creation failed: %lu\n", GetLastError());
    if (RaceThread && WakerThread)
    {
        WaitStatus = WaitForSingleObject(RaceThread, TEST_TIMEOUT_MS);
        InterlockedExchange(&RaceStop, 1);
        pRtlWakeAddressAll(&RaceAddress);
        ok_long(WaitStatus, WAIT_OBJECT_0);
        ok_long(RaceCompleted, RACE_ROUNDS);
        ok_long(RaceFailures, 0);
        ok_long(WaitForSingleObject(WakerThread, TEST_TIMEOUT_MS), WAIT_OBJECT_0);
    }
    else
    {
        InterlockedExchange(&RaceStop, 1);
    }
    if (RaceThread)
        CloseHandle(RaceThread);
    if (WakerThread)
        CloseHandle(WakerThread);
}
