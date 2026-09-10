/*
 * PROJECT:     ReactOS shell32
 * LICENSE:     GPL-3.0-or-later
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Public queued shell file operations
 */
#pragma once

class CFileOperation :
    public CComCoClass<CFileOperation, &CLSID_FileOperation>,
    public CComObjectRootEx<CComSingleThreadModel>,
    public IFileOperation
{
    struct Job
    {
        UINT Operation;
        DWORD Attributes;
        CComPtr<IShellItem> Item, Destination;
        CComPtr<IFileOperationProgressSink> Sink;
        CStringW Name, Template;
    };
    struct Subscription
    {
        DWORD Cookie;
        CComPtr<IFileOperationProgressSink> Sink;
    };
    CAtlArray<Job *> m_Jobs;
    CAtlArray<Subscription> m_Sinks;
    CAtlArray<CComPtr<IFileOperationProgressSink> > m_ActiveSinks;
    CComPtr<IOperationsProgressDialog> m_Dialog;
    CComPtr<IPropertyChangeArray> m_Properties;
    HWND m_Owner;
    DWORD m_Flags, m_NextCookie;
    BOOL m_Aborted, m_Performing;
    Job *m_Job;
    HRESULT m_CallbackResult;

    HRESULT Queue(UINT operation, IShellItem *item, IShellItem *destination, PCWSTR name, IFileOperationProgressSink *sink, DWORD attributes = 0, PCWSTR templateName = NULL);
    HRESULT QueueItems(UINT operation, IUnknown *items, IShellItem *destination, PCWSTR name = NULL);
    HRESULT Notify(FILEOPCALLBACKEVENT event, PCWSTR source, PCWSTR destination, UINT attributes, HRESULT result);
    HRESULT NotifySink(IFileOperationProgressSink *sink, FILEOPCALLBACKEVENT event, IShellItem *source, IShellItem *destination, PCWSTR name, IShellItem *created, UINT attributes, HRESULT result);
    static HRESULT CALLBACK Callback(FILEOPCALLBACKEVENT event, PCWSTR source, PCWSTR destination, UINT attributes, HRESULT result, void *data);
    HRESULT ApplyProperties(IShellItem *item);
    void ClearJobs();

public:
    CFileOperation();
    ~CFileOperation();
    STDMETHOD(Advise)(IFileOperationProgressSink *sink, DWORD *cookie) override;
    STDMETHOD(Unadvise)(DWORD cookie) override;
    STDMETHOD(SetOperationFlags)(DWORD flags) override;
    STDMETHOD(SetProgressMessage)(LPCWSTR message) override;
    STDMETHOD(SetProgressDialog)(IOperationsProgressDialog *dialog) override;
    STDMETHOD(SetProperties)(IPropertyChangeArray *properties) override;
    STDMETHOD(SetOwnerWindow)(HWND owner) override;
    STDMETHOD(ApplyPropertiesToItem)(IShellItem *item) override;
    STDMETHOD(ApplyPropertiesToItems)(IUnknown *items) override;
    STDMETHOD(RenameItem)(IShellItem *item, LPCWSTR name, IFileOperationProgressSink *sink) override;
    STDMETHOD(RenameItems)(IUnknown *items, LPCWSTR name) override;
    STDMETHOD(MoveItem)(IShellItem *item, IShellItem *destination, LPCWSTR name, IFileOperationProgressSink *sink) override;
    STDMETHOD(MoveItems)(IUnknown *items, IShellItem *destination) override;
    STDMETHOD(CopyItem)(IShellItem *item, IShellItem *destination, LPCWSTR name, IFileOperationProgressSink *sink) override;
    STDMETHOD(CopyItems)(IUnknown *items, IShellItem *destination) override;
    STDMETHOD(DeleteItem)(IShellItem *item, IFileOperationProgressSink *sink) override;
    STDMETHOD(DeleteItems)(IUnknown *items) override;
    STDMETHOD(NewItem)(IShellItem *destination, DWORD attributes, LPCWSTR name, LPCWSTR templateName, IFileOperationProgressSink *sink) override;
    STDMETHOD(PerformOperations)() override;
    STDMETHOD(GetAnyOperationsAborted)(BOOL *aborted) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_FILEOPERATION)
    DECLARE_NOT_AGGREGATABLE(CFileOperation)
    BEGIN_COM_MAP(CFileOperation)
        COM_INTERFACE_ENTRY_IID(IID_IFileOperation, IFileOperation)
    END_COM_MAP()
};
