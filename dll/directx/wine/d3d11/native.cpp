/*
 * PROJECT:     ReactOS Direct3D 11 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 user-mode display driver backend
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <d3dkmthk.h>
#include <d3d10umddi.h>
#include <drivers/directx/umd_adapter.h>
#include <wine/winedxgi.h>
#include <dwmframe.h>
#include <dxgi_dcomp.h>
#include <vkd3d_shader.h>
#include <wine/debug.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "native_wddm20.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

extern "C" DWORD_PTR NTAPI NtUserCallOneParam(DWORD_PTR, DWORD);

static_assert(sizeof(D3D11DDI_DEVICEFUNCS) == 150 * sizeof(void *), "D3D11 device DDI layout");
#ifdef _WIN64
static_assert(offsetof(D3D10DDIARG_CREATEDEVICE, Flags) == 72, "CreateDevice flags layout");
#endif

static HRESULT StatusToHresult(NTSTATUS status)
{
    return status >= 0 ? S_OK : HRESULT_FROM_NT(status);
}

class NativeAllocation
{
public:
    static void *operator new(size_t size) noexcept { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size); }
    static void operator delete(void *ptr) noexcept { HeapFree(GetProcessHeap(), 0, ptr); }
};

class NativePrivateData
{
    struct Entry
    {
        Entry *next;
        GUID guid;
        UINT size;
        IUnknown *object;
        BYTE data[1];
    };
    Entry *head = NULL;
public:
    ~NativePrivateData() { while (head) Set(head->guid, 0, NULL, NULL); }
    HRESULT Get(REFGUID guid, UINT *size, void *data)
    {
        if (!size) return E_INVALIDARG;
        for (Entry *e = head; e; e = e->next)
        {
            if (!IsEqualGUID(guid, e->guid)) continue;
            UINT capacity = *size;
            *size = e->size;
            if (!data) return S_OK;
            if (capacity < e->size) return DXGI_ERROR_MORE_DATA;
            memcpy(data, e->data, e->size);
            if (e->object) e->object->AddRef();
            return S_OK;
        }
        *size = 0;
        return DXGI_ERROR_NOT_FOUND;
    }
    HRESULT Set(REFGUID guid, UINT size, const void *data, IUnknown *object)
    {
        /* NULL data removes an entry regardless of the supplied byte count.
         * An interface setter supplies &object even when object is NULL. */
        Entry *replacement = NULL;
        if (data)
        {
            SIZE_T bytes = size;
            if (bytes + offsetof(Entry, data) < bytes) return E_OUTOFMEMORY;
            replacement = static_cast<Entry *>(HeapAlloc(GetProcessHeap(), 0, offsetof(Entry, data) + size));
            if (!replacement) return E_OUTOFMEMORY;
            replacement->guid = guid;
            replacement->size = size;
            replacement->object = object;
            memcpy(replacement->data, data, size);
            if (object) object->AddRef();
        }
        Entry **link = &head;
        while (*link && !IsEqualGUID(guid, (*link)->guid)) link = &(*link)->next;
        Entry *old = *link;
        if (replacement)
        {
            replacement->next = old ? old->next : NULL;
            *link = replacement;
        }
        else if (old) *link = old->next;
        if (old)
        {
            if (old->object) old->object->Release();
            HeapFree(GetProcessHeap(), 0, old);
        }
        return replacement || old ? S_OK : S_FALSE;
    }
};

class NativeDevice;
class NativeContext;
class NativeDeviceContextState;
class NativeTexture2D;
class NativeRenderTargetView;
class NativeSwapChain;
class NativeShaderResourceView;

#include "native_commands.h"

static UINT NativeVertexFormatSize(DXGI_FORMAT format);
static UINT NativeBufferFormatSize(DXGI_FORMAT format);
static bool NativeUploadFormat(DXGI_FORMAT, UINT, UINT, UINT *, UINT *);
static void NativeRetainResource(ID3D11Resource *resource);
static void NativeDropResource(ID3D11Resource *resource);

static DXGI_FORMAT NativeDepthResourceFormat(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
        case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24G8_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

struct NativeSharedTextureData
{
    UINT signature;
    UINT version;
    D3D11_TEXTURE2D_DESC desc;
};

static const UINT native_shared_texture_signature = 0x54313144;
static HRESULT APIENTRY NativePresent(HANDLE, DXGIDDICB_PRESENT *);

/* Weak entries preserve COM identity while an object is alive, including
 * private context bindings. Final child destruction and lookups share lock. */
struct NativeStateCacheEntry
{
    NativeStateCacheEntry *next = NULL, **previous = NULL;
    IUnknown *object = NULL;
    const void *description = NULL;
    SIZE_T size = 0;
    ~NativeStateCacheEntry()
    {
        if (!previous) return;
        *previous = next;
        if (next) next->previous = previous;
    }
    void Insert(NativeStateCacheEntry **head, IUnknown *value, const void *desc, SIZE_T bytes)
    {
        next = *head;
        if (next) next->previous = &next;
        previous = head;
        *head = this;
        object = value;
        description = desc;
        size = bytes;
    }
};

enum NativeStateKind { NativeSamplerKind, NativeBlendKind, NativeDepthKind, NativeRasterizerKind };

class NativeDevice final : public ID3D11Device1, public IDXGIDevice2, public ID3D11Multithread,
        public IWineDXGISwapChainFactory, public NativeAllocation
{
public:
    LONG references = 1;
    LONG total_references = 1;
    LONG children = 0;
    LONG multithread_protected = FALSE;
    bool clearing = false;
    IDXGIAdapter *adapter = NULL;
    D3DKMT_HANDLE km_adapter = 0;
    D3DKMT_HANDLE km_device = 0;
    HANDLE runtime_device = NULL;
    HMODULE umd = NULL;
    HMODULE runtime = NULL;
    HRESULT (WINAPI *destroy_callbacks)(HANDLE) = NULL;
    HRESULT (WINAPI *register_resource)(HANDLE, HANDLE, D3DKMT_CREATEALLOCATIONFLAGS, const void *, UINT) = NULL;
    HRESULT (WINAPI *get_resource_handles)(HANDLE, HANDLE, D3DKMT_HANDLE *, D3DKMT_HANDLE *) = NULL;
    HRESULT (WINAPI *get_single_allocation)(HANDLE, HANDLE, D3DKMT_HANDLE *) = NULL;
    HRESULT (WINAPI *adopt_resource)(HANDLE, HANDLE, D3DKMT_HANDLE, D3DKMT_HANDLE) = NULL;
    HRESULT (WINAPI *release_resource)(HANDLE, HANDLE) = NULL;
    HRESULT (WINAPI *rotate_resources)(HANDLE, const HANDLE *, UINT) = NULL;
    HRESULT (WINAPI *enqueue_event)(HANDLE, HANDLE) = NULL;
    D3D10DDI_HADAPTER driver_adapter = {};
    D3D10DDI_HDEVICE driver_device = {};
    D3D10_2DDI_ADAPTERFUNCS adapter_functions = {};
    D3D11DDI_DEVICEFUNCS functions = {};
    D3DWDDM2_0DDI_DEVICEFUNCS wddm20_functions = {};
    D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS wddm20_callbacks = {};
    DXGI1_4_DDI_BASE_FUNCTIONS wddm20_dxgi = {};
    PFND3D10DDI_RETRIEVESUBOBJECT retrieve_subobject = NULL;
    void *driver_device_storage = NULL;
    bool wddm20 = false;
    ROS_UMD_ADAPTER_CALLBACKS adapter_callbacks = {};
    D3DDDI_DEVICECALLBACKS kernel_callbacks = {};
    D3D11DDI_CORELAYER_DEVICECALLBACKS core_callbacks = {};
    DXGI_DDI_BASE_CALLBACKS dxgi_callbacks = {};
    DXGI1_1_DDI_BASE_FUNCTIONS dxgi_functions = {};
    NativePrivateData private_data;
    NativeContext *context = NULL;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    UINT threading_caps = 0;
    UINT shader_caps = 0;
    UINT flags = 0;
    UINT exception_mode = 0;
    HRESULT operation_error = S_OK;
    HRESULT removed_reason = S_OK;
    CRITICAL_SECTION lock;
    bool initialized = false;
    bool driver_created = false;
    ULONG_PTR resource_sequence = 0;
    NativeStateCacheEntry *state_cache[4][64] = {};
    D3D10DDI_HBLENDSTATE default_blend = {};
    D3D10DDI_HDEPTHSTENCILSTATE default_depth = {};
    D3D10DDI_HRASTERIZERSTATE default_rasterizer = {};

    D3D10DDI_HRTRESOURCE AllocateResourceIdentity()
    {
        /* Called with lock held. The UMD may deallocate a resource after its
         * COM object has been destroyed. Never reuse that object's address as
         * the callback cookie, or delayed callbacks can target a new resource.
         * Exhaustion must fail rather than wrap into a still-live identity. */
        D3D10DDI_HRTRESOURCE identity = {};
        if (resource_sequence != ~(ULONG_PTR)0)
            identity.handle = reinterpret_cast<HANDLE>(++resource_sequence);
        return identity;
    }

    NativeDevice() { InitializeCriticalSection(&lock); }
    ~NativeDevice();
    HRESULT Initialize(IDXGIAdapter *, UINT, const D3D_FEATURE_LEVEL *, UINT);
    HRESULT InitializeDefaultStates();
    HRESULT CreateTexture(const D3D11_TEXTURE2D_DESC *, const D3D11_SUBRESOURCE_DATA *, ID3D11Texture2D **,
            bool present = false, const DXGI_DDI_PRIMARY_DESC *primary = NULL);
    HRESULT STDMETHODCALLTYPE create_swapchain(IDXGIFactory *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
            const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **) override;
    void Retain() { InterlockedIncrement(&total_references); }
    void Drop() { if (!InterlockedDecrement(&total_references)) delete this; }
    void ChildAddRef() { Retain(); InterlockedIncrement(&children); }
    void ChildRelease() { InterlockedDecrement(&children); MaybeDestroy(); Drop(); }
    void MaybeDestroy();
    void Unimplemented(const char *method) { FIXME("Native D3D11 %s is not implemented.\n", method); operation_error = E_NOTIMPL; }
    void BeginCall() { operation_error = S_OK; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { Retain(); return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override;
    void STDMETHODCALLTYPE Enter() override { EnterCriticalSection(&lock); }
    void STDMETHODCALLTYPE Leave() override { LeaveCriticalSection(&lock); }
    BOOL STDMETHODCALLTYPE SetMultithreadProtected(BOOL enable) override
    {
        /* Native calls also take this lock to protect shared UMD bookkeeping.
         * Keep that internal protection when the caller disables its optional
         * context protection; Enter/Leave still serialize a whole call group. */
        return InterlockedExchange(&multithread_protected, !!enable);
    }
    BOOL STDMETHODCALLTYPE GetMultithreadProtected() override
    {
        return InterlockedCompareExchange(&multithread_protected, FALSE, FALSE);
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID iid, void **out) override { return adapter->QueryInterface(iid, out); }
    HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = adapter;
        adapter->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateSurface(const DXGI_SURFACE_DESC *, UINT, DXGI_USAGE, const DXGI_SHARED_RESOURCE *, IDXGISurface **out) override { if (out) *out = NULL; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryResourceResidency(IUnknown *const *, DXGI_RESIDENCY *, UINT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT priority) override;
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT *priority) override;
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT latency) override;
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *latency) override;
    HRESULT STDMETHODCALLTYPE OfferResources(UINT, IDXGIResource *const *, DXGI_OFFER_RESOURCE_PRIORITY) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ReclaimResources(UINT, IDXGIResource *const *, BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnqueueSetEvent(HANDLE event) override;
    HRESULT STDMETHODCALLTYPE CreateBuffer(const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Buffer **ppBuffer) override;
    HRESULT STDMETHODCALLTYPE CreateTexture1D(const D3D11_TEXTURE1D_DESC *, const D3D11_SUBRESOURCE_DATA *, ID3D11Texture1D **) override;
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D11_TEXTURE2D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D) override;
    HRESULT STDMETHODCALLTYPE CreateTexture3D(const D3D11_TEXTURE3D_DESC *, const D3D11_SUBRESOURCE_DATA *, ID3D11Texture3D **) override;
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc, ID3D11ShaderResourceView **ppSRView) override;
    HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc, ID3D11UnorderedAccessView **ppUAView) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC *pDesc, ID3D11RenderTargetView **ppRTView) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D11DepthStencilView **ppDepthStencilView) override;
    HRESULT STDMETHODCALLTYPE CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout **ppInputLayout) override;
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11VertexShader **ppVertexShader) override;
    HRESULT STDMETHODCALLTYPE CreateGeometryShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader) override;
    HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(const void *pShaderBytecode, SIZE_T BytecodeLength, const D3D11_SO_DECLARATION_ENTRY *pSODeclaration, UINT NumEntries, const UINT *pBufferStrides, UINT NumStrides, UINT RasterizedStream, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader) override;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11PixelShader **ppPixelShader) override;
    HRESULT STDMETHODCALLTYPE CreateHullShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11HullShader **ppHullShader) override;
    HRESULT STDMETHODCALLTYPE CreateDomainShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11DomainShader **ppDomainShader) override;
    HRESULT STDMETHODCALLTYPE CreateComputeShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11ComputeShader **ppComputeShader) override;
    HRESULT STDMETHODCALLTYPE CreateClassLinkage(ID3D11ClassLinkage **ppLinkage) override { if (ppLinkage) *ppLinkage = NULL; Unimplemented("CreateClassLinkage"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateBlendState(const D3D11_BLEND_DESC *pBlendStateDesc, ID3D11BlendState **ppBlendState) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc, ID3D11DepthStencilState **ppDepthStencilState) override;
    HRESULT STDMETHODCALLTYPE CreateRasterizerState(const D3D11_RASTERIZER_DESC *pRasterizerDesc, ID3D11RasterizerState **ppRasterizerState) override;
    HRESULT STDMETHODCALLTYPE CreateSamplerState(const D3D11_SAMPLER_DESC *pSamplerDesc, ID3D11SamplerState **ppSamplerState) override;
    HRESULT STDMETHODCALLTYPE CreateQuery(const D3D11_QUERY_DESC *pQueryDesc, ID3D11Query **ppQuery) override;
    HRESULT STDMETHODCALLTYPE CreatePredicate(const D3D11_QUERY_DESC *, ID3D11Predicate **) override;
    HRESULT STDMETHODCALLTYPE CreateCounter(const D3D11_COUNTER_DESC *pCounterDesc, ID3D11Counter **ppCounter) override { if (ppCounter) *ppCounter = NULL; Unimplemented("CreateCounter"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateDeferredContext(UINT ContextFlags, ID3D11DeviceContext **ppDeferredContext) override;
    HRESULT STDMETHODCALLTYPE OpenSharedResource(HANDLE hResource, REFIID ReturnedInterface, void **ppResource) override;
    HRESULT STDMETHODCALLTYPE CheckFormatSupport(DXGI_FORMAT Format, UINT *pFormatSupport) override;
    HRESULT ValidateTextureBindings(DXGI_FORMAT format, UINT bind, D3D10DDIRESOURCE_TYPE dimension);
    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(DXGI_FORMAT Format, UINT SampleCount, UINT *pNumQualityLevels) override;
    void STDMETHODCALLTYPE CheckCounterInfo(D3D11_COUNTER_INFO *pCounterInfo) override { Unimplemented("CheckCounterInfo"); }
    HRESULT STDMETHODCALLTYPE CheckCounter(const D3D11_COUNTER_DESC *pDesc, D3D11_COUNTER_TYPE *pType, UINT *pActiveCounters, LPSTR szName, UINT *pNameLength, LPSTR szUnits, UINT *pUnitsLength, LPSTR szDescription, UINT *pDescriptionLength) override { Unimplemented("CheckCounter"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D11_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override;
    D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel() override;
    UINT STDMETHODCALLTYPE GetCreationFlags() override;
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override;
    void STDMETHODCALLTYPE GetImmediateContext(ID3D11DeviceContext **ppImmediateContext) override;
    HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT RaiseFlags) override;
    UINT STDMETHODCALLTYPE GetExceptionMode() override;
    void STDMETHODCALLTYPE GetImmediateContext1(ID3D11DeviceContext1 **ppImmediateContext) override;
    HRESULT STDMETHODCALLTYPE CreateDeferredContext1(UINT ContextFlags, ID3D11DeviceContext1 **ppDeferredContext) override;
    HRESULT STDMETHODCALLTYPE CreateBlendState1(const D3D11_BLEND_DESC1 *pBlendStateDesc, ID3D11BlendState1 **ppBlendState) override;
    HRESULT STDMETHODCALLTYPE CreateRasterizerState1(const D3D11_RASTERIZER_DESC1 *pRasterizerDesc, ID3D11RasterizerState1 **ppRasterizerState) override;
    HRESULT STDMETHODCALLTYPE CreateDeviceContextState(UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels,
            UINT FeatureLevels, UINT SDKVersion, REFIID EmulatedInterface,
            D3D_FEATURE_LEVEL *pChosenFeatureLevel, ID3DDeviceContextState **ppContextState) override;
    HRESULT STDMETHODCALLTYPE OpenSharedResource1(HANDLE hResource, REFIID ReturnedInterface, void **ppResource) override;
    HRESULT STDMETHODCALLTYPE OpenSharedResourceByName(LPCWSTR lpName, DWORD dwDesiredAccess,
            REFIID ReturnedInterface, void **ppResource) override;
};

class NativeLock
{
    NativeDevice *device;
public:
    explicit NativeLock(NativeDevice *d) : device(d) { EnterCriticalSection(&device->lock); }
    ~NativeLock() { LeaveCriticalSection(&device->lock); }
};

template<class Desc>
static NativeStateCacheEntry **NativeStateBucket(NativeDevice *device, NativeStateKind kind, const Desc &desc)
{
    const BYTE *bytes = reinterpret_cast<const BYTE *>(&desc);
    UINT hash = 2166136261u;
    for (SIZE_T i = 0; i < sizeof(desc); ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return &device->state_cache[kind][hash % 64];
}

template<class Interface, class Desc>
static Interface *NativeFindState(NativeDevice *device, NativeStateKind kind, const Desc &desc)
{
    for (NativeStateCacheEntry *entry = *NativeStateBucket(device, kind, desc); entry; entry = entry->next)
        if (entry->size == sizeof(desc) && !memcmp(entry->description, &desc, sizeof(desc)))
        {
            entry->object->AddRef();
            return static_cast<Interface *>(entry->object);
        }
    return NULL;
}

class NativeContext final : public ID3D11DeviceContext1, public ID3D11Multithread,
        public ID3DUserDefinedAnnotation, public NativeAllocation
{
public:
    NativeDevice *device;
    LONG references = 0;
    bool deferred = false;
    bool recording_disabled = false;
    HRESULT recording_error = S_OK;
    NativeRecordedCommand *commands = NULL, **command_tail = &commands;
    NativeDeferredMapping *mappings = NULL;
    NativeDeviceContextState *active_state = NULL;
    template<class F> void Record(const F &call)
    {
        if (recording_disabled || FAILED(recording_error)) return;
        NativeRecordedCommand *command = new NativeRecordedCall<F>(call);
        if (!command) { recording_error = E_OUTOFMEMORY; return; }
        *command_tail = command;
        command_tail = &command->next;
    }
    template<class T> bool Captured(const NativeCommandArray<T> &values)
    {
        if (values.Valid()) return true;
        recording_error = E_OUTOFMEMORY;
        return false;
    }
    void CopyStateTo(NativeContext *destination);
    void ClearLocalState();
    void ClearMappings();
    void ReleaseContextState();
    HRESULT MapDeferred(ID3D11Resource *, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE *);
    void UnmapDeferred(ID3D11Resource *, UINT);
    void UpdateDeferred(ID3D11Resource *, UINT, const D3D11_BOX *, const void *, UINT, UINT);
    NativePrivateData private_data;
    ID3D11VertexShader *vertex_shader = NULL;
    ID3D11PixelShader *pixel_shader = NULL;
    ID3D11GeometryShader *geometry_shader = NULL;
    ID3D11HullShader *hull_shader = NULL;
    ID3D11DomainShader *domain_shader = NULL;
    ID3D11ComputeShader *compute_shader = NULL;
    ID3D11ShaderResourceView *shader_resources[6][128] = {};
    ID3D11SamplerState *samplers[6][16] = {};
    ID3D11Buffer *constant_buffers[6][14] = {};
    ID3D11RenderTargetView *render_targets[8] = {};
    ID3D11DepthStencilView *depth_view = NULL;
    ID3D11UnorderedAccessView *compute_uavs[8] = {}, *pixel_uavs[8] = {};
    UINT render_target_count = 0;
    ID3D11Buffer *stream_outputs[4] = {};
    void RefreshStreamOutput();
    void RefreshOutputBindings(const UINT *initial = NULL);
    ID3D11InputLayout *input_layout = NULL;
    ID3D11Buffer *vertex_buffers[32] = {};
    UINT vertex_strides[32] = {}, vertex_offsets[32] = {};
    ID3D11Buffer *index_buffer = NULL;
    DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
    UINT index_offset = 0;
    ID3D11BlendState *blend_state = NULL;
    ID3D11RasterizerState *rasterizer_state = NULL;
    ID3D11DepthStencilState *depth_stencil_state = NULL;
    ID3D11Predicate *predicate = NULL;
    BOOL predicate_value = FALSE;
    FLOAT blend_factor[4] = {1, 1, 1, 1};
    UINT sample_mask = ~0u;
    UINT stencil_ref = 0;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    D3D11_VIEWPORT viewports[16] = {};
    D3D11_RECT scissors[16] = {};
    UINT viewport_count = 0, scissor_count = 0;
    void SetConstantBuffers(UINT stage, UINT start, UINT count, ID3D11Buffer *const *buffers);
    void SetSamplers(UINT stage, UINT start, UINT count, ID3D11SamplerState *const *states);
    void SetShaderResources(UINT stage, UINT start, UINT count, ID3D11ShaderResourceView *const *views);
    explicit NativeContext(NativeDevice *d, bool recording = false) : device(d), deferred(recording) {}
    ~NativeContext();
    void STDMETHODCALLTYPE GetDevice(ID3D11Device **out) override { if (out) { *out = device; device->AddRef(); } }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *size, void *data) override { NativeLock guard(device); return private_data.Get(guid, size, data); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void *data) override { NativeLock guard(device); return private_data.Set(guid, size, data, NULL); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *object) override { NativeLock guard(device); return private_data.Set(guid, sizeof(object), &object, const_cast<IUnknown *>(object)); }

    void Unimplemented(const char *method)
    {
        if (deferred) { FIXME("Native deferred D3D11 %s is not implemented.\n", method); recording_error = E_NOTIMPL; }
        else device->Unimplemented(method);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    void STDMETHODCALLTYPE Enter() override { device->Enter(); }
    void STDMETHODCALLTYPE Leave() override { device->Leave(); }
    BOOL STDMETHODCALLTYPE SetMultithreadProtected(BOOL enable) override { return device->SetMultithreadProtected(enable); }
    BOOL STDMETHODCALLTYPE GetMultithreadProtected() override { return device->GetMultithreadProtected(); }
    /* No annotation consumer is attached to this runtime. The interface stays
     * available, with the documented inactive-capture return values. */
    INT STDMETHODCALLTYPE BeginEvent(LPCWSTR) override { return -1; }
    INT STDMETHODCALLTYPE EndEvent() override { return -1; }
    void STDMETHODCALLTYPE SetMarker(LPCWSTR) override {}
    BOOL STDMETHODCALLTYPE GetStatus() override { return FALSE; }
    void STDMETHODCALLTYPE VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override;
    void STDMETHODCALLTYPE PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override;
    void STDMETHODCALLTYPE PSSetShader(ID3D11PixelShader *pPixelShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override;
    void STDMETHODCALLTYPE PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override;
    void STDMETHODCALLTYPE VSSetShader(ID3D11VertexShader *pVertexShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override;
    void STDMETHODCALLTYPE DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) override;
    void STDMETHODCALLTYPE Draw(UINT VertexCount, UINT StartVertexLocation) override;
    HRESULT STDMETHODCALLTYPE Map(ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE *pMappedResource) override;
    void STDMETHODCALLTYPE Unmap(ID3D11Resource *pResource, UINT Subresource) override;
    void STDMETHODCALLTYPE PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override;
    void STDMETHODCALLTYPE IASetInputLayout(ID3D11InputLayout *pInputLayout) override;
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets) override;
    void STDMETHODCALLTYPE IASetIndexBuffer(ID3D11Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset) override;
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) override;
    void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) override;
    void STDMETHODCALLTYPE GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override;
    void STDMETHODCALLTYPE GSSetShader(ID3D11GeometryShader *pShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override;
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology) override;
    void STDMETHODCALLTYPE VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override;
    void STDMETHODCALLTYPE VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override;
    void STDMETHODCALLTYPE Begin(ID3D11Asynchronous *pAsync) override;
    void STDMETHODCALLTYPE End(ID3D11Asynchronous *pAsync) override;
    HRESULT STDMETHODCALLTYPE GetData(ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags) override;
    void STDMETHODCALLTYPE SetPredication(ID3D11Predicate *, BOOL) override;
    void STDMETHODCALLTYPE GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override;
    void STDMETHODCALLTYPE GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override;
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView) override;
    void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts) override;
    void STDMETHODCALLTYPE OMSetBlendState(ID3D11BlendState *pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) override;
    void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D11DepthStencilState *pDepthStencilState, UINT StencilRef) override;
    void STDMETHODCALLTYPE SOSetTargets(UINT NumBuffers, ID3D11Buffer *const *ppSOTargets, const UINT *pOffsets) override;
    void STDMETHODCALLTYPE DrawAuto() override;
    void STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override;
    void STDMETHODCALLTYPE DispatchIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE RSSetState(ID3D11RasterizerState *pRasterizerState) override;
    void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT *pViewports) override;
    void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D11_RECT *pRects) override;
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox) override;
    void STDMETHODCALLTYPE CopyResource(ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource) override;
    void STDMETHODCALLTYPE UpdateSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) override;
    void STDMETHODCALLTYPE CopyStructureCount(ID3D11Buffer *pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView *pSrcView) override;
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4]) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView *pUnorderedAccessView, const UINT Values[4]) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView *pUnorderedAccessView, const FLOAT Values[4]) override;
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) override;
    void STDMETHODCALLTYPE GenerateMips(ID3D11ShaderResourceView *pShaderResourceView) override;
    void STDMETHODCALLTYPE SetResourceMinLOD(ID3D11Resource *pResource, FLOAT MinLOD) override;
    FLOAT STDMETHODCALLTYPE GetResourceMinLOD(ID3D11Resource *pResource) override;
    void STDMETHODCALLTYPE ResolveSubresource(ID3D11Resource *, UINT, ID3D11Resource *, UINT, DXGI_FORMAT) override;
    void STDMETHODCALLTYPE ExecuteCommandList(ID3D11CommandList *pCommandList, BOOL RestoreContextState) override;
    void STDMETHODCALLTYPE HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override;
    void STDMETHODCALLTYPE HSSetShader(ID3D11HullShader *pHullShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override;
    void STDMETHODCALLTYPE HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override;
    void STDMETHODCALLTYPE HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override;
    void STDMETHODCALLTYPE DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override;
    void STDMETHODCALLTYPE DSSetShader(ID3D11DomainShader *pDomainShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override;
    void STDMETHODCALLTYPE DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override;
    void STDMETHODCALLTYPE DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override;
    void STDMETHODCALLTYPE CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override;
    void STDMETHODCALLTYPE CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts) override;
    void STDMETHODCALLTYPE CSSetShader(ID3D11ComputeShader *pComputeShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override;
    void STDMETHODCALLTYPE CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override;
    void STDMETHODCALLTYPE CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override;
    void STDMETHODCALLTYPE VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override;
    void STDMETHODCALLTYPE PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override;
    void STDMETHODCALLTYPE PSGetShader(ID3D11PixelShader **ppPixelShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override;
    void STDMETHODCALLTYPE PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override;
    void STDMETHODCALLTYPE VSGetShader(ID3D11VertexShader **ppVertexShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override;
    void STDMETHODCALLTYPE PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override;
    void STDMETHODCALLTYPE IAGetInputLayout(ID3D11InputLayout **ppInputLayout) override;
    void STDMETHODCALLTYPE IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppVertexBuffers, UINT *pStrides, UINT *pOffsets) override;
    void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D11Buffer **pIndexBuffer, DXGI_FORMAT *Format, UINT *Offset) override;
    void STDMETHODCALLTYPE GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override;
    void STDMETHODCALLTYPE GSGetShader(ID3D11GeometryShader **ppGeometryShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override;
    void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY *pTopology) override;
    void STDMETHODCALLTYPE VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override;
    void STDMETHODCALLTYPE VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override;
    void STDMETHODCALLTYPE GetPredication(ID3D11Predicate **, BOOL *) override;
    void STDMETHODCALLTYPE GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override;
    void STDMETHODCALLTYPE GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override;
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView) override;
    void STDMETHODCALLTYPE OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews) override;
    void STDMETHODCALLTYPE OMGetBlendState(ID3D11BlendState **ppBlendState, FLOAT BlendFactor[4], UINT *pSampleMask) override;
    void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D11DepthStencilState **ppDepthStencilState, UINT *pStencilRef) override;
    void STDMETHODCALLTYPE SOGetTargets(UINT NumBuffers, ID3D11Buffer **ppSOTargets) override;
    void STDMETHODCALLTYPE RSGetState(ID3D11RasterizerState **ppRasterizerState) override;
    void STDMETHODCALLTYPE RSGetViewports(UINT *pNumViewports, D3D11_VIEWPORT *pViewports) override;
    void STDMETHODCALLTYPE RSGetScissorRects(UINT *pNumRects, D3D11_RECT *pRects) override;
    void STDMETHODCALLTYPE HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override;
    void STDMETHODCALLTYPE HSGetShader(ID3D11HullShader **ppHullShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override;
    void STDMETHODCALLTYPE HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override;
    void STDMETHODCALLTYPE HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override;
    void STDMETHODCALLTYPE DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override;
    void STDMETHODCALLTYPE DSGetShader(ID3D11DomainShader **ppDomainShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override;
    void STDMETHODCALLTYPE DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override;
    void STDMETHODCALLTYPE DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override;
    void STDMETHODCALLTYPE CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override;
    void STDMETHODCALLTYPE CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews) override;
    void STDMETHODCALLTYPE CSGetShader(ID3D11ComputeShader **ppComputeShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override;
    void STDMETHODCALLTYPE CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override;
    void STDMETHODCALLTYPE CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override;
    void STDMETHODCALLTYPE ClearState() override;
    void STDMETHODCALLTYPE Flush() override;
    D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType() override;
    UINT STDMETHODCALLTYPE GetContextFlags() override;
    HRESULT STDMETHODCALLTYPE FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList **ppCommandList) override;
    void STDMETHODCALLTYPE CopySubresourceRegion1(ID3D11Resource *pDstResource, UINT DstSubresource,
            UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource,
            const D3D11_BOX *pSrcBox, UINT CopyFlags) override;
    void STDMETHODCALLTYPE UpdateSubresource1(ID3D11Resource *pDstResource, UINT DstSubresource,
            const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch,
            UINT SrcDepthPitch, UINT CopyFlags) override;
    void STDMETHODCALLTYPE DiscardResource(ID3D11Resource *pResource) override;
    void STDMETHODCALLTYPE DiscardView(ID3D11View *pResourceView) override;
    void STDMETHODCALLTYPE VSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant,
            const UINT *pNumConstants) override;
    void STDMETHODCALLTYPE HSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant,
            const UINT *pNumConstants) override;
    void STDMETHODCALLTYPE DSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant,
            const UINT *pNumConstants) override;
    void STDMETHODCALLTYPE GSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant,
            const UINT *pNumConstants) override;
    void STDMETHODCALLTYPE PSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant,
            const UINT *pNumConstants) override;
    void STDMETHODCALLTYPE CSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant,
            const UINT *pNumConstants) override;
    void STDMETHODCALLTYPE VSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants) override;
    void STDMETHODCALLTYPE HSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants) override;
    void STDMETHODCALLTYPE DSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants) override;
    void STDMETHODCALLTYPE GSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants) override;
    void STDMETHODCALLTYPE PSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants) override;
    void STDMETHODCALLTYPE CSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers,
            ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants) override;
    void STDMETHODCALLTYPE SwapDeviceContextState(ID3DDeviceContextState *pState,
            ID3DDeviceContextState **ppPreviousState) override;
    void STDMETHODCALLTYPE ClearView(ID3D11View *pView, const FLOAT Color[4],
            const D3D11_RECT *pRect, UINT NumRects) override;
    void STDMETHODCALLTYPE DiscardView1(ID3D11View *pResourceView,
            const D3D11_RECT *pRects, UINT NumRects) override;
};

template<class Interface, const GUID *iid>
class NativeChild : public Interface, public NativeAllocation
{
public:
    NativeDevice *device;
    LONG references = 1;
    LONG private_references = 1;
    NativePrivateData private_data;
    explicit NativeChild(NativeDevice *d) : device(d) { device->ChildAddRef(); device->AddRef(); }
    virtual ~NativeChild() {}
    void Retain() { InterlockedIncrement(&private_references); }
    void Drop()
    {
        NativeDevice *owner = device;
        {
            /* A cache lookup must not resurrect a child whose final private
             * reference is already being destroyed. ChildRelease stays outside
             * the guard because it may destroy the device and its lock. */
            NativeLock guard(owner);
            if (InterlockedDecrement(&private_references)) return;
            delete this;
        }
        owner->ChildRelease();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID requested, void **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        if (!IsEqualGUID(requested, *iid) && !IsEqualGUID(requested, IID_IUnknown)
                && !IsEqualGUID(requested, IID_ID3D11DeviceChild)) return E_NOINTERFACE;
        *out = static_cast<Interface *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        ULONG count = InterlockedIncrement(&references);
        if (count == 1) { Retain(); device->AddRef(); }
        return count;
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG count = InterlockedDecrement(&references);
        if (!count)
        {
            NativeDevice *d = device;
            Drop();
            d->Release();
        }
        return count;
    }
    void STDMETHODCALLTYPE GetDevice(ID3D11Device **out) override { if (out) { *out = device; device->AddRef(); } }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *size, void *data) override { NativeLock guard(device); return private_data.Get(guid, size, data); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void *data) override { NativeLock guard(device); return private_data.Set(guid, size, data, NULL); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *object) override { NativeLock guard(device); return private_data.Set(guid, sizeof(object), &object, const_cast<IUnknown *>(object)); }
};

class NativeTextureResource final : public IDXGIResource, public IDXGISurface
{
    ID3D11Resource *texture;
    NativeDevice *device;
    const DXGI_USAGE *usage;
public:
    NativeTextureResource(ID3D11Resource *t, NativeDevice *d, const DXGI_USAGE *u) : texture(t), device(d), usage(u) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT *, void *) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void *) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown *) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void **) override;
    HRESULT STDMETHODCALLTYPE GetSharedHandle(HANDLE *) override;
    HRESULT STDMETHODCALLTYPE GetUsage(DXGI_USAGE *) override;
    HRESULT STDMETHODCALLTYPE SetEvictionPriority(UINT) override;
    HRESULT STDMETHODCALLTYPE GetEvictionPriority(UINT *) override;
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SURFACE_DESC *) override;
    HRESULT STDMETHODCALLTYPE Map(DXGI_MAPPED_RECT *, UINT) override;
    HRESULT STDMETHODCALLTYPE Unmap() override;
};

