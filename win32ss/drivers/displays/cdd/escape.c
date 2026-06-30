/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM compositor private control channel (DrvEscape).
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * A compositor and the display driver need a private channel that GDI knows
 * nothing about; the public mechanism is DrvEscape / ExtEscape with a private
 * escape code. cdd recognizes exactly two codes (our values, see cdd.h):
 *
 *   CDD_ESCAPE_SUPPRESS_CURSOR  - LONG in: non-zero = the compositor draws the
 *       cursor, so cdd stops drawing its own cursor; zero = resume. Returns 1.
 *   CDD_ESCAPE_COMPOSITION_SYNC - LONG in: non-zero = begin a composed frame,
 *       zero = end it. Either way cdd presents the current frame and acks so
 *       the compositor can pace itself. Returns 1.
 *
 * Everything else is routed to the default handler (return 0 -> "not
 * supported"), exactly as if the driver had no DrvEscape at all. This is the
 * seam a future DWM plugs into; without a DWM these escapes never arrive and
 * cdd presents directly.
 */

#include "cdd.h"

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

   UNREFERENCED_PARAMETER(cjOut);
   UNREFERENCED_PARAMETER(pvOut);

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
          RequestedEscape == CDD_ESCAPE_COMPOSITION_SYNC)
      {
         return 1;
      }
      return 0;
   }

   if (iEsc == CDD_ESCAPE_SUPPRESS_CURSOR)
   {
      /* Reject malformed calls so ExtEscape reports failure (<= 0). */
      if (pvIn == NULL || cjIn < sizeof(LONG))
         return 0;

      value = *(const LONG *)pvIn;
      ppdev->CursorSuppressed = (value != 0);

      /*
       * Suppression takes effect on the next RcddMovePointer/RcddSetPointerShape
       * (both honor CursorSuppressed). Resume restores the cursor on the next
       * pointer update issued by GDI.
       */
      return 1;
   }

   if (iEsc == CDD_ESCAPE_COMPOSITION_SYNC)
   {
      if (pvIn == NULL || cjIn < sizeof(LONG))
         return 0;

      value = *(const LONG *)pvIn;
      ppdev->CompositionActive = (value != 0);

      /*
       * Present/vblank acknowledge: push the composed frame to the WDDM
       * scan-out so the compositor can pace the next frame.
       */
      RcddPresent(ppdev, NULL);
      return 1;
   }

   /* Unknown escape - behave as a driver with no escape handler. */
   return 0;
}
