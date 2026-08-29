/*
 * PROJECT:     ReactOS Core Audio
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Cross-process shared-mode render engine contract
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#pragma once

struct reactos_shared_render_client;

struct reactos_render_transport_ops
{
    HRESULT (*create)(DWORD endpoint_index, const WAVEFORMATEXTENSIBLE *format, void **transport);
    void (*destroy)(void *transport);
    HRESULT (*start)(void *transport);
    HRESULT (*stop)(void *transport);
    UINT32 (*writable_frames)(void *transport);
    UINT32 (*period_frames)(void *transport);
    BOOL (*queue_frames)(void *transport, const BYTE *data, UINT32 frames);
};

HRESULT
reactos_shared_render_create(
    DWORD endpoint_index,
    const WAVEFORMATEXTENSIBLE *format,
    UINT32 requested_buffer_frames,
    const struct reactos_render_transport_ops *transport_ops,
    struct reactos_shared_render_client **client,
    UINT32 *buffer_frames);
void
reactos_shared_render_release(struct reactos_shared_render_client *client);
HRESULT
reactos_shared_render_start(struct reactos_shared_render_client *client);
HRESULT
reactos_shared_render_stop(struct reactos_shared_render_client *client);
HRESULT
reactos_shared_render_reset(struct reactos_shared_render_client *client);
HRESULT
reactos_shared_render_write(struct reactos_shared_render_client *client, const BYTE *data, UINT32 frames);
HRESULT
reactos_shared_render_get_padding(struct reactos_shared_render_client *client, UINT32 *padding);
HRESULT
reactos_shared_render_get_position(struct reactos_shared_render_client *client, UINT64 *position, UINT64 *qpc_time);
