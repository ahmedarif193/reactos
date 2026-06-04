/*
 * PROJECT:     ReactOS RPi5 Display-Only WDDM Miniport (rpi5dod)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2712 HVS (Hardware Video Scaler) scanout-plane programming
 *              for the firmware-GOP present path: make the live scanout plane
 *              opaque so the GDI/DWM desktop is shown instead of composited to
 *              black.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * The VideoCore VII HVS scans out by walking a per-channel display list in a
 * small on-chip RAM.  Each plane is a run of control dwords chained by
 * CTL0.NEXT; the channel LIST_PTR register holds the dword index of the active
 * head, which the HVS re-reads every frame.  The UEFI firmware leaves a plane
 * scanning out the GOP framebuffer, but configured to honour the per-pixel
 * SOURCE ALPHA.  GDI/DWM write 0x00RRGGBB (alpha byte 0), so the desktop
 * background composites transparent and scans out BLACK.  We rewrite the live
 * plane's CTL0.ALPHA_MASK to FIXED with an opaque CTL2 value, so the source
 * alpha is ignored and the desktop is shown.
 *
 * This is the display-only counterpart of the rpi5vc4 XPDM miniport's
 * Rpi5HvsInstallScanout (win32ss/drivers/miniport/rpi5vc4/rpi5vc4_hvs.c): the
 * same opaque-plane technique, without the cursor overlay (the WDDM DOD uses a
 * software cursor) and without flips (the firmware FB is the scanout surface).
 */

#include "rpi5dod.h"
#include "rpi5dod_hvs.h"

#define NDEBUG
#include <debug.h>

#define R5DOD_TRACE(fmt, ...) DbgPrint("RPI5DOD: " fmt, ##__VA_ARGS__)

/*
 * Build a single opaque, full-screen unity scanout plane into Dl[0..8].
 * Mirrors the rpi5vc4 dword layout (gen6 unity):
 *   CTL0, POS0, CTL2, POS2, context, PTR0(upper8 addr), PTR1(lower32 addr),
 *   PTR2(pitch), END.
 */
static VOID
Rpi5DodHvsBuildOpaquePlane(
    _Out_writes_(RPI5DOD_HVS_PLANE_DWORDS) PULONG Dl,
    _In_ ULONGLONG PhysAddr,
    _In_ ULONG     Width,
    _In_ ULONG     Height,
    _In_ ULONG     PitchBytes)
{
    /* CTL0: valid, NEXT = element size in dwords, fixed (opaque) alpha mask,
     * unity scale, BGRA order, RGBA8888 format. */
    Dl[0] = RPI5DOD_HVS_CTL0_VALID |
            ((RPI5DOD_HVS_PLANE_DWORDS & RPI5DOD_HVS_CTL0_NEXT_MASK) << RPI5DOD_HVS_CTL0_NEXT_SHIFT) |
            (RPI5DOD_HVS_CTL0_ALPHA_MASK_FIXED << RPI5DOD_HVS_CTL0_ALPHA_MASK_SHIFT) |
            RPI5DOD_HVS_CTL0_UNITY |
            (RPI5DOD_HVS_PIXEL_ORDER_BGRA << RPI5DOD_HVS_CTL0_ORDER_SHIFT) |
            (RPI5DOD_HVS_PIXEL_FORMAT_RGBA8888 << RPI5DOD_HVS_CTL0_FORMAT_SHIFT);

    /* POS0: top-left at (0,0). */
    Dl[1] = 0;

    /* CTL2: fixed, fully-opaque alpha value. */
    Dl[2] = RPI5DOD_HVS_CTL2_ALPHA_OPAQUE << RPI5DOD_HVS_CTL2_ALPHA_VALUE_SHIFT;

    /* POS2: source size, stored as (dimension - 1). */
    Dl[3] = (((Height - 1) & 0x1fff) << RPI5DOD_HVS_POS2_LINES_SHIFT) |
            ((Width - 1) & 0x1fff);

    /* context/status (HVS owns this at runtime). */
    Dl[4] = RPI5DOD_HVS_CONTEXT_INIT;

    /* PTR0: upper 8 bits of the 40-bit DMA address. */
    Dl[5] = (ULONG)((PhysAddr >> 32) & 0xff);

    /* PTR1: lower 32 bits of the address. */
    Dl[6] = (ULONG)(PhysAddr & 0xffffffff);

    /* PTR2: pitch in bytes (linear surface). */
    Dl[7] = PitchBytes & RPI5DOD_HVS_PTR2_PITCH_MASK;

    /* element terminator. */
    Dl[8] = RPI5DOD_HVS_CTL0_END;
}

