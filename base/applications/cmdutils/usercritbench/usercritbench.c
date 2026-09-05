/*
 * PROJECT:     ReactOS Utilities
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     USER critical section scalability bench
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define BENCH_PROP L"UserCritBenchProp"
#define OWNER_VALUE ((HANDLE)(ULONG_PTR)0x1234)
#define CHILD_VALUE ((HANDLE)(ULONG_PTR)0x5678)

typedef struct _WORKER
{
    HANDLE hThread;
    HWND hwnd;
    volatile LONG *pStop;
    ULONGLONG ops;
    ULONGLONG errors;
} WORKER, *PWORKER;

static DWORD WINAPI ReaderThread(LPVOID Param)
{
    PWORKER w = Param;
    WCHAR cls[64];

    while (!*w->pStop)
    {
        GetAsyncKeyState(VK_SHIFT);
        if (GetPropW(w->hwnd, BENCH_PROP) != OWNER_VALUE) w->errors++;
        GetKeyState(VK_CONTROL);
        GetForegroundWindow();
        if (!GetClassNameW(w->hwnd, cls, 64)) w->errors++;
        if (!GetThreadDesktop(GetCurrentThreadId())) w->errors++;
        w->ops += 6;
    }
    return 0;
}

static DWORD WINAPI WriterThread(LPVOID Param)
{
    PWORKER w = Param;
    HWND h;

    while (!*w->pStop)
    {
        h = CreateWindowExW(0, L"STATIC", L"ucb", WS_POPUP, 0, 0, 10, 10,
                            NULL, NULL, GetModuleHandleW(NULL), NULL);
        if (!h)
        {
            w->errors++;
            continue;
        }
        SetPropW(h, BENCH_PROP, CHILD_VALUE);
        if (GetPropW(h, BENCH_PROP) != CHILD_VALUE) w->errors++;
        RemovePropW(h, BENCH_PROP);
        DestroyWindow(h);
        w->ops += 5;
    }
    return 0;
}

static void RunPhase(const WCHAR *Name, HWND hwnd, int Seconds, int Readers, int Writers)
{
    WORKER *rd = calloc(Readers, sizeof(WORKER));
    WORKER *wr = calloc(Writers ? Writers : 1, sizeof(WORKER));
    volatile LONG stop = 0;
    LARGE_INTEGER f, t0, t1;
    ULONGLONG rops = 0, wops = 0, errs = 0;
    double secs;
    int i;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < Readers; i++)
    {
        rd[i].hwnd = hwnd;
        rd[i].pStop = &stop;
        rd[i].hThread = CreateThread(NULL, 0, ReaderThread, &rd[i], 0, NULL);
    }
    for (i = 0; i < Writers; i++)
    {
        wr[i].hwnd = hwnd;
        wr[i].pStop = &stop;
        wr[i].hThread = CreateThread(NULL, 0, WriterThread, &wr[i], 0, NULL);
    }
    Sleep(Seconds * 1000);
    InterlockedExchange(&stop, 1);
    for (i = 0; i < Readers; i++)
    {
        WaitForSingleObject(rd[i].hThread, INFINITE);
        CloseHandle(rd[i].hThread);
        rops += rd[i].ops;
        errs += rd[i].errors;
    }
    for (i = 0; i < Writers; i++)
    {
        WaitForSingleObject(wr[i].hThread, INFINITE);
        CloseHandle(wr[i].hThread);
        wops += wr[i].ops;
        errs += wr[i].errors;
    }
    QueryPerformanceCounter(&t1);
    secs = (double)(t1.QuadPart - t0.QuadPart) / (double)f.QuadPart;

    printf("USERCRIT_RESULT phase=%ls readers=%d writers=%d secs=%.2f reader_ops=%llu reader_ops_per_s=%.0f writer_ops=%llu writer_ops_per_s=%.0f errors=%llu\n",
           Name, Readers, Writers, secs, rops, rops / secs, wops, wops / secs, errs);
    fflush(stdout);
    free(rd);
    free(wr);
}

int wmain(int argc, WCHAR *argv[])
{
    int seconds = argc > 1 ? _wtoi(argv[1]) : 5;
    int readers = argc > 2 ? _wtoi(argv[2]) : 4;
    int writers = argc > 3 ? _wtoi(argv[3]) : 1;
    HWND hwnd;

    hwnd = CreateWindowExW(0, L"STATIC", L"ucb-owner", WS_POPUP, 0, 0, 10, 10,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!hwnd)
    {
        printf("USERCRIT_RESULT error=CreateWindow %lu\n", GetLastError());
        return 1;
    }
    SetPropW(hwnd, BENCH_PROP, OWNER_VALUE);

    RunPhase(L"reader1", hwnd, seconds, 1, 0);
    RunPhase(L"readersN", hwnd, seconds, readers, 0);
    RunPhase(L"mixed", hwnd, seconds, readers, writers);
    RunPhase(L"writer1", hwnd, seconds, 0, 1);

    RemovePropW(hwnd, BENCH_PROP);
    DestroyWindow(hwnd);
    printf("USERCRIT_DONE\n");
    return 0;
}
