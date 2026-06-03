/*
 * ReactOS Generic Framebuffer display driver
 *
 * Copyright (C) 2004 Filip Navara
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "framebuf.h"

#define FRAMEBUF_ENUM_RECT_LIMIT 32
#define ROP4_NOOP 0x0000AAAA

/*
 * Some 32bpp firmware framebuffers expose a reserved byte that display hardware
 * can treat as alpha. GDI writes only the RGB bytes, so hook drawing entry points
 * for such modes, punt the work to the engine, then make the reserved byte
 * opaque over the touched region. The same hook also drains ARM64 write-combined
 * framebuffer stores so the display sees the final pixels before the system goes
 * idle.
 */
#define FRAMEBUF_RESERVED_SURFACE_HOOKS \
   (HOOK_BITBLT | HOOK_STRETCHBLT | HOOK_TEXTOUT | HOOK_PAINT | \
    HOOK_STROKEPATH | HOOK_FILLPATH | HOOK_STROKEANDFILLPATH | \
    HOOK_LINETO | HOOK_COPYBITS | HOOK_TRANSPARENTBLT | \
    HOOK_ALPHABLEND | HOOK_GRADIENTFILL)

typedef struct _RECT_ENUM
{
   ULONG c;
   RECTL arcl[FRAMEBUF_ENUM_RECT_LIMIT];
} RECT_ENUM;

static VOID
FrameBufferMakeWellOrderedRect(
   IN OUT RECTL *Rect)
{
   LONG Tmp;

   if (Rect->left > Rect->right)
   {
      Tmp = Rect->left;
      Rect->left = Rect->right;
      Rect->right = Tmp;
   }

   if (Rect->top > Rect->bottom)
   {
      Tmp = Rect->top;
      Rect->top = Rect->bottom;
      Rect->bottom = Tmp;
   }
}

static BOOL
FrameBufferIntersectRect(
   OUT RECTL *Dest,
   IN const RECTL *Rect1,
   IN const RECTL *Rect2)
{
   Dest->left = max(Rect1->left, Rect2->left);
   Dest->top = max(Rect1->top, Rect2->top);
   Dest->right = min(Rect1->right, Rect2->right);
   Dest->bottom = min(Rect1->bottom, Rect2->bottom);

   return (Dest->left < Dest->right) && (Dest->top < Dest->bottom);
}

static VOID
FrameBufferUnionRect(
   OUT RECTL *Dest,
   IN const RECTL *Rect1,
   IN const RECTL *Rect2)
{
   Dest->left = min(Rect1->left, Rect2->left);
   Dest->top = min(Rect1->top, Rect2->top);
   Dest->right = max(Rect1->right, Rect2->right);
   Dest->bottom = max(Rect1->bottom, Rect2->bottom);
}

static PPDEV
FrameBufferGetTargetPdev(
   IN SURFOBJ *Surface)
{
   PPDEV ppdev;

   if (Surface == NULL || Surface->dhsurf == NULL)
      return NULL;

   ppdev = (PPDEV)Surface->dhsurf;
   if (ppdev->Signature != FRAMEBUF_PDEV_SIGNATURE ||
       ppdev->hSurfEng != Surface->hsurf)
   {
      return NULL;
   }

   return ppdev;
}

static VOID
FrameBufferDrainStores(VOID)
{
#if defined(_M_ARM64)
   __dsb(_ARM64_BARRIER_SY);
#endif
}

