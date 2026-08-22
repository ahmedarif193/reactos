/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shell namespace tree control
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <commoncontrols.h>
#include <uxtheme.h>

WINE_DEFAULT_DEBUG_CHANNEL(shell);

#define NAMESPACE_TREE_CLASS L"ReactOS Namespace Tree Control"

class CNamespaceShellItemArray :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellItemArray
{
private:
    CAtlArray<CComPtr<IShellItem> > m_Items;

public:
    HRESULT Initialize(const CAtlArray<CComPtr<IShellItem> >& Items)
    {
        for (size_t i = 0; i < Items.GetCount(); ++i)
            m_Items.Add(Items[i]);
        return S_OK;
    }

    STDMETHODIMP BindToHandler(IBindCtx *pbc, REFGUID rbhid, REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetPropertyStore(GETPROPERTYSTOREFLAGS flags, REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetPropertyDescriptionList(REFPROPERTYKEY keyType, REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetAttributes(SIATTRIBFLAGS flags, SFGAOF mask, SFGAOF *attributes) override
    {
        SIATTRIBFLAGS operation;
        SFGAOF result;

        if (!attributes)
            return E_POINTER;

        operation = (SIATTRIBFLAGS)(flags & SIATTRIBFLAGS_MASK);
        if (operation != SIATTRIBFLAGS_AND && operation != SIATTRIBFLAGS_OR &&
            operation != SIATTRIBFLAGS_APPCOMPAT)
        {
            *attributes = 0;
            return E_INVALIDARG;
        }

        result = (operation == SIATTRIBFLAGS_OR) ? 0 : mask;
        for (size_t i = 0; i < m_Items.GetCount(); ++i)
        {
            SFGAOF itemAttributes = mask;
            HRESULT hr = m_Items[i]->GetAttributes(mask, &itemAttributes);
            if (FAILED(hr))
            {
                *attributes = 0;
                return hr;
            }

            if (operation == SIATTRIBFLAGS_OR)
                result |= itemAttributes;
            else
                result &= itemAttributes;
        }

        *attributes = result & mask;
        if (operation == SIATTRIBFLAGS_OR)
            return *attributes ? S_OK : S_FALSE;
        return *attributes == mask ? S_OK : S_FALSE;
    }

    STDMETHODIMP GetCount(DWORD *count) override
    {
        if (!count)
            return E_POINTER;
        *count = (DWORD)m_Items.GetCount();
        return S_OK;
    }

    STDMETHODIMP GetItemAt(DWORD index, IShellItem **item) override
    {
        if (!item)
            return E_POINTER;
        *item = NULL;
        if (index >= m_Items.GetCount())
            return E_BOUNDS;

        *item = m_Items[index];
        (*item)->AddRef();
        return S_OK;
    }

    STDMETHODIMP EnumItems(IEnumShellItems **enumerator) override
    {
        if (!enumerator)
            return E_POINTER;
        *enumerator = NULL;
        return E_NOTIMPL;
    }

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CNamespaceShellItemArray)

    BEGIN_COM_MAP(CNamespaceShellItemArray)
        COM_INTERFACE_ENTRY_IID(IID_IShellItemArray, IShellItemArray)
    END_COM_MAP()
};

static HRESULT CreateNamespaceShellItemArray(
    const CAtlArray<CComPtr<IShellItem> >& Items,
    IShellItemArray **Array)
{
    _CComObject<CNamespaceShellItemArray> *Object;
    HRESULT hr;

    if (!Array)
        return E_POINTER;
    *Array = NULL;

    hr = _CComObject<CNamespaceShellItemArray>::CreateInstance(&Object);
    if (FAILED(hr))
        return hr;

    Object->AddRef();
    hr = Object->Initialize(Items);
    if (SUCCEEDED(hr))
        hr = Object->QueryInterface(IID_PPV_ARG(IShellItemArray, Array));
    Object->Release();
    return hr;
}

class CNamespaceTreeControl :
    public CWindowImpl<CNamespaceTreeControl>,
    public CComCoClass<CNamespaceTreeControl, &CLSID_NamespaceTreeControl>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public INameSpaceTreeControl2,
    public IOleWindow
{
private:
    struct RootEntry
    {
        CComPtr<IShellItem> Item;
        CComPtr<IShellItemFilter> Filter;
        CComHeapPtr<ITEMIDLIST> Pidl;
        SHCONTF EnumFlags;
        NSTCROOTSTYLE Style;
        HTREEITEM TreeItem;
        BOOL Enumerated;

        RootEntry() : EnumFlags((SHCONTF)0), Style(NSTCRS_VISIBLE), TreeItem(NULL), Enumerated(FALSE)
        {
        }
    };

    struct TreeItemData
    {
        CComPtr<IShellItem> Item;
        CComHeapPtr<ITEMIDLIST> Pidl;
        RootEntry *Root;
        NSTCITEMSTATE State;
        INT CustomState;
        BOOL Enumerated;
        BOOL IsRoot;

        TreeItemData() : Root(NULL), State(NSTCIS_NONE), CustomState(0), Enumerated(FALSE), IsRoot(FALSE)
        {
        }
    };

    struct EventEntry
    {
        DWORD Cookie;
        CComPtr<INameSpaceTreeControlEvents> Events;

        EventEntry() : Cookie(0)
        {
        }
    };

    HWND m_Tree;
    CComPtr<IImageList> m_ImageList;
    CAtlList<RootEntry *> m_Roots;
    CAtlList<EventEntry> m_Events;
    DWORD m_NextCookie;
    NSTCSTYLE m_Style;
    NSTCSTYLE2 m_Style2;
    LONG m_SuppressSelection;
    BOOL m_Destroying;

    DWORD GetTreeStyle() const
    {
        DWORD Style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
                      TVS_DISABLEDRAGDROP;

        if (m_Style & NSTCS_HASEXPANDOS)
            Style |= TVS_HASBUTTONS;
        if (m_Style & NSTCS_HASLINES)
            Style |= TVS_HASLINES;
        if (m_Style & NSTCS_SINGLECLICKEXPAND)
            Style |= TVS_TRACKSELECT | TVS_SINGLEEXPAND;
        if (m_Style & NSTCS_FULLROWSELECT)
            Style |= TVS_FULLROWSELECT;
        if (m_Style & NSTCS_ROOTHASEXPANDO)
            Style |= TVS_LINESATROOT;
        if (m_Style & NSTCS_SHOWSELECTIONALWAYS)
            Style |= TVS_SHOWSELALWAYS;
        if (!(m_Style & NSTCS_NOINFOTIP))
            Style |= TVS_INFOTIP;
        if (!(m_Style & NSTCS_HORIZONTALSCROLL))
            Style |= TVS_NOHSCROLL;
        if (m_Style & NSTCS_CHECKBOXES)
            Style |= TVS_CHECKBOXES;
        if (m_Style & NSTCS_TABSTOP)
            Style |= WS_TABSTOP;
        if (m_Style & NSTCS_BORDER)
            Style |= WS_BORDER;

        return Style;
    }

    void ApplyTreeStyle()
    {
        if (!m_Tree)
            return;

        ::SetWindowLongPtrW(m_Tree, GWL_STYLE, GetTreeStyle());
        ::SetWindowPos(m_Tree, NULL, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                       SWP_FRAMECHANGED);
    }

    TreeItemData *GetTreeItemData(HTREEITEM TreeItem) const
    {
        TVITEMW Item = {0};

        if (!m_Tree || !TreeItem)
            return NULL;

        Item.mask = TVIF_PARAM;
        Item.hItem = TreeItem;
        if (!TreeView_GetItem(m_Tree, &Item))
            return NULL;
        return reinterpret_cast<TreeItemData *>(Item.lParam);
    }

    HTREEITEM GetNextDepthFirst(HTREEITEM TreeItem) const
    {
        HTREEITEM Next;

        if ((Next = TreeView_GetChild(m_Tree, TreeItem)) != NULL)
            return Next;

        while (TreeItem)
        {
            if ((Next = TreeView_GetNextSibling(m_Tree, TreeItem)) != NULL)
                return Next;
            TreeItem = TreeView_GetParent(m_Tree, TreeItem);
        }
        return NULL;
    }

    HTREEITEM FindTreeItem(PCIDLIST_ABSOLUTE Pidl) const
    {
        HTREEITEM TreeItem;

        for (TreeItem = TreeView_GetRoot(m_Tree); TreeItem;
             TreeItem = GetNextDepthFirst(TreeItem))
        {
            TreeItemData *Data = GetTreeItemData(TreeItem);
            if (Data && ILIsEqual(Data->Pidl, Pidl))
                return TreeItem;
        }
        return NULL;
    }

    HRESULT GetItemPidl(IShellItem *Item, CComHeapPtr<ITEMIDLIST>& Pidl) const
    {
        PIDLIST_ABSOLUTE RawPidl = NULL;
        HRESULT hr;

        if (!Item)
            return E_INVALIDARG;

        hr = SHGetIDListFromObject(Item, &RawPidl);
        if (SUCCEEDED(hr))
            Pidl.Attach(RawPidl);
        return hr;
    }

    RootEntry *FindRoot(IShellItem *Item, POSITION *FoundPosition = NULL) const
    {
        CComHeapPtr<ITEMIDLIST> Pidl;
        POSITION Position;

        if (FAILED(GetItemPidl(Item, Pidl)))
            return NULL;

        Position = m_Roots.GetHeadPosition();
        while (Position)
        {
            POSITION Current = Position;
            RootEntry *Root = m_Roots.GetNext(Position);
            if (ILIsEqual(Root->Pidl, Pidl))
            {
                if (FoundPosition)
                    *FoundPosition = Current;
                return Root;
            }
        }
        return NULL;
    }

    void SnapshotEvents(CAtlList<CComPtr<INameSpaceTreeControlEvents> >& Snapshot) const
    {
        POSITION Position = m_Events.GetHeadPosition();
        while (Position)
            Snapshot.AddTail(m_Events.GetNext(Position).Events);
    }

    void NotifyItemAdded(IShellItem *Item, BOOL IsRoot)
    {
        CAtlList<CComPtr<INameSpaceTreeControlEvents> > Snapshot;
        SnapshotEvents(Snapshot);
        POSITION Position = Snapshot.GetHeadPosition();
        while (Position)
            Snapshot.GetNext(Position)->OnItemAdded(Item, IsRoot);
    }

    void NotifyItemDeleted(IShellItem *Item, BOOL IsRoot)
    {
        CAtlList<CComPtr<INameSpaceTreeControlEvents> > Snapshot;
        SnapshotEvents(Snapshot);
        POSITION Position = Snapshot.GetHeadPosition();
        while (Position)
            Snapshot.GetNext(Position)->OnItemDeleted(Item, IsRoot);
    }

    void NotifyBeforeExpand(IShellItem *Item)
    {
        CAtlList<CComPtr<INameSpaceTreeControlEvents> > Snapshot;
        SnapshotEvents(Snapshot);
        POSITION Position = Snapshot.GetHeadPosition();
        while (Position)
            Snapshot.GetNext(Position)->OnBeforeExpand(Item);
    }

    void NotifyAfterExpand(IShellItem *Item)
    {
        CAtlList<CComPtr<INameSpaceTreeControlEvents> > Snapshot;
        SnapshotEvents(Snapshot);
        POSITION Position = Snapshot.GetHeadPosition();
        while (Position)
            Snapshot.GetNext(Position)->OnAfterExpand(Item);
    }

    void NotifyStateChanging(IShellItem *Item, NSTCITEMSTATE Mask, NSTCITEMSTATE State)
    {
        CAtlList<CComPtr<INameSpaceTreeControlEvents> > Snapshot;
        SnapshotEvents(Snapshot);
        POSITION Position = Snapshot.GetHeadPosition();
        while (Position)
            Snapshot.GetNext(Position)->OnItemStateChanging(Item, Mask, State);
    }

    void NotifyStateChanged(IShellItem *Item, NSTCITEMSTATE Mask, NSTCITEMSTATE State)
    {
        CAtlList<CComPtr<INameSpaceTreeControlEvents> > Snapshot;
        SnapshotEvents(Snapshot);
        POSITION Position = Snapshot.GetHeadPosition();
        while (Position)
            Snapshot.GetNext(Position)->OnItemStateChanged(Item, Mask, State);
    }

    void NotifySelectionChanged(IShellItem *Item)
    {
        CAtlArray<CComPtr<IShellItem> > Items;
        CComPtr<IShellItemArray> Array;
        CAtlList<CComPtr<INameSpaceTreeControlEvents> > Snapshot;

        Items.Add(Item);
        if (FAILED(CreateNamespaceShellItemArray(Items, &Array)))
            return;

        SnapshotEvents(Snapshot);
        POSITION Position = Snapshot.GetHeadPosition();
        while (Position)
            Snapshot.GetNext(Position)->OnSelectionChanged(Array);
    }

    void NotifyItemClick(IShellItem *Item, NSTCEHITTEST HitTest, NSTCECLICKTYPE ClickType)
    {
        CAtlList<CComPtr<INameSpaceTreeControlEvents> > Snapshot;
        SnapshotEvents(Snapshot);
        POSITION Position = Snapshot.GetHeadPosition();
        while (Position)
            Snapshot.GetNext(Position)->OnItemClick(Item, HitTest, ClickType);
    }

    void NotifyKeyboardInput(UINT Message, WPARAM wParam, LPARAM lParam)
    {
        CAtlList<CComPtr<INameSpaceTreeControlEvents> > Snapshot;
        SnapshotEvents(Snapshot);
        POSITION Position = Snapshot.GetHeadPosition();
        while (Position)
            Snapshot.GetNext(Position)->OnKeyboardInput(Message, wParam, lParam);
    }

    HRESULT InsertTreeItem(
        RootEntry *Root,
        HTREEITEM Parent,
        HTREEITEM InsertAfter,
        IShellItem *Item,
        PCIDLIST_ABSOLUTE Pidl,
        BOOL IsRoot,
        HTREEITEM *Inserted)
    {
        CComHeapPtr<WCHAR> DisplayName;
        TreeItemData *Data;
        TVINSERTSTRUCTW Insert = {0};
        SHFILEINFOW FileInfo = {0};
        SFGAOF Attributes = SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_STREAM;
        HRESULT hr;

        if (Inserted)
            *Inserted = NULL;

        if (!IsRoot && Root->Filter)
        {
            hr = Root->Filter->IncludeItem(Item);
            if (hr != S_OK)
                return hr == S_FALSE ? S_FALSE : hr;
        }

        hr = Item->GetDisplayName(SIGDN_NORMALDISPLAY, &DisplayName);
        if (FAILED(hr))
            return hr;

        Data = new TreeItemData;
        if (!Data)
            return E_OUTOFMEMORY;

        Data->Pidl.Attach(ILClone(Pidl));
        if (!Data->Pidl)
        {
            delete Data;
            return E_OUTOFMEMORY;
        }

        Data->Item = Item;
        Data->Root = Root;
        Data->IsRoot = IsRoot;
        Item->GetAttributes(Attributes, &Attributes);

        SHGetFileInfoW(reinterpret_cast<LPCWSTR>(Pidl), 0, &FileInfo, sizeof(FileInfo),
                       SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);

        Insert.hParent = Parent;
        Insert.hInsertAfter = InsertAfter;
        Insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE |
                           TVIF_CHILDREN;
        Insert.item.pszText = DisplayName;
        Insert.item.iImage = FileInfo.iIcon;
        Insert.item.iSelectedImage = FileInfo.iIcon;
        Insert.item.lParam = reinterpret_cast<LPARAM>(Data);
        Insert.item.cChildren = ((Attributes & SFGAO_HASSUBFOLDER) &&
                                 !(Attributes & SFGAO_STREAM)) ? 1 : 0;

        HTREEITEM TreeItem = TreeView_InsertItem(m_Tree, &Insert);
        if (!TreeItem)
        {
            delete Data;
            return HRESULT_FROM_WIN32(GetLastError());
        }

        if (Inserted)
            *Inserted = TreeItem;
        NotifyItemAdded(Item, IsRoot);
        return S_OK;
    }

    HRESULT BindToFolder(PCIDLIST_ABSOLUTE Pidl, IShellFolder **Folder) const
    {
        if (!Folder)
            return E_POINTER;
        *Folder = NULL;

        if (ILIsEmpty(Pidl))
            return SHGetDesktopFolder(Folder);
        return SHBindToObject(NULL, Pidl, NULL, IID_PPV_ARG(IShellFolder, Folder));
    }

    HRESULT EnumerateChildren(RootEntry *Root, HTREEITEM Parent, PCIDLIST_ABSOLUTE ParentPidl)
    {
        CComPtr<IShellFolder> Folder;
        CComPtr<IEnumIDList> Enumerator;
        CComHeapPtr<ITEMIDLIST> RelativePidl;
        SHCONTF EnumFlags = Root->EnumFlags;
        ULONG Fetched;
        HRESULT hr;
        UINT Count = 0;

        hr = BindToFolder(ParentPidl, &Folder);
        if (FAILED(hr))
            return hr;

        if (Root->Filter)
        {
            CComPtr<IShellItem> ParentItem;
            if (SUCCEEDED(SHCreateItemFromIDList(ParentPidl, IID_PPV_ARG(IShellItem, &ParentItem))))
                Root->Filter->GetEnumFlagsForItem(ParentItem, &EnumFlags);
        }

        hr = Folder->EnumObjects(m_hWnd, EnumFlags, &Enumerator);
        if (hr == S_FALSE)
            return S_OK;
        if (FAILED(hr))
            return hr;

        SendMessageW(m_Tree, WM_SETREDRAW, FALSE, 0);
        while ((hr = Enumerator->Next(1, &RelativePidl, &Fetched)) == S_OK)
        {
            CComHeapPtr<ITEMIDLIST> AbsolutePidl(ILCombine(ParentPidl, RelativePidl));
            CComPtr<IShellItem> Item;

            if (!AbsolutePidl)
            {
                hr = E_OUTOFMEMORY;
                break;
            }

            hr = SHCreateItemFromIDList(AbsolutePidl, IID_PPV_ARG(IShellItem, &Item));
            if (SUCCEEDED(hr))
            {
                hr = InsertTreeItem(Root, Parent, TVI_LAST, Item, AbsolutePidl, FALSE, NULL);
                if (hr == S_OK)
                    ++Count;
                else if (hr == S_FALSE)
                    hr = S_OK;
            }

            RelativePidl.Free();
            if (FAILED(hr))
                break;
        }
        SendMessageW(m_Tree, WM_SETREDRAW, TRUE, 0);

        if (Parent && Parent != TVI_ROOT)
        {
            TVITEMW Item = {0};
            Item.mask = TVIF_CHILDREN;
            Item.hItem = Parent;
            Item.cChildren = Count != 0;
            TreeView_SetItem(m_Tree, &Item);
            if (Count)
                TreeView_SortChildren(m_Tree, Parent, FALSE);
        }
        return FAILED(hr) ? hr : S_OK;
    }

    HRESULT PopulateTreeItem(HTREEITEM TreeItem)
    {
        TreeItemData *Data = GetTreeItemData(TreeItem);
        HRESULT hr = S_OK;

        if (!Data)
            return E_INVALIDARG;

        if (!Data->Enumerated)
        {
            NotifyBeforeExpand(Data->Item);
            hr = EnumerateChildren(Data->Root, TreeItem, Data->Pidl);
            Data->Enumerated = TRUE;
            NotifyAfterExpand(Data->Item);
        }

        return hr;
    }

    HRESULT EnsureTreeItemExpanded(HTREEITEM TreeItem)
    {
        HRESULT hr = PopulateTreeItem(TreeItem);

        if (SUCCEEDED(hr))
            TreeView_Expand(m_Tree, TreeItem, TVE_EXPAND);
        return hr;
    }

    HTREEITEM FindVisibleRootInsertAfter(RootEntry *Target) const
    {
        HTREEITEM Previous = TVI_FIRST;
        POSITION Position = m_Roots.GetHeadPosition();

        while (Position)
        {
            RootEntry *Root = m_Roots.GetNext(Position);
            if (Root == Target)
                break;
            if (Root->TreeItem)
                Previous = Root->TreeItem;
        }
        return Previous;
    }

    BOOL IsPidlAncestor(PCIDLIST_ABSOLUTE Parent, PCIDLIST_ABSOLUTE Child) const
    {
        if (ILIsEqual(Parent, Child))
            return TRUE;
        return ILFindChild(const_cast<PIDLIST_ABSOLUTE>(Parent), Child) != NULL;
    }

    HTREEITEM FindChildOnPath(HTREEITEM Parent, RootEntry *Root, PCIDLIST_ABSOLUTE Target) const
    {
        HTREEITEM Item = Parent ? TreeView_GetChild(m_Tree, Parent) : TreeView_GetRoot(m_Tree);

        while (Item)
        {
            TreeItemData *Data = GetTreeItemData(Item);
            if (Data && Data->Root == Root && IsPidlAncestor(Data->Pidl, Target))
                return Item;
            Item = TreeView_GetNextSibling(m_Tree, Item);
        }
        return NULL;
    }

    HTREEITEM FindAndExpandToPidl(PCIDLIST_ABSOLUTE Target)
    {
        POSITION Position = m_Roots.GetHeadPosition();

        while (Position)
        {
            RootEntry *Root = m_Roots.GetNext(Position);
            HTREEITEM Current;

            if (!IsPidlAncestor(Root->Pidl, Target))
                continue;

            if (Root->TreeItem)
            {
                Current = Root->TreeItem;
                if (ILIsEqual(Root->Pidl, Target))
                    return Current;
            }
            else
            {
                if (!Root->Enumerated)
                {
                    if (FAILED(EnumerateChildren(Root, TVI_ROOT, Root->Pidl)))
                        continue;
                    Root->Enumerated = TRUE;
                }
                Current = FindChildOnPath(NULL, Root, Target);
                if (!Current)
                    continue;
            }

            while (Current)
            {
                TreeItemData *Data = GetTreeItemData(Current);
                if (!Data)
                    break;
                if (ILIsEqual(Data->Pidl, Target))
                    return Current;
                if (FAILED(EnsureTreeItemExpanded(Current)))
                    break;
                Current = FindChildOnPath(Current, Root, Target);
            }
        }
        return NULL;
    }

    void DeleteRootTreeItems(RootEntry *Root)
    {
        if (Root->TreeItem)
        {
            TreeView_DeleteItem(m_Tree, Root->TreeItem);
            Root->TreeItem = NULL;
            return;
        }

        HTREEITEM Item = TreeView_GetRoot(m_Tree);
        while (Item)
        {
            HTREEITEM Next = TreeView_GetNextSibling(m_Tree, Item);
            TreeItemData *Data = GetTreeItemData(Item);
            if (Data && Data->Root == Root)
                TreeView_DeleteItem(m_Tree, Item);
            Item = Next;
        }
    }

    void DeleteAllRoots()
    {
        if (m_Tree)
            TreeView_DeleteAllItems(m_Tree);

        while (!m_Roots.IsEmpty())
            delete m_Roots.RemoveHead();
    }

    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&)
    {
        INITCOMMONCONTROLSEX Controls = {sizeof(Controls), ICC_TREEVIEW_CLASSES};
        InitCommonControlsEx(&Controls);

        m_Tree = CreateWindowExW(0, WC_TREEVIEWW, NULL, GetTreeStyle(), 0, 0, 0, 0,
                                 m_hWnd, NULL, shell32_hInstance, NULL);
        if (!m_Tree)
            return -1;

        if (SUCCEEDED(SHGetImageList(SHIL_SMALL, IID_PPV_ARG(IImageList, &m_ImageList))))
            TreeView_SetImageList(m_Tree, reinterpret_cast<HIMAGELIST>(m_ImageList.p), TVSIL_NORMAL);

        HFONT Font = reinterpret_cast<HFONT>(SendMessageW(::GetParent(m_hWnd), WM_GETFONT, 0, 0));
        if (!Font)
            Font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(m_Tree, WM_SETFONT, reinterpret_cast<WPARAM>(Font), TRUE);
        return 0;
    }

    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&)
    {
        m_Destroying = TRUE;
        DeleteAllRoots();
        if (m_Tree)
        {
            ::DestroyWindow(m_Tree);
            m_Tree = NULL;
        }
        m_ImageList.Release();
        m_Destroying = FALSE;
        return 0;
    }

    LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        if (m_Tree)
            ::MoveWindow(m_Tree, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    }

    LRESULT OnNotify(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        NMHDR *Header = reinterpret_cast<NMHDR *>(lParam);

        if (!Header || Header->hwndFrom != m_Tree)
            return 0;

        switch (Header->code)
        {
            case TVN_ITEMEXPANDINGW:
            {
                NMTREEVIEWW *Notify = reinterpret_cast<NMTREEVIEWW *>(lParam);
                if (Notify->action == TVE_EXPAND)
                    PopulateTreeItem(Notify->itemNew.hItem);
                break;
            }

            case TVN_SELCHANGEDW:
            {
                NMTREEVIEWW *Notify = reinterpret_cast<NMTREEVIEWW *>(lParam);
                TreeItemData *Data = GetTreeItemData(Notify->itemNew.hItem);
                if (Data && !m_SuppressSelection)
                    NotifySelectionChanged(Data->Item);
                break;
            }

            case TVN_DELETEITEMW:
            {
                NMTREEVIEWW *Notify = reinterpret_cast<NMTREEVIEWW *>(lParam);
                TreeItemData *Data = reinterpret_cast<TreeItemData *>(Notify->itemOld.lParam);
                if (Data)
                {
                    if (Data->IsRoot && Data->Root)
                        Data->Root->TreeItem = NULL;
                    if (!m_Destroying)
                        NotifyItemDeleted(Data->Item, Data->IsRoot);
                    delete Data;
                }
                break;
            }

            case TVN_KEYDOWN:
            {
                NMTVKEYDOWN *Notify = reinterpret_cast<NMTVKEYDOWN *>(lParam);
                NotifyKeyboardInput(WM_KEYDOWN, Notify->wVKey, 0);
                break;
            }

            case NM_CLICK:
            case NM_DBLCLK:
            case NM_RCLICK:
            {
                TVHITTESTINFO Hit = {0};
                NSTCEHITTEST NamespaceHit = NSTCEHT_NOWHERE;
                NSTCECLICKTYPE ClickType;

                GetCursorPos(&Hit.pt);
                ::ScreenToClient(m_Tree, &Hit.pt);
                TreeView_HitTest(m_Tree, &Hit);
                TreeItemData *Data = GetTreeItemData(Hit.hItem);
                if (!Data)
                    break;

                NamespaceHit = (NSTCEHITTEST)0;
                if (Hit.flags & TVHT_ONITEMICON)
                    NamespaceHit = (NSTCEHITTEST)(NamespaceHit | NSTCEHT_ONITEMICON);
                if (Hit.flags & TVHT_ONITEMLABEL)
                    NamespaceHit = (NSTCEHITTEST)(NamespaceHit | NSTCEHT_ONITEMLABEL);
                if (Hit.flags & TVHT_ONITEMINDENT)
                    NamespaceHit = (NSTCEHITTEST)(NamespaceHit | NSTCEHT_ONITEMIDENT);
                if (Hit.flags & TVHT_ONITEMBUTTON)
                    NamespaceHit = (NSTCEHITTEST)(NamespaceHit | NSTCEHT_ONITEMBUTTON);
                if (Hit.flags & TVHT_ONITEMRIGHT)
                    NamespaceHit = (NSTCEHITTEST)(NamespaceHit | NSTCEHT_ONITEMRIGHT);
                if (Hit.flags & TVHT_ONITEMSTATEICON)
                    NamespaceHit = (NSTCEHITTEST)(NamespaceHit | NSTCEHT_ONITEMSTATEICON);
                if (!NamespaceHit)
                    NamespaceHit = NSTCEHT_NOWHERE;

                ClickType = Header->code == NM_RCLICK ? NSTCECT_RBUTTON : NSTCECT_LBUTTON;
                if (Header->code == NM_DBLCLK)
                    ClickType = (NSTCECLICKTYPE)(ClickType | NSTCECT_DBLCLICK);
                NotifyItemClick(Data->Item, NamespaceHit, ClickType);
                break;
            }
        }
        return 0;
    }

public:
    CNamespaceTreeControl() :
        m_Tree(NULL),
        m_NextCookie(0),
        m_Style((NSTCSTYLE)0),
        m_Style2(NSTCS2_DEFAULT),
        m_SuppressSelection(0),
        m_Destroying(FALSE)
    {
    }

