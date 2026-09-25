/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that process termination finishes a window destructor
 *              abandoned in user mode
 */
#include "precomp.h"

typedef struct _DESTROY_SHARED
{
    HWND Window;
} DESTROY_SHARED;

static HANDLE DestroyReady, DestroyBlock;
static WNDPROC PreviousProc;

static LRESULT CALLBACK
DestroyProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = PreviousProc ? CallWindowProcW(PreviousProc, Window, Message, WParam, LParam)
                                 : DefWindowProcW(Window, Message, WParam, LParam);

    if (Message == WM_NCDESTROY)
    {
        /* The built-in procedure has processed WM_NCDESTROY, but the kernel
         * destructor cannot finish until this callback returns. */
        SetEvent(DestroyReady);
        WaitForSingleObject(DestroyBlock, INFINITE);
    }
    return Result;
}

static void
DestroyChild(HANDLE Mapping, UINT Builtin)
{
    DESTROY_SHARED *Shared;
    WNDCLASSW Class = {0};
    HWND Window;

    Shared = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*Shared));
    if (!Shared)
        ExitProcess(10);

    Class.lpfnWndProc = DestroyProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"User32InterruptedDestroy";
    if (!Builtin && !RegisterClassW(&Class))
        ExitProcess(11);

    Window = CreateWindowExW(0, Builtin ? L"STATIC" : Class.lpszClassName,
                            L"Interrupted destruction", WS_OVERLAPPEDWINDOW,
                            0, 0, 64, 64, NULL, NULL, Class.hInstance, NULL);
    if (!Window)
        ExitProcess(12);
    if (Builtin)
    {
        PreviousProc = (WNDPROC)SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)DestroyProc);
        if (!PreviousProc)
            ExitProcess(13);
    }
    Shared->Window = Window;
    DestroyWindow(Window);
    /* The parent never releases DestroyBlock; reaching here is unexpected. */
    ExitProcess(14);
}

static void
TestInterruptedDestroy(UINT Builtin)
{
    SECURITY_ATTRIBUTES Security = {sizeof(Security), NULL, TRUE};
    STARTUPINFOA Startup = {sizeof(Startup)};
    PROCESS_INFORMATION Child = {0};
    DESTROY_SHARED *Shared = NULL;
    HANDLE Mapping = NULL, Ready = NULL, Block = NULL;
    char Module[MAX_PATH], Command[MAX_PATH + 160];
    DWORD Wait, ExitCode = 0;
    BOOL Ret;

    winetest_push_context("%s class", Builtin ? "built-in" : "private");
    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &Security, PAGE_READWRITE,
                                0, sizeof(*Shared), NULL);
    Ready = CreateEventW(&Security, TRUE, FALSE, NULL);
    Block = CreateEventW(&Security, TRUE, FALSE, NULL);
    ok(Mapping && Ready && Block, "Failed to create synchronization objects: %lu\n", GetLastError());
    if (!Mapping || !Ready || !Block)
        goto Cleanup;
    Shared = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*Shared));
    ok(Shared != NULL, "MapViewOfFile failed: %lu\n", GetLastError());
    if (!Shared)
        goto Cleanup;

    GetModuleFileNameA(NULL, Module, ARRAY_SIZE(Module));
    sprintf(Command, "\"%s\" DestroyWindow child %p %p %p %u", Module, Mapping, Ready, Block, Builtin);
    Ret = CreateProcessA(NULL, Command, NULL, NULL, TRUE, 0, NULL, NULL, &Startup, &Child);
    ok(Ret, "CreateProcess failed: %lu\n", GetLastError());
    if (!Ret)
        goto Cleanup;

    Wait = WaitForSingleObject(Ready, 10000);
    ok(Wait == WAIT_OBJECT_0, "Child did not reach WM_NCDESTROY: %#lx\n", Wait);
    ok(Shared->Window != NULL, "Child did not publish its window\n");
    trace("Terminating child during WM_NCDESTROY, window %p\n", Shared->Window);
    Ret = TerminateProcess(Child.hProcess, 0x1234);
    ok(Ret, "TerminateProcess failed: %lu\n", GetLastError());
    if (!Ret)
        SetEvent(Block);
    Wait = WaitForSingleObject(Child.hProcess, 10000);
    ok(Wait == WAIT_OBJECT_0, "Child termination did not finish: %#lx\n", Wait);
    Ret = GetExitCodeProcess(Child.hProcess, &ExitCode);
    ok(Ret && ExitCode == 0x1234, "Unexpected child exit %#lx (query %d)\n", ExitCode, Ret);
    ok(!IsWindow(Shared->Window), "Window survived process termination\n");

Cleanup:
    if (Child.hThread) CloseHandle(Child.hThread);
    if (Child.hProcess) CloseHandle(Child.hProcess);
    if (Shared) UnmapViewOfFile(Shared);
    if (Block) CloseHandle(Block);
    if (Ready) CloseHandle(Ready);
    if (Mapping) CloseHandle(Mapping);
    winetest_pop_context();
}

START_TEST(DestroyWindow)
{
    int Argc;
    char **Argv;
    HANDLE Mapping;
    UINT Builtin;

    Argc = winetest_get_mainargs(&Argv);
    if (Argc == 7 && !strcmp(Argv[2], "child"))
    {
        if (sscanf(Argv[3], "%p", &Mapping) != 1 ||
            sscanf(Argv[4], "%p", &DestroyReady) != 1 ||
            sscanf(Argv[5], "%p", &DestroyBlock) != 1 ||
            sscanf(Argv[6], "%u", &Builtin) != 1)
            ExitProcess(9);
        DestroyChild(Mapping, Builtin);
        return;
    }

    TestInterruptedDestroy(0);
    TestInterruptedDestroy(1);
}
