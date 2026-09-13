/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Isolated CFG indirect-call scenarios
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef PAGE_TARGETS_INVALID
#define PAGE_TARGETS_INVALID 0x40000000
#endif
#ifndef CFG_CALL_TARGET_VALID
#define CFG_CALL_TARGET_VALID 0x00000001
#endif

typedef struct _CFG_CALL_TARGET_INFO_LOCAL
{
    ULONG_PTR Offset;
    ULONG_PTR Flags;
} CFG_CALL_TARGET_INFO_LOCAL;

typedef BOOL (WINAPI *PFN_SET_PROCESS_VALID_CALL_TARGETS)(HANDLE, PVOID, SIZE_T, ULONG, CFG_CALL_TARGET_INFO_LOCAL *);
typedef DWORD (WINAPI *PFN_CFG_TARGET)(VOID);
typedef BYTE CFG_VECTOR __attribute__((vector_size(16)));
typedef VOID (*PFN_CFG_FLOAT_TARGET)(double *, double, double, double, double, double, double, double, double);
typedef VOID (*PFN_CFG_VECTOR_TARGET)(CFG_VECTOR *, CFG_VECTOR, CFG_VECTOR, CFG_VECTOR, CFG_VECTOR, CFG_VECTOR, CFG_VECTOR, CFG_VECTOR, CFG_VECTOR);

static volatile PFN_CFG_TARGET CfgCallTarget;

__declspec(noinline)
static DWORD WINAPI
CfgLocalTarget(VOID)
{
    return 0x47f;
}

__declspec(noinline) __declspec(align(16))
static DWORD WINAPI
CfgInteriorTarget(VOID)
{
    volatile DWORD Value = 0x470;

    Value += 1;
    Value += 2;
    Value += 3;
    Value += 4;
    Value += 5;
    return Value;
}

static DWORD
CallIndirect(PFN_CFG_TARGET Target)
{
    CfgCallTarget = Target;
    return CfgCallTarget();
}

__declspec(noinline)
static VOID
CfgFloatTarget(double *Result, double A, double B, double C, double D, double E, double F, double G, double H)
{
    Result[0] = A;
    Result[1] = B;
    Result[2] = C;
    Result[3] = D;
    Result[4] = E;
    Result[5] = F;
    Result[6] = G;
    Result[7] = H;
}

__declspec(noinline)
static VOID
CfgVectorTarget(CFG_VECTOR *Result, CFG_VECTOR A, CFG_VECTOR B, CFG_VECTOR C, CFG_VECTOR D, CFG_VECTOR E, CFG_VECTOR F, CFG_VECTOR G, CFG_VECTOR H)
{
    Result[0] = A;
    Result[1] = B;
    Result[2] = C;
    Result[3] = D;
    Result[4] = E;
    Result[5] = F;
    Result[6] = G;
    Result[7] = H;
}

__declspec(noinline)
VOID
CallFloatIndirect(PFN_CFG_FLOAT_TARGET Target, double *Result, double A, double B, double C, double D, double E, double F, double G, double H)
{
    Target(Result, A, B, C, D, E, F, G, H);
}

__declspec(noinline)
VOID
CallVectorIndirect(PFN_CFG_VECTOR_TARGET Target, CFG_VECTOR *Result, CFG_VECTOR A, CFG_VECTOR B, CFG_VECTOR C, CFG_VECTOR D, CFG_VECTOR E, CFG_VECTOR F, CFG_VECTOR G, CFG_VECTOR H)
{
    Target(Result, A, B, C, D, E, F, G, H);
}

