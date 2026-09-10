/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed Arif
 *
 * Interactive, opt-in disposable-VM test. The same executable drives the
 * shell's visible menus and checks their effects on Windows and ReactOS.
 * --boot-suite deliberately restarts Explorer and reboots the guest once.
 * A host UI driver selects the requested visible menu commands and buttons.
 */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <knownfolders.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdarg.h>

#define APP_COUNT 5
#define ALL_PINS ((1u << APP_COUNT) - 1)
#define CHECKPOINT_VERSION 0x50494e31

typedef struct
{
    UINT count;
    DWORD hash;
    WCHAR path[MAX_PATH];
} PIN_SNAPSHOT;

typedef struct
{
    DWORD version, phase;
    ULONGLONG bootTime;
    WCHAR directory[MAX_PATH];
    WCHAR target[APP_COUNT][MAX_PATH], shortcut[APP_COUNT][MAX_PATH];
    PIN_SNAPSHOT pins[APP_COUNT];
    DWORD unrelatedHash, unrelatedCount;
} CHECKPOINT;

static CHECKPOINT state;
static WCHAR checkpointPath[MAX_PATH];
static HANDLE logFile = INVALID_HANDLE_VALUE, serialFile = INVALID_HANDLE_VALUE;
static unsigned failures, steps;

static void Log(const char *format, ...)
{
    char text[2048];
    DWORD written;
    va_list args;
    va_start(args, format);
    StringCchVPrintfA(text, ARRAYSIZE(text), format, args);
    va_end(args);
    printf("%s", text);
    fflush(stdout);
    if (logFile != INVALID_HANDLE_VALUE)
    {
        WriteFile(logFile, text, lstrlenA(text), &written, NULL);
        FlushFileBuffers(logFile);
    }
    if (serialFile != INVALID_HANDLE_VALUE)
        WriteFile(serialFile, text, lstrlenA(text), &written, NULL);
}

#define CHECK(condition, ...) do { if (!(condition)) { ++failures; Log("PIN_FAIL line=%u ", __LINE__); Log(__VA_ARGS__); } } while (0)

static ULONGLONG BootTime(void)
{
    FILETIME ft;
    ULARGE_INTEGER time;
    GetSystemTimeAsFileTime(&ft);
    time.LowPart = ft.dwLowDateTime;
    time.HighPart = ft.dwHighDateTime;
    return time.QuadPart - GetTickCount64() * 10000;
}

static BOOL SaveCheckpoint(void)
{
    DWORD written = 0;
    HANDLE file = CreateFileW(checkpointPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL result = file != INVALID_HANDLE_VALUE && WriteFile(file, &state, sizeof(state), &written, NULL) && written == sizeof(state) && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    CHECK(result, "Saving checkpoint failed: %lu\n", GetLastError());
    return result;
}

static HRESULT CreateLink(PCWSTR target, PCWSTR path)
{
    IShellLinkW *link = NULL;
    IPersistFile *file = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link);
    if (SUCCEEDED(hr)) hr = IShellLinkW_SetPath(link, target);
    if (SUCCEEDED(hr)) hr = IShellLinkW_SetArguments(link, L"--pin-contract \"argument with spaces\"");
    if (SUCCEEDED(hr)) hr = IShellLinkW_SetWorkingDirectory(link, state.directory);
    if (SUCCEEDED(hr)) hr = IShellLinkW_SetIconLocation(link, target, 0);
    if (SUCCEEDED(hr)) hr = IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&file);
    if (SUCCEEDED(hr)) hr = IPersistFile_Save(file, path, TRUE);
    if (file) IPersistFile_Release(file);
    if (link) IShellLinkW_Release(link);
    return hr;
}

