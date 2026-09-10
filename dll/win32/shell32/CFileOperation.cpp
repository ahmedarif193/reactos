/*
 * PROJECT:     ReactOS shell32
 * LICENSE:     GPL-3.0-or-later
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     IFileOperation using the shell's file engine and progress UI
 */
#include "precomp.h"
#include <sherrors.h>

enum { OP_NEW = 0x100, OP_PROPERTIES };

// Public IFileOperation flag values; SHFILEOPSTRUCT only carries the low word.
enum ExtendedOperationFlags
{
    RecycleOnDelete = 0x00080000, // FOFX_RECYCLEONDELETE
    NoMinimizeBox = 0x01000000, // FOFX_NOMINIMIZEBOX
    HideSourcePath = 0x04000000, // FOFX_DONTDISPLAYSOURCEPATH
    HideDestinationPath = 0x08000000, // FOFX_DONTDISPLAYDESTPATH
    HideLocations = 0x80000000 // FOFX_DONTDISPLAYLOCATIONS
};

CFileOperation::CFileOperation() : m_Owner(NULL), m_Flags(FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR), m_NextCookie(1), m_Aborted(FALSE), m_Performing(FALSE), m_Job(NULL), m_CallbackResult(S_OK)
{
}

CFileOperation::~CFileOperation()
{
    ClearJobs();
}

void CFileOperation::ClearJobs()
{
    for (SIZE_T i = 0; i < m_Jobs.GetCount(); ++i) delete m_Jobs[i];
    m_Jobs.SetCount(0);
}

STDMETHODIMP CFileOperation::Advise(IFileOperationProgressSink *sink, DWORD *cookie)
{
    if (!sink || !cookie) return E_INVALIDARG;
    Subscription subscription;
    subscription.Cookie = m_NextCookie++;
    if (!subscription.Cookie) subscription.Cookie = m_NextCookie++;
    subscription.Sink = sink;
    if (m_Sinks.Add(subscription) == (SIZE_T)-1) return E_OUTOFMEMORY;
    *cookie = subscription.Cookie;
    return S_OK;
}

STDMETHODIMP CFileOperation::Unadvise(DWORD cookie)
{
    for (SIZE_T i = 0; i < m_Sinks.GetCount(); ++i)
    {
        if (m_Sinks[i].Cookie != cookie) continue;
        for (SIZE_T j = i + 1; j < m_Sinks.GetCount(); ++j) m_Sinks[j - 1] = m_Sinks[j];
        m_Sinks.SetCount(m_Sinks.GetCount() - 1);
        return S_OK;
    }
    return E_INVALIDARG;
}

STDMETHODIMP CFileOperation::SetOperationFlags(DWORD flags)
{
    if (m_Performing) return E_UNEXPECTED;
    m_Flags = flags;
    return S_OK;
}

STDMETHODIMP CFileOperation::SetProgressMessage(LPCWSTR message)
{
    // This method is also unimplemented by the native IFileOperation class.
    return E_NOTIMPL;
}

STDMETHODIMP CFileOperation::SetProgressDialog(IOperationsProgressDialog *dialog)
{
    if (!dialog) return E_INVALIDARG;
    if (m_Performing) return E_UNEXPECTED;
    m_Dialog = dialog;
    return S_OK;
}

STDMETHODIMP CFileOperation::SetProperties(IPropertyChangeArray *properties)
{
    if (!properties) return E_INVALIDARG;
    m_Properties = properties;
    return S_OK;
}

STDMETHODIMP CFileOperation::SetOwnerWindow(HWND owner)
{
    m_Owner = owner;
    return S_OK;
}

HRESULT CFileOperation::Queue(UINT operation, IShellItem *item, IShellItem *destination, PCWSTR name, IFileOperationProgressSink *sink, DWORD attributes, PCWSTR templateName)
{
    if (m_Performing) return E_UNEXPECTED;
    if (operation != OP_NEW && !item) return E_INVALIDARG;
    if ((operation == FO_COPY || operation == FO_MOVE || operation == OP_NEW) && !destination) return E_INVALIDARG;
    if ((operation == FO_RENAME || operation == OP_NEW) && (!name || !*name)) return E_INVALIDARG;
    if (name && (wcschr(name, L'\\') || wcschr(name, L'/') || wcschr(name, L':'))) return E_INVALIDARG;
    Job *job = new Job;
    if (!job) return E_OUTOFMEMORY;
    job->Operation = operation;
    job->Attributes = attributes;
    job->Item = item;
    job->Destination = destination;
    job->Sink = sink;
    job->Name = name ? name : L"";
    job->Template = templateName ? templateName : L"";
    if (m_Jobs.Add(job) == (SIZE_T)-1) { delete job; return E_OUTOFMEMORY; }
    return S_OK;
}

