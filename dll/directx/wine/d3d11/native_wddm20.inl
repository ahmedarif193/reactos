/*
 * PROJECT: ReactOS Direct3D 11 runtime
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: WDDM 2.0 argument adapters, included after the native runtime object types
 */

struct alignas(16) NativeWddm20DeviceHeader
{
    NativeDevice *device;
};

static NativeDevice *NativeWddm20Device(D3D10DDI_HDEVICE handle)
{
    return (static_cast<NativeWddm20DeviceHeader *>(handle.pDrvPrivate) - 1)->device;
}

#define NATIVE_WDDM20_CONSTANT_BUFFERS(Name) \
static void APIENTRY NativeWddm20##Name(D3D10DDI_HDEVICE h, UINT start, UINT count, const D3D10DDI_HRESOURCE *resources) \
{ \
    NativeWddm20Device(h)->wddm20_functions.pfn##Name(h, start, count, resources, NULL, NULL); \
}
NATIVE_WDDM20_CONSTANT_BUFFERS(VsSetConstantBuffers)
NATIVE_WDDM20_CONSTANT_BUFFERS(PsSetConstantBuffers)
NATIVE_WDDM20_CONSTANT_BUFFERS(GsSetConstantBuffers)
NATIVE_WDDM20_CONSTANT_BUFFERS(HsSetConstantBuffers)
NATIVE_WDDM20_CONSTANT_BUFFERS(DsSetConstantBuffers)
NATIVE_WDDM20_CONSTANT_BUFFERS(CsSetConstantBuffers)
#undef NATIVE_WDDM20_CONSTANT_BUFFERS

#define NATIVE_WDDM20_UPDATE(Name) \
static void APIENTRY NativeWddm20##Name(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE resource, UINT subresource, \
        const D3D10_DDI_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch) \
{ \
    NativeWddm20Device(h)->wddm20_functions.pfn##Name(h, resource, subresource, box, data, row_pitch, depth_pitch, 0); \
}
NATIVE_WDDM20_UPDATE(DefaultConstantBufferUpdateSubresourceUP)
NATIVE_WDDM20_UPDATE(ResourceUpdateSubresourceUP)
#undef NATIVE_WDDM20_UPDATE

#define NATIVE_WDDM20_COPY(Name) \
static void APIENTRY NativeWddm20##Name(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE dst, UINT dst_subresource, \
        UINT x, UINT y, UINT z, D3D10DDI_HRESOURCE src, UINT src_subresource, const D3D10_DDI_BOX *box) \
{ \
    NativeWddm20Device(h)->wddm20_functions.pfn##Name(h, dst, dst_subresource, x, y, z, src, src_subresource, box, 0); \
}
NATIVE_WDDM20_COPY(ResourceCopyRegion)
NATIVE_WDDM20_COPY(ResourceConvertRegion)
#undef NATIVE_WDDM20_COPY

static void APIENTRY NativeWddm20Flush(D3D10DDI_HDEVICE h)
{
    /* ALL contexts, unconditional flush; do not turn an API Flush into a hint. */
    NativeWddm20Device(h)->wddm20_functions.pfnFlush(h, 0, 0);
}

static void APIENTRY NativeWddm20CheckMultisampleQualityLevels(D3D10DDI_HDEVICE h,
        DXGI_FORMAT format, UINT samples, UINT *quality)
{
    NativeWddm20Device(h)->wddm20_functions.pfnCheckMultisampleQualityLevels(h, format, samples, 0, quality);
}

static D3D11_1_DDI_BLEND_DESC NativeWddm20Convert(const D3D10_1_DDI_BLEND_DESC &old)
{
    D3D11_1_DDI_BLEND_DESC value = {};
    value.AlphaToCoverageEnable = old.AlphaToCoverageEnable;
    value.IndependentBlendEnable = old.IndependentBlendEnable;
    for (UINT i = 0; i < ARRAYSIZE(value.RenderTarget); ++i)
    {
        value.RenderTarget[i].BlendEnable = old.RenderTarget[i].BlendEnable;
        value.RenderTarget[i].SrcBlend = old.RenderTarget[i].SrcBlend;
        value.RenderTarget[i].DestBlend = old.RenderTarget[i].DestBlend;
        value.RenderTarget[i].BlendOp = old.RenderTarget[i].BlendOp;
        value.RenderTarget[i].SrcBlendAlpha = old.RenderTarget[i].SrcBlendAlpha;
        value.RenderTarget[i].DestBlendAlpha = old.RenderTarget[i].DestBlendAlpha;
        value.RenderTarget[i].BlendOpAlpha = old.RenderTarget[i].BlendOpAlpha;
        value.RenderTarget[i].RenderTargetWriteMask = old.RenderTarget[i].RenderTargetWriteMask;
        value.RenderTarget[i].LogicOp = D3D11_1_DDI_LOGIC_OP_COPY;
    }
    return value;
}

