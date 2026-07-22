/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared application header (types, theme, data engine, controls)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#ifndef UNICODE
#error taskmgr11 must be compiled with UNICODE
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <ws2tcpip.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <winsvc.h>
#include <psapi.h>
#include <wtsapi32.h>
#include <iphlpapi.h>
#include <ntddstor.h>
#include <strsafe.h>

extern "C" {
#define NTOS_MODE_USER
#include <ndk/exfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/pofuncs.h>
#include <ndk/rtlfuncs.h>
}

#include <gdiplus.h>

#include "resource.h"

/* ------------------------------------------------------------------ */
/*  Small POD vector (no STL in this codebase)                         */
/* ------------------------------------------------------------------ */

template <typename T>
struct Vec
{
    T*  p;
    int n;
    int cap;

    Vec() : p(NULL), n(0), cap(0) {}
    ~Vec() { Free(); }

    void Free()
    {
        if (p) HeapFree(GetProcessHeap(), 0, p);
        p = NULL; n = 0; cap = 0;
    }

    void Clear() { n = 0; }

    BOOL Reserve(int want)
    {
        if (want <= cap) return TRUE;
        int newCap = cap ? cap * 2 : 16;
        if (newCap < want) newCap = want;
        T* np = p ? (T*)HeapReAlloc(GetProcessHeap(), 0, p, newCap * sizeof(T))
                  : (T*)HeapAlloc(GetProcessHeap(), 0, newCap * sizeof(T));
        if (!np) return FALSE;
        p = np; cap = newCap;
        return TRUE;
    }

    BOOL Trim(int keep)
    {
        if (keep < n) keep = n;
        if (keep >= cap) return TRUE;
        if (!keep)
        {
            Free();
            return TRUE;
        }
        T* np = (T*)HeapReAlloc(GetProcessHeap(), 0, p, keep * sizeof(T));
        if (!np) return FALSE;
        p = np; cap = keep;
        return TRUE;
    }

    T* Add()
    {
        if (!Reserve(n + 1)) return NULL;
        T* it = &p[n++];
        ZeroMemory(it, sizeof(T));
        return it;
    }

    BOOL Push(const T& v)
    {
        if (!Reserve(n + 1)) return FALSE;
        p[n++] = v;
        return TRUE;
    }

    void RemoveAt(int i)
    {
        if (i < 0 || i >= n) return;
        MoveMemory(&p[i], &p[i + 1], (n - 1 - i) * sizeof(T));
        n--;
    }

    T& operator[](int i) { return p[i]; }
    const T& operator[](int i) const { return p[i]; }

private:
    Vec(const Vec&);
    Vec& operator=(const Vec&);
};

/* ------------------------------------------------------------------ */
/*  Theme                                                              */
/* ------------------------------------------------------------------ */

enum ThemeMode { TM_SYSTEM = 0, TM_LIGHT = 1, TM_DARK = 2 };

struct Theme
{
    BOOL     dark;

    COLORREF winBg;         /* window / mica base                     */
    COLORREF captionText;
    COLORREF cardBg;        /* raised card surface                    */
    COLORREF cardBorder;
    COLORREF listBg;        /* list body surface                      */
    COLORREF headerBg;      /* column header                          */
    COLORREF hoverBg;
    COLORREF selBg;
    COLORREF railHover;
    COLORREF railSel;
    COLORREF textMain;
    COLORREF textSec;
    COLORREF textDis;
    COLORREF accent;
    COLORREF accentHover;
    COLORREF accentPressed;
    COLORREF accentText;
    COLORREF divider;
    COLORREF inputBg;
    COLORREF inputBorder;
    COLORREF scrollThumb;
    COLORREF scrollThumbHot;
    COLORREF closeHover;    /* #C42B1C */
    COLORREF dangerText;

    /* graph line colors: 0=CPU 1=Memory 2=Disk 3=Network */
    COLORREF graph[4];

    HFONT fCaption;   /* 12    titlebar                */
    HFONT fTitle;     /* 20 sb page title              */
    HFONT fBody;      /* 13    lists, labels           */
    HFONT fBodySemi;  /* 13 sb group rows, buttons     */
    HFONT fSmall;     /* 11    column names, captions  */
    HFONT fSmallSemi;
    HFONT fMed;       /* 15                            */
    HFONT fMedSemi;
    HFONT fBig;       /* 26 light  perf big values     */
};

