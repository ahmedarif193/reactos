/*
 * Stub implementation for the legacy Internet Browser context menu
 *
 * ReactOS project
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "shresdef.h"

#ifdef _MSC_VER
class ATL_NO_VTABLE CInternetContextMenu :
#else
class CInternetContextMenu :
#endif
    public CComCoClass<CInternetContextMenu, &CLSID_Internet>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IContextMenu,
    public IShellExtInit
{
public:
    CInternetContextMenu();
    ~CInternetContextMenu();

    DECLARE_REGISTRY_RESOURCEID(IDR_INTERNETCONTEXTMENU)
    DECLARE_NOT_AGGREGATABLE(CInternetContextMenu)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CInternetContextMenu)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
        COM_INTERFACE_ENTRY_IID(IID_IShellExtInit, IShellExtInit)
    END_COM_MAP()

    // IContextMenu
    STDMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override;
    STDMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO lpici) override;
    STDMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uFlags, UINT *lpReserved, LPSTR pszName, UINT cchMax) override;

    // IShellExtInit
    STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pdtobj, HKEY hkeyProgID) override;
};
