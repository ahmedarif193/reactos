/*
 * PROJECT:     ReactOS VirtIO GPU Display Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     XPDM 2D display driver for the VirtIO GPU miniport
 */

#include "display.h"

static LOGFONTW SystemFont =
    { 16, 7, 0, 0, 700, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_DONTCARE,
      L"System" };
static LOGFONTW AnsiVariableFont =
    { 12, 9, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
      CLIP_STROKE_PRECIS, PROOF_QUALITY, VARIABLE_PITCH | FF_DONTCARE,
      L"MS Sans Serif" };
static LOGFONTW AnsiFixedFont =
    { 12, 9, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
      CLIP_STROKE_PRECIS, PROOF_QUALITY, FIXED_PITCH | FF_DONTCARE,
      L"Courier" };

#define VIRTGPU_SAVED_BITS_SIGNATURE 'bsrV'
#define VIRTGPU_PDEV_SIGNATURE 'pgrV'
#define VIRTGPU_DEVICE_BITMAP_SIGNATURE 'bdrV'
#define VIRTGPU_OPENGL_ICD_VERSION 1
#define VIRTGPU_OPENGL_ICD_DRIVER_VERSION 1
#define OPENGL_GETINFO_DRVNAME 0

/* ReactOS Eng path and PlgBlt fallbacks are stubs, so do not hook them here. */
#define VIRTGPU_SURFACE_HOOKS \
    (HOOK_SYNCHRONIZE | \
     HOOK_BITBLT | \
     HOOK_COPYBITS | \
     HOOK_LINETO | \
     HOOK_PAINT | \
     HOOK_STRETCHBLT | \
     HOOK_STRETCHBLTROP | \
     HOOK_ALPHABLEND | \
     HOOK_TRANSPARENTBLT | \
     HOOK_GRADIENTFILL)

typedef struct _VIRTGPU_DEVICE_BITMAP
{
    ULONG Signature;
    PPDEV ppdev;
    HSURF hSurfBacking;
    SURFOBJ *psoBacking;
    SIZEL Size;
    ULONG Format;
} VIRTGPU_DEVICE_BITMAP, *PVIRTGPU_DEVICE_BITMAP;

typedef struct _VIRTGPU_SAVED_BITS
{
    ULONG Signature;
    RECTL Rect;
    ULONG Stride;
    ULONG Size;
    UCHAR Bits[1];
} VIRTGPU_SAVED_BITS, *PVIRTGPU_SAVED_BITS;

typedef struct _VIRTGPU_OPENGL_INFO
{
    ULONG Version;
    ULONG DriverVersion;
    WCHAR DriverName[MAX_PATH + 1];
} VIRTGPU_OPENGL_INFO, *PVIRTGPU_OPENGL_INFO;

static DRVFN VirtGpuDrvFunctions[] =
{
    { INDEX_DrvEnablePDEV, (PFN)DrvEnablePDEV },
    { INDEX_DrvCompletePDEV, (PFN)DrvCompletePDEV },
    { INDEX_DrvDisablePDEV, (PFN)DrvDisablePDEV },
    { INDEX_DrvEnableSurface, (PFN)DrvEnableSurface },
    { INDEX_DrvDisableSurface, (PFN)DrvDisableSurface },
    { INDEX_DrvAssertMode, (PFN)DrvAssertMode },
    { INDEX_DrvGetModes, (PFN)DrvGetModes },
    { INDEX_DrvSetPointerShape, (PFN)DrvSetPointerShape },
    { INDEX_DrvMovePointer, (PFN)DrvMovePointer },
    { INDEX_DrvSynchronize, (PFN)DrvSynchronize },
    { INDEX_DrvSynchronizeSurface, (PFN)DrvSynchronizeSurface },
    { INDEX_DrvSaveScreenBits, (PFN)DrvSaveScreenBits },
    { INDEX_DrvCreateDeviceBitmap, (PFN)DrvCreateDeviceBitmap },
    { INDEX_DrvDeleteDeviceBitmap, (PFN)DrvDeleteDeviceBitmap },
    { INDEX_DrvEscape, (PFN)DrvEscape },
    { INDEX_DrvGetDirectDrawInfo, (PFN)DrvGetDirectDrawInfo },
    { INDEX_DrvEnableDirectDraw, (PFN)DrvEnableDirectDraw },
    { INDEX_DrvDisableDirectDraw, (PFN)DrvDisableDirectDraw },
    { INDEX_DrvBitBlt, (PFN)DrvBitBlt },
    { INDEX_DrvCopyBits, (PFN)DrvCopyBits },
    { INDEX_DrvLineTo, (PFN)DrvLineTo },
    { INDEX_DrvPaint, (PFN)DrvPaint },
    { INDEX_DrvStretchBlt, (PFN)DrvStretchBlt },
    { INDEX_DrvStretchBltROP, (PFN)DrvStretchBltROP },
    { INDEX_DrvAlphaBlend, (PFN)DrvAlphaBlend },
    { INDEX_DrvTransparentBlt, (PFN)DrvTransparentBlt },
    { INDEX_DrvGradientFill, (PFN)DrvGradientFill },
};

static VOID
VirtGpuOrderRect(_Inout_ RECTL *Rect)
{
    LONG Temp;

    if (Rect->left > Rect->right)
    {
        Temp = Rect->left;
        Rect->left = Rect->right;
        Rect->right = Temp;
    }

    if (Rect->top > Rect->bottom)
    {
        Temp = Rect->top;
        Rect->top = Rect->bottom;
        Rect->bottom = Temp;
    }
}

static BOOL
VirtGpuIntersectRect(
    _Out_ RECTL *Destination,
    _In_ const RECTL *Left,
    _In_ const RECTL *Right)
{
    Destination->left = max(Left->left, Right->left);
    Destination->top = max(Left->top, Right->top);
    Destination->right = min(Left->right, Right->right);
    Destination->bottom = min(Left->bottom, Right->bottom);
    return (Destination->left < Destination->right) &&
           (Destination->top < Destination->bottom);
}

static PPDEV
VirtGpuDispPdevFromSurface(
    _In_opt_ SURFOBJ *pso)
{
    PPDEV ppdev;

    if (pso == NULL)
        return NULL;

    if (pso->dhpdev != 0)
    {
        ppdev = (PPDEV)pso->dhpdev;
        if (ppdev->Signature == VIRTGPU_PDEV_SIGNATURE)
            return ppdev;
    }

    return NULL;
}

static PPDEV
VirtGpuDispFindPdev(
    _In_opt_ SURFOBJ *First,
    _In_opt_ SURFOBJ *Second,
    _In_opt_ SURFOBJ *Third)
{
    PPDEV ppdev;

    ppdev = VirtGpuDispPdevFromSurface(First);
    if (ppdev != NULL)
        return ppdev;

    ppdev = VirtGpuDispPdevFromSurface(Second);
    if (ppdev != NULL)
        return ppdev;

    return VirtGpuDispPdevFromSurface(Third);
}

static BOOL
VirtGpuDispIsPrimarySurface(
    _In_ PPDEV ppdev,
    _In_opt_ SURFOBJ *pso)
{
    return (pso != NULL) && (pso->dhsurf == (DHSURF)ppdev);
}

static PVIRTGPU_DEVICE_BITMAP
VirtGpuDispGetDeviceBitmap(
    _In_ PPDEV ppdev,
    _In_opt_ SURFOBJ *pso)
{
    PVIRTGPU_DEVICE_BITMAP DeviceBitmap;

    if ((ppdev == NULL) ||
        (pso == NULL) ||
        (pso->dhsurf == 0) ||
        (pso->dhsurf == (DHSURF)ppdev) ||
        (pso->dhpdev != (DHPDEV)ppdev))
    {
        return NULL;
    }

    DeviceBitmap = (PVIRTGPU_DEVICE_BITMAP)pso->dhsurf;
    if ((DeviceBitmap->Signature != VIRTGPU_DEVICE_BITMAP_SIGNATURE) ||
        (DeviceBitmap->ppdev != ppdev))
    {
        return NULL;
    }

    return DeviceBitmap;
}

static SURFOBJ *
VirtGpuDispMapSurface(
    _In_ PPDEV ppdev,
    _In_opt_ SURFOBJ *pso)
{
    PVIRTGPU_DEVICE_BITMAP DeviceBitmap;

    if (VirtGpuDispIsPrimarySurface(ppdev, pso))
        return ppdev->psoFrameBuffer;

    DeviceBitmap = VirtGpuDispGetDeviceBitmap(ppdev, pso);
    if (DeviceBitmap != NULL)
        return DeviceBitmap->psoBacking;

    return pso;
}

