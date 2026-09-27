/*
 * PROJECT:     LiberNT Media Foundation decoders
 * FILE:        dll/win32/msmpeg2vdec/ffmpeg.c
 * PURPOSE:     Wine's decoder transform interface implemented with libavcodec
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "gst_private.h"

#include "mfapi.h"
#include "mferror.h"

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

/* Timestamps cross the interface in 100-nanosecond units. */
#define MF_TIME_BASE 10000000

struct transform
{
    CRITICAL_SECTION cs;
    struct wg_transform_attrs attrs;
    BOOL video;

    AVCodecContext *context;
    AVFrame *frame;
    BOOL frame_pending;
    BOOL draining;
    BOOL drain_sent;

    AVPacket **queue;
    UINT32 queue_size;
    UINT32 queue_head;
    UINT32 queue_count;

    GUID output_subtype;
    MFVideoInfo output_info;
    UINT32 stream_width;
    UINT32 stream_height;
    MFVideoArea stream_aperture;
    MFRatio stream_aspect;
    MFRatio frame_rate;

    SwrContext *converter;
    AVChannelLayout converter_layout;
    int converter_format;
    int converter_rate;
    AVChannelLayout output_layout;
    enum AVSampleFormat output_format;
    UINT32 output_rate;
    UINT32 block_align;
    BYTE *audio;
    UINT32 audio_capacity;
    UINT32 audio_size;
    UINT32 audio_offset;
    INT64 audio_pts;
};

static const GUID *const video_output_subtypes[] =
{
    &MFVideoFormat_NV12,
    &MFVideoFormat_YV12,
    &MFVideoFormat_IYUV,
    &MFVideoFormat_I420,
    &MFVideoFormat_YUY2,
};

static struct transform *get_transform(wg_transform_t handle)
{
    return (struct transform *)(UINT_PTR)handle;
}

static BYTE *wg_sample_data(struct wg_sample *sample)
{
    return (BYTE *)(UINT_PTR)sample->data;
}

static BOOL is_video_output_subtype(const GUID *subtype)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(video_output_subtypes); ++i)
        if (IsEqualGUID(subtype, video_output_subtypes[i]))
            return TRUE;
    return FALSE;
}

static BOOL is_h264_subtype(const GUID *subtype)
{
    return IsEqualGUID(subtype, &MFVideoFormat_H264) || IsEqualGUID(subtype, &MFVideoFormat_H264_ES);
}

static void clear_queue(struct transform *transform)
{
    while (transform->queue_count)
    {
        av_packet_free(&transform->queue[transform->queue_head]);
        transform->queue_head = (transform->queue_head + 1) % transform->queue_size;
        --transform->queue_count;
    }
    transform->queue_head = 0;
}

static void free_transform(struct transform *transform)
{
    if (transform->queue)
        clear_queue(transform);
    free(transform->queue);
    av_frame_free(&transform->frame);
    avcodec_free_context(&transform->context);
    swr_free(&transform->converter);
    av_channel_layout_uninit(&transform->converter_layout);
    av_channel_layout_uninit(&transform->output_layout);
    free(transform->audio);
    DeleteCriticalSection(&transform->cs);
    free(transform);
}

static HRESULT set_extradata(AVCodecContext *context, const BYTE *data, UINT32 size)
{
    if (!(context->extradata = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE)))
        return E_OUTOFMEMORY;
    memcpy(context->extradata, data, size);
    context->extradata_size = size;
    return S_OK;
}

/* MF_MT_MPEG_SEQUENCE_HEADER is Annex B; convert an avcC record so libavcodec
 * does not switch to length-prefixed input. */
static HRESULT set_h264_extradata(AVCodecContext *context, const BYTE *data, UINT32 size)
{
    UINT32 pos = 5, count, length, output_size = 0, i;
    unsigned int set;
    BYTE *output;
    HRESULT hr;

    if (size < 7 || data[0] != 1)
        return set_extradata(context, data, size);

    if (!(output = malloc(size + 2 * (31 + 255))))
        return E_OUTOFMEMORY;

    for (set = 0; set < 2 && pos < size; ++set)
    {
        count = set ? data[pos] : data[pos] & 0x1f;
        ++pos;
        for (i = 0; i < count; ++i)
        {
            if (pos + 2 > size || pos + 2 + (length = data[pos] << 8 | data[pos + 1]) > size)
            {
                WARN("Truncated avcC record.\n");
                free(output);
                return MF_E_INVALIDMEDIATYPE;
            }
            output[output_size++] = 0;
            output[output_size++] = 0;
            output[output_size++] = 0;
            output[output_size++] = 1;
            memcpy(output + output_size, data + pos + 2, length);
            output_size += length;
            pos += 2 + length;
        }
    }

    hr = set_extradata(context, output, output_size);
    free(output);
    return hr;
}

