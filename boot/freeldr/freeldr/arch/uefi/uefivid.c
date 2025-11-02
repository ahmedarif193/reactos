/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video output
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>
#include <Cpu.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/* Forward declarations for EDID helpers */
static BOOLEAN UefiParseEdidPreferred(const UINT8* Edid, UINT32 Size, UINT32* OutW, UINT32* OutH);
static BOOLEAN UefiGetPreferredResolutionFromEdid(UINT32* OutW, UINT32* OutH);
static BOOLEAN UefiSelectBestModeByAspect(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop,
                                          UINT32 targetW,
                                          UINT32 targetH);
static BOOLEAN UefiTrySetExactMode(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop,
                                   UINT32 w,
                                   UINT32 h);

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16
#define TOP_BOTTOM_LINES 0
#define LOWEST_SUPPORTED_RES 1

/* Preferred resolution bounds used when selecting a fallback mode */
#define PREFERRED_WIDTH_MIN  800
#define PREFERRED_WIDTH_MAX  1920
#define PREFERRED_HEIGHT_MIN 600
#define PREFERRED_HEIGHT_MAX 1200

/* GLOBALS ********************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern UCHAR BitmapFont8x16[256 * 16];

UCHAR MachDefaultTextColor = COLOR_GRAY;
REACTOS_INTERNAL_BGCONTEXT framebufferData;
EFI_GUID EfiGraphicsOutputProtocol = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

/* Minimal EDID protocol definitions (declared locally if headers are absent) */
typedef struct _EFI_EDID_COMMON_PROTOCOL {
    UINT32 SizeOfEdid;
    UINT8* Edid;
} EFI_EDID_COMMON_PROTOCOL;

/* GUIDs for EDID protocols */
static EFI_GUID EfiEdidDiscoveredProtocolGuid =
    { 0x1c0c34f6, 0xd380, 0x41fa, {0xa0, 0x49, 0x8a, 0xd0, 0x6c, 0x1a, 0x66, 0xaa} };
static EFI_GUID EfiEdidActiveProtocolGuid =
    { 0xbd8c1056, 0x9f36, 0x44ec, {0x92, 0xa8, 0xa6, 0x33, 0x7f, 0x81, 0x79, 0x86} };

static UINT32 ConsoleX = 0;
static UINT32 ConsoleY = 0;
static UINT32 MaxConsoleX = 0;
static UINT32 MaxConsoleY = 0;
static BOOLEAN GopConsoleInitialized = FALSE;

/* GOP mode enumeration (exported to winldr) */
PLOADER_PARAMETER_GOP_MODE UefiGopModes = NULL;
ULONG UefiGopModeCount = 0;
ULONG UefiGopPreferredMode = 0;
/* Forward declarations ******************************************************/
static VOID UefiVideoConfigureFramebufferCache(VOID);

