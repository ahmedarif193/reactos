/*
 * ReactOS Explorer
 *
 * Copyright 2006 - 2007 Thomas Weidenmueller <w3seek@reactos.org>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "precomp.h"
#include <winver.h>
#include <commoncontrols.h>
#include <regstr.h>
#include <shlwapi_undoc.h>

/* Set DUMP_TASKS to 1 to enable a dump of the tasks and task groups every
   5 seconds */
#define DUMP_TASKS  0
#define DEBUG_SHELL_HOOK 0

#define MAX_TASKS_COUNT (0x7FFF)
#define TASK_ITEM_ARRAY_ALLOC   64

//************************************************************************
// Fullscreen windows (a.k.a. rude apps) checker

#define TIMER_ID_VALIDATE_RUDE_APP 5
#define TIMER_ID_HOVER_PREVIEW 6
#define HOVER_PREVIEW_DELAY 400
#define TSWM_TASKBUTTONMBUTTON (WM_USER + 3)
#define IDM_JUMP_LAUNCH 1
#define IDM_JUMP_CLOSE 2
#define IDM_GROUP_CASCADE 3
#define IDM_GROUP_STACKED 4
#define IDM_GROUP_SIDEBYSIDE 5
#define IDM_GROUP_MINIMIZE 6
#define IDM_GROUP_CLOSE 7
#define IDM_JUMP_PIN 8
#define IDM_JUMP_UNPIN 9
#define VALIDATE_RUDE_INTERVAL 1000
#define VALIDATE_RUDE_MAX_COUNT 5
#define TIMER_ID_BUTTON_ANIM 7
#define BUTTON_ANIM_INTERVAL 15
#define BUTTON_ANIM_DURATION 180
#define TSWM_TASKDRAGBEGIN (WM_USER + 4)
#define TSWM_TASKDRAGMOVE (WM_USER + 5)
#define TSWM_TASKDRAGEND (WM_USER + 6)

static BOOL
SHELL_GetMonitorRect(
    _In_opt_ HMONITOR hMonitor,
    _Out_opt_ PRECT prcDest,
    _In_ BOOL bWorkAreaOnly)
{
    MONITORINFO mi = { sizeof(mi) };
    if (!hMonitor || !::GetMonitorInfoW(hMonitor, &mi))
    {
        if (!prcDest)
            return FALSE;

        if (bWorkAreaOnly)
            ::SystemParametersInfoW(SPI_GETWORKAREA, 0, prcDest, 0);
        else
            ::SetRect(prcDest, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

        return FALSE;
    }

    if (prcDest)
        *prcDest = (bWorkAreaOnly ? mi.rcWork : mi.rcMonitor);
    return TRUE;
}

static BOOL
SHELL_IsParentOwnerOrSelf(_In_ HWND hwndTarget, _In_ HWND hWnd)
{
    for (; hWnd; hWnd = ::GetParent(hWnd))
    {
        if (hWnd == hwndTarget)
            return TRUE;
    }
    return FALSE;
}

static BOOL
SHELL_IsRudeWindowActive(_In_ HWND hWnd)
{
    HWND hwndFore = ::GetForegroundWindow();
    DWORD dwThreadId = ::GetWindowThreadProcessId(hWnd, NULL);
    return dwThreadId == ::GetWindowThreadProcessId(hwndFore, NULL) ||
           SHELL_IsParentOwnerOrSelf(hWnd, hwndFore);
}

static BOOL
SHELL_IsRudeWindow(_In_opt_ HMONITOR hMonitor, _In_ HWND hWnd, _In_ BOOL bDontCheckActive)
{
    if (!::IsWindowVisible(hWnd) || hWnd == ::GetDesktopWindow())
        return FALSE;

    RECT rcMonitor;
    SHELL_GetMonitorRect(hMonitor, &rcMonitor, FALSE);

    DWORD style = ::GetWindowLongPtrW(hWnd, GWL_STYLE);

    RECT rcWnd;
    enum { CHECK_STYLE = WS_THICKFRAME | WS_DLGFRAME | WS_BORDER };
    if ((style & CHECK_STYLE) == CHECK_STYLE)
    {
        ::GetClientRect(hWnd, &rcWnd); // Ignore frame
        ::MapWindowPoints(hWnd, NULL, (PPOINT)&rcWnd, sizeof(RECT) / sizeof(POINT));
    }
    else
    {
        ::GetWindowRect(hWnd, &rcWnd);
    }

    RECT rcUnion;
    ::UnionRect(&rcUnion, &rcWnd, &rcMonitor);

    return ::EqualRect(&rcUnion, &rcWnd) && (bDontCheckActive || SHELL_IsRudeWindowActive(hWnd));
}

////////////////////////////////////////////////////////////////

const WCHAR szTaskSwitchWndClass[] = L"MSTaskSwWClass";
const WCHAR szRunningApps[] = L"Running Applications";

#if DEBUG_SHELL_HOOK
const struct {
    INT msg;
    LPCWSTR msg_name;
} hshell_msg [] = {
        { HSHELL_WINDOWCREATED, L"HSHELL_WINDOWCREATED" },
        { HSHELL_WINDOWDESTROYED, L"HSHELL_WINDOWDESTROYED" },
        { HSHELL_ACTIVATESHELLWINDOW, L"HSHELL_ACTIVATESHELLWINDOW" },
        { HSHELL_WINDOWACTIVATED, L"HSHELL_WINDOWACTIVATED" },
        { HSHELL_GETMINRECT, L"HSHELL_GETMINRECT" },
        { HSHELL_REDRAW, L"HSHELL_REDRAW" },
        { HSHELL_TASKMAN, L"HSHELL_TASKMAN" },
        { HSHELL_LANGUAGE, L"HSHELL_LANGUAGE" },
        { HSHELL_SYSMENU, L"HSHELL_SYSMENU" },
        { HSHELL_ENDTASK, L"HSHELL_ENDTASK" },
        { HSHELL_ACCESSIBILITYSTATE, L"HSHELL_ACCESSIBILITYSTATE" },
        { HSHELL_APPCOMMAND, L"HSHELL_APPCOMMAND" },
        { HSHELL_WINDOWREPLACED, L"HSHELL_WINDOWREPLACED" },
        { HSHELL_WINDOWREPLACING, L"HSHELL_WINDOWREPLACING" },
        { HSHELL_RUDEAPPACTIVATED, L"HSHELL_RUDEAPPACTIVATED" },
};
#endif

typedef struct _TASK_ANIM
{
    INT dx;
    INT dy;
    DWORD dwStart;
    INT SnapX;
    INT SnapY;
    BOOL bSnapped;
} TASK_ANIM, *PTASK_ANIM;

typedef struct _TASK_GROUP
{
    /* We have to use a linked list instead of an array so we don't have to
       update all pointers to groups in the task item array when removing
       groups. */
    struct _TASK_GROUP *Next;

    DWORD dwTaskCount;
    DWORD dwProcessId;
    INT Index;
    INT IconIndex;
    HICON hPinnedIcon;
    HANDLE hLaunchProcess;
    WCHAR szExePath[MAX_PATH];
    WCHAR szLinkPath[MAX_PATH];
    FILETIME PinCreationTime;
    TASK_ANIM Anim;
    union
    {
        DWORD dwFlags;
        struct
        {

            DWORD IsCollapsed : 1;
            DWORD IsPinned : 1;
            DWORD PinSeen : 1;
            DWORD LaunchInProgress : 1;
        };
    };
} TASK_GROUP, *PTASK_GROUP;

typedef struct _TASK_ITEM
{
    HWND hWnd;
    PTASK_GROUP Group;
    INT Index;
    INT IconIndex;
    TASK_ANIM Anim;

    union
    {
        DWORD dwFlags;
        struct
        {

            /* IsFlashing is TRUE when the task bar item should be flashing. */
            DWORD IsFlashing : 1;

            /* RenderFlashed is only TRUE if the task bar item should be
               drawn with a flash. */
            DWORD RenderFlashed : 1;
        };
    };
} TASK_ITEM, *PTASK_ITEM;

typedef struct _TASKBAR_PIN_RECORD
{
    WCHAR szShortcut[MAX_PATH];
    WCHAR szTarget[MAX_PATH];
    FILETIME CreationTime;
} TASKBAR_PIN_RECORD, *PTASKBAR_PIN_RECORD;

static BOOL CALLBACK
CollectTaskbarPin(PCWSTR pszShortcut, PCWSTR pszTarget,
                  const FILETIME *pCreationTime, LPARAM lParam)
{
    CAtlArray<TASKBAR_PIN_RECORD> *pRecords =
        (CAtlArray<TASKBAR_PIN_RECORD> *)lParam;
    TASKBAR_PIN_RECORD Record;

    ZeroMemory(&Record, sizeof(Record));
    if (FAILED(StringCchCopyW(Record.szShortcut, _countof(Record.szShortcut),
                              pszShortcut)) ||
        FAILED(StringCchCopyW(Record.szTarget, _countof(Record.szTarget),
                              pszTarget)))
    {
        return TRUE;
    }
    Record.CreationTime = *pCreationTime;
    pRecords->Add(Record);
    return TRUE;
}

static int __cdecl
CompareTaskbarPins(const void *pLeft, const void *pRight)
{
    const TASKBAR_PIN_RECORD *pA = (const TASKBAR_PIN_RECORD *)pLeft;
    const TASKBAR_PIN_RECORD *pB = (const TASKBAR_PIN_RECORD *)pRight;
    LONG result = CompareFileTime(&pA->CreationTime, &pB->CreationTime);
    return result ? result : lstrcmpiW(pA->szShortcut, pB->szShortcut);
}


class CHardErrorThread
{
    DWORD m_ThreadId;
    HANDLE m_hThread;
    LONG m_bThreadRunning;
    DWORD m_Status;
    DWORD m_dwType;
    CStringW m_Title;
    CStringW m_Text;
public:

    CHardErrorThread():
        m_ThreadId(0),
        m_hThread(NULL),
        m_bThreadRunning(FALSE),
        m_Status(NULL),
        m_dwType(NULL)
    {
    }

    ~CHardErrorThread()
    {
        if (m_bThreadRunning)
        {
            /* Try to unstuck Show */
            PostThreadMessage(m_ThreadId, WM_QUIT, 0, 0);
            DWORD ret = WaitForSingleObject(m_hThread, 3*1000);
            if (ret == WAIT_TIMEOUT)
                TerminateThread(m_hThread, 0);
            CloseHandle(m_hThread);
        }
    }

    HRESULT ThreadProc()
    {
        HRESULT hr;
        CComPtr<IUserNotification> pnotification;

        hr = OleInitialize(NULL);
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        hr = CoCreateInstance(CLSID_UserNotification,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              IID_PPV_ARG(IUserNotification, &pnotification));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        hr = pnotification->SetBalloonInfo(m_Title, m_Text, NIIF_WARNING);
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        hr = pnotification->SetIconInfo(NULL, NULL);
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        /* Show will block until the balloon closes */
        hr = pnotification->Show(NULL, 0);
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        return S_OK;
    }

    static DWORD CALLBACK s_HardErrorThreadProc(IN OUT LPVOID lpParameter)
    {
        CHardErrorThread* pThis = reinterpret_cast<CHardErrorThread*>(lpParameter);
        pThis->ThreadProc();
        CloseHandle(pThis->m_hThread);
        OleUninitialize();
        InterlockedExchange(&pThis->m_bThreadRunning, FALSE);
        return 0;
    }

    void StartThread(PBALLOON_HARD_ERROR_DATA pData)
    {
        BOOL bIsRunning = InterlockedExchange(&m_bThreadRunning, TRUE);

        /* Ignore the new message if we are already showing one */
        if (bIsRunning)
            return;

        m_Status = pData->Status;
        m_dwType = pData->dwType;
        m_Title = (PWCHAR)((ULONG_PTR)pData + pData->TitleOffset);
        m_Text = (PWCHAR)((ULONG_PTR)pData + pData->MessageOffset);
        m_hThread = CreateThread(NULL, 0, s_HardErrorThreadProc, this, 0, &m_ThreadId);
        if (!m_hThread)
        {
            m_bThreadRunning = FALSE;
        }
    }
};

class CTaskToolbar :
    public CWindowImplBaseT< CToolbar<TASK_ITEM>, CControlWinTraits >
{
    BOOL m_bDragArmed;
    BOOL m_bDragging;
    INT m_iPressIndex;
    POINT m_ptPress;

public:
    INT UpdateTbButtonSpacing(IN BOOL bHorizontal, IN BOOL bThemed, IN UINT uiRows = 0, IN UINT uiBtnsPerLine = 0)
    {
        TBMETRICS tbm;

        tbm.cbSize = sizeof(tbm);
        tbm.dwMask = TBMF_BARPAD | TBMF_BUTTONSPACING;

        tbm.cxBarPad = tbm.cyBarPad = 0;

        if (bThemed)
        {
            tbm.cxButtonSpacing = 0;
            tbm.cyButtonSpacing = 0;
        }
        else
        {
            if (bHorizontal || uiBtnsPerLine > 1)
                tbm.cxButtonSpacing = (3 * GetSystemMetrics(SM_CXEDGE) / 2);
            else
                tbm.cxButtonSpacing = 0;

            if (!bHorizontal || uiRows > 1)
                tbm.cyButtonSpacing = (3 * GetSystemMetrics(SM_CYEDGE) / 2);
            else
                tbm.cyButtonSpacing = 0;
        }

        SetMetrics(&tbm);

        return tbm.cxButtonSpacing;
    }

    VOID BeginUpdate()
    {
        SetRedraw(FALSE);
    }

    VOID EndUpdate()
    {
        SendMessageW(WM_SETREDRAW, TRUE);
        InvalidateRect(NULL, TRUE);
    }

    BOOL SetButtonCommandId(IN INT iButtonIndex, IN INT iCommandId)
    {
        TBBUTTONINFO tbbi;

        tbbi.cbSize = sizeof(tbbi);
        tbbi.dwMask = TBIF_BYINDEX | TBIF_COMMAND;
        tbbi.idCommand = iCommandId;

        return SetButtonInfo(iButtonIndex, &tbbi) != 0;
    }

    LRESULT OnNcHitTestToolbar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt;

        /* See if the mouse is on a button */
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ScreenToClient(&pt);

        INT index = HitTest(&pt);
        if (index < 0)
        {
            /* Make the control appear to be transparent outside of any buttons */
            return HTTRANSPARENT;
        }

        bHandled = FALSE;
        return 0;
    }

    LRESULT OnMButtonUpToolbar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        INT index = HitTest(&pt);

        if (index >= 0)
            ::SendMessageW(GetParent(), TSWM_TASKBUTTONMBUTTON, (WPARAM)index, 0);
        return 0;
    }

    static BOOL ExceedsDragThreshold(IN POINT pt, IN POINT ptPress)
    {
        INT dx = pt.x - ptPress.x, dy = pt.y - ptPress.y;

        if (dx < 0)
            dx = -dx;
        if (dy < 0)
            dy = -dy;
        return dx >= GetSystemMetrics(SM_CXDRAG) || dy >= GetSystemMetrics(SM_CYDRAG);
    }

    LRESULT OnMouseMoveToolbar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (m_bDragging)
        {
            ::SendMessageW(GetParent(), TSWM_TASKDRAGMOVE, 0, MAKELPARAM(pt.x, pt.y));
            return 0;
        }

        if (m_bDragArmed && (wParam & MK_LBUTTON) && ExceedsDragThreshold(pt, m_ptPress))
        {
            m_bDragArmed = FALSE;
            SendMessageW(WM_LBUTTONUP, 0, MAKELPARAM(-32000, -32000));
            if (::SendMessageW(GetParent(), TSWM_TASKDRAGBEGIN, (WPARAM)m_iPressIndex,
                               MAKELPARAM(m_ptPress.x, m_ptPress.y)))
            {
                m_bDragging = TRUE;
                SetCapture();
                ::SendMessageW(GetParent(), TSWM_TASKDRAGMOVE, 0, MAKELPARAM(pt.x, pt.y));
                return 0;
            }
        }

        bHandled = FALSE;
        if (m_bTrackGlow)
        {
            INT hot = GetHotItem();
            RECT rc;

            if (hot >= 0 && GetItemRect(hot, &rc))
                InvalidateRect(&rc, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonDownToolbar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        m_iPressIndex = HitTest(&pt);
        m_bDragArmed = m_iPressIndex >= 0;
        m_ptPress = pt;
        bHandled = FALSE;
        return 0;
    }

    LRESULT OnLButtonUpToolbar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        m_bDragArmed = FALSE;
        if (!m_bDragging)
        {
            bHandled = FALSE;
            return 0;
        }
        m_bDragging = FALSE;
        if (::GetCapture() == m_hWnd)
            ::ReleaseCapture();
        ::SendMessageW(GetParent(), TSWM_TASKDRAGEND, 0, 0);
        return 0;
    }

    LRESULT OnCaptureChangedToolbar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        bHandled = FALSE;
        if (m_bDragging)
        {
            m_bDragging = FALSE;
            ::SendMessageW(GetParent(), TSWM_TASKDRAGEND, 0, 0);
        }
        return 0;
    }

    LRESULT OnPaintToolbar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc, hdcMem;
        HBITMAP hbm;
        INT cx, cy;

        if (wParam)
        {
            bHandled = FALSE;
            return 0;
        }

        hdc = BeginPaint(&ps);
        if (!hdc)
            return 0;

        cx = ps.rcPaint.right - ps.rcPaint.left;
        cy = ps.rcPaint.bottom - ps.rcPaint.top;
        hdcMem = (cx > 0 && cy > 0) ? CreateCompatibleDC(hdc) : NULL;
        hbm = hdcMem ? CreateCompatibleBitmap(hdc, cx, cy) : NULL;
        if (hbm)
        {
            HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

            SetViewportOrgEx(hdcMem, -ps.rcPaint.left, -ps.rcPaint.top, NULL);
            SendMessageW(WM_PRINTCLIENT, (WPARAM)hdcMem, PRF_CLIENT);
            SetViewportOrgEx(hdcMem, 0, 0, NULL);
            BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, cx, cy, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbm);
        }
        else if (cx > 0 && cy > 0)
        {
            SendMessageW(WM_PRINTCLIENT, (WPARAM)hdc, PRF_CLIENT);
        }
        if (hdcMem)
            DeleteDC(hdcMem);
        EndPaint(&ps);
        return 0;
    }

    VOID CancelDrag()
    {
        m_bDragArmed = FALSE;
        if (!m_bDragging)
            return;
        m_bDragging = FALSE;
        if (m_hWnd && ::GetCapture() == m_hWnd)
            ::ReleaseCapture();
    }

public:
    BOOL m_bTrackGlow;

    BEGIN_MSG_MAP(CNotifyToolbar)
        MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTestToolbar)
        MESSAGE_HANDLER(WM_MBUTTONUP, OnMButtonUpToolbar)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMoveToolbar)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDownToolbar)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUpToolbar)
        MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChangedToolbar)
        MESSAGE_HANDLER(WM_PAINT, OnPaintToolbar)
    END_MSG_MAP()

    BOOL Initialize(HWND hWndParent)
    {
        DWORD styles = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
            TBSTYLE_TOOLTIPS | TBSTYLE_WRAPABLE | TBSTYLE_LIST | TBSTYLE_TRANSPARENT |
            CCS_TOP | CCS_NORESIZE | CCS_NODIVIDER;

        // HACK & FIXME: CORE-18016
        HWND toolbar = CToolbar::Create(hWndParent, styles);
        m_hWnd = NULL;
        m_bTrackGlow = FALSE;
        m_bDragArmed = FALSE;
        m_bDragging = FALSE;
        m_iPressIndex = -1;
        m_ptPress.x = m_ptPress.y = 0;
        return SubclassWindow(toolbar);
    }
};

