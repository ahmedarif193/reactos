/*
 * Copyright 2020 Esme Povirk
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "ole2.h"

#include "wincodecs_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wincodecs);

static const PROPBAG2 encoder_option_properties[ENCODER_OPTION_END] = {
    { PROPBAG2_TYPE_DATA, VT_BOOL, 0, 0, (LPOLESTR)L"InterlaceOption" },
    { PROPBAG2_TYPE_DATA, VT_UI1,  0, 0, (LPOLESTR)L"FilterOption" },
    { PROPBAG2_TYPE_DATA, VT_UI1,  0, 0, (LPOLESTR)L"TiffCompressionMethod" },
    { PROPBAG2_TYPE_DATA, VT_R4,   0, 0, (LPOLESTR)L"CompressionQuality" },
    { PROPBAG2_TYPE_DATA, VT_R4,            0, 0, (LPOLESTR)L"ImageQuality" },
    { PROPBAG2_TYPE_DATA, VT_UI1,           0, 0, (LPOLESTR)L"BitmapTransform" },
    { PROPBAG2_TYPE_DATA, VT_I4 | VT_ARRAY, 0, 0, (LPOLESTR)L"Luminance" },
    { PROPBAG2_TYPE_DATA, VT_I4 | VT_ARRAY, 0, 0, (LPOLESTR)L"Chrominance" },
    { PROPBAG2_TYPE_DATA, VT_UI1,           0, 0, (LPOLESTR)L"JpegYCrCbSubsampling" },
    { PROPBAG2_TYPE_DATA, VT_BOOL,          0, 0, (LPOLESTR)L"SuppressApp0" },
    { PROPBAG2_TYPE_DATA, VT_BOOL,          0, 0, (LPOLESTR)L"Lossless" }
};

typedef struct CommonEncoder {
    IWICBitmapEncoder IWICBitmapEncoder_iface;
    LONG ref;
    CRITICAL_SECTION lock; /* must be held when stream or encoder is accessed */
    IStream *stream;
    struct encoder *encoder;
    struct encoder_info encoder_info;
    UINT frame_count;
    BOOL uncommitted_frame;
    BOOL committed;
} CommonEncoder;

typedef struct CommonEncoderFrame {
    IWICBitmapFrameEncode IWICBitmapFrameEncode_iface;
    IWICMetadataBlockWriter IWICMetadataBlockWriter_iface;
    LONG ref;
    CommonEncoder *parent;
    struct encoder_frame encoder_frame;
    BOOL initialized;
    BOOL frame_created;
    UINT lines_written;
    BOOL committed;
    BOOL committing;
    IWICMetadataWriter **metadata;
    UINT metadata_count;
    size_t metadata_capacity;
    ULONGLONG metadata_revision;
} CommonEncoderFrame;

static inline CommonEncoder *impl_from_IWICBitmapEncoder(IWICBitmapEncoder *iface)
{
    return CONTAINING_RECORD(iface, CommonEncoder, IWICBitmapEncoder_iface);
}

static inline CommonEncoderFrame *impl_from_IWICBitmapFrameEncode(IWICBitmapFrameEncode *iface)
{
    return CONTAINING_RECORD(iface, CommonEncoderFrame, IWICBitmapFrameEncode_iface);
}

static inline CommonEncoderFrame *impl_from_IWICMetadataBlockWriter(IWICMetadataBlockWriter *iface)
{
    return CONTAINING_RECORD(iface, CommonEncoderFrame, IWICMetadataBlockWriter_iface);
}

