#include "precomp.h"

START_TEST(QueryProcessCycleTime)
{
    BOOL (WINAPI *pQueryProcessCycleTime)(HANDLE, PULONG64) =
        (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryProcessCycleTime");
    HANDLE Process;
    ULONG64 CycleTime;
    BOOL Result;

    if (!pQueryProcessCycleTime)
    {
        skip("QueryProcessCycleTime is not exported\n");
        return;
    }

    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                          FALSE,
                          GetCurrentProcessId());
    ok(Process != NULL, "OpenProcess failed: %lu\n", GetLastError());
    if (Process == NULL)
        return;

    CycleTime = MAXULONGLONG;
    Result = pQueryProcessCycleTime(Process, &CycleTime);
    ok(Result, "QueryProcessCycleTime failed: %lu\n", GetLastError());
    ok(CycleTime != MAXULONGLONG, "CycleTime was not written\n");

    CloseHandle(Process);
}
