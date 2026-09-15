/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS Win32k subsystem
 * PURPOSE:          Layered window support
 * FILE:             win32ss/user/ntuser/layered.c
 * PROGRAMER:
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserMisc);


typedef struct _LRD_PROP
{ 
   COLORREF Key;
   BYTE     Alpha;
   BYTE     is_Layered;
   BYTE     PixelAlpha;
   BYTE     UpdateLayered;
   DWORD    Flags;
} LRD_PROP, *PLRD_PROP;

static PLRD_PROP
IntGetLayeredProp(PWND pWnd, BOOL Create)
{
   PLRD_PROP pLrdProp = UserGetProp(pWnd, AtomLayer, TRUE);

   if (pLrdProp || !Create)
      return pLrdProp;
   pLrdProp = ExAllocatePoolWithTag(PagedPool, sizeof(LRD_PROP), USERTAG_REDIRECT);
   if (!pLrdProp)
      return NULL;
   RtlZeroMemory(pLrdProp, sizeof(*pLrdProp));
   if (!UserSetProp(pWnd,
                    AtomLayer,
                    (HANDLE)pLrdProp,
                    TRUE))
   {
      ExFreePoolWithTag(pLrdProp, USERTAG_REDIRECT);
      return NULL;
   }
   return pLrdProp;
}

BOOL FASTCALL
GetLayeredStatus(PWND pWnd)
{
   PLRD_PROP pLrdProp = UserGetProp(pWnd, AtomLayer, TRUE);
   if (pLrdProp)
   {
      return pLrdProp->is_Layered;
   }
   return FALSE;
}

BOOL FASTCALL
SetLayeredStatus(PWND pWnd, BYTE set)
{
   PLRD_PROP pLrdProp = IntGetLayeredProp(pWnd, FALSE);
   if (pLrdProp)
   {
      pLrdProp->is_Layered = set;
      if (!set)
      {
         pLrdProp->PixelAlpha = FALSE;
         pLrdProp->UpdateLayered = FALSE;
         pLrdProp->Flags = 0;
      }
      return TRUE;
   }
   return FALSE;
}

/* For the dwm compositor: a layered window's constant alpha / colorkey / flags
 * (LWA_ALPHA / LWA_COLORKEY). Returns FALSE for a non-layered window. */
BOOL FASTCALL
IntCompositionGetLayered(PWND pWnd, BYTE *pAlpha, COLORREF *pKey, DWORD *pFlags)
{
   PLRD_PROP p;
   if (!(pWnd->ExStyle & WS_EX_LAYERED))
      return FALSE;
   p = IntGetLayeredProp(pWnd, FALSE);
   if (!p || (!p->is_Layered && !p->UpdateLayered))
      return FALSE;
   *pAlpha = p->Alpha;
   *pKey = p->Key;
   *pFlags = p->Flags;
   if (p->PixelAlpha)
      *pFlags |= DWM_WINDOW_PREMULTIPLIED_ALPHA;
   return TRUE;
}

BOOL FASTCALL
IntSetLayeredWindowAttributes(PWND pWnd,
                              COLORREF crKey,
                              BYTE bAlpha,
                              DWORD dwFlags)
{
   PLRD_PROP pLrdProp;
   INT was_Layered;
   COLORREF oldKey;
   BYTE oldAlpha;
   BYTE oldPixelAlpha;
   BYTE oldUpdateLayered;
   DWORD oldFlags;

   if (!(pWnd->ExStyle & WS_EX_LAYERED) )
   {
      ERR("Not a Layered Window!\n");
      return FALSE;
   }

   pLrdProp = IntGetLayeredProp(pWnd, TRUE);
   if (!pLrdProp)
   {
      ERR("failed to allocate LRD_PROP\n");
      return FALSE;
   }

   if (pLrdProp)
   {
      was_Layered = pLrdProp->is_Layered;
      oldKey = pLrdProp->Key;
      oldAlpha = pLrdProp->Alpha;
      oldPixelAlpha = pLrdProp->PixelAlpha;
      oldUpdateLayered = pLrdProp->UpdateLayered;
      oldFlags = pLrdProp->Flags;

      pLrdProp->Key = crKey;

      if (dwFlags & LWA_ALPHA)
      {
         pLrdProp->Alpha = bAlpha;
      }
      else
      {
         if (!pLrdProp->is_Layered) pLrdProp->Alpha = 0;
      }

      pLrdProp->Flags = dwFlags;
      pLrdProp->PixelAlpha = FALSE;
      pLrdProp->UpdateLayered = FALSE;
      pLrdProp->is_Layered = 1;
  
      if (!was_Layered)
         co_UserRedrawWindow(pWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME );

      if (!was_Layered || oldKey != pLrdProp->Key ||
          oldAlpha != pLrdProp->Alpha || oldPixelAlpha != pLrdProp->PixelAlpha ||
          oldUpdateLayered != pLrdProp->UpdateLayered || oldFlags != pLrdProp->Flags)
      {
         IntCompositionDamageWindowMetadata(pWnd);
      }
   }
   // FIXME: Now set some bits to the Window DC!!!!
   return TRUE;
}

