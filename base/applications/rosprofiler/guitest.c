/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     End-to-end GUI recording and analysis smoke test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rosprofiler.h"
#include "resource.h"

#include <commctrl.h>
#include <dlgs.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

#define IDC_PROCESS 1001
#define IDC_REFRESH 1002
#define IDC_INTERVAL 1003
#define IDC_DURATION 1004
#define IDC_RECORD 1005
#define IDC_STOP 1006
#define IDC_TAB 1010
#define IDC_FLAME 1011
#define IDC_FUNCTIONS 1012
#define IDC_LAUNCH 1014
#define IDC_SEARCH 1015
#define IDC_THREAD_FILTER 1016
#define IDC_TIME_START 1017
#define IDC_APPLY_FILTER 1019
#define IDC_RESET_FILTER 1020
#define IDC_TIMELINE 1021
#define IDC_SESSION_SUMMARY 1022
#define IDC_BACKEND 1023

#define IDM_FILE_OPEN 2001
#define RPERF_MAIN_CLASS L"RosProfilerMainWindow"
#define RPERF_TASKMGR_CLASS L"TaskManager11Frame"
#define RPERF_GUI_TEST_TIMEOUT 10000
#define RPERF_SMP_TEST_DURATION 60000

typedef struct _RPERF_WINDOW_SEARCH
{
    DWORD ProcessId;
    PCWSTR ClassName;
    INT RequiredControl;
    HWND Window;
} RPERF_WINDOW_SEARCH;

typedef struct _RPERF_GUI_TEST
{
    PROCESS_INFORMATION GuiProcess;
    HWND MainWindow;
    HANDLE ReadyEvent;
    HANDLE StopEvent;
    HANDLE DoneEvent;
    WCHAR LogPath[MAX_PATH];
    WCHAR ReloadPath[MAX_PATH];
} RPERF_GUI_TEST;

static VOID
RperfDiagnosticMarker(PCSTR Format,
                      ...)
{
    CHAR Buffer[512];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, ARRAYSIZE(Buffer), Format, Arguments);
    va_end(Arguments);
    Buffer[ARRAYSIZE(Buffer) - 1] = ANSI_NULL;

    printf("%s\n", Buffer);
    fflush(stdout);
    OutputDebugStringA(Buffer);
    OutputDebugStringA("\n");
}

static BOOL CALLBACK
RperfFindWindowCallback(HWND Window,
                        LPARAM Parameter)
{
    RPERF_WINDOW_SEARCH *Search = (RPERF_WINDOW_SEARCH *)Parameter;
    WCHAR ClassName[64];
    DWORD ProcessId;

    GetWindowThreadProcessId(Window, &ProcessId);
    if (ProcessId != Search->ProcessId)
        return TRUE;
    if (Search->ClassName != NULL)
    {
        if (!GetClassNameW(Window, ClassName, ARRAYSIZE(ClassName)) ||
            lstrcmpW(ClassName, Search->ClassName) != 0)
        {
            return TRUE;
        }
    }
    if (Search->RequiredControl != 0 &&
        GetDlgItem(Window, Search->RequiredControl) == NULL)
    {
        return TRUE;
    }

    Search->Window = Window;
    return FALSE;
}

static HWND
RperfFindProcessWindow(DWORD ProcessId,
                       PCWSTR ClassName,
                       INT RequiredControl)
{
    RPERF_WINDOW_SEARCH Search;

    ZeroMemory(&Search, sizeof(Search));
    Search.ProcessId = ProcessId;
    Search.ClassName = ClassName;
    Search.RequiredControl = RequiredControl;
    EnumWindows(RperfFindWindowCallback, (LPARAM)&Search);
    return Search.Window;
}

static HWND
RperfWaitForProcessWindow(DWORD ProcessId,
                          PCWSTR ClassName,
                          INT RequiredControl,
                          DWORD Timeout)
{
    DWORD Start = GetTickCount();
    HWND Window;

    do
    {
        Window = RperfFindProcessWindow(ProcessId,
                                        ClassName,
                                        RequiredControl);
        if (Window != NULL)
            return Window;
        Sleep(25);
    } while (GetTickCount() - Start < Timeout);
    return NULL;
}

static BOOL
RperfWaitForWindowEnabled(HWND Window,
                          BOOL Enabled,
                          DWORD Timeout)
{
    DWORD Start = GetTickCount();

    do
    {
        if (!!IsWindowEnabled(Window) == !!Enabled)
            return TRUE;
        Sleep(25);
    } while (GetTickCount() - Start < Timeout);
    return FALSE;
}

static LRESULT
RperfFindComboItemData(HWND Combo,
                       LPARAM ItemData)
{
    LRESULT Count;
    LRESULT Index;

    Count = SendMessageW(Combo, CB_GETCOUNT, 0, 0);
    for (Index = 0; Index < Count; ++Index)
    {
        if (SendMessageW(Combo, CB_GETITEMDATA, Index, 0) == ItemData)
            return Index;
    }
    return CB_ERR;
}

static BOOL
RperfPingWindow(HWND Window,
                DWORD Timeout)
{
    DWORD_PTR Result;

    if (Window == NULL || !IsWindow(Window))
        return FALSE;
    return SendMessageTimeoutW(Window,
                               WM_NULL,
                               0,
                               0,
                               SMTO_ABORTIFHUNG | SMTO_BLOCK,
                               Timeout,
                               &Result) != 0;
}

static BOOL
RperfWaitForListRows(HWND List,
                     LRESULT *RowCount,
                     DWORD Timeout)
{
    DWORD Start = GetTickCount();
    LRESULT Count;

    do
    {
        Count = SendMessageW(List, LVM_GETITEMCOUNT, 0, 0);
        if (Count > 0)
        {
            if (RowCount != NULL)
                *RowCount = Count;
            return TRUE;
        }
        Sleep(25);
    } while (GetTickCount() - Start < Timeout);
    return FALSE;
}

static BOOL
RperfWaitForFile(PCWSTR Path,
                 DWORD Timeout)
{
    DWORD Start = GetTickCount();

    do
    {
        if (GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES)
            return TRUE;
        Sleep(25);
    } while (GetTickCount() - Start < Timeout);
    return FALSE;
}

