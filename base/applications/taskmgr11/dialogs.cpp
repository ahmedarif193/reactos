/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Win11-styled modal dialogs (confirm, run task, affinity)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define DLG_CLASS L"TM11Dialog"

/* ------------------------------------------------------------------ */
/*  Base modal dialog                                                  */
/* ------------------------------------------------------------------ */

struct Dlg
{
    HWND hwnd;
    HWND owner;
    BOOL done;
    UINT result;         /* 0 cancel, 1 primary */
    BtnStrip btns;
    WCHAR title[96];

    Dlg() : hwnd(NULL), owner(NULL), done(FALSE), result(0) { title[0] = 0; }
    virtual ~Dlg() {}

    virtual void OnCreate() {}
    virtual void OnFocus() {}
    virtual int  BodyHeight() = 0;                /* content below title  */
    virtual void PaintBody(HDC dc, const RECT& body) = 0;
    virtual BOOL OnMouseBody(UINT msg, POINT pt) { (void)msg; (void)pt; return FALSE; }
    virtual void OnCommandId(int id)
    {
        result = (id == 1) ? 1 : 0;
        done = TRUE;
    }
    virtual void OnKey(WPARAM vk)
    {
        if (vk == VK_ESCAPE) { result = 0; done = TRUE; }
        if (vk == VK_RETURN) { result = 1; done = TRUE; }
    }

    /* top padding below the (system-drawn) caption */
    int TitleH() { return S(10); }
    void BodyRect(RECT* r)
    {
        GetClientRect(hwnd, r);
        r->top += TitleH();
        r->bottom -= S(56);   /* button row */
    }
};

static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Dlg* d = (Dlg*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        d = (Dlg*)cs->lpCreateParams;
        d->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        return TRUE;
    }
    case WM_CREATE:
        d->btns.hwnd = hwnd;
        d->OnCreate();
        return 0;

    case WM_SIZE:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT ba = { rc.left + S(16), rc.bottom - S(52), rc.right - S(16), rc.bottom - S(8) };
        d->btns.LayoutRight(ba, S(8));
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        BufPaint bp;
        HDC dc = bp.Begin(hdc, &ps.rcPaint);

        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect32(dc, ps.rcPaint, g_t.cardBg);

        RECT body;
        d->BodyRect(&body);
        body.left += S(18);
        body.right -= S(18);
        d->PaintBody(dc, body);

        /* button row separator area uses window bg tint */
        RECT br = { rc.left, rc.bottom - S(56), rc.right, rc.bottom };
        FillRect32(dc, br, g_t.dark ? Blend(g_t.cardBg, RGB(0,0,0), 18)
                                    : Blend(g_t.cardBg, RGB(0,0,0), 3));
        d->btns.Paint(dc);

        bp.End();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int id = d->btns.OnMouse(msg, pt);
        if (id)
        {
            d->OnCommandId(id);
            return 0;
        }
        if (d->OnMouseBody(msg, pt))
            return 0;
        return 0;
    }

    case WM_KEYDOWN:
        d->OnKey(wp);
        return 0;

    case WM_CLOSE:
        d->result = 0;
        d->done = TRUE;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterDlgClass(void)
{
    static BOOL s_reg = FALSE;
    if (s_reg) return;
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = DLG_CLASS;
    RegisterClassW(&wc);
    s_reg = TRUE;
}