static HRESULT get_video_output(IMFMediaType *type, GUID *subtype, MFVideoInfo *info, MFRatio *aspect)
{
    UINT64 ratio;
    UINT32 size;
    HRESULT hr;

    memset(info, 0, sizeof(*info));
    if (FAILED(hr = IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, subtype)))
        return hr;
    if (!is_video_output_subtype(subtype))
    {
        WARN("Unsupported output subtype %s.\n", debugstr_guid(subtype));
        return MF_E_INVALIDMEDIATYPE;
    }

    if (SUCCEEDED(IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &ratio)))
    {
        info->dwWidth = ratio >> 32;
        info->dwHeight = (UINT32)ratio;
    }
    if (FAILED(IMFMediaType_GetBlob(type, &MF_MT_MINIMUM_DISPLAY_APERTURE, (BYTE *)&info->MinimumDisplayAperture,
            sizeof(info->MinimumDisplayAperture), &size)))
        memset(&info->MinimumDisplayAperture, 0, sizeof(info->MinimumDisplayAperture));

    aspect->Numerator = aspect->Denominator = 1;
    if (SUCCEEDED(IMFMediaType_GetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO, &ratio)) && (ratio >> 32) && (UINT32)ratio)
    {
        aspect->Numerator = ratio >> 32;
        aspect->Denominator = (UINT32)ratio;
    }
    return S_OK;
}

static HRESULT create_video(struct transform *transform, IMFMediaType *input_type, IMFMediaType *output_type)
{
    AVCodecContext *context;
    const AVCodec *codec;
    UINT32 header_size;
    GUID subtype;
    BYTE *header;
    UINT64 ratio;
    HRESULT hr;
    int ret;

    if (FAILED(hr = IMFMediaType_GetGUID(input_type, &MF_MT_SUBTYPE, &subtype)))
        return hr;
    if (!is_h264_subtype(&subtype))
    {
        FIXME("Unsupported input subtype %s.\n", debugstr_guid(&subtype));
        return MF_E_INVALIDMEDIATYPE;
    }
    if (FAILED(hr = get_video_output(output_type, &transform->output_subtype, &transform->output_info,
            &transform->stream_aspect)))
        return hr;

    transform->video = TRUE;
    transform->stream_width = transform->output_info.dwWidth;
    transform->stream_height = transform->output_info.dwHeight;
    transform->stream_aperture = transform->output_info.MinimumDisplayAperture;
    if (SUCCEEDED(IMFMediaType_GetUINT64(input_type, &MF_MT_FRAME_RATE, &ratio)) && (ratio >> 32) && (UINT32)ratio)
    {
        transform->frame_rate.Numerator = ratio >> 32;
        transform->frame_rate.Denominator = (UINT32)ratio;
    }

    if (!(codec = avcodec_find_decoder(AV_CODEC_ID_H264)))
        return MF_E_TOPO_CODEC_NOT_FOUND;
    if (!(transform->context = context = avcodec_alloc_context3(codec)))
        return E_OUTOFMEMORY;

    context->pkt_timebase = (AVRational){1, MF_TIME_BASE};
    /* Slice threads cannot split the usual one-slice-per-frame stream, so a
     * low-latency client still gets frame threads, limited to one frame of delay. */
    context->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    context->thread_count = transform->attrs.low_latency ? 2 : 0;
    if (IsEqualGUID(&subtype, &MFVideoFormat_H264_ES))
        context->flags2 |= AV_CODEC_FLAG2_CHUNKS;

    if (SUCCEEDED(IMFMediaType_GetAllocatedBlob(input_type, &MF_MT_MPEG_SEQUENCE_HEADER, &header, &header_size)))
    {
        hr = set_h264_extradata(context, header, header_size);
        CoTaskMemFree(header);
        if (FAILED(hr))
            return hr;
    }

    if ((ret = avcodec_open2(context, codec, NULL)) < 0)
    {
        ERR("Failed to open the H.264 decoder, error %d.\n", ret);
        return E_FAIL;
    }

    TRACE("Created H.264 decoder %p, output %s %ux%u, %d threads, type %#x.\n", transform,
            debugstr_guid(&transform->output_subtype), transform->stream_width, transform->stream_height,
            context->thread_count, context->active_thread_type);
    return S_OK;
}