class NativeTexture1D final : public NativeChild<ID3D11Texture1D, &IID_ID3D11Texture1D>
{
public:
    D3D11_TEXTURE1D_DESC desc = {};
    FLOAT min_lod = 0;
    D3D10DDI_HRESOURCE handle = {};
    D3D10DDI_HRTRESOURCE runtime_handle;
    UINT priority = 0;
    bool created = false;
    DXGI_USAGE usage = 0;
    NativeTextureResource dxgi_resource;
    BYTE *mapped = NULL;
    explicit NativeTexture1D(NativeDevice *d)
        : NativeChild(d), runtime_handle(d->AllocateResourceIdentity()), dxgi_resource(this, d, &usage) {}
    ~NativeTexture1D()
    {
        NativeLock guard(device);
        if (created) device->functions.pfnDestroyResource(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
        HeapFree(GetProcessHeap(), 0, mapped);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_INVALIDARG;
        if (IsEqualGUID(iid, IID_IDXGIObject) || IsEqualGUID(iid, IID_IDXGIDeviceSubObject)
                || IsEqualGUID(iid, IID_IDXGIResource))
        {
            *out = static_cast<IDXGIResource *>(&dxgi_resource); AddRef(); return S_OK;
        }
        if (IsEqualGUID(iid, IID_IDXGISurface) && desc.MipLevels == 1 && desc.ArraySize == 1)
        {
            *out = static_cast<IDXGISurface *>(&dxgi_resource); AddRef(); return S_OK;
        }
        if (IsEqualGUID(iid, IID_ID3D11Resource))
        {
            *out = static_cast<ID3D11Resource *>(this); AddRef(); return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION *out) override { if (out) *out = D3D11_RESOURCE_DIMENSION_TEXTURE1D; }
    void STDMETHODCALLTYPE SetEvictionPriority(UINT value) override { priority = value; }
    UINT STDMETHODCALLTYPE GetEvictionPriority() override { return priority; }
    void STDMETHODCALLTYPE GetDesc(D3D11_TEXTURE1D_DESC *out) override { if (out) *out = desc; }
};

class NativeTexture2D : public NativeChild<ID3D11Texture2D, &IID_ID3D11Texture2D>
{
public:
    D3D11_TEXTURE2D_DESC desc = {};
    FLOAT min_lod = 0;
    D3D10DDI_HRESOURCE handle = {};
    D3D10DDI_HRTRESOURCE runtime_handle;
    UINT priority = 0;
    bool created = false;
    bool registered = false;
    DXGI_USAGE usage = 0;
    NativeTextureResource dxgi_resource;
    BYTE *mapped = NULL;
    explicit NativeTexture2D(NativeDevice *d)
        : NativeChild(d), runtime_handle(d->AllocateResourceIdentity()), dxgi_resource(this, d, &usage) {}
    ~NativeTexture2D()
    {
        NativeLock guard(device);
        if (created) device->functions.pfnDestroyResource(device->driver_device, handle);
        if (registered) device->release_resource(device->runtime_device, runtime_handle.handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
        HeapFree(GetProcessHeap(), 0, mapped);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_INVALIDARG;
        if (IsEqualGUID(iid, IID_IDXGIObject) || IsEqualGUID(iid, IID_IDXGIDeviceSubObject)
                || IsEqualGUID(iid, IID_IDXGIResource))
        {
            *out = static_cast<IDXGIResource *>(&dxgi_resource); AddRef(); return S_OK;
        }
        if (IsEqualGUID(iid, IID_ID3D11Resource))
        {
            if (!out) return E_INVALIDARG;
            *out = static_cast<ID3D11Texture2D *>(this); AddRef(); return S_OK;
        }
        if (IsEqualGUID(iid, IID_IDXGISurface) && desc.MipLevels == 1 && desc.ArraySize == 1)
        {
            *out = static_cast<IDXGISurface *>(&dxgi_resource); AddRef(); return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    HRESULT GetSharedHandle(HANDLE *out)
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        if (!(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED)) return DXGI_ERROR_INVALID_CALL;
        NativeLock guard(device);
        D3DKMT_HANDLE share = 0;
        HRESULT hr = device->get_resource_handles(device->runtime_device, runtime_handle.handle, NULL, &share);
        if (FAILED(hr)) return hr;
        if (!share) return DXGI_ERROR_INVALID_CALL;
        *out = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(share));
        return S_OK;
    }
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION *out) override { if (out) *out = D3D11_RESOURCE_DIMENSION_TEXTURE2D; }
    void STDMETHODCALLTYPE SetEvictionPriority(UINT value) override { priority = value; }
    UINT STDMETHODCALLTYPE GetEvictionPriority() override { return priority; }
    void STDMETHODCALLTYPE GetDesc(D3D11_TEXTURE2D_DESC *out) override { if (out) *out = desc; }
};

HRESULT STDMETHODCALLTYPE NativeTextureResource::QueryInterface(REFIID iid, void **out) { return texture->QueryInterface(iid, out); }
ULONG STDMETHODCALLTYPE NativeTextureResource::AddRef() { return texture->AddRef(); }
ULONG STDMETHODCALLTYPE NativeTextureResource::Release() { return texture->Release(); }
HRESULT STDMETHODCALLTYPE NativeTextureResource::GetPrivateData(REFGUID guid, UINT *size, void *data) { return texture->GetPrivateData(guid, size, data); }
HRESULT STDMETHODCALLTYPE NativeTextureResource::SetPrivateData(REFGUID guid, UINT size, const void *data) { return texture->SetPrivateData(guid, size, data); }
HRESULT STDMETHODCALLTYPE NativeTextureResource::SetPrivateDataInterface(REFGUID guid, const IUnknown *data) { return texture->SetPrivateDataInterface(guid, data); }
HRESULT STDMETHODCALLTYPE NativeTextureResource::GetParent(REFIID iid, void **out) { return device->QueryInterface(iid, out); }
HRESULT STDMETHODCALLTYPE NativeTextureResource::GetDevice(REFIID iid, void **out) { return device->QueryInterface(iid, out); }
HRESULT STDMETHODCALLTYPE NativeTextureResource::GetSharedHandle(HANDLE *out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    D3D11_RESOURCE_DIMENSION dimension;
    texture->GetType(&dimension);
    if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return DXGI_ERROR_INVALID_CALL;
    return static_cast<NativeTexture2D *>(static_cast<ID3D11Texture2D *>(texture))->GetSharedHandle(out);
}
HRESULT STDMETHODCALLTYPE NativeTextureResource::GetUsage(DXGI_USAGE *out) { if (!out) return E_INVALIDARG; *out = *usage; return S_OK; }
HRESULT STDMETHODCALLTYPE NativeTextureResource::SetEvictionPriority(UINT value) { texture->SetEvictionPriority(value); return S_OK; }
HRESULT STDMETHODCALLTYPE NativeTextureResource::GetEvictionPriority(UINT *out) { if (!out) return E_INVALIDARG; *out = texture->GetEvictionPriority(); return S_OK; }

class NativeTexture3D : public NativeChild<ID3D11Texture3D, &IID_ID3D11Texture3D>
{
public:
    D3D11_TEXTURE3D_DESC desc = {};
    FLOAT min_lod = 0;
    D3D10DDI_HRESOURCE handle = {};
    D3D10DDI_HRTRESOURCE runtime_handle;
    UINT priority = 0;
    bool created = false;
    BYTE *mapped = NULL;
    explicit NativeTexture3D(NativeDevice *d) : NativeChild(d), runtime_handle(d->AllocateResourceIdentity()) {}
    ~NativeTexture3D()
    {
        NativeLock guard(device);
        if (created) device->functions.pfnDestroyResource(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
        HeapFree(GetProcessHeap(), 0, mapped);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_INVALIDARG;
        if (IsEqualGUID(iid, IID_ID3D11Resource))
        {
            *out = static_cast<ID3D11Texture3D *>(this); AddRef(); return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION *out) override { if (out) *out = D3D11_RESOURCE_DIMENSION_TEXTURE3D; }
    void STDMETHODCALLTYPE SetEvictionPriority(UINT value) override { priority = value; }
    UINT STDMETHODCALLTYPE GetEvictionPriority() override { return priority; }
    void STDMETHODCALLTYPE GetDesc(D3D11_TEXTURE3D_DESC *out) override { if (out) *out = desc; }
};

/* A borrowed description for operations shared by 2D, cube and volume textures.
 * Volume subresources index mips only; depth slices are not array subresources. */
struct NativeTextureInfo
{
    ID3D11Resource *resource;
    D3D11_TEXTURE2D_DESC desc;
    D3D10DDI_HRESOURCE handle;
    FLOAT *min_lod;
    BYTE *mapped;
    UINT depth;
    D3D10DDIRESOURCE_TYPE dimension;
};

static NativeTextureInfo *GetNativeTexture(ID3D11Resource *resource, NativeDevice *device, NativeTextureInfo *info)
{
    if (!resource) return NULL;
    ID3D11Device *owner = NULL;
    resource->GetDevice(&owner);
    bool same = owner == static_cast<ID3D11Device *>(device);
    if (owner) owner->Release();
    if (!same) return NULL;
    D3D11_RESOURCE_DIMENSION dimension;
    resource->GetType(&dimension);
    if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
    {
        NativeTexture1D *texture = static_cast<NativeTexture1D *>(static_cast<ID3D11Texture1D *>(resource));
        info->min_lod = &texture->min_lod;
        info->desc = {};
        info->desc.Width = texture->desc.Width;
        info->desc.Height = info->desc.SampleDesc.Count = 1;
        info->desc.MipLevels = texture->desc.MipLevels;
        info->desc.ArraySize = texture->desc.ArraySize;
        info->desc.Format = texture->desc.Format;
        info->desc.Usage = texture->desc.Usage;
        info->desc.BindFlags = texture->desc.BindFlags;
        info->desc.CPUAccessFlags = texture->desc.CPUAccessFlags;
        info->desc.MiscFlags = texture->desc.MiscFlags;
        info->handle = texture->handle;
        info->mapped = texture->mapped;
        info->depth = 1;
        info->dimension = D3D10DDIRESOURCE_TEXTURE1D;
    }
    else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        NativeTexture2D *texture = static_cast<NativeTexture2D *>(static_cast<ID3D11Texture2D *>(resource));
        info->min_lod = &texture->min_lod;
        info->desc = texture->desc;
        info->handle = texture->handle;
        info->mapped = texture->mapped;
        info->depth = 1;
        info->dimension = texture->desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE
                ? D3D10DDIRESOURCE_TEXTURECUBE : D3D10DDIRESOURCE_TEXTURE2D;
    }
    else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
    {
        NativeTexture3D *texture = static_cast<NativeTexture3D *>(static_cast<ID3D11Texture3D *>(resource));
        info->min_lod = &texture->min_lod;
        info->desc = {};
        info->desc.Width = texture->desc.Width;
        info->desc.Height = texture->desc.Height;
        info->desc.MipLevels = texture->desc.MipLevels;
        info->desc.ArraySize = info->desc.SampleDesc.Count = 1;
        info->desc.Format = texture->desc.Format;
        info->desc.Usage = texture->desc.Usage;
        info->desc.BindFlags = texture->desc.BindFlags;
        info->desc.CPUAccessFlags = texture->desc.CPUAccessFlags;
        info->desc.MiscFlags = texture->desc.MiscFlags;
        info->handle = texture->handle;
        info->mapped = texture->mapped;
        info->depth = texture->desc.Depth;
        info->dimension = D3D10DDIRESOURCE_TEXTURE3D;
    }
    else return NULL;
    info->resource = resource;
    return info;
}

HRESULT STDMETHODCALLTYPE NativeTextureResource::GetDesc(DXGI_SURFACE_DESC *out)
{
    if (!out) return E_INVALIDARG;
    NativeTextureInfo info;
    if (!GetNativeTexture(texture, device, &info)) return E_INVALIDARG;
    out->Width = info.desc.Width;
    out->Height = info.desc.Height;
    out->Format = info.desc.Format;
    out->SampleDesc = info.desc.SampleDesc;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeTextureResource::Map(DXGI_MAPPED_RECT *out, UINT flags)
{
    if (!out) return E_INVALIDARG;
    *out = {};
    if (flags & ~(DXGI_MAP_READ | DXGI_MAP_WRITE | DXGI_MAP_DISCARD)
            || !(flags & (DXGI_MAP_READ | DXGI_MAP_WRITE))
            || ((flags & DXGI_MAP_DISCARD) && (flags & DXGI_MAP_READ))) return E_INVALIDARG;
    D3D11_MAP map = flags & DXGI_MAP_DISCARD ? D3D11_MAP_WRITE_DISCARD
            : flags & DXGI_MAP_READ ? (flags & DXGI_MAP_WRITE ? D3D11_MAP_READ_WRITE : D3D11_MAP_READ)
            : D3D11_MAP_WRITE;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = device->context->Map(texture, 0, map, 0, &mapped);
    if (FAILED(hr)) return hr;
    out->Pitch = mapped.RowPitch;
    out->pBits = static_cast<BYTE *>(mapped.pData);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeTextureResource::Unmap()
{
    NativeTextureInfo info;
    if (!GetNativeTexture(texture, device, &info) || !info.mapped[0]) return DXGI_ERROR_INVALID_CALL;
    device->context->Unmap(texture, 0);
    return S_OK;
}

class NativeRenderTargetView : public NativeChild<ID3D11RenderTargetView, &IID_ID3D11RenderTargetView>
{
public:
    ID3D11Resource *texture = NULL;
    D3D10DDI_HRESOURCE resource_handle = {};
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    D3D10DDI_HRENDERTARGETVIEW handle = {};
    bool created = false;
    explicit NativeRenderTargetView(NativeDevice *d) : NativeChild(d) {}
    ~NativeRenderTargetView()
    {
        NativeLock guard(device);
        if (created) device->functions.pfnDestroyRenderTargetView(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
        if (texture) NativeDropResource(texture);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (IsEqualGUID(iid, IID_ID3D11View))
        {
            if (!out) return E_INVALIDARG;
            *out = static_cast<ID3D11RenderTargetView *>(this); AddRef(); return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    void STDMETHODCALLTYPE GetResource(ID3D11Resource **out) override { if (out) { *out = texture; texture->AddRef(); } }
    void STDMETHODCALLTYPE GetDesc(D3D11_RENDER_TARGET_VIEW_DESC *out) override { if (out) *out = desc; }
};

class NativeDepthView : public NativeChild<ID3D11DepthStencilView, &IID_ID3D11DepthStencilView>
{
public:
    ID3D11Resource *texture = NULL;
    D3D10DDI_HRESOURCE resource_handle = {};
    D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
    D3D10DDI_HDEPTHSTENCILVIEW handle = {};
    UINT mip_slice = 0, first_slice = 0, slice_count = 1;
    bool created = false;
    explicit NativeDepthView(NativeDevice *d) : NativeChild(d) {}
    bool ConflictsWith(const NativeShaderResourceView *) const;
    ~NativeDepthView()
    {
        NativeLock guard(device);
        if (created) device->functions.pfnDestroyDepthStencilView(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
        if (texture) NativeDropResource(texture);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (IsEqualGUID(iid, IID_ID3D11View))
        {
            if (!out) return E_INVALIDARG;
            *out = static_cast<ID3D11DepthStencilView *>(this); AddRef(); return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    void STDMETHODCALLTYPE GetResource(ID3D11Resource **out) override { if (out) { *out = texture; texture->AddRef(); } }
    void STDMETHODCALLTYPE GetDesc(D3D11_DEPTH_STENCIL_VIEW_DESC *out) override { if (out) *out = desc; }
};

class NativeInputLayout : public NativeChild<ID3D11InputLayout, &IID_ID3D11InputLayout>
{
public:
    D3D10DDI_HELEMENTLAYOUT handle = {};
    bool created = false;
    explicit NativeInputLayout(NativeDevice *d) : NativeChild(d) {}
    ~NativeInputLayout()
    {
        NativeLock guard(device);
        if (created) device->functions.pfnDestroyElementLayout(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
    }
};

template<class Interface, const GUID *iid, class Desc, class Handle>
class NativeState : public NativeChild<Interface, iid>
{
public:
    Desc desc = {};
    NativeStateCacheEntry cache_entry;
    Handle handle = {};
    void (APIENTRY *destroy)(D3D10DDI_HDEVICE, Handle) = NULL;
    bool created = false;
    explicit NativeState(NativeDevice *d) : NativeChild<Interface, iid>(d) {}
    ~NativeState()
    {
        NativeLock guard(this->device);
        if (created) destroy(this->device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
    }
    void STDMETHODCALLTYPE GetDesc(Desc *out) override { if (out) *out = desc; }
};

using NativeSampler = NativeState<ID3D11SamplerState, &IID_ID3D11SamplerState, D3D11_SAMPLER_DESC, D3D10DDI_HSAMPLER>;
using NativeDepthStencil = NativeState<ID3D11DepthStencilState, &IID_ID3D11DepthStencilState, D3D11_DEPTH_STENCIL_DESC, D3D10DDI_HDEPTHSTENCILSTATE>;

class NativeBlend : public NativeChild<ID3D11BlendState1, &IID_ID3D11BlendState1>
{
public:
    D3D11_BLEND_DESC1 desc = {};
    NativeStateCacheEntry cache_entry;
    D3D10DDI_HBLENDSTATE handle = {};
    void (APIENTRY *destroy)(D3D10DDI_HDEVICE, D3D10DDI_HBLENDSTATE) = NULL;
    bool created = false;
    explicit NativeBlend(NativeDevice *d) : NativeChild(d) {}
    ~NativeBlend()
    {
        NativeLock guard(device);
        if (created) destroy(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (IsEqualGUID(iid, IID_ID3D11BlendState))
        {
            if (!out) return E_INVALIDARG;
            *out = static_cast<ID3D11BlendState *>(this);
            AddRef();
            return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    void STDMETHODCALLTYPE GetDesc(D3D11_BLEND_DESC *out) override
    {
        if (!out) return;
        out->AlphaToCoverageEnable = desc.AlphaToCoverageEnable;
        out->IndependentBlendEnable = desc.IndependentBlendEnable;
        for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            out->RenderTarget[i].BlendEnable = desc.RenderTarget[i].BlendEnable;
            out->RenderTarget[i].SrcBlend = desc.RenderTarget[i].SrcBlend;
            out->RenderTarget[i].DestBlend = desc.RenderTarget[i].DestBlend;
            out->RenderTarget[i].BlendOp = desc.RenderTarget[i].BlendOp;
            out->RenderTarget[i].SrcBlendAlpha = desc.RenderTarget[i].SrcBlendAlpha;
            out->RenderTarget[i].DestBlendAlpha = desc.RenderTarget[i].DestBlendAlpha;
            out->RenderTarget[i].BlendOpAlpha = desc.RenderTarget[i].BlendOpAlpha;
            out->RenderTarget[i].RenderTargetWriteMask = desc.RenderTarget[i].RenderTargetWriteMask;
        }
    }
    void STDMETHODCALLTYPE GetDesc1(D3D11_BLEND_DESC1 *out) override { if (out) *out = desc; }
};

class NativeRasterizer : public NativeChild<ID3D11RasterizerState1, &IID_ID3D11RasterizerState1>
{
public:
    D3D11_RASTERIZER_DESC1 desc = {};
    NativeStateCacheEntry cache_entry;
    D3D10DDI_HRASTERIZERSTATE handle = {};
    void (APIENTRY *destroy)(D3D10DDI_HDEVICE, D3D10DDI_HRASTERIZERSTATE) = NULL;
    bool created = false;
    explicit NativeRasterizer(NativeDevice *d) : NativeChild(d) {}
    ~NativeRasterizer()
    {
        NativeLock guard(device);
        if (created) destroy(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (IsEqualGUID(iid, IID_ID3D11RasterizerState))
        {
            if (!out) return E_INVALIDARG;
            *out = static_cast<ID3D11RasterizerState *>(this);
            AddRef();
            return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    void STDMETHODCALLTYPE GetDesc(D3D11_RASTERIZER_DESC *out) override
    {
        if (out) memcpy(out, &desc, sizeof(*out));
    }
    void STDMETHODCALLTYPE GetDesc1(D3D11_RASTERIZER_DESC1 *out) override { if (out) *out = desc; }
};

template<class Interface, const GUID *iid>
class NativeShader : public NativeChild<Interface, iid>
{
public:
    D3D10DDI_HSHADER handle = {};
    bool created = false;
    explicit NativeShader(NativeDevice *d) : NativeChild<Interface, iid>(d) {}
    ~NativeShader()
    {
        NativeLock guard(this->device);
        if (created) this->device->functions.pfnDestroyShader(this->device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
    }
};
using NativeVertexShader = NativeShader<ID3D11VertexShader, &IID_ID3D11VertexShader>;
using NativePixelShader = NativeShader<ID3D11PixelShader, &IID_ID3D11PixelShader>;
using NativeGeometryShader = NativeShader<ID3D11GeometryShader, &IID_ID3D11GeometryShader>;

using NativeHullShader = NativeShader<ID3D11HullShader, &IID_ID3D11HullShader>;

using NativeDomainShader = NativeShader<ID3D11DomainShader, &IID_ID3D11DomainShader>;

using NativeComputeShader = NativeShader<ID3D11ComputeShader, &IID_ID3D11ComputeShader>;

class NativeShaderResourceView : public NativeChild<ID3D11ShaderResourceView, &IID_ID3D11ShaderResourceView>
{
public:
    ID3D11Resource *texture = NULL;
    D3D10DDI_HRESOURCE resource_handle = {};
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    D3D10DDI_HSHADERRESOURCEVIEW handle = {};
    bool created = false;
    explicit NativeShaderResourceView(NativeDevice *d) : NativeChild(d) {}
    ~NativeShaderResourceView()
    {
        NativeLock guard(device);
        if (created) device->functions.pfnDestroyShaderResourceView(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
        if (texture) NativeDropResource(texture);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (IsEqualGUID(iid, IID_ID3D11View))
        {
            if (!out) return E_INVALIDARG;
            *out = static_cast<ID3D11ShaderResourceView *>(this); AddRef(); return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    void STDMETHODCALLTYPE GetResource(ID3D11Resource **out) override { if (out) { *out = texture; texture->AddRef(); } }
    void STDMETHODCALLTYPE GetDesc(D3D11_SHADER_RESOURCE_VIEW_DESC *out) override { if (out) *out = desc; }
};

class NativeUnorderedAccessView : public NativeChild<ID3D11UnorderedAccessView, &IID_ID3D11UnorderedAccessView>
{
public:
    ID3D11Resource *texture = NULL;
    D3D10DDI_HRESOURCE resource_handle = {};
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
    D3D11DDI_HUNORDEREDACCESSVIEW handle = {};
    bool created = false;
    explicit NativeUnorderedAccessView(NativeDevice *d) : NativeChild(d) {}
    ~NativeUnorderedAccessView()
    {
        NativeLock guard(device);
        if (created) device->functions.pfnDestroyUnorderedAccessView(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
        if (texture) NativeDropResource(texture);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (IsEqualGUID(iid, IID_ID3D11View))
        {
            if (!out) return E_INVALIDARG;
            *out = static_cast<ID3D11UnorderedAccessView *>(this); AddRef(); return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    void STDMETHODCALLTYPE GetResource(ID3D11Resource **out) override { if (out) { *out = texture; texture->AddRef(); } }
    void STDMETHODCALLTYPE GetDesc(D3D11_UNORDERED_ACCESS_VIEW_DESC *out) override { if (out) *out = desc; }
};

bool NativeDepthView::ConflictsWith(const NativeShaderResourceView *view) const
{
    if (!view || view->texture != texture) return false;
    UINT mip, mips, first, count;
    switch (view->desc.ViewDimension)
    {
        case D3D11_SRV_DIMENSION_TEXTURE1D:
        case D3D11_SRV_DIMENSION_TEXTURE2D:
            mip = view->desc.Texture2D.MostDetailedMip;
            mips = view->desc.Texture2D.MipLevels;
            first = 0;
            count = 1;
            break;
        case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
        case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
            mip = view->desc.Texture2DArray.MostDetailedMip;
            mips = view->desc.Texture2DArray.MipLevels;
            first = view->desc.Texture2DArray.FirstArraySlice;
            count = view->desc.Texture2DArray.ArraySize;
            break;
        default: return true;
    }
    if (mip_slice < mip || mip_slice - mip >= mips
            || first_slice >= first + count || first >= first_slice + slice_count) return false;
    bool stencil = view->desc.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT
            || view->desc.Format == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
    return !(desc.Flags & (stencil ? D3D11_DSV_READ_ONLY_STENCIL : D3D11_DSV_READ_ONLY_DEPTH));
}

/* Bind hazards are per subresource. Buffer elements and 3D depth slices share
 * one subresource; array slices and mip levels can be bound independently. */
struct NativeViewRange
{
    ID3D11Resource *resource = NULL;
    UINT mip = 0, mips = 1, first = 0, count = 1;
};

static NativeViewRange NativeRange(ID3D11ShaderResourceView *object)
{
    NativeViewRange range;
    if (!object) return range;
    auto *view = static_cast<NativeShaderResourceView *>(object);
    range.resource = view->texture;
    const auto &d = view->desc;
    switch (d.ViewDimension)
    {
        case D3D11_SRV_DIMENSION_TEXTURE1D:
        case D3D11_SRV_DIMENSION_TEXTURE2D:
        case D3D11_SRV_DIMENSION_TEXTURE3D:
            range.mip = d.Texture2D.MostDetailedMip; range.mips = d.Texture2D.MipLevels; break;
        case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
        case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
            range.mip = d.Texture2DArray.MostDetailedMip; range.mips = d.Texture2DArray.MipLevels;
            range.first = d.Texture2DArray.FirstArraySlice; range.count = d.Texture2DArray.ArraySize; break;
        case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
            range.first = d.Texture2DMSArray.FirstArraySlice; range.count = d.Texture2DMSArray.ArraySize; break;
        case D3D11_SRV_DIMENSION_TEXTURECUBE:
            range.mip = d.TextureCube.MostDetailedMip; range.mips = d.TextureCube.MipLevels; range.count = 6; break;
        case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
            range.mip = d.TextureCubeArray.MostDetailedMip; range.mips = d.TextureCubeArray.MipLevels;
            range.first = d.TextureCubeArray.First2DArrayFace; range.count = 6 * d.TextureCubeArray.NumCubes; break;
        default: break;
    }
    return range;
}

static NativeViewRange NativeRange(ID3D11RenderTargetView *object)
{
    NativeViewRange range;
    if (!object) return range;
    auto *view = static_cast<NativeRenderTargetView *>(object);
    range.resource = view->texture;
    const auto &d = view->desc;
    switch (d.ViewDimension)
    {
        case D3D11_RTV_DIMENSION_TEXTURE1D:
        case D3D11_RTV_DIMENSION_TEXTURE2D:
        case D3D11_RTV_DIMENSION_TEXTURE3D: range.mip = d.Texture2D.MipSlice; break;
        case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
        case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
            range.mip = d.Texture2DArray.MipSlice; range.first = d.Texture2DArray.FirstArraySlice;
            range.count = d.Texture2DArray.ArraySize; break;
        case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
            range.first = d.Texture2DMSArray.FirstArraySlice; range.count = d.Texture2DMSArray.ArraySize; break;
        default: break;
    }
    return range;
}

static NativeViewRange NativeRange(ID3D11UnorderedAccessView *object)
{
    NativeViewRange range;
    if (!object) return range;
    auto *view = static_cast<NativeUnorderedAccessView *>(object);
    range.resource = view->texture;
    const auto &d = view->desc;
    if (d.ViewDimension != D3D11_UAV_DIMENSION_BUFFER) range.mip = d.Texture2D.MipSlice;
    if (d.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE1DARRAY || d.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2DARRAY)
    {
        range.first = d.Texture2DArray.FirstArraySlice;
        range.count = d.Texture2DArray.ArraySize;
    }
    return range;
}

static NativeViewRange NativeRange(ID3D11DepthStencilView *object)
{
    NativeViewRange range;
    if (!object) return range;
    auto *view = static_cast<NativeDepthView *>(object);
    range.resource = view->texture;
    range.mip = view->mip_slice;
    range.first = view->first_slice;
    range.count = view->slice_count;
    return range;
}

template<class A, class B> static bool NativeViewsOverlap(A *a, B *b)
{
    NativeViewRange x = NativeRange(a), y = NativeRange(b);
    return x.resource && x.resource == y.resource
            && x.mip < y.mip + y.mips && y.mip < x.mip + x.mips
            && x.first < y.first + y.count && y.first < x.first + x.count;
}

class NativeQuery : public NativeChild<ID3D11Predicate, &IID_ID3D11Query>
{
public:
    D3D11_QUERY_DESC desc = {};
    D3D10DDI_HQUERY handle = {};
    UINT data_size = 0;
    bool created = false;
    bool ended = false;
    bool begun = false;
    bool is_predicate = false;
    explicit NativeQuery(NativeDevice *d) : NativeChild(d) {}
    ~NativeQuery()
    {
        NativeLock guard(device);
        if (created) device->functions.pfnDestroyQuery(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (IsEqualGUID(iid, IID_ID3D11Asynchronous)
                || (is_predicate && IsEqualGUID(iid, IID_ID3D11Predicate)))
        {
            if (!out) return E_INVALIDARG;
            *out = static_cast<ID3D11Predicate *>(this); AddRef(); return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    void STDMETHODCALLTYPE GetDesc(D3D11_QUERY_DESC *out) override { if (out) *out = desc; }
    UINT STDMETHODCALLTYPE GetDataSize() override { return data_size; }
};

static HRESULT APIENTRY NativeQueryAdapter(HANDLE handle, const D3DDDICB_QUERYADAPTERINFO *args)
{
    if (!args || !handle || reinterpret_cast<ULONG_PTR>(handle) > ~0u) return E_INVALIDARG;
    D3DKMT_QUERYADAPTERINFO query = {};
    query.hAdapter = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(handle));
    query.Type = KMTQAITYPE_UMDRIVERPRIVATE;
    query.pPrivateDriverData = args->pPrivateDriverData;
    query.PrivateDriverDataSize = args->PrivateDriverDataSize;
    return StatusToHresult(D3DKMTQueryAdapterInfo(&query));
}

static void APIENTRY NativeSetError(D3D10DDI_HRTCORELAYER layer, HRESULT error)
{
    NativeDevice *device = static_cast<NativeDevice *>(layer.handle);
    if (error == DXGI_DDI_ERR_WASSTILLDRAWING) error = DXGI_ERROR_WAS_STILL_DRAWING;
    if (error == DXGI_DDI_ERR_UNSUPPORTED) error = DXGI_ERROR_UNSUPPORTED;
    device->operation_error = error;
    if (FAILED(error) && error != DXGI_ERROR_WAS_STILL_DRAWING)
        WARN("Native UMD reported error %#lx.\n", error);
    if (SUCCEEDED(device->removed_reason) && (error == DXGI_ERROR_DEVICE_REMOVED || error == DXGI_ERROR_DEVICE_RESET
            || error == DXGI_ERROR_DEVICE_HUNG || error == DXGI_ERROR_DRIVER_INTERNAL_ERROR))
        device->removed_reason = error;
}

static HRESULT APIENTRY NativeQueryAdapter2(HANDLE handle, const D3DDDICB_QUERYADAPTERINFO2 *args)
{
    return RosUmdQueryAdapterInfo2(handle, args, D3DKMTQueryAdapterInfo);
}

#include "native_wddm20.inl"

template<class Desc, class DriverHandle, class RuntimeHandle, class Calc, class Create>
static HRESULT CreateNativeDefaultState(NativeDevice *device, const Desc &desc,
        DriverHandle &handle, RuntimeHandle runtime_handle, Calc calc, Create create)
{
    SIZE_T size = calc(device->driver_device, &desc);
    handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!handle.pDrvPrivate) return E_OUTOFMEMORY;
    device->BeginCall();
    create(device->driver_device, &desc, handle, runtime_handle);
    if (FAILED(device->operation_error))
    {
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
        handle.pDrvPrivate = NULL;
    }
    return device->operation_error;
}

HRESULT NativeDevice::InitializeDefaultStates()
{
    if (!functions.pfnCalcPrivateBlendStateSize || !functions.pfnCreateBlendState
            || !functions.pfnDestroyBlendState || !functions.pfnSetBlendState
            || !functions.pfnCalcPrivateDepthStencilStateSize || !functions.pfnCreateDepthStencilState
            || !functions.pfnDestroyDepthStencilState || !functions.pfnSetDepthStencilState
            || !functions.pfnCalcPrivateRasterizerStateSize || !functions.pfnCreateRasterizerState
            || !functions.pfnDestroyRasterizerState || !functions.pfnSetRasterizerState) return E_NOINTERFACE;

    /* NULL API bindings select default state objects; they do not supply a
     * valid driver state. Keep the internal DDI objects outside COM ownership
     * so they neither appear in GetState nor keep their device alive. */
    D3D10_1_DDI_BLEND_DESC blend = {};
    for (UINT i = 0; i < ARRAYSIZE(blend.RenderTarget); ++i)
    {
        blend.RenderTarget[i].SrcBlend = blend.RenderTarget[i].SrcBlendAlpha = D3D10_DDI_BLEND_ONE;
        blend.RenderTarget[i].DestBlend = blend.RenderTarget[i].DestBlendAlpha = D3D10_DDI_BLEND_ZERO;
        blend.RenderTarget[i].BlendOp = blend.RenderTarget[i].BlendOpAlpha = D3D10_DDI_BLEND_OP_ADD;
        blend.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    }
    D3D10DDI_HRTBLENDSTATE runtime_blend = {&default_blend};
    HRESULT hr = CreateNativeDefaultState(this, blend, default_blend, runtime_blend,
            functions.pfnCalcPrivateBlendStateSize, functions.pfnCreateBlendState);
    if (FAILED(hr)) return hr;

    D3D10_DDI_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D10_DDI_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D10_DDI_COMPARISON_LESS;
    depth.StencilReadMask = depth.StencilWriteMask = 0xff;
    depth.FrontFace.StencilFailOp = depth.FrontFace.StencilDepthFailOp = depth.FrontFace.StencilPassOp = D3D10_DDI_STENCIL_OP_KEEP;
    depth.FrontFace.StencilFunc = D3D10_DDI_COMPARISON_ALWAYS;
    depth.BackFace = depth.FrontFace;
    D3D10DDI_HRTDEPTHSTENCILSTATE runtime_depth = {&default_depth};
    hr = CreateNativeDefaultState(this, depth, default_depth, runtime_depth,
            functions.pfnCalcPrivateDepthStencilStateSize, functions.pfnCreateDepthStencilState);
    if (FAILED(hr)) return hr;

    D3D10_DDI_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode = D3D10_DDI_FILL_SOLID;
    rasterizer.CullMode = D3D10_DDI_CULL_BACK;
    rasterizer.DepthClipEnable = TRUE;
    D3D10DDI_HRTRASTERIZERSTATE runtime_rasterizer = {&default_rasterizer};
    return CreateNativeDefaultState(this, rasterizer, default_rasterizer, runtime_rasterizer,
            functions.pfnCalcPrivateRasterizerStateSize, functions.pfnCreateRasterizerState);
}

HRESULT NativeDevice::Initialize(IDXGIAdapter *selected_adapter, UINT creation_flags,
        const D3D_FEATURE_LEVEL *levels, UINT count)
{
    DXGI_ADAPTER_DESC desc;
    HRESULT hr = selected_adapter->GetDesc(&desc);
    if (FAILED(hr)) return hr;
    D3DKMT_OPENADAPTERFROMLUID open = {};
    open.AdapterLuid = desc.AdapterLuid;
    if (FAILED(hr = StatusToHresult(D3DKMTOpenAdapterFromLuid(&open)))) return hr;
    km_adapter = open.hAdapter;
    D3DKMT_UMDFILENAMEINFO name = {};
    name.Version = KMTUMDVERSION_DX11;
    D3DKMT_QUERYADAPTERINFO query = {};
    query.hAdapter = km_adapter;
    query.Type = KMTQAITYPE_UMDRIVERNAME;
    query.pPrivateDriverData = &name;
    query.PrivateDriverDataSize = sizeof(name);
    if (D3DKMTQueryAdapterInfo(&query) < 0 || !name.UmdFileName[0]) return DXGI_ERROR_UNSUPPORTED;
    name.UmdFileName[MAX_PATH - 1] = 0;
    DWORD load_flags = 0;
    if ((name.UmdFileName[0] == '\\' && name.UmdFileName[1] == '\\')
            || (name.UmdFileName[0] && name.UmdFileName[1] == ':' && name.UmdFileName[2] == '\\'))
        load_flags = LOAD_WITH_ALTERED_SEARCH_PATH;
    umd = LoadLibraryExW(name.UmdFileName, NULL, load_flags);
    if (!umd) return HRESULT_FROM_WIN32(GetLastError());
    PFND3D10DDI_OPENADAPTER open_adapter = reinterpret_cast<PFND3D10DDI_OPENADAPTER>(GetProcAddress(umd, "OpenAdapter10_2"));
    if (!open_adapter) return DXGI_ERROR_UNSUPPORTED;
    adapter_callbacks.pfnQueryAdapterInfoCb = NativeQueryAdapter;
    adapter_callbacks.pfnQueryAdapterInfoCb2 = NativeQueryAdapter2;
    D3D10DDIARG_OPENADAPTER args = {};
    args.hRTAdapter.handle = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(km_adapter));
    args.Interface = NATIVE_WDDM20_INTERFACE;
    args.Version = NATIVE_WDDM20_BUILD << 16;
    args.pAdapterCallbacks = reinterpret_cast<const D3DDDI_ADAPTERCALLBACKS *>(&adapter_callbacks);
    args.pAdapterFuncs_2 = &adapter_functions;
    if (FAILED(hr = open_adapter(&args))) return hr;
    driver_adapter = args.hAdapter;
    if (!adapter_functions.pfnGetSupportedVersions || !adapter_functions.pfnGetCaps
            || !adapter_functions.pfnCalcPrivateDeviceSize || !adapter_functions.pfnCreateDevice
            || !adapter_functions.pfnCloseAdapter) return DXGI_ERROR_UNSUPPORTED;
    UINT versions_count = 0;
    if (FAILED(hr = adapter_functions.pfnGetSupportedVersions(driver_adapter, &versions_count, NULL))) return hr;
    if (!versions_count || versions_count > 4096) return E_FAIL;
    UINT64 *versions = static_cast<UINT64 *>(HeapAlloc(GetProcessHeap(), 0, versions_count * sizeof(*versions)));
    if (!versions) return E_OUTOFMEMORY;
    UINT capacity = versions_count;
    hr = adapter_functions.pfnGetSupportedVersions(driver_adapter, &versions_count, versions);
    bool supported = false;
    if (SUCCEEDED(hr) && versions_count <= capacity)
        for (UINT i = 0; i < versions_count; ++i)
        {
            if (versions[i] == ((UINT64)D3D11_0_DDI_INTERFACE_VERSION << 32 | (UINT64)D3D11_0_DDI_BUILD_VERSION << 16)) supported = true;
            if (versions[i] == ((UINT64)NATIVE_WDDM20_INTERFACE << 32 | (UINT64)NATIVE_WDDM20_BUILD << 16))
                wddm20 = true;
        }
    HeapFree(GetProcessHeap(), 0, versions);
    if (!supported && !wddm20) return DXGI_ERROR_UNSUPPORTED;

    D3D11DDI_3DPIPELINESUPPORT_CAPS pipeline = {};
    D3D10_2DDIARG_GETCAPS caps = {};
    caps.Type = D3D11DDICAPS_3DPIPELINESUPPORT;
    caps.pData = &pipeline;
    caps.DataSize = sizeof(pipeline);
    if (FAILED(hr = adapter_functions.pfnGetCaps(driver_adapter, &caps))) return hr;

    D3D11DDI_THREADING_CAPS threading = {};
    caps.Type = D3D11DDICAPS_THREADING;
    caps.pData = &threading;
    caps.DataSize = sizeof(threading);
    if (SUCCEEDED(adapter_functions.pfnGetCaps(driver_adapter, &caps)))
        threading_caps = threading.Caps;

    D3D11DDI_SHADER_CAPS shader = {};
    caps.Type = D3D11DDICAPS_SHADER;
    caps.pData = &shader;
    caps.DataSize = sizeof(shader);
    if (SUCCEEDED(adapter_functions.pfnGetCaps(driver_adapter, &caps)))
        shader_caps = shader.Caps;

    UINT pipeline_level = ~0u;
    for (UINT i = 0; i < count; ++i)
    {
        UINT level;
        switch (levels[i])
        {
            case D3D_FEATURE_LEVEL_10_0: level = 0; break;
            case D3D_FEATURE_LEVEL_10_1: level = 1; break;
            case D3D_FEATURE_LEVEL_11_0: level = 2; break;
            default: continue;
        }
        if (!(pipeline.Caps & (1u << level))) continue;
        feature_level = levels[i];
        pipeline_level = level;
        break;
    }
    if (pipeline_level == ~0u) return DXGI_ERROR_UNSUPPORTED;

    D3DKMT_CREATEDEVICE create = {};
    create.hAdapter = km_adapter;
    if (FAILED(hr = StatusToHresult(D3DKMTCreateDevice(&create)))) return hr;
    km_device = create.hDevice;
    runtime = LoadLibraryW(L"d3dumdrt.dll");
    if (!runtime) return HRESULT_FROM_WIN32(GetLastError());
    typedef HRESULT (WINAPI *CreateCallbacks)(D3DKMT_HANDLE, D3DKMT_HANDLE, D3DDDI_DEVICECALLBACKS *, HANDLE *);
    CreateCallbacks create_callbacks = reinterpret_cast<CreateCallbacks>(GetProcAddress(runtime, "D3DUmdRtCreateDeviceCallbacks"));
    destroy_callbacks = reinterpret_cast<HRESULT (WINAPI *)(HANDLE)>(GetProcAddress(runtime, "D3DUmdRtDestroyDeviceCallbacks"));
    register_resource = reinterpret_cast<decltype(register_resource)>(GetProcAddress(runtime, "D3DUmdRtRegisterResource"));
    get_resource_handles = reinterpret_cast<decltype(get_resource_handles)>(GetProcAddress(runtime, "D3DUmdRtGetResourceHandles"));
    get_single_allocation = reinterpret_cast<decltype(get_single_allocation)>(GetProcAddress(runtime, "D3DUmdRtGetSingleResourceAllocation"));
    adopt_resource = reinterpret_cast<decltype(adopt_resource)>(GetProcAddress(runtime, "D3DUmdRtAdoptResource"));
    release_resource = reinterpret_cast<decltype(release_resource)>(GetProcAddress(runtime, "D3DUmdRtReleaseResource"));
    rotate_resources = reinterpret_cast<decltype(rotate_resources)>(GetProcAddress(runtime, "D3DUmdRtRotateResourceIdentities"));
    enqueue_event = reinterpret_cast<decltype(enqueue_event)>(GetProcAddress(runtime, "D3DUmdRtEnqueueSetEvent"));
    if (!create_callbacks || !destroy_callbacks || !register_resource || !get_resource_handles
            || !adopt_resource || !release_resource) return E_NOINTERFACE;
    /* D3D10/11 AllocateCb uses D3DDDI_ALLOCATIONINFO; pAllocationInfo2 is
     * reserved in this callback contract. Neither a WDDM 2.x kernel driver
     * nor GPU virtual-address support changes the UMD's allocation stride.
     * Selecting INFO2 from those capabilities reads past the driver's array. */
    if (FAILED(hr = create_callbacks(km_adapter, km_device, &kernel_callbacks, &runtime_device))) return hr;

    D3D10DDIARG_CALCPRIVATEDEVICESIZE size_args = {};
    size_args.Interface = wddm20 ? NATIVE_WDDM20_INTERFACE : D3D11_0_DDI_INTERFACE_VERSION;
    size_args.Version = (wddm20 ? NATIVE_WDDM20_BUILD : D3D11_0_DDI_BUILD_VERSION) << 16;
    size_args.Flags = pipeline_level << 1;
    if (creation_flags & D3D11_CREATE_DEVICE_SINGLETHREADED) size_args.Flags |= 0x10;
    SIZE_T private_size = adapter_functions.pfnCalcPrivateDeviceSize(driver_adapter, &size_args);
    if (!private_size) return E_FAIL;
    SIZE_T prefix_size = wddm20 ? sizeof(NativeWddm20DeviceHeader) : 0;
    if (private_size > MAXULONG_PTR - prefix_size) return E_OUTOFMEMORY;
    driver_device_storage = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, private_size + prefix_size);
    if (!driver_device_storage) return E_OUTOFMEMORY;
    driver_device.pDrvPrivate = static_cast<BYTE *>(driver_device_storage) + prefix_size;
    if (wddm20)
    {
        static_cast<NativeWddm20DeviceHeader *>(driver_device_storage)->device = this;
        NativeWddm20InitializeCallbacks(this);
    }
    core_callbacks.pfnSetErrorCb = NativeSetError;
    dxgi_callbacks.pfnPresentCb = NativePresent;
    D3D10DDIARG_CREATEDEVICE create_args = {};
    create_args.hRTDevice.handle = runtime_device;
    create_args.Interface = size_args.Interface;
    create_args.Version = size_args.Version;
    create_args.Flags = size_args.Flags;
    create_args.pKTCallbacks = &kernel_callbacks;
    create_args.p11DeviceFuncs = wddm20 ? reinterpret_cast<D3D11DDI_DEVICEFUNCS *>(&wddm20_functions) : &functions;
    create_args.hDrvDevice = driver_device;
    create_args.hRTCoreLayer.handle = this;
    create_args.p11UMCallbacks = wddm20 ? reinterpret_cast<D3D11DDI_CORELAYER_DEVICECALLBACKS *>(&wddm20_callbacks) : &core_callbacks;
    create_args.DXGIBaseDDI.pDXGIBaseCallbacks = &dxgi_callbacks;
    create_args.DXGIBaseDDI.pDXGIDDIBaseFunctions2 = wddm20 ? reinterpret_cast<DXGI1_1_DDI_BASE_FUNCTIONS *>(&wddm20_dxgi) : &dxgi_functions;
    if (wddm20) create_args.ppfnRetrieveSubObject = &retrieve_subobject;
    if (FAILED(hr = adapter_functions.pfnCreateDevice(driver_adapter, &create_args)))
    {
        WARN("Native UMD failed to create a DDI %#x device, hr %#lx.\n", size_args.Interface, hr);
        return hr;
    }
    driver_created = true;
    if (wddm20) NativeWddm20InstallFunctions(this);
    if (!functions.pfnDestroyDevice || !functions.pfnCalcPrivateResourceSize
            || !functions.pfnCreateResource || !functions.pfnDestroyResource
            || !functions.pfnCalcPrivateRenderTargetViewSize || !functions.pfnCreateRenderTargetView
            || !functions.pfnDestroyRenderTargetView || !functions.pfnClearRenderTargetView
            || !functions.pfnResourceCopy || !functions.pfnStagingResourceMap
            || !functions.pfnStagingResourceUnmap || !functions.pfnFlush) return E_NOINTERFACE;
    if (FAILED(hr = InitializeDefaultStates())) return hr;
    context = new NativeContext(this);
    if (!context) return E_OUTOFMEMORY;
    context->OMSetBlendState(NULL, NULL, ~0u);
    context->OMSetDepthStencilState(NULL, 0);
    context->RSSetState(NULL);
    adapter = selected_adapter;
    adapter->AddRef();
    flags = creation_flags;
    initialized = true;
    TRACE("Native UMD %s created D3D11 device %p, feature level %#x.\n", debugstr_w(name.UmdFileName), this, feature_level);
    return S_OK;
}

NativeDevice::~NativeDevice()
{
    clearing = true;
    if (context) delete context;
    context = NULL;
    /* Unbind internal defaults before destroying them. There is no later draw
     * on this device, so a null DDI state is only used during teardown. */
    if (default_blend.pDrvPrivate)
    {
        D3D10DDI_HBLENDSTATE empty = {};
        const FLOAT factors[4] = {1, 1, 1, 1};
        functions.pfnSetBlendState(driver_device, empty, factors, ~0u);
        functions.pfnDestroyBlendState(driver_device, default_blend);
        HeapFree(GetProcessHeap(), 0, default_blend.pDrvPrivate);
    }
    if (default_depth.pDrvPrivate)
    {
        D3D10DDI_HDEPTHSTENCILSTATE empty = {};
        functions.pfnSetDepthStencilState(driver_device, empty, 0);
        functions.pfnDestroyDepthStencilState(driver_device, default_depth);
        HeapFree(GetProcessHeap(), 0, default_depth.pDrvPrivate);
    }
    if (default_rasterizer.pDrvPrivate)
    {
        D3D10DDI_HRASTERIZERSTATE empty = {};
        functions.pfnSetRasterizerState(driver_device, empty);
        functions.pfnDestroyRasterizerState(driver_device, default_rasterizer);
        HeapFree(GetProcessHeap(), 0, default_rasterizer.pDrvPrivate);
    }
    if (driver_created && functions.pfnDestroyDevice) functions.pfnDestroyDevice(driver_device);
    if (runtime_device && destroy_callbacks)
    {
        HRESULT hr = destroy_callbacks(runtime_device);
        if (FAILED(hr)) ERR("Native UMD retained runtime objects at device teardown: %#lx.\n", hr);
    }
    HeapFree(GetProcessHeap(), 0, driver_device_storage);
    if (km_device)
    {
        D3DKMT_DESTROYDEVICE args = {};
        args.hDevice = km_device;
        D3DKMTDestroyDevice(&args);
    }
    if (driver_adapter.pDrvPrivate && adapter_functions.pfnCloseAdapter) adapter_functions.pfnCloseAdapter(driver_adapter);
    if (km_adapter)
    {
        D3DKMT_CLOSEADAPTER args = {};
        args.hAdapter = km_adapter;
        D3DKMTCloseAdapter(&args);
    }
    if (umd) FreeLibrary(umd);
    if (runtime) FreeLibrary(runtime);
    if (adapter) adapter->Release();
    DeleteCriticalSection(&lock);
}

HRESULT STDMETHODCALLTYPE NativeDevice::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, IID_ID3D11Device))
        *out = static_cast<ID3D11Device *>(this);
    else if (IsEqualGUID(iid, IID_ID3D11Device1))
        *out = static_cast<ID3D11Device1 *>(this);
    else if (IsEqualGUID(iid, IID_IDXGIObject) || IsEqualGUID(iid, IID_IDXGIDevice)
            || IsEqualGUID(iid, IID_IDXGIDevice1) || IsEqualGUID(iid, IID_IDXGIDevice2))
        *out = static_cast<IDXGIDevice2 *>(this);
    else if (IsEqualGUID(iid, IID_IWineDXGISwapChainFactory))
        *out = static_cast<IWineDXGISwapChainFactory *>(this);
    else if (IsEqualGUID(iid, IID_ID3D11Multithread))
        *out = static_cast<ID3D11Multithread *>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

void NativeDevice::MaybeDestroy()
{
    NativeLock guard(this);
    if (references || (context && context->references) || clearing) return;
    clearing = true;
    if (context)
    {
        context->ClearState();
        context->ReleaseContextState();
    }
    clearing = false;
}

ULONG STDMETHODCALLTYPE NativeDevice::Release()
{
    ULONG count = InterlockedDecrement(&references);
    MaybeDestroy();
    Drop();
    return count;
}

HRESULT STDMETHODCALLTYPE NativeContext::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, IID_ID3D11DeviceChild)
            || IsEqualGUID(iid, IID_ID3D11DeviceContext))
        *out = static_cast<ID3D11DeviceContext *>(this);
    else if (IsEqualGUID(iid, IID_ID3D11DeviceContext1))
        *out = static_cast<ID3D11DeviceContext1 *>(this);
    else if (IsEqualGUID(iid, IID_ID3D11Multithread) && !deferred)
        *out = static_cast<ID3D11Multithread *>(this);
    else if (IsEqualGUID(iid, IID_ID3DUserDefinedAnnotation))
        *out = static_cast<ID3DUserDefinedAnnotation *>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE NativeContext::AddRef()
{
    NativeLock guard(device);
    ULONG count = InterlockedIncrement(&references);
    /* The device owns the immediate context's storage. External context
     * references collectively retain one public device reference. */
    if (count == 1) device->AddRef();
    return count;
}

ULONG STDMETHODCALLTYPE NativeContext::Release()
{
    NativeDevice *owner = device;
    ULONG count;
    {
        NativeLock guard(owner);
        count = InterlockedDecrement(&references);
    }
    /* Releasing the last device reference can also delete this context and
     * its lock, so do it after the lock guard has gone out of scope. */
    if (!count)
    {
        if (deferred) delete this;
        owner->Release();
    }
    return count;
}

HRESULT STDMETHODCALLTYPE NativeDevice::GetPrivateData(REFGUID guid, UINT *size, void *data) { NativeLock guard(this); return private_data.Get(guid, size, data); }
HRESULT STDMETHODCALLTYPE NativeDevice::SetPrivateData(REFGUID guid, UINT size, const void *data) { NativeLock guard(this); return private_data.Set(guid, size, data, NULL); }
HRESULT STDMETHODCALLTYPE NativeDevice::SetPrivateDataInterface(REFGUID guid, const IUnknown *object) { NativeLock guard(this); return private_data.Set(guid, sizeof(object), &object, const_cast<IUnknown *>(object)); }
D3D_FEATURE_LEVEL STDMETHODCALLTYPE NativeDevice::GetFeatureLevel() { return feature_level; }
UINT STDMETHODCALLTYPE NativeDevice::GetCreationFlags() { return flags; }
HRESULT STDMETHODCALLTYPE NativeDevice::GetDeviceRemovedReason()
{
    NativeLock guard(this);
    if (FAILED(removed_reason)) return removed_reason;

    D3DKMT_GETDEVICESTATE state = {};
    state.hDevice = km_device;
    state.StateType = D3DKMT_DEVICESTATE_EXECUTION;
    NTSTATUS status = D3DKMTGetDeviceState(&state);
    if (status == STATUS_DEVICE_REMOVED)
        return removed_reason = DXGI_ERROR_DEVICE_REMOVED;
    if (status == STATUS_GRAPHICS_ADAPTER_WAS_RESET)
        return removed_reason = DXGI_ERROR_DEVICE_RESET;
    if (status == STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE)
        return removed_reason = DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    /* A failed query does not establish removal, and must not become a
     * cached success or prevent a later successful state query. */
    if (status != STATUS_SUCCESS)
        return status < 0 ? StatusToHresult(status) : E_UNEXPECTED;

    switch (state.ExecutionState)
    {
        case D3DKMT_DEVICEEXECUTION_ACTIVE:
            return S_OK;
        case D3DKMT_DEVICEEXECUTION_RESET:
            removed_reason = DXGI_ERROR_DEVICE_RESET;
            break;
        case D3DKMT_DEVICEEXECUTION_HUNG:
            removed_reason = DXGI_ERROR_DEVICE_HUNG;
            break;
        case D3DKMT_DEVICEEXECUTION_STOPPED:
            removed_reason = DXGI_ERROR_DEVICE_REMOVED;
            break;
        case D3DKMT_DEVICEEXECUTION_ERROR_OUTOFMEMORY:
        case D3DKMT_DEVICEEXECUTION_ERROR_DMAFAULT:
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
        case D3DKMT_DEVICEEXECUTION_ERROR_DMAPAGEFAULT:
#endif
        default:
            removed_reason = DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            break;
    }
    return removed_reason;
}
void STDMETHODCALLTYPE NativeDevice::GetImmediateContext(ID3D11DeviceContext **out) { if (out) { *out = context; context->AddRef(); } }
void STDMETHODCALLTYPE NativeDevice::GetImmediateContext1(ID3D11DeviceContext1 **out) { if (out) { *out = context; context->AddRef(); } }
HRESULT STDMETHODCALLTYPE NativeDevice::CreateDeferredContext1(UINT flags, ID3D11DeviceContext1 **out)
{
    if (out) *out = NULL;
    ID3D11DeviceContext *base = NULL;
    HRESULT hr = CreateDeferredContext(flags, out ? &base : NULL);
    if (SUCCEEDED(hr) && out)
    {
        hr = base->QueryInterface(IID_ID3D11DeviceContext1, reinterpret_cast<void **>(out));
        base->Release();
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::OpenSharedResource1(HANDLE, REFIID, void **out)
{
    if (out) *out = NULL;
    /* This entry point accepts NT shared handles, not the legacy global KMT
     * handles consumed by OpenSharedResource(). */
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE NativeDevice::OpenSharedResourceByName(LPCWSTR, DWORD, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE NativeDevice::SetExceptionMode(UINT mode) { if (mode & ~D3D11_RAISE_FLAG_DRIVER_INTERNAL_ERROR) return E_INVALIDARG; exception_mode = mode; return S_OK; }
UINT STDMETHODCALLTYPE NativeDevice::GetExceptionMode() { return exception_mode; }
HRESULT STDMETHODCALLTYPE NativeDevice::SetGPUThreadPriority(INT) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE NativeDevice::GetGPUThreadPriority(INT *out) { if (!out) return E_INVALIDARG; *out = 0; return E_NOTIMPL; }

HRESULT STDMETHODCALLTYPE NativeDevice::SetMaximumFrameLatency(UINT latency)
{
    if (latency > 16) return DXGI_ERROR_INVALID_CALL;
    NativeLock guard(this);
    D3DKMT_SETQUEUEDLIMIT limit = {};
    limit.hDevice = km_device;
    limit.Type = D3DKMT_SET_QUEUEDLIMIT_PRESENT;
    limit.QueuedPresentLimit = latency;
    return StatusToHresult(D3DKMTSetQueuedLimit(&limit));
}

HRESULT STDMETHODCALLTYPE NativeDevice::GetMaximumFrameLatency(UINT *latency)
{
    if (!latency) return DXGI_ERROR_INVALID_CALL;
    NativeLock guard(this);
    D3DKMT_SETQUEUEDLIMIT limit = {};
    limit.hDevice = km_device;
    limit.Type = D3DKMT_GET_QUEUEDLIMIT_PRESENT;
    HRESULT hr = StatusToHresult(D3DKMTSetQueuedLimit(&limit));
    if (SUCCEEDED(hr)) *latency = limit.QueuedPresentLimit;
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::EnqueueSetEvent(HANDLE event)
{
    if (!event) return E_INVALIDARG;
    NativeLock guard(this);
    if (!enqueue_event) return E_NOTIMPL;
    /* Device removal also releases completion waiters. This event says
     * nothing about whether a swapchain image was actually displayed. */
    if (FAILED(GetDeviceRemovedReason())) return SetEvent(event) ? S_OK : E_INVALIDARG;
    context->Flush();
    if (FAILED(GetDeviceRemovedReason())) return SetEvent(event) ? S_OK : E_INVALIDARG;
    return enqueue_event(runtime_device, event);
}

class NativeBuffer : public NativeChild<ID3D11Buffer, &IID_ID3D11Buffer>
{
public:
    D3D11_BUFFER_DESC desc = {};
    D3D10DDI_HRESOURCE handle = {};
    D3D10DDI_HRTRESOURCE runtime_handle;
    UINT priority = 0;
    bool created = false;
    bool mapped = false;
    explicit NativeBuffer(NativeDevice *d) : NativeChild(d), runtime_handle(d->AllocateResourceIdentity()) {}
    ~NativeBuffer()
    {
        NativeLock guard(device);
        if (created) device->functions.pfnDestroyResource(device->driver_device, handle);
        HeapFree(GetProcessHeap(), 0, handle.pDrvPrivate);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (IsEqualGUID(iid, IID_ID3D11Resource))
        {
            if (!out) return E_INVALIDARG;
            *out = static_cast<ID3D11Buffer *>(this); AddRef(); return S_OK;
        }
        return NativeChild::QueryInterface(iid, out);
    }
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION *out) override { if (out) *out = D3D11_RESOURCE_DIMENSION_BUFFER; }
    void STDMETHODCALLTYPE SetEvictionPriority(UINT value) override { priority = value; }
    UINT STDMETHODCALLTYPE GetEvictionPriority() override { return priority; }
    void STDMETHODCALLTYPE GetDesc(D3D11_BUFFER_DESC *out) override { if (out) *out = desc; }
};

static NativeBuffer *GetNativeBuffer(ID3D11Resource *resource, NativeDevice *device)
{
    if (!resource) return NULL;
    D3D11_RESOURCE_DIMENSION dimension;
    resource->GetType(&dimension);
    if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER) return NULL;
    ID3D11Device *owner = NULL;
    resource->GetDevice(&owner);
    bool same = owner == static_cast<ID3D11Device *>(device);
    if (owner) owner->Release();
    return same ? static_cast<NativeBuffer *>(static_cast<ID3D11Buffer *>(resource)) : NULL;
}

#define NATIVE_RESOURCE_REFERENCE(Name, Method) \
static void Name(ID3D11Resource *resource) \
{ \
    D3D11_RESOURCE_DIMENSION dimension; \
    resource->GetType(&dimension); \
    switch (dimension) \
    { \
        case D3D11_RESOURCE_DIMENSION_BUFFER: static_cast<NativeBuffer *>(static_cast<ID3D11Buffer *>(resource))->Method(); break; \
        case D3D11_RESOURCE_DIMENSION_TEXTURE1D: static_cast<NativeTexture1D *>(static_cast<ID3D11Texture1D *>(resource))->Method(); break; \
        case D3D11_RESOURCE_DIMENSION_TEXTURE2D: static_cast<NativeTexture2D *>(static_cast<ID3D11Texture2D *>(resource))->Method(); break; \
        case D3D11_RESOURCE_DIMENSION_TEXTURE3D: static_cast<NativeTexture3D *>(static_cast<ID3D11Texture3D *>(resource))->Method(); break; \
        default: break; \
    } \
}
NATIVE_RESOURCE_REFERENCE(NativeRetainResource, Retain)
NATIVE_RESOURCE_REFERENCE(NativeDropResource, Drop)
#undef NATIVE_RESOURCE_REFERENCE

HRESULT STDMETHODCALLTYPE NativeDevice::CreateBuffer(const D3D11_BUFFER_DESC *desc,
        const D3D11_SUBRESOURCE_DATA *initial, ID3D11Buffer **out)
{
    if (out) *out = NULL;
    if (!desc || !desc->ByteWidth || desc->Usage > D3D11_USAGE_STAGING) return E_INVALIDARG;
    if (initial && !initial->pSysMem) return E_INVALIDARG;
    if ((desc->BindFlags & D3D11_BIND_CONSTANT_BUFFER) && (desc->BindFlags != D3D11_BIND_CONSTANT_BUFFER
            || desc->ByteWidth % 16 || desc->ByteWidth > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16)) return E_INVALIDARG;
    if (desc->Usage == D3D11_USAGE_IMMUTABLE && !initial) return E_INVALIDARG;
    if ((desc->Usage == D3D11_USAGE_DEFAULT || desc->Usage == D3D11_USAGE_IMMUTABLE) && desc->CPUAccessFlags) return E_INVALIDARG;
    if (desc->Usage == D3D11_USAGE_DYNAMIC && desc->CPUAccessFlags != D3D11_CPU_ACCESS_WRITE) return E_INVALIDARG;
    if (desc->Usage == D3D11_USAGE_STAGING && (desc->BindFlags || desc->MiscFlags)) return E_INVALIDARG;
    D3D11_BUFFER_DESC normalized = *desc;
    if (normalized.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
    {
        if (!normalized.StructureByteStride || normalized.StructureByteStride % 4
                || normalized.StructureByteStride > D3D11_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES
                || normalized.ByteWidth % normalized.StructureByteStride
                || (normalized.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)) return E_INVALIDARG;
    }
    else
    {
        /* StructureByteStride is ignored for ordinary buffers.  The native
         * runtime must not expose or forward the application's unused value. */
        normalized.StructureByteStride = 0;
    }
    if (!out) return S_FALSE;
    NativeLock guard(this);
    NativeBuffer *buffer = new NativeBuffer(this);
    if (!buffer) return E_OUTOFMEMORY;
    if (!buffer->runtime_handle.handle) { buffer->Release(); return E_OUTOFMEMORY; }
    buffer->desc = normalized;
    D3D10DDI_MIPINFO mip = {normalized.ByteWidth, 1, 1, normalized.ByteWidth, 1, 1};
    D3D11DDIARG_CREATERESOURCE args = {};
    args.pMipInfoList = &mip;
    args.pInitialDataUP = reinterpret_cast<const D3D10_DDIARG_SUBRESOURCE_UP *>(initial);
    args.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
    args.Usage = normalized.Usage;
    args.BindFlags = normalized.BindFlags & ~D3D11_BIND_UNORDERED_ACCESS;
    if (normalized.BindFlags & D3D11_BIND_UNORDERED_ACCESS) args.BindFlags |= 0x100;
    args.MapFlags = normalized.CPUAccessFlags >> 16;
    args.MiscFlags = normalized.MiscFlags;
    args.Format = DXGI_FORMAT_UNKNOWN;
    args.SampleDesc.Count = 1;
    args.MipLevels = args.ArraySize = 1;
    args.ByteStride = normalized.StructureByteStride;
    SIZE_T size = functions.pfnCalcPrivateResourceSize(driver_device, &args);
    buffer->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!buffer->handle.pDrvPrivate) { buffer->Release(); return E_OUTOFMEMORY; }
    BeginCall();
    functions.pfnCreateResource(driver_device, &args, buffer->handle, buffer->runtime_handle);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { buffer->Release(); return hr; }
    buffer->created = true;
    *out = buffer;
    return S_OK;
}

static HRESULT MapBuffer(NativeDevice *device, NativeBuffer *buffer, UINT subresource,
        D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped)
{
    if (subresource || buffer->mapped || type < D3D11_MAP_READ || type > D3D11_MAP_WRITE_NO_OVERWRITE
            || (flags & ~D3D11_MAP_FLAG_DO_NOT_WAIT)) return E_INVALIDARG;
    if ((type == D3D11_MAP_READ || type == D3D11_MAP_READ_WRITE) && !(buffer->desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ)) return E_INVALIDARG;
    if (type != D3D11_MAP_READ && !(buffer->desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE)) return E_INVALIDARG;
    bool constant = !!(buffer->desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER);
    if (type >= D3D11_MAP_WRITE_DISCARD && buffer->desc.Usage != D3D11_USAGE_DYNAMIC) return E_INVALIDARG;
    if (type == D3D11_MAP_WRITE_NO_OVERWRITE && constant) return E_INVALIDARG;
    PFND3D10DDI_RESOURCEMAP map;
    if (buffer->desc.Usage == D3D11_USAGE_STAGING) map = device->functions.pfnStagingResourceMap;
    else if (type == D3D11_MAP_WRITE_DISCARD) map = constant
            ? device->functions.pfnDynamicConstantBufferMapDiscard : device->functions.pfnDynamicIABufferMapDiscard;
    else if (type == D3D11_MAP_WRITE_NO_OVERWRITE) map = device->functions.pfnDynamicIABufferMapNoOverwrite;
    else return E_INVALIDARG;
    if (!map) return E_NOTIMPL;
    NativeLock guard(device);
    device->BeginCall();
    D3D10DDI_MAPPED_SUBRESOURCE result = {};
    map(device->driver_device, buffer->handle, 0, static_cast<D3D10_DDI_MAP>(type), flags, &result);
    if (FAILED(device->operation_error)) return device->operation_error;
    if (!result.pData) return E_FAIL;
    mapped->pData = result.pData;
    mapped->RowPitch = result.RowPitch;
    mapped->DepthPitch = result.DepthPitch;
    buffer->mapped = true;
    return S_OK;
}


static NativeTexture2D *NativeTexture(ID3D11Resource *resource, NativeDevice *device)
{
    if (!resource) return NULL;
    D3D11_RESOURCE_DIMENSION dimension;
    resource->GetType(&dimension);
    if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return NULL;
    ID3D11Device *owner = NULL;
    resource->GetDevice(&owner);
    bool same = owner == static_cast<ID3D11Device *>(device);
    if (owner) owner->Release();
    return same ? static_cast<NativeTexture2D *>(static_cast<ID3D11Texture2D *>(resource)) : NULL;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateTexture2D(const D3D11_TEXTURE2D_DESC *input,
        const D3D11_SUBRESOURCE_DATA *initial, ID3D11Texture2D **out)
{
    return CreateTexture(input, initial, out);
}

/* A typeless allocation inherits the output capabilities of its typed views.
 * Do not ask the UMD to interpret a typeless format as a render target. */
static DXGI_FORMAT NativeColorViewFormat(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R32G32B32_TYPELESS: return DXGI_FORMAT_R32G32B32_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R8G8_TYPELESS: return DXGI_FORMAT_R8G8_UNORM;
        case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_FLOAT;
        case DXGI_FORMAT_R8_TYPELESS: return DXGI_FORMAT_R8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
        default: return format;
    }
}

HRESULT NativeDevice::ValidateTextureBindings(DXGI_FORMAT format, UINT bind, D3D10DDIRESOURCE_TYPE dimension)
{
    UINT allowed = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
            | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS;
    if (dimension == D3D10DDIRESOURCE_TEXTURE2D) allowed |= D3D11_BIND_DECODER | D3D11_BIND_VIDEO_ENCODER;
    if (bind & ~allowed) return E_INVALIDARG;
    bool typed_depth = NativeDepthResourceFormat(format) != DXGI_FORMAT_UNKNOWN;
    bool depth_stencil_family = (format >= DXGI_FORMAT_R32G8X24_TYPELESS && format <= DXGI_FORMAT_X32_TYPELESS_G8X24_UINT)
            || (format >= DXGI_FORMAT_R24G8_TYPELESS && format <= DXGI_FORMAT_X24_TYPELESS_G8_UINT);
    bool depth_resource = typed_depth || format == DXGI_FORMAT_R16_TYPELESS || format == DXGI_FORMAT_R32_TYPELESS
            || format == DXGI_FORMAT_R24G8_TYPELESS || format == DXGI_FORMAT_R32G8X24_TYPELESS;
    if (dimension == D3D10DDIRESOURCE_TEXTURE3D && (typed_depth || depth_stencil_family)) return E_INVALIDARG;
    if ((bind & D3D11_BIND_DEPTH_STENCIL) && (!depth_resource || dimension == D3D10DDIRESOURCE_TEXTURE3D
            || (bind & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS)))) return E_INVALIDARG;
    if (typed_depth && (bind & ~D3D11_BIND_DEPTH_STENCIL)) return E_INVALIDARG;
    if (depth_stencil_family && (bind & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS))) return E_INVALIDARG;
    if ((bind & D3D11_BIND_UNORDERED_ACCESS) && feature_level < D3D_FEATURE_LEVEL_11_0) return E_INVALIDARG;
    if (bind & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS))
    {
        UINT support = 0;
        HRESULT hr = CheckFormatSupport(NativeColorViewFormat(format), &support);
        if (FAILED(hr)) return hr;
        if ((bind & D3D11_BIND_RENDER_TARGET) && !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)) return E_INVALIDARG;
        if ((bind & D3D11_BIND_UNORDERED_ACCESS) && !(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)) return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateTexture1D(const D3D11_TEXTURE1D_DESC *input,
        const D3D11_SUBRESOURCE_DATA *initial, ID3D11Texture1D **out)
{
    if (out) *out = NULL;
    UINT limit = feature_level >= D3D_FEATURE_LEVEL_11_0 ? 16384 : 8192;
    UINT array_limit = feature_level >= D3D_FEATURE_LEVEL_11_0 ? 2048 : 512;
    if (!input || !input->Width || input->Width > limit || !input->ArraySize || input->ArraySize > array_limit
            || input->Usage > D3D11_USAGE_STAGING || input->Format == DXGI_FORMAT_UNKNOWN
            || (input->Format >= DXGI_FORMAT_AYUV && input->Format != DXGI_FORMAT_B4G4R4A4_UNORM)) return E_INVALIDARG;
    bool compressed = (input->Format >= DXGI_FORMAT_BC1_TYPELESS && input->Format <= DXGI_FORMAT_BC5_SNORM)
            || (input->Format >= DXGI_FORMAT_BC6H_TYPELESS && input->Format <= DXGI_FORMAT_BC7_UNORM_SRGB);
    UINT row_bytes, rows;
    if (compressed || input->Format == DXGI_FORMAT_R8G8_B8G8_UNORM || input->Format == DXGI_FORMAT_G8R8_G8B8_UNORM
            || !NativeUploadFormat(input->Format, input->Width, 1, &row_bytes, &rows)) return E_INVALIDARG;
    HRESULT validation = ValidateTextureBindings(input->Format, input->BindFlags, D3D10DDIRESOURCE_TEXTURE1D);
    if (FAILED(validation)) return validation;
    if (input->MiscFlags & ~(D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_RESOURCE_CLAMP)) return E_INVALIDARG;
    if (input->CPUAccessFlags & ~(D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE)) return E_INVALIDARG;
    if (input->Usage == D3D11_USAGE_STAGING && (input->BindFlags || input->MiscFlags)) return E_INVALIDARG;
    if (input->Usage == D3D11_USAGE_IMMUTABLE && !initial) return E_INVALIDARG;
    if ((input->Usage == D3D11_USAGE_DEFAULT || input->Usage == D3D11_USAGE_IMMUTABLE) && input->CPUAccessFlags) return E_INVALIDARG;
    if (input->Usage == D3D11_USAGE_DYNAMIC && (input->CPUAccessFlags != D3D11_CPU_ACCESS_WRITE
            || (input->BindFlags & ~D3D11_BIND_SHADER_RESOURCE) || input->ArraySize != 1)) return E_INVALIDARG;
    if ((input->MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS)
            && (input->BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE))
                != (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)) return E_INVALIDARG;
    UINT maximum_mips = 1;
    for (UINT width = input->Width; width > 1; width >>= 1) ++maximum_mips;
    UINT mip_count = input->MipLevels ? input->MipLevels : maximum_mips;
    if (mip_count > maximum_mips || (input->Usage == D3D11_USAGE_DYNAMIC && mip_count != 1)) return E_INVALIDARG;
    UINT subresources = mip_count * input->ArraySize;
    if (initial)
        for (UINT i = 0; i < subresources; ++i) if (!initial[i].pSysMem) return E_INVALIDARG;
    if (!out) return S_FALSE;

    NativeLock guard(this);
    NativeTexture1D *texture = new NativeTexture1D(this);
    if (!texture) return E_OUTOFMEMORY;
    if (!texture->runtime_handle.handle) { texture->Release(); return E_OUTOFMEMORY; }
    texture->desc = *input;
    texture->desc.MipLevels = mip_count;
    if (input->BindFlags & D3D11_BIND_SHADER_RESOURCE) texture->usage |= DXGI_USAGE_SHADER_INPUT;
    if (input->BindFlags & D3D11_BIND_RENDER_TARGET) texture->usage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;
    if (input->BindFlags & D3D11_BIND_UNORDERED_ACCESS) texture->usage |= DXGI_USAGE_UNORDERED_ACCESS;
    D3D10DDI_MIPINFO mips[15] = {};
    for (UINT i = 0; i < mip_count; ++i)
    {
        mips[i].TexelWidth = mips[i].PhysicalWidth = max(1u, input->Width >> i);
        mips[i].TexelHeight = mips[i].PhysicalHeight = mips[i].TexelDepth = mips[i].PhysicalDepth = 1;
    }
    D3D11DDIARG_CREATERESOURCE args = {};
    NativeCommandArray<D3D10_DDIARG_SUBRESOURCE_UP> normalized;
    if (initial)
    {
        normalized = NativeCommandArray<D3D10_DDIARG_SUBRESOURCE_UP>(subresources, NULL);
        if (!normalized.Valid()) { texture->Release(); return E_OUTOFMEMORY; }
        for (UINT i = 0; i < subresources; ++i)
        {
            NativeUploadFormat(input->Format, mips[i % mip_count].TexelWidth, 1, &row_bytes, &rows);
            normalized.Data()[i].pSysMem = const_cast<void *>(initial[i].pSysMem);
            normalized.Data()[i].SysMemPitch = normalized.Data()[i].SysMemSlicePitch = row_bytes;
        }
        args.pInitialDataUP = normalized.Data();
    }
    args.pMipInfoList = mips;
    args.ResourceDimension = D3D10DDIRESOURCE_TEXTURE1D;
    args.Usage = input->Usage;
    args.BindFlags = input->BindFlags & ~D3D11_BIND_UNORDERED_ACCESS;
    if (input->BindFlags & D3D11_BIND_UNORDERED_ACCESS) args.BindFlags |= 0x100;
    args.MapFlags = input->CPUAccessFlags >> 16;
    args.MiscFlags = input->MiscFlags;
    args.Format = input->Format;
    args.SampleDesc.Count = 1;
    args.ArraySize = input->ArraySize;
    args.MipLevels = mip_count;
    SIZE_T size = functions.pfnCalcPrivateResourceSize(driver_device, &args);
    texture->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    texture->mapped = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, subresources));
    if (!texture->handle.pDrvPrivate || !texture->mapped) { texture->Release(); return E_OUTOFMEMORY; }
    BeginCall();
    functions.pfnCreateResource(driver_device, &args, texture->handle, texture->runtime_handle);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { texture->Release(); return hr; }
    texture->created = true;
    *out = texture;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateTexture3D(const D3D11_TEXTURE3D_DESC *input,
        const D3D11_SUBRESOURCE_DATA *initial, ID3D11Texture3D **out)
{
    if (out) *out = NULL;
    if (!input || !input->Width || !input->Height || !input->Depth
            || input->Width > D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
            || input->Height > D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
            || input->Depth > D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
            || input->Format == DXGI_FORMAT_UNKNOWN || input->Usage > D3D11_USAGE_STAGING
            || (input->BindFlags & D3D11_BIND_DEPTH_STENCIL)) return E_INVALIDARG;
    HRESULT validation = ValidateTextureBindings(input->Format, input->BindFlags, D3D10DDIRESOURCE_TEXTURE3D);
    if (FAILED(validation)) return validation;
    if (input->MiscFlags & ~(D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_RESOURCE_CLAMP)) return E_INVALIDARG;
    if (input->CPUAccessFlags & ~(D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE)) return E_INVALIDARG;
    if (input->Usage == D3D11_USAGE_STAGING && (input->BindFlags || input->MiscFlags)) return E_INVALIDARG;
    if (input->Usage == D3D11_USAGE_IMMUTABLE && !initial) return E_INVALIDARG;
    if ((input->Usage == D3D11_USAGE_DEFAULT || input->Usage == D3D11_USAGE_IMMUTABLE) && input->CPUAccessFlags) return E_INVALIDARG;
    if (input->Usage == D3D11_USAGE_DYNAMIC && (input->CPUAccessFlags != D3D11_CPU_ACCESS_WRITE
            || (input->BindFlags & ~D3D11_BIND_SHADER_RESOURCE))) return E_INVALIDARG;
    UINT maximum_mips = 1;
    for (UINT dimension = max(input->Depth, max(input->Width, input->Height)); dimension > 1; dimension >>= 1) ++maximum_mips;
    UINT mip_count = input->MipLevels ? input->MipLevels : maximum_mips;
    if (mip_count > maximum_mips) return E_INVALIDARG;
    if (initial)
        for (UINT i = 0; i < mip_count; ++i) if (!initial[i].pSysMem) return E_INVALIDARG;
    if (!out) return S_FALSE;
    NativeLock guard(this);
    NativeTexture3D *texture = new NativeTexture3D(this);
    if (!texture) return E_OUTOFMEMORY;
    if (!texture->runtime_handle.handle) { texture->Release(); return E_OUTOFMEMORY; }
    texture->desc = *input;
    texture->desc.MipLevels = mip_count;
    D3D10DDI_MIPINFO mips[12] = {};
    for (UINT i = 0; i < mip_count; ++i)
    {
        mips[i].TexelWidth = mips[i].PhysicalWidth = max(1u, input->Width >> i);
        mips[i].TexelHeight = mips[i].PhysicalHeight = max(1u, input->Height >> i);
        mips[i].TexelDepth = mips[i].PhysicalDepth = max(1u, input->Depth >> i);
    }
    D3D11DDIARG_CREATERESOURCE args = {};
    args.pMipInfoList = mips;
    args.pInitialDataUP = reinterpret_cast<const D3D10_DDIARG_SUBRESOURCE_UP *>(initial);
    args.ResourceDimension = D3D10DDIRESOURCE_TEXTURE3D;
    args.Usage = input->Usage;
    args.BindFlags = input->BindFlags & ~D3D11_BIND_UNORDERED_ACCESS;
    if (input->BindFlags & D3D11_BIND_UNORDERED_ACCESS) args.BindFlags |= 0x100;
    args.MapFlags = input->CPUAccessFlags >> 16;
    args.MiscFlags = input->MiscFlags;
    args.Format = input->Format;
    args.SampleDesc.Count = args.ArraySize = 1;
    args.MipLevels = mip_count;
    SIZE_T size = functions.pfnCalcPrivateResourceSize(driver_device, &args);
    texture->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    texture->mapped = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, mip_count));
    if (!texture->handle.pDrvPrivate || !texture->mapped) { texture->Release(); return E_OUTOFMEMORY; }
    BeginCall();
    functions.pfnCreateResource(driver_device, &args, texture->handle, texture->runtime_handle);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { texture->Release(); return hr; }
    texture->created = true;
    *out = texture;
    return S_OK;
}

HRESULT NativeDevice::CreateTexture(const D3D11_TEXTURE2D_DESC *input,
        const D3D11_SUBRESOURCE_DATA *initial, ID3D11Texture2D **out,
        bool present, const DXGI_DDI_PRIMARY_DESC *primary)
{
    if (out) *out = NULL;
    UINT dimension_limit = feature_level >= D3D_FEATURE_LEVEL_11_0 ? 16384 : 8192;
    UINT array_limit = feature_level >= D3D_FEATURE_LEVEL_11_0 ? 2048 : 512;
    if (!input || !input->Width || !input->Height || !input->ArraySize
            || input->Width > dimension_limit || input->Height > dimension_limit
            || input->ArraySize > array_limit
            || !input->SampleDesc.Count || input->Format == DXGI_FORMAT_UNKNOWN) return E_INVALIDARG;
    HRESULT validation = ValidateTextureBindings(input->Format, input->BindFlags, D3D10DDIRESOURCE_TEXTURE2D);
    if (FAILED(validation)) return validation;
    if (input->Usage > D3D11_USAGE_STAGING) return E_INVALIDARG;
    if (input->Usage == D3D11_USAGE_STAGING && (input->BindFlags || input->MiscFlags || input->SampleDesc.Count != 1)) return E_INVALIDARG;
    if (input->Usage == D3D11_USAGE_IMMUTABLE && !initial) return E_INVALIDARG;
    if ((input->Usage == D3D11_USAGE_DEFAULT || input->Usage == D3D11_USAGE_IMMUTABLE) && input->CPUAccessFlags) return E_INVALIDARG;
    if (input->Usage == D3D11_USAGE_DYNAMIC && input->CPUAccessFlags != D3D11_CPU_ACCESS_WRITE) return E_INVALIDARG;
    if ((input->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) && (input->Width != input->Height
            || input->ArraySize < 6 || input->SampleDesc.Count != 1
            || (feature_level < D3D_FEATURE_LEVEL_10_1 && input->ArraySize != 6))) return E_INVALIDARG;
    if ((input->MiscFlags & (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))
            == (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)) return E_INVALIDARG;
    if (input->MiscFlags & (D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE)) return E_NOTIMPL;
    if ((input->MiscFlags & D3D11_RESOURCE_MISC_SHARED) && (input->Usage != D3D11_USAGE_DEFAULT
            || input->CPUAccessFlags || input->MipLevels != 1 || input->ArraySize != 1
            || input->SampleDesc.Count != 1)) return E_INVALIDARG;
    UINT maximum_mips = 1;
    for (UINT dimension = max(input->Width, input->Height); dimension > 1; dimension >>= 1) ++maximum_mips;
    UINT mip_count = input->MipLevels ? input->MipLevels : maximum_mips;
    if (mip_count > maximum_mips || (input->SampleDesc.Count > 1 && (mip_count != 1 || initial))) return E_INVALIDARG;
    if (input->SampleDesc.Count == 1)
    {
        if (input->SampleDesc.Quality) return E_INVALIDARG;
    }
    else
    {
        UINT quality_count = 0;
        DXGI_FORMAT sample_format = NativeColorViewFormat(input->Format);
        if (input->Format == DXGI_FORMAT_R24G8_TYPELESS) sample_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        if (input->Format == DXGI_FORMAT_R32G8X24_TYPELESS) sample_format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        validation = CheckMultisampleQualityLevels(sample_format, input->SampleDesc.Count, &quality_count);
        if (FAILED(validation) || !quality_count) return E_INVALIDARG;
        bool fixed_pattern = input->SampleDesc.Quality == D3D11_STANDARD_MULTISAMPLE_PATTERN
                || input->SampleDesc.Quality == D3D11_CENTER_MULTISAMPLE_PATTERN;
        if (fixed_pattern)
        {
            if (feature_level < D3D_FEATURE_LEVEL_10_1 || input->SampleDesc.Count > 16
                    || (input->SampleDesc.Count & (input->SampleDesc.Count - 1))) return E_INVALIDARG;
        }
        else if (input->SampleDesc.Quality >= quality_count) return E_INVALIDARG;
    }
    if (initial)
        for (UINT i = 0; i < mip_count * input->ArraySize; ++i) if (!initial[i].pSysMem) return E_INVALIDARG;
    if (!out) return S_FALSE;
    NativeLock guard(this);
    NativeTexture2D *texture = new NativeTexture2D(this);
    if (!texture) return E_OUTOFMEMORY;
    if (!texture->runtime_handle.handle) { texture->Release(); return E_OUTOFMEMORY; }
    texture->desc = *input;
    texture->desc.MipLevels = mip_count;
    if (input->BindFlags & D3D11_BIND_SHADER_RESOURCE) texture->usage |= DXGI_USAGE_SHADER_INPUT;
    if (input->BindFlags & D3D11_BIND_RENDER_TARGET) texture->usage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;
    if (input->BindFlags & D3D11_BIND_UNORDERED_ACCESS) texture->usage |= DXGI_USAGE_UNORDERED_ACCESS;
    if (present) texture->usage |= DXGI_USAGE_BACK_BUFFER;
    D3D10DDI_MIPINFO mips[15] = {};
    for (UINT i = 0; i < mip_count; ++i)
    {
        mips[i].TexelWidth = mips[i].PhysicalWidth = max(1u, input->Width >> i);
        mips[i].TexelHeight = mips[i].PhysicalHeight = max(1u, input->Height >> i);
        mips[i].TexelDepth = mips[i].PhysicalDepth = 1;
    }
    D3D11DDIARG_CREATERESOURCE args = {};
    args.pMipInfoList = mips;
    args.pInitialDataUP = reinterpret_cast<const D3D10_DDIARG_SUBRESOURCE_UP *>(initial);
    args.ResourceDimension = input->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE
            ? D3D10DDIRESOURCE_TEXTURECUBE : D3D10DDIRESOURCE_TEXTURE2D;
    args.Usage = input->Usage;
    args.BindFlags = input->BindFlags & ~D3D11_BIND_UNORDERED_ACCESS;
    if (input->BindFlags & D3D11_BIND_UNORDERED_ACCESS) args.BindFlags |= 0x100;
    if (present) args.BindFlags |= D3D10_DDI_BIND_PRESENT;
    if (present && !primary) args.BindFlags |= D3D10_DDI_BIND_SHADER_RESOURCE;
    args.MapFlags = input->CPUAccessFlags >> 16;
    args.MiscFlags = input->MiscFlags;
    args.Format = input->Format;
    args.SampleDesc = input->SampleDesc;
    args.MipLevels = mip_count;
    args.ArraySize = input->ArraySize;
    args.pPrimaryDesc = const_cast<DXGI_DDI_PRIMARY_DESC *>(primary);
    SIZE_T size = functions.pfnCalcPrivateResourceSize(driver_device, &args);
    texture->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    texture->mapped = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, mip_count * input->ArraySize));
    if (!texture->handle.pDrvPrivate || !texture->mapped) { texture->Release(); return E_OUTOFMEMORY; }
    NativeSharedTextureData shared_data = {native_shared_texture_signature, 1, texture->desc};
    D3DKMT_CREATEALLOCATIONFLAGS allocation_flags = {};
    allocation_flags.CreateShared = !!(input->MiscFlags & D3D11_RESOURCE_MISC_SHARED) || (present && !primary);
    if (allocation_flags.CreateShared)
        shared_data.desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
    if (present && !primary) shared_data.desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = register_resource(runtime_device, texture->runtime_handle.handle, allocation_flags, &shared_data, sizeof(shared_data));
    if (FAILED(hr))
    {
        WARN("Failed to register native texture, hr %#lx.\n", hr);
        texture->Release();
        return hr;
    }
    texture->registered = true;
    BeginCall();
    functions.pfnCreateResource(driver_device, &args, texture->handle, texture->runtime_handle);
    hr = operation_error;
    if (FAILED(hr)) { texture->Release(); return hr; }
    texture->created = true;
    *out = texture;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeDevice::OpenSharedResource(HANDLE shared, REFIID iid, void **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    ULONG_PTR value = reinterpret_cast<ULONG_PTR>(shared);
    if (!value || value > ~0u) return E_INVALIDARG;
    if (!functions.pfnCalcPrivateOpenedResourceSize || !functions.pfnOpenResource) return E_NOTIMPL;
    NativeLock guard(this);
    D3DKMT_QUERYRESOURCEINFO query = {};
    query.hDevice = km_device;
    query.hGlobalShare = static_cast<D3DKMT_HANDLE>(value);
    HRESULT hr = StatusToHresult(D3DKMTQueryResourceInfo(&query));
    if (FAILED(hr)) return hr;
    if ((query.PrivateRuntimeDataSize != sizeof(NativeSharedTextureData)
            && query.PrivateRuntimeDataSize != sizeof(DWM_DX_SHARED_SURFACE_INFO)) || !query.NumAllocations
            || query.NumAllocations > ~0u / sizeof(D3DDDI_OPENALLOCATIONINFO)) return DXGI_ERROR_UNSUPPORTED;
    NativeSharedTextureData data = {};
    DWM_DX_SHARED_SURFACE_INFO composition_data = {};
    D3DKMT_OPENRESOURCE open = {};
    open.hDevice = km_device;
    open.hGlobalShare = query.hGlobalShare;
    open.NumAllocations = query.NumAllocations;
    open.pPrivateRuntimeData = query.PrivateRuntimeDataSize == sizeof(data)
            ? static_cast<void *>(&data) : static_cast<void *>(&composition_data);
    open.PrivateRuntimeDataSize = query.PrivateRuntimeDataSize;
    open.ResourcePrivateDriverDataSize = query.ResourcePrivateDriverDataSize;
    open.TotalPrivateDriverDataBufferSize = query.TotalPrivateDriverDataSize;
    open.pOpenAllocationInfo = static_cast<D3DDDI_OPENALLOCATIONINFO *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            query.NumAllocations * sizeof(*open.pOpenAllocationInfo)));
    open.pResourcePrivateDriverData = HeapAlloc(GetProcessHeap(), 0, query.ResourcePrivateDriverDataSize ? query.ResourcePrivateDriverDataSize : 1);
    open.pTotalPrivateDriverDataBuffer = HeapAlloc(GetProcessHeap(), 0, query.TotalPrivateDriverDataSize ? query.TotalPrivateDriverDataSize : 1);
    NativeTexture2D *texture = NULL;
    if (!open.pOpenAllocationInfo || !open.pResourcePrivateDriverData || !open.pTotalPrivateDriverDataBuffer)
        hr = E_OUTOFMEMORY;
    else
        hr = StatusToHresult(D3DKMTOpenResource(&open));
    if (SUCCEEDED(hr))
    {
        /* A CPU-visible OS surface has a linear pitch; a producer's GPU
         * buffer has none, its layout being the driver's. */
        if (query.PrivateRuntimeDataSize == sizeof(composition_data)
                && composition_data.Magic == DWM_DX_SURFACE_INFO_MAGIC
                && composition_data.Format == DWM_DX_FORMAT_B8G8R8A8_UNORM
                && composition_data.Width <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
                && ((composition_data.Version == DWM_DX_SURFACE_INFO_VERSION
                     && composition_data.Pitch >= composition_data.Width * 4)
                    || (composition_data.Version == DWM_DX_SURFACE_INFO_VERSION_GPU
                        && composition_data.Pitch == 0)))
        {
            /* This describes the shared surface. The miniport's private
             * allocation payload is still passed unchanged to OpenResource. */
            data.signature = native_shared_texture_signature;
            data.version = 1;
            data.desc.Width = composition_data.Width;
            data.desc.Height = composition_data.Height;
            data.desc.MipLevels = data.desc.ArraySize = data.desc.SampleDesc.Count = 1;
            data.desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            data.desc.Usage = D3D11_USAGE_DEFAULT;
            data.desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            data.desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        }
        if (data.signature != native_shared_texture_signature || data.version != 1 || !data.desc.Width
                || !data.desc.Height || data.desc.Width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
                || data.desc.Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
                || data.desc.MipLevels != 1 || data.desc.ArraySize != 1
                || data.desc.SampleDesc.Count != 1 || !(data.desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED))
            hr = DXGI_ERROR_UNSUPPORTED;
        else if (!(texture = new NativeTexture2D(this)))
            hr = E_OUTOFMEMORY;
        else if (!texture->runtime_handle.handle)
            hr = E_OUTOFMEMORY;
        else
        {
            texture->desc = data.desc;
            if (data.desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) texture->usage |= DXGI_USAGE_SHADER_INPUT;
            if (data.desc.BindFlags & D3D11_BIND_RENDER_TARGET) texture->usage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;
            D3D10DDIARG_OPENRESOURCE args = {};
            args.NumAllocations = open.NumAllocations;
            args.pOpenAllocationInfo = open.pOpenAllocationInfo;
            args.hKMResource.handle = open.hResource;
            args.pPrivateDriverData = open.pResourcePrivateDriverData;
            args.PrivateDriverDataSize = open.ResourcePrivateDriverDataSize;
            SIZE_T size = functions.pfnCalcPrivateOpenedResourceSize(driver_device, &args);
            texture->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
            texture->mapped = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 1));
            if (!texture->handle.pDrvPrivate || !texture->mapped)
                hr = E_OUTOFMEMORY;
            else if (SUCCEEDED(hr = adopt_resource(runtime_device, texture->runtime_handle.handle, open.hResource, open.hGlobalShare)))
            {
                open.hResource = 0;
                texture->registered = true;
                BeginCall();
                functions.pfnOpenResource(driver_device, &args, texture->handle, texture->runtime_handle);
                if (SUCCEEDED(hr = operation_error))
                {
                    texture->created = true;
                    hr = texture->QueryInterface(iid, out);
                }
            }
        }
    }
    if (texture) texture->Release();
    if (open.hResource)
    {
        D3DKMT_DESTROYALLOCATION destroy = {};
        destroy.hDevice = km_device;
        destroy.hResource = open.hResource;
        D3DKMTDestroyAllocation(&destroy);
    }
    HeapFree(GetProcessHeap(), 0, open.pOpenAllocationInfo);
    HeapFree(GetProcessHeap(), 0, open.pResourcePrivateDriverData);
    HeapFree(GetProcessHeap(), 0, open.pTotalPrivateDriverDataBuffer);
    return hr;
}

static bool NativeViewSliceRange(UINT first, UINT &count, UINT total)
{
    if (first >= total) return false;
    if (count == ~0u) count = total - first;
    return count && count <= total - first;
}

static DXGI_FORMAT NativeFormatFamily(DXGI_FORMAT format)
{
#define NATIVE_FORMAT_FAMILY(first, last) \
    if (format >= DXGI_FORMAT_##first##_TYPELESS && format <= DXGI_FORMAT_##last) return DXGI_FORMAT_##first##_TYPELESS
    NATIVE_FORMAT_FAMILY(R32G32B32A32, R32G32B32A32_SINT);
    NATIVE_FORMAT_FAMILY(R32G32B32, R32G32B32_SINT);
    NATIVE_FORMAT_FAMILY(R16G16B16A16, R16G16B16A16_SINT);
    NATIVE_FORMAT_FAMILY(R32G32, R32G32_SINT);
    NATIVE_FORMAT_FAMILY(R32G8X24, X32_TYPELESS_G8X24_UINT);
    NATIVE_FORMAT_FAMILY(R10G10B10A2, R10G10B10A2_UINT);
    NATIVE_FORMAT_FAMILY(R8G8B8A8, R8G8B8A8_SINT);
    NATIVE_FORMAT_FAMILY(R16G16, R16G16_SINT);
    NATIVE_FORMAT_FAMILY(R32, R32_SINT);
    NATIVE_FORMAT_FAMILY(R24G8, X24_TYPELESS_G8_UINT);
    NATIVE_FORMAT_FAMILY(R8G8, R8G8_SINT);
    NATIVE_FORMAT_FAMILY(R16, R16_SINT);
    NATIVE_FORMAT_FAMILY(R8, R8_SINT);
    NATIVE_FORMAT_FAMILY(BC1, BC1_UNORM_SRGB);
    NATIVE_FORMAT_FAMILY(BC2, BC2_UNORM_SRGB);
    NATIVE_FORMAT_FAMILY(BC3, BC3_UNORM_SRGB);
    NATIVE_FORMAT_FAMILY(BC4, BC4_SNORM);
    NATIVE_FORMAT_FAMILY(BC5, BC5_SNORM);
    NATIVE_FORMAT_FAMILY(BC6H, BC6H_SF16);
    NATIVE_FORMAT_FAMILY(BC7, BC7_UNORM_SRGB);
#undef NATIVE_FORMAT_FAMILY
    if (format == DXGI_FORMAT_B8G8R8A8_TYPELESS || format == DXGI_FORMAT_B8G8R8A8_UNORM
            || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    if (format == DXGI_FORMAT_B8G8R8X8_TYPELESS || format == DXGI_FORMAT_B8G8R8X8_UNORM
            || format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) return DXGI_FORMAT_B8G8R8X8_TYPELESS;
    return DXGI_FORMAT_UNKNOWN;
}

static bool NativeViewFormatCompatible(const NativeTextureInfo &texture, DXGI_FORMAT format)
{
    DXGI_FORMAT family = NativeFormatFamily(format);
    if (format == DXGI_FORMAT_UNKNOWN || format == family) return false;
    if (format == texture.desc.Format || family == texture.desc.Format) return true;
    if (texture.desc.Format == DXGI_FORMAT_NV12 || texture.desc.Format == DXGI_FORMAT_NV11)
        return format == DXGI_FORMAT_R8_UNORM || format == DXGI_FORMAT_R8_UINT
                || format == DXGI_FORMAT_R8G8_UNORM || format == DXGI_FORMAT_R8G8_UINT;
    if (texture.desc.Format == DXGI_FORMAT_P010 || texture.desc.Format == DXGI_FORMAT_P016)
        return format == DXGI_FORMAT_R16_UNORM || format == DXGI_FORMAT_R16_UINT
                || format == DXGI_FORMAT_R16G16_UNORM || format == DXGI_FORMAT_R16G16_UINT;
    if (texture.desc.Format == DXGI_FORMAT_AYUV || texture.desc.Format == DXGI_FORMAT_YUY2)
        return format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UINT;
    if (texture.desc.Format == DXGI_FORMAT_Y410)
        return format == DXGI_FORMAT_R10G10B10A2_UNORM || format == DXGI_FORMAT_R10G10B10A2_UINT;
    if (texture.desc.Format == DXGI_FORMAT_Y416 || texture.desc.Format == DXGI_FORMAT_Y210
            || texture.desc.Format == DXGI_FORMAT_Y216)
        return format == DXGI_FORMAT_R16G16B16A16_UNORM || format == DXGI_FORMAT_R16G16B16A16_UINT;
    /* DXGI back buffers allow the UNORM/sRGB view pair without a typeless
     * allocation. Ordinary fully typed resources do not allow this cast. */
    if (texture.dimension == D3D10DDIRESOURCE_TEXTURE2D)
    {
        NativeTexture2D *object = static_cast<NativeTexture2D *>(static_cast<ID3D11Texture2D *>(texture.resource));
        if ((object->usage & DXGI_USAGE_BACK_BUFFER) && family == NativeFormatFamily(texture.desc.Format))
        {
            DXGI_FORMAT unorm = family == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM
                    : family == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM
                    : family == DXGI_FORMAT_B8G8R8X8_TYPELESS ? DXGI_FORMAT_B8G8R8X8_UNORM : DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT srgb = family == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                    : family == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                    : family == DXGI_FORMAT_B8G8R8X8_TYPELESS ? DXGI_FORMAT_B8G8R8X8_UNORM_SRGB : DXGI_FORMAT_UNKNOWN;
            return unorm != DXGI_FORMAT_UNKNOWN && (format == unorm || format == srgb)
                    && (texture.desc.Format == unorm || texture.desc.Format == srgb);
        }
    }
    return false;
}

static bool NativeBufferViewRange(UINT first, UINT count, UINT stride, UINT bytes)
{
    /* Divide first to avoid overflowing either element-to-byte product. */
    return stride && count && first < bytes / stride && count <= bytes / stride - first;
}

static HRESULT NativeBufferViewStride(NativeDevice *device, DXGI_FORMAT format, bool target, UINT *stride)
{
    if (format == DXGI_FORMAT_UNKNOWN || format == NativeFormatFamily(format)) return E_INVALIDARG;
    UINT support = 0, rows = 0;
    HRESULT hr = device->CheckFormatSupport(format, &support);
    if (FAILED(hr)) return hr;
    if (!(support & D3D11_FORMAT_SUPPORT_BUFFER)
            || (target && !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET))
            || !NativeUploadFormat(format, 1, 1, stride, &rows) || rows != 1) return E_INVALIDARG;
    return S_OK;
}

static HRESULT NativeBuildRenderTargetView(NativeDevice *device, ID3D11Resource *resource,
        const D3D10DDIARG_CREATERENDERTARGETVIEW &args, const D3D11_RENDER_TARGET_VIEW_DESC &desc, ID3D11RenderTargetView **out)
{
    if (!out) return S_FALSE;
    NativeLock guard(device);
    NativeRenderTargetView *view = new NativeRenderTargetView(device);
    if (!view) return E_OUTOFMEMORY;
    view->texture = resource;
    NativeRetainResource(resource);
    view->resource_handle = args.hDrvResource;
    view->desc = desc;
    view->desc.Format = args.Format;
    SIZE_T size = device->functions.pfnCalcPrivateRenderTargetViewSize(device->driver_device, &args);
    view->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!view->handle.pDrvPrivate) { view->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTRENDERTARGETVIEW runtime_view = {view};
    device->BeginCall();
    device->functions.pfnCreateRenderTargetView(device->driver_device, &args, view->handle, runtime_view);
    HRESULT hr = device->operation_error;
    if (FAILED(hr)) { view->Release(); return hr; }
    view->created = true;
    *out = view;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateRenderTargetView(ID3D11Resource *resource,
        const D3D11_RENDER_TARGET_VIEW_DESC *input, ID3D11RenderTargetView **out)
{
    if (out) *out = NULL;
    if (NativeBuffer *buffer = GetNativeBuffer(resource, this))
    {
        if (!input || input->ViewDimension != D3D11_RTV_DIMENSION_BUFFER
                || !(buffer->desc.BindFlags & D3D11_BIND_RENDER_TARGET)) return E_INVALIDARG;
        UINT stride = 0;
        HRESULT hr = NativeBufferViewStride(this, input->Format, true, &stride);
        if (FAILED(hr)) return hr;
        if (!NativeBufferViewRange(input->Buffer.FirstElement, input->Buffer.NumElements, stride, buffer->desc.ByteWidth))
            return E_INVALIDARG;
        D3D10DDIARG_CREATERENDERTARGETVIEW args = {};
        args.hDrvResource = buffer->handle;
        args.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
        args.Format = input->Format;
        args.Buffer.FirstElement = input->Buffer.FirstElement;
        args.Buffer.NumElements = input->Buffer.NumElements;
        return NativeBuildRenderTargetView(this, resource, args, *input, out);
    }
    NativeTextureInfo info;
    NativeTextureInfo *texture = GetNativeTexture(resource, this, &info);
    if (!texture || !(texture->desc.BindFlags & D3D11_BIND_RENDER_TARGET)) return E_INVALIDARG;
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    if (input) desc = *input;
    else
    {
        desc.Format = texture->desc.Format;
        if (texture->dimension == D3D10DDIRESOURCE_TEXTURE1D)
        {
            desc.ViewDimension = texture->desc.ArraySize > 1 ? D3D11_RTV_DIMENSION_TEXTURE1DARRAY : D3D11_RTV_DIMENSION_TEXTURE1D;
            desc.Texture1DArray.ArraySize = texture->desc.ArraySize;
        }
        else if (texture->dimension == D3D10DDIRESOURCE_TEXTURE3D)
        {
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
            desc.Texture3D.WSize = texture->depth;
        }
        else if (texture->desc.SampleDesc.Count > 1)
            desc.ViewDimension = texture->desc.ArraySize > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D11_RTV_DIMENSION_TEXTURE2DMS;
        else
            desc.ViewDimension = texture->desc.ArraySize > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
        if (desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY) desc.Texture2DArray.ArraySize = texture->desc.ArraySize;
        if (desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY) desc.Texture2DMSArray.ArraySize = texture->desc.ArraySize;
    }
    D3D10DDIARG_CREATERENDERTARGETVIEW args = {};
    args.hDrvResource = texture->handle;
    args.Format = desc.Format == DXGI_FORMAT_UNKNOWN ? texture->desc.Format : desc.Format;
    if (!NativeViewFormatCompatible(*texture, args.Format)) return E_INVALIDARG;
    UINT support = 0;
    HRESULT format_status = CheckFormatSupport(args.Format, &support);
    if (FAILED(format_status)) return format_status;
    if (!(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)) return E_INVALIDARG;
    args.ResourceDimension = texture->dimension;
    if ((desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE1D || desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE1DARRAY)
            != (texture->dimension == D3D10DDIRESOURCE_TEXTURE1D)) return E_INVALIDARG;
    if ((desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE3D) != (texture->dimension == D3D10DDIRESOURCE_TEXTURE3D)) return E_INVALIDARG;
    switch (desc.ViewDimension)
    {
        /* 1D and 2D view unions have identical mip/array fields. */
        case D3D11_RTV_DIMENSION_TEXTURE1D:
        case D3D11_RTV_DIMENSION_TEXTURE2D:
            if (texture->desc.SampleDesc.Count != 1 || texture->desc.ArraySize != 1) return E_INVALIDARG;
            args.Tex2D.MipSlice = desc.Texture2D.MipSlice;
            args.Tex2D.ArraySize = 1;
            break;
        case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
        case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
            if (texture->desc.SampleDesc.Count != 1) return E_INVALIDARG;
            if (!NativeViewSliceRange(desc.Texture2DArray.FirstArraySlice, desc.Texture2DArray.ArraySize,
                    texture->desc.ArraySize)) return E_INVALIDARG;
            args.Tex2D.MipSlice = desc.Texture2DArray.MipSlice;
            args.Tex2D.FirstArraySlice = desc.Texture2DArray.FirstArraySlice;
            args.Tex2D.ArraySize = desc.Texture2DArray.ArraySize;
            break;
        case D3D11_RTV_DIMENSION_TEXTURE2DMS:
            args.Tex2D.ArraySize = 1;
            break;
        case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
            if (!NativeViewSliceRange(desc.Texture2DMSArray.FirstArraySlice, desc.Texture2DMSArray.ArraySize,
                    texture->desc.ArraySize)) return E_INVALIDARG;
            args.Tex2D.FirstArraySlice = desc.Texture2DMSArray.FirstArraySlice;
            args.Tex2D.ArraySize = desc.Texture2DMSArray.ArraySize;
            break;
        case D3D11_RTV_DIMENSION_TEXTURE3D:
        {
            UINT mip = desc.Texture3D.MipSlice;
            if (mip >= texture->desc.MipLevels) return E_INVALIDARG;
            UINT depth = max(1u, texture->depth >> mip);
            if (desc.Texture3D.FirstWSlice >= depth) return E_INVALIDARG;
            if (desc.Texture3D.WSize == ~0u) desc.Texture3D.WSize = depth - desc.Texture3D.FirstWSlice;
            if (!desc.Texture3D.WSize || desc.Texture3D.WSize > depth - desc.Texture3D.FirstWSlice) return E_INVALIDARG;
            args.Tex3D.MipSlice = mip;
            args.Tex3D.FirstW = desc.Texture3D.FirstWSlice;
            args.Tex3D.WSize = desc.Texture3D.WSize;
            break;
        }
        default: return E_INVALIDARG;
    }
    if (texture->dimension != D3D10DDIRESOURCE_TEXTURE3D && (args.Tex2D.MipSlice >= texture->desc.MipLevels || !args.Tex2D.ArraySize
            || args.Tex2D.FirstArraySlice >= texture->desc.ArraySize
            || args.Tex2D.ArraySize > texture->desc.ArraySize - args.Tex2D.FirstArraySlice)) return E_INVALIDARG;
    return NativeBuildRenderTargetView(this, resource, args, desc, out);
}

HRESULT STDMETHODCALLTYPE NativeDevice::CheckFormatSupport(DXGI_FORMAT format, UINT *support)
{
    if (!support) return E_INVALIDARG;
    *support = 0;
    if (!functions.pfnCheckFormatSupport) return E_NOTIMPL;
    NativeLock guard(this);
    BeginCall();
    UINT ddi_support = 0;
    functions.pfnCheckFormatSupport(driver_device, format, &ddi_support);
    if (FAILED(operation_error)) return operation_error;
    /* The DDI describes format capabilities; API dimension/use flags are
     * derived by the runtime, rather than exposed as the DDI bit mask. */
    if (ddi_support & 0x1) *support |= D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
    if (ddi_support & 0x2) *support |= D3D11_FORMAT_SUPPORT_RENDER_TARGET;
    if (ddi_support & 0x4) *support |= D3D11_FORMAT_SUPPORT_BLENDABLE;
    if (ddi_support & 0x8) *support |= D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
    if (ddi_support & 0x10) *support |= D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD;
    /* Core vertex formats are required by feature level 10.0 and later.
     * CheckFormatSupport's DDI vertex bit describes additional format support;
     * it is not a complete list of the runtime's input-assembler capabilities.
     * In particular, Intel returns only SHADER_SAMPLE for R32G32B32_FLOAT. */
    if (NativeVertexFormatSize(format) || (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_VERTEX_BUFFER))
        *support |= D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER;
    if (format == DXGI_FORMAT_R16_UINT || format == DXGI_FORMAT_R32_UINT)
        *support |= D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER;
    if (ddi_support & 0x200) *support |= D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
    if (NativeBufferFormatSize(format) || (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_BUFFER))
        *support |= D3D11_FORMAT_SUPPORT_BUFFER | D3D11_FORMAT_SUPPORT_SHADER_LOAD;
    if (ddi_support & 0x4000) *support |= D3D11_FORMAT_SUPPORT_SHADER_GATHER;
    if (!(ddi_support & 0x80000000) && (ddi_support & 0x7))
    {
        *support |= D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_TEXTURECUBE
                | D3D11_FORMAT_SUPPORT_MIP | D3D11_FORMAT_SUPPORT_SHADER_LOAD;
        bool block_compressed = (format >= DXGI_FORMAT_BC1_TYPELESS && format <= DXGI_FORMAT_BC5_SNORM)
                || (format >= DXGI_FORMAT_BC6H_TYPELESS && format <= DXGI_FORMAT_BC7_UNORM_SRGB);
        bool depth_stencil = NativeDepthResourceFormat(format) != DXGI_FORMAT_UNKNOWN
                || (format >= DXGI_FORMAT_R32G8X24_TYPELESS && format <= DXGI_FORMAT_X32_TYPELESS_G8X24_UINT)
                || (format >= DXGI_FORMAT_R24G8_TYPELESS && format <= DXGI_FORMAT_X24_TYPELESS_G8_UINT);
        if (!block_compressed && !depth_stencil) *support |= D3D11_FORMAT_SUPPORT_TEXTURE3D;
    }
    /* The DDI has no depth-stencil bit. For a supported typed depth format,
     * its output-merger role follows from the format itself. */
    if (!(ddi_support & 0x80000000) && NativeDepthResourceFormat(format) != DXGI_FORMAT_UNKNOWN)
        *support |= D3D11_FORMAT_SUPPORT_DEPTH_STENCIL | D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_MIP;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CheckMultisampleQualityLevels(DXGI_FORMAT format, UINT samples, UINT *quality)
{
    if (!quality) return E_INVALIDARG;
    *quality = 0;
    if (!samples || samples > D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT) return E_INVALIDARG;
    if (!functions.pfnCheckMultisampleQualityLevels) return E_NOTIMPL;
    NativeLock guard(this);
    BeginCall();
    functions.pfnCheckMultisampleQualityLevels(driver_device, format, samples, quality);
    return operation_error;
}

static bool NativeFeatureData(void *data, UINT size, SIZE_T expected)
{
    return data && size == expected;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CheckFeatureSupport(D3D11_FEATURE feature, void *data, UINT size)
{
    switch (feature)
    {
        case D3D11_FEATURE_THREADING:
        {
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_THREADING))) return E_INVALIDARG;
            D3D11_FEATURE_DATA_THREADING *caps = static_cast<D3D11_FEATURE_DATA_THREADING *>(data);
            caps->DriverConcurrentCreates = !!(threading_caps & D3D11DDICAPS_FREETHREADED);
            caps->DriverCommandLists = !!(threading_caps
                    & (D3D11DDICAPS_COMMANDLISTS | D3D11DDICAPS_COMMANDLISTS_BUILD_2));
            return S_OK;
        }
        case D3D11_FEATURE_DOUBLES:
        {
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_DOUBLES))) return E_INVALIDARG;
            static_cast<D3D11_FEATURE_DATA_DOUBLES *>(data)->DoublePrecisionFloatShaderOps =
                    !!(shader_caps & D3D11DDICAPS_SHADER_DOUBLES);
            return S_OK;
        }
        case D3D11_FEATURE_FORMAT_SUPPORT:
        {
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT))) return E_INVALIDARG;
            D3D11_FEATURE_DATA_FORMAT_SUPPORT *caps = static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT *>(data);
            return CheckFormatSupport(caps->InFormat, &caps->OutFormatSupport);
        }
        case D3D11_FEATURE_FORMAT_SUPPORT2:
        {
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT2))) return E_INVALIDARG;
            static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT2 *>(data)->OutFormatSupport2 = 0;
            return S_OK;
        }
        case D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS:
        {
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS))) return E_INVALIDARG;
            D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS *caps =
                    static_cast<D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS *>(data);
            caps->ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x = !!(shader_caps
                    & D3D11DDICAPS_SHADER_COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X);
            return S_OK;
        }
        case D3D11_FEATURE_D3D11_OPTIONS:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        case D3D11_FEATURE_ARCHITECTURE_INFO:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_ARCHITECTURE_INFO))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        case D3D11_FEATURE_D3D9_OPTIONS:
        {
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_D3D9_OPTIONS))) return E_INVALIDARG;
            static_cast<D3D11_FEATURE_DATA_D3D9_OPTIONS *>(data)->FullNonPow2TextureSupport = TRUE;
            return S_OK;
        }
        case D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        case D3D11_FEATURE_D3D9_SHADOW_SUPPORT:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        case D3D11_FEATURE_D3D11_OPTIONS1:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS1))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        case D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT:
        {
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT))) return E_INVALIDARG;
            static_cast<D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT *>(data)->SimpleInstancingSupported = TRUE;
            return S_OK;
        }
        case D3D11_FEATURE_MARKER_SUPPORT:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_MARKER_SUPPORT))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        case D3D11_FEATURE_D3D9_OPTIONS1:
        {
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_D3D9_OPTIONS1))) return E_INVALIDARG;
            D3D11_FEATURE_DATA_D3D9_OPTIONS1 *caps = static_cast<D3D11_FEATURE_DATA_D3D9_OPTIONS1 *>(data);
            ZeroMemory(caps, sizeof(*caps));
            caps->FullNonPow2TextureSupported = TRUE;
            caps->SimpleInstancingSupported = TRUE;
            return S_OK;
        }
        case D3D11_FEATURE_D3D11_OPTIONS2:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS2))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        case D3D11_FEATURE_D3D11_OPTIONS3:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS3))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        case D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        case D3D11_FEATURE_SHADER_CACHE:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_SHADER_CACHE))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        case D3D11_FEATURE_D3D11_OPTIONS5:
            if (!NativeFeatureData(data, size, sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS5))) return E_INVALIDARG;
            ZeroMemory(data, size);
            return S_OK;
        default:
            return E_NOTIMPL;
    }
}