extern Theme g_t;

void Theme_Apply(BOOL dark, int dpi);
void Theme_Free(void);
BOOL Theme_SystemPrefersDark(void);

/* graph indices */
#define GR_CPU  0
#define GR_MEM  1
#define GR_DISK 2
#define GR_NET  3

/* ------------------------------------------------------------------ */
/*  App state / settings                                               */
/* ------------------------------------------------------------------ */

enum PageId
{
    PG_PROCESSES = 0,
    PG_PERFORMANCE,
    PG_APPHISTORY,
    PG_STARTUP,
    PG_USERS,
    PG_DETAILS,
    PG_SERVICES,
    PG_SETTINGS,
    PG_COUNT
};

enum UpdateSpeed { SPD_HIGH = 0, SPD_NORMAL = 1, SPD_LOW = 2, SPD_PAUSED = 3 };

struct Settings
{
    DWORD theme;         /* ThemeMode              */
    DWORD startPage;     /* PageId                 */
    DWORD speed;         /* UpdateSpeed            */
    BOOL  onTop;
    BOOL  minOnUse;      /* minimize on End Task   */
    BOOL  hideWhenMin;
    BOOL  navExpanded;
    BOOL  noEffPrompt;   /* don't ask again        */
    BOOL  fullAcctName;
    WINDOWPLACEMENT wp;  /* wp.length==0 if unset  */
};

struct AppState
{
    HINSTANCE hInst;
    HWND      hFrame;
    int       dpi;
    int       page;            /* active PageId */
    BOOL      maximized;
    BOOL      active;          /* frame is foreground-active */
    WCHAR     search[128];     /* current filter, lowercase  */
    Settings  st;
    HICON     hAppIcon;
    HICON     hAppIconSm;
};

extern AppState g_app;

void Settings_Load(void);
void Settings_Save(void);
void App_ApplyTheme(void);       /* re-derive g_t from settings+dpi, repaint all */
void App_SetPage(int pg);
void App_UpdateNow(void);        /* force a data tick + refresh */
UINT App_TimerMs(void);          /* 0 = paused */

/* frame -> pages notification messages */
#define WM_APP_SEARCH      (WM_APP + 1)   /* search text changed          */
#define WM_APP_SELCHANGED  (WM_APP + 2)   /* page selection changed; frame updates command enables */
#define WM_APP_TRAYICON    (WM_APP + 3)
#define WM_APP_THEMECHG    (WM_APP + 4)   /* broadcast after Theme_Apply  */

/* dpi scale */
int S(int px);

/* ------------------------------------------------------------------ */
/*  Formatting helpers (format.cpp lives in render.cpp)                */
/* ------------------------------------------------------------------ */

void FmtBytes(ULONGLONG v, WCHAR* buf, int cch);            /* 8.1 GB    */
void FmtMemMB(ULONGLONG v, WCHAR* buf, int cch);            /* 1,234.5 MB*/
void FmtRate(double bps, WCHAR* buf, int cch);              /* 1.2 MB/s  */
void FmtMbps(double bps, WCHAR* buf, int cch);              /* 8.0 Mbps  */
void FmtPct(double p, WCHAR* buf, int cch);                 /* 3.4%      */
void FmtCpuTime(LONGLONG t100ns, WCHAR* buf, int cch);      /* 0:01:32   */
void FmtUptime(ULONGLONG secs, WCHAR* buf, int cch);        /* 0:03:44:12*/
void FmtThousands(ULONGLONG v, WCHAR* buf, int cch);        /* 71,053    */

/* ------------------------------------------------------------------ */
/*  Rendering helpers                                                  */
/* ------------------------------------------------------------------ */

struct BufPaint
{
    HDC     target;
    HDC     dc;
    HBITMAP bmp, bmpOld;
    RECT    rc;

    BufPaint() : target(NULL), dc(NULL), bmp(NULL), bmpOld(NULL) {}
    HDC Begin(HDC hdcTarget, const RECT* rcPaint);
    void End(void);
};

Gdiplus::Color GP(COLORREF c, BYTE a = 255);
COLORREF Blend(COLORREF a, COLORREF b, int pctB);           /* pctB 0..100 */