static UINT RunDlg(Dlg* d, HWND owner, int w, int h)
{
    RegisterDlgClass();
    d->owner = owner;

    /* w/h describe the client area; the system caption/frame is added on top */
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT adj = { 0, 0, w, h };
    AdjustWindowRectEx(&adj, style, FALSE, exStyle);
    int ww = adj.right - adj.left;
    int wh = adj.bottom - adj.top;

    RECT orc;
    GetWindowRect(owner, &orc);
    int x = orc.left + ((orc.right - orc.left) - ww) / 2;
    int y = orc.top + ((orc.bottom - orc.top) - wh) / 2;
    HMONITOR mon = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    if (x + ww > mi.rcWork.right) x = mi.rcWork.right - ww;
    if (y + wh > mi.rcWork.bottom) y = mi.rcWork.bottom - wh;

    EnableWindow(owner, FALSE);

    CreateWindowExW(exStyle, DLG_CLASS, d->title, style,
                    x, y, ww, wh, owner, NULL, g_app.hInst, d);
    ShowWindow(d->hwnd, SW_SHOW);
    SetForegroundWindow(d->hwnd);
    SetFocus(d->hwnd);
    d->OnFocus();

    MSG msg;
    while (!d->done && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN &&
            (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE || msg.wParam == VK_TAB))
        {
            d->OnKey(msg.wParam);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(d->hwnd);
    return d->result;
}

/* ------------------------------------------------------------------ */
/*  Confirm dialog                                                     */
/* ------------------------------------------------------------------ */

struct ConfirmDlg : Dlg
{
    const ConfirmOpts* o;
    int  bodyH;
    BOOL check;
    RECT rcCheck;
    BOOL hotCheck;

    ConfirmDlg() : o(NULL), bodyH(0), check(FALSE), hotCheck(FALSE)
    { SetRectEmpty(&rcCheck); }

    void OnCreate()
    {
        btns.Add(2, o->secondary ? o->secondary : L"Cancel", IC_NONE, BS_OUTLINE);
        btns.Add(1, o->primary ? o->primary : L"OK", IC_NONE, BS_ACCENT);
    }

    int BodyHeight() { return bodyH; }

    void PaintBody(HDC dc, const RECT& body)
    {
        RECT tr = body;
        DrawTextClip(dc, o->body, tr, g_t.fBody, g_t.textSec, DT_LEFT | DT_WORDBREAK);

        if (o->checkbox)
        {
            RECT cr = { body.left, body.bottom - S(28), body.left + S(20), body.bottom - S(8) };
            rcCheck = cr;
            rcCheck.right = body.right;
            /* box */
            FillRoundRect(dc, cr, check ? g_t.accent : g_t.inputBg,
                          check ? g_t.accent : g_t.textSec, S(4));
            if (check)
            {
                RECT gr = { cr.left + S(3), cr.top + S(3), cr.right - S(3), cr.bottom - S(3) };
                DrawGlyph(dc, gr, IC_CHECK, g_t.accentText);
            }
            RECT lr = { cr.right + S(30) - S(20), cr.top, body.right, cr.bottom };
            lr.left = cr.left + S(28);
            DrawTextClip(dc, o->checkbox, lr, g_t.fBody, g_t.textMain,
                         DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
    }

    BOOL OnMouseBody(UINT msg, POINT pt)
    {
        if (o->checkbox && msg == WM_LBUTTONUP && PtInRect(&rcCheck, pt))
        {
            check = !check;
            InvalidateRect(hwnd, &rcCheck, FALSE);
            return TRUE;
        }
        return FALSE;
    }
};

BOOL Dlg_Confirm(HWND owner, const ConfirmOpts& o)
{
    ConfirmDlg d;
    d.o = &o;
    d.check = o.checkState ? *o.checkState : FALSE;
    StringCchCopyW(d.title, _countof(d.title), o.title);

    /* measure body text */
    int w = S(420);
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, g_t.fBody);
    RECT mr = { 0, 0, w - S(36), 0 };
    DrawTextW(dc, o.body, -1, &mr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);

    d.bodyH = mr.bottom + S(16) + (o.checkbox ? S(36) : 0);
    int h = d.TitleH() + d.bodyH + S(56) + S(8);

    UINT r = RunDlg(&d, owner, w, h);
    if (o.checkState) *o.checkState = d.check;
    return r == 1;
}

/* ------------------------------------------------------------------ */
/*  Run new task                                                       */
/* ------------------------------------------------------------------ */

struct RunTaskDlg : Dlg
{
    HWND edit;
    WNDPROC editProc;
    BOOL admin;
    RECT rcCheck;
    WCHAR text[1024];
    static RunTaskDlg* s_this;

    RunTaskDlg() : edit(NULL), editProc(NULL), admin(FALSE)
    { SetRectEmpty(&rcCheck); text[0] = 0; }

    void SnapText() { if (edit) GetWindowTextW(edit, text, _countof(text)); }
    void OnFocus() { if (edit) SetFocus(edit); }
    void OnKey(WPARAM vk)
    {
        if (vk == VK_RETURN) SnapText();
        Dlg::OnKey(vk);
    }

    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        RunTaskDlg* d = s_this;
        if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE))
        {
            d->OnKey(wp);
            return 0;
        }
        if (msg == WM_CHAR && (wp == 13 || wp == 27))
            return 0;
        return CallWindowProcW(d->editProc, h, msg, wp, lp);
    }

    void OnCreate()
    {
        btns.Add(2, L"Cancel", IC_NONE, BS_OUTLINE);
        btns.Add(3, L"Browse...", IC_NONE, BS_OUTLINE);
        btns.Add(1, L"OK", IC_NONE, BS_ACCENT);

        edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                               S(18) + S(2), TitleH() + S(58), S(420) - S(40), S(18),
                               hwnd, (HMENU)(INT_PTR)10, g_app.hInst, NULL);
        s_this = this;
        editProc = (WNDPROC)SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
        SendMessageW(edit, WM_SETFONT, (WPARAM)g_t.fBody, 0);
    }

    int BodyHeight() { return S(130); }

    void PaintBody(HDC dc, const RECT& body)
    {
        RECT ir = { body.left, body.top + S(2), body.right, body.top + S(34) };
        DrawTextClip(dc,
            L"Type the name of a program, folder, document, or Internet resource, "
            L"and Task Manager will open it for you.",
            ir, g_t.fBody, g_t.textSec, DT_LEFT | DT_WORDBREAK);

        /* input frame */
        RECT er = { body.left, body.top + S(48), body.right, body.top + S(78) };
        FillRoundRect(dc, er, g_t.inputBg, g_t.inputBorder, S(6));

        /* admin checkbox */
        RECT cr = { body.left, body.top + S(92), body.left + S(20), body.top + S(112) };
        rcCheck = cr;
        rcCheck.right = body.right;
        FillRoundRect(dc, cr, admin ? g_t.accent : g_t.inputBg,
                      admin ? g_t.accent : g_t.textSec, S(4));
        if (admin)
        {
            RECT gr = { cr.left + S(3), cr.top + S(3), cr.right - S(3), cr.bottom - S(3) };
            DrawGlyph(dc, gr, IC_CHECK, g_t.accentText);
        }
        RECT lr = { cr.left + S(28), cr.top, body.right, cr.bottom };
        DrawTextClip(dc, L"Create this task with administrative privileges.",
                     lr, g_t.fBody, g_t.textMain, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    BOOL OnMouseBody(UINT msg, POINT pt)
    {
        if (msg == WM_LBUTTONUP && PtInRect(&rcCheck, pt))
        {
            admin = !admin;
            InvalidateRect(hwnd, &rcCheck, FALSE);
            return TRUE;
        }
        return FALSE;
    }

    void OnCommandId(int id)
    {
        if (id == 3)
        {
            WCHAR file[MAX_PATH] = L"";
            OPENFILENAMEW ofn;
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Programs\0*.exe;*.com;*.bat;*.cmd\0All files\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = _countof(file);
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn))
            {
                WCHAR quoted[MAX_PATH + 2];
                StringCchPrintfW(quoted, _countof(quoted), L"\"%s\"", file);
                SetWindowTextW(edit, quoted);
                SetFocus(edit);
            }
            return;
        }
        if (id == 1) SnapText();
        Dlg::OnCommandId(id);
    }
};

