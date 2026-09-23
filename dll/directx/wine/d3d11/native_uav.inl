/*
 * PROJECT: ReactOS Direct3D 11 runtime
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Native unordered access, output bindings, and compute commands
 */

/* Updating CPU state before a driver callback lets reentrant state refreshes
 * see the new bindings. Keep old DDI objects alive until that bind completes. */
template<class Interface, UINT Count> class NativeBindingSnapshot
{
    Interface *objects[Count];
public:
    explicit NativeBindingSnapshot(Interface *const *slots)
    {
        for (UINT i = 0; i < Count; ++i)
        {
            objects[i] = slots[i];
            if (objects[i]) NativeBindingObject(objects[i])->Retain();
        }
    }
    ~NativeBindingSnapshot()
    {
        for (UINT i = 0; i < Count; ++i) if (objects[i]) NativeBindingObject(objects[i])->Drop();
    }
};

HRESULT STDMETHODCALLTYPE NativeDevice::CreateUnorderedAccessView(ID3D11Resource *resource,
        const D3D11_UNORDERED_ACCESS_VIEW_DESC *input, ID3D11UnorderedAccessView **out)
{
    if (out) *out = NULL;
    if (!resource) return E_INVALIDARG;
    if (!functions.pfnCalcPrivateUnorderedAccessViewSize || !functions.pfnCreateUnorderedAccessView
            || !functions.pfnDestroyUnorderedAccessView) return E_NOTIMPL;
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
    D3D11DDIARG_CREATEUNORDEREDACCESSVIEW args = {};
    if (input) desc = *input;
    if (NativeBuffer *buffer = GetNativeBuffer(resource, this))
    {
        if (!(buffer->desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)) return E_INVALIDARG;
        bool structured = !!(buffer->desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED);
        if (!input)
        {
            if (!structured) return E_INVALIDARG;
            desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            desc.Buffer.NumElements = buffer->desc.ByteWidth / buffer->desc.StructureByteStride;
        }
        if (desc.ViewDimension != D3D11_UAV_DIMENSION_BUFFER) return E_INVALIDARG;
        UINT flags = desc.Buffer.Flags, stride;
        if (flags & ~(D3D11_BUFFER_UAV_FLAG_RAW | D3D11_BUFFER_UAV_FLAG_APPEND | D3D11_BUFFER_UAV_FLAG_COUNTER))
            return E_INVALIDARG;
        if ((flags & D3D11_BUFFER_UAV_FLAG_APPEND) && (flags & D3D11_BUFFER_UAV_FLAG_COUNTER)) return E_INVALIDARG;
        if (structured)
        {
            if (desc.Format != DXGI_FORMAT_UNKNOWN || (flags & D3D11_BUFFER_UAV_FLAG_RAW)) return E_INVALIDARG;
            stride = buffer->desc.StructureByteStride;
        }
        else if (flags & D3D11_BUFFER_UAV_FLAG_RAW)
        {
            if (flags != D3D11_BUFFER_UAV_FLAG_RAW || desc.Format != DXGI_FORMAT_R32_TYPELESS
                    || !(buffer->desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)) return E_INVALIDARG;
            stride = sizeof(UINT);
        }
        else
        {
            if (flags || FAILED(NativeBufferViewStride(this, desc.Format, false, &stride))) return E_INVALIDARG;
            UINT support = 0;
            if (FAILED(CheckFormatSupport(desc.Format, &support))
                    || !(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)) return E_INVALIDARG;
        }
        if (!NativeBufferViewRange(desc.Buffer.FirstElement, desc.Buffer.NumElements, stride, buffer->desc.ByteWidth))
            return E_INVALIDARG;
        args.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
        args.hDrvResource = buffer->handle;
        args.Buffer.FirstElement = desc.Buffer.FirstElement;
        args.Buffer.NumElements = desc.Buffer.NumElements;
        args.Buffer.Flags = flags;
    }
    else
    {
        NativeTextureInfo info;
        if (!GetNativeTexture(resource, this, &info) || !(info.desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
                || info.desc.SampleDesc.Count != 1 || feature_level < D3D_FEATURE_LEVEL_11_0) return E_INVALIDARG;
        bool one = info.dimension == D3D10DDIRESOURCE_TEXTURE1D;
        bool volume = info.dimension == D3D10DDIRESOURCE_TEXTURE3D;
        if (!input)
        {
            desc.Format = info.desc.Format;
            desc.ViewDimension = volume ? D3D11_UAV_DIMENSION_TEXTURE3D : one
                    ? (info.desc.ArraySize > 1 ? D3D11_UAV_DIMENSION_TEXTURE1DARRAY : D3D11_UAV_DIMENSION_TEXTURE1D)
                    : (info.desc.ArraySize > 1 ? D3D11_UAV_DIMENSION_TEXTURE2DARRAY : D3D11_UAV_DIMENSION_TEXTURE2D);
            if (volume) desc.Texture3D.WSize = info.depth;
            else desc.Texture2DArray.ArraySize = info.desc.ArraySize;
        }
        if (desc.Format == DXGI_FORMAT_UNKNOWN) desc.Format = info.desc.Format;
        UINT support = 0;
        if (!NativeViewFormatCompatible(info, desc.Format) || FAILED(CheckFormatSupport(desc.Format, &support))
                || !(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)) return E_INVALIDARG;
        UINT mip = desc.Texture2D.MipSlice;
        if (mip >= info.desc.MipLevels) return E_INVALIDARG;
        args.hDrvResource = info.handle;
        if (desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE3D)
        {
            if (!volume || !NativeViewSliceRange(desc.Texture3D.FirstWSlice, desc.Texture3D.WSize,
                    max(1u, info.depth >> mip))) return E_INVALIDARG;
            args.ResourceDimension = D3D10DDIRESOURCE_TEXTURE3D;
            args.Tex3D.MipSlice = mip;
            args.Tex3D.FirstW = desc.Texture3D.FirstWSlice;
            args.Tex3D.WSize = desc.Texture3D.WSize;
        }
        else
        {
            if (volume || (one ? (desc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE1D
                    && desc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE1DARRAY)
                    : (desc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D
                    && desc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2DARRAY))) return E_INVALIDARG;
            bool array = desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE1DARRAY
                    || desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            UINT first = array ? desc.Texture2DArray.FirstArraySlice : 0;
            UINT count = array ? desc.Texture2DArray.ArraySize : 1;
            if ((!array && info.desc.ArraySize != 1) || !NativeViewSliceRange(first, count, info.desc.ArraySize))
                return E_INVALIDARG;
            if (array) desc.Texture2DArray.ArraySize = count;
            args.ResourceDimension = one ? D3D10DDIRESOURCE_TEXTURE1D : D3D10DDIRESOURCE_TEXTURE2D;
            args.Tex2D.MipSlice = mip;
            args.Tex2D.FirstArraySlice = first;
            args.Tex2D.ArraySize = count;
        }
    }
    args.Format = desc.Format;
    if (!out) return S_FALSE;
    NativeLock guard(this);
    auto *view = new NativeUnorderedAccessView(this);
    if (!view) return E_OUTOFMEMORY;
    view->texture = resource;
    NativeRetainResource(resource);
    view->resource_handle = args.hDrvResource;
    view->desc = desc;
    BeginCall();
    SIZE_T size = functions.pfnCalcPrivateUnorderedAccessViewSize(driver_device, &args);
    HRESULT hr = operation_error;
    if (FAILED(hr)) { view->Release(); return hr; }
    view->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!view->handle.pDrvPrivate) { view->Release(); return E_OUTOFMEMORY; }
    D3D11DDI_HRTUNORDEREDACCESSVIEW runtime_view = {view};
    BeginCall();
    functions.pfnCreateUnorderedAccessView(driver_device, &args, view->handle, runtime_view);
    hr = operation_error;
    if (FAILED(hr)) { view->Release(); return hr; }
    view->created = true;
    *out = view;
    return S_OK;
}