static HRESULT set_audio_output(struct transform *transform, IMFMediaType *type)
{
    enum AVSampleFormat format;
    UINT32 channels, rate, bits, mask;
    GUID subtype;
    HRESULT hr;

    if (FAILED(hr = IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype)))
        return hr;
    if (FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, &channels)) || !channels
            || FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate)) || !rate)
        return MF_E_INVALIDMEDIATYPE;
    if (FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)))
        bits = 0;

    if (IsEqualGUID(&subtype, &MFAudioFormat_Float) && (!bits || bits == 32))
        format = AV_SAMPLE_FMT_FLT, bits = 32;
    else if (IsEqualGUID(&subtype, &MFAudioFormat_PCM) && (!bits || bits == 16))
        format = AV_SAMPLE_FMT_S16, bits = 16;
    else if (IsEqualGUID(&subtype, &MFAudioFormat_PCM) && bits == 32)
        format = AV_SAMPLE_FMT_S32;
    else
    {
        WARN("Unsupported output subtype %s, %u bits.\n", debugstr_guid(&subtype), bits);
        return MF_E_INVALIDMEDIATYPE;
    }

    av_channel_layout_uninit(&transform->output_layout);
    if (FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_CHANNEL_MASK, &mask)) || !mask
            || av_channel_layout_from_mask(&transform->output_layout, mask) < 0
            || transform->output_layout.nb_channels != channels)
    {
        av_channel_layout_uninit(&transform->output_layout);
        av_channel_layout_default(&transform->output_layout, channels);
    }

    transform->output_format = format;
    transform->output_rate = rate;
    transform->block_align = channels * bits / 8;
    swr_free(&transform->converter);
    transform->audio_size = transform->audio_offset = 0;
    return S_OK;
}

