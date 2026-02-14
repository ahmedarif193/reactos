/*
 * Shell Desktop
 *
 * Copyright 2008 Thomas Bluemel
 * Copyright 2020 Katayama Hirofumi MZ
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "shelldesktop.h"

// Support for multiple monitors is disabled till LVM_SETWORKAREAS gets implemented
#ifdef MULTIMONITOR_SUPPORT
#include <atlcoll.h>
#endif

#include <dbt.h>
#include <winioctl.h>

WINE_DEFAULT_DEBUG_CHANNEL(desktop);

static const WCHAR szProgmanClassName[]  = L"Progman";
static const WCHAR szProgmanWindowName[] = L"Program Manager";
static const GUID  kGuidDevInterfaceCdrom = {0x53f56308, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}};
static const GUID  kGuidDevInterfaceVolume = {0x53f5630d, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}};
static const UINT_PTR kDesktopCdromPollTimerId = 0x4344; /* 'CD' */
static const UINT kDesktopCdromPollIntervalMs = 2000;

static DWORD
QueryCdromDriveMask(VOID)
{
    WCHAR szPath[] = L"A:\\";
    DWORD dwDrives = GetLogicalDrives();
    DWORD dwCdromMask = 0;

    for (INT iDrive = 0; iDrive <= 'Z' - 'A'; ++iDrive)
    {
        DWORD dwBit = (1u << iDrive);
        if (!(dwDrives & dwBit))
            continue;

        szPath[0] = L'A' + iDrive;
        if (GetDriveTypeW(szPath) == DRIVE_CDROM)
            dwCdromMask |= dwBit;
    }

    return dwCdromMask;
}

