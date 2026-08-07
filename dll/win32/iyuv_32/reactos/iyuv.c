/*
 * iyuv Video "Decoder" (ReactOS native backend)
 * Copyright 2026 Brendan McGrath for CodeWeavers
 * Copyright 2026 ReactOS Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */

#include <stdarg.h>
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "vfw.h"

#include "iyuv_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(iyuv_32);

static HINSTANCE IYUV_32_module;

#define FOURCC_I420 mmioFOURCC('I', '4', '2', '0')
#define FOURCC_IYUV mmioFOURCC('I', 'Y', 'U', 'V')
#define compare_fourcc(fcc1, fcc2) (((fcc1) ^ (fcc2)) & ~0x20202020)

static inline BYTE clamp_component(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return value;
}

static void convert_pixel(BYTE y, BYTE u, BYTE v, BYTE *red, BYTE *green, BYTE *blue)
{
    int c = (int)y - 16;
    int d = (int)u - 128;
    int e = (int)v - 128;

    *red = clamp_component((1192 * c + 1634 * e) / 1024);
    *green = clamp_component((1192 * c - 400 * d - 833 * e) / 1024);
    *blue = clamp_component((1192 * c + 2066 * d) / 1024);
}

static LRESULT IYUV_Open(const ICINFO *icinfo)
{
    TRACE("DRV_OPEN %p\n", icinfo);

    if (icinfo && compare_fourcc(icinfo->fccType, ICTYPE_VIDEO))
        return 0;

    return 1;
}

static LRESULT IYUV_DecompressQuery(const BITMAPINFOHEADER *in, const BITMAPINFOHEADER *out)
{
    if (!in || (compare_fourcc(in->biCompression, FOURCC_I420) && compare_fourcc(in->biCompression, FOURCC_IYUV)))
        return ICERR_BADFORMAT;
    if (!in->biHeight || !in->biWidth)
        return ICERR_BADFORMAT;

    if (out)
    {
        if (out->biCompression != BI_RGB)
            return ICERR_BADFORMAT;
        if (out->biBitCount != 24 && out->biBitCount != 16 && out->biBitCount != 8)
            return ICERR_BADFORMAT;
        if (in->biWidth != out->biWidth || in->biHeight != out->biHeight)
            return ICERR_BADFORMAT;
    }

    return ICERR_OK;
}

static LRESULT IYUV_DecompressGetFormat(BITMAPINFOHEADER *in, BITMAPINFOHEADER *out)
{
    if (!in || (compare_fourcc(in->biCompression, FOURCC_I420) && compare_fourcc(in->biCompression, FOURCC_IYUV)))
        return ICERR_BADFORMAT;

    if (!out)
        return sizeof(*out);

    memset(out, 0, sizeof(*out));
    out->biSize = sizeof(*out);
    out->biWidth = in->biWidth;
    out->biHeight = abs(in->biHeight);
    out->biCompression = BI_RGB;
    out->biPlanes = 1;
    out->biBitCount = 24;
    out->biSizeImage = out->biWidth * out->biHeight * 3;
    return ICERR_OK;
}

static LRESULT IYUV_DecompressBegin(const BITMAPINFOHEADER *in, const BITMAPINFOHEADER *out)
{
    return IYUV_DecompressQuery(in, out);
}

