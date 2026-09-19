/*
 * PROJECT:     browseui
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ITaskbarList header
 * COPYRIGHT:   Copyright 2018 Mark Jansen (mark.jansen@reactos.org)
 */

#ifndef _CTASKBARLIST_H_
#define _CTASKBARLIST_H_

class CTaskbarList :
    public CComCoClass<CTaskbarList, &CLSID_TaskbarList>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public ITaskbarList4
{
    HWND m_hTaskWnd;
    UINT m_ShellHookMsg;

    HWND TaskWnd();
    void SendTaskWndShellHook(WPARAM wParam, HWND hWnd);

public:
    CTaskbarList();
    virtual ~CTaskbarList();

    /*** ITaskbarList3 / ITaskbarList4 methods ***/
    STDMETHOD(SetProgressValue)(HWND hwnd, ULONGLONG completed, ULONGLONG total) override;
    STDMETHOD(SetProgressState)(HWND hwnd, TBPFLAG flags) override;
    STDMETHOD(RegisterTab)(HWND tab, HWND mdi) override;
    STDMETHOD(UnregisterTab)(HWND tab) override;
    STDMETHOD(SetTabOrder)(HWND tab, HWND before) override;
    STDMETHOD(SetTabActive)(HWND tab, HWND mdi, DWORD reserved) override;
    STDMETHOD(ThumbBarAddButtons)(HWND hwnd, UINT count, LPTHUMBBUTTON buttons) override;
    STDMETHOD(ThumbBarUpdateButtons)(HWND hwnd, UINT count, LPTHUMBBUTTON buttons) override;
    STDMETHOD(ThumbBarSetImageList)(HWND hwnd, HIMAGELIST images) override;
    STDMETHOD(SetOverlayIcon)(HWND hwnd, HICON icon, LPCWSTR description) override;
    STDMETHOD(SetThumbnailTooltip)(HWND hwnd, LPCWSTR tip) override;
    STDMETHOD(SetThumbnailClip)(HWND hwnd, RECT *clip) override;
    STDMETHOD(SetTabProperties)(HWND tab, STPFLAG flags) override;

    /*** ITaskbarList2 methods ***/
    STDMETHOD(MarkFullscreenWindow)(HWND hwnd, BOOL fFullscreen) override;

    /*** ITaskbarList methods ***/
    STDMETHOD(HrInit)() override;
    STDMETHOD(AddTab)(HWND hwnd) override;
    STDMETHOD(DeleteTab)(HWND hwnd) override;
    STDMETHOD(ActivateTab)(HWND hwnd) override;
    STDMETHOD(SetActiveAlt)(HWND hwnd) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_TASKBARLIST)
    DECLARE_NOT_AGGREGATABLE(CTaskbarList)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CTaskbarList)
        COM_INTERFACE_ENTRY_IID(IID_ITaskbarList4, ITaskbarList4)
        COM_INTERFACE_ENTRY_IID(IID_ITaskbarList3, ITaskbarList3)
        COM_INTERFACE_ENTRY_IID(IID_ITaskbarList2, ITaskbarList2)
        COM_INTERFACE_ENTRY_IID(IID_ITaskbarList, ITaskbarList)
    END_COM_MAP()
};


#endif // _CTASKBARLIST_H_