VOID
VirtGpuDispFlushRect(
    _Inout_ PPDEV ppdev,
    _In_opt_ const RECTL *Rect)
{
    VIRTGPU_DIRTY_RECT DirtyRect;
    RECTL Clipped;
    RECTL Bounds;
    ULONG Returned;

    if ((ppdev == NULL) || (ppdev->hDriver == NULL))
        return;

    Bounds.left = 0;
    Bounds.top = 0;
    Bounds.right = ppdev->ScreenWidth;
    Bounds.bottom = ppdev->ScreenHeight;

    if (Rect != NULL)
    {
        Clipped = *Rect;
        VirtGpuOrderRect(&Clipped);
        if (!VirtGpuIntersectRect(&Clipped, &Clipped, &Bounds))
            return;
    }
    else
    {
        Clipped = Bounds;
    }

    DirtyRect.Left = Clipped.left;
    DirtyRect.Top = Clipped.top;
    DirtyRect.Right = Clipped.right;
    DirtyRect.Bottom = Clipped.bottom;

    EngDeviceIoControl(ppdev->hDriver,
                       IOCTL_VIDEO_VIRTGPU_FLUSH_RECT,
                       &DirtyRect,
                       sizeof(DirtyRect),
                       NULL,
                       0,
                       &Returned);
}

VOID
VirtGpuDispFlushSurfaceRect(
    _Inout_ SURFOBJ *pso,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ const RECTL *Rect)
{
    RECTL DirtyRect;
    RECTL Ordered;
    PPDEV ppdev;

    if (pso == NULL)
        return;

    ppdev = VirtGpuDispFindPdev(pso, NULL, NULL);
    if ((ppdev == NULL) || !VirtGpuDispIsPrimarySurface(ppdev, pso))
        return;

    if (Rect != NULL)
    {
        Ordered = *Rect;
        VirtGpuOrderRect(&Ordered);
    }
    else
    {
        Ordered.left = 0;
        Ordered.top = 0;
        Ordered.right = pso->sizlBitmap.cx;
        Ordered.bottom = pso->sizlBitmap.cy;
    }

    if ((pco != NULL) && (pco->iDComplexity != DC_TRIVIAL))
    {
        if (!VirtGpuIntersectRect(&DirtyRect, &Ordered, &pco->rclBounds))
            return;
    }
    else
    {
        DirtyRect = Ordered;
    }

    VirtGpuDispFlushRect(ppdev, &DirtyRect);
}

static DWORD
VirtGpuGetAvailableModes(
    _In_ HANDLE hDriver,
    _Outptr_ PVIDEO_MODE_INFORMATION *ModeInfo,
    _Out_ DWORD *ModeInfoSize)
{
    VIDEO_NUM_MODES Modes;
    PVIDEO_MODE_INFORMATION ModeInfoPtr;
    ULONG Returned;
    DWORD Index;

    if (EngDeviceIoControl(hDriver,
                           IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES,
                           NULL,
                           0,
                           &Modes,
                           sizeof(Modes),
                           &Returned) != 0)
    {
        return 0;
    }

    if ((Modes.NumModes == 0) || (Modes.ModeInformationLength == 0))
        return 0;

    *ModeInfoSize = Modes.ModeInformationLength;
    *ModeInfo = EngAllocMem(0,
                            Modes.NumModes * Modes.ModeInformationLength,
                            ALLOC_TAG);
    if (*ModeInfo == NULL)
        return 0;

    if (EngDeviceIoControl(hDriver,
                           IOCTL_VIDEO_QUERY_AVAIL_MODES,
                           NULL,
                           0,
                           *ModeInfo,
                           Modes.NumModes * Modes.ModeInformationLength,
                           &Returned) != 0)
    {
        EngFreeMem(*ModeInfo);
        *ModeInfo = NULL;
        return 0;
    }

    ModeInfoPtr = *ModeInfo;
    for (Index = 0; Index < Modes.NumModes; ++Index)
    {
        if ((ModeInfoPtr->NumberOfPlanes != 1) ||
            !(ModeInfoPtr->AttributeFlags & VIDEO_MODE_GRAPHICS) ||
            (ModeInfoPtr->BitsPerPlane != 32))
        {
            ModeInfoPtr->Length = 0;
        }

        ModeInfoPtr = (PVIDEO_MODE_INFORMATION)
            ((PUCHAR)ModeInfoPtr + Modes.ModeInformationLength);
    }

    return Modes.NumModes;
}

BOOL
VirtGpuDispInitScreenInfo(
    _Inout_ PPDEV ppdev,
    _In_ LPDEVMODEW DevMode,
    _Out_ PGDIINFO GdiInfo,
    _Out_ PDEVINFO DevInfo)
{
    ULONG ModeCount;
    ULONG ModeInfoSize;
    PVIDEO_MODE_INFORMATION ModeInfo;
    PVIDEO_MODE_INFORMATION ModeInfoPtr;
    PVIDEO_MODE_INFORMATION SelectedMode = NULL;
    ULONG Index;
    ULONG Dpi;

    memset(GdiInfo, 0, sizeof(*GdiInfo));
    memset(DevInfo, 0, sizeof(*DevInfo));

    ModeCount = VirtGpuGetAvailableModes(ppdev->hDriver, &ModeInfo, &ModeInfoSize);
    if (ModeCount == 0)
        return FALSE;

    ModeInfoPtr = ModeInfo;
    for (Index = 0; Index < ModeCount; ++Index)
    {
        if (ModeInfoPtr->Length != 0)
        {
            if ((DevMode->dmPelsWidth == 0 && DevMode->dmPelsHeight == 0) ||
                ((DevMode->dmPelsWidth == ModeInfoPtr->VisScreenWidth) &&
                 (DevMode->dmPelsHeight == ModeInfoPtr->VisScreenHeight) &&
                 ((DevMode->dmBitsPerPel == 0) ||
                  (DevMode->dmBitsPerPel ==
                   ModeInfoPtr->BitsPerPlane * ModeInfoPtr->NumberOfPlanes))))
            {
                SelectedMode = ModeInfoPtr;
                break;
            }
        }

        ModeInfoPtr = (PVIDEO_MODE_INFORMATION)((PUCHAR)ModeInfoPtr + ModeInfoSize);
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
    ppdev->BitsPerPixel =
        (UCHAR)(SelectedMode->BitsPerPlane * SelectedMode->NumberOfPlanes);
    ppdev->RedMask = SelectedMode->RedMask;
    ppdev->GreenMask = SelectedMode->GreenMask;
    ppdev->BlueMask = SelectedMode->BlueMask;

    Dpi = (DevMode->dmLogPixels != 0) ? DevMode->dmLogPixels : 96;

    GdiInfo->ulVersion = GDI_DRIVER_VERSION;
    GdiInfo->ulTechnology = DT_RASDISPLAY;
    GdiInfo->ulHorzSize = SelectedMode->XMillimeter;
    GdiInfo->ulVertSize = SelectedMode->YMillimeter;
    GdiInfo->ulHorzRes = SelectedMode->VisScreenWidth;
    GdiInfo->ulVertRes = SelectedMode->VisScreenHeight;
    GdiInfo->ulPanningHorzRes = SelectedMode->VisScreenWidth;
    GdiInfo->ulPanningVertRes = SelectedMode->VisScreenHeight;
    GdiInfo->cBitsPixel = SelectedMode->BitsPerPlane;
    GdiInfo->cPlanes = SelectedMode->NumberOfPlanes;
    GdiInfo->ulVRefresh = SelectedMode->Frequency;
    GdiInfo->ulBltAlignment = 1;
    GdiInfo->ulLogPixelsX = Dpi;
    GdiInfo->ulLogPixelsY = Dpi;
    GdiInfo->flTextCaps = TC_RA_ABLE;
    GdiInfo->ulDACRed = SelectedMode->NumberRedBits;
    GdiInfo->ulDACGreen = SelectedMode->NumberGreenBits;
    GdiInfo->ulDACBlue = SelectedMode->NumberBlueBits;
    GdiInfo->ulAspectX = 0x24;
    GdiInfo->ulAspectY = 0x24;
    GdiInfo->ulAspectXY = 0x33;
    GdiInfo->xStyleStep = 1;
    GdiInfo->yStyleStep = 1;
    GdiInfo->denStyleStep = 3;
    GdiInfo->ulNumColors = (ULONG)-1;
    GdiInfo->ulNumPalReg = 0;
    GdiInfo->ulHTOutputFormat = HT_FORMAT_32BPP;
    GdiInfo->ulPrimaryOrder = PRIMARY_ORDER_CBA;
    GdiInfo->ulHTPatternSize = HT_PATSIZE_4x4_M;
    GdiInfo->flHTFlags = HT_FLAG_ADDITIVE_PRIMS;
    GdiInfo->ciDevice.Red.x = 6700;
    GdiInfo->ciDevice.Red.y = 3300;
    GdiInfo->ciDevice.Green.x = 2100;
    GdiInfo->ciDevice.Green.y = 7100;
    GdiInfo->ciDevice.Blue.x = 1400;
    GdiInfo->ciDevice.Blue.y = 800;
    GdiInfo->ciDevice.AlignmentWhite.x = 3127;
    GdiInfo->ciDevice.AlignmentWhite.y = 3290;
    GdiInfo->ciDevice.RedGamma = 20000;
    GdiInfo->ciDevice.GreenGamma = 20000;
    GdiInfo->ciDevice.BlueGamma = 20000;

    DevInfo->flGraphicsCaps = 0;
    DevInfo->flGraphicsCaps2 = 0;
    DevInfo->lfDefaultFont = SystemFont;
    DevInfo->lfAnsiVarFont = AnsiVariableFont;
    DevInfo->lfAnsiFixFont = AnsiFixedFont;
    DevInfo->iDitherFormat = BMF_32BPP;

    EngFreeMem(ModeInfo);
    return TRUE;
}

BOOL
VirtGpuDispInitPalette(
    _Inout_ PPDEV ppdev,
    _Inout_ PDEVINFO DevInfo)
{
    ppdev->DefaultPalette = DevInfo->hpalDefault =
        EngCreatePalette(PAL_BITFIELDS,
                         0,
                         NULL,
                         ppdev->RedMask,
                         ppdev->GreenMask,
                         ppdev->BlueMask);
    return ppdev->DefaultPalette != NULL;
}

static VOID
VirtGpuDispInit3D(_Inout_ PPDEV ppdev)
{
    VIRTGPU_3D_CAPS Caps;
    ULONG Returned;

    ppdev->ThreeDEnabled = FALSE;
    ppdev->ContextInitSupported = FALSE;
    ppdev->ResourceUuidSupported = FALSE;
    ppdev->ResourceBlobSupported = FALSE;
    ppdev->PreferredCapsetId = 0;
    ppdev->PreferredCapsetVersion = 0;
    ppdev->SupportedCapsetMask = 0;

    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_VIRTGPU_QUERY_3D_CAPS,
                           NULL,
                           0,
                           &Caps,
                           sizeof(Caps),
                           &Returned) != 0)
    {
        return;
    }

    if ((Returned < sizeof(Caps)) || (Caps.Size != sizeof(Caps)))
        return;

    ppdev->ThreeDEnabled = Caps.Enabled != 0;
    ppdev->ContextInitSupported = Caps.ContextInitSupported != 0;
    ppdev->ResourceUuidSupported = Caps.ResourceUuidSupported != 0;
    ppdev->ResourceBlobSupported = Caps.ResourceBlobSupported != 0;
    ppdev->PreferredCapsetId = Caps.PreferredCapsetId;
    ppdev->PreferredCapsetVersion = Caps.PreferredCapsetVersion;
    ppdev->SupportedCapsetMask = Caps.SupportedCapsetMask;
}

