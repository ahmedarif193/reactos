#include "frame.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer11);

CNavPane::~CNavPane()
{
    FreeItems();
}

VOID CNavPane::FreeItems()
{
    for (SIZE_T i = 0; i < m_Items.GetCount(); i++)
    {
        if (m_Items[i].pidl)
            ILFree(m_Items[i].pidl);
    }
    m_Items.SetCount(0);
}

static VOID
NavAddRoot(CAtlArray<E11_NAVITEM> &Items, LPCWSTR pszName, int nIcon, LPITEMIDLIST pidl)
{
    E11_NAVITEM item;
    ZeroMemory(&item, sizeof(item));
    item.nKind = NAV_ROOT;
    item.nIcon = nIcon;
    item.bExpanded = TRUE;
    item.pidl = pidl;
    StringCchCopyW(item.szName, _countof(item.szName), pszName);
    Items.Add(item);
}

static VOID
NavAddCsidl(CAtlArray<E11_NAVITEM> &Items, int csidl, LPCWSTR pszName, int nIcon)
{
    E11_NAVITEM item;
    LPITEMIDLIST pidl = NULL;

    if (FAILED(SHGetSpecialFolderLocation(NULL, csidl, &pidl)) || !pidl)
        return;

    ZeroMemory(&item, sizeof(item));
    item.nKind = NAV_ITEM;
    item.pidl = pidl;
    item.nDepth = 1;
    item.nIcon = nIcon;
    StringCchCopyW(item.szName, _countof(item.szName), pszName);
    Items.Add(item);
}

static VOID
NavAddPath(CAtlArray<E11_NAVITEM> &Items, LPCWSTR pszPath, LPCWSTR pszName, int nIcon)
{
    E11_NAVITEM item;
    LPITEMIDLIST pidl = NULL;

    if (FAILED(SHParseDisplayName(pszPath, NULL, &pidl, 0, NULL)) || !pidl)
        return;

    ZeroMemory(&item, sizeof(item));
    item.nKind = NAV_ITEM;
    item.pidl = pidl;
    item.nDepth = 1;
    item.nIcon = nIcon;
    StringCchCopyW(item.szName, _countof(item.szName), pszName);
    Items.Add(item);
}

