/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Job object commit limits
 */

#include "precomp.h"

#define CHILD_FIRST_COMMIT_FAILED 0x1
#define CHILD_OVERCOMMIT_ALLOWED 0x2
#define CHILD_WRONG_ERROR 0x4
#define CHILD_RESERVE_FAILED 0x8
#define CHILD_RELEASE_NOT_REUSABLE 0x10

#define MB (1024 * 1024)

static DWORD
RunChild(SIZE_T FirstCommit, SIZE_T SecondCommit)
{
    DWORD Failures = 0;
    PVOID First, Second, Reserve, Again;

    First = VirtualAlloc(NULL, FirstCommit, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!First)
    {
        SbxChildFail(&Failures, CHILD_FIRST_COMMIT_FAILED, "first commit", GetLastError());
        return Failures;
    }

    SetLastError(0xdeadbeef);
    Second = VirtualAlloc(NULL, SecondCommit, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Second)
    {
        SbxChildFail(&Failures, CHILD_OVERCOMMIT_ALLOWED, "commit above the job limit succeeded", 0);
        VirtualFree(Second, 0, MEM_RELEASE);
    }
    else if (GetLastError() != ERROR_COMMITMENT_LIMIT)
    {
        SbxChildFail(&Failures, CHILD_WRONG_ERROR, "commit above the job limit error", GetLastError());
    }

    Reserve = VirtualAlloc(NULL, 256 * MB, MEM_RESERVE, PAGE_NOACCESS);
    if (!Reserve)
        SbxChildFail(&Failures, CHILD_RESERVE_FAILED, "reservation counted against the commit limit", GetLastError());
    else
        VirtualFree(Reserve, 0, MEM_RELEASE);

    VirtualFree(First, 0, MEM_RELEASE);
    Again = VirtualAlloc(NULL, FirstCommit, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Again)
        SbxChildFail(&Failures, CHILD_RELEASE_NOT_REUSABLE, "released commit not returned to the job", GetLastError());
    else
        VirtualFree(Again, 0, MEM_RELEASE);

    return Failures;
}

static void
RunLimitCase(DWORD LimitFlag, SIZE_T Limit, DWORD ExpectedMessage, PCSTR Tag)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits;
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT Port;
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    PROCESS_INFORMATION Info;
    HANDLE Job, CompletionPort;
    SIZE_T Size = 0;
    DWORD ExitCode, Bytes, Length;
    ULONG_PTR Key;
    LPOVERLAPPED Overlapped;
    BOOL SawLimit = FALSE;
    ULONG Round;

    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "%s: CreateJobObject failed %lu\n", Tag, GetLastError());
    if (!Job) return;

    CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ok(CompletionPort != NULL, "%s: CreateIoCompletionPort failed %lu\n", Tag, GetLastError());
    Port.CompletionKey = (PVOID)(ULONG_PTR)0x5B;
    Port.CompletionPort = CompletionPort;
    ok(SetInformationJobObject(Job, JobObjectAssociateCompletionPortInformation, &Port, sizeof(Port)),
       "%s: completion port association failed %lu\n", Tag, GetLastError());

    ZeroMemory(&Limits, sizeof(Limits));
    Limits.BasicLimitInformation.LimitFlags = LimitFlag | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (LimitFlag == JOB_OBJECT_LIMIT_PROCESS_MEMORY)
        Limits.ProcessMemoryLimit = Limit;
    else
        Limits.JobMemoryLimit = Limit;
    ok(SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)),
       "%s: memory limit failed %lu\n", Tag, GetLastError());

    ZeroMemory(&Limits, sizeof(Limits));
    ok(QueryInformationJobObject(Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits), &Length),
       "%s: limit query failed %lu\n", Tag, GetLastError());
    ok((LimitFlag == JOB_OBJECT_LIMIT_PROCESS_MEMORY ? Limits.ProcessMemoryLimit : Limits.JobMemoryLimit) == Limit,
       "%s: limit not stored\n", Tag);

    InitializeProcThreadAttributeList(NULL, 1, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    ok(Attributes && InitializeProcThreadAttributeList(Attributes, 1, 0, &Size), "%s: attribute list failed\n", Tag);
    ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &Job, sizeof(Job), NULL, NULL),
       "%s: JOB_LIST failed %lu\n", Tag, GetLastError());

    ok(SbxSpawnChild("JobMemory", LimitFlag == JOB_OBJECT_LIMIT_PROCESS_MEMORY ? "process" : "job",
                     NULL, NULL, Attributes, CREATE_NO_WINDOW, &Info),
       "%s: child spawn failed %lu\n", Tag, GetLastError());
    if (Info.hProcess)
    {
        ExitCode = SbxWaitChild(&Info);
        ok(ExitCode == 0, "%s: child failed with 0x%lx\n", Tag, ExitCode);
    }
    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);

    for (Round = 0; Round < 16; Round++)
    {
        if (!GetQueuedCompletionStatus(CompletionPort, &Bytes, &Key, &Overlapped, 200))
            break;
        if (Key == 0x5B && Bytes == ExpectedMessage)
            SawLimit = TRUE;
    }
    ok(SawLimit, "%s: memory limit message %lu not posted\n", Tag, ExpectedMessage);

    ZeroMemory(&Limits, sizeof(Limits));
    ok(QueryInformationJobObject(Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits), &Length),
       "%s: peak query failed %lu\n", Tag, GetLastError());
    if (LimitFlag == JOB_OBJECT_LIMIT_PROCESS_MEMORY)
        ok(Limits.PeakProcessMemoryUsed >= 32 * MB && Limits.PeakProcessMemoryUsed <= Limit,
           "%s: peak process memory %Iu\n", Tag, Limits.PeakProcessMemoryUsed);
    else
        ok(Limits.PeakJobMemoryUsed >= 32 * MB &&
           Limits.PeakJobMemoryUsed >= Limits.PeakProcessMemoryUsed,
           "%s: peak job memory %Iu, peak process memory %Iu\n", Tag,
           Limits.PeakJobMemoryUsed, Limits.PeakProcessMemoryUsed);

    CloseHandle(CompletionPort);
    CloseHandle(Job);
}

