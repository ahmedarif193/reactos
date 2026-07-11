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

/* Bring-up diagnostics removed; retained as a no-op so the call sites and the
 * real error prints around them stay intact. */
#define RPI5_HVS_DIAG(Dev, ...) ((void)(Dev))

/*
 * Select the live display-list head.  The C-step vs D-step LPTRS register
 * guess (D nonzero/non-FF) proved too naive for silicon: validate BOTH
 * candidates against the list itself (CTL0.VALID at the head, in-range
 * head index) and prefer the head whose plane actually scans one of our
 * expected framebuffers.  Prints every raw value on the first calls so a
 * serial log pinpoints the real hardware state.
 */
static BOOLEAN
Rpi5HvsSelectHead(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PVOID HvsBase,
    _Out_ PULONG LptrsRegOut,
    _Out_ PULONG LptrsValOut,
    _Out_ PULONG HeadOut,
    _In_ ULONG ExpectPtr1A,
    _In_ ULONG ExpectPtr1B)
{
    volatile ULONG *Dlist =
        (volatile ULONG *)((PUCHAR)HvsBase + RPI5_HVS_DLIST_OFFSET);
    ULONG Regs[2] = { RPI5_HVS_LPTRS_D, RPI5_HVS_LPTRS_C };
    ULONG Control;
    ULONG BestReg = 0, BestVal = 0, BestHead = 0;
    BOOLEAN BestMatches = FALSE, HaveValid = FALSE;
    ULONG i;

    Control = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_REG_CONTROL));
    if (!(Control & RPI5_HVS_CONTROL_HVS_EN))
    {
        RPI5_HVS_DIAG(DeviceExtension,
                      "RPI5VC4: HVS disabled (CTRL=%08lx)\n", Control);
        return FALSE;
    }

    for (i = 0; i < 2; i++)
    {
        ULONG Val, Head;
        ULONG Ctl0, Ptr1;
        BOOLEAN Valid, Matches;

        /* A channel that reads all-ones long enough is not populated on
         * this stepping; stop probing it every frame. */
        if (DeviceExtension->HvsLptrsDead[i] >= 8)
            continue;

        Val = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + Regs[i]));
        Head = Val & RPI5_HVS_LPTRS_HEAD_MASK;

        if (Val == 0xffffffff || Head == 0 ||
            Head + RPI5_HVS_PLANE_DWORDS >= RPI5_HVS_DLIST_DWORDS)
        {
            if (Val == 0xffffffff &&
                DeviceExtension->HvsLptrsDead[i] < 8)
                DeviceExtension->HvsLptrsDead[i]++;
            RPI5_HVS_DIAG(DeviceExtension,
                          "RPI5VC4: LPTRS@0x%03lx=%08lx head=%lu (skip)\n",
                          Regs[i], Val, Head);
            continue;
        }
        DeviceExtension->HvsLptrsDead[i] = 0;

        Ctl0 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 0]);
        Ptr1 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 6]);
        Valid = (Ctl0 & RPI5_HVS_CTL0_VALID) != 0;
        Matches = Valid && (Ptr1 == ExpectPtr1A || Ptr1 == ExpectPtr1B);

        RPI5_HVS_DIAG(DeviceExtension,
                      "RPI5VC4: LPTRS@0x%03lx=%08lx head=%lu ctl0=%08lx "
                      "ptr1=%08lx valid=%u match=%u\n",
                      Regs[i], Val, Head, Ctl0, Ptr1, Valid, Matches);

        if (Matches && !BestMatches)
        {
            BestReg = Regs[i]; BestVal = Val; BestHead = Head;
            BestMatches = TRUE; HaveValid = TRUE;
        }
        else if (Valid && !HaveValid)
        {
            BestReg = Regs[i]; BestVal = Val; BestHead = Head;
            HaveValid = TRUE;
        }
    }

    if (!HaveValid)
        return FALSE;

    *LptrsRegOut = BestReg;
    *LptrsValOut = BestVal;
    *HeadOut = BestHead;
    return TRUE;
}