static BOOL
IsCdromMediaPresent(_In_ WCHAR DriveLetter)
{
    WCHAR szDevice[] = L"\\\\.\\X:";
    HANDLE hDevice;
    DWORD BytesReturned;

    szDevice[4] = DriveLetter;
    hDevice = CreateFileW(szDevice,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    if (hDevice != INVALID_HANDLE_VALUE)
    {
        BOOL Present = DeviceIoControl(hDevice,
                                       IOCTL_STORAGE_CHECK_VERIFY,
                                       NULL,
                                       0,
                                       NULL,
                                       0,
                                       &BytesReturned,
                                       NULL);
#ifdef IOCTL_STORAGE_CHECK_VERIFY2
        if (!Present)
        {
            Present = DeviceIoControl(hDevice,
                                      IOCTL_STORAGE_CHECK_VERIFY2,
                                      NULL,
                                      0,
                                      NULL,
                                      0,
                                      &BytesReturned,
                                      NULL);
        }
#endif
        CloseHandle(hDevice);
        if (Present)
            return TRUE;
    }
    /* Fallback for providers that don't support storage verify IOCTLs. */
    WCHAR szPath[] = L"A:\\";
    szPath[0] = DriveLetter;
    return GetVolumeInformationW(szPath, NULL, 0, NULL, NULL, NULL, NULL, 0);
}

static DWORD
QueryCdromMediaMask(VOID)
{
    DWORD dwDrives = QueryCdromDriveMask();
    DWORD dwCdromMediaMask = 0;

    for (INT iDrive = 0; iDrive <= 'Z' - 'A'; ++iDrive)
    {
        DWORD dwBit = (1u << iDrive);
        if (!(dwDrives & dwBit))
            continue;

        BOOL present = IsCdromMediaPresent(L'A' + iDrive);
        if (present)
            dwCdromMediaMask |= dwBit;
    }

    return dwCdromMediaMask;
}

static BOOL IsDesktopBrowserForwardShellViewCmd(WORD Cmd)
{
    // Note: The normal CShellBrowser forwards the entire FCIDM_SHVIEWFIRST..LAST range, we do not.
    // Note: Windows allows FCIDM_SHVIEW_SHOWINGROUPS but we don't support it nor does it make sense.
    return (FCIDM_SHVIEW_CREATELINK <= Cmd && Cmd <= FCIDM_SHVIEW_DESELECTALL) || 
           (FCIDM_SHVIEW_ARRANGE_AUTO <= Cmd && Cmd <= FCIDM_SHVIEW_ARRANGE_AUTOGRID) ||
           (Cmd == FCIDM_SHVIEW_REFRESH);
}

class CDesktopBrowser :
    public CWindowImpl<CDesktopBrowser, CWindow, CFrameWinTraits>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellBrowser,
    public IShellBrowserService,
    public IServiceProvider
{
private:
    HACCEL m_hAccel;
    HWND m_hWndShellView;
    CComPtr<IShellDesktopTray> m_Tray;
    CComPtr<IShellView>        m_ShellView;

    CComPtr<IOleWindow>        m_ChangeNotifyServer;
    HWND                       m_hwndChangeNotifyServer;
    HDEVNOTIFY                 m_hDevNotifyCdrom;
    HDEVNOTIFY                 m_hDevNotifyVolume;
    DWORD                      m_dwCdromMediaMask;
    DWORD                      m_dwCdromDriveMask;
    DWORD m_dwDrives;

    LRESULT _NotifyTray(UINT uMsg, WPARAM wParam, LPARAM lParam);
    HRESULT _Resize();
    BOOL UpdateDriveTopology(UINT_PTR EventTag, BOOL IsTimerPoll, BOOL AllowRemove);
    VOID UpdateCdromMediaState(UINT_PTR EventTag, BOOL IsTimerPoll);

public:
    CDesktopBrowser();
    ~CDesktopBrowser();
    HRESULT Initialize(IShellDesktopTray *ShellDeskx);

    // *** IOleWindow methods ***
    STDMETHOD(GetWindow)(HWND *lphwnd) override;
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) override;

    // *** IShellBrowser methods ***
    STDMETHOD(InsertMenusSB)(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths) override;
    STDMETHOD(SetMenuSB)(HMENU hmenuShared, HOLEMENU holemenuRes, HWND hwndActiveObject) override;
    STDMETHOD(RemoveMenusSB)(HMENU hmenuShared) override;
    STDMETHOD(SetStatusTextSB)(LPCOLESTR pszStatusText) override;
    STDMETHOD(EnableModelessSB)(BOOL fEnable) override;
    STDMETHOD(TranslateAcceleratorSB)(MSG *pmsg, WORD wID) override;
    STDMETHOD(BrowseObject)(LPCITEMIDLIST pidl, UINT wFlags) override;
    STDMETHOD(GetViewStateStream)(DWORD grfMode, IStream **ppStrm) override;
    STDMETHOD(GetControlWindow)(UINT id, HWND *lphwnd) override;
    STDMETHOD(SendControlMsg)(UINT id, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pret) override;
    STDMETHOD(QueryActiveShellView)(struct IShellView **ppshv) override;
    STDMETHOD(OnViewWindowActive)(struct IShellView *ppshv) override;
    STDMETHOD(SetToolbarItems)(LPTBBUTTON lpButtons, UINT nButtons, UINT uFlags) override;

    // *** IShellBrowserService methods ***
    STDMETHOD(GetPropertyBag)(long flags, REFIID riid, void **ppv) override;

    // *** IBrowserService2 methods (fake for now) ***
    inline void SetTopBrowser() const {}

    // *** IServiceProvider methods ***
    STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override;

    // message handlers
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnOpenNewWindow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnGetChangeNotifyServer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnDeviceChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnShowOptionsDlg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSaveState(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);

DECLARE_WND_CLASS_EX(szProgmanClassName, CS_DBLCLKS, COLOR_DESKTOP)

BEGIN_MSG_MAP(CBaseBar)
    MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
    MESSAGE_HANDLER(WM_SIZE, OnSize)
    MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnSettingChange)
    MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
    MESSAGE_HANDLER(WM_CLOSE, OnClose)
    MESSAGE_HANDLER(WM_EXPLORER_OPEN_NEW_WINDOW, OnOpenNewWindow)
    MESSAGE_HANDLER(WM_COMMAND, OnCommand)
    MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
    MESSAGE_HANDLER(WM_DESKTOP_GET_CNOTIFY_SERVER, OnGetChangeNotifyServer)
    MESSAGE_HANDLER(WM_DEVICECHANGE, OnDeviceChange)
    MESSAGE_HANDLER(WM_TIMER, OnTimer)
    MESSAGE_HANDLER(WM_PROGMAN_OPENSHELLSETTINGS, OnShowOptionsDlg)
    MESSAGE_HANDLER(WM_PROGMAN_SAVESTATE, OnSaveState)