static VOID
UefiVideoConfigureFramebufferCache(VOID)
{
    EFI_STATUS Status;
    EFI_CPU_ARCH_PROTOCOL *CpuProtocol;
    EFI_GUID CpuProtocolGuid = EFI_CPU_ARCH_PROTOCOL_GUID;
    EFI_PHYSICAL_ADDRESS FramebufferBase;
    EFI_PHYSICAL_ADDRESS AttributeBase;
    UINT64 AttributeLength;
    UINT64 EndOffset;

    if (framebufferData.BaseAddress == 0 || framebufferData.BufferSize == 0)
        return;

    if (GlobalSystemTable == NULL || GlobalSystemTable->BootServices == NULL)
        return;

    FramebufferBase = (EFI_PHYSICAL_ADDRESS)framebufferData.BaseAddress;
    AttributeBase = FramebufferBase & ~((EFI_PHYSICAL_ADDRESS)EFI_PAGE_SIZE - 1);
    EndOffset = (FramebufferBase - AttributeBase) + framebufferData.BufferSize;
    AttributeLength = (EndOffset + EFI_PAGE_SIZE - 1) & ~((UINT64)EFI_PAGE_SIZE - 1);

    Status = GlobalSystemTable->BootServices->LocateProtocol(&CpuProtocolGuid,
                                                             NULL,
                                                             (VOID **)&CpuProtocol);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI GOP: CPU arch protocol unavailable (Status %lx)\n", Status);
        return;
    }

    if (CpuProtocol->SetMemoryAttributes == NULL)
    {
        TRACE("UEFI GOP: CPU arch protocol lacks SetMemoryAttributes\n");
        return;
    }

    Status = CpuProtocol->SetMemoryAttributes(CpuProtocol,
                                              AttributeBase,
                                              AttributeLength,
                                              EFI_MEMORY_WC);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI GOP: Failed to set framebuffer WC attributes (Status %lx)\n",
              Status);

        /* Try a write-back fallback for any failure code */
        {
            EFI_STATUS FallbackStatus;

            FallbackStatus = CpuProtocol->SetMemoryAttributes(CpuProtocol,
                                                               AttributeBase,
                                                               AttributeLength,
                                                               EFI_MEMORY_WB);
            if (EFI_ERROR(FallbackStatus))
            {
                TRACE("UEFI GOP: Write-back fallback for framebuffer failed (Status %lx)\n",
                      FallbackStatus);
            }
            else
            {
                TRACE("UEFI GOP: Framebuffer caching set to write-back fallback\n");
            }
        }
        return;
    }

    TRACE("UEFI GOP: Framebuffer mapped with write-combining cache attribute\n");
}

/* FUNCTIONS ******************************************************************/