static HRESULT LoadLinkSnapshot(PCWSTR path, IShellLinkW **link, DWORD *hash)
{
    HANDLE file;
    LARGE_INTEGER size;
    HGLOBAL memory = NULL;
    BYTE *data;
    DWORD read, i;
    IStream *stream = NULL;
    IPersistStream *persist = NULL;
    HRESULT hr;
    *link = NULL;
    *hash = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.HighPart) { CloseHandle(file); return E_FAIL; }
    memory = GlobalAlloc(GMEM_MOVEABLE, size.LowPart);
    data = memory ? GlobalLock(memory) : NULL;
    if (!data) { if (memory) GlobalFree(memory); CloseHandle(file); return E_OUTOFMEMORY; }
    hr = ReadFile(file, data, size.LowPart, &read, NULL) && read == size.LowPart ? S_OK : E_FAIL;
    CloseHandle(file);
    *hash = 2166136261u;
    for (i = 0; SUCCEEDED(hr) && i < read; ++i) *hash = (*hash ^ data[i]) * 16777619u;
    GlobalUnlock(memory);
    if (FAILED(hr)) { GlobalFree(memory); return hr; }
    hr = CreateStreamOnHGlobal(memory, TRUE, &stream);
    if (FAILED(hr)) { GlobalFree(memory); return hr; }
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)link);
    if (SUCCEEDED(hr)) hr = IShellLinkW_QueryInterface(*link, &IID_IPersistStream, (void **)&persist);
    if (SUCCEEDED(hr)) hr = IPersistStream_Load(persist, stream);
    if (persist) IPersistStream_Release(persist);
    IStream_Release(stream);
    return hr;
}