HRESULT STDMETHODCALLTYPE NativeContext::Map(ID3D11Resource *resource, UINT subresource,
        D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped)
{
    if (deferred) return MapDeferred(resource, subresource, type, flags, mapped);

    if (!mapped) return E_INVALIDARG;
    ZeroMemory(mapped, sizeof(*mapped));
    if (NativeBuffer *buffer = GetNativeBuffer(resource, device))
        return MapBuffer(device, buffer, subresource, type, flags, mapped);
    NativeTextureInfo info;
    NativeTextureInfo *texture = GetNativeTexture(resource, device, &info);
    if (!texture || subresource >= texture->desc.MipLevels * texture->desc.ArraySize || texture->mapped[subresource]
            || (flags & ~D3D11_MAP_FLAG_DO_NOT_WAIT)) return E_INVALIDARG;
    if (type < D3D11_MAP_READ || type > D3D11_MAP_WRITE_NO_OVERWRITE) return E_INVALIDARG;
    if ((type == D3D11_MAP_READ || type == D3D11_MAP_READ_WRITE) && !(texture->desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ)) return E_INVALIDARG;
    if (type != D3D11_MAP_READ && !(texture->desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE)) return E_INVALIDARG;
    if (type == D3D11_MAP_WRITE_NO_OVERWRITE) return E_INVALIDARG;
    if (type == D3D11_MAP_WRITE_DISCARD && texture->desc.Usage != D3D11_USAGE_DYNAMIC) return E_INVALIDARG;
    NativeLock guard(device);
    device->BeginCall();
    D3D10DDI_MAPPED_SUBRESOURCE result = {};
    PFND3D10DDI_RESOURCEMAP map = texture->desc.Usage == D3D11_USAGE_STAGING
            ? device->functions.pfnStagingResourceMap : device->functions.pfnDynamicResourceMapDiscard;
    if (!map) return E_NOTIMPL;
    map(device->driver_device, texture->handle, subresource, static_cast<D3D10_DDI_MAP>(type), flags, &result);
    if (FAILED(device->operation_error)) return device->operation_error;
    if (!result.pData) return E_FAIL;
    mapped->pData = result.pData;
    mapped->RowPitch = result.RowPitch;
    mapped->DepthPitch = result.DepthPitch;
    texture->mapped[subresource] = 1;
    return S_OK;
}

