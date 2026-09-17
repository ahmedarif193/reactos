/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process mitigation policies: win32k lockdown, dynamic code, child process restriction
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TORTURE_CHILDREN 14

static DWORD
QueryPolicyFlags(HANDLE Process, PROCESS_MITIGATION_POLICY Policy)
{
    DWORD Flags = 0xFFFFFFFF;
    GetProcessMitigationPolicy(Process, Policy, &Flags, sizeof(Flags));
    return Flags;
}

static BOOL
SpawnMitigationHelper(PCSTR Mode,
                      LPPROC_THREAD_ATTRIBUTE_LIST Attributes,
                      PPROCESS_INFORMATION Info)
{
    WCHAR Application[MAX_PATH], *FileName;

    if (!GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)))
        return FALSE;
    FileName = wcsrchr(Application, L'\\');
    if (!FileName)
    {
        SetLastError(ERROR_BAD_PATHNAME);
        return FALSE;
    }
    if (FAILED(StringCchCopyW(FileName + 1,
                              ARRAYSIZE(Application) - (FileName + 1 - Application),
                              L"sandbox_helper.exe")))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    return SbxSpawnChildExecutable(Application, NULL, "Mitigations", Mode, NULL,
                                   NULL, Attributes, CREATE_NO_WINDOW, Info);
}

static DWORD
LaunchWithMitigationOptions(PCSTR Mode,
                            const ULONGLONG *Mitigations,
                            SIZE_T MitigationSize,
                            PDWORD ChildPolicy)
{
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    SIZE_T Size = 0;
    PROCESS_INFORMATION Info;
    DWORD ExitCode = 0xFFFFFFFF;

    InitializeProcThreadAttributeList(NULL, 2, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Attributes || !InitializeProcThreadAttributeList(Attributes, 2, 0, &Size))
    {
        ok(0, "InitializeProcThreadAttributeList failed %lu\n", GetLastError());
        return ExitCode;
    }
    if (Mitigations && MitigationSize)
        ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
                                     (PVOID)Mitigations, MitigationSize, NULL, NULL),
           "UpdateProcThreadAttribute(MITIGATION_POLICY) failed %lu\n", GetLastError());
    if (ChildPolicy)
        ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, ChildPolicy, sizeof(*ChildPolicy), NULL, NULL),
           "UpdateProcThreadAttribute(CHILD_PROCESS_POLICY) failed %lu\n", GetLastError());
    if (SpawnMitigationHelper(Mode, Attributes, &Info))
    {
        if (!strcmp(Mode, "win32k"))
        {
            DWORD Flags = QueryPolicyFlags(Info.hProcess, ProcessSystemCallDisablePolicy);
            ok(Flags & 1, "parent query of child SystemCallDisable policy: 0x%lx\n", Flags);
        }
        ExitCode = SbxWaitChild(&Info);
    }
    else
    {
        ok(0, "CreateProcess(%s) failed %lu\n", Mode, GetLastError());
    }
    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);
    return ExitCode;
}

static DWORD
LaunchWithMitigation(PCSTR Mode, ULONGLONG Mitigations, PDWORD ChildPolicy)
{
    return LaunchWithMitigationOptions(Mode, Mitigations ? &Mitigations : NULL,
                                       Mitigations ? sizeof(Mitigations) : 0,
                                       ChildPolicy);
}

static DWORD
LaunchWithComponentFilter(VOID)
{
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    COMPONENT_FILTER Filter = {COMPONENT_KTM};
    SIZE_T Size = 0;
    PROCESS_INFORMATION Info;
    DWORD ExitCode = 0xFFFFFFFF;

    InitializeProcThreadAttributeList(NULL, 1, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Attributes || !InitializeProcThreadAttributeList(Attributes, 1, 0, &Size))
    {
        ok(0, "InitializeProcThreadAttributeList(component filter) failed %lu\n",
           GetLastError());
        return ExitCode;
    }

    if (!UpdateProcThreadAttribute(Attributes, 0,
                                   PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER,
                                   &Filter, sizeof(Filter), NULL, NULL))
    {
        ok(0, "UpdateProcThreadAttribute(COMPONENT_FILTER) failed %lu\n",
           GetLastError());
    }
    else if (SpawnMitigationHelper("ktm", Attributes, &Info))
    {
        ExitCode = SbxWaitChild(&Info);
    }
    else
    {
        ok(0, "CreateProcess(ktm component filter) failed %lu\n", GetLastError());
    }

    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);
    return ExitCode;
}