BOOLEAN
Rpi5DodHvsMakeScanoutOpaque(
    _In_ ULONGLONG FrameBufferPhys,
    _In_ ULONG     Width,
    _In_ ULONG     Height,
    _In_ ULONG     PitchBytes)
{
    PHYSICAL_ADDRESS HvsPhys;
    volatile ULONG  *Dlist;
    PVOID            HvsBase;
    ULONG            Control, LptrsC, LptrsD, LptrsReg, LptrsVal, Head;
    ULONG            CurCtl0, CurPtr0, CurPtr1;
    ULONG            Plane[RPI5DOD_HVS_PLANE_DWORDS];
    ULONG            FbLow  = (ULONG)(FrameBufferPhys & 0xffffffff);
    ULONG            FbHigh = (ULONG)((FrameBufferPhys >> 32) & 0xff);
    ULONG            i;

    if (FrameBufferPhys == 0 || Width == 0 || Height == 0 || PitchBytes == 0)
        return FALSE;

    HvsPhys.QuadPart = RPI5DOD_HVS_PHYS;
    HvsBase = MmMapIoSpace(HvsPhys, RPI5DOD_HVS_LENGTH, MmNonCached);
    if (HvsBase == NULL)
    {
        R5DOD_TRACE("HvsMakeScanoutOpaque: HVS map failed\n");
        return FALSE;
    }
    Dlist = (volatile ULONG *)((PUCHAR)HvsBase + RPI5DOD_HVS_DLIST_OFFSET);

    /* Read the global control and both possible LIST_PTR registers (D-step
     * first, then C-step). */
    Control = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5DOD_HVS_REG_CONTROL));
    LptrsC  = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5DOD_HVS_LPTRS_C));
    LptrsD  = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5DOD_HVS_LPTRS_D));
    if (LptrsD != 0xffffffff && LptrsD != 0)
    {
        LptrsReg = RPI5DOD_HVS_LPTRS_D;
        LptrsVal = LptrsD;
    }
    else
    {
        LptrsReg = RPI5DOD_HVS_LPTRS_C;
        LptrsVal = LptrsC;
    }
    Head = LptrsVal & RPI5DOD_HVS_LPTRS_HEAD_MASK;

    if (!(Control & RPI5DOD_HVS_CONTROL_HVS_EN) || Head == 0)
    {
        R5DOD_TRACE("HvsMakeScanoutOpaque: HVS not ready (ctrl=0x%08lX head=%lu)\n",
                    Control, Head);
        MmUnmapIoSpace(HvsBase, RPI5DOD_HVS_LENGTH);
        return FALSE;
    }

    /*
     * Only take over a head that is a VALID plane scanning out the GOP
     * framebuffer (the firmware's plane on first install, or our own opaque
     * plane on a re-install).  Otherwise leave the hardware alone.
     */
    CurCtl0 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 0]);
    CurPtr0 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 5]);
    CurPtr1 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 6]);

    if (!(CurCtl0 & RPI5DOD_HVS_CTL0_VALID) ||
        CurPtr1 != FbLow ||
        (CurPtr0 & 0xff) != FbHigh)
    {
        R5DOD_TRACE("HvsMakeScanoutOpaque: head plane does not match GOP FB "
                    "(ctl0=0x%08lX ptr0=0x%08lX ptr1=0x%08lX want hi=0x%lX lo=0x%08lX) - skip\n",
                    CurCtl0, CurPtr0, CurPtr1, FbHigh, FbLow);
        MmUnmapIoSpace(HvsBase, RPI5DOD_HVS_LENGTH);
        return FALSE;
    }

    Rpi5DodHvsBuildOpaquePlane(Plane, FrameBufferPhys, Width, Height, PitchBytes);

    /*
     * If the live head already holds exactly our opaque plane (same control
     * word and FB pointer), do nothing: word 4 is the context/status word the
     * HVS owns and updates while scanning, and rewriting the live plane
     * mid-frame would corrupt that frame.  We only need to flip the firmware's
     * alpha plane to opaque ONCE.
     */
    if (CurCtl0 == Plane[0] && CurPtr1 == Plane[6])
    {
        R5DOD_TRACE("HvsMakeScanoutOpaque: scanout already opaque - no change\n");
        MmUnmapIoSpace(HvsBase, RPI5DOD_HVS_LENGTH);
        return TRUE;
    }

    /* Write the opaque plane over the firmware's plane at the live head. */
    for (i = 0; i < RPI5DOD_HVS_PLANE_DWORDS; i++)
        WRITE_REGISTER_ULONG((PULONG)&Dlist[Head + i], Plane[i]);

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    /* Re-arm the head so the HVS re-reads the list. */
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + LptrsReg), LptrsVal);

    MmUnmapIoSpace(HvsBase, RPI5DOD_HVS_LENGTH);

    R5DOD_TRACE("HvsMakeScanoutOpaque: scanout plane made opaque (head=%lu %lux%lu pitch=%lu)\n",
                Head, Width, Height, PitchBytes);
    return TRUE;
}