END_MSG_MAP()

BEGIN_COM_MAP(CDesktopBrowser)
    COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
    COM_INTERFACE_ENTRY_IID(IID_IShellBrowser, IShellBrowser)
    COM_INTERFACE_ENTRY_IID(IID_IShellBrowserService, IShellBrowserService)
    COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
END_COM_MAP()
};

CDesktopBrowser::CDesktopBrowser():
    m_hAccel(NULL),
    m_hWndShellView(NULL),
    m_hwndChangeNotifyServer(NULL),
    m_hDevNotifyCdrom(NULL),
    m_hDevNotifyVolume(NULL),
    m_dwCdromMediaMask(0),
    m_dwCdromDriveMask(0),
    m_dwDrives(::GetLogicalDrives())
{
    SetTopBrowser();
}

CDesktopBrowser::~CDesktopBrowser()
{
    if (m_hWnd)
        ::KillTimer(m_hWnd, kDesktopCdromPollTimerId);

    if (m_hDevNotifyCdrom)
    {
        if (!::UnregisterDeviceNotification(m_hDevNotifyCdrom))
            WARN("UnregisterDeviceNotification(CDROM) failed, gle=%lu\n", GetLastError());
        m_hDevNotifyCdrom = NULL;
    }

    if (m_hDevNotifyVolume)
    {
        if (!::UnregisterDeviceNotification(m_hDevNotifyVolume))
            WARN("UnregisterDeviceNotification(VOLUME) failed, gle=%lu\n", GetLastError());
        m_hDevNotifyVolume = NULL;
    }

    if (m_ShellView.p != NULL && m_hWndShellView != NULL)
    {
        m_ShellView->DestroyViewWindow();
    }

    if (m_hwndChangeNotifyServer)
    {
        ::DestroyWindow(m_hwndChangeNotifyServer);
    }
}

#ifdef MULTIMONITOR_SUPPORT
BOOL CALLBACK MonitorEnumProc(
  _In_ HMONITOR hMonitor,
  _In_ HDC      hdcMonitor,
  _In_ LPRECT   lprcMonitor,
  _In_ LPARAM   dwData
)
{
    CAtlList<RECT> *list = (CAtlList<RECT>*)dwData;
    MONITORINFO MonitorInfo;
    MonitorInfo.cbSize = sizeof(MonitorInfo);
    if (::GetMonitorInfoW(hMonitor, &MonitorInfo))
    {
        list->AddTail(MonitorInfo.rcWork);
    }

    return TRUE;
}
#endif

HRESULT CDesktopBrowser::_Resize()
{
    RECT rcNewSize;

#ifdef MULTIMONITOR_SUPPORT

    UINT cMonitors = GetSystemMetrics(SM_CMONITORS);
    if (cMonitors == 1)
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcNewSize, 0);
    }
    else
    {
        SetRect(&rcNewSize,
                GetSystemMetrics(SM_XVIRTUALSCREEN),
                GetSystemMetrics(SM_YVIRTUALSCREEN),
                GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN));
    }

    ::MoveWindow(m_hWnd, rcNewSize.left, rcNewSize.top, rcNewSize.right - rcNewSize.left, rcNewSize.bottom - rcNewSize.top, TRUE);
    ::MoveWindow(m_hWndShellView, 0, 0, rcNewSize.right - rcNewSize.left, rcNewSize.bottom - rcNewSize.top, TRUE);

    if (cMonitors != 1)
    {
        CAtlList<RECT> list;
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&list);
        RECT* prcWorkAreas = new RECT[list.GetCount()];
        int i = 0;
        for (POSITION it = list.GetHeadPosition(); it; list.GetNext(it))
            prcWorkAreas[i++] = list.GetAt(it);

        HWND hwndListView = FindWindowExW(m_hWndShellView, NULL, WC_LISTVIEW, NULL);

        ::SendMessageW(hwndListView, LVM_SETWORKAREAS , i, (LPARAM)prcWorkAreas);
    }

