/*
 * PROJECT:     ReactOS WDDM software GPU miniport
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Device-neutral firmware framebuffer validation helpers
 */

#include "softgpu.h"

BOOLEAN
SoftGpuDecodeLoaderGop(
    _In_ const SOFTGPU_LOADER_FRAMEBUFFER *FrameBuffer,
    _Out_ PULONG Pitch,
    _Out_ PULONGLONG VisibleLength)
{
    ULONG ColorMasks;
    ULONGLONG Base;

    if (FrameBuffer == NULL || Pitch == NULL || VisibleLength == NULL ||
        FrameBuffer->FrameBufferBase.QuadPart == 0 ||
        FrameBuffer->FrameBufferSize == 0 ||
        FrameBuffer->HorizontalResolution == 0 ||
        FrameBuffer->VerticalResolution == 0 ||
        FrameBuffer->HorizontalResolution > SOFTGPU_MAX_DISPLAY_WIDTH ||
        FrameBuffer->VerticalResolution > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        FrameBuffer->PixelsPerScanLine < FrameBuffer->HorizontalResolution ||
        FrameBuffer->PixelsPerScanLine > SOFTGPU_MAX_DISPLAY_WIDTH)
    {
        return FALSE;
    }

    /*
     * FreeLdr normalizes PixelFormat to bits per pixel and supplies the channel
     * masks. The software present path copies X8R8G8B8 pixels without format
     * conversion, so accept only that exact memory layout.
     */
    if (FrameBuffer->PixelFormat !=
            SOFTGPU_DISPLAY_BYTES_PER_PIXEL * 8 ||
        FrameBuffer->RedMask != 0x00FF0000 ||
        FrameBuffer->GreenMask != 0x0000FF00 ||
        FrameBuffer->BlueMask != 0x000000FF)
    {
        return FALSE;
    }

    ColorMasks = FrameBuffer->RedMask |
                 FrameBuffer->GreenMask |
                 FrameBuffer->BlueMask;
    if ((FrameBuffer->RedMask & FrameBuffer->GreenMask) != 0 ||
        (FrameBuffer->RedMask & FrameBuffer->BlueMask) != 0 ||
        (FrameBuffer->GreenMask & FrameBuffer->BlueMask) != 0 ||
        (FrameBuffer->Reserved & ColorMasks) != 0)
    {
        return FALSE;
    }

    if (FrameBuffer->PixelsPerScanLine >
            MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
    {
        return FALSE;
    }

    *Pitch = FrameBuffer->PixelsPerScanLine *
             SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
    if (*Pitch == 0 ||
        *Pitch > SOFTGPU_MAX_DISPLAY_PITCH ||
        FrameBuffer->VerticalResolution > (~(ULONGLONG)0) / *Pitch)
    {
        return FALSE;
    }

    *VisibleLength =
        (ULONGLONG)FrameBuffer->VerticalResolution * *Pitch;
    Base = (ULONGLONG)FrameBuffer->FrameBufferBase.QuadPart;
    return *VisibleLength != 0 &&
           *VisibleLength <= FrameBuffer->FrameBufferSize &&
           *VisibleLength <= (ULONGLONG)SOFTGPU_MAX_SURFACE_SIZE &&
           Base <= (~(ULONGLONG)0) - *VisibleLength;
}

BOOLEAN
SoftGpuValidatePostDisplayInfo(
    _In_ const DXGK_DISPLAY_INFORMATION *DisplayInfo,
    _Out_ PULONGLONG VisibleLength)
{
    ULONG MinimumPitch;
    ULONGLONG Base;

    if (DisplayInfo == NULL || VisibleLength == NULL ||
        DisplayInfo->PhysicAddress.QuadPart == 0 ||
        DisplayInfo->Width == 0 ||
        DisplayInfo->Height == 0 ||
        DisplayInfo->Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
        DisplayInfo->Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        (DisplayInfo->ColorFormat != D3DDDIFMT_X8R8G8B8 &&
         DisplayInfo->ColorFormat != D3DDDIFMT_A8R8G8B8))
    {
        return FALSE;
    }

    if (DisplayInfo->Width >
            MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
    {
        return FALSE;
    }

    MinimumPitch =
        DisplayInfo->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
    if (DisplayInfo->Pitch < MinimumPitch ||
        DisplayInfo->Pitch > SOFTGPU_MAX_DISPLAY_PITCH ||
        (DisplayInfo->Pitch % SOFTGPU_DISPLAY_BYTES_PER_PIXEL) != 0 ||
        DisplayInfo->Height > (~(ULONGLONG)0) / DisplayInfo->Pitch)
    {
        return FALSE;
    }

    *VisibleLength =
        (ULONGLONG)DisplayInfo->Pitch * DisplayInfo->Height;
    Base = (ULONGLONG)DisplayInfo->PhysicAddress.QuadPart;
    return *VisibleLength != 0 &&
           *VisibleLength <= (ULONGLONG)SOFTGPU_MAX_SURFACE_SIZE &&
           Base <= (~(ULONGLONG)0) - *VisibleLength;
}

NTSTATUS
SoftGpuAcquirePostDisplay(
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo,
    _Out_ PULONGLONG VisibleLength)
{
    NTSTATUS Status;

    if (DxgkInterface == NULL ||
        DisplayInfo == NULL ||
        VisibleLength == NULL ||
        DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->Size <
            FIELD_OFFSET(DXGK_INTERFACE,
                         DxgkCbAcquirePostDisplayOwnership) +
                sizeof(DxgkInterface->DxgkCbAcquirePostDisplayOwnership) ||
        DxgkInterface->DxgkCbAcquirePostDisplayOwnership == NULL)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RtlZeroMemory(DisplayInfo, sizeof(*DisplayInfo));
    Status = DxgkInterface->DxgkCbAcquirePostDisplayOwnership(
                 DxgkInterface->DeviceHandle,
                 DisplayInfo);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!SoftGpuValidatePostDisplayInfo(DisplayInfo, VisibleLength))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    return STATUS_SUCCESS;
}