void FillRect32(HDC dc, const RECT& r, COLORREF c);
void FillRoundRect(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int radius); /* border=CLR_NONE to skip */
void DrawTextClip(HDC dc, const WCHAR* txt, const RECT& r, HFONT f, COLORREF c, UINT dtFlags);
void DrawVLine(HDC dc, int x, int y0, int y1, COLORREF c);
void DrawHLine(HDC dc, int y, int x0, int x1, COLORREF c);

/* heat map cell background for utilization t in [0..1]; also text color */
COLORREF HeatBg(double t);
COLORREF HeatText(double t);

/* 61-sample history ring for 60-second graphs */
#define HIST_N 61
struct HistRing
{
    float v[HIST_N];
    int   count;      /* number of valid samples (grows to HIST_N) */
    void  Push(float f)
    {
        if (count < HIST_N) { v[count++] = f; return; }
        MoveMemory(&v[0], &v[1], (HIST_N - 1) * sizeof(float));
        v[HIST_N - 1] = f;
    }
    float Last() const { return count ? v[count - 1] : 0.0f; }
    float Max() const
    {
        float m = 0;
        for (int i = 0; i < count; i++) if (v[i] > m) m = v[i];
        return m;
    }
};

struct GraphStyle
{
    COLORREF line;
    COLORREF secondLine;  /* 0 = same as primary          */
    BOOL     grid;        /* draw background grid           */
    BOOL     border;
    double   yMax;        /* value mapped to top; <=0 auto  */
    const HistRing* second; /* optional dotted series       */
};

struct GraphPaint
{
    Gdiplus::Graphics graphics;

    explicit GraphPaint(HDC dc);

private:
    GraphPaint(const GraphPaint&);
    GraphPaint& operator=(const GraphPaint&);
};

void DrawGraph(HDC dc, const RECT& r, const HistRing* h, const GraphStyle& gs);
void DrawGraph(GraphPaint& paint, const RECT& r, const HistRing* h,
               const GraphStyle& gs);

/* ------------------------------------------------------------------ */
/*  Lucide vector icons                                                */
/* ------------------------------------------------------------------ */

enum IconId
{
    IC_NONE = 0,
    IC_HAMBURGER,
    IC_PROCESSES,     /* gauge           */
    IC_PERF,          /* activity        */
    IC_HISTORY,       /* history         */
    IC_STARTUP,       /* rocket          */
    IC_USERS,         /* users           */
    IC_DETAILS,       /* list            */
    IC_SERVICES,      /* wrench          */
    IC_SETTINGS,      /* gear            */
    IC_SEARCH,        /* magnifier       */
    IC_RUNTASK,       /* terminal window */
    IC_ENDTASK,       /* x               */
    IC_LEAF,          /* efficiency mode */
    IC_PAUSE,         /* suspended       */
    IC_CHEV_R,
    IC_CHEV_D,
    IC_CHEV_U,
    IC_MORE,          /* ...             */
    IC_MIN,
    IC_MAX,
    IC_RESTORE,
    IC_CLOSE,
    IC_CHECK,
    IC_COPY,
    IC_DISCONNECT,
    IC_SIGNOUT,
    IC_ENABLE,        /* check           */
    IC_DISABLE,       /* ban             */
    IC_REFRESH,
    IC_OPENFOLDER,
    IC_OPENAPP,
    IC_WINDOW,        /* generic app       */
    IC_COUNT
};

/* draws icon centered in r, stroke color c; size derives from r */
void DrawGlyph(HDC dc, const RECT& r, IconId icon, COLORREF c);

/* ------------------------------------------------------------------ */
/*  Data engine                                                        */
/* ------------------------------------------------------------------ */

#define PF_HASWINDOW   0x0001
#define PF_HUNG        0x0002
#define PF_SUSPENDED   0x0004
#define PF_EFFICIENCY  0x0008
#define PF_SVCHOST     0x0010
#define PF_WINDIR      0x0020   /* image inside %SystemRoot%          */
#define PF_CRITICAL    0x0040   /* known system-critical image        */
#define PF_NEW         0x0080   /* first tick we see it               */

enum ProcCategory { CAT_APP = 0, CAT_BACKGROUND = 1, CAT_WINDOWS = 2 };

