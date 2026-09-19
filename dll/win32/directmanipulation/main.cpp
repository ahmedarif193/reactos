/*
 * PROJECT:     ReactOS DirectManipulation
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     COM manager and synchronous manual viewports
 */

#include <windows.h>
#include <objbase.h>
#include <atlbase.h>
#include <atlcom.h>
#include <atlsimpcoll.h>
#include <directmanipulation.h>
#include <float.h>

static LONG objectCount;
static LONG serverLocks;
static CComModule module;

struct ModuleObject
{
    ModuleObject() { InterlockedIncrement(&objectCount); }
    ~ModuleObject() { InterlockedDecrement(&objectCount); }
};

typedef CComCritSecLock<CComAutoCriticalSection> ObjectLock;

static HRESULT CopyMatrix(float *destination, const float *source, DWORD count)
{
    if (!source || count != 6) return E_INVALIDARG;
    for (DWORD i = 0; i < count; ++i)
        if (!_finite(source[i])) return E_INVALIDARG;
    CopyMemory(destination, source, 6 * sizeof(float));
    return S_OK;
}

static void MultiplyMatrix(float *out, const float *a, const float *b)
{
    float result[6] =
    {
        a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3],
        a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
        a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5]
    };
    CopyMemory(out, result, sizeof(result));
}

class CViewport;

