/*
 * ReactOS Generic Framebuffer VMware acceleration helpers
 *
 * Copyright (C) 2024 ReactOS Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "framebuf.h"

#define FB_RECT_ENUM_LIMIT 32
#define FB_RBRUSH_SIGNATURE 0x42665242 /* 'BrFB' */

typedef struct _FB_RECT_ENUM
{
    ULONG c;
    RECTL arcl[FB_RECT_ENUM_LIMIT];
} FB_RECT_ENUM;

FORCEINLINE LONG
FbBytesPerPixel(_In_ PPDEV ppdev)
{
    return (ppdev->BitsPerPixel + 7) / 8;
}

FORCEINLINE LONG
FbBytesPerFormat(ULONG BitmapFormat)
{
    switch (BitmapFormat)
    {
        case BMF_8BPP:
            return 1;
        case BMF_16BPP:
            return 2;
        case BMF_24BPP:
            return 3;
        case BMF_32BPP:
            return 4;
        default:
            return 0;
    }
}

FORCEINLINE BOOLEAN
FbIsIndexedFormat(ULONG BitmapFormat)
{
    return (BitmapFormat == BMF_1BPP) ||
           (BitmapFormat == BMF_4BPP) ||
           (BitmapFormat == BMF_8BPP);
}

static BOOLEAN
FbIntersectRectPair(_Out_ PRECTL Target,
                    _In_ const RECTL *A,
                    _In_ const RECTL *B)
{
    Target->left = max(A->left, B->left);
    Target->top = max(A->top, B->top);
    Target->right = min(A->right, B->right);
    Target->bottom = min(A->bottom, B->bottom);
    return (Target->left < Target->right) && (Target->top < Target->bottom);
}

static BOOLEAN
FbClipRectToScreen(_In_ PPDEV ppdev,
                   _Inout_ PRECTL Rect)
{
    if (Rect->left < 0)
        Rect->left = 0;
    if (Rect->top < 0)
        Rect->top = 0;
    if (Rect->right > (LONG)ppdev->ScreenWidth)
        Rect->right = ppdev->ScreenWidth;
    if (Rect->bottom > (LONG)ppdev->ScreenHeight)
        Rect->bottom = ppdev->ScreenHeight;

    return (Rect->left < Rect->right) && (Rect->top < Rect->bottom);
}

typedef struct _FB_RECT_PARAMS
{
    LONG DstLeft;
    LONG DstTop;
    LONG SrcLeft;
    LONG SrcTop;
    LONG Width;
    LONG Height;
} FB_RECT_PARAMS, *PFB_RECT_PARAMS;

static BOOLEAN
FbSetupBlitParameters(_In_ LONG DestLimitWidth,
                      _In_ LONG DestLimitHeight,
                      _In_ LONG SrcLimitWidth,
                      _In_ LONG SrcLimitHeight,
                      _In_ const RECTL *DestRect,
                      _In_ const POINTL *SrcPoint,
                      _Out_ PFB_RECT_PARAMS Params)
{
    if (!DestRect || !SrcPoint)
        return FALSE;

    RECTL clipped = *DestRect;

    if (clipped.left < 0)
        clipped.left = 0;
    if (clipped.top < 0)
        clipped.top = 0;
    if (clipped.right > DestLimitWidth)
        clipped.right = DestLimitWidth;
    if (clipped.bottom > DestLimitHeight)
        clipped.bottom = DestLimitHeight;

    if (clipped.left >= clipped.right || clipped.top >= clipped.bottom)
        return FALSE;

    LONG dx = clipped.left - DestRect->left;
    LONG dy = clipped.top - DestRect->top;

    LONG width = clipped.right - clipped.left;
    LONG height = clipped.bottom - clipped.top;

    LONG srcLeft = SrcPoint->x + dx;
    LONG srcTop = SrcPoint->y + dy;
    LONG srcRight = srcLeft + width;
    LONG srcBottom = srcTop + height;

    if (srcLeft < 0)
    {
        LONG delta = -srcLeft;
        clipped.left += delta;
        width -= delta;
        srcLeft = 0;
    }
    if (srcTop < 0)
    {
        LONG delta = -srcTop;
        clipped.top += delta;
        height -= delta;
        srcTop = 0;
    }

    if (srcRight > SrcLimitWidth)
    {
        LONG delta = srcRight - SrcLimitWidth;
        width -= delta;
    }
    if (srcBottom > SrcLimitHeight)
    {
        LONG delta = srcBottom - SrcLimitHeight;
        height -= delta;
    }

    if (width <= 0 || height <= 0)
        return FALSE;

    clipped.right = clipped.left + width;
    clipped.bottom = clipped.top + height;

    Params->DstLeft = clipped.left;
    Params->DstTop = clipped.top;
    Params->SrcLeft = srcLeft;
    Params->SrcTop = srcTop;
    Params->Width = width;
    Params->Height = height;

    return TRUE;
}

