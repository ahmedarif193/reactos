/*
 * Copyright 2026 Ahmed Arif
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#define COBJMACROS

#include <stdarg.h>
#include <limits.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "d3d9.h"
#include "d3dkmthk.h"
#include "d3dumddi.h"
#include "dxva2api.h"
#include <reactos/dxva_surface_fence.h>

#include "decoder.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxva2);

#define DXVA2_MAX_CAP_ENTRIES 256u
#define DXVA2_BUFFER_TYPE_COUNT 9u

static const GUID reactos_dxva_surface_fence_guid = REACTOS_DXVA_SURFACE_FENCE_GUID_INIT;

typedef HRESULT (WINAPI *PFN_D3DUMDRT_CREATE_DEVICE_CALLBACKS)(
        D3DKMT_HANDLE adapter, D3DKMT_HANDLE device, D3DDDI_DEVICECALLBACKS *callbacks,
        HANDLE *runtime_device);
typedef HRESULT (WINAPI *PFN_D3DUMDRT_DESTROY_DEVICE_CALLBACKS)(HANDLE runtime_device);

struct dxva2_umd_runtime
{
    D3DKMT_HANDLE kmt_adapter;
    D3DKMT_HANDLE kmt_device;
    HMODULE runtime_module;
    HMODULE umd_module;
    PFN_D3DUMDRT_DESTROY_DEVICE_CALLBACKS destroy_callbacks;
    HANDLE runtime_device;
    HANDLE umd_adapter;
    HANDLE umd_device;
    D3DDDI_ADAPTERFUNCS adapter_funcs;
    D3DDDI_DEVICEFUNCS device_funcs;
};

struct dxva2_decoder_buffer
{
    HANDLE resource;
    D3DDDIFORMAT format;
    UINT size;
    BOOL locked;
};

struct dxva2_surface_fence
{
    IReactOSDxvaSurfaceFence IReactOSDxvaSurfaceFence_iface;
    LONG refcount;
    CRITICAL_SECTION lock;
    HANDLE event;
    volatile LONG pending;
    volatile LONG result;
    volatile LONG producer_thread_id;
    struct dxva2_decoder *decoder;
    UINT surface_index;
    UINT active_calls;
    HANDLE calls_drained_event;
};

struct dxva2_decoder_surface
{
    IDirect3DSurface9 *surface;
    HANDLE resource;
    struct dxva2_surface_fence *fence;
    BOOL fence_attached;
    BOOL shared_locked;
    D3DDDIARG_LOCK shared_lock;
    ULONG generation;
    ULONG fallback_generation;
};

struct dxva2_decoder
{
    IDirectXVideoDecoder IDirectXVideoDecoder_iface;
    LONG refcount;
    IDirectXVideoDecoderService *service;
    IDirect3DDevice9 *device;
    struct dxva2_umd_runtime runtime;
    GUID guid;
    DXVA2_VideoDesc video_desc;
    DXVA2_ConfigPictureDecode config;
    HANDLE decode;
    struct dxva2_decoder_surface *surfaces;
    UINT surface_count;
    struct dxva2_decoder_buffer *buffers;
    UINT buffer_count;
    CRITICAL_SECTION copy_lock;
    HANDLE copy_thread;
    HANDLE copy_wake_event;
    HANDLE copy_stop_event;
    HANDLE overlay;
    UINT overlay_surface;
    RECT overlay_source;
    RECT overlay_destination;
    DWORD overlay_flags;
    UINT current_surface;
    BOOL frame_begun;
};

static HRESULT dxva2_decoder_copy_output(struct dxva2_decoder *decoder,
        UINT surface_index);

static HRESULT dxva2_decoder_hide_overlay(struct dxva2_decoder *decoder);

static HRESULT dxva2_umd_lock_resource(struct dxva2_umd_runtime *runtime,
        HANDLE resource, BOOL read_only, D3DDDIARG_LOCK *lock);

static HRESULT dxva2_umd_unlock_resource(struct dxva2_umd_runtime *runtime,
        HANDLE resource);

static HRESULT dxva2_decoder_unlock_shared_surface(
        struct dxva2_decoder *decoder, UINT surface_index);

static HRESULT dxva2_surface_fence_begin_call(
        struct dxva2_surface_fence *fence,
        struct dxva2_decoder **decoder, UINT *surface_index);

static void dxva2_surface_fence_end_call(struct dxva2_surface_fence *fence);

static HRESULT dxva2_decoder_get_presentation_flags(
        const struct dxva2_decoder *decoder, DWORD *flags)
{
    UINT nominal_range;
    UINT transfer_matrix;

    if (!flags)
        return E_POINTER;

    nominal_range = decoder->video_desc.SampleFormat.NominalRange;
    transfer_matrix = decoder->video_desc.SampleFormat.VideoTransferMatrix;
    *flags = 0;

    if (nominal_range == DXVA2_NominalRange_Unknown ||
            nominal_range == DXVA2_NominalRange_16_235)
        *flags |= REACTOS_DXVA_SURFACE_PRESENT_LIMITED_RGB;
    else if (nominal_range != DXVA2_NominalRange_0_255)
        return E_NOTIMPL;

    if (transfer_matrix == DXVA2_VideoTransferMatrix_BT709 ||
            (transfer_matrix == DXVA2_VideoTransferMatrix_Unknown &&
             decoder->video_desc.SampleHeight > 576))
        *flags |= REACTOS_DXVA_SURFACE_PRESENT_BT709;
    else if (transfer_matrix != DXVA2_VideoTransferMatrix_Unknown &&
            transfer_matrix != DXVA2_VideoTransferMatrix_BT601)
        return E_NOTIMPL;

    return S_OK;
}

static struct dxva2_surface_fence *impl_from_IReactOSDxvaSurfaceFence(
        IReactOSDxvaSurfaceFence *iface)
{
    return CONTAINING_RECORD(iface, struct dxva2_surface_fence,
            IReactOSDxvaSurfaceFence_iface);
}

static HRESULT WINAPI dxva2_surface_fence_QueryInterface(
        IReactOSDxvaSurfaceFence *iface, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    if (!IsEqualIID(iid, &IID_IUnknown))
    {
        *object = NULL;
        return E_NOINTERFACE;
    }

    *object = iface;
    IReactOSDxvaSurfaceFence_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI dxva2_surface_fence_AddRef(IReactOSDxvaSurfaceFence *iface)
{
    struct dxva2_surface_fence *fence = impl_from_IReactOSDxvaSurfaceFence(iface);

    return InterlockedIncrement(&fence->refcount);
}

static ULONG WINAPI dxva2_surface_fence_Release(IReactOSDxvaSurfaceFence *iface)
{
    struct dxva2_surface_fence *fence = impl_from_IReactOSDxvaSurfaceFence(iface);
    ULONG refcount = InterlockedDecrement(&fence->refcount);

    if (!refcount)
    {
        CloseHandle(fence->calls_drained_event);
        CloseHandle(fence->event);
        DeleteCriticalSection(&fence->lock);
        free(fence);
    }
    return refcount;
}

static HRESULT WINAPI dxva2_surface_fence_GetPresentationFlags(
        IReactOSDxvaSurfaceFence *iface, DWORD *flags)
{
    struct dxva2_surface_fence *fence = impl_from_IReactOSDxvaSurfaceFence(iface);
    struct dxva2_decoder *decoder;
    UINT surface_index;
    HRESULT hr;

    if (!flags)
        return E_POINTER;
    if (FAILED(hr = dxva2_surface_fence_begin_call(fence, &decoder, &surface_index)))
        return hr;
    hr = dxva2_decoder_get_presentation_flags(decoder, flags);
    dxva2_surface_fence_end_call(fence);
    return hr;
}

static HRESULT dxva2_surface_fence_begin_call(struct dxva2_surface_fence *fence,
        struct dxva2_decoder **decoder, UINT *surface_index)
{
    HRESULT hr = S_OK;

    EnterCriticalSection(&fence->lock);
    if (!fence->decoder)
    {
        hr = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
    }
    else
    {
        if (!fence->active_calls++)
            ResetEvent(fence->calls_drained_event);
        *decoder = fence->decoder;
        *surface_index = fence->surface_index;
    }
    LeaveCriticalSection(&fence->lock);
    return hr;
}

static void dxva2_surface_fence_end_call(struct dxva2_surface_fence *fence)
{
    EnterCriticalSection(&fence->lock);
    if (fence->active_calls && !--fence->active_calls)
        SetEvent(fence->calls_drained_event);
    LeaveCriticalSection(&fence->lock);
}

static HRESULT dxva2_surface_fence_wait_internal(
        struct dxva2_surface_fence *fence, DWORD flags)
{
    if (flags & ~REACTOS_DXVA_SURFACE_FENCE_DONOTWAIT)
        return E_INVALIDARG;

    for (;;)
    {
        DWORD wait_status;

        EnterCriticalSection(&fence->lock);
        if (!fence->pending)
        {
            HRESULT result = fence->result;

            LeaveCriticalSection(&fence->lock);
            return result;
        }
        if ((DWORD)fence->producer_thread_id == GetCurrentThreadId())
        {
            LeaveCriticalSection(&fence->lock);
            return S_OK;
        }
        LeaveCriticalSection(&fence->lock);

        wait_status = WaitForSingleObject(fence->event,
                flags & REACTOS_DXVA_SURFACE_FENCE_DONOTWAIT ? 0 : INFINITE);
        if (wait_status == WAIT_TIMEOUT)
            return D3DERR_WASSTILLDRAWING;
        if (wait_status != WAIT_OBJECT_0)
            return HRESULT_FROM_WIN32(GetLastError());
    }
}

static HRESULT WINAPI dxva2_surface_fence_Wait(
        IReactOSDxvaSurfaceFence *iface, DWORD flags)
{
    struct dxva2_surface_fence *fence = impl_from_IReactOSDxvaSurfaceFence(iface);
    struct dxva2_decoder *decoder;
    UINT surface_index;
    HRESULT hr;

    if (FAILED(hr = dxva2_surface_fence_begin_call(fence, &decoder, &surface_index)))
        return hr;
    UNREFERENCED_PARAMETER(decoder);
    UNREFERENCED_PARAMETER(surface_index);
    hr = dxva2_surface_fence_wait_internal(fence, flags);
    dxva2_surface_fence_end_call(fence);
    return hr;
}

static HRESULT WINAPI dxva2_surface_fence_GetSharedMemory(
        IReactOSDxvaSurfaceFence *iface, DWORD flags,
        REACTOS_DXVA_SURFACE_MEMORY *memory)
{
    struct dxva2_surface_fence *fence = impl_from_IReactOSDxvaSurfaceFence(iface);
    struct dxva2_decoder_surface *surface;
    struct dxva2_decoder *decoder;
    ULONGLONG chroma_offset;
    UINT surface_index;
    UINT storage_height;
    UINT row_count;
    HRESULT hr;

    if (!memory)
        return E_POINTER;
    memset(memory, 0, sizeof(*memory));
    if (flags & ~REACTOS_DXVA_SURFACE_FENCE_DONOTWAIT)
        return E_INVALIDARG;
    if (FAILED(hr = dxva2_surface_fence_begin_call(fence, &decoder, &surface_index)))
        return hr;
    if (FAILED(hr = dxva2_surface_fence_wait_internal(fence, flags)))
        goto done;

    /* Keep the completed allocation mapped only until the application reuses
     * this render target in BeginFrame. This gives D3D9 a stable source for a
     * synchronous texture upload without overlapping the next decode. */
    EnterCriticalSection(&decoder->copy_lock);
    surface = &decoder->surfaces[surface_index];
    if (!surface->shared_locked)
    {
        if (FAILED(hr = dxva2_decoder_hide_overlay(decoder)))
            goto unlock;
        if (FAILED(hr = dxva2_umd_lock_resource(&decoder->runtime,
                surface->resource, TRUE, &surface->shared_lock)))
            goto unlock;
        surface->shared_locked = TRUE;
    }

    if (!surface->shared_lock.pSurfData ||
            (decoder->video_desc.SampleHeight & 1) ||
            surface->shared_lock.Pitch < decoder->video_desc.SampleWidth ||
            !surface->shared_lock.SlicePitch ||
            surface->shared_lock.SlicePitch % surface->shared_lock.Pitch)
    {
        hr = E_FAIL;
        (void)dxva2_decoder_unlock_shared_surface(decoder, surface_index);
        goto unlock;
    }
    row_count = surface->shared_lock.SlicePitch / surface->shared_lock.Pitch;
    if ((ULONGLONG)row_count * 2 % 3)
    {
        hr = E_FAIL;
        (void)dxva2_decoder_unlock_shared_surface(decoder, surface_index);
        goto unlock;
    }
    storage_height = row_count * 2 / 3;
    chroma_offset = (ULONGLONG)surface->shared_lock.Pitch * storage_height;
    if (storage_height < decoder->video_desc.SampleHeight ||
            chroma_offset > surface->shared_lock.SlicePitch ||
            decoder->video_desc.SampleHeight / 2 >
            (surface->shared_lock.SlicePitch - chroma_offset) /
            surface->shared_lock.Pitch)
    {
        hr = E_FAIL;
        (void)dxva2_decoder_unlock_shared_surface(decoder, surface_index);
        goto unlock;
    }

    memory->Data = surface->shared_lock.pSurfData;
    memory->Size = surface->shared_lock.SlicePitch;
    memory->Width = decoder->video_desc.SampleWidth;
    memory->Height = decoder->video_desc.SampleHeight;
    memory->Pitch = surface->shared_lock.Pitch;
    memory->StorageHeight = storage_height;
    memory->PlaneCount = 2;
    memory->PlaneOffsets[0] = 0;
    memory->PlaneOffsets[1] = (UINT)chroma_offset;
    memory->PlanePitches[0] = surface->shared_lock.Pitch;
    memory->PlanePitches[1] = surface->shared_lock.Pitch;
    memory->Generation = surface->generation;
    hr = S_OK;