class CPrimaryContent final : public IDirectManipulationContent, public IDirectManipulationPrimaryContent
{
    CViewport *m_viewport;
    CComAutoCriticalSection m_lock;
    RECT m_rect;
    CComPtr<IUnknown> m_tag;
    UINT32 m_tagId;
    float m_transform[6];
    float m_minZoom, m_maxZoom;
public:
    CPrimaryContent(CViewport *viewport) : m_viewport(viewport), m_rect{}, m_tagId(0),
        m_transform{1, 0, 0, 1, 0, 0}, m_minZoom(FLT_MIN), m_maxZoom(FLT_MAX) {}
    STDMETHODIMP QueryInterface(REFIID iid, void **out) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP GetContentRect(RECT *rect) override
    {
        if (!rect) return E_POINTER;
        ObjectLock lock(m_lock);
        *rect = m_rect;
        return S_OK;
    }
    STDMETHODIMP SetContentRect(const RECT *rect) override
    {
        if (!rect || rect->right < rect->left || rect->bottom < rect->top) return E_INVALIDARG;
        ObjectLock lock(m_lock);
        m_rect = *rect;
        return S_OK;
    }
    STDMETHODIMP GetViewport(REFIID iid, void **out) override;
    STDMETHODIMP GetTag(REFIID iid, void **out, UINT32 *id) override
    {
        if (!out || !id) return E_POINTER;
        *out = NULL;
        CComPtr<IUnknown> tag;
        { ObjectLock lock(m_lock); *id = m_tagId; tag = m_tag; }
        return tag ? tag->QueryInterface(iid, out) : S_OK;
    }
    STDMETHODIMP SetTag(IUnknown *tag, UINT32 id) override
    {
        ObjectLock lock(m_lock);
        m_tag = tag;
        m_tagId = id;
        return S_OK;
    }
    STDMETHODIMP GetOutputTransform(float *matrix, DWORD count) override;
    STDMETHODIMP GetContentTransform(float *matrix, DWORD count) override
    {
        if (!matrix || count != 6) return E_INVALIDARG;
        ObjectLock lock(m_lock);
        CopyMemory(matrix, m_transform, sizeof(m_transform));
        return S_OK;
    }
    STDMETHODIMP SyncContentTransform(const float *matrix, DWORD count) override;
    STDMETHODIMP SetSnapInterval(DIRECTMANIPULATION_MOTION_TYPES, float, float) override { return E_NOTIMPL; }
    STDMETHODIMP SetSnapPoints(DIRECTMANIPULATION_MOTION_TYPES, const float *, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP SetSnapType(DIRECTMANIPULATION_MOTION_TYPES, DIRECTMANIPULATION_SNAPPOINT_TYPE) override { return E_NOTIMPL; }
    STDMETHODIMP SetSnapCoordinate(DIRECTMANIPULATION_MOTION_TYPES, DIRECTMANIPULATION_SNAPPOINT_COORDINATE, float) override { return E_NOTIMPL; }
    STDMETHODIMP SetZoomBoundaries(float minimum, float maximum) override
    {
        if (!_finite(minimum) || !_finite(maximum) || minimum <= 0 || maximum < minimum) return E_INVALIDARG;
        ObjectLock lock(m_lock);
        m_minZoom = minimum;
        m_maxZoom = maximum;
        return S_OK;
    }
    float ClampZoom(float zoom)
    {
        ObjectLock lock(m_lock);
        return max(m_minZoom, min(m_maxZoom, zoom));
    }
    STDMETHODIMP SetHorizontalAlignment(DIRECTMANIPULATION_HORIZONTALALIGNMENT alignment) override
    { return alignment == DIRECTMANIPULATION_HORIZONTALALIGNMENT_NONE ? S_OK : E_NOTIMPL; }
    STDMETHODIMP SetVerticalAlignment(DIRECTMANIPULATION_VERTICALALIGNMENT alignment) override
    { return alignment == DIRECTMANIPULATION_VERTICALALIGNMENT_NONE ? S_OK : E_NOTIMPL; }
    STDMETHODIMP GetInertiaEndTransform(float *matrix, DWORD count) override
    { return GetContentTransform(matrix, count); }
    STDMETHODIMP GetCenterPoint(float *x, float *y) override
    {
        if (!x || !y) return E_POINTER;
        ObjectLock lock(m_lock);
        *x = (float(m_rect.left) + float(m_rect.right)) / 2;
        *y = (float(m_rect.top) + float(m_rect.bottom)) / 2;
        return S_OK;
    }
};

class CViewport : public CComObjectRootEx<CComMultiThreadModel>, public IDirectManipulationViewport2,
                  private ModuleObject
{
    struct Handler
    {
        DWORD cookie;
        CComPtr<IDirectManipulationViewportEventHandler> sink;
    };
    CComAutoCriticalSection m_lock;
    CSimpleArray<Handler> m_handlers;
    CComPtr<IUnknown> m_tag;
    UINT32 m_tagId;
    DWORD m_nextCookie;
    RECT m_rect;
    float m_transform[6], m_display[6];
    DIRECTMANIPULATION_STATUS m_status;
    DIRECTMANIPULATION_CONFIGURATION m_configuration;
    CSimpleArray<DIRECTMANIPULATION_CONFIGURATION> m_configurations;
    BOOL m_abandoned;
    CPrimaryContent m_content;

    HRESULT Notify(DIRECTMANIPULATION_STATUS status, BOOL changed)
    {
        CSimpleArray<CComPtr<IDirectManipulationViewportEventHandler> > handlers;
        DIRECTMANIPULATION_STATUS previous;
        {
            ObjectLock lock(m_lock);
            if (m_abandoned) return E_UNEXPECTED;
            for (int i = 0; i < m_handlers.GetSize(); ++i)
                if (!handlers.Add(m_handlers[i].sink)) return E_OUTOFMEMORY;
            previous = m_status;
            m_status = status;
        }
        AddRef();
        for (int i = 0; i < handlers.GetSize(); ++i)
        {
            if (previous != status) handlers[i]->OnViewportStatusChanged(this, status, previous);
            if (changed)
            {
                handlers[i]->OnViewportUpdated(this);
                handlers[i]->OnContentUpdated(this, &m_content);
            }
        }
        Release();
        return S_OK;
    }
public:
    CViewport() : m_tagId(0), m_nextCookie(0), m_rect{}, m_transform{1, 0, 0, 1, 0, 0},
        m_display{1, 0, 0, 1, 0, 0}, m_status(DIRECTMANIPULATION_BUILDING),
        m_configuration(DIRECTMANIPULATION_CONFIGURATION_NONE), m_abandoned(FALSE), m_content(this) {}
    BEGIN_COM_MAP(CViewport)
        COM_INTERFACE_ENTRY_IID(IID_IDirectManipulationViewport, IDirectManipulationViewport)
        COM_INTERFACE_ENTRY_IID(IID_IDirectManipulationViewport2, IDirectManipulationViewport2)
    END_COM_MAP()
    HRESULT ContentChanged()
    {
        DIRECTMANIPULATION_STATUS status;
        GetStatus(&status);
        return Notify(status, TRUE);
    }
    HRESULT OutputTransform(float *matrix, DWORD count)
    {
        HRESULT hr = m_content.GetContentTransform(matrix, count);
        if (FAILED(hr)) return hr;
        ObjectLock lock(m_lock);
        MultiplyMatrix(matrix, matrix, m_transform);
        MultiplyMatrix(matrix, matrix, m_display);
        return S_OK;
    }
    STDMETHODIMP Enable() override { return Notify(DIRECTMANIPULATION_READY, FALSE); }
    STDMETHODIMP Disable() override { return Notify(DIRECTMANIPULATION_DISABLED, FALSE); }
    STDMETHODIMP SetContact(UINT32) override { return E_NOTIMPL; }
    STDMETHODIMP ReleaseContact(UINT32) override { return E_INVALIDARG; }
    STDMETHODIMP ReleaseAllContacts() override { return S_OK; }
    STDMETHODIMP GetStatus(DIRECTMANIPULATION_STATUS *status) override
    {
        if (!status) return E_POINTER;
        ObjectLock lock(m_lock);
        *status = m_status;
        return S_OK;
    }
    STDMETHODIMP GetTag(REFIID iid, void **out, UINT32 *id) override
    {
        if (!out || !id) return E_POINTER;
        *out = NULL;
        CComPtr<IUnknown> tag;
        { ObjectLock lock(m_lock); *id = m_tagId; tag = m_tag; }
        return tag ? tag->QueryInterface(iid, out) : S_OK;
    }
    STDMETHODIMP SetTag(IUnknown *tag, UINT32 id) override
    {
        ObjectLock lock(m_lock);
        m_tag = tag;
        m_tagId = id;
        return S_OK;
    }
    STDMETHODIMP GetViewportRect(RECT *rect) override
    {
        if (!rect) return E_POINTER;
        ObjectLock lock(m_lock);
        *rect = m_rect;
        return S_OK;
    }
    STDMETHODIMP SetViewportRect(const RECT *rect) override
    {
        if (!rect || rect->right <= rect->left || rect->bottom <= rect->top) return E_INVALIDARG;
        ObjectLock lock(m_lock);
        m_rect = *rect;
        return S_OK;
    }
    STDMETHODIMP ZoomToRect(float left, float top, float right, float bottom, BOOL animate) override
    {
        if (!_finite(left) || !_finite(top) || !_finite(right) || !_finite(bottom) ||
            right <= left || bottom <= top) return E_INVALIDARG;
        if (animate) return E_NOTIMPL;
        RECT rect;
        GetViewportRect(&rect);
        if (IsRectEmpty(&rect)) return E_UNEXPECTED;
        float scale = m_content.ClampZoom(min((float(rect.right) - rect.left) / (right - left),
                                             (float(rect.bottom) - rect.top) / (bottom - top)));
        float matrix[6] = {scale, 0, 0, scale, rect.left - left * scale, rect.top - top * scale};
        return m_content.SyncContentTransform(matrix, 6);
    }
    STDMETHODIMP SetViewportTransform(const float *matrix, DWORD count) override
    {
        HRESULT hr;
        { ObjectLock lock(m_lock); hr = CopyMatrix(m_transform, matrix, count); }
        return FAILED(hr) ? hr : ContentChanged();
    }
    STDMETHODIMP SyncDisplayTransform(const float *matrix, DWORD count) override
    {
        HRESULT hr;
        { ObjectLock lock(m_lock); hr = CopyMatrix(m_display, matrix, count); }
        return FAILED(hr) ? hr : ContentChanged();
    }
    STDMETHODIMP GetPrimaryContent(REFIID iid, void **out) override { return m_content.QueryInterface(iid, out); }
    STDMETHODIMP AddContent(IDirectManipulationContent *) override { return E_NOTIMPL; }
    STDMETHODIMP RemoveContent(IDirectManipulationContent *) override { return E_INVALIDARG; }
    STDMETHODIMP SetViewportOptions(DIRECTMANIPULATION_VIEWPORT_OPTIONS options) override
    {
        return options & ~DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE ? E_NOTIMPL : S_OK;
    }
    STDMETHODIMP AddConfiguration(DIRECTMANIPULATION_CONFIGURATION configuration) override
    {
        if (configuration & ~0x3b7) return E_INVALIDARG;
        ObjectLock lock(m_lock);
        for (int i = 0; i < m_configurations.GetSize(); ++i)
            if (m_configurations[i] == configuration) return S_OK;
        return m_configurations.Add(configuration) ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP RemoveConfiguration(DIRECTMANIPULATION_CONFIGURATION configuration) override
    {
        ObjectLock lock(m_lock);
        return m_configurations.Remove(configuration) ? S_OK : E_INVALIDARG;
    }
    STDMETHODIMP ActivateConfiguration(DIRECTMANIPULATION_CONFIGURATION configuration) override
    {
        HRESULT hr = AddConfiguration(configuration);
        if (FAILED(hr)) return hr;
        ObjectLock lock(m_lock);
        m_configuration = configuration;
        return S_OK;
    }
    STDMETHODIMP SetManualGesture(DIRECTMANIPULATION_GESTURE_CONFIGURATION configuration) override
    { return configuration == DIRECTMANIPULATION_GESTURE_NONE ? S_OK : E_NOTIMPL; }
    STDMETHODIMP SetChaining(DIRECTMANIPULATION_MOTION_TYPES types) override
    { return types == DIRECTMANIPULATION_MOTION_NONE ? S_OK : E_NOTIMPL; }
    STDMETHODIMP AddEventHandler(HWND window, IDirectManipulationViewportEventHandler *handler, DWORD *cookie) override
    {
        if (!handler || !cookie || !IsWindow(window)) return E_INVALIDARG;
        *cookie = 0;
        ObjectLock lock(m_lock);
        Handler entry;
        entry.cookie = ++m_nextCookie;
        entry.sink = handler;
        if (!m_handlers.Add(entry)) return E_OUTOFMEMORY;
        *cookie = entry.cookie;
        return S_OK;
    }
    STDMETHODIMP RemoveEventHandler(DWORD cookie) override
    {
        ObjectLock lock(m_lock);
        for (int i = 0; i < m_handlers.GetSize(); ++i)
            if (m_handlers[i].cookie == cookie) { m_handlers.RemoveAt(i); return S_OK; }
        return E_INVALIDARG;
    }
    STDMETHODIMP SetInputMode(DIRECTMANIPULATION_INPUT_MODE mode) override
    { return mode == DIRECTMANIPULATION_INPUT_MODE_MANUAL ? S_OK : E_NOTIMPL; }
    STDMETHODIMP SetUpdateMode(DIRECTMANIPULATION_INPUT_MODE mode) override
    { return mode == DIRECTMANIPULATION_INPUT_MODE_MANUAL ? S_OK : E_NOTIMPL; }
    STDMETHODIMP Stop() override { return Notify(DIRECTMANIPULATION_READY, FALSE); }
    STDMETHODIMP Abandon() override
    {
        ObjectLock lock(m_lock);
        m_abandoned = TRUE;
        m_status = DIRECTMANIPULATION_DISABLED;
        m_handlers.RemoveAll();
        m_tag.Release();
        return S_OK;
    }
    STDMETHODIMP AddBehavior(IUnknown *, DWORD *cookie) override
    { if (cookie) *cookie = 0; return E_NOTIMPL; }
    STDMETHODIMP RemoveBehavior(DWORD) override { return E_INVALIDARG; }
    STDMETHODIMP RemoveAllBehaviors() override { return S_OK; }
};

HRESULT CPrimaryContent::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (iid == IID_IUnknown || iid == IID_IDirectManipulationContent)
        *out = static_cast<IDirectManipulationContent *>(this);
    else if (iid == IID_IDirectManipulationPrimaryContent)
        *out = static_cast<IDirectManipulationPrimaryContent *>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}
ULONG CPrimaryContent::AddRef() { return m_viewport->AddRef(); }
ULONG CPrimaryContent::Release() { return m_viewport->Release(); }
HRESULT CPrimaryContent::GetViewport(REFIID iid, void **out) { return m_viewport->QueryInterface(iid, out); }
HRESULT CPrimaryContent::GetOutputTransform(float *matrix, DWORD count) { return m_viewport->OutputTransform(matrix, count); }
HRESULT CPrimaryContent::SyncContentTransform(const float *matrix, DWORD count)
{
    HRESULT hr;
    { ObjectLock lock(m_lock); hr = CopyMatrix(m_transform, matrix, count); }
    return FAILED(hr) ? hr : m_viewport->ContentChanged();
}

class CUpdateManager : public CComObjectRootEx<CComMultiThreadModel>, public IDirectManipulationUpdateManager,
                       private ModuleObject
{
public:
    BEGIN_COM_MAP(CUpdateManager)
        COM_INTERFACE_ENTRY_IID(IID_IDirectManipulationUpdateManager, IDirectManipulationUpdateManager)
    END_COM_MAP()
    STDMETHODIMP RegisterWaitHandleCallback(HANDLE, IDirectManipulationUpdateHandler *, DWORD *cookie) override
    { if (cookie) *cookie = 0; return E_NOTIMPL; }
    STDMETHODIMP UnregisterWaitHandleCallback(DWORD) override { return E_INVALIDARG; }
    STDMETHODIMP Update(IDirectManipulationFrameInfoProvider *provider) override
    {
        if (!provider) return S_OK;
        ULONGLONG time, process, composition;
        return provider->GetNextFrameInfo(&time, &process, &composition);
    }
};

class CManager : public CComObjectRootEx<CComMultiThreadModel>, public IDirectManipulationManager2,
                 private ModuleObject
{
    CComAutoCriticalSection m_lock;
    CSimpleArray<HWND> m_activeWindows;
    CComPtr<IDirectManipulationUpdateManager> m_update;
public:
    BEGIN_COM_MAP(CManager)
        COM_INTERFACE_ENTRY_IID(IID_IDirectManipulationManager, IDirectManipulationManager)
        COM_INTERFACE_ENTRY_IID(IID_IDirectManipulationManager2, IDirectManipulationManager2)
    END_COM_MAP()
    STDMETHODIMP Activate(HWND window) override
    {
        if (!IsWindow(window)) return E_INVALIDARG;
        ObjectLock lock(m_lock);
        for (int i = 0; i < m_activeWindows.GetSize(); ++i)
            if (m_activeWindows[i] == window) return S_OK;
        return m_activeWindows.Add(window) ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP Deactivate(HWND window) override
    {
        ObjectLock lock(m_lock);
        m_activeWindows.Remove(window);
        return S_OK;
    }
    STDMETHODIMP RegisterHitTestTarget(HWND, HWND, DIRECTMANIPULATION_HITTEST_TYPE) override { return E_NOTIMPL; }
    STDMETHODIMP ProcessInput(const MSG *message, BOOL *handled) override
    {
        if (!handled) return E_POINTER;
        *handled = FALSE;
        if (!message) return E_INVALIDARG;
        return S_OK;
    }
    STDMETHODIMP GetUpdateManager(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = NULL;
        ObjectLock lock(m_lock);
        if (!m_update)
        {
            CComObject<CUpdateManager> *manager;
            HRESULT hr = CComObject<CUpdateManager>::CreateInstance(&manager);
            if (FAILED(hr)) return hr;
            m_update = manager;
        }
        return m_update->QueryInterface(iid, out);
    }
    STDMETHODIMP CreateViewport(IDirectManipulationFrameInfoProvider *provider, HWND window,
                                REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = NULL;
        if (!IsWindow(window)) return E_INVALIDARG;
        if (provider) return E_NOTIMPL;
        CComObject<CViewport> *viewport;
        HRESULT hr = CComObject<CViewport>::CreateInstance(&viewport);
        if (FAILED(hr)) return hr;
        viewport->AddRef();
        hr = viewport->QueryInterface(iid, out);
        viewport->Release();
        return hr;
    }
    STDMETHODIMP CreateContent(IDirectManipulationFrameInfoProvider *, REFCLSID, REFIID, void **out) override
    { if (!out) return E_POINTER; *out = NULL; return E_NOTIMPL; }
    STDMETHODIMP CreateBehavior(REFCLSID, REFIID, void **out) override
    { if (!out) return E_POINTER; *out = NULL; return CLASS_E_CLASSNOTAVAILABLE; }
};

class CFactory : public CComObjectRootEx<CComMultiThreadModel>, public IClassFactory, private ModuleObject
{
public:
    BEGIN_COM_MAP(CFactory)
        COM_INTERFACE_ENTRY_IID(IID_IClassFactory, IClassFactory)
    END_COM_MAP()
    STDMETHODIMP CreateInstance(IUnknown *outer, REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = NULL;
        if (outer) return CLASS_E_NOAGGREGATION;
        CComObject<CManager> *manager;
        HRESULT hr = CComObject<CManager>::CreateInstance(&manager);
        if (FAILED(hr)) return hr;
        manager->AddRef();
        hr = manager->QueryInterface(iid, out);
        manager->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) InterlockedIncrement(&serverLocks);
        else InterlockedDecrement(&serverLocks);
        return S_OK;
    }
};

EXTERN_C HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (clsid != CLSID_DirectManipulationManager) return CLASS_E_CLASSNOTAVAILABLE;
    CComObject<CFactory> *factory;
    HRESULT hr = CComObject<CFactory>::CreateInstance(&factory);
    if (FAILED(hr)) return hr;
    factory->AddRef();
    hr = factory->QueryInterface(iid, out);
    factory->Release();
    return hr;
}

EXTERN_C HRESULT WINAPI DllCanUnloadNow()
{
    return objectCount || serverLocks ? S_FALSE : S_OK;
}