static BOOLEAN
FbSetupDeviceCopy(_In_ PPDEV ppdev,
                  _In_ const RECTL *DestRect,
                  _In_ const POINTL *SrcPoint,
                  _Out_ PFB_RECT_PARAMS Params)
{
    if (!ppdev->ScreenPtr)
        return FALSE;

    return FbSetupBlitParameters((LONG)ppdev->ScreenWidth,
                                 (LONG)ppdev->ScreenHeight,
                                 (LONG)ppdev->ScreenWidth,
                                 (LONG)ppdev->ScreenHeight,
                                 DestRect,
                                 SrcPoint,
                                 Params);
}

static BOOLEAN
FbCpuDeviceCopy(_In_ PPDEV ppdev,
                _In_ const FB_RECT_PARAMS *Params)
{
    if (!ppdev->ScreenPtr || !Params)
        return FALSE;

    const LONG bytesPerPixel = FbBytesPerPixel(ppdev);
    if (bytesPerPixel == 0)
        return FALSE;

    LONG rowBytes = Params->Width * bytesPerPixel;
    if (rowBytes <= 0 || Params->Height <= 0)
        return FALSE;

    PUCHAR base = (PUCHAR)ppdev->ScreenPtr;
    LONG stride = ppdev->ScreenDelta;

    PUCHAR dstLine = base + (SIZE_T)Params->DstTop * stride + (SIZE_T)Params->DstLeft * bytesPerPixel;
    PUCHAR srcLine = base + (SIZE_T)Params->SrcTop * stride + (SIZE_T)Params->SrcLeft * bytesPerPixel;

    if (dstLine == srcLine)
        return TRUE;

    if (srcLine < dstLine && (srcLine + (SIZE_T)(Params->Height - 1) * stride) >= dstLine)
    {
        dstLine += (SIZE_T)(Params->Height - 1) * stride;
        srcLine += (SIZE_T)(Params->Height - 1) * stride;
        for (LONG row = 0; row < Params->Height; ++row)
        {
            RtlMoveMemory(dstLine, srcLine, rowBytes);
            dstLine -= stride;
            srcLine -= stride;
        }
    }
    else
    {
        for (LONG row = 0; row < Params->Height; ++row)
        {
            RtlMoveMemory(dstLine, srcLine, rowBytes);
            dstLine += stride;
            srcLine += stride;
        }
    }

    return TRUE;
}

static BOOLEAN
FbCpuSolidFill(_In_ PPDEV ppdev,
               _In_ const RECTL *Rect,
               _In_ ULONG Color)
{
    if (!ppdev->ScreenPtr || !Rect)
        return FALSE;

    RECTL clipped = *Rect;
    if (!FbClipRectToScreen(ppdev, &clipped))
        return FALSE;

    LONG width = clipped.right - clipped.left;
    LONG height = clipped.bottom - clipped.top;
    if (width <= 0 || height <= 0)
        return FALSE;

    const LONG bytesPerPixel = FbBytesPerPixel(ppdev);
    PUCHAR base = (PUCHAR)ppdev->ScreenPtr;
    LONG stride = ppdev->ScreenDelta;
    PUCHAR line = base + (SIZE_T)clipped.top * stride + (SIZE_T)clipped.left * bytesPerPixel;

    switch (ppdev->BitsPerPixel)
    {
        case 8:
        {
            UCHAR value = (UCHAR)Color;
            for (LONG y = 0; y < height; ++y)
            {
                RtlFillMemory(line, width, value);
                line += stride;
            }
            break;
        }
        case 16:
        case 15:
        {
            USHORT value = (USHORT)Color;
            for (LONG y = 0; y < height; ++y)
            {
                PUSHORT dst = (PUSHORT)line;
                for (LONG x = 0; x < width; ++x)
                    dst[x] = value;
                line += stride;
            }
            break;
        }
        case 24:
        {
            UCHAR c0 = (UCHAR)Color;
            UCHAR c1 = (UCHAR)(Color >> 8);
            UCHAR c2 = (UCHAR)(Color >> 16);
            for (LONG y = 0; y < height; ++y)
            {
                PUCHAR dst = line;
                for (LONG x = 0; x < width; ++x)
                {
                    dst[0] = c0;
                    dst[1] = c1;
                    dst[2] = c2;
                    dst += 3;
                }
                line += stride;
            }
            break;
        }
        case 32:
        {
            ULONG value = Color;
            for (LONG y = 0; y < height; ++y)
            {
                PULONG dst = (PULONG)line;
                for (LONG x = 0; x < width; ++x)
                    dst[x] = value;
                line += stride;
            }
            break;
        }
        default:
            return FALSE;
    }

    return TRUE;
}

typedef BOOL (*FB_RECT_CALLBACK)(_In_ PPDEV ppdev,
                                 _In_ const RECTL *Rect,
                                 _In_opt_ const POINTL *SrcPoint,
                                 _In_opt_ PVOID Context);

static BOOL
FbDeviceCopyRectCallback(_In_ PPDEV ppdev,
                         _In_ const RECTL *Rect,
                         _In_opt_ const POINTL *SrcPoint,
                         _In_opt_ PVOID Context)
{
    FB_RECT_PARAMS params;

    UNREFERENCED_PARAMETER(Context);

    if (!SrcPoint)
        return FALSE;

    if (!FbSetupDeviceCopy(ppdev, Rect, SrcPoint, &params))
        return FALSE;

    return FbCpuDeviceCopy(ppdev, &params);
}

