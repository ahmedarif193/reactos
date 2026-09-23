/*
 * PROJECT: ReactOS Direct3D 11 runtime
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Native deferred command recording and playback
 */

/* Deferred recording uses the same validated native operations at playback.
 * No UMD work is issued while recording, and command lists own their inputs. */

class NativeCommandList final : public NativeChild<ID3D11CommandList, &IID_ID3D11CommandList>
{
public:
    NativeRecordedCommand *commands = NULL;
    explicit NativeCommandList(NativeDevice *device) : NativeChild(device) {}
    ~NativeCommandList()
    {
        NativeRecordedCommand::DeleteList(commands);
    }
    UINT STDMETHODCALLTYPE GetContextFlags() override { return 0; }
};

class NativeSavedState : public NativeAllocation
{
    LONG references = 1;
public:
    NativeContext state;
    explicit NativeSavedState(NativeContext *source) : state(source->device, true)
    {
        state.recording_disabled = true;
        source->CopyStateTo(&state);
    }
    void AddRef() { InterlockedIncrement(&references); }
    void Release() { if (!InterlockedDecrement(&references)) delete this; }
};

/* An active state has a private reference, separate from its application
 * references. Keeping it bound must neither change its public refcount nor
 * form a device/context/state reference cycle. */
class NativeDeviceContextState final : public ID3DDeviceContextState, public NativeAllocation
{
    LONG references = 0;
    LONG private_references = 1;
    NativePrivateData private_data;
public:
    NativeDevice *device;
    NativeContext saved;
    D3D_FEATURE_LEVEL feature_level;
    UINT flags;

    NativeDeviceContextState(NativeDevice *owner, D3D_FEATURE_LEVEL level, UINT creation_flags)
        : device(owner), saved(owner, true), feature_level(level), flags(creation_flags)
    {
        device->ChildAddRef();
        saved.recording_disabled = true;
    }
    void Retain() { InterlockedIncrement(&private_references); }
    void Drop()
    {
        if (InterlockedDecrement(&private_references)) return;
        NativeDevice *owner = device;
        delete this;
        owner->ChildRelease();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        if (!IsEqualGUID(iid, IID_IUnknown) && !IsEqualGUID(iid, IID_ID3D11DeviceChild)
                && !IsEqualGUID(iid, IID_ID3DDeviceContextState)) return E_NOINTERFACE;
        *out = static_cast<ID3DDeviceContextState *>(this);
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
            NativeDevice *owner = device;
            Drop();
            owner->Release();
        }
        return count;
    }
    void STDMETHODCALLTYPE GetDevice(ID3D11Device **out) override
    {
        if (out) { *out = device; device->AddRef(); }
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *size, void *data) override
    {
        NativeLock guard(device);
        return private_data.Get(guid, size, data);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void *data) override
    {
        NativeLock guard(device);
        return private_data.Set(guid, size, data, NULL);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *object) override
    {
        NativeLock guard(device);
        return private_data.Set(guid, sizeof(object), &object, const_cast<IUnknown *>(object));
    }
};

HRESULT STDMETHODCALLTYPE NativeDevice::CreateDeviceContextState(UINT state_flags,
        const D3D_FEATURE_LEVEL *levels, UINT count, UINT sdk_version, REFIID emulated,
        D3D_FEATURE_LEVEL *chosen, ID3DDeviceContextState **out)
{
    if (chosen) *chosen = static_cast<D3D_FEATURE_LEVEL>(0);
    if (out) *out = NULL;
    if (!levels || !count || state_flags & ~D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
            || ((flags & D3D11_CREATE_DEVICE_SINGLETHREADED)
                && !(state_flags & D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED))
            || sdk_version != D3D11_SDK_VERSION
            || (!IsEqualGUID(emulated, IID_ID3D11Device) && !IsEqualGUID(emulated, IID_ID3D11Device1)))
        return E_INVALIDARG;

    D3D_FEATURE_LEVEL selected = static_cast<D3D_FEATURE_LEVEL>(0);
    for (UINT i = 0; i < count; ++i)
    {
        if (levels[i] != D3D_FEATURE_LEVEL_10_0 && levels[i] != D3D_FEATURE_LEVEL_10_1
                && levels[i] != D3D_FEATURE_LEVEL_11_0) continue;
        if (levels[i] <= feature_level) { selected = levels[i]; break; }
    }
    if (!selected) return E_INVALIDARG;
    if (out)
    {
        NativeDeviceContextState *state = new NativeDeviceContextState(this, selected, state_flags);
        if (!state) return E_OUTOFMEMORY;
        state->AddRef();
        state->Drop();
        *out = state;
    }
    if (chosen) *chosen = selected;
    return out ? S_OK : S_FALSE;
}