    ~CNamespaceTreeControl()
    {
        if (m_hWnd && ::IsWindow(m_hWnd))
            DestroyWindow();
        DeleteAllRoots();
    }

    DECLARE_WND_CLASS_EX(NAMESPACE_TREE_CLASS, CS_DBLCLKS, COLOR_WINDOW)
    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CNamespaceTreeControl)

    BEGIN_COM_MAP(CNamespaceTreeControl)
        COM_INTERFACE_ENTRY_IID(IID_INameSpaceTreeControl2, INameSpaceTreeControl2)
        COM_INTERFACE_ENTRY2_IID(IID_INameSpaceTreeControl, INameSpaceTreeControl, INameSpaceTreeControl2)
        COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
    END_COM_MAP()

    BEGIN_MSG_MAP(CNamespaceTreeControl)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
    END_MSG_MAP()

    STDMETHODIMP Initialize(HWND Parent, RECT *Rect, NSTCSTYLE Style) override
    {
        RECT InitialRect = {0, 0, 0, 0};
        DWORD Error;

        if (!Parent || !::IsWindow(Parent))
            return E_INVALIDARG;
        if (m_hWnd)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

        if (Rect)
            InitialRect = *Rect;
        m_Style = Style;

        if (!Create(Parent, InitialRect, NULL,
                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                    0, 0U, NULL))
        {
            Error = GetLastError();
            return HRESULT_FROM_WIN32(Error ? Error : ERROR_NOT_ENOUGH_MEMORY);
        }
        return S_OK;
    }

    STDMETHODIMP TreeAdvise(IUnknown *Unknown, DWORD *Cookie) override
    {
        EventEntry Entry;
        HRESULT hr;

        if (!Unknown || !Cookie)
            return E_INVALIDARG;
        *Cookie = 0;

        hr = Unknown->QueryInterface(IID_PPV_ARG(INameSpaceTreeControlEvents, &Entry.Events));
        if (FAILED(hr))
            return hr;

        do
            ++m_NextCookie;
        while (!m_NextCookie);
        Entry.Cookie = m_NextCookie;
        m_Events.AddTail(Entry);
        *Cookie = Entry.Cookie;
        return S_OK;
    }

    STDMETHODIMP TreeUnadvise(DWORD Cookie) override
    {
        POSITION Position = m_Events.GetHeadPosition();

        while (Position)
        {
            POSITION Current = Position;
            EventEntry& Entry = m_Events.GetNext(Position);
            if (Entry.Cookie == Cookie)
            {
                m_Events.RemoveAt(Current);
                return S_OK;
            }
        }
        return CONNECT_E_NOCONNECTION;
    }

    STDMETHODIMP AppendRoot(
        IShellItem *RootItem,
        SHCONTF EnumFlags,
        NSTCROOTSTYLE RootStyle,
        IShellItemFilter *Filter) override
    {
        return InsertRoot((INT)m_Roots.GetCount(), RootItem, EnumFlags, RootStyle, Filter);
    }

    STDMETHODIMP InsertRoot(
        INT Index,
        IShellItem *RootItem,
        SHCONTF EnumFlags,
        NSTCROOTSTYLE RootStyle,
        IShellItemFilter *Filter) override
    {
        RootEntry *Root;
        POSITION Position;
        HRESULT hr;

        if (!RootItem)
            return E_INVALIDARG;
        if (!m_Tree)
            return E_UNEXPECTED;
        if (Index < 0 || (size_t)Index > m_Roots.GetCount())
            return E_INVALIDARG;

        Root = new RootEntry;
        if (!Root)
            return E_OUTOFMEMORY;

        Root->Item = RootItem;
        Root->Filter = Filter;
        Root->EnumFlags = EnumFlags;
        Root->Style = RootStyle;
        hr = GetItemPidl(RootItem, Root->Pidl);
        if (FAILED(hr))
        {
            delete Root;
            return hr;
        }

        if ((size_t)Index == m_Roots.GetCount())
            Position = m_Roots.AddTail(Root);
        else
            Position = m_Roots.InsertBefore(m_Roots.FindIndex(Index), Root);

        if (RootStyle & NSTCRS_HIDDEN)
        {
            hr = EnumerateChildren(Root, TVI_ROOT, Root->Pidl);
            Root->Enumerated = SUCCEEDED(hr);
        }
        else
        {
            hr = InsertTreeItem(Root, TVI_ROOT, FindVisibleRootInsertAfter(Root), RootItem,
                                Root->Pidl, TRUE, &Root->TreeItem);
            if (SUCCEEDED(hr) && (RootStyle & NSTCRS_EXPANDED))
                hr = EnsureTreeItemExpanded(Root->TreeItem);
        }

        if (FAILED(hr))
        {
            DeleteRootTreeItems(Root);
            m_Roots.RemoveAt(Position);
            delete Root;
        }
        return hr;
    }

    STDMETHODIMP RemoveRoot(IShellItem *RootItem) override
    {
        POSITION Position = NULL;
        RootEntry *Root;

        if (!RootItem)
            return E_INVALIDARG;

        Root = FindRoot(RootItem, &Position);
        if (!Root)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

        DeleteRootTreeItems(Root);
        m_Roots.RemoveAt(Position);
        delete Root;
        return S_OK;
    }

    STDMETHODIMP RemoveAllRoots() override
    {
        DeleteAllRoots();
        return S_OK;
    }

    STDMETHODIMP GetRootItems(IShellItemArray **RootItems) override
    {
        CAtlArray<CComPtr<IShellItem> > Items;
        POSITION Position;

        if (!RootItems)
            return E_POINTER;
        *RootItems = NULL;

        Position = m_Roots.GetHeadPosition();
        while (Position)
            Items.Add(m_Roots.GetNext(Position)->Item);
        return CreateNamespaceShellItemArray(Items, RootItems);
    }

    STDMETHODIMP SetItemState(
        IShellItem *Item,
        NSTCITEMSTATE Mask,
        NSTCITEMSTATE State) override
    {
        CComHeapPtr<ITEMIDLIST> Pidl;
        HTREEITEM TreeItem;
        TreeItemData *Data;
        TVITEMW TvItem = {0};
        NSTCITEMSTATE NewState;
        HRESULT hr;

        hr = GetItemPidl(Item, Pidl);
        if (FAILED(hr))
            return hr;
        TreeItem = FindTreeItem(Pidl);
        if (!TreeItem)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        Data = GetTreeItemData(TreeItem);

        NewState = (NSTCITEMSTATE)((Data->State & ~Mask) | (State & Mask));
        NotifyStateChanging(Item, Mask, NewState);

        if (Mask & NSTCIS_BOLD)
        {
            TvItem.mask = TVIF_STATE;
            TvItem.hItem = TreeItem;
            TvItem.stateMask = TVIS_BOLD;
            TvItem.state = (NewState & NSTCIS_BOLD) ? TVIS_BOLD : 0;
            TreeView_SetItem(m_Tree, &TvItem);
        }

        if (Mask & NSTCIS_EXPANDED)
        {
            if (NewState & NSTCIS_EXPANDED)
                EnsureTreeItemExpanded(TreeItem);
            else
                TreeView_Expand(m_Tree, TreeItem, TVE_COLLAPSE);
        }

        if (Mask & (NSTCIS_SELECTED | NSTCIS_SELECTEDNOEXPAND))
        {
            ++m_SuppressSelection;
            if (NewState & (NSTCIS_SELECTED | NSTCIS_SELECTEDNOEXPAND))
                TreeView_SelectItem(m_Tree, TreeItem);
            else if (TreeView_GetSelection(m_Tree) == TreeItem)
                TreeView_SelectItem(m_Tree, NULL);
            --m_SuppressSelection;
        }

        Data->State = NewState;
        NotifyStateChanged(Item, Mask, NewState);
        return S_OK;
    }

    STDMETHODIMP GetItemState(
        IShellItem *Item,
        NSTCITEMSTATE Mask,
        NSTCITEMSTATE *State) override
    {
        CComHeapPtr<ITEMIDLIST> Pidl;
        HTREEITEM TreeItem;
        TreeItemData *Data;
        UINT TvState;
        HRESULT hr;

        if (!State)
            return E_POINTER;
        *State = NSTCIS_NONE;

        hr = GetItemPidl(Item, Pidl);
        if (FAILED(hr))
            return hr;
        TreeItem = FindTreeItem(Pidl);
        if (!TreeItem)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        Data = GetTreeItemData(TreeItem);

        TvState = TreeView_GetItemState(m_Tree, TreeItem, TVIS_SELECTED | TVIS_EXPANDED | TVIS_BOLD);
        Data->State = (NSTCITEMSTATE)(Data->State & ~(NSTCIS_SELECTED | NSTCIS_EXPANDED | NSTCIS_BOLD));
        if (TvState & TVIS_SELECTED)
            Data->State = (NSTCITEMSTATE)(Data->State | NSTCIS_SELECTED);
        if (TvState & TVIS_EXPANDED)
            Data->State = (NSTCITEMSTATE)(Data->State | NSTCIS_EXPANDED);
        if (TvState & TVIS_BOLD)
            Data->State = (NSTCITEMSTATE)(Data->State | NSTCIS_BOLD);
        *State = (NSTCITEMSTATE)(Data->State & Mask);
        return S_OK;
    }

    STDMETHODIMP GetSelectedItems(IShellItemArray **SelectedItems) override
    {
        CAtlArray<CComPtr<IShellItem> > Items;
        HTREEITEM Selection;
        TreeItemData *Data;

        if (!SelectedItems)
            return E_POINTER;
        *SelectedItems = NULL;

        Selection = TreeView_GetSelection(m_Tree);
        Data = GetTreeItemData(Selection);
        if (Data)
            Items.Add(Data->Item);
        return CreateNamespaceShellItemArray(Items, SelectedItems);
    }

    STDMETHODIMP GetItemCustomState(IShellItem *Item, INT *StateNumber) override
    {
        CComHeapPtr<ITEMIDLIST> Pidl;
        HTREEITEM TreeItem;
        TreeItemData *Data;
        HRESULT hr;

        if (!StateNumber)
            return E_POINTER;
        *StateNumber = 0;
        hr = GetItemPidl(Item, Pidl);
        if (FAILED(hr))
            return hr;
        TreeItem = FindTreeItem(Pidl);
        if (!TreeItem)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        Data = GetTreeItemData(TreeItem);
        *StateNumber = Data->CustomState;
        return S_OK;
    }

    STDMETHODIMP SetItemCustomState(IShellItem *Item, INT StateNumber) override
    {
        CComHeapPtr<ITEMIDLIST> Pidl;
        HTREEITEM TreeItem;
        TreeItemData *Data;
        HRESULT hr;

        hr = GetItemPidl(Item, Pidl);
        if (FAILED(hr))
            return hr;
        TreeItem = FindTreeItem(Pidl);
        if (!TreeItem)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        Data = GetTreeItemData(TreeItem);
        Data->CustomState = StateNumber;
        return S_OK;
    }

    STDMETHODIMP EnsureItemVisible(IShellItem *Item) override
    {
        CComHeapPtr<ITEMIDLIST> Pidl;
        HTREEITEM TreeItem;
        HRESULT hr;

        hr = GetItemPidl(Item, Pidl);
        if (FAILED(hr))
            return hr;

        TreeItem = FindTreeItem(Pidl);
        if (!TreeItem)
            TreeItem = FindAndExpandToPidl(Pidl);
        if (!TreeItem)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

        TreeView_EnsureVisible(m_Tree, TreeItem);
        ++m_SuppressSelection;
        TreeView_SelectItem(m_Tree, TreeItem);
        --m_SuppressSelection;
        return S_OK;
    }

    STDMETHODIMP SetTheme(LPCWSTR Theme) override
    {
        if (!m_Tree)
            return E_UNEXPECTED;
        return SetWindowTheme(m_Tree, Theme, NULL);
    }

    STDMETHODIMP GetNextItem(
        IShellItem *Item,
        NSTCGNI Relation,
        IShellItem **NextItem) override
    {
        CComHeapPtr<ITEMIDLIST> Pidl;
        HTREEITEM TreeItem = NULL;
        HTREEITEM Next = NULL;
        TreeItemData *Data;
        HRESULT hr;

        if (!NextItem)
            return E_POINTER;
        *NextItem = NULL;

        if (Item)
        {
            hr = GetItemPidl(Item, Pidl);
            if (FAILED(hr))
                return hr;
            TreeItem = FindTreeItem(Pidl);
            if (!TreeItem)
                return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }

        switch (Relation)
        {
            case NSTCGNI_NEXT:
                Next = TreeItem ? TreeView_GetNextSibling(m_Tree, TreeItem) : NULL;
                break;
            case NSTCGNI_NEXTVISIBLE:
                Next = TreeItem ? TreeView_GetNextVisible(m_Tree, TreeItem) : NULL;
                break;
            case NSTCGNI_PREV:
                Next = TreeItem ? TreeView_GetPrevSibling(m_Tree, TreeItem) : NULL;
                break;
            case NSTCGNI_PREVVISIBLE:
                Next = TreeItem ? TreeView_GetPrevVisible(m_Tree, TreeItem) : NULL;
                break;
            case NSTCGNI_PARENT:
                Next = TreeItem ? TreeView_GetParent(m_Tree, TreeItem) : NULL;
                break;
            case NSTCGNI_CHILD:
                Next = TreeItem ? TreeView_GetChild(m_Tree, TreeItem) : NULL;
                break;
            case NSTCGNI_FIRSTVISIBLE:
                Next = TreeView_GetFirstVisible(m_Tree);
                break;
            case NSTCGNI_LASTVISIBLE:
                for (Next = TreeView_GetFirstVisible(m_Tree); Next; )
                {
                    HTREEITEM Candidate = TreeView_GetNextVisible(m_Tree, Next);
                    if (!Candidate)
                        break;
                    Next = Candidate;
                }
                break;
            default:
                return E_INVALIDARG;
        }

        Data = GetTreeItemData(Next);
        if (!Data)
            return S_FALSE;
        *NextItem = Data->Item;
        (*NextItem)->AddRef();
        return S_OK;
    }

    STDMETHODIMP HitTest(POINT *Point, IShellItem **Item) override
    {
        TVHITTESTINFO Hit = {0};
        TreeItemData *Data;

        if (!Point || !Item)
            return E_POINTER;
        *Item = NULL;

        Hit.pt = *Point;
        TreeView_HitTest(m_Tree, &Hit);
        Data = GetTreeItemData(Hit.hItem);
        if (!Data)
            return S_FALSE;
        *Item = Data->Item;
        (*Item)->AddRef();
        return S_OK;
    }

    STDMETHODIMP GetItemRect(IShellItem *Item, RECT *Rect) override
    {
        CComHeapPtr<ITEMIDLIST> Pidl;
        HTREEITEM TreeItem;
        HRESULT hr;

        if (!Rect)
            return E_POINTER;
        SetRectEmpty(Rect);

        hr = GetItemPidl(Item, Pidl);
        if (FAILED(hr))
            return hr;
        TreeItem = FindTreeItem(Pidl);
        if (!TreeItem)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        return TreeView_GetItemRect(m_Tree, TreeItem, Rect, FALSE) ? S_OK : E_FAIL;
    }

    STDMETHODIMP CollapseAll() override
    {
        HTREEITEM Item = TreeView_GetRoot(m_Tree);
        while (Item)
        {
            TreeView_Expand(m_Tree, Item, TVE_COLLAPSE);
            Item = TreeView_GetNextSibling(m_Tree, Item);
        }
        return S_OK;
    }

    STDMETHODIMP SetControlStyle(NSTCSTYLE Mask, NSTCSTYLE Style) override
    {
        m_Style = (NSTCSTYLE)((m_Style & ~Mask) | (Style & Mask));
        ApplyTreeStyle();
        return S_OK;
    }

    STDMETHODIMP GetControlStyle(NSTCSTYLE Mask, NSTCSTYLE *Style) override
    {
        if (!Style)
            return E_POINTER;
        *Style = (NSTCSTYLE)(m_Style & Mask);
        return S_OK;
    }

    STDMETHODIMP SetControlStyle2(NSTCSTYLE2 Mask, NSTCSTYLE2 Style) override
    {
        m_Style2 = (NSTCSTYLE2)((m_Style2 & ~Mask) | (Style & Mask));
        return S_OK;
    }

    STDMETHODIMP GetControlStyle2(NSTCSTYLE2 Mask, NSTCSTYLE2 *Style) override
    {
        if (!Style)
            return E_POINTER;
        *Style = (NSTCSTYLE2)(m_Style2 & Mask);
        return S_OK;
    }

    STDMETHODIMP GetWindow(HWND *Window) override
    {
        if (!Window)
            return E_POINTER;
        *Window = m_hWnd;
        return m_hWnd ? S_OK : E_FAIL;
    }

    STDMETHODIMP ContextSensitiveHelp(BOOL EnterMode) override
    {
        return E_NOTIMPL;
    }
};

EXTERN_C HRESULT WINAPI NamespaceTreeControl_Constructor(
    IUnknown *Outer,
    REFIID riid,
    void **Object)
{
    if (Outer)
        return CLASS_E_NOAGGREGATION;
    return ShellObjectCreator<CNamespaceTreeControl>(riid, Object);
}