void STDMETHODCALLTYPE NativeContext::Unmap(ID3D11Resource *resource, UINT subresource)
{
    if (deferred) { UnmapDeferred(resource, subresource); return; }

    if (NativeBuffer *buffer = GetNativeBuffer(resource, device))
    {
        if (subresource || !buffer->mapped) return;
        PFND3D10DDI_RESOURCEUNMAP unmap = buffer->desc.Usage == D3D11_USAGE_STAGING ? device->functions.pfnStagingResourceUnmap
                : (buffer->desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) ? device->functions.pfnDynamicConstantBufferUnmap
                : device->functions.pfnDynamicIABufferUnmap;
        if (!unmap) { Unimplemented("Unmap"); return; }
        NativeLock guard(device);
        unmap(device->driver_device, buffer->handle, 0);
        buffer->mapped = false;
        return;
    }
    NativeTextureInfo info;
    NativeTextureInfo *texture = GetNativeTexture(resource, device, &info);
    if (!texture || subresource >= texture->desc.MipLevels * texture->desc.ArraySize || !texture->mapped[subresource]) return;
    NativeLock guard(device);
    PFND3D10DDI_RESOURCEUNMAP unmap = texture->desc.Usage == D3D11_USAGE_STAGING
            ? device->functions.pfnStagingResourceUnmap : device->functions.pfnDynamicResourceUnmap;
    if (!unmap) { Unimplemented("Unmap"); return; }
    unmap(device->driver_device, texture->handle, subresource);
    texture->mapped[subresource] = 0;
}