static HRESULT create_audio(struct transform *transform, IMFMediaType *input_type, IMFMediaType *output_type)
{
    UINT32 payload = 0, channels = 0, rate = 0, size = 0, config = 0;
    enum AVCodecID codec_id = AV_CODEC_ID_AAC;
    AVCodecContext *context = NULL;
    const AVCodec *codec = NULL;
    BYTE *user_data;
    GUID subtype;
    HRESULT hr;
    int ret;

    if (FAILED(hr = IMFMediaType_GetGUID(input_type, &MF_MT_SUBTYPE, &subtype)))
        return hr;
    if (FAILED(hr = set_audio_output(transform, output_type)))
        return hr;

    IMFMediaType_GetUINT32(input_type, &MF_MT_AUDIO_NUM_CHANNELS, &channels);
    IMFMediaType_GetUINT32(input_type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    if (FAILED(IMFMediaType_GetAllocatedBlob(input_type, &MF_MT_USER_DATA, &user_data, &size)))
    {
        user_data = NULL;
        size = 0;
    }

    if (IsEqualGUID(&subtype, &MFAudioFormat_AAC))
    {
        /* The user data holds the HEAACWAVEINFO fields that follow WAVEFORMATEX,
         * then the AudioSpecificConfig. */
        config = min(size, sizeof(HEAACWAVEINFO) - sizeof(WAVEFORMATEX));
        if (FAILED(IMFMediaType_GetUINT32(input_type, &MF_MT_AAC_PAYLOAD_TYPE, &payload)) && size >= 2)
            payload = user_data[0] | user_data[1] << 8;
    }
    else if (IsEqualGUID(&subtype, &MFAudioFormat_ADTS))
        payload = 1;
    else if (!IsEqualGUID(&subtype, &MFAudioFormat_RAW_AAC))
        hr = MF_E_INVALIDMEDIATYPE;

    if (SUCCEEDED(hr) && payload == 3)
        codec_id = AV_CODEC_ID_AAC_LATM;
    else if (SUCCEEDED(hr) && payload > 1)
        hr = MF_E_INVALIDMEDIATYPE;

    if (FAILED(hr))
        FIXME("Unsupported input subtype %s, payload type %u.\n", debugstr_guid(&subtype), payload);
    else if (!(codec = avcodec_find_decoder(codec_id)))
        hr = MF_E_TOPO_CODEC_NOT_FOUND;
    else if (!(transform->context = context = avcodec_alloc_context3(codec)))
        hr = E_OUTOFMEMORY;
    else
    {
        context->pkt_timebase = (AVRational){1, MF_TIME_BASE};
        if (rate)
            context->sample_rate = rate;
        if (channels)
            av_channel_layout_default(&context->ch_layout, channels);
        if (!payload && size > config)
            hr = set_extradata(context, user_data + config, size - config);
    }
    CoTaskMemFree(user_data);
    if (FAILED(hr))
        return hr;

    if ((ret = avcodec_open2(context, codec, NULL)) < 0)
    {
        ERR("Failed to open the AAC decoder, error %d.\n", ret);
        return E_FAIL;
    }

    TRACE("Created AAC decoder %p, payload %u, %u Hz, %u channels.\n", transform, payload, rate, channels);
    return S_OK;
}

HRESULT wg_transform_create_mf(IMFMediaType *input_type, IMFMediaType *output_type,
        const struct wg_transform_attrs *attrs, wg_transform_t *out)
{
    struct transform *transform;
    GUID major;
    HRESULT hr;

    TRACE("input_type %p, output_type %p.\n", input_type, output_type);

    *out = 0;
    if (FAILED(hr = IMFMediaType_GetMajorType(input_type, &major)))
        return hr;
    if (!(transform = calloc(1, sizeof(*transform))))
        return E_OUTOFMEMORY;

    InitializeCriticalSection(&transform->cs);
    transform->attrs = *attrs;
    transform->queue_size = attrs->input_queue_length + 1;
    transform->audio_pts = AV_NOPTS_VALUE;

    if (!(transform->queue = calloc(transform->queue_size, sizeof(*transform->queue)))
            || !(transform->frame = av_frame_alloc()))
        hr = E_OUTOFMEMORY;
    else if (IsEqualGUID(&major, &MFMediaType_Video))
        hr = create_video(transform, input_type, output_type);
    else if (IsEqualGUID(&major, &MFMediaType_Audio))
        hr = create_audio(transform, input_type, output_type);
    else
        hr = MF_E_INVALIDMEDIATYPE;

    if (FAILED(hr))
    {
        WARN("Failed to create transform, hr %#lx.\n", hr);
        free_transform(transform);
        return hr;
    }

    *out = (wg_transform_t)(UINT_PTR)transform;
    return S_OK;
}

void wg_transform_destroy(wg_transform_t handle)
{
    TRACE("transform %#I64x.\n", handle);

    free_transform(get_transform(handle));
}

HRESULT wg_transform_push_data(wg_transform_t handle, struct wg_sample *sample)
{
    struct transform *transform = get_transform(handle);
    AVPacket *packet;
    HRESULT hr = S_OK;

    TRACE("transform %p, sample %p, size %u, flags %#x.\n", transform, sample, sample->size, sample->flags);

    EnterCriticalSection(&transform->cs);

    if (transform->draining || transform->queue_count == transform->queue_size)
        hr = MF_E_NOTACCEPTING;
    else if (sample->size)
    {
        if (!(packet = av_packet_alloc()) || av_new_packet(packet, sample->size) < 0)
        {
            av_packet_free(&packet);
            hr = E_OUTOFMEMORY;
        }
        else
        {
            memcpy(packet->data, wg_sample_data(sample), sample->size);
            if (sample->flags & WG_SAMPLE_FLAG_HAS_PTS)
                packet->pts = sample->pts;
            if (sample->flags & WG_SAMPLE_FLAG_HAS_DURATION)
                packet->duration = sample->duration;
            if (sample->flags & WG_SAMPLE_FLAG_SYNC_POINT)
                packet->flags |= AV_PKT_FLAG_KEY;
            transform->queue[(transform->queue_head + transform->queue_count++) % transform->queue_size] = packet;
        }
    }

    LeaveCriticalSection(&transform->cs);
    return hr;
}

/* Feeds queued input to the decoder until it returns a frame. */
static HRESULT receive_frame(struct transform *transform)
{
    unsigned int errors = 0;
    AVPacket *packet;
    int ret;

    if (transform->frame_pending)
        return S_OK;

    while (errors <= 32)
    {
        if (!(ret = avcodec_receive_frame(transform->context, transform->frame)))
        {
            transform->frame_pending = TRUE;
            return S_OK;
        }
        if (ret == AVERROR_EOF)
        {
            avcodec_flush_buffers(transform->context);
            transform->draining = transform->drain_sent = FALSE;
            break;
        }
        if (ret != AVERROR(EAGAIN))
        {
            WARN("Failed to decode, error %d.\n", ret);
            ++errors;
            continue;
        }

        if (transform->queue_count)
        {
            packet = transform->queue[transform->queue_head];
            if ((ret = avcodec_send_packet(transform->context, packet)) == AVERROR(EAGAIN))
            {
                ++errors;
                continue;
            }
            if (ret < 0)
                WARN("Failed to send %d bytes, error %d.\n", packet->size, ret);
            av_packet_free(&transform->queue[transform->queue_head]);
            transform->queue_head = (transform->queue_head + 1) % transform->queue_size;
            --transform->queue_count;
        }
        else if (transform->draining && !transform->drain_sent)
        {
            avcodec_send_packet(transform->context, NULL);
            transform->drain_sent = TRUE;
        }
        else
            break;
    }

    return MF_E_TRANSFORM_NEED_MORE_INPUT;
}

static void release_frame(struct transform *transform)
{
    av_frame_unref(transform->frame);
    transform->frame_pending = FALSE;
}

/* Returns TRUE if the decoded geometry differs from the one last reported. */
static BOOL update_stream_geometry(struct transform *transform, const AVFrame *frame)
{
    UINT32 align = transform->attrs.output_plane_align;
    UINT32 width = (frame->width + align) & ~align, height = (frame->height + align) & ~align;
    MFVideoArea aperture = {{0}};
    MFRatio aspect = {1, 1};

    if (width != (UINT32)frame->width || height != (UINT32)frame->height)
    {
        aperture.Area.cx = frame->width;
        aperture.Area.cy = frame->height;
    }
    if (frame->sample_aspect_ratio.num > 0 && frame->sample_aspect_ratio.den > 0)
    {
        aspect.Numerator = frame->sample_aspect_ratio.num;
        aspect.Denominator = frame->sample_aspect_ratio.den;
    }

    if (width == transform->stream_width && height == transform->stream_height
            && !memcmp(&aperture, &transform->stream_aperture, sizeof(aperture))
            && (UINT64)aspect.Numerator * transform->stream_aspect.Denominator
            == (UINT64)aspect.Denominator * transform->stream_aspect.Numerator)
        return FALSE;

    TRACE("Stream geometry changed to %ux%u, visible %dx%d, aspect %u:%u.\n", width, height,
            frame->width, frame->height, aspect.Numerator, aspect.Denominator);

    transform->stream_width = width;
    transform->stream_height = height;
    transform->stream_aperture = aperture;
    transform->stream_aspect = aspect;
    if (!transform->frame_rate.Numerator && transform->context->framerate.num > 0
            && transform->context->framerate.den > 0)
    {
        transform->frame_rate.Numerator = transform->context->framerate.num;
        transform->frame_rate.Denominator = transform->context->framerate.den;
    }
    return TRUE;
}

static void interleave_chroma_row(BYTE *dst, const BYTE *u, const BYTE *v, UINT32 width)
{
    UINT32 i;

    for (i = 0; i < width; ++i)
    {
        dst[2 * i] = u[i];
        dst[2 * i + 1] = v[i];
    }
}

static void pack_yuy2_row(BYTE *dst, const BYTE *y, const BYTE *u, const BYTE *v, UINT32 width)
{
    UINT32 i;

    for (i = 0; i + 1 < width; i += 2, dst += 4)
    {
        dst[0] = y[i];
        dst[1] = u[i / 2];
        dst[2] = y[i + 1];
        dst[3] = v[i / 2];
    }
    if (width & 1)
    {
        dst[0] = dst[2] = y[width - 1];
        dst[1] = u[width / 2];
        dst[3] = v[width / 2];
    }
}

/* Lays out planes like Wine's GStreamer transform: rows padded to the plane
 * alignment, plus any padding the output type's display aperture requests. */
static HRESULT copy_video_frame(struct transform *transform, const AVFrame *frame, struct wg_sample *sample)
{
    const MFVideoArea *aperture = &transform->output_info.MinimumDisplayAperture;
    BOOL yuy2 = IsEqualGUID(&transform->output_subtype, &MFVideoFormat_YUY2);
    BOOL nv12 = IsEqualGUID(&transform->output_subtype, &MFVideoFormat_NV12);
    UINT32 align = transform->attrs.output_plane_align, width = frame->width, height = frame->height;
    UINT32 chroma_width = (width + 1) / 2, chroma_height = (height + 1) / 2;
    UINT32 plane_width = (width + align) & ~align, plane_height = (height + align) & ~align;
    UINT32 pitch, chroma_pitch, chroma_rows, size, row;
    BYTE *data = wg_sample_data(sample), *u_plane, *v_plane;
    LONG extra;

    if (!is_mf_video_area_empty(aperture))
    {
        if ((extra = (LONG)transform->output_info.dwWidth - aperture->OffsetX.value - aperture->Area.cx) > 0)
            plane_width = max(plane_width, width + extra);
        if ((extra = (LONG)transform->output_info.dwHeight - aperture->OffsetY.value - aperture->Area.cy) > 0)
            plane_height = max(plane_height, height + extra);
    }

    if (sample->stride < 0)
    {
        FIXME("Bottom-up output is not supported.\n");
        return E_NOTIMPL;
    }
    if (sample->stride)
        pitch = sample->stride;
    else
        pitch = ((yuy2 ? plane_width * 2 : plane_width) + align) & ~align;

    chroma_rows = (plane_height + 1) / 2;
    chroma_pitch = nv12 ? pitch : pitch / 2;
    if (yuy2)
        size = pitch * plane_height;
    else if (nv12)
        size = pitch * (plane_height + chroma_rows);
    else
        size = pitch * plane_height + 2 * chroma_pitch * chroma_rows;

    if (pitch < (yuy2 ? width * 2 : width) || (!yuy2 && chroma_pitch < (nv12 ? chroma_width * 2 : chroma_width))
            || sample->max_size < size)
    {
        ERR("Output buffer is too small, pitch %u, size %u, need %ux%u.\n", pitch, sample->max_size, width, height);
        return MF_E_BUFFERTOOSMALL;
    }

    if (yuy2)
    {
        for (row = 0; row < plane_height; ++row)
        {
            UINT32 src = min(row, height - 1);

            pack_yuy2_row(data + row * pitch, frame->data[0] + src * frame->linesize[0],
                    frame->data[1] + (src / 2) * frame->linesize[1],
                    frame->data[2] + (src / 2) * frame->linesize[2], width);
        }
    }
    else
    {
        for (row = 0; row < plane_height; ++row)
            memcpy(data + row * pitch, frame->data[0] + min(row, height - 1) * frame->linesize[0], width);

        u_plane = data + pitch * plane_height;
        for (row = 0; nv12 && row < chroma_rows; ++row)
        {
            UINT32 src = min(row, chroma_height - 1);

            interleave_chroma_row(u_plane + row * pitch, frame->data[1] + src * frame->linesize[1],
                    frame->data[2] + src * frame->linesize[2], chroma_width);
        }

        v_plane = u_plane + chroma_pitch * chroma_rows;
        if (IsEqualGUID(&transform->output_subtype, &MFVideoFormat_YV12))
        {
            BYTE *plane = u_plane;
            u_plane = v_plane;
            v_plane = plane;
        }
        for (row = 0; !nv12 && row < chroma_rows; ++row)
        {
            UINT32 src = min(row, chroma_height - 1);

            memcpy(u_plane + row * chroma_pitch, frame->data[1] + src * frame->linesize[1], chroma_width);
            memcpy(v_plane + row * chroma_pitch, frame->data[2] + src * frame->linesize[2], chroma_width);
        }
    }

    sample->size = size;
    return S_OK;
}

static HRESULT read_video(struct transform *transform, struct wg_sample *sample)
{
    const AVFrame *frame = transform->frame;
    HRESULT hr;

    if (FAILED(hr = receive_frame(transform)))
        return hr;

    if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P)
    {
        FIXME("Unsupported pixel format %d.\n", frame->format);
        release_frame(transform);
        return E_FAIL;
    }

    if (update_stream_geometry(transform, frame) && transform->attrs.allow_format_change)
        return MF_E_TRANSFORM_STREAM_CHANGE;

    if (FAILED(hr = copy_video_frame(transform, frame, sample)))
        return hr;

    if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
    {
        sample->pts = frame->best_effort_timestamp;
        sample->flags |= WG_SAMPLE_FLAG_HAS_PTS;
        if (transform->attrs.preserve_timestamps)
            sample->flags |= WG_SAMPLE_FLAG_PRESERVE_TIMESTAMPS;
    }
    if (frame->duration > 0)
    {
        sample->duration = frame->duration;
        sample->flags |= WG_SAMPLE_FLAG_HAS_DURATION;
    }
    sample->flags |= WG_SAMPLE_FLAG_SYNC_POINT;

    release_frame(transform);
    return S_OK;
}

