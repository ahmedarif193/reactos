/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Browser broker job lifetime, process-count and notification semantics
 */

#include "precomp.h"

#define JOB_COMPLETION_KEY ((ULONG_PTR)0x5342584a)
#define JOB_TERMINATE_STATUS 0x5a

static LPPROC_THREAD_ATTRIBUTE_LIST
CreateJobAttributeList(PHANDLE Job)
{
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    SIZE_T Size = 0;
    BOOL Initialized;

    InitializeProcThreadAttributeList(NULL, 1, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Attributes)
        return NULL;
    Initialized = InitializeProcThreadAttributeList(Attributes, 1, 0, &Size);
    if (!Initialized ||
        !UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                   Job, sizeof(*Job), NULL, NULL))
    {
        if (Initialized) DeleteProcThreadAttributeList(Attributes);
        HeapFree(GetProcessHeap(), 0, Attributes);
        return NULL;
    }
    return Attributes;
}

static void
FreeJobAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST Attributes)
{
    if (!Attributes) return;
    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);
}

static BOOL
SpawnInJob(HANDLE Job, PCSTR Mode, DWORD Flags, PPROCESS_INFORMATION Info)
{
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    BOOL Result;

    Attributes = CreateJobAttributeList(&Job);
    if (!Attributes)
        return FALSE;
    Result = SbxSpawnChild("JobLifecycle", Mode, NULL, NULL, Attributes,
                           Flags, Info);
    FreeJobAttributeList(Attributes);
    return Result;
}

static void
CloseProcessInformation(PPROCESS_INFORMATION Info)
{
    if (Info->hThread) CloseHandle(Info->hThread);
    if (Info->hProcess) CloseHandle(Info->hProcess);
    ZeroMemory(Info, sizeof(*Info));
}

