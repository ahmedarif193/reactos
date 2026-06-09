/*
 * PROJECT:     ReactOS Raspberry Pi 5 (BCM2712) display miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2712 HVS (Hardware Video Scaler) programming -
 *              multi-plane display-list generation and scanout ownership.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Inspired by the only available driver out there, which is the Linux
 * equivalent (drm/vc4).
 *
 * The HVS scans out by walking a per-channel "display list" in a small on-chip
 * RAM.  Each plane is a run of control dwords chained by CTL0.NEXT; the channel
 * LIST_PTR register holds the dword index of the active head, which the HVS
 * re-reads every frame.  We generate our own list - an opaque full-screen
 * scanout plane (configured to ignore the per-pixel source alpha, so GDI output
 * is not composited to black), optionally with a per-pixel-alpha cursor overlay
 * plane on top - and install it at the live head.  The HVS then composites our
 * planes in hardware, which is the foundation of the rest of the pipeline.
 */

#include "rpi5vc4_hvs.h"
#include "rpi5vc4_crtc.h"

/* Map the HVS register block once and cache it (re-entered on every cursor move). */
static volatile ULONG *
Rpi5HvsMap(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PHYSICAL_ADDRESS HvsPhys;

    if (DeviceExtension->HvsBase == NULL)
    {
        HvsPhys.QuadPart = RPI5_HVS_PHYS;
        DeviceExtension->HvsBase = MmMapIoSpace(HvsPhys, RPI5_HVS_LENGTH, MmNonCached);
    }

    return (volatile ULONG *)DeviceExtension->HvsBase;
}

static BOOLEAN
Rpi5HvsClipCursor(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PULONG X,
    _Out_ PULONG Y,
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONGLONG Phys)
{
    LONG CursorX = DeviceExtension->CursorX;
    LONG CursorY = DeviceExtension->CursorY;
    ULONG SourceX = 0;
    ULONG SourceY = 0;
    ULONG CursorWidth = DeviceExtension->CursorWidth;
    ULONG CursorHeight = DeviceExtension->CursorHeight;
    ULONGLONG CursorPhys = (ULONGLONG)DeviceExtension->CursorPhys.QuadPart;

    if (!DeviceExtension->CursorVisible ||
        !DeviceExtension->CursorShapeValid ||
        CursorPhys == 0 ||
        CursorWidth == 0 ||
        CursorHeight == 0)
    {
        return FALSE;
    }

    if (CursorX < 0)
    {
        SourceX = (ULONG)-CursorX;
        if (SourceX >= CursorWidth)
            return FALSE;
        CursorWidth -= SourceX;
        CursorX = 0;
    }

    if (CursorY < 0)
    {
        SourceY = (ULONG)-CursorY;
        if (SourceY >= CursorHeight)
            return FALSE;
        CursorHeight -= SourceY;
        CursorY = 0;
    }

    if ((ULONG)CursorX >= DeviceExtension->ScreenWidth ||
        (ULONG)CursorY >= DeviceExtension->ScreenHeight)
    {
        return FALSE;
    }

    if (CursorWidth > DeviceExtension->ScreenWidth - (ULONG)CursorX)
        CursorWidth = DeviceExtension->ScreenWidth - (ULONG)CursorX;
    if (CursorHeight > DeviceExtension->ScreenHeight - (ULONG)CursorY)
        CursorHeight = DeviceExtension->ScreenHeight - (ULONG)CursorY;

    if (CursorWidth == 0 || CursorHeight == 0)
        return FALSE;

    *X = (ULONG)CursorX;
    *Y = (ULONG)CursorY;
    *Width = CursorWidth;
    *Height = CursorHeight;
    *Phys = CursorPhys + ((ULONGLONG)SourceY * RPI5VC4_CURSOR_WIDTH + SourceX) *
                         sizeof(ULONG);
    return TRUE;
}

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
    _In_ ULONG Order)
{
    ULONG AlphaMask;
    ULONG Ctl2;

    if (Opaque)
    {
        /* Force a fixed, fully-opaque alpha: the source alpha byte is ignored. */
        AlphaMask = RPI5_HVS_CTL0_ALPHA_MASK_FIXED;
        Ctl2 = RPI5_HVS_CTL2_ALPHA_OPAQUE << RPI5_HVS_CTL2_ALPHA_VALUE_SHIFT;
    }
    else
    {
        /* Blend using premultiplied per-pixel source alpha for the cursor overlay. */
        AlphaMask = RPI5_HVS_CTL0_ALPHA_MASK_NONE;
        Ctl2 = RPI5_HVS_CTL2_ALPHA_PREMULT |
               (RPI5_HVS_CTL2_ALPHA_OPAQUE << RPI5_HVS_CTL2_ALPHA_VALUE_SHIFT);
    }

    /* Word 0 - CTL0. NEXT is the full element size in dwords. */
    Dl[0] = RPI5_HVS_CTL0_VALID |
            ((RPI5_HVS_PLANE_DWORDS & RPI5_HVS_CTL0_NEXT_MASK) << RPI5_HVS_CTL0_NEXT_SHIFT) |
            (AlphaMask << RPI5_HVS_CTL0_ALPHA_MASK_SHIFT) |
            RPI5_HVS_CTL0_UNITY |
            (Order << RPI5_HVS_CTL0_ORDER_SHIFT) |
            (Format << RPI5_HVS_CTL0_FORMAT_SHIFT);

    /* Word 1 - POS0: top-left position. */
    Dl[1] = ((Y & 0x1fff) << RPI5_HVS_POS0_Y_SHIFT) | (X & 0x1fff);

    /* Word 2 - CTL2: alpha mode + value. */
    Dl[2] = Ctl2;

    /* Word 3 - POS2: source size, stored as (dimension - 1). */
    Dl[3] = (((Height - 1) & 0x1fff) << RPI5_HVS_POS2_LINES_SHIFT) | ((Width - 1) & 0x1fff);

    /* Word 4 - context/status (HVS overwrites this at runtime). */
    Dl[4] = RPI5_HVS_CONTEXT_INIT;

    /* Word 5 - PTR0: upper 8 bits of the 40-bit DMA address plus UPM slot. */
    Dl[5] = (ULONG)((PhysAddr >> 32) & 0xff);
    if (!Opaque)
    {
        Dl[5] |= (RPI5_HVS_CURSOR_UPM_BASE << RPI5_HVS_PTR0_UPM_BASE_SHIFT) |
                 (RPI5_HVS_CURSOR_UPM_HANDLE << RPI5_HVS_PTR0_UPM_HANDLE_SHIFT);
    }

    /* Word 6 - PTR1: lower 32 bits of the address. */
    Dl[6] = (ULONG)(PhysAddr & 0xffffffff);

    /* Word 7 - PTR2: pitch in bytes (linear surface). */
    Dl[7] = PitchBytes & RPI5_HVS_PTR2_PITCH_MASK;

    /* Word 8 - element terminator. */
    Dl[8] = RPI5_HVS_CTL0_END;

    return RPI5_HVS_PLANE_DWORDS;
}

