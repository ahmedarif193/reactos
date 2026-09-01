/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SoftGPU platform binding for the BCM2837 VC4 display engine
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi3vc4.h"

NTSTATUS
SoftGpuPlatformEscape(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ CONST DXGKARG_ESCAPE *Escape)
{
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PRPI3VC4_CONTEXT Context;
    PRPI3VC4_ESCAPE_INFO Info;
    NTSTATUS Status;
    ULONG Caps;

    if (Device == NULL || Escape == NULL ||
        Escape->hDevice == NULL || Escape->hContext != NULL ||
        Escape->pPrivateDriverData == NULL ||
        Escape->PrivateDriverDataSize < sizeof(RPI3VC4_ESCAPE_INFO) ||
        Escape->Flags.DriverKnownEscape)
    {
        return STATUS_NOT_SUPPORTED;
    }

    Info = (PRPI3VC4_ESCAPE_INFO)Escape->pPrivateDriverData;
    if (Info->Magic != RPI3VC4_ESCAPE_MAGIC ||
        Info->Op != RPI3VC4_ESCAPE_OP_QUERY_INFO)
    {
        return STATUS_NOT_SUPPORTED;
    }

    KmdDevice = (PSOFTGPU_KMD_DEVICE)Escape->hDevice;
    if (KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        KmdDevice->Adapter != Device)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Context = (PRPI3VC4_CONTEXT)Device->PlatformContext;
    if (Context == NULL)
        return STATUS_DEVICE_NOT_READY;

    Status = Rpi3Vc4EnsureV3dReady(Context);
    Caps = RPI3VC4_CAP_POWER_CONTROL;
    if (Context->V3dReady)
        Caps |= RPI3VC4_CAP_IDENT_VALID;
    if (Context->DirectScanoutReady)
        Caps |= RPI3VC4_CAP_LINEAR_SCANOUT;
    if (NT_SUCCESS(Context->V3dRenderTestStatus))
    {
        Caps |= RPI3VC4_CAP_RENDER_THREAD |
                RPI3VC4_CAP_VALIDATED_CL_SUBMIT |
                RPI3VC4_CAP_MONITORED_FENCE |
                RPI3VC4_CAP_OPENGL_20;
    }

    RtlZeroMemory(Info, sizeof(*Info));
    Info->Magic = RPI3VC4_ESCAPE_MAGIC;
    Info->Op = RPI3VC4_ESCAPE_OP_QUERY_INFO;
    Info->Size = sizeof(*Info);
    Info->AbiVersion = RPI3VC4_ESCAPE_INFO_ABI_VERSION;
    Info->InitializationStatus = Status;
    Info->Caps = Caps;
    Info->V3dReady = Context->V3dReady ? 1 : 0;
    Info->V3dIdent0 = Context->V3dIdent0;
    Info->V3dIdent1 = Context->V3dIdent1;
    Info->V3dIdent2 = Context->V3dIdent2;
    Info->V3dPhysical = Context->V3dPhysical.QuadPart;
    Info->ScreenWidth = Device->Width;
    Info->ScreenHeight = Device->Height;
    Info->ScreenPitch = Context->PrimaryPitch;
    Info->RenderTestStatus = Context->V3dRenderTestStatus;
    Info->RenderTestPixel = Context->V3dRenderTestPixel;
    return STATUS_SUCCESS;
}

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
    return Rpi3Vc4QueryPlatform(Device, DxgkInterface, Config);
}

NTSTATUS
SoftGpuPlatformRender(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PSOFTGPU_KMD_DEVICE KmdDevice,
    _Inout_ PDXGKARG_RENDER Render)
{
    return Rpi3Vc4ValidateRender(Device, KmdDevice, Render);
}

NTSTATUS
SoftGpuPlatformSubmitCommand(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const DXGKARG_SUBMITCOMMAND *SubmitCommand)
{
    return Rpi3Vc4SubmitCommand(Device, SubmitCommand);
}

BOOLEAN
SoftGpuPlatformInterruptRoutine(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    return Rpi3Vc4Interrupt(Device);
}

VOID
SoftGpuPlatformDpcRoutine(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    Rpi3Vc4Dpc(Device);
}

NTSTATUS
SoftGpuPlatformStartScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    return Rpi3Vc4StartScanout(Device);
}

NTSTATUS
SoftGpuPlatformStopScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    return Rpi3Vc4StopScanout(Device);
}

NTSTATUS
SoftGpuPlatformSetPrimary(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PHYSICAL_ADDRESS PrimaryAddress,
    _In_ ULONG Pitch,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BOOLEAN Visible)
{
    return Rpi3Vc4SetPrimary(Device,
                             PrimaryAddress,
                             Pitch,
                             Width,
                             Height,
                             Visible);
}

NTSTATUS
SoftGpuPlatformPresentDisplayOnly(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly)
{
    return Rpi3Vc4PresentDisplayOnly(Device, PresentDisplayOnly);
}

NTSTATUS
SoftGpuPlatformUpdatePointer(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    return Rpi3Vc4UpdatePointer(Device);
}

VOID
SoftGpuPlatformFillNodeMetadata(
    _In_ ULONG NodeOrdinal,
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata)
{
    /*
     * Each node's engine type is what tells the OS -- and anything showing
     * per-engine utilization -- which kind of work that queue carries.  The
     * names are the ones Windows shows for these types.
     */
    static const struct
    {
        DXGK_ENGINE_TYPE EngineType;
        const WCHAR     *FriendlyName;
    } Nodes[SOFTGPU_ENGINE_COUNT] =
    {
        { DXGK_ENGINE_TYPE_3D,           L"3D" },
        { DXGK_ENGINE_TYPE_COPY,         L"Copy" },
        { DXGK_ENGINE_TYPE_VIDEO_DECODE, L"Video Decode" },
        { DXGK_ENGINE_TYPE_VIDEO_ENCODE, L"Video Encode" },
    };
    SIZE_T NameBytes;

    if (NodeOrdinal >= SOFTGPU_ENGINE_COUNT)
        NodeOrdinal = 0;

    GetNodeMetadata->EngineType = Nodes[NodeOrdinal].EngineType;
    NameBytes = (wcslen(Nodes[NodeOrdinal].FriendlyName) + 1) * sizeof(WCHAR);
    if (NameBytes > sizeof(GetNodeMetadata->FriendlyName))
        NameBytes = sizeof(GetNodeMetadata->FriendlyName);
    RtlCopyMemory(GetNodeMetadata->FriendlyName,
                  Nodes[NodeOrdinal].FriendlyName,
                  NameBytes);
}

VOID
SoftGpuPlatformInitializeTiming(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    if (Device != NULL)
        InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
}

BOOLEAN
SoftGpuPlatformWaitForVerticalBlank(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    return Rpi3Vc4WaitForVerticalBlank(Device);
}

NTSTATUS
SoftGpuPlatformQueryScanLine(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGKARG_GETSCANLINE GetScanLine)
{
    return Rpi3Vc4QueryScanLine(Device, GetScanLine);
}
