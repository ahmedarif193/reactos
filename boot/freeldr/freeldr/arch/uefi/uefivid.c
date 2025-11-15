/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video output
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>
#include <Cpu.h>

#include <drivers/acpi/acpi.h>
#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/* Forward declarations for EDID helpers */
static BOOLEAN UefiParseEdidPreferred(const UINT8* Edid, UINT32 Size, UINT32* OutW, UINT32* OutH);
static BOOLEAN UefiGetPreferredResolutionFromEdid(UINT32* OutW, UINT32* OutH);
static BOOLEAN UefiSelectBestModeByAspect(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop,
                                          UINT32 targetW,
                                          UINT32 targetH);

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16
#define TOP_BOTTOM_LINES 0

/* Remember BGRT splash state so we can re-apply it when the text UI repaints. */
static PBGRT_TABLE gCachedBgrtTable = NULL;
static BOOLEAN gBgrtLogoDrawn = FALSE;
static BOOLEAN gBgrtRefreshInProgress = FALSE;
/* Protected BGRT rectangle in pixel coordinates; operations should avoid
 * touching this area so the firmware logo remains static. */
static BOOLEAN gBgrtRectValid = FALSE;
static UINT32 gBgrtLeft = 0, gBgrtTop = 0, gBgrtRight = 0, gBgrtBottom = 0;
#define LOWEST_SUPPORTED_RES 1

/* Preferred resolution bounds used when selecting a fallback mode */
#define PREFERRED_WIDTH_MIN  800
#define PREFERRED_WIDTH_MAX  1280
#define PREFERRED_HEIGHT_MIN 600
#define PREFERRED_HEIGHT_MAX 800

#define FALLBACK_CONSOLE_WIDTH   1280
#define FALLBACK_CONSOLE_HEIGHT   800
#define BGRT_BMP_MAGIC           0x4D42 /* 'BM' */
#define BI_RGB                   0
#ifndef BI_BITFIELDS
#define BI_BITFIELDS             3
#endif

extern PBGRT_TABLE GetBgrtTable(VOID);

#pragma pack(push, 1)
typedef struct _BMP_FILE_HEADER
{
    USHORT bfType;
    ULONG  bfSize;
    USHORT bfReserved1;
    USHORT bfReserved2;
    ULONG  bfOffBits;
} BMP_FILE_HEADER, *PBMP_FILE_HEADER;

typedef struct _BMP_INFO_HEADER
{
    ULONG  biSize;
    LONG   biWidth;
    LONG   biHeight;
    USHORT biPlanes;
    USHORT biBitCount;
    ULONG  biCompression;
    ULONG  biSizeImage;
    LONG   biXPelsPerMeter;
    LONG   biYPelsPerMeter;
    ULONG  biClrUsed;
    ULONG  biClrImportant;
} BMP_INFO_HEADER, *PBMP_INFO_HEADER;
#pragma pack(pop)

/* GLOBALS ********************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern UCHAR BitmapFont8x16[256 * 16];

UCHAR MachDefaultTextColor = COLOR_GRAY;
REACTOS_INTERNAL_BGCONTEXT framebufferData;
EFI_GUID EfiGraphicsOutputProtocol = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static BOOLEAN UefiGopFramebufferReady = FALSE;
static BOOLEAN FramebufferCacheAttempted = FALSE;
static BOOLEAN FramebufferCacheConfigured = FALSE;
static BOOLEAN FramebufferCacheWarned = FALSE;
static ULONG CachedLineStrideBytes = 0;

/* Helper to normalize a color channel using a bit mask to 0..255 */
static __inline UINT8
UefiExtract8FromMask(UINT32 value, UINT32 mask)
{
    UINT32 shift = 0, bits = 0, m = mask;
    if (!mask) return 0;
    while ((m & 1U) == 0U) { m >>= 1; shift++; }
    while ((m & 1U) == 1U) { m >>= 1; bits++; }
    if (bits == 0) return 0;
    {
        UINT32 v = (value & mask) >> shift;
        if (bits >= 8) return (UINT8)(v >> (bits - 8));
        return (UINT8)((v * 255U) / ((1U << bits) - 1U));
    }
}

BOOLEAN
UefiIsFramebufferReady(VOID)
{
    return UefiGopFramebufferReady &&
           framebufferData.BaseAddress != 0 &&
           framebufferData.BufferSize != 0;
}

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
SIZE_T UefiGopModeCount = 0;
ULONG UefiGopPreferredMode = 0;
PLOADER_PARAMETER_FRAMEBUFFER UefiGopFramebuffers = NULL;
ULONG UefiGopFramebufferCount = 0;
BOOLEAN UefiGopModesPermanent = FALSE;
BOOLEAN UefiGopFramebuffersPermanent = FALSE;
/* Forward declarations ******************************************************/
static VOID UefiVideoConfigureFramebufferCache(VOID);
static VOID UefiVideoInvalidateStrideCache(VOID);
static ULONG UefiVideoGetLineStride(VOID);
static VOID UefiVideoEnsureFramebufferCache(VOID);
static VOID UefiVideoWarnOnUncachedFramebuffer(VOID);
static VOID UefiClampResolutionBounds(UINT32* Width, UINT32* Height);
static BOOLEAN UefiComputeTargetConsoleResolution(UINT32* Width, UINT32* Height);
static BOOLEAN UefiFillFramebufferDescriptor(EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop,
                                             PLOADER_PARAMETER_FRAMEBUFFER Descriptor);
static BOOLEAN UefiSelectBestModeByAspect(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop,
                                          UINT32 targetW,
                                          UINT32 targetH);
