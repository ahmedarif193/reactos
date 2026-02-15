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

#ifndef EXPERIMENTAL_MOUSE_CURSOR_SUPPORT

/*
 * DrvSetPointerShape
 *
 * Sets the new pointer shape.
 *
 * Status
 *    @implemented
 */

ULONG APIENTRY
DrvSetPointerShape(
   IN SURFOBJ *pso,
   IN SURFOBJ *psoMask,
   IN SURFOBJ *psoColor,
   IN XLATEOBJ *pxlo,
   IN LONG xHot,
   IN LONG yHot,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl,
   IN FLONG fl)
{
   PPDEV ppdev = pso ? (PPDEV)pso->dhpdev : NULL;

   /* Track cursor dimensions and hotspot for shadow flush in DrvMovePointer */
   if (ppdev && psoMask)
   {
      ppdev->CursorWidth = psoMask->sizlBitmap.cx;
      /* AND mask + XOR mask stacked vertically, so height is half */
      ppdev->CursorHeight = psoMask->sizlBitmap.cy / 2;
   }
   else if (ppdev && psoColor)
   {
      ppdev->CursorWidth = psoColor->sizlBitmap.cx;
      ppdev->CursorHeight = psoColor->sizlBitmap.cy;
   }

   if (ppdev)
   {
      ppdev->CursorHotX = xHot;
      ppdev->CursorHotY = yHot;
   }

   {
      /*
       * Pass the original device surface to EngSetPointerShape.  The engine
       * needs hdev (device association) from the surface to find the PDEVOBJ.
       * Cursor drawing inside the engine uses IntEngBitBlt, which dispatches
       * to our DrvBitBlt where shadow substitution and VRAM flush happen.
       */
      ULONG ret = EngSetPointerShape(pso, psoMask, psoColor, pxlo, xHot, yHot, x, y, prcl, fl);

      /* Flush the cursor area so the new shape is visible on VRAM */
      if (ppdev && ppdev->UsingShadow)
      {
         if (prcl)
         {
            FbShadowFlushRect(ppdev, prcl);
            ppdev->OldCursorRect = *prcl;
         }
         else if (x >= 0)
         {
            LONG cw = ppdev->CursorWidth > 0 ? ppdev->CursorWidth : 32;
            LONG ch = ppdev->CursorHeight > 0 ? ppdev->CursorHeight : 32;
            LONG hotX = ppdev->CursorHotX;
            LONG hotY = ppdev->CursorHotY;
            RECTL cursorRect;
            cursorRect.left = x - hotX;
            cursorRect.top = y - hotY;
            cursorRect.right = cursorRect.left + cw;
            cursorRect.bottom = cursorRect.top + ch;
            FbShadowFlushRect(ppdev, &cursorRect);
            ppdev->OldCursorRect = cursorRect;
         }
      }

      return ret;
   }
}

/*
 * DrvMovePointer
 *
 * Moves the pointer to a new position and ensures that GDI does not interfere
 * with the display of the pointer.
 *
 * Status
 *    @implemented
 */

