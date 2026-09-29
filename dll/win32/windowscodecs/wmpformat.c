/*
 * PROJECT:     LiberNT Windows Imaging Component
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     JPEG XR encoding and decoding with the Microsoft reference codec
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#define COBJMACROS
#include <stdarg.h>
#include <stdint.h>
#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "wincodecs_private.h"
#undef ERR
#include "JXRGlue.h"

struct wmp_stream
{
    struct WMPStream iface;
    IStream *stream;
    HRESULT result;
};

static ERR wmp_stream_close(struct WMPStream **stream)
{
    return WMP_errSuccess;
}

static Bool wmp_stream_eos(struct WMPStream *iface)
{
    struct wmp_stream *stream = CONTAINING_RECORD(iface, struct wmp_stream, iface);
    ULONGLONG position, size;
    return FAILED(stream_seek(stream->stream, 0, STREAM_SEEK_CUR, &position)) ||
            FAILED(stream_getsize(stream->stream, &size)) || position >= size;
}

static ERR wmp_stream_read(struct WMPStream *iface, void *data, size_t size)
{
    struct wmp_stream *stream = CONTAINING_RECORD(iface, struct wmp_stream, iface);
    ULONG count, chunk;
    BYTE *cursor = data;

    if (FAILED(stream->result)) return WMP_errFileIO;
    while (size)
    {
        chunk = min(size, MAXDWORD);
        stream->result = stream_read(stream->stream, cursor, chunk, &count);
        if (SUCCEEDED(stream->result) && count != chunk) stream->result = WINCODEC_ERR_STREAMREAD;
        if (FAILED(stream->result)) return WMP_errFileIO;
        size -= chunk;
        cursor += chunk;
    }
    return WMP_errSuccess;
}

static ERR wmp_stream_write(struct WMPStream *iface, const void *data, size_t size)
{
    struct wmp_stream *stream = CONTAINING_RECORD(iface, struct wmp_stream, iface);
    ULONG count, chunk;
    const BYTE *cursor = data;

    if (FAILED(stream->result)) return WMP_errFileIO;
    while (size)
    {
        chunk = min(size, MAXDWORD);
        stream->result = stream_write(stream->stream, cursor, chunk, &count);
        if (SUCCEEDED(stream->result) && count != chunk) stream->result = WINCODEC_ERR_STREAMWRITE;
        if (FAILED(stream->result)) return WMP_errFileIO;
        size -= chunk;
        cursor += chunk;
    }
    return WMP_errSuccess;
}

static ERR wmp_stream_set_pos(struct WMPStream *iface, size_t position)
{
    struct wmp_stream *stream = CONTAINING_RECORD(iface, struct wmp_stream, iface);
    if (FAILED(stream->result)) return WMP_errFileIO;
    if ((ULONGLONG)position > MAXLONGLONG) return WMP_errInvalidArgument;
    stream->result = stream_seek(stream->stream, position, STREAM_SEEK_SET, NULL);
    return FAILED(stream->result) ? WMP_errFileIO : WMP_errSuccess;
}

static ERR wmp_stream_get_pos(struct WMPStream *iface, size_t *position)
{
    struct wmp_stream *stream = CONTAINING_RECORD(iface, struct wmp_stream, iface);
    ULONGLONG offset;
    if (FAILED(stream->result)) return WMP_errFileIO;
    stream->result = stream_seek(stream->stream, 0, STREAM_SEEK_CUR, &offset);
    if (FAILED(stream->result)) return WMP_errFileIO;
    if (offset > SIZE_MAX) return WMP_errBufferOverflow;
    *position = offset;
    return WMP_errSuccess;
}

static ERR wmp_stream_get_size(struct WMPStream *iface, size_t *size)
{
    struct wmp_stream *stream = CONTAINING_RECORD(iface, struct wmp_stream, iface);
    ULONGLONG length;
    if (FAILED(stream->result)) return WMP_errFileIO;
    stream->result = stream_getsize(stream->stream, &length);
    if (FAILED(stream->result)) return WMP_errFileIO;
    if (length > SIZE_MAX) return WMP_errBufferOverflow;
    *size = length;
    return WMP_errSuccess;
}

static void initialize_wmp_stream(struct wmp_stream *stream, IStream *target)
{
    stream->stream = target;
    stream->result = S_OK;
    stream->iface.Close = wmp_stream_close;
    stream->iface.EOS = wmp_stream_eos;
    stream->iface.Read = wmp_stream_read;
    stream->iface.Write = wmp_stream_write;
    stream->iface.SetPos = wmp_stream_set_pos;
    stream->iface.GetPos = wmp_stream_get_pos;
    stream->iface.GetSize = wmp_stream_get_size;
}

static HRESULT wmp_result(struct wmp_stream *stream, ERR error)
{
    if (FAILED(stream->result)) return stream->result;
    switch (error)
    {
    case WMP_errSuccess: return S_OK;
    case WMP_errOutOfMemory: return E_OUTOFMEMORY;
    case WMP_errUnsupportedFormat: return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    case WMP_errFileIO: return WINCODEC_ERR_STREAMREAD;
    case WMP_errInvalidArgument:
    case WMP_errInvalidParameter: return E_INVALIDARG;
    case WMP_errOutOfSequence: return WINCODEC_ERR_WRONGSTATE;
    default: return E_FAIL;
    }
}

struct wmp_encoder
{
    struct encoder iface;
    struct wmp_stream stream;
    struct encoder_frame frame;
    PKImageEncode *codec;
    BYTE *pixels;
    UINT stride, lines;
    HRESULT result;
};

static HRESULT CDECL wmp_encoder_initialize(struct encoder *iface, IStream *stream)
{
    struct wmp_encoder *encoder = CONTAINING_RECORD(iface, struct wmp_encoder, iface);
    initialize_wmp_stream(&encoder->stream, stream);
    return S_OK;
}

static HRESULT CDECL wmp_encoder_get_supported_format(struct encoder *iface, GUID *format,
        DWORD *bpp, BOOL *indexed)
{
    PKPixelInfo info = {0};
    info.pGUIDPixFmt = format;
    if (IsEqualGUID(format, &GUID_WICPixelFormatDontCare) ||
            PixelFormatLookup(&info, LOOKUP_FORWARD) || !info.cbitUnit)
    {
        *format = GUID_WICPixelFormat24bppBGR;
        info.pGUIDPixFmt = format;
        if (PixelFormatLookup(&info, LOOKUP_FORWARD)) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    *bpp = info.cbitUnit;
    *indexed = FALSE;
    return S_OK;
}

static HRESULT CDECL wmp_encoder_create_frame(struct encoder *iface, const struct encoder_frame *frame)
{
    struct wmp_encoder *encoder = CONTAINING_RECORD(iface, struct wmp_encoder, iface);
    ULONGLONG stride = ((ULONGLONG)frame->width * frame->bpp + 7) / 8;
    if (!frame->width || !frame->height || frame->width > INT_MAX || frame->height > INT_MAX ||
            stride > INT_MAX || frame->height > SIZE_MAX / stride) return E_INVALIDARG;
    encoder->pixels = malloc((size_t)stride * frame->height);
    if (!encoder->pixels) return E_OUTOFMEMORY;
    encoder->stride = stride;
    encoder->frame = *frame;
    return S_OK;
}

static HRESULT CDECL wmp_encoder_write_lines(struct encoder *iface, BYTE *data, DWORD count, DWORD stride)
{
    struct wmp_encoder *encoder = CONTAINING_RECORD(iface, struct wmp_encoder, iface);
    UINT i;
    if (FAILED(encoder->result)) return encoder->result;
    if (count > encoder->frame.height - encoder->lines || stride < encoder->stride) return E_INVALIDARG;
    for (i = 0; i < count; ++i)
        memcpy(encoder->pixels + (size_t)(encoder->lines + i) * encoder->stride,
                data + (size_t)i * stride, encoder->stride);
    encoder->lines += count;
    return S_OK;
}

static HRESULT CDECL wmp_encoder_commit_frame(struct encoder *iface,
        const struct encoder_metadata *metadata, UINT count)
{
    struct wmp_encoder *encoder = CONTAINING_RECORD(iface, struct wmp_encoder, iface);
    CWMIStrCodecParam parameters = {0};
    PKPixelInfo info = {0};
    ERR error;
    UINT i, j;

    if (FAILED(encoder->result)) return encoder->result;
    for (i = 0; i < count; ++i)
    {
        if (!IsEqualGUID(&metadata[i].format, &GUID_MetadataFormatExif) &&
                !IsEqualGUID(&metadata[i].format, &GUID_MetadataFormatGps))
            return WINCODEC_ERR_UNSUPPORTEDOPERATION;
        for (j = 0; j < i; ++j)
            if (IsEqualGUID(&metadata[i].format, &metadata[j].format))
                return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    if (encoder->lines != encoder->frame.height) return WINCODEC_ERR_WRONGSTATE;
    error = PKImageEncode_Create_WMP(&encoder->codec);
    if (error) return encoder->result = wmp_result(&encoder->stream, error);
    encoder->codec->pStream = &encoder->stream.iface;
    info.pGUIDPixFmt = &encoder->frame.pixel_format;
    error = PixelFormatLookup(&info, LOOKUP_FORWARD);
    parameters.uiDefaultQPIndex = encoder->frame.lossless ? 1 : 10;
    parameters.uiDefaultQPIndexAlpha = 1;
    parameters.cfColorFormat = info.cfColorFormat == CF_RGB || info.cfColorFormat == CF_RGBE ?
            YUV_444 : info.cfColorFormat;
    parameters.bdBitDepth = BD_LONG;
    parameters.olOverlap = OL_ONE;
    parameters.bfBitstreamFormat = FREQUENCY;
    parameters.sbSubband = SB_ALL;
    parameters.uAlphaMode = (info.grBit & PK_pixfmtHasAlpha) ? 2 : 0;
    if (!error) error = encoder->codec->Initialize(encoder->codec, &encoder->stream.iface,
            &parameters, sizeof(parameters));
    if (!error) error = encoder->codec->SetPixelFormat(encoder->codec, encoder->frame.pixel_format);
    if (!error) error = encoder->codec->SetSize(encoder->codec, encoder->frame.width, encoder->frame.height);
    if (!error) error = encoder->codec->SetResolution(encoder->codec,
            encoder->frame.dpix ? encoder->frame.dpix : 96, encoder->frame.dpiy ? encoder->frame.dpiy : 96);
    for (i = 0; !error && i < count; ++i)
    {
        if (IsEqualGUID(&metadata[i].format, &GUID_MetadataFormatExif))
            error = PKImageEncode_SetEXIFMetadata_WMP(encoder->codec, metadata[i].data, metadata[i].size);
        else if (IsEqualGUID(&metadata[i].format, &GUID_MetadataFormatGps))
            error = PKImageEncode_SetGPSInfoMetadata_WMP(encoder->codec, metadata[i].data, metadata[i].size);
        else error = WMP_errUnsupportedFormat;
    }
    if (!error) error = encoder->codec->WritePixels(encoder->codec,
            encoder->frame.height, encoder->pixels, encoder->stride);
    encoder->result = wmp_result(&encoder->stream, error);
    return encoder->result;
}

static HRESULT CDECL wmp_encoder_commit_file(struct encoder *iface)
{
    struct wmp_encoder *encoder = CONTAINING_RECORD(iface, struct wmp_encoder, iface);
    return encoder->result;
}

static void CDECL wmp_encoder_destroy(struct encoder *iface)
{
    struct wmp_encoder *encoder = CONTAINING_RECORD(iface, struct wmp_encoder, iface);
    if (encoder->codec) encoder->codec->Release(&encoder->codec);
    free(encoder->pixels);
    free(encoder);
}

static const struct encoder_funcs wmp_encoder_funcs =
{
    wmp_encoder_initialize, wmp_encoder_get_supported_format, wmp_encoder_create_frame,
    wmp_encoder_write_lines, wmp_encoder_commit_frame, wmp_encoder_commit_file, wmp_encoder_destroy
};

HRESULT WmpEncoder_CreateInstance(REFIID iid, void **result)
{
    struct encoder_info info = {0};
    struct wmp_encoder *encoder = calloc(1, sizeof(*encoder));
    if (!encoder) return E_OUTOFMEMORY;
    encoder->iface.vtable = &wmp_encoder_funcs;
    info.container_format = GUID_ContainerFormatWmp;
    info.clsid = CLSID_WICWmpEncoder;
    info.flags = ENCODER_FLAGS_SUPPORTS_METADATA | ENCODER_FLAGS_METADATA_UNINITIALIZED;
    info.encoder_options[0] = ENCODER_OPTION_LOSSLESS;
    info.encoder_options[1] = ENCODER_OPTION_END;
    return CommonEncoder_CreateInstance(&encoder->iface, &info, iid, result);
}

struct wmp_decoder
{
    struct decoder iface;
    struct wmp_stream stream;
    PKImageDecode *codec;
    struct decoder_frame frame;
    BYTE *pixels;
    UINT stride;
    HRESULT result;
};

static HRESULT CDECL wmp_decoder_initialize(struct decoder *iface, IStream *stream, struct decoder_stat *stat)
{
    struct wmp_decoder *decoder = CONTAINING_RECORD(iface, struct wmp_decoder, iface);
    PKPixelInfo info = {0};
    ERR error;
    U32 color_size;
    I32 width, height;
    Float xres, yres;
    HRESULT hr;

    if (decoder->codec) decoder->codec->Release(&decoder->codec);
    initialize_wmp_stream(&decoder->stream, stream);
    decoder->result = S_OK;
    hr = stream_seek(stream, 0, STREAM_SEEK_SET, NULL);
    if (FAILED(hr)) return hr;
    error = PKImageDecode_Create_WMP(&decoder->codec);
    if (!error) error = decoder->codec->Initialize(decoder->codec, &decoder->stream.iface);
    if (!error) error = decoder->codec->GetPixelFormat(decoder->codec, &decoder->frame.pixel_format);
    info.pGUIDPixFmt = &decoder->frame.pixel_format;
    if (!error) error = PixelFormatLookup(&info, LOOKUP_FORWARD);
    if (!error) error = decoder->codec->GetSize(decoder->codec, &width, &height);
    if (!error) error = decoder->codec->GetResolution(decoder->codec, &xres, &yres);
    if (!error) error = decoder->codec->GetColorContext(decoder->codec, NULL, &color_size);
    if (error) return wmp_result(&decoder->stream, error);
    if (width <= 0 || height <= 0 || !info.cbitUnit ||
            ((ULONGLONG)width * info.cbitUnit + 7) / 8 > INT_MAX) return WINCODEC_ERR_BADIMAGE;
    decoder->codec->WMP.wmiSCP.uAlphaMode = (info.grBit & PK_pixfmtHasAlpha) ? 2 : 0;
    decoder->frame.width = width;
    decoder->frame.height = height;
    decoder->frame.bpp = info.cbitUnit;
    decoder->frame.dpix = xres;
    decoder->frame.dpiy = yres;
    decoder->frame.num_color_contexts = !!color_size;
    decoder->stride = ((ULONGLONG)width * info.cbitUnit + 7) / 8;
    stat->frame_count = 1;
    stat->flags = WICBitmapDecoderCapabilityCanDecodeAllImages |
            WICBitmapDecoderCapabilityCanDecodeSomeImages | WICBitmapDecoderCapabilityCanEnumerateMetadata;
    return S_OK;
}

static HRESULT CDECL wmp_decoder_get_frame_info(struct decoder *iface, UINT frame, struct decoder_frame *info)
{
    struct wmp_decoder *decoder = CONTAINING_RECORD(iface, struct wmp_decoder, iface);
    if (frame) return WINCODEC_ERR_FRAMEMISSING;
    *info = decoder->frame;
    return S_OK;
}

static HRESULT CDECL wmp_decoder_get_palette(struct decoder *iface, UINT frame, WICColor *colors, UINT *count)
{
    return WINCODEC_ERR_PALETTEUNAVAILABLE;
}

static HRESULT CDECL wmp_decoder_copy_pixels(struct decoder *iface, UINT frame, const WICRect *rect,
        UINT stride, UINT size, BYTE *buffer)
{
    struct wmp_decoder *decoder = CONTAINING_RECORD(iface, struct wmp_decoder, iface);
    PKRect area = {0};
    ERR error;
    BYTE *pixels;
    if (frame) return WINCODEC_ERR_FRAMEMISSING;
    if (FAILED(decoder->result)) return decoder->result;
    if (!decoder->pixels)
    {
        if (decoder->frame.height > SIZE_MAX / decoder->stride) return E_OUTOFMEMORY;
        pixels = calloc(decoder->frame.height, decoder->stride);
        if (!pixels) return E_OUTOFMEMORY;
        area.Width = decoder->frame.width;
        area.Height = decoder->frame.height;
        error = decoder->codec->Copy(decoder->codec, &area, pixels, decoder->stride);
        if (error)
        {
            free(pixels);
            return decoder->result = wmp_result(&decoder->stream, error);
        }
        decoder->pixels = pixels;
    }
    return copy_pixels(decoder->frame.bpp, decoder->pixels, decoder->frame.width,
            decoder->frame.height, decoder->stride, rect, stride, size, buffer);
}

static HRESULT CDECL wmp_decoder_get_metadata(struct decoder *iface, UINT frame,
        UINT *count, struct decoder_block **blocks)
{
    struct wmp_decoder *decoder = CONTAINING_RECORD(iface, struct wmp_decoder, iface);
    WmpDEMisc *metadata = &decoder->codec->WMP.wmiDEMisc;
    struct decoder_block *result;
    UINT used = 0;
    if (frame) return WINCODEC_ERR_FRAMEMISSING;
    result = calloc(2, sizeof(*result));
    if (!result) return E_OUTOFMEMORY;
    if (metadata->uEXIFMetadataOffset)
    {
        result[used].offset = metadata->uEXIFMetadataOffset;
        result[used].reader_clsid = CLSID_WICExifMetadataReader;
        ++used;
    }
    if (metadata->uGPSInfoMetadataOffset)
    {
        result[used].offset = metadata->uGPSInfoMetadataOffset;
        result[used].reader_clsid = CLSID_WICGpsMetadataReader;
        ++used;
    }
    *count = used;
    *blocks = result;
    while (used--) result[used].options = DECODER_BLOCK_FULL_STREAM | DECODER_BLOCK_READER_CLSID |
            WICPersistOptionLittleEndian | WICPersistOptionNoCacheStream;
    return S_OK;
}

static HRESULT CDECL wmp_decoder_get_color(struct decoder *iface, UINT frame, UINT index,
        BYTE **data, DWORD *size)
{
    struct wmp_decoder *decoder = CONTAINING_RECORD(iface, struct wmp_decoder, iface);
    U32 count = 0;
    BYTE *buffer;
    ERR error;
    if (frame || index >= decoder->frame.num_color_contexts) return WINCODEC_ERR_VALUEOUTOFRANGE;
    error = decoder->codec->GetColorContext(decoder->codec, NULL, &count);
    if (error) return wmp_result(&decoder->stream, error);
    buffer = malloc(count ? count : 1);
    if (!buffer) return E_OUTOFMEMORY;
    error = decoder->codec->GetColorContext(decoder->codec, buffer, &count);
    if (error)
    {
        free(buffer);
        return wmp_result(&decoder->stream, error);
    }
    *data = buffer;
    *size = count;
    return S_OK;
}

static void CDECL wmp_decoder_destroy(struct decoder *iface)
{
    struct wmp_decoder *decoder = CONTAINING_RECORD(iface, struct wmp_decoder, iface);
    if (decoder->codec) decoder->codec->Release(&decoder->codec);
    free(decoder->pixels);
    free(decoder);
}

static const struct decoder_funcs wmp_decoder_funcs =
{
    wmp_decoder_initialize, wmp_decoder_get_frame_info, wmp_decoder_get_palette,
    wmp_decoder_copy_pixels, wmp_decoder_get_metadata, wmp_decoder_get_color, wmp_decoder_destroy
};

HRESULT WmpDecoder_CreateInstance(REFIID iid, void **result)
{
    struct decoder_info info;
    struct wmp_decoder *decoder = calloc(1, sizeof(*decoder));
    if (!decoder) return E_OUTOFMEMORY;
    decoder->iface.vtable = &wmp_decoder_funcs;
    info.container_format = GUID_ContainerFormatWmp;
    info.block_format = GUID_ContainerFormatWmp;
    info.clsid = CLSID_WICWmpDecoder;
    return CommonDecoder_CreateInstance(&decoder->iface, &info, iid, result);
}
