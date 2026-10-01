/* SPDX-FileCopyrightText: 2026 Ahmed ARIF
/* SPDX-License-Identifier: GPL-3.0-or-later
 * Boot service for the independent NT4 PowerPC compiler probes.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static SERVICE_STATUS_HANDLE StatusHandle;
static SERVICE_STATUS Status;
static HANDLE Log;

static void text(const char *s)
{
    DWORD n = 0, written;
    while (s[n]) ++n;
    WriteFile(Log, s, n, &written, NULL);
    FlushFileBuffers(Log);
}

static void number(DWORD n)
{
    char s[11];
    unsigned i;
    s[0] = '0'; s[1] = 'x'; s[10] = 0;
    for (i = 0; i != 8; ++i) s[i+2] = "0123456789abcdef"[(n >> (28-4*i)) & 15];
    text(s);
}

static void WINAPI control(DWORD code)
{
    (void)code;
    SetServiceStatus(StatusHandle, &Status);
}

static void run(char *command)
{
    static STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    DWORD result, exitcode;
    startup.cb = sizeof(startup);
    text("START "); text(command); text("\r\n");
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, "C:\\ppcabi", &startup, &process))
    {
        text("CREATE FAILED "); number(GetLastError()); text("\r\n");
        return;
    }
    result = WaitForSingleObject(process.hProcess, 120000);
    if (result != WAIT_OBJECT_0)
    {
        text("TIMEOUT/WAIT FAILED "); number(result); text("\r\n");
        TerminateProcess(process.hProcess, 98);
        WaitForSingleObject(process.hProcess, 10000);
    }
    if (GetExitCodeProcess(process.hProcess, &exitcode))
    { text("EXIT "); number(exitcode); text("\r\n"); }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

static void WINAPI service_main(DWORD argc, char **argv)
{
    static const char *cases[] = {
#define PPC_ABI_CASE(name) #name,
#include "cases.def"
#undef PPC_ABI_CASE
    };
    char command[80];
    unsigned optimization, i, j, n;
    (void)argc; (void)argv;
    StatusHandle = RegisterServiceCtrlHandlerA("PpcAbiProbe", control);
    if (!StatusHandle) return;
    Status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    Status.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(StatusHandle, &Status);
    Log = CreateFileA("C:\\ppcabi\\runner.log", GENERIC_WRITE, FILE_SHARE_READ,
                      NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    text("NT4 PPC ABI boot service\r\n");
    for (optimization = 0; optimization != 2; ++optimization)
        for (i = 0; i != sizeof(cases)/sizeof(cases[0]); ++i)
        {
            const char *prefix = optimization ? "C:\\ppcabi\\abi-o2.exe " :
                                                "C:\\ppcabi\\abi-o0.exe ";
            for (n = 0; prefix[n]; ++n) command[n] = prefix[n];
            for (j = 0; cases[i][j]; ++j) command[n++] = cases[i][j];
            command[n] = 0;
            run(command);
        }
    text("COMPLETE\r\n");
    CloseHandle(Log);
    Status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(StatusHandle, &Status);
}

void WINAPI service_entry(void)
{
    SERVICE_TABLE_ENTRYA table[] = {{"PpcAbiProbe", service_main}, {NULL, NULL}};
    StartServiceCtrlDispatcherA(table);
    ExitProcess(0);
}
