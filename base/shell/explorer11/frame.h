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

typedef struct _E11_NAVITEM
{
    WCHAR szName[64];
    LPITEMIDLIST pidl;
    int nIndent;
    BOOL bHeader;
    BOOL bPinned;
    RECT rc;
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
    E11CMD_REFRESH
};

typedef struct _E11_RIBBONBTN
{
    LPCWSTR pszLabel;
    int nCmd;
    BOOL bBig;
    BOOL bGroupEnd;
    RECT rc;
} E11_RIBBONBTN;

#define E11_QUICKACCESS_KEY L"Software\\ReactOS\\Explorer11\\QuickAccess"

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
    VOID FreeItems();
};

#endif
