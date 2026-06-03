/*
 * PROJECT:     ReactOS Raspberry Pi 5 (BCM2712) display miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2712 HVS (Hardware Video Scaler) programming -
 *              multi-plane display-list generation and scanout ownership.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Ported from the Linux drm/vc4 driver (Dave Stevenson, Dom Cobley, Maxime
 * Ripard, Tim Gover) and adapted for ReactOS.
 *
 * The registers and display-list element layout below are the BCM2712 "HVS" -
 * the SCALER6 / SCALER6D display compositor.
 */

#ifndef _RPI5VC4_HVS_H_
#define _RPI5VC4_HVS_H_

#include "rpi5vc4.h"

/* HVS register block (BCM2712), per the device-tree 'hvs' node. */
#define RPI5_HVS_PHYS                   0x107C580000ULL
#define RPI5_HVS_LENGTH                 0x1A000
#define RPI5_HVS_DLIST_OFFSET           0x4000  /* SCALER6 display-list RAM base */

/* Global / per-channel registers (byte offsets into the register block). */
#define RPI5_HVS_REG_CONTROL            0x20    /* SCALER6_CONTROL */
#define RPI5_HVS_CONTROL_HVS_EN         (1u << 31)
#define RPI5_HVS_LPTRS_C                0x3c    /* SCALER6_DISP0_LPTRS  (C-step) */
#define RPI5_HVS_LPTRS_D                0x110   /* SCALER6D_DISP0_LPTRS (D-step) */
#define RPI5_HVS_LPTRS_HEAD_MASK        0xfffu  /* HEADE: dword index of list head */

/*
 * Display-list control word 0 (CTL0).
 *   END        bit 31      list terminator
 *   VALID      bit 30      this element is a valid plane
 *   NEXT       bits 29:24  dword offset from this CTL0 to the next element
 *   ALPHA_MASK bits 19:18  0 = use per-pixel source alpha; 3 (D-step) = fixed
 *   UNITY      bit 15      no scaling (source size == screen size)
 *   ORDERRGBA  bits 14:13  channel order
 *   FORMAT     bits 4:0    HVS pixel format
 */
#define RPI5_HVS_CTL0_END               (1u << 31)
#define RPI5_HVS_CTL0_VALID             (1u << 30)
#define RPI5_HVS_CTL0_NEXT_SHIFT        24
#define RPI5_HVS_CTL0_NEXT_MASK         0x3fu
#define RPI5_HVS_CTL0_ALPHA_MASK_SHIFT  18
#define RPI5_HVS_CTL0_ALPHA_MASK_NONE   0u      /* use per-pixel source alpha */
#define RPI5_HVS_CTL0_ALPHA_MASK_FIXED  3u      /* SCALER6D: force fixed alpha */
#define RPI5_HVS_CTL0_UNITY             (1u << 15)
#define RPI5_HVS_CTL0_ORDER_SHIFT       13
#define RPI5_HVS_CTL0_FORMAT_SHIFT      0

/*
 * Control word 2 (CTL2): alpha processing + 12-bit fixed alpha value.
 * On SCALER6D, CTL0.ALPHA_MASK selects fixed vs per-pixel alpha; CTL2 supplies
 * the premultiplied-alpha bit and the fixed alpha property value.
 */
#define RPI5_HVS_CTL2_ALPHA_PREMULT     (1u << 29)
#define RPI5_HVS_CTL2_ALPHA_VALUE_SHIFT 4
#define RPI5_HVS_CTL2_ALPHA_OPAQUE      0xfffu

/* POS0 (top-left): START_Y bits 28:16, START_X bits 12:0. */
#define RPI5_HVS_POS0_Y_SHIFT           16
/* POS2 (source size, stored as N-1): SRC_LINES bits 28:16, SRC_WIDTH bits 12:0. */
#define RPI5_HVS_POS2_LINES_SHIFT       16

/* PTR2: PITCH bits 16:0 (bytes per line for a linear surface). */
#define RPI5_HVS_PTR0_UPM_BASE_SHIFT    16
#define RPI5_HVS_PTR0_UPM_HANDLE_SHIFT  10
#define RPI5_HVS_PTR2_PITCH_MASK        0x1ffffu

/*
 * SCALER6D planes need distinct UPM fetch-buffer slots.  The primary scanout
 * uses handle field 0/base 0; keep the cursor in a separate low, aligned slot.
 */
#define RPI5_HVS_CURSOR_UPM_BASE        64u
#define RPI5_HVS_CURSOR_UPM_HANDLE      1u

/* HVS pixel formats (CTL0 bits 4:0) and channel orders (CTL0 bits 14:13). */
#define RPI5_HVS_PIXEL_FORMAT_RGBA8888  7
#define RPI5_HVS_PIXEL_ORDER_BGRA       2       /* matches a BGRX/BGRA framebuffer */

/* The HVS overwrites this context word with per-frame runtime state. */
#define RPI5_HVS_CONTEXT_INIT           0xc0c0c0c0u

/*
 * One SCALER6 plane element:
 *   CTL0 POS0 CTL2 POS2 CTX PTR0 PTR1 PTR2 END
 *
 * The CTL0 NEXT field is the full element size in dwords.  Keeping the END word
 * inside each element matches the gen6 display-list contract and avoids making
 * multi-plane scanout depend on the following element's CTL0 being parsed as a
 * delimiter.
 */
#define RPI5_HVS_PLANE_DWORDS           9
/* Worst-case list we emit: scanout + cursor overlay + END terminator. */
#define RPI5_HVS_MAX_DLIST_DWORDS       (2 * RPI5_HVS_PLANE_DWORDS + 1)

/*
 * Emit one unscaled plane element into Dl. An opaque plane forces a fixed
 * opaque alpha (source alpha ignored); a non-opaque plane blends with the
 * per-pixel source alpha. Returns the dword count written (always
 * RPI5_HVS_PLANE_DWORDS).
 */
ULONG
Rpi5HvsBuildPlane(
    _Out_writes_(RPI5_HVS_PLANE_DWORDS) PULONG Dl,
    _In_ BOOLEAN Opaque,
    _In_ ULONGLONG PhysAddr,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG PitchBytes,
    _In_ ULONG Format,
    _In_ ULONG Order);

/*
 * Take ownership of the active HVS scanout: install our own generated display
 * list - the opaque scanout plane, plus the cursor overlay plane when visible -
 * at the live LIST_PTR head and re-latch it.
 */
VOID
Rpi5HvsInstallScanout(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

BOOLEAN
Rpi5HvsMoveCursor(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

BOOLEAN
Rpi5HvsFlipScanout(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PHYSICAL_ADDRESS FrameBufferPhysical);

#endif /* _RPI5VC4_HVS_H_ */