static BOOLEAN UefiTrySetExactMode(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop,
                                   UINT32 w,
                                   UINT32 h);
static BOOLEAN UefiVideoBlitBgrtBitmap(PBGRT_TABLE Bgrt);
static inline BOOLEAN
UefiVideoBgrtRectIntersects(UINT32 Left, UINT32 Top, UINT32 Right, UINT32 Bottom)
{
    if (!gBgrtRectValid)
        return FALSE;
    if (Right < gBgrtLeft || Left > gBgrtRight || Bottom < gBgrtTop || Top > gBgrtBottom)
        return FALSE;
    return TRUE;
}

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

    FramebufferCacheAttempted = TRUE;
    FramebufferCacheConfigured = FALSE;

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

        if (Status == EFI_UNSUPPORTED || Status == EFI_ACCESS_DENIED)
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
                FramebufferCacheConfigured = TRUE;
            }
        }
        return;
    }

    TRACE("UEFI GOP: Framebuffer mapped with write-combining cache attribute\n");
    FramebufferCacheConfigured = TRUE;
}

static VOID
UefiVideoInvalidateStrideCache(VOID)
{
    CachedLineStrideBytes = 0;
}

static ULONG
UefiVideoGetLineStride(VOID)
{
    if (CachedLineStrideBytes == 0)
    {
        ULONG Pitch = framebufferData.PixelsPerScanLine * sizeof(ULONG);

        if (Pitch == 0)
            return 0;

        CachedLineStrideBytes = (Pitch + sizeof(ULONG) - 1) & ~(sizeof(ULONG) - 1);
    }

    return CachedLineStrideBytes;
}

static VOID
UefiVideoEnsureFramebufferCache(VOID)
{
    if (!FramebufferCacheAttempted)
        UefiVideoConfigureFramebufferCache();
}

static VOID
UefiVideoWarnOnUncachedFramebuffer(VOID)
{
    if (FramebufferCacheAttempted && !FramebufferCacheConfigured && !FramebufferCacheWarned)
    {
        TRACE("UEFI GOP: framebuffer writes are uncached\n");
        FramebufferCacheWarned = TRUE;
    }
}

static VOID
UefiClampResolutionBounds(UINT32* Width, UINT32* Height)
{
    UINT32 w = *Width;
    UINT32 h = *Height;

    if (w == 0 || h == 0)
        return;

    if (w > PREFERRED_WIDTH_MAX)
    {
        h = (UINT32)(((UINT64)h * PREFERRED_WIDTH_MAX) / w);
        if (h == 0)
            h = 1;
        w = PREFERRED_WIDTH_MAX;
    }

    if (h > PREFERRED_HEIGHT_MAX)
    {
        w = (UINT32)(((UINT64)w * PREFERRED_HEIGHT_MAX) / h);
        if (w == 0)
            w = 1;
        h = PREFERRED_HEIGHT_MAX;
    }

    if (w < PREFERRED_WIDTH_MIN)
    {
        h = (UINT32)(((UINT64)h * PREFERRED_WIDTH_MIN) / w);
        if (h == 0)
            h = 1;
        w = PREFERRED_WIDTH_MIN;
    }

    if (h < PREFERRED_HEIGHT_MIN)
    {
        w = (UINT32)(((UINT64)w * PREFERRED_HEIGHT_MIN) / h);
        if (w == 0)
            w = 1;
        h = PREFERRED_HEIGHT_MIN;
    }

    *Width = w;
    *Height = h;
}

static BOOLEAN
UefiComputeTargetConsoleResolution(UINT32* Width, UINT32* Height)
{
    UINT32 targetW = 0;
    UINT32 targetH = 0;

    if (UefiGetPreferredResolutionFromEdid(&targetW, &targetH))
    {
        UefiClampResolutionBounds(&targetW, &targetH);
    }
    else
    {
        targetW = FALLBACK_CONSOLE_WIDTH;
        targetH = FALLBACK_CONSOLE_HEIGHT;
    }

    if (targetW == 0 || targetH == 0)
        return FALSE;

    *Width = targetW;
    *Height = targetH;
    return TRUE;
}

static BOOLEAN
UefiFillFramebufferDescriptor(EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop,
                              PLOADER_PARAMETER_FRAMEBUFFER Descriptor)
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* ModeInfo;

    if (!Descriptor || !Gop || !Gop->Mode || !Gop->Mode->Info)
        return FALSE;

    ModeInfo = Gop->Mode->Info;
    if (ModeInfo->PixelFormat == PixelBltOnly)
    {
        TRACE("UEFI GOP: skipping PixelBltOnly framebuffer handle (Mode %u)\n",
              Gop->Mode->Mode);
        RtlZeroMemory(Descriptor, sizeof(*Descriptor));
        return FALSE;
    }

    RtlZeroMemory(Descriptor, sizeof(*Descriptor));

    Descriptor->FrameBufferBase.QuadPart = (ULONGLONG)Gop->Mode->FrameBufferBase;
    if (Gop->Mode->FrameBufferSize > MAXULONG)
        Descriptor->FrameBufferSize = MAXULONG;
    else
        Descriptor->FrameBufferSize = (ULONG)Gop->Mode->FrameBufferSize;

    Descriptor->HorizontalResolution = ModeInfo->HorizontalResolution;
    Descriptor->VerticalResolution = ModeInfo->VerticalResolution;
    Descriptor->PixelsPerScanLine = ModeInfo->PixelsPerScanLine;
    Descriptor->PixelFormat = ModeInfo->PixelFormat;

    switch (ModeInfo->PixelFormat)
    {
        case PixelRedGreenBlueReserved8BitPerColor:
            Descriptor->RedMask      = 0x000000FF;
            Descriptor->GreenMask    = 0x0000FF00;
            Descriptor->BlueMask     = 0x00FF0000;
            Descriptor->Reserved     = 0xFF000000;
            break;

        case PixelBlueGreenRedReserved8BitPerColor:
            Descriptor->RedMask      = 0x00FF0000;
            Descriptor->GreenMask    = 0x0000FF00;
            Descriptor->BlueMask     = 0x000000FF;
            Descriptor->Reserved     = 0xFF000000;
            break;

        case PixelBitMask:
            Descriptor->RedMask      = ModeInfo->PixelInformation.RedMask;
            Descriptor->GreenMask    = ModeInfo->PixelInformation.GreenMask;
            Descriptor->BlueMask     = ModeInfo->PixelInformation.BlueMask;
            Descriptor->Reserved     = ModeInfo->PixelInformation.ReservedMask;
            TRACE("UEFI GOP: PixelBitMask mode masks R=0x%08x G=0x%08x B=0x%08x Res=0x%08x\n",
                  Descriptor->RedMask,
                  Descriptor->GreenMask,
                  Descriptor->BlueMask,
                  Descriptor->Reserved);
            break;

        default:
            TRACE("UEFI GOP: unsupported pixel format %u while filling descriptor\n",
                  ModeInfo->PixelFormat);
            RtlZeroMemory(Descriptor, sizeof(*Descriptor));
            return FALSE;
    }

    return TRUE;
}