/* There can be at most one distinct physical disk per mounted drive letter. */
#define TM_MAX_DISKS 26

struct DiskSnapshot
{
    double    readBps, writeBps;
    double    activePct, responseMs;
    WCHAR     model[160];
    WCHAR     volumes[112];
    WCHAR     type[24];
    WCHAR     interfaceName[24];
    ULONGLONG capacity, formatted;
    DWORD     number;
    BOOL      present;
    BOOL      perfValid;
    BOOL      system;
    BOOL      pageFile;
    HistRing  hTransfer;       /* bytes/s (read + write) */
    HistRing  hActive;         /* 0..100 or empty        */
};

struct ProcExtra
{
    ULONG   pid;
    LONGLONG createTime;
    BOOL    resolved;        /* path/desc/user resolution done   */
    HICON   icon;            /* small icon (owned)               */
    WCHAR   path[MAX_PATH];
    WCHAR   desc[128];       /* version FileDescription          */
    WCHAR   user[96];
    WCHAR   arch[8];         /* x64 / x86                        */
    DWORD   appHistorySlot;  /* cached one-based history index   */
    BOOL    efficiencySet;   /* explicitly enabled by this app   */
    BOOL    inUse;           /* GC mark                          */
};

struct ProcRow
{
    ULONG    pid, ppid;
    ULONG    sessionId;
    WCHAR    image[64];
    LONGLONG cpu100ns;        /* cumulative kernel+user           */
    double   cpuPct;          /* of total CPU capacity, 0..100    */
    ULONGLONG memBytes;       /* private working set (or private commit) */
    ULONGLONG ioBytes;        /* cumulative read+write+other transfer */
    double   diskBps;
    double   netBps;          /* always 0 (not tracked per-process) */
    ULONG    threads, handles;
    LONG     basePri;
    LONGLONG createTime;
    ULONG    flags;           /* PF_*                             */
    int      category;        /* ProcCategory                     */
    HWND     mainWnd;
    WCHAR    wndTitle[128];
    ProcExtra* x;             /* cache entry (owned by engine)    */
};

struct SvcRow
{
    WCHAR name[96];
    WCHAR disp[192];
    WCHAR group[64];
    ULONG pid;
    DWORD state;             /* SERVICE_RUNNING etc. */
};

struct UserRow
{
    DWORD sessionId;
    WCHAR user[96];
    WCHAR state[32];
    double cpuPct;
    ULONGLONG memBytes;
    int   nProc;
};

enum StartupSource { SS_HKCU_RUN, SS_HKLM_RUN, SS_FOLDER_USER, SS_FOLDER_COMMON };

struct StartupRow
{
    WCHAR name[128];
    WCHAR publisher[128];
    WCHAR command[512];
    WCHAR valueName[128];    /* registry value / file name */
    int   source;
    BOOL  enabled;
};

struct AppHistRow
{
    WCHAR    image[64];
    WCHAR    displayName[128];
    WCHAR    path[MAX_PATH];
    LONGLONG cpu100ns;
    ULONGLONG netBytes;
    ULONGLONG notificationBytes;
    HICON     icon;             /* owned by the data engine; not persisted */
    BOOL      iconResolved;
};

struct SysSnapshot
{
    /* static */
    int       nCpu;
    ULONG     pageSize;
    ULONGLONG ramBytes;
    WCHAR     cpuName[96];
    DWORD     cpuMHz;          /* base speed */
    WCHAR     hostName[64];

    /* static hardware detail (0 = unknown / unavailable) */
    int       sockets;
    int       cores;
    ULONG     l1KB, l2KB, l3KB;
    int       virtMode;        /* 0 unknown, 1 virtualization enabled, 2 virtual machine */
    DWORD     ramSpeedMTs;
    int       ramSlotsUsed, ramSlotsTotal;
    BYTE      ramFormFactor;   /* SMBIOS type 17 form factor */
    ULONGLONG ramInstalled;    /* physically installed (SMBIOS); 0 unknown */

    DWORD     cpuCurMHz;       /* per tick; 0 unknown */

    /* per tick */
    double    cpuTotalPct;       /* 0..100 */
    double    cpuKernelPct;
    ULONG     procCount, threadCount, handleCount;
    ULONGLONG upSeconds;