static void
TestBreakaway(BOOL AllowBreakaway)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits;
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    PROCESS_INFORMATION Info, Grandchild;
    STARTUPINFOW Startup;
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH * 2];
    DWORD ExitCode;
    ULONGLONG Options = PROCESS_CREATION_MITIGATION_POLICY_FORCE_RELOCATE_IMAGES_ALWAYS_ON;
    HANDLE Job;
    SIZE_T Size = 0;
    BOOL Success, InJob = TRUE;

    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObject failed %lu\n", GetLastError());
    if (!Job) return;
    ZeroMemory(&Limits, sizeof(Limits));
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                              (AllowBreakaway ? JOB_OBJECT_LIMIT_BREAKAWAY_OK : 0);
    ok(SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)),
       "job limits failed %lu\n", GetLastError());

    GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application));
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" JobMemory child noop", Application);
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    ok(CreateProcessW(Application, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &Startup, &Info),
       "parent child spawn failed %lu\n", GetLastError());
    if (!Info.hProcess) { CloseHandle(Job); return; }
    ok(AssignProcessToJobObject(Job, Info.hProcess), "AssignProcessToJobObject failed %lu\n", GetLastError());

    InitializeProcThreadAttributeList(NULL, 2, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    ok(Attributes && InitializeProcThreadAttributeList(Attributes, 2, 0, &Size), "attribute list failed\n");
    ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &Info.hProcess, sizeof(HANDLE), NULL, NULL),
       "PARENT_PROCESS attribute failed %lu\n", GetLastError());
    ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &Options, sizeof(Options), NULL, NULL),
       "MITIGATION_POLICY attribute failed %lu\n", GetLastError());

    {
        STARTUPINFOEXW StartupEx;
        ZeroMemory(&StartupEx, sizeof(StartupEx));
        StartupEx.StartupInfo.cb = sizeof(StartupEx);
        StartupEx.lpAttributeList = Attributes;
        SetLastError(0xdeadbeef);
        Success = CreateProcessW(Application, CommandLine, NULL, NULL, FALSE,
                                 CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB | EXTENDED_STARTUPINFO_PRESENT,
                                 NULL, NULL, &StartupEx.StartupInfo, &Grandchild);
    }
    if (AllowBreakaway)
    {
        ok(Success, "breakaway denied although the job allows it (%lu)\n", GetLastError());
        if (Success)
        {
            ok(IsProcessInJob(Grandchild.hProcess, Job, &InJob) && !InJob, "broken-away child is still in the job\n");
            TerminateProcess(Grandchild.hProcess, 0);
            CloseHandle(Grandchild.hThread);
            CloseHandle(Grandchild.hProcess);
        }
    }
    else
    {
        ok(!Success && GetLastError() == ERROR_ACCESS_DENIED, "breakaway allowed without BREAKAWAY_OK: %d %lu\n", Success, GetLastError());
        if (Success)
        {
            TerminateProcess(Grandchild.hProcess, 0);
            CloseHandle(Grandchild.hThread);
            CloseHandle(Grandchild.hProcess);
        }
    }

    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);
    TerminateProcess(Info.hProcess, 0);
    WaitForSingleObject(Info.hProcess, 5000);
    GetExitCodeProcess(Info.hProcess, &ExitCode);
    CloseHandle(Info.hThread);
    CloseHandle(Info.hProcess);
    CloseHandle(Job);
}

START_TEST(JobMemory)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);

    if (SbxIsChild(Arguments, Count, "noop")) ExitProcess(0);

    if (SbxIsChild(Arguments, Count, "process")) ExitProcess(RunChild(32 * MB, 96 * MB));
    if (SbxIsChild(Arguments, Count, "job")) ExitProcess(RunChild(32 * MB, 72 * MB));

    RunLimitCase(JOB_OBJECT_LIMIT_PROCESS_MEMORY, 96 * MB, JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT, "process limit");
    RunLimitCase(JOB_OBJECT_LIMIT_JOB_MEMORY, 96 * MB, JOB_OBJECT_MSG_JOB_MEMORY_LIMIT, "job limit");
    TestBreakaway(TRUE);
    TestBreakaway(FALSE);
}
