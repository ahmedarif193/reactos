/*
 * PROJECT:     ReactOS Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Modern This PC and search views
 */

#include "frame.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer11);

typedef struct _E11_SEARCHJOB
{
    HWND hwndNotify;
    LONG lGen;
    WCHAR szRoot[MAX_PATH];
    WCHAR szQuery[128];
    E11_SEARCHROW *pRows;
    int cRows;
} E11_SEARCHJOB;

#define E11_SEARCH_MAX      400
#define E11_SEARCH_DEPTH    8
#define E11_SEARCH_MS       8000

static VOID
E11SearchDir(E11_SEARCHJOB *pJob, LPCWSTR pszDir, int nDepth, ULONGLONG tDeadline)
{
    WCHAR szPattern[MAX_PATH];
    WIN32_FIND_DATAW wfd;
    HANDLE hFind;

    if (nDepth > E11_SEARCH_DEPTH || pJob->cRows >= E11_SEARCH_MAX ||
        GetTickCount64() > tDeadline)
        return;

    StringCchPrintfW(szPattern, _countof(szPattern), L"%s\\*", pszDir);
    hFind = FindFirstFileW(szPattern, &wfd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (!wcscmp(wfd.cFileName, L".") || !wcscmp(wfd.cFileName, L".."))
            continue;

        BOOL bFolder = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (StrStrIW(wfd.cFileName, pJob->szQuery))
        {
            if (pJob->cRows < E11_SEARCH_MAX)
            {
                E11_SEARCHROW *pRow = &pJob->pRows[pJob->cRows++];
                ZeroMemory(pRow, sizeof(*pRow));
                StringCchCopyW(pRow->szName, _countof(pRow->szName), wfd.cFileName);
                StringCchCopyW(pRow->szDir, _countof(pRow->szDir), pszDir);
                pRow->bFolder = bFolder;
            }
        }

        if (bFolder && pJob->cRows < E11_SEARCH_MAX)
        {
            WCHAR szSub[MAX_PATH];
            if (SUCCEEDED(StringCchPrintfW(szSub, _countof(szSub), L"%s\\%s",
                                           pszDir, wfd.cFileName)))
                E11SearchDir(pJob, szSub, nDepth + 1, tDeadline);
        }
    } while (pJob->cRows < E11_SEARCH_MAX &&
             GetTickCount64() <= tDeadline &&
             FindNextFileW(hFind, &wfd));

    FindClose(hFind);
}

static DWORD WINAPI
E11SearchThread(LPVOID pvParam)
{
    E11_SEARCHJOB *pJob = (E11_SEARCHJOB *)pvParam;
    WCHAR szRoot[MAX_PATH];

    StringCchCopyW(szRoot, _countof(szRoot), pJob->szRoot);
    PathRemoveBackslashW(szRoot);

    E11SearchDir(pJob, szRoot, 0, GetTickCount64() + E11_SEARCH_MS);

    if (!SendMessageW(pJob->hwndNotify, E11M_SEARCHDONE, (WPARAM)pJob->lGen, (LPARAM)pJob))
    {
        HeapFree(GetProcessHeap(), 0, pJob->pRows);
        HeapFree(GetProcessHeap(), 0, pJob);
    }
    return 0;
}

VOID CThisPCView::StartSearch(LPCWSTR pszRoot, LPCWSTR pszQuery)
{
    E11_SEARCHJOB *pJob;

    StopSearch();

    m_bSearchMode = TRUE;
    m_bSearching = TRUE;
    m_Results.SetCount(0);
    m_iHot = m_iSelected = -1;
    StringCchCopyW(m_szQuery, _countof(m_szQuery), pszQuery);

    pJob = (E11_SEARCHJOB *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pJob));
    if (!pJob)
    {
        m_bSearching = FALSE;
        return;
    }
    pJob->pRows = (E11_SEARCHROW *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                             E11_SEARCH_MAX * sizeof(E11_SEARCHROW));
    if (!pJob->pRows)
    {
        HeapFree(GetProcessHeap(), 0, pJob);
        m_bSearching = FALSE;
        return;
    }

    pJob->hwndNotify = m_hWnd;
    pJob->lGen = InterlockedIncrement(&m_lSearchGen);
    StringCchCopyW(pJob->szRoot, _countof(pJob->szRoot), pszRoot);
    StringCchCopyW(pJob->szQuery, _countof(pJob->szQuery), pszQuery);

    m_hSearchThread = CreateThread(NULL, 0, E11SearchThread, pJob, 0, NULL);
    if (!m_hSearchThread)
    {
        HeapFree(GetProcessHeap(), 0, pJob->pRows);
        HeapFree(GetProcessHeap(), 0, pJob);
        m_bSearching = FALSE;
    }

    InvalidateRect(NULL, TRUE);
}