static D3DWDDM2_0DDI_RASTERIZER_DESC NativeWddm20Convert(const D3D10_DDI_RASTERIZER_DESC &old)
{
    D3DWDDM2_0DDI_RASTERIZER_DESC value = {};
    static_assert(offsetof(D3DWDDM2_0DDI_RASTERIZER_DESC, ForcedSampleCount) == sizeof(old), "Rasterizer prefix");
    memcpy(&value, &old, sizeof(old));
    return value;
}

static D3DWDDM2_0DDIARG_CREATEQUERY NativeWddm20Convert(const D3D10DDIARG_CREATEQUERY &old)
{
    D3DWDDM2_0DDIARG_CREATEQUERY value = {old.Query, old.MiscFlags, 1 /* 3D context */};
    return value;
}

static D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW NativeWddm20Convert(const D3D10DDIARG_CREATERENDERTARGETVIEW &old)
{
    D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW value = {};
    value.hDrvResource = old.hDrvResource;
    value.Format = old.Format;
    value.ResourceDimension = old.ResourceDimension;
    switch (old.ResourceDimension)
    {
        case D3D11DDIRESOURCE_BUFFEREX:
        case D3D10DDIRESOURCE_BUFFER: value.Buffer = old.Buffer; break;
        case D3D10DDIRESOURCE_TEXTURE1D: value.Tex1D = old.Tex1D; break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            value.Tex2D.MipSlice = old.Tex2D.MipSlice;
            value.Tex2D.FirstArraySlice = old.Tex2D.FirstArraySlice;
            value.Tex2D.ArraySize = old.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D: value.Tex3D = old.Tex3D; break;
        case D3D10DDIRESOURCE_TEXTURECUBE: value.TexCube = old.TexCube; break;
    }
    return value;
}

static D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW NativeWddm20Convert(const D3D11DDIARG_CREATESHADERRESOURCEVIEW &old)
{
    D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW value = {};
    value.hDrvResource = old.hDrvResource;
    value.Format = old.Format;
    value.ResourceDimension = old.ResourceDimension;
    switch (old.ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER: value.Buffer = old.Buffer; break;
        case D3D11DDIRESOURCE_BUFFEREX: value.BufferEx = old.BufferEx; break;
        case D3D10DDIRESOURCE_TEXTURE1D: value.Tex1D = old.Tex1D; break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            value.Tex2D.MostDetailedMip = old.Tex2D.MostDetailedMip;
            value.Tex2D.FirstArraySlice = old.Tex2D.FirstArraySlice;
            value.Tex2D.MipLevels = old.Tex2D.MipLevels;
            value.Tex2D.ArraySize = old.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D: value.Tex3D = old.Tex3D; break;
        case D3D10DDIRESOURCE_TEXTURECUBE: value.TexCube = old.TexCube; break;
    }
    return value;
}

static D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW NativeWddm20Convert(const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW &old)
{
    D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW value = {};
    value.hDrvResource = old.hDrvResource;
    value.Format = old.Format;
    value.ResourceDimension = old.ResourceDimension;
    switch (old.ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER: value.Buffer = old.Buffer; break;
        case D3D10DDIRESOURCE_TEXTURE1D: value.Tex1D = old.Tex1D; break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            value.Tex2D.MipSlice = old.Tex2D.MipSlice;
            value.Tex2D.FirstArraySlice = old.Tex2D.FirstArraySlice;
            value.Tex2D.ArraySize = old.Tex2D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D: value.Tex3D = old.Tex3D; break;
        default: break;
    }
    return value;
}

#define NATIVE_WDDM20_OBJECT(Name, Old, New, DriverHandle, RuntimeHandle) \
static SIZE_T APIENTRY NativeWddm20CalcPrivate##Name##Size(D3D10DDI_HDEVICE h, const Old *old) \
{ \
    New value = NativeWddm20Convert(*old); \
    return NativeWddm20Device(h)->wddm20_functions.pfnCalcPrivate##Name##Size(h, &value); \
} \
static void APIENTRY NativeWddm20Create##Name(D3D10DDI_HDEVICE h, const Old *old, DriverHandle object, RuntimeHandle runtime) \
{ \
    New value = NativeWddm20Convert(*old); \
    NativeWddm20Device(h)->wddm20_functions.pfnCreate##Name(h, &value, object, runtime); \
}
NATIVE_WDDM20_OBJECT(BlendState, D3D10_1_DDI_BLEND_DESC, D3D11_1_DDI_BLEND_DESC, D3D10DDI_HBLENDSTATE, D3D10DDI_HRTBLENDSTATE)
NATIVE_WDDM20_OBJECT(RasterizerState, D3D10_DDI_RASTERIZER_DESC, D3DWDDM2_0DDI_RASTERIZER_DESC, D3D10DDI_HRASTERIZERSTATE, D3D10DDI_HRTRASTERIZERSTATE)
NATIVE_WDDM20_OBJECT(Query, D3D10DDIARG_CREATEQUERY, D3DWDDM2_0DDIARG_CREATEQUERY, D3D10DDI_HQUERY, D3D10DDI_HRTQUERY)
NATIVE_WDDM20_OBJECT(RenderTargetView, D3D10DDIARG_CREATERENDERTARGETVIEW, D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW, D3D10DDI_HRENDERTARGETVIEW, D3D10DDI_HRTRENDERTARGETVIEW)
NATIVE_WDDM20_OBJECT(ShaderResourceView, D3D11DDIARG_CREATESHADERRESOURCEVIEW, D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW, D3D10DDI_HSHADERRESOURCEVIEW, D3D10DDI_HRTSHADERRESOURCEVIEW)
NATIVE_WDDM20_OBJECT(UnorderedAccessView, D3D11DDIARG_CREATEUNORDEREDACCESSVIEW, D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW, D3D11DDI_HUNORDEREDACCESSVIEW, D3D11DDI_HRTUNORDEREDACCESSVIEW)
#undef NATIVE_WDDM20_OBJECT