/* FUNCTIONS ******************************************************************/


EFI_STATUS
UefiInitializeVideo(VOID)
{
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* CurrentInfo;
    EFI_HANDLE* GopHandles = NULL;
    UINTN GopHandleCount = 0;
    UINTN PrimaryHandleIndex = 0;
    ULONG MaxFramebufferSlots = 0;
    BOOLEAN ModeChosen = FALSE;

    UefiGopFramebufferReady = FALSE;
    RtlZeroMemory(&framebufferData, sizeof(framebufferData));
    UefiVideoInvalidateStrideCache();
    FramebufferCacheAttempted = FALSE;
    FramebufferCacheConfigured = FALSE;
    FramebufferCacheWarned = FALSE;
    if (!GlobalSystemTable || !GlobalSystemTable->BootServices)
        return EFI_UNSUPPORTED;

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(ByProtocol,
                                                                 &EfiGraphicsOutputProtocol,
                                                                 NULL,
                                                                 &GopHandleCount,
                                                                 &GopHandles);
    if (EFI_ERROR(Status) || GopHandleCount == 0)
    {
        TRACE("Failed to enumerate GOP handles (Status %lx)\n", Status);
        if (GopHandles)
            GlobalSystemTable->BootServices->FreePool(GopHandles);
        return EFI_NOT_FOUND;
    }

    /* TODO: Prefer the handle backing ConOut when firmware exposes it. */
    for (PrimaryHandleIndex = 0; PrimaryHandleIndex < GopHandleCount; ++PrimaryHandleIndex)
    {
        Status = GlobalSystemTable->BootServices->HandleProtocol(GopHandles[PrimaryHandleIndex],
                                                                 &EfiGraphicsOutputProtocol,
                                                                 (VOID**)&gop);
        if (!EFI_ERROR(Status) && gop && gop->Mode)
            break;
    }

    if (EFI_ERROR(Status) || gop == NULL)
    {
        TRACE("Failed to acquire GOP protocol from any handle (Status %lx)\n", Status);
        GlobalSystemTable->BootServices->FreePool(GopHandles);
        return Status;
    }

    UefiGopFramebuffersPermanent = FALSE;
    UefiGopFramebufferCount = 0;
    if (GopHandleCount > 0)
    {
        SIZE_T framebufferSlots = GopHandleCount;
        SIZE_T framebufferBytes;

        if (framebufferSlots > ((SIZE_T)-1) / sizeof(LOADER_PARAMETER_FRAMEBUFFER))
        {
            TRACE("UEFI GOP: handle count too large (%lu); skipping framebuffer export\n",
                  (unsigned long)GopHandleCount);
            framebufferSlots = 0;
        }
        else
        {
            framebufferBytes = framebufferSlots * sizeof(LOADER_PARAMETER_FRAMEBUFFER);
            UefiGopFramebuffers = FrLdrHeapAlloc(framebufferBytes, 'pGFb');
            if (UefiGopFramebuffers)
            {
                RtlZeroMemory(UefiGopFramebuffers, framebufferBytes);
                MmSetMemoryType(UefiGopFramebuffers, framebufferBytes, LoaderMemoryData);
                UefiGopFramebuffersPermanent = TRUE;
                MaxFramebufferSlots = (framebufferSlots > MAXULONG) ? MAXULONG
                                                                   : (ULONG)framebufferSlots;
            }
            else
            {
                TRACE("UEFI GOP: failed to allocate %lu framebuffer descriptors\n",
                      (unsigned long)GopHandleCount);
            }
        }
    }

    TRACE("UEFI GOP: Protocol located successfully (handles=%lu, primary=%lu)\n",
          (unsigned long)GopHandleCount,
          (unsigned long)PrimaryHandleIndex);
    TRACE("  MaxMode: %d\n", gop->Mode->MaxMode);
    TRACE("  Current Mode: %d\n", gop->Mode->Mode);

    const BOOLEAN kAllowModeSwitch = FALSE;
    UINT32 targetWidth = 0;
    UINT32 targetHeight = 0;
    BOOLEAN haveTargetResolution = UefiComputeTargetConsoleResolution(&targetWidth, &targetHeight);

    if (kAllowModeSwitch && haveTargetResolution)
    {
        if (UefiTrySetExactMode(gop, targetWidth, targetHeight))
        {
            ModeChosen = TRUE;
        }
        else if (UefiSelectBestModeByAspect(gop, targetWidth, targetHeight))
        {
            ModeChosen = TRUE;
        }
    }

    /* Enumerate all GOP modes, cache minimal descriptors for the kernel */
    UefiGopModesPermanent = FALSE;
    if (gop->Mode->MaxMode > 0)
    {
        UINT32 i;
        SIZE_T modeBytes;
        SIZE_T modeCount;

        modeCount = gop->Mode->MaxMode;
        UefiGopModeCount = modeCount;
        UefiGopPreferredMode = (ULONG)gop->Mode->Mode;

        if (modeCount > 0 &&
            modeCount <= ((SIZE_T)-1) / sizeof(LOADER_PARAMETER_GOP_MODE))
        {
            modeBytes = modeCount * sizeof(LOADER_PARAMETER_GOP_MODE);
            UefiGopModes = FrLdrHeapAlloc(modeBytes, 'MPOG');
        }
        else
        {
            UefiGopModes = NULL;
        }

        if (UefiGopModes)
        {
            RtlZeroMemory(UefiGopModes, modeBytes);
            MmSetMemoryType(UefiGopModes, modeBytes, LoaderMemoryData);
            UefiGopModesPermanent = TRUE;
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
            TRACE("UEFI GOP: failed to allocate %lu mode descriptors\n",
                  (unsigned long)gop->Mode->MaxMode);
        }
    }
    
    /* Try EDID-based preferred/native selection; else use aspect-matched best */
    if (kAllowModeSwitch && !ModeChosen && haveTargetResolution)
    {
        if (UefiSelectBestModeByAspect(gop, targetWidth, targetHeight))
            ModeChosen = TRUE;
    }

    if (kAllowModeSwitch && !ModeChosen)
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
            if (GopHandles)
                GlobalSystemTable->BootServices->FreePool(GopHandles);
            return ModeStatus;
        }
    }

    /* Record the final firmware mode index after all SetMode attempts */
    UefiGopPreferredMode = (ULONG)gop->Mode->Mode;

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
    UefiVideoInvalidateStrideCache();
    FramebufferCacheAttempted = FALSE;
    FramebufferCacheConfigured = FALSE;
    FramebufferCacheWarned = FALSE;

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

    if (framebufferData.BaseAddress != 0 && framebufferData.BufferSize != 0)
    {
        UefiGopFramebufferReady = TRUE;
    }
    else
    {
        WARN("UEFI GOP: Framebuffer info missing after initialization\n");
    }

    if (MaxFramebufferSlots > 0 && UefiGopFramebuffers)
    {
        ULONG recorded = 0;

        if (gop && gop->Mode && gop->Mode->Info)
        {
            if (UefiFillFramebufferDescriptor(gop, &UefiGopFramebuffers[recorded]))
                recorded++;
        }

        for (UINTN handleIndex = 0;
             handleIndex < GopHandleCount && recorded < MaxFramebufferSlots;
             ++handleIndex)
        {
            EFI_GRAPHICS_OUTPUT_PROTOCOL* otherGop = NULL;

            if (handleIndex == PrimaryHandleIndex)
                continue;

            EFI_STATUS HandleStatus = GlobalSystemTable->BootServices->HandleProtocol(GopHandles[handleIndex],
                                                                                     &EfiGraphicsOutputProtocol,
                                                                                     (VOID**)&otherGop);
            if (EFI_ERROR(HandleStatus) || !otherGop || !otherGop->Mode || !otherGop->Mode->Info)
                continue;

            if (UefiFillFramebufferDescriptor(otherGop, &UefiGopFramebuffers[recorded]))
                recorded++;
        }

        UefiGopFramebufferCount = recorded;
    }

    if (GopHandles)
    {
        GlobalSystemTable->BootServices->FreePool(GopHandles);
        GopHandles = NULL;
    }

    return EFI_SUCCESS;
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
    ULONG Line;
    ULONG Pitch;
    ULONG VisibleHeight;
    SIZE_T RowBytes;
    PUCHAR Row;

    if (framebufferData.BaseAddress == 0)
        return;

    Pitch = UefiVideoGetLineStride();
    if (Pitch == 0)
        return;

    VisibleHeight = framebufferData.ScreenHeight - (FullScreen ? 0 : 2 * TOP_BOTTOM_LINES);
    RowBytes = (SIZE_T)framebufferData.ScreenWidth * sizeof(ULONG);
    Row = (PUCHAR)framebufferData.BaseAddress + (FullScreen ? 0 : TOP_BOTTOM_LINES) * Pitch;

    for (Line = 0; Line < VisibleHeight; Line++)
    {
        UINT32 PixelY = (FullScreen ? 0 : TOP_BOTTOM_LINES) + Line;
        if (gBgrtRectValid && PixelY >= gBgrtTop && PixelY <= gBgrtBottom)
        {
            SIZE_T LeftBytes = (SIZE_T)min((UINT32)framebufferData.ScreenWidth, gBgrtLeft) * sizeof(ULONG);
            if (LeftBytes > 0)
                RtlFillMemoryUlong(Row, LeftBytes, Color);
            if (gBgrtRight + 1 < (UINT32)framebufferData.ScreenWidth)
            {
                SIZE_T RightStart = ((SIZE_T)gBgrtRight + 1) * sizeof(ULONG);
                SIZE_T RightBytes = RowBytes - RightStart;
                if (RightBytes > 0)
                    RtlFillMemoryUlong(Row + RightStart, RightBytes, Color);
            }
        }
        else
        {
            RtlFillMemoryUlong(Row, RowBytes, Color);
        }
        Row += Pitch;
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
    ULONG Pitch;
    PUCHAR GlyphBase;
    unsigned Line;
    unsigned Col;

    if (framebufferData.BaseAddress == 0)
        return;

    Pitch = UefiVideoGetLineStride();
    if (Pitch == 0)
        return;

    UefiVideoEnsureFramebufferCache();
    UefiVideoWarnOnUncachedFramebuffer();

    /* If the glyph intersects the protected BGRT rectangle, skip drawing. */
    {
        UINT32 GlyphLeft = X * CHAR_WIDTH;
        UINT32 GlyphTop = (Y * CHAR_HEIGHT) + TOP_BOTTOM_LINES;
        UINT32 GlyphRight = GlyphLeft + CHAR_WIDTH - 1;
        UINT32 GlyphBottom = GlyphTop + CHAR_HEIGHT - 1;
        if (UefiVideoBgrtRectIntersects(GlyphLeft, GlyphTop, GlyphRight, GlyphBottom))
        {
            return;
        }
    }

    FontPtr = BitmapFont8x16 + Char * CHAR_HEIGHT;
    GlyphBase = (PUCHAR)framebufferData.BaseAddress +
                (Y * CHAR_HEIGHT + TOP_BOTTOM_LINES) * Pitch +
                X * CHAR_WIDTH * sizeof(ULONG);

    for (Line = 0; Line < CHAR_HEIGHT; Line++)
    {
        UCHAR Mask = 0x80;
        PULONG Pixel = (PULONG)GlyphBase;

        for (Col = 0; Col < CHAR_WIDTH; Col++)
        {
            Pixel[Col] = (FontPtr[Line] & Mask) ? FgColor : BgColor;
            Mask >>= 1;
        }

        GlyphBase += Pitch;
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
UefiVideoCopyOffScreenBufferRectToVRAM(
    PVOID Buffer,
    ULONG Left,
    ULONG Top,
    ULONG Right,
    ULONG Bottom)
{
    PUCHAR OffScreenBuffer = (PUCHAR)Buffer;
    ULONG GridWidth = framebufferData.ScreenWidth / CHAR_WIDTH;
    ULONG GridHeight = (framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT;

    if (Left > Right || Top > Bottom)
        return;

    if (GridWidth == 0 || GridHeight == 0)
        return;

    if (Right >= GridWidth) Right = GridWidth - 1;
    if (Bottom >= GridHeight) Bottom = GridHeight - 1;

    for (ULONG y = Top; y <= Bottom; ++y)
    {
        ULONG rowIndex = (y * GridWidth) * 2; /* 2 bytes per cell */
        for (ULONG x = Left; x <= Right; ++x)
        {
            ULONG cell = rowIndex + (x * 2);
            UCHAR ch = OffScreenBuffer[cell + 0];
            UCHAR at = OffScreenBuffer[cell + 1];
            UefiVideoPutChar(ch, at, x, y);
        }
    }

    /* Refresh BGRT only once per partial update. */
    UefiVideoRefreshBootLogo();
}

VOID
UefiVideoScrollUp(VOID)
{
    ULONG BgColor, Dummy;
    ULONG Pitch;
    ULONG VisiblePixelsY;
    SIZE_T RowBytes;
    PUCHAR Base;
    PUCHAR Src;
    PUCHAR Dst;
    SIZE_T CopyBytes;
    ULONG Line;

    if (framebufferData.BaseAddress == 0)
        return;

    Pitch = UefiVideoGetLineStride();
    if (Pitch == 0)
        return;

    VisiblePixelsY = framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES;
    if (VisiblePixelsY <= CHAR_HEIGHT)
        return;

    RowBytes = (SIZE_T)framebufferData.ScreenWidth * sizeof(ULONG);
    Base = (PUCHAR)framebufferData.BaseAddress;
    /* Replace bulk scroll with per-line copies excluding the BGRT area. */
    {
        ULONG CopyLines = VisiblePixelsY - CHAR_HEIGHT;
        for (Line = 0; Line < CopyLines; ++Line)
        {
            PUCHAR DstRow = Base + (TOP_BOTTOM_LINES + Line) * Pitch;
            PUCHAR SrcRow = DstRow + CHAR_HEIGHT * Pitch;
            UINT32 PixelY = TOP_BOTTOM_LINES + Line;

            if (gBgrtRectValid && PixelY >= gBgrtTop && PixelY <= gBgrtBottom)
            {
                SIZE_T LeftBytes = (SIZE_T)min((UINT32)framebufferData.ScreenWidth, gBgrtLeft) * sizeof(ULONG);
                if (LeftBytes > 0)
                    RtlMoveMemory(DstRow, SrcRow, LeftBytes);
                if (gBgrtRight + 1 < (UINT32)framebufferData.ScreenWidth)
                {
                    SIZE_T RightStart = ((SIZE_T)gBgrtRight + 1) * sizeof(ULONG);
                    SIZE_T RightBytes = RowBytes - RightStart;
                    if (RightBytes > 0)
                        RtlMoveMemory(DstRow + RightStart, SrcRow + RightStart, RightBytes);
                }
            }
            else
            {
                RtlMoveMemory(DstRow, SrcRow, RowBytes);
            }
        }
    }

    UefiVideoAttrToColors(ATTR(COLOR_WHITE, COLOR_BLACK), &Dummy, &BgColor);

    {
        PUCHAR DstRow = Base + (TOP_BOTTOM_LINES + (VisiblePixelsY - CHAR_HEIGHT)) * Pitch;
        for (Line = 0; Line < CHAR_HEIGHT; ++Line)
        {
            UINT32 PixelY = TOP_BOTTOM_LINES + (VisiblePixelsY - CHAR_HEIGHT) + Line;
            if (gBgrtRectValid && PixelY >= gBgrtTop && PixelY <= gBgrtBottom)
            {
                SIZE_T LeftBytes = (SIZE_T)min((UINT32)framebufferData.ScreenWidth, gBgrtLeft) * sizeof(ULONG);
                if (LeftBytes > 0)
                    RtlFillMemoryUlong(DstRow, LeftBytes, BgColor);
                if (gBgrtRight + 1 < (UINT32)framebufferData.ScreenWidth)
                {
                    SIZE_T RightStart = ((SIZE_T)gBgrtRight + 1) * sizeof(ULONG);
                    SIZE_T RightBytes = RowBytes - RightStart;
                    if (RightBytes > 0)
                        RtlFillMemoryUlong(DstRow + RightStart, RightBytes, BgColor);
                }
            }
            else
            {
                RtlFillMemoryUlong(DstRow, RowBytes, BgColor);
            }
            DstRow += Pitch;
        }
    }

    UefiVideoRefreshBootLogo();
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
                TRACE("UEFI GOP: switched to requested %ux%u mode (index %u)\n",
                      w,
                      h,
                      i);
                return TRUE;
            }

            return FALSE;
        }
    }

    return FALSE;
}