static BOOL
IntSetUpdateLayeredAttributes(PWND pWnd, const UPDATELAYEREDWINDOWINFO *info)
{
   PLRD_PROP pLrdProp = IntGetLayeredProp(pWnd, TRUE);
   DWORD Flags = info->dwFlags & (ULW_COLORKEY | ULW_ALPHA);
   BYTE Alpha = (Flags & ULW_ALPHA) ? info->pblend->SourceConstantAlpha : 255;
   BYTE PixelAlpha = (Flags & ULW_ALPHA) && (info->pblend->AlphaFormat & AC_SRC_ALPHA);
   BOOL Changed;

   if (!pLrdProp)
      return FALSE;
   Changed = !pLrdProp->UpdateLayered || pLrdProp->Key != info->crKey ||
             pLrdProp->Alpha != Alpha || pLrdProp->PixelAlpha != PixelAlpha ||
             pLrdProp->Flags != Flags;
   pLrdProp->Key = info->crKey;
   pLrdProp->Alpha = Alpha;
   pLrdProp->PixelAlpha = PixelAlpha;
   pLrdProp->UpdateLayered = TRUE;
   pLrdProp->Flags = Flags;
   if (Changed)
      IntCompositionDamageWindowMetadata(pWnd);
   return TRUE;
}

