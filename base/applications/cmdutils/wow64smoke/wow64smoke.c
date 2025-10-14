#include <windows.h>
#include <stdio.h>

static BOOL
CheckWow64Support(VOID)
{
    BOOL isWow64 = FALSE;
    if (!IsWow64Process(GetCurrentProcess(), &isWow64))
    {
        DWORD error = GetLastError();
        fprintf(stderr, "[wow64smoke] IsWow64Process failed (%lu)\n", error);
        return FALSE;
    }

    if (!isWow64)
    {
        fprintf(stderr, "[wow64smoke] Process is not running under WOW64\n");
        return FALSE;
    }

    return TRUE;
}

static BOOL
CheckSysWow64Directory(VOID)
{
    CHAR buffer[MAX_PATH];
    UINT size = GetSystemWow64DirectoryA(buffer, sizeof(buffer));
    if (size == 0)
    {
        DWORD error = GetLastError();
        fprintf(stderr, "[wow64smoke] GetSystemWow64DirectoryA failed (%lu)\n", error);
        return FALSE;
    }

    printf("[wow64smoke] SysWOW64 directory: %s\n", buffer);
    return TRUE;
}

static VOID
DumpNativeSystemInfo(VOID)
{
    SYSTEM_INFO systemInfo;
    ZeroMemory(&systemInfo, sizeof(systemInfo));
    GetNativeSystemInfo(&systemInfo);

    printf("[wow64smoke] Native page size: %lu\n", systemInfo.dwPageSize);
    printf("[wow64smoke] Number of processors: %lu\n", systemInfo.dwNumberOfProcessors);
    printf("[wow64smoke] Processor architecture: %u\n", systemInfo.wProcessorArchitecture);
}

int
main(int argc, char **argv)
{
    BOOL ok = TRUE;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    printf("[wow64smoke] ReactOS WOW64 smoke test\n");
    printf("[wow64smoke] Pointer size (bytes): %zu\n", sizeof(PVOID));

    ok &= CheckWow64Support();
    ok &= CheckSysWow64Directory();
    DumpNativeSystemInfo();

    if (!ok)
    {
        fprintf(stderr, "[wow64smoke] Smoke test FAILED\n");
        return 1;
    }

    printf("[wow64smoke] Smoke test PASSED\n");
    return 0;
}