static BOOL Snapshot(PIN_SNAPSHOT *pins, DWORD *otherHash, DWORD *otherCount)
{
    PWSTR folder = NULL;
    WCHAR pattern[MAX_PATH], path[MAX_PATH], target[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    HRESULT hr;
    DWORD error;
    UINT i;
    ZeroMemory(pins, sizeof(PIN_SNAPSHOT) * APP_COUNT);
    *otherHash = *otherCount = 0;
    hr = SHGetKnownFolderPath(&FOLDERID_UserPinned, KF_FLAG_DONT_VERIFY, NULL, &folder);
    if (FAILED(hr)) return FALSE;
    hr = StringCchPrintfW(pattern, ARRAYSIZE(pattern), L"%s\\TaskBar\\*.lnk", folder);
    CoTaskMemFree(folder);
    if (FAILED(hr)) return FALSE;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    do
    {
        IShellLinkW *link = NULL;
        DWORD hash;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        StringCchCopyW(path, ARRAYSIZE(path), pattern);
        PathRemoveFileSpecW(path);
        if (!PathAppendW(path, data.cFileName)) { FindClose(find); return FALSE; }
        hr = LoadLinkSnapshot(path, &link, &hash);
        if (SUCCEEDED(hr)) hr = IShellLinkW_GetPath(link, target, ARRAYSIZE(target), NULL, SLGP_UNCPRIORITY);
        if (FAILED(hr))
        {
            if (link) IShellLinkW_Release(link);
            FindClose(find);
            return FALSE;
        }
        for (i = 0; i < APP_COUNT; ++i)
        {
            if (SUCCEEDED(hr) && !lstrcmpiW(target, state.target[i]))
            {
                ++pins[i].count;
                pins[i].hash = hash;
                StringCchCopyW(pins[i].path, ARRAYSIZE(pins[i].path), path);
                break;
            }
        }
        if (i == APP_COUNT)
        {
            ++*otherCount;
            for (i = 0; data.cFileName[i]; ++i) hash = (hash ^ data.cFileName[i]) * 16777619u;
            *otherHash ^= hash;
        }
        if (link) IShellLinkW_Release(link);
    } while (FindNextFileW(find, &data));
    error = GetLastError();
    FindClose(find);
    return error == ERROR_NO_MORE_FILES;
}

static UINT Mask(const PIN_SNAPSHOT *pins)
{
    UINT i, result = 0;
    for (i = 0; i < APP_COUNT; ++i) if (pins[i].count) result |= 1u << i;
    return result;
}

static BOOL VerifyMask(UINT expected, BOOL wait)
{
    PIN_SNAPSHOT current[APP_COUNT];
    DWORD otherHash, otherCount, start = GetTickCount();
    UINT i, actual = ~expected;
    BOOL got = FALSE;
    do
    {
        got = Snapshot(current, &otherHash, &otherCount);
        if (got) actual = Mask(current);
        if (got && actual == expected) break;
        if (!wait) break;
        Sleep(200);
    } while (GetTickCount() - start < 120000);
    CHECK(got && actual == expected, "Pin set expected=%02x actual=%02x readable=%u\n", expected, actual, got);
    if (!got || actual != expected) return FALSE;
    CHECK(otherHash == state.unrelatedHash && otherCount == state.unrelatedCount, "An unrelated pin changed\n");
    for (i = 0; i < APP_COUNT; ++i)
    {
        CHECK(current[i].count == !!(expected & (1u << i)), "App %c has %u shortcuts\n", 'A' + i, current[i].count);
        if (current[i].count && state.pins[i].count)
            CHECK(current[i].hash == state.pins[i].hash && !lstrcmpiW(current[i].path, state.pins[i].path), "Untouched app %c changed shortcut content or path\n", 'A' + i);
        CHECK(GetFileAttributesW(state.target[i]) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(state.shortcut[i]) != INVALID_FILE_ATTRIBUTES, "App %c lost its source files\n", 'A' + i);
        state.pins[i] = current[i];
    }
    Log("PIN_SET_OK step=%u mask=%02x failures=%u\n", steps, actual, failures);
    return failures == 0;
}

static void PressMenuKey(void)
{
    INPUT input[4] = {{0}};
    UINT i;
    for (i = 0; i < ARRAYSIZE(input); ++i) input[i].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_SHIFT;
    input[1].ki.wVk = VK_F10;
    input[2].ki.wVk = VK_F10;
    input[2].ki.dwFlags = KEYEVENTF_KEYUP;
    input[3].ki.wVk = VK_SHIFT;
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    CHECK(SendInput(ARRAYSIZE(input), input, sizeof(INPUT)) == ARRAYSIZE(input), "Opening context menu failed\n");
}

static BOOL PinStep(UINT app, BOOL pin)
{
    PIDLIST_ABSOLUTE pidl = NULL;
    HRESULT hr = SHParseDisplayName(state.shortcut[app], NULL, &pidl, 0, NULL);
    UINT expected = pin ? Mask(state.pins) | (1u << app) : Mask(state.pins) & ~(1u << app);
    ++steps;
    if (SUCCEEDED(hr)) hr = SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
    CoTaskMemFree(pidl);
    CHECK(SUCCEEDED(hr), "Opening shortcut in Explorer: %08lx\n", hr);
    if (FAILED(hr)) return FALSE;
    Sleep(2500);
    PressMenuKey();
    Log("PIN_UI_READY step=%u action=%s app=%c expected=%02x\n", steps, pin ? "PIN" : "UNPIN", 'A' + app, expected);
    return VerifyMask(expected, TRUE);
}

static BOOL WaitForShell(DWORD oldPid)
{
    DWORD start = GetTickCount(), pid = 0;
    HWND tray;
    do
    {
        tray = FindWindowW(L"Shell_TrayWnd", NULL);
        if (tray) GetWindowThreadProcessId(tray, &pid);
        if (tray && IsWindowVisible(tray) && pid && pid != oldPid)
        {
            Log("PIN_SHELL_READY pid=%lu previous=%lu\n", pid, oldPid);
            Sleep(2000);
            return TRUE;
        }
        Sleep(200);
    } while (GetTickCount() - start < 60000);
    CHECK(FALSE, "Explorer did not create a visible taskbar\n");
    return FALSE;
}

static BOOL RestartExplorer(void)
{
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
    DWORD pid, length = MAX_PATH, start;
    WCHAR path[MAX_PATH], command[MAX_PATH + 3];
    HANDLE process;
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi;
    si.cb = sizeof(si);
    if (!tray) return FALSE;
    GetWindowThreadProcessId(tray, &pid);
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!process) return FALSE;
    if (!QueryFullProcessImageNameW(process, 0, path, &length)) { CloseHandle(process); return FALSE; }
    Log("PIN_RESTART_EXPLORER pid=%lu path=%ls\n", pid, path);
    if (!TerminateProcess(process, 0)) { CloseHandle(process); return FALSE; }
    WaitForSingleObject(process, 10000);
    CloseHandle(process);
    start = GetTickCount();
    while (!FindWindowW(L"Shell_TrayWnd", NULL) && GetTickCount() - start < 4000) Sleep(200);
    if (!FindWindowW(L"Shell_TrayWnd", NULL))
    {
        StringCchPrintfW(command, ARRAYSIZE(command), L"\"%s\"", path);
        if (!CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return FALSE;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return WaitForShell(pid);
}

typedef struct { UINT app, count; BOOL close; } WINDOW_QUERY;

static BOOL CALLBACK VisitFixture(HWND window, LPARAM parameter)
{
    WINDOW_QUERY *query = (WINDOW_QUERY *)parameter;
    WCHAR classname[64], path[MAX_PATH];
    HANDLE process;
    DWORD pid, length = ARRAYSIZE(path);
    GetClassNameW(window, classname, ARRAYSIZE(classname));
    if (lstrcmpW(classname, L"PinParityFixture")) return TRUE;
    GetWindowThreadProcessId(window, &pid);
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process)
    {
        if (QueryFullProcessImageNameW(process, 0, path, &length) && !lstrcmpiW(path, state.target[query->app]))
        {
            ++query->count;
            if (query->close) PostMessageW(window, WM_CLOSE, 0, 0);
        }
        CloseHandle(process);
    }
    return TRUE;
}

static BOOL WaitForWindows(UINT app, UINT expected, BOOL close)
{
    DWORD start = GetTickCount();
    WINDOW_QUERY query;
    do
    {
        query.app = app;
        query.count = 0;
        query.close = close;
        EnumWindows(VisitFixture, (LPARAM)&query);
        if (query.count == expected) break;
        Sleep(200);
    } while (GetTickCount() - start < 120000);
    CHECK(query.count == expected, "App %c expected %u windows, found %u\n", 'A' + app, expected, query.count);
    return query.count == expected;
}

static BOOL LaunchStep(UINT app, UINT count)
{
    ++steps;
    Log("PIN_LAUNCH_READY step=%u app=%c windows=%u\n", steps, 'A' + app, count);
    if (!WaitForWindows(app, count, FALSE)) return FALSE;
    Log("PIN_LAUNCH_OK app=%c windows=%u\n", 'A' + app, count);
    return VerifyMask(ALL_PINS, FALSE);
}

static BOOL RebootGuest(void)
{
    HANDLE token;
    TOKEN_PRIVILEGES privileges;
    BOOL result = OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token);
    if (!result) return FALSE;
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    result = LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid);
    if (result) result = AdjustTokenPrivileges(token, FALSE, &privileges, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    if (!result) return FALSE;
    Log("PIN_REBOOT_REQUESTED mask=%02x\n", Mask(state.pins));
    return ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED);
}