    ULONGLONG memTotal, memInUse, memAvail, memCommit, memCommitLimit;
    ULONGLONG memCached, memPagedPool, memNonPagedPool;

    /* System-wide totals used by the Processes page summary. */
    double    diskReadBps, diskWriteBps;
    int       diskCount;
    DiskSnapshot disks[TM_MAX_DISKS];

    double    netRecvBps, netSendBps;
    WCHAR     netAdapter[160];
    WCHAR     netName[96];
    WCHAR     netType[32];
    WCHAR     netDns[160];
    WCHAR     netIpv4[64];
    WCHAR     netIpv6[96];
    ULONGLONG netLinkBps;
    BOOL      netPresent;
    BOOL      netConnected;

    /* history */
    HistRing  hCpu;        /* 0..100          */
    HistRing  hCpuKernel;  /* 0..100          */
    HistRing  hCpuLogical[64];
    HistRing  hCpuLogicalKernel[64];
    HistRing  hMem;        /* 0..100 used     */
    HistRing  hNetRecv;    /* bytes/s         */
    HistRing  hNetSend;    /* bytes/s         */

    Vec<ProcRow> procs;
};

namespace Data
{
    extern SysSnapshot g;

    void Init(void);
    void Shutdown(void);
    void Tick(void);                       /* collect a full sample */

    const WCHAR* SvchostServices(ULONG pid);   /* comma list or NULL */
    const WCHAR* SvchostGroup(ULONG pid);
    Vec<SvcRow>& Services(void);
    void RefreshServices(void);

    Vec<UserRow>& Users(void);
    void RefreshUsers(void);
    void UpdateUserUsage(void);

    Vec<StartupRow>& StartupItems(void);
    void RefreshStartup(void);
    BOOL SetStartupEnabled(const StartupRow& it, BOOL enable);

    Vec<AppHistRow>& AppHistory(void);
    void ResolveAppHistoryIcons(int budget);
    void ClearAppHistory(void);
    void GetAppHistorySince(FILETIME* since);
    BOOL AppHistoryNetworkAvailable(void);
    BOOL AppHistoryNotificationsAvailable(void);
    BOOL OpenAppHistory(const AppHistRow& app);

    ProcRow* FindProc(ULONG pid);

    /* actions */
    BOOL KillProcess(ULONG pid, BOOL tree);
    BOOL SetEfficiency(ULONG pid, BOOL on);
    BOOL IsEfficiency(const ProcRow& p);
    DWORD GetPriClass(ULONG pid);
    BOOL SetPriClass(ULONG pid, DWORD cls);
    BOOL GetAffinity(ULONG pid, DWORD_PTR* mask, DWORD_PTR* sysMask);
    BOOL SetAffinity(ULONG pid, DWORD_PTR mask);
    BOOL OpenFileLocation(const ProcRow& p);
    void SearchOnline(const WCHAR* term);
    BOOL ShowFileProperties(const ProcRow& p);
    BOOL SvcControl(const WCHAR* name, int op);   /* 0 stop, 1 start, 2 restart */
    BOOL UserDisconnect(DWORD session);
    BOOL UserLogoff(DWORD session);
    BOOL RunTask(const WCHAR* cmd, BOOL admin);
}

/* ------------------------------------------------------------------ */
/*  TreeList control                                                   */
/* ------------------------------------------------------------------ */

#define TLC_RIGHT   0x0001    /* right-aligned values            */
#define TLC_HEAT    0x0002    /* heat-map background + header total */
#define TLC_FIXED   0x0004    /* not user-resizable              */

struct TLColumn
{
    int    id;
    WCHAR  title[48];
    int    width;         /* px (already dpi scaled by page) */
    int    minWidth;
    UINT   flags;
};

#define TLR_ITEM    0
#define TLR_GROUP   1         /* section header "Apps (7)"       */

struct TLRow
{
    LONGLONG key;         /* stable identity for selection     */
    LPARAM   data;        /* page cookie                       */
    BYTE     kind;
    BYTE     depth;
    BYTE     expandable;
    BYTE     expanded;
};

