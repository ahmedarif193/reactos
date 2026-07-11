/*
 * Native DXGI (dxgkrnl-backed).
 *
 * Replaces the previous "backend unavailable" stub with a real IDXGIFactory
 * whose adapter enumeration is served straight from D3DKMTEnumAdapters2 — no
 * Vulkan, no wined3d — matching how Win11's dxgi.dll fronts the display
 * kernel.  This is the system DXGI slot; DXVK's Vulkan-based dxgi is an
 * app-local translation layer, not the OS infrastructure.
 *
 * Implemented today: factory/adapter object model + enumeration.
 * Deferred (return E_NOTIMPL): swapchains / composition present, which route
 * through the dxgkrnl flip path and land with the WDDM present work.
 */

#include "dxgi_native.h"

/* NT10 / WDDM3.0 — set authoritatively via the CMake compile definition;
 * kept here only so the TU still resolves the WIN8+ thunks if built directly. */
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0xF003 /* DXGKDDI_INTERFACE_VERSION_WDDM3_0 */
#endif
#include <d3dkmthk.h>
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

#ifndef DXGI_ERROR_NOT_FOUND
#define DXGI_ERROR_NOT_FOUND    ((HRESULT)0x887A0002L)
#define DXGI_ERROR_INVALID_CALL ((HRESULT)0x887A0001L)
#define DXGI_ERROR_UNSUPPORTED  ((HRESULT)0x887A0004L)
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(x) ((NTSTATUS)(x) >= 0)
#endif