HRESULT CFileOperation::QueueItems(UINT operation, IUnknown *items, IShellItem *destination, PCWSTR name)
{
    if (!items) return E_INVALIDARG;
    CComPtr<IShellItem> item;
    if (SUCCEEDED(items->QueryInterface(IID_PPV_ARG(IShellItem, &item)))) return Queue(operation, item, destination, name, NULL);
    CComPtr<IShellItemArray> array;
    HRESULT hr = items->QueryInterface(IID_PPV_ARG(IShellItemArray, &array));
    if (FAILED(hr))
    {
        CComPtr<IDataObject> data;
        hr = items->QueryInterface(IID_PPV_ARG(IDataObject, &data));
        if (SUCCEEDED(hr)) hr = SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARG(IShellItemArray, &array));
    }
    if (SUCCEEDED(hr))
    {
        DWORD count;
        hr = array->GetCount(&count);
        if (FAILED(hr)) return hr;
        for (DWORD i = 0; i < count; ++i)
        {
            item.Release();
            hr = array->GetItemAt(i, &item);
            if (SUCCEEDED(hr)) hr = Queue(operation, item, destination, name, NULL);
            if (FAILED(hr)) return hr;
        }
        return S_OK;
    }
    CComPtr<IEnumShellItems> enumerator;
    hr = items->QueryInterface(IID_PPV_ARG(IEnumShellItems, &enumerator));
    if (FAILED(hr)) return hr;
    for (;;)
    {
        item.Release();
        hr = enumerator->Next(1, &item, NULL);
        if (hr != S_OK) return hr == S_FALSE ? S_OK : hr;
        hr = Queue(operation, item, destination, name, NULL);
        if (FAILED(hr)) return hr;
    }
}

STDMETHODIMP CFileOperation::ApplyPropertiesToItem(IShellItem *item) { return Queue(OP_PROPERTIES, item, NULL, NULL, NULL); }
STDMETHODIMP CFileOperation::ApplyPropertiesToItems(IUnknown *items) { return QueueItems(OP_PROPERTIES, items, NULL); }
STDMETHODIMP CFileOperation::RenameItem(IShellItem *item, LPCWSTR name, IFileOperationProgressSink *sink) { return Queue(FO_RENAME, item, NULL, name, sink); }
STDMETHODIMP CFileOperation::RenameItems(IUnknown *items, LPCWSTR name) { return QueueItems(FO_RENAME, items, NULL, name); }
STDMETHODIMP CFileOperation::MoveItem(IShellItem *item, IShellItem *destination, LPCWSTR name, IFileOperationProgressSink *sink) { return Queue(FO_MOVE, item, destination, name, sink); }
STDMETHODIMP CFileOperation::MoveItems(IUnknown *items, IShellItem *destination) { return QueueItems(FO_MOVE, items, destination); }
STDMETHODIMP CFileOperation::CopyItem(IShellItem *item, IShellItem *destination, LPCWSTR name, IFileOperationProgressSink *sink) { return Queue(FO_COPY, item, destination, name, sink); }
STDMETHODIMP CFileOperation::CopyItems(IUnknown *items, IShellItem *destination) { return QueueItems(FO_COPY, items, destination); }
STDMETHODIMP CFileOperation::DeleteItem(IShellItem *item, IFileOperationProgressSink *sink) { return Queue(FO_DELETE, item, NULL, NULL, sink); }
STDMETHODIMP CFileOperation::DeleteItems(IUnknown *items) { return QueueItems(FO_DELETE, items, NULL); }
STDMETHODIMP CFileOperation::NewItem(IShellItem *destination, DWORD attributes, LPCWSTR name, LPCWSTR templateName, IFileOperationProgressSink *sink) { return Queue(OP_NEW, NULL, destination, name, sink, attributes, templateName); }

