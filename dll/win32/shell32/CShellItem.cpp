/*
 * IShellItem implementation
 *
 * Copyright 2008 Vincent Povirk for CodeWeavers
 * Copyright 2009 Andrew Hill
 * Copyright 2013 Katayama Hirofumi MZ
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

EXTERN_C HRESULT WINAPI SHCreateShellItem(PCIDLIST_ABSOLUTE pidlParent,
    IShellFolder *psfParent, PCUITEMID_CHILD pidl, IShellItem **ppsi);

CShellItem::CShellItem() :
    m_pidl(NULL)
{
}

CShellItem::~CShellItem()
{
    ILFree(m_pidl);
}

HRESULT CShellItem::get_parent_pidl(LPITEMIDLIST *parent_pidl)
{
    *parent_pidl = ILClone(m_pidl);
    if (*parent_pidl)
    {
        if (ILRemoveLastID(*parent_pidl))
            return S_OK;
        else
        {
            ILFree(*parent_pidl);
            *parent_pidl = NULL;
            return E_INVALIDARG;
        }
    }
    else
    {
        *parent_pidl = NULL;
        return E_OUTOFMEMORY;
    }
}

HRESULT CShellItem::get_parent_shellfolder(IShellFolder **ppsf)
{
    HRESULT hr;
    LPITEMIDLIST parent_pidl;
    CComPtr<IShellFolder>        desktop;

    hr = get_parent_pidl(&parent_pidl);
    if (SUCCEEDED(hr))
    {
        hr = SHGetDesktopFolder(&desktop);
        if (SUCCEEDED(hr))
            hr = desktop->BindToObject(parent_pidl, NULL, IID_PPV_ARG(IShellFolder, ppsf));
        ILFree(parent_pidl);
    }

    return hr;
}

HRESULT CShellItem::get_shellfolder(IBindCtx *pbc, REFIID riid, void **ppvOut)
{
    CComPtr<IShellFolder> psf;
    CComPtr<IShellFolder> psfDesktop;
    HRESULT ret;

    ret = SHGetDesktopFolder(&psfDesktop);
    if (FAILED_UNEXPECTEDLY(ret))
        return ret;

    if (_ILIsDesktop(m_pidl))
        psf = psfDesktop;
    else
    {
        ret = psfDesktop->BindToObject(m_pidl, pbc, IID_PPV_ARG(IShellFolder, &psf));
        if (FAILED_UNEXPECTEDLY(ret))
            return ret;
    }

    return psf->QueryInterface(riid, ppvOut);
}

HRESULT WINAPI CShellItem::BindToHandler(IBindCtx *pbc, REFGUID rbhid, REFIID riid, void **ppvOut)
{
    HRESULT ret;
    TRACE("(%p, %p,%s,%p,%p)\n", this, pbc, shdebugstr_guid(&rbhid), riid, ppvOut);

    *ppvOut = NULL;
    if (IsEqualGUID(rbhid, BHID_SFObject))
    {
        return get_shellfolder(pbc, riid, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_SFUIObject))
    {
        CComPtr<IShellFolder> psf_parent;
        if (_ILIsDesktop(m_pidl))
            ret = SHGetDesktopFolder(&psf_parent);
        else
            ret = get_parent_shellfolder(&psf_parent);
        if (FAILED_UNEXPECTEDLY(ret))
            return ret;

        LPCITEMIDLIST pidl = ILFindLastID(m_pidl);
        return psf_parent->GetUIObjectOf(NULL, 1, &pidl, riid, NULL, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_DataObject))
    {
        return BindToHandler(pbc, BHID_SFUIObject, IID_IDataObject, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_SFViewObject))
    {
        CComPtr<IShellFolder> psf;
        ret = get_shellfolder(NULL, IID_PPV_ARG(IShellFolder, &psf));
        if (FAILED_UNEXPECTEDLY(ret))
            return ret;

        return psf->CreateViewObject(NULL, riid, ppvOut);
    }

    FIXME("Unsupported BHID %s.\n", debugstr_guid(&rbhid));

    return MK_E_NOOBJECT;
}

HRESULT WINAPI CShellItem::GetParent(IShellItem **ppsi)
{
    HRESULT hr;
    LPITEMIDLIST parent_pidl;

    TRACE("(%p,%p)\n", this, ppsi);

    hr = get_parent_pidl(&parent_pidl);
    if (SUCCEEDED(hr))
    {
        hr = SHCreateShellItem(NULL, NULL, parent_pidl, ppsi);
        ILFree(parent_pidl);
    }

    return hr;
}

HRESULT WINAPI CShellItem::GetDisplayName(SIGDN sigdnName, LPWSTR *ppszName)
{
    return SHGetNameFromIDList(m_pidl, sigdnName, ppszName);
}

HRESULT WINAPI CShellItem::GetAttributes(SFGAOF sfgaoMask, SFGAOF *psfgaoAttribs)
{
    CComPtr<IShellFolder>        parent_folder;
    LPCITEMIDLIST child_pidl;
    HRESULT hr;

    TRACE("(%p,%x,%p)\n", this, sfgaoMask, psfgaoAttribs);

    if (_ILIsDesktop(m_pidl))
        hr = SHGetDesktopFolder(&parent_folder);
    else
        hr = get_parent_shellfolder(&parent_folder);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    child_pidl = ILFindLastID(m_pidl);
    *psfgaoAttribs = sfgaoMask;
    hr = parent_folder->GetAttributesOf(1, &child_pidl, psfgaoAttribs);
    *psfgaoAttribs &= sfgaoMask;

    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return (sfgaoMask == *psfgaoAttribs) ? S_OK : S_FALSE;
}

HRESULT WINAPI CShellItem::Compare(IShellItem *oth, SICHINTF hint, int *piOrder)
{
    HRESULT hr;
    CComPtr<IPersistIDList>      pIDList;
    CComPtr<IShellFolder>        psfDesktop;
    LPITEMIDLIST pidl;

    TRACE("(%p,%p,%x,%p)\n", this, oth, hint, piOrder);

    if (piOrder == NULL || oth == NULL)
        return E_POINTER;

    hr = oth->QueryInterface(IID_PPV_ARG(IPersistIDList, &pIDList));
    if (SUCCEEDED(hr))
    {
        hr = pIDList->GetIDList(&pidl);
        if (SUCCEEDED(hr))
        {
            hr = SHGetDesktopFolder(&psfDesktop);
            if (SUCCEEDED(hr))
            {
                hr = psfDesktop->CompareIDs(hint, m_pidl, pidl);
                *piOrder = (int)(short)SCODE_CODE(hr);
            }
            ILFree(pidl);
        }
    }

    if(FAILED(hr))
        return hr;

    if(*piOrder)
        return S_FALSE;
    else
        return S_OK;
}

HRESULT WINAPI CShellItem::GetClassID(CLSID *pClassID)
{
    TRACE("(%p,%p)\n", this, pClassID);

    *pClassID = CLSID_ShellItem;
    return S_OK;
}

HRESULT WINAPI CShellItem::SetIDList(PCIDLIST_ABSOLUTE pidlx)
{
    LPITEMIDLIST new_pidl;

    TRACE("(%p,%p)\n", this, pidlx);

    new_pidl = ILClone(pidlx);
    if (new_pidl)
    {
        ILFree(m_pidl);
        m_pidl = new_pidl;
        return S_OK;
    }
    else
        return E_OUTOFMEMORY;
}

HRESULT WINAPI CShellItem::GetIDList(PIDLIST_ABSOLUTE *ppidl)
{
    TRACE("(%p,%p)\n", this, ppidl);

    *ppidl = ILClone(m_pidl);
    if (*ppidl)
        return S_OK;
    else
        return E_OUTOFMEMORY;
}

HRESULT WINAPI SHCreateShellItem(PCIDLIST_ABSOLUTE pidlParent,
    IShellFolder *psfParent, PCUITEMID_CHILD pidl, IShellItem **ppsi)
{
    HRESULT hr;
    CComPtr<IShellItem> newShellItem;
    LPITEMIDLIST new_pidl;
    CComPtr<IPersistIDList>            newPersistIDList;

    TRACE("(%p,%p,%p,%p)\n", pidlParent, psfParent, pidl, ppsi);

    *ppsi = NULL;

    if (!pidl)
        return E_INVALIDARG;

    if (pidlParent || psfParent)
    {
        LPITEMIDLIST temp_parent = NULL;
        if (!pidlParent)
        {
            CComPtr<IPersistFolder2>    ppf2Parent;

            if (FAILED(psfParent->QueryInterface(IID_PPV_ARG(IPersistFolder2, &ppf2Parent))))
            {
                FIXME("couldn't get IPersistFolder2 interface of parent\n");
                return E_NOINTERFACE;
            }

            if (FAILED(ppf2Parent->GetCurFolder(&temp_parent)))
            {
                FIXME("couldn't get parent PIDL\n");
                return E_NOINTERFACE;
            }

            pidlParent = temp_parent;
        }

        new_pidl = ILCombine(pidlParent, pidl);
        ILFree(temp_parent);

        if (!new_pidl)
            return E_OUTOFMEMORY;
    }
    else
    {
        new_pidl = ILClone(pidl);
        if (!new_pidl)
            return E_OUTOFMEMORY;
    }

    hr = CShellItem::_CreatorClass::CreateInstance(NULL, IID_PPV_ARG(IShellItem, &newShellItem));
    if (FAILED(hr))
    {
        ILFree(new_pidl);
        return hr;
    }
    hr = newShellItem->QueryInterface(IID_PPV_ARG(IPersistIDList, &newPersistIDList));
    if (FAILED(hr))
    {
        ILFree(new_pidl);
        return hr;
    }
    hr = newPersistIDList->SetIDList(new_pidl);
    if (FAILED(hr))
    {
        ILFree(new_pidl);
        return hr;
    }
    ILFree(new_pidl);

    *ppsi = newShellItem.Detach();

    return hr;
}

HRESULT WINAPI SHCreateItemFromIDList(PCIDLIST_ABSOLUTE pidl, REFIID riid, void **ppv)
{
    CComPtr<IShellItem> item;
    HRESULT hr;

    if (!pidl)
        return E_INVALIDARG;

    *ppv = NULL;
    hr = SHCreateShellItem(NULL, NULL, pidl, &item);
    if (SUCCEEDED(hr))
        hr = item->QueryInterface(riid, ppv);
    return hr;
}

HRESULT WINAPI SHCreateItemFromParsingName(PCWSTR pszPath, IBindCtx *pbc,
    REFIID riid, void **ppv)
{
    LPITEMIDLIST pidl;
    HRESULT hr;

    *ppv = NULL;
    hr = SHParseDisplayName(pszPath, pbc, &pidl, 0, NULL);
    if (SUCCEEDED(hr))
    {
        hr = SHCreateItemFromIDList(pidl, riid, ppv);
        ILFree(pidl);
    }
    return hr;
}

HRESULT WINAPI SHCreateItemFromRelativeName(IShellItem *parent, PCWSTR name, IBindCtx *pbc,
    REFIID riid, void **ppv)
{
    CComPtr<IShellFolder> desktop, folder;
    LPITEMIDLIST pidlFolder = NULL, pidl = NULL;
    HRESULT hr;

    TRACE("(%p, %s, %p, %s, %p)\n", parent, debugstr_w(name), pbc,
          debugstr_guid(&riid), ppv);

    if (!ppv)
        return E_INVALIDARG;
    *ppv = NULL;
    if (!name)
        return E_INVALIDARG;

    hr = SHGetIDListFromObject(parent, &pidlFolder);
    if (hr != S_OK)
        return hr;

    hr = SHGetDesktopFolder(&desktop);
    if (hr != S_OK)
        goto cleanup;

    if (!_ILIsDesktop(pidlFolder))
    {
        hr = desktop->BindToObject(pidlFolder, NULL, IID_PPV_ARG(IShellFolder, &folder));
        if (hr != S_OK)
            goto cleanup;
    }

    hr = (folder ? folder : desktop)->ParseDisplayName(NULL, pbc, const_cast<LPWSTR>(name),
                                                       NULL, &pidl, NULL);
    if (hr == S_OK)
        hr = SHCreateItemFromIDList(pidl, riid, ppv);

cleanup:
    ILFree(pidlFolder);
    ILFree(pidl);
    return hr;
}

HRESULT WINAPI SHGetItemFromObject(IUnknown *punk, REFIID riid, void **ppv)
{
    LPITEMIDLIST pidl;
    HRESULT hr;

    hr = SHGetIDListFromObject(punk, &pidl);
    if (SUCCEEDED(hr))
    {
        hr = SHCreateItemFromIDList(pidl, riid, ppv);
        ILFree(pidl);
    }
    return hr;
}

class CShellItemArray :
    public CComCoClass<CShellItemArray, &CLSID_NULL>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellItemArray
{
    CIDA *m_pCIDA;
    STGMEDIUM m_Medium;

public:
    CShellItemArray() : m_pCIDA(NULL)
    {
        m_Medium.tymed = TYMED_NULL;
    }

    virtual ~CShellItemArray()
    {
        CDataObjectHIDA::DestroyCIDA(m_pCIDA, m_Medium);
    }

    HRESULT Initialize(IDataObject *pdo)
    {
        return CDataObjectHIDA::CreateCIDA(pdo, &m_pCIDA, m_Medium);
    }

    inline UINT GetCount() const { return m_pCIDA->cidl; }

    // IShellItemArray
    STDMETHODIMP BindToHandler(IBindCtx *pbc, REFGUID rbhid, REFIID riid, void **ppv) override
    {
        UNIMPLEMENTED;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetPropertyStore(GETPROPERTYSTOREFLAGS flags, REFIID riid, void **ppv) override
    {
        UNIMPLEMENTED;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetPropertyDescriptionList(REFPROPERTYKEY keyType, REFIID riid, void **ppv) override
    {
        UNIMPLEMENTED;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetAttributes(SIATTRIBFLAGS dwAttribFlags, SFGAOF sfgaoMask, SFGAOF *psfgaoAttribs) override
    {
        UNIMPLEMENTED;
        *psfgaoAttribs = 0;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetCount(DWORD*pCount) override
    {
        *pCount = m_pCIDA ? GetCount() : 0;
        return S_OK;
    }

    STDMETHODIMP GetItemAt(DWORD nIndex, IShellItem **ppItem) override
    {
        if (!ppItem)
            return E_INVALIDARG;
        *ppItem = NULL;
        if (!m_pCIDA)
            return E_UNEXPECTED;
        if (nIndex >= GetCount())
            return E_FAIL;
        return SHCreateShellItem(HIDA_GetPIDLFolder(m_pCIDA), NULL,
                                 HIDA_GetPIDLItem(m_pCIDA, nIndex), ppItem);
    }

    STDMETHODIMP EnumItems(IEnumShellItems **ppESI) override
    {
        UNIMPLEMENTED;
        *ppESI = NULL;
        return E_NOTIMPL;
    }

DECLARE_NO_REGISTRY()
DECLARE_NOT_AGGREGATABLE(CShellItemArray)

BEGIN_COM_MAP(CShellItemArray)
    COM_INTERFACE_ENTRY_IID(IID_IShellItemArray, IShellItemArray)
END_COM_MAP()
};

EXTERN_C HRESULT WINAPI
SHCreateShellItemArrayFromDataObject(_In_ IDataObject *pdo, _In_ REFIID riid, _Out_ void **ppv)
{
    return ShellObjectCreatorInit<CShellItemArray>(pdo, riid, ppv);
}

EXTERN_C HRESULT WINAPI
SHCreateShellItemArray(_In_opt_ PCIDLIST_ABSOLUTE pidlParent, _In_opt_ IShellFolder *psf,
                       _In_ UINT cidl, _In_reads_(cidl) PCUITEMID_CHILD_ARRAY ppidl,
                       _Out_ IShellItemArray **ppsiItemArray)
{
    CComPtr<IDataObject> dataObject;
    PIDLIST_ABSOLUTE allocatedParent = NULL;
    HRESULT hr;

    if (!ppsiItemArray || !ppidl || !cidl || (!pidlParent && !psf))
        return E_INVALIDARG;
    *ppsiItemArray = NULL;

    if (!pidlParent)
    {
        hr = SHGetIDListFromObject(psf, &allocatedParent);
        if (FAILED(hr))
            return hr;
        pidlParent = allocatedParent;
    }

    hr = SHCreateDataObject(pidlParent, cidl, ppidl, NULL, IID_PPV_ARG(IDataObject, &dataObject));
    if (SUCCEEDED(hr))
        hr = SHCreateShellItemArrayFromDataObject(dataObject, IID_PPV_ARG(IShellItemArray, ppsiItemArray));
    ILFree(allocatedParent);
    return hr;
}

EXTERN_C HRESULT WINAPI SHGetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD dwFlags,
                                             HANDLE hToken, PWSTR *ppszPath);

EXTERN_C HRESULT WINAPI SHCreateItemWithParent(PCIDLIST_ABSOLUTE pidlParent, IShellFolder *psfParent,
    PCUITEMID_CHILD pidl, REFIID riid, void **ppvItem)
{
    CComPtr<IShellItem> item;
    HRESULT hr;

    TRACE("(%p, %p, %p, %s, %p)\n", pidlParent, psfParent, pidl, debugstr_guid(&riid), ppvItem);

    if (!ppvItem)
        return E_INVALIDARG;

    *ppvItem = NULL;

    if (!pidl || (!pidlParent && !psfParent))
        return E_INVALIDARG;

    hr = SHCreateShellItem(pidlParent, psfParent, pidl, &item);
    if (SUCCEEDED(hr))
        hr = item->QueryInterface(riid, ppvItem);
    return hr;
}

EXTERN_C HRESULT WINAPI SHCreateItemInKnownFolder(REFKNOWNFOLDERID kfid, DWORD dwKFFlags,
    PCWSTR pszItem, REFIID riid, void **ppv)
{
    CComHeapPtr<WCHAR> folderPath;
    WCHAR szPath[MAX_PATH];
    HRESULT hr;

    TRACE("(%s, 0x%lx, %s, %s, %p)\n", debugstr_guid(&kfid), dwKFFlags,
          debugstr_w(pszItem), debugstr_guid(&riid), ppv);

    if (!ppv)
        return E_INVALIDARG;

    *ppv = NULL;

    hr = SHGetKnownFolderPath(kfid, dwKFFlags, NULL, &folderPath);
    if (FAILED(hr))
        return hr;

    if (!pszItem || !*pszItem)
        return SHCreateItemFromParsingName(folderPath, NULL, riid, ppv);

    if (FAILED(StringCchCopyW(szPath, _countof(szPath), folderPath)))
        return E_FAIL;
    if (!PathAppendW(szPath, pszItem))
        return E_FAIL;

    return SHCreateItemFromParsingName(szPath, NULL, riid, ppv);
}

EXTERN_C HRESULT WINAPI SHBindToFolderIDListParentEx(IShellFolder *psfRoot, PCUIDLIST_RELATIVE pidl,
    IBindCtx *ppbc, REFIID riid, void **ppv, PCUITEMID_CHILD *ppidlLast)
{
    CComPtr<IShellFolder> root;
    LPCITEMIDLIST pidlChild;
    LPITEMIDLIST pidlParent = NULL;
    HRESULT hr;

    TRACE("(%p, %p, %p, %s, %p, %p)\n", psfRoot, pidl, ppbc, debugstr_guid(&riid), ppv, ppidlLast);

    if (!ppv)
        return E_INVALIDARG;

    *ppv = NULL;
    if (ppidlLast)
        *ppidlLast = NULL;

    if (!pidl)
        return E_INVALIDARG;

    if (psfRoot)
    {
        root = psfRoot;
    }
    else
    {
        hr = SHGetDesktopFolder(&root);
        if (FAILED(hr))
            return hr;
    }

    pidlChild = ILFindLastID(pidl);
    if (pidlChild == pidl)
    {
        hr = root->QueryInterface(riid, ppv);
    }
    else
    {
        pidlParent = ILClone(pidl);
        if (!pidlParent)
            return E_OUTOFMEMORY;
        ILRemoveLastID(pidlParent);

        hr = root->BindToObject(pidlParent, ppbc, riid, ppv);
        ILFree(pidlParent);
    }

    if (SUCCEEDED(hr) && ppidlLast)
        *ppidlLast = (PCUITEMID_CHILD)pidlChild;

    return hr;
}

EXTERN_C HRESULT WINAPI SHBindToFolderIDListParent(IShellFolder *psfRoot, PCUIDLIST_RELATIVE pidl,
    REFIID riid, void **ppv, PCUITEMID_CHILD *ppidlLast)
{
    return SHBindToFolderIDListParentEx(psfRoot, pidl, NULL, riid, ppv, ppidlLast);
}

EXTERN_C HRESULT WINAPI SHGetFolderPathEx(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken,
    LPWSTR pszPath, UINT cchPath)
{
    CComHeapPtr<WCHAR> path;
    HRESULT hr;

    TRACE("(%s, 0x%lx, %p, %p, %u)\n", debugstr_guid(&rfid), dwFlags, hToken, pszPath, cchPath);

    if (!pszPath || !cchPath)
        return E_INVALIDARG;

    hr = SHGetKnownFolderPath(rfid, dwFlags, hToken, &path);
    if (FAILED(hr))
        return hr;

    if (wcslen(path) >= cchPath)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    return StringCchCopyW(pszPath, cchPath, path);
}
