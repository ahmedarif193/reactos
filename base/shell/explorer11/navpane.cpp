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
NavAddHeader(CAtlArray<E11_NAVITEM> &Items, LPCWSTR pszName)
{
    E11_NAVITEM item;
    ZeroMemory(&item, sizeof(item));
    item.bHeader = TRUE;
    StringCchCopyW(item.szName, _countof(item.szName), pszName);
    Items.Add(item);
}

static VOID
NavAddCsidl(CAtlArray<E11_NAVITEM> &Items, int csidl, LPCWSTR pszFallbackName)
{
    E11_NAVITEM item;
    LPITEMIDLIST pidl = NULL;
    SHFILEINFOW sfi;

    if (FAILED(SHGetSpecialFolderLocation(NULL, csidl, &pidl)) || !pidl)
        return;

    ZeroMemory(&item, sizeof(item));
    item.pidl = pidl;
    item.nIndent = 1;

    ZeroMemory(&sfi, sizeof(sfi));
    if (SHGetFileInfoW((LPCWSTR)pidl, 0, &sfi, sizeof(sfi),
                       SHGFI_PIDL | SHGFI_DISPLAYNAME) && sfi.szDisplayName[0])
        StringCchCopyW(item.szName, _countof(item.szName), sfi.szDisplayName);
    else
        StringCchCopyW(item.szName, _countof(item.szName), pszFallbackName);

    Items.Add(item);
}

static VOID
NavAddPath(CAtlArray<E11_NAVITEM> &Items, LPCWSTR pszPath, LPCWSTR pszName)
{
    E11_NAVITEM item;
    LPITEMIDLIST pidl = NULL;

    if (FAILED(SHParseDisplayName(pszPath, NULL, &pidl, 0, NULL)) || !pidl)
        return;

    ZeroMemory(&item, sizeof(item));
    item.pidl = pidl;
    item.nIndent = 1;
    StringCchCopyW(item.szName, _countof(item.szName), pszName);
    Items.Add(item);
}

VOID CNavPane::Build()
{
    FreeItems();

    NavAddHeader(m_Items, L"Quick access");
    NavAddCsidl(m_Items, CSIDL_DESKTOPDIRECTORY, L"Desktop");
    NavAddCsidl(m_Items, CSIDL_PERSONAL, L"Documents");
    NavAddCsidl(m_Items, CSIDL_MYPICTURES, L"Pictures");
    NavAddCsidl(m_Items, CSIDL_MYMUSIC, L"Music");

    NavAddHeader(m_Items, L"This PC");

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
            StringCchPrintfW(szName, _countof(szName), L"Drive (%c:)", L'A' + i);

        NavAddPath(m_Items, szRoot, szName);
    }

    NavAddHeader(m_Items, L"Network");
    NavAddCsidl(m_Items, CSIDL_NETWORK, L"Network");
}

VOID CNavPane::Layout(const RECT *prcPane)
{
    int y = prcPane->top + E11Scale(8);

    for (SIZE_T i = 0; i < m_Items.GetCount(); i++)
    {
        E11_NAVITEM &item = m_Items[i];
        int nRowH = item.bHeader ? E11Scale(30) : E11Scale(26);
        SetRect(&item.rc, prcPane->left, y, prcPane->right, y + nRowH);
        y += nRowH;
    }
}

VOID CNavPane::Paint(HDC hdc, const RECT *prcPane, const E11_PALETTE *pPal, HFONT hFont, HFONT hFontHeader)
{
    SetBkMode(hdc, TRANSPARENT);

    for (SIZE_T i = 0; i < m_Items.GetCount(); i++)
    {
        E11_NAVITEM &item = m_Items[i];

        if (item.bHeader)
        {
            RECT rcText = item.rc;
            rcText.left += E11Scale(12);
            rcText.top += E11Scale(6);
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

            RECT rcMark = { item.rc.left, item.rc.top + E11Scale(4),
                            item.rc.left + E11Scale(3), item.rc.bottom - E11Scale(4) };
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

        RECT rcText = item.rc;
        rcText.left += E11Scale(12) + item.nIndent * E11Scale(16);
        rcText.right -= E11Scale(6);
        SelectObject(hdc, hFont);
        SetTextColor(hdc, pPal->Text);
        DrawTextW(hdc, item.szName, -1, &rcText,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

int CNavPane::HitTest(POINT pt)
{
    for (SIZE_T i = 0; i < m_Items.GetCount(); i++)
    {
        if (!m_Items[i].bHeader && PtInRect(&m_Items[i].rc, pt))
            return (int)i;
    }
    return -1;
}