static HRESULT APIENTRY NativeWddm20CreateContext(D3D10DDI_HRTCORELAYER layer, D3DWDDM2_0DDICB_CREATECONTEXT *args)
{
    NativeDevice *device = static_cast<NativeDevice *>(layer.handle);
    if (!args || !device->kernel_callbacks.pfnCreateContextCb) return E_INVALIDARG;
    D3DDDICB_CREATECONTEXT legacy = {};
    legacy.NodeOrdinal = args->NodeOrdinal;
    legacy.EngineAffinity = args->EngineAffinity;
    legacy.Flags = args->Flags;
    legacy.pPrivateDriverData = args->pPrivateDriverData;
    legacy.PrivateDriverDataSize = args->PrivateDriverDataSize;
    HRESULT hr = device->kernel_callbacks.pfnCreateContextCb(device->runtime_device, &legacy);
    if (SUCCEEDED(hr))
    {
        args->hContext = legacy.hContext;
        args->pCommandBuffer = legacy.pCommandBuffer;
        args->CommandBufferSize = legacy.CommandBufferSize;
        args->pAllocationList = legacy.pAllocationList;
        args->AllocationListSize = legacy.AllocationListSize;
        args->pPatchLocationList = legacy.pPatchLocationList;
        args->PatchLocationListSize = legacy.PatchLocationListSize;
        args->CommandBuffer = legacy.CommandBuffer;
    }
    return hr;
}

static HRESULT APIENTRY NativeWddm20CreateContextVirtual(D3D10DDI_HRTCORELAYER layer, D3DWDDM2_0DDICB_CREATECONTEXTVIRTUAL *args)
{
    NativeDevice *device = static_cast<NativeDevice *>(layer.handle);
    if (!args || !device->kernel_callbacks.pfnCreateContextVirtualCb) return E_INVALIDARG;
    D3DDDICB_CREATECONTEXTVIRTUAL legacy = {};
    legacy.NodeOrdinal = args->NodeOrdinal;
    legacy.EngineAffinity = args->EngineAffinity;
    legacy.Flags = args->Flags;
    legacy.pPrivateDriverData = args->pPrivateDriverData;
    legacy.PrivateDriverDataSize = args->PrivateDriverDataSize;
    HRESULT hr = device->kernel_callbacks.pfnCreateContextVirtualCb(device->runtime_device, &legacy);
    if (SUCCEEDED(hr)) args->hContext = legacy.hContext;
    return hr;
}

static void APIENTRY NativeWddm20NoDeferredWork(D3D10DDI_HRTCORELAYER)
{
    /* The native runtime already destroys staging resources synchronously and
     * has no deferred garbage collection or amortized work queue. */
}

static UINT NativeWddm20RefreshCount(UINT count, UINT base, UINT capacity)
{
    if (base >= capacity) return 0;
    return count == ~0u ? capacity - base : min(count, capacity - base);
}