static BOOL InitializeFixtures(PCWSTR module)
{
    WCHAR temp[MAX_PATH], desktop[MAX_PATH];
    UINT i;
    HRESULT hr;
    state.version = CHECKPOINT_VERSION;
    if (!GetTempPathW(ARRAYSIZE(temp), temp) || !GetTempFileNameW(temp, L"pin", 0, state.directory)) return FALSE;
    DeleteFileW(state.directory);
    if (!CreateDirectoryW(state.directory, NULL)) return FALSE;
    hr = SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desktop);
    if (FAILED(hr)) return FALSE;
    for (i = 0; i < APP_COUNT; ++i)
    {
        StringCchPrintfW(state.target[i], ARRAYSIZE(state.target[i]), L"%s\\PinTest%c.exe", state.directory, 'A' + i);
        StringCchPrintfW(state.shortcut[i], ARRAYSIZE(state.shortcut[i]), L"%s\\Pin Test %c.lnk", desktop, 'A' + i);
        if (GetFileAttributesW(state.shortcut[i]) != INVALID_FILE_ATTRIBUTES) return FALSE;
        if (!CopyFileW(module, state.target[i], TRUE)) return FALSE;
        hr = CreateLink(state.target[i], state.shortcut[i]);
        if (FAILED(hr)) return FALSE;
        SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, state.shortcut[i], NULL);
    }
    if (!Snapshot(state.pins, &state.unrelatedHash, &state.unrelatedCount)) return FALSE;
    CHECK(Mask(state.pins) == 0, "Fresh fixtures already have pins\n");
    Log("PIN_FIXTURES_READY directory=%ls\n", state.directory);
    return SaveCheckpoint();
}

