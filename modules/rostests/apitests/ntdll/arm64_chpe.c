/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Smoke tests for the NTDLL import surface needed by ARM64 CHPE/FEX
 */

#include "precomp.h"

typedef NTSTATUS (NTAPI *PFN_NtOpenKeyEx)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, ULONG);
typedef NTSTATUS (NTAPI *PFN_NtAllocateVirtualMemoryEx)(HANDLE, PVOID *, PSIZE_T, ULONG, ULONG, PVOID, ULONG);
typedef BOOL (WINAPI *PFN_RtlQueryPerformanceCounter)(PLARGE_INTEGER);
typedef BOOL (WINAPI *PFN_RtlQueryPerformanceFrequency)(PLARGE_INTEGER);
typedef NTSTATUS (WINAPI *PFN_RtlWaitOnAddress)(const VOID *, const VOID *, SIZE_T, const LARGE_INTEGER *);
typedef VOID (WINAPI *PFN_RtlWakeAddress)(const VOID *);

static HMODULE Ntdll;

static
PVOID
LookupProc(
    _In_ PCSTR Name)
{
    PVOID Proc = (PVOID)GetProcAddress(Ntdll, Name);
    ok(Proc != NULL, "%s is missing\n", Name);
    return Proc;
}

static
VOID
Arm64ChpeTestRequiredImports(VOID)
{
    static const PCSTR Imports[] =
    {
        "LdrGetDllFullName",
        "NtAllocateVirtualMemoryEx",
        "NtOpenKeyEx",
        "RtlIsEcCode",
        "RtlLocateExtendedFeature",
        "RtlLocateExtendedFeature2",
        "RtlQueryPerformanceCounter",
        "RtlQueryPerformanceFrequency",
        "RtlQuerySystemTime",
        "RtlSystemTimeToTimeFields",
        "RtlWaitOnAddress",
        "RtlWakeAddressAll",
        "RtlWakeAddressSingle",
    };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Imports); Index++)
        LookupProc(Imports[Index]);
}

static
VOID
Arm64ChpeTestNtAllocateVirtualMemoryExSmoke(VOID)
{
    PFN_NtAllocateVirtualMemoryEx pNtAllocateVirtualMemoryEx;
    NTSTATUS Status;
    PVOID BaseAddress;
    SIZE_T RegionSize;

    pNtAllocateVirtualMemoryEx = LookupProc("NtAllocateVirtualMemoryEx");
    if (!pNtAllocateVirtualMemoryEx)
        return;

    BaseAddress = NULL;
    RegionSize = PAGE_SIZE;
    Status = pNtAllocateVirtualMemoryEx(NtCurrentProcess(),
                                        &BaseAddress,
                                        &RegionSize,
                                        MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE,
                                        NULL,
                                        0);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RegionSize = 0;
        Status = NtFreeVirtualMemory(NtCurrentProcess(),
                                     &BaseAddress,
                                     &RegionSize,
                                     MEM_RELEASE);
        ok_hex(Status, STATUS_SUCCESS);
    }
}

static
VOID
Arm64ChpeTestNtOpenKeyExSmoke(VOID)
{
    PFN_NtOpenKeyEx pNtOpenKeyEx;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;
    NTSTATUS Status;
    HANDLE Key;

    pNtOpenKeyEx = LookupProc("NtOpenKeyEx");
    if (!pNtOpenKeyEx)
        return;

    RtlInitUnicodeString(&Name, L"\\Registry\\Machine");
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Key = NULL;
    Status = pNtOpenKeyEx(&Key, KEY_READ, &Attributes, 0);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        NtClose(Key);

    Key = NULL;
    Status = pNtOpenKeyEx(&Key, KEY_READ, &Attributes, REG_OPTION_OPEN_LINK);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        NtClose(Key);
}

static
VOID
Arm64ChpeTestWaitOnAddressSmoke(VOID)
{
    PFN_RtlWaitOnAddress pRtlWaitOnAddress;
    PFN_RtlWakeAddress pRtlWakeAddressAll;
    PFN_RtlWakeAddress pRtlWakeAddressSingle;
    ULONG Address;
    ULONG Compare;
    NTSTATUS Status;

    pRtlWaitOnAddress = LookupProc("RtlWaitOnAddress");
    pRtlWakeAddressAll = LookupProc("RtlWakeAddressAll");
    pRtlWakeAddressSingle = LookupProc("RtlWakeAddressSingle");
    if (!pRtlWaitOnAddress || !pRtlWakeAddressAll || !pRtlWakeAddressSingle)
        return;

    Address = 1;
    Compare = 0;
    Status = pRtlWaitOnAddress(&Address, &Compare, sizeof(Address), NULL);
    ok_hex(Status, STATUS_SUCCESS);

    Status = pRtlWaitOnAddress(&Address, &Address, 5, NULL);
    ok_hex(Status, STATUS_INVALID_PARAMETER);

    pRtlWakeAddressSingle(&Address);
    pRtlWakeAddressAll(&Address);
}

static
VOID
Arm64ChpeTestRtlPerformanceSmoke(VOID)
{
    PFN_RtlQueryPerformanceCounter pRtlQueryPerformanceCounter;
    PFN_RtlQueryPerformanceFrequency pRtlQueryPerformanceFrequency;
    LARGE_INTEGER Counter;
    LARGE_INTEGER Frequency;
    BOOL Ret;

    pRtlQueryPerformanceCounter = LookupProc("RtlQueryPerformanceCounter");
    pRtlQueryPerformanceFrequency = LookupProc("RtlQueryPerformanceFrequency");
    if (!pRtlQueryPerformanceCounter || !pRtlQueryPerformanceFrequency)
        return;

    Ret = pRtlQueryPerformanceCounter(&Counter);
    ok(Ret, "RtlQueryPerformanceCounter failed\n");
    ok(Counter.QuadPart != 0, "Expected a non-zero performance counter\n");

    Ret = pRtlQueryPerformanceFrequency(&Frequency);
    ok(Ret, "RtlQueryPerformanceFrequency failed\n");
    ok(Frequency.QuadPart > 0, "Expected a positive performance frequency\n");
}

START_TEST(arm64_chpe)
{
    Ntdll = GetModuleHandleW(L"ntdll.dll");
    ok(Ntdll != NULL, "GetModuleHandleW(ntdll.dll) failed: %lu\n", GetLastError());
    if (!Ntdll)
        return;

    Arm64ChpeTestRequiredImports();
    Arm64ChpeTestNtAllocateVirtualMemoryExSmoke();
    Arm64ChpeTestNtOpenKeyExSmoke();
    Arm64ChpeTestWaitOnAddressSmoke();
    Arm64ChpeTestRtlPerformanceSmoke();
}
