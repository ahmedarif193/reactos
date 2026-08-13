/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Test job object completion port notifications
 */

#include "precomp.h"

static BOOL expect_completion(HANDLE port, DWORD expected_message, ULONG_PTR expected_key, DWORD expected_pid)
{
    OVERLAPPED *overlapped = NULL;
    ULONG_PTR key = 0;
    DWORD message = 0;
    BOOL ret;

    ret = GetQueuedCompletionStatus(port, &message, &key, &overlapped, 2000);
    ok(ret, "GetQueuedCompletionStatus failed with %lu\n", GetLastError());
    if (!ret) return FALSE;

    ok(message == expected_message, "Expected message %lu, got %lu\n", expected_message, message);
    ok(key == expected_key, "Expected key %p, got %p\n", (PVOID)expected_key, (PVOID)key);
    ok((DWORD)(ULONG_PTR)overlapped == expected_pid, "Expected process %lu, got %lu\n", expected_pid, (DWORD)(ULONG_PTR)overlapped);
    return message == expected_message && key == expected_key && (DWORD)(ULONG_PTR)overlapped == expected_pid;
}

static VOID test_kill_on_close(PCWSTR application)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info;
    PROCESS_INFORMATION process_info;
    STARTUPINFOW startup_info;
    WCHAR command_line[MAX_PATH * 2];
    HANDLE duplicate_job = NULL;
    HANDLE job;
    DWORD exit_code;
    DWORD result;
    DWORD suspend_count;
    BOOL ret;

    RtlZeroMemory(&limit_info, sizeof(limit_info));
    RtlZeroMemory(&process_info, sizeof(process_info));
    RtlZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    StringCchPrintfW(command_line, _countof(command_line), L"\"%s\" JobObject child", application);

    ret = CreateProcessW(application, command_line, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startup_info, &process_info);
    ok(ret, "CreateProcessW failed with %lu\n", GetLastError());
    if (!ret) return;

    job = CreateJobObjectW(NULL, NULL);
    ok(job != NULL, "CreateJobObjectW failed with %lu\n", GetLastError());
    if (!job) goto cleanup;

    limit_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    ret = SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info));
    ok(ret, "SetInformationJobObject failed with %lu\n", GetLastError());
    if (!ret) goto cleanup;

    ret = AssignProcessToJobObject(job, process_info.hProcess);
    ok(ret, "AssignProcessToJobObject failed with %lu\n", GetLastError());
    if (!ret) goto cleanup;

    ret = DuplicateHandle(GetCurrentProcess(), job, GetCurrentProcess(), &duplicate_job, 0, FALSE, DUPLICATE_SAME_ACCESS);
    ok(ret, "DuplicateHandle failed with %lu\n", GetLastError());
    if (!ret) goto cleanup;

    suspend_count = ResumeThread(process_info.hThread);
    ok(suspend_count != (DWORD)-1, "ResumeThread failed with %lu\n", GetLastError());
    if (suspend_count == (DWORD)-1) goto cleanup;

    Sleep(100);
    ret = CloseHandle(job);
    ok(ret, "CloseHandle(job) failed with %lu\n", GetLastError());
    if (!ret) goto cleanup;
    job = NULL;
    result = WaitForSingleObject(process_info.hProcess, 250);
    ok(result == WAIT_TIMEOUT, "Child exited while a duplicate job handle remained: %#lx\n", result);
    ret = CloseHandle(duplicate_job);
    ok(ret, "CloseHandle(duplicate_job) failed with %lu\n", GetLastError());
    if (!ret) goto cleanup;
    duplicate_job = NULL;
    result = WaitForSingleObject(process_info.hProcess, 5000);
    ok(result == WAIT_OBJECT_0, "Kill-on-close child wait returned %#lx\n", result);
    exit_code = STILL_ACTIVE;
    ret = GetExitCodeProcess(process_info.hProcess, &exit_code);
    ok(ret, "GetExitCodeProcess failed with %lu\n", GetLastError());
    ok(exit_code == 0, "Expected kill-on-close exit code 0, got %#lx\n", exit_code);

cleanup:
    if (duplicate_job) CloseHandle(duplicate_job);
    if (job) CloseHandle(job);
    TerminateProcess(process_info.hProcess, 0);
    WaitForSingleObject(process_info.hProcess, 5000);
    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
}

START_TEST(JobObject)
{
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT port_info;
    PROCESS_INFORMATION process_info;
    STARTUPINFOW startup_info;
    WCHAR application[MAX_PATH];
    WCHAR command_line[MAX_PATH * 2];
    HANDLE job = NULL;
    HANDLE port = NULL;
    BOOL ret;
    int argc;
    char **argv;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        Sleep(INFINITE);
        return;
    }

    RtlZeroMemory(&startup_info, sizeof(startup_info));
    RtlZeroMemory(&process_info, sizeof(process_info));
    startup_info.cb = sizeof(startup_info);

    GetModuleFileNameW(NULL, application, _countof(application));
    StringCchPrintfW(command_line, _countof(command_line), L"\"%s\" JobObject child", application);
    ret = CreateProcessW(application, command_line, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startup_info, &process_info);
    if (!ret)
    {
        skip("CreateProcessW failed with %lu\n", GetLastError());
        return;
    }

    job = CreateJobObjectW(NULL, NULL);
    ok(job != NULL, "CreateJobObjectW failed with %lu\n", GetLastError());
    if (!job) goto cleanup;

    ret = AssignProcessToJobObject(job, process_info.hProcess);
    ok(ret, "AssignProcessToJobObject failed with %lu\n", GetLastError());
    if (!ret) goto cleanup;

    port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ok(port != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    if (!port) goto cleanup;

    port_info.CompletionKey = job;
    port_info.CompletionPort = port;
    ret = SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &port_info, sizeof(port_info));
    ok(ret, "SetInformationJobObject failed with %lu\n", GetLastError());
    if (!ret) goto cleanup;

    expect_completion(port, JOB_OBJECT_MSG_NEW_PROCESS, (ULONG_PTR)job, process_info.dwProcessId);

    ResumeThread(process_info.hThread);
    ret = TerminateProcess(process_info.hProcess, 0);
    ok(ret, "TerminateProcess failed with %lu\n", GetLastError());
    WaitForSingleObject(process_info.hProcess, 5000);

    expect_completion(port, JOB_OBJECT_MSG_EXIT_PROCESS, (ULONG_PTR)job, process_info.dwProcessId);
    expect_completion(port, JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO, (ULONG_PTR)job, 0);

cleanup:
    if (process_info.hProcess)
    {
        TerminateProcess(process_info.hProcess, 0);
        WaitForSingleObject(process_info.hProcess, 5000);
        CloseHandle(process_info.hProcess);
    }
    if (process_info.hThread) CloseHandle(process_info.hThread);
    if (port) CloseHandle(port);
    if (job) CloseHandle(job);
    test_kill_on_close(application);
}