static BOOL
FbSolidFillRectCallback(_In_ PPDEV ppdev,
                        _In_ const RECTL *Rect,
                        _In_opt_ const POINTL *SrcPoint,
                        _In_opt_ PVOID Context)
{
    const ULONG *Color = (const ULONG *)Context;

    UNREFERENCED_PARAMETER(SrcPoint);

    if (!Color)
        return TRUE;

    return FbCpuSolidFill(ppdev, Rect, *Color);
}

typedef struct _FB_COPY_CONTEXT
{
    SURFOBJ *Source;
    XLATEOBJ *Xlate;
    ULONG SourceFormat;
} FB_COPY_CONTEXT, *PFB_COPY_CONTEXT;

typedef struct _FB_RBRUSH
{
    ULONG Signature;
    ULONG BytesPerPixel;
    ULONG Width;
    ULONG Height;
    LONG Stride;
    UCHAR Bits[1];
} FB_RBRUSH, *PFB_RBRUSH;

static BOOLEAN
FbCpuBitmapToDeviceCopy(_In_ PPDEV ppdev,
                        _In_ const FB_COPY_CONTEXT *Context,
                        _In_ const FB_RECT_PARAMS *Params)
{
    if (!ppdev || !Context || !Params || !Context->Source)
        return FALSE;

    SURFOBJ *source = Context->Source;
    if (!source->pvScan0)
        return FALSE;

    LONG bytesPerPixel = FbBytesPerPixel(ppdev);
    if (bytesPerPixel == 0)
        return FALSE;

    LONG rowBytes = Params->Width * bytesPerPixel;
    if (rowBytes <= 0 || Params->Height <= 0)
        return FALSE;

    PUCHAR dstBase = (PUCHAR)ppdev->ScreenPtr;
    LONG dstStride = ppdev->ScreenDelta;
    LONG height = Params->Height;

    LONG srcStride = source->lDelta;
    PUCHAR srcRowBase;

    if (srcStride < 0)
    {
        srcStride = -srcStride;
        srcRowBase = (PUCHAR)source->pvScan0 + (SIZE_T)(Params->SrcTop + height - 1) * srcStride;
    }
    else
    {
        srcRowBase = (PUCHAR)source->pvScan0 + (SIZE_T)Params->SrcTop * srcStride;
    }

    srcRowBase += (SIZE_T)Params->SrcLeft * bytesPerPixel;

    PUCHAR dstLine = dstBase + (SIZE_T)Params->DstTop * dstStride + (SIZE_T)Params->DstLeft * bytesPerPixel;
    PUCHAR srcLine = srcRowBase;

    for (LONG row = 0; row < height; ++row)
    {
        RtlMoveMemory(dstLine, srcLine, rowBytes);
        dstLine += dstStride;
        srcLine += (source->lDelta < 0) ? -(LONG)srcStride : srcStride;
    }

    return TRUE;
}

static VOID
FbStoreColor(PUCHAR Dst,
             ULONG Color,
             LONG BytesPerPixel)
{
    switch (BytesPerPixel)
    {
        case 1:
            *Dst = (UCHAR)Color;
            break;
        case 2:
            *(PUSHORT)Dst = (USHORT)Color;
            break;
        case 3:
            Dst[0] = (UCHAR)Color;
            Dst[1] = (UCHAR)(Color >> 8);
            Dst[2] = (UCHAR)(Color >> 16);
            break;
        case 4:
            *(PULONG)Dst = Color;
            break;
    }
}

static ULONG
FbExtractTrueColor(ULONG Format,
                   const UCHAR *Pixel)
{
    switch (Format)
    {
        case BMF_16BPP:
        {
            USHORT value = *(const USHORT*)Pixel;
            ULONG red = (value >> 11) & 0x1F;
            ULONG green = (value >> 5) & 0x3F;
            ULONG blue = value & 0x1F;
            red = (red * 255 + 15) / 31;
            green = (green * 255 + 31) / 63;
            blue = (blue * 255 + 15) / 31;
            return (red << 16) | (green << 8) | blue;
        }
        case BMF_24BPP:
            return Pixel[0] | (Pixel[1] << 8) | (Pixel[2] << 16);
        case BMF_32BPP:
            return Pixel[0] | (Pixel[1] << 8) | (Pixel[2] << 16);
        default:
            return 0;
    }
}

static ULONG
FbScaleColorToMask(ULONG Component,
                   ULONG Mask)
{
    if (Mask == 0)
        return 0;

    ULONG shift = 0;
    while ((Mask & 1) == 0)
    {
        Mask >>= 1;
        ++shift;
    }

    ULONG bits = 0;
    ULONG temp = Mask;
    while (temp & 1)
    {
        ++bits;
        temp >>= 1;
    }

    ULONG value = (Component * ((1u << bits) - 1) + 127) / 255;
    return (value << shift);
}

static ULONG
FbPackColorForTarget(PPDEV ppdev,
                     ULONG Color)
{
    switch (ppdev->BitsPerPixel)
    {
        case 32:
        case 24:
            return Color;
        case 16:
        {
            ULONG red = FbScaleColorToMask((Color >> 16) & 0xFF,
                                           ppdev->RedMask ? ppdev->RedMask : 0xF800);
            ULONG green = FbScaleColorToMask((Color >> 8) & 0xFF,
                                             ppdev->GreenMask ? ppdev->GreenMask : 0x07E0);
            ULONG blue = FbScaleColorToMask(Color & 0xFF,
                                            ppdev->BlueMask ? ppdev->BlueMask : 0x001F);
            return red | green | blue;
        }
        default:
            return Color;
    }
}