#else
     SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcNewSize, 0);
    ::MoveWindow(m_hWnd, rcNewSize.left, rcNewSize.top, rcNewSize.right - rcNewSize.left, rcNewSize.bottom - rcNewSize.top, TRUE);
    ::MoveWindow(m_hWndShellView, 0, 0, rcNewSize.right - rcNewSize.left, rcNewSize.bottom - rcNewSize.top, TRUE);

#endif
    return S_OK;
}

HRESULT CDesktopBrowser::Initialize(IShellDesktopTray *ShellDesk)
{
    CComPtr<IShellFolder> psfDesktop;
    HRESULT hRet;
    hRet = SHGetDesktopFolder(&psfDesktop);
    if (FAILED_UNEXPECTEDLY(hRet))
        return hRet;

    m_Tray = ShellDesk;

    Create(NULL, NULL, szProgmanWindowName, WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW);
    if (!m_hWnd)
        return E_FAIL;

    hRet = psfDesktop->CreateViewObject(m_hWnd, IID_PPV_ARG(IShellView, &m_ShellView));
    if (FAILED_UNEXPECTEDLY(hRet))
        return hRet;

    m_Tray->RegisterDesktopWindow(m_hWnd);
    if (FAILED_UNEXPECTEDLY(hRet))
        return hRet;

    BOOL fHideIcons = SHELL_GetSetting(SSF_HIDEICONS, fHideIcons);
    FOLDERSETTINGS fs;
    RECT rcShellView = {0,0,0,0};
    fs.ViewMode = FVM_ICON;
    fs.fFlags = FWF_DESKTOP | FWF_NOCLIENTEDGE | FWF_NOSCROLL | FWF_TRANSPARENT |
                FWF_AUTOARRANGE | (fHideIcons ? FWF_NOICONS : 0);
    hRet = m_ShellView->CreateViewWindow(NULL, &fs, (IShellBrowser *)this, &rcShellView, &m_hWndShellView);
    if (FAILED_UNEXPECTEDLY(hRet))
        return hRet;

    _Resize();

    HWND hwndListView = FindWindowExW(m_hWndShellView, NULL, WC_LISTVIEW, NULL);

    m_hAccel = LoadAcceleratorsW(shell32_hInstance, MAKEINTRESOURCEW(IDA_DESKBROWSER));

#if 1
    /* A Windows8+ specific hack */
    ::ShowWindow(m_hWndShellView, SW_SHOW);
    ::ShowWindow(hwndListView, SW_SHOW);
#endif
    ShowWindow(SW_SHOW);
    UpdateWindow();

    m_dwCdromDriveMask = QueryCdromDriveMask();
    m_dwCdromMediaMask = QueryCdromMediaMask();

    DEV_BROADCAST_DEVICEINTERFACE_W notifyFilter = {};
    notifyFilter.dbcc_size = sizeof(notifyFilter);
    notifyFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

    notifyFilter.dbcc_classguid = kGuidDevInterfaceCdrom;
    m_hDevNotifyCdrom = RegisterDeviceNotificationW(m_hWnd, &notifyFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (m_hDevNotifyCdrom)
    {
        TRACE("Registered cdrom device notifications: handle=%p class=%s\n",
              m_hDevNotifyCdrom, wine_dbgstr_guid(&kGuidDevInterfaceCdrom));
    }
    else
    {
        WARN("RegisterDeviceNotificationW(cdrom) failed, gle=%lu\n", GetLastError());
    }

    notifyFilter.dbcc_classguid = kGuidDevInterfaceVolume;
    m_hDevNotifyVolume = RegisterDeviceNotificationW(m_hWnd, &notifyFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (m_hDevNotifyVolume)
    {
        TRACE("Registered volume device notifications: handle=%p class=%s\n",
              m_hDevNotifyVolume, wine_dbgstr_guid(&kGuidDevInterfaceVolume));
    }
    else
    {
        WARN("RegisterDeviceNotificationW(volume) failed, gle=%lu\n", GetLastError());
    }

    if (!::SetTimer(m_hWnd, kDesktopCdromPollTimerId, kDesktopCdromPollIntervalMs, NULL))
        WARN("SetTimer(CDROM poll) failed, gle=%lu\n", GetLastError());

    return hRet;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetWindow(HWND *lphwnd)
{
    if (lphwnd == NULL)
        return E_POINTER;
    *lphwnd = m_hWnd;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::ContextSensitiveHelp(BOOL fEnterMode)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::InsertMenusSB(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SetMenuSB(HMENU hmenuShared, HOLEMENU holemenuRes, HWND hwndActiveObject)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::RemoveMenusSB(HMENU hmenuShared)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SetStatusTextSB(LPCOLESTR lpszStatusText)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::EnableModelessSB(BOOL fEnable)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::TranslateAcceleratorSB(LPMSG lpmsg, WORD wID)
{
    if (!::TranslateAcceleratorW(m_hWnd, m_hAccel, lpmsg))
        return S_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::BrowseObject(LPCITEMIDLIST pidl, UINT wFlags)
{
    /*
     * We should use IShellWindows interface here in order to attempt to
     * find an open shell window that shows the requested pidl and activate it
     */

    DWORD dwFlags = ((wFlags & SBSP_EXPLOREMODE) != 0) ? SH_EXPLORER_CMDLINE_FLAG_E : 0;
    return SHOpenNewFrame(ILClone(pidl), NULL, 0, dwFlags);
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetViewStateStream(DWORD grfMode, IStream **ppStrm)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetControlWindow(UINT id, HWND *lphwnd)
{
    if (lphwnd == NULL)
        return E_POINTER;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SendControlMsg(UINT id, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pret)
{
    if (pret == NULL)
        return E_POINTER;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::QueryActiveShellView(IShellView **ppshv)
{
    if (ppshv == NULL)
        return E_POINTER;
    *ppshv = m_ShellView;
    if (*ppshv != NULL)
        (*ppshv)->AddRef();

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::OnViewWindowActive(IShellView *ppshv)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::SetToolbarItems(LPTBBUTTON lpButtons, UINT nButtons, UINT uFlags)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::GetPropertyBag(long flags, REFIID riid, void **ppv)
{
    ITEMIDLIST deskpidl = {};
    return SHGetViewStatePropertyBag(&deskpidl, L"Desktop", flags | SHGVSPB_ROAM, riid, ppv);
}

HRESULT STDMETHODCALLTYPE CDesktopBrowser::QueryService(REFGUID guidService, REFIID riid, PVOID *ppv)
{
    /* FIXME - handle guidService (SID_STopLevelBrowser for IShellBrowserService etc) */
    return QueryInterface(riid, ppv);
}

LRESULT CDesktopBrowser::_NotifyTray(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HWND hWndTray;
    HRESULT hRet;

    hRet = m_Tray->GetTrayWindow(&hWndTray);
    if (SUCCEEDED(hRet))
        ::PostMessageW(hWndTray, uMsg, wParam, lParam);

    return 0;
}

LRESULT CDesktopBrowser::OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    switch (LOWORD(wParam))
    {
        case FCIDM_DESKBROWSER_CLOSE:
            return _NotifyTray(TWM_DOEXITWINDOWS, 0, 0);
        case FCIDM_DESKBROWSER_FOCUS:
            if (GetKeyState(VK_SHIFT))
                return _NotifyTray(TWM_CYCLEFOCUS, 1, 0xFFFFFFFF);
            else
                return _NotifyTray(TWM_CYCLEFOCUS, 1, 1);
        case FCIDM_DESKBROWSER_SEARCH:
            SHFindFiles(NULL, NULL);
            break;
        case FCIDM_DESKBROWSER_REFRESH:
            if (m_ShellView)
                m_ShellView->Refresh();
            break;
        default:
            if (IsDesktopBrowserForwardShellViewCmd(LOWORD(wParam)) && m_hWndShellView)
                return SendMessageW(m_hWndShellView, uMsg, wParam, lParam);
            break;
    }
    return 0;
}


LRESULT CDesktopBrowser::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    return (LRESULT)PaintDesktop((HDC)wParam);
}

LRESULT CDesktopBrowser::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (wParam == SIZE_MINIMIZED)
    {
        /* Hey, we're the desktop!!! */
        ::ShowWindow(m_hWnd, SW_RESTORE);
    }

    ::InvalidateRect(m_hWndShellView, NULL, TRUE);

    return 0;
}

LRESULT CDesktopBrowser::OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (uMsg == WM_SETTINGCHANGE /* == WM_WININICHANGE */ &&
        lstrcmpiW((LPCWSTR)lParam, L"Environment") == 0)
    {
        LPVOID lpEnvironment;
        RegenerateUserEnvironment(&lpEnvironment, TRUE);
    }
    SHSettingsChanged((LPCVOID)wParam, (PCWSTR)lParam); // Invalidate cached restrictions

    if (m_hWndShellView)
    {
        /* Forward the message */
        ::SendMessageW(m_hWndShellView, uMsg, wParam, lParam);
    }

    if (uMsg == WM_SETTINGCHANGE && wParam == SPI_SETWORKAREA && m_hWndShellView != NULL)
    {
        _Resize();
    }

    return 0;
}

LRESULT CDesktopBrowser::OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    return _NotifyTray(TWM_DOEXITWINDOWS, 0, 0);
}

LRESULT CDesktopBrowser::OnOpenNewWindow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    TRACE("Proxy Desktop message 1035 received.\n");
    SHOnCWMCommandLine((HANDLE)lParam);
    return 0;
}

LRESULT CDesktopBrowser::OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    ::SetFocus(m_hWndShellView);
    return 0;
}

// Message WM_DESKTOP_GET_CNOTIFY_SERVER: Get or create the change notification server.
//   wParam: BOOL bCreate; The flag whether it creates or not.
//   lParam: Ignored.
//   return: The window handle of the server window.
LRESULT CDesktopBrowser::OnGetChangeNotifyServer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    BOOL bCreate = (BOOL)wParam;
    if (bCreate && !::IsWindow(m_hwndChangeNotifyServer))
    {
        HRESULT hres = CChangeNotifyServer_CreateInstance(IID_PPV_ARG(IOleWindow, &m_ChangeNotifyServer));
        if (FAILED_UNEXPECTEDLY(hres))
            return NULL;

        hres = m_ChangeNotifyServer->GetWindow(&m_hwndChangeNotifyServer);
        if (FAILED_UNEXPECTEDLY(hres))
            return NULL;
    }
    return (LRESULT)m_hwndChangeNotifyServer;
}

VOID CDesktopBrowser::UpdateCdromMediaState(UINT_PTR EventTag, BOOL IsTimerPoll)
{
    UNREFERENCED_PARAMETER(EventTag);
    UNREFERENCED_PARAMETER(IsTimerPoll);

    DWORD nextCdromDriveMask = QueryCdromDriveMask();
    if ((nextCdromDriveMask | m_dwCdromDriveMask) == 0)
    {
        m_dwCdromDriveMask = 0;
        m_dwCdromMediaMask = 0;
        return;
    }

    DWORD nextCdromMediaMask = QueryCdromMediaMask();
    DWORD mediaRemoved = m_dwCdromMediaMask & ~nextCdromMediaMask;
    DWORD mediaInserted = ~m_dwCdromMediaMask & nextCdromMediaMask;
    DWORD driveTypeChanged = m_dwCdromDriveMask ^ nextCdromDriveMask;

    if (!(mediaRemoved | mediaInserted | driveTypeChanged))
    {
        m_dwCdromDriveMask = nextCdromDriveMask;
        return;
    }

    for (INT iDrive = 0; iDrive <= 'Z' - 'A'; ++iDrive)
    {
        WCHAR szPath[MAX_PATH];
        DWORD dwBit = (1u << iDrive);

        PathBuildRootW(szPath, iDrive);

        if (mediaRemoved & dwBit)
            SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATHW, szPath, NULL);
        if (mediaInserted & dwBit)
            SHChangeNotify(SHCNE_MEDIAINSERTED, SHCNF_PATHW, szPath, NULL);
    }

    if (driveTypeChanged)
    {
        /* Force a broad refresh when CDROM classification changed without a letter topology change. */
        PIDLIST_ABSOLUTE pidlMyComputer = NULL;
        if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &pidlMyComputer)) && pidlMyComputer)
        {
            SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST, pidlMyComputer, NULL);
            ILFree(pidlMyComputer);
        }
    }

    m_dwCdromDriveMask = nextCdromDriveMask;
    m_dwCdromMediaMask = nextCdromMediaMask;
}

