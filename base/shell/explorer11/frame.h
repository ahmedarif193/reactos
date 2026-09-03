#ifndef _EXPLORER11_FRAME_H_
#define _EXPLORER11_FRAME_H_

#include <stdio.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#define COBJMACROS

#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <wingdi.h>
#include <winnls.h>
#include <wincon.h>
#include <atlbase.h>
#include <atlcom.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlcoll.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <shellutils.h>
#include <strsafe.h>
#include <windowsx.h>
#include <commctrl.h>

#include <wine/debug.h>

#include "../explorer/resource.h"

extern HINSTANCE g_hInstance;

static inline INT
E11Scale(INT Value)
{
    static INT s_iDpi = 0;
    if (!s_iDpi)
    {
        HDC hdc = GetDC(NULL);
        s_iDpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
        if (hdc)
            ReleaseDC(NULL, hdc);
        if (s_iDpi <= 0)
            s_iDpi = 96;
    }
    return MulDiv(Value, s_iDpi, 96);
}

typedef struct _E11_PALETTE
{
    COLORREF FrameBg;
    COLORREF BarBg;
    COLORREF Text;
    COLORREF DimText;
    COLORREF HotFill;
    COLORREF Accent;
    COLORREF Border;
    COLORREF EditBg;
} E11_PALETTE;

VOID E11GetPalette(E11_PALETTE *pPal);

enum E11_ICON
{
    EI_NONE = 0,
    EI_FOLDER,
    EI_FOLDER_DESKTOP,
    EI_FOLDER_DOCS,
    EI_FOLDER_DOWNLOADS,
    EI_FOLDER_MUSIC,
    EI_FOLDER_PICTURES,
    EI_FOLDER_VIDEOS,
    EI_PC,
    EI_DRIVE,
    EI_DRIVE_CD,
    EI_NETWORK,
    EI_PIN,
    EI_FILE,
    EI_COPY,
    EI_CUT,
    EI_PASTE,
    EI_DELETE,
    EI_RENAME,
    EI_NEWFOLDER,
    EI_PROPERTIES,
    EI_COPYPATH,
    EI_VIEW_XLARGE,
    EI_VIEW_LARGE,
    EI_VIEW_MEDIUM,
    EI_VIEW_SMALL,
    EI_VIEW_LIST,
    EI_VIEW_DETAILS,
    EI_VIEW_TILES,
    EI_REFRESH,
    EI_SETTINGS,
    EI_UNINSTALL,
    EI_SYSPROPS,
    EI_MANAGE,
    EI_MAPDRIVE,
    EI_NETLOC,
    EI_MEDIA,
    EI_OPEN,
    EI_SEARCH
};

VOID E11DrawIcon(HDC hdc, const RECT *prc, int nIcon, const E11_PALETTE *pPal);
VOID E11DrawFluentRes(HDC hdc, const RECT *prc, UINT nResId, COLORREF crTint);
VOID E11DrawIconDim(HDC hdc, const RECT *prc, int nIcon, const E11_PALETTE *pPal, BOOL bDim);
HICON E11CreateAppIcon(int cxIcon);

enum E11_NAVKIND
{
    NAV_ROOT = 0,
    NAV_ITEM
};

typedef struct _E11_NAVITEM
{
    WCHAR szName[64];
    LPITEMIDLIST pidl;
    int nKind;
    int nDepth;
    int nIcon;
    BOOL bExpanded;
    BOOL bPinned;
    BOOL bVisible;
    RECT rc;
    RECT rcChevron;
} E11_NAVITEM;

enum E11_RIBBON_CMD
{
    E11CMD_NONE = 0,
    E11CMD_COPY,
    E11CMD_CUT,
    E11CMD_PASTE,
    E11CMD_DELETE,
    E11CMD_RENAME,
    E11CMD_NEWFOLDER,
    E11CMD_PROPERTIES,
    E11CMD_PIN,
    E11CMD_COPYPATH,
    E11CMD_VIEW_EXTRALARGE,
    E11CMD_VIEW_LARGE,
    E11CMD_VIEW_MEDIUM,
    E11CMD_VIEW_SMALL,
    E11CMD_VIEW_LIST,
    E11CMD_VIEW_DETAILS,
    E11CMD_VIEW_TILES,
    E11CMD_REFRESH,
    E11CMD_OPEN,
    E11CMD_MAPDRIVE,
    E11CMD_ADDNETLOC,
    E11CMD_MEDIA,
    E11CMD_SETTINGS,
    E11CMD_UNINSTALL,
    E11CMD_SYSPROPS,
    E11CMD_MANAGE
};

