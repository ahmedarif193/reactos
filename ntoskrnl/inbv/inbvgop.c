/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI GOP-specific boot UI helpers (separate from legacy path)
 * Copyright : Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <reactos/arc/arc.h>
#include <drivers/bootvid/bootvid.h>
#include "logo.h"
#include "resource.h"
#include "inbvgop.h"
#include "reactos_gop_logo.h"
#include "uefi_center_mark.h"

extern BOOLEAN ShowProgressBar;

typedef struct tagBITMAPINFOHEADER
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
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

#ifndef BI_RGB
#define BI_RGB 0
#endif
#ifndef BI_RLE4
#define BI_RLE4 2
#endif
#define INBV_BMP_MAGIC              0x4D42
#define INBV_WORDMARK_SCALE         0.40f   /* wordmark drawn at 40% of source size */
#define INBV_WORDMARK_BOTTOM_MARGIN 32      /* pixels from screen bottom edge */
#define INBV_CENTERMARK_SCALE       0.05f   /* center mark drawn at 5% of source size */
#define INBV_CENTERMARK_VPOS        0.625f  /* vertical: 62.5% into lower half of screen */
#define INBV_SPINNER_STEPS          72      /* rotation steps per revolution (5 deg each) */
#define INBV_SPINNER_FRAME_MS       33      /* ~30 fps */
#define INBV_PI                     3.14159265f

typedef struct _INBV_GOP_RECT
{
    ULONG X;
    ULONG Y;
    ULONG Width;
    ULONG Height;
} INBV_GOP_RECT, *PINBV_GOP_RECT;

/* ---- Center-mark spinner state (persists beyond INIT) ---- */

typedef struct _INBV_SPINNER_STATE
{
    const UCHAR *Bits;
    ULONG SrcWidth, SrcHeight, SrcStride, BytesPerPixel;
    BOOLEAN TopDown;
    float SrcCX, SrcCY;
    float InvScale;

    LONG  DestCX, DestCY;
    ULONG BBoxHalf;           /* half-side of bounding square */

    ULONG RedMask, GreenMask, BlueMask;
    ULONG RedShift, GreenShift, BlueShift;
    ULONG RedMax, GreenMax, BlueMax;
} INBV_SPINNER_STATE;

static INBV_SPINNER_STATE g_Spinner;
static float   g_SinTable[INBV_SPINNER_STEPS];
static float   g_CosTable[INBV_SPINNER_STEPS];
static KEVENT  g_SpinnerStop;
static KEVENT  g_SpinnerDone;
static BOOLEAN g_SpinnerReady = FALSE;

extern VOID NTAPI InbvAcquireLock(VOID);
extern VOID NTAPI InbvReleaseLock(VOID);

CODE_SEG("INIT")
static
BOOLEAN
InbvGopQueryInfo(
    _Out_ LOADER_PARAMETER_FRAMEBUFFER *FbInfo)
{
    if (!FbInfo)
        return FALSE;

    RtlZeroMemory(FbInfo, sizeof(*FbInfo));

    if (!InbvGetGopFrameBufferInfo(FbInfo))
        return FALSE;

    if (FbInfo->FrameBufferSize == 0 ||
        FbInfo->HorizontalResolution == 0 ||
        FbInfo->VerticalResolution == 0)
    {
        return FALSE;
    }

    return TRUE;
}

static ULONG InbvMaskShift(ULONG Mask)
{
    ULONG Shift = 0;
    if (!Mask) return 0;
    while ((Mask & 1) == 0) { Shift++; Mask >>= 1; }
    return Shift;
}

static ULONG InbvMaskMax(ULONG Mask)
{
    ULONG Value = 0;
    while (Mask) { Value = (Value << 1) | 1; Mask >>= 1; }
    return Value;
}

