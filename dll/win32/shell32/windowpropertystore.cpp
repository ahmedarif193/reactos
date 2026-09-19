/*
 * PROJECT:     ReactOS shell32
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Window property stores
 */

#include "precomp.h"
#include <propsys.h>

static const WCHAR propertyPrefix[] = L"ReactOS.PropertyStore.";

class CWindowPropertyStore :
    public CComObjectRootEx<CComMultiThreadModel>,
    public IPropertyStore
{
    HWND m_window;
    DWORD m_process;
    HANDLE m_mutex;

    HRESULT Lock()
    {
        DWORD result = WaitForSingleObject(m_mutex, INFINITE);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
            return HRESULT_FROM_WIN32(GetLastError());
        DWORD process = 0;
        if (!GetWindowThreadProcessId(m_window, &process) || process != m_process)
        {
            ReleaseMutex(m_mutex);
            return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
        }
        return S_OK;
    }

    static HRESULT PropertyName(REFPROPERTYKEY key, WCHAR *name, UINT count)
    {
        StringCchCopyW(name, count, propertyPrefix);
        return PSStringFromPropertyKey(key, name + _countof(propertyPrefix) - 1,
                                      count - _countof(propertyPrefix) + 1);
    }

    struct Enumeration
    {
        DWORD count;
        DWORD index;
        PROPERTYKEY *key;
        BOOL found;
    };

    static BOOL CALLBACK EnumProperty(HWND, LPWSTR name, HANDLE, ULONG_PTR data)
    {
        Enumeration *enumeration = reinterpret_cast<Enumeration *>(data);
        PROPERTYKEY key;
        if (IS_INTRESOURCE(name) ||
            wcsncmp(name, propertyPrefix, _countof(propertyPrefix) - 1) ||
            FAILED(PSPropertyKeyFromString(name + _countof(propertyPrefix) - 1, &key)))
            return TRUE;
        if (enumeration->key && enumeration->count == enumeration->index)
        {
            *enumeration->key = key;
            enumeration->found = TRUE;
        }
        ++enumeration->count;
        return TRUE;
    }

public:
    CWindowPropertyStore() : m_window(NULL), m_process(0), m_mutex(NULL) {}
    ~CWindowPropertyStore() { if (m_mutex) CloseHandle(m_mutex); }

    BEGIN_COM_MAP(CWindowPropertyStore)
        COM_INTERFACE_ENTRY_IID(IID_IPropertyStore, IPropertyStore)
    END_COM_MAP()

    HRESULT Initialize(HWND window)
    {
        if (!GetWindowThreadProcessId(window, &m_process))
            return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
        m_window = window;
        WCHAR name[96];
        StringCchPrintfW(name, _countof(name), L"Local\\ReactOS.WindowPropertyStore.%lu.%08lx",
                         m_process, HandleToUlong(window));
        m_mutex = CreateMutexW(NULL, FALSE, name);
        return m_mutex ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }

    STDMETHODIMP GetCount(DWORD *count) override
    {
        if (!count) return E_POINTER;
        *count = 0;
        HRESULT hr = Lock();
        if (FAILED(hr)) return hr;
        Enumeration enumeration = {0, 0, NULL, FALSE};
        EnumPropsExW(m_window, EnumProperty, reinterpret_cast<LPARAM>(&enumeration));
        *count = enumeration.count;
        ReleaseMutex(m_mutex);
        return S_OK;
    }

    STDMETHODIMP GetAt(DWORD index, PROPERTYKEY *key) override
    {
        if (!key) return E_POINTER;
        ZeroMemory(key, sizeof(*key));
        HRESULT hr = Lock();
        if (FAILED(hr)) return hr;
        Enumeration enumeration = {0, index, key, FALSE};
        EnumPropsExW(m_window, EnumProperty, reinterpret_cast<LPARAM>(&enumeration));
        ReleaseMutex(m_mutex);
        return enumeration.found ? S_OK : E_INVALIDARG;
    }

    STDMETHODIMP GetValue(REFPROPERTYKEY key, PROPVARIANT *value) override
    {
        if (!value) return E_POINTER;
        PropVariantInit(value);
        WCHAR name[96];
        HRESULT hr = PropertyName(key, name, _countof(name));
        if (FAILED(hr)) return hr;
        hr = Lock();
        if (FAILED(hr)) return hr;
        HANDLE handle = GetPropW(m_window, name);
        if (handle)
        {
            DWORD *data = static_cast<DWORD *>(SHLockSharedEx(handle, m_process, FALSE));
            if (!data)
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            else
            {
                CComPtr<IStream> stream;
                MEMORY_BASIC_INFORMATION info;
                SIZE_T available = 0;
                if (VirtualQuery(data, &info, sizeof(info)))
                    available = info.RegionSize - (reinterpret_cast<BYTE *>(data) -
                                                  static_cast<BYTE *>(info.BaseAddress));
                if (available >= sizeof(*data) && *data <= available - sizeof(*data))
                    stream.Attach(SHCreateMemStream(reinterpret_cast<BYTE *>(data + 1), *data));
                hr = stream ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                if (SUCCEEDED(hr))
                {
                    CComPtr<IPropertyStorage> storage;
                    hr = StgOpenPropStg(stream, key.fmtid, 0, 0, &storage);
                    if (SUCCEEDED(hr))
                    {
                        PROPSPEC spec = { PRSPEC_PROPID, { PID_FIRST_USABLE } };
                        hr = storage->ReadMultiple(1, &spec, value);
                    }
                }
                SHUnlockShared(data);
            }
        }
        ReleaseMutex(m_mutex);
        return hr;
    }

    STDMETHODIMP SetValue(REFPROPERTYKEY key, REFPROPVARIANT value) override
    {
        WCHAR name[96];
        HRESULT hr = PropertyName(key, name, _countof(name));
        if (FAILED(hr)) return hr;
        HANDLE replacement = NULL;
        if (value.vt != VT_EMPTY)
        {
            CComPtr<IStream> stream;
            CComPtr<IPropertyStorage> storage;
            hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
            if (SUCCEEDED(hr))
                hr = StgCreatePropStg(stream, key.fmtid, NULL, PROPSETFLAG_DEFAULT, 0, &storage);
            PROPSPEC spec = { PRSPEC_PROPID, { PID_FIRST_USABLE } };
            if (SUCCEEDED(hr)) hr = storage->WriteMultiple(1, &spec, &value, PID_FIRST_USABLE);
            if (SUCCEEDED(hr)) hr = storage->Commit(STGC_DEFAULT);
            STATSTG stat;
            if (SUCCEEDED(hr)) hr = stream->Stat(&stat, STATFLAG_NONAME);
            if (FAILED(hr)) return hr;
            if (stat.cbSize.HighPart || stat.cbSize.LowPart > MAXDWORD - 2 * sizeof(DWORD))
                return E_OUTOFMEMORY;
            DWORD size = stat.cbSize.LowPart;
            DWORD *data = static_cast<DWORD *>(CoTaskMemAlloc(sizeof(DWORD) + size));
            if (!data) return E_OUTOFMEMORY;
            *data = size;
            LARGE_INTEGER offset = {};
            hr = stream->Seek(offset, STREAM_SEEK_SET, NULL);
            ULONG read = 0;
            if (SUCCEEDED(hr)) hr = stream->Read(data + 1, size, &read);
            if (SUCCEEDED(hr) && read != size) hr = STG_E_READFAULT;
            if (SUCCEEDED(hr))
            {
                replacement = SHAllocShared(data, sizeof(DWORD) + size, m_process);
                if (!replacement) hr = E_OUTOFMEMORY;
            }
            CoTaskMemFree(data);
            if (FAILED(hr)) return hr;
        }
        hr = Lock();
        if (SUCCEEDED(hr))
        {
            HANDLE previous = GetPropW(m_window, name);
            if (replacement && !SetPropW(m_window, name, replacement))
                hr = HRESULT_FROM_WIN32(GetLastError());
            else
            {
                if (!replacement) RemovePropW(m_window, name);
                if (previous) SHFreeShared(previous, m_process);
                replacement = NULL;
            }
            ReleaseMutex(m_mutex);
        }
        if (replacement) SHFreeShared(replacement, m_process);
        return hr;
    }

    STDMETHODIMP Commit() override { return S_OK; }
};

EXTERN_C HRESULT WINAPI SHGetPropertyStoreForWindow(HWND window, REFIID iid, void **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    CComObject<CWindowPropertyStore> *store;
    HRESULT hr = CComObject<CWindowPropertyStore>::CreateInstance(&store);
    if (FAILED(hr)) return hr;
    store->AddRef();
    hr = store->Initialize(window);
    if (SUCCEEDED(hr)) hr = store->QueryInterface(iid, out);
    store->Release();
    return hr;
}
