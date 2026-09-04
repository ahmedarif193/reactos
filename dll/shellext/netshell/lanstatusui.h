
/// CLSID
/// HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{7007ACCF-3202-11D1-AAD2-00805FC1270E}
// IID B722BCCB-4E68-101B-A2BC-00AA00404770

#define WM_SHOWSTATUSDLG    (WM_USER+10)

typedef struct tagNotificationItem
{
    struct tagNotificationItem *pNext;
    CLSID guidItem;
    UINT uID;
    HWND hwndDlg;
    INetConnection *pNet;
} NOTIFICATION_ITEM;

typedef struct
{
    INetConnection *pNet;
    HWND hwndStatusDlg;         /* LanStatusDlg window */
    HWND hwndDlg;               /* status dialog window */
    DWORD dwAdapterIndex;
    UINT_PTR nIDEvent;
    UINT DHCPEnabled;
    DWORD dwInOctets;
    DWORD dwOutOctets;
    DWORD IpAddress;
    DWORD SubnetMask;
    DWORD Gateway;
    UINT uID;
    UINT Status;
} LANSTATUSUI_CONTEXT;

class CLanStatus:
    public CComCoClass<CLanStatus, &CLSID_ConnectionTray>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IOleCommandTarget
{
    public:
        DECLARE_CLASSFACTORY_SINGLETON(CLanStatus)

        CLanStatus();

        // IOleCommandTarget
        STDMETHOD(QueryStatus)(const GUID *pguidCmdGroup, ULONG cCmds, OLECMD *prgCmds, OLECMDTEXT *pCmdText) override;
        STDMETHOD(Exec)(const GUID *pguidCmdGroup, DWORD nCmdID, DWORD nCmdexecopt, VARIANT *pvaIn, VARIANT *pvaOut) override;

    private:
        HRESULT InitializeNetTaskbarNotifications();
        HRESULT EnumerateTrayConnections();
        HRESULT ShowStatusDialogByCLSID(const GUID *pguidCmdGroup);
        VOID StartRetry();
        VOID StopRetry();
        static LRESULT CALLBACK RetryWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

        CComPtr<INetConnectionManager> m_lpNetMan;
        NOTIFICATION_ITEM *m_pHead;
        HWND m_hwndRetry;

    public:
        DECLARE_NO_REGISTRY()
        DECLARE_CENTRAL_INSTANCE_NOT_AGGREGATABLE(CLanStatus)
        DECLARE_PROTECT_FINAL_CONSTRUCT()

        BEGIN_COM_MAP(CLanStatus)
            COM_INTERFACE_ENTRY_IID(IID_IOleCommandTarget, IOleCommandTarget)
        END_COM_MAP()

};
