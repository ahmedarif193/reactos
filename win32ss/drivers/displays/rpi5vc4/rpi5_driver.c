/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RPi5 XPDM driver entry point
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5_gdi.h"

static DHPDEV APIENTRY
Rpi5EnablePDEV(
    DEVMODEW *DeviceMode,
    LPWSTR LogAddress,
    ULONG PatternCount,
    HSURF *PatternSurfaces,
    ULONG CapsSize,
    ULONG *Caps,
    ULONG DeviceInfoSize,
    DEVINFO *DeviceInfo,
    HDEV EngineDevice,
    LPWSTR DeviceName,
    HANDLE Miniport)
{
    DHPDEV Device;

    Device = FrameBufferEnablePDEV(DeviceMode,
                                   LogAddress,
                                   PatternCount,
                                   PatternSurfaces,
                                   CapsSize,
                                   Caps,
                                   DeviceInfoSize,
                                   DeviceInfo,
                                   EngineDevice,
                                   DeviceName,
                                   Miniport);
    if (Device != NULL)
        Rpi5InitializeGdiPdev((PPDEV)Device);

    return Device;
}

static DRVFN Rpi5DrvFunctionTable[] =
{
    {INDEX_DrvEnablePDEV, (PFN)Rpi5EnablePDEV},
    {INDEX_DrvCompletePDEV, (PFN)DrvCompletePDEV},
    {INDEX_DrvDisablePDEV, (PFN)DrvDisablePDEV},
    {INDEX_DrvEnableSurface, (PFN)DrvEnableSurface},
    {INDEX_DrvDisableSurface, (PFN)DrvDisableSurface},
    {INDEX_DrvAssertMode, (PFN)DrvAssertMode},
    {INDEX_DrvGetModes, (PFN)DrvGetModes},
    {INDEX_DrvSetPalette, (PFN)DrvSetPalette},
    {INDEX_DrvSetPointerShape, (PFN)DrvSetPointerShape},
    {INDEX_DrvMovePointer, (PFN)DrvMovePointer},
    {INDEX_DrvEnableDirectDraw, (PFN)DrvEnableDirectDraw},
    {INDEX_DrvDisableDirectDraw, (PFN)DrvDisableDirectDraw},
    {INDEX_DrvBitBlt, (PFN)DrvBitBlt},
    {INDEX_DrvCopyBits, (PFN)DrvCopyBits},
    {INDEX_DrvLineTo, (PFN)DrvLineTo},
    {INDEX_DrvPaint, (PFN)DrvPaint},
    {INDEX_DrvStretchBlt, (PFN)DrvStretchBlt},
    {INDEX_DrvStretchBltROP, (PFN)DrvStretchBltROP},
    {INDEX_DrvAlphaBlend, (PFN)DrvAlphaBlend},
    {INDEX_DrvTransparentBlt, (PFN)DrvTransparentBlt},
    {INDEX_DrvGradientFill, (PFN)DrvGradientFill},
    {INDEX_DrvSynchronizeSurface, (PFN)DrvSynchronizeSurface},
    {INDEX_DrvEscape, (PFN)DrvEscape},
};

BOOL APIENTRY
DrvEnableDriver(
    ULONG EngineVersion,
    ULONG Size,
    PDRVENABLEDATA EnableData)
{
    UNREFERENCED_PARAMETER(EngineVersion);

    if (Size < sizeof(*EnableData))
        return FALSE;

    EnableData->c = RTL_NUMBER_OF(Rpi5DrvFunctionTable);
    EnableData->pdrvfn = Rpi5DrvFunctionTable;
    EnableData->iDriverVersion = DDI_DRIVER_VERSION_NT5;
    return TRUE;
}
