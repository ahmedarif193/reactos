/*
 * PROJECT:     ReactOS Canonical Display Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM redirection bitmap allocation and lifetime
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "cdd.h"

static VOID
RcddDestroyRedirectionAllocation(
   IN PRCDD_PDEV ppdev,
   IN CONST DXGK_REDIRECTION_SURFACE_CREATE *Create)
{
   DXGK_REDIRECTION_SURFACE_DESTROY Destroy;
   ULONG BytesReturned;

   if (Create->AllocationHandle == 0 || Create->ResourceHandle == 0 ||
       Create->GlobalShare == 0)
   {
      return;
   }

   RtlZeroMemory(&Destroy, sizeof(Destroy));
   Destroy.StructSize = sizeof(Destroy);
   Destroy.AllocationHandle = Create->AllocationHandle;
   Destroy.ResourceHandle = Create->ResourceHandle;
   Destroy.GlobalShare = Create->GlobalShare;
   (VOID)EngDeviceIoControl(ppdev->hDriver,
                            IOCTL_VIDEO_DXGK_DESTROY_REDIRECTION_SURFACE,
                            &Destroy, sizeof(Destroy),
                            NULL, 0, &BytesReturned);
}

HBITMAP APIENTRY
RcddCreateDeviceBitmapEx(
   IN DHPDEV dhpdev,
   IN SIZEL sizl,
   IN ULONG iFormat,
   IN DWORD Flags,
   IN DHSURF dhsurfGroup,
   IN DWORD DxFormat,
#if (NTDDI_VERSION >= NTDDI_WIN8)
   IN DWORD SubresourceIndex,
#endif
   OUT HANDLE *phSharedSurface)
{
   PRCDD_PDEV ppdev = (PRCDD_PDEV)dhpdev;
   PRCDD_BITMAP Bitmap;
   DXGK_REDIRECTION_SURFACE_CREATE Create;
   DXGK_REDIRECTION_SURFACE_ASSOCIATE Associate;
   HBITMAP hBitmap;
   ULONG BytesReturned, ControlStatus;

#if (NTDDI_VERSION >= NTDDI_WIN8)
   if (SubresourceIndex != 0)
      return NULL;
#endif
   if (ppdev == NULL || phSharedSurface == NULL ||
       sizl.cx <= 0 || sizl.cy <= 0 || iFormat != BMF_32BPP ||
       Flags != CDBEX_REDIRECTION || dhsurfGroup != NULL ||
       DxFormat != DWM_DX_FORMAT_B8G8R8A8_UNORM)
   {
      return NULL;
   }

   *phSharedSurface = NULL;
   Bitmap = EngAllocMem(FL_ZERO_MEMORY, sizeof(*Bitmap), ALLOC_TAG);
   if (Bitmap == NULL)
      return NULL;

   RtlZeroMemory(&Create, sizeof(Create));
   Create.StructSize = sizeof(Create);
   Create.Width = (ULONG)sizl.cx;
   Create.Height = (ULONG)sizl.cy;
   Create.Format = DxFormat;
   ControlStatus = EngDeviceIoControl(ppdev->hDriver,
                                      IOCTL_VIDEO_DXGK_CREATE_REDIRECTION_SURFACE,
                                      &Create, sizeof(Create),
                                      &Create, sizeof(Create),
                                      &BytesReturned);
   if (ControlStatus != 0)
   {
      EngFreeMem(Bitmap);
      return NULL;
   }
   if (BytesReturned < sizeof(Create) || Create.Pitch == 0 ||
       Create.AllocationBytes < (ULONGLONG)Create.Pitch * Create.Height ||
       Create.AllocationHandle == 0 || Create.ResourceHandle == 0 ||
       Create.GlobalShare == 0 || Create.CpuAddress == 0)
   {
      RcddDestroyRedirectionAllocation(ppdev, &Create);
      EngFreeMem(Bitmap);
      return NULL;
   }

   Bitmap->Pdev = ppdev;
   InitializeListHead(&Bitmap->ListEntry);
   Bitmap->AllocationHandle = Create.AllocationHandle;
   Bitmap->ResourceHandle = Create.ResourceHandle;
   Bitmap->GlobalShare = Create.GlobalShare;
   Bitmap->Pitch = Create.Pitch;
   Bitmap->Width = Create.Width;
   Bitmap->Height = Create.Height;
   RtlZeroMemory((PVOID)(ULONG_PTR)Create.CpuAddress,
                 (SIZE_T)Create.Pitch * Create.Height);

   hBitmap = EngCreateRedirectionDeviceBitmap((DHSURF)Bitmap, sizl, iFormat);
   if (hBitmap != NULL &&
       EngModifySurface((HSURF)hBitmap, ppdev->hDevEng, 0, MS_SHAREDACCESS,
                        (DHSURF)Bitmap, (PVOID)(ULONG_PTR)Create.CpuAddress,
                        (LONG)Create.Pitch, NULL))
   {
      RtlZeroMemory(&Associate, sizeof(Associate));
      Associate.StructSize = sizeof(Associate);
      Associate.AllocationHandle = Create.AllocationHandle;
      Associate.ResourceHandle = Create.ResourceHandle;
      Associate.GlobalShare = Create.GlobalShare;
      Associate.SurfaceHandle = (ULONGLONG)(ULONG_PTR)hBitmap;
      ControlStatus = EngDeviceIoControl(
                         ppdev->hDriver,
                         IOCTL_VIDEO_DXGK_ASSOCIATE_REDIRECTION_SURFACE,
                         &Associate, sizeof(Associate),
                         NULL, 0, &BytesReturned);
      if (ControlStatus == 0)
      {
         EngAcquireSemaphore(ppdev->RedirectionLock);
         InsertTailList(&ppdev->RedirectionBitmapList, &Bitmap->ListEntry);
         ppdev->RedirectionBitmapCount++;
         EngReleaseSemaphore(ppdev->RedirectionLock);
         *phSharedSurface = (HANDLE)(ULONG_PTR)Create.GlobalShare;
         return hBitmap;
      }
   }

   if (hBitmap != NULL)
   {
      /* The engine owns dhsurf after creation and invokes
       * DrvDeleteDeviceBitmapEx while deleting the surface. */
      EngDeleteSurface((HSURF)hBitmap);
   }
   else
   {
      RcddDestroyRedirectionAllocation(ppdev, &Create);
      EngFreeMem(Bitmap);
   }
   return NULL;
}