void STDMETHODCALLTYPE NativeContext::ClearRenderTargetView(ID3D11RenderTargetView *target, const FLOAT color[4])
{
    if (deferred)
    {
        if (!target || !color) return;
        NativeLock guard(device);
        NativeCommandRef<ID3D11RenderTargetView> retained(target);
        NativeCommandArray<FLOAT> values(4, color);
        if (Captured(values)) Record([=](NativeContext *context) { context->ClearRenderTargetView(retained.Get(), values.Data()); });
        return;
    }

    if (!target || !color) return;
    NativeRenderTargetView *view = static_cast<NativeRenderTargetView *>(target);
    if (view->device != device) return;
    NativeLock guard(device);
    FLOAT value[4];
    memcpy(value, color, sizeof(value));
    device->functions.pfnClearRenderTargetView(device->driver_device, view->handle, value);
}

void STDMETHODCALLTYPE NativeContext::GenerateMips(ID3D11ShaderResourceView *resource_view)
{
    if (deferred)
    {
        NativeLock guard(device);
        NativeCommandRef<ID3D11ShaderResourceView> retained(resource_view);
        Record([=](NativeContext *context) { context->GenerateMips(retained.Get()); });
        return;
    }

    if (!resource_view || !device->functions.pfnGenMips) return;
    NativeShaderResourceView *view = static_cast<NativeShaderResourceView *>(resource_view);
    NativeTextureInfo info;
    NativeTextureInfo *texture = GetNativeTexture(view->texture, device, &info);
    if (view->device != device || !view->created || !texture
            || texture->desc.MipLevels <= 1
            || !(texture->desc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS)
            || !(texture->desc.BindFlags & D3D11_BIND_RENDER_TARGET)) return;
    NativeLock guard(device);
    device->BeginCall();
    device->functions.pfnGenMips(device->driver_device, view->handle);
}

void STDMETHODCALLTYPE NativeContext::SetResourceMinLOD(ID3D11Resource *resource, FLOAT min_lod)
{
    /* This DDI is excluded from deferred-context function tables. Ignore the
     * invalid call without marking the rest of the command list unsupported. */
    if (deferred) return;
    NativeTextureInfo info;
    NativeLock guard(device);
    if (!GetNativeTexture(resource, device, &info)
            || !(info.desc.MiscFlags & D3D11_RESOURCE_MISC_RESOURCE_CLAMP)) return;
    if (!device->functions.pfnSetResourceMinLOD) { Unimplemented("SetResourceMinLOD DDI"); return; }
    device->BeginCall();
    device->functions.pfnSetResourceMinLOD(device->driver_device, info.handle, min_lod);
    if (SUCCEEDED(device->operation_error)) *info.min_lod = min_lod;
}

FLOAT STDMETHODCALLTYPE NativeContext::GetResourceMinLOD(ID3D11Resource *resource)
{
    if (deferred) return 0;
    NativeTextureInfo info;
    NativeLock guard(device);
    if (!GetNativeTexture(resource, device, &info)) return 0;
    return *info.min_lod;
}

void STDMETHODCALLTYPE NativeContext::CopyResource(ID3D11Resource *dst, ID3D11Resource *src)
{
    if (deferred)
    {
        NativeLock guard(device);
        NativeCommandRef<ID3D11Resource> destination(dst), source(src);
        Record([=](NativeContext *context) { context->CopyResource(destination.Get(), source.Get()); });
        return;
    }

    if (NativeBuffer *d = GetNativeBuffer(dst, device))
    {
        NativeBuffer *s = GetNativeBuffer(src, device);
        if (!s || s == d || d->desc.ByteWidth != s->desc.ByteWidth || d->mapped || s->mapped || d->desc.Usage == D3D11_USAGE_IMMUTABLE) return;
        NativeLock guard(device);
        device->functions.pfnResourceCopy(device->driver_device, d->handle, s->handle);
        return;
    }
    NativeTextureInfo dst_info, src_info;
    NativeTextureInfo *d = GetNativeTexture(dst, device, &dst_info), *s = GetNativeTexture(src, device, &src_info);
    if (!d || !s || dst == src || d->depth != s->depth
            || (d->dimension == D3D10DDIRESOURCE_TEXTURE3D) != (s->dimension == D3D10DDIRESOURCE_TEXTURE3D)
            || d->desc.Width != s->desc.Width || d->desc.Height != s->desc.Height
            || d->desc.ArraySize != s->desc.ArraySize || d->desc.MipLevels != s->desc.MipLevels
            || (d->desc.Format != s->desc.Format
                && (NativeFormatFamily(d->desc.Format) == DXGI_FORMAT_UNKNOWN
                    || NativeFormatFamily(d->desc.Format) != NativeFormatFamily(s->desc.Format)))
            || d->desc.SampleDesc.Count != s->desc.SampleDesc.Count
            || d->desc.SampleDesc.Quality != s->desc.SampleDesc.Quality || d->desc.Usage == D3D11_USAGE_IMMUTABLE) return;
    if ((d->desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) && device->feature_level < D3D_FEATURE_LEVEL_10_1) return;
    NativeLock guard(device);
    device->functions.pfnResourceCopy(device->driver_device, d->handle, s->handle);
}

void STDMETHODCALLTYPE NativeContext::CopySubresourceRegion(ID3D11Resource *dst, UINT dst_subresource,
        UINT x, UINT y, UINT z, ID3D11Resource *src, UINT src_subresource, const D3D11_BOX *box)
{
    if (deferred)
    {
        NativeLock guard(device);
        NativeCommandRef<ID3D11Resource> destination(dst), source(src);
        NativeCommandArray<D3D11_BOX> region(box ? 1 : 0, box);
        if (!Captured(region)) return;
        Record([=](NativeContext *context) { context->CopySubresourceRegion(destination.Get(), dst_subresource, x, y, z, source.Get(), src_subresource, region.Data()); });
        return;
    }

    if (NativeBuffer *db = GetNativeBuffer(dst, device))
    {
        /* A buffer is one subresource addressed in bytes along the box's x axis. */
        NativeBuffer *sb = GetNativeBuffer(src, device);
        if (!sb || sb == db || dst_subresource || src_subresource || y || z || db->mapped || sb->mapped
                || db->desc.Usage == D3D11_USAGE_IMMUTABLE || !device->functions.pfnResourceCopyRegion) return;
        D3D10_DDI_BOX bytes = {0, 0, 0, static_cast<LONG>(sb->desc.ByteWidth), 1, 1};
        if (box)
        {
            if (box->left >= box->right || box->top >= box->bottom || box->front >= box->back) return;
            if (box->top || box->bottom != 1 || box->front || box->back != 1 || box->right > sb->desc.ByteWidth) return;
            bytes.left = static_cast<LONG>(box->left);
            bytes.right = static_cast<LONG>(box->right);
        }
        UINT count = static_cast<UINT>(bytes.right - bytes.left);
        if (x > db->desc.ByteWidth || count > db->desc.ByteWidth - x) return;
        NativeLock guard(device);
        device->functions.pfnResourceCopyRegion(device->driver_device, db->handle, 0, x, 0, 0, sb->handle, 0, &bytes);
        return;
    }
    NativeTextureInfo dst_info, src_info;
    NativeTextureInfo *d = GetNativeTexture(dst, device, &dst_info), *s = GetNativeTexture(src, device, &src_info);
    if (!d || !s || dst_subresource >= d->desc.MipLevels * d->desc.ArraySize
            || src_subresource >= s->desc.MipLevels * s->desc.ArraySize || !device->functions.pfnResourceCopyRegion) return;
    if ((d->desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) && device->feature_level < D3D_FEATURE_LEVEL_10_1) return;
    NativeLock guard(device);
    device->functions.pfnResourceCopyRegion(device->driver_device, d->handle, dst_subresource,
            x, y, z, s->handle, src_subresource, reinterpret_cast<const D3D10_DDI_BOX *>(box));
}

void STDMETHODCALLTYPE NativeContext::ResolveSubresource(ID3D11Resource *dst, UINT dst_subresource,
        ID3D11Resource *src, UINT src_subresource, DXGI_FORMAT format)
{
    if (deferred)
    {
        NativeLock guard(device);
        NativeCommandRef<ID3D11Resource> destination(dst), source(src);
        Record([=](NativeContext *context) { context->ResolveSubresource(destination.Get(), dst_subresource, source.Get(), src_subresource, format); });
        return;
    }

    NativeTexture2D *d = NativeTexture(dst, device), *s = NativeTexture(src, device);
    if (!d || !s || d == s || !device->functions.pfnResourceResolveSubresource
            || d->desc.SampleDesc.Count != 1 || s->desc.SampleDesc.Count <= 1
            || d->desc.Usage != D3D11_USAGE_DEFAULT || format != d->desc.Format || format != s->desc.Format
            || NativeDepthResourceFormat(format) != DXGI_FORMAT_UNKNOWN
            || dst_subresource >= d->desc.MipLevels * d->desc.ArraySize
            || src_subresource >= s->desc.MipLevels * s->desc.ArraySize
            || max(1u, d->desc.Width >> (dst_subresource % d->desc.MipLevels)) != s->desc.Width
            || max(1u, d->desc.Height >> (dst_subresource % d->desc.MipLevels)) != s->desc.Height) return;
    NativeLock guard(device);
    device->BeginCall();
    device->functions.pfnResourceResolveSubresource(device->driver_device, d->handle, dst_subresource,
            s->handle, src_subresource, format);
}

void STDMETHODCALLTYPE NativeContext::UpdateSubresource(ID3D11Resource *resource, UINT subresource,
        const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch)
{
    if (deferred) { UpdateDeferred(resource, subresource, box, data, row_pitch, depth_pitch); return; }

    if (NativeBuffer *buffer = GetNativeBuffer(resource, device))
    {
        if (!data || subresource || buffer->desc.Usage != D3D11_USAGE_DEFAULT || buffer->mapped) return;
        PFND3D10DDI_RESOURCEUPDATESUBRESOURCEUP update = (buffer->desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER)
                ? device->functions.pfnDefaultConstantBufferUpdateSubresourceUP : device->functions.pfnResourceUpdateSubresourceUP;
        if (!update) { Unimplemented("UpdateSubresource"); return; }
        NativeLock guard(device);
        update(device->driver_device, buffer->handle, 0, reinterpret_cast<const D3D10_DDI_BOX *>(box), data, row_pitch, depth_pitch);
        return;
    }
    NativeTextureInfo info;
    NativeTextureInfo *texture = GetNativeTexture(resource, device, &info);
    if (!texture || !data || texture->desc.Usage != D3D11_USAGE_DEFAULT
            || subresource >= texture->desc.MipLevels * texture->desc.ArraySize) return;
    NativeLock guard(device);
    device->functions.pfnResourceUpdateSubresourceUP(device->driver_device, texture->handle,
            subresource, reinterpret_cast<const D3D10_DDI_BOX *>(box), data, row_pitch, depth_pitch);
}

void STDMETHODCALLTYPE NativeContext::CopySubresourceRegion1(ID3D11Resource *dst, UINT dst_subresource,
        UINT x, UINT y, UINT z, ID3D11Resource *src, UINT src_subresource,
        const D3D11_BOX *box, UINT flags)
{
    if (flags) return;
    CopySubresourceRegion(dst, dst_subresource, x, y, z, src, src_subresource, box);
}

void STDMETHODCALLTYPE NativeContext::UpdateSubresource1(ID3D11Resource *resource, UINT subresource,
        const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch, UINT flags)
{
    if (flags) return;
    UpdateSubresource(resource, subresource, box, data, row_pitch, depth_pitch);
}

void STDMETHODCALLTYPE NativeContext::DiscardResource(ID3D11Resource *)
{
    /* Discard is a performance hint.  With DiscardAPIsSeenByDriver false the
     * Windows 11 runtime consumes the call without forwarding it to the DDI. */
}

void STDMETHODCALLTYPE NativeContext::DiscardView(ID3D11View *)
{
}

static bool NativeWholeConstantBufferRange(const UINT *first, const UINT *count)
{
    return !first && !count;
}

void STDMETHODCALLTYPE NativeContext::VSSetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer *const *buffers, const UINT *first, const UINT *constants)
{
    if (NativeWholeConstantBufferRange(first, constants)) VSSetConstantBuffers(start, count, buffers);
}

void STDMETHODCALLTYPE NativeContext::HSSetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer *const *buffers, const UINT *first, const UINT *constants)
{
    if (NativeWholeConstantBufferRange(first, constants)) HSSetConstantBuffers(start, count, buffers);
}

void STDMETHODCALLTYPE NativeContext::DSSetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer *const *buffers, const UINT *first, const UINT *constants)
{
    if (NativeWholeConstantBufferRange(first, constants)) DSSetConstantBuffers(start, count, buffers);
}

void STDMETHODCALLTYPE NativeContext::GSSetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer *const *buffers, const UINT *first, const UINT *constants)
{
    if (NativeWholeConstantBufferRange(first, constants)) GSSetConstantBuffers(start, count, buffers);
}

void STDMETHODCALLTYPE NativeContext::PSSetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer *const *buffers, const UINT *first, const UINT *constants)
{
    if (NativeWholeConstantBufferRange(first, constants)) PSSetConstantBuffers(start, count, buffers);
}

void STDMETHODCALLTYPE NativeContext::CSSetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer *const *buffers, const UINT *first, const UINT *constants)
{
    if (NativeWholeConstantBufferRange(first, constants)) CSSetConstantBuffers(start, count, buffers);
}

static void NativeGetConstantBuffers1(NativeDevice *device, ID3D11Buffer *const *slots,
        UINT capacity, UINT start, UINT count, ID3D11Buffer **buffers,
        UINT *first, UINT *constants)
{
    if (start > capacity || count > capacity - start) return;
    NativeLock guard(device);
    for (UINT i = 0; i < count; ++i)
    {
        ID3D11Buffer *buffer = slots[start + i];
        if (buffers)
        {
            buffers[i] = buffer;
            if (buffer) buffer->AddRef();
        }
        if (first) first[i] = 0;
        if (constants)
        {
            NativeBuffer *native = GetNativeBuffer(buffer, device);
            constants[i] = native ? min(native->desc.ByteWidth / 16,
                    static_cast<UINT>(D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT)) : 0;
        }
    }
}

void STDMETHODCALLTYPE NativeContext::VSGetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer **buffers, UINT *first, UINT *constants)
{
    NativeGetConstantBuffers1(device, constant_buffers[0], ARRAYSIZE(constant_buffers[0]),
            start, count, buffers, first, constants);
}

void STDMETHODCALLTYPE NativeContext::HSGetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer **buffers, UINT *first, UINT *constants)
{
    NativeGetConstantBuffers1(device, constant_buffers[3], ARRAYSIZE(constant_buffers[3]),
            start, count, buffers, first, constants);
}

void STDMETHODCALLTYPE NativeContext::DSGetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer **buffers, UINT *first, UINT *constants)
{
    NativeGetConstantBuffers1(device, constant_buffers[4], ARRAYSIZE(constant_buffers[4]),
            start, count, buffers, first, constants);
}

void STDMETHODCALLTYPE NativeContext::GSGetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer **buffers, UINT *first, UINT *constants)
{
    NativeGetConstantBuffers1(device, constant_buffers[2], ARRAYSIZE(constant_buffers[2]),
            start, count, buffers, first, constants);
}

void STDMETHODCALLTYPE NativeContext::PSGetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer **buffers, UINT *first, UINT *constants)
{
    NativeGetConstantBuffers1(device, constant_buffers[1], ARRAYSIZE(constant_buffers[1]),
            start, count, buffers, first, constants);
}

void STDMETHODCALLTYPE NativeContext::CSGetConstantBuffers1(UINT start, UINT count,
        ID3D11Buffer **buffers, UINT *first, UINT *constants)
{
    NativeGetConstantBuffers1(device, constant_buffers[5], ARRAYSIZE(constant_buffers[5]),
            start, count, buffers, first, constants);
}

void STDMETHODCALLTYPE NativeContext::ClearView(ID3D11View *view, const FLOAT color[4],
        const D3D11_RECT *rects, UINT)
{
    if (!view || !color || rects) return;
    ID3D11RenderTargetView *target = NULL;
    if (SUCCEEDED(view->QueryInterface(IID_ID3D11RenderTargetView,
            reinterpret_cast<void **>(&target))))
    {
        ClearRenderTargetView(target, color);
        target->Release();
    }
}

void STDMETHODCALLTYPE NativeContext::DiscardView1(ID3D11View *, const D3D11_RECT *, UINT)
{
}

void STDMETHODCALLTYPE NativeContext::Flush() { if (deferred) return; NativeLock guard(device); device->functions.pfnFlush(device->driver_device); }

D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE NativeContext::GetType() { return deferred ? D3D11_DEVICE_CONTEXT_DEFERRED : D3D11_DEVICE_CONTEXT_IMMEDIATE; }
UINT STDMETHODCALLTYPE NativeContext::GetContextFlags() { return 0; }