VOID APIENTRY
DrvMovePointer(
   IN SURFOBJ *pso,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl)
{
   PPDEV ppdev = pso ? (PPDEV)pso->dhpdev : NULL;
   RECTL oldRect;
   RECTL newRect;
   BOOLEAN haveOld = FALSE;
   BOOLEAN haveNew = FALSE;
   BOOLEAN sameRect = FALSE;

   if (ppdev && ppdev->UsingShadow)
   {
      oldRect = ppdev->OldCursorRect;
      haveOld = (oldRect.left < oldRect.right) && (oldRect.top < oldRect.bottom);
   }

   /*
    * Pass the original device surface to EngMovePointer.  The engine needs
    * hdev from the surface.  Cursor drawing dispatches through DrvBitBlt
    * where shadow substitution and VRAM flush happen automatically.
    */
   EngMovePointer(pso, x, y, prcl);

   /* Update tracked position and flush areas after shadow update */
   if (ppdev && ppdev->UsingShadow)
   {
      if (x == -1)
      {
         if (haveOld)
            FbShadowFlushRect(ppdev, &oldRect);

         /* Cursor hidden; clear the rect so next move doesn't flush stale area */
         ppdev->OldCursorRect.left = 0;
         ppdev->OldCursorRect.top = 0;
         ppdev->OldCursorRect.right = 0;
         ppdev->OldCursorRect.bottom = 0;
      }
      else
      {
         if (prcl)
         {
            newRect = *prcl;
         }
         else
         {
            LONG cw = ppdev->CursorWidth > 0 ? ppdev->CursorWidth : 32;
            LONG ch = ppdev->CursorHeight > 0 ? ppdev->CursorHeight : 32;
            LONG hotX = ppdev->CursorHotX;
            LONG hotY = ppdev->CursorHotY;
            newRect.left = x - hotX;
            newRect.top = y - hotY;
            newRect.right = newRect.left + cw;
            newRect.bottom = newRect.top + ch;
         }

         haveNew = (newRect.left < newRect.right) && (newRect.top < newRect.bottom);

         if (haveOld && haveNew)
         {
            sameRect = (oldRect.left == newRect.left) &&
                       (oldRect.top == newRect.top) &&
                       (oldRect.right == newRect.right) &&
                       (oldRect.bottom == newRect.bottom);
         }

         if (haveOld || haveNew)
         {
            const RECTL *oldToFlush = NULL;
            const RECTL *newToFlush = NULL;

            if (haveOld)
               oldToFlush = &oldRect;
            if (haveNew && !sameRect)
               newToFlush = &newRect;

            FbShadowFlushRects(ppdev, oldToFlush, newToFlush);
         }

         if (haveNew)
            ppdev->OldCursorRect = newRect;
         else
         {
            ppdev->OldCursorRect.left = 0;
            ppdev->OldCursorRect.top = 0;
            ppdev->OldCursorRect.right = 0;
            ppdev->OldCursorRect.bottom = 0;
         }
      }
   }
}

#else

VOID FASTCALL
IntHideMousePointer(PPDEV ppdev, SURFOBJ *DestSurface)
{
   if (ppdev->PointerAttributes.Enable == FALSE)
   {
      return;
   }

   ppdev->PointerAttributes.Enable = FALSE;
   if (ppdev->PointerSaveSurface != NULL)
   {
      RECTL DestRect;
      POINTL SrcPoint;
      SURFOBJ *SaveSurface;
      SURFOBJ *MaskSurface;

      DestRect.left = max(ppdev->PointerAttributes.Column, 0);
      DestRect.top = max(ppdev->PointerAttributes.Row, 0);
      DestRect.right = min(
         ppdev->PointerAttributes.Column + ppdev->PointerAttributes.Width,
         ppdev->ScreenWidth - 1);
      DestRect.bottom = min(
         ppdev->PointerAttributes.Row + ppdev->PointerAttributes.Height,
         ppdev->ScreenHeight - 1);

      SrcPoint.x = max(-ppdev->PointerAttributes.Column, 0);
      SrcPoint.y = max(-ppdev->PointerAttributes.Row, 0);

      SaveSurface = EngLockSurface(ppdev->PointerSaveSurface);
      MaskSurface = EngLockSurface(ppdev->PointerMaskSurface);
      EngBitBlt(DestSurface, SaveSurface, MaskSurface, NULL, NULL,
                &DestRect, &SrcPoint, &SrcPoint, NULL, NULL, SRCCOPY);
      EngUnlockSurface(MaskSurface);
      EngUnlockSurface(SaveSurface);
   }
}