class CTaskSwitchWnd :
    public CComCoClass<CTaskSwitchWnd>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public CWindowImpl < CTaskSwitchWnd, CWindow, CControlWinTraits >,
    public IOleWindow,
    public IDropTarget
{
    CTaskToolbar m_TaskBar;

    CComPtr<ITrayWindow> m_Tray;

    UINT m_ShellHookMsg;
    UINT m_TaskbarPinChangedMsg;

    WORD m_TaskItemCount;
    WORD m_AllocatedTaskItems;

    PTASK_GROUP m_TaskGroups;
    PTASK_ITEM m_TaskItems;
    PTASK_ITEM m_ActiveTaskItem;
    BOOL m_bMaterial;
    COLORREF m_crMaterial;
    INT m_cyTaskRow;
    INT m_HoverIndex;
    BOOL m_HoverPreviewPending;
    COLORREF m_crGlowCache;
    INT m_GlowCacheIcon;
    INT m_GlowCacheCount;

    HTHEME m_Theme;
    UINT m_ButtonsPerLine;
    WORD m_ButtonCount;

    HIMAGELIST m_ImageList;

    BOOL m_IsGroupingEnabled;
    BOOL m_IsDestroying;

    INT m_nRudeAppValidationCounter;

    SIZE m_ButtonSize;

    BOOL m_bDragging;
    CStringW m_DropPath;
    CComPtr<IDropTargetHelper> m_DropTargetHelper;
    INT m_DropIndex;
    PTASK_GROUP m_DragGroup;
    HWND m_DragWnd;
    POINT m_DragGrab;
    POINT m_DragPos;
    RECT m_rcDragPaint;
    RECT m_rcAnimBounds;
    BOOL m_bAnimTimer;

    UINT m_uHardErrorMsg;
    CHardErrorThread m_HardErrorThread;

public:
    CTaskSwitchWnd() :
        m_ShellHookMsg(NULL),
        m_TaskbarPinChangedMsg(NULL),
        m_TaskItemCount(0),
        m_AllocatedTaskItems(0),
        m_TaskGroups(NULL),
        m_TaskItems(NULL),
        m_ActiveTaskItem(NULL),
        m_bMaterial(FALSE),
        m_crMaterial(0),
        m_cyTaskRow(0),
        m_HoverIndex(-1),
        m_HoverPreviewPending(FALSE),
        m_crGlowCache(0),
        m_GlowCacheIcon(-1),
        m_GlowCacheCount(0),
        m_Theme(NULL),
        m_ButtonsPerLine(0),
        m_ButtonCount(0),
        m_ImageList(NULL),
        m_IsGroupingEnabled(FALSE),
        m_IsDestroying(FALSE),
        m_nRudeAppValidationCounter(0),
        m_bDragging(FALSE),
        m_DropIndex(-1),
        m_DragGroup(NULL),
        m_DragWnd(NULL),
        m_bAnimTimer(FALSE)
    {
        ZeroMemory(&m_ButtonSize, sizeof(m_ButtonSize));
        ZeroMemory(&m_DragGrab, sizeof(m_DragGrab));
        ZeroMemory(&m_DragPos, sizeof(m_DragPos));
        SetRectEmpty(&m_rcDragPaint);
        SetRectEmpty(&m_rcAnimBounds);
        m_uHardErrorMsg = RegisterWindowMessageW(L"HardError");
        m_TaskbarPinChangedMsg = RegisterWindowMessageW(TASKBAR_PIN_CHANGED_MESSAGE);
    }
    virtual ~CTaskSwitchWnd() { }

    INT GetWndTextFromTaskItem(IN PTASK_ITEM TaskItem, LPWSTR szBuf, DWORD cchBuf)
    {
        /* Get the window text without sending a message so we don't hang if an
           application isn't responding! */
        return InternalGetWindowText(TaskItem->hWnd, szBuf, cchBuf);
    }


#if DUMP_TASKS != 0
    VOID DumpTasks()
    {
        PTASK_GROUP CurrentGroup;
        PTASK_ITEM CurrentTaskItem, LastTaskItem;

        TRACE("Tasks dump:\n");
        if (m_IsGroupingEnabled)
        {
            CurrentGroup = m_TaskGroups;
            while (CurrentGroup != NULL)
            {
                TRACE("- Group PID: 0x%p Tasks: %d Index: %d\n", CurrentGroup->dwProcessId, CurrentGroup->dwTaskCount, CurrentGroup->Index);

                CurrentTaskItem = m_TaskItems;
                LastTaskItem = CurrentTaskItem + m_TaskItemCount;
                while (CurrentTaskItem != LastTaskItem)
                {
                    if (CurrentTaskItem->Group == CurrentGroup)
                    {
                        TRACE("  + Task hwnd: 0x%p Index: %d\n", CurrentTaskItem->hWnd, CurrentTaskItem->Index);
                    }
                    CurrentTaskItem++;
                }

                CurrentGroup = CurrentGroup->Next;
            }

            CurrentTaskItem = m_TaskItems;
            LastTaskItem = CurrentTaskItem + m_TaskItemCount;
            while (CurrentTaskItem != LastTaskItem)
            {
                if (CurrentTaskItem->Group == NULL)
                {
                    TRACE("- Task hwnd: 0x%p Index: %d\n", CurrentTaskItem->hWnd, CurrentTaskItem->Index);
                }
                CurrentTaskItem++;
            }
        }
        else
        {
            CurrentTaskItem = m_TaskItems;
            LastTaskItem = CurrentTaskItem + m_TaskItemCount;
            while (CurrentTaskItem != LastTaskItem)
            {
                TRACE("- Task hwnd: 0x%p Index: %d\n", CurrentTaskItem->hWnd, CurrentTaskItem->Index);
                CurrentTaskItem++;
            }
        }
    }
#endif

    VOID UpdateIndexesAfter(IN INT iIndex, BOOL bInserted)
    {
        PTASK_GROUP CurrentGroup;
        PTASK_ITEM CurrentTaskItem, LastTaskItem;
        INT NewIndex;

        int offset = bInserted ? +1 : -1;

        if (m_TaskGroups)
        {
            /* Update all affected groups */
            CurrentGroup = m_TaskGroups;
            while (CurrentGroup != NULL)
            {
                if (CurrentGroup->IsCollapsed &&
                    CurrentGroup->Index >= iIndex)
                {
                    /* Update the toolbar buttons */
                    NewIndex = CurrentGroup->Index + offset;
                    if (m_TaskBar.SetButtonCommandId(CurrentGroup->Index + offset, NewIndex))
                    {
                        CurrentGroup->Index = NewIndex;
                    }
                    else
                        CurrentGroup->Index = -1;
                }

                CurrentGroup = CurrentGroup->Next;
            }
        }

        /* Update all affected task items */
        if (!m_TaskItemCount)
            return;
        CurrentTaskItem = m_TaskItems;
        LastTaskItem = CurrentTaskItem + m_TaskItemCount;
        while (CurrentTaskItem != LastTaskItem)
        {
            CurrentGroup = CurrentTaskItem->Group;
            if (CurrentGroup != NULL)
            {
                if (!CurrentGroup->IsCollapsed &&
                    CurrentTaskItem->Index >= iIndex)
                {
                    goto UpdateTaskItemBtn;
                }
            }
            else if (CurrentTaskItem->Index >= iIndex)
            {
            UpdateTaskItemBtn:
                /* Update the toolbar buttons */
                NewIndex = CurrentTaskItem->Index + offset;
                if (m_TaskBar.SetButtonCommandId(CurrentTaskItem->Index + offset, NewIndex))
                {
                    CurrentTaskItem->Index = NewIndex;
                }
                else
                    CurrentTaskItem->Index = -1;
            }

            CurrentTaskItem++;
        }
    }


    PTASK_ITEM FirstTaskOfGroup(IN PTASK_GROUP TaskGroup)
    {
        PTASK_ITEM TaskItem, LastItem;

        if (!m_TaskItemCount)
            return NULL;
        TaskItem = m_TaskItems;
        LastItem = TaskItem + m_TaskItemCount;
        while (TaskItem != LastItem)
        {
            if (TaskItem->Group == TaskGroup)
                return TaskItem;
            TaskItem++;
        }
        return NULL;
    }

    BOOL GroupContainsActive(IN PTASK_GROUP TaskGroup)
    {
        return m_ActiveTaskItem != NULL && m_ActiveTaskItem->Group == TaskGroup;
    }

    BOOL GroupHasFlash(IN PTASK_GROUP TaskGroup)
    {
        PTASK_ITEM TaskItem, LastItem;

        if (!m_TaskItemCount)
            return FALSE;
        TaskItem = m_TaskItems;
        LastItem = TaskItem + m_TaskItemCount;
        while (TaskItem != LastItem)
        {
            if (TaskItem->Group == TaskGroup && TaskItem->RenderFlashed)
                return TRUE;
            TaskItem++;
        }
        return FALSE;
    }

    HICON CopyTaskButtonIcon(IN PTASK_GROUP TaskGroup, IN PTASK_ITEM TaskItem = NULL)
    {
        if (TaskGroup && TaskGroup->hPinnedIcon)
            return CopyIcon(TaskGroup->hPinnedIcon);
        if (!TaskItem && TaskGroup && !TaskGroup->IsCollapsed)
            TaskItem = FirstTaskOfGroup(TaskGroup);
        INT Index = TaskItem ? TaskItem->IconIndex : TaskGroup ? TaskGroup->IconIndex : -1;
        return Index >= 0 ? ImageList_GetIcon(m_ImageList, Index, ILD_TRANSPARENT) : NULL;
    }

    HICON GetGroupIcon(IN PTASK_GROUP TaskGroup, OUT BOOL *pbDestroy)
    {
        *pbDestroy = FALSE;
        if (TaskGroup->IsPinned && TaskGroup->hPinnedIcon)
            return TaskGroup->hPinnedIcon;
        PTASK_ITEM First = FirstTaskOfGroup(TaskGroup);
        HICON icon = NULL;
        if (TaskGroup->IsPinned)
        {
            SHFILEINFOW sfi;
            ZeroMemory(&sfi, sizeof(sfi));
            PCWSTR pszIconPath = TaskGroup->szLinkPath[0] ?
                                 TaskGroup->szLinkPath : TaskGroup->szExePath;
            // Fetch the base image; SHGFI_ICON adds a shortcut arrow for large icons.
            DWORD_PTR Images = SHGetFileInfoW(pszIconPath, 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | (UseSmallTaskIcons() ? SHGFI_SMALLICON : SHGFI_LARGEICON));
            if (Images)
            {
                icon = ImageList_GetIcon((HIMAGELIST)Images, sfi.iIcon, ILD_TRANSPARENT);
                *pbDestroy = icon != NULL;
            }
            if (!icon && TaskGroup->szExePath[0])
            {
                HICON hLarge = NULL, hSmall = NULL;

                if (ExtractIconExW(TaskGroup->szExePath, 0, &hLarge, &hSmall, 1))
                {
                    icon = UseSmallTaskIcons() ? hSmall : hLarge;
                    if (!icon)
                        icon = UseSmallTaskIcons() ? hLarge : hSmall;
                    else if (hLarge && hSmall)
                        DestroyIcon(UseSmallTaskIcons() ? hLarge : hSmall);
                    *pbDestroy = icon != NULL;
                }
            }
        }
        if (!icon && First)
            icon = GetWndIcon(First->hWnd);
        if (!icon)
            icon = static_cast<HICON>(LoadImageW(NULL, MAKEINTRESOURCEW(OIC_SAMPLE), IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE));
        if (TaskGroup->IsPinned && icon)
        {
            TaskGroup->hPinnedIcon = *pbDestroy ? icon : CopyIcon(icon);
            if (TaskGroup->hPinnedIcon)
            {
                *pbDestroy = FALSE;
                icon = TaskGroup->hPinnedIcon;
            }
        }
        return icon;
    }

    VOID ReplaceGroupIcon(IN PTASK_GROUP TaskGroup)
    {
        BOOL bDestroy = FALSE;
        HICON hGroupIcon = GetGroupIcon(TaskGroup, &bDestroy);
        INT iImage = hGroupIcon ? ImageList_ReplaceIcon(m_ImageList,
                                                        TaskGroup->IconIndex,
                                                        hGroupIcon)
                                : -1;
        if (bDestroy)
            DestroyIcon(hGroupIcon);
        if (iImage >= 0)
            TaskGroup->IconIndex = iImage;
    }

    INT UpdateTaskGroupButton(IN PTASK_GROUP TaskGroup)
    {
        TBBUTTONINFO tbbi;
        WCHAR szText[2];

        ASSERT(TaskGroup->Index >= 0);

        szText[0] = 0;

        tbbi.cbSize = sizeof(tbbi);
        tbbi.dwMask = TBIF_BYINDEX | TBIF_STATE | TBIF_TEXT | TBIF_IMAGE;
        tbbi.fsState = TBSTATE_ENABLED;
        if (GroupContainsActive(TaskGroup))
            tbbi.fsState |= TBSTATE_CHECKED;
        if (GroupHasFlash(TaskGroup))
            tbbi.fsState |= TBSTATE_MARKED;
        if (!m_Tray->IsHorizontal() || (m_ButtonsPerLine != 0 &&
            (TaskGroup->Index + 1) % m_ButtonsPerLine == 0))
        {
            tbbi.fsState |= TBSTATE_WRAP;
        }
        tbbi.pszText = szText;

        ReplaceGroupIcon(TaskGroup);
        tbbi.iImage = TaskGroup->IconIndex;

        if (!m_TaskBar.SetButtonInfo(TaskGroup->Index, &tbbi))
        {
            TaskGroup->Index = -1;
            return -1;
        }

        return TaskGroup->Index;
    }

    VOID RemoveGroupIcon(IN PTASK_GROUP TaskGroup)
    {
        TBBUTTONINFO tbbi;
        PTASK_ITEM currentTaskItem, LastItem;
        PTASK_GROUP currentGroup;

        if (TaskGroup->IconIndex == -1)
            return;

        tbbi.cbSize = sizeof(tbbi);
        tbbi.dwMask = TBIF_IMAGE;

        currentTaskItem = m_TaskItems;
        LastItem = m_TaskItemCount ? currentTaskItem + m_TaskItemCount : currentTaskItem;
        while (currentTaskItem != LastItem)
        {
            if (currentTaskItem->IconIndex > TaskGroup->IconIndex)
            {
                currentTaskItem->IconIndex--;
                if (currentTaskItem->Index >= 0)
                {
                    tbbi.iImage = currentTaskItem->IconIndex;
                    m_TaskBar.SetButtonInfo(currentTaskItem->Index, &tbbi);
                }
            }
            currentTaskItem++;
        }

        currentGroup = m_TaskGroups;
        while (currentGroup != NULL)
        {
            if (currentGroup != TaskGroup &&
                currentGroup->IconIndex > TaskGroup->IconIndex)
            {
                currentGroup->IconIndex--;
                if (currentGroup->IsCollapsed && currentGroup->Index >= 0)
                {
                    tbbi.iImage = currentGroup->IconIndex;
                    m_TaskBar.SetButtonInfo(currentGroup->Index, &tbbi);
                }
            }
            currentGroup = currentGroup->Next;
        }

        ImageList_Remove(m_ImageList, TaskGroup->IconIndex);
        TaskGroup->IconIndex = -1;
    }

    PTASK_GROUP FindTaskGroupByPath(IN PCWSTR pszExePath)
    {
        PTASK_GROUP TaskGroup = m_TaskGroups;
        while (TaskGroup != NULL)
        {
            if (TaskGroup->szExePath[0] &&
                !lstrcmpiW(TaskGroup->szExePath, pszExePath))
            {
                return TaskGroup;
            }
            TaskGroup = TaskGroup->Next;
        }
        return NULL;
    }

    PTASK_GROUP AppendTaskGroup(IN PCWSTR pszExePath)
    {
        PTASK_GROUP TaskGroup = (PTASK_GROUP)HeapAlloc(hProcessHeap,
                                                       HEAP_ZERO_MEMORY,
                                                       sizeof(*TaskGroup));
        if (!TaskGroup)
            return NULL;

        TaskGroup->Index = -1;
        TaskGroup->IconIndex = -1;
        StringCchCopyW(TaskGroup->szExePath, _countof(TaskGroup->szExePath),
                       pszExePath ? pszExePath : L"");

        PTASK_GROUP *pLink = &m_TaskGroups;
        while (*pLink)
            pLink = &(*pLink)->Next;
        *pLink = TaskGroup;
        return TaskGroup;
    }

    VOID ClearTaskGroupLaunch(IN PTASK_GROUP TaskGroup)
    {
        if (TaskGroup->hLaunchProcess)
        {
            CloseHandle(TaskGroup->hLaunchProcess);
            TaskGroup->hLaunchProcess = NULL;
        }
    }

    VOID FreeTaskGroup(IN PTASK_GROUP TaskGroup)
    {
        if (m_DragGroup == TaskGroup)
            AbortTaskDrag();
        ClearTaskGroupLaunch(TaskGroup);
        if (TaskGroup->hPinnedIcon)
            DestroyIcon(TaskGroup->hPinnedIcon);
        HeapFree(hProcessHeap, 0, TaskGroup);
    }

    VOID UnlinkAndFreeTaskGroup(IN PTASK_GROUP TaskGroup)
    {
        PTASK_GROUP *pLink = &m_TaskGroups;
        while (*pLink && *pLink != TaskGroup)
            pLink = &(*pLink)->Next;
        if (*pLink == TaskGroup)
        {
            *pLink = TaskGroup->Next;
            FreeTaskGroup(TaskGroup);
        }
    }

    VOID DeleteTaskGroupButton(IN PTASK_GROUP TaskGroup)
    {
        INT iIndex = TaskGroup->Index;
        m_TaskBar.BeginUpdate();
        if (iIndex >= 0 && m_TaskBar.DeleteButton(iIndex))
        {
            TaskGroup->Index = -1;
            m_ButtonCount--;
            UpdateIndexesAfter(iIndex, FALSE);
        }
        RemoveGroupIcon(TaskGroup);
        UpdateButtonsSize(TRUE);
        m_TaskBar.EndUpdate();
    }

    VOID SetTaskGroupPin(IN PTASK_GROUP TaskGroup,
                         IN const TASKBAR_PIN_RECORD *pRecord)
    {
        if (pRecord)
        {
            if (!TaskGroup->IsPinned && !TaskGroup->hPinnedIcon)
                TaskGroup->hPinnedIcon = CopyTaskButtonIcon(TaskGroup);
            else if (TaskGroup->IsPinned && lstrcmpiW(TaskGroup->szLinkPath, pRecord->szShortcut))
            {
                if (TaskGroup->hPinnedIcon)
                    DestroyIcon(TaskGroup->hPinnedIcon);
                TaskGroup->hPinnedIcon = NULL;
            }
            TaskGroup->IsPinned = TRUE;
            TaskGroup->PinSeen = TRUE;
            TaskGroup->PinCreationTime = pRecord->CreationTime;
            StringCchCopyW(TaskGroup->szLinkPath,
                           _countof(TaskGroup->szLinkPath),
                           pRecord->szShortcut);
            if (TaskGroup->IsCollapsed && TaskGroup->Index >= 0)
                UpdateTaskGroupButton(TaskGroup);
            else
            {
                TaskGroup->IsCollapsed = FALSE;
                CollapseTaskGroup(TaskGroup);
            }
            return;
        }

        TaskGroup->IsPinned = FALSE;
        if (TaskGroup->hPinnedIcon)
        {
            DestroyIcon(TaskGroup->hPinnedIcon);
            TaskGroup->hPinnedIcon = NULL;
        }
        TaskGroup->PinSeen = FALSE;
        TaskGroup->szLinkPath[0] = 0;
        ZeroMemory(&TaskGroup->PinCreationTime,
                   sizeof(TaskGroup->PinCreationTime));
        if (TaskGroup->dwTaskCount == 0)
        {
            DeleteTaskGroupButton(TaskGroup);
            UnlinkAndFreeTaskGroup(TaskGroup);
        }
        else if (!ShouldCombine(TaskGroup) && TaskGroup->IsCollapsed)
        {
            ExpandTaskGroup(TaskGroup);
        }
        else if (TaskGroup->IsCollapsed && TaskGroup->Index >= 0)
        {
            UpdateTaskGroupButton(TaskGroup);
        }
    }

    HRESULT ApplySavedTaskbarPin(IN PCWSTR pszTarget, IN PCWSTR pszShortcut, IN HICON hIcon)
    {
        TASKBAR_PIN_RECORD Record = {};
        WIN32_FILE_ATTRIBUTE_DATA Attributes;
        HRESULT hr = StringCchCopyW(Record.szTarget, _countof(Record.szTarget), pszTarget);
        if (SUCCEEDED(hr))
            hr = StringCchCopyW(Record.szShortcut, _countof(Record.szShortcut), pszShortcut);
        if (FAILED(hr))
            return hr;
        if (!GetFileAttributesExW(pszShortcut, GetFileExInfoStandard, &Attributes))
            return HRESULT_FROM_WIN32(GetLastError());
        Record.CreationTime = Attributes.ftCreationTime;

        PTASK_GROUP Group = FindTaskGroupByPath(pszTarget);
        if (!Group)
            Group = AppendTaskGroup(pszTarget);
        if (!Group)
            return E_OUTOFMEMORY;
        if (hIcon && !Group->hPinnedIcon)
            Group->hPinnedIcon = CopyIcon(hIcon);
        SetTaskGroupPin(Group, &Record);
        return S_OK;
    }

    VOID SyncTaskbarPins()
    {
        if (m_IsDestroying)
            return;
        CAtlArray<TASKBAR_PIN_RECORD> Records;
        HRESULT hr = TaskbarPin_Enum(CollectTaskbarPin, (LPARAM)&Records);
        if (FAILED(hr))
            return;
        if (Records.GetCount() > 1)
        {
            qsort(Records.GetData(), Records.GetCount(),
                  sizeof(TASKBAR_PIN_RECORD), CompareTaskbarPins);
        }

        PTASK_GROUP TaskGroup = m_TaskGroups;
        while (TaskGroup)
        {
            TaskGroup->PinSeen = FALSE;
            TaskGroup = TaskGroup->Next;
        }

        for (SIZE_T i = 0; i < Records.GetCount(); ++i)
        {
            BOOL bDuplicate = FALSE;
            for (SIZE_T j = 0; j < i; ++j)
            {
                if (!lstrcmpiW(Records[i].szTarget, Records[j].szTarget))
                {
                    bDuplicate = TRUE;
                    break;
                }
            }
            if (bDuplicate)
                continue;

            TaskGroup = FindTaskGroupByPath(Records[i].szTarget);
            if (!TaskGroup)
                TaskGroup = AppendTaskGroup(Records[i].szTarget);
            if (TaskGroup)
                SetTaskGroupPin(TaskGroup, &Records[i]);
        }

        TaskGroup = m_TaskGroups;
        while (TaskGroup)
        {
            PTASK_GROUP Next = TaskGroup->Next;
            if (TaskGroup->IsPinned && !TaskGroup->PinSeen)
            {
                // A shortcut that could not be read is not an unpin operation.
                DWORD Attributes = GetFileAttributesW(TaskGroup->szLinkPath);
                DWORD error = Attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
                BOOL bRemoved = (Attributes == INVALID_FILE_ATTRIBUTES && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) ||
                                (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY));
                for (SIZE_T i = 0; !bRemoved && i < Records.GetCount(); ++i)
                {
                    if (!lstrcmpiW(Records[i].szShortcut, TaskGroup->szLinkPath))
                        bRemoved = TRUE; // The same shortcut now points to another application.
                }
                if (bRemoved)
                    SetTaskGroupPin(TaskGroup, NULL);
            }
            TaskGroup = Next;
        }

        // Use the same order for existing running groups and pins loaded at logon.
        ApplyPinOrder(Records);
        if (Records.GetCount())
            SaveTaskbarPinOrder();
    }

    static BOOL IsGroupListed(IN const CAtlArray<PTASK_GROUP> &List, IN PTASK_GROUP TaskGroup)
    {
        for (SIZE_T i = 0; i < List.GetCount(); ++i)
        {
            if (List[i] == TaskGroup)
                return TRUE;
        }
        return FALSE;
    }

    INT NthPinnedButtonIndex(IN SIZE_T n)
    {
        INT Last = -1;

        for (SIZE_T i = 0; i <= n; ++i)
        {
            INT Best = -1;

            for (PTASK_GROUP TaskGroup = m_TaskGroups; TaskGroup; TaskGroup = TaskGroup->Next)
            {
                if (!TaskGroup->IsPinned || TaskGroup->Index <= Last)
                    continue;
                if (Best < 0 || TaskGroup->Index < Best)
                    Best = TaskGroup->Index;
            }
            if (Best < 0)
                return -1;
            Last = Best;
        }
        return Last;
    }

    VOID ApplyPinOrder(IN const CAtlArray<TASKBAR_PIN_RECORD> &Records)
    {
        CAtlArray<TASKBAR_PIN_ORDER> Saved;
        CAtlArray<PTASK_GROUP> Desired;
        PTASK_GROUP TaskGroup;
        SIZE_T i;

        TaskbarPin_LoadOrder(Saved);
        for (i = 0; i < Saved.GetCount(); ++i)
        {
            TaskGroup = FindTaskGroupByPath(Saved[i].szTarget);
            if (TaskGroup && TaskGroup->IsPinned && TaskGroup->Index >= 0 &&
                !IsGroupListed(Desired, TaskGroup))
            {
                Desired.Add(TaskGroup);
            }
        }
        for (i = 0; i < Records.GetCount(); ++i)
        {
            TaskGroup = FindTaskGroupByPath(Records[i].szTarget);
            if (TaskGroup && TaskGroup->IsPinned && TaskGroup->Index >= 0 &&
                !IsGroupListed(Desired, TaskGroup))
            {
                Desired.Add(TaskGroup);
            }
        }
        for (TaskGroup = m_TaskGroups; TaskGroup; TaskGroup = TaskGroup->Next)
        {
            if (TaskGroup->IsPinned && TaskGroup->Index >= 0 &&
                !IsGroupListed(Desired, TaskGroup))
            {
                Desired.Add(TaskGroup);
            }
        }

        for (i = 0; i < Desired.GetCount(); ++i)
        {
            INT Slot = NthPinnedButtonIndex(i);

            if (Slot >= 0 && Desired[i]->Index != Slot)
                MoveTaskButton(Desired[i]->Index, Slot);
        }
    }

    VOID SaveTaskbarPinOrder()
    {
        CAtlArray<TASKBAR_PIN_ORDER> Order;

        for (INT i = 0; i < (INT)m_ButtonCount; ++i)
        {
            PTASK_GROUP TaskGroup = FindTaskGroupByIndex(i);
            TASKBAR_PIN_ORDER Entry;

            if (!TaskGroup || !TaskGroup->IsPinned || !TaskGroup->szExePath[0])
                continue;
            if (SUCCEEDED(StringCchCopyW(Entry.szTarget, _countof(Entry.szTarget),
                                         TaskGroup->szExePath)))
            {
                Order.Add(Entry);
            }
        }
        TaskbarPin_SaveOrder(Order);
    }

    static INT IndexAfterMove(INT index, INT from, INT to)
    {
        if (index == from)
            return to;
        if (from < to && index > from && index <= to)
            return index - 1;
        if (from > to && index >= to && index < from)
            return index + 1;
        return index;
    }

    VOID MoveTaskButton(INT from, INT to)
    {
        if (from == to || !m_TaskBar.SendMessage(TB_MOVEBUTTON, from, to))
            return;
        TaskPreview_Hide();
        m_HoverIndex = -1;
        for (PTASK_GROUP Group = m_TaskGroups; Group; Group = Group->Next)
        {
            if (Group->IsCollapsed && Group->Index >= 0)
            {
                Group->Index = IndexAfterMove(Group->Index, from, to);
                m_TaskBar.SetButtonCommandId(Group->Index, Group->Index);
            }
        }
        for (SIZE_T i = 0; i < m_TaskItemCount; ++i)
        {
            PTASK_ITEM Item = &m_TaskItems[i];
            if (Item->Index >= 0)
            {
                Item->Index = IndexAfterMove(Item->Index, from, to);
                m_TaskBar.SetButtonCommandId(Item->Index, Item->Index);
            }
        }
        UpdateButtonsSize(TRUE);
    }

    static INT AnimEaseOut(IN INT Elapsed, IN INT Duration)
    {
        INT Left;

        if (Duration <= 0 || Elapsed >= Duration)
            return 1000;
        if (Elapsed <= 0)
            return 0;
        Left = 1000 - (Elapsed * 1000 / Duration);
        return 1000 - (Left * Left * Left / 1000000);
    }

    static BOOL GetAnimOffset(IN const TASK_ANIM *pAnim, OUT POINT *ppt)
    {
        INT Eased;

        ppt->x = ppt->y = 0;
        if (!pAnim->dwStart)
            return FALSE;
        Eased = AnimEaseOut((INT)(GetTickCount() - pAnim->dwStart), BUTTON_ANIM_DURATION);
        if (Eased >= 1000)
            return FALSE;
        ppt->x = pAnim->dx * (1000 - Eased) / 1000;
        ppt->y = pAnim->dy * (1000 - Eased) / 1000;
        return TRUE;
    }

    static VOID ClearAnim(IN OUT TASK_ANIM *pAnim)
    {
        pAnim->dx = pAnim->dy = 0;
        pAnim->dwStart = 0;
        pAnim->bSnapped = FALSE;
    }

    static VOID StartAnim(IN OUT TASK_ANIM *pAnim, IN INT dx, IN INT dy)
    {
        DWORD Now = GetTickCount();

        pAnim->dx = dx;
        pAnim->dy = dy;
        pAnim->dwStart = Now ? Now : 1;
    }

    PTASK_ANIM GetButtonAnim(IN INT Index)
    {
        PTASK_GROUP TaskGroup;
        PTASK_ITEM TaskItem;

        if (Index < 0)
            return NULL;
        TaskGroup = FindTaskGroupByIndex(Index);
        if (TaskGroup)
            return &TaskGroup->Anim;
        TaskItem = FindTaskItemByIndex(Index);
        return TaskItem ? &TaskItem->Anim : NULL;
    }

    BOOL GetSlotStride(OUT POINT *ppt)
    {
        RECT rc0, rc1;

        ppt->x = ppt->y = 0;
        if (!m_ButtonCount || !m_TaskBar.GetItemRect(0, &rc0))
            return FALSE;
        if (m_ButtonCount > 1 && m_TaskBar.GetItemRect(1, &rc1) &&
            (rc1.left != rc0.left || rc1.top != rc0.top))
        {
            ppt->x = rc1.left - rc0.left;
            ppt->y = rc1.top - rc0.top;
            return TRUE;
        }
        if (m_Tray->IsHorizontal())
            ppt->x = rc0.right - rc0.left;
        else
            ppt->y = rc0.bottom - rc0.top;
        return TRUE;
    }

    VOID GetDropShift(IN INT Index, OUT POINT *ppt)
    {
        POINT Stride;

        ppt->x = ppt->y = 0;
        if (m_DropIndex < 0 || Index < m_DropIndex || !GetSlotStride(&Stride))
            return;
        *ppt = Stride;
    }

    VOID GetButtonVisualOffset(IN PTASK_ANIM pAnim, IN INT Index, OUT POINT *ppt)
    {
        POINT ptShift;

        ppt->x = ppt->y = 0;
        if (pAnim)
            GetAnimOffset(pAnim, ppt);
        GetDropShift(Index, &ptShift);
        ppt->x += ptShift.x;
        ppt->y += ptShift.y;
    }

    VOID CaptureButtonPositions()
    {
        RECT rc;
        POINT pt;

        for (INT i = 0; i < (INT)m_ButtonCount; ++i)
        {
            PTASK_ANIM pAnim = GetButtonAnim(i);

            if (!pAnim || !m_TaskBar.GetItemRect(i, &rc))
                continue;
            GetButtonVisualOffset(pAnim, i, &pt);
            pAnim->SnapX = rc.left + pt.x;
            pAnim->SnapY = rc.top + pt.y;
            pAnim->bSnapped = TRUE;
        }
    }

    VOID AnimateButtonPositions()
    {
        RECT rc;
        INT DragIndex = DragButtonIndex();
        BOOL bAny = FALSE;

        for (INT i = 0; i < (INT)m_ButtonCount; ++i)
        {
            PTASK_ANIM pAnim = GetButtonAnim(i);
            INT dx, dy;

            if (!pAnim || !pAnim->bSnapped)
                continue;
            pAnim->bSnapped = FALSE;
            if (i == DragIndex || !m_TaskBar.GetItemRect(i, &rc))
            {
                ClearAnim(pAnim);
                continue;
            }
            POINT ptShift;

            GetDropShift(i, &ptShift);
            dx = pAnim->SnapX - (rc.left + ptShift.x);
            dy = pAnim->SnapY - (rc.top + ptShift.y);
            if (!dx && !dy)
            {
                ClearAnim(pAnim);
                continue;
            }
            StartAnim(pAnim, dx, dy);
            bAny = TRUE;
        }
        if (bAny)
            StartButtonAnimation();
    }

    BOOL HasButtonAnimation()
    {
        POINT pt;
        BOOL bAny = FALSE;

        for (INT i = 0; i < (INT)m_ButtonCount; ++i)
        {
            PTASK_ANIM pAnim = GetButtonAnim(i);

            if (!pAnim || !pAnim->dwStart)
                continue;
            if (GetAnimOffset(pAnim, &pt))
                bAny = TRUE;
            else
                ClearAnim(pAnim);
        }
        return bAny;
    }

    VOID StartButtonAnimation()
    {
        if (!m_bAnimTimer)
            m_bAnimTimer = SetTimer(TIMER_ID_BUTTON_ANIM, BUTTON_ANIM_INTERVAL, NULL) != 0;
        AddAnimBounds();
        m_TaskBar.InvalidateRect(IsRectEmpty(&m_rcAnimBounds) ? NULL : &m_rcAnimBounds, TRUE);
    }

    VOID AddAnimBounds()
    {
        RECT rc, rcVisual;
        POINT pt;

        for (INT i = 0; i < (INT)m_ButtonCount; ++i)
        {
            PTASK_ANIM pAnim = GetButtonAnim(i);

            if (!pAnim || !pAnim->dwStart || !m_TaskBar.GetItemRect(i, &rc))
                continue;
            rcVisual = rc;
            GetDropShift(i, &pt);
            OffsetRect(&rc, pt.x, pt.y);
            GetButtonVisualOffset(pAnim, i, &pt);
            OffsetRect(&rcVisual, pt.x, pt.y);
            UnionRect(&m_rcAnimBounds, &m_rcAnimBounds, &rc);
            UnionRect(&m_rcAnimBounds, &m_rcAnimBounds, &rcVisual);
        }
    }

    VOID InvalidateDragButton()
    {
        RECT rc;
        POINT pt;
        INT Index = DragButtonIndex();

        if (Index < 0 || !m_TaskBar.GetItemRect(Index, &rc) || !GetDragOffset(&pt))
        {
            m_TaskBar.InvalidateRect(NULL, TRUE);
            return;
        }
        OffsetRect(&rc, pt.x, pt.y);
        InvalidateDragPaint();
        m_TaskBar.InvalidateRect(&rc, TRUE);
        m_rcDragPaint = rc;
    }

    VOID InvalidateDragPaint()
    {
        if (IsRectEmpty(&m_rcDragPaint))
            return;
        m_TaskBar.InvalidateRect(&m_rcDragPaint, TRUE);
        SetRectEmpty(&m_rcDragPaint);
    }

    VOID MoveTaskButtonAnimated(IN INT from, IN INT to)
    {
        BOOL bAnimate = IsModernTaskbar();

        if (from == to)
            return;
        if (bAnimate)
            CaptureButtonPositions();
        MoveTaskButton(from, to);
        if (bAnimate)
            AnimateButtonPositions();
    }

    INT DragButtonIndex()
    {
        PTASK_ITEM TaskItem;

        if (!m_bDragging)
            return -1;
        if (m_DragGroup)
            return m_DragGroup->IsCollapsed ? m_DragGroup->Index : -1;
        TaskItem = m_DragWnd ? FindTaskItem(m_DragWnd) : NULL;
        return TaskItem ? TaskItem->Index : -1;
    }

    BOOL GetDragOffset(OUT POINT *ppt)
    {
        RECT rc, rcClient;
        INT Index = DragButtonIndex();
        INT x, y, cx, cy;

        ppt->x = ppt->y = 0;
        if (Index < 0 || !m_TaskBar.GetItemRect(Index, &rc) ||
            !m_TaskBar.GetClientRect(&rcClient))
        {
            return FALSE;
        }

        cx = rc.right - rc.left;
        cy = rc.bottom - rc.top;
        x = m_DragPos.x - m_DragGrab.x;
        y = m_DragPos.y - m_DragGrab.y;
        if (x > rcClient.right - cx)
            x = rcClient.right - cx;
        if (x < rcClient.left)
            x = rcClient.left;
        if (y > rcClient.bottom - cy)
            y = rcClient.bottom - cy;
        if (y < rcClient.top)
            y = rcClient.top;
        ppt->x = x - rc.left;
        ppt->y = y - rc.top;
        return TRUE;
    }

    INT CalcDragTarget(IN INT DragIndex)
    {
        RECT rc, rcDrag;
        POINT pt, ptCentre;
        INT Target = 0;

        if (DragIndex < 0 || !m_TaskBar.GetItemRect(DragIndex, &rcDrag) || !GetDragOffset(&pt))
            return -1;
        ptCentre.x = rcDrag.left + pt.x + (rcDrag.right - rcDrag.left) / 2;
        ptCentre.y = rcDrag.top + pt.y + (rcDrag.bottom - rcDrag.top) / 2;

        for (INT i = 0; i < (INT)m_ButtonCount; ++i)
        {
            BOOL bBefore;

            if (i == DragIndex || !m_TaskBar.GetItemRect(i, &rc))
                continue;
            if (rc.bottom <= ptCentre.y)
                bBefore = TRUE;
            else if (rc.top > ptCentre.y)
                bBefore = FALSE;
            else
                bBefore = ((rc.left + rc.right) / 2) < ptCentre.x;
            if (bBefore)
                ++Target;
        }
        return Target;
    }

    BOOL BeginTaskDrag(IN INT Index, IN POINT ptPress)
    {
        RECT rc;
        PTASK_GROUP TaskGroup;
        PTASK_ITEM TaskItem;

        m_bDragging = FALSE;
        m_DragGroup = NULL;
        m_DragWnd = NULL;
        if (Index < 0 || Index >= (INT)m_ButtonCount)
            return FALSE;
        if (!m_TaskBar.GetItemRect(Index, &rc))
            return FALSE;
        TaskGroup = FindTaskGroupByIndex(Index);
        TaskItem = TaskGroup ? NULL : FindTaskItemByIndex(Index);
        if (!TaskGroup && !TaskItem)
            return FALSE;

        m_DragGroup = TaskGroup;
        m_DragWnd = TaskItem ? TaskItem->hWnd : NULL;
        m_DragGrab.x = ptPress.x - rc.left;
        m_DragGrab.y = ptPress.y - rc.top;
        m_DragPos = ptPress;
        m_bDragging = TRUE;
        SetRectEmpty(&m_rcDragPaint);
        CancelTaskPreview();
        m_HoverIndex = -1;
        UpdateButtonsSize(TRUE);
        return TRUE;
    }

    VOID UpdateTaskDrag(IN POINT pt)
    {
        INT Index = DragButtonIndex(), Target;

        if (!m_bDragging)
            return;
        if (Index < 0)
        {
            EndTaskDrag();
            return;
        }
        m_DragPos = pt;
        Target = CalcDragTarget(Index);
        if (Target >= 0 && Target != Index)
            MoveTaskButtonAnimated(Index, Target);
        InvalidateDragButton();
    }

    VOID EndTaskDrag()
    {
        POINT pt = { 0, 0 };
        INT Index;
        PTASK_ANIM pAnim;

        if (!m_bDragging)
            return;
        Index = DragButtonIndex();
        GetDragOffset(&pt);
        m_bDragging = FALSE;
        m_DragGroup = NULL;
        m_DragWnd = NULL;
        InvalidateDragPaint();
        SaveTaskbarPinOrder();
        pAnim = GetButtonAnim(Index);
        if (pAnim && IsModernTaskbar() && (pt.x || pt.y))
            StartAnim(pAnim, pt.x, pt.y);
        StartButtonAnimation();
    }

    VOID AbortTaskDrag()
    {
        if (!m_bDragging)
            return;
        m_bDragging = FALSE;
        m_DragGroup = NULL;
        m_DragWnd = NULL;
        InvalidateDragPaint();
        m_TaskBar.CancelDrag();
    }

    VOID FreeAllTaskGroups()
    {
        PTASK_GROUP TaskGroup = m_TaskGroups;
        while (TaskGroup)
        {
            PTASK_GROUP Next = TaskGroup->Next;
            FreeTaskGroup(TaskGroup);
            TaskGroup = Next;
        }
        m_TaskGroups = NULL;
    }

    VOID CollapseTaskGroup(IN PTASK_GROUP TaskGroup)
    {
        PTASK_ITEM TaskItem, LastItem;
        TBBUTTON tbBtn = { 0 };
        WCHAR szText[2];
        INT iIndex = -1;

        if (TaskGroup->IsCollapsed)
            return;

        szText[0] = 0;

        m_TaskBar.BeginUpdate();

        LastItem = m_TaskItemCount ? m_TaskItems + m_TaskItemCount : m_TaskItems;
        for (TaskItem = m_TaskItems; TaskItem != LastItem; TaskItem++)
        {
            if (TaskItem->Group == TaskGroup && TaskItem->Index >= 0 &&
                (iIndex < 0 || TaskItem->Index < iIndex))
            {
                iIndex = TaskItem->Index;
            }
        }
        if (iIndex < 0)
            iIndex = m_ButtonCount;

        for (;;)
        {
            PTASK_ITEM Highest = NULL;
            for (TaskItem = m_TaskItems; TaskItem != LastItem; TaskItem++)
            {
                if (TaskItem->Group == TaskGroup && TaskItem->Index >= 0 &&
                    (Highest == NULL || TaskItem->Index > Highest->Index))
                {
                    Highest = TaskItem;
                }
            }
            if (Highest == NULL)
                break;
            DeleteTaskItemButton(Highest);
        }

        TaskGroup->IsCollapsed = TRUE;

        ReplaceGroupIcon(TaskGroup);

        tbBtn.iBitmap = TaskGroup->IconIndex;
        tbBtn.fsState = TBSTATE_ENABLED | TBSTATE_ELLIPSES;
        if (GroupContainsActive(TaskGroup))
            tbBtn.fsState |= TBSTATE_CHECKED;
        tbBtn.fsStyle = BTNS_CHECK | BTNS_NOPREFIX | BTNS_SHOWTEXT;
        tbBtn.iString = (DWORD_PTR)szText;
        tbBtn.idCommand = iIndex;

        if (m_TaskBar.InsertButton(iIndex, &tbBtn))
        {
            UpdateIndexesAfter(iIndex, TRUE);
            TaskGroup->Index = iIndex;
            m_ButtonCount++;
            UpdateButtonsSize(TRUE);
        }
        else
        {
            TaskGroup->Index = -1;
            TaskGroup->IsCollapsed = FALSE;
        }

        m_TaskBar.EndUpdate();
    }

    VOID ExpandTaskGroup(IN PTASK_GROUP TaskGroup)
    {
        PTASK_ITEM TaskItem, LastItem;
        INT iInsert;

        if (!TaskGroup->IsCollapsed)
            return;

        m_TaskBar.BeginUpdate();

        iInsert = TaskGroup->Index;
        if (iInsert >= 0 && m_TaskBar.DeleteButton(iInsert))
        {
            TaskGroup->Index = -1;
            m_ButtonCount--;
            UpdateIndexesAfter(iInsert, FALSE);
        }
        if (iInsert < 0)
            iInsert = m_ButtonCount;
        RemoveGroupIcon(TaskGroup);
        TaskGroup->Index = -1;
        TaskGroup->IsCollapsed = FALSE;

        LastItem = m_TaskItems + m_TaskItemCount;
        for (TaskItem = m_TaskItems; TaskItem != LastItem; TaskItem++)
        {
            TBBUTTON tbBtn = { 0 };
            WCHAR windowText[255];
            HICON icon;

            if (TaskItem->Group != TaskGroup || TaskItem->Index >= 0)
                continue;

            icon = GetWndIcon(TaskItem->hWnd);
            if (!icon)
                icon = static_cast<HICON>(LoadImageW(NULL, MAKEINTRESOURCEW(OIC_SAMPLE), IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE));
            TaskItem->IconIndex = ImageList_ReplaceIcon(m_ImageList, -1, icon);

            tbBtn.iBitmap = TaskItem->IconIndex;
            tbBtn.fsState = TBSTATE_ENABLED | TBSTATE_ELLIPSES;
            tbBtn.fsStyle = BTNS_CHECK | BTNS_NOPREFIX | BTNS_SHOWTEXT;
            windowText[0] = 0;
            if (!IsModernTaskbar())
                GetWndTextFromTaskItem(TaskItem, windowText, _countof(windowText));
            tbBtn.iString = (DWORD_PTR)windowText;
            tbBtn.idCommand = iInsert;

            if (m_TaskBar.InsertButton(iInsert, &tbBtn))
            {
                UpdateIndexesAfter(iInsert, TRUE);
                TaskItem->Index = iInsert;
                m_ButtonCount++;
                UpdateTaskItemButton(TaskItem);
                iInsert++;
            }
        }

        UpdateButtonsSize(TRUE);
        m_TaskBar.EndUpdate();
    }

    BOOL ShouldCombine(IN PTASK_GROUP TaskGroup)
    {
        return TaskGroup && (TaskGroup->IsPinned ||
               (m_IsGroupingEnabled && IsModernTaskbar() && TaskGroup->dwTaskCount > 1));
    }

    VOID UpdateTaskGroupLayout()
    {
        for (PTASK_GROUP Group = m_TaskGroups; Group; Group = Group->Next)
        {
            if (ShouldCombine(Group))
                CollapseTaskGroup(Group);
            else if (Group->IsCollapsed)
                ExpandTaskGroup(Group);
        }
    }

    VOID RefreshTaskButtons()
    {
        for (PTASK_GROUP Group = m_TaskGroups; Group; Group = Group->Next)
        {
            if (Group->IsCollapsed && Group->Index >= 0)
                UpdateTaskGroupButton(Group);
        }

        PTASK_ITEM Last = m_TaskItemCount ? m_TaskItems + m_TaskItemCount : m_TaskItems;
        for (PTASK_ITEM Item = m_TaskItems; Item != Last; ++Item)
        {
            if (Item->Index >= 0)
                UpdateTaskItemButton(Item);
        }
    }

    BOOL IsModernTaskbar()
    {
        return m_bMaterial;
    }

    BOOL UseSmallTaskIcons()
    {
        return g_TaskbarSettings.bSmallIcons && !IsModernTaskbar();
    }

    VOID ApplyTaskRowHeight()
    {
        DWORD dwSize;
        INT cxIcon, cyIcon, cyPad, cyWant;

        if (!m_ImageList)
            return;

        if (!ImageList_GetIconSize(m_ImageList, &cxIcon, &cyIcon))
            return;

        /* TB_SETBITMAPSIZE is also the way back from the synthetic modern
           row height: it makes the toolbar recalculate its natural metrics. */
        m_TaskBar.SendMessageW(TB_SETBITMAPSIZE, 0, MAKELPARAM(cxIcon, cyIcon));
        if (!IsModernTaskbar() || !m_Tray->IsHorizontal())
            return;

        dwSize = (DWORD)m_TaskBar.SendMessageW(TB_GETBUTTONSIZE, 0, 0);
        cyPad = (INT)HIWORD(dwSize) - cyIcon;
        cyWant = m_cyTaskRow;
        if (cyWant <= 0)
            return;
        if (cyPad < 0 || cyWant - cyPad <= cyIcon)
            return;

        m_TaskBar.SendMessageW(TB_SETBITMAPSIZE, 0, MAKELPARAM(cxIcon, cyWant - cyPad));
        m_TaskBar.SendMessageW(TB_SETBUTTONSIZE, 0, MAKELPARAM(LOWORD(dwSize), cyWant));
    }

    HICON GetWndIcon(HWND hwnd)
    {
        HICON hIcon = NULL;
        BOOL bSmall = UseSmallTaskIcons();

        /* Retrieve icon by sending a message */
#define GET_ICON(type) \
    SendMessageTimeout(hwnd, WM_GETICON, (type), 0, SMTO_NOTIMEOUTIFNOTHUNG, 100, (PDWORD_PTR)&hIcon)

        LRESULT bAlive = GET_ICON(bSmall ? ICON_SMALL2 : ICON_BIG);
        if (hIcon)
            return hIcon;

        if (bAlive)
        {
            bAlive = GET_ICON(ICON_SMALL);
            if (hIcon)
                return hIcon;
        }

        if (bAlive)
        {
            GET_ICON(bSmall ? ICON_BIG : ICON_SMALL2);
            if (hIcon)
                return hIcon;
        }
#undef GET_ICON

        /* If we failed, retrieve icon from the window class */
        hIcon = (HICON)GetClassLongPtr(hwnd, bSmall ? GCLP_HICONSM : GCLP_HICON);
        if (hIcon)
            return hIcon;

        return (HICON)GetClassLongPtr(hwnd, bSmall ? GCLP_HICON : GCLP_HICONSM);
    }

    INT UpdateTaskItemButton(IN PTASK_ITEM TaskItem)
    {
        TBBUTTONINFO tbbi = { 0 };
        HICON icon;
        WCHAR windowText[255];

        ASSERT(TaskItem->Index >= 0);

        tbbi.cbSize = sizeof(tbbi);
        tbbi.dwMask = TBIF_BYINDEX | TBIF_STATE | TBIF_TEXT | TBIF_IMAGE;
        tbbi.fsState = TBSTATE_ENABLED;
        if (m_ActiveTaskItem == TaskItem)
            tbbi.fsState |= TBSTATE_CHECKED;

        if (TaskItem->RenderFlashed)
            tbbi.fsState |= TBSTATE_MARKED;

        /* Check if we're updating a button that is the last one in the
           line. If so, we need to set the TBSTATE_WRAP flag! */
        if (!m_Tray->IsHorizontal() || (m_ButtonsPerLine != 0 &&
            (TaskItem->Index + 1) % m_ButtonsPerLine == 0))
        {
            tbbi.fsState |= TBSTATE_WRAP;
        }

        if (IsModernTaskbar())
        {
            windowText[0] = 0;
            tbbi.pszText = windowText;
        }
        else if (GetWndTextFromTaskItem(TaskItem, windowText, _countof(windowText)) > 0)
        {
            tbbi.pszText = windowText;
        }

        icon = GetWndIcon(TaskItem->hWnd);
        if (!icon)
            icon = static_cast<HICON>(LoadImageW(NULL, MAKEINTRESOURCEW(OIC_SAMPLE), IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE));
        if (icon)
        {
            INT iImage = ImageList_ReplaceIcon(m_ImageList, TaskItem->IconIndex, icon);

            if (iImage >= 0)
                TaskItem->IconIndex = iImage;
        }
        tbbi.iImage = TaskItem->IconIndex;

        if (!m_TaskBar.SetButtonInfo(TaskItem->Index, &tbbi))
        {
            TaskItem->Index = -1;
            return -1;
        }

        TRACE("Updated button %d for hwnd 0x%p\n", TaskItem->Index, TaskItem->hWnd);
        return TaskItem->Index;
    }

    VOID RemoveIcon(IN PTASK_ITEM TaskItem)
    {
        TBBUTTONINFO tbbi;
        PTASK_ITEM currentTaskItem, LastItem;

        if (TaskItem->IconIndex == -1)
            return;

        tbbi.cbSize = sizeof(tbbi);
        tbbi.dwMask = TBIF_IMAGE;

        currentTaskItem = m_TaskItems;
        LastItem = currentTaskItem + m_TaskItemCount;
        while (currentTaskItem != LastItem)
        {
            if (currentTaskItem->IconIndex > TaskItem->IconIndex)
            {
                currentTaskItem->IconIndex--;
                if (currentTaskItem->Index >= 0)
                {
                    tbbi.iImage = currentTaskItem->IconIndex;
                    m_TaskBar.SetButtonInfo(currentTaskItem->Index, &tbbi);
                }
            }
            currentTaskItem++;
        }

        PTASK_GROUP currentGroup = m_TaskGroups;
        while (currentGroup != NULL)
        {
            if (currentGroup->IconIndex > TaskItem->IconIndex)
            {
                currentGroup->IconIndex--;
                if (currentGroup->IsCollapsed && currentGroup->Index >= 0)
                {
                    tbbi.iImage = currentGroup->IconIndex;
                    m_TaskBar.SetButtonInfo(currentGroup->Index, &tbbi);
                }
            }
            currentGroup = currentGroup->Next;
        }
        ImageList_Remove(m_ImageList, TaskItem->IconIndex);
        TaskItem->IconIndex = -1;
    }

    PTASK_ITEM FindLastTaskItemOfGroup(
        IN PTASK_GROUP TaskGroup  OPTIONAL,
        IN PTASK_ITEM NewTaskItem  OPTIONAL)
    {
        PTASK_ITEM TaskItem, LastTaskItem, FoundTaskItem = NULL;
        DWORD dwTaskCount;

        ASSERT(m_IsGroupingEnabled);

        TaskItem = m_TaskItems;
        LastTaskItem = TaskItem + m_TaskItemCount;

        dwTaskCount = (TaskGroup != NULL ? TaskGroup->dwTaskCount : MAX_TASKS_COUNT);

        ASSERT(dwTaskCount > 0);

        while (TaskItem != LastTaskItem)
        {
            if (TaskItem->Group == TaskGroup)
            {
                if ((NewTaskItem != NULL && TaskItem != NewTaskItem) || NewTaskItem == NULL)
                {
                    FoundTaskItem = TaskItem;
                }

                if (--dwTaskCount == 0)
                {
                    /* We found the last task item in the group! */
                    break;
                }
            }

            TaskItem++;
        }

        return FoundTaskItem;
    }

    INT CalculateTaskItemNewButtonIndex(IN PTASK_ITEM TaskItem)
    {
        PTASK_GROUP TaskGroup;
        PTASK_ITEM LastTaskItem;

        /* NOTE: This routine assumes that the group is *not* collapsed! */

        TaskGroup = TaskItem->Group;
        if (m_IsGroupingEnabled)
        {
            if (TaskGroup != NULL)
            {
                ASSERT(TaskGroup->Index < 0);
                ASSERT(!TaskGroup->IsCollapsed);

                if (TaskGroup->dwTaskCount > 1)
                {
                    LastTaskItem = FindLastTaskItemOfGroup(TaskGroup, TaskItem);
                    if (LastTaskItem != NULL)
                    {
                        /* Since the group is expanded the task items must have an index */
                        ASSERT(LastTaskItem->Index >= 0);

                        return LastTaskItem->Index + 1;
                    }
                }
            }
            else
            {
                /* Find the last NULL group button. NULL groups are added at the end of the
                   task item list when grouping is enabled */
                LastTaskItem = FindLastTaskItemOfGroup(NULL, TaskItem);
                if (LastTaskItem != NULL)
                {
                    ASSERT(LastTaskItem->Index >= 0);

                    return LastTaskItem->Index + 1;
                }
            }
        }

        return m_ButtonCount;
    }

    INT AddTaskItemButton(IN OUT PTASK_ITEM TaskItem)
    {
        WCHAR windowText[255];
        TBBUTTON tbBtn = { 0 };
        INT iIndex;
        HICON icon;

        if (TaskItem->Index >= 0)
        {
            return UpdateTaskItemButton(TaskItem);
        }

        if (TaskItem->Group != NULL &&
            !TaskItem->Group->IsCollapsed &&
            ShouldCombine(TaskItem->Group))
        {
            CollapseTaskGroup(TaskItem->Group);
        }

        if (TaskItem->Group != NULL &&
            TaskItem->Group->IsCollapsed)
        {
            /* The task group is collapsed, we only need to update the group button */
            return UpdateTaskGroupButton(TaskItem->Group);
        }

        icon = GetWndIcon(TaskItem->hWnd);
        if (!icon)
            icon = static_cast<HICON>(LoadImageW(NULL, MAKEINTRESOURCEW(OIC_SAMPLE), IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE));
        TaskItem->IconIndex = ImageList_ReplaceIcon(m_ImageList, -1, icon);

        tbBtn.iBitmap = TaskItem->IconIndex;
        tbBtn.fsState = TBSTATE_ENABLED | TBSTATE_ELLIPSES;
        tbBtn.fsStyle = BTNS_CHECK | BTNS_NOPREFIX | BTNS_SHOWTEXT;
        tbBtn.dwData = TaskItem->Index;

        if (IsModernTaskbar())
        {
            windowText[0] = 0;
            tbBtn.iString = (DWORD_PTR) windowText;
        }
        else if (GetWndTextFromTaskItem(TaskItem, windowText, _countof(windowText)) > 0)
        {
            tbBtn.iString = (DWORD_PTR) windowText;
        }

        /* Find out where to insert the new button */
        iIndex = CalculateTaskItemNewButtonIndex(TaskItem);
        ASSERT(iIndex >= 0);
        tbBtn.idCommand = iIndex;

        m_TaskBar.BeginUpdate();

        if (m_TaskBar.InsertButton(iIndex, &tbBtn))
        {
            UpdateIndexesAfter(iIndex, TRUE);

            TRACE("Added button %d for hwnd 0x%p\n", iIndex, TaskItem->hWnd);

            TaskItem->Index = iIndex;
            m_ButtonCount++;

            /* Update button sizes and fix the button wrapping */
            UpdateButtonsSize(TRUE);
            return iIndex;
        }

        m_TaskBar.EndUpdate();

        return -1;
    }

    BOOL DeleteTaskItemButton(IN OUT PTASK_ITEM TaskItem)
    {
        PTASK_GROUP TaskGroup;
        INT iIndex;

        TaskGroup = TaskItem->Group;

        if (TaskItem->Index >= 0)
        {
            if ((TaskGroup != NULL && !TaskGroup->IsCollapsed) ||
                TaskGroup == NULL)
            {
                m_TaskBar.BeginUpdate();

                RemoveIcon(TaskItem);
                iIndex = TaskItem->Index;
                if (m_TaskBar.DeleteButton(iIndex))
                {
                    TaskItem->Index = -1;
                    m_ButtonCount--;

                    UpdateIndexesAfter(iIndex, FALSE);

                    /* Update button sizes and fix the button wrapping */
                    UpdateButtonsSize(TRUE);
                    return TRUE;
                }

                m_TaskBar.EndUpdate();
            }
        }

        return FALSE;
    }

    BOOL GetTaskExePath(IN DWORD dwProcessId, OUT LPWSTR pszPath, IN DWORD cchPath)
    {
        HANDLE hProcess;
        DWORD cch = cchPath;

        pszPath[0] = 0;

        hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
        if (!hProcess)
            hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwProcessId);
        if (!hProcess)
            return FALSE;

        if (!QueryFullProcessImageNameW(hProcess, 0, pszPath, &cch))
            pszPath[0] = 0;

        CloseHandle(hProcess);
        if (pszPath[0])
        {
            CStringW Target;
            if (TaskbarPin_ResolveTarget(pszPath, Target))
                StringCchCopyW(pszPath, cchPath, Target);
        }
        return pszPath[0] != 0;
    }

    PTASK_GROUP FindLaunchingTaskGroup(IN DWORD ProcessId)
    {
        for (PTASK_GROUP Group = m_TaskGroups; Group; Group = Group->Next)
        {
            if (Group->hLaunchProcess && WaitForSingleObject(Group->hLaunchProcess, 0) == WAIT_TIMEOUT && GetProcessId(Group->hLaunchProcess) == ProcessId)
                return Group;
        }
        return NULL;
    }

    PTASK_GROUP AddToTaskGroup(IN HWND hWnd)
    {
        DWORD dwProcessId;
        PTASK_GROUP TaskGroup;
        WCHAR szExePath[MAX_PATH];

        if (!GetWindowThreadProcessId(hWnd,
            &dwProcessId))
        {
            TRACE("Cannot get process id of hwnd 0x%p\n", hWnd);
            return NULL;
        }

        GetTaskExePath(dwProcessId, szExePath, _countof(szExePath));

        TaskGroup = FindLaunchingTaskGroup(dwProcessId);
        if (TaskGroup)
        {
            TaskGroup->dwTaskCount++;
            TaskGroup->dwProcessId = dwProcessId;
            ClearTaskGroupLaunch(TaskGroup);
            return TaskGroup;
        }

        /* Try to find an existing task group */
        TaskGroup = m_TaskGroups;
        while (TaskGroup != NULL)
        {
            if ((szExePath[0] && TaskGroup->szExePath[0] &&
                 _wcsicmp(TaskGroup->szExePath, szExePath) == 0) ||
                (TaskGroup->dwTaskCount && TaskGroup->dwProcessId == dwProcessId) ||
                (!szExePath[0] && !TaskGroup->szExePath[0] &&
                 TaskGroup->dwProcessId == dwProcessId))
            {
                TaskGroup->dwTaskCount++;
                TaskGroup->dwProcessId = dwProcessId;
                ClearTaskGroupLaunch(TaskGroup);
                return TaskGroup;
            }
            TaskGroup = TaskGroup->Next;
        }

        /* Allocate a new task group */
        TaskGroup = AppendTaskGroup(szExePath);
        if (TaskGroup != NULL)
        {
            TaskGroup->dwTaskCount = 1;
            TaskGroup->dwProcessId = dwProcessId;
        }

        return TaskGroup;
    }

    VOID RemoveTaskFromTaskGroup(IN OUT PTASK_ITEM TaskItem)
    {
        PTASK_GROUP TaskGroup;

        TaskGroup = TaskItem->Group;
        if (TaskGroup != NULL)
        {
            DWORD dwNewTaskCount = --TaskGroup->dwTaskCount;
            TaskItem->Group = NULL;
            if (dwNewTaskCount == 0 && TaskGroup->IsPinned)
            {
                /* The shortcut owns this button even after its last window is gone. */
                TaskGroup->dwProcessId = 0;
                if (!m_IsDestroying)
                {
                    if (TaskGroup->IsCollapsed && TaskGroup->Index >= 0)
                        UpdateTaskGroupButton(TaskGroup);
                    else
                    {
                        TaskGroup->IsCollapsed = FALSE;
                        CollapseTaskGroup(TaskGroup);
                    }
                }
            }
            else if (dwNewTaskCount == 0)
            {
                if (!m_IsDestroying && TaskGroup->IsCollapsed)
                    DeleteTaskGroupButton(TaskGroup);
                UnlinkAndFreeTaskGroup(TaskGroup);
            }
            else if (TaskGroup->IsCollapsed &&
                TaskGroup->Index >= 0 &&
                !m_IsDestroying)
            {
                if (ShouldCombine(TaskGroup))
                {
                    /* Update the task group button */
                    UpdateTaskGroupButton(TaskGroup);
                }
                else
                {
                    /* Expand the group of one task button to a task button */
                    ExpandTaskGroup(TaskGroup);
                }
            }
        }
    }

    PTASK_ITEM FindTaskItem(IN HWND hWnd)
    {
        PTASK_ITEM TaskItem, LastItem;

        if (!m_TaskItemCount)
            return NULL;
        TaskItem = m_TaskItems;
        LastItem = TaskItem + m_TaskItemCount;
        while (TaskItem != LastItem)
        {
            if (TaskItem->hWnd == hWnd)
                return TaskItem;

            TaskItem++;
        }

        return NULL;
    }

    PTASK_ITEM FindOtherTaskItem(IN HWND hWnd)
    {
        PTASK_ITEM LastItem, TaskItem;
        DWORD dwProcessId;

        if (!m_TaskItemCount || !GetWindowThreadProcessId(hWnd, &dwProcessId))
        {
            return NULL;
        }

        /* Try to find another task that belongs to the same
           process as the given window */
        TaskItem = m_TaskItems;
        LastItem = TaskItem + m_TaskItemCount;
        while (TaskItem != LastItem)
        {
            DWORD dwProcessIdTask;
            if (GetWindowThreadProcessId(TaskItem->hWnd, &dwProcessIdTask) && dwProcessIdTask == dwProcessId)
                return TaskItem;

            TaskItem++;
        }

        return NULL;
    }

    PTASK_ITEM AllocTaskItem()
    {
        if (m_TaskItemCount >= MAX_TASKS_COUNT)
        {
            /* We need the most significant bit in 16 bit command IDs to indicate whether it
               is a task group or task item. WM_COMMAND limits command IDs to 16 bits! */
            return NULL;
        }

        ASSERT(m_AllocatedTaskItems >= m_TaskItemCount);

        if (!m_TaskItems)
        {
            m_TaskItems = (PTASK_ITEM) HeapAlloc(hProcessHeap,
                0,
                TASK_ITEM_ARRAY_ALLOC * sizeof(*m_TaskItems));
            if (m_TaskItems != NULL)
            {
                m_AllocatedTaskItems = TASK_ITEM_ARRAY_ALLOC;
            }
            else
                return NULL;
        }
        else if (m_TaskItemCount >= m_AllocatedTaskItems)
        {
            PTASK_ITEM NewArray;
            SIZE_T NewArrayLength, ActiveTaskItemIndex;

            NewArrayLength = m_AllocatedTaskItems + TASK_ITEM_ARRAY_ALLOC;

            NewArray = (PTASK_ITEM) HeapReAlloc(hProcessHeap,
                0,
                m_TaskItems,
                NewArrayLength * sizeof(*m_TaskItems));
            if (NewArray != NULL)
            {
                if (m_ActiveTaskItem != NULL)
                {
                    /* Fixup the ActiveTaskItem pointer */
                    ActiveTaskItemIndex = m_ActiveTaskItem - m_TaskItems;
                    m_ActiveTaskItem = NewArray + ActiveTaskItemIndex;
                }
                m_AllocatedTaskItems = (WORD) NewArrayLength;
                m_TaskItems = NewArray;
            }
            else
                return NULL;
        }

        return m_TaskItems + m_TaskItemCount++;
    }

    VOID FreeTaskItem(IN OUT PTASK_ITEM TaskItem)
    {
        WORD wIndex;

        if (TaskItem == m_ActiveTaskItem)
            m_ActiveTaskItem = NULL;
        else if (m_ActiveTaskItem && m_ActiveTaskItem > TaskItem)
            --m_ActiveTaskItem;

        wIndex = (WORD) (TaskItem - m_TaskItems);
        if (wIndex + 1 < m_TaskItemCount)
        {
            MoveMemory(TaskItem,
                TaskItem + 1,
                (m_TaskItemCount - wIndex - 1) * sizeof(*TaskItem));
        }

        m_TaskItemCount--;
    }

    VOID DeleteTaskItem(IN OUT PTASK_ITEM TaskItem)
    {
        if (!m_IsDestroying)
        {
            /* Delete the task button from the toolbar */
            DeleteTaskItemButton(TaskItem);
        }

        /* Remove the task from it's group */
        RemoveTaskFromTaskGroup(TaskItem);

        /* Free the task item */
        FreeTaskItem(TaskItem);
    }

    VOID CheckActivateTaskItem(IN OUT PTASK_ITEM TaskItem)
    {
        PTASK_ITEM OldTaskItem = m_ActiveTaskItem;
        PTASK_GROUP OldGroup = OldTaskItem != NULL ? OldTaskItem->Group : NULL;
        PTASK_GROUP NewGroup = TaskItem != NULL ? TaskItem->Group : NULL;

        if (OldTaskItem == TaskItem)
            return;

        m_ActiveTaskItem = TaskItem;

        if (OldTaskItem != NULL)
        {
            if (OldGroup != NULL && OldGroup->IsCollapsed)
            {
                if (OldGroup != NewGroup && OldGroup->Index >= 0)
                    UpdateTaskGroupButton(OldGroup);
            }
            else if (OldTaskItem->Index >= 0)
            {
                UpdateTaskItemButton(OldTaskItem);
            }
        }

        if (TaskItem != NULL)
        {
            if (NewGroup != NULL && NewGroup->IsCollapsed)
            {
                if (NewGroup->Index >= 0)
                    UpdateTaskGroupButton(NewGroup);
            }
            else if (TaskItem->Index >= 0)
            {
                UpdateTaskItemButton(TaskItem);
            }
        }
        else
        {
            TRACE("Active TaskItem now NULL\n");
        }
    }

    PTASK_ITEM FindTaskItemByIndex(IN INT Index)
    {
        PTASK_ITEM TaskItem, LastItem;

        if (!m_TaskItemCount || Index < 0)
            return NULL;
        TaskItem = m_TaskItems;
        LastItem = TaskItem + m_TaskItemCount;
        while (TaskItem != LastItem)
        {
            if (TaskItem->Index == Index)
                return TaskItem;

            TaskItem++;
        }

        return NULL;
    }

    PTASK_GROUP FindTaskGroupByIndex(IN INT Index)
    {
        PTASK_GROUP CurrentGroup;

        CurrentGroup = m_TaskGroups;
        while (CurrentGroup != NULL)
        {
            if (CurrentGroup->Index == Index)
                break;

            CurrentGroup = CurrentGroup->Next;
        }

        return CurrentGroup;
    }

    VOID RefreshTaskItemGroup(IN PTASK_ITEM TaskItem)
    {
        if (!TaskItem->Group || TaskItem->Group->IsPinned)
            return;
        DWORD ProcessId = 0;
        WCHAR Path[MAX_PATH] = L"";
        if (!GetWindowThreadProcessId(TaskItem->hWnd, &ProcessId))
            return;
        PTASK_GROUP Group = FindLaunchingTaskGroup(ProcessId);
        if (!Group)
        {
            if (!GetTaskExePath(ProcessId, Path, _countof(Path)))
                return;
            Group = FindTaskGroupByPath(Path);
        }
        if (!Group)
        {
            if (!TaskItem->Group->szExePath[0])
                StringCchCopyW(TaskItem->Group->szExePath, _countof(TaskItem->Group->szExePath), Path);
            return;
        }
        if (Group == TaskItem->Group)
            return;

        DeleteTaskItemButton(TaskItem);
        RemoveTaskFromTaskGroup(TaskItem);
        TaskItem->Group = Group;
        Group->dwTaskCount++;
        Group->dwProcessId = ProcessId;
        ClearTaskGroupLaunch(Group);
        AddTaskItemButton(TaskItem);
    }

    BOOL AddTask(IN HWND hWnd)
    {
        PTASK_ITEM TaskItem;

        if (!::IsWindow(hWnd) || m_Tray->IsSpecialHWND(hWnd))
            return FALSE;

        TaskItem = FindTaskItem(hWnd);
        if (TaskItem != NULL)
            RefreshTaskItemGroup(TaskItem);
        if (TaskItem == NULL)
        {
            TRACE("Add window 0x%p\n", hWnd);
            TaskItem = AllocTaskItem();
            if (TaskItem != NULL)
            {
                ZeroMemory(TaskItem, sizeof(*TaskItem));
                TaskItem->hWnd = hWnd;
                TaskItem->Index = -1;
                TaskItem->IconIndex = -1;
                TaskItem->Group = AddToTaskGroup(hWnd);

                if (!m_IsDestroying)
                {
                    AddTaskItemButton(TaskItem);
                }
            }
        }

        return TaskItem != NULL;
    }

    BOOL ActivateTaskItem(IN OUT PTASK_ITEM TaskItem  OPTIONAL)
    {
        if (TaskItem != NULL)
        {
            TRACE("Activate window 0x%p on button %d\n", TaskItem->hWnd, TaskItem->Index);
        }

        CheckActivateTaskItem(TaskItem);
        return FALSE;
    }

    BOOL ActivateTask(IN HWND hWnd)
    {
        PTASK_ITEM TaskItem;

        if (!hWnd)
        {
            return ActivateTaskItem(NULL);
        }

        TaskItem = FindTaskItem(hWnd);
        if (TaskItem == NULL)
        {
            TaskItem = FindOtherTaskItem(hWnd);
        }

        if (TaskItem == NULL)
        {
            WARN("Activate window 0x%p, could not find task\n", hWnd);
            RefreshWindowList();
        }

        return ActivateTaskItem(TaskItem);
    }

    BOOL DeleteTask(IN HWND hWnd)
    {
        PTASK_ITEM TaskItem;

        TaskItem = FindTaskItem(hWnd);
        if (TaskItem != NULL)
        {
            TRACE("Delete window 0x%p on button %d\n", hWnd, TaskItem->Index);
            DeleteTaskItem(TaskItem);
            return TRUE;
        }
        //else
        //TRACE("Failed to delete window 0x%p\n", hWnd);

        return FALSE;
    }

    VOID DeleteAllTasks()
    {
        PTASK_ITEM CurrentTask;

        if (m_TaskItemCount > 0)
        {
            CurrentTask = m_TaskItems + m_TaskItemCount;
            do
            {
                DeleteTaskItem(--CurrentTask);
            } while (CurrentTask != m_TaskItems);
        }
    }

    VOID FlashTaskItem(IN OUT PTASK_ITEM TaskItem)
    {
        TaskItem->RenderFlashed = 1;
        if (TaskItem->Group != NULL && TaskItem->Group->IsCollapsed)
        {
            if (TaskItem->Group->Index >= 0)
                UpdateTaskGroupButton(TaskItem->Group);
        }
        else if (TaskItem->Index >= 0)
        {
            UpdateTaskItemButton(TaskItem);
        }
    }

    BOOL FlashTask(IN HWND hWnd)
    {
        PTASK_ITEM TaskItem;

        TaskItem = FindTaskItem(hWnd);
        if (TaskItem != NULL)
        {
            TRACE("Flashing window 0x%p on button %d\n", hWnd, TaskItem->Index);
            FlashTaskItem(TaskItem);
            return TRUE;
        }

        return FALSE;
    }

    VOID RedrawTaskItem(IN OUT PTASK_ITEM TaskItem)
    {
        PTASK_GROUP TaskGroup;

        TaskGroup = TaskItem->Group;
        if (TaskGroup != NULL)
        {
            if (TaskGroup->IsCollapsed && TaskGroup->Index >= 0)
            {
                UpdateTaskGroupButton(TaskGroup);
            }
            else if (TaskItem->Index >= 0)
            {
                goto UpdateTaskItem;
            }
        }
        else if (TaskItem->Index >= 0)
        {
        UpdateTaskItem:
            TaskItem->RenderFlashed = 0;
            UpdateTaskItemButton(TaskItem);
        }
    }


    BOOL RedrawTask(IN HWND hWnd)
    {
        PTASK_ITEM TaskItem;

        TaskItem = FindTaskItem(hWnd);
        if (TaskItem != NULL)
        {
            RedrawTaskItem(TaskItem);
            return TRUE;
        }

        return FALSE;
    }

    VOID UpdateButtonsSize(IN BOOL bRedrawDisabled)
    {
        RECT rcClient;
        UINT uiRows, uiMax, uiMin, uiBtnsPerLine, ui;
        LONG NewBtnSize;
        BOOL Horizontal;

        /* Update the size of the image list if needed */
        int cx, cy;
        BOOL bSmall = UseSmallTaskIcons();
        ImageList_GetIconSize(m_ImageList, &cx, &cy);
        if (cx != GetSystemMetrics(bSmall ? SM_CXSMICON : SM_CXICON) ||
            cy != GetSystemMetrics(bSmall ? SM_CYSMICON : SM_CYICON))
        {
            ImageList_SetIconSize(m_ImageList,
                                  GetSystemMetrics(bSmall ? SM_CXSMICON : SM_CXICON),
                                  GetSystemMetrics(bSmall ? SM_CYSMICON : SM_CYICON));

            /* SetIconSize removes all icons so we have to reinsert them */
            PTASK_ITEM TaskItem = m_TaskItems;
            PTASK_ITEM LastTaskItem = m_TaskItemCount ? m_TaskItems + m_TaskItemCount : m_TaskItems;
            while (TaskItem != LastTaskItem)
            {
                TaskItem->IconIndex = -1;
                if (TaskItem->Index >= 0)
                    UpdateTaskItemButton(TaskItem);

                TaskItem++;
            }

            PTASK_GROUP TaskGroup = m_TaskGroups;
            while (TaskGroup != NULL)
            {
                TaskGroup->IconIndex = -1;
                if (TaskGroup->IsCollapsed && TaskGroup->Index >= 0)
                    UpdateTaskGroupButton(TaskGroup);

                TaskGroup = TaskGroup->Next;
            }
            m_TaskBar.SetImageList(m_ImageList);
        }

        ApplyTaskRowHeight();
        DWORD ButtonSize = (DWORD)m_TaskBar.SendMessageW(TB_GETBUTTONSIZE, 0, 0);
        m_ButtonSize.cy = HIWORD(ButtonSize);

        if (GetClientRect(&rcClient) && !IsRectEmpty(&rcClient))
        {
            if (m_ButtonCount > 0)
            {
                Horizontal = m_Tray->IsHorizontal();

                if (Horizontal)
                {
                    TBMETRICS tbm = { 0 };
                    tbm.cbSize = sizeof(tbm);
                    tbm.dwMask = TBMF_BUTTONSPACING;
                    m_TaskBar.GetMetrics(&tbm);

                    if (m_ButtonSize.cy + tbm.cyButtonSpacing != 0)
                        uiRows = (rcClient.bottom + tbm.cyButtonSpacing) / (m_ButtonSize.cy + tbm.cyButtonSpacing);
                    else
                        uiRows = 1;

                    if (uiRows == 0)
                        uiRows = 1;

                    uiBtnsPerLine = (m_ButtonCount + uiRows - 1) / uiRows;
                }
                else
                {
                    uiBtnsPerLine = 1;
                    uiRows = m_ButtonCount;
                }

                if (!bRedrawDisabled)
                    m_TaskBar.BeginUpdate();

                /* We might need to update the button spacing */
                int cxButtonSpacing = m_TaskBar.UpdateTbButtonSpacing(
                    Horizontal, m_Theme != NULL,
                    uiRows, uiBtnsPerLine);

                /* Determine the minimum and maximum width of a button */
                uiMin = GetSystemMetrics(SM_CXSIZE) + (2 * GetSystemMetrics(SM_CXEDGE));
                if (Horizontal)
                {
                    uiMax = IsModernTaskbar() ? (UINT)ShellScaleForDpi(62) : GetSystemMetrics(SM_CXMINIMIZED);

                    /* Calculate the ideal width and make sure it's within the allowed range */
                    NewBtnSize = (rcClient.right - (uiBtnsPerLine * cxButtonSpacing)) / uiBtnsPerLine;

                    if (NewBtnSize < (LONG) uiMin)
                        NewBtnSize = uiMin;
                    if (NewBtnSize >(LONG)uiMax)
                        NewBtnSize = uiMax;

                    /* Recalculate how many buttons actually fit into one line */
                    uiBtnsPerLine = rcClient.right / (NewBtnSize + cxButtonSpacing);
                    if (uiBtnsPerLine == 0)
                        uiBtnsPerLine++;
                }
                else
                {
                    NewBtnSize = uiMax = rcClient.right;
                }

                m_ButtonSize.cx = NewBtnSize;

                m_ButtonsPerLine = uiBtnsPerLine;

                for (ui = 0; ui != m_ButtonCount; ui++)
                {
                    TBBUTTONINFOW tbbi = { 0 };
                    tbbi.cbSize = sizeof(tbbi);
                    tbbi.dwMask = TBIF_BYINDEX | TBIF_SIZE | TBIF_STATE;
                    tbbi.cx = (INT) NewBtnSize;
                    tbbi.fsState = TBSTATE_ENABLED;

                    /* Check if we're updating a button that is the last one in the
                       line. If so, we need to set the TBSTATE_WRAP flag! */
                    if (Horizontal)
                    {
                        if ((ui + 1) % uiBtnsPerLine == 0)
                            tbbi.fsState |= TBSTATE_WRAP;
                    }
                    else
                    {
                        tbbi.fsState |= TBSTATE_WRAP;
                    }

                    if (m_ActiveTaskItem != NULL)
                    {
                        PTASK_GROUP ActiveGroup = m_ActiveTaskItem->Group;
                        if (ActiveGroup != NULL && ActiveGroup->IsCollapsed)
                        {
                            if (ActiveGroup->Index == (INT)ui)
                                tbbi.fsState |= TBSTATE_CHECKED;
                        }
                        else if (m_ActiveTaskItem->Index == (INT)ui)
                        {
                            tbbi.fsState |= TBSTATE_CHECKED;
                        }
                    }

                    m_TaskBar.SetButtonInfo(ui, &tbbi);
                }
            }
            else
            {
                m_ButtonsPerLine = 0;
                m_ButtonSize.cx = 0;
            }
        }

        // FIXME: This seems to be enabling redraws prematurely, but moving it to its right place doesn't work!
        m_TaskBar.EndUpdate();
    }

    BOOL CALLBACK EnumWindowsProc(IN HWND hWnd)
    {
        if (m_Tray->IsTaskWnd(hWnd))
        {
            TRACE("Adding task for %p...\n", hWnd);
            AddTask(hWnd);
        }
        return TRUE;
    }

    static BOOL CALLBACK s_EnumWindowsProc(IN HWND hWnd, IN LPARAM lParam)
    {
        CTaskSwitchWnd * This = (CTaskSwitchWnd *) lParam;

        return This->EnumWindowsProc(hWnd);
    }

    BOOL RefreshWindowList()
    {
        TRACE("Refreshing window list...\n");
        /* Add all windows to the toolbar */
        return EnumWindows(s_EnumWindowsProc, (LPARAM)this);
    }

    LRESULT OnThemeChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        TRACE("OnThemeChanged\n");

        BOOL bWasModernTaskbar = IsModernTaskbar();

        if (m_Theme)
            CloseThemeData(m_Theme);

        if (IsThemeActive())
            m_Theme = OpenThemeData(m_hWnd, L"TaskBand");
        else
            m_Theme = NULL;
        m_bMaterial = ShellGetTaskbarMaterial(&m_crMaterial, &m_cyTaskRow);
        m_TaskBar.m_bTrackGlow = m_bMaterial && IsModernTaskbar();
        m_GlowCacheIcon = -1;
        m_HoverIndex = -1;
        TaskPreview_Hide();

        m_IsGroupingEnabled = g_TaskbarSettings.bGroupButtons || IsModernTaskbar();
        UpdateTaskGroupLayout();

        m_TaskBar.UpdateTbButtonSpacing(m_Tray->IsHorizontal(), m_Theme != NULL);
        UpdateButtonsSize(FALSE);
        if (bWasModernTaskbar != IsModernTaskbar())
            RefreshTaskButtons();
        InvalidateRect(NULL, TRUE);

        return TRUE;
    }

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (!m_TaskBar.Initialize(m_hWnd))
            return FALSE;

        SetWindowTheme(m_TaskBar.m_hWnd, m_Tray->IsHorizontal() ? L"TaskBand" : L"TaskBandVert", NULL);
        m_bMaterial = ShellGetTaskbarMaterial(&m_crMaterial, &m_cyTaskRow);
        m_TaskBar.m_bTrackGlow = m_bMaterial && IsModernTaskbar();

        m_ImageList = ImageList_Create(GetSystemMetrics(UseSmallTaskIcons() ? SM_CXSMICON : SM_CXICON),
                                       GetSystemMetrics(UseSmallTaskIcons() ? SM_CYSMICON : SM_CYICON),
                                       ILC_COLOR32 | ILC_MASK, 0, 1000);
        m_TaskBar.SetImageList(m_ImageList);
        ApplyTaskRowHeight();

        /* Set proper spacing between buttons */
        m_TaskBar.UpdateTbButtonSpacing(m_Tray->IsHorizontal(), m_Theme != NULL);

        /* Register the shell hook */
        m_ShellHookMsg = RegisterWindowMessageW(L"SHELLHOOK");

        TRACE("ShellHookMsg got assigned number %d\n", m_ShellHookMsg);

        RegisterShellHook(m_hWnd, 3); /* 1 if no NT! We're targeting NT so we don't care! */

        RegisterDragDrop(m_TaskBar.m_hWnd, static_cast<IDropTarget *>(this));
        RegisterDragDrop(m_hWnd, static_cast<IDropTarget *>(this));
        CoCreateInstance(CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARG(IDropTargetHelper, &m_DropTargetHelper));

        SyncTaskbarPins();
        RefreshWindowList();

        /* Recalculate the button size */
        UpdateButtonsSize(FALSE);