extern "C" HRESULT d3d11_native_create_device(IDXGIAdapter *adapter, UINT flags,
        const D3D_FEATURE_LEVEL *levels, UINT count, ID3D11Device **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    NativeDevice *device = new NativeDevice;
    if (!device) return E_OUTOFMEMORY;
    HRESULT hr = device->Initialize(adapter, flags, levels, count);
    if (FAILED(hr)) { delete device; return hr; }
    *out = static_cast<ID3D11Device *>(device);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateSamplerState(const D3D11_SAMPLER_DESC *input, ID3D11SamplerState **out)
{
    if (out) *out = NULL;
    if (!input) return E_INVALIDARG;
    if (!functions.pfnCalcPrivateSamplerSize || !functions.pfnCreateSampler || !functions.pfnDestroySampler)
        return E_NOTIMPL;
    D3D11_SAMPLER_DESC desc = *input;
    if (!D3D11_DECODE_IS_ANISOTROPIC_FILTER(desc.Filter)) desc.MaxAnisotropy = 0;
    if (!D3D11_DECODE_IS_COMPARISON_FILTER(desc.Filter)) desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    if (desc.AddressU != D3D11_TEXTURE_ADDRESS_BORDER && desc.AddressV != D3D11_TEXTURE_ADDRESS_BORDER
            && desc.AddressW != D3D11_TEXTURE_ADDRESS_BORDER) memset(desc.BorderColor, 0, sizeof(desc.BorderColor));
    if (!out) return S_FALSE;
    NativeLock guard(this);
    if ((*out = NativeFindState<ID3D11SamplerState>(this, NativeSamplerKind, desc))) return S_OK;
    NativeSampler *state = new NativeSampler(this);
    if (!state) return E_OUTOFMEMORY;
    state->desc = desc;
    state->destroy = functions.pfnDestroySampler;
    static_assert(sizeof(desc) == sizeof(D3D10_DDI_SAMPLER_DESC), "Sampler descriptor ABI");
    const D3D10_DDI_SAMPLER_DESC *ddi_desc = reinterpret_cast<const D3D10_DDI_SAMPLER_DESC *>(&desc);
    SIZE_T size = functions.pfnCalcPrivateSamplerSize(driver_device, ddi_desc);
    state->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!state->handle.pDrvPrivate) { state->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTSAMPLER runtime_state = {state};
    BeginCall();
    functions.pfnCreateSampler(driver_device, ddi_desc, state->handle, runtime_state);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { state->Release(); return hr; }
    state->created = true;
    state->cache_entry.Insert(NativeStateBucket(this, NativeSamplerKind, desc), state, &state->desc, sizeof(desc));
    *out = state;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateBlendState(const D3D11_BLEND_DESC *input, ID3D11BlendState **out)
{
    if (out) *out = NULL;
    if (!input) return E_INVALIDARG;

    D3D11_BLEND_DESC1 desc = {};
    desc.AlphaToCoverageEnable = input->AlphaToCoverageEnable;
    desc.IndependentBlendEnable = input->IndependentBlendEnable;
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        desc.RenderTarget[i].BlendEnable = input->RenderTarget[i].BlendEnable;
        desc.RenderTarget[i].LogicOpEnable = FALSE;
        desc.RenderTarget[i].SrcBlend = input->RenderTarget[i].SrcBlend;
        desc.RenderTarget[i].DestBlend = input->RenderTarget[i].DestBlend;
        desc.RenderTarget[i].BlendOp = input->RenderTarget[i].BlendOp;
        desc.RenderTarget[i].SrcBlendAlpha = input->RenderTarget[i].SrcBlendAlpha;
        desc.RenderTarget[i].DestBlendAlpha = input->RenderTarget[i].DestBlendAlpha;
        desc.RenderTarget[i].BlendOpAlpha = input->RenderTarget[i].BlendOpAlpha;
        desc.RenderTarget[i].LogicOp = D3D11_LOGIC_OP_COPY;
        desc.RenderTarget[i].RenderTargetWriteMask = input->RenderTarget[i].RenderTargetWriteMask;
    }

    ID3D11BlendState1 *state = NULL;
    HRESULT hr = CreateBlendState1(&desc, out ? &state : NULL);
    if (SUCCEEDED(hr) && out) *out = state;
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateBlendState1(const D3D11_BLEND_DESC1 *input,
        ID3D11BlendState1 **out)
{
    if (out) *out = NULL;
    if (!input) return E_INVALIDARG;
    if (!functions.pfnCalcPrivateBlendStateSize || !functions.pfnCreateBlendState
            || !functions.pfnDestroyBlendState) return E_NOTIMPL;

    /* The API ignores render targets 1..7 unless independent blending is
     * enabled, and ignores blend factors and operations for disabled targets.
     * Canonicalize those don't-care fields before crossing the DDI boundary;
     * UMDs receive valid enum values in every slot and GetDesc() exposes the
     * same normalized state as the system D3D11 runtime. */
    D3D11_BLEND_DESC1 desc = {};
    D3D11_BLEND_DESC driver_desc = {};
    desc.AlphaToCoverageEnable = driver_desc.AlphaToCoverageEnable = !!input->AlphaToCoverageEnable;
    desc.IndependentBlendEnable = driver_desc.IndependentBlendEnable = !!input->IndependentBlendEnable;
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        const D3D11_RENDER_TARGET_BLEND_DESC1 &source =
                input->RenderTarget[input->IndependentBlendEnable ? i : 0];
        D3D11_RENDER_TARGET_BLEND_DESC1 &target = desc.RenderTarget[i];
        D3D11_RENDER_TARGET_BLEND_DESC &driver_target = driver_desc.RenderTarget[i];

        /* D3D11_FEATURE_DATA_D3D11_OPTIONS reports OutputMergerLogicOp as
         * false for a D3D11.0 DDI.  The 11.1 runtime interface still exists,
         * but must not manufacture logic-op support that the UMD did not
         * advertise. */
        if (source.LogicOpEnable) return E_INVALIDARG;

        target.BlendEnable = driver_target.BlendEnable = !!source.BlendEnable;
        target.LogicOpEnable = FALSE;
        target.LogicOp = D3D11_LOGIC_OP_COPY;
        target.RenderTargetWriteMask = driver_target.RenderTargetWriteMask = source.RenderTargetWriteMask;
        if (driver_target.BlendEnable)
        {
            target.SrcBlend = driver_target.SrcBlend = source.SrcBlend;
            target.DestBlend = driver_target.DestBlend = source.DestBlend;
            target.BlendOp = driver_target.BlendOp = source.BlendOp;
            target.SrcBlendAlpha = driver_target.SrcBlendAlpha = source.SrcBlendAlpha;
            target.DestBlendAlpha = driver_target.DestBlendAlpha = source.DestBlendAlpha;
            target.BlendOpAlpha = driver_target.BlendOpAlpha = source.BlendOpAlpha;
        }
        else
        {
            target.SrcBlend = driver_target.SrcBlend = D3D11_BLEND_ONE;
            target.SrcBlendAlpha = driver_target.SrcBlendAlpha = D3D11_BLEND_ONE;
            target.DestBlend = driver_target.DestBlend = D3D11_BLEND_ZERO;
            target.DestBlendAlpha = driver_target.DestBlendAlpha = D3D11_BLEND_ZERO;
            target.BlendOp = driver_target.BlendOp = D3D11_BLEND_OP_ADD;
            target.BlendOpAlpha = driver_target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        }
    }

    if (!out) return S_FALSE;
    static_assert(sizeof(D3D11_BLEND_DESC) == sizeof(D3D10_1_DDI_BLEND_DESC), "Blend descriptor ABI");
    const D3D10_1_DDI_BLEND_DESC *ddi_desc =
            reinterpret_cast<const D3D10_1_DDI_BLEND_DESC *>(&driver_desc);
    NativeLock guard(this);
    if ((*out = NativeFindState<ID3D11BlendState1>(this, NativeBlendKind, desc))) return S_OK;
    NativeBlend *state = new NativeBlend(this);
    if (!state) return E_OUTOFMEMORY;
    state->desc = desc;
    state->destroy = functions.pfnDestroyBlendState;
    SIZE_T size = functions.pfnCalcPrivateBlendStateSize(driver_device, ddi_desc);
    state->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!state->handle.pDrvPrivate) { state->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTBLENDSTATE runtime_state = {state};
    BeginCall();
    functions.pfnCreateBlendState(driver_device, ddi_desc, state->handle, runtime_state);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { state->Release(); return hr; }
    state->created = true;
    state->cache_entry.Insert(NativeStateBucket(this, NativeBlendKind, desc), state, &state->desc, sizeof(desc));
    *out = state;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateRasterizerState(const D3D11_RASTERIZER_DESC *input,
        ID3D11RasterizerState **out)
{
    if (out) *out = NULL;
    if (!input) return E_INVALIDARG;
    D3D11_RASTERIZER_DESC1 desc = {};
    memcpy(&desc, input, sizeof(*input));
    desc.ForcedSampleCount = 0;
    ID3D11RasterizerState1 *state = NULL;
    HRESULT hr = CreateRasterizerState1(&desc, out ? &state : NULL);
    if (SUCCEEDED(hr) && out) *out = state;
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateRasterizerState1(const D3D11_RASTERIZER_DESC1 *input,
        ID3D11RasterizerState1 **out)
{
    if (out) *out = NULL;
    if (!input) return E_INVALIDARG;
    if (input->ForcedSampleCount) return E_INVALIDARG;
    if (!functions.pfnCalcPrivateRasterizerStateSize || !functions.pfnCreateRasterizerState
            || !functions.pfnDestroyRasterizerState) return E_NOTIMPL;
    if (!out) return S_FALSE;

    D3D11_RASTERIZER_DESC1 desc = *input;
    desc.FrontCounterClockwise = !!desc.FrontCounterClockwise;
    desc.DepthClipEnable = !!desc.DepthClipEnable;
    desc.ScissorEnable = !!desc.ScissorEnable;
    desc.MultisampleEnable = !!desc.MultisampleEnable;
    desc.AntialiasedLineEnable = !!desc.AntialiasedLineEnable;
    D3D11_RASTERIZER_DESC driver_desc;
    memcpy(&driver_desc, &desc, sizeof(driver_desc));
    static_assert(sizeof(D3D11_RASTERIZER_DESC) == sizeof(D3D10_DDI_RASTERIZER_DESC),
            "Rasterizer descriptor ABI");
    const D3D10_DDI_RASTERIZER_DESC *ddi_desc =
            reinterpret_cast<const D3D10_DDI_RASTERIZER_DESC *>(&driver_desc);
    NativeLock guard(this);
    if ((*out = NativeFindState<ID3D11RasterizerState1>(this, NativeRasterizerKind, desc))) return S_OK;
    NativeRasterizer *state = new NativeRasterizer(this);
    if (!state) return E_OUTOFMEMORY;
    state->desc = desc;
    state->destroy = functions.pfnDestroyRasterizerState;
    SIZE_T size = functions.pfnCalcPrivateRasterizerStateSize(driver_device, ddi_desc);
    state->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!state->handle.pDrvPrivate) { state->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTRASTERIZERSTATE runtime_state = {state};
    BeginCall();
    functions.pfnCreateRasterizerState(driver_device, ddi_desc, state->handle, runtime_state);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { state->Release(); return hr; }
    state->created = true;
    state->cache_entry.Insert(NativeStateBucket(this, NativeRasterizerKind, desc), state, &state->desc, sizeof(desc));
    *out = state;
    return S_OK;
}

static UINT ReadDword(const BYTE *data)
{
    UINT value;
    memcpy(&value, data, sizeof(value));
    return value;
}

static UINT NativeSignatureSystemValue(UINT name, UINT index)
{
    /* DXBC reflection groups tessellation factors by semantic; the DDI uses
     * the scalar system-value names from the shader instruction encoding. */
    switch (name)
    {
        case D3D_NAME_FINAL_QUAD_EDGE_TESSFACTOR:
            return D3D11_SB_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR + index;
        case D3D_NAME_FINAL_QUAD_INSIDE_TESSFACTOR:
            return D3D11_SB_NAME_FINAL_QUAD_U_INSIDE_TESSFACTOR + index;
        case D3D_NAME_FINAL_TRI_EDGE_TESSFACTOR:
            return D3D11_SB_NAME_FINAL_TRI_U_EQ_0_EDGE_TESSFACTOR + index;
        case D3D_NAME_FINAL_TRI_INSIDE_TESSFACTOR:
            return D3D11_SB_NAME_FINAL_TRI_INSIDE_TESSFACTOR;
        case D3D_NAME_FINAL_LINE_DETAIL_TESSFACTOR:
            return D3D11_SB_NAME_FINAL_LINE_DETAIL_TESSFACTOR;
        case D3D_NAME_FINAL_LINE_DENSITY_TESSFACTOR:
            return D3D11_SB_NAME_FINAL_LINE_DENSITY_TESSFACTOR;
        default:
            return name;
    }
}

class NativeShaderBytecode
{
    const BYTE *output_signature = NULL;
    UINT output_signature_size = 0, output_signature_stride = 0;
    bool output_signature_streams = false;
public:
    const UINT *tokens = NULL;
    D3D10DDIARG_STAGE_IO_SIGNATURES signatures = {};
    D3D11_1DDIARG_STAGE_IO_SIGNATURES modern_signatures = {};
    D3D11DDIARG_TESSELLATION_IO_SIGNATURES tessellation = {};
    D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES modern_tessellation = {};
    ~NativeShaderBytecode()
    {
        HeapFree(GetProcessHeap(), 0, signatures.pInputSignature);
        HeapFree(GetProcessHeap(), 0, signatures.pOutputSignature);
        HeapFree(GetProcessHeap(), 0, modern_signatures.pInputSignature);
        HeapFree(GetProcessHeap(), 0, modern_signatures.pOutputSignature);
        HeapFree(GetProcessHeap(), 0, tessellation.pPatchConstantSignature);
        HeapFree(GetProcessHeap(), 0, modern_tessellation.pPatchConstantSignature);
    }
    HRESULT ReadSignature(const BYTE *data, UINT size, UINT tag, UINT kind)
    {
        if (size < 8) return E_INVALIDARG;
        UINT count = ReadDword(data);
        bool stream = tag == 0x31475349 || tag == 0x3147534f || tag == 0x3547534f || tag == 0x31475350;
        UINT stride = stream ? (tag == 0x3547534f ? 28 : 32) : 24;
        if (count > (size - 8) / stride) return E_INVALIDARG;
        D3D10DDIARG_SIGNATURE_ENTRY *entries = static_cast<D3D10DDIARG_SIGNATURE_ENTRY *>(
                HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count ? count * sizeof(*entries) : 1));
        if (!entries) return E_OUTOFMEMORY;
        D3D11_1DDIARG_SIGNATURE_ENTRY2 *modern = static_cast<D3D11_1DDIARG_SIGNATURE_ENTRY2 *>(
                HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count ? count * sizeof(*modern) : 1));
        if (!modern)
        {
            HeapFree(GetProcessHeap(), 0, entries);
            return E_OUTOFMEMORY;
        }
        for (UINT i = 0; i < count; ++i)
        {
            const BYTE *entry = data + 8 + i * stride + (stream ? 4 : 0);
            entries[i].SystemValue = NativeSignatureSystemValue(ReadDword(entry + 8), ReadDword(entry + 4));
            entries[i].Register = ReadDword(entry + 16);
            entries[i].Mask = entry[20] & 0x0f;
            modern[i].SystemValue = static_cast<D3D10_SB_NAME>(entries[i].SystemValue);
            modern[i].Register = entries[i].Register;
            modern[i].Mask = entries[i].Mask;
            modern[i].Stream = stream ? ReadDword(data + 8 + i * stride) : 0;
            modern[i].RegisterComponentType = static_cast<D3D10_SB_REGISTER_COMPONENT_TYPE>(ReadDword(entry + 12));
            modern[i].MinPrecision = static_cast<D3D11_SB_OPERAND_MIN_PRECISION>(stride == 32 ? ReadDword(entry + 24) : 0);
        }
        if (kind == 0)
        {
            HeapFree(GetProcessHeap(), 0, signatures.pInputSignature);
            signatures.pInputSignature = entries;
            signatures.NumInputSignatureEntries = count;
            HeapFree(GetProcessHeap(), 0, modern_signatures.pInputSignature);
            modern_signatures.pInputSignature = modern;
            modern_signatures.NumInputSignatureEntries = count;
        }
        else if (kind == 1)
        {
            output_signature = data;
            output_signature_size = size;
            output_signature_stride = stride;
            output_signature_streams = stream;
            HeapFree(GetProcessHeap(), 0, signatures.pOutputSignature);
            signatures.pOutputSignature = entries;
            signatures.NumOutputSignatureEntries = count;
            HeapFree(GetProcessHeap(), 0, modern_signatures.pOutputSignature);
            modern_signatures.pOutputSignature = modern;
            modern_signatures.NumOutputSignatureEntries = count;
        }
        else
        {
            HeapFree(GetProcessHeap(), 0, tessellation.pPatchConstantSignature);
            tessellation.pPatchConstantSignature = entries;
            tessellation.NumPatchConstantSignatureEntries = count;
            HeapFree(GetProcessHeap(), 0, modern_tessellation.pPatchConstantSignature);
            modern_tessellation.pPatchConstantSignature = modern;
            modern_tessellation.NumPatchConstantSignatureEntries = count;
        }
        return S_OK;
    }
    HRESULT Parse(const void *code, SIZE_T length, UINT shader_type)
    {
        if (!code || length < 32) return E_INVALIDARG;
        const BYTE *data = static_cast<const BYTE *>(code);
        if (ReadDword(data) != 0x43425844) return E_INVALIDARG;
        UINT size = ReadDword(data + 24), chunks = ReadDword(data + 28);
        if (size > length || size < 32 || chunks > (size - 32) / 4) return E_INVALIDARG;
        for (UINT i = 0; i < chunks; ++i)
        {
            UINT offset = ReadDword(data + 32 + i * 4);
            if ((offset & 3) || offset > size - 8) return E_INVALIDARG;
            UINT tag = ReadDword(data + offset), chunk_size = ReadDword(data + offset + 4);
            if (chunk_size > size - offset - 8) return E_INVALIDARG;
            const BYTE *chunk = data + offset + 8;
            if (tag == 0x52444853 || tag == 0x58454853)
            {
                if (chunk_size < 8 || (shader_type != ~0u && (ReadDword(chunk) >> 16) != shader_type)
                        || ReadDword(chunk + 4) < 2 || ReadDword(chunk + 4) > chunk_size / 4) return E_INVALIDARG;
                tokens = reinterpret_cast<const UINT *>(chunk);
            }
            else if (tag == 0x4e475349 || tag == 0x31475349 || tag == 0x4e47534f || tag == 0x3147534f
                    || tag == 0x3547534f || tag == 0x47534350 || tag == 0x31475350)
            {
                HRESULT hr = ReadSignature(chunk, chunk_size, tag, tag == 0x4e475349 || tag == 0x31475349 ? 0
                        : tag == 0x47534350 || tag == 0x31475350 ? 2 : 1);
                if (FAILED(hr)) return hr;
            }
        }
        tessellation.pInputSignature = signatures.pInputSignature;
        tessellation.NumInputSignatureEntries = signatures.NumInputSignatureEntries;
        tessellation.pOutputSignature = signatures.pOutputSignature;
        tessellation.NumOutputSignatureEntries = signatures.NumOutputSignatureEntries;
        modern_tessellation.pInputSignature = modern_signatures.pInputSignature;
        modern_tessellation.NumInputSignatureEntries = modern_signatures.NumInputSignatureEntries;
        modern_tessellation.pOutputSignature = modern_signatures.pOutputSignature;
        modern_tessellation.NumOutputSignatureEntries = modern_signatures.NumOutputSignatureEntries;
        return tokens || (shader_type == ~0u && output_signature) ? S_OK : E_INVALIDARG;
    }
    const D3D11_1DDIARG_SIGNATURE_ENTRY2 *FindOutput(const char *semantic, UINT index, UINT stream) const
    {
        for (UINT i = 0; i < modern_signatures.NumOutputSignatureEntries; ++i)
        {
            const BYTE *entry = output_signature + 8 + i * output_signature_stride
                    + (output_signature_streams ? 4 : 0);
            UINT name = ReadDword(entry);
            if (name >= output_signature_size
                    || !memchr(output_signature + name, 0, output_signature_size - name)) return NULL;
            const auto *signature = &modern_signatures.pOutputSignature[i];
            if (signature->Stream == stream && ReadDword(entry + 4) == index
                    && !stricmp(reinterpret_cast<const char *>(output_signature + name), semantic))
                return signature;
        }
        return NULL;
    }
};

template<class Interface, const GUID *iid>
static HRESULT CreateNativeShader(NativeDevice *device, const void *code, SIZE_T length,
        ID3D11ClassLinkage *linkage, Interface **out, UINT type)
{
    if (out) *out = NULL;
    NativeShaderBytecode bytecode;
    HRESULT hr = bytecode.Parse(code, length, type);
    if (FAILED(hr)) return hr;
    UINT model = bytecode.tokens[0] & 0xff;
    UINT maximum = device->feature_level >= D3D_FEATURE_LEVEL_11_0 ? 0x50
            : device->feature_level >= D3D_FEATURE_LEVEL_10_1 ? 0x41 : 0x40;
    bool tessellation = type == 3 || type == 4;
    bool compute = type == 5;
    if ((model != 0x40 && model != 0x41 && model != 0x50) || model > maximum
            || (tessellation && (model != 0x50 || device->feature_level < D3D_FEATURE_LEVEL_11_0))
            || (compute && device->feature_level < D3D_FEATURE_LEVEL_11_0
                && !(device->shader_caps & D3D11DDICAPS_SHADER_COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X)))
        return E_INVALIDARG;
    if (linkage) return E_NOTIMPL;
    auto &f = device->functions;
    auto &m = device->wddm20_functions;
    PFND3D10DDI_CREATEVERTEXSHADER create = type == 0 ? f.pfnCreatePixelShader
            : type == 1 ? f.pfnCreateVertexShader : f.pfnCreateGeometryShader;
    PFND3D11_1DDI_CREATEVERTEXSHADER modern_create = type == 0 ? m.pfnCreatePixelShader
            : type == 1 ? m.pfnCreateVertexShader : m.pfnCreateGeometryShader;
    PFND3D11DDI_CREATEHULLSHADER create_tessellation = type == 3 ? f.pfnCreateHullShader : f.pfnCreateDomainShader;
    PFND3D11_1DDI_CREATEHULLSHADER modern_create_tessellation = type == 3 ? m.pfnCreateHullShader : m.pfnCreateDomainShader;
    if (!f.pfnDestroyShader) return E_NOTIMPL;
    if (tessellation)
    {
        if (device->wddm20 ? (!m.pfnCalcPrivateTessellationShaderSize || !modern_create_tessellation)
                : (!f.pfnCalcPrivateTessellationShaderSize || !create_tessellation)) return E_NOTIMPL;
    }
    else if ((compute && !f.pfnCreateComputeShader) || (device->wddm20
            ? (!m.pfnCalcPrivateShaderSize || (!compute && !modern_create))
            : (!f.pfnCalcPrivateShaderSize || (!compute && !create)))) return E_NOTIMPL;
    if (!out) return S_FALSE;
    NativeLock guard(device);
    NativeShader<Interface, iid> *shader = new NativeShader<Interface, iid>(device);
    if (!shader) return E_OUTOFMEMORY;
    device->BeginCall();
    SIZE_T size;
    if (tessellation)
        size = device->wddm20
                ? m.pfnCalcPrivateTessellationShaderSize(device->driver_device, bytecode.tokens, &bytecode.modern_tessellation)
                : f.pfnCalcPrivateTessellationShaderSize(device->driver_device, bytecode.tokens, &bytecode.tessellation);
    else
        size = device->wddm20
                ? m.pfnCalcPrivateShaderSize(device->driver_device, bytecode.tokens, &bytecode.modern_signatures)
                : f.pfnCalcPrivateShaderSize(device->driver_device, bytecode.tokens, &bytecode.signatures);
    if (FAILED(device->operation_error)) { hr = device->operation_error; shader->Release(); return hr; }
    shader->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!shader->handle.pDrvPrivate) { shader->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTSHADER runtime_shader = {shader};
    device->BeginCall();
    if (compute)
        f.pfnCreateComputeShader(device->driver_device, bytecode.tokens, shader->handle, runtime_shader);
    else if (tessellation)
    {
        if (device->wddm20)
            modern_create_tessellation(device->driver_device, bytecode.tokens, shader->handle, runtime_shader, &bytecode.modern_tessellation);
        else
            create_tessellation(device->driver_device, bytecode.tokens, shader->handle, runtime_shader, &bytecode.tessellation);
    }
    else if (device->wddm20)
        modern_create(device->driver_device, bytecode.tokens, shader->handle, runtime_shader, &bytecode.modern_signatures);
    else
        create(device->driver_device, bytecode.tokens, shader->handle, runtime_shader, &bytecode.signatures);
    hr = device->operation_error;
    if (FAILED(hr)) { shader->Release(); return hr; }
    shader->created = true;
    *out = shader;
    return S_OK;
}

#define NATIVE_CREATE_SHADER(Kind, Type) \
HRESULT STDMETHODCALLTYPE NativeDevice::Create##Kind##Shader(const void *code, SIZE_T length, \
        ID3D11ClassLinkage *linkage, ID3D11##Kind##Shader **out) \
{ \
    return CreateNativeShader<ID3D11##Kind##Shader, &IID_ID3D11##Kind##Shader>(this, code, length, linkage, out, Type); \
}
NATIVE_CREATE_SHADER(Pixel, 0)
NATIVE_CREATE_SHADER(Vertex, 1)
NATIVE_CREATE_SHADER(Geometry, 2)
NATIVE_CREATE_SHADER(Hull, 3)
NATIVE_CREATE_SHADER(Domain, 4)
NATIVE_CREATE_SHADER(Compute, 5)
#undef NATIVE_CREATE_SHADER

#include "native_so.inl"

static HRESULT NativeBuildShaderResourceView(NativeDevice *device, ID3D11Resource *resource,
        const D3D11DDIARG_CREATESHADERRESOURCEVIEW &args, const D3D11_SHADER_RESOURCE_VIEW_DESC &desc, ID3D11ShaderResourceView **out)
{
    if (!out) return S_FALSE;
    NativeLock guard(device);
    NativeShaderResourceView *view = new NativeShaderResourceView(device);
    if (!view) return E_OUTOFMEMORY;
    view->texture = resource;
    NativeRetainResource(resource);
    view->resource_handle = args.hDrvResource;
    view->desc = desc;
    view->desc.Format = args.Format;
    SIZE_T size = device->functions.pfnCalcPrivateShaderResourceViewSize(device->driver_device, &args);
    view->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!view->handle.pDrvPrivate) { view->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTSHADERRESOURCEVIEW runtime_view = {view};
    device->BeginCall();
    device->functions.pfnCreateShaderResourceView(device->driver_device, &args, view->handle, runtime_view);
    HRESULT hr = device->operation_error;
    if (FAILED(hr)) { view->Release(); return hr; }
    view->created = true;
    *out = view;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateShaderResourceView(ID3D11Resource *resource,
        const D3D11_SHADER_RESOURCE_VIEW_DESC *input, ID3D11ShaderResourceView **out)
{
    if (out) *out = NULL;
    if (!functions.pfnCalcPrivateShaderResourceViewSize || !functions.pfnCreateShaderResourceView
            || !functions.pfnDestroyShaderResourceView) return E_NOTIMPL;
    if (NativeBuffer *buffer = GetNativeBuffer(resource, this))
    {
        if (!(buffer->desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) return E_INVALIDARG;
        bool structured = !!(buffer->desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED);
        D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
        if (input) desc = *input;
        else if (structured)
        {
            desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            desc.Buffer.NumElements = buffer->desc.ByteWidth / buffer->desc.StructureByteStride;
        }
        else return E_INVALIDARG;
        if (desc.ViewDimension != D3D11_SRV_DIMENSION_BUFFER && desc.ViewDimension != D3D11_SRV_DIMENSION_BUFFEREX)
            return E_INVALIDARG;
        UINT flags = desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX ? desc.BufferEx.Flags : 0;
        UINT stride = 0;
        if (flags & ~D3D11_BUFFEREX_SRV_FLAG_RAW) return E_INVALIDARG;
        if (flags & D3D11_BUFFEREX_SRV_FLAG_RAW)
        {
            if (structured || !(buffer->desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
                    || desc.Format != DXGI_FORMAT_R32_TYPELESS) return E_INVALIDARG;
            stride = sizeof(UINT);
        }
        else if (structured)
        {
            if (desc.Format != DXGI_FORMAT_UNKNOWN) return E_INVALIDARG;
            stride = buffer->desc.StructureByteStride;
        }
        else
        {
            HRESULT hr = NativeBufferViewStride(this, desc.Format, false, &stride);
            if (FAILED(hr)) return hr;
        }
        if (!NativeBufferViewRange(desc.Buffer.FirstElement, desc.Buffer.NumElements, stride, buffer->desc.ByteWidth))
            return E_INVALIDARG;
        D3D11DDIARG_CREATESHADERRESOURCEVIEW args = {};
        args.hDrvResource = buffer->handle;
        args.Format = desc.Format;
        args.ResourceDimension = desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX
                ? D3D11DDIRESOURCE_BUFFEREX : D3D10DDIRESOURCE_BUFFER;
        args.BufferEx.FirstElement = desc.Buffer.FirstElement;
        args.BufferEx.NumElements = desc.Buffer.NumElements;
        args.BufferEx.Flags = flags;
        return NativeBuildShaderResourceView(this, resource, args, desc, out);
    }
    NativeTextureInfo info;
    NativeTextureInfo *texture = GetNativeTexture(resource, this, &info);
    if (!texture || !(texture->desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) return E_INVALIDARG;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc;
    /* The first union member is shorter than the cube/array descriptors.
     * Zero all storage before selecting a default view dimension. */
    ZeroMemory(&desc, sizeof(desc));
    if (input) desc = *input;
    else
    {
        desc.Format = texture->desc.Format;
        if (texture->dimension == D3D10DDIRESOURCE_TEXTURE1D)
        {
            desc.ViewDimension = texture->desc.ArraySize > 1 ? D3D11_SRV_DIMENSION_TEXTURE1DARRAY : D3D11_SRV_DIMENSION_TEXTURE1D;
            desc.Texture1DArray.MipLevels = texture->desc.MipLevels;
            desc.Texture1DArray.ArraySize = texture->desc.ArraySize;
        }
        else if (texture->dimension == D3D10DDIRESOURCE_TEXTURE3D)
        {
            desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
            desc.Texture3D.MipLevels = texture->desc.MipLevels;
        }
        else if (texture->dimension == D3D10DDIRESOURCE_TEXTURECUBE)
        {
            desc.ViewDimension = texture->desc.ArraySize < 12 ? D3D11_SRV_DIMENSION_TEXTURECUBE : D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
            desc.TextureCubeArray.MipLevels = texture->desc.MipLevels;
            desc.TextureCubeArray.NumCubes = texture->desc.ArraySize / 6;
        }
        else
        {
            desc.ViewDimension = texture->desc.ArraySize > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
            desc.Texture2DArray.MipLevels = texture->desc.MipLevels;
            desc.Texture2DArray.ArraySize = texture->desc.ArraySize;
            if (texture->desc.SampleDesc.Count > 1)
            {
                desc.ViewDimension = texture->desc.ArraySize > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMS;
                desc.Texture2DMSArray.FirstArraySlice = 0;
                desc.Texture2DMSArray.ArraySize = texture->desc.ArraySize;
            }
        }
    }
    D3D11DDIARG_CREATESHADERRESOURCEVIEW args = {};
    args.hDrvResource = texture->handle;
    args.Format = desc.Format == DXGI_FORMAT_UNKNOWN ? texture->desc.Format : desc.Format;
    if (!NativeViewFormatCompatible(*texture, args.Format)) return E_INVALIDARG;
    UINT mip = 0, mips = 1, first = 0, slices = 1;
    bool volume = texture->dimension == D3D10DDIRESOURCE_TEXTURE3D;
    bool one_dimensional = texture->dimension == D3D10DDIRESOURCE_TEXTURE1D;
    if ((desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE1D || desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE1DARRAY)
            != one_dimensional) return E_INVALIDARG;
    if ((desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE3D) != volume) return E_INVALIDARG;
    bool multisample = desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DMS
            || desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
    if (!multisample && texture->desc.SampleDesc.Count > 1) return E_INVALIDARG;
    args.ResourceDimension = one_dimensional ? D3D10DDIRESOURCE_TEXTURE1D : D3D10DDIRESOURCE_TEXTURE2D;
    switch (desc.ViewDimension)
    {
        case D3D11_SRV_DIMENSION_TEXTURE1D:
        case D3D11_SRV_DIMENSION_TEXTURE2D:
            if (texture->desc.ArraySize != 1) return E_INVALIDARG;
            mip = desc.Texture2D.MostDetailedMip;
            mips = desc.Texture2D.MipLevels;
            break;
        case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
        case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
            if (!NativeViewSliceRange(desc.Texture2DArray.FirstArraySlice, desc.Texture2DArray.ArraySize,
                    texture->desc.ArraySize)) return E_INVALIDARG;
            mip = desc.Texture2DArray.MostDetailedMip;
            mips = desc.Texture2DArray.MipLevels;
            first = desc.Texture2DArray.FirstArraySlice;
            slices = desc.Texture2DArray.ArraySize;
            break;
        case D3D11_SRV_DIMENSION_TEXTURE2DMS:
            break;
        case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
            if (!NativeViewSliceRange(desc.Texture2DMSArray.FirstArraySlice, desc.Texture2DMSArray.ArraySize,
                    texture->desc.ArraySize)) return E_INVALIDARG;
            first = desc.Texture2DMSArray.FirstArraySlice;
            slices = desc.Texture2DMSArray.ArraySize;
            break;
        case D3D11_SRV_DIMENSION_TEXTURE3D:
            args.ResourceDimension = D3D10DDIRESOURCE_TEXTURE3D;
            mip = desc.Texture3D.MostDetailedMip;
            mips = desc.Texture3D.MipLevels;
            break;
        case D3D11_SRV_DIMENSION_TEXTURECUBE:
            if (texture->dimension != D3D10DDIRESOURCE_TEXTURECUBE) return E_INVALIDARG;
            args.ResourceDimension = D3D10DDIRESOURCE_TEXTURECUBE;
            mip = desc.TextureCube.MostDetailedMip;
            mips = desc.TextureCube.MipLevels;
            slices = 6;
            break;
        case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
            if (texture->dimension != D3D10DDIRESOURCE_TEXTURECUBE || feature_level < D3D_FEATURE_LEVEL_10_1
                    || desc.TextureCubeArray.First2DArrayFace >= texture->desc.ArraySize) return E_INVALIDARG;
            if (desc.TextureCubeArray.NumCubes == ~0u)
                desc.TextureCubeArray.NumCubes = (texture->desc.ArraySize - desc.TextureCubeArray.First2DArrayFace) / 6;
            if (desc.TextureCubeArray.NumCubes > (texture->desc.ArraySize - desc.TextureCubeArray.First2DArrayFace) / 6)
                return E_INVALIDARG;
            args.ResourceDimension = D3D10DDIRESOURCE_TEXTURECUBE;
            mip = desc.TextureCubeArray.MostDetailedMip;
            mips = desc.TextureCubeArray.MipLevels;
            first = desc.TextureCubeArray.First2DArrayFace;
            slices = desc.TextureCubeArray.NumCubes * 6;
            break;
        default: return E_INVALIDARG;
    }
    if (mip >= texture->desc.MipLevels) return E_INVALIDARG;
    if (mips == ~0u) mips = texture->desc.MipLevels - mip;
    if (!mips || mips > texture->desc.MipLevels - mip || !slices
            || first >= texture->desc.ArraySize || slices > texture->desc.ArraySize - first) return E_INVALIDARG;
    if (args.ResourceDimension == D3D10DDIRESOURCE_TEXTURE3D)
    {
        args.Tex3D.MostDetailedMip = mip;
        args.Tex3D.MipLevels = mips;
        desc.Texture3D.MipLevels = mips;
    }
    else if (args.ResourceDimension == D3D10DDIRESOURCE_TEXTURECUBE)
    {
        args.TexCube.MostDetailedMip = mip;
        args.TexCube.MipLevels = mips;
        args.TexCube.First2DArrayFace = first;
        args.TexCube.NumCubes = slices / 6;
        desc.TextureCube.MipLevels = mips;
    }
    else
    {
        args.Tex2D.MostDetailedMip = mip;
        args.Tex2D.MipLevels = mips;
        args.Tex2D.FirstArraySlice = first;
        args.Tex2D.ArraySize = slices;
        if (!multisample) desc.Texture2D.MipLevels = mips;
    }
    return NativeBuildShaderResourceView(this, resource, args, desc, out);
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateQuery(const D3D11_QUERY_DESC *desc, ID3D11Query **out)
{
    if (out) *out = NULL;
    if (!desc || (desc->MiscFlags & ~D3D11_QUERY_MISC_PREDICATEHINT)
            || (desc->MiscFlags && desc->Query != D3D11_QUERY_OCCLUSION_PREDICATE)) return E_INVALIDARG;
    if (desc->Query >= D3D11_QUERY_SO_STATISTICS_STREAM0 && feature_level < D3D_FEATURE_LEVEL_11_0)
        return E_INVALIDARG;
    D3D10DDIARG_CREATEQUERY args = {};
    args.MiscFlags = desc->MiscFlags;
    bool predicate = false;
    UINT data_size;
    switch (desc->Query)
    {
        case D3D11_QUERY_EVENT: args.Query = D3D10DDI_QUERY_EVENT; data_size = sizeof(BOOL); break;
        case D3D11_QUERY_OCCLUSION: args.Query = D3D10DDI_QUERY_OCCLUSION; data_size = sizeof(UINT64); break;
        case D3D11_QUERY_TIMESTAMP: args.Query = D3D10DDI_QUERY_TIMESTAMP; data_size = sizeof(UINT64); break;
        case D3D11_QUERY_TIMESTAMP_DISJOINT: args.Query = D3D10DDI_QUERY_TIMESTAMPDISJOINT; data_size = sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT); break;
        case D3D11_QUERY_PIPELINE_STATISTICS: args.Query = D3D11DDI_QUERY_PIPELINESTATS; data_size = sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS); break;
        case D3D11_QUERY_OCCLUSION_PREDICATE:
            args.Query = D3D10DDI_QUERY_OCCLUSIONPREDICATE; data_size = sizeof(BOOL); predicate = true; break;
        case D3D11_QUERY_SO_STATISTICS:
            args.Query = D3D10DDI_QUERY_STREAMOUTPUTSTATS; data_size = sizeof(D3D11_QUERY_DATA_SO_STATISTICS); break;
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE:
            args.Query = D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE; data_size = sizeof(BOOL); predicate = true; break;
        case D3D11_QUERY_SO_STATISTICS_STREAM0:
        case D3D11_QUERY_SO_STATISTICS_STREAM1:
        case D3D11_QUERY_SO_STATISTICS_STREAM2:
        case D3D11_QUERY_SO_STATISTICS_STREAM3:
            args.Query = static_cast<D3D10DDI_QUERY>(D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0
                    + (desc->Query - D3D11_QUERY_SO_STATISTICS_STREAM0) / 2);
            data_size = sizeof(D3D11_QUERY_DATA_SO_STATISTICS); break;
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3:
            args.Query = static_cast<D3D10DDI_QUERY>(D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0
                    + (desc->Query - D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0) / 2);
            data_size = sizeof(BOOL); predicate = true; break;
        default: return E_INVALIDARG;
    }
    if (!functions.pfnCalcPrivateQuerySize || !functions.pfnCreateQuery || !functions.pfnDestroyQuery
            || !functions.pfnQueryGetData || !functions.pfnQueryEnd) return E_NOTIMPL;
    if (!out) return S_FALSE;
    NativeLock guard(this);
    NativeQuery *query = new NativeQuery(this);
    if (!query) return E_OUTOFMEMORY;
    query->desc = *desc;
    query->data_size = data_size;
    query->is_predicate = predicate;
    SIZE_T size = functions.pfnCalcPrivateQuerySize(driver_device, &args);
    query->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!query->handle.pDrvPrivate) { query->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTQUERY runtime_query = {query};
    BeginCall();
    functions.pfnCreateQuery(driver_device, &args, query->handle, runtime_query);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { query->Release(); return hr; }
    query->created = true;
    *out = query;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreatePredicate(const D3D11_QUERY_DESC *desc, ID3D11Predicate **out)
{
    if (out) *out = NULL;
    /* The stream-specific overflow predicates are exposed through CreateQuery. */
    if (!desc || (desc->Query != D3D11_QUERY_OCCLUSION_PREDICATE
            && desc->Query != D3D11_QUERY_SO_OVERFLOW_PREDICATE)) return E_INVALIDARG;
    ID3D11Query *query = NULL;
    HRESULT hr = CreateQuery(desc, out ? &query : NULL);
    if (SUCCEEDED(hr) && out) *out = static_cast<NativeQuery *>(query);
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC *input, ID3D11DepthStencilState **out)
{
    if (out) *out = NULL;
    if (!input) return E_INVALIDARG;
    /* Build a canonical key with deterministic padding and defaults for
     * disabled depth/stencil fields, shared by GetDesc and the driver. */
    D3D11_DEPTH_STENCIL_DESC normalized = {};
    normalized.DepthEnable = !!input->DepthEnable;
    normalized.DepthWriteMask = input->DepthEnable ? input->DepthWriteMask : D3D11_DEPTH_WRITE_MASK_ALL;
    normalized.DepthFunc = input->DepthEnable ? input->DepthFunc : D3D11_COMPARISON_LESS;
    normalized.StencilEnable = !!input->StencilEnable;
    normalized.StencilReadMask = input->StencilEnable ? input->StencilReadMask : D3D11_DEFAULT_STENCIL_READ_MASK;
    normalized.StencilWriteMask = input->StencilEnable ? input->StencilWriteMask : D3D11_DEFAULT_STENCIL_WRITE_MASK;
    if (input->StencilEnable)
    {
        normalized.FrontFace = input->FrontFace;
        normalized.BackFace = input->BackFace;
    }
    else
    {
        normalized.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        normalized.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        normalized.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        normalized.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        normalized.BackFace = normalized.FrontFace;
    }
    const D3D11_DEPTH_STENCIL_DESC *desc = &normalized;
    if (!functions.pfnCalcPrivateDepthStencilStateSize || !functions.pfnCreateDepthStencilState || !functions.pfnDestroyDepthStencilState) return E_NOTIMPL;
    if (!out) return S_FALSE;
    NativeLock guard(this);
    if ((*out = NativeFindState<ID3D11DepthStencilState>(this, NativeDepthKind, normalized))) return S_OK;
    NativeDepthStencil *state = new NativeDepthStencil(this);
    if (!state) return E_OUTOFMEMORY;
    state->desc = *desc;
    state->destroy = functions.pfnDestroyDepthStencilState;
    D3D10_DDI_DEPTH_STENCIL_DESC args = {};
    args.DepthEnable = desc->DepthEnable;
    args.DepthWriteMask = static_cast<D3D10_DDI_DEPTH_WRITE_MASK>(desc->DepthWriteMask);
    args.DepthFunc = static_cast<D3D10_DDI_COMPARISON_FUNC>(desc->DepthFunc);
    args.StencilEnable = args.FrontEnable = args.BackEnable = desc->StencilEnable;
    args.StencilReadMask = desc->StencilReadMask;
    args.StencilWriteMask = desc->StencilWriteMask;
    memcpy(&args.FrontFace, &desc->FrontFace, sizeof(args.FrontFace));
    memcpy(&args.BackFace, &desc->BackFace, sizeof(args.BackFace));
    SIZE_T size = functions.pfnCalcPrivateDepthStencilStateSize(driver_device, &args);
    state->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!state->handle.pDrvPrivate) { state->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTDEPTHSTENCILSTATE runtime_state = {state};
    BeginCall();
    functions.pfnCreateDepthStencilState(driver_device, &args, state->handle, runtime_state);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { state->Release(); return hr; }
    state->created = true;
    state->cache_entry.Insert(NativeStateBucket(this, NativeDepthKind, normalized), state, &state->desc, sizeof(normalized));
    *out = state;
    return S_OK;
}

/* Context bindings are private references. Public child references retain the
 * device, so using AddRef for bindings would create a device/context cycle. */
#define NATIVE_BINDING(Interface, Object) \
static Object *NativeBindingObject(Interface *object) { return static_cast<Object *>(object); }
NATIVE_BINDING(ID3D11Buffer, NativeBuffer)
NATIVE_BINDING(ID3D11VertexShader, NativeVertexShader)
NATIVE_BINDING(ID3D11PixelShader, NativePixelShader)
NATIVE_BINDING(ID3D11GeometryShader, NativeGeometryShader)
NATIVE_BINDING(ID3D11ShaderResourceView, NativeShaderResourceView)
NATIVE_BINDING(ID3D11RenderTargetView, NativeRenderTargetView)
NATIVE_BINDING(ID3D11DepthStencilView, NativeDepthView)
NATIVE_BINDING(ID3D11InputLayout, NativeInputLayout)
NATIVE_BINDING(ID3D11SamplerState, NativeSampler)
NATIVE_BINDING(ID3D11BlendState, NativeBlend)
NATIVE_BINDING(ID3D11DepthStencilState, NativeDepthStencil)
NATIVE_BINDING(ID3D11RasterizerState, NativeRasterizer)
NATIVE_BINDING(ID3D11Predicate, NativeQuery)
NATIVE_BINDING(ID3D11HullShader, NativeHullShader)
NATIVE_BINDING(ID3D11DomainShader, NativeDomainShader)
NATIVE_BINDING(ID3D11ComputeShader, NativeComputeShader)
NATIVE_BINDING(ID3D11UnorderedAccessView, NativeUnorderedAccessView)
#undef NATIVE_BINDING

template<class Interface>
static void ReplaceObject(Interface *&slot, Interface *value)
{
    if (slot == value) return;
    if (value) NativeBindingObject(value)->Retain();
    Interface *old = slot;
    slot = value;
    if (old) NativeBindingObject(old)->Drop();
}

#define NATIVE_SET_SHADER(Method, Interface, Object, Member, Function) \
void STDMETHODCALLTYPE NativeContext::Method(Interface *shader, ID3D11ClassInstance *const *, UINT count) \
{ \
    if (count) { Unimplemented(#Method " class instances"); return; } \
    Object *object = static_cast<Object *>(shader); \
    if ((object && object->device != device) || !device->functions.Function) return; \
    NativeLock guard(device); \
    D3D10DDI_HSHADER handle = {}; \
    if (object) handle = object->handle; \
    if (deferred && !recording_disabled) \
    { \
        NativeCommandRef<Interface> retained(shader); \
        Record([=](NativeContext *context) { context->Method(retained.Get(), NULL, 0); }); \
    } \
    if (!deferred) device->functions.Function(device->driver_device, handle); \
    ReplaceObject(Member, shader); \
}
NATIVE_SET_SHADER(VSSetShader, ID3D11VertexShader, NativeVertexShader, vertex_shader, pfnVsSetShader)
NATIVE_SET_SHADER(PSSetShader, ID3D11PixelShader, NativePixelShader, pixel_shader, pfnPsSetShader)
NATIVE_SET_SHADER(GSSetShader, ID3D11GeometryShader, NativeGeometryShader, geometry_shader, pfnGsSetShader)
NATIVE_SET_SHADER(HSSetShader, ID3D11HullShader, NativeHullShader, hull_shader, pfnHsSetShader)
NATIVE_SET_SHADER(DSSetShader, ID3D11DomainShader, NativeDomainShader, domain_shader, pfnDsSetShader)
NATIVE_SET_SHADER(CSSetShader, ID3D11ComputeShader, NativeComputeShader, compute_shader, pfnCsSetShader)
#undef NATIVE_SET_SHADER

void NativeContext::SetConstantBuffers(UINT stage, UINT start, UINT count, ID3D11Buffer *const *buffers)
{
    if (start > 14 || count > 14 - start || (count && !buffers)) return;
    D3D10DDI_HRESOURCE handles[14] = {};
    for (UINT i = 0; i < count; ++i)
    {
        if (!buffers[i]) continue;
        NativeBuffer *buffer = GetNativeBuffer(buffers[i], device);
        if (!buffer || !(buffer->desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER)) return;
        handles[i] = buffer->handle;
    }
    PFND3D10DDI_SETCONSTANTBUFFERS set = stage == 0 ? device->functions.pfnVsSetConstantBuffers
            : stage == 1 ? device->functions.pfnPsSetConstantBuffers
            : stage == 2 ? device->functions.pfnGsSetConstantBuffers
            : stage == 3 ? device->functions.pfnHsSetConstantBuffers
            : stage == 4 ? device->functions.pfnDsSetConstantBuffers : device->functions.pfnCsSetConstantBuffers;
    if (!set) { Unimplemented("SetConstantBuffers"); return; }
    NativeLock guard(device);
    if (deferred && !recording_disabled)
    {
        NativeCommandArray<ID3D11Buffer *> values(count, buffers);
        if (Captured(values)) Record([=](NativeContext *context) { context->SetConstantBuffers(stage, start, count, values.Data()); });
    }
    if (!deferred) set(device->driver_device, start, count, handles);
    for (UINT i = 0; i < count; ++i) ReplaceObject(constant_buffers[stage][start + i], buffers[i]);
}
void STDMETHODCALLTYPE NativeContext::VSSetConstantBuffers(UINT start, UINT count, ID3D11Buffer *const *buffers) { SetConstantBuffers(0, start, count, buffers); }
void STDMETHODCALLTYPE NativeContext::PSSetConstantBuffers(UINT start, UINT count, ID3D11Buffer *const *buffers) { SetConstantBuffers(1, start, count, buffers); }
void STDMETHODCALLTYPE NativeContext::GSSetConstantBuffers(UINT start, UINT count, ID3D11Buffer *const *buffers) { SetConstantBuffers(2, start, count, buffers); }
void STDMETHODCALLTYPE NativeContext::CSSetConstantBuffers(UINT start, UINT count, ID3D11Buffer *const *buffers) { SetConstantBuffers(5, start, count, buffers); }
void STDMETHODCALLTYPE NativeContext::DSSetConstantBuffers(UINT start, UINT count, ID3D11Buffer *const *buffers) { SetConstantBuffers(4, start, count, buffers); }
void STDMETHODCALLTYPE NativeContext::HSSetConstantBuffers(UINT start, UINT count, ID3D11Buffer *const *buffers) { SetConstantBuffers(3, start, count, buffers); }

void NativeContext::SetSamplers(UINT stage, UINT start, UINT count, ID3D11SamplerState *const *states)
{
    if (start > 16 || count > 16 - start || (count && !states)) return;
    D3D10DDI_HSAMPLER handles[16] = {};
    for (UINT i = 0; i < count; ++i)
    {
        if (!states[i]) continue;
        NativeSampler *state = static_cast<NativeSampler *>(states[i]);
        if (state->device != device) return;
        handles[i] = state->handle;
    }
    PFND3D10DDI_SETSAMPLERS set = stage == 0 ? device->functions.pfnVsSetSamplers
            : stage == 1 ? device->functions.pfnPsSetSamplers
            : stage == 2 ? device->functions.pfnGsSetSamplers
            : stage == 3 ? device->functions.pfnHsSetSamplers
            : stage == 4 ? device->functions.pfnDsSetSamplers : device->functions.pfnCsSetSamplers;
    if (!set) { Unimplemented("SetSamplers"); return; }
    NativeLock guard(device);
    if (deferred && !recording_disabled)
    {
        NativeCommandArray<ID3D11SamplerState *> values(count, states);
        if (Captured(values)) Record([=](NativeContext *context) { context->SetSamplers(stage, start, count, values.Data()); });
    }
    if (!deferred) set(device->driver_device, start, count, handles);
    for (UINT i = 0; i < count; ++i) ReplaceObject(samplers[stage][start + i], states[i]);
}
void STDMETHODCALLTYPE NativeContext::VSSetSamplers(UINT start, UINT count, ID3D11SamplerState *const *states) { SetSamplers(0, start, count, states); }
void STDMETHODCALLTYPE NativeContext::PSSetSamplers(UINT start, UINT count, ID3D11SamplerState *const *states) { SetSamplers(1, start, count, states); }
void STDMETHODCALLTYPE NativeContext::GSSetSamplers(UINT start, UINT count, ID3D11SamplerState *const *states) { SetSamplers(2, start, count, states); }
void STDMETHODCALLTYPE NativeContext::CSSetSamplers(UINT start, UINT count, ID3D11SamplerState *const *states) { SetSamplers(5, start, count, states); }
void STDMETHODCALLTYPE NativeContext::DSSetSamplers(UINT start, UINT count, ID3D11SamplerState *const *states) { SetSamplers(4, start, count, states); }
void STDMETHODCALLTYPE NativeContext::HSSetSamplers(UINT start, UINT count, ID3D11SamplerState *const *states) { SetSamplers(3, start, count, states); }

void NativeContext::SetShaderResources(UINT stage, UINT start, UINT count, ID3D11ShaderResourceView *const *views)
{
    if (start > 128 || count > 128 - start || (count && !views)) return;
    PFND3D10DDI_SETSHADERRESOURCES set = stage == 0 ? device->functions.pfnVsSetShaderResources
            : stage == 1 ? device->functions.pfnPsSetShaderResources
            : stage == 2 ? device->functions.pfnGsSetShaderResources
            : stage == 3 ? device->functions.pfnHsSetShaderResources
            : stage == 4 ? device->functions.pfnDsSetShaderResources : device->functions.pfnCsSetShaderResources;
    if (!set) { Unimplemented("SetShaderResources"); return; }
    NativeLock guard(device);
    D3D10DDI_HSHADERRESOURCEVIEW handles[128] = {};
    ID3D11ShaderResourceView *accepted[128] = {};
    for (UINT i = 0; i < count; ++i)
    {
        NativeShaderResourceView *view = static_cast<NativeShaderResourceView *>(views[i]);
        if (!view) continue;
        if (view->device != device) return;
        bool conflict = false;
        for (UINT j = 0; j < 8; ++j)
            if (NativeViewsOverlap(render_targets[j], view) || NativeViewsOverlap(compute_uavs[j], view)
                    || NativeViewsOverlap(pixel_uavs[j], view)) conflict = true;
        for (UINT j = 0; j < 4; ++j) if (stream_outputs[j] == view->texture) conflict = true;
        if (depth_view && static_cast<NativeDepthView *>(depth_view)->ConflictsWith(view)) conflict = true;
        if (conflict) continue;
        handles[i] = view->handle;
        accepted[i] = view;
        if (!deferred && device->functions.pfnShaderResourceViewReadAfterWriteHazard)
            device->functions.pfnShaderResourceViewReadAfterWriteHazard(device->driver_device, view->handle, view->resource_handle);
    }
    if (deferred && !recording_disabled)
    {
        NativeCommandArray<ID3D11ShaderResourceView *> values(count, accepted);
        if (Captured(values)) Record([=](NativeContext *context) { context->SetShaderResources(stage, start, count, values.Data()); });
    }
    if (!deferred) set(device->driver_device, start, count, handles);
    for (UINT i = 0; i < count; ++i) ReplaceObject(shader_resources[stage][start + i], accepted[i]);
}
void STDMETHODCALLTYPE NativeContext::VSSetShaderResources(UINT start, UINT count, ID3D11ShaderResourceView *const *views) { SetShaderResources(0, start, count, views); }
void STDMETHODCALLTYPE NativeContext::PSSetShaderResources(UINT start, UINT count, ID3D11ShaderResourceView *const *views) { SetShaderResources(1, start, count, views); }
void STDMETHODCALLTYPE NativeContext::GSSetShaderResources(UINT start, UINT count, ID3D11ShaderResourceView *const *views) { SetShaderResources(2, start, count, views); }
void STDMETHODCALLTYPE NativeContext::CSSetShaderResources(UINT start, UINT count, ID3D11ShaderResourceView *const *views) { SetShaderResources(5, start, count, views); }
void STDMETHODCALLTYPE NativeContext::DSSetShaderResources(UINT start, UINT count, ID3D11ShaderResourceView *const *views) { SetShaderResources(4, start, count, views); }
void STDMETHODCALLTYPE NativeContext::HSSetShaderResources(UINT start, UINT count, ID3D11ShaderResourceView *const *views) { SetShaderResources(3, start, count, views); }

void STDMETHODCALLTYPE NativeContext::OMSetRenderTargets(UINT count, ID3D11RenderTargetView *const *targets, ID3D11DepthStencilView *depth)
{
    OMSetRenderTargetsAndUnorderedAccessViews(count, targets, depth, 0, D3D11_KEEP_UNORDERED_ACCESS_VIEWS, NULL, NULL);
}

void STDMETHODCALLTYPE NativeContext::OMSetBlendState(ID3D11BlendState *state, const FLOAT factors[4], UINT mask)
{
    NativeBlend *blend = static_cast<NativeBlend *>(state);
    if ((blend && blend->device != device) || !device->functions.pfnSetBlendState) return;
    NativeLock guard(device);
    D3D10DDI_HBLENDSTATE handle = blend ? blend->handle : device->default_blend;
    for (UINT i = 0; i < 4; ++i) blend_factor[i] = factors ? factors[i] : 1.0f;
    sample_mask = mask;
    if (deferred && !recording_disabled)
    {
        NativeCommandRef<ID3D11BlendState> retained(state);
        NativeCommandArray<FLOAT> values(4, blend_factor);
        if (!Captured(values)) return;
        Record([=](NativeContext *context) { context->OMSetBlendState(retained.Get(), values.Data(), mask); });
    }
    if (!deferred) device->functions.pfnSetBlendState(device->driver_device, handle, blend_factor, mask);
    ReplaceObject(blend_state, state);
}
void STDMETHODCALLTYPE NativeContext::OMSetDepthStencilState(ID3D11DepthStencilState *state, UINT reference)
{
    NativeDepthStencil *depth = static_cast<NativeDepthStencil *>(state);
    if ((depth && depth->device != device) || !device->functions.pfnSetDepthStencilState) return;
    NativeLock guard(device);
    D3D10DDI_HDEPTHSTENCILSTATE handle = depth ? depth->handle : device->default_depth;
    stencil_ref = reference;
    if (deferred && !recording_disabled)
    {
        NativeCommandRef<ID3D11DepthStencilState> retained(state);
        Record([=](NativeContext *context) { context->OMSetDepthStencilState(retained.Get(), reference); });
    }
    if (!deferred) device->functions.pfnSetDepthStencilState(device->driver_device, handle, reference);
    ReplaceObject(depth_stencil_state, state);
}
void STDMETHODCALLTYPE NativeContext::RSSetState(ID3D11RasterizerState *state)
{
    NativeRasterizer *rasterizer = static_cast<NativeRasterizer *>(state);
    if ((rasterizer && rasterizer->device != device) || !device->functions.pfnSetRasterizerState) return;
    NativeLock guard(device);
    D3D10DDI_HRASTERIZERSTATE handle = rasterizer ? rasterizer->handle : device->default_rasterizer;
    if (deferred && !recording_disabled)
    {
        NativeCommandRef<ID3D11RasterizerState> retained(state);
        Record([=](NativeContext *context) { context->RSSetState(retained.Get()); });
    }
    if (!deferred) device->functions.pfnSetRasterizerState(device->driver_device, handle);
    ReplaceObject(rasterizer_state, state);
}
void STDMETHODCALLTYPE NativeContext::RSSetViewports(UINT count, const D3D11_VIEWPORT *values)
{
    if (count > 16 || (count && !values) || !device->functions.pfnSetViewports) return;
    NativeLock guard(device);
    UINT old_count = viewport_count;
    viewport_count = count;
    if (count) memcpy(viewports, values, count * sizeof(*values));
    if (deferred && !recording_disabled)
    {
        NativeCommandArray<D3D11_VIEWPORT> copied(count, values);
        if (Captured(copied)) Record([=](NativeContext *context) { context->RSSetViewports(count, copied.Data()); });
    }
    if (!deferred) device->functions.pfnSetViewports(device->driver_device, count, old_count > count ? old_count - count : 0,
            reinterpret_cast<const D3D10_DDI_VIEWPORT *>(values));
}
void STDMETHODCALLTYPE NativeContext::RSSetScissorRects(UINT count, const D3D11_RECT *values)
{
    if (count > 16 || (count && !values) || !device->functions.pfnSetScissorRects) return;
    NativeLock guard(device);
    UINT old_count = scissor_count;
    scissor_count = count;
    if (count) memcpy(scissors, values, count * sizeof(*values));
    if (deferred && !recording_disabled)
    {
        NativeCommandArray<D3D11_RECT> copied(count, values);
        if (Captured(copied)) Record([=](NativeContext *context) { context->RSSetScissorRects(count, copied.Data()); });
    }
    if (!deferred) device->functions.pfnSetScissorRects(device->driver_device, count, old_count > count ? old_count - count : 0,
            reinterpret_cast<const D3D10_DDI_RECT *>(values));
}
void STDMETHODCALLTYPE NativeContext::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY value)
{
    if (!device->functions.pfnIaSetTopology) return;
    NativeLock guard(device);
    topology = value;
    if (deferred && !recording_disabled)
        Record([=](NativeContext *context) { context->IASetPrimitiveTopology(value); });
    if (!deferred) device->functions.pfnIaSetTopology(device->driver_device, static_cast<D3D10_DDI_PRIMITIVE_TOPOLOGY>(value));
}
void STDMETHODCALLTYPE NativeContext::Draw(UINT vertices, UINT start) {
    if (deferred) { NativeLock guard(device); Record([=](NativeContext *context) { context->Draw(vertices, start); }); return; }
 NativeLock guard(device); if (device->functions.pfnDraw) device->functions.pfnDraw(device->driver_device, vertices, start); }
void STDMETHODCALLTYPE NativeContext::DrawInstanced(UINT vertices, UINT instances, UINT start, UINT first_instance) {
    if (deferred) { NativeLock guard(device); Record([=](NativeContext *context) { context->DrawInstanced(vertices, instances, start, first_instance); }); return; }
 NativeLock guard(device); if (device->functions.pfnDrawInstanced) device->functions.pfnDrawInstanced(device->driver_device, vertices, instances, start, first_instance); }
void STDMETHODCALLTYPE NativeContext::DrawIndexed(UINT indices, UINT start, INT vertex) {
    if (deferred) { NativeLock guard(device); Record([=](NativeContext *context) { context->DrawIndexed(indices, start, vertex); }); return; }
 NativeLock guard(device); if (device->functions.pfnDrawIndexed) device->functions.pfnDrawIndexed(device->driver_device, indices, start, vertex); }
void STDMETHODCALLTYPE NativeContext::DrawIndexedInstanced(UINT indices, UINT instances, UINT start, INT vertex, UINT first_instance) {
    if (deferred) { NativeLock guard(device); Record([=](NativeContext *context) { context->DrawIndexedInstanced(indices, instances, start, vertex, first_instance); }); return; }
 NativeLock guard(device); if (device->functions.pfnDrawIndexedInstanced) device->functions.pfnDrawIndexedInstanced(device->driver_device, indices, instances, start, vertex, first_instance); }

void STDMETHODCALLTYPE NativeContext::SetPredication(ID3D11Predicate *value, BOOL expected)
{
    NativeQuery *query = static_cast<NativeQuery *>(value);
    if (query && (query->device != device || !query->is_predicate)) return;
    if (!device->functions.pfnSetPredication) { Unimplemented("SetPredication DDI"); return; }
    NativeLock guard(device);
    if (deferred && !recording_disabled)
    {
        NativeCommandRef<ID3D11Predicate> retained(value);
        Record([=](NativeContext *context) { context->SetPredication(retained.Get(), expected); });
    }
    if (!deferred)
    {
        D3D10DDI_HQUERY handle = {};
        if (query) handle = query->handle;
        device->functions.pfnSetPredication(device->driver_device, handle, expected);
    }
    ReplaceObject(predicate, value);
    predicate_value = expected;
}

void STDMETHODCALLTYPE NativeContext::GetPredication(ID3D11Predicate **out, BOOL *value)
{
    NativeLock guard(device);
    if (out) { *out = predicate; if (*out) (*out)->AddRef(); }
    if (value) *value = predicate_value;
}

void STDMETHODCALLTYPE NativeContext::Begin(ID3D11Asynchronous *async)
{
    if (deferred)
    {
        NativeLock guard(device);
        NativeCommandRef<ID3D11Asynchronous> retained(async);
        Record([=](NativeContext *context) { context->Begin(retained.Get()); });
        return;
    }

    if (!async || !device->functions.pfnQueryBegin) return;
    NativeQuery *query = static_cast<NativeQuery *>(static_cast<ID3D11Query *>(async));
    if (query->device != device || query->desc.Query == D3D11_QUERY_EVENT || query->desc.Query == D3D11_QUERY_TIMESTAMP) return;
    NativeLock guard(device);
    if (query->begun || query == predicate) return;
    query->begun = true;
    query->ended = false;
    device->functions.pfnQueryBegin(device->driver_device, query->handle);
}
void STDMETHODCALLTYPE NativeContext::End(ID3D11Asynchronous *async)
{
    if (deferred)
    {
        NativeLock guard(device);
        NativeCommandRef<ID3D11Asynchronous> retained(async);
        Record([=](NativeContext *context) { context->End(retained.Get()); });
        return;
    }

    if (!async || !device->functions.pfnQueryEnd) return;
    NativeQuery *query = static_cast<NativeQuery *>(static_cast<ID3D11Query *>(async));
    if (query->device != device) return;
    NativeLock guard(device);
    if (query == predicate) return;
    if (!query->begun && query->desc.Query != D3D11_QUERY_EVENT && query->desc.Query != D3D11_QUERY_TIMESTAMP) return;
    device->functions.pfnQueryEnd(device->driver_device, query->handle);
    query->begun = false;
    query->ended = true;
}
HRESULT STDMETHODCALLTYPE NativeContext::GetData(ID3D11Asynchronous *async, void *data, UINT size, UINT flags)
{
    if (deferred) return DXGI_ERROR_INVALID_CALL;

    if (!async || (flags & ~D3D11_ASYNC_GETDATA_DONOTFLUSH)) return E_INVALIDARG;
    NativeQuery *query = static_cast<NativeQuery *>(static_cast<ID3D11Query *>(async));
    if (query->device != device || (size && (!data || size != query->data_size))) return E_INVALIDARG;
    NativeLock guard(device);
    if (!query->ended || (query->desc.MiscFlags & D3D11_QUERY_MISC_PREDICATEHINT)) return DXGI_ERROR_INVALID_CALL;
    /* A non-NULL pointer with zero size is a readiness poll, not a data read. */
    if (!size) data = NULL;
    device->BeginCall();
    device->functions.pfnQueryGetData(device->driver_device, query->handle, data, size, flags);
    if (device->operation_error == DXGI_DDI_ERR_WASSTILLDRAWING || device->operation_error == DXGI_ERROR_WAS_STILL_DRAWING) return S_FALSE;
    return device->operation_error;
}

void STDMETHODCALLTYPE NativeContext::ClearState()
{
    NativeLock guard(device);
    if (deferred && !recording_disabled) Record([](NativeContext *context) { context->ClearState(); });
    bool previous = recording_disabled;
    recording_disabled = true;
    SetPredication(NULL, FALSE);
    VSSetShader(NULL, NULL, 0);
    PSSetShader(NULL, NULL, 0);
    GSSetShader(NULL, NULL, 0);
    HSSetShader(NULL, NULL, 0);
    DSSetShader(NULL, NULL, 0);
    CSSetShader(NULL, NULL, 0);
    ID3D11ShaderResourceView *views[128] = {};
    ID3D11SamplerState *states[16] = {};
    ID3D11Buffer *buffers[14] = {};
    for (UINT i = 0; i < 6; ++i)
    {
        SetShaderResources(i, 0, 128, views);
        SetSamplers(i, 0, 16, states);
        SetConstantBuffers(i, 0, 14, buffers);
    }
    ID3D11UnorderedAccessView *uavs[8] = {};
    CSSetUnorderedAccessViews(0, device->feature_level >= D3D_FEATURE_LEVEL_11_0 ? 8 : 1, uavs, NULL);
    SOSetTargets(0, NULL, NULL);
    OMSetRenderTargetsAndUnorderedAccessViews(0, NULL, NULL, 0, 0, NULL, NULL);
    OMSetBlendState(NULL, NULL, ~0u);
    OMSetDepthStencilState(NULL, 0);
    IASetInputLayout(NULL);
    IASetVertexBuffers(0, 32, NULL, NULL, NULL);
    IASetIndexBuffer(NULL, DXGI_FORMAT_UNKNOWN, 0);
    RSSetState(NULL);
    RSSetViewports(0, NULL);
    RSSetScissorRects(0, NULL);
    IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
    recording_disabled = previous;
}

void STDMETHODCALLTYPE NativeContext::IASetInputLayout(ID3D11InputLayout *layout)
{
    NativeInputLayout *object = static_cast<NativeInputLayout *>(layout);
    if (object && object->device != device) return;
    if (!device->functions.pfnIaSetInputLayout) return;
    NativeLock guard(device);
    D3D10DDI_HELEMENTLAYOUT handle = {};
    if (object) handle = object->handle;
    if (deferred && !recording_disabled)
    {
        NativeCommandRef<ID3D11InputLayout> retained(layout);
        Record([=](NativeContext *context) { context->IASetInputLayout(retained.Get()); });
    }
    if (!deferred) device->functions.pfnIaSetInputLayout(device->driver_device, handle);
    ReplaceObject(input_layout, layout);
}

static ULONGLONG NativeTraceNow()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}
static int NativeTraceCompare(const void *a, const void *b)
{
    ULONGLONG x = *(const ULONGLONG *)a, y = *(const ULONGLONG *)b;
    return (x > y) - (x < y);
}

/* A retained frame is handed to DWM once the kernel reports that its GPU
 * work retired, so Present returns without waiting for the GPU. One thread
 * publishes the frames of every chain strictly in presentation order. */
struct NativePublishRecord
{
    NativePublishRecord *next;
    NativeSwapChain *swapchain;
    DWM_DX_SURFACE_EXCHANGE exchange;
    HANDLE completion; /* set by the kernel when the frame's GPU work retires */
    HANDLE release;    /* the chain's release event of the published buffer */
    bool busy;
};

class NativeSwapChain final : public IDXGISwapChain3, public NativeAllocation
{
public:
    LONG references = 1;
    NativeDevice *device;
    IDXGIFactory *factory;
    HWND window;
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc = {};
    DXGI_RGBA background = {};
    NativePrivateData private_data;
    NativeTexture2D *buffers[16] = {};
    /* A chain whose buffers cannot be handed to DWM copies each frame into
     * the free member of this ring. One frame is on screen and one may be
     * waiting to be picked up, so a third keeps Present from blocking. */
    NativeTexture2D *transports[3] = {};
    UINT transport_frame[3] = {}, transport_fills = 0;
    NativeTexture2D *transport = NULL; /* the latest frame, one of transports */
    RECT frame_damage[8] = {};
    DWM_DX_SURFACE_EXCHANGE publication = {};
    /* Client publications are retained: DWM samples each frame in place and
     * sets that buffer's release event once it no longer reads it. */
    D3DKMT_HANDLE release_shares[16] = {};
    HANDLE release_events[16] = {};
    /* Each queued frame holds a distinct buffer, so a chain has at most 16. */
    NativePublishRecord publish_records[16] = {};
    CRITICAL_SECTION publish_lock;
    HANDLE publishes_drained = NULL;
    UINT queued_publishes = 0;
    HMODULE retirement_module = NULL;
    UINT present_count = 0;
    struct { ULONGLONG interval, present, consumed, producer, driver, submit, poll, sleeps; } trace_frames[4096] = {};
    ULONG trace_count = 0, trace_lost = 0;
    ULONGLONG trace_previous = 0, trace_consumed = 0, trace_producer = 0, trace_driver = 0;
    ULONGLONG trace_submit = 0, trace_poll = 0, trace_sleeps = 0;
    HRESULT PresentMeasured(UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
    void DumpTrace();
    bool primary = false;
    bool composition = false;
    bool transport_valid = false;

    NativeSwapChain(NativeDevice *d, IDXGIFactory *f, HWND w) : device(d), factory(f), window(w)
    {
        InitializeCriticalSection(&publish_lock);
        device->AddRef();
        factory->AddRef();
    }
    ~NativeSwapChain()
    {
        DrainPublishes();
        DumpTrace();
        RetirePublication();
        CloseReleaseEvents();
        /* The publishing thread signals the drain while it holds the lock. */
        EnterCriticalSection(&publish_lock);
        LeaveCriticalSection(&publish_lock);
        DeleteCriticalSection(&publish_lock);
        for (UINT i = 0; i < 16; ++i) if (publish_records[i].completion) CloseHandle(publish_records[i].completion);
        if (publishes_drained) CloseHandle(publishes_drained);
        for (UINT i = 0; i < 3; ++i) if (transports[i]) transports[i]->Release();
        for (UINT i = 0; i < 16; ++i) if (buffers[i]) buffers[i]->Release();
        factory->Release();
        device->Release();
    }
    HRESULT AllocateBuffers(const DXGI_SWAP_CHAIN_DESC1 &requested);
    static DWORD WINAPI DestroyPending(void *);
    HRESULT RetirePublication();
    HANDLE ReleaseEvent(NativeTexture2D *texture, D3DKMT_HANDLE *share);
    bool Released(NativeTexture2D *texture);
    HRESULT WaitForRelease(NativeTexture2D *texture);
    bool AllReleased(DWORD timeout);
    void CloseReleaseEvents();
    HRESULT Publish(NativeTexture2D *);
    HRESULT FillTransport(UINT, const DXGI_PRESENT_PARAMETERS *);
    HRESULT QueuePublish(const DWM_DX_SURFACE_EXCHANGE &, HANDLE release);
    void CompletePublish(NativePublishRecord *);
    void DrainPublishes();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        if (!IsEqualGUID(iid, IID_IUnknown) && !IsEqualGUID(iid, IID_IDXGIObject)
                && !IsEqualGUID(iid, IID_IDXGIDeviceSubObject) && !IsEqualGUID(iid, IID_IDXGISwapChain)
                && !IsEqualGUID(iid, IID_IDXGISwapChain1) && !IsEqualGUID(iid, IID_IDXGISwapChain2)
                && !IsEqualGUID(iid, IID_IDXGISwapChain3)) return E_NOINTERFACE;
        *out = static_cast<IDXGISwapChain3 *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG count = InterlockedDecrement(&references);
        if (count) return count;
        if (!primary && (queued_publishes || !AllReleased(0))
                && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(DestroyPending), &retirement_module))
        {
            HANDLE thread = CreateThread(NULL, 0, DestroyPending, this, 0, NULL);
            if (thread) { CloseHandle(thread); return 0; }
            FreeLibrary(retirement_module);
            retirement_module = NULL;
        }
        delete this;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *size, void *data) override { NativeLock guard(device); return private_data.Get(guid, size, data); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void *data) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *data) override { NativeLock guard(device); return private_data.Set(guid, sizeof(data), &data, const_cast<IUnknown *>(data)); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID iid, void **out) override { return factory->QueryInterface(iid, out); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid, void **out) override { return device->QueryInterface(iid, out); }
    HRESULT STDMETHODCALLTYPE Present(UINT interval, UINT flags) override { return Present1(interval, flags, NULL); }
    HRESULT STDMETHODCALLTYPE Present1(UINT, UINT, const DXGI_PRESENT_PARAMETERS *) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index, REFIID iid, void **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        NativeLock guard(device);
        if (index >= desc.BufferCount || !buffers[index]) return DXGI_ERROR_INVALID_CALL;
        return buffers[index]->QueryInterface(iid, out);
    }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen, IDXGIOutput *output) override
    {
        if (composition) return DXGI_ERROR_INVALID_CALL;
        if (fullscreen || output) return DXGI_ERROR_UNSUPPORTED;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL *fullscreen, IDXGIOutput **output) override
    {
        if (composition) return DXGI_ERROR_INVALID_CALL;
        if (fullscreen) *fullscreen = !fullscreen_desc.Windowed;
        if (output) *output = NULL;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC *out) override
    {
        if (!out) return E_INVALIDARG;
        NativeLock guard(device);
        ZeroMemory(out, sizeof(*out));
        out->BufferDesc.Width = desc.Width;
        out->BufferDesc.Height = desc.Height;
        out->BufferDesc.RefreshRate = fullscreen_desc.RefreshRate;
        out->BufferDesc.Format = desc.Format;
        out->BufferDesc.ScanlineOrdering = fullscreen_desc.ScanlineOrdering;
        out->BufferDesc.Scaling = fullscreen_desc.Scaling;
        out->SampleDesc = desc.SampleDesc;
        out->BufferUsage = desc.BufferUsage;
        out->BufferCount = desc.BufferCount;
        out->OutputWindow = window;
        out->Windowed = fullscreen_desc.Windowed;
        out->SwapEffect = desc.SwapEffect;
        out->Flags = desc.Flags;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT, UINT, UINT, DXGI_FORMAT, UINT) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        if (composition) return DXGI_ERROR_INVALID_CALL;
        HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        for (UINT index = 0;; ++index)
        {
            IDXGIOutput *candidate = NULL;
            HRESULT hr = device->adapter->EnumOutputs(index, &candidate);
            if (FAILED(hr)) return hr;
            DXGI_OUTPUT_DESC output_desc;
            hr = candidate->GetDesc(&output_desc);
            if (SUCCEEDED(hr) && output_desc.Monitor == monitor) { *out = candidate; return S_OK; }
            candidate->Release();
        }
    }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS *out) override
    {
        if (!out) return E_INVALIDARG;
        ZeroMemory(out, sizeof(*out));
        return DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
    }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT *out) override { if (!out) return E_INVALIDARG; *out = present_count; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1 *out) override { if (!out) return E_INVALIDARG; NativeLock guard(device); *out = desc; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *out) override { if (!out) return E_INVALIDARG; if (composition) return DXGI_ERROR_INVALID_CALL; *out = fullscreen_desc; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND *out) override { if (!out) return E_INVALIDARG; *out = composition ? NULL : window; return composition ? DXGI_ERROR_INVALID_CALL : S_OK; }
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID, void **out) override { if (out) *out = NULL; return DXGI_ERROR_INVALID_CALL; }
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput **out) override { if (!out) return E_INVALIDARG; *out = NULL; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA *value) override { if (!value) return E_INVALIDARG; NativeLock guard(device); background = *value; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA *out) override { if (!out) return E_INVALIDARG; NativeLock guard(device); *out = background; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override { return rotation == DXGI_MODE_ROTATION_IDENTITY ? S_OK : DXGI_ERROR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION *out) override { if (!out) return E_INVALIDARG; *out = DXGI_MODE_ROTATION_IDENTITY; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT width, UINT height) override
    {
        NativeLock guard(device);
        if (!width || !height || width > desc.Width || height > desc.Height) return E_INVALIDARG;
        /* Publication currently covers the complete texture. Do not silently
         * accept a crop that the compositor would then ignore. */
        return width == desc.Width && height == desc.Height ? S_OK : DXGI_ERROR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT *width, UINT *height) override
    {
        if (!width || !height) return E_INVALIDARG;
        NativeLock guard(device);
        *width = desc.Width; *height = desc.Height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT) override { return DXGI_ERROR_INVALID_CALL; }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *) override { return DXGI_ERROR_INVALID_CALL; }
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return NULL; }
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F *matrix) override
    {
        if (!composition) return DXGI_ERROR_INVALID_CALL;
        if (!matrix || matrix->_12 != 0 || matrix->_21 != 0) return E_INVALIDARG;
        return matrix->_11 == 1 && matrix->_22 == 1 && matrix->_31 == 0 && matrix->_32 == 0
                ? S_OK : DXGI_ERROR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F *matrix) override
    {
        if (!composition) return DXGI_ERROR_INVALID_CALL;
        if (!matrix) return E_INVALIDARG;
        *matrix = {1, 0, 0, 1, 0, 0};
        return S_OK;
    }
    /* D3D11 rotates resource identities on Present, so the application's
     * current buffer remains zero (unlike explicit D3D12 buffer rotation). */
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return 0; }
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space, UINT *support) override
    {
        if (!support) return E_INVALIDARG;
        *support = color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
                ? DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE color_space) override
    {
        /* The current compositor and scanout path interpret RGB as SDR. */
        return color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? S_OK : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT, UINT, UINT, DXGI_FORMAT, UINT,
            const UINT *, IUnknown *const *) override
    {
        /* This entry point requires a D3D12 command-queue-backed chain. */
        return DXGI_ERROR_INVALID_CALL;
    }
};

