/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Address waits must make progress across thread priorities.
 */

#include <windows.h>
#include <wine/test.h>

typedef NTSTATUS (NTAPI *wait_address_fn)(const void *, const void *, SIZE_T, const LARGE_INTEGER *);
typedef void (NTAPI *wake_address_fn)(const void *);

static wait_address_fn wait_address;
static wake_address_fn wake_all, wake_single;
static volatile LONG stop_worker, completed, failed;
static LONG address;
static HANDLE ready;

static DWORD WINAPI lower_priority_worker(void *arg)
{
    LONG compare = 1;

    SetEvent(ready);
    while (!InterlockedCompareExchange(&stop_worker, 0, 0))
    {
        /* Unequal values must return immediately, even when a higher-priority
         * waker preempts this call while it owns an internal address lock. */
        if (wait_address(&address, &compare, sizeof(compare), NULL))
            InterlockedIncrement(&failed);
        InterlockedIncrement(&completed);
    }
    return 0;
}

static DWORD run_child(void)
{
    DWORD_PTR process_mask, system_mask, cpu;
    HANDLE thread;
    DWORD i, result;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) return 1;
    cpu = process_mask & (0 - process_mask);
    if (!SetThreadAffinityMask(GetCurrentThread(), cpu)) return 2;
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)) return 3;
    if (!SetThreadPriorityBoost(GetCurrentThread(), TRUE)) return 4;
    ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ready) return 5;
    thread = CreateThread(NULL, 0, lower_priority_worker, NULL, CREATE_SUSPENDED, NULL);
    if (!thread) return 6;
    if (!SetThreadAffinityMask(thread, cpu)) return 7;
    if (!SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL)) return 8;
    if (!SetThreadPriorityBoost(thread, TRUE)) return 9;
    if (ResumeThread(thread) != 1) return 10;
    if (WaitForSingleObject(ready, 5000) != WAIT_OBJECT_0) return 11;

    for (i = 0; i < 512; ++i)
    {
        /* Blocking between wakes gives the lower-priority owner CPU time.
         * The timed wake can then preempt it inside the address operation. */
        Sleep(1);
        if (i & 1) wake_all(&address);
        else wake_single(&address);
    }
    InterlockedExchange(&stop_worker, 1);
    result = WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    CloseHandle(ready);
    return result == WAIT_OBJECT_0 && completed > 0 && !failed ? 0 : 12;
}

START_TEST(RtlWaitOnAddressPriority)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION process;
    char path[MAX_PATH], command[MAX_PATH + 96], **argv;
    DWORD result, exit_code;
    int argc, priority;
    BOOL ret;

    wait_address = (void *)GetProcAddress(ntdll, "RtlWaitOnAddress");
    wake_all = (void *)GetProcAddress(ntdll, "RtlWakeAddressAll");
    wake_single = (void *)GetProcAddress(ntdll, "RtlWakeAddressSingle");
    if (!wait_address || !wake_all || !wake_single)
    {
        win_skip("Address wait exports unavailable\n");
        return;
    }
    argc = winetest_get_mainargs(&argv);
    if (argc > 2 && !strcmp(argv[2], "priority-child")) ExitProcess(run_child());

    /* Bound a broken implementation in a separate process. The watchdog must
     * be able to run even on a single-CPU system with a spinning child. */
    priority = GetThreadPriority(GetCurrentThread());
    ret = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    ok(ret, "Setting watchdog priority failed %lu\n", GetLastError());
    if (!ret) return;
    GetModuleFileNameA(NULL, path, ARRAY_SIZE(path));
    sprintf(command, "\"%s\" RtlWaitOnAddressPriority priority-child", path);
    startup.cb = sizeof(startup);
    ret = CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process);
    ok(ret, "CreateProcess failed %lu\n", GetLastError());
    if (ret)
    {
        result = WaitForSingleObject(process.hProcess, 30000);
        ok(result == WAIT_OBJECT_0, "Priority contention did not finish: %#lx\n", result);
        if (result == WAIT_OBJECT_0)
        {
            ret = GetExitCodeProcess(process.hProcess, &exit_code);
            ok(ret, "GetExitCodeProcess failed %lu\n", GetLastError());
            if (ret) ok(!exit_code, "Priority contention child exit code %lu\n", exit_code);
        }
        else
        {
            /* Terminate only the test child, so a deadlock cannot hang the
             * remaining API suite. */
            TerminateProcess(process.hProcess, 13);
            ok(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0,
               "Timed-out child did not terminate\n");
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    SetThreadPriority(GetCurrentThread(), priority);
}
