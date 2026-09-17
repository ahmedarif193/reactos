/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Job object UI restriction enforcement
 */

#include "precomp.h"

#define CHILD_DESKTOP_CREATED 0x1
#define CHILD_DESKTOP_SWITCHED 0x2
#define CHILD_DISPLAY_CHANGED 0x4
#define CHILD_GLOBAL_ATOM_VISIBLE 0x8
#define CHILD_PRIVATE_ATOM_FAILED 0x10
#define CHILD_CLIPBOARD_READ 0x20
#define CHILD_CLIPBOARD_WRITTEN 0x40
#define CHILD_CLIPBOARD_EMPTY_FAILED 0x80
#define CHILD_SPI_GET_FAILED 0x100
#define CHILD_SPI_SET_ALLOWED 0x200
#define CHILD_FOREIGN_HANDLE_USED 0x400
#define CHILD_GRANTED_HANDLE_BLOCKED 0x800
#define CHILD_OWN_WINDOW_FAILED 0x1000
#define CHILD_OWN_HANDLE_BLOCKED 0x2000
#define CHILD_NOT_IN_JOB 0x4000
#define CHILD_EXITWINDOWS_ALLOWED 0x8000

#define JOB_ATOM_NAME L"SbxJobUiAtom"

static HWND gBlockedWindow, gGrantedWindow;

static LRESULT CALLBACK
JobWindowProc(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hwnd, Msg, wParam, lParam);
}

static DWORD
RunChild(HWND Blocked, HWND Granted)
{
    DWORD Failures = 0;
    HDESK Desktop;
    BOOL InJob = FALSE, Beep = FALSE;
    HWND Own;
    HGLOBAL Data;

    if (!IsProcessInJob(GetCurrentProcess(), NULL, &InJob) || !InJob)
        SbxChildFail(&Failures, CHILD_NOT_IN_JOB, "not in job", GetLastError());

    Desktop = CreateDesktopW(L"sbx_job_desktop", NULL, NULL, 0, GENERIC_ALL, NULL);
    if (Desktop) { SbxChildFail(&Failures, CHILD_DESKTOP_CREATED, "CreateDesktop allowed", 0); CloseDesktop(Desktop); }
    if (SwitchDesktop(GetThreadDesktop(GetCurrentThreadId())))
        SbxChildFail(&Failures, CHILD_DESKTOP_SWITCHED, "SwitchDesktop allowed", 0);
    if (ChangeDisplaySettingsW(NULL, 0) == DISP_CHANGE_SUCCESSFUL)
        SbxChildFail(&Failures, CHILD_DISPLAY_CHANGED, "ChangeDisplaySettings allowed", 0);
    if (GlobalFindAtomW(JOB_ATOM_NAME) != 0)
        SbxChildFail(&Failures, CHILD_GLOBAL_ATOM_VISIBLE, "global atom visible", 0);
    if (GlobalAddAtomW(L"SbxJobUiChildAtom") == 0)
        SbxChildFail(&Failures, CHILD_PRIVATE_ATOM_FAILED, "job-private atom", GetLastError());
    if (ExitWindowsEx(0x80000000, 0))
        SbxChildFail(&Failures, CHILD_EXITWINDOWS_ALLOWED, "ExitWindowsEx allowed", 0);

    if (OpenClipboard(NULL))
    {
        if (GetClipboardData(CF_UNICODETEXT)) SbxChildFail(&Failures, CHILD_CLIPBOARD_READ, "clipboard read allowed", 0);
        Data = GlobalAlloc(GMEM_MOVEABLE, 8);
        if (SetClipboardData(CF_TEXT, Data)) SbxChildFail(&Failures, CHILD_CLIPBOARD_WRITTEN, "clipboard write allowed", 0);
        else GlobalFree(Data);
        if (!EmptyClipboard()) SbxChildFail(&Failures, CHILD_CLIPBOARD_EMPTY_FAILED, "EmptyClipboard failed", GetLastError());
        CloseClipboard();
    }

    if (!SystemParametersInfoW(SPI_GETBEEP, 0, &Beep, 0))
        SbxChildFail(&Failures, CHILD_SPI_GET_FAILED, "SPI_GETBEEP", GetLastError());
    if (SystemParametersInfoW(SPI_SETBEEP, Beep, NULL, 0))
        SbxChildFail(&Failures, CHILD_SPI_SET_ALLOWED, "SPI_SETBEEP allowed", 0);

    if (PostMessageW(Blocked, WM_NULL, 0, 0))
        SbxChildFail(&Failures, CHILD_FOREIGN_HANDLE_USED, "foreign window handle usable", 0);
    if (!PostMessageW(Granted, WM_NULL, 0, 0))
        SbxChildFail(&Failures, CHILD_GRANTED_HANDLE_BLOCKED, "granted window handle blocked", GetLastError());

    Own = CreateWindowExW(0, L"STATIC", L"sbx", WS_OVERLAPPED, 0, 0, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!Own) SbxChildFail(&Failures, CHILD_OWN_WINDOW_FAILED, "CreateWindow", GetLastError());
    else
    {
        if (!PostMessageW(Own, WM_NULL, 0, 0)) SbxChildFail(&Failures, CHILD_OWN_HANDLE_BLOCKED, "own window blocked", GetLastError());
        DestroyWindow(Own);
    }
    return Failures;
}