static BOOLEAN
UefiVideoBlitBgrtBitmap(PBGRT_TABLE Bgrt)
{
    const UINT8* Base;
    const BMP_FILE_HEADER* FileHeader;
    const BMP_INFO_HEADER* InfoHeader;
    const UINT8* PixelData;
    UINT32 Width, Height;
    BOOLEAN BottomUp;
    UINT32 BytesPerPixel;
    UINT32 SrcStride;
    UINT32 DestX, DestY;
    UINT32 CopyWidth, CopyHeight;
    ULONG Pitch;
    PUCHAR Framebuffer;
    BOOLEAN UseGopBlt = FALSE;

    if (!Bgrt)
    {
        TRACE("BGRT logo: no table provided\n");
        return FALSE;
    }

    if (Bgrt->LogoAddress == 0)
    {
        TRACE("BGRT logo: table reports null logo address\n");
        return FALSE;
    }

    if (!UefiIsFramebufferReady())
    {
        TRACE("BGRT logo: framebuffer not ready\n");
        return FALSE;
    }

    Base = (const UINT8*)(ULONG_PTR)Bgrt->LogoAddress;
    if (!Base)
    {
        TRACE("BGRT logo: logo base pointer null after translation\n");
        return FALSE;
    }

    if (framebufferData.ScreenWidth == 0 || framebufferData.ScreenHeight == 0)
    {
        TRACE("BGRT logo: framebuffer dimensions are zero (%u x %u)\n",
              framebufferData.ScreenWidth,
              framebufferData.ScreenHeight);
        return FALSE;
    }

    FileHeader = (const BMP_FILE_HEADER*)Base;
    if (FileHeader->bfType != BGRT_BMP_MAGIC)
    {
        TRACE("BGRT logo: bitmap header magic invalid (0x%04x)\n", FileHeader->bfType);
        return FALSE;
    }

    InfoHeader = (const BMP_INFO_HEADER*)(Base + sizeof(BMP_FILE_HEADER));
    if (InfoHeader->biSize < sizeof(BMP_INFO_HEADER))
    {
        TRACE("BGRT logo: info header too small (%lu)\n", InfoHeader->biSize);
        return FALSE;
    }

    if (InfoHeader->biPlanes != 1)
    {
        TRACE("BGRT logo: unsupported plane count %u\n", InfoHeader->biPlanes);
        return FALSE;
    }

    /* Accept BI_RGB (24/32 bpp) and BI_BITFIELDS (16/32 bpp masks) */
    if (InfoHeader->biCompression != BI_RGB && InfoHeader->biCompression != 3 /* BI_BITFIELDS */)
    {
        TRACE("BGRT logo: unsupported compression %lu\n", InfoHeader->biCompression);
        return FALSE;
    }

    if (InfoHeader->biCompression == BI_RGB)
    {
        if (InfoHeader->biBitCount != 24 && InfoHeader->biBitCount != 32)
        {
            TRACE("BGRT logo: unsupported bit depth %u\n", InfoHeader->biBitCount);
            return FALSE;
        }
    }
    else /* BI_BITFIELDS */
    {
        if (InfoHeader->biBitCount != 16 && InfoHeader->biBitCount != 32)
        {
            TRACE("BGRT logo: unsupported bit depth for bitfields %u\n", InfoHeader->biBitCount);
            return FALSE;
        }
    }

    BytesPerPixel = (InfoHeader->biBitCount + 7) / 8;
    Width = (InfoHeader->biWidth < 0) ? (UINT32)(-InfoHeader->biWidth) : (UINT32)InfoHeader->biWidth;
    Height = (InfoHeader->biHeight < 0) ? (UINT32)(-InfoHeader->biHeight) : (UINT32)InfoHeader->biHeight;
    BottomUp = (InfoHeader->biHeight > 0);

    if (Width == 0 || Height == 0)
    {
        TRACE("BGRT logo: invalid bitmap dimensions %u x %u\n", Width, Height);
        return FALSE;
    }

    if (FileHeader->bfOffBits < sizeof(BMP_FILE_HEADER) + InfoHeader->biSize)
    {
        TRACE("BGRT logo: pixel data offset too small (0x%lx)\n", FileHeader->bfOffBits);
        return FALSE;
    }

    PixelData = Base + FileHeader->bfOffBits;
    if (InfoHeader->biCompression == BI_RGB)
    {
        SrcStride = (InfoHeader->biBitCount == 32)
                        ? Width * 4
                        : (UINT32)(((UINT64)Width * 3ULL + 3ULL) & ~3ULL);
    }
    else /* BI_BITFIELDS */
    {
        /* Stride for 16/32 bpp with DWORD alignment */
        UINT32 bits = InfoHeader->biBitCount;
        SrcStride = (UINT32)((((UINT64)Width * bits + 31ULL) & ~31ULL) >> 3);
    }

    DestX = (Bgrt->OffsetX < framebufferData.ScreenWidth) ? Bgrt->OffsetX : framebufferData.ScreenWidth - 1;
    DestY = (Bgrt->OffsetY < framebufferData.ScreenHeight) ? Bgrt->OffsetY : framebufferData.ScreenHeight - 1;

    CopyWidth = (Width <= framebufferData.ScreenWidth - DestX)
                    ? Width
                    : framebufferData.ScreenWidth - DestX;
    CopyHeight = (Height <= framebufferData.ScreenHeight - DestY)
                    ? Height
                    : framebufferData.ScreenHeight - DestY;

    if (CopyWidth == 0 || CopyHeight == 0)
    {
        TRACE("BGRT logo: nothing to copy after clipping (dest=%u,%u size=%u x %u)\n",
              DestX,
              DestY,
              CopyWidth,
              CopyHeight);
        return FALSE;
    }

    Pitch = UefiVideoGetLineStride();
    Framebuffer = (PUCHAR)framebufferData.BaseAddress;
    if (Pitch == 0 || !Framebuffer || framebufferData.PixelFormat == PixelBltOnly)
    {
        /* Fallback to GOP->Blt path when no linear FB available */
        UseGopBlt = TRUE;
    }
    else
    {
        Framebuffer += (SIZE_T)DestY * Pitch + DestX * sizeof(ULONG);
    }

    if (!UseGopBlt)
    {
        /* Linear framebuffer write path */
        for (UINT32 Row = 0; Row < CopyHeight; ++Row)
        {
            UINT32 SrcRowIndex = BottomUp ? (Height - 1 - Row) : Row;
            const UINT8* Src = PixelData + (SIZE_T)SrcRowIndex * SrcStride;
            PULONG Dst = (PULONG)(Framebuffer + (SIZE_T)Row * Pitch);

            if (InfoHeader->biCompression == BI_RGB)
            {
                for (UINT32 Col = 0; Col < CopyWidth; ++Col)
                {
                    UINT8 Blue = Src[0];
                    UINT8 Green = Src[1];
                    UINT8 Red = Src[2];
                    UINT8 Alpha = (BytesPerPixel == 4) ? Src[3] : 0xFF;
                    if (!(BytesPerPixel == 4 && Alpha == 0))
                    {
                        ULONG Pixel = 0xFF000000 | ((ULONG)Red << 16) | ((ULONG)Green << 8) | (ULONG)Blue;
                        Dst[Col] = Pixel;
                    }
                    Src += BytesPerPixel;
                }
            }
            else /* BI_BITFIELDS */
            {
                const UINT32* Masks = (const UINT32*)(Base + sizeof(BMP_FILE_HEADER) + InfoHeader->biSize);
                UINT32 rMask = Masks[0], gMask = Masks[1], bMask = Masks[2];
                UINT32 aMask = 0; /* optional fourth mask */
                if (InfoHeader->biBitCount == 32 && FileHeader->bfOffBits >= sizeof(BMP_FILE_HEADER) + InfoHeader->biSize + sizeof(UINT32) * 4)
                {
                    aMask = Masks[3];
                }

                if (InfoHeader->biBitCount == 16)
                {
                    const UINT16* Src16 = (const UINT16*)Src;
                    for (UINT32 Col = 0; Col < CopyWidth; ++Col)
                    {
                        UINT32 v = Src16[Col];
                        UINT8 R = UefiExtract8FromMask(v, rMask);
                        UINT8 G = UefiExtract8FromMask(v, gMask);
                        UINT8 B = UefiExtract8FromMask(v, bMask);
                        ULONG Pixel = 0xFF000000 | ((ULONG)R << 16) | ((ULONG)G << 8) | (ULONG)B;
                        Dst[Col] = Pixel;
                    }
                }
                else /* 32bpp masks */
                {
                    const UINT32* Src32 = (const UINT32*)Src;
                    for (UINT32 Col = 0; Col < CopyWidth; ++Col)
                    {
                        UINT32 v = Src32[Col];
                        UINT8 R = UefiExtract8FromMask(v, rMask);
                        UINT8 G = UefiExtract8FromMask(v, gMask);
                        UINT8 B = UefiExtract8FromMask(v, bMask);
                        UINT8 A = (aMask ? UefiExtract8FromMask(v, aMask) : 0xFF);
                        if (A != 0)
                        {
                            ULONG Pixel = 0xFF000000 | ((ULONG)R << 16) | ((ULONG)G << 8) | (ULONG)B;
                            Dst[Col] = Pixel;
                        }
                    }
                }
            }
        }
    }
    else
    {
        /* GOP->Blt fallback path */
        EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
        EFI_STATUS st = GlobalSystemTable->BootServices->LocateProtocol(&EfiGraphicsOutputProtocol, NULL, (VOID**)&gop);
        if (EFI_ERROR(st) || !gop || !gop->Blt)
        {
            TRACE("BGRT logo: GOP->Blt unavailable for fallback\n");
            return FALSE;
        }

        /* Prepare per-row blits, honoring transparency by splitting into runs */
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL* RowBuf = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)FrLdrTempAlloc(CopyWidth * sizeof(*RowBuf), 'tRGB');
        if (!RowBuf)
            return FALSE;

        const UINT32* Masks = (const UINT32*)(Base + sizeof(BMP_FILE_HEADER) + InfoHeader->biSize);
        UINT32 rMask = 0, gMask = 0, bMask = 0, aMask = 0;
        if (InfoHeader->biCompression == BI_BITFIELDS)
        {
            rMask = Masks[0]; gMask = Masks[1]; bMask = Masks[2];
            if (InfoHeader->biBitCount == 32 && FileHeader->bfOffBits >= sizeof(BMP_FILE_HEADER) + InfoHeader->biSize + sizeof(UINT32) * 4)
                aMask = Masks[3];
        }
        for (UINT32 Row = 0; Row < CopyHeight; ++Row)
        {
            UINT32 SrcRowIndex = BottomUp ? (Height - 1 - Row) : Row;
            const UINT8* Src = PixelData + (SIZE_T)SrcRowIndex * SrcStride;

            /* Build row buffer and blit runs of non-transparent pixels */
            UINT32 runStart = 0;
            BOOLEAN inRun = FALSE;
            for (UINT32 Col = 0; Col <= CopyWidth; ++Col)
            {
                BOOLEAN opaque = FALSE;
                UINT8 R=0,G=0,B=0,A=0xFF;
                if (Col < CopyWidth)
                {
                    if (InfoHeader->biCompression == BI_RGB)
                    {
                        B = Src[0]; G = Src[1]; R = Src[2]; A = (BytesPerPixel == 4) ? Src[3] : 0xFF;
                        Src += BytesPerPixel;
                    }
                    else if (InfoHeader->biBitCount == 16)
                    {
                        UINT32 v = ((const UINT16*)Src)[0]; Src += 2;
                        R = UefiExtract8FromMask(v, rMask); G = UefiExtract8FromMask(v, gMask); B = UefiExtract8FromMask(v, bMask); A = 0xFF;
                    }
                    else /* 32bpp masks */
                    {
                        UINT32 v = ((const UINT32*)Src)[0]; Src += 4;
                        R = UefiExtract8FromMask(v, rMask); G = UefiExtract8FromMask(v, gMask); B = UefiExtract8FromMask(v, bMask); A = (aMask ? UefiExtract8FromMask(v, aMask) : 0xFF);
                    }
                    opaque = (A != 0);
                    RowBuf[Col].Red = R; RowBuf[Col].Green = G; RowBuf[Col].Blue = B; RowBuf[Col].Reserved = 0xFF;
                }

                if (opaque && !inRun) { inRun = TRUE; runStart = Col; }
                if ((!opaque && inRun) || (Col == CopyWidth && inRun))
                {
                    UINT32 runLen = Col - runStart;
                    if (runLen > 0)
                    {
                        EFI_STATUS bst = gop->Blt(gop,
                                                  &RowBuf[runStart],
                                                  EfiBltBufferToVideo,
                                                  0, 0,
                                                  DestX + runStart,
                                                  DestY + Row,
                                                  runLen,
                                                  1,
                                                  0);
                        (void)bst;
                    }
                    inRun = FALSE;
                }
            }
        }
        FrLdrTempFree(RowBuf, 'tRGB');
    }

    return TRUE;
}