RunTaskDlg* RunTaskDlg::s_this = NULL;

BOOL Dlg_RunTask(HWND owner, WCHAR* cmd, int cch, BOOL* adminOut)
{
    RunTaskDlg d;
    StringCchCopyW(d.title, _countof(d.title), L"Create new task");
    int w = S(420);
    int h = d.TitleH() + d.BodyHeight() + S(56) + S(8);
    UINT r = RunDlg(&d, owner, w, h);
    if (r == 1)
    {
        StringCchCopyW(cmd, cch, d.text);
        if (adminOut) *adminOut = d.admin;
        if (!cmd[0]) r = 0;
    }
    return r == 1;
}

/* ------------------------------------------------------------------ */
/*  Processor affinity                                                 */
/* ------------------------------------------------------------------ */

struct AffinityDlg : Dlg
{
    ULONG pid;
    DWORD_PTR mask, sysMask;
    int nCpu;
    RECT rcAll;
    BOOL hotIdx;

    AffinityDlg() : pid(0), mask(0), sysMask(0), nCpu(0), hotIdx(FALSE)
    { SetRectEmpty(&rcAll); }

    void OnCreate()
    {
        btns.Add(2, L"Cancel", IC_NONE, BS_OUTLINE);
        btns.Add(1, L"OK", IC_NONE, BS_ACCENT);
    }

