/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM compositor and display-miniport control channel.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * A compositor, an OpenGL ICD, and the display driver need private channels
 * that GDI knows nothing about; the public mechanism is DrvEscape / ExtEscape
 * with private escape codes. cdd recognizes the compositor codes in cdd.h and
 * forwards the bounded Raspberry Pi GPU services to the WDDM miniport:
 *
 *   CDD_ESCAPE_SUPPRESS_CURSOR  - LONG in: non-zero = the compositor draws the
 *       cursor, so cdd stops drawing its own cursor; zero = resume. Returns 1.
 *   CDD_ESCAPE_COMPOSITION_SYNC - LONG in: non-zero = begin a composed frame,
 *       zero = end it. Either way cdd presents the current frame and acks so
 *       the compositor can pace itself. Returns 1.
 *
 * Unknown escapes return 0 ("not supported").
 */

#include "cdd.h"
#include <reactos/rpi5vc4_xpdm.h>

#define CDD_OPENGL_ICD_VERSION 1
#define CDD_OPENGL_ICD_DRIVER_VERSION 1
#define OPENGL_GETINFO_DRVNAME 0

typedef struct _CDD_OPENGL_INFO
{
   ULONG Version;
   ULONG DriverVersion;
   WCHAR DriverName[MAX_PATH + 1];
} CDD_OPENGL_INFO, *PCDD_OPENGL_INFO;

static ULONG
RcddGpuEscape(
   IN PRCDD_PDEV ppdev,
   IN ULONG Op,
   IN PVOID pvIn,
   IN ULONG cjIn,
   OUT PVOID pvOut,
   IN ULONG cjOut)
{
   PRPI5VC4_WDDM_GPU_ESCAPE Wrapper;
   ULONG PayloadBytes = max(cjIn, cjOut);
   ULONG WrapperBytes;
   ULONG Returned = 0;
   ULONG CopyOut;

   if (PayloadBytes > ~(ULONG)0 -
                      FIELD_OFFSET(RPI5VC4_WDDM_GPU_ESCAPE, Payload))
      return 0;

   WrapperBytes = FIELD_OFFSET(RPI5VC4_WDDM_GPU_ESCAPE, Payload) + PayloadBytes;
   Wrapper = EngAllocMem(FL_ZERO_MEMORY, WrapperBytes, 'EGdC');
   if (Wrapper == NULL)
      return 0;

   Wrapper->Magic = RPI5VC4_WDDM_GPU_ESCAPE_MAGIC;
   Wrapper->Op = Op;
   Wrapper->InputLength = cjIn;
   Wrapper->OutputLength = cjOut;
   if (cjIn != 0)
      memcpy(Wrapper->Payload, pvIn, cjIn);

   if (EngDeviceIoControl(ppdev->hDriver,
                          IOCTL_VIDEO_DXGK_GPU_ESCAPE,
                          Wrapper,
                          WrapperBytes,
                          Wrapper,
                          WrapperBytes,
                          &Returned) != 0)
   {
      EngFreeMem(Wrapper);
      return 0;
   }

   CopyOut = min(Wrapper->OutputLength, cjOut);
   if (CopyOut != 0 && pvOut != NULL)
      memcpy(pvOut, Wrapper->Payload, CopyOut);

   EngFreeMem(Wrapper);
   return CopyOut;
}

static BOOL
RcddV3dExecutionSupported(
   IN PRCDD_PDEV ppdev)
{
   RPI5VC4_V3D_INFO Info;

   memset(&Info, 0, sizeof(Info));
   if (RcddGpuEscape(ppdev,
                     RPI5VC4_ESCAPE_QUERY_V3D,
                     NULL,
                     0,
                     &Info,
                     sizeof(Info)) < sizeof(Info))
   {
      return FALSE;
   }

   return Info.AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
          Info.Version == 71 &&
          (Info.Flags & RPI5VC4_V3D_FLAG_IDENT_VALID) != 0;
}