static BOOL
GetSupportedMitigations(_Out_writes_(2) ULONGLONG Supported[2])
{
    ZeroMemory(Supported, 2 * sizeof(*Supported));
    return GetProcessMitigationPolicy(GetCurrentProcess(),
                                      ProcessMitigationOptionsMask,
                                      Supported, 2 * sizeof(*Supported));
}

static VOID
TestBrowserCreationMitigations(ULONGLONG Supported[2])
{
    ULONGLONG Options[2] = {0, 0};
    const ULONGLONG RequiredWord1 =
        PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE |
        PROCESS_CREATION_MITIGATION_POLICY_FORCE_RELOCATE_IMAGES_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_STRICT_HANDLE_CHECKS_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_BLOCK_NON_MICROSOFT_BINARIES_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_FONT_DISABLE_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_REMOTE_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_PREFER_SYSTEM32_ALWAYS_ON;
    DWORD ExitCode, CetPolicy = 0;

    ok((Supported[0] & RequiredWord1) == RequiredWord1,
       "Windows 11 mask lacks browser creation mitigations: supported 0x%I64x required 0x%I64x\n",
       Supported[0], RequiredWord1);

    ExitCode = LaunchWithMitigation(
        "basic",
        PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE |
        PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON,
        NULL);
    ok(ExitCode == 0, "DEP/heap-termination child exit 0x%lx\n", ExitCode);

    ok((Supported[1] & PROCESS_CREATION_MITIGATION_POLICY2_MODULE_TAMPERING_PROTECTION_ALWAYS_ON) != 0,
       "Windows 11 mask lacks Chromium module-tampering mitigation: 0x%I64x\n", Supported[1]);
    ok((Supported[1] & PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_INDIRECT_BRANCH_PREDICTION_ALWAYS_ON) != 0,
       "Windows 11 mask lacks Chromium indirect-branch mitigation: 0x%I64x\n", Supported[1]);
    ok((Supported[1] & PROCESS_CREATION_MITIGATION_POLICY2_FSCTL_SYSTEM_CALL_DISABLE_ALWAYS_ON) != 0,
       "Windows 11 mask lacks Chromium FSCTL mitigation: 0x%I64x\n", Supported[1]);
    ok((Supported[1] & PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_CORE_SHARING_ALWAYS_ON) != 0,
       "Windows 11 mask lacks Chromium core-sharing mitigation: 0x%I64x\n", Supported[1]);

    Options[1] = Supported[1] &
        PROCESS_CREATION_MITIGATION_POLICY2_MODULE_TAMPERING_PROTECTION_ALWAYS_ON;
    if (Options[1])
    {
        ExitCode = LaunchWithMitigationOptions("policy2", Options, sizeof(Options), NULL);
        ok(ExitCode == 0, "module-tampering child exit 0x%lx\n", ExitCode);
    }

    Options[1] = Supported[1] &
        PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_INDIRECT_BRANCH_PREDICTION_ALWAYS_ON;
    if (Options[1])
    {
        ExitCode = LaunchWithMitigationOptions("policy2", Options, sizeof(Options), NULL);
        ok(ExitCode == 0, "indirect-branch child exit 0x%lx\n", ExitCode);
    }

    Options[1] = Supported[1] &
        (PROCESS_CREATION_MITIGATION_POLICY2_MODULE_TAMPERING_PROTECTION_ALWAYS_ON |
         PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_INDIRECT_BRANCH_PREDICTION_ALWAYS_ON);
    if (Options[1])
    {
        ExitCode = LaunchWithMitigationOptions("policy2", Options, sizeof(Options), NULL);
        ok(ExitCode == 0, "combined policy2 child exit 0x%lx\n", ExitCode);
    }

    Options[1] = Supported[1] &
        PROCESS_CREATION_MITIGATION_POLICY2_FSCTL_SYSTEM_CALL_DISABLE_ALWAYS_ON;
    if (Options[1])
    {
        ExitCode = LaunchWithMitigationOptions("fsctl", Options, sizeof(Options), NULL);
        ok(ExitCode == 0, "FSCTL-disable child exit 0x%lx\n", ExitCode);
    }
    else
    {
        skip("FSCTL system-call mitigation is not advertised by this Windows 11 build\n");
    }

    Options[1] = Supported[1] &
        PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_CORE_SHARING_ALWAYS_ON;
    if (Options[1])
    {
        ExitCode = LaunchWithMitigationOptions("core", Options, sizeof(Options), NULL);
        ok(ExitCode == 0, "restrict-core-sharing child exit 0x%lx\n", ExitCode);
    }
    else
    {
        skip("restrict-core-sharing is not advertised by this Windows 11 machine\n");
    }

    if (GetProcessMitigationPolicy(GetCurrentProcess(),
                                   SBX_PROCESS_USER_SHADOW_STACK_POLICY,
                                   &CetPolicy, sizeof(CetPolicy)) &&
        (CetPolicy & 0x1))
    {
        Options[1] = Supported[1] &
            PROCESS_CREATION_MITIGATION_POLICY2_CET_USER_SHADOW_STACKS_ALWAYS_OFF;
        ok(Options[1] != 0, "CET-enabled parent has no CET creation mask\n");
        if (Options[1])
        {
            ExitCode = LaunchWithMitigationOptions("cetoff", Options, sizeof(Options), NULL);
            ok(ExitCode == 0, "CET-disable child exit 0x%lx\n", ExitCode);
        }

        Options[1] = Supported[1] &
            PROCESS_CREATION_MITIGATION_POLICY2_CET_USER_SHADOW_STACKS_STRICT_MODE;
        if (Options[1] == PROCESS_CREATION_MITIGATION_POLICY2_CET_USER_SHADOW_STACKS_STRICT_MODE)
        {
            ExitCode = LaunchWithMitigationOptions("cetstrict", Options, sizeof(Options), NULL);
            ok(ExitCode == 0, "CET-strict child exit 0x%lx\n", ExitCode);
        }

        Options[1] = Supported[1] &
            PROCESS_CREATION_MITIGATION_POLICY2_CET_DYNAMIC_APIS_OUT_OF_PROC_ONLY_ALWAYS_OFF;
        if (Options[1])
        {
            ExitCode = LaunchWithMitigationOptions("cetdynamic", Options, sizeof(Options), NULL);
            ok(ExitCode == 0, "CET dynamic-API child exit 0x%lx\n", ExitCode);
        }
    }
    else
    {
        skip("CET is not enabled for the native ARM64 test image\n");
    }
}