ULONG
APIENTRY
DrvGetModes(
    _In_ HANDLE hDriver,
    _In_ ULONG cjSize,
    _Out_opt_ DEVMODEW *pdm)
{
    ULONG ModeCount;
    ULONG ModeInfoSize;
    PVIDEO_MODE_INFORMATION ModeInfo;
    PVIDEO_MODE_INFORMATION ModeInfoPtr;
    ULONG OutputSize = 0;
    ULONG Index;

    UNREFERENCED_PARAMETER(cjSize);

    ModeCount = VirtGpuGetAvailableModes(hDriver, &ModeInfo, &ModeInfoSize);
    if (ModeCount == 0)
        return 0;

    if (pdm == NULL)
    {
        EngFreeMem(ModeInfo);
        return ModeCount * sizeof(DEVMODEW);
    }

    ModeInfoPtr = ModeInfo;
    for (Index = 0; Index < ModeCount; ++Index)
    {
        if (ModeInfoPtr->Length != 0)
        {
            memset(pdm, 0, sizeof(DEVMODEW));
            memcpy(pdm->dmDeviceName, DEVICE_NAME, sizeof(DEVICE_NAME));
            pdm->dmSpecVersion = DM_SPECVERSION;
            pdm->dmDriverVersion = DM_SPECVERSION;
            pdm->dmSize = sizeof(DEVMODEW);
            pdm->dmBitsPerPel =
                ModeInfoPtr->NumberOfPlanes * ModeInfoPtr->BitsPerPlane;
            pdm->dmPelsWidth = ModeInfoPtr->VisScreenWidth;
            pdm->dmPelsHeight = ModeInfoPtr->VisScreenHeight;
            pdm->dmDisplayFrequency = ModeInfoPtr->Frequency;
            pdm->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT |
                            DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;

            pdm = (LPDEVMODEW)((PUCHAR)pdm + sizeof(DEVMODEW));
            OutputSize += sizeof(DEVMODEW);
        }

        ModeInfoPtr = (PVIDEO_MODE_INFORMATION)((PUCHAR)ModeInfoPtr + ModeInfoSize);
    }

    EngFreeMem(ModeInfo);
    return OutputSize;
}

static PUCHAR
VirtGpuSurfaceScan(_In_ SURFOBJ *pso, _In_ LONG y)
{
    PUCHAR Bits = pso->pvScan0 ? pso->pvScan0 : pso->pvBits;
    return Bits + (y * pso->lDelta);
}

static BOOL
VirtGpuMonoMaskBit(_In_ SURFOBJ *pso, _In_ LONG x, _In_ LONG y)
{
    PUCHAR Row = VirtGpuSurfaceScan(pso, y);
    return (Row[x >> 3] & (0x80 >> (x & 7))) != 0;
}

VOID
VirtGpuDispDisablePointer(_Inout_ PPDEV ppdev)
{
    ULONG Returned;

    if (!ppdev->PointerSupported)
        return;

    EngDeviceIoControl(ppdev->hDriver,
                       IOCTL_VIDEO_DISABLE_POINTER,
                       NULL,
                       0,
                       NULL,
                       0,
                       &Returned);
    ppdev->PointerVisible = FALSE;
}

BOOL
VirtGpuDispInitPointer(_Inout_ PPDEV ppdev)
{
    ULONG Returned;
    ULONG WidthInBytes;
    ULONG PixelBytes;

    memset(&ppdev->PointerCapabilities, 0, sizeof(ppdev->PointerCapabilities));

    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES,
                           NULL,
                           0,
                           &ppdev->PointerCapabilities,
                           sizeof(ppdev->PointerCapabilities),
                           &Returned) != 0)
    {
        return TRUE;
    }

    if ((Returned < sizeof(VIDEO_POINTER_CAPABILITIES)) ||
        !(ppdev->PointerCapabilities.Flags & VIDEO_MODE_COLOR_POINTER) ||
        (ppdev->PointerCapabilities.MaxWidth == 0) ||
        (ppdev->PointerCapabilities.MaxHeight == 0))
    {
        return TRUE;
    }

    if (ppdev->PointerCapabilities.MaxWidth >
        VIRTGPU_DISP_MAX_ULONG / sizeof(ULONG))
    {
        return FALSE;
    }

    WidthInBytes = ppdev->PointerCapabilities.MaxWidth * sizeof(ULONG);
    if (ppdev->PointerCapabilities.MaxHeight >
        VIRTGPU_DISP_MAX_ULONG / WidthInBytes)
    {
        return FALSE;
    }

    PixelBytes = WidthInBytes * ppdev->PointerCapabilities.MaxHeight;
    ppdev->PointerAttributesSize =
        offsetof(VIDEO_POINTER_ATTRIBUTES, Pixels) + PixelBytes;
    ppdev->PointerAttributes =
        EngAllocMem(FL_ZERO_MEMORY, ppdev->PointerAttributesSize, ALLOC_TAG);
    if (ppdev->PointerAttributes == NULL)
        return FALSE;

    ppdev->PointerAttributes->Flags = VIDEO_MODE_COLOR_POINTER;
    ppdev->PointerAttributes->WidthInBytes = WidthInBytes;
    ppdev->PointerAttributes->Width = ppdev->PointerCapabilities.MaxWidth;
    ppdev->PointerAttributes->Height = ppdev->PointerCapabilities.MaxHeight;
    ppdev->PointerSupported = TRUE;
    return TRUE;
}

