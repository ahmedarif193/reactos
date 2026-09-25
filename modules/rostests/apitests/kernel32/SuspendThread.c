/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Resume must release every pending suspend, including rapid reuse.
 */

#include <windows.h>
#include <wine/test.h>

static volatile LONG stop_worker, heartbeat;
static HANDLE ready;

static DWORD WINAPI worker(void *arg)
{
    SetEvent(ready);
    while (!InterlockedCompareExchange(&stop_worker, 0, 0))
    {
        InterlockedIncrement(&heartbeat);
        if ((heartbeat & 255) == 0) SwitchToThread();
    }
    return 123;
}

START_TEST(SuspendThread)
{
    HANDLE thread;
    DWORD result, previous, exit_code, i, round;
    DWORD_PTR process_mask, system_mask, old_affinity = 0, first, second;
    CONTEXT context;
    LONG before;

    ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(ready != NULL, "CreateEvent failed %lu\n", GetLastError());
    if (!ready) return;
    thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    ok(thread != NULL, "CreateThread failed %lu\n", GetLastError());
    if (!thread) { CloseHandle(ready); return; }
    result = WaitForSingleObject(ready, 5000);
    ok(result == WAIT_OBJECT_0, "Worker startup returned %#lx\n", result);

    /* Put the caller and target on different CPUs when possible, so resume can
     * race with delivery of the preceding suspend APC. Restore caller affinity. */
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask))
    {
        first = process_mask & (0-process_mask);
        second = process_mask & ~first;
        second &= 0-second;
        if (first && second)
        {
            old_affinity = SetThreadAffinityMask(GetCurrentThread(), first);
            SetThreadAffinityMask(thread, second);
        }
    }

    previous = SuspendThread(thread);
    ok(previous == 0, "First suspend returned %lu\n", previous);
    previous = SuspendThread(thread);
    ok(previous == 1, "Nested suspend returned %lu\n", previous);
    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL;
    ok(GetThreadContext(thread, &context), "GetThreadContext failed %lu\n", GetLastError());
    before = InterlockedCompareExchange(&heartbeat, 0, 0);
    previous = ResumeThread(thread);
    ok(previous == 2, "Partial resume returned %lu\n", previous);
    Sleep(20);
    ok(heartbeat == before, "Worker ran while still suspended\n");
    previous = ResumeThread(thread);
    ok(previous == 1, "Final resume returned %lu\n", previous);

    for (round = 0; round < 16; ++round)
    {
        for (i = 0; i < 4096; ++i)
        {
            previous = SuspendThread(thread);
            ok(previous == 0, "Round %lu iteration %lu suspend returned %lu\n", round, i, previous);
            previous = ResumeThread(thread);
            ok(previous == 1, "Round %lu iteration %lu resume returned %lu\n", round, i, previous);
        }
        before = InterlockedCompareExchange(&heartbeat, 0, 0);
        for (i = 0; i < 100 && heartbeat == before; ++i) Sleep(10);
        ok(heartbeat != before, "Round %lu: worker stranded after balanced suspend/resume\n", round);
        if (heartbeat == before) break;
    }

    InterlockedExchange(&stop_worker, 1);
    result = WaitForSingleObject(thread, 3000);
    ok(result == WAIT_OBJECT_0, "Worker did not exit after all suspends were resumed: %#lx\n", result);
    if (result == WAIT_OBJECT_0)
    {
        GetExitCodeThread(thread, &exit_code);
        ok(exit_code == 123, "Worker exit code %lu\n", exit_code);
    }
    if (old_affinity) SetThreadAffinityMask(GetCurrentThread(), old_affinity);
    CloseHandle(thread);
    CloseHandle(ready);
}
