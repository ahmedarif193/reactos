#define STANDALONE
#include <wine/test.h>

#ifdef __REACTOS__
void winetest_wait_child_process_info(const PROCESS_INFORMATION *info)
{
    DWORD ret;

    winetest_ok(info->hProcess != NULL, "No child process handle (CreateProcess failed?)\n");
    if (!info->hProcess)
        return;

    ret = WaitForSingleObject(info->hProcess, 30000);
    if (ret == WAIT_TIMEOUT)
        winetest_ok(0, "Timed out waiting for the child process\n");
    else if (ret != WAIT_OBJECT_0)
        winetest_ok(0, "Could not wait for the child process: %d le=%u\n", (UINT)ret, (UINT)GetLastError());
    else
    {
        DWORD exit_code;

        GetExitCodeProcess(info->hProcess, &exit_code);
        if (exit_code > 255)
        {
            winetest_print_lock();
            if (winetest_color)
                winetest_printf(winetest_color_bright_red);
            winetest_print_location("unhandled exception %08x in child process %04x\n", (UINT)exit_code, (UINT)info->dwProcessId);
            if (winetest_color)
                winetest_printf(winetest_color_reset);
            winetest_print_unlock();
            InterlockedIncrement(&winetest_failures);
        }
        else if (exit_code)
        {
            winetest_print_lock();
            winetest_print_location("%u failures in child process\n", (UINT)exit_code);
            winetest_print_unlock();
            while (exit_code-- > 0)
                InterlockedIncrement(&winetest_failures);
        }

        if (!CloseHandle(info->hProcess))
            ok(0, "failed to close process handle, error %lu\n", GetLastError());
        CloseHandle(info->hThread);
    }
}
#endif