static ULONG InbvPackColor(ULONG RedMask, ULONG GreenMask, ULONG BlueMask,
                           ULONG RedShift, ULONG GreenShift, ULONG BlueShift,
                           ULONG RedMax, ULONG GreenMax, ULONG BlueMax,
                           UCHAR r, UCHAR g, UCHAR b)
{
    ULONG R = RedMask   ? ((ULONG)r * RedMax   + 127) / 255 : 0;
    ULONG G = GreenMask ? ((ULONG)g * GreenMax + 127) / 255 : 0;
    ULONG B = BlueMask  ? ((ULONG)b * BlueMax  + 127) / 255 : 0;
    return (RedMask   ? ((R << RedShift)   & RedMask)   : 0) |
           (GreenMask ? ((G << GreenShift) & GreenMask) : 0) |
           (BlueMask  ? ((B << BlueShift)  & BlueMask)  : 0);
}

static USHORT
InbvReadU16(
    _In_reads_(2) const UCHAR *Ptr)
{
    return (USHORT)(Ptr[0] | ((USHORT)Ptr[1] << 8));
}

static ULONG
InbvReadU32(
    _In_reads_(4) const UCHAR *Ptr)
{
    return ((ULONG)Ptr[0]) |
           ((ULONG)Ptr[1] << 8) |
           ((ULONG)Ptr[2] << 16) |
           ((ULONG)Ptr[3] << 24);
}

/*
 * Bhaskara I sine approximation (max error < 0.2%).
 * Only uses basic float arithmetic -- no libm needed.
 */
static float
InbvApproxSin(float x)
{
    float sign = 1.0f, xpi;
    while (x < 0.0f) x += 2.0f * INBV_PI;
    while (x >= 2.0f * INBV_PI) x -= 2.0f * INBV_PI;
    if (x >= INBV_PI) { x -= INBV_PI; sign = -1.0f; }
    if (x > INBV_PI / 2.0f) x = INBV_PI - x;
    xpi = x * (INBV_PI - x);
    return sign * (16.0f * xpi) / (5.0f * INBV_PI * INBV_PI - 4.0f * xpi);
}

static
BOOLEAN
InbvGopComputeWordmarkRect(
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight,
    _Out_ PINBV_GOP_RECT Rect)
{
    const ULONG BitmapFileHeaderSize = 14; /* sizeof(BITMAPFILEHEADER) */
    PUCHAR Bitmap = (PUCHAR)g_ReactOSGopLogoBmp + BitmapFileHeaderSize;
    PBITMAPINFOHEADER Header;
    ULONG SrcWidth, SrcHeight;
    ULONG DstWidth, DstHeight;

    if (!Rect)
        return FALSE;

    Header = (PBITMAPINFOHEADER)Bitmap;
    if (!Header || Header->biPlanes != 1 || Header->biBitCount != 4)
        return FALSE;

    if (Header->biCompression != BI_RGB && Header->biCompression != BI_RLE4)
        return FALSE;

    SrcWidth = (ULONG)Header->biWidth;
    SrcHeight = (Header->biHeight < 0) ? (ULONG)(-Header->biHeight) : (ULONG)Header->biHeight;
    if (!SrcWidth || !SrcHeight)
        return FALSE;

    DstWidth = (ULONG)(SrcWidth * INBV_WORDMARK_SCALE + 0.5f);
    if (DstWidth == 0)
        DstWidth = 1;

    DstHeight = (ULONG)(SrcHeight * INBV_WORDMARK_SCALE + 0.5f);
    if (DstHeight == 0)
        DstHeight = 1;

    Rect->Width = DstWidth;
    Rect->Height = DstHeight;
    Rect->X = (ScreenWidth > DstWidth) ? ((ScreenWidth - DstWidth) / 2) : 0;
    Rect->Y = (ScreenHeight > DstHeight + INBV_WORDMARK_BOTTOM_MARGIN) ?
              (ScreenHeight - DstHeight - INBV_WORDMARK_BOTTOM_MARGIN) :
              0;
    return TRUE;
}

/*
 * Draw one rotated frame of the center mark.
 * Float math runs at PASSIVE_LEVEL (outside lock); only the blit is locked.
 */
