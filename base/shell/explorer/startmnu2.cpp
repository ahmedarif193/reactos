#include "precomp.h"
#include <commctrl.h>
#include <windowsx.h>

#define SM2_RECENT_LINK_KEY L"Software\\ReactOS\\StartMenu2\\RecentApps\\Links"
#define SM2_RECENT_EXE_KEY  L"Software\\ReactOS\\StartMenu2\\RecentApps\\Executables"

enum SM2VIEW { SM2V_MAIN, SM2V_PROGRAMS, SM2V_SEARCH };

enum SM2ROWTYPE
{
    SM2R_RECENT,
    SM2R_ALLPROGRAMS,
    SM2R_BACK,
    SM2R_TREE,
    SM2R_RESULT,
    SM2R_RIGHT,
    SM2R_SHUTDOWN,
    SM2R_SHUTARROW
};

enum SM2RCMD
{
    SM2C_PROFILE,
    SM2C_DOCUMENTS,
    SM2C_PICTURES,
    SM2C_MUSIC,
    SM2C_GAMES,
    SM2C_COMPUTER,
    SM2C_CONTROL,
    SM2C_PRINTERS,
    SM2C_GAP
};

enum SM2SHUTCMD
{
    SM2F_SHUTDOWN = 1,
    SM2F_RESTART,
    SM2F_LOGOFF,
    SM2F_LOCK
};

enum
{
    SM2_TIMER_ANIM = 1,
    SM2_IDC_SEARCH = 1000,
    SM2_CONTEXT_FIRST = 1,
    SM2_CONTEXT_LAST = 0x6FFF,
    SM2_CONTEXT_REMOVE_RECENT = 0x7000,
    SM2M_FLYOUTCMD = WM_APP + 41,
    SM2M_FLYOUTGONE = WM_APP + 42
};

enum SM2ANIMPHASE { SM2A_NONE, SM2A_OPEN, SM2A_CLOSE };

struct SM2PALETTE
{
    COLORREF Border, LeftBg, LeftText, Sep, HotFill, HotBorder;
    COLORREF RightBg, RightText, RightHot, RightSep;
    COLORREF ShutFill, ShutHot, ShutBorder;
    COLORREF SearchBg, SearchBrd, Cue, Tri, Scroll, ScrollHot;
    COLORREF PicEdge, PicBg, PicFg;
};

static const SM2PALETTE g_SM2PalLight =
{
    RGB( 59,  86, 115), RGB(255, 255, 255), RGB( 34,  34,  34),
    RGB(219, 219, 219), RGB(212, 231, 248), RGB(125, 177, 227),
    RGB( 93, 128, 161), RGB(255, 255, 255), RGB(118, 149, 178), RGB(129, 157, 184),
    RGB( 77, 110, 142), RGB(104, 137, 168), RGB(171, 192, 212),
    RGB(255, 255, 255), RGB(163, 163, 163), RGB(121, 121, 121),
    RGB( 68,  68,  68), RGB(205, 205, 205), RGB(166, 166, 166),
    RGB(180, 187, 194), RGB(129, 154, 178), RGB(238, 242, 246)
};

static const SM2PALETTE g_SM2PalDark =
{
    RGB( 24,  24,  24), RGB( 43,  43,  43), RGB(235, 235, 235),
    RGB( 72,  72,  72), RGB( 64,  82, 100), RGB(104, 141, 178),
    RGB( 44,  60,  76), RGB(240, 240, 240), RGB( 64,  82, 100), RGB( 74,  90, 106),
    RGB( 36,  50,  64), RGB( 58,  76,  94), RGB(112, 132, 152),
    RGB( 30,  30,  30), RGB( 92,  92,  92), RGB(150, 150, 150),
    RGB(204, 204, 204), RGB( 88,  88,  88), RGB(124, 124, 124),
    RGB( 70,  70,  70), RGB( 58,  74,  90), RGB(222, 228, 234)
};

static SM2PALETTE g_SM2Pal = g_SM2PalLight;
static HBRUSH g_SM2EditBrush = NULL;

#define SM2_CLR_BORDER      (g_SM2Pal.Border)
#define SM2_CLR_LEFT_BG     (g_SM2Pal.LeftBg)
#define SM2_CLR_LEFT_TEXT   (g_SM2Pal.LeftText)
#define SM2_CLR_SEP         (g_SM2Pal.Sep)
#define SM2_CLR_HOT_FILL    (g_SM2Pal.HotFill)
#define SM2_CLR_HOT_BORDER  (g_SM2Pal.HotBorder)
#define SM2_CLR_RIGHT_BG    (g_SM2Pal.RightBg)
#define SM2_CLR_RIGHT_TEXT  (g_SM2Pal.RightText)
#define SM2_CLR_RIGHT_HOT   (g_SM2Pal.RightHot)
#define SM2_CLR_RIGHT_SEP   (g_SM2Pal.RightSep)
#define SM2_CLR_SHUT_FILL   (g_SM2Pal.ShutFill)
#define SM2_CLR_SHUT_HOT    (g_SM2Pal.ShutHot)
#define SM2_CLR_SHUT_BORDER (g_SM2Pal.ShutBorder)
#define SM2_CLR_SEARCH_BG   (g_SM2Pal.SearchBg)
#define SM2_CLR_SEARCH_BRD  (g_SM2Pal.SearchBrd)
#define SM2_CLR_CUE         (g_SM2Pal.Cue)
#define SM2_CLR_TRI         (g_SM2Pal.Tri)
#define SM2_CLR_SCROLL      (g_SM2Pal.Scroll)
#define SM2_CLR_SCROLL_HOT  (g_SM2Pal.ScrollHot)
#define SM2_CLR_PIC_EDGE    (g_SM2Pal.PicEdge)
#define SM2_CLR_PIC_BG      (g_SM2Pal.PicBg)
#define SM2_CLR_PIC_FG      (g_SM2Pal.PicFg)

static BOOL
SM2IsDarkTheme(VOID)
{
    DWORD dwValue = 1, cbData = sizeof(dwValue), dwType;
    if (SHGetValueW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    L"AppsUseLightTheme", &dwType, &dwValue, &cbData) == ERROR_SUCCESS)
    {
        return dwValue == 0;
    }
    COLORREF c = GetSysColor(COLOR_WINDOW);
    return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000 < 128;
}

static VOID
SM2SelectPalette(VOID)
{
    g_SM2Pal = SM2IsDarkTheme() ? g_SM2PalDark : g_SM2PalLight;
    if (g_SM2EditBrush)
        DeleteObject(g_SM2EditBrush);
    g_SM2EditBrush = CreateSolidBrush(g_SM2Pal.SearchBg);
}

static COLORREF SM2Mix(COLORREF a, COLORREF b, int t);

VOID StartMenu2_GetFlyoutPalette(OUT SM2_FLYOUT_PALETTE *pPal)
{
    SM2SelectPalette();
    pPal->PanelBg = g_SM2Pal.LeftBg;
    pPal->PanelText = g_SM2Pal.LeftText;
    pPal->DimText = SM2Mix(g_SM2Pal.LeftText, g_SM2Pal.LeftBg, 28);
    pPal->HotFill = g_SM2Pal.HotFill;
    pPal->HotBorder = g_SM2Pal.HotBorder;
    pPal->Border = g_SM2Pal.Border;
    pPal->AccentBg = GetSysColor(COLOR_HIGHLIGHT);
    pPal->AccentText = RGB(255, 255, 255);
}

static COLORREF
SM2Mix(COLORREF a, COLORREF b, int t)
{
    if (t <= 0) return a;
    if (t >= 255) return b;
    return RGB(GetRValue(a) + MulDiv(GetRValue(b) - GetRValue(a), t, 255),
               GetGValue(a) + MulDiv(GetGValue(b) - GetGValue(a), t, 255),
               GetBValue(a) + MulDiv(GetBValue(b) - GetBValue(a), t, 255));
}

static double
SM2Ease(double t)
{
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double u = 1.0 - t;
    return 1.0 - u * u * u;
}

struct SM2ITEM
{
    CStringW szName;
    CStringW szPath;
    CStringW szTargetPath;
    BOOL bFolder;
    BOOL bExpanded;
    HICON hSmallIcon;
    HICON hLargeIcon;
    BOOL bSmallTried;
    BOOL bTargetTried;
    CAtlArray<SM2ITEM*> Children;

    SM2ITEM() : bFolder(FALSE), bExpanded(FALSE), hSmallIcon(NULL), hLargeIcon(NULL),
                bSmallTried(FALSE), bTargetTried(FALSE) {}

    ~SM2ITEM()
    {
        Clear();
    }

    VOID Clear()
    {
        for (SIZE_T i = 0; i < Children.GetCount(); i++)
            delete Children[i];
        Children.SetCount(0);
        if (hSmallIcon) { DestroyIcon(hSmallIcon); hSmallIcon = NULL; }
        if (hLargeIcon) { DestroyIcon(hLargeIcon); hLargeIcon = NULL; }
        bSmallTried = FALSE;
        bTargetTried = FALSE;
        szTargetPath.Empty();
        bExpanded = FALSE;
    }
};

static int __cdecl
SM2CompareItems(const void *a, const void *b)
{
    SM2ITEM *pa = *(SM2ITEM**)a;
    SM2ITEM *pb = *(SM2ITEM**)b;
    if (pa->bFolder != pb->bFolder)
        return pa->bFolder ? 1 : -1;
    return lstrcmpiW(pa->szName, pb->szName);
}

static VOID
SM2SortTree(SM2ITEM *pParent)
{
    SIZE_T count = pParent->Children.GetCount();
    if (count > 1)
        qsort(pParent->Children.GetData(), count, sizeof(SM2ITEM*), SM2CompareItems);
    for (SIZE_T i = 0; i < count; i++)
    {
        if (pParent->Children[i]->bFolder)
            SM2SortTree(pParent->Children[i]);
    }
}

static SM2ITEM*
SM2FindChild(SM2ITEM *pParent, LPCWSTR pszName, BOOL bFolder)
{
    for (SIZE_T i = 0; i < pParent->Children.GetCount(); i++)
    {
        SM2ITEM *p = pParent->Children[i];
        if (p->bFolder == bFolder && p->szName.CompareNoCase(pszName) == 0)
            return p;
    }
    return NULL;
}

static VOID
SM2EnumFolder(LPCWSTR pszDir, SM2ITEM *pParent, BOOL bFilesOnly)
{
    WCHAR szSpec[MAX_PATH];
    WIN32_FIND_DATAW wfd;
    HANDLE hFind;

    StringCchPrintfW(szSpec, _countof(szSpec), L"%s\\*", pszDir);
    hFind = FindFirstFileW(szSpec, &wfd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (wfd.cFileName[0] == L'.' &&
            (wfd.cFileName[1] == 0 || (wfd.cFileName[1] == L'.' && wfd.cFileName[2] == 0)))
            continue;
        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
            continue;

        WCHAR szFull[MAX_PATH];
        StringCchPrintfW(szFull, _countof(szFull), L"%s\\%s", pszDir, wfd.cFileName);

        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (bFilesOnly)
                continue;
            SM2ITEM *pFolder = SM2FindChild(pParent, wfd.cFileName, TRUE);
            if (!pFolder)
            {
                pFolder = new SM2ITEM();
                pFolder->bFolder = TRUE;
                pFolder->szName = wfd.cFileName;
                pFolder->szPath = szFull;
                pParent->Children.Add(pFolder);
            }
            SM2EnumFolder(szFull, pFolder, FALSE);
        }
        else
        {
            LPCWSTR pszExt = PathFindExtensionW(wfd.cFileName);
            if (lstrcmpiW(pszExt, L".lnk") != 0 && lstrcmpiW(pszExt, L".url") != 0)
                continue;

            WCHAR szName[MAX_PATH];
            StringCchCopyW(szName, _countof(szName), wfd.cFileName);
            PathRemoveExtensionW(szName);

            if (SM2FindChild(pParent, szName, FALSE))
                continue;

            SM2ITEM *pItem = new SM2ITEM();
            pItem->bFolder = FALSE;
            pItem->szName = szName;
            pItem->szPath = szFull;
            pParent->Children.Add(pItem);
        }
    } while (FindNextFileW(hFind, &wfd));

    FindClose(hFind);
}

static VOID
SM2BuildTree(SM2ITEM *pRoot)
{
    static const int roots[] =
    {
        CSIDL_STARTMENU, CSIDL_COMMON_STARTMENU,
        CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS
    };

    pRoot->Clear();

    for (UINT i = 0; i < _countof(roots); i++)
    {
        WCHAR szPath[MAX_PATH];
        BOOL bFilesOnly = (roots[i] == CSIDL_STARTMENU || roots[i] == CSIDL_COMMON_STARTMENU);
        if (SUCCEEDED(SHGetFolderPathW(NULL, roots[i], NULL, SHGFP_TYPE_CURRENT, szPath)))
            SM2EnumFolder(szPath, pRoot, bFilesOnly);
    }

    SM2SortTree(pRoot);
}

