/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM display-only miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Display-only present path: dirty-rect blits from the OS
 *              shadow surface into the write-combined firmware framebuffer
 *              the HVS scans out, plus the bugcheck-time system display
 *              DDIs and the POST framebuffer release handoff.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5vc4.h"
#include "rpi5vc4_hvs.h"
#include "rpi5vc4_crtc.h"

#define NDEBUG
#include <reactos/debug.h>

/*
 * Kernel VA of the surface the HVS is currently scanning: the firmware
 * framebuffer at boot, or a flipped-to allocation inside the VRAM slab
 * after DxgkDdiSetVidPnSourceAddress.
 */
PVOID
Rpi5Vc4CurrentScanoutVa(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    ULONGLONG Current = (ULONGLONG)DeviceExtension->FrameBufferPhysical.QuadPart;
    ULONGLONG SlabBase = (ULONGLONG)DeviceExtension->VramPhysical.QuadPart;
    ULONGLONG RingBase = (ULONGLONG)DeviceExtension->FlipBufPhys.QuadPart;
    ULONGLONG RingSize = (ULONGLONG)DeviceExtension->FlipBufSize *
                         DeviceExtension->FlipBufCount;

    if (Current == (ULONGLONG)DeviceExtension->FirmwareFrameBufferPhysical.QuadPart)
        return DeviceExtension->FrameBufferVa;

    if (DeviceExtension->FlipBufVa != NULL &&
        RingSize != 0 &&
        Current >= RingBase &&
        Current < RingBase + RingSize)
    {
        return (PUCHAR)DeviceExtension->FlipBufVa + (Current - RingBase);
    }

    if (DeviceExtension->VramVa != NULL &&
        Current >= SlabBase &&
        Current < SlabBase + DeviceExtension->VramSize)
    {
        return (PUCHAR)DeviceExtension->VramVa + (Current - SlabBase);
    }

    return DeviceExtension->FrameBufferVa;
}

/* Copy one clipped rectangle of 32bpp pixels into a scanout-layout surface. */
static VOID
Rpi5Vc4BlitRectTo(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PUCHAR Destination,
    _In_reads_bytes_(_Inexpressible_("pitch * height")) const UCHAR *Source,
    _In_ LONG SourcePitch,
    _In_ const RECT *Rect)
{
    LONG Left = Rect->left;
    LONG Top = Rect->top;
    LONG Right = Rect->right;
    LONG Bottom = Rect->bottom;
    LONG Row;
    SIZE_T RowBytes;

    if (Left < 0)
        Left = 0;
    if (Top < 0)
        Top = 0;
    if (Right > (LONG)DeviceExtension->ScreenWidth)
        Right = (LONG)DeviceExtension->ScreenWidth;
    if (Bottom > (LONG)DeviceExtension->ScreenHeight)
        Bottom = (LONG)DeviceExtension->ScreenHeight;

    if (Left >= Right || Top >= Bottom)
        return;

    /* Round the span out to whole 64-byte lines so the copy into the
     * write-combined scanout runs as co-aligned full-line bursts. */
    Left &= ~15L;
    Right = (Right + 15) & ~15L;
    if (Right > (LONG)DeviceExtension->ScreenWidth)
        Right = (LONG)DeviceExtension->ScreenWidth;

    RowBytes = (SIZE_T)(Right - Left) * 4;

    for (Row = Top; Row < Bottom; Row++)
    {
        RtlCopyMemory(Destination + (SIZE_T)Row * DeviceExtension->BytesPerScanLine +
                          (SIZE_T)Left * 4,
                      Source + (SIZE_T)Row * SourcePitch + (SIZE_T)Left * 4,
                      RowBytes);
    }
}

static VOID
Rpi5Vc4BlitRect(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(_Inexpressible_("pitch * height")) const UCHAR *Source,
    _In_ LONG SourcePitch,
    _In_ const RECT *Rect)
{
    Rpi5Vc4BlitRectTo(DeviceExtension,
                      Rpi5Vc4CurrentScanoutVa(DeviceExtension),
                      Source, SourcePitch, Rect);
}