static BOOLEAN
FbCpuIndexedBitmapToDeviceCopy(_In_ PPDEV ppdev,
                               _In_ const FB_COPY_CONTEXT *Context,
                               _In_ const FB_RECT_PARAMS *Params)
{
    if (!ppdev || !Context || !Params)
        return FALSE;

    SURFOBJ *source = Context->Source;
    XLATEOBJ *xlate = Context->Xlate;

    if (!source || !source->pvScan0 || !xlate)
        return FALSE;

    LONG bytesPerPixel = FbBytesPerPixel(ppdev);
    if (bytesPerPixel == 0)
        return FALSE;

    LONG width = Params->Width;
    LONG height = Params->Height;
    if (width <= 0 || height <= 0)
        return FALSE;

    PUCHAR dstBase = (PUCHAR)ppdev->ScreenPtr;
    LONG dstStride = ppdev->ScreenDelta;

    LONG srcStride = source->lDelta;
    PUCHAR srcRowBase;

    if (srcStride < 0)
    {
        srcStride = -srcStride;
        srcRowBase = (PUCHAR)source->pvScan0 + (SIZE_T)(Params->SrcTop + height - 1) * srcStride;
    }
    else
    {
        srcRowBase = (PUCHAR)source->pvScan0 + (SIZE_T)Params->SrcTop * srcStride;
    }

    PUCHAR dstLine = dstBase + (SIZE_T)Params->DstTop * dstStride + (SIZE_T)Params->DstLeft * bytesPerPixel;
    PUCHAR srcLine = srcRowBase;

    for (LONG row = 0; row < height; ++row)
    {
        PUCHAR dstPixel = dstLine;

        switch (source->iBitmapFormat)
        {
            case BMF_1BPP:
            {
                const PUCHAR src = srcLine + (Params->SrcLeft >> 3);
                LONG bitStart = Params->SrcLeft & 7;
                for (LONG col = 0; col < width; ++col)
                {
                    LONG bitIndex = bitStart + col;
                    UCHAR byteValue = src[bitIndex >> 3];
                    UCHAR bit = 0x80 >> (bitIndex & 7);
                    ULONG index = (byteValue & bit) ? 1 : 0;
                    ULONG color = XLATEOBJ_iXlate(xlate, index);
                    ULONG packed = FbPackColorForTarget(ppdev, color);
                    FbStoreColor(dstPixel, packed, bytesPerPixel);
                    dstPixel += bytesPerPixel;
                }
                break;
            }
            case BMF_4BPP:
            {
                const PUCHAR src = srcLine + (Params->SrcLeft >> 1);
                LONG nibbleStart = Params->SrcLeft & 1;
                for (LONG col = 0; col < width; ++col)
                {
                    LONG nibbleIndex = nibbleStart + col;
                    UCHAR byteValue = src[nibbleIndex >> 1];
                    ULONG index = (nibbleIndex & 1) ? (byteValue >> 4) : (byteValue & 0x0F);
                    ULONG color = XLATEOBJ_iXlate(xlate, index);
                    ULONG packed = FbPackColorForTarget(ppdev, color);
                    FbStoreColor(dstPixel, packed, bytesPerPixel);
                    dstPixel += bytesPerPixel;
                }
                break;
            }
            case BMF_8BPP:
            {
                const PUCHAR src = srcLine + Params->SrcLeft;
                for (LONG col = 0; col < width; ++col)
                {
                    ULONG color = XLATEOBJ_iXlate(xlate, src[col]);
                    ULONG packed = FbPackColorForTarget(ppdev, color);
                    FbStoreColor(dstPixel, packed, bytesPerPixel);
                    dstPixel += bytesPerPixel;
                }
                break;
            }
            default:
                return FALSE;
        }

        dstLine += dstStride;
        srcLine += (source->lDelta < 0) ? -(LONG)srcStride : srcStride;
    }

    return TRUE;
}

typedef struct _FB_PATTERN_CONTEXT
{
    const FB_RBRUSH *Pattern;
    LONG OriginX;
    LONG OriginY;
} FB_PATTERN_CONTEXT, *PFB_PATTERN_CONTEXT;