unlock:
    LeaveCriticalSection(&decoder->copy_lock);
done:
    dxva2_surface_fence_end_call(fence);
    return hr;
}

static HRESULT WINAPI dxva2_surface_fence_PrepareFallback(
        IReactOSDxvaSurfaceFence *iface, DWORD flags)
{
    struct dxva2_surface_fence *fence = impl_from_IReactOSDxvaSurfaceFence(iface);
    struct dxva2_decoder *decoder;
    UINT surface_index;
    ULONG generation;
    HRESULT hr;

    if (flags & ~REACTOS_DXVA_SURFACE_FENCE_DONOTWAIT)
        return E_INVALIDARG;
    if ((DWORD)InterlockedCompareExchange(&fence->producer_thread_id, 0, 0) ==
            GetCurrentThreadId())
        return S_OK;
    if (FAILED(hr = dxva2_surface_fence_begin_call(fence, &decoder, &surface_index)))
        return hr;
    if (FAILED(hr = dxva2_surface_fence_wait_internal(fence, flags)))
        goto done;

    EnterCriticalSection(&decoder->copy_lock);
    generation = decoder->surfaces[surface_index].generation;
    if (decoder->surfaces[surface_index].fallback_generation == generation)
    {
        LeaveCriticalSection(&decoder->copy_lock);
        hr = S_OK;
        goto done;
    }
    if (FAILED(hr = dxva2_decoder_hide_overlay(decoder)))
    {
        LeaveCriticalSection(&decoder->copy_lock);
        goto done;
    }

    InterlockedExchange(&fence->producer_thread_id, GetCurrentThreadId());
    hr = dxva2_decoder_copy_output(decoder, surface_index);
    InterlockedExchange(&fence->producer_thread_id, 0);
    if (SUCCEEDED(hr))
        decoder->surfaces[surface_index].fallback_generation = generation;
    LeaveCriticalSection(&decoder->copy_lock);

done:
    dxva2_surface_fence_end_call(fence);
    return hr;
}

static HRESULT WINAPI dxva2_surface_fence_Present(
        IReactOSDxvaSurfaceFence *iface, const RECT *source,
        const RECT *destination, DWORD flags)
{
    struct dxva2_surface_fence *fence = impl_from_IReactOSDxvaSurfaceFence(iface);
    struct dxva2_decoder *decoder;
    D3DDDI_OVERLAYINFO overlay_info;
    UINT surface_index;
    HRESULT hr;

    if (!source || !destination || source->left < 0 || source->top < 0 ||
            source->right <= source->left || source->bottom <= source->top ||
            destination->left < 0 || destination->top < 0 ||
            destination->right <= destination->left ||
            destination->bottom <= destination->top ||
            (flags & ~REACTOS_DXVA_SURFACE_PRESENT_ALLOWED))
        return E_INVALIDARG;
    if (FAILED(hr = dxva2_surface_fence_begin_call(fence, &decoder, &surface_index)))
        return hr;
    if (FAILED(hr = dxva2_surface_fence_wait_internal(fence, 0)))
        goto done;
    if (!decoder->runtime.device_funcs.pfnCreateOverlay ||
            !decoder->runtime.device_funcs.pfnUpdateOverlay ||
            !decoder->runtime.device_funcs.pfnFlipOverlay ||
            !decoder->runtime.device_funcs.pfnDestroyOverlay)
    {
        hr = E_NOTIMPL;
        goto done;
    }

    {
        DWORD presentation_flags;

        if (FAILED(hr = dxva2_decoder_get_presentation_flags(
                decoder, &presentation_flags)))
            goto done;
        flags |= presentation_flags;
    }

    memset(&overlay_info, 0, sizeof(overlay_info));
    overlay_info.hResource = decoder->surfaces[surface_index].resource;
    overlay_info.SrcRect = *source;
    overlay_info.DstRect = *destination;
    overlay_info.Flags.LimitedRGB =
            !!(flags & REACTOS_DXVA_SURFACE_PRESENT_LIMITED_RGB);
    overlay_info.Flags.YCbCrBT709 =
            !!(flags & REACTOS_DXVA_SURFACE_PRESENT_BT709);

    EnterCriticalSection(&decoder->copy_lock);
    if (!decoder->overlay)
    {
        D3DDDIARG_CREATEOVERLAY create;

        memset(&create, 0, sizeof(create));
        create.VidPnSourceId = 0;
        create.OverlayInfo = overlay_info;
        hr = decoder->runtime.device_funcs.pfnCreateOverlay(
                decoder->runtime.umd_device, &create);
        if (SUCCEEDED(hr) && create.hOverlay)
            decoder->overlay = create.hOverlay;
        else if (SUCCEEDED(hr))
            hr = E_FAIL;
    }
    else if (!EqualRect(&decoder->overlay_source, source) ||
            !EqualRect(&decoder->overlay_destination, destination) ||
            decoder->overlay_flags != flags)
    {
        D3DDDIARG_UPDATEOVERLAY update;

        memset(&update, 0, sizeof(update));
        update.hOverlay = decoder->overlay;
        update.OverlayInfo = overlay_info;
        hr = decoder->runtime.device_funcs.pfnUpdateOverlay(
                decoder->runtime.umd_device, &update);
    }
    else
    {
        D3DDDIARG_FLIPOVERLAY flip;

        memset(&flip, 0, sizeof(flip));
        flip.hOverlay = decoder->overlay;
        flip.hSource = decoder->surfaces[surface_index].resource;
        hr = decoder->runtime.device_funcs.pfnFlipOverlay(
                decoder->runtime.umd_device, &flip);
    }
    if (SUCCEEDED(hr))
    {
        decoder->overlay_surface = surface_index;
        decoder->overlay_source = *source;
        decoder->overlay_destination = *destination;
        decoder->overlay_flags = flags;
    }
    LeaveCriticalSection(&decoder->copy_lock);

done:
    dxva2_surface_fence_end_call(fence);
    return hr;
}