BOOLEAN
UefiVideoDisplayBootLogo(VOID)
{
    PBGRT_TABLE Bgrt;

    TRACE("BGRT logo: display request (already drawn=%s)\n",
          gBgrtLogoDrawn ? "yes" : "no");

    if (gBgrtLogoDrawn)
        return TRUE;

    if (!UefiIsFramebufferReady())
    {
        TRACE("BGRT logo: framebuffer not ready when display requested\n");
        return FALSE;
    }

    Bgrt = GetBgrtTable();
    if (!Bgrt)
    {
        TRACE("BGRT logo: ACPI BGRT table not available\n");
        return FALSE;
    }

    TRACE("BGRT logo: table version=%u status=0x%02X imageType=%u offset=(%lu,%lu) addr=0x%llx\n",
          Bgrt->Version,
          Bgrt->Status,
          Bgrt->ImageType,
          Bgrt->OffsetX,
          Bgrt->OffsetY,
          Bgrt->LogoAddress);

    if (!(Bgrt->Status & BGRT_STATUS_IMAGE_VALID))
    {
        TRACE("BGRT logo: table status invalid (0x%02X)\n", Bgrt->Status);
        return FALSE;
    }

    if (!UefiVideoBlitBgrtBitmap(Bgrt))
    {
        TRACE("BGRT logo: bitmap blit routine failed\n");
        return FALSE;
    }

    gCachedBgrtTable = Bgrt;
    gBgrtLogoDrawn = TRUE;
    TRACE("BGRT logo: firmware splash drawn successfully\n");
    return TRUE;
}

BOOLEAN
UefiVideoIsBootLogoDrawn(VOID)
{
    return gBgrtLogoDrawn;
}

VOID
UefiVideoRefreshBootLogo(VOID)
{
    if (!gBgrtLogoDrawn || !gCachedBgrtTable)
        return;

    if (!UefiIsFramebufferReady())
        return;

    if (gBgrtRefreshInProgress)
        return;

    gBgrtRefreshInProgress = TRUE;
    if (!UefiVideoBlitBgrtBitmap(gCachedBgrtTable))
    {
        TRACE("BGRT logo: refresh blit failed\n");
    }
    gBgrtRefreshInProgress = FALSE;
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
