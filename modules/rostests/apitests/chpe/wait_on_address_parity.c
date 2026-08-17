/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable wait-on-address handoff and timeout parity probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define HANDOFF_ROUNDS 512
#define RACE_ROUNDS 2048
#define TEST_TIMEOUT_MS 10000

typedef BOOL (WINAPI *PWAIT_ON_ADDRESS)(volatile VOID *, PVOID, SIZE_T, DWORD);
typedef VOID (WINAPI *PWAKE_BY_ADDRESS)(PVOID);
typedef LONG (WINAPI *PRTL_WAIT_ON_ADDRESS)(const VOID *, const VOID *, SIZE_T, const LARGE_INTEGER *);
typedef VOID (WINAPI *PRTL_WAKE_ADDRESS)(const VOID *);
typedef LONG (WINAPI *PNT_ALERT_THREAD_BY_THREAD_ID)(HANDLE);
typedef LONG (WINAPI *PNT_WAIT_FOR_ALERT_BY_THREAD_ID)(PVOID, const LARGE_INTEGER *);

#define TEST_STATUS_SUCCESS ((LONG)0x00000000L)
#define TEST_STATUS_ALERTED ((LONG)0x00000101L)
#define TEST_STATUS_TIMEOUT ((LONG)0x00000102L)
#define TEST_STATUS_INVALID_PARAMETER ((LONG)0xC000000DL)

static PWAIT_ON_ADDRESS WaitOnAddressPtr;
static PWAKE_BY_ADDRESS WakeByAddressAllPtr;
static PRTL_WAIT_ON_ADDRESS RtlWaitOnAddressPtr;
static PNT_ALERT_THREAD_BY_THREAD_ID NtAlertThreadByThreadIdPtr;
static volatile LONG HandoffAddress;
static volatile LONG HandoffReady;
static volatile LONG HandoffCompleted;
static volatile LONG HandoffTimeouts;
static volatile LONG HandoffStatusFailures;
static volatile LONG HandoffValueFailures;
static volatile LONG HandoffSpuriousWakes;
static volatile LONG HandoffFirstStatusRound;
static volatile LONG HandoffFirstStatus;
static volatile LONG HandoffFirstSpuriousRound;
static volatile LONG HandoffFirstValueRound;
static volatile LONG HandoffFirstValue;
static volatile LONG RaceStop;
static volatile LONG RaceCompleted;
static volatile LONG RaceFailures;
static LONG RaceAddress;
static volatile LONG AlertAddress;
static volatile LONG AlertReady;
static LONG AlertStatus;

static int
log_result(PCSTR Format, ...)
{
    CHAR Buffer[512];
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    fputs(Buffer, stdout);
    OutputDebugStringA(Buffer);
    return Length;
}

#define printf log_result