HRESULT CFileOperation::NotifySink(IFileOperationProgressSink *sink, FILEOPCALLBACKEVENT event, IShellItem *source, IShellItem *destination, PCWSTR name, IShellItem *created, UINT attributes, HRESULT result)
{
    switch (event)
    {
        case FOCE_PRECOPYITEM: return sink->PreCopyItem(0, source, destination, name);
        case FOCE_POSTCOPYITEM: return sink->PostCopyItem(0, source, destination, name, result, created);
        case FOCE_PREMOVEITEM: return sink->PreMoveItem(0, source, destination, name);
        case FOCE_POSTMOVEITEM: return sink->PostMoveItem(0, source, destination, name, result, created);
        case FOCE_PREDELETEITEM: return sink->PreDeleteItem(0, source);
        case FOCE_POSTDELETEITEM: return sink->PostDeleteItem(0, source, result, created);
        case FOCE_PRERENAMEITEM: return sink->PreRenameItem(0, source, name);
        case FOCE_POSTRENAMEITEM: return sink->PostRenameItem(0, source, name, result, created);
        case FOCE_PRENEWITEM: return sink->PreNewItem(0, destination, name);
        case FOCE_POSTNEWITEM: return sink->PostNewItem(0, destination, name, m_Job->Template.IsEmpty() ? NULL : (PCWSTR)m_Job->Template, attributes, result, created);
        default: return S_OK;
    }
}

HRESULT CFileOperation::Notify(FILEOPCALLBACKEVENT event, PCWSTR sourcePath, PCWSTR destinationPath, UINT attributes, HRESULT result)
{
    if (event == FOCE_STARTOPERATIONS) return S_OK;
    if (event == FOCE_FINISHOPERATIONS) { if (SUCCEEDED(m_CallbackResult)) m_CallbackResult = result; return S_OK; }
    CComPtr<IShellItem> source = m_Job->Item, destination = m_Job->Destination, created;
    PCWSTR name = destinationPath ? PathFindFileNameW(destinationPath) : (PCWSTR)m_Job->Name;
    BOOL post = event == FOCE_POSTCOPYITEM || event == FOCE_POSTMOVEITEM || event == FOCE_POSTRENAMEITEM || event == FOCE_POSTNEWITEM;
    if (result == S_OK && (event == FOCE_POSTMOVEITEM || event == FOCE_POSTRENAMEITEM || event == FOCE_POSTDELETEITEM)) result = COPYENGINE_S_DONT_PROCESS_CHILDREN;
    if (post && SUCCEEDED(result) && destinationPath) SHCreateItemFromParsingName(destinationPath, NULL, IID_PPV_ARG(IShellItem, &created));
    if (sourcePath && source)
    {
        PWSTR original = NULL;
        if (SUCCEEDED(source->GetDisplayName(SIGDN_FILESYSPATH, &original)))
        {
            if (lstrcmpiW(original, sourcePath))
            {
                CComPtr<IShellItem> nested;
                if (SUCCEEDED(SHCreateItemFromParsingName(sourcePath, NULL, IID_PPV_ARG(IShellItem, &nested)))) source = nested;
            }
            CoTaskMemFree(original);
        }
    }
    HRESULT hr = S_OK;
    for (SIZE_T i = 0; i < m_ActiveSinks.GetCount(); ++i)
    {
        HRESULT next = NotifySink(m_ActiveSinks[i], event, source, destination, name, created, attributes, result);
        if (FAILED(next) && SUCCEEDED(hr)) hr = next;
    }
    if (m_Job->Sink)
    {
        HRESULT next = NotifySink(m_Job->Sink, event, source, destination, name, created, attributes, result);
        if (FAILED(next) && SUCCEEDED(hr)) hr = next;
    }
    if (FAILED(hr)) m_CallbackResult = hr;
    return hr;
}

HRESULT CALLBACK CFileOperation::Callback(FILEOPCALLBACKEVENT event, PCWSTR source, PCWSTR destination, UINT attributes, HRESULT result, void *data)
{
    return static_cast<CFileOperation *>(data)->Notify(event, source, destination, attributes, result);
}

HRESULT CFileOperation::ApplyProperties(IShellItem *item)
{
    if (!m_Properties) return E_UNEXPECTED;
    CComPtr<IShellItem2> item2;
    HRESULT hr = item->QueryInterface(IID_PPV_ARG(IShellItem2, &item2));
    CComPtr<IPropertyStore> store;
    if (SUCCEEDED(hr)) hr = item2->GetPropertyStore(GPS_READWRITE, IID_PPV_ARG(IPropertyStore, &store));
    UINT count = 0;
    if (SUCCEEDED(hr)) hr = m_Properties->GetCount(&count);
    for (UINT i = 0; i < count && SUCCEEDED(hr); ++i)
    {
        CComPtr<IPropertyChange> change;
        hr = m_Properties->GetAt(i, IID_PPV_ARG(IPropertyChange, &change));
        PROPERTYKEY key;
        PROPVARIANT oldValue = {}, newValue = {};
        if (SUCCEEDED(hr)) hr = change->GetPropertyKey(&key);
        if (SUCCEEDED(hr)) hr = store->GetValue(key, &oldValue);
        if (SUCCEEDED(hr)) hr = change->ApplyToPropVariant(oldValue, &newValue);
        if (SUCCEEDED(hr)) hr = store->SetValue(key, newValue);
        PropVariantClear(&oldValue);
        PropVariantClear(&newValue);
    }
    if (SUCCEEDED(hr)) hr = store->Commit();
    return hr;
}