VOID APIENTRY
RcddDeleteDeviceBitmapEx(
   IN OUT DHSURF dhsurf)
{
   PRCDD_BITMAP Bitmap = (PRCDD_BITMAP)dhsurf;
   DXGK_REDIRECTION_SURFACE_DESTROY Destroy;
   ULONG BytesReturned;

   if (Bitmap == NULL)
      return;

   EngAcquireSemaphore(Bitmap->Pdev->RedirectionLock);
   if (!IsListEmpty(&Bitmap->ListEntry))
   {
      RemoveEntryList(&Bitmap->ListEntry);
      InitializeListHead(&Bitmap->ListEntry);
      ASSERT(Bitmap->Pdev->RedirectionBitmapCount != 0);
      if (Bitmap->Pdev->RedirectionBitmapCount != 0)
         Bitmap->Pdev->RedirectionBitmapCount--;
   }
   EngReleaseSemaphore(Bitmap->Pdev->RedirectionLock);

   RtlZeroMemory(&Destroy, sizeof(Destroy));
   Destroy.StructSize = sizeof(Destroy);
   Destroy.AllocationHandle = Bitmap->AllocationHandle;
   Destroy.ResourceHandle = Bitmap->ResourceHandle;
   Destroy.GlobalShare = Bitmap->GlobalShare;
   (VOID)EngDeviceIoControl(Bitmap->Pdev->hDriver,
                            IOCTL_VIDEO_DXGK_DESTROY_REDIRECTION_SURFACE,
                            &Destroy, sizeof(Destroy),
                            NULL, 0, &BytesReturned);
   EngFreeMem(Bitmap);
}
