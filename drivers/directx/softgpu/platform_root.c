/*
 * PROJECT:     ReactOS WDDM software GPU miniport
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Generic root-enumerated firmware framebuffer provider
 */

#include "softgpu.h"

#if defined(_M_IX86) || defined(_M_AMD64)
#define SOFTGPU_VGA_INPUT_STATUS_1       ((PUCHAR)(ULONG_PTR)0x3da)
#define SOFTGPU_VGA_VERTICAL_RETRACE     0x08
#define SOFTGPU_VBLANK_POLL_US           20
#define SOFTGPU_VBLANK_TIMEOUT_US        20000

static BOOLEAN
SoftGpuPlatformWaitForRetraceState(
    _In_ BOOLEAN InRetrace)
{
    ULONG Elapsed;

    for (Elapsed = 0;
         Elapsed < SOFTGPU_VBLANK_TIMEOUT_US;
         Elapsed += SOFTGPU_VBLANK_POLL_US)
    {
        BOOLEAN Current =
            (READ_PORT_UCHAR(SOFTGPU_VGA_INPUT_STATUS_1) &
             SOFTGPU_VGA_VERTICAL_RETRACE) != 0;

        if (Current == InRetrace)
            return TRUE;

        KeStallExecutionProcessor(SOFTGPU_VBLANK_POLL_US);
    }

    return FALSE;
}
#endif

NTSTATUS
SoftGpuPlatformValidatePdo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    return PhysicalDeviceObject != NULL ?
               STATUS_SUCCESS :
               STATUS_INVALID_PARAMETER;
}

NTSTATUS
SoftGpuPlatformQueryStart(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    UNREFERENCED_PARAMETER(Device);

    if (DxgkInterface == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Config, sizeof(*Config));
    Config->Width = SOFTGPU_DEFAULT_WIDTH;
    Config->Height = SOFTGPU_DEFAULT_HEIGHT;
    Config->Format = SOFTGPU_DEFAULT_FORMAT;

#if (REACTOS_WDDM_TARGET_LEVEL < 1200)
    {
        SOFTGPU_LOADER_FRAMEBUFFER LoaderFrameBuffer;
        ULONG LoaderPitch;
        ULONGLONG LoaderVisibleLength;

        /*
         * WDDM 1.0/1.1 has no POST-ownership callback. Use the loader handoff
         * when present; a genuinely headless boot retains the explicit
         * 1024x768 software-only fallback.
         */
        RtlZeroMemory(&LoaderFrameBuffer, sizeof(LoaderFrameBuffer));
        if (InbvGetGopFrameBufferInfo(&LoaderFrameBuffer))
        {
            if (!SoftGpuDecodeLoaderGop(&LoaderFrameBuffer,
                                        &LoaderPitch,
                                        &LoaderVisibleLength))
            {
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }

            Config->Width = LoaderFrameBuffer.HorizontalResolution;
            Config->Height = LoaderFrameBuffer.VerticalResolution;
            Config->Format = SOFTGPU_DEFAULT_FORMAT;
            Config->ScanoutPhysicalAddress =
                LoaderFrameBuffer.FrameBufferBase;
            Config->ScanoutPitch = LoaderPitch;
            Config->ScanoutSize = LoaderVisibleLength;
        }
    }
#else
    {
        DXGK_DISPLAY_INFORMATION PostDisplayInfo;
        ULONGLONG PostVisibleLength;
        NTSTATUS Status;

        /*
         * This root adapter is a Basic Display fallback. It starts only when
         * dxgkrnl transfers a valid firmware framebuffer to it; otherwise a
         * hardware miniport already owns the boot display or no display exists.
         */
        Status = SoftGpuAcquirePostDisplay(DxgkInterface,
                                           &PostDisplayInfo,
                                           &PostVisibleLength);
        if (!NT_SUCCESS(Status))
            return Status;

        Config->Width = PostDisplayInfo.Width;
        Config->Height = PostDisplayInfo.Height;
        Config->Format = PostDisplayInfo.ColorFormat;
        Config->ScanoutPhysicalAddress =
            PostDisplayInfo.PhysicAddress;
        Config->ScanoutPitch = PostDisplayInfo.Pitch;
        Config->ScanoutSize = PostVisibleLength;
    }
#endif

    return STATUS_SUCCESS;
}

VOID
SoftGpuPlatformFillNodeMetadata(
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata)
{
    GetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    RtlCopyMemory(GetNodeMetadata->FriendlyName,
                  L"ReactOS software GPU",
                  sizeof(L"ReactOS software GPU"));
}

VOID
SoftGpuPlatformInitializeTiming(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    if (Device == NULL)
        return;

    InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
#if defined(_M_IX86) || defined(_M_AMD64)
    /* A changing VGA status bit is a capability probe, not an assumption. */
    if (SoftGpuPlatformWaitForRetraceState(FALSE) &&
        SoftGpuPlatformWaitForRetraceState(TRUE))
    {
        InterlockedExchange(&Device->ScanoutVBlankAvailable, 1);
    }
#endif
}

BOOLEAN
SoftGpuPlatformWaitForVerticalBlank(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    if (Device == NULL ||
        InterlockedCompareExchange(
            &Device->ScanoutVBlankAvailable, 0, 0) == 0)
    {
        return FALSE;
    }

#if defined(_M_IX86) || defined(_M_AMD64)
    /* Start at the leading edge, leaving a full scan period for the copy. */
    if (SoftGpuPlatformWaitForRetraceState(FALSE) &&
        SoftGpuPlatformWaitForRetraceState(TRUE))
    {
        return TRUE;
    }
#endif

    /* Do not impose a timeout on every later present if timing disappears. */
    InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
    return FALSE;
}

NTSTATUS
SoftGpuPlatformQueryScanLine(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGKARG_GETSCANLINE GetScanLine)
{
    if (Device == NULL || GetScanLine == NULL ||
        GetScanLine->VidPnSourceId != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

#if defined(_M_IX86) || defined(_M_AMD64)
    if (InterlockedCompareExchange(
            &Device->ScanoutVBlankAvailable, 0, 0) != 0)
    {
        GetScanLine->ScanLine = 0;
        GetScanLine->InVerticalBlank =
            (READ_PORT_UCHAR(SOFTGPU_VGA_INPUT_STATUS_1) &
             SOFTGPU_VGA_VERTICAL_RETRACE) != 0;
        return STATUS_SUCCESS;
    }
#endif

    return STATUS_NOT_SUPPORTED;
}