static VOID
FrameBufferSetReservedBitsRect(
   IN PPDEV ppdev,
   IN const RECTL *Rect)
{
   RECTL ClippedRect;
   LONG X;
   LONG Y;
   PBYTE Line;
   LONG ReservedByte;

   if (ppdev->ReservedMask == 0 || ppdev->BitsPerPixel != 32)
      return;

   /*
    * The reserved/alpha channel of a firmware GOP mode occupies one whole
    * byte (e.g. 0xFF000000 for BGRX/RGBX). Store that byte directly rather
    * than OR-ing the whole pixel: the framebuffer is mapped write-combining
    * (uncached), so a read-modify-write would issue an uncached read per
    * pixel and stall every paint for a noticeable time - producing a slow
    * top-to-bottom "fill" with a black trailing band. A plain byte store has
    * no read-back and the write-combining buffer coalesces the writes.
    */
   switch (ppdev->ReservedMask)
   {
      case 0xFF000000: ReservedByte = 3; break;
      case 0x00FF0000: ReservedByte = 2; break;
      case 0x0000FF00: ReservedByte = 1; break;
      case 0x000000FF: ReservedByte = 0; break;
      default:         ReservedByte = -1; break;
   }

   ClippedRect = *Rect;
   FrameBufferMakeWellOrderedRect(&ClippedRect);

   if (ClippedRect.left < 0)
      ClippedRect.left = 0;
   if (ClippedRect.top < 0)
      ClippedRect.top = 0;
   if (ClippedRect.right > (LONG)ppdev->ScreenWidth)
      ClippedRect.right = ppdev->ScreenWidth;
   if (ClippedRect.bottom > (LONG)ppdev->ScreenHeight)
      ClippedRect.bottom = ppdev->ScreenHeight;

   if (ClippedRect.left >= ClippedRect.right ||
       ClippedRect.top >= ClippedRect.bottom)
   {
      return;
   }

   for (Y = ClippedRect.top; Y < ClippedRect.bottom; Y++)
   {
      Line = (PBYTE)ppdev->ScreenPtr + Y * ppdev->ScreenDelta;

      if (ReservedByte >= 0)
      {
         PBYTE Pixel = Line + ClippedRect.left * 4 + ReservedByte;

         for (X = ClippedRect.left; X < ClippedRect.right; X++)
         {
            *Pixel = 0xFF;
            Pixel += 4;
         }
      }
      else
      {
         PDWORD Pixel = (PDWORD)Line + ClippedRect.left;

         for (X = ClippedRect.left; X < ClippedRect.right; X++)
            *Pixel++ |= ppdev->ReservedMask;
      }
   }
}

static VOID
FrameBufferSetReservedBits(
   IN PPDEV ppdev,
   IN CLIPOBJ *Clip,
   IN RECTL *Rect)
{
   RECT_ENUM RectEnum;
   RECTL DrawRect;
   BOOL EnumMore;
   ULONG i;

   if (ppdev->ReservedMask == 0 || ppdev->BitsPerPixel != 32)
      return;

   if (Rect == NULL)
      return;

   if (Clip == NULL || Clip->iDComplexity == DC_TRIVIAL)
   {
      FrameBufferSetReservedBitsRect(ppdev, Rect);
      return;
   }

   if (Clip->iDComplexity == DC_RECT)
   {
      if (FrameBufferIntersectRect(&DrawRect, Rect, &Clip->rclBounds))
         FrameBufferSetReservedBitsRect(ppdev, &DrawRect);
      return;
   }

   CLIPOBJ_cEnumStart(Clip, FALSE, CT_RECTANGLES, CD_ANY, 0);
   do
   {
      EnumMore = CLIPOBJ_bEnum(Clip, sizeof(RectEnum), (PVOID)&RectEnum);
      for (i = 0; i < RectEnum.c; i++)
      {
         if (FrameBufferIntersectRect(&DrawRect, Rect, &RectEnum.arcl[i]))
            FrameBufferSetReservedBitsRect(ppdev, &DrawRect);
      }
   } while (EnumMore);
}

/*
 * Force the reserved/alpha channel opaque over the region a drawing operation
 * just touched on the device surface. When the caller knows the painted
 * rectangle it passes it as Bounds; otherwise (path/paint style operations)
 * Bounds is NULL and the extent is taken from the clip, falling back to the
 * whole surface only when the operation was unclipped.
 */
static VOID
FrameBufferMarkOpaque(
   IN SURFOBJ *Surface,
   IN CLIPOBJ *Clip,
   IN RECTL *Bounds OPTIONAL)
{
   PPDEV ppdev;
   RECTL Derived;

   ppdev = FrameBufferGetTargetPdev(Surface);
   if (ppdev == NULL)
      return;

   if (ppdev->ReservedMask == 0 || ppdev->BitsPerPixel != 32)
      return;

   if (Bounds == NULL)
   {
      if (Clip != NULL && Clip->iDComplexity != DC_TRIVIAL)
      {
         Derived = Clip->rclBounds;
      }
      else
      {
         Derived.left = 0;
         Derived.top = 0;
         Derived.right = (LONG)ppdev->ScreenWidth;
         Derived.bottom = (LONG)ppdev->ScreenHeight;
      }
      Bounds = &Derived;
   }

   FrameBufferSetReservedBits(ppdev, Clip, Bounds);

   /*
    * The framebuffer is mapped write-combining and the display scanout reads
    * DRAM as a separate, non-coherent observer. Drain the write-combining
    * buffer to the point of coherency so the pixels the engine just wrote (and
    * the reserved-byte writes above) become visible to the scanout now. Without
    * this the final writes before an idle period - typically the topmost text
    * glyphs - can sit in the write buffer and only reach the display when some
    * later drawing (e.g. a mouse move) flushes it, which shows up as text that
    * is missing until the cursor hovers over it.
    */
   FrameBufferDrainStores();
}

