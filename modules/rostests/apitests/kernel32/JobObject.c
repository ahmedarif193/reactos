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
}