VOID
Rpi5HvsInstallScanout(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->Headless)
        return;

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

    DeviceExtension->HvsCursorFastValid = FALSE;

    HvsBase = (PVOID)Rpi5HvsMap(DeviceExtension);
    if (HvsBase == NULL)
    {
        DbgPrint("RPI5VC4: HVS map failed\n");
        return;
    }
    Dlist = (volatile ULONG *)((PUCHAR)HvsBase + RPI5_HVS_DLIST_OFFSET);

    if (!Rpi5HvsSelectHead(DeviceExtension, HvsBase,
                           &LptrsReg, &LptrsVal, &Head,
                           (ULONG)(Phys & 0xffffffff),
                           (ULONG)(DeviceExtension->FirmwareFrameBufferPhysical.QuadPart & 0xffffffff)))
    {
        DbgPrint("RPI5VC4: InstallScanout: no usable HVS head - skip\n");
        return;
    }
    (VOID)LptrsC; (VOID)LptrsD; (VOID)Control;

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
            DbgPrint("RPI5VC4: takeover refused: head=%lu ctl0=%08lx "
                     "ptr1=%08lx (want %08lx or %08lx)\n",
                     Head, CurCtl0, CurPtr1, FbLow, FirmwareFbLow);
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

    /*
     * Rewriting a live in-place cursor element: keep the HVS-owned context
     * word (word 4) — resetting it mid-scan corrupts the overlay for that
     * frame, the same mechanism as the scanout icon-change flicker above.
     */
    if (StartIndex == RPI5_HVS_PLANE_DWORDS &&
        Count > StartIndex + RPI5_HVS_PLANE_DWORDS &&
        READ_REGISTER_ULONG((PULONG)&Dlist[Head + StartIndex]) == Plane[StartIndex] &&
        READ_REGISTER_ULONG((PULONG)&Dlist[Head + StartIndex + 6]) == Plane[StartIndex + 6])
    {
        Plane[StartIndex + 4] =
            READ_REGISTER_ULONG((PULONG)&Dlist[Head + StartIndex + 4]);
    }

    /*
     * Descending order: an element's CTL0 VALID bit is its lowest word and
     * lands last, so a mid-frame list walk never sees a half-written element.
     */
    for (i = Count; i > StartIndex; i--)
        WRITE_REGISTER_ULONG((PULONG)&Dlist[Head + i - 1], Plane[i - 1]);

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + LptrsReg), LptrsVal);

    RPI5_HVS_DIAG(DeviceExtension,
                  "RPI5VC4: takeover OK reg=0x%03lx head=%lu fb=%02lx:%08lx "
                  "cursor=%u start=%lu count=%lu\n",
                  LptrsReg, Head,
                  (ULONG)((Phys >> 32) & 0xff), (ULONG)(Phys & 0xffffffff),
                  HasCursor, StartIndex, Count);

    /* Cache the cursor-plane location so moves can skip re-validation reads. */
    if (Count == 2 * RPI5_HVS_PLANE_DWORDS + 1)
    {
        DeviceExtension->HvsLptrsReg = LptrsReg;
        DeviceExtension->HvsLptrsVal = LptrsVal;
        DeviceExtension->HvsCursorHead = Head + RPI5_HVS_PLANE_DWORDS;
        DeviceExtension->HvsCursorFastValid = TRUE;
    }
}

