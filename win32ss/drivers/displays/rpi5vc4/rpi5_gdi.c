/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bounded V3D publication backend for generic XPDM GDI hooks
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5_gdi.h"

#define RPI5VC4_GDI_ALLOC_TAG 'iG5R'

static BOOL
Rpi5ClipPresentRect(
    _In_ PPDEV Device,
    _In_opt_ const RECTL *Rect,
    _In_opt_ CLIPOBJ *Clip,
    _Out_ RECTL *Clipped)
{
    LONG Swap;

    Clipped->left = 0;
    Clipped->top = 0;
    Clipped->right = Device->ScreenWidth;
    Clipped->bottom = Device->ScreenHeight;
    if (Rect != NULL)
    {
        *Clipped = *Rect;
        if (Clipped->left > Clipped->right)
        {
            Swap = Clipped->left;
            Clipped->left = Clipped->right;
            Clipped->right = Swap;
        }
        if (Clipped->top > Clipped->bottom)
        {
            Swap = Clipped->top;
            Clipped->top = Clipped->bottom;
            Clipped->bottom = Swap;
        }
    }

    Clipped->left = max(Clipped->left, 0);
    Clipped->top = max(Clipped->top, 0);
    Clipped->right = min(Clipped->right, (LONG)Device->ScreenWidth);
    Clipped->bottom = min(Clipped->bottom, (LONG)Device->ScreenHeight);
    if (Clip != NULL && Clip->iDComplexity != DC_TRIVIAL)
    {
        Clipped->left = max(Clipped->left, Clip->rclBounds.left);
        Clipped->top = max(Clipped->top, Clip->rclBounds.top);
        Clipped->right = min(Clipped->right, Clip->rclBounds.right);
        Clipped->bottom = min(Clipped->bottom, Clip->rclBounds.bottom);
    }

    return Clipped->left < Clipped->right &&
           Clipped->top < Clipped->bottom;
}

static VOID APIENTRY
Rpi5PublishShadowSurface(
    _In_ SURFOBJ *Surface,
    _In_opt_ const RECTL *Rect,
    _In_opt_ CLIPOBJ *Clip,
    _In_ FRAMEBUF_SHADOW_OPERATION Operation)
{
    RPI5VC4_GDI_PRESENT_RESULT Result;
    PRPI5VC4_GDI_PRESENT_REQUEST Request;
    PPDEV Device;
    RECTL Clipped;
    ULONG HeaderSize = FIELD_OFFSET(RPI5VC4_GDI_PRESENT_REQUEST, Pixels);
    ULONG Width;
    ULONG Height;
    ULONG RowBytes;
    ULONG PixelBytes;
    ULONG RequestSize;
    ULONG Returned = 0;
    ULONG IoStatus;
    ULONG Row;
    PUCHAR Source;
    BOOL HardwarePresented;

    UNREFERENCED_PARAMETER(Operation);

    Device = (PPDEV)Surface->dhpdev;
    if (Device->BitsPerPixel != 32 ||
        !Rpi5ClipPresentRect(Device, Rect, Clip, &Clipped))
    {
        return;
    }

    Width = Clipped.right - Clipped.left;
    Height = Clipped.bottom - Clipped.top;
    if (Width > RPI5VC4_V3D_CLEAR_MAX_WIDTH ||
        Height > RPI5VC4_V3D_CLEAR_MAX_HEIGHT)
    {
        goto CpuFallback;
    }

    RowBytes = Width * sizeof(ULONG);
    PixelBytes = RowBytes * Height;
    if (PixelBytes / Height != RowBytes ||
        HeaderSize > ~0u - PixelBytes)
    {
        goto CpuFallback;
    }

    RequestSize = HeaderSize + PixelBytes;
    Request = EngAllocMem(0, RequestSize, RPI5VC4_GDI_ALLOC_TAG);
    if (Request == NULL)
        goto CpuFallback;

    Request->Size = RequestSize;
    Request->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Request->Width = Width;
    Request->Height = Height;
    Request->DestinationX = Clipped.left;
    Request->DestinationY = Clipped.top;
    Request->Format = RPI5VC4_GDI_FORMAT_BGRX8;
    Request->RowStride = RowBytes;
    Request->PixelBytes = PixelBytes;
    Source = Device->ShadowPtr +
             Clipped.top * Device->ScreenDelta +
             Clipped.left * sizeof(ULONG);
    if (RowBytes == Device->ScreenDelta)
    {
        memcpy(Request->Pixels, Source, PixelBytes);
    }
    else
    {
        for (Row = 0; Row < Height; Row++)
        {
            memcpy(Request->Pixels + Row * RowBytes, Source, RowBytes);
            Source += Device->ScreenDelta;
        }
    }

    memset(&Result, 0, sizeof(Result));
    IoStatus = EngDeviceIoControl(Device->hDriver,
                                  IOCTL_VIDEO_RPI5VC4_PRESENT_GDI,
                                  Request,
                                  RequestSize,
                                  &Result,
                                  sizeof(Result),
                                  &Returned);
    EngFreeMem(Request);

    HardwarePresented =
        IoStatus == 0 &&
        Returned >= sizeof(Result) &&
        Result.AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
        Result.Status == RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS &&
        (Result.Diagnostics.Flags &
         RPI5VC4_V3D_SELFTEST_FLAG_PASSED) != 0;
    if (HardwarePresented)
        return;

CpuFallback:
    IntFlushShadowRect(Device, &Clipped);
}

VOID
Rpi5InitializeGdiPdev(
    _Inout_ PPDEV Device)
{
    Device->ShadowPublish = Rpi5PublishShadowSurface;
}