#define NATIVE_WDDM20_REFRESH_RANGE(Name, Stage, Method, Member, Capacity) \
static void APIENTRY NativeWddm20State##Name(D3D10DDI_HRTCORELAYER layer, UINT count, UINT base) \
{ \
    NativeContext *context = static_cast<NativeDevice *>(layer.handle)->context; \
    count = NativeWddm20RefreshCount(count, base, Capacity); \
    if (context && count) context->Method(Stage, base, count, &context->Member[Stage][base]); \
}
NATIVE_WDDM20_REFRESH_RANGE(VsConstBuf, 0, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(PsConstBuf, 1, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(GsConstBuf, 2, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(VsSrv, 0, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(PsSrv, 1, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(GsSrv, 2, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(VsSampler, 0, SetSamplers, samplers, 16)
NATIVE_WDDM20_REFRESH_RANGE(PsSampler, 1, SetSamplers, samplers, 16)
NATIVE_WDDM20_REFRESH_RANGE(GsSampler, 2, SetSamplers, samplers, 16)
NATIVE_WDDM20_REFRESH_RANGE(HsConstBuf, 3, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(HsSrv, 3, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(HsSampler, 3, SetSamplers, samplers, 16)
NATIVE_WDDM20_REFRESH_RANGE(DsConstBuf, 4, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(DsSrv, 4, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(DsSampler, 4, SetSamplers, samplers, 16)
NATIVE_WDDM20_REFRESH_RANGE(CsConstBuf, 5, SetConstantBuffers, constant_buffers, 14)
NATIVE_WDDM20_REFRESH_RANGE(CsSrv, 5, SetShaderResources, shader_resources, 128)
NATIVE_WDDM20_REFRESH_RANGE(CsSampler, 5, SetSamplers, samplers, 16)
#undef NATIVE_WDDM20_REFRESH_RANGE

#define NATIVE_WDDM20_REFRESH(Name, Expression) \
static void APIENTRY NativeWddm20State##Name(D3D10DDI_HRTCORELAYER layer) \
{ \
    NativeContext *context = static_cast<NativeDevice *>(layer.handle)->context; \
    if (context) context->Expression; \
}
NATIVE_WDDM20_REFRESH(VsShader, VSSetShader(context->vertex_shader, NULL, 0))
NATIVE_WDDM20_REFRESH(PsShader, PSSetShader(context->pixel_shader, NULL, 0))
NATIVE_WDDM20_REFRESH(GsShader, GSSetShader(context->geometry_shader, NULL, 0))
NATIVE_WDDM20_REFRESH(IaInputLayout, IASetInputLayout(context->input_layout))
NATIVE_WDDM20_REFRESH(IaIndexBuf, IASetIndexBuffer(context->index_buffer, context->index_format, context->index_offset))
NATIVE_WDDM20_REFRESH(IaPrimitiveTopology, IASetPrimitiveTopology(context->topology))
NATIVE_WDDM20_REFRESH(OmRenderTargets, RefreshOutputBindings())
NATIVE_WDDM20_REFRESH(OmBlendState, OMSetBlendState(context->blend_state, context->blend_factor, context->sample_mask))
NATIVE_WDDM20_REFRESH(OmDepthState, OMSetDepthStencilState(context->depth_stencil_state, context->stencil_ref))
NATIVE_WDDM20_REFRESH(RsRastState, RSSetState(context->rasterizer_state))
NATIVE_WDDM20_REFRESH(RsViewports, RSSetViewports(context->viewport_count, context->viewports))
NATIVE_WDDM20_REFRESH(RsScissor, RSSetScissorRects(context->scissor_count, context->scissors))
NATIVE_WDDM20_REFRESH(HsShader, HSSetShader(context->hull_shader, NULL, 0))
NATIVE_WDDM20_REFRESH(DsShader, DSSetShader(context->domain_shader, NULL, 0))
NATIVE_WDDM20_REFRESH(CsShader, CSSetShader(context->compute_shader, NULL, 0))
#undef NATIVE_WDDM20_REFRESH

static void APIENTRY NativeWddm20StateIaVertexBuf(D3D10DDI_HRTCORELAYER layer, UINT count, UINT base)
{
    NativeContext *context = static_cast<NativeDevice *>(layer.handle)->context;
    count = NativeWddm20RefreshCount(count, base, 32);
    if (context && count) context->IASetVertexBuffers(base, count, &context->vertex_buffers[base],
            &context->vertex_strides[base], &context->vertex_offsets[base]);
}

static void APIENTRY NativeWddm20StateCsUav(D3D10DDI_HRTCORELAYER layer, UINT count, UINT base)
{
    NativeContext *context = static_cast<NativeDevice *>(layer.handle)->context;
    count = NativeWddm20RefreshCount(count, base, 8);
    if (context && count) context->CSSetUnorderedAccessViews(base, count, context->compute_uavs + base, NULL);
}

static void APIENTRY NativeWddm20StateSoTargets(D3D10DDI_HRTCORELAYER layer)
{
    NativeContext *context = static_cast<NativeDevice *>(layer.handle)->context;
    if (context) context->RefreshStreamOutput();
}

static void APIENTRY NativeWddm20StateTextFilterSize(D3D10DDI_HRTCORELAYER layer)
{
    auto *device = static_cast<NativeDevice *>(layer.handle);
    if (device->context && device->wddm20_functions.pfnSetTextFilterSize)
        device->wddm20_functions.pfnSetTextFilterSize(device->driver_device, 0, 0);
}

static void NativeWddm20InitializeCallbacks(NativeDevice *device)
{
    auto &callbacks = device->wddm20_callbacks;
    callbacks.pfnSetErrorCb = NativeSetError;
    callbacks.pfnCreateContextCb = NativeWddm20CreateContext;
    callbacks.pfnCreateContextVirtualCb = NativeWddm20CreateContextVirtual;
    callbacks.pfnDisableDeferredStagingResourceDestruction = NativeWddm20NoDeferredWork;
    callbacks.pfnPerformAmortizedProcessingCb = NativeWddm20NoDeferredWork;
#define REFRESH(Name) callbacks.pfnState##Name##Cb = NativeWddm20State##Name
    REFRESH(VsConstBuf); REFRESH(PsConstBuf); REFRESH(GsConstBuf);
    REFRESH(VsSrv); REFRESH(PsSrv); REFRESH(GsSrv);
    REFRESH(VsSampler); REFRESH(PsSampler); REFRESH(GsSampler);
    REFRESH(VsShader); REFRESH(PsShader); REFRESH(GsShader);
    REFRESH(IaInputLayout); REFRESH(IaVertexBuf); REFRESH(IaIndexBuf); REFRESH(IaPrimitiveTopology);
    REFRESH(OmRenderTargets); REFRESH(OmBlendState); REFRESH(OmDepthState);
    REFRESH(RsRastState); REFRESH(RsViewports); REFRESH(RsScissor);
    REFRESH(HsShader); REFRESH(HsConstBuf); REFRESH(HsSrv); REFRESH(HsSampler);
    REFRESH(DsShader); REFRESH(DsConstBuf); REFRESH(DsSrv); REFRESH(DsSampler);
    REFRESH(CsShader); REFRESH(CsConstBuf); REFRESH(CsSrv); REFRESH(CsSampler); REFRESH(CsUav);
    REFRESH(SoTargets); REFRESH(TextFilterSize);
#undef REFRESH
}


static void NativeWddm20InstallFunctions(NativeDevice *device)
{
    /* Copy only identical signatures. Changed slots use typed adapters below;
     * an unsupported API must never call a newer DDI through an older type. */
    device->functions.pfnPsSetShaderResources = device->wddm20_functions.pfnPsSetShaderResources;
    device->functions.pfnPsSetShader = device->wddm20_functions.pfnPsSetShader;
    device->functions.pfnPsSetSamplers = device->wddm20_functions.pfnPsSetSamplers;
    device->functions.pfnVsSetShader = device->wddm20_functions.pfnVsSetShader;
    device->functions.pfnDrawIndexed = device->wddm20_functions.pfnDrawIndexed;
    device->functions.pfnDraw = device->wddm20_functions.pfnDraw;
    device->functions.pfnDynamicIABufferMapNoOverwrite = device->wddm20_functions.pfnDynamicIABufferMapNoOverwrite;
    device->functions.pfnDynamicIABufferUnmap = device->wddm20_functions.pfnDynamicIABufferUnmap;
    device->functions.pfnDynamicConstantBufferMapDiscard = device->wddm20_functions.pfnDynamicConstantBufferMapDiscard;
    device->functions.pfnDynamicIABufferMapDiscard = device->wddm20_functions.pfnDynamicIABufferMapDiscard;
    device->functions.pfnDynamicConstantBufferUnmap = device->wddm20_functions.pfnDynamicConstantBufferUnmap;
    device->functions.pfnIaSetInputLayout = device->wddm20_functions.pfnIaSetInputLayout;
    device->functions.pfnIaSetVertexBuffers = device->wddm20_functions.pfnIaSetVertexBuffers;
    device->functions.pfnIaSetIndexBuffer = device->wddm20_functions.pfnIaSetIndexBuffer;
    device->functions.pfnDrawIndexedInstanced = device->wddm20_functions.pfnDrawIndexedInstanced;
    device->functions.pfnDrawInstanced = device->wddm20_functions.pfnDrawInstanced;
    device->functions.pfnDynamicResourceMapDiscard = device->wddm20_functions.pfnDynamicResourceMapDiscard;
    device->functions.pfnDynamicResourceUnmap = device->wddm20_functions.pfnDynamicResourceUnmap;
    device->functions.pfnGsSetShader = device->wddm20_functions.pfnGsSetShader;
    device->functions.pfnIaSetTopology = device->wddm20_functions.pfnIaSetTopology;
    device->functions.pfnStagingResourceMap = device->wddm20_functions.pfnStagingResourceMap;
    device->functions.pfnStagingResourceUnmap = device->wddm20_functions.pfnStagingResourceUnmap;
    device->functions.pfnVsSetShaderResources = device->wddm20_functions.pfnVsSetShaderResources;
    device->functions.pfnVsSetSamplers = device->wddm20_functions.pfnVsSetSamplers;
    device->functions.pfnGsSetShaderResources = device->wddm20_functions.pfnGsSetShaderResources;
    device->functions.pfnGsSetSamplers = device->wddm20_functions.pfnGsSetSamplers;
    device->functions.pfnSetRenderTargets = device->wddm20_functions.pfnSetRenderTargets;
    device->functions.pfnShaderResourceViewReadAfterWriteHazard = device->wddm20_functions.pfnShaderResourceViewReadAfterWriteHazard;
    device->functions.pfnResourceReadAfterWriteHazard = device->wddm20_functions.pfnResourceReadAfterWriteHazard;
    device->functions.pfnSetBlendState = device->wddm20_functions.pfnSetBlendState;
    device->functions.pfnSetDepthStencilState = device->wddm20_functions.pfnSetDepthStencilState;
    device->functions.pfnSetRasterizerState = device->wddm20_functions.pfnSetRasterizerState;
    device->functions.pfnQueryEnd = device->wddm20_functions.pfnQueryEnd;
    device->functions.pfnQueryBegin = device->wddm20_functions.pfnQueryBegin;
    device->functions.pfnSoSetTargets = device->wddm20_functions.pfnSoSetTargets;
    device->functions.pfnDrawAuto = device->wddm20_functions.pfnDrawAuto;
    device->functions.pfnSetViewports = device->wddm20_functions.pfnSetViewports;
    device->functions.pfnSetScissorRects = device->wddm20_functions.pfnSetScissorRects;
    device->functions.pfnClearRenderTargetView = device->wddm20_functions.pfnClearRenderTargetView;
    device->functions.pfnClearDepthStencilView = device->wddm20_functions.pfnClearDepthStencilView;
    device->functions.pfnSetPredication = device->wddm20_functions.pfnSetPredication;
    device->functions.pfnQueryGetData = device->wddm20_functions.pfnQueryGetData;
    device->functions.pfnGenMips = device->wddm20_functions.pfnGenMips;
    device->functions.pfnResourceCopy = device->wddm20_functions.pfnResourceCopy;
    device->functions.pfnResourceResolveSubresource = device->wddm20_functions.pfnResourceResolveSubresource;
    device->functions.pfnResourceMap = device->wddm20_functions.pfnResourceMap;
    device->functions.pfnResourceUnmap = device->wddm20_functions.pfnResourceUnmap;
    device->functions.pfnResourceIsStagingBusy = device->wddm20_functions.pfnResourceIsStagingBusy;
    device->functions.pfnCalcPrivateResourceSize = device->wddm20_functions.pfnCalcPrivateResourceSize;
    device->functions.pfnCalcPrivateOpenedResourceSize = device->wddm20_functions.pfnCalcPrivateOpenedResourceSize;
    device->functions.pfnCreateResource = device->wddm20_functions.pfnCreateResource;
    device->functions.pfnOpenResource = device->wddm20_functions.pfnOpenResource;
    device->functions.pfnDestroyResource = device->wddm20_functions.pfnDestroyResource;
    device->functions.pfnDestroyShaderResourceView = device->wddm20_functions.pfnDestroyShaderResourceView;
    device->functions.pfnDestroyRenderTargetView = device->wddm20_functions.pfnDestroyRenderTargetView;
    device->functions.pfnCalcPrivateDepthStencilViewSize = device->wddm20_functions.pfnCalcPrivateDepthStencilViewSize;
    device->functions.pfnCreateDepthStencilView = device->wddm20_functions.pfnCreateDepthStencilView;
    device->functions.pfnDestroyDepthStencilView = device->wddm20_functions.pfnDestroyDepthStencilView;
    device->functions.pfnCalcPrivateElementLayoutSize = device->wddm20_functions.pfnCalcPrivateElementLayoutSize;
    device->functions.pfnCreateElementLayout = device->wddm20_functions.pfnCreateElementLayout;
    device->functions.pfnDestroyElementLayout = device->wddm20_functions.pfnDestroyElementLayout;
    device->functions.pfnDestroyBlendState = device->wddm20_functions.pfnDestroyBlendState;
    device->functions.pfnCalcPrivateDepthStencilStateSize = device->wddm20_functions.pfnCalcPrivateDepthStencilStateSize;
    device->functions.pfnCreateDepthStencilState = device->wddm20_functions.pfnCreateDepthStencilState;
    device->functions.pfnDestroyDepthStencilState = device->wddm20_functions.pfnDestroyDepthStencilState;
    device->functions.pfnDestroyRasterizerState = device->wddm20_functions.pfnDestroyRasterizerState;
    device->functions.pfnDestroyShader = device->wddm20_functions.pfnDestroyShader;
    device->functions.pfnCalcPrivateSamplerSize = device->wddm20_functions.pfnCalcPrivateSamplerSize;
    device->functions.pfnCreateSampler = device->wddm20_functions.pfnCreateSampler;
    device->functions.pfnDestroySampler = device->wddm20_functions.pfnDestroySampler;
    device->functions.pfnDestroyQuery = device->wddm20_functions.pfnDestroyQuery;
    device->functions.pfnCheckFormatSupport = device->wddm20_functions.pfnCheckFormatSupport;
    device->functions.pfnCheckCounterInfo = device->wddm20_functions.pfnCheckCounterInfo;
    device->functions.pfnCheckCounter = device->wddm20_functions.pfnCheckCounter;
    device->functions.pfnDestroyDevice = device->wddm20_functions.pfnDestroyDevice;
    device->functions.pfnSetTextFilterSize = device->wddm20_functions.pfnSetTextFilterSize;
    device->functions.pfnResourceConvert = device->wddm20_functions.pfnResourceConvert;
    device->functions.pfnDrawIndexedInstancedIndirect = device->wddm20_functions.pfnDrawIndexedInstancedIndirect;
    device->functions.pfnDrawInstancedIndirect = device->wddm20_functions.pfnDrawInstancedIndirect;
    device->functions.pfnCommandListExecute = device->wddm20_functions.pfnCommandListExecute;
    device->functions.pfnHsSetShaderResources = device->wddm20_functions.pfnHsSetShaderResources;
    device->functions.pfnHsSetShader = device->wddm20_functions.pfnHsSetShader;
    device->functions.pfnHsSetSamplers = device->wddm20_functions.pfnHsSetSamplers;
    device->functions.pfnDsSetShaderResources = device->wddm20_functions.pfnDsSetShaderResources;
    device->functions.pfnDsSetShader = device->wddm20_functions.pfnDsSetShader;
    device->functions.pfnDsSetSamplers = device->wddm20_functions.pfnDsSetSamplers;
    device->functions.pfnCheckDeferredContextHandleSizes = device->wddm20_functions.pfnCheckDeferredContextHandleSizes;
    device->functions.pfnCalcDeferredContextHandleSize = device->wddm20_functions.pfnCalcDeferredContextHandleSize;
    device->functions.pfnCalcPrivateDeferredContextSize = device->wddm20_functions.pfnCalcPrivateDeferredContextSize;
    device->functions.pfnCreateDeferredContext = device->wddm20_functions.pfnCreateDeferredContext;
    device->functions.pfnAbandonCommandList = device->wddm20_functions.pfnAbandonCommandList;
    device->functions.pfnCalcPrivateCommandListSize = device->wddm20_functions.pfnCalcPrivateCommandListSize;
    device->functions.pfnCreateCommandList = device->wddm20_functions.pfnCreateCommandList;
    device->functions.pfnDestroyCommandList = device->wddm20_functions.pfnDestroyCommandList;
    device->functions.pfnPsSetShaderWithIfaces = device->wddm20_functions.pfnPsSetShaderWithIfaces;
    device->functions.pfnVsSetShaderWithIfaces = device->wddm20_functions.pfnVsSetShaderWithIfaces;
    device->functions.pfnGsSetShaderWithIfaces = device->wddm20_functions.pfnGsSetShaderWithIfaces;
    device->functions.pfnHsSetShaderWithIfaces = device->wddm20_functions.pfnHsSetShaderWithIfaces;
    device->functions.pfnDsSetShaderWithIfaces = device->wddm20_functions.pfnDsSetShaderWithIfaces;
    device->functions.pfnCsSetShaderWithIfaces = device->wddm20_functions.pfnCsSetShaderWithIfaces;
    device->functions.pfnCreateComputeShader = device->wddm20_functions.pfnCreateComputeShader;
    device->functions.pfnCsSetShader = device->wddm20_functions.pfnCsSetShader;
    device->functions.pfnCsSetShaderResources = device->wddm20_functions.pfnCsSetShaderResources;
    device->functions.pfnCsSetSamplers = device->wddm20_functions.pfnCsSetSamplers;
    device->functions.pfnDestroyUnorderedAccessView = device->wddm20_functions.pfnDestroyUnorderedAccessView;
    device->functions.pfnClearUnorderedAccessViewUint = device->wddm20_functions.pfnClearUnorderedAccessViewUint;
    device->functions.pfnClearUnorderedAccessViewFloat = device->wddm20_functions.pfnClearUnorderedAccessViewFloat;
    device->functions.pfnCsSetUnorderedAccessViews = device->wddm20_functions.pfnCsSetUnorderedAccessViews;
    device->functions.pfnDispatch = device->wddm20_functions.pfnDispatch;
    device->functions.pfnDispatchIndirect = device->wddm20_functions.pfnDispatchIndirect;
    device->functions.pfnSetResourceMinLOD = device->wddm20_functions.pfnSetResourceMinLOD;
    device->functions.pfnCopyStructureCount = device->wddm20_functions.pfnCopyStructureCount;
    device->functions.pfnRecycleCommandList = device->wddm20_functions.pfnRecycleCommandList;
    device->functions.pfnRecycleCreateCommandList = device->wddm20_functions.pfnRecycleCreateCommandList;
    device->functions.pfnRecycleCreateDeferredContext = device->wddm20_functions.pfnRecycleCreateDeferredContext;
    device->functions.pfnRecycleDestroyCommandList = device->wddm20_functions.pfnRecycleDestroyCommandList;
    device->functions.pfnVsSetConstantBuffers = device->wddm20_functions.pfnVsSetConstantBuffers ? NativeWddm20VsSetConstantBuffers : NULL;
    device->functions.pfnPsSetConstantBuffers = device->wddm20_functions.pfnPsSetConstantBuffers ? NativeWddm20PsSetConstantBuffers : NULL;
    device->functions.pfnGsSetConstantBuffers = device->wddm20_functions.pfnGsSetConstantBuffers ? NativeWddm20GsSetConstantBuffers : NULL;
    device->functions.pfnHsSetConstantBuffers = device->wddm20_functions.pfnHsSetConstantBuffers ? NativeWddm20HsSetConstantBuffers : NULL;
    device->functions.pfnDsSetConstantBuffers = device->wddm20_functions.pfnDsSetConstantBuffers ? NativeWddm20DsSetConstantBuffers : NULL;
    device->functions.pfnCsSetConstantBuffers = device->wddm20_functions.pfnCsSetConstantBuffers ? NativeWddm20CsSetConstantBuffers : NULL;
    device->functions.pfnDefaultConstantBufferUpdateSubresourceUP = device->wddm20_functions.pfnDefaultConstantBufferUpdateSubresourceUP ? NativeWddm20DefaultConstantBufferUpdateSubresourceUP : NULL;
    device->functions.pfnResourceUpdateSubresourceUP = device->wddm20_functions.pfnResourceUpdateSubresourceUP ? NativeWddm20ResourceUpdateSubresourceUP : NULL;
    device->functions.pfnResourceCopyRegion = device->wddm20_functions.pfnResourceCopyRegion ? NativeWddm20ResourceCopyRegion : NULL;
    device->functions.pfnResourceConvertRegion = device->wddm20_functions.pfnResourceConvertRegion ? NativeWddm20ResourceConvertRegion : NULL;
    device->functions.pfnFlush = device->wddm20_functions.pfnFlush ? NativeWddm20Flush : NULL;
    device->functions.pfnCheckMultisampleQualityLevels = device->wddm20_functions.pfnCheckMultisampleQualityLevels ? NativeWddm20CheckMultisampleQualityLevels : NULL;
    device->functions.pfnCalcPrivateBlendStateSize = device->wddm20_functions.pfnCalcPrivateBlendStateSize ? NativeWddm20CalcPrivateBlendStateSize : NULL;
    device->functions.pfnCreateBlendState = device->wddm20_functions.pfnCreateBlendState ? NativeWddm20CreateBlendState : NULL;
    device->functions.pfnCalcPrivateRasterizerStateSize = device->wddm20_functions.pfnCalcPrivateRasterizerStateSize ? NativeWddm20CalcPrivateRasterizerStateSize : NULL;
    device->functions.pfnCreateRasterizerState = device->wddm20_functions.pfnCreateRasterizerState ? NativeWddm20CreateRasterizerState : NULL;
    device->functions.pfnCalcPrivateQuerySize = device->wddm20_functions.pfnCalcPrivateQuerySize ? NativeWddm20CalcPrivateQuerySize : NULL;
    device->functions.pfnCreateQuery = device->wddm20_functions.pfnCreateQuery ? NativeWddm20CreateQuery : NULL;
    device->functions.pfnCalcPrivateRenderTargetViewSize = device->wddm20_functions.pfnCalcPrivateRenderTargetViewSize ? NativeWddm20CalcPrivateRenderTargetViewSize : NULL;
    device->functions.pfnCreateRenderTargetView = device->wddm20_functions.pfnCreateRenderTargetView ? NativeWddm20CreateRenderTargetView : NULL;
    device->functions.pfnCalcPrivateShaderResourceViewSize = device->wddm20_functions.pfnCalcPrivateShaderResourceViewSize ? NativeWddm20CalcPrivateShaderResourceViewSize : NULL;
    device->functions.pfnCreateShaderResourceView = device->wddm20_functions.pfnCreateShaderResourceView ? NativeWddm20CreateShaderResourceView : NULL;
    device->functions.pfnCalcPrivateUnorderedAccessViewSize = device->wddm20_functions.pfnCalcPrivateUnorderedAccessViewSize ? NativeWddm20CalcPrivateUnorderedAccessViewSize : NULL;
    device->functions.pfnCreateUnorderedAccessView = device->wddm20_functions.pfnCreateUnorderedAccessView ? NativeWddm20CreateUnorderedAccessView : NULL;
    static_assert(offsetof(DXGI1_4_DDI_BASE_FUNCTIONS, pfnBlt1) == sizeof(DXGI1_1_DDI_BASE_FUNCTIONS), "DXGI base prefix");
    memcpy(&device->dxgi_functions, &device->wddm20_dxgi, sizeof(device->dxgi_functions));
}