struct ITreeListOwner
{
    virtual void TLCellText(LPARAM data, int col, WCHAR* buf, int cch) = 0;
    virtual double TLCellHeat(LPARAM data, int col) { (void)data; (void)col; return -1.0; }
    virtual COLORREF TLCellHeatBackground(LPARAM data, int col, double heat)
    { (void)data; (void)col; return HeatBg(heat); }
    virtual COLORREF TLCellHeatText(LPARAM data, int col, double heat)
    { (void)data; (void)col; return HeatText(heat); }
    virtual HICON TLRowIcon(LPARAM data) { (void)data; return NULL; }
    virtual IconId TLRowGlyph(LPARAM data) { (void)data; return IC_NONE; }        /* fallback icon glyph */
    virtual IconId TLStatusGlyph(LPARAM data, int col, COLORREF* c) { (void)data; (void)col; (void)c; return IC_NONE; }
    virtual void TLHeaderValue(int col, WCHAR* buf, int cch) { (void)col; buf[0] = 0; (void)cch; }
    virtual double TLHeaderHeat(int col) { (void)col; return -1.0; }
    virtual void TLSecondaryText(LPARAM data, WCHAR* buf, int cch) { (void)data; buf[0] = 0; (void)cch; } /* dimmed suffix in name col */
    virtual void TLOnContext(LPARAM data, POINT scr) { (void)data; (void)scr; }
    virtual void TLOnActivate(LPARAM data) { (void)data; }
    virtual void TLOnSelChanged(void) {}
    virtual void TLOnToggle(LPARAM data, BOOL expanded) { (void)data; (void)expanded; }
    virtual void TLOnSort(int col, BOOL desc) { (void)col; (void)desc; }
    virtual void TLOnDelete(LPARAM data) { (void)data; }
};

void TL_Register(void);
HWND TL_Create(HWND parent, ITreeListOwner* owner, BOOL doubleHeader);
void TL_SetColumns(HWND h, const TLColumn* cols, int n);
void TL_SetRows(HWND h, const TLRow* rows, int n);       /* keeps selection by key */
void TL_Refresh(HWND h);                                 /* repaint (data changed) */
LONGLONG TL_GetSelKey(HWND h);
void TL_SetSelKey(HWND h, LONGLONG key);                 /* select + reveal */
LPARAM  TL_GetSelData(HWND h);
void TL_SetSort(HWND h, int col, BOOL desc);             /* visual indicator */
int  TL_GetSortCol(HWND h, BOOL* desc);
void TL_SetEmptyText(HWND h, const WCHAR* txt);

/* ------------------------------------------------------------------ */
/*  Popup menu (Win11 styled)                                          */
/* ------------------------------------------------------------------ */

#define MIF_SEP       0x0001
#define MIF_DISABLED  0x0002
#define MIF_CHECKED   0x0004   /* checkmark            */
#define MIF_RADIO     0x0008   /* bullet               */
#define MIF_DEFAULT   0x0010   /* bold                 */
#define MIF_DANGER    0x0020   /* red text             */

struct MItem
{
    UINT         id;
    const WCHAR* text;
    UINT         flags;
    const MItem* sub;
    int          nSub;
};

UINT Menu_Show(HWND owner, POINT scrPt, const MItem* items, int n);
/* show below an anchor rect (client coords of owner) e.g. dropdowns */
UINT Menu_ShowBelow(HWND owner, const RECT& rcAnchor, const MItem* items, int n, int minWidth = 0);

/* ------------------------------------------------------------------ */
/*  Shared widgets                                                     */
/* ------------------------------------------------------------------ */

#define BS_SUBTLE   0
#define BS_ACCENT   1
#define BS_OUTLINE  2
#define BS_ICONONLY 3

struct UiBtn
{
    int   id;
    WCHAR text[64];
    IconId icon;
    UINT  style;
    BOOL  enabled;
    RECT  r;          /* set by layout */
};

struct BtnStrip
{
    Vec<UiBtn> b;
    int hot, pressed;
    HWND hwnd;        /* host window for invalidation */

    BtnStrip() : hot(-1), pressed(-1), hwnd(NULL) {}
    void Clear() { b.Clear(); hot = -1; pressed = -1; }
    void Add(int id, const WCHAR* text, IconId icon, UINT style, BOOL enabled = TRUE);
    void SetEnabled(int id, BOOL en);
    BOOL GetEnabled(int id);
    int  LayoutRight(const RECT& area, int gap);  /* returns used width; right-aligned */
    int  LayoutLeft(const RECT& area, int gap);
    void Paint(HDC dc);
    /* returns clicked id on button-up, else 0; call from host wndproc */
    int  OnMouse(UINT msg, POINT pt);
    void ClearHot(void);
};