HRESULT STDMETHODCALLTYPE NativeSwapChain::SetPrivateData(REFGUID guid, UINT size, const void *data)
{
    NativeLock guard(device);
    if (!IsEqualGUID(guid, GUID_ReactOSDXGICompositionWindow))
        return private_data.Set(guid, size, data, NULL);
    if (!composition || !data || size != sizeof(reactos_dxgi_composition_target))
        return E_INVALIDARG;
    reactos_dxgi_composition_target target;
    memcpy(&target, data, sizeof(target));
    if (!IsWindow(target.window)) return E_INVALIDARG;
    /* The native publication currently represents a complete client layer.
     * Reject transforms/clips that cannot be represented by that contract. */
    if (target.offset_x || target.offset_y || (target.has_clip &&
            (target.clip.left > 0 || target.clip.top > 0 ||
             target.clip.right < static_cast<LONG>(desc.Width) ||
             target.clip.bottom < static_cast<LONG>(desc.Height)))) return E_NOTIMPL;
    if (window != target.window)
    {
        HRESULT hr = RetirePublication();
        if (FAILED(hr)) return hr;
        window = target.window;
    }
    DrainPublishes();
    if (transport_valid && !publication.GlobalShare)
        return Publish(transport);
    return S_OK;
}

DWORD WINAPI NativeSwapChain::DestroyPending(void *argument)
{
    NativeSwapChain *swapchain = static_cast<NativeSwapChain *>(argument);
    HMODULE module = swapchain->retirement_module;
    /* The last external Release need not block the window's message thread.
     * Keep the native backing alive until DWM has stopped reading it. */
    delete swapchain;
    FreeLibraryAndExitThread(module, 0);
    return 0;
}

static struct
{
    CRITICAL_SECTION lock;
    NativePublishRecord *head, *tail;
    HANDLE wake;
    bool usable;
} native_publish_queue;
static INIT_ONCE native_publish_once = INIT_ONCE_STATIC_INIT;

static DWORD WINAPI NativePublishThread(void *)
{
    for (;;)
    {
        EnterCriticalSection(&native_publish_queue.lock);
        NativePublishRecord *record = native_publish_queue.head;
        if (record)
        {
            native_publish_queue.head = record->next;
            if (!native_publish_queue.head) native_publish_queue.tail = NULL;
        }
        LeaveCriticalSection(&native_publish_queue.lock);
        if (record) record->swapchain->CompletePublish(record);
        else WaitForSingleObject(native_publish_queue.wake, INFINITE);
    }
}

static BOOL CALLBACK NativeInitPublishQueue(PINIT_ONCE, void *, void **)
{
    HMODULE module = NULL;
    InitializeCriticalSection(&native_publish_queue.lock);
    native_publish_queue.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    /* The thread can outlive the last chain and the application's last
     * reference to this module, so it keeps the module loaded. */
    if (!native_publish_queue.wake || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(NativePublishThread), &module))
        return TRUE;
    HANDLE thread = CreateThread(NULL, 0, NativePublishThread, NULL, 0, NULL);
    if (!thread)
    {
        FreeLibrary(module);
        return TRUE;
    }
    CloseHandle(thread);
    native_publish_queue.usable = true;
    return TRUE;
}

/* Queue a retained frame whose GPU work was just flushed. The caller has
 * reset release, which marks the buffer busy until DWM lets it go. */
HRESULT NativeSwapChain::QueuePublish(const DWM_DX_SURFACE_EXCHANGE &exchange, HANDLE release)
{
    InitOnceExecuteOnce(&native_publish_once, NativeInitPublishQueue, NULL, NULL);
    if (!native_publish_queue.usable) return E_OUTOFMEMORY;
    NativePublishRecord *record = NULL;
    EnterCriticalSection(&publish_lock);
    for (UINT i = 0; i < 16 && !record; ++i) if (!publish_records[i].busy) record = &publish_records[i];
    LeaveCriticalSection(&publish_lock);
    if (!record) return E_OUTOFMEMORY;
    if (!record->completion) record->completion = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!publishes_drained) publishes_drained = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (!record->completion || !publishes_drained) return HRESULT_FROM_WIN32(GetLastError());
    HRESULT hr = device->EnqueueSetEvent(record->completion);
    if (FAILED(hr)) return hr;
    record->next = NULL;
    record->swapchain = this;
    record->exchange = exchange;
    record->release = release;
    EnterCriticalSection(&publish_lock);
    record->busy = true;
    if (queued_publishes++ == 0) ResetEvent(publishes_drained);
    LeaveCriticalSection(&publish_lock);
    EnterCriticalSection(&native_publish_queue.lock);
    if (native_publish_queue.tail) native_publish_queue.tail->next = record;
    else native_publish_queue.head = record;
    native_publish_queue.tail = record;
    LeaveCriticalSection(&native_publish_queue.lock);
    SetEvent(native_publish_queue.wake);
    return S_OK;
}

/* Runs on the publishing thread, which never takes the device lock. */
void NativeSwapChain::CompletePublish(NativePublishRecord *record)
{
    /* Device removal also sets the completion event. A frame whose GPU work
     * never retires is dropped rather than shown incomplete. */
    NTSTATUS status = STATUS_TIMEOUT;
    if (WaitForSingleObject(record->completion, 5000) == WAIT_OBJECT_0)
        status = static_cast<NTSTATUS>(NtUserCallOneParam(
                reinterpret_cast<DWORD_PTR>(&record->exchange), DWM_ROUTINE_DXSURFACE));
    EnterCriticalSection(&publish_lock);
    if (status >= 0) publication = record->exchange;
    else SetEvent(record->release);
    record->busy = false;
    if (--queued_publishes == 0) SetEvent(publishes_drained);
    LeaveCriticalSection(&publish_lock);
}

/* Every reader of publication first lets the queued frames of this chain
 * reach DWM, so the publishing thread is its only concurrent writer. */
void NativeSwapChain::DrainPublishes()
{
    if (publishes_drained) WaitForSingleObject(publishes_drained, INFINITE);
}

/* The release event of the buffer allocation currently behind texture. */
HANDLE NativeSwapChain::ReleaseEvent(NativeTexture2D *texture, D3DKMT_HANDLE *share)
{
    D3DKMT_HANDLE shared = 0;
    if (FAILED(device->get_resource_handles(device->runtime_device, texture->runtime_handle.handle,
            NULL, &shared)) || !shared)
        return NULL;
    if (share) *share = shared;
    UINT free_slot = 16;
    for (UINT i = 0; i < 16; ++i)
    {
        if (release_events[i] && release_shares[i] == shared) return release_events[i];
        if (!release_events[i] && free_slot == 16) free_slot = i;
    }
    if (free_slot == 16) return NULL;
    release_events[free_slot] = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (!release_events[free_slot]) return NULL;
    release_shares[free_slot] = shared;
    return release_events[free_slot];
}

bool NativeSwapChain::Released(NativeTexture2D *texture)
{
    HANDLE event = ReleaseEvent(texture, NULL);
    return !event || WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
}

HRESULT NativeSwapChain::WaitForRelease(NativeTexture2D *texture)
{
    HANDLE event = ReleaseEvent(texture, NULL);
    if (!event) return S_OK;
    ULONGLONG start = NativeTraceNow();
    DWORD result = WaitForSingleObject(event, 5000);
    trace_consumed += NativeTraceNow() - start;
    if (result == WAIT_OBJECT_0) return S_OK;
    return result == WAIT_TIMEOUT ? DXGI_ERROR_WAS_STILL_DRAWING : HRESULT_FROM_WIN32(GetLastError());
}

bool NativeSwapChain::AllReleased(DWORD timeout)
{
    DWORD started = GetTickCount();
    for (UINT i = 0; i < 16; ++i)
    {
        if (!release_events[i]) continue;
        DWORD elapsed = GetTickCount() - started;
        if (WaitForSingleObject(release_events[i], elapsed < timeout ? timeout - elapsed : 0) != WAIT_OBJECT_0)
            return false;
    }
    return true;
}

void NativeSwapChain::CloseReleaseEvents()
{
    for (UINT i = 0; i < 16; ++i)
    {
        if (release_events[i]) CloseHandle(release_events[i]);
        release_events[i] = NULL;
        release_shares[i] = 0;
    }
}

HRESULT NativeSwapChain::RetirePublication()
{
    DrainPublishes();
    if (!publication.GlobalShare) return S_OK;
    DWM_DX_SURFACE_EXCHANGE exchange = publication;
    exchange.Action = DWM_DX_SURFACE_UNREGISTER;
    NTSTATUS status = static_cast<NTSTATUS>(NtUserCallOneParam(
            reinterpret_cast<DWORD_PTR>(&exchange), DWM_ROUTINE_DXSURFACE));
    /* A consumed tuple can already have been retired by window destruction
     * or resize. Never unregister a newer publication on that window. */
    HRESULT hr = status == STATUS_NOT_FOUND ? S_OK : StatusToHresult(status);
    if (SUCCEEDED(hr)) ZeroMemory(&publication, sizeof(publication));
    /* DWM releases the retained frames it still holds on its next frame. */
    if (SUCCEEDED(hr) && !AllReleased(5000)) hr = DXGI_ERROR_WAS_STILL_DRAWING;
    return hr;
}

/* Copy the back buffer into a transport DWM has released. That transport
 * holds an older frame, so the damage of every present since is copied. */
HRESULT NativeSwapChain::FillTransport(UINT flags, const DXGI_PRESENT_PARAMETERS *parameters)
{
    UINT frame = transport_fills + 1, index = 3;
    for (UINT i = 0; i < 3; ++i)
    {
        if (transports[i] == transport || !Released(transports[i])) continue;
        if (index == 3 || transport_frame[i] > transport_frame[index]) index = i;
    }
    if (index == 3)
    {
        if (flags & DXGI_PRESENT_DO_NOT_WAIT) return DXGI_ERROR_WAS_STILL_DRAWING;
        HANDLE events[3];
        UINT candidates[3], count = 0;
        for (UINT i = 0; i < 3; ++i)
        {
            if (transports[i] == transport) continue;
            events[count] = ReleaseEvent(transports[i], NULL);
            if (!events[count]) return E_OUTOFMEMORY;
            candidates[count++] = i;
        }
        ULONGLONG start = NativeTraceNow();
        DWORD result = WaitForMultipleObjects(count, events, FALSE, 5000);
        trace_consumed += NativeTraceNow() - start;
        if (result >= WAIT_OBJECT_0 + count)
            return result == WAIT_TIMEOUT ? DXGI_ERROR_WAS_STILL_DRAWING : HRESULT_FROM_WIN32(GetLastError());
        index = candidates[result - WAIT_OBJECT_0];
    }
    RECT full = {0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
    RECT &damage = frame_damage[frame % ARRAYSIZE(frame_damage)];
    damage = full;
    if (parameters && parameters->DirtyRectsCount)
    {
        SetRectEmpty(&damage);
        for (UINT i = 0; i < parameters->DirtyRectsCount; ++i)
            UnionRect(&damage, &damage, &parameters->pDirtyRects[i]);
    }
    UINT held = transport_frame[index];
    RECT older = {};
    if (!held || frame - held > ARRAYSIZE(frame_damage)) older = full;
    for (UINT past = held + 1; past < frame && !EqualRect(&older, &full); ++past)
        UnionRect(&older, &older, &frame_damage[past % ARRAYSIZE(frame_damage)]);
    NativeTexture2D *target = transports[index];
    device->BeginCall();
    if (EqualRect(&older, &full) || EqualRect(&damage, &full))
        device->context->CopyResource(target, buffers[0]);
    else
    {
        const RECT *rects = parameters->pDirtyRects;
        for (UINT i = 0; i <= parameters->DirtyRectsCount; ++i)
        {
            const RECT &r = i < parameters->DirtyRectsCount ? rects[i] : older;
            if (IsRectEmpty(&r)) continue;
            D3D11_BOX box = {static_cast<UINT>(r.left), static_cast<UINT>(r.top), 0,
                            static_cast<UINT>(r.right), static_cast<UINT>(r.bottom), 1};
            device->context->CopySubresourceRegion(target, 0, r.left, r.top, 0, buffers[0], 0, &box);
        }
    }
    if (FAILED(device->operation_error)) return device->operation_error;
    transport_fills = frame;
    transport_frame[index] = frame;
    transport = target;
    return S_OK;
}

HRESULT NativeSwapChain::Publish(NativeTexture2D *texture)
{
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
        return DXGI_ERROR_UNSUPPORTED;
    DWM_DX_SURFACE_EXCHANGE exchange = {};
    exchange.StructSize = sizeof(exchange);
    exchange.Action = DWM_DX_SURFACE_PUBLISH;
    exchange.Window = reinterpret_cast<ULONG_PTR>(window);
    DXGI_ADAPTER_DESC adapter_desc;
    HRESULT hr = device->adapter->GetDesc(&adapter_desc);
    if (FAILED(hr)) return hr;
    exchange.AdapterLuid = adapter_desc.AdapterLuid;
    D3DKMT_HANDLE shared = 0;
    HANDLE release = ReleaseEvent(texture, &shared);
    if (!release) return E_OUTOFMEMORY;
    exchange.GlobalShare = shared;
    exchange.Info.Magic = DWM_DX_SURFACE_INFO_MAGIC;
    exchange.Info.Version = DWM_DX_SURFACE_INFO_VERSION_GPU;
    exchange.Info.Width = desc.Width;
    exchange.Info.Height = desc.Height;
    exchange.Info.Format = desc.Format;
    exchange.Flags = DWM_DX_PUBLISH_RETAINED;
    if (desc.AlphaMode == DXGI_ALPHA_MODE_PREMULTIPLIED)
        exchange.Flags |= DWM_DX_PUBLISH_PREMULTIPLIED;
    exchange.ReadyEvent = reinterpret_cast<ULONG_PTR>(release);
    /* A transport retains undamaged pixels across Present1 calls, and a flip
     * buffer holds a complete frame, so the whole image is published. */
    exchange.UpdateRect.right = desc.Width;
    exchange.UpdateRect.bottom = desc.Height;
    ResetEvent(release);
    ULONGLONG trace_start = NativeTraceNow();
    hr = QueuePublish(exchange, release);
    trace_submit += NativeTraceNow() - trace_start;
    if (FAILED(hr)) SetEvent(release);
    return hr;
}

struct NativePresentContext
{
    NativeSwapChain *swapchain;
    UINT interval;
    UINT flags;
    const DXGI_PRESENT_PARAMETERS *parameters;
    D3DKMT_HANDLE source_allocation;
    bool submitted;
    bool composed;
    HRESULT result;
};

static HRESULT APIENTRY NativePresent(HANDLE runtime_device, DXGIDDICB_PRESENT *callback)
{
    if (!callback || !callback->pDXGIContext) return E_INVALIDARG;
    NativePresentContext *context = static_cast<NativePresentContext *>(callback->pDXGIContext);
    NativeSwapChain *swapchain = context->swapchain;
    NativeDevice *device = swapchain->device;
    if (runtime_device != device->runtime_device || !callback->hSrcAllocation
            || callback->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT) return E_INVALIDARG;
    if (!swapchain->primary)
    {
        /* This runtime publishes a shared client texture to DWM after the UMD
         * returns and the producing GPU EVENT completes. A destination-free
         * KMT Blt would be a second, unresolved presentation of that frame.
         * Only the exact owned source can use this composition transport. */
        if (!context->source_allocation || callback->hSrcAllocation != context->source_allocation ||
            callback->hDstAllocation || callback->BroadcastContextCount || context->composed)
            context->result = E_INVALIDARG;
        else
        {
            context->composed = true;
            context->result = S_OK;
        }
        return context->result;
    }
    D3DKMT_PRESENT present = {};
    present.hDevice = device->km_device;
    present.hContext = callback->hContext ? static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(callback->hContext)) : 0;
    present.hWindow = swapchain->window;
    present.hSource = callback->hSrcAllocation;
    present.hDestination = callback->hDstAllocation;
    present.SrcRect.right = swapchain->desc.Width;
    present.SrcRect.bottom = swapchain->desc.Height;
    if (!GetClientRect(swapchain->window, &present.DstRect)) return DXGI_ERROR_INVALID_CALL;
    POINT origin = {0, 0};
    if (!ClientToScreen(swapchain->window, &origin)) return DXGI_ERROR_INVALID_CALL;
    OffsetRect(&present.DstRect, origin.x, origin.y);
    present.Flags.Blt = 1;
    present.Flags.SrcRectValid = present.Flags.DstRectValid = 1;
    present.Flags.FlipDoNotWait = !!(context->flags & DXGI_PRESENT_DO_NOT_WAIT);
    present.Flags.FlipRestart = !!(context->flags & DXGI_PRESENT_RESTART);
    present.FlipInterval = static_cast<D3DDDI_FLIPINTERVAL_TYPE>(context->interval);
    present.PresentCount = swapchain->present_count + 1;
    present.Flags.PresentCountValid = 1;
    present.BroadcastContextCount = callback->BroadcastContextCount;
    for (UINT i = 0; i < callback->BroadcastContextCount; ++i)
        present.BroadcastContext[i] = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(callback->BroadcastContext[i]));
    /* The negotiated D3D11.0 interface uses the Windows 7 callback prefix.
     * Later WDDM fields are not present in every driver's stack object. */
    /* The registered compositor's complete output is promoted to a flip by
     * win32k. Its damage describes rendering work, not a partial scanout. */
    if (!swapchain->primary && context->parameters && context->parameters->DirtyRectsCount)
    {
        present.SubRectCnt = context->parameters->DirtyRectsCount;
        present.pSrcSubRects = context->parameters->pDirtyRects;
    }
    /* The UMD flushed before this callback, but its rendering and present
     * contexts can use different GPU engines. Retire the producing work
     * before the kernel may select this private compositor primary. */
    D3DKMT_WAITFORIDLE ready = {};
    ready.hDevice = device->km_device;
    NTSTATUS status = D3DKMTWaitForIdle(&ready);
    if (status != STATUS_SUCCESS)
    {
        context->result = StatusToHresult(status);
        return context->result;
    }
    status = D3DKMTPresent(&present);
    context->submitted = status == STATUS_SUCCESS;
    if (context->submitted)
    {
        /* This private compositor chain reuses two primary buffers. Its GPU
         * EVENT query only covers rendering: the accepted present still owns
         * its source until the kernel's passive scanout step completes. */
        status = D3DKMTWaitForIdle(&ready);
    }
    /* Only the privately registered compositor output can be suspended by
     * source ownership. Ordinary client flip chains keep their DXGI contract. */
    context->result = swapchain->primary && status == STATUS_GRAPHICS_PRESENT_OCCLUDED
            ? DXGI_STATUS_OCCLUDED : status == STATUS_DEVICE_BUSY
            ? DXGI_ERROR_WAS_STILL_DRAWING : StatusToHresult(status);
    return context->result;
}