/* Always pass the complete OM state to the DDI. Counter resets are commands,
 * not pipeline state: refreshes and saved-state restoration must retain them. */
void NativeContext::RefreshOutputBindings(const UINT *initial)
{
    if (deferred || !device->functions.pfnSetRenderTargets) return;
    NativeLock guard(device);
    D3D10DDI_HRENDERTARGETVIEW targets[8] = {};
    D3D11DDI_HUNORDEREDACCESSVIEW uavs[8] = {};
    D3D10DDI_HDEPTHSTENCILVIEW depth = {};
    UINT counts[8];
    for (UINT i = 0; i < 8; ++i)
    {
        if (render_targets[i]) targets[i] = static_cast<NativeRenderTargetView *>(render_targets[i])->handle;
        if (pixel_uavs[i]) uavs[i] = static_cast<NativeUnorderedAccessView *>(pixel_uavs[i])->handle;
        counts[i] = initial ? initial[i] : ~0u;
    }
    if (depth_view) depth = static_cast<NativeDepthView *>(depth_view)->handle;
    device->functions.pfnSetRenderTargets(device->driver_device, targets, render_target_count, 8 - render_target_count,
            depth, uavs + render_target_count, counts + render_target_count, render_target_count,
            8 - render_target_count, 0, 8);
}