static HRESULT WINAPI dxva2_surface_fence_Hide(
        IReactOSDxvaSurfaceFence *iface)
{
    struct dxva2_surface_fence *fence = impl_from_IReactOSDxvaSurfaceFence(iface);
    struct dxva2_decoder *decoder;
    UINT surface_index;
    HRESULT hr;

    if (FAILED(hr = dxva2_surface_fence_begin_call(fence, &decoder, &surface_index)))
        return hr;
    UNREFERENCED_PARAMETER(surface_index);
    EnterCriticalSection(&decoder->copy_lock);
    hr = dxva2_decoder_hide_overlay(decoder);
    LeaveCriticalSection(&decoder->copy_lock);
    dxva2_surface_fence_end_call(fence);
    return hr;
}

static const IReactOSDxvaSurfaceFenceVtbl dxva2_surface_fence_vtbl =
{
    dxva2_surface_fence_QueryInterface,
    dxva2_surface_fence_AddRef,
    dxva2_surface_fence_Release,
    dxva2_surface_fence_GetPresentationFlags,
    dxva2_surface_fence_Wait,
    dxva2_surface_fence_GetSharedMemory,
    dxva2_surface_fence_PrepareFallback,
    dxva2_surface_fence_Present,
    dxva2_surface_fence_Hide,
};

static struct dxva2_surface_fence *dxva2_surface_fence_create(
        struct dxva2_decoder *decoder, UINT surface_index)
{
    struct dxva2_surface_fence *fence;

    if (!(fence = calloc(1, sizeof(*fence))))
        return NULL;
    fence->IReactOSDxvaSurfaceFence_iface.lpVtbl = &dxva2_surface_fence_vtbl;
    fence->refcount = 1;
    fence->result = S_OK;
    fence->decoder = decoder;
    fence->surface_index = surface_index;
    InitializeCriticalSection(&fence->lock);
    if (!(fence->event = CreateEventW(NULL, TRUE, TRUE, NULL)) ||
            !(fence->calls_drained_event = CreateEventW(NULL, TRUE, TRUE, NULL)))
    {
        if (fence->event)
            CloseHandle(fence->event);
        DeleteCriticalSection(&fence->lock);
        free(fence);
        return NULL;
    }
    return fence;
}

static void dxva2_surface_fence_detach(struct dxva2_surface_fence *fence)
{
    EnterCriticalSection(&fence->lock);
    fence->decoder = NULL;
    if (!fence->active_calls)
        SetEvent(fence->calls_drained_event);
    LeaveCriticalSection(&fence->lock);
    WaitForSingleObject(fence->calls_drained_event, INFINITE);
}

static void dxva2_surface_fence_submit(struct dxva2_surface_fence *fence)
{
    EnterCriticalSection(&fence->lock);
    ResetEvent(fence->event);
    fence->result = E_PENDING;
    fence->pending = TRUE;
    LeaveCriticalSection(&fence->lock);
}

static void dxva2_surface_fence_complete(struct dxva2_surface_fence *fence,
        HRESULT result)
{
    EnterCriticalSection(&fence->lock);
    fence->result = result;
    fence->pending = FALSE;
    SetEvent(fence->event);
    LeaveCriticalSection(&fence->lock);
}

static struct dxva2_decoder *impl_from_IDirectXVideoDecoder(IDirectXVideoDecoder *iface)
{
    return CONTAINING_RECORD(iface, struct dxva2_decoder, IDirectXVideoDecoder_iface);
}

static void dxva2_video_desc_to_ddi(const DXVA2_VideoDesc *source, DXVADDI_VIDEODESC *destination)
{
    memset(destination, 0, sizeof(*destination));
    destination->SampleWidth = source->SampleWidth;
    destination->SampleHeight = source->SampleHeight;
    destination->SampleFormat.Value = source->SampleFormat.value;
    destination->Format = (D3DDDIFORMAT)source->Format;
    destination->InputSampleFreq.Numerator = source->InputSampleFreq.Numerator;
    destination->InputSampleFreq.Denominator = source->InputSampleFreq.Denominator;
    destination->OutputFrameFreq.Numerator = source->OutputFrameFreq.Numerator;
    destination->OutputFrameFreq.Denominator = source->OutputFrameFreq.Denominator;
    destination->UABProtectionLevel = source->UABProtectionLevel;
    destination->Reserved = source->Reserved;
}

static HRESULT dxva2_config_to_ddi(const DXVA2_ConfigPictureDecode *source,
        DXVADDI_CONFIGPICTUREDECODE *destination)
{
    if (source->ConfigMinRenderTargetBuffCount > USHRT_MAX)
        return E_INVALIDARG;

    memset(destination, 0, sizeof(*destination));
    destination->guidConfigBitstreamEncryption = source->guidConfigBitstreamEncryption;
    destination->guidConfigMBcontrolEncryption = source->guidConfigMBcontrolEncryption;
    destination->guidConfigResidDiffEncryption = source->guidConfigResidDiffEncryption;
    destination->ConfigBitstreamRaw = source->ConfigBitstreamRaw;
    destination->ConfigMBcontrolRasterOrder = source->ConfigMBcontrolRasterOrder;
    destination->ConfigResidDiffHost = source->ConfigResidDiffHost;
    destination->ConfigSpatialResid8 = source->ConfigSpatialResid8;
    destination->ConfigResid8Subtraction = source->ConfigResid8Subtraction;
    destination->ConfigSpatialHost8or9Clipping = source->ConfigSpatialHost8or9Clipping;
    destination->ConfigSpatialResidInterleaved = source->ConfigSpatialResidInterleaved;
    destination->ConfigIntraResidUnsigned = source->ConfigIntraResidUnsigned;
    destination->ConfigResidDiffAccelerator = source->ConfigResidDiffAccelerator;
    destination->ConfigHostInverseScan = source->ConfigHostInverseScan;
    destination->ConfigSpecificIDCT = source->ConfigSpecificIDCT;
    destination->Config4GroupedCoefs = source->Config4GroupedCoefs;
    destination->ConfigMinRenderTargetBuffCount = source->ConfigMinRenderTargetBuffCount;
    destination->ConfigDecoderSpecific = source->ConfigDecoderSpecific;
    return S_OK;
}

static void dxva2_config_from_ddi(const DXVADDI_CONFIGPICTUREDECODE *source,
        DXVA2_ConfigPictureDecode *destination)
{
    memset(destination, 0, sizeof(*destination));
    destination->guidConfigBitstreamEncryption = source->guidConfigBitstreamEncryption;
    destination->guidConfigMBcontrolEncryption = source->guidConfigMBcontrolEncryption;
    destination->guidConfigResidDiffEncryption = source->guidConfigResidDiffEncryption;
    destination->ConfigBitstreamRaw = source->ConfigBitstreamRaw;
    destination->ConfigMBcontrolRasterOrder = source->ConfigMBcontrolRasterOrder;
    destination->ConfigResidDiffHost = source->ConfigResidDiffHost;
    destination->ConfigSpatialResid8 = source->ConfigSpatialResid8;
    destination->ConfigResid8Subtraction = source->ConfigResid8Subtraction;
    destination->ConfigSpatialHost8or9Clipping = source->ConfigSpatialHost8or9Clipping;
    destination->ConfigSpatialResidInterleaved = source->ConfigSpatialResidInterleaved;
    destination->ConfigIntraResidUnsigned = source->ConfigIntraResidUnsigned;
    destination->ConfigResidDiffAccelerator = source->ConfigResidDiffAccelerator;
    destination->ConfigHostInverseScan = source->ConfigHostInverseScan;
    destination->ConfigSpecificIDCT = source->ConfigSpecificIDCT;
    destination->Config4GroupedCoefs = source->Config4GroupedCoefs;
    destination->ConfigMinRenderTargetBuffCount = source->ConfigMinRenderTargetBuffCount;
    destination->ConfigDecoderSpecific = source->ConfigDecoderSpecific;
}

static void dxva2_umd_runtime_cleanup(struct dxva2_umd_runtime *runtime)
{
    D3DKMT_DESTROYDEVICE destroy_device;
    D3DKMT_CLOSEADAPTER close_adapter;

    if (runtime->umd_device && runtime->device_funcs.pfnDestroyDevice)
        runtime->device_funcs.pfnDestroyDevice(runtime->umd_device);
    runtime->umd_device = NULL;

    if (runtime->umd_adapter && runtime->adapter_funcs.pfnCloseAdapter)
        runtime->adapter_funcs.pfnCloseAdapter(runtime->umd_adapter);
    runtime->umd_adapter = NULL;

    if (runtime->runtime_device && runtime->destroy_callbacks)
        runtime->destroy_callbacks(runtime->runtime_device);
    runtime->runtime_device = NULL;

    if (runtime->umd_module)
        FreeLibrary(runtime->umd_module);
    runtime->umd_module = NULL;
    if (runtime->runtime_module)
        FreeLibrary(runtime->runtime_module);
    runtime->runtime_module = NULL;

    if (runtime->kmt_device)
    {
        memset(&destroy_device, 0, sizeof(destroy_device));
        destroy_device.hDevice = runtime->kmt_device;
        D3DKMTDestroyDevice(&destroy_device);
    }
    runtime->kmt_device = 0;

    if (runtime->kmt_adapter)
    {
        memset(&close_adapter, 0, sizeof(close_adapter));
        close_adapter.hAdapter = runtime->kmt_adapter;
        D3DKMTCloseAdapter(&close_adapter);
    }
    runtime->kmt_adapter = 0;
}

