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
typedef NTSTATUS (NTAPI *PFN_RtlGetNativeSystemInformation)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
typedef BOOL (WINAPI *PFN_IsWow64Process2)(HANDLE, PUSHORT, PUSHORT);
typedef NTSTATUS (NTAPI *PFN_ThreadSuspendCount)(HANDLE, PULONG);

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
Arm64ChpeTestAddressRequirements(VOID)
{
    PFN_NtAllocateVirtualMemoryEx Allocate = LookupProc("NtAllocateVirtualMemoryEx");
    MEM_EXTENDED_PARAMETER Parameter = {0};
    MEM_ADDRESS_REQUIREMENTS Requirements = {0};
    PVOID BaseAddress;
    SIZE_T RegionSize, FreeSize;
    NTSTATUS Status;
    ULONG Index;
    struct
    {
        ULONG_PTR Low;
        ULONG_PTR High;
        SIZE_T Alignment;
        SIZE_T Size;
        NTSTATUS Status;
    } Cases[] =
    {
#ifdef _WIN64
        {0x100000000ULL, 0, 0, PAGE_SIZE, STATUS_SUCCESS},
        {0x100000000ULL, 0x18002ffffULL, 0x200000, PAGE_SIZE, STATUS_SUCCESS},
        {0x100000000ULL, 0x10000ffffULL, 0, 0x20000, STATUS_NO_MEMORY},
#endif
        {0, 0, 0, PAGE_SIZE, STATUS_SUCCESS},
        {0x20000000, 0x3002ffff, 0x200000, PAGE_SIZE, STATUS_SUCCESS},
        {1, 0, 0, PAGE_SIZE, STATUS_INVALID_PARAMETER},
        {MAXULONG_PTR, 0, 0, PAGE_SIZE, STATUS_INVALID_PARAMETER},
        {0, 0x30020000, 0, PAGE_SIZE, STATUS_INVALID_PARAMETER},
        {0x30030000, 0x3002ffff, 0, PAGE_SIZE, STATUS_INVALID_PARAMETER},
        {0, 0, PAGE_SIZE, PAGE_SIZE, STATUS_INVALID_PARAMETER},
        {0, 0, 0x30000, PAGE_SIZE, STATUS_INVALID_PARAMETER}
    };

    if (!Allocate)
        return;

    Parameter.Type = MemExtendedParameterAddressRequirements;
    Parameter.Pointer = &Requirements;
    for (Index = 0; Index < RTL_NUMBER_OF(Cases); ++Index)
    {
        Requirements.LowestStartingAddress = (PVOID)Cases[Index].Low;
        Requirements.HighestEndingAddress = (PVOID)Cases[Index].High;
        Requirements.Alignment = Cases[Index].Alignment;
        BaseAddress = NULL;
        RegionSize = Cases[Index].Size;
        Status = Allocate(NtCurrentProcess(), &BaseAddress, &RegionSize,
                          MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, &Parameter, 1);
        ok(Status == Cases[Index].Status, "Case %lu: status %lx, expected %lx\n",
           Index, Status, Cases[Index].Status);
        if (!NT_SUCCESS(Status))
            continue;

        ok((ULONG_PTR)BaseAddress >= Cases[Index].Low, "Case %lu: base %p below lower bound\n", Index, BaseAddress);
        if (Cases[Index].High)
            ok((ULONG_PTR)BaseAddress + RegionSize - 1 <= Cases[Index].High,
               "Case %lu: base %p size %Ix exceeds upper bound\n", Index, BaseAddress, RegionSize);
        if (Cases[Index].Alignment)
            ok(!((ULONG_PTR)BaseAddress & (Cases[Index].Alignment - 1)),
               "Case %lu: base %p is misaligned\n", Index, BaseAddress);
        *(volatile ULONG *)BaseAddress = 0x12345678;
        ok(*(volatile ULONG *)BaseAddress == 0x12345678, "Case %lu: committed memory is not writable\n", Index);
        FreeSize = 0;
        Status = NtFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &FreeSize, MEM_RELEASE);
        ok_hex(Status, STATUS_SUCCESS);
    }

    Requirements.LowestStartingAddress = (PVOID)0x20000000;
    Requirements.HighestEndingAddress = NULL;
    Requirements.Alignment = 0;
    BaseAddress = (PVOID)0x20000000;
    RegionSize = PAGE_SIZE;
    Status = Allocate(NtCurrentProcess(), &BaseAddress, &RegionSize,
                      MEM_RESERVE, PAGE_READWRITE, &Parameter, 1);
    ok_hex(Status, STATUS_INVALID_PARAMETER);
    if (NT_SUCCESS(Status))
    {
        FreeSize = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &FreeSize, MEM_RELEASE);
    }
}

static
VOID
Arm64ChpeTestEmulationAddressLimit(VOID)
{
#ifdef _WIN64
    SYSTEM_BASIC_INFORMATION Information;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemEmulationBasicInformation, &Information, sizeof(Information), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(Information.MaximumUserModeAddress < 0x100000000ULL,
           "Emulated address limit is not 32-bit: %Ix\n", Information.MaximumUserModeAddress);
        ok(Information.MaximumUserModeAddress >= 0x7ffeffff,
           "Emulated address limit is too small: %Ix\n", Information.MaximumUserModeAddress);
    }