ULONG APIENTRY
RcddEscape(
   IN SURFOBJ *pso,
   IN ULONG iEsc,
   IN ULONG cjIn,
   IN PVOID pvIn,
   IN ULONG cjOut,
   OUT PVOID pvOut)
{
   PRCDD_PDEV ppdev;
   LONG value;

   if (pso == NULL || pso->dhpdev == NULL)
      return 0;

   ppdev = (PRCDD_PDEV)pso->dhpdev;

   /*
    * QUERYESCSUPPORT lets a caller probe which escapes we implement; the queried
    * escape code is passed in pvIn.
    */
   if (iEsc == QUERYESCSUPPORT)
   {
      ULONG RequestedEscape;

      if (pvIn == NULL || cjIn < sizeof(ULONG))
         return 0;

      RequestedEscape = *(PULONG)pvIn;
      if (RequestedEscape == CDD_ESCAPE_SUPPRESS_CURSOR ||
          RequestedEscape == CDD_ESCAPE_COMPOSITION_SYNC ||
          RequestedEscape == CDD_ESCAPE_REGISTER_VBLANK ||
          RequestedEscape == CDD_ESCAPE_PRESENT_STATS)
      {
         return 1;
      }

      if (RequestedEscape == RPI5VC4_ESCAPE_RENDER_CLEAR ||
          RequestedEscape == RPI5VC4_ESCAPE_RENDER_TRIANGLE ||
          RequestedEscape == RPI5VC4_ESCAPE_RENDER_BATCH ||
          RequestedEscape == RPI5VC4_ESCAPE_UPLOAD_TEXTURE ||
          RequestedEscape == RPI5VC4_ESCAPE_RENDER_GRAPH ||
          RequestedEscape == RPI5VC4_ESCAPE_READ_GRAPH ||
          RequestedEscape == RPI5VC4_ESCAPE_READ_TEXTURE ||
          RequestedEscape == OPENGL_GETINFO)
      {
         return RcddV3dExecutionSupported(ppdev) ? 1 : 0;
      }

      if (RequestedEscape == RPI5VC4_ESCAPE_QUERY_PLATFORM ||
          RequestedEscape == RPI5VC4_ESCAPE_QUERY_V3D ||
          RequestedEscape == RPI5VC4_ESCAPE_RUN_V3D_SELFTEST ||
          RequestedEscape == RPI5VC4_ESCAPE_WAIT_VBLANK)
      {
         return 1;
      }
      return 0;
   }

   if (iEsc == OPENGL_GETINFO)
   {
      static const WCHAR IcdName[] = L"Rpi5Vc4";
      PCDD_OPENGL_INFO Info;

      if (pvIn == NULL || cjIn < sizeof(ULONG) ||
          pvOut == NULL || cjOut < sizeof(*Info) ||
          *(PULONG)pvIn != OPENGL_GETINFO_DRVNAME ||
          !RcddV3dExecutionSupported(ppdev))
      {
         return 0;
      }

      Info = pvOut;
      memset(Info, 0, sizeof(*Info));
      Info->Version = CDD_OPENGL_ICD_VERSION;
      Info->DriverVersion = CDD_OPENGL_ICD_DRIVER_VERSION;
      memcpy(Info->DriverName, IcdName, sizeof(IcdName));
      return sizeof(*Info);
   }

   if (iEsc == RPI5VC4_ESCAPE_QUERY_PLATFORM &&
       pvOut != NULL && cjOut >= sizeof(RPI5VC4_PLATFORM_INFO))
   {
      return RcddGpuEscape(ppdev, iEsc, NULL, 0, pvOut, cjOut);
   }

   if (iEsc == RPI5VC4_ESCAPE_QUERY_V3D &&
       pvOut != NULL && cjOut >= sizeof(RPI5VC4_V3D_INFO))
   {
      return RcddGpuEscape(ppdev, iEsc, NULL, 0, pvOut, cjOut);
   }

   if (iEsc == RPI5VC4_ESCAPE_RUN_V3D_SELFTEST &&
       pvOut != NULL && cjOut >= sizeof(RPI5VC4_V3D_SELFTEST))
   {
      return RcddGpuEscape(ppdev, iEsc, NULL, 0, pvOut, cjOut);
   }

   if (iEsc == RPI5VC4_ESCAPE_RENDER_CLEAR &&
       pvIn != NULL && cjIn >= sizeof(RPI5VC4_V3D_CLEAR_REQUEST) &&
       pvOut != NULL && cjOut >= FIELD_OFFSET(RPI5VC4_V3D_CLEAR_RESULT, Pixels))
   {
      return RcddGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);
   }

   if (iEsc == RPI5VC4_ESCAPE_RENDER_TRIANGLE &&
       pvIn != NULL && cjIn >= sizeof(RPI5VC4_V3D_TRIANGLE_REQUEST) &&
       pvOut != NULL && cjOut >= FIELD_OFFSET(RPI5VC4_V3D_TRIANGLE_RESULT, Pixels))
   {
      return RcddGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);
   }

   if (iEsc == RPI5VC4_ESCAPE_RENDER_BATCH &&
       pvIn != NULL && cjIn >= FIELD_OFFSET(RPI5VC4_V3D_BATCH_REQUEST, Vertices) &&
       pvOut != NULL && cjOut >= FIELD_OFFSET(RPI5VC4_V3D_BATCH_RESULT, Pixels))
   {
      return RcddGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);
   }

   if (iEsc == RPI5VC4_ESCAPE_UPLOAD_TEXTURE &&
       pvIn != NULL && cjIn >= FIELD_OFFSET(RPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST, Pixels) &&
       pvOut != NULL && cjOut >= sizeof(RPI5VC4_V3D_TEXTURE_UPLOAD_RESULT))
   {
      return RcddGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);
   }

   if (iEsc == RPI5VC4_ESCAPE_RENDER_GRAPH &&
       pvIn != NULL && cjIn >= FIELD_OFFSET(RPI5VC4_V3D_RENDER_GRAPH_REQUEST, Pixels) &&
       pvOut != NULL && cjOut >= sizeof(RPI5VC4_V3D_RENDER_GRAPH_RESULT))
   {
      return RcddGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);
   }

   if (iEsc == RPI5VC4_ESCAPE_READ_GRAPH &&
       pvIn != NULL && cjIn >= sizeof(RPI5VC4_V3D_READ_GRAPH_REQUEST) &&
       pvOut != NULL && cjOut >= FIELD_OFFSET(RPI5VC4_V3D_READ_GRAPH_RESULT, Pixels))
   {
      return RcddGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);
   }

   if (iEsc == RPI5VC4_ESCAPE_READ_TEXTURE &&
       pvIn != NULL && cjIn >= sizeof(RPI5VC4_V3D_READ_TEXTURE_REQUEST) &&
       pvOut != NULL && cjOut >= FIELD_OFFSET(RPI5VC4_V3D_READ_TEXTURE_RESULT, Pixels))
   {
      return RcddGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);
   }

   if (iEsc == RPI5VC4_ESCAPE_WAIT_VBLANK &&
       pvOut != NULL && cjOut >= sizeof(RPI5VC4_VBLANK_RESULT))
   {
      return RcddGpuEscape(ppdev, iEsc, NULL, 0, pvOut, cjOut);
   }

   if (iEsc == CDD_ESCAPE_PRESENT_STATS)
   {
      ULONG Ret;

      if (pvOut == NULL || cjOut < sizeof(DXGK_PRESENT_STATS))
         return 0;

      if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_DXGK_PRESENT_STATS,
                             NULL, 0, pvOut, sizeof(DXGK_PRESENT_STATS), &Ret))
      {
         return 0;
      }

      return 1;
   }

   if (iEsc == CDD_ESCAPE_SUPPRESS_CURSOR)
   {
      /* Reject malformed calls so ExtEscape reports failure (<= 0). */
      if (pvIn == NULL || cjIn < sizeof(LONG))
         return 0;

      value = *(const LONG *)pvIn;
      RcddSetCursorSuppressed(ppdev, pso, value != 0);
      return 1;
   }

   if (iEsc == CDD_ESCAPE_COMPOSITION_SYNC)
   {
      ULONG Ret;

      if (pvIn == NULL || cjIn < sizeof(LONG))
         return 0;

      value = *(const LONG *)pvIn;

      /* END flushes the dirty rects accumulated during the composition. */
      if (EngDeviceIoControl(ppdev->hDriver,
                             (value != 0) ? IOCTL_VIDEO_DXGK_COMPOSITION_BEGIN
                                          : IOCTL_VIDEO_DXGK_COMPOSITION_END,
                             NULL, 0, NULL, 0, &Ret))
      {
         return 0;
      }

      ppdev->CompositionActive = (value != 0);
      return 1;
   }

   if (iEsc == CDD_ESCAPE_REGISTER_VBLANK)
   {
      ULONG Ret;

      if (pvIn == NULL || cjIn < sizeof(ULONGLONG))
         return 0;

      /* Opaque event handle in the compositor process (0 clears). dxgkrnl
       * validates and references it before the present timer can signal it. */
      if (EngDeviceIoControl(ppdev->hDriver,
                             IOCTL_VIDEO_DXGK_REGISTER_VBLANK,
                             pvIn, sizeof(ULONGLONG), NULL, 0, &Ret))
      {
         return 0;
      }
      return 1;
   }

   /* Unknown escape - behave as a driver with no escape handler. */
   return 0;
}