static HRESULT dxva2_umd_runtime_init(IDirect3DDevice9 *device, struct dxva2_umd_runtime *runtime)
{
    PFN_D3DUMDRT_CREATE_DEVICE_CALLBACKS create_callbacks;
    PFND3DDDI_OPENADAPTER open_umd_adapter;
    D3DDEVICE_CREATION_PARAMETERS creation_parameters;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME open_adapter;
    D3DKMT_QUERYADAPTERINFO query_adapter;
    D3DKMT_UMDFILENAMEINFO umd_filename;
    D3DKMT_CREATEDEVICE create_kmt_device;
    D3DDDI_ADAPTERCALLBACKS adapter_callbacks;
    D3DDDI_DEVICECALLBACKS device_callbacks;
    D3DDDIARG_OPENADAPTER open_umd;
    D3DDDIARG_CREATEDEVICE create_umd_device;
    MONITORINFOEXW monitor_info;
    IDirect3D9 *d3d = NULL;
    HMONITOR monitor;
    NTSTATUS status;
    HRESULT hr;

    if (!device || !runtime)
        return DXVA2_E_NOT_INITIALIZED;
    memset(runtime, 0, sizeof(*runtime));

    if (FAILED(hr = IDirect3DDevice9_GetCreationParameters(device, &creation_parameters)))
        return hr;
    if (FAILED(hr = IDirect3DDevice9_GetDirect3D(device, &d3d)))
        return hr;
    monitor = IDirect3D9_GetAdapterMonitor(d3d, creation_parameters.AdapterOrdinal);
    IDirect3D9_Release(d3d);
    if (!monitor)
        return DXVA2_E_NOT_AVAILABLE;

    memset(&monitor_info, 0, sizeof(monitor_info));
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&monitor_info))
        return HRESULT_FROM_WIN32(GetLastError());

    memset(&open_adapter, 0, sizeof(open_adapter));
    lstrcpynW(open_adapter.DeviceName, monitor_info.szDevice, ARRAY_SIZE(open_adapter.DeviceName));
    status = D3DKMTOpenAdapterFromGdiDisplayName(&open_adapter);
    if (status < 0)
        return DXVA2_E_NOT_AVAILABLE;
    runtime->kmt_adapter = open_adapter.hAdapter;

    memset(&umd_filename, 0, sizeof(umd_filename));
    umd_filename.Version = KMTUMDVERSION_DX9;
    memset(&query_adapter, 0, sizeof(query_adapter));
    query_adapter.hAdapter = runtime->kmt_adapter;
    query_adapter.Type = KMTQAITYPE_UMDRIVERNAME;
    query_adapter.pPrivateDriverData = &umd_filename;
    query_adapter.PrivateDriverDataSize = sizeof(umd_filename);
    status = D3DKMTQueryAdapterInfo(&query_adapter);
    if (status < 0 || !umd_filename.UmdFileName[0])
    {
        hr = DXVA2_E_NOT_AVAILABLE;
        goto failed;
    }

    memset(&create_kmt_device, 0, sizeof(create_kmt_device));
    create_kmt_device.hAdapter = runtime->kmt_adapter;
    status = D3DKMTCreateDevice(&create_kmt_device);
    if (status < 0)
    {
        hr = DXVA2_E_NOT_AVAILABLE;
        goto failed;
    }
    runtime->kmt_device = create_kmt_device.hDevice;

    runtime->runtime_module = LoadLibraryW(L"d3dumdrt.dll");
    if (!runtime->runtime_module)
    {
        hr = DXVA2_E_NOT_AVAILABLE;
        goto failed;
    }
    create_callbacks = (PFN_D3DUMDRT_CREATE_DEVICE_CALLBACKS)GetProcAddress(runtime->runtime_module,
            "D3DUmdRtCreateDeviceCallbacks");
    runtime->destroy_callbacks = (PFN_D3DUMDRT_DESTROY_DEVICE_CALLBACKS)GetProcAddress(runtime->runtime_module,
            "D3DUmdRtDestroyDeviceCallbacks");
    if (!create_callbacks || !runtime->destroy_callbacks)
    {
        hr = DXVA2_E_NOT_AVAILABLE;
        goto failed;
    }

    memset(&device_callbacks, 0, sizeof(device_callbacks));
    if (FAILED(hr = create_callbacks(runtime->kmt_adapter, runtime->kmt_device,
            &device_callbacks, &runtime->runtime_device)))
        goto failed;

    runtime->umd_module = LoadLibraryW(umd_filename.UmdFileName);
    if (!runtime->umd_module)
    {
        hr = DXVA2_E_NOT_AVAILABLE;
        goto failed;
    }
    open_umd_adapter = (PFND3DDDI_OPENADAPTER)GetProcAddress(runtime->umd_module, D3DDDI_OPENADAPTER_PROCNAME);
    if (!open_umd_adapter)
    {
        hr = DXVA2_E_NOT_AVAILABLE;
        goto failed;
    }

    memset(&adapter_callbacks, 0, sizeof(adapter_callbacks));
    memset(&open_umd, 0, sizeof(open_umd));
    open_umd.hAdapter = (HANDLE)(ULONG_PTR)runtime->kmt_adapter;
    open_umd.Interface = D3D_UMD_INTERFACE_VERSION;
    open_umd.Version = D3D_UMD_INTERFACE_VERSION;
    open_umd.pAdapterCallbacks = &adapter_callbacks;
    open_umd.pAdapterFuncs = &runtime->adapter_funcs;
    if (FAILED(hr = open_umd_adapter(&open_umd)))
        goto failed;
    runtime->umd_adapter = open_umd.hAdapter;
    if (open_umd.DriverVersion != D3D_UMD_INTERFACE_VERSION ||
            !runtime->adapter_funcs.pfnGetCaps || !runtime->adapter_funcs.pfnCreateDevice ||
            !runtime->adapter_funcs.pfnCloseAdapter)
    {
        hr = DXVA2_E_NOT_AVAILABLE;
        goto failed;
    }

    memset(&create_umd_device, 0, sizeof(create_umd_device));
    create_umd_device.hDevice = runtime->runtime_device;
    create_umd_device.Interface = D3D_UMD_INTERFACE_VERSION;
    create_umd_device.Version = D3D_UMD_INTERFACE_VERSION;
    create_umd_device.pCallbacks = &device_callbacks;
    create_umd_device.pDeviceFuncs = &runtime->device_funcs;
    if (FAILED(hr = runtime->adapter_funcs.pfnCreateDevice(runtime->umd_adapter, &create_umd_device)))
        goto failed;
    runtime->umd_device = create_umd_device.hDevice;
    return S_OK;

failed:
    dxva2_umd_runtime_cleanup(runtime);
    return hr;
}

static HRESULT dxva2_umd_get_caps(struct dxva2_umd_runtime *runtime, D3DDDICAPS_TYPE type,
        const void *info, void *data, UINT data_size)
{
    D3DDDIARG_GETCAPS caps;

    if (!runtime || !runtime->umd_adapter || !runtime->adapter_funcs.pfnGetCaps || !data || !data_size)
        return E_INVALIDARG;

    memset(&caps, 0, sizeof(caps));
    caps.Type = type;
    caps.pInfo = (void *)info;
    caps.pData = data;
    caps.DataSize = data_size;
    return runtime->adapter_funcs.pfnGetCaps(runtime->umd_adapter, &caps);
}

static HRESULT dxva2_umd_create_resource(struct dxva2_umd_runtime *runtime, D3DDDIFORMAT format,
        UINT width, UINT height, D3DDDI_POOL pool, D3DDDI_RESOURCEFLAGS flags,
        void *runtime_cookie, HANDLE *resource)
{
    D3DDDI_SURFACEINFO surface;
    D3DDDIARG_CREATERESOURCE create;
    HRESULT hr;

    if (!runtime || !runtime->umd_device || !runtime->device_funcs.pfnCreateResource ||
            !width || !height || !runtime_cookie || !resource)
        return E_INVALIDARG;
    *resource = NULL;

    memset(&surface, 0, sizeof(surface));
    surface.Width = width;
    surface.Height = height;
    surface.Depth = 1;

    memset(&create, 0, sizeof(create));
    create.Format = format;
    create.Pool = pool;
    create.MultisampleType = D3DDDIMULTISAMPLE_NONE;
    create.pSurfList = &surface;
    create.SurfCount = 1;
    create.MipLevels = 1;
    create.hResource = runtime_cookie;
    create.Flags = flags;
    create.Rotation = D3DDDI_ROTATION_IDENTITY;
    if (SUCCEEDED(hr = runtime->device_funcs.pfnCreateResource(runtime->umd_device, &create)))
        *resource = create.hResource;
    return hr;
}

static HRESULT dxva2_umd_lock_resource(struct dxva2_umd_runtime *runtime, HANDLE resource,
        BOOL read_only, D3DDDIARG_LOCK *lock)
{
    if (!runtime || !runtime->umd_device || !runtime->device_funcs.pfnLock || !resource || !lock)
        return E_INVALIDARG;

    memset(lock, 0, sizeof(*lock));
    lock->hResource = resource;
    lock->Flags.ReadOnly = read_only;
    return runtime->device_funcs.pfnLock(runtime->umd_device, lock);
}