static VOID
SM2FlattenTree(SM2ITEM *pParent, CAtlArray<SM2ITEM*> &Flat)
{
    for (SIZE_T i = 0; i < pParent->Children.GetCount(); i++)
    {
        SM2ITEM *p = pParent->Children[i];
        if (p->bFolder)
            SM2FlattenTree(p, Flat);
        else
            Flat.Add(p);
    }
}

static BOOL
SM2NormalizePath(LPCWSTR pszPath, CStringW &Path)
{
    WCHAR szExpanded[MAX_PATH], szFull[MAX_PATH];
    LPCWSTR pszExpanded = pszPath;

    if (!pszPath || !*pszPath)
        return FALSE;

    DWORD cch = ExpandEnvironmentStringsW(pszPath, szExpanded, _countof(szExpanded));
    if (cch && cch <= _countof(szExpanded))
        pszExpanded = szExpanded;

    cch = GetFullPathNameW(pszExpanded, _countof(szFull), szFull, NULL);
    if (cch && cch < _countof(szFull))
        Path = szFull;
    else
        Path = pszExpanded;

    return !Path.IsEmpty();
}

static ULONGLONG
SM2GetLastUsed(LPCWSTR pszKey, LPCWSTR pszPath)
{
    ULONGLONG Time = 0;
    DWORD cbData = sizeof(Time), dwType = 0;

    if (SHGetValueW(HKEY_CURRENT_USER, pszKey, pszPath,
                    &dwType, &Time, &cbData) != ERROR_SUCCESS ||
        dwType != REG_QWORD || cbData != sizeof(Time))
    {
        return 0;
    }
    return Time;
}

static VOID
SM2SetLastUsed(LPCWSTR pszKey, LPCWSTR pszPath)
{
    FILETIME ft;
    ULARGE_INTEGER Time;

    GetSystemTimeAsFileTime(&ft);
    Time.LowPart = ft.dwLowDateTime;
    Time.HighPart = ft.dwHighDateTime;
    SHSetValueW(HKEY_CURRENT_USER, pszKey, pszPath,
                REG_QWORD, &Time.QuadPart, sizeof(Time.QuadPart));
}

static BOOL
SM2GetTargetPath(SM2ITEM *pItem)
{
    if (pItem->bTargetTried)
        return !pItem->szTargetPath.IsEmpty();

    pItem->bTargetTried = TRUE;
    if (lstrcmpiW(PathFindExtensionW(pItem->szPath), L".lnk") != 0)
        return FALSE;

    CComPtr<IShellLinkW> pLink;
    if (FAILED(pLink.CoCreateInstance(CLSID_ShellLink, IID_IShellLinkW)))
        return FALSE;

    CComPtr<IPersistFile> pPersist;
    if (FAILED(pLink->QueryInterface(IID_PPV_ARG(IPersistFile, &pPersist))) ||
        FAILED(pPersist->Load(pItem->szPath, STGM_READ)))
    {
        return FALSE;
    }

    WCHAR szTarget[MAX_PATH] = L"";
    if (FAILED(pLink->GetPath(szTarget, _countof(szTarget), NULL, 0)) ||
        !szTarget[0])
    {
        return FALSE;
    }

    return SM2NormalizePath(szTarget, pItem->szTargetPath);
}

static HICON
SM2LoadShellIcon(LPCWSTR pszPath, BOOL bLarge)
{
    SHFILEINFOW sfi;
    ZeroMemory(&sfi, sizeof(sfi));
    SHGetFileInfoW(pszPath, 0, &sfi, sizeof(sfi),
                   SHGFI_ICON | (bLarge ? SHGFI_LARGEICON : SHGFI_SMALLICON));
    return sfi.hIcon;
}

static VOID
SM2LaunchItem(SM2ITEM *pItem)
{
    if (!pItem || pItem->bFolder)
        return;

    HINSTANCE hResult = ShellExecuteW(NULL, NULL, pItem->szPath, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hResult > 32)
        SM2SetLastUsed(SM2_RECENT_LINK_KEY, pItem->szPath);
}

static DWORD
SM2ExitWindows(UINT uFlags)
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    TOKEN_PRIVILEGES PreviousState;
    DWORD cbPreviousState = sizeof(PreviousState);
    DWORD dwError;

    if (uFlags == EWX_LOGOFF)
        return ExitWindowsEx(uFlags, SHTDN_REASON_MAJOR_OTHER) ? ERROR_SUCCESS : GetLastError();

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return GetLastError();

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid))
    {
        dwError = GetLastError();
        CloseHandle(hToken);
        return dwError;
    }

    /* A successful return can still mean that the token lacks the privilege. */
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(PreviousState), &PreviousState, &cbPreviousState))
    {
        dwError = GetLastError();
        CloseHandle(hToken);
        return dwError;
    }
    dwError = GetLastError();
    if (dwError != ERROR_SUCCESS)
    {
        CloseHandle(hToken);
        return dwError;
    }

    /* Let Winlogon end the session normally, including application vetoes. */
    dwError = ExitWindowsEx(uFlags, SHTDN_REASON_MAJOR_OTHER) ? ERROR_SUCCESS : GetLastError();

    /* Explorer survives a cancelled shutdown: restore its original privileges. */
    if (!AdjustTokenPrivileges(hToken, FALSE, &PreviousState, 0, NULL, NULL) &&
        dwError == ERROR_SUCCESS)
    {
        dwError = GetLastError();
    }

    CloseHandle(hToken);
    return dwError;
}

static VOID
SM2DoShutdownCmd(HWND hwndOwner, int nCmd)
{
    static BOOL bInProgress = FALSE;
    DWORD dwError = ERROR_SUCCESS;
    LPCWSTR pszAction;

    /* ExitWindowsEx can pump messages while the shutdown request is pending. */
    if (bInProgress)
        return;

    switch (nCmd)
    {
        case SM2F_SHUTDOWN:
            pszAction = L"Shut down";
            break;
        case SM2F_RESTART:
            pszAction = L"Restart";
            break;
        case SM2F_LOGOFF:
            pszAction = L"Log off";
            break;
        case SM2F_LOCK:
            pszAction = L"Lock";
            break;
        default:
            return;
    }

    bInProgress = TRUE;
    switch (nCmd)
    {
        case SM2F_SHUTDOWN:
        case SM2F_RESTART:
            if (SHRestricted(REST_NOCLOSE))
                dwError = ERROR_ACCESS_DISABLED_BY_POLICY;
            else
                dwError = SM2ExitWindows(nCmd == SM2F_RESTART ? EWX_REBOOT : EWX_POWEROFF);
            break;
        case SM2F_LOGOFF:
            dwError = SM2ExitWindows(EWX_LOGOFF);
            break;
        case SM2F_LOCK:
            if (!LockWorkStation())
                dwError = GetLastError();
            break;
    }

    if (dwError != ERROR_SUCCESS && dwError != ERROR_CANCELLED)
    {
        WCHAR szError[512];
        if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dwError, 0, szError, _countof(szError), NULL))
            StringCchPrintfW(szError, _countof(szError), L"The operation could not be completed (error %lu).", dwError);
        ::MessageBoxW(hwndOwner, szError, pszAction, MB_OK | MB_ICONERROR);
    }
    bInProgress = FALSE;
}

static VOID
SM2OpenPath(LPCWSTR pszPath)
{
    ShellExecuteW(NULL, L"open", pszPath, NULL, NULL, SW_SHOWNORMAL);
}

static VOID
SM2OpenCsidlFolder(int csidl)
{
    WCHAR szPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, szPath)))
        SM2OpenPath(szPath);
}

static VOID
SM2OpenCsidlIdList(int csidl)
{
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_IDLIST;
    sei.nShow = SW_SHOWNORMAL;
    sei.lpIDList = SHCloneSpecialIDList(NULL, csidl, FALSE);
    if (sei.lpIDList)
    {
        ShellExecuteExW(&sei);
        ILFree((LPITEMIDLIST)sei.lpIDList);
    }
}

static VOID
SM2OpenGames(VOID)
{
    static const int roots[] = { CSIDL_COMMON_PROGRAMS, CSIDL_PROGRAMS };
    for (UINT i = 0; i < _countof(roots); i++)
    {
        WCHAR szPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, roots[i], NULL, SHGFP_TYPE_CURRENT, szPath)))
        {
            PathAppendW(szPath, L"Games");
            if (PathIsDirectoryW(szPath))
            {
                SM2OpenPath(szPath);
                return;
            }
        }
    }
    SM2OpenCsidlFolder(CSIDL_PROGRAMS);
}

static VOID
SM2ExecRightCmd(int nCmd)
{
    switch (nCmd)
    {
        case SM2C_PROFILE:   SM2OpenCsidlFolder(CSIDL_PROFILE); break;
        case SM2C_DOCUMENTS: SM2OpenCsidlFolder(CSIDL_PERSONAL); break;
        case SM2C_PICTURES:  SM2OpenCsidlFolder(CSIDL_MYPICTURES); break;
        case SM2C_MUSIC:     SM2OpenCsidlFolder(CSIDL_MYMUSIC); break;
        case SM2C_GAMES:     SM2OpenGames(); break;
        case SM2C_COMPUTER:  SM2OpenCsidlIdList(CSIDL_DRIVES); break;
        case SM2C_CONTROL:   SM2OpenCsidlIdList(CSIDL_CONTROLS); break;
        case SM2C_PRINTERS:  SM2OpenCsidlIdList(CSIDL_PRINTERS); break;
    }
}

struct SM2RIGHTDEF
{
    LPCWSTR pszLabel;
    int nCmd;
};

static const SM2RIGHTDEF g_SM2RightDefs[] =
{
    { L"Documents",            SM2C_DOCUMENTS },
    { L"Pictures",             SM2C_PICTURES },
    { L"Music",                SM2C_MUSIC },
    { NULL,                    SM2C_GAP },
    { L"Games",                SM2C_GAMES },
    { L"Computer",             SM2C_COMPUTER },
    { NULL,                    SM2C_GAP },
    { L"Control Panel",        SM2C_CONTROL },
    { L"Devices and Printers", SM2C_PRINTERS },
};

struct SM2ROW
{
    RECT rc;
    int nType;
    SM2ITEM *pItem;
    int nData;
};

typedef CWinTraits<WS_POPUP | WS_CLIPCHILDREN,
                   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED> CStartMenu2Traits;
typedef CWinTraits<WS_POPUP,
                   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE> CStartMenu2PicTraits;

class CStartMenu2UserPic :
    public CWindowImpl<CStartMenu2UserPic, CWindow, CStartMenu2PicTraits>
{
public:
    DECLARE_WND_CLASS_EX(L"StartMenu2UserPic", 0, COLOR_WINDOW)

    int m_iDpi;

    CStartMenu2UserPic() : m_iDpi(96) {}

    int Sc(int v) const { return MulDiv(v, m_iDpi, 96); }

    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return 1;
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);
        RECT rc;
        GetClientRect(&rc);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

        HBRUSH hbr = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(hdcMem, &rc, hbr);
        DeleteObject(hbr);

        HBRUSH hbrEdge = CreateSolidBrush(SM2_CLR_PIC_EDGE);
        FrameRect(hdcMem, &rc, hbrEdge);
        DeleteObject(hbrEdge);

        RECT rcPic = rc;
        InflateRect(&rcPic, -Sc(7), -Sc(7));
        hbr = CreateSolidBrush(SM2_CLR_PIC_BG);
        FillRect(hdcMem, &rcPic, hbr);
        DeleteObject(hbr);

        int cx = (rcPic.left + rcPic.right) / 2;
        int w = rcPic.right - rcPic.left;
        int headR = w * 16 / 100;
        int headCy = rcPic.top + w * 38 / 100;

        SaveDC(hdcMem);
        IntersectClipRect(hdcMem, rcPic.left, rcPic.top, rcPic.right, rcPic.bottom);

        HBRUSH hbrFg = CreateSolidBrush(SM2_CLR_PIC_FG);
        HGDIOBJ hbrOld = SelectObject(hdcMem, hbrFg);
        HGDIOBJ hpenOld = SelectObject(hdcMem, GetStockObject(NULL_PEN));

        Ellipse(hdcMem, cx - headR, headCy - headR, cx + headR + 1, headCy + headR + 1);

        int bodyR = w * 30 / 100;
        int bodyTop = headCy + headR + Sc(3);
        Ellipse(hdcMem, cx - bodyR, bodyTop, cx + bodyR + 1, bodyTop + bodyR * 2);

        SelectObject(hdcMem, hpenOld);
        SelectObject(hdcMem, hbrOld);
        DeleteObject(hbrFg);
        RestoreDC(hdcMem, -1);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);

        EndPaint(&ps);
        return 0;
    }

    BEGIN_MSG_MAP(CStartMenu2UserPic)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
    END_MSG_MAP()
};