HRESULT NativeSwapChain::AllocateBuffers(const DXGI_SWAP_CHAIN_DESC1 &requested)
{
    if (requested.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) return DXGI_ERROR_UNSUPPORTED;
    if (!requested.Width || !requested.Height || !requested.BufferCount || requested.BufferCount > 16
            || requested.SampleDesc.Count != 1 || requested.SampleDesc.Quality || requested.Stereo) return DXGI_ERROR_INVALID_CALL;
    bool flip = requested.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || requested.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if ((flip && requested.BufferCount < 2) || (!flip && requested.SwapEffect != DXGI_SWAP_EFFECT_DISCARD
            && requested.SwapEffect != DXGI_SWAP_EFFECT_SEQUENTIAL)) return DXGI_ERROR_INVALID_CALL;
    if (requested.BufferCount > 1 && (!device->dxgi_functions.pfnRotateResourceIdentities || !device->rotate_resources)) return DXGI_ERROR_UNSUPPORTED;
    if (requested.AlphaMode != DXGI_ALPHA_MODE_UNSPECIFIED && requested.AlphaMode != DXGI_ALPHA_MODE_IGNORE
            && !(composition && requested.AlphaMode == DXGI_ALPHA_MODE_PREMULTIPLIED)) return DXGI_ERROR_UNSUPPORTED;
    if (!primary && requested.Format != DXGI_FORMAT_B8G8R8A8_UNORM
            && requested.Format != DXGI_FORMAT_R8G8B8A8_UNORM) return DXGI_ERROR_UNSUPPORTED;
    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = requested.Width;
    texture_desc.Height = requested.Height;
    texture_desc.MipLevels = texture_desc.ArraySize = 1;
    texture_desc.Format = requested.Format;
    texture_desc.SampleDesc = requested.SampleDesc;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    if (requested.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT) texture_desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (requested.BufferUsage & DXGI_USAGE_SHADER_INPUT) texture_desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (requested.BufferUsage & DXGI_USAGE_UNORDERED_ACCESS) texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    DXGI_DDI_PRIMARY_DESC primary_desc = {};
    primary_desc.ModeDesc.Width = requested.Width;
    primary_desc.ModeDesc.Height = requested.Height;
    primary_desc.ModeDesc.Format = requested.Format;
    primary_desc.ModeDesc.RefreshRate.Numerator = fullscreen_desc.RefreshRate.Numerator;
    primary_desc.ModeDesc.RefreshRate.Denominator = fullscreen_desc.RefreshRate.Denominator;
    primary_desc.ModeDesc.ScanlineOrdering = DXGI_DDI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    primary_desc.ModeDesc.Rotation = DXGI_DDI_MODE_ROTATION_IDENTITY;
    NativeTexture2D *new_buffers[16] = {};
    NativeTexture2D *new_transports[3] = {};
    HRESULT hr = S_OK;
    for (UINT i = 0; i < requested.BufferCount; ++i)
    {
        ID3D11Texture2D *texture = NULL;
        hr = device->CreateTexture(&texture_desc, NULL, &texture, true, primary ? &primary_desc : NULL);
        if (FAILED(hr)) break;
        new_buffers[i] = static_cast<NativeTexture2D *>(texture);
    }
    /* Single buffering needs an immutable publication; composition also
     * needs retained history for damage across rotating back buffers. */
    for (UINT i = 0; SUCCEEDED(hr) && !primary && (requested.BufferCount == 1 || composition) && i < 3; ++i)
    {
        ID3D11Texture2D *texture = NULL;
        hr = device->CreateTexture(&texture_desc, NULL, &texture, true);
        if (SUCCEEDED(hr)) new_transports[i] = static_cast<NativeTexture2D *>(texture);
    }
    if (SUCCEEDED(hr)) hr = RetirePublication();
    if (SUCCEEDED(hr))
    {
        for (UINT i = 0; i < 16; ++i)
        {
            if (buffers[i]) buffers[i]->Release();
            buffers[i] = new_buffers[i];
            new_buffers[i] = NULL;
        }
        for (UINT i = 0; i < 3; ++i)
        {
            if (transports[i]) transports[i]->Release();
            transports[i] = new_transports[i];
            new_transports[i] = NULL;
            transport_frame[i] = 0;
        }
        transport_fills = 0;
        transport = NULL;
        transport_valid = false;
        desc = requested;
        CloseReleaseEvents();
    }
    for (UINT i = 0; i < 3; ++i) if (new_transports[i]) new_transports[i]->Release();
    for (UINT i = 0; i < 16; ++i) if (new_buffers[i]) new_buffers[i]->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::create_swapchain(IDXGIFactory *factory, HWND window,
        const DXGI_SWAP_CHAIN_DESC1 *requested, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,
        IDXGIOutput *output, IDXGISwapChain1 **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!factory || (window && !IsWindow(window)) || !requested || !fullscreen) return DXGI_ERROR_INVALID_CALL;
    if (!fullscreen->Windowed || output) return DXGI_ERROR_UNSUPPORTED;
    if (!dxgi_functions.pfnPresent) return DXGI_ERROR_UNSUPPORTED;
    NativeLock guard(this);
    NativeSwapChain *swapchain = new NativeSwapChain(this, factory, window);
    if (!swapchain) return E_OUTOFMEMORY;
    swapchain->fullscreen_desc = *fullscreen;
    swapchain->composition = !window;
    swapchain->primary = window && GetPropW(window, DWM_PROP_GPU_OUTPUT) != NULL;
    DXGI_SWAP_CHAIN_DESC1 desc = *requested;
    RECT client = {};
    HRESULT hr = !window || GetClientRect(window, &client) ? S_OK : DXGI_ERROR_INVALID_CALL;
    if (!window && (!desc.Width || !desc.Height)) hr = DXGI_ERROR_INVALID_CALL;
    if (!desc.Width) desc.Width = max(1l, client.right - client.left);
    if (!desc.Height) desc.Height = max(1l, client.bottom - client.top);
    if (SUCCEEDED(hr)) hr = swapchain->AllocateBuffers(desc);
    if (FAILED(hr)) { swapchain->Release(); return hr; }
    *out = swapchain;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeSwapChain::ResizeBuffers(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    NativeLock guard(device);
    for (UINT i = 0; i < desc.BufferCount; ++i)
        if (buffers[i] && buffers[i]->references != 1) return DXGI_ERROR_INVALID_CALL;
    DXGI_SWAP_CHAIN_DESC1 requested = desc;
    RECT client = {};
    if (composition ? (!width || !height) : !GetClientRect(window, &client)) return DXGI_ERROR_INVALID_CALL;
    if (count) requested.BufferCount = count;
    requested.Width = width ? width : max(1l, client.right - client.left);
    requested.Height = height ? height : max(1l, client.bottom - client.top);
    if (format != DXGI_FORMAT_UNKNOWN) requested.Format = format;
    requested.Flags = flags;
    return AllocateBuffers(requested);
}

HRESULT STDMETHODCALLTYPE NativeSwapChain::Present1(UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS *parameters)
{
    ULONGLONG start = NativeTraceNow();
    trace_consumed = trace_producer = trace_driver = 0;
    trace_submit = trace_poll = trace_sleeps = 0;
    HRESULT hr = PresentMeasured(interval, flags, parameters);
    ULONGLONG end = NativeTraceNow();
    if (hr == S_OK && !(flags & DXGI_PRESENT_TEST))
    {
        if (trace_count < ARRAYSIZE(trace_frames))
        {
            trace_frames[trace_count].interval = trace_previous ? end - trace_previous : 0;
            trace_frames[trace_count].present = end - start;
            trace_frames[trace_count].consumed = trace_consumed;
            trace_frames[trace_count].producer = trace_producer;
            trace_frames[trace_count].driver = trace_driver;
            trace_frames[trace_count].submit = trace_submit;
            trace_frames[trace_count].poll = trace_poll;
            trace_frames[trace_count].sleeps = trace_sleeps;
            ++trace_count;
        }
        else ++trace_lost;
        trace_previous = end;
    }
    return hr;
}

void NativeSwapChain::DumpTrace()
{
    const ULONG skip = 60;
    if (primary || trace_count <= skip) return;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    ULONGLONG *values = static_cast<ULONGLONG *>(HeapAlloc(GetProcessHeap(), 0, trace_count * sizeof(*values)));
    if (!values) return;
    const char *names[] = {"interval", "present", "consumed_wait", "producer_wait", "driver_present", "producer_submit", "producer_poll", "producer_sleeps"};
    for (ULONG metric = 0; metric < ARRAYSIZE(names); ++metric)
    {
        ULONGLONG sum = 0;
        ULONG count = trace_count - skip;
        for (ULONG i = skip; i < trace_count; ++i)
        {
            auto &f = trace_frames[i];
            ULONGLONG v = metric == 0 ? f.interval : metric == 1 ? f.present : metric == 2 ? f.consumed : metric == 3 ? f.producer : metric == 4 ? f.driver : metric == 5 ? f.submit : metric == 6 ? f.poll : f.sleeps;
            values[i - skip] = v; sum += v;
        }
        qsort(values, count, sizeof(*values), NativeTraceCompare);
        char line[384];
        _snprintf(line, sizeof(line), "NATIVE_PRESENT_TIMING pid=%lu metric=%s samples=%lu lost=%lu avg_us=%I64u p95_us=%I64u p99_us=%I64u max_us=%I64u buffers=%u transport=%u\n",
            GetCurrentProcessId(), names[metric], count, trace_lost,
            sum * 1000000 / frequency.QuadPart / count,
            values[(count - 1) * 95 / 100] * 1000000 / frequency.QuadPart,
            values[(count - 1) * 99 / 100] * 1000000 / frequency.QuadPart,
            values[count - 1] * 1000000 / frequency.QuadPart, desc.BufferCount, transport != NULL);
        line[sizeof(line)-1] = 0; OutputDebugStringA(line);
    }
    HeapFree(GetProcessHeap(), 0, values);
}

HRESULT NativeSwapChain::PresentMeasured(UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS *parameters)
{
    if (interval > 4 || (flags & ~(DXGI_PRESENT_TEST | DXGI_PRESENT_DO_NOT_WAIT | DXGI_PRESENT_RESTART
            | DXGI_PRESENT_DO_NOT_SEQUENCE))) return DXGI_ERROR_INVALID_CALL;
    if (parameters && ((parameters->DirtyRectsCount && !parameters->pDirtyRects)
            || (!!parameters->pScrollRect != !!parameters->pScrollOffset))) return DXGI_ERROR_INVALID_CALL;
    if (parameters && parameters->pScrollRect) return E_NOTIMPL;
    NativeLock guard(device);
    HRESULT device_status = device->GetDeviceRemovedReason();
    if (FAILED(device_status)) return device_status;
    if (!(composition && !window) && !IsWindow(window)) return DXGI_ERROR_INVALID_CALL;
    if (IsIconic(window)) return DXGI_STATUS_OCCLUDED;
    if (flags & DXGI_PRESENT_TEST)
    {
        if (!primary) return S_OK;
        D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP check = {};
        check.hAdapter = device->km_adapter;
        /* Native compositor scanout currently targets the primary source. */
        check.VidPnSourceId = 0;
        NTSTATUS status = D3DKMTCheckVidPnExclusiveOwnership(&check);
        if (status == STATUS_GRAPHICS_PRESENT_OCCLUDED) return DXGI_STATUS_OCCLUDED;
        if (status == STATUS_SUCCESS) return S_OK;
        return status < 0 ? StatusToHresult(status) : E_UNEXPECTED;
    }
    /* The private compositor primary has a blocking retirement contract.
     * Ordinary client swap chains retain their nonblocking publication path. */
    if (primary && (flags & DXGI_PRESENT_DO_NOT_WAIT)) return DXGI_ERROR_INVALID_CALL;
    if (parameters)
    {
        for (UINT i = 0; i < parameters->DirtyRectsCount; ++i)
        {
            const RECT &rect = parameters->pDirtyRects[i];
            if (rect.left < 0 || rect.top < 0 || rect.right <= rect.left || rect.bottom <= rect.top
                    || static_cast<UINT>(rect.right) > desc.Width || static_cast<UINT>(rect.bottom) > desc.Height)
                return DXGI_ERROR_INVALID_CALL;
        }
        if (!primary && !transports[0] && parameters->DirtyRectsCount)
        {
            const RECT &rect = parameters->pDirtyRects[0];
            /* The current compositor publication carries one complete client
             * frame; precise partial damage needs a versioned history. */
            if (parameters->DirtyRectsCount != 1 || rect.left || rect.top
                    || static_cast<UINT>(rect.right) != desc.Width || static_cast<UINT>(rect.bottom) != desc.Height)
                return E_NOTIMPL;
        }
    }
    bool sequence = !(flags & DXGI_PRESENT_DO_NOT_SEQUENCE);
    if (!sequence && !present_count) return S_OK;
    if (!primary && sequence)
    {
        HRESULT hr = S_OK;
        if (transports[0])
            hr = FillTransport(flags, parameters);
        /* The next buffer becomes the back buffer. With three or more, DWM
         * frees it without this present, so a caller that may not block is
         * refused until it has. A two-buffer chain frees it by presenting. */
        else if ((flags & DXGI_PRESENT_DO_NOT_WAIT) && desc.BufferCount > 2 && !Released(buffers[1]))
            hr = DXGI_ERROR_WAS_STILL_DRAWING;
        if (FAILED(hr)) return hr;
    }
    NativeTexture2D *source = transport ? transport : buffers[sequence ? 0 : desc.BufferCount - 1];
    D3DKMT_HANDLE source_allocation = 0;
    if (!primary)
    {
        if (!device->get_single_allocation) return DXGI_ERROR_UNSUPPORTED;
        HRESULT hr = device->get_single_allocation(device->runtime_device, source->runtime_handle.handle, &source_allocation);
        if (FAILED(hr)) return hr;
        if (!source_allocation) return DXGI_ERROR_INVALID_CALL;
    }
    NativePresentContext context = {this, interval, flags, parameters, source_allocation, false, false, E_FAIL};
    DXGI_DDI_ARG_PRESENT args = {};
    args.hDevice = reinterpret_cast<DXGI_DDI_HDEVICE>(device->driver_device.pDrvPrivate);
    args.hSurfaceToPresent = reinterpret_cast<DXGI_DDI_HRESOURCE>(source->handle.pDrvPrivate);
    args.pDXGIContext = &context;
    args.Flags.Blt = 1;
    args.FlipInterval = static_cast<DXGI_DDI_FLIP_INTERVAL_TYPE>(interval);
    device->BeginCall();
    ULONGLONG trace_driver_start = NativeTraceNow();
    HRESULT hr = device->dxgi_functions.pfnPresent(&args);
    trace_driver += NativeTraceNow() - trace_driver_start;
    if (FAILED(hr)) return hr;
    if (FAILED(device->operation_error)) return device->operation_error;
    if (hr != S_OK) return hr;
    /* Drivers may return S_OK after a nonfatal callback result. A suspended
     * output did not present a frame and must not advance buffer identities. */
    if (context.result != S_OK) return context.result;
    if (primary ? !context.submitted : !context.composed) return E_FAIL;
    if (!primary && sequence)
    {
        transport_valid = transport != NULL;
        /* An unbound composition chain retains the latest frame. Commit
         * publishes it when a visual first acquires a target window. */
        if (window)
        {
            hr = Publish(source);
            if (FAILED(hr)) return hr;
        }
    }
    ++present_count;
    if (desc.BufferCount > 1 && sequence)
    {
        DXGI_DDI_HRESOURCE resources[16];
        HANDLE runtime_resources[16];
        for (UINT i = 0; i < desc.BufferCount; ++i)
        {
            resources[i] = reinterpret_cast<DXGI_DDI_HRESOURCE>(buffers[i]->handle.pDrvPrivate);
            runtime_resources[i] = buffers[i]->runtime_handle.handle;
        }
        DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES rotate = {args.hDevice, resources, desc.BufferCount};
        hr = device->dxgi_functions.pfnRotateResourceIdentities(&rotate);
        if (SUCCEEDED(hr)) hr = device->rotate_resources(device->runtime_device, runtime_resources, desc.BufferCount);
        if (FAILED(hr)) { device->removed_reason = DXGI_ERROR_DRIVER_INTERNAL_ERROR; return hr; }
        /* The application renders into the new back buffer once Present
         * returns, so DWM must have released that frame first. */
        if (window && !transports[0])
        {
            hr = WaitForRelease(buffers[0]);
            if (FAILED(hr)) return hr;
        }
    }
    if (sequence && (desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD))
    {
        ID3D11RenderTargetView *targets[8];
        for (UINT i = 0; i < 8; ++i)
        {
            NativeRenderTargetView *view = static_cast<NativeRenderTargetView *>(device->context->render_targets[i]);
            targets[i] = view && view->texture == buffers[0] ? NULL : view;
        }
        device->context->OMSetRenderTargets(8, targets, NULL);
    }
    return S_OK;
}

static UINT NativeVertexFormatSize(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT: return 16;
        case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT: return 12;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT: return 8;
        case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_UINT: case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SNORM: case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R16G16_SINT:
        case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT: return 4;
        case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_UINT: case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_R8G8_SINT:
        case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SNORM: case DXGI_FORMAT_R16_SINT: return 2;
        case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SNORM: case DXGI_FORMAT_R8_SINT: return 1;
        default: return 0;
    }
}

static UINT NativeBufferFormatSize(DXGI_FORMAT format)
{
    /* These typed buffer formats are mandatory independently of the DDI's
     * optional BUFFER bit, just like the core vertex-input format set. */
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8X8_UNORM) return 4;
    return NativeVertexFormatSize(format);
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC *elements, UINT count,
        const void *code, SIZE_T length, ID3D11InputLayout **out)
{
    if (out) *out = NULL;
    if ((count && !elements) || count > D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT || !code || !length) return E_INVALIDARG;
    if (!functions.pfnCalcPrivateElementLayoutSize || !functions.pfnCreateElementLayout || !functions.pfnDestroyElementLayout) return E_NOTIMPL;
    struct InputSignature
    {
        vkd3d_shader_signature value = {};
        ~InputSignature() { vkd3d_shader_free_shader_signature(&value); }
    } signature;
    vkd3d_shader_code bytecode = {code, length};
    int result = vkd3d_shader_parse_input_signature(&bytecode, &signature.value, NULL);
    if (result < 0) return result == VKD3D_ERROR_OUT_OF_MEMORY ? E_OUTOFMEMORY : E_INVALIDARG;
    D3D10DDIARG_INPUT_ELEMENT_DESC declarations[D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT] = {};
    UINT offsets[32] = {}, slot_classes[32] = {}, step_rates[32] = {}, used = 0;
    bool used_slots[32] = {};
    for (UINT i = 0; i < count; ++i)
    {
        const D3D11_INPUT_ELEMENT_DESC &input = elements[i];
        UINT size = NativeVertexFormatSize(input.Format), support = 0;
        if (!input.SemanticName || !size || input.InputSlot >= 32
                || (input.InputSlotClass != D3D11_INPUT_PER_VERTEX_DATA && input.InputSlotClass != D3D11_INPUT_PER_INSTANCE_DATA)
                || (input.InputSlotClass == D3D11_INPUT_PER_VERTEX_DATA && input.InstanceDataStepRate)) return E_INVALIDARG;
        if (used_slots[input.InputSlot] && (slot_classes[input.InputSlot] != input.InputSlotClass
                || step_rates[input.InputSlot] != input.InstanceDataStepRate)) return E_INVALIDARG;
        used_slots[input.InputSlot] = true;
        slot_classes[input.InputSlot] = input.InputSlotClass;
        step_rates[input.InputSlot] = input.InstanceDataStepRate;
        if (FAILED(CheckFormatSupport(input.Format, &support)) || !(support & D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER)) return E_INVALIDARG;
        UINT alignment = min(size, 4u);
        UINT offset = input.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT
                ? (offsets[input.InputSlot] + alignment - 1) & ~(alignment - 1) : input.AlignedByteOffset;
        if (offset % alignment || offset > D3D11_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES - size) return E_INVALIDARG;
        offsets[input.InputSlot] = offset + size;
        vkd3d_shader_signature_element *entry = vkd3d_shader_find_signature_element(&signature.value,
                input.SemanticName, input.SemanticIndex, 0);
        if (!entry) continue;
        for (UINT j = 0; j < used; ++j)
            if (declarations[j].InputRegister == entry->register_index) return E_INVALIDARG;
        D3D10DDIARG_INPUT_ELEMENT_DESC &declaration = declarations[used++];
        declaration.InputSlot = input.InputSlot;
        declaration.AlignedByteOffset = offset;
        declaration.Format = input.Format;
        declaration.InputSlotClass = static_cast<D3D10_DDI_INPUT_CLASSIFICATION>(input.InputSlotClass);
        declaration.InstanceDataStepRate = input.InstanceDataStepRate;
        declaration.InputRegister = entry->register_index;
    }
    for (UINT i = 0; i < signature.value.element_count; ++i)
    {
        const vkd3d_shader_signature_element &entry = signature.value.elements[i];
        if (!_stricmp(entry.semantic_name, "SV_VertexID") || !_stricmp(entry.semantic_name, "SV_InstanceID")) continue;
        bool found = false;
        for (UINT j = 0; j < used; ++j) if (declarations[j].InputRegister == entry.register_index) found = true;
        if (!found) return E_INVALIDARG;
    }
    if (!out) return S_FALSE;
    NativeLock guard(this);
    NativeInputLayout *layout = new NativeInputLayout(this);
    if (!layout) return E_OUTOFMEMORY;
    D3D10DDIARG_CREATEELEMENTLAYOUT args = {declarations, used};
    SIZE_T size = functions.pfnCalcPrivateElementLayoutSize(driver_device, &args);
    layout->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!layout->handle.pDrvPrivate) { layout->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTELEMENTLAYOUT runtime_layout = {layout};
    BeginCall();
    functions.pfnCreateElementLayout(driver_device, &args, layout->handle, runtime_layout);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { layout->Release(); return hr; }
    layout->created = true;
    *out = layout;
    return S_OK;
}

void STDMETHODCALLTYPE NativeContext::IASetVertexBuffers(UINT start, UINT count, ID3D11Buffer *const *buffers,
        const UINT *strides, const UINT *offsets)
{
    if (start > 32 || count > 32 - start || (buffers && count && (!strides || !offsets))
            || !device->functions.pfnIaSetVertexBuffers) return;
    NativeLock guard(device);
    D3D10DDI_HRESOURCE handles[32] = {};
    UINT new_strides[32] = {}, new_offsets[32] = {};
    for (UINT i = 0; i < count; ++i)
    {
        new_strides[i] = strides ? strides[i] : 0;
        new_offsets[i] = offsets ? offsets[i] : 0;
        if (!buffers || !buffers[i]) continue;
        NativeBuffer *buffer = GetNativeBuffer(buffers[i], device);
        if (!buffer || !(buffer->desc.BindFlags & D3D11_BIND_VERTEX_BUFFER)
                || strides[i] > D3D11_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES) return;
        handles[i] = buffer->handle;
    }
    if (deferred && !recording_disabled)
    {
        NativeCommandArray<ID3D11Buffer *> values(count, buffers);
        NativeCommandArray<UINT> copied_strides(count, new_strides), copied_offsets(count, new_offsets);
        if (Captured(values) && Captured(copied_strides) && Captured(copied_offsets))
            Record([=](NativeContext *context) { context->IASetVertexBuffers(start, count,
                    values.Data(), copied_strides.Data(), copied_offsets.Data()); });
    }
    if (!deferred) device->functions.pfnIaSetVertexBuffers(device->driver_device, start, count, handles, new_strides, new_offsets);
    for (UINT i = 0; i < count; ++i)
    {
        ReplaceObject(vertex_buffers[start + i], buffers ? buffers[i] : NULL);
        vertex_strides[start + i] = new_strides[i];
        vertex_offsets[start + i] = new_offsets[i];
    }
}

void STDMETHODCALLTYPE NativeContext::IASetIndexBuffer(ID3D11Buffer *buffer, DXGI_FORMAT format, UINT offset)
{
    if (!device->functions.pfnIaSetIndexBuffer) return;
    NativeLock guard(device);
    D3D10DDI_HRESOURCE handle = {};
    if (buffer)
    {
        NativeBuffer *object = GetNativeBuffer(buffer, device);
        UINT alignment = format == DXGI_FORMAT_R16_UINT ? 2 : format == DXGI_FORMAT_R32_UINT ? 4 : 0;
        if (!object || !(object->desc.BindFlags & D3D11_BIND_INDEX_BUFFER) || !alignment || offset % alignment) return;
        handle = object->handle;
    }
    if (deferred && !recording_disabled)
    {
        NativeCommandRef<ID3D11Buffer> retained(buffer);
        Record([=](NativeContext *context) { context->IASetIndexBuffer(retained.Get(), format, offset); });
    }
    if (!deferred) device->functions.pfnIaSetIndexBuffer(device->driver_device, handle, format, offset);
    ReplaceObject(index_buffer, buffer);
    index_format = format;
    index_offset = offset;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateDepthStencilView(ID3D11Resource *resource,
        const D3D11_DEPTH_STENCIL_VIEW_DESC *input, ID3D11DepthStencilView **out)
{
    if (out) *out = NULL;
    NativeTextureInfo info;
    NativeTextureInfo *texture = GetNativeTexture(resource, this, &info);
    if (!texture || texture->dimension == D3D10DDIRESOURCE_TEXTURE3D
            || !(texture->desc.BindFlags & D3D11_BIND_DEPTH_STENCIL)) return E_INVALIDARG;
    if (!functions.pfnCalcPrivateDepthStencilViewSize || !functions.pfnCreateDepthStencilView
            || !functions.pfnDestroyDepthStencilView) return E_NOTIMPL;
    D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
    if (input) desc = *input;
    else
    {
        desc.Format = texture->desc.Format;
        if (texture->dimension == D3D10DDIRESOURCE_TEXTURE1D)
        {
            desc.ViewDimension = texture->desc.ArraySize > 1 ? D3D11_DSV_DIMENSION_TEXTURE1DARRAY : D3D11_DSV_DIMENSION_TEXTURE1D;
            desc.Texture1DArray.ArraySize = texture->desc.ArraySize;
        }
        else if (texture->desc.SampleDesc.Count > 1)
            desc.ViewDimension = texture->desc.ArraySize > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D11_DSV_DIMENSION_TEXTURE2DMS;
        else
            desc.ViewDimension = texture->desc.ArraySize > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
        if (desc.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DARRAY) desc.Texture2DArray.ArraySize = texture->desc.ArraySize;
        if (desc.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY) desc.Texture2DMSArray.ArraySize = texture->desc.ArraySize;
    }
    if (desc.Flags & ~(D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL)) return E_INVALIDARG;
    D3D11DDIARG_CREATEDEPTHSTENCILVIEW args = {};
    args.hDrvResource = texture->handle;
    args.Format = desc.Format == DXGI_FORMAT_UNKNOWN ? texture->desc.Format : desc.Format;
    args.Flags = desc.Flags;
    args.ResourceDimension = texture->dimension == D3D10DDIRESOURCE_TEXTURE1D
            ? D3D10DDIRESOURCE_TEXTURE1D : D3D10DDIRESOURCE_TEXTURE2D;
    if ((desc.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE1D || desc.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE1DARRAY)
            != (texture->dimension == D3D10DDIRESOURCE_TEXTURE1D)) return E_INVALIDARG;
    DXGI_FORMAT base_format = NativeDepthResourceFormat(args.Format);
    if (base_format == DXGI_FORMAT_UNKNOWN || (texture->desc.Format != args.Format
            && texture->desc.Format != base_format)) return E_INVALIDARG;
    switch (desc.ViewDimension)
    {
        case D3D11_DSV_DIMENSION_TEXTURE1D:
        case D3D11_DSV_DIMENSION_TEXTURE2D:
            if (texture->desc.SampleDesc.Count != 1 || texture->desc.ArraySize != 1) return E_INVALIDARG;
            args.Tex2D.MipSlice = desc.Texture2D.MipSlice;
            args.Tex2D.ArraySize = 1;
            break;
        case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
        case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
            if (texture->desc.SampleDesc.Count != 1) return E_INVALIDARG;
            if (!NativeViewSliceRange(desc.Texture2DArray.FirstArraySlice, desc.Texture2DArray.ArraySize,
                    texture->desc.ArraySize)) return E_INVALIDARG;
            args.Tex2D.MipSlice = desc.Texture2DArray.MipSlice;
            args.Tex2D.FirstArraySlice = desc.Texture2DArray.FirstArraySlice;
            args.Tex2D.ArraySize = desc.Texture2DArray.ArraySize;
            break;
        case D3D11_DSV_DIMENSION_TEXTURE2DMS:
            /* The MS view forms also accept single-sample resources. The
             * DDI describes the actual resource and selects mip zero. */
            args.Tex2D.ArraySize = 1;
            break;
        case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
            if (!NativeViewSliceRange(desc.Texture2DMSArray.FirstArraySlice, desc.Texture2DMSArray.ArraySize,
                    texture->desc.ArraySize)) return E_INVALIDARG;
            args.Tex2D.FirstArraySlice = desc.Texture2DMSArray.FirstArraySlice;
            args.Tex2D.ArraySize = desc.Texture2DMSArray.ArraySize;
            break;
        default: return E_INVALIDARG;
    }
    if (args.Tex2D.MipSlice >= texture->desc.MipLevels || !args.Tex2D.ArraySize
            || args.Tex2D.FirstArraySlice >= texture->desc.ArraySize
            || args.Tex2D.ArraySize > texture->desc.ArraySize - args.Tex2D.FirstArraySlice) return E_INVALIDARG;
    if (!out) return S_FALSE;
    NativeLock guard(this);
    NativeDepthView *view = new NativeDepthView(this);
    if (!view) return E_OUTOFMEMORY;
    view->texture = resource;
    NativeRetainResource(resource);
    view->resource_handle = texture->handle;
    view->desc = desc;
    view->desc.Format = args.Format;
    view->mip_slice = args.Tex2D.MipSlice;
    view->first_slice = args.Tex2D.FirstArraySlice;
    view->slice_count = args.Tex2D.ArraySize;
    SIZE_T size = functions.pfnCalcPrivateDepthStencilViewSize(driver_device, &args);
    view->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!view->handle.pDrvPrivate) { view->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTDEPTHSTENCILVIEW runtime_view = {view};
    BeginCall();
    functions.pfnCreateDepthStencilView(driver_device, &args, view->handle, runtime_view);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { view->Release(); return hr; }
    view->created = true;
    *out = view;
    return S_OK;
}

void STDMETHODCALLTYPE NativeContext::ClearDepthStencilView(ID3D11DepthStencilView *input, UINT flags, FLOAT depth, UINT8 stencil)
{
    if (deferred)
    {
        NativeLock guard(device);
        NativeCommandRef<ID3D11DepthStencilView> retained(input);
        Record([=](NativeContext *context) { context->ClearDepthStencilView(retained.Get(), flags, depth, stencil); });
        return;
    }

    NativeDepthView *view = static_cast<NativeDepthView *>(input);
    if (!view || view->device != device || !device->functions.pfnClearDepthStencilView
            || (flags & ~(D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL))) return;
    NativeLock guard(device);
    device->functions.pfnClearDepthStencilView(device->driver_device, view->handle, flags, min(1.0f, max(0.0f, depth)), stencil);
}

template<class Interface>
static void NativeReturnObject(Interface *object, Interface **out)
{
    if (!out) return;
    *out = object;
    if (object) object->AddRef();
}

template<class Interface, UINT capacity>
static void NativeGetSlots(NativeDevice *device, Interface *const (&slots)[capacity], UINT start, UINT count, Interface **out)
{
    if (!out || start > capacity || count > capacity - start) return;
    NativeLock guard(device);
    for (UINT i = 0; i < count; ++i) NativeReturnObject(slots[start + i], out + i);
}

#define NATIVE_STAGE_GETTERS(Prefix, Stage, Interface, Shader) \
void STDMETHODCALLTYPE NativeContext::Prefix##GetConstantBuffers(UINT start, UINT count, ID3D11Buffer **out) { NativeGetSlots(device, constant_buffers[Stage], start, count, out); } \
void STDMETHODCALLTYPE NativeContext::Prefix##GetShaderResources(UINT start, UINT count, ID3D11ShaderResourceView **out) { NativeGetSlots(device, shader_resources[Stage], start, count, out); } \
void STDMETHODCALLTYPE NativeContext::Prefix##GetSamplers(UINT start, UINT count, ID3D11SamplerState **out) { NativeGetSlots(device, samplers[Stage], start, count, out); } \
void STDMETHODCALLTYPE NativeContext::Prefix##GetShader(Interface **out, ID3D11ClassInstance **, UINT *count) \
{ NativeLock guard(device); NativeReturnObject(Shader, out); if (count) *count = 0; }
NATIVE_STAGE_GETTERS(VS, 0, ID3D11VertexShader, vertex_shader)
NATIVE_STAGE_GETTERS(PS, 1, ID3D11PixelShader, pixel_shader)
NATIVE_STAGE_GETTERS(GS, 2, ID3D11GeometryShader, geometry_shader)
NATIVE_STAGE_GETTERS(HS, 3, ID3D11HullShader, hull_shader)
NATIVE_STAGE_GETTERS(DS, 4, ID3D11DomainShader, domain_shader)
NATIVE_STAGE_GETTERS(CS, 5, ID3D11ComputeShader, compute_shader)
#undef NATIVE_STAGE_GETTERS

void STDMETHODCALLTYPE NativeContext::IAGetInputLayout(ID3D11InputLayout **out) { NativeLock guard(device); NativeReturnObject(input_layout, out); }
void STDMETHODCALLTYPE NativeContext::IAGetVertexBuffers(UINT start, UINT count, ID3D11Buffer **out, UINT *strides, UINT *offsets)
{
    if (start > 32 || count > 32 - start) return;
    NativeLock guard(device);
    for (UINT i = 0; i < count; ++i)
    {
        if (out) NativeReturnObject(vertex_buffers[start + i], out + i);
        if (strides) strides[i] = vertex_strides[start + i];
        if (offsets) offsets[i] = vertex_offsets[start + i];
    }
}
void STDMETHODCALLTYPE NativeContext::IAGetIndexBuffer(ID3D11Buffer **out, DXGI_FORMAT *format, UINT *offset)
{
    NativeLock guard(device);
    NativeReturnObject(index_buffer, out);
    if (format) *format = index_format;
    if (offset) *offset = index_offset;
}
void STDMETHODCALLTYPE NativeContext::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY *out) { if (out) { NativeLock guard(device); *out = topology; } }
void STDMETHODCALLTYPE NativeContext::OMGetRenderTargets(UINT count, ID3D11RenderTargetView **targets, ID3D11DepthStencilView **depth)
{
    if (count > 8) return;
    NativeLock guard(device);
    if (targets) for (UINT i = 0; i < count; ++i) NativeReturnObject(render_targets[i], targets + i);
    NativeReturnObject(depth_view, depth);
}
void STDMETHODCALLTYPE NativeContext::OMGetBlendState(ID3D11BlendState **out, FLOAT factors[4], UINT *mask)
{
    NativeLock guard(device);
    NativeReturnObject(blend_state, out);
    if (factors) memcpy(factors, blend_factor, sizeof(blend_factor));
    if (mask) *mask = sample_mask;
}
void STDMETHODCALLTYPE NativeContext::OMGetDepthStencilState(ID3D11DepthStencilState **out, UINT *reference)
{
    NativeLock guard(device);
    NativeReturnObject(depth_stencil_state, out);
    if (reference) *reference = stencil_ref;
}
void STDMETHODCALLTYPE NativeContext::RSGetState(ID3D11RasterizerState **out) { NativeLock guard(device); NativeReturnObject(rasterizer_state, out); }
void STDMETHODCALLTYPE NativeContext::RSGetViewports(UINT *count, D3D11_VIEWPORT *out)
{
    if (!count) return;
    NativeLock guard(device);
    if (out)
    {
        UINT copied = min(*count, viewport_count);
        memcpy(out, viewports, copied * sizeof(*out));
        if (*count > copied) ZeroMemory(out + copied, (*count - copied) * sizeof(*out));
    }
    *count = viewport_count;
}
void STDMETHODCALLTYPE NativeContext::RSGetScissorRects(UINT *count, D3D11_RECT *out)
{
    if (!count) return;
    NativeLock guard(device);
    if (out)
    {
        UINT copied = min(*count, scissor_count);
        memcpy(out, scissors, copied * sizeof(*out));
        if (*count > copied) ZeroMemory(out + copied, (*count - copied) * sizeof(*out));
    }
    *count = scissor_count;
}

#include "native_uav.inl"
#include "native_commands.inl"
