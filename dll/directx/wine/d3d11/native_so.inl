/*
 * PROJECT: ReactOS Direct3D 11 runtime
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Native geometry shaders and stream-output declarations
 */

HRESULT STDMETHODCALLTYPE NativeDevice::CreateGeometryShaderWithStreamOutput(const void *code,
        SIZE_T length, const D3D11_SO_DECLARATION_ENTRY *entries, UINT count,
        const UINT *strides, UINT stride_count, UINT rasterized_stream,
        ID3D11ClassLinkage *linkage, ID3D11GeometryShader **out)
{
    if (out) *out = NULL;
    if (count > D3D11_SO_STREAM_COUNT * D3D11_SO_OUTPUT_COMPONENT_COUNT
            || (!!entries != !!count) || stride_count > D3D11_SO_BUFFER_SLOT_COUNT
            || (stride_count && !strides)
            || (rasterized_stream != D3D11_SO_NO_RASTERIZED_STREAM && rasterized_stream >= D3D11_SO_STREAM_COUNT))
        return E_INVALIDARG;
    bool fl10 = feature_level < D3D_FEATURE_LEVEL_11_0;
    if (fl10 && (rasterized_stream || stride_count > 1 || (!count && stride_count))) return E_INVALIDARG;

    NativeShaderBytecode bytecode;
    HRESULT hr = bytecode.Parse(code, length, ~0u);
    if (FAILED(hr)) return hr;
    UINT type = bytecode.tokens ? bytecode.tokens[0] >> 16 : ~0u;
    bool passthrough = type != 2;
    if (passthrough && (!count || (type != ~0u && type != 1 && type != 4))) return E_INVALIDARG;
    if (bytecode.tokens)
    {
        UINT model = bytecode.tokens[0] & 0xff;
        UINT maximum = feature_level >= D3D_FEATURE_LEVEL_11_0 ? 0x50
                : feature_level >= D3D_FEATURE_LEVEL_10_1 ? 0x41 : 0x40;
        if ((model != 0x40 && model != 0x41 && model != 0x50) || model > maximum
                || (type == 4 && (model != 0x50 || fl10))) return E_INVALIDARG;
    }
    if (linkage) return E_NOTIMPL;

    NativeCommandArray<D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY> translated(count, NULL);
    if (!translated.Valid()) return E_OUTOFMEMORY;
    UINT buffer_bytes[4] = {}, buffer_entries[4] = {}, data_entries[4] = {};
    UINT buffer_stream[4] = {~0u, ~0u, ~0u, ~0u};
    UINT stream_components[4] = {};
    BYTE used_components[4][32] = {};
    UINT expanded_count = 0, used_buffers = 0;
    for (UINT i = 0; i < count; ++i)
    {
        const auto &entry = entries[i];
        if (entry.Stream >= D3D11_SO_STREAM_COUNT || (fl10 && entry.Stream)
                || entry.OutputSlot >= D3D11_SO_BUFFER_SLOT_COUNT || !entry.ComponentCount)
            return E_INVALIDARG;
        UINT slot = entry.OutputSlot;
        if (buffer_stream[slot] != ~0u && buffer_stream[slot] != entry.Stream) return E_INVALIDARG;
        buffer_stream[slot] = entry.Stream;
        if (used_buffers <= slot) used_buffers = slot + 1;
        ++buffer_entries[slot];
        buffer_bytes[slot] += 4 * entry.ComponentCount;
        if (buffer_bytes[slot] > D3D11_SO_BUFFER_MAX_STRIDE_IN_BYTES) return E_INVALIDARG;
        auto &ddi = translated.Data()[i];
        ddi.Stream = entry.Stream;
        ddi.OutputSlot = slot;
        if (!entry.SemanticName)
        {
            if (entry.SemanticIndex || entry.StartComponent) return E_INVALIDARG;
            ddi.RegisterIndex = ~0u;
            /* Temporarily store the gap length; the DDI needs <=4-bit masks. */
            ddi.RegisterMask = entry.ComponentCount;
            expanded_count += (entry.ComponentCount + 3) / 4;
            continue;
        }
        if (entry.StartComponent > 3 || entry.ComponentCount > 4
                || entry.StartComponent + entry.ComponentCount > 4) return E_INVALIDARG;
        const auto *signature = bytecode.FindOutput(entry.SemanticName, entry.SemanticIndex, entry.Stream);
        if (!signature || signature->Register >= 32 || !signature->Mask) return E_INVALIDARG;
        UINT first = 0;
        while (!(signature->Mask & (1u << first))) ++first;
        UINT mask = ((1u << entry.ComponentCount) - 1) << (first + entry.StartComponent);
        if ((mask & signature->Mask) != mask || (used_components[entry.Stream][signature->Register] & mask))
            return E_INVALIDARG;
        used_components[entry.Stream][signature->Register] |= mask;
        stream_components[entry.Stream] += entry.ComponentCount;
        if (stream_components[entry.Stream] > D3D11_SO_OUTPUT_COMPONENT_COUNT) return E_INVALIDARG;
        ddi.RegisterIndex = signature->Register;
        ddi.RegisterMask = mask;
        ++data_entries[slot];
        ++expanded_count;
    }
    UINT driver_strides[4] = {};
    for (UINT slot = 0; slot < used_buffers; ++slot)
    {
        if (!buffer_entries[slot]) continue;
        if (!data_entries[slot] || (fl10 && used_buffers > 1 && buffer_entries[slot] > 1)) return E_INVALIDARG;
        UINT stride = buffer_bytes[slot];
        if (stride_count)
        {
            if (stride_count <= slot || strides[slot] < stride || (strides[slot] & 3)
                    || strides[slot] > D3D11_SO_BUFFER_MAX_STRIDE_IN_BYTES) return E_INVALIDARG;
            stride = strides[slot];
        }
        driver_strides[slot] = stride;
    }
    NativeCommandArray<D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY> declaration(expanded_count, NULL);
    if (!declaration.Valid()) return E_OUTOFMEMORY;
    UINT cursor = 0;
    for (UINT i = 0; i < count; ++i)
    {
        const auto &entry = translated.Data()[i];
        if (entry.RegisterIndex != ~0u)
            declaration.Data()[cursor++] = entry;
        else
        {
            UINT remaining = entry.RegisterMask;
            while (remaining)
            {
                UINT components = remaining > 4 ? 4 : remaining;
                auto &gap = declaration.Data()[cursor++];
                gap = entry;
                gap.RegisterMask = (1u << components) - 1;
                remaining -= components;
            }
        }
    }

    auto &f = functions;
    auto &m = wddm20_functions;
    if (!f.pfnDestroyShader || (wddm20
            ? (!m.pfnCalcPrivateGeometryShaderWithStreamOutput || !m.pfnCreateGeometryShaderWithStreamOutput)
            : (!f.pfnCalcPrivateGeometryShaderWithStreamOutput || !f.pfnCreateGeometryShaderWithStreamOutput)))
        return E_NOTIMPL;
    if (!out) return S_FALSE;

    D3D10DDIARG_STAGE_IO_SIGNATURES signatures = bytecode.signatures;
    D3D11_1DDIARG_STAGE_IO_SIGNATURES modern = bytecode.modern_signatures;
    if (passthrough)
    {
        /* With no GS program the previous stage's outputs are forwarded. */
        signatures.pInputSignature = signatures.pOutputSignature;
        signatures.NumInputSignatureEntries = signatures.NumOutputSignatureEntries;
        modern.pInputSignature = modern.pOutputSignature;
        modern.NumInputSignatureEntries = modern.NumOutputSignatureEntries;
    }
    D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT args = {};
    args.pShaderCode = passthrough ? NULL : bytecode.tokens;
    args.pOutputStreamDecl = declaration.Data();
    args.NumEntries = expanded_count;
    args.BufferStridesInBytes = driver_strides;
    args.NumStrides = used_buffers;
    args.RasterizedStream = rasterized_stream;
    NativeLock guard(this);
    NativeGeometryShader *shader = new NativeGeometryShader(this);
    if (!shader) return E_OUTOFMEMORY;
    BeginCall();
    SIZE_T size = wddm20
            ? m.pfnCalcPrivateGeometryShaderWithStreamOutput(driver_device, &args, &modern)
            : f.pfnCalcPrivateGeometryShaderWithStreamOutput(driver_device, &args, &signatures);
    if (FAILED(operation_error)) { hr = operation_error; shader->Release(); return hr; }
    shader->handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!shader->handle.pDrvPrivate) { shader->Release(); return E_OUTOFMEMORY; }
    D3D10DDI_HRTSHADER runtime_shader = {shader};
    BeginCall();
    if (wddm20)
        m.pfnCreateGeometryShaderWithStreamOutput(driver_device, &args, shader->handle, runtime_shader, &modern);
    else
        f.pfnCreateGeometryShaderWithStreamOutput(driver_device, &args, shader->handle, runtime_shader, &signatures);
    hr = operation_error;
    if (FAILED(hr)) { shader->Release(); return hr; }
    shader->created = true;
    *out = shader;
    return S_OK;
}
