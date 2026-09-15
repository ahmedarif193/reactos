#include "precomp.h"

BOOL WINAPI QueryProcessCycleTime(HANDLE ProcessHandle, PULONG64 CycleTime);

START_TEST(QueryProcessCycleTime)
{
    HANDLE Process;
    ULONG64 CycleTime;
    BOOL Result;

    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                          FALSE,
                          GetCurrentProcessId());
    ok(Process != NULL, "OpenProcess failed: %lu\n", GetLastError());
    if (Process == NULL)
        return;

    CycleTime = MAXULONGLONG;
    Result = QueryProcessCycleTime(Process, &CycleTime);
    ok(Result, "QueryProcessCycleTime failed: %lu\n", GetLastError());
    ok(CycleTime != MAXULONGLONG, "CycleTime was not written\n");

    CloseHandle(Process);
}
