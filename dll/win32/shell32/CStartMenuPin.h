/*
 * PROJECT:     ReactOS shell32
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     Start-menu pin compatibility and taskbar pin context handler
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

class CStartMenuPin :
    public CComCoClass<CStartMenuPin, &CLSID_StartMenuPin>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IStartMenuPinnedList,
    public IContextMenu,
    public IShellExtInit,
    public IObjectWithSite
{
private:
    CStringW m_Path;
    CComPtr<IUnknown> m_Site;
    BOOL m_HasCommand;
    BOOL m_Unpin;

public:
    CStartMenuPin() : m_HasCommand(FALSE), m_Unpin(FALSE) {}

    // IStartMenuPinnedList
    STDMETHODIMP RemoveFromList(IShellItem *pItem) override;

    // IContextMenu
    STDMETHODIMP QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst,
                                  UINT idCmdLast, UINT uFlags) override;
    STDMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO pici) override;
    STDMETHODIMP GetCommandString(UINT_PTR idCommand, UINT uFlags,
                                  UINT *pReserved, LPSTR pszName,
                                  UINT cchMax) override;

    // IShellExtInit
    STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pDataObject,
                            HKEY hkeyProgID) override;

    // IObjectWithSite
    STDMETHODIMP SetSite(IUnknown *pSite) override;
    STDMETHODIMP GetSite(REFIID riid, void **ppvSite) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_STARTMENUPIN)
    DECLARE_NOT_AGGREGATABLE(CStartMenuPin)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CStartMenuPin)
        COM_INTERFACE_ENTRY_IID(IID_IStartMenuPinnedList, IStartMenuPinnedList)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
        COM_INTERFACE_ENTRY_IID(IID_IShellExtInit, IShellExtInit)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
    END_COM_MAP()
};
