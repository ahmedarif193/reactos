#include "frame.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer11);

HINSTANCE g_hInstance = NULL;

static BOOL
E11IsDark(VOID)
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

VOID E11GetPalette(E11_PALETTE *pPal)
{
    if (E11IsDark())
    {
        pPal->FrameBg = RGB(32, 32, 32);
        pPal->BarBg = RGB(43, 43, 43);
        pPal->Text = RGB(235, 235, 235);
        pPal->DimText = RGB(150, 150, 150);
        pPal->HotFill = RGB(61, 61, 61);
        pPal->Accent = GetSysColor(COLOR_HIGHLIGHT);
        pPal->Border = RGB(24, 24, 24);
        pPal->EditBg = RGB(25, 25, 25);
    }
    else
    {
        pPal->FrameBg = RGB(255, 255, 255);
        pPal->BarBg = RGB(245, 246, 247);
        pPal->Text = RGB(34, 34, 34);
        pPal->DimText = RGB(112, 112, 112);
        pPal->HotFill = RGB(229, 243, 255);
        pPal->Accent = GetSysColor(COLOR_HIGHLIGHT);
        pPal->Border = RGB(216, 216, 216);
        pPal->EditBg = RGB(255, 255, 255);
    }
}

class CExplorerFrame :
    public CWindowImpl<CExplorerFrame, CWindow, CFrameWinTraits>,
    public IShellBrowser,
    public IServiceProvider
{
public:
    DECLARE_WND_CLASS_EX(L"Explorer11Frame", CS_HREDRAW | CS_VREDRAW, COLOR_WINDOW)

    LONG m_cRef;
    E11_PALETTE m_Pal;
    CNavPane m_NavPane;
    CComPtr<IShellView> m_pShellView;
    HWND m_hwndView;
    LPITEMIDLIST m_pidlCurrent;
    CAtlArray<LPITEMIDLIST> m_BackStack;
    CAtlArray<LPITEMIDLIST> m_FwdStack;
    HFONT m_hFont;
    HFONT m_hFontHeader;
    HFONT m_hFontAddress;
    WCHAR m_szAddress[MAX_PATH];
    WCHAR m_szItems[64];
    int m_iHotNav;
    RECT m_rcBack, m_rcFwd, m_rcUp, m_rcAddress, m_rcPane, m_rcViewArea, m_rcStatus;
    int m_iRibbonTab;
    int m_iHotTab;
    int m_iHotBtn;
    UINT m_uViewMode;
    CAtlArray<E11_RIBBONBTN> m_RibbonBtns;
    RECT m_rcTabs, m_rcRibbon;
    RECT m_aTabRects[4];

    CExplorerFrame() : m_cRef(1), m_hwndView(NULL), m_pidlCurrent(NULL),
                       m_hFont(NULL), m_hFontHeader(NULL), m_hFontAddress(NULL),
                       m_iHotNav(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        m_szAddress[0] = 0;
        m_szItems[0] = 0;
        m_NavPane.m_pFrame = this;
        E11GetPalette(&m_Pal);
    }

    int Sc(int v) const { return E11Scale(v); }
    int BarH() const { return Sc(44); }
    int PaneW() const { return Sc(220); }
    int StatusH() const { return Sc(24); }

    // *** IUnknown ***
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IOleWindow || riid == IID_IShellBrowser)
            *ppv = static_cast<IShellBrowser *>(this);
        else if (riid == IID_IServiceProvider)
            *ppv = static_cast<IServiceProvider *>(this);
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() { return InterlockedDecrement(&m_cRef); }

    // *** IOleWindow ***
    STDMETHODIMP GetWindow(HWND *phwnd)
    {
        *phwnd = m_hWnd;
        return S_OK;
    }

    STDMETHODIMP ContextSensitiveHelp(BOOL fEnterMode) { return E_NOTIMPL; }

    // *** IShellBrowser ***
    STDMETHODIMP InsertMenusSB(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths) { return E_NOTIMPL; }
    STDMETHODIMP SetMenuSB(HMENU hmenuShared, HOLEMENU holemenuRes, HWND hwndActiveObject) { return S_OK; }
    STDMETHODIMP RemoveMenusSB(HMENU hmenuShared) { return S_OK; }

    STDMETHODIMP SetStatusTextSB(LPCOLESTR pszStatusText)
    {
        return S_OK;
    }

    STDMETHODIMP EnableModelessSB(BOOL fEnable) { return S_OK; }
    STDMETHODIMP TranslateAcceleratorSB(MSG *pmsg, WORD wID) { return S_FALSE; }

    STDMETHODIMP BrowseObject(PCUIDLIST_RELATIVE pidl, UINT wFlags)
    {
        if (wFlags & SBSP_NAVIGATEBACK)
            return NavigateBack();
        if (wFlags & SBSP_NAVIGATEFORWARD)
            return NavigateForward();
        if (wFlags & SBSP_PARENT)
            return NavigateUp();

        if (wFlags & SBSP_RELATIVE)
        {
            LPITEMIDLIST pidlNew = ILCombine(m_pidlCurrent, (PCUIDLIST_ABSOLUTE)pidl);
            if (!pidlNew)
                return E_OUTOFMEMORY;
            HRESULT hr = BrowseTo(pidlNew, TRUE);
            ILFree(pidlNew);
            return hr;
        }

        return BrowseTo((PCIDLIST_ABSOLUTE)pidl, TRUE);
    }

    STDMETHODIMP GetViewStateStream(DWORD grfMode, IStream **ppStrm)
    {
        *ppStrm = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetControlWindow(UINT id, HWND *phwnd)
    {
        *phwnd = NULL;
        return S_FALSE;
    }

    STDMETHODIMP SendControlMsg(UINT id, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pret)
    {
        if (pret)
            *pret = 0;
        return S_FALSE;
    }

    STDMETHODIMP QueryActiveShellView(IShellView **ppshv)
    {
        *ppshv = m_pShellView;
        if (m_pShellView)
        {
            m_pShellView.p->AddRef();
            return S_OK;
        }
        return E_FAIL;
    }

    STDMETHODIMP OnViewWindowActive(IShellView *pshv) { return S_OK; }
    STDMETHODIMP SetToolbarItems(LPTBBUTTONSB lpButtons, UINT nButtons, UINT uFlags) { return S_OK; }

    // *** IServiceProvider ***
    STDMETHODIMP QueryService(REFGUID guidService, REFIID riid, void **ppv)
    {
        if (guidService == SID_SShellBrowser || guidService == SID_STopLevelBrowser)
            return QueryInterface(riid, ppv);
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    // *** Navigation ***
    VOID UpdateAddressText()
    {
        m_szAddress[0] = 0;
        if (m_pidlCurrent)
        {
            if (!SHGetPathFromIDListW(m_pidlCurrent, m_szAddress) || !m_szAddress[0])
            {
                SHFILEINFOW sfi;
                ZeroMemory(&sfi, sizeof(sfi));
                if (SHGetFileInfoW((LPCWSTR)m_pidlCurrent, 0, &sfi, sizeof(sfi),
                                   SHGFI_PIDL | SHGFI_DISPLAYNAME))
                    StringCchCopyW(m_szAddress, _countof(m_szAddress), sfi.szDisplayName);
            }
            SetWindowTextW(m_szAddress[0] ? m_szAddress : L"Explorer");
        }
    }

    VOID UpdateItemCount()
    {
        m_szItems[0] = 0;
        if (m_pShellView)
        {
            CComPtr<IFolderView> pFolderView;
            int nItems = 0;
            if (SUCCEEDED(m_pShellView->QueryInterface(IID_PPV_ARG(IFolderView, &pFolderView))) &&
                SUCCEEDED(pFolderView->ItemCount(SVGIO_ALLVIEW, &nItems)))
            {
                StringCchPrintfW(m_szItems, _countof(m_szItems),
                                 nItems == 1 ? L"%d item" : L"%d items", nItems);
            }
        }
        InvalidateRect(&m_rcStatus, FALSE);
    }

    VOID SyncNavSelection()
    {
        m_NavPane.m_iSelected = -1;
        if (!m_pidlCurrent)
            return;
        for (SIZE_T i = 0; i < m_NavPane.m_Items.GetCount(); i++)
        {
            if (!m_NavPane.m_Items[i].bHeader &&
                m_NavPane.m_Items[i].pidl &&
                ILIsEqual(m_NavPane.m_Items[i].pidl, m_pidlCurrent))
            {
                m_NavPane.m_iSelected = (int)i;
                break;
            }
        }
        InvalidateRect(&m_rcPane, FALSE);
    }

    HRESULT BrowseTo(PCIDLIST_ABSOLUTE pidl, BOOL bPushHistory)
    {
        CComPtr<IShellFolder> pDesktop;
        CComPtr<IShellFolder> pFolder;
        CComPtr<IShellView> pNewView;
        FOLDERSETTINGS fs;
        RECT rcView;
        HWND hwndNewView = NULL;
        HRESULT hr;

        if (!pidl)
            return E_INVALIDARG;

        hr = SHGetDesktopFolder(&pDesktop);
        if (FAILED(hr))
            return hr;

        if (ILIsEmpty(pidl))
        {
            pFolder = pDesktop;
        }
        else
        {
            hr = pDesktop->BindToObject(pidl, NULL, IID_PPV_ARG(IShellFolder, &pFolder));
            if (FAILED(hr))
                return hr;
        }

        hr = pFolder->CreateViewObject(m_hWnd, IID_PPV_ARG(IShellView, &pNewView));
        if (FAILED(hr))
            return hr;

        fs.ViewMode = FVM_DETAILS;
        fs.fFlags = FWF_SHOWSELALWAYS | FWF_NOWEBVIEW;

        rcView = m_rcViewArea;

        hr = pNewView->CreateViewWindow(m_pShellView, &fs,
                                        static_cast<IShellBrowser *>(this),
                                        &rcView, &hwndNewView);
        if (FAILED(hr) || !hwndNewView)
            return FAILED(hr) ? hr : E_FAIL;

        if (m_pShellView)
        {
            m_pShellView->UIActivate(SVUIA_DEACTIVATE);
            m_pShellView->DestroyViewWindow();
        }

        if (bPushHistory && m_pidlCurrent)
        {
            m_BackStack.Add(m_pidlCurrent);
            for (SIZE_T i = 0; i < m_FwdStack.GetCount(); i++)
                ILFree(m_FwdStack[i]);
            m_FwdStack.SetCount(0);
            m_pidlCurrent = NULL;
        }
        else if (m_pidlCurrent)
        {
            ILFree(m_pidlCurrent);
            m_pidlCurrent = NULL;
        }

        m_pidlCurrent = ILClone(pidl);
        m_pShellView = pNewView;
        m_hwndView = hwndNewView;

        m_pShellView->UIActivate(SVUIA_ACTIVATE_NOFOCUS);
        ::SetWindowPos(m_hwndView, NULL,
                       m_rcViewArea.left, m_rcViewArea.top,
                       m_rcViewArea.right - m_rcViewArea.left,
                       m_rcViewArea.bottom - m_rcViewArea.top,
                       SWP_NOZORDER | SWP_SHOWWINDOW);

        UpdateAddressText();
        UpdateItemCount();
        SyncNavSelection();
        InvalidateRect(NULL, FALSE);
        return S_OK;
    }

    HRESULT NavigateBack()
    {
        if (m_BackStack.GetCount() == 0)
            return S_FALSE;

        LPITEMIDLIST pidlTarget = m_BackStack[m_BackStack.GetCount() - 1];
        m_BackStack.SetCount(m_BackStack.GetCount() - 1);

        if (m_pidlCurrent)
        {
            m_FwdStack.Add(m_pidlCurrent);
            m_pidlCurrent = NULL;
        }

        HRESULT hr = BrowseTo(pidlTarget, FALSE);
        ILFree(pidlTarget);
        return hr;
    }

    HRESULT NavigateForward()
    {
        if (m_FwdStack.GetCount() == 0)
            return S_FALSE;

        LPITEMIDLIST pidlTarget = m_FwdStack[m_FwdStack.GetCount() - 1];
        m_FwdStack.SetCount(m_FwdStack.GetCount() - 1);

        if (m_pidlCurrent)
        {
            m_BackStack.Add(m_pidlCurrent);
            m_pidlCurrent = NULL;
        }

        HRESULT hr = BrowseTo(pidlTarget, FALSE);
        ILFree(pidlTarget);
        return hr;
    }

    HRESULT NavigateUp()
    {
        LPITEMIDLIST pidlParent;

        if (!m_pidlCurrent || ILIsEmpty(m_pidlCurrent))
            return S_FALSE;

        pidlParent = ILClone(m_pidlCurrent);
        if (!pidlParent)
            return E_OUTOFMEMORY;

        ILRemoveLastID(pidlParent);
        HRESULT hr = BrowseTo(pidlParent, TRUE);
        ILFree(pidlParent);
        return hr;
    }

    // *** Layout / painting ***
    VOID Layout()
    {
        RECT rcClient;
        GetClientRect(&rcClient);

        SetRect(&m_rcBack, Sc(8), (BarH() - Sc(28)) / 2, Sc(8) + Sc(32), (BarH() + Sc(28)) / 2);
        SetRect(&m_rcFwd, m_rcBack.right + Sc(2), m_rcBack.top, m_rcBack.right + Sc(2) + Sc(32), m_rcBack.bottom);
        SetRect(&m_rcUp, m_rcFwd.right + Sc(2), m_rcBack.top, m_rcFwd.right + Sc(2) + Sc(32), m_rcBack.bottom);
        SetRect(&m_rcAddress, m_rcUp.right + Sc(10), (BarH() - Sc(28)) / 2,
                rcClient.right - Sc(12), (BarH() + Sc(28)) / 2);

        SetRect(&m_rcPane, 0, BarH(), PaneW(), rcClient.bottom - StatusH());
        SetRect(&m_rcViewArea, PaneW() + 1, BarH(),
                rcClient.right, rcClient.bottom - StatusH());
        SetRect(&m_rcStatus, 0, rcClient.bottom - StatusH(), rcClient.right, rcClient.bottom);

        m_NavPane.Layout(&m_rcPane);

        if (m_hwndView)
        {
            ::SetWindowPos(m_hwndView, NULL,
                           m_rcViewArea.left, m_rcViewArea.top,
                           m_rcViewArea.right - m_rcViewArea.left,
                           m_rcViewArea.bottom - m_rcViewArea.top,
                           SWP_NOZORDER);
        }
    }

    VOID DrawNavButton(HDC hdc, const RECT *prc, int nGlyph, BOOL bEnabled, BOOL bHot)
    {
        if (bHot && bEnabled)
        {
            HBRUSH hbrHot = CreateSolidBrush(m_Pal.HotFill);
            FillRect(hdc, prc, hbrHot);
            DeleteObject(hbrHot);
        }

        COLORREF crGlyph = bEnabled ? m_Pal.Text : m_Pal.DimText;
        HPEN hpen = CreatePen(PS_SOLID, Sc(2), crGlyph);
        HGDIOBJ hpenOld = SelectObject(hdc, hpen);
        int cx = (prc->left + prc->right) / 2;
        int cy = (prc->top + prc->bottom) / 2;
        int r = Sc(5);

        if (nGlyph == 0)
        {
            MoveToEx(hdc, cx + r, cy - r, NULL);
            LineTo(hdc, cx - r, cy);
            LineTo(hdc, cx + r, cy + r);
        }
        else if (nGlyph == 1)
        {
            MoveToEx(hdc, cx - r, cy - r, NULL);
            LineTo(hdc, cx + r, cy);
            LineTo(hdc, cx - r, cy + r);
        }
        else
        {
            MoveToEx(hdc, cx - r, cy + r / 2, NULL);
            LineTo(hdc, cx, cy - r);
            LineTo(hdc, cx + r, cy + r / 2);
        }

        SelectObject(hdc, hpenOld);
        DeleteObject(hpen);
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);
        RECT rcClient;
        GetClientRect(&rcClient);

        RECT rcBar = { 0, 0, rcClient.right, BarH() };
        HBRUSH hbrBar = CreateSolidBrush(m_Pal.BarBg);
        FillRect(hdc, &rcBar, hbrBar);
        DeleteObject(hbrBar);

        HBRUSH hbrPane = CreateSolidBrush(m_Pal.BarBg);
        FillRect(hdc, &m_rcPane, hbrPane);
        DeleteObject(hbrPane);

        HBRUSH hbrStatus = CreateSolidBrush(m_Pal.BarBg);
        FillRect(hdc, &m_rcStatus, hbrStatus);
        DeleteObject(hbrStatus);

        HBRUSH hbrLine = CreateSolidBrush(m_Pal.Border);
        RECT rcSep = { 0, BarH() - 1, rcClient.right, BarH() };
        FillRect(hdc, &rcSep, hbrLine);
        RECT rcSep2 = { m_rcPane.right, m_rcPane.top, m_rcPane.right + 1, m_rcPane.bottom };
        FillRect(hdc, &rcSep2, hbrLine);
        RECT rcSep3 = { 0, m_rcStatus.top, rcClient.right, m_rcStatus.top + 1 };
        FillRect(hdc, &rcSep3, hbrLine);
        DeleteObject(hbrLine);

        DrawNavButton(hdc, &m_rcBack, 0, m_BackStack.GetCount() > 0, m_iHotNav == 1);
        DrawNavButton(hdc, &m_rcFwd, 1, m_FwdStack.GetCount() > 0, m_iHotNav == 2);
        DrawNavButton(hdc, &m_rcUp, 2, m_pidlCurrent && !ILIsEmpty(m_pidlCurrent), m_iHotNav == 3);

        HBRUSH hbrEdit = CreateSolidBrush(m_Pal.EditBg);
        FillRect(hdc, &m_rcAddress, hbrEdit);
        DeleteObject(hbrEdit);
        HBRUSH hbrEditEdge = CreateSolidBrush(m_Pal.Border);
        FrameRect(hdc, &m_rcAddress, hbrEditEdge);
        DeleteObject(hbrEditEdge);

        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ hFontOld = SelectObject(hdc, m_hFontAddress);
        SetTextColor(hdc, m_Pal.Text);
        RECT rcAddrText = m_rcAddress;
        rcAddrText.left += Sc(10);
        rcAddrText.right -= Sc(6);
        DrawTextW(hdc, m_szAddress[0] ? m_szAddress : L"This PC", -1, &rcAddrText,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        m_NavPane.Paint(hdc, &m_rcPane, &m_Pal, m_hFont, m_hFontHeader);

        SelectObject(hdc, m_hFont);
        SetTextColor(hdc, m_Pal.DimText);
        RECT rcStatusText = m_rcStatus;
        rcStatusText.left += Sc(12);
        DrawTextW(hdc, m_szItems, -1, &rcStatusText,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        SelectObject(hdc, hFontOld);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return 1;
    }

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_hFont = CreateFontW(-Sc(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        m_hFontHeader = CreateFontW(-Sc(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        m_hFontAddress = CreateFontW(-Sc(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        m_NavPane.Build();
        Layout();
        return 0;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (m_pShellView)
        {
            m_pShellView->UIActivate(SVUIA_DEACTIVATE);
            m_pShellView->DestroyViewWindow();
            m_pShellView.Release();
        }
        if (m_pidlCurrent)
        {
            ILFree(m_pidlCurrent);
            m_pidlCurrent = NULL;
        }
        for (SIZE_T i = 0; i < m_BackStack.GetCount(); i++)
            ILFree(m_BackStack[i]);
        m_BackStack.SetCount(0);
        for (SIZE_T i = 0; i < m_FwdStack.GetCount(); i++)
            ILFree(m_FwdStack[i]);
        m_FwdStack.SetCount(0);

        if (m_hFont) { DeleteObject(m_hFont); m_hFont = NULL; }
        if (m_hFontHeader) { DeleteObject(m_hFontHeader); m_hFontHeader = NULL; }
        if (m_hFontAddress) { DeleteObject(m_hFontAddress); m_hFontAddress = NULL; }

        PostQuitMessage(0);
        return 0;
    }

    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        Layout();
        InvalidateRect(NULL, TRUE);
        return 0;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int iHotNav = 0;
        if (PtInRect(&m_rcBack, pt)) iHotNav = 1;
        else if (PtInRect(&m_rcFwd, pt)) iHotNav = 2;
        else if (PtInRect(&m_rcUp, pt)) iHotNav = 3;

        int iHotPane = PtInRect(&m_rcPane, pt) ? m_NavPane.HitTest(pt) : -1;

        if (iHotNav != m_iHotNav || iHotPane != m_NavPane.m_iHot)
        {
            m_iHotNav = iHotNav;
            m_NavPane.m_iHot = iHotPane;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (PtInRect(&m_rcBack, pt))
        {
            NavigateBack();
            return 0;
        }
        if (PtInRect(&m_rcFwd, pt))
        {
            NavigateForward();
            return 0;
        }
        if (PtInRect(&m_rcUp, pt))
        {
            NavigateUp();
            return 0;
        }

        int iNav = m_NavPane.HitTest(pt);
        if (iNav >= 0 && m_NavPane.m_Items[iNav].pidl)
            BrowseTo(m_NavPane.m_Items[iNav].pidl, TRUE);

        return 0;
    }

    LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (m_hwndView)
            ::SetFocus(m_hwndView);
        return 0;
    }

    BEGIN_MSG_MAP(CExplorerFrame)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
    END_MSG_MAP()
};

static HRESULT
E11GetStartPidl(LPCWSTR pszCmdLine, LPITEMIDLIST *ppidl)
{
    *ppidl = NULL;

    if (pszCmdLine && pszCmdLine[0])
    {
        WCHAR szPath[MAX_PATH];
        StringCchCopyW(szPath, _countof(szPath), pszCmdLine);
        PathUnquoteSpacesW(szPath);
        if (SUCCEEDED(SHParseDisplayName(szPath, NULL, ppidl, 0, NULL)) && *ppidl)
            return S_OK;
    }

    return SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, ppidl);
}

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    INITCOMMONCONTROLSEX icc;
    LPITEMIDLIST pidlStart = NULL;
    MSG msg;

    g_hInstance = hInstance;

    if (FAILED(OleInitialize(NULL)))
        return 1;

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES;
    InitCommonControlsEx(&icc);

    CExplorerFrame *pFrame = new CExplorerFrame();
    if (!pFrame)
    {
        OleUninitialize();
        return 1;
    }

    RECT rcInit = { CW_USEDEFAULT, CW_USEDEFAULT, 0, 0 };
    if (!pFrame->Create(NULL, rcInit, L"Explorer",
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN))
    {
        delete pFrame;
        OleUninitialize();
        return 1;
    }

    pFrame->SetWindowPos(NULL, 0, 0, E11Scale(960), E11Scale(640),
                         SWP_NOMOVE | SWP_NOZORDER);
    pFrame->ShowWindow(nShowCmd == SW_HIDE ? SW_SHOW : nShowCmd);
    pFrame->UpdateWindow();

    if (SUCCEEDED(E11GetStartPidl(lpCmdLine, &pidlStart)) && pidlStart)
    {
        pFrame->BrowseTo(pidlStart, FALSE);
        ILFree(pidlStart);
    }

    while (GetMessageW(&msg, NULL, 0, 0))
    {
        if (pFrame->m_pShellView &&
            pFrame->m_pShellView->TranslateAccelerator(&msg) == S_OK)
            continue;

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    delete pFrame;
    OleUninitialize();
    return 0;
}