static BOOL
FrameBufferLatchScanout(
   IN PPDEV ppdev)
{
   ULONG Returned;

   if (ppdev == NULL || ppdev->hDriver == NULL)
      return FALSE;

   if (ppdev->ScanoutLatchProbed && !ppdev->ScanoutLatchSupported)
      return FALSE;

   FrameBufferDrainStores();

   ppdev->ScanoutLatchProbed = TRUE;
   if (EngDeviceIoControl(ppdev->hDriver,
                          IOCTL_VIDEO_RPI5VC4_LATCH_SCANOUT,
                          NULL,
                          0,
                          NULL,
                          0,
                          &Returned))
   {
      ppdev->ScanoutLatchSupported = FALSE;
      return FALSE;
   }

   ppdev->ScanoutLatchSupported = TRUE;
   return TRUE;
}

BOOL APIENTRY
DrvBitBlt(
   IN OUT SURFOBJ *psoTrg,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclTrg,
   IN POINTL *pptlSrc,
   IN POINTL *pptlMask,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrush,
   IN ROP4 rop4)
{
   BOOL Result;

   Result = EngBitBlt(psoTrg,
                      psoSrc,
                      psoMask,
                      pco,
                      pxlo,
                      prclTrg,
                      pptlSrc,
                      pptlMask,
                      pbo,
                      pptlBrush,
                      rop4);

   if (Result && rop4 != ROP4_NOOP)
      FrameBufferMarkOpaque(psoTrg, pco, prclTrg);

   return Result;
}

BOOL APIENTRY
DrvCopyBits(
   OUT SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDest,
   IN POINTL *pptlSrc)
{
   BOOL Result;

   Result = EngCopyBits(psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc);

   if (Result)
      FrameBufferMarkOpaque(psoDest, pco, prclDest);

   return Result;
}

BOOL APIENTRY
DrvTextOut(
   IN SURFOBJ *pso,
   IN STROBJ *pstro,
   IN FONTOBJ *pfo,
   IN CLIPOBJ *pco,
   IN RECTL *prclExtra,
   IN RECTL *prclOpaque,
   IN BRUSHOBJ *pboFore,
   IN BRUSHOBJ *pboOpaque,
   IN POINTL *pptlOrg,
   IN MIX mix)
{
   BOOL Result;
   RECTL Bounds;

   Result = EngTextOut(pso, pstro, pfo, pco, prclExtra, prclOpaque,
                       pboFore, pboOpaque, pptlOrg, mix);

   if (Result)
   {
      if (pstro != NULL)
      {
         Bounds = pstro->rclBkGround;
         if (prclOpaque != NULL)
            FrameBufferUnionRect(&Bounds, &Bounds, prclOpaque);
         FrameBufferMarkOpaque(pso, pco, &Bounds);
      }
      else if (prclOpaque != NULL)
      {
         FrameBufferMarkOpaque(pso, pco, prclOpaque);
      }
      else if (prclExtra != NULL)
      {
         FrameBufferMarkOpaque(pso, pco, prclExtra);
      }
   }

   return Result;
}

BOOL APIENTRY
DrvLineTo(
   IN SURFOBJ *pso,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN LONG x1,
   IN LONG y1,
   IN LONG x2,
   IN LONG y2,
   IN RECTL *prclBounds,
   IN MIX mix)
{
   BOOL Result;

   Result = EngLineTo(pso, pco, pbo, x1, y1, x2, y2, prclBounds, mix);

   if (Result)
      FrameBufferMarkOpaque(pso, pco, prclBounds);

   return Result;
}

BOOL APIENTRY
DrvStrokePath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN XFORMOBJ *pxo,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN LINEATTRS *plineattrs,
   IN MIX mix)
{
   BOOL Result;

   Result = EngStrokePath(pso, ppo, pco, pxo, pbo, pptlBrushOrg, plineattrs, mix);

   if (Result)
      FrameBufferMarkOpaque(pso, pco, NULL);

   return Result;
}

