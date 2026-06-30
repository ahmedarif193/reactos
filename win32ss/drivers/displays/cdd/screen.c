/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Mode enumeration, PDEV mode/caps init and assert-mode.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "cdd.h"

static const LOGFONTW SystemFont = { 16, 7, 0, 0, 700, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_DONTCARE, L"System" };
static const LOGFONTW AnsiVariableFont = { 12, 9, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_STROKE_PRECIS, PROOF_QUALITY, VARIABLE_PITCH | FF_DONTCARE, L"MS Sans Serif" };
static const LOGFONTW AnsiFixedFont = { 12, 9, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_STROKE_PRECIS, PROOF_QUALITY, FIXED_PITCH | FF_DONTCARE, L"Courier" };

/*
 * RcddGetAvailableModes
 *
 * Asks the WDDM display device (dxgkrnl) for its mode list and filters it to
 * the modes cdd supports (one plane, graphics, 8/16/24/32 bpp). Rejected modes
 * have their Length zeroed.
 */
static DWORD
RcddGetAvailableModes(
   HANDLE hDriver,
   PVIDEO_MODE_INFORMATION *ModeInfo,
   DWORD *ModeInfoSize)
{
   ULONG ulTemp;
   VIDEO_NUM_MODES Modes;
   PVIDEO_MODE_INFORMATION ModeInfoPtr;

   if (EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES, NULL,
                          0, &Modes, sizeof(VIDEO_NUM_MODES), &ulTemp))
   {
      return 0;
   }

   if (Modes.NumModes == 0)
   {
      return 0;
   }

   *ModeInfoSize = Modes.ModeInformationLength;

   *ModeInfo = (PVIDEO_MODE_INFORMATION)EngAllocMem(0, Modes.NumModes *
      Modes.ModeInformationLength, ALLOC_TAG);
   if (*ModeInfo == NULL)
   {
      return 0;
   }

   if (EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_AVAIL_MODES, NULL, 0,
                          *ModeInfo, Modes.NumModes * Modes.ModeInformationLength,
                          &ulTemp))
   {
      EngFreeMem(*ModeInfo);
      *ModeInfo = NULL;
      return 0;
   }

   ulTemp = Modes.NumModes;
   ModeInfoPtr = *ModeInfo;

   while (ulTemp--)
   {
      if ((ModeInfoPtr->NumberOfPlanes != 1) ||
          !(ModeInfoPtr->AttributeFlags & VIDEO_MODE_GRAPHICS) ||
          ((ModeInfoPtr->BitsPerPlane != 8) &&
           (ModeInfoPtr->BitsPerPlane != 16) &&
           (ModeInfoPtr->BitsPerPlane != 24) &&
           (ModeInfoPtr->BitsPerPlane != 32)))
      {
         ModeInfoPtr->Length = 0;
      }

      ModeInfoPtr = (PVIDEO_MODE_INFORMATION)
         (((PUCHAR)ModeInfoPtr) + Modes.ModeInformationLength);
   }

   return Modes.NumModes;
}

/*
 * RcddInitScreenInfo
 *
 * Selects the mode requested in pDevMode (or the first supported mode) and
 * fills GDIINFO/DEVINFO from the committed WDDM mode.
 */