int BtnMeasure(const UiBtn& btn);    /* preferred width */

/* toggle switch / radio painters for settings page */
void DrawToggle(HDC dc, const RECT& r, BOOL on, BOOL hot, BOOL enabled);
void DrawRadio(HDC dc, const RECT& r, BOOL on, BOOL hot);

/* search box (child window hosting an EDIT) */
#define SB_CLASS L"TM11Search"
HWND Search_Create(HWND parent, const WCHAR* placeholder);
void Search_GetText(HWND h, WCHAR* buf, int cch);
void Search_Clear(HWND h);
void Search_SetPlaceholder(HWND h, const WCHAR* placeholder);
void Search_Focus(HWND h);
void Search_Register(void);

/* ------------------------------------------------------------------ */
/*  Dialogs                                                            */
/* ------------------------------------------------------------------ */

struct ConfirmOpts
{
    const WCHAR* title;
    const WCHAR* body;
    const WCHAR* primary;     /* accent button text; NULL = "OK"   */
    const WCHAR* secondary;   /* NULL = "Cancel"                   */
    const WCHAR* checkbox;    /* optional "don't ask again"        */
    BOOL*        checkState;
    BOOL         danger;      /* primary button red                */
};

BOOL Dlg_Confirm(HWND owner, const ConfirmOpts& o);
BOOL Dlg_RunTask(HWND owner, WCHAR* cmd, int cch, BOOL* admin);
BOOL Dlg_Affinity(HWND owner, ULONG pid);

/* ------------------------------------------------------------------ */
/*  Pages                                                              */
/* ------------------------------------------------------------------ */

/* command ids used by page header button strips */
enum
{
    CMD_RUNTASK = 100,
    CMD_ENDTASK,
    CMD_EFFICIENCY,
    CMD_MORE,
    CMD_STARTUP_ENABLE,
    CMD_STARTUP_DISABLE,
    CMD_USER_DISCONNECT,
    CMD_USER_LOGOFF,
    CMD_SVC_OPENMSC,
    CMD_COPY,
    CMD_OPENAPP,
    CMD_EXPANDALL,
    CMD_COLLAPSEALL,
};

struct Page
{
    HWND hwnd;
    Page() : hwnd(NULL) {}
    virtual ~Page() {}
    virtual const WCHAR* Title() = 0;
    virtual HWND Create(HWND parent) = 0;
    virtual void OnTick() {}                       /* fresh snapshot available   */
    virtual void OnShow(BOOL shown) { (void)shown; }
    virtual void OnThemeChanged() {}
    virtual BOOL WantSearch() { return FALSE; }
    virtual const WCHAR* SearchHint() { return L"Type a name, publisher, or PID to search"; }
    virtual void OnSearch() {}
    virtual void BuildCommands(BtnStrip& s) { (void)s; }
    virtual void UpdateCommands(BtnStrip& s) { (void)s; }  /* enable/disable */
    virtual void OnCommand(int id) { (void)id; }
};

Page* CreateProcessesPage(void);
Page* CreatePerformancePage(void);
Page* CreateAppHistoryPage(void);
Page* CreateStartupPage(void);
Page* CreateUsersPage(void);
Page* CreateDetailsPage(void);
Page* CreateServicesPage(void);
Page* CreateSettingsPage(void);

/* main.cpp helpers used by pages */
void Frame_UpdateCommandStates(void);   /* call when selection changes */
void Frame_SwitchToDetails(ULONG pid);  /* "Go to details"             */
void Frame_SwitchToServices(void);
void DetailsPage_SelectPid(ULONG pid);  /* implemented by details page */

/* shared process actions with Win11 UX (confirm dialogs etc.) */
void Action_EndTask(HWND owner, ProcRow* p, BOOL confirm);
void Action_ToggleEfficiency(HWND owner, ProcRow* p);
BOOL GetFileCompany(const WCHAR* path, WCHAR* buf, int cch);