static BOOL DeleteFixtureFile(PCWSTR path)
{
    DWORD start = GetTickCount(), error;
    do
    {
        if (DeleteFileW(path)) return TRUE;
        error = GetLastError();
        if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED) break;
        Sleep(200);
    } while (GetTickCount() - start < 10000);
    CHECK(FALSE, "Cleaning fixture %ls failed: %lu\n", path, error);
    return FALSE;
}

static BOOL RunSuite(void)
{
    static const BYTE apps[] = {2, 0, 4, 1, 3, 2, 1, 4, 2, 4, 1, 0, 3, 0, 3};
    static const BOOL pin[] = {1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1};
    static const BYTE finalOrder[] = {2, 4, 1, 0, 3};
    UINT i;
    if (!WaitForShell(0)) return FALSE;
    if (!state.phase)
    {
        for (i = 0; i < ARRAYSIZE(apps); ++i) if (!PinStep(apps[i], pin[i])) return FALSE;
        Log("PIN_CHECKPOINT_FIVE_READY\n");
        Sleep(5000);
        if (!RestartExplorer()) { CHECK(FALSE, "Restarting Explorer failed\n"); return FALSE; }
        if (!VerifyMask(ALL_PINS, FALSE)) return FALSE;
        Log("PIN_EXPLORER_PERSISTENCE_OK\n");
        Sleep(5000);
        for (i = 0; i < APP_COUNT; ++i)
        {
            if (!LaunchStep(finalOrder[i], 1) || !WaitForWindows(finalOrder[i], 0, TRUE)) return FALSE;
            if (!VerifyMask(ALL_PINS, FALSE)) return FALSE;
            Sleep(1500);
        }
        state.phase = 1;
        state.bootTime = BootTime();
        if (!SaveCheckpoint()) return FALSE;
        Sleep(3000);
        if (!RebootGuest()) { CHECK(FALSE, "Requesting reboot failed: %lu\n", GetLastError()); return FALSE; }
        Sleep(INFINITE);
    }
    CHECK(state.phase == 1, "Unexpected checkpoint phase %lu\n", state.phase);
    CHECK(BootTime() > state.bootTime + 20000000, "The second phase did not follow a reboot\n");
    if (failures || !VerifyMask(ALL_PINS, FALSE)) return FALSE;
    Log("PIN_REBOOT_PERSISTENCE_OK\n");
    Sleep(5000);
    for (i = 0; i < APP_COUNT; ++i)
    {
        if (!LaunchStep(finalOrder[i], 1) || !WaitForWindows(finalOrder[i], 0, TRUE)) return FALSE;
        if (!VerifyMask(ALL_PINS, FALSE)) return FALSE;
        Sleep(1500);
    }
    Log("PIN_REBOOT_LAUNCHES_OK\n");
    if (!LaunchStep(2, 1) || !LaunchStep(2, 2)) return FALSE;
    ++steps;
    Log("PIN_TASKBAR_UNPIN_READY step=%u app=C windows=2\n", steps);
    if (!VerifyMask(ALL_PINS & ~(1u << 2), TRUE) || !WaitForWindows(2, 2, FALSE)) return FALSE;
    if (!WaitForWindows(2, 0, TRUE)) return FALSE;
    Log("PIN_UNPIN_RUNNING_OK\n");
    Sleep(3000);
    if (!PinStep(2, TRUE)) return FALSE;
    for (i = 0; i < APP_COUNT; ++i) if (!PinStep(finalOrder[i], FALSE)) return FALSE;
    if (!VerifyMask(0, FALSE)) return FALSE;
    for (i = 0; i < APP_COUNT; ++i)
    {
        DeleteFixtureFile(state.shortcut[i]);
        DeleteFixtureFile(state.target[i]);
        SHChangeNotify(SHCNE_DELETE, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, state.shortcut[i], NULL);
    }
    RemoveDirectoryW(state.directory);
    if (failures) return FALSE;
    state.phase = 2;
    if (!SaveCheckpoint()) return FALSE;
    return failures == 0;
}