STDMETHODIMP CFileOperation::PerformOperations()
{
    if (m_Performing || !m_Jobs.GetCount()) return E_UNEXPECTED;
    m_Performing = TRUE;
    m_Aborted = FALSE;
    m_CallbackResult = S_OK;
    m_ActiveSinks.SetCount(0);
    for (SIZE_T i = 0; i < m_Sinks.GetCount(); ++i) m_ActiveSinks.Add(m_Sinks[i].Sink);
    CComPtr<IOperationsProgressDialog> dialog = m_Dialog;
    HRESULT hr = S_OK;
    BOOL started = FALSE;
    if (!(m_Flags & FOF_SILENT))
    {
        DWORD flags = OPPROGDLG_ENABLEPAUSE;
        if (m_Flags & NoMinimizeBox) flags |= PROGDLG_NOMINIMIZE;
        if (m_Flags & HideSourcePath) flags |= OPPROGDLG_DONTDISPLAYSOURCEPATH;
        if (m_Flags & HideDestinationPath) flags |= OPPROGDLG_DONTDISPLAYDESTPATH;
        if (m_Flags & HideLocations) flags |= OPPROGDLG_DONTDISPLAYLOCATIONS;
        if (!dialog) hr = dialog.CoCreateInstance(CLSID_ProgressDialog, IID_IOperationsProgressDialog, NULL, CLSCTX_INPROC_SERVER);
        if (SUCCEEDED(hr)) hr = dialog->StartProgressDialog(m_Owner, flags);
        started = SUCCEEDED(hr);
        if (started) dialog->SetMode(PDM_PREFLIGHT);
    }
    FILEOP_PROGRESS progress = {started ? dialog.p : NULL, 0, 0, 0, 0};
    for (SIZE_T i = 0; i < m_Jobs.GetCount() && SUCCEEDED(hr); ++i)
    {
        Job *job = m_Jobs[i];
        if (job->Item)
        {
            PWSTR path = NULL;
            hr = job->Item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            ULONGLONG bytes = 0, items = 0;
            if (SUCCEEDED(hr)) hr = SHELL32_CountFileOperation(path, &bytes, &items, started ? dialog.p : NULL);
            CoTaskMemFree(path);
            if (SUCCEEDED(hr))
            {
                progress.TotalBytes += min(bytes, MAXULONGLONG - progress.TotalBytes);
                progress.TotalItems += min(items, MAXULONGLONG - progress.TotalItems);
            }
        }
        else ++progress.TotalItems;
    }
    if (started) { dialog->SetMode(PDM_RUN); dialog->ResetTimer(); }
    for (SIZE_T i = 0; i < m_ActiveSinks.GetCount(); ++i)
    {
        HRESULT result = m_ActiveSinks[i]->StartOperations();
        if (FAILED(result) && SUCCEEDED(hr)) hr = result;
        m_ActiveSinks[i]->ResetTimer();
    }
    for (SIZE_T i = 0; i < m_Jobs.GetCount() && SUCCEEDED(hr); ++i)
    {
        m_Job = m_Jobs[i];
        if (started)
        {
            PDOPSTATUS status;
            do
            {
                hr = dialog->GetOperationStatus(&status);
                if (SUCCEEDED(hr) && (status == PDOPS_CANCELLED || status == PDOPS_STOPPED)) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                if (SUCCEEDED(hr) && status == PDOPS_PAUSED) Sleep(50);
            } while (SUCCEEDED(hr) && status == PDOPS_PAUSED);
            if (FAILED(hr)) break;
        }
        PWSTR source = NULL, folder = NULL;
        WCHAR destination[MAX_PATH] = L"";
        if (m_Job->Item) hr = m_Job->Item->GetDisplayName(SIGDN_FILESYSPATH, &source);
        if (SUCCEEDED(hr) && m_Job->Destination) hr = m_Job->Destination->GetDisplayName(SIGDN_FILESYSPATH, &folder);
        if (SUCCEEDED(hr) && m_Job->Operation == FO_RENAME)
        {
            hr = StringCchCopyW(destination, _countof(destination), source);
            if (SUCCEEDED(hr)) PathRemoveFileSpecW(destination);
            if (SUCCEEDED(hr) && !PathAppendW(destination, m_Job->Name)) hr = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        }
        else if (SUCCEEDED(hr) && folder)
        {
            hr = StringCchCopyW(destination, _countof(destination), folder);
            PCWSTR name = m_Job->Name.IsEmpty() && source ? PathFindFileNameW(source) : (PCWSTR)m_Job->Name;
            if (SUCCEEDED(hr) && !PathAppendW(destination, name)) hr = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        }
        if (SUCCEEDED(hr) && m_Job->Operation == OP_PROPERTIES) hr = ApplyProperties(m_Job->Item);
        else if (SUCCEEDED(hr) && m_Job->Operation == OP_NEW)
        {
            hr = Notify(FOCE_PRENEWITEM, NULL, destination, m_Job->Attributes, S_OK);
            if (SUCCEEDED(hr))
            {
                if (m_Job->Attributes & FILE_ATTRIBUTE_DIRECTORY)
                    hr = CreateDirectoryW(destination, NULL) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
                else if (!m_Job->Template.IsEmpty())
                    hr = CopyFileW(m_Job->Template, destination, TRUE) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
                else
                {
                    HANDLE file = CreateFileW(destination, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, m_Job->Attributes, NULL);
                    if (file == INVALID_HANDLE_VALUE) hr = HRESULT_FROM_WIN32(GetLastError());
                    else CloseHandle(file);
                }
                Notify(FOCE_POSTNEWITEM, NULL, destination, m_Job->Attributes, hr);
                if (SUCCEEDED(hr))
                {
                    ++progress.CompletedItems;
                    SHChangeNotify((m_Job->Attributes & FILE_ATTRIBUTE_DIRECTORY) ? SHCNE_MKDIR : SHCNE_CREATE, SHCNF_PATHW, destination, NULL);
                }
            }
        }
        else if (SUCCEEDED(hr))
        {
            // SHFileOperation takes double-NUL-terminated paths even for a single item.
            WCHAR from[MAX_PATH + 1] = {0}, to[MAX_PATH + 1] = {0};
            hr = StringCchCopyW(from, MAX_PATH, source);
            if (SUCCEEDED(hr)) hr = StringCchCopyW(to, MAX_PATH, destination);
            if (SUCCEEDED(hr))
            {
                SHFILEOPSTRUCTW request = {0};
                request.hwnd = m_Owner;
                request.wFunc = m_Job->Operation;
                request.pFrom = from;
                request.pTo = to;
                request.fFlags = (FILEOP_FLAGS)m_Flags;
                if (m_Job->Operation == FO_DELETE && (m_Flags & RecycleOnDelete)) request.fFlags |= FOF_ALLOWUNDO;
                if (!m_Job->Name.IsEmpty()) request.fFlags |= FOF_MULTIDESTFILES;
                m_CallbackResult = S_OK;
                int error = SHELL32_FileOperation(&request, Callback, this, &progress);
                hr = FAILED(m_CallbackResult) ? m_CallbackResult : HRESULT_FROM_WIN32(error);
                if (request.fAnyOperationsAborted && m_CallbackResult != E_ABORT) { m_Aborted = TRUE; if (SUCCEEDED(hr)) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); }
                if (request.hNameMappings) SHFreeNameMappings(request.hNameMappings);
            }
        }
        CoTaskMemFree(source);
        CoTaskMemFree(folder);
        for (SIZE_T j = 0; j < m_ActiveSinks.GetCount(); ++j) m_ActiveSinks[j]->UpdateProgress((UINT)min(progress.TotalItems, (ULONGLONG)MAXUINT), (UINT)min(progress.CompletedItems, (ULONGLONG)MAXUINT));
    }
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || hr == HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))
    {
        m_Aborted = TRUE;
        hr = COPYENGINE_E_USER_CANCELLED;
    }
    if (started)
    {
        if (SUCCEEDED(hr)) dialog->UpdateProgress(progress.TotalBytes ? progress.TotalBytes : progress.TotalItems, progress.TotalBytes ? progress.TotalBytes : progress.TotalItems, progress.TotalBytes, progress.TotalBytes, progress.TotalItems, progress.TotalItems);
        dialog->StopProgressDialog();
    }
    for (SIZE_T i = 0; i < m_ActiveSinks.GetCount(); ++i) m_ActiveSinks[i]->FinishOperations(hr == E_ABORT ? S_OK : hr);
    m_ActiveSinks.SetCount(0);
    m_Job = NULL;
    ClearJobs();
    m_Performing = FALSE;
    return hr;
}

STDMETHODIMP CFileOperation::GetAnyOperationsAborted(BOOL *aborted)
{
    if (!aborted) return E_POINTER;
    *aborted = m_Aborted;
    return S_OK;
}