void NativeContext::ReleaseContextState()
{
    NativeDeviceContextState *state = active_state;
    active_state = NULL;
    if (state) state->Drop();
}

void STDMETHODCALLTYPE NativeContext::SwapDeviceContextState(ID3DDeviceContextState *input,
        ID3DDeviceContextState **previous)
{
    if (previous) *previous = NULL;
    if (deferred || !input) return;
    NativeDeviceContextState *next = static_cast<NativeDeviceContextState *>(input);
    if (next->device != device) return;
    NativeLock guard(device);
    if (!active_state)
    {
        UINT state_flags = device->flags & D3D11_CREATE_DEVICE_SINGLETHREADED
                ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
        active_state = new NativeDeviceContextState(device, device->feature_level, state_flags);
        if (!active_state) { device->operation_error = E_OUTOFMEMORY; return; }
    }
    NativeDeviceContextState *old = active_state;
    if (previous) { old->AddRef(); *previous = old; }
    if (next == old) return;

    /* Transfer bindings to the inactive state. The active state's live
     * bindings belong to this context, so there is no stale snapshot and no
     * second set of retained resources while it is active. Queries and video
     * state are deliberately outside the pipeline-state copy. */
    next->Retain();
    CopyStateTo(&old->saved);
    next->saved.CopyStateTo(this);
    next->saved.ClearLocalState();
    active_state = next;
    old->Drop();
}

NativeContext::~NativeContext()
{
    if (deferred) ClearLocalState();
    ReleaseContextState();
    NativeRecordedCommand::DeleteList(commands);
    ClearMappings();
}

void NativeContext::ClearMappings()
{
    while (mappings)
    {
        NativeDeferredMapping *next = mappings->next;
        delete mappings;
        mappings = next;
    }
}

void NativeContext::ClearLocalState()
{
    bool previous = recording_disabled;
    recording_disabled = true;
    ClearState();
    recording_disabled = previous;
}