template<class View> static void NativeUnbindInputs(NativeContext *context, View *view)
{
    NativeViewRange range = NativeRange(view);
    if (!range.resource) return;
    NativeBindingSnapshot<ID3D11Buffer, 4> old_stream(context->stream_outputs);
    bool stream_changed = false;
    for (UINT i = 0; i < 4; ++i)
        if (context->stream_outputs[i] == range.resource)
        {
            ReplaceObject(context->stream_outputs[i], static_cast<ID3D11Buffer *>(NULL));
            stream_changed = true;
        }
    if (stream_changed) context->RefreshStreamOutput();
    for (UINT stage = 0; stage < 6; ++stage)
        for (UINT slot = 0; slot < 128; ++slot)
            if (NativeViewsOverlap(view, context->shader_resources[stage][slot]))
            {
                ID3D11ShaderResourceView *null_view = NULL;
                context->SetShaderResources(stage, slot, 1, &null_view);
            }
}

void STDMETHODCALLTYPE NativeContext::CSSetUnorderedAccessViews(UINT start, UINT count,
        ID3D11UnorderedAccessView *const *input, const UINT *initial)
{
    UINT capacity = device->feature_level >= D3D_FEATURE_LEVEL_11_0 ? 8 : 1;
    if (start > capacity || count > capacity - start || !device->functions.pfnCsSetUnorderedAccessViews) return;
    NativeLock guard(device);
    NativeBindingSnapshot<ID3D11RenderTargetView, 8> old_targets(render_targets);
    NativeBindingSnapshot<ID3D11UnorderedAccessView, 8> old_pixel_uavs(pixel_uavs);
    NativeBindingSnapshot<ID3D11DepthStencilView, 1> old_depth(&depth_view);
    NativeBindingSnapshot<ID3D11UnorderedAccessView, 8> old_compute_uavs(compute_uavs);
    ID3D11UnorderedAccessView *values[8] = {};
    UINT counts[8];
    for (UINT i = 0; i < count; ++i)
    {
        auto *view = input ? static_cast<NativeUnorderedAccessView *>(input[i]) : NULL;
        if (view && view->device != device) return;
        values[i] = view;
        counts[i] = initial ? initial[i] : ~0u;
        for (UINT j = 0; j < i; ++j) if (NativeViewsOverlap(values[i], values[j])) return;
    }
    NativeCommandArray<ID3D11UnorderedAccessView *> retained(count, values);
    if (!Captured(retained)) return;
    if (deferred && !recording_disabled)
    {
        NativeCommandArray<UINT> copied(count, counts);
        if (!Captured(copied)) return;
        Record([=](NativeContext *context) { context->CSSetUnorderedAccessViews(start, count, retained.Data(), copied.Data()); });
    }
    bool changed_output = false;
    for (UINT i = 0; i < count; ++i)
    {
        if (!values[i]) continue;
        NativeUnbindInputs(this, values[i]);
        for (UINT j = 0; j < 8; ++j)
        {
            if ((j < start || j >= start + count) && NativeViewsOverlap(values[i], compute_uavs[j]))
            {
                D3D11DDI_HUNORDEREDACCESSVIEW empty = {};
                UINT keep = ~0u;
                if (!deferred) device->functions.pfnCsSetUnorderedAccessViews(device->driver_device, j, 1, &empty, &keep);
                ReplaceObject(compute_uavs[j], static_cast<ID3D11UnorderedAccessView *>(NULL));
            }
            if (NativeViewsOverlap(values[i], render_targets[j]))
            { ReplaceObject(render_targets[j], static_cast<ID3D11RenderTargetView *>(NULL)); changed_output = true; }
            if (NativeViewsOverlap(values[i], pixel_uavs[j]))
            { ReplaceObject(pixel_uavs[j], static_cast<ID3D11UnorderedAccessView *>(NULL)); changed_output = true; }
        }
        if (NativeViewsOverlap(values[i], depth_view))
        { ReplaceObject(depth_view, static_cast<ID3D11DepthStencilView *>(NULL)); changed_output = true; }
    }
    if (changed_output) RefreshOutputBindings();
    D3D11DDI_HUNORDEREDACCESSVIEW handles[8] = {};
    for (UINT i = 0; i < count; ++i)
    {
        auto *view = static_cast<NativeUnorderedAccessView *>(values[i]);
        if (view) handles[i] = view->handle;
        ReplaceObject(compute_uavs[start + i], values[i]);
    }
    if (!deferred) device->functions.pfnCsSetUnorderedAccessViews(device->driver_device, start, count, handles, counts);
}

