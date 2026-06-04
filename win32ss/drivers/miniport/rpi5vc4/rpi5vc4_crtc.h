/*
 * PROJECT:     ReactOS Raspberry Pi 5 (BCM2712) display miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2712 CRTC / PixelValve (timing generator) access.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Ported from the Linux drm/vc4 driver (Dave Stevenson, Dom Cobley, Maxime
 * Ripard, Tim Gover) and adapted for ReactOS.
 *
 * The PixelValve (PV) is the per-output timing generator that drives an HDMI
 * controller from an HVS output FIFO: it produces the HSYNC/VSYNC and active /
 * blanking timing of the scanned-out raster.  The firmware programs and enables
 * one PV for the active HDMI mode; we read it back here to learn the live mode
 * (and as the foundation for taking over mode-set later).
 */

#ifndef _RPI5VC4_CRTC_H_
#define _RPI5VC4_CRTC_H_

#include "rpi5vc4.h"

/* PixelValve register blocks (BCM2712), from the device-tree pixelvalve nodes. */
#define RPI5_PV0_PHYS                   0x107C410000ULL
#define RPI5_PV1_PHYS                   0x107C411000ULL
#define RPI5_PV_LENGTH                  0x100

/* PixelValve register offsets. */
#define RPI5_PV_CONTROL                 0x00
#define RPI5_PV_V_CONTROL               0x04
#define RPI5_PV_VSYNCD_EVEN             0x08
#define RPI5_PV_HORZA                   0x0c    /* HBP:31..16  HSYNC:15..0  */
#define RPI5_PV_HORZB                   0x10    /* HFP:31..16  HACTIVE:15..0 */
#define RPI5_PV_VERTA                   0x14    /* VBP:31..16  VSYNC:15..0  */
#define RPI5_PV_VERTB                   0x18    /* VFP:31..16  VACTIVE:15..0 */
#define RPI5_PV_HACT_ACT                0x30
#define RPI5_PV_INTSTAT                 0x28
#define RPI5_PV_STAT                    0x2c

/* PV_CONTROL / PV_V_CONTROL bits we care about. */
#define RPI5_PV_CONTROL_EN              (1u << 0)
#define RPI5_PV_VCONTROL_VIDEN          (1u << 0)
#define RPI5_PV_VCONTROL_INTERLACE      (1u << 4)
#define RPI5_PV_INT_VFP_START           (1u << 7)

/* Extract the 16-bit low/high halves shared by the HORZ/VERT timing words. */
#define RPI5_PV_LO16(v)                 ((v) & 0xffff)
#define RPI5_PV_HI16(v)                 (((v) >> 16) & 0xffff)

/*
 * Read both PixelValves and report the live raster timing of whichever one is
 * enabled (driving the active HDMI output). Read-only: it does not change the
 * firmware-programmed mode. Returns TRUE if an enabled PV was found.
 */
BOOLEAN
Rpi5CrtcReportTiming(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

BOOLEAN
Rpi5CrtcProgramCurrentTiming(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

BOOLEAN
Rpi5CrtcWaitForVBlank(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

#endif /* _RPI5VC4_CRTC_H_ */
