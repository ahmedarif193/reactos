/*
 * PROJECT:     ReactOS Wine DMO compatibility layer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native FFmpeg backend declarations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#pragma once

#include "unixlib.h"

extern NTSTATUS process_attach(void *params);
extern NTSTATUS demuxer_check(void *params);
extern NTSTATUS demuxer_create(void *params);
extern NTSTATUS demuxer_destroy(void *params);
extern NTSTATUS demuxer_read(void *params);
extern NTSTATUS demuxer_seek(void *params);
extern NTSTATUS demuxer_stream_lang(void *params);
extern NTSTATUS demuxer_stream_name(void *params);
extern NTSTATUS demuxer_stream_type(void *params);

#undef UNIX_CALL
#define UNIX_CALL(func, params) func(params)