static BOOLEAN
FbCpuTrueColorConvertCopy(_In_ PPDEV ppdev,
                          _In_ const FB_COPY_CONTEXT *Context,
                          _In_ const FB_RECT_PARAMS *Params)
{
    if (!ppdev || !Context || !Params)
        return FALSE;

    SURFOBJ *source = Context->Source;
    if (!source || !source->pvScan0)
        return FALSE;

    LONG dstBytes = FbBytesPerPixel(ppdev);
    LONG srcBytes = FbBytesPerFormat(Context->SourceFormat);
    if (dstBytes == 0 || srcBytes == 0)
        return FALSE;

    LONG width = Params->Width;
    LONG height = Params->Height;
    if (width <= 0 || height <= 0)
        return FALSE;

    PUCHAR dstBase = (PUCHAR)ppdev->ScreenPtr;
    LONG dstStride = ppdev->ScreenDelta;

    LONG srcStride = source->lDelta;
    PUCHAR srcRowBase;

    if (srcStride < 0)
    {
        srcStride = -srcStride;
        srcRowBase = (PUCHAR)source->pvScan0 + (SIZE_T)(Params->SrcTop + height - 1) * srcStride;
    }
    else
    {
        srcRowBase = (PUCHAR)source->pvScan0 + (SIZE_T)Params->SrcTop * srcStride;
    }

    PUCHAR dstLine = dstBase + (SIZE_T)Params->DstTop * dstStride + (SIZE_T)Params->DstLeft * dstBytes;
    PUCHAR srcLine = srcRowBase + (SIZE_T)Params->SrcLeft * srcBytes;

    for (LONG row = 0; row < height; ++row)
    {
        PUCHAR dstPixel = dstLine;
        PUCHAR srcPixel = srcLine;

        for (LONG col = 0; col < width; ++col)
        {
            ULONG color = FbExtractTrueColor(Context->SourceFormat, srcPixel);
            ULONG packed = FbPackColorForTarget(ppdev, color);
            FbStoreColor(dstPixel, packed, dstBytes);
            dstPixel += dstBytes;
            srcPixel += srcBytes;
        }

        dstLine += dstStride;
        srcLine += (source->lDelta < 0) ? -(LONG)srcStride : srcStride;
    }

    return TRUE;
}

static BOOLEAN
FbCpuPatternFill(_In_ PPDEV ppdev,
                 _In_ const FB_PATTERN_CONTEXT *Context,
                 _In_ const RECTL *Rect)
{
    const FB_RBRUSH *pattern;
    LONG bytesPerPixel;
    LONG width, height;
    LONG stride;
    LONG rectWidth, rectHeight;
    PUCHAR dstBase;
    LONG dstStride;
    LONG originX, originY;

    if (!ppdev || !Context || !Rect)
        return FALSE;

    pattern = Context->Pattern;
    if (!pattern || pattern->Signature != FB_RBRUSH_SIGNATURE)
        return FALSE;

    bytesPerPixel = pattern->BytesPerPixel;
    width = (LONG)pattern->Width;
    height = (LONG)pattern->Height;
    stride = pattern->Stride;

    if (bytesPerPixel == 0 || width <= 0 || height <= 0)
        return FALSE;

    if (!ppdev->ScreenPtr)
        return FALSE;

    RECTL clipped = *Rect;
    if (!FbClipRectToScreen(ppdev, &clipped))
        return FALSE;

    rectWidth = clipped.right - clipped.left;
    rectHeight = clipped.bottom - clipped.top;

    dstBase = (PUCHAR)ppdev->ScreenPtr;
    dstStride = ppdev->ScreenDelta;
    originX = Context->OriginX;
    originY = Context->OriginY;

    LONG startPatternY = clipped.top - originY;
    startPatternY %= height;
    if (startPatternY < 0)
        startPatternY += height;

    for (LONG row = 0; row < rectHeight; ++row)
    {
        LONG patternY = (startPatternY + row) % height;
        const UCHAR *patternLine = pattern->Bits + (SIZE_T)patternY * stride;

        LONG patternX = clipped.left - originX;
        patternX %= width;
        if (patternX < 0)
            patternX += width;

        PUCHAR dstPixel = dstBase + (SIZE_T)(clipped.top + row) * dstStride + (SIZE_T)clipped.left * bytesPerPixel;
        LONG remaining = rectWidth;

        while (remaining > 0)
        {
            LONG chunk = width - patternX;
            if (chunk > remaining)
                chunk = remaining;

            const UCHAR *patternPixel = patternLine + (SIZE_T)patternX * bytesPerPixel;
            RtlMoveMemory(dstPixel, patternPixel, (SIZE_T)chunk * bytesPerPixel);

            remaining -= chunk;
            dstPixel += (SIZE_T)chunk * bytesPerPixel;
            patternX = 0;
        }
    }

    return TRUE;
}

static BOOL
FbPatternFillRectCallback(_In_ PPDEV ppdev,
                          _In_ const RECTL *Rect,
                          _In_opt_ const POINTL *SrcPoint,
                          _In_opt_ PVOID Context)
{
    PFB_PATTERN_CONTEXT patternContext = (PFB_PATTERN_CONTEXT)Context;

    UNREFERENCED_PARAMETER(SrcPoint);

    if (!patternContext)
        return FALSE;

    return FbCpuPatternFill(ppdev, patternContext, Rect);
}