/*
 * Move rects: the OS keeps pSource current, so a move is satisfied by
 * blitting the destination rectangle from the source surface (the
 * Microsoft basic-display approach).  This avoids screen-to-screen copies
 * that would read back through the uncached write-combined scanout mapping.
 */
static VOID
Rpi5Vc4MoveRect(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(_Inexpressible_("pitch * height")) const UCHAR *Source,
    _In_ LONG SourcePitch,
    _In_ const D3DKMT_MOVE_RECT *Move)
{
    Rpi5Vc4BlitRect(DeviceExtension, Source, SourcePitch, &Move->DestRect);
}

/* ========================================================================
 * Wayland-style flip presents: compose into an off-screen buffer of a
 * private triple-buffer ring, then atomically page-flip the HVS plane
 * (pointer words latch at frame start). pSource is always the complete
 * current frame, so each buffer catches up via a per-buffer stale rect.
 * Falls back to the classic blit-into-live-scanout path when the ring
 * cannot be allocated or the flip path is latched broken.
 * ====================================================================== */

static VOID
Rpi5Vc4RectUnion(_Inout_ RECT *Union, _In_ const RECT *Add)
{
    if (Add->right <= Add->left || Add->bottom <= Add->top)
        return;
    if (Union->right <= Union->left || Union->bottom <= Union->top)
    {
        *Union = *Add;
        return;
    }
    if (Add->left < Union->left) Union->left = Add->left;
    if (Add->top < Union->top) Union->top = Add->top;
    if (Add->right > Union->right) Union->right = Add->right;
    if (Add->bottom > Union->bottom) Union->bottom = Add->bottom;
}

static BOOLEAN
Rpi5Vc4EnsureFlipRing(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    ULONG BufSize = DeviceExtension->BytesPerScanLine *
                    DeviceExtension->ScreenHeight;
    ULONG Count;
    ULONG i;

    if (DeviceExtension->FlipPresentBroken ||
        DeviceExtension->HvsFlipBroken ||
        BufSize == 0)
    {
        return FALSE;
    }

    if (DeviceExtension->FlipBufVa != NULL)
    {
        /* Mode changed since allocation: give up on flips (the Pi firmware
         * mode is fixed for the session; do not churn 25MB allocations). */
        if (DeviceExtension->FlipBufSize != BufSize)
            return FALSE;
        return DeviceExtension->FlipBufCount != 0;
    }

    Low.QuadPart = 0x40000000ULL;
    High.QuadPart = 0xFFFFFFFFFFULL;
    Boundary.QuadPart = 0;

    for (Count = 3; Count >= 2; Count--)
    {
        DeviceExtension->FlipBufVa = MmAllocateContiguousMemorySpecifyCache(
            (SIZE_T)BufSize * Count, Low, High, Boundary, MmWriteCombined);
        if (DeviceExtension->FlipBufVa != NULL)
            break;
    }

    if (DeviceExtension->FlipBufVa == NULL)
    {
        DbgPrint("RPI5VC4: flip ring alloc failed — presents stay on the "
                 "direct blit path\n");
        DeviceExtension->FlipPresentBroken = TRUE;
        return FALSE;
    }

    DeviceExtension->FlipBufPhys =
        MmGetPhysicalAddress(DeviceExtension->FlipBufVa);

    /* Atomic no-wait flips are a single PTR1 write: every buffer must share
     * the high address byte with the ring base. */
    if ((ULONGLONG)(DeviceExtension->FlipBufPhys.QuadPart) >> 32 !=
        (ULONGLONG)(DeviceExtension->FlipBufPhys.QuadPart +
                    (LONGLONG)BufSize * Count - 1) >> 32)
    {
        MmFreeContiguousMemorySpecifyCache(DeviceExtension->FlipBufVa,
                                           (SIZE_T)BufSize * Count,
                                           MmWriteCombined);
        DeviceExtension->FlipBufVa = NULL;
        DeviceExtension->FlipPresentBroken = TRUE;
        return FALSE;
    }

#if defined(_M_ARM64)
    /* Clean+invalidate the cacheable kernel-linear alias once (same hazard
     * as the VRAM slab: stale dirty lines writing back over scanned pixels). */
    {
        SIZE_T Offset;

        for (Offset = 0; Offset < (SIZE_T)BufSize * Count; Offset += PAGE_SIZE)
        {
            PHYSICAL_ADDRESS PagePhys;
            PUCHAR AliasVa;
            SIZE_T Line;

            PagePhys.QuadPart = DeviceExtension->FlipBufPhys.QuadPart + Offset;
            AliasVa = MmGetVirtualForPhysical(PagePhys);
            if (AliasVa == NULL)
                continue;

            for (Line = 0; Line < PAGE_SIZE; Line += 64)
            {
                __asm__ __volatile__("dc civac, %0"
                                     :: "r"(AliasVa + Line) : "memory");
            }
        }
        __asm__ __volatile__("dsb sy" ::: "memory");
    }
#endif

    RtlZeroMemory(DeviceExtension->FlipBufVa, (SIZE_T)BufSize * Count);
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif

    DeviceExtension->FlipBufSize = BufSize;
    DeviceExtension->FlipBufCount = Count;
    DeviceExtension->FlipBufIndex = Count - 1; /* first present flips to 0 */
    DeviceExtension->FlipsSinceVBlank = 0;

    /* Every buffer starts stale: its first present full-blits from pSource. */
    for (i = 0; i < Count; i++)
    {
        DeviceExtension->FlipStale[i].left = 0;
        DeviceExtension->FlipStale[i].top = 0;
        DeviceExtension->FlipStale[i].right = (LONG)DeviceExtension->ScreenWidth;
        DeviceExtension->FlipStale[i].bottom = (LONG)DeviceExtension->ScreenHeight;
    }

    DbgPrint("RPI5VC4: flip ring: %lu x %lu KB at %02lx:%08lx\n",
             Count, BufSize / 1024,
             (ULONG)((DeviceExtension->FlipBufPhys.QuadPart >> 32) & 0xff),
             (ULONG)(DeviceExtension->FlipBufPhys.QuadPart & 0xffffffff));
    return TRUE;
}