    int Rows() { return (nCpu + 1 + 3) / 4; }
    int BodyHeight() { return S(40) + Rows() * S(30) + S(10); }

    void CpuRect(int i, RECT* r)   /* i == -1 -> "All processors" */
    {
        RECT body;
        BodyRect(&body);
        body.left += S(18);
        int idx = i + 1;
        int col = idx % 4, row = idx / 4;
        r->left = body.left + col * S(92);
        r->top = body.top + S(34) + row * S(30);
        r->right = r->left + S(86);
        r->bottom = r->top + S(26);
    }

    void PaintBody(HDC dc, const RECT& body)
    {
        RECT ir = { body.left, body.top + S(2), body.right, body.top + S(30) };
        DrawTextClip(dc, L"Which processors are allowed to run this process?",
                     ir, g_t.fBody, g_t.textSec, DT_LEFT | DT_WORDBREAK);

        for (int i = -1; i < nCpu; i++)
        {
            RECT r;
            CpuRect(i, &r);
            if (i == -1) rcAll = r;

            BOOL on = (i == -1)
                      ? ((mask & sysMask) == sysMask)
                      : ((mask >> i) & 1) != 0;

            RECT cb = { r.left, r.top + S(3), r.left + S(20), r.top + S(23) };
            FillRoundRect(dc, cb, on ? g_t.accent : g_t.inputBg,
                          on ? g_t.accent : g_t.textSec, S(4));
            if (on)
            {
                RECT gr = { cb.left + S(3), cb.top + S(3), cb.right - S(3), cb.bottom - S(3) };
                DrawGlyph(dc, gr, IC_CHECK, g_t.accentText);
            }
            WCHAR lbl[32];
            if (i == -1)
                StringCchCopyW(lbl, _countof(lbl), L"All");
            else
                StringCchPrintfW(lbl, _countof(lbl), L"CPU %d", i);
            RECT lr = { cb.right + S(8), r.top, r.right, r.bottom };
            DrawTextClip(dc, lbl, lr, g_t.fBody, g_t.textMain,
                         DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
    }

    BOOL OnMouseBody(UINT msg, POINT pt)
    {
        if (msg != WM_LBUTTONUP) return FALSE;
        for (int i = -1; i < nCpu; i++)
        {
            RECT r;
            CpuRect(i, &r);
            if (PtInRect(&r, pt))
            {
                if (i == -1)
                {
                    BOOL all = (mask & sysMask) == sysMask;
                    mask = all ? 0 : sysMask;
                }
                else
                {
                    mask ^= ((DWORD_PTR)1 << i);
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return TRUE;
            }
        }
        return FALSE;
    }

    void OnKey(WPARAM vk)
    {
        if (vk == VK_RETURN && !mask) return;   /* need at least one cpu */
        Dlg::OnKey(vk);
    }
};

BOOL Dlg_Affinity(HWND owner, ULONG pid)
{
    AffinityDlg d;
    d.pid = pid;
    if (!Data::GetAffinity(pid, &d.mask, &d.sysMask))
        return FALSE;
    d.nCpu = 0;
    for (DWORD_PTR m = d.sysMask; m; m >>= 1)
        d.nCpu++;
    if (d.nCpu > 32) d.nCpu = 32;

    WCHAR t[96];
    ProcRow* p = Data::FindProc(pid);
    StringCchPrintfW(t, _countof(t), L"Processor affinity: %s",
                     p ? p->image : L"process");
    StringCchCopyW(d.title, _countof(d.title), t);

    int w = S(18) * 2 + 4 * S(92) + S(10);
    int h = d.TitleH() + d.BodyHeight() + S(56) + S(8);
    if (RunDlg(&d, owner, w, h) == 1 && d.mask)
        return Data::SetAffinity(pid, d.mask);
    return FALSE;
}