static BOOL
VirtGpuBuildPointerPixels(
    _Inout_ PPDEV ppdev,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ SURFOBJ *psoColor,
    _In_ FLONG fl,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    PVIDEO_POINTER_ATTRIBUTES Attributes;
    ULONG SourceWidth;
    ULONG SourceHeight;
    ULONG PixelBytes;
    ULONG x;
    ULONG y;

    if (psoColor != NULL)
    {
        if (psoColor->iBitmapFormat != BMF_32BPP)
            return FALSE;

        SourceWidth = psoColor->sizlBitmap.cx;
        SourceHeight = psoColor->sizlBitmap.cy;
    }
    else if (psoMask != NULL)
    {
        SourceWidth = psoMask->sizlBitmap.cx;
        SourceHeight = psoMask->sizlBitmap.cy / 2;
    }
    else
    {
        return FALSE;
    }

    if ((SourceWidth == 0) ||
        (SourceHeight == 0) ||
        (SourceWidth > ppdev->PointerCapabilities.MaxWidth) ||
        (SourceHeight > ppdev->PointerCapabilities.MaxHeight))
    {
        return FALSE;
    }

    Attributes = ppdev->PointerAttributes;
    PixelBytes = ppdev->PointerAttributesSize -
                 offsetof(VIDEO_POINTER_ATTRIBUTES, Pixels);
    memset(Attributes->Pixels, 0, PixelBytes);

    for (y = 0; y < SourceHeight; ++y)
    {
        PULONG DestinationRow =
            (PULONG)(Attributes->Pixels + (y * Attributes->WidthInBytes));

        if (psoColor != NULL)
        {
            PULONG SourceRow = (PULONG)VirtGpuSurfaceScan(psoColor, y);

            for (x = 0; x < SourceWidth; ++x)
            {
                ULONG Pixel = SourceRow[x];

                if (!(fl & SPS_ALPHA))
                    Pixel |= 0xFF000000;

                if (!(fl & SPS_ALPHA) &&
                    (psoMask != NULL) &&
                    VirtGpuMonoMaskBit(psoMask, x, y))
                {
                    Pixel &= 0x00FFFFFF;
                }

                DestinationRow[x] = Pixel;
            }
        }
        else
        {
            for (x = 0; x < SourceWidth; ++x)
            {
                BOOL AndMask = VirtGpuMonoMaskBit(psoMask, x, y);
                BOOL XorMask = VirtGpuMonoMaskBit(psoMask, x, y + SourceHeight);

                if (AndMask && !XorMask)
                    DestinationRow[x] = 0x00000000;
                else if (!AndMask && !XorMask)
                    DestinationRow[x] = 0xFF000000;
                else
                    DestinationRow[x] = 0xFFFFFFFF;
            }
        }
    }

    *Width = SourceWidth;
    *Height = SourceHeight;
    return TRUE;
}

BOOL
VirtGpuDispSetPointerShape(
    _Inout_ PPDEV ppdev,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ SURFOBJ *psoColor,
    _In_ LONG xHot,
    _In_ LONG yHot,
    _In_ LONG x,
    _In_ LONG y,
    _In_ FLONG fl)
{
    PVIDEO_POINTER_ATTRIBUTES Attributes;
    ULONG Returned;
    ULONG Width;
    ULONG Height;

    if (!ppdev->PointerSupported)
        return FALSE;

    if ((psoMask == NULL) && (psoColor == NULL))
    {
        VirtGpuDispDisablePointer(ppdev);
        ppdev->PointerShapeValid = FALSE;
        return TRUE;
    }

    if (!VirtGpuBuildPointerPixels(ppdev, psoMask, psoColor, fl, &Width, &Height))
        return FALSE;

    Attributes = ppdev->PointerAttributes;
    Attributes->Flags = VIDEO_MODE_COLOR_POINTER;
    Attributes->Width = Width;
    Attributes->Height = Height;
    Attributes->WidthInBytes =
        ppdev->PointerCapabilities.MaxWidth * sizeof(ULONG);
    Attributes->Enable = (x != -1);
    Attributes->Column = (SHORT)(x - xHot);
    Attributes->Row = (SHORT)(y - yHot);

    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_SET_POINTER_ATTR,
                           Attributes,
                           ppdev->PointerAttributesSize,
                           NULL,
                           0,
                           &Returned) != 0)
    {
        return FALSE;
    }

    ppdev->PointerHotSpot.x = xHot;
    ppdev->PointerHotSpot.y = yHot;
    ppdev->PointerShapeValid = TRUE;
    ppdev->PointerVisible = (x != -1);
    return TRUE;
}

BOOL
VirtGpuDispMovePointer(_Inout_ PPDEV ppdev, _In_ LONG x, _In_ LONG y)
{
    VIDEO_POINTER_POSITION Position;
    ULONG Returned;

    if (!ppdev->PointerSupported || !ppdev->PointerShapeValid)
        return FALSE;

    if (x == -1)
    {
        VirtGpuDispDisablePointer(ppdev);
        return TRUE;
    }

    Position.Column = (SHORT)(x - ppdev->PointerHotSpot.x);
    Position.Row = (SHORT)(y - ppdev->PointerHotSpot.y);

    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_SET_POINTER_POSITION,
                           &Position,
                           sizeof(Position),
                           NULL,
                           0,
                           &Returned) != 0)
    {
        return FALSE;
    }

    if (!ppdev->PointerVisible)
    {
        if (EngDeviceIoControl(ppdev->hDriver,
                               IOCTL_VIDEO_ENABLE_POINTER,
                               NULL,
                               0,
                               NULL,
                               0,
                               &Returned) != 0)
        {
            return FALSE;
        }
    }

    ppdev->PointerVisible = TRUE;
    return TRUE;
}

BOOL
APIENTRY
DrvEnableDriver(
    _In_ ULONG iEngineVersion,
    _In_ ULONG cj,
    _Out_ PDRVENABLEDATA pded)
{
    UNREFERENCED_PARAMETER(iEngineVersion);

    if (cj < sizeof(DRVENABLEDATA))
        return FALSE;

    pded->c = sizeof(VirtGpuDrvFunctions) / sizeof(VirtGpuDrvFunctions[0]);
    pded->pdrvfn = VirtGpuDrvFunctions;
    pded->iDriverVersion = DDI_DRIVER_VERSION_NT5;
    return TRUE;
}

DHPDEV
APIENTRY
DrvEnablePDEV(
    _In_ DEVMODEW *pdm,
    _In_ LPWSTR pwszLogAddress,
    _In_ ULONG cPat,
    _Out_ HSURF *phsurfPatterns,
    _In_ ULONG cjCaps,
    _Out_ ULONG *pdevcaps,
    _In_ ULONG cjDevInfo,
    _Out_ DEVINFO *pdi,
    _In_ HDEV hdev,
    _In_ LPWSTR pwszDeviceName,
    _In_ HANDLE hDriver)
{
    PPDEV ppdev;
    GDIINFO GdiInfo;
    DEVINFO DevInfo;

    UNREFERENCED_PARAMETER(pwszLogAddress);
    UNREFERENCED_PARAMETER(cPat);
    UNREFERENCED_PARAMETER(phsurfPatterns);
    UNREFERENCED_PARAMETER(hdev);
    UNREFERENCED_PARAMETER(pwszDeviceName);

    ppdev = EngAllocMem(FL_ZERO_MEMORY, sizeof(*ppdev), ALLOC_TAG);
    if (ppdev == NULL)
        return NULL;

    ppdev->Signature = VIRTGPU_PDEV_SIGNATURE;
    ppdev->hDriver = hDriver;

    if (!VirtGpuDispInitScreenInfo(ppdev, pdm, &GdiInfo, &DevInfo))
    {
        EngFreeMem(ppdev);
        return NULL;
    }
    VirtGpuDispInit3D(ppdev);

    if (!VirtGpuDispInitPalette(ppdev, &DevInfo))
    {
        EngFreeMem(ppdev);
        return NULL;
    }

    if (!VirtGpuDispInitPointer(ppdev))
    {
        EngDeletePalette(ppdev->DefaultPalette);
        EngFreeMem(ppdev);
        return NULL;
    }

    memcpy(pdi, &DevInfo, min(sizeof(DEVINFO), cjDevInfo));
    memcpy(pdevcaps, &GdiInfo, min(sizeof(GDIINFO), cjCaps));
    return (DHPDEV)ppdev;
}

VOID
APIENTRY
DrvCompletePDEV(_In_ DHPDEV dhpdev, _In_ HDEV hdev)
{
    ((PPDEV)dhpdev)->hDevEng = hdev;
}

VOID
APIENTRY
DrvDisablePDEV(_In_ DHPDEV dhpdev)
{
    PPDEV ppdev = (PPDEV)dhpdev;

    if (ppdev->DefaultPalette != NULL)
        EngDeletePalette(ppdev->DefaultPalette);

    if (ppdev->PointerAttributes != NULL)
        EngFreeMem(ppdev->PointerAttributes);

    ppdev->Signature = 0;
    EngFreeMem(ppdev);
}

HSURF
APIENTRY
DrvEnableSurface(_In_ DHPDEV dhpdev)
{
    PPDEV ppdev = (PPDEV)dhpdev;
    VIDEO_MEMORY VideoMemory;
    VIDEO_MEMORY_INFORMATION VideoMemoryInfo;
    ULONG Returned;
    SIZEL ScreenSize;
    HSURF FrameBufferSurface;
    HSURF DeviceSurface;

    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_SET_CURRENT_MODE,
                           &ppdev->ModeIndex,
                           sizeof(ULONG),
                           NULL,
                           0,
                           &Returned) != 0)
    {
        return NULL;
    }

    VideoMemory.RequestedVirtualAddress = NULL;
    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_MAP_VIDEO_MEMORY,
                           &VideoMemory,
                           sizeof(VideoMemory),
                           &VideoMemoryInfo,
                           sizeof(VideoMemoryInfo),
                           &Returned) != 0)
    {
        return NULL;
    }

    ppdev->ScreenPtr = VideoMemoryInfo.FrameBufferBase;
    ScreenSize.cx = ppdev->ScreenWidth;
    ScreenSize.cy = ppdev->ScreenHeight;

    FrameBufferSurface = (HSURF)EngCreateBitmap(ScreenSize,
                                                ppdev->ScreenDelta,
                                                BMF_32BPP,
                                                (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
                                                ppdev->ScreenPtr);
    if (FrameBufferSurface == NULL)
        return NULL;

    ppdev->psoFrameBuffer = EngLockSurface(FrameBufferSurface);
    if (ppdev->psoFrameBuffer == NULL)
    {
        EngDeleteSurface(FrameBufferSurface);
        return NULL;
    }

    DeviceSurface = (HSURF)EngCreateDeviceSurface((DHSURF)ppdev,
                                                  ScreenSize,
                                                  BMF_32BPP);
    if (DeviceSurface == NULL)
    {
        EngUnlockSurface(ppdev->psoFrameBuffer);
        ppdev->psoFrameBuffer = NULL;
        EngDeleteSurface(FrameBufferSurface);
        return NULL;
    }

    if (!EngAssociateSurface(DeviceSurface, ppdev->hDevEng, VIRTGPU_SURFACE_HOOKS))
    {
        EngDeleteSurface(DeviceSurface);
        EngUnlockSurface(ppdev->psoFrameBuffer);
        ppdev->psoFrameBuffer = NULL;
        EngDeleteSurface(FrameBufferSurface);
        return NULL;
    }

    ppdev->hSurfEng = DeviceSurface;
    ppdev->hSurfFrameBuffer = FrameBufferSurface;
    VirtGpuDispFlushRect(ppdev, NULL);
    return DeviceSurface;
}