static HRESULT dxva2_umd_unlock_resource(struct dxva2_umd_runtime *runtime, HANDLE resource)
{
    D3DDDIARG_UNLOCK unlock;

    if (!runtime || !runtime->umd_device || !runtime->device_funcs.pfnUnlock || !resource)
        return E_INVALIDARG;
    memset(&unlock, 0, sizeof(unlock));
    unlock.hResource = resource;
    return runtime->device_funcs.pfnUnlock(runtime->umd_device, &unlock);
}

/* decoder->copy_lock must be held. */
static HRESULT dxva2_decoder_unlock_shared_surface(
        struct dxva2_decoder *decoder, UINT surface_index)
{
    struct dxva2_decoder_surface *surface;
    HRESULT hr;

    if (surface_index >= decoder->surface_count)
        return E_INVALIDARG;
    surface = &decoder->surfaces[surface_index];
    if (!surface->shared_locked)
        return S_OK;

    hr = dxva2_umd_unlock_resource(&decoder->runtime, surface->resource);
    if (SUCCEEDED(hr))
    {
        memset(&surface->shared_lock, 0, sizeof(surface->shared_lock));
        surface->shared_locked = FALSE;
    }
    return hr;
}

static HRESULT dxva2_query_count(struct dxva2_umd_runtime *runtime, D3DDDICAPS_TYPE type,
        const void *info, UINT *count)
{
    HRESULT hr;

    *count = 0;
    if (FAILED(hr = dxva2_umd_get_caps(runtime, type, info, count, sizeof(*count))))
        return hr;
    if (*count > DXVA2_MAX_CAP_ENTRIES)
        return E_FAIL;
    return S_OK;
}

HRESULT dxva2_decoder_get_device_guids(IDirect3DDevice9 *device, UINT *count, GUID **guids)
{
    struct dxva2_umd_runtime runtime;
    HRESULT hr;

    if (!count || !guids)
        return E_POINTER;
    *count = 0;
    *guids = NULL;
    if (!device)
        return DXVA2_E_NOT_INITIALIZED;
    if (FAILED(hr = dxva2_umd_runtime_init(device, &runtime)))
        return hr;
    if (FAILED(hr = dxva2_query_count(&runtime, D3DDDICAPS_GETDECODEGUIDCOUNT, NULL, count)) || !*count)
        goto done;
    if (!(*guids = CoTaskMemAlloc((SIZE_T)*count * sizeof(**guids))))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (FAILED(hr = dxva2_umd_get_caps(&runtime, D3DDDICAPS_GETDECODEGUIDS, NULL,
            *guids, *count * sizeof(**guids))))
    {
        CoTaskMemFree(*guids);
        *guids = NULL;
        *count = 0;
    }

done:
    dxva2_umd_runtime_cleanup(&runtime);
    return hr;
}

HRESULT dxva2_decoder_get_render_targets(IDirect3DDevice9 *device, REFGUID guid,
        UINT *count, D3DFORMAT **formats)
{
    struct dxva2_umd_runtime runtime;
    HRESULT hr;

    if (!guid || !count || !formats)
        return E_POINTER;
    *count = 0;
    *formats = NULL;
    if (!device)
        return DXVA2_E_NOT_INITIALIZED;
    if (FAILED(hr = dxva2_umd_runtime_init(device, &runtime)))
        return hr;
    if (FAILED(hr = dxva2_query_count(&runtime, D3DDDICAPS_GETDECODERTFORMATCOUNT, guid, count)) || !*count)
        goto done;
    if (!(*formats = CoTaskMemAlloc((SIZE_T)*count * sizeof(**formats))))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (FAILED(hr = dxva2_umd_get_caps(&runtime, D3DDDICAPS_GETDECODERTFORMATS, guid,
            *formats, *count * sizeof(**formats))))
    {
        CoTaskMemFree(*formats);
        *formats = NULL;
        *count = 0;
    }

done:
    dxva2_umd_runtime_cleanup(&runtime);
    return hr;
}

HRESULT dxva2_decoder_get_configurations(IDirect3DDevice9 *device, REFGUID guid,
        const DXVA2_VideoDesc *video_desc, UINT *count, DXVA2_ConfigPictureDecode **configs)
{
    struct dxva2_umd_runtime runtime;
    DXVADDI_DECODEINPUT input;
    DXVADDI_CONFIGPICTUREDECODE *ddi_configs = NULL;
    UINT i;
    HRESULT hr;

    if (!guid || !video_desc || !count)
        return E_POINTER;
    *count = 0;
    if (configs)
        *configs = NULL;
    if (!device)
        return DXVA2_E_NOT_INITIALIZED;
    if (FAILED(hr = dxva2_umd_runtime_init(device, &runtime)))
        return hr;

    memset(&input, 0, sizeof(input));
    input.pGuid = guid;
    dxva2_video_desc_to_ddi(video_desc, &input.VideoDesc);
    if (FAILED(hr = dxva2_query_count(&runtime,
            D3DDDICAPS_GETDECODECONFIGURATIONCOUNT, &input, count)) ||
            !*count || !configs)
        goto done;
    if (!(ddi_configs = CoTaskMemAlloc((SIZE_T)*count * sizeof(*ddi_configs))) ||
            !(*configs = CoTaskMemAlloc((SIZE_T)*count * sizeof(**configs))))
    {
        hr = E_OUTOFMEMORY;
        goto failed;
    }
    if (FAILED(hr = dxva2_umd_get_caps(&runtime, D3DDDICAPS_GETDECODECONFIGURATIONS, &input,
            ddi_configs, *count * sizeof(*ddi_configs))))
        goto failed;
    for (i = 0; i < *count; ++i)
        dxva2_config_from_ddi(&ddi_configs[i], &(*configs)[i]);
    goto done;

failed:
    CoTaskMemFree(*configs);
    *configs = NULL;
    *count = 0;
done:
    CoTaskMemFree(ddi_configs);
    dxva2_umd_runtime_cleanup(&runtime);
    return hr;
}

static struct dxva2_decoder_buffer *dxva2_decoder_find_buffer(struct dxva2_decoder *decoder, UINT type)
{
    D3DDDIFORMAT format;
    UINT i;

    if (type >= DXVA2_BUFFER_TYPE_COUNT)
        return NULL;
    format = (D3DDDIFORMAT)(D3DDDIFMT_DXVACOMPBUFFER_BASE + type);
    for (i = 0; decoder->buffers && i < decoder->buffer_count; ++i)
    {
        if (decoder->buffers[i].format == format)
            return &decoder->buffers[i];
    }
    return NULL;
}