void NativeContext::CopyStateTo(NativeContext *destination)
{
    destination->ClearState();
    destination->VSSetShader(vertex_shader, NULL, 0);
    destination->PSSetShader(pixel_shader, NULL, 0);
    destination->GSSetShader(geometry_shader, NULL, 0);
    destination->HSSetShader(hull_shader, NULL, 0);
    destination->DSSetShader(domain_shader, NULL, 0);
    destination->CSSetShader(compute_shader, NULL, 0);
    destination->OMSetRenderTargetsAndUnorderedAccessViews(render_target_count, render_targets, depth_view,
            render_target_count, 8 - render_target_count, pixel_uavs + render_target_count, NULL);
    destination->CSSetUnorderedAccessViews(0, device->feature_level >= D3D_FEATURE_LEVEL_11_0 ? 8 : 1, compute_uavs, NULL);
    const UINT append[4] = {~0u, ~0u, ~0u, ~0u};
    destination->SOSetTargets(4, stream_outputs, append);
    for (UINT stage = 0; stage < 6; ++stage)
    {
        destination->SetConstantBuffers(stage, 0, 14, constant_buffers[stage]);
        destination->SetSamplers(stage, 0, 16, samplers[stage]);
        destination->SetShaderResources(stage, 0, 128, shader_resources[stage]);
    }
    destination->OMSetBlendState(blend_state, blend_factor, sample_mask);
    destination->OMSetDepthStencilState(depth_stencil_state, stencil_ref);
    destination->RSSetState(rasterizer_state);
    destination->RSSetViewports(viewport_count, viewports);
    destination->RSSetScissorRects(scissor_count, scissors);
    destination->IASetInputLayout(input_layout);
    destination->IASetVertexBuffers(0, 32, vertex_buffers, vertex_strides, vertex_offsets);
    destination->IASetIndexBuffer(index_buffer, index_format, index_offset);
    destination->IASetPrimitiveTopology(topology);
    destination->SetPredication(predicate, predicate_value);
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateDeferredContext(UINT context_flags, ID3D11DeviceContext **out)
{
    if (out) *out = NULL;
    if (context_flags) return E_INVALIDARG;
    if (flags & D3D11_CREATE_DEVICE_SINGLETHREADED) return DXGI_ERROR_INVALID_CALL;
    if (!out) return S_FALSE;
    NativeContext *created = new NativeContext(this, true);
    if (!created) return E_OUTOFMEMORY;
    created->AddRef();
    *out = created;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NativeContext::FinishCommandList(BOOL restore, ID3D11CommandList **out)
{
    if (out) *out = NULL;
    if (!deferred) return DXGI_ERROR_INVALID_CALL;
    NativeLock guard(device);
    for (NativeDeferredMapping *mapping = mappings; mapping; mapping = mapping->next)
        if (mapping->mapped) return DXGI_ERROR_INVALID_CALL;
    if (FAILED(recording_error)) return recording_error;
    NativeCommandList *list = new NativeCommandList(device);
    if (!list) return E_OUTOFMEMORY;
    list->commands = commands;
    commands = NULL;
    command_tail = &commands;
    ClearMappings();
    if (restore)
    {
        NativeSavedState *saved = new NativeSavedState(this);
        if (saved)
        {
            NativeCommandRef<NativeSavedState> retained(saved);
            Record([retained](NativeContext *context) { retained.Get()->state.CopyStateTo(context); });
            saved->Release();
        }
        else recording_error = E_OUTOFMEMORY;
    }
    else ClearLocalState();
    if (out) *out = list;
    else list->Release();
    return S_OK;
}

void STDMETHODCALLTYPE NativeContext::ExecuteCommandList(ID3D11CommandList *input, BOOL restore)
{
    if (!input) return;
    ID3D11Device *owner = NULL;
    input->GetDevice(&owner);
    bool same = owner == static_cast<ID3D11Device *>(device);
    if (owner) owner->Release();
    if (!same) return;
    NativeLock guard(device);
    if (deferred)
    {
        NativeCommandRef<ID3D11CommandList> retained(input);
        Record([retained, restore](NativeContext *context) { context->ExecuteCommandList(retained.Get(), restore); });
        if (!restore) ClearLocalState();
        return;
    }
    NativeSavedState *saved = restore ? new NativeSavedState(this) : NULL;
    if (restore && !saved) { device->operation_error = E_OUTOFMEMORY; return; }
    ClearState();
    NativeCommandList *list = static_cast<NativeCommandList *>(input);
    for (NativeRecordedCommand *command = list->commands; command; command = command->next)
        command->Execute(this);
    if (saved)
    {
        saved->state.CopyStateTo(this);
        saved->Release();
    }
    else ClearState();
}

/* Physical row geometry, including compressed and packed formats. This is
 * host upload layout only; the UMD still chooses the GPU resource layout. */
static bool NativeUploadFormat(DXGI_FORMAT format, UINT width, UINT height, UINT *row_bytes, UINT *rows)
{
    UINT bytes, block_width = 1, block_height = 1;
    if (format >= DXGI_FORMAT_R32G32B32A32_TYPELESS && format <= DXGI_FORMAT_R32G32B32A32_SINT) bytes = 16;
    else if (format >= DXGI_FORMAT_R32G32B32_TYPELESS && format <= DXGI_FORMAT_R32G32B32_SINT) bytes = 12;
    else if (format >= DXGI_FORMAT_R16G16B16A16_TYPELESS && format <= DXGI_FORMAT_X32_TYPELESS_G8X24_UINT) bytes = 8;
    else if (format >= DXGI_FORMAT_R10G10B10A2_TYPELESS && format <= DXGI_FORMAT_X24_TYPELESS_G8_UINT) bytes = 4;
    else if (format >= DXGI_FORMAT_R8G8_TYPELESS && format <= DXGI_FORMAT_R16_SINT) bytes = 2;
    else if (format >= DXGI_FORMAT_R8_TYPELESS && format <= DXGI_FORMAT_A8_UNORM) bytes = 1;
    else if (format == DXGI_FORMAT_R1_UNORM) { bytes = 1; block_width = 8; }
    else if (format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP) bytes = 4;
    else if (format == DXGI_FORMAT_R8G8_B8G8_UNORM || format == DXGI_FORMAT_G8R8_G8B8_UNORM
            || format == DXGI_FORMAT_YUY2) { bytes = 4; block_width = 2; }
    else if (format >= DXGI_FORMAT_BC1_TYPELESS && format <= DXGI_FORMAT_BC5_SNORM)
    {
        bytes = (format <= DXGI_FORMAT_BC1_UNORM_SRGB || (format >= DXGI_FORMAT_BC4_TYPELESS
                && format <= DXGI_FORMAT_BC4_SNORM)) ? 8 : 16;
        block_width = block_height = 4;
    }
    else if (format == DXGI_FORMAT_B5G6R5_UNORM || format == DXGI_FORMAT_B5G5R5A1_UNORM
            || format == DXGI_FORMAT_B4G4R4A4_UNORM) bytes = 2;
    else if (format >= DXGI_FORMAT_B8G8R8A8_UNORM && format <= DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) bytes = 4;
    else if (format >= DXGI_FORMAT_BC6H_TYPELESS && format <= DXGI_FORMAT_BC7_UNORM_SRGB)
    { bytes = 16; block_width = block_height = 4; }
    else if (format == DXGI_FORMAT_AYUV || format == DXGI_FORMAT_Y410) bytes = 4;
    else if (format == DXGI_FORMAT_Y416) bytes = 8;
    else if (format == DXGI_FORMAT_Y210 || format == DXGI_FORMAT_Y216) { bytes = 8; block_width = 2; }
    else return false;
    ULONGLONG row = ((static_cast<ULONGLONG>(width) + block_width - 1) / block_width) * bytes;
    if (!width || !height || row > ~0u) return false;
    *row_bytes = static_cast<UINT>(row);
    *rows = (height + block_height - 1) / block_height;
    return true;
}

static bool NativeUploadLayout(NativeDevice *device, ID3D11Resource *resource, UINT subresource,
        const D3D11_BOX *box, UINT *row_bytes, UINT *rows, UINT *depth, D3D11_USAGE *usage, UINT *cpu_access)
{
    NativeBuffer *buffer = GetNativeBuffer(resource, device);
    UINT width, height;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    if (buffer)
    {
        if (subresource) return false;
        width = buffer->desc.ByteWidth;
        height = *depth = 1;
        *usage = buffer->desc.Usage;
        *cpu_access = buffer->desc.CPUAccessFlags;
    }
    else
    {
        NativeTextureInfo info;
        if (!GetNativeTexture(resource, device, &info) || info.desc.SampleDesc.Count != 1
                || subresource >= info.desc.MipLevels * info.desc.ArraySize) return false;
        UINT mip = subresource % info.desc.MipLevels;
        width = max(1u, info.desc.Width >> mip);
        height = max(1u, info.desc.Height >> mip);
        *depth = max(1u, info.depth >> mip);
        format = info.desc.Format;
        *usage = info.desc.Usage;
        *cpu_access = info.desc.CPUAccessFlags;
    }
    if (box)
    {
        if (box->left >= box->right || box->top >= box->bottom || box->front >= box->back
                || box->right > width || box->bottom > height || box->back > *depth) return false;
        width = box->right - box->left;
        height = box->bottom - box->top;
        *depth = box->back - box->front;
    }
    if (buffer) { *row_bytes = width; *rows = 1; }
    else if (!NativeUploadFormat(format, width, height, row_bytes, rows)) return false;
    ULONGLONG slice = static_cast<ULONGLONG>(*row_bytes) * *rows;
    return slice <= ~0u && *depth <= ~(SIZE_T)0 / slice;
}

HRESULT NativeContext::MapDeferred(ID3D11Resource *resource, UINT subresource,
        D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped)
{
    if (!mapped) return E_INVALIDARG;
    mapped->pData = NULL;
    if (type != D3D11_MAP_WRITE_DISCARD && type != D3D11_MAP_WRITE_NO_OVERWRITE) return E_INVALIDARG;
    if (flags & ~D3D11_MAP_FLAG_DO_NOT_WAIT) return E_INVALIDARG;
    UINT row_bytes, rows, depth, cpu_access;
    D3D11_USAGE usage;
    if (!NativeUploadLayout(device, resource, subresource, NULL, &row_bytes, &rows, &depth, &usage, &cpu_access)
            || usage != D3D11_USAGE_DYNAMIC || !(cpu_access & D3D11_CPU_ACCESS_WRITE)) return E_INVALIDARG;
    if (type == D3D11_MAP_WRITE_NO_OVERWRITE)
    {
        NativeBuffer *buffer = GetNativeBuffer(resource, device);
        if (!buffer || !(buffer->desc.BindFlags & (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER))) return E_INVALIDARG;
    }
    NativeLock guard(device);
    NativeDeferredMapping *mapping = mappings;
    while (mapping && (mapping->resource.Get() != resource || mapping->subresource != subresource)) mapping = mapping->next;
    if (mapping && mapping->mapped) return E_INVALIDARG;
    if (!mapping && type == D3D11_MAP_WRITE_NO_OVERWRITE) return D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD;
    NativeCommandArray<BYTE> bytes(static_cast<SIZE_T>(row_bytes) * rows * depth,
            type == D3D11_MAP_WRITE_NO_OVERWRITE ? mapping->bytes.Data() : NULL);
    if (!bytes.Valid()) return E_OUTOFMEMORY;
    if (!mapping)
    {
        mapping = new NativeDeferredMapping(resource, subresource);
        if (!mapping) return E_OUTOFMEMORY;
        mapping->next = mappings;
        mappings = mapping;
    }
    mapping->row_bytes = row_bytes;
    mapping->rows = rows;
    mapping->depth = depth;
    mapping->bytes = bytes;
    mapping->mapped = true;
    mapped->pData = bytes.Data();
    mapped->RowPitch = row_bytes;
    mapped->DepthPitch = row_bytes * rows;
    return S_OK;
}

void NativeContext::UnmapDeferred(ID3D11Resource *resource, UINT subresource)
{
    NativeLock guard(device);
    NativeDeferredMapping *mapping = mappings;
    while (mapping && (mapping->resource.Get() != resource || mapping->subresource != subresource)) mapping = mapping->next;
    if (!mapping || !mapping->mapped) return;
    NativeCommandRef<ID3D11Resource> retained(resource);
    NativeCommandArray<BYTE> bytes(mapping->bytes);
    UINT row_bytes = mapping->row_bytes, rows = mapping->rows, depth = mapping->depth;
    Record([=](NativeContext *context)
    {
        D3D11_MAPPED_SUBRESOURCE target = {};
        HRESULT hr = context->Map(retained.Get(), subresource, D3D11_MAP_WRITE_DISCARD, 0, &target);
        if (FAILED(hr)) { context->device->operation_error = hr; return; }
        for (UINT z = 0; z < depth; ++z)
            for (UINT y = 0; y < rows; ++y)
                memcpy(static_cast<BYTE *>(target.pData) + static_cast<SIZE_T>(z) * target.DepthPitch
                        + static_cast<SIZE_T>(y) * target.RowPitch,
                        bytes.Data() + (static_cast<SIZE_T>(z) * rows + y) * row_bytes, row_bytes);
        context->Unmap(retained.Get(), subresource);
    });
    mapping->mapped = false;
}

void NativeContext::UpdateDeferred(ID3D11Resource *resource, UINT subresource,
        const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch)
{
    UINT row_bytes, rows, depth, cpu_access;
    D3D11_USAGE usage;
    if (!data || !NativeUploadLayout(device, resource, subresource, box, &row_bytes, &rows, &depth, &usage, &cpu_access)
            || usage != D3D11_USAGE_DEFAULT || (rows > 1 && row_pitch < row_bytes)
            || (depth > 1 && static_cast<ULONGLONG>(depth_pitch) < static_cast<ULONGLONG>(row_pitch) * rows)) return;
    NativeLock guard(device);
    NativeCommandArray<BYTE> bytes(static_cast<SIZE_T>(row_bytes) * rows * depth, NULL);
    NativeCommandArray<D3D11_BOX> region(box ? 1 : 0, box);
    if (!Captured(bytes) || !Captured(region)) return;
    for (UINT z = 0; z < depth; ++z)
        for (UINT y = 0; y < rows; ++y)
            memcpy(bytes.Data() + (static_cast<SIZE_T>(z) * rows + y) * row_bytes,
                    static_cast<const BYTE *>(data) + static_cast<SIZE_T>(z) * depth_pitch
                    + static_cast<SIZE_T>(y) * row_pitch, row_bytes);
    NativeCommandRef<ID3D11Resource> retained(resource);
    Record([=](NativeContext *context)
    {
        context->UpdateSubresource(retained.Get(), subresource, region.Data(), bytes.Data(), row_bytes, row_bytes * rows);
    });
}