VOID CThisPCView::StopSearch()
{
    InterlockedIncrement(&m_lSearchGen);
    if (m_hSearchThread)
    {
        CloseHandle(m_hSearchThread);
        m_hSearchThread = NULL;
    }
    m_bSearching = FALSE;
}

LRESULT CThisPCView::OnSearchDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    E11_SEARCHJOB *pJob = (E11_SEARCHJOB *)lParam;

    if ((LONG)wParam != m_lSearchGen || !m_bSearchMode)
        return 0;

    m_Results.SetCount(0);
    for (int i = 0; i < pJob->cRows; i++)
        m_Results.Add(pJob->pRows[i]);

    HeapFree(GetProcessHeap(), 0, pJob->pRows);
    HeapFree(GetProcessHeap(), 0, pJob);
    m_bSearching = FALSE;
    if (m_hSearchThread)
    {
        CloseHandle(m_hSearchThread);
        m_hSearchThread = NULL;
    }

    LayoutTiles();
    InvalidateRect(NULL, TRUE);
    return 1;
}

static int
E11FolderIconForCsidl(int csidl)
{
    switch (csidl)
    {
        case CSIDL_DESKTOPDIRECTORY: return EI_FOLDER_DESKTOP;
        case CSIDL_PERSONAL:         return EI_FOLDER_DOCS;
        case CSIDL_MYPICTURES:       return EI_FOLDER_PICTURES;
        case CSIDL_MYMUSIC:          return EI_FOLDER_MUSIC;
        case CSIDL_MYVIDEO:          return EI_FOLDER_VIDEOS;
        default:                     return EI_FOLDER;
    }
}

static VOID
E11FormatSize(ULONGLONG ull, LPWSTR psz, UINT cch)
{
    if (ull >= 1024ull * 1024 * 1024)
    {
        UINT whole = (UINT)(ull >> 30);
        UINT frac = (UINT)((ull * 10) >> 30) % 10;
        StringCchPrintfW(psz, cch, L"%u.%u GB", whole, frac);
    }
    else
    {
        StringCchPrintfW(psz, cch, L"%u MB", (UINT)(ull >> 20));
    }
}