VOID
APIENTRY
DrvDisableSurface(_In_ DHPDEV dhpdev)
{
    PPDEV ppdev = (PPDEV)dhpdev;
    VIDEO_MEMORY VideoMemory;
    ULONG Returned;

    VirtGpuDispDisablePointer(ppdev);

    if (ppdev->hSurfEng != NULL)
    {
        EngDeleteSurface(ppdev->hSurfEng);
        ppdev->hSurfEng = NULL;
    }

    if (ppdev->psoFrameBuffer != NULL)
    {
        EngUnlockSurface(ppdev->psoFrameBuffer);
        ppdev->psoFrameBuffer = NULL;
    }

    if (ppdev->hSurfFrameBuffer != NULL)
    {
        EngDeleteSurface(ppdev->hSurfFrameBuffer);
        ppdev->hSurfFrameBuffer = NULL;
    }

    VideoMemory.RequestedVirtualAddress = ppdev->ScreenPtr;
    EngDeviceIoControl(ppdev->hDriver,
                       IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                       &VideoMemory,
                       sizeof(VideoMemory),
                       NULL,
                       0,
                       &Returned);
    ppdev->ScreenPtr = NULL;
}

BOOL
APIENTRY
DrvAssertMode(_In_ DHPDEV dhpdev, _In_ BOOL bEnable)
{
    PPDEV ppdev = (PPDEV)dhpdev;
    ULONG Returned;

    if (bEnable)
    {
        if (EngDeviceIoControl(ppdev->hDriver,
                               IOCTL_VIDEO_SET_CURRENT_MODE,
                               &ppdev->ModeIndex,
                               sizeof(ULONG),
                               NULL,
                               0,
                               &Returned) != 0)
        {
            return FALSE;
        }

        VirtGpuDispFlushRect(ppdev, NULL);
        return TRUE;
    }

    return EngDeviceIoControl(ppdev->hDriver,
                              IOCTL_VIDEO_RESET_DEVICE,
                              NULL,
                              0,
                              NULL,
                              0,
                              &Returned) == 0;
}

ULONG
APIENTRY
DrvSetPointerShape(
    _In_ SURFOBJ *pso,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ SURFOBJ *psoColor,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ LONG xHot,
    _In_ LONG yHot,
    _In_ LONG x,
    _In_ LONG y,
    _Inout_opt_ RECTL *prcl,
    _In_ FLONG fl)
{
    PPDEV ppdev;

    UNREFERENCED_PARAMETER(pxlo);
    UNREFERENCED_PARAMETER(prcl);

    if (pso == NULL)
        return SPS_ERROR;

    ppdev = (PPDEV)pso->dhpdev;
    if (!VirtGpuDispSetPointerShape(ppdev, psoMask, psoColor, xHot, yHot, x, y, fl))
        return SPS_DECLINE;

    return SPS_ACCEPT_NOEXCLUDE;
}

VOID
APIENTRY
DrvMovePointer(
    _In_ SURFOBJ *pso,
    _In_ LONG x,
    _In_ LONG y,
    _Inout_opt_ RECTL *prcl)
{
    UNREFERENCED_PARAMETER(prcl);

    if (pso != NULL)
        VirtGpuDispMovePointer((PPDEV)pso->dhpdev, x, y);
}

VOID
APIENTRY
DrvSynchronize(_In_ DHPDEV dhpdev, _In_opt_ RECTL *prcl)
{
    VirtGpuDispFlushRect((PPDEV)dhpdev, prcl);
}

VOID
APIENTRY
DrvSynchronizeSurface(
    _In_ SURFOBJ *pso,
    _In_opt_ RECTL *prcl,
    _In_ FLONG fl)
{
    PPDEV ppdev;

    UNREFERENCED_PARAMETER(fl);

    ppdev = VirtGpuDispFindPdev(pso, NULL, NULL);
    if ((ppdev != NULL) && VirtGpuDispIsPrimarySurface(ppdev, pso))
        VirtGpuDispFlushRect(ppdev, prcl);
}

static VOID
VirtGpuDispCopySavedBits(
    _Inout_ PUCHAR Destination,
    _In_ ULONG DestinationStride,
    _In_ const UCHAR *Source,
    _In_ ULONG SourceStride,
    _In_ ULONG RowBytes,
    _In_ ULONG Height)
{
    ULONG Row;

    for (Row = 0; Row < Height; ++Row)
    {
        memcpy(Destination, Source, RowBytes);
        Destination += DestinationStride;
        Source += SourceStride;
    }
}

ULONG_PTR
APIENTRY
DrvSaveScreenBits(
    _In_ SURFOBJ *pso,
    _In_ ULONG iMode,
    _In_ ULONG_PTR ident,
    _In_ RECTL *prcl)
{
    PPDEV ppdev;
    PVIRTGPU_SAVED_BITS Saved;
    RECTL Rect;
    RECTL Bounds;
    RECTL Clipped;
    ULONG Width;
    ULONG Height;
    ULONG RowBytes;
    ULONG Size;
    ULONG AllocationSize;
    PUCHAR ScreenBase;
    PUCHAR Source;
    PUCHAR Destination;
    ULONG SourceX;
    ULONG SourceY;

    ppdev = VirtGpuDispFindPdev(pso, NULL, NULL);
    if ((ppdev == NULL) ||
        (ppdev->ScreenPtr == NULL) ||
        !VirtGpuDispIsPrimarySurface(ppdev, pso))
    {
        return 0;
    }

    if (iMode == SS_FREE)
    {
        Saved = (PVIRTGPU_SAVED_BITS)ident;
        if ((Saved == NULL) || (Saved->Signature != VIRTGPU_SAVED_BITS_SIGNATURE))
            return 0;

        Saved->Signature = 0;
        EngFreeMem(Saved);
        return TRUE;
    }

    Bounds.left = 0;
    Bounds.top = 0;
    Bounds.right = ppdev->ScreenWidth;
    Bounds.bottom = ppdev->ScreenHeight;

    if (iMode == SS_SAVE)
    {
        Rect = (prcl != NULL) ? *prcl : Bounds;
        VirtGpuOrderRect(&Rect);
        if (!VirtGpuIntersectRect(&Clipped, &Rect, &Bounds))
            return 0;

        Width = Clipped.right - Clipped.left;
        Height = Clipped.bottom - Clipped.top;
        if (Width > (VIRTGPU_DISP_MAX_ULONG / sizeof(ULONG)))
            return 0;

        RowBytes = Width * sizeof(ULONG);
        if (Height != 0 && RowBytes > (VIRTGPU_DISP_MAX_ULONG / Height))
        {
            return 0;
        }

        Size = RowBytes * Height;
        if (Size > (VIRTGPU_DISP_MAX_ULONG - offsetof(VIRTGPU_SAVED_BITS, Bits)))
            return 0;

        AllocationSize = offsetof(VIRTGPU_SAVED_BITS, Bits) + Size;
        Saved = EngAllocMem(0, AllocationSize, ALLOC_TAG);
        if (Saved == NULL)
            return 0;

        Saved->Signature = VIRTGPU_SAVED_BITS_SIGNATURE;
        Saved->Rect = Clipped;
        Saved->Stride = RowBytes;
        Saved->Size = Size;

        ScreenBase = ppdev->ScreenPtr;
        Source = ScreenBase + (Clipped.top * ppdev->ScreenDelta) +
                 (Clipped.left * sizeof(ULONG));
        VirtGpuDispCopySavedBits(Saved->Bits,
                                 Saved->Stride,
                                 Source,
                                 ppdev->ScreenDelta,
                                 RowBytes,
                                 Height);
        return (ULONG_PTR)Saved;
    }

    if (iMode == SS_RESTORE)
    {
        Saved = (PVIRTGPU_SAVED_BITS)ident;
        if ((Saved == NULL) || (Saved->Signature != VIRTGPU_SAVED_BITS_SIGNATURE))
            return 0;

        if (!VirtGpuIntersectRect(&Clipped, &Saved->Rect, &Bounds))
            return 0;

        Width = Clipped.right - Clipped.left;
        Height = Clipped.bottom - Clipped.top;
        RowBytes = Width * sizeof(ULONG);
        SourceX = Clipped.left - Saved->Rect.left;
        SourceY = Clipped.top - Saved->Rect.top;

        Source = Saved->Bits + (SourceY * Saved->Stride) +
                 (SourceX * sizeof(ULONG));
        Destination = (PUCHAR)ppdev->ScreenPtr +
                      (Clipped.top * ppdev->ScreenDelta) +
                      (Clipped.left * sizeof(ULONG));

        VirtGpuDispCopySavedBits(Destination,
                                 ppdev->ScreenDelta,
                                 Source,
                                 Saved->Stride,
                                 RowBytes,
                                 Height);
        VirtGpuDispFlushRect(ppdev, &Clipped);
        return TRUE;
    }

    return 0;
}