static HRESULT WINAPI dxva2_decoder_QueryInterface(IDirectXVideoDecoder *iface, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(iid, &IID_IDirectXVideoDecoder) || IsEqualIID(iid, &IID_IUnknown))
    {
        *object = iface;
        IDirectXVideoDecoder_AddRef(iface);
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dxva2_decoder_AddRef(IDirectXVideoDecoder *iface)
{
    struct dxva2_decoder *decoder = impl_from_IDirectXVideoDecoder(iface);
    return InterlockedIncrement(&decoder->refcount);
}

/* decoder->copy_lock must be held. */
static HRESULT dxva2_decoder_hide_overlay(struct dxva2_decoder *decoder)
{
    D3DDDIARG_DESTROYOVERLAY destroy;
    HRESULT hr;

    if (!decoder->overlay)
        return S_OK;
    if (!decoder->runtime.device_funcs.pfnDestroyOverlay)
        return E_NOTIMPL;

    memset(&destroy, 0, sizeof(destroy));
    destroy.hOverlay = decoder->overlay;
    hr = decoder->runtime.device_funcs.pfnDestroyOverlay(
            decoder->runtime.umd_device, &destroy);
    if (SUCCEEDED(hr))
    {
        decoder->overlay = NULL;
        decoder->overlay_surface = UINT_MAX;
        SetRectEmpty(&decoder->overlay_source);
        SetRectEmpty(&decoder->overlay_destination);
        decoder->overlay_flags = 0;
    }
    return hr;
}

static void dxva2_decoder_destroy(struct dxva2_decoder *decoder)
{
    UINT i;

    /* Remove the private binding first so no new D3D9 operation can enter
     * while the decoder's UMD device is being dismantled. */
    for (i = 0; decoder->surfaces && i < decoder->surface_count; ++i)
    {
        if (decoder->surfaces[i].surface &&
                decoder->surfaces[i].fence_attached)
        {
            IDirect3DSurface9_FreePrivateData(decoder->surfaces[i].surface,
                    &reactos_dxva_surface_fence_guid);
            decoder->surfaces[i].fence_attached = FALSE;
        }
    }

    if (decoder->copy_stop_event)
        SetEvent(decoder->copy_stop_event);
    if (decoder->copy_wake_event)
        SetEvent(decoder->copy_wake_event);

    /* Destroying the decode device cancels backend resource waits.  Do this
     * before joining the copy thread, which may be blocked in pfnLock waiting
     * for a decoded render target. */
    if (decoder->decode && decoder->runtime.device_funcs.pfnDestroyDecodeDevice)
        decoder->runtime.device_funcs.pfnDestroyDecodeDevice(decoder->runtime.umd_device, decoder->decode);
    decoder->decode = NULL;

    if (decoder->copy_thread)
    {
        WaitForSingleObject(decoder->copy_thread, INFINITE);
        CloseHandle(decoder->copy_thread);
    }
    decoder->copy_thread = NULL;

    for (i = 0; decoder->surfaces && i < decoder->surface_count; ++i)
    {
        if (decoder->surfaces[i].fence)
            dxva2_surface_fence_detach(decoder->surfaces[i].fence);
    }

    EnterCriticalSection(&decoder->copy_lock);
    (void)dxva2_decoder_hide_overlay(decoder);
    for (i = 0; decoder->surfaces && i < decoder->surface_count; ++i)
    {
        HRESULT hr = dxva2_decoder_unlock_shared_surface(decoder, i);

        if (FAILED(hr))
            WARN("Failed to release shared decoded surface %u, hr %#lx.\n", i, hr);
    }
    LeaveCriticalSection(&decoder->copy_lock);

    for (i = 0; decoder->buffers && i < decoder->buffer_count; ++i)
    {
        if (decoder->buffers[i].locked)
            dxva2_umd_unlock_resource(&decoder->runtime, decoder->buffers[i].resource);
    }

    if (decoder->runtime.device_funcs.pfnDestroyResource)
    {
        for (i = 0; decoder->buffers && i < decoder->buffer_count; ++i)
        {
            if (decoder->buffers[i].resource)
                decoder->runtime.device_funcs.pfnDestroyResource(decoder->runtime.umd_device,
                        decoder->buffers[i].resource);
        }
        for (i = 0; decoder->surfaces && i < decoder->surface_count; ++i)
        {
            if (decoder->surfaces[i].resource)
                decoder->runtime.device_funcs.pfnDestroyResource(decoder->runtime.umd_device,
                        decoder->surfaces[i].resource);
        }
    }
    dxva2_umd_runtime_cleanup(&decoder->runtime);

    for (i = 0; decoder->surfaces && i < decoder->surface_count; ++i)
    {
        if (decoder->surfaces[i].fence)
        {
            IReactOSDxvaSurfaceFence_Release(
                    &decoder->surfaces[i].fence->IReactOSDxvaSurfaceFence_iface);
        }
        if (decoder->surfaces[i].surface)
            IDirect3DSurface9_Release(decoder->surfaces[i].surface);
    }
    if (decoder->copy_stop_event)
        CloseHandle(decoder->copy_stop_event);
    if (decoder->copy_wake_event)
        CloseHandle(decoder->copy_wake_event);
    DeleteCriticalSection(&decoder->copy_lock);
    free(decoder->buffers);
    free(decoder->surfaces);
    if (decoder->device)
        IDirect3DDevice9_Release(decoder->device);
    if (decoder->service)
        IDirectXVideoDecoderService_Release(decoder->service);
    free(decoder);
}

static ULONG WINAPI dxva2_decoder_Release(IDirectXVideoDecoder *iface)
{
    struct dxva2_decoder *decoder = impl_from_IDirectXVideoDecoder(iface);
    ULONG refcount = InterlockedDecrement(&decoder->refcount);

    if (!refcount)
        dxva2_decoder_destroy(decoder);
    return refcount;
}

static HRESULT WINAPI dxva2_decoder_GetVideoDecoderService(IDirectXVideoDecoder *iface,
        IDirectXVideoDecoderService **service)
{
    struct dxva2_decoder *decoder = impl_from_IDirectXVideoDecoder(iface);

    if (!service)
        return E_POINTER;
    *service = decoder->service;
    IDirectXVideoDecoderService_AddRef(*service);
    return S_OK;
}

static HRESULT WINAPI dxva2_decoder_GetCreationParameters(IDirectXVideoDecoder *iface, GUID *guid,
        DXVA2_VideoDesc *video_desc, DXVA2_ConfigPictureDecode *config,
        IDirect3DSurface9 ***render_targets, UINT *surface_count)
{
    struct dxva2_decoder *decoder = impl_from_IDirectXVideoDecoder(iface);
    IDirect3DSurface9 **surfaces;
    UINT i;

    if (!guid || !video_desc || !config || !render_targets || !surface_count)
        return E_POINTER;
    *render_targets = NULL;
    *surface_count = 0;
    if (!(surfaces = CoTaskMemAlloc((SIZE_T)decoder->surface_count * sizeof(*surfaces))))
        return E_OUTOFMEMORY;
    for (i = 0; i < decoder->surface_count; ++i)
    {
        surfaces[i] = decoder->surfaces[i].surface;
        IDirect3DSurface9_AddRef(surfaces[i]);
    }
    *guid = decoder->guid;
    *video_desc = decoder->video_desc;
    *config = decoder->config;
    *render_targets = surfaces;
    *surface_count = decoder->surface_count;
    return S_OK;
}

static HRESULT WINAPI dxva2_decoder_GetBuffer(IDirectXVideoDecoder *iface, UINT type,
        void **data, UINT *size)
{
    struct dxva2_decoder *decoder = impl_from_IDirectXVideoDecoder(iface);
    struct dxva2_decoder_buffer *buffer;
    D3DDDIARG_LOCK lock;
    HRESULT hr;

    if (!data || !size)
        return E_POINTER;
    *data = NULL;
    *size = 0;
    if (!decoder->frame_begun || !(buffer = dxva2_decoder_find_buffer(decoder, type)))
        return E_INVALIDARG;
    if (buffer->locked)
        return D3DERR_WASSTILLDRAWING;
    if (FAILED(hr = dxva2_umd_lock_resource(&decoder->runtime, buffer->resource, FALSE, &lock)))
        return hr;
    if (!lock.pSurfData || !lock.SlicePitch)
    {
        dxva2_umd_unlock_resource(&decoder->runtime, buffer->resource);
        return E_FAIL;
    }
    buffer->locked = TRUE;
    buffer->size = lock.SlicePitch;
    *data = lock.pSurfData;
    *size = lock.SlicePitch;
    return S_OK;
}

static HRESULT WINAPI dxva2_decoder_ReleaseBuffer(IDirectXVideoDecoder *iface, UINT type)
{
    struct dxva2_decoder *decoder = impl_from_IDirectXVideoDecoder(iface);
    struct dxva2_decoder_buffer *buffer;
    HRESULT hr;

    if (!(buffer = dxva2_decoder_find_buffer(decoder, type)) || !buffer->locked)
        return E_INVALIDARG;
    hr = dxva2_umd_unlock_resource(&decoder->runtime, buffer->resource);
    if (SUCCEEDED(hr))
        buffer->locked = FALSE;
    return hr;
}

static HRESULT WINAPI dxva2_decoder_BeginFrame(IDirectXVideoDecoder *iface,
        IDirect3DSurface9 *render_target, void *pvp_data)
{
    struct dxva2_decoder *decoder = impl_from_IDirectXVideoDecoder(iface);
    D3DDDIARG_SETDECODERENDERTARGET set_target;
    D3DDDIARG_DECODEBEGINFRAME begin;
    UINT i;
    HRESULT hr;

    if (!render_target || decoder->frame_begun)
        return E_INVALIDARG;
    for (i = 0; i < decoder->surface_count; ++i)
    {
        if (decoder->surfaces[i].surface == render_target)
            break;
    }
    if (i == decoder->surface_count)
        return E_INVALIDARG;
    if (InterlockedCompareExchange(&decoder->surfaces[i].fence->pending, 0, 0))
        return E_PENDING;

    EnterCriticalSection(&decoder->copy_lock);
    if (decoder->overlay && decoder->overlay_surface == i)
        hr = dxva2_decoder_hide_overlay(decoder);
    else
        hr = S_OK;
    if (SUCCEEDED(hr))
        hr = dxva2_decoder_unlock_shared_surface(decoder, i);
    LeaveCriticalSection(&decoder->copy_lock);
    if (FAILED(hr))
        return hr;

    memset(&set_target, 0, sizeof(set_target));
    set_target.hDecode = decoder->decode;
    set_target.hRenderTarget = decoder->surfaces[i].resource;
    if (FAILED(hr = decoder->runtime.device_funcs.pfnSetDecodeRenderTarget(
            decoder->runtime.umd_device, &set_target)))
        return hr;

    memset(&begin, 0, sizeof(begin));
    begin.hDecode = decoder->decode;
    begin.pPVPSetKey = pvp_data;
    if (FAILED(hr = decoder->runtime.device_funcs.pfnDecodeBeginFrame(decoder->runtime.umd_device, &begin)))
        return hr;
    decoder->current_surface = i;
    decoder->frame_begun = TRUE;
    return S_OK;
}

static HRESULT dxva2_copy_nv12_rows(void *destination, UINT destination_pitch,
        const void *source, UINT source_pitch, UINT source_storage_height,
        UINT width, UINT height)
{
    BYTE *destination_row;
    const BYTE *source_row;
    UINT i;

    if (!destination || !source || destination_pitch < width || source_pitch < width ||
            source_storage_height < height || (height & 1) || (source_storage_height & 1))
        return E_INVALIDARG;

    destination_row = destination;
    source_row = source;
    for (i = 0; i < height; ++i)
    {
        memcpy(destination_row, source_row, width);
        destination_row += destination_pitch;
        source_row += source_pitch;
    }

    destination_row = (BYTE *)destination + (SIZE_T)destination_pitch * height;
    source_row = (const BYTE *)source + (SIZE_T)source_pitch * source_storage_height;
    for (i = 0; i < height / 2; ++i)
    {
        memcpy(destination_row, source_row, width);
        destination_row += destination_pitch;
        source_row += source_pitch;
    }
    return S_OK;
}

static HRESULT dxva2_decoder_copy_output(struct dxva2_decoder *decoder, UINT surface_index)
{
    struct dxva2_decoder_surface *surface = &decoder->surfaces[surface_index];
    IDirect3DSurface9 *target = decoder->surfaces[surface_index].surface;
    IDirect3DSurface9 *staging = NULL;
    D3DDDIARG_LOCK source_lock;
    D3DLOCKED_RECT target_lock;
    POINT point = {0, 0};
    BOOL unlock_source = FALSE;
    UINT source_storage_height;
    UINT source_rows;
    HRESULT unlock_hr;
    HRESULT hr;

    if (surface->shared_locked)
    {
        source_lock = surface->shared_lock;
    }
    else
    {
        if (FAILED(hr = dxva2_umd_lock_resource(&decoder->runtime,
                surface->resource, TRUE, &source_lock)))
            return hr;
        unlock_source = TRUE;
    }
    if (!source_lock.pSurfData || source_lock.Pitch < decoder->video_desc.SampleWidth ||
            !source_lock.SlicePitch || source_lock.SlicePitch % source_lock.Pitch)
    {
        hr = E_FAIL;
        goto done;
    }
    source_rows = source_lock.SlicePitch / source_lock.Pitch;
    if ((source_rows * 2) % 3)
    {
        hr = E_FAIL;
        goto done;
    }
    source_storage_height = source_rows * 2 / 3;

    memset(&target_lock, 0, sizeof(target_lock));
    hr = IDirect3DSurface9_LockRect(target, &target_lock, NULL, 0);
    if (SUCCEEDED(hr))
    {
        if (target_lock.Pitch <= 0)
        {
            IDirect3DSurface9_UnlockRect(target);
            hr = E_FAIL;
            goto done;
        }
        hr = dxva2_copy_nv12_rows(target_lock.pBits, target_lock.Pitch,
                source_lock.pSurfData, source_lock.Pitch, source_storage_height,
                decoder->video_desc.SampleWidth, decoder->video_desc.SampleHeight);
        IDirect3DSurface9_UnlockRect(target);
        goto done;
    }

    if (FAILED(hr = IDirect3DDevice9_CreateOffscreenPlainSurface(decoder->device,
            decoder->video_desc.SampleWidth, decoder->video_desc.SampleHeight,
            decoder->video_desc.Format, D3DPOOL_SYSTEMMEM, &staging, NULL)))
        goto done;
    if (FAILED(hr = IDirect3DSurface9_LockRect(staging, &target_lock, NULL, 0)))
        goto done;
    if (target_lock.Pitch <= 0)
    {
        IDirect3DSurface9_UnlockRect(staging);
        hr = E_FAIL;
        goto done;
    }
    hr = dxva2_copy_nv12_rows(target_lock.pBits, target_lock.Pitch,
            source_lock.pSurfData, source_lock.Pitch, source_storage_height,
            decoder->video_desc.SampleWidth, decoder->video_desc.SampleHeight);
    IDirect3DSurface9_UnlockRect(staging);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice9_UpdateSurface(decoder->device, staging, NULL, target, &point);

done:
    if (staging)
        IDirect3DSurface9_Release(staging);
    unlock_hr = unlock_source
            ? dxva2_umd_unlock_resource(&decoder->runtime, surface->resource)
            : S_OK;
    return FAILED(hr) ? hr : unlock_hr;
}

static HRESULT dxva2_decoder_wait_output(struct dxva2_decoder *decoder,
        UINT surface_index)
{
    D3DDDIARG_LOCK lock;
    HRESULT unlock_hr;
    HRESULT hr;

    hr = dxva2_umd_lock_resource(&decoder->runtime,
            decoder->surfaces[surface_index].resource, TRUE, &lock);
    if (FAILED(hr))
        return hr;
    unlock_hr = dxva2_umd_unlock_resource(&decoder->runtime,
            decoder->surfaces[surface_index].resource);
    return FAILED(hr) ? hr : unlock_hr;
}

static DWORD WINAPI dxva2_decoder_copy_thread(void *context)
{
    struct dxva2_decoder *decoder = context;
    HANDLE wait_handles[2];
    UINT i;

    wait_handles[0] = decoder->copy_wake_event;
    wait_handles[1] = decoder->copy_stop_event;
    for (;;)
    {
        DWORD wait_status;

        wait_status = WaitForMultipleObjects(ARRAY_SIZE(wait_handles), wait_handles,
                FALSE, INFINITE);
        if (wait_status == WAIT_OBJECT_0 + 1)
            break;
        if (wait_status != WAIT_OBJECT_0)
            break;

        for (;;)
        {
            HRESULT hr;

            EnterCriticalSection(&decoder->copy_lock);
            for (i = 0; i < decoder->surface_count; ++i)
            {
                if (InterlockedCompareExchange(&decoder->surfaces[i].fence->pending, 0, 0))
                    break;
            }
            if (i == decoder->surface_count)
                ResetEvent(decoder->copy_wake_event);
            LeaveCriticalSection(&decoder->copy_lock);
            if (i == decoder->surface_count)
                break;

            hr = dxva2_decoder_wait_output(decoder, i);
            if (WaitForSingleObject(decoder->copy_stop_event, 0) == WAIT_OBJECT_0)
                hr = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
            dxva2_surface_fence_complete(decoder->surfaces[i].fence, hr);
            if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED))
                WARN("Failed to complete decoded surface %u, hr %#lx.\n", i, hr);
        }
    }

    for (i = 0; i < decoder->surface_count; ++i)
    {
        if (InterlockedCompareExchange(&decoder->surfaces[i].fence->pending, 0, 0))
        {
            dxva2_surface_fence_complete(decoder->surfaces[i].fence,
                    HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED));
        }
    }
    return 0;
}