BOOL
RcddInitScreenInfo(
   PRCDD_PDEV ppdev,
   LPDEVMODEW pDevMode,
   PGDIINFO pGdiInfo,
   PDEVINFO pDevInfo)
{
   ULONG ModeCount;
   ULONG ModeInfoSize;
   PVIDEO_MODE_INFORMATION ModeInfo, ModeInfoPtr, SelectedMode = NULL;
   VIDEO_COLOR_CAPABILITIES ColorCapabilities;
   ULONG Temp;

   ModeCount = RcddGetAvailableModes(ppdev->hDriver, &ModeInfo, &ModeInfoSize);
   if (ModeCount == 0)
   {
      return FALSE;
   }

   if (pDevMode->dmPelsWidth == 0 && pDevMode->dmPelsHeight == 0 &&
       pDevMode->dmBitsPerPel == 0 && pDevMode->dmDisplayFrequency == 0)
   {
      ModeInfoPtr = ModeInfo;
      while (ModeCount-- > 0)
      {
         if (ModeInfoPtr->Length == 0)
         {
            ModeInfoPtr = (PVIDEO_MODE_INFORMATION)
               (((PUCHAR)ModeInfoPtr) + ModeInfoSize);
            continue;
         }
         SelectedMode = ModeInfoPtr;
         break;
      }
   }
   else
   {
      ModeInfoPtr = ModeInfo;
      while (ModeCount-- > 0)
      {
         if (ModeInfoPtr->Length > 0 &&
             pDevMode->dmPelsWidth == ModeInfoPtr->VisScreenWidth &&
             pDevMode->dmPelsHeight == ModeInfoPtr->VisScreenHeight &&
             pDevMode->dmBitsPerPel == (ModeInfoPtr->BitsPerPlane *
                                        ModeInfoPtr->NumberOfPlanes) &&
             pDevMode->dmDisplayFrequency == ModeInfoPtr->Frequency)
         {
            SelectedMode = ModeInfoPtr;
            break;
         }

         ModeInfoPtr = (PVIDEO_MODE_INFORMATION)
            (((PUCHAR)ModeInfoPtr) + ModeInfoSize);
      }
   }

   if (SelectedMode == NULL)
   {
      EngFreeMem(ModeInfo);
      return FALSE;
   }

   ppdev->ModeIndex = SelectedMode->ModeIndex;
   ppdev->ScreenWidth = SelectedMode->VisScreenWidth;
   ppdev->ScreenHeight = SelectedMode->VisScreenHeight;
   ppdev->ScreenDelta = SelectedMode->ScreenStride;
   ppdev->BitsPerPixel = (UCHAR)(SelectedMode->BitsPerPlane * SelectedMode->NumberOfPlanes);

   ppdev->RedMask = SelectedMode->RedMask;
   ppdev->GreenMask = SelectedMode->GreenMask;
   ppdev->BlueMask = SelectedMode->BlueMask;

   pGdiInfo->ulVersion = GDI_DRIVER_VERSION;
   pGdiInfo->ulTechnology = DT_RASDISPLAY;
   pGdiInfo->ulHorzSize = SelectedMode->XMillimeter;
   pGdiInfo->ulVertSize = SelectedMode->YMillimeter;
   pGdiInfo->ulHorzRes = SelectedMode->VisScreenWidth;
   pGdiInfo->ulVertRes = SelectedMode->VisScreenHeight;
   pGdiInfo->ulPanningHorzRes = SelectedMode->VisScreenWidth;
   pGdiInfo->ulPanningVertRes = SelectedMode->VisScreenHeight;
   pGdiInfo->cBitsPixel = SelectedMode->BitsPerPlane;
   pGdiInfo->cPlanes = SelectedMode->NumberOfPlanes;
   pGdiInfo->ulVRefresh = SelectedMode->Frequency;
   pGdiInfo->ulBltAlignment = 1;
   pGdiInfo->ulLogPixelsX = pDevMode->dmLogPixels;
   pGdiInfo->ulLogPixelsY = pDevMode->dmLogPixels;
   pGdiInfo->flTextCaps = TC_RA_ABLE;
   pGdiInfo->flRaster = 0;
   pGdiInfo->ulDACRed = SelectedMode->NumberRedBits;
   pGdiInfo->ulDACGreen = SelectedMode->NumberGreenBits;
   pGdiInfo->ulDACBlue = SelectedMode->NumberBlueBits;
   pGdiInfo->ulAspectX = 0x24;
   pGdiInfo->ulAspectY = 0x24;
   pGdiInfo->ulAspectXY = 0x33;
   pGdiInfo->xStyleStep = 1;
   pGdiInfo->yStyleStep = 1;
   pGdiInfo->denStyleStep = 3;
   pGdiInfo->ptlPhysOffset.x = 0;
   pGdiInfo->ptlPhysOffset.y = 0;
   pGdiInfo->szlPhysSize.cx = 0;
   pGdiInfo->szlPhysSize.cy = 0;

   if (!EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_QUERY_COLOR_CAPABILITIES,
                           NULL, 0, &ColorCapabilities,
                           sizeof(VIDEO_COLOR_CAPABILITIES), &Temp))
   {
      pGdiInfo->ciDevice.Red.x = ColorCapabilities.RedChromaticity_x;
      pGdiInfo->ciDevice.Red.y = ColorCapabilities.RedChromaticity_y;
      pGdiInfo->ciDevice.Green.x = ColorCapabilities.GreenChromaticity_x;
      pGdiInfo->ciDevice.Green.y = ColorCapabilities.GreenChromaticity_y;
      pGdiInfo->ciDevice.Blue.x = ColorCapabilities.BlueChromaticity_x;
      pGdiInfo->ciDevice.Blue.y = ColorCapabilities.BlueChromaticity_y;
      pGdiInfo->ciDevice.AlignmentWhite.x = ColorCapabilities.WhiteChromaticity_x;
      pGdiInfo->ciDevice.AlignmentWhite.y = ColorCapabilities.WhiteChromaticity_y;
      pGdiInfo->ciDevice.AlignmentWhite.Y = ColorCapabilities.WhiteChromaticity_Y;
      if (ColorCapabilities.AttributeFlags & VIDEO_DEVICE_COLOR)
      {
         pGdiInfo->ciDevice.RedGamma = ColorCapabilities.RedGamma;
         pGdiInfo->ciDevice.GreenGamma = ColorCapabilities.GreenGamma;
         pGdiInfo->ciDevice.BlueGamma = ColorCapabilities.BlueGamma;
      }
      else
      {
         pGdiInfo->ciDevice.RedGamma = ColorCapabilities.WhiteGamma;
         pGdiInfo->ciDevice.GreenGamma = ColorCapabilities.WhiteGamma;
         pGdiInfo->ciDevice.BlueGamma = ColorCapabilities.WhiteGamma;
      }
   }
   else
   {
      pGdiInfo->ciDevice.Red.x = 6700;
      pGdiInfo->ciDevice.Red.y = 3300;
      pGdiInfo->ciDevice.Green.x = 2100;
      pGdiInfo->ciDevice.Green.y = 7100;
      pGdiInfo->ciDevice.Blue.x = 1400;
      pGdiInfo->ciDevice.Blue.y = 800;
      pGdiInfo->ciDevice.AlignmentWhite.x = 3127;
      pGdiInfo->ciDevice.AlignmentWhite.y = 3290;
      pGdiInfo->ciDevice.AlignmentWhite.Y = 0;
      pGdiInfo->ciDevice.RedGamma = 20000;
      pGdiInfo->ciDevice.GreenGamma = 20000;
      pGdiInfo->ciDevice.BlueGamma = 20000;
   }

   pGdiInfo->ciDevice.Red.Y = 0;
   pGdiInfo->ciDevice.Green.Y = 0;
   pGdiInfo->ciDevice.Blue.Y = 0;
   pGdiInfo->ciDevice.Cyan.x = 0;
   pGdiInfo->ciDevice.Cyan.y = 0;
   pGdiInfo->ciDevice.Cyan.Y = 0;
   pGdiInfo->ciDevice.Magenta.x = 0;
   pGdiInfo->ciDevice.Magenta.y = 0;
   pGdiInfo->ciDevice.Magenta.Y = 0;
   pGdiInfo->ciDevice.Yellow.x = 0;
   pGdiInfo->ciDevice.Yellow.y = 0;
   pGdiInfo->ciDevice.Yellow.Y = 0;
   pGdiInfo->ciDevice.MagentaInCyanDye = 0;
   pGdiInfo->ciDevice.YellowInCyanDye = 0;
   pGdiInfo->ciDevice.CyanInMagentaDye = 0;
   pGdiInfo->ciDevice.YellowInMagentaDye = 0;
   pGdiInfo->ciDevice.CyanInYellowDye = 0;
   pGdiInfo->ciDevice.MagentaInYellowDye = 0;
   pGdiInfo->ulDevicePelsDPI = 0;
   pGdiInfo->ulPrimaryOrder = PRIMARY_ORDER_CBA;
   pGdiInfo->ulHTPatternSize = HT_PATSIZE_4x4_M;
   pGdiInfo->flHTFlags = HT_FLAG_ADDITIVE_PRIMS;

   pDevInfo->flGraphicsCaps = 0;
   pDevInfo->lfDefaultFont = SystemFont;
   pDevInfo->lfAnsiVarFont = AnsiVariableFont;
   pDevInfo->lfAnsiFixFont = AnsiFixedFont;
   pDevInfo->cFonts = 0;
   pDevInfo->cxDither = 0;
   pDevInfo->cyDither = 0;
   pDevInfo->hpalDefault = 0;
   pDevInfo->flGraphicsCaps2 = 0;

   if (ppdev->BitsPerPixel == 8)
   {
      pGdiInfo->ulNumColors = 20;
      pGdiInfo->ulNumPalReg = 1 << ppdev->BitsPerPixel;
      pGdiInfo->ulHTOutputFormat = HT_FORMAT_8BPP;
      pDevInfo->flGraphicsCaps |= GCAPS_PALMANAGED;
      pDevInfo->iDitherFormat = BMF_8BPP;
      /* Assuming palette is orthogonal - all colors are same size. */
      ppdev->PaletteShift = (UCHAR)(8 - pGdiInfo->ulDACRed);
   }
   else
   {
      pGdiInfo->ulNumColors = (ULONG)(-1);
      pGdiInfo->ulNumPalReg = 0;
      switch (ppdev->BitsPerPixel)
      {
         case 16:
            pGdiInfo->ulHTOutputFormat = HT_FORMAT_16BPP;
            pDevInfo->iDitherFormat = BMF_16BPP;
            break;

         case 24:
            pGdiInfo->ulHTOutputFormat = HT_FORMAT_24BPP;
            pDevInfo->iDitherFormat = BMF_24BPP;
            break;

         default:
            pGdiInfo->ulHTOutputFormat = HT_FORMAT_32BPP;
            pDevInfo->iDitherFormat = BMF_32BPP;
      }
   }

   RcddInitHardwarePointer(ppdev);

   EngFreeMem(ModeInfo);
   return TRUE;
}