static HRESULT WINAPI CommonEncoderFrame_QueryInterface(IWICBitmapFrameEncode *iface, REFIID iid,
    void **ppv)
{
    CommonEncoderFrame *object = impl_from_IWICBitmapFrameEncode(iface);
    TRACE("(%p,%s,%p)\n", iface, debugstr_guid(iid), ppv);

    if (!ppv) return E_INVALIDARG;

    if (IsEqualIID(&IID_IUnknown, iid) ||
        IsEqualIID(&IID_IWICBitmapFrameEncode, iid))
    {
        *ppv = &object->IWICBitmapFrameEncode_iface;
    }
    else if (object->parent->encoder_info.flags & ENCODER_FLAGS_SUPPORTS_METADATA
            && IsEqualIID(&IID_IWICMetadataBlockWriter, iid))
    {
        *ppv = &object->IWICMetadataBlockWriter_iface;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI CommonEncoderFrame_AddRef(IWICBitmapFrameEncode *iface)
{
    CommonEncoderFrame *This = impl_from_IWICBitmapFrameEncode(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI CommonEncoderFrame_Release(IWICBitmapFrameEncode *iface)
{
    CommonEncoderFrame *This = impl_from_IWICBitmapFrameEncode(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);

    if (ref == 0)
    {
        UINT i;
        for (i = 0; i < This->metadata_count; ++i) IWICMetadataWriter_Release(This->metadata[i]);
        free(This->metadata);
        IWICBitmapEncoder_Release(&This->parent->IWICBitmapEncoder_iface);
        free(This);
    }

    return ref;
}

static HRESULT initialize_jpeg_metadata(CommonEncoderFrame *frame)
{
    IWICMetadataWriter *writer;
    PROPVARIANT id, value;
    HRESULT hr;
    UINT i;

    hr = App0MetadataWriter_CreateInstance(&IID_IWICMetadataWriter, (void **)&writer);
    if (FAILED(hr)) return hr;
    id.vt = VT_UI2;
    value.vt = VT_UI2;
    for (i = 0; i < 4; ++i)
    {
        if (i == 1) continue;
        id.uiVal = i;
        value.uiVal = i ? 1 : 0x101;
        hr = IWICMetadataWriter_SetValue(writer, NULL, &id, &value);
        if (FAILED(hr)) break;
    }
    if (SUCCEEDED(hr) && !wincodecs_array_reserve((void **)&frame->metadata,
            &frame->metadata_capacity, 1, sizeof(*frame->metadata))) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) frame->metadata[frame->metadata_count++] = writer;
    else IWICMetadataWriter_Release(writer);
    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_Initialize(IWICBitmapFrameEncode *iface,
    IPropertyBag2 *pIEncoderOptions)
{
    CommonEncoderFrame *This = impl_from_IWICBitmapFrameEncode(iface);
    HRESULT hr=S_OK;
    struct encoder_frame options = {{0}};
    PROPBAG2 opts[7]= {{0}};
    VARIANT opt_values[7];
    HRESULT opt_hres[7];
    DWORD num_opts, i;
    BOOL suppress_app0 = FALSE;

    TRACE("(%p,%p)\n", iface, pIEncoderOptions);

    if (pIEncoderOptions)
    {
        for (i=0; This->parent->encoder_info.encoder_options[i] != ENCODER_OPTION_END; i++)
            opts[i] = encoder_option_properties[This->parent->encoder_info.encoder_options[i]];
        num_opts = i;

        hr = IPropertyBag2_Read(pIEncoderOptions, num_opts, opts, NULL, opt_values, opt_hres);

        if (FAILED(hr))
            return hr;

        for (i=0; This->parent->encoder_info.encoder_options[i] != ENCODER_OPTION_END; i++)
        {
            VARIANT *val = &opt_values[i];

            switch (This->parent->encoder_info.encoder_options[i])
            {
            case ENCODER_OPTION_INTERLACE:
                if (V_VT(val) == VT_EMPTY)
                    options.interlace = FALSE;
                else
                    options.interlace = (V_BOOL(val) != 0);
                break;
            case ENCODER_OPTION_FILTER:
                options.filter = V_UI1(val);
                if (options.filter > WICPngFilterAdaptive)
                {
                    WARN("Unrecognized filter option value %lu.\n", options.filter);
                    options.filter = WICPngFilterUnspecified;
                }
                break;
            case ENCODER_OPTION_LOSSLESS:
                options.lossless = V_VT(val) == VT_BOOL && V_BOOL(val) != VARIANT_FALSE;
                break;
            case ENCODER_OPTION_SUPPRESS_APP0:
                suppress_app0 = V_VT(val) == VT_BOOL && V_BOOL(val) != VARIANT_FALSE;
                break;
            default:
                break;
            }
        }
    }
    else
    {
        options.interlace = FALSE;
        options.filter = WICPngFilterUnspecified;
    }

    EnterCriticalSection(&This->parent->lock);

    if (This->initialized)
        hr = WINCODEC_ERR_WRONGSTATE;
    else
    {
        This->encoder_frame = options;
        if (!suppress_app0 && IsEqualGUID(&This->parent->encoder_info.container_format, &GUID_ContainerFormatJpeg))
            hr = initialize_jpeg_metadata(This);
        if (SUCCEEDED(hr)) This->initialized = TRUE;
    }

    LeaveCriticalSection(&This->parent->lock);

    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_SetSize(IWICBitmapFrameEncode *iface,
    UINT uiWidth, UINT uiHeight)
{
    CommonEncoderFrame *This = impl_from_IWICBitmapFrameEncode(iface);
    HRESULT hr;

    TRACE("(%p,%u,%u)\n", iface, uiWidth, uiHeight);

    EnterCriticalSection(&This->parent->lock);

    if (This->parent->encoder_info.flags & ENCODER_FLAGS_ICNS_SIZE)
    {
        if (uiWidth != uiHeight)
        {
            WARN("cannot generate ICNS icon from %dx%d image\n", uiWidth, uiHeight);
            hr = E_INVALIDARG;
            goto end;
        }

        switch (uiWidth)
        {
            case 16:
            case 32:
            case 48:
            case 128:
            case 256:
            case 512:
                break;
            default:
                WARN("cannot generate ICNS icon from %dx%d image\n", uiWidth, uiHeight);
                hr = E_INVALIDARG;
                goto end;
        }
    }

    if (!This->initialized || This->frame_created)
    {
        hr = WINCODEC_ERR_WRONGSTATE;
    }
    else
    {
        This->encoder_frame.width = uiWidth;
        This->encoder_frame.height = uiHeight;
        hr = S_OK;
    }

end:
    LeaveCriticalSection(&This->parent->lock);

    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_SetResolution(IWICBitmapFrameEncode *iface,
    double dpiX, double dpiY)
{
    CommonEncoderFrame *This = impl_from_IWICBitmapFrameEncode(iface);
    HRESULT hr;

    TRACE("(%p,%0.2f,%0.2f)\n", iface, dpiX, dpiY);

    EnterCriticalSection(&This->parent->lock);

    if (!This->initialized || This->frame_created)
    {
        hr = WINCODEC_ERR_WRONGSTATE;
    }
    else
    {
        This->encoder_frame.dpix = dpiX;
        This->encoder_frame.dpiy = dpiY;
        hr = S_OK;
    }

    LeaveCriticalSection(&This->parent->lock);

    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_SetPixelFormat(IWICBitmapFrameEncode *iface,
    WICPixelFormatGUID *pPixelFormat)
{
    CommonEncoderFrame *This = impl_from_IWICBitmapFrameEncode(iface);
    HRESULT hr;
    GUID pixel_format;
    DWORD bpp;
    BOOL indexed;

    TRACE("(%p,%s)\n", iface, debugstr_guid(pPixelFormat));

    EnterCriticalSection(&This->parent->lock);

    if (!This->initialized || This->frame_created)
    {
        hr = WINCODEC_ERR_WRONGSTATE;
    }
    else
    {
        pixel_format = *pPixelFormat;
        hr = encoder_get_supported_format(This->parent->encoder, &pixel_format, &bpp, &indexed);
    }

    if (SUCCEEDED(hr))
    {
        TRACE("<-- %s bpp=%li indexed=%i\n", wine_dbgstr_guid(&pixel_format), bpp, indexed);
        *pPixelFormat = pixel_format;
        This->encoder_frame.pixel_format = pixel_format;
        This->encoder_frame.bpp = bpp;
        This->encoder_frame.indexed = indexed;
    }

    LeaveCriticalSection(&This->parent->lock);

    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_SetColorContexts(IWICBitmapFrameEncode *iface,
    UINT cCount, IWICColorContext **ppIColorContext)
{
    FIXME("(%p,%u,%p): stub\n", iface, cCount, ppIColorContext);
    return E_NOTIMPL;
}

static HRESULT WINAPI CommonEncoderFrame_SetPalette(IWICBitmapFrameEncode *iface,
    IWICPalette *palette)
{
    CommonEncoderFrame *This = impl_from_IWICBitmapFrameEncode(iface);
    HRESULT hr;

    TRACE("(%p,%p)\n", iface, palette);

    if (!palette)
        return E_INVALIDARG;

    EnterCriticalSection(&This->parent->lock);

    if (!This->initialized)
        hr = WINCODEC_ERR_NOTINITIALIZED;
    else if (This->frame_created)
        hr = WINCODEC_ERR_WRONGSTATE;
    else
        hr = IWICPalette_GetColors(palette, 256, This->encoder_frame.palette,
            &This->encoder_frame.num_colors);

    LeaveCriticalSection(&This->parent->lock);

    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_SetThumbnail(IWICBitmapFrameEncode *iface,
    IWICBitmapSource *pIThumbnail)
{
    FIXME("(%p,%p): stub\n", iface, pIThumbnail);
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

static HRESULT WINAPI CommonEncoderFrame_WritePixels(IWICBitmapFrameEncode *iface,
    UINT lineCount, UINT cbStride, UINT cbBufferSize, BYTE *pbPixels)
{
    CommonEncoderFrame *This = impl_from_IWICBitmapFrameEncode(iface);
    HRESULT hr=S_OK;
    ULONGLONG required_stride;

    TRACE("(%p,%u,%u,%u,%p)\n", iface, lineCount, cbStride, cbBufferSize, pbPixels);

    EnterCriticalSection(&This->parent->lock);

    if (!This->initialized || !This->encoder_frame.height || !This->encoder_frame.width ||
        !This->encoder_frame.bpp)
    {
        LeaveCriticalSection(&This->parent->lock);
        return WINCODEC_ERR_WRONGSTATE;
    }

    required_stride = ((ULONGLONG)This->encoder_frame.width * This->encoder_frame.bpp + 7)/8;

    if (lineCount == 0 || This->encoder_frame.height - This->lines_written < lineCount ||
        cbStride < required_stride || cbBufferSize < (ULONGLONG)cbStride * (lineCount - 1) + required_stride ||
        !pbPixels)
    {
        LeaveCriticalSection(&This->parent->lock);
        return E_INVALIDARG;
    }

    if (!This->frame_created)
    {
        hr = encoder_create_frame(This->parent->encoder, &This->encoder_frame);
        if (SUCCEEDED(hr))
            This->frame_created = TRUE;
    }

    if (SUCCEEDED(hr))
    {
        hr = encoder_write_lines(This->parent->encoder, pbPixels, lineCount, cbStride);
        if (SUCCEEDED(hr))
            This->lines_written += lineCount;
    }

    LeaveCriticalSection(&This->parent->lock);

    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_WriteSource(IWICBitmapFrameEncode *iface,
    IWICBitmapSource *pIBitmapSource, WICRect *prc)
{
    CommonEncoderFrame *This = impl_from_IWICBitmapFrameEncode(iface);
    HRESULT hr;
    TRACE("(%p,%p,%s)\n", iface, pIBitmapSource, debug_wic_rect(prc));

    if (!This->initialized)
        return WINCODEC_ERR_WRONGSTATE;

    hr = configure_write_source(iface, pIBitmapSource, prc,
        This->encoder_frame.bpp ? &This->encoder_frame.pixel_format : NULL,
        This->encoder_frame.width, This->encoder_frame.height,
        This->encoder_frame.dpix, This->encoder_frame.dpiy);

    if (SUCCEEDED(hr))
    {
        hr = write_source(iface, pIBitmapSource, prc,
            &This->encoder_frame.pixel_format, This->encoder_frame.bpp,
            !This->encoder_frame.num_colors && This->encoder_frame.indexed,
            This->encoder_frame.width, This->encoder_frame.height);
    }

    return hr;
}

static void free_frame_metadata(struct encoder_metadata *metadata, UINT count)
{
    UINT i;
    for (i = 0; i < count; ++i) free(metadata[i].data);
    free(metadata);
}

static HRESULT serialize_frame_metadata(CommonEncoderFrame *frame, struct encoder_metadata **data,
        UINT *metadata_count, ULONGLONG *revision)
{
    IWICMetadataWriter **writers;
    struct encoder_metadata *metadata;
    IWICPersistStream *persist;
    IStream *stream;
    LARGE_INTEGER move = {{0}};
    STATSTG stat;
    ULONG read;
    UINT count, i;
    HRESULT hr = S_OK;

    EnterCriticalSection(&frame->parent->lock);
    count = frame->metadata_count;
    *revision = frame->metadata_revision;
    writers = calloc(count ? count : 1, sizeof(*writers));
    if (writers)
        for (i = 0; i < count; ++i)
        {
            writers[i] = frame->metadata[i];
            IWICMetadataWriter_AddRef(writers[i]);
        }
    LeaveCriticalSection(&frame->parent->lock);
    if (!writers) return E_OUTOFMEMORY;
    metadata = calloc(count ? count : 1, sizeof(*metadata));
    if (!metadata) hr = E_OUTOFMEMORY;
    for (i = 0; SUCCEEDED(hr) && i < count; ++i)
    {
        hr = IWICMetadataWriter_GetMetadataFormat(writers[i], &metadata[i].format);
        if (FAILED(hr)) break;
        if (IsEqualGUID(&metadata[i].format, &GUID_MetadataFormatApp0) && frame->encoder_frame.dpix && frame->encoder_frame.dpiy)
        {
            PROPVARIANT id, value;
            id.vt = VT_UI2;
            id.uiVal = 1;
            value.vt = VT_UI1;
            value.bVal = 1;
            hr = IWICMetadataWriter_SetValue(writers[i], NULL, &id, &value);
            id.uiVal = 2;
            value.vt = VT_UI2;
            value.uiVal = frame->encoder_frame.dpix;
            if (SUCCEEDED(hr)) hr = IWICMetadataWriter_SetValue(writers[i], NULL, &id, &value);
            id.uiVal = 3;
            value.uiVal = frame->encoder_frame.dpiy;
            if (SUCCEEDED(hr)) hr = IWICMetadataWriter_SetValue(writers[i], NULL, &id, &value);
            if (FAILED(hr)) break;
        }
        hr = IWICMetadataWriter_QueryInterface(writers[i], &IID_IWICPersistStream, (void **)&persist);
        if (FAILED(hr)) break;
        hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
        if (SUCCEEDED(hr))
        {
            hr = IWICPersistStream_SaveEx(persist, stream, WICPersistOptionLittleEndian, FALSE);
            if (SUCCEEDED(hr)) hr = IStream_Stat(stream, &stat, STATFLAG_NONAME);
            if (SUCCEEDED(hr) && stat.cbSize.QuadPart > MAXDWORD) hr = WINCODEC_ERR_TOOMUCHMETADATA;
            if (SUCCEEDED(hr))
            {
                metadata[i].size = stat.cbSize.LowPart;
                metadata[i].data = malloc(metadata[i].size ? metadata[i].size : 1);
                if (!metadata[i].data) hr = E_OUTOFMEMORY;
                else
                {
                    hr = IStream_Seek(stream, move, STREAM_SEEK_SET, NULL);
                    if (SUCCEEDED(hr)) hr = IStream_Read(stream, metadata[i].data, metadata[i].size, &read);
                    if (SUCCEEDED(hr) && read != metadata[i].size) hr = WINCODEC_ERR_STREAMREAD;
                }
            }
            IStream_Release(stream);
        }
        IWICPersistStream_Release(persist);
    }
    for (i = 0; i < count; ++i) IWICMetadataWriter_Release(writers[i]);
    free(writers);
    if (FAILED(hr))
    {
        if (metadata) free_frame_metadata(metadata, count);
    }
    else
    {
        *data = metadata;
        *metadata_count = count;
    }
    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_Commit(IWICBitmapFrameEncode *iface)
{
    CommonEncoderFrame *This = impl_from_IWICBitmapFrameEncode(iface);
    struct encoder_metadata *metadata = NULL;
    UINT metadata_count = 0;
    ULONGLONG revision;
    HRESULT hr;

    TRACE("(%p)\n", iface);
    EnterCriticalSection(&This->parent->lock);
    hr = !This->frame_created || This->lines_written != This->encoder_frame.height ||
            This->committed || This->committing ? WINCODEC_ERR_WRONGSTATE : S_OK;
    if (SUCCEEDED(hr)) This->committing = TRUE;
    LeaveCriticalSection(&This->parent->lock);
    if (FAILED(hr)) return hr;
    hr = serialize_frame_metadata(This, &metadata, &metadata_count, &revision);
    EnterCriticalSection(&This->parent->lock);
    if (FAILED(hr))
    {
        This->committing = FALSE;
        LeaveCriticalSection(&This->parent->lock);
        return hr;
    }
    if (This->committed || This->metadata_revision != revision)
        hr = WINCODEC_ERR_WRONGSTATE;
    else
    {
        hr = encoder_commit_frame(This->parent->encoder, metadata, metadata_count);
        if (SUCCEEDED(hr))
        {
            This->committed = TRUE;
            This->parent->uncommitted_frame = FALSE;
        }
    }
    This->committing = FALSE;
    LeaveCriticalSection(&This->parent->lock);
    free_frame_metadata(metadata, metadata_count);
    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_GetMetadataQueryWriter(IWICBitmapFrameEncode *iface,
    IWICMetadataQueryWriter **ppIMetadataQueryWriter)
{
    CommonEncoderFrame *encoder = impl_from_IWICBitmapFrameEncode(iface);

    TRACE("iface, %p, ppIMetadataQueryWriter %p.\n", iface, ppIMetadataQueryWriter);

    if (!ppIMetadataQueryWriter)
        return E_INVALIDARG;

    if (!encoder->initialized && !(encoder->parent->encoder_info.flags & ENCODER_FLAGS_METADATA_UNINITIALIZED))
        return WINCODEC_ERR_NOTINITIALIZED;

    if (!(encoder->parent->encoder_info.flags & ENCODER_FLAGS_SUPPORTS_METADATA))
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;

    return MetadataQueryWriter_CreateInstanceFromBlockWriter(&encoder->IWICMetadataBlockWriter_iface, ppIMetadataQueryWriter);
}

static const IWICBitmapFrameEncodeVtbl CommonEncoderFrame_Vtbl = {
    CommonEncoderFrame_QueryInterface,
    CommonEncoderFrame_AddRef,
    CommonEncoderFrame_Release,
    CommonEncoderFrame_Initialize,
    CommonEncoderFrame_SetSize,
    CommonEncoderFrame_SetResolution,
    CommonEncoderFrame_SetPixelFormat,
    CommonEncoderFrame_SetColorContexts,
    CommonEncoderFrame_SetPalette,
    CommonEncoderFrame_SetThumbnail,
    CommonEncoderFrame_WritePixels,
    CommonEncoderFrame_WriteSource,
    CommonEncoderFrame_Commit,
    CommonEncoderFrame_GetMetadataQueryWriter
};

static HRESULT WINAPI CommonEncoder_QueryInterface(IWICBitmapEncoder *iface, REFIID iid,
    void **ppv)
{
    CommonEncoder *This = impl_from_IWICBitmapEncoder(iface);
    TRACE("(%p,%s,%p)\n", iface, debugstr_guid(iid), ppv);

    if (!ppv) return E_INVALIDARG;

    if (IsEqualIID(&IID_IUnknown, iid) ||
        IsEqualIID(&IID_IWICBitmapEncoder, iid))
    {
        *ppv = &This->IWICBitmapEncoder_iface;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI CommonEncoder_AddRef(IWICBitmapEncoder *iface)
{
    CommonEncoder *This = impl_from_IWICBitmapEncoder(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI CommonEncoder_Release(IWICBitmapEncoder *iface)
{
    CommonEncoder *This = impl_from_IWICBitmapEncoder(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);

    if (ref == 0)
    {
        This->lock.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->lock);
        if (This->stream)
            IStream_Release(This->stream);
        encoder_destroy(This->encoder);
        free(This);
    }

    return ref;
}

static HRESULT WINAPI CommonEncoder_Initialize(IWICBitmapEncoder *iface,
    IStream *pIStream, WICBitmapEncoderCacheOption cacheOption)
{
    CommonEncoder *This = impl_from_IWICBitmapEncoder(iface);
    HRESULT hr;

    TRACE("(%p,%p,%u)\n", iface, pIStream, cacheOption);

    if (!pIStream)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    if (This->stream)
    {
        LeaveCriticalSection(&This->lock);
        return WINCODEC_ERR_WRONGSTATE;
    }

    hr = encoder_initialize(This->encoder, pIStream);

    if (SUCCEEDED(hr))
    {
        This->stream = pIStream;
        IStream_AddRef(This->stream);
    }

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI CommonEncoder_GetContainerFormat(IWICBitmapEncoder *iface, GUID *format)
{
    CommonEncoder *This = impl_from_IWICBitmapEncoder(iface);
    TRACE("(%p,%p)\n", iface, format);

    if (!format)
        return E_INVALIDARG;

    memcpy(format, &This->encoder_info.container_format, sizeof(*format));
    return S_OK;
}

static HRESULT WINAPI CommonEncoder_GetEncoderInfo(IWICBitmapEncoder *iface, IWICBitmapEncoderInfo **info)
{
    CommonEncoder *This = impl_from_IWICBitmapEncoder(iface);
    IWICComponentInfo *comp_info;
    HRESULT hr;

    TRACE("%p,%p\n", iface, info);

    if (!info) return E_INVALIDARG;

    hr = CreateComponentInfo(&This->encoder_info.clsid, &comp_info);
    if (hr == S_OK)
    {
        hr = IWICComponentInfo_QueryInterface(comp_info, &IID_IWICBitmapEncoderInfo, (void **)info);
        IWICComponentInfo_Release(comp_info);
    }
    return hr;
}

static HRESULT WINAPI CommonEncoder_SetColorContexts(IWICBitmapEncoder *iface,
    UINT cCount, IWICColorContext **ppIColorContext)
{
    FIXME("(%p,%u,%p): stub\n", iface, cCount, ppIColorContext);
    return E_NOTIMPL;
}

static HRESULT WINAPI CommonEncoder_SetPalette(IWICBitmapEncoder *iface, IWICPalette *palette)
{
    CommonEncoder *This = impl_from_IWICBitmapEncoder(iface);
    HRESULT hr;

    TRACE("(%p,%p)\n", iface, palette);

    EnterCriticalSection(&This->lock);

    hr = This->stream ? WINCODEC_ERR_UNSUPPORTEDOPERATION : WINCODEC_ERR_NOTINITIALIZED;

    LeaveCriticalSection(&This->lock);

    return hr;
}

static HRESULT WINAPI CommonEncoder_SetThumbnail(IWICBitmapEncoder *iface, IWICBitmapSource *pIThumbnail)
{
    TRACE("(%p,%p)\n", iface, pIThumbnail);
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

static HRESULT WINAPI CommonEncoder_SetPreview(IWICBitmapEncoder *iface, IWICBitmapSource *pIPreview)
{
    TRACE("(%p,%p)\n", iface, pIPreview);
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

static HRESULT WINAPI CommonEncoderFrame_Block_QueryInterface(IWICMetadataBlockWriter *iface, REFIID iid, void **ppv)
{
    CommonEncoderFrame *encoder = impl_from_IWICMetadataBlockWriter(iface);

    return IWICBitmapFrameEncode_QueryInterface(&encoder->IWICBitmapFrameEncode_iface, iid, ppv);
}

static ULONG WINAPI CommonEncoderFrame_Block_AddRef(IWICMetadataBlockWriter *iface)
{
    CommonEncoderFrame *encoder = impl_from_IWICMetadataBlockWriter(iface);

    return IWICBitmapFrameEncode_AddRef(&encoder->IWICBitmapFrameEncode_iface);
}

static ULONG WINAPI CommonEncoderFrame_Block_Release(IWICMetadataBlockWriter *iface)
{
    CommonEncoderFrame *encoder = impl_from_IWICMetadataBlockWriter(iface);

    return IWICBitmapFrameEncode_Release(&encoder->IWICBitmapFrameEncode_iface);
}

static HRESULT WINAPI CommonEncoderFrame_Block_GetContainerFormat(IWICMetadataBlockWriter *iface, GUID *container_format)
{
    CommonEncoderFrame *frame = impl_from_IWICMetadataBlockWriter(iface);
    if (!container_format) return E_INVALIDARG;
    *container_format = frame->parent->encoder_info.container_format;
    return S_OK;
}

static HRESULT WINAPI CommonEncoderFrame_Block_GetCount(IWICMetadataBlockWriter *iface, UINT *count)
{
    CommonEncoderFrame *frame = impl_from_IWICMetadataBlockWriter(iface);
    if (!count) return E_INVALIDARG;
    EnterCriticalSection(&frame->parent->lock);
    *count = frame->metadata_count;
    LeaveCriticalSection(&frame->parent->lock);
    return S_OK;
}

static HRESULT WINAPI CommonEncoderFrame_Block_GetWriterByIndex(IWICMetadataBlockWriter *iface, UINT index,
        IWICMetadataWriter **writer)
{
    CommonEncoderFrame *frame = impl_from_IWICMetadataBlockWriter(iface);
    HRESULT hr = S_OK;
    if (!writer) return E_INVALIDARG;
    *writer = NULL;
    EnterCriticalSection(&frame->parent->lock);
    if (index >= frame->metadata_count) hr = WINCODEC_ERR_PROPERTYNOTFOUND;
    else
    {
        *writer = frame->metadata[index];
        IWICMetadataWriter_AddRef(*writer);
    }
    LeaveCriticalSection(&frame->parent->lock);
    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_Block_GetReaderByIndex(IWICMetadataBlockWriter *iface,
        UINT index, IWICMetadataReader **reader)
{
    IWICMetadataWriter *writer;
    HRESULT hr;
    if (!reader) return E_INVALIDARG;
    *reader = NULL;
    hr = CommonEncoderFrame_Block_GetWriterByIndex(iface, index, &writer);
    if (SUCCEEDED(hr))
    {
        hr = IWICMetadataWriter_QueryInterface(writer, &IID_IWICMetadataReader, (void **)reader);
        IWICMetadataWriter_Release(writer);
    }
    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_Block_GetEnumerator(IWICMetadataBlockWriter *iface, IEnumUnknown **enumerator)
{
    CommonEncoderFrame *frame = impl_from_IWICMetadataBlockWriter(iface);
    HRESULT hr;
    EnterCriticalSection(&frame->parent->lock);
    hr = create_metadata_writer_enumerator((IUnknown **)frame->metadata, frame->metadata_count, enumerator);
    LeaveCriticalSection(&frame->parent->lock);
    return hr;
}

static HRESULT check_frame_metadata_writer(CommonEncoderFrame *frame, IWICMetadataWriter *writer)
{
    GUID format;
    HRESULT hr;
    if (!writer) return E_INVALIDARG;
    hr = IWICMetadataWriter_GetMetadataFormat(writer, &format);
    if (FAILED(hr)) return hr;
    if (IsEqualGUID(&frame->parent->encoder_info.container_format, &GUID_ContainerFormatJpeg))
        return IsEqualGUID(&format, &GUID_MetadataFormatApp0) || IsEqualGUID(&format, &GUID_MetadataFormatApp1) ?
                S_OK : WINCODEC_ERR_UNSUPPORTEDOPERATION;
    if (IsEqualGUID(&frame->parent->encoder_info.container_format, &GUID_ContainerFormatWmp))
        return IsEqualGUID(&format, &GUID_MetadataFormatExif) || IsEqualGUID(&format, &GUID_MetadataFormatGps) ?
                S_OK : WINCODEC_ERR_UNSUPPORTEDOPERATION;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

static HRESULT WINAPI CommonEncoderFrame_Block_InitializeFromBlockReader(IWICMetadataBlockWriter *iface,
        IWICMetadataBlockReader *reader)
{
    CommonEncoderFrame *frame = impl_from_IWICMetadataBlockWriter(iface);
    IWICMetadataWriter **writers, **old_writers;
    IWICMetadataReader *item;
    UINT count, old_count, i;
    HRESULT hr;

    if (!reader) return E_INVALIDARG;
    hr = IWICMetadataBlockReader_GetCount(reader, &count);
    if (FAILED(hr)) return hr;
    if (!(writers = calloc(count ? count : 1, sizeof(*writers)))) return E_OUTOFMEMORY;
    for (i = 0; i < count; ++i)
    {
        hr = IWICMetadataBlockReader_GetReaderByIndex(reader, i, &item);
        if (SUCCEEDED(hr))
        {
            hr = create_metadata_writer_from_reader(item, NULL, &writers[i]);
            IWICMetadataReader_Release(item);
        }
        if (SUCCEEDED(hr)) hr = check_frame_metadata_writer(frame, writers[i]);
        if (FAILED(hr)) break;
    }
    if (SUCCEEDED(hr))
    {
        EnterCriticalSection(&frame->parent->lock);
        if (!frame->initialized || frame->committed || frame->committing) hr = WINCODEC_ERR_WRONGSTATE;
        else
        {
            old_writers = frame->metadata;
            old_count = frame->metadata_count;
            frame->metadata = writers;
            frame->metadata_count = count;
            frame->metadata_capacity = count;
            ++frame->metadata_revision;
            writers = old_writers;
            count = old_count;
        }
        LeaveCriticalSection(&frame->parent->lock);
    }
    for (i = 0; i < count; ++i) if (writers[i]) IWICMetadataWriter_Release(writers[i]);
    free(writers);
    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_Block_AddWriter(IWICMetadataBlockWriter *iface, IWICMetadataWriter *writer)
{
    CommonEncoderFrame *frame = impl_from_IWICMetadataBlockWriter(iface);
    HRESULT hr = check_frame_metadata_writer(frame, writer);
    if (FAILED(hr)) return hr;
    EnterCriticalSection(&frame->parent->lock);
    if (!frame->initialized || frame->committed || frame->committing) hr = WINCODEC_ERR_WRONGSTATE;
    else if (frame->metadata_count == UINT_MAX ||
            !wincodecs_array_reserve((void **)&frame->metadata, &frame->metadata_capacity,
                    frame->metadata_count + 1, sizeof(*frame->metadata))) hr = E_OUTOFMEMORY;
    else
    {
        IWICMetadataWriter_AddRef(writer);
        frame->metadata[frame->metadata_count++] = writer;
        ++frame->metadata_revision;
    }
    LeaveCriticalSection(&frame->parent->lock);
    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_Block_SetWriterByIndex(IWICMetadataBlockWriter *iface, UINT index,
        IWICMetadataWriter *writer)
{
    CommonEncoderFrame *frame = impl_from_IWICMetadataBlockWriter(iface);
    IWICMetadataWriter *old_writer = NULL;
    HRESULT hr = check_frame_metadata_writer(frame, writer);
    if (FAILED(hr)) return hr;
    EnterCriticalSection(&frame->parent->lock);
    if (!frame->initialized || frame->committed || frame->committing) hr = WINCODEC_ERR_WRONGSTATE;
    else if (index >= frame->metadata_count) hr = WINCODEC_ERR_PROPERTYNOTFOUND;
    else
    {
        IWICMetadataWriter_AddRef(writer);
        old_writer = frame->metadata[index];
        frame->metadata[index] = writer;
        ++frame->metadata_revision;
    }
    LeaveCriticalSection(&frame->parent->lock);
    if (old_writer) IWICMetadataWriter_Release(old_writer);
    return hr;
}

static HRESULT WINAPI CommonEncoderFrame_Block_RemoveWriterByIndex(IWICMetadataBlockWriter *iface, UINT index)
{
    CommonEncoderFrame *frame = impl_from_IWICMetadataBlockWriter(iface);
    IWICMetadataWriter *writer = NULL;
    HRESULT hr = S_OK;
    EnterCriticalSection(&frame->parent->lock);
    if (!frame->initialized || frame->committed || frame->committing) hr = WINCODEC_ERR_WRONGSTATE;
    else if (index >= frame->metadata_count) hr = WINCODEC_ERR_PROPERTYNOTFOUND;
    else
    {
        writer = frame->metadata[index];
        --frame->metadata_count;
        memmove(frame->metadata + index, frame->metadata + index + 1,
                (frame->metadata_count - index) * sizeof(*frame->metadata));
        ++frame->metadata_revision;
    }
    LeaveCriticalSection(&frame->parent->lock);
    if (writer) IWICMetadataWriter_Release(writer);
    return hr;
}

static const IWICMetadataBlockWriterVtbl CommonEncoderFrame_BlockVtbl =
{
    CommonEncoderFrame_Block_QueryInterface,
    CommonEncoderFrame_Block_AddRef,
    CommonEncoderFrame_Block_Release,
    CommonEncoderFrame_Block_GetContainerFormat,
    CommonEncoderFrame_Block_GetCount,
    CommonEncoderFrame_Block_GetReaderByIndex,
    CommonEncoderFrame_Block_GetEnumerator,
    CommonEncoderFrame_Block_InitializeFromBlockReader,
    CommonEncoderFrame_Block_GetWriterByIndex,
    CommonEncoderFrame_Block_AddWriter,
    CommonEncoderFrame_Block_SetWriterByIndex,
    CommonEncoderFrame_Block_RemoveWriterByIndex,
};

static HRESULT WINAPI CommonEncoder_CreateNewFrame(IWICBitmapEncoder *iface,
    IWICBitmapFrameEncode **ppIFrameEncode, IPropertyBag2 **ppIEncoderOptions)
{
    CommonEncoder *This = impl_from_IWICBitmapEncoder(iface);
    CommonEncoderFrame *result;
    HRESULT hr;
    DWORD opts_length;
    PROPBAG2 opts[6];

    TRACE("(%p,%p,%p)\n", iface, ppIFrameEncode, ppIEncoderOptions);

    EnterCriticalSection(&This->lock);

    if (This->frame_count != 0 && !(This->encoder_info.flags & ENCODER_FLAGS_MULTI_FRAME))
    {
        LeaveCriticalSection(&This->lock);
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }

    if (!This->stream || This->committed || This->uncommitted_frame)
    {
        LeaveCriticalSection(&This->lock);
        return WINCODEC_ERR_NOTINITIALIZED;
    }

    result = calloc(1, sizeof(*result));
    if (!result)
    {
        LeaveCriticalSection(&This->lock);
        return E_OUTOFMEMORY;
    }

    result->IWICBitmapFrameEncode_iface.lpVtbl = &CommonEncoderFrame_Vtbl;
    result->IWICMetadataBlockWriter_iface.lpVtbl = &CommonEncoderFrame_BlockVtbl;
    result->ref = 1;
    result->parent = This;

    if (ppIEncoderOptions)
    {
        for (opts_length = 0; This->encoder_info.encoder_options[opts_length] < ENCODER_OPTION_END; opts_length++)
        {
            opts[opts_length] = encoder_option_properties[This->encoder_info.encoder_options[opts_length]];
        }

        hr = CreatePropertyBag2(opts, opts_length, ppIEncoderOptions);
        if (FAILED(hr))
        {
            LeaveCriticalSection(&This->lock);
            free(result);
            return hr;
        }
    }

    IWICBitmapEncoder_AddRef(iface);
    This->frame_count++;
    This->uncommitted_frame = TRUE;

    LeaveCriticalSection(&This->lock);

    *ppIFrameEncode = &result->IWICBitmapFrameEncode_iface;

    return S_OK;
}

static HRESULT WINAPI CommonEncoder_Commit(IWICBitmapEncoder *iface)
{
    CommonEncoder *This = impl_from_IWICBitmapEncoder(iface);
    HRESULT hr;

    TRACE("(%p)\n", iface);

    EnterCriticalSection(&This->lock);

    if (This->committed || This->uncommitted_frame)
        hr = WINCODEC_ERR_WRONGSTATE;
    else
    {
        hr = encoder_commit_file(This->encoder);
        if (SUCCEEDED(hr))
            This->committed = TRUE;
    }

    LeaveCriticalSection(&This->lock);

    return hr;
}

static HRESULT WINAPI CommonEncoder_GetMetadataQueryWriter(IWICBitmapEncoder *iface,
    IWICMetadataQueryWriter **ppIMetadataQueryWriter)
{
    FIXME("(%p,%p): stub\n", iface, ppIMetadataQueryWriter);
    return E_NOTIMPL;
}

static const IWICBitmapEncoderVtbl CommonEncoder_Vtbl = {
    CommonEncoder_QueryInterface,
    CommonEncoder_AddRef,
    CommonEncoder_Release,
    CommonEncoder_Initialize,
    CommonEncoder_GetContainerFormat,
    CommonEncoder_GetEncoderInfo,
    CommonEncoder_SetColorContexts,
    CommonEncoder_SetPalette,
    CommonEncoder_SetThumbnail,
    CommonEncoder_SetPreview,
    CommonEncoder_CreateNewFrame,
    CommonEncoder_Commit,
    CommonEncoder_GetMetadataQueryWriter
};

HRESULT CommonEncoder_CreateInstance(struct encoder *encoder,
    const struct encoder_info *encoder_info, REFIID iid, void** ppv)
{
    CommonEncoder *This;
    HRESULT ret;

    TRACE("(%s,%p)\n", debugstr_guid(iid), ppv);

    *ppv = NULL;

    This = malloc(sizeof(CommonEncoder));
    if (!This)
    {
        encoder_destroy(encoder);
        return E_OUTOFMEMORY;
    }

    This->IWICBitmapEncoder_iface.lpVtbl = &CommonEncoder_Vtbl;
    This->ref = 1;
    This->stream = NULL;
    This->encoder = encoder;
    This->encoder_info = *encoder_info;
    This->frame_count = 0;
    This->uncommitted_frame = FALSE;
    This->committed = FALSE;
    InitializeCriticalSectionEx(&This->lock, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO);
    This->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": CommonEncoder.lock");

    ret = IWICBitmapEncoder_QueryInterface(&This->IWICBitmapEncoder_iface, iid, ppv);
    IWICBitmapEncoder_Release(&This->IWICBitmapEncoder_iface);

    return ret;
}