BOOL CDesktopBrowser::UpdateDriveTopology(UINT_PTR EventTag, BOOL IsTimerPoll, BOOL AllowRemove)
{
    UNREFERENCED_PARAMETER(IsTimerPoll);

    DWORD dwDrives = ::GetLogicalDrives();
    DWORD nextDrives = m_dwDrives;
    BOOL changed = FALSE;

    for (INT iDrive = 0; iDrive <= 'Z' - 'A'; ++iDrive)
    {
        WCHAR szPath[MAX_PATH];
        DWORD dwBit = (1u << iDrive);

        if (!(m_dwDrives & dwBit) && (dwDrives & dwBit))
        {
            PathBuildRootW(szPath, iDrive);
            SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATHW, szPath, NULL);
            nextDrives |= dwBit;
            changed = TRUE;
        }
        else if ((m_dwDrives & dwBit) && !(dwDrives & dwBit))
        {
            PathBuildRootW(szPath, iDrive);
            if (AllowRemove)
            {
                SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATHW, szPath, NULL);
                nextDrives &= ~dwBit;
                changed = TRUE;
            }
            else
            {
                TRACE("WM_DEVICECHANGE: suppressed drive remove %s on non-final event=0x%Ix\n",
                      debugstr_w(szPath),
                      EventTag);
            }
        }
    }

    m_dwDrives = nextDrives;
    return changed;
}

