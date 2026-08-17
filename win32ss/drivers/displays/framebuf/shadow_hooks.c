/*
 * PROJECT:     ReactOS Generic Framebuffer display driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Platform-neutral XPDM shadow-surface hooks
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "framebuf.h"

static SURFOBJ *
IntGetShadowSurface(
   SURFOBJ *pso)
{
   PPDEV ppdev;

   if (pso != NULL && pso->dhpdev != NULL &&
       pso->dhsurf == (DHSURF)pso->dhpdev)
   {
      ppdev = (PPDEV)pso->dhpdev;
      if (ppdev->ShadowActive && ppdev->psoShadow != NULL)
         return ppdev->psoShadow;
   }

   return pso;
}

BOOL APIENTRY
DrvBitBlt(
   IN SURFOBJ *psoTrg,
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

   Result = EngBitBlt(IntGetShadowSurface(psoTrg),
                      IntGetShadowSurface(psoSrc),
                      IntGetShadowSurface(psoMask), pco, pxlo, prclTrg,
                      pptlSrc, pptlMask, pbo, pptlBrush, rop4);
   if (Result)
      IntPublishShadowSurface(psoTrg, prclTrg, pco,
                              FramebufShadowBitBlt);

   return Result;
}

BOOL APIENTRY
DrvCopyBits(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDest,
   IN POINTL *pptlSrc)
{
   BOOL Result;

   Result = EngCopyBits(IntGetShadowSurface(psoDest),
                        IntGetShadowSurface(psoSrc), pco, pxlo,
                        prclDest, pptlSrc);
   if (Result)
      IntPublishShadowSurface(psoDest, prclDest, pco,
                              FramebufShadowCopyBits);

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
   RECTL Bounds;
   BOOL Result;

   Result = EngLineTo(IntGetShadowSurface(pso), pco, pbo,
                      x1, y1, x2, y2, prclBounds, mix);
   if (!Result)
      return FALSE;

   if (prclBounds != NULL)
   {
      Bounds = *prclBounds;
   }
   else
   {
      Bounds.left = min(x1, x2);
      Bounds.top = min(y1, y2);
      Bounds.right = max(x1, x2);
      Bounds.bottom = max(y1, y2);
      if (Bounds.right < MAXLONG)
         Bounds.right++;
      if (Bounds.bottom < MAXLONG)
         Bounds.bottom++;
   }
   IntPublishShadowSurface(pso, &Bounds, pco, FramebufShadowLineTo);
   return TRUE;
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

   Result = EngPaint(IntGetShadowSurface(pso), pco, pbo,
                     pptlBrushOrg, mix);
   if (Result)
   {
      RECTL Bounds;

      if (pco != NULL)
      {
         IntPublishShadowSurface(pso, &pco->rclBounds, pco,
                                 FramebufShadowPaint);
      }
      else
      {
         Bounds.left = 0;
         Bounds.top = 0;
         Bounds.right = pso->sizlBitmap.cx;
         Bounds.bottom = pso->sizlBitmap.cy;
         IntPublishShadowSurface(pso, &Bounds, NULL,
                                 FramebufShadowPaint);
      }
   }
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

   Result = EngStretchBlt(IntGetShadowSurface(psoDest),
                          IntGetShadowSurface(psoSrc),
                          IntGetShadowSurface(psoMask), pco, pxlo, pca,
                          pptlHTOrg, prclDest, prclSrc, pptlMask, iMode);
   if (Result)
      IntPublishShadowSurface(psoDest, prclDest, pco,
                              FramebufShadowStretchBlt);
   return Result;
}

BOOL APIENTRY
DrvStretchBltROP(
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
   IN ULONG iMode,
   IN BRUSHOBJ *pbo,
   IN DWORD rop4)
{
   BOOL Result;

   Result = EngStretchBltROP(IntGetShadowSurface(psoDest),
                             IntGetShadowSurface(psoSrc),
                             IntGetShadowSurface(psoMask), pco, pxlo, pca,
                             pptlHTOrg, prclDest, prclSrc, pptlMask,
                             iMode, pbo, rop4);
   if (Result)
      IntPublishShadowSurface(psoDest, prclDest, pco,
                              FramebufShadowStretchBltRop);
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

   Result = EngAlphaBlend(IntGetShadowSurface(psoDest),
                          IntGetShadowSurface(psoSrc), pco, pxlo,
                          prclDest, prclSrc, pBlendObj);
   if (Result)
      IntPublishShadowSurface(psoDest, prclDest, pco,
                              FramebufShadowAlphaBlend);
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

   Result = EngTransparentBlt(IntGetShadowSurface(psoDst),
                              IntGetShadowSurface(psoSrc), pco, pxlo,
                              prclDst, prclSrc, iTransColor, ulReserved);
   if (Result)
      IntPublishShadowSurface(psoDst, prclDst, pco,
                              FramebufShadowTransparentBlt);
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

   Result = EngGradientFill(IntGetShadowSurface(psoDest), pco, pxlo,
                            pVertex, nVertex,
                            pMesh, nMesh, prclExtents,
                            pptlDitherOrg, ulMode);
   if (Result)
      IntPublishShadowSurface(psoDest, prclExtents, pco,
                              FramebufShadowGradientFill);
   return Result;
}

VOID APIENTRY
DrvSynchronizeSurface(
   IN SURFOBJ *pso,
   IN RECTL *prcl,
   IN FLONG fl)
{
   IntSynchronizeShadowSurface(pso, prcl, fl);
}