BOOL FASTCALL
IntUpdateLayeredWindowI( PWND pWnd,
                         UPDATELAYEREDWINDOWINFO *info )
{
   PWND Parent;
   RECT Window, Client;
   SIZE Offset;
   BOOL ret = FALSE;
   DWORD flags = SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW;

   Window = pWnd->rcWindow;
   Client = pWnd->rcClient;

   Parent = pWnd->spwndParent;
   if (pWnd->style & WS_CHILD && Parent)
   {
      RECTL_vOffsetRect(&Window, - Parent->rcClient.left, - Parent->rcClient.top);
      RECTL_vOffsetRect(&Client, - Parent->rcClient.left, - Parent->rcClient.top);
   }

   if (info->pptDst)
   {
      Offset.cx = info->pptDst->x - Window.left;
      Offset.cy = info->pptDst->y - Window.top;
      RECTL_vOffsetRect( &Client, Offset.cx, Offset.cy );
      RECTL_vOffsetRect( &Window, Offset.cx, Offset.cy );
      flags &= ~SWP_NOMOVE;
   }
   if (info->psize)
   {
      Offset.cx = info->psize->cx - (Window.right  - Window.left);
      Offset.cy = info->psize->cy - (Window.bottom - Window.top);
      if (info->psize->cx <= 0 || info->psize->cy <= 0)
      {
          ERR("Update Layered Invalid Parameters\n");
          EngSetLastError( ERROR_INVALID_PARAMETER );
          return FALSE;
      }
      if ((info->dwFlags & ULW_EX_NORESIZE) && (Offset.cx || Offset.cy))
      {
          ERR("Wrong Size\n");
          EngSetLastError( ERROR_INCORRECT_SIZE );
          return FALSE;
      }
      Client.right  += Offset.cx;
      Client.bottom += Offset.cy;
      Window.right  += Offset.cx;
      Window.bottom += Offset.cy;
      flags &= ~SWP_NOSIZE;
   }

   if (info->hdcSrc)
   {
      HDC hdc, hdcBuffer, hdcBlend;
      RECT Rect;
      BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, 0 };
      COLORREF color_key = (info->dwFlags & ULW_COLORKEY) ? info->crKey : CLR_INVALID;
      HBITMAP hOldBitmap, hbmSrc;
      DIBSECTION dibs;
      BOOL bComposited, bDirect;
      LONG SrcX, SrcY;

      Rect = Window;

      RECTL_vOffsetRect( &Rect, -Window.left, -Window.top );

      TRACE("H %d W %d\n",Rect.bottom - Rect.top,Rect.right - Rect.left);

      if (!info->hdcDst) hdc = UserGetDCEx(pWnd, NULL, DCX_USESTYLE);
      else hdc = info->hdcDst;

      bComposited = IntCompositionIsEnabled();
      bDirect = bComposited ||
                ((color_key == CLR_INVALID) && (info->prcDirty == NULL));
      hbmSrc = NULL;
      hdcBuffer = NULL;
      hOldBitmap = NULL;
      hdcBlend = info->hdcSrc;

      if (!bDirect)
      {
         hbmSrc = NtGdiCreateCompatibleBitmap(info->hdcSrc, Rect.right - Rect.left, Rect.bottom - Rect.top);

         GreGetObject(hbmSrc, sizeof(DIBSECTION), &dibs);

         TRACE("Source Bitmap bc %d\n",dibs.dsBmih.biBitCount);

         hdcBuffer = NtGdiCreateCompatibleDC(hdc);

         hOldBitmap = (HBITMAP)NtGdiSelectBitmap(hdcBuffer, hbmSrc);
         hdcBlend = hdcBuffer;

         NtGdiStretchBlt(hdcBuffer,
                         Rect.left,
                         Rect.top,
                         Rect.right - Rect.left,
                         Rect.bottom - Rect.top,
                         info->hdcSrc,
                         Rect.left + (info->pptSrc ? info->pptSrc->x : 0),
                         Rect.top  + (info->pptSrc ? info->pptSrc->y : 0),
                         Rect.right - Rect.left,
                         Rect.bottom - Rect.top,
                         SRCCOPY,
                         color_key);
      }

      if (info->prcDirty)
      {
         ERR("prcDirty\n");
         RECTL_bIntersectRect( &Rect, &Rect, info->prcDirty );
         if (!bComposited)
            NtGdiPatBlt(hdc,
                        Rect.left,
                        Rect.top,
                        Rect.right - Rect.left,
                        Rect.bottom - Rect.top,
                        BLACKNESS);
      }

      SrcX = Rect.left + ((bDirect && info->pptSrc) ? info->pptSrc->x : 0);
      SrcY = Rect.top + ((bDirect && info->pptSrc) ? info->pptSrc->y : 0);

      if (info->dwFlags & ULW_ALPHA)
      {
         blend = *info->pblend;
         TRACE("ULW_ALPHA bop %d Alpha %d aF %d\n", blend.BlendOp, blend.SourceConstantAlpha, blend.AlphaFormat);
      }

      if (bComposited)
      {
         ret = NtGdiStretchBlt(hdc,
                               Rect.left,
                               Rect.top,
                               Rect.right - Rect.left,
                               Rect.bottom - Rect.top,
                               hdcBlend,
                               SrcX,
                               SrcY,
                               Rect.right - Rect.left,
                               Rect.bottom - Rect.top,
                               SRCCOPY,
                               0);
      }
      else
      {
         ret = NtGdiAlphaBlend(hdc,
                               Rect.left,
                               Rect.top,
                               Rect.right - Rect.left,
                               Rect.bottom - Rect.top,
                               hdcBlend,
                               SrcX,
                               SrcY,
                               Rect.right - Rect.left,
                               Rect.bottom - Rect.top,
                               blend,
                               0);
      }

      if (hdcBuffer) NtGdiSelectBitmap(hdcBuffer, hOldBitmap);
      if (hbmSrc) GreDeleteObject(hbmSrc);
      if (hdcBuffer) IntGdiDeleteDC(hdcBuffer, FALSE);
      if (!info->hdcDst) UserReleaseDC(pWnd, hdc, FALSE);
      if (ret && !IntSetUpdateLayeredAttributes(pWnd, info))
      {
         EngSetLastError(ERROR_NOT_ENOUGH_MEMORY);
         ret = FALSE;
      }
   }
   else
      ret = TRUE;

   co_WinPosSetWindowPos(pWnd, 0, Window.left, Window.top, Window.right - Window.left, Window.bottom - Window.top, flags);
   return ret;
}


/*
 * @implemented
 */
