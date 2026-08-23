/*
 * PROJECT:     ReactOS Raspberry Pi 5 (BCM2712) display miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2712 CRTC / PixelValve (timing generator) access.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Inspired by the only available driver out there, which is the Linux
 * equivalent (drm/vc4).
 *
 * For now this reads back the PixelValve the firmware enabled, records the
 * live raster timing, and can re-write the same timing as a no-op ownership
 * check.  The firmware-set HDMI mode remains the active mode.
 */

#include "rpi5vc4_crtc.h"

/* Map the selected PixelValve register block once and cache it. */
static PVOID
Rpi5CrtcMapPv(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->PixelValveBase == NULL &&
        DeviceExtension->PixelValveValid &&
        DeviceExtension->PixelValvePhysical.QuadPart != 0 &&
        DeviceExtension->PixelValveLength >= RPI5_PV_REQUIRED_LENGTH)
    {
        DeviceExtension->PixelValveBase =
            MmMapIoSpace(DeviceExtension->PixelValvePhysical,
                         DeviceExtension->PixelValveLength,
                         MmNonCached);
    }

    return DeviceExtension->PixelValveBase;
}

static BOOLEAN
Rpi5CrtcReportPv(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PVIDEO_ACCESS_RANGE Range,
    _In_ ULONG Index)
{
    PVOID Base;
    ULONG Control, VControl, HorzA, HorzB, VertA, VertB;
    BOOLEAN Enabled;

    if (Range->RangeStart.QuadPart == 0 ||
        Range->RangeLength < RPI5_PV_REQUIRED_LENGTH)
    {
        return FALSE;
    }

    Base = MmMapIoSpace(Range->RangeStart,
                        Range->RangeLength,
                        MmNonCached);
    if (Base == NULL)
    {
        DbgPrint("RPI5VC4: PV%lu map failed\n", Index);
        return FALSE;
    }

    Control  = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_CONTROL));
    VControl = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_V_CONTROL));
    HorzA    = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZA));
    HorzB    = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZB));
    VertA    = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTA));
    VertB    = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTB));
    Enabled  = (Control & RPI5_PV_CONTROL_EN) && (VControl & RPI5_PV_VCONTROL_VIDEN);

    if (Enabled)
    {
        BOOLEAN PreferThisValve =
            !DeviceExtension->PixelValveValid ||
            RPI5_PV_LO16(VertB) == DeviceExtension->ScreenHeight;

        if (PreferThisValve)
        {
            DeviceExtension->PixelValveValid = TRUE;
            DeviceExtension->PixelValvePhysical = Range->RangeStart;
            DeviceExtension->PixelValveLength = Range->RangeLength;
            DeviceExtension->PixelValveControl = Control;
            DeviceExtension->PixelValveVControl = VControl;
            DeviceExtension->PixelValveVsyncEven =
                READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VSYNCD_EVEN));
            DeviceExtension->PixelValveHorzA = HorzA;
            DeviceExtension->PixelValveHorzB = HorzB;
            DeviceExtension->PixelValveVertA = VertA;
            DeviceExtension->PixelValveVertB = VertB;
            DeviceExtension->PixelValveHactAct =
                READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HACT_ACT));
        }
    }

    MmUnmapIoSpace(Base, Range->RangeLength);
    return Enabled;
}

BOOLEAN
Rpi5CrtcReportTiming(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    BOOLEAN Found;

    DeviceExtension->PixelValveValid = FALSE;

    Found  = Rpi5CrtcReportPv(DeviceExtension,
                              &DeviceExtension->PixelValveRange[0],
                              0);
    Found |= Rpi5CrtcReportPv(DeviceExtension,
                              &DeviceExtension->PixelValveRange[1],
                              1);
    if (!Found)
        DbgPrint("RPI5VC4: no enabled PixelValve found (firmware HDMI off?)\n");
    return Found;
}

BOOLEAN
Rpi5CrtcProgramCurrentTiming(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Base;
    ULONG Control, VControl;

    Base = Rpi5CrtcMapPv(DeviceExtension);
    if (Base == NULL)
        return FALSE;

    Control = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_CONTROL));
    VControl = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_V_CONTROL));
    if (!(Control & RPI5_PV_CONTROL_EN) ||
        !(VControl & RPI5_PV_VCONTROL_VIDEN))
    {
        return FALSE;
    }

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VSYNCD_EVEN),
                         DeviceExtension->PixelValveVsyncEven);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZA),
                         DeviceExtension->PixelValveHorzA);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZB),
                         DeviceExtension->PixelValveHorzB);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTA),
                         DeviceExtension->PixelValveVertA);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTB),
                         DeviceExtension->PixelValveVertB);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HACT_ACT),
                         DeviceExtension->PixelValveHactAct);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_V_CONTROL),
                         DeviceExtension->PixelValveVControl);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_CONTROL),
                         DeviceExtension->PixelValveControl);

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    if (READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZA)) !=
            DeviceExtension->PixelValveHorzA ||
        READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZB)) !=
            DeviceExtension->PixelValveHorzB ||
        READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTA)) !=
            DeviceExtension->PixelValveVertA ||
        READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTB)) !=
            DeviceExtension->PixelValveVertB ||
        READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_V_CONTROL)) !=
            DeviceExtension->PixelValveVControl ||
        READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_CONTROL)) !=
            DeviceExtension->PixelValveControl)
    {
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
Rpi5CrtcWaitForVBlank(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Base;
    ULONG Control, VControl;
    ULONG Tries;

    Base = Rpi5CrtcMapPv(DeviceExtension);
    if (Base == NULL)
        return FALSE;

    Control = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_CONTROL));
    VControl = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_V_CONTROL));
    if (!(Control & RPI5_PV_CONTROL_EN) ||
        !(VControl & RPI5_PV_VCONTROL_VIDEN))
    {
        return FALSE;
    }

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT),
                         RPI5_PV_INT_VFP_START);

    for (Tries = 0; Tries < 400; Tries++)
    {
        if (READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT)) &
            RPI5_PV_INT_VFP_START)
        {
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT),
                                 RPI5_PV_INT_VFP_START);
            return TRUE;
        }

        VideoPortStallExecution(50);
    }

    return FALSE;
}