static HRESULT convert_audio_frame(struct transform *transform, const AVFrame *frame)
{
    UINT32 capacity;
    uint8_t *output;
    int ret, samples;

    if (!transform->converter || frame->format != transform->converter_format
            || frame->sample_rate != transform->converter_rate
            || av_channel_layout_compare(&frame->ch_layout, &transform->converter_layout))
    {
        swr_free(&transform->converter);
        if ((ret = swr_alloc_set_opts2(&transform->converter, &transform->output_layout, transform->output_format,
                transform->output_rate, &frame->ch_layout, frame->format, frame->sample_rate, 0, NULL)) < 0
                || (ret = swr_init(transform->converter)) < 0)
        {
            ERR("Failed to convert %d Hz, %d channels, format %d, error %d.\n", frame->sample_rate,
                    frame->ch_layout.nb_channels, frame->format, ret);
            swr_free(&transform->converter);
            return E_FAIL;
        }
        av_channel_layout_uninit(&transform->converter_layout);
        av_channel_layout_copy(&transform->converter_layout, &frame->ch_layout);
        transform->converter_format = frame->format;
        transform->converter_rate = frame->sample_rate;
    }

    if ((samples = swr_get_out_samples(transform->converter, frame->nb_samples)) < 0)
        return E_FAIL;
    capacity = samples * transform->block_align;
    if (capacity > transform->audio_capacity)
    {
        if (!(output = realloc(transform->audio, capacity)))
            return E_OUTOFMEMORY;
        transform->audio = output;
        transform->audio_capacity = capacity;
    }

    output = transform->audio;
    if ((samples = swr_convert(transform->converter, &output, samples,
            (const uint8_t * const *)frame->extended_data, frame->nb_samples)) < 0)
    {
        ERR("Failed to convert audio, error %d.\n", samples);
        return E_FAIL;
    }

    transform->audio_size = samples * transform->block_align;
    transform->audio_offset = 0;
    transform->audio_pts = frame->best_effort_timestamp;
    return S_OK;
}