typedef struct _E11_RIBBONBTN
{
    LPCWSTR pszLabel;
    LPCWSTR pszGroup;
    int nCmd;
    int nIcon;
    BOOL bBig;
    BOOL bDisabled;
    RECT rc;
} E11_RIBBONBTN;

typedef struct _E11_CRUMB
{
    WCHAR szName[64];
    LPITEMIDLIST pidl;
    RECT rc;
} E11_CRUMB;

#define E11_QUICKACCESS_KEY L"Software\\ReactOS\\Explorer11\\QuickAccess"

#define E11M_SEARCHENTER   (WM_APP + 20)
#define E11M_SEARCHESC     (WM_APP + 21)
#define E11M_SEARCHDONE    (WM_APP + 22)
#define E11M_OPENTILE      (WM_APP + 23)
#define E11M_OPENRESULT    (WM_APP + 24)

class CExplorerFrame;

class CNavPane
{
public:
    CExplorerFrame *m_pFrame;
    CAtlArray<E11_NAVITEM> m_Items;
    int m_iHot;
    int m_iSelected;

    CNavPane() : m_pFrame(NULL), m_iHot(-1), m_iSelected(-1) {}
    ~CNavPane();

    VOID Build();
    VOID Layout(const RECT *prcPane);
    VOID Paint(HDC hdc, const RECT *prcPane, const E11_PALETTE *pPal, HFONT hFont, HFONT hFontHeader);
    int HitTest(POINT pt);
    BOOL HitChevron(POINT pt, int *piItem);
    VOID FreeItems();
};

typedef struct _E11_TILE
{
    WCHAR szName[80];
    WCHAR szInfo[96];
    WCHAR szPath[MAX_PATH];
    int nIcon;
    int nGroup;
    ULONGLONG ullTotal;
    ULONGLONG ullFree;
    BOOL bHasBar;
    RECT rc;
} E11_TILE;

typedef struct _E11_SEARCHROW
{
    WCHAR szName[128];
    WCHAR szDir[MAX_PATH];
    BOOL bFolder;
    RECT rc;
} E11_SEARCHROW;

class CThisPCView :
    public CWindowImpl<CThisPCView, CWindow, CControlWinTraits>
{
public:
    DECLARE_WND_CLASS_EX(L"E11ThisPCView", CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, COLOR_WINDOW)

    CExplorerFrame *m_pFrame;
    E11_PALETTE m_Pal;
    HFONT m_hFont;
    HFONT m_hFontHeader;
    CAtlArray<E11_TILE> m_Tiles;
    CAtlArray<E11_SEARCHROW> m_Results;
    BOOL m_bSearchMode;
    BOOL m_bSearching;
    BOOL m_abCollapsed[2];
    RECT m_arcGroup[2];
    int m_anGroupCount[2];
    int m_iHot;
    int m_iSelected;
    int m_iLastClick;
    ULONGLONG m_ullLastClick;
    WCHAR m_szQuery[128];
    HANDLE m_hSearchThread;
    LONG m_lSearchGen;

    CThisPCView() : m_pFrame(NULL), m_hFont(NULL), m_hFontHeader(NULL),
                    m_bSearchMode(FALSE), m_bSearching(FALSE),
                    m_iHot(-1), m_iSelected(-1), m_iLastClick(-1), m_ullLastClick(0),
                    m_hSearchThread(NULL), m_lSearchGen(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        m_abCollapsed[0] = m_abCollapsed[1] = FALSE;
        m_anGroupCount[0] = m_anGroupCount[1] = 0;
        ZeroMemory(m_arcGroup, sizeof(m_arcGroup));
        m_szQuery[0] = 0;
    }

    int Sc(int v) const { return E11Scale(v); }

    VOID SetFonts(HFONT hFont, HFONT hFontHeader) { m_hFont = hFont; m_hFontHeader = hFontHeader; }
    VOID BuildThisPC();
    VOID StartSearch(LPCWSTR pszRoot, LPCWSTR pszQuery);
    VOID StopSearch();
    VOID LayoutTiles();
    int ItemCount() const;

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnSearchDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);

    BEGIN_MSG_MAP(CThisPCView)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(E11M_SEARCHDONE, OnSearchDone)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()
};

#endif