class CStartMenu2Flyout :
    public CWindowImpl<CStartMenu2Flyout, CWindow, CStartMenu2Traits>
{
public:
    DECLARE_WND_CLASS_EX(L"StartMenu2Flyout", CS_DROPSHADOW, COLOR_WINDOW)

    HWND m_hwndOwner;
    HFONT m_hFont;
    int m_iDpi;
    int m_iHot;
    ULONGLONG m_AnimT0;
    BOOL m_bTracking;

    struct FLYITEM { LPCWSTR pszLabel; int nCmd; };
    static const FLYITEM c_Items[3];

    CStartMenu2Flyout() : m_hwndOwner(NULL), m_hFont(NULL), m_iDpi(96),
                          m_iHot(-1), m_AnimT0(0), m_bTracking(FALSE) {}

    int Sc(int v) const { return MulDiv(v, m_iDpi, 96); }
    int RowH() const { return Sc(26); }

    BOOL Popup(HWND hwndOwner, HFONT hFont, int iDpi, const RECT *prcAnchor)
    {
        m_hwndOwner = hwndOwner;
        m_hFont = hFont;
        m_iDpi = iDpi;

        int w = Sc(150);
        int h = RowH() * _countof(c_Items) + Sc(10);
        int x = prcAnchor->right - w;
        int y = prcAnchor->top - h - Sc(4);

        if (!Create(hwndOwner, CWindow::rcDefault, NULL))
            return FALSE;

        SetWindowPos(HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
        ShowWindow(SW_SHOWNA);
        m_AnimT0 = GetTickCount64();
        SetTimer(SM2_TIMER_ANIM, 16, NULL);
        return TRUE;
    }

    int HitTest(POINT pt)
    {
        RECT rc;
        GetClientRect(&rc);
        for (UINT i = 0; i < _countof(c_Items); i++)
        {
            RECT rcItem = { Sc(4), Sc(5) + RowH() * (int)i, rc.right - Sc(4), Sc(5) + RowH() * ((int)i + 1) };
            if (PtInRect(&rcItem, pt))
                return i;
        }
        return -1;
    }

    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        double t = (double)(GetTickCount64() - m_AnimT0) / 120.0;
        BYTE alpha = (BYTE)(255.0 * SM2Ease(t));
        SetLayeredWindowAttributes(m_hWnd, 0, alpha, LWA_ALPHA);
        if (t >= 1.0)
            KillTimer(SM2_TIMER_ANIM);
        return 0;
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);
        RECT rc;
        GetClientRect(&rc);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

        HBRUSH hbr = CreateSolidBrush(SM2_CLR_RIGHT_BG);
        FillRect(hdcMem, &rc, hbr);
        DeleteObject(hbr);
        HBRUSH hbrEdge = CreateSolidBrush(SM2_CLR_SHUT_BORDER);
        FrameRect(hdcMem, &rc, hbrEdge);
        DeleteObject(hbrEdge);

        SetBkMode(hdcMem, TRANSPARENT);
        HGDIOBJ hFontOld = SelectObject(hdcMem, m_hFont);

        for (UINT i = 0; i < _countof(c_Items); i++)
        {
            RECT rcItem = { Sc(4), Sc(5) + RowH() * (int)i, rc.right - Sc(4), Sc(5) + RowH() * ((int)i + 1) };
            if ((int)i == m_iHot)
            {
                HBRUSH hbrHot = CreateSolidBrush(SM2_CLR_RIGHT_HOT);
                FillRect(hdcMem, &rcItem, hbrHot);
                DeleteObject(hbrHot);
            }
            RECT rcText = rcItem;
            rcText.left += Sc(12);
            SetTextColor(hdcMem, SM2_CLR_RIGHT_TEXT);
            DrawTextW(hdcMem, c_Items[i].pszLabel, -1, &rcText,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        SelectObject(hdcMem, hFontOld);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return 1;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int iHot = HitTest(pt);
        if (iHot != m_iHot)
        {
            m_iHot = iHot;
            InvalidateRect(NULL, FALSE);
        }
        if (!m_bTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
            TrackMouseEvent(&tme);
            m_bTracking = TRUE;
        }
        return 0;
    }

    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_bTracking = FALSE;
        if (m_iHot != -1)
        {
            m_iHot = -1;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    VOID ActivateItem(int i)
    {
        if (i < 0 || i >= (int)_countof(c_Items))
            return;

        HWND hwndOwner = m_hwndOwner;
        int nCmd = c_Items[i].nCmd;
        DestroyWindow();
        ::PostMessageW(hwndOwner, SM2M_FLYOUTCMD, nCmd, 0);
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ActivateItem(HitTest(pt));
        return 0;
    }

    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == VK_ESCAPE || wParam == VK_LEFT)
        {
            HWND hwndOwner = m_hwndOwner;
            DestroyWindow();
            ::PostMessageW(hwndOwner, SM2M_FLYOUTGONE, 0, 0);
        }
        else if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_HOME || wParam == VK_END)
        {
            int count = (int)_countof(c_Items);
            if (wParam == VK_HOME)
                m_iHot = 0;
            else if (wParam == VK_END)
                m_iHot = count - 1;
            else if (m_iHot < 0)
                m_iHot = (wParam == VK_UP) ? count - 1 : 0;
            else
                m_iHot = (m_iHot + (wParam == VK_UP ? count - 1 : 1)) % count;
            InvalidateRect(NULL, FALSE);
        }
        else if (wParam == VK_RETURN || wParam == VK_SPACE)
        {
            ActivateItem(m_iHot);
        }
        return 0;
    }

    LRESULT OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            HWND hwndOther = (HWND)lParam;
            HWND hwndOwner = m_hwndOwner;
            BOOL bOutside = (hwndOther != hwndOwner);
            DestroyWindow();
            ::PostMessageW(hwndOwner, SM2M_FLYOUTGONE, bOutside, 0);
        }
        return 0;
    }

    virtual void OnFinalMessage(HWND hWnd) override
    {
        delete this;
    }

    BEGIN_MSG_MAP(CStartMenu2Flyout)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
    END_MSG_MAP()
};

const CStartMenu2Flyout::FLYITEM CStartMenu2Flyout::c_Items[3] =
{
    { L"Restart", SM2F_RESTART },
    { L"Log off (soon)", SM2F_LOGOFF },
    { L"Lock (soon)",    SM2F_LOCK },
};

