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

#ifndef __DXVA2_DECODER_H
#define __DXVA2_DECODER_H

HRESULT dxva2_decoder_get_device_guids(IDirect3DDevice9 *device, UINT *count, GUID **guids);
HRESULT dxva2_decoder_get_render_targets(IDirect3DDevice9 *device, REFGUID guid,
        UINT *count, D3DFORMAT **formats);
HRESULT dxva2_decoder_get_configurations(IDirect3DDevice9 *device, REFGUID guid,
        const DXVA2_VideoDesc *video_desc, UINT *count, DXVA2_ConfigPictureDecode **configs);
HRESULT dxva2_decoder_create(IDirectXVideoDecoderService *service, IDirect3DDevice9 *device,
        REFGUID guid, const DXVA2_VideoDesc *video_desc, const DXVA2_ConfigPictureDecode *config,
        IDirect3DSurface9 **render_targets, UINT surface_count, IDirectXVideoDecoder **decoder);

#endif /* __DXVA2_DECODER_H */