VOID
Rpi5Vc4FreeFlipRing(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->FlipBufVa != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(
            DeviceExtension->FlipBufVa,
            (SIZE_T)DeviceExtension->FlipBufSize * DeviceExtension->FlipBufCount,
            MmWriteCombined);
        DeviceExtension->FlipBufVa = NULL;
        DeviceExtension->FlipBufCount = 0;
        DeviceExtension->FlipBufSize = 0;
    }
}

static BOOLEAN
Rpi5Vc4FlipPresent(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly)
{
    ULONG Count = DeviceExtension->FlipBufCount;
    ULONG Back = (DeviceExtension->FlipBufIndex + 1) % Count;
    PUCHAR BackVa = (PUCHAR)DeviceExtension->FlipBufVa +
                    (SIZE_T)Back * DeviceExtension->FlipBufSize;
    PHYSICAL_ADDRESS BackPhys;
    RECT Union = {0, 0, 0, 0};
    RECT Stale;
    ULONG i;

    /* Small updates blit straight into the live buffer: a flip pays the
     * catch-up amplification (~3x the WC writes) to avoid a tear no one can
     * see on a small rect. Only large updates (drags, window paints, video)
     * take the tear-free flip. The live buffer is by definition current, so
     * only the other ring buffers get stale-marked. */
    {
        ULONGLONG Area = 0, ScreenArea;

        for (i = 0; i < PresentDisplayOnly->NumMoves; i++)
        {
            const RECT *r = &PresentDisplayOnly->pMoves[i].DestRect;
            Area += (ULONGLONG)(r->right - r->left) * (r->bottom - r->top);
        }
        for (i = 0; i < PresentDisplayOnly->NumDirtyRects; i++)
        {
            const RECT *r = &PresentDisplayOnly->pDirtyRect[i];
            Area += (ULONGLONG)(r->right - r->left) * (r->bottom - r->top);
        }

        ScreenArea = (ULONGLONG)DeviceExtension->ScreenWidth *
                     DeviceExtension->ScreenHeight;

        if (Area != 0 && ScreenArea != 0 && Area * 8 < ScreenArea)
        {
            for (i = 0; i < PresentDisplayOnly->NumMoves; i++)
            {
                Rpi5Vc4BlitRect(DeviceExtension,
                                PresentDisplayOnly->pSource,
                                PresentDisplayOnly->Pitch,
                                &PresentDisplayOnly->pMoves[i].DestRect);
                Rpi5Vc4RectUnion(&Union, &PresentDisplayOnly->pMoves[i].DestRect);
            }
            for (i = 0; i < PresentDisplayOnly->NumDirtyRects; i++)
            {
                Rpi5Vc4BlitRect(DeviceExtension,
                                PresentDisplayOnly->pSource,
                                PresentDisplayOnly->Pitch,
                                &PresentDisplayOnly->pDirtyRect[i]);
                Rpi5Vc4RectUnion(&Union, &PresentDisplayOnly->pDirtyRect[i]);
            }

#if defined(_M_ARM64)
            __dsb(_ARM64_BARRIER_SY);
#endif
            KeMemoryBarrier();

            for (i = 0; i < Count; i++)
            {
                ULONGLONG BufPhys = (ULONGLONG)DeviceExtension->FlipBufPhys.QuadPart +
                                    (ULONGLONG)i * DeviceExtension->FlipBufSize;
                if (BufPhys != (ULONGLONG)DeviceExtension->FrameBufferPhysical.QuadPart)
                    Rpi5Vc4RectUnion(&DeviceExtension->FlipStale[i], &Union);
            }
            return TRUE;
        }
    }

    /* The current scanout must be one of our ring buffers or the takeover
     * frame — FlipScanoutEx validates the head against FrameBufferPhysical
     * anyway, so just track vblank latches here. */
    if (Rpi5CrtcVBlankSeen(DeviceExtension))
        DeviceExtension->FlipsSinceVBlank = 0;

    /* With a ring of N, up to N-2 pending flips keep the back buffer
     * off-screen. A burst beyond that waits one vblank to resync (rare). */
    if (DeviceExtension->FlipsSinceVBlank + 2 > Count)
    {
        Rpi5CrtcWaitForVBlank(DeviceExtension);
        DeviceExtension->FlipsSinceVBlank = 0;
    }

    /* Catch this buffer up (it missed the frames flipped since it was last
     * on screen), then apply this present's rects. */
    Stale = DeviceExtension->FlipStale[Back];
    if (Stale.right > Stale.left && Stale.bottom > Stale.top)
    {
        Rpi5Vc4BlitRectTo(DeviceExtension, BackVa,
                          PresentDisplayOnly->pSource,
                          PresentDisplayOnly->Pitch,
                          &Stale);
    }

    for (i = 0; i < PresentDisplayOnly->NumMoves; i++)
    {
        Rpi5Vc4BlitRectTo(DeviceExtension, BackVa,
                          PresentDisplayOnly->pSource,
                          PresentDisplayOnly->Pitch,
                          &PresentDisplayOnly->pMoves[i].DestRect);
        Rpi5Vc4RectUnion(&Union, &PresentDisplayOnly->pMoves[i].DestRect);
    }

    for (i = 0; i < PresentDisplayOnly->NumDirtyRects; i++)
    {
        Rpi5Vc4BlitRectTo(DeviceExtension, BackVa,
                          PresentDisplayOnly->pSource,
                          PresentDisplayOnly->Pitch,
                          &PresentDisplayOnly->pDirtyRect[i]);
        Rpi5Vc4RectUnion(&Union, &PresentDisplayOnly->pDirtyRect[i]);
    }

    /* Drain the write-combine buffers before the HVS can latch the flip. */
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    BackPhys.QuadPart = DeviceExtension->FlipBufPhys.QuadPart +
                        (LONGLONG)Back * DeviceExtension->FlipBufSize;
    if (!Rpi5HvsFlipScanoutEx(DeviceExtension, BackPhys, FALSE))
    {
        /* Screen still shows the old buffer, which missed this present:
         * over-mark everything and let the caller run the classic path. */
        for (i = 0; i < Count; i++)
            Rpi5Vc4RectUnion(&DeviceExtension->FlipStale[i], &Union);
        return FALSE;
    }

    DeviceExtension->FlipBufIndex = Back;
    DeviceExtension->FlipsSinceVBlank++;

    DeviceExtension->FlipStale[Back].left = 0;
    DeviceExtension->FlipStale[Back].top = 0;
    DeviceExtension->FlipStale[Back].right = 0;
    DeviceExtension->FlipStale[Back].bottom = 0;
    for (i = 0; i < Count; i++)
    {
        if (i != Back)
            Rpi5Vc4RectUnion(&DeviceExtension->FlipStale[i], &Union);
    }

    return TRUE;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiPresentDisplayOnly(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    ULONG i;

    if (DeviceExtension == NULL || PresentDisplayOnly == NULL)
        return STATUS_INVALID_PARAMETER;

    if (PresentDisplayOnly->VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    if (DeviceExtension->FrameBufferVa == NULL ||
        PresentDisplayOnly->pSource == NULL ||
        PresentDisplayOnly->BytesPerPixel != 4 ||
        PresentDisplayOnly->Pitch <= 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Wayland-style: compose off-screen and page-flip — tear-free by
     * construction. Classic beam-raced blit below is the fallback. */
    if (Rpi5Vc4EnsureFlipRing(DeviceExtension) &&
        Rpi5Vc4FlipPresent(DeviceExtension, PresentDisplayOnly))
    {
        return STATUS_SUCCESS;
    }

    /* If a flip ring exists, the live buffer may have missed flipped frames:
     * catch it up before applying this present. */
    if (DeviceExtension->FlipBufVa != NULL &&
        DeviceExtension->FlipBufCount != 0)
    {
        ULONG Live = DeviceExtension->FlipBufIndex;
        ULONGLONG LivePhys = (ULONGLONG)DeviceExtension->FlipBufPhys.QuadPart +
                             (ULONGLONG)Live * DeviceExtension->FlipBufSize;
        RECT Stale = DeviceExtension->FlipStale[Live];

        if (LivePhys == (ULONGLONG)DeviceExtension->FrameBufferPhysical.QuadPart &&
            Stale.right > Stale.left && Stale.bottom > Stale.top)
        {
            Rpi5Vc4BlitRect(DeviceExtension,
                            PresentDisplayOnly->pSource,
                            PresentDisplayOnly->Pitch,
                            &Stale);
            DeviceExtension->FlipStale[Live].left = 0;
            DeviceExtension->FlipStale[Live].top = 0;
            DeviceExtension->FlipStale[Live].right = 0;
            DeviceExtension->FlipStale[Live].bottom = 0;
        }
    }

    {
        ULONGLONG Area = 0, ScreenArea;

        for (i = 0; i < PresentDisplayOnly->NumMoves; i++)
        {
            const RECT *r = &PresentDisplayOnly->pMoves[i].DestRect;
            Area += (ULONGLONG)(r->right - r->left) * (r->bottom - r->top);
        }
        for (i = 0; i < PresentDisplayOnly->NumDirtyRects; i++)
        {
            const RECT *r = &PresentDisplayOnly->pDirtyRect[i];
            Area += (ULONGLONG)(r->right - r->left) * (r->bottom - r->top);
        }

        ScreenArea = (ULONGLONG)DeviceExtension->ScreenWidth *
                     DeviceExtension->ScreenHeight;

        /* Race the beam: large updates start at VFP so the top-down copy
         * (2-4x raster speed) stays ahead of the scanout — no visible tear
         * on drags. Small updates skip the wait. */
        if (ScreenArea != 0 && Area * 4 >= ScreenArea)
            Rpi5CrtcWaitForVBlank(DeviceExtension);
    }

    for (i = 0; i < PresentDisplayOnly->NumMoves; i++)
    {
        Rpi5Vc4MoveRect(DeviceExtension,
                        PresentDisplayOnly->pSource,
                        PresentDisplayOnly->Pitch,
                        &PresentDisplayOnly->pMoves[i]);
    }

    for (i = 0; i < PresentDisplayOnly->NumDirtyRects; i++)
    {
        Rpi5Vc4BlitRect(DeviceExtension,
                        PresentDisplayOnly->pSource,
                        PresentDisplayOnly->Pitch,
                        &PresentDisplayOnly->pDirtyRect[i]);
    }

    /*
     * Drain the write-combine buffers so the (non-coherent) HVS picks the
     * new pixels up on its next display-list walk.
     */
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    return STATUS_SUCCESS;
}

/* ========================================================================
 * POST framebuffer release + bugcheck-time system display
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiStopDeviceAndReleasePostDisplayOwnership(
    _In_  PVOID MiniportDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    if (DeviceExtension == NULL || DisplayInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(DisplayInfo, sizeof(*DisplayInfo));
    DisplayInfo->Width = DeviceExtension->ScreenWidth;
    DisplayInfo->Height = DeviceExtension->ScreenHeight;
    DisplayInfo->Pitch = DeviceExtension->BytesPerScanLine;
    DisplayInfo->ColorFormat = DeviceExtension->ColorFormat;
    DisplayInfo->PhysicAddress = DeviceExtension->FrameBufferPhysical;
    DisplayInfo->TargetId = TargetId;

    return Rpi5Vc4DdiStopDevice(MiniportDeviceContext);
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiSystemDisplayEnable(
    _In_  PVOID MiniportDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ PUINT Width,
    _Out_ PUINT Height,
    _Out_ D3DDDIFORMAT *ColorFormat)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);

    if (DeviceExtension == NULL ||
        Width == NULL || Height == NULL || ColorFormat == NULL ||
        DeviceExtension->FrameBufferVa == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * The kernel's bugcheck writer (Inbv) draws into the firmware
     * framebuffer: re-point the scanout at it and drop the cursor
     * overlay so the panic screen is what the HVS shows.
     */
    DeviceExtension->CursorVisible = FALSE;
    Rpi5HvsFlipScanout(DeviceExtension,
                       DeviceExtension->FirmwareFrameBufferPhysical);
    Rpi5HvsInstallScanout(DeviceExtension);

    *Width = DeviceExtension->ScreenWidth;
    *Height = DeviceExtension->ScreenHeight;
    *ColorFormat = DeviceExtension->ColorFormat;
    return STATUS_SUCCESS;
}

VOID
APIENTRY
Rpi5Vc4DdiSystemDisplayWrite(
    _In_ PVOID MiniportDeviceContext,
    _In_ PVOID Source,
    _In_ UINT SourceWidth,
    _In_ UINT SourceHeight,
    _In_ UINT SourceStride,
    _In_ UINT PositionX,
    _In_ UINT PositionY)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    PUCHAR Destination;
    UINT Row;

    if (DeviceExtension == NULL ||
        DeviceExtension->FrameBufferVa == NULL ||
        Source == NULL ||
        PositionX >= DeviceExtension->ScreenWidth ||
        PositionY >= DeviceExtension->ScreenHeight)
    {
        return;
    }

    if (SourceWidth > DeviceExtension->ScreenWidth - PositionX)
        SourceWidth = DeviceExtension->ScreenWidth - PositionX;
    if (SourceHeight > DeviceExtension->ScreenHeight - PositionY)
        SourceHeight = DeviceExtension->ScreenHeight - PositionY;

    Destination = (PUCHAR)Rpi5Vc4CurrentScanoutVa(DeviceExtension) +
                  (SIZE_T)PositionY * DeviceExtension->BytesPerScanLine +
                  (SIZE_T)PositionX * 4;

    for (Row = 0; Row < SourceHeight; Row++)
    {
        RtlCopyMemory(Destination + (SIZE_T)Row * DeviceExtension->BytesPerScanLine,
                      (PUCHAR)Source + (SIZE_T)Row * SourceStride,
                      (SIZE_T)SourceWidth * 4);
    }

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
}