static HRESULT read_audio(struct transform *transform, struct wg_sample *sample)
{
    UINT32 size, offset;
    HRESULT hr;

    while (transform->audio_offset == transform->audio_size)
    {
        if (FAILED(hr = receive_frame(transform)))
            return hr;
        hr = convert_audio_frame(transform, transform->frame);
        release_frame(transform);
        if (FAILED(hr))
            return hr;
    }

    offset = transform->audio_offset;
    size = transform->audio_size - offset;
    if (size > sample->max_size)
    {
        if (!(size = sample->max_size - sample->max_size % transform->block_align))
            size = sample->max_size;
        sample->flags |= WG_SAMPLE_FLAG_INCOMPLETE;
    }
    memcpy(wg_sample_data(sample), transform->audio + offset, size);
    sample->size = size;
    transform->audio_offset += size;

    if (transform->audio_pts != AV_NOPTS_VALUE)
    {
        sample->pts = transform->audio_pts
                + (INT64)(offset / transform->block_align) * MF_TIME_BASE / transform->output_rate;
        sample->flags |= WG_SAMPLE_FLAG_HAS_PTS;
    }
    sample->duration = (UINT64)(size / transform->block_align) * MF_TIME_BASE / transform->output_rate;
    sample->flags |= WG_SAMPLE_FLAG_HAS_DURATION | WG_SAMPLE_FLAG_SYNC_POINT;
    return S_OK;
}