void STDMETHODCALLTYPE NativeContext::CSGetUnorderedAccessViews(UINT start, UINT count, ID3D11UnorderedAccessView **out)
{
    NativeGetSlots(device, compute_uavs, start, count, out);
}

void STDMETHODCALLTYPE NativeContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT target_count,
        ID3D11RenderTargetView *const *targets, ID3D11DepthStencilView *depth, UINT start, UINT count,
        ID3D11UnorderedAccessView *const *input, const UINT *initial)
{
    bool keep_targets = target_count == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL;
    bool keep_uavs = count == D3D11_KEEP_UNORDERED_ACCESS_VIEWS;
    if (keep_targets && keep_uavs) return;
    if (!keep_targets && (target_count > 8 || (target_count && !targets))) return;
    if (!keep_uavs && (start > 8 || count > 8 - start || (!keep_targets && count && start < target_count))) return;
    if (!device->functions.pfnSetRenderTargets) return;
    NativeLock guard(device);
    NativeBindingSnapshot<ID3D11RenderTargetView, 8> old_targets(render_targets);
    NativeBindingSnapshot<ID3D11UnorderedAccessView, 8> old_pixel_uavs(pixel_uavs);
    NativeBindingSnapshot<ID3D11DepthStencilView, 1> old_depth(&depth_view);
    ID3D11RenderTargetView *next_targets[8] = {};
    ID3D11UnorderedAccessView *next_uavs[8] = {};
    UINT counts[8];
    if (keep_targets) { depth = depth_view; target_count = min(render_target_count, start); }
    auto *depth_target = static_cast<NativeDepthView *>(depth);
    if (depth_target && depth_target->device != device) return;
    for (UINT i = 0; i < 8; ++i)
    {
        if (i < target_count) next_targets[i] = keep_targets ? render_targets[i] : targets[i];
        auto *target = static_cast<NativeRenderTargetView *>(next_targets[i]);
        if (target && target->device != device) return;
        next_uavs[i] = keep_uavs ? pixel_uavs[i] : i >= start && i - start < count && input ? input[i - start] : NULL;
        auto *view = static_cast<NativeUnorderedAccessView *>(next_uavs[i]);
        if (view && view->device != device) return;
        counts[i] = !keep_uavs && initial && i >= start && i - start < count ? initial[i - start] : ~0u;
    }
    for (UINT i = 0; i < 8; ++i)
    {
        if (keep_uavs && i < target_count) next_uavs[i] = NULL;
        for (UINT j = 0; j < 8; ++j)
        {
            if (NativeViewsOverlap(next_targets[i], next_uavs[j]))
            {
                if (keep_targets) next_targets[i] = NULL;
                else if (keep_uavs) next_uavs[j] = NULL;
                else return;
            }
            if (i != j && (NativeViewsOverlap(next_targets[i], next_targets[j])
                    || NativeViewsOverlap(next_uavs[i], next_uavs[j]))) return;
        }
        if (NativeViewsOverlap(next_uavs[i], depth))
        {
            if (keep_uavs) next_uavs[i] = NULL;
            else return;
        }
    }
    /* Retain all new objects before unbinding conflicting old references. */
    NativeCommandArray<ID3D11RenderTargetView *> retained_targets(8, next_targets);
    NativeCommandArray<ID3D11UnorderedAccessView *> retained_uavs(8, next_uavs);
    NativeCommandRef<ID3D11DepthStencilView> retained_depth(depth);
    if (!Captured(retained_targets) || !Captured(retained_uavs)) return;
    if (deferred && !recording_disabled)
    {
        NativeCommandArray<UINT> copied(8, counts);
        if (!Captured(copied)) return;
        Record([=](NativeContext *context) {
            context->OMSetRenderTargetsAndUnorderedAccessViews(target_count, retained_targets.Data(), retained_depth.Get(),
                    target_count, 8 - target_count, retained_uavs.Data() + target_count, copied.Data() + target_count);
        });
    }
    for (UINT i = 0; i < 8; ++i)
    {
        NativeUnbindInputs(this, next_targets[i]);
        NativeUnbindInputs(this, next_uavs[i]);
        for (UINT j = 0; j < 8; ++j)
            if (NativeViewsOverlap(compute_uavs[j], next_targets[i]) || NativeViewsOverlap(compute_uavs[j], next_uavs[i])
                    || NativeViewsOverlap(compute_uavs[j], depth))
            {
                ID3D11UnorderedAccessView *empty = NULL;
                CSSetUnorderedAccessViews(j, 1, &empty, NULL);
            }
    }
    if (depth_target)
        for (UINT stage = 0; stage < 6; ++stage)
            for (UINT slot = 0; slot < 128; ++slot)
                if (depth_target->ConflictsWith(static_cast<NativeShaderResourceView *>(shader_resources[stage][slot])))
                {
                    ID3D11ShaderResourceView *empty = NULL;
                    SetShaderResources(stage, slot, 1, &empty);
                }
    for (UINT i = 0; i < 8; ++i)
    {
        ReplaceObject(render_targets[i], next_targets[i]);
        ReplaceObject(pixel_uavs[i], next_uavs[i]);
        if (!deferred && next_targets[i] && device->functions.pfnResourceReadAfterWriteHazard)
            device->functions.pfnResourceReadAfterWriteHazard(device->driver_device,
                    static_cast<NativeRenderTargetView *>(next_targets[i])->resource_handle);
    }
    ReplaceObject(depth_view, depth);
    if (!deferred && depth_target && device->functions.pfnResourceReadAfterWriteHazard)
        device->functions.pfnResourceReadAfterWriteHazard(device->driver_device, depth_target->resource_handle);
    render_target_count = target_count;
    RefreshOutputBindings(counts);
}