static HRESULT WINAPI dxva2_decoder_EndFrame(IDirectXVideoDecoder *iface, HANDLE *completion_handle)
{
    struct dxva2_decoder *decoder = impl_from_IDirectXVideoDecoder(iface);
    D3DDDIARG_DECODEENDFRAME end;
    UINT i;
    HRESULT hr;

    if (!decoder->frame_begun)
        return E_INVALIDARG;
    for (i = 0; i < decoder->buffer_count; ++i)
    {
        if (decoder->buffers[i].locked)
            return E_INVALIDARG;
    }
    if (completion_handle)
        *completion_handle = NULL;

    EnterCriticalSection(&decoder->copy_lock);
    if (InterlockedCompareExchange(
            &decoder->surfaces[decoder->current_surface].fence->pending, 0, 0))
    {
        LeaveCriticalSection(&decoder->copy_lock);
        return E_PENDING;
    }
    if (++decoder->surfaces[decoder->current_surface].generation == 0)
        decoder->surfaces[decoder->current_surface].generation = 1;
    dxva2_surface_fence_submit(decoder->surfaces[decoder->current_surface].fence);
    LeaveCriticalSection(&decoder->copy_lock);

    memset(&end, 0, sizeof(end));
    end.hDecode = decoder->decode;
    end.pHandleComplete = NULL;
    hr = decoder->runtime.device_funcs.pfnDecodeEndFrame(decoder->runtime.umd_device, &end);
    decoder->frame_begun = FALSE;
    if (SUCCEEDED(hr))
        SetEvent(decoder->copy_wake_event);
    else
        dxva2_surface_fence_complete(decoder->surfaces[decoder->current_surface].fence, hr);
    return hr;
}

static HRESULT WINAPI dxva2_decoder_Execute(IDirectXVideoDecoder *iface,
        const DXVA2_DecodeExecuteParams *params)
{
    struct dxva2_decoder *decoder = impl_from_IDirectXVideoDecoder(iface);
    DXVADDI_DECODEBUFFERDESC *buffers = NULL;
    DXVADDI_PRIVATEDATA private_input;
    DXVADDI_PRIVATEDATA private_output;
    D3DDDIARG_DECODEEXTENSIONEXECUTE extension;
    D3DDDIARG_DECODEEXECUTE execute;
    struct dxva2_decoder_buffer *buffer;
    DXVA2_DecodeBufferDesc *source;
    UINT i;
    HRESULT hr;

    if (!params || !decoder->frame_begun ||
            (params->NumCompBuffers && !params->pCompressedBuffers) ||
            params->NumCompBuffers > decoder->buffer_count)
        return E_INVALIDARG;
    if (params->NumCompBuffers && !(buffers = calloc(params->NumCompBuffers, sizeof(*buffers))))
        return E_OUTOFMEMORY;

    for (i = 0; i < params->NumCompBuffers; ++i)
    {
        source = &params->pCompressedBuffers[i];
        buffer = dxva2_decoder_find_buffer(decoder, source->CompressedBufferType);
        if (!buffer || buffer->locked || source->BufferIndex ||
                source->DataOffset > buffer->size ||
                source->DataSize > buffer->size - source->DataOffset)
        {
            hr = E_INVALIDARG;
            goto done;
        }
        buffers[i].hBuffer = buffer->resource;
        buffers[i].CompressedBufferType = buffer->format;
        buffers[i].DataOffset = source->DataOffset;
        buffers[i].DataSize = source->DataSize;
        buffers[i].FirstMBaddress = source->FirstMBaddress;
        buffers[i].NumMBsInBuffer = source->NumMBsInBuffer;
        buffers[i].Width = source->Width;
        buffers[i].Height = source->Height;
        buffers[i].Stride = source->Stride;
        buffers[i].ReservedBits = source->ReservedBits;
        buffers[i].pCipherCounter = source->pvPVPState;
    }

    memset(&execute, 0, sizeof(execute));
    execute.hDecode = decoder->decode;
    execute.NumCompBuffers = params->NumCompBuffers;
    execute.pCompressedBuffers = buffers;
    if (FAILED(hr = decoder->runtime.device_funcs.pfnDecodeExecute(decoder->runtime.umd_device, &execute)))
        goto done;

    if (params->pExtensionData)
    {
        if (!decoder->runtime.device_funcs.pfnDecodeExtensionExecute)
        {
            hr = E_NOTIMPL;
            goto done;
        }
        memset(&extension, 0, sizeof(extension));
        memset(&private_input, 0, sizeof(private_input));
        memset(&private_output, 0, sizeof(private_output));
        private_input.pData = params->pExtensionData->pPrivateInputData;
        private_input.DataSize = params->pExtensionData->PrivateInputDataSize;
        private_output.pData = params->pExtensionData->pPrivateOutputData;
        private_output.DataSize = params->pExtensionData->PrivateOutputDataSize;
        extension.hDecode = decoder->decode;
        extension.Function = params->pExtensionData->Function;
        extension.pPrivateInput = private_input.pData || private_input.DataSize ? &private_input : NULL;
        extension.pPrivateOutput = private_output.pData || private_output.DataSize ? &private_output : NULL;
        hr = decoder->runtime.device_funcs.pfnDecodeExtensionExecute(decoder->runtime.umd_device, &extension);
    }

done:
    free(buffers);
    return hr;
}