#if DUMP_TASKS != 0
        SetTimer(hwnd, 1, 5000, NULL);
#endif
        return TRUE;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        m_IsDestroying = TRUE;

        RevokeDragDrop(m_TaskBar.m_hWnd);
        RevokeDragDrop(m_hWnd);
        m_DropTargetHelper.Release();
        AbortTaskDrag();
        CancelTaskPreview();
        KillTimer(TIMER_ID_BUTTON_ANIM);
        KillTimer(TIMER_ID_VALIDATE_RUDE_APP);

        /* Unregister the shell hook */
        RegisterShellHook(m_hWnd, FALSE);

        CloseThemeData(m_Theme);
        DeleteAllTasks();
        FreeAllTaskGroups();
        HeapFree(hProcessHeap, 0, m_TaskItems);
        m_TaskItems = NULL;
        m_AllocatedTaskItems = 0;

        if (m_ImageList)
        {
            ImageList_Destroy(m_ImageList);
            m_ImageList = NULL;
        }

        return TRUE;
    }

    static BOOL InvokeRegistryAppKeyCommand(UINT uAppCmd)
    {
        BOOL bResult = FALSE;
        WCHAR szBuf[MAX_PATH * 2];
        wsprintfW(szBuf, L"%s\\AppKey\\%u", REGSTR_PATH_EXPLORER, uAppCmd);
        HUSKEY hKey;
        if (SHRegOpenUSKeyW(szBuf, KEY_READ, NULL, &hKey, FALSE) != ERROR_SUCCESS)
            return bResult;

        DWORD cb = sizeof(szBuf);
        if (!bResult && SHRegQueryUSValueW(hKey, L"ShellExecute", NULL, szBuf, &cb, FALSE, NULL, 0) == ERROR_SUCCESS)
        {
            bResult = TRUE;
        }
        cb = sizeof(szBuf);
        if (!bResult && SHRegQueryUSValueW(hKey, L"Association", NULL, szBuf, &cb, FALSE, NULL, 0) == ERROR_SUCCESS)
        {
            bResult = TRUE;
            cb = _countof(szBuf);
            if (AssocQueryString(ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE, szBuf, NULL, szBuf, &cb) != S_OK)
                *szBuf = UNICODE_NULL;
        }
        cb = sizeof(szBuf);
        if (!bResult && SHRegQueryUSValueW(hKey, L"RegisteredApp", NULL, szBuf, &cb, FALSE, NULL, 0) == ERROR_SUCCESS)
        {
            bResult = TRUE;
            SHRunIndirectRegClientCommand(NULL, szBuf);
            *szBuf = UNICODE_NULL; // Don't execute again
        }
        SHRegCloseUSKey(hKey);

        // Note: Tweak UI uses an empty string for its "Do nothing" option.
        if (bResult && *szBuf)
            ShellExec_RunDLLW(NULL, NULL, szBuf, SW_SHOW);
        return bResult;
    }

    BOOL HandleAppCommand(IN WPARAM wParam, IN LPARAM lParam)
    {
        const UINT uAppCmd = GET_APPCOMMAND_LPARAM(lParam);
        if (InvokeRegistryAppKeyCommand(uAppCmd))
            return TRUE;
        switch (uAppCmd)
        {
            case APPCOMMAND_VOLUME_MUTE:
            case APPCOMMAND_VOLUME_DOWN:
            case APPCOMMAND_VOLUME_UP:
                // TODO: Try IMMDeviceEnumerator::GetDefaultAudioEndpoint first and then fall back to mixer.
                FIXME("Call the mixer API to change the global volume\n");
                return TRUE;
            case APPCOMMAND_BROWSER_SEARCH:
                return SHFindFiles(NULL, NULL);
        }
        return FALSE;
    }

    LRESULT OnShellHook(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        BOOL Ret = FALSE;

        /* In case the shell hook wasn't registered properly, ignore WM_NULLs*/
        if (uMsg == 0)
        {
            bHandled = FALSE;
            return 0;
        }

        TRACE("Received shell hook message: wParam=%08lx, lParam=%08lx\n", wParam, lParam);

        switch ((INT) wParam)
        {
        case HSHELL_APPCOMMAND:
            Ret = HandleAppCommand(0, lParam);
            break;

        case HSHELL_WINDOWCREATED:
            if (AddTask((HWND) lParam))
                StartMenu2_RecordUsage((HWND)lParam);
            break;

        case HSHELL_WINDOWDESTROYED:
            /* The window still exists! Delay destroying it a bit */
            OnWindowDestroyed((HWND)lParam);
            DeleteTask((HWND)lParam);
            break;

        case HSHELL_RUDEAPPACTIVATED:
        case HSHELL_WINDOWACTIVATED:
            OnWindowActivated((HWND)lParam);
            ActivateTask((HWND)lParam);
            if (FindTaskItem((HWND)lParam))
                StartMenu2_RecordUsage((HWND)lParam);
            break;

        case HSHELL_FLASH:
            FlashTask((HWND) lParam);
            break;

        case HSHELL_REDRAW:
            RedrawTask((HWND) lParam);
            break;

        case HSHELL_TASKMAN:
            ::PostMessage(m_Tray->GetHWND(), TWM_OPENSTARTMENU, 0, 0);
            break;

        case HSHELL_ACTIVATESHELLWINDOW:
            ::SwitchToThisWindow(m_Tray->GetHWND(), TRUE);
            ::SetForegroundWindow(m_Tray->GetHWND());
            break;

        case HSHELL_LANGUAGE:
        case HSHELL_SYSMENU:
        case HSHELL_ENDTASK:
        case HSHELL_ACCESSIBILITYSTATE:
        case HSHELL_WINDOWREPLACED:
        case HSHELL_WINDOWREPLACING:

        case HSHELL_GETMINRECT:
        default:
        {
#if DEBUG_SHELL_HOOK
            int i, found;
            for (i = 0, found = 0; i != _countof(hshell_msg); i++)
            {
                if (hshell_msg[i].msg == (INT) wParam)
                {
                    TRACE("Shell message %ws unhandled (lParam = 0x%p)!\n", hshell_msg[i].msg_name, lParam);
                    found = 1;
                    break;
                }
            }
            if (found)
                break;
#endif
            TRACE("Shell message %d unhandled (lParam = 0x%p)!\n", (INT) wParam, lParam);
            break;
        }
        }

        return Ret;
    }

    VOID CancelTaskPreview()
    {
        /* Ignore an already queued WM_TIMER until the pointer changes buttons. */
        m_HoverPreviewPending = FALSE;
        KillTimer(TIMER_ID_HOVER_PREVIEW);
        TaskPreview_Hide();
    }

    VOID HandleTaskItemClick(IN OUT PTASK_ITEM TaskItem)
    {
        BOOL bIsMinimized;
        BOOL bIsActive;

        TaskPreview_Hide();
        if (::IsWindow(TaskItem->hWnd))
        {
            bIsMinimized = ::IsIconic(TaskItem->hWnd);
            bIsActive = (TaskItem == m_ActiveTaskItem);

            TRACE("Active TaskItem %p, selected TaskItem %p\n", m_ActiveTaskItem, TaskItem);
            if (m_ActiveTaskItem)
                TRACE("Active TaskItem hWnd=%p, TaskItem hWnd %p\n", m_ActiveTaskItem->hWnd, TaskItem->hWnd);

            TRACE("Valid button clicked. HWND=%p, IsMinimized=%s, IsActive=%s...\n",
                TaskItem->hWnd, bIsMinimized ? "Yes" : "No", bIsActive ? "Yes" : "No");

            if (!bIsMinimized && bIsActive)
            {
                if (!::IsHungAppWindow(TaskItem->hWnd))
                    ::ShowWindowAsync(TaskItem->hWnd, SW_MINIMIZE);
                TRACE("Valid button clicked. App window Minimized.\n");
            }
            else
            {
                ::SwitchToThisWindow(TaskItem->hWnd, TRUE);

                TRACE("Valid button clicked. App window Restored.\n");
            }
        }
    }

    UINT CollectGroupWindows(IN PTASK_GROUP TaskGroup, OUT HWND *pahWnd, IN UINT cMax)
    {
        if (!m_TaskItemCount || !TaskGroup)
            return 0;
        PTASK_ITEM TaskItem, LastItem = m_TaskItems + m_TaskItemCount;
        UINT cWindows = 0;

        for (TaskItem = m_TaskItems; TaskItem != LastItem && cWindows < cMax; TaskItem++)
        {
            if (TaskItem->Group == TaskGroup && ::IsWindow(TaskItem->hWnd))
                pahWnd[cWindows++] = TaskItem->hWnd;
        }
        return cWindows;
    }

    BOOL GetButtonScreenRect(IN INT Index, OUT RECT *prc)
    {
        if (Index < 0 || !m_TaskBar.GetItemRect(Index, prc))
            return FALSE;
        ::MapWindowPoints(m_TaskBar.m_hWnd, NULL, (LPPOINT)prc, 2);
        return TRUE;
    }

    PTASK_GROUP GroupOfIndex(IN INT Index)
    {
        PTASK_GROUP TaskGroup = FindTaskGroupByIndex(Index);
        PTASK_ITEM TaskItem;

        if (TaskGroup != NULL)
            return TaskGroup;
        TaskItem = FindTaskItemByIndex(Index);
        return TaskItem != NULL ? TaskItem->Group : NULL;
    }

    VOID LaunchTaskPath(IN PCWSTR pszTarget, IN PCWSTR pszSource)
    {
        CStringW Target = pszTarget, Source = pszSource;
        PTASK_GROUP TaskGroup = FindTaskGroupByPath(Target);
        if (TaskGroup)
            TaskGroup->LaunchInProgress = TRUE;
        CancelTaskPreview();
        HANDLE Process = NULL;
        HRESULT hr = TaskbarPin_Launch(Source, &Process);

        // The Run As path can dispatch window creation and unpin messages.
        TaskGroup = FindTaskGroupByPath(Target);
        if (TaskGroup)
        {
            TaskGroup->LaunchInProgress = FALSE;
            HWND Window;
            if (Process && !CollectGroupWindows(TaskGroup, &Window, 1))
            {
                ClearTaskGroupLaunch(TaskGroup);
                TaskGroup->hLaunchProcess = Process;
                Process = NULL;
            }
        }
        if (Process)
            CloseHandle(Process);
        if (FAILED(hr))
        {
            ERR("Taskbar launch failed for %S: %08lx\n", Target.GetString(), hr);
            WCHAR Reason[256] = L"";
            DWORD Error = HRESULT_FACILITY(hr) == FACILITY_WIN32 ? HRESULT_CODE(hr) : ERROR_GEN_FAILURE;
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, Error, 0, Reason, _countof(Reason), NULL);
            CStringW Message = L"The application could not be started.";
            if (Reason[0])
            {
                Message += L"\n\n";
                Message += Reason;
            }
            MessageBoxW(Message, L"Taskbar", MB_OK | MB_ICONERROR);
        }
    }

    VOID LaunchNewInstance(IN PTASK_GROUP TaskGroup)
    {
        if (TaskGroup == NULL || (!TaskGroup->szLinkPath[0] && !TaskGroup->szExePath[0]))
            return;
        LaunchTaskPath(TaskGroup->szExePath, TaskGroup->szLinkPath[0] ? TaskGroup->szLinkPath : TaskGroup->szExePath);
    }

    BOOL IsTaskGroupLaunching(IN PTASK_GROUP TaskGroup)
    {
        if (TaskGroup->LaunchInProgress)
            return TRUE;
        if (!TaskGroup->hLaunchProcess)
            return FALSE;
        if (WaitForSingleObject(TaskGroup->hLaunchProcess, 0) == WAIT_TIMEOUT)
            return TRUE;
        ClearTaskGroupLaunch(TaskGroup);
        return FALSE;
    }

    VOID CycleGroup(IN PTASK_GROUP TaskGroup)
    {
        if (!TaskGroup || !TaskGroup->dwTaskCount)
        {
            if (TaskGroup)
                HandleTaskGroupClick(TaskGroup);
            return;
        }
        PTASK_ITEM Item, Last = m_TaskItems + m_TaskItemCount, First = NULL, Next = NULL;
        BOOL bAfterActive = FALSE;

        for (Item = m_TaskItems; Item != Last; Item++)
        {
            if (Item->Group != TaskGroup || !::IsWindow(Item->hWnd))
                continue;
            if (First == NULL)
                First = Item;
            if (bAfterActive)
            {
                Next = Item;
                break;
            }
            if (Item == m_ActiveTaskItem)
                bAfterActive = TRUE;
        }
        if (Next == NULL)
            Next = First;
        if (Next != NULL)
        {
            TaskPreview_Hide();
            ::SwitchToThisWindow(Next->hWnd, TRUE);
        }
    }

    VOID ShowHoverPreview(IN INT Index)
    {
        PTASK_GROUP TaskGroup = FindTaskGroupByIndex(Index);
        PTASK_ITEM TaskItem;
        HWND ahWnd[16];
        UINT cWindows = 0;
        INT_PTR id;
        RECT rcBtn;

        if (TaskGroup != NULL && TaskGroup->IsCollapsed)
        {
            cWindows = CollectGroupWindows(TaskGroup, ahWnd, _countof(ahWnd));
            id = (INT_PTR)TaskGroup;
        }
        else
        {
            TaskItem = FindTaskItemByIndex(Index);
            if (TaskItem == NULL || !::IsWindow(TaskItem->hWnd))
                return;
            ahWnd[cWindows++] = TaskItem->hWnd;
            id = (INT_PTR)TaskItem;
        }
        if (cWindows == 0 || TaskPreview_IsVisibleFor(id))
            return;
        if (!GetButtonScreenRect(Index, &rcBtn))
            return;
        TaskPreview_ShowHover(m_TaskBar.m_hWnd, &rcBtn, ahWnd, cWindows, id);
    }

    VOID OnHotTaskChanged(IN INT Index)
    {
        if (Index == m_HoverIndex)
            return;
        m_HoverIndex = Index;
        m_HoverPreviewPending = FALSE;
        KillTimer(TIMER_ID_HOVER_PREVIEW);
        if (Index < 0)
            return;
        if (TaskPreview_IsHover())
            ShowHoverPreview(Index);
        else
            m_HoverPreviewPending = SetTimer(TIMER_ID_HOVER_PREVIEW, HOVER_PREVIEW_DELAY, NULL) != 0;
    }

    VOID HandleTaskGroupClick(IN OUT PTASK_GROUP TaskGroup)
    {
        HWND ahWnd[16];
        UINT cWindows;

        cWindows = CollectGroupWindows(TaskGroup, ahWnd, _countof(ahWnd));
        if (!cWindows)
        {
            CStringW Target = TaskGroup->szExePath;
            RefreshWindowList();
            TaskGroup = FindTaskGroupByPath(Target);
            if (!TaskGroup)
                return;
            cWindows = CollectGroupWindows(TaskGroup, ahWnd, _countof(ahWnd));
        }
        if (cWindows == 0)
        {
            if (!IsTaskGroupLaunching(TaskGroup))
                LaunchNewInstance(TaskGroup);
            return;
        }
        if (cWindows == 1)
        {
            PTASK_ITEM TaskItem = FindTaskItem(ahWnd[0]);
            if (TaskItem)
                HandleTaskItemClick(TaskItem);
            return;
        }
        CycleGroup(TaskGroup);
    }

    BOOL HandleButtonClick(IN WORD wIndex)
    {
        PTASK_ITEM TaskItem;
        PTASK_GROUP TaskGroup;

        CancelTaskPreview();

        if (IsModernTaskbar() && (GetKeyState(VK_SHIFT) & 0x8000))
        {
            LaunchNewInstance(GroupOfIndex((INT) wIndex));
            return TRUE;
        }

        if (m_TaskGroups)
        {
            TaskGroup = FindTaskGroupByIndex((INT) wIndex);
            if (TaskGroup != NULL && TaskGroup->IsCollapsed)
            {
                if (IsModernTaskbar() && (GetKeyState(VK_CONTROL) & 0x8000))
                    CycleGroup(TaskGroup);
                else
                    HandleTaskGroupClick(TaskGroup);
                return TRUE;
            }
        }

        TaskItem = FindTaskItemByIndex((INT) wIndex);
        if (TaskItem != NULL)
        {
            HandleTaskItemClick(TaskItem);
            return TRUE;
        }

        return FALSE;
    }

    static VOID CALLBACK
    SendAsyncProc(HWND hwnd, UINT uMsg, DWORD_PTR dwData, LRESULT lResult)
    {
        ::PostMessageW(hwnd, WM_NULL, 0, 0);
    }

    VOID HandleTaskItemRightClick(IN OUT PTASK_ITEM TaskItem)
    {
        POINT pt;
        GetCursorPos(&pt);

        SetForegroundWindow(TaskItem->hWnd);

        ActivateTask(TaskItem->hWnd);

        if (GetForegroundWindow() != TaskItem->hWnd)
            ERR("HandleTaskItemRightClick detected the window did not become foreground\n");

        ::SendMessageCallbackW(TaskItem->hWnd, WM_POPUPSYSTEMMENU, 0, MAKELPARAM(pt.x, pt.y),
                               SendAsyncProc, (ULONG_PTR)TaskItem);
    }

    static VOID GetAppDisplayName(IN LPCWSTR pszExe, OUT LPWSTR pszOut, IN size_t cch)
    {
        DWORD dwHandle = 0, cb;
        LPVOID pData;
        struct { WORD wLang, wCode; } *pTrans = NULL;
        UINT cbTrans = 0, cchDesc = 0;
        LPWSTR pszDesc = NULL;
        WCHAR szSub[64];

        pszOut[0] = 0;
        if (!pszExe[0])
            return;
        cb = GetFileVersionInfoSizeW(pszExe, &dwHandle);
        if (cb != 0)
        {
            pData = HeapAlloc(hProcessHeap, 0, cb);
            if (pData != NULL)
            {
                if (GetFileVersionInfoW(pszExe, 0, cb, pData) &&
                    VerQueryValueW(pData, L"\\VarFileInfo\\Translation",
                                   (LPVOID *)&pTrans, &cbTrans) &&
                    cbTrans >= 4)
                {
                    StringCchPrintfW(szSub, _countof(szSub),
                                     L"\\StringFileInfo\\%04x%04x\\FileDescription",
                                     pTrans->wLang, pTrans->wCode);
                    if (VerQueryValueW(pData, szSub, (LPVOID *)&pszDesc, &cchDesc) &&
                        pszDesc != NULL && pszDesc[0])
                        StringCchCopyW(pszOut, cch, pszDesc);
                }
                HeapFree(hProcessHeap, 0, pData);
            }
        }
        if (!pszOut[0])
        {
            StringCchCopyW(pszOut, cch, PathFindFileNameW(pszExe));
            PathRemoveExtensionW(pszOut);
        }
    }

    INT TrackTaskMenu(IN HMENU hMenu)
    {
        POINT pt;
        INT cmd;

        GetCursorPos(&pt);
        SetForegroundWindow(m_hWnd);
        cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                               pt.x, pt.y, m_hWnd, NULL);
        PostMessageW(WM_NULL, 0, 0);
        return cmd;
    }

    VOID ShowJumpList(IN PTASK_GROUP TaskGroup, IN PTASK_ITEM TaskItem)
    {
        HMENU hMenu = CreatePopupMenu();
        WCHAR szName[128];
        CAtlArray<HWND> Windows;
        INT cmd;

        if (!hMenu)
            return;
        // Popup menus dispatch shell events: the original group may be freed while open.
        CStringW Target = TaskGroup ? TaskGroup->szExePath : L"";
        CStringW Source = TaskGroup && TaskGroup->szLinkPath[0] ? TaskGroup->szLinkPath : Target;
        HICON hPinIcon = CopyTaskButtonIcon(TaskGroup, TaskItem);
        if (TaskItem)
            Windows.Add(TaskItem->hWnd);
        else if (TaskGroup)
        {
            for (SIZE_T i = 0; i < m_TaskItemCount; ++i)
            {
                if (m_TaskItems[i].Group == TaskGroup && ::IsWindow(m_TaskItems[i].hWnd))
                    Windows.Add(m_TaskItems[i].hWnd);
            }
        }
        // Refresh after taking snapshots: a pin change may replace or free the group.
        SyncTaskbarPins();
        TaskGroup = FindTaskGroupByPath(Target);
        BOOL bPinned = TaskGroup && TaskGroup->IsPinned;
        Source = bPinned ? TaskGroup->szLinkPath : Target;
        TaskPreview_Hide();
        GetAppDisplayName(Target, szName, _countof(szName));
        if (szName[0])
            AppendMenuW(hMenu, MF_STRING, IDM_JUMP_LAUNCH, szName);

        if (!TaskbarPin_IsDisabled() && (bPinned || TaskbarPin_IsPinnable(Source)))
        {
            if (GetMenuItemCount(hMenu) > 0)
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, bPinned ? IDM_JUMP_UNPIN : IDM_JUMP_PIN, bPinned ? L"Unpin this program from taskbar" : L"Pin this program to taskbar");
        }

        if (Windows.GetCount() > 0)
        {
            if (GetMenuItemCount(hMenu) > 0)
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_JUMP_CLOSE, Windows.GetCount() > 1 ? L"Close all windows" : L"Close window");
        }
        cmd = TrackTaskMenu(hMenu);
        DestroyMenu(hMenu);
        if (cmd == IDM_JUMP_LAUNCH)
            LaunchTaskPath(Target, Source);
        else if (cmd == IDM_JUMP_PIN || cmd == IDM_JUMP_UNPIN)
        {
            CStringW Shortcut;
            HRESULT hr;
            if (cmd == IDM_JUMP_PIN)
            {
                hr = TaskbarPin_Create(Source, &Shortcut, hPinIcon);
                if (SUCCEEDED(hr))
                    hr = ApplySavedTaskbarPin(Target, Shortcut, hPinIcon);
            }
            else
            {
                hr = TaskbarPin_Remove(Target);
                if (SUCCEEDED(hr))
                {
                    TaskGroup = FindTaskGroupByPath(Target);
                    if (TaskGroup && TaskGroup->IsPinned)
                        SetTaskGroupPin(TaskGroup, NULL);
                }
            }
            if (SUCCEEDED(hr))
                SyncTaskbarPins();
            else
            {
                ERR("Taskbar %s failed for %S: %08lx\n", cmd == IDM_JUMP_PIN ? "pin" : "unpin", Target.GetString(), hr);
                MessageBoxW(cmd == IDM_JUMP_PIN ? L"The app could not be pinned to the taskbar." : L"The app could not be unpinned from the taskbar.", L"Taskbar", MB_OK | MB_ICONERROR);
            }
        }
        else if (cmd == IDM_JUMP_CLOSE)
        {
            for (SIZE_T i = 0; i < Windows.GetCount(); ++i)
                ::PostMessageW(Windows[i], WM_CLOSE, 0, 0);
        }
        if (hPinIcon)
            DestroyIcon(hPinIcon);
    }

    VOID ShowGroupMenu(IN PTASK_GROUP TaskGroup)
    {
        HMENU hMenu = CreatePopupMenu();
        HWND ahWnd[16];
        UINT cWindows, i;
        INT cmd;

        if (!hMenu)
            return;
        TaskPreview_Hide();
        cWindows = CollectGroupWindows(TaskGroup, ahWnd, _countof(ahWnd));
        AppendMenuW(hMenu, MF_STRING, IDM_GROUP_CASCADE, L"Cascade");
        AppendMenuW(hMenu, MF_STRING, IDM_GROUP_STACKED, L"Show windows stacked");
        AppendMenuW(hMenu, MF_STRING, IDM_GROUP_SIDEBYSIDE, L"Show windows side by side");
        AppendMenuW(hMenu, MF_STRING, IDM_GROUP_MINIMIZE, L"Minimize all windows");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_GROUP_CLOSE, L"Close all windows");
        cmd = TrackTaskMenu(hMenu);
        DestroyMenu(hMenu);
        if (cmd == 0 || cWindows == 0)
            return;
        if (cmd == IDM_GROUP_CASCADE || cmd == IDM_GROUP_STACKED || cmd == IDM_GROUP_SIDEBYSIDE)
        {
            for (i = 0; i < cWindows; i++)
            {
                if (::IsIconic(ahWnd[i]))
                    ::ShowWindow(ahWnd[i], SW_RESTORE);
            }
            if (cmd == IDM_GROUP_CASCADE)
                CascadeWindows(NULL, 0, NULL, cWindows, ahWnd);
            else
                TileWindows(NULL, cmd == IDM_GROUP_STACKED ? MDITILE_HORIZONTAL : MDITILE_VERTICAL,
                            NULL, cWindows, ahWnd);
        }
        else if (cmd == IDM_GROUP_MINIMIZE)
        {
            for (i = 0; i < cWindows; i++)
                ::ShowWindowAsync(ahWnd[i], SW_MINIMIZE);
        }
        else if (cmd == IDM_GROUP_CLOSE)
        {
            for (i = 0; i < cWindows; i++)
                ::PostMessageW(ahWnd[i], WM_CLOSE, 0, 0);
        }
    }

    VOID HandleTaskGroupRightClick(IN OUT PTASK_GROUP TaskGroup)
    {
        if ((GetKeyState(VK_SHIFT) & 0x8000) && TaskGroup->dwTaskCount > 1)
            ShowGroupMenu(TaskGroup);
        else if ((GetKeyState(VK_SHIFT) & 0x8000) && TaskGroup->dwTaskCount == 1)
        {
            PTASK_ITEM Item = FirstTaskOfGroup(TaskGroup);
            if (Item)
                HandleTaskItemRightClick(Item);
        }
        else
            ShowJumpList(TaskGroup, NULL);
    }

    BOOL HandleButtonRightClick(IN WORD wIndex)
    {
        PTASK_ITEM TaskItem;
        PTASK_GROUP TaskGroup;
        CancelTaskPreview();
        if (m_TaskGroups)
        {
            TaskGroup = FindTaskGroupByIndex((INT) wIndex);
            if (TaskGroup != NULL && TaskGroup->IsCollapsed)
            {
                HandleTaskGroupRightClick(TaskGroup);
                return TRUE;
            }
        }

        TaskItem = FindTaskItemByIndex((INT) wIndex);

        if (TaskItem != NULL)
        {
            if (IsModernTaskbar() && !(GetKeyState(VK_SHIFT) & 0x8000))
                ShowJumpList(TaskItem->Group, TaskItem);
            else
                HandleTaskItemRightClick(TaskItem);
            return TRUE;
        }

        return FALSE;
    }


    static COLORREF IconDominantColor(HICON hIcon, COLORREF crFallback)
    {
        ICONINFO ii;
        BITMAP bm;
        BITMAPINFO bmi;
        DWORD *pBits = NULL;
        HDC hdc = NULL;
        ULONGLONG sumR = 0, sumG = 0, sumB = 0, sumW = 0;
        COLORREF cr = crFallback;
        INT x, y, mx;

        if (!hIcon || !GetIconInfo(hIcon, &ii))
            return crFallback;
        if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof(bm), &bm) &&
            bm.bmWidth > 0 && bm.bmHeight > 0 && bm.bmWidth * bm.bmHeight <= 256 * 256)
        {
            ZeroMemory(&bmi, sizeof(bmi));
            bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
            bmi.bmiHeader.biWidth = bm.bmWidth;
            bmi.bmiHeader.biHeight = -bm.bmHeight;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            pBits = (DWORD *)HeapAlloc(hProcessHeap, 0, (SIZE_T)bm.bmWidth * bm.bmHeight * 4);
            hdc = ::GetDC(NULL);
            if (pBits && hdc &&
                GetDIBits(hdc, ii.hbmColor, 0, bm.bmHeight, pBits, &bmi, DIB_RGB_COLORS))
            {
                BOOL bHasAlpha = FALSE;

                for (y = 0; y < bm.bmWidth * bm.bmHeight; y++)
                {
                    if (pBits[y] & 0xFF000000)
                    {
                        bHasAlpha = TRUE;
                        break;
                    }
                }
                for (y = 0; y < bm.bmHeight; y++)
                {
                    for (x = 0; x < bm.bmWidth; x++)
                    {
                        DWORD px = pBits[y * bm.bmWidth + x];
                        INT a = bHasAlpha ? (INT)(px >> 24) : 255;
                        INT r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
                        INT hi = max(r, max(g, b)), lo = min(r, min(g, b));
                        ULONGLONG w;

                        if (a < 128 || hi < 48)
                            continue;
                        w = (ULONGLONG)(hi - lo + 6) * a;
                        sumR += r * w;
                        sumG += g * w;
                        sumB += b * w;
                        sumW += w;
                    }
                }
                if (sumW != 0)
                    cr = RGB(sumR / sumW, sumG / sumW, sumB / sumW);
            }
            if (hdc)
                ::ReleaseDC(NULL, hdc);
            if (pBits)
                HeapFree(hProcessHeap, 0, pBits);
        }
        if (ii.hbmColor)
            DeleteObject(ii.hbmColor);
        if (ii.hbmMask)
            DeleteObject(ii.hbmMask);
        mx = max(GetRValue(cr), max(GetGValue(cr), GetBValue(cr)));
        if (mx > 0 && mx < 255)
        {
            cr = RGB(min(255, GetRValue(cr) * 255 / mx),
                     min(255, GetGValue(cr) * 255 / mx),
                     min(255, GetBValue(cr) * 255 / mx));
        }
        return cr;
    }

    COLORREF GlowColorForIcon(INT IconIndex)
    {
        INT count = m_ImageList ? ImageList_GetImageCount(m_ImageList) : 0;
        HICON hIcon;

        if (IconIndex == m_GlowCacheIcon && count == m_GlowCacheCount)
            return m_crGlowCache;
        hIcon = m_ImageList ? ImageList_GetIcon(m_ImageList, IconIndex, ILD_TRANSPARENT) : NULL;
        m_crGlowCache = IconDominantColor(hIcon, RGB(110, 170, 255));
        if (hIcon)
            DestroyIcon(hIcon);
        m_GlowCacheIcon = IconIndex;
        m_GlowCacheCount = count;
        return m_crGlowCache;
    }

    VOID DrawWin7Well(HDC hdc, const RECT *prc, INT radius, INT liftFill, INT liftEdge,
                      BOOL bGlow, COLORREF crGlow, POINT ptGlow, INT amp,
                      HTHEME hTheme, INT state)
    {
        INT w = prc->right - prc->left, h = prc->bottom - prc->top;
        COLORREF crFill = ShellLiftColor(m_crMaterial, liftFill);
        COLORREF crEdge = ShellLiftColor(m_crMaterial, liftEdge);
        HBRUSH hbrFill = CreateSolidBrush(crFill);
        HPEN hpenEdge = CreatePen(PS_SOLID, 1, crEdge);
        HGDIOBJ hbrOld, hpenOld;

        if (w <= 0 || h <= 0 || !hbrFill || !hpenEdge)
        {
            if (hbrFill) DeleteObject(hbrFill);
            if (hpenEdge) DeleteObject(hpenEdge);
            return;
        }
        /* Use the task-band artwork, including its translucent face and inner
           highlight. An opaque material fill hides both the glass and theme. */
        BOOL bThemed = hTheme && SUCCEEDED(DrawThemeBackground(hTheme, hdc, TP_BUTTON, state, prc, prc));
        if (!bThemed)
        {
            hbrOld = SelectObject(hdc, hbrFill);
            hpenOld = SelectObject(hdc, GetStockObject(NULL_PEN));
            RoundRect(hdc, prc->left, prc->top, prc->right + 1, prc->bottom + 1, radius, radius);
            SelectObject(hdc, hpenOld);
            SelectObject(hdc, hbrOld);
        }
        if (bGlow)
        {
            BITMAPINFO bmi;
            DWORD *pBits = NULL;
            HBITMAP hbm;
            HDC hdcMem = CreateCompatibleDC(hdc);

            ZeroMemory(&bmi, sizeof(bmi));
            bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
            bmi.bmiHeader.biWidth = w;
            bmi.bmiHeader.biHeight = -h;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&pBits, NULL, 0);
            if (hdcMem && hbm && pBits)
            {
                HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);
                HRGN hrgn = CreateRoundRectRgn(prc->left, prc->top, prc->right + 1,
                                               prc->bottom + 1, radius, radius);
                INT gx = ptGlow.x - prc->left, gy = ptGlow.y - prc->top;
                INT R = max(w, h) * 4 / 5;
                INT R2 = R * R;
                INT gr = GetRValue(crGlow), gg = GetGValue(crGlow), gb = GetBValue(crGlow);
                INT x, y;

                /* Add the icon glow to the rendered glass, preserving its
                   reflection and the backdrop underneath it. */
                BOOL bCopied = BitBlt(hdcMem, 0, 0, w, h, hdc, prc->left, prc->top, SRCCOPY);
                GdiFlush();
                for (y = 0; bCopied && y < h; y++)
                {
                    for (x = 0; x < w; x++)
                    {
                        INT dx = x - gx, dy = y - gy;
                        INT d2 = dx * dx + dy * dy;
                        DWORD px = pBits[y * w + x];
                        INT r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;

                        if (d2 < R2)
                        {
                            INT f = 255 - d2 * 255 / R2;
                            INT t = f * f / 255;
                            INT add = amp * t / 255;

                            r = min(255, r + gr * add / 255);
                            g = min(255, g + gg * add / 255);
                            b = min(255, b + gb * add / 255);
                        }
                        pBits[y * w + x] = ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
                    }
                }
                INT saved = bCopied ? SaveDC(hdc) : 0;
                if (saved)
                {
                    if (hrgn)
                        ExtSelectClipRgn(hdc, hrgn, RGN_AND);
                    /* Keep the theme's two edge pixels crisp. */
                    if (bThemed)
                        IntersectClipRect(hdc, prc->left + 2, prc->top + 2, prc->right - 2, prc->bottom - 2);
                    BitBlt(hdc, prc->left, prc->top, w, h, hdcMem, 0, 0, SRCCOPY);
                    RestoreDC(hdc, saved);
                }
                if (hrgn)
                    DeleteObject(hrgn);
                SelectObject(hdcMem, hbmOld);
            }
            if (hbm)
                DeleteObject(hbm);
            if (hdcMem)
                DeleteDC(hdcMem);
        }
        if (!bThemed)
        {
            hbrOld = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            hpenOld = SelectObject(hdc, hpenEdge);
            RoundRect(hdc, prc->left, prc->top, prc->right, prc->bottom, radius, radius);
            SelectObject(hdc, hpenOld);
            SelectObject(hdc, hbrOld);
        }
        DeleteObject(hpenEdge);
        DeleteObject(hbrFill);
    }

    static VOID DrawVertGradient(HDC hdc, const RECT *prc, COLORREF top, COLORREF bottom)
    {
        int h = prc->bottom - prc->top;
        if (h <= 0)
            return;
        for (int y = 0; y < h; y++)
        {
            RECT rcLine = { prc->left, prc->top + y, prc->right, prc->top + y + 1 };
            COLORREF clr = RGB(GetRValue(top) + MulDiv(GetRValue(bottom) - GetRValue(top), y, h),
                               GetGValue(top) + MulDiv(GetGValue(bottom) - GetGValue(top), y, h),
                               GetBValue(top) + MulDiv(GetBValue(bottom) - GetBValue(top), y, h));
            HBRUSH hbr = CreateSolidBrush(clr);
            FillRect(hdc, &rcLine, hbr);
            DeleteObject(hbr);
        }
    }

    LRESULT DrawWin7TaskButtonWorker(IN OUT NMTBCUSTOMDRAW *nmtbcd, IN INT IconIndex, IN INT nLayers)
    {
        HDC hdc = nmtbcd->nmcd.hdc;
        RECT rc = nmtbcd->nmcd.rc;
        UINT uState = nmtbcd->nmcd.uItemState;
        BOOL bChecked = (uState & CDIS_CHECKED);
        BOOL bHot = (uState & CDIS_HOT);
        BOOL bPressed = (uState & CDIS_SELECTED);
        INT nStep = ShellScaleForDpi(6);
        RECT rcFace = rc;
        COLORREF crEdge = 0, crTop = 0, crBottom = 0;
        BOOL bFill = FALSE;

        if (nLayers > 3)
            nLayers = 3;
        if (nLayers > 1)
            rcFace.right -= nStep * (nLayers - 1);

        if (m_bMaterial && (nLayers > 0 || bHot || bPressed || bChecked))
        {
            BOOL bFlash = (uState & CDIS_MARKED) != 0;
            HTHEME hButtonTheme = GetWindowTheme(m_TaskBar.m_hWnd);
            INT state = bPressed ? TS_PRESSED :
                        bChecked ? (bHot ? TS_HOTCHECKED : TS_CHECKED) :
                        bHot ? TS_HOT : TS_NORMAL;
            RECT rcWell = rcFace;
            INT radius = ShellScaleForDpi(3);
            INT liftFill, liftEdge, amp, i;
            BOOL bGlow = bHot || bChecked || bPressed || bFlash;
            COLORREF crGlow = 0;
            POINT ptGlow;

            if (hButtonTheme && !IsThemePartDefined(hButtonTheme, TP_BUTTON, 0))
                hButtonTheme = NULL;
            /* The transparent toolbar has already painted the parent backdrop
               before notifying us, including on mouse-move redraws. */
            InflateRect(&rcWell, -ShellScaleForDpi(1), -ShellScaleForDpi(2));
            if (bFlash)        { liftFill = 12; liftEdge = 60; amp = 40; }
            else if (bPressed) { liftFill = 5;  liftEdge = 40; amp = 14; }
            else if (bChecked) { liftFill = 18; liftEdge = 56; amp = bHot ? 38 : 24; }
            else if (bHot)     { liftFill = 12; liftEdge = 48; amp = 34; }
            else               { liftFill = 8;  liftEdge = 28; amp = 0; }

            ptGlow.x = (rcWell.left + rcWell.right) / 2;
            ptGlow.y = rcWell.bottom - (rcWell.bottom - rcWell.top) / 4;
            if (bGlow)
            {
                if (bFlash)
                    crGlow = RGB(255, 160, 40);
                else
                    crGlow = GlowColorForIcon(IconIndex);
                if (bHot && !bFlash)
                {
                    POINT pt;

                    GetCursorPos(&pt);
                    ::ScreenToClient(m_TaskBar.m_hWnd, &pt);
                    if (pt.x >= rcWell.left && pt.x < rcWell.right)
                        ptGlow.x = pt.x;
                    if (pt.y >= rcWell.top && pt.y < rcWell.bottom)
                        ptGlow.y = pt.y;
                }
            }
            for (i = nLayers - 1; i >= 1; i--)
            {
                RECT rcBack = rcWell;

                OffsetRect(&rcBack, i * nStep, 0);
                INT saved = SaveDC(hdc);
                if (!saved)
                    continue;
                /* Only the exposed strip belongs to a rear layer. Blending
                   whole translucent buttons stacks their reflections across
                   the front face and leaks their left edges through it. */
                if (hButtonTheme)
                    IntersectClipRect(hdc, rcBack.right - nStep, rcBack.top, rcBack.right, rcBack.bottom);
                DrawWin7Well(hdc, &rcBack, radius, max(0, liftFill - i), max(0, liftEdge - i * 6), FALSE, 0, ptGlow, 0, hButtonTheme, TS_NORMAL);
                RestoreDC(hdc, saved);
            }
            DrawWin7Well(hdc, &rcWell, radius, liftFill, liftEdge, bGlow, crGlow, ptGlow, amp, hButtonTheme, state);
            if (!hButtonTheme && bChecked && !bPressed)
            {
                RECT rcLight = { rcWell.left + radius / 2, rcWell.top + 1,
                                 rcWell.right - radius / 2, rcWell.top + 2 };
                HBRUSH hbrLight = CreateSolidBrush(ShellLiftColor(m_crMaterial, 72));

                FillRect(hdc, &rcLight, hbrLight);
                DeleteObject(hbrLight);
            }
            crEdge = ShellLiftColor(m_crMaterial, liftEdge);
            bFill = TRUE;
        }
        else if (bPressed || bChecked)
        {
            crTop = RGB(26, 29, 32);
            crBottom = RGB(44, 49, 54);
            crEdge = RGB(88, 95, 102);
            bFill = TRUE;
        }
        else if (bHot)
        {
            crTop = RGB(78, 84, 90);
            crBottom = RGB(46, 51, 56);
            crEdge = RGB(102, 110, 118);
            bFill = TRUE;
        }

        if (bFill && !m_bMaterial)
        {
            HBRUSH hbrEdge = CreateSolidBrush(crEdge);
            INT i;

            for (i = nLayers - 1; i >= 1; i--)
            {
                RECT rcBack = rcFace;

                OffsetRect(&rcBack, i * nStep, 0);
                DrawVertGradient(hdc, &rcBack, ShellLiftColor(crTop, -8),
                                 ShellLiftColor(crBottom, -8));
                FrameRect(hdc, &rcBack, hbrEdge);
            }
            DrawVertGradient(hdc, &rcFace, crTop, crBottom);
            FrameRect(hdc, &rcFace, hbrEdge);
            DeleteObject(hbrEdge);
        }

        if (IconIndex >= 0 && m_ImageList)
        {
            INT cx = 0, cy = 0, x, y;

            if (!ImageList_GetIconSize(m_ImageList, &cx, &cy) || cx <= 0 || cy <= 0)
            {
                cx = GetSystemMetrics(UseSmallTaskIcons() ? SM_CXSMICON : SM_CXICON);
                cy = GetSystemMetrics(UseSmallTaskIcons() ? SM_CYSMICON : SM_CYICON);
            }
            x = rcFace.left + ((rcFace.right - rcFace.left) - cx) / 2;
            y = rcFace.top + ((rcFace.bottom - rcFace.top) - cy) / 2;
            ImageList_Draw(m_ImageList, IconIndex, hdc, x, y, ILD_TRANSPARENT);
        }

        return CDRF_SKIPDEFAULT;
    }

    LRESULT DrawWin7TaskButton(IN OUT NMTBCUSTOMDRAW *nmtbcd, IN PTASK_ITEM TaskItem)
    {
        return DrawWin7TaskButtonWorker(nmtbcd, TaskItem->IconIndex, 1);
    }

    VOID DrawDraggedTaskButton(IN const NMTBCUSTOMDRAW *nmtbcd)
    {
        NMTBCUSTOMDRAW cd;
        RECT rc;
        POINT pt;
        INT Index = DragButtonIndex();
        PTASK_GROUP TaskGroup;
        PTASK_ITEM TaskItem;

        if (Index < 0 || !m_TaskBar.GetItemRect(Index, &rc) || !GetDragOffset(&pt))
            return;
        OffsetRect(&rc, pt.x, pt.y);
        TaskGroup = FindTaskGroupByIndex(Index);
        TaskItem = TaskGroup ? NULL : FindTaskItemByIndex(Index);

        ZeroMemory(&cd, sizeof(cd));
        cd.nmcd.hdc = nmtbcd->nmcd.hdc;
        cd.nmcd.rc = rc;
        cd.nmcd.dwItemSpec = (DWORD_PTR)Index;
        cd.nmcd.uItemState = CDIS_HOT;
        if (TaskGroup ? GroupContainsActive(TaskGroup) : (TaskItem && m_ActiveTaskItem == TaskItem))
            cd.nmcd.uItemState |= CDIS_CHECKED;
        if (TaskGroup)
            DrawWin7TaskButtonWorker(&cd, TaskGroup->IconIndex, (INT)TaskGroup->dwTaskCount);
        else if (TaskItem)
            DrawWin7TaskButton(&cd, TaskItem);
    }

    LRESULT HandleItemPaint(IN OUT NMTBCUSTOMDRAW *nmtbcd)
    {
        LRESULT Ret = CDRF_DODEFAULT;
        PTASK_GROUP TaskGroup;
        PTASK_ITEM TaskItem;

        TaskItem = FindTaskItemByIndex((INT) nmtbcd->nmcd.dwItemSpec);
        TaskGroup = FindTaskGroupByIndex((INT) nmtbcd->nmcd.dwItemSpec);
        if (IsModernTaskbar())
        {
            PTASK_ANIM pAnim = TaskGroup ? &TaskGroup->Anim : TaskItem ? &TaskItem->Anim : NULL;
            POINT pt;

            if ((INT)nmtbcd->nmcd.dwItemSpec == DragButtonIndex())
                return CDRF_SKIPDEFAULT;
            GetButtonVisualOffset(pAnim, (INT)nmtbcd->nmcd.dwItemSpec, &pt);
            if (pt.x || pt.y)
                OffsetRect(&nmtbcd->nmcd.rc, pt.x, pt.y);
        }
        if (TaskGroup == NULL && TaskItem != NULL)
        {
            ASSERT(TaskItem != NULL);

            if (TaskItem != NULL && ::IsWindow(TaskItem->hWnd))
            {
                /* Make the entire button flashing if necessary */
                if (nmtbcd->nmcd.uItemState & CDIS_MARKED)
                {
                    if (IsModernTaskbar() && m_bMaterial)
                        return DrawWin7TaskButton(nmtbcd, TaskItem);
                    Ret = TBCDRF_NOBACKGROUND;
                    if (!m_Theme)
                    {
                        SelectObject(nmtbcd->nmcd.hdc, GetSysColorBrush(COLOR_HIGHLIGHT));
                        Rectangle(nmtbcd->nmcd.hdc,
                            nmtbcd->nmcd.rc.left,
                            nmtbcd->nmcd.rc.top,
                            nmtbcd->nmcd.rc.right,
                            nmtbcd->nmcd.rc.bottom);
                    }
                    else
                    {
                        DrawThemeBackground(m_Theme, nmtbcd->nmcd.hdc, TDP_FLASHBUTTON, 0, &nmtbcd->nmcd.rc, 0);
                    }
                    nmtbcd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
                    return Ret;
                }

                if (IsModernTaskbar())
                    return DrawWin7TaskButton(nmtbcd, TaskItem);
            }
        }
        else if (TaskGroup != NULL)
        {
            if (IsModernTaskbar())
                return DrawWin7TaskButtonWorker(nmtbcd, TaskGroup->IconIndex,
                                                (INT)TaskGroup->dwTaskCount);
        }
        return Ret;
    }

    LRESULT HandleToolbarNotification(IN const NMHDR *nmh)
    {
        LRESULT Ret = 0;

        switch (nmh->code)
        {
        case TBN_GETINFOTIPW:
        {
            LPNMTBGETINFOTIPW pTip = (LPNMTBGETINFOTIPW) nmh;
            PTASK_ITEM TaskItem = FindTaskItemByIndex(pTip->iItem);
            if (TaskItem == NULL)
            {
                PTASK_GROUP TaskGroup = FindTaskGroupByIndex(pTip->iItem);
                if (TaskGroup != NULL)
                    TaskItem = FirstTaskOfGroup(TaskGroup);
                if (TaskItem != NULL && pTip->pszText != NULL && pTip->cchTextMax > 0)
                {
                    WCHAR szTitle[255];
                    szTitle[0] = 0;
                    GetWndTextFromTaskItem(TaskItem, szTitle, _countof(szTitle));
                    StringCchPrintfW(pTip->pszText, pTip->cchTextMax, L"%s (%lu)",
                                     szTitle, TaskGroup->dwTaskCount);
                }
                else if (TaskGroup != NULL && TaskGroup->IsPinned &&
                         pTip->pszText != NULL && pTip->cchTextMax > 0)
                {
                    GetAppDisplayName(TaskGroup->szExePath, pTip->pszText,
                                      pTip->cchTextMax);
                }
                break;
            }
            if (pTip->pszText != NULL && pTip->cchTextMax > 0)
                GetWndTextFromTaskItem(TaskItem, pTip->pszText, pTip->cchTextMax);
            break;
        }

        case TBN_HOTITEMCHANGE:
        {
            const NMTBHOTITEM *pHot = (const NMTBHOTITEM *)nmh;

            if (IsModernTaskbar())
                OnHotTaskChanged((pHot->dwFlags & HICF_LEAVING) ? -1 : pHot->idNew);
            break;
        }

        case NM_CUSTOMDRAW:
        {
            LPNMTBCUSTOMDRAW nmtbcd = (LPNMTBCUSTOMDRAW) nmh;

            switch (nmtbcd->nmcd.dwDrawStage)
            {

            case CDDS_ITEMPREPAINT:
                Ret = HandleItemPaint(nmtbcd);
                break;

            case CDDS_PREPAINT:
                Ret = CDRF_NOTIFYITEMDRAW;
                if (m_bDragging && IsModernTaskbar())
                    Ret |= CDRF_NOTIFYPOSTPAINT;
                break;

            case CDDS_POSTPAINT:
                if (m_bDragging && IsModernTaskbar())
                    DrawDraggedTaskButton(nmtbcd);
                Ret = CDRF_DODEFAULT;
                break;

            default:
                Ret = CDRF_DODEFAULT;
                break;
            }
            break;
        }
        }

        return Ret;
    }

    // Internal structure for IsRudeEnumProc
    typedef struct tagRUDEAPPDATA
    {
        HMONITOR hTargetMonitor;
        HWND hwndFound;
        HWND hwndFirstCheck;
    } RUDEAPPDATA, *PRUDEAPPDATA;

    // Find any rude app
    static BOOL CALLBACK
    IsRudeEnumProc(_In_ HWND hwnd, _In_ LPARAM lParam)
    {
        PRUDEAPPDATA pData = (PRUDEAPPDATA)lParam;

        HMONITOR hMon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (!hMon ||
            (pData->hTargetMonitor && pData->hTargetMonitor != hMon) ||
            !SHELL_IsRudeWindow(hMon, hwnd, (hwnd == pData->hwndFirstCheck)))
        {
            return TRUE; // Continue
        }

        pData->hwndFound = hwnd;
        return FALSE; // Finish
    }

    // Internal structure for FullScreenEnumProc
    typedef struct tagFULLSCREENDATA
    {
        const RECT *pRect;
        HMONITOR hTargetMonitor;
        ITrayWindow *pTray;
    } FULLSCREENDATA, *PFULLSCREENDATA;

    // Notify ABN_FULLSCREENAPP for each monitor
    static BOOL CALLBACK
    FullScreenEnumProc(_In_ HMONITOR hMonitor, _In_opt_ HDC hDC, _In_ LPRECT prc, _In_ LPARAM lParam)
    {
        PFULLSCREENDATA pData = (PFULLSCREENDATA)lParam;

        BOOL bFullOpening = (pData->hTargetMonitor == hMonitor);
        if (!bFullOpening && pData->pRect)
        {
            RECT rc, rcMon;
            SHELL_GetMonitorRect(hMonitor, &rcMon, FALSE);
            ::IntersectRect(&rc, &rcMon, pData->pRect);
            bFullOpening = ::EqualRect(&rc, &rcMon);
        }

        // Notify ABN_FULLSCREENAPP to appbars
        pData->pTray->NotifyFullScreenToAppBars(hMonitor, bFullOpening);
        return TRUE;
    }

    void HandleFullScreenApp(_In_opt_ HWND hwndRude)
    {
        // Notify ABN_FULLSCREENAPP for every monitor
        RECT rc;
        FULLSCREENDATA Data = { NULL, NULL, NULL };
        if (hwndRude && ::GetWindowRect(hwndRude, &rc))
        {
            Data.pRect = &rc;
            Data.hTargetMonitor = ::MonitorFromWindow(hwndRude, MONITOR_DEFAULTTONULL);
        }
        Data.pTray = m_Tray;
        ::EnumDisplayMonitors(NULL, NULL, FullScreenEnumProc, (LPARAM)&Data);

        if (hwndRude)
        {
            if (!g_TaskbarSettings.sr.AlwaysOnTop)
            {
                // Make the taskbar bottom
                UINT uFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
                HWND hwndTray = m_Tray->GetHWND();
                ::SetWindowPos(hwndTray, HWND_BOTTOM, 0, 0, 0, 0, uFlags);
            }

            // Switch to the rude app if necessary
            DWORD exstyle = (DWORD)::GetWindowLongPtrW(hwndRude, GWL_EXSTYLE);
            if (!(exstyle & WS_EX_TOPMOST) && !SHELL_IsRudeWindowActive(hwndRude))
                ::SwitchToThisWindow(hwndRude, TRUE);
        }
    }

    HWND FindRudeApp(_In_opt_ HWND hwndFirstCheck)
    {
        // Quick check
        HMONITOR hMon = MonitorFromWindow(hwndFirstCheck, MONITOR_DEFAULTTONEAREST);
        RUDEAPPDATA data = { hMon, NULL, hwndFirstCheck };
        if (::IsWindow(hwndFirstCheck) && !IsRudeEnumProc(hwndFirstCheck, (LPARAM)&data))
            return hwndFirstCheck;

        // Slow check
        ::EnumWindows(IsRudeEnumProc, (LPARAM)&data);

        return data.hwndFound;
    }

    // WM_WINDOWPOSCHANGED
    LRESULT OnWindowPosChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        // Re-start rude app validation
        KillTimer(TIMER_ID_VALIDATE_RUDE_APP);
        SetTimer(TIMER_ID_VALIDATE_RUDE_APP, VALIDATE_RUDE_INTERVAL, NULL);
        m_nRudeAppValidationCounter = 0;
        bHandled = FALSE;
        return 0;
    }

    // HSHELL_WINDOWACTIVATED, HSHELL_RUDEAPPACTIVATED
    void OnWindowActivated(_In_ HWND hwndTarget)
    {
        // Re-start rude app validation
        KillTimer(TIMER_ID_VALIDATE_RUDE_APP);
        SetTimer(TIMER_ID_VALIDATE_RUDE_APP, VALIDATE_RUDE_INTERVAL, NULL);
        m_nRudeAppValidationCounter = 0;
    }

    // HSHELL_WINDOWDESTROYED
    void OnWindowDestroyed(_In_ HWND hwndTarget)
    {
        if (!FindTaskItem(hwndTarget))
            return;
        HWND hwndRude = FindRudeApp(hwndTarget);
        HandleFullScreenApp(hwndRude);
    }

    LRESULT OnEraseBackground(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        HDC hdc = (HDC) wParam;

        if (!IsAppThemed())
        {
            bHandled = FALSE;
            return 0;
        }

        RECT rect;
        GetClientRect(&rect);
        DrawThemeParentBackground(m_hWnd, hdc, &rect);

        return TRUE;
    }

    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        SIZE szClient;

        szClient.cx = LOWORD(lParam);
        szClient.cy = HIWORD(lParam);
        if (m_TaskBar.m_hWnd != NULL)
        {
            m_TaskBar.SetWindowPos(NULL, 0, 0, szClient.cx, szClient.cy, SWP_NOZORDER);

            UpdateButtonsSize(FALSE);
        }
        return TRUE;
    }

    LRESULT OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        LRESULT Ret = TRUE;
        /* We want the tray window to be draggable everywhere, so make the control
        appear transparent */
        Ret = DefWindowProc(uMsg, wParam, lParam);
        if (Ret != HTVSCROLL && Ret != HTHSCROLL)
            Ret = HTTRANSPARENT;
        return Ret;
    }

    LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        LRESULT Ret = TRUE;
        if (lParam != 0 && (HWND) lParam == m_TaskBar.m_hWnd)
        {
            HandleButtonClick(LOWORD(wParam));
        }
        return Ret;
    }

    LRESULT OnNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        LRESULT Ret = TRUE;
        const NMHDR *nmh = (const NMHDR *) lParam;

        if (nmh->hwndFrom == m_TaskBar.m_hWnd)
        {
            Ret = HandleToolbarNotification(nmh);
        }
        return Ret;
    }

    LRESULT OnUpdateTaskbarPos(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        SetWindowTheme(m_TaskBar.m_hWnd, m_Tray->IsHorizontal() ? L"TaskBand" : L"TaskBandVert", NULL);
        /* Update the button spacing */
        m_TaskBar.UpdateTbButtonSpacing(m_Tray->IsHorizontal(), m_Theme != NULL);
        UpdateButtonsSize(FALSE);
        return TRUE;
    }

    LRESULT OnTaskbarSettingsChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        BOOL bSettingsChanged = FALSE;
        TaskbarSettings* newSettings = (TaskbarSettings*)lParam;

        if (newSettings->bGroupButtons != g_TaskbarSettings.bGroupButtons)
        {
            bSettingsChanged = TRUE;
            g_TaskbarSettings.bGroupButtons = newSettings->bGroupButtons;
            m_IsGroupingEnabled = g_TaskbarSettings.bGroupButtons || IsModernTaskbar();
        }

        if (newSettings->bSmallIcons != g_TaskbarSettings.bSmallIcons)
        {
            bSettingsChanged = TRUE;
            g_TaskbarSettings.bSmallIcons = newSettings->bSmallIcons;
        }

        if (bSettingsChanged)
        {
            UpdateTaskGroupLayout();
            /* Refresh each task item view */
            RefreshWindowList();
            UpdateButtonsSize(FALSE);
        }

        return 0;
    }

    LRESULT OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        LRESULT Ret = 0;
        INT_PTR iBtn = -1;

        if (m_TaskBar.m_hWnd != NULL)
        {
            POINT pt;

            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            ::ScreenToClient(m_TaskBar.m_hWnd, &pt);

            iBtn = m_TaskBar.HitTest(&pt);
            if (iBtn >= 0)
            {
                HandleButtonRightClick(iBtn);
            }
        }
        if (iBtn < 0)
        {
            /* Not on a taskbar button, so forward message to tray */
            Ret = SendMessage(m_Tray->GetHWND(), uMsg, wParam, lParam);
        }
        return Ret;
    }

    LRESULT OnKludgeItemRect(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        PTASK_ITEM TaskItem = FindTaskItem((HWND) wParam);
        if (TaskItem)
        {
            RECT* prcMinRect = (RECT*) lParam;
            RECT rcItem = { 0 }, rcToolbar;
            INT iIndex = TaskItem->Index;

            if (iIndex < 0 && TaskItem->Group != NULL)
                iIndex = TaskItem->Group->Index;
            if (iIndex < 0 || !m_TaskBar.GetItemRect(iIndex, &rcItem))
                return FALSE;

            m_TaskBar.GetWindowRect(&rcToolbar);

            OffsetRect(&rcItem, rcToolbar.left, rcToolbar.top);

            *prcMinRect = rcItem;
            return TRUE;
        }
        return FALSE;
    }

    LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        return MA_NOACTIVATE;
    }

    LRESULT OnTaskDragBegin(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        return BeginTaskDrag((INT)wParam, pt);
    }

    LRESULT OnTaskDragMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        UpdateTaskDrag(pt);
        return 0;
    }

    LRESULT OnTaskDragEnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        EndTaskDrag();
        return 0;
    }

    LRESULT OnTaskButtonMButton(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (IsModernTaskbar())
            LaunchNewInstance(GroupOfIndex((INT)wParam));
        return 0;
    }

    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        switch (wParam)
        {
#if DUMP_TASKS != 0
            case 1:
                DumpTasks();
                break;
#endif
            case TIMER_ID_HOVER_PREVIEW:
            {
                KillTimer(wParam);
                BOOL bPending = m_HoverPreviewPending;
                m_HoverPreviewPending = FALSE;
                if (bPending && m_HoverIndex >= 0 && m_TaskBar.GetHotItem() == m_HoverIndex)
                    ShowHoverPreview(m_HoverIndex);
                break;
            }

            case TIMER_ID_BUTTON_ANIM:
            {
                RECT rcAnim = m_rcAnimBounds;

                if (!HasButtonAnimation())
                {
                    KillTimer(TIMER_ID_BUTTON_ANIM);
                    m_bAnimTimer = FALSE;
                    SetRectEmpty(&m_rcAnimBounds);
                }
                m_TaskBar.InvalidateRect(IsRectEmpty(&rcAnim) ? NULL : &rcAnim, TRUE);
                break;
            }

            case TIMER_ID_VALIDATE_RUDE_APP:
            {
                // Real activation of rude app might take some time after HSHELL_...ACTIVATED.
                // Wait up to 5 seconds with validating the rude app at each second.
                HWND hwndRude = FindRudeApp(NULL);
                HandleFullScreenApp(hwndRude);

                KillTimer(wParam);
                ++m_nRudeAppValidationCounter;
                if (m_nRudeAppValidationCounter < VALIDATE_RUDE_MAX_COUNT && !hwndRude)
                    SetTimer(wParam, VALIDATE_RUDE_INTERVAL, NULL);
                break;
            }
            default:
            {
                WARN("Unknown timer ID: %p\n", wParam);
                break;
            }
        }
        return TRUE;
    }

    LRESULT OnSetFont(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        return m_TaskBar.SendMessageW(uMsg, wParam, lParam);
    }

    LRESULT OnSettingChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (wParam == SPI_SETNONCLIENTMETRICS)
        {
            /*  Don't update the font, this will be done when we get a WM_SETFONT from our parent */
            UpdateButtonsSize(FALSE);
        }

        return 0;
    }

    LRESULT OnTaskbarPinChanged(UINT uMsg, WPARAM wParam, LPARAM lParam,
                                BOOL& bHandled)
    {
        SyncTaskbarPins();
        return 0;
    }

    LRESULT OnCopyData(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        PCOPYDATASTRUCT cpData = (PCOPYDATASTRUCT)lParam;
        if (cpData->dwData == m_uHardErrorMsg)
        {
            /* A hard error balloon message */
            PBALLOON_HARD_ERROR_DATA pData = (PBALLOON_HARD_ERROR_DATA)cpData->lpData;
            ERR("Got balloon data 0x%x, 0x%x, '%S', '%S'\n", pData->Status, pData->dwType, (WCHAR*)((ULONG_PTR)pData + pData->TitleOffset), (WCHAR*)((ULONG_PTR)pData + pData->MessageOffset));
            if (pData->cbHeaderSize == sizeof(BALLOON_HARD_ERROR_DATA))
                m_HardErrorThread.StartThread(pData);
            return TRUE;
        }

        return FALSE;
    }

    HRESULT Initialize(IN HWND hWndParent, IN OUT ITrayWindow *tray)
    {
        m_Tray = tray;
        m_bMaterial = ShellGetTaskbarMaterial(&m_crMaterial, &m_cyTaskRow);
        m_IsGroupingEnabled = g_TaskbarSettings.bGroupButtons || IsModernTaskbar();
        Create(hWndParent, 0, szRunningApps, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP);
        if (!m_hWnd)
            return E_FAIL;
        return S_OK;
    }

    VOID SetDropIndex(IN INT Index)
    {
        BOOL bAnimate = IsModernTaskbar();

        if (Index == m_DropIndex)
            return;
        if (bAnimate)
            CaptureButtonPositions();
        m_DropIndex = Index;
        if (bAnimate)
            AnimateButtonPositions();
        m_TaskBar.InvalidateRect(NULL, TRUE);
    }

    static BOOL GetDropPinPath(IN IDataObject *pDataObj, OUT CStringW &Path)
    {
        FORMATETC Format = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM Medium;
        BOOL bResult = FALSE;

        Path.Empty();
        if (!pDataObj || FAILED(pDataObj->GetData(&Format, &Medium)))
            return FALSE;

        HDROP hDrop = (HDROP)Medium.hGlobal;
        if (Medium.tymed == TYMED_HGLOBAL && hDrop)
        {
            WCHAR szFile[MAX_PATH];
            UINT Length = DragQueryFileW(hDrop, 0, NULL, 0);

            if (DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0) == 1 &&
                Length && Length < _countof(szFile) &&
                DragQueryFileW(hDrop, 0, szFile, _countof(szFile)) == Length &&
                TaskbarPin_IsPinnable(szFile, NULL))
            {
                Path = szFile;
                bResult = TRUE;
            }
        }
        ReleaseStgMedium(&Medium);
        return bResult;
    }

    INT DropIndexFromPoint(IN POINTL ptScreen)
    {
        POINT pt = { ptScreen.x, ptScreen.y };
        RECT rc;
        INT Index = 0;

        ::ScreenToClient(m_TaskBar.m_hWnd, &pt);
        for (INT i = 0; i < (INT)m_ButtonCount; ++i)
        {
            BOOL bBefore;

            if (!m_TaskBar.GetItemRect(i, &rc))
                continue;
            if (rc.bottom <= pt.y)
                bBefore = TRUE;
            else if (rc.top > pt.y)
                bBefore = FALSE;
            else
                bBefore = ((rc.left + rc.right) / 2) < pt.x;
            if (bBefore)
                ++Index;
        }
        return Index;
    }

    // *** IDropTarget methods ***

    STDMETHODIMP
    DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override
    {
        if (!pdwEffect)
            return E_INVALIDARG;

        m_DropPath.Empty();
        if ((*pdwEffect & DROPEFFECT_LINK) && !TaskbarPin_IsDisabled() &&
            GetDropPinPath(pDataObj, m_DropPath))
        {
            *pdwEffect = DROPEFFECT_LINK;
            SetDropIndex(DropIndexFromPoint(pt));
        }
        else
        {
            *pdwEffect = DROPEFFECT_NONE;
            SetDropIndex(-1);
        }
        if (m_DropTargetHelper)
        {
            POINT ptScreen = { pt.x, pt.y };
            m_DropTargetHelper->DragEnter(m_hWnd, pDataObj, &ptScreen, *pdwEffect);
        }
        return S_OK;
    }

    STDMETHODIMP
    DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override
    {
        if (!pdwEffect)
            return E_INVALIDARG;

        if (m_DropPath.IsEmpty() || !(*pdwEffect & DROPEFFECT_LINK))
        {
            *pdwEffect = DROPEFFECT_NONE;
            SetDropIndex(-1);
        }
        else
        {
            *pdwEffect = DROPEFFECT_LINK;
            SetDropIndex(DropIndexFromPoint(pt));
        }
        if (m_DropTargetHelper)
        {
            POINT ptScreen = { pt.x, pt.y };
            m_DropTargetHelper->DragOver(&ptScreen, *pdwEffect);
        }
        return S_OK;
    }

    STDMETHODIMP
    DragLeave() override
    {
        m_DropPath.Empty();
        SetDropIndex(-1);
        if (m_DropTargetHelper)
            m_DropTargetHelper->DragLeave();
        return S_OK;
    }

    STDMETHODIMP
    Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override
    {
        CStringW Path, Target;
        INT Index;
        DWORD AllowedEffects;

        if (!pdwEffect)
            return E_INVALIDARG;

        AllowedEffects = *pdwEffect;
        *pdwEffect = DROPEFFECT_NONE;
        Index = m_DropIndex >= 0 ? m_DropIndex : DropIndexFromPoint(pt);
        m_DropPath.Empty();
        SetDropIndex(-1);
        if (m_DropTargetHelper)
        {
            POINT ptScreen = { pt.x, pt.y };
            m_DropTargetHelper->Drop(pDataObj, &ptScreen, AllowedEffects);
        }
        if (!(AllowedEffects & DROPEFFECT_LINK) || TaskbarPin_IsDisabled() ||
            !GetDropPinPath(pDataObj, Path))
            return S_OK;

        if (FAILED(TaskbarPin_Create(Path)))
            return S_OK;

        SyncTaskbarPins();
        if (TaskbarPin_ResolveTarget(Path, Target))
        {
            PTASK_GROUP TaskGroup = FindTaskGroupByPath(Target);

            if (TaskGroup && TaskGroup->Index >= 0 && Index >= 0 && Index < (INT)m_ButtonCount)
                MoveTaskButtonAnimated(TaskGroup->Index, Index);
            SaveTaskbarPinOrder();
        }
        *pdwEffect = DROPEFFECT_LINK;
        return S_OK;
    }

    // *** IOleWindow methods ***

    STDMETHODIMP
    GetWindow(HWND* phwnd) override
    {
        if (!phwnd)
            return E_INVALIDARG;
        *phwnd = m_hWnd;
        return S_OK;
    }

    STDMETHODIMP
    ContextSensitiveHelp(BOOL fEnterMode) override
    {
        return E_NOTIMPL;
    }

    DECLARE_WND_CLASS_EX(szTaskSwitchWndClass, CS_DBLCLKS, COLOR_3DFACE)

    BEGIN_MSG_MAP(CTaskSwitchWnd)
        MESSAGE_HANDLER(WM_THEMECHANGED, OnThemeChanged)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
        MESSAGE_HANDLER(TSWM_UPDATETASKBARPOS, OnUpdateTaskbarPos)
        MESSAGE_HANDLER(TWM_SETTINGSCHANGED, OnTaskbarSettingsChanged)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
        MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChanged)
        MESSAGE_HANDLER(m_TaskbarPinChangedMsg, OnTaskbarPinChanged)
        MESSAGE_HANDLER(m_ShellHookMsg, OnShellHook)
        MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
        MESSAGE_HANDLER(WM_KLUDGEMINRECT, OnKludgeItemRect)
        MESSAGE_HANDLER(TSWM_TASKBUTTONMBUTTON, OnTaskButtonMButton)
        MESSAGE_HANDLER(TSWM_TASKDRAGBEGIN, OnTaskDragBegin)
        MESSAGE_HANDLER(TSWM_TASKDRAGMOVE, OnTaskDragMove)
        MESSAGE_HANDLER(TSWM_TASKDRAGEND, OnTaskDragEnd)
        MESSAGE_HANDLER(WM_COPYDATA, OnCopyData)
        MESSAGE_HANDLER(WM_WINDOWPOSCHANGED, OnWindowPosChanged)
    END_MSG_MAP()

    DECLARE_NOT_AGGREGATABLE(CTaskSwitchWnd)

    DECLARE_PROTECT_FINAL_CONSTRUCT()
    BEGIN_COM_MAP(CTaskSwitchWnd)
        COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
        COM_INTERFACE_ENTRY_IID(IID_IDropTarget, IDropTarget)
    END_COM_MAP()
};

HRESULT CTaskSwitchWnd_CreateInstance(IN HWND hWndParent, IN OUT ITrayWindow *Tray, REFIID riid, void **ppv)
{
    return ShellObjectCreatorInit<CTaskSwitchWnd>(hWndParent, Tray, riid, ppv);
}