VOID CNavPane::Build()
{
    LPITEMIDLIST pidlPc = NULL;

    FreeItems();

    NavAddRoot(m_Items, L"Quick access", EI_NONE, NULL);
    NavAddCsidl(m_Items, CSIDL_DESKTOPDIRECTORY, L"Desktop", EI_FOLDER_DESKTOP);
    NavAddCsidl(m_Items, CSIDL_PERSONAL, L"Documents", EI_FOLDER_DOCS);
    NavAddCsidl(m_Items, CSIDL_MYPICTURES, L"Pictures", EI_FOLDER_PICTURES);
    NavAddCsidl(m_Items, CSIDL_MYMUSIC, L"Music", EI_FOLDER_MUSIC);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, E11_QUICKACCESS_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        for (int i = 0; i < 32; i++)
        {
            WCHAR szValue[16], szPath[MAX_PATH];
            DWORD cbData = sizeof(szPath), dwType;
            StringCchPrintfW(szValue, _countof(szValue), L"Pin%d", i);
            if (RegQueryValueExW(hKey, szValue, NULL, &dwType,
                                 (LPBYTE)szPath, &cbData) == ERROR_SUCCESS &&
                dwType == REG_SZ && szPath[0])
            {
                LPCWSTR pszName = PathFindFileNameW(szPath);
                if (!pszName || !pszName[0])
                    pszName = szPath;
                NavAddPath(m_Items, szPath, pszName, EI_FOLDER);
                if (m_Items.GetCount() > 0)
                    m_Items[m_Items.GetCount() - 1].bPinned = TRUE;
            }
        }
        RegCloseKey(hKey);
    }

    SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &pidlPc);
    NavAddRoot(m_Items, L"This PC", EI_PC, pidlPc);

    NavAddCsidl(m_Items, CSIDL_DESKTOPDIRECTORY, L"Desktop", EI_FOLDER_DESKTOP);
    NavAddCsidl(m_Items, CSIDL_PERSONAL, L"Documents", EI_FOLDER_DOCS);

    {
        WCHAR szDownloads[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, szDownloads)))
        {
            PathAppendW(szDownloads, L"Downloads");
            if (!PathFileExistsW(szDownloads))
                CreateDirectoryW(szDownloads, NULL);
            NavAddPath(m_Items, szDownloads, L"Downloads", EI_FOLDER_DOWNLOADS);
        }
    }

    NavAddCsidl(m_Items, CSIDL_MYMUSIC, L"Music", EI_FOLDER_MUSIC);
    NavAddCsidl(m_Items, CSIDL_MYPICTURES, L"Pictures", EI_FOLDER_PICTURES);
    NavAddCsidl(m_Items, CSIDL_MYVIDEO, L"Videos", EI_FOLDER_VIDEOS);

    DWORD dwDrives = GetLogicalDrives();
    for (int i = 0; i < 26; i++)
    {
        if (!(dwDrives & (1 << i)))
            continue;

        WCHAR szRoot[8];
        StringCchPrintfW(szRoot, _countof(szRoot), L"%c:\\", L'A' + i);

        UINT uType = GetDriveTypeW(szRoot);
        if (uType == DRIVE_UNKNOWN || uType == DRIVE_NO_ROOT_DIR)
            continue;

        WCHAR szLabel[64];
        WCHAR szName[80];
        szLabel[0] = 0;
        GetVolumeInformationW(szRoot, szLabel, _countof(szLabel), NULL, NULL, NULL, NULL, 0);
        if (szLabel[0])
            StringCchPrintfW(szName, _countof(szName), L"%s (%c:)", szLabel, L'A' + i);
        else
            StringCchPrintfW(szName, _countof(szName), L"%s (%c:)",
                             uType == DRIVE_CDROM ? L"DVD Drive" : L"Local Disk", L'A' + i);

        NavAddPath(m_Items, szRoot, szName,
                   uType == DRIVE_CDROM ? EI_DRIVE_CD : EI_DRIVE);
    }

    NavAddRoot(m_Items, L"Network", EI_NONE, NULL);
    NavAddCsidl(m_Items, CSIDL_NETWORK, L"Network", EI_NETWORK);
}

VOID CNavPane::Layout(const RECT *prcPane)
{
    int y = prcPane->top + E11Scale(10);
    BOOL bParentExpanded = TRUE;

    for (SIZE_T i = 0; i < m_Items.GetCount(); i++)
    {
        E11_NAVITEM &item = m_Items[i];

        if (item.nKind == NAV_ROOT)
            bParentExpanded = TRUE;

        item.bVisible = (item.nKind == NAV_ROOT) || bParentExpanded;
        if (!item.bVisible)
        {
            SetRectEmpty(&item.rc);
            SetRectEmpty(&item.rcChevron);
            continue;
        }

        int nRowH = item.nKind == NAV_ROOT ? E11Scale(30) : E11Scale(26);
        SetRect(&item.rc, prcPane->left, y, prcPane->right, y + nRowH);

        if (item.nKind == NAV_ROOT)
        {
            SetRect(&item.rcChevron, prcPane->left + E11Scale(4), y + (nRowH - E11Scale(16)) / 2,
                    prcPane->left + E11Scale(20), y + (nRowH + E11Scale(16)) / 2);
            bParentExpanded = item.bExpanded;
        }
        else
        {
            SetRectEmpty(&item.rcChevron);
        }

        y += nRowH;
        if (item.nKind == NAV_ROOT && !item.bExpanded)
            y += E11Scale(2);
    }
}