VOID FASTCALL
IntShowMousePointer(PPDEV ppdev, SURFOBJ *DestSurface)
{
   if (ppdev->PointerAttributes.Enable)
   {
      return;
   }

   ppdev->PointerAttributes.Enable = TRUE;

   /*
    * Copy the pixels under the cursor to temporary surface.
    */

   if (ppdev->PointerSaveSurface != NULL)
   {
      RECTL DestRect;
      POINTL SrcPoint;
      SURFOBJ *SaveSurface;

      SrcPoint.x = max(ppdev->PointerAttributes.Column, 0);
      SrcPoint.y = max(ppdev->PointerAttributes.Row, 0);

      DestRect.left = SrcPoint.x - ppdev->PointerAttributes.Column;
      DestRect.top = SrcPoint.y - ppdev->PointerAttributes.Row;
      DestRect.right = min(
         ppdev->PointerAttributes.Width,
         ppdev->ScreenWidth - ppdev->PointerAttributes.Column - 1);
      DestRect.bottom = min(
         ppdev->PointerAttributes.Height,
         ppdev->ScreenHeight - ppdev->PointerAttributes.Row - 1);

      SaveSurface = EngLockSurface(ppdev->PointerSaveSurface);
      EngBitBlt(SaveSurface, DestSurface, NULL, NULL, NULL,
                &DestRect, &SrcPoint, NULL, NULL, NULL, SRCCOPY);
      EngUnlockSurface(SaveSurface);
   }

   /*
    * Blit the cursor on the screen.
    */

   {
      RECTL DestRect;
      POINTL SrcPoint;
      SURFOBJ *ColorSurf;
      SURFOBJ *MaskSurf;

      DestRect.left = max(ppdev->PointerAttributes.Column, 0);
      DestRect.top = max(ppdev->PointerAttributes.Row, 0);
      DestRect.right = min(
         ppdev->PointerAttributes.Column + ppdev->PointerAttributes.Width,
         ppdev->ScreenWidth - 1);
      DestRect.bottom = min(
         ppdev->PointerAttributes.Row + ppdev->PointerAttributes.Height,
         ppdev->ScreenHeight - 1);

      SrcPoint.x = max(-ppdev->PointerAttributes.Column, 0);
      SrcPoint.y = max(-ppdev->PointerAttributes.Row, 0);

      MaskSurf = EngLockSurface(ppdev->PointerMaskSurface);
      if (ppdev->PointerColorSurface != NULL)
      {
         ColorSurf = EngLockSurface(ppdev->PointerColorSurface);
         EngBitBlt(DestSurface, ColorSurf, MaskSurf, NULL, ppdev->PointerXlateObject,
                   &DestRect, &SrcPoint, &SrcPoint, NULL, NULL, 0xAACC);
         EngUnlockSurface(ColorSurf);
      }
      else
      {
         /* FIXME */
         EngBitBlt(DestSurface, MaskSurf, NULL, NULL, ppdev->PointerXlateObject,
                   &DestRect, &SrcPoint, NULL, NULL, NULL, SRCAND);
         SrcPoint.y += ppdev->PointerAttributes.Height;
         EngBitBlt(DestSurface, MaskSurf, NULL, NULL, ppdev->PointerXlateObject,
                   &DestRect, &SrcPoint, NULL, NULL, NULL, SRCINVERT);
      }
      EngUnlockSurface(MaskSurf);
   }
}

/*
 * DrvSetPointerShape
 *
 * Sets the new pointer shape.
 *
 * Status
 *    @implemented
 */