static DWORD
TestFloatingArguments(BOOL Vectors)
{
    static PFN_CFG_FLOAT_TARGET volatile FloatTarget = CfgFloatTarget;
    static PFN_CFG_VECTOR_TARGET volatile VectorTarget = CfgVectorTarget;
    static const double Expected[] = {1.25, -2.5, 3.75, -4.125, 5.5, -6.75, 7.875, -8.25};
    CFG_VECTOR Input[8], Output[8];
    double Result[8];
    ULONG Round, Index, Failures = 0;

    for (Index = 0; Index < sizeof(Input); ++Index)
        ((BYTE *)Input)[Index] = (BYTE)(Index + 1);
    for (Round = 0; Round < 2; ++Round)
    {
        if (Vectors)
        {
            ZeroMemory(Output, sizeof(Output));
            CallVectorIndirect(VectorTarget, Output, Input[0], Input[1], Input[2], Input[3], Input[4], Input[5], Input[6], Input[7]);
            for (Index = 0; Index < ARRAYSIZE(Input); ++Index)
            {
                if (memcmp(&Input[Index], &Output[Index], sizeof(Input[Index])) != 0)
                {
                    printf("CFGCHILD|vector-arguments|round=%lu|register=%lu|mismatch\n", Round, Index);
                    ++Failures;
                }
            }
        }
        else
        {
            ZeroMemory(Result, sizeof(Result));
            CallFloatIndirect(FloatTarget, Result, Expected[0], Expected[1], Expected[2], Expected[3], Expected[4], Expected[5], Expected[6], Expected[7]);
            for (Index = 0; Index < ARRAYSIZE(Expected); ++Index)
            {
                if (memcmp(&Expected[Index], &Result[Index], sizeof(Expected[Index])) != 0)
                {
                    printf("CFGCHILD|floating-arguments|round=%lu|register=%lu|mismatch\n", Round, Index);
                    ++Failures;
                }
            }
        }
    }
    printf("CFGCHILD|%s-arguments|failures=%lu\n", Vectors ? "vector" : "floating", Failures);
    return Failures ? 0x4f : 0;
}

static BOOL
GetSiblingPath(WCHAR Path[MAX_PATH], const WCHAR *Name)
{
    WCHAR *Separator;
    DWORD Length = GetModuleFileNameW(NULL, Path, MAX_PATH);

    if (Length == 0 || Length >= MAX_PATH)
        return FALSE;
    Separator = wcsrchr(Path, L'\\');
    if (Separator == NULL || (SIZE_T)(Separator - Path) + 1 + lstrlenW(Name) >= MAX_PATH)
        return FALSE;
    lstrcpyW(Separator + 1, Name);
    return TRUE;
}

static PFN_CFG_TARGET
LoadTarget(const WCHAR *DllName, const char *ExportName)
{
    WCHAR Path[MAX_PATH];
    HMODULE Module;

    if (!GetSiblingPath(Path, DllName))
        return NULL;
    Module = LoadLibraryW(Path);
    if (Module == NULL)
        return NULL;
    return (PFN_CFG_TARGET)GetProcAddress(Module, ExportName);
}

static PVOID
CreateDynamicTarget(DWORD Protection, SIZE_T Offset, PVOID *AllocationBase)
{
#if defined(_M_ARM64)
    static const DWORD Code[] =
    {
        0x52808fe0, /* mov w0, #0x47f */
        0xd65f03c0  /* ret */
    };
    PVOID Address, Target;
    DWORD OldProtection;

    Address = VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, Protection);
    if (Address == NULL)
        return NULL;
    if (Offset > 0x1000 - sizeof(Code))
        return NULL;
    Target = (BYTE *)Address + Offset;
    CopyMemory(Target, Code, sizeof(Code));
    if ((Protection & 0xff) == PAGE_READWRITE)
    {
        if (!VirtualProtect(Address, 0x1000, PAGE_EXECUTE_READ, &OldProtection))
            return NULL;
    }
    FlushInstructionCache(GetCurrentProcess(), Target, sizeof(Code));
    if (AllocationBase != NULL)
        *AllocationBase = Address;
    return Target;
#else
    UNREFERENCED_PARAMETER(Protection);
    return NULL;
#endif
}