static VOID
InbvGopSpinnerDrawFrame(ULONG Step)
{
    const INBV_SPINNER_STATE *S = &g_Spinner;
    float cosA = g_CosTable[Step];
    float sinA = g_SinTable[Step];
    static ULONG LineBuf[2048];
    ULONG fullW = S->BBoxHalf * 2;
    ULONG x0 = ((ULONG)S->DestCX > S->BBoxHalf) ? ((ULONG)S->DestCX - S->BBoxHalf) : 0;
    LONG  ry;

    if (fullW > RTL_NUMBER_OF(LineBuf))
        fullW = RTL_NUMBER_OF(LineBuf);

    for (ry = -(LONG)S->BBoxHalf; ry < (LONG)S->BBoxHalf; ry++)
    {
        LONG screenY = S->DestCY + ry;
        LONG rx;
        if (screenY < 0)
            continue;

        /* Zero the row — pixels outside the rotated source stay black */
        RtlZeroMemory(LineBuf, fullW * sizeof(ULONG));

        for (rx = -(LONG)S->BBoxHalf; rx < (LONG)S->BBoxHalf; rx++)
        {
            float srcFX = S->SrcCX + ((float)rx * cosA + (float)ry * sinA) * S->InvScale;
            float srcFY = S->SrcCY + (-(float)rx * sinA + (float)ry * cosA) * S->InvScale;
            LONG sx = (LONG)srcFX;
            LONG sy = (LONG)srcFY;
            ULONG effRow, idx;
            const UCHAR *Pixel;
            UCHAR b, g, r, a;

            if (sx < 0 || sy < 0 ||
                (ULONG)sx >= S->SrcWidth || (ULONG)sy >= S->SrcHeight)
                continue;

            effRow = S->TopDown ? (ULONG)sy : (S->SrcHeight - 1 - (ULONG)sy);
            Pixel = S->Bits + (SIZE_T)effRow * S->SrcStride +
                    (ULONG)sx * S->BytesPerPixel;
            b = Pixel[0]; g = Pixel[1]; r = Pixel[2];
            a = (S->BytesPerPixel == 4) ? Pixel[3] : 0xFF;
            if (a == 0)
                continue;

            idx = (ULONG)(rx + (LONG)S->BBoxHalf);
            if (idx < fullW)
                LineBuf[idx] = InbvPackColor(S->RedMask, S->GreenMask,
                                             S->BlueMask, S->RedShift,
                                             S->GreenShift, S->BlueShift,
                                             S->RedMax, S->GreenMax,
                                             S->BlueMax, r, g, b);
        }

        InbvAcquireLock();
        VidBufferToScreenBlt((PUCHAR)LineBuf, x0, (ULONG)screenY,
                             fullW, 1, fullW * sizeof(ULONG));
        InbvReleaseLock();
    }
}