static LRESULT IYUV_Decompress(const ICDECOMPRESS *params)
{
    const BITMAPINFOHEADER *in, *out;
    const BYTE *source, *y_plane, *u_plane, *v_plane;
    BYTE *target;
    LONG width, height, source_stride, chroma_stride, target_stride;
    LONG x, y;

    if (!params || !params->lpbiInput || !params->lpbiOutput || !params->lpInput || !params->lpOutput)
        return ICERR_BADPARAM;

    in = params->lpbiInput;
    out = params->lpbiOutput;
    if (IYUV_DecompressQuery(in, out) != ICERR_OK)
        return ICERR_BADFORMAT;

    width = in->biWidth;
    height = abs(in->biHeight);
    if (width < 0)
        width = -width;
    source_stride = width;
    chroma_stride = (width + 1) / 2;
    target_stride = ((width * out->biBitCount + 31) / 32) * 4;

    source = params->lpInput;
    target = params->lpOutput;
    y_plane = source;
    u_plane = y_plane + source_stride * height;
    v_plane = u_plane + chroma_stride * ((height + 1) / 2);

    for (y = 0; y < height; ++y)
    {
        BYTE *row = target + (height - 1 - y) * target_stride;

        for (x = 0; x < width; ++x)
        {
            BYTE red, green, blue;
            convert_pixel(y_plane[y * source_stride + x], u_plane[(y / 2) * chroma_stride + x / 2], v_plane[(y / 2) * chroma_stride + x / 2], &red, &green, &blue);

            if (out->biBitCount == 24)
            {
                row[x * 3] = blue;
                row[x * 3 + 1] = green;
                row[x * 3 + 2] = red;
            }
            else if (out->biBitCount == 16)
            {
                WORD value = ((WORD)(red >> 3) << 10) | ((WORD)(green >> 3) << 5) | (blue >> 3);
                ((WORD *)row)[x] = value;
            }
            else
            {
                row[x] = (red & 0xe0) | ((green >> 3) & 0x1c) | (blue >> 6);
            }
        }
    }

    return ICERR_OK;
}

static LRESULT IYUV_GetInfo(ICINFO *icinfo, DWORD size)
{
    if (!icinfo)
        return sizeof(*icinfo);
    if (size < sizeof(*icinfo))
        return 0;

    memset(icinfo, 0, sizeof(*icinfo));
    icinfo->dwSize = sizeof(*icinfo);
    icinfo->fccType = ICTYPE_VIDEO;
    icinfo->fccHandler = FOURCC_IYUV;
    icinfo->dwVersionICM = ICVERSION;
    LoadStringW(IYUV_32_module, IDS_NAME, icinfo->szName, sizeof(icinfo->szName) / sizeof(icinfo->szName[0]));
    LoadStringW(IYUV_32_module, IDS_DESCRIPTION, icinfo->szDescription, sizeof(icinfo->szDescription) / sizeof(icinfo->szDescription[0]));
    return sizeof(*icinfo);
}

LRESULT WINAPI IYUV_DriverProc(DWORD_PTR driver_id, HDRVR hdrvr, UINT msg, LPARAM param1, LPARAM param2)
{
    UNREFERENCED_PARAMETER(hdrvr);

    switch (msg)
    {
    case DRV_LOAD:
        return TRUE;
    case DRV_OPEN:
        return IYUV_Open((ICINFO *)param2);
    case DRV_CLOSE:
        return TRUE;
    case DRV_ENABLE:
    case DRV_DISABLE:
    case DRV_FREE:
        return 0;
    case ICM_GETINFO:
        return IYUV_GetInfo((ICINFO *)param1, (DWORD)param2);
    case ICM_DECOMPRESS_QUERY:
        return IYUV_DecompressQuery((BITMAPINFOHEADER *)param1, (BITMAPINFOHEADER *)param2);
    case ICM_DECOMPRESS_GET_FORMAT:
        return IYUV_DecompressGetFormat((BITMAPINFOHEADER *)param1, (BITMAPINFOHEADER *)param2);
    case ICM_DECOMPRESS:
        return IYUV_Decompress((ICDECOMPRESS *)param1);
    case ICM_DECOMPRESS_BEGIN:
        return IYUV_DecompressBegin((BITMAPINFOHEADER *)param1, (BITMAPINFOHEADER *)param2);
    case ICM_DECOMPRESS_END:
        return ICERR_OK;
    case ICM_COMPRESS_QUERY:
        return ICERR_BADFORMAT;
    }

    UNREFERENCED_PARAMETER(driver_id);
    return ICERR_UNSUPPORTED;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        IYUV_32_module = module;
    }
    return TRUE;
}