START_TEST(JobUi)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);
    HANDLE Job;
    JOBOBJECT_BASIC_UI_RESTRICTIONS Ui;
    WNDCLASSW Class;
    PROCESS_INFORMATION Info;
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    SIZE_T Size = 0;
    WCHAR Extra[64];
    HGLOBAL Text;
    DWORD ExitCode;
    ATOM Atom;

    if (SbxIsChild(Arguments, Count, "target") && Count >= 6)
        ExitProcess(RunChild((HWND)(ULONG_PTR)_strtoui64(Arguments[4], NULL, 10), (HWND)(ULONG_PTR)_strtoui64(Arguments[5], NULL, 10)));

    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObject failed %lu\n", GetLastError());
    Ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_ALL;
    ok(SetInformationJobObject(Job, JobObjectBasicUIRestrictions, &Ui, sizeof(Ui)), "UI restrictions failed %lu\n", GetLastError());

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = JobWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"SbxJobUiWindow";
    RegisterClassW(&Class);
    gBlockedWindow = CreateWindowExW(0, L"SbxJobUiWindow", L"blocked", WS_OVERLAPPED, 0, 0, 20, 20, NULL, NULL, Class.hInstance, NULL);
    gGrantedWindow = CreateWindowExW(0, L"SbxJobUiWindow", L"granted", WS_OVERLAPPED, 0, 0, 20, 20, NULL, NULL, Class.hInstance, NULL);
    ok(gBlockedWindow && gGrantedWindow, "window creation failed %lu\n", GetLastError());
    ok(UserHandleGrantAccess(gGrantedWindow, Job, TRUE), "UserHandleGrantAccess failed %lu\n", GetLastError());

    Atom = GlobalAddAtomW(JOB_ATOM_NAME);
    ok(Atom != 0, "GlobalAddAtom failed %lu\n", GetLastError());
    if (OpenClipboard(NULL))
    {
        EmptyClipboard();
        Text = GlobalAlloc(GMEM_MOVEABLE, 8);
        if (Text) { wcscpy(GlobalLock(Text), L"sbx"); GlobalUnlock(Text); SetClipboardData(CF_UNICODETEXT, Text); }
        CloseClipboard();
    }

    InitializeProcThreadAttributeList(NULL, 1, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    ok(Attributes && InitializeProcThreadAttributeList(Attributes, 1, 0, &Size), "attribute list failed %lu\n", GetLastError());
    ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &Job, sizeof(Job), NULL, NULL), "JOB_LIST attribute failed %lu\n", GetLastError());
    StringCchPrintfW(Extra, ARRAYSIZE(Extra), L"%Iu %Iu", (SIZE_T)(ULONG_PTR)gBlockedWindow, (SIZE_T)(ULONG_PTR)gGrantedWindow);
    ok(SbxSpawnChild("JobUi", "target", Extra, NULL, Attributes, CREATE_NO_WINDOW, &Info), "child spawn failed %lu\n", GetLastError());
    if (Info.hProcess)
    {
        ExitCode = SbxWaitChild(&Info);
        ok(ExitCode == 0, "job-restricted child failed with 0x%lx\n", ExitCode);
    }
    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);

    ok(GlobalFindAtomW(L"SbxJobUiChildAtom") == 0, "job-private atom leaked into the global table\n");
    if (Atom) GlobalDeleteAtom(Atom);
    if (OpenClipboard(NULL)) { EmptyClipboard(); CloseClipboard(); }
    DestroyWindow(gBlockedWindow);
    DestroyWindow(gGrantedWindow);
    CloseHandle(Job);
}
