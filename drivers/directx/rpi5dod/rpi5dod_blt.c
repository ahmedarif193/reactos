/*
 * PROJECT:     ReactOS RPi5 Display-Only WDDM Miniport (rpi5dod)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Present blt: copy the composed source frame into the HVS/GOP
 *              scanout framebuffer.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "rpi5dod.h"

/*
 * Rpi5DodBltSourceToScanout
 *
 * Copies the dirty rectangles of a complete 32-bpp source frame into the
 * scanout framebuffer.  The source is dxgkrnl's shadow framebuffer, which
 * already holds a COMPLETE composed frame (DWM composited into it via the
 * kernel present path), so copying whole rects yields a tear-free update.
 *
 * Both surfaces are 32-bpp.  Source pitch is the present's Pitch; the
 * scanout pitch comes from the GOP DispInfo.  All rects are clamped to the
 * scanout bounds.
 *
 * IRQL: PASSIVE_LEVEL (called from DxgkDdiPresentDisplayOnly worker)
 */
VOID
Rpi5DodBltSourceToScanout(
    _In_ PRPI5DOD_MODE              Mode,
    _In_reads_bytes_(SrcPitch * Height) PVOID Source,
    _In_ ULONG                      SrcPitch,
    _In_ ULONG                      NumDirtyRects,
    _In_reads_opt_(NumDirtyRects) CONST RECT *DirtyRects)
{
    PUCHAR DstBase;
    PUCHAR SrcBase;
    ULONG  DstPitch;
    LONG   ScreenW;
    LONG   ScreenH;
    ULONG  i;
    RECT   FullRect;

    if (Mode == NULL || Mode->FrameBufferVa == NULL || Source == NULL)
        return;

    DstBase  = (PUCHAR)Mode->FrameBufferVa;
    SrcBase  = (PUCHAR)Source;
    DstPitch = Mode->DispInfo.Pitch;
    ScreenW  = (LONG)Mode->DispInfo.Width;
    ScreenH  = (LONG)Mode->DispInfo.Height;

    /*
     * Defensive bound: never write past the framebuffer that was actually
     * mapped at StartDevice (FrameBufferLength).  The present-time DispInfo
     * geometry and the mapped length can disagree (e.g. a mode change after
     * the FB was mapped), and this blt is not wrapped in SEH, so an over-write
     * would be an unhandled page fault / bugcheck rather than a dropped frame.
     */
    if (DstPitch != 0 && Mode->FrameBufferLength != 0)
    {
        LONG MaxRows = (LONG)(Mode->FrameBufferLength / DstPitch);
        if (ScreenH > MaxRows)
            ScreenH = MaxRows;
    }
    if (DstPitch != 0 && ScreenW > (LONG)(DstPitch / RPI5DOD_SOURCE_BYTES))
        ScreenW = (LONG)(DstPitch / RPI5DOD_SOURCE_BYTES);

    /* No dirty rects supplied -> treat the whole screen as dirty. */
    if (NumDirtyRects == 0 || DirtyRects == NULL)
    {
        FullRect.left   = 0;
        FullRect.top    = 0;
        FullRect.right  = ScreenW;
        FullRect.bottom = ScreenH;
        DirtyRects      = &FullRect;
        NumDirtyRects   = 1;
    }

    for (i = 0; i < NumDirtyRects; i++)
    {
        RECT  r = DirtyRects[i];
        LONG  y;
        LONG  rowBytes;

        /* Clamp to the scanout surface. */
        if (r.left   < 0)        r.left   = 0;
        if (r.top    < 0)        r.top    = 0;
        if (r.right  > ScreenW)  r.right  = ScreenW;
        if (r.bottom > ScreenH)  r.bottom = ScreenH;
        if (r.right <= r.left || r.bottom <= r.top)
            continue;

        rowBytes = (r.right - r.left) * RPI5DOD_SOURCE_BYTES;

        for (y = r.top; y < r.bottom; y++)
        {
            PUCHAR dst = DstBase + (SIZE_T)y * DstPitch +
                         (SIZE_T)r.left * RPI5DOD_SOURCE_BYTES;
            PUCHAR src = SrcBase + (SIZE_T)y * SrcPitch +
                         (SIZE_T)r.left * RPI5DOD_SOURCE_BYTES;
            RtlCopyMemory(dst, src, (SIZE_T)rowBytes);
        }
    }
}
