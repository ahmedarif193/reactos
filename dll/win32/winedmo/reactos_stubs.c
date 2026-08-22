/*
 * PROJECT:     ReactOS Wine DMO compatibility layer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Provide an unavailable native media backend without target FFmpeg
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "reactos.h"

#define DEFINE_BACKEND_STUB(name) \
    NTSTATUS name(void *params) \
    { \
        UNREFERENCED_PARAMETER(params); \
        return STATUS_PROCEDURE_NOT_FOUND; \
    }

DEFINE_BACKEND_STUB(process_attach)
DEFINE_BACKEND_STUB(demuxer_check)
DEFINE_BACKEND_STUB(demuxer_create)
DEFINE_BACKEND_STUB(demuxer_destroy)
DEFINE_BACKEND_STUB(demuxer_read)
DEFINE_BACKEND_STUB(demuxer_seek)
DEFINE_BACKEND_STUB(demuxer_stream_lang)
DEFINE_BACKEND_STUB(demuxer_stream_name)
DEFINE_BACKEND_STUB(demuxer_stream_type)
