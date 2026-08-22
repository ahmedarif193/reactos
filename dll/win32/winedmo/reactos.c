/*
 * PROJECT:     ReactOS Wine DMO compatibility layer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native FFmpeg adapter for Wine's winedmo demuxer
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* Wine normally uses a Unix-call boundary. ReactOS invokes the same upstream demuxer callbacks in-process. */

#include "config.h"
#include "unix_private.h"

#include <errno.h>
#include <stdio.h>

WINE_DEFAULT_DEBUG_CHANNEL(dmo);

int64_t unix_seek_callback(void *opaque, int64_t offset, int whence)
{
    struct stream_context *context = opaque;
    struct winedmo_stream *stream = (struct winedmo_stream *)(UINT_PTR)context->stream;
    UINT64 position;
    NTSTATUS status;

    if (whence == AVSEEK_SIZE) return context->length;
    if (whence == SEEK_END) offset += context->length;
    if (whence == SEEK_CUR) offset += context->position;
    if (offset < 0) offset = 0;
    if ((UINT64)offset > context->length) offset = context->length;

    if ((UINT64)offset / context->capacity != context->position / context->capacity)
    {
        if (!stream->p_seek) return AVERROR(EINVAL);
        position = ((UINT64)offset / context->capacity) * context->capacity;
        status = stream->p_seek(stream, &position);
        if (status || position != ((UINT64)offset / context->capacity) * context->capacity) return AVERROR(EINVAL);
        context->size = 0;
    }

    context->position = offset;
    return offset;
}

static int stream_context_read(struct stream_context *context)
{
    struct winedmo_stream *stream = (struct winedmo_stream *)(UINT_PTR)context->stream;
    ULONG size = context->capacity;
    NTSTATUS status;

    if (!stream->p_read) return AVERROR(EINVAL);
    status = stream->p_read(stream, context->buffer, &size);
    if (status) return AVERROR(EINVAL);
    context->size = size;
    return 0;
}

int unix_read_callback(void *opaque, uint8_t *buffer, int size)
{
    struct stream_context *context = opaque;
    int result;
    int total = 0;

    if (!(size = min((UINT64)size, context->length - context->position))) return AVERROR_EOF;

    while (size)
    {
        int buffer_offset = context->position % context->capacity;
        int step;

        if (!context->size && (result = stream_context_read(context)) < 0) return result;
        if (!(step = min(size, context->size - buffer_offset))) break;
        memcpy(buffer, context->buffer + buffer_offset, step);
        buffer += step;
        total += step;
        size -= step;
        context->position += step;
        if (!(context->position % context->capacity)) context->size = 0;
    }

    return total ? total : AVERROR_EOF;
}

static void vlog(void *context, int level, const char *format, va_list args)
{
    enum __wine_debug_class debug_class = __WINE_DBCL_TRACE;

    UNREFERENCED_PARAMETER(context);
    if (level <= AV_LOG_WARNING) debug_class = __WINE_DBCL_WARN;
    if (level <= AV_LOG_ERROR) debug_class = __WINE_DBCL_ERR;
    wine_dbg_vlog(debug_class, __wine_dbch___default, __func__, format, args);
}

NTSTATUS process_attach(void *params)
{
    UNREFERENCED_PARAMETER(params);
    av_log_set_callback(vlog);
    TRACE("FFmpeg avformat version %u, avcodec version %u\n", avformat_version(), avcodec_version());
    return STATUS_SUCCESS;
}