static BOOL
VirtGpuDispCanCreateDeviceBitmap(
    _In_ SIZEL Size,
    _In_ ULONG Format)
{
    if ((Size.cx <= 0) || (Size.cy <= 0))
        return FALSE;

    switch (Format)
    {
        case BMF_1BPP:
        case BMF_4BPP:
        case BMF_8BPP:
        case BMF_16BPP:
        case BMF_24BPP:
        case BMF_32BPP:
            return TRUE;

        default:
            return FALSE;
    }
}

HBITMAP
APIENTRY
DrvCreateDeviceBitmap(
    _In_ DHPDEV dhpdev,
    _In_ SIZEL sizl,
    _In_ ULONG iFormat)
{
    PPDEV ppdev = (PPDEV)dhpdev;
    PVIRTGPU_DEVICE_BITMAP DeviceBitmap;
    HBITMAP hbmDevice = NULL;

    if ((ppdev == NULL) ||
        (ppdev->Signature != VIRTGPU_PDEV_SIGNATURE) ||
        (ppdev->hDevEng == NULL) ||
        !VirtGpuDispCanCreateDeviceBitmap(sizl, iFormat))
    {
        return NULL;
    }

    DeviceBitmap = EngAllocMem(FL_ZERO_MEMORY, sizeof(*DeviceBitmap), ALLOC_TAG);
    if (DeviceBitmap == NULL)
        return NULL;

    DeviceBitmap->Signature = VIRTGPU_DEVICE_BITMAP_SIGNATURE;
    DeviceBitmap->ppdev = ppdev;
    DeviceBitmap->Size = sizl;
    DeviceBitmap->Format = iFormat;

    DeviceBitmap->hSurfBacking = (HSURF)EngCreateBitmap(sizl,
                                                        0,
                                                        iFormat,
                                                        BMF_TOPDOWN,
                                                        NULL);
    if (DeviceBitmap->hSurfBacking == NULL)
        goto Failure;

    DeviceBitmap->psoBacking = EngLockSurface(DeviceBitmap->hSurfBacking);
    if (DeviceBitmap->psoBacking == NULL)
        goto Failure;

    hbmDevice = EngCreateDeviceBitmap((DHSURF)DeviceBitmap, sizl, iFormat);
    if (hbmDevice == NULL)
        goto Failure;

    if (!EngAssociateSurface((HSURF)hbmDevice, ppdev->hDevEng, VIRTGPU_SURFACE_HOOKS))
        goto Failure;

    return hbmDevice;

Failure:
    if (hbmDevice != NULL)
        EngDeleteSurface((HSURF)hbmDevice);

    if (DeviceBitmap->psoBacking != NULL)
        EngUnlockSurface(DeviceBitmap->psoBacking);

    if (DeviceBitmap->hSurfBacking != NULL)
        EngDeleteSurface(DeviceBitmap->hSurfBacking);

    DeviceBitmap->Signature = 0;
    EngFreeMem(DeviceBitmap);
    return NULL;
}

VOID
APIENTRY
DrvDeleteDeviceBitmap(_In_ DHSURF dhsurf)
{
    PVIRTGPU_DEVICE_BITMAP DeviceBitmap = (PVIRTGPU_DEVICE_BITMAP)dhsurf;

    if ((DeviceBitmap == NULL) ||
        (DeviceBitmap->Signature != VIRTGPU_DEVICE_BITMAP_SIGNATURE))
    {
        return;
    }

    DeviceBitmap->Signature = 0;

    if (DeviceBitmap->psoBacking != NULL)
        EngUnlockSurface(DeviceBitmap->psoBacking);

    if (DeviceBitmap->hSurfBacking != NULL)
        EngDeleteSurface(DeviceBitmap->hSurfBacking);

    EngFreeMem(DeviceBitmap);
}

static VOID
VirtGpuDispFillDirectDrawPixelFormat(
    _In_ PPDEV ppdev,
    _Out_ DDPIXELFORMAT *PixelFormat)
{
    memset(PixelFormat, 0, sizeof(*PixelFormat));
    PixelFormat->dwSize = sizeof(*PixelFormat);
    PixelFormat->dwFlags = DDPF_RGB;
    PixelFormat->dwRGBBitCount = ppdev->BitsPerPixel;
    PixelFormat->dwRBitMask = ppdev->RedMask;
    PixelFormat->dwGBitMask = ppdev->GreenMask;
    PixelFormat->dwBBitMask = ppdev->BlueMask;

    if (ppdev->BitsPerPixel == 8)
        PixelFormat->dwFlags |= DDPF_PALETTEINDEXED8;
}

BOOL
APIENTRY
DrvGetDirectDrawInfo(
    _In_ DHPDEV dhpdev,
    _Out_ DD_HALINFO *pHalInfo,
    _Out_ DWORD *pdwNumHeaps,
    _Out_opt_ VIDEOMEMORY *pvmList,
    _Out_ DWORD *pdwNumFourCCCodes,
    _Out_opt_ DWORD *pdwFourCC)
{
    PPDEV ppdev = (PPDEV)dhpdev;

    UNREFERENCED_PARAMETER(pvmList);
    UNREFERENCED_PARAMETER(pdwFourCC);

    if ((ppdev == NULL) ||
        (ppdev->Signature != VIRTGPU_PDEV_SIGNATURE) ||
        (pHalInfo == NULL) ||
        (pdwNumHeaps == NULL) ||
        (pdwNumFourCCCodes == NULL))
    {
        return FALSE;
    }

    *pdwNumHeaps = 0;
    *pdwNumFourCCCodes = 0;

    memset(pHalInfo, 0, sizeof(*pHalInfo));
    pHalInfo->dwSize = sizeof(*pHalInfo);
    pHalInfo->dwFlags = DDHALINFO_ISPRIMARYDISPLAY;
    pHalInfo->vmiData.pvPrimary = ppdev->ScreenPtr;
    pHalInfo->vmiData.fpPrimary = 0;
    pHalInfo->vmiData.dwDisplayWidth = ppdev->ScreenWidth;
    pHalInfo->vmiData.dwDisplayHeight = ppdev->ScreenHeight;
    pHalInfo->vmiData.lDisplayPitch = ppdev->ScreenDelta;
    pHalInfo->vmiData.dwOffscreenAlign = 4;
    VirtGpuDispFillDirectDrawPixelFormat(ppdev, &pHalInfo->vmiData.ddpfDisplay);

    pHalInfo->ddCaps.dwSize = sizeof(pHalInfo->ddCaps);
    pHalInfo->ddCaps.dwCaps = DDCAPS_GDI |
                               DDCAPS_CANBLTSYSMEM |
                               DDCAPS_NOHARDWARE;
    pHalInfo->ddCaps.dwVidMemTotal = ppdev->ScreenDelta * ppdev->ScreenHeight;
    pHalInfo->ddCaps.dwVidMemFree = 0;
    pHalInfo->ddCaps.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE |
                                       DDSCAPS_OFFSCREENPLAIN |
                                       DDSCAPS_SYSTEMMEMORY;
    return TRUE;
}

BOOL
APIENTRY
DrvEnableDirectDraw(
    _In_ DHPDEV dhpdev,
    _Out_opt_ DD_CALLBACKS *pCallBacks,
    _Out_opt_ DD_SURFACECALLBACKS *pSurfaceCallBacks,
    _Out_opt_ DD_PALETTECALLBACKS *pPaletteCallBacks)
{
    PPDEV ppdev = (PPDEV)dhpdev;

    if ((ppdev == NULL) || (ppdev->Signature != VIRTGPU_PDEV_SIGNATURE))
        return FALSE;

    if (pCallBacks != NULL)
    {
        memset(pCallBacks, 0, sizeof(*pCallBacks));
        pCallBacks->dwSize = sizeof(*pCallBacks);
    }

    if (pSurfaceCallBacks != NULL)
    {
        memset(pSurfaceCallBacks, 0, sizeof(*pSurfaceCallBacks));
        pSurfaceCallBacks->dwSize = sizeof(*pSurfaceCallBacks);
    }

    if (pPaletteCallBacks != NULL)
    {
        memset(pPaletteCallBacks, 0, sizeof(*pPaletteCallBacks));
        pPaletteCallBacks->dwSize = sizeof(*pPaletteCallBacks);
    }

    ppdev->DirectDrawEnabled = TRUE;
    return TRUE;
}