/* Interface identifiers (public, ABI-fixed). */
static const GUID IID_IUnknown_local        = {0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID IID_IDXGIObject_local     = {0xaec22fb8,0x76f3,0x4639,{0x9b,0xe0,0x28,0xeb,0x43,0xa6,0x7a,0x2e}};
static const GUID IID_IDXGIFactory_local    = {0x7b7166ec,0x21c7,0x44ae,{0xb2,0x1a,0xc9,0xae,0x32,0x1a,0xe3,0x69}};
static const GUID IID_IDXGIFactory1_local   = {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};
static const GUID IID_IDXGIFactory2_local   = {0x50c83a1c,0xe072,0x4c48,{0x87,0xb0,0x36,0x30,0xfa,0x36,0xa6,0xd0}};
static const GUID IID_IDXGIFactory3_local   = {0x25483823,0xcd46,0x4c7d,{0x86,0xca,0x47,0xaa,0x95,0xb8,0x37,0xbd}};
static const GUID IID_IDXGIFactory4_local   = {0x1bc6ea02,0xef36,0x464f,{0xbf,0x0c,0x21,0xca,0x39,0xe5,0x16,0x8a}};
static const GUID IID_IDXGIAdapter_local    = {0x2411e7e1,0x12ac,0x4ccf,{0xbd,0x14,0x97,0x98,0xe8,0x53,0x4d,0xc0}};
static const GUID IID_IDXGIAdapter1_local   = {0x29038f61,0x3839,0x4626,{0x91,0xfd,0x08,0x68,0x79,0x01,0x1a,0x05}};

static inline BOOL guid_eq(REFIID a, const GUID *b) { return IsEqualGUID(a, b); }

/* ===================== IDXGIAdapter1 ===================== */

typedef struct dxgi_adapter {
    IDXGIAdapter1 iface;
    LONG refcount;
    DXGI_ADAPTER_DESC1 desc;
} dxgi_adapter;

static inline dxgi_adapter *adapter_from(IDXGIAdapter1 *iface)
{
    return CONTAINING_RECORD(iface, dxgi_adapter, iface);
}

static HRESULT STDMETHODCALLTYPE adapter_QueryInterface(IDXGIAdapter1 *iface, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (guid_eq(riid, &IID_IUnknown_local) || guid_eq(riid, &IID_IDXGIObject_local) ||
        guid_eq(riid, &IID_IDXGIAdapter_local) || guid_eq(riid, &IID_IDXGIAdapter1_local))
    {
        iface->lpVtbl->AddRef(iface);
        *ppv = iface;
        return S_OK;
    }
    WARN("adapter: no interface %s\n", debugstr_guid(riid));
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE adapter_AddRef(IDXGIAdapter1 *iface)
{
    return InterlockedIncrement(&adapter_from(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE adapter_Release(IDXGIAdapter1 *iface)
{
    dxgi_adapter *a = adapter_from(iface);
    ULONG rc = InterlockedDecrement(&a->refcount);
    if (!rc) HeapFree(GetProcessHeap(), 0, a);
    return rc;
}

static HRESULT STDMETHODCALLTYPE adapter_SetPrivateData(IDXGIAdapter1 *iface, REFGUID g, UINT s, const void *d)
{ return S_OK; }
static HRESULT STDMETHODCALLTYPE adapter_SetPrivateDataInterface(IDXGIAdapter1 *iface, REFGUID g, const IUnknown *o)
{ return S_OK; }
static HRESULT STDMETHODCALLTYPE adapter_GetPrivateData(IDXGIAdapter1 *iface, REFGUID g, UINT *s, void *d)
{ return DXGI_ERROR_NOT_FOUND; }

static HRESULT STDMETHODCALLTYPE adapter_GetParent(IDXGIAdapter1 *iface, REFIID riid, void **parent)
{ if (parent) *parent = NULL; return E_NOINTERFACE; }

static HRESULT STDMETHODCALLTYPE adapter_EnumOutputs(IDXGIAdapter1 *iface, UINT idx, IDXGIOutput **out)
{
    /* Output enumeration lands with the dxgkrnl present path (roadmap M6). */
    if (out) *out = NULL;
    return DXGI_ERROR_NOT_FOUND;
}

static HRESULT STDMETHODCALLTYPE adapter_GetDesc(IDXGIAdapter1 *iface, DXGI_ADAPTER_DESC *desc)
{
    dxgi_adapter *a = adapter_from(iface);
    if (!desc) return E_INVALIDARG;
    CopyMemory(desc, &a->desc, sizeof(*desc)); /* DESC is DESC1 minus trailing Flags */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE adapter_CheckInterfaceSupport(IDXGIAdapter1 *iface, REFGUID guid, LARGE_INTEGER *v)
{ if (v) v->QuadPart = 0; return DXGI_ERROR_UNSUPPORTED; }

static HRESULT STDMETHODCALLTYPE adapter_GetDesc1(IDXGIAdapter1 *iface, DXGI_ADAPTER_DESC1 *desc)
{
    dxgi_adapter *a = adapter_from(iface);
    if (!desc) return E_INVALIDARG;
    *desc = a->desc;
    return S_OK;
}

static const IDXGIAdapter1Vtbl adapter_vtbl = {
    adapter_QueryInterface, adapter_AddRef, adapter_Release,
    adapter_SetPrivateData, adapter_SetPrivateDataInterface, adapter_GetPrivateData,
    adapter_GetParent, adapter_EnumOutputs, adapter_GetDesc,
    adapter_CheckInterfaceSupport, adapter_GetDesc1,
};

static IDXGIAdapter1 *adapter_create(const D3DKMT_ADAPTERINFO *info)
{
    dxgi_adapter *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*a));
    if (!a) return NULL;
    a->iface.lpVtbl = &adapter_vtbl;
    a->refcount = 1;
    /* Minimal, honest descriptor: real LUID from the kernel, generic name,
     * memory sizes filled once D3DKMTQueryAdapterInfo(GETSEGMENTSIZE) is
     * wired (roadmap M6).  Enough for adapter identity/dedup by LUID. */
    lstrcpynW(a->desc.Description, L"ReactOS Display Adapter", 128);
    a->desc.AdapterLuid = info->AdapterLuid;
    a->desc.Flags = DXGI_ADAPTER_FLAG_NONE;
    return &a->iface;
}

/* ===================== IDXGIFactory4 ===================== */

typedef struct dxgi_factory {
    IDXGIFactory4 iface;
    LONG refcount;
    UINT creation_flags;
} dxgi_factory;

static inline dxgi_factory *factory_from(IDXGIFactory4 *iface)
{
    return CONTAINING_RECORD(iface, dxgi_factory, iface);
}

static HRESULT STDMETHODCALLTYPE factory_QueryInterface(IDXGIFactory4 *iface, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (guid_eq(riid, &IID_IUnknown_local) || guid_eq(riid, &IID_IDXGIObject_local) ||
        guid_eq(riid, &IID_IDXGIFactory_local) || guid_eq(riid, &IID_IDXGIFactory1_local) ||
        guid_eq(riid, &IID_IDXGIFactory2_local) || guid_eq(riid, &IID_IDXGIFactory3_local) ||
        guid_eq(riid, &IID_IDXGIFactory4_local))
    {
        iface->lpVtbl->AddRef(iface);
        *ppv = iface;
        return S_OK;
    }
    WARN("factory: no interface %s\n", debugstr_guid(riid));
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_AddRef(IDXGIFactory4 *iface)
{
    return InterlockedIncrement(&factory_from(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE factory_Release(IDXGIFactory4 *iface)
{
    dxgi_factory *f = factory_from(iface);
    ULONG rc = InterlockedDecrement(&f->refcount);
    if (!rc) HeapFree(GetProcessHeap(), 0, f);
    return rc;
}

static HRESULT STDMETHODCALLTYPE factory_SetPrivateData(IDXGIFactory4 *iface, REFGUID g, UINT s, const void *d)
{ return S_OK; }
static HRESULT STDMETHODCALLTYPE factory_SetPrivateDataInterface(IDXGIFactory4 *iface, REFGUID g, const IUnknown *o)
{ return S_OK; }
static HRESULT STDMETHODCALLTYPE factory_GetPrivateData(IDXGIFactory4 *iface, REFGUID g, UINT *s, void *d)
{ return DXGI_ERROR_NOT_FOUND; }
static HRESULT STDMETHODCALLTYPE factory_GetParent(IDXGIFactory4 *iface, REFIID riid, void **parent)
{ if (parent) *parent = NULL; return E_NOINTERFACE; }

/* Enumeration core: D3DKMTEnumAdapters2 -> IDXGIAdapter1 by index. */
static HRESULT factory_enum_index(UINT idx, IDXGIAdapter1 **out)
{
    D3DKMT_ADAPTERINFO info[16];
    D3DKMT_ENUMADAPTERS2 ea;
    NTSTATUS st;

    if (!out) return E_INVALIDARG;
    *out = NULL;

    RtlZeroMemory(&ea, sizeof(ea));
    ea.NumAdapters = ARRAYSIZE(info);
    ea.pAdapters = info;
    st = D3DKMTEnumAdapters2(&ea);
    if (!NT_SUCCESS(st))
    {
        WARN("D3DKMTEnumAdapters2 failed %#lx\n", (long)st);
        return DXGI_ERROR_NOT_FOUND;
    }
    if (idx >= ea.NumAdapters)
        return DXGI_ERROR_NOT_FOUND;

    *out = adapter_create(&info[idx]);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE factory_EnumAdapters1(IDXGIFactory4 *iface, UINT idx, IDXGIAdapter1 **out)
{
    return factory_enum_index(idx, out);
}

static HRESULT STDMETHODCALLTYPE factory_EnumAdapters(IDXGIFactory4 *iface, UINT idx, IDXGIAdapter **out)
{
    /* IDXGIAdapter is a base of IDXGIAdapter1 — the object satisfies both. */
    return factory_enum_index(idx, (IDXGIAdapter1 **)out);
}

static HRESULT STDMETHODCALLTYPE factory_EnumAdapterByLuid(IDXGIFactory4 *iface, LUID luid, REFIID iid, void **out)
{
    UINT i;
    if (!out) return E_INVALIDARG;
    *out = NULL;
    for (i = 0; ; i++)
    {
        IDXGIAdapter1 *a = NULL;
        DXGI_ADAPTER_DESC1 desc;
        HRESULT hr = factory_enum_index(i, &a);
        if (hr != S_OK) break;
        if (SUCCEEDED(a->lpVtbl->GetDesc1(a, &desc)) &&
            desc.AdapterLuid.LowPart == luid.LowPart &&
            desc.AdapterLuid.HighPart == luid.HighPart)
        {
            hr = a->lpVtbl->QueryInterface(a, iid, out);
            a->lpVtbl->Release(a);
            return hr;
        }
        a->lpVtbl->Release(a);
    }
    return DXGI_ERROR_NOT_FOUND;
}

static HRESULT STDMETHODCALLTYPE factory_EnumWarpAdapter(IDXGIFactory4 *iface, REFIID iid, void **out)
{ if (out) *out = NULL; return DXGI_ERROR_NOT_FOUND; }

static HRESULT STDMETHODCALLTYPE factory_MakeWindowAssociation(IDXGIFactory4 *iface, HWND w, UINT f)
{ return S_OK; }
static HRESULT STDMETHODCALLTYPE factory_GetWindowAssociation(IDXGIFactory4 *iface, HWND *w)
{ if (w) *w = NULL; return S_OK; }
static WINBOOL STDMETHODCALLTYPE factory_IsCurrent(IDXGIFactory4 *iface)
{ return TRUE; }
static WINBOOL STDMETHODCALLTYPE factory_IsWindowedStereoEnabled(IDXGIFactory4 *iface)
{ return FALSE; }
static HRESULT STDMETHODCALLTYPE factory_GetSharedResourceAdapterLuid(IDXGIFactory4 *iface, HANDLE r, LUID *l)
{ return DXGI_ERROR_INVALID_CALL; }
static HRESULT STDMETHODCALLTYPE factory_RegisterStereoStatusWindow(IDXGIFactory4 *iface, HWND w, UINT m, DWORD *c)
{ if (c) *c = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE factory_RegisterStereoStatusEvent(IDXGIFactory4 *iface, HANDLE e, DWORD *c)
{ if (c) *c = 0; return S_OK; }
static void STDMETHODCALLTYPE factory_UnregisterStereoStatus(IDXGIFactory4 *iface, DWORD c) {}
static HRESULT STDMETHODCALLTYPE factory_RegisterOcclusionStatusWindow(IDXGIFactory4 *iface, HWND w, UINT m, DWORD *c)
{ if (c) *c = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE factory_RegisterOcclusionStatusEvent(IDXGIFactory4 *iface, HANDLE e, DWORD *c)
{ if (c) *c = 0; return S_OK; }
static void STDMETHODCALLTYPE factory_UnregisterOcclusionStatus(IDXGIFactory4 *iface, DWORD c) {}
static UINT STDMETHODCALLTYPE factory_GetCreationFlags(IDXGIFactory4 *iface)
{ return factory_from(iface)->creation_flags; }

/* Swapchain / composition present — dxgkrnl flip path, roadmap M6. */
static HRESULT STDMETHODCALLTYPE factory_CreateSwapChain(IDXGIFactory4 *iface, IUnknown *d, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **sc)
{ FIXME("swapchain present not yet implemented\n"); if (sc) *sc = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE factory_CreateSoftwareAdapter(IDXGIFactory4 *iface, HMODULE m, IDXGIAdapter **a)
{ if (a) *a = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE factory_CreateSwapChainForHwnd(IDXGIFactory4 *iface, IUnknown *d, HWND h, const DXGI_SWAP_CHAIN_DESC1 *desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fs, IDXGIOutput *ro, IDXGISwapChain1 **sc)
{ FIXME("swapchain present not yet implemented\n"); if (sc) *sc = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE factory_CreateSwapChainForCoreWindow(IDXGIFactory4 *iface, IUnknown *d, IUnknown *w, const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *ro, IDXGISwapChain1 **sc)
{ if (sc) *sc = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE factory_CreateSwapChainForComposition(IDXGIFactory4 *iface, IUnknown *d, const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *ro, IDXGISwapChain1 **sc)
{ FIXME("composition swapchain not yet implemented\n"); if (sc) *sc = NULL; return E_NOTIMPL; }

static const IDXGIFactory4Vtbl factory_vtbl = {
    factory_QueryInterface, factory_AddRef, factory_Release,
    factory_SetPrivateData, factory_SetPrivateDataInterface, factory_GetPrivateData, factory_GetParent,
    factory_EnumAdapters, factory_MakeWindowAssociation, factory_GetWindowAssociation,
    factory_CreateSwapChain, factory_CreateSoftwareAdapter,
    factory_EnumAdapters1, factory_IsCurrent,
    factory_IsWindowedStereoEnabled, factory_CreateSwapChainForHwnd, factory_CreateSwapChainForCoreWindow,
    factory_GetSharedResourceAdapterLuid, factory_RegisterStereoStatusWindow, factory_RegisterStereoStatusEvent,
    factory_UnregisterStereoStatus, factory_RegisterOcclusionStatusWindow, factory_RegisterOcclusionStatusEvent,
    factory_UnregisterOcclusionStatus, factory_CreateSwapChainForComposition,
    factory_GetCreationFlags, factory_EnumAdapterByLuid, factory_EnumWarpAdapter,
};

static HRESULT dxgi_create_factory(UINT flags, REFIID riid, void **out)
{
    dxgi_factory *f;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;

    f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f) return E_OUTOFMEMORY;
    f->iface.lpVtbl = &factory_vtbl;
    f->refcount = 1;
    f->creation_flags = flags;

    hr = factory_QueryInterface(&f->iface, riid, out);
    factory_Release(&f->iface);
    return hr;
}

HRESULT WINAPI CreateDXGIFactory(REFIID riid, void **factory)
{
    TRACE("riid %s, factory %p\n", debugstr_guid(riid), factory);
    return dxgi_create_factory(0, riid, factory);
}

HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **factory)
{
    TRACE("riid %s, factory %p\n", debugstr_guid(riid), factory);
    return dxgi_create_factory(0, riid, factory);
}

HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void **factory)
{
    TRACE("flags %#x, riid %s, factory %p\n", flags, debugstr_guid(riid), factory);
    return dxgi_create_factory(flags, riid, factory);
}

HRESULT WINAPI DXGID3D10CreateDevice(void *factory, void *adapter, void *unknown,
        UINT flags, void *device_layer, UINT layer_count, void **device)
{
    FIXME("factory %p, adapter %p: D3D10 device creation not implemented.\n", factory, adapter);
    if (device) *device = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI DXGID3D10RegisterLayers(void *layers, UINT layer_count)
{
    FIXME("layers %p, layer_count %u: stub.\n", layers, layer_count);
    return S_OK;
}

HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID riid, void **debug)
{
    TRACE("flags %#x, riid %s, debug %p\n", flags, debugstr_guid(riid), debug);
    if (debug) *debug = NULL;
    return E_NOINTERFACE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        WCHAR exe[260];
        exe[0] = 0;
        GetModuleFileNameW(NULL, exe, 260);
        ERR("dxgi.dll (native) loaded into %s\n", debugstr_w(exe));
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