VOID
Rpi5HvsInstallScanout(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    volatile ULONG *Dlist;
    PVOID HvsBase;
    ULONG LptrsC, LptrsD, LptrsReg, LptrsVal, Head, Control;
    ULONG Plane[RPI5_HVS_MAX_DLIST_DWORDS];
    ULONG Count = 0;
    ULONG i;
    ULONG StartIndex = 0;
    ULONG CurCtl0, CurPtr1;
    BOOLEAN HasCursor;
    ULONG Width  = DeviceExtension->ScreenWidth;
    ULONG Height = DeviceExtension->ScreenHeight;
    ULONG Pitch  = DeviceExtension->BytesPerScanLine;
    ULONGLONG Phys = (ULONGLONG)DeviceExtension->FrameBufferPhysical.QuadPart;

    if (Width == 0 || Height == 0 || Pitch == 0 || Phys == 0)
        return;

    HvsBase = (PVOID)Rpi5HvsMap(DeviceExtension);
    if (HvsBase == NULL)
    {
        DbgPrint("RPI5VC4: HVS map failed\n");
        return;
    }
    Dlist = (volatile ULONG *)((PUCHAR)HvsBase + RPI5_HVS_DLIST_OFFSET);

    /* Read the global control and both possible LIST_PTR registers. */
    Control = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_REG_CONTROL));
    LptrsC = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_LPTRS_C));
    LptrsD = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_LPTRS_D));
    if (LptrsD != 0xffffffff && LptrsD != 0)
    {
        LptrsReg = RPI5_HVS_LPTRS_D;
        LptrsVal = LptrsD;
    }
    else
    {
        LptrsReg = RPI5_HVS_LPTRS_C;
        LptrsVal = LptrsC;
    }
    Head = LptrsVal & RPI5_HVS_LPTRS_HEAD_MASK;

    if (!(Control & RPI5_HVS_CONTROL_HVS_EN) || Head == 0)
    {
        DbgPrint("RPI5VC4: HVS not ready (ctrl=0x%08lx head=%lu) - skip\n",
                 Control, Head);
        return;
    }

    /*
     * Only take over a head that is currently a VALID plane scanning out our
     * framebuffer (the firmware's plane, or our own from a previous install).
     * Otherwise leave the hardware alone rather than corrupt an unknown list.
     */
    CurCtl0 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 0]);
    CurPtr1 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 6]);

    HasCursor = DeviceExtension->CursorVisible &&
                DeviceExtension->CursorPhys.QuadPart != 0 &&
                DeviceExtension->CursorWidth != 0 &&
                DeviceExtension->CursorHeight != 0;

    /*
     * Only take over a head holding a VALID plane that scans out our framebuffer
     * (the firmware's plane on first install) or our cursor overlay (our own
     * list on re-install). Otherwise leave the hardware alone.
     */
    {
        ULONG FbLow = (ULONG)(Phys & 0xffffffff);
        ULONG FirmwareFbLow = (ULONG)(DeviceExtension->FirmwareFrameBufferPhysical.QuadPart & 0xffffffff);
        if (!(CurCtl0 & RPI5_HVS_CTL0_VALID) ||
            (CurPtr1 != FbLow &&
             CurPtr1 != FirmwareFbLow))
        {
            DbgPrint("RPI5VC4: HVS head does not match our planes - not taking over\n");
            return;
        }
    }

    Count += Rpi5HvsBuildPlane(&Plane[Count], TRUE, Phys, 0, 0, Width, Height, Pitch,
                               RPI5_HVS_PIXEL_FORMAT_RGBA8888, RPI5_HVS_PIXEL_ORDER_BGRA);

    /*
     * If the live head already holds exactly our scanout plane (same control
     * word and framebuffer pointer), do NOT rewrite the scanout element: word 4
     * of every element is the context/status word the HVS owns and updates as it
     * scans, and stomping it on the *live* scanout plane mid-frame corrupts that
     * frame's desktop - this is the icon-change flicker. Only (re)build the
     * cursor overlay that follows it, exactly as Rpi5HvsMoveCursor leaves the
     * scanout untouched. The full scanout plane is written only on the first
     * take-over, when its control word still differs (the firmware's alpha
     * plane), which initialises the context word once.
     */
    if (CurCtl0 == Plane[0] && CurPtr1 == Plane[6])
        StartIndex = RPI5_HVS_PLANE_DWORDS;

    if (HasCursor)
    {
        ULONG CursorX;
        ULONG CursorY;
        ULONG CursorWidth = DeviceExtension->CursorWidth;
        ULONG CursorHeight = DeviceExtension->CursorHeight;
        ULONGLONG CursorPhys = (ULONGLONG)DeviceExtension->CursorPhys.QuadPart;

        if (Rpi5HvsClipCursor(DeviceExtension,
                              &CursorX,
                              &CursorY,
                              &CursorWidth,
                              &CursorHeight,
                              &CursorPhys))
        {
            Count += Rpi5HvsBuildPlane(&Plane[Count], FALSE,
                                       CursorPhys,
                                       CursorX, CursorY,
                                       CursorWidth,
                                       CursorHeight,
                                       RPI5VC4_CURSOR_WIDTH * sizeof(ULONG),
                                       RPI5_HVS_PIXEL_FORMAT_RGBA8888,
                                       RPI5_HVS_PIXEL_ORDER_BGRA);
        }
    }

    /* List terminator. */
    Plane[Count++] = RPI5_HVS_CTL0_END;

    for (i = StartIndex; i < Count; i++)
        WRITE_REGISTER_ULONG((PULONG)&Dlist[Head + i], Plane[i]);

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + LptrsReg), LptrsVal);
}