#endif
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
Arm64ChpeTestNativeProcessorInformation(VOID)
{
    PFN_RtlGetNativeSystemInformation QueryNative = LookupProc("RtlGetNativeSystemInformation");
    PFN_IsWow64Process2 QueryMachines;
    SYSTEM_PROCESSOR_INFORMATION Info;
    ULONG LegacyInfo[4];
    USHORT ProcessMachine, NativeMachine, ExpectedArchitecture;
    ULONG Length;
    NTSTATUS Status;

    QueryMachines = (PFN_IsWow64Process2)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
    if (!QueryNative || !QueryMachines)
    {
        skip("Native processor/machine queries are unavailable\n");
        return;
    }
    if (!QueryMachines(GetCurrentProcess(), &ProcessMachine, &NativeMachine))
    {
        ok(FALSE, "IsWow64Process2 failed: %lu\n", GetLastError());
        return;
    }
    switch (NativeMachine)
    {
        case IMAGE_FILE_MACHINE_I386: ExpectedArchitecture = PROCESSOR_ARCHITECTURE_INTEL; break;
        case IMAGE_FILE_MACHINE_AMD64: ExpectedArchitecture = PROCESSOR_ARCHITECTURE_AMD64; break;
        case IMAGE_FILE_MACHINE_ARMNT: ExpectedArchitecture = PROCESSOR_ARCHITECTURE_ARM; break;
        case IMAGE_FILE_MACHINE_ARM64: ExpectedArchitecture = PROCESSOR_ARCHITECTURE_ARM64; break;
        default:
            ok(FALSE, "Unexpected native machine %x\n", NativeMachine);
            return;
    }

    memset(&Info, 0xcc, sizeof(Info));
    Length = 0xdeadbeef;
    Status = QueryNative(SystemProcessorInformation, &Info, sizeof(Info), &Length);
    ok_hex(Status, STATUS_SUCCESS);
    ok(Length == sizeof(Info), "Unexpected native processor info length %lu\n", Length);
    if (NT_SUCCESS(Status))
        ok(Info.ProcessorArchitecture == ExpectedArchitecture, "Native architecture %u, expected %u for machine %x\n", Info.ProcessorArchitecture, ExpectedArchitecture, NativeMachine);

    memset(&Info, 0xcc, sizeof(Info));
    Status = QueryNative(SystemProcessorInformation, &Info, sizeof(Info), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(Info.ProcessorArchitecture == ExpectedArchitecture, "Native architecture without ReturnLength: %u, expected %u\n", Info.ProcessorArchitecture, ExpectedArchitecture);

    Status = QueryNative(SystemProcessorInformation, &Info, sizeof(Info) - 1, &Length);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    memset(LegacyInfo, 0xcc, sizeof(LegacyInfo));
    Length = 0xdeadbeef;
    Status = QueryNative(SystemProcessorInformation, LegacyInfo, 12, &Length);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Length, 12);
    if (NT_SUCCESS(Status)) ok_hex((USHORT)LegacyInfo[0], ExpectedArchitecture);
    ok_hex(LegacyInfo[3], 0xcccccccc);
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

static DWORD WINAPI Arm64ChpeThreadExit(PVOID Parameter)
{
    return (ULONG_PTR)Parameter;
}

static VOID Arm64ChpeTestThreadSuspendResume(VOID)
{
    PFN_ThreadSuspendCount Suspend = LookupProc("NtSuspendThread");
    PFN_ThreadSuspendCount Resume = LookupProc("NtResumeThread");
    HANDLE Thread;
    ULONG Previous;
    DWORD Result, ExitCode;
    NTSTATUS Status;

    if (!Suspend || !Resume) return;
    Thread = CreateThread(NULL, 0, Arm64ChpeThreadExit, ULongToPtr(0x1234), CREATE_SUSPENDED, NULL);
    ok(Thread != NULL, "Cannot create suspended worker: %lu\n", GetLastError());
    if (!Thread) return;
    Previous = 0xdeadbeef;
    Status = Suspend(Thread, &Previous);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Previous, 1);
    Previous = 0xdeadbeef;
    Status = Resume(Thread, &Previous);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Previous, 2);
    Previous = 0xdeadbeef;
    Status = Resume(Thread, &Previous);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Previous, 1);
    Result = WaitForSingleObject(Thread, 5000);
    ok_hex(Result, WAIT_OBJECT_0);
    if (Result == WAIT_OBJECT_0)
    {
        ExitCode = STILL_ACTIVE;
        ok(GetExitCodeThread(Thread, &ExitCode), "Cannot query worker exit code\n");
        ok_hex(ExitCode, 0x1234);
    }
    else TerminateThread(Thread, 1);
    CloseHandle(Thread);
}

START_TEST(arm64_chpe)
{
    Ntdll = GetModuleHandleW(L"ntdll.dll");
    ok(Ntdll != NULL, "GetModuleHandleW(ntdll.dll) failed: %lu\n", GetLastError());
    if (!Ntdll)
        return;

    Arm64ChpeTestRequiredImports();
    Arm64ChpeTestNtAllocateVirtualMemoryExSmoke();
    Arm64ChpeTestAddressRequirements();
    Arm64ChpeTestEmulationAddressLimit();
    Arm64ChpeTestNtOpenKeyExSmoke();
    Arm64ChpeTestWaitOnAddressSmoke();
    Arm64ChpeTestNativeProcessorInformation();
    Arm64ChpeTestRtlPerformanceSmoke();
    Arm64ChpeTestThreadSuspendResume();
}