static BOOL
FbBitmapToDeviceRectCallback(_In_ PPDEV ppdev,
                             _In_ const RECTL *Rect,
                             _In_opt_ const POINTL *SrcPoint,
                             _In_opt_ PVOID Context)
{
    PFB_COPY_CONTEXT copyContext = (PFB_COPY_CONTEXT)Context;
    FB_RECT_PARAMS params;

    if (!copyContext || !copyContext->Source || !SrcPoint)
        return FALSE;

    SURFOBJ *source = copyContext->Source;

    if (!FbSetupBlitParameters((LONG)ppdev->ScreenWidth,
                               (LONG)ppdev->ScreenHeight,
                               source->sizlBitmap.cx,
                               source->sizlBitmap.cy,
                               Rect,
                               SrcPoint,
                               &params))
    {
        return FALSE;
    }

    LONG srcBytes = FbBytesPerFormat(copyContext->SourceFormat);
    LONG dstBytes = FbBytesPerPixel(ppdev);
    BOOLEAN trivialXlate = (!copyContext->Xlate) || (copyContext->Xlate->flXlate & XO_TRIVIAL);

    if (trivialXlate && (srcBytes != 0) && (srcBytes == dstBytes))
    {
        return FbCpuBitmapToDeviceCopy(ppdev, copyContext, &params);
    }

    if (!trivialXlate && copyContext->Xlate && FbIsIndexedFormat(copyContext->SourceFormat))
    {
        return FbCpuIndexedBitmapToDeviceCopy(ppdev, copyContext, &params);
    }

    if (trivialXlate && !FbIsIndexedFormat(copyContext->SourceFormat))
    {
        return FbCpuTrueColorConvertCopy(ppdev, copyContext, &params);
    }

    return FALSE;
}

static BOOL
FbProcessClippedRects(_In_ PPDEV ppdev,
                      _In_ const RECTL *DestRect,
                      _In_opt_ const POINTL *SrcPoint,
                      _In_opt_ CLIPOBJ *Clip,
                      _In_ FB_RECT_CALLBACK Callback,
                      _In_opt_ PVOID Context)
{
    if (!Callback)
        return FALSE;

    if (!Clip || Clip->iDComplexity == DC_TRIVIAL)
    {
        return Callback(ppdev, DestRect, SrcPoint, Context);
    }

    if (Clip->iDComplexity == DC_RECT)
    {
        RECTL bounded;
        if (!FbIntersectRectPair(&bounded, DestRect, &Clip->rclBounds))
            return TRUE;

        POINTL adjustedSrc;
        const POINTL *srcToUse = NULL;
        if (SrcPoint)
        {
            adjustedSrc.x = SrcPoint->x + (bounded.left - DestRect->left);
            adjustedSrc.y = SrcPoint->y + (bounded.top - DestRect->top);
            srcToUse = &adjustedSrc;
        }

        return Callback(ppdev, &bounded, srcToUse, Context);
    }

    if (Clip->iDComplexity == DC_COMPLEX)
    {
        FB_RECT_ENUM rectEnum;
        BOOL more;

        CLIPOBJ_cEnumStart(Clip, FALSE, CT_RECTANGLES, CD_ANY, 0);
        do
        {
            more = CLIPOBJ_bEnum(Clip, sizeof(rectEnum), (PVOID)&rectEnum);
            for (ULONG i = 0; i < rectEnum.c; ++i)
            {
                RECTL bounded;
                if (!FbIntersectRectPair(&bounded, DestRect, &rectEnum.arcl[i]))
                    continue;

                POINTL adjustedSrc;
                const POINTL *srcToUse = NULL;
                if (SrcPoint)
                {
                    adjustedSrc.x = SrcPoint->x + (bounded.left - DestRect->left);
                    adjustedSrc.y = SrcPoint->y + (bounded.top - DestRect->top);
                    srcToUse = &adjustedSrc;
                }

                if (!Callback(ppdev, &bounded, srcToUse, Context))
                    return FALSE;
            }
        } while (more);

        return TRUE;
    }

    return TRUE;
}

#ifndef ROP3_TO_ROP4
/* Local copy of the Win32 helper that mirrors an 8-bit ROP3 into a 16-bit ROP4 value. */
#define ROP3_TO_ROP4(Rop3) ((((Rop3) >> 8) & 0xff00) | (((Rop3) >> 16) & 0x00ff))
#endif

static BOOL
FbVmwareRectCopy(_In_ PPDEV ppdev,
                 _In_ const RECTL *DestRect,
                 _In_ const POINTL *SrcPoint)
{
    VMWARE_VIDEO_BLIT cmd = {0};
    DWORD returned = 0;

    cmd.DestX = DestRect->left;
    cmd.DestY = DestRect->top;
    cmd.Width = DestRect->right - DestRect->left;
    cmd.Height = DestRect->bottom - DestRect->top;

    if (cmd.Width == 0 || cmd.Height == 0)
        return TRUE;

    cmd.SrcX = SrcPoint->x;
    cmd.SrcY = SrcPoint->y;

    if (!EngDeviceIoControl(ppdev->hDriver,
                            IOCTL_VIDEO_VMWARE_FIFO_BLIT,
                            &cmd,
                            sizeof(cmd),
                            NULL,
                            0,
                            &returned))
    {
        return FALSE;
    }

    return TRUE;
}

