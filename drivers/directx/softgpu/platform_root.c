/*
 * PROJECT:     ReactOS WDDM software GPU miniport
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Generic root-enumerated firmware framebuffer provider
 */

#include "softgpu.h"

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
