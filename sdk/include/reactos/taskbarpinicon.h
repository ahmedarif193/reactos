/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed Arif
 */

#pragma once

inline HRESULT TaskbarPin_GetIconPath(PCWSTR pszShortcut, CStringW &Path)
{
    WCHAR szPath[MAX_PATH];
    HRESULT hr = StringCchPrintfW(szPath, _countof(szPath), L"%s.taskbar.ico", pszShortcut);
    if (SUCCEEDED(hr))
        Path = szPath;
    return hr;
}

inline BOOL TaskbarPin_GetOwnedIconPath(PCWSTR pszShortcut, CStringW &Path)
{
    Path.Empty();
    CStringW Expected;
    if (FAILED(TaskbarPin_GetIconPath(pszShortcut, Expected)))
        return FALSE;
    CComPtr<IShellLinkW> Link;
    HRESULT hr = Link.CoCreateInstance(CLSID_ShellLink, IID_IShellLinkW, NULL, CLSCTX_INPROC_SERVER);
    CComPtr<IPersistFile> Persist;
    if (SUCCEEDED(hr))
        hr = Link->QueryInterface(IID_PPV_ARG(IPersistFile, &Persist));
    if (SUCCEEDED(hr))
        hr = Persist->Load(pszShortcut, STGM_READ);
    WCHAR szIcon[MAX_PATH] = L"";
    INT Index = 0;
    if (SUCCEEDED(hr))
        hr = Link->GetIconLocation(szIcon, _countof(szIcon), &Index);
    if (FAILED(hr) || lstrcmpiW(szIcon, Expected) || Index != 0)
        return FALSE;
    Path = Expected;
    return TRUE;
}
