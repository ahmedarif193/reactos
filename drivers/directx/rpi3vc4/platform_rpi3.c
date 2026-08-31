/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SoftGPU platform binding for the BCM2837 VC4 display engine
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi3vc4.h"

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
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata)
{
    GetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    RtlCopyMemory(GetNodeMetadata->FriendlyName,
                  L"Raspberry Pi 3 VC4",
                  sizeof(L"Raspberry Pi 3 VC4"));
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