class CStartMenu2Wnd :
    public CWindowImpl<CStartMenu2Wnd, CWindow, CStartMenu2Traits>
{
public:
    DECLARE_WND_CLASS_EX(L"StartMenu2", CS_DROPSHADOW, COLOR_WINDOW)

    struct HOTANIM
    {
        BOOL bActive;
        ULONGLONG t0;
        int nType;
        SM2ITEM *pItem;
        int nData;
    };

    ITrayWindow *m_Tray;
    HWND m_hwndTray;
    HWND m_hwndEdit;
    HFONT m_hFont;
    HFONT m_hFontCue;
    HFONT m_hFontUser;
    HTHEME m_hLiquidTheme;
    int m_iDpi;

    SM2ITEM m_Root;
    CAtlArray<SM2ITEM*> m_Flat;
    CAtlArray<SM2ITEM*> m_RecentExecutables;
    CAtlArray<SM2ITEM*> m_Recent;
    CAtlArray<SM2ITEM*> m_Results;
    CAtlArray<SM2ITEM*> m_TreeVis;
    CAtlArray<int> m_TreeDepth;
    CAtlArray<SM2ROW> m_Rows;
    CAtlArray<int> m_SepYs;

    int m_nView;
    int m_nScroll;
    int m_nContentH;
    SIZE m_size;

    RECT m_rcLeft, m_rcRight, m_rcList, m_rcSearch, m_rcBottomRow;
    RECT m_rcShut, m_rcShutArrow, m_rcUser, m_rcSlide;
    RECT m_rcScroll, m_rcThumb;
    BOOL m_bScrollVis, m_bDragScroll;
    int m_nDragOff;

    int m_iHot, m_iSel, m_iPressed;
    HOTANIM m_HotIn, m_HotOut;

    int m_AnimPhase;
    ULONGLONG m_AnimT0;
    POINT m_ptFinal;
    int m_dxSlide, m_dySlide;

    struct
    {
        BOOL bActive;
        ULONGLONG t0;
        int dir;
        HBITMAP hbmOld;
        HBITMAP hbmNew;
    } m_ViewAnim;

    CStartMenu2UserPic m_UserPic;
    HWND m_hwndFlyout;
    ULONGLONG m_LastDismiss;
    UINT m_uPosition;
    WCHAR m_szUser[128];
    BOOL m_bTracking;
    BOOL m_bInContextMenu;
    CComPtr<IContextMenu2> m_ContextMenu2;
    CComPtr<IContextMenu3> m_ContextMenu3;

    CStartMenu2Wnd() :
        m_Tray(NULL), m_hwndTray(NULL), m_hwndEdit(NULL),
        m_hFont(NULL), m_hFontCue(NULL), m_hFontUser(NULL),
        m_hLiquidTheme(NULL), m_iDpi(96),
        m_nView(SM2V_MAIN), m_nScroll(0), m_nContentH(0),
        m_bScrollVis(FALSE), m_bDragScroll(FALSE), m_nDragOff(0),
        m_iHot(-1), m_iSel(-1), m_iPressed(-1),
        m_AnimPhase(SM2A_NONE), m_AnimT0(0), m_dxSlide(0), m_dySlide(0),
        m_hwndFlyout(NULL), m_LastDismiss(0), m_uPosition(ABE_BOTTOM),
        m_bTracking(FALSE), m_bInContextMenu(FALSE)
    {
        ZeroMemory(&m_HotIn, sizeof(m_HotIn));
        ZeroMemory(&m_HotOut, sizeof(m_HotOut));
        ZeroMemory(&m_ViewAnim, sizeof(m_ViewAnim));
        ZeroMemory(&m_size, sizeof(m_size));
        ZeroMemory(&m_ptFinal, sizeof(m_ptFinal));
        m_szUser[0] = 0;
    }

    int Sc(int v) const { return MulDiv(v, m_iDpi, 96); }

    VOID RefreshLiquidMaterial()
    {
        if (m_hLiquidTheme)
            CloseThemeData(m_hLiquidTheme);
        m_hLiquidTheme = OpenThemeData(m_hWnd, L"StartMenu");
    }
    int MenuW() const { return Sc(430); }
    int MenuH() const { return Sc(488); }
    int LeftW() const { return Sc(250); }
    int RecentRowH() const { return Sc(40); }
    int ListRowH() const { return Sc(26); }
    int RightRowH() const { return Sc(30); }

    BOOL Init(ITrayWindow *pTray, HWND hwndTray)
    {
        m_Tray = pTray;
        m_hwndTray = hwndTray;

        HDC hdc = ::GetDC(NULL);
        if (hdc)
        {
            m_iDpi = GetDeviceCaps(hdc, LOGPIXELSY);
            ::ReleaseDC(NULL, hdc);
        }
        if (m_iDpi <= 0)
            m_iDpi = 96;

        return Create(hwndTray, CWindow::rcDefault, NULL) != NULL;
    }

    VOID Term()
    {
        if (IsWindow())
            DestroyWindow();
    }

    BOOL IsMenuVisible()
    {
        return IsWindow() && IsWindowVisible() && m_AnimPhase != SM2A_CLOSE;
    }

    VOID BuildRecent()
    {
        m_Recent.SetCount(0);

        CAtlArray<ULONGLONG> LastUsed;
        LastUsed.SetCount(m_Flat.GetCount());
        for (SIZE_T i = 0; i < m_Flat.GetCount(); i++)
            LastUsed[i] = SM2GetLastUsed(SM2_RECENT_LINK_KEY, m_Flat[i]->szPath);

        /* A shell-window notification identifies an executable rather than the
           shortcut used to start it. Fold shortcuts with the same target into
           one entry, preferring the shortcut most recently launched directly. */
        CAtlArray<BOOL> Grouped;
        Grouped.SetCount(m_Flat.GetCount());
        for (SIZE_T i = 0; i < m_Flat.GetCount(); i++)
            Grouped[i] = FALSE;

        for (SIZE_T i = 0; i < m_Flat.GetCount(); i++)
        {
            if (Grouped[i] || !SM2GetTargetPath(m_Flat[i]))
                continue;

            SIZE_T representative = i;
            ULONGLONG groupTime = SM2GetLastUsed(SM2_RECENT_EXE_KEY,
                                                  m_Flat[i]->szTargetPath);
            ULONGLONG linkTime = LastUsed[i];

            for (SIZE_T j = i; j < m_Flat.GetCount(); j++)
            {
                if (!SM2GetTargetPath(m_Flat[j]) ||
                    m_Flat[j]->szTargetPath.CompareNoCase(m_Flat[i]->szTargetPath) != 0)
                {
                    continue;
                }

                Grouped[j] = TRUE;
                if (LastUsed[j] > linkTime)
                {
                    representative = j;
                    linkTime = LastUsed[j];
                }
                if (LastUsed[j] > groupTime)
                    groupTime = LastUsed[j];
                LastUsed[j] = 0;
            }
            LastUsed[representative] = groupTime;
        }

        CAtlArray<BOOL> Used;
        Used.SetCount(m_Flat.GetCount());
        for (SIZE_T i = 0; i < m_Flat.GetCount(); i++)
            Used[i] = FALSE;

        while (m_Recent.GetCount() < 10)
        {
            int best = -1;
            for (SIZE_T i = 0; i < m_Flat.GetCount(); i++)
            {
                if (Used[i] || LastUsed[i] == 0)
                    continue;
                if (best < 0 || LastUsed[i] > LastUsed[best] ||
                    (LastUsed[i] == LastUsed[best] &&
                     lstrcmpiW(m_Flat[i]->szName, m_Flat[best]->szName) < 0))
                {
                    best = (int)i;
                }
            }
            if (best < 0)
                break;
            Used[best] = TRUE;
            m_Recent.Add(m_Flat[best]);
        }

        for (SIZE_T i = 0; i < m_Recent.GetCount(); i++)
        {
            if (!m_Recent[i]->hLargeIcon)
                m_Recent[i]->hLargeIcon = SM2LoadShellIcon(m_Recent[i]->szPath, TRUE);
        }
    }

    VOID AddTreeVis(SM2ITEM *pParent, int nDepth)
    {
        for (SIZE_T i = 0; i < pParent->Children.GetCount(); i++)
        {
            SM2ITEM *p = pParent->Children[i];
            m_TreeVis.Add(p);
            m_TreeDepth.Add(nDepth);
            if (p->bFolder && p->bExpanded)
                AddTreeVis(p, nDepth + 1);
        }
    }

    VOID BuildTreeVis()
    {
        m_TreeVis.SetCount(0);
        m_TreeDepth.SetCount(0);
        AddTreeVis(&m_Root, 0);
    }

    VOID ClearRecentExecutables()
    {
        for (SIZE_T i = 0; i < m_RecentExecutables.GetCount(); ++i)
            delete m_RecentExecutables[i];
        m_RecentExecutables.SetCount(0);
    }

    VOID AddRecentExecutables()
    {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, SM2_RECENT_EXE_KEY, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
            return;
        for (DWORD i = 0;; ++i)
        {
            WCHAR szPath[MAX_PATH], szKey[MAX_PATH];
            DWORD cchPath = _countof(szPath), type, cbTime = sizeof(ULONGLONG);
            ULONGLONG Time;
            LONG error = RegEnumValueW(hKey, i, szPath, &cchPath, NULL, &type, (LPBYTE)&Time, &cbTime);
            if (error == ERROR_MORE_DATA)
                continue;
            if (error != ERROR_SUCCESS)
                break;
            if (type != REG_QWORD || cbTime != sizeof(Time) || !Time || lstrcmpiW(PathFindExtensionW(szPath), L".exe"))
                continue;
            DWORD attributes = GetFileAttributesW(szPath);
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            StringCchPrintfW(szKey, _countof(szKey), L"Applications\\%s", PathFindFileNameW(szPath));
            if (SHGetValueW(HKEY_CLASSES_ROOT, szKey, L"NoStartPage", NULL, NULL, NULL) == ERROR_SUCCESS)
                continue;

            BOOL bHasShortcut = FALSE;
            for (SIZE_T j = 0; j < m_Flat.GetCount(); ++j)
            {
                if (SM2GetTargetPath(m_Flat[j]) && !m_Flat[j]->szTargetPath.CompareNoCase(szPath))
                {
                    bHasShortcut = TRUE;
                    break;
                }
            }
            if (bHasShortcut)
                continue;
            SM2ITEM *pItem = new SM2ITEM;
            if (!pItem)
                break;
            pItem->szPath = szPath;
            pItem->szTargetPath = szPath;
            pItem->bTargetTried = TRUE;
            pItem->szName = PathFindFileNameW(szPath);
            PathRemoveExtensionW(pItem->szName.GetBuffer());
            pItem->szName.ReleaseBuffer();
            m_RecentExecutables.Add(pItem);
            m_Flat.Add(pItem);
        }
        RegCloseKey(hKey);
    }

    VOID RebuildData()
    {
        ClearHotAnims();
        ClearViewAnim();
        ClearRecentExecutables();
        m_Flat.SetCount(0);
        m_Recent.SetCount(0);
        m_Results.SetCount(0);
        m_TreeVis.SetCount(0);
        m_TreeDepth.SetCount(0);
        m_Rows.SetCount(0);
        SM2BuildTree(&m_Root);
        SM2FlattenTree(&m_Root, m_Flat);
        AddRecentExecutables();
        BuildRecent();
        BuildTreeVis();
    }

    VOID BuildResults(LPCWSTR pszQuery)
    {
        m_Results.SetCount(0);
        for (SIZE_T i = 0; i < m_Flat.GetCount() && m_Results.GetCount() < 100; i++)
        {
            if (StrStrIW(m_Flat[i]->szName, pszQuery))
                m_Results.Add(m_Flat[i]);
        }
    }

    VOID Layout()
    {
        int w = m_size.cx, h = m_size.cy;

        SetRect(&m_rcLeft, 1, 1, LeftW(), h - 1);
        SetRect(&m_rcRight, LeftW(), 1, w - 1, h - 1);

        SetRect(&m_rcSearch, m_rcLeft.left + Sc(11), h - Sc(42),
                m_rcLeft.right - Sc(11), h - Sc(12));
        SetRect(&m_rcBottomRow, m_rcLeft.left + Sc(6), m_rcSearch.top - Sc(38),
                m_rcLeft.right - Sc(6), m_rcSearch.top - Sc(8));
        SetRect(&m_rcList, m_rcLeft.left + Sc(6), m_rcLeft.top + Sc(6),
                m_rcLeft.right - Sc(6), m_rcBottomRow.top - Sc(4));
        SetRect(&m_rcSlide, m_rcLeft.left, m_rcLeft.top,
                m_rcLeft.right, m_rcSearch.top - Sc(4));

        int rx = m_rcRight.left + Sc(14);
        int rw = m_rcRight.right - Sc(14);
        SetRect(&m_rcUser, rx, m_rcRight.top + Sc(32), rw,
                m_rcRight.top + Sc(32) + RightRowH());

        int arroww = Sc(22);
        SetRect(&m_rcShut, rx, h - Sc(42), rw - arroww, h - Sc(14));
        SetRect(&m_rcShutArrow, rw - arroww, h - Sc(42), rw, h - Sc(14));

        if (m_hwndEdit)
        {
            int editH = Sc(18);
            int sh = m_rcSearch.bottom - m_rcSearch.top;
            ::MoveWindow(m_hwndEdit,
                         m_rcSearch.left + Sc(7),
                         m_rcSearch.top + (sh - editH) / 2,
                         m_rcSearch.right - m_rcSearch.left - Sc(38),
                         editH, TRUE);
        }
    }

    VOID AddRow(const RECT &rc, int nType, SM2ITEM *pItem, int nData)
    {
        SM2ROW row;
        row.rc = rc;
        row.nType = nType;
        row.pItem = pItem;
        row.nData = nData;
        m_Rows.Add(row);
    }

    VOID LayoutListRows(CAtlArray<SM2ITEM*> &Items, int nType, int nRowH, const RECT &rcArea)
    {
        int areaH = rcArea.bottom - rcArea.top;
        m_nContentH = (int)Items.GetCount() * nRowH;
        m_bScrollVis = (m_nContentH > areaH);

        int nMaxScroll = m_bScrollVis ? m_nContentH - areaH : 0;
        if (m_nScroll > nMaxScroll) m_nScroll = nMaxScroll;
        if (m_nScroll < 0) m_nScroll = 0;

        int rightEdge = rcArea.right - (m_bScrollVis ? Sc(12) : 0);

        for (SIZE_T i = 0; i < Items.GetCount(); i++)
        {
            int top = rcArea.top + (int)i * nRowH - m_nScroll;
            if (top + nRowH <= rcArea.top)
                continue;
            if (top >= rcArea.bottom)
                break;
            RECT rc = { rcArea.left, top, rightEdge, top + nRowH };
            AddRow(rc, nType, Items[i], (int)i);
        }

        if (m_bScrollVis)
        {
            SetRect(&m_rcScroll, rcArea.right - Sc(8), rcArea.top,
                    rcArea.right - Sc(2), rcArea.bottom);
            int trackH = m_rcScroll.bottom - m_rcScroll.top;
            int thumbH = max(Sc(24), MulDiv(trackH, areaH, m_nContentH));
            int thumbY = m_rcScroll.top;
            if (nMaxScroll > 0)
                thumbY += MulDiv(trackH - thumbH, m_nScroll, nMaxScroll);
            SetRect(&m_rcThumb, m_rcScroll.left, thumbY, m_rcScroll.right, thumbY + thumbH);
        }
        else
        {
            SetRectEmpty(&m_rcScroll);
            SetRectEmpty(&m_rcThumb);
        }
    }

    VOID LayoutRows()
    {
        m_Rows.SetCount(0);
        m_SepYs.SetCount(0);
        m_bScrollVis = FALSE;
        SetRectEmpty(&m_rcScroll);
        SetRectEmpty(&m_rcThumb);

        switch (m_nView)
        {
            case SM2V_MAIN:
            {
                int y = m_rcList.top + Sc(2);
                for (SIZE_T i = 0; i < m_Recent.GetCount(); i++)
                {
                    RECT rc = { m_rcList.left, y, m_rcList.right, y + RecentRowH() };
                    if (rc.bottom > m_rcList.bottom)
                        break;
                    AddRow(rc, SM2R_RECENT, m_Recent[i], (int)i);
                    y += RecentRowH();
                }
                AddRow(m_rcBottomRow, SM2R_ALLPROGRAMS, NULL, 0);
                break;
            }
            case SM2V_PROGRAMS:
            {
                LayoutListRows(m_TreeVis, SM2R_TREE, ListRowH(), m_rcList);
                AddRow(m_rcBottomRow, SM2R_BACK, NULL, 0);
                break;
            }
            case SM2V_SEARCH:
            {
                RECT rcArea = m_rcList;
                rcArea.bottom = m_rcSearch.top - Sc(8);
                LayoutListRows(m_Results, SM2R_RESULT, ListRowH(), rcArea);
                break;
            }
        }

        AddRow(m_rcUser, SM2R_RIGHT, NULL, SM2C_PROFILE);

        int rx = m_rcUser.left;
        int rw = m_rcUser.right;
        int y = m_rcUser.bottom + Sc(10);
        for (UINT i = 0; i < _countof(g_SM2RightDefs); i++)
        {
            if (g_SM2RightDefs[i].nCmd == SM2C_GAP)
            {
                m_SepYs.Add(y + Sc(5));
                y += Sc(12);
                continue;
            }
            RECT rc = { rx, y, rw, y + RightRowH() };
            AddRow(rc, SM2R_RIGHT, NULL, g_SM2RightDefs[i].nCmd);
            y += RightRowH();
        }

        AddRow(m_rcShut, SM2R_SHUTDOWN, NULL, 0);
        AddRow(m_rcShutArrow, SM2R_SHUTARROW, NULL, 0);
    }

    static BOOL SameRow(const HOTANIM &h, const SM2ROW &row)
    {
        return h.nType == row.nType && h.pItem == row.pItem && h.nData == row.nData;
    }

    int RowHotAlpha(int i, ULONGLONG now)
    {
        const SM2ROW &row = m_Rows[i];
        if (i == m_iSel)
            return 255;
        if (m_HotIn.bActive && SameRow(m_HotIn, row))
            return (int)(255.0 * SM2Ease((double)(now - m_HotIn.t0) / 90.0));
        if (m_HotOut.bActive && SameRow(m_HotOut, row))
        {
            double t = (double)(now - m_HotOut.t0) / 180.0;
            if (t > 1.0) t = 1.0;
            return 255 - (int)(255.0 * t);
        }
        return 0;
    }

    static VOID DrawTri(HDC hdc, int cx, int cy, int s, int dir, COLORREF clr)
    {
        POINT pts[3];
        switch (dir)
        {
            case 0:
                pts[0].x = cx - s / 2; pts[0].y = cy - s;
                pts[1].x = cx - s / 2; pts[1].y = cy + s;
                pts[2].x = cx + s;     pts[2].y = cy;
                break;
            case 1:
                pts[0].x = cx + s / 2; pts[0].y = cy - s;
                pts[1].x = cx + s / 2; pts[1].y = cy + s;
                pts[2].x = cx - s;     pts[2].y = cy;
                break;
            default:
                pts[0].x = cx - s;     pts[0].y = cy - s / 2;
                pts[1].x = cx + s;     pts[1].y = cy - s / 2;
                pts[2].x = cx;         pts[2].y = cy + s;
                break;
        }
        HBRUSH hbr = CreateSolidBrush(clr);
        HGDIOBJ hbrOld = SelectObject(hdc, hbr);
        HGDIOBJ hpenOld = SelectObject(hdc, GetStockObject(NULL_PEN));
        Polygon(hdc, pts, 3);
        SelectObject(hdc, hpenOld);
        SelectObject(hdc, hbrOld);
        DeleteObject(hbr);
    }

    VOID DrawHighlight(HDC hdc, const RECT &rc, int alpha, COLORREF base, COLORREF fill, BOOL bBorder)
    {
        if (alpha <= 0)
            return;
        HBRUSH hbr = CreateSolidBrush(SM2Mix(base, fill, alpha));
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);
        if (bBorder && alpha > 60)
        {
            HBRUSH hbrEdge = CreateSolidBrush(SM2Mix(base, SM2_CLR_HOT_BORDER, alpha));
            FrameRect(hdc, &rc, hbrEdge);
            DeleteObject(hbrEdge);
        }
    }

    VOID DrawSeparator(HDC hdc, int x1, int x2, int y, COLORREF clr)
    {
        RECT rc = { x1, y, x2, y + 1 };
        HBRUSH hbr = CreateSolidBrush(clr);
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);
    }

    VOID DrawItemText(HDC hdc, LPCWSTR psz, RECT rc, COLORREF clr)
    {
        SetTextColor(hdc, clr);
        DrawTextW(hdc, psz, -1, &rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    VOID DrawLeftPanel(HDC hdc, ULONGLONG now, BOOL bSnapshot)
    {
        SaveDC(hdc);
        IntersectClipRect(hdc, m_rcSlide.left, m_rcSlide.top, m_rcSlide.right, m_rcSlide.bottom);

        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ hFontOld = SelectObject(hdc, m_hFont);

        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            const SM2ROW &row = m_Rows[i];
            RECT rc = row.rc;
            int alpha = bSnapshot ? 0 : RowHotAlpha((int)i, now);

            switch (row.nType)
            {
                case SM2R_RECENT:
                {
                    DrawHighlight(hdc, rc, alpha, SM2_CLR_LEFT_BG, SM2_CLR_HOT_FILL, TRUE);
                    int cy = (rc.top + rc.bottom) / 2;
                    if (row.pItem->hLargeIcon)
                    {
                        DrawIconEx(hdc, rc.left + Sc(6), cy - Sc(16),
                                   row.pItem->hLargeIcon, Sc(32), Sc(32), 0, NULL, DI_NORMAL);
                    }
                    RECT rcText = { rc.left + Sc(46), rc.top, rc.right - Sc(6), rc.bottom };
                    DrawItemText(hdc, row.pItem->szName, rcText, SM2_CLR_LEFT_TEXT);
                    break;
                }
                case SM2R_TREE:
                case SM2R_RESULT:
                {
                    DrawHighlight(hdc, rc, alpha, SM2_CLR_LEFT_BG, SM2_CLR_HOT_FILL, TRUE);
                    int depth = (row.nType == SM2R_TREE && row.nData < (int)m_TreeDepth.GetCount())
                                ? m_TreeDepth[row.nData] : 0;
                    int indent = depth * Sc(14);
                    int cy = (rc.top + rc.bottom) / 2;
                    SM2ITEM *p = row.pItem;
                    if (row.nType == SM2R_TREE && p->bFolder)
                        DrawTri(hdc, rc.left + indent + Sc(9), cy, Sc(3),
                                p->bExpanded ? 2 : 0, SM2_CLR_TRI);
                    if (!p->bSmallTried)
                    {
                        p->bSmallTried = TRUE;
                        p->hSmallIcon = SM2LoadShellIcon(p->szPath, FALSE);
                    }
                    if (p->hSmallIcon)
                    {
                        DrawIconEx(hdc, rc.left + indent + Sc(17), cy - Sc(8),
                                   p->hSmallIcon, Sc(16), Sc(16), 0, NULL, DI_NORMAL);
                    }
                    RECT rcText = { rc.left + indent + Sc(38), rc.top, rc.right - Sc(4), rc.bottom };
                    DrawItemText(hdc, p->szName, rcText, SM2_CLR_LEFT_TEXT);
                    break;
                }
                case SM2R_ALLPROGRAMS:
                case SM2R_BACK:
                {
                    DrawSeparator(hdc, m_rcList.left, m_rcList.right, rc.top - Sc(3), SM2_CLR_SEP);
                    DrawHighlight(hdc, rc, alpha, SM2_CLR_LEFT_BG, SM2_CLR_HOT_FILL, TRUE);
                    int cy = (rc.top + rc.bottom) / 2;
                    DrawTri(hdc, rc.left + Sc(14), cy, Sc(4),
                            row.nType == SM2R_ALLPROGRAMS ? 0 : 1, SM2_CLR_TRI);
                    RECT rcText = { rc.left + Sc(28), rc.top, rc.right - Sc(4), rc.bottom };
                    DrawItemText(hdc,
                                 row.nType == SM2R_ALLPROGRAMS ? L"All Programs" : L"Back",
                                 rcText, SM2_CLR_LEFT_TEXT);
                    break;
                }
            }
        }

        if (m_nView == SM2V_SEARCH && m_Results.GetCount() == 0)
        {
            RECT rcText = { m_rcList.left + Sc(8), m_rcList.top + Sc(4),
                            m_rcList.right, m_rcList.top + Sc(28) };
            SetTextColor(hdc, SM2_CLR_CUE);
            DrawTextW(hdc, L"No items match your search.", -1, &rcText,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        if (m_bScrollVis && !bSnapshot)
        {
            HBRUSH hbr = CreateSolidBrush(m_bDragScroll ? SM2_CLR_SCROLL_HOT : SM2_CLR_SCROLL);
            FillRect(hdc, &m_rcThumb, hbr);
            DeleteObject(hbr);
        }

        SelectObject(hdc, hFontOld);
        RestoreDC(hdc, -1);
    }

    VOID DrawSearchBox(HDC hdc)
    {
        HBRUSH hbr = CreateSolidBrush(SM2_CLR_SEARCH_BG);
        FillRect(hdc, &m_rcSearch, hbr);
        DeleteObject(hbr);
        HBRUSH hbrEdge = CreateSolidBrush(SM2_CLR_SEARCH_BRD);
        FrameRect(hdc, &m_rcSearch, hbrEdge);
        DeleteObject(hbrEdge);

        int cx = m_rcSearch.right - Sc(17);
        int cy = (m_rcSearch.top + m_rcSearch.bottom) / 2 - Sc(2);
        HPEN hpen = CreatePen(PS_SOLID, Sc(2), SM2_CLR_CUE);
        HGDIOBJ hpenOld = SelectObject(hdc, hpen);
        HGDIOBJ hbrOld = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Ellipse(hdc, cx - Sc(4), cy - Sc(4), cx + Sc(4), cy + Sc(4));
        MoveToEx(hdc, cx + Sc(3), cy + Sc(3), NULL);
        LineTo(hdc, cx + Sc(7), cy + Sc(7));
        SelectObject(hdc, hbrOld);
        SelectObject(hdc, hpenOld);
        DeleteObject(hpen);
    }

    VOID DrawRightPanel(HDC hdc, ULONGLONG now)
    {
        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ hFontOld = SelectObject(hdc, m_hFont);

        for (SIZE_T i = 0; i < m_SepYs.GetCount(); i++)
            DrawSeparator(hdc, m_rcUser.left, m_rcUser.right, m_SepYs[i], SM2_CLR_RIGHT_SEP);

        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            const SM2ROW &row = m_Rows[i];
            RECT rc = row.rc;
            int alpha = RowHotAlpha((int)i, now);

            switch (row.nType)
            {
                case SM2R_RIGHT:
                {
                    DrawHighlight(hdc, rc, alpha, SM2_CLR_RIGHT_BG, SM2_CLR_RIGHT_HOT, FALSE);
                    RECT rcText = { rc.left + Sc(8), rc.top, rc.right - Sc(4), rc.bottom };
                    if (row.nData == SM2C_PROFILE)
                    {
                        HGDIOBJ hPrev = SelectObject(hdc, m_hFontUser);
                        DrawItemText(hdc, m_szUser, rcText, SM2_CLR_RIGHT_TEXT);
                        SelectObject(hdc, hPrev);
                    }
                    else
                    {
                        LPCWSTR psz = NULL;
                        for (UINT j = 0; j < _countof(g_SM2RightDefs); j++)
                        {
                            if (g_SM2RightDefs[j].nCmd == row.nData)
                            {
                                psz = g_SM2RightDefs[j].pszLabel;
                                break;
                            }
                        }
                        if (psz)
                            DrawItemText(hdc, psz, rcText, SM2_CLR_RIGHT_TEXT);
                    }
                    break;
                }
                case SM2R_SHUTDOWN:
                {
                    HBRUSH hbr = CreateSolidBrush(SM2Mix(SM2_CLR_SHUT_FILL, SM2_CLR_SHUT_HOT, alpha));
                    FillRect(hdc, &rc, hbr);
                    DeleteObject(hbr);
                    RECT rcText = rc;
                    SetTextColor(hdc, SM2_CLR_RIGHT_TEXT);
                    DrawTextW(hdc, L"Shut down", -1, &rcText,
                              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
                    break;
                }
                case SM2R_SHUTARROW:
                {
                    HBRUSH hbr = CreateSolidBrush(SM2Mix(SM2_CLR_SHUT_FILL, SM2_CLR_SHUT_HOT, alpha));
                    FillRect(hdc, &rc, hbr);
                    DeleteObject(hbr);
                    int cy = (rc.top + rc.bottom) / 2;
                    int cx = (rc.left + rc.right) / 2;
                    DrawTri(hdc, cx, cy, Sc(4), 0, SM2_CLR_RIGHT_TEXT);
                    break;
                }
            }
        }

        RECT rcShutAll = { m_rcShut.left, m_rcShut.top, m_rcShutArrow.right, m_rcShutArrow.bottom };
        HBRUSH hbrEdge = CreateSolidBrush(SM2_CLR_SHUT_BORDER);
        FrameRect(hdc, &rcShutAll, hbrEdge);
        RECT rcDiv = { m_rcShut.right, m_rcShut.top, m_rcShut.right + 1, m_rcShut.bottom };
        FillRect(hdc, &rcDiv, hbrEdge);
        DeleteObject(hbrEdge);

        SelectObject(hdc, hFontOld);
    }

    HBITMAP RenderLeftSnapshot()
    {
        int w = m_rcSlide.right - m_rcSlide.left;
        int h = m_rcSlide.bottom - m_rcSlide.top;
        HDC hdcScreen = ::GetDC(NULL);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
        HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

        SetWindowOrgEx(hdcMem, m_rcSlide.left, m_rcSlide.top, NULL);
        HBRUSH hbr = CreateSolidBrush(SM2_CLR_LEFT_BG);
        FillRect(hdcMem, &m_rcSlide, hbr);
        DeleteObject(hbr);
        DrawLeftPanel(hdcMem, GetTickCount64(), TRUE);

        SelectObject(hdcMem, hbmOld);
        DeleteDC(hdcMem);
        ::ReleaseDC(NULL, hdcScreen);
        return hbm;
    }

    VOID DrawViewAnim(HDC hdc, ULONGLONG now)
    {
        double t = (double)(now - m_ViewAnim.t0) / 200.0;
        double e = SM2Ease(t);
        int w = m_rcSlide.right - m_rcSlide.left;
        int h = m_rcSlide.bottom - m_rcSlide.top;
        int off = (int)(w * e);

        int oldX, newX;
        if (m_ViewAnim.dir < 0)
        {
            oldX = m_rcSlide.left - off;
            newX = m_rcSlide.left + w - off;
        }
        else
        {
            oldX = m_rcSlide.left + off;
            newX = m_rcSlide.left - w + off;
        }

        SaveDC(hdc);
        IntersectClipRect(hdc, m_rcSlide.left, m_rcSlide.top, m_rcSlide.right, m_rcSlide.bottom);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HGDIOBJ hOld = SelectObject(hdcMem, m_ViewAnim.hbmOld);
        BitBlt(hdc, oldX, m_rcSlide.top, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, m_ViewAnim.hbmNew);
        BitBlt(hdc, newX, m_rcSlide.top, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);

        RestoreDC(hdc, -1);
    }

    VOID DrawMenu(HDC hdc)
    {
        ULONGLONG now = GetTickCount64();
        RECT rcAll = { 0, 0, m_size.cx, m_size.cy };

        HBRUSH hbr = CreateSolidBrush(SM2_CLR_LEFT_BG);
        FillRect(hdc, &rcAll, hbr);
        DeleteObject(hbr);

        RECT rcRight = { LeftW(), 0, m_size.cx, m_size.cy };
        hbr = CreateSolidBrush(SM2_CLR_RIGHT_BG);
        FillRect(hdc, &rcRight, hbr);
        DeleteObject(hbr);

        if (m_ViewAnim.bActive)
            DrawViewAnim(hdc, now);
        else
            DrawLeftPanel(hdc, now, FALSE);

        DrawSearchBox(hdc);
        DrawRightPanel(hdc, now);

        HBRUSH hbrEdge = CreateSolidBrush(SM2_CLR_BORDER);
        FrameRect(hdc, &rcAll, hbrEdge);
        DeleteObject(hbrEdge);
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);
        RECT rc;
        GetClientRect(&rc);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

        DrawMenu(hdcMem);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return 1;
    }

    VOID StartAnimTimer()
    {
        SetTimer(SM2_TIMER_ANIM, 16, NULL);
    }

    VOID ClearViewAnim()
    {
        if (m_ViewAnim.hbmOld) DeleteObject(m_ViewAnim.hbmOld);
        if (m_ViewAnim.hbmNew) DeleteObject(m_ViewAnim.hbmNew);
        ZeroMemory(&m_ViewAnim, sizeof(m_ViewAnim));
    }

    VOID ClearHotAnims()
    {
        ZeroMemory(&m_HotIn, sizeof(m_HotIn));
        ZeroMemory(&m_HotOut, sizeof(m_HotOut));
        m_iHot = -1;
        m_iSel = -1;
        m_iPressed = -1;
    }

    VOID PositionUserPic(int menuX, int menuY, BYTE alpha)
    {
#if 1
        return;
#endif
        if (!m_UserPic.IsWindow())
            return;
        int pic = Sc(84);
        int rightW = m_size.cx - LeftW();
        int x = menuX + LeftW() + (rightW - pic) / 2;
        int y = menuY - pic + Sc(30);
        m_UserPic.SetWindowPos(NULL, x, y, pic, pic,
                               SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
        SetLayeredWindowAttributes(m_UserPic.m_hWnd, 0, alpha, LWA_ALPHA);
    }

    HRESULT Popup(const RECT *prcBtn, UINT uPos)
    {
        if (!IsWindow())
            return E_FAIL;
        if (IsMenuVisible())
            return S_FALSE;

        ULONGLONG now = GetTickCount64();
        if (now - m_LastDismiss < 300)
            return S_FALSE;

        m_uPosition = uPos;
        SM2SelectPalette();
        RebuildData();

        int w = MenuW(), h = MenuH();
        m_size.cx = w;
        m_size.cy = h;

        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        POINT ptRef = { prcBtn->left, prcBtn->top };
        GetMonitorInfoW(MonitorFromPoint(ptRef, MONITOR_DEFAULTTONEAREST), &mi);

        int x, y;
        m_dxSlide = 0;
        m_dySlide = 0;
        switch (uPos)
        {
            case ABE_TOP:
                x = prcBtn->left;
                y = prcBtn->bottom;
                m_dySlide = -Sc(16);
                break;
            case ABE_LEFT:
                x = prcBtn->right;
                y = prcBtn->top;
                m_dxSlide = -Sc(16);
                break;
            case ABE_RIGHT:
                x = prcBtn->left - w;
                y = prcBtn->top;
                m_dxSlide = Sc(16);
                break;
            default:
                x = prcBtn->left;
                y = prcBtn->top - h;
                m_dySlide = Sc(16);
                break;
        }

        if (x + w > mi.rcMonitor.right) x = mi.rcMonitor.right - w;
        if (x < mi.rcMonitor.left) x = mi.rcMonitor.left;
        if (y + h > mi.rcMonitor.bottom) y = mi.rcMonitor.bottom - h;
        if (y < mi.rcMonitor.top + Sc(60)) y = mi.rcMonitor.top + Sc(60);

        m_ptFinal.x = x;
        m_ptFinal.y = y;

        m_nView = SM2V_MAIN;
        m_nScroll = 0;
        ClearViewAnim();
        ClearHotAnims();
        m_bDragScroll = FALSE;

        Layout();
        ::SetWindowTextW(m_hwndEdit, L"");
        ::ShowWindow(m_hwndEdit, SW_SHOW);
        LayoutRows();

        m_AnimPhase = SM2A_OPEN;
        m_AnimT0 = now;

        SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
        SetWindowPos(HWND_TOPMOST, x + m_dxSlide, y + m_dySlide, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PositionUserPic(x + m_dxSlide, y + m_dySlide, 0);
        if (m_UserPic.IsWindow())
            m_UserPic.SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0,
                                   SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);

        ::SetForegroundWindow(m_hWnd);
        ::SetFocus(m_hwndEdit);

        InvalidateRect(NULL, FALSE);
        StartAnimTimer();
        return S_OK;
    }

    VOID Hide()
    {
        if (!IsWindow() || !IsWindowVisible())
            return;
        if (m_AnimPhase == SM2A_CLOSE)
            return;

        CloseFlyout();
        Tray_OnStartMenuDismissed(m_Tray);

        m_AnimPhase = SM2A_CLOSE;
        m_AnimT0 = GetTickCount64();
        StartAnimTimer();
    }

    VOID FinishHide()
    {
        m_AnimPhase = SM2A_NONE;
        ShowWindow(SW_HIDE);
        if (m_UserPic.IsWindow())
            m_UserPic.ShowWindow(SW_HIDE);
        ::SetWindowTextW(m_hwndEdit, L"");
        ClearViewAnim();
        ClearHotAnims();
    }

    VOID CloseFlyout()
    {
        if (m_hwndFlyout && ::IsWindow(m_hwndFlyout))
            ::DestroyWindow(m_hwndFlyout);
        m_hwndFlyout = NULL;
    }

    VOID ShowFlyout()
    {
        if (m_hwndFlyout)
            return;
        CStartMenu2Flyout *pFlyout = new CStartMenu2Flyout();
        RECT rcAnchor = m_rcShutArrow;
        ::MapWindowPoints(m_hWnd, NULL, (POINT*)&rcAnchor, 2);
        if (pFlyout->Popup(m_hWnd, m_hFont, m_iDpi, &rcAnchor))
        {
            m_hwndFlyout = pFlyout->m_hWnd;
            ::SetForegroundWindow(m_hwndFlyout);
            ::SetFocus(m_hwndFlyout);
        }
        else
            delete pFlyout;
    }

    VOID SwitchView(int nView, int dir)
    {
        if (m_ViewAnim.bActive || nView == m_nView)
            return;

        ClearViewAnim();
        HBITMAP hbmOld = RenderLeftSnapshot();

        m_nView = nView;
        m_nScroll = 0;
        ClearHotAnims();
        if (nView == SM2V_PROGRAMS)
            BuildTreeVis();
        LayoutRows();

        HBITMAP hbmNew = RenderLeftSnapshot();

        m_ViewAnim.bActive = TRUE;
        m_ViewAnim.t0 = GetTickCount64();
        m_ViewAnim.dir = dir;
        m_ViewAnim.hbmOld = hbmOld;
        m_ViewAnim.hbmNew = hbmNew;

        InvalidateRect(NULL, FALSE);
        StartAnimTimer();
    }

    VOID SetScroll(int nScroll)
    {
        int areaH = m_rcList.bottom - m_rcList.top;
        if (m_nView == SM2V_SEARCH)
            areaH = (m_rcSearch.top - Sc(8)) - m_rcList.top;
        int nMax = m_nContentH > areaH ? m_nContentH - areaH : 0;
        if (nScroll > nMax) nScroll = nMax;
        if (nScroll < 0) nScroll = 0;
        if (nScroll == m_nScroll)
            return;
        m_nScroll = nScroll;
        LayoutRows();
        InvalidateRect(NULL, FALSE);
    }

    int HitTest(POINT pt)
    {
        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            int nType = m_Rows[i].nType;
            if (m_ViewAnim.bActive &&
                (nType == SM2R_RECENT || nType == SM2R_TREE || nType == SM2R_RESULT ||
                 nType == SM2R_ALLPROGRAMS || nType == SM2R_BACK))
                continue;
            if (PtInRect(&m_Rows[i].rc, pt))
                return (int)i;
        }
        return -1;
    }

    VOID UpdateHot(int i)
    {
        if (i == m_iHot)
            return;

        ULONGLONG now = GetTickCount64();
        if (m_iHot >= 0 && m_iHot < (int)m_Rows.GetCount())
        {
            m_HotOut.bActive = TRUE;
            m_HotOut.t0 = now;
            m_HotOut.nType = m_Rows[m_iHot].nType;
            m_HotOut.pItem = m_Rows[m_iHot].pItem;
            m_HotOut.nData = m_Rows[m_iHot].nData;
        }
        if (i >= 0 && i < (int)m_Rows.GetCount())
        {
            m_HotIn.bActive = TRUE;
            m_HotIn.t0 = now;
            m_HotIn.nType = m_Rows[i].nType;
            m_HotIn.pItem = m_Rows[i].pItem;
            m_HotIn.nData = m_Rows[i].nData;
        }
        else
        {
            m_HotIn.bActive = FALSE;
        }

        m_iHot = i;
        m_iSel = -1;
        InvalidateRect(NULL, FALSE);
        StartAnimTimer();
    }

    VOID ActivateRow(int i)
    {
        if (i < 0 || i >= (int)m_Rows.GetCount())
            return;

        SM2ROW row = m_Rows[i];
        switch (row.nType)
        {
            case SM2R_RECENT:
            case SM2R_RESULT:
                SM2LaunchItem(row.pItem);
                Hide();
                break;
            case SM2R_TREE:
                if (row.pItem->bFolder)
                {
                    row.pItem->bExpanded = !row.pItem->bExpanded;
                    BuildTreeVis();
                    ClearHotAnims();
                    LayoutRows();
                    InvalidateRect(NULL, FALSE);
                }
                else
                {
                    SM2LaunchItem(row.pItem);
                    Hide();
                }
                break;
            case SM2R_ALLPROGRAMS:
                SwitchView(SM2V_PROGRAMS, -1);
                break;
            case SM2R_BACK:
                SwitchView(SM2V_MAIN, 1);
                break;
            case SM2R_RIGHT:
                SM2ExecRightCmd(row.nData);
                Hide();
                break;
            case SM2R_SHUTDOWN:
                Hide();
                FinishHide();
                SM2DoShutdownCmd(m_hwndTray, SM2F_SHUTDOWN);
                break;
            case SM2R_SHUTARROW:
                ShowFlyout();
                break;
        }
    }

    BOOL IsContextRow(int i) const
    {
        if (i < 0 || i >= (int)m_Rows.GetCount() || !m_Rows[i].pItem)
            return FALSE;
        return m_Rows[i].nType == SM2R_RECENT ||
               m_Rows[i].nType == SM2R_RESULT ||
               m_Rows[i].nType == SM2R_TREE;
    }

    VOID RemoveContextVerb(IContextMenu *pContextMenu, HMENU hMenu,
                           UINT cCommands, PCWSTR pszVerb)
    {
        for (UINT i = 0; i < cCommands; ++i)
        {
            WCHAR szVerb[64] = L"";
            if (SUCCEEDED(pContextMenu->GetCommandString(i, GCS_VERBW, NULL,
                                                         (LPSTR)szVerb,
                                                         _countof(szVerb))) &&
                !lstrcmpiW(szVerb, pszVerb))
            {
                DeleteMenu(hMenu, SM2_CONTEXT_FIRST + i, MF_BYCOMMAND);
            }
        }
    }

    VOID CleanContextSeparators(HMENU hMenu)
    {
        BOOL bPreviousSeparator = TRUE;
        for (INT i = 0; i < GetMenuItemCount(hMenu);)
        {
            MENUITEMINFOW mii = { sizeof(mii), MIIM_FTYPE };
            if (!GetMenuItemInfoW(hMenu, i, TRUE, &mii))
                break;
            if (mii.fType & MFT_SEPARATOR)
            {
                if (bPreviousSeparator)
                {
                    DeleteMenu(hMenu, i, MF_BYPOSITION);
                    continue;
                }
                bPreviousSeparator = TRUE;
            }
            else
            {
                bPreviousSeparator = FALSE;
            }
            ++i;
        }

        INT count = GetMenuItemCount(hMenu);
        if (count > 0)
        {
            MENUITEMINFOW mii = { sizeof(mii), MIIM_FTYPE };
            if (GetMenuItemInfoW(hMenu, count - 1, TRUE, &mii) &&
                (mii.fType & MFT_SEPARATOR))
            {
                DeleteMenu(hMenu, count - 1, MF_BYPOSITION);
            }
        }
    }

    VOID RemoveRecentItem(SM2ITEM *pItem)
    {
        if (!pItem)
            return;

        SHDeleteValueW(HKEY_CURRENT_USER, SM2_RECENT_LINK_KEY, pItem->szPath);
        if (SM2GetTargetPath(pItem))
        {
            SHDeleteValueW(HKEY_CURRENT_USER, SM2_RECENT_EXE_KEY, pItem->szTargetPath);
            for (SIZE_T i = 0; i < m_Flat.GetCount(); ++i)
            {
                if (SM2GetTargetPath(m_Flat[i]) && !m_Flat[i]->szTargetPath.CompareNoCase(pItem->szTargetPath))
                    SHDeleteValueW(HKEY_CURRENT_USER, SM2_RECENT_LINK_KEY, m_Flat[i]->szPath);
            }
        }

        BuildRecent();
        ClearHotAnims();
        m_iHot = m_iSel = -1;
        LayoutRows();
        InvalidateRect(NULL, FALSE);
    }

    static INT_PTR CALLBACK RenameDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        PWSTR pszName = (PWSTR)::GetWindowLongPtrW(hwnd, DWLP_USER);
        if (msg == WM_INITDIALOG)
        {
            pszName = (PWSTR)lParam;
            ::SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
            ::SendDlgItemMessageW(hwnd, IDC_STARTMENU_NAME, EM_SETLIMITTEXT, MAX_PATH - 1, 0);
            ::SetDlgItemTextW(hwnd, IDC_STARTMENU_NAME, pszName);
            ::SendDlgItemMessageW(hwnd, IDC_STARTMENU_NAME, EM_SETSEL, 0, -1);
            return TRUE;
        }
        if (msg == WM_COMMAND)
        {
            if (LOWORD(wParam) == IDOK && pszName && ::GetDlgItemTextW(hwnd, IDC_STARTMENU_NAME, pszName, MAX_PATH))
                ::EndDialog(hwnd, IDOK);
            else if (LOWORD(wParam) == IDCANCEL)
                ::EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }

    HRESULT RenameContextItem(IShellFolder *pParent, PCUITEMID_CHILD pidlChild, PCWSTR pszOldPath)
    {
        STRRET Name;
        WCHAR szName[MAX_PATH];
        HRESULT hr = pParent->GetDisplayNameOf(pidlChild, SHGDN_INFOLDER | SHGDN_FOREDITING, &Name);
        if (FAILED(hr))
            return hr;
        hr = StrRetToBufW(&Name, pidlChild, szName, _countof(szName));
        if (FAILED(hr))
            return hr;
        if (DialogBoxParamW(hExplorerInstance, MAKEINTRESOURCEW(IDD_STARTMENU_RENAME), m_hWnd, RenameDialogProc, (LPARAM)szName) != IDOK)
            return S_FALSE;

        PITEMID_CHILD pidlNew = NULL;
        hr = pParent->SetNameOf(m_hWnd, pidlChild, szName, SHGDN_INFOLDER | SHGDN_FOREDITING, &pidlNew);
        if (SUCCEEDED(hr) && pidlNew)
        {
            WCHAR szNewPath[MAX_PATH];
            if (SUCCEEDED(pParent->GetDisplayNameOf(pidlNew, SHGDN_FORPARSING, &Name)) && SUCCEEDED(StrRetToBufW(&Name, pidlNew, szNewPath, _countof(szNewPath))))
            {
                ULONGLONG LastUsed = SM2GetLastUsed(SM2_RECENT_LINK_KEY, pszOldPath);
                if (LastUsed && lstrcmpiW(szNewPath, pszOldPath) && SHSetValueW(HKEY_CURRENT_USER, SM2_RECENT_LINK_KEY, szNewPath, REG_QWORD, &LastUsed, sizeof(LastUsed)) == ERROR_SUCCESS)
                    SHDeleteValueW(HKEY_CURRENT_USER, SM2_RECENT_LINK_KEY, pszOldPath);
            }
        }
        ILFree(pidlNew);
        if (FAILED(hr))
        {
            WCHAR szError[512];
            if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, HRESULT_CODE(hr), 0, szError, _countof(szError), NULL))
                StringCchCopyW(szError, _countof(szError), L"The item could not be renamed.");
            MessageBoxW(szError, L"Rename", MB_OK | MB_ICONERROR);
        }
        return hr;
    }

    VOID ShowItemContextMenu(int iRow, POINT ptScreen)
    {
        if (!IsContextRow(iRow))
            return;

        SM2ROW Row = m_Rows[iRow];
        CStringW ItemPath = Row.pItem->szPath;
        PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(Row.pItem->szPath);
        if (!pidl)
            return;

        CComPtr<IShellFolder> Parent;
        PCUITEMID_CHILD pidlChild = NULL;
        HRESULT hr = SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &Parent),
                                    &pidlChild);
        if (FAILED(hr))
        {
            ILFree(pidl);
            return;
        }

        CComPtr<IContextMenu> ContextMenu;
        hr = Parent->GetUIObjectOf(m_hWnd, 1, &pidlChild,
                                   IID_NULL_PPV_ARG(IContextMenu, &ContextMenu));
        if (FAILED(hr))
        {
            ILFree(pidl);
            return;
        }

        HMENU hMenu = CreatePopupMenu();
        if (!hMenu)
        {
            ILFree(pidl);
            return;
        }

        UINT uFlags = CMF_NORMAL | CMF_CANRENAME;
        if (GetKeyState(VK_SHIFT) & 0x8000)
            uFlags |= CMF_EXTENDEDVERBS;
        hr = ContextMenu->QueryContextMenu(hMenu, 0, SM2_CONTEXT_FIRST,
                                           SM2_CONTEXT_LAST, uFlags);
        UINT cCommands = SUCCEEDED(hr) ? HRESULT_CODE(hr) : 0;
        if (SUCCEEDED(hr) && Row.nType == SM2R_RECENT)
        {
            RemoveContextVerb(ContextMenu, hMenu, cCommands, L"cut");
            RemoveContextVerb(ContextMenu, hMenu, cCommands, L"delete");
            RemoveContextVerb(ContextMenu, hMenu, cCommands, L"link");
            RemoveContextVerb(ContextMenu, hMenu, cCommands, L"createlink");
            if (lstrcmpiW(PathFindExtensionW(ItemPath), L".lnk"))
                RemoveContextVerb(ContextMenu, hMenu, cCommands, L"rename");
            CleanContextSeparators(hMenu);
            if (GetMenuItemCount(hMenu) > 0)
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, SM2_CONTEXT_REMOVE_RECENT,
                        L"Remove from this list");
        }

        if (SUCCEEDED(hr) && GetMenuItemCount(hMenu) > 0)
        {
            m_ContextMenu2.Release();
            m_ContextMenu3.Release();
            ContextMenu->QueryInterface(IID_PPV_ARG(IContextMenu3, &m_ContextMenu3));
            if (!m_ContextMenu3)
                ContextMenu->QueryInterface(IID_PPV_ARG(IContextMenu2, &m_ContextMenu2));

            m_bInContextMenu = TRUE;
            SetForegroundWindow(m_hWnd);
            UINT idCommand = TrackPopupMenuEx(hMenu,
                                              TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                              ptScreen.x, ptScreen.y,
                                              m_hWnd, NULL);
            m_ContextMenu3.Release();
            m_ContextMenu2.Release();
            PostMessageW(WM_NULL, 0, 0);

            if (idCommand == SM2_CONTEXT_REMOVE_RECENT)
            {
                RemoveRecentItem(Row.pItem);
            }
            else if (idCommand >= SM2_CONTEXT_FIRST &&
                     idCommand < SM2_CONTEXT_FIRST + cCommands)
            {
                WCHAR szVerb[64] = L"";
                ContextMenu->GetCommandString(idCommand - SM2_CONTEXT_FIRST, GCS_VERBW, NULL, (LPSTR)szVerb, _countof(szVerb));
                BOOL bLaunch = !Row.pItem->bFolder && (!lstrcmpiW(szVerb, L"open") || !lstrcmpiW(szVerb, L"runas") || !lstrcmpiW(szVerb, L"runasuser"));
                BOOL bRename = !lstrcmpiW(szVerb, L"rename");
                BOOL bKeepOpen = bRename || !lstrcmpiW(szVerb, L"taskbarpin") || !lstrcmpiW(szVerb, L"taskbarunpin") || !lstrcmpiW(szVerb, L"copy");
                if (!bKeepOpen)
                    Hide();
                CMINVOKECOMMANDINFOEX Info = { sizeof(Info) };
                Info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                if (GetKeyState(VK_SHIFT) & 0x8000)
                    Info.fMask |= CMIC_MASK_SHIFT_DOWN;
                if (GetKeyState(VK_CONTROL) & 0x8000)
                    Info.fMask |= CMIC_MASK_CONTROL_DOWN;
                Info.hwnd = m_hWnd;
                Info.lpVerb = MAKEINTRESOURCEA(idCommand - SM2_CONTEXT_FIRST);
                Info.lpVerbW = MAKEINTRESOURCEW(idCommand - SM2_CONTEXT_FIRST);
                Info.nShow = SW_SHOWNORMAL;
                Info.ptInvoke = ptScreen;
                if (bRename)
                    hr = RenameContextItem(Parent, pidlChild, ItemPath);
                else if (!lstrcmpiW(szVerb, L"opencontaining"))
                    hr = SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
                else
                    hr = ContextMenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&Info);
                if (bLaunch && SUCCEEDED(hr))
                    SM2SetLastUsed(SM2_RECENT_LINK_KEY, ItemPath);

                RebuildData();
                UpdateSearch();
                LayoutRows();
                InvalidateRect(NULL, FALSE);
            }
            m_bInContextMenu = FALSE;
            if (IsMenuVisible() && ::GetForegroundWindow() == m_hWnd)
            {
                ::SetFocus(m_hwndEdit);
            }
        }

        DestroyMenu(hMenu);
        ILFree(pidl);
    }

    VOID ShowKeyboardContextMenu()
    {
        int iRow = IsContextRow(m_iSel) ? m_iSel : m_iHot;
        if (!IsContextRow(iRow))
        {
            for (SIZE_T i = 0; i < m_Rows.GetCount(); ++i)
            {
                if (IsContextRow((int)i))
                {
                    iRow = (int)i;
                    break;
                }
            }
        }
        if (!IsContextRow(iRow))
            return;

        m_iSel = iRow;
        m_iHot = -1;
        InvalidateRect(NULL, FALSE);
        RECT rc = m_Rows[iRow].rc;
        POINT pt = { rc.left + Sc(24), rc.bottom };
        ClientToScreen(&pt);
        ShowItemContextMenu(iRow, pt);
    }

    VOID MoveSel(int dir)
    {
        int count = (int)m_Rows.GetCount();
        if (count == 0)
            return;

        int start = (m_iSel >= 0) ? m_iSel : ((m_iHot >= 0) ? m_iHot : (dir > 0 ? -1 : 0));
        for (int step = 1; step <= count; step++)
        {
            int i = start + dir * step;
            if (i < 0) i += count * ((-i) / count + 1);
            i %= count;
            m_iSel = i;
            break;
        }
        m_iHot = -1;
        m_HotIn.bActive = FALSE;
        InvalidateRect(NULL, FALSE);
    }

    VOID OnEditKey(UINT vk)
    {
        switch (vk)
        {
            case VK_ESCAPE:
                if (::GetWindowTextLengthW(m_hwndEdit) > 0)
                    ::SetWindowTextW(m_hwndEdit, L"");
                else if (m_nView == SM2V_PROGRAMS)
                    SwitchView(SM2V_MAIN, 1);
                else
                    Hide();
                break;
            case VK_RETURN:
                if (m_iSel >= 0)
                    ActivateRow(m_iSel);
                else if (m_nView == SM2V_SEARCH)
                {
                    for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
                    {
                        if (m_Rows[i].nType == SM2R_RESULT)
                        {
                            ActivateRow((int)i);
                            break;
                        }
                    }
                }
                break;
            case VK_UP:
                MoveSel(-1);
                break;
            case VK_DOWN:
                MoveSel(1);
                break;
            case VK_APPS:
            case VK_F10:
                ShowKeyboardContextMenu();
                break;
        }
    }

    VOID UpdateSearch()
    {
        WCHAR szText[256];
        ::GetWindowTextW(m_hwndEdit, szText, _countof(szText));

        if (!szText[0])
        {
            if (m_nView == SM2V_SEARCH)
            {
                m_nView = SM2V_MAIN;
                m_nScroll = 0;
                ClearViewAnim();
                ClearHotAnims();
                LayoutRows();
                InvalidateRect(NULL, FALSE);
            }
            return;
        }

        BuildResults(szText);
        if (m_nView != SM2V_SEARCH)
        {
            m_nView = SM2V_SEARCH;
            ClearViewAnim();
            ClearHotAnims();
        }
        m_nScroll = 0;
        LayoutRows();
        InvalidateRect(NULL, FALSE);
    }

    static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                             LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef)
    {
        CStartMenu2Wnd *pThis = (CStartMenu2Wnd*)dwRef;

        switch (uMsg)
        {
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE || wParam == VK_RETURN ||
                    wParam == VK_UP || wParam == VK_DOWN ||
                    wParam == VK_APPS ||
                    (wParam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000)))
                {
                    pThis->OnEditKey((UINT)wParam);
                    return 0;
                }
                break;
            case WM_CHAR:
                if (wParam == L'\r' || wParam == 27)
                    return 0;
                break;
            case WM_MOUSEWHEEL:
                return ::SendMessageW(pThis->m_hWnd, uMsg, wParam, lParam);
            case WM_PAINT:
            {
                LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
                if (::GetWindowTextLengthW(hWnd) == 0)
                {
                    HDC hdc = ::GetDC(hWnd);
                    if (hdc)
                    {
                        RECT rc;
                        ::GetClientRect(hWnd, &rc);
                        SetBkMode(hdc, TRANSPARENT);
                        SetTextColor(hdc, SM2_CLR_CUE);
                        HGDIOBJ hOld = SelectObject(hdc, pThis->m_hFontCue);
                        DrawTextW(hdc, L"Search programs and files", -1, &rc,
                                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                        SelectObject(hdc, hOld);
                        ::ReleaseDC(hWnd, hdc);
                    }
                }
                return lr;
            }
        }
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        RefreshLiquidMaterial();
        NONCLIENTMETRICSW ncm;
        ncm.cbSize = sizeof(ncm);
        LOGFONTW lf;
        ZeroMemory(&lf, sizeof(lf));
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            lf = ncm.lfMenuFont;

        StringCchCopyW(lf.lfFaceName, _countof(lf.lfFaceName), L"Segoe UI");
        lf.lfHeight = -Sc(12);
        lf.lfWeight = FW_NORMAL;
        lf.lfItalic = FALSE;
        lf.lfQuality = ANTIALIASED_QUALITY;
        m_hFont = CreateFontIndirectW(&lf);

        lf.lfItalic = TRUE;
        m_hFontCue = CreateFontIndirectW(&lf);

        lf.lfItalic = FALSE;
        lf.lfHeight = -Sc(13);
        m_hFontUser = CreateFontIndirectW(&lf);

        if (!GetCurrentLoggedOnUserName(m_szUser, _countof(m_szUser)))
            StringCchCopyW(m_szUser, _countof(m_szUser), L"User");

        m_hwndEdit = CreateWindowExW(0, L"EDIT", L"",
                                     WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
                                     0, 0, 10, 10,
                                     m_hWnd, (HMENU)(INT_PTR)SM2_IDC_SEARCH,
                                     hExplorerInstance, NULL);
        if (m_hwndEdit)
        {
            ::SendMessageW(m_hwndEdit, WM_SETFONT, (WPARAM)m_hFont, FALSE);
            SetWindowSubclass(m_hwndEdit, EditSubclassProc, 1, (DWORD_PTR)this);
        }