static BOOL
RperfSetFileDialogName(HWND Dialog,
                       PCWSTR Path)
{
    HWND Combo = GetDlgItem(Dialog, cmb13);
    HWND Edit = Combo != NULL ?
                FindWindowExW(Combo, NULL, L"Edit", NULL) : NULL;

    if (Edit != NULL)
        return SetWindowTextW(Edit, Path);
    if (Combo != NULL)
        return SetWindowTextW(Combo, Path);
    return SetDlgItemTextW(Dialog, edt1, Path);
}

static BOOL
RperfBuildSiblingPath(PCWSTR Name,
                      PWSTR Path,
                      SIZE_T PathCount)
{
    DWORD Length;
    PWSTR Separator;
    SIZE_T DirectoryLength;

    Length = GetModuleFileNameW(NULL, Path, (DWORD)PathCount);
    if (Length == 0 || Length >= PathCount)
        return FALSE;
    Separator = wcsrchr(Path, L'\\');
    if (Separator == NULL)
        return FALSE;
    DirectoryLength = (SIZE_T)(Separator + 1 - Path);
    if (DirectoryLength + wcslen(Name) >= PathCount)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    lstrcpyW(Separator + 1, Name);
    return TRUE;
}

static BOOL
RperfBuildSystemPath(PCWSTR Name,
                     PWSTR Path,
                     SIZE_T PathCount)
{
    UINT Length;
    SIZE_T NameLength;

    Length = GetSystemDirectoryW(Path, (UINT)PathCount);
    if (Length == 0 || Length >= PathCount)
        return FALSE;
    NameLength = wcslen(Name);
    if ((SIZE_T)Length + 1 + NameLength >= PathCount)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (Length != 0 && Path[Length - 1] != L'\\')
        Path[Length++] = L'\\';
    lstrcpyW(Path + Length, Name);
    return TRUE;
}

static BOOL
RperfBuildModuleDirectory(PWSTR Path,
                          SIZE_T PathCount)
{
    DWORD Length;
    PWSTR Separator;

    Length = GetModuleFileNameW(NULL, Path, (DWORD)PathCount);
    if (Length == 0 || Length >= PathCount)
        return FALSE;
    Separator = wcsrchr(Path, L'\\');
    if (Separator == NULL)
        return FALSE;
    *Separator = UNICODE_NULL;
    return TRUE;
}

