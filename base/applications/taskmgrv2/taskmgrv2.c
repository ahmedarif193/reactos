/*
 * taskmgrv2.c — wWinMain entry point, single-instance mutex,
 *                top-level state, settings load/save.
 */
#include "precomp.h"

/* -----------------------------------------------------------------------
 * Globals
 * ----------------------------------------------------------------------- */
HINSTANCE g_hInst    = NULL;
HWND      g_hMainWnd = NULL;
HANDLE    g_hMutex   = NULL;

/* -----------------------------------------------------------------------
 * TmgrV2_FatalError
 * ----------------------------------------------------------------------- */
void TmgrV2_FatalError(LPCWSTR title, LPCWSTR fmt, ...)
{
    WCHAR buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 512, fmt, ap);
    va_end(ap);
    buf[511] = L'\0';
    MessageBoxW(NULL, buf, title, MB_OK | MB_ICONERROR);
}

/* -----------------------------------------------------------------------
 * TmgrV2_LoadSettings
 * Read top-level settings into shell globals before the window exists.
 * Per-module settings are loaded by each module's own init function.
 * ----------------------------------------------------------------------- */
void TmgrV2_LoadSettings(void)
{
    /* Nothing to do at this level; shell and page modules load their
     * own settings in Shell_Main / PageXxx_LoadPrefs. */
}

/* -----------------------------------------------------------------------
 * TmgrV2_SaveSettings
 * Called after Shell_Main returns.
 * ----------------------------------------------------------------------- */
void TmgrV2_SaveSettings(void)
{
    /* Shell saves WindowPlacement inside Shell_Main; page modules save
     * their own prefs.  Nothing to do here. */
}

/* -----------------------------------------------------------------------
 * TmgrV2_Run — thin wrapper; Shell_Main owns the real work.
 * ----------------------------------------------------------------------- */
int TmgrV2_Run(HINSTANCE hInst, LPWSTR lpCmdLine, int nCmdShow)
{
    return Shell_Main(hInst, lpCmdLine, nCmdShow);
}

/* -----------------------------------------------------------------------
 * wWinMain
 * ----------------------------------------------------------------------- */
int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPWSTR    lpCmdLine,
                      int       nCmdShow)
{
    HANDLE hProc;
    TOKEN_PRIVILEGES tp;
    HANDLE hToken;

    (void)hPrevInstance;

    g_hInst  = hInstance;
    g_hMutex = CreateMutexW(NULL, TRUE, L"taskmgrv2ros");
    if (g_hMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        /* Bring the existing taskmgrv2 window forward, then exit. */
        HWND hPrev = FindWindowW(L"TaskMgrV2.MainWindow", NULL);
        if (hPrev)
        {
            SendMessageW(hPrev, WM_SYSCOMMAND, SC_RESTORE, 0);
            SetForegroundWindow(hPrev);
        }
        CloseHandle(g_hMutex);
        return 0;
    }
    if (!g_hMutex)
        return 1;

    /* Match classic's behavior: high priority class */
    hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());
    if (hProc)
    {
        SetPriorityClass(hProc, HIGH_PRIORITY_CLASS);
        CloseHandle(hProc);
    }

    /* Acquire SE_DEBUG_NAME so we can open foreign-user processes */
    if (OpenProcessToken(GetCurrentProcess(),
                         TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
    {
        ZeroMemory(&tp, sizeof(tp));
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, SE_DEBUG_NAME,
                                  &tp.Privileges[0].Luid))
        {
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
        }
        CloseHandle(hToken);
    }

    TmgrV2_LoadSettings();

    if (!Theme_Init())
    {
        TmgrV2_FatalError(L"taskmgrv2", L"Theme_Init failed");
        CloseHandle(g_hMutex);
        return 2;
    }

    if (!PerfDataV2Initialize())
    {
        TmgrV2_FatalError(L"taskmgrv2", L"PerfData init failed");
        Theme_Shutdown();
        CloseHandle(g_hMutex);
        return 3;
    }

    CpuTopo_Detect();

    SetProcessShutdownParameters(1, SHUTDOWN_NORETRY);

    {
        int rc = Shell_Main(hInstance, lpCmdLine, nCmdShow);
        TmgrV2_SaveSettings();
        PerfDataV2Uninitialize();
        Theme_Shutdown();
        CloseHandle(g_hMutex);
        return rc;
    }
}