HRESULT wg_transform_read_data(wg_transform_t handle, struct wg_sample *sample)
{
    struct transform *transform = get_transform(handle);
    HRESULT hr;

    TRACE("transform %p, sample %p, max_size %u, stride %d.\n", transform, sample, sample->max_size, sample->stride);

    EnterCriticalSection(&transform->cs);
    if (transform->video)
        hr = read_video(transform, sample);
    else
        hr = read_audio(transform, sample);
    LeaveCriticalSection(&transform->cs);

    if (hr != S_OK)
        sample->size = 0;
    return hr;
}

bool wg_transform_get_status(wg_transform_t handle, bool *accepts_input)
{
    struct transform *transform = get_transform(handle);

    EnterCriticalSection(&transform->cs);
    *accepts_input = transform->queue_count < transform->queue_size;
    LeaveCriticalSection(&transform->cs);
    return true;
}

HRESULT wg_transform_get_output_type(wg_transform_t handle, IMFMediaType **media_type)
{
    struct transform *transform = get_transform(handle);
    WAVEFORMATEXTENSIBLE wfx = {{0}};
    MFVIDEOFORMAT format = {0};
    UINT32 bits;
    HRESULT hr;

    TRACE("transform %p, media_type %p.\n", transform, media_type);

    EnterCriticalSection(&transform->cs);

    if (transform->video)
    {
        format.dwSize = sizeof(format);
        format.guidFormat = transform->output_subtype;
        format.videoInfo.dwWidth = transform->stream_width;
        format.videoInfo.dwHeight = transform->stream_height;
        format.videoInfo.PixelAspectRatio = transform->stream_aspect;
        format.videoInfo.FramesPerSecond = transform->frame_rate;
        format.videoInfo.MinimumDisplayAperture = transform->stream_aperture;
        format.videoInfo.GeometricAperture = transform->stream_aperture;
        format.videoInfo.PanScanAperture = transform->stream_aperture;
        hr = MFCreateVideoMediaType(&format, (IMFVideoMediaType **)media_type);
    }
    else
    {
        bits = transform->block_align / transform->output_layout.nb_channels * 8;
        wfx.Format.wFormatTag = transform->output_format == AV_SAMPLE_FMT_FLT
                ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
        wfx.Format.nChannels = transform->output_layout.nb_channels;
        wfx.Format.nSamplesPerSec = transform->output_rate;
        wfx.Format.wBitsPerSample = bits;
        wfx.Format.nBlockAlign = transform->block_align;
        wfx.Format.nAvgBytesPerSec = transform->output_rate * transform->block_align;
        if (wfx.Format.nChannels > 2)
        {
            wfx.SubFormat = transform->output_format == AV_SAMPLE_FMT_FLT ? MFAudioFormat_Float : MFAudioFormat_PCM;
            wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            wfx.Format.cbSize = sizeof(wfx) - sizeof(wfx.Format);
            wfx.Samples.wValidBitsPerSample = bits;
            if (transform->output_layout.order == AV_CHANNEL_ORDER_NATIVE)
                wfx.dwChannelMask = transform->output_layout.u.mask;
        }
        hr = MFCreateAudioMediaType(&wfx.Format, (IMFAudioMediaType **)media_type);
    }

    LeaveCriticalSection(&transform->cs);
    return hr;
}