BOOLEAN
Rpi5HvsMoveCursor(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    volatile ULONG *Dlist;
    PVOID HvsBase;
    ULONG LptrsD, LptrsReg, LptrsVal, Head, Control;
    ULONG CursorHead, Ctl0, Ptr0, Ptr1;
    ULONG CursorX;
    ULONG CursorY;
    ULONG Width = DeviceExtension->CursorWidth;
    ULONG Height = DeviceExtension->CursorHeight;
    ULONGLONG CursorPhys = (ULONGLONG)DeviceExtension->CursorPhys.QuadPart;
    ULONGLONG CursorBase = CursorPhys;
    ULONGLONG CursorLimit = CursorBase +
                            RPI5VC4_CURSOR_WIDTH * RPI5VC4_CURSOR_HEIGHT *
                            sizeof(ULONG);
    ULONGLONG ActiveCursorPhys;
    ULONGLONG FrameBufferPhys = (ULONGLONG)DeviceExtension->FrameBufferPhysical.QuadPart;

    if (!Rpi5HvsClipCursor(DeviceExtension,
                           &CursorX,
                           &CursorY,
                           &Width,
                           &Height,
                           &CursorPhys))
    {
        return FALSE;
    }

    HvsBase = (PVOID)Rpi5HvsMap(DeviceExtension);
    if (HvsBase == NULL)
        return FALSE;

    Dlist = (volatile ULONG *)((PUCHAR)HvsBase + RPI5_HVS_DLIST_OFFSET);
    Control = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_REG_CONTROL));
    LptrsD = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_LPTRS_D));
    if (LptrsD != 0xffffffff && LptrsD != 0)
    {
        LptrsReg = RPI5_HVS_LPTRS_D;
        LptrsVal = LptrsD;
    }
    else
    {
        LptrsReg = RPI5_HVS_LPTRS_C;
        LptrsVal = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_LPTRS_C));
    }

    Head = LptrsVal & RPI5_HVS_LPTRS_HEAD_MASK;
    if (!(Control & RPI5_HVS_CONTROL_HVS_EN) || Head == 0)
        return FALSE;

    Ctl0 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 0]);
    Ptr0 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 5]);
    Ptr1 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 6]);
    if (!(Ctl0 & RPI5_HVS_CTL0_VALID) ||
        (Ptr0 & 0xff) != (ULONG)((FrameBufferPhys >> 32) & 0xff) ||
        Ptr1 != (ULONG)(FrameBufferPhys & 0xffffffff))
        return FALSE;

    CursorHead = Head + RPI5_HVS_PLANE_DWORDS;
    Ctl0 = READ_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 0]);
    Ptr0 = READ_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 5]);
    Ptr1 = READ_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 6]);
    ActiveCursorPhys = ((ULONGLONG)(Ptr0 & 0xff) << 32) | Ptr1;
    if (!(Ctl0 & RPI5_HVS_CTL0_VALID) ||
        ActiveCursorPhys < CursorBase ||
        ActiveCursorPhys >= CursorLimit)
        return FALSE;

    WRITE_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 1],
                         ((CursorY & 0x1fff) << RPI5_HVS_POS0_Y_SHIFT) |
                         (CursorX & 0x1fff));
    WRITE_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 3],
                         (((Height - 1) & 0x1fff) << RPI5_HVS_POS2_LINES_SHIFT) |
                         ((Width - 1) & 0x1fff));
    WRITE_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 5],
                         (Ptr0 & ~0xffu) | (ULONG)((CursorPhys >> 32) & 0xff));
    WRITE_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 6],
                         (ULONG)(CursorPhys & 0xffffffff));
    WRITE_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 7],
                         RPI5VC4_CURSOR_WIDTH * sizeof(ULONG));

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + LptrsReg), LptrsVal);
    return TRUE;
}