// Detect drive topology changes from PnP notifications.
LRESULT CDesktopBrowser::OnDeviceChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    BOOL topologyChanged;
    BOOL shouldProbeMedia;
    const BOOL isRemoveComplete = (wParam == DBT_DEVICEREMOVECOMPLETE);
    const DEV_BROADCAST_HDR *pHdr = reinterpret_cast<const DEV_BROADCAST_HDR *>(lParam);
    if (pHdr && pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
    {
        const DEV_BROADCAST_DEVICEINTERFACE_W *pIf = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W *>(pHdr);
        TRACE("WM_DEVICECHANGE: wParam=0x%Ix type=interface class=%s name=%s\n",
              static_cast<UINT_PTR>(wParam),
              wine_dbgstr_guid(&pIf->dbcc_classguid),
              debugstr_w(pIf->dbcc_name));
    }
    else
    {
        TRACE("WM_DEVICECHANGE: wParam=0x%Ix devtype=0x%lx\n",
              static_cast<UINT_PTR>(wParam),
              pHdr ? pHdr->dbch_devicetype : 0);
    }

    if (wParam != DBT_DEVICEARRIVAL &&
        wParam != DBT_DEVICEREMOVECOMPLETE &&
        wParam != DBT_DEVNODES_CHANGED)
        return 0;

    topologyChanged = UpdateDriveTopology(static_cast<UINT_PTR>(wParam), FALSE, isRemoveComplete);
    shouldProbeMedia = topologyChanged || (wParam == DBT_DEVNODES_CHANGED);

    if (!shouldProbeMedia && pHdr)
    {
        if (pHdr->dbch_devicetype == DBT_DEVTYP_VOLUME)
        {
            shouldProbeMedia = TRUE;
        }
        else if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
        {
            const DEV_BROADCAST_DEVICEINTERFACE_W *pIf = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W *>(pHdr);
            shouldProbeMedia = IsEqualGUID(pIf->dbcc_classguid, kGuidDevInterfaceCdrom) ||
                               IsEqualGUID(pIf->dbcc_classguid, kGuidDevInterfaceVolume);
        }
    }

    if (shouldProbeMedia)
        UpdateCdromMediaState(static_cast<UINT_PTR>(wParam), FALSE);

    return 0;
}