#if 0
        m_UserPic.m_iDpi = m_iDpi;
        m_UserPic.Create(m_hWnd, CWindow::rcDefault, NULL);
#endif

        return 0;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        KillTimer(SM2_TIMER_ANIM);
        CloseFlyout();
        if (m_hwndEdit)
        {
            RemoveWindowSubclass(m_hwndEdit, EditSubclassProc, 1);
            m_hwndEdit = NULL;
        }
        if (m_UserPic.IsWindow())
            m_UserPic.DestroyWindow();
        ClearViewAnim();
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = NULL; }
        if (m_hFontCue) { DeleteObject(m_hFontCue); m_hFontCue = NULL; }
        if (m_hFontUser) { DeleteObject(m_hFontUser); m_hFontUser = NULL; }
        if (m_hLiquidTheme)
        {
            CloseThemeData(m_hLiquidTheme);
            m_hLiquidTheme = NULL;
        }
        m_Root.Clear();
        ClearRecentExecutables();
        return 0;
    }

    LRESULT OnThemeChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        RefreshLiquidMaterial();
        InvalidateRect(NULL, TRUE);
        return 0;
    }

    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam != SM2_TIMER_ANIM)
            return 0;

        ULONGLONG now = GetTickCount64();
        BOOL bActive = FALSE;

        if (m_AnimPhase == SM2A_OPEN)
        {
            double t = (double)(now - m_AnimT0) / 170.0;
            double e = SM2Ease(t);
            BYTE alpha = (BYTE)(255.0 * e);
            int x = m_ptFinal.x + (int)(m_dxSlide * (1.0 - e));
            int y = m_ptFinal.y + (int)(m_dySlide * (1.0 - e));
            SetLayeredWindowAttributes(m_hWnd, 0, alpha, LWA_ALPHA);
            SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            PositionUserPic(x, y, alpha);
            if (t >= 1.0)
                m_AnimPhase = SM2A_NONE;
            else
                bActive = TRUE;
        }
        else if (m_AnimPhase == SM2A_CLOSE)
        {
            double t = (double)(now - m_AnimT0) / 170.0;
            if (t > 1.0) t = 1.0;
            double e = SM2Ease(1.0 - t);
            BYTE alpha = (BYTE)(255.0 * e);
            int x = m_ptFinal.x + (int)(m_dxSlide * (1.0 - e));
            int y = m_ptFinal.y + (int)(m_dySlide * (1.0 - e));
            SetLayeredWindowAttributes(m_hWnd, 0, alpha, LWA_ALPHA);
            SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            PositionUserPic(x, y, alpha);
            if (t >= 1.0)
                FinishHide();
            else
                bActive = TRUE;
        }

        if (m_HotIn.bActive || m_HotOut.bActive)
        {
            if (m_HotOut.bActive && now - m_HotOut.t0 > 200)
                m_HotOut.bActive = FALSE;
            if (m_HotIn.bActive && now - m_HotIn.t0 > 120)
            {
                if (!m_HotOut.bActive)
                    m_HotIn.t0 = now - 1000;
                else
                    bActive = TRUE;
            }
            else if (m_HotIn.bActive)
                bActive = TRUE;
            if (m_HotOut.bActive)
                bActive = TRUE;
            InvalidateRect(NULL, FALSE);
        }

        if (m_ViewAnim.bActive)
        {
            if (now - m_ViewAnim.t0 >= 200)
            {
                ClearViewAnim();
            }
            else
                bActive = TRUE;
            InvalidateRect(NULL, FALSE);
        }

        if (!bActive)
            KillTimer(SM2_TIMER_ANIM);

        return 0;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (m_bDragScroll)
        {
            int trackH = (m_rcScroll.bottom - m_rcScroll.top) - (m_rcThumb.bottom - m_rcThumb.top);
            int areaH = m_rcList.bottom - m_rcList.top;
            if (m_nView == SM2V_SEARCH)
                areaH = (m_rcSearch.top - Sc(8)) - m_rcList.top;
            int nMax = m_nContentH > areaH ? m_nContentH - areaH : 0;
            if (trackH > 0 && nMax > 0)
            {
                int y = pt.y - m_nDragOff - m_rcScroll.top;
                SetScroll(MulDiv(nMax, y, trackH));
            }
            return 0;
        }

        UpdateHot(HitTest(pt));

        if (!m_bTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
            TrackMouseEvent(&tme);
            m_bTracking = TRUE;
        }
        return 0;
    }

    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_bTracking = FALSE;
        if (!m_bDragScroll)
            UpdateHot(-1);
        return 0;
    }

    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (m_bScrollVis && PtInRect(&m_rcThumb, pt))
        {
            m_bDragScroll = TRUE;
            m_nDragOff = pt.y - m_rcThumb.top;
            SetCapture();
            InvalidateRect(NULL, FALSE);
            return 0;
        }
        if (m_bScrollVis && PtInRect(&m_rcScroll, pt))
        {
            int areaH = m_rcList.bottom - m_rcList.top;
            if (m_nView == SM2V_SEARCH)
                areaH = (m_rcSearch.top - Sc(8)) - m_rcList.top;
            SetScroll(m_nScroll + (pt.y < m_rcThumb.top ? -areaH : areaH));
            return 0;
        }

        m_iPressed = HitTest(pt);
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (m_bDragScroll)
        {
            m_bDragScroll = FALSE;
            ReleaseCapture();
            InvalidateRect(NULL, FALSE);
            return 0;
        }

        int i = HitTest(pt);
        if (i >= 0 && i == m_iPressed)
            ActivateRow(i);
        m_iPressed = -1;
        return 0;
    }

    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (m_nView == SM2V_PROGRAMS || m_nView == SM2V_SEARCH)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            SetScroll(m_nScroll - MulDiv(delta, ListRowH() * 3, WHEEL_DELTA));
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(&pt);
            UpdateHot(HitTest(pt));
        }
        return 0;
    }

    LRESULT OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            if (m_bInContextMenu)
                return 0;
            HWND hwndOther = (HWND)lParam;
            if (m_hwndFlyout && hwndOther == m_hwndFlyout)
                return 0;
            if (IsMenuVisible())
            {
                m_LastDismiss = GetTickCount64();
                Hide();
            }
        }
        return 0;
    }

    LRESULT OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if ((HWND)wParam == m_hwndEdit)
        {
            bHandled = FALSE;
            return 0;
        }

        if ((SHORT)LOWORD(lParam) == -1 && (SHORT)HIWORD(lParam) == -1)
        {
            ShowKeyboardContextMenu();
            return 0;
        }

        POINT ptScreen = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        POINT ptClient = ptScreen;
        ScreenToClient(&ptClient);
        int iRow = HitTest(ptClient);
        if (IsContextRow(iRow))
        {
            m_iSel = iRow;
            m_iHot = -1;
            InvalidateRect(NULL, FALSE);
            ShowItemContextMenu(iRow, ptScreen);
        }
        return 0;
    }

    LRESULT OnContextMenuMessage(UINT uMsg, WPARAM wParam, LPARAM lParam,
                                 BOOL &bHandled)
    {
        if (m_ContextMenu3)
        {
            LRESULT lResult = 0;
            HRESULT hr = m_ContextMenu3->HandleMenuMsg2(uMsg, wParam, lParam,
                                                        &lResult);
            bHandled = SUCCEEDED(hr);
            return lResult;
        }
        if (m_ContextMenu2)
        {
            HRESULT hr = m_ContextMenu2->HandleMenuMsg(uMsg, wParam, lParam);
            bHandled = SUCCEEDED(hr);
            return 0;
        }
        bHandled = FALSE;
        return 0;
    }

    LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (m_hwndEdit)
            ::SetFocus(m_hwndEdit);
        return 0;
    }

    LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (LOWORD(wParam) == SM2_IDC_SEARCH && HIWORD(wParam) == EN_CHANGE)
            UpdateSearch();
        return 0;
    }

    LRESULT OnCtlColorEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        HDC hdc = (HDC)wParam;
        if (!g_SM2EditBrush)
            SM2SelectPalette();
        SetTextColor(hdc, SM2_CLR_LEFT_TEXT);
        SetBkColor(hdc, SM2_CLR_SEARCH_BG);
        return (LRESULT)g_SM2EditBrush;
    }

    LRESULT OnFlyoutCmd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_hwndFlyout = NULL;
        Hide();
        FinishHide();
        SM2DoShutdownCmd(m_hwndTray, (int)wParam);
        return 0;
    }

    LRESULT OnFlyoutGone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_hwndFlyout = NULL;
        if (wParam)
        {
            m_LastDismiss = GetTickCount64();
            Hide();
        }
        else if (IsMenuVisible())
        {
            ::SetForegroundWindow(m_hWnd);
            ::SetFocus(m_hwndEdit);
        }
        return 0;
    }

    LRESULT OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        Hide();
        return 0;
    }

    BEGIN_MSG_MAP(CStartMenu2Wnd)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_THEMECHANGED, OnThemeChanged)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
        MESSAGE_HANDLER(WM_INITMENUPOPUP, OnContextMenuMessage)
        MESSAGE_HANDLER(WM_DRAWITEM, OnContextMenuMessage)
        MESSAGE_HANDLER(WM_MEASUREITEM, OnContextMenuMessage)
        MESSAGE_HANDLER(WM_MENUCHAR, OnContextMenuMessage)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
        MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(SM2M_FLYOUTCMD, OnFlyoutCmd)
        MESSAGE_HANDLER(SM2M_FLYOUTGONE, OnFlyoutGone)
    END_MSG_MAP()
};