BOOLEAN
Rpi5HvsMoveCursor(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->Headless)
        return TRUE;

    volatile ULONG *Dlist;
    PVOID HvsBase;
    ULONG LptrsVal;
    ULONG CursorHead;
    ULONG CursorX;
    ULONG CursorY;
    ULONG Width = DeviceExtension->CursorWidth;
    ULONG Height = DeviceExtension->CursorHeight;
    ULONGLONG CursorPhys = (ULONGLONG)DeviceExtension->CursorPhys.QuadPart;

    /* Fall back to a full Rpi5HvsInstallScanout when no validated plane is live. */
    if (!DeviceExtension->HvsCursorFastValid)
        return FALSE;

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

    /* One guard read: if the live head moved, re-validate via the full path. */
    LptrsVal = READ_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + DeviceExtension->HvsLptrsReg));
    if (LptrsVal != DeviceExtension->HvsLptrsVal)
    {
        DeviceExtension->HvsCursorFastValid = FALSE;
        return FALSE;
    }

    Dlist = (volatile ULONG *)((PUCHAR)HvsBase + RPI5_HVS_DLIST_OFFSET);
    CursorHead = DeviceExtension->HvsCursorHead;

    WRITE_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 1], ((CursorY & 0x1fff) << RPI5_HVS_POS0_Y_SHIFT) | (CursorX & 0x1fff));
    WRITE_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 3], (((Height - 1) & 0x1fff) << RPI5_HVS_POS2_LINES_SHIFT) | ((Width - 1) & 0x1fff));
    WRITE_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 5], (RPI5_HVS_CURSOR_UPM_BASE << RPI5_HVS_PTR0_UPM_BASE_SHIFT) | (RPI5_HVS_CURSOR_UPM_HANDLE << RPI5_HVS_PTR0_UPM_HANDLE_SHIFT) | (ULONG)((CursorPhys >> 32) & 0xff));
    WRITE_REGISTER_ULONG((PULONG)&Dlist[CursorHead + 6], (ULONG)(CursorPhys & 0xffffffff));

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + DeviceExtension->HvsLptrsReg), LptrsVal);
    return TRUE;
}