/*
 * RcddGetModes
 *
 * Returns the list of available modes for the device.
 */
ULONG APIENTRY
RcddGetModes(
   IN HANDLE hDriver,
   IN ULONG cjSize,
   OUT DEVMODEW *pdm)
{
   ULONG ModeCount;
   ULONG ModeInfoSize;
   PVIDEO_MODE_INFORMATION ModeInfo, ModeInfoPtr;
   ULONG OutputSize;

   UNREFERENCED_PARAMETER(cjSize);

   ModeCount = RcddGetAvailableModes(hDriver, &ModeInfo, &ModeInfoSize);
   if (ModeCount == 0)
   {
      return 0;
   }

   if (pdm == NULL)
   {
      EngFreeMem(ModeInfo);
      return ModeCount * sizeof(DEVMODEW);
   }

   OutputSize = 0;
   ModeInfoPtr = ModeInfo;

   while (ModeCount-- > 0)
   {
      if (ModeInfoPtr->Length == 0)
      {
         ModeInfoPtr = (PVIDEO_MODE_INFORMATION)(((ULONG_PTR)ModeInfoPtr) + ModeInfoSize);
         continue;
      }

      memset(pdm, 0, sizeof(DEVMODEW));
      memcpy(pdm->dmDeviceName, DEVICE_NAME, sizeof(DEVICE_NAME));
      pdm->dmSpecVersion =
      pdm->dmDriverVersion = DM_SPECVERSION;
      pdm->dmSize = sizeof(DEVMODEW);
      pdm->dmDriverExtra = 0;
      pdm->dmBitsPerPel = ModeInfoPtr->NumberOfPlanes * ModeInfoPtr->BitsPerPlane;
      pdm->dmPelsWidth = ModeInfoPtr->VisScreenWidth;
      pdm->dmPelsHeight = ModeInfoPtr->VisScreenHeight;
      pdm->dmDisplayFrequency = ModeInfoPtr->Frequency;
      pdm->dmDisplayFlags = 0;
      pdm->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT |
                      DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;

      ModeInfoPtr = (PVIDEO_MODE_INFORMATION)(((ULONG_PTR)ModeInfoPtr) + ModeInfoSize);
      pdm = (LPDEVMODEW)(((ULONG_PTR)pdm) + sizeof(DEVMODEW));
      OutputSize += sizeof(DEVMODEW);
   }

   EngFreeMem(ModeInfo);
   return OutputSize;
}

/*
 * RcddAssertMode
 *
 * Enables or disables the device on a mode switch / power transition. On
 * re-enable the WDDM mode is re-committed and the whole shadow is re-presented
 * because the scan-out contents are undefined.
 */
BOOL APIENTRY
RcddAssertMode(
   IN DHPDEV dhpdev,
   IN BOOL bEnable)
{
   PRCDD_PDEV ppdev = (PRCDD_PDEV)dhpdev;
   ULONG ulTemp;

   if (bEnable)
   {
      if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                             &(ppdev->ModeIndex), sizeof(ULONG), NULL, 0,
                             &ulTemp))
      {
         return FALSE;
      }
      if (ppdev->BitsPerPixel == 8)
      {
         RcddSetPaletteEntries(dhpdev, ppdev->PaletteEntries, 0, 256);
      }

      /* The scan-out contents are unknown; re-present the full shadow. */
      RcddPresent(ppdev, NULL);

      return TRUE;
   }

   /* Reset the device to a known state via the WDDM display device. */
   return !EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_RESET_DEVICE,
                              NULL, 0, NULL, 0, &ulTemp);
}