static BOOL
FbVmwareRectFill(_In_ PPDEV ppdev,
                 _In_ const RECTL *Rect,
                 _In_ ULONG Color)
{
    VMWARE_VIDEO_FILL cmd = {0};
    DWORD returned = 0;

    cmd.X = Rect->left;
    cmd.Y = Rect->top;
    cmd.Width = Rect->right - Rect->left;
    cmd.Height = Rect->bottom - Rect->top;
    cmd.Color = Color;

    if (cmd.Width == 0 || cmd.Height == 0)
        return TRUE;

    if (!EngDeviceIoControl(ppdev->hDriver,
                            IOCTL_VIDEO_VMWARE_FIFO_FILL,
                            &cmd,
                            sizeof(cmd),
                            NULL,
                            0,
                            &returned))
    {
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
DrvCopyBits(SURFOBJ *psoDst,
            SURFOBJ *psoSrc,
            CLIPOBJ *pco,
            XLATEOBJ *pxlo,
            RECTL *prclDst,
            POINTL *pptlSrc)
{
    PPDEV ppdev = (PPDEV)psoDst->dhpdev;

    if (ppdev &&
        psoDst->iType == STYPE_DEVICE &&
        psoSrc && psoSrc->iType == STYPE_DEVICE &&
        (psoSrc->dhpdev == psoDst->dhpdev) &&
        (!pxlo || (pxlo->flXlate & XO_TRIVIAL)) &&
        pptlSrc)
    {
        if (FbProcessClippedRects(ppdev,
                                  prclDst,
                                  pptlSrc,
                                  pco,
                                  FbDeviceCopyRectCallback,
                                  NULL))
        {
            return TRUE;
        }
    }
    else if (ppdev &&
             psoDst->iType == STYPE_DEVICE &&
             psoSrc && psoSrc->iType == STYPE_BITMAP &&
             pptlSrc &&
             psoSrc->pvScan0)
    {
        LONG srcBytes = FbBytesPerFormat(psoSrc->iBitmapFormat);
        LONG dstBytes = FbBytesPerPixel(ppdev);
        BOOLEAN trivialXlate = (!pxlo) || (pxlo->flXlate & XO_TRIVIAL);

        if (((trivialXlate && (srcBytes != 0) && (srcBytes == dstBytes)) ||
             (!trivialXlate && pxlo && FbIsIndexedFormat(psoSrc->iBitmapFormat))) &&
            srcBytes != 0 && dstBytes != 0)
        {
            FB_COPY_CONTEXT context = { psoSrc, pxlo, psoSrc->iBitmapFormat };
            if (FbProcessClippedRects(ppdev,
                                      prclDst,
                                      pptlSrc,
                                      pco,
                                      FbBitmapToDeviceRectCallback,
                                      &context))
            {
                return TRUE;
            }
        }
    }

    if (ppdev && ppdev->VmwareFifo &&
        psoDst->iType == STYPE_DEVICE &&
        psoSrc && psoSrc->iType == STYPE_DEVICE &&
        pptlSrc != NULL &&
        (!pco || pco->iDComplexity == DC_TRIVIAL) &&
        (!pxlo || (pxlo->flXlate & XO_TRIVIAL)))
    {
        if (FbVmwareRectCopy(ppdev, prclDst, pptlSrc))
            return TRUE;
    }

    return EngCopyBits(psoDst, psoSrc, pco, pxlo, prclDst, pptlSrc);
}

BOOL APIENTRY
DrvBitBlt(SURFOBJ *psoDst,
          SURFOBJ *psoSrc,
          SURFOBJ *psoMask,
          CLIPOBJ *pco,
          XLATEOBJ *pxlo,
          RECTL *prclDst,
          POINTL *pptlSrc,
          POINTL *pptlMask,
          BRUSHOBJ *pbo,
          POINTL *pptlBrush,
          ROP4 rop4)
{
    PPDEV ppdev = (PPDEV)psoDst->dhpdev;

    if (ppdev && ppdev->VmwareFifo && (!pco || pco->iDComplexity == DC_TRIVIAL))
    {
        BOOL trivialXlate = (!pxlo) || (pxlo->flXlate & XO_TRIVIAL);

        if (rop4 == ROP3_TO_ROP4(SRCCOPY) &&
            psoSrc && psoSrc->iType == STYPE_DEVICE &&
            psoDst->iType == STYPE_DEVICE &&
            psoMask == NULL &&
            pptlSrc != NULL &&
            trivialXlate)
        {
            if (FbVmwareRectCopy(ppdev, prclDst, pptlSrc))
                return TRUE;
        }
        else if (rop4 == ROP3_TO_ROP4(PATCOPY) &&
                 psoDst->iType == STYPE_DEVICE &&
                 psoSrc == NULL &&
                 psoMask == NULL &&
                 pbo != NULL &&
                 pbo->iSolidColor != 0xFFFFFFFF &&
                 ppdev->BitsPerPixel >= 15 &&
                 trivialXlate)
        {
            if (FbVmwareRectFill(ppdev, prclDst, pbo->iSolidColor))
                return TRUE;
        }
    }

    if (ppdev && psoDst->iType == STYPE_DEVICE)
    {
        BOOL trivialXlate = (!pxlo) || (pxlo->flXlate & XO_TRIVIAL);

        if (rop4 == ROP3_TO_ROP4(SRCCOPY) &&
            psoSrc && psoSrc->iType == STYPE_DEVICE &&
            pptlSrc && trivialXlate &&
            (psoDst->dhpdev == psoSrc->dhpdev))
        {
            if (FbProcessClippedRects(ppdev,
                                      prclDst,
                                      pptlSrc,
                                      pco,
                                      FbDeviceCopyRectCallback,
                                      NULL))
            {
                return TRUE;
            }
        }
        else if (rop4 == ROP3_TO_ROP4(SRCCOPY) &&
                 psoSrc && psoSrc->iType == STYPE_BITMAP &&
                 psoMask == NULL &&
                 pptlSrc &&
                 psoSrc->pvScan0)
        {
            LONG srcBytes = FbBytesPerFormat(psoSrc->iBitmapFormat);
            LONG dstBytes = FbBytesPerPixel(ppdev);
            BOOLEAN trivialCopy = (!pxlo) || (pxlo->flXlate & XO_TRIVIAL);

            if (((trivialCopy && (srcBytes != 0) && (srcBytes == dstBytes)) ||
                 (!trivialCopy && pxlo && FbIsIndexedFormat(psoSrc->iBitmapFormat))) &&
                srcBytes != 0 && dstBytes != 0)
            {
                FB_COPY_CONTEXT context = { psoSrc, pxlo, psoSrc->iBitmapFormat };
                if (FbProcessClippedRects(ppdev,
                                          prclDst,
                                          pptlSrc,
                                          pco,
                                          FbBitmapToDeviceRectCallback,
                                          &context))
                {
                    return TRUE;
                }
            }
        }
        else if (rop4 == ROP3_TO_ROP4(PATCOPY) &&
                 psoSrc == NULL &&
                 psoMask == NULL &&
                 pbo)
        {
            if ((pbo->iSolidColor != 0xFFFFFFFF) && trivialXlate)
            {
                ULONG color = pbo->iSolidColor;
                if (FbProcessClippedRects(ppdev,
                                          prclDst,
                                          NULL,
                                          pco,
                                          FbSolidFillRectCallback,
                                          &color))
                {
                    return TRUE;
                }
            }
            else if ((pbo->iSolidColor == 0xFFFFFFFF) && trivialXlate)
            {
                PFB_RBRUSH realized = (PFB_RBRUSH)BRUSHOBJ_pvGetRbrush(pbo);
                if (realized && realized->Signature == FB_RBRUSH_SIGNATURE)
                {
                    FB_PATTERN_CONTEXT context =
                    {
                        realized,
                        pptlBrush ? pptlBrush->x : 0,
                        pptlBrush ? pptlBrush->y : 0
                    };

                    if (FbProcessClippedRects(ppdev,
                                              prclDst,
                                              NULL,
                                              pco,
                                              FbPatternFillRectCallback,
                                              &context))
                    {
                        return TRUE;
                    }
                }
            }
        }
    }

    return EngBitBlt(psoDst,
                     psoSrc,
                     psoMask,
                     pco,
                     pxlo,
                     prclDst,
                     pptlSrc,
                     pptlMask,
                     pbo,
                     pptlBrush,
                     rop4);
}

BOOL
APIENTRY
DrvRealizeBrush(BRUSHOBJ *pbo,
                SURFOBJ *psoTarget,
                SURFOBJ *psoPattern,
                SURFOBJ *psoMask,
                XLATEOBJ *pxlo,
                ULONG iHatch)
{
    LONG targetBytes, patternBytes;
    ULONG width, height;
    LONG stride;
    SIZE_T size;
    PFB_RBRUSH brush;
    PUCHAR dst;
    PUCHAR srcLine;
    LONG srcDelta;

    UNREFERENCED_PARAMETER(psoMask);
    UNREFERENCED_PARAMETER(iHatch);

    if (!pbo || !psoTarget || !psoPattern)
        return FALSE;

    if (pbo->iSolidColor != 0xFFFFFFFF)
        return FALSE;

    targetBytes = FbBytesPerFormat(psoTarget->iBitmapFormat);
    patternBytes = FbBytesPerFormat(psoPattern->iBitmapFormat);
    if ((targetBytes == 0) || (patternBytes == 0) || (targetBytes != patternBytes))
        return FALSE;

    if (pxlo && !(pxlo->flXlate & XO_TRIVIAL))
        return FALSE;

    width = psoPattern->sizlBitmap.cx;
    height = psoPattern->sizlBitmap.cy;
    if (width == 0 || height == 0)
        return FALSE;

    stride = width * targetBytes;
    size = FIELD_OFFSET(FB_RBRUSH, Bits) + (SIZE_T)stride * height;

    brush = (PFB_RBRUSH)BRUSHOBJ_pvAllocRbrush(pbo, size);
    if (!brush)
        return FALSE;

    brush->Signature = FB_RBRUSH_SIGNATURE;
    brush->BytesPerPixel = targetBytes;
    brush->Width = width;
    brush->Height = height;
    brush->Stride = stride;

    dst = brush->Bits;
    srcDelta = psoPattern->lDelta;
    if (srcDelta < 0)
        srcLine = (PUCHAR)psoPattern->pvScan0 + (SIZE_T)(height - 1) * (SIZE_T)(-srcDelta);
    else
        srcLine = (PUCHAR)psoPattern->pvScan0;

    for (ULONG y = 0; y < height; ++y)
    {
        RtlMoveMemory(dst, srcLine, stride);
        dst += stride;
        srcLine += psoPattern->lDelta;
    }

    return TRUE;
}