void STDMETHODCALLTYPE NativeContext::OMGetRenderTargetsAndUnorderedAccessViews(UINT count,
        ID3D11RenderTargetView **targets, ID3D11DepthStencilView **depth, UINT start, UINT uav_count,
        ID3D11UnorderedAccessView **uavs)
{
    NativeLock guard(device);
    OMGetRenderTargets(count, targets, depth);
    NativeGetSlots(device, pixel_uavs, start, uav_count, uavs);
}

static void NativeUavClearValues(DXGI_FORMAT format, const UINT input[4], UINT output[4])
{
    UINT mask = ~0u;
    switch (NativeFormatFamily(format))
    {
        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            mask = 0xff;
            break;
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            mask = 0xffff;
            break;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            for (UINT i = 0; i < 3; ++i) output[i] = input[i] & 0x3ff;
            output[3] = input[3] & 3;
            return;
        default:
            break;
    }
    if (format == DXGI_FORMAT_R11G11B10_FLOAT)
    {
        output[0] = input[0] & 0x7ff;
        output[1] = input[1] & 0x7ff;
        output[2] = input[2] & 0x3ff;
        output[3] = 0;
        return;
    }
    /* Uint clears copy low channel bits without conversion or sign extension.
     * Keep raw/structured and 32-bit values intact. In particular, passing a
     * sign-extended narrow integer to the UMD can corrupt adjacent channels. */
    for (UINT i = 0; i < 4; ++i) output[i] = input[i] & mask;
}