static void
TestActiveProcessLimit(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION Accounting;
    PROCESS_INFORMATION First, Second;
    HANDLE Job;
    DWORD Error, Length;
    BOOL Result, InJob = FALSE;

    ZeroMemory(&First, sizeof(First));
    ZeroMemory(&Second, sizeof(Second));
    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObject failed %lu\n", GetLastError());
    if (!Job) return;
    ZeroMemory(&Limits, sizeof(Limits));
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    Limits.BasicLimitInformation.ActiveProcessLimit = 1;
    ok(SetInformationJobObject(Job, JobObjectExtendedLimitInformation,
                               &Limits, sizeof(Limits)),
       "active-process limit failed %lu\n", GetLastError());

    ok(SpawnInJob(Job, "wait", CREATE_SUSPENDED, &First),
       "first process launch failed %lu\n", GetLastError());
    if (!First.hProcess) goto Cleanup;
    ok(IsProcessInJob(First.hProcess, Job, &InJob) && InJob,
       "first process was not assigned at creation (%lu)\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = SpawnInJob(Job, "noop", CREATE_SUSPENDED, &Second);
    Error = GetLastError();
    ok(!Result, "active-process limit allowed a second target\n");
    if (Result)
    {
        TerminateProcess(Second.hProcess, 0);
        CloseProcessInformation(&Second);
    }
    else
    {
        ok(Error == ERROR_NOT_ENOUGH_QUOTA,
           "active-process rejection error %lu\n", Error);
    }

    ZeroMemory(&Accounting, sizeof(Accounting));
    ok(QueryInformationJobObject(Job, JobObjectBasicAccountingInformation,
                                 &Accounting, sizeof(Accounting), &Length),
       "job accounting query failed %lu\n", GetLastError());
    ok(Accounting.ActiveProcesses == 1,
       "active process count %lu\n", Accounting.ActiveProcesses);
    ok(Accounting.TotalProcesses == 1 && Accounting.TotalTerminatedProcesses == 0,
       "process totals %lu/%lu\n", Accounting.TotalProcesses,
       Accounting.TotalTerminatedProcesses);

Cleanup:
    if (First.hProcess)
    {
        TerminateProcess(First.hProcess, 0);
        WaitForSingleObject(First.hProcess, 5000);
        CloseProcessInformation(&First);
    }
    CloseHandle(Job);
}

static void
TestTerminateJob(void)
{
    PROCESS_INFORMATION Info;
    HANDLE Job;
    BOOL InJob = FALSE;
    DWORD ExitCode = STILL_ACTIVE;

    ZeroMemory(&Info, sizeof(Info));
    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObject failed %lu\n", GetLastError());
    if (!Job) return;
    ok(SpawnInJob(Job, "wait", 0, &Info),
       "terminate-job target launch failed %lu\n", GetLastError());
    if (!Info.hProcess) goto Cleanup;
    ok(IsProcessInJob(Info.hProcess, Job, &InJob) && InJob,
       "terminate-job target is not in its job\n");
    ok(TerminateJobObject(Job, JOB_TERMINATE_STATUS),
       "TerminateJobObject failed %lu\n", GetLastError());
    ok(WaitForSingleObject(Info.hProcess, 5000) == WAIT_OBJECT_0,
       "TerminateJobObject did not stop the target\n");
    ok(GetExitCodeProcess(Info.hProcess, &ExitCode) &&
       ExitCode == JOB_TERMINATE_STATUS,
       "terminated target exit code 0x%lx\n", ExitCode);

Cleanup:
    if (Info.hProcess)
    {
        if (WaitForSingleObject(Info.hProcess, 0) != WAIT_OBJECT_0)
            TerminateProcess(Info.hProcess, 0);
        CloseProcessInformation(&Info);
    }
    CloseHandle(Job);
}

static void
TestKillOnJobClose(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits;
    PROCESS_INFORMATION Info;
    HANDLE Job;
    BOOL InJob = FALSE;
    DWORD ExitCode = STILL_ACTIVE;

    ZeroMemory(&Info, sizeof(Info));
    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObject failed %lu\n", GetLastError());
    if (!Job) return;
    ZeroMemory(&Limits, sizeof(Limits));
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    ok(SetInformationJobObject(Job, JobObjectExtendedLimitInformation,
                               &Limits, sizeof(Limits)),
       "kill-on-close setup failed %lu\n", GetLastError());
    ok(SpawnInJob(Job, "wait", CREATE_SUSPENDED, &Info),
       "kill-on-close target launch failed %lu\n", GetLastError());
    if (!Info.hProcess)
    {
        CloseHandle(Job);
        return;
    }
    ok(IsProcessInJob(Info.hProcess, Job, &InJob) && InJob,
       "kill-on-close target is not in its job\n");

    CloseHandle(Job);
    Job = NULL;
    ok(WaitForSingleObject(Info.hProcess, 5000) == WAIT_OBJECT_0,
       "closing the last kill-on-close job handle did not stop the target\n");
    ok(GetExitCodeProcess(Info.hProcess, &ExitCode) && ExitCode != STILL_ACTIVE,
       "kill-on-close target remained active\n");
    CloseProcessInformation(&Info);
}

static void
TestCompletionPortLifecycle(void)
{
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT Association;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION Accounting;
    PROCESS_INFORMATION Info;
    HANDLE Job, Port;
    DWORD ExitCode, Message, Length;
    ULONG_PTR Key;
    LPOVERLAPPED Value;
    BOOL SawNew = FALSE, SawExit = FALSE, SawZero = FALSE;
    ULONG Attempt;

    Job = CreateJobObjectW(NULL, NULL);
    Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ZeroMemory(&Info, sizeof(Info));
    ok(Job != NULL && Port != NULL, "job/completion port creation failed %lu\n",
       GetLastError());
    if (!Job || !Port) goto Cleanup;
    Association.CompletionKey = (PVOID)JOB_COMPLETION_KEY;
    Association.CompletionPort = Port;
    ok(SetInformationJobObject(Job, JobObjectAssociateCompletionPortInformation,
                               &Association, sizeof(Association)),
       "job completion-port association failed %lu\n", GetLastError());

    ok(SpawnInJob(Job, "noop", 0, &Info),
       "completion target launch failed %lu\n", GetLastError());
    if (!Info.hProcess) goto Cleanup;
    ExitCode = SbxWaitChild(&Info);
    ok(ExitCode == 0, "completion target failed with 0x%lx\n", ExitCode);

    for (Attempt = 0; Attempt < 16 && !SawZero; Attempt++)
    {
        Key = 0;
        Message = 0;
        Value = NULL;
        if (!GetQueuedCompletionStatus(Port, &Message, &Key, &Value, 1000))
            continue;
        ok(Key == JOB_COMPLETION_KEY, "completion key %Ix\n", Key);
        if (Message == JOB_OBJECT_MSG_NEW_PROCESS)
            SawNew = TRUE;
        else if (Message == JOB_OBJECT_MSG_EXIT_PROCESS)
            SawExit = TRUE;
        else if (Message == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO)
            SawZero = TRUE;
    }
    ok(SawNew, "JOB_OBJECT_MSG_NEW_PROCESS was not delivered\n");
    ok(SawExit, "JOB_OBJECT_MSG_EXIT_PROCESS was not delivered\n");
    ok(SawZero, "JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO was not delivered\n");

    ZeroMemory(&Accounting, sizeof(Accounting));
    ok(QueryInformationJobObject(Job, JobObjectBasicAccountingInformation,
                                 &Accounting, sizeof(Accounting), &Length),
       "final accounting query failed %lu\n", GetLastError());
    ok(Accounting.TotalProcesses == 1 && Accounting.ActiveProcesses == 0 &&
       Accounting.TotalTerminatedProcesses == 0,
       "final process totals %lu/%lu/%lu\n", Accounting.TotalProcesses,
       Accounting.ActiveProcesses, Accounting.TotalTerminatedProcesses);

Cleanup:
    if (Port) CloseHandle(Port);
    if (Job) CloseHandle(Job);
}

START_TEST(JobLifecycle)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);

    if (SbxIsChild(Arguments, Count, "noop")) ExitProcess(0);
    if (SbxIsChild(Arguments, Count, "wait"))
    {
        Sleep(60000);
        ExitProcess(0);
    }

    TestActiveProcessLimit();
    TestTerminateJob();
    TestKillOnJobClose();
    TestCompletionPortLifecycle();
}
