/*
 * PROJECT:     ReactOS RPi5 Display-Only WDDM Miniport (rpi5dod)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Raspberry Pi 5 VideoCore VII HVS (Hardware Video Scaler)
 *              register and display-list definitions for the optional
 *              direct-scanout present path.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * These mirror the SCALER6 (HVS) definitions used by the rpi5vc4 XPDM
 * miniport (win32ss/drivers/miniport/rpi5vc4/rpi5vc4_hvs.h).  rpi5dod's
 * default present path blts into the firmware GOP framebuffer (which the HVS
 * already scans out), so these are only needed if a future build steers the
 * HVS display-list to point at an rpi5dod-owned framebuffer instead.
 */

#ifndef _RPI5DOD_HVS_H_
#define _RPI5DOD_HVS_H_

/* SCALER6 / HVS register block on the BCM2712 (Pi 5). */
#define RPI5DOD_HVS_PHYS            0x107C580000ULL
#define RPI5DOD_HVS_LENGTH         0x1A000
#define RPI5DOD_HVS_DLIST_OFFSET   0x4000   /* SCALER6 display-list RAM base */

#define RPI5DOD_HVS_REG_CONTROL    0x20     /* SCALER6_CONTROL              */
#define RPI5DOD_HVS_CONTROL_HVS_EN (1u << 31)
#define RPI5DOD_HVS_LPTRS_C        0x3c     /* SCALER6_DISP0_LPTRS  (C-step)*/
#define RPI5DOD_HVS_LPTRS_D        0x110    /* SCALER6D_DISP0_LPTRS (D-step)*/
#define RPI5DOD_HVS_LPTRS_HEAD_MASK 0xfffu  /* HEADE: dword index of head   */

/* ---- HVS display-list element fields (SCALER6 "gen6" unity plane) ------- *
 * The HVS scans out by walking a display list in a small on-chip RAM
 * (RPI5DOD_HVS_DLIST_OFFSET).  Each plane is a run of control dwords chained
 * by CTL0.NEXT.  The firmware's plane scans out the GOP framebuffer but
 * honours the per-pixel SOURCE ALPHA: GDI writes 0x00RRGGBB (alpha byte 0),
 * so every pixel composites transparent -> the desktop background scans out
 * BLACK.  Rewriting CTL0.ALPHA_MASK to FIXED (with an opaque CTL2 value) makes
 * the plane ignore the source alpha, so GDI/DWM output is shown opaque.  These
 * mirror the rpi5vc4 XPDM miniport's SCALER6 definitions (proven on D-step
 * silicon: a 0x00FFFFFF fill scanned out BLACK, 0xFFFFFFFF scanned out WHITE).
 */
#define RPI5DOD_HVS_CTL0_END            (1u << 31)
#define RPI5DOD_HVS_CTL0_VALID          (1u << 30)
#define RPI5DOD_HVS_CTL0_NEXT_SHIFT     24
#define RPI5DOD_HVS_CTL0_NEXT_MASK      0x3fu
#define RPI5DOD_HVS_CTL0_ALPHA_MASK_SHIFT 18
#define RPI5DOD_HVS_CTL0_ALPHA_MASK_NONE  0u   /* use per-pixel source alpha   */
#define RPI5DOD_HVS_CTL0_ALPHA_MASK_FIXED 3u   /* SCALER6D: force fixed alpha  */
#define RPI5DOD_HVS_CTL0_UNITY          (1u << 15)
#define RPI5DOD_HVS_CTL0_ORDER_SHIFT    13
#define RPI5DOD_HVS_CTL0_FORMAT_SHIFT   0

#define RPI5DOD_HVS_CTL2_ALPHA_PREMULT  (1u << 29)
#define RPI5DOD_HVS_CTL2_ALPHA_VALUE_SHIFT 4
#define RPI5DOD_HVS_CTL2_ALPHA_OPAQUE   0xfffu

#define RPI5DOD_HVS_POS0_Y_SHIFT        16
#define RPI5DOD_HVS_POS2_LINES_SHIFT    16
#define RPI5DOD_HVS_PTR2_PITCH_MASK     0x1ffffu

#define RPI5DOD_HVS_PIXEL_FORMAT_RGBA8888 7
#define RPI5DOD_HVS_PIXEL_ORDER_BGRA    2       /* matches a BGRX/BGRA framebuffer */

#define RPI5DOD_HVS_CONTEXT_INIT        0xc0c0c0c0u  /* HVS overwrites at runtime */
#define RPI5DOD_HVS_PLANE_DWORDS        9

/*
 * Make the live HVS scanout plane OPAQUE (ignore per-pixel source alpha) so
 * the GDI/DWM desktop in the firmware GOP framebuffer is displayed instead of
 * composited to black.  Implemented in rpi5dod_hvs.c.  Returns TRUE if the
 * plane was found and made opaque (or was already opaque).  Safe no-op if the
 * HVS head does not match the GOP framebuffer (leaves the hardware untouched).
 *
 * This is what keeps the Pi 5 desktop visible (not black) when rpi5dod owns
 * the WDDM display: the firmware HVS plane is the same one GDI/uefifb paints
 * the desktop into, so making it opaque shows the real desktop.
 */
BOOLEAN
Rpi5DodHvsMakeScanoutOpaque(
    _In_ ULONGLONG FrameBufferPhys,
    _In_ ULONG     Width,
    _In_ ULONG     Height,
    _In_ ULONG     PitchBytes);

#endif /* _RPI5DOD_HVS_H_ */
