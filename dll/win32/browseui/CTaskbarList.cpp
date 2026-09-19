/*
 * PROJECT:     browseui
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ITaskbarList implementation
 * COPYRIGHT:   Copyright 2018 Mark Jansen (mark.jansen@reactos.org)
 */

#include "precomp.h"


/***********************************************************************
 *   ITaskbarList2 implementation
 */

#define TWM_GETTASKSWITCH (WM_USER + 236)

CTaskbarList::CTaskbarList()
    : m_hTaskWnd(NULL)
{
    m_ShellHookMsg = RegisterWindowMessageW(L"SHELLHOOK");
}

CTaskbarList::~CTaskbarList()
{
}

HWND CTaskbarList::TaskWnd()
{
    HWND hTrayWnd;
    if (m_hTaskWnd && ::IsWindow(m_hTaskWnd))
        return m_hTaskWnd;

    hTrayWnd = FindWindowW(L"Shell_TrayWnd", NULL);
    if (hTrayWnd)
    {
        m_hTaskWnd = (HWND)SendMessageW(hTrayWnd, TWM_GETTASKSWITCH, 0L, 0L);
    }
    return m_hTaskWnd;
}

void CTaskbarList::SendTaskWndShellHook(WPARAM wParam, HWND hWnd)
{
    HWND hTaskWnd = TaskWnd();
    if (hTaskWnd && m_ShellHookMsg)
        ::SendMessageW(hTaskWnd, m_ShellHookMsg, wParam, (LPARAM)hWnd);
}


HRESULT WINAPI CTaskbarList::MarkFullscreenWindow(HWND hwnd, BOOL fFullscreen)
{
    UNIMPLEMENTED;
    return E_NOTIMPL;
}


/***********************************************************************
 *   ITaskbarList implementation
 */

HRESULT WINAPI CTaskbarList::HrInit()
{
    if (m_ShellHookMsg == NULL)
        return E_OUTOFMEMORY;

    if (!TaskWnd())
        return E_HANDLE;

    return S_OK;
}

HRESULT WINAPI CTaskbarList::AddTab(HWND hwnd)
{
    SendTaskWndShellHook(HSHELL_WINDOWCREATED, hwnd);
    return S_OK;
}

HRESULT WINAPI CTaskbarList::DeleteTab(HWND hwnd)
{
    SendTaskWndShellHook(HSHELL_WINDOWDESTROYED, hwnd);
    return S_OK;
}

HRESULT WINAPI CTaskbarList::ActivateTab(HWND hwnd)
{
    SendTaskWndShellHook(HSHELL_WINDOWACTIVATED, hwnd);
    return S_OK;
}

HRESULT WINAPI CTaskbarList::SetActiveAlt(HWND hwnd)
{
    UNIMPLEMENTED;
    return E_NOTIMPL;
}


/* Explorer does not yet implement thumbnail tabs or task button decorations.
 * Expose the complete interface so clients can still use the inherited tab API,
 * but do not report successful rendering of unsupported decorations. */

HRESULT WINAPI CTaskbarList::SetProgressValue(HWND hwnd, ULONGLONG completed, ULONGLONG total)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::SetProgressState(HWND hwnd, TBPFLAG flags)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::RegisterTab(HWND tab, HWND mdi)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::UnregisterTab(HWND tab)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::SetTabOrder(HWND tab, HWND before)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::SetTabActive(HWND tab, HWND mdi, DWORD reserved)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::ThumbBarAddButtons(HWND hwnd, UINT count, LPTHUMBBUTTON buttons)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::ThumbBarUpdateButtons(HWND hwnd, UINT count, LPTHUMBBUTTON buttons)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::ThumbBarSetImageList(HWND hwnd, HIMAGELIST images)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::SetOverlayIcon(HWND hwnd, HICON icon, LPCWSTR description)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::SetThumbnailTooltip(HWND hwnd, LPCWSTR tip)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::SetThumbnailClip(HWND hwnd, RECT *clip)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CTaskbarList::SetTabProperties(HWND tab, STPFLAG flags)
{
    return E_NOTIMPL;
}