BOOLEAN
Rpi5HvsInstallPlaneList(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_reads_(Count) CONST RPI5VC4_HVS_PLANE *Planes,
    _In_ ULONG Count)
{
    if (DeviceExtension->Headless)
        return TRUE;

    volatile ULONG *Dlist;
    PVOID HvsBase;
    ULONG List[RPI5_HVS_PRIVATE_SLOT_DWORDS];
    ULONG Used = 0;
    ULONG LptrsD, LptrsReg, LptrsVal, Control, Slot;
    ULONG CursorAt = 0;
    ULONG i;

    if (Count == 0 || Count > RPI5_HVS_MPO_MAX_PLANES)
        return FALSE;

    DeviceExtension->HvsCursorFastValid = FALSE;

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

    if (!(Control & RPI5_HVS_CONTROL_HVS_EN) ||
        (LptrsVal & RPI5_HVS_LPTRS_HEAD_MASK) == 0)
    {
        return FALSE;
    }

    /* Linux vc6_hvs_hw_init parity (it-30/31): the firmware handover
     * leaves the HVS AXI request cap and arbiter priority unprogrammed;
     * an uncapped HVS scanning the slab starves the V3D PTB final flush
     * (the slab-region wedge).  Cap to Linux's values once. */
    {
        ULONG NewControl = Control;

        NewControl &= ~(RPI5_HVS_CONTROL_PF_LINES_MASK |
                        RPI5_HVS_CONTROL_MAX_REQS_MASK);
        NewControl |= (8u << RPI5_HVS_CONTROL_PF_LINES_SHIFT) |
                      (15u << RPI5_HVS_CONTROL_MAX_REQS_SHIFT);
        if (NewControl != Control)
        {
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_REG_CONTROL),
                                 NewControl);
        }
        /* it-32: LOW HVS arbiter priority (Linux maxes it to protect
         * scanout; on this arbiter that starves the V3D PTB — the
         * remaining big-job wedge).  MAX_REQS cap stays. */
        if (LptrsReg == RPI5_HVS_LPTRS_D)
        {
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_PRI_MAP0_D), 0);
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_PRI_MAP1_D), 0);
        }
        else
        {
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_PRI_MAP0_C), 0);
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + RPI5_HVS_PRI_MAP1_C), 0);
        }
    }

    /* Build bottom-up: layer 0 first, cursor overlay on top. */
    for (i = 0; i < Count; i++)
    {
        if (Planes[i].Phys == 0 ||
            Planes[i].Width == 0 || Planes[i].Height == 0 ||
            Planes[i].PitchBytes == 0)
        {
            return FALSE;
        }

        Used += Rpi5HvsBuildPlane(&List[Used],
                                  Planes[i].Opaque,
                                  Planes[i].Phys,
                                  Planes[i].X, Planes[i].Y,
                                  Planes[i].Width, Planes[i].Height,
                                  Planes[i].PitchBytes,
                                  RPI5_HVS_PIXEL_FORMAT_RGBA8888,
                                  RPI5_HVS_PIXEL_ORDER_BGRA);
    }

    {
        ULONG CursorX, CursorY, CursorWidth, CursorHeight;
        ULONGLONG CursorPhys;

        if (Rpi5HvsClipCursor(DeviceExtension,
                              &CursorX, &CursorY,
                              &CursorWidth, &CursorHeight,
                              &CursorPhys))
        {
            CursorAt = Used;
            Used += Rpi5HvsBuildPlane(&List[Used], FALSE,
                                      CursorPhys,
                                      CursorX, CursorY,
                                      CursorWidth, CursorHeight,
                                      RPI5VC4_CURSOR_WIDTH * sizeof(ULONG),
                                      RPI5_HVS_PIXEL_FORMAT_RGBA8888,
                                      RPI5_HVS_PIXEL_ORDER_BGRA);
        }
    }

    List[Used++] = RPI5_HVS_CTL0_END;

    /* Double-buffer between the two private slots, then re-point the head. */
    Slot = (DeviceExtension->HvsActivePrivateSlot == RPI5_HVS_PRIVATE_SLOT_A)
               ? RPI5_HVS_PRIVATE_SLOT_B : RPI5_HVS_PRIVATE_SLOT_A;

    for (i = 0; i < Used; i++)
        WRITE_REGISTER_ULONG((PULONG)&Dlist[Slot + i], List[i]);

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    LptrsVal = (LptrsVal & ~RPI5_HVS_LPTRS_HEAD_MASK) | Slot;
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + LptrsReg), LptrsVal);
    DeviceExtension->HvsActivePrivateSlot = Slot;

    /* Keep the cursor-move fast path alive on the private list. */
    if (CursorAt != 0)
    {
        DeviceExtension->HvsLptrsReg = LptrsReg;
        DeviceExtension->HvsLptrsVal = LptrsVal;
        DeviceExtension->HvsCursorHead = Slot + CursorAt;
        DeviceExtension->HvsCursorFastValid = TRUE;
    }

    return TRUE;
}