int
main(int argc, char **argv)
{
    PFN_SET_PROCESS_VALID_CALL_TARGETS SetValidTargets;
    CFG_CALL_TARGET_INFO_LOCAL TargetInfo;
    PFN_CFG_TARGET Target;
    PVOID DynamicTarget, DynamicBase;
    MEMORY_BASIC_INFORMATION MemoryInformation;
    BOOL Result;
    DWORD Error;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 2)
        return 0x40;

    if (!strcmp(argv[1], "valid-local"))
        return CallIndirect(CfgLocalTarget) == 0x47f ? 0 : 0x41;

    if (!strcmp(argv[1], "valid-floating-arguments"))
        return TestFloatingArguments(FALSE);

    if (!strcmp(argv[1], "valid-vector-arguments"))
        return TestFloatingArguments(TRUE);

    if (!strcmp(argv[1], "valid-guarded"))
    {
        Target = LoadTarget(L"cfg_guarded.dll", "CfgGuardedTarget");
        return Target != NULL && CallIndirect(Target) == 0x47f ? 0 : 0x42;
    }

    if (!strcmp(argv[1], "plain-interior"))
    {
        Target = LoadTarget(L"cfg_plain.dll", "CfgPlainTarget");
        if (Target == NULL)
            return 0x43;
        CallIndirect((PFN_CFG_TARGET)((BYTE *)Target + sizeof(DWORD)));
        return 0;
    }

    if (!strcmp(argv[1], "invalid-aligned"))
    {
        CallIndirect((PFN_CFG_TARGET)((BYTE *)CfgInteriorTarget + 16));
        return 0x44;
    }

    if (!strcmp(argv[1], "invalid-misaligned"))
    {
        CallIndirect((PFN_CFG_TARGET)((BYTE *)CfgLocalTarget + 2));
        return 0x45;
    }

    if (!strcmp(argv[1], "dynamic-default"))
    {
        DynamicTarget = CreateDynamicTarget(PAGE_READWRITE, 0, &DynamicBase);
        if (DynamicTarget == NULL)
            return 0x46;
        return CallIndirect((PFN_CFG_TARGET)DynamicTarget) == 0x47f ? 0 : 0x47;
    }

    if (!strcmp(argv[1], "dynamic-invalid"))
    {
        DynamicTarget = CreateDynamicTarget(PAGE_EXECUTE_READWRITE | PAGE_TARGETS_INVALID, 0, &DynamicBase);
        if (DynamicTarget == NULL)
            return 0x48;
        CallIndirect((PFN_CFG_TARGET)DynamicTarget);
        return 0x49;
    }

    if (!strcmp(argv[1], "dynamic-register") || !strcmp(argv[1], "dynamic-marked"))
    {
        DynamicTarget = CreateDynamicTarget(PAGE_EXECUTE_READWRITE | PAGE_TARGETS_INVALID, 16, &DynamicBase);
        if (DynamicTarget == NULL)
            return 0x4a;
        SetValidTargets = (PFN_SET_PROCESS_VALID_CALL_TARGETS)LoadTarget(L"cfg_plain.dll", "CfgPlainSetProcessValidCallTargets");
        if (SetValidTargets == NULL)
            return 0x4b;
        if (VirtualQuery(DynamicTarget, &MemoryInformation, sizeof(MemoryInformation)) != sizeof(MemoryInformation))
            return 0x4c;
        TargetInfo.Offset = (ULONG_PTR)DynamicTarget - (ULONG_PTR)MemoryInformation.BaseAddress;
        TargetInfo.Flags = CFG_CALL_TARGET_VALID;
        SetLastError(ERROR_SUCCESS);
        Result = SetValidTargets(GetCurrentProcess(),
                                 MemoryInformation.BaseAddress,
                                 MemoryInformation.RegionSize,
                                 1,
                                 &TargetInfo);
        Error = GetLastError();
        printf("CFGCHILD|settargets|result=%u|error=%lu|base=%p|size=%Iu|offset=%Ix|flags=%Ix\n",
               Result,
               Error,
               MemoryInformation.BaseAddress,
               MemoryInformation.RegionSize,
               TargetInfo.Offset,
               TargetInfo.Flags);
        fflush(stdout);
        if (!Result)
            return 0x4d;
        if (!strcmp(argv[1], "dynamic-register"))
            return 0;
        return CallIndirect((PFN_CFG_TARGET)DynamicTarget) == 0x47f ? 0 : 0x4d;
    }

    return 0x4e;
}