static DWORD
find_export_rva(
    HMODULE Module,
    PCSTR Name)
{
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS NtHeader;
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PDWORD Functions;
    PDWORD Names;
    PWORD Ordinals;
    DWORD Index;

    if (!Module || !Name)
        return 0;

    DosHeader = (PIMAGE_DOS_HEADER)Module;
    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    NtHeader = (PIMAGE_NT_HEADERS)((PBYTE)Module + DosHeader->e_lfanew);
    if (NtHeader->Signature != IMAGE_NT_SIGNATURE || !NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress)
        return 0;

    ExportDirectory = (PIMAGE_EXPORT_DIRECTORY)((PBYTE)Module + NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    Functions = (PDWORD)((PBYTE)Module + ExportDirectory->AddressOfFunctions);
    Names = (PDWORD)((PBYTE)Module + ExportDirectory->AddressOfNames);
    Ordinals = (PWORD)((PBYTE)Module + ExportDirectory->AddressOfNameOrdinals);
    for (Index = 0; Index < ExportDirectory->NumberOfNames; ++Index)
    {
        if (!strcmp((PCSTR)((PBYTE)Module + Names[Index]), Name))
            return Functions[Ordinals[Index]];
    }

    return 0;
}

static void
log_module_export(
    PCSTR Label,
    HMODULE Module)
{
    CHAR Path[MAX_PATH];
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS NtHeader;
    FARPROC Resolved;
    DWORD RawRva;

    Path[0] = ANSI_NULL;
    if (!Module)
    {
        printf("WAITADDR_MODULE label=%s module=%p\n", Label, Module);
        return;
    }

    DosHeader = (PIMAGE_DOS_HEADER)Module;
    NtHeader = (PIMAGE_NT_HEADERS)((PBYTE)Module + DosHeader->e_lfanew);
    RawRva = find_export_rva(Module, "WaitOnAddress");
    Resolved = GetProcAddress(Module, "WaitOnAddress");
    GetModuleFileNameA(Module, Path, sizeof(Path));
    printf("WAITADDR_MODULE label=%s module=%p machine=0x%04x raw_rva=0x%08lx raw=%p resolved=%p path=%s\n", Label, Module, NtHeader->FileHeader.Machine, RawRva, RawRva ? (PBYTE)Module + RawRva : NULL, Resolved, Path);
}

static BOOL
wait_for_value(
    volatile LONG *Value,
    LONG Expected)
{
    DWORD Start = GetTickCount();

    while (InterlockedCompareExchange(Value, 0, 0) < Expected)
    {
        if (GetTickCount() - Start >= TEST_TIMEOUT_MS)
            return FALSE;

        Sleep(0);
    }

    return TRUE;
}

static DWORD WINAPI
handoff_waiter(PVOID Parameter)
{
    LONG Round;

    UNREFERENCED_PARAMETER(Parameter);

    for (Round = 1; Round <= HANDOFF_ROUNDS; ++Round)
    {
        LONG Compare = Round - 1;
        LONG Value;
        DWORD Start;

        InterlockedExchange(&HandoffReady, Round);
        Start = GetTickCount();
        do
        {
            DWORD Elapsed = GetTickCount() - Start;
            DWORD Remaining = Elapsed < 1000 ? 1000 - Elapsed : 0;
            BOOL Result = WaitOnAddressPtr(&HandoffAddress, &Compare, sizeof(Compare), Remaining);
            DWORD Error = Result ? ERROR_SUCCESS : GetLastError();

            if (Error == ERROR_TIMEOUT)
            {
                InterlockedIncrement(&HandoffTimeouts);
                break;
            }
            if (!Result)
            {
                if (InterlockedIncrement(&HandoffStatusFailures) == 1)
                {
                    HandoffFirstStatusRound = Round;
                    HandoffFirstStatus = Error;
                }
                break;
            }

            Value = InterlockedCompareExchange(&HandoffAddress, 0, 0);
            if (Value == Compare && InterlockedIncrement(&HandoffSpuriousWakes) == 1)
                HandoffFirstSpuriousRound = Round;
        } while (Value == Compare);

        Value = InterlockedCompareExchange(&HandoffAddress, 0, 0);
        if (Value != Round)
        {
            if (InterlockedIncrement(&HandoffValueFailures) == 1)
            {
                HandoffFirstValueRound = Round;
                HandoffFirstValue = Value;
            }
        }
        InterlockedExchange(&HandoffCompleted, Round);
    }

    return 0;
}

static DWORD WINAPI
race_waiter(PVOID Parameter)
{
    LONG Round;

    UNREFERENCED_PARAMETER(Parameter);

    for (Round = 1; Round <= RACE_ROUNDS; ++Round)
    {
        LONG Compare = 0;
        BOOL Result;

        Result = WaitOnAddressPtr(&RaceAddress, &Compare, sizeof(Compare), 1);
        if (!Result && GetLastError() != ERROR_TIMEOUT)
            InterlockedIncrement(&RaceFailures);
        InterlockedExchange(&RaceCompleted, Round);
    }

    return 0;
}

static DWORD WINAPI
race_waker(PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    while (!InterlockedCompareExchange(&RaceStop, 0, 0))
    {
        WakeByAddressAllPtr(&RaceAddress);
        Sleep(0);
    }

    return 0;
}

static DWORD WINAPI
alert_waiter(PVOID Parameter)
{
    LARGE_INTEGER Timeout;
    LONG Compare = 0;

    UNREFERENCED_PARAMETER(Parameter);

    InterlockedExchange(&AlertReady, 1);
    Timeout.QuadPart = -100000000LL;
    AlertStatus = RtlWaitOnAddressPtr((const VOID *)&AlertAddress, &Compare, sizeof(Compare), &Timeout);
    return 0;
}

int
main(void)
{
    PWAKE_BY_ADDRESS WakeByAddressSinglePtr;
    PRTL_WAKE_ADDRESS RtlWakeAddressAllPtr;
    PRTL_WAKE_ADDRESS RtlWakeAddressSinglePtr;
    PNT_WAIT_FOR_ALERT_BY_THREAD_ID NtWaitForAlertByThreadIdPtr;
    HMODULE SynchApiSet;
    HMODULE Provider;
    HMODULE Kernel32;
    HMODULE KernelBase;
    HMODULE Ntdll;
    HANDLE HandoffThread;
    HANDLE RaceThread;
    HANDLE WakerThread;
    HANDLE AlertThread;
    DWORD HandoffWait;
    DWORD RaceWait;
    DWORD WakerWait;
    DWORD AlertWait;
    DWORD AlertThreadId;
    LONG Address;
    LONG Compare;
    LONG Round;
    BOOL MismatchResult;
    BOOL InvalidResult;
    BOOL PrewakeResult;
    LARGE_INTEGER ZeroTimeout;
    LONG DirectMismatchStatus;
    LONG DirectInvalidStatus;
    LONG DirectTimeoutStatus;
    LONG AlertSetFirstStatus;
    LONG AlertSetSecondStatus;
    LONG AlertFirstStatus;
    LONG AlertSecondStatus;
    LONG AlertWakeStatus;
    DWORD InvalidError;
    DWORD PrewakeError;
    INT Failed;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_WAIT_ON_ADDRESS_BEGIN\n");

    SynchApiSet = LoadLibraryW(L"api-ms-win-core-synch-l1-2-0.dll");
    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    KernelBase = GetModuleHandleW(L"kernelbase.dll");
    Provider = SynchApiSet;
    if (!Provider)
        Provider = LoadLibraryW(L"kernelbase.dll");
    if (!Provider)
        Provider = GetModuleHandleW(L"kernel32.dll");
    WaitOnAddressPtr = Provider ? (PWAIT_ON_ADDRESS)GetProcAddress(Provider, "WaitOnAddress") : NULL;
    WakeByAddressAllPtr = Provider ? (PWAKE_BY_ADDRESS)GetProcAddress(Provider, "WakeByAddressAll") : NULL;
    WakeByAddressSinglePtr = Provider ? (PWAKE_BY_ADDRESS)GetProcAddress(Provider, "WakeByAddressSingle") : NULL;
    if (!WaitOnAddressPtr || !WakeByAddressAllPtr || !WakeByAddressSinglePtr)
    {
        printf("CHPE_WAIT_ON_ADDRESS_FAIL exports provider=%p error=%lu\n", Provider, GetLastError());
        return 2;
    }
    printf("WAITADDR_PROVIDER module=%p apiset=%p\n", Provider, SynchApiSet);
    log_module_export("provider", Provider);
    log_module_export("kernel32", Kernel32);
    log_module_export("kernelbase", KernelBase);
    printf("WAITADDR_PUBLIC_EXPORTS wait=%p wake_all=%p wake_single=%p\n", WaitOnAddressPtr, WakeByAddressAllPtr, WakeByAddressSinglePtr);

    Ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlWaitOnAddressPtr = Ntdll ? (PRTL_WAIT_ON_ADDRESS)GetProcAddress(Ntdll, "RtlWaitOnAddress") : NULL;
    RtlWakeAddressAllPtr = Ntdll ? (PRTL_WAKE_ADDRESS)GetProcAddress(Ntdll, "RtlWakeAddressAll") : NULL;
    RtlWakeAddressSinglePtr = Ntdll ? (PRTL_WAKE_ADDRESS)GetProcAddress(Ntdll, "RtlWakeAddressSingle") : NULL;
    NtAlertThreadByThreadIdPtr = Ntdll ? (PNT_ALERT_THREAD_BY_THREAD_ID)GetProcAddress(Ntdll, "NtAlertThreadByThreadId") : NULL;
    NtWaitForAlertByThreadIdPtr = Ntdll ? (PNT_WAIT_FOR_ALERT_BY_THREAD_ID)GetProcAddress(Ntdll, "NtWaitForAlertByThreadId") : NULL;
    if (!RtlWaitOnAddressPtr || !RtlWakeAddressAllPtr || !RtlWakeAddressSinglePtr || !NtAlertThreadByThreadIdPtr || !NtWaitForAlertByThreadIdPtr)
    {
        printf("CHPE_WAIT_ON_ADDRESS_FAIL ntdll_exports module=%p error=%lu\n", Ntdll, GetLastError());
        return 3;
    }
    printf("WAITADDR_NTDLL_EXPORTS wait=%p wake_all=%p wake_single=%p alert=%p alert_wait=%p\n", RtlWaitOnAddressPtr, RtlWakeAddressAllPtr, RtlWakeAddressSinglePtr, NtAlertThreadByThreadIdPtr, NtWaitForAlertByThreadIdPtr);

    Address = 1;
    Compare = 0;
    MismatchResult = WaitOnAddressPtr(&Address, &Compare, sizeof(Address), INFINITE);
    SetLastError(ERROR_SUCCESS);
    InvalidResult = WaitOnAddressPtr(&Address, &Address, 3, INFINITE);
    InvalidError = GetLastError();
    WakeByAddressSinglePtr(&Address);
    SetLastError(ERROR_SUCCESS);
    PrewakeResult = WaitOnAddressPtr(&Address, &Address, sizeof(Address), 1);
    PrewakeError = GetLastError();
    Failed = !MismatchResult || InvalidResult || InvalidError != ERROR_INVALID_PARAMETER || PrewakeResult || PrewakeError != ERROR_TIMEOUT;
    printf("WAITADDR_BASE mismatch=%d invalid=%d invalid_error=%lu prewake=%d prewake_error=%lu result=%d\n", MismatchResult, InvalidResult, InvalidError, PrewakeResult, PrewakeError, Failed);

    Address = 1;
    Compare = 0;
    ZeroTimeout.QuadPart = 0;
    DirectMismatchStatus = RtlWaitOnAddressPtr(&Address, &Compare, sizeof(Address), NULL);
    DirectInvalidStatus = RtlWaitOnAddressPtr(&Address, &Address, 3, NULL);
    RtlWakeAddressSinglePtr(&Address);
    DirectTimeoutStatus = RtlWaitOnAddressPtr(&Address, &Address, sizeof(Address), &ZeroTimeout);
    RtlWakeAddressAllPtr(NULL);
    Failed |= DirectMismatchStatus != TEST_STATUS_SUCCESS || DirectInvalidStatus != TEST_STATUS_INVALID_PARAMETER || DirectTimeoutStatus != TEST_STATUS_TIMEOUT;
    printf("WAITADDR_NTDLL mismatch=0x%08lx invalid=0x%08lx timeout=0x%08lx result=%d\n", DirectMismatchStatus, DirectInvalidStatus, DirectTimeoutStatus, Failed);

    AlertSetFirstStatus = NtAlertThreadByThreadIdPtr((HANDLE)(ULONG_PTR)GetCurrentThreadId());
    AlertSetSecondStatus = NtAlertThreadByThreadIdPtr((HANDLE)(ULONG_PTR)GetCurrentThreadId());
    AlertFirstStatus = NtWaitForAlertByThreadIdPtr((PVOID)(ULONG_PTR)0x123, &ZeroTimeout);
    AlertSecondStatus = NtWaitForAlertByThreadIdPtr((PVOID)(ULONG_PTR)0x321, &ZeroTimeout);
    Failed |= AlertSetFirstStatus != TEST_STATUS_SUCCESS || AlertSetSecondStatus != TEST_STATUS_SUCCESS || AlertFirstStatus != TEST_STATUS_ALERTED || AlertSecondStatus != TEST_STATUS_TIMEOUT;
    printf("WAITADDR_ALERT_STOCKPILE set1=0x%08lx set2=0x%08lx first=0x%08lx second=0x%08lx result=%d\n", AlertSetFirstStatus, AlertSetSecondStatus, AlertFirstStatus, AlertSecondStatus, Failed);

    AlertAddress = 0;
    AlertReady = 0;
    AlertStatus = TEST_STATUS_TIMEOUT;
    AlertThread = CreateThread(NULL, 0, alert_waiter, NULL, 0, &AlertThreadId);
    if (!AlertThread || !wait_for_value(&AlertReady, 1))
    {
        printf("CHPE_WAIT_ON_ADDRESS_FAIL alert_thread error=%lu\n", GetLastError());
        if (AlertThread)
            CloseHandle(AlertThread);
        return 4;
    }
    AlertWakeStatus = NtAlertThreadByThreadIdPtr((HANDLE)(ULONG_PTR)AlertThreadId);
    AlertWait = WaitForSingleObject(AlertThread, TEST_TIMEOUT_MS);
    Failed |= AlertWakeStatus != TEST_STATUS_SUCCESS || AlertWait != WAIT_OBJECT_0 || AlertStatus != TEST_STATUS_SUCCESS;
    printf("WAITADDR_DIRECT_ALERT wake=0x%08lx wait=0x%08lx status=0x%08lx result=%d\n", AlertWakeStatus, AlertWait, AlertStatus, Failed);
    CloseHandle(AlertThread);

    HandoffAddress = 0;
    HandoffReady = 0;
    HandoffCompleted = 0;
    HandoffTimeouts = 0;
    HandoffStatusFailures = 0;
    HandoffValueFailures = 0;
    HandoffSpuriousWakes = 0;
    HandoffFirstStatusRound = 0;
    HandoffFirstStatus = 0;
    HandoffFirstSpuriousRound = 0;
    HandoffFirstValueRound = 0;
    HandoffFirstValue = 0;
    HandoffThread = CreateThread(NULL, 0, handoff_waiter, NULL, 0, NULL);
    if (!HandoffThread)
    {
        printf("CHPE_WAIT_ON_ADDRESS_FAIL handoff_thread error=%lu\n", GetLastError());
        return 5;
    }

    for (Round = 1; Round <= HANDOFF_ROUNDS; ++Round)
    {
        if (!wait_for_value(&HandoffReady, Round))
            break;

        InterlockedExchange(&HandoffAddress, Round);
        WakeByAddressAllPtr((PVOID)&HandoffAddress);
        if (!wait_for_value(&HandoffCompleted, Round))
            break;
    }

    HandoffWait = WaitForSingleObject(HandoffThread, TEST_TIMEOUT_MS);
    Failed |= HandoffWait != WAIT_OBJECT_0 || HandoffCompleted != HANDOFF_ROUNDS || HandoffTimeouts != 0 || HandoffStatusFailures != 0 || HandoffValueFailures != 0;
    printf("WAITADDR_HANDOFF rounds=%ld completed=%ld timeouts=%ld status_failures=%ld first_status_round=%ld first_status=%ld value_failures=%ld first_value_round=%ld first_value=%ld spurious=%ld first_spurious_round=%ld wait=0x%08lx result=%d\n", HANDOFF_ROUNDS, HandoffCompleted, HandoffTimeouts, HandoffStatusFailures, HandoffFirstStatusRound, HandoffFirstStatus, HandoffValueFailures, HandoffFirstValueRound, HandoffFirstValue, HandoffSpuriousWakes, HandoffFirstSpuriousRound, HandoffWait, Failed);
    CloseHandle(HandoffThread);

    RaceAddress = 0;
    RaceStop = 0;
    RaceCompleted = 0;
    RaceFailures = 0;
    RaceThread = CreateThread(NULL, 0, race_waiter, NULL, 0, NULL);
    WakerThread = CreateThread(NULL, 0, race_waker, NULL, 0, NULL);
    if (!RaceThread || !WakerThread)
    {
        printf("CHPE_WAIT_ON_ADDRESS_FAIL race_thread error=%lu\n", GetLastError());
        if (RaceThread)
            CloseHandle(RaceThread);
        if (WakerThread)
            CloseHandle(WakerThread);
        return 6;
    }

    RaceWait = WaitForSingleObject(RaceThread, TEST_TIMEOUT_MS);
    InterlockedExchange(&RaceStop, 1);
    WakeByAddressAllPtr(&RaceAddress);
    WakerWait = WaitForSingleObject(WakerThread, TEST_TIMEOUT_MS);
    Failed |= RaceWait != WAIT_OBJECT_0 || WakerWait != WAIT_OBJECT_0 || RaceCompleted != RACE_ROUNDS || RaceFailures != 0;
    printf("WAITADDR_RACE rounds=%ld completed=%ld failures=%ld waiter=0x%08lx waker=0x%08lx result=%d\n", RACE_ROUNDS, RaceCompleted, RaceFailures, RaceWait, WakerWait, Failed);
    CloseHandle(RaceThread);
    CloseHandle(WakerThread);

    printf("CHPE_WAIT_ON_ADDRESS_%s\n", Failed ? "FAIL" : "PASS");
    return Failed;
}