BOOL APIENTRY
DrvFillPath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN MIX mix,
   IN FLONG flOptions)
{
   BOOL Result;

   Result = EngFillPath(pso, ppo, pco, pbo, pptlBrushOrg, mix, flOptions);

   if (Result)
      FrameBufferMarkOpaque(pso, pco, NULL);

   return Result;
}

BOOL APIENTRY
DrvStrokeAndFillPath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN XFORMOBJ *pxo,
   IN BRUSHOBJ *pboStroke,
   IN LINEATTRS *plineattrs,
   IN BRUSHOBJ *pboFill,
   IN POINTL *pptlBrushOrg,
   IN MIX mixFill,
   IN FLONG flOptions)
{
   BOOL Result;

   Result = EngStrokeAndFillPath(pso, ppo, pco, pxo, pboStroke, plineattrs,
                                 pboFill, pptlBrushOrg, mixFill, flOptions);

   if (Result)
      FrameBufferMarkOpaque(pso, pco, NULL);

   return Result;
}

BOOL APIENTRY
DrvPaint(
   IN SURFOBJ *pso,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN MIX mix)
{
   BOOL Result;

   Result = EngPaint(pso, pco, pbo, pptlBrushOrg, mix);

   if (Result)
      FrameBufferMarkOpaque(pso, pco, NULL);

   return Result;
}

BOOL APIENTRY
DrvStretchBlt(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN COLORADJUSTMENT *pca,
   IN POINTL *pptlHTOrg,
   IN RECTL *prclDest,
   IN RECTL *prclSrc,
   IN POINTL *pptlMask,
   IN ULONG iMode)
{
   BOOL Result;

   Result = EngStretchBlt(psoDest, psoSrc, psoMask, pco, pxlo, pca,
                          pptlHTOrg, prclDest, prclSrc, pptlMask, iMode);

   if (Result)
      FrameBufferMarkOpaque(psoDest, pco, prclDest);

   return Result;
}

BOOL APIENTRY
DrvTransparentBlt(
   IN SURFOBJ *psoDst,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDst,
   IN RECTL *prclSrc,
   IN ULONG iTransColor,
   IN ULONG ulReserved)
{
   BOOL Result;

   Result = EngTransparentBlt(psoDst, psoSrc, pco, pxlo, prclDst, prclSrc,
                              iTransColor, ulReserved);

   if (Result)
      FrameBufferMarkOpaque(psoDst, pco, prclDst);

   return Result;
}

BOOL APIENTRY
DrvAlphaBlend(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDest,
   IN RECTL *prclSrc,
   IN BLENDOBJ *pBlendObj)
{
   BOOL Result;

   Result = EngAlphaBlend(psoDest, psoSrc, pco, pxlo, prclDest, prclSrc, pBlendObj);

   if (Result)
      FrameBufferMarkOpaque(psoDest, pco, prclDest);

   return Result;
}

BOOL APIENTRY
DrvGradientFill(
   IN SURFOBJ *psoDest,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN TRIVERTEX *pVertex,
   IN ULONG nVertex,
   IN PVOID pMesh,
   IN ULONG nMesh,
   IN RECTL *prclExtents,
   IN POINTL *pptlDitherOrg,
   IN ULONG ulMode)
{
   BOOL Result;

   Result = EngGradientFill(psoDest, pco, pxlo, pVertex, nVertex, pMesh, nMesh,
                            prclExtents, pptlDitherOrg, ulMode);

   if (Result)
      FrameBufferMarkOpaque(psoDest, pco, prclExtents);

   return Result;
}

VOID APIENTRY
DrvSynchronize(
   IN DHPDEV dhpdev,
   IN RECTL *prcl)
{
   PPDEV ppdev = (PPDEV)dhpdev;

   UNREFERENCED_PARAMETER(prcl);

   FrameBufferLatchScanout(ppdev);
}

VOID APIENTRY
DrvSynchronizeSurface(
   IN SURFOBJ *pso,
   IN RECTL *prcl,
   IN FLONG fl)
{
   PPDEV ppdev;

   UNREFERENCED_PARAMETER(prcl);
   UNREFERENCED_PARAMETER(fl);

   ppdev = FrameBufferGetTargetPdev(pso);
   if (ppdev != NULL)
      FrameBufferLatchScanout(ppdev);
}

/*
 * DrvEnableSurface
 *
 * Create engine bitmap around frame buffer and set the video mode requested
 * when PDEV was initialized.
 *
 * Status
 *    @implemented
 */