static LRESULT CALLBACK FixtureWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void RunFixture(WCHAR letter)
{
    WNDCLASSW wc = {0};
    WCHAR title[32];
    HWND window;
    MSG message;
    wc.lpfnWndProc = FixtureWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"PinParityFixture";
    RegisterClassW(&wc);
    StringCchPrintfW(title, ARRAYSIZE(title), L"Pin Test %c", letter);
    window = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 120, 100, 430, 240, NULL, NULL, wc.hInstance, NULL);
    CreateWindowExW(0, L"STATIC", title, WS_CHILD | WS_VISIBLE, 25, 25, 350, 70, window, NULL, wc.hInstance, NULL);
    while (GetMessageW(&message, NULL, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
}

void mainCRTStartup(void)
{
    WCHAR module[MAX_PATH], logPath[MAX_PATH];
    PCWSTR name;
    HANDLE file;
    DWORD bytes = 0;
    BOOL initialized, success;
    HRESULT hr;
    GetModuleFileNameW(NULL, module, ARRAYSIZE(module));
    name = PathFindFileNameW(module);
    if (!wcsncmp(name, L"PinTest", 7) && name[7] >= 'A' && name[7] <= 'E') { RunFixture(name[7]); ExitProcess(0); }
    if (!wcsstr(GetCommandLineW(), L"--boot-suite"))
    {
        printf("Use --boot-suite ONLY in a disposable VM. This test restarts Explorer and reboots once.\n");
        ExitProcess(2);
    }
    StringCchCopyW(checkpointPath, ARRAYSIZE(checkpointPath), module);
    PathRemoveFileSpecW(checkpointPath);
    StringCchPrintfW(logPath, ARRAYSIZE(logPath), L"%s\\pin-parity.log", checkpointPath);
    PathAppendW(checkpointPath, L"pin-checkpoint.bin");
    logFile = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    serialFile = CreateFileW(L"\\\\.\\Global\\org.qemu.rawserial.0", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { Log("PIN_PARITY_DONE failures=1 CoInitializeEx=%08lx\n", hr); ExitProcess(1); }
    file = CreateFileW(checkpointPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        initialized = ReadFile(file, &state, sizeof(state), &bytes, NULL) && bytes == sizeof(state) && state.version == CHECKPOINT_VERSION;
        CloseHandle(file);
    }
    else initialized = InitializeFixtures(module);
    Log("PIN_PARITY_BEGIN phase=%lu boot=%I64u\n", state.phase, BootTime());
    CHECK(initialized, "Fixture initialization or checkpoint validation failed\n");
    success = initialized && RunSuite();
    if (!success && !failures) ++failures;
    Log("PIN_PARITY_DONE phase=%lu steps=%u failures=%u\n", state.phase, steps, failures);
    CoUninitialize();
    if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
    if (serialFile != INVALID_HANDLE_VALUE) CloseHandle(serialFile);
    ExitProcess(failures ? 1 : 0);
}