static void
TestLocalPolicies(void)
{
    PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY SysCall;
    PROCESS_MITIGATION_CHILD_PROCESS_POLICY Child;
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY Dynamic;
    DWORD Bad = 0xFF;

    ZeroMemory(&SysCall, sizeof(SysCall));
    ok(GetProcessMitigationPolicy(GetCurrentProcess(), ProcessSystemCallDisablePolicy, &SysCall, sizeof(SysCall)),
       "GetProcessMitigationPolicy(SystemCallDisable) failed %lu\n", GetLastError());
    ok(SysCall.Flags == 0, "SystemCallDisable flags 0x%lx\n", SysCall.Flags);
    ZeroMemory(&Child, sizeof(Child));
    ok(GetProcessMitigationPolicy(GetCurrentProcess(), ProcessChildProcessPolicy, &Child, sizeof(Child)),
       "GetProcessMitigationPolicy(ChildProcess) failed %lu\n", GetLastError());
    ok(Child.Flags == 0, "ChildProcess flags 0x%lx\n", Child.Flags);
    ZeroMemory(&Dynamic, sizeof(Dynamic));
    ok(GetProcessMitigationPolicy(GetCurrentProcess(), ProcessDynamicCodePolicy, &Dynamic, sizeof(Dynamic)),
       "GetProcessMitigationPolicy(DynamicCode) failed %lu\n", GetLastError());
    ok(Dynamic.Flags == 0, "DynamicCode flags 0x%lx\n", Dynamic.Flags);

    ok(!SetProcessMitigationPolicy(ProcessSystemCallDisablePolicy, &Bad, sizeof(Bad)), "invalid SystemCallDisable flags accepted\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "error %lu\n", GetLastError());
    ok(!SetProcessMitigationPolicy(ProcessChildProcessPolicy, &Bad, sizeof(Bad)), "invalid ChildProcess flags accepted\n");
    ok(!SetProcessMitigationPolicy(ProcessSystemCallDisablePolicy, &SysCall, 2), "bad length accepted\n");
}

START_TEST(Mitigations)
{
    DWORD ExitCode, Restricted = PROCESS_CREATION_CHILD_PROCESS_RESTRICTED;
    PROCESS_INFORMATION Children[TORTURE_CHILDREN];
    ULONGLONG Supported[2];
    ULONG Index;

    TestLocalPolicies();
    ok(GetSupportedMitigations(Supported),
       "GetProcessMitigationPolicy(OptionsMask) failed %lu\n", GetLastError());
    trace("supported creation mitigations: word1=0x%I64x word2=0x%I64x\n",
          Supported[0], Supported[1]);
    TestBrowserCreationMitigations(Supported);

    ExitCode = LaunchWithMitigation("noop", 0, NULL);
    ok(ExitCode == 0, "noop child exit 0x%lx\n", ExitCode);

    ExitCode = LaunchWithMitigation("win32k", PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON, NULL);
    ok(ExitCode == 0, "win32k lockdown child exit 0x%lx\n", ExitCode);

    ExitCode = LaunchWithMitigation("win32krt", 0, NULL);
    ok(ExitCode == 0, "runtime win32k lockdown child exit 0x%lx\n", ExitCode);

    ExitCode = LaunchWithMitigation("dyncode", PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON, NULL);
    ok(ExitCode == 0, "dynamic code child exit 0x%lx\n", ExitCode);

    ExitCode = LaunchWithMitigation("signature", PROCESS_CREATION_MITIGATION_POLICY_BLOCK_NON_MICROSOFT_BINARIES_ALWAYS_ON, NULL);
    ok(ExitCode == 0, "Microsoft-signed-only child exit 0x%lx\n", ExitCode);

    ExitCode = LaunchWithMitigation("dllsearch", 0, NULL);
    ok(ExitCode == 0, "DLL-search-order child exit 0x%lx\n", ExitCode);

    ExitCode = LaunchWithComponentFilter();
    ok(ExitCode == 0, "KTM component-filter child exit 0x%lx\n", ExitCode);

    ExitCode = LaunchWithMitigation("childpolicy", 0, &Restricted);
    ok(ExitCode == 0, "child policy child exit 0x%lx\n", ExitCode);

    ExitCode = LaunchWithMitigation("combined",
                                    PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON |
                                    PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON |
                                    PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE,
                                    &Restricted);
    ok(ExitCode == 0, "combined mitigation child exit 0x%lx\n", ExitCode);

    ZeroMemory(Children, sizeof(Children));
    for (Index = 0; Index < TORTURE_CHILDREN; Index++)
    {
        LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
        SIZE_T Size = 0;
        SIZE_T MitigationSize = sizeof(ULONGLONG);
        ULONGLONG Mitigations[2] = {0, 0};
        DWORD *ChildPolicy = NULL;
        COMPONENT_FILTER ComponentFilter = {0};
        BOOL UseComponentFilter = FALSE;
        PCSTR Mode;

        if (Index % 7 == 0)
        {
            Mode = "dyncode";
            Mitigations[0] = PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON;
        }
        else if (Index % 7 == 1)
        {
            Mode = "win32k";
            Mitigations[0] = PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON;
        }
        else if (Index % 7 == 2)
        {
            Mode = "combined";
            Mitigations[0] = PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON |
                             PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON;
            ChildPolicy = &Restricted;
        }
        else if (Index % 7 == 3)
        {
            Mode = "policy2";
            Mitigations[1] = Supported[1] &
                PROCESS_CREATION_MITIGATION_POLICY2_MODULE_TAMPERING_PROTECTION_ALWAYS_ON;
            MitigationSize = Mitigations[1] ? sizeof(Mitigations) : 0;
        }
        else if (Index % 7 == 4)
        {
            Mode = "policy2";
            Mitigations[1] = Supported[1] &
                PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_INDIRECT_BRANCH_PREDICTION_ALWAYS_ON;
            MitigationSize = Mitigations[1] ? sizeof(Mitigations) : 0;
        }
        else if (Index % 7 == 5)
        {
            Mode = "dllsearch";
            MitigationSize = 0;
        }
        else
        {
            Mode = "ktm";
            MitigationSize = 0;
            ComponentFilter.ComponentFlags = COMPONENT_KTM;
            UseComponentFilter = TRUE;
        }

        InitializeProcThreadAttributeList(NULL, ChildPolicy ? 2 : 1, 0, &Size);
        Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
        InitializeProcThreadAttributeList(Attributes, ChildPolicy ? 2 : 1, 0, &Size);
        if (MitigationSize)
            UpdateProcThreadAttribute(Attributes, 0,
                                      PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
                                      Mitigations, MitigationSize, NULL, NULL);
        if (ChildPolicy)
            UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY,
                                      ChildPolicy, sizeof(*ChildPolicy), NULL, NULL);
        if (UseComponentFilter)
            UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER,
                                      &ComponentFilter, sizeof(ComponentFilter), NULL, NULL);
        if (!SpawnMitigationHelper(Mode, Attributes, &Children[Index]))
            ok(0, "torture child %lu creation failed %lu\n", Index, GetLastError());
        DeleteProcThreadAttributeList(Attributes);
        HeapFree(GetProcessHeap(), 0, Attributes);
    }
    for (Index = 0; Index < TORTURE_CHILDREN; Index++)
    {
        if (!Children[Index].hProcess) continue;
        ExitCode = SbxWaitChild(&Children[Index]);
        ok(ExitCode == 0, "torture child %lu exit 0x%lx\n", Index, ExitCode);
    }
}