HSURF APIENTRY
DrvEnableSurface(
   IN DHPDEV dhpdev)
{
   PPDEV ppdev = (PPDEV)dhpdev;
   HSURF hSurface;
   ULONG BitmapType;
   SIZEL ScreenSize;
   VIDEO_MEMORY VideoMemory;
   VIDEO_MEMORY_INFORMATION VideoMemoryInfo;
   ULONG ulTemp;

   /*
    * Set video mode of our adapter.
    */

   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                          &(ppdev->ModeIndex), sizeof(ULONG), NULL, 0,
                          &ulTemp))
   {
      return NULL;
   }

   /*
    * Map the framebuffer into our memory.
    */

   VideoMemory.RequestedVirtualAddress = NULL;
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY,
                          &VideoMemory, sizeof(VIDEO_MEMORY),
                          &VideoMemoryInfo, sizeof(VIDEO_MEMORY_INFORMATION),
                          &ulTemp))
   {
      return NULL;
   }

   ppdev->ScreenPtr = VideoMemoryInfo.FrameBufferBase;

   switch (ppdev->BitsPerPixel)
   {
      case 8:
         IntSetPalette(dhpdev, ppdev->PaletteEntries, 0, 256);
         BitmapType = BMF_8BPP;
         break;

      case 16:
         BitmapType = BMF_16BPP;
         break;

      case 24:
         BitmapType = BMF_24BPP;
         break;

      case 32:
         BitmapType = BMF_32BPP;
         break;

      default:
         return NULL;
   }

   ppdev->iDitherFormat = BitmapType;

   ScreenSize.cx = ppdev->ScreenWidth;
   ScreenSize.cy = ppdev->ScreenHeight;

   hSurface = (HSURF)EngCreateBitmap(ScreenSize, ppdev->ScreenDelta, BitmapType,
                                     (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
                                     ppdev->ScreenPtr);
   if (hSurface == NULL)
   {
      return NULL;
   }

   /*
    * Associate the surface with our device.
    */
   if (!EngAssociateSurface(hSurface,
                            ppdev->hDevEng,
                            (ppdev->ReservedMask != 0 && ppdev->BitsPerPixel == 32) ?
                                FRAMEBUF_RESERVED_SURFACE_HOOKS : 0))
   {
      EngDeleteSurface(hSurface);
      return NULL;
   }

   ppdev->hSurfEng = hSurface;

   return hSurface;
}

/*
 * DrvDisableSurface
 *
 * Used by GDI to notify a driver that the surface created by DrvEnableSurface
 * for the current device is no longer needed.
 *
 * Status
 *    @implemented
 */

VOID APIENTRY
DrvDisableSurface(
   IN DHPDEV dhpdev)
{
   DWORD ulTemp;
   VIDEO_MEMORY VideoMemory;
   PPDEV ppdev = (PPDEV)dhpdev;

   EngDeleteSurface(ppdev->hSurfEng);
   ppdev->hSurfEng = NULL;

#ifdef EXPERIMENTAL_MOUSE_CURSOR_SUPPORT
   /* Clear all mouse pointer surfaces. */
   DrvSetPointerShape(NULL, NULL, NULL, NULL, 0, 0, 0, 0, NULL, 0);
#endif

   /*
    * Unmap the framebuffer.
    */

   VideoMemory.RequestedVirtualAddress = ((PPDEV)dhpdev)->ScreenPtr;
   EngDeviceIoControl(((PPDEV)dhpdev)->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                      &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
}

/*
 * DrvAssertMode
 *
 * Sets the mode of the specified physical device to either the mode specified
 * when the PDEV was initialized or to the default mode of the hardware.
 *
 * Status
 *    @implemented
 */

BOOL APIENTRY
DrvAssertMode(
   IN DHPDEV dhpdev,
   IN BOOL bEnable)
{
   PPDEV ppdev = (PPDEV)dhpdev;
   ULONG ulTemp;

   if (bEnable)
   {
      /*
       * Reinitialize the device to a clean state.
       */
      if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                             &(ppdev->ModeIndex), sizeof(ULONG), NULL, 0,
                             &ulTemp))
      {
          /* We failed, bail out */
          return FALSE;
      }
      if (ppdev->BitsPerPixel == 8)
      {
	     IntSetPalette(dhpdev, ppdev->PaletteEntries, 0, 256);
      }

      return TRUE;
   }
   else
   {
      /*
       * Call the miniport driver to reset the device to a known state.
       */
      return !EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_RESET_DEVICE,
                                 NULL, 0, NULL, 0, &ulTemp);
   }
}