HRESULT wg_transform_set_output_type(wg_transform_t handle, IMFMediaType *media_type)
{
    struct transform *transform = get_transform(handle);
    MFVideoInfo info;
    MFRatio aspect;
    GUID subtype;
    HRESULT hr;

    TRACE("transform %p, media_type %p.\n", transform, media_type);

    EnterCriticalSection(&transform->cs);

    if (!transform->video)
        hr = set_audio_output(transform, media_type);
    else if (SUCCEEDED(hr = get_video_output(media_type, &subtype, &info, &aspect)))
    {
        transform->output_subtype = subtype;
        transform->output_info = info;
        if (!transform->attrs.allow_format_change)
        {
            transform->stream_width = info.dwWidth;
            transform->stream_height = info.dwHeight;
            transform->stream_aperture = info.MinimumDisplayAperture;
            transform->stream_aspect = aspect;
        }
    }

    LeaveCriticalSection(&transform->cs);
    return hr;
}

HRESULT wg_transform_drain(wg_transform_t handle)
{
    struct transform *transform = get_transform(handle);

    TRACE("transform %p.\n", transform);

    EnterCriticalSection(&transform->cs);
    transform->draining = TRUE;
    LeaveCriticalSection(&transform->cs);
    return S_OK;
}

HRESULT wg_transform_flush(wg_transform_t handle)
{
    struct transform *transform = get_transform(handle);

    TRACE("transform %p.\n", transform);

    EnterCriticalSection(&transform->cs);
    clear_queue(transform);
    avcodec_flush_buffers(transform->context);
    release_frame(transform);
    transform->draining = transform->drain_sent = FALSE;
    swr_free(&transform->converter);
    transform->audio_size = transform->audio_offset = 0;
    LeaveCriticalSection(&transform->cs);
    return S_OK;
}

void wg_transform_notify_qos(wg_transform_t handle,
        bool underflow, double proportion, int64_t diff, uint64_t timestamp)
{
    TRACE("transform %#I64x, underflow %d, proportion %.16e, diff %I64d, timestamp %I64u.\n",
            handle, underflow, proportion, diff, timestamp);
}

HRESULT check_video_transform_support(const MFVIDEOFORMAT *input, const MFVIDEOFORMAT *output)
{
    if (!is_h264_subtype(&input->guidFormat) || !is_video_output_subtype(&output->guidFormat))
        return MF_E_INVALIDMEDIATYPE;
    return avcodec_find_decoder(AV_CODEC_ID_H264) ? S_OK : MF_E_TOPO_CODEC_NOT_FOUND;
}

HRESULT check_audio_transform_support(const WAVEFORMATEX *input, const WAVEFORMATEX *output)
{
    if (input->wFormatTag != WAVE_FORMAT_MPEG_HEAAC && input->wFormatTag != WAVE_FORMAT_RAW_AAC1
            && input->wFormatTag != WAVE_FORMAT_MPEG_ADTS_AAC)
        return MF_E_INVALIDMEDIATYPE;
    return avcodec_find_decoder(AV_CODEC_ID_AAC) ? S_OK : MF_E_TOPO_CODEC_NOT_FOUND;
}