ULONG APIENTRY
DrvSetPointerShape(
   IN SURFOBJ *pso,
   IN SURFOBJ *psoMask,
   IN SURFOBJ *psoColor,
   IN XLATEOBJ *pxlo,
   IN LONG xHot,
   IN LONG yHot,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl,
   IN FLONG fl)
{
   PPDEV ppdev = (PPDEV)pso->dhpdev;
   SURFOBJ *TempSurfObj;

   IntHideMousePointer(ppdev, pso);

   if (ppdev->PointerColorSurface != NULL)
   {
      /* FIXME: Is this really needed? */
      TempSurfObj = EngLockSurface(ppdev->PointerColorSurface);
      EngFreeMem(TempSurfObj->pvBits);
      TempSurfObj->pvBits = NULL;
      EngUnlockSurface(TempSurfObj);

      EngDeleteSurface(ppdev->PointerColorSurface);
      ppdev->PointerColorSurface = NULL;
   }

   if (ppdev->PointerMaskSurface != NULL)
   {
      /* FIXME: Is this really needed? */
      TempSurfObj = EngLockSurface(ppdev->PointerMaskSurface);
      EngFreeMem(TempSurfObj->pvBits);
      TempSurfObj->pvBits = NULL;
      EngUnlockSurface(TempSurfObj);

      EngDeleteSurface(ppdev->PointerMaskSurface);
      ppdev->PointerMaskSurface = NULL;
   }

   if (ppdev->PointerSaveSurface != NULL)
   {
      EngDeleteSurface(ppdev->PointerSaveSurface);
      ppdev->PointerSaveSurface = NULL;
   }

   /*
    * See if we are being asked to hide the pointer.
    */

   if (psoMask == NULL)
   {
      return SPS_ACCEPT_EXCLUDE;
   }

   ppdev->PointerHotSpot.x = xHot;
   ppdev->PointerHotSpot.y = yHot;

   ppdev->PointerXlateObject = pxlo;
   ppdev->PointerAttributes.Column = x - xHot;
   ppdev->PointerAttributes.Row = y - yHot;
   ppdev->PointerAttributes.Width = psoMask->lDelta << 3;
   ppdev->PointerAttributes.Height = (psoMask->cjBits / psoMask->lDelta) >> 1;

   if (psoColor != NULL)
   {
      SIZEL Size;
      PBYTE Bits;

      Size.cx = ppdev->PointerAttributes.Width;
      Size.cy = ppdev->PointerAttributes.Height;
      Bits = EngAllocMem(0, psoColor->cjBits, ALLOC_TAG);
      memcpy(Bits, psoColor->pvBits, psoColor->cjBits);

      ppdev->PointerColorSurface = (HSURF)EngCreateBitmap(Size,
         psoColor->lDelta, psoColor->iBitmapFormat, 0, Bits);
   }

   {
      SIZEL Size;
      PBYTE Bits;

      Size.cx = ppdev->PointerAttributes.Width;
      Size.cy = ppdev->PointerAttributes.Height << 1;
      Bits = EngAllocMem(0, psoMask->cjBits, ALLOC_TAG);
      memcpy(Bits, psoMask->pvBits, psoMask->cjBits);

      ppdev->PointerMaskSurface = (HSURF)EngCreateBitmap(Size,
         psoMask->lDelta, psoMask->iBitmapFormat, 0, Bits);
   }

   /*
    * Create surface for saving the pixels under the cursor.
    */

   {
      SIZEL Size;
      LONG lDelta;

      Size.cx = ppdev->PointerAttributes.Width;
      Size.cy = ppdev->PointerAttributes.Height;

      switch (pso->iBitmapFormat)
      {
         case BMF_8BPP: lDelta = Size.cx; break;
         case BMF_16BPP: lDelta = Size.cx << 1; break;
         case BMF_24BPP: lDelta = Size.cx * 3; break;
         case BMF_32BPP: lDelta = Size.cx << 2; break;
      }

      ppdev->PointerSaveSurface = (HSURF)EngCreateBitmap(
         Size, lDelta, pso->iBitmapFormat, BMF_NOZEROINIT, NULL);
   }

   IntShowMousePointer(ppdev, pso);

   return SPS_ACCEPT_EXCLUDE;
}

/*
 * DrvMovePointer
 *
 * Moves the pointer to a new position and ensures that GDI does not interfere
 * with the display of the pointer.
 *
 * Status
 *    @implemented
 */

VOID APIENTRY
DrvMovePointer(
   IN SURFOBJ *pso,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl)
{
   PPDEV ppdev = (PPDEV)pso->dhpdev;
   BOOL WasVisible;

   WasVisible = ppdev->PointerAttributes.Enable;
   if (WasVisible)
   {
      IntHideMousePointer(ppdev, pso);
   }

   if (x == -1)
   {
      return;
   }

   ppdev->PointerAttributes.Column = x - ppdev->PointerHotSpot.x;
   ppdev->PointerAttributes.Row = y - ppdev->PointerHotSpot.y;

   if (WasVisible)
   {
      IntShowMousePointer(ppdev, pso);
   }
}

#endif