static VOID NTAPI
InbvGopSpinnerThread(PVOID Context)
{
    ULONG Frame = 0;
    LARGE_INTEGER Delay;

    UNREFERENCED_PARAMETER(Context);
    Delay.QuadPart = -(LONGLONG)(INBV_SPINNER_FRAME_MS * 10000LL);

    while (KeReadStateEvent(&g_SpinnerStop) == 0)
    {
        InbvGopSpinnerDrawFrame(Frame);
        Frame = (Frame + 1) % INBV_SPINNER_STEPS;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    /* Erase mark area on exit */
    {
        const INBV_SPINNER_STATE *S = &g_Spinner;
        static ULONG ZeroBuf[2048];
        ULONG fullW = S->BBoxHalf * 2;
        ULONG x0 = ((ULONG)S->DestCX > S->BBoxHalf) ?
                    ((ULONG)S->DestCX - S->BBoxHalf) : 0;
        ULONG y0 = ((ULONG)S->DestCY > S->BBoxHalf) ?
                    ((ULONG)S->DestCY - S->BBoxHalf) : 0;
        ULONG row;

        if (fullW > RTL_NUMBER_OF(ZeroBuf))
            fullW = RTL_NUMBER_OF(ZeroBuf);
        RtlZeroMemory(ZeroBuf, fullW * sizeof(ULONG));

        InbvAcquireLock();
        for (row = 0; row < fullW; row++)
            VidBufferToScreenBlt((PUCHAR)ZeroBuf, x0, y0 + row,
                                 fullW, 1, fullW * sizeof(ULONG));
        InbvReleaseLock();
    }

    KeSetEvent(&g_SpinnerDone, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

CODE_SEG("INIT")
static BOOLEAN
InbvGopSpinnerSetup(
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight)
{
    const UCHAR *Base = g_UefiCenterMarkBmp;
    const ULONG BitmapSize = g_UefiCenterMarkBmpSize;
    BITMAPINFOHEADER Header;
    INBV_SPINNER_STATE *S = &g_Spinner;
    ULONG DstWidth, DstHeight, HalfHeight, DestX, DestY, i;
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    USHORT bfType;
    ULONG bfOffBits;

    if (!Base || BitmapSize < (14 + sizeof(BITMAPINFOHEADER)))
        return FALSE;

    bfType = InbvReadU16(Base);
    bfOffBits = InbvReadU32(Base + 10);
    if (bfType != INBV_BMP_MAGIC || bfOffBits >= BitmapSize)
        return FALSE;

    RtlCopyMemory(&Header, Base + 14, sizeof(Header));
    if (Header.biSize < sizeof(BITMAPINFOHEADER) ||
        Header.biPlanes != 1 ||
        Header.biCompression != BI_RGB ||
        (Header.biBitCount != 24 && Header.biBitCount != 32))
        return FALSE;

    if (Header.biWidth <= 0 || Header.biHeight == 0)
        return FALSE;

    S->SrcWidth      = (ULONG)Header.biWidth;
    S->SrcHeight     = (Header.biHeight < 0) ? (ULONG)(-Header.biHeight)
                                              : (ULONG)Header.biHeight;
    S->TopDown       = (Header.biHeight < 0);
    S->BytesPerPixel = Header.biBitCount / 8;
    S->SrcStride     = (ULONG)((((ULONGLONG)S->SrcWidth * Header.biBitCount
                                 + 31) & ~31ULL) >> 3);

    if ((ULONGLONG)S->SrcStride * S->SrcHeight >
        (ULONGLONG)(BitmapSize - bfOffBits))
        return FALSE;

    S->Bits     = Base + bfOffBits;
    S->SrcCX    = (float)S->SrcWidth  / 2.0f;
    S->SrcCY    = (float)S->SrcHeight / 2.0f;
    S->InvScale = 1.0f / INBV_CENTERMARK_SCALE;

    /* Destination size and position */
    DstWidth  = (ULONG)(S->SrcWidth  * INBV_CENTERMARK_SCALE + 0.5f);
    DstHeight = (ULONG)(S->SrcHeight * INBV_CENTERMARK_SCALE + 0.5f);
    if (DstWidth  == 0) DstWidth  = 1;
    if (DstHeight == 0) DstHeight = 1;
    if (DstWidth  > ScreenWidth)  DstWidth  = ScreenWidth;
    if (DstHeight > ScreenHeight) DstHeight = ScreenHeight;

    HalfHeight = ScreenHeight / 2;
    DestX = (ScreenWidth > DstWidth) ? ((ScreenWidth - DstWidth) / 2) : 0;
    DestY = (ULONG)(HalfHeight + (ScreenHeight - HalfHeight) *
                    INBV_CENTERMARK_VPOS);
    if (DestY > DstHeight / 2)
        DestY -= DstHeight / 2;
    else
        DestY = HalfHeight;
    if (DestY + DstHeight > ScreenHeight)
        DestY = ScreenHeight - DstHeight;

    S->DestCX   = (LONG)(DestX + DstWidth  / 2);
    S->DestCY   = (LONG)(DestY + DstHeight / 2);

    /* Bounding square for any rotation angle: diagonal / 2 + margin */
    {
        /* Integer sqrt via Newton iteration (no sqrtf in kernel) */
        ULONG d = DstWidth * DstWidth + DstHeight * DstHeight, s;
        if (d > 0)
        {
            s = d;
            while ((ULONGLONG)s * s > d)
                s = (s + d / s) / 2;
            S->BBoxHalf = s / 2 + 2;
        }
        else
        {
            S->BBoxHalf = 2;
        }
    }

    /* Cache framebuffer color info */
    if (!InbvGopQueryInfo(&FbInfo))
        return FALSE;

    S->RedMask   = FbInfo.RedMask;   S->GreenMask = FbInfo.GreenMask;
    S->BlueMask  = FbInfo.BlueMask;
    S->RedShift   = InbvMaskShift(S->RedMask);
    S->GreenShift = InbvMaskShift(S->GreenMask);
    S->BlueShift  = InbvMaskShift(S->BlueMask);
    S->RedMax     = InbvMaskMax(S->RedMask   >> S->RedShift);
    S->GreenMax   = InbvMaskMax(S->GreenMask >> S->GreenShift);
    S->BlueMax    = InbvMaskMax(S->BlueMask  >> S->BlueShift);

    /* Build sin/cos lookup table */
    for (i = 0; i < INBV_SPINNER_STEPS; i++)
    {
        float angle = (float)i * 2.0f * INBV_PI / (float)INBV_SPINNER_STEPS;
        g_SinTable[i] = InbvApproxSin(angle);
        g_CosTable[i] = InbvApproxSin(angle + INBV_PI / 2.0f);
    }

    return TRUE;
}

CODE_SEG("INIT")
static VOID
InbvGopSpinnerStart(
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight)
{
    HANDLE Thread;
    OBJECT_ATTRIBUTES ObjAttr;

    if (!InbvGopSpinnerSetup(ScreenWidth, ScreenHeight))
        return;

    KeInitializeEvent(&g_SpinnerStop, NotificationEvent, FALSE);
    KeInitializeEvent(&g_SpinnerDone, NotificationEvent, FALSE);
    g_SpinnerReady = TRUE;

    InitializeObjectAttributes(&ObjAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    if (NT_SUCCESS(PsCreateSystemThread(&Thread, 0, &ObjAttr, NULL, NULL,
                                        InbvGopSpinnerThread, NULL)))
    {
        ZwClose(Thread);
    }
    else
    {
        g_SpinnerReady = FALSE;
    }
}

VOID
NTAPI
InbvGopSpinnerStop(VOID)
{
    if (!g_SpinnerReady)
        return;

    KeSetEvent(&g_SpinnerStop, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&g_SpinnerDone, Executive, KernelMode, FALSE, NULL);
    g_SpinnerReady = FALSE;
}

static
BOOLEAN
InbvGopDrawWordmark(
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight,
    _In_opt_ const INBV_GOP_RECT *WordmarkRectOverride)
{
    const ULONG BitmapFileHeaderSize = 14; /* sizeof(BITMAPFILEHEADER) */
    PUCHAR Bitmap = (PUCHAR)g_ReactOSGopLogoBmp + BitmapFileHeaderSize;
    PBITMAPINFOHEADER Header;
    ULONG SrcWidth, SrcHeight;
    INBV_GOP_RECT WordmarkRect;
    ULONG DstWidth, DstHeight, DestX, DestY;
    BOOLEAN TopDown = FALSE;

    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    ULONG RedMask = 0, GreenMask = 0, BlueMask = 0;
    ULONG RedShift = 0, GreenShift = 0, BlueShift = 0;
    ULONG RedMax = 0, GreenMax = 0, BlueMax = 0;

    Header = (PBITMAPINFOHEADER)Bitmap;
    if (!Header || Header->biPlanes != 1 || Header->biBitCount != 4)
        return FALSE;

    if (Header->biCompression != BI_RGB && Header->biCompression != BI_RLE4)
        return FALSE;

    SrcWidth = (ULONG)Header->biWidth;
    SrcHeight = (Header->biHeight < 0) ? (ULONG)(-Header->biHeight) : (ULONG)Header->biHeight;
    TopDown = (Header->biHeight < 0);
    if (!SrcWidth || !SrcHeight)
        return FALSE;

    if (WordmarkRectOverride)
    {
        WordmarkRect = *WordmarkRectOverride;
    }
    else
    {
        if (!InbvGopComputeWordmarkRect(ScreenWidth, ScreenHeight, &WordmarkRect))
            return FALSE;
    }

    DstWidth = WordmarkRect.Width;
    DstHeight = WordmarkRect.Height;
    DestX = WordmarkRect.X;
    DestY = WordmarkRect.Y;

    if (!InbvGopQueryInfo(&FbInfo))
        return FALSE;

    RedMask = FbInfo.RedMask; GreenMask = FbInfo.GreenMask; BlueMask = FbInfo.BlueMask;
    RedShift = InbvMaskShift(RedMask); GreenShift = InbvMaskShift(GreenMask); BlueShift = InbvMaskShift(BlueMask);
    RedMax = InbvMaskMax(RedMask >> RedShift); GreenMax = InbvMaskMax(GreenMask >> GreenShift); BlueMax = InbvMaskMax(BlueMask >> BlueShift);

    /* Build 32-bit palette */
    ULONG PaletteCount = Header->biClrUsed ? Header->biClrUsed : 16;
    if (PaletteCount == 0)
        PaletteCount = 16;
    if (PaletteCount > 16)
        PaletteCount = 16;
    typedef struct _BMP_RGBQUAD { UCHAR b,g,r,a; } BMP_RGBQUAD;
    BMP_RGBQUAD* Pal = (BMP_RGBQUAD*)(Bitmap + Header->biSize);
    ULONG Palette32[16] = {0};
    for (ULONG i = 0; i < 16; i++)
    {
        UCHAR r = (i < PaletteCount) ? Pal[i].r : Pal[0].r;
        UCHAR g = (i < PaletteCount) ? Pal[i].g : Pal[0].g;
        UCHAR b = (i < PaletteCount) ? Pal[i].b : Pal[0].b;
        Palette32[i] = InbvPackColor(RedMask,GreenMask,BlueMask,RedShift,GreenShift,BlueShift,RedMax,GreenMax,BlueMax,r,g,b);
    }

    /* Locate pixel data */
    PUCHAR Bits = Bitmap + Header->biSize + PaletteCount * sizeof(BMP_RGBQUAD);
    LONG SrcDelta = ((SrcWidth + 1) / 2);
    SrcDelta = (SrcDelta + 3) & ~3;

    /* Helper to extract 8-bit channel from packed color */
#define INBV_EXTRACT(C,Mask,Shift,Maxv) \
    ((Mask) ? ((Maxv) ? ((((((C) & (Mask)) >> (Shift)) * 255) + ((Maxv)/2)) / (Maxv)) : 0) : 0)

    for (ULONG dy = 0; dy < DstHeight; dy++)
    {
        ULONG sy = (ULONG)min((ULONGLONG)SrcHeight - 1,
                              (ULONGLONG)(dy / INBV_WORDMARK_SCALE));
        ULONG effRow = TopDown ? sy : (SrcHeight - 1 - sy);
        PUCHAR Row = Bits + effRow * SrcDelta;

        /* Write scaled row in chunks to the screen using BootVID */
        static ULONG LineBuf[1024];
        ULONG produced = 0;
        while (produced < DstWidth)
        {
            ULONG toDo = min((ULONG)1024, DstWidth - produced);
            for (ULONG dx = 0; dx < toDo; dx++)
            {
                /* Map destination x back to source and do a simple 2x2 box filter for smoothing */
                ULONG sx0 = (ULONG)min((ULONGLONG)SrcWidth - 1,
                                       (ULONGLONG)((produced + dx) / INBV_WORDMARK_SCALE));
                ULONG sy0 = sy;
                ULONG sx1 = min(sx0 + 1, SrcWidth  - 1);
                ULONG sy1 = min(sy0 + 1, SrcHeight - 1);

                /* Fetch 4 neighboring indices */
                UCHAR b00 = Row[sx0 / 2];
                UCHAR i00 = (sx0 & 1) ? (b00 & 0x0F) : ((b00 >> 4) & 0x0F);

                UCHAR b10 = Row[sx1 / 2];
                UCHAR i10 = (sx1 & 1) ? (b10 & 0x0F) : ((b10 >> 4) & 0x0F);

                /* For y+1 we need the next source row */
                ULONG effRow1 = TopDown ? sy1 : (SrcHeight - 1 - sy1);
                PUCHAR Row1 = Bits + effRow1 * SrcDelta;

                UCHAR b01 = Row1[sx0 / 2];
                UCHAR i01 = (sx0 & 1) ? (b01 & 0x0F) : ((b01 >> 4) & 0x0F);
                UCHAR b11 = Row1[sx1 / 2];
                UCHAR i11 = (sx1 & 1) ? (b11 & 0x0F) : ((b11 >> 4) & 0x0F);

                /* Unpack to 8-bit RGB by reversing the screen format */
                ULONG c00 = Palette32[i00];
                ULONG c10 = Palette32[i10];
                ULONG c01 = Palette32[i01];
                ULONG c11 = Palette32[i11];

                /* Extract and normalize to 0..255 */
                ULONG r = (INBV_EXTRACT(c00, RedMask,   RedShift,   RedMax)   +
                           INBV_EXTRACT(c10, RedMask,   RedShift,   RedMax)   +
                           INBV_EXTRACT(c01, RedMask,   RedShift,   RedMax)   +
                           INBV_EXTRACT(c11, RedMask,   RedShift,   RedMax)) / 4;
                ULONG g = (INBV_EXTRACT(c00, GreenMask, GreenShift, GreenMax) +
                           INBV_EXTRACT(c10, GreenMask, GreenShift, GreenMax) +
                           INBV_EXTRACT(c01, GreenMask, GreenShift, GreenMax) +
                           INBV_EXTRACT(c11, GreenMask, GreenShift, GreenMax)) / 4;
                ULONG b = (INBV_EXTRACT(c00, BlueMask,  BlueShift,  BlueMax)  +
                           INBV_EXTRACT(c10, BlueMask,  BlueShift,  BlueMax)  +
                           INBV_EXTRACT(c01, BlueMask,  BlueShift,  BlueMax)  +
                           INBV_EXTRACT(c11, BlueMask,  BlueShift,  BlueMax)) / 4;

                LineBuf[dx] = InbvPackColor(RedMask,GreenMask,BlueMask,
                                             RedShift,GreenShift,BlueShift,
                                             RedMax,GreenMax,BlueMax,
                                             (UCHAR)r,(UCHAR)g,(UCHAR)b);
            }
            VidBufferToScreenBlt((PUCHAR)LineBuf,
                                 DestX + produced,
                                 DestY + dy,
                                 toDo,
                                 1,
                                 toDo * sizeof(ULONG));
            produced += toDo;
        }
    }

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
InbvGopHandleBootBitmap(
    _In_ BOOLEAN TextMode)
{
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    ULONG Width, Height;
    BOOLEAN BgrtActive;
    BOOLEAN WordmarkDrawn;

    if (!InbvGopQueryInfo(&FbInfo))
        return FALSE;

    Width = FbInfo.HorizontalResolution;
    Height = FbInfo.VerticalResolution;
    BgrtActive = InbvQueryBgrtInfo(NULL);
    UNREFERENCED_PARAMETER(BgrtActive);

    if (TextMode)
    {
        /* Debug/Text mode: raw text only, like legacy console. */
        InbvSetTextColor(BV_COLOR_WHITE);
        InbvSolidColorFill(0, 0, Width - 1, Height - 1, BV_COLOR_BLACK);
        InbvSetScrollRegion(0, 0, Width - 1, Height - 1);
        ShowProgressBar = FALSE;
        InbvEnableDisplayString(TRUE);
        return TRUE;
    }

    /* Non-debug: minimal quiet mode with just the ReactOS wordmark. */
    InbvSetTextColor(BV_COLOR_WHITE);
    /* Do not clear the framebuffer here: preserve any firmware BGRT */

    InbvGopSpinnerStart(Width, Height);
    WordmarkDrawn = InbvGopDrawWordmark(Width, Height, NULL);

    if (!WordmarkDrawn)
    {
        static const CHAR LoadingMsg[] = "ReactOS";
        const ULONG CharW = 8;
        const ULONG CharH = 13;
        ULONG msgPx = (ULONG)(sizeof(LoadingMsg) - 1) * CharW;
        ULONG x = (Width > msgPx) ? ((Width - msgPx) / 2) : 0;
        ULONG y = (Height > (CharH + 4)) ? (Height - (CharH + 4)) : 0;
        VidDisplayStringXY((PUCHAR)LoadingMsg, x, y, TRUE);
    }

    ShowProgressBar = FALSE;
    InbvEnableDisplayString(FALSE);
    return TRUE;
}