VOID
APIENTRY
DrvDisableDirectDraw(_In_ DHPDEV dhpdev)
{
    PPDEV ppdev = (PPDEV)dhpdev;

    if ((ppdev != NULL) && (ppdev->Signature == VIRTGPU_PDEV_SIGNATURE))
        ppdev->DirectDrawEnabled = FALSE;
}

static BOOL
VirtGpuDispIs3DIoControl(_In_ ULONG IoControlCode)
{
    switch (IoControlCode)
    {
        case IOCTL_VIDEO_VIRTGPU_QUERY_3D_CAPS:
        case IOCTL_VIDEO_VIRTGPU_GET_CAPSET:
        case IOCTL_VIDEO_VIRTGPU_3D_CREATE_CONTEXT:
        case IOCTL_VIDEO_VIRTGPU_3D_DESTROY_CONTEXT:
        case IOCTL_VIDEO_VIRTGPU_3D_CREATE_RESOURCE:
        case IOCTL_VIDEO_VIRTGPU_3D_DESTROY_RESOURCE:
        case IOCTL_VIDEO_VIRTGPU_3D_TRANSFER_TO_HOST:
        case IOCTL_VIDEO_VIRTGPU_3D_TRANSFER_FROM_HOST:
        case IOCTL_VIDEO_VIRTGPU_3D_SUBMIT:
        case IOCTL_VIDEO_VIRTGPU_3D_ATTACH_RESOURCE:
        case IOCTL_VIDEO_VIRTGPU_3D_DETACH_RESOURCE:
        case IOCTL_VIDEO_VIRTGPU_3D_WAIT_FENCE:
        case IOCTL_VIDEO_VIRTGPU_3D_CREATE_BLOB:
        case IOCTL_VIDEO_VIRTGPU_3D_ASSIGN_UUID:
        case IOCTL_VIDEO_VIRTGPU_3D_MAP_BLOB:
        case IOCTL_VIDEO_VIRTGPU_3D_UNMAP_BLOB:
            return TRUE;

        default:
            return FALSE;
    }
}

static ULONG
VirtGpuDispEscape3DIoControl(
    _Inout_ PPDEV ppdev,
    _In_ ULONG cjIn,
    _In_reads_bytes_(cjIn) PVOID pvIn,
    _In_ ULONG cjOut,
    _Out_writes_bytes_(cjOut) PVOID pvOut)
{
    PVIRTGPU_ESCAPE_3D_IOCTL Escape;
    ULONG HeaderSize = offsetof(VIRTGPU_ESCAPE_3D_IOCTL, Data);
    ULONG Returned = 0;
    PVOID InputBuffer;

    if ((pvIn == NULL) || (cjIn < HeaderSize))
        return 0;

    Escape = pvIn;
    if ((Escape->Size < HeaderSize) ||
        (Escape->Size > cjIn) ||
        (Escape->InputSize > Escape->Size - HeaderSize) ||
        !VirtGpuDispIs3DIoControl(Escape->IoControlCode))
    {
        return 0;
    }

    if ((Escape->IoControlCode != IOCTL_VIDEO_VIRTGPU_QUERY_3D_CAPS) &&
        !ppdev->ThreeDEnabled &&
        !ppdev->ResourceBlobSupported)
    {
        return 0;
    }

    InputBuffer = Escape->InputSize != 0 ? Escape->Data : NULL;
    if (EngDeviceIoControl(ppdev->hDriver,
                           Escape->IoControlCode,
                           InputBuffer,
                           Escape->InputSize,
                           pvOut,
                           cjOut,
                           &Returned) != 0)
    {
        return 0;
    }

    return Returned != 0 ? Returned : 1;
}

ULONG
APIENTRY
DrvEscape(
    _In_ SURFOBJ *pso,
    _In_ ULONG iEsc,
    _In_ ULONG cjIn,
    _In_reads_bytes_(cjIn) PVOID pvIn,
    _In_ ULONG cjOut,
    _Out_writes_bytes_(cjOut) PVOID pvOut)
{
    static const WCHAR IcdName[] = L"VirtGpu";
    PPDEV ppdev;
    BOOL OpenGlSupported;

    ppdev = VirtGpuDispFindPdev(pso, NULL, NULL);
    if (ppdev == NULL)
        return 0;

    OpenGlSupported =
        ppdev->ThreeDEnabled && (ppdev->PreferredCapsetId != 0);

    if (iEsc == QUERYESCSUPPORT)
    {
        ULONG RequestedEscape;

        if ((pvIn == NULL) || (cjIn < sizeof(RequestedEscape)))
            return 0;

        RequestedEscape = *(PULONG)pvIn;
        if (RequestedEscape == OPENGL_GETINFO)
            return OpenGlSupported ? 1 : 0;
        if (RequestedEscape == VIRTGPU_ESCAPE_3D_IOCTL_CODE)
            return 1;
        return 0;
    }

    if (iEsc == VIRTGPU_ESCAPE_3D_IOCTL_CODE)
    {
        return VirtGpuDispEscape3DIoControl(ppdev,
                                            cjIn,
                                            pvIn,
                                            cjOut,
                                            pvOut);
    }

    if (!OpenGlSupported)
        return 0;

    if (iEsc == OPENGL_GETINFO)
    {
        ULONG Query;
        PVIRTGPU_OPENGL_INFO Info;

        if ((pvIn == NULL) ||
            (pvOut == NULL) ||
            (cjIn < sizeof(Query)) ||
            (cjOut < sizeof(*Info)))
        {
            return 0;
        }

        Query = *(PULONG)pvIn;
        if (Query != OPENGL_GETINFO_DRVNAME)
            return 0;

        Info = pvOut;
        memset(Info, 0, sizeof(*Info));
        Info->Version = VIRTGPU_OPENGL_ICD_VERSION;
        Info->DriverVersion = VIRTGPU_OPENGL_ICD_DRIVER_VERSION;
        memcpy(Info->DriverName, IcdName, sizeof(IcdName));
        return sizeof(*Info);
    }

    return 0;
}

BOOL
APIENTRY
DrvBitBlt(
    _Inout_ SURFOBJ *psoTrg,
    _In_opt_ SURFOBJ *psoSrc,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclTrg,
    _In_opt_ POINTL *pptlSrc,
    _In_opt_ POINTL *pptlMask,
    _In_opt_ BRUSHOBJ *pbo,
    _In_opt_ POINTL *pptlBrush,
    _In_ ROP4 rop4)
{
    PPDEV ppdev;
    SURFOBJ *psoEngTrg = psoTrg;
    SURFOBJ *psoEngSrc = psoSrc;
    SURFOBJ *psoEngMask = psoMask;
    BOOL FlushTarget = FALSE;
    BOOL Result;

    ppdev = VirtGpuDispFindPdev(psoTrg, psoSrc, psoMask);
    if (ppdev != NULL)
    {
        FlushTarget = VirtGpuDispIsPrimarySurface(ppdev, psoTrg);
        psoEngTrg = VirtGpuDispMapSurface(ppdev, psoTrg);
        psoEngSrc = VirtGpuDispMapSurface(ppdev, psoSrc);
        psoEngMask = VirtGpuDispMapSurface(ppdev, psoMask);
    }

    Result = EngBitBlt(psoEngTrg, psoEngSrc, psoEngMask, pco, pxlo, prclTrg,
                       pptlSrc, pptlMask, pbo, pptlBrush, rop4);
    if (Result && FlushTarget)
        VirtGpuDispFlushSurfaceRect(psoTrg, pco, prclTrg);
    return Result;
}

BOOL
APIENTRY
DrvCopyBits(
    _Inout_ SURFOBJ *psoDest,
    _In_ SURFOBJ *psoSrc,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclDest,
    _In_ POINTL *pptlSrc)
{
    PPDEV ppdev;
    SURFOBJ *psoEngDest = psoDest;
    SURFOBJ *psoEngSrc = psoSrc;
    BOOL FlushTarget = FALSE;
    BOOL Result;

    ppdev = VirtGpuDispFindPdev(psoDest, psoSrc, NULL);
    if (ppdev != NULL)
    {
        FlushTarget = VirtGpuDispIsPrimarySurface(ppdev, psoDest);
        psoEngDest = VirtGpuDispMapSurface(ppdev, psoDest);
        psoEngSrc = VirtGpuDispMapSurface(ppdev, psoSrc);
    }

    Result = EngCopyBits(psoEngDest, psoEngSrc, pco, pxlo, prclDest, pptlSrc);
    if (Result && FlushTarget)
        VirtGpuDispFlushSurfaceRect(psoDest, pco, prclDest);
    return Result;
}