LRESULT CDesktopBrowser::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (wParam == kDesktopCdromPollTimerId)
    {
        BOOL topologyChanged = UpdateDriveTopology(static_cast<UINT_PTR>(wParam), TRUE, TRUE);
        if (topologyChanged || m_dwCdromDriveMask != 0)
            UpdateCdromMediaState(static_cast<UINT_PTR>(wParam), TRUE);
    }
    return 0;
}

extern VOID WINAPI ShowFolderOptionsDialog(UINT Page, BOOL Async);

LRESULT CDesktopBrowser::OnShowOptionsDlg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    switch (wParam)
    {
        case 0:
#if (NTDDI_VERSION >= NTDDI_VISTA)
        case 2:
        case 7:
#endif
            ShowFolderOptionsDialog((UINT)(UINT_PTR)wParam, TRUE);
            break;
        case 1:
            _NotifyTray(WM_COMMAND, TRAYCMD_TASKBAR_PROPERTIES, 0);
            break;
    }
    return 0;
}

LRESULT CDesktopBrowser::OnSaveState(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
    if (m_ShellView && !SHRestricted(REST_NOSAVESET))
        m_ShellView->SaveViewState();
    return 0;
}

HRESULT CDesktopBrowser_CreateInstance(IShellDesktopTray *Tray, REFIID riid, void **ppv)
{
    return ShellObjectCreatorInit<CDesktopBrowser, IShellDesktopTray*>(Tray, riid, ppv);
}