VOID CThisPCView::BuildThisPC()
{
    static const struct { int csidl; LPCWSTR pszName; } c_Folders[] =
    {
        { CSIDL_DESKTOPDIRECTORY, L"Desktop" },
        { CSIDL_PERSONAL,         L"Documents" },
        { CSIDL_PROFILE,          L"Downloads" },
        { CSIDL_MYMUSIC,          L"Music" },
        { CSIDL_MYPICTURES,       L"Pictures" },
        { CSIDL_MYVIDEO,          L"Videos" },
    };

    m_bSearchMode = FALSE;
    m_Tiles.SetCount(0);
    m_iHot = m_iSelected = -1;
    m_anGroupCount[0] = m_anGroupCount[1] = 0;

    for (UINT i = 0; i < _countof(c_Folders); i++)
    {
        E11_TILE tile;
        WCHAR szPath[MAX_PATH];

        ZeroMemory(&tile, sizeof(tile));
        if (FAILED(SHGetFolderPathW(NULL, c_Folders[i].csidl, NULL, 0, szPath)))
            continue;
        if (c_Folders[i].csidl == CSIDL_PROFILE)
        {
            PathAppendW(szPath, L"Downloads");
            if (!PathFileExistsW(szPath))
                CreateDirectoryW(szPath, NULL);
        }

        StringCchCopyW(tile.szName, _countof(tile.szName), c_Folders[i].pszName);
        StringCchCopyW(tile.szPath, _countof(tile.szPath), szPath);
        tile.nIcon = (c_Folders[i].csidl == CSIDL_PROFILE)
                         ? EI_FOLDER_DOWNLOADS
                         : E11FolderIconForCsidl(c_Folders[i].csidl);
        tile.nGroup = 0;
        m_Tiles.Add(tile);
        m_anGroupCount[0]++;
    }

    CComPtr<IShellFolder> pDesktop;
    CComPtr<IShellFolder> pDrives;
    LPITEMIDLIST pidlDrives = NULL;

    if (SUCCEEDED(SHGetDesktopFolder(&pDesktop)) &&
        SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &pidlDrives)) &&
        SUCCEEDED(pDesktop->BindToObject(pidlDrives, NULL, IID_PPV_ARG(IShellFolder, &pDrives))))
    {
        CComPtr<IEnumIDList> pEnum;
        if (SUCCEEDED(pDrives->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &pEnum)) && pEnum)
        {
            LPITEMIDLIST pidlChild;
            while (pEnum->Next(1, &pidlChild, NULL) == S_OK)
            {
                STRRET sr;
                WCHAR szParse[MAX_PATH];

                szParse[0] = 0;
                if (SUCCEEDED(pDrives->GetDisplayNameOf(pidlChild, SHGDN_FORPARSING, &sr)))
                    StrRetToBufW(&sr, pidlChild, szParse, _countof(szParse));

                if (szParse[1] == L':' && szParse[2] == L'\\' && !szParse[3])
                {
                    E11_TILE tile;
                    UINT uType = GetDriveTypeW(szParse);
                    ULARGE_INTEGER ulFree, ulTotal;

                    ZeroMemory(&tile, sizeof(tile));
                    StringCchCopyW(tile.szPath, _countof(tile.szPath), szParse);

                    if (SUCCEEDED(pDrives->GetDisplayNameOf(pidlChild, SHGDN_NORMAL, &sr)))
                        StrRetToBufW(&sr, pidlChild, tile.szName, _countof(tile.szName));
                    if (!tile.szName[0])
                        StringCchPrintfW(tile.szName, _countof(tile.szName), L"Local Disk (%c:)", szParse[0]);

                    tile.nIcon = (uType == DRIVE_CDROM) ? EI_DRIVE_CD : EI_DRIVE;
                    tile.nGroup = 1;

                    if (uType != DRIVE_CDROM &&
                        GetDiskFreeSpaceExW(szParse, NULL, &ulTotal, &ulFree) && ulTotal.QuadPart)
                    {
                        WCHAR szFree[32], szTotal[32];
                        tile.ullTotal = ulTotal.QuadPart;
                        tile.ullFree = ulFree.QuadPart;
                        tile.bHasBar = TRUE;
                        E11FormatSize(tile.ullFree, szFree, _countof(szFree));
                        E11FormatSize(tile.ullTotal, szTotal, _countof(szTotal));
                        StringCchPrintfW(tile.szInfo, _countof(tile.szInfo),
                                         L"%s free of %s", szFree, szTotal);
                    }

                    m_Tiles.Add(tile);
                    m_anGroupCount[1]++;
                }
                ILFree(pidlChild);
            }
        }
    }
    if (pidlDrives)
        ILFree(pidlDrives);

    LayoutTiles();
}

int CThisPCView::ItemCount() const
{
    if (m_bSearchMode)
        return (int)m_Results.GetCount();
    return (int)m_Tiles.GetCount();
}

VOID CThisPCView::LayoutTiles()
{
    RECT rcClient;
    GetClientRect(&rcClient);

    if (m_bSearchMode)
    {
        int y = Sc(14);
        int nRowH = Sc(40);
        for (SIZE_T i = 0; i < m_Results.GetCount(); i++)
        {
            SetRect(&m_Results[i].rc, Sc(16), y, rcClient.right - Sc(16), y + nRowH);
            y += nRowH + Sc(2);
        }
        return;
    }

    int nTileW = Sc(238);
    int nCols = max(1, (rcClient.right - Sc(24)) / (nTileW + Sc(12)));
    int y = Sc(12);

    for (int g = 0; g < 2; g++)
    {
        SetRect(&m_arcGroup[g], Sc(16), y, rcClient.right - Sc(16), y + Sc(24));
        y += Sc(30);

        if (!m_abCollapsed[g])
        {
            int nTileH = (g == 0) ? Sc(52) : Sc(62);
            int col = 0;

            for (SIZE_T i = 0; i < m_Tiles.GetCount(); i++)
            {
                E11_TILE &tile = m_Tiles[i];
                if (tile.nGroup != g)
                    continue;

                SetRect(&tile.rc,
                        Sc(20) + col * (nTileW + Sc(12)), y,
                        Sc(20) + col * (nTileW + Sc(12)) + nTileW, y + nTileH);
                col++;
                if (col == nCols)
                {
                    col = 0;
                    y += nTileH + Sc(8);
                }
            }
            if (col)
                y += nTileH + Sc(8);
        }
        else
        {
            for (SIZE_T i = 0; i < m_Tiles.GetCount(); i++)
                if (m_Tiles[i].nGroup == g)
                    SetRectEmpty(&m_Tiles[i].rc);
        }
        y += Sc(8);
    }
}

