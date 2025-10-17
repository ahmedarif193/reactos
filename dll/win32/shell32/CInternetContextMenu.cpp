/*
 * Stub implementation for the legacy Internet Browser context menu
 *
 * ReactOS project
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "precomp.h"
#include "CInternetContextMenu.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

static const WCHAR INTERNET_COMMAND_KEY[] =
    L"CLSID\\{871C5380-42A0-1069-A2EA-08002B30309D}\\Shell\\open\\Command";

static HRESULT LaunchRegisteredCommand(HWND hwnd, int nShow)
{
    DWORD cbData = 0;
    LSTATUS status = RegGetValueW(HKEY_CLASSES_ROOT,
                                   INTERNET_COMMAND_KEY,
                                   NULL,
                                   RRF_RT_REG_SZ,
                                   NULL,
                                   NULL,
                                   &cbData);
    if (status != ERROR_SUCCESS)
    {
        ERR("CInternetContextMenu: RegGetValue size query failed (%ld)\n", status);
        return HRESULT_FROM_WIN32(status);
    }

    if (cbData < sizeof(WCHAR))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    CComHeapPtr<WCHAR> buffer;
    if (!buffer.Allocate(cbData / sizeof(WCHAR)))
        return E_OUTOFMEMORY;

    status = RegGetValueW(HKEY_CLASSES_ROOT,
                           INTERNET_COMMAND_KEY,
                           NULL,
                           RRF_RT_REG_SZ,
                           NULL,
                           buffer.m_pData,
                           &cbData);
    if (status != ERROR_SUCCESS)
    {
        ERR("CInternetContextMenu: RegGetValue data query failed (%ld)\n", status);
        return HRESULT_FROM_WIN32(status);
    }

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(buffer.m_pData, &argc);
    if (!argv || argc == 0)
    {
        if (argv)
            LocalFree(argv);
        ERR("CInternetContextMenu: CommandLineToArgvW returned no entries\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    CStringW parameters;
    for (int i = 1; i < argc; ++i)
    {
        if (!parameters.IsEmpty())
            parameters += L' ';
        parameters += argv[i];
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.hwnd = hwnd;
    sei.nShow = (nShow != 0) ? nShow : SW_SHOWNORMAL;
    sei.lpFile = argv[0];
    sei.lpParameters = parameters.IsEmpty() ? NULL : parameters.GetString();

    BOOL ok = ShellExecuteExW(&sei);
    HRESULT hr = ok ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(hr))
    {
        ERR("CInternetContextMenu: ShellExecuteExW failed hr=%lx (file=%S params=%S)\n",
            hr,
            argv[0],
            parameters.IsEmpty() ? L"" : parameters.GetString());
    }

    LocalFree(argv);
    return hr;
}

CInternetContextMenu::CInternetContextMenu()
{
}

CInternetContextMenu::~CInternetContextMenu()
{
}

STDMETHODIMP CInternetContextMenu::QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    UNREFERENCED_PARAMETER(hmenu);
    UNREFERENCED_PARAMETER(indexMenu);
    UNREFERENCED_PARAMETER(idCmdFirst);
    UNREFERENCED_PARAMETER(idCmdLast);
    UNREFERENCED_PARAMETER(uFlags);

    ERR("CInternetContextMenu: QueryContextMenu (flags=0x%X)\n", uFlags);
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
}

STDMETHODIMP CInternetContextMenu::InvokeCommand(LPCMINVOKECOMMANDINFO lpici)
{
    if (!lpici)
        return E_INVALIDARG;

    TRACE("CInternetContextMenu::InvokeCommand called (verb=%p, fMask=0x%lx)\n", lpici->lpVerb, lpici->fMask);

    if (HIWORD(lpici->lpVerb))
    {
        LPCSTR verb = reinterpret_cast<LPCSTR>(lpici->lpVerb);
        if (lstrcmpiA(verb, "open") != 0 && lstrcmpiA(verb, "default") != 0)
        {
            ERR("CInternetContextMenu: unexpected verb '%s'\n", verb);
            return E_FAIL;
        }
    }
    else if (LOWORD(lpici->lpVerb) != 0)
    {
        ERR("CInternetContextMenu: unexpected verb id %u\n", LOWORD(lpici->lpVerb));
        return E_FAIL;
    }

    return LaunchRegisteredCommand(lpici->hwnd, lpici->nShow);
}

STDMETHODIMP CInternetContextMenu::GetCommandString(UINT_PTR idCmd, UINT uFlags, UINT *lpReserved, LPSTR pszName, UINT cchMax)
{
    UNREFERENCED_PARAMETER(idCmd);
    UNREFERENCED_PARAMETER(uFlags);
    UNREFERENCED_PARAMETER(lpReserved);

    if (!pszName || cchMax == 0)
        return E_INVALIDARG;

    ERR("CInternetContextMenu: GetCommandString stub (idCmd=%llu)\n",
        static_cast<unsigned long long>(idCmd));
    *pszName = '\0';
    return E_NOTIMPL;
}

STDMETHODIMP CInternetContextMenu::Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pdtobj, HKEY hkeyProgID)
{
    UNREFERENCED_PARAMETER(pidlFolder);
    UNREFERENCED_PARAMETER(pdtobj);
    UNREFERENCED_PARAMETER(hkeyProgID);

    ERR("CInternetContextMenu: Initialize stub (pidl=%p, dataObj=%p)\n", pidlFolder, pdtobj);
    return S_OK;
}