/*************************************************************************
 * SHCreateDesktop            [SHELL32.200]
 *
 */
HANDLE WINAPI SHCreateDesktop(IShellDesktopTray *Tray)
{
    if (Tray == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    CComPtr<IShellBrowser> Browser;
    HRESULT hr = CDesktopBrowser_CreateInstance(Tray, IID_PPV_ARG(IShellBrowser, &Browser));
    if (FAILED_UNEXPECTEDLY(hr))
        return NULL;

    return static_cast<HANDLE>(Browser.Detach());
}

/*************************************************************************
 * SHCreateDesktop            [SHELL32.201]
 *
 */
BOOL WINAPI SHDesktopMessageLoop(HANDLE hDesktop)
{
    if (hDesktop == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    MSG Msg;
    BOOL bRet;

    CComPtr<IShellBrowser> browser;
    CComPtr<IShellView> shellView;

    browser.Attach(static_cast<IShellBrowser*>(hDesktop));
    HRESULT hr = browser->QueryActiveShellView(&shellView);
    if (FAILED_UNEXPECTEDLY(hr))
        return FALSE;

    while ((bRet = ::GetMessageW(&Msg, NULL, 0, 0)) != 0)
    {
        if (bRet != -1)
        {
            if (shellView->TranslateAcceleratorW(&Msg) != S_OK)
            {
                ::TranslateMessage(&Msg);
                ::DispatchMessageW(&Msg);
            }
        }
    }

    return TRUE;
}

/*************************************************************************
 *  SHIsTempDisplayMode [SHELL32.724]
 *
 * Is the current display settings temporary?
 */
EXTERN_C BOOL WINAPI SHIsTempDisplayMode(VOID)
{
    TRACE("\n");

    if (GetSystemMetrics(SM_REMOTESESSION) || GetSystemMetrics(SM_REMOTECONTROL))
        return FALSE;

    DEVMODEW DevMode;
    ZeroMemory(&DevMode, sizeof(DevMode));
    DevMode.dmSize = sizeof(DevMode);

    if (!EnumDisplaySettingsW(NULL, ENUM_REGISTRY_SETTINGS, &DevMode))
        return FALSE;

    if (!DevMode.dmPelsWidth || !DevMode.dmPelsHeight)
        return FALSE;

    HDC hDC = GetDC(NULL);
    DWORD cxWidth = GetDeviceCaps(hDC, HORZRES);
    DWORD cyHeight = GetDeviceCaps(hDC, VERTRES);
    ReleaseDC(NULL, hDC);

    return (cxWidth != DevMode.dmPelsWidth || cyHeight != DevMode.dmPelsHeight);
}