BOOL
APIENTRY
NtUserGetLayeredWindowAttributes(
    HWND hwnd,
    COLORREF *pcrKey,
    BYTE *pbAlpha,
    DWORD *pdwFlags)
{
   PLRD_PROP pLrdProp;
   PWND pWnd;
   BOOL Ret = FALSE;

   TRACE("Enter NtUserGetLayeredWindowAttributes\n");
   UserEnterShared();

   if (!(pWnd = UserGetWindowObject(hwnd)) ||
       !(pWnd->ExStyle & WS_EX_LAYERED) )
   {
      ERR("Not a Layered Window!\n");
      goto Exit;
   }

   pLrdProp = UserGetProp(pWnd, AtomLayer, TRUE);

   if (!pLrdProp)
   {
      TRACE("No Prop!\n");
      goto Exit;
   }

   if (pLrdProp->is_Layered == 0)
   {
      goto Exit;
   }

   _SEH2_TRY
   {
      if (pcrKey)
      {
          ProbeForWrite(pcrKey, sizeof(*pcrKey), 1);
          *pcrKey = pLrdProp->Key;
      }
      if (pbAlpha)
      {
          ProbeForWrite(pbAlpha, sizeof(*pbAlpha), 1);
          *pbAlpha = pLrdProp->Alpha;
      }
      if (pdwFlags)
      {
          ProbeForWrite(pdwFlags, sizeof(*pdwFlags), 1);
          *pdwFlags = pLrdProp->Flags;
      }
   }
   _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
   {
      SetLastNtError(_SEH2_GetExceptionCode());
      _SEH2_YIELD(goto Exit);
   }
   _SEH2_END;

   Ret = TRUE;

Exit:
   TRACE("Leave NtUserGetLayeredWindowAttributes, ret=%i\n", Ret);
   UserLeave();
   return Ret;
}

/*
 * @implemented
 */
BOOL APIENTRY
NtUserSetLayeredWindowAttributes(HWND hwnd,
			   COLORREF crKey,
			   BYTE bAlpha,
			   DWORD dwFlags)
{
   PWND pWnd;
   BOOL Ret = FALSE;

   TRACE("Enter NtUserSetLayeredWindowAttributes\n");
   UserEnterExclusive();

   if (!(pWnd = UserGetWindowObject(hwnd)) ||
       !(pWnd->ExStyle & WS_EX_LAYERED) )
   {
      ERR("Not a Layered Window!\n");
      goto Exit;
   }

   Ret = IntSetLayeredWindowAttributes(pWnd, crKey, bAlpha, dwFlags);
Exit:
   TRACE("Leave NtUserSetLayeredWindowAttributes, ret=%i\n", Ret);
   UserLeave();
   return Ret;
}

/*
 * @implemented
 */
BOOL
APIENTRY
NtUserUpdateLayeredWindow(
   HWND hwnd,
   HDC hdcDst,
   POINT *pptDst,
   SIZE *psize,
   HDC hdcSrc,
   POINT *pptSrc,
   COLORREF crKey,
   BLENDFUNCTION *pblend,
   DWORD dwFlags,
   RECT *prcDirty)
{
   UPDATELAYEREDWINDOWINFO info;
   POINT Dst, Src; 
   SIZE Size;
   RECT Dirty;
   BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, 0 };
   PWND pWnd;
   BOOL Ret = FALSE;

   TRACE("Enter NtUserUpdateLayeredWindow\n");
   UserEnterExclusive();

   if (!(pWnd = UserGetWindowObject(hwnd)))
   {
      goto Exit;
   }

   _SEH2_TRY
   {
      if (pptDst)
      {
         ProbeForRead(pptDst, sizeof(POINT), 1);
         Dst = *pptDst;
      }
      if (pptSrc)
      {
         ProbeForRead(pptSrc, sizeof(POINT), 1);
         Src = *pptSrc;
      }
      ProbeForRead(psize, sizeof(SIZE), 1);
      Size = *psize;
      if (pblend)
      {
         ProbeForRead(pblend, sizeof(BLENDFUNCTION), 1);
         blend = *pblend;
      }
      if (prcDirty)
      {
         ProbeForRead(prcDirty, sizeof(RECT), 1);
         Dirty = *prcDirty;
      }
   }
   _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
   {
      EngSetLastError( ERROR_INVALID_PARAMETER );
      _SEH2_YIELD(goto Exit);
   }
   _SEH2_END;

   if ( GetLayeredStatus(pWnd) ||
        dwFlags & ~(ULW_COLORKEY | ULW_ALPHA | ULW_OPAQUE | ULW_EX_NORESIZE) ||
       !(pWnd->ExStyle & WS_EX_LAYERED) )
   {
      ERR("Layered Window Invalid Parameters\n");
      EngSetLastError( ERROR_INVALID_PARAMETER );
      goto Exit;
   }

   info.cbSize   = sizeof(info);
   info.hdcDst   = hdcDst;
   info.pptDst   = pptDst? &Dst : NULL;
   info.psize    = &Size;
   info.hdcSrc   = hdcSrc;
   info.pptSrc   = pptSrc ? &Src : NULL;
   info.crKey    = crKey;
   info.pblend   = &blend;
   info.dwFlags  = dwFlags;
   info.prcDirty = prcDirty ? &Dirty : NULL;
   Ret = IntUpdateLayeredWindowI( pWnd, &info );
Exit:
   TRACE("Leave NtUserUpdateLayeredWindow, ret=%i\n", Ret);
   UserLeave();
   return Ret;
}

/* EOF */