BOOLEAN
Rpi5HvsFlipScanout(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PHYSICAL_ADDRESS FrameBufferPhysical)
{
    volatile ULONG *Dlist;
    PVOID HvsBase;
    ULONG LptrsD, LptrsReg, LptrsVal, Head, Control, Ctl0, Ptr0, Ptr1;
    ULONGLONG Phys = (ULONGLONG)FrameBufferPhysical.QuadPart;
    ULONGLONG CurrentPhys = (ULONGLONG)DeviceExtension->FrameBufferPhysical.QuadPart;

    if (Phys == 0)
        return FALSE;

    if (Phys == CurrentPhys)
        return TRUE;

    Rpi5CrtcWaitForVBlank(DeviceExtension);

    HvsBase = (PVOID)Rpi5HvsMap(DeviceExtension);
    if (HvsBase == NULL)
        return FALSE;

    Dlist = (volatile ULONG *)((PUCHAR)HvsBase + RPI5_HVS_DLIST_OFFSET);
    Control = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_REG_CONTROL));
    LptrsD = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_LPTRS_D));
    if (LptrsD != 0xffffffff && LptrsD != 0)
    {
        LptrsReg = RPI5_HVS_LPTRS_D;
        LptrsVal = LptrsD;
    }
    else
    {
        LptrsReg = RPI5_HVS_LPTRS_C;
        LptrsVal = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_LPTRS_C));
    }

    Head = LptrsVal & RPI5_HVS_LPTRS_HEAD_MASK;
    if (!(Control & RPI5_HVS_CONTROL_HVS_EN) || Head == 0)
        return FALSE;

    Ctl0 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 0]);
    if (!(Ctl0 & RPI5_HVS_CTL0_VALID))
        return FALSE;

    Ptr0 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 5]);
    Ptr1 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 6]);
    if (((Ptr0 & 0xff) != (ULONG)((CurrentPhys >> 32) & 0xff)) ||
        Ptr1 != (ULONG)(CurrentPhys & 0xffffffff))
        return FALSE;

    Ptr0 = (Ptr0 & ~0xffu) | (ULONG)((Phys >> 32) & 0xff);
    WRITE_REGISTER_ULONG((PULONG)&Dlist[Head + 5], Ptr0);
    WRITE_REGISTER_ULONG((PULONG)&Dlist[Head + 6], (ULONG)(Phys & 0xffffffff));

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + LptrsReg), LptrsVal);
    DeviceExtension->FrameBufferPhysical = FrameBufferPhysical;
    return TRUE;
}
