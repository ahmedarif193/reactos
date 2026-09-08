/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <tlhelp32.h>
static DWORD ProcessId;
static HWND MainWindow;
static BOOL CALLBACK FindWindowForProcess(HWND window, LPARAM unused)
{
    DWORD pid;
    WCHAR title[128];
    GetWindowThreadProcessId(window, &pid);
    if (pid != ProcessId) return TRUE;
    GetWindowTextW(window, title, 128);
    if (title[0] && MainWindow != window) printf("FAN_WINDOW title=%ls visible=%d hwnd=%p\n", title, IsWindowVisible(window), window);
    if (IsWindowVisible(window) && !wcscmp(title, L"Raspberry Pi 5 Fan Control")) { MainWindow = window; return FALSE; }
    return TRUE;
}
static void SampleThread(HANDLE thread, HANDLE process)
{
    CONTEXT context;
    ULONG64 stack[20];
    SIZE_T size;
    unsigned j;
    BOOL matched;
    HANDLE snapshot;
    MODULEENTRY32W module = {sizeof(module)};
    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_FULL;
    if (SuspendThread(thread) == (DWORD)-1) return;
    if (GetThreadContext(thread, &context))
    {
        printf("FAN_THREAD pc=%I64x lr=%I64x fp=%I64x sp=%I64x\n", context.Pc, context.Lr, context.Fp, context.Sp);
        if (ReadProcessMemory(process, (void *)(ULONG_PTR)context.Sp, stack, sizeof(stack), &size))
            for (j = 0; j < 20; j++) printf("FAN_STACK %u %I64x\n", j, stack[j]);
    }
    ResumeThread(thread);
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ProcessId);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        if (Module32FirstW(snapshot, &module)) do
        {
            matched = (context.Pc >= (ULONG_PTR)module.modBaseAddr && context.Pc < (ULONG_PTR)module.modBaseAddr + module.modBaseSize) || (context.Lr >= (ULONG_PTR)module.modBaseAddr && context.Lr < (ULONG_PTR)module.modBaseAddr + module.modBaseSize);
            for (j = 0; j < 20; j++) if (stack[j] >= (ULONG_PTR)module.modBaseAddr && stack[j] < (ULONG_PTR)module.modBaseAddr + module.modBaseSize) matched = TRUE;
            if (matched) printf("FAN_MODULE %ls base=%p size=%lx\n", module.szModule, module.modBaseAddr, module.modBaseSize);
        } while (Module32NextW(snapshot, &module));
        CloseHandle(snapshot);
    }
}
int main(void)
{
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    WCHAR command[] = L"D:\\RPi5FanControl\\GUI\\Rpi5FanControl.exe";
    DWORD i, code;
    DWORD_PTR response;
    RECT client;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, 0, NULL, L"D:\\RPi5FanControl\\GUI", &startup, &process)) { printf("FAN_START_FAILED error=%lu\n", GetLastError()); return 1; }
    ProcessId = process.dwProcessId;
    for (i = 0; i < 50; ++i)
    {
        if (WaitForSingleObject(process.hProcess, 100) == WAIT_OBJECT_0) { GetExitCodeProcess(process.hProcess, &code); printf("FAN_EXIT_BEFORE_READY code=%lx\n", code); return 1; }
        if (i == 10)
        {
            THREADENTRY32 entry = {sizeof(entry)};
            HANDLE threads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (threads != INVALID_HANDLE_VALUE)
            {
                if (Thread32First(threads, &entry)) do
                {
                    if (entry.th32OwnerProcessID == ProcessId)
                    {
                        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
                        printf("FAN_THREAD_ID %lu\n", entry.th32ThreadID);
                        if (thread) { SampleThread(thread, process.hProcess); CloseHandle(thread); }
                    }
                } while (Thread32Next(threads, &entry));
                CloseHandle(threads);
            }
        }
        EnumWindows(FindWindowForProcess, 0);
        if (MainWindow && SendMessageTimeoutW(MainWindow, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, &response))
        {
            GetClientRect(MainWindow, &client);
            if (client.right > 0 && client.bottom > 0)
            {
                printf("FAN_WINDOW_READY title=Raspberry Pi 5 Fan Control client=%ldx%ld pid=%lu\n", client.right, client.bottom, ProcessId);
                CloseHandle(process.hThread); CloseHandle(process.hProcess);
                return 0;
            }
        }
    }
    puts("FAN_WINDOW_NOT_READY");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return 1;
}