static BOOL
RperfVerifyApplicationIcon(PCWSTR Path)
{
    HMODULE Image;
    HICON Icon;
    DWORD Error = ERROR_SUCCESS;

    Image = LoadLibraryExW(Path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (Image == NULL)
        return FALSE;
    Icon = (HICON)LoadImageW(Image,
                             MAKEINTRESOURCEW(IDI_ROSPROFILER),
                             IMAGE_ICON,
                             32,
                             32,
                             LR_DEFAULTCOLOR);
    if (Icon == NULL)
        Error = GetLastError();
    else
        DestroyIcon(Icon);
    FreeLibrary(Image);
    if (Error != ERROR_SUCCESS)
    {
        SetLastError(Error);
        return FALSE;
    }
    return TRUE;
}

static BOOL
RperfReadSummarySamples(HWND Summary,
                        SIZE_T *Filtered,
                        SIZE_T *Total)
{
    WCHAR Text[4096];
    PCWSTR Samples;
    WCHAR *End;

    if (SendMessageW(Summary,
                     WM_GETTEXT,
                     ARRAYSIZE(Text),
                     (LPARAM)Text) <= 0)
    {
        return FALSE;
    }
    Samples = wcsstr(Text, L"Samples in current view: ");
    if (Samples == NULL)
        return FALSE;
    Samples += wcslen(L"Samples in current view: ");
    *Filtered = (SIZE_T)_wcstoui64(Samples, &End, 10);
    if (End == Samples || wcsncmp(End, L" of ", 4) != 0)
        return FALSE;
    Samples = End + 4;
    *Total = (SIZE_T)_wcstoui64(Samples, &End, 10);
    return End != Samples;
}

static BOOL
RperfReadSummaryCounter(HWND Summary,
                        PCWSTR Label,
                        ULONGLONG *Value)
{
    WCHAR Text[4096];
    PCWSTR Number;
    WCHAR *End;

    if (SendMessageW(Summary,
                     WM_GETTEXT,
                     ARRAYSIZE(Text),
                     (LPARAM)Text) <= 0)
    {
        return FALSE;
    }
    Number = wcsstr(Text, Label);
    if (Number == NULL)
        return FALSE;
    Number += wcslen(Label);
    *Value = _wcstoui64(Number, &End, 10);
    return End != Number;
}

static BOOL
RperfWaitForSummaryPath(HWND Summary,
                        PCWSTR Path,
                        DWORD Timeout)
{
    WCHAR Text[4096];
    DWORD Start = GetTickCount();

    do
    {
        if (SendMessageW(Summary,
                         WM_GETTEXT,
                         ARRAYSIZE(Text),
                         (LPARAM)Text) > 0 &&
            wcsstr(Text, Path) != NULL)
        {
            return TRUE;
        }
        Sleep(25);
    } while (GetTickCount() - Start < Timeout);
    return FALSE;
}

static BOOL
RperfVerifyFooter(PCWSTR Path)
{
    FILE *File;
    CHAR Line[4096];
    CHAR LastLine[4096] = "";

    File = _wfopen(Path, L"rt");
    if (File == NULL)
        return FALSE;
    while (fgets(Line, sizeof(Line), File) != NULL)
        lstrcpynA(LastLine, Line, ARRAYSIZE(LastLine));
    fclose(File);
    return LastLine[0] == 'e' && LastLine[1] == '\t';
}

static BOOL
RperfClickTab(HWND Tab,
              INT Index)
{
    static const INT Centers[] = { 40, 125, 195, 250 };

    if (Index < 0 || Index >= (INT)ARRAYSIZE(Centers))
        return FALSE;
    SendMessageW(Tab,
                 WM_LBUTTONDOWN,
                 MK_LBUTTON,
                 MAKELPARAM(Centers[Index], 10));
    SendMessageW(Tab,
                 WM_LBUTTONUP,
                 0,
                 MAKELPARAM(Centers[Index], 10));
    Sleep(50);
    return TRUE;
}

static VOID
RperfCleanupGuiTest(RPERF_GUI_TEST *Test)
{
    HWND Dialog;

    if (Test->StopEvent != NULL)
        SetEvent(Test->StopEvent);
    if (Test->DoneEvent != NULL)
        WaitForSingleObject(Test->DoneEvent, 1000);

    if (Test->GuiProcess.dwProcessId != 0)
    {
        Dialog = RperfFindProcessWindow(Test->GuiProcess.dwProcessId,
                                        L"#32770",
                                        0);
        if (Dialog != NULL)
            PostMessageW(Dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
    }
    if (Test->MainWindow != NULL && IsWindow(Test->MainWindow))
        PostMessageW(Test->MainWindow, WM_CLOSE, 0, 0);
    if (Test->GuiProcess.hProcess != NULL &&
        WaitForSingleObject(Test->GuiProcess.hProcess, 2000) != WAIT_OBJECT_0)
    {
        TerminateProcess(Test->GuiProcess.hProcess, 1);
        WaitForSingleObject(Test->GuiProcess.hProcess, 2000);
    }

    if (Test->GuiProcess.hThread != NULL)
        CloseHandle(Test->GuiProcess.hThread);
    if (Test->GuiProcess.hProcess != NULL)
        CloseHandle(Test->GuiProcess.hProcess);
    if (Test->ReadyEvent != NULL)
        CloseHandle(Test->ReadyEvent);
    if (Test->StopEvent != NULL)
        CloseHandle(Test->StopEvent);
    if (Test->DoneEvent != NULL)
        CloseHandle(Test->DoneEvent);
    if (Test->LogPath[0] != UNICODE_NULL)
        DeleteFileW(Test->LogPath);
    if (Test->ReloadPath[0] != UNICODE_NULL)
        DeleteFileW(Test->ReloadPath);
}

static LRESULT
RperfWaitForProcessComboItem(HWND MainWindow,
                             DWORD ProcessId,
                             DWORD Timeout)
{
    HWND Combo = GetDlgItem(MainWindow, IDC_PROCESS);
    DWORD Start = GetTickCount();
    LRESULT Index;

    if (Combo == NULL)
        return CB_ERR;
    PostMessageW(MainWindow,
                 WM_COMMAND,
                 MAKEWPARAM(IDC_REFRESH, BN_CLICKED),
                 (LPARAM)GetDlgItem(MainWindow, IDC_REFRESH));
    do
    {
        Index = RperfFindComboItemData(Combo, (LPARAM)ProcessId);
        if (Index != CB_ERR)
            return Index;
        Sleep(25);
    } while (GetTickCount() - Start < Timeout);
    return CB_ERR;
}

static VOID
RperfCloseTestProcess(PROCESS_INFORMATION *Process,
                      HWND Window)
{
    if (Window != NULL && IsWindow(Window))
        PostMessageW(Window, WM_CLOSE, 0, 0);
    if (Process->hProcess != NULL &&
        WaitForSingleObject(Process->hProcess, 3000) != WAIT_OBJECT_0)
    {
        TerminateProcess(Process->hProcess, 1);
        WaitForSingleObject(Process->hProcess, 2000);
    }
    if (Process->hThread != NULL)
        CloseHandle(Process->hThread);
    if (Process->hProcess != NULL)
        CloseHandle(Process->hProcess);
    ZeroMemory(Process, sizeof(*Process));
}

static INT
RperfRunSmpTaskmgrAttachTest(VOID)
{
    PROCESS_INFORMATION TaskProcess;
    PROCESS_INFORMATION ProfilerProcess;
    STARTUPINFOW Startup;
    SYSTEM_INFO SystemInfo;
    WCHAR TaskPath[MAX_PATH];
    WCHAR ProfilerPath[MAX_PATH];
    WCHAR WorkDirectory[MAX_PATH];
    WCHAR CommandLine[MAX_PATH * 2];
    WCHAR TemporaryDirectory[MAX_PATH];
    WCHAR LogPath[MAX_PATH];
    WCHAR BackendText[288];
    DWORD TemporaryLength;
    DWORD Start;
    DWORD LastReport = MAXDWORD;
    DWORD Elapsed;
    DWORD TaskFailures = 0;
    DWORD ProfilerFailures = 0;
    LRESULT ProcessIndex;
    LRESULT BackendIndex;
    HWND TaskWindow = NULL;
    HWND ProfilerWindow = NULL;
    HWND Dialog;
    HWND StopButton;
    HWND ProcessCombo;
    HWND BackendCombo;
    BOOL PreserveState = FALSE;
    INT Result = 1;

    ZeroMemory(&TaskProcess, sizeof(TaskProcess));
    ZeroMemory(&ProfilerProcess, sizeof(ProfilerProcess));
    ZeroMemory(&Startup, sizeof(Startup));
    ZeroMemory(&SystemInfo, sizeof(SystemInfo));
    ZeroMemory(LogPath, sizeof(LogPath));
    Startup.cb = sizeof(Startup);

#define RPERF_SMP_CHECK(Expression, Message) \
    do \
    { \
        if (!(Expression)) \
        { \
            RperfDiagnosticMarker("ROSPROF_SMPTEST FAIL stage=%s line=%lu error=%lu", \
                                  (Message), \
                                  (ULONG)__LINE__, \
                                  GetLastError()); \
            goto Cleanup; \
        } \
    } while (0)

    GetSystemInfo(&SystemInfo);
    RperfDiagnosticMarker("ROSPROF_SMPTEST START cpus=%lu",
                          SystemInfo.dwNumberOfProcessors);
    RPERF_SMP_CHECK(RperfBuildSystemPath(L"taskmgr11.exe",
                                         TaskPath,
                                         ARRAYSIZE(TaskPath)),
                    "taskmgr-path");
    RPERF_SMP_CHECK(RperfBuildSystemPath(L"rosprofiler.exe",
                                         ProfilerPath,
                                         ARRAYSIZE(ProfilerPath)),
                    "profiler-path");
    RPERF_SMP_CHECK(GetSystemDirectoryW(WorkDirectory,
                                        ARRAYSIZE(WorkDirectory)) != 0,
                    "system-directory");
    RPERF_SMP_CHECK(GetFileAttributesW(TaskPath) != INVALID_FILE_ATTRIBUTES,
                    "taskmgr-missing");
    RPERF_SMP_CHECK(GetFileAttributesW(ProfilerPath) != INVALID_FILE_ATTRIBUTES,
                    "profiler-missing");

    TemporaryLength = GetTempPathW(ARRAYSIZE(TemporaryDirectory),
                                   TemporaryDirectory);
    RPERF_SMP_CHECK(TemporaryLength != 0 &&
                        TemporaryLength < ARRAYSIZE(TemporaryDirectory),
                    "temporary-directory");
    _snwprintf(LogPath,
               ARRAYSIZE(LogPath),
               L"%srosprofiler-smp-taskmgr-%lu.rperf",
               TemporaryDirectory,
               GetCurrentProcessId());
    LogPath[ARRAYSIZE(LogPath) - 1] = UNICODE_NULL;
    DeleteFileW(LogPath);

    _snwprintf(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\"", TaskPath);
    CommandLine[ARRAYSIZE(CommandLine) - 1] = UNICODE_NULL;
    RPERF_SMP_CHECK(CreateProcessW(TaskPath,
                                   CommandLine,
                                   NULL,
                                   NULL,
                                   FALSE,
                                   0,
                                   NULL,
                                   WorkDirectory,
                                   &Startup,
                                   &TaskProcess),
                    "taskmgr-launch");
    TaskWindow = RperfWaitForProcessWindow(TaskProcess.dwProcessId,
                                           RPERF_TASKMGR_CLASS,
                                           0,
                                           RPERF_GUI_TEST_TIMEOUT);
    RPERF_SMP_CHECK(TaskWindow != NULL, "taskmgr-window");
    RperfDiagnosticMarker("ROSPROF_SMPTEST TASKMGR_READY pid=%lu hwnd=%p",
                          TaskProcess.dwProcessId,
                          TaskWindow);

    _snwprintf(CommandLine,
               ARRAYSIZE(CommandLine),
               L"\"%s\"",
               ProfilerPath);
    CommandLine[ARRAYSIZE(CommandLine) - 1] = UNICODE_NULL;
    RPERF_SMP_CHECK(CreateProcessW(ProfilerPath,
                                   CommandLine,
                                   NULL,
                                   NULL,
                                   FALSE,
                                   0,
                                   NULL,
                                   WorkDirectory,
                                   &Startup,
                                   &ProfilerProcess),
                    "profiler-launch");
    ProfilerWindow = RperfWaitForProcessWindow(ProfilerProcess.dwProcessId,
                                               RPERF_MAIN_CLASS,
                                               IDC_DURATION,
                                               RPERF_GUI_TEST_TIMEOUT);
    RPERF_SMP_CHECK(ProfilerWindow != NULL, "profiler-window");
    RperfDiagnosticMarker("ROSPROF_SMPTEST PROFILER_READY pid=%lu hwnd=%p",
                          ProfilerProcess.dwProcessId,
                          ProfilerWindow);

    ProcessCombo = GetDlgItem(ProfilerWindow, IDC_PROCESS);
    BackendCombo = GetDlgItem(ProfilerWindow, IDC_BACKEND);
    StopButton = GetDlgItem(ProfilerWindow, IDC_STOP);
    RPERF_SMP_CHECK(ProcessCombo != NULL && BackendCombo != NULL &&
                        StopButton != NULL,
                    "profiler-controls");
    ProcessIndex = RperfWaitForProcessComboItem(ProfilerWindow,
                                                TaskProcess.dwProcessId,
                                                RPERF_GUI_TEST_TIMEOUT);
    RPERF_SMP_CHECK(ProcessIndex != CB_ERR, "taskmgr-process-selection");
    RPERF_SMP_CHECK(SendMessageW(ProcessCombo,
                                 CB_SETCURSEL,
                                 ProcessIndex,
                                 0) != CB_ERR,
                    "taskmgr-process-select");
    BackendIndex = RperfFindComboItemData(BackendCombo, RperfBackendKernel);
    RPERF_SMP_CHECK(BackendIndex != CB_ERR, "kernel-backend-selection");
    RPERF_SMP_CHECK(SendMessageW(BackendCombo,
                                 CB_GETLBTEXT,
                                 BackendIndex,
                                 (LPARAM)BackendText) != CB_ERR,
                    "kernel-backend-name");
    BackendText[ARRAYSIZE(BackendText) - 1] = UNICODE_NULL;
    RPERF_SMP_CHECK(wcsstr(BackendText, L"(unavailable)") == NULL,
                    "kernel-backend-unavailable");
    RPERF_SMP_CHECK(SendMessageW(BackendCombo,
                                 CB_SETCURSEL,
                                 BackendIndex,
                                 0) != CB_ERR,
                    "kernel-backend-select");
    SetDlgItemTextW(ProfilerWindow, IDC_INTERVAL, L"20");
    SetDlgItemTextW(ProfilerWindow, IDC_DURATION, L"0");
    RperfDiagnosticMarker("ROSPROF_SMPTEST PRE_ATTACH target=%lu interval_ms=20 backend=%lu",
                          TaskProcess.dwProcessId,
                          (ULONG)RperfBackendKernel);
    PostMessageW(ProfilerWindow,
                 WM_COMMAND,
                 MAKEWPARAM(IDC_RECORD, BN_CLICKED),
                 (LPARAM)GetDlgItem(ProfilerWindow, IDC_RECORD));

    Dialog = RperfWaitForProcessWindow(ProfilerProcess.dwProcessId,
                                       L"#32770",
                                       cmb13,
                                       RPERF_GUI_TEST_TIMEOUT);
    if (Dialog == NULL)
    {
        Dialog = RperfWaitForProcessWindow(ProfilerProcess.dwProcessId,
                                           L"#32770",
                                           edt1,
                                           1000);
    }
    RPERF_SMP_CHECK(Dialog != NULL, "save-dialog");
    RPERF_SMP_CHECK(RperfSetFileDialogName(Dialog, LogPath),
                    "save-path");
    PostMessageW(Dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    RPERF_SMP_CHECK(RperfWaitForWindowEnabled(StopButton,
                                              TRUE,
                                              RPERF_GUI_TEST_TIMEOUT),
                    "recording-start");
    RPERF_SMP_CHECK(RperfWaitForFile(LogPath, RPERF_GUI_TEST_TIMEOUT),
                    "streaming-log");
    RperfDiagnosticMarker("ROSPROF_SMPTEST RECORDING log=%ls", LogPath);

    Start = GetTickCount();
    do
    {
        BOOL TaskResponsive;
        BOOL ProfilerResponsive;
        DWORD Report;

        RPERF_SMP_CHECK(WaitForSingleObject(TaskProcess.hProcess, 0) ==
                            WAIT_TIMEOUT,
                        "taskmgr-exited");
        RPERF_SMP_CHECK(WaitForSingleObject(ProfilerProcess.hProcess, 0) ==
                            WAIT_TIMEOUT,
                        "profiler-exited");
        TaskResponsive = RperfPingWindow(TaskWindow, 500);
        ProfilerResponsive = RperfPingWindow(ProfilerWindow, 500);
        TaskFailures = TaskResponsive ? 0 : TaskFailures + 1;
        ProfilerFailures = ProfilerResponsive ? 0 : ProfilerFailures + 1;
        Elapsed = GetTickCount() - Start;
        Report = Elapsed / 5000;
        if (Report != LastReport)
        {
            RperfDiagnosticMarker("ROSPROF_SMPTEST ALIVE elapsed_ms=%lu task=%u profiler=%u",
                                  Elapsed,
                                  TaskResponsive,
                                  ProfilerResponsive);
            LastReport = Report;
        }
        if (TaskFailures >= 3 || ProfilerFailures >= 3)
        {
            RperfDiagnosticMarker("ROSPROF_SMPTEST HANG elapsed_ms=%lu task_failures=%lu profiler_failures=%lu",
                                  Elapsed,
                                  TaskFailures,
                                  ProfilerFailures);
            PreserveState = TRUE;
            Result = 2;
            goto Cleanup;
        }
        Sleep(250);
    } while (GetTickCount() - Start < RPERF_SMP_TEST_DURATION);

    PostMessageW(ProfilerWindow,
                 WM_COMMAND,
                 MAKEWPARAM(IDC_STOP, BN_CLICKED),
                 (LPARAM)StopButton);
    RPERF_SMP_CHECK(RperfWaitForWindowEnabled(StopButton,
                                              FALSE,
                                              RPERF_GUI_TEST_TIMEOUT),
                    "recording-stop");
    RperfDiagnosticMarker("ROSPROF_SMPTEST PASS elapsed_ms=%lu",
                          GetTickCount() - Start);
    Result = 0;

Cleanup:
    if (!PreserveState)
    {
        RperfCloseTestProcess(&ProfilerProcess, ProfilerWindow);
        RperfCloseTestProcess(&TaskProcess, TaskWindow);
        if (LogPath[0] != UNICODE_NULL)
            DeleteFileW(LogPath);
    }
    else
    {
        if (ProfilerProcess.hThread != NULL)
            CloseHandle(ProfilerProcess.hThread);
        if (ProfilerProcess.hProcess != NULL)
            CloseHandle(ProfilerProcess.hProcess);
        if (TaskProcess.hThread != NULL)
            CloseHandle(TaskProcess.hThread);
        if (TaskProcess.hProcess != NULL)
            CloseHandle(TaskProcess.hProcess);
    }
#undef RPERF_SMP_CHECK
    return Result;
}

#define RPERF_GUI_CHECK(Expression, Message) \
    do \
    { \
        if (!(Expression)) \
        { \
            printf("[FAIL] GUI end-to-end: %s (line %lu, error %lu)\n", \
                   (Message), \
                   (ULONG)__LINE__, \
                   GetLastError()); \
            goto Cleanup; \
        } \
    } while (0)

int
wmain(int argc,
      WCHAR **argv)
{
    RPERF_GUI_TEST Test;
    STARTUPINFOW Startup;
    WCHAR ProfilerPath[MAX_PATH];
    WCHAR WorkloadPath[MAX_PATH];
    WCHAR WorkDirectory[MAX_PATH];
    WCHAR CommandLine[MAX_PATH * 2];
    WCHAR Arguments[512];
    WCHAR ReadyName[96], StopName[96], DoneName[96];
    WCHAR TemporaryDirectory[MAX_PATH];
    WCHAR TimeText[64];
    DWORD Unique = GetTickCount();
    DWORD TemporaryLength;
    HWND Dialog, StopButton, Functions, Threads, Summary, Timeline, Tab;
    HWND Pages[4];
    RECT TimelineRect;
    LRESULT LiveRows, ReloadedRows;
    SIZE_T FilteredSamples, TotalSamples;
    ULONGLONG AttemptedSamples, AcceptedSamples, FailedSamples;
    INT GraphLeft, GraphWidth;
    BOOL Result = FALSE;

    WCHAR ModulePath[MAX_PATH];
    PCWSTR ModuleName;

    ModulePath[0] = UNICODE_NULL;
    GetModuleFileNameW(NULL, ModulePath, ARRAYSIZE(ModulePath));
    ModulePath[ARRAYSIZE(ModulePath) - 1] = UNICODE_NULL;
    ModuleName = wcsrchr(ModulePath, L'\\');
    ModuleName = ModuleName != NULL ? ModuleName + 1 : ModulePath;
    if ((argc == 2 &&
         lstrcmpiW(argv[1], L"--smp-taskmgr-attach") == 0) ||
        lstrcmpiW(ModuleName, L"rosprofiler_smp_attach.exe") == 0)
    {
        return RperfRunSmpTaskmgrAttachTest();
    }
    ZeroMemory(&Test, sizeof(Test));
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);

    RPERF_GUI_CHECK(RperfBuildSiblingPath(L"rosprofiler.exe",
                                          ProfilerPath,
                                          ARRAYSIZE(ProfilerPath)),
                    "cannot locate rosprofiler.exe");
    RPERF_GUI_CHECK(RperfBuildSiblingPath(L"rosprofiler_selftest.exe",
                                          WorkloadPath,
                                          ARRAYSIZE(WorkloadPath)),
                    "cannot locate workload executable");
    RPERF_GUI_CHECK(RperfBuildModuleDirectory(WorkDirectory,
                                              ARRAYSIZE(WorkDirectory)),
                    "cannot determine test directory");
    RPERF_GUI_CHECK(GetFileAttributesW(ProfilerPath) !=
                        INVALID_FILE_ATTRIBUTES &&
                    GetFileAttributesW(WorkloadPath) !=
                        INVALID_FILE_ATTRIBUTES,
                    "required sibling executable is missing");
    RPERF_GUI_CHECK(RperfVerifyApplicationIcon(ProfilerPath),
                    "application icon resource is missing");

    _snwprintf(ReadyName,
               ARRAYSIZE(ReadyName),
               L"RosProfilerGuiReady-%lu-%lu",
               GetCurrentProcessId(),
               Unique);
    _snwprintf(StopName,
               ARRAYSIZE(StopName),
               L"RosProfilerGuiStop-%lu-%lu",
               GetCurrentProcessId(),
               Unique);
    _snwprintf(DoneName,
               ARRAYSIZE(DoneName),
               L"RosProfilerGuiDone-%lu-%lu",
               GetCurrentProcessId(),
               Unique);
    ReadyName[ARRAYSIZE(ReadyName) - 1] = UNICODE_NULL;
    StopName[ARRAYSIZE(StopName) - 1] = UNICODE_NULL;
    DoneName[ARRAYSIZE(DoneName) - 1] = UNICODE_NULL;
    Test.ReadyEvent = CreateEventW(NULL, TRUE, FALSE, ReadyName);
    Test.StopEvent = CreateEventW(NULL, TRUE, FALSE, StopName);
    Test.DoneEvent = CreateEventW(NULL, TRUE, FALSE, DoneName);
    RPERF_GUI_CHECK(Test.ReadyEvent != NULL && Test.StopEvent != NULL &&
                        Test.DoneEvent != NULL,
                    "cannot create workload synchronization events");

    TemporaryLength = GetTempPathW(ARRAYSIZE(TemporaryDirectory),
                                   TemporaryDirectory);
    RPERF_GUI_CHECK(TemporaryLength != 0 &&
                        TemporaryLength < ARRAYSIZE(TemporaryDirectory),
                    "cannot obtain the temporary directory");
    _snwprintf(Test.LogPath,
               ARRAYSIZE(Test.LogPath),
               L"%srosprofiler-guitest-%lu-%lu.rperf",
               TemporaryDirectory,
               GetCurrentProcessId(),
               Unique);
    Test.LogPath[ARRAYSIZE(Test.LogPath) - 1] = UNICODE_NULL;
    _snwprintf(Test.ReloadPath,
               ARRAYSIZE(Test.ReloadPath),
               L"%srosprofiler-guitest-reopen-%lu-%lu.rperf",
               TemporaryDirectory,
               GetCurrentProcessId(),
               Unique);
    Test.ReloadPath[ARRAYSIZE(Test.ReloadPath) - 1] = UNICODE_NULL;
    DeleteFileW(Test.LogPath);
    DeleteFileW(Test.ReloadPath);

    _snwprintf(CommandLine,
               ARRAYSIZE(CommandLine),
               L"\"%s\"",
               ProfilerPath);
    CommandLine[ARRAYSIZE(CommandLine) - 1] = UNICODE_NULL;
    RPERF_GUI_CHECK(CreateProcessW(ProfilerPath,
                                   CommandLine,
                                   NULL,
                                   NULL,
                                   FALSE,
                                   0,
                                   NULL,
                                   WorkDirectory,
                                   &Startup,
                                   &Test.GuiProcess),
                    "cannot launch the profiler GUI");

    Test.MainWindow = RperfWaitForProcessWindow(Test.GuiProcess.dwProcessId,
                                                RPERF_MAIN_CLASS,
                                                IDC_DURATION,
                                                RPERF_GUI_TEST_TIMEOUT);
    RPERF_GUI_CHECK(Test.MainWindow != NULL, "main window did not appear");
    SetDlgItemTextW(Test.MainWindow, IDC_DURATION, L"0");
    PostMessageW(Test.MainWindow,
                 WM_COMMAND,
                 MAKEWPARAM(IDC_LAUNCH, BN_CLICKED),
                 (LPARAM)GetDlgItem(Test.MainWindow, IDC_LAUNCH));

    Dialog = RperfWaitForProcessWindow(Test.GuiProcess.dwProcessId,
                                       L"#32770",
                                       IDC_LAUNCH_PATH,
                                       RPERF_GUI_TEST_TIMEOUT);
    RPERF_GUI_CHECK(Dialog != NULL, "launch dialog did not appear");
    SetDlgItemTextW(Dialog, IDC_LAUNCH_PATH, WorkloadPath);
    _snwprintf(Arguments,
               ARRAYSIZE(Arguments),
               L"--named-workload %s %s %s",
               ReadyName,
               StopName,
               DoneName);
    Arguments[ARRAYSIZE(Arguments) - 1] = UNICODE_NULL;
    SetDlgItemTextW(Dialog, IDC_LAUNCH_ARGUMENTS, Arguments);
    SetDlgItemTextW(Dialog, IDC_LAUNCH_DIRECTORY, WorkDirectory);
    PostMessageW(Dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);

    Dialog = RperfWaitForProcessWindow(Test.GuiProcess.dwProcessId,
                                       L"#32770",
                                       cmb13,
                                       RPERF_GUI_TEST_TIMEOUT);
    if (Dialog == NULL)
    {
        Dialog = RperfWaitForProcessWindow(Test.GuiProcess.dwProcessId,
                                           L"#32770",
                                           edt1,
                                           1000);
    }
    RPERF_GUI_CHECK(Dialog != NULL, "save dialog did not appear");
    RPERF_GUI_CHECK(RperfSetFileDialogName(Dialog, Test.LogPath),
                    "cannot set the save path");
    PostMessageW(Dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);

    StopButton = GetDlgItem(Test.MainWindow, IDC_STOP);
    RPERF_GUI_CHECK(WaitForSingleObject(Test.ReadyEvent,
                                        RPERF_GUI_TEST_TIMEOUT) == WAIT_OBJECT_0,
                    "launched workload did not become ready");
    RPERF_GUI_CHECK(StopButton != NULL &&
                        RperfWaitForWindowEnabled(StopButton,
                                                  TRUE,
                                                  RPERF_GUI_TEST_TIMEOUT),
                    "recording did not start");
    RPERF_GUI_CHECK(!IsWindowEnabled(GetDlgItem(Test.MainWindow, IDC_TAB)) &&
                        !IsWindowEnabled(GetDlgItem(Test.MainWindow,
                                                    IDC_FLAME)) &&
                        !IsWindowEnabled(GetDlgItem(Test.MainWindow,
                                                    IDC_FUNCTIONS)) &&
                        !IsWindowEnabled(GetDlgItem(Test.MainWindow,
                                                    IDC_TIMELINE)) &&
                        !IsWindowEnabled(GetDlgItem(Test.MainWindow,
                                                    IDC_SEARCH)),
                    "analysis controls remained interactive while recording");
    RPERF_GUI_CHECK(RperfWaitForFile(Test.LogPath,
                                     RPERF_GUI_TEST_TIMEOUT),
                    "streaming log was not created");
    Sleep(500);
    PostMessageW(Test.MainWindow,
                 WM_COMMAND,
                 MAKEWPARAM(IDC_STOP, BN_CLICKED),
                 (LPARAM)StopButton);
    RPERF_GUI_CHECK(RperfWaitForWindowEnabled(
                        GetDlgItem(Test.MainWindow, IDC_TAB),
                        TRUE,
                        RPERF_GUI_TEST_TIMEOUT),
                    "recording and offline analysis did not finish");
    RPERF_GUI_CHECK(!IsWindowEnabled(StopButton),
                    "stop remained enabled after offline analysis");
    RPERF_GUI_CHECK(IsWindowEnabled(GetDlgItem(Test.MainWindow, IDC_TAB)) &&
                        IsWindowEnabled(GetDlgItem(Test.MainWindow,
                                                  IDC_FLAME)) &&
                        IsWindowEnabled(GetDlgItem(Test.MainWindow,
                                                  IDC_FUNCTIONS)) &&
                        IsWindowEnabled(GetDlgItem(Test.MainWindow,
                                                  IDC_TIMELINE)) &&
                        IsWindowEnabled(GetDlgItem(Test.MainWindow,
                                                  IDC_SEARCH)),
                    "analysis controls were not restored after recording");
    Sleep(300);
    RPERF_GUI_CHECK(RperfVerifyFooter(Test.LogPath),
                    "recording has no completion footer");

    Functions = GetDlgItem(Test.MainWindow, IDC_FUNCTIONS);
    Threads = GetDlgItem(Test.MainWindow, IDC_THREAD_FILTER);
    Summary = GetDlgItem(Test.MainWindow, IDC_SESSION_SUMMARY);
    Timeline = GetDlgItem(Test.MainWindow, IDC_TIMELINE);
    Tab = GetDlgItem(Test.MainWindow, IDC_TAB);
    Pages[0] = GetDlgItem(Test.MainWindow, IDC_FLAME);
    Pages[1] = Functions;
    Pages[2] = Timeline;
    Pages[3] = Summary;
    RPERF_GUI_CHECK(Functions != NULL && Threads != NULL && Summary != NULL &&
                        Timeline != NULL && Tab != NULL && Pages[0] != NULL,
                    "analysis controls are missing");
    RPERF_GUI_CHECK(RperfWaitForListRows(Functions,
                                         &LiveRows,
                                         RPERF_GUI_TEST_TIMEOUT),
                    "hot-function analysis is empty");
    RPERF_GUI_CHECK(SendMessageW(Threads, CB_GETCOUNT, 0, 0) >= 2,
                    "thread filter was not populated");
    RPERF_GUI_CHECK(SendMessageW(Summary, WM_GETTEXTLENGTH, 0, 0) >= 200,
                    "session diagnostics were not populated");
    RPERF_GUI_CHECK(RperfReadSummarySamples(Summary,
                                            &FilteredSamples,
                                            &TotalSamples) &&
                        FilteredSamples != 0 &&
                        FilteredSamples == TotalSamples,
                    "live sample totals are inconsistent");
    RPERF_GUI_CHECK(
        RperfReadSummaryCounter(Summary,
                                L"Attempted samples: ",
                                &AttemptedSamples) &&
        RperfReadSummaryCounter(Summary,
                                L"Accepted samples: ",
                                &AcceptedSamples) &&
        RperfReadSummaryCounter(Summary,
                                L"Failed samples: ",
                                &FailedSamples) &&
        AcceptedSamples == TotalSamples &&
        AttemptedSamples == AcceptedSamples + FailedSamples,
        "capture accounting is missing or inconsistent");
    RPERF_GUI_CHECK(CopyFileW(Test.LogPath, Test.ReloadPath, FALSE),
                    "cannot create the reopen fixture");

    PostMessageW(Test.MainWindow,
                 WM_COMMAND,
                 MAKEWPARAM(IDM_FILE_OPEN, 0),
                 0);
    Dialog = RperfWaitForProcessWindow(Test.GuiProcess.dwProcessId,
                                       L"#32770",
                                       cmb13,
                                       RPERF_GUI_TEST_TIMEOUT);
    if (Dialog == NULL)
    {
        Dialog = RperfWaitForProcessWindow(Test.GuiProcess.dwProcessId,
                                           L"#32770",
                                           edt1,
                                           1000);
    }
    RPERF_GUI_CHECK(Dialog != NULL, "open dialog did not appear");
    RPERF_GUI_CHECK(RperfSetFileDialogName(Dialog, Test.ReloadPath),
                    "cannot set the reopen path");
    PostMessageW(Dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    RPERF_GUI_CHECK(RperfWaitForSummaryPath(Summary,
                                            Test.ReloadPath,
                                            RPERF_GUI_TEST_TIMEOUT),
                    "background reopen did not publish the new session");
    RPERF_GUI_CHECK(RperfWaitForWindowEnabled(Tab,
                                              TRUE,
                                              RPERF_GUI_TEST_TIMEOUT),
                    "background reopen did not complete");

    ReloadedRows = SendMessageW(Functions, LVM_GETITEMCOUNT, 0, 0);
    RPERF_GUI_CHECK(ReloadedRows == LiveRows,
                    "reopened analysis differs from live analysis");
    RPERF_GUI_CHECK(RperfReadSummarySamples(Summary,
                                            &FilteredSamples,
                                            &TotalSamples) &&
                        FilteredSamples != 0 &&
                        FilteredSamples == TotalSamples,
                    "reopened sample totals are inconsistent");

    GetClientRect(Timeline, &TimelineRect);
    GraphLeft = 88;
    GraphWidth = TimelineRect.right - GraphLeft - 12;
    RPERF_GUI_CHECK(GraphWidth > 200, "timeline has no usable graph area");
    SendMessageW(Timeline,
                 WM_LBUTTONDOWN,
                 MK_LBUTTON,
                 MAKELPARAM(GraphLeft + GraphWidth / 4, 60));
    SendMessageW(Timeline,
                 WM_MOUSEMOVE,
                 MK_LBUTTON,
                 MAKELPARAM(GraphLeft + (GraphWidth * 3) / 4, 60));
    SendMessageW(Timeline,
                 WM_LBUTTONUP,
                 0,
                 MAKELPARAM(GraphLeft + (GraphWidth * 3) / 4, 60));
    Sleep(200);
    RPERF_GUI_CHECK(RperfReadSummarySamples(Summary,
                                            &FilteredSamples,
                                            &TotalSamples) &&
                        FilteredSamples != 0 &&
                        FilteredSamples < TotalSamples,
                    "timeline range did not reduce the sample population");

    PostMessageW(Test.MainWindow,
                 WM_COMMAND,
                 MAKEWPARAM(IDC_RESET_FILTER, BN_CLICKED),
                 (LPARAM)GetDlgItem(Test.MainWindow, IDC_RESET_FILTER));
    Sleep(150);
    SendMessageW(Threads, CB_SETCURSEL, 1, 0);
    PostMessageW(Test.MainWindow,
                 WM_COMMAND,
                 MAKEWPARAM(IDC_APPLY_FILTER, BN_CLICKED),
                 (LPARAM)GetDlgItem(Test.MainWindow, IDC_APPLY_FILTER));
    Sleep(150);
    RPERF_GUI_CHECK(SendMessageW(Functions, LVM_GETITEMCOUNT, 0, 0) > 0,
                    "thread-filtered analysis is empty");

    SetDlgItemTextW(Test.MainWindow,
                    IDC_SEARCH,
                    L"symbol-that-cannot-exist-92f6");
    Sleep(100);
    RPERF_GUI_CHECK(SendMessageW(Functions, LVM_GETITEMCOUNT, 0, 0) == 0,
                    "symbol search did not filter the hot-function table");
    SetDlgItemTextW(Test.MainWindow, IDC_SEARCH, L"");
    PostMessageW(Test.MainWindow,
                 WM_COMMAND,
                 MAKEWPARAM(IDC_RESET_FILTER, BN_CLICKED),
                 (LPARAM)GetDlgItem(Test.MainWindow, IDC_RESET_FILTER));
    Sleep(200);
    RPERF_GUI_CHECK(SendMessageW(Functions, LVM_GETITEMCOUNT, 0, 0) ==
                        ReloadedRows,
                    "filter reset did not restore the full population");
    SendMessageW(GetDlgItem(Test.MainWindow, IDC_TIME_START),
                 WM_GETTEXT,
                 ARRAYSIZE(TimeText),
                 (LPARAM)TimeText);
    RPERF_GUI_CHECK(lstrcmpW(TimeText, L"0.000000") == 0,
                    "time-range reset is incorrect");

    RPERF_GUI_CHECK(IsWindowVisible(Pages[0]),
                    "flame page is not initially visible");
    RPERF_GUI_CHECK(RperfClickTab(Tab, 1) && IsWindowVisible(Pages[1]) &&
                        !IsWindowVisible(Pages[0]),
                    "hot-functions tab did not switch pages");
    RPERF_GUI_CHECK(RperfClickTab(Tab, 2) && IsWindowVisible(Pages[2]) &&
                        !IsWindowVisible(Pages[1]),
                    "timeline tab did not switch pages");
    RPERF_GUI_CHECK(RperfClickTab(Tab, 3) && IsWindowVisible(Pages[3]) &&
                        !IsWindowVisible(Pages[2]),
                    "session tab did not switch pages");
    RPERF_GUI_CHECK(RperfClickTab(Tab, 0) && IsWindowVisible(Pages[0]) &&
                        !IsWindowVisible(Pages[3]),
                    "flame tab did not restore its page");

    SetEvent(Test.StopEvent);
    RPERF_GUI_CHECK(WaitForSingleObject(Test.DoneEvent,
                                        RPERF_GUI_TEST_TIMEOUT) == WAIT_OBJECT_0,
                    "workload did not stop cleanly");
    Result = TRUE;

Cleanup:
    if (Result)
    {
        printf("[PASS] GUI icon, launch, streamed capture, manual stop, analysis, "
               "reopen, timeline, thread/search filters, reset, and tabs "
               "(%ld hot functions)\n",
               (long)ReloadedRows);
    }
    RperfCleanupGuiTest(&Test);
    return Result ? 0 : 1;
}