LRESULT CThisPCView::OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(&ps);
    RECT rcClient;
    GetClientRect(&rcClient);

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbm = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
    HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

    HBRUSH hbrBg = CreateSolidBrush(m_Pal.FrameBg);
    FillRect(hdcMem, &rcClient, hbrBg);
    DeleteObject(hbrBg);
    SetBkMode(hdcMem, TRANSPARENT);

    if (m_bSearchMode)
    {
        WCHAR szHdr[192];
        RECT rcHdr = { Sc(16), Sc(0), rcClient.right - Sc(16), Sc(14) };

        if (m_bSearching)
            StringCchPrintfW(szHdr, _countof(szHdr), L"Searching for \"%s\"...", m_szQuery);
        else
            StringCchPrintfW(szHdr, _countof(szHdr), L"%d results for \"%s\"",
                             (int)m_Results.GetCount(), m_szQuery);

        SelectObject(hdcMem, m_hFontHeader);
        SetTextColor(hdcMem, m_Pal.DimText);
        DrawTextW(hdcMem, szHdr, -1, &rcHdr, DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX);

        SelectObject(hdcMem, m_hFont);
        for (SIZE_T i = 0; i < m_Results.GetCount(); i++)
        {
            E11_SEARCHROW &row = m_Results[i];
            if (row.rc.bottom < 0 || row.rc.top > rcClient.bottom)
                continue;

            if ((int)i == m_iSelected || (int)i == m_iHot)
            {
                HBRUSH hbrHot = CreateSolidBrush(m_Pal.HotFill);
                FillRect(hdcMem, &row.rc, hbrHot);
                DeleteObject(hbrHot);
            }

            RECT rcIcon = { row.rc.left + Sc(6), (row.rc.top + row.rc.bottom) / 2 - Sc(12),
                            row.rc.left + Sc(30), (row.rc.top + row.rc.bottom) / 2 + Sc(12) };
            E11DrawIcon(hdcMem, &rcIcon, row.bFolder ? EI_FOLDER : EI_FILE, &m_Pal);

            RECT rcName = { rcIcon.right + Sc(10), row.rc.top + Sc(3),
                            row.rc.right - Sc(8), (row.rc.top + row.rc.bottom) / 2 + Sc(2) };
            SetTextColor(hdcMem, m_Pal.Text);
            DrawTextW(hdcMem, row.szName, -1, &rcName,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

            RECT rcDir = { rcIcon.right + Sc(10), (row.rc.top + row.rc.bottom) / 2 + Sc(1),
                           row.rc.right - Sc(8), row.rc.bottom - Sc(2) };
            SetTextColor(hdcMem, m_Pal.DimText);
            DrawTextW(hdcMem, row.szDir, -1, &rcDir,
                      DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | DT_NOPREFIX);
        }
    }
    else
    {
        static const LPCWSTR c_apszGroups[2] = { L"Folders", L"Devices and drives" };

        for (int g = 0; g < 2; g++)
        {
            WCHAR szHdr[64];
            int ccx = m_arcGroup[g].left + Sc(7);
            int ccy = (m_arcGroup[g].top + m_arcGroup[g].bottom) / 2;
            int a = Sc(5);
            RECT rcChevIcon = { ccx - a, ccy - a, ccx + a, ccy + a };
            E11DrawFluentRes(hdcMem, &rcChevIcon,
                             m_abCollapsed[g] ? IDI_FLU_CHEVRIGHT : IDI_FLU_CHEVDOWN,
                             m_Pal.DimText);

            StringCchPrintfW(szHdr, _countof(szHdr), L"%s (%d)",
                             c_apszGroups[g], m_anGroupCount[g]);
            RECT rcHdrText = m_arcGroup[g];
            rcHdrText.left += Sc(20);
            SelectObject(hdcMem, m_hFontHeader);
            SetTextColor(hdcMem, m_Pal.Text);
            DrawTextW(hdcMem, szHdr, -1, &rcHdrText,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        for (SIZE_T i = 0; i < m_Tiles.GetCount(); i++)
        {
            E11_TILE &tile = m_Tiles[i];
            if (IsRectEmpty(&tile.rc))
                continue;

            if ((int)i == m_iSelected)
            {
                HBRUSH hbrSel = CreateSolidBrush(m_Pal.HotFill);
                FillRect(hdcMem, &tile.rc, hbrSel);
                DeleteObject(hbrSel);
                HBRUSH hbrEdge = CreateSolidBrush(m_Pal.Accent);
                FrameRect(hdcMem, &tile.rc, hbrEdge);
                DeleteObject(hbrEdge);
            }
            else if ((int)i == m_iHot)
            {
                HBRUSH hbrHot = CreateSolidBrush(m_Pal.HotFill);
                FillRect(hdcMem, &tile.rc, hbrHot);
                DeleteObject(hbrHot);
            }

            int cyMid = (tile.rc.top + tile.rc.bottom) / 2;
            RECT rcIcon = { tile.rc.left + Sc(8), cyMid - Sc(20),
                            tile.rc.left + Sc(48), cyMid + Sc(20) };
            E11DrawIcon(hdcMem, &rcIcon, tile.nIcon, &m_Pal);

            int xText = rcIcon.right + Sc(10);
            if (!tile.bHasBar && !tile.szInfo[0])
            {
                RECT rcName = { xText, tile.rc.top, tile.rc.right - Sc(6), tile.rc.bottom };
                SelectObject(hdcMem, m_hFont);
                SetTextColor(hdcMem, m_Pal.Text);
                DrawTextW(hdcMem, tile.szName, -1, &rcName,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            else
            {
                RECT rcName = { xText, tile.rc.top + Sc(4), tile.rc.right - Sc(6), tile.rc.top + Sc(22) };
                SelectObject(hdcMem, m_hFont);
                SetTextColor(hdcMem, m_Pal.Text);
                DrawTextW(hdcMem, tile.szName, -1, &rcName,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

                if (tile.bHasBar)
                {
                    RECT rcTrack = { xText, tile.rc.top + Sc(26), tile.rc.right - Sc(14), tile.rc.top + Sc(34) };
                    HBRUSH hbrTrack = CreateSolidBrush(m_Pal.HotFill);
                    FillRect(hdcMem, &rcTrack, hbrTrack);
                    DeleteObject(hbrTrack);

                    ULONGLONG ullUsed = tile.ullTotal - tile.ullFree;
                    int nUsedW = (int)((rcTrack.right - rcTrack.left) *
                                       (ullUsed * 100 / tile.ullTotal) / 100);
                    RECT rcUsed = { rcTrack.left, rcTrack.top, rcTrack.left + nUsedW, rcTrack.bottom };
                    HBRUSH hbrUsed = CreateSolidBrush(m_Pal.Accent);
                    FillRect(hdcMem, &rcUsed, hbrUsed);
                    DeleteObject(hbrUsed);
                }

                RECT rcInfo = { xText, tile.rc.top + Sc(36), tile.rc.right - Sc(6), tile.rc.bottom - Sc(2) };
                SetTextColor(hdcMem, m_Pal.DimText);
                DrawTextW(hdcMem, tile.szInfo, -1, &rcInfo,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
    }

    BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    EndPaint(&ps);
    return 0;
}

LRESULT CThisPCView::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    return 1;
}

LRESULT CThisPCView::OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    int iHot = -1;

    if (m_bSearchMode)
    {
        for (SIZE_T i = 0; i < m_Results.GetCount(); i++)
            if (PtInRect(&m_Results[i].rc, pt))
            {
                iHot = (int)i;
                break;
            }
    }
    else
    {
        for (SIZE_T i = 0; i < m_Tiles.GetCount(); i++)
            if (PtInRect(&m_Tiles[i].rc, pt))
            {
                iHot = (int)i;
                break;
            }
    }

    if (iHot != m_iHot)
    {
        m_iHot = iHot;
        InvalidateRect(NULL, FALSE);
    }
    return 0;
}

LRESULT CThisPCView::OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

    if (!m_bSearchMode)
    {
        for (int g = 0; g < 2; g++)
        {
            if (PtInRect(&m_arcGroup[g], pt))
            {
                m_abCollapsed[g] = !m_abCollapsed[g];
                LayoutTiles();
                InvalidateRect(NULL, TRUE);
                return 0;
            }
        }
    }

    m_iSelected = m_iHot;
    InvalidateRect(NULL, FALSE);

    if (m_iSelected >= 0)
    {
        ULONGLONG ullNow = GetTickCount64();
        if (m_iSelected == m_iLastClick &&
            ullNow - m_ullLastClick <= GetDoubleClickTime())
        {
            m_iLastClick = -1;
            ::SendMessageW(GetParent(), m_bSearchMode ? E11M_OPENRESULT : E11M_OPENTILE,
                           (WPARAM)m_iSelected, 0);
            return 0;
        }
        m_iLastClick = m_iSelected;
        m_ullLastClick = ullNow;
    }
    return 0;
}

LRESULT CThisPCView::OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (m_iHot < 0)
        return 0;

    ::SendMessageW(GetParent(), m_bSearchMode ? E11M_OPENRESULT : E11M_OPENTILE,
                   (WPARAM)m_iHot, 0);
    return 0;
}

LRESULT CThisPCView::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    LayoutTiles();
    InvalidateRect(NULL, TRUE);
    return 0;
}

LRESULT CThisPCView::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    StopSearch();
    return 0;
}