BOOLEAN
Rpi5HvsFlipScanoutEx(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PHYSICAL_ADDRESS FrameBufferPhysical,
    _In_ BOOLEAN WaitVBlank)
{
    if (DeviceExtension->Headless)
        return TRUE;

    volatile ULONG *Dlist;
    PVOID HvsBase;
    ULONG LptrsD, LptrsReg, LptrsVal, Head, Control, Ctl0, Ptr0, Ptr1;
    ULONGLONG Phys = (ULONGLONG)FrameBufferPhysical.QuadPart;
    ULONGLONG CurrentPhys = (ULONGLONG)DeviceExtension->FrameBufferPhysical.QuadPart;

    if (Phys == 0)
        return FALSE;

    if (Phys == CurrentPhys)
        return TRUE;

    /* Latched off after repeated silicon failures: fail FAST so the
     * present path doesn't burn a vblank wait per frame (this is what
     * swamped the delayed work queue on the first hardware run). */
    if (DeviceExtension->HvsFlipBroken)
        return FALSE;

    /* The pointer words are latched at frame start, so a flip within one
     * 4GB window (single PTR1 write) is atomic without any wait — the
     * triple-buffered present path relies on that. Callers replacing the
     * buffer wholesale (park, MPO base) still serialize on the vblank. */
    if (WaitVBlank)
        Rpi5CrtcWaitForVBlank(DeviceExtension);

    HvsBase = (PVOID)Rpi5HvsMap(DeviceExtension);
    if (HvsBase == NULL)
        return FALSE;

    Dlist = (volatile ULONG *)((PUCHAR)HvsBase + RPI5_HVS_DLIST_OFFSET);
    (VOID)Control; (VOID)LptrsD;

    if (!Rpi5HvsSelectHead(DeviceExtension, HvsBase,
                           &LptrsReg, &LptrsVal, &Head,
                           (ULONG)(CurrentPhys & 0xffffffff),
                           (ULONG)(Phys & 0xffffffff)))
    {
        goto FlipFailed;
    }

    Ctl0 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 0]);
    if (!(Ctl0 & RPI5_HVS_CTL0_VALID))
    {
        RPI5_HVS_DIAG(DeviceExtension,
                      "RPI5VC4: flip: head %lu CTL0=%08lx not VALID\n",
                      Head, Ctl0);
        goto FlipFailed;
    }

    Ptr0 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 5]);
    Ptr1 = READ_REGISTER_ULONG((PULONG)&Dlist[Head + 6]);
    if (((Ptr0 & 0xff) != (ULONG)((CurrentPhys >> 32) & 0xff)) ||
        Ptr1 != (ULONG)(CurrentPhys & 0xffffffff))
    {
        RPI5_HVS_DIAG(DeviceExtension,
                      "RPI5VC4: flip: head %lu scans %02lx:%08lx, expected "
                      "%02lx:%08lx (target %02lx:%08lx)\n",
                      Head, Ptr0 & 0xff, Ptr1,
                      (ULONG)((CurrentPhys >> 32) & 0xff),
                      (ULONG)(CurrentPhys & 0xffffffff),
                      (ULONG)((Phys >> 32) & 0xff),
                      (ULONG)(Phys & 0xffffffff));
        goto FlipFailed;
    }

    Ptr0 = (Ptr0 & ~0xffu) | (ULONG)((Phys >> 32) & 0xff);
    WRITE_REGISTER_ULONG((PULONG)&Dlist[Head + 5], Ptr0);
    WRITE_REGISTER_ULONG((PULONG)&Dlist[Head + 6], (ULONG)(Phys & 0xffffffff));

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HvsBase + LptrsReg), LptrsVal);
    DeviceExtension->FrameBufferPhysical = FrameBufferPhysical;

    if (DeviceExtension->HvsFlipFailCount != 0)
        DeviceExtension->HvsFlipFailCount = 0;
    RPI5_HVS_DIAG(DeviceExtension,
                  "RPI5VC4: flip OK head=%lu -> %02lx:%08lx\n",
                  Head, (ULONG)((Phys >> 32) & 0xff),
                  (ULONG)(Phys & 0xffffffff));
    return TRUE;

FlipFailed:
    if (++DeviceExtension->HvsFlipFailCount >= 16 &&
        !DeviceExtension->HvsFlipBroken)
    {
        DeviceExtension->HvsFlipBroken = TRUE;
        DbgPrint("RPI5VC4: 16 consecutive flip failures — latching the "
                 "flip path OFF (desktop stays on the takeover buffer; "
                 "see serial diagnostics above for the hardware state)\n");
    }
    return FALSE;
}

BOOLEAN
Rpi5HvsFlipScanout(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PHYSICAL_ADDRESS FrameBufferPhysical)
{
    return Rpi5HvsFlipScanoutEx(DeviceExtension, FrameBufferPhysical, TRUE);
}