VOID CNavPane::Paint(HDC hdc, const RECT *prcPane, const E11_PALETTE *pPal, HFONT hFont, HFONT hFontHeader)
{
    SetBkMode(hdc, TRANSPARENT);

    for (SIZE_T i = 0; i < m_Items.GetCount(); i++)
    {
        E11_NAVITEM &item = m_Items[i];

        if (!item.bVisible)
            continue;

        if (item.nKind == NAV_ROOT)
        {
            if ((int)i == m_iHot)
            {
                HBRUSH hbrHot = CreateSolidBrush(pPal->HotFill);
                FillRect(hdc, &item.rc, hbrHot);
                DeleteObject(hbrHot);
            }

            int ccx = (item.rcChevron.left + item.rcChevron.right) / 2;
            int ccy = (item.rcChevron.top + item.rcChevron.bottom) / 2;
            int a = E11Scale(5);
            RECT rcChevIcon = { ccx - a, ccy - a, ccx + a, ccy + a };
            E11DrawFluentRes(hdc, &rcChevIcon,
                             item.bExpanded ? IDI_FLU_CHEVDOWN : IDI_FLU_CHEVRIGHT,
                             pPal->DimText);

            RECT rcText = item.rc;
            rcText.left += E11Scale(24);
            SelectObject(hdc, hFontHeader);
            SetTextColor(hdc, pPal->Text);
            DrawTextW(hdc, item.szName, -1, &rcText,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            continue;
        }

        if ((int)i == m_iSelected)
        {
            HBRUSH hbrSel = CreateSolidBrush(pPal->HotFill);
            FillRect(hdc, &item.rc, hbrSel);
            DeleteObject(hbrSel);

            RECT rcMark = { item.rc.left + E11Scale(2), item.rc.top + E11Scale(5),
                            item.rc.left + E11Scale(5), item.rc.bottom - E11Scale(5) };
            HBRUSH hbrMark = CreateSolidBrush(pPal->Accent);
            FillRect(hdc, &rcMark, hbrMark);
            DeleteObject(hbrMark);
        }
        else if ((int)i == m_iHot)
        {
            HBRUSH hbrHot = CreateSolidBrush(pPal->HotFill);
            FillRect(hdc, &item.rc, hbrHot);
            DeleteObject(hbrHot);
        }

        int xIcon = item.rc.left + E11Scale(24);
        int cyMid = (item.rc.top + item.rc.bottom) / 2;
        RECT rcIcon = { xIcon, cyMid - E11Scale(8), xIcon + E11Scale(16), cyMid + E11Scale(8) };
        E11DrawIcon(hdc, &rcIcon, item.nIcon, pPal);

        RECT rcText = item.rc;
        rcText.left = rcIcon.right + E11Scale(8);
        rcText.right -= E11Scale(22);
        SelectObject(hdc, hFont);
        SetTextColor(hdc, pPal->Text);
        DrawTextW(hdc, item.szName, -1, &rcText,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (item.bPinned)
        {
            RECT rcPin = { item.rc.right - E11Scale(20), cyMid - E11Scale(7),
                           item.rc.right - E11Scale(6), cyMid + E11Scale(7) };
            E11DrawIconDim(hdc, &rcPin, EI_PIN, pPal, TRUE);
        }
    }
}

int CNavPane::HitTest(POINT pt)
{
    for (SIZE_T i = 0; i < m_Items.GetCount(); i++)
    {
        if (m_Items[i].bVisible && m_Items[i].nKind == NAV_ITEM &&
            PtInRect(&m_Items[i].rc, pt))
            return (int)i;
    }
    return -1;
}

BOOL CNavPane::HitChevron(POINT pt, int *piItem)
{
    for (SIZE_T i = 0; i < m_Items.GetCount(); i++)
    {
        if (m_Items[i].bVisible && m_Items[i].nKind == NAV_ROOT &&
            PtInRect(&m_Items[i].rc, pt))
        {
            *piItem = (int)i;
            return TRUE;
        }
    }
    return FALSE;
}