static CStartMenu2Wnd *g_pStartMenu2 = NULL;

HRESULT StartMenu2_Create(IN ITrayWindow *Tray, IN HWND hwndTray)
{
    if (g_pStartMenu2)
        return S_FALSE;

    CStartMenu2Wnd *pMenu = new CStartMenu2Wnd();
    if (!pMenu)
        return E_OUTOFMEMORY;

    if (!pMenu->Init(Tray, hwndTray))
    {
        delete pMenu;
        return E_FAIL;
    }

    g_pStartMenu2 = pMenu;
    return S_OK;
}

HRESULT StartMenu2_Popup(IN const RECT *prcStartBtn, IN UINT uPosition)
{
    if (!g_pStartMenu2)
        return E_FAIL;
    return g_pStartMenu2->Popup(prcStartBtn, uPosition);
}

VOID StartMenu2_Hide(VOID)
{
    if (g_pStartMenu2)
        g_pStartMenu2->Hide();
}

BOOL StartMenu2_IsVisible(VOID)
{
    return g_pStartMenu2 && g_pStartMenu2->IsMenuVisible();
}

VOID StartMenu2_RecordUsage(IN HWND hWnd)
{
    DWORD dwProcessId;
    WCHAR szPath[MAX_PATH];
    DWORD cchPath = _countof(szPath);

    if (!hWnd || !GetWindowThreadProcessId(hWnd, &dwProcessId))
        return;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
    if (!hProcess)
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwProcessId);
    if (!hProcess)
        return;

    szPath[0] = 0;
    if (QueryFullProcessImageNameW(hProcess, 0, szPath, &cchPath))
    {
        CStringW Path;
        if (SM2NormalizePath(szPath, Path))
            SM2SetLastUsed(SM2_RECENT_EXE_KEY, Path);
    }
    CloseHandle(hProcess);
}

VOID StartMenu2_Destroy(VOID)
{
    if (g_pStartMenu2)
    {
        g_pStartMenu2->Term();
        delete g_pStartMenu2;
        g_pStartMenu2 = NULL;
    }
    if (g_SM2EditBrush)
    {
        DeleteObject(g_SM2EditBrush);
        g_SM2EditBrush = NULL;
    }
}