BOOL
APIENTRY
DrvLineTo(
    _Inout_ SURFOBJ *pso,
    _In_opt_ CLIPOBJ *pco,
    _In_ BRUSHOBJ *pbo,
    _In_ LONG x1,
    _In_ LONG y1,
    _In_ LONG x2,
    _In_ LONG y2,
    _In_opt_ RECTL *prclBounds,
    _In_ MIX mix)
{
    RECTL Bounds;
    PPDEV ppdev;
    SURFOBJ *psoEng = pso;
    BOOL FlushTarget = FALSE;
    BOOL Result;

    ppdev = VirtGpuDispFindPdev(pso, NULL, NULL);
    if (ppdev != NULL)
    {
        FlushTarget = VirtGpuDispIsPrimarySurface(ppdev, pso);
        psoEng = VirtGpuDispMapSurface(ppdev, pso);
    }

    Result = EngLineTo(psoEng, pco, pbo, x1, y1, x2, y2, prclBounds, mix);

    if (Result && FlushTarget)
    {
        if (prclBounds != NULL)
        {
            Bounds = *prclBounds;
        }
        else
        {
            Bounds.left = min(x1, x2) - 1;
            Bounds.top = min(y1, y2) - 1;
            Bounds.right = max(x1, x2) + 2;
            Bounds.bottom = max(y1, y2) + 2;
        }

        VirtGpuDispFlushSurfaceRect(pso, pco, &Bounds);
    }

    return Result;
}

BOOL
APIENTRY
DrvPaint(
    _Inout_ SURFOBJ *pso,
    _In_opt_ CLIPOBJ *pco,
    _In_ BRUSHOBJ *pbo,
    _In_opt_ POINTL *pptlBrushOrg,
    _In_ MIX mix)
{
    PPDEV ppdev;
    SURFOBJ *psoEng = pso;
    BOOL FlushTarget = FALSE;
    BOOL Result;

    ppdev = VirtGpuDispFindPdev(pso, NULL, NULL);
    if (ppdev != NULL)
    {
        FlushTarget = VirtGpuDispIsPrimarySurface(ppdev, pso);
        psoEng = VirtGpuDispMapSurface(ppdev, pso);
    }

    Result = EngPaint(psoEng, pco, pbo, pptlBrushOrg, mix);
    if (Result && FlushTarget)
        VirtGpuDispFlushSurfaceRect(pso, pco, pco ? &pco->rclBounds : NULL);
    return Result;
}

BOOL
APIENTRY
DrvStretchBlt(
    _Inout_ SURFOBJ *psoDest,
    _Inout_ SURFOBJ *psoSrc,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_opt_ COLORADJUSTMENT *pca,
    _In_ POINTL *pptlHTOrg,
    _In_ RECTL *prclDest,
    _In_ RECTL *prclSrc,
    _When_(psoMask, _In_) POINTL *pptlMask,
    _In_ ULONG iMode)
{
    PPDEV ppdev;
    SURFOBJ *psoEngDest = psoDest;
    SURFOBJ *psoEngSrc = psoSrc;
    SURFOBJ *psoEngMask = psoMask;
    BOOL FlushTarget = FALSE;
    BOOL Result;

    ppdev = VirtGpuDispFindPdev(psoDest, psoSrc, psoMask);
    if (ppdev != NULL)
    {
        FlushTarget = VirtGpuDispIsPrimarySurface(ppdev, psoDest);
        psoEngDest = VirtGpuDispMapSurface(ppdev, psoDest);
        psoEngSrc = VirtGpuDispMapSurface(ppdev, psoSrc);
        psoEngMask = VirtGpuDispMapSurface(ppdev, psoMask);
    }

    Result = EngStretchBlt(psoEngDest, psoEngSrc, psoEngMask, pco, pxlo, pca,
                           pptlHTOrg, prclDest, prclSrc, pptlMask, iMode);
    if (Result && FlushTarget)
        VirtGpuDispFlushSurfaceRect(psoDest, pco, prclDest);
    return Result;
}

BOOL
APIENTRY
DrvStretchBltROP(
    _Inout_ SURFOBJ *psoDest,
    _Inout_ SURFOBJ *psoSrc,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_opt_ COLORADJUSTMENT *pca,
    _In_ POINTL *pptlHTOrg,
    _In_ RECTL *prclDest,
    _In_ RECTL *prclSrc,
    _When_(psoMask, _In_) POINTL *pptlMask,
    _In_ ULONG iMode,
    _In_opt_ BRUSHOBJ *pbo,
    _In_ DWORD rop4)
{
    PPDEV ppdev;
    SURFOBJ *psoEngDest = psoDest;
    SURFOBJ *psoEngSrc = psoSrc;
    SURFOBJ *psoEngMask = psoMask;
    BOOL FlushTarget = FALSE;
    BOOL Result;

    ppdev = VirtGpuDispFindPdev(psoDest, psoSrc, psoMask);
    if (ppdev != NULL)
    {
        FlushTarget = VirtGpuDispIsPrimarySurface(ppdev, psoDest);
        psoEngDest = VirtGpuDispMapSurface(ppdev, psoDest);
        psoEngSrc = VirtGpuDispMapSurface(ppdev, psoSrc);
        psoEngMask = VirtGpuDispMapSurface(ppdev, psoMask);
    }

    Result = EngStretchBltROP(psoEngDest, psoEngSrc, psoEngMask, pco, pxlo,
                              pca, pptlHTOrg, prclDest, prclSrc, pptlMask,
                              iMode, pbo, rop4);
    if (Result && FlushTarget)
        VirtGpuDispFlushSurfaceRect(psoDest, pco, prclDest);
    return Result;
}

BOOL
APIENTRY
DrvAlphaBlend(
    _Inout_ SURFOBJ *psoDest,
    _In_ SURFOBJ *psoSrc,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclDest,
    _In_ RECTL *prclSrc,
    _In_ BLENDOBJ *pBlendObj)
{
    PPDEV ppdev;
    SURFOBJ *psoEngDest = psoDest;
    SURFOBJ *psoEngSrc = psoSrc;
    BOOL FlushTarget = FALSE;
    BOOL Result;

    ppdev = VirtGpuDispFindPdev(psoDest, psoSrc, NULL);
    if (ppdev != NULL)
    {
        FlushTarget = VirtGpuDispIsPrimarySurface(ppdev, psoDest);
        psoEngDest = VirtGpuDispMapSurface(ppdev, psoDest);
        psoEngSrc = VirtGpuDispMapSurface(ppdev, psoSrc);
    }

    Result = EngAlphaBlend(psoEngDest, psoEngSrc, pco, pxlo,
                           prclDest, prclSrc, pBlendObj);
    if (Result && FlushTarget)
        VirtGpuDispFlushSurfaceRect(psoDest, pco, prclDest);
    return Result;
}

BOOL
APIENTRY
DrvTransparentBlt(
    _Inout_ SURFOBJ *psoDst,
    _In_ SURFOBJ *psoSrc,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclDst,
    _In_ RECTL *prclSrc,
    _In_ ULONG iTransColor,
    _In_ ULONG ulReserved)
{
    PPDEV ppdev;
    SURFOBJ *psoEngDst = psoDst;
    SURFOBJ *psoEngSrc = psoSrc;
    BOOL FlushTarget = FALSE;
    BOOL Result;

    ppdev = VirtGpuDispFindPdev(psoDst, psoSrc, NULL);
    if (ppdev != NULL)
    {
        FlushTarget = VirtGpuDispIsPrimarySurface(ppdev, psoDst);
        psoEngDst = VirtGpuDispMapSurface(ppdev, psoDst);
        psoEngSrc = VirtGpuDispMapSurface(ppdev, psoSrc);
    }

    Result = EngTransparentBlt(psoEngDst, psoEngSrc, pco, pxlo,
                               prclDst, prclSrc, iTransColor, ulReserved);
    if (Result && FlushTarget)
        VirtGpuDispFlushSurfaceRect(psoDst, pco, prclDst);
    return Result;
}

BOOL
APIENTRY
DrvGradientFill(
    _Inout_ SURFOBJ *psoDest,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ TRIVERTEX *pVertex,
    _In_ ULONG nVertex,
    _In_ PVOID pMesh,
    _In_ ULONG nMesh,
    _In_ RECTL *prclExtents,
    _In_ POINTL *pptlDitherOrg,
    _In_ ULONG ulMode)
{
    PPDEV ppdev;
    SURFOBJ *psoEngDest = psoDest;
    BOOL FlushTarget = FALSE;
    BOOL Result;

    ppdev = VirtGpuDispFindPdev(psoDest, NULL, NULL);
    if (ppdev != NULL)
    {
        FlushTarget = VirtGpuDispIsPrimarySurface(ppdev, psoDest);
        psoEngDest = VirtGpuDispMapSurface(ppdev, psoDest);
    }

    Result = EngGradientFill(psoEngDest, pco, pxlo, pVertex, nVertex,
                             pMesh, nMesh, prclExtents,
                             pptlDitherOrg, ulMode);
    if (Result && FlushTarget)
        VirtGpuDispFlushSurfaceRect(psoDest, pco, prclExtents);
    return Result;
}