static const IDirectXVideoDecoderVtbl dxva2_decoder_vtbl =
{
    dxva2_decoder_QueryInterface,
    dxva2_decoder_AddRef,
    dxva2_decoder_Release,
    dxva2_decoder_GetVideoDecoderService,
    dxva2_decoder_GetCreationParameters,
    dxva2_decoder_GetBuffer,
    dxva2_decoder_ReleaseBuffer,
    dxva2_decoder_BeginFrame,
    dxva2_decoder_EndFrame,
    dxva2_decoder_Execute,
};

HRESULT dxva2_decoder_create(IDirectXVideoDecoderService *service, IDirect3DDevice9 *device,
        REFGUID guid, const DXVA2_VideoDesc *video_desc, const DXVA2_ConfigPictureDecode *config,
        IDirect3DSurface9 **render_targets, UINT surface_count, IDirectXVideoDecoder **decoder_out)
{
    struct dxva2_decoder *decoder = NULL;
    DXVADDI_DECODEBUFFERINFO *buffer_info = NULL;
    DXVADDI_DECODEINPUT input;
    DXVADDI_CONFIGPICTUREDECODE ddi_config;
    D3DDDIARG_CREATEDECODEDEVICE create_decode;
    D3DDDI_RESOURCEFLAGS flags;
    D3DSURFACE_DESC surface_desc;
    UINT i, j;
    HRESULT hr;

    if (!decoder_out)
        return E_POINTER;
    *decoder_out = NULL;
    if (!service || !device || !guid || !video_desc || !config || !render_targets || !surface_count ||
            surface_count > DXVA2_MAX_CAP_ENTRIES || !video_desc->SampleWidth || !video_desc->SampleHeight)
        return E_INVALIDARG;
    if (config->ConfigMinRenderTargetBuffCount > surface_count)
        return E_INVALIDARG;
    if (FAILED(hr = dxva2_config_to_ddi(config, &ddi_config)))
        return hr;

    if (!(decoder = calloc(1, sizeof(*decoder))))
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&decoder->copy_lock);
    decoder->IDirectXVideoDecoder_iface.lpVtbl = &dxva2_decoder_vtbl;
    decoder->refcount = 1;
    decoder->guid = *guid;
    decoder->video_desc = *video_desc;
    decoder->config = *config;
    decoder->surface_count = surface_count;
    decoder->current_surface = UINT_MAX;
    decoder->overlay_surface = UINT_MAX;
    decoder->copy_wake_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    decoder->copy_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!decoder->copy_wake_event || !decoder->copy_stop_event)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto failed;
    }
    if (!(decoder->surfaces = calloc(surface_count, sizeof(*decoder->surfaces))))
    {
        hr = E_OUTOFMEMORY;
        goto failed;
    }

    for (i = 0; i < surface_count; ++i)
    {
        if (!render_targets[i] || FAILED(hr = IDirect3DSurface9_GetDesc(render_targets[i], &surface_desc)))
            goto failed;
        if (surface_desc.Format != video_desc->Format || surface_desc.Width < video_desc->SampleWidth ||
                surface_desc.Height < video_desc->SampleHeight)
        {
            hr = E_INVALIDARG;
            goto failed;
        }
        decoder->surfaces[i].surface = render_targets[i];
        IDirect3DSurface9_AddRef(render_targets[i]);
        if (!(decoder->surfaces[i].fence =
                dxva2_surface_fence_create(decoder, i)))
        {
            hr = E_OUTOFMEMORY;
            goto failed;
        }
    }

    decoder->service = service;
    IDirectXVideoDecoderService_AddRef(service);
    decoder->device = device;
    IDirect3DDevice9_AddRef(device);
    if (FAILED(hr = dxva2_umd_runtime_init(device, &decoder->runtime)))
        goto failed;
    if (!decoder->runtime.device_funcs.pfnCreateResource ||
            !decoder->runtime.device_funcs.pfnDestroyResource ||
            !decoder->runtime.device_funcs.pfnLock || !decoder->runtime.device_funcs.pfnUnlock ||
            !decoder->runtime.device_funcs.pfnCreateDecodeDevice ||
            !decoder->runtime.device_funcs.pfnDestroyDecodeDevice ||
            !decoder->runtime.device_funcs.pfnSetDecodeRenderTarget ||
            !decoder->runtime.device_funcs.pfnDecodeBeginFrame ||
            !decoder->runtime.device_funcs.pfnDecodeEndFrame ||
            !decoder->runtime.device_funcs.pfnDecodeExecute)
    {
        hr = DXVA2_E_NOT_AVAILABLE;
        goto failed;
    }

    memset(&flags, 0, sizeof(flags));
    flags.Video = 1;
    flags.DecodeRenderTarget = 1;
    for (i = 0; i < surface_count; ++i)
    {
        if (FAILED(hr = dxva2_umd_create_resource(&decoder->runtime, (D3DDDIFORMAT)video_desc->Format,
                video_desc->SampleWidth, video_desc->SampleHeight, D3DDDIPOOL_VIDEOMEMORY,
                flags, &decoder->surfaces[i], &decoder->surfaces[i].resource)))
            goto failed;
    }

    memset(&input, 0, sizeof(input));
    input.pGuid = guid;
    dxva2_video_desc_to_ddi(video_desc, &input.VideoDesc);
    if (FAILED(hr = dxva2_query_count(&decoder->runtime,
            D3DDDICAPS_GETDECODECOMPRESSEDBUFFERINFOCOUNT, &input, &decoder->buffer_count)) ||
            !decoder->buffer_count)
        goto failed;
    if (!(buffer_info = calloc(decoder->buffer_count, sizeof(*buffer_info))) ||
            !(decoder->buffers = calloc(decoder->buffer_count, sizeof(*decoder->buffers))))
    {
        hr = E_OUTOFMEMORY;
        goto failed;
    }
    if (FAILED(hr = dxva2_umd_get_caps(&decoder->runtime,
            D3DDDICAPS_GETDECODECOMPRESSEDBUFFERINFO, &input, buffer_info,
            decoder->buffer_count * sizeof(*buffer_info))))
        goto failed;

    memset(&flags, 0, sizeof(flags));
    flags.Video = 1;
    flags.DecodeCompressedBuffer = 1;
    for (i = 0; i < decoder->buffer_count; ++i)
    {
        if (buffer_info[i].CompressedBufferType < D3DDDIFMT_DXVACOMPBUFFER_BASE ||
                buffer_info[i].CompressedBufferType >= D3DDDIFMT_DXVACOMPBUFFER_BASE + DXVA2_BUFFER_TYPE_COUNT ||
                !buffer_info[i].CreationWidth || !buffer_info[i].CreationHeight)
        {
            hr = E_INVALIDARG;
            goto failed;
        }
        for (j = 0; j < i; ++j)
        {
            if (decoder->buffers[j].format == buffer_info[i].CompressedBufferType)
            {
                hr = E_INVALIDARG;
                goto failed;
            }
        }
        decoder->buffers[i].format = buffer_info[i].CompressedBufferType;
        if (FAILED(hr = dxva2_umd_create_resource(&decoder->runtime, buffer_info[i].CompressedBufferType,
                buffer_info[i].CreationWidth, buffer_info[i].CreationHeight, buffer_info[i].CreationPool,
                flags, &decoder->buffers[i], &decoder->buffers[i].resource)))
            goto failed;
        if (buffer_info[i].CreationWidth > UINT_MAX / buffer_info[i].CreationHeight)
        {
            hr = E_OUTOFMEMORY;
            goto failed;
        }
        decoder->buffers[i].size = buffer_info[i].CreationWidth * buffer_info[i].CreationHeight;
    }

    memset(&create_decode, 0, sizeof(create_decode));
    create_decode.pGuid = &decoder->guid;
    dxva2_video_desc_to_ddi(video_desc, &create_decode.VideoDesc);
    create_decode.pConfig = &ddi_config;
    if (FAILED(hr = decoder->runtime.device_funcs.pfnCreateDecodeDevice(
            decoder->runtime.umd_device, &create_decode)))
        goto failed;
    decoder->decode = create_decode.hDecode;
    if (!decoder->decode)
    {
        hr = E_FAIL;
        goto failed;
    }

    decoder->copy_thread = CreateThread(NULL, 0, dxva2_decoder_copy_thread,
            decoder, 0, NULL);
    if (!decoder->copy_thread)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto failed;
    }

    for (i = 0; i < surface_count; ++i)
    {
        if (FAILED(hr = IDirect3DSurface9_SetPrivateData(render_targets[i],
                &reactos_dxva_surface_fence_guid,
                &decoder->surfaces[i].fence->IReactOSDxvaSurfaceFence_iface,
                sizeof(IUnknown *), D3DSPD_IUNKNOWN)))
            goto failed;
        decoder->surfaces[i].fence_attached = TRUE;
    }

    free(buffer_info);
    *decoder_out = &decoder->IDirectXVideoDecoder_iface;
    return S_OK;

failed:
    free(buffer_info);
    dxva2_decoder_destroy(decoder);
    return hr;
}

/* EOF */