EFI_STATUS
UefiInitializeVideo(VOID)
{
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* CurrentInfo;
    BOOLEAN ModeChosen = FALSE;

    RtlZeroMemory(&framebufferData, sizeof(framebufferData));
    Status = GlobalSystemTable->BootServices->LocateProtocol(&EfiGraphicsOutputProtocol, 0, (void**)&gop);
    if (Status != EFI_SUCCESS)
    {
        TRACE("Failed to find GOP with status %d\n", Status);
        return Status;
    }

    TRACE("UEFI GOP: Protocol located successfully\n");
    TRACE("  MaxMode: %d\n", gop->Mode->MaxMode);
    TRACE("  Current Mode: %d\n", gop->Mode->Mode);

    /*
     * Temporary: Force a static GOP resolution to make text readable on
     * high-DPI firmware defaults. We use 1280x800 (16:10) to match common
     * panels and keep aspect with 2560x1600 displays. This is a stop-gap;
     * we can instead inherit the UEFI-provided mode (current firmware mode)
     * or use EDID to select a native/preferred mode once font scaling and
     * UI are in place.
     */
    if (UefiTrySetExactMode(gop, 1280, 800))
    {
        ModeChosen = TRUE;
    }

    /* Enumerate all GOP modes, cache minimal descriptors for the kernel */
    if (gop->Mode->MaxMode > 0)
    {
        UINT32 i;
        UefiGopModeCount = (ULONG)gop->Mode->MaxMode;
        UefiGopPreferredMode = (ULONG)gop->Mode->Mode;
        UefiGopModes = FrLdrHeapAlloc(sizeof(LOADER_PARAMETER_GOP_MODE) * UefiGopModeCount, 'MPOG');
        if (UefiGopModes)
        {
            RtlZeroMemory(UefiGopModes, sizeof(LOADER_PARAMETER_GOP_MODE) * UefiGopModeCount);
            for (i = 0; i < UefiGopModeCount; ++i)
            {
                EFI_STATUS ModeStatus;
                EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info = NULL;
                UINTN InfoSize = 0;
                ModeStatus = gop->QueryMode(gop, i, &InfoSize, &Info);
                if (!EFI_ERROR(ModeStatus) && Info)
                {
                    UefiGopModes[i].HorizontalResolution = Info->HorizontalResolution;
                    UefiGopModes[i].VerticalResolution = Info->VerticalResolution;
                    UefiGopModes[i].PixelsPerScanLine = Info->PixelsPerScanLine;
                    UefiGopModes[i].PixelFormat = Info->PixelFormat;
                    if (Info->PixelFormat == PixelBitMask)
                    {
                        UefiGopModes[i].RedMask   = Info->PixelInformation.RedMask;
                        UefiGopModes[i].GreenMask = Info->PixelInformation.GreenMask;
                        UefiGopModes[i].BlueMask  = Info->PixelInformation.BlueMask;
                    }
                    else if (Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
                    {
                        UefiGopModes[i].RedMask = 0x000000FF;
                        UefiGopModes[i].GreenMask = 0x0000FF00;
                        UefiGopModes[i].BlueMask = 0x00FF0000;
                    }
                    else if (Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
                    {
                        UefiGopModes[i].RedMask = 0x00FF0000;
                        UefiGopModes[i].GreenMask = 0x0000FF00;
                        UefiGopModes[i].BlueMask = 0x000000FF;
                    }
                }
            }
        }
        else
        {
            UefiGopModeCount = 0;
        }
    }
    
    /* Try EDID-based preferred/native selection; else use aspect-matched best */
    if (!ModeChosen)
    {
        UINT32 prefW = 0, prefH = 0;
        if (UefiGetPreferredResolutionFromEdid(&prefW, &prefH))
        {
            TRACE("UEFI EDID: Preferred/native %ux%u\n", prefW, prefH);
            (void)UefiSelectBestModeByAspect(gop, prefW, prefH);
        }
        else
        {
            /* No EDID: use current firmware aspect to pick the highest resolution with closest aspect */
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* CurInfo = gop->Mode->Info;
            if (CurInfo && CurInfo->HorizontalResolution && CurInfo->VerticalResolution)
            {
                (void)UefiSelectBestModeByAspect(gop, CurInfo->HorizontalResolution, CurInfo->VerticalResolution);
            }
        }
    }

    /* Refresh current mode info after potential SetMode */
    CurrentInfo = gop->Mode->Info;
    if (!CurrentInfo)
    {
        UINTN InfoSize = 0;
        EFI_STATUS ModeStatus = gop->QueryMode(gop, gop->Mode->Mode, &InfoSize, &CurrentInfo);
        if (EFI_ERROR(ModeStatus))
        {
            TRACE("UEFI GOP: QueryMode failed (%d); aborting video init\n", ModeStatus);
            return ModeStatus;
        }
    }

    framebufferData.BaseAddress        = (ULONG_PTR)gop->Mode->FrameBufferBase;
    framebufferData.BufferSize         = gop->Mode->FrameBufferSize;
    framebufferData.ScreenWidth        = gop->Mode->Info->HorizontalResolution;
    framebufferData.ScreenHeight       = gop->Mode->Info->VerticalResolution;
    framebufferData.PixelsPerScanLine  = gop->Mode->Info->PixelsPerScanLine;
    framebufferData.PixelFormat        = gop->Mode->Info->PixelFormat;
    framebufferData.RedMask            = 0;
    framebufferData.GreenMask          = 0;
    framebufferData.BlueMask           = 0;
    framebufferData.ReservedMask       = 0;

    switch (gop->Mode->Info->PixelFormat)
    {
        case PixelRedGreenBlueReserved8BitPerColor:
            framebufferData.RedMask      = 0x000000FF;
            framebufferData.GreenMask    = 0x0000FF00;
            framebufferData.BlueMask     = 0x00FF0000;
            framebufferData.ReservedMask = 0xFF000000;
            break;

        case PixelBlueGreenRedReserved8BitPerColor:
            framebufferData.RedMask      = 0x00FF0000;
            framebufferData.GreenMask    = 0x0000FF00;
            framebufferData.BlueMask     = 0x000000FF;
            framebufferData.ReservedMask = 0xFF000000;
            break;

        case PixelBitMask:
            framebufferData.RedMask      = gop->Mode->Info->PixelInformation.RedMask;
            framebufferData.GreenMask    = gop->Mode->Info->PixelInformation.GreenMask;
            framebufferData.BlueMask     = gop->Mode->Info->PixelInformation.BlueMask;
            framebufferData.ReservedMask = gop->Mode->Info->PixelInformation.ReservedMask;
            break;

        default:
            break;
    }

    UefiVideoConfigureFramebufferCache();

    /* Print GOP framebuffer details */
    TRACE("UEFI GOP: Framebuffer initialized:\n");
    TRACE("  BaseAddress: 0x%lx\n", framebufferData.BaseAddress);
    TRACE("  BufferSize: 0x%x\n", framebufferData.BufferSize);
    TRACE("  Resolution: %dx%d\n", framebufferData.ScreenWidth, framebufferData.ScreenHeight);
    TRACE("  PixelsPerScanLine: %d\n", framebufferData.PixelsPerScanLine);
    TRACE("  PixelFormat: %d\n", framebufferData.PixelFormat);
    TRACE("  Masks: R=0x%08x G=0x%08x B=0x%08x\n",
          framebufferData.RedMask,
          framebufferData.GreenMask,
          framebufferData.BlueMask);
    
    /* Initialize console dimensions for software text rendering */
    MaxConsoleX = framebufferData.ScreenWidth / CHAR_WIDTH;
    MaxConsoleY = (framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT;
    ConsoleX = 0;
    ConsoleY = 0;
    GopConsoleInitialized = TRUE;
    TRACE("UEFI GOP: Console dimensions %dx%d chars\n", MaxConsoleX, MaxConsoleY);

    return Status;
}

VOID
UefiPrintFramebufferData(VOID)
{
    TRACE("Framebuffer BaseAddress       : %X\n", framebufferData.BaseAddress);
    TRACE("Framebuffer BufferSize        : %X\n", framebufferData.BufferSize);
    TRACE("Framebuffer ScreenWidth       : %d\n", framebufferData.ScreenWidth);
    TRACE("Framebuffer ScreenHeight      : %d\n", framebufferData.ScreenHeight);
    TRACE("Framebuffer PixelsPerScanLine : %d\n", framebufferData.PixelsPerScanLine);
    TRACE("Framebuffer PixelFormat       : %d\n", framebufferData.PixelFormat);
}

static ULONG
UefiVideoAttrToSingleColor(UCHAR Attr)
{
    UCHAR Intensity;
    Intensity = (0 == (Attr & 0x08) ? 127 : 255);

    return 0xff000000 |
           (0 == (Attr & 0x04) ? 0 : (Intensity << 16)) |
           (0 == (Attr & 0x02) ? 0 : (Intensity << 8)) |
           (0 == (Attr & 0x01) ? 0 : Intensity);
}

static VOID
UefiVideoAttrToColors(UCHAR Attr, ULONG *FgColor, ULONG *BgColor)
{
    *FgColor = UefiVideoAttrToSingleColor(Attr & 0xf);
    *BgColor = UefiVideoAttrToSingleColor((Attr >> 4) & 0xf);
}


static VOID
UefiVideoClearScreenColor(ULONG Color, BOOLEAN FullScreen)
{
    ULONG Delta;
    ULONG Line, Col;
    PULONG p;

    Delta = (framebufferData.PixelsPerScanLine * 4 + 3) & ~ 0x3;
    for (Line = 0; Line < framebufferData.ScreenHeight - (FullScreen ? 0 : 2 * TOP_BOTTOM_LINES); Line++)
    {
        p = (PULONG) ((char *) framebufferData.BaseAddress + (Line + (FullScreen ? 0 : TOP_BOTTOM_LINES)) * Delta);
        for (Col = 0; Col < framebufferData.ScreenWidth; Col++)
        {
            *p++ = Color;
        }
    }
}

VOID
UefiVideoClearScreen(UCHAR Attr)
{
    ULONG FgColor, BgColor;

    UefiVideoAttrToColors(Attr, &FgColor, &BgColor);
    UefiVideoClearScreenColor(BgColor, FALSE);
}

VOID
UefiVideoOutputChar(UCHAR Char, unsigned X, unsigned Y, ULONG FgColor, ULONG BgColor)
{
    PUCHAR FontPtr;
    PULONG Pixel;
    UCHAR Mask;
    unsigned Line;
    unsigned Col;
    ULONG Delta;
    Delta = (framebufferData.PixelsPerScanLine * 4 + 3) & ~ 0x3;
    FontPtr = BitmapFont8x16 + Char * 16;
    Pixel = (PULONG) ((char *) framebufferData.BaseAddress +
            (Y * CHAR_HEIGHT + TOP_BOTTOM_LINES) *  Delta + X * CHAR_WIDTH * 4);

    for (Line = 0; Line < CHAR_HEIGHT; Line++)
    {
        Mask = 0x80;
        for (Col = 0; Col < CHAR_WIDTH; Col++)
        {
            Pixel[Col] = (0 != (FontPtr[Line] & Mask) ? FgColor : BgColor);
            Mask = Mask >> 1;
        }
        Pixel = (PULONG) ((char *) Pixel + Delta);
    }
}

VOID
UefiVideoPutChar(int Ch, UCHAR Attr, unsigned X, unsigned Y)
{
    ULONG FgColor = 0;
    ULONG BgColor = 0;
    if (Ch != 0)
    {
        UefiVideoAttrToColors(Attr, &FgColor, &BgColor);
        UefiVideoOutputChar(Ch, X, Y, FgColor, BgColor);
    }
}

VOID
UefiVideoGetDisplaySize(PULONG Width, PULONG Height, PULONG Depth)
{
    *Width =  framebufferData.ScreenWidth / CHAR_WIDTH;
    *Height = (framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT;
    *Depth =  0;
}

VIDEODISPLAYMODE
UefiVideoSetDisplayMode(char *DisplayMode, BOOLEAN Init)
{
    /* We only have one mode, semi-text */
    return VideoTextMode;
}

ULONG
UefiVideoGetBufferSize(VOID)
{
    return ((framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT * (framebufferData.ScreenWidth / CHAR_WIDTH) * 2);
}

VOID
UefiVideoCopyOffScreenBufferToVRAM(PVOID Buffer)
{
    PUCHAR OffScreenBuffer = (PUCHAR)Buffer;

    ULONG Col, Line;
    for (Line = 0; Line < (framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT; Line++)
    {
        for (Col = 0; Col < framebufferData.ScreenWidth / CHAR_WIDTH; Col++)
        {
            UefiVideoPutChar(OffScreenBuffer[0], OffScreenBuffer[1], Col, Line);
            OffScreenBuffer += 2;
        }
    }
}

VOID
UefiVideoScrollUp(VOID)
{
    ULONG BgColor, Dummy;
    ULONG Delta;
    Delta = (framebufferData.PixelsPerScanLine * 4 + 3) & ~ 0x3;
    ULONG PixelCount = framebufferData.ScreenWidth * CHAR_HEIGHT *
                       (((framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT) - 1);
    PULONG Src = (PULONG)((PUCHAR)framebufferData.BaseAddress + (CHAR_HEIGHT + TOP_BOTTOM_LINES) * Delta);
    PULONG Dst = (PULONG)((PUCHAR)framebufferData.BaseAddress + TOP_BOTTOM_LINES * Delta);

    UefiVideoAttrToColors(ATTR(COLOR_WHITE, COLOR_BLACK), &Dummy, &BgColor);

    while (PixelCount--)
        *Dst++ = *Src++;

    for (PixelCount = 0; PixelCount < framebufferData.ScreenWidth * CHAR_HEIGHT; PixelCount++)
        *Dst++ = BgColor;
}

VOID
UefiVideoSetTextCursorPosition(UCHAR X, UCHAR Y)
{
    //TODO We don't have a cursor yet
}

VOID
UefiVideoHideShowTextCursor(BOOLEAN Show)
{
    //TODO We don't have a cursor yet
}

BOOLEAN
UefiVideoIsPaletteFixed(VOID)
{
    return 0;
}

VOID
UefiVideoSetPaletteColor(UCHAR Color, UCHAR Red,
                         UCHAR Green, UCHAR Blue)
{
    //Not supported
}

VOID
UefiVideoGetPaletteColor(UCHAR Color, UCHAR* Red,
                         UCHAR* Green, UCHAR* Blue)
{
    //Not supported
}

/* Software text rendering helpers for the GOP console */

/* Direct GOP console output function */
VOID
UefiGopConsolePutChar(CHAR Ch)
{
    ULONG FgColor, BgColor;
    
    if (!GopConsoleInitialized || framebufferData.BaseAddress == 0)
        return;
    
    /* Get current colors */
    UefiVideoAttrToColors(MachDefaultTextColor, &FgColor, &BgColor);
    
    /* Handle special characters */
    if (Ch == '\r')
    {
        ConsoleX = 0;
        return;
    }
    else if (Ch == '\n')
    {
        ConsoleX = 0;
        ConsoleY++;
    }
    else if (Ch == '\t')
    {
        ConsoleX = (ConsoleX + 8) & ~7;
    }
    else if (Ch == '\b')
    {
        if (ConsoleX > 0)
        {
            ConsoleX--;
            UefiVideoOutputChar(' ', ConsoleX, ConsoleY, FgColor, BgColor);
        }
    }
    else
    {
        /* Output normal character */
        UefiVideoOutputChar(Ch, ConsoleX, ConsoleY, FgColor, BgColor);
        ConsoleX++;
    }
    
    /* Handle line wrap */
    if (ConsoleX >= MaxConsoleX)
    {
        ConsoleX = 0;
        ConsoleY++;
    }
    
    /* Handle scrolling */
    if (ConsoleY >= MaxConsoleY)
    {
        UefiVideoScrollUp();
        ConsoleY = MaxConsoleY - 1;
    }
}

/* GOP console string output */
VOID
UefiGopConsolePutString(PCSTR String)
{
    if (!String)
        return;
        
    while (*String)
    {
        UefiGopConsolePutChar(*String);
        String++;
    }
}

/* Clear GOP console screen */
VOID
UefiGopConsoleClear(VOID)
{
    if (!GopConsoleInitialized || framebufferData.BaseAddress == 0)
        return;
    
    UefiVideoClearScreen(MachDefaultTextColor);
    ConsoleX = 0;
    ConsoleY = 0;
}

/* Set GOP console cursor position */
VOID
UefiGopConsoleSetCursor(UINT32 X, UINT32 Y)
{
    if (!GopConsoleInitialized)
        return;
        
    if (X < MaxConsoleX)
        ConsoleX = X;
    
    if (Y < MaxConsoleY)
        ConsoleY = Y;
}

/* Get GOP console status */
BOOLEAN
UefiGopConsoleIsInitialized(VOID)
{
    return GopConsoleInitialized && (framebufferData.BaseAddress != 0);
}

/*
 * Select a GOP video mode whose aspect ratio is closest to targetW:targetH.
 * On ties, prefer the mode with the larger pixel area. Skip PixelBltOnly modes.
 * Returns TRUE on success (or when already in the best mode), FALSE otherwise.
 */
static BOOLEAN
UefiSelectBestModeByAspect(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop,
                           UINT32 targetW,
                           UINT32 targetH)
{
    UINT32 i;
    UINT32 bestIndex = gop->Mode->Mode;
    BOOLEAN found = FALSE;
    UINT64 bestScore = ~0ULL; /* smaller is better */
    UINT64 targetRatio = (targetH != 0) ? ((UINT64)targetW * 100000ULL) / (UINT64)targetH : 0ULL;

    if (!gop || !gop->Mode || gop->Mode->MaxMode == 0)
        return FALSE;

    for (i = 0; i < (UINT32)gop->Mode->MaxMode; ++i)
    {
        EFI_STATUS st;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = NULL;
        UINTN infoSize = 0;

        st = gop->QueryMode(gop, i, &infoSize, &info);
        if (EFI_ERROR(st) || !info)
            continue;
        if (info->PixelFormat == PixelBltOnly)
            continue; /* no linear framebuffer */
        if (info->HorizontalResolution == 0 || info->VerticalResolution == 0)
            continue;

        /* Score by aspect closeness then by larger area */
        UINT32 w = info->HorizontalResolution;
        UINT32 h = info->VerticalResolution;
        UINT64 modeRatio = ((UINT64)w * 100000ULL) / (UINT64)h;
        UINT64 aspectDelta = (modeRatio > targetRatio) ? (modeRatio - targetRatio) : (targetRatio - modeRatio);
        UINT64 area = (UINT64)w * (UINT64)h;
        UINT64 score = (aspectDelta << 32) | (0xFFFFFFFFULL - (UINT64)(area & 0xFFFFFFFFULL));

        if (!found || score < bestScore)
        {
            bestScore = score;
            bestIndex = i;
            found = TRUE;
        }
    }

    if (!found)
        return FALSE;

    if (bestIndex == (UINT32)gop->Mode->Mode)
        return TRUE; /* already in best mode */

    if (EFI_ERROR(gop->SetMode(gop, bestIndex)))
        return FALSE;

    return TRUE;
}
/*
 * Try to set an exact GOP mode matching the given WxH. Skips PixelBltOnly.
 */
static BOOLEAN
UefiTrySetExactMode(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop,
                    UINT32 w,
                    UINT32 h)
{
    UINT32 i;
    if (!gop || !gop->Mode)
        return FALSE;

    for (i = 0; i < (UINT32)gop->Mode->MaxMode; ++i)
    {
        EFI_STATUS st;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = NULL;
        UINTN infoSize = 0;
        st = gop->QueryMode(gop, i, &infoSize, &info);
        if (EFI_ERROR(st) || !info)
            continue;
        if (info->PixelFormat == PixelBltOnly)
            continue;
        if (info->HorizontalResolution == w && info->VerticalResolution == h)
        {
            if (!EFI_ERROR(gop->SetMode(gop, i)))
            {
                TRACE("UEFI GOP: Forced exact mode %ux%u at index %u\n", w, h, i);
                return TRUE;
            }
            return FALSE;
        }
    }
    return FALSE;
}
static BOOLEAN
UefiParseEdidPreferred(const UINT8* Edid, UINT32 Size, UINT32* OutW, UINT32* OutH)
{
    const UINT8 Header[8] = {0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00};
    if (!Edid || Size < 128 || !OutW || !OutH) return FALSE;
    if (memcmp(Edid, Header, 8) != 0) return FALSE;

    /* First detailed timing descriptor at offset 54 */
    const UINT8* dtd = Edid + 54;
    UINT16 pclk = (UINT16)dtd[0] | ((UINT16)dtd[1] << 8);
    if (pclk == 0) return FALSE; /* no timing here */

    UINT32 ha = dtd[2] | ((UINT32)(dtd[4] & 0xF0) << 4);
    UINT32 va = dtd[5] | ((UINT32)(dtd[7] & 0xF0) << 4);
    if (ha == 0 || va == 0) return FALSE;
    *OutW = ha;
    *OutH = va;
    return TRUE;
}

static BOOLEAN
UefiGetPreferredResolutionFromEdid(UINT32* OutW, UINT32* OutH)
{
    EFI_STATUS Status;
    EFI_EDID_COMMON_PROTOCOL* EdidProto = NULL;
    if (!OutW || !OutH) return FALSE;

    /* Try Active EDID first */
    Status = GlobalSystemTable->BootServices->LocateProtocol(&EfiEdidActiveProtocolGuid, 0, (void**)&EdidProto);
    if (!EFI_ERROR(Status) && EdidProto && EdidProto->Edid && EdidProto->SizeOfEdid >= 128)
    {
        if (UefiParseEdidPreferred(EdidProto->Edid, EdidProto->SizeOfEdid, OutW, OutH))
            return TRUE;
    }

    /* Fallback to Discovered EDID */
    EdidProto = NULL;
    Status = GlobalSystemTable->BootServices->LocateProtocol(&EfiEdidDiscoveredProtocolGuid, 0, (void**)&EdidProto);
    if (!EFI_ERROR(Status) && EdidProto && EdidProto->Edid && EdidProto->SizeOfEdid >= 128)
    {
        if (UefiParseEdidPreferred(EdidProto->Edid, EdidProto->SizeOfEdid, OutW, OutH))
            return TRUE;
    }
    return FALSE;
}