static void NativeUavClearValues(DXGI_FORMAT, const FLOAT input[4], FLOAT output[4])
{
    memcpy(output, input, 4 * sizeof(*output));
}

#define NATIVE_CLEAR_UAV(Suffix, ValueType) \
void STDMETHODCALLTYPE NativeContext::ClearUnorderedAccessView##Suffix(ID3D11UnorderedAccessView *object, const ValueType values[4]) \
{ \
    auto *view = static_cast<NativeUnorderedAccessView *>(object); \
    if (!view || view->device != device || !values || !device->functions.pfnClearUnorderedAccessView##Suffix) return; \
    NativeLock guard(device); \
    if (deferred) \
    { \
        if (recording_disabled) return; \
        NativeCommandRef<ID3D11UnorderedAccessView> retained(object); \
        NativeCommandArray<ValueType> copied(4, values); \
        if (Captured(copied)) Record([=](NativeContext *context) { context->ClearUnorderedAccessView##Suffix(retained.Get(), copied.Data()); }); \
        return; \
    } \
    ValueType normalized[4]; \
    NativeUavClearValues(view->desc.Format, values, normalized); \
    device->functions.pfnClearUnorderedAccessView##Suffix(device->driver_device, view->handle, normalized); \
}
NATIVE_CLEAR_UAV(Uint, UINT)
NATIVE_CLEAR_UAV(Float, FLOAT)
#undef NATIVE_CLEAR_UAV

void STDMETHODCALLTYPE NativeContext::Dispatch(UINT x, UINT y, UINT z)
{
    if (x > 65535 || y > 65535 || z > 65535 || (device->feature_level < D3D_FEATURE_LEVEL_11_0 && z != 1)
            || !device->functions.pfnDispatch) return;
    NativeLock guard(device);
    if (deferred)
    {
        if (!recording_disabled) Record([=](NativeContext *context) { context->Dispatch(x, y, z); });
        return;
    }
    device->functions.pfnDispatch(device->driver_device, x, y, z);
}

void STDMETHODCALLTYPE NativeContext::DispatchIndirect(ID3D11Buffer *buffer, UINT offset)
{
    auto *args = GetNativeBuffer(buffer, device);
    if (!args || !(args->desc.MiscFlags & D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS) || (offset & 3)
            || offset > args->desc.ByteWidth || args->desc.ByteWidth - offset < 3 * sizeof(UINT)
            || !device->functions.pfnDispatchIndirect) return;
    NativeLock guard(device);
    if (deferred)
    {
        if (recording_disabled) return;
        NativeCommandRef<ID3D11Buffer> retained(buffer);
        Record([=](NativeContext *context) { context->DispatchIndirect(retained.Get(), offset); });
        return;
    }
    device->functions.pfnDispatchIndirect(device->driver_device, args->handle, offset);
}

void STDMETHODCALLTYPE NativeContext::CopyStructureCount(ID3D11Buffer *buffer, UINT offset, ID3D11UnorderedAccessView *object)
{
    auto *destination = GetNativeBuffer(buffer, device);
    auto *view = static_cast<NativeUnorderedAccessView *>(object);
    if (!destination || !view || view->device != device || (offset & 3)
            || offset > destination->desc.ByteWidth || destination->desc.ByteWidth - offset < sizeof(UINT)
            || view->desc.ViewDimension != D3D11_UAV_DIMENSION_BUFFER
            || !(view->desc.Buffer.Flags & (D3D11_BUFFER_UAV_FLAG_APPEND | D3D11_BUFFER_UAV_FLAG_COUNTER))
            || !device->functions.pfnCopyStructureCount) return;
    NativeLock guard(device);
    if (deferred)
    {
        if (recording_disabled) return;
        NativeCommandRef<ID3D11Buffer> retained_buffer(buffer);
        NativeCommandRef<ID3D11UnorderedAccessView> retained_view(object);
        Record([=](NativeContext *context) { context->CopyStructureCount(retained_buffer.Get(), offset, retained_view.Get()); });
        return;
    }
    device->functions.pfnCopyStructureCount(device->driver_device, destination->handle, offset, view->handle);
}

void NativeContext::RefreshStreamOutput()
{
    if (deferred || !device->functions.pfnSoSetTargets) return;
    NativeLock guard(device);
    D3D10DDI_HRESOURCE handles[4] = {};
    UINT offsets[4] = {~0u, ~0u, ~0u, ~0u};
    for (UINT i = 0; i < 4; ++i) if (stream_outputs[i]) handles[i] = GetNativeBuffer(stream_outputs[i], device)->handle;
    device->functions.pfnSoSetTargets(device->driver_device, 4, 0, handles, offsets);
}

void STDMETHODCALLTYPE NativeContext::SOSetTargets(UINT count, ID3D11Buffer *const *buffers, const UINT *offsets)
{
    if (count > 4 || (count && (!buffers || !offsets)) || !device->functions.pfnSoSetTargets) return;
    NativeLock guard(device);
    NativeBindingSnapshot<ID3D11RenderTargetView, 8> old_targets(render_targets);
    NativeBindingSnapshot<ID3D11UnorderedAccessView, 8> old_pixel_uavs(pixel_uavs);
    NativeBindingSnapshot<ID3D11DepthStencilView, 1> old_depth(&depth_view);
    D3D10DDI_HRESOURCE handles[4] = {};
    for (UINT i = 0; i < count; ++i)
    {
        if (!buffers[i]) continue;
        auto *buffer = GetNativeBuffer(buffers[i], device);
        if (!buffer || !(buffer->desc.BindFlags & D3D11_BIND_STREAM_OUTPUT)
                || (offsets[i] != ~0u && ((offsets[i] & 3) || offsets[i] > buffer->desc.ByteWidth))) return;
        for (UINT j = 0; j < i; ++j) if (buffers[j] == buffers[i]) return;
        handles[i] = buffer->handle;
    }
    NativeCommandArray<ID3D11Buffer *> retained(count, buffers);
    if (!Captured(retained)) return;
    if (deferred && !recording_disabled)
    {
        NativeCommandArray<UINT> copied(count, offsets);
        if (!Captured(copied)) return;
        Record([=](NativeContext *context) { context->SOSetTargets(count, retained.Data(), copied.Data()); });
    }
    bool changed_output = false;
    for (UINT i = 0; i < count; ++i)
    {
        if (!buffers[i]) continue;
        for (UINT stage = 0; stage < 6; ++stage)
            for (UINT slot = 0; slot < 128; ++slot)
                if (NativeRange(shader_resources[stage][slot]).resource == buffers[i])
                {
                    ID3D11ShaderResourceView *empty = NULL;
                    SetShaderResources(stage, slot, 1, &empty);
                }
        for (UINT j = 0; j < 8; ++j)
        {
            if (NativeRange(compute_uavs[j]).resource == buffers[i])
            {
                ID3D11UnorderedAccessView *empty = NULL;
                CSSetUnorderedAccessViews(j, 1, &empty, NULL);
            }
            if (NativeRange(pixel_uavs[j]).resource == buffers[i])
            { ReplaceObject(pixel_uavs[j], static_cast<ID3D11UnorderedAccessView *>(NULL)); changed_output = true; }
            if (NativeRange(render_targets[j]).resource == buffers[i])
            { ReplaceObject(render_targets[j], static_cast<ID3D11RenderTargetView *>(NULL)); changed_output = true; }
        }
    }
    if (changed_output) RefreshOutputBindings();
    if (!deferred) device->functions.pfnSoSetTargets(device->driver_device, count, 4 - count, handles, offsets);
    for (UINT i = 0; i < 4; ++i) ReplaceObject(stream_outputs[i], i < count ? buffers[i] : NULL);
}

void STDMETHODCALLTYPE NativeContext::SOGetTargets(UINT count, ID3D11Buffer **out)
{
    NativeGetSlots(device, stream_outputs, 0, count, out);
}

void STDMETHODCALLTYPE NativeContext::DrawAuto()
{
    if (!device->functions.pfnDrawAuto) return;
    NativeLock guard(device);
    if (deferred)
    {
        if (!recording_disabled) Record([](NativeContext *context) { context->DrawAuto(); });
        return;
    }
    device->functions.pfnDrawAuto(device->driver_device);
}
